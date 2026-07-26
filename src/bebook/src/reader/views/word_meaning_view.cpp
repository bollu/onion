#include "./word_meaning_view.h"

#include "reader/config.h"
#include "reader/conj_layout.h"
#include "lexicon/italian_article.h"
#include "reader/draw_modal_border.h"
#include "reader/system_styling.h"

#include "sys/keymap.h"
#include "sys/screen.h"

#include "text/font.h"
#include "text/styled_text.h"

#include "util/sdl_font_cache.h"
#include "util/sdl_utils.h"
#include "util/str_utils.h"

#include <algorithm>
#include <string>

namespace
{

// Content box: near-full-window, leaving the modal border + its padding as the margin.
constexpr int BOX_MARGIN = 10;
// Inner padding between the box border and the content, for a bit of breathing room.
constexpr int PAD_X = 16;
constexpr int PAD_Y = 12;

// Short tab label for a conjugation tense. The full display_name ("Indicativo Presente")
// is too wide for the cycler; these keep the strip compact.
std::string short_tense_label(const std::string &tense, const std::string &fallback)
{
    if (tense == "presente")         return "Presente";
    if (tense == "imperfetto")       return "Imperfetto";
    if (tense == "passato_prossimo") return "Passato Pross.";
    if (tense == "passato_remoto")   return "Passato Rem.";
    if (tense == "futuro_semplice")  return "Futuro";
    if (tense == "condizionale")     return "Condizionale";
    return fallback;
}

// Blit one short, unwrapped string at (x, top-y); returns its width. Used only for the
// tab strip's multi-coloured pieces, where the middle is highlighted. Body text and the
// header go through the wrapping/centering text engine instead.
int blit_line(
    SDL_Surface *dest, text::Font *font, const std::string &s,
    int x, int y, SDL_Color fg, SDL_Color bg
)
{
    if (s.empty())
    {
        return 0;
    }
    auto surf = surface_unique_ptr{ text::render_text_shaded(font, s.c_str(), fg, bg) };
    if (!surf)
    {
        return 0;
    }
    SDL_Rect dst = { static_cast<Sint16>(x), static_cast<Sint16>(y), 0, 0 };
    int w = surf->w;
    SDL_BlitSurface(surf.get(), nullptr, dest, &dst);
    return w;
}

} // namespace

WordMeaningView::WordMeaningView(
    const std::string &surface,
    const lexicon::LexiconService &lexicon,
    SystemStyling &styling
)
    : lexicon(lexicon)
    , styling(styling)
    , styling_sub_id(styling.subscribe_to_changes([this](SystemStyling::ChangeId) {
          _needs_render = true;
      }))
    , scroll_throttle(180, 40)
{
    load(surface);
}

void WordMeaningView::load(const std::string &s)
{
    surface = s;
    analyses.clear();
    suggestions.clear();
    suggestion_index = 0;

    // Resolve the surface to one or more analyses, gathering everything each lemma needs so
    // rendering is a pure read of already-fetched data.
    for (const auto &entry : lexicon.lemmatize(s))
    {
        Analysis a;
        a.lemma = entry;
        a.it_en = lexicon.lookup_it_en(entry.lemma);
        a.it_it = lexicon.lookup_it_it(entry.lemma);
        a.conj  = lexicon.conjugations(entry.lemma);
        analyses.push_back(std::move(a));
    }

    // Fallback: the form is unknown to the morphology table but may itself be a headword
    // (e.g. an uninflected lemma). Treat the surface as its own lemma if it has senses.
    if (analyses.empty())
    {
        const std::string key = to_lower(s);
        auto en = lexicon.lookup_it_en(key);
        auto it = lexicon.lookup_it_it(key);
        if (!en.empty() || !it.empty())
        {
            Analysis a;
            a.lemma = lexicon::LemmaEntry{ key, "", "", "" };
            a.it_en = std::move(en);
            a.it_it = std::move(it);
            analyses.push_back(std::move(a));
        }
    }

    // Still nothing: offer the closest known words ("did you mean").
    if (analyses.empty())
    {
        for (const auto &sg : lexicon.suggest(s))
        {
            suggestions.push_back(sg.lemma);
        }
    }

    active_analysis = analyses.empty() ? -1 : 0;
    rebuild_tabs();
    _needs_render = true;
}

