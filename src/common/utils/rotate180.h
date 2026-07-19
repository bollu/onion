#ifndef ROTATE_180_H__
#define ROTATE_180_H__

#include "SDL/SDL_rotozoom.h"
#include <SDL/SDL.h>
#include <assert.h>

SDL_Surface *rotate180(SDL_Surface *original)
{
    // A NULL surface here means the caller's IMG_Load failed - most often a
    // missing theme asset, or a libSDL_image without PNG support, since it
    // dlopen()s libpng rather than linking it. Without this the next line
    // dereferences original->format and dies with a bare segfault a long way
    // from the actual cause.
    assert(original != NULL && "rotate180: image failed to load");

    SDL_Surface *rotated = rotozoomSurface(original, 180.0, 1.0, 0);
    SDL_FillRect(original, NULL, SDL_MapRGB(original->format, 255, 0, 0));
    SDL_Rect rect = {-2, -2};
    SDL_BlitSurface(rotated, NULL, original, &rect);
    SDL_FreeSurface(rotated);
    return original;
}

#endif // ROTATE_180_H__
