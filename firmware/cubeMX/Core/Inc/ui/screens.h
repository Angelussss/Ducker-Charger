/**
 * @file    screens.h
 * @brief   Application UI screens declarations.
 *
 * Each screen is defined by two functions:
 *
 *   Screen_XXX_Draw()   --> draws EVERYTHING (background, fixed labels, values).
 *                           Called only when entering the screen.
 *
 *   Screen_XXX_Update() --> redraws ONLY the parts that change (numbers).
 *                           Called periodically by the UI loop.
 *
 * This two-phase approach eliminates flickering: the background and fixed
 * labels are drawn once, only the numerical values are overwritten.
 *
 * SCREEN LAYOUTS:
 *
 * MAIN (240x320):
 *   ┌──────────────────────┐
 *   │  DUCKER  [●USB-C]    │  <- header (30 px)
 *   ├──────────────────────┤
 *   │   [████████░░] 85%   │  <- battery bar (40 px)
 *   │      14.75 V         │  <- voltage (40 px)
 *   │     +2450 mA         │  <- current (40 px)
 *   ├──────────────────────┤
 *   │  ~~~~~~~~~~~~~~~~~~~~│  <- current graph (100 px)
 *   ├──────────────────────┤
 *   │  ●  ○  ○  ○          │  <- navigation dots footer (30 px)
 *   └──────────────────────┘
 *
 * DETAIL: 2x3 grid of ValueLabel widgets with all telemetry data.
 *
 * GRAPH:  Full-screen graph + time axis + legend.
 *
 * SETTINGS: Scrollable list of ON/OFF toggles navigable with the encoder.
 */

#ifndef __SCREENS_H
#define __SCREENS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

/* =========================================================
 * SCREEN: BOOT (splash screen)
 * ========================================================= */

/**
 * @brief  Show the project logo/name at startup.
 * @note   No _Update() because the screen is static. After ~1.5 s
 *         ui_state.c automatically navigates to MAIN.
 */
void Screen_Boot_Draw(void);

/* =========================================================
 * SCREEN: MAIN (home)
 * ========================================================= */

/**
 * @brief  Draw the main screen in full.
 * @note   Draws background, all labels and the current values. Called only
 *         when entering the screen (e.g. at startup or returning from another).
 */
void Screen_Main_Draw(void);

/**
 * @brief  Update only the dynamic parts of the main screen.
 * @note   Redraws: SoC percentage, voltage, current, graph.
 *         Does NOT redraw: background, labels, static icons.
 *         Called periodically by UI_Tick().
 */
void Screen_Main_Update(void);

/* =========================================================
 * SCREEN: DETAIL (full telemetry data)
 * ========================================================= */

/**
 * @brief  Draw the screen showing all system data.
 * @note   Shows: SoC, voltage, current, power, temperature, charger state.
 */
void Screen_Detail_Draw(void);

/**
 * @brief  Update the numerical values on the detail screen.
 */
void Screen_Detail_Update(void);

/* =========================================================
 * SCREEN: GRAPH (full-screen graph)
 * ========================================================= */

/**
 * @brief  Draw the full-screen graph screen.
 * @note   The graph shows the last TELEMETRY_HISTORY_SIZE current samples.
 *         X axis = time, Y axis = current in mA.
 */
void Screen_Graph_Draw(void);

/**
 * @brief  Update the graph with new data.
 * @note   Redraws the entire graph area (partial update on a line graph
 *         without artifacts is impractical).
 */
void Screen_Graph_Update(void);

/* =========================================================
 * SCREEN: SETTINGS
 * ========================================================= */

/**
 * @brief  Draw the settings screen.
 * @note   Available entries:
 *           - USB-A 1:    ON / OFF  (USB_A1_CTRL_Pin)
 *           - USB-A 2:    ON / OFF  (USB_A2_CTTL_Pin)
 *           - Lab output: ON / OFF  (LAB_ENABLER_Pin)
 *           - USB-C 2:    ON / OFF  (USB_C2_ENABLER_Pin)
 * @param  selected_row  Currently highlighted row (0–3)
 */
void Screen_Settings_Draw(uint8_t selected_row);

/**
 * @brief  Update the row highlight and the ON/OFF values.
 * @param  selected_row  Currently highlighted row
 */
void Screen_Settings_Update(uint8_t selected_row);

/**
 * @brief  Toggle the ON/OFF state of the output at the given row.
 * @note   Updates both the internal state and the physical GPIO pin.
 *         Called from ui_state.c when the button is pressed in SETTINGS.
 * @param  row  Row index (0 = USB-A1, 1 = USB-A2, 2 = Lab, 3 = USB-C2)
 */
void Screen_Settings_Toggle(uint8_t row);

/**
 * @brief  Return the appropriate colour for a battery charge percentage.
 * @note   Shared utility used by screens.c and widgets.c.
 *         >50% = green, >20% = yellow, <=20% = red.
 * @param  pct  Percentage 0–100
 * @retval RGB565 colour
 */
uint16_t battery_color_pub(uint8_t pct);

#ifdef __cplusplus
}
#endif

#endif /* __SCREENS_H */