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
void Screen_Graph_CycleScale(void);

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
uint16_t temp_color_pub(int16_t temp_c);
uint16_t volt_color_pub(uint16_t mv);
uint8_t volt_alarm_pub(uint16_t mv);   /* 0 fine, 1 warn 12-13V, 2 bad <12V */
void Screen_Header_RefreshIcons(void);

void Screen_Ports_Draw(void);
void Screen_Stats_Draw(void);
typedef enum {
    WARN_TEMP = 0,   /* temp outside comfort cave */
    WARN_VLOW,       /* 12-13 V: heads-up */
    WARN_VCRIT,      /* <12 V: very bad, go light sleep */
    WARN_OVCH,       /* BATHI set: pack overcharged, unplug the charger
                        (firmware cannot stop the autonomous C1 charge path) */
    WARN_CHGINH,     /* gauge CHG_INH/XCHG with a charger attached: pack outside
                        its charge-temperature window, unplug the charger */
    WARN_NOCAL,      /* gauge never calibrated (QEN clear): shown at boot and
                        at every wake until the wizard's IT-enable step runs */
    WARN_COUNT
} WarnType_t;

/* "gauge not calibrated" cache: refreshed at UI_Init and after the
 * wizard's IT-enable; read by the header icon and the warning scan
 * (an I2C read per UI tick would be too chatty). */
void    Screen_NoCal_Refresh(void);
uint8_t Screen_NoCal_Get(void);

typedef enum { CONFIRM_LOCKALL = 0, CONFIRM_SHUTDOWN, CONFIRM_CALIBRATE } ConfirmAction_t;
void Screen_Confirm_Open(ConfirmAction_t action);
void Screen_Confirm_Draw(void);
void Screen_Confirm_OnRotate(int8_t d);
/* return: 0 = still open, 1 = yes, 2 = no */
uint8_t Screen_Confirm_OnPress(void);
ConfirmAction_t Screen_Confirm_GetAction(void);

uint32_t Screen_Display_GetAutoSleepMs(void);   /* 0 = off */

void Screen_Warning_Open(WarnType_t type);
void Screen_Warning_Draw(void);
void Screen_Sleep_Draw(void);

/* FSM fault takeover screen: shows ERROR/EMERGENCY + the cause read from
 * the charge-layer getters. Redraw is cheap enough to run on state change. */
void Screen_Fault_Draw(void);
void Screen_Display_Open(void);
void Screen_Display_Draw(void);
void Screen_Display_OnRotate(int8_t d);
void Screen_Display_OnPress(void);
void Screen_Test_Open(void);
void Screen_Test_Draw(void);
void Screen_Test_OnRotate(int8_t d);
void Screen_Test_OnPress(void);
void Screen_Settings_LockAll(uint8_t on);
uint8_t Screen_Settings_GetLockAll(void);
void Screen_Stats_Update(void);
void Screen_Ports_Update(void);

/* ---- Calibration page (gauge calibration wizard) ---- */
void Screen_Cal_Open(void);
void Screen_Cal_Draw(void);
void Screen_Cal_Update(void);      /* live readings + offset-routine poll */
void Screen_Cal_OnRotate(int8_t d);
void Screen_Cal_OnPress(void);

/* ---- Output channel page (Lab / USB-C2) ---- */
void Screen_Output_Open(uint8_t channel);        /* 0 = Lab, 1 = USB-C2 */
void Screen_Output_Draw(void);
void Screen_Output_Update(void);   /* periodic readout refresh (Lab page) */
void Screen_Output_OnRotate(int8_t delta);
void Screen_Output_OnPress(void);
uint8_t Screen_Settings_IsOutputRow(uint8_t row);/* 1 when row open channel page */
const char *Screen_Output_RowStatus(uint8_t channel); /* "OFF" / "ON 12.0V" for settings list */

#ifdef __cplusplus
}
#endif

#endif /* __SCREENS_H */