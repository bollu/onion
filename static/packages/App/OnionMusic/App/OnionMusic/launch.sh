#!/bin/sh
echo $0 $*
cd $(dirname "$0")

# Free the audio device: Miyoo's audioserver holds mi_ao open.
. /mnt/SDCARD/.tmp_update/script/stop_audioserver.sh

# Suppress hibernation while playing (read by keymon).
touch /tmp/stay_awake

./musicPlayer

rm -f /tmp/stay_awake
