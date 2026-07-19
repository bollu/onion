#include "./text_render.h"

#include <algorithm>
#include <cmath>

namespace text
{

namespace
{

// sRGB transfer function, per IEC 61966-2-1.
float srgb_to_linear(uint8_t v)
{
    float s = v / 255.0f;
    return s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
}

uint8_t linear_to_srgb(float l)
{
    l = std::max(0.0f, std::min(1.0f, l));
    float s = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
    return static_cast<uint8_t>(s * 255.0f + 0.5f);
}

struct ClipRect
{
    int x0, y0, x1, y1;
};

ClipRect clip_for(const SDL_Surface *dst, const SDL_Rect *clip)
{
    ClipRect c{0, 0, dst->w, dst->h};
    if (clip)
    {
        c.x0 = std::max<int>(c.x0, clip->x);
        c.y0 = std::max<int>(c.y0, clip->y);
        c.x1 = std::min<int>(c.x1, clip->x + clip->w);
        c.y1 = std::min<int>(c.y1, clip->y + clip->h);
    }
    return c;
}

} // namespace

TextPalette::TextPalette()
{
    for (int i = 0; i < 256; ++i)
    {
        entries[i] = 0;
    }
}

void TextPalette::build(const SDL_PixelFormat *format, SDL_Color fg, SDL_Color bg)
{
    const float fr = srgb_to_linear(fg.r), fg_ = srgb_to_linear(fg.g), fb = srgb_to_linear(fg.b);
    const float br = srgb_to_linear(bg.r), bg_ = srgb_to_linear(bg.g), bb = srgb_to_linear(bg.b);

    for (int i = 0; i < 256; ++i)
    {
        const float a = i / 255.0f;
        entries[i] = SDL_MapRGB(
            const_cast<SDL_PixelFormat *>(format),
            linear_to_srgb(br + (fr - br) * a),
            linear_to_srgb(bg_ + (fg_ - bg_) * a),
            linear_to_srgb(bb + (fb - bb) * a)
        );
    }
}

namespace
{

// Walks the glyphs of a run, resolving each to a rasterized bitmap and its integer
// destination position, and hands them to `emit`. Shared by both draw paths so the
// positioning logic exists in exactly one place.
template <typename EmitFn>
Fixed for_each_glyph(
    const std::vector<PositionedGlyph> &glyphs,
    Fixed origin_x,
    int baseline_y,
    GlyphAtlas &atlas,
    uint32_t size_px,
    EmitFn emit
)
{
    Fixed pen = origin_x;

    for (const auto &g : glyphs)
    {
        const Fixed gx = pen + g.x_offset;

        // The integer part positions the blit; the fractional part selects which
        // sub-pixel rasterization of the glyph to use.
        const int phase = GlyphAtlas::phase_for(gx);
        const GlyphBitmap *bm = atlas.get(g.face_id, size_px, g.glyph_index, phase);

        if (bm && bm->width > 0 && bm->height > 0)
        {
            const int dst_x = fixed_floor(gx) + bm->bearing_x;
            const int dst_y = baseline_y - bm->bearing_y - fixed_round(g.y_offset);
            emit(*bm, dst_x, dst_y);
        }

        pen += g.x_advance;
    }

    return pen;
}

} // namespace

Fixed draw_glyphs_shaded(
    SDL_Surface *dst,
    const std::vector<PositionedGlyph> &glyphs,
    Fixed origin_x,
    int baseline_y,
    GlyphAtlas &atlas,
    uint32_t size_px,
    const TextPalette &palette,
    const SDL_Rect *clip
)
{
    if (!dst)
    {
        return origin_x;
    }

    const ClipRect c = clip_for(dst, clip);
    const int bpp = dst->format->BytesPerPixel;
    uint8_t *pixels = static_cast<uint8_t *>(dst->pixels);

    return for_each_glyph(
        glyphs, origin_x, baseline_y, atlas, size_px,
        [&](const GlyphBitmap &bm, int dst_x, int dst_y) {
            const int x_begin = std::max(c.x0, dst_x);
            const int x_end = std::min(c.x1, dst_x + bm.width);
            const int y_begin = std::max(c.y0, dst_y);
            const int y_end = std::min(c.y1, dst_y + bm.height);

            for (int y = y_begin; y < y_end; ++y)
            {
                const uint8_t *src = bm.coverage + static_cast<size_t>(y - dst_y) * bm.width;
                uint8_t *row = pixels + static_cast<size_t>(y) * dst->pitch;

                for (int x = x_begin; x < x_end; ++x)
                {
                    const uint8_t a = src[x - dst_x];
                    if (!a)
                    {
                        continue;
                    }

                    const uint32_t value = palette[a];
                    uint8_t *p = row + static_cast<size_t>(x) * bpp;

                    switch (bpp)
                    {
                        case 4: *reinterpret_cast<uint32_t *>(p) = value; break;
                        case 2: *reinterpret_cast<uint16_t *>(p) = static_cast<uint16_t>(value); break;
                        default: *p = static_cast<uint8_t>(value); break;
                    }
                }
            }
        }
    );
}

Fixed draw_glyphs_blended(
    SDL_Surface *dst,
    const std::vector<PositionedGlyph> &glyphs,
    Fixed origin_x,
    int baseline_y,
    GlyphAtlas &atlas,
    uint32_t size_px,
    SDL_Color fg,
    const SDL_Rect *clip
)
{
    if (!dst)
    {
        return origin_x;
    }

    const ClipRect c = clip_for(dst, clip);
    const int bpp = dst->format->BytesPerPixel;
    uint8_t *pixels = static_cast<uint8_t *>(dst->pixels);

    const float fr = srgb_to_linear(fg.r);
    const float fgn = srgb_to_linear(fg.g);
    const float fb = srgb_to_linear(fg.b);

    // Destination values repeat heavily, so memoise the sRGB->linear direction.
    static float to_linear[256];
    static bool to_linear_ready = false;
    if (!to_linear_ready)
    {
        for (int i = 0; i < 256; ++i)
        {
            to_linear[i] = srgb_to_linear(static_cast<uint8_t>(i));
        }
        to_linear_ready = true;
    }

    return for_each_glyph(
        glyphs, origin_x, baseline_y, atlas, size_px,
        [&](const GlyphBitmap &bm, int dst_x, int dst_y) {
            const int x_begin = std::max(c.x0, dst_x);
            const int x_end = std::min(c.x1, dst_x + bm.width);
            const int y_begin = std::max(c.y0, dst_y);
            const int y_end = std::min(c.y1, dst_y + bm.height);

            for (int y = y_begin; y < y_end; ++y)
            {
                const uint8_t *src = bm.coverage + static_cast<size_t>(y - dst_y) * bm.width;
                uint8_t *row = pixels + static_cast<size_t>(y) * dst->pitch;

                for (int x = x_begin; x < x_end; ++x)
                {
                    const uint8_t a = src[x - dst_x];
                    if (!a)
                    {
                        continue;
                    }

                    uint8_t *p = row + static_cast<size_t>(x) * bpp;
                    uint32_t existing = 0;
                    switch (bpp)
                    {
                        case 4: existing = *reinterpret_cast<uint32_t *>(p); break;
                        case 2: existing = *reinterpret_cast<uint16_t *>(p); break;
                        default: existing = *p; break;
                    }

                    uint8_t dr, dg, db;
                    SDL_GetRGB(existing, dst->format, &dr, &dg, &db);

                    const float alpha = a / 255.0f;
                    const uint32_t value = SDL_MapRGB(
                        dst->format,
                        linear_to_srgb(to_linear[dr] + (fr - to_linear[dr]) * alpha),
                        linear_to_srgb(to_linear[dg] + (fgn - to_linear[dg]) * alpha),
                        linear_to_srgb(to_linear[db] + (fb - to_linear[db]) * alpha)
                    );

                    switch (bpp)
                    {
                        case 4: *reinterpret_cast<uint32_t *>(p) = value; break;
                        case 2: *reinterpret_cast<uint16_t *>(p) = static_cast<uint16_t>(value); break;
                        default: *p = static_cast<uint8_t>(value); break;
                    }
                }
            }
        }
    );
}

Fixed run_advance(const std::vector<PositionedGlyph> &glyphs)
{
    Fixed total = 0;
    for (const auto &g : glyphs)
    {
        total += g.x_advance;
    }
    return total;
}

} // namespace text
