#ifndef SDL_FONT_CACHE_H_
#define SDL_FONT_CACHE_H_

#include "text/font.h"

#include <string>

enum class FontLoadErrorOpt
{
    NoThrow,
    ThrowOnError,
};

// Loads a font at a size, sharing the underlying face across every caller.
//
// Returns a text::Font rather than a TTF_Font: the reader no longer uses SDL_ttf, whose
// integer-rounded advances and kerning limited to the legacy `kern` table capped text
// quality. The signature is otherwise unchanged so call sites read the same.
text::Font *cached_load_font(const std::string &font_path, uint32_t size, FontLoadErrorOpt opt = FontLoadErrorOpt::ThrowOnError);

#endif
