# Ducker-Charger — Project Documentation

Entry point of the Ducker-Charger documentation: what the project is, how the
hardware is organized, how the firmware works, and which tools exist to develop
and test it. Every section links to the detailed document that covers it.

## Documentation map

```text
docs/
├── README.md                        # ← you are here: overview + code walkthrough
├── hardware/
│   ├── Hardware_Architecture.md     # power architecture, rails, subsystem analysis
│   └── BSP_reference.md             # pin map, ADC channels, I2C buses, GPIO tables
├── firmware/
│   ├── Firmware_Architecture.md     # module layers, main loop, telemetry/encoder/display/UI
│   ├── FSM.md                       # power-state machine: events, states, transition matrix
│   ├── Charge_Management.md         # charge.c: sensors, register maps, port logic
│   ├── Boot_and_Provisioning.md     # every-boot IC init + one-time gauge provisioning
│   ├── Gauge_Calibration.md         # on-device fuel-gauge calibration wizard
│   └── UI_and_Display.md            # screens, widgets, per-screen behaviour
└── assets/
    └── ui_demo.gif                  # UI demo capture
```

Related documentation outside `docs/`:

| Location | Content |
| --- | --- |
| [`PCB/README.md`](../PCB/README.md) | KiCad projects, schematic sheets, system block diagram |
| [`PCB/Ducker-Charger/docs/`](../PCB/Ducker-Charger/docs/) | IC datasheets and technical reference manuals |
| [`firmware/CYPD3175/README.md`](../firmware/CYPD3175/README.md) | Secondary-port PD controller firmware (build, flash, LED driver) |
| [`firmware/emulator/README.md`](../firmware/emulator/README.md) | Full-PCB emulator (firmware vs datasheet-level IC models) |
| [`firmware/interface-tester/README.md`](../firmware/interface-tester/README.md) | Desktop UI simulator (SDL) |

---

## 1. What the Ducker-Charger is

A custom, open-source smart power bank built around a 4S3P pack of twelve 18650
cells (Sony Murata VTC5 — 14.4 V nominal / 16.8 V max / ~115 Wh). It exposes:

- **USB-C1 (primary)** — bidirectional USB-PD: charges the pack *and* powers loads (OTG).
- **USB-C2 (secondary)** — source-only USB-PD output, with a user-settable output ceiling.
- **2× USB-A** — fixed 5 V outputs, individually switchable.
- **Lab output** — banana-jack pins driven like a bench supply (shares the STPD01 rail with USB-C2, interlocked).

HMI: ILI9341 TFT (SPI) + rotary encoder with push-button.

Design philosophy: **distributed architecture**. Cell safety, power conversion,
PD negotiation and gauging each live in a dedicated IC; the STM32 supervises,
aggregates telemetry and drives the UI. Battery protection works even if every
line of firmware fails.

## 2. Hardware in one page

Full analysis: [`hardware/Hardware_Architecture.md`](hardware/Hardware_Architecture.md).
Pin-level reference: [`hardware/BSP_reference.md`](hardware/BSP_reference.md).

**NVDC topology**: the system load hangs on `VSYS`, regulated by the buck-boost
charger from either VBUS or the battery — the device is "instant-on" from USB-C
even with a dead pack.

| Subsystem | IC | Role |
| --- | --- | --- |
| Cell protection | TI **BQ77915** | Autonomous hardware OV/UV/OC/SC cutoff + passive balancing (4S, always-on). No firmware involved. |
| Charger | TI **BQ25713** | 4-switch buck-boost NVDC charger. On the TPS25750's **private** I2C bus — the MCU cannot reach it; it only sees the analog IADPT/IBAT/PSYS pins and CHRG_OK / EN_OTG GPIOs. |
| Primary PD (USB-C1) | TI **TPS25750** | PD controller; ROM-based, receives its 32 KB patch bundle from the STM32 at every boot. |
| Secondary PD (USB-C2) | Infineon **CYPD3175** (CCG3PA) | Autonomous PD negotiation, monitored by the MCU over HPI. Replaced the EOL STUSB4710. Own firmware, flashed independently. |
| Aux programmable rail | ST **STPD01** | Generates the voltage negotiated on USB-C2 / set for the lab output. |
| Fuel gauge | TI **BQ34Z100-R2** | Impedance Track SoC / time-to-empty. Battery-referenced, behind an **ISO1540** I2C isolator. |
| Telemetry | TI **INA3221** | Shunt monitor for USB-A1/A2 (CH3 grounded). |
| Aux rails | TI **TPS54302 / TPS54202** | Step-downs for logic and 5 V rails. |
| Host | **STM32F401RBT6** | I2C master, ADC telemetry, FSM, HMI, supervisor. |

**Buses:**

