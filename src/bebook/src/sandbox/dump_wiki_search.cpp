// Renders the wiki article-search screen to a PNG.
//
// Run:  ./build/<platform>/sandbox wikisearch <zim> <query> out.png

#include "wiki/wiki_context.h"
#include "wiki/views/article_search_view.h"

#include "reader/config.h"
#include "reader/system_styling.h"

#include "sys/screen.h"
#include "util/screenshot.h"

#include <SDL/SDL.h>

#include <iostream>
#include <string>

void dump_wiki_search(const std::string &zim_path, const std::string &query,
                      const std::string &out_path)
{
    WikiContext context;
    if (!context.open(zim_path))
    {
        std::cerr << "cannot open " << zim_path << ": " << context.error() << "\n";
        return;
    }

    SDL_Surface *surface = SDL_CreateRGBSurface(
        SDL_SWSURFACE, SCREEN_WIDTH, SCREEN_HEIGHT, 32,
        0x00FF0000, 0x0000FF00, 0x000000FF, 0);

    SystemStyling styling(DEFAULT_FONT_NAME, DEFAULT_FONT_SIZE, DEFAULT_COLOR_THEME,
                          DEFAULT_SHOULDER_KEYMAP);

    ArticleSearchView view(context, styling);
    view.set_query(query);
    view.render(surface, true);

    if (!write_surface_png(surface, out_path))
    {
        std::cerr << "failed to write " << out_path << "\n";
    }
    SDL_FreeSurface(surface);
}
