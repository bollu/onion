#!/bin/sh
# Launched by MainUI with the book path as $1.
#
# Registering books as a "system" is what puts them in OnionOS Recents: MainUI writes
# Roms/recentlist.json itself whenever it launches an entry from a system registered by
# an Emu/*/config.json, so recents, favourites, box art and GameSwitcher all follow
# without bebook doing anything.
#
# The cd is required, not cosmetic: bebook.cfg and resources/fonts resolve relative to
# the working directory. $1 arrives absolute, so cd'ing away is safe.
cd /mnt/SDCARD/App/BeBook || exit 1

export LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH
exec ./bebook "$@" 2>>log.txt
