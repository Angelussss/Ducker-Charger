#ifndef STM32_HAL_STUB_H
#define STM32_HAL_STUB_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// ---- HAL status ----
typedef enum {
    HAL_OK      = 0x00,
    HAL_ERROR   = 0x01,
    HAL_BUSY    = 0x02,
    HAL_TIMEOUT = 0x03
} HAL_StatusTypeDef;

// ---- GPIO ----
typedef enum {
    GPIO_PIN_RESET = 0,
    GPIO_PIN_SET
} GPIO_PinState;

#define GPIO_PIN_0  ((uint16_t)0x0001)
#define GPIO_PIN_1  ((uint16_t)0x0002)
#define GPIO_PIN_2  ((uint16_t)0x0004)
#define GPIO_PIN_3  ((uint16_t)0x0008)
#define GPIO_PIN_4  ((uint16_t)0x0010)
#define GPIO_PIN_5  ((uint16_t)0x0020)
#define GPIO_PIN_6  ((uint16_t)0x0040)
#define GPIO_PIN_7  ((uint16_t)0x0080)
#define GPIO_PIN_8  ((uint16_t)0x0100)
#define GPIO_PIN_9  ((uint16_t)0x0200)
#define GPIO_PIN_10 ((uint16_t)0x0400)
#define GPIO_PIN_11 ((uint16_t)0x0800)
#define GPIO_PIN_12 ((uint16_t)0x1000)
#define GPIO_PIN_13 ((uint16_t)0x2000)
#define GPIO_PIN_14 ((uint16_t)0x4000)
#define GPIO_PIN_15 ((uint16_t)0x8000)

typedef struct { uint8_t id; } GPIO_TypeDef;

extern GPIO_TypeDef *const GPIOA;
extern GPIO_TypeDef *const GPIOB;
extern GPIO_TypeDef *const GPIOC;
extern GPIO_TypeDef *const GPIOD;

// ---- I2C ----
#define I2C_MEMADD_SIZE_8BIT  1U
#define I2C_MEMADD_SIZE_16BIT 2U
#define HAL_MAX_DELAY         0xFFFFFFFFU

typedef struct { uint8_t dummy; } I2C_HandleTypeDef;

// ---- ADC ----
typedef struct { uint8_t dummy; } ADC_HandleTypeDef;

// ---- SPI (display driver header pulls this in via fsm.c) ----
typedef struct { uint8_t dummy; } SPI_HandleTypeDef;

// ---- PWR ----
#define PWR_LOWPOWERREGULATOR_ON 0x00000001U
#define PWR_STOPENTRY_WFI        0x01U

// ---- HAL API ----
GPIO_PinState     HAL_GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);
void              HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState);

HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *hi2c, uint16_t DevAddress,
                                    uint16_t MemAddress, uint16_t MemAddSize,
                                    uint8_t *pData, uint16_t Size, uint32_t Timeout);
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *hi2c, uint16_t DevAddress,
                                     uint16_t MemAddress, uint16_t MemAddSize,
                                     uint8_t *pData, uint16_t Size, uint32_t Timeout);

// Raw transactions (used by the TPS25750 length-prefixed host interface).
// Transmit is logged like Mem_Write with mem_addr = pData[0]; Receive pops
// the same response queue as Mem_Read (zeros + HAL_OK when empty).
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *hi2c, uint16_t DevAddress,
                                          uint8_t *pData, uint16_t Size, uint32_t Timeout);
HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *hi2c, uint16_t DevAddress,
                                         uint8_t *pData, uint16_t Size, uint32_t Timeout);

HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *hadc);
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *hadc);
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *hadc, uint32_t Timeout);
uint32_t          HAL_ADC_GetValue(ADC_HandleTypeDef *hadc);

void HAL_SuspendTick(void);
void HAL_ResumeTick(void);
void HAL_Delay(uint32_t Delay);
void HAL_PWR_EnterSTOPMode(uint32_t Regulator, uint8_t STOPEntry);
uint32_t HAL_GetTick(void);

// Simulated tick counter returned by HAL_GetTick; tests advance it by hand
// (e.g. to step past debounce/grace windows). Reset to 0 by stub_reset().
extern uint32_t stub_tick;

// ---- Stub control API ----

// Reset all stub state (GPIO, write log, I2C queue, ADC value).
void stub_reset(void);

// Set a GPIO input pin state (simulates hardware changing a pin before the code reads it).
void stub_gpio_set(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);

// Queue one I2C read response. The next HAL_I2C_Mem_Read call pops this entry.
// If the queue is empty, Mem_Read returns zeros + HAL_OK.
void stub_i2c_queue(const uint8_t *data, uint16_t len, HAL_StatusTypeDef status);

// Configure the value returned by every HAL_ADC_GetValue call.
extern uint32_t stub_adc_value;

// GPIO write log: each HAL_GPIO_WritePin call appends here.
typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    GPIO_PinState state;
} GpioWriteEntry;

#define GPIO_WRITE_LOG_SIZE 64
extern GpioWriteEntry stub_gpio_write_log[GPIO_WRITE_LOG_SIZE];
extern int            stub_gpio_write_count;

// I2C write log: each HAL_I2C_Mem_Write call appends here.
typedef struct {
    uint16_t dev_addr;
    uint16_t mem_addr;
    uint8_t  data[8];
    uint16_t len;
} I2cWriteEntry;

#define I2C_WRITE_LOG_SIZE 64
extern I2cWriteEntry stub_i2c_write_log[I2C_WRITE_LOG_SIZE];
extern int           stub_i2c_write_count;

#endif // STM32_HAL_STUB_H
