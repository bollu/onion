#include "./sdl_font_cache.h"

#include <iostream>
#include <stdexcept>

text::Font *cached_load_font(const std::string &font_path, uint32_t size, FontLoadErrorOpt opt)
{
    // text::Engine already caches by (family, style, size) and shares FT_Face objects
    // across sizes, so there is nothing left for this layer to memoise.
    text::Font *font = text::Engine::instance().load(font_path, size);

    if (!font)
    {
        std::cerr << "Failed to load font: " << font_path << " " << size << std::endl;

        if (opt == FontLoadErrorOpt::ThrowOnError)
        {
            throw std::runtime_error("Failed to load font");
        }
    }

    return font;
}
