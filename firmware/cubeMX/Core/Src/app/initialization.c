/**
 * @file    initialization.c
 * @brief   Every-boot IC initialization. See initialization.h.
 *
 * All device I2C addresses and register offsets come from
 * system/defines.h, the single source of truth also used by
 * system/charge.c and app/provisioning.c. Do not redefine them here;
 * a prior revision of this file kept its own copies and they drifted
 * out of sync with defines.h for both STPD01 and TPS25750.
 *
 * VERIFY BEFORE FIRST USE (marked CHECK):
 *   - TPS25750 I2C address (TPS25750_PD_CONTROLLER_ADDR in defines.h):
 *     charge.c's runtime PD-negotiation path already uses 0x20; this
 *     file now uses the same constant. A netlist read (ADCIN1/2 = GND)
 *     had suggested 0x21, confirm against the TPS25750 TRM table
 *     "I2C address selection" before relying on either value.
 *   - The PBMs data layout and burst-write chunking against the TRM
 *     section "Patch Bundle Burst Mode" (document already in the repo).
 *
 * Design rules:
 *   - Bounded timeouts everywhere; never HAL_MAX_DELAY (a hung bus must
 *     degrade the boot, not freeze the product).
 *   - Each step retried once before being declared failed.
 */

#include "app/initialization.h"
#include "main.h"
#include "i2c.h"        /* hi2c1 (I2C_LP), hi2c3 (I2C_PD) */
#include "system/defines.h"
#include "system/tps25750_io.h"

#include <string.h>

/* Patch bundle: the const array is NOT part of this branch; it gets
 * generated from firmware/TPS25750/TPS25750D_DuckerCharger.bin (already
 * in the repo) at integration time, e.g.:
 *   python3 -c "d=open('TPS25750D_DuckerCharger.bin','rb').read(); ..."
 * or xxd -i. Linking without it fails on purpose: the integrator must
 * pick the bundle revision consciously. */
extern const uint8_t  tps25750_bundle[];
extern const uint32_t tps25750_bundle_size;

/* =========================================================
 * Bus / device constants
 * ========================================================= */

#define I2C_TIMEOUT_MS          (50u)
#define STEP_RETRIES            (2u)

/* TPS25750 host-interface registers (length-prefixed I2C payloads). */
#define TPS_REG_MODE            (0x03u)
#define TPS_REG_CMD1            (0x08u)
#define TPS_REG_DATA1           (0x09u)

/* 4CC helpers */
#define TPS_4CC(a,b,c,d)        ((uint32_t)(a) | ((uint32_t)(b) << 8) | \
                                 ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))
#define TPS_MODE_PTCH           TPS_4CC('P','T','C','H')
#define TPS_MODE_APP            TPS_4CC('A','P','P',' ')
#define TPS_CMD_PBMs            TPS_4CC('P','B','M','s')
#define TPS_CMD_PBMc            TPS_4CC('P','B','M','c')
#define TPS_CMD_PBMe            TPS_4CC('P','B','M','e')

#define TPS_PATCH_TIMEOUT_MS    (2000u)   /* whole-bundle budget */
#define TPS_BURST_CHUNK         (256u)    /* bytes per burst I2C write */

/* CHECK: burst target address is returned by PBMs; this is the default
 * suggested in the TRM examples. Distinct from TPS25750_PD_CONTROLLER_ADDR, 
 * burst mode targets a separate alternate address, not a duplicate of it. */
#define TPS_BURST_ADDR_DEFAULT  (0x22u << 1)

/* INA3221 CONFIG value (ch1+ch2 enabled, ch3 grounded/unused, 16-sample
 * averaging, 1.1 ms bus & shunt conversion, continuous shunt+bus mode).
 * Address/register-offset macros come from system/defines.h. */
#define INA3221_CONFIG_VALUE    (0x6527u)

/* STPD01 safe-default encodings (VOUT: 3.0 V + 20 mV/LSB, ILIM: 100 mA +
 * 100 mA/LSB, see datasheet tables). Address/register-offset macros come
 * from system/defines.h. */
