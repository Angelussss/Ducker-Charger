/**
 * @file    dev_ina3221.c
 * @brief   INA3221 3-channel shunt monitor model @ I2C1 0x40.
 *
 * 16-bit registers, MSB first on the wire (datasheet). Shunt voltage
 * regs report the board model's port currents through the 10 mOhm
 * shunts (40 uV/LSB, value left-shifted by 3). CH3 tied to GND.
 */
#include "sim.h"

#include <string.h>

typedef struct {
    uint16_t config;
    uint8_t  reg_ptr;
    int      alert[3];      /* injected critical alerts */
} Ina;

static Ina in3;

static uint16_t shunt_reg(float current_ma)
{
    /* I -> Vshunt = I * 10 mOhm; counts = Vshunt / 40 uV; reg = counts << 3 */
    float vshunt_mv = current_ma * 0.01f;
    int counts = (int)(vshunt_mv / 0.04f);
    if (counts >  4095) counts =  4095;
    if (counts < -4096) counts = -4096;
    return (uint16_t)((int16_t)(counts << 3));
}

static uint16_t reg_read16(uint8_t r)
{
    int a1_oc = in3.alert[0] || board.a1_ma > 3000.0f;
    int a2_oc = in3.alert[1] || board.a2_ma > 3000.0f;
    switch (r) {
    case 0x00: return in3.config;
    case 0x01: return shunt_reg(board.a1_ma);
    case 0x03: return shunt_reg(board.a2_ma);
    case 0x05: return 0;                       /* CH3 grounded */
    case 0x0F: return (uint16_t)((a1_oc << 9) | (a2_oc << 8));
    default:   return 0;
    }
}

static void reg_write16(uint8_t r, uint16_t v)
{
    if (r == 0x00) {
        in3.config = v;
        sim_log("[INA ] CONFIG = 0x%04X", v);
    }
}

static int ina_mem_read(SimI2CDev *d, uint16_t mem, uint16_t ms,
                        uint8_t *buf, uint16_t n)
{
    (void)d; (void)ms;
    uint16_t v = reg_read16((uint8_t)mem);
    for (uint16_t i = 0; i < n; i++)
        buf[i] = (i == 0) ? (uint8_t)(v >> 8)
               : (i == 1) ? (uint8_t)(v & 0xFF) : 0;
    return 0;
}

static int ina_mem_write(SimI2CDev *d, uint16_t mem, uint16_t ms,
                         const uint8_t *buf, uint16_t n)
{
    (void)d; (void)ms;
    if (n >= 2)
        reg_write16((uint8_t)mem, (uint16_t)((buf[0] << 8) | buf[1]));
    return 0;
}

static int ina_mtx(SimI2CDev *d, const uint8_t *buf, uint16_t n)
{
    if (n == 0) return -1;
    in3.reg_ptr = buf[0];
    return (n >= 3) ? ina_mem_write(d, buf[0], 1, buf + 1, (uint16_t)(n - 1)) : 0;
}

static int ina_mrx(SimI2CDev *d, uint8_t *buf, uint16_t n)
{
    return ina_mem_read(d, in3.reg_ptr, 1, buf, n);
}

static SimI2CDev ina_dev = {
    .addr8 = (uint16_t)(0x40 << 1), .name = "INA3221",
    .mem_read = ina_mem_read, .mem_write = ina_mem_write,
    .mtx = ina_mtx, .mrx = ina_mrx,
};

void ina_inject_alert(int channel, int on)
{
    if (channel >= 1 && channel <= 3)
        in3.alert[channel - 1] = on;
    sim_log("[INA ] inject critical alert ch%d=%d", channel, on);
}

void ina_attach(void)
{
    memset(&in3, 0, sizeof(in3));
    sim_i2c_attach(1, &ina_dev);
}
