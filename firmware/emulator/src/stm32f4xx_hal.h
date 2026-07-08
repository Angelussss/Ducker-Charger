/**
 * @file    stm32f4xx_hal.h  (EMULATOR fake)
 * @brief   Host-native replacement for the STM32F4 HAL.
 *
 * Shadows the real HAL header (-Isrc comes first in the Makefile) so the
 * unmodified firmware sources compile on the host. Every HAL entry point
 * the firmware uses is routed by hal_impl.c into the board / IC models:
 *
 *   HAL_I2C_*   -> sim_bus dispatch -> dev_bq34z100 / dev_tps25750 /
 *                  dev_cypd3175 / dev_stpd01 / dev_ina3221
 *   HAL_ADC_*   -> board model (NTC zones, currents, system power)
 *   HAL_SPI_*   -> dev_ili9341 panel model (command/pixel stream decode)
 *   HAL_GPIO_*  -> pin table shared with the board model + EXTI emulation
 *   Tick/Delay  -> host monotonic clock
 *   STOP mode   -> blocks the firmware thread until a wake edge
 */
#ifndef SIM_STM32F4XX_HAL_H
#define SIM_STM32F4XX_HAL_H

#include <stdint.h>
#include <stddef.h>

/* ---------------- Status / GPIO primitives ---------------- */

typedef enum { HAL_OK = 0, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET } GPIO_PinState;

typedef struct { int idx; } GPIO_TypeDef;   /* idx: 0=A 1=B 2=C */
extern GPIO_TypeDef sim_GPIOA, sim_GPIOB, sim_GPIOC;
#define GPIOA (&sim_GPIOA)
#define GPIOB (&sim_GPIOB)
#define GPIOC (&sim_GPIOC)

#define GPIO_PIN_0   ((uint16_t)0x0001)
#define GPIO_PIN_1   ((uint16_t)0x0002)
#define GPIO_PIN_2   ((uint16_t)0x0004)
#define GPIO_PIN_3   ((uint16_t)0x0008)
#define GPIO_PIN_4   ((uint16_t)0x0010)
#define GPIO_PIN_5   ((uint16_t)0x0020)
#define GPIO_PIN_6   ((uint16_t)0x0040)
#define GPIO_PIN_7   ((uint16_t)0x0080)
#define GPIO_PIN_8   ((uint16_t)0x0100)
#define GPIO_PIN_9   ((uint16_t)0x0200)
#define GPIO_PIN_10  ((uint16_t)0x0400)
#define GPIO_PIN_11  ((uint16_t)0x0800)
#define GPIO_PIN_12  ((uint16_t)0x1000)
#define GPIO_PIN_13  ((uint16_t)0x2000)
#define GPIO_PIN_14  ((uint16_t)0x4000)
#define GPIO_PIN_15  ((uint16_t)0x8000)
#define GPIO_PIN_All ((uint16_t)0xFFFF)

typedef struct {
    uint32_t Pin;
    uint32_t Mode;
    uint32_t Pull;
    uint32_t Speed;
    uint32_t Alternate;
} GPIO_InitTypeDef;

#define GPIO_MODE_INPUT              0x00000000u
#define GPIO_MODE_OUTPUT_PP          0x00000001u
#define GPIO_MODE_OUTPUT_OD          0x00000011u
#define GPIO_MODE_IT_RISING          0x10110000u
#define GPIO_MODE_IT_FALLING         0x10210000u
#define GPIO_MODE_IT_RISING_FALLING  0x10310000u
#define GPIO_NOPULL                  0x00000000u
#define GPIO_PULLUP                  0x00000001u
#define GPIO_PULLDOWN                0x00000002u
#define GPIO_SPEED_FREQ_LOW          0x00000000u
#define GPIO_SPEED_FREQ_MEDIUM       0x00000001u
#define GPIO_SPEED_FREQ_HIGH         0x00000002u

/* ---------------- Peripheral handles ---------------- */

typedef struct { int bus; }  I2C_HandleTypeDef;   /* bus: 1 or 3 */
typedef struct { int id; }   SPI_HandleTypeDef;
typedef struct { int cur; }  ADC_HandleTypeDef;   /* scan-sequence rank */
typedef struct { int id; }   UART_HandleTypeDef;

typedef struct { volatile uint16_t CNT; } TIM_TypeDef;
typedef struct { TIM_TypeDef *Instance; } TIM_HandleTypeDef;

