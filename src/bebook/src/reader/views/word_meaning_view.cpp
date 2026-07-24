#include "./word_meaning_view.h"

#include "reader/config.h"
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
    : styling(styling)
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

std::vector<WordMeaningView::BodyItem> WordMeaningView::body_items() const
{
    using Kind = BodyItem::Kind;
    std::vector<BodyItem> out;
    if (active_analysis < 0 || tabs.empty())
    {
        out.push_back({ Kind::Prose, "Nessuna voce.", "" });
        return out;
    }

    const Analysis &a = analyses[active_analysis];
    const Tab &tab = tabs[active_tab];

    auto add_senses = [&out](const std::vector<lexicon::Sense> &senses, const char *empty_msg) {
        if (senses.empty())
        {
            out.push_back({ Kind::Prose, empty_msg, "" });
            return;
        }
        int n = 1;
        for (const auto &s : senses)
        {
            out.push_back({ Kind::Prose, std::to_string(n++) + ".  " + s.gloss, "" });
            out.push_back({ Kind::Blank, "", "" });
        }
        if (!out.empty() && out.back().kind == Kind::Blank)
        {
            out.pop_back();
        }
    };

    switch (tab.kind)
    {
        case TabKind::ItEn:
            add_senses(a.it_en, "(nessuna definizione inglese)");
            break;
        case TabKind::ItIt:
            add_senses(a.it_it, "(nessuna definizione italiana)");
            break;
        case TabKind::Conj:
        {
            const lexicon::ConjTable &t = a.conj[tab.conj_index];
            for (int i = 0; i < 6; ++i)
            {
                out.push_back({ Kind::Conjugation, lexicon::PERSON_LABELS[i], t.forms[i] });
            }
            break;
        }
    }
    return out;
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

    // --- Header: surface -> lemma (lemma bold), quick gloss, morphology ---------------
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
            head = lemma;  // the selection already is the lemma
            runs.push_back({ 0u, static_cast<uint32_t>(lemma.size()), text::Style::Bold });
        }
        else
        {
            head = surface + " \xE2\x86\x92 " + lemma;  // ->
            runs.push_back({ static_cast<uint32_t>(head.size() - lemma.size()),
                             static_cast<uint32_t>(lemma.size()), text::Style::Bold });
        }
        y = draw_prose(head, &runs, cx0, y, cw, theme.main_text);
    }

    if (active_analysis >= 0 && !analyses[active_analysis].it_en.empty())
    {
        y = draw_prose(analyses[active_analysis].it_en.front().gloss, nullptr,
                       cx0, y, cw, theme.secondary_text);
    }

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
    rule(y);
    y += 1 + PAD_Y / 2;

    // --- Tab strip: "◂ [Title] ▸" centred, active title in a filled pill ---------------
    if (!tabs.empty())
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
        const int total = wa + arrow_gap + pill_w + arrow_gap + wa;

        int gx = cx0 + (cw - total) / 2;
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
    }
    y += line_h;

    y += PAD_Y / 2;
    rule(y);
    y += 1 + PAD_Y / 2;

    // --- Body -------------------------------------------------------------------------
    const int hint_y = box_y + box_h - PAD_Y - line_h;
    const int lower_rule_y = hint_y - PAD_Y / 2;
    const int body_top = y;
    const int body_bottom = lower_rule_y - PAD_Y / 2;
    const int body_visible = std::max(1, (body_bottom - body_top) / line_h);

    const int scrollbar_w = 4;
    const int body_w = cw - scrollbar_w - 6;  // leave room for the scrollbar gutter
    const int pron_col = 92;                  // conjugation pronoun column width

    // Flatten body items into physical lines: prose paragraphs wrap; conjugation and blank
    // items are one line each. `item` indexes back into `items` (not a pointer) so nothing
    // dangles; `paras` is reserved up front so `paras.back().c_str()` handed to StyledText
    // below can't be invalidated by a reallocation mid-loop.
    const std::vector<BodyItem> items = body_items();
    std::vector<std::string> paras;                        // prose text, kept alive for draw
    std::vector<std::vector<text::Line>> layouts;
    paras.reserve(items.size());
    layouts.reserve(items.size());
    struct Phys { int type; int para; int line; int item; };  // type: 0 prose,1 conj,2 blank
    std::vector<Phys> phys;
    for (int i = 0; i < static_cast<int>(items.size()); ++i)
    {
        const BodyItem &it = items[i];
        if (it.kind == BodyItem::Kind::Conjugation)
        {
            phys.push_back({ 1, -1, -1, i });
        }
        else if (it.kind == BodyItem::Kind::Blank || it.a.empty())
        {
            phys.push_back({ 2, -1, -1, i });
        }
        else
        {
            const int pidx = static_cast<int>(paras.size());
            paras.push_back(it.a);
            text::StyledText st = styled_of(paras.back(), nullptr);
            layouts.push_back(text::layout_paragraph(st, font, body_w, text::Align::Left, true));
            for (int k = 0; k < static_cast<int>(layouts.back().size()); ++k)
            {
                phys.push_back({ 0, pidx, k, i });
            }
        }
    }

    const int total_lines = static_cast<int>(phys.size());
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
        const Phys &p = phys[idx];
        const int ry = body_top + r * line_h;
        if (p.type == 2)
        {
            continue;
        }
        if (p.type == 1)
        {
            const BodyItem &it = items[p.item];
            blit_line(dest, font, it.a, cx0, ry, theme.secondary_text, theme.background);
            blit_line(dest, font, it.b, cx0 + pron_col, ry, theme.main_text, theme.background);
        }
        else
        {
            text::StyledText st = styled_of(paras[p.para], nullptr);
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
        std::string hint = "L/R schede   \xE2\x86\x95 scorri   B indietro";
        if (analyses.size() > 1)
        {
            hint += "   X analisi";
        }
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
