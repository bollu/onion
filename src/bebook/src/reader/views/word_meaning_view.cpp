#include "./word_meaning_view.h"

#include "reader/config.h"
#include "reader/draw_modal_border.h"
#include "reader/system_styling.h"

#include "sys/keymap.h"
#include "sys/screen.h"

#include "text/font.h"

#include "util/sdl_font_cache.h"
#include "util/sdl_utils.h"
#include "util/str_utils.h"

#include <algorithm>

namespace
{

// Content box: near-full-window, leaving the modal border + its padding as the margin.
constexpr int BOX_MARGIN = 10;

// Short tab label for a conjugation tense. The full display_name ("Indicativo Presente")
// is too wide for the cycler; these keep the strip compact.
std::string short_tense_label(const std::string &tense, const std::string &fallback)
{
    if (tense == "presente")         return "Presente";
    if (tense == "imperfetto")       return "Imperfetto";
    if (tense == "passato_prossimo") return "Passato Pross.";
    if (tense == "futuro_semplice")  return "Futuro";
    if (tense == "condizionale")     return "Condizionale";
    return fallback;
}

// Blit one line of text at (x, baseline_top_y), returning its width. Empty text is a
// no-op returning 0, matching how render_text_shaded guards.
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
    , surface(surface)
    , scroll_throttle(250, 60)
{
    // Resolve the surface to one or more analyses, gathering everything each lemma needs so
    // rendering is a pure read of already-fetched data.
    for (const auto &entry : lexicon.lemmatize(surface))
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
        const std::string key = to_lower(surface);
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

    active_analysis = analyses.empty() ? -1 : 0;
    rebuild_tabs();
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

    tabs.push_back({ TabKind::ItEn, "It \xE2\x86\x92 En", -1 });  // ->
    tabs.push_back({ TabKind::ItIt, "It \xE2\x86\x92 It", -1 });

    const Analysis &a = analyses[active_analysis];
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

std::vector<WordMeaningView::BodyRow> WordMeaningView::body_rows() const
{
    std::vector<BodyRow> rows;
    if (active_analysis < 0 || tabs.empty())
    {
        rows.push_back({ "No dictionary entry.", "" });
        return rows;
    }

    const Analysis &a = analyses[active_analysis];
    const Tab &tab = tabs[active_tab];

    auto add_senses = [&rows](const std::vector<lexicon::Sense> &senses, const char *empty_msg) {
        if (senses.empty())
        {
            rows.push_back({ empty_msg, "" });
            return;
        }
        for (const auto &s : senses)
        {
            rows.push_back({ "\xE2\x80\xA2 " + s.gloss, "" });  // bullet
        }
    };

    switch (tab.kind)
    {
        case TabKind::ItEn:
            add_senses(a.it_en, "(no English definition)");
            break;
        case TabKind::ItIt:
            add_senses(a.it_it, "(nessuna definizione)");
            break;
        case TabKind::Conj:
        {
            const lexicon::ConjTable &t = a.conj[tab.conj_index];
            for (int i = 0; i < 6; ++i)
            {
                rows.push_back({ lexicon::PERSON_LABELS[i], t.forms[i] });
            }
            break;
        }
    }
    return rows;
}

void WordMeaningView::move_tab(int dir)
{
    if (tabs.empty())
    {
        return;
    }
    const int n = static_cast<int>(tabs.size());
    active_tab = (active_tab + dir % n + n) % n;
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

    const int box_w = SCREEN_WIDTH - 2 * BOX_MARGIN - 2 * DIALOG_PADDING;
    const int box_h = SCREEN_HEIGHT - 2 * BOX_MARGIN - 2 * DIALOG_PADDING;
    draw_modal_border(box_w, box_h, theme, dest);

    const int x0 = SCREEN_WIDTH / 2 - box_w / 2;
    const int y0 = SCREEN_HEIGHT / 2 - box_h / 2;
    const int x1 = x0 + box_w;

    // Clip every glyph to the content box so a long gloss stops at the border.
    SDL_Rect content_clip = {
        static_cast<Sint16>(x0), static_cast<Sint16>(y0),
        static_cast<Uint16>(box_w), static_cast<Uint16>(box_h)
    };
    SDL_SetClipRect(dest, &content_clip);

    int y = y0;

    // Header line 1: surface -> lemma, with the first English gloss if there is one.
    {
        std::string head = surface;
        if (active_analysis >= 0)
        {
            const Analysis &a = analyses[active_analysis];
            head += " \xE2\x86\x92 " + a.lemma.lemma;  // ->
            if (!a.it_en.empty())
            {
                head += "  (" + a.it_en.front().gloss + ")";
            }
        }
        blit_line(dest, font, head, x0, y, theme.main_text, theme.background);
        y += line_h;
    }

    // Header line 2: morphology of the active analysis, plus an ambiguity marker.
    {
        std::string sub;
        if (active_analysis >= 0)
        {
            sub = analyses[active_analysis].lemma.morphology_human;
        }
        if (analyses.size() > 1)
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "  \xE2\x80\xB9%d/%zu\xE2\x80\xBA",  // <k/n>
                     active_analysis + 1, analyses.size());
            sub += buf;
        }
        blit_line(dest, font, sub, x0, y, theme.secondary_text, theme.background);
        y += line_h + line_h / 3;
    }

