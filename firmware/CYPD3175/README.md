# Ducker-Charger — CYPD3175 (EZ-PD CCG3PA) firmware customisation

Custom bits to graft onto the **EZ-PD CCGx Power SDK** reference firmware
for the USB-C2 PD controller (CYPD3175-24LQXQ, the STUSB4710
replacement). Reviewed here first; moves into the git repo only after
approval.

## Contents

```
src/ducker_led.c|h      status-LED driver (GPIO + SDK software timer)
src/INTEGRATION.md      where to hook it inside the reference project
config/c2_pd_config.md  config-table values (PDOs, protections) for the
                        EZ-PD Configuration Utility — no code involved
```

## Status

- **BUILT AND LINKED** (2026-07-04) against CCGx Power SDK 3.5 with
  PSoC Creator 4.4 in the win11 VM, zero warnings under `-Werror`.
  Flash: 89.6% (noboot) vs 88.7% baseline — the LED costs ~600 B.
  Built hex in `build/`; the patched reference files (app.c, main.c,
  both .cyprj with the ducker_led source entries) are mirrored in
  `patched-reference/` and live in the VM under `C:\ducker\fw`.
- Two of the three original CHECKs are resolved against SDK 3.5
  (timer ID = USER_TIMERS_START_ID+4, GPIO signatures verified).
- **One CHECK left**: the LED pin is a placeholder
  (`GPIO_PORT_1_PIN_4`) until the CCG3PA section of the schematic
  exists.
- The big open item is the power stage: `pa_direct_fb` expects the
  CCG3PA to drive the regulator via direct feedback. Copying the CCG3PA
  reference topology in the new PCB keeps firmware work at
  "config + LED"; keeping the STPD01 I2C chain means real psource-layer
  work.

## Toolchain

| Piece | Purpose |
|---|---|
| EZ-PD CCGx Power SDK | stack + reference projects (`CYPD3175-24LQXQ_pa_direct_fb`, `CYPD3171-24LQXQ_pb`) |
| PSoC Creator 4.x (Windows) | build |
| EZ-PD Configuration Utility | config table edit + bootloader flashing |
| MiniProg3/4 (SWD) | first flash on a blank chip |

Getting started: Infineon AN218179 "Getting Started with EZ-PD CCG3PA".

## LED behaviour (as implemented)

| State | Pattern |
|---|---|
| No cable | off |
| Attached / negotiating | slow blink (2 Hz) |
| Contract established | solid on |
| OVP/OCP fault | fast blink (8 Hz) |
