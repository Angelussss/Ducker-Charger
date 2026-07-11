#ifndef CYPD3175_MODEL_H
#define CYPD3175_MODEL_H

#include "i2c_device_model.h"
#include "system/defines.h"
#include <string.h>

// Software model of the CYPD3175 (CCG3PA) as seen over I2C.
// Reads return register state; writes to INTR_REG apply the HPI write-to-clear
// semantics (bits written 1 are cleared), and deassert ALERT# when all bits are 0.
typedef struct {
    uint8_t intr_reg;       // CYPD3175_INTR_REG (0x0006)
    uint8_t response_reg;   // CYPD3175_RESPONSE_REG (0x007E): current event code
    uint8_t type_c_status;  // CYPD3175_PORT0_TYPE_C_STATUS (0x100C)
    uint8_t pdo[4];         // CYPD3175_PORT0_CURRENT_PDO (0x1010)
    uint8_t rdo[4];         // CYPD3175_PORT0_CURRENT_RDO (0x1014)
    I2cDeviceModel _ifc;    // must be last — registered pointer into this struct
} CYPD3175_Model;

// Zero-fill m, wire up I2C dispatch, and register on the bus.
void cypd_model_init(CYPD3175_Model *m);

// Inject a port-level event:
//   - loads event_code into RESPONSE_REG
//   - sets INTR_REG port0 bit (0x02)
//   - optionally loads pdo[4] / rdo[4] (pass NULL to leave unchanged)
//   - asserts ALERT# by pulling CYPD3175_INT_Pin LOW (simulating hardware interrupt)
void cypd_model_inject_event(CYPD3175_Model *m, uint8_t event_code,
                              const uint8_t pdo[4], const uint8_t rdo[4]);

#endif // CYPD3175_MODEL_H
