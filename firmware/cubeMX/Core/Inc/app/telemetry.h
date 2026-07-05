/**
 * @file    telemetry.h
 * @brief   Real-time system data collection and aggregation.
 *          Raccolta e aggregazione dei dati di sistema in tempo reale.
 *
 * This module periodically reads all system sensors (fuel gauge, charger)
 * via I2C and stores everything in a single central struct that the entire
 * UI layer can read from.
 *
 * Questo modulo legge periodicamente tutti i sensori (fuel gauge, charger)
 * via I2C e salva tutto in una struttura centrale che tutta la UI puo' leggere.
 *
 * ARCHITECTURE:
 *   BQ34Z100 (hi2c1 / I2C_LP, via ISO1540)        ──┐
 *   BQ25713  (I2C_EX private to TPS25750, no MCU)  ──┼──> Telemetry_Poll() ──> SystemTelemetry_t
 *   NTC ADC                                        ──┘
 *
 * NOTE: BQ25713 is not reachable from the STM32. Charge status is
 * derived from BQ34Z100 CHG flag and current sign.
 *
 * TYPICAL USE:
 *   // in main loop, every 500 ms:
 *   Telemetry_Poll();
 *   uint8_t soc = telemetry.soc_percent;
 */

#ifndef __TELEMETRY_H
#define __TELEMETRY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

/* =========================================================
 * CONFIGURATION CONSTANTS
 * ========================================================= */

/**
 * Number of historical samples kept for the line graph.
 * At one sample every ~500 ms this gives 30 seconds of history.
 */
#define TELEMETRY_HISTORY_SIZE      60

/**
 * Minimum interval between two consecutive polls, in milliseconds.
 */
#define TELEMETRY_POLL_INTERVAL_MS  500

/* =========================================================
 * I2C CHIP ADDRESSES
 * ========================================================= */

/**
 * BQ34Z100-R2 fuel gauge on I2C3 (battery side, isolated via ISO1540).
 */
#define BQ34Z100_I2C_ADDR   (0x55 << 1)

/**
 * BQ25713 charger on I2C1 (system side).
 */
#define BQ25713_I2C_ADDR    (0x6B << 1)

/* =========================================================
 * BQ34Z100 REGISTERS
 * Source: TI datasheet SLUSAW5
 * ========================================================= */

#define BQ34Z100_REG_SOC     0x02  /**< State of Charge [%] – 2 bytes */
#define BQ34Z100_REG_VOLTAGE 0x08  /**< Pack voltage (tensione) [mV] – 2 bytes */
#define BQ34Z100_REG_CURRENT 0x0A  /**< Signed current [mA] – 2 bytes */
#define BQ34Z100_REG_TEMP    0x0C  /**< Temperature [0.1 K] – 2 bytes */
#define BQ34Z100_REG_FLAGS   0x06  /**< Status flags (FC, CHG, OT...) – 2 bytes */

/* =========================================================
 * BQ25713 REGISTERS
 * Source: TI datasheet SLUSDH0
 * ========================================================= */

#define BQ25713_REG_CHGSTATUS0  0x20  /**< Charger status: VBUS presence & input type */
#define BQ25713_REG_CHGSTATUS1  0x21  /**< Charger status: charge phase */

/* =========================================================
 * BQ34Z100 FLAG BITS
 * ========================================================= */

#define BQ34Z100_FLAG_FC   (1 << 9)   /**< Full Charged: battery completely full */
#define BQ34Z100_FLAG_CHG  (1 << 8)   /**< Charging detected */
#define BQ34Z100_FLAG_OT   (1 << 15)  /**< Over Temperature */

/* =========================================================
 * MAIN DATA STRUCTURE
 * ========================================================= */

/**
 * @brief Complete snapshot of the system state.
 *
 * Updated by Telemetry_Poll(), read by the entire UI.
 * Do not write to this struct directly from outside this module.
 *
 * CURRENT SIGN CONVENTION:
 *   Positive value = battery is CHARGING
 *   Negative value = battery is DISCHARGING 
 */