WordMeaningView::~WordMeaningView()
{
    styling.unsubscribe_from_changes(styling_sub_id);
}

void WordMeaningView::rebuild_tabs()
{
    tabs.clear();
    active_tab = 0;
    body_scroll = 0;

    if (active_analysis < 0)
    {
        return;
    }

    const Analysis &a = analyses[active_analysis];

    // Only offer a definition tab that has content: the shipped build has no It->It glosses
    // yet, so an always-present "It -> It" tab would be a permanently empty dead end.
    if (!a.it_en.empty())
    {
        tabs.push_back({ TabKind::ItEn, "It \xE2\x86\x92 En", -1 });  // ->
    }
    if (!a.it_it.empty())
    {
        tabs.push_back({ TabKind::ItIt, "It \xE2\x86\x92 It", -1 });
    }

    if (a.lemma.is_verb())
    {
        for (int i = 0; i < static_cast<int>(a.conj.size()); ++i)
        {
            tabs.push_back({
                TabKind::Conj,
                short_tense_label(a.conj[i].tense, a.conj[i].display_name),
                i
            });
        }
    }
}

const lexicon::ConjTable *WordMeaningView::body_conj_table() const
{
    if (active_analysis < 0 || tabs.empty() || tabs[active_tab].kind != TabKind::Conj)
    {
        return nullptr;
    }
    return &analyses[active_analysis].conj[tabs[active_tab].conj_index];
}

std::vector<std::string> WordMeaningView::body_paragraphs() const
{
    std::vector<std::string> out;
    if (active_analysis < 0 || tabs.empty())
    {
        out.push_back("Nessuna voce.");
        return out;
    }

    const Analysis &a = analyses[active_analysis];

    auto add_senses = [&out](const std::vector<lexicon::Sense> &senses, const char *empty_msg) {
        if (senses.empty())
        {
            out.push_back(empty_msg);
            return;
        }
        int n = 1;
        for (const auto &s : senses)
        {
            // No blank between senses: glosses render on consecutive lines for a denser list.
            out.push_back(std::to_string(n++) + ".  " + s.gloss);
        }
    };

    switch (tabs[active_tab].kind)
    {
        case TabKind::ItEn:
            add_senses(a.it_en, "(nessuna definizione inglese)");
            break;
        case TabKind::ItIt:
            add_senses(a.it_it, "(nessuna definizione italiana)");
            break;
        case TabKind::Conj:
            break;  // body_conj_table() serves this tab
    }
    return out;
}

void WordMeaningView::move_analysis(int dir)
{
    if (analyses.size() < 2)
    {
        return;
    }
    const int n = static_cast<int>(analyses.size());
    active_analysis = ((active_analysis + dir) % n + n) % n;
    rebuild_tabs();
    _needs_render = true;
}

void WordMeaningView::move_tab(int dir)
{
    if (tabs.empty())
    {
        return;
    }
    const int n = static_cast<int>(tabs.size());
    active_tab = ((active_tab + dir) % n + n) % n;
    body_scroll = 0;
    _needs_render = true;
}

void WordMeaningView::scroll_body(int dir)
{
    body_scroll = std::max(0, body_scroll + dir);
    _needs_render = true;
}

