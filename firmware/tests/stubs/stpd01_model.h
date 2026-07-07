#ifndef STPD01_MODEL_H
#define STPD01_MODEL_H

#include "i2c_device_model.h"
#include "system/defines.h"

// Software model of the STPD01 buck converter as seen over I2C.
// Writes to VOUT/ILIM/DIGITAL_ENABLE are captured in the struct fields so tests
// can verify that the firmware computed the correct register values.
// INT_STAT is read-only from firmware's perspective; set it with the helpers below.
typedef struct {
    uint8_t vout_reg;           // VOUT_REG_ADDR (0x00): set by setupSTPD01()
    uint8_t ilim_reg;           // ILIM_REG_ADDR (0x01): set by setupSTPD01()
    uint8_t int_stat_reg;       // INT_STAT_REG_ADDR (0x02): returned to checkSTPD01()
    uint8_t digital_enable_reg; // DIGITAL_ENABLE_REG_ADDR (0x06)
    I2cDeviceModel _ifc;
} STPD01_Model;

// Zero-fill m, set int_stat to healthy (0x08 = powerOn), wire up I2C dispatch.
void stpd01_model_init(STPD01_Model *m);

// Inject fault bits into INT_STAT: firmware's checkSTPD01() will read this.
//   0x01 = OVP, 0x04 = SCP, 0x08 = powerOn, 0x20 = OTP, 0x80 = ILIM
void stpd01_model_set_fault(STPD01_Model *m, uint8_t int_stat_bits);

// Restore healthy state (powerOn only, no fault bits).
void stpd01_model_set_healthy(STPD01_Model *m);

#endif // STPD01_MODEL_H
