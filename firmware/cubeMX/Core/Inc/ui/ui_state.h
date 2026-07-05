/**
 * @file    ui_state.h
 * @brief   State machine for navigation between UI screens.
 *          Macchina a stati per la navigazione tra le schermate UI.
 *
 * This module manages WHICH screen is active and WHEN to transition to
 * another one. It is the "brain" of the user interface.
 * Questo modulo gestisce QUALE schermata e' attiva e QUANDO passare
 * a un'altra. E' il "cervello" dell'interfaccia utente.
 *
 * NAVIGATION / NAVIGAZIONE:
 *   Rotate encoder RIGHT / LEFT --> navigate menu / change value /
 *                                    naviga nel menu / cambia valore
 *   Short press (click)         --> enter screen / confirm /
 *                                    entra nella schermata / conferma
 *   Long press (hold 1 s)       --> return to main screen from anywhere /
 *                                    torna alla schermata principale da qualsiasi punto
 *
 * STATE MACHINE / MACCHINA A STATI:
 *
 *              [BOOT]
 *                 |  (auto after 1.5 s / automatico dopo 1.5s)
 *                 v
 *   [MAIN] <----> [DETAIL] <----> [GRAPH] <----> [SETTINGS]
 *     ^                                               |
 *     |_______________(long press anywhere / long press ovunque)__|
 *
 * UPDATE FREQUENCY / FREQUENZA DI AGGIORNAMENTO:
 *   UI_Tick() must be called in the main loop. Internally it manages two
 *   independent timers:
 *   UI_Tick() va chiamata nel loop principale. Internamente gestisce due timer indipendenti:
 *   - Sensor poll:  every TELEMETRY_POLL_INTERVAL_MS (500 ms)
 *   - UI refresh:   every UI_REFRESH_INTERVAL_MS (250 ms = 4 fps)
 *   4 fps is sufficient for slowly-changing numbers and leaves plenty of
 *   CPU time for I2C polling.
 *   4 fps e' sufficiente per numeri che cambiano lentamente e lascia tempo alla CPU per I2C.
 */

#ifndef __UI_STATE_H
#define __UI_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

/* =========================================================
 * CONFIGURATION / CONFIGURAZIONE
 * ========================================================= */

/**
 * How often to refresh the displayed values, in milliseconds.
 * Ogni quanti millisecondi aggiornare i valori a schermo.
 */
#define UI_REFRESH_INTERVAL_MS  250

/**
 * How long the button must be held to trigger a "go home" action, in ms.
 * Quanto a lungo tenere premuto il bottone (ms) per tornare alla schermata home.
 */
#define UI_LONG_PRESS_MS        1000

/* =========================================================
 * AVAILABLE SCREENS / SCHERMATE DISPONIBILI
 * ========================================================= */

/**
 * @brief  Identifiers for the application screens.
 *         Identificatori delle schermate dell'applicazione.
 *
 * The order reflects the horizontal navigation with the encoder.
 * L'ordine rispecchia la navigazione orizzontale con l'encoder.
 */
typedef enum {
    UI_SCREEN_BOOT = 0,   /**< Splash screen, not navigable / Splash screen, non navigabile */
    UI_SCREEN_MAIN,       /**< Home: large SoC + current + graph / Home: SoC grande + corrente + grafico */
    UI_SCREEN_DETAIL,     /**< All numerical telemetry data / Tutti i dati di telemetria */
    UI_SCREEN_GRAPH,      /**< Full-screen current/power graph / Grafico fullscreen corrente/potenza */
    UI_SCREEN_SETTINGS,   /**< USB output control and lab mode / Controllo uscite USB e modalita' lab */
    UI_SCREEN_COUNT,      /**< Used internally for wrap-around navigation / Usato internamente per il wrap-around */
} UIScreen_t;

/* =========================================================
 * INTERNAL STATE STRUCTURE / STRUTTURA DI STATO INTERNO
 * Do not write to this directly from outside; use the API functions.
 * Non scrivere direttamente dall'esterno; usare le funzioni API.
 * ========================================================= */

typedef struct {
    UIScreen_t current_screen;    /**< Currently displayed screen / Schermata attualmente visualizzata */
    UIScreen_t prev_screen;       /**< Previous screen (to detect transitions) / Schermata precedente */
    uint8_t    needs_full_redraw; /**< 1 = redraw everything, 0 = partial update only / 1 = ridisegna tutto, 0 = solo update parziale */
    uint32_t   last_refresh_tick; /**< Tick of the last UI refresh / Tick dell'ultimo refresh UI */
    uint32_t   btn_press_tick;    /**< Tick when the button was first pressed / Tick di quando il bottone e' stato premuto */
    uint8_t    btn_was_held;      /**< Flag to avoid double event on long press / Flag per evitare doppio evento su long press */
} UIState_t;

/** Global UI state (defined in ui_state.c) / Stato globale UI (definito in ui_state.c) */
extern UIState_t ui_state;

/* =========================================================
 * PUBLIC API / API PUBBLICA
 * ========================================================= */

/**
 * @brief  Initialise the UI system.
 *         Inizializza il sistema UI.
 * @note   Call in main() after the display, encoder and telemetry have all
 *         been initialised. Shows the boot screen.
 *         Chiamare in main() dopo aver inizializzato display, encoder e telemetria.
 *         Mostra la schermata di boot.
 */
void UI_Init(void);

/**
 * @brief  Main UI function: call this in the main loop.
 *         Funzione principale della UI: va chiamata nel loop di main().
 * @note   Handles internally: encoder and button reading, telemetry polling
 *         (respecting the interval), and refreshing the current screen.
 *         Does not block: returns immediately if there is nothing to do.
 *         Gestisce internamente: lettura encoder e bottone, polling telemetria
 *         (rispettando l'intervallo), aggiornamento della schermata corrente.
 *         Non blocca: ritorna subito se non c'e' nulla da fare.
 */
void UI_Tick(void);

/**
 * @brief  Navigate to the specified screen.
 *         Naviga alla schermata specificata.
 * @note   Sets the needs_full_redraw flag so the screen is fully redrawn
 *         the first time it appears.
 *         Imposta il flag needs_full_redraw cosi' la schermata viene
 *         ridisegnata completamente la prima volta.
 * @param  screen  Destination screen / Schermata di destinazione
 */
void UI_NavigateTo(UIScreen_t screen);

/**
 * @brief  Return the currently displayed screen.
 *         Restituisce la schermata attualmente visualizzata.
 * @retval Current UIScreen_t / UIScreen_t corrente
 */
UIScreen_t UI_GetCurrentScreen(void);

#ifdef __cplusplus
}
#endif

#endif /* __UI_STATE_H */
