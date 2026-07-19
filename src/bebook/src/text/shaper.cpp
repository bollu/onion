#include "./shaper.h"

#include "util/utf8.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <hb.h>
#include <hb-ft.h>
#include <hb-ot.h>

#include <string>
#include <unordered_map>

namespace text
{

namespace
{

// hb_font_t is keyed by face and pixel size. Packed into one integer so it can key an
// unordered_map without a custom hash.
inline uint64_t font_key(FaceId face, uint32_t size_px)
{
    return (static_cast<uint64_t>(face) << 32) | size_px;
}

struct HbFontDeleter
{
    void operator()(hb_font_t *f) const { if (f) hb_font_destroy(f); }
};
struct HbFaceDeleter
{
    void operator()(hb_face_t *f) const { if (f) hb_face_destroy(f); }
};

using HbFontPtr = std::unique_ptr<hb_font_t, HbFontDeleter>;
using HbFacePtr = std::unique_ptr<hb_face_t, HbFaceDeleter>;

// Line breaking measures the same words over and over, so cache widths. Bounded by a
// plain size check and cleared wholesale, which is enough: the working set is one
// paragraph, and the cost of a miss is a single shaping call.
constexpr size_t MEASURE_CACHE_LIMIT = 4096;

} // namespace

struct ShaperState
{
    const FaceCache &faces;

    mutable std::unordered_map<FaceId, HbFacePtr> hb_faces;
    mutable std::unordered_map<uint64_t, HbFontPtr> hb_fonts;
    mutable std::unordered_map<std::string, Fixed> measure_cache;

    explicit ShaperState(const FaceCache &faces) : faces(faces) {}

    hb_font_t *font_for(FaceId id, uint32_t size_px) const
    {
        uint64_t key = font_key(id, size_px);
        auto it = hb_fonts.find(key);
        if (it != hb_fonts.end())
        {
            return it->second.get();
        }

        auto fit = hb_faces.find(id);
        if (fit == hb_faces.end())
        {
            // Any pixel size will do here; hb_face_t is size-independent.
            FT_Face ft = faces.ft_face(id, size_px);
            if (!ft)
            {
                return nullptr;
            }
            HbFacePtr hf{hb_ft_face_create_referenced(ft)};
            if (!hf)
            {
                return nullptr;
            }
            fit = hb_faces.emplace(id, std::move(hf)).first;
        }

        HbFontPtr font{hb_font_create(fit->second.get())};
        if (!font)
        {
            return nullptr;
        }

        // Scale chosen so HarfBuzz emits positions directly in 26.6 pixels.
        hb_font_set_scale(font.get(), static_cast<int>(size_px) * 64, static_cast<int>(size_px) * 64);
        hb_font_set_ppem(font.get(), size_px, size_px);
        hb_ot_font_set_funcs(font.get());

        hb_font_t *raw = font.get();
        hb_fonts.emplace(key, std::move(font));
        return raw;
    }
};

Shaper::Shaper(const FaceCache &faces) : state(std::make_unique<ShaperState>(faces))
{
}

Shaper::~Shaper()
{
}

namespace
{

// Shape one segment that is entirely covered by a single face.
void shape_segment(
    const ShaperState &s,
    FaceId face,
    uint32_t size_px,
    const char *utf8,
    uint32_t run_length,
    uint32_t seg_offset,
    uint32_t seg_length,
    uint32_t cluster_base,
    std::vector<PositionedGlyph> &out
)
{
    hb_font_t *font = s.font_for(face, size_px);
    if (!font || seg_length == 0)
    {
        return;
    }

    hb_buffer_t *buf = hb_buffer_create();

    // The full run is supplied as context with only this segment marked for shaping, so
    // HarfBuzz can still apply contextual rules across a fallback boundary. The length is
    // passed explicitly rather than as -1: callers hand us a (pointer, length) slice of a
    // larger buffer, which is not guaranteed to be NUL-terminated at `run_length`.
    hb_buffer_add_utf8(
        buf,
        utf8,
        static_cast<int>(run_length),
        seg_offset,
        static_cast<int>(seg_length)
    );
    hb_buffer_guess_segment_properties(buf);
    hb_shape(font, buf, nullptr, 0);

    unsigned int count = 0;
    const hb_glyph_info_t *info = hb_buffer_get_glyph_infos(buf, &count);
    const hb_glyph_position_t *pos = hb_buffer_get_glyph_positions(buf, &count);

    out.reserve(out.size() + count);
    for (unsigned int i = 0; i < count; ++i)
    {
        PositionedGlyph g;
        g.glyph_index = info[i].codepoint;  // post-shaping this is a glyph id, not a char
        g.face_id = face;
        g.x_offset = pos[i].x_offset;
        g.y_offset = pos[i].y_offset;
        g.x_advance = pos[i].x_advance;
        g.cluster = cluster_base + info[i].cluster;
        out.push_back(g);
    }

    hb_buffer_destroy(buf);
}

} // namespace

void Shaper::shape(
    int family,
    Style style,
    uint32_t size_px,
    const char *utf8,
    uint32_t length,
    uint32_t cluster_base,
    std::vector<PositionedGlyph> &out
) const
{
    if (!utf8 || length == 0)
    {
        return;
    }

    // Split into maximal segments sharing one face, so that a stretch of CJK handled by
    // the fallback font is still shaped as a unit rather than glyph by glyph.
    const char *p = utf8;
    const char *end = utf8 + length;

    uint32_t seg_start = 0;
    FaceId seg_face = INVALID_FACE;

    while (p < end)
    {
        const char *cp_start = p;
        uint32_t cp = utf8_decode(&p, end);
        FaceId face = state->faces.face_for_codepoint(family, style, cp);

        if (seg_face == INVALID_FACE)
        {
            seg_face = face;
        }
        else if (face != seg_face)
        {
            uint32_t seg_end = static_cast<uint32_t>(cp_start - utf8);
            shape_segment(*state, seg_face, size_px, utf8, length, seg_start, seg_end - seg_start, cluster_base, out);
            seg_start = seg_end;
            seg_face = face;
        }
    }

    if (seg_face != INVALID_FACE && seg_start < length)
    {
        shape_segment(*state, seg_face, size_px, utf8, length, seg_start, length - seg_start, cluster_base, out);
    }
}

Fixed Shaper::measure(
    int family,
    Style style,
    uint32_t size_px,
    const char *utf8,
    uint32_t length
) const
{
    if (!utf8 || length == 0)
    {
        return 0;
    }

    std::string key;
    key.reserve(length + 8);
    key.push_back(static_cast<char>(family));
    key.push_back(static_cast<char>(style));
    key.push_back(static_cast<char>(size_px & 0xFF));
    key.push_back(static_cast<char>((size_px >> 8) & 0xFF));
    key.append(utf8, length);

    auto it = state->measure_cache.find(key);
    if (it != state->measure_cache.end())
    {
        return it->second;
    }

    std::vector<PositionedGlyph> glyphs;
    shape(family, style, size_px, utf8, length, 0, glyphs);

    Fixed total = 0;
    for (const auto &g : glyphs)
    {
        total += g.x_advance;
    }

    if (state->measure_cache.size() >= MEASURE_CACHE_LIMIT)
    {
        state->measure_cache.clear();
    }
    state->measure_cache.emplace(std::move(key), total);

    return total;
}

void Shaper::clear_cache()
{
    state->measure_cache.clear();
}

} // namespace text
