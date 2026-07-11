#include "stm32_hal_stub.h"
#include "i2c_device_model.h"

// ---- GPIO instances (id matches index into gpio_pin_state[]) ----
static GPIO_TypeDef _GPIOA = {0};
static GPIO_TypeDef _GPIOB = {1};
static GPIO_TypeDef _GPIOC = {2};
static GPIO_TypeDef _GPIOD = {3};
GPIO_TypeDef *const GPIOA = &_GPIOA;
GPIO_TypeDef *const GPIOB = &_GPIOB;
GPIO_TypeDef *const GPIOC = &_GPIOC;
GPIO_TypeDef *const GPIOD = &_GPIOD;

// ---- HAL handle instances (declared extern in stubs/i2c.h and stubs/adc.h) ----
I2C_HandleTypeDef hi2c1 = {0};
I2C_HandleTypeDef hi2c3 = {0};
ADC_HandleTypeDef hadc1 = {0};

// ---- GPIO state and write log ----
static uint16_t gpio_pin_state[4] = {0};

GpioWriteEntry stub_gpio_write_log[GPIO_WRITE_LOG_SIZE];
int            stub_gpio_write_count = 0;

I2cWriteEntry  stub_i2c_write_log[I2C_WRITE_LOG_SIZE];
int            stub_i2c_write_count = 0;

// ---- ADC ----
uint32_t stub_adc_value = 0;

// ---- Tick ----
uint32_t stub_tick = 0;

// ---- I2C response queue ----
#define STUB_I2C_QUEUE_SIZE 64

typedef struct {
    uint8_t           data[16];
    uint16_t          len;
    HAL_StatusTypeDef status;
} StubI2CResponse;

static StubI2CResponse i2c_queue[STUB_I2C_QUEUE_SIZE];
static int i2c_head  = 0;
static int i2c_tail  = 0;
static int i2c_count = 0;

// ---- Helpers ----
static int pin_to_bit(uint16_t pin) {
    for (int i = 0; i < 16; i++)
        if (pin & (1u << i)) return i;
    return 0;
}

// ---- Stub control ----
void stub_reset(void) {
    memset(gpio_pin_state, 0, sizeof(gpio_pin_state));
    stub_gpio_write_count = 0;
    memset(stub_gpio_write_log, 0, sizeof(stub_gpio_write_log));
    stub_i2c_write_count = 0;
    memset(stub_i2c_write_log, 0, sizeof(stub_i2c_write_log));
    stub_adc_value = 0;
    stub_tick = 0;
    i2c_head = i2c_tail = i2c_count = 0;
    i2c_model_reset_all();
}

void stub_gpio_set(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state) {
    if (state == GPIO_PIN_SET)
        gpio_pin_state[port->id] |= pin;
    else
        gpio_pin_state[port->id] &= ~(uint16_t)pin;
}

void stub_i2c_queue(const uint8_t *data, uint16_t len, HAL_StatusTypeDef status) {
    if (i2c_count >= STUB_I2C_QUEUE_SIZE) return;
    StubI2CResponse *r = &i2c_queue[i2c_head];
    uint16_t clen = (len < 16) ? len : 16;
    memcpy(r->data, data, clen);
    r->len    = clen;
    r->status = status;
    i2c_head  = (i2c_head + 1) % STUB_I2C_QUEUE_SIZE;
    i2c_count++;
}

// ---- GPIO ----
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin) {
    (void)pin_to_bit; // suppress unused-function warning
    return ((gpio_pin_state[GPIOx->id] & GPIO_Pin) != 0) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState) {
    if (stub_gpio_write_count < GPIO_WRITE_LOG_SIZE)
        stub_gpio_write_log[stub_gpio_write_count++] =
            (GpioWriteEntry){GPIOx, GPIO_Pin, PinState};
    if (PinState == GPIO_PIN_SET)
        gpio_pin_state[GPIOx->id] |= GPIO_Pin;
    else
        gpio_pin_state[GPIOx->id] &= ~(uint16_t)GPIO_Pin;
}