bool WordMeaningView::render(SDL_Surface *dest, bool force_render)
{
    if (!_needs_render && !force_render)
    {
        return false;
    }
    _needs_render = false;

    text::Font *font = styling.get_loaded_font();
    const auto &theme = styling.get_loaded_color_theme();
    const int line_h = text::font_line_height(font);
    const int ascent = text::font_ascent(font);

    const int box_w = SCREEN_WIDTH - 2 * BOX_MARGIN - 2 * DIALOG_PADDING;
    const int box_h = SCREEN_HEIGHT - 2 * BOX_MARGIN - 2 * DIALOG_PADDING;
    draw_modal_border(box_w, box_h, theme, dest);

    const int box_x = SCREEN_WIDTH / 2 - box_w / 2;
    const int box_y = SCREEN_HEIGHT / 2 - box_h / 2;

    // Inner content column, inset from the box border for breathing room.
    const int cx0 = box_x + PAD_X;
    const int cx1 = box_x + box_w - PAD_X;
    const int cw = cx1 - cx0;

    SDL_Rect content_clip = {
        static_cast<Sint16>(box_x), static_cast<Sint16>(box_y),
        static_cast<Uint16>(box_w), static_cast<Uint16>(box_h)
    };
    SDL_SetClipRect(dest, &content_clip);

    auto map = [&](const SDL_Color &c) { return SDL_MapRGB(dest->format, c.r, c.g, c.b); };
    const Uint32 rule_color = map(theme.secondary_text);

    auto styled_of = [&](const std::string &s, const std::vector<text::StyleRun> *runs) {
        text::StyledText st;
        st.text = s.c_str();
        st.length = static_cast<uint32_t>(s.size());
        st.runs = runs;
        st.family = font->family;
        st.size_px = font->size_px;
        return st;
    };

    // Left-aligned, hyphenated, wrapped paragraph at (x, y_top) within `width`. Returns the
    // y below it. Empty text is a blank line. Goes through the shared reader text engine.
    auto draw_prose = [&](const std::string &s, const std::vector<text::StyleRun> *runs,
                          int x, int y_top, int width, SDL_Color fg) -> int {
        if (s.empty())
        {
            return y_top + line_h;
        }
        text::StyledText st = styled_of(s, runs);
        auto lines = text::layout_paragraph(st, font, width, text::Align::Left, true);
        int yy = y_top;
        for (const auto &ln : lines)
        {
            text::draw_line_aligned(dest, st, ln, x, width, yy + ascent,
                                    text::Align::Left, fg, theme.background, &content_clip);
            yy += line_h;
        }
        return yy;
    };

    auto rule = [&](int ry) {
        SDL_Rect r = { static_cast<Sint16>(cx0), static_cast<Sint16>(ry),
                       static_cast<Uint16>(cw), 1 };
        SDL_FillRect(dest, &r, rule_color);
    };

    int y = box_y + PAD_Y;

    // --- Header line 1: surface -> lemma (bold), then the first gloss beside it ---------
    //
    // The gloss shares this line rather than taking one of its own, and that is what leaves
    // the body six lines -- enough for a six-row conjugation table with no scroll. It is the
    // gloss that rides here, and not the morphology below, because the gloss is duplicated:
    // it is verbatim sense 1 of the It->En tab, one L/R press away, so eliding it costs
    // nothing. Morphology appears nowhere else and needs its full line (see below).
    //
    // Both halves are elided to a single line. Previously the head went through the wrapping
    // draw_prose, so a long surface->lemma silently ate a body line.
    {
        std::string lemma = active_analysis >= 0 ? analyses[active_analysis].lemma.lemma
                                                 : std::string();
        std::string head;
        std::vector<text::StyleRun> runs;
        if (lemma.empty())
        {
            head = surface;
        }
        else if (to_lower(surface) == to_lower(lemma))
        {
            // The selection already is the headword. A noun shows its article, which is how
            // a dictionary states gender without an abbreviation to decode; the article is
            // kept out of the bold run so the word itself still stands out.
            head = lemma;
            std::string article;
            if (active_analysis >= 0 && analyses[active_analysis].lemma.pos == "NOUN")
            {
                article = lexicon::definite_article(lemma, lexicon.noun_gender(lemma));
            }
            if (!article.empty())
            {
                head = (article.back() == '\'') ? article + lemma : article + " " + lemma;
            }
            runs.push_back({ static_cast<uint32_t>(head.size() - lemma.size()),
                             static_cast<uint32_t>(lemma.size()), text::Style::Bold });
        }
        else
        {
            head = surface + " \xE2\x86\x92 " + lemma;  // ->
            runs.push_back({ static_cast<uint32_t>(head.size() - lemma.size()),
                             static_cast<uint32_t>(lemma.size()), text::Style::Bold });
        }

        // The head keeps two thirds at most, so a long one cannot crowd the gloss out
        // entirely. Elision here is a last resort; ordinary entries are far shorter.
        const std::string head_shown = text::elide_to_width(font, head, cw * 2 / 3);
        if (head_shown != head)
        {
            runs.clear();  // offsets no longer address the shown text
        }
        text::StyledText st = styled_of(head_shown, runs.empty() ? nullptr : &runs);
        auto head_lines = text::layout_paragraph(st, font, cw, text::Align::Left, false);

        // measure_styled, not text_size: the lemma is drawn bold, and measuring the plain
        // string put the separator underneath the last glyph of the lemma.
        const int head_w = text::fixed_floor(
            text::measure_styled(st, 0, st.length) + text::FIXED_ONE - 1);
        if (!head_lines.empty())
        {
            text::draw_line_aligned(dest, st, head_lines.front(), cx0, cw, y + ascent,
                                    text::Align::Left, theme.main_text, theme.background,
                                    &content_clip);
        }

        if (active_analysis >= 0 && !analyses[active_analysis].it_en.empty())
        {
            // Bold is synthesised by emboldening, which adds ink without adding advance, so
            // the lemma's last glyph paints slightly past its measured width. Sit the
            // separator off it rather than flush against it.
            const int gap = PAD_X / 2;
            const std::string sep = "\xC2\xB7";  // ·
            int sep_w = 0;
            text::text_size(font, sep.c_str(), &sep_w, nullptr);

            const int sep_x = cx0 + head_w + gap;
            const int gloss_x = sep_x + sep_w + gap;
            const int gloss_w = cx1 - gloss_x;
            if (gloss_w > 0)
            {
                blit_line(dest, font, sep, sep_x, y, theme.secondary_text, theme.background);
                blit_line(dest, font,
                          text::elide_to_width(
                              font, analyses[active_analysis].it_en.front().gloss, gloss_w),
                          gloss_x, y, theme.secondary_text, theme.background);
            }
        }
        y += line_h;
    }

    // --- Header line 2: morphology, which gets the whole width ------------------------
    //
    // 59% of forms produce a morphology string past ~21 characters, and the longest,
    // "verbo · congiuntivo · imperfetto · lui/lei", measures 537px beside the ‹k/n› counter
    // in a 538px column. It exactly fills this line, so it can neither share one nor be
    // elided without losing content the popup shows nowhere else.
    if (active_analysis >= 0)
    {
        const int morph_y = y;
        const std::string &morph = analyses[active_analysis].lemma.morphology_human;
        y = morph.empty() ? y + line_h
                          : draw_prose(morph, nullptr, cx0, y, cw, theme.secondary_text);
        if (analyses.size() > 1)
        {
            char buf[24];
            snprintf(buf, sizeof(buf), "\xE2\x80\xB9%d/%zu\xE2\x80\xBA",  // <k/n>
                     active_analysis + 1, analyses.size());
            int w = 0;
            text::text_size(font, buf, &w, nullptr);
            blit_line(dest, font, buf, cx1 - w, morph_y, theme.secondary_text, theme.background);
        }
    }

    y += PAD_Y / 2;

    // --- No entry: fuzzy "did you mean" suggestions, as a selectable list --------------
    if (active_analysis < 0 && !suggestions.empty())
    {
        rule(y);
        y += 1 + PAD_Y / 2;
        draw_prose("Forse cercavi:", nullptr, cx0, y, cw, theme.secondary_text);
        y += line_h + PAD_Y / 4;

        for (int i = 0; i < static_cast<int>(suggestions.size()); ++i)
        {
            const bool sel = (i == suggestion_index);
            if (sel)
            {
                SDL_Rect hl = { static_cast<Sint16>(cx0 - 4), static_cast<Sint16>(y - 1),
                                static_cast<Uint16>(cw + 8), static_cast<Uint16>(line_h + 2) };
                SDL_FillRect(dest, &hl, map(theme.highlight_background));
            }
            blit_line(dest, font, suggestions[i], cx0, y,
                      sel ? theme.highlight_text : theme.main_text,
                      sel ? theme.highlight_background : theme.background);
            y += line_h;
        }

        const int hint_y = box_y + box_h - PAD_Y - line_h;
        rule(hint_y - PAD_Y / 2);
        draw_prose("\xE2\x86\x95 scegli   A apri   B indietro", nullptr,
                   cx0, hint_y, cw, theme.secondary_text);

        SDL_SetClipRect(dest, nullptr);
        return true;
    }

    // --- Tab strip: "Â« [Title] Â»" left-aligned, active title in a filled pill -----------
    //
    // Drawn only when there is somewhere to cycle to. With one tab it was a pill that
    // could not be left, a "1/1" counter saying nothing, and a rule -- three lines of
    // chrome above what is often a one-line answer. That space goes to the body.
    //
    // Left-aligned and with no rule above it: centred between two rules it read as a
    // formal banner across the popup. It now sits on the header's left margin, and the one
    // remaining rule is the one that matters -- the one dividing it from the meaning.
    if (tabs.size() > 1)
    {
        const std::string arrow_l = "\xC2\xAB";  // «  (Latin-1: Charis has it; ◂/▸ tofu)
        const std::string arrow_r = "\xC2\xBB";  // »
        const std::string &title = tabs[active_tab].title;
        const int pill_pad = 10;
        const int arrow_gap = 10;

        int wa = 0, wt = 0;
        text::text_size(font, arrow_l.c_str(), &wa, nullptr);
        text::text_size(font, title.c_str(), &wt, nullptr);
        const int pill_w = wt + 2 * pill_pad;

        int gx = cx0;
        blit_line(dest, font, arrow_l, gx, y, theme.secondary_text, theme.background);
        gx += wa + arrow_gap;

        SDL_Rect pill = { static_cast<Sint16>(gx), static_cast<Sint16>(y - 1),
                          static_cast<Uint16>(pill_w), static_cast<Uint16>(line_h + 2) };
        SDL_FillRect(dest, &pill, map(theme.highlight_background));
        blit_line(dest, font, title, gx + pill_pad, y, theme.highlight_text, theme.highlight_background);
        gx += pill_w + arrow_gap;

        blit_line(dest, font, arrow_r, gx, y, theme.secondary_text, theme.background);

        char pos[24];
        snprintf(pos, sizeof(pos), "%d/%zu", active_tab + 1, tabs.size());
        int pw = 0;
        text::text_size(font, pos, &pw, nullptr);
        blit_line(dest, font, pos, cx1 - pw, y, theme.secondary_text, theme.background);

        y += line_h;
        y += PAD_Y / 2;
    }

    // A single rule, immediately above the body: after the tab strip when there is one,
    // straight after the header when there is not.
    rule(y);
    y += 1 + PAD_Y / 2;

    // --- Body -------------------------------------------------------------------------
    const int hint_y = box_y + box_h - PAD_Y - line_h;
    const int lower_rule_y = hint_y - PAD_Y / 2;
    const int body_top = y;
    const int body_bottom = lower_rule_y - PAD_Y / 2;
    const int body_visible = std::max(1, (body_bottom - body_top) / line_h);

    const int scrollbar_w = 4;
    const int scrollbar_gutter = 6;
    const int body_w = cw - scrollbar_w - scrollbar_gutter;

    // Conjugation pronoun column: the widest pronoun label plus a gutter, so forms line up
    // and nothing clips at large font sizes (a fixed width would).
    int pron_col = 0;
    for (const char *label : lexicon::PERSON_LABELS)
    {
        int w = 0;
        text::text_size(font, label, &w, nullptr);
        pron_col = std::max(pron_col, w);
    }
    pron_col += PAD_X;

    // Lay the body out. A tab is either definitions or one conjugation table, so this
    // branches once on which, rather than flattening a list of tagged rows.
    const lexicon::ConjTable *conj_table = body_conj_table();

    std::vector<std::string> paragraphs;        // kept alive: StyledText holds c_str() into it
    std::vector<std::vector<text::Line>> layouts;
    struct ProseLine { int para; int line; };   // a wrapped line of paragraph `para`
    std::vector<ProseLine> prose_lines;

    conj::Columns cols = {};
    std::vector<conj::Row> conj_rows;

    if (conj_table != nullptr)
    {
        int widest_sing = 0;
        int widest_plur = 0;
        for (int i = 0; i < 3; ++i)
        {
            int w = 0;
            text::text_size(font, conj_table->forms[i].c_str(), &w, nullptr);
            widest_sing = std::max(widest_sing, w);
            text::text_size(font, conj_table->forms[i + 3].c_str(), &w, nullptr);
            widest_plur = std::max(widest_plur, w);
        }
        // A wider gutter than PAD_X between the columns: at PAD_X the singular form and the
        // plural pronoun read as one run of text. When two columns do not fit -- compound
        // tenses like "sono andato/andata" beside "siamo andati/andate" -- conj::rows falls
        // back to six single-person rows in natural order, which the body now has room for.
        cols = conj::columns(pron_col, widest_sing, widest_plur, PAD_X * 2, body_w);
        conj_rows = conj::rows(cols.two_columns);
    }
    else
    {
        paragraphs = body_paragraphs();
        layouts.reserve(paragraphs.size());
        for (int i = 0; i < static_cast<int>(paragraphs.size()); ++i)
        {
            text::StyledText st = styled_of(paragraphs[i], nullptr);
            layouts.push_back(text::layout_paragraph(st, font, body_w, text::Align::Left, true));
            for (int k = 0; k < static_cast<int>(layouts.back().size()); ++k)
            {
                prose_lines.push_back({ i, k });
            }
        }
    }

    const int total_lines = conj_table != nullptr ? static_cast<int>(conj_rows.size())
                                                  : static_cast<int>(prose_lines.size());
    const int max_scroll = std::max(0, total_lines - body_visible);
    if (body_scroll > max_scroll)
    {
        body_scroll = max_scroll;
    }

    for (int r = 0; r < body_visible; ++r)
    {
        const int idx = body_scroll + r;
        if (idx >= total_lines)
        {
            break;
        }
        const int ry = body_top + r * line_h;
        if (conj_table != nullptr)
        {
            const conj::Row &row = conj_rows[idx];
            blit_line(dest, font, lexicon::PERSON_LABELS[row.left], cx0, ry,
                      theme.secondary_text, theme.background);
            blit_line(dest, font, conj_table->forms[row.left], cx0 + pron_col, ry,
                      theme.main_text, theme.background);
            if (row.right >= 0)
            {
                blit_line(dest, font, lexicon::PERSON_LABELS[row.right],
                          cx0 + cols.plural_col, ry, theme.secondary_text, theme.background);
                blit_line(dest, font, conj_table->forms[row.right],
                          cx0 + cols.plural_col + pron_col, ry,
                          theme.main_text, theme.background);
            }
        }
        else
        {
            const ProseLine &p = prose_lines[idx];
            text::StyledText st = styled_of(paragraphs[p.para], nullptr);
            text::draw_line_aligned(dest, st, layouts[p.para][p.line], cx0, body_w,
                                    ry + ascent, text::Align::Left,
                                    theme.main_text, theme.background, &content_clip);
        }
    }

    // Slim scrollbar (thumb only) at the right gutter when the body overflows.
    if (total_lines > body_visible)
    {
        const int track_h = body_visible * line_h;
        const int thumb_h = std::max(line_h, track_h * body_visible / total_lines);
        const int thumb_y = body_top + (track_h - thumb_h) * body_scroll / max_scroll;
        SDL_Rect thumb = { static_cast<Sint16>(cx1 - scrollbar_w), static_cast<Sint16>(thumb_y),
                           static_cast<Uint16>(scrollbar_w), static_cast<Uint16>(thumb_h) };
        SDL_FillRect(dest, &thumb, map(theme.secondary_text));
    }

    // --- Hint bar ---------------------------------------------------------------------
    rule(lower_rule_y);
    {
        // Only advertise what this word actually offers. A noun with one tab and one
        // analysis has nothing to cycle, and a prompt for a key that does nothing is
        // worse than no prompt.
        std::string hint;
        if (tabs.size() > 1)
        {
            hint += "\xE2\x86\x94 schede   ";
        }
        if (max_scroll > 0)
        {
            hint += "\xE2\x86\x95 scorri   ";
        }
        if (analyses.size() > 1)
        {
            hint += "L/R sensi   ";
        }
        hint += "B indietro";
        draw_prose(hint, nullptr, cx0, hint_y, cw, theme.secondary_text);
    }

    SDL_SetClipRect(dest, nullptr);
    return true;
}

