// Renders the dictionary app's SearchView to a PNG so its keyboard/results layout can be
// judged by eye. Run:  ./build/<platform>/sandbox dict <query> out.png

#include "dict/views/search_view.h"

#include "reader/config.h"
#include "reader/system_styling.h"
#include "reader/view_stack.h"

#include "lexicon/lexicon_service.h"

#include "sys/screen.h"
#include "util/screenshot.h"

#include <SDL/SDL.h>

#include <iostream>
#include <string>

void dump_search(const std::string &query, const std::string &out_path)
{
    SDL_Surface *surface = SDL_CreateRGBSurface(
        SDL_SWSURFACE, SCREEN_WIDTH, SCREEN_HEIGHT, 32,
        0x00FF0000, 0x0000FF00, 0x000000FF, 0);

    SystemStyling styling(DEFAULT_FONT_NAME, DEFAULT_FONT_SIZE, DEFAULT_COLOR_THEME, DEFAULT_SHOULDER_KEYMAP);
    lexicon::LexiconService lex(LEXICON_DB_PATH);
    ViewStack view_stack;

    SearchView view(lex, styling, view_stack);
    if (!query.empty())
    {
        view.set_query(query);
    }
    view.render(surface, true);

    if (!write_surface_png(surface, out_path))
    {
        std::cerr << "failed to write " << out_path << "\n";
    }
    SDL_FreeSurface(surface);
}
