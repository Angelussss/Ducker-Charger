/**
 * @file    encoder.c
 * @brief   Rotary encoder + push-button via TIM3 encoder mode.
 *
 * EC11E15244G1 gives 2 counts/detent in TI12 (x4) mode — divide by 2.
 * Requires .ioc: TIM3 EncoderMode = TIM_ENCODERMODE_TI12.
 */

#include "app/encoder.h"
#include "main.h"   /* ENCOD_BUTT_Pin, ENCOD_BUTT_GPIO_Port */

static TIM_HandleTypeDef *_htim = NULL;
static uint16_t  _last_count          = 0;
static uint8_t   _btn_last_state      = 1;  /* active-low: 1 = idle */
static uint32_t  _btn_last_change_tick = 0;
static uint8_t   _btn_pressed_flag    = 0;

void Encoder_Init(TIM_HandleTypeDef *htim_ptr)
{
    _htim = htim_ptr;
    HAL_TIM_Encoder_Start(_htim, TIM_CHANNEL_ALL);
    _last_count        = (uint16_t)__HAL_TIM_GET_COUNTER(_htim);
    _btn_last_state    = 1;
    _btn_pressed_flag  = 0;
}

int8_t Encoder_GetDelta(void)
{
    uint16_t current_count = (uint16_t)__HAL_TIM_GET_COUNTER(_htim);
    int16_t  delta         = (int16_t)(current_count - _last_count);
    _last_count = current_count;
    return (int8_t)(delta / 2);
}

uint8_t Encoder_IsPressed(void)
{
    uint32_t now           = HAL_GetTick();
    uint8_t  current_state = HAL_GPIO_ReadPin(ENCOD_BUTT_GPIO_Port, ENCOD_BUTT_Pin);

    if (current_state != _btn_last_state)
    {
        if ((now - _btn_last_change_tick) > ENCODER_DEBOUNCE_MS)
        {
            _btn_last_change_tick = now;
            _btn_last_state       = current_state;
            if (current_state == 0)  /* falling edge = pressed */
                _btn_pressed_flag = 1;
        }
    }

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
