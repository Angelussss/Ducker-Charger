#include "system/charge.h"
#include "system/event.h"
#include "system/fsm.h"
#include "stubs/stm32_hal_stub.h"
#include "framework.h"
#include <math.h>

// ---- Event helpers ----
static void drain(void) {
    Event_t e;
    while (event_pop(&e));
}

static bool pop_eq(Event_t expected) {
    Event_t e;
    return event_pop(&e) && (e == expected);
}

// ---- I2C helpers ----

// Prime 13 reads consumed by one readSensors() call (SoC=100, voltage=15000mV, no flags).
static void queue_sensors_ok(void) {
    static const uint8_t zeros2[2] = {0, 0};
    static const uint8_t zero1[1]  = {0};
    uint8_t volt[2]  = { 0x98, 0x3A }; // 15000 = 0x3A98, little-endian
    uint8_t soc1[1]  = { 100 };
    uint8_t flags[2] = { 0, 0 };
    stub_i2c_queue(zeros2, 2, HAL_OK); // internal temperature
    stub_i2c_queue(zeros2, 2, HAL_OK); // external temperature
    stub_i2c_queue(zero1,  1, HAL_OK); // voltage scale
    stub_i2c_queue(volt,   2, HAL_OK); // voltage
    stub_i2c_queue(zero1,  1, HAL_OK); // current scale
    stub_i2c_queue(zeros2, 2, HAL_OK); // current
    stub_i2c_queue(zeros2, 2, HAL_OK); // avg current
    stub_i2c_queue(soc1,   1, HAL_OK); // SoC
    stub_i2c_queue(zeros2, 2, HAL_OK); // avg time to empty
    stub_i2c_queue(zeros2, 2, HAL_OK); // avg time to full
    stub_i2c_queue(zeros2, 2, HAL_OK); // cycle count
    stub_i2c_queue(zeros2, 2, HAL_OK); // state of health
    stub_i2c_queue(flags,  2, HAL_OK); // FLAGS
}

// Prime 4 reads consumed by one readINA() call (no alerts).
static void queue_ina_ok(void) {
    static const uint8_t zeros2[2] = {0, 0};
    stub_i2c_queue(zeros2, 2, HAL_OK);
    stub_i2c_queue(zeros2, 2, HAL_OK);
    stub_i2c_queue(zeros2, 2, HAL_OK);
    stub_i2c_queue(zeros2, 2, HAL_OK);
}

// Queue INTR_REG (port0 bit set) + RESPONSE_REG for one CYPD3175 event.
static void queue_cypd_event(uint8_t event_code) {
    uint8_t intr[1] = { CYPD3175_INTR_PORT0_BIT };
    uint8_t ev[1]   = { event_code };
    stub_i2c_queue(intr, 1, HAL_OK);
    stub_i2c_queue(ev,   1, HAL_OK);
}

// PDO encoding: 20 V (voltageRaw=400, 400×50mV), 5 A max (maxCurrentRaw=500, 500×10mA).
// Bit layout from charge.c:
//   voltageRaw    = ((buf[2] & 0x0F) << 6) | ((buf[1] >> 2) & 0x3F)
//   maxCurrentRaw = ((buf[1] & 0x03) << 8) | buf[0]
static const uint8_t PDO_20V_5A[4] = { 0xF4, 0x41, 0x06, 0x00 };

// RDO encoding: 3 A operating (opCurrentRaw=300, 300×10mA).
// Bit layout: opCurrentRaw = ((buf[2] & 0x0F) << 6) | ((buf[1] >> 2) & 0x3F)
static const uint8_t RDO_3A[4] = { 0x00, 0xB0, 0x04, 0x00 };

// Queue a full CONTRACT_COMPLETE read sequence:
//   INTR + RESPONSE + PDO (4 bytes) + RDO (4 bytes) + STPD01 INT_STAT (1 byte).
static void queue_contract_complete(uint8_t stpd01_int_stat) {
    uint8_t stpd[1] = { stpd01_int_stat };
    queue_cypd_event(CYPD3175_EVT_CONTRACT_COMPLETE);
    stub_i2c_queue(PDO_20V_5A, 4, HAL_OK);
    stub_i2c_queue(RDO_3A,     4, HAL_OK);
    stub_i2c_queue(stpd,       1, HAL_OK);
}