| Bus | Pins | Devices (7-bit addr) |
| --- | --- | --- |
| I2C3 "PD bus" | PA8 / PC9 | TPS25750 (0x20, length-prefixed host interface) |
| I2C1 "system bus" | PB6 / PB7 | CYPD3175 (0x08, HPI) · STPD01 (0x05) · INA3221 (0x40) · BQ34Z100 (0x55, isolated) |
| ADC1 | 8 channels | 5× NTC + IADPT / IBAT / PSYS from the charger |
| SPI1 | PA5 / PB5 | ILI9341 display (write-only) |
| TIM3 | PC6 / PC7 | Quadrature encoder (button on PC0 / EXTI0) |

**Grounds:** system `GND` and battery `-BATT` join only through the protection
FETs and the sense resistor; when protection trips, `-BATT` floats — hence the
ISO1540 isolator in front of the gauge.

## 3. Firmware — how the code works

Full detail: [`firmware/Firmware_Architecture.md`](firmware/Firmware_Architecture.md).

### 3.1 Layered structure (`firmware/cubeMX/Core/`)

```text
UI          ui/ui_state.c · ui/screens.c · ui/widgets.c
App         app/telemetry.c · app/encoder.c · app/initialization.c · app/provisioning.c · app/calibration.c
System      system/fsm.c · system/charge.c · system/event.c · system/tps25750_io.c
Display     display/ili9341.c · display/gfx.c
HAL         CubeMX-generated (SPI1 · I2C1 · I2C3 · TIM3 · ADC1 · GPIO)
```

Strictly one-directional: lower layers never know about upper ones. The UI reads
charge-layer getters read-only; only the FSM drives actuators; only `charge.c`
touches sensors and pushes events.

### 3.2 Boot (`main.c` + `app/initialization.c` + `app/provisioning.c`)

Doc: [`firmware/Boot_and_Provisioning.md`](firmware/Boot_and_Provisioning.md).

1. HAL + clock (HSE/PLL, 84 MHz) and `MX_*_Init()` peripherals.
2. **`System_Initialization()`** — every-boot bring-up of ICs with volatile config:
   - **TPS25750**: streams the 32 KB patch bundle (`app/tps25750_bundle.c`,
     generated from `firmware/TPS25750/*.bin`) over I2C3, verifies `PTCH → APP`
     transition. Skipped on warm reboot. No bundle → no PD, no charging.
   - **INA3221** config (CH1-2, averaging) and **STPD01** safe defaults
     (5 V / 3 A, output **off**).
   - 50 ms bounded timeouts, one retry per step; failures degrade the boot
     (`Init_GetReport()` feeds the boot screen) but never block it.
3. **Gauge provisioning** — one-time, marker-guarded write of pack parameters
   into the BQ34Z100 data flash; runs only on a verified device, at rest.
4. `Encoder_Init` · `Telemetry_Init` · `ILI9341_Init` · `UI_Init` · charge-layer
   `init()` · `PB_FSM_Init()` → IDLE.

### 3.3 Main loop (cooperative, no RTOS)

Each `while(1)` iteration: refresh port-status pins (`readNCS()`), pop one event
from the ring buffer into `PB_FSM_FireEvent()`, track encoder activity and the
deep-sleep button hold, `PB_FSM_Update()` (onExit/onEnter/onRun), `Telemetry_Poll()`
(rate-limited to 500 ms; keeps running in SLEEP so energy/uptime/histories don't
stop), and `UI_Tick()` (250 ms refresh) unless the FSM has the backlight off.
Sensor polling lives in each state's `onRun`, so what gets polled follows the
power state automatically.

### 3.4 Power-state FSM (`system/fsm.c`)

Doc: [`firmware/FSM.md`](firmware/FSM.md). Hybrid **vtable + transition matrix**:
behaviour per state (`onEnter/onRun/onExit`), transitions in a `[state][event]`
lookup. States: `DEEP_SLEEP`, `SLEEP`, `IDLE`, `SAFETY_LOCK`, `LOW_V`,
`EMERGENCY`, `CHARGING`, `MANUAL` (lab mode), `ERROR`. Events come from the
charge layer (charger plug/unplug, SoC thresholds 15 %/10 %, under-voltage,
overtemperature, critical faults) and from the UI (buttons, inactivity, lock,
lab mode). Protection states cut outputs in `onEnter`; `DEEP_SLEEP` uses STM32
STOP mode with the encoder button as sole wake source.

### 3.5 Charge management (`system/charge.c`)

Doc: [`firmware/Charge_Management.md`](firmware/Charge_Management.md). The only
module that touches sensors/actuators:

- **ADC**: NTC → °C (Beta equation), IADPT → mA, PSYS → W.
- **I2C**: gauge registers/flags, INA3221 currents + critical alerts, STPD01 status.
- **Primary port**: on `PD_IRQ`, decode TPS25750 `INT_EVENT1` (plug, power status,
  new contract as sink/provider), parse Fixed-Voltage PDO/RDO, gate **OTG**
  (EN_OTG raised only in IDLE/SLEEP; the pin is an independent kill switch on C1 sourcing).
