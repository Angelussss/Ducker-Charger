#!/bin/sh
# Launch the UI simulator (builds first if sim_ui is missing).
# Edit sim_config.ini to change the fake sensor data, then re-run.
cd "$(dirname "$0")"
[ -f sim_ui ] || make sim_ui || exit 1
./sim_ui "$@"
