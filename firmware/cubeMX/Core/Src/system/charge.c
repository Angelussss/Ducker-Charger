#include "system/charge.h"
#include "math.h"
#include "stm32f4xx_hal.h"
#include "adc.h"
#include "i2c.h"

// Ports: USB-A1; USB-A2; USB-C (primary, via TPS25750); USB-C (secondary, via CYPD3175)
// I2C1 -> CYPD3175 STPD01PUR INA3221 BQ34Z100
// I2C3 -> TPS25750

/*
    To do:

        --- Hardware checks ---
        - Check constants for temperature sensors
        - Check TPS25750_PD_CONTROLLER_ADDR if it's 0x20 or 0x21 (currently using 0x20)
        - Check whether STPD01 EN (PC11) needs to be asserted early in init() to power internal peripherals, or only after
          PD negotiation for USB-C2 output. Current behavior: enabled after negotiation

        --- Software checks ---
        - Finalize throws for faults, overtemperature, short-circuit, etc...
        - If I2C reading with interrupt is ok -> delete currentlyReading logic
        - Check for the INA3221
        - (Optional) Compress PDO configuration selection into one single function
        - (Optional) Read assigned V and I_max to STPD01

*/
// 3 larghezza, 2 profondità e 2 altezza
volatile I2C_ReadState currentlyReading;

const int timeout = 10;         // In ms
const float VREF = 3300;        // In mV
const float VMAX = 4095;        // 2^12 - 1
const float V_25 = 760;         // Voltage at 25ºC (in mV)
const float AVG_SLOPE = 2.5f;   // mV/ºC

// Thermistors variables (for temperature zones 1-4 + onBoardTemperature)
const float R_PULLUP = 10000.0f;
const float R0 = 10000.0f;      // To check in thermistor's datasheet
const float T0 = 298.15;        // Temperature (expressed in K) at 25ºC
const float BETA = 3950.0;      // To check in thermistor's datasheet

// Critical Signals
int PD_IRQ = GPIO_PIN_RESET;
int PD_IRQ_PREV = GPIO_PIN_SET;           // For checking previous state of PD_IRQ after its update; Normal: IRQ is set to HIGH
int STPD01_INT = GPIO_PIN_SET;
int CYPD3175_INT = GPIO_PIN_SET;
int CHRG_OK = GPIO_PIN_SET;

// NON-Critical Signals
int USBA1_STATUS = GPIO_PIN_RESET;
int USBA2_STATUS = GPIO_PIN_RESET;
int EN_OTG_STATUS = GPIO_PIN_RESET;
int STPD01_EN_STATUS = GPIO_PIN_RESET;
int USBC2_STATUS = GPIO_PIN_RESET;

SensorData sensor_data;
FuelGaugeSensors fuelGaugeSensors;
PDContract primaryUSBC_Contract;
PDContract secondaryUSBC_PDContract;
STPD01_Status stpd01_status;
INA3221_Sensors ina3221_sensors;
// secondaryUSBC_PDContract.isSink = false;
HAL_StatusTypeDef status;

void init() {
    // Enable USB ports (USB-A1 and USB-A2) and enable Aux (LP_ST_EN - PC11)
    enable_USBA1();
    enable_USBA2();
    //HAL_GPIO_WritePin(USB_STPD01_EN_CTRL_GPIO_Port, USB_STPD01_EN_CTRL_Pin, GPIO_PIN_SET);

    // Secondary USB-C is always the source
    secondaryUSBC_PDContract.isSink = false;
    primaryUSBC_Contract.isNegotiationDone = false;
    secondaryUSBC_PDContract.isNegotiationDone = false;
}

float toVoltage(uint32_t raw) {
    return ((float)raw / VMAX) * VREF;
}

float toResistance(float vout) {        // For calculating R_NTC
    if (vout <= 0.1f) return 1e9f;       // Avoid divide by zero
    if (vout >= 3299.0f)  return 1.0f;       // Saturated high

    return R_PULLUP * (vout / (VREF - vout));
}

float toCelsius(float resistance) {       // To be checked. For T1, T2, T3, T4 sensors
    float tempK = 1.0f / ( (1.0f / T0) + (1.0f / BETA) * logf(resistance / R0) );
    return tempK - 273.15f;
}

