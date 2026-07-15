# USB-C2 PD configuration (EZ-PD Configuration Utility)

Values to set in the config table of the reference firmware. No code
involved: open the reference project's `.c`/`.cyacd` config in the
EZ-PD Configuration Utility, edit, save, flash.

These mirror what the Ducker UI already assumes (channel page "USB-C 2":
PDO steps 5/9/12/15/20 V, shared 3 A limit from the power stage).

## Source PDOs (port role: Source)

| # | Type  | Voltage | Current | Note |
|---|-------|---------|---------|------|
| 1 | Fixed | 5 V     | 3.0 A   | mandatory vSafe5V |
| 2 | Fixed | 9 V     | 3.0 A   | |
| 3 | Fixed | 12 V    | 3.0 A   | |
| 4 | Fixed | 15 V    | 3.0 A   | |
| 5 | Fixed | 20 V    | 3.0 A   | 60 W, max for a standard 3 A cable, no e-marker needed |

PPS: off for the first bring-up (the STPD01 chain can do it later if
ever wanted (20 mV steps) but keep the first firmware simple).

## Protections (starting points: review against the power stage)

| Parameter | Value | Rationale |
|---|---|---|
| OVP threshold | +20 % of contract voltage | SDK default ballpark |
| OCP threshold | 120 % of PDO current, ~10 ms debounce | above STPD01 ILIM so the buck limits first |
| OTP | on, sensor per reference design | |
| VConn | off | no active cables expected |

Design intent: the **STPD01 `ILIM` register remains the primary current
limiter** (it is what the UI sets); the CCG3PA OCP is the backstop, so
it must sit slightly above the STPD01 limit to avoid false trips.

## Dynamic PDO ceiling (UI "Max PD volt")

The UI lets the user cap the maximum negotiable voltage. Config-table
PDOs are static; the runtime cap needs one of:

1. **HPI** enabled in the firmware build, the STM32 masks PDOs over
   I2C at runtime (clean, matches the old STUSB4710 approach), or
2. renegotiation triggered by the CCG3PA itself on a GPIO from the
   STM32 (crude, limited), or
3. dropping the runtime cap and keeping a static PDO list (UI page
   becomes display-only for C2).

Decide together with the schematic; option 1 is the one the UI was
designed around.
