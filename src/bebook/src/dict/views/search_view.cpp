#include "./search_view.h"

#include "dict/match_detail.h"

#include "reader/conj_layout.h"
#include "reader/system_styling.h"
#include "reader/view_stack.h"
#include "reader/views/word_meaning_view.h"

#include "sys/keymap.h"
#include "sys/screen.h"

#include "text/font.h"

#include "reader/word_preview.h"

#include "util/sdl_font_cache.h"
#include "util/sdl_utils.h"

#include <algorithm>
#include <memory>
#include <set>

namespace
{

constexpr int MARGIN = 16;

// Three candidates is about what a mistyped Italian word needs, and it is what leaves the
// panel below room to answer in full rather than teasing.
constexpr int MAX_MATCHES = 3;
// With no conjugation to show, the freed lines go to senses instead.
constexpr size_t MAX_SENSES_NO_CONJ = 4;

// ASCII-only on-screen keyboard: lower-case QWERTY plus the apostrophe for Italian elisions
// (l', dell'). Words are searched folded, so ASCII finds the accented entries. Space isn't
// offered (multiword entries are filtered from results); backspace is on the Y/L1 buttons.
const std::vector<std::string> &keyboard()
{
    static const std::vector<std::string> kb = {
        "qwertyuiop",
        "asdfghjkl",
        "zxcvbnm'",
    };
    return kb;
}

int blit(SDL_Surface *dest, text::Font *font, const std::string &s, int x, int y,
         SDL_Color fg, SDL_Color bg)
{
    if (s.empty()) return 0;
    auto surf = surface_unique_ptr{ text::render_text_shaded(font, s.c_str(), fg, bg) };
    if (!surf) return 0;
    SDL_Rect dst = { static_cast<Sint16>(x), static_cast<Sint16>(y), 0, 0 };
    int w = surf->w;
    SDL_BlitSurface(surf.get(), nullptr, dest, &dst);
    return w;
}

} // namespace

SearchView::SearchView(
    const lexicon::LexiconService &lexicon,
    SystemStyling &styling,
    ViewStack &view_stack
)
    : lexicon(lexicon)
    , styling(styling)
    , view_stack(view_stack)
    , styling_sub_id(styling.subscribe_to_changes([this](SystemStyling::ChangeId) {
          _needs_render = true;
      }))
    // 50ms is one frame at TARGET_FPS 20, so this is as fast as the loop allows -- the old
    // 90ms quantised up to 100ms anyway, which is why 90 and 60 felt identical on device.
    , nav_throttle(180, 50)
    , backspace_throttle(300, 60)
{
}

SearchView::~SearchView()
{
    styling.unsubscribe_from_changes(styling_sub_id);
}

void SearchView::set_query(const std::string &q)
{
    query = q;
    refresh_results();
}

void SearchView::refresh_results()
{
    // Deduped by lemma, not by surface: "cane" matches the lemmas cane, cana, cagna and
    // cano, which are four different dictionary entries that happen to share a spelling.
    // Keying on the surface would show "cane cane cane" and hide the distinction the strip
    // exists to make. The first surface for each lemma is kept, so "faccio" stays the way
    // in to fare rather than the infinitive.
    const std::vector<lexicon::SearchHit> hits = lexicon.search(query);
    results.clear();
    std::set<std::string> seen;
    for (const auto &h : hits)
    {
        if (static_cast<int>(results.size()) >= MAX_MATCHES)
        {
            break;
        }
        if (seen.insert(h.lemma).second)
        {
            results.push_back(h);
        }
    }
    result_index = 0;
    rebuild_detail();
    _needs_render = true;
}

