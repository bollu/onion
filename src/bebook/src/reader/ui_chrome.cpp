#include "reader/ui_chrome.h"

#include "reader/config.h"

#include "sys/screen.h"

#include "text/font.h"

#include "util/sdl_font_cache.h"
#include "util/sdl_utils.h"

#include <algorithm>

namespace ui
{

int blit_text(SDL_Surface *dest, text::Font *font, const std::string &s,
              int x, int y, SDL_Color fg, SDL_Color bg)
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
    const int w = surf->w;
    SDL_BlitSurface(surf.get(), nullptr, dest, &dst);
    return w;
}

const std::vector<std::string> &keyboard_rows()
{
    static const std::vector<std::string> rows = {
        "qwertyuiop",
        "asdfghjkl",
        "zxcvbnm '",
    };
    return rows;
}

namespace
{

int widest_row()
{
    int w = 0;
    for (const auto &row : keyboard_rows())
    {
        w = std::max(w, static_cast<int>(row.size()));
    }
    return w;
}

}

text::Font *chrome_font(unsigned size, text::Font *fallback)
{
    text::Font *f = cached_load_font(SYSTEM_FONT, size, FontLoadErrorOpt::NoThrow);
    return f != nullptr ? f : fallback;
}

text::Font *hint_font(unsigned size, text::Font *fallback)
{
    return chrome_font(std::max<unsigned>(12, size * 3 / 4), fallback);
}

void draw_key_hint(SDL_Surface *dest, const std::string &hint, int x0, int x1, int y,
                   unsigned font_size, text::Font *fallback, const ColorTheme &theme)
{
    if (hint.empty())
    {
        return;
    }
    text::Font *font = hint_font(font_size, fallback);
    const std::string shown = text::elide_to_width(font, hint, x1 - x0);
    int w = 0;
    text::text_size(font, shown.c_str(), &w, nullptr);
    blit_text(dest, font, shown, x0 + (x1 - x0 - w) / 2, y,
              theme.secondary_text, theme.background);
}

KeyboardBox keyboard_box(int x0, int x1, int line_height, int bottom_margin)
{
    KeyboardBox box;
    const int rows = static_cast<int>(keyboard_rows().size());
    box.cell_w = (x1 - x0) / widest_row();
    // A row pitch of the line height alone leaves rows touching, so a filled key bleeds
    // into the row beneath it.
    box.cell_h = line_height + 6;
    box.height = rows * box.cell_h;
    box.top = SCREEN_HEIGHT - bottom_margin - box.height;
    box.left = x0 + (x1 - x0 - widest_row() * box.cell_w) / 2;
    return box;
}

void draw_keyboard(SDL_Surface *dest, const KeyboardBox &box, int sel_row, int sel_col,
                   unsigned font_size, text::Font *fallback, const ColorTheme &theme)
{
    text::Font *font = chrome_font(font_size, fallback);
    const auto &rows = keyboard_rows();

    auto mapc = [&](const SDL_Color &c) { return SDL_MapRGB(dest->format, c.r, c.g, c.b); };

    for (int r = 0; r < static_cast<int>(rows.size()); ++r)
    {
        const std::string &row = rows[r];
        const int y = box.top + r * box.cell_h;
        const int row_left =
            box.left + (widest_row() - static_cast<int>(row.size())) * box.cell_w / 2;

        for (int c = 0; c < static_cast<int>(row.size()); ++c)
        {
            const bool sel = (r == sel_row && c == sel_col);
            const int x = row_left + c * box.cell_w;

            SDL_Rect cell = {
                static_cast<Sint16>(x + 1), static_cast<Sint16>(y + 1),
                static_cast<Uint16>(box.cell_w - 2), static_cast<Uint16>(box.cell_h - 2)
            };
            SDL_FillRect(dest, &cell,
                         mapc(sel ? theme.highlight_background : theme.background));

            const std::string label(1, row[c]);
            int lw = 0;
            text::text_size(font, label.c_str(), &lw, nullptr);
            blit_text(dest, font, label, x + (box.cell_w - lw) / 2, y + 3,
                      sel ? theme.highlight_text : theme.main_text,
                      sel ? theme.highlight_background : theme.background);
        }
    }
}

}
