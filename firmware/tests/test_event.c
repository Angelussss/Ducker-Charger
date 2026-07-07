#include "system/event.h"
#include "framework.h"

static void drain(void) {
    Event_t e;
    while (event_pop(&e));
}

int main(void) {
    Event_t e;

    // Push 3 events, pop in FIFO order
    drain();
    event_push(EVT_CHARGER_CONNECTED);
    event_push(EVT_SOC_SAFETY);
    event_push(EVT_FAULT_OT);
    ASSERT(event_pop(&e) && e == EVT_CHARGER_CONNECTED);
    ASSERT(event_pop(&e) && e == EVT_SOC_SAFETY);
    ASSERT(event_pop(&e) && e == EVT_FAULT_OT);
    ASSERT(!event_pop(&e));

    // Pop from empty queue returns false
    drain();
    ASSERT(!event_pop(&e));

    // event_pending: false when empty, true when not
    drain();
    ASSERT(!event_pending());
    event_push(EVT_BUTTON_SHORT);
    ASSERT(event_pending());
    event_pop(&e);
    ASSERT(!event_pending());

    // Queue holds exactly 8 slots; 9th push is silently dropped
    drain();
    for (int i = 0; i < 8; i++) event_push(EVT_ERROR);
    event_push(EVT_CHARGER_CONNECTED); // must be dropped
    for (int i = 0; i < 8; i++) {
        ASSERT(event_pop(&e) && e == EVT_ERROR);
    }
    ASSERT(!event_pop(&e)); // EVT_CHARGER_CONNECTED was never stored

    // Queue wraps around correctly after partial drain + refill
    drain();
    for (int i = 0; i < 6; i++) event_push(EVT_INACTIVITY);
    for (int i = 0; i < 6; i++) event_pop(&e);
    event_push(EVT_BUTTON_LONG);
    event_push(EVT_MANUAL_ENTER);
    ASSERT(event_pop(&e) && e == EVT_BUTTON_LONG);
    ASSERT(event_pop(&e) && e == EVT_MANUAL_ENTER);
    ASSERT(!event_pop(&e));

    TEST_RESULT();
}
