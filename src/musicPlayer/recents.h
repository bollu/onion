#ifndef MUSICPLAYER_RECENTS_H__
#define MUSICPLAYER_RECENTS_H__

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <SDL/SDL.h>

#include "components/JsonGameEntry.h"
#include "system/settings.h"
#include "utils/IMG_Save.h"
#include "utils/file.h"
#include "utils/hash.h"
#include "utils/log.h"
#include "utils/str.h"

// Puts the app in Recents without being a registered system. resumeGame() in
// system/state.h runs `launch rompath` for any type 5 entry whose paths both exist,
// so naming our own launcher resumes the track we were on.

#define RECENTS_LAUNCH "/mnt/SDCARD/App/OnionMusic/launch.sh"
#define ROM_SCREENS_DIR "/mnt/SDCARD/Saves/CurrentProfile/romScreens"

static const char *_recents_path(void)
{
    return exists(RECENTLIST_HIDDEN_PATH) ? RECENTLIST_HIDDEN_PATH : RECENTLIST_PATH;
}

// The Game Switcher prefers romScreens/<hash>.png over the box art, so this is what
// makes the tile show what was playing. keymon does the same for RetroArch.
static void _recents_writePreview(const char *rompath, SDL_Surface *screen)
{
    if (screen == NULL)
        return;

    mkdirs(ROM_SCREENS_DIR);

    // IMG_Save drops any pixel whose alpha is zero, and `screen` is created without an
    // alpha mask, so save an opaque copy or the whole frame comes out transparent.
    SDL_Surface *opaque = SDL_ConvertSurface(screen, screen->format, SDL_SWSURFACE);
    if (opaque == NULL)
        return;

    Uint32 *pixels = (Uint32 *)opaque->pixels;
    for (int i = 0; i < (opaque->pitch / 4) * opaque->h; i++)
        pixels[i] |= 0xFF000000;

    char path[STR_MAX * 2];
    snprintf(path, sizeof(path) - 1, "%s/%" PRIu32 ".png", ROM_SCREENS_DIR,
             FNV1A_Pippip_Yurii(rompath, strlen(rompath)));
    IMG_Save(opaque, path);
    SDL_FreeSurface(opaque);
}

// Rewrites the list with this track first and any earlier OnionMusic entry dropped,
// so Recents shows one tile that follows what is playing rather than accumulating.
static void _recents_writeEntry(const char *rompath, const char *label)
{
    JsonGameEntry entry = {.type = 5};
    strncpy(entry.label, label, STR_MAX - 1);
    strncpy(entry.launch, RECENTS_LAUNCH, STR_MAX - 1);
    strncpy(entry.rompath, rompath, STR_MAX - 1);

    char line[STR_MAX * 6];
    JsonGameEntry_toJson(line, &entry);

    const char *path = _recents_path();
    char(*kept)[STR_MAX * 3] = malloc(sizeof(char[64][STR_MAX * 3]));
    if (kept == NULL)
        return;

    int keep_count = 0;
    FILE *fp = fopen(path, "r");
    if (fp != NULL) {
        char existing[STR_MAX * 3];
        while (keep_count < 64 && fgets(existing, sizeof(existing), fp) != NULL) {
            if (strstr(existing, RECENTS_LAUNCH) != NULL)
                continue;
            if (strlen(existing) > 1)
                strcpy(kept[keep_count++], existing);
        }
        fclose(fp);
    }

    if ((fp = file_open_ensure_path(path, "w")) == NULL) {
        free(kept);
        return;
    }
    fprintf(fp, "%s\n", line);
    for (int i = 0; i < keep_count; i++)
        fputs(kept[i], fp);
    fclose(fp);
    free(kept);

    printf_debug("recents: %s -> %s\n", label, path);
}

// No-op when nothing played, so merely opening the app leaves no trace.
void recents_save(const char *rompath, const char *label, SDL_Surface *screen)
{
    if (rompath == NULL || strlen(rompath) == 0)
        return;

    _recents_writePreview(rompath, screen);
    _recents_writeEntry(rompath, label);
}

#endif // MUSICPLAYER_RECENTS_H__
