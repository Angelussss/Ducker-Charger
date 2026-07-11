#!/bin/sh
# launch the emulator, building it first if missing
cd "$(dirname "$0")" || exit 1
[ -x emulator ] || make || exit 1
exec ./emulator "$@"
