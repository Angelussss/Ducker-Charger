/**
 * @file    telemetry.c
 * @brief   Telemetry aggregation module for the UI layer.
 *
 * Does NOT touch I2C itself: the fuel gauge (BQ34Z100), INA3221 and PD
 * contracts are already polled by the charge-management layer every FSM
 * tick (system/charge.c). This module only reads the charge layer's
 * getters and reshapes the data into the UI-facing SystemTelemetry_t /
 * PortStats_t / SystemStats_t structs (plus locally-computed history and
 * lifetime stats that the charge layer has no reason to know about).
 *
 * BQ25713 is on TPS25750's private I2C_EX bus — not reachable from STM32.
 * Charge state is derived from the BQ34Z100 CHG flag and the primary
 * USB-C PD contract (sink = charger connected).
 */

#include "app/telemetry.h"
#include "system/charge.h"

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

static uint8_t _was_charging; /* detect charge-session starts */

/* -----------------------------------------------------------------------
 * Core poll
 * ----------------------------------------------------------------------- */

static void do_poll(void)
{
    FuelGaugeSensors fg  = getFuelGaugeData();
    INA3221_Sensors  ina = getINA3221_Sensors();
    PDContract primary   = getPrimaryUSBC_Contract();
    PDContract secondary = getSecondaryUSBC_Contract();

    telemetry.soc_percent   = (uint8_t)fg.SoC;
    telemetry.voltage_mV    = (uint16_t)fg.voltage;
    telemetry.current_mA    = (int16_t)fg.current;
    /* Battery-pack NTC is read via the gauge's "external temperature"
     * command (0x0C) — see charge.c readSensors(). */
    telemetry.temp_celsius  = (int16_t)fg.externalTemperature;
    telemetry.is_full       = fg.flags.FC  ? 1u : 0u;
    telemetry.over_temp     = (fg.flags.OTC || fg.flags.OTD) ? 1u : 0u;

    /* CHG flag can lag; trust current sign too */
    telemetry.is_charging = (fg.flags.CHG || telemetry.current_mA > 0) ? 1u : 0u;

    /* vbus_present: primary USB-C is plugged in acting as sink (being charged) */
    telemetry.vbus_present = (primary.isPlugged && primary.isSink) ? 1u : 0u;
    telemetry.charge_phase = telemetry.is_charging ? 2u : 0u;   /* 2 = fast */

    /* Power [mW] */
    telemetry.power_mW = (int32_t)telemetry.voltage_mV * telemetry.current_mA / 1000;

    tte_min = (uint16_t)fg.avgTimeToEmpty;
    ttf_min = (uint16_t)fg.avgTimeToFull;

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

    sys_stats.cycle_count      = (uint16_t)fg.cycleCount;
    sys_stats.state_of_health  = (uint8_t)fg.stateOfHealth;
    sys_stats.design_cap_mAh   = 7800u;   /* 3P Murata VTC5, see provisioning.c */
    /* Charge layer doesn't track full-charge capacity (FCC) separately —
     * approximate today's capacity from design capacity and SoH. */
    sys_stats.full_cap_mAh     = (uint16_t)(sys_stats.design_cap_mAh * fg.stateOfHealth / 100u);

    /* Per-port monitor data: only A1/A2 have real shunts (INA3221 ch1/ch2). */
    port_stats[0].current_mA = (int16_t)ina.current_channel1;
    port_stats[0].active     = (uint8_t)get_USBA1_Status();
    port_stats[1].current_mA = (int16_t)ina.current_channel2;
    port_stats[1].active     = (uint8_t)get_USBA2_Status();
    port_stats[2].active     = (uint8_t)get_OTG_Status();
    port_stats[3].active     = (uint8_t)get_USBC2_Status();
    port_stats[3].voltage_mv = (uint16_t)secondary.voltage;
    port_stats[3].current_mA = (int16_t)secondary.operatingCurrent;

    telemetry.last_poll_tick = HAL_GetTick();
    /* Charge layer doesn't report per-read I2C success/failure via its
     * getters (data may be stale if a read failed silently upstream) —
     * best-effort until it does. */
    telemetry.sensor_ok = 1u;
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

void Telemetry_Init(I2C_HandleTypeDef *hi2c1_ptr, I2C_HandleTypeDef *hi2c3_ptr)
{
    /* I2C access lives entirely in the charge-management layer now. */
    (void)hi2c1_ptr;
    (void)hi2c3_ptr;
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
