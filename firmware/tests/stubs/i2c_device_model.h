#ifndef I2C_DEVICE_MODEL_H
#define I2C_DEVICE_MODEL_H

#include "stm32_hal_stub.h"
#include <stdint.h>

// A registered device model intercepts all I2C reads/writes for a given address.
// HAL_I2C_Mem_Read/Write dispatch here first; if no model matches they fall
// back to the pre-queued response FIFO so existing tests are unaffected.
typedef struct I2cDeviceModel {
    uint16_t dev_addr;
    HAL_StatusTypeDef (*on_read) (uint16_t mem_addr, uint8_t       *buf, uint16_t len, void *ctx);
    HAL_StatusTypeDef (*on_write)(uint16_t mem_addr, const uint8_t *buf, uint16_t len, void *ctx);
    void *ctx;
} I2cDeviceModel;

// Register a model.  Call after stub_reset() since reset clears all models.
void            i2c_model_register(I2cDeviceModel *model);

// Clear all registered models (called automatically by stub_reset()).
void            i2c_model_reset_all(void);

// Return the model registered for dev_addr, or NULL.
I2cDeviceModel *i2c_model_find(uint16_t dev_addr);

#endif // I2C_DEVICE_MODEL_H
