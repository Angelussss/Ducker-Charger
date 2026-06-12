/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define STATUS_Pin GPIO_PIN_13
#define STATUS_GPIO_Port GPIOC
#define ENCOD_BUTT_Pin GPIO_PIN_0
#define ENCOD_BUTT_GPIO_Port GPIOC
#define USB_A1_CTRL_Pin GPIO_PIN_1
#define USB_A1_CTRL_GPIO_Port GPIOC
#define USB_A2_CTRL_Pin GPIO_PIN_2
#define USB_A2_CTRL_GPIO_Port GPIOC
#define DISP_CS_Pin GPIO_PIN_0
#define DISP_CS_GPIO_Port GPIOB
#define DISP_DC_Pin GPIO_PIN_1
#define DISP_DC_GPIO_Port GPIOB
#define DISP_RST_Pin GPIO_PIN_2
#define DISP_RST_GPIO_Port GPIOB
#define HP_CHRG_OK_Pin GPIO_PIN_13
#define HP_CHRG_OK_GPIO_Port GPIOB
#define HP_PD_IRQ_Pin GPIO_PIN_14
#define HP_PD_IRQ_GPIO_Port GPIOB
#define HP_EN_OTG_Pin GPIO_PIN_15
#define HP_EN_OTG_GPIO_Port GPIOB
#define LAB_ENABLER_Pin GPIO_PIN_11
#define LAB_ENABLER_GPIO_Port GPIOA
#define USB_C2_ENABLER_Pin GPIO_PIN_12
#define USB_C2_ENABLER_GPIO_Port GPIOA
#define LP_ST_EN_Pin GPIO_PIN_11
#define LP_ST_EN_GPIO_Port GPIOC
#define LP_ST_INT_Pin GPIO_PIN_12
#define LP_ST_INT_GPIO_Port GPIOC

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
