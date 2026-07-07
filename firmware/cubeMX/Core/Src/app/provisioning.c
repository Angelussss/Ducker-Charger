/**
 * @file    provisioning.c
 * @brief   BQ34Z100-R2 data-flash provisioning. See provisioning.h.
 *
 * Data-flash access protocol (BQ34Z100-R2 TRM, "Accessing Data Flash"):
 *   0x61  BlockDataControl  (write 0x00 to enable block access)
 *   0x3E  DataFlashClass    (subclass id)
 *   0x3F  DataFlashBlock    (32-byte block index within the subclass)
 *   0x40..0x5F              32-byte block data window
 *   0x60  BlockDataChecksum (255 - (sum of block bytes) & 0xFF)
 *
 * VERIFY BEFORE FIRST USE (marked CHECK):
 *   - Subclass IDs and offsets against the BQ34Z100-R2 TRM data-flash
 *     table (the TRM is already in the repo docs). The values below
 *     follow the TRM naming but MUST be cross-checked once.
 *   - Unseal keys: 0x0414/0x3672 are the TI defaults; if the gauge is
 *     ever sealed with custom keys, update them here.
 *   - The pack sits behind the ISO1540 on I2C_LP: all accesses go
 *     through the normal hi2c1 path, nothing special needed.
 */

#include "app/provisioning.h"
#include "main.h"
#include "i2c.h"        /* hi2c1 (I2C_LP -> ISO1540 -> gauge) */
#include "system/defines.h"

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#define I2C_TIMEOUT_MS          (50u)

/* Standard commands */
#define REG_CNTL                (0x00u)   /* Control(), 2 bytes */
#define REG_AI                  (0x0Au)   /* AverageCurrent, mA  */
#define REG_BDC                 (0x61u)   /* BlockDataControl    */
#define REG_DF_CLASS            (0x3Eu)
#define REG_DF_BLOCK            (0x3Fu)
#define REG_BLOCK_DATA          (0x40u)
#define REG_BLOCK_CSUM          (0x60u)

/* Control() subcommands */
#define CNTL_UNSEAL_KEY1        (0x0414u)
#define CNTL_UNSEAL_KEY2        (0x3672u)
#define CNTL_RESET              (0x0041u)
#define CNTL_DEVICE_TYPE        (0x0001u)

/* Control() DEVICE_TYPE response for the BQ34Z100 family. */
#define GAUGE_DEVICE_TYPE       (0x0100u)

/* Safety: refuse DF writes unless |AverageCurrent| below this. */
#define PROV_MAX_REST_MA        (50)

/* =========================================================
 * Pack parameters (CHECK every subclass/offset against the TRM table)
 * ========================================================= */

/* Subclass 48 "Data": Design Capacity (mAh, big endian in DF).
 * 3P of Murata VTC5 (2600 mAh) = 7800 mAh — adjust if cells change. */
#define SUBCLASS_DATA           (48u)
#define OFF_DESIGN_CAPACITY     (11u)     /* CHECK: TRM offset */
static const uint8_t design_capacity[2] = { (7800u >> 8) & 0xFFu,
                                             7800u & 0xFFu };

/* Subclass 64 "Registers": Pack Configuration + Number of Series Cells.
 * VOLTSEL (external divider) must be set for a 4S pack. */
#define SUBCLASS_REGISTERS      (64u)
#define OFF_PACK_CONFIG         (0u)      /* CHECK: TRM offset + bits  */
#define OFF_SERIES_CELLS        (7u)      /* CHECK: TRM offset         */
static const uint8_t series_cells[1] = { 4u };

/* "Manufacturer Info Block A": we use the first two bytes as our
 * provisioning marker ("DK" + revision). Bump the revision when the
 * patch list changes so old units re-provision.
 * CHECK: subclass id not yet cross-checked against the TRM table —
 * 58 is a placeholder here, do not assume it is correct. */
#define SUBCLASS_MFG_INFO_A     (58u)     /* CHECK: TRM subclass id */
#define OFF_MARKER              (0u)
#define PROV_MARKER_0           ((uint8_t)'D')
#define PROV_MARKER_1           ((uint8_t)0x01)   /* pack config rev */
static const uint8_t marker[2] = { PROV_MARKER_0, PROV_MARKER_1 };

static const GaugeDfPatch_t patches[] = {
    { SUBCLASS_DATA,       OFF_DESIGN_CAPACITY, 2u, design_capacity },
    { SUBCLASS_REGISTERS,  OFF_SERIES_CELLS,    1u, series_cells    },
    /* Golden-image entries (Qmax, Ra tables, calibration) go here once
     * the learning cycle on the sample pack has been run in bqStudio. */
    { SUBCLASS_MFG_INFO_A, OFF_MARKER,          2u, marker          },
};
#define N_PATCHES  (sizeof(patches) / sizeof(patches[0]))

/* =========================================================
 * Low-level helpers
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
    return HAL_I2C_Master_Receive(&hi2c1, FUEL_GAUGE_ADDR, d, n, I2C_TIMEOUT_MS);
}

static HAL_StatusTypeDef control_write(uint16_t sub)
{
    uint8_t d[2] = { (uint8_t)(sub & 0xFFu), (uint8_t)(sub >> 8) };
    return reg_write(REG_CNTL, d, 2u);
}

/** Control() subcommand round-trip: write, settle, read the response. */
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

