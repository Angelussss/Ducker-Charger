#include "system/fsm.h"
#include "main.h"
#include "system/charge.h"

extern void SystemClock_Config(void);

// ---- Forward declarations ----

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

// ERROR
static void Error_Enter(FSM *fsm);
static void Error_Run(FSM *fsm);
static void Error_Exit(FSM *fsm);

// ---- Vtable ----

static const PB_State_t StateTable[STATE_NUMBER] = {
    [STATE_DEEP_SLEEP] = {DeepSleep_Enter, DeepSleep_Run, DeepSleep_Exit},
    [STATE_SLEEP] = {Sleep_Enter, Sleep_Run, Sleep_Exit},
    [STATE_IDLE] = {Idle_Enter, Idle_Run, Idle_Exit},
    [STATE_SAFETY_LOCK] = {SafetyLock_Enter, SafetyLock_Run, SafetyLock_Exit},
    [STATE_LOW_V] = {LowV_Enter, LowV_Run, LowV_Exit},
    [STATE_EMERGENCY] = {Emergency_Enter, Emergency_Run, Emergency_Exit},
    [STATE_CHARGING] = {Charging_Enter, Charging_Run, Charging_Exit},
    [STATE_MANUAL] = {Manual_Enter, Manual_Run, Manual_Exit},
    [STATE_ERROR] = {Error_Enter, Error_Run, Error_Exit},
};

// ---- Transition matrix ----
// Rows: current state. Columns: event. Value: next state, STATE_NUMBER = no
// transition
static const State_ID_t TransitionTable[STATE_NUMBER][EVT_NUMBER] = {
    [STATE_DEEP_SLEEP] =
        {
            [0 ... EVT_NUMBER - 1] = STATE_NUMBER,
            [EVT_CHARGER_CONNECTED] = STATE_IDLE,
            [EVT_BUTTON_SHORT] = STATE_IDLE,
        },
    [STATE_SLEEP] =
        {
            [0 ... EVT_NUMBER - 1] = STATE_NUMBER,
            [EVT_CHARGER_CONNECTED] = STATE_CHARGING,
            [EVT_SOC_SAFETY] = STATE_SAFETY_LOCK,
            [EVT_SOC_LOWV] = STATE_LOW_V,
            [EVT_SOC_UNDERV] = STATE_EMERGENCY,
            [EVT_SOC_OVCH] = STATE_SAFETY_LOCK,
            [EVT_FAULT_OT] = STATE_ERROR,
            [EVT_FAULT_CRITICAL] = STATE_EMERGENCY,
            [EVT_ERROR] = STATE_ERROR,
            [EVT_BUTTON_SHORT] = STATE_IDLE,
            [EVT_BUTTON_LONG] = STATE_DEEP_SLEEP,
        },
    [STATE_IDLE] =
        {
            [0 ... EVT_NUMBER - 1] = STATE_NUMBER,
            [EVT_CHARGER_CONNECTED] = STATE_CHARGING,
            [EVT_SOC_SAFETY] = STATE_SAFETY_LOCK,
            [EVT_SOC_LOWV] = STATE_LOW_V,
            [EVT_SOC_UNDERV] = STATE_EMERGENCY,
            [EVT_SOC_OVCH] = STATE_SAFETY_LOCK,
            [EVT_FAULT_OT] = STATE_ERROR,
            [EVT_FAULT_CRITICAL] = STATE_EMERGENCY,
            [EVT_ERROR] = STATE_ERROR,
            [EVT_INACTIVITY] = STATE_SLEEP,
            [EVT_BUTTON_LONG] = STATE_DEEP_SLEEP,
            [EVT_MANUAL_ENTER] = STATE_MANUAL,
        },
    [STATE_SAFETY_LOCK] =
        {
            [0 ... EVT_NUMBER - 1] = STATE_NUMBER,
            [EVT_CHARGER_CONNECTED] =
                STATE_CHARGING, // conditional: see PB_FSM_FireEvent
            [EVT_SOC_LOWV] = STATE_LOW_V,
            [EVT_SOC_UNDERV] = STATE_EMERGENCY,
            [EVT_SOC_OK] = STATE_IDLE,
            [EVT_FAULT_CRITICAL] = STATE_EMERGENCY,
        },
    [STATE_LOW_V] =
        {
            [0 ... EVT_NUMBER - 1] = STATE_NUMBER,
            [EVT_CHARGER_CONNECTED] = STATE_CHARGING,
            [EVT_SOC_UNDERV] = STATE_EMERGENCY,
            [EVT_SOC_OK] = STATE_IDLE,
            [EVT_FAULT_OT] = STATE_ERROR,
            [EVT_FAULT_CRITICAL] = STATE_EMERGENCY,
            [EVT_ERROR] = STATE_ERROR,
            [EVT_INACTIVITY] = STATE_SLEEP,
        },
    [STATE_EMERGENCY] =
        {
            [0 ... EVT_NUMBER - 1] = STATE_NUMBER,
        },
    [STATE_CHARGING] =
        {
            [0 ... EVT_NUMBER - 1] = STATE_NUMBER,
            [EVT_CHARGER_DISCONNECTED] = STATE_IDLE,
            [EVT_SOC_SAFETY] = STATE_SAFETY_LOCK,
            [EVT_SOC_LOWV] = STATE_LOW_V,
            [EVT_SOC_UNDERV] = STATE_EMERGENCY,
            [EVT_SOC_OVCH] = STATE_SAFETY_LOCK,
            [EVT_FAULT_OT] = STATE_ERROR,
            [EVT_FAULT_CRITICAL] = STATE_EMERGENCY,
            [EVT_FAULT_OCC] = STATE_SAFETY_LOCK,
            [EVT_ERROR] = STATE_ERROR,
            [EVT_INACTIVITY] = STATE_SLEEP,
            [EVT_MANUAL_ENTER] = STATE_MANUAL,
        },
    [STATE_MANUAL] =
        {
            [0 ... EVT_NUMBER - 1] = STATE_NUMBER,
            [EVT_SOC_SAFETY] = STATE_SAFETY_LOCK,
            [EVT_SOC_LOWV] = STATE_LOW_V,
            [EVT_SOC_UNDERV] = STATE_EMERGENCY,
            [EVT_SOC_OVCH] = STATE_SAFETY_LOCK,
            [EVT_FAULT_OT] = STATE_ERROR,
            [EVT_FAULT_CRITICAL] = STATE_EMERGENCY,
            [EVT_ERROR] = STATE_ERROR,
            [EVT_MANUAL_EXIT] = STATE_IDLE,
        },
    [STATE_ERROR] =
        {
            [0 ... EVT_NUMBER - 1] = STATE_NUMBER,
            [EVT_CHARGER_CONNECTED] = STATE_CHARGING,
            [EVT_SOC_SAFETY] = STATE_SAFETY_LOCK,
            [EVT_SOC_LOWV] = STATE_LOW_V,
            [EVT_SOC_UNDERV] = STATE_EMERGENCY,
            [EVT_FAULT_CRITICAL] = STATE_EMERGENCY,
            [EVT_ERROR_CLEAR] = STATE_IDLE,
            [EVT_BUTTON_SHORT] = STATE_IDLE,
        },
};

