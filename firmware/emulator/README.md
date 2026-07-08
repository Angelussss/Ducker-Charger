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
| `o` | INA3221 critical alert on A1 |
| `t` `h` `u` `f` | gauge faults: overtemp / BATHI / BATLOW / CF |
| `x` `z` `r` | CYPD3175 OVP / OCP / hard reset |
| `g` | STPD01 short-circuit fault |
| `k` | toggle CHRG_OK line |
| `w` | wake from STOP mode (deep sleep) |
| `i` | print full status (board + firmware getters + pins) |
| `s` | dump screen.ppm / gram.ppm |
| ESC | quit |

`emu_config.ini` sets the initial battery state, PD contracts, loads and
IC behavior. `time_scale` accelerates only the battery physics.

## Findings (state of the firmware when the emulator was written)

Discovered by building faithful IC models and running the firmware
against them — each reproduces deterministically:

1. **Target build broken**: `initialization.c` uses `INA3221_REG_CONFIG`
   which was defined nowhere (initialization.c *is* in the CubeMX
   Makefile). Fixed in `system/defines.h` (CONFIG = 0x00 per datasheet).
2. **TPS25750 length-prefix framing** (`charge.c`): every host-interface
   register read returns `[len][data...]` and writes expect
   `[reg][len][data...]` — `initialization.c` handles this,
   `primaryUSBC_ConnectionINT()` does not (`HAL_I2C_Mem_Read/Write` on
   INT_EVENT1 / INT_CLEAR1 / POWER_STATUS / PDO / RDO with no length
   byte). Result with the faithful model (`tps_strict_len = 1`): C1
   charger plug is **silently never detected**, no EVT_CHARGER_CONNECTED,
   FSM never enters CHARGING. Set `tps_strict_len = 0` to bypass and
   test the rest.
3. **CHRG_OK at boot**: BQ25713 CHRG_OK is low without a valid adapter,
   so the first `readCS()` pushes `EVT_ERROR` and the FSM starts in
   STATE_ERROR on every battery-powered boot.
4. **Spurious CRITICAL BATTERY at every boot**: during the BOOT splash
   `UI_Tick()` returns before `Telemetry_Poll()` (ui_state.c:389), and
   the warning arbiter (ui_state.c:130) runs first on the tick that
   leaves BOOT — it reads `telemetry.voltage_mV == 0` → WARN_VCRIT →
   locks all outputs and enters light sleep.
5. **`Screen_Sleep_Draw()` is unreachable**: `UI_Tick()` early-returns
   for UI_SCREEN_SLEEP before the draw switch, so sleep never blanks
   the panel or sends DISPOFF — the last frame stays frozen on screen.
6. **Backlight polarity conflict**: `ili9341.c` drives BCKL_CTRL as
   active-low (JP402 in PMOS position), `fsm.c` writes SET/RESET as if
   it were active-high (Idle_Enter/Sleep_Enter etc.). With the PMOS
   jumper, IDLE turns the backlight OFF and SLEEP turns it ON.
7. **DEEP_SLEEP is a one-way trap**: STOP mode wakes on the encoder
   EXTI, but nothing ever pushes `EVT_BUTTON_SHORT` /
   `EVT_CHARGER_CONNECTED` from an interrupt context, so the FSM can
   never leave STATE_DEEP_SLEEP (already hinted by the comment in
   main.c).

## Fidelity notes / limits

- BQ34Z100 reports current as two's complement (the real gauge does);
  charge.c currently reinterprets it as unsigned float — visible as
  ~65000 mA readings while discharging.
- STPD01 `DIGITAL_ENABLE` (0x06) is stored but does not gate the output
  in the model (power-on follows the EN pin only). The datasheet is
  ambiguous here and the firmware writes 0 at init and never 1 — verify
  on hardware before trusting either.
- Timing is wall-clock: I2C/SPI transfers are instantaneous, HAL_Delay
  sleeps for real. `time_scale` compresses battery time only.
- BQ25713 is on the TPS25750's private I2C bus on the real board and is
  not modeled beyond CHRG_OK + the charge-current profile.
