#include "./font.h"

#include "./face_cache.h"
#include "./shaper.h"
#include "./text_render.h"

#include "util/str_utils.h"

#include <cstring>
#include <deque>
#include <filesystem>
#include <unordered_map>

namespace text
{

namespace
{

// Style faces are looked for beside the regular file under the conventional suffixes
// shipped by most OFL families (e.g. Literata-Regular.ttf / Literata-Italic.ttf).
std::string sibling_with_suffix(const std::string &regular_path, const char *const *suffixes)
{
    std::filesystem::path p(regular_path);
    const std::string ext = p.extension().string();
    std::string stem = p.stem().string();

    // Strip a trailing -Regular/-Roman/-Book if present, so "Literata-Regular" becomes
    // "Literata" before the style suffix is appended.
    for (const char *drop : {"-Regular", "-Roman", "-Book"})
    {
        const size_t n = std::strlen(drop);
        if (stem.size() > n && stem.compare(stem.size() - n, n, drop) == 0)
        {
            stem.erase(stem.size() - n);
            break;
        }
    }

    for (const char *const *s = suffixes; *s; ++s)
    {
        std::filesystem::path candidate = p.parent_path() / (stem + *s + ext);
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec))
        {
            return candidate.string();
        }
    }
    return std::string();
}

const char *const ITALIC_SUFFIXES[] = {"-Italic", "-It", "Italic", "-Oblique", nullptr};
const char *const BOLD_SUFFIXES[] = {"-Bold", "Bold", "-Semibold", nullptr};
const char *const BOLD_ITALIC_SUFFIXES[] = {"-BoldItalic", "-BoldIt", "BoldItalic", "-Bold-Italic", nullptr};

} // namespace

struct Engine::Impl
{
    FaceCache faces;
    Shaper shaper{faces};
    GlyphAtlas atlas{faces};

    // Path -> family index.
    std::unordered_map<std::string, int> families;

    // Font handles are handed out as stable pointers, so they live in a deque rather
    // than a vector (which would invalidate them on growth).
    std::deque<Font> fonts;
    std::unordered_map<uint64_t, Font *> font_lookup;
};

Engine::Engine() : impl(new Impl())
{
}

Engine::~Engine()
{
    delete impl;
}

Engine &Engine::instance()
{
    static Engine engine;
    return engine;
}

FaceCache &Engine::faces() { return impl->faces; }
Shaper &Engine::shaper() { return impl->shaper; }
GlyphAtlas &Engine::atlas() { return impl->atlas; }

void Engine::set_hinting(Hinting h) { impl->atlas.set_hinting(h); }

void Engine::add_fallback(const std::string &path)
{
    impl->faces.add_fallback(path);
}

Font *Engine::load(const std::string &path, uint32_t size_px, Style style)
{
    int family;
    auto it = impl->families.find(path);
    if (it != impl->families.end())
    {
        family = it->second;
    }
    else
    {
        FamilySpec spec;
        spec.name = path;
        spec.regular = path;
        spec.italic = sibling_with_suffix(path, ITALIC_SUFFIXES);
        spec.bold = sibling_with_suffix(path, BOLD_SUFFIXES);
        spec.bold_italic = sibling_with_suffix(path, BOLD_ITALIC_SUFFIXES);

        family = impl->faces.add_family(spec);
        if (family < 0)
        {
            return nullptr;
        }
        impl->families.emplace(path, family);
    }

    const uint64_t key = (static_cast<uint64_t>(family) << 40)
                       | (static_cast<uint64_t>(style) << 32)
                       | size_px;

    auto fit = impl->font_lookup.find(key);
    if (fit != impl->font_lookup.end())
    {
        return fit->second;
    }

    impl->fonts.push_back(Font{family, style, size_px});
    Font *font = &impl->fonts.back();
    impl->font_lookup.emplace(key, font);
    return font;
}

Fixed text_width(const Font *font, const char *utf8, uint32_t length)
{
    if (!font || !utf8 || length == 0)
    {
        return 0;
    }
    return Engine::instance().shaper().measure(
        font->family, font->style, font->size_px, utf8, length
    );
}

void text_size(const Font *font, const char *utf8, int *w, int *h)
{
    if (w) *w = 0;
    if (h) *h = 0;
    if (!font || !utf8)
    {
        return;
    }

    Engine &e = Engine::instance();
    const uint32_t len = static_cast<uint32_t>(std::strlen(utf8));

    if (w)
    {
        const Fixed advance = e.shaper().measure(font->family, font->style, font->size_px, utf8, len);
        // Round up: callers use this to decide whether text fits.
        *w = fixed_floor(advance + FIXED_ONE - 1);
    }
    if (h)
    {
        *h = font_line_height(font);
    }
}

