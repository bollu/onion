// Renders a TokenView -- the reading surface both bebook and bewiki use -- to a PNG, so the
// peek panel, the word cursor and the camera can be judged by eye without a device.
//
// Run:  ./build/<platform>/sandbox reader <zim> <article> out.png [moves] [theme]
//
// `moves` is how many times to press DOWN before rendering, which is how the camera gets
// exercised: at 0 the cursor sits wherever the document opened, and it should walk down the
// first half-page before the page starts scrolling under it.

#include "wiki/wiki_context.h"

#include "reader/config.h"
#include "reader/system_styling.h"
#include "reader/word_preview.h"
#include "reader/views/token_view/token_view.h"
#include "reader/views/token_view/token_view_styling.h"

#include "lexicon/lexicon_service.h"

#include "sys/keymap.h"
#include "sys/screen.h"
#include "util/screenshot.h"

#include <SDL/SDL.h>

#include <iostream>
#include <memory>
#include <string>

void dump_reader(const std::string &zim_path, const std::string &article,
                 const std::string &out_path, int moves, const std::string &theme)
{
    WikiContext context;
    if (!context.open(zim_path))
    {
        std::cerr << "cannot open " << zim_path << ": " << context.error() << "\n";
        return;
    }

    auto reader = context.open_article(article);
    if (!reader)
    {
        std::cerr << "no such article: " << article << "\n";
        return;
    }

    SDL_Surface *surface = SDL_CreateRGBSurface(
        SDL_SWSURFACE, SCREEN_WIDTH, SCREEN_HEIGHT, 32,
        0x00FF0000, 0x0000FF00, 0x000000FF, 0);

    SystemStyling styling(DEFAULT_FONT_NAME, DEFAULT_FONT_SIZE, DEFAULT_COLOR_THEME,
                          DEFAULT_SHOULDER_KEYMAP);
    if (!theme.empty())
    {
        styling.set_color_theme(theme);
    }
    TokenViewStyling token_styling(DEFAULT_JUSTIFY, DEFAULT_HYPHENATE);

    lexicon::LexiconService lexicon(LEXICON_DB_PATH);
    if (!lexicon.ok())
    {
        std::cerr << "warning: no lexicon at " << LEXICON_DB_PATH
                  << "; the peek panel will be blank\n";
    }

    TokenView view(reader, 0, styling, token_styling);
    view.set_follows_links(true);
    view.set_word_select_hint("A significato   X segui   B esci");
    view.set_on_word_preview([&lexicon](const std::string &surface) {
        return summarize_word(lexicon, surface);
    });

    for (int i = 0; i < moves; ++i)
    {
        view.on_keypress(SW_BTN_DOWN);
    }
    view.render(surface, true);

    if (!write_surface_png(surface, out_path))
    {
        std::cerr << "failed to write " << out_path << "\n";
    }
    SDL_FreeSurface(surface);
}
