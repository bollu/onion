#ifndef COVER_EXTRACT_H_
#define COVER_EXTRACT_H_

#include "util/sdl_pointer.h"

#include <filesystem>

// Decodes a book's cover and scales it to fit within (max_w, max_h) preserving aspect.
// Returns nullptr if the book has no cover or it cannot be decoded.
//
// Opens the book's zip, so this is I/O bound; keep it off the render thread.
surface_unique_ptr extract_cover(const std::filesystem::path &book_path, int max_w, int max_h);

#endif
