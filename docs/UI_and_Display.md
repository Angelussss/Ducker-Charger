# UI and Display System

Complete description of the display stack: from SPI bytes to the full
user interface, including input handling and the state machine.

---

## Stack overview

```
┌─────────────────────────────────────────────────────────┐
│  main()  — calls UI_Init() once, UI_Tick() every loop   │
├─────────────────────────────────────────────────────────┤
│  ui_state.c   — state machine: which screen, navigation │
├──────────────────────┬──────────────────────────────────┤
│  screens.c           │  encoder.c                       │
│  (draw every screen) │  (rotary knob + button)          │
├──────────────────────┤                                  │
│  widgets.c           │  telemetry.c                     │
│  (battery, graphs,   │  (BQ34Z100 I2C1, 500 ms poll)    │
│   icons, rows)       │                                  │
├──────────────────────┴──────────────────────────────────┤
│  gfx.c  — text, lines, rectangles, circles              │
├─────────────────────────────────────────────────────────┤
│  ili9341.c  — SPI driver (ILI9341 / ST7789V)            │
├─────────────────────────────────────────────────────────┤
│  SPI1  PA5(SCK) PA7(MOSI)  +  PB0(CS) PB1(DC) PB2(RST) │
│  Backlight: PB8 (BCKL_CTRL, TIM10_CH1)                  │
└─────────────────────────────────────────────────────────┘
```

---

## Layer 1 — ILI9341/ST7789 driver (`ili9341.c`)

Handles everything below the pixel level: hardware reset, init sequence,
and moving bytes to the panel over SPI1.

**Init sequence** (`ILI9341_Init`):
1. RST low 10 ms → RST high 120 ms (hardware reset)
2. Software reset (0x01) + 150 ms
3. Sleep Out (0x11) + 120 ms
4. Pixel format 0x3A/0x55 → 16 bpp RGB565
5. MADCTL 0x36/0x00 → portrait, RGB order
6. Display inversion ON (0x21) — required by the generic 2" ST7789V
   modules; if a real ILI9341 panel shows inverted colours, drop this
7. Display On (0x29)
8. `ILI9341_SetBrightness(100)` — backlight on

**SPI protocol**: every transaction is `CS low → DC low/high → byte(s) → CS high`.
DC low = command register, DC high = pixel data.

**Pixel format**: RGB565 big-endian. The panel wants the high byte first;
`ILI9341_SendPixels` swaps the bytes explicitly because the STM32 is
little-endian.

**Backlight** (`BCKL_CTRL` / PB8): jumper JP402 selects PMOS or NMOS FET.
Current config assumes PMOS (active-low: `GPIO_PIN_RESET` = backlight on).
`ILI9341_SetBrightness` is on/off only for now; true PWM dimming needs
TIM10_CH1 on PB8 (marked TODO in the source).

---

## Layer 2 — GFX primitives (`gfx.c`)

Sits above the driver. Knows about geometry and text; knows nothing about
battery bars or menus.

**Coordinate system**: origin (0, 0) = top-left. X grows right, Y grows down.
Display size 240 × 320 px.

**Fonts**: three fixed bitmap fonts, column-major (1 byte = 1 column, LSB = top row):

| Constant | Size | Use |
|----------|------|-----|
| `GFX_FontSmall` | 6 × 8 px | secondary labels |
| `GFX_FontMedium` | 8 × 16 px | normal text and values |
| `GFX_FontLarge` | 16 × 24 px | large numbers (not used directly; screens use `GFX_DrawStringScaled`) |

`GFX_DrawStringScaled` renders any font at integer scale (e.g. scale 2 =
each font pixel becomes a 2 × 2 block).

**Shapes**: line (Bresenham), rectangle, rounded rectangle, circle,
horizontal/vertical line optimised variants.

---

## Layer 3 — Widgets (`widgets.c`)

Reusable compound drawings that multiple screens share.

