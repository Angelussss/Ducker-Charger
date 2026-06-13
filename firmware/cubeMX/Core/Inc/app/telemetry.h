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
 * CONFIGURATION CONSTANTS / COSTANTI DI CONFIGURAZIONE
 * ========================================================= */

/**
 * Number of historical samples kept for the line graph.
 * At one sample every ~500 ms this gives 30 seconds of history.
 *
 * Numero di campioni storici tenuti per il grafico a linee.
 * A un campione ogni ~500ms si hanno 30 secondi di storia.
 */
#define TELEMETRY_HISTORY_SIZE      60

/**
 * Minimum interval between two consecutive polls, in milliseconds.
 * Intervallo minimo tra due poll consecutivi, in millisecondi.
 */
#define TELEMETRY_POLL_INTERVAL_MS  500

/* =========================================================
 * I2C CHIP ADDRESSES / INDIRIZZI I2C DEI CHIP
 * ========================================================= */

/**
 * BQ34Z100-R2 fuel gauge on I2C3 (battery side, isolated via ISO1540).
 * BQ34Z100-R2 fuel gauge su I2C3 (lato batteria, isolato via ISO1540).
 */
#define BQ34Z100_I2C_ADDR   (0x55 << 1)

/**
 * BQ25713 charger on I2C1 (system side).
 * BQ25713 charger su I2C1 (lato sistema).
 */
#define BQ25713_I2C_ADDR    (0x6B << 1)

/* =========================================================
 * BQ34Z100 REGISTERS / REGISTRI BQ34Z100
 * Source / Fonte: TI datasheet SLUSAW5
 * ========================================================= */

#define BQ34Z100_REG_SOC     0x02  /**< State of Charge [%] – 2 bytes / 2 byte */
#define BQ34Z100_REG_VOLTAGE 0x08  /**< Pack voltage [mV] – 2 bytes / Tensione pack [mV] – 2 byte */
#define BQ34Z100_REG_CURRENT 0x0A  /**< Signed current [mA] – 2 bytes / Corrente con segno [mA] – 2 byte */
#define BQ34Z100_REG_TEMP    0x0C  /**< Temperature [0.1 K] – 2 bytes / Temperatura [0.1 K] – 2 byte */
#define BQ34Z100_REG_FLAGS   0x06  /**< Status flags (FC, CHG, OT...) – 2 bytes / Flag di stato – 2 byte */

/* =========================================================
 * BQ25713 REGISTERS / REGISTRI BQ25713
 * Source / Fonte: TI datasheet SLUSDH0
 * ========================================================= */

#define BQ25713_REG_CHGSTATUS0  0x20  /**< Charger status: VBUS presence & input type / Stato charger: VBUS e tipo input */
#define BQ25713_REG_CHGSTATUS1  0x21  /**< Charger status: charge phase / Stato charger: fase di carica */

/* =========================================================
 * BQ34Z100 FLAG BITS / BIT DI FLAG DEL BQ34Z100
 * ========================================================= */

#define BQ34Z100_FLAG_FC   (1 << 9)   /**< Full Charged: battery completely full / Batteria completamente carica */
#define BQ34Z100_FLAG_CHG  (1 << 8)   /**< Charging detected / Rilevata carica in corso */
#define BQ34Z100_FLAG_OT   (1 << 15)  /**< Over Temperature / Sovratemperatura */

/* =========================================================
 * MAIN DATA STRUCTURE / STRUTTURA DATI PRINCIPALE
 * ========================================================= */

/**
 * @brief Complete snapshot of the system state.
 *        Snapshot completo dello stato del sistema.
 *
 * Updated by Telemetry_Poll(), read by the entire UI.
 * Do not write to this struct directly from outside this module.
 * Aggiornata da Telemetry_Poll(), letta da tutta la UI.
 * Non scrivere questa struttura direttamente dall'esterno.
 *
 * CURRENT SIGN CONVENTION / CONVENZIONE SUL SEGNO DELLA CORRENTE:
 *   Positive value = battery is CHARGING  / Valore positivo = batteria in CARICA
 *   Negative value = battery is DISCHARGING / Valore negativo = batteria in SCARICA
 */
