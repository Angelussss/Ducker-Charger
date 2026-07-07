#include "system/charge.h"
#include "system/event.h"
#include "stubs/stm32_hal_stub.h"
#include "framework.h"

// ---- I2C response helpers ----

// Queue all 13 responses consumed by one readSensors() call.
// voltage_raw: raw uint16_t written to fuelGaugeSensors.voltage (mV, no scale)
// flags_hi / flags_lo: byte layout of the FLAGS register (0x0E): [0]=lo, [1]=hi
static void queue_sensor_read(uint8_t soc, uint8_t flags_hi, uint8_t flags_lo,
                               uint16_t voltage_raw) {
    static const uint8_t zeros2[2] = {0, 0};
    static const uint8_t zero1[1]  = {0};
    uint8_t volt[2]  = { voltage_raw & 0xFF, (voltage_raw >> 8) & 0xFF };
    uint8_t soc1[1]  = { soc };
    uint8_t flags[2] = { flags_lo, flags_hi }; // buffer[0]=lo byte, buffer[1]=hi byte

    stub_i2c_queue(zeros2, 2, HAL_OK); // internal temperature
    stub_i2c_queue(zeros2, 2, HAL_OK); // external temperature
    stub_i2c_queue(zero1,  1, HAL_OK); // voltage scale (0 → no scaling)
    stub_i2c_queue(volt,   2, HAL_OK); // voltage
    stub_i2c_queue(zero1,  1, HAL_OK); // current scale
    stub_i2c_queue(zeros2, 2, HAL_OK); // current
    stub_i2c_queue(zeros2, 2, HAL_OK); // avg current
    stub_i2c_queue(soc1,   1, HAL_OK); // SoC
    stub_i2c_queue(zeros2, 2, HAL_OK); // avg time to empty
    stub_i2c_queue(zeros2, 2, HAL_OK); // avg time to full
    stub_i2c_queue(zeros2, 2, HAL_OK); // cycle count
    stub_i2c_queue(zeros2, 2, HAL_OK); // state of health
    stub_i2c_queue(flags,  2, HAL_OK); // FLAGS register
}

// Queue all 4 responses consumed by one readINA() call.
// critical_alert bits 9/8 of MASK_ENABLE → ch1/ch2 alerts (big-endian register)
static void queue_ina_read(bool ch1_alert, bool ch2_alert) {
    static const uint8_t zeros2[2] = {0, 0};
    uint16_t mask = 0;
    if (ch1_alert) mask |= (1u << 9);
    if (ch2_alert) mask |= (1u << 8);
    uint8_t msk[2] = { (mask >> 8) & 0xFF, mask & 0xFF };

    stub_i2c_queue(zeros2, 2, HAL_OK); // CH1 shunt voltage
    stub_i2c_queue(zeros2, 2, HAL_OK); // CH2 shunt voltage
    stub_i2c_queue(zeros2, 2, HAL_OK); // CH3 shunt voltage
    stub_i2c_queue(msk,    2, HAL_OK); // MASK_ENABLE
}

// ---- Event queue helpers ----
static void drain(void) {
    Event_t e;
    while (event_pop(&e));
}

static bool pop_eq(Event_t expected) {
    Event_t e;
    return event_pop(&e) && (e == expected);
}

// ---- Per-test setup ----
// Resets all stub state and primes static edge-detection variables to a known
// baseline: SoC=100 (no SOC events), no flags, no INA alerts, CHRG_OK high.
static void test_setup(void) {
    stub_reset();
    drain();

    // Set all critical GPIO interrupt lines inactive (HIGH = no interrupt pending)
    stub_gpio_set(GPIOB, USB_IRQ_CTRL_Pin,      GPIO_PIN_SET); // PD_IRQ
    stub_gpio_set(GPIOC, STPD01_INT_Pin,   GPIO_PIN_SET); // STPD01_INT
    stub_gpio_set(GPIOC, CYPD3175_INT_Pin,       GPIO_PIN_SET); // CYPD3175_INT
    stub_gpio_set(GPIOB, USB_CHRG_OK_CTRL_Pin,  GPIO_PIN_SET); // CHRG_OK

    // Prime readSensors() statics: prevSoC=100, prevOT=false, prevBATHI=false, prevBATLOW=false
    queue_sensor_read(100, 0x00, 0x00, 15000); // 15000 mV > UNDERV_VOLTAGE_MV=12000
    readSensors();
    drain();

    // Prime readINA() statics: prevOCC1=false, prevOCC2=false
    queue_ina_read(false, false);
    readINA();
    drain();

    // Prime readCS() static: prevCHRG_OK=true (CHRG_OK pin is HIGH)
    readCS(); // PD_IRQ=HIGH → no I2C reads triggered
    drain();

    // Clear GPIO write log accumulated during priming
    stub_gpio_write_count = 0;
}

