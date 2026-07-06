# Power Bank FSM

## Table of Contents

- [Introduction](#introduction)
- [Events](#events)
  - [Event List](#event-list)
  - [Event Queue](#event-queue)
  - [Event Producers](#event-producers)

---

## Introduction

This document describes the Finite State Machine (FSM) that governs the overall operating behavior of the Ducker Charger. The FSM determines which state the device is in at any given time — controlling which ports are active, what the display shows, and how the system reacts to hardware events such as low battery, fault conditions, or charger connection.

The FSM is implemented in `firmware/cubeMX/Core/Src/system/fsm.c` using a **hybrid vtable + transition matrix** approach:
- The **vtable** defines the behavior of each state through three handlers: `onEnter`, `onRun`, and `onExit`.
- The **transition matrix** encodes when to leave a state: a 2D lookup table indexed by `[current state][event]` returns the next state, keeping transition logic fully decoupled from state behavior.

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
