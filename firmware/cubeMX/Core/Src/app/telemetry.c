/**
 * @file    telemetry.c
 * @brief   Telemetry module: reads BQ34Z100 via I2C1 (I2C_LP / ISO1540).
 *
 * BQ25713 is on TPS25750's private I2C_EX bus — not reachable from STM32.
 * Charge state is derived from BQ34Z100 CHG flag and current sign.
 *
 * CHECK before first use:
 *   - BQ34Z100 register byte order (little-endian per TRM SLUSAW5)
 *   - TTE/TTF register addresses (0x18 / 0x1A per TRM "Standard Commands")
 *   - Cycle count 0x2A, SoH 0x2E, FCC 0x10 per TRM
 */

#include "app/telemetry.h"
#include "main.h"
#include "i2c.h"     /* hi2c1 (I2C_LP) */

#include <string.h>

/* -----------------------------------------------------------------------
 * Globals (declared extern in telemetry.h)
 * ----------------------------------------------------------------------- */

SystemTelemetry_t telemetry;
PortStats_t       port_stats[5];   /* A1/A2/C1(OTG)/C2/Lab — stubbed for now */
uint16_t          cell_mv[4];      /* per-cell not routed to MCU on this PCB */
uint16_t          tte_min;
uint16_t          ttf_min;
SystemStats_t     sys_stats;

/* -----------------------------------------------------------------------
 * Private state
 * ----------------------------------------------------------------------- */

static I2C_HandleTypeDef *_hi2c_gauge;   /* hi2c1 */
static uint8_t            _was_charging; /* detect charge-session starts */

#define GAUGE_ADDR  (0x55u << 1)
#define I2C_TO      (50u)

/* -----------------------------------------------------------------------
 * Low-level helpers
 * ----------------------------------------------------------------------- */

static HAL_StatusTypeDef gauge_read(uint8_t reg, uint8_t *buf, uint8_t n)
{
    if (HAL_I2C_Master_Transmit(_hi2c_gauge, GAUGE_ADDR, &reg, 1u, I2C_TO) != HAL_OK)
        return HAL_ERROR;
    return HAL_I2C_Master_Receive(_hi2c_gauge, GAUGE_ADDR, buf, n, I2C_TO);
}

/* -----------------------------------------------------------------------
 * Core poll
 * ----------------------------------------------------------------------- */

static void do_poll(void)
{
    uint8_t buf[2];
    uint8_t ok = 1u;

    /* SoC [%] */
    if (gauge_read(BQ34Z100_REG_SOC, buf, 2u) == HAL_OK)
        telemetry.soc_percent = buf[0];
    else ok = 0u;

    /* Voltage [mV], little-endian */
    if (gauge_read(BQ34Z100_REG_VOLTAGE, buf, 2u) == HAL_OK)
        telemetry.voltage_mV = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    else ok = 0u;

    /* Current [mA] signed, little-endian */
    if (gauge_read(BQ34Z100_REG_CURRENT, buf, 2u) == HAL_OK)
        telemetry.current_mA = (int16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
    else ok = 0u;

    /* Temperature: raw in 0.1 K units → convert to °C */
    if (gauge_read(BQ34Z100_REG_TEMP, buf, 2u) == HAL_OK) {
        uint16_t raw = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
        telemetry.temp_celsius = (int16_t)((int32_t)raw / 10 - 273);
    } else ok = 0u;

    /* Status flags */
    if (gauge_read(BQ34Z100_REG_FLAGS, buf, 2u) == HAL_OK) {
        uint16_t f = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
        telemetry.is_full     = (f & BQ34Z100_FLAG_FC)  ? 1u : 0u;
        telemetry.is_charging = (f & BQ34Z100_FLAG_CHG) ? 1u : 0u;
        telemetry.over_temp   = (f & BQ34Z100_FLAG_OT)  ? 1u : 0u;
    } else ok = 0u;

    /* CHG flag can lag; trust current sign too */
    if (telemetry.current_mA > 0)
        telemetry.is_charging = 1u;

    /* BQ25713 not reachable, derive from gauge */
    telemetry.vbus_present = telemetry.is_charging;
    telemetry.charge_phase = telemetry.is_charging ? 2u : 0u;   /* 2 = fast */

    /* Power [mW] */
    telemetry.power_mW = (int32_t)telemetry.voltage_mV * telemetry.current_mA / 1000;

    /* TTE / TTF [min] — BQ34Z100 std commands 0x18/0x1A */
    if (gauge_read(0x18u, buf, 2u) == HAL_OK)
        tte_min = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    if (gauge_read(0x1Au, buf, 2u) == HAL_OK)
        ttf_min = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);

    telemetry.current_history[telemetry.history_idx] = telemetry.current_mA;
    telemetry.history_idx = (telemetry.history_idx + 1u) % TELEMETRY_HISTORY_SIZE;
    if (telemetry.history_idx == 0u)
        telemetry.history_full = 1u;

    if (telemetry.is_charging && !_was_charging)
        sys_stats.charge_sessions++;
    _was_charging = telemetry.is_charging;

    if (telemetry.temp_celsius > sys_stats.max_temp_c)
        sys_stats.max_temp_c = telemetry.temp_celsius;
    if (telemetry.current_mA > sys_stats.max_current_in_mA)
        sys_stats.max_current_in_mA = telemetry.current_mA;
    if (telemetry.current_mA < -sys_stats.max_current_out_mA)
        sys_stats.max_current_out_mA = (int16_t)(-telemetry.current_mA);

    sys_stats.uptime_s = HAL_GetTick() / 1000u;

    /* gauge flash: non-critical, ignore single read failures */
    if (gauge_read(0x2Au, buf, 2u) == HAL_OK)
        sys_stats.cycle_count = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    if (gauge_read(0x2Eu, buf, 2u) == HAL_OK)
        sys_stats.state_of_health = buf[0];
    if (gauge_read(0x10u, buf, 2u) == HAL_OK)
        sys_stats.full_cap_mAh = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);

    sys_stats.design_cap_mAh = 7800u;   /* 3P Murata VTC5, see provisioning.c */

    telemetry.last_poll_tick = HAL_GetTick();
    telemetry.sensor_ok = ok;
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

void Telemetry_Init(I2C_HandleTypeDef *hi2c1_ptr, I2C_HandleTypeDef *hi2c3_ptr)
{
    (void)hi2c3_ptr;   /* BQ25713 not on any MCU-accessible bus */
    _hi2c_gauge   = hi2c1_ptr;
    _was_charging = 0u;
    memset(&telemetry, 0, sizeof(telemetry));
    memset(port_stats, 0, sizeof(port_stats));
    memset(cell_mv,    0, sizeof(cell_mv));
    memset(&sys_stats, 0, sizeof(sys_stats));
    tte_min = 0u;
    ttf_min = 0u;
}

uint8_t Telemetry_Poll(void)
{
    if ((HAL_GetTick() - telemetry.last_poll_tick) < TELEMETRY_POLL_INTERVAL_MS)
        return 0u;
    do_poll();
    return 1u;
}

void Telemetry_ForcePoll(void)
{
    do_poll();
}
