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

Every log line carries the firmware FSM state as a column, and every
event fired into the FSM is traced as a `[FSM ]` line with the
transition it caused (`CHARGER_CONNECTED: IDLE -> CHARGING`), including
events that were ignored or refused by a guard — the log alone tells the
whole story of a scenario. The hook is `PB_FSM_TraceEvent()`, a weak
no-op in fsm.c that the emulator overrides; the target build pays
nothing for it.

## Findings (state of the firmware when the emulator was written)

Discovered by building faithful IC models and running the firmware
against them — each reproduces deterministically:

1. **Target build broken**: `initialization.c` uses `INA3221_REG_CONFIG`
   which was defined nowhere (initialization.c *is* in the CubeMX
   Makefile). Fixed in `system/defines.h` (CONFIG = 0x00 per datasheet).
2. **TPS25750 length-prefix framing** (`charge.c`) — **FIXED**: every
   host-interface register read returns `[len][data...]` and writes expect
   `[reg][len][data...]` — `initialization.c` handled this,
   `primaryUSBC_ConnectionINT()` did not (`HAL_I2C_Mem_Read/Write` on
   INT_EVENT1 / INT_CLEAR1 / POWER_STATUS / PDO / RDO with no length
   byte). Result with the faithful model (`tps_strict_len = 1`): C1
   charger plug was **silently never detected**, no EVT_CHARGER_CONNECTED,
   FSM never entered CHARGING. Fixed by moving all TPS25750 register
   access to the shared length-prefixed helpers in
   `system/tps25750_io.c`; `tps_strict_len = 0` is no longer needed.
3. **CHRG_OK at boot** — **FIXED**: BQ25713 CHRG_OK is low without a
   valid adapter, so the first `readCS()` pushed `EVT_ERROR` and the FSM
   started in STATE_ERROR on every battery-powered boot. Fixed: the
   falling edge is an error only while a charger contract is active
   (adapter expected), evaluated after the INT handlers so a same-cycle
   unplug is already accounted for.
4. **Spurious CRITICAL BATTERY at every boot** — **FIXED**: during the
   BOOT splash `UI_Tick()` returned before `Telemetry_Poll()`, and the
   warning arbiter ran first on the tick that left BOOT — it read
   `telemetry.voltage_mV == 0` → WARN_VCRIT → locked all outputs and
   entered light sleep. Fixed: poll moved to the top of the tick (data
   flows during the splash) and the arbiter is gated on
   `telemetry.sensor_ok`.
5. **`Screen_Sleep_Draw()` is (nearly) unreachable** — **FIXED**:
   `UI_Tick()` early-returns for UI_SCREEN_SLEEP before the draw switch,
   so sleep never blanked the panel — the last frame stayed frozen.
   Fixed: the sleep branch draws the blank frame itself on entry, and
   the refresh block can no longer consume the redraw flag when a screen
   handler entered SLEEP in the same tick.
6. **Backlight polarity conflict** — **FIXED**: `ili9341.c` drives
   BCKL_CTRL as active-low (JP402 in PMOS position), `fsm.c` wrote
   SET/RESET as if it were active-high — with the PMOS jumper IDLE
   turned the backlight OFF and SLEEP turned it ON. Fixed: polarity
   lives only in the driver (`ILI9341_Backlight()`), fsm.c calls it.
7. **DEEP_SLEEP is a one-way trap** — **FIXED**: STOP mode wakes on the
   encoder EXTI, but nothing pushed `EVT_BUTTON_SHORT` after the wake
   (and nothing ever pushed `EVT_BUTTON_LONG` to get there either), so
   DEEP_SLEEP was both unreachable and inescapable. Fixed: a ≥3 s hold
   released pushes `EVT_BUTTON_LONG` (main.c), the post-STOP resume in
   `DeepSleep_Enter` pushes `EVT_BUTTON_SHORT`, and the UI restarts from
   the splash swallowing the wake press (`UI_OnDeepSleepWake`).
8. **C2 auto-enable bypassed battery protection** — **FIXED** (found
   after the list above): `secondaryUSBC_ConnectionINT()` re-opened
   STPD01 + USB-C2 on CONTRACT_COMPLETE regardless of FSM state — so a
   device plugged into C2 during SAFETY_LOCK / LOW_V (states that had
   just closed every output to protect the pack) powered up at full PD
   voltage anyway. Fixed: auto-enable only in IDLE/SLEEP
   (`PB_FSM_ActiveState()` gate); elsewhere the event is acked and the
   contract stored, output stays closed. The same policy gates the
   manual UI enables (SETTINGS toggles, OUTPUT page): turning an output
   ON is refused in CHARGING and in the protection states, turning OFF
   always goes through.
9. **Opening SETTINGS silently killed A1/A2** — **FIXED** (found after
   the list above): `Screen_Settings_Update()` force-wrote the USB-A
   pins from menu-row state that was never synced with reality (default
   OFF), so merely opening the menu shut both ports off behind the
   FSM's back. Fixed: drawing never actuates — the menu reads the live
   pin state, and only an explicit toggle click writes the pin.
10. **BQ34Z100 current read as unsigned** — **FIXED**: the gauge reports
    current/avgCurrent as two's complement; charge.c reinterpreted them
    as unsigned — visible as ~65000 mA readings while discharging. Fixed
    with a signed cast in `readSensors()`.
11. **SLEEP ignored the button** — **FIXED**: in STATE_SLEEP the main
    loop skips `UI_Tick()`, which is the only consumer of button presses,
    and nothing translated the press into `EVT_BUTTON_SHORT` — so the
    SLEEP → IDLE wake existed in the transition table but could never
    fire from the button (only a charger plug or an SoC event got out).
    Fixed in main.c: while in SLEEP the raw press is consumed and pushed
    as `EVT_BUTTON_SHORT`.
12. **Overcharge SAFETY_LOCK was an inescapable deadlock** — **REDESIGNED**:
    OVCH → SAFETY_LOCK closed every output, but the firmware cannot stop
    the C1 charge current anyway (BQ25713 is slaved to the TPS25750 on its
    private bus) — so the pack kept charging while "locked" and the only
    exit (SOC_OK) could never fire near full. Final design: overcharge
    causes NO transition — charger attached always means CHARGING with
    outputs off (never passthrough; with the adapter in, loads would be
    fed by the power path without discharging the pack anyway). The
    `ovchargeBlock` flag is informational only (UI BATTERY FULL warning +
    trace); recovery is unplug → IDLE → discharge until the gauge clears
    BATHI, on whose falling edge `charge.c` emits `EVT_SOC_OK`. The gauge
    model gained the matching BATHI set/clear hysteresis (16.4 V / 16.0 V).
    See docs/FSM.md "Overcharge".

## Fidelity notes / limits
- STPD01 `DIGITAL_ENABLE` (0x06) is stored but does not gate the output
  in the model (power-on follows the EN pin only). The datasheet is
  ambiguous here and the firmware writes 0 at init and never 1 — verify
  on hardware before trusting either.
- Timing is wall-clock: I2C/SPI transfers are instantaneous, HAL_Delay
  sleeps for real. `time_scale` compresses battery time only.
- BQ25713 is on the TPS25750's private I2C bus on the real board and is
  not modeled beyond CHRG_OK + the charge-current profile.
