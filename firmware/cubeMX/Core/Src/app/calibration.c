/**
 * @file    calibration.c
 * @brief   BQ34Z100-R2 on-device calibration. See calibration.h.
 *
 * DF layout (TRM data-flash table, subclass 104 "Calibration/Data"):
 *   off 0   F4  CC Gain          (default 0.4768  for a 10 mOhm shunt)
 *   off 4   F4  CC Delta         (default 567744.56, = CC Gain * DELTA_RATIO)
 *   off 8   I2  CC Offset
 *   off 10  I1  Board Offset
 *   off 11  I1  Int Temp Offset  (0.1 C)
 *   off 12  I1  Ext Temp Offset  (0.1 C)
 *   off 14  U2  Voltage Divider  (mV)
 *
 * F4 is the TI/Xemics floating-point format: byte0 = exponent + 128,
 * bytes 1..3 = mantissa with the implicit leading bit replaced by the
 * sign in bit 7 of byte 1 (the classic bq34z100 encoding).
 *
 * The low-level I2C/DF helpers mirror provisioning.c on purpose (same
 * bus, same gauge, same block protocol); they stay module-private the
 * same way. If a third gauge module ever appears, hoist them into a
 * shared app/gauge_io.c instead of copying again.
 */

#include "app/calibration.h"
#include "main.h"
#include "i2c.h"            /* hi2c1 (I2C_LP -> ISO1540 -> gauge) */
#include "system/defines.h"

#include <math.h>
#include <string.h>

#define I2C_TIMEOUT_MS      (50u)

/* Standard commands */
#define REG_CNTL            (0x00u)
#define REG_VOLT            (0x08u)
#define REG_AI              (0x0Au)
#define REG_DF_CLASS        (0x3Eu)
#define REG_DF_BLOCK        (0x3Fu)
#define REG_BLOCK_DATA      (0x40u)
#define REG_BLOCK_CSUM      (0x60u)
#define REG_BDC             (0x61u)

/* Control() subcommands (TRM 2.1.1) */
#define CNTL_CONTROL_STATUS (0x0000u)
#define CNTL_DEVICE_TYPE    (0x0001u)
#define CNTL_BOARD_OFFSET   (0x0009u)
#define CNTL_CC_OFFSET      (0x000Au)
#define CNTL_CC_OFFSET_SAVE (0x000Bu)
#define CNTL_UNSEAL_KEY1    (0x0414u)
#define CNTL_UNSEAL_KEY2    (0x3672u)

#define GAUGE_DEVICE_TYPE   (0x0100u)

/* CONTROL_STATUS high-byte flags (TRM table 2-3) */
#define CS_HI_SS            (1u << 5)
#define CS_HI_CCA           (1u << 3)
#define CS_HI_BCA           (1u << 2)

/* DF Calibration/Data subclass */
#define SUBCLASS_CAL_DATA   (104u)
#define OFF_CC_GAIN         (0u)
#define OFF_CC_DELTA        (4u)
#define OFF_CC_OFFSET       (8u)
#define OFF_BOARD_OFFSET    (10u)
#define OFF_INT_TEMP_OFF    (11u)
#define OFF_EXT_TEMP_OFF    (12u)
#define OFF_DIVIDER         (14u)

/* CC Delta / CC Gain is a fixed device constant; keep the TRM default
 * ratio (567744.56 / 0.4768) when scaling the gain. */
#define DELTA_RATIO         (1190738.0f)

/* Reject entered values implying more than +/-15% correction: either
 * the divider/shunt is far off nominal (hardware bug, not calibration)
 * or the user mistyped. */
#define MAX_RATIO_ERR       (0.15f)

/* Internal offset routines take a few seconds; TRM gives no hard
 * figure, bqStudio waits ~16 s worst case. */
#define OFFSET_TIMEOUT_MS   (20000u)