| Widget | Function | What it draws |
|--------|----------|---------------|
| `Widget_BatteryBar` | `_Draw` / `_Update` | Battery outline + fill bar; `_Update` redraws only the fill (faster) |
| `Widget_LineGraph` | `_Draw` / `_Trace` | Bordered graph area + ring-buffer data trace; `_Trace` draws only the line (for multi-trace overlays) |
| `Widget_ValueLabel` | `_Draw` / `_Update` | Small grey label + larger value string; `_Update` erases and rewrites only the value row |
| `Widget_StatusIcon` | `_Draw` | 8 × 8 px geometric icon + text label (no bitmaps: drawn with lines and rects) |
| `Widget_MenuRow` | `_Draw` | Full-width row, accent fill when selected |
| `Widget_Button` | `_Draw` | Compact button, accent fill when selected |

Icon types: `ICON_USB_C`, `ICON_CHARGING` (bolt), `ICON_FULL` (check),
`ICON_DISCHARGING` (arrow), `ICON_TEMP_OK/WARN/COLD`, `ICON_OUTPUT_ON/OFF`,
`ICON_ALERT` (!).

---

## Layer 4 — Screens (`screens.c`)

One `Draw` + one `Update` function per screen. `Draw` redraws the whole
screen (called once on transition). `Update` redraws only the parts that
change with new telemetry data (called every 250 ms).

### Screen map

```
      [BOOT splash]
           │ auto 1.5 s
           ▼
┌──────────────────────────────────────────────────────────┐
│  MAIN ◄──► DETAILS ◄──► GRAPH ◄──► PORTS ◄──► STATS     │
│   ▲                                              │       │
│   └──────────────── wrap ────────────────────────┘       │
│                                                          │
│  SETTINGS overlay (long press to open, Exit to close)    │
│     ├─ USB-A 1/2 toggles                                 │
│     ├─ Lab OUTPUT page (STPD01 voltage + current limit)  │
│     ├─ USB-C 2 OUTPUT page (PDO selection)               │
│     ├─ Lock all (confirm modal)                          │
│     ├─ DISPLAY page (brightness, screen off, auto-sleep) │
│     └─ TEST page (force warning scenarios)               │
│                                                          │
│  Modals: CONFIRM (yes/no), WARNING (temp / voltage)      │
│  SLEEP (screen dark)                                     │
└──────────────────────────────────────────────────────────┘
```

### Screen descriptions

**BOOT** — logo + title, shown 1.5 s at power-on; also shown briefly
(1.2 s) when waking from light sleep.

**MAIN** — battery shape (Widget_BatteryBar), giant SoC %, time-to-empty or
time-to-full under it, pack voltage and current at 2× scale, 30-sample
current graph.

**DETAILS** — 2 × 3 grid of value labels (SoC, voltage, current, power,
temperature, charge phase) + CELLS box (4 parallel-group voltages; note:
the current PCB has no per-cell path to the MCU — values will show 0 until
a multi-cell gauge is added).

**GRAPH** — full-screen current graph. Short press cycles the Y-axis full
scale: 1 A → 5 A → 15 A → back to 1 A.

**PORTS** — one graph area, up to 5 overlaid traces (USB-A1 cyan, USB-A2
lime, USB-C1/OTG white, USB-C2 magenta, Lab yellow) + legend with live V/I
per port. Port data comes from `port_stats[]` in telemetry; A1/A2 have real
INA3221 shunts; C2/Lab are stubbed until dedicated sensing is added.

**STATS** — lifetime data from BQ34Z100 (cycle count, SoH, capacity) and
per-boot session counters (charges, max temp/current, energy out, uptime).

**SETTINGS** — overlay, 8 scrollable rows. USB-A1/A2 are plain GPIO toggles
(PC1/PC2). Lab and USB-C2 open their own OUTPUT sub-page.

**OUTPUT** — two channels sharing one STPD01 rail; hardware interlock
(enabling one disables the other via GPIO PA11/PA12). Lab page: voltage
3–20 V in 100 mV steps, current limit 100 mA–3 A in 100 mA steps. USB-C2
page: PDO selection (5/9/12/15/20 V). Writing to STPD01 registers is marked
TODO — the UI state is tracked but the I2C write is not yet wired.

**DISPLAY** — brightness slider (10–100 %, maps to `ILI9341_SetBrightness`),
screen-off toggle, auto-sleep timeout (Off / 1 / 5 / 15 min).

**TEST** — forces telemetry values for UI stress scenarios: low voltage,
critical voltage, low temperature, high temperature, overcurrent, reset
(calls `Telemetry_ForcePoll`). Useful during development.

