/**
 * @file    dev_bq34z100.c
 * @brief   BQ34Z100-R2 fuel gauge model @ I2C1 0x55.
 *
 * Register values are computed live from the board battery model.
 * Implements: standard commands used by charge.c, Control() subcommands
 * and the data-flash window used by provisioning.c (BlockDataControl /
 * DataFlashClass / DataFlashBlock / 0x40..0x5F / checksum).
 */
#include "sim.h"

#include <math.h>
#include <string.h>

/* standard command register map (subset) */
#define R_CNTL   0x00
#define R_SOC    0x02
#define R_VOLT   0x08
#define R_AI     0x0A
#define R_TEMPX  0x0C
#define R_FLAGS  0x0E
#define R_I      0x10
#define R_TTE    0x18
#define R_TTF    0x1A
#define R_VSCALE 0x20
#define R_ISCALE 0x21
#define R_TEMPI  0x2A
#define R_CYC    0x2C
#define R_SOH    0x2E

/* data-flash access */
#define R_DFCLS  0x3E
#define R_DFBLK  0x3F
#define R_BLKDAT 0x40
#define R_BLKCS  0x60
#define R_BDC    0x61

#define DF_CLASSES 128
#define DF_BLOCKS  4

typedef struct {
    uint8_t  reg_ptr;
    uint16_t cntl_resp;
    uint8_t  df_class, df_block, bdc;
    uint8_t  df[DF_CLASSES][DF_BLOCKS][32];
    /* injected fault flags */
    int inj_ot, inj_bathi, inj_batlow, inj_cf;
    int inj_bus_fail;       /* 1 = NACK every transaction (bus fault) */
    int bathi_latch;        /* BATHI hysteresis: set >=16.4 V, clear <16.0 V */
    int chginh_latch;       /* CHG_INH/XCHG: set <0 C or >45 C, 3 C hysteresis */
    int sealed;             /* accepted unseal keys clear it */
    int unseal_step;
    int qen;                /* IT_ENABLE received (one-way, like silicon) */
    /* calibration-routine emulation (CC_OFFSET / BOARD_OFFSET) */
    uint32_t cca_until, bca_until;
} Bq34;

static Bq34 gg;

/* ---- calibration model ----
 * The DF Calibration/Data block (subclass 104) is live: reported voltage,
 * current and temperature are distorted by the stored parameters exactly
 * as on the real silicon, so the firmware's ratio-method calibration can
 * be exercised end to end. bq34_attach() seeds the block with deliberate
 * errors; a correct wizard run converges to DIV_TRUE / GAIN_TRUE. */

#define CAL_SUBCLASS   104
#define GAIN_TRUE      0.4768f     /* 10 mOhm shunt */
#define DIV_TRUE       5000.0f

/* F4 (Xemics) float codec — same encoding calibration.c uses */
static float xemics_decode(const uint8_t b[4])
{
    float mant = (float)(b[1] & 0x7Fu) / 256.0f + (float)b[2] / 65536.0f
               + (float)b[3] / 16777216.0f + 0.5f;
    float val  = mant * powf(2.0f, (float)((int)b[0] - 128));
    return (b[1] & 0x80u) ? -val : val;
}

static void xemics_encode(float val, uint8_t b[4])
{
    int neg = (val < 0.0f);
    float x = neg ? -val : val;
    int exp = 0;
    if (x < 1e-30f) x = 1e-30f;
    while (x < 0.5f)  { x *= 2.0f; exp--; }
    while (x >= 1.0f) { x /= 2.0f; exp++; }
    uint32_t m = (uint32_t)((x - 0.5f) * 2.0f * 8388608.0f + 0.5f);
    if (m >= 8388608u) { m = 0u; exp++; }
    b[0] = (uint8_t)(exp + 128);
    b[1] = (uint8_t)((m >> 16) & 0x7Fu) | (neg ? 0x80u : 0x00u);
    b[2] = (uint8_t)(m >> 8);
    b[3] = (uint8_t)m;
}

static float cal_gain(void)
{ return xemics_decode(&gg.df[CAL_SUBCLASS][0][0]); }

static float cal_divider(void)
{
    const uint8_t *b = gg.df[CAL_SUBCLASS][0];
    return (float)(((uint16_t)b[14] << 8) | b[15]);
}

