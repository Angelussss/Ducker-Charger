# Boot Initialization & IC Provisioning

**Scope:** the `feat/boot-camp` work — how every programmable IC on the
Ducker-Charger gets its configuration, at boot and in production.

## The four programming paths

Not everything on this board is "flashed" the same way. The complete
map:

| IC | What | When | How |
|---|---|---|---|
| STM32F401RB | application firmware | development / production | SWD (ST-Link) |
| CYPD3175 (CCG3PA) | USB-PD firmware, persistent | production + updates | SWD (MiniProg / OpenOCD `psoc4`) or CC/I2C bootloader — see `firmware/CYPD3175/README.md` |
| TPS25750 | patch + configuration bundle, **volatile** | **every boot** | streamed over I2C3 by the STM32 (`initialization.c`) |
| BQ34Z100-R2 | data-flash configuration (pack parameters, gauging) | **once**, in production | I2C, marker-guarded (`provisioning.c`) |

The BQ25713 charger has no user firmware: it is configured at runtime
by the TPS25750 through their private I2C bus (`I2C_EX`), so it never
appears in this document.

## Every-boot initialization — `Core/Src/app/initialization.c`

`System_Initialization()` runs once from `main()` (USER CODE 2 section,
after the `MX_*_Init()` calls) and configures the ICs whose settings do
not survive a power cycle, in this order:

1. **TPS25750** *(critical)* — the device is ROM-based and boots into
   patch mode: the 32 KB patch/config bundle
   (`Core/Src/app/tps25750_bundle.c`, generated from
   `firmware/TPS25750/TPS25750D_DuckerCharger.bin`) is streamed over
   I2C3 using the Patch Bundle Burst Mode protocol (`PBMs` → raw burst
   writes → `PBMc`), then the transition to `APP` mode is verified.
   Register access goes through the shared `system/tps25750_io.c`
   helpers (the host interface is length-prefixed on the wire); only
   the raw burst writes bypass them, as the burst target is a separate
   plain I2C address with no register framing.
   A warm reboot (device already in `APP`) skips the stream entirely.
   Without this step there is no PD on USB-C1 and no charging.
2. **INA3221** — configuration register: channels 1–2 enabled (channel
   3 is grounded on this board), 16-sample averaging, 1.1 ms
   conversions, continuous mode. Written and read back.
3. **STPD01** — safe defaults: VOUT 5 V, ILIM 3 A, **output disabled**.
   Turning the output on remains the runtime logic's job.

Design rules baked into the module:

- **Bounded timeouts everywhere** (50 ms per transaction), never
  `HAL_MAX_DELAY`: a dead bus degrades the boot, it must never freeze
  the product.
- Each step is retried once, then recorded as failed.
- **The boot always continues.** `Init_GetReport()` exposes the
  per-step outcome so the UI boot screen (or a UART log) can show what
  failed; a failed TPS25750 means a degraded product (no PD/charge),
  not a dead display.

## One-time provisioning — `Core/Src/app/provisioning.c`

The fuel gauge keeps its configuration in its own data flash, so this
is **not** part of the hot boot path: DF writes wear the gauge's flash
and must happen with the pack at rest.

### Version check over I2C

`Provisioning_Check()` answers "does this gauge need programming?"
in two stages:

1. `Control() DEVICE_TYPE` (0x0001) must return **0x0100** — confirms
   a BQ34Z100 actually answers at 0x55. Any other answer means wrong
   or absent silicon: the module then **refuses to write anything**.
2. A two-byte marker in *Manufacturer Info Block A*: `'D'` + a
   pack-configuration revision. Blank marker → factory-fresh gauge;
   older revision → re-provision. Bump `PROV_MARKER_1` whenever the
   patch list changes and already-deployed units re-provision
   themselves at the next check.

`Provisioning_Required()` returns 1 only for the two legitimate cases
(blank / old revision) — bus errors and wrong devices are *not*
invitations to write.

### What gets written

`Provisioning_RunGauge()` — guarded by `|AverageCurrent| < 50 mA` —
unseals with the TI default keys and applies a patch list
(`GaugeDfPatch_t`: subclass, offset, bytes) through the standard
data-flash block protocol (0x3E/0x3F class+block select, 0x40 window,
0x60 checksum). Blocks whose content already matches are skipped to
spare the flash. Current list: design capacity (7800 mAh, 3P × VTC5),
series cells (4), the revision marker.

### Electrical calibration (on-device)

Shunt/divider/offset calibration is done from the device itself —
SETTINGS → Calibration, multimeter-assisted, no bqStudio needed. The
resulting DF values belong in the patch list above as golden-image
entries. Full procedure: [Gauge_Calibration.md](Gauge_Calibration.md).

### The golden image (still to do)

Static parameters are not enough for accurate gauging: the BQ34Z100
uses Impedance Track and must **learn** Qmax and the Ra tables on a
sample pack once — ChemID selection, full
charge/rest/discharge/rest learning cycle, then export. The exported
golden image becomes additional `GaugeDfPatch_t` entries, and every
production unit receives it through this same module. Until then, SoC
and capacity readings are indicative only.

## CYPD3175 (USB-C2 PD controller) — `firmware/CYPD3175/`

Built separately with the Infineon EZ-PD CCGx Power SDK 3.5 + PSoC
Creator 4.4 and flashed independently from the STM32. The folder
contains the compiled hex (reference `pa_direct_fb` + our status-LED
module), the LED driver sources, minimal patches against the SDK
reference (full SDK sources are not redistributed for licensing
reasons), and the PD configuration values (PDOs 5/9/12/15/20 V @ 3 A,
protections) for the EZ-PD Configuration Utility. Details and the
rebuild procedure: `firmware/CYPD3175/README.md`.

## Open verification points

All marked `CHECK:` in the sources (`grep -rn CHECK Core/Src/app`):

| # | What | Where to verify |
|---|---|---|
| 1 | PBMs data layout and burst chunking | TPS25750 TRM, "Patch Bundle Burst Mode" |
| 2 | Burst target address (default 0x22 assumed; PBMs response reports the real one) | TPS25750 TRM |
| 3 | BQ34Z100 subclass IDs / offsets (Design Capacity, Series Cells, Mfg Info A) | BQ34Z100-R2 TRM data-flash table |
| 4 | Design capacity value (7800 mAh) vs final cell choice | pack BOM |
| 5 | CYPD3175 status-LED pin (placeholder `GPIO_PORT_1_PIN_4`) | schematic, once the CCG3PA section exists |

**Resolved since:** all IC I2C addresses now live in the single shared
header `system/defines.h` — TPS25750 settled at 0x20
(`TPS25750_PD_CONTROLLER_ADDR`), STPD01 at 0x05 (`ADD` pin grounded per
the schematic netlist, see `Charge_Management.md`).

## Maintenance notes

- **Flash budget:** the TPS bundle costs 32 KB of MCU flash. The UI and
  charge logic are now integrated in the build — re-measure with
  `arm-none-eabi-size` after significant changes (128 KB total).
- **CubeMX regeneration rewrites the Makefile** (it has no USER CODE
  sections): after every regeneration, re-check that all the application
  entries (`Core/Src/app/*.c`, `system/*.c`, `display/*.c`, `ui/*.c`)
  are still in `C_SOURCES`.
- The `main.c` hooks live inside USER CODE sections and survive
  regeneration.
