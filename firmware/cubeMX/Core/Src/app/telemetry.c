/**
 * @file    telemetry.c
 * @brief   Sensor polling implementation: BQ34Z100 fuel gauge and BQ25713 charger.
 *
 * IMPORTANT NOTE ON THE I2C BUSES:
 *   - I2C1 (hi2c1): system side. Connects BQ25713 and TPS25750.
 *   - I2C3 (hi2c3): battery side, ISOLATED via ISO1540. Connects BQ34Z100.
 *   Never mix the two buses! The fuel gauge references -BATT which may
 *   float relative to GND when the protection FETs open.
 *
 * NOTE (merge): Questo modulo legge BQ34Z100 e BQ25713 direttamente via I2C
 *   usando funzioni locali. Al merge con il branch di Francesco, le letture
 *   I2C vanno sostituite con le sue funzioni get (getFuelGaugeData(),
 *   getPrimaryUSBC_Contract()).
 *   Vedi i TODO inline in Telemetry_ForcePoll().
 */

#include "app/telemetry.h"
#include <string.h>     /* memset */

/* =========================================================
 * GLOBAL AND STATIC VARIABLES
 * ========================================================= */

/** Global data struct, accessible from the entire application via extern. */
SystemTelemetry_t telemetry;

/* I2C handle pointers saved during Telemetry_Init().*/
static I2C_HandleTypeDef *_hi2c1 = NULL;   /* BQ25713 */
static I2C_HandleTypeDef *_hi2c3 = NULL;   /* BQ34Z100 */

/* =========================================================
 * PRIVATE HELPER FUNCTIONS
 * ========================================================= */

/**
 * @brief  Read 2 bytes from a BQ34Z100 register via I2C3.
 *
 * The BQ34Z100 uses the SBS (Smart Battery System) protocol:
 * send the register address byte, then read the 2-byte response.
 * Data arrives in little-endian order (low byte first).
 *
 * @param  reg  Register address 
 * @param  out  Pointer where the 16-bit value will be written
 * @retval HAL_OK if successful, HAL_ERROR otherwise 
 *
 * TODO (merge): Rimuovere questa funzione e read_bq25713_reg() quando
 *   Telemetry_ForcePoll() usa getFuelGaugeData() di Francesco.
 */
static HAL_StatusTypeDef read_bq34z100_reg(uint8_t reg, uint16_t *out)
{
    uint8_t buf[2] = {0, 0};

    /* Send the register address we want to read. */
    if (HAL_I2C_Master_Transmit(_hi2c3, BQ34Z100_I2C_ADDR, &reg, 1, 10) != HAL_OK)
        return HAL_ERROR;

    /* Read the 2-byte response.*/
    if (HAL_I2C_Master_Receive(_hi2c3, BQ34Z100_I2C_ADDR, buf, 2, 10) != HAL_OK)
        return HAL_ERROR;

    /* Reconstruct the value: buf[0] is the least significant byte (little-endian).*/
    *out = (uint16_t)(buf[0]) | ((uint16_t)(buf[1]) << 8);
    return HAL_OK;
}

/**
 * @brief  Read 1 byte from a BQ25713 register via I2C1.
 * @param  reg  Register address 
 * @param  out  Pointer where the byte will be written
 * @retval HAL_OK if successful, HAL_ERROR otherwise
 */
static HAL_StatusTypeDef read_bq25713_reg(uint8_t reg, uint8_t *out)
{
    if (HAL_I2C_Master_Transmit(_hi2c1, BQ25713_I2C_ADDR, &reg, 1, 10) != HAL_OK)
        return HAL_ERROR;

    if (HAL_I2C_Master_Receive(_hi2c1, BQ25713_I2C_ADDR, out, 1, 10) != HAL_OK)
        return HAL_ERROR;

    return HAL_OK;
}

/**
 * @brief  Append a sample to the current-history ring buffer.
 *
 * Ring buffer: when the index reaches the end, it wraps back to the start
 * and overwrites the oldest data. Ideal for a sliding-window history.
 *
 */
static void push_to_history(int16_t sample)
{
    telemetry.current_history[telemetry.history_idx] = sample;
    telemetry.history_idx = (telemetry.history_idx + 1) % TELEMETRY_HISTORY_SIZE;

    /* After the first full lap the buffer is completely filled. */
    if (telemetry.history_idx == 0)
        telemetry.history_full = 1;
}

/* =========================================================
 * PUBLIC FUNCTIONS 
 * ========================================================= */

/* TODO (merge): Rimuovere i parametri hi2c1_ptr e hi2c3_ptr quando
 *   Telemetry_ForcePoll() non accede più al bus I2C direttamente. */
void Telemetry_Init(I2C_HandleTypeDef *hi2c1_ptr, I2C_HandleTypeDef *hi2c3_ptr)
{
    /* Save the I2C handle pointers for later use.*/
    _hi2c1 = hi2c1_ptr;
    _hi2c3 = hi2c3_ptr;

    /* Zero-initialise the entire struct (sensor_ok = 0, all values = 0). */
    memset(&telemetry, 0, sizeof(SystemTelemetry_t));

    /* Force an immediate poll at startup so the UI has real data right away
     * instead of showing zeros everywhere. */
    Telemetry_ForcePoll();
}

uint8_t Telemetry_Poll(void)
{
    uint32_t now = HAL_GetTick();

    /* If it is too early, do nothing and return 0. */
    if ((now - telemetry.last_poll_tick) < TELEMETRY_POLL_INTERVAL_MS)
        return 0;

    telemetry.last_poll_tick = now;
    Telemetry_ForcePoll();
    return 1;
}