static float cal_ext_temp_off_c(void)
{ return (float)(int8_t)gg.df[CAL_SUBCLASS][0][12] * 0.1f; }

/* ---- flag & value computation from board state ---- */

static uint16_t flags_word(void)
{
    uint8_t hi = 0, lo = 0;
    int chg = board.i_batt_ma > 20.0f;
    int dsg = board.i_batt_ma < -20.0f;
    int ot  = gg.inj_ot || board.temp_c >= 55.0f;

    if (ot && chg)             hi |= 1u << 7;                 /* OTC   */
    if (ot && !chg)            hi |= 1u << 6;                 /* OTD   */
    /* BATHI with hysteresis, like the real gauge's data-flash set/clear
     * thresholds: set at >=16.4 V (4.10 V/cell), clear below 16.0 V
     * (4.00 V/cell). Without it the flag flapped on the IR-drop step at
     * plug/unplug and never cleared near full. */
    if (board.pack_mv >= 16400.0f)      gg.bathi_latch = 1;
    else if (board.pack_mv < 16000.0f)  gg.bathi_latch = 0;
    if (gg.inj_bathi || gg.bathi_latch) hi |= 1u << 5;        /* BATHI */
    if (gg.inj_batlow || board.pack_mv <= 12200.0f) hi |= 1u << 4; /* BATLOW */
    /* charge-inhibit window (JEITA-style data flash: charge only 0..45 C),
     * 3 C hysteresis on re-entry like the real gauge */
    if (board.temp_c < 0.0f || board.temp_c > 45.0f)      gg.chginh_latch = 1;
    else if (board.temp_c > 3.0f && board.temp_c < 42.0f) gg.chginh_latch = 0;
    if (gg.chginh_latch) { hi |= 1u << 3;                     /* CHG_INH */
                           hi |= 1u << 2; }                   /* XCHG    */
    if (board.soc >= 99.5f)    hi |= 1u << 1;                 /* FC    */
    if (chg)                   hi |= 1u << 0;                 /* CHG   */

    if (dsg)                   lo |= 1u << 0;                 /* DSG   */
    if (board.soc < 5.0f)      lo |= 1u << 1;                 /* SOCF  */
    if (board.soc < 10.0f)     lo |= 1u << 2;                 /* SOC1  */
    if (gg.inj_cf)             lo |= 1u << 4;                 /* CF    */
    if (fabsf(board.i_batt_ma) < 20.0f) lo |= 1u << 7;        /* REST  */

    return (uint16_t)((hi << 8) | lo);
}

static uint16_t tte_min(void)
{
    if (board.i_batt_ma >= -20.0f) return 65535u;
    float cap_mah = 7800.0f * (float)cfg.soh / 100.0f;
    return (uint16_t)(board.soc / 100.0f * cap_mah
                      / -board.i_batt_ma * 60.0f);
}

static uint16_t ttf_min(void)
{
    if (board.i_batt_ma <= 20.0f) return 65535u;
    float cap_mah = 7800.0f * (float)cfg.soh / 100.0f;
    return (uint16_t)((100.0f - board.soc) / 100.0f * cap_mah
                      / board.i_batt_ma * 60.0f);
}

