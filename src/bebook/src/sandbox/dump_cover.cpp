#include "filetypes/epub/epub_reader.h"
#include "reader/cover_extract.h"
#include "util/screenshot.h"

#include <iostream>

// Prints a book's bibliographic metadata and writes its cover thumbnail to `out_path`.
// The device has no way to show decode failures, so this is how covers get eyeballed.
void dump_cover(const std::string &book_path, const std::string &out_path)
{
    {
        EPubReader reader(book_path);
        if (!reader.open())
        {
            std::cerr << "Failed to open " << book_path << std::endl;
            return;
        }

        const auto &metadata = reader.get_metadata();
        std::cout << "title:  " << metadata.title << std::endl;
        std::cout << "author: " << metadata.author << std::endl;
        std::cout << "cover:  " << metadata.cover_href << std::endl;
    }

    auto cover = extract_cover(book_path, 200, 300);
    if (!cover)
    {
        std::cerr << "No cover" << std::endl;
        return;
    }

    std::cout << "size:   " << cover->w << "x" << cover->h << std::endl;

    if (!write_surface_png(cover.get(), out_path))
    {
        std::cerr << "Failed to write " << out_path << std::endl;
    }
}
