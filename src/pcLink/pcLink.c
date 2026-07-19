/**
 * pcLink - share the SD card with a computer over the device's own Wi-Fi hotspot.
 *
 * The Miyoo Mini Plus USB-C port is charge-only: its data lines are not routed to
 * the SoC, so the card cannot be mounted over a cable. This is the closest
 * equivalent - the device becomes its own access point and serves the card over
 * SMB, so a computer joins it directly with no router, no internet and no network
 * credentials beyond one fixed password.
 *
 * All of the plumbing already existed for netplay; this only drives it and shows
 * the user what to type. See sharing.h.
 */

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "system/keymap_sw.h"
#include "system/lang.h"
#include "system/settings.h"
#include "theme/theme.h"
#include "utils/keystate.h"
#include "utils/log.h"
#include "utils/sdl_init.h"

#include "./render.h"
#include "./sharing.h"

#define FRAMES_PER_SECOND 30

// Status line and client count are polled; no need to do it every frame.
#define STATUS_POLL_MS 1000

typedef enum View { VIEW_IDLE,
                    VIEW_STARTING,
                    VIEW_SHARING,
                    VIEW_STOPPING,
                    VIEW_ERROR } View;

static bool quit = false;

// Set from the signal handler, so teardown happens on the main thread rather than
// calling system() from a handler.
static volatile sig_atomic_t terminated = 0;

static void sigHandler(int sig)
{
    switch (sig) {
    case SIGINT:
    case SIGTERM:
        terminated = 1;
        quit = true;
        break;
    default:
        break;
    }
}

int main(void)
{
    log_setName("pcLink");

    // pressMenu2Kill can SIGTERM this app while the hotspot is up, so teardown has
    // to survive signals - otherwise the user is left with no Wi-Fi and no
    // indication why.
    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);

    SDL_InitDefault();

    // settings_load() must precede lang_load(): the language file is named by
    // settings.language.
    settings_load();
    lang_load();

    View view = VIEW_IDLE;
    SharingStep step = STEP_HOTSPOT;

    char ssid[STR_MAX] = "", ap_pass[STR_MAX] = "", address[STR_MAX] = "";

    KeyState keystate[320] = {(KeyState)0};
    bool redraw = true;

    uint32_t acc_ticks = 0, last_ticks = SDL_GetTicks(),
             time_step = 1000 / FRAMES_PER_SECOND;
    uint32_t last_poll = 0;

    while (!quit) {
        uint32_t ticks = SDL_GetTicks();
        acc_ticks += ticks - last_ticks;
        last_ticks = ticks;

        if (updateKeystate(keystate, &quit, true, NULL)) {
            if (keystate[SW_BTN_MENU] == PRESSED) {
                quit = true;
            }
            else if (keystate[SW_BTN_B] == PRESSED) {
                // While sharing, B stops sharing rather than exiting, so the user
                // can't walk away leaving the device in AP mode by accident.
                if (view == VIEW_SHARING)
                    view = VIEW_STOPPING;
                else
                    quit = true;
                redraw = true;
            }
            else if (keystate[SW_BTN_A] == PRESSED) {
                if (view == VIEW_IDLE || view == VIEW_ERROR) {
                    step = STEP_HOTSPOT;
                    view = VIEW_STARTING;
                }
                else if (view == VIEW_SHARING) {
                    view = VIEW_STOPPING;
                }
                redraw = true;
            }
        }

        if (acc_ticks < time_step)
            continue;

        // Refresh the status line and client count periodically.
        if (view == VIEW_SHARING && ticks - last_poll >= STATUS_POLL_MS) {
            last_poll = ticks;
            redraw = true;
        }

        if (redraw) {
            switch (view) {
            case VIEW_IDLE:
                render_idle(screen);
                break;
            case VIEW_STARTING:
                render_starting(screen, step);
                break;
            case VIEW_SHARING:
                render_sharing(screen, ssid, ap_pass, address);
                break;
            case VIEW_STOPPING:
                render_stopping(screen);
                break;
            case VIEW_ERROR:
                render_error(screen, step);
                break;
            }

            SDL_BlitSurface(screen, NULL, video, NULL);
            SDL_Flip(video);

            redraw = false;
        }

        // Blocking work runs *after* the frame is drawn, so the screen already
        // shows what is happening while it runs.
        if (view == VIEW_STARTING) {
            if (!sharing_runStep(step)) {
                printf_debug("Step %d failed\n", (int)step);
                sharing_stop(); // don't leave a half-configured radio behind
                view = VIEW_ERROR;
            }
            else if (++step >= STEP_COUNT) {
                sharing_getApCredentials(ssid, ap_pass, STR_MAX);
                sharing_getApAddress(address, STR_MAX);
                printf_debug("Sharing on %s at %s\n", ssid, address);
                view = VIEW_SHARING;
            }
            redraw = true;
        }
        else if (view == VIEW_STOPPING) {
            sharing_stop();
            view = VIEW_IDLE;
            redraw = true;
        }

        acc_ticks -= time_step;
    }

    // Unconditional teardown. Covers MENU, B, SIGTERM, and quitting mid-startup:
    // sharing_stop() is safe when nothing is running.
    if (terminated)
        printf_debug("Terminated by signal, tearing down\n");
    sharing_stop();

    SDL_FillRect(video, NULL, 0);
    SDL_Flip(video);

    lang_free();
    resources_free();
    SDL_FreeSurface(screen);
    SDL_FreeSurface(video);
    SDL_Quit();

    return EXIT_SUCCESS;
}
