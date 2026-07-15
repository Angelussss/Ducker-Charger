#include "system/fsm.h"
#include "system/event.h"
#include "stubs/stm32_hal_stub.h"
#include "framework.h"

static FSM fsm;

// Force the FSM into a state without triggering Enter/Exit handlers.
static void set_state(State_ID_t s) {
    fsm.currentState = s;
    fsm.nextState    = s;
}

static void drain(void) {
    Event_t e;
    while (event_pop(&e));
}

// Each test block calls this to get a clean slate.
static void setup(void) {
    stub_reset();
    drain();
    PB_FSM_Init(&fsm); // enters STATE_IDLE, calls Idle_Enter (GPIO writes only)
    stub_gpio_write_count = 0; // clear the Idle_Enter write log
}

int main(void) {
    // Init lands in STATE_IDLE
    setup();
    ASSERT(fsm.currentState == STATE_IDLE);
    ASSERT(fsm.ovchargeBlock == false);

    // IDLE + CHARGER_CONNECTED → CHARGING
    setup(); set_state(STATE_IDLE);
    PB_FSM_FireEvent(&fsm, EVT_CHARGER_CONNECTED);
    ASSERT(fsm.nextState == STATE_CHARGING);

    // IDLE + SOC_SAFETY → SAFETY_LOCK
    setup(); set_state(STATE_IDLE);
    PB_FSM_FireEvent(&fsm, EVT_SOC_SAFETY);
    ASSERT(fsm.nextState == STATE_SAFETY_LOCK);

    // IDLE + SOC_OVCH → NO transition, ovchargeBlock set (informational)
    setup(); set_state(STATE_IDLE);
    fsm.ovchargeBlock = false;
    PB_FSM_FireEvent(&fsm, EVT_SOC_OVCH);
    ASSERT(fsm.nextState == STATE_IDLE);
    ASSERT(fsm.ovchargeBlock == true);

    // IDLE + CHARGER_CONNECTED while ovchargeBlock → CHARGING anyway:
    // charger attached always means CHARGING (outputs off, no passthrough);
    // the flag only drives the UI warning
    setup(); set_state(STATE_IDLE);
    fsm.ovchargeBlock = true;
    PB_FSM_FireEvent(&fsm, EVT_CHARGER_CONNECTED);
    ASSERT(fsm.nextState == STATE_CHARGING);

    // CHARGING + SOC_OVCH → NO transition: stay in CHARGING, outputs stay
    // off. Recovery is unplug (→ IDLE) then discharge until BATHI clears.
    setup(); set_state(STATE_CHARGING);
    fsm.ovchargeBlock = false;
    PB_FSM_FireEvent(&fsm, EVT_SOC_OVCH);
    ASSERT(fsm.nextState == STATE_CHARGING);
    ASSERT(fsm.ovchargeBlock == true);

    // IDLE + INACTIVITY → SLEEP
    setup(); set_state(STATE_IDLE);
    PB_FSM_FireEvent(&fsm, EVT_INACTIVITY);
    ASSERT(fsm.nextState == STATE_SLEEP);

    // IDLE + BUTTON_LONG → DEEP_SLEEP
    setup(); set_state(STATE_IDLE);
    PB_FSM_FireEvent(&fsm, EVT_BUTTON_LONG);
    ASSERT(fsm.nextState == STATE_DEEP_SLEEP);

    // IDLE + MANUAL_ENTER → MANUAL
    setup(); set_state(STATE_IDLE);
    PB_FSM_FireEvent(&fsm, EVT_MANUAL_ENTER);
    ASSERT(fsm.nextState == STATE_MANUAL);

    // IDLE + unregistered event (CHARGER_DISCONNECTED) → no transition
    setup(); set_state(STATE_IDLE);
    PB_FSM_FireEvent(&fsm, EVT_CHARGER_DISCONNECTED);
    ASSERT(fsm.nextState == STATE_IDLE);

    // CHARGING + CHARGER_DISCONNECTED → IDLE
    setup(); set_state(STATE_CHARGING);
    PB_FSM_FireEvent(&fsm, EVT_CHARGER_DISCONNECTED);
    ASSERT(fsm.nextState == STATE_IDLE);

    // CHARGING + FAULT_OCC → SAFETY_LOCK
    setup(); set_state(STATE_CHARGING);
    PB_FSM_FireEvent(&fsm, EVT_FAULT_OCC);
    ASSERT(fsm.nextState == STATE_SAFETY_LOCK);

    // SAFETY_LOCK + CHARGER_CONNECTED → CHARGING regardless of
    // ovchargeBlock (the flag is informational, never a transition guard:
    // charger attached always means CHARGING, outputs off)
    setup(); set_state(STATE_SAFETY_LOCK);
    fsm.ovchargeBlock = false;
    PB_FSM_FireEvent(&fsm, EVT_CHARGER_CONNECTED);
    ASSERT(fsm.nextState == STATE_CHARGING);

    setup(); set_state(STATE_SAFETY_LOCK);
    fsm.ovchargeBlock = true;
    PB_FSM_FireEvent(&fsm, EVT_CHARGER_CONNECTED);
    ASSERT(fsm.nextState == STATE_CHARGING);

    // SOC_OK clears ovchargeBlock and returns SAFETY_LOCK → IDLE
    setup(); set_state(STATE_SAFETY_LOCK);
    fsm.ovchargeBlock = true;
    PB_FSM_FireEvent(&fsm, EVT_SOC_OK);
    ASSERT(fsm.ovchargeBlock == false);
    ASSERT(fsm.nextState == STATE_IDLE);

    // User lock: IDLE + EVT_LOCK → SAFETY_LOCK, userLock set
    setup(); set_state(STATE_IDLE);
    fsm.userLock = false;
    PB_FSM_FireEvent(&fsm, EVT_LOCK);
    ASSERT(fsm.nextState == STATE_SAFETY_LOCK);
    ASSERT(fsm.userLock == true);

    // EVT_UNLOCK honored only for a user lock → IDLE
    setup(); set_state(STATE_SAFETY_LOCK);
    fsm.userLock = true;
    PB_FSM_FireEvent(&fsm, EVT_UNLOCK);
    ASSERT(fsm.nextState == STATE_IDLE);

    // EVT_UNLOCK on a low-SoC SAFETY_LOCK (userLock=false) → blocked
    setup(); set_state(STATE_SAFETY_LOCK);
    fsm.userLock = false;
    PB_FSM_FireEvent(&fsm, EVT_UNLOCK);
    ASSERT(fsm.nextState == STATE_SAFETY_LOCK);

    // MANUAL + EVT_LOCK → SAFETY_LOCK (lab mode can be locked too)
    setup(); set_state(STATE_MANUAL);
    PB_FSM_FireEvent(&fsm, EVT_LOCK);
    ASSERT(fsm.nextState == STATE_SAFETY_LOCK);

    // ERROR + BUTTON_SHORT → IDLE
    setup(); set_state(STATE_ERROR);
    PB_FSM_FireEvent(&fsm, EVT_BUTTON_SHORT);
    ASSERT(fsm.nextState == STATE_IDLE);

    // ERROR + ERROR_CLEAR → IDLE
    setup(); set_state(STATE_ERROR);
    PB_FSM_FireEvent(&fsm, EVT_ERROR_CLEAR);
    ASSERT(fsm.nextState == STATE_IDLE);

    // EMERGENCY, no transitions out
    setup(); set_state(STATE_EMERGENCY);
    PB_FSM_FireEvent(&fsm, EVT_BUTTON_SHORT);
    ASSERT(fsm.nextState == STATE_EMERGENCY);
    PB_FSM_FireEvent(&fsm, EVT_CHARGER_CONNECTED);
    ASSERT(fsm.nextState == STATE_EMERGENCY);
    PB_FSM_FireEvent(&fsm, EVT_SOC_OK);
    ASSERT(fsm.nextState == STATE_EMERGENCY);

    // LOW_V + SOC_OK → IDLE
    setup(); set_state(STATE_LOW_V);
    PB_FSM_FireEvent(&fsm, EVT_SOC_OK);
    ASSERT(fsm.nextState == STATE_IDLE);

    // LOW_V + SOC_UNDERV → EMERGENCY
    setup(); set_state(STATE_LOW_V);
    PB_FSM_FireEvent(&fsm, EVT_SOC_UNDERV);
    ASSERT(fsm.nextState == STATE_EMERGENCY);

    // MANUAL + MANUAL_EXIT → IDLE
    setup(); set_state(STATE_MANUAL);
    PB_FSM_FireEvent(&fsm, EVT_MANUAL_EXIT);
    ASSERT(fsm.nextState == STATE_IDLE);

    // SLEEP + BUTTON_SHORT → IDLE
    setup(); set_state(STATE_SLEEP);
    PB_FSM_FireEvent(&fsm, EVT_BUTTON_SHORT);
    ASSERT(fsm.nextState == STATE_IDLE);

    TEST_RESULT();
}
