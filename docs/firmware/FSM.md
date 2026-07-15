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
  - [Overcharge (`ovchargeBlock`)](#overcharge-ovchargeblock)

---

## Introduction

This document describes the Finite State Machine (FSM) that governs the overall operating behavior of the Ducker Charger. The FSM determines which state the device is in at any given time, controlling which ports are active, what the display shows, and how the system reacts to hardware events such as low battery, fault conditions, or charger connection.

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
| Main Loop | `main.c` | Calls `readNCS()` each tick to refresh port status variables, drains one event from the queue, calls `PB_FSM_FireEvent()`, checks the inactivity and deep-sleep-hold timers, calls `PB_FSM_Update()`, then `Telemetry_Poll()` (here and not in `UI_Tick()`, so uptime/energy/port histories keep running while the screen is dark in SLEEP), and finally `UI_Tick()` unless the FSM has the backlight off. |

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

The flow is **unidirectional**: the charge layer pushes events and the FSM consumes them. The FSM's `Run()` handlers call charge layer getters for display data and `enable/disable_*` actuators in `Enter`/`Exit`; they never call `event_push()`.

---

## Events

Events are the signals that drive FSM transitions. They are produced by the charge management layer, ISRs, and the main loop, then consumed by the FSM. Neither layer needs to know about the other's internals, the event queue is the only coupling point between them.

### Event List

| Event | Trigger |
| :---- | :------ |
| `EVT_CHARGER_CONNECTED` | Primary USB-C plug detected |
| `EVT_CHARGER_DISCONNECTED` | Primary USB-C removed |
| `EVT_SOC_SAFETY` | SoC drops below 15% |
| `EVT_SOC_LOWV` | SoC drops below 10% |
| `EVT_SOC_UNDERV` | `BATLOW` flag set or pack voltage below damage threshold |
| `EVT_SOC_OK` | SoC recovers above 15% |
| `EVT_SOC_OVCH` | BQ34Z100 `BATHI` flag set, overcharge detected |
| `EVT_FAULT_OT` | `OTC`/`OTD` flags or STPD01 OTP, recoverable overtemperature |
| `EVT_FAULT_CRITICAL` | BMS dead / I2C loss on fuel gauge, unrecoverable |
| `EVT_FAULT_OCC` | Overcurrent on charging input (INA3221) |
| `EVT_ERROR` | General recoverable error (I2C fault, sensor read fail) |
| `EVT_ERROR_CLEAR` | Error condition resolved |
| `EVT_INACTIVITY` | No user input for `INACTIVITY_TIMEOUT_MS` (2 min) |
| `EVT_BUTTON_SHORT` | Encoder button short press |
| `EVT_BUTTON_LONG` | Encoder button held ≥ 3 s then released, or the UI DISPLAY page's "Shutdown" row confirmed |
| `EVT_MANUAL_ENTER` | UI enables the Lab output channel (OUTPUT page) |
| `EVT_MANUAL_EXIT` | UI disables the Lab output channel |
| `EVT_LOCK` | UI "Lock all": user-requested SAFETY_LOCK (sets `userLock`) |
| `EVT_UNLOCK` | UI "Lock all" off, honored only if the lock was user-initiated |

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
| UI DISPLAY page "Shutdown" (confirmed) | `EVT_BUTTON_LONG`, same primitive as the physical 3 s hold, subject to the same transition-table gate |
| Main loop inactivity timer | `EVT_INACTIVITY` |
| UI OUTPUT page (Lab channel enable/disable) | `EVT_MANUAL_ENTER`, `EVT_MANUAL_EXIT` |
| UI SETTINGS "Lock all" | `EVT_LOCK`, `EVT_UNLOCK` |
| UI FAULT screen OK button | `EVT_BUTTON_SHORT` (ERROR acknowledge) |
| `Idle_Enter` re-validation | `EVT_CHARGER_CONNECTED`, `EVT_SOC_LOWV`, `EVT_SOC_SAFETY` (edge-detected producers cannot re-fire for conditions that persisted across a protection state) |

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

*Note:* There is no `BOOT` state, peripheral initialization runs before `PB_FSM_Init()` is called.

*Note:* There is no hard port "off", all port disable operations use the detach path (controller stays powered, output disabled).

*Note:* "All detached" includes the C1 OTG source path: protection states (`SAFETY_LOCK`, `LOW_V`, `EMERGENCY`, `DEEP_SLEEP`) also call `disable_OTG()`, dropping `EN_OTG` (PB15). Per the BQ25713 TRM, OTG mode requires that pin HIGH **and** the I2C enable bit the TPS25750 sets (an AND condition) so the MCU holding it low is a real, independent kill switch on C1 ever discharging the pack, even with a sink device still plugged in. `CHARGING` does not need the call: C1 cannot be sink and source on the same connector at once.

### SoC and Voltage Thresholds

A hybrid approach is used: **SoC** (from BQ34Z100 coulomb counting) for the upper thresholds because it is the user-facing metric, and **pack voltage / `BATLOW` flag** for the undervoltage threshold because it directly reflects hardware protection limits.

```c
#define SOC_SAFETY_THRESHOLD    15.0f    // %, IDLE → SAFETY_LOCK
#define SOC_LOWV_THRESHOLD      10.0f    // %, SAFETY_LOCK → LOW_V
#define UNDERV_VOLTAGE_MV     3000.0f    // mV/cell, LOW_V → EMERGENCY
#define SOC_OK_THRESHOLD        15.0f    // %, recovery back to IDLE
#define INACTIVITY_TIMEOUT_MS  120000    // ms, IDLE/CHARGING/LOW_V → SLEEP
```

`INACTIVITY_TIMEOUT_MS` deliberately matches the UI DISPLAY page's default
"Auto sleep" setting (2 min) so the FSM's own inactivity sleep does not
silently pre-empt the user-facing option.

The `BATLOW` flag from the BQ34Z100 (`fuelGaugeSensors.flags.BATLOW`) is used as the primary UNDERV trigger, the gauge raises it when the configured minimum cell voltage is reached.

### State Behaviors

Each state implements three handlers (`onEnter`, `onRun`, `onExit`) defined in `system/fsm.c`.

#### DEEP_SLEEP

`onEnter` is the unusual one; it blocks. After detaching all ports (including the C1 OTG source path via `disable_OTG()`) and turning the display off, it calls `HAL_SuspendTick()` (otherwise SysTick would immediately wake the MCU) then `HAL_PWR_EnterSTOPMode()`, which puts the Cortex-M4 into STOP mode: PLL and most clocks are gated, RAM is retained, current drops from ~20 mA to ~20 µA. The processor halts at that line until an EXTI fires. **The encoder button (EXTI0, both edges) is the only configured wake source**, no other pin has an EXTI handler, so plugging in a charger cannot, by itself, wake the MCU. When the button edge arrives, `HAL_PWR_EnterSTOPMode` returns on the internal HSI oscillator (the PLL was killed by STOP), so `SystemClock_Config()` restores the correct frequency, followed by `HAL_ResumeTick()`.

**Wake gate:** a button press alone is not honored. `onEnter` loops: after each STOP-mode exit it reads `HP_CHRG_OK` (PB13, the BQ25713 valid-adapter output) and, if no charger is present, re-enters STOP immediately, screen and ports never come back, no event fires, externally indistinguishable from "nothing happened". Only a press *while a charger is attached* breaks the loop; `onEnter` then pushes `EVT_BUTTON_SHORT` itself (nothing else would, the FSM would stay trapped otherwise), driving `DEEP_SLEEP → IDLE`, and `Idle_Enter`'s contract re-validation immediately converges to CHARGING. Once awake, unplugging the charger does **not** return the device to DEEP_SLEEP; it is fully on, and only the explicit shutdown paths (3 s hold or the UI Shutdown row) put it back.

`onRun` is a no-op: by the time it is called, `onEnter` has already returned post-wakeup and pushed the event. The main loop will pop it and fire the transition on the next iteration.

`onExit` turns the display back on.

#### SLEEP

Lighter than DEEP_SLEEP, the MCU stays fully running, only the display turns off. Ports are intentionally **not touched** in `onEnter` because SLEEP can be reached from any state (IDLE, CHARGING, LOW_V…) and each of those already left ports in the correct state. Touching them here would break CHARGING's port state, for example.

`onRun` runs the full poll, `readCS()`, `readSensors()`, `readINA()`. `readCS()` is required so that `PD_IRQ` is serviced while sleeping: without it, `EVT_CHARGER_CONNECTED` could never fire from SLEEP and the SLEEP → CHARGING transition would be unreachable. `readINA()` is included because ports are not touched on SLEEP entry, if the device fell asleep from IDLE, USB-A1/A2 are still active and need overcurrent monitoring.

`onExit` turns the display back on when waking.

#### IDLE

The "everything is fine" state. `onEnter` re-enables both USB-A ports and the display. `enable_USBC2()` and `enable_STPD01()` are **not** called here because the secondary USB-C is managed through its own negotiation flow in `readCS()` and `secondaryUSBC_ConnectionINT()`, the FSM does not reach in and enable it directly.

`onRun` does a full sensor poll each tick (`readCS()`, `readSensors()`, `readINA()`) so the charge layer can detect threshold crossings and push events.

`onExit` is empty, the next state's `onEnter` handles its own setup.

#### CHARGING

When the device is being charged it gives all available power to the battery, no outputs. `onEnter` disables everything. `onRun` continues the full sensor poll so fault events (`EVT_FAULT_OCC`, `EVT_SOC_OVCH`) are detected while charging.

`onExit` deliberately does **not** re-enable anything: the destination state decides. `Idle_Enter` re-enables USB-A1/A2 (normal unplug path); CHARGING → SLEEP on inactivity keeps them off, charging continues in SLEEP with the same no-outputs policy as CHARGING itself. USBC2 and STPD01 always go through the CYPD3175 negotiation flow.

#### SAFETY_LOCK

Battery is low, detach everything and wait, **or** the user asked for it: the SETTINGS "Lock all" row pushes `EVT_LOCK`, which lands here with the `userLock` flag set, and `EVT_UNLOCK` (same row) releases it back to IDLE. The unlock is refused while `userLock` is clear, so a low-SoC lock cannot be dismissed from the menu; leaving SAFETY_LOCK by any path clears `userLock`. The display stays on so the user can see the SoC. `onRun` keeps polling so `EVT_SOC_LOWV` (further drain) or `EVT_SOC_OK` (charger brought it back) can be detected. `EVT_FAULT_OT` and `EVT_ERROR` also escalate to `STATE_ERROR` from here. (Overcharge no longer routes here, see [Overcharge](#overcharge-ovchargeblock).)

`onExit` is empty, the destination state's `onEnter` handles re-enabling whatever is appropriate.

*Note:* `onRun` does not currently call `readINA()`, unlike every other actively-monitored state, an INA3221 overcurrent condition is not detected while locked in this state.

#### LOW_V

`onEnter` disables all ports and turns the display off. The disable calls are redundant on the normal path (arriving from SAFETY_LOCK, ports are already off) but necessary for paths coming from SLEEP, IDLE, or ERROR where ports may still be active. At this SoC level every milliamp counts and the display is not worth the draw.

`onRun` keeps sensors alive so `EVT_SOC_UNDERV` and `EVT_CHARGER_CONNECTED` can still be detected.

*Note:* like `SAFETY_LOCK`, `onRun` does not currently call `readINA()`, an INA3221 overcurrent condition is not detected while in this state either.

#### EMERGENCY

Terminal state. Everything detaches, the MCU keeps running but does nothing further. `onRun` is empty; there are no outgoing transitions in the matrix so `PB_FSM_FireEvent()` will never trigger a state change from here. `onExit` is never called in practice. The display message is left for the display layer to handle.

#### MANUAL (LAB mode)

`onEnter` raises `C2_LAB_EN`, which puts the secondary USB-C port into lab mode, CYPD3175 PD negotiation is bypassed and the STPD01 is driven directly by the user via the encoder. The state is entered/left through the UI OUTPUT page: enabling the Lab channel pushes `EVT_MANUAL_ENTER` and programs the converter through the charge layer (`setupSTPD01(voltage, current)` + `enable_STPD01()`, the same calls run again when the user confirms a new voltage/current value); disabling it pushes `EVT_MANUAL_EXIT`.

`onRun` runs the full poll, `readCS()`, `readSensors()`, `readINA()`. This is necessary because MANUAL has outgoing transitions on SoC thresholds, overtemperature, and critical faults, none of which could fire if sensors were not polled. `readCS()` also keeps `CHRG_OK` edge detection alive.

`onExit` cleans up: disables STPD01 and USB-C2, then pulls `C2_LAB_EN` low to return the port to normal PD-controlled operation.

#### ERROR

The only state where `onEnter` deliberately does nothing to ports. By the time `EVT_ERROR` reaches the FSM, `charge.c` has already shut down the affected subsystem (e.g. `stpd01_PowerStateINT()` called `disable_USBC2()` and `disable_STPD01()` before pushing the event). A blanket disable in `onEnter` would incorrectly shut down ports that are still healthy.

The specific fault cause is not carried in the event; it is left in the charge layer structs (`stpd01_status`, `ina3221_sensors`, `fuelGaugeSensors.flags`, etc.). The display layer retrieves the cause by calling the relevant charge layer getters and rendering whichever flag is still set. This keeps error detail out of the FSM entirely.

`onRun` runs the full poll, `readCS()`, `readSensors()`, `readINA()`. `readCS()` is required so that `PD_IRQ` is serviced: without it the ERROR → CHARGING transition on `EVT_CHARGER_CONNECTED` would never trigger. `readINA()` is included because `onEnter` leaves ports untouched, so channels that are still active must continue to be monitored.

**Recovery:** the UI FAULT screen (`UI_SCREEN_FAULT`) opens automatically whenever the FSM is in ERROR or EMERGENCY, names the cause by reading the charge-layer getters (`getSTPD01_Status()`, `getFuelGaugeData()`, `getINA3221_Sensors()`, `getCYPD_LastFaultEvent()`), and, for ERROR only, its OK button pushes `EVT_BUTTON_SHORT`, which is the producer for the ERROR → IDLE acknowledge transition (EMERGENCY shows no button: it is terminal). `EVT_ERROR_CLEAR` is still not auto-generated by any producer; it is reserved for future automatic recovery (e.g. transient I2C fault clears) and is already handled in the transition matrix for when it is eventually wired up.

---

## Transition Matrix

`—` = no transition.

```
             CHGR  CHGR  SOC_  SOC_  SOC_   SOC_ SOC_  FLT_  FLT_   FLT_  ERR  ERR_  INACT  BTN_  BTN_  MAN   MAN
             CONN  DISC  SFTY  LOWV  UNDERV OK   OVCH  OT    CRIT   OCC   (gn) CLR   IVITY  SHRT  LONG  ENTR  EXIT
DEEP_SLEEP   IDLE  —     —     —     —      —    —     —     —      —     —    —     —      IDLE  —     —     —
SLEEP        CHGR  —     SFTY  LOWV  EMRG   —    —     ERR   EMRG   —     ERR  —     —      IDLE  DEEP  —     —
IDLE         CHGR  —     SFTY  LOWV  EMRG   —    —     ERR   EMRG   —     ERR  —     SLEEP  —     DEEP  MAN   —
CHARGING     —     IDLE  SFTY  LOWV  EMRG   —    —     ERR   EMRG   SFTY  ERR  —     SLEEP  —     —     MAN   —
MANUAL       —     —     SFTY  LOWV  EMRG   —    —     ERR   EMRG   —     ERR  —     —      —     —     —     IDLE
SAFETY_LOCK  CHGR  —     —     LOWV  EMRG   IDLE —     ERR   EMRG   —     ERR  —     —      —     —     —     —
LOW_V        CHGR  —     —     —     EMRG   IDLE —     ERR   EMRG   —     ERR  —     SLEEP  —     —     —     —
ERROR        CHGR  —     SFTY  LOWV  EMRG   —    —     —     EMRG   —     —    IDLE  —      IDLE  —     —     —
EMERGENCY    —     —     —     —     —      —    —     —     —      —     —    —     —      —     —     —     —
```

Not shown above for width: `EVT_LOCK` (IDLE → SAFETY_LOCK, MANUAL →
SAFETY_LOCK; sets `userLock`) and `EVT_UNLOCK` (SAFETY_LOCK → IDLE,
**guarded**: honored only while `userLock` is set, so a low-SoC lock cannot
be dismissed from the menu; `userLock` clears on any SAFETY_LOCK exit).

### Overcharge (`ovchargeBlock`)

Overcharge (`EVT_SOC_OVCH`, gauge `BATHI` flag) causes **no transition at
all**. The design follows from two invariants:

1. **Charger attached ⇔ CHARGING, outputs off; never passthrough.** The
   CHARGING state simply tracks physical reality. The firmware cannot stop
   the C1 charge path anyway (the BQ25713 is slaved to the TPS25750 on its
   private I2C_EX bus), so pretending to be in another state while current
   flows would only break the no-passthrough policy: with the charger
   attached, loads would be fed by the adapter power path while the FSM
   claims to be idle. Real cell protection is the BQ7791500 BMS (hardware).
2. **Recovery = unplug, then discharge.** Attaching loads with the charger
   still in does not discharge the pack (the adapter feeds them, measured
   in the emulator: net pack current stays positive). On unplug the normal
   `CHARGER_DISCONNECTED → IDLE` transition re-opens the outputs, the user
   discharges, and the gauge clears `BATHI` with hysteresis.

The FSM context keeps an `ovchargeBlock` flag as **informational state**
(it drives the UI `BATTERY FULL` warning and the trace; it does not gate
any transition):

| Event | Effect on `ovchargeBlock` |
| :---- | :------------------------ |
| `EVT_SOC_OVCH` | Set to `true` (BATHI set: overcharge episode begins) |
| `EVT_SOC_OK` | Cleared to `false` (BATHI cleared: episode over) |

`EVT_SOC_OK` is produced by `charge.c` on the `BATHI` falling edge (the
gauge drops it with hysteresis once the pack voltage relaxes below the
clear threshold), and, as before, when SoC rises back through 15 % for
the low-SoC recovery path.

Note that `Idle_Enter` re-fires `EVT_CHARGER_CONNECTED` when the primary
contract is still active (e.g. waking from SLEEP while charging), so the
FSM converges back to CHARGING whenever a charger is physically attached.
