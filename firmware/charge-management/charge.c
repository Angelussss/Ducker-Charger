#include "charge.h"
#include "stm32f4xx_hal.h"
#include "i2c.h"

/*
Still to be implemented (for the sensor part):
    - Get value of the sensor of the HP.IBAT	PA7	IN7	Battery Current (Charge/Discharge)

*/

// To be configured during ADC Initialization
extern ADC_HandleTypeDef hadcTZ1;
extern ADC_HandleTypeDef hadcTZ2;
extern ADC_HandleTypeDef hadcTZ3;
extern ADC_HandleTypeDef hadcTZ4;
extern ADC_HandleTypeDef hadcUSBCIC;
extern ADC_HandleTypeDef hadcBC;
extern ADC_HandleTypeDef hadcTSP;

// I2C1 -> STUSB4710 STPD01PUR INA3221 BQ34Z100
// I2C2 -> TPS25750
volatile I2C_ReadState currentlyReading;
uint8_t buffer[2];      // Buffer of 2 for storing both MSB and LSB if necessary (each one in a 8-bit register, so, for example, using size = 2 I'll read both 0x2a and 0x2B)
int16_t rawI2C;         // Variable for Size = 1 cases

// (Interrupt solo per Fuel Gauge)
const int timeout = 10;         // In ms
const float VREF = 3300;        // In mV
const float VMAX = 4095;        // 2^12 - 1
const float V_25 = 760;         // Voltage at 25ºC (in mV)
const float AVG_SLOPE = 2.5f;   // mV/ºC

// Critical Signals
int PD_IRQ = GPIO_PIN_RESET;
int PD_IRQ_PREV = GPIO_PIN_SET;           // For checking previous state of PD_IRQ after its update; Normal: IRQ is set to HIGH
int ST_INT = GPIO_PIN_RESET;
int CHRG_OK = GPIO_PIN_RESET;

// NON-Critical Signals
int USBA1_STATUS = GPIO_PIN_RESET;
int USBA2_STATUS = GPIO_PIN_RESET;
int EN_OTG_STATUS = GPIO_PIN_RESET;
int ST_EN_STATUS = GPIO_PIN_RESET;



SensorData sensor_data;
FuelGaugeSensors fuelGaugeSensors;

/*  Read via ADC1; VREF+ = VDD (+3.3V)
    Signal Name     STM32 Pin	ADC Channel	Description
    ----------------------------------------------------------------------
    NTC_1	        PA0	        IN0	            Temp Zone 1 (General/FETs)
    ----------------------------------------------------------------------
    NTC_2	        PA1	        IN1	            Temp Zone 2
    ----------------------------------------------------------------------
    NTC_3	        PA2	        IN2	            Temp Zone 3
    ----------------------------------------------------------------------
    NTC_4	        PA3	        IN3	            Temp Zone 4
    ----------------------------------------------------------------------
    HP.IADPT	    PA6	        IN6	            USB-C Input Current
    ----------------------------------------------------------------------
    HP.IBAT	        PA7	        IN7	            Battery Current (Charge/Discharge)
    ----------------------------------------------------------------------
    HP.PSYS	        PC4	        IN14	        Total System Power
*/

/*  ADC:
*** Polling mode IO operation ***
     =================================
     [..]
       (+) Start the ADC peripheral using HAL_ADC_Start()
       (+) Wait for end of conversion using HAL_ADC_PollForConversion(), at this stage
           user can specify the value of timeout according to his end application
       (+) To read the ADC converted values, use the HAL_ADC_GetValue() function.
       (+) Stop the ADC peripheral using HAL_ADC_Stop()
*/