void readSensors() {
    uint32_t raw;
    float voltage;

    // Start the ADC peripheral
    HAL_ADC_Start(&hadc1);

        // --- Temperature Zone 1 Reading ---
    HAL_ADC_PollForConversion(&hadc1, timeout);
    raw = HAL_ADC_GetValue(&hadc1);
    voltage = toVoltage(raw);
    sensor_data.tempZone[0] = toCelsius(toResistance(voltage));

        // --- Temperature Zone 2 Reading ---
    HAL_ADC_PollForConversion(&hadc1, timeout);
    raw = HAL_ADC_GetValue(&hadc1);
    voltage = toVoltage(raw);
    sensor_data.tempZone[1] = toCelsius(toResistance(voltage));

        // --- Temperature Zone 3 Reading ---
    HAL_ADC_PollForConversion(&hadc1, timeout);
    raw = HAL_ADC_GetValue(&hadc1);
    voltage = toVoltage(raw);
    sensor_data.tempZone[2] = toCelsius(toResistance(voltage));

        // --- Temperature Zone 4 Reading ---
    HAL_ADC_PollForConversion(&hadc1, timeout);
    raw = HAL_ADC_GetValue(&hadc1);
    voltage = toVoltage(raw);
    sensor_data.tempZone[3] = toCelsius(toResistance(voltage));

        // --- On Board Temperature Reading ---
    HAL_ADC_PollForConversion(&hadc1, timeout);
    raw = HAL_ADC_GetValue(&hadc1);
    voltage = toVoltage(raw);
    sensor_data.onBoardTemperature = toCelsius(toResistance(voltage));
    //sensor_data.onBoardTemperature = (V_25 - sensor_data.onBoardTemperature) / AVG_SLOPE + 25.0f;

        // --- USB-C Input Current ---
    HAL_ADC_PollForConversion(&hadc1, timeout);
    raw = HAL_ADC_GetValue(&hadc1);
    sensor_data.usbCInputCurrent = toVoltage(raw) / 0.2f;       // Result in mA

        // --- Battery Charge/Discahrge Current ---
    HAL_ADC_PollForConversion(&hadc1, timeout);
    raw = HAL_ADC_GetValue(&hadc1);
    // Not added since it is also read via I2C. Need to read it anyway to continue scan sequence

        // --- Total System Power ---
    HAL_ADC_PollForConversion(&hadc1, timeout);
    raw = HAL_ADC_GetValue(&hadc1);
    sensor_data.power_sys_W = (toVoltage(raw) / 1000.0f) * 60.0f;

    // Stop the ADC peripheral
    HAL_ADC_Stop(&hadc1);

    // --- FUEL GAUGE SENSORS ---
    uint8_t buffer[2];      // Buffer of 2 for storing both MSB and LSB if necessary (each one in a 8-bit register, so, for example, using size = 2 I'll read both 0x2a and 0x2B)
    uint16_t rawI2C;
    currentlyReading = READ_INTERNAL_TEMPERATURE;       // (0x2A/0x2B)
    HAL_I2C_Mem_Read(&hi2c1, FUEL_GAUGE_ADDR, 0x2A, I2C_MEMADD_SIZE_8BIT, buffer, 2, HAL_MAX_DELAY);
    // T is received in units of 0.1 K
    rawI2C = (uint16_t)((buffer[1] << 8) | buffer[0]);
    fuelGaugeSensors.internalTemperature = (rawI2C * 0.1f) - 273.15f;      // Converting to ºC

    currentlyReading = READ_EXTERNAL_TEMPERATURE;       // (0x0C/0x0D)
    HAL_I2C_Mem_Read(&hi2c1, FUEL_GAUGE_ADDR, 0x0C, I2C_MEMADD_SIZE_8BIT, buffer, 2, HAL_MAX_DELAY);
    rawI2C = (uint16_t)((buffer[1] << 8) | buffer[0]);
    fuelGaugeSensors.externalTemperature = (rawI2C * 0.1f) - 273.15f;      // Converting to ºC

    currentlyReading = READ_VOLTAGE_SCALE;      // (0x20)
    HAL_I2C_Mem_Read(&hi2c1, FUEL_GAUGE_ADDR, 0x20, I2C_MEMADD_SIZE_8BIT, buffer, 1, HAL_MAX_DELAY);
    fuelGaugeSensors.voltageScale = buffer[0];

    currentlyReading = READ_VOLTAGE;            // (0x08/0x09)
    HAL_I2C_Mem_Read(&hi2c1, FUEL_GAUGE_ADDR, 0x08, I2C_MEMADD_SIZE_8BIT, buffer, 2, HAL_MAX_DELAY);
    rawI2C = (uint16_t)((buffer[1] << 8) | buffer[0]);
    if (fuelGaugeSensors.voltageScale > 1) {
        fuelGaugeSensors.voltage = rawI2C * fuelGaugeSensors.voltageScale;
    } else {
        fuelGaugeSensors.voltage = rawI2C;
    }

    currentlyReading = READ_CURRENT_SCALE;      // (0x21)
    HAL_I2C_Mem_Read(&hi2c1, FUEL_GAUGE_ADDR, 0x21, I2C_MEMADD_SIZE_8BIT, buffer, 1, HAL_MAX_DELAY);
    fuelGaugeSensors.currentScale = buffer[0];

    currentlyReading = READ_CURRENT;            // (0x10/0x11)
    HAL_I2C_Mem_Read(&hi2c1, FUEL_GAUGE_ADDR, 0x10, I2C_MEMADD_SIZE_8BIT, buffer, 2, HAL_MAX_DELAY);
    rawI2C = (uint16_t)((buffer[1] << 8) | buffer[0]);
    if (fuelGaugeSensors.currentScale > 1) {
        fuelGaugeSensors.current = rawI2C * fuelGaugeSensors.currentScale;
    } else {
        fuelGaugeSensors.current = rawI2C;
    }

    currentlyReading = READ_AVG_CURRENT;        // (0x0A/0x0B)
    HAL_I2C_Mem_Read(&hi2c1, FUEL_GAUGE_ADDR, 0x0A, I2C_MEMADD_SIZE_8BIT, buffer, 2, HAL_MAX_DELAY);
    rawI2C = (uint16_t)((buffer[1] << 8) | buffer[0]);
    if (fuelGaugeSensors.currentScale > 1) {
        fuelGaugeSensors.avgCurrent = rawI2C * fuelGaugeSensors.currentScale;
    } else {
        fuelGaugeSensors.avgCurrent = rawI2C;
    }

    currentlyReading = READ_SOC;                // (0x02)
    HAL_I2C_Mem_Read(&hi2c1, FUEL_GAUGE_ADDR, 0x02, I2C_MEMADD_SIZE_8BIT, buffer, 1, HAL_MAX_DELAY);
    fuelGaugeSensors.SoC = buffer[0];

    static float prevSoC = 100.0f;
    if (fuelGaugeSensors.SoC < SOC_LOWV_THRESHOLD && prevSoC >= SOC_LOWV_THRESHOLD)
        event_push(EVT_SOC_LOWV);
    else if (fuelGaugeSensors.SoC < SOC_SAFETY_THRESHOLD && prevSoC >= SOC_SAFETY_THRESHOLD)
        event_push(EVT_SOC_SAFETY);
    if (fuelGaugeSensors.SoC >= SOC_OK_THRESHOLD && prevSoC < SOC_OK_THRESHOLD)
        event_push(EVT_SOC_OK);
    prevSoC = fuelGaugeSensors.SoC;

    currentlyReading = READ_AVG_TIME_TO_EMPTY;  // (0x18/0x19)
    HAL_I2C_Mem_Read(&hi2c1, FUEL_GAUGE_ADDR, 0x18, I2C_MEMADD_SIZE_8BIT, buffer, 2, HAL_MAX_DELAY);
    fuelGaugeSensors.avgTimeToEmpty = (uint16_t)((buffer[1] << 8) | buffer[0]);

    currentlyReading = READ_AVG_TIME_TO_FULL;   // (0x1A/0x1B)
    HAL_I2C_Mem_Read(&hi2c1, FUEL_GAUGE_ADDR, 0x1A, I2C_MEMADD_SIZE_8BIT, buffer, 2, HAL_MAX_DELAY);
    fuelGaugeSensors.avgTimeToFull = (uint16_t)((buffer[1] << 8) | buffer[0]);

    currentlyReading = READ_CYCLE_COUNT;        // (0x2C/0x2D)
    HAL_I2C_Mem_Read(&hi2c1, FUEL_GAUGE_ADDR, 0x2C, I2C_MEMADD_SIZE_8BIT, buffer, 2, HAL_MAX_DELAY);
    fuelGaugeSensors.cycleCount = (uint16_t)((buffer[1] << 8) | buffer[0]);

    currentlyReading = READ_STATE_OF_HEALTH;    // (0x2E/0x2F)
    HAL_I2C_Mem_Read(&hi2c1, FUEL_GAUGE_ADDR, 0x2E, I2C_MEMADD_SIZE_8BIT, buffer, 2, HAL_MAX_DELAY);
    fuelGaugeSensors.stateOfHealth = (uint16_t)((buffer[1] << 8) | buffer[0]);

    currentlyReading = READ_FLAGS;              // (0x0E/0x0F)
    HAL_I2C_Mem_Read(&hi2c1, FUEL_GAUGE_ADDR, 0x0E, I2C_MEMADD_SIZE_8BIT, buffer, 2, HAL_MAX_DELAY);
    // Little-endian order -> buffer[0] contains Low-Byte; buffer[1] contains High-Byte
    uint8_t rawLow = buffer[0];
    uint8_t rawHigh = buffer[1];

    // High-Byte
    fuelGaugeSensors.flags.OTC = (rawHigh >> 7) & 0x01;     // Shift to get bit 7 in position 0 and then mask every other bit
    fuelGaugeSensors.flags.OTD = (rawHigh >> 6) & 0x01;
    fuelGaugeSensors.flags.BATHI = (rawHigh >> 5) & 0x01;
    fuelGaugeSensors.flags.BATLOW = (rawHigh >> 4) & 0x01;
    fuelGaugeSensors.flags.CHG_INH = (rawHigh >> 3) & 0x01;
    fuelGaugeSensors.flags.XCHG = (rawHigh >> 2) & 0x01;
    fuelGaugeSensors.flags.FC = (rawHigh >> 1) & 0x01;
    fuelGaugeSensors.flags.CHG = (rawHigh >> 0) & 0x01;

    // Low-Byte
    fuelGaugeSensors.flags.DSG = (rawLow >> 0) & 0x01;

    // FSM fault events — edge-detected to avoid queue saturation
    static bool prevOT     = false;
    static bool prevBATHI  = false;
    static bool prevBATLOW = false;

    bool ot = fuelGaugeSensors.flags.OTC || fuelGaugeSensors.flags.OTD;
    if (ot && !prevOT)     event_push(EVT_FAULT_OT);
    prevOT = ot;

    if (fuelGaugeSensors.flags.BATHI && !prevBATHI)   event_push(EVT_SOC_OVCH);
    prevBATHI = fuelGaugeSensors.flags.BATHI;

    bool underv = fuelGaugeSensors.flags.BATLOW || (fuelGaugeSensors.voltage < UNDERV_VOLTAGE_MV);
    if (underv && !prevBATLOW) event_push(EVT_SOC_UNDERV);
    prevBATLOW = underv;

    // Display-only status flags — no FSM event
    if (fuelGaugeSensors.flags.CHG_INH) {
        // charging inhibited (display layer: show blocked indicator)
    }
    if (fuelGaugeSensors.flags.XCHG) {
        // charging not allowed (display layer: show blocked indicator)
    }
    if (fuelGaugeSensors.flags.FC) {
        // full charge detected (display layer)
    }
    if (fuelGaugeSensors.flags.CHG) {
        // fast charging active (display layer)
    }
    if (fuelGaugeSensors.flags.DSG) {
        // discharging active (display layer)
    }
}

