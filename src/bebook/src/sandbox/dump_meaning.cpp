// Renders the dictionary popup (WordMeaningView) to a PNG so its look can be judged by eye.
//
// Run:  ./build/<platform>/sandbox meaning facevo out.png [tab_index]
//
// Draws over a neutral grey "page" so the modal's dimming mask reads realistically.

#include "reader/views/word_meaning_view.h"
#include "reader/system_styling.h"
#include "reader/config.h"

#include "lexicon/lexicon_service.h"

#include "sys/keymap.h"
#include "sys/screen.h"
#include "util/screenshot.h"

#include <SDL/SDL.h>

#include <iostream>
#include <string>

void dump_meaning(const std::string &word, const std::string &out_path, int tab_index)
{
    SDL_Surface *surface = SDL_CreateRGBSurface(
        SDL_SWSURFACE, SCREEN_WIDTH, SCREEN_HEIGHT, 32,
        0x00FF0000, 0x0000FF00, 0x000000FF, 0
    );
    SDL_FillRect(surface, nullptr, SDL_MapRGB(surface->format, 42, 42, 50));  // stand-in page

    SystemStyling styling(DEFAULT_FONT_NAME, DEFAULT_FONT_SIZE, DEFAULT_COLOR_THEME, DEFAULT_SHOULDER_KEYMAP);
    lexicon::LexiconService lex(LEXICON_DB_PATH);
    if (!lex.ok())
    {
        std::cerr << "lexicon not ok at " << LEXICON_DB_PATH << "\n";
    }

    WordMeaningView view(word, lex, styling);
    for (int i = 0; i < tab_index; ++i)
    {
        view.on_keypress(SW_BTN_RIGHT);  // step to the requested tab
    }
    view.render(surface, true);

    if (!write_surface_png(surface, out_path))
    {
        std::cerr << "failed to write " << out_path << "\n";
    }
    SDL_FreeSurface(surface);
}