void SearchView::rebuild_detail()
{
    detail = Detail{};
    conj_tables.clear();

    if (result_index < 0 || result_index >= static_cast<int>(results.size()))
    {
        return;
    }

    const lexicon::SearchHit &hit = results[result_index];
    const std::vector<lexicon::LemmaEntry> analyses = lexicon.lemmatize(hit.word);

    // The reading whose lemma the search agreed on, so the panel explains the match the
    // user is actually looking at rather than an unrelated homograph.
    const lexicon::LemmaEntry *entry = nullptr;
    for (const auto &a : analyses)
    {
        if (a.lemma == hit.lemma) { entry = &a; break; }
    }
    if (entry == nullptr && !analyses.empty())
    {
        entry = &analyses.front();
    }

    const std::string lemma = entry ? entry->lemma : hit.lemma;
    const std::string features = entry ? entry->features : std::string();

    if (entry && entry->is_verb())
    {
        conj_tables = lexicon.conjugations(lemma);
    }
    detail.person = dict::person_index_for_features(features);
    detail.conj = dict::pick_table(conj_tables, dict::tense_key_for_features(features));

    // "io faccio -> fare · Indicativo Presente": the form, what it belongs to, and which
    // table is on screen. For a verb the tense names itself, so describe_morphology would
    // only repeat the person already spelled out in front.
    std::string headline;
    if (detail.person >= 0)
    {
        headline += std::string(lexicon::PERSON_LABELS[detail.person]) + " ";
    }
    headline += hit.word;
    if (lemma != hit.word)
    {
        headline += " \xE2\x86\x92 " + lemma;
    }
    const std::string tail = detail.conj
        ? detail.conj->display_name
        : (entry ? lexicon::describe_morphology(entry->pos, entry->features) : std::string());
    if (!tail.empty())
    {
        headline += "  \xC2\xB7  " + tail;
    }
    detail.headline = headline;

    const std::vector<lexicon::Sense> senses = lexicon.lookup_it_en(lemma);
    const size_t room = detail.conj ? 1 : MAX_SENSES_NO_CONJ;
    for (size_t i = 0; i < senses.size() && detail.senses.size() < room; ++i)
    {
        detail.senses.push_back(senses[i].gloss);
    }

    // Only readings of *this* lemma count. The strip already exposes the other lemmas a
    // surface can belong to, so counting all of them would offer the modal for every
    // homograph in order to show what is on screen already. "sono" still qualifies: essere
    // 1sg and essere 3pl are two readings of one lemma, and only the modal separates them.
    size_t readings = 0;
    for (const auto &a : analyses)
    {
        if (a.lemma == lemma) { ++readings; }
    }
    detail.more = dict::has_more_to_show(
        std::max<size_t>(readings, 1), conj_tables.size(), senses.size(), detail.senses.size());
}

void SearchView::cycle_match(int dir)
{
    const int n = static_cast<int>(results.size());
    if (n <= 1)
    {
        return;
    }
    result_index = ((result_index + dir) % n + n) % n;
    rebuild_detail();
    _needs_render = true;
}

void SearchView::type_char(char c)
{
    query.push_back(c);
    refresh_results();
}

void SearchView::backspace()
{
    if (!query.empty())
    {
        query.pop_back();
        refresh_results();
    }
}

void SearchView::activate_key()
{
    type_char(keyboard()[kb_row][kb_col]);
}

void SearchView::move_key(int dr, int dc)
{
    const auto &kb = keyboard();
    const int rows = static_cast<int>(kb.size());
    // Wrapping, so no press is ever a no-op: an edge that silently swallows input reads
    // as the app having stopped responding.
    kb_row = ((kb_row + dr) % rows + rows) % rows;
    const int cols = static_cast<int>(kb[kb_row].size());
    kb_col = ((kb_col + dc) % cols + cols) % cols;
    if (kb_col >= cols) { kb_col = cols - 1; }
    _needs_render = true;
}

void SearchView::open_selected()
{
    if (result_index < 0 || result_index >= static_cast<int>(results.size()))
    {
        return;
    }
    view_stack.push(std::make_shared<WordMeaningView>(
        results[result_index].word, lexicon, styling));
}

void SearchView::on_keypress(SDLKey key)
{
    switch (key)
    {
        case SW_BTN_UP:    move_key(-1, 0); break;
        case SW_BTN_DOWN:  move_key(1, 0); break;
        case SW_BTN_LEFT:  move_key(0, -1); break;
        case SW_BTN_RIGHT: move_key(0, 1); break;

        case SW_BTN_A:     activate_key(); break;
        case SW_BTN_B:     backspace(); break;

        // Cycle the matches without leaving the keyboard, which is the whole point: in
        // the common case there is no focus to move at all.
        case SW_BTN_L1:
        case SW_BTN_L2:    cycle_match(-1); break;
        case SW_BTN_R1:
        case SW_BTN_R2:    cycle_match(1); break;

        // Only offered when the modal has something the panel does not already show.
        case SW_BTN_Y:     if (detail.more) { open_selected(); } break;

        case SW_BTN_START: _is_done = true; break;
        default:           break;
    }
}

void SearchView::on_keyheld(SDLKey key, uint32_t held_time_ms)
{
    switch (key)
    {
        case SW_BTN_UP:
        case SW_BTN_DOWN:
        case SW_BTN_LEFT:
        case SW_BTN_RIGHT:
            if (nav_throttle(held_time_ms)) { on_keypress(key); }
            break;
        // Held backspace erases continuously. Its own throttle, because deleting
        // overshoots more easily than moving a cursor and wants a beat longer first.
        case SW_BTN_B:
            if (backspace_throttle(held_time_ms)) { backspace(); }
            break;
        default:
            break;
    }
}

