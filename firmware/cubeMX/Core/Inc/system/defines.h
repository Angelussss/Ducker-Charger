#ifndef SYSTEM_DEFINES_H
#define SYSTEM_DEFINES_H

#include "stm32f4xx.h"

// ---- SoC / voltage thresholds — 4S3P Sony VTC5, BQ7791500 UV=2900mV/cell
// OV=4200mV/cell ----
#define SOC_SAFETY_THRESHOLD 15.0f // % — IDLE → SAFETY_LOCK
#define SOC_LOWV_THRESHOLD 10.0f   // % — SAFETY_LOCK → LOW_V
#define SOC_OK_THRESHOLD 15.0f     // % — recovery back to IDLE
#define UNDERV_VOLTAGE_MV                                                      \
  12000.0f // mV pack — 3000 mV/cell, 100 mV above BQ7791500 UV cutoff (2900
           // mV/cell)
#define BATHI_VOLTAGE_MV                                                       \
  16400.0f // mV pack — 4100 mV/cell × 4S, mirrors BQ34Z100 BATHI data flash
           // threshold

#define INACTIVITY_TIMEOUT_MS 30000 // ms — IDLE/CHARGING/LOW_V → SLEEP

// ---- BQ34Z100-R2 Fuel Gauge @ I2C1 ----
#define FUEL_GAUGE_ADDR ((uint16_t)(0x55 << 1))

// ---- TPS25750 Primary USB-C PD Controller @ I2C3 ----
#define TPS25750_PD_CONTROLLER_ADDR ((uint16_t)(0x20 << 1))
#define INT_EVENT1_REG_ADDR 0x14
#define INT_MASK1_REG_ADDR 0x16
#define INT_CLEAR1_REG_ADDR 0x18
#define POWER_STATUS_REG_ADDR 0x3F
#define ACTIVE_CONTRACT_PDO_REG_ADDR 0x34
#define ACTIVE_CONTRACT_RDO_REG_ADDR 0x35

// ---- CYPD3175-24LQXQ Secondary USB-C PD Controller @ I2C1 (CCG3PA, EZ-PD HPI) ----
// Default 7-bit address; 0x08/0x12/0x44 are CCGx defaults per EZ-PD Config Utility §4.11.5
#define CYPD3175_PD_CONTROLLER_ADDR     ((uint16_t)(0x08 << 1))
// Device-level HPI registers (offsets from 0x0000, per cy_hpi_master_dev_reg_t)
#define CYPD3175_INTR_REG               ((uint16_t)0x0006)
#define CYPD3175_RESPONSE_REG           ((uint16_t)0x007E)
// INTR_REG bit masks
#define CYPD3175_INTR_DEV_BIT           (0x01U)
#define CYPD3175_INTR_PORT0_BIT         (0x02U)
// Port 0 HPI registers (port base 0x1000 + offset, per cy_hpi_master_port_reg_t)
#define CYPD3175_PORT0_TYPE_C_STATUS    ((uint16_t)0x100C)
#define CYPD3175_PORT0_CURRENT_PDO      ((uint16_t)0x1010)
#define CYPD3175_PORT0_CURRENT_RDO      ((uint16_t)0x1014)
// HPI event codes read from RESPONSE_REG (per cy_hpi_master_response_t)
#define CYPD3175_EVT_OCP                (0x82U)
#define CYPD3175_EVT_OVP                (0x83U)
#define CYPD3175_EVT_DISCONNECT         (0x85U)
#define CYPD3175_EVT_CONTRACT_COMPLETE  (0x86U)
#define CYPD3175_EVT_HARD_RESET         (0x8FU)
#define CYPD3175_EVT_OTP                (0xB6U)

// ---- STPD01 Aux Buck Converter @ I2C1 ----
// 7-bit address per datasheet Table 9: ADD pin grounded on PCB -> ADD1,ADD0 = 01 -> 0b0000101
#define STPD01_PD_ADDR ((uint16_t)(0x05 << 1))
#define VOUT_REG_ADDR 0x00
#define ILIM_REG_ADDR 0x01
#define INT_STAT_REG_ADDR 0x02
#define DIGITAL_ENABLE_REG_ADDR 0x06

// ---- INA3221 3-Channel Current Monitor @ I2C1 ----
// CH1 = USB-A1, CH2 = USB-A2, CH3 = GND (unused)
#define INA3221_ADDR ((uint16_t)(0x40 << 1))
#define INA3221_REG_CONFIG 0x00
#define SHUNT_VOLTAGE_CH1_REG_ADDR 0x01
#define SHUNT_VOLTAGE_CH2_REG_ADDR 0x03
#define SHUNT_VOLTAGE_CH3_REG_ADDR 0x05
#define MASK_ENABLE_REG_ADDR 0x0F

// ---- Critical signals (EXTI / status inputs) ----
#define USB_IRQ_CTRL_Pin GPIO_PIN_14
#define USB_IRQ_CTRL_GPIO_Port GPIOB
#define STPD01_INT_Pin GPIO_PIN_12
#define STPD01_INT_GPIO_Port GPIOC
#define CYPD3175_INT_Pin GPIO_PIN_3
#define CYPD3175_INT_GPIO_Port GPIOC
#define USB_CHRG_OK_CTRL_Pin GPIO_PIN_13
#define USB_CHRG_OK_CTRL_GPIO_Port GPIOB

// ---- Non-critical signals (port enable outputs) ----
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

#endif // SYSTEM_DEFINES_H
