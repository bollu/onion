// A minimal themed app to copy when starting a new one. See README.md.

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>

// theme/theme.h, not the individual render headers: it orders them correctly.
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

    // Creates `video` (hardware) and `screen` (what we draw into).
    SDL_InitDefault();

    // settings_load() first: it names the language file lang_load() reads.
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
                // Centred using the real surface size, so any resolution works.
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

    // Blank rather than a stale frame.
    SDL_FillRect(video, NULL, 0);
    SDL_Flip(video);

    lang_free();
    resources_free();
    SDL_FreeSurface(screen);
    SDL_FreeSurface(video);
    SDL_Quit();

    return EXIT_SUCCESS;
}
