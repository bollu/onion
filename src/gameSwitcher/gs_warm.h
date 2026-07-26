#ifndef GAME_SWITCHER_WARM_H__
#define GAME_SWITCHER_WARM_H__

#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "gs_model.h"

// Pull the entry under the cursor into the page cache while the user is still choosing.
//
// Launching is synchronous from the switcher's point of view: it writes cmd_to_run.sh and
// exits, and runtime.sh runs the command the instant it returns. So every millisecond the
// launched program then spends waiting on the SD card is time the user watches a black
// screen. None of that work goes away here -- it just happens during the pause before the
// button instead of the pause after it, which is the only pause anyone minds.
//
// posix_fadvise(WILLNEED) rather than a thread or a read loop: it queues the pages with the
// kernel and returns immediately, so a single-threaded SDL app never blocks on it. The
// kernel is also free to ignore or discard the hint under memory pressure, which is exactly
// the right failure mode for a guess about what the user will pick. Preferred over
// readahead(2) because it needs no _GNU_SOURCE, which this translation unit cannot set --
// gameSwitcher.c includes <fcntl.h> before it reaches this header.

// How long the cursor must sit still. Short enough that scrolling past an entry never
// triggers it, long enough that stopping on one always does.
#define WARM_DWELL_MS 200

// The device has 128 MB and page cache is reclaimable, so an over-eager warm costs nothing
// permanent. This is a head, not a whole file: an 876 MB archive cannot be cached, but its
// header, path list and first clusters are what every launch reads first.
#define WARM_MAX_BYTES (12 * 1024 * 1024)

static int warm_last_index = -1;
static uint32_t warm_dwell_start = 0;

// Queue the head of one file. Silent on every failure: a warm that does not happen is a
// launch at the old speed, never a broken launch.
static void warm_file(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return;
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return;
    }

    struct stat st;
    if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) {
        size_t len = (size_t)st.st_size;
        if (len > WARM_MAX_BYTES) {
            len = WARM_MAX_BYTES;
        }
        posix_fadvise(fd, 0, (off_t)len, POSIX_FADV_WILLNEED);
    }

    close(fd);
}

// Call once per frame with the current tick. Warms an entry after the cursor has rested on
// it, and only once -- moving away and back re-warms, which is free if the pages are still
// resident and correct if they were evicted.
static void warm_tick(int current_index, bool selection_changed, uint32_t ticks)
{
    if (selection_changed) {
        warm_dwell_start = ticks;
        warm_last_index = -1;
        return;
    }

    if (current_index == warm_last_index) {
        return;
    }

    if (ticks - warm_dwell_start < WARM_DWELL_MS) {
        return;
    }

    if (current_index < 0 || current_index >= game_list_len) {
        return;
    }

    warm_last_index = current_index;

    Game_s *game = &game_list[current_index];

    // The launcher first -- it is small, it is always executed, and it is what the kernel
    // needs before anything else can start. Then the payload, which is the big one.
    warm_file(game->recentItem.launch);
    warm_file(game->recentItem.rompath);
}

#endif // GAME_SWITCHER_WARM_H__