void init() {
    // Initialize INA3221 (I2C_LP) immediately to provide OCP (Over Current Protection) for USB-A ports
    // Enable USB ports (USB-A1 and USB-A2) and enable Aux (LP_ST_EN - PC11)
    HAL_GPIO_WritePin(USB_A1_CTRL_GPIO_Port, USB_A1_CTRL_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(USB_A2_CTRL_GPIO_Port, USB_A2_CTRL_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(USB_ST_EN_CTRL_GPIO_Port, USB_ST_EN_CTRL_Pin, GPIO_PIN_SET);
}

float toVoltage(uint32_t raw) {
    return ((float)raw / VMAX) * VREF;
}

float toCelsius(float temp) {
    return (V_25 - temp) / AVG_SLOPE + 25.0f;
}

void readSensors() {
    uint32_t raw;

        // --- Temperature Zone 1 Reading ---
    // Start the ADC peripheral
    HAL_ADC_Start(&hadcTZ1);

    // Read Temp Zone 1
    HAL_ADC_PollForConversion(&hadcTZ1, timeout);
    raw = HAL_ADC_GetValue(&hadcTZ1);
    sensor_data.tempZone[0] = toVoltage(raw);
    sensor_data.tempZone[0] = toCelsius(sensor_data.tempZone[0]);

    // Stop the ADC peripheral
    HAL_ADC_Stop(&hadcTZ1);

        // --- Temperature Zone 2 Reading ---
    HAL_ADC_Start(&hadcTZ2);
    HAL_ADC_PollForConversion(&hadcTZ2, timeout);
    raw = HAL_ADC_GetValue(&hadcTZ2);
    sensor_data.tempZone[1] = toVoltage(raw);
    sensor_data.tempZone[1] = toCelsius(sensor_data.tempZone[1]);
    HAL_ADC_Stop(&hadcTZ2);

        // --- Temperature Zone 3 Reading ---
    HAL_ADC_Start(&hadcTZ3);
    HAL_ADC_PollForConversion(&hadcTZ3, timeout);
    raw = HAL_ADC_GetValue(&hadcTZ3);
    sensor_data.tempZone[2] = toVoltage(raw);
    sensor_data.tempZone[2] = toCelsius(sensor_data.tempZone[2]);
    HAL_ADC_Stop(&hadcTZ3);

        // --- Temperature Zone 4 Reading ---
    HAL_ADC_Start(&hadcTZ4);
    HAL_ADC_PollForConversion(&hadcTZ4, timeout);
    raw = HAL_ADC_GetValue(&hadcTZ4);
    sensor_data.tempZone[3] = toVoltage(raw);
    sensor_data.tempZone[3] = toCelsius(sensor_data.tempZone[3]);
    HAL_ADC_Stop(&hadcTZ4);

        // --- USB-C Input Current ---
    HAL_ADC_Start(&hadcUSBCIC);
    HAL_ADC_PollForConversion(&hadcUSBCIC, timeout);
    raw = HAL_ADC_GetValue(&hadcUSBCIC);
    sensor_data.usbCInputCurrent = toVoltage(raw) / 200.0f;

    HAL_ADC_Stop(&hadcUSBCIC);

        // --- Battery Current (Charge/Discharge) ---
    HAL_ADC_Start(&hadcBC);
    HAL_ADC_PollForConversion(&hadcBC, timeout);
    raw = HAL_ADC_GetValue(&hadcBC);
    // vedi datasheet integrato
    HAL_ADC_Stop(&hadcBC);

        // --- Total System Power ---
    HAL_ADC_Start(&hadcTSP);
    HAL_ADC_PollForConversion(&hadcTSP, timeout);
    raw = HAL_ADC_GetValue(&hadcTSP);
    sensor_data.power_sys_W = (toVoltage(raw) / 1000.0f) * 60.0f;
    //batteryCurrent = ;       // I < 0: charging; I > 0: discharging
    HAL_ADC_Stop(&hadcTSP);

    // --- FUEL GAUGE SENSORS ---
    currentlyReading = READ_INTERNAL_TEMPERATURE;       // (0x2A/0x2B)
    HAL_I2C_Mem_Read_IT(&hi2c1, FUEL_GAUGE_ADDR, 0x2A, I2C_MEMADD_SIZE_8BIT, buffer, 2);
    currentlyReading = READ_EXTERNAL_TEMPERATURE;       // (0x0C/0x0D)
    HAL_I2C_Mem_Read_IT(&hi2c1, FUEL_GAUGE_ADDR, 0x0C, I2C_MEMADD_SIZE_8BIT, buffer, 2);
    currentlyReading = READ_VOLTAGE_SCALE;      // (0x20)
    HAL_I2C_Mem_Read_IT(&hi2c1, FUEL_GAUGE_ADDR, 0x20, I2C_MEMADD_SIZE_8BIT, buffer, 1);
    currentlyReading = READ_VOLTAGE;            // (0x08/0x09)
    HAL_I2C_Mem_Read_IT(&hi2c1, FUEL_GAUGE_ADDR, 0x08, I2C_MEMADD_SIZE_8BIT, buffer, 2);
    currentlyReading = READ_CURRENT_SCALE;      // (0x21)
    HAL_I2C_Mem_Read_IT(&hi2c1, FUEL_GAUGE_ADDR, 0x21, I2C_MEMADD_SIZE_8BIT, buffer, 1);
    currentlyReading = READ_CURRENT;            // (0x10/0x11)
    HAL_I2C_Mem_Read_IT(&hi2c1, FUEL_GAUGE_ADDR, 0x10, I2C_MEMADD_SIZE_8BIT, buffer, 2);
    currentlyReading = READ_AVG_CURRENT;        // (0x0A/0x0B)
    HAL_I2C_Mem_Read_IT(&hi2c1, FUEL_GAUGE_ADDR, 0x0A, I2C_MEMADD_SIZE_8BIT, buffer, 2);
    currentlyReading = READ_SOC;                // (0x02)
    HAL_I2C_Mem_Read_IT(&hi2c1, FUEL_GAUGE_ADDR, 0x02, I2C_MEMADD_SIZE_8BIT, buffer, 1);
    currentlyReading = READ_AVG_TIME_TO_EMPTY;  // (0x18/0x19)
    HAL_I2C_Mem_Read_IT(&hi2c1, FUEL_GAUGE_ADDR, 0x18, I2C_MEMADD_SIZE_8BIT, buffer, 1);
    currentlyReading = READ_AVG_TIME_TO_FULL;   // (0x1A/0x1B)
    HAL_I2C_Mem_Read_IT(&hi2c1, FUEL_GAUGE_ADDR, 0x1A, I2C_MEMADD_SIZE_8BIT, buffer, 1);
    currentlyReading = READ_CYCLE_COUNT;        // (0x2C/0x2D)
    HAL_I2C_Mem_Read_IT(&hi2c1, FUEL_GAUGE_ADDR, 0x2C, I2C_MEMADD_SIZE_8BIT, buffer, 2);
    currentlyReading = READ_STATE_OF_HEALTH;    // (0x2E/0x2F)
    HAL_I2C_Mem_Read_IT(&hi2c1, FUEL_GAUGE_ADDR, 0x2E, I2C_MEMADD_SIZE_8BIT, buffer, 2);
    currentlyReading = READ_FLAGS;              // (0x0E/0x0F)
    HAL_I2C_Mem_Read_IT(&hi2c1, FUEL_GAUGE_ADDR, 0x0E, I2C_MEMADD_SIZE_8BIT, buffer, 2);
}

void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *hi2c) {
    //if (hi2c == &I2C_LP)
    switch (currentlyReading) {
        case READ_INTERNAL_TEMPERATURE: {       // T is received in units of 0.1 K
            uint16_t raw = (uint16_t)((buffer[1] << 8) | buffer[0]);
            fuelGaugeSensors.internalTemperature = (raw * 0.1f) - 273.15f;
            break;
        }
        case READ_EXTERNAL_TEMPERATURE: {
            uint16_t raw = (uint16_t)((buffer[1] << 8) | buffer[0]);
            fuelGaugeSensors.externalTemperature = (raw * 0.1f) - 273.15f;
            break;
        }
        case READ_VOLTAGE_SCALE: {
            fuelGaugeSensors.voltageScale = (uint16_t)((buffer[1] << 8) | buffer[0]);
            break;
        }
        case READ_VOLTAGE: {        // To check if mV
            uint16_t raw = (uint16_t)((buffer[1] << 8) | buffer[0]);
            if (fuelGaugeSensors.voltageScale > 1) {
                fuelGaugeSensors.voltage = raw * fuelGaugeSensors.voltageScale;
            } else {
                fuelGaugeSensors.voltage = raw;
            }
            break;
        }
        case READ_CURRENT_SCALE: {
            fuelGaugeSensors.currentScale = (uint16_t)((buffer[1] << 8) | buffer[0]);
            break;
        }
        case READ_CURRENT: {        // To check if mA
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

void readCS() {        // Check and update critical signals and their status (e.g., CHRG_OK)
    // Read HP.PD_IRQ (PB14) and update PD_IRQ status
    PD_IRQ_PREV = PD_IRQ;   // Remember PD_IRQ status before updating it
    PD_IRQ = HAL_GPIO_ReadPin(USB_IRQ_CTRL_GPIO_Port, USB_IRQ_CTRL_Pin);

    // Read LP_ST_INT (PC12) and update ST_INT status
    ST_INT = HAL_GPIO_ReadPin(USB_ST_INT_CTRL_GPIO_Port, USB_ST_INT_CTRL_Pin);

    // Read HP.CHRG_OK (PB13) and update CHRG_OK status
    CHRG_OK = HAL_GPIO_ReadPin(USB_CHRG_OK_CTRL_GPIO_Port, USB_CHRG_OK_CTRL_Pin);

    // Before end of function throw eventual errors due to faults/interrupts
    if (ST_INT == GPIO_PIN_RESET) {     // (Normal: ST_INT is set to HIGH)
        // Throw Handle Aux Converter Fault (Overcurrent/Temp)
    }

    if (CHRG_OK == GPIO_PIN_RESET) {
        // Throw high-current path not ok
    }

    if (PD_IRQ != PD_IRQ_PREV && PD_IRQ == GPIO_PIN_RESET) {
        // Throw interrupt (device has been pluggled)
        pluggedINT();
    } else if (PD_IRQ != PD_IRQ_PREV && PD_IRQ == GPIO_PIN_SET) {
        // Throw interrupt (device has been unplugged)
        unpluggedINT();
    }
}

void readNCS() {        // Check and update NON-critical signals and their status (e.g., USBA1_STATUS)
    USBA1_STATUS = HAL_GPIO_ReadPin(USB_A1_CTRL_GPIO_Port, USB_A1_CTRL_Pin);
    USBA2_STATUS = HAL_GPIO_ReadPin(USB_A2_CTRL_GPIO_Port, USB_A2_CTRL_Pin);
    EN_OTG_STATUS = HAL_GPIO_ReadPin(USB_OTG_CTRL_GPIO_Port, USB_OTG_CTRL_Pin);
    ST_EN_STATUS = HAL_GPIO_ReadPin(USB_ST_EN_CTRL_GPIO_Port, USB_ST_EN_CTRL_Pin);
}

void pluggedINT() {}

void unpluggedINT() {}




