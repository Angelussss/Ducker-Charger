#ifndef SYSTEM_DEFINES_H
#define SYSTEM_DEFINES_H

#include "stm32f4xx.h"

// ---- SoC / voltage thresholds — 4S3P Sony VTC5, BQ7791500 UV=2900mV/cell OV=4200mV/cell ----
#define SOC_SAFETY_THRESHOLD   15.0f      // % — IDLE → SAFETY_LOCK
#define SOC_LOWV_THRESHOLD     10.0f      // % — SAFETY_LOCK → LOW_V
#define SOC_OK_THRESHOLD       15.0f      // % — recovery back to IDLE
#define UNDERV_VOLTAGE_MV   12400.0f      // mV pack — 3100 mV/cell, 200 mV above BQ7791500 UV cutoff (2900 mV/cell)

#define INACTIVITY_TIMEOUT_MS  30000      // ms — IDLE/CHARGING/LOW_V → SLEEP

// ---- BQ34Z100-R2 Fuel Gauge @ I2C1 ----
#define FUEL_GAUGE_ADDR                 ((uint16_t)(0x55 << 1))

// ---- TPS25750 Primary USB-C PD Controller @ I2C3 ----
#define TPS25750_PD_CONTROLLER_ADDR     ((uint16_t)(0x20 << 1))
#define INT_EVENT1_REG_ADDR             0x14
#define INT_MASK1_REG_ADDR              0x16
#define INT_CLEAR1_REG_ADDR             0x18
#define POWER_STATUS_REG_ADDR           0x3F
#define ACTIVE_CONTRACT_PDO_REG_ADDR    0x34
#define ACTIVE_CONTRACT_RDO_REG_ADDR    0x35

// ---- STUSB4710 Secondary USB-C PD Controller @ I2C1 ----
#define STUSB4710_PD_CONTROLLER_ADDR    ((uint16_t)(0x28 << 1))
#define ALERT_STATUS_REG_ADDR           0x0B
#define CC_CONNECTION_STATUS_REG_ADDR   0x0E
#define VBUS_ENABLE_STATUS_REG_ADDR     0x27
#define SRC_PDO1_REG_ADDR               0x71
#define SRC_PDO2_REG_ADDR               0x75
#define SRC_PDO3_REG_ADDR               0x79
#define SRC_PDO4_REG_ADDR               0x7D
#define SRC_PDO5_REG_ADDR               0x81
#define SRC_RDO_REG_ADDR                0x91

// ---- STPD01 Aux Buck Converter @ I2C1 ----
#define STPD01_PD_ADDR                  ((uint16_t)(0x54 << 1))
#define VOUT_REG_ADDR                   0x00
#define ILIM_REG_ADDR                   0x01
#define INT_STAT_REG_ADDR               0x02
#define DIGITAL_ENABLE_REG_ADDR         0x06

// ---- INA3221 3-Channel Current Monitor @ I2C1 ----
// CH1 = USB-A1, CH2 = USB-A2, CH3 = GND (unused)
#define INA3221_ADDR                    ((uint16_t)(0x40 << 1))
#define SHUNT_VOLTAGE_CH1_REG_ADDR      0x01
#define SHUNT_VOLTAGE_CH2_REG_ADDR      0x03
#define SHUNT_VOLTAGE_CH3_REG_ADDR      0x05
#define MASK_ENABLE_REG_ADDR            0x0F

// ---- Critical signals (EXTI / status inputs) ----
#define USB_IRQ_CTRL_Pin                GPIO_PIN_14
#define USB_IRQ_CTRL_GPIO_Port          GPIOB
#define USB_ST_INT_CTRL_Pin             GPIO_PIN_12
#define USB_ST_INT_CTRL_GPIO_Port       GPIOC
#define C2_RDY_CTRL_Pin                 GPIO_PIN_3
#define C2_RDY_CTRL_GPIO_Port           GPIOC
#define USB_CHRG_OK_CTRL_Pin            GPIO_PIN_13
#define USB_CHRG_OK_CTRL_GPIO_Port      GPIOB

// ---- Non-critical signals (port enable outputs) ----
#define USB_A1_CTRL_Pin                 GPIO_PIN_1
#define USB_A1_CTRL_GPIO_Port           GPIOC
#define USB_A2_CTRL_Pin                 GPIO_PIN_2
#define USB_A2_CTRL_GPIO_Port           GPIOC
#define USB_OTG_CTRL_Pin                GPIO_PIN_15
#define USB_OTG_CTRL_GPIO_Port          GPIOB
#define USB_STPD01_EN_CTRL_Pin          GPIO_PIN_11
#define USB_STPD01_EN_CTRL_GPIO_Port    GPIOC
#define USB_C2_EN_Pin                   GPIO_PIN_12
#define USB_C2_EN_GPIO_Port             GPIOA

#endif // SYSTEM_DEFINES_H