float voltageToCurrentChannel(const uint8_t buffer[2]) {
    int16_t raw;
    raw = (int16_t)((buffer[0] << 8) | buffer[1]);      // MSB first
    int16_t counts = raw >> 3;                          // Arithmetic shift since raw is signed
    float shuntVoltage_mV = counts * 0.04f;             // 40 µV = 0.04 mV per count
    return shuntVoltage_mV / 0.01f;                     // 10 mΩ shunt; mV / mΩ = mA,
}

void readINA() {
    uint8_t buffer[2];
    // Read shunt voltage and calculate currents for the 3 channels
    HAL_I2C_Mem_Read(&hi2c1, INA3221_ADDR, SHUNT_VOLTAGE_CH1_REG_ADDR, I2C_MEMADD_SIZE_8BIT, buffer, 2, HAL_MAX_DELAY);
    ina3221_sensors.current_channel1 = voltageToCurrentChannel(buffer);
    HAL_I2C_Mem_Read(&hi2c1, INA3221_ADDR, SHUNT_VOLTAGE_CH2_REG_ADDR, I2C_MEMADD_SIZE_8BIT, buffer, 2, HAL_MAX_DELAY);
    ina3221_sensors.current_channel2 = voltageToCurrentChannel(buffer);
    HAL_I2C_Mem_Read(&hi2c1, INA3221_ADDR, SHUNT_VOLTAGE_CH3_REG_ADDR, I2C_MEMADD_SIZE_8BIT, buffer, 2, HAL_MAX_DELAY);
    ina3221_sensors.current_channel3 = voltageToCurrentChannel(buffer);

    // Read critical alerts flag indicators for the 3 channels
    HAL_I2C_Mem_Read(&hi2c1, INA3221_ADDR, MASK_ENABLE_REG_ADDR, I2C_MEMADD_SIZE_8BIT, buffer, 2, HAL_MAX_DELAY);
    uint16_t maskEnable = (uint16_t)((buffer[0] << 8) | buffer[1]);

    // Critical-alert flag indicators Channels 1-3: bits 9-7 respectively
    ina3221_sensors.critical_alert_channel1 = (maskEnable >> 9) & 0x01;     // Channel 1 critical alert flag
    ina3221_sensors.critical_alert_channel2 = (maskEnable >> 8) & 0x01;     // Channel 2 critical alert flag
    ina3221_sensors.critical_alert_channel3 = (maskEnable >> 7) & 0x01;     // Channel 3 critical alert flag

    static bool prevOCC1 = false;
    static bool prevOCC2 = false;

    if (ina3221_sensors.critical_alert_channel1 && !prevOCC1) {
        disable_USBA1();
        event_push(EVT_ERROR);
    }
    prevOCC1 = ina3221_sensors.critical_alert_channel1;

    if (ina3221_sensors.critical_alert_channel2 && !prevOCC2) {
        disable_USBA2();
        event_push(EVT_ERROR);
    }
    prevOCC2 = ina3221_sensors.critical_alert_channel2;
    // channel3 tied to GND — unused
}

