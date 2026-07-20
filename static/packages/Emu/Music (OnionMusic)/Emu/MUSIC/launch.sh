#!/bin/sh
# Launched by MainUI with the track path as $1.
#
# Registering music as a system is what puts individual tracks in Recents and the
# Game Switcher: an app launch records type 3, which the switcher discards.
# The cd is required; $1 arrives absolute, so it is safe.
cd /mnt/SDCARD/App/OnionMusic || exit 1

# Do NOT stop audioserver: killing it makes Mix_OpenAudio block. See App/OnionMusic.

# Pin performance while decoding, or the audio buffer can underrun.
governor_path=/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
old_governor=$(cat $governor_path 2>/dev/null)
echo performance > $governor_path 2>/dev/null

# keymon reads this and skips hibernate.
touch /tmp/stay_awake

./musicPlayer "$@" 2>log.txt

rm -f /tmp/stay_awake

[ -n "$old_governor" ] && echo "$old_governor" > $governor_path 2>/dev/null