### Colour thresholds (defined in `screens.c`)

| Threshold | Value | Effect |
|-----------|-------|--------|
| `TH_SOC_LOW` | 15 % | SoC turns orange |
| `TH_SOC_CRIT` | 5 % | SoC turns red |
| `TH_TEMP_WARN` | 45 °C | temp header icon turns red |
| `TH_TEMP_CRIT` | 60 °C | WARNING modal (over-temp) |
| `TH_TEMP_LOW` | 10 °C | WARNING modal (cold) |
| `TH_VLOW` | ~13 V | VLOW warning |
| `TH_VCRIT` | ~12 V | VCRIT warning → lock + light sleep |

Voltage band colours on MAIN: 14–17 V green, 13–14 V orange, 12–13 V red,
< 12 V red blinking.

### Header icons (every non-modal screen)

Drawn by `Screen_Header_RefreshIcons()` every 250 ms tick. Icons shown:
USB-C plug (vbus), bolt/check/arrow (charging state), thermometer
(temperature: OK/warn/cold), alert "!" when a warning condition is active.

---

## Layer 5 — Encoder input (`encoder.c`)

Hardware: EC11E15244G1 on-board encoder, 15 pulses / 30 detents per
revolution. External RC debounce + pull-ups on R401/403/404.

TIM3 is configured in encoder mode (quadrature decoding in hardware; zero
CPU for counting). **The `.ioc` must have `EncoderMode = TIM_ENCODERMODE_TI12`**
(counts both A and B edges). The default TI1 mode gives 1 count/detent
and `delta/4` loses slow rotations entirely.

- `Encoder_Init(&htim3)` — starts TIM3, records initial counter
- `Encoder_GetDelta()` — reads `TIM3->CNT`, computes `delta / 2` (2 counts
  per detent in TI12), clears accumulator for next call
- `Encoder_IsPressed()` — falling-edge detection on PC0 (ENCOD_BUTT),
  50 ms software debounce, one-shot flag
- `Encoder_IsHeld()` — raw pin read (no debounce, no consume); used by
  the state machine for long-press detection

---

## Layer 6 — Telemetry (`telemetry.c`)

Reads BQ34Z100 fuel gauge via I2C1 (I2C_LP / ISO1540 / battery side)
every 500 ms. Results go into the global `SystemTelemetry_t telemetry`
struct which all screens read directly.

**BQ34Z100 registers polled** (little-endian, 2 bytes each):

| Register | Name | Field |
|----------|------|-------|
| 0x02 | StateOfCharge | `soc_percent` |
| 0x06 | Flags | `is_full`, `is_charging`, `over_temp` |
| 0x08 | Voltage | `voltage_mV` |
| 0x0A | AverageCurrent | `current_mA` (signed) |
| 0x0C | Temperature | `temp_celsius` (raw 0.1 K → °C) |
| 0x10 | FullChargeCapacity | `sys_stats.full_cap_mAh` |
| 0x18 | AverageTimeToEmpty | `tte_min` |
| 0x1A | AverageTimeToFull | `ttf_min` |
| 0x2A | CycleCount | `sys_stats.cycle_count` |
| 0x2E | StateOfHealth | `sys_stats.state_of_health` |

**BQ25713** (charger) is on TPS25750's private I2C_EX bus and is not
reachable from the STM32. `vbus_present` and `charge_phase` are derived
from BQ34Z100 current sign and flags instead.

`port_stats[]`, `cell_mv[]` and most of `sys_stats` are defined but
currently zero/stubbed — they need INA3221 reads and a multi-cell gauge
to be populated.

---

## UI state machine (`ui_state.c`)

`UI_Tick()` is called every main loop iteration. It runs in order:

1. **Boot timeout** — if on BOOT screen and 1.5 s elapsed → navigate to MAIN
2. **Sleep wake** — if on SLEEP screen and button pressed → wake (with or without splash)
3. **Auto-sleep** — if idle longer than the configured timeout → `UI_EnterSleep()`
4. **Warning check** — every tick, check `telemetry.voltage_mV` and `telemetry.temp_celsius`
   against thresholds; raise unacked warnings as a modal
5. **Long-press detection** — button held ≥ 1000 ms → open SETTINGS overlay;
   stores the origin screen to return to on Exit