// ---- Engine ----

void PB_FSM_Init(FSM *fsm) {
  fsm->currentState = STATE_IDLE;
  fsm->nextState = STATE_IDLE;
  fsm->ovchargeBlock = false;

  if (StateTable[fsm->currentState].onEnter)
    StateTable[fsm->currentState].onEnter(fsm);
}

void PB_FSM_RequestState(FSM *fsm, State_ID_t newState) {
  fsm->nextState = newState;
}

void PB_FSM_FireEvent(FSM *fsm, Event_t event) {
  if (event == EVT_SOC_OVCH)
    fsm->ovchargeBlock = true;
  if (event == EVT_SOC_OK)
    fsm->ovchargeBlock = false;

  // Conditional: SAFETY_LOCK → CHARGING is blocked while overcharge is active
  if (fsm->currentState == STATE_SAFETY_LOCK &&
      event == EVT_CHARGER_CONNECTED && fsm->ovchargeBlock)
    return;

  State_ID_t next = TransitionTable[fsm->currentState][event];
  if (next != STATE_NUMBER)
    PB_FSM_RequestState(fsm, next);
}

void PB_FSM_Update(FSM *fsm) {
  if (fsm->currentState != fsm->nextState) {
    if (StateTable[fsm->currentState].onExit)
      StateTable[fsm->currentState].onExit(fsm);

    fsm->currentState = fsm->nextState;

    if (StateTable[fsm->currentState].onEnter)
      StateTable[fsm->currentState].onEnter(fsm);
  }

  if (StateTable[fsm->currentState].onRun)
    StateTable[fsm->currentState].onRun(fsm);
}

// ---- State handlers ----

// DEEP_SLEEP ---------------------------------------------------------------

