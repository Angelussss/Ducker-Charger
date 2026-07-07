// Integration tests for the CYPD3175 HPI interrupt path.
//
// Unlike test_cypd3175.c (which pre-queues canned byte responses), these tests
// use live device models that respond to firmware I2C reads/writes exactly as
// hardware would.  Each test can therefore verify:
//   - Model register state after the handler runs (INTR_REG = 0 after ACK)
//   - ALERT# GPIO lifecycle (LOW when event pending, HIGH after ACK)
//   - STPD01 VOUT/ILIM register values written by setupSTPD01()
//   - That no spurious I2C reads target the wrong device
//
// setupSTPD01() clamps current to 3 A max.  For a 20 V / 5 A PDO:
//   vout_reg = 0xC4 + (20000-11000)/200 = 0xC4 + 45 = 0xF1
//   ilim_reg = (3000-100)/100            = 29       = 0x1D

#include "system/charge.h"
#include "system/event.h"
#include "stubs/stm32_hal_stub.h"
#include "stubs/cypd3175_model.h"
#include "stubs/stpd01_model.h"
#include "framework.h"
#include <math.h>
#include <string.h>

// ---- Event helpers ----
static void drain(void) { Event_t e; while (event_pop(&e)); }
static bool pop_eq(Event_t expected) { Event_t e; return event_pop(&e) && (e == expected); }

// ---- Queue helpers for unmodelled devices (BQ34Z100, INA3221) ----
// These use the FIFO queue because no device model is registered for them.

static void queue_sensors_ok(void) {
    static const uint8_t z2[2] = {0,0};
    static const uint8_t z1[1] = {0};
    uint8_t volt[2]  = { 0x98, 0x3A }; // 15000 mV little-endian
    uint8_t soc[1]   = { 100 };
    uint8_t flags[2] = { 0, 0 };
    stub_i2c_queue(z2,   2, HAL_OK);
    stub_i2c_queue(z2,   2, HAL_OK);
    stub_i2c_queue(z1,   1, HAL_OK);
    stub_i2c_queue(volt, 2, HAL_OK);
    stub_i2c_queue(z1,   1, HAL_OK);
    stub_i2c_queue(z2,   2, HAL_OK);
    stub_i2c_queue(z2,   2, HAL_OK);
    stub_i2c_queue(soc,  1, HAL_OK);
    stub_i2c_queue(z2,   2, HAL_OK);
    stub_i2c_queue(z2,   2, HAL_OK);
    stub_i2c_queue(z2,   2, HAL_OK);
    stub_i2c_queue(z2,   2, HAL_OK);
    stub_i2c_queue(flags,2, HAL_OK);
}

static void queue_ina_ok(void) {
    static const uint8_t z2[2] = {0,0};
    stub_i2c_queue(z2, 2, HAL_OK);
    stub_i2c_queue(z2, 2, HAL_OK);
    stub_i2c_queue(z2, 2, HAL_OK);
    stub_i2c_queue(z2, 2, HAL_OK);
}

// PDO: 20 V (400×50mV), 5 A max (500×10mA).
// RDO: 3 A operating (300×10mA).
static const uint8_t PDO_20V_5A[4] = { 0xF4, 0x41, 0x06, 0x00 };
static const uint8_t RDO_3A[4]     = { 0x00, 0xB0, 0x04, 0x00 };

// ---- Test fixture ----
// Resets all stub state, registers models, primes charge.c statics.
static void test_setup(CYPD3175_Model *cypd, STPD01_Model *stpd) {
    stub_reset(); // clears queue, write logs, GPIO, AND model registry
    drain();

    // Register device models — all CYPD3175/STPD01 I2C traffic goes through these.
    memset(cypd, 0, sizeof(*cypd));
    cypd_model_init(cypd);

    memset(stpd, 0, sizeof(*stpd));
    stpd01_model_init(stpd); // sets int_stat = 0x08 (powerOn, healthy)

    // GPIO defaults: all interrupt lines inactive (HIGH)
    stub_gpio_set(GPIOB, USB_IRQ_CTRL_Pin,     GPIO_PIN_SET);
    stub_gpio_set(GPIOC, STPD01_INT_Pin,  GPIO_PIN_SET);
    stub_gpio_set(GPIOC, CYPD3175_INT_Pin,      GPIO_PIN_SET); // ALERT# deasserted
    stub_gpio_set(GPIOB, USB_CHRG_OK_CTRL_Pin, GPIO_PIN_SET);

    // Prime charge.c statics (readSensors/readINA/readCS use queue; CYPD3175/STPD01 use models)
    queue_sensors_ok();
    readSensors(); drain();
    queue_ina_ok();
    readINA(); drain();
    readCS(); drain();

    stub_gpio_write_count = 0;
    stub_i2c_write_count  = 0;
}