#define HAL_MAX_DELAY          0xFFFFFFFFu
#define TIM_CHANNEL_ALL        0x0000003Cu
#define I2C_MEMADD_SIZE_8BIT   0x00000001u
#define I2C_MEMADD_SIZE_16BIT  0x00000002u

typedef enum { SysTick_IRQn = -1, EXTI0_IRQn = 6 } IRQn_Type;

/* ---------------- RCC / PWR (accept-all stubs) ---------------- */

typedef struct {
    uint32_t PLLState, PLLSource, PLLM, PLLN, PLLP, PLLQ;
} RCC_PLLInitTypeDef;

typedef struct {
    uint32_t OscillatorType, HSEState, HSIState, LSEState, LSIState,
             HSICalibrationValue;
    RCC_PLLInitTypeDef PLL;
} RCC_OscInitTypeDef;

typedef struct {
    uint32_t ClockType, SYSCLKSource, AHBCLKDivider,
             APB1CLKDivider, APB2CLKDivider;
} RCC_ClkInitTypeDef;

#define RCC_OSCILLATORTYPE_HSE   0x1u
#define RCC_HSE_ON               0x1u
#define RCC_PLL_ON               0x2u
#define RCC_PLLSOURCE_HSE        0x1u
#define RCC_PLLP_DIV2            0x2u
#define RCC_CLOCKTYPE_HCLK       0x2u
#define RCC_CLOCKTYPE_SYSCLK     0x1u
#define RCC_CLOCKTYPE_PCLK1      0x4u
#define RCC_CLOCKTYPE_PCLK2      0x8u
#define RCC_SYSCLKSOURCE_PLLCLK  0x2u
#define RCC_SYSCLK_DIV1          0x0u
#define RCC_HCLK_DIV1            0x0u
#define RCC_HCLK_DIV2            0x1u
#define FLASH_LATENCY_2          0x2u

#define PWR_REGULATOR_VOLTAGE_SCALE2  0x2u
#define PWR_LOWPOWERREGULATOR_ON      0x1u
#define PWR_STOPENTRY_WFI             0x1u

#define __HAL_RCC_PWR_CLK_ENABLE()          do {} while (0)
#define __HAL_PWR_VOLTAGESCALING_CONFIG(x)  do {} while (0)
#define __disable_irq()                     do {} while (0)
#define __enable_irq()                      do {} while (0)

#define __HAL_TIM_GET_COUNTER(h)  ((h)->Instance->CNT)

/* ---------------- Prototypes ---------------- */

HAL_StatusTypeDef HAL_Init(void);
uint32_t HAL_GetTick(void);
void     HAL_Delay(uint32_t ms);
void     HAL_SuspendTick(void);
void     HAL_ResumeTick(void);

HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef *cfg);
HAL_StatusTypeDef HAL_RCC_ClockConfig(RCC_ClkInitTypeDef *cfg, uint32_t latency);
void HAL_PWR_EnterSTOPMode(uint32_t regulator, uint32_t entry);

void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState st);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
void HAL_GPIO_EXTI_IRQHandler(uint16_t pin);
void HAL_GPIO_EXTI_Callback(uint16_t pin);

void HAL_NVIC_SetPriority(IRQn_Type irq, uint32_t prio, uint32_t sub);
void HAL_NVIC_EnableIRQ(IRQn_Type irq);

HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *h);
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *h);
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *h, uint32_t timeout);
uint32_t          HAL_ADC_GetValue(ADC_HandleTypeDef *h);

HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *h, uint16_t addr,
                                   uint16_t mem, uint16_t memsize,
                                   uint8_t *buf, uint16_t n, uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *h, uint16_t addr,
                                    uint16_t mem, uint16_t memsize,
                                    uint8_t *buf, uint16_t n, uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *h, uint16_t addr,
                                          uint8_t *buf, uint16_t n, uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *h, uint16_t addr,
                                         uint8_t *buf, uint16_t n, uint32_t timeout);

HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *h, uint8_t *d,
                                   uint16_t n, uint32_t timeout);

HAL_StatusTypeDef HAL_TIM_Encoder_Start(TIM_HandleTypeDef *h, uint32_t ch);

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *h, uint8_t *d,
                                    uint16_t n, uint32_t timeout);

#endif /* SIM_STM32F4XX_HAL_H */
