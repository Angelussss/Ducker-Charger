#include "charge.h"
#include "../cubeMX/Drivers/STM32F4xx_HAL_Driver/Inc/stm32f4xx_hal_adc.h"
#include <iostream>

using namespace std;

const float MAX_V = 16.8;
const float NOMINAL_V = 14.4;

const float DEFAULT_VOLTAGE = 0.0;      // To be defined

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

void readSensors() {
    ADC_HandleTypeDef hadc;
    // Start the ADC peripheral
    HAL_ADC_Start(&hadc);

    // Read Temp Zone 1


    // Stop the ADC peripheral
    HAL_ADC_Stop(&hadc);

}









