// OnionMusic: a library list and a Now Playing screen, MP3 only. Replaces the
// vendored GMU build by linking the system SDL_mixer. See README.md.

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

// Shows the current track in the list's sticky note.
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

    // Sized so the compiler can see the format cannot overflow.
    char note[STR_MAX + 8];
    snprintf(note, sizeof(note), "%s %s", player_isPaused() ? "||" : ">",
             tracks[index].label);
    list_updateStickyNote(sticky, note);
}

int main(int argc, char *argv[])
{
    log_setName("musicPlayer");

    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);

    SDL_InitDefault();
    printf_debug("milestone: SDL_InitDefault at %u ms\n", SDL_GetTicks());

    // Audio is opened here rather than by SDL_InitDefault, so a music-sized buffer can
    // be used without imposing it on the apps that only play UI blips. Carry on if it
    // fails: a usable UI beats a blank screen.
    if (!audio_init())
        printf_debug("Audio init failed: %s. Playback will not work.\n",
                     audio_error());
    printf_debug("milestone: audio_init at %u ms\n", SDL_GetTicks());

    // settings_load() first: it names the language file lang_load() reads.
    settings_load();
    lang_load();
    printf_debug("milestone: settings+lang at %u ms\n", SDL_GetTicks());

    // MainUI passes the track path as $1 for entries launched from a system.
    const char *start_path = (argc > 1) ? argv[1] : NULL;
    char scan_dir[STR_MAX * 2];
    library_resolveScanDir(start_path, scan_dir, sizeof(scan_dir));

    int found = library_scan(scan_dir);
    bool has_tracks = found > 0;
    printf_debug("milestone: scanned %d track(s) in %s at %u ms\n", found, scan_dir,
                 SDL_GetTicks());

    List list = list_createWithSticky(has_tracks ? found : 1, "OnionMusic");
    if (has_tracks)
        library_toList(&list);

    View view = VIEW_LIBRARY;

    // Launched with a track: play it and open Now Playing, so Recents resumes it.
    if (start_path != NULL) {
        int start_index = library_indexOfPath(start_path);
        if (start_index >= 0) {
            list_scrollTo(&list, start_index);
            player_play(start_index);
            view = VIEW_NOW_PLAYING;
        }
        else {
            printf_debug("Track not found in %s: %s\n", scan_dir, start_path);
        }
    }

    updateNowPlayingNote(&list, has_tracks);
    KeyState keystate[320] = {(KeyState)0};
    bool redraw = true;
    bool first_paint = true;

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

        // Set by Mix_HookMusicFinished, which only flips a flag, so no polling.
        if (player_currentIndex() >= 0 && audio_takeFinished()) {
            player_next(false);
            updateNowPlayingNote(&list, has_tracks);
            redraw = true;
        }

        // Now Playing redraws every frame so the progress bar advances.
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

            if (first_paint) {
                printf_debug("milestone: first paint at %u ms\n", SDL_GetTicks());
                first_paint = false;
            }
            redraw = false;
        }

        acc_ticks -= time_step;
    }

    SDL_FillRect(video, NULL, 0);
    SDL_Flip(video);

    player_free();
    audio_close();

    list_free(&list);
    lang_free();
    resources_free();
    SDL_FreeSurface(screen);
    SDL_FreeSurface(video);
    SDL_Quit();

    return EXIT_SUCCESS;
}
