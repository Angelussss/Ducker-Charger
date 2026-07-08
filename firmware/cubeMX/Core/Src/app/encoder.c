/**
 * @file    encoder.c
 * @brief   Rotary encoder + push-button via TIM3 encoder mode.
 *
 * EC11E15244G1 gives 2 counts/detent in TI12 (x4) mode — divide by 2.
 * Requires .ioc: TIM3 EncoderMode = TIM_ENCODERMODE_TI12.
 *
 * The push-button (PC0) is edge-driven via EXTI0: the .ioc still declares
 * PC0 as plain GPIO_Input, Encoder_Init() re-configures it as an interrupt
 * source at runtime so the generated MX_GPIO_Init() never needs touching.
 * The EXTI0_IRQHandler lives in the USER CODE section of stm32f4xx_it.c.
 */

#include "app/encoder.h"
#include "main.h"   /* ENCOD_BUTT_Pin, ENCOD_BUTT_GPIO_Port */

static TIM_HandleTypeDef *_htim = NULL;
static uint16_t  _last_count          = 0;
static volatile uint8_t   _btn_last_state       = 1;  /* active-low: 1 = idle */
static volatile uint32_t  _btn_last_change_tick = 0;
static volatile uint8_t   _btn_pressed_flag     = 0;
static volatile uint32_t  _last_activity_tick   = 0;

void Encoder_Init(TIM_HandleTypeDef *htim_ptr)
{
    _htim = htim_ptr;
    HAL_TIM_Encoder_Start(_htim, TIM_CHANNEL_ALL);
    _last_count        = (uint16_t)__HAL_TIM_GET_COUNTER(_htim);
    _btn_last_state    = 1;
    _btn_pressed_flag  = 0;
    _last_activity_tick = HAL_GetTick();

    /* Override MX_GPIO_Init(): PC0 becomes an EXTI line on both edges.
     * Both edges are needed so the debounce timestamp also tracks the
     * release — otherwise release bounce would register as a new press. */
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin  = ENCOD_BUTT_Pin;
    gpio.Mode = GPIO_MODE_IT_RISING_FALLING;
    gpio.Pull = GPIO_NOPULL;   /* external pull-up on board */
    HAL_GPIO_Init(ENCOD_BUTT_GPIO_Port, &gpio);

    /* Numerically higher than SysTick (0) so HAL_GetTick() stays valid
     * inside the callback. */
    HAL_NVIC_SetPriority(EXTI0_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);
}

int8_t Encoder_GetDelta(void)
{
    uint16_t current_count = (uint16_t)__HAL_TIM_GET_COUNTER(_htim);
    int16_t  delta         = (int16_t)(current_count - _last_count);
    _last_count = current_count;
    if (delta != 0)
        _last_activity_tick = HAL_GetTick();
    return (int8_t)(delta / 2);
}

/**
 * EXTI callback, invoked in interrupt context on every PC0 edge.
 * Same debounce state machine as the old polled version, just event-driven:
 * edges arriving within ENCODER_DEBOUNCE_MS of the last accepted transition
 * are ignored, a debounced falling edge latches the pressed flag.
 *
 * HAL defines this callback as weak; if another EXTI line needs a handler
 * later, add its branch here.
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin != ENCOD_BUTT_Pin)
        return;

    uint32_t now           = HAL_GetTick();
    uint8_t  current_state = HAL_GPIO_ReadPin(ENCOD_BUTT_GPIO_Port, ENCOD_BUTT_Pin);

    if (current_state != _btn_last_state)
    {
        if ((now - _btn_last_change_tick) > ENCODER_DEBOUNCE_MS)
        {
            _btn_last_change_tick = now;
            _btn_last_state       = current_state;
            _last_activity_tick   = now;
            if (current_state == 0)  /* falling edge = pressed */
                _btn_pressed_flag = 1;
        }
    }
}

uint8_t Encoder_IsPressed(void)
{
    if (_btn_pressed_flag)
    {
        _btn_pressed_flag = 0;
        return 1;
    }
    return 0;
}

uint8_t Encoder_IsHeld(void)
{
    return (HAL_GPIO_ReadPin(ENCOD_BUTT_GPIO_Port, ENCOD_BUTT_Pin)
            == GPIO_PIN_RESET) ? 1 : 0;
}

uint32_t Encoder_LastActivityTick(void)
{
    return _last_activity_tick;
}