/* byte-addressed read of any standard command register */
static uint8_t reg_read_byte(uint8_t reg)
{
    uint16_t w;
    switch (reg & 0xFE) {
    case R_CNTL:  w = gg.cntl_resp;                          break;
    /* reported values are distorted by the DF calibration parameters,
     * like the real ADC front-end: divider scales V, CC gain scales I */
    case R_VOLT:  w = (uint16_t)(board.pack_mv
                                 * cal_divider() / DIV_TRUE); break;
    case R_AI:
    case R_I:     w = (uint16_t)(int16_t)(board.i_batt_ma
                                 * cal_gain() / GAIN_TRUE);   break;
    case R_TEMPX: w = (uint16_t)((board.temp_c + cal_ext_temp_off_c()
                                  + 273.15f) * 10.0f);        break;
    case R_TEMPI: w = (uint16_t)((board.temp_c + 275.15f) * 10.0f);  break;
    case R_FLAGS: w = flags_word();                          break;
    case R_TTE:   w = tte_min();                             break;
    case R_TTF:   w = ttf_min();                             break;
    case R_CYC:   w = (uint16_t)cfg.cycles;                  break;
    case R_SOH:   w = (uint16_t)cfg.soh;                     break;
    default:      w = 0;                                     break;
    }

    switch (reg) {
    case R_SOC:    return (uint8_t)(board.soc + 0.5f);
    case R_VSCALE: return 1;
    case R_ISCALE: return 1;
    case R_DFCLS:  return gg.df_class;
    case R_DFBLK:  return gg.df_block;
    case R_BLKCS: {
        uint32_t s = 0;
        const uint8_t *b = gg.df[gg.df_class % DF_CLASSES]
                                [gg.df_block % DF_BLOCKS];
        for (int i = 0; i < 32; i++) s += b[i];
        return (uint8_t)(255u - (s & 0xFFu));
    }
    default: break;
    }

    if (reg >= R_BLKDAT && reg < R_BLKDAT + 32)
        return gg.df[gg.df_class % DF_CLASSES]
                    [gg.df_block % DF_BLOCKS][reg - R_BLKDAT];

    return (reg & 1) ? (uint8_t)(w >> 8) : (uint8_t)(w & 0xFF);
}

static void cntl_subcommand(uint16_t sub)
{
    switch (sub) {
    case 0x0000: {                               /* CONTROL_STATUS    */
        uint16_t hi = 0, lo = 0;
        if (gg.sealed)                    hi |= 1u << 5;   /* SS  */
        if (sim_now_ms() < gg.cca_until)  hi |= 1u << 3;   /* CCA */
        if (sim_now_ms() < gg.bca_until)  hi |= 1u << 2;   /* BCA */
        if (gg.qen) { lo |= 1u << 0;                       /* QEN */
                      lo |= 1u << 1; }                     /* VOK */
        gg.cntl_resp = (uint16_t)((hi << 8) | lo);
        break;
    }
    case 0x0021:                                  /* IT_ENABLE         */
        gg.qen = 1;
        gg.df[82][0][4] = 0x04;    /* Update Status: IT on, not learned */
        sim_log("[GG  ] IT_ENABLE: QEN set, Update Status 0x04");
        break;
    case 0x0001: gg.cntl_resp = 0x0100; break;   /* DEVICE_TYPE       */
    case 0x0009:                                  /* BOARD_OFFSET      */
        gg.bca_until = sim_now_ms() + 1500u;
        gg.df[CAL_SUBCLASS][0][10] = (uint8_t)(int8_t)(-2);
        sim_log("[GG  ] BOARD_OFFSET routine started");
        break;
    case 0x000A:                                  /* CC_OFFSET         */
        gg.cca_until = sim_now_ms() + 2000u;
        sim_log("[GG  ] CC_OFFSET routine started");
        break;
    case 0x000B:                                  /* CC_OFFSET_SAVE    */
        gg.df[CAL_SUBCLASS][0][8] = 0xFF;         /* -12 raw, I2 BE    */
        gg.df[CAL_SUBCLASS][0][9] = 0xF4;
        sim_log("[GG  ] CC offset saved");
        break;
    case 0x0414: gg.unseal_step = 1;    break;   /* unseal key 1      */
    case 0x3672:
        if (gg.unseal_step == 1) { gg.sealed = 0; sim_log("[GG  ] unsealed"); }
        gg.unseal_step = 0;
        break;
    case 0x0041:                                  /* RESET             */
        sim_log("[GG  ] reset (config reload)");
        break;
    default:
        gg.cntl_resp = 0x0000;
        break;
    }
}

static void reg_write_byte(uint8_t reg, uint8_t val)
{
    static uint8_t cntl_lo;
    switch (reg) {
    case R_CNTL:     cntl_lo = val;                       break;
    case R_CNTL + 1: cntl_subcommand((uint16_t)(val << 8) | cntl_lo); break;
    case R_DFCLS:    gg.df_class = val;                   break;
    case R_DFBLK:    gg.df_block = val;                   break;
    case R_BDC:      gg.bdc = val;                        break;
    case R_BLKCS:    /* checksum accepted, block committed */
        sim_log("[GG  ] data-flash write: subclass %u block %u",
                gg.df_class, gg.df_block);
        break;
    default:
        if (reg >= R_BLKDAT && reg < R_BLKDAT + 32)
            gg.df[gg.df_class % DF_CLASSES]
                 [gg.df_block % DF_BLOCKS][reg - R_BLKDAT] = val;
        break;
    }
}