#define STPD01_VOUT_5V          ((uint8_t)((5000u - 3000u) / 20u))
#define STPD01_ILIM_3A          ((uint8_t)((3000u - 100u) / 100u))

/* TPS25750 register access goes through system/tps25750_io.c, which
 * implements the host interface's length-prefixed payload framing:
 * a write is [reg][len][data...], a read returns [len][data...]. */

/** Send a 4CC command through CMD1 and poll until it completes. */
static HAL_StatusTypeDef tps_cmd(uint32_t cmd, uint32_t timeout_ms)
{
    uint32_t t0 = HAL_GetTick();
    uint32_t readback = 0;

    if (tps25750_write(TPS_REG_CMD1, (const uint8_t *)&cmd, 4u) != HAL_OK)
        return HAL_ERROR;

    /* CMD1 reads back 0 on success, '!CMD' on rejection. */
    do {
        if (tps25750_read(TPS_REG_CMD1, (uint8_t *)&readback, 4u) != HAL_OK)
            return HAL_ERROR;
        if (readback == 0u)
            return HAL_OK;
        if (readback == TPS_4CC('!','C','M','D'))
            return HAL_ERROR;
        HAL_Delay(2);
    } while ((HAL_GetTick() - t0) < timeout_ms);

    return HAL_TIMEOUT;
}

static HAL_StatusTypeDef tps_get_mode(uint32_t *mode)
{
    return tps25750_read(TPS_REG_MODE, (uint8_t *)mode, 4u);
}

/* =========================================================
 * Step 1: TPS25750 patch stream
 * ========================================================= */

static InitStatus_t init_tps25750(void)
{
    uint32_t mode = 0;

    if (tps_get_mode(&mode) != HAL_OK)
        return INIT_FAILED;

    if (mode == TPS_MODE_APP)
        return INIT_OK;             /* already patched (warm reboot) */

    if (mode != TPS_MODE_PTCH)
        return INIT_FAILED;         /* BOOT/unknown: needs investigation */

    /* --- PBMs: announce the bundle ---------------------------------
     * Data layout (CHECK against TRM "Patch Bundle Burst Mode"):
     *   [0..3] bundle size, little endian
     *   [4]    burst slave address (8-bit)
     *   [5]    timeout, units of 100 ms
     */
    {
        uint8_t d[6];
        d[0] = (uint8_t)(tps25750_bundle_size & 0xFFu);
        d[1] = (uint8_t)((tps25750_bundle_size >> 8) & 0xFFu);
        d[2] = (uint8_t)((tps25750_bundle_size >> 16) & 0xFFu);
        d[3] = (uint8_t)((tps25750_bundle_size >> 24) & 0xFFu);
        d[4] = TPS_BURST_ADDR_DEFAULT;
        d[5] = (uint8_t)(TPS_PATCH_TIMEOUT_MS / 100u);

        if (tps25750_write(TPS_REG_DATA1, d, sizeof(d)) != HAL_OK)
            return INIT_FAILED;
        if (tps_cmd(TPS_CMD_PBMs, 200u) != HAL_OK)
            return INIT_FAILED;
    }

    /* --- Burst: stream the bundle as raw writes to the burst address */
    {
        uint32_t off = 0;
        while (off < tps25750_bundle_size) {
            uint16_t n = (uint16_t)((tps25750_bundle_size - off) > TPS_BURST_CHUNK
                                    ? TPS_BURST_CHUNK
                                    : (tps25750_bundle_size - off));
            if (HAL_I2C_Master_Transmit(&hi2c3, TPS_BURST_ADDR_DEFAULT,
                                        (uint8_t *)&tps25750_bundle[off], n,
                                        I2C_TIMEOUT_MS) != HAL_OK) {
                (void)tps_cmd(TPS_CMD_PBMe, 200u);   /* abort cleanly */
                return INIT_FAILED;
            }
            off += n;
        }
    }

    /* --- PBMc: complete and wait for APP mode ----------------------- */
    if (tps_cmd(TPS_CMD_PBMc, 1000u) != HAL_OK)
        return INIT_FAILED;

    {
        uint32_t t0 = HAL_GetTick();
        do {
            if (tps_get_mode(&mode) == HAL_OK && mode == TPS_MODE_APP)
                return INIT_OK;
            HAL_Delay(10);
        } while ((HAL_GetTick() - t0) < 500u);
    }

    return INIT_FAILED;
}

