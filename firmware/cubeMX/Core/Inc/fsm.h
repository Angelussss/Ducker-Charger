#ifndef FSM_H
#define FSM_H

#include <stdbool.h>
#include <stdint.h>

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
} FSM;

typedef struct {
  void (*onEnter)(FSM *fsm);
  void (*onRun)(FSM *fsm);
  void (*onExit)(FSM *fsm);
} PB_State_t;

// ---- Public API ----
void PB_FSM_Init(FSM *fsm);
void PB_FSM_Update(FSM *fsm);
void PB_FSM_RequestState(FSM *fsm, State_ID_t newState);

// ---- Hardware check ----
bool PB_Check_Fault_HW();

#endif // FSM_H
