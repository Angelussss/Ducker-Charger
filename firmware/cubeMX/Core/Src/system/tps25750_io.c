#include "system/tps25750_io.h"
#include "system/defines.h"
#include "i2c.h"        /* hi2c3 (I2C_PD) */
#include <string.h>

#define TPS25750_IO_TIMEOUT_MS  (50u)

HAL_StatusTypeDef tps25750_read(uint8_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t raw[65];

    if (len > sizeof(raw) - 1u)
        return HAL_ERROR;

    if (HAL_I2C_Master_Transmit(&hi2c3, TPS25750_PD_CONTROLLER_ADDR, &reg, 1u,
                                TPS25750_IO_TIMEOUT_MS) != HAL_OK)
        return HAL_ERROR;
    if (HAL_I2C_Master_Receive(&hi2c3, TPS25750_PD_CONTROLLER_ADDR, raw,
                               (uint16_t)(len + 1u),
                               TPS25750_IO_TIMEOUT_MS) != HAL_OK)
        return HAL_ERROR;

    /* raw[0] = payload length as reported by the device */
    memcpy(buf, &raw[1], len);
    return HAL_OK;
}

HAL_StatusTypeDef tps25750_write(uint8_t reg, const uint8_t *data, uint8_t len)
{
    uint8_t raw[66];

    if (len > sizeof(raw) - 2u)
        return HAL_ERROR;

    raw[0] = reg;
    raw[1] = len;
    memcpy(&raw[2], data, len);
    return HAL_I2C_Master_Transmit(&hi2c3, TPS25750_PD_CONTROLLER_ADDR, raw,
                                   (uint16_t)(len + 2u),
                                   TPS25750_IO_TIMEOUT_MS);
}