static void DeepSleep_Enter(FSM *fsm) {
  (void)fsm;
  disable_USBA1();
  disable_USBA2();
  disable_USBC2();
  disable_STPD01();
  HAL_GPIO_WritePin(BCKL_CTRL_GPIO_Port, BCKL_CTRL_Pin, GPIO_PIN_RESET);
  HAL_SuspendTick();
  HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);

  HAL_ResumeTick();
  SystemClock_Config();
}

static void DeepSleep_Run(FSM *fsm) { (void)fsm; }

static void DeepSleep_Exit(FSM *fsm) {
  (void)fsm;
  HAL_GPIO_WritePin(BCKL_CTRL_GPIO_Port, BCKL_CTRL_Pin, GPIO_PIN_SET);
}

// SLEEP --------------------------------------------------------------------

static void Sleep_Enter(FSM *fsm) {
  (void)fsm;
  HAL_GPIO_WritePin(BCKL_CTRL_GPIO_Port, BCKL_CTRL_Pin, GPIO_PIN_RESET);
}

static void Sleep_Run(FSM *fsm) {
  (void)fsm;
  readSensors();
}

static void Sleep_Exit(FSM *fsm) {
  (void)fsm;
  HAL_GPIO_WritePin(BCKL_CTRL_GPIO_Port, BCKL_CTRL_Pin, GPIO_PIN_SET);
}

// IDLE ---------------------------------------------------------------------

static void Idle_Enter(FSM *fsm) {
  (void)fsm;
  enable_USBA1();
  enable_USBA2();
  HAL_GPIO_WritePin(BCKL_CTRL_GPIO_Port, BCKL_CTRL_Pin, GPIO_PIN_SET);
}

static void Idle_Run(FSM *fsm) {
  (void)fsm;
  readCS();
  readSensors();
  readINA();
}

static void Idle_Exit(FSM *fsm) { (void)fsm; }

// SAFETY_LOCK --------------------------------------------------------------

static void SafetyLock_Enter(FSM *fsm) {
  (void)fsm;
  disable_USBA1();
  disable_USBA2();
  disable_USBC2();
  disable_STPD01();
}

static void SafetyLock_Run(FSM *fsm) {
  (void)fsm;
  readSensors();
  readCS();
}

static void SafetyLock_Exit(FSM *fsm) { (void)fsm; }

// LOW_V --------------------------------------------------------------------

static void LowV_Enter(FSM *fsm) {
  (void)fsm;
  disable_USBA1();
  disable_USBA2();
  disable_USBC2();
  disable_STPD01();
  HAL_GPIO_WritePin(BCKL_CTRL_GPIO_Port, BCKL_CTRL_Pin, GPIO_PIN_RESET);
}

static void LowV_Run(FSM *fsm) {
  (void)fsm;
  readSensors();
  readCS();
}

static void LowV_Exit(FSM *fsm) { (void)fsm; }

// EMERGENCY ----------------------------------------------------------------

static void Emergency_Enter(FSM *fsm) {
  (void)fsm;
  disable_USBA1();
  disable_USBA2();
  disable_USBC2();
  disable_STPD01();
}

static void Emergency_Run(FSM *fsm) { (void)fsm; }

static void Emergency_Exit(FSM *fsm) { (void)fsm; }

// CHARGING -----------------------------------------------------------------

static void Charging_Enter(FSM *fsm) {
  (void)fsm;
  disable_USBA1();
  disable_USBA2();
  disable_USBC2();
  disable_STPD01();
}

static void Charging_Run(FSM *fsm) {
  (void)fsm;
  readCS();
  readSensors();
  readINA();
}

static void Charging_Exit(FSM *fsm) {
  (void)fsm;
  enable_USBA1();
  enable_USBA2();
}

// MANUAL -------------------------------------------------------------------

static void Manual_Enter(FSM *fsm) {
  (void)fsm;
  HAL_GPIO_WritePin(C2_LAB_EN_GPIO_Port, C2_LAB_EN_Pin, GPIO_PIN_SET);
}

static void Manual_Run(FSM *fsm) { (void)fsm; }

static void Manual_Exit(FSM *fsm) {
  (void)fsm;
  disable_STPD01();
  disable_USBC2();
  HAL_GPIO_WritePin(C2_LAB_EN_GPIO_Port, C2_LAB_EN_Pin, GPIO_PIN_RESET);
}

// ERROR --------------------------------------------------------------------

static void Error_Enter(FSM *fsm) { (void)fsm; }

static void Error_Run(FSM *fsm) {
  (void)fsm;
  readSensors();
}

static void Error_Exit(FSM *fsm) { (void)fsm; }
