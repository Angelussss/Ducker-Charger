/**
 * @file    encoder.c
 * @brief   Rotary encoder with push-button implementation.
 *
 * HOW TIM3 ENCODER MODE WORKS / COME FUNZIONA L'ENCODER MODE DI TIM3:
 *   In encoder mode the timer does not count time but counts the edges
 *   of the encoder's A and B signals. The counter value (TIM3->CNT)
 *   increments when rotating right and decrements when rotating left.
 *   Each physical "click" of the encoder produces typically 4 edges
 *   (this depends on the encoder resolution; the EC11 uses 4 edges/click).
 *
 *   Advantage over interrupts: the hardware handles quadrature decoding and
 *   bounce filtering automatically — zero CPU wasted.
 *
 * BUTTON DEBOUNCE:
 *   Mechanical buttons "bounce": pressing once causes the signal to toggle
 *   several times in a few milliseconds before settling. Debounce ignores
 *   transitions that are too close together in time.
 */

#include "app/encoder.h"
#include "main.h"   /* ENCOD_BUTT_Pin, ENCOD_BUTT_GPIO_Port */

/* =========================================================
 * STATIC (FILE-PRIVATE) VARIABLES 
 * ========================================================= */

/** Timer handle pointer, saved during Encoder_Init(). */
static TIM_HandleTypeDef *_htim = NULL;

/** Previous counter value, used to compute the delta. */
static uint16_t _last_count = 0;

/** Previous button state (for falling-edge detection).
 *  1 = not pressed (pull-up active). Active-low: 0 means pressed. */
static uint8_t _btn_last_state = 1;

/** Tick of the last button state change (for debounce). */
static uint32_t _btn_last_change_tick = 0;

/** "Pressed" flag waiting to be consumed by Encoder_IsPressed(). */
static uint8_t _btn_pressed_flag = 0;

/* =========================================================
 * PUBLIC FUNCTIONS
 * ========================================================= */

void Encoder_Init(TIM_HandleTypeDef *htim_ptr)
{
    _htim = htim_ptr;

    /* Start the timer in encoder mode.
     * HAL_TIM_Encoder_Start() sets up the timer to count edges on both
     * channels (CH1 and CH2) simultaneously. */
    HAL_TIM_Encoder_Start(_htim, TIM_CHANNEL_ALL);

    /* Read the initial counter value as the reference point. */
    _last_count = (uint16_t)__HAL_TIM_GET_COUNTER(_htim);

    _btn_last_state    = 1;
    _btn_pressed_flag  = 0;
}

int8_t Encoder_GetDelta(void)
{
    /* Read the current hardware counter value. */
    uint16_t current_count = (uint16_t)__HAL_TIM_GET_COUNTER(_htim);

    /* Calculate how much it changed since the last read.*/
    int16_t delta = (int16_t)(current_count - _last_count);

    /* Save the current value for the next call. */
    _last_count = current_count;

    /* Divide by 4: our encoder produces 4 edges per physical click.
     * Dividing by 4 converts raw edges into actual user clicks.
     * Uses integer division (truncation toward zero).*/
    return (int8_t)(delta / 4);
}

uint8_t Encoder_IsPressed(void)
{
    uint32_t now = HAL_GetTick();

    /* Read the current state of the button pin.
     * Active-low: GPIO_PIN_RESET (0) means the button IS pressed. */
    uint8_t current_state = HAL_GPIO_ReadPin(ENCOD_BUTT_GPIO_Port, ENCOD_BUTT_Pin);

    /* Debounce: ignore transitions that are too close to the last one.
     * If the signal changed but less than ENCODER_DEBOUNCE_MS have passed
     * since the last change, it is probably a bounce — discard it. */
    if (current_state != _btn_last_state)
    {
        if ((now - _btn_last_change_tick) > ENCODER_DEBOUNCE_MS)
        {
            _btn_last_change_tick = now;
            _btn_last_state = current_state;

            /* Detect falling edge only (0 = pressed).
             * This fires the event when the button is PRESSED, not released. */
            if (current_state == 0)
                _btn_pressed_flag = 1;
        }
    }

    /* If there is a pending event, consume it and return 1. */
    if (_btn_pressed_flag)
    {
        _btn_pressed_flag = 0;
        return 1;
    }

    return 0;
}

uint8_t Encoder_IsHeld(void)
{
    /* Read the physical pin state directly without consuming any event.
     * The button is active-low (0 = pressed), so we invert: return 1 when
     * the pin is low (button is physically pressed down).*/
     
    return (HAL_GPIO_ReadPin(ENCOD_BUTT_GPIO_Port, ENCOD_BUTT_Pin)
            == GPIO_PIN_RESET) ? 1 : 0;
}
