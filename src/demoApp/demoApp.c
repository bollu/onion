/**
 * demoApp - a minimal "hello world" app, meant as a starting point for new apps.
 *
 * It draws a themed screen with a header, a line of text, and a footer hint, then
 * exits on B or MENU. Everything it does is the same way the real apps do it, so
 * copying this directory is a reasonable way to start something new.
 *
 * See README.md in this directory for how to wire a new app into the build.
 */

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>

// Include theme/theme.h rather than the individual theme/render/*.h headers: it
// pulls them in via theme/render.h in an order that resolves their interdependencies.
#include "system/battery.h"
#include "system/keymap_sw.h"
#include "system/lang.h"
#include "system/settings.h"
#include "theme/background.h"
#include "theme/theme.h"
#include "utils/keystate.h"
#include "utils/log.h"
#include "utils/sdl_init.h"

#define FRAMES_PER_SECOND 30

static bool quit = false;

static void sigHandler(int sig)
{
    switch (sig) {
    case SIGINT:
    case SIGTERM:
        quit = true;
        break;
    default:
        break;
    }
}

int main(void)
{
    log_setName("demoApp");

    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);

    // Creates `video` (the hardware surface) and `screen` (what we draw into),
    // and starts SDL/TTF. Both are declared in utils/sdl_init.h.
    SDL_InitDefault();

    // settings_load() must come before lang_load(): the language file to load is
    // named by settings.language.
    settings_load();
    lang_load();

    KeyState keystate[320] = {(KeyState)0};
    bool redraw = true;

    uint32_t acc_ticks = 0, last_ticks = SDL_GetTicks(),
             time_step = 1000 / FRAMES_PER_SECOND;

    while (!quit) {
        uint32_t ticks = SDL_GetTicks();
        acc_ticks += ticks - last_ticks;
        last_ticks = ticks;

        if (updateKeystate(keystate, &quit, true, NULL)) {
            if (keystate[SW_BTN_B] == PRESSED || keystate[SW_BTN_MENU] == PRESSED)
                quit = true;
        }

        if (acc_ticks < time_step)
            continue;

        if (redraw) {
            // Theme background, then header/footer, then our own content.
            SDL_BlitSurface(theme_background(), NULL, screen, NULL);
            theme_renderHeader(screen, "Demo App", false);

            TTF_Font *font = resource_getFont(TITLE);
            SDL_Surface *text =
                TTF_RenderUTF8_Blended(font, "Hello, world!", theme()->list.color);

            if (text != NULL) {
                // Centre the text in the screen, using the real surface size so
                // this stays correct on both 640x480 and higher-res devices.
                SDL_Rect pos = {(screen->w - text->w) / 2,
                                (screen->h - text->h) / 2};
                SDL_BlitSurface(text, NULL, screen, &pos);
                SDL_FreeSurface(text);
            }

            theme_renderFooter(screen);
            theme_renderStandardHint(
                screen, lang_get(LANG_SELECT, LANG_FALLBACK_SELECT),
                lang_get(LANG_BACK, LANG_FALLBACK_BACK));
            theme_renderHeaderBattery(screen, battery_getPercentage());

            SDL_BlitSurface(screen, NULL, video, NULL);
            SDL_Flip(video);

            redraw = false;
        }

        acc_ticks -= time_step;
    }

    // Leave a blank screen behind rather than a stale frame.
    SDL_FillRect(video, NULL, 0);
    SDL_Flip(video);

    lang_free();
    resources_free();
    SDL_FreeSurface(screen);
    SDL_FreeSurface(video);
    SDL_Quit();

    return EXIT_SUCCESS;
}
