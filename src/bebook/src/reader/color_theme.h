#ifndef COLOR_THEME_H_
#define COLOR_THEME_H_

#include "SDL/SDL_video.h"

struct ColorTheme
{
    SDL_Color background;
    SDL_Color main_text;
    SDL_Color secondary_text;
    SDL_Color highlight_background;
    SDL_Color highlight_text;

    // Hypertext only, so unused by books. A word that links elsewhere is tinted, and
    // takes a different highlight from an ordinary word when selected -- which is what
    // tells the reader, before pressing anything, that the button will navigate.
    SDL_Color link_background;
    SDL_Color link_highlight_background;
};

#endif
