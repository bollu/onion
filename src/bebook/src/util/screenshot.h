#ifndef SCREENSHOT_H_
#define SCREENSHOT_H_

#include <SDL/SDL.h>
#include <string>

// Writes an SDL surface out as a PNG. Used by the BEBOOK_SCREENSHOT development hook in
// main(), and by the packaging scripts to generate cover art.
bool write_surface_png(const SDL_Surface *surface, const std::string &path);

#endif
