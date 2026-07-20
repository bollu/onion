#!/bin/sh
# Launched by MainUI with the track path as $1.
#
# Registering music as a "system" is what puts individual tracks in OnionOS Recents
# and the Game Switcher: MainUI writes Roms/recentlist.json itself whenever it
# launches an entry from a system registered by an Emu/*/config.json, and the Game
# Switcher only accepts entries of type 5 (game) -- an app launch records type 3 and
# is discarded. So the app alone can never appear there; a track can.
#
# The cd is required: config.json and the theme resolve relative to the working
# directory. $1 arrives absolute, so cd'ing away is safe.
cd /mnt/SDCARD/App/OnionMusic || exit 1

# Free the audio device: Miyoo's audioserver holds mi_ao open.
. /mnt/SDCARD/.tmp_update/script/stop_audioserver.sh

# Suppress hibernation while playing (read by keymon).
touch /tmp/stay_awake

./musicPlayer "$@"

rm -f /tmp/stay_awake