/*
void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c) {
    //if (hi2c == &I2C_LP)
    switch (currentlyReading) {
        case READ_INTERNAL_TEMPERATURE: {       // T is received in units of 0.1 K
            uint16_t raw = (uint16_t)((buffer[1] << 8) | buffer[0]);
            fuelGaugeSensors.internalTemperature = (raw * 0.1f) - 273.15f;      // Converting to ºC
            break;
        }
        case READ_EXTERNAL_TEMPERATURE: {
            uint16_t raw = (uint16_t)((buffer[1] << 8) | buffer[0]);
            fuelGaugeSensors.externalTemperature = (raw * 0.1f) - 273.15f;      // Converting to ºC
            break;
        }
        case READ_VOLTAGE_SCALE: {
            fuelGaugeSensors.voltageScale = buffer[0];
            break;
        }
        case READ_VOLTAGE: {
            uint16_t raw = (uint16_t)((buffer[1] << 8) | buffer[0]);
            if (fuelGaugeSensors.voltageScale > 1) {
                fuelGaugeSensors.voltage = raw * fuelGaugeSensors.voltageScale;
            } else {
                fuelGaugeSensors.voltage = raw;
            }
            break;
        }
        case READ_CURRENT_SCALE: {
            fuelGaugeSensors.currentScale = buffer[0];
            break;
        }
        case READ_CURRENT: {
            uint16_t raw = (uint16_t)((buffer[1] << 8) | buffer[0]);
            if (fuelGaugeSensors.currentScale > 1) {
                fuelGaugeSensors.current = raw * fuelGaugeSensors.currentScale;
            } else {
                fuelGaugeSensors.current = raw;
            }
            break;
        }
        case READ_AVG_CURRENT: {
            uint16_t raw = (uint16_t)((buffer[1] << 8) | buffer[0]);
            if (fuelGaugeSensors.currentScale > 1) {
                fuelGaugeSensors.avgCurrent = raw * fuelGaugeSensors.currentScale;
            } else {
                fuelGaugeSensors.avgCurrent = raw;
            }
            break;
        }
        case READ_SOC: {
            fuelGaugeSensors.SoC = buffer[0];
            break;
        }
        case READ_AVG_TIME_TO_EMPTY: {
            fuelGaugeSensors.avgTimeToEmpty = (uint16_t)((buffer[1] << 8) | buffer[0]);
            break;
        }
        case READ_AVG_TIME_TO_FULL: {
            fuelGaugeSensors.avgTimeToFull = (uint16_t)((buffer[1] << 8) | buffer[0]);
            break;
        }
        case READ_CYCLE_COUNT: {
            fuelGaugeSensors.cycleCount = (uint16_t)((buffer[1] << 8) | buffer[0]);
            break;
        }
        case READ_STATE_OF_HEALTH: {
            fuelGaugeSensors.stateOfHealth = (uint16_t)((buffer[1] << 8) | buffer[0]);
            break;
        }
        case READ_FLAGS: {
            // Little-endian order -> buffer[0] contains Low-Byte; buffer[1] contains High-Byte
            uint8_t rawLow = buffer[0];
            uint8_t rawHigh = buffer[1];
            // High-Byte
            fuelGaugeSensors.flags.OTC = (rawHigh >> 7) & 0x01;     // Shift to get bit 7 in position 0 and then mask every other bit
            fuelGaugeSensors.flags.OTD = (rawHigh >> 6) & 0x01;
            fuelGaugeSensors.flags.BATHI = (rawHigh >> 5) & 0x01;
            fuelGaugeSensors.flags.BATLOW = (rawHigh >> 4) & 0x01;
            fuelGaugeSensors.flags.CHG_INH = (rawHigh >> 3) & 0x01;
            fuelGaugeSensors.flags.XCHG = (rawHigh >> 2) & 0x01;
            fuelGaugeSensors.flags.FC = (rawHigh >> 1) & 0x01;
            fuelGaugeSensors.flags.CHG = (rawHigh >> 0) & 0x01;

            // Low-Byte
            fuelGaugeSensors.flags.DSG = (rawLow >> 0) & 0x01;
            break;
        }
    }
}
*/

