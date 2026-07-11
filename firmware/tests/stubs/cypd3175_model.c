#include "cypd3175_model.h"
#include "stm32_hal_stub.h"

static HAL_StatusTypeDef cypd_on_read(uint16_t addr, uint8_t *buf, uint16_t len, void *ctx) {
    CYPD3175_Model *m = ctx;
    switch (addr) {
        case CYPD3175_INTR_REG:
            if (len >= 1) buf[0] = m->intr_reg;
            break;
        case CYPD3175_RESPONSE_REG:
            if (len >= 1) buf[0] = m->response_reg;
            break;
        case CYPD3175_PORT0_TYPE_C_STATUS:
            if (len >= 1) buf[0] = m->type_c_status;
            break;
        case CYPD3175_PORT0_CURRENT_PDO:
            if (len >= 4) memcpy(buf, m->pdo, 4);
            break;
        case CYPD3175_PORT0_CURRENT_RDO:
            if (len >= 4) memcpy(buf, m->rdo, 4);
            break;
        default:
            if (len > 0) memset(buf, 0, len);
            break;
    }
    return HAL_OK;
}

static HAL_StatusTypeDef cypd_on_write(uint16_t addr, const uint8_t *buf, uint16_t len, void *ctx) {
    CYPD3175_Model *m = ctx;
    if (addr == CYPD3175_INTR_REG && len >= 1) {
        // HPI write-to-clear: each 1-bit in buf[0] clears the corresponding INTR_REG bit.
        m->intr_reg &= ~buf[0];
        if (m->intr_reg == 0) {
            // All sources acknowledged — deassert ALERT# (CYPD3175_INT goes HIGH).
            stub_gpio_set(CYPD3175_INT_GPIO_Port, CYPD3175_INT_Pin, GPIO_PIN_SET);
        }
    }
    return HAL_OK;
}

void cypd_model_init(CYPD3175_Model *m) {
    m->_ifc.dev_addr = CYPD3175_PD_CONTROLLER_ADDR;
    m->_ifc.on_read  = cypd_on_read;
    m->_ifc.on_write = cypd_on_write;
    m->_ifc.ctx      = m;
    i2c_model_register(&m->_ifc);
}

void cypd_model_inject_event(CYPD3175_Model *m, uint8_t event_code,
                              const uint8_t pdo[4], const uint8_t rdo[4]) {
    m->response_reg  = event_code;
    m->intr_reg     |= CYPD3175_INTR_PORT0_BIT;
    if (pdo) memcpy(m->pdo, pdo, 4);
    if (rdo) memcpy(m->rdo, rdo, 4);
    // ALERT# goes low — in hardware this triggers the EXTI ISR which calls the handler.
    stub_gpio_set(CYPD3175_INT_GPIO_Port, CYPD3175_INT_Pin, GPIO_PIN_RESET);
}