bool SearchView::render(SDL_Surface *dest, bool force_render)
{
    if (!_needs_render && !force_render)
    {
        return false;
    }
    _needs_render = false;

    text::Font *font = styling.get_loaded_font();
    const auto &theme = styling.get_loaded_color_theme();
    const int line_h = text::font_line_height(font);
    auto mapc = [&](const SDL_Color &c) { return SDL_MapRGB(dest->format, c.r, c.g, c.b); };

    SDL_FillRect(dest, nullptr, mapc(theme.background));

    const int cx0 = MARGIN;
    const int cx1 = SCREEN_WIDTH - MARGIN;

    // --- Search bar -------------------------------------------------------------------
    int y = MARGIN;
    {
        const std::string shown = "\xF0\x9F\x94\x8D " + query;  // 🔍 (falls back to box if absent)
        blit(dest, font, query.empty() ? std::string("Cerca una parola\xE2\x80\xA6") : query,
             cx0, y, query.empty() ? theme.secondary_text : theme.main_text, theme.background);
        // caret
        int qw = 0;
        text::text_size(font, query.c_str(), &qw, nullptr);
        SDL_Rect caret = { static_cast<Sint16>(cx0 + qw + 1), static_cast<Sint16>(y),
                           2, static_cast<Uint16>(line_h) };
        SDL_FillRect(dest, &caret, mapc(theme.main_text));
        y += line_h + 4;
        SDL_Rect rule = { static_cast<Sint16>(cx0), static_cast<Sint16>(y),
                          static_cast<Uint16>(cx1 - cx0), 1 };
        SDL_FillRect(dest, &rule, mapc(theme.secondary_text));
        y += 6;
    }

    // --- Keyboard geometry (bottom) ---------------------------------------------------
    const auto &kb = keyboard();
    const int cell_w = (cx1 - cx0) / 10;
    const int cell_h = line_h + 6;
    const int kb_top = SCREEN_HEIGHT - MARGIN - static_cast<int>(kb.size()) * cell_h;

    // --- Match strip ------------------------------------------------------------------
    // All the candidates at once, active one pilled. L1/R1 cycle it; the panel below
    // follows, so choosing between matches never means leaving the keyboard.
    const int strip_y = y;
    if (results.empty())
    {
        if (!query.empty())
        {
            blit(dest, font, "Nessun risultato", cx0, strip_y,
                 theme.secondary_text, theme.background);
        }
    }
    else
    {
        int sx = cx0;
        for (int i = 0; i < static_cast<int>(results.size()); ++i)
        {
            const std::string &w = results[i].lemma;
            int ww = 0;
            text::text_size(font, w.c_str(), &ww, nullptr);

            const bool sel = (i == result_index);
            if (sel)
            {
                SDL_Rect pill = { static_cast<Sint16>(sx - 6), static_cast<Sint16>(strip_y - 1),
                                  static_cast<Uint16>(ww + 12), static_cast<Uint16>(line_h + 2) };
                SDL_FillRect(dest, &pill, mapc(theme.highlight_background));
            }
            blit(dest, font, w, sx, strip_y,
                 sel ? theme.highlight_text : theme.secondary_text,
                 sel ? theme.highlight_background : theme.background);
            sx += ww + 26;
        }

        if (results.size() > 1)
        {
            char pos[24];
            snprintf(pos, sizeof(pos), "%d/%d", result_index + 1,
                     static_cast<int>(results.size()));
            int pw = 0;
            text::text_size(font, pos, &pw, nullptr);
            blit(dest, font, pos, cx1 - pw, strip_y, theme.secondary_text, theme.background);
        }
    }
    y = strip_y + line_h + 4;
    {
        SDL_Rect rule = { static_cast<Sint16>(cx0), static_cast<Sint16>(y),
                          static_cast<Uint16>(cx1 - cx0), 1 };
        SDL_FillRect(dest, &rule, mapc(theme.secondary_text));
        y += 6;
    }

    // --- Detail panel -----------------------------------------------------------------
    if (!detail.headline.empty())
    {
        blit(dest, font, detail.headline, cx0, y, theme.main_text, theme.background);
        y += line_h;

        for (const auto &g : detail.senses)
        {
            blit(dest, font, g, cx0, y, theme.secondary_text, theme.background);
            y += line_h;
        }

        if (detail.conj != nullptr)
        {
            y += line_h / 3;

            // Widest pronoun and widest form, so the plural column clears the singular one
            // whatever the tense -- compound forms ("siamo andati") are far wider.
            int pron_w = 0;
            for (const char *label : lexicon::PERSON_LABELS)
            {
                int w = 0;
                text::text_size(font, label, &w, nullptr);
                pron_w = std::max(pron_w, w);
            }
            int widest_sing = 0;
            int widest_plur = 0;
            for (int i = 0; i < 3; ++i)
            {
                int w = 0;
                text::text_size(font, detail.conj->forms[i].c_str(), &w, nullptr);
                widest_sing = std::max(widest_sing, w);
                text::text_size(font, detail.conj->forms[i + 3].c_str(), &w, nullptr);
                widest_plur = std::max(widest_plur, w);
            }
            const int pron_col = pron_w + 12;

            // Shared with the reader's meaning popup; see reader/conj_layout.h. This panel
            // has no vertical room to fall back to six rows as the popup does, so when two
            // columns do not fit it still draws two -- at the even split conj::columns
            // returns -- and elides to cell_w. Y opens the untruncated table.
            const conj::Columns cols =
                conj::columns(pron_col, widest_sing, widest_plur, 28, cx1 - cx0);
            const int col2 = cols.plural_col;

            // Singular beside its plural: three rows for six persons.
            for (int i = 0; i < 3; ++i)
            {
                for (int half = 0; half < 2; ++half)
                {
                    const int idx = i + 3 * half;
                    const int bx = cx0 + half * col2;
                    // Mark the person the searched form actually is, so the eye lands on
                    // it. A fill rather than a text colour, because highlight_text equals
                    // main_text on some themes and would mark nothing at all.
                    const bool is_match = (idx == detail.person);
                    // Each column against its own width: the plural cell is the wider one,
                    // and sharing a single minimum elided "facciamo" though it fit.
                    const int cell_w = half == 0 ? cols.singular_cell_w : cols.plural_cell_w;
                    const std::string form =
                        text::elide_to_width(font, detail.conj->forms[idx], cell_w);
                    int fw = 0;
                    text::text_size(font, form.c_str(), &fw, nullptr);
                    if (is_match)
                    {
                        SDL_Rect pill = {
                            static_cast<Sint16>(bx + pron_col - 5), static_cast<Sint16>(y - 1),
                            static_cast<Uint16>(fw + 10), static_cast<Uint16>(line_h + 2) };
                        SDL_FillRect(dest, &pill, mapc(theme.highlight_background));
                    }
                    blit(dest, font, lexicon::PERSON_LABELS[idx], bx, y,
                         theme.secondary_text, theme.background);
                    blit(dest, font, form, bx + pron_col, y,
                         is_match ? theme.highlight_text : theme.main_text,
                         is_match ? theme.highlight_background : theme.background);
                }
                y += line_h;
            }
        }
    }

    // --- Hint line (just above the keyboard) ------------------------------------------
    // Only the keys that do something here: Y is absent whenever the modal would add
    // nothing to what is already on screen.
    {
        std::string hint = "A scrivi   B canc.";
        if (results.size() > 1) { hint += "   L/R voci"; }
        if (detail.more)        { hint += "   Y dettagli"; }
        hint += "   START esci";
        blit(dest, font, hint, cx0, kb_top - line_h - 2,
             theme.secondary_text, theme.background);
    }

    // --- Keyboard ---------------------------------------------------------------------
    for (int r = 0; r < static_cast<int>(kb.size()); ++r)
    {
        const int ky = kb_top + r * cell_h;
        const std::string &row = kb[r];
        for (int c = 0; c < static_cast<int>(row.size()); ++c)
        {
            const std::string label(1, row[c]);
            const int kx = cx0 + c * cell_w;
            const bool sel = (r == kb_row && c == kb_col);
            SDL_Rect cell = { static_cast<Sint16>(kx + 1), static_cast<Sint16>(ky + 1),
                              static_cast<Uint16>(cell_w - 2), static_cast<Uint16>(cell_h - 2) };
            SDL_FillRect(dest, &cell, mapc(sel ? theme.highlight_background : theme.background));
            int lw = 0;
            text::text_size(font, label.c_str(), &lw, nullptr);
            blit(dest, font, label, kx + (cell_w - lw) / 2, ky + 3,
                 sel ? theme.highlight_text : theme.main_text,
                 sel ? theme.highlight_background : theme.background);
        }
    }

    return true;
}

bool SearchView::is_done()
{
    return _is_done;
}
