#include "charge.h"
#include "stm32f4xx_hal.h"
#include "i2c.h"

// To be configured during ADC Initialization
extern ADC_HandleTypeDef hadcTZ1;
extern ADC_HandleTypeDef hadcTZ2;
extern ADC_HandleTypeDef hadcTZ3;
extern ADC_HandleTypeDef hadcTZ4;
extern ADC_HandleTypeDef hadcUSBCIC;
extern ADC_HandleTypeDef hadcBC;
extern ADC_HandleTypeDef hadcTSP;

// I2C1 -> STUSB4710 STPD01PUR INA3221 BQ34Z100
// I2C3 -> TPS25750

/*
    To do:
        - Check after I2C_Mem_read -> if !HAL_OK to throw possible error
        - Compress PDO configuration selection into one single function
        - Re-check interrupt handling for both primary and secondary USB-C ports
        - (Optional) Read assigned V and I_max to STPD01

*/

volatile I2C_ReadState currentlyReading;

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
int STPD01_EN_STATUS = GPIO_PIN_RESET;
int USBC2_STATUS = GPIO_PIN_RESET;

SensorData sensor_data;
FuelGaugeSensors fuelGaugeSensors;
PDContract primaryUSBC_Contract;
PDContract secondaryUSBC_PDContract;
STPD01_Status stpd01_status;
// secondaryUSBC_PDContract.isSink = false;

// Ports: USB-A1; USB-A2; USB-C (primary, via TPS25750); USB-C (secondary, via STUSB4710A)

