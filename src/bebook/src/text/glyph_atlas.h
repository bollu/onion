#ifndef GLYPH_ATLAS_H_
#define GLYPH_ATLAS_H_

#include "./face_cache.h"
#include "./text_types.h"

#include <memory>
#include <vector>

namespace text
{

// Number of horizontal subpixel phases each glyph is rasterized at.
//
// With fractional pen positions, a glyph can land anywhere between two pixels. Rounding
// the pen to whole pixels would throw away exactly the precision the shaper works to
// preserve, so instead each glyph is rasterized at PHASES evenly spaced sub-pixel
// offsets and the nearest one is blitted. 4 phases means a worst-case positioning error
// of 1/8 px, which is well below what is visible, and costs 4x the glyph cache.
constexpr int SUBPIXEL_PHASES = 4;

enum class Hinting
{
    None,   // truest letterforms; best at high pixel density
    Light,  // vertical-only; leaves horizontal metrics untouched
    Normal, // full grid-fitting; distorts shapes but maximises stem contrast
};

// One rasterized glyph: an 8-bit coverage bitmap plus its placement relative to the pen.
struct GlyphBitmap
{
    const uint8_t *coverage;  // width*height alpha, owned by the atlas
    int width;
    int height;
    int bearing_x;  // left edge relative to pen, in whole pixels
    int bearing_y;  // top edge relative to baseline, positive up
};

struct GlyphAtlasState;

// Rasterizes glyphs once and keeps them. Drawing a page then costs one small alpha blit
// per glyph and no rasterization or allocation at all, which is what makes per-word
// justified placement affordable at 20 FPS on the device.
class GlyphAtlas
{
    std::unique_ptr<GlyphAtlasState> state;

public:
    explicit GlyphAtlas(const FaceCache &faces);
    ~GlyphAtlas();

    GlyphAtlas(const GlyphAtlas &) = delete;
    GlyphAtlas &operator=(const GlyphAtlas &) = delete;

    void set_hinting(Hinting hinting);
    Hinting hinting() const;

    // Emboldens stems slightly to compensate for the optical thinning that linear-light
    // compositing produces on dark backgrounds. 0 disables.
    void set_stem_darkening(int amount_26_6);

    // `phase` selects the subpixel offset, in [0, SUBPIXEL_PHASES).
    // Returns nullptr if the glyph could not be rasterized.
    const GlyphBitmap *get(FaceId face, uint32_t size_px, uint32_t glyph_index, int phase);

    // Convenience: pick the phase nearest to a fractional pen position.
    static int phase_for(Fixed x);

    void clear();
    size_t bytes_used() const;
};

} // namespace text

#endif