/* =========================================================
 * Low-level helpers (see header note about the provisioning.c twin)
 * ========================================================= */

static HAL_StatusTypeDef reg_write(uint8_t reg, const uint8_t *d, uint8_t n)
{
    uint8_t buf[34];
    buf[0] = reg;
    memcpy(&buf[1], d, n);
    return HAL_I2C_Master_Transmit(&hi2c1, FUEL_GAUGE_ADDR, buf,
                                   (uint16_t)(n + 1u), I2C_TIMEOUT_MS);
}

static HAL_StatusTypeDef reg_read(uint8_t reg, uint8_t *d, uint8_t n)
{
    if (HAL_I2C_Master_Transmit(&hi2c1, FUEL_GAUGE_ADDR, &reg, 1u,
                                I2C_TIMEOUT_MS) != HAL_OK)
        return HAL_ERROR;
    return HAL_I2C_Master_Receive(&hi2c1, FUEL_GAUGE_ADDR, d, n,
                                  I2C_TIMEOUT_MS);
}

static HAL_StatusTypeDef control_write(uint16_t sub)
{
    uint8_t d[2] = { (uint8_t)(sub & 0xFFu), (uint8_t)(sub >> 8) };
    return reg_write(REG_CNTL, d, 2u);
}

static HAL_StatusTypeDef control_read(uint16_t sub, uint16_t *out)
{
    uint8_t d[2];

    if (control_write(sub) != HAL_OK)
        return HAL_ERROR;
    HAL_Delay(2);
    if (reg_read(REG_CNTL, d, 2u) != HAL_OK)
        return HAL_ERROR;

    *out = (uint16_t)d[0] | ((uint16_t)d[1] << 8);
    return HAL_OK;
}

static HAL_StatusTypeDef df_read_block(uint8_t subclass, uint8_t block,
                                       uint8_t *buf)
{
    uint8_t v = 0x00u;

    if (reg_write(REG_BDC, &v, 1u) != HAL_OK)             return HAL_ERROR;
    if (reg_write(REG_DF_CLASS, &subclass, 1u) != HAL_OK) return HAL_ERROR;
    if (reg_write(REG_DF_BLOCK, &block, 1u) != HAL_OK)    return HAL_ERROR;
    HAL_Delay(2);
    return reg_read(REG_BLOCK_DATA, buf, 32u);
}

static HAL_StatusTypeDef df_write_block(const uint8_t *buf)
{
    uint32_t sum = 0;
    uint8_t  csum;

    if (reg_write(REG_BLOCK_DATA, buf, 32u) != HAL_OK)
        return HAL_ERROR;

    for (uint8_t i = 0; i < 32u; i++)
        sum += buf[i];
    csum = (uint8_t)(255u - (sum & 0xFFu));

    if (reg_write(REG_BLOCK_CSUM, &csum, 1u) != HAL_OK)
        return HAL_ERROR;

    HAL_Delay(100);     /* DF write time, TRM-mandated pause */
    return HAL_OK;
}

/* =========================================================
 * F4 (Xemics) float codec
 * ========================================================= */

static float xemics_decode(const uint8_t b[4])
{
    int   exp  = (int)b[0] - 128;
    /* byte1 bit7 is the sign; the implicit mantissa leading bit is 1 */
    float mant = (float)(b[1] & 0x7Fu) / 256.0f
               + (float)b[2] / 65536.0f
               + (float)b[3] / 16777216.0f
               + 0.5f;                       /* implicit leading bit */
    float val  = mant * powf(2.0f, (float)exp);

    return (b[1] & 0x80u) ? -val : val;
}