void readCS() {        // Check and update critical signals and their status (e.g., CHRG_OK)
    // Read HP.PD_IRQ (PB14) and update PD_IRQ status
    //PD_IRQ_PREV = PD_IRQ;   // Remember PD_IRQ status before updating it
    PD_IRQ = HAL_GPIO_ReadPin(USB_IRQ_CTRL_GPIO_Port, USB_IRQ_CTRL_Pin);

    // Read STPD01_INT (PC12): STPD01 interrupt — converter power state change
    STPD01_INT = HAL_GPIO_ReadPin(STPD01_INT_GPIO_Port, STPD01_INT_Pin);

    // Read CYPD3175_INT (PC3): CYPD3175 interrupt — PD protocol event
    CYPD3175_INT = HAL_GPIO_ReadPin(CYPD3175_INT_GPIO_Port, CYPD3175_INT_Pin);

    // Read HP.CHRG_OK (PB13) and update CHRG_OK status
    CHRG_OK = HAL_GPIO_ReadPin(USB_CHRG_OK_CTRL_GPIO_Port, USB_CHRG_OK_CTRL_Pin);

    static bool prevCHRG_OK = true;
    bool chrgOkNow = (CHRG_OK == GPIO_PIN_SET);
    if (!chrgOkNow && prevCHRG_OK) event_push(EVT_ERROR);
    prevCHRG_OK = chrgOkNow;

    if (PD_IRQ == GPIO_PIN_RESET) {
        primaryUSBC_ConnectionINT();
    }

    if (STPD01_INT == GPIO_PIN_RESET) {
        stpd01_PowerStateINT();
    }

    if (CYPD3175_INT == GPIO_PIN_RESET) {
        secondaryUSBC_ConnectionINT();
    }
}

void readNCS() {        // Check and update NON-critical signals and their status (e.g., USBA1_STATUS)
    USBA1_STATUS = HAL_GPIO_ReadPin(USB_A1_CTRL_GPIO_Port, USB_A1_CTRL_Pin);
    USBA2_STATUS = HAL_GPIO_ReadPin(USB_A2_CTRL_GPIO_Port, USB_A2_CTRL_Pin);
    USBC2_STATUS = HAL_GPIO_ReadPin(USB_C2_EN_GPIO_Port, USB_C2_EN_Pin);
    EN_OTG_STATUS = HAL_GPIO_ReadPin(USB_OTG_CTRL_GPIO_Port, USB_OTG_CTRL_Pin);
    STPD01_EN_STATUS = HAL_GPIO_ReadPin(USB_STPD01_EN_CTRL_GPIO_Port, USB_STPD01_EN_CTRL_Pin);
}

/*
    INT_EVENT1: Interrupt event bit field for I2Cs_IRQ. If any bit in this register is 1, then the I2Cs_IRQ pin is pulled low
    INT_CLEAR1: Interrupt clear bit field for INT_EVENT1. Bits set in this register are cleared from INT_EVENT1
    Note: PDO = Power Data Object; RDO = Request Data Object
    // First PDO is always fixed at 5V
*/

