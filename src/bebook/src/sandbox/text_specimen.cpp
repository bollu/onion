// Renders type specimens to PNG so the text engine can be judged by eye rather than by
// unit test. Typography is a visual property; this is the harness the plan calls for.
//
// Build:  make specimen
// Run:    ./build/specimen <out-dir> [font.ttf]

#include "text/face_cache.h"
#include "text/glyph_atlas.h"
#include "text/shaper.h"
#include "text/text_render.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "extern/stb/stb_image_write.h"

#include <SDL/SDL.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{

constexpr int WIDTH = 640;   // the Miyoo Mini's panel
constexpr int MARGIN = 28;

struct Canvas
{
    SDL_Surface *surface;

    Canvas(int w, int h, SDL_Color bg)
    {
        surface = SDL_CreateRGBSurface(
            SDL_SWSURFACE, w, h, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0
        );
        SDL_FillRect(surface, nullptr, SDL_MapRGB(surface->format, bg.r, bg.g, bg.b));
    }

    ~Canvas() { if (surface) SDL_FreeSurface(surface); }

    void write_png(const std::string &path) const
    {
        std::vector<uint8_t> rgb(static_cast<size_t>(surface->w) * surface->h * 3);
        for (int y = 0; y < surface->h; ++y)
        {
            const uint8_t *row = static_cast<const uint8_t *>(surface->pixels) + y * surface->pitch;
            for (int x = 0; x < surface->w; ++x)
            {
                uint8_t r, g, b;
                SDL_GetRGB(*reinterpret_cast<const uint32_t *>(row + x * 4), surface->format, &r, &g, &b);
                size_t i = (static_cast<size_t>(y) * surface->w + x) * 3;
                rgb[i] = r; rgb[i + 1] = g; rgb[i + 2] = b;
            }
        }
        stbi_write_png(path.c_str(), surface->w, surface->h, 3, rgb.data(), surface->w * 3);
        std::printf("wrote %s (%dx%d)\n", path.c_str(), surface->w, surface->h);
    }
};

// Naive greedy wrap, standing in until Knuth-Plass lands in WP3. Good enough to show
// off shaping, subpixel positioning and blending.
void draw_paragraph(
    Canvas &canvas,
    text::FaceCache &faces,
    text::Shaper &shaper,
    text::GlyphAtlas &atlas,
    int family,
    text::Style style,
    uint32_t size_px,
    const char *body,
    int &y,
    const text::TextPalette &palette
)
{
    const text::Fixed avail = text::int_to_fixed(WIDTH - 2 * MARGIN);
    const auto metrics = faces.metrics(faces.primary_face(family, style), size_px);
    const int line_height = metrics.natural_line_height();

    std::string text_body(body);
    size_t pos = 0;

    while (pos < text_body.size())
    {
        size_t best_end = std::string::npos;
        size_t probe = pos;

        while (true)
        {
            size_t space = text_body.find(' ', probe);
            size_t candidate = (space == std::string::npos) ? text_body.size() : space;

            text::Fixed w = shaper.measure(
                family, style, size_px, text_body.data() + pos, static_cast<uint32_t>(candidate - pos)
            );
            if (w > avail && best_end != std::string::npos)
            {
                break;
            }
            best_end = candidate;
            if (candidate == text_body.size())
            {
                break;
            }
            probe = candidate + 1;
        }

        std::vector<text::PositionedGlyph> glyphs;
        shaper.shape(
            family, style, size_px,
            text_body.data() + pos, static_cast<uint32_t>(best_end - pos), 0, glyphs
        );

        y += metrics.ascent;
        text::draw_glyphs_shaded(
            canvas.surface, glyphs, text::int_to_fixed(MARGIN), y, atlas, size_px, palette
        );
        y += line_height - metrics.ascent;

        pos = (best_end >= text_body.size()) ? text_body.size() : best_end + 1;
    }
}

const char *SAMPLE =
    "It is a truth universally acknowledged, that a single man in possession of a good "
    "fortune, must be in want of a wife. However little known the feelings or views of "
    "such a man may be on his first entering a neighbourhood, this truth is so well "
    "fixed in the minds of the surrounding families, that he is considered as the "
    "rightful property of some one or other of their daughters.";

} // namespace

int main(int argc, char **argv)
{
    const std::string out_dir = argc > 1 ? argv[1] : ".";
    const std::string font = argc > 2 ? argv[2] : "resources/fonts/DejaVuSerif.ttf";

    SDL_Init(SDL_INIT_VIDEO);

    text::FaceCache faces;
    text::FamilySpec spec;
    spec.name = "body";
    spec.regular = font;
    int family = faces.add_family(spec);
    if (family < 0)
    {
        std::fprintf(stderr, "could not open %s\n", font.c_str());
        return 1;
    }

    text::Shaper shaper(faces);
    text::GlyphAtlas atlas(faces);

    const SDL_Color light_bg{235, 232, 225, 0};
    const SDL_Color light_fg{28, 26, 24, 0};
    const SDL_Color dark_bg{18, 18, 20, 0};
    const SDL_Color dark_fg{222, 220, 214, 0};

    struct Variant { const char *name; text::Hinting hinting; bool dark; };
    const Variant variants[] = {
        {"hint-none-light",   text::Hinting::None,   false},
        {"hint-light-light",  text::Hinting::Light,  false},
        {"hint-normal-light", text::Hinting::Normal, false},
        {"hint-light-dark",   text::Hinting::Light,  true},
    };

    for (const auto &v : variants)
    {
        atlas.set_hinting(v.hinting);

        const SDL_Color bg = v.dark ? dark_bg : light_bg;
        const SDL_Color fg = v.dark ? dark_fg : light_fg;

        Canvas canvas(WIDTH, 480, bg);
        text::TextPalette palette;
        palette.build(canvas.surface->format, fg, bg);

        int y = MARGIN;
        draw_paragraph(canvas, faces, shaper, atlas, family, text::Style::Regular, 26,
                       SAMPLE, y, palette);

        canvas.write_png(out_dir + "/specimen-" + v.name + ".png");
    }

    SDL_Quit();
    return 0;
}