void init() {
    // Initialize INA3221 (I2C_LP) immediately to provide OCP (Overcurrent Protection) for USB-A ports
    // Enable USB ports (USB-A1 and USB-A2) and enable Aux (LP_ST_EN - PC11)
    HAL_GPIO_WritePin(USB_A1_CTRL_GPIO_Port, USB_A1_CTRL_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(USB_A2_CTRL_GPIO_Port, USB_A2_CTRL_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(USB_STPD01_EN_CTRL_GPIO_Port, USB_STPD01_EN_CTRL_Pin, GPIO_PIN_SET);

    // Secondary USB-C is always the source
    secondaryUSBC_PDContract.isSink = false;
    primaryUSBC_Contract.isNegotiationDone = false;
    secondaryUSBC_PDContract.isNegotiationDone = false;
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

        // --- Total System Power ---
    HAL_ADC_Start(&hadcTSP);
    HAL_ADC_PollForConversion(&hadcTSP, timeout);
    raw = HAL_ADC_GetValue(&hadcTSP);
    sensor_data.power_sys_W = (toVoltage(raw) / 1000.0f) * 60.0f;
    HAL_ADC_Stop(&hadcTSP);

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
    PD_IRQ_PREV = PD_IRQ;   // Remember PD_IRQ status before updating it
    PD_IRQ = HAL_GPIO_ReadPin(USB_IRQ_CTRL_GPIO_Port, USB_IRQ_CTRL_Pin);

    // Read LP_ST_INT (PC12) and update ST_INT status
    ST_INT = HAL_GPIO_ReadPin(USB_ST_INT_CTRL_GPIO_Port, USB_ST_INT_CTRL_Pin);

    // Read HP.CHRG_OK (PB13) and update CHRG_OK status
    CHRG_OK = HAL_GPIO_ReadPin(USB_CHRG_OK_CTRL_GPIO_Port, USB_CHRG_OK_CTRL_Pin);

    // Before end of function throw eventual errors due to faults/interrupts
    if (ST_INT == GPIO_PIN_RESET) {     // (Normal: ST_INT is set to HIGH)
        // Throw Handle Aux Converter Fault (Overcurrent/Temp)
        // To confirm overtemperature check OTD / OTC flags
    }

    if (CHRG_OK == GPIO_PIN_RESET) {
        // Throw high-current path not ok
    }

    if (PD_IRQ == GPIO_PIN_RESET) {
        primaryUSBC_ConnectionINT();
    }

    // Remember to add the secondary USB-C interrupt + handling

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

    if (plugInsertOrRemoval && powerStatusUpdate) {
        primaryUSBC_Contract.isPlugged = powerConnection;
        if (primaryUSBC_Contract.isPlugged) {
            // Determine whether the powerbank is the source or the sink
            primaryUSBC_Contract.isSink = sourceSink;        // 1 -> is sink; 0 -> is source
        } else {        // if not plugged
            primaryUSBC_Contract.isNegotiationDone = false;
        }
    }

    if (primaryUSBC_Contract.isPlugged && (newContractAsCons || newContractAsProv)) {
        primaryUSBC_Contract.isNegotiationDone = true;
        // Read incoming contract
        uint8_t activeContractPDOBuffer[6];
        uint8_t activeContractRDOBuffer[4];

        // Read PDO for fetching Voltage a maximum Current
        HAL_I2C_Mem_Read(&hi2c3, TPS25750_PD_CONTROLLER_ADDR, ACTIVE_CONTRACT_PDO_REG_ADDR, I2C_MEMADD_SIZE_8BIT, activeContractPDOBuffer, 6, HAL_MAX_DELAY);
        uint16_t voltageRaw = ((activeContractPDOBuffer[2] & 0x0F) << 6) | ((activeContractPDOBuffer[1] >> 2) & 0x3F);  // (19:10)
        uint16_t maxCurrentRaw = ((activeContractPDOBuffer[1] & 0x03) << 8) | activeContractPDOBuffer[0];       // (9:0)

        // Read RDO for fetching negotiated current
        HAL_I2C_Mem_Read(&hi2c3, TPS25750_PD_CONTROLLER_ADDR, ACTIVE_CONTRACT_RDO_REG_ADDR, I2C_MEMADD_SIZE_8BIT, activeContractRDOBuffer, 4, HAL_MAX_DELAY);
        uint16_t operatingCurrentRaw = ((activeContractRDOBuffer[2] & 0x0F) << 6) | ((activeContractRDOBuffer[1] >> 2) & 0x03F);        // (19:10)

        // Convert fetched values accordingly
        primaryUSBC_Contract.voltage = voltageRaw * 50.0f;                    // To get mV (as stated in the USB_PD standard)
        primaryUSBC_Contract.maxCurrent = maxCurrentRaw * 10.0f;              // To get mA (as stated in the USB_PD standard)
        primaryUSBC_Contract.operatingCurrent = operatingCurrentRaw * 10.0f;  // To get mA (as stated in the USB_PD standard)

        // INA3221?
    }

    // Clear INT_EVENT1 to make I2Cs_IRQ back to HIGH (only bits previously read are cleared to be able to get possibly new occurring interrupts)
    HAL_I2C_Mem_Write(&hi2c3, TPS25750_PD_CONTROLLER_ADDR, INT_CLEAR1_REG_ADDR, I2C_MEMADD_SIZE_8BIT, eventBuffer, 11, HAL_MAX_DELAY);
}

void secondaryUSBC_ConnectionINT() {
    uint8_t alertBuffer[2];
    uint8_t activeContractRDOBuffer[4];
    // Read alert status register (the register is read and clear)
    HAL_I2C_Mem_Read(&hi2c1, STUSB4710_PD_CONTROLLER_ADDR, ALERT_STATUS_REG_ADDR, I2C_MEMADD_SIZE_8BIT, alertBuffer, 1, HAL_MAX_DELAY);
    bool CC_HW_FAULT_STATUS_AL = (alertBuffer[0] >> 4) & 0x01;      // Need to handle the alert
    bool TYPEC_MONITORING_STATUS_AL = (alertBuffer[0] >> 5) & 0x01; // Need to handle the alert
    bool PORT_STATUS_AL = (alertBuffer[0] >> 6) & 0x01;             // Need to handle the alert

    // Read port status register if alert is fired
    if (PORT_STATUS_AL) {
        HAL_I2C_Mem_Read(&hi2c1, STUSB4710_PD_CONTROLLER_ADDR, CC_CONNECTION_STATUS_REG_ADDR, I2C_MEMADD_SIZE_8BIT, alertBuffer, 1, HAL_MAX_DELAY);
        switch ((alertBuffer[0] >> 5) & 0x07) {
            case 0: {       // NONE_ATT: Not connected
                secondaryUSBC_PDContract.isPlugged = false;
                secondaryUSBC_PDContract.isNegotiationDone = false;
                disable_USBC2();
                disable_STPD01();
                break;
            }
            case 1: {       // SNK_ATT: Sink device connected
                secondaryUSBC_PDContract.isPlugged = true;
                break;
            }
            case 2: {       // SRC_ATT: Source device connected
                secondaryUSBC_PDContract.isPlugged = false;     // Reject connection
                secondaryUSBC_PDContract.isNegotiationDone = false;
                disable_USBC2();
                disable_STPD01();
                //secondaryUSBC_PDContract.isSink = true;
                // Throw error: secondary USB-C is source-only
                break;
            }
            default: {      // DBG_ATT / AUD_ATT / POW_ACC_ATT
                // Throw unexpected flag
            }
        }
    }

    if (secondaryUSBC_PDContract.isPlugged && !secondaryUSBC_PDContract.isNegotiationDone && !secondaryUSBC_PDContract.isSink) {
        // Fetch (if RDO negotiated the PDO configuration)
        HAL_I2C_Mem_Read(&hi2c1, STUSB4710_PD_CONTROLLER_ADDR, SRC_RDO_REG_ADDR, I2C_MEMADD_SIZE_8BIT, activeContractRDOBuffer, 4, HAL_MAX_DELAY);
        uint8_t PDOConfiguration = (activeContractRDOBuffer[3] >> 4) & 0x07;
        switch (PDOConfiguration) {
            case 0: {
                // No negotiated contract
                break;
            }
            case 1: {
                secondaryUSBC_PDContract.isNegotiationDone = true;
                selectPDO1();
                break;
            }
            case 2: {
                secondaryUSBC_PDContract.isNegotiationDone = true;
                selectPDO2();
                break;
            }
            case 3: {
                secondaryUSBC_PDContract.isNegotiationDone = true;
                selectPDO3();
                break;
            }
            case 4: {
                secondaryUSBC_PDContract.isNegotiationDone = true;
                selectPDO4();
                break;
            }
            case 5: {
                secondaryUSBC_PDContract.isNegotiationDone = true;
                selectPDO5();
                break;
            }
            default: {
                // Throw error
            }
        }

        if (PDOConfiguration > 0 && PDOConfiguration <= 5) {
            uint16_t operatingCurrentRaw = ((activeContractRDOBuffer[2] & 0x0F) << 6) | ((activeContractRDOBuffer[1] >> 2) & 0x03F);        // (19:10)
            secondaryUSBC_PDContract.operatingCurrent = operatingCurrentRaw * 10.0f;  // To get mA (as stated in the USB_PD standard)
            setupSTPD01(secondaryUSBC_PDContract.voltage, secondaryUSBC_PDContract.maxCurrent);

            // After setup, enable STPD01, wait to let it stabilize and then check for its flags and VBUS
            enable_STPD01();
            HAL_Delay(10);      // Wait for 10ms
            // Check VBUS_EN_SRC and STPD01 flags, if everything ok enable charging
            if (checkSTPD01()) {
                // Check VBUS_EN_SRC to confirm power path is alive, if ok enable USB-C2 port
                uint8_t vbusBuffer;
                HAL_I2C_Mem_Read(&hi2c1, STUSB4710_PD_CONTROLLER_ADDR, VBUS_ENABLE_STATUS_REG_ADDR, I2C_MEMADD_SIZE_8BIT, &vbusBuffer, 1, HAL_MAX_DELAY);
                if (vbusBuffer & 0x01) {
                    enable_USBC2();
                }
            } else {
                disable_STPD01();
                if (stpd01_status.shortCircuitProtection) {
                    // Throw STPD01 short-circuit handling
                    // Probably disables both STPD01 and USB-C2 (if enabled)
                } else if (stpd01_status.overVoltageProtection) {
                    // Throw STPD01 over voltage handling
                    // Probably disables both STPD01 and USB-C2 (if enabled)
                } else if (stpd01_status.overTemperatureWarning) {
                    // Throw over temperature warning handling
                    // (Possibly enables anyway the std01 and continues charging procedure)
                } else if (stpd01_status.overTemperatureProtection) {
                    // Throw STPD01 over temperature handling
                    // Probably disables both STPD01 and USB-C2 (if enabled)
                } else {
                    // Throw inductor peak current protection handling
                    // Probably disables both STPD01 and USB-C2 (if enabled)
                }
            }

        }

    }

    // To do: manage VBUS_EN_SRC

}

void setupSTPD01(float voltage_mV, float current_mA) {
    // First, ensure STPD01 is disabled
    disable_STPD01();
    // --- Convert max voltage and max current into the correspontent addresses
    // Clamp values to STPD operational range
    if (voltage_mV < 3000) {
        voltage_mV = 3000;
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
    HAL_I2C_Mem_Write(&hi2c1, STPD01_PD_ADDR, VOUT_REG_ADDR, I2C_MEMADD_SIZE_8BIT, &voltageReg, 1, HAL_MAX_DELAY);
    HAL_I2C_Mem_Write(&hi2c1, STPD01_PD_ADDR, ILIM_REG_ADDR, I2C_MEMADD_SIZE_8BIT, &currentReg, 1, HAL_MAX_DELAY);
}

bool checkSTPD01() {
    uint8_t buffer;
    HAL_I2C_Mem_Read(&hi2c1, STPD01_PD_ADDR, INT_STAT_REG_ADDR, I2C_MEMADD_SIZE_8BIT, &buffer, 1, HAL_MAX_DELAY);
    stpd01_status.overVoltageProtection = (buffer >> 0) & 0x01;
    stpd01_status.shortCircuitProtection = (buffer >> 2) & 0x01;
    stpd01_status.powerOn = (buffer >> 3) & 0x01;
    // OR:
    // stpd01_status.powerOn = HAL_GPIO_ReadPin(USB_STPD01_EN_CTRL_GPIO_Port, USB_STPD01_EN_CTRL_Pin);   //(already implemented in NCS reading)
    // stpd01_status.powerOn = STPD01_EN_STATUS;
    stpd01_status.overTemperatureProtection = (buffer >> 5) & 0x01;
    stpd01_status.overTemperatureWarning = (buffer >> 6) & 0x01;
    stpd01_status.inductorPeakCurrentProtection = (buffer >> 7) & 0x01;
    if (stpd01_status.overVoltageProtection ||
        stpd01_status.shortCircuitProtection ||
        stpd01_status.overTemperatureProtection ||
        stpd01_status.overTemperatureWarning ||
        stpd01_status.inductorPeakCurrentProtection) {
        return false;
    }
    return true;
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

// Selection functions for PDO configuration for secondary USB-C
void selectPDO1() {
    uint8_t activeContractPDOBuffer[4];
    // Read PDO for fetching Voltage a maximum Current
    HAL_I2C_Mem_Read(&hi2c1, STUSB4710_PD_CONTROLLER_ADDR, SRC_PDO1_REG_ADDR, I2C_MEMADD_SIZE_8BIT, activeContractPDOBuffer, 4, HAL_MAX_DELAY);
    uint16_t voltageRaw = ((activeContractPDOBuffer[2] & 0x0F) << 6) | ((activeContractPDOBuffer[1] >> 2) & 0x3F);  // (19:10)
    uint16_t maxCurrentRaw = ((activeContractPDOBuffer[1] & 0x03) << 8) | activeContractPDOBuffer[0];       // (9:0)

    // Convert fetched values accordingly
    secondaryUSBC_PDContract.voltage = voltageRaw * 50.0f;                    // To get mV (as stated in the USB_PD standard)
    secondaryUSBC_PDContract.maxCurrent = maxCurrentRaw * 10.0f;              // To get mA (as stated in the USB_PD standard)
}

void selectPDO2() {
    uint8_t activeContractPDOBuffer[4];
    // Read PDO for fetching Voltage a maximum Current
    HAL_I2C_Mem_Read(&hi2c1, STUSB4710_PD_CONTROLLER_ADDR, SRC_PDO2_REG_ADDR, I2C_MEMADD_SIZE_8BIT, activeContractPDOBuffer, 4, HAL_MAX_DELAY);
    uint16_t voltageRaw = ((activeContractPDOBuffer[2] & 0x0F) << 6) | ((activeContractPDOBuffer[1] >> 2) & 0x3F);  // (19:10)
    uint16_t maxCurrentRaw = ((activeContractPDOBuffer[1] & 0x03) << 8) | activeContractPDOBuffer[0];       // (9:0)

    // Convert fetched values accordingly
    secondaryUSBC_PDContract.voltage = voltageRaw * 50.0f;                    // To get mV (as stated in the USB_PD standard)
    secondaryUSBC_PDContract.maxCurrent = maxCurrentRaw * 10.0f;              // To get mA (as stated in the USB_PD standard)
}

void selectPDO3() {
    uint8_t activeContractPDOBuffer[4];
    // Read PDO for fetching Voltage a maximum Current
    HAL_I2C_Mem_Read(&hi2c1, STUSB4710_PD_CONTROLLER_ADDR, SRC_PDO3_REG_ADDR, I2C_MEMADD_SIZE_8BIT, activeContractPDOBuffer, 4, HAL_MAX_DELAY);
    uint16_t voltageRaw = ((activeContractPDOBuffer[2] & 0x0F) << 6) | ((activeContractPDOBuffer[1] >> 2) & 0x3F);  // (19:10)
    uint16_t maxCurrentRaw = ((activeContractPDOBuffer[1] & 0x03) << 8) | activeContractPDOBuffer[0];       // (9:0)

    // Convert fetched values accordingly
    secondaryUSBC_PDContract.voltage = voltageRaw * 50.0f;                    // To get mV (as stated in the USB_PD standard)
    secondaryUSBC_PDContract.maxCurrent = maxCurrentRaw * 10.0f;              // To get mA (as stated in the USB_PD standard)
}

void selectPDO4() {
    uint8_t activeContractPDOBuffer[4];
    // Read PDO for fetching Voltage a maximum Current
    HAL_I2C_Mem_Read(&hi2c1, STUSB4710_PD_CONTROLLER_ADDR, SRC_PDO4_REG_ADDR, I2C_MEMADD_SIZE_8BIT, activeContractPDOBuffer, 4, HAL_MAX_DELAY);
    uint16_t voltageRaw = ((activeContractPDOBuffer[2] & 0x0F) << 6) | ((activeContractPDOBuffer[1] >> 2) & 0x3F);  // (19:10)
    uint16_t maxCurrentRaw = ((activeContractPDOBuffer[1] & 0x03) << 8) | activeContractPDOBuffer[0];       // (9:0)

    // Convert fetched values accordingly
    secondaryUSBC_PDContract.voltage = voltageRaw * 50.0f;                    // To get mV (as stated in the USB_PD standard)
    secondaryUSBC_PDContract.maxCurrent = maxCurrentRaw * 10.0f;              // To get mA (as stated in the USB_PD standard)
}

void selectPDO5() {
    uint8_t activeContractPDOBuffer[4];
    // Read PDO for fetching Voltage a maximum Current
    HAL_I2C_Mem_Read(&hi2c1, STUSB4710_PD_CONTROLLER_ADDR, SRC_PDO5_REG_ADDR, I2C_MEMADD_SIZE_8BIT, activeContractPDOBuffer, 4, HAL_MAX_DELAY);
    uint16_t voltageRaw = ((activeContractPDOBuffer[2] & 0x0F) << 6) | ((activeContractPDOBuffer[1] >> 2) & 0x3F);  // (19:10)
    uint16_t maxCurrentRaw = ((activeContractPDOBuffer[1] & 0x03) << 8) | activeContractPDOBuffer[0];       // (9:0)

    // Convert fetched values accordingly
    secondaryUSBC_PDContract.voltage = voltageRaw * 50.0f;                    // To get mV (as stated in the USB_PD standard)
    secondaryUSBC_PDContract.maxCurrent = maxCurrentRaw * 10.0f;              // To get mA (as stated in the USB_PD standard)
}
