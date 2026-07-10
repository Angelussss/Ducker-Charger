#ifndef FSM_H
#define FSM_H

#include <stdbool.h>
#include <stdint.h>
#include "system/event.h"

// ---- State definitions ----
typedef enum {
  STATE_DEEP_SLEEP = 0,
  STATE_SLEEP,
  STATE_IDLE,
  STATE_SAFETY_LOCK,
  STATE_LOW_V,
  STATE_EMERGENCY,
  STATE_CHARGING,
  STATE_MANUAL,
  STATE_ERROR,
  STATE_NUMBER
} State_ID_t;

// ---- FSM Context ----
typedef struct {
  State_ID_t currentState;
  State_ID_t nextState;
  bool ovchargeBlock; // overcharge episode in progress: set by EVT_SOC_OVCH
                      // (gauge BATHI), cleared by EVT_SOC_OK. Informational
                      // (UI warning / trace) — it does NOT gate transitions:
                      // charger attached always means CHARGING, outputs off.
  bool userLock;      // SAFETY_LOCK was requested by the user (EVT_LOCK from
                      // the menu). EVT_UNLOCK is honored only when set — a
                      // low-SoC SAFETY_LOCK cannot be dismissed from the menu.
                      // Cleared on any exit from SAFETY_LOCK.
} FSM;

typedef struct {
  void (*onEnter)(FSM *fsm);
  void (*onRun)(FSM *fsm);
  void (*onExit)(FSM *fsm);
} PB_State_t;

// ---- Public API ----

/** @brief Initialize the FSM, set initial state to IDLE, and call its onEnter handler. */
void PB_FSM_Init(FSM *fsm);

/** @brief Run one FSM tick: execute the pending state transition (if any), then call the active state's onRun handler. */
void PB_FSM_Update(FSM *fsm);

/** @brief Schedule a transition to newState; takes effect on the next PB_FSM_Update() call. */
void PB_FSM_RequestState(FSM *fsm, State_ID_t newState);

/** @brief Look up the transition matrix for the given event and request the next state if a valid transition exists.
 *         Handles the overcharge conditional for SAFETY_LOCK. Must be called for every event popped from the queue. */
void PB_FSM_FireEvent(FSM *fsm, Event_t event);

/** @brief Return the FSM's current state. */
State_ID_t PB_FSM_GetState(const FSM *fsm);

/** @brief Current state of the last FSM that ran PB_FSM_Init/Update.
 *         Lets layers without access to main()'s FSM instance (charge.c
 *         interrupt handlers) check whether outputs may be enabled.
 *         Defaults to STATE_IDLE before any FSM has been initialized. */
State_ID_t PB_FSM_ActiveState(void);

/** @brief Trace hook, called by PB_FSM_FireEvent for EVERY event:
 *         `to` is the requested state, STATE_NUMBER when the event causes
 *         no transition, `blocked` marks a transition refused by a guard
 *         (today only SAFETY_LOCK -> CHARGING under overcharge).
 *         Weak no-op by default: the target build pays nothing, the
 *         emulator/tests can override it to record the event history. */
void PB_FSM_TraceEvent(State_ID_t from, Event_t event, State_ID_t to,
                       bool blocked);

#endif // FSM_H