- **Secondary port**: on `C2_RDY`, read the CYPD3175 HPI event (contract complete,
  disconnect, OVP/OCP/OTP, hard reset), then program the STPD01 through
  `secondaryUSBC_ApplyOutput()` — which clamps the negotiated contract to the
  user-set ceiling, verifies no post-setup fault, and only then enables the port.
- Pushes all FSM events; exposes getters for telemetry/UI.

### 3.6 Telemetry, display, UI

Docs: [`firmware/Firmware_Architecture.md`](firmware/Firmware_Architecture.md),
[`firmware/UI_and_Display.md`](firmware/UI_and_Display.md).
`app/telemetry.c` reshapes charge-layer getters into UI structs (battery snapshot,
per-port V/I with 60-sample histories, lifetime/session stats) — no I2C of its own.
Display stack: `ili9341.c` (SPI, RGB565, 240×320) + `gfx.c` (text/primitives).
UI: screen carousel MAIN ↔ DETAIL ↔ GRAPH ↔ PORTS ↔ STATS, SETTINGS overlay
(long-press/double-click) with OUTPUT / DISPLAY / CALIBRATION / TEST sub-pages, CONFIRM/WARNING
modals, FAULT takeover, SLEEP page. Two-phase draw (full `Draw` on entry, partial
`Update` at 4 fps) eliminates flicker.

### 3.7 Companion firmware (not on the STM32)

- **TPS25750** (`firmware/TPS25750/`): TI web-GUI config project + generated
  `.bin` → embedded as `tps25750_bundle.c`, streamed at every boot (volatile).
- **CYPD3175** (`firmware/CYPD3175/`): built with Infineon CCGx Power SDK /
  PSoC Creator; flashed once over SWD or CC bootloader. Repo keeps the built hex,
  a status-LED driver, SDK patches and the PD config (PDOs 5/9/12/15/20 V @ 3 A).
- **BQ25713**: no firmware — configured at runtime by the TPS25750.

## 4. Development tooling (host-side, no hardware needed)

| Tool | What it does |
| --- | --- |
| [`firmware/emulator/`](../firmware/emulator/README.md) | Runs the **unmodified firmware** natively against register-level models of every IC (HPI, TPS patch flow, gauge data flash, battery physics with OCV/IR/thermal). SDL window renders the panel; headless scripted mode for regression runs. Firmware bugs are supposed to surface here. |
| [`firmware/interface-tester/`](../firmware/interface-tester/README.md) | UI-only simulator: real UI sources on a fake framebuffer + fake telemetry from `sim_config.ini`. Fastest way to iterate on screens. |
| `firmware/tests/` | Host-side unit tests: `make` runs `test_event`, `test_fsm`, `test_charge`, `test_cypd3175`, `test_hpi_schematic`, `test_math` against HAL stubs and IC models. |

## 5. Build & flash

```sh
cd firmware/cubeMX && make          # arm-none-eabi → build/cubeMX.elf/.hex/.bin
cd firmware/tests   && make         # host unit tests (gcc)
cd firmware/emulator && ./build.sh && ./run.sh          # full-board emulator (SDL2)
cd firmware/interface-tester && ./build.sh && ./run.sh  # UI simulator (SDL2)
```

STM32 flashes over SWD (ST-Link). Per-IC programming paths are mapped in
[`firmware/Boot_and_Provisioning.md`](firmware/Boot_and_Provisioning.md).
**Caveat:** CubeMX regeneration rewrites the Makefile — re-check the application
entries in `C_SOURCES` afterwards.

## 6. Mechanical

`powerbank_enclosure/` holds the 3D-printable enclosure (prototype 3): Fusion 360
source (`.f3d`), STEP, STLs (body, upper lid, short-side lid) and renders.

## 7. Status & open points

- Work in progress; hardware not yet validated — see the root [README](../README.md) disclaimer.
- Open `CHECK:` items (TPS burst chunking/address, gauge data-flash offsets,
  design capacity vs final pack, CYPD3175 LED pin) tracked in
  [`firmware/Boot_and_Provisioning.md`](firmware/Boot_and_Provisioning.md#open-verification-points).
- Fuel-gauge calibration is done **on-device**: SETTINGS → Calibration, a
  nine-step multimeter-assisted wizard (electrical cal, pack config, IT
  enable, learning-cycle monitor) — see
  [`firmware/Gauge_Calibration.md`](firmware/Gauge_Calibration.md). Until it
  is run on the real pack, SoC readings are indicative and the UI shows a
  NOT CALIBRATED warning at every wake. ChemID selection (optional accuracy
  refinement) still needs one bqStudio session.
