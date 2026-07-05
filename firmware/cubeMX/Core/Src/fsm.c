#include "fsm.h"

// DEEP_SLEEP
static void DeepSleep_Enter(FSM *fsm);
static void DeepSleep_Run(FSM *fsm);
static void DeepSleep_Exit(FSM *fsm);

// SLEEP
static void Sleep_Enter(FSM *fsm);
static void Sleep_Run(FSM *fsm);
static void Sleep_Exit(FSM *fsm);

// IDLE
static void Idle_Enter(FSM *fsm);
static void Idle_Run(FSM *fsm);
static void Idle_Exit(FSM *fsm);

// SAFETY_LOCK
static void SafetyLock_Enter(FSM *fsm);
static void SafetyLock_Run(FSM *fsm);
static void SafetyLock_Exit(FSM *fsm);

// LOW_V
static void LowV_Enter(FSM *fsm);
static void LowV_Run(FSM *fsm);
static void LowV_Exit(FSM *fsm);

// EMERGENCY
static void Emergency_Enter(FSM *fsm);
static void Emergency_Run(FSM *fsm);
static void Emergency_Exit(FSM *fsm);

// CHARGING
static void Charging_Enter(FSM *fsm);
static void Charging_Run(FSM *fsm);
static void Charging_Exit(FSM *fsm);

// MANUAL
static void Manual_Enter(FSM *fsm);
static void Manual_Run(FSM *fsm);
static void Manual_Exit(FSM *fsm);

// Error
static void Error_Enter(FSM *fsm);
static void Error_Run(FSM *fsm);
static void Error_Exit(FSM *fsm);

/* ---- The Lookup Table
 * This table map the enum State_ID_t with the specific function.
 * |    State   |   onEnter  |   onRun  |   onExit  |
 * | STATE_IDLE | Idle_Enter | Idle_Run | Idle_Exit |
 *
 * */

static const PB_State_t StateTable[STATE_NUMBER] = {
    [STATE_DEEP_SLEEP] = {DeepSleep_Enter, DeepSleep_Run, DeepSleep_Exit},
    [STATE_SLEEP] = {Sleep_Enter, Sleep_Run, Sleep_Exit},
    [STATE_IDLE] = {Idle_Enter, Idle_Run, Idle_Exit},
    [STATE_SAFETY_LOCK] = {SafetyLock_Enter, SafetyLock_Run, SafetyLock_Exit},
    [STATE_LOW_V] = {LowV_Enter, LowV_Run, LowV_Exit},
    [STATE_EMERGENCY] = {Emergency_Enter, Emergency_Run, Emergency_Exit},
    [STATE_CHARGING] = {Charging_Enter, Charging_Run, Charging_Exit},
    [STATE_MANUAL] = {Manual_Enter, Manual_Run, Manual_Exit},
    [STATE_ERROR] = {Error_Enter, Error_Run, Error_Exit}};

void PB_FSM_Init(FSM *fsm) {
  fsm->currentState = STATE_IDLE;
  fsm->nextState = STATE_IDLE;

  if (StateTable[fsm->currentState].onEnter) {
    StateTable[fsm->currentState].onEnter(fsm);
  }
}

void PB_FSM_RequestState(FSM *fsm, State_ID_t newState) {
  fsm->nextState = newState;
}

void PB_FSM_Update(FSM *fsm) {
  if (fsm->currentState != fsm->nextState) {
    // Exit old
    if (StateTable[fsm->currentState].onExit)
      StateTable[fsm->currentState].onExit(fsm);

    fsm->currentState = fsm->nextState;

    // Enter new
    if (StateTable[fsm->currentState].onEnter)
      StateTable[fsm->currentState].onEnter(fsm);
  }

  if (StateTable[fsm->currentState].onRun) {
    StateTable[fsm->currentState].onRun(fsm);
  }
}
