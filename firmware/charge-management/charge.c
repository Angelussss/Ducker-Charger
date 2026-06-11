#include "charge.h"
#include "stm32f4xx_hal.h"

// To be configured during ADC Initialization
extern ADC_HandleTypeDef hadcTZ1;
extern ADC_HandleTypeDef hadcTZ2;
extern ADC_HandleTypeDef hadcTZ3;
extern ADC_HandleTypeDef hadcTZ4;
extern ADC_HandleTypeDef hadcUSBCIC;
extern ADC_HandleTypeDef hadcBC;
extern ADC_HandleTypeDef hadcTSP;
// interrupt per fuel gauge

const int timeout = 10;         // In ms
const float VREF = 3300;        // In mV
const float VMAX = 4095;        // 2^12 - 1
const float V_25 = 760;         // Voltage at 25ºC (in mV)
const float AVG_SLOPE = 2.5f;   // mV/ºC

SensorData sensor_data;

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

}









