#ifndef TEXT_FONT_H_
#define TEXT_FONT_H_

#include "./glyph_atlas.h"
#include "./text_types.h"
#include "util/sdl_pointer.h"

#include <SDL/SDL.h>

#include <string>

namespace text
{

class FaceCache;
class Shaper;

// A font handle: one family at one size, in one style. Deliberately shaped like the
// TTF_Font* it replaces so the view layer could be migrated mechanically, but backed by
// FreeType + HarfBuzz, so it gets shaping, fractional advances and linear-light
// antialiasing that SDL_ttf could not provide.
struct Font
{
    int family;
    Style style;
    uint32_t size_px;
};

// Process-wide engine. Mirrors the existing global font cache in util/sdl_font_cache.h:
// fonts are shared across every view, and the atlas only pays off if it is shared too.
class Engine
{
public:
    static Engine &instance();

    // Registers `path` as a family's regular face. If sibling files following the usual
    // -Italic/-Bold/-BoldItalic naming exist beside it they are picked up as the real
    // style faces; otherwise those styles fall back to regular.
    Font *load(const std::string &path, uint32_t size_px, Style style = Style::Regular);

    void add_fallback(const std::string &path);

    FaceCache &faces();
    Shaper &shaper();
    GlyphAtlas &atlas();

    void set_hinting(Hinting h);

private:
    Engine();
    ~Engine();
    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;

    struct Impl;
    Impl *impl;
};

// --- SDL_ttf-shaped helpers -------------------------------------------------------
// These mirror TTF_SizeUTF8 / TTF_RenderUTF8_Shaded / TTF_FontHeight so that call sites
// read the same as before the migration.

// Width and height in whole pixels. Width is rounded up from the exact 26.6 advance.
void text_size(const Font *font, const char *utf8, int *w, int *h);

// Exact width of an explicit-length slice, in 26.6. Unlike TTF_SizeUTF8 this does not
// require a NUL terminator, so callers measuring part of a larger buffer no longer have
// to temporarily overwrite it.
Fixed text_width(const Font *font, const char *utf8, uint32_t length);

int font_line_height(const Font *font);
int font_ascent(const Font *font);

// Truncate `s` to fit `max_px`, appending an ellipsis when it does not. UTF-8 aware: whole
// code points are dropped from the end so a multibyte glyph is never split. For single-line
// cells that have a hard width and no room to wrap.
std::string elide_to_width(const Font *font, const std::string &s, int max_px);

// Renders onto an opaque `bg`, like TTF_RenderUTF8_Shaded. Returns nullptr for empty
// text, matching how the existing views guard their blits.
surface_unique_ptr render_text_shaded(
    const Font *font,
    const char *utf8,
    SDL_Color fg,
    SDL_Color bg,
    SDL_PixelFormat *format = nullptr
);

// Draws a laid-out line, spreading `extra_total` (which may be negative) evenly across
// the line's inter-word gaps to justify it, and optionally appending the hyphen that a
// discretionary break introduced.
//
// The distribution is exact: pen positions are 26.6 throughout, so the remainder after
// dividing by the gap count is handed out a single 1/64px unit at a time rather than
// being rounded away. That is what stops justified spacing from looking uneven, which
// is unavoidable when glyph positions can only land on whole pixels.
//
// `length` bounds the slice explicitly, so a line may point into the middle of the
// paragraph buffer without needing a NUL.
int draw_text_justified(
    SDL_Surface *dst,
    const Font *font,
    const char *utf8,
    uint32_t length,
    Fixed extra_total,
    uint32_t gaps,
    bool trailing_hyphen,
    int x,
    int baseline_y,
    SDL_Color fg,
    SDL_Color bg,
    const SDL_Rect *clip = nullptr
);

// Draws straight onto `dst` with no intermediate surface. Preferred for the reader's
// hot path; returns the pen x after the last glyph.
int draw_text(
    SDL_Surface *dst,
    const Font *font,
    const char *utf8,
    int x,
    int baseline_y,
    SDL_Color fg,
    SDL_Color bg,
    const SDL_Rect *clip = nullptr
);

} // namespace text

#endif
