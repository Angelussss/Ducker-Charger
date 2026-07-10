#include "system/fsm.h"
#include "main.h"
#include "system/charge.h"
#include "display/ili9341.h"   /* ILI9341_Backlight: owns BCKL_CTRL polarity (JP402) */

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
    // Note on EVT_SOC_OVCH: overcharge causes NO transition anywhere.
    // Invariant: charger attached <=> CHARGING (outputs off, never
    // passthrough) — the state tracks physical reality, which the
    // firmware cannot change anyway (BQ25713 is slaved to the TPS25750
    // on its private bus). The event only maintains ovchargeBlock
    // (informational: UI warning, trace); recovery is the user
    // unplugging (-> IDLE, outputs back) and discharging until the
    // gauge clears BATHI (EVT_SOC_OK).
    [STATE_SLEEP] =
        {
            [0 ... EVT_NUMBER - 1] = STATE_NUMBER,
            [EVT_CHARGER_CONNECTED] = STATE_CHARGING,
            [EVT_SOC_SAFETY] = STATE_SAFETY_LOCK,
            [EVT_SOC_LOWV] = STATE_LOW_V,
            [EVT_SOC_UNDERV] = STATE_EMERGENCY,
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
            [EVT_FAULT_OT] = STATE_ERROR,
            [EVT_FAULT_CRITICAL] = STATE_EMERGENCY,
            [EVT_ERROR] = STATE_ERROR,
            [EVT_INACTIVITY] = STATE_SLEEP,
            [EVT_BUTTON_LONG] = STATE_DEEP_SLEEP,
            [EVT_MANUAL_ENTER] = STATE_MANUAL,
            [EVT_LOCK] = STATE_SAFETY_LOCK,
        },
    [STATE_SAFETY_LOCK] =
        {
            [0 ... EVT_NUMBER - 1] = STATE_NUMBER,
            [EVT_CHARGER_CONNECTED] = STATE_CHARGING,
            [EVT_SOC_LOWV] = STATE_LOW_V,
            [EVT_SOC_UNDERV] = STATE_EMERGENCY,
            [EVT_SOC_OK] = STATE_IDLE,
            [EVT_FAULT_OT] = STATE_ERROR,
            [EVT_FAULT_CRITICAL] = STATE_EMERGENCY,
            [EVT_ERROR] = STATE_ERROR,
            [EVT_UNLOCK] = STATE_IDLE, // guarded: only if userLock is set
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
            // No EVT_SOC_OVCH transition: overcharged or not, while the
            // charger is in we stay in CHARGING with outputs off (no
            // passthrough). With the charger attached the loads would be
            // fed by the adapter anyway (BQ25713 power path), so leaving
            // for IDLE would break the no-passthrough rule without
            // actually discharging the pack.
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
            [EVT_FAULT_OT] = STATE_ERROR,
            [EVT_FAULT_CRITICAL] = STATE_EMERGENCY,
            [EVT_ERROR] = STATE_ERROR,
            [EVT_MANUAL_EXIT] = STATE_IDLE,
            [EVT_LOCK] = STATE_SAFETY_LOCK,
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

// State of the last FSM initialized/updated, readable without the instance
// pointer (PB_FSM_ActiveState). There is exactly one FSM on this firmware.
static State_ID_t activeState = STATE_IDLE;

void PB_FSM_Init(FSM *fsm) {
  fsm->currentState = STATE_IDLE;
  fsm->nextState = STATE_IDLE;
  fsm->ovchargeBlock = false;
  fsm->userLock = false;
  activeState = fsm->currentState;

  if (StateTable[fsm->currentState].onEnter)
    StateTable[fsm->currentState].onEnter(fsm);
}

void PB_FSM_RequestState(FSM *fsm, State_ID_t newState) {
  fsm->nextState = newState;
}

// Weak no-op: overridden by the emulator/tests to record event history.
__attribute__((weak)) void PB_FSM_TraceEvent(State_ID_t from, Event_t event,
                                             State_ID_t to, bool blocked) {
  (void)from;
  (void)event;
  (void)to;
  (void)blocked;
}

void PB_FSM_FireEvent(FSM *fsm, Event_t event) {
  // ovchargeBlock is informational state, not a transition guard: the
  // charger charges autonomously whether we like it or not, so a charger
  // connection always enters CHARGING (outputs off, no passthrough) and
  // the UI warns the user to unplug. The flag tracks the overcharge
  // episode from EVT_SOC_OVCH (BATHI set) to EVT_SOC_OK (BATHI cleared).
  if (event == EVT_SOC_OVCH)
    fsm->ovchargeBlock = true;
  if (event == EVT_SOC_OK)
    fsm->ovchargeBlock = false;
  if (event == EVT_LOCK)
    fsm->userLock = true;

  State_ID_t next = TransitionTable[fsm->currentState][event];

  // Guard: EVT_UNLOCK undoes only a USER-initiated lock. A SAFETY_LOCK
  // entered for low SoC must not be dismissible from the menu.
  if (event == EVT_UNLOCK && !fsm->userLock && next != STATE_NUMBER) {
    PB_FSM_TraceEvent(fsm->currentState, event, next, true);
    return;
  }
  PB_FSM_TraceEvent(fsm->currentState, event, next, false);
  if (next != STATE_NUMBER)
    PB_FSM_RequestState(fsm, next);
}

State_ID_t PB_FSM_GetState(const FSM *fsm) { return fsm->currentState; }

void PB_FSM_Update(FSM *fsm) {
  if (fsm->currentState != fsm->nextState) {
    if (StateTable[fsm->currentState].onExit)
      StateTable[fsm->currentState].onExit(fsm);

    fsm->currentState = fsm->nextState;
    activeState = fsm->currentState;

    if (StateTable[fsm->currentState].onEnter)
      StateTable[fsm->currentState].onEnter(fsm);
  }

  if (StateTable[fsm->currentState].onRun)
    StateTable[fsm->currentState].onRun(fsm);
}

State_ID_t PB_FSM_ActiveState(void) { return activeState; }

// ---- State handlers ----

// DEEP_SLEEP ---------------------------------------------------------------

static void DeepSleep_Enter(FSM *fsm) {
  (void)fsm;
  disable_USBA1();
  disable_USBA2();
  disable_USBC2();
  disable_STPD01();
  disable_OTG();
  ILI9341_Backlight(0);

  /* Button (EXTI0) is the sole wake source, but a wake only sticks if a
   * charger is present (CHRG_OK, plain input, not an EXTI line): without
   * one this loops straight back into STOP, transparently. */
  do {
    HAL_SuspendTick();
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);

    HAL_ResumeTick();
    SystemClock_Config();
  } while (HAL_GPIO_ReadPin(USB_CHRG_OK_CTRL_GPIO_Port,
                             USB_CHRG_OK_CTRL_Pin) != GPIO_PIN_SET);

  /* Charger was present at the accepted press: push the event ourselves
   * (nothing else would) so DEEP_SLEEP -> IDLE fires, and IDLE's readCS()
   * picks the already-present contract up right after. */
  event_push(EVT_BUTTON_SHORT);
}