// Drive contract to connected+negotiated state (isPlugged=true, isNegotiationDone=true).
static void prime_connected(CYPD3175_Model *cypd) {
    cypd_model_inject_event(cypd, CYPD3175_EVT_CONTRACT_COMPLETE, PDO_20V_5A, RDO_3A);
    secondaryUSBC_ConnectionINT();
    drain();
    stub_gpio_write_count = 0;
    stub_i2c_write_count  = 0;
    // Restore ALERT# to deasserted after the clear (model handles this but explicit is clearer)
    stub_gpio_set(GPIOC, CYPD3175_INT_Pin, GPIO_PIN_SET);
}

static bool alert_is_low(void) {
    return HAL_GPIO_ReadPin(CYPD3175_INT_GPIO_Port, CYPD3175_INT_Pin) == GPIO_PIN_RESET;
}
static bool alert_is_high(void) {
    return HAL_GPIO_ReadPin(CYPD3175_INT_GPIO_Port, CYPD3175_INT_Pin) == GPIO_PIN_SET;
}

// ---- Tests ----
int main(void) {
    CYPD3175_Model cypd;
    STPD01_Model   stpd;
    Event_t e;

    // 1. ALERT# lifecycle for OVP: pin goes LOW on inject, HIGH after INTR_REG is ACKed.
    test_setup(&cypd, &stpd);
    ASSERT(alert_is_high()); // quiet before event
    cypd_model_inject_event(&cypd, CYPD3175_EVT_OVP, NULL, NULL);
    ASSERT(alert_is_low());  // ALERT# asserted by model
    secondaryUSBC_ConnectionINT();
    ASSERT(alert_is_high()); // handler wrote INTR_REG → model deasserted ALERT#
    ASSERT(cypd.intr_reg == 0);
    ASSERT(pop_eq(EVT_FAULT_CRITICAL));
    ASSERT(!event_pop(&e));

    // 2. OCP: same lifecycle, different event code.
    test_setup(&cypd, &stpd);
    cypd_model_inject_event(&cypd, CYPD3175_EVT_OCP, NULL, NULL);
    ASSERT(alert_is_low());
    secondaryUSBC_ConnectionINT();
    ASSERT(alert_is_high());
    ASSERT(cypd.intr_reg == 0);
    ASSERT(pop_eq(EVT_FAULT_CRITICAL));
    ASSERT(!event_pop(&e));

    // 3. OTP: ALERT# cleared, EVT_FAULT_OT, isNegotiationDone cleared.
    test_setup(&cypd, &stpd);
    prime_connected(&cypd);
    ASSERT(getSecondaryUSBC_Contract().isNegotiationDone);
    cypd_model_inject_event(&cypd, CYPD3175_EVT_OTP, NULL, NULL);
    secondaryUSBC_ConnectionINT();
    ASSERT(alert_is_high());
    ASSERT(cypd.intr_reg == 0);
    ASSERT(pop_eq(EVT_FAULT_OT));
    ASSERT(!getSecondaryUSBC_Contract().isNegotiationDone);

    // 4. HARD_RESET: ALERT# cleared, no event, isNegotiationDone cleared, isPlugged unchanged.
    test_setup(&cypd, &stpd);
    prime_connected(&cypd);
    cypd_model_inject_event(&cypd, CYPD3175_EVT_HARD_RESET, NULL, NULL);
    secondaryUSBC_ConnectionINT();
    ASSERT(alert_is_high());
    ASSERT(cypd.intr_reg == 0);
    ASSERT(!event_pop(&e));
    ASSERT(!getSecondaryUSBC_Contract().isNegotiationDone);
    ASSERT(getSecondaryUSBC_Contract().isPlugged); // HARD_RESET does not unplug

    // 5. DISCONNECT: ALERT# cleared, isPlugged set to false.
    test_setup(&cypd, &stpd);
    prime_connected(&cypd);
    ASSERT(getSecondaryUSBC_Contract().isPlugged);
    cypd_model_inject_event(&cypd, CYPD3175_EVT_DISCONNECT, NULL, NULL);
    secondaryUSBC_ConnectionINT();
    ASSERT(alert_is_high());
    ASSERT(cypd.intr_reg == 0);
    ASSERT(!event_pop(&e));
    ASSERT(!getSecondaryUSBC_Contract().isPlugged);

    // 6. CONTRACT_COMPLETE + healthy STPD01: verify full register round-trip.
    //    setupSTPD01() must write correct VOUT and ILIM values to the STPD01 model.
    //    Expected (20 V PDO, 5 A clamped to 3 A by STPD01 max):
    //      vout_reg = 0xC4 + (20000-11000)/200 = 0xF1
    //      ilim_reg = (3000-100)/100            = 0x1D
    test_setup(&cypd, &stpd);
    cypd_model_inject_event(&cypd, CYPD3175_EVT_CONTRACT_COMPLETE, PDO_20V_5A, RDO_3A);
    secondaryUSBC_ConnectionINT();
    ASSERT(alert_is_high());
    ASSERT(cypd.intr_reg == 0);
    ASSERT(!event_pop(&e));
    {
        PDContract c = getSecondaryUSBC_Contract();
        ASSERT(c.isPlugged);
        ASSERT(c.isNegotiationDone);
        ASSERT(fabsf(c.voltage          - 20000.0f) < 1.0f);
        ASSERT(fabsf(c.maxCurrent       - 5000.0f)  < 1.0f);
        ASSERT(fabsf(c.operatingCurrent - 3000.0f)  < 1.0f);
    }
    // STPD01 model captured the register writes from setupSTPD01()
    ASSERT(stpd.vout_reg == 0xF1); // 20 V
    ASSERT(stpd.ilim_reg == 0x1D); // 3 A (clamped)
    // USB-C2 must be enabled (last GPIO write = GPIOA/PIN_12/SET)
    {
        bool usbc2_enabled = false;
        for (int i = 0; i < stub_gpio_write_count; i++) {
            if (stub_gpio_write_log[i].port->id == GPIOA->id &&
                stub_gpio_write_log[i].pin      == USB_C2_EN_Pin &&
                stub_gpio_write_log[i].state    == GPIO_PIN_SET)
                usbc2_enabled = true;
        }
        ASSERT(usbc2_enabled);
    }

    // 7. CONTRACT_COMPLETE + STPD01 OVP fault: EVT_FAULT_CRITICAL, ALERT# still cleared.
    test_setup(&cypd, &stpd);
    stpd01_model_set_fault(&stpd, 0x01); // OVP bit
    cypd_model_inject_event(&cypd, CYPD3175_EVT_CONTRACT_COMPLETE, PDO_20V_5A, RDO_3A);
    secondaryUSBC_ConnectionINT();
    ASSERT(alert_is_high()); // INTR_REG was ACKed regardless of STPD01 fault
    ASSERT(cypd.intr_reg == 0);
    ASSERT(pop_eq(EVT_FAULT_CRITICAL));
    ASSERT(!event_pop(&e));
    // STPD01 register was still written before the fault was detected
    ASSERT(stpd.vout_reg == 0xF1);
    ASSERT(stpd.ilim_reg == 0x1D);

    // 8. CONTRACT_COMPLETE + STPD01 OTP fault: EVT_FAULT_OT.
    test_setup(&cypd, &stpd);
    stpd01_model_set_fault(&stpd, 0x20); // OTP bit
    cypd_model_inject_event(&cypd, CYPD3175_EVT_CONTRACT_COMPLETE, PDO_20V_5A, RDO_3A);
    secondaryUSBC_ConnectionINT();
    ASSERT(alert_is_high());
    ASSERT(cypd.intr_reg == 0);
    ASSERT(pop_eq(EVT_FAULT_OT));
    ASSERT(!event_pop(&e));

    // 9. Back-to-back interrupts: two EXTI firings with independent ACK cycles.
    //    After the first handler runs, ALERT# must go high (first ACK done).
    //    After inject of second event, it goes low again.
    test_setup(&cypd, &stpd);
    cypd_model_inject_event(&cypd, CYPD3175_EVT_OVP, NULL, NULL);
    ASSERT(alert_is_low());
    secondaryUSBC_ConnectionINT();
    ASSERT(alert_is_high()); // first interrupt fully ACKed
    ASSERT(cypd.intr_reg == 0);

    cypd_model_inject_event(&cypd, CYPD3175_EVT_OCP, NULL, NULL);
    ASSERT(alert_is_low()); // second interrupt pending
    secondaryUSBC_ConnectionINT();
    ASSERT(alert_is_high()); // second interrupt ACKed
    ASSERT(cypd.intr_reg == 0);

    ASSERT(pop_eq(EVT_FAULT_CRITICAL)); // OVP
    ASSERT(pop_eq(EVT_FAULT_CRITICAL)); // OCP
    ASSERT(!event_pop(&e));

    // 10. Stale interrupt (intr_reg=0 on read): handler writes 0x00 back, ALERT# stays high.
    //     Simulates the MCU waking from STOP with NVIC pending but no new event.
    test_setup(&cypd, &stpd);
    // Do NOT inject — intr_reg stays 0, ALERT# stays high.
    secondaryUSBC_ConnectionINT(); // called with no pending interrupt
    ASSERT(alert_is_high());
    ASSERT(cypd.intr_reg == 0);
    ASSERT(!event_pop(&e));
    // The write-to-clear write still happened (writes 0x00 to INTR_REG)
    ASSERT(stub_i2c_write_count >= 1);
    ASSERT(stub_i2c_write_log[0].mem_addr == CYPD3175_INTR_REG);
    ASSERT(stub_i2c_write_log[0].data[0]  == 0x00);

    TEST_RESULT();
}