static void xemics_encode(float val, uint8_t b[4])
{
    int   neg = (val < 0.0f);
    float x   = neg ? -val : val;
    int   exp = 0;

    if (x < 1e-30f)
        x = 1e-30f;
    while (x < 0.5f)  { x *= 2.0f; exp--; }
    while (x >= 1.0f) { x /= 2.0f; exp++; }
    /* x in [0.5, 1): strip the implicit leading bit, spread over 23 bits */
    x = (x - 0.5f) * 2.0f;                       /* [0, 1) */

    uint32_t m = (uint32_t)(x * 8388608.0f + 0.5f);   /* 2^23 */
    if (m >= 8388608u) { m = 0u; exp++; }

    b[0] = (uint8_t)(exp + 128);
    b[1] = (uint8_t)((m >> 16) & 0x7Fu);
    b[2] = (uint8_t)(m >> 8);
    b[3] = (uint8_t)m;
    if (neg)
        b[1] |= 0x80u;
}

/* =========================================================
 * DF calibration block read/modify/write
 * ========================================================= */

static CalStatus_t cal_read_raw(uint8_t buf[32])
{
    return (df_read_block(SUBCLASS_CAL_DATA, 0u, buf) == HAL_OK)
           ? CAL_OK : CAL_BUS_ERROR;
}

static CalStatus_t cal_write_raw(const uint8_t buf[32])
{
    uint8_t v = 0x00u;

    /* re-select the block right before writing: the window register
     * pair may have been disturbed by another module in between */
    uint8_t subclass = SUBCLASS_CAL_DATA, block = 0u;
    if (reg_write(REG_BDC, &v, 1u) != HAL_OK)             return CAL_BUS_ERROR;
    if (reg_write(REG_DF_CLASS, &subclass, 1u) != HAL_OK) return CAL_BUS_ERROR;
    if (reg_write(REG_DF_BLOCK, &block, 1u) != HAL_OK)    return CAL_BUS_ERROR;
    HAL_Delay(2);
    return (df_write_block(buf) == HAL_OK) ? CAL_OK : CAL_BUS_ERROR;
}

/* =========================================================
 * Public API
 * ========================================================= */

CalStatus_t Calibration_Begin(void)
{
    uint16_t devtype = 0;

    if (control_read(CNTL_DEVICE_TYPE, &devtype) != HAL_OK)
        return CAL_BUS_ERROR;
    if (devtype != GAUGE_DEVICE_TYPE)
        return CAL_WRONG_DEVICE;

    /* Unseal with the TI default keys (no-op if already unsealed). */
    if (control_write(CNTL_UNSEAL_KEY1) != HAL_OK) return CAL_BUS_ERROR;
    HAL_Delay(2);
    if (control_write(CNTL_UNSEAL_KEY2) != HAL_OK) return CAL_BUS_ERROR;
    HAL_Delay(2);

    return CAL_OK;
}

CalStatus_t Calibration_Read(CalData_t *out)
{
    uint8_t buf[32];

    CalStatus_t st = cal_read_raw(buf);
    if (st != CAL_OK)
        return st;

    out->cc_gain      = xemics_decode(&buf[OFF_CC_GAIN]);
    out->cc_delta     = xemics_decode(&buf[OFF_CC_DELTA]);
    out->cc_offset    = (int16_t)(((uint16_t)buf[OFF_CC_OFFSET] << 8)
                                  | buf[OFF_CC_OFFSET + 1u]);
    out->board_offset = (int8_t)buf[OFF_BOARD_OFFSET];
    out->int_temp_off = (int8_t)buf[OFF_INT_TEMP_OFF];
    out->ext_temp_off = (int8_t)buf[OFF_EXT_TEMP_OFF];
    out->divider      = (uint16_t)(((uint16_t)buf[OFF_DIVIDER] << 8)
                                   | buf[OFF_DIVIDER + 1u]);
    return CAL_OK;
}

