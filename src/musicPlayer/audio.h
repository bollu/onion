#ifndef MUSICPLAYER_AUDIO_H__
#define MUSICPLAYER_AUDIO_H__

#include <stdbool.h>

#include <SDL/SDL.h>

// The audio backend, behind a seam so the app can be built without SDL_mixer
// (NO_AUDIO=1) to test the UI on its own. SDL_InitDefault() already skips
// SDL_INIT_AUDIO and Mix_OpenAudio when HAS_AUDIO is undefined.

#ifdef HAS_AUDIO

#include <SDL/SDL_mixer.h>

static Mix_Music *_audio_music = NULL;

static inline const char *audio_error(void) { return Mix_GetError(); }
static inline bool audio_isPlaying(void) { return Mix_PlayingMusic() != 0; }

static inline void audio_stop(void)
{
    if (_audio_music == NULL)
        return;
    Mix_HaltMusic();
    Mix_FreeMusic(_audio_music);
    _audio_music = NULL;
}

static inline bool audio_load(const char *path)
{
    _audio_music = Mix_LoadMUS(path);
    return _audio_music != NULL;
}

static inline bool audio_play(void)
{
    if (_audio_music == NULL)
        return false;
    if (Mix_PlayMusic(_audio_music, 1) != -1)
        return true;
    Mix_FreeMusic(_audio_music);
    _audio_music = NULL;
    return false;
}

static inline void audio_pause(void) { Mix_PauseMusic(); }
static inline void audio_resume(void) { Mix_ResumeMusic(); }
static inline bool audio_seek(double sec) { return Mix_SetMusicPosition(sec) == 0; }
static inline void audio_close(void) { Mix_CloseAudio(); }

#else // NO_AUDIO

// Reports playback as working so the UI, the elapsed clock and auto-advance all
// still get exercised; audio_isPlaying() is driven by the caller's clock instead.
static bool _audio_loaded = false;
static bool _audio_playing = false;

static inline const char *audio_error(void) { return "built without audio"; }
static inline bool audio_isPlaying(void) { return _audio_playing; }
static inline void audio_stop(void) { _audio_loaded = _audio_playing = false; }
static inline bool audio_load(const char *path)
{
    (void)path;
    _audio_loaded = true;
    return true;
}
static inline bool audio_play(void) { return (_audio_playing = _audio_loaded); }
static inline void audio_pause(void) {}
static inline void audio_resume(void) {}
static inline bool audio_seek(double sec) { (void)sec; return true; }
static inline void audio_close(void) {}

// Lets the caller retire a track once its elapsed time passes the duration.
static inline void audio_markFinished(void) { _audio_playing = false; }

#endif // HAS_AUDIO

#endif // MUSICPLAYER_AUDIO_H__
