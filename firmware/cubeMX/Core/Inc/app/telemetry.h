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
 * ARCHITECTURE / ARCHITETTURA:
 *   BQ34Z100 (I2C3) ──┐
 *   BQ25713  (I2C1) ──┼──> Telemetry_Poll() ──> SystemTelemetry_t
 *   NTC ADC        ──┘
 *
 * TYPICAL USE / USO TIPICO:
 *   // In the main loop, every 500 ms / Nel loop principale, ogni 500ms:
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
 * I2C CHIP ADDRESSES / INDIRIZZI I2C DEI CHIP
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

    /* --- Battery data from BQ34Z100 via I2C3 / Dati batteria dal BQ34Z100 via I2C3 --- */
    uint8_t  soc_percent;   /**< State of Charge: 0–100% */
    uint16_t voltage_mV;    /**< Total pack voltage in mV (typical: 10000–16800)*/
    int16_t  current_mA;    /**< Current in mA. Positive = charging, negative = discharging*/
    int16_t  temp_celsius;  /**< Temperature in whole degrees Celsius */
    uint8_t  is_full;       /**< 1 if the FC flag from the fuel gauge is set */
    uint8_t  is_charging;   /**< 1 if current > 0 (battery charging) */
    uint8_t  over_temp;     /**< 1 if the fuel gauge signals over-temperature */

    /* --- Charger data from BQ25713 via I2C1 / Dati charger dal BQ25713 via I2C1 --- */
    uint8_t  vbus_present;  /**< 1 if a USB-C power supply is connected */
    uint8_t  charge_phase;  /**< Charge phase: 0=idle, 1=pre-charge, 2=fast, 3=taper */

    /* --- Locally computed values / Valori calcolati localmente --- */
    int32_t  power_mW;      /**< Instantaneous power = voltage_mV * current_mA / 1000  */

    /* --- Current history ring buffer for the line graph / Ring buffer storico corrente per il grafico --- */
    int16_t  current_history[TELEMETRY_HISTORY_SIZE]; /**< Circular buffer of past samples / Buffer circolare dei campioni passati */
    uint8_t  history_idx;   /**< Index of the next free slot in the ring buffer */
    uint8_t  history_full;  /**< 1 once the buffer has been filled at least once */

    /* --- Polling metadata / Metadata del polling --- */
    uint32_t last_poll_tick; /**< HAL_GetTick() value of the last successful poll */
    uint8_t  sensor_ok;      /**< 1 if the last poll read all sensors successfully */

} SystemTelemetry_t;

/* =========================================================
 * GLOBAL VARIABLE
 * Declared here, defined in telemetry.c
 * ========================================================= */

/** Global struct readable by the entire application */
extern SystemTelemetry_t telemetry;

/* =========================================================
 * PUBLIC API
 * ========================================================= */

/**
 * @brief  Initialise the telemetry module.
 * @note   Call once in main() after MX_I2C1_Init() and MX_I2C3_Init().
 * @param  hi2c1_ptr  Pointer to the I2C1 handle (for BQ25713)
 * @param  hi2c3_ptr  Pointer to the I2C3 handle (for BQ34Z100)
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
