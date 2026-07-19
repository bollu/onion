#ifndef TEXT_RENDER_H_
#define TEXT_RENDER_H_

#include "./glyph_atlas.h"
#include "./text_types.h"

#include <SDL/SDL.h>

#include <cstdint>
#include <vector>

namespace text
{

// A precomputed foreground-over-background ramp, indexed by glyph coverage.
//
// The interpolation is done in linear light, not directly on sRGB values. FreeType's
// coverage byte is a linear area measurement, so mixing the endpoints in sRGB space --
// which is what SDL_ttf's Shaded renderer does -- systematically misweights every
// antialiased pixel. The error is largest for light-on-dark, which is exactly the
// default reading theme, and shows up as text that looks bolder and softer than the
// design intends.
//
// Because the ramp is precomputed, correctness costs nothing at blit time: a pixel is
// one table lookup and one store, the same as before.
class TextPalette
{
    uint32_t entries[256];

public:
    TextPalette();
    void build(const SDL_PixelFormat *format, SDL_Color fg, SDL_Color bg);
    uint32_t operator[](uint8_t coverage) const { return entries[coverage]; }
};

// Draws glyphs with their baseline at `baseline_y` and pen origin at `origin_x`
// (26.6). Assumes the destination under the text is uniformly `palette`'s background,
// which holds for the reader's solid theme background.
//
// Returns the pen position after the last glyph, so callers can chain runs.
Fixed draw_glyphs_shaded(
    SDL_Surface *dst,
    const std::vector<PositionedGlyph> &glyphs,
    Fixed origin_x,
    int baseline_y,
    GlyphAtlas &atlas,
    uint32_t size_px,
    const TextPalette &palette,
    const SDL_Rect *clip = nullptr
);

// Same, but alpha-blends against whatever is already in the destination, for text over
// images or gradients. Slower: a real blend per pixel rather than a lookup.
Fixed draw_glyphs_blended(
    SDL_Surface *dst,
    const std::vector<PositionedGlyph> &glyphs,
    Fixed origin_x,
    int baseline_y,
    GlyphAtlas &atlas,
    uint32_t size_px,
    SDL_Color fg,
    const SDL_Rect *clip = nullptr
);

// Total advance of a shaped run, in 26.6.
Fixed run_advance(const std::vector<PositionedGlyph> &glyphs);

} // namespace text

#endif