CalStatus_t Calibration_Live(uint16_t *mv, int16_t *ma)
{
    uint8_t d[2];

    if (reg_read(REG_VOLT, d, 2u) != HAL_OK)
        return CAL_BUS_ERROR;
    *mv = (uint16_t)d[0] | ((uint16_t)d[1] << 8);   /* little-endian */

    if (reg_read(REG_AI, d, 2u) != HAL_OK)
        return CAL_BUS_ERROR;
    *ma = (int16_t)((uint16_t)d[0] | ((uint16_t)d[1] << 8));

    return CAL_OK;
}

/* ---- Step 1: internal offset routines ---- */

static uint32_t offset_start_tick;
static uint8_t  offset_stage;   /* 0 idle, 1 CC running, 2 board running */

CalStatus_t Calibration_OffsetStart(void)
{
    if (control_write(CNTL_CC_OFFSET) != HAL_OK)
        return CAL_BUS_ERROR;
    offset_start_tick = HAL_GetTick();
    offset_stage = 1u;
    return CAL_OK;
}

CalStatus_t Calibration_OffsetPoll(void)
{
    uint16_t cs = 0;

    if (offset_stage == 0u)
        return CAL_OK;
    if (HAL_GetTick() - offset_start_tick > OFFSET_TIMEOUT_MS) {
        offset_stage = 0u;
        return CAL_TIMEOUT;
    }
    if (control_read(CNTL_CONTROL_STATUS, &cs) != HAL_OK)
        return CAL_BUS_ERROR;

    if (offset_stage == 1u) {
        if (cs & ((uint16_t)CS_HI_CCA << 8))
            return CAL_BUSY;
        /* CC offset done: persist it, then run the board-offset pass */
        if (control_write(CNTL_CC_OFFSET_SAVE) != HAL_OK)
            return CAL_BUS_ERROR;
        HAL_Delay(2);
        if (control_write(CNTL_BOARD_OFFSET) != HAL_OK)
            return CAL_BUS_ERROR;
        offset_stage = 2u;
        return CAL_BUSY;
    }

    /* stage 2: board offset */
    if (cs & ((uint16_t)CS_HI_BCA << 8))
        return CAL_BUSY;
    offset_stage = 0u;
    return CAL_OK;
}

/* ---- Step 2: voltage divider ---- */

CalStatus_t Calibration_ApplyVoltage(uint16_t true_mv)
{
    uint8_t  buf[32];
    uint16_t rep_mv;
    int16_t  ma;

    CalStatus_t st = Calibration_Live(&rep_mv, &ma);
    if (st != CAL_OK)
        return st;
    if (rep_mv == 0u)
        return CAL_RANGE;

    float ratio = (float)true_mv / (float)rep_mv;
    if (ratio < 1.0f - MAX_RATIO_ERR || ratio > 1.0f + MAX_RATIO_ERR)
        return CAL_RANGE;

    st = cal_read_raw(buf);
    if (st != CAL_OK)
        return st;

    uint16_t div = (uint16_t)(((uint16_t)buf[OFF_DIVIDER] << 8)
                              | buf[OFF_DIVIDER + 1u]);
    uint32_t nd  = (uint32_t)((float)div * ratio + 0.5f);
    if (nd == 0u || nd > 65535u)
        return CAL_RANGE;

    buf[OFF_DIVIDER]      = (uint8_t)(nd >> 8);
    buf[OFF_DIVIDER + 1u] = (uint8_t)nd;
    return cal_write_raw(buf);
}

/* ---- Step 3: current gain (capture under load, commit at rest) ---- */

static float   pending_ratio;
static uint8_t pending_current;

CalStatus_t Calibration_CaptureCurrent(int16_t true_ma)
{
    uint16_t mv;
    int16_t  rep_ma;

    CalStatus_t st = Calibration_Live(&mv, &rep_ma);
    if (st != CAL_OK)
        return st;
    if (rep_ma == 0 || true_ma == 0)
        return CAL_RANGE;

    float ratio = (float)true_ma / (float)rep_ma;
    if (ratio < 1.0f - MAX_RATIO_ERR || ratio > 1.0f + MAX_RATIO_ERR)
        return CAL_RANGE;

    pending_ratio   = ratio;
    pending_current = 1u;
    return CAL_OK;
}

