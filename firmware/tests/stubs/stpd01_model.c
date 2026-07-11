#include "stpd01_model.h"

static HAL_StatusTypeDef stpd01_on_read(uint16_t addr, uint8_t *buf, uint16_t len, void *ctx) {
    STPD01_Model *m = ctx;
    if (len < 1) return HAL_OK;
    switch (addr) {
        case VOUT_REG_ADDR:           buf[0] = m->vout_reg;           break;
        case ILIM_REG_ADDR:           buf[0] = m->ilim_reg;           break;
        case INT_STAT_REG_ADDR:       buf[0] = m->int_stat_reg;       break;
        case DIGITAL_ENABLE_REG_ADDR: buf[0] = m->digital_enable_reg; break;
        default:                      buf[0] = 0;                     break;
    }
    return HAL_OK;
}

static HAL_StatusTypeDef stpd01_on_write(uint16_t addr, const uint8_t *buf, uint16_t len, void *ctx) {
    STPD01_Model *m = ctx;
    if (len < 1) return HAL_OK;
    switch (addr) {
        case VOUT_REG_ADDR:           m->vout_reg           = buf[0]; break;
        case ILIM_REG_ADDR:           m->ilim_reg           = buf[0]; break;
        case DIGITAL_ENABLE_REG_ADDR: m->digital_enable_reg = buf[0]; break;
        // INT_STAT is read-only from firmware's side — writes are ignored
    }
    return HAL_OK;
}

void stpd01_model_init(STPD01_Model *m) {
    m->int_stat_reg  = 0x08; // powerOn bit only — no faults
    m->_ifc.dev_addr = STPD01_PD_ADDR;
    m->_ifc.on_read  = stpd01_on_read;
    m->_ifc.on_write = stpd01_on_write;
    m->_ifc.ctx      = m;
    i2c_model_register(&m->_ifc);
}

void stpd01_model_set_fault(STPD01_Model *m, uint8_t int_stat_bits) {
    m->int_stat_reg = int_stat_bits;
}

void stpd01_model_set_healthy(STPD01_Model *m) {
    m->int_stat_reg = 0x08;
}