/* =========================================================
 * Step 2: INA3221 configuration
 * ========================================================= */

static InitStatus_t init_ina3221(void)
{
    uint8_t  w[3] = { INA3221_REG_CONFIG,
                      (uint8_t)(INA3221_CONFIG_VALUE >> 8),
                      (uint8_t)(INA3221_CONFIG_VALUE & 0xFFu) };
    uint8_t  r[2] = { 0, 0 };

    if (HAL_I2C_Master_Transmit(&hi2c1, INA3221_ADDR, w, 3u,
                                I2C_TIMEOUT_MS) != HAL_OK)
        return INIT_FAILED;

    /* Read back to confirm the device is alive and took the value. */
    if (HAL_I2C_Mem_Read(&hi2c1, INA3221_ADDR, INA3221_REG_CONFIG,
                         I2C_MEMADD_SIZE_8BIT, r, 2u,
                         I2C_TIMEOUT_MS) != HAL_OK)
        return INIT_FAILED;

    return (((uint16_t)r[0] << 8 | r[1]) == INA3221_CONFIG_VALUE)
           ? INIT_OK : INIT_FAILED;
}

/* =========================================================
 * Step 3: STPD01 safe defaults (output kept disabled)
 * ========================================================= */

static InitStatus_t init_stpd01(void)
{
    uint8_t vout[2] = { VOUT_REG_ADDR, STPD01_VOUT_5V };
    uint8_t ilim[2] = { ILIM_REG_ADDR, STPD01_ILIM_3A };
    uint8_t dis [2] = { DIGITAL_ENABLE_REG_ADDR, 0x00u };

    if (HAL_I2C_Master_Transmit(&hi2c1, STPD01_PD_ADDR, vout, 2u,
                                I2C_TIMEOUT_MS) != HAL_OK)
        return INIT_FAILED;
    if (HAL_I2C_Master_Transmit(&hi2c1, STPD01_PD_ADDR, ilim, 2u,
                                I2C_TIMEOUT_MS) != HAL_OK)
        return INIT_FAILED;
    if (HAL_I2C_Master_Transmit(&hi2c1, STPD01_PD_ADDR, dis, 2u,
                                I2C_TIMEOUT_MS) != HAL_OK)
        return INIT_FAILED;

    return INIT_OK;
}

/* =========================================================
 * Step table and runner
 * ========================================================= */

typedef InitStatus_t (*InitFn_t)(void);

typedef struct {
    const char *name;
    InitFn_t   fn;
    uint8_t     critical;
} InitStep_t;

static InitReport_t report[3] = {
    { "TPS25750", INIT_NOT_RUN, 1u },
    { "INA3221",  INIT_NOT_RUN, 0u },
    { "STPD01",   INIT_NOT_RUN, 0u },
};

static const InitStep_t steps[3] = {
    { "TPS25750", init_tps25750, 1u },
    { "INA3221",  init_ina3221,  0u },
    { "STPD01",   init_stpd01,   0u },
};

uint8_t System_Initialization(void)
{
    uint8_t failed = 0;

    for (uint8_t i = 0; i < 3u; i++) {
        InitStatus_t st = INIT_FAILED;

        for (uint8_t attempt = 0; attempt < STEP_RETRIES; attempt++) {
            st = steps[i].fn();
            if (st == INIT_OK)
                break;
            HAL_Delay(20);
        }

        report[i].status = st;
        if (st != INIT_OK)
            failed++;
        /* Non-critical failures degrade, critical ones too: the boot
         * always continues so the UI can display what went wrong. */
    }

    return failed;
}

const InitReport_t *Init_GetReport(uint8_t *count)
{
    if (count != NULL)
        *count = 3u;
    return report;
}