typedef struct {

    /* --- Battery data from BQ34Z100 via I2C3 / Dati batteria dal BQ34Z100 via I2C3 --- */
    uint8_t  soc_percent;   /**< State of Charge: 0–100% */
    uint16_t voltage_mV;    /**< Total pack voltage in mV (typical: 10000–16800) / Tensione totale del pack in mV */
    int16_t  current_mA;    /**< Current in mA. Positive = charging, negative = discharging / Corrente in mA. Positivo = carica, negativo = scarica */
    int16_t  temp_celsius;  /**< Temperature in whole degrees Celsius / Temperatura in gradi Celsius interi */
    uint8_t  is_full;       /**< 1 if the FC flag from the fuel gauge is set / 1 se il flag FC del fuel gauge e' attivo */
    uint8_t  is_charging;   /**< 1 if current > 0 (battery charging) / 1 se corrente > 0 (batteria in carica) */
    uint8_t  over_temp;     /**< 1 if the fuel gauge signals over-temperature / 1 se il fuel gauge segnala sovratemperatura */

    /* --- Charger data from BQ25713 via I2C1 / Dati charger dal BQ25713 via I2C1 --- */
    uint8_t  vbus_present;  /**< 1 if a USB-C power supply is connected / 1 se un alimentatore USB-C e' collegato */
    uint8_t  charge_phase;  /**< Charge phase: 0=idle, 1=pre-charge, 2=fast, 3=taper / Fase di carica */

    /* --- Locally computed values / Valori calcolati localmente --- */
    int32_t  power_mW;      /**< Instantaneous power = voltage_mV * current_mA / 1000 / Potenza istantanea */

    /* --- Current history ring buffer for the line graph / Ring buffer storico corrente per il grafico --- */
    int16_t  current_history[TELEMETRY_HISTORY_SIZE]; /**< Circular buffer of past samples / Buffer circolare dei campioni passati */
    uint8_t  history_idx;   /**< Index of the next free slot in the ring buffer / Indice del prossimo slot libero nel ring buffer */
    uint8_t  history_full;  /**< 1 once the buffer has been filled at least once / 1 dopo il primo giro completo del buffer */

    /* --- Polling metadata / Metadata del polling --- */
    uint32_t last_poll_tick; /**< HAL_GetTick() value of the last successful poll / Valore di HAL_GetTick() dell'ultimo poll riuscito */
    uint8_t  sensor_ok;      /**< 1 if the last poll read all sensors successfully / 1 se l'ultimo poll ha letto tutti i sensori correttamente */

} SystemTelemetry_t;

/* =========================================================
 * GLOBAL VARIABLE / VARIABILE GLOBALE
 * Declared here, defined in telemetry.c
 * Dichiarata qui, definita in telemetry.c
 * ========================================================= */

/** Global struct readable by the entire application / Struttura globale leggibile da tutta l'applicazione */
extern SystemTelemetry_t telemetry;

/* =========================================================
 * PUBLIC API / API PUBBLICA
 * ========================================================= */

/**
 * @brief  Initialise the telemetry module.
 *         Inizializza il modulo telemetria.
 * @note   Call once in main() after MX_I2C1_Init() and MX_I2C3_Init().
 *         Chiamare una volta in main() dopo MX_I2C1_Init() e MX_I2C3_Init().
 * @param  hi2c1_ptr  Pointer to the I2C1 handle (for BQ25713) / Puntatore all'handle I2C1 (per BQ25713)
 * @param  hi2c3_ptr  Pointer to the I2C3 handle (for BQ34Z100) / Puntatore all'handle I2C3 (per BQ34Z100)
 */
void Telemetry_Init(I2C_HandleTypeDef *hi2c1_ptr, I2C_HandleTypeDef *hi2c3_ptr);

/**
 * @brief  Read all sensors and update the global `telemetry` struct.
 *         Legge tutti i sensori e aggiorna la struttura globale `telemetry`.
 * @note   Call this in the main loop. Internally uses HAL_GetTick() to
 *         respect TELEMETRY_POLL_INTERVAL_MS and returns immediately if
 *         it is too early for the next poll.
 *         Chiamare nel loop principale. Usa HAL_GetTick() internamente per
 *         rispettare TELEMETRY_POLL_INTERVAL_MS e ritorna subito se e' troppo presto.
 * @retval 1 if a poll was performed, 0 if it was too early /
 *         1 se e' stato eseguito un poll, 0 se era troppo presto
 */
uint8_t Telemetry_Poll(void);

/**
 * @brief  Force an immediate poll, ignoring the interval timer.
 *         Forza un poll immediato ignorando il timer di intervallo.
 * @note   Useful at startup to have real data right away without waiting 500 ms.
 *         Utile all'avvio per avere dati reali subito senza aspettare 500ms.
 */
void Telemetry_ForcePoll(void);

#ifdef __cplusplus
}
#endif

#endif /* __TELEMETRY_H */
