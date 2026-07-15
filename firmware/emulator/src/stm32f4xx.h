/* EMULATOR shim: system/defines.h includes "stm32f4xx.h" for the GPIO
 * macros, everything it needs lives in the fake HAL header. */
#ifndef SIM_STM32F4XX_H
#define SIM_STM32F4XX_H
#include "stm32f4xx_hal.h"
#endif
