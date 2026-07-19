#ifndef SHAPER_H_
#define SHAPER_H_

#include "./face_cache.h"
#include "./text_types.h"

#include <memory>
#include <vector>

namespace text
{

struct ShaperState;

// Turns UTF-8 into positioned glyphs via HarfBuzz.
//
// This is the seam the rest of the engine talks to: layout never touches HarfBuzz
// directly, so the shaper could be swapped for a simpler advance-summing implementation
// without disturbing line breaking or rendering.
//
// Positions come back in 26.6 fixed point. HarfBuzz's own OpenType font functions supply
// the metrics rather than FreeType's, deliberately: they read `hmtx` and scale linearly,
// giving unrounded fractional advances, whereas FreeType would hand back advances already
// rounded to whole pixels. Using them also means shaping does not depend on the mutable
// pixel size currently set on the shared FT_Face.
class Shaper
{
    std::unique_ptr<ShaperState> state;

public:
    explicit Shaper(const FaceCache &faces);
    ~Shaper();

    Shaper(const Shaper &) = delete;
    Shaper &operator=(const Shaper &) = delete;

    // Shape one run of text that is all in the same style, splitting internally at
    // font-fallback boundaries. Glyphs are appended to `out` with x_offset/y_offset
    // relative to the pen, and `cluster` set to the byte offset within `utf8` plus
    // `cluster_base`.
    void shape(
        int family,
        Style style,
        uint32_t size_px,
        const char *utf8,
        uint32_t length,
        uint32_t cluster_base,
        std::vector<PositionedGlyph> &out
    ) const;

    // Total advance width of a run, in 26.6. Equivalent to summing the advances from
    // shape(), but cached, since line breaking measures the same words repeatedly.
    Fixed measure(
        int family,
        Style style,
        uint32_t size_px,
        const char *utf8,
        uint32_t length
    ) const;

    void clear_cache();
};

} // namespace text

#endif
