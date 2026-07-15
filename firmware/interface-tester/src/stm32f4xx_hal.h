/* Fake HAL for native UI simulation, replaces the real STM32 HAL. */
#ifndef SIM_STM32F4XX_HAL_H
#define SIM_STM32F4XX_HAL_H

#include <stdint.h>
#include <stddef.h>

typedef enum { HAL_OK = 0, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET } GPIO_PinState;

typedef struct { int dummy; } GPIO_TypeDef;
typedef struct { int dummy; } SPI_HandleTypeDef;
typedef struct { int dummy; } I2C_HandleTypeDef;
typedef struct { uint16_t CNT; } TIM_Instance_t;
typedef struct { TIM_Instance_t *Instance; } TIM_HandleTypeDef;

#define HAL_MAX_DELAY 0xFFFFFFFFU
#define TIM_CHANNEL_ALL 0

#define GPIO_PIN_0  ((uint16_t)0x0001)
#define GPIO_PIN_1  ((uint16_t)0x0002)
#define GPIO_PIN_2  ((uint16_t)0x0004)
#define GPIO_PIN_8  ((uint16_t)0x0100)
#define GPIO_PIN_11 ((uint16_t)0x0800)
#define GPIO_PIN_12 ((uint16_t)0x1000)

uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t ms);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState st);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *h, uint8_t *d, uint16_t n, uint32_t t);
HAL_StatusTypeDef HAL_TIM_Encoder_Start(TIM_HandleTypeDef *h, uint32_t ch);
#define __HAL_TIM_GET_COUNTER(h) ((h)->Instance->CNT)

#endif
