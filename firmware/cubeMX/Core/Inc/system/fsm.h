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
  bool ovchargeBlock; // blocks CHARGER_CONNECTED → CHARGING from SAFETY_LOCK when set by EVT_SOC_OVCH
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

#endif // FSM_H