uint8_t Calibration_HasPendingCurrent(void) { return pending_current; }

CalStatus_t Calibration_CommitCurrent(void)
{
    uint8_t buf[32];

    if (!pending_current)
        return CAL_RANGE;

    CalStatus_t st = cal_read_raw(buf);
    if (st != CAL_OK)
        return st;

    float gain = xemics_decode(&buf[OFF_CC_GAIN]) * pending_ratio;
    xemics_encode(gain, &buf[OFF_CC_GAIN]);
    xemics_encode(gain * DELTA_RATIO, &buf[OFF_CC_DELTA]);

    st = cal_write_raw(buf);
    if (st == CAL_OK)
        pending_current = 0u;
    return st;
}

/* ---- Step 5: pack configuration (VOLTSEL / cells / capacity) ---- */

/* Pack Configuration: subclass 64 "Registers", offset 0, VOLTSEL is
 * bit 3 of the MSB (TRM 2.2 walk-through example). Series cells at
 * offset 7 of the same subclass; Design Capacity subclass 48 offset 11
 * (2 bytes big-endian), same values provisioning.c programs. */
#define SUBCLASS_REGISTERS      (64u)
#define OFF_PACK_CONFIG_MSB     (0u)
#define PACK_CFG_VOLTSEL_BIT    (1u << 3)
#define OFF_SERIES_CELLS        (7u)
#define SERIES_CELLS            (4u)
#define SUBCLASS_DATA           (48u)
#define OFF_DESIGN_CAPACITY     (11u)     /* CHECK: TRM offset */
#define DESIGN_CAPACITY_MAH     (7800u)   /* 3P x Murata VTC5  */

static CalStatus_t df_rmw(uint8_t subclass, uint8_t offset,
                          const uint8_t *data, uint8_t len)
{
    uint8_t buf[32], v = 0x00u;
    uint8_t block  = offset / 32u;
    uint8_t in_off = offset % 32u;

    if ((in_off + len) > 32u)
        return CAL_RANGE;
    if (df_read_block(subclass, block, buf) != HAL_OK)
        return CAL_BUS_ERROR;
    if (memcmp(&buf[in_off], data, len) == 0)
        return CAL_OK;              /* already correct: spare the flash */
    memcpy(&buf[in_off], data, len);

    if (reg_write(REG_BDC, &v, 1u) != HAL_OK)             return CAL_BUS_ERROR;
    if (reg_write(REG_DF_CLASS, &subclass, 1u) != HAL_OK) return CAL_BUS_ERROR;
    if (reg_write(REG_DF_BLOCK, &block, 1u) != HAL_OK)    return CAL_BUS_ERROR;
    HAL_Delay(2);
    return (df_write_block(buf) == HAL_OK) ? CAL_OK : CAL_BUS_ERROR;
}

CalStatus_t Calibration_ApplyPackConfig(void)
{
    uint8_t buf[32];
    CalStatus_t st;

    /* VOLTSEL: read-modify-write of the single MSB, preserving the
     * other Pack Configuration bits (blind overwrite would clobber
     * factory defaults we do not manage) */
    if (df_read_block(SUBCLASS_REGISTERS, 0u, buf) != HAL_OK)
        return CAL_BUS_ERROR;
    uint8_t msb = (uint8_t)(buf[OFF_PACK_CONFIG_MSB] | PACK_CFG_VOLTSEL_BIT);
    st = df_rmw(SUBCLASS_REGISTERS, OFF_PACK_CONFIG_MSB, &msb, 1u);
    if (st != CAL_OK)
        return st;

    uint8_t cells = SERIES_CELLS;
    st = df_rmw(SUBCLASS_REGISTERS, OFF_SERIES_CELLS, &cells, 1u);
    if (st != CAL_OK)
        return st;

    uint8_t cap[2] = { (uint8_t)(DESIGN_CAPACITY_MAH >> 8),
                       (uint8_t)(DESIGN_CAPACITY_MAH & 0xFFu) };
    return df_rmw(SUBCLASS_DATA, OFF_DESIGN_CAPACITY, cap, 2u);
}