// ---- Per-test setup ----
// Resets stub state and primes all statics in charge.c to a known baseline.
static void test_setup(void) {
    stub_reset();
    drain();
    stub_gpio_set(GPIOB, USB_IRQ_CTRL_Pin,     GPIO_PIN_SET);
    stub_gpio_set(GPIOC, STPD01_INT_Pin,  GPIO_PIN_SET);
    stub_gpio_set(GPIOC, CYPD3175_INT_Pin,      GPIO_PIN_SET);
    stub_gpio_set(GPIOB, USB_CHRG_OK_CTRL_Pin, GPIO_PIN_SET);
    queue_sensors_ok();
    readSensors();
    drain();
    queue_ina_ok();
    readINA();
    drain();
    readCS();
    drain();
    stub_gpio_write_count  = 0;
    stub_i2c_write_count   = 0;
}

// Drive secondaryUSBC_PDContract into isPlugged=true, isNegotiationDone=true
// via a successful CONTRACT_COMPLETE, then clear both write logs.
static void prime_connected(void) {
    queue_contract_complete(0x08); // 0x08 = powerOn bit only → healthy STPD01
    secondaryUSBC_ConnectionINT();
    drain();
    stub_gpio_write_count = 0;
    stub_i2c_write_count  = 0;
}

// ---- Tests ----
int main(void) {
    Event_t e;

    // 1. INTR_REG I2C read failure → EVT_ERROR, return immediately
    test_setup();
    {
        uint8_t dummy[1] = {0};
        stub_i2c_queue(dummy, 1, HAL_ERROR);
    }
    secondaryUSBC_ConnectionINT();
    ASSERT(pop_eq(EVT_ERROR));
    ASSERT(!event_pop(&e));

    // 2. INTR_REG port0 bit not set → silent return, no event
    test_setup();
    {
        uint8_t intr[1] = { 0x01 }; // only dev bit (bit0), not port0 bit (bit1)
        stub_i2c_queue(intr, 1, HAL_OK);
    }
    secondaryUSBC_ConnectionINT();
    ASSERT(!event_pop(&e));

    // 3. RESPONSE_REG I2C read failure → EVT_ERROR
    test_setup();
    {
        uint8_t intr[1]  = { CYPD3175_INTR_PORT0_BIT };
        uint8_t dummy[1] = {0};
        stub_i2c_queue(intr,  1, HAL_OK);
        stub_i2c_queue(dummy, 1, HAL_ERROR);
    }
    secondaryUSBC_ConnectionINT();
    ASSERT(pop_eq(EVT_ERROR));
    ASSERT(!event_pop(&e));

    // 4. OVP event (0x83) → EVT_FAULT_CRITICAL, USB-C2 and STPD01 disabled
    test_setup();
    queue_cypd_event(CYPD3175_EVT_OVP);
    secondaryUSBC_ConnectionINT();
    ASSERT(pop_eq(EVT_FAULT_CRITICAL));
    ASSERT(!event_pop(&e));
    ASSERT(stub_gpio_write_count >= 2);
    ASSERT(stub_gpio_write_log[0].port->id == GPIOA->id);
    ASSERT(stub_gpio_write_log[0].pin      == USB_C2_EN_Pin);
    ASSERT(stub_gpio_write_log[0].state    == GPIO_PIN_RESET);
    ASSERT(stub_gpio_write_log[1].port->id == GPIOC->id);
    ASSERT(stub_gpio_write_log[1].pin      == USB_STPD01_EN_CTRL_Pin);
    ASSERT(stub_gpio_write_log[1].state    == GPIO_PIN_RESET);
    ASSERT(!getSecondaryUSBC_Contract().isPlugged);
    ASSERT(!getSecondaryUSBC_Contract().isNegotiationDone);

    // 5. OCP event (0x82) → EVT_FAULT_CRITICAL
    test_setup();
    queue_cypd_event(CYPD3175_EVT_OCP);
    secondaryUSBC_ConnectionINT();
    ASSERT(pop_eq(EVT_FAULT_CRITICAL));
    ASSERT(!event_pop(&e));

    // 6. OTP event (0xB6) → EVT_FAULT_OT, isNegotiationDone cleared
    test_setup();
    prime_connected();
    ASSERT(getSecondaryUSBC_Contract().isNegotiationDone);
    queue_cypd_event(CYPD3175_EVT_OTP);
    secondaryUSBC_ConnectionINT();
    ASSERT(pop_eq(EVT_FAULT_OT));
    ASSERT(!event_pop(&e));
    ASSERT(!getSecondaryUSBC_Contract().isNegotiationDone);

    // 7. HARD_RESET event (0x8F) → no event, isNegotiationDone cleared, isPlugged unchanged
    test_setup();
    prime_connected();
    ASSERT(getSecondaryUSBC_Contract().isPlugged);
    ASSERT(getSecondaryUSBC_Contract().isNegotiationDone);
    queue_cypd_event(CYPD3175_EVT_HARD_RESET);
    secondaryUSBC_ConnectionINT();
    ASSERT(!event_pop(&e));
    ASSERT(!getSecondaryUSBC_Contract().isNegotiationDone);
    ASSERT(getSecondaryUSBC_Contract().isPlugged); // HARD_RESET does not clear isPlugged
    ASSERT(stub_gpio_write_count >= 2);
    ASSERT(stub_gpio_write_log[0].port->id == GPIOA->id);
    ASSERT(stub_gpio_write_log[0].pin      == USB_C2_EN_Pin);
    ASSERT(stub_gpio_write_log[0].state    == GPIO_PIN_RESET);
    ASSERT(stub_gpio_write_log[1].port->id == GPIOC->id);
    ASSERT(stub_gpio_write_log[1].pin      == USB_STPD01_EN_CTRL_Pin);
    ASSERT(stub_gpio_write_log[1].state    == GPIO_PIN_RESET);

    // 8. DISCONNECT event (0x85) → no event, isPlugged and isNegotiationDone cleared
    test_setup();
    prime_connected();
    ASSERT(getSecondaryUSBC_Contract().isPlugged);
    queue_cypd_event(CYPD3175_EVT_DISCONNECT);
    secondaryUSBC_ConnectionINT();
    ASSERT(!event_pop(&e));
    ASSERT(!getSecondaryUSBC_Contract().isPlugged);
    ASSERT(!getSecondaryUSBC_Contract().isNegotiationDone);

    // 9. CONTRACT_COMPLETE, PDO read fails → EVT_ERROR
    test_setup();
    {
        uint8_t dummy[4] = {0};
        queue_cypd_event(CYPD3175_EVT_CONTRACT_COMPLETE);
        stub_i2c_queue(dummy, 4, HAL_ERROR); // PDO read fails
    }
    secondaryUSBC_ConnectionINT();
    ASSERT(pop_eq(EVT_ERROR));
    ASSERT(!event_pop(&e));

    // 10. CONTRACT_COMPLETE, RDO read fails → EVT_ERROR
    test_setup();
    {
        uint8_t dummy[4] = {0};
        queue_cypd_event(CYPD3175_EVT_CONTRACT_COMPLETE);
        stub_i2c_queue(PDO_20V_5A, 4, HAL_OK);
        stub_i2c_queue(dummy,      4, HAL_ERROR); // RDO read fails
    }
    secondaryUSBC_ConnectionINT();
    ASSERT(pop_eq(EVT_ERROR));
    ASSERT(!event_pop(&e));

    // 11. CONTRACT_COMPLETE, healthy STPD01 (INT_STAT=0x08, powerOn only)
    //     → no event, contract values correct (20V/5A/3A), USB-C2 enabled
    test_setup();
    queue_contract_complete(0x08);
    secondaryUSBC_ConnectionINT();
    ASSERT(!event_pop(&e));
    {
        PDContract c = getSecondaryUSBC_Contract();
        ASSERT(c.isPlugged);
        ASSERT(c.isNegotiationDone);
        ASSERT(fabsf(c.voltage          - 20000.0f) < 1.0f);
        ASSERT(fabsf(c.maxCurrent       - 5000.0f)  < 1.0f);
        ASSERT(fabsf(c.operatingCurrent - 3000.0f)  < 1.0f);
    }
    // GPIO sequence: [0]=STPD01 RESET (setupSTPD01), [1]=STPD01 SET, [2]=USBC2 SET
    ASSERT(stub_gpio_write_count >= 3);
    ASSERT(stub_gpio_write_log[0].port->id == GPIOC->id);
    ASSERT(stub_gpio_write_log[0].pin      == USB_STPD01_EN_CTRL_Pin);
    ASSERT(stub_gpio_write_log[0].state    == GPIO_PIN_RESET);
    ASSERT(stub_gpio_write_log[1].port->id == GPIOC->id);
    ASSERT(stub_gpio_write_log[1].pin      == USB_STPD01_EN_CTRL_Pin);
    ASSERT(stub_gpio_write_log[1].state    == GPIO_PIN_SET);
    ASSERT(stub_gpio_write_log[2].port->id == GPIOA->id);
    ASSERT(stub_gpio_write_log[2].pin      == USB_C2_EN_Pin);
    ASSERT(stub_gpio_write_log[2].state    == GPIO_PIN_SET);

    // 12. CONTRACT_COMPLETE, STPD01 OVP fault (INT_STAT=0x01, bit0) → EVT_FAULT_CRITICAL
    test_setup();
    queue_contract_complete(0x01); // OVP bit
    secondaryUSBC_ConnectionINT();
    ASSERT(pop_eq(EVT_FAULT_CRITICAL));
    ASSERT(!event_pop(&e));
    // STPD01 must have been disabled after the fault
    {
        bool found_stpd01_disable = false;
        for (int i = 0; i < stub_gpio_write_count; i++) {
            if (stub_gpio_write_log[i].port->id == GPIOC->id &&
                stub_gpio_write_log[i].pin      == USB_STPD01_EN_CTRL_Pin &&
                stub_gpio_write_log[i].state    == GPIO_PIN_RESET) {
                found_stpd01_disable = true;
            }
        }
        ASSERT(found_stpd01_disable);
    }

    // 13. CONTRACT_COMPLETE, STPD01 OTP fault (INT_STAT=0x20, bit5) → EVT_FAULT_OT
    test_setup();
    queue_contract_complete(0x20); // OTP bit
    secondaryUSBC_ConnectionINT();
    ASSERT(pop_eq(EVT_FAULT_OT));
    ASSERT(!event_pop(&e));

    // 14. Unknown event code → EVT_ERROR pushed, no state change
    test_setup();
    queue_cypd_event(0xAA);
    secondaryUSBC_ConnectionINT();
    ASSERT(pop_eq(EVT_ERROR));
    ASSERT(!event_pop(&e));

    // ---- HPI interrupt handshake: write-to-clear verification ----

    // 15. No port0 bit: handler must write back the original intrReg value to clear the
    //     device-level interrupt (HPI write-to-clear protocol).
    test_setup();
    {
        uint8_t intr[1] = { 0x01 }; // dev bit only (bit0), not port0
        stub_i2c_queue(intr, 1, HAL_OK);
    }
    secondaryUSBC_ConnectionINT();
    ASSERT(!event_pop(&e));
    // Exactly one I2C write: INTR_REG cleared with the same value that was read (0x01)
    ASSERT(stub_i2c_write_count >= 1);
    ASSERT(stub_i2c_write_log[0].dev_addr == CYPD3175_PD_CONTROLLER_ADDR);
    ASSERT(stub_i2c_write_log[0].mem_addr == CYPD3175_INTR_REG);
    ASSERT(stub_i2c_write_log[0].data[0]  == 0x01);

    // 16. Port0 event: handler must write CYPD3175_INTR_PORT0_BIT (0x02) to INTR_REG
    //     as the first I2C write, before any GPIO or further processing.
    test_setup();
    queue_cypd_event(CYPD3175_EVT_OVP);
    secondaryUSBC_ConnectionINT();
    ASSERT(pop_eq(EVT_FAULT_CRITICAL));
    ASSERT(stub_i2c_write_count >= 1);
    ASSERT(stub_i2c_write_log[0].dev_addr == CYPD3175_PD_CONTROLLER_ADDR);
    ASSERT(stub_i2c_write_log[0].mem_addr == CYPD3175_INTR_REG);
    ASSERT(stub_i2c_write_log[0].data[0]  == CYPD3175_INTR_PORT0_BIT);

    // 17. CONTRACT_COMPLETE clear: same write-to-clear handshake applies before
    //     PDO/RDO reads and STPD01 setup.
    test_setup();
    queue_contract_complete(0x08);
    secondaryUSBC_ConnectionINT();
    ASSERT(!event_pop(&e));
    // First write = INTR_REG clear with 0x02; subsequent writes = STPD01 VOUT and ILIM
    ASSERT(stub_i2c_write_count >= 3);
    ASSERT(stub_i2c_write_log[0].dev_addr == CYPD3175_PD_CONTROLLER_ADDR);
    ASSERT(stub_i2c_write_log[0].mem_addr == CYPD3175_INTR_REG);
    ASSERT(stub_i2c_write_log[0].data[0]  == CYPD3175_INTR_PORT0_BIT);
    // STPD01 register writes (VOUT then ILIM)
    ASSERT(stub_i2c_write_log[1].dev_addr == STPD01_PD_ADDR);
    ASSERT(stub_i2c_write_log[1].mem_addr == VOUT_REG_ADDR);
    ASSERT(stub_i2c_write_log[2].dev_addr == STPD01_PD_ADDR);
    ASSERT(stub_i2c_write_log[2].mem_addr == ILIM_REG_ADDR);

    // ---- Back-to-back interrupt simulation ----

    // 18. Two port0 events arrive in sequence (e.g., two EXTI firings).
    //     Each call must independently read, process, and clear INTR_REG.
    test_setup();
    queue_cypd_event(CYPD3175_EVT_OVP);  // first interrupt
    queue_cypd_event(CYPD3175_EVT_OCP);  // second interrupt
    secondaryUSBC_ConnectionINT();        // handles OVP
    secondaryUSBC_ConnectionINT();        // handles OCP
    ASSERT(pop_eq(EVT_FAULT_CRITICAL));   // from OVP
    ASSERT(pop_eq(EVT_FAULT_CRITICAL));   // from OCP
    ASSERT(!event_pop(&e));
    // Two separate INTR_REG clear writes (one per call)
    ASSERT(stub_i2c_write_count >= 2);
    ASSERT(stub_i2c_write_log[0].mem_addr == CYPD3175_INTR_REG);
    ASSERT(stub_i2c_write_log[0].data[0]  == CYPD3175_INTR_PORT0_BIT);
    ASSERT(stub_i2c_write_log[1].mem_addr == CYPD3175_INTR_REG);
    ASSERT(stub_i2c_write_log[1].data[0]  == CYPD3175_INTR_PORT0_BIT);

    // 19. Interrupt with no pending event (INTR_REG=0x00, stale EXTI).
    //     Handler writes back 0x00 to clear and returns without pushing any event.
    test_setup();
    {
        uint8_t intr[1] = { 0x00 };
        stub_i2c_queue(intr, 1, HAL_OK);
    }
    secondaryUSBC_ConnectionINT();
    ASSERT(!event_pop(&e));
    ASSERT(stub_i2c_write_count >= 1);
    ASSERT(stub_i2c_write_log[0].dev_addr == CYPD3175_PD_CONTROLLER_ADDR);
    ASSERT(stub_i2c_write_log[0].mem_addr == CYPD3175_INTR_REG);
    ASSERT(stub_i2c_write_log[0].data[0]  == 0x00);

    // ---- FSM gate on the auto-enable path ----

    // 20. CONTRACT_COMPLETE while the FSM is in SAFETY_LOCK: the event is
    //     acked and the contract stored, but STPD01/USB-C2 must NOT be
    //     re-enabled — the state just shut every output to protect the pack.
    test_setup();
    {
        FSM fsm;
        PB_FSM_Init(&fsm);                       // IDLE
        PB_FSM_FireEvent(&fsm, EVT_SOC_SAFETY);  // → SAFETY_LOCK
        PB_FSM_Update(&fsm);
        drain();
        stub_gpio_write_count = 0;
        stub_i2c_write_count  = 0;

        queue_cypd_event(CYPD3175_EVT_CONTRACT_COMPLETE);
        stub_i2c_queue(PDO_20V_5A, 4, HAL_OK);
        stub_i2c_queue(RDO_3A,     4, HAL_OK);
        secondaryUSBC_ConnectionINT();
        ASSERT(!event_pop(&e));
        ASSERT(getSecondaryUSBC_Contract().isPlugged);        // contract remembered
        ASSERT(!getSecondaryUSBC_Contract().isNegotiationDone); // but output not opened
        // INTR ack only — no STPD01 VOUT/ILIM writes, no GPIO enables
        ASSERT(stub_i2c_write_count == 1);
        ASSERT(stub_i2c_write_log[0].mem_addr == CYPD3175_INTR_REG);
        for (int i = 0; i < stub_gpio_write_count; i++)
            ASSERT(stub_gpio_write_log[i].state == GPIO_PIN_RESET);

        // 21. Same event with the FSM back in IDLE → output opens normally.
        PB_FSM_FireEvent(&fsm, EVT_SOC_OK);      // SAFETY_LOCK → IDLE
        PB_FSM_Update(&fsm);
        drain();
        stub_gpio_write_count = 0;
        stub_i2c_write_count  = 0;
        queue_contract_complete(0x08);
        secondaryUSBC_ConnectionINT();
        ASSERT(!event_pop(&e));
        ASSERT(getSecondaryUSBC_Contract().isNegotiationDone);
    }

    TEST_RESULT();
}
