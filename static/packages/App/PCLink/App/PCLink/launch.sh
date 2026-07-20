#!/bin/sh
echo $0 $*
cd $(dirname "$0")

# keymon SIGSTOPs everything on screen-off unless this exists.
touch /tmp/stay_awake

./pcLink

rm -f /tmp/stay_awake
