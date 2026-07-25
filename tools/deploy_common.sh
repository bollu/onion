#!/bin/sh
# Shared by deploy_card.sh and deploy_wifi.sh: what to send and how to compare it.
# Sourced, not executed.

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STAGE="$REPO_ROOT/build/sideload"

# The card's own paths, for reporting and for the wifi destination.
CARD_DICT="App/BeBook/resources/italian.sqlite"
CARD_ZIM_DIR="Roms/WIKI"

# --checksum rather than the default size+mtime check. The destination is FAT32, which
# stores mtimes at 2-second resolution, so a written file's timestamp comes back rounded
# and differs from the source -- every file then looks changed and the 157MB dictionary
# re-sends on every run. Comparing content sidesteps that entirely: a file moves if and
# only if its bytes differ.
#
# -rltv rather than -a: -a would add -pgoD, and perms, owner, group and device nodes are
# all meaningless on FAT32 -- each one produces a per-file error.
#
# --partial so a connection dropped partway through the 112MB archive resumes.
#
# Flags are kept rsync 2.6.9-compatible: that is what macOS ships, so --info=progress2
# and other 3.x conveniences are unavailable.
RSYNC_FLAGS="-rltv --checksum --partial --progress --no-perms --no-owner --no-group"

# macOS scatters these across any volume it touches. dot-clean.sh on the device only
# sweeps Roms/ and Media/, never App/, so they have to be kept off the card here.
# shellcheck disable=SC2034  # used by the scripts that source this
RSYNC_EXCLUDES="--exclude=.DS_Store --exclude=._* --exclude=__MACOSX
                --exclude=.Spotlight-V100 --exclude=.fseventsd --exclude=.Trashes
                --exclude=.TemporaryItems --exclude=.apDisk"

if [ -n "$DRY_RUN" ]; then
    RSYNC_FLAGS="$RSYNC_FLAGS --dry-run"
fi

say() { printf '%s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

human_size() {
    # BSD stat; the tree is only ever read on the host.
    du -sh "$1" 2>/dev/null | awk '{print $1}'
}

# Refuses to deploy a tree that is missing, and warns about content that is merely
# absent -- shipping a reader with no dictionary or no articles should be loud, not
# something discovered on the device.
check_stage() {
    [ -d "$STAGE" ] || die "nothing staged at $STAGE -- run 'make sideload-bewiki' first"
    [ -d "$STAGE/App/BeBook" ] || die "$STAGE has no App/BeBook -- staging did not run"

    [ -x "$STAGE/App/BeBook/bewiki" ] || [ -f "$STAGE/App/BeBook/bewiki" ] \
        || say "warning: no bewiki binary staged. Cross-compile with: make with-toolchain CMD=bewiki"

    if [ ! -f "$STAGE/$CARD_DICT" ]; then
        say "warning: no italian.sqlite staged -- the dictionary popup will come up empty."
    fi

    if ! ls "$STAGE/$CARD_ZIM_DIR"/*.zim >/dev/null 2>&1; then
        say "warning: no .zim staged -- bewiki will have no articles to read."
        say "         Fetch one with: src/bebook/tools/fetch_zim.sh"
    fi

    say "Staged payload: $(human_size "$STAGE")"
}
