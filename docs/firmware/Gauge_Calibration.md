# Fuel-Gauge Calibration (on-device wizard)

The BQ34Z100-R2 needs a one-time electrical calibration before its
readings can be trusted: shunt, voltage divider and layout all carry
tolerances that only a comparison against an external reference can
remove. This project does it **entirely on the device**: SETTINGS →
**Calibration**: with a multimeter as the only external tool. No
bqStudio, no EV2400.

Code: backend `Core/Src/app/calibration.c`, UI `Screen_Cal_*` in
`Core/Src/ui/screens.c` (page `UI_SCREEN_CALPG`).

## Why this is not "just another config"

Boot-camp (`initialization.c`) and provisioning (`provisioning.c`)
write **constants known at compile time**. Calibration values are
**measured, per-board**: they come out of comparing the gauge's own
reading with a multimeter. Once measured they live in the gauge's own
data flash: **non-volatile, no further action needed**: the wizard is
the whole "first pack attach" procedure. Copying the values into the
`provisioning.c` patch list is an *optional* backup, useful only if
the gauge chip is ever replaced or its data flash reset (the review
step shows the values for that purpose).

## Method: the ratio method

The TRM does not document the raw-ADC registers bqStudio uses, but all
three measurements are **linear** in their DF parameter, so a
correction needs only the *reported* value:

```
Voltage Divider' = Voltage Divider x V_multimeter / Voltage()
CC Gain'         = CC Gain         x I_multimeter / Current()
CC Delta'        = CC Gain' x 1190738          (fixed TRM ratio)
Ext Temp Offset' += (T_true - Temperature())   (0.1 C units)
```

Offsets cannot be ratio'd (they are additive at zero signal): the
wizard uses the gauge's **internal routines**: Control() `CC_OFFSET`
(0x000A) + `CC_OFFSET_SAVE` (0x000B), then `BOARD_OFFSET` (0x0009),
polling the `CCA`/`BCA` bits in `CONTROL_STATUS` until they clear.

DF target: subclass **104 (Calibration/Data)**: CC Gain (F4 @0),
CC Delta (F4 @4), CC Offset (I2 @8), Board Offset (I1 @10), Int/Ext
Temp Offset (I1 @11/12), Voltage Divider (U2 @14). F4 is the TI/Xemics
float format (codec in `calibration.c`). Same block protocol and
default unseal keys as `provisioning.c`.

## The wizard, step by step

| Step | What you do | What the firmware does |
|---|---|---|
| 1/9 GAUGE CHECK | press CHECK | `DEVICE_TYPE` must answer 0x0100, unseal (TI default keys). Wrong device → refuses everything. |
| 2/9 ZERO OFFSET | get the current through the shunt to **true ~0 mA** (best: MCU powered from the SWD connector, every load off), press START | runs `CC_OFFSET` + `BOARD_OFFSET` internally (~20 s budget), saves both |
| 3/9 VOLTAGE | multimeter on the pack terminals, dial the reading in (10 mV steps), press APPLY | rewrites `Voltage Divider` by the ratio |
| 4/9 CURRENT | attach any steady load through the multimeter (10 A range) on USB-A1, dial the reading, press CAPTURE; **remove the load**, press SAVE | captures the ratio under load, writes `CC Gain`/`CC Delta` at rest |
| 5/9 TEMPERATURE | dial the ambient temperature, press APPLY | adjusts `Ext Temp Offset` (rejects diffs > 12 C; that is a wiring problem, not an offset) |
| 6/9 PACK CONFIG | press APPLY | writes VOLTSEL (Pack Configuration MSB bit 3, external divider, mandatory for 4S), Number of Series Cells (4), Design Capacity (7800 mAh). Read-modify-write, already-correct bytes skipped. |
| 7/9 ENABLE IT | press ENABLE | Control() `IT_ENABLE` (0x0021). **One-way** on the gauge ([QEN] cannot be cleared); run it only after the electrical calibration. |
| 8/9 LEARNING | run the learning cycle (full charge, rest 2 h, discharge C/5 to term voltage, rest 5 h) | passive live monitor: Update Status (0x04 → 0x05 Qmax → 0x06 Qmax+Ra), QEN/VOK, FC/REST flags, average current, and a next-action hint |
| 9/9 REVIEW | nothing, done | DF readout (gain, divider, offsets, Update Status). Values are permanent in the gauge; optionally note them down as a backup for a future gauge replacement. |

Guards baked in:

- Every write happens on an explicit APPLY/SAVE, browsing never
  touches the flash.
- Entered values implying a correction > **±15%** are rejected
  (`CAL_RANGE`): that is a hardware bug or a typo, not calibration.
- The current-step write is deferred until after the load is removed,
  so the DF write happens with the pack at rest.

## MCU self-draw trap (offset step)

While the firmware runs, the MCU's own supply current (~tens of mA)
flows through the battery shunt: an offset calibrated in that state
would hide real discharge current forever. Hence the SWD-power
instruction. If external power is impossible, skip the offset step and
keep the factory offsets, a wrong offset is worse than none.

## Testing without hardware

The emulator models the calibration path end to end
(`firmware/emulator/src/dev_bq34z100.c`): the DF calibration block is
live (reported V/I/T are distorted by the stored divider/gain/offset,
seeded with deliberate errors at attach), and the internal offset
routines run with realistic `CCA`/`BCA` timing. A correct wizard run
visibly converges the readings; every DF write shows in the log as
`data-flash write: subclass 104`.

Note for scripted runs: the UI's double-click arbiter holds single
clicks for 400 ms, leave ≥600 ms between a press and the next
rotation or the rotation lands first and moves the selection.

## What calibration does NOT cover

**ChemID selection**: the chemistry profile is a proprietary data block
distributed only through bqStudio's chemistry database, the wizard
cannot conjure it. Program it once via bqStudio (or accept the factory
default profile with degraded SoC accuracy); once written, the chem DF
block can be exported and added to the `provisioning.c` patch list like
everything else. The learning cycle itself needs hours and an external
load, the wizard monitors it (step 8/9) but cannot run it. See the
"golden image" section of
[Boot_and_Provisioning.md](Boot_and_Provisioning.md). Order matters:

```
electrical calibration -> static params -> ChemID -> IT enable
-> learning cycle -> export golden image into provisioning.c
```
