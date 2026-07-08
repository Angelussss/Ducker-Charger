# interface-tester

Native desktop simulator for the Ducker-Charger UI.
Compiles the real firmware UI sources against a fake framebuffer instead of
the SPI display, so you can see and interact with every screen without hardware.

## Quick start

```sh
# needs gcc + SDL2
# Arch:   sudo pacman -S sdl2
# Ubuntu: sudo apt install libsdl2-dev

./build.sh   # (re)compile sim_ui from the current firmware sources
./run.sh     # launch sim_ui, compiles it first if missing
```

The window is 240×320 — the same resolution as the real display.

## Controls

| Key | Action |
|-----|--------|
| Arrow up / mouse wheel up | Rotate encoder right (+1 click) |
| Arrow down / mouse wheel down | Rotate encoder left (−1 click) |
| Space (tap) | Encoder press (short click) |
| Space held ≥ 1 s | Long press → opens SETTINGS overlay |
| Double space within 400 ms | Jump straight to SETTINGS |
| ESC | Quit |

## Fake sensor data — sim_config.ini

Edit `sim_config.ini` and relaunch to change what the UI displays.
No rebuild needed — the file is read every time at startup.

Key fields:

| Key | What it does |
|-----|-------------|
| `soc` | State of charge 0–100 % |
| `pack_mv` | Pack voltage in mV (typical 11000–16800) |
| `batt_ma` | Battery current: positive = charging, negative = draining |
| `batt_ripple` | Sine amplitude on `batt_ma` (0 = flat line on graph) |
| `temp_c` | Battery temperature in °C |
| `vbus` | 1 = USB-C charger plugged in |
| `phase` | Charger phase: 0 idle · 1 pre-charge · 2 fast · 3 taper |
| `a1/a2/c1/c2/lab_active` | Port enabled/disabled |
| `*_mv` / `*_ma` / `*_ripple` | Per-port voltage, current, graph ripple |
| `tte_min` / `ttf_min` | Time-to-empty / time-to-full shown under SoC |
| `cycles`, `health`, `cap_*` | OVERALL DATA screen — lifetime gauge stats |

Set `batt_ma` negative and `vbus = 0` to simulate discharging.
Set `temp_c = 65` to trigger the over-temperature warning.
Set `soc = 4` to trigger the critical low-battery warning + sleep.

## What each file does

| File | Role |
|------|------|
| `build.sh` | Recompile `sim_ui` from the current firmware sources |
| `run.sh` | Launch `sim_ui`; compiles it first if the binary is missing |
| `sim_config.ini` | Fake sensor data, read at startup |
| `Makefile` | Build rules; UI sources pulled from `../cubeMX/Core/Src/` |
| `src/stm32f4xx_hal.h` | Minimal HAL stub (HAL_GetTick, GPIO, SPI types) |
| `src/main.h` | GPIO pin name stubs matching the real firmware `main.h` |
| `src/sim_config.h/c` | INI file parser and `SimCfg` struct |
| `src/sim_stubs.c` | HAL + encoder + telemetry stubs; feeds fake data to the UI |
| `src/sim_display.c` | Implements ILI9341 API against an in-memory RGB565 framebuffer |
| `src/sdl_main.c` | SDL2 window, event loop, keyboard/mouse → encoder events |

## How it links to the firmware

The Makefile compiles these files **directly from the firmware tree** —
no copies:

```
../cubeMX/Core/Src/display/gfx.c
../cubeMX/Core/Src/ui/widgets.c
../cubeMX/Core/Src/ui/screens.c
../cubeMX/Core/Src/ui/ui_state.c
```

`-Isrc` is listed first so the stub headers (`stm32f4xx_hal.h`, `main.h`)
shadow the real HAL. Everything else comes from `../cubeMX/Core/Inc`.

After editing any UI file, run `./build.sh` then `./run.sh` to see the result.
`sim_ui` is not committed to git — it is always generated locally.
