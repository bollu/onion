/**
 * musicPlayer - a native music player for Onion.
 *
 * Replaces the vendored GMU build (static/packages/App/Music Player (GMU)),
 * which ships a prebuilt binary plus eight decoder .so files and its own copy of
 * SDL 1.2. This links the system SDL_mixer instead, which on this device is built
 * against libmad and therefore decodes MP3 with nothing extra bundled.
 *
 * Two views: a library list and a Now Playing screen. Scope is MP3 only; see
 * library.h for why.
 */

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "components/list.h"
#include "system/keymap_sw.h"
#include "system/lang.h"
#include "system/settings.h"
#include "theme/theme.h"
#include "utils/keystate.h"
#include "utils/log.h"
#include "utils/sdl_init.h"

#include "./library.h"
#include "./player.h"
#include "./render.h"

#define FRAMES_PER_SECOND 30
#define SEEK_STEP_SECONDS 5.0

typedef enum View { VIEW_LIBRARY,
                    VIEW_NOW_PLAYING } View;

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

/** Shows the current track in the list's sticky note. */
static void updateNowPlayingNote(List *list, bool has_tracks)
{
    if (!has_tracks || !list->has_sticky)
        return;

    ListItem *sticky = &list->items[0];
    int index = player_currentIndex();

    if (index < 0) {
        list_updateStickyNote(sticky, "Nothing playing");
        return;
    }

    // Sized to hold the longest possible label plus the state prefix, so the
    // compiler can see the format can't overflow.
    char note[STR_MAX + 8];
    snprintf(note, sizeof(note), "%s %s", player_isPaused() ? "||" : ">",
             tracks[index].label);
    list_updateStickyNote(sticky, note);
}

int main(void)
{
    log_setName("musicPlayer");

    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);

    SDL_InitDefault();

    // settings_load() must precede lang_load(): the language file is named by
    // settings.language.
    settings_load();
    lang_load();

    int found = library_scan(MUSIC_DIR);
    bool has_tracks = found > 0;
    printf_debug("Found %d track(s) in %s\n", found, MUSIC_DIR);

    List list = list_createWithSticky(has_tracks ? found : 1, "Music Player");
    if (has_tracks)
        library_toList(&list);
    updateNowPlayingNote(&list, has_tracks);

    View view = VIEW_LIBRARY;
    KeyState keystate[320] = {(KeyState)0};
    bool redraw = true;
    bool durations_loaded = false;

    uint32_t acc_ticks = 0, last_ticks = SDL_GetTicks(),
             time_step = 1000 / FRAMES_PER_SECOND;

    while (!quit) {
        uint32_t ticks = SDL_GetTicks();
        acc_ticks += ticks - last_ticks;
        last_ticks = ticks;

        if (updateKeystate(keystate, &quit, true, NULL)) {
            if (keystate[SW_BTN_MENU] == PRESSED) {
                quit = true;
            }
            else if (keystate[SW_BTN_B] == PRESSED) {
                // B backs out of Now Playing, and exits from the library.
                if (view == VIEW_NOW_PLAYING) {
                    view = VIEW_LIBRARY;
                    redraw = true;
                }
                else {
                    quit = true;
                }
            }
            else if (keystate[SW_BTN_A] == PRESSED) {
                if (view == VIEW_LIBRARY && has_tracks) {
                    player_play(list.active_pos);
                    view = VIEW_NOW_PLAYING;
                }
                else if (view == VIEW_NOW_PLAYING) {
                    player_togglePause();
                }
                updateNowPlayingNote(&list, has_tracks);
                redraw = true;
            }
            else if (keystate[SW_BTN_X] == PRESSED) {
                player_togglePause();
                updateNowPlayingNote(&list, has_tracks);
                redraw = true;
            }
            else if (keystate[SW_BTN_Y] == PRESSED) {
                player_toggleShuffle();
                redraw = true;
            }
            else if (keystate[SW_BTN_SELECT] == PRESSED) {
                player_cycleRepeat();
                redraw = true;
            }
            else if (keystate[SW_BTN_START] == PRESSED && has_tracks) {
                // Jump to whatever is playing.
                if (player_currentIndex() >= 0) {
                    view = VIEW_NOW_PLAYING;
                    redraw = true;
                }
            }
            else if (keystate[SW_BTN_R1] == PRESSED) {
                player_next(true);
                updateNowPlayingNote(&list, has_tracks);
                redraw = true;
            }
            else if (keystate[SW_BTN_L1] == PRESSED) {
                player_previous();
                updateNowPlayingNote(&list, has_tracks);
                redraw = true;
            }
            else if (view == VIEW_LIBRARY && keystate[SW_BTN_UP] >= PRESSED) {
                redraw |= list_keyUp(&list, keystate[SW_BTN_UP] == REPEATING);
            }
            else if (view == VIEW_LIBRARY && keystate[SW_BTN_DOWN] >= PRESSED) {
                redraw |= list_keyDown(&list, keystate[SW_BTN_DOWN] == REPEATING);
            }
            else if (view == VIEW_NOW_PLAYING &&
                     keystate[SW_BTN_RIGHT] >= PRESSED) {
                player_seek(SEEK_STEP_SECONDS);
                redraw = true;
            }
            else if (view == VIEW_NOW_PLAYING &&
                     keystate[SW_BTN_LEFT] >= PRESSED) {
                player_seek(-SEEK_STEP_SECONDS);
                redraw = true;
            }
        }

        if (acc_ticks < time_step)
            continue;

        // Durations require reading every file to count frames, which is too
        // slow to do before the first frame is drawn. Do it once, after the UI
        // is already on screen.
        if (!durations_loaded && has_tracks) {
            library_loadDurations();
            durations_loaded = true;
            redraw = true;
        }

        // Auto-advance. SDL_mixer 1.2 has Mix_HookMusicFinished, but that
        // callback runs on the audio thread where calling back into Mix_* is
        // unsafe, so the end of a track is detected by polling instead.
        if (player_currentIndex() >= 0 && !player_isPaused() &&
            !player_isPlaying()) {
            player_next(false);
            updateNowPlayingNote(&list, has_tracks);
            redraw = true;
        }

        // The progress bar has to advance on its own, so Now Playing redraws
        // every frame while a track is running.
        if (view == VIEW_NOW_PLAYING && player_currentIndex() >= 0 &&
            !player_isPaused())
            redraw = true;

        if (redraw) {
            if (view == VIEW_NOW_PLAYING)
                render_nowPlaying(screen);
            else
                render_library(screen, &list, has_tracks);

            SDL_BlitSurface(screen, NULL, video, NULL);
            SDL_Flip(video);

            redraw = false;
        }

        acc_ticks -= time_step;
    }

    SDL_FillRect(video, NULL, 0);
    SDL_Flip(video);

    player_free();
    Mix_CloseAudio();

    list_free(&list);
    lang_free();
    resources_free();
    SDL_FreeSurface(screen);
    SDL_FreeSurface(video);
    SDL_Quit();

    return EXIT_SUCCESS;
}
