#include "./sdl_utils.h"
#include "./sdl_font_cache.h"

// stb_image replaces SDL_image here. SDL_image 1.2 is unmaintained and no longer
// packaged by most systems, which made desktop builds impossible; stb_image is a
// public-domain header with no link-time dependency, covers the PNG/JPEG/GIF that epubs
// actually contain, and sniffs the container rather than trusting the file extension.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#include "extern/stb/stb_image.h"

#include <iostream>

int detect_line_height(const text::Font *font)
{
    int h = text::font_line_height(font);
    return h > 0 ? h : 24;
}

int detect_line_height(const std::string &font, uint32_t size)
{
    return detect_line_height(cached_load_font(font, size));
}

surface_unique_ptr load_surface_from_ptr(const char *data, uint32_t size, const std::string &img_format, SDL_PixelFormat *surface_format)
{
    int w = 0, h = 0, channels = 0;
    unsigned char *pixels = stbi_load_from_memory(
        reinterpret_cast<const stbi_uc *>(data),
        static_cast<int>(size),
        &w, &h, &channels,
        4  // always request RGBA so the surface layout below is fixed
    );

    if (!pixels)
    {
        std::cerr << "Failed to load image (" << img_format << "): "
                  << stbi_failure_reason() << std::endl;
        return nullptr;
    }

    // stb hands back tightly packed RGBA in memory order, so the masks are byte-order
    // dependent rather than the literal 0xFF000000 style constants.
    surface_unique_ptr loaded{
        SDL_CreateRGBSurfaceFrom(
            pixels, w, h, 32, w * 4,
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
            0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF
#else
            0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000
#endif
        )
    };

    if (!loaded)
    {
        stbi_image_free(pixels);
        return nullptr;
    }

    // Convert into the destination format, which also copies out of stb's buffer so it
    // can be released.
    surface_unique_ptr converted{
        surface_format
            ? SDL_ConvertSurface(loaded.get(), surface_format, 0)
            : SDL_DisplayFormat(loaded.get())
    };

    loaded.reset();
    stbi_image_free(pixels);

    return converted;
}
