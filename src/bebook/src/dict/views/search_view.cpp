#include "./search_view.h"

#include "reader/system_styling.h"
#include "reader/view_stack.h"
#include "reader/views/word_meaning_view.h"

#include "sys/keymap.h"
#include "sys/screen.h"

#include "text/font.h"

#include "util/sdl_font_cache.h"
#include "util/sdl_utils.h"

#include <algorithm>
#include <memory>

namespace
{

constexpr int MARGIN = 16;

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
    results = lexicon.search(query);
    result_index = 0;
    result_scroll = 0;
    if (results.empty() && focus == Focus::Results)
    {
        focus = Focus::Keyboard;
    }
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
    kb_row = std::max(0, std::min(kb_row + dr, static_cast<int>(kb.size()) - 1));
    kb_col = std::max(0, std::min(kb_col + dc, static_cast<int>(kb[kb_row].size()) - 1));
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
    if (focus == Focus::Results)
    {
        switch (key)
        {
            case SW_BTN_UP:
                if (result_index > 0) { --result_index; _needs_render = true; }
                break;
            case SW_BTN_DOWN:
                if (result_index + 1 < static_cast<int>(results.size()))
                {
                    ++result_index;
                    _needs_render = true;
                }
                else
                {
                    focus = Focus::Keyboard;  // fall out of the bottom into the keyboard
                    kb_row = 0;
                    _needs_render = true;
                }
                break;
            case SW_BTN_A:
                open_selected();
                break;
            case SW_BTN_B:
                focus = Focus::Keyboard;
                _needs_render = true;
                break;
            default:
                break;
        }
        return;
    }

    // Keyboard focus.
    switch (key)
    {
        case SW_BTN_UP:
            if (kb_row == 0 && !results.empty())
            {
                focus = Focus::Results;
                _needs_render = true;
            }
            else
            {
                move_key(-1, 0);
            }
            break;
        case SW_BTN_DOWN:  move_key(1, 0); break;
        case SW_BTN_LEFT:  move_key(0, -1); break;
        case SW_BTN_RIGHT: move_key(0, 1); break;
        case SW_BTN_A:     activate_key(); break;
        case SW_BTN_L1:
        case SW_BTN_Y:     backspace(); break;   // quick backspace
        case SW_BTN_B:     _is_done = true; break;
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
            if (nav_throttle(held_time_ms)) on_keypress(key);
            break;
        // Held backspace erases continuously. Its own throttle, because deleting is
        // easier to overshoot than moving a cursor and wants a beat longer before it runs.
        case SW_BTN_B:
            if (backspace_throttle(held_time_ms)) backspace();
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

    // --- Results (between bar and keyboard) -------------------------------------------
    const int results_top = y;
    const int results_bottom = kb_top - 6;
    const int visible = std::max(1, (results_bottom - results_top) / line_h);

    if (result_index < result_scroll) result_scroll = result_index;
    if (result_index >= result_scroll + visible) result_scroll = result_index - visible + 1;

    if (results.empty() && !query.empty())
    {
        blit(dest, font, "Nessun risultato", cx0, results_top, theme.secondary_text, theme.background);
    }
    for (int i = 0; i < visible; ++i)
    {
        const int idx = result_scroll + i;
        if (idx >= static_cast<int>(results.size())) break;
        const auto &hit = results[idx];
        const int ry = results_top + i * line_h;
        const bool sel = (focus == Focus::Results && idx == result_index);
        if (sel)
        {
            SDL_Rect hl = { static_cast<Sint16>(cx0 - 4), static_cast<Sint16>(ry - 1),
                            static_cast<Uint16>(cx1 - cx0 + 8), static_cast<Uint16>(line_h + 2) };
            SDL_FillRect(dest, &hl, mapc(theme.highlight_background));
        }
        const SDL_Color fg = sel ? theme.highlight_text : theme.main_text;
        const SDL_Color bg = sel ? theme.highlight_background : theme.background;
        int wx = cx0 + blit(dest, font, hit.word, cx0, ry, fg, bg);
        if (hit.lemma != hit.word)
        {
            blit(dest, font, "  \xE2\x86\x92 " + hit.lemma, wx, ry,
                 sel ? theme.highlight_text : theme.secondary_text, bg);
        }
    }

    // --- Hint line (just above the keyboard) ------------------------------------------
    blit(dest, font, "A scrivi   Y cancella   \xE2\x86\x91 risultati   B esci",
         cx0, kb_top - line_h - 2, theme.secondary_text, theme.background);

    // --- Keyboard ---------------------------------------------------------------------
    for (int r = 0; r < static_cast<int>(kb.size()); ++r)
    {
        const int ky = kb_top + r * cell_h;
        const std::string &row = kb[r];
        for (int c = 0; c < static_cast<int>(row.size()); ++c)
        {
            const std::string label(1, row[c]);
            const int kx = cx0 + c * cell_w;
            const bool sel = (focus == Focus::Keyboard && r == kb_row && c == kb_col);
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
