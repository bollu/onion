#ifndef READER_UI_CHROME_H_
#define READER_UI_CHROME_H_

#include "reader/color_theme.h"

#include <SDL/SDL.h>

#include <string>
#include <vector>

namespace text { struct Font; }

// Chrome shared by every screen that has an on-screen keyboard or a row of key bindings:
// the dictionary's search, the wiki's article search, and the meaning popup's hint bar.
//
// It lives here because those three had it three times over -- the same keyboard layout, the
// same blit helper down to a brace, the same "load the monospace face a size down and centre
// it" -- and a keyboard that drifts between two screens used minutes apart is worse than
// either version.
namespace ui
{

// One short, unwrapped string at (x, top-y). Returns its width, so callers can lay out a
// row piece by piece. Empty strings draw nothing and measure zero.
int blit_text(SDL_Surface *dest, text::Font *font, const std::string &s,
              int x, int y, SDL_Color fg, SDL_Color bg);

// The keyboard, as rows of keys. One definition: the two search screens are used minutes
// apart, and a different arrangement in each would cost more than either could gain.
const std::vector<std::string> &keyboard_rows();

// The fixed-pitch face the settings screen uses. Chrome is not prose: fixed advances line
// keys into columns with no per-cell nudging, and it is what makes these screens read as
// parts of one system. Falls back to `fallback` if the face will not load.
text::Font *chrome_font(unsigned size, text::Font *fallback);

// The same face a size down, for hint rows. Monospace is wider than the reading face at the
// same nominal size, and a full list of bindings overruns the screen at body size.
text::Font *hint_font(unsigned size, text::Font *fallback);

// A row of key bindings, centred within [x0, x1) and elided if it still will not fit.
void draw_key_hint(SDL_Surface *dest, const std::string &hint, int x0, int x1, int y,
                   unsigned font_size, text::Font *fallback, const ColorTheme &theme);

// Where the keyboard sits. Computed rather than assumed so the hint row above it can be
// placed against `top` without the two disagreeing.
struct KeyboardBox
{
    int top;      // y of the first row
    int left;     // x of a full-width row
    int cell_w;
    int cell_h;
    int height;
};

KeyboardBox keyboard_box(int x0, int x1, int line_height, int bottom_margin);

// Draw it, with (sel_row, sel_col) filled. Rows shorter than the widest are inset by half a
// cell per missing key, so they stay centred on each other rather than all flush left.
void draw_keyboard(SDL_Surface *dest, const KeyboardBox &box, int sel_row, int sel_col,
                   unsigned font_size, text::Font *fallback, const ColorTheme &theme);

}

#endif
