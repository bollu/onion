#!/bin/sh
cd $(dirname "$0")

export LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH

# "$@" rather than "$1": when launched from the apps list there is no argument, and
# passing an empty one would make bebook try to open the book at path "" and exit.
exec ./bebook "$@" 2>log.txt