/* ---- SimI2CDev glue ---- */

static int gg_mem_read(SimI2CDev *d, uint16_t mem, uint16_t ms,
                       uint8_t *buf, uint16_t n)
{
    (void)d; (void)ms;
    if (gg.inj_bus_fail) return -1;     /* NACK -> HAL_ERROR */
    for (uint16_t i = 0; i < n; i++)
        buf[i] = reg_read_byte((uint8_t)(mem + i));
    gg.reg_ptr = (uint8_t)(mem + n);
    return 0;
}

static int gg_mem_write(SimI2CDev *d, uint16_t mem, uint16_t ms,
                        const uint8_t *buf, uint16_t n)
{
    (void)d; (void)ms;
    if (gg.inj_bus_fail) return -1;     /* NACK -> HAL_ERROR */
    for (uint16_t i = 0; i < n; i++)
        reg_write_byte((uint8_t)(mem + i), buf[i]);
    return 0;
}

static int gg_mtx(SimI2CDev *d, const uint8_t *buf, uint16_t n)
{
    if (n == 0) return -1;
    gg.reg_ptr = buf[0];
    return (n > 1) ? gg_mem_write(d, buf[0], 1, buf + 1, (uint16_t)(n - 1)) : 0;
}

static int gg_mrx(SimI2CDev *d, uint8_t *buf, uint16_t n)
{
    return gg_mem_read(d, gg.reg_ptr, 1, buf, n);
}

static SimI2CDev gg_dev = {
    .addr8 = (uint16_t)(0x55 << 1), .name = "BQ34Z100",
    .mem_read = gg_mem_read, .mem_write = gg_mem_write,
    .mtx = gg_mtx, .mrx = gg_mrx,
};

void bq34_attach(void)
{
    memset(&gg, 0, sizeof(gg));
    gg.sealed = 1;

    /* factory-fresh calibration block with deliberate errors: +2.8% on
     * the current gain, -2.6% on the divider, -2.0 C on the ext sensor.
     * The on-device wizard is expected to converge these to the _TRUE
     * values (watch the DF writes in the log). */
    xemics_encode(0.4900f, &gg.df[CAL_SUBCLASS][0][0]);          /* CC Gain  */
    xemics_encode(0.4900f * 1190738.0f, &gg.df[CAL_SUBCLASS][0][4]); /* Delta */
    gg.df[CAL_SUBCLASS][0][12] = (uint8_t)(int8_t)(-20);         /* ext toff */
    gg.df[CAL_SUBCLASS][0][14] = (uint8_t)(4870u >> 8);          /* divider  */
    gg.df[CAL_SUBCLASS][0][15] = (uint8_t)(4870u & 0xFFu);

    if (cfg.gauge_provisioned) {
        /* Manufacturer Info Block A, subclass 58: "D" + rev 0x01 marker */
        gg.df[58][0][0] = 'D';
        gg.df[58][0][1] = 0x01;
        /* design capacity / series cells as provisioning would leave them */
        gg.df[48][0][11] = (uint8_t)(7800u >> 8);
        gg.df[48][0][12] = (uint8_t)(7800u & 0xFF);
        gg.df[64][0][7]  = 4;
    }
    sim_i2c_attach(1, &gg_dev);
}

void bq34_inject_ot(int on)     { gg.inj_ot = on;     sim_log("[GG  ] inject OT=%d", on); }
void bq34_inject_bathi(int on)  { gg.inj_bathi = on;  sim_log("[GG  ] inject BATHI=%d", on); }
void bq34_inject_batlow(int on) { gg.inj_batlow = on; sim_log("[GG  ] inject BATLOW=%d", on); }
void bq34_inject_cf(int on)     { gg.inj_cf = on;     sim_log("[GG  ] inject CF=%d", on); }
void bq34_inject_bus_fail(int on){ gg.inj_bus_fail = on; sim_log("[GG  ] inject BUS_FAIL=%d", on); }