    // Tab cycler: "<-  Title  ->   (i/n)", the title highlighted.
    if (!tabs.empty())
    {
        int tx = x0;
        tx += blit_line(dest, font, "\xE2\x97\x82  ", tx, y, theme.secondary_text, theme.background); // left triangle
        tx += blit_line(dest, font, tabs[active_tab].title, tx, y, theme.highlight_background, theme.background);
        tx += blit_line(dest, font, "  \xE2\x96\xB8", tx, y, theme.secondary_text, theme.background);  // right triangle

        char pos[24];
        snprintf(pos, sizeof(pos), "(%d/%zu)", active_tab + 1, tabs.size());
        int pw = 0;
        text::text_size(font, pos, &pw, nullptr);
        blit_line(dest, font, pos, x1 - pw, y, theme.secondary_text, theme.background);
    }
    y += line_h + line_h / 3;

    // Body: rows for the active tab, scrolled. The last line is reserved for the hint bar.
    const int hint_y = y0 + box_h - line_h;
    const int body_top = y;
    const int body_visible = std::max(1, (hint_y - body_top) / line_h);

    const std::vector<BodyRow> rows = body_rows();
    const int max_scroll = std::max(0, static_cast<int>(rows.size()) - body_visible);
    if (body_scroll > max_scroll)
    {
        body_scroll = max_scroll;
    }

    const int person_col = 96;  // width reserved for the person label in conjugation rows
    for (int i = 0; i < body_visible; ++i)
    {
        const int idx = body_scroll + i;
        if (idx >= static_cast<int>(rows.size()))
        {
            break;
        }
        const BodyRow &row = rows[idx];
        const int ry = body_top + i * line_h;
        if (row.right.empty())
        {
            blit_line(dest, font, row.left, x0, ry, theme.main_text, theme.background);
        }
        else
        {
            blit_line(dest, font, row.left, x0, ry, theme.secondary_text, theme.background);
            blit_line(dest, font, row.right, x0 + person_col, ry, theme.main_text, theme.background);
        }
    }

    // A faint "more below/above" cue: arrows at the right of the body if it overflows.
    if (body_scroll > 0)
    {
        blit_line(dest, font, "\xE2\x96\xB4", x1 - 12, body_top, theme.secondary_text, theme.background);
    }
    if (body_scroll < max_scroll)
    {
        blit_line(dest, font, "\xE2\x96\xBE", x1 - 12, hint_y - line_h, theme.secondary_text, theme.background);
    }

    // Hint bar.
    {
        std::string hint = "L/R tabs   \xE2\x86\x95 scroll   B back";
        if (analyses.size() > 1)
        {
            hint += "   X sense";
        }
        blit_line(dest, font, hint, x0, hint_y, theme.secondary_text, theme.background);
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
    switch (key)
    {
        case SW_BTN_B:
            _is_done = true;
            break;
        case SW_BTN_LEFT:
        case SW_BTN_L1:
        case SW_BTN_L2:
            move_tab(-1);
            break;
        case SW_BTN_RIGHT:
        case SW_BTN_R1:
        case SW_BTN_R2:
            move_tab(1);
            break;
        case SW_BTN_UP:
            scroll_body(-1);
            break;
        case SW_BTN_DOWN:
            scroll_body(1);
            break;
        case SW_BTN_X:
            if (analyses.size() > 1)
            {
                active_analysis = (active_analysis + 1) % static_cast<int>(analyses.size());
                rebuild_tabs();
                _needs_render = true;
            }
            break;
        default:
            break;
    }
}

void WordMeaningView::on_keyheld(SDLKey key, uint32_t held_time_ms)
{
    if ((key == SW_BTN_UP || key == SW_BTN_DOWN) && scroll_throttle(held_time_ms))
    {
        scroll_body(key == SW_BTN_UP ? -1 : 1);
    }
}
