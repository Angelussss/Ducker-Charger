#ifndef __CHARGEMANAGEMENT__
#define __CHARGEMANAGEMENT__
#include "stdbool.h"
#include "system/defines.h"
#include "system/event.h"

// Sensors:
typedef struct {
    float tempZone[4];
    float onBoardTemperature;
    float usbCInputCurrent;
    float power_sys_W;
} SensorData;

typedef struct {
    // High-Byte
    bool OTC;           // (BIT7) Overtemperature in Charge condition is detected. True when set
    bool OTD;           // (BIT6) Overtemperature in Discharge condition is detected. True when set
    bool BATHI;         // (BIT5) High battery voltage condition
    bool BATLOW;        // (BIT4) Low battery voltage condition
    bool CHG_INH;       // (BIT3) Unable to start charging
    bool XCHG;          // (BIT2) Charging not allowed
    bool FC;            // (BIT1) Full Charge detected
    bool CHG;           // (BIT0) (Fast) charging allowed. True when set

    // Low-Byte
    bool DSG;           // (BIT0) Discharging detected. True when set
} fuelGaugeFlags;

typedef struct {
    float internalTemperature;  // Expressed in ºC
    float externalTemperature;  // Expressed in ºC
    int voltageScale;
    float voltage;              // Expressed in mV
    int currentScale;
    float current;              // Expressed in mA
    float avgCurrent;           // Expressed in mA
    float SoC;                  // State of Charge in % (referred to the Full Charge Capacity)
    float avgTimeToEmpty;       // Expressed in minutes; a value of 65535 indicates the battery is not being discharged
    float avgTimeToFull;        // Expressed in minutes; a value of 65535 indicates the battery is not charging
    unsigned int cycleCount;
    unsigned int stateOfHealth; // Expressed in percentage
    // Available energy
    // Average power
    fuelGaugeFlags flags;
} FuelGaugeSensors;

typedef struct {
    bool isSink;
    bool isPlugged;
    bool isNegotiationDone;
    float voltage;
    float maxCurrent;
    float operatingCurrent;
} PDContract;

typedef struct {
    bool overVoltageProtection;
    bool shortCircuitProtection;
    bool powerOn;
    bool overTemperatureProtection;
    bool overTemperatureWarning;
    bool inductorPeakCurrentProtection;
} STPD01_Status;

typedef struct {
    float current_channel1;
    float current_channel2;
    float current_channel3;
    bool critical_alert_channel1;
    bool critical_alert_channel2;
    bool critical_alert_channel3;
} INA3221_Sensors;

typedef enum {
    READ_INTERNAL_TEMPERATURE,
    READ_EXTERNAL_TEMPERATURE,
    READ_VOLTAGE_SCALE,
    READ_VOLTAGE,
    READ_CURRENT_SCALE,
    READ_CURRENT,
    READ_AVG_CURRENT,
    READ_SOC,
    READ_AVG_TIME_TO_EMPTY,
    READ_AVG_TIME_TO_FULL,
    READ_CYCLE_COUNT,
    READ_STATE_OF_HEALTH,
    READ_FLAGS
} I2C_ReadState;


extern SensorData sensor_data;

void init();

void readSensors();

float toVoltage(uint32_t raw);

float toCelsius(float temp);

void readINA();

void readCS();

void readNCS();

void primaryUSBC_ConnectionINT();

void secondaryUSBC_ConnectionINT();

bool setupSTPD01(float voltage, float current);

bool checkSTPD01();

void stpd01_PowerStateINT();

void enable_USBA1();

void disable_USBA1();

void enable_USBA2();

void disable_USBA2();

void enable_STPD01 ();

void disable_STPD01 ();

void enable_USBC2();

void disable_USBC2();

// Get data functions
SensorData getSensorData();

FuelGaugeSensors getFuelGaugeData();

STPD01_Status getSTPD01_Status();

INA3221_Sensors getINA3221_Sensors();

PDContract getPrimaryUSBC_Contract();

PDContract getSecondaryUSBC_Contract();

int get_USBA1_Status();

int get_USBA2_Status();

int get_USBC2_Status();

int get_OTG_Status();

int get_STPD01_Enabled();

#endif