#!/bin/sh
echo $0 $*
cd $(dirname "$0")

# Do NOT stop audioserver here. GMU did, and so do Drastic/PICO-8/ScummVM, but they
# all bundle their own SDL that drives mi_ao directly. This app links the *system*
# SDL_mixer, whose audio goes through audioserver -- kill it and Mix_OpenAudio has
# nothing to talk to and blocks, which showed up as a freeze on a black screen.
# Tweaks is the proof: it is built with HAS_AUDIO, plays sounds, and leaves
# audioserver alone.

# Decoding MP3 on this SoC is tight enough that the ondemand governor can let
# the audio buffer underrun. GMU and MiyooPod both pin performance for the same
# reason; unlike GMU, this puts it back afterwards.
governor_path=/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
old_governor=$(cat $governor_path 2>/dev/null)
echo performance > $governor_path 2>/dev/null

# Suppress hibernation while playing (read by keymon).
touch /tmp/stay_awake

./musicPlayer "$@" 2>log.txt

rm -f /tmp/stay_awake

[ -n "$old_governor" ] && echo "$old_governor" > $governor_path 2>/dev/null
