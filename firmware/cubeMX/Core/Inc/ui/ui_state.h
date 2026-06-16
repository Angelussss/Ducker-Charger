/**
 * @file    ui_state.h
 * @brief   State machine for navigation between UI screens.
 *
 * This module manages WHICH screen is active and WHEN to transition to
 * another one. It is the "brain" of the user interface.
 *
 * NAVIGATION:
 *   Rotate encoder RIGHT / LEFT --> navigate menu / change value
 *   Short press (click)         --> enter screen / confirm
 *   Long press (hold 1 s)       --> return to main screen from anywhere
 *
 * STATE MACHINE:
 *
 *              [BOOT]
 *                 |  (auto after 1.5 s)
 *                 v
 *   [MAIN] <----> [DETAIL] <----> [GRAPH] <----> [SETTINGS]
 *     ^                                               |
 *     |______________(long press anywhere)____________|
 *
 * UPDATE FREQUENCY:
 *   UI_Tick() must be called in the main loop. Internally it manages two
 *   independent timers:
 *
 *   - Sensor poll:  every TELEMETRY_POLL_INTERVAL_MS (500 ms)
 *   - UI refresh:   every UI_REFRESH_INTERVAL_MS (250 ms = 4 fps)
 *
 *   4 fps is sufficient for slowly-changing numbers and leaves plenty of
 *   CPU time for I2C polling.
 */

#ifndef __UI_STATE_H
#define __UI_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

/* =========================================================
 * CONFIGURATION
 * ========================================================= */

/**
 * How often to refresh the displayed values, in milliseconds.
 */
#define UI_REFRESH_INTERVAL_MS  250

/**
 * How long the button must be held to trigger a "go home" action, in ms.
 */
#define UI_LONG_PRESS_MS        1000

/* =========================================================
 * AVAILABLE SCREENS
 * ========================================================= */

/**
 * @brief  Identifiers for the application screens.
 *
 * The order reflects the horizontal navigation with the encoder.
 */
typedef enum {
    UI_SCREEN_BOOT = 0,   /**< Splash screen, not navigable */
    UI_SCREEN_MAIN,       /**< Home: large SoC + current + graph */
    UI_SCREEN_DETAIL,     /**< All numerical telemetry data */
    UI_SCREEN_GRAPH,      /**< Full-screen current/power graph */
    UI_SCREEN_SETTINGS,   /**< USB output control and lab mode */
    UI_SCREEN_COUNT,      /**< Used internally for wrap-around navigation */
} UIScreen_t;

/* =========================================================
 * INTERNAL STATE STRUCTURE
 * Do not write to this directly from outside; use the API functions.
 * ========================================================= */

typedef struct {
    UIScreen_t current_screen;    /**< Currently displayed screen */
    UIScreen_t prev_screen;       /**< Previous screen (to detect transitions) */
    uint8_t    needs_full_redraw; /**< 1 = redraw everything, 0 = partial update only */
    uint32_t   last_refresh_tick; /**< Tick of the last UI refresh */
    uint32_t   btn_press_tick;    /**< Tick when the button was first pressed */
    uint8_t    btn_was_held;      /**< Flag to avoid double event on long press */
} UIState_t;

/** Global UI state (defined in ui_state.c) */
extern UIState_t ui_state;

/* =========================================================
 * PUBLIC API
 * ========================================================= */

/**
 * @brief  Initialise the UI system.
 * @note   Call in main() after the display, encoder and telemetry have all
 *         been initialised. Shows the boot screen.
 */
void UI_Init(void);

/**
 * @brief  Main UI function: call this in the main loop.
 * @note   Handles internally: encoder and button reading, telemetry polling
 *         (respecting the interval), and refreshing the current screen.
 *         Does not block: returns immediately if there is nothing to do.
 */
void UI_Tick(void);

/**
 * @brief  Navigate to the specified screen.
 * @note   Sets the needs_full_redraw flag so the screen is fully redrawn
 *         the first time it appears.
 * @param  screen  Destination screen
 */
void UI_NavigateTo(UIScreen_t screen);

/**
 * @brief  Return the currently displayed screen.
 * @retval Current UIScreen_t
 */
UIScreen_t UI_GetCurrentScreen(void);

#ifdef __cplusplus
}
#endif

#endif /* __UI_STATE_H */