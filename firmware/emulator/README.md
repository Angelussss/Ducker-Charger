# emulator — full-PCB emulator for the Ducker-Charger firmware

Runs the **unmodified firmware** (`main.c`, FSM, charge management,
boot-camp init, gauge provisioning, telemetry, encoder, ILI9341 driver,
UI) natively on the PC against register-level models of the board's ICs.

Where `interface-tester/` fakes the telemetry and replaces the display
API, this emulator models **the PCB**: the ICs behave as their
datasheets/TRMs say, not as the firmware expects. Firmware bugs are
supposed to surface here — that is the point.

```
firmware thread (real, unmodified code)          emulator
────────────────────────────────────────  ──────────────────────────
main.c / fsm.c / charge.c / event.c        fake HAL (src/hal_impl.c)
initialization.c / provisioning.c            ├── I2C1 ─ BQ34Z100 (gauge, data flash, flags)
telemetry.c / encoder.c                      │         ├ CYPD3175 (HPI, event queue, INT PC3)
ili9341.c / gfx.c / ui/*                     │         ├ STPD01   (VOUT/ILIM/INT_STAT, EN PC11)
                                             │         └ INA3221  (shunts A1/A2, MSB-first)
                                             ├── I2C3 ─ TPS25750 (len-prefixed regs, PTCH→APP
                                             │          patch flow, burst addr 0x22, IRQ PB14)
                                             ├── ADC1 ─ NTC dividers + current/power senses
                                             ├── SPI1 ─ ILI9341/ST7789 panel (CASET/RASET/
                                             │          RAMWR decode → SDL window)
                                             └── GPIO ─ pin table, EXTI0, CHRG_OK, EN pins
                                           board.c: 4S3P battery physics (OCV curve, SoC
                                           integration, IR drop, thermal), power flow
```

## Quick start

```sh
# needs gcc + SDL2  (Arch: sudo pacman -S sdl2)
./build.sh
./run.sh                     # SDL window, 240x320 like the panel
./run.sh --headless 8000 --at 2500:c --at 5000:v    # scripted, no window
```

Headless mode dumps `screen.ppm` (as the eye would see it: backlight,
display-on and inversion applied) and `gram.ppm` (raw GRAM) plus a
status report on exit.

## Keys / scripted events

| Key | Action |
|-----|--------|
| arrows / wheel | encoder rotation |
| SPACE | encoder button (hold = long press) |
| `c` | plug / unplug the C1 charger (TPS25750 + CHRG_OK) |
| `d` | attach / detach a sink device on C1 (powerbank sources, OTG) |
| `v` | attach / detach a PD sink on C2 (CYPD3175) |
| `1` / `2` | attach / detach a load on USB-A1 / A2 |
| `l` | attach / detach a resistive load on the lab output (`lab_load_ohm`) |
| `o` | INA3221 critical alert on A1 |
| `t` `h` `u` `f` | gauge faults: overtemp / BATHI / BATLOW / CF |
| `x` `z` `r` | CYPD3175 OVP / OCP / hard reset |
| `g` | STPD01 short-circuit fault |
| `k` | toggle CHRG_OK line |
| `w` | wake from STOP mode (deep sleep) |
| `q` | shutdown: push the long-press event straight into DEEP_SLEEP (only takes effect from IDLE/SLEEP, same as a real button hold) |
| `i` | print full status (board + firmware getters + pins) |
| `s` | dump screen.ppm / gram.ppm |
| ESC | quit |

`emu_config.ini` sets the initial battery state, PD contracts, loads and
IC behavior. `time_scale` accelerates only the battery physics.

## Recording gifs

`--rec <ms>` dumps a numbered frame to `frames/` every `<ms>` in both
the SDL and the headless loop. `./record.sh [out.gif]` wraps it:
records an interactive SDL session until ESC, trims the display-off
boot frames, inverts the colors back to the real-panel palette and
assembles the gif with ffmpeg (`REC_MS=50` for 20 fps).

## Gauge calibration model

The BQ34Z100 model exercises the on-device calibration wizard end to
end: the DF calibration block (subclass 104) is live — reported V/I/T
are distorted by the stored divider/gain/offset, seeded with deliberate
errors at attach — the internal offset routines run with realistic
CCA/BCA timing, and IT_ENABLE sets QEN + Update Status 0x04. A correct
wizard run visibly converges the readings.

Every log line carries the firmware FSM state as a column, and every
event fired into the FSM is traced as a `[FSM ]` line with the
transition it caused (`CHARGER_CONNECTED: IDLE -> CHARGING`), including
events that were ignored or refused by a guard — the log alone tells the
whole story of a scenario. The hook is `PB_FSM_TraceEvent()`, a weak
no-op in fsm.c that the emulator overrides; the target build pays
nothing for it.

## Fidelity notes / limits
- STPD01 `DIGITAL_ENABLE` (0x06) is stored but does not gate the output
  in the model (power-on follows the EN pin only) — verify on hardware
  before trusting either.
- Timing is wall-clock: I2C/SPI transfers are instantaneous, HAL_Delay
  sleeps for real. `time_scale` compresses battery time only.
- BQ25713 is on the TPS25750's private I2C bus on the real board and is
  not modeled beyond CHRG_OK + the charge-current profile.
