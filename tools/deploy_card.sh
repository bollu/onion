#!/bin/sh
# Install the staged tree onto an SD card mounted on this Mac.
#
# Fast (a reader moves the whole ~284MB in seconds) and works on any Miyoo, including the
# base Mini, which has no Wi-Fi at all. The cost is having to take the card out of the
# device; deploy_wifi.sh is the alternative when that is the annoying part.
#
#   make deploy-card                      # auto-detect the card
#   make deploy-card SD=/Volumes/MIYOO    # name it explicitly
#   DRY_RUN=1 make deploy-card            # show what would move
set -e

. "$(dirname "$0")/deploy_common.sh"

# An Onion card has both of these at its root. Either alone is too weak: plenty of disks
# have a Roms folder, and .tmp_update alone would match a half-formatted card.
is_onion_card() {
    [ -d "$1/.tmp_update" ] && [ -d "$1/Roms" ]
}

# Reports through DETECTED_CARD rather than stdout: a command substitution would capture
# the guidance printed on failure instead of showing it, which defeats the point.
detect_card() {
    found=""
    count=0
    for vol in /Volumes/*; do
        [ -d "$vol" ] || continue
        if is_onion_card "$vol"; then
            found="$vol"
            count=$((count + 1))
        fi
    done

    if [ "$count" -eq 1 ]; then
        DETECTED_CARD="$found"
        return 0
    fi

    if [ "$count" -eq 0 ]; then
        say "No Onion SD card found under /Volumes."
        say ""
        say "Mounted volumes:"
        for vol in /Volumes/*; do
            [ -e "$vol" ] && say "  $vol"
        done
        say ""
        say "Insert the card, or name it explicitly:  make deploy-card SD=/Volumes/NAME"
        exit 1
    fi

    say "More than one Onion card is mounted; refusing to guess."
    for vol in /Volumes/*; do
        [ -d "$vol" ] && is_onion_card "$vol" && say "  $vol"
    done
    say ""
    say "Pick one:  make deploy-card SD=/Volumes/NAME"
    exit 1
}

check_stage

if [ -n "$SD" ]; then
    card="${SD%/}"
    [ -d "$card" ] || die "$card is not a mounted volume"
    # Still fingerprinted even when named explicitly: unpacking 284MB onto a backup drive
    # because of a typo is the failure worth engineering against. FORCE=1 to override.
    if ! is_onion_card "$card" && [ -z "$FORCE" ]; then
        die "$card does not look like an Onion card (no .tmp_update/ and Roms/).
       If you are sure, re-run with FORCE=1."
    fi
else
    detect_card
    card="$DETECTED_CARD"
fi

say "Card: $card"

[ -w "$card" ] || die "$card is not writable"

# A 284MB write onto a nearly full card fails halfway through, leaving a broken install.
avail_kb="$(df -k "$card" | awk 'NR==2 {print $4}')"
need_kb="$(du -sk "$STAGE" | awk '{print $1}')"
if [ -n "$avail_kb" ] && [ "$avail_kb" -lt "$need_kb" ]; then
    die "not enough space on $card: need ~$((need_kb / 1024))MB, have $((avail_kb / 1024))MB"
fi

say ""
# The trailing /./ makes rsync treat build/sideload as the transfer root, so the tree
# lands at the card root rather than inside a 'sideload' directory.
# shellcheck disable=SC2086
rsync $RSYNC_FLAGS $RSYNC_EXCLUDES "$STAGE/./" "$card/"

if [ -n "$DRY_RUN" ]; then
    say ""
    say "Dry run -- nothing was written."
    exit 0
fi

# FAT32 write caching means the card is not safe to pull the instant rsync returns;
# pacman_install.sh on the device ends the same way.
sync

say ""
say "Done. Eject before removing:  diskutil eject '$card'"
