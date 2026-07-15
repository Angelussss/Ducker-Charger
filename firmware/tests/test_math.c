#include "system/charge.h"
#include "framework.h"
#include <math.h>
#include <stdint.h>

// Not declared in charge.h, internal helpers exercised directly here
float toResistance(float vout);
float voltageToCurrentChannel(const uint8_t buffer[2]);

int main(void) {
    // toVoltage: raw / VMAX(4095) * VREF(3300mV)
    ASSERT(fabsf(toVoltage(0)    - 0.0f)    < 1.0f);
    ASSERT(fabsf(toVoltage(4095) - 3300.0f) < 1.0f);
    ASSERT(fabsf(toVoltage(2047) - 1650.0f) < 1.0f);

    // toResistance: R_PULLUP * Vout / (VREF - Vout); at Vout = VREF/2, R = R_PULLUP (10000)
    ASSERT(fabsf(toResistance(1650.0f) - 10000.0f) < 10.0f);

    // toCelsius: at R = R0 (10000), temperature should resolve to 25C
    ASSERT(fabsf(toCelsius(10000.0f) - 25.0f) < 0.1f);

    // voltageToCurrentChannel: buffer is MSB-first INA3221 shunt-voltage register.
    // 0x0100 = 256 raw -> >>3 (drop 3 reserved LSBs) = 32 counts
    // 32 * 0.04mV/count = 1.28mV; 1.28mV / 0.01ohm shunt = 128mA
    uint8_t buffer[2] = {0x01, 0x00};
    ASSERT(fabsf(voltageToCurrentChannel(buffer) - 128.0f) < 0.1f);

    TEST_RESULT();
}
