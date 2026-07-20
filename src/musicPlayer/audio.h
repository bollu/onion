#ifndef MUSICPLAYER_AUDIO_H__
#define MUSICPLAYER_AUDIO_H__

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL/SDL.h>

#include "utils/log.h"

// The audio backend, behind a seam so the app can be built without SDL_mixer
// (NO_AUDIO=1) to test the UI on its own.

#ifdef MUSICPLAYER_AUDIO

#include <SDL/SDL_mixer.h>

// Bigger than the 4096 SDL_InitDefault() uses for UI blips: a music player has to
// survive SD-card reads mid-track. MiyooPod hit the same wall and raised it too.
#define AUDIO_CHUNK_FRAMES 8192

// Whole tracks are decoded from RAM (see audio_load). Anything larger streams from
// the card instead -- the device only has 128MB, and MP3s are 3-10MB.
#define AUDIO_MAX_RAM_BYTES (32 * 1024 * 1024)

static Mix_Music *_audio_music = NULL;
static void *_audio_data = NULL;
static volatile int _audio_finished = 0;

static void _audio_onFinished(void) { _audio_finished = 1; }

static inline const char *audio_error(void) { return Mix_GetError(); }

// SDL_InitDefault() has already opened the device with the 4096-frame buffer meant
// for UI blips, so reopen it with one sized for continuous playback.
static inline bool audio_init(void)
{
    Mix_CloseAudio();
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, AUDIO_CHUNK_FRAMES) < 0) {
        printf_debug("Mix_OpenAudio failed: %s\n", Mix_GetError());
        return false;
    }
    Mix_HookMusicFinished(_audio_onFinished);
    return true;
}

static inline void audio_stop(void)
{
    if (_audio_music != NULL) {
        Mix_HaltMusic();
        Mix_FreeMusic(_audio_music);
        _audio_music = NULL;
    }
    free(_audio_data);
    _audio_data = NULL;
    _audio_finished = 0;
}

// Reads the track into memory so playback does no SD I/O, which is what starves the
// decoder mid-song. Falls back to streaming if it is too big or cannot be read.
static inline bool audio_load(const char *path)
{
    audio_stop();

    FILE *fp = fopen(path, "rb");
    if (fp != NULL) {
        fseek(fp, 0, SEEK_END);
        long size = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        if (size > 0 && size <= AUDIO_MAX_RAM_BYTES) {
            _audio_data = malloc((size_t)size);
            if (_audio_data != NULL &&
                fread(_audio_data, 1, (size_t)size, fp) == (size_t)size) {
                fclose(fp);
                // Owns _audio_data, so it must outlive the Mix_Music.
                SDL_RWops *rw = SDL_RWFromMem(_audio_data, (int)size);
                if (rw != NULL && (_audio_music = Mix_LoadMUS_RW(rw)) != NULL) {
                    printf_debug("loaded %ld bytes from RAM: %s\n", size, path);
                    return true;
                }
            }
            free(_audio_data);
            _audio_data = NULL;
        }
        else {
            printf_debug("too big for RAM (%ld bytes), streaming: %s\n", size, path);
        }
        fclose(fp);
    }

    _audio_music = Mix_LoadMUS(path);
    return _audio_music != NULL;
}

static inline bool audio_play(void)
{
    if (_audio_music == NULL)
        return false;
    _audio_finished = 0;
    if (Mix_PlayMusic(_audio_music, 1) != -1)
        return true;
    audio_stop();
    return false;
}

static inline bool audio_isPlaying(void)
{
    return Mix_PlayingMusic() && !Mix_PausedMusic();
}

// True once per finished track, so the caller need not poll Mix_PlayingMusic().
static inline bool audio_takeFinished(void)
{
    if (!_audio_finished)
        return false;
    _audio_finished = 0;
    return true;
}

static inline void audio_pause(void) { Mix_PauseMusic(); }
static inline void audio_resume(void) { Mix_ResumeMusic(); }
static inline bool audio_seek(double sec) { return Mix_SetMusicPosition(sec) == 0; }
static inline void audio_close(void) { Mix_CloseAudio(); }

#else // NO_AUDIO

// Reports playback as working so the UI, the elapsed clock and auto-advance all still
// get exercised; the caller's clock stands in for the backend.
static bool _audio_loaded = false;
static bool _audio_playing = false;
static bool _audio_finished = false;

static inline const char *audio_error(void) { return "built without audio"; }
static inline bool audio_init(void) { return true; }
static inline bool audio_isPlaying(void) { return _audio_playing; }
static inline void audio_stop(void)
{
    _audio_loaded = _audio_playing = _audio_finished = false;
}
static inline bool audio_load(const char *path)
{
    (void)path;
    audio_stop();
    return (_audio_loaded = true);
}
static inline bool audio_play(void) { return (_audio_playing = _audio_loaded); }
static inline void audio_pause(void) {}
static inline void audio_resume(void) {}
static inline bool audio_seek(double sec) { (void)sec; return true; }
static inline void audio_close(void) {}

// Driven by player_elapsed() passing the track duration.
static inline void audio_markFinished(void)
{
    _audio_playing = false;
    _audio_finished = true;
}
static inline bool audio_takeFinished(void)
{
    if (!_audio_finished)
        return false;
    _audio_finished = false;
    return true;
}

#endif // MUSICPLAYER_AUDIO

#endif // MUSICPLAYER_AUDIO_H__
