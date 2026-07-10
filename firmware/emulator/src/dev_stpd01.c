/**
 * @file    dev_stpd01.c
 * @brief   STPD01 programmable buck converter model @ I2C1 0x05.
 *
 * Registers (datasheet): 0x00 VOUT, 0x01 ILIM, 0x02 INT_STAT,
 * 0x06 DIGITAL_ENABLE. Power-on tracks the EN pin (PC11); INT_STAT
 * fault bits are injectable and assert the INT line (PC12, active low).
 */
#include "sim.h"

#include <string.h>

typedef struct {
    uint8_t vout, ilim, dig_en;
    uint8_t fault_bits;         /* injected: OVP=1<<0 SCP=1<<2 OTP=1<<5
                                   OTW=1<<6 IPK=1<<7                     */
    uint8_t reg_ptr;
} Stpd;

static Stpd st;

static float vout_mv(void)
{
    /* inverse of the firmware encoding tables */
    if (st.vout <= 0x90) return 3000.0f + st.vout * 20.0f;
    if (st.vout <  0xC4) return 5900.0f + (st.vout - 0x91) * 100.0f;
    return 11000.0f + (st.vout - 0xC4) * 200.0f;
}

float stpd_vout_mv(void) { return vout_mv(); }

/* programmed current limit, same encoding the firmware writes to ILIM */
float stpd_ilim_ma(void) { return 100.0f + st.ilim * 100.0f; }

static uint8_t int_stat(void)
{
    uint8_t v = st.fault_bits;
    int en = sim_gpio_get(2, 11);           /* PC11 EN pin */
    if (en && !(st.fault_bits & 0xA5))      /* no OVP/SCP/OTP/IPK  */
        v |= 1u << 3;                       /* PB (power good/on)  */
    return v;
}

static void int_update(void)
{
    /* INT (PC12) low while a fault is latched */
    sim_gpio_drive_input(2, 12, st.fault_bits ? 0 : 1);
}

static uint8_t reg_read_byte(uint8_t r)
{
    switch (r) {
    case 0x00: return st.vout;
    case 0x01: return st.ilim;
    case 0x02: return int_stat();
    case 0x06: return st.dig_en;
    default:   return 0;
    }
}

static void reg_write_byte(uint8_t r, uint8_t v)
{
    switch (r) {
    case 0x00: st.vout = v;
        sim_log("[STPD] VOUT reg=0x%02X (%.0f mV)", v, vout_mv());   break;
    case 0x01: st.ilim = v;
        sim_log("[STPD] ILIM reg=0x%02X (%u mA)", v, 100u + v * 100u); break;
    case 0x06: st.dig_en = v; break;
    default: break;
    }
}

static int st_mem_read(SimI2CDev *d, uint16_t mem, uint16_t ms,
                       uint8_t *buf, uint16_t n)
{
    (void)d; (void)ms;
    for (uint16_t i = 0; i < n; i++)
        buf[i] = reg_read_byte((uint8_t)(mem + i));
    return 0;
}

static int st_mem_write(SimI2CDev *d, uint16_t mem, uint16_t ms,
                        const uint8_t *buf, uint16_t n)
{
    (void)d; (void)ms;
    for (uint16_t i = 0; i < n; i++)
        reg_write_byte((uint8_t)(mem + i), buf[i]);
    return 0;
}

static int st_mtx(SimI2CDev *d, const uint8_t *buf, uint16_t n)
{
    if (n == 0) return -1;
    st.reg_ptr = buf[0];
    return (n > 1) ? st_mem_write(d, buf[0], 1, buf + 1, (uint16_t)(n - 1)) : 0;
}

static int st_mrx(SimI2CDev *d, uint8_t *buf, uint16_t n)
{
    return st_mem_read(d, st.reg_ptr, 1, buf, n);
}

static SimI2CDev st_dev = {
    .addr8 = (uint16_t)(0x05 << 1), .name = "STPD01",
    .mem_read = st_mem_read, .mem_write = st_mem_write,
    .mtx = st_mtx, .mrx = st_mrx,
};

void stpd_inject_fault(uint8_t bits)
{
    st.fault_bits = bits;
    sim_log("[STPD] inject INT_STAT fault bits 0x%02X", bits);
    int_update();
}

void stpd_attach(void)
{
    memset(&st, 0, sizeof(st));
    sim_i2c_attach(1, &st_dev);
}
