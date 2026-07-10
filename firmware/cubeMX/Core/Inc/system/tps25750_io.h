/**
 * @file    tps25750_io.h
 * @brief   Length-prefixed register I/O for the TPS25750 host interface.
 *
 * Every TPS25750 host-interface transaction carries a length prefix on
 * the wire: a register read returns [len][data...] and a register write
 * is [reg][len][data...]. HAL_I2C_Mem_* cannot produce this framing, so
 * all TPS25750 register access must go through these helpers (I2C3).
 */
#ifndef TPS25750_IO_H
#define TPS25750_IO_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

/** Read a host-interface register; buf receives len payload bytes. */
HAL_StatusTypeDef tps25750_read(uint8_t reg, uint8_t *buf, uint8_t len);

/** Write len payload bytes to a host-interface register. */
HAL_StatusTypeDef tps25750_write(uint8_t reg, const uint8_t *data, uint8_t len);

#endif /* TPS25750_IO_H */
