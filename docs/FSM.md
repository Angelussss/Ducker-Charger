# Power Bank FSM

## Table of Contents

- [Introduction](#introduction)
- [Architecture](#architecture)
  - [Layer Responsibilities](#layer-responsibilities)
  - [Data Flow](#data-flow)
- [Events](#events)
  - [Event List](#event-list)
  - [Event Queue](#event-queue)
  - [Event Producers](#event-producers)
- [States](#states)
  - [State Overview](#state-overview)
  - [SoC and Voltage Thresholds](#soc-and-voltage-thresholds)
  - [State Behaviors](#state-behaviors)
- [Transition Matrix](#transition-matrix)
  - [Overcharge Conditional](#overcharge-conditional)

---

## Introduction

This document describes the Finite State Machine (FSM) that governs the overall operating behavior of the Ducker Charger. The FSM determines which state the device is in at any given time — controlling which ports are active, what the display shows, and how the system reacts to hardware events such as low battery, fault conditions, or charger connection.

The FSM is implemented in `firmware/cubeMX/Core/Src/system/fsm.c` using a **hybrid vtable + transition matrix** approach:
- The **vtable** defines the behavior of each state through three handlers: `onEnter`, `onRun`, and `onExit`.
- The **transition matrix** encodes when to leave a state: a 2D lookup table indexed by `[current state][event]` returns the next state, keeping transition logic fully decoupled from state behavior.

---

## Architecture

### Layer Responsibilities

| Layer | Files | Role |
| :---- | :---- | :--- |
| Charge Management | `system/charge.h/.c` | Reads all sensors, drives all actuators (`enable/disable_*`), pushes events to the queue. Knows nothing about the FSM. |
| FSM | `system/fsm.h/.c` | Consumes events, determines state transitions, calls charge layer actuators in Enter/Exit handlers. Never pushes events. |
| Event Queue | `system/event.h/.c` | Ring buffer decoupling event producers from the FSM consumer. |
| Display / UI | `ui/`, `display/` | Reads charge layer getters read-only. Never writes to charge state. |
| Main Loop | `main.c` | Calls `readNCS()` each tick to refresh port status variables, drains one event from the queue, calls `PB_FSM_FireEvent()`, checks the inactivity timer, then calls `PB_FSM_Update()`. |

### Data Flow

```
charge.c  ──getters──►  fsm.c  ──getters──►  display / ui.c
  (data +               (logic)
  actuation)◄──enable/disable──┘
     │
     │  event_push()
     ▼
 event_queue  (ring buffer, 8 slots)
     │
     │  drained each main-loop tick
     ▼
 PB_FSM_FireEvent()  →  TransitionTable lookup  →  PB_FSM_RequestState()
                                                          │
                                                          ▼
                                                   PB_FSM_Update()  →  onExit / onEnter / onRun
```

The flow is **unidirectional**: the charge layer pushes events and the FSM consumes them. The FSM's `Run()` handlers call charge layer getters for display data and `enable/disable_*` actuators in `Enter`/`Exit` — they never call `event_push()`.

---

## Events

Events are the signals that drive FSM transitions. They are produced by the charge management layer, ISRs, and the main loop, then consumed by the FSM. Neither layer needs to know about the other's internals — the event queue is the only coupling point between them.

### Event List

| Event | Trigger |
| :---- | :------ |
| `EVT_CHARGER_CONNECTED` | Primary USB-C plug detected |
| `EVT_CHARGER_DISCONNECTED` | Primary USB-C removed |
| `EVT_SOC_SAFETY` | SoC drops below 15% |
| `EVT_SOC_LOWV` | SoC drops below 10% |
| `EVT_SOC_UNDERV` | `BATLOW` flag set or pack voltage below damage threshold |
| `EVT_SOC_OK` | SoC recovers above 15% |
| `EVT_SOC_OVCH` | BQ34Z100 `BATHI` flag set — overcharge detected |
| `EVT_FAULT_OT` | `OTC`/`OTD` flags or STPD01 OTP — recoverable overtemperature |
| `EVT_FAULT_CRITICAL` | BMS dead / I2C loss on fuel gauge — unrecoverable |
| `EVT_FAULT_OCC` | Overcurrent on charging input (INA3221) |
| `EVT_ERROR` | General recoverable error (I2C fault, sensor read fail) |
| `EVT_ERROR_CLEAR` | Error condition resolved |
| `EVT_INACTIVITY` | No user input for `INACTIVITY_TIMEOUT_MS` (30 s) |
| `EVT_BUTTON_SHORT` | Encoder button short press |
| `EVT_BUTTON_LONG` | Encoder button long press |
| `EVT_MANUAL_ENTER` | UI selects LAB mode |
| `EVT_MANUAL_EXIT` | UI exits LAB mode |

### Event Queue

Events are stored in an 8-slot ring buffer defined in `system/event.h/.c`. The queue exposes three functions:

```c
void event_push(Event_t event);  // called by charge layer and ISRs
bool event_pop(Event_t *out);    // called by main loop each tick; returns false if empty
bool event_pending(void);        // returns true if at least one event is waiting
```

If the queue is full, `event_push()` silently drops the incoming event. The queue is drained every main loop iteration before `PB_FSM_Update()` is called.

### Event Producers

| Source | Events pushed |
| :----- | :------------ |
| `primaryUSBC_ConnectionINT()` | `EVT_CHARGER_CONNECTED`, `EVT_CHARGER_DISCONNECTED` |
| `readSensors()` | `EVT_SOC_SAFETY`, `EVT_SOC_LOWV`, `EVT_SOC_UNDERV`, `EVT_SOC_OK`, `EVT_SOC_OVCH`, `EVT_FAULT_OT`, `EVT_FAULT_CRITICAL` |
| `readINA()` | `EVT_FAULT_OCC` |
| `stpd01_PowerStateINT()` | `EVT_FAULT_CRITICAL` (OVP/SCP/ILIM), `EVT_FAULT_OT` (OTP) |
| `secondaryUSBC_ConnectionINT()` | `EVT_ERROR` |
| Any `HAL_I2C_Mem_Read` → `HAL_ERROR` | `EVT_ERROR` |
| Encoder button ISR | `EVT_BUTTON_SHORT`, `EVT_BUTTON_LONG` |
| Main loop inactivity timer | `EVT_INACTIVITY` |
| UI menu | `EVT_MANUAL_ENTER`, `EVT_MANUAL_EXIT` |

---

## States

### State Overview

| State | SoC Range | Output Ports | Display |
| :---- | :-------- | :----------- | :------ |
| `IDLE` | 100% – 15% | All active | ON |
| `SAFETY_LOCK` | 15% – 10% | All detached | ON |
| `LOW_V` | 10% – UNDERV threshold | All detached | OFF |
| `EMERGENCY` | Below UNDERV threshold | All detached | OFF |
| `CHARGING` | Any | All detached | ON |
| `MANUAL` | Any | C2 = LAB mode, A1/A2 active | ON |
| `SLEEP` | Any | Unchanged from previous state | OFF |
| `DEEP_SLEEP` | Any | All detached | OFF |
| `ERROR` | Any | Faulted subsystem only detached | ON |

*Note:* There is no `BOOT` state — peripheral initialization runs before `PB_FSM_Init()` is called.

*Note:* There is no hard port "off" — all port disable operations use the detach path (controller stays powered, output disabled).

### SoC and Voltage Thresholds

A hybrid approach is used: **SoC** (from BQ34Z100 coulomb counting) for the upper thresholds because it is the user-facing metric, and **pack voltage / `BATLOW` flag** for the undervoltage threshold because it directly reflects hardware protection limits.

```c
#define SOC_SAFETY_THRESHOLD    15.0f    // % — IDLE → SAFETY_LOCK
#define SOC_LOWV_THRESHOLD      10.0f    // % — SAFETY_LOCK → LOW_V
#define UNDERV_VOLTAGE_MV     3000.0f    // mV/cell — LOW_V → EMERGENCY
#define SOC_OK_THRESHOLD        15.0f    // % — recovery back to IDLE
#define INACTIVITY_TIMEOUT_MS   30000    // ms — IDLE/CHARGING → SLEEP
```

The `BATLOW` flag from the BQ34Z100 (`fuelGaugeSensors.flags.BATLOW`) is used as the primary UNDERV trigger — the gauge raises it when the configured minimum cell voltage is reached.

### State Behaviors

Each state implements three handlers — `onEnter`, `onRun`, `onExit` — defined in `system/fsm.c`.

#### DEEP_SLEEP

`onEnter` is the unusual one — it blocks. After detaching all ports and turning the display off, it calls `HAL_SuspendTick()` (otherwise SysTick would immediately wake the MCU) then `HAL_PWR_EnterSTOPMode()`, which puts the Cortex-M4 into STOP mode: PLL and most clocks are gated, RAM is retained, current drops from ~20 mA to ~20 µA. The processor halts at that line until an EXTI fires (button press or charger detection). The ISR runs first — setting the button flag or pushing a charger event — then `HAL_PWR_EnterSTOPMode` returns. At that point the MCU is running on the internal HSI oscillator because the PLL was killed by STOP, so `SystemClock_Config()` is called immediately after to restore the correct frequency, followed by `HAL_ResumeTick()` to restart `HAL_GetTick()`.

`onRun` is a no-op: by the time it is called, `onEnter` has already returned post-wakeup and the ISR has set the relevant flag. The main loop will pop the event and fire the transition on the next iteration.

`onExit` turns the display back on.

#### SLEEP

Lighter than DEEP_SLEEP — the MCU stays fully running, only the display turns off. Ports are intentionally **not touched** in `onEnter` because SLEEP can be reached from any state (IDLE, CHARGING, LOW_V…) and each of those already left ports in the correct state. Touching them here would break CHARGING's port state, for example.

`onRun` runs the full poll — `readCS()`, `readSensors()`, `readINA()`. `readCS()` is required so that `PD_IRQ` is serviced while sleeping: without it, `EVT_CHARGER_CONNECTED` could never fire from SLEEP and the SLEEP → CHARGING transition would be unreachable. `readINA()` is included because ports are not touched on SLEEP entry — if the device fell asleep from IDLE, USB-A1/A2 are still active and need overcurrent monitoring.

`onExit` turns the display back on when waking.

#### IDLE

The "everything is fine" state. `onEnter` re-enables both USB-A ports and the display. `enable_USBC2()` and `enable_STPD01()` are **not** called here because the secondary USB-C is managed through its own negotiation flow in `readCS()` and `secondaryUSBC_ConnectionINT()` — the FSM does not reach in and enable it directly.

`onRun` does a full sensor poll each tick (`readCS()`, `readSensors()`, `readINA()`) so the charge layer can detect threshold crossings and push events.

`onExit` is empty — the next state's `onEnter` handles its own setup.

#### CHARGING

When the device is being charged it gives all available power to the battery — no outputs. `onEnter` disables everything. `onRun` continues the full sensor poll so fault events (`EVT_FAULT_OCC`, `EVT_SOC_OVCH`) are detected while charging.

`onExit` re-enables USB-A1 and USB-A2 when leaving back to IDLE, so the user can immediately use those ports again after disconnecting the charger. USBC2 and STPD01 are not re-enabled here — those go through the CYPD3175 negotiation flow.

#### SAFETY_LOCK

Battery is low (or overcharged) — detach everything and wait. The display stays on so the user can see the SoC. `onRun` keeps polling so `EVT_SOC_LOWV` (further drain) or `EVT_SOC_OK` (charger brought it back) can be detected. `EVT_FAULT_OT` and `EVT_ERROR` also escalate to `STATE_ERROR` from here — a fresh overtemperature or sensor/bus fault while SoC is critically low is no longer silently dropped.

`onExit` is empty — the destination state's `onEnter` handles re-enabling whatever is appropriate.

*Note:* `onRun` does not currently call `readINA()`, unlike every other actively-monitored state — an INA3221 overcurrent condition is not detected while locked in this state.

#### LOW_V

`onEnter` disables all ports and turns the display off. The disable calls are redundant on the normal path (arriving from SAFETY_LOCK, ports are already off) but necessary for paths coming from SLEEP, IDLE, or ERROR where ports may still be active. At this SoC level every milliamp counts and the display is not worth the draw.

`onRun` keeps sensors alive so `EVT_SOC_UNDERV` and `EVT_CHARGER_CONNECTED` can still be detected.

*Note:* like `SAFETY_LOCK`, `onRun` does not currently call `readINA()` — an INA3221 overcurrent condition is not detected while in this state either.

#### EMERGENCY

Terminal state. Everything detaches, the MCU keeps running but does nothing further. `onRun` is empty — there are no outgoing transitions in the matrix so `PB_FSM_FireEvent()` will never trigger a state change from here. `onExit` is never called in practice. The display message is left for the display layer to handle.

#### MANUAL (LAB mode)

`onEnter` raises `C2_LAB_EN`, which puts the secondary USB-C port into lab mode — CYPD3175 PD negotiation is bypassed and the STPD01 is driven directly by the user via the encoder. The actual STPD01 configuration (`setupSTPD01(voltage, current)`) and `enable_STPD01()` happen in the UI layer's encoder loop, not here.

`onRun` runs the full poll — `readCS()`, `readSensors()`, `readINA()`. This is necessary because MANUAL has outgoing transitions on SoC thresholds, overtemperature, and critical faults — none of which could fire if sensors were not polled. `readCS()` also keeps `CHRG_OK` edge detection alive.

`onExit` cleans up: disables STPD01 and USB-C2, then pulls `C2_LAB_EN` low to return the port to normal PD-controlled operation.

#### ERROR

The only state where `onEnter` deliberately does nothing to ports. By the time `EVT_ERROR` reaches the FSM, `charge.c` has already shut down the affected subsystem (e.g. `stpd01_PowerStateINT()` called `disable_USBC2()` and `disable_STPD01()` before pushing the event). A blanket disable in `onEnter` would incorrectly shut down ports that are still healthy.

The specific fault cause is not carried in the event — it is left in the charge layer structs (`stpd01_status`, `ina3221_sensors`, `fuelGaugeSensors.flags`, etc.). The display layer retrieves the cause by calling the relevant charge layer getters and rendering whichever flag is still set. This keeps error detail out of the FSM entirely.

`onRun` runs the full poll — `readCS()`, `readSensors()`, `readINA()`. `readCS()` is required so that `PD_IRQ` is serviced: without it the ERROR → CHARGING transition on `EVT_CHARGER_CONNECTED` would never trigger. `readINA()` is included because `onEnter` leaves ports untouched, so channels that are still active must continue to be monitored.

**Recovery:** `EVT_ERROR_CLEAR` is not currently auto-generated by any producer — no automatic recovery path is wired. The only exit from `STATE_ERROR` in normal operation is `EVT_BUTTON_SHORT`: the user acknowledges the error on the display and presses the encoder button to return to `STATE_IDLE`. `EVT_ERROR_CLEAR` is reserved for future automatic recovery (e.g. transient I2C fault clears, CHRG_OK rising edge) and is already handled in the transition matrix for when it is eventually wired up.

---

## Transition Matrix

`—` = no transition. `*` = conditional (see [Overcharge Conditional](#overcharge-conditional)).

```
             CHGR  CHGR  SOC_  SOC_  SOC_   SOC_ SOC_  FLT_  FLT_   FLT_  ERR  ERR_  INACT  BTN_  BTN_  MAN   MAN
             CONN  DISC  SFTY  LOWV  UNDERV OK   OVCH  OT    CRIT   OCC   (gn) CLR   IVITY  SHRT  LONG  ENTR  EXIT
DEEP_SLEEP   IDLE  —     —     —     —      —    —     —     —      —     —    —     —      IDLE  —     —     —
SLEEP        CHGR  —     SFTY  LOWV  EMRG   —    SFTY  ERR   EMRG   —     ERR  —     —      IDLE  DEEP  —     —
IDLE         CHGR  —     SFTY  LOWV  EMRG   —    SFTY  ERR   EMRG   —     ERR  —     SLEEP  —     DEEP  MAN   —
CHARGING     —     IDLE  SFTY  LOWV  EMRG   —    SFTY  ERR   EMRG   SFTY  ERR  —     SLEEP  —     —     MAN   —
MANUAL       —     —     SFTY  LOWV  EMRG   —    SFTY  ERR   EMRG   —     ERR  —     —      —     —     —     IDLE
SAFETY_LOCK  CHGR* —     —     LOWV  EMRG   IDLE —     ERR   EMRG   —     ERR  —     —      —     —     —     —
LOW_V        CHGR  —     —     —     EMRG   IDLE —     ERR   EMRG   —     ERR  —     SLEEP  —     —     —     —
ERROR        CHGR  —     SFTY  LOWV  EMRG   —    —     —     EMRG   —     —    IDLE  —      IDLE  —     —     —
EMERGENCY    —     —     —     —     —      —    —     —     —      —     —    —     —      —     —     —     —
```

### Overcharge Conditional

`SAFETY_LOCK → CHARGING` on `EVT_CHARGER_CONNECTED` is marked `*` because it is conditional: it is only allowed when `SAFETY_LOCK` was entered due to low SoC (`EVT_SOC_SAFETY`). If `SAFETY_LOCK` was entered due to overcharge (`EVT_SOC_OVCH`), connecting a charger must not restart charging — the pack is already too full.

A pure transition matrix cannot express this distinction (one cell, one value), so the FSM context carries an `ovchargeBlock` flag:

| Event | Effect on `ovchargeBlock` |
| :---- | :------------------------ |
| `EVT_SOC_OVCH` | Set to `true` |
| `EVT_SOC_OK` | Cleared to `false` |

`PB_FSM_FireEvent()` checks this flag before performing the matrix lookup: if the current state is `SAFETY_LOCK`, the incoming event is `EVT_CHARGER_CONNECTED`, and `ovchargeBlock` is `true`, the event is discarded and no transition occurs. Once `EVT_SOC_OK` fires (BATHI clears), the flag is cleared and a subsequent charger connection will transition to `CHARGING` normally.
