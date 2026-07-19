#!/bin/sh
echo $0 $*
cd $(dirname "$0")

# Suppress hibernation: keymon SIGSTOPs everything on screen-off unless this
# exists, which would freeze the hotspot mid-transfer.
touch /tmp/stay_awake

./pcLink

rm -f /tmp/stay_awake
