#!/bin/sh
# BeDict shares BeBook's fonts and Italian dictionary rather than shipping a second
# ~29MB copy, and resources resolve relative to the working directory, so run from there.
cd /mnt/SDCARD/App/BeBook || exit 1

export LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH

exec ./bedict 2>dictlog.txt
