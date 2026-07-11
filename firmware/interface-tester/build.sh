#!/bin/sh
# Rebuild sim_ui from the current firmware UI sources.
# Run this after editing screens.c / widgets.c / ui_state.c / gfx.c.
# Needs: gcc, libsdl2-dev  (pacman -S sdl2 / apt install libsdl2-dev)
cd "$(dirname "$0")"
make sim_ui
