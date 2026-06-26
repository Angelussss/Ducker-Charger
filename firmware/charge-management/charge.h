#ifndef __CHARGEMANAGEMENT__
#define __CHARGEMANAGEMENT__
#include "stdbool.h"
#include "stm32f4xx.h"

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

// Fuel Gauge Mapping
#define FUEL_GAUGE_ADDR ((uint16_t)(0x55 << 1))     // Convert 7-bit address to 8-bit and then cast to 16-bit

// TPS25750 Mapping
// TO CHECK --> 0x20 / 0x21 ?
#define TPS25750_PD_CONTROLLER_ADDR ((uint16_t)(0x20 << 1))     // Convert 7-bit address to 8-bit and then cast to 16-bit
#define INT_EVENT1_REG_ADDR 0x14
#define INT_MASK1_REG_ADDR 0x16
#define INT_CLEAR1_REG_ADDR 0x18
#define POWER_STATUS_REG_ADDR 0x3F
#define ACTIVE_CONTRACT_PDO_REG_ADDR 0x34
#define ACTIVE_CONTRACT_RDO_REG_ADDR 0x35

// STUSB4710 Mapping
#define STUSB4710_PD_CONTROLLER_ADDR ((uint16_t)(0x28 << 1))
#define ALERT_STATUS_REG_ADDR 0x0B
#define CC_CONNECTION_STATUS_REG_ADDR 0x0E      // Equivalent of PORT_STATUS_1 register of the STUSB4700
#define VBUS_ENABLE_STATUS_REG_ADDR 0x27                 // To check if power path is alive
#define SRC_PDO1_REG_ADDR 0x71
#define SRC_PDO2_REG_ADDR 0x75
#define SRC_PDO3_REG_ADDR 0x79
#define SRC_PDO4_REG_ADDR 0x7D
#define SRC_PDO5_REG_ADDR 0x81
#define SRC_RDO_REG_ADDR 0x91

// STPD01 Mapping
// (Power supply for secondary USB-C)
#define STPD01_PD_ADDR ((uint16_t)(0x54 << 1))      // To be verified (according to GitHub repo)
#define VOUT_REG_ADDR 0x00
#define ILIM_REG_ADDR 0x01
#define INT_STAT_REG_ADDR 0x02
//#define SERVICES_REG_ADDR 0x05        // Contains the discharge toggle ON/OFF (default is ON)
#define DIGITAL_ENABLE_REG_ADDR 0x06

// INA3221 Mapping
#define INA3221_ADDR ((uint16_t)(0x40 << 1))
#define SHUNT_VOLTAGE_CH1_REG_ADDR 0x01
#define SHUNT_VOLTAGE_CH2_REG_ADDR 0x03
#define SHUNT_VOLTAGE_CH3_REG_ADDR 0x05
#define MASK_ENABLE_REG_ADDR 0x0F

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
#define USB_STPD01_EN_CTRL_Pin GPIO_PIN_11
#define USB_STPD01_EN_CTRL_GPIO_Port GPIOC
#define USB_C2_EN_Pin GPIO_PIN_12
#define USB_C2_EN_GPIO_Port GPIOA

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

void enable_USBA1();

void disable_USBA1();

void enable_USBA2();

void disable_USBA2();

void enable_STPD01 ();

void disable_STPD01 ();

void enable_USBC2();

void disable_USBC2();

void selectPDO1();

void selectPDO2();

void selectPDO3();

void selectPDO4();

void selectPDO5();

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