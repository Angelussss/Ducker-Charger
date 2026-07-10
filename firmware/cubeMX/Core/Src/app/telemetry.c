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
PortStats_t       port_stats[5];   /* A1/A2/C1(OTG)/C2/Lab — see do_poll() */
uint16_t          tte_min;
uint16_t          ttf_min;
SystemStats_t     sys_stats;

/* -----------------------------------------------------------------------
 * Private state
 * ----------------------------------------------------------------------- */

static uint8_t _was_charging; /* detect charge-session starts */

/* Sub-mWh remainder of the discharge-energy integral, so that repeated
 * short polls don't each truncate to zero and lose the whole total. */
static uint32_t _energy_mW_ms;

/* Commit one sample to a port row and advance its ring buffer. The PORTS
 * screen reads history[]/idx directly, so every poll must push — including
 * a zero for an idle port, otherwise its trace freezes on stale samples. */
static void port_push(uint8_t i, uint16_t mv, int16_t ma, uint8_t active)
{
    PortStats_t *p = &port_stats[i];
    p->voltage_mv = mv;
    p->current_mA = ma;
    p->active     = active;
    p->history[p->idx] = ma;
    p->idx = (uint8_t)((p->idx + 1u) % TELEMETRY_HISTORY_SIZE);
}

/* -----------------------------------------------------------------------
 * Core poll
 * ----------------------------------------------------------------------- */

static void do_poll(void)
{
    FuelGaugeSensors fg  = getFuelGaugeData();
    INA3221_Sensors  ina = getINA3221_Sensors();
    PDContract primary   = getPrimaryUSBC_Contract();
    SensorData  sd       = getSensorData();

    telemetry.soc_percent   = (uint8_t)fg.SoC;
    telemetry.voltage_mV    = (uint16_t)fg.voltage;
    telemetry.current_mA    = (int16_t)fg.current;
    /* Battery-pack NTC is read via the gauge's "external temperature"
     * command (0x0C) — see charge.c readSensors(). */
    telemetry.temp_celsius  = (int16_t)fg.externalTemperature;
    telemetry.is_full       = fg.flags.FC  ? 1u : 0u;
    telemetry.over_temp     = (fg.flags.OTC || fg.flags.OTD) ? 1u : 0u;
    telemetry.over_volt     = fg.flags.BATHI ? 1u : 0u;
    telemetry.charge_inhibited = (fg.flags.CHG_INH || fg.flags.XCHG) ? 1u : 0u;

    /* CHG flag can lag; trust current sign too */
    telemetry.is_charging = (fg.flags.CHG || telemetry.current_mA > 0) ? 1u : 0u;

    /* vbus_present: primary USB-C is plugged in acting as sink (being charged) */
    telemetry.vbus_present = (primary.isPlugged && primary.isSink) ? 1u : 0u;
    telemetry.charge_phase = telemetry.is_charging ? 1u : 0u;

    /* Power [mW] */
    telemetry.power_mW = (int32_t)telemetry.voltage_mV * telemetry.current_mA / 1000;

    /* Integrate energy drawn out of the pack (STATS "Energy out" row).
     * last_poll_tick == 0 is the very first poll: no interval to integrate
     * over yet. The clamp keeps a long gap (DEEP_SLEEP stops this loop for
     * minutes) from both overflowing the accumulator and booking energy
     * that was never delivered at the last known rate. */
    uint32_t now_tick = HAL_GetTick();
    if (telemetry.last_poll_tick != 0u && telemetry.power_mW < 0)
    {
        uint32_t dt_ms = now_tick - telemetry.last_poll_tick;
        if (dt_ms > 4u * TELEMETRY_POLL_INTERVAL_MS)
            dt_ms = TELEMETRY_POLL_INTERVAL_MS;
        _energy_mW_ms += (uint32_t)(-telemetry.power_mW) * dt_ms;
        sys_stats.energy_out_mWh += _energy_mW_ms / 3600000u;
        _energy_mW_ms            %= 3600000u;
    }

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

    /* Per-port monitor: A1/A2 have real INA3221 shunts. C1/C2/Lab don't
     * (C1's OTG current is on a private bus; C2/Lab share STPD01 with
     * INA3221 ch3 grounded), so their current is derived below. */
    int16_t a1_mA = (int16_t)ina.current_channel1;
    int16_t a2_mA = (int16_t)ina.current_channel2;

    int32_t p_sys_mW  = (int32_t)(sd.power_sys_W * 1000.0f);
    int32_t p_usba_mW = 5 * ((int32_t)a1_mA + (int32_t)a2_mA); /* 5000 mV * mA / 1000 */
    int32_t p_rail_mW = p_sys_mW - p_usba_mW;
    if (p_rail_mW < 0)
        p_rail_mW = 0;   /* ADC noise around zero load */

    uint16_t rail_mv = (uint16_t)getSTPD01_SetpointVoltage();
    int16_t  rail_mA = (rail_mv > 0u)
                     ? (int16_t)(p_rail_mW * 1000 / (int32_t)rail_mv)
                     : 0;

    uint8_t stpd_on = get_STPD01_Enabled() ? 1u : 0u;
    uint8_t lab_on  = (get_LAB_Status()   && stpd_on) ? 1u : 0u;
    uint8_t c2_on   = (get_USBC2_Status() && stpd_on) ? 1u : 0u;

    /* C1 current: sink reads IADPT directly. Source (OTG) has no MCU
     * sense, so isolate it by subtracting the known A1/A2 + C2/Lab power
     * from total pack discharge (gauge current x voltage), then convert
     * the remainder to current at C1's own contract voltage. Crosses a
     * domain boundary (skips the other channels' own conversion loss),
     * so it runs a bit high — sizing, not calibration. */
    int16_t c1_mA = 0;
    if (primary.isSink) {
        c1_mA = (int16_t)sd.usbCInputCurrent;
    } else if (get_OTG_Status() && fg.current < 0.0f && primary.voltage > 0.0f) {
        int32_t pack_out_mW = (int32_t)((-fg.current) * fg.voltage / 1000.0f);
        int32_t c1_mW = pack_out_mW - p_usba_mW - p_rail_mW;
        if (c1_mW < 0) c1_mW = 0;
        c1_mA = (int16_t)(c1_mW * 1000 / (int32_t)primary.voltage);
    }

    port_push(0, 5000u, a1_mA, get_USBA1_Status() ? 1u : 0u);
    port_push(1, 5000u, a2_mA, get_USBA2_Status() ? 1u : 0u);
    port_push(2, (uint16_t)primary.voltage, c1_mA, primary.isPlugged ? 1u : 0u);
    /* rail_mv, not the raw negotiated secondary.voltage: with a user
     * ceiling below the contract, STPD01's setpoint is what's actually
     * delivered (see setSecondaryUSBC_VoltageCeiling in charge.c). */
    port_push(3, c2_on ? rail_mv : 0u, c2_on ? rail_mA : 0, c2_on);
    port_push(4, lab_on ? rail_mv : 0u, lab_on ? rail_mA : 0, lab_on);

    telemetry.last_poll_tick = now_tick;
    /* Honest freshness: charge.c tracks the gauge I2C status per cycle.
     * 0 means every battery number above is the LAST GOOD read, not live —
     * the UI flags it and the warning arbiter stays quiet. */
    telemetry.sensor_ok = getFuelGaugeReadOK() ? 1u : 0u;
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
    _energy_mW_ms = 0u;
    memset(&telemetry, 0, sizeof(telemetry));
    memset(port_stats, 0, sizeof(port_stats));
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
