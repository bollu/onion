#ifndef MUSICPLAYER_PLAYER_H__
#define MUSICPLAYER_PLAYER_H__

#include <SDL/SDL.h>
#include <SDL/SDL_mixer.h>
#include <stdbool.h>
#include <stdlib.h>

#include "utils/log.h"

#include "./library.h"
#include "./mp3.h"

// SDL_mixer 1.2 reports neither duration nor position, so both are tracked here.
// Mix_OpenAudio() is done by SDL_InitDefault(); only one Mix_Music loads at a time.

typedef enum RepeatMode { REPEAT_OFF,
                          REPEAT_ALL,
                          REPEAT_ONE } RepeatMode;

static Mix_Music *_music = NULL;
static int _current_index = -1;
static bool _paused = false;
static bool _shuffle = false;
static RepeatMode _repeat = REPEAT_ALL;

static double _duration = 0.0;    // seconds, 0 if unknown
static double _base_offset = 0.0; // elapsed seconds at _play_start_ticks
static uint32_t _play_start_ticks = 0;

int player_currentIndex(void) { return _current_index; }
bool player_isPaused(void) { return _paused; }
bool player_isPlaying(void) { return Mix_PlayingMusic() != 0; }
bool player_shuffle(void) { return _shuffle; }
RepeatMode player_repeat(void) { return _repeat; }
double player_duration(void) { return _duration; }

void player_toggleShuffle(void) { _shuffle = !_shuffle; }
void player_cycleRepeat(void) { _repeat = (RepeatMode)((_repeat + 1) % 3); }

/** Elapsed seconds in the current track. Frozen while paused. */
double player_elapsed(void)
{
    if (_current_index < 0)
        return 0.0;
    if (_paused)
        return _base_offset;

    double elapsed = _base_offset + (SDL_GetTicks() - _play_start_ticks) / 1000.0;

    // Don't run past the end before the main loop's poll notices.
    if (_duration > 0.0 && elapsed > _duration)
        return _duration;

    return elapsed;
}

void player_stop(void)
{
    if (_music != NULL) {
        Mix_HaltMusic();
        Mix_FreeMusic(_music);
        _music = NULL;
    }
    _current_index = -1;
    _paused = false;
    _duration = 0.0;
    _base_offset = 0.0;
}

bool player_play(int index)
{
    if (index < 0 || index >= track_count)
        return false;

    player_stop();

    _music = Mix_LoadMUS(tracks[index].path);
    if (_music == NULL) {
        printf_debug("Mix_LoadMUS failed for %s: %s\n", tracks[index].path,
                     Mix_GetError());
        return false;
    }

    if (Mix_PlayMusic(_music, 1) == -1) {
        printf_debug("Mix_PlayMusic failed: %s\n", Mix_GetError());
        Mix_FreeMusic(_music);
        _music = NULL;
        return false;
    }

    _current_index = index;
    _paused = false;
    _duration = tracks[index].duration;
    _base_offset = 0.0;
    _play_start_ticks = SDL_GetTicks();
    return true;
}

void player_togglePause(void)
{
    if (_music == NULL)
        return;

    if (_paused) {
        Mix_ResumeMusic();
        _play_start_ticks = SDL_GetTicks();
        _paused = false;
    }
    else {
        _base_offset = player_elapsed();
        Mix_PauseMusic();
        _paused = true;
    }
}

/** Index of the next track, honouring shuffle and repeat. -1 means stop. */
int player_nextIndex(bool user_initiated)
{
    if (track_count == 0 || _current_index < 0)
        return -1;

    if (_repeat == REPEAT_ONE && !user_initiated)
        return _current_index;

    if (_shuffle && track_count > 1) {
        int next;
        do {
            next = rand() % track_count;
        } while (next == _current_index);
        return next;
    }

    int next = _current_index + 1;

    if (next >= track_count) {
        // Pressing next always wraps; running off the end wraps only when repeating.
        if (user_initiated || _repeat == REPEAT_ALL)
            return 0;
        return -1;
    }

    return next;
}

void player_next(bool user_initiated)
{
    int next = player_nextIndex(user_initiated);
    if (next < 0)
        player_stop();
    else
        player_play(next);
}

void player_previous(void)
{
    if (track_count == 0 || _current_index < 0)
        return;

    // Restart the track unless we are near its start.
    if (player_elapsed() > 3.0) {
        player_play(_current_index);
        return;
    }

    int prev = _shuffle && track_count > 1
                   ? rand() % track_count
                   : (_current_index - 1 + track_count) % track_count;
    player_play(prev);
}

/** Seeks by `delta` seconds, clamped to the track. */
void player_seek(double delta)
{
    if (_music == NULL)
        return;

    double target = player_elapsed() + delta;

    if (target < 0.0)
        target = 0.0;
    if (_duration > 0.0 && target > _duration - 1.0)
        target = _duration - 1.0;

    if (Mix_SetMusicPosition(target) == 0) {
        _base_offset = target;
        _play_start_ticks = SDL_GetTicks();
    }
    else {
        // Leave the clock alone if the backend refused, so the display stays honest.
        printf_debug("Seek failed: %s\n", Mix_GetError());
    }
}

void player_free(void) { player_stop(); }

#endif // MUSICPLAYER_PLAYER_H__