typedef struct {

    /* --- Battery data from BQ34Z100 via I2C1/I2C_LP */
    uint8_t  soc_percent;   /**< State of Charge: 0–100% */
    uint16_t voltage_mV;    /**< Total pack voltage in mV (typical: 10000–16800)*/
    int16_t  current_mA;    /**< Current in mA. Positive = charging, negative = discharging*/
    int16_t  temp_celsius;  /**< Temperature in whole degrees Celsius */
    uint8_t  is_full;       /**< 1 if the FC flag from the fuel gauge is set */
    uint8_t  is_charging;   /**< 1 if current > 0 (battery charging) */
    uint8_t  over_temp;     /**< 1 if the fuel gauge signals over-temperature */

    /* --- Charger data from BQ25713 via I2C1 */
    uint8_t  vbus_present;  /**< 1 if a USB-C power supply is connected */
    uint8_t  charge_phase;  /**< Charge phase: 0=idle, 1=pre-charge, 2=fast, 3=taper */

    /* --- Locally computed values */
    int32_t  power_mW;      /**< Instantaneous power = voltage_mV * current_mA / 1000  */

    /* --- Current history ring buffer for the line graph */
    int16_t  current_history[TELEMETRY_HISTORY_SIZE]; /**< Circular buffer of past samples */
    uint8_t  history_idx;   /**< Index of the next free slot in the ring buffer */
    uint8_t  history_full;  /**< 1 once the buffer has been filled at least once */

    /* --- Polling metadata */
    uint32_t last_poll_tick; /**< HAL_GetTick() value of the last successful poll */
    uint8_t  sensor_ok;      /**< 1 if the last poll read all sensors successfully */

} SystemTelemetry_t;

/* =========================================================
 * GLOBAL VARIABLE
 * Declared here, defined in telemetry.c
 * ========================================================= */

/** Global struct readable by the entire application */
extern SystemTelemetry_t telemetry;

/* ---- SIM EXTENSION: per-port monitor data ----
 * hardware truth: A1/A2 have real shunts (INA3221 ch1/ch2);
 * C2/Lab current need extra sensing on STPD01 rail. not there yet. */
typedef struct {
    uint16_t voltage_mv;
    int16_t  current_mA;
    int16_t  history[TELEMETRY_HISTORY_SIZE];
    uint8_t  idx;
    uint8_t  active;
} PortStats_t;
extern PortStats_t port_stats[5];   /* 0=USB-A1 1=USB-A2 2=USB-C1(OTG) 3=USB-C2 4=LAB */

/* voltages of 4 parallel groups (4S). HW truth: today board has NO
 * per-cell path to MCU (BQ77915 mute, BQ34Z100 only whole pack):
 * need dedicated sensing or multi-cell gauge. */
extern uint16_t cell_mv[4];

/* fuel gauge guesses (BQ34Z100 reg 0x18/0x1A, already mapped in
 * charge-management): minutes to empty / full. 0 = no data. */
extern uint16_t tte_min, ttf_min;

/* ---- SIM EXTENSION: overall statistics ----
 * lifetime rows free from BQ34Z100 (gauge remember in own flash);
 * session rows counted by MCU since boot. */
typedef struct {
    /* from fuel gauge (survive reboot) */
    uint16_t cycle_count;       /* full charge cycles */
    uint8_t  state_of_health;   /* % */
    uint16_t full_cap_mAh;      /* capacity today at full charge */
    uint16_t design_cap_mAh;
    /* counted by MCU (since boot) */
    uint16_t charge_sessions;   /* times charging started */
    int16_t  max_temp_c;
    int16_t  max_current_out_mA;
    int16_t  max_current_in_mA;
    uint32_t energy_out_mWh;
    uint32_t uptime_s;
} SystemStats_t;
extern SystemStats_t sys_stats;

/* =========================================================
 * PUBLIC API
 * ========================================================= */

/**
 * @brief  Initialise the telemetry module.
 * @param  hi2c1_ptr  I2C1 handle (BQ34Z100 via I2C_LP/ISO1540)
 * @param  hi2c3_ptr  reserved, unused (BQ25713 not reachable from MCU)
 */
void Telemetry_Init(I2C_HandleTypeDef *hi2c1_ptr, I2C_HandleTypeDef *hi2c3_ptr);

/**
 * @brief  Read all sensors and update the global `telemetry` struct.
 * @note   Call this in the main loop. Internally uses HAL_GetTick() to
 *         respect TELEMETRY_POLL_INTERVAL_MS and returns immediately if
 *         it is too early for the next poll.
 * @retval 1 if a poll was performed, 0 if it was too early 
 */
uint8_t Telemetry_Poll(void);

/**
 * @brief  Force an immediate poll, ignoring the interval timer.
 * @note   Useful at startup to have real data right away without waiting 500 ms.
 */
void Telemetry_ForcePoll(void);

#ifdef __cplusplus
}
#endif

#endif /* __TELEMETRY_H */
