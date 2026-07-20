#!/bin/sh
echo $0 $*
cd $(dirname "$0")

# Do NOT stop audioserver: this links the system SDL_mixer, whose audio routes
# through it. Killing it makes Mix_OpenAudio block (a freeze on a black screen).

# Pin performance while decoding, or the audio buffer can underrun.
governor_path=/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
old_governor=$(cat $governor_path 2>/dev/null)
echo performance > $governor_path 2>/dev/null

# keymon reads this and skips hibernate.
touch /tmp/stay_awake

./musicPlayer "$@" 2>log.txt

rm -f /tmp/stay_awake

[ -n "$old_governor" ] && echo "$old_governor" > $governor_path 2>/dev/null
