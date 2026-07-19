#include "./screenshot.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO_UNUSED
#include "extern/stb/stb_image_write.h"

#include <vector>

bool write_surface_png(const SDL_Surface *surface, const std::string &path)
{
    if (!surface)
    {
        return false;
    }

    SDL_Surface *s = const_cast<SDL_Surface *>(surface);
    std::vector<unsigned char> rgb(static_cast<size_t>(s->w) * s->h * 3);

    if (SDL_MUSTLOCK(s))
    {
        SDL_LockSurface(s);
    }

    const int bpp = s->format->BytesPerPixel;
    for (int y = 0; y < s->h; ++y)
    {
        const unsigned char *row = static_cast<const unsigned char *>(s->pixels) + y * s->pitch;
        for (int x = 0; x < s->w; ++x)
        {
            const unsigned char *p = row + x * bpp;
            Uint32 value = 0;
            switch (bpp)
            {
                case 4: value = *reinterpret_cast<const Uint32 *>(p); break;
                case 2: value = *reinterpret_cast<const Uint16 *>(p); break;
                default: value = *p; break;
            }

            Uint8 r, g, b;
            SDL_GetRGB(value, s->format, &r, &g, &b);

            const size_t i = (static_cast<size_t>(y) * s->w + x) * 3;
            rgb[i] = r; rgb[i + 1] = g; rgb[i + 2] = b;
        }
    }

    if (SDL_MUSTLOCK(s))
    {
        SDL_UnlockSurface(s);
    }

    return stbi_write_png(path.c_str(), s->w, s->h, 3, rgb.data(), s->w * 3) != 0;
}