/** Select subclass/block and load the 32-byte window into buf. */
static HAL_StatusTypeDef df_read_block(uint8_t subclass, uint8_t block,
                                       uint8_t *buf)
{
    uint8_t v;

    v = 0x00u;
    if (reg_write(REG_BDC, &v, 1u) != HAL_OK)          return HAL_ERROR;
    if (reg_write(REG_DF_CLASS, &subclass, 1u) != HAL_OK) return HAL_ERROR;
    if (reg_write(REG_DF_BLOCK, &block, 1u) != HAL_OK) return HAL_ERROR;
    HAL_Delay(2);
    return reg_read(REG_BLOCK_DATA, buf, 32u);
}

/** Write back a modified 32-byte window with its checksum. */
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

/** Re-checkable safety gate: true if |AverageCurrent| is below the rest
 * threshold. Called before the DF-write sequence starts AND again before
 * every individual patch, since the sequence spans several I2C round-trips
 * and HAL_Delay(2)/HAL_Delay(100) pauses during which a charger or load
 * could start drawing current. */
static bool pack_at_rest(void)
{
    int16_t avg_ma = 0;

    if (reg_read(REG_AI, (uint8_t *)&avg_ma, 2u) != HAL_OK)
        return false;
    return (avg_ma <= PROV_MAX_REST_MA) && (avg_ma >= -PROV_MAX_REST_MA);
}

/** Apply one patch (read-modify-write of the containing block). */
static HAL_StatusTypeDef df_apply(const GaugeDfPatch_t *p)
{
    uint8_t buf[32];
    uint8_t block  = p->offset / 32u;
    uint8_t in_off = p->offset % 32u;

    if ((in_off + p->len) > 32u)
        return HAL_ERROR;           /* patch must not straddle blocks */

    if (df_read_block(p->subclass, block, buf) != HAL_OK)
        return HAL_ERROR;

    if (memcmp(&buf[in_off], p->data, p->len) == 0)
        return HAL_OK;              /* already correct: spare the flash */

    memcpy(&buf[in_off], p->data, p->len);
    return df_write_block(buf);
}

/* =========================================================
 * Public API
 * ========================================================= */

ProvCheck_t Provisioning_Check(uint8_t *rev_out)
{
    uint16_t devtype = 0;
    uint8_t  buf[32];

    if (rev_out != NULL)
        *rev_out = 0u;

    /* 1. Right chip on the bus? */
    if (control_read(CNTL_DEVICE_TYPE, &devtype) != HAL_OK)
        return PROV_CHECK_BUS_ERROR;
    if (devtype != GAUGE_DEVICE_TYPE)
        return PROV_CHECK_WRONG_DEVICE;

    /* 2. Provisioning marker + revision */
    if (df_read_block(SUBCLASS_MFG_INFO_A, 0u, buf) != HAL_OK)
        return PROV_CHECK_BUS_ERROR;

    if (buf[OFF_MARKER] != PROV_MARKER_0)
        return PROV_CHECK_BLANK;

    if (rev_out != NULL)
        *rev_out = buf[OFF_MARKER + 1u];

    return (buf[OFF_MARKER + 1u] == PROV_MARKER_1)
           ? PROV_CHECK_OK : PROV_CHECK_OLD_REV;
}

uint8_t Provisioning_Required(void)
{
    ProvCheck_t c = Provisioning_Check(NULL);

    /* Bus errors and wrong devices are NOT "please provision me":
     * writing the data flash of an unknown chip would be reckless. */
    return (c == PROV_CHECK_BLANK || c == PROV_CHECK_OLD_REV) ? 1u : 0u;
}

ProvStatus_t Provisioning_RunGauge(void)
{
    {
        ProvCheck_t c = Provisioning_Check(NULL);
        if (c == PROV_CHECK_OK)
            return PROV_NOT_NEEDED;
        if (c != PROV_CHECK_BLANK && c != PROV_CHECK_OLD_REV)
            return PROV_FAILED;     /* wrong device / bus error */
    }

    /* Safety gate: pack must be at rest for DF writes. */
    if (!pack_at_rest())
        return PROV_UNSAFE;

    /* Unseal (TI default keys; no-op if already unsealed). */
    if (control_write(CNTL_UNSEAL_KEY1) != HAL_OK) return PROV_FAILED;
    HAL_Delay(2);
    if (control_write(CNTL_UNSEAL_KEY2) != HAL_OK) return PROV_FAILED;
    HAL_Delay(2);

    /* Re-check before every patch: the sequence spans several I2C
     * round-trips and delays during which a charger/load could attach. */
    for (uint8_t i = 0; i < N_PATCHES; i++) {
        if (!pack_at_rest())
            return PROV_UNSAFE;
        if (df_apply(&patches[i]) != HAL_OK)
            return PROV_FAILED;
    }

    /* Reset so the gauge reloads the new configuration. */
    (void)control_write(CNTL_RESET);
    HAL_Delay(250);

    return PROV_OK;
}