bool WordMeaningView::is_done()
{
    return _is_done;
}

bool WordMeaningView::is_modal()
{
    return true;
}

void WordMeaningView::on_keypress(SDLKey key)
{
    // "Did you mean" list: up/down move the cursor, A opens the picked word.
    if (active_analysis < 0 && !suggestions.empty())
    {
        const int n = static_cast<int>(suggestions.size());
        switch (key)
        {
            case SW_BTN_B:
                _is_done = true;
                break;
            case SW_BTN_UP:
                suggestion_index = (suggestion_index - 1 + n) % n;
                _needs_render = true;
                break;
            case SW_BTN_DOWN:
                suggestion_index = (suggestion_index + 1) % n;
                _needs_render = true;
                break;
            case SW_BTN_A:
                load(suggestions[suggestion_index]);
                break;
            default:
                break;
        }
        return;
    }

    switch (key)
    {
        case SW_BTN_B:
            _is_done = true;
            break;
        // The d-pad moves within one entry; the shoulders move between entries. "ne" has
        // several unrelated readings and cycling them was on X, which is not where a hand
        // reaches for "show me the next one".
        case SW_BTN_LEFT:
            move_tab(-1);
            break;
        case SW_BTN_RIGHT:
            move_tab(1);
            break;
        case SW_BTN_L1:
        case SW_BTN_L2:
            move_analysis(-1);
            break;
        case SW_BTN_R1:
        case SW_BTN_R2:
            move_analysis(1);
            break;
        case SW_BTN_UP:
            scroll_body(-1);
            break;
        case SW_BTN_DOWN:
            scroll_body(1);
            break;
        case SW_BTN_X:
            move_analysis(1);
            break;
        default:
            break;
    }
}

void WordMeaningView::on_keyheld(SDLKey key, uint32_t held_time_ms)
{
    if ((key == SW_BTN_UP || key == SW_BTN_DOWN) && scroll_throttle(held_time_ms))
    {
        if (active_analysis < 0 && !suggestions.empty())
        {
            on_keypress(key);  // move the suggestion cursor
        }
        else
        {
            scroll_body(key == SW_BTN_UP ? -1 : 1);
        }
    }
}
