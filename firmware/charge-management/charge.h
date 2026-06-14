#ifndef __CHARGEMANAGEMENT__
#define __CHARGEMANAGEMENT__
#include "stdbool.h"
#include "stm32f4xx.h"

// Sensors:
typedef struct {
    float tempZone[4];
    float usbCInputCurrent;
    float power_sys_W;
} SensorData;

typedef struct {
    // High-Byte
    bool OTC;           // (BIT7) Overtemperature in Charge condition is detected. True when set
    bool OTD;           // (BIT6) Overtemperature in Discharge condition is detected. True when set
    bool BATHI;         // (BIT5) High battery voltage condition
    bool BATLOW;        // (BIT4) Low battery voltage condition
    bool CHG_INH;       // (BIT3) Unable to begin chargin
    bool XCHG;          // (BIT2) Charging not allowed
    bool FC;            // (BIT1) Full Charge detected
    bool CHG;           // (BIT0) (Fast) charging allowed. True when set

    // Low-Byte
    bool DSG;           // (BIT0) Discharging detected. True when set
} Flags;

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
    Flags flags;
} FuelGaugeSensors;

typedef enum {      // Possible reading states for I2C
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

// Fuel Gauge Mapping
#define FUEL_GAUGE_ADDR ((uint16_t)(0x55 << 1))     // Convert 7-bit address to 8-bit and then cast to 16-bit

// TPS25750 Mapping
// TO CHECK --> 0x20 / 0x21 ?
#define PD_CONTROLLER_ADDR ((uint16_t)(0x20 << 1))     // Convert 7-bit address to 8-bit and then cast to 16-bit
#define INT_EVENT1_REG_ADDR 0x14
#define INT_MASK1_REG_ADDR 0x16
#define INT_CLEAR1_REG_ADDR 0x18
#define POWER_STATUS_REG_ADDR 0x3F
#define ACTIVE_CONTRACT_PDO_REG_ADDR 0x34
#define ACTIVE_CONTRACT_RDO_REG_ADDR 0x35

typedef struct {
    bool isSink;
    bool isPlugged;
    float voltage;
    float maxCurrent;
    float operatingCurrent;
} PDContract;

// Critical Signals Mapping
#define USB_IRQ_CTRL_Pin GPIO_PIN_14        // IRQ Pin is the number 14
#define USB_IRQ_CTRL_GPIO_Port GPIOB        // IRQ GPIO channel is the B (PB14)
#define USB_ST_INT_CTRL_Pin GPIO_PIN_12
#define USB_ST_INT_CTRL_GPIO_Port GPIOC
#define USB_CHRG_OK_CTRL_Pin GPIO_PIN_13
#define USB_CHRG_OK_CTRL_GPIO_Port GPIOB

// NON-Critical Signals Mapping
#define USB_A1_CTRL_Pin GPIO_PIN_1
#define USB_A1_CTRL_GPIO_Port GPIOC
#define USB_A2_CTRL_Pin GPIO_PIN_2
#define USB_A2_CTRL_GPIO_Port GPIOC
#define USB_OTG_CTRL_Pin GPIO_PIN_15
#define USB_OTG_CTRL_GPIO_Port GPIOB
#define USB_ST_EN_CTRL_Pin GPIO_PIN_11
#define USB_ST_EN_CTRL_GPIO_Port GPIOC

extern SensorData sensor_data;

void init();

void readSensors();

float toVoltage(uint32_t raw);

float toCelsius(float temp);

void readCS();

void primaryUSBC_ConnectionINT();

void secondaryUSBC_ConnectionINT();






#endif