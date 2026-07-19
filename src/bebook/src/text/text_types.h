#ifndef TEXT_TYPES_H_
#define TEXT_TYPES_H_

#include <cstdint>
#include <string>
#include <vector>

namespace text
{

// Horizontal positions are kept in FreeType's 26.6 fixed point (1/64 px) all the way
// through shaping and line breaking, and only rounded when a glyph is finally blitted.
// This is the whole point of the engine: SDL_ttf rounded every advance up to a whole
// pixel, which is what made spacing look uneven.
using Fixed = int32_t;

constexpr Fixed FIXED_ONE = 64;

constexpr Fixed int_to_fixed(int v) { return static_cast<Fixed>(v) * FIXED_ONE; }
constexpr int fixed_floor(Fixed v) { return static_cast<int>(v >> 6); }
constexpr int fixed_round(Fixed v) { return static_cast<int>((v + 32) >> 6); }
constexpr Fixed fixed_frac(Fixed v) { return v & (FIXED_ONE - 1); }

inline double fixed_to_double(Fixed v) { return v / 64.0; }

enum class Style : uint8_t
{
    Regular = 0,
    Italic = 1,
    Bold = 2,
    BoldItalic = 3,
};

constexpr int NUM_STYLES = 4;

inline Style style_with_italic(Style s, bool italic)
{
    uint8_t b = static_cast<uint8_t>(s);
    return static_cast<Style>(italic ? (b | 1) : (b & ~1));
}

inline Style style_with_bold(Style s, bool bold)
{
    uint8_t b = static_cast<uint8_t>(s);
    return static_cast<Style>(bold ? (b | 2) : (b & ~2));
}

inline bool style_is_italic(Style s) { return static_cast<uint8_t>(s) & 1; }
inline bool style_is_bold(Style s) { return static_cast<uint8_t>(s) & 2; }

// A contiguous span of a paragraph's UTF-8 bytes rendered in one style.
struct StyleRun
{
    uint32_t offset;
    uint32_t length;
    Style style;

    bool operator==(const StyleRun &o) const
    {
        return offset == o.offset && length == o.length && style == o.style;
    }
};

// Identifies one concrete FT_Face (a family member, or a fallback font).
using FaceId = uint16_t;
constexpr FaceId INVALID_FACE = static_cast<FaceId>(-1);

// A glyph placed relative to the origin of its line. `cluster` is the byte offset into
// the source UTF-8 that produced this glyph, which is how layout maps glyphs back to
// text positions for line breaking and addressing.
struct PositionedGlyph
{
    uint32_t glyph_index;
    FaceId face_id;
    Fixed x_offset;
    Fixed y_offset;
    Fixed x_advance;
    uint32_t cluster;
};

// Vertical metrics for one face at one size, in whole pixels.
struct FaceMetrics
{
    int ascent;
    int descent;   // positive number of pixels below the baseline
    int line_gap;

    int natural_line_height() const { return ascent + descent + line_gap; }
};

} // namespace text

#endif
