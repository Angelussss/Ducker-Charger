/**
 * @file    encoder.h
 * @brief   Rotary encoder with push-button management (EC11 or equivalent). [Gestione del rotary encoder con pulsante (EC11 o equivalente). ]
 *
 * A rotary encoder produces three signals:
 *   - A and B: two quadrature pulses that indicate direction and speed. [Due impulsi in quadratura che indicano direzione e velocita']
 *   - SW:      push-button (axis press). [Pulsante (pressione dell'asse)]
 *
 * HARDWARE CONNECTIONS / CONNESSIONI HARDWARE (from main.h / da main.h):
 *   Button            --> PC0  (ENCOD_BUTT_Pin)
 *   Channel A         --> TIM3 CH1 (PA6)  [handled by HAL_TIM encoder mode]
 *   Channel B         --> TIM3 CH2 (PB5)  [handled by HAL_TIM encoder mode]
 *
 * HOW HARDWARE ENCODER MODE WORKS / COME FUNZIONA L'ENCODER MODE HARDWARE:
 *   TIM3 is configured in "Encoder Mode" by CubeMX. This means the timer
 *   hardware automatically counts the edges [fronti] of signals A and B.
 *   No interrupts are needed: just read TIM3->CNT to know how much the
 *   encoder has been rotated. This is the most efficient approach possible.
 *
 * TYPICAL USE / USO TIPICO:
 *   // In main(), after MX_TIM3_Init()
 *   Encoder_Init(&htim3);
 *
 *   // In the loop
 *   int8_t delta = Encoder_GetDelta();
 *   if (delta > 0) { // rotated right / ruotato a destra }
 *   if (delta < 0) { // rotated left  / ruotato a sinistra }
 *   if (Encoder_IsPressed()) { // button pressed }
 */

#ifndef __ENCODER_H
#define __ENCODER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

/* =========================================================
 * CONFIGURATION
 * ========================================================= */

/**
 * Button debounce time in milliseconds.
 * Below this threshold mechanical bounces of the button are ignored.
 * 50 ms is a standard value for most low-cost encoders.
 *
 * Tempo di debounce per il pulsante in millisecondi.
 * Sotto questa soglia i bounce meccanici del pulsante vengono ignorati.
 * 50ms e' un valore standard per la maggior parte degli encoder economici.
 */
#define ENCODER_DEBOUNCE_MS  50

/* =========================================================
 * PUBLIC API / API PUBBLICA
 * ========================================================= */

/**
 * @brief  Initialise the encoder module. [Inizializza il modulo encoder.]
 * @note   Call in main() after MX_TIM3_Init(). Starts the timer in encoder
 *         mode and resets the counter to zero.
 * @param  htim_ptr  Pointer to the timer handle used for the encoder (htim3).
 *                   Puntatore all'handle del timer usato per l'encoder (htim3).
 */
void Encoder_Init(TIM_HandleTypeDef *htim_ptr);

/**
 * @brief  Read how much the encoder has been rotated since the last call.
 * @note   Each call "consumes" the movement: calling twice in a row without
 *         any rotation between them returns 0 on the second call.
 * @retval Number of clicks: positive = right, negative = left, 0 = still.
 *
 * Example:
 *   int8_t d = Encoder_GetDelta();
 *   if (d > 0) menu_index++;
 *   if (d < 0) menu_index--;
 */
int8_t Encoder_GetDelta(void);

/**
 * @brief  Check whether the encoder push-button has been pressed.
 * @note   Debounce is handled internally. Returns 1 only once per press
 *         (falling-edge detection, not level detection).
 *         Uses PC0 (ENCOD_BUTT_Pin on GPIOC).
 * @retval 1 if the button was pressed since the last call, 0 otherwise.
 */
uint8_t Encoder_IsPressed(void);

/**
 * @brief  Check whether the button is currently held down continuously.
ù * @note   Unlike Encoder_IsPressed(), this does NOT consume the event.
 *         Useful for "hold for N seconds" actions.
 * @retval 1 if the button is physically pressed right now, 0 otherwise.
 *         1 se il pulsante e' fisicamente premuto in questo momento, 0 altrimenti.
 */
uint8_t Encoder_IsHeld(void);

#ifdef __cplusplus
}
#endif

#endif /* __ENCODER_H */
