#include "./glyph_atlas.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include <unordered_map>

namespace text
{

namespace
{

// Key: face | size | glyph index | phase, packed into 64 bits.
inline uint64_t glyph_key(FaceId face, uint32_t size_px, uint32_t glyph_index, int phase)
{
    return (static_cast<uint64_t>(face) << 48)
         | (static_cast<uint64_t>(size_px & 0xFFF) << 36)
         | (static_cast<uint64_t>(glyph_index & 0xFFFFFFF) << 8)
         | static_cast<uint64_t>(phase & 0xFF);
}

struct CachedGlyph
{
    std::vector<uint8_t> coverage;
    GlyphBitmap bitmap;
};

// Rasterized glyphs are small (a 26px face averages well under 400 bytes per glyph, and
// 4 phases of a full Latin working set lands around 300KB). This cap exists to bound
// pathological cases such as scrolling a CJK book, not as a routine constraint.
constexpr size_t ATLAS_BUDGET_BYTES = 8u * 1024u * 1024u;

int32_t ft_load_target(Hinting h)
{
    switch (h)
    {
        case Hinting::None:   return FT_LOAD_NO_HINTING;
        case Hinting::Light:  return FT_LOAD_TARGET_LIGHT;
        case Hinting::Normal: return FT_LOAD_TARGET_NORMAL;
    }
    return FT_LOAD_TARGET_LIGHT;
}

} // namespace

struct GlyphAtlasState
{
    const FaceCache &faces;
    std::unordered_map<uint64_t, CachedGlyph> glyphs;
    size_t bytes = 0;

    // Light hinting is the default deliberately. The Miyoo Mini's panel is 640x480 on a
    // 2.8" screen, around 285 DPI, where full grid-fitting distorts letterforms for
    // contrast we do not need. Light hints vertically only, leaving the horizontal
    // metrics that the shaper computed untouched.
    Hinting hinting = Hinting::Light;
    int stem_darkening = 0;

    explicit GlyphAtlasState(const FaceCache &faces) : faces(faces) {}
};

GlyphAtlas::GlyphAtlas(const FaceCache &faces)
    : state(std::make_unique<GlyphAtlasState>(faces))
{
}

GlyphAtlas::~GlyphAtlas()
{
}

void GlyphAtlas::set_hinting(Hinting hinting)
{
    if (state->hinting != hinting)
    {
        state->hinting = hinting;
        clear();
    }
}

Hinting GlyphAtlas::hinting() const
{
    return state->hinting;
}

void GlyphAtlas::set_stem_darkening(int amount_26_6)
{
    if (state->stem_darkening != amount_26_6)
    {
        state->stem_darkening = amount_26_6;
        clear();
    }
}

int GlyphAtlas::phase_for(Fixed x)
{
    // Map the fractional part of the pen position onto a phase bucket, rounding to
    // nearest so the error is at most half a phase.
    Fixed frac = fixed_frac(x);
    int phase = (frac * SUBPIXEL_PHASES + FIXED_ONE / 2) / FIXED_ONE;
    return phase % SUBPIXEL_PHASES;
}

const GlyphBitmap *GlyphAtlas::get(FaceId face, uint32_t size_px, uint32_t glyph_index, int phase)
{
    if (phase < 0 || phase >= SUBPIXEL_PHASES)
    {
        phase = 0;
    }

    uint64_t key = glyph_key(face, size_px, glyph_index, phase);
    auto it = state->glyphs.find(key);
    if (it != state->glyphs.end())
    {
        return &it->second.bitmap;
    }

    FT_Face ft = state->faces.ft_face(face, size_px);
    if (!ft)
    {
        return nullptr;
    }

    if (FT_Load_Glyph(ft, glyph_index, FT_LOAD_DEFAULT | ft_load_target(state->hinting)))
    {
        return nullptr;
    }

    FT_GlyphSlot slot = ft->glyph;

    if (slot->format == FT_GLYPH_FORMAT_OUTLINE)
    {
        // Shift by the subpixel phase before rasterizing, so the glyph body itself lands
        // between pixels rather than merely its integer origin.
        FT_Pos dx = static_cast<FT_Pos>(phase) * FIXED_ONE / SUBPIXEL_PHASES;
        if (dx)
        {
            FT_Outline_Translate(&slot->outline, dx, 0);
        }

        if (state->stem_darkening > 0)
        {
            FT_Outline_Embolden(&slot->outline, state->stem_darkening);
        }
    }

    if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL))
    {
        return nullptr;
    }

    const FT_Bitmap &bm = slot->bitmap;

    CachedGlyph cached;
    cached.coverage.resize(static_cast<size_t>(bm.width) * bm.rows);

    // FreeType rows may be padded; copy row by row into a tightly packed buffer.
    for (unsigned int y = 0; y < bm.rows; ++y)
    {
        const uint8_t *src = bm.buffer + static_cast<int>(y) * bm.pitch;
        uint8_t *dst = cached.coverage.data() + static_cast<size_t>(y) * bm.width;
        for (unsigned int x = 0; x < bm.width; ++x)
        {
            dst[x] = src[x];
        }
    }

    cached.bitmap.width = static_cast<int>(bm.width);
    cached.bitmap.height = static_cast<int>(bm.rows);
    cached.bitmap.bearing_x = slot->bitmap_left;
    cached.bitmap.bearing_y = slot->bitmap_top;

    if (state->bytes > ATLAS_BUDGET_BYTES)
    {
        clear();
    }
    state->bytes += cached.coverage.size();

    auto inserted = state->glyphs.emplace(key, std::move(cached));
    CachedGlyph &stored = inserted.first->second;
    // Pointer must be taken after the move into the map.
    stored.bitmap.coverage = stored.coverage.data();
    return &stored.bitmap;
}

void GlyphAtlas::clear()
{
    state->glyphs.clear();
    state->bytes = 0;
}

size_t GlyphAtlas::bytes_used() const
{
    return state->bytes;
}

} // namespace text