void primaryUSBC_ConnectionINT() {
    // Read INT_EVENT1 (0x14) [Little-endian]
    uint8_t eventBuffer[11];
    HAL_I2C_Mem_Read(&hi2c3, TPS25750_PD_CONTROLLER_ADDR, INT_EVENT1_REG_ADDR, I2C_MEMADD_SIZE_8BIT, eventBuffer, 11, HAL_MAX_DELAY);
    // Read from INT_EVENT1
    bool plugInsertOrRemoval = (eventBuffer[0] >> 3) & 0x01;    // USB Plug Status has Changed
    bool newContractAsCons = (eventBuffer[1] >> 4) & 0x01;      // Far-end source has accepted an RDO sent by the PD Controller as a Sink
    bool newContractAsProv = (eventBuffer[1] >> 5) & 0x01;      // An RDO from the far-end device has been accepted and the PD Controller is a Source
    bool usbHostPresent = (eventBuffer[2] >> 4) & 0x01;         // Set when STATUS.UsbHostPresent transitions to 11b
    bool usbHostPresentNoLonger = (eventBuffer[2] >> 5) & 0x01;          // Set when STATUS.UsbHostPresent transitions to anything other than 11b
    bool powerStatusUpdate = (eventBuffer[3] >> 0) & 0x01;      // Set if Power Status changes

    // Read from PowerStatus
    bool powerConnection;       // Asserted if there is a connection
    bool sourceSink;            // Source / Sink indicator
    uint8_t typeCCurrent;       // If the port is connected as source, the field is updated upon initial connection only (useful for sink mode only)
    // For having more details about newContractAsCons and newContractAsProv, see ACTIVE_CONTRACT_PDO register (0x34) and ACTIVE_CONTRACT_RDO register (0x35)

    if (powerStatusUpdate) {
        uint8_t powerStatusBuffer[2];
        // Read POWER_STATUS Register
        HAL_I2C_Mem_Read(&hi2c3, TPS25750_PD_CONTROLLER_ADDR, POWER_STATUS_REG_ADDR, I2C_MEMADD_SIZE_8BIT, powerStatusBuffer, 2, HAL_MAX_DELAY);
        powerConnection = (powerStatusBuffer[0] >> 0) & 0x01;
        sourceSink = (powerStatusBuffer[0] >> 1) & 0x01;
        typeCCurrent = (powerStatusBuffer[0] >> 2) & 0x03;
    }

    // Check the AND gate in the if below
    if (plugInsertOrRemoval && powerStatusUpdate) {
        primaryUSBC_Contract.isPlugged = powerConnection;
        if (primaryUSBC_Contract.isPlugged) {
            // Determine whether the powerbank is the source or the sink
            primaryUSBC_Contract.isSink = sourceSink;        // 1 -> is sink; 0 -> is source
            if (sourceSink) event_push(EVT_CHARGER_CONNECTED);
        } else {        // if not plugged
            primaryUSBC_Contract.isNegotiationDone = false;
            event_push(EVT_CHARGER_DISCONNECTED);
        }
    }

    if (primaryUSBC_Contract.isPlugged && (newContractAsCons || newContractAsProv)) {
        primaryUSBC_Contract.isNegotiationDone = true;
        // Read incoming contract
        uint8_t activeContractPDOBuffer[6];
        uint8_t activeContractRDOBuffer[4];

        // Check PDO type is compatible, otherwise throw a warning and ignore it
        HAL_I2C_Mem_Read(&hi2c3, TPS25750_PD_CONTROLLER_ADDR, ACTIVE_CONTRACT_PDO_REG_ADDR, I2C_MEMADD_SIZE_8BIT, activeContractPDOBuffer, 6, HAL_MAX_DELAY);
        uint8_t pdoType = (activeContractPDOBuffer[3] >> 6) & 0x03;     // Bits 31:30
        if (pdoType != 0x00) {
            event_push(EVT_ERROR);
        } else {
            // Read PDO for fetching Voltage a maximum Current
            uint16_t voltageRaw = ((activeContractPDOBuffer[2] & 0x0F) << 6) | ((activeContractPDOBuffer[1] >> 2) & 0x3F);  // (19:10)
            uint16_t maxCurrentRaw = ((activeContractPDOBuffer[1] & 0x03) << 8) | activeContractPDOBuffer[0];       // (9:0)

            // Read RDO for fetching negotiated current
            HAL_I2C_Mem_Read(&hi2c3, TPS25750_PD_CONTROLLER_ADDR, ACTIVE_CONTRACT_RDO_REG_ADDR, I2C_MEMADD_SIZE_8BIT, activeContractRDOBuffer, 4, HAL_MAX_DELAY);
            uint16_t operatingCurrentRaw = ((activeContractRDOBuffer[2] & 0x0F) << 6) | ((activeContractRDOBuffer[1] >> 2) & 0x03F);        // (19:10)

            // Convert fetched values accordingly
            primaryUSBC_Contract.voltage = voltageRaw * 50.0f;                    // To get mV (as stated in the USB_PD standard)
            primaryUSBC_Contract.maxCurrent = maxCurrentRaw * 10.0f;              // To get mA (as stated in the USB_PD standard)
            primaryUSBC_Contract.operatingCurrent = operatingCurrentRaw * 10.0f;  // To get mA (as stated in the USB_PD standard)

        }
    }

    // Clear INT_EVENT1 to make I2Cs_IRQ back to HIGH (only bits previously read are cleared to be able to get possibly new occurring interrupts)
    HAL_I2C_Mem_Write(&hi2c3, TPS25750_PD_CONTROLLER_ADDR, INT_CLEAR1_REG_ADDR, I2C_MEMADD_SIZE_8BIT, eventBuffer, 11, HAL_MAX_DELAY);
}

// Shared fault/disconnect teardown for the secondary USB-C port.
// clearPlugged is false only for Hard Reset, where the device is still
// physically present and the CYPD3175 re-advertises automatically.
static void secondaryUSBC_Shutdown(bool clearPlugged) {
    disable_USBC2();
    disable_STPD01();
    secondaryUSBC_PDContract.isNegotiationDone = false;
    if (clearPlugged) {
        secondaryUSBC_PDContract.isPlugged = false;
    }
}

