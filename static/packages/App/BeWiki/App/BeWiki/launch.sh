#!/bin/sh
# BeWiki shares BeBook's fonts and Italian dictionary rather than shipping a second
# 80MB copy, and resources resolve relative to the working directory, so run from there.
cd /mnt/SDCARD/App/BeBook || exit 1

export LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH

# "$@" rather than "$1": launched from the apps list there is no argument, while the
# Game Switcher resumes by passing back the stub naming the article we were reading.
exec ./bewiki "$@" 2>wikilog.txt
