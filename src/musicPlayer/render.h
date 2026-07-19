#ifndef MUSICPLAYER_RENDER_H__
#define MUSICPLAYER_RENDER_H__

#include <stdio.h>

#include "components/list.h"
#include "system/battery.h"
#include "system/lang.h"
#include "theme/background.h"
#include "theme/theme.h"

#include "./library.h"
#include "./player.h"

/**
 * All layout constants are multiplied by g_scale, which theme/load.h sets from
 * the device resolution, so the same numbers work on 640x480 and higher-res
 * screens. This mirrors how the theme render headers lay things out.
 */

/** Formats seconds as m:ss, or h:mm:ss past an hour. */
static void _formatTime(double seconds, char *out, size_t out_size)
{
    if (seconds < 0.0)
        seconds = 0.0;

    int total = (int)seconds;
    int hours = total / 3600;
    int mins = (total % 3600) / 60;
    int secs = total % 60;

    if (hours > 0)
        snprintf(out, out_size, "%d:%02d:%02d", hours, mins, secs);
    else
        snprintf(out, out_size, "%d:%02d", mins, secs);
}

/** Blits `text` centred horizontally at vertical position `y`. */
static void _blitCentered(SDL_Surface *screen, SDL_Surface *text, int y)
{
    if (text == NULL)
        return;
    SDL_Rect pos = {(screen->w - text->w) / 2, y};
    SDL_BlitSurface(text, NULL, screen, &pos);
}

/**
 * Renders text centred, truncating with an ellipsis if it would exceed max_width.
 * SDL_ttf has no built-in ellipsis, so the string is trimmed a character at a
 * time until it fits - fine for the handful of labels drawn per frame.
 */
static void _renderCenteredText(SDL_Surface *screen, TTF_Font *font,
                                const char *str, SDL_Color color, int y,
                                int max_width)
{
    char buffer[STR_MAX];
    strncpy(buffer, str, STR_MAX - 1);
    buffer[STR_MAX - 1] = '\0';

    int w = 0, h = 0;
    TTF_SizeUTF8(font, buffer, &w, &h);

    if (w > max_width) {
        size_t len = strlen(buffer);
        while (len > 1 && w > max_width) {
            len--;
            buffer[len] = '\0';
            strcpy(buffer + (len > 3 ? len - 3 : 0), "...");
            TTF_SizeUTF8(font, buffer, &w, &h);
        }
    }

    SDL_Surface *text = TTF_RenderUTF8_Blended(font, buffer, color);
    _blitCentered(screen, text, y);
    if (text != NULL)
        SDL_FreeSurface(text);
}

/**
 * Draws the progress bar: a track, a filled portion, and a knob.
 *
 * Colours come from the active theme so this matches the rest of the UI. The
 * unfilled portion is the same colour at low alpha, which reads correctly on
 * both light and dark themes without needing a second theme field.
 */
static void _renderProgressBar(SDL_Surface *screen, double elapsed,
                               double duration, int y)
{
    SDL_Color color = theme()->list.color;

    int bar_width = (int)(440.0 * g_scale);
    int bar_height = (int)(6.0 * g_scale);
    int x = (screen->w - bar_width) / 2;

    // Unfilled track, drawn dim so the filled portion reads as the accent.
    SDL_Surface *track = SDL_CreateRGBSurface(SDL_SWSURFACE, bar_width,
                                              bar_height, 32, 0, 0, 0, 0);
    if (track != NULL) {
        SDL_FillRect(track, NULL,
                     SDL_MapRGB(track->format, color.r / 4, color.g / 4,
                                color.b / 4));
        SDL_Rect track_pos = {x, y};
        SDL_BlitSurface(track, NULL, screen, &track_pos);
        SDL_FreeSurface(track);
    }

    if (duration <= 0.0)
        return;

    double ratio = elapsed / duration;
    if (ratio < 0.0)
        ratio = 0.0;
    if (ratio > 1.0)
        ratio = 1.0;

    int filled = (int)(bar_width * ratio);

    if (filled > 0) {
        SDL_Surface *fill = SDL_CreateRGBSurface(SDL_SWSURFACE, filled,
                                                 bar_height, 32, 0, 0, 0, 0);
        if (fill != NULL) {
            SDL_FillRect(fill, NULL,
                         SDL_MapRGB(fill->format, color.r, color.g, color.b));
            SDL_Rect fill_pos = {x, y};
            SDL_BlitSurface(fill, NULL, screen, &fill_pos);
            SDL_FreeSurface(fill);
        }
    }

    // Knob, so the position is readable at a glance even when barely started.
    int knob = (int)(12.0 * g_scale);
    SDL_Surface *knob_surface =
        SDL_CreateRGBSurface(SDL_SWSURFACE, knob, knob, 32, 0, 0, 0, 0);
    if (knob_surface != NULL) {
        SDL_FillRect(knob_surface, NULL,
                     SDL_MapRGB(knob_surface->format, color.r, color.g, color.b));
        SDL_Rect knob_pos = {x + filled - knob / 2, y + bar_height / 2 - knob / 2};
        if (knob_pos.x < x)
            knob_pos.x = x;
        if (knob_pos.x > x + bar_width - knob)
            knob_pos.x = x + bar_width - knob;
        SDL_BlitSurface(knob_surface, NULL, screen, &knob_pos);
        SDL_FreeSurface(knob_surface);
    }
}

