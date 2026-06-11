#ifndef __CHARGEMANAGEMENT__
#define __CHARGEMANAGEMENT__
#include "stm32f4xx.h"

// Sensors:
typedef struct {
    float tempZone[4];
    float usbCInputCurrent;
    float batteryCurrent;
    float power_sys_W;
} SensorData;

extern SensorData sensor_data;

void init();

void readSensors();

float toVoltage(uint32_t raw);

float toCelsius(float temp);












#endif