// ---- I2C ----
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *hi2c, uint16_t DevAddress,
                                    uint16_t MemAddress, uint16_t MemAddSize,
                                    uint8_t *pData, uint16_t Size, uint32_t Timeout) {
    (void)hi2c; (void)MemAddSize; (void)Timeout;
    // Device model takes priority over the FIFO queue.
    I2cDeviceModel *dev = i2c_model_find(DevAddress);
    if (dev) return dev->on_read(MemAddress, pData, Size, dev->ctx);
    // Queue fallback — used by unmodelled devices (BQ34Z100, INA3221).
    if (i2c_count == 0) {
        memset(pData, 0, Size);
        return HAL_OK;
    }
    StubI2CResponse *r = &i2c_queue[i2c_tail];
    i2c_tail  = (i2c_tail + 1) % STUB_I2C_QUEUE_SIZE;
    i2c_count--;
    uint16_t clen = (r->len < Size) ? r->len : Size;
    memcpy(pData, r->data, clen);
    if (clen < Size) memset(pData + clen, 0, Size - clen);
    return r->status;
}

HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *hi2c, uint16_t DevAddress,
                                     uint16_t MemAddress, uint16_t MemAddSize,
                                     uint8_t *pData, uint16_t Size, uint32_t Timeout) {
    (void)hi2c; (void)MemAddSize; (void)Timeout;
    // Always log the write first.
    if (stub_i2c_write_count < I2C_WRITE_LOG_SIZE) {
        I2cWriteEntry *w = &stub_i2c_write_log[stub_i2c_write_count++];
        w->dev_addr = DevAddress;
        w->mem_addr = MemAddress;
        w->len      = (Size < 8) ? Size : 8;
        memcpy(w->data, pData, w->len);
    }
    // Forward to device model if registered (e.g. write-to-clear on INTR_REG).
    I2cDeviceModel *dev = i2c_model_find(DevAddress);
    if (dev) return dev->on_write(MemAddress, pData, Size, dev->ctx);
    return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *hi2c, uint16_t DevAddress,
                                          uint8_t *pData, uint16_t Size, uint32_t Timeout) {
    (void)hi2c; (void)Timeout;
    if (stub_i2c_write_count < I2C_WRITE_LOG_SIZE) {
        I2cWriteEntry *w = &stub_i2c_write_log[stub_i2c_write_count++];
        w->dev_addr = DevAddress;
        w->mem_addr = (Size > 0) ? pData[0] : 0;
        w->len      = (Size > 1) ? ((Size - 1 < 8) ? Size - 1 : 8) : 0;
        if (w->len) memcpy(w->data, pData + 1, w->len);
    }
    return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *hi2c, uint16_t DevAddress,
                                         uint8_t *pData, uint16_t Size, uint32_t Timeout) {
    (void)hi2c; (void)DevAddress; (void)Timeout;
    if (i2c_count == 0) {
        memset(pData, 0, Size);
        return HAL_OK;
    }
    StubI2CResponse *r = &i2c_queue[i2c_tail];
    i2c_tail  = (i2c_tail + 1) % STUB_I2C_QUEUE_SIZE;
    i2c_count--;
    uint16_t clen = (r->len < Size) ? r->len : Size;
    memcpy(pData, r->data, clen);
    if (clen < Size) memset(pData + clen, 0, Size - clen);
    return r->status;
}

// ---- ADC ----
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *h)                         { (void)h; return HAL_OK; }
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *h)                          { (void)h; return HAL_OK; }
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *h, uint32_t t) { (void)h; (void)t; return HAL_OK; }
uint32_t          HAL_ADC_GetValue(ADC_HandleTypeDef *h)                       { (void)h; return stub_adc_value; }

// ---- Display backlight (fsm.c calls it; real impl in ili9341.c, which the
// tests don't link). Mirrors the real polarity: active-low, JP402 PMOS. ----
void ILI9341_Backlight(uint8_t on) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

// ---- Misc ----
uint32_t HAL_GetTick(void)                                           { return stub_tick; }
void HAL_SuspendTick(void)                                           {}
void HAL_ResumeTick(void)                                            {}
void HAL_Delay(uint32_t d)                                           { (void)d; }
void HAL_PWR_EnterSTOPMode(uint32_t r, uint8_t e)                   { (void)r; (void)e; }
void SystemClock_Config(void)                                        {}
void Error_Handler(void)                                             {}
