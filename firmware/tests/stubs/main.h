#ifndef __MAIN_H
#define __MAIN_H

#include "stm32_hal_stub.h"

// Pins used by fsm.c that come from the real CubeMX-generated main.h
#define BCKL_CTRL_Pin        GPIO_PIN_8
#define BCKL_CTRL_GPIO_Port  GPIOB
#define C2_LAB_EN_Pin        GPIO_PIN_11
#define C2_LAB_EN_GPIO_Port  GPIOA

void Error_Handler(void);

#endif // __MAIN_H