/* ---- Step 6: Impedance Track enable ---- */

#define CNTL_IT_ENABLE      (0x0021u)

CalStatus_t Calibration_ITEnable(void)
{
    if (control_write(CNTL_IT_ENABLE) != HAL_OK)
        return CAL_BUS_ERROR;
    HAL_Delay(2);
    return CAL_OK;
}

/* ---- Step 7: learning-cycle monitor ---- */

/* Update Status: DF Gas Gauging/State, subclass 82, offset 4 (H1).
 * 0x04 = IT on, nothing learned; 0x05 = Qmax learned; 0x06 = Qmax+Ra
 * learned (production-ready). */
#define SUBCLASS_GG_STATE   (82u)
#define OFF_UPDATE_STATUS   (4u)

CalStatus_t Calibration_LearnStatus(LearnStatus_t *out)
{
    uint8_t  buf[32], d[2];
    uint16_t cs = 0;

    if (control_read(CNTL_CONTROL_STATUS, &cs) != HAL_OK)
        return CAL_BUS_ERROR;
    out->qen = (cs & (1u << 0)) ? 1u : 0u;
    out->vok = (cs & (1u << 1)) ? 1u : 0u;

    if (df_read_block(SUBCLASS_GG_STATE, 0u, buf) != HAL_OK)
        return CAL_BUS_ERROR;
    out->update_status = buf[OFF_UPDATE_STATUS];

    /* Flags(): REST low bit 7, DSG low bit 0, FC high bit 1 */
    if (reg_read(0x0Eu, d, 2u) != HAL_OK)
        return CAL_BUS_ERROR;
    out->rest = (d[0] & (1u << 7)) ? 1u : 0u;
    out->dsg  = (d[0] & (1u << 0)) ? 1u : 0u;
    out->fc   = (d[1] & (1u << 1)) ? 1u : 0u;

    if (reg_read(REG_AI, d, 2u) != HAL_OK)
        return CAL_BUS_ERROR;
    out->avg_ma = (int16_t)((uint16_t)d[0] | ((uint16_t)d[1] << 8));

    return CAL_OK;
}

uint8_t Calibration_IsDone(void)
{
    uint16_t cs = 0;

    if (control_read(CNTL_CONTROL_STATUS, &cs) != HAL_OK)
        return 1u;                  /* dead bus: NO SENSOR's problem */
    return (cs & (1u << 0)) ? 1u : 0u;     /* QEN */
}

/* ---- Step 4: external temperature offset ---- */

CalStatus_t Calibration_ApplyTemp(int16_t true_c10)
{
    uint8_t d[2], buf[32];

    /* Temperature() 0x0C/0x0D, 0.1 K little-endian */
    if (reg_read(0x0Cu, d, 2u) != HAL_OK)
        return CAL_BUS_ERROR;
    int16_t rep_c10 = (int16_t)(((uint16_t)d[0] | ((uint16_t)d[1] << 8))
                                - 2731);

    int16_t delta = (int16_t)(true_c10 - rep_c10);
    if (delta > 120 || delta < -120)         /* > 12 C: not an offset */
        return CAL_RANGE;

    CalStatus_t st = cal_read_raw(buf);
    if (st != CAL_OK)
        return st;

    int16_t off = (int16_t)((int8_t)buf[OFF_EXT_TEMP_OFF]) + delta;
    if (off > 127)  off = 127;
    if (off < -128) off = -128;
    buf[OFF_EXT_TEMP_OFF] = (uint8_t)(int8_t)off;

    return cal_write_raw(buf);
}
