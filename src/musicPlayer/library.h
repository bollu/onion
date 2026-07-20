#ifndef MUSICPLAYER_LIBRARY_H__
#define MUSICPLAYER_LIBRARY_H__

#include <dirent.h>
#include <stdbool.h>
#include <string.h>

#include "components/list.h"
#include "utils/file.h"
#include "utils/log.h"
#include "utils/str.h"

#include "./mp3.h"

#define MUSIC_DIR "/mnt/SDCARD/Media/Music"
#define MAX_TRACKS 500

typedef struct Track {
    char path[STR_MAX * 2];
    char label[STR_MAX];
    double duration; // seconds; 0 until computed, see library_loadDurations()
} Track;

static Track tracks[MAX_TRACKS];
static int track_count = 0;

// MP3 only: the device's SDL_mixer has libmad and nothing else.
static bool _isSupported(const char *name)
{
    const char *ext = file_getExtension(name);
    return ext != NULL && strcasecmp(ext, "mp3") == 0;
}

static void _stripExtension(const char *name, char *out)
{
    strncpy(out, name, STR_MAX - 1);
    out[STR_MAX - 1] = '\0';
    char *dot = strrchr(out, '.');
    if (dot != NULL)
        *dot = '\0';
}

int library_scan(const char *dir_path)
{
    DIR *dp;
    struct dirent *ep;

    track_count = 0;

    if ((dp = opendir(dir_path)) == NULL) {
        printf_debug("Music directory not found: %s\n", dir_path);
        return 0;
    }

    while ((ep = readdir(dp)) != NULL && track_count < MAX_TRACKS) {
        if (ep->d_type == DT_DIR || !_isSupported(ep->d_name))
            continue;

        Track *track = &tracks[track_count];
        snprintf(track->path, sizeof(track->path) - 1, "%s/%s", dir_path,
                 ep->d_name);
        _stripExtension(ep->d_name, track->label);
        track_count++;
    }

    closedir(dp);
    return track_count;
}

// A track's own folder when launched with one, so next/prev stay in that album.
void library_resolveScanDir(const char *start_path, char *scan_dir, size_t size)
{
    if (start_path == NULL || strlen(start_path) == 0) {
        snprintf(scan_dir, size, "%s", MUSIC_DIR);
        return;
    }

    snprintf(scan_dir, size, "%s", start_path);

    char *last_slash = strrchr(scan_dir, '/');
    if (last_slash != NULL && last_slash != scan_dir)
        *last_slash = '\0';
    else
        snprintf(scan_dir, size, "%s", MUSIC_DIR);
}

// Index of `path` in the scanned library, or -1 if it isn't there.
int library_indexOfPath(const char *path)
{
    for (int i = 0; i < track_count; i++) {
        if (strcmp(tracks[i].path, path) == 0)
            return i;
    }
    return -1;
}

// Separate from the scan: reads every file end to end, so run it after first draw.
void library_loadDurations(void)
{
    for (int i = 0; i < track_count; i++) {
        if (tracks[i].duration == 0.0)
            tracks[i].duration = mp3_getDuration(tracks[i].path);
    }
}

// Fills `list` with one ACTION item per track.
void library_toList(List *list)
{
    for (int i = 0; i < track_count; i++) {
        list_addItem(list, (ListItem){
                               .action_id = i,
                               .item_type = ACTION,
                               .payload_ptr = &tracks[i],
                           });
        strncpy(list->items[i].label, tracks[i].label, STR_MAX - 1);
    }
}

#endif // MUSICPLAYER_LIBRARY_H__