void secondaryUSBC_ConnectionINT() {
    uint8_t intrReg;
    uint8_t eventCode;
    uint8_t pdoBuf[4];
    uint8_t rdoBuf[4];
    uint8_t clearBit;
    uint16_t voltageRaw, maxCurrentRaw, opCurrentRaw;

    if (HAL_I2C_Mem_Read(&hi2c1, CYPD3175_PD_CONTROLLER_ADDR,
                          CYPD3175_INTR_REG, I2C_MEMADD_SIZE_16BIT,
                          &intrReg, 1, HAL_MAX_DELAY) != HAL_OK) {
        event_push(EVT_ERROR);
        return;
    }

    if (!(intrReg & CYPD3175_INTR_PORT0_BIT)) {
        // No port-0 event pending. Still write back exactly the bits we read:
        // INTR_REG is write-1-to-clear, so this acks only the bit(s) observed
        // set here (e.g. DEV_INTR) without touching PORT0_INTR. We don't
        // decode device-level events, but we must ack them or the interrupt
        // line (CYPD3175_INT) stays asserted and this handler is re-entered
        // every readCS() cycle forever.
        HAL_I2C_Mem_Write(&hi2c1, CYPD3175_PD_CONTROLLER_ADDR,
                           CYPD3175_INTR_REG, I2C_MEMADD_SIZE_16BIT,
                           &intrReg, 1, HAL_MAX_DELAY);
        return;
    }

    if (HAL_I2C_Mem_Read(&hi2c1, CYPD3175_PD_CONTROLLER_ADDR,
                          CYPD3175_RESPONSE_REG, I2C_MEMADD_SIZE_16BIT,
                          &eventCode, 1, HAL_MAX_DELAY) != HAL_OK) {
        event_push(EVT_ERROR);
        return;
    }

    clearBit = CYPD3175_INTR_PORT0_BIT;
    HAL_I2C_Mem_Write(&hi2c1, CYPD3175_PD_CONTROLLER_ADDR,
                       CYPD3175_INTR_REG, I2C_MEMADD_SIZE_16BIT,
                       &clearBit, 1, HAL_MAX_DELAY);

    if (eventCode == CYPD3175_EVT_OVP || eventCode == CYPD3175_EVT_OCP) {
        secondaryUSBC_Shutdown(true);
        event_push(EVT_FAULT_CRITICAL);
    } else if (eventCode == CYPD3175_EVT_OTP) {
        secondaryUSBC_Shutdown(true);
        event_push(EVT_FAULT_OT);
    } else if (eventCode == CYPD3175_EVT_HARD_RESET) {
        secondaryUSBC_Shutdown(false);
    } else if (eventCode == CYPD3175_EVT_DISCONNECT) {
        secondaryUSBC_Shutdown(true);
    } else if (eventCode == CYPD3175_EVT_CONTRACT_COMPLETE) {
        secondaryUSBC_PDContract.isPlugged = true;

        if (HAL_I2C_Mem_Read(&hi2c1, CYPD3175_PD_CONTROLLER_ADDR,
                              CYPD3175_PORT0_CURRENT_PDO, I2C_MEMADD_SIZE_16BIT,
                              pdoBuf, 4, HAL_MAX_DELAY) != HAL_OK) {
            event_push(EVT_ERROR);
            return;
        }
        if (HAL_I2C_Mem_Read(&hi2c1, CYPD3175_PD_CONTROLLER_ADDR,
                              CYPD3175_PORT0_CURRENT_RDO, I2C_MEMADD_SIZE_16BIT,
                              rdoBuf, 4, HAL_MAX_DELAY) != HAL_OK) {
            event_push(EVT_ERROR);
            return;
        }

        // Fixed PDO decode: voltage [19:10] × 50 mV, max current [9:0] × 10 mA
        voltageRaw    = ((pdoBuf[2] & 0x0F) << 6) | ((pdoBuf[1] >> 2) & 0x3F);
        maxCurrentRaw = ((pdoBuf[1] & 0x03) << 8) | pdoBuf[0];
        // RDO decode: operating current [19:10] × 10 mA
        opCurrentRaw  = ((rdoBuf[2] & 0x0F) << 6) | ((rdoBuf[1] >> 2) & 0x3F);

        if (voltageRaw == 0 || maxCurrentRaw == 0) {
            // PDO/RDO not populated yet (premature/spurious event before the
            // CYPD3175 finished writing the contract) — wait for the next event
            // instead of configuring STPD01 with a fabricated 0V/0A contract.
            // No known-good HPI attach-type register bit map is available in
            // this repo (CYPD3175_PORT0_TYPE_C_STATUS is defined but its bit
            // layout isn't documented here) — this zero-check is the only
            // guard against a non-PD/garbage "contract" reaching setupSTPD01().
            return;
        }

        secondaryUSBC_PDContract.voltage          = voltageRaw    * 50.0f;
        secondaryUSBC_PDContract.maxCurrent       = maxCurrentRaw * 10.0f;
        secondaryUSBC_PDContract.operatingCurrent = opCurrentRaw  * 10.0f;

        if (!setupSTPD01(secondaryUSBC_PDContract.voltage,
                         secondaryUSBC_PDContract.maxCurrent)) {
            event_push(EVT_ERROR);
            return;
        }
        enable_STPD01();
        HAL_Delay(10);

        if (checkSTPD01() && stpd01_status.powerOn) {
            enable_USBC2();
            secondaryUSBC_PDContract.isNegotiationDone = true;
        } else {
            disable_STPD01();
            if (stpd01_status.shortCircuitProtection  ||
                stpd01_status.overVoltageProtection   ||
                stpd01_status.inductorPeakCurrentProtection) {
                event_push(EVT_FAULT_CRITICAL);
            } else if (stpd01_status.overTemperatureProtection) {
                event_push(EVT_FAULT_OT);
            } else {
                // I2C failure, or no fault bit set but the converter never
                // reached power-on — either way, charging didn't start.
                event_push(EVT_ERROR);
            }
        }
    } else {
        // Unrecognized HPI event code
        event_push(EVT_ERROR);
    }
}

bool setupSTPD01(float voltage_mV, float current_mA) {
    // First, ensure STPD01 is disabled
    disable_STPD01();
    // --- Convert max voltage and max current into the correspontent addresses
    // Clamp values to STPD operational range
    if (voltage_mV < 5000) {
        voltage_mV = 5000;
    } else if (voltage_mV > 20000) {
        voltage_mV = 20000;
    }

    if (current_mA < 100) {
        current_mA = 100;
    } else if (current_mA > 3000) {
        current_mA = 3000;
    }

    int v = (int)voltage_mV;
    int c = (int)current_mA;

    uint8_t voltageReg;
    if (v < 5900) {
        voltageReg = (uint8_t)((v - 3000) / 20);          // 0x00 base, 20mV steps
    } else if (v < 11000) {
        voltageReg = 0x91 + (uint8_t)((v - 5900) / 100);  // 0x91 base, 100mV steps
    } else {
        voltageReg = 0xC4 + (uint8_t)((v - 11000) / 200); // 0xC4 base, 200mV steps
    }

    uint8_t currentReg = (uint8_t)((c - 100) / 100);      // 0x00 = 100mA, steps of 100mA

    // --- Provide max voltage and max current addresses to STPD01
    status = HAL_I2C_Mem_Write(&hi2c1, STPD01_PD_ADDR, VOUT_REG_ADDR, I2C_MEMADD_SIZE_8BIT, &voltageReg, 1, HAL_MAX_DELAY);
    if (status != HAL_OK) return false;

    status = HAL_I2C_Mem_Write(&hi2c1, STPD01_PD_ADDR, ILIM_REG_ADDR, I2C_MEMADD_SIZE_8BIT, &currentReg, 1, HAL_MAX_DELAY);
    if (status != HAL_OK) return false;
    return true;
}

