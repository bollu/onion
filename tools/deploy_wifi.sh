#!/bin/sh
# Install the staged tree onto the device over its own Wi-Fi hotspot.
#
# Nothing needs installing on the device: Onion already ships dropbear and rsync in
# .tmp_update/bin. Slower than a card reader and Miyoo Mini Plus only -- the base Mini has
# no Wi-Fi -- but the card never leaves the device.
#
# On the device, first:
#   Tweaks > Network > WiFi > Hotspot          (SSID MiyooMini+APOnionOS, pw onionos+)
#   Tweaks > Network > SSH
#   Tweaks > Network > Keep services alive while playing
# then join that network from this Mac.
#
#   make deploy-wifi
#   make deploy-wifi DEVICE_HOST=192.168.1.50   # joined your router instead
#   DRY_RUN=1 make deploy-wifi
set -e

. "$(dirname "$0")/deploy_common.sh"

# dnsmasq hands out .101-.200 and the device takes range_start - 1. Hardcoded the same way
# in static/build/.tmp_update/script/network/hotspot_join.sh.
DEVICE_HOST="${DEVICE_HOST:-192.168.100.100}"
DEVICE_USER="${DEVICE_USER:-onion}"

DEVICE_RSYNC=/mnt/SDCARD/.tmp_update/bin/rsync

# MACs=hmac-sha1 because the device's dropbear 2022.83 is too old to negotiate with a
# current OpenSSH client; the repo's own docs use the same flag.
SSH_OPTS="-o MACs=hmac-sha1 -o StrictHostKeyChecking=accept-new -o ConnectTimeout=5"

check_stage

say ""
say "Device: $DEVICE_USER@$DEVICE_HOST"

# The two likely failures are "not joined to the hotspot" and "SSH is off", and they need
# different fixes, so they are distinguished rather than collapsed into a timeout.
if ! ping -c 1 -W 2000 "$DEVICE_HOST" >/dev/null 2>&1; then
    say ""
    say "$DEVICE_HOST is not reachable."
    say ""
    say "  * Is the hotspot on?   Tweaks > Network > WiFi > Hotspot"
    say "  * Is this Mac joined?  SSID 'MiyooMini+APOnionOS', password 'onionos+'"
    say ""
    say "If the device is on your router instead, pass its address:"
    say "  make deploy-wifi DEVICE_HOST=192.168.1.50"
    exit 1
fi

if ! nc -z -G 3 "$DEVICE_HOST" 22 >/dev/null 2>&1; then
    say ""
    say "$DEVICE_HOST answers, but nothing is listening on port 22."
    say ""
    say "  * Turn SSH on:  Tweaks > Network > SSH"
    say ""
    say "Note that launching a game or app kills it again unless"
    say "'Keep services alive while playing' is also enabled."
    exit 1
fi

# Confirms it is actually the handheld and its card is mounted, rather than some other
# host that happens to hold this address.
# shellcheck disable=SC2086  # SSH_OPTS is several flags and must word-split
if ! ssh $SSH_OPTS "$DEVICE_USER@$DEVICE_HOST" "test -d /mnt/SDCARD/.tmp_update" 2>/dev/null; then
    die "connected to $DEVICE_HOST, but /mnt/SDCARD/.tmp_update is not there.
       Either this is not a Miyoo running Onion, or the login failed
       (default user 'onion', password 'onion')."
fi

# runtime.sh kills dropbear the moment a game or app launches, which would drop a transfer
# partway through. Worth warning about before spending minutes on it.
# shellcheck disable=SC2086  # SSH_OPTS is several flags and must word-split
if ! ssh $SSH_OPTS "$DEVICE_USER@$DEVICE_HOST" \
        "test -f /mnt/SDCARD/.tmp_update/config/.keepServicesAlive" 2>/dev/null; then
    say ""
    say "warning: 'Keep services alive while playing' is off, so SSH dies as soon as"
    say "         anything is launched on the device. Enable it in Tweaks > Network"
    say "         if the transfer keeps stopping."
fi

say ""
# -z here but not for the card: 2.4GHz HT20 is a few MB/s and both bulk payloads compress
# well, whereas over a reader it would only burn CPU.
#
# --rsync-path is mandatory: the device's rsync is only on PATH via init_env.sh, which a
# non-interactive ssh session never sources. It links just libc, so no LD_LIBRARY_PATH.
# shellcheck disable=SC2086
rsync $RSYNC_FLAGS $RSYNC_EXCLUDES -z \
      --rsync-path="$DEVICE_RSYNC" \
      -e "ssh $SSH_OPTS" \
      "$STAGE/./" "$DEVICE_USER@$DEVICE_HOST:/mnt/SDCARD/"

if [ -n "$DRY_RUN" ]; then
    say ""
    say "Dry run -- nothing was written."
    exit 0
fi

# shellcheck disable=SC2086  # SSH_OPTS is several flags and must word-split
ssh $SSH_OPTS "$DEVICE_USER@$DEVICE_HOST" "sync" 2>/dev/null || true

say ""
say "Done. BeWiki should be in the apps list on the next reload."
