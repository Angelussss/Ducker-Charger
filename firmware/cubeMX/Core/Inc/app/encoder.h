/**
 * @file    encoder.h
 * @brief   Rotary encoder with push-button management (EC11 or equivalent).
 *          Gestione del rotary encoder con pulsante (EC11 o equivalente).
 *
 * A rotary encoder produces three signals:
 * Un rotary encoder produce tre segnali:
 *   - A and B: two quadrature pulses that indicate direction and speed.
 *              Due impulsi in quadratura che indicano direzione e velocita'.
 *   - SW:      push-button (axis press). / Pulsante (pressione dell'asse).
 *
 * HARDWARE CONNECTIONS / CONNESSIONI HARDWARE (from main.h / da main.h):
 *   Button / Pulsante --> PC0  (ENCOD_BUTT_Pin)
 *   Channel A         --> TIM3 CH1 (PA6)  [handled by HAL_TIM encoder mode]
 *   Channel B         --> TIM3 CH2 (PB5)  [handled by HAL_TIM encoder mode]
 *
 * HOW HARDWARE ENCODER MODE WORKS / COME FUNZIONA L'ENCODER MODE HARDWARE:
 *   TIM3 is configured in "Encoder Mode" by CubeMX. This means the timer
 *   hardware automatically counts the edges of signals A and B.
 *   No interrupts are needed: just read TIM3->CNT to know how much the
 *   encoder has been rotated. This is the most efficient approach possible.
 *
 *   TIM3 e' configurato in "Encoder Mode" da CubeMX. L'hardware del timer
 *   conta automaticamente i fronti dei segnali A e B.
 *   Non serve alcun interrupt: basta leggere TIM3->CNT per sapere quanto
 *   e' stato ruotato. E' il modo piu' efficiente possibile.
 *
 * TYPICAL USE / USO TIPICO:
 *   // In main(), after MX_TIM3_Init() / In main(), dopo MX_TIM3_Init():
 *   Encoder_Init(&htim3);
 *
 *   // In the loop / Nel loop:
 *   int8_t delta = Encoder_GetDelta();
 *   if (delta > 0) { // rotated right / ruotato a destra }
 *   if (delta < 0) { // rotated left  / ruotato a sinistra }
 *   if (Encoder_IsPressed()) { // button pressed / bottone premuto }
 */

#ifndef __ENCODER_H
#define __ENCODER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

/* =========================================================
 * CONFIGURATION / CONFIGURAZIONE
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
 * @brief  Initialise the encoder module.
 *         Inizializza il modulo encoder.
 * @note   Call in main() after MX_TIM3_Init(). Starts the timer in encoder
 *         mode and resets the counter to zero.
 *         Chiamare in main() dopo MX_TIM3_Init(). Avvia il timer in modalita'
 *         encoder e azzera il contatore.
 * @param  htim_ptr  Pointer to the timer handle used for the encoder (htim3).
 *                   Puntatore all'handle del timer usato per l'encoder (htim3).
 */
void Encoder_Init(TIM_HandleTypeDef *htim_ptr);

/**
 * @brief  Read how much the encoder has been rotated since the last call.
 *         Legge di quanto e' stato ruotato l'encoder dall'ultima chiamata.
 * @note   Each call "consumes" the movement: calling twice in a row without
 *         any rotation between them returns 0 on the second call.
 *         Ogni chiamata "consuma" il movimento: chiamare due volte di fila
 *         senza rotazioni nel mezzo restituisce 0 alla seconda chiamata.
 * @retval Number of clicks: positive = right, negative = left, 0 = still.
 *         Numero di click: positivo = destra, negativo = sinistra, 0 = fermo.
 *
 * Example / Esempio:
 *   int8_t d = Encoder_GetDelta();
 *   if (d > 0) menu_index++;
 *   if (d < 0) menu_index--;
 */
int8_t Encoder_GetDelta(void);

/**
 * @brief  Check whether the encoder push-button has been pressed.
 *         Controlla se il pulsante dell'encoder e' stato premuto.
 * @note   Debounce is handled internally. Returns 1 only once per press
 *         (falling-edge detection, not level detection).
 *         Uses PC0 (ENCOD_BUTT_Pin on GPIOC).
 *         Il debounce e' gestito internamente. Restituisce 1 solo una volta
 *         per pressione (rilevamento fronte, non livello).
 *         Usa PC0 (ENCOD_BUTT_Pin su GPIOC).
 * @retval 1 if the button was pressed since the last call, 0 otherwise.
 *         1 se il pulsante e' stato premuto dall'ultima chiamata, 0 altrimenti.
 */
uint8_t Encoder_IsPressed(void);

/**
 * @brief  Check whether the button is currently held down continuously.
 *         Controlla se il pulsante e' tenuto premuto in modo continuativo.
 * @note   Unlike Encoder_IsPressed(), this does NOT consume the event.
 *         Useful for "hold for N seconds" actions.
 *         A differenza di Encoder_IsPressed(), non consuma l'evento.
 *         Utile per azioni "tieni premuto per N secondi".
 * @retval 1 if the button is physically pressed right now, 0 otherwise.
 *         1 se il pulsante e' fisicamente premuto in questo momento, 0 altrimenti.
 */
uint8_t Encoder_IsHeld(void);

#ifdef __cplusplus
}
#endif

#endif /* __ENCODER_H */
