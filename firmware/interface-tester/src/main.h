/* Fake main.h for native UI simulation, pin names match current firmware main.h. */
#ifndef SIM_MAIN_H
#define SIM_MAIN_H
#include "stm32f4xx_hal.h"
extern GPIO_TypeDef SIM_GPIOA, SIM_GPIOB, SIM_GPIOC;
#define ENCOD_BUTT_Pin         GPIO_PIN_0
#define ENCOD_BUTT_GPIO_Port   (&SIM_GPIOC)
#define USB_A1_CTRL_Pin        GPIO_PIN_1
#define USB_A1_CTRL_GPIO_Port  (&SIM_GPIOC)
#define USB_A2_CTRL_Pin        GPIO_PIN_2
#define USB_A2_CTRL_GPIO_Port  (&SIM_GPIOC)
#define C2_LAB_EN_Pin          GPIO_PIN_11
#define C2_LAB_EN_GPIO_Port    (&SIM_GPIOA)
#define C2_PORT_EN_Pin         GPIO_PIN_12
#define C2_PORT_EN_GPIO_Port   (&SIM_GPIOA)
#define DISP_CS_Pin            GPIO_PIN_0
#define DISP_CS_GPIO_Port      (&SIM_GPIOB)
#define DISP_DC_Pin            GPIO_PIN_1
#define DISP_DC_GPIO_Port      (&SIM_GPIOB)
#define DISP_RST_Pin           GPIO_PIN_2
#define DISP_RST_GPIO_Port     (&SIM_GPIOB)
#define BCKL_CTRL_Pin          GPIO_PIN_8
#define BCKL_CTRL_GPIO_Port    (&SIM_GPIOB)
#endif
