#ifndef SDL_POINTER_H_
#define SDL_POINTER_H_

#include <SDL/SDL.h>
#include <memory>

// The TTF_Font deleter is gone with SDL_ttf; text::Font handles are owned by
// text::Engine and are never individually freed.
struct SDL_Deleter {
  void operator()(SDL_Surface* surface);
};

using surface_unique_ptr = std::unique_ptr<SDL_Surface, SDL_Deleter>;

#endif