// ---- Tests ----

int main(void) {
    Event_t e;

    // SoC drops below SAFETY threshold (15%) → EVT_SOC_SAFETY
    test_setup();
    queue_sensor_read(14, 0x00, 0x00, 15000); // SoC=14 < 15, prev=100
    readSensors();
    ASSERT(pop_eq(EVT_SOC_SAFETY));
    ASSERT(!event_pop(&e));

    // Same SoC on consecutive call → no repeat event (edge detection)
    queue_sensor_read(14, 0x00, 0x00, 15000); // SoC still 14, prev=14
    readSensors();
    ASSERT(!event_pop(&e));

    // SoC drops below LOWV threshold (10%) → EVT_SOC_LOWV (not SOC_SAFETY)
    test_setup();
    queue_sensor_read(9, 0x00, 0x00, 15000); // SoC=9 < 10, prev=100
    readSensors();
    ASSERT(pop_eq(EVT_SOC_LOWV));
    ASSERT(!event_pop(&e));

    // SoC recovers above SOC_OK threshold (15%) → EVT_SOC_OK
    test_setup();
    queue_sensor_read(9, 0x00, 0x00, 15000); // drive prevSoC below threshold
    readSensors(); drain();
    queue_sensor_read(16, 0x00, 0x00, 15000); // 16 >= SOC_OK_THRESHOLD=15, prev=9 < 15
    readSensors();
    ASSERT(pop_eq(EVT_SOC_OK));
    ASSERT(!event_pop(&e));

    // BATHI flag set → EVT_SOC_OVCH
    // BATHI is bit5 of the FLAGS high byte: 0x20
    test_setup();
    queue_sensor_read(100, 0x20, 0x00, 15000);
    readSensors();
    ASSERT(pop_eq(EVT_SOC_OVCH));
    ASSERT(!event_pop(&e));

    // OTC flag set → EVT_FAULT_OT
    // OTC is bit7 of the FLAGS high byte: 0x80
    test_setup();
    queue_sensor_read(100, 0x80, 0x00, 15000);
    readSensors();
    ASSERT(pop_eq(EVT_FAULT_OT));
    ASSERT(!event_pop(&e));

    // BATLOW flag set → EVT_SOC_UNDERV
    // BATLOW is bit4 of the FLAGS high byte: 0x10
    test_setup();
    queue_sensor_read(100, 0x10, 0x00, 15000);
    readSensors();
    ASSERT(pop_eq(EVT_SOC_UNDERV));
    ASSERT(!event_pop(&e));

    // INA CH1 OCC → disable USB-A1 + EVT_FAULT_OCC
    test_setup();
    queue_ina_read(true, false);
    readINA();
    ASSERT(pop_eq(EVT_FAULT_OCC));
    ASSERT(!event_pop(&e));
    // USB-A1 must have been disabled (GPIO write: GPIOC, GPIO_PIN_1, RESET)
    ASSERT(stub_gpio_write_count >= 1);
    ASSERT(stub_gpio_write_log[0].port->id == GPIOC->id);
    ASSERT(stub_gpio_write_log[0].pin   == USB_A1_CTRL_Pin);
    ASSERT(stub_gpio_write_log[0].state == GPIO_PIN_RESET);

    // Same CH1 alert on next call → no repeat event (edge detection)
    queue_ina_read(true, false);
    readINA();
    ASSERT(!event_pop(&e));

    // INA CH2 OCC → disable USB-A2 + EVT_FAULT_OCC
    test_setup();
    queue_ina_read(false, true);
    readINA();
    ASSERT(pop_eq(EVT_FAULT_OCC));
    ASSERT(!event_pop(&e));
    ASSERT(stub_gpio_write_count >= 1);
    ASSERT(stub_gpio_write_log[0].port->id == GPIOC->id);
    ASSERT(stub_gpio_write_log[0].pin   == USB_A2_CTRL_Pin);
    ASSERT(stub_gpio_write_log[0].state == GPIO_PIN_RESET);

    // CHRG_OK falling edge → EVT_ERROR
    test_setup();
    stub_gpio_set(GPIOB, USB_CHRG_OK_CTRL_Pin, GPIO_PIN_RESET); // CHRG_OK goes LOW
    readCS();
    ASSERT(pop_eq(EVT_ERROR));
    ASSERT(!event_pop(&e));

    // CHRG_OK stays low → no repeat event
    readCS();
    ASSERT(!event_pop(&e));

    TEST_RESULT();
}
