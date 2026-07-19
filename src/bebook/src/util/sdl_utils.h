#ifndef SDL_UTILS_H_
#define SDL_UTILS_H_

#include "./sdl_pointer.h"
#include "text/font.h"

#include <string>

int detect_line_height(const text::Font *font);
int detect_line_height(const std::string &font, uint32_t size);

// Decodes an in-memory image into a surface matching `surface_format`.
//
// `img_format` is the file extension and is now only a hint for diagnostics: the decoder
// sniffs the actual container, which is more robust than the previous behaviour of
// trusting the extension from the epub's href.
surface_unique_ptr load_surface_from_ptr(const char *data, uint32_t size, const std::string &img_format, SDL_PixelFormat *surface_format);

#endif