int font_line_height(const Font *font)
{
    if (!font)
    {
        return 0;
    }
    Engine &e = Engine::instance();
    FaceId face = e.faces().primary_face(font->family, font->style);
    return e.faces().metrics(face, font->size_px).natural_line_height();
}

int font_ascent(const Font *font)
{
    if (!font)
    {
        return 0;
    }
    Engine &e = Engine::instance();
    FaceId face = e.faces().primary_face(font->family, font->style);
    return e.faces().metrics(face, font->size_px).ascent;
}

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
    const SDL_Rect *clip
)
{
    if (!dst || !font || !utf8 || length == 0)
    {
        return x;
    }

    Engine &e = Engine::instance();

    std::vector<PositionedGlyph> glyphs;
    e.shaper().shape(font->family, font->style, font->size_px, utf8, length, 0, glyphs);

    if (gaps > 0 && extra_total != 0)
    {
        // Split the adjustment exactly: every gap gets the same whole number of 1/64px
        // units, and the leftover units are handed out one per gap from the left. The
        // widths therefore differ by at most 1/64px, and their sum is exactly
        // `extra_total` -- so the line ends precisely on the column edge.
        const Fixed per_gap = extra_total / static_cast<Fixed>(gaps);
        Fixed remainder = extra_total - per_gap * static_cast<Fixed>(gaps);
        const Fixed remainder_step = remainder >= 0 ? 1 : -1;

        for (auto &g : glyphs)
        {
            if (g.cluster >= length || !is_whitespace(utf8[g.cluster]))
            {
                continue;
            }

            g.x_advance += per_gap;
            if (remainder != 0)
            {
                g.x_advance += remainder_step;
                remainder -= remainder_step;
            }
        }
    }

    if (trailing_hyphen)
    {
        // The hyphen exists only in the rendered line, never in the source text, so that
        // the reader's address arithmetic -- which counts source characters -- is not
        // disturbed by a break decision.
        e.shaper().shape(font->family, font->style, font->size_px, "-", 1, 0, glyphs);
    }

    TextPalette palette;
    palette.build(dst->format, fg, bg);

    const Fixed end = draw_glyphs_shaded(
        dst, glyphs, int_to_fixed(x), baseline_y, e.atlas(), font->size_px, palette, clip
    );
    return fixed_round(end);
}

int draw_text(
    SDL_Surface *dst,
    const Font *font,
    const char *utf8,
    int x,
    int baseline_y,
    SDL_Color fg,
    SDL_Color bg,
    const SDL_Rect *clip
)
{
    if (!dst || !font || !utf8 || !*utf8)
    {
        return x;
    }

    Engine &e = Engine::instance();

    std::vector<PositionedGlyph> glyphs;
    e.shaper().shape(
        font->family, font->style, font->size_px,
        utf8, static_cast<uint32_t>(std::strlen(utf8)), 0, glyphs
    );

    TextPalette palette;
    palette.build(dst->format, fg, bg);

    const Fixed end = draw_glyphs_shaded(
        dst, glyphs, int_to_fixed(x), baseline_y, e.atlas(), font->size_px, palette, clip
    );
    return fixed_round(end);
}

surface_unique_ptr render_text_shaded(
    const Font *font,
    const char *utf8,
    SDL_Color fg,
    SDL_Color bg,
    SDL_PixelFormat *format
)
{
    if (!font || !utf8 || !*utf8)
    {
        return nullptr;
    }

    int w = 0;
    text_size(font, utf8, &w, nullptr);
    const int h = font_line_height(font);
    if (w <= 0 || h <= 0)
    {
        return nullptr;
    }

    surface_unique_ptr surface{
        format
            ? SDL_CreateRGBSurface(
                  SDL_SWSURFACE, w, h, format->BitsPerPixel,
                  format->Rmask, format->Gmask, format->Bmask, 0
              )
            : SDL_CreateRGBSurface(
                  SDL_SWSURFACE, w, h, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0
              )
    };
    if (!surface)
    {
        return nullptr;
    }

    SDL_FillRect(
        surface.get(), nullptr,
        SDL_MapRGB(surface->format, bg.r, bg.g, bg.b)
    );

    draw_text(surface.get(), font, utf8, 0, font_ascent(font), fg, bg);

    return surface;
}

} // namespace text