bool checkSTPD01() {
    uint8_t buffer;
    if (HAL_I2C_Mem_Read(&hi2c1, STPD01_PD_ADDR, INT_STAT_REG_ADDR,
                          I2C_MEMADD_SIZE_8BIT, &buffer, 1, HAL_MAX_DELAY) != HAL_OK) {
        // I2C failure: status is unknown, not "no fault" — clear stale data so
        // callers can't mistake a previous good read for a fresh confirmation.
        stpd01_status = (STPD01_Status){0};
        return false;
    }
    stpd01_status.overVoltageProtection         = (buffer >> 0) & 0x01;
    stpd01_status.shortCircuitProtection        = (buffer >> 2) & 0x01;
    stpd01_status.powerOn                       = (buffer >> 3) & 0x01;
    stpd01_status.overTemperatureProtection     = (buffer >> 5) & 0x01;
    stpd01_status.overTemperatureWarning        = (buffer >> 6) & 0x01;
    stpd01_status.inductorPeakCurrentProtection = (buffer >> 7) & 0x01;
    if (stpd01_status.overVoltageProtection         ||
        stpd01_status.shortCircuitProtection        ||
        stpd01_status.overTemperatureProtection     ||
        stpd01_status.inductorPeakCurrentProtection) {
        return false;
    }
    return true;
}

void stpd01_PowerStateINT() {
    uint8_t buffer;
    if (HAL_I2C_Mem_Read(&hi2c1, STPD01_PD_ADDR, INT_STAT_REG_ADDR,
                          I2C_MEMADD_SIZE_8BIT, &buffer, 1, HAL_MAX_DELAY) != HAL_OK) {
        event_push(EVT_ERROR);
        return;
    }

    stpd01_status.overVoltageProtection         = (buffer >> 0) & 0x01;
    stpd01_status.shortCircuitProtection        = (buffer >> 2) & 0x01;
    stpd01_status.powerOn                       = (buffer >> 3) & 0x01;
    stpd01_status.overTemperatureProtection     = (buffer >> 5) & 0x01;
    stpd01_status.overTemperatureWarning        = (buffer >> 6) & 0x01;
    stpd01_status.inductorPeakCurrentProtection = (buffer >> 7) & 0x01;

    if (stpd01_status.overVoltageProtection     ||
        stpd01_status.shortCircuitProtection    ||
        stpd01_status.inductorPeakCurrentProtection) {
        disable_USBC2();
        disable_STPD01();
        secondaryUSBC_PDContract.isNegotiationDone = false;
        secondaryUSBC_PDContract.isPlugged = false;
        event_push(EVT_FAULT_CRITICAL);
    } else if (stpd01_status.overTemperatureProtection) {
        disable_USBC2();
        disable_STPD01();
        secondaryUSBC_PDContract.isNegotiationDone = false;
        secondaryUSBC_PDContract.isPlugged = false;
        event_push(EVT_FAULT_OT);
    }
    // OTW alone: no FSM event, display layer handles the warning
}

void enable_USBA1() {
    HAL_GPIO_WritePin(USB_A1_CTRL_GPIO_Port, USB_A1_CTRL_Pin, GPIO_PIN_SET);
    USBA1_STATUS = true;
}

void disable_USBA1() {
    HAL_GPIO_WritePin(USB_A1_CTRL_GPIO_Port, USB_A1_CTRL_Pin, GPIO_PIN_RESET);
    USBA1_STATUS = false;
}

void enable_USBA2() {
    HAL_GPIO_WritePin(USB_A2_CTRL_GPIO_Port, USB_A2_CTRL_Pin, GPIO_PIN_SET);
    USBA2_STATUS = true;
}

void disable_USBA2() {
    HAL_GPIO_WritePin(USB_A2_CTRL_GPIO_Port, USB_A2_CTRL_Pin, GPIO_PIN_RESET);
    USBA2_STATUS = false;
}

void enable_STPD01 () {
    HAL_GPIO_WritePin(USB_STPD01_EN_CTRL_GPIO_Port, USB_STPD01_EN_CTRL_Pin, GPIO_PIN_SET);
    STPD01_EN_STATUS = true;
}

void disable_STPD01 () {
    HAL_GPIO_WritePin(USB_STPD01_EN_CTRL_GPIO_Port, USB_STPD01_EN_CTRL_Pin, GPIO_PIN_RESET);
    STPD01_EN_STATUS = false;
}

void enable_USBC2() {
    HAL_GPIO_WritePin(USB_C2_EN_GPIO_Port, USB_C2_EN_Pin, GPIO_PIN_SET);
    USBC2_STATUS = true;
}

void disable_USBC2() {
    HAL_GPIO_WritePin(USB_C2_EN_GPIO_Port, USB_C2_EN_Pin, GPIO_PIN_RESET);
    USBC2_STATUS = false;
}

// Get data functions
SensorData getSensorData() {
    return sensor_data;
}

FuelGaugeSensors getFuelGaugeData() {
    return fuelGaugeSensors;
}

STPD01_Status getSTPD01_Status() {
    return stpd01_status;
}

INA3221_Sensors getINA3221_Sensors() {
    return ina3221_sensors;
}

PDContract getPrimaryUSBC_Contract() {
    return primaryUSBC_Contract;
}

PDContract getSecondaryUSBC_Contract() {
    return secondaryUSBC_PDContract;
}

int get_USBA1_Status() {
    return USBA1_STATUS;
}

int get_USBA2_Status() {
    return USBA2_STATUS;
}

int get_USBC2_Status() {
    return USBC2_STATUS;
}

int get_OTG_Status() {
    return EN_OTG_STATUS;
}

int get_STPD01_Enabled() {
    return STPD01_EN_STATUS;        // getSTPD01_Status() provides isPowerOn
}