6. **Click arbiter** — single press is held pending for 400 ms (`UI_DOUBLE_CLICK_MS`);
   if a second press arrives → double-click (jump to SETTINGS); if the window
   expires → clean single click delivered to the screen handler;
   a long press cancels any pending click
7. **Per-screen navigation** — encoder delta and clean click are dispatched
   to the current screen handler
8. **Telemetry poll** — `Telemetry_Poll()` returns immediately if < 500 ms
   since last poll; otherwise reads I2C and updates the struct
9. **Periodic redraw** — every 250 ms: `needs_full_redraw` flag → full `Draw()`;
   otherwise `Update()` for data-only regions + `Screen_Header_RefreshIcons()`

### Navigation summary

| Encoder action | Result |
|----------------|--------|
| Rotate (main carousel) | Move between MAIN / DETAILS / GRAPH / PORTS / STATS |
| Rotate (SETTINGS) | Scroll through 8 rows |
| Rotate (OUTPUT, DISPLAY, TEST) | Change value / scroll rows |
| Short press (GRAPH) | Cycle Y-axis scale |
| Short press (SETTINGS row 0–1) | Toggle USB-A output |
| Short press (SETTINGS row 2–3) | Open Lab / USB-C2 channel page |
| Short press (SETTINGS row 4) | Lock all (confirm modal) |
| Short press (SETTINGS row 5) | Open DISPLAY page |
| Short press (SETTINGS row 6) | Open TEST page |
| Short press (SETTINGS row 7) | Exit SETTINGS |
| Double press | Jump directly to SETTINGS from anywhere |
| Long press ≥ 1 s | Open SETTINGS overlay |

### Warning behaviour

Three warning types (`WARN_TEMP`, `WARN_VLOW`, `WARN_VCRIT`), each shown at
most once per boot. The modal auto-acks after 30 s with no input. VCRIT
additionally locks all outputs and triggers light sleep before acking.

### Sleep modes

- **Sleep** (`UI_EnterSleep`) — screen dark, only encoder press wakes it;
  returns to the screen that was active before sleep
- **Light sleep** (`UI_EnterSleepLight`) — same but wake shows a 1.2 s logo
  splash before returning to the previous screen; used after VCRIT and
  after "Light sleep" action in SETTINGS

---

## Main loop integration (`main.c`)

```c
/* After all MX_*_Init() calls: */
System_Initialization();      /* TPS25750, INA3221, STPD01 */
Provisioning_RunGauge();      /* BQ34Z100 data flash (one-time) */

Encoder_Init(&htim3);
Telemetry_Init(&hi2c1, &hi2c3);
ILI9341_Init(&hspi1);
UI_Init();                    /* shows boot screen */

while (1) {
    UI_Tick();                /* input + telemetry + redraw */
}
```

`UI_Tick()` is non-blocking. It returns immediately if neither the
250 ms display timer nor the 500 ms telemetry timer has expired.

---

## Timing summary

| Action | Period | Where |
|--------|--------|-------|
| Telemetry I2C read | 500 ms | `Telemetry_Poll()` inside `UI_Tick()` |
| Display refresh | 250 ms | `UI_Tick()` periodic branch |
| Header icon refresh | 250 ms | `Screen_Header_RefreshIcons()` |
| Encoder delta read | every `UI_Tick()` call | `Encoder_GetDelta()` |
| Click arbiter window | 400 ms | `UI_DOUBLE_CLICK_MS` |
| Long press threshold | 1000 ms | `UI_LONG_PRESS_MS` |
| Warning auto-ack | 30 s | `UI_Tick()` warning branch |
| Boot screen duration | 1500 ms | `BOOT_SCREEN_DURATION_MS` |
| Light-sleep wake splash | 1200 ms | hardcoded in `UI_Tick()` |

---

## Development — interface-tester

`firmware/interface-tester/` contains a native desktop simulator that
compiles the real `gfx.c`, `widgets.c`, `screens.c` and `ui_state.c`
directly from the firmware tree (no copies) against an SDL2 window.

```sh
cd firmware/interface-tester
./build.sh    # compile sim_ui
./run.sh      # launch
```

Edit `sim_config.ini` to change the fake sensor values shown by the UI.
See `firmware/interface-tester/README.md` for full controls and config
reference.
