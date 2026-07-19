#include "./cover_extract.h"

#include "filetypes/epub/epub_reader.h"
#include "util/sdl_utils.h"
#include "util/str_utils.h"

#include "extern/rotozoom/SDL_rotozoom.h"

#include <algorithm>

namespace
{

// Covers are decoded into a fixed 32-bit layout rather than the screen format: indexing
// runs before (and independently of) any video mode being set, and rotozoom's smooth
// filter only handles 32bpp anyway.
surface_unique_ptr make_format_template()
{
    return surface_unique_ptr{
        SDL_CreateRGBSurface(
            SDL_SWSURFACE, 1, 1, 32,
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
            0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF
#else
            0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000
#endif
        )
    };
}

} // namespace

surface_unique_ptr extract_cover(const std::filesystem::path &book_path, int max_w, int max_h)
{
    if (max_w <= 0 || max_h <= 0 || to_lower(book_path.extension().string()) != ".epub")
    {
        return nullptr;
    }

    EPubReader reader(book_path);
    if (!reader.open())
    {
        return nullptr;
    }

    const std::string &cover_href = reader.get_metadata().cover_href;
    if (cover_href.empty())
    {
        return nullptr;
    }

    auto cover_data = reader.load_resource(cover_href);
    if (cover_data.empty())
    {
        return nullptr;
    }

    std::string file_ext = std::filesystem::path(cover_href).extension().string();
    if (!file_ext.empty())
    {
        file_ext.erase(0, 1);
    }

    auto format_template = make_format_template();
    if (!format_template)
    {
        return nullptr;
    }

    auto cover = load_surface_from_ptr(
        cover_data.data(),
        cover_data.size(),
        file_ext,
        format_template->format
    );
    if (!cover || cover->w <= 0 || cover->h <= 0)
    {
        return nullptr;
    }

    double scale = std::min(
        static_cast<double>(max_w) / cover->w,
        static_cast<double>(max_h) / cover->h
    );
    if (scale >= 1.0)
    {
        // Enlarging a small cover only blurs it; leave it at native size.
        return cover;
    }

    surface_unique_ptr scaled{ zoomSurface(cover.get(), scale, scale, SMOOTHING_ON) };
    return scaled ? std::move(scaled) : std::move(cover);
}