void Telemetry_ForcePoll(void)
{
    uint16_t raw  = 0;
    uint8_t  raw8 = 0;
    uint8_t  all_ok = 1;   /* becomes 0 if even one chip does not respond */

    /* ---- Read BQ34Z100 (fuel gauge, on I2C3) ---- */

    /* TODO (merge): Sostituire le letture BQ34Z100 qui sotto con:
     *     FuelGaugeSensors fg = getFuelGaugeData();
     *   Voltage e current sono in mV e mA */

    /* State of Charge: the register returns a value in 0.01 % units. */
    if (read_bq34z100_reg(BQ34Z100_REG_SOC, &raw) == HAL_OK)
    {
        telemetry.soc_percent = (uint8_t)(raw / 100);
        if (telemetry.soc_percent > 100) telemetry.soc_percent = 100;
        /* TODO (merge): telemetry.soc_percent = (uint8_t)fg.SoC; */
    }
    else { all_ok = 0; }

    /* Pack voltage in mV. */
    if (read_bq34z100_reg(BQ34Z100_REG_VOLTAGE, &raw) == HAL_OK)
    {
        telemetry.voltage_mV = raw;
        /* TODO (merge): telemetry.voltage_mV = (uint16_t)fg.voltage; */
    }
    else { all_ok = 0; }

    /* Signed current in mA (int16_t: positive = charging, negative = discharging).
     *
     * Casting uint16_t to int16_t gives the correct sign automatically
     * thanks to two's complement representation. */
    if (read_bq34z100_reg(BQ34Z100_REG_CURRENT, &raw) == HAL_OK)
    {
        telemetry.current_mA = (int16_t)raw;
        /* TODO (merge): telemetry.current_mA = (int16_t)fg.current; */
    }
    else { all_ok = 0; }

    /* Temperature: the register gives a value in tenths of Kelvin.
     *
     * Conversion: T[K] = raw / 10 --> T[°C] = T[K] - 273
     * Example: raw = 2981 --> 298.1 K --> 25.1°C --> 25°C */
    if (read_bq34z100_reg(BQ34Z100_REG_TEMP, &raw) == HAL_OK)
    {
        telemetry.temp_celsius = (int16_t)((raw / 10) - 273);
        /* TODO (merge): telemetry.temp_celsius = (int16_t)fg.internalTemperature;
         *   Nota: La conversione K->°C la fa già charge.c, il valore arriva già in °C. */
    }
    else { all_ok = 0; }

    /* Status flags: Full Charge, Charging, Over Temperature. */
    if (read_bq34z100_reg(BQ34Z100_REG_FLAGS, &raw) == HAL_OK)
    {
        telemetry.is_full     = (raw & BQ34Z100_FLAG_FC)  ? 1 : 0;
        telemetry.is_charging = (raw & BQ34Z100_FLAG_CHG) ? 1 : 0;
        telemetry.over_temp   = (raw & BQ34Z100_FLAG_OT)  ? 1 : 0;
        /* TODO (merge):
         *   telemetry.is_full     = fg.flags.FC;
         *   telemetry.is_charging = fg.flags.CHG;
         *   telemetry.over_temp   = fg.flags.OTC; */
    }
    else { all_ok = 0; }

    /* ---- Read BQ25713 (charger, on I2C1) ---- */

    /* TODO (merge): Sostituire le letture BQ25713 qui sotto con:
     *     PDContract contract = getPrimaryUSBC_Contract(); */

    /* Charger status: bits [7:5] of CHGSTATUS0 = VBUS_STAT.
     * 000 = no input; any other value = input present.*/
    if (read_bq25713_reg(BQ25713_REG_CHGSTATUS0, &raw8) == HAL_OK)
    {
        telemetry.vbus_present = ((raw8 >> 5) != 0) ? 1 : 0;
        /* TODO (merge): telemetry.vbus_present = contract.isPlugged; */
    }
    else { all_ok = 0; }

    /* Charge phase from CHGSTATUS1, bits [2:0] = CHRG_STAT. */
    if (read_bq25713_reg(BQ25713_REG_CHGSTATUS1, &raw8) == HAL_OK)
    {
        telemetry.charge_phase = raw8 & 0x07;
        /* FIXME (merge): charge_phase (CHRG_STAT del BQ25713) non ha un
         *   equivalente in charge.h. Mantenere direttamente questa lettura? */
    }
    else { all_ok = 0; }

    /* ---- Derived calculations ---- */

    /* Instantaneous power: P = V * I
     *
     * voltage_mV * current_mA / 1000 = power_mW
     * We use int32_t to avoid overflow: 16800 mV * 5000 mA = 84,000,000
     * which does not fit in int16_t (max 32767) but fits easily in int32_t.
    */
    telemetry.power_mW = ((int32_t)telemetry.voltage_mV *
                          (int32_t)telemetry.current_mA) / 1000;

    /* Append current sample to the ring buffer for the graph. */
    push_to_history(telemetry.current_mA);

    /* Update the sensor health flag. */
    /* TODO (merge): Le funzioni get di Francesco non restituiscono HAL_OK/HAL_ERROR,
     *   quindi sensor_ok non si può più derivare. Concordare 
     *   un modo per rilevare errori di lettura. */
    telemetry.sensor_ok = all_ok;
}