/** Builds the "Shuffle - Repeat all" style status line. */
static void _modeLabel(char *out, size_t out_size)
{
    const char *repeat_str = "Repeat off";
    switch (player_repeat()) {
    case REPEAT_ALL:
        repeat_str = "Repeat all";
        break;
    case REPEAT_ONE:
        repeat_str = "Repeat one";
        break;
    default:
        break;
    }

    snprintf(out, out_size, "%s  -  %s",
             player_shuffle() ? "Shuffle on" : "Shuffle off", repeat_str);
}

/** The Now Playing screen: title, progress bar, times, and mode line. */
void render_nowPlaying(SDL_Surface *screen)
{
    SDL_BlitSurface(theme_background(), NULL, screen, NULL);
    theme_renderHeader(screen, "Now Playing", false);

    int index = player_currentIndex();
    int max_width = (int)(560.0 * g_scale);

    if (index < 0) {
        _renderCenteredText(screen, resource_getFont(TITLE), "Nothing playing",
                            theme()->list.color, (int)(200.0 * g_scale),
                            max_width);
    }
    else {
        double elapsed = player_elapsed();
        double duration = player_duration();

        _renderCenteredText(screen, resource_getFont(TITLE), tracks[index].label,
                            theme()->list.color, (int)(140.0 * g_scale),
                            max_width);

        // Play state, so a paused track is obvious without reading the hint bar.
        _renderCenteredText(screen, resource_getFont(HINT),
                            player_isPaused() ? "|| Paused" : "> Playing",
                            theme()->hint.color, (int)(196.0 * g_scale),
                            max_width);

        _renderProgressBar(screen, elapsed, duration, (int)(260.0 * g_scale));

        char elapsed_str[32], duration_str[32], time_str[80];
        _formatTime(elapsed, elapsed_str, sizeof(elapsed_str));

        if (duration > 0.0) {
            _formatTime(duration, duration_str, sizeof(duration_str));
            snprintf(time_str, sizeof(time_str), "%s / %s", elapsed_str,
                     duration_str);
        }
        else {
            // Duration is unknown for a file mp3.h couldn't parse; show only
            // what is actually known rather than a misleading total.
            snprintf(time_str, sizeof(time_str), "%s", elapsed_str);
        }

        _renderCenteredText(screen, resource_getFont(HINT), time_str,
                            theme()->hint.color, (int)(286.0 * g_scale),
                            max_width);

        char mode_str[80];
        _modeLabel(mode_str, sizeof(mode_str));
        _renderCenteredText(screen, resource_getFont(HINT), mode_str,
                            theme()->hint.color, (int)(330.0 * g_scale),
                            max_width);
    }

    theme_renderFooter(screen);
    theme_renderStandardHint(screen, "Play/Pause",
                             lang_get(LANG_BACK, LANG_FALLBACK_BACK));
    theme_renderHeaderBattery(screen, battery_getPercentage());
}

/** The library list, with the current track shown in the sticky note. */
void render_library(SDL_Surface *screen, List *list, bool has_tracks)
{
    SDL_BlitSurface(theme_background(), NULL, screen, NULL);
    theme_renderHeader(screen, list->title, false);

    if (has_tracks) {
        theme_renderList(screen, list);
        theme_renderFooterStatus(screen, list->active_pos + 1, list->item_count);
    }
    else {
        _renderCenteredText(screen, resource_getFont(TITLE),
                            "No MP3s in Media/Music", theme()->list.color,
                            (int)(200.0 * g_scale), (int)(560.0 * g_scale));
        theme_renderFooter(screen);
    }

    theme_renderStandardHint(screen, lang_get(LANG_SELECT, LANG_FALLBACK_SELECT),
                             lang_get(LANG_BACK, LANG_FALLBACK_BACK));
    theme_renderHeaderBattery(screen, battery_getPercentage());
}

#endif // MUSICPLAYER_RENDER_H__