static void DeepSleep_Run(FSM *fsm) { (void)fsm; }

static void DeepSleep_Exit(FSM *fsm) {
  (void)fsm;
  ILI9341_Backlight(1);
}

// SLEEP --------------------------------------------------------------------

static void Sleep_Enter(FSM *fsm) {
  (void)fsm;
  ILI9341_Backlight(0);
}

static void Sleep_Run(FSM *fsm) {
  (void)fsm;
  readCS();
  readSensors();
  readINA();
}

static void Sleep_Exit(FSM *fsm) {
  (void)fsm;
  ILI9341_Backlight(1);
}

// IDLE ---------------------------------------------------------------------

static void Idle_Enter(FSM *fsm) {
  (void)fsm;
  enable_USBA1();
  enable_USBA2();
  ILI9341_Backlight(1);

  // Charger still delivering? IDLE is then only a transient stop: re-fire
  // the (edge-triggered, so otherwise lost) event and settle in CHARGING.
  // Covers the SLEEP -> IDLE wake with the charger attached, and any other
  // path that dropped to IDLE while the contract stayed active.
  PDContract c1 = getPrimaryUSBC_Contract();
  if (c1.isPlugged && c1.isSink)
    event_push(EVT_CHARGER_CONNECTED);

  // Same re-validation for the SoC thresholds: their producers are
  // edge-detected, so crossing a threshold while a protection state was
  // already active leaves no edge to fire on the way back to IDLE
  // (user unlock at 14%, ERROR acked while low, ...). voltage == 0 means
  // the gauge has never been read yet (boot) — nothing to validate.
  FuelGaugeSensors fg = getFuelGaugeData();
  if (fg.voltage > 0) {
    if (fg.SoC < SOC_LOWV_THRESHOLD)
      event_push(EVT_SOC_LOWV);
    else if (fg.SoC < SOC_SAFETY_THRESHOLD)
      event_push(EVT_SOC_SAFETY);
  }
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
  disable_OTG();
}

static void SafetyLock_Run(FSM *fsm) {
  (void)fsm;
  readSensors();
  readCS();
}

static void SafetyLock_Exit(FSM *fsm) {
  // whatever the exit path (unlock, charger, SoC recovery), the user-lock
  // episode is over — a fresh EVT_LOCK is needed to lock again
  fsm->userLock = false;
}

// LOW_V --------------------------------------------------------------------

static void LowV_Enter(FSM *fsm) {
  (void)fsm;
  disable_USBA1();
  disable_USBA2();
  disable_USBC2();
  disable_STPD01();
  disable_OTG();
  ILI9341_Backlight(0);
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
  disable_OTG();
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
  // No blanket re-enable here: the destination state decides. Idle_Enter
  // turns A1/A2 back on; CHARGING -> SLEEP (inactivity while charging)
  // must keep them OFF — charging continues in SLEEP and outputs stay
  // closed, same policy as CHARGING itself.
}

// MANUAL -------------------------------------------------------------------

static void Manual_Enter(FSM *fsm) {
  (void)fsm;
  HAL_GPIO_WritePin(C2_LAB_EN_GPIO_Port, C2_LAB_EN_Pin, GPIO_PIN_SET);
}

static void Manual_Run(FSM *fsm) {
  (void)fsm;
  readCS();
  readSensors();
  readINA();
}

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
  readCS();
  readSensors();
  readINA();
}

static void Error_Exit(FSM *fsm) { (void)fsm; }
