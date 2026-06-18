/**
 * @file    screens.c
 * @brief   Application UI screens implementation.
 *
 * COORDINATE LAYOUT (240x320 display, portrait mode):
 *
 *   Y=0   ┌────────────────────────┐
 *         │  HEADER  (30 px)       │  title + status icons
 *   Y=30  ├────────────────────────┤
 *         │  CONTENT (260 px)      │  screen-specific
 *   Y=290 ├────────────────────────┤
 *         │  FOOTER  (30 px)       │  navigation dots
 *   Y=320 └────────────────────────┘
 */

#include "ui/screens.h"
#include "ui/widgets.h"
#include "app/telemetry.h"
#include "main.h"       /* GPIO pin definitions */
#include <stdio.h>      /* snprintf */

/* =========================================================
 * LAYOUT CONSTANTS
 * ========================================================= */

#define HEADER_Y    0
#define HEADER_H    30
#define CONTENT_Y   (HEADER_Y + HEADER_H)
#define CONTENT_H   260
#define FOOTER_Y    290
#define FOOTER_H    30

/* MAIN screen element positions */
#define MAIN_BATTBAR_X   10
#define MAIN_BATTBAR_Y   (CONTENT_Y + 10)
#define MAIN_BATTBAR_W   220
#define MAIN_BATTBAR_H   40

#define MAIN_VOLTAGE_X   10
#define MAIN_VOLTAGE_Y   (MAIN_BATTBAR_Y + MAIN_BATTBAR_H + 10)

#define MAIN_CURRENT_X   130
#define MAIN_CURRENT_Y   MAIN_VOLTAGE_Y

#define MAIN_GRAPH_X     10
#define MAIN_GRAPH_Y     (MAIN_VOLTAGE_Y + 55)
#define MAIN_GRAPH_W     220
#define MAIN_GRAPH_H     100

/* =========================================================
 * PRIVATE HELPER FUNCTIONS
 * ========================================================= */

/**
 * @brief  Draw the common header bar shared by all screens.
 * @param  title  Title string to display 
*/
static void draw_header(const char *title)
{
    /* Header background  */
    ILI9341_FillRect(0, HEADER_Y, ILI9341_WIDTH, HEADER_H, COLOR_DARKGRAY);

    /* Title text */
    GFX_DrawString(8, HEADER_Y + 8, title, &GFX_FontMedium, COLOR_WHITE, COLOR_DARKGRAY);

    /* USB-C icon top-right when charger is connected.*/
    if (telemetry.vbus_present)
        Widget_StatusIcon_Draw(200, HEADER_Y + 8, ICON_USB_C, "", 1);

    /* Charging or full icon */
    if (telemetry.is_full)
        Widget_StatusIcon_Draw(185, HEADER_Y + 8, ICON_FULL, "", 1);
    else if (telemetry.is_charging)
        Widget_StatusIcon_Draw(185, HEADER_Y + 8, ICON_CHARGING, "", 1);
}

/**
 * @brief  Draw the footer navigation dots.
 *
 * Four dots represent the four navigable screens.
 * The active dot is filled; the others are outlines only.
 *
 * @param  active_dot  Active screen index (0=main…3=settings)
 */
static void draw_footer(uint8_t active_dot)
{
    ILI9341_FillRect(0, FOOTER_Y, ILI9341_WIDTH, FOOTER_H, COLOR_DARKGRAY);

    const uint16_t dot_xs[] = {80, 106, 132, 158};
    for (uint8_t i = 0; i < 4; i++)
    {
        uint16_t color = (i == active_dot) ? COLOR_WHITE : COLOR_GRAY;
        GFX_DrawCircle(dot_xs[i], FOOTER_Y + 15, 4, color);
        if (i == active_dot)
            ILI9341_FillRect(dot_xs[i] - 2, FOOTER_Y + 13, 5, 5, color);
    }
}

/**
 * @brief  Return the text colour for a current value.
 *         Green = charging (positive), cyan = discharging (negative).
 */
static uint16_t current_color(int16_t current_mA)
{
    return (current_mA >= 0) ? COLOR_GREEN : COLOR_CYAN;
}

/* =========================================================
 * PUBLIC FUNCTION – battery_color_pub
 *
 * Shared colour logic used by both screens.c and widgets.c.
 * ========================================================= */

uint16_t battery_color_pub(uint8_t pct)
{
    if (pct > 50) return COLOR_GREEN;
    if (pct > 20) return COLOR_YELLOW;
    return COLOR_DANGER;
}

/* =========================================================
 * SCREEN: BOOT
 * ========================================================= */

void Screen_Boot_Draw(void)
{
    ILI9341_FillScreen(COLOR_BG);

    /* Project name centred on screen */
    GFX_DrawString(55,  120, "DUCKER",   &GFX_FontLarge, COLOR_ACCENT, COLOR_BG);
    GFX_DrawString(40,  150, "CHARGER",  &GFX_FontLarge, COLOR_WHITE,  COLOR_BG);

    /* Subtitle */
    GFX_DrawString(50, 185, "Smart UPS 4S3P", &GFX_FontSmall, COLOR_GRAY, COLOR_BG);

    /* Decorative lines */
    GFX_DrawHLine(40, 115, 160, COLOR_ACCENT);
    GFX_DrawHLine(40, 200, 160, COLOR_GRAY);

    /* Version string at the bottom */
    GFX_DrawString(60, 290, "v0.1 - IoT Project", &GFX_FontSmall, COLOR_DARKGRAY, COLOR_BG);
}

/* =========================================================
 * SCREEN: MAIN
 * ========================================================= */

void Screen_Main_Draw(void)
{
    ILI9341_FillScreen(COLOR_BG);

    draw_header("DUCKER CHARGER");

    /* Battery bar */
    Widget_BatteryBar_Draw(MAIN_BATTBAR_X, MAIN_BATTBAR_Y,
                           MAIN_BATTBAR_W, MAIN_BATTBAR_H,
                           telemetry.soc_percent);

    /* Fixed labels drawn once here, not repeated in _Update().*/
    GFX_DrawString(MAIN_VOLTAGE_X, MAIN_VOLTAGE_Y,
                   "VOLTAGE", &GFX_FontSmall, COLOR_LIGHTGRAY, COLOR_BG);
    GFX_DrawString(MAIN_CURRENT_X, MAIN_CURRENT_Y,
                   "CURRENT", &GFX_FontSmall, COLOR_LIGHTGRAY, COLOR_BG);

    /* Separator line above the graph */
    GFX_DrawHLine(10, MAIN_GRAPH_Y - 5, 220, COLOR_DARKGRAY);

    /* Graph label */
    GFX_DrawString(10, MAIN_GRAPH_Y - 16,
                   "Current (30 s)", &GFX_FontSmall, COLOR_GRAY, COLOR_BG);

    draw_footer(0); /* dot 0 = MAIN */

    /* Populate dynamic values immediately on first draw.*/
    Screen_Main_Update();
}

void Screen_Main_Update(void)
{
    char buf[20];

    /* Update battery bar fill and percentage. */
    Widget_BatteryBar_Update(MAIN_BATTBAR_X, MAIN_BATTBAR_Y,
                             MAIN_BATTBAR_W, MAIN_BATTBAR_H,
                             telemetry.soc_percent);

    /* Voltage: format "14.75 V" */
    snprintf(buf, sizeof(buf), "%d.%02d V",
             telemetry.voltage_mV / 1000,
             (telemetry.voltage_mV % 1000) / 10);
    Widget_ValueLabel_Update(MAIN_VOLTAGE_X,
                             MAIN_VOLTAGE_Y + GFX_FONT_SMALL_H + 2,
                             buf, COLOR_WHITE);

    /* Current: format "+2450 mA" or "-1230 mA". */
    snprintf(buf, sizeof(buf), "%+d mA", telemetry.current_mA);
    Widget_ValueLabel_Update(MAIN_CURRENT_X,
                             MAIN_CURRENT_Y + GFX_FONT_SMALL_H + 2,
                             buf, current_color(telemetry.current_mA));

    /* Redraw the current history graph. */
    Widget_LineGraph_Draw(MAIN_GRAPH_X, MAIN_GRAPH_Y,
                          MAIN_GRAPH_W, MAIN_GRAPH_H,
                          telemetry.current_history,
                          TELEMETRY_HISTORY_SIZE,
                          telemetry.history_idx,
                          -5000, 5000,
                          COLOR_CYAN, COLOR_BG);

    /* Refresh status icons in the header right side. */
    if (telemetry.vbus_present)
        Widget_StatusIcon_Draw(200, HEADER_Y + 8, ICON_USB_C, "", 1);
    else
        ILI9341_FillRect(185, HEADER_Y + 5, 50, 20, COLOR_DARKGRAY);
}

/* =========================================================
 * SCREEN: DETAIL
 *
 * 2-column x 3-row grid layout:
 *
 *   Col A (x=10)      Col B (x=125)
 *   SoC               Voltage
 *   Current           Power
 *   Temperature       Charger state
 * ========================================================= */

#define DETAIL_COL_A   10
#define DETAIL_COL_B   125
#define DETAIL_ROW_1   (CONTENT_Y + 10)
#define DETAIL_ROW_2   (DETAIL_ROW_1 + 75)
#define DETAIL_ROW_3   (DETAIL_ROW_2 + 75)

void Screen_Detail_Draw(void)
{
    ILI9341_FillScreen(COLOR_BG);
    draw_header("DETAILS");

    /* Fixed grid labels */
    GFX_DrawString(DETAIL_COL_A, DETAIL_ROW_1, "CHARGE",      &GFX_FontSmall, COLOR_LIGHTGRAY, COLOR_BG);
    GFX_DrawString(DETAIL_COL_B, DETAIL_ROW_1, "VOLTAGE",     &GFX_FontSmall, COLOR_LIGHTGRAY, COLOR_BG);
    GFX_DrawString(DETAIL_COL_A, DETAIL_ROW_2, "CURRENT",     &GFX_FontSmall, COLOR_LIGHTGRAY, COLOR_BG);
    GFX_DrawString(DETAIL_COL_B, DETAIL_ROW_2, "POWER",       &GFX_FontSmall, COLOR_LIGHTGRAY, COLOR_BG);
    GFX_DrawString(DETAIL_COL_A, DETAIL_ROW_3, "TEMP",        &GFX_FontSmall, COLOR_LIGHTGRAY, COLOR_BG);
    GFX_DrawString(DETAIL_COL_B, DETAIL_ROW_3, "CHARGER",     &GFX_FontSmall, COLOR_LIGHTGRAY, COLOR_BG);

    /* Grid separator lines */
    GFX_DrawVLine(120, CONTENT_Y + 5, CONTENT_H - 10, COLOR_DARKGRAY);
    GFX_DrawHLine(10,  DETAIL_ROW_2 - 5, 220, COLOR_DARKGRAY);
    GFX_DrawHLine(10,  DETAIL_ROW_3 - 5, 220, COLOR_DARKGRAY);

    draw_footer(1); /* dot 1 = DETAIL */
    Screen_Detail_Update();
}

void Screen_Detail_Update(void)
{
    char buf[20];
    /* Offset below the small label to where the large value starts.*/
    uint16_t val_y = GFX_FONT_SMALL_H + 4;

    /* SoC */
    snprintf(buf, sizeof(buf), "%d%%", telemetry.soc_percent);
    Widget_ValueLabel_Update(DETAIL_COL_A, DETAIL_ROW_1 + val_y,
                             buf, battery_color_pub(telemetry.soc_percent));

    /* Voltage */
    snprintf(buf, sizeof(buf), "%d.%02dV",
             telemetry.voltage_mV / 1000,
             (telemetry.voltage_mV % 1000) / 10);
    Widget_ValueLabel_Update(DETAIL_COL_B, DETAIL_ROW_1 + val_y, buf, COLOR_WHITE);

    /* Current */
    snprintf(buf, sizeof(buf), "%+dmA", telemetry.current_mA);
    Widget_ValueLabel_Update(DETAIL_COL_A, DETAIL_ROW_2 + val_y,
                             buf, current_color(telemetry.current_mA));

    /* Power */
    snprintf(buf, sizeof(buf), "%+ldmW", (long)telemetry.power_mW);
    Widget_ValueLabel_Update(DETAIL_COL_B, DETAIL_ROW_2 + val_y, buf, COLOR_YELLOW);

    /* Temperature */
    snprintf(buf, sizeof(buf), "%d C", telemetry.temp_celsius);
    Widget_ValueLabel_Update(DETAIL_COL_A, DETAIL_ROW_3 + val_y,
                             buf, telemetry.over_temp ? COLOR_DANGER : COLOR_WHITE);

    /* Charger state */
    const char *phase_str[] = {"IDLE", "PRE", "FAST", "TAPER"};
    const char *state_str   = telemetry.vbus_present
                              ? phase_str[telemetry.charge_phase & 0x03]
                              : "NO INPUT";
    Widget_ValueLabel_Update(DETAIL_COL_B, DETAIL_ROW_3 + val_y,
                             state_str,
                             telemetry.vbus_present ? COLOR_GREEN : COLOR_GRAY);
}

/* =========================================================
 * SCREEN: GRAPH
 * ========================================================= */

#define GRAPH_AREA_X   5
#define GRAPH_AREA_Y   (CONTENT_Y + 20)
#define GRAPH_AREA_W   230
#define GRAPH_AREA_H   220

void Screen_Graph_Draw(void)
{
    ILI9341_FillScreen(COLOR_BG);
    draw_header("CURRENT GRAPH");

    /* Y-axis labels (max and min values). */
    GFX_DrawString(GRAPH_AREA_X, GRAPH_AREA_Y,
                   "+5A", &GFX_FontSmall, COLOR_GRAY, COLOR_BG);
    GFX_DrawString(GRAPH_AREA_X,
                   GRAPH_AREA_Y + GRAPH_AREA_H - GFX_FONT_SMALL_H,
                   "-5A", &GFX_FontSmall, COLOR_GRAY, COLOR_BG);

    /* X-axis label */
    GFX_DrawString(55, GRAPH_AREA_Y + GRAPH_AREA_H + 5,
                   "< 30 seconds >", &GFX_FontSmall, COLOR_GRAY, COLOR_BG);

    draw_footer(2); /* dot 2 = GRAPH */
    Screen_Graph_Update();
}

void Screen_Graph_Update(void)
{
    /* Full-screen current graph, range -5000 mA to +5000 mA.
     * The first 18 px on the left are reserved for the Y-axis labels. */
    Widget_LineGraph_Draw(GRAPH_AREA_X + 18, GRAPH_AREA_Y,
                          GRAPH_AREA_W - 18, GRAPH_AREA_H,
                          telemetry.current_history,
                          TELEMETRY_HISTORY_SIZE,
                          telemetry.history_idx,
                          -5000, 5000,
                          COLOR_CYAN, COLOR_BG);

    /* Instantaneous current value top-right of the graph area.*/
    char buf[16];
    snprintf(buf, sizeof(buf), "%+d mA", telemetry.current_mA);
    ILI9341_FillRect(140, CONTENT_Y + 5, 90, GFX_FONT_MEDIUM_H, COLOR_BG);
    GFX_DrawString(140, CONTENT_Y + 5, buf, &GFX_FontMedium,
                   current_color(telemetry.current_mA), COLOR_BG);
}

/* =========================================================
 * SCREEN: SETTINGS
 * ========================================================= */

/**
 * @brief  Descriptor for one settings menu row.
 *
 * The `static` keyword means this array keeps its values between calls,
 * so the ON/OFF toggle state persists across screen transitions.
 */
typedef struct {
    const char   *name;       /**< Display name */
    GPIO_TypeDef *gpio_port;  /**< GPIO port controlling the output */
    uint16_t      gpio_pin;   /**< GPIO pin */
    uint8_t       state;      /**< Current state: 0 = OFF, 1 = ON */
} SettingsRow_t;

static SettingsRow_t _settings_rows[] = {
    { "USB-A 1",    USB_A1_CTRL_GPIO_Port,    USB_A1_CTRL_Pin,    0 },
    { "USB-A 2",    USB_A2_CTTL_GPIO_Port,    USB_A2_CTTL_Pin,    0 },
    { "Lab output", LAB_ENABLER_GPIO_Port,    LAB_ENABLER_Pin,    0 },
    { "USB-C 2",    USB_C2_ENABLER_GPIO_Port, USB_C2_ENABLER_Pin, 0 },
};
#define SETTINGS_NUM_ROWS  (sizeof(_settings_rows) / sizeof(_settings_rows[0]))

#define SETTINGS_ROW_H      50
#define SETTINGS_ROW_START  (CONTENT_Y + 10)

void Screen_Settings_Draw(uint8_t selected_row)
{
    ILI9341_FillScreen(COLOR_BG);
    draw_header("SETTINGS");

    /* Instruction bar at the bottom */
    ILI9341_FillRect(0, FOOTER_Y, ILI9341_WIDTH, FOOTER_H, COLOR_DARKGRAY);
    GFX_DrawString(10, FOOTER_Y + 8,
                   "PRESS=toggle  HOLD=home",
                   &GFX_FontSmall, COLOR_GRAY, COLOR_DARKGRAY);

    Screen_Settings_Update(selected_row);
}

void Screen_Settings_Update(uint8_t selected_row)
{
    char buf[32];

    for (uint8_t i = 0; i < SETTINGS_NUM_ROWS; i++)
    {
        uint16_t row_y = SETTINGS_ROW_START + i * SETTINGS_ROW_H;

        /* Build the row string: "Name         [ON]" or "Name         [OFF]". */
        snprintf(buf, sizeof(buf), "%-12s [%s]",
                 _settings_rows[i].name,
                 _settings_rows[i].state ? "ON " : "OFF");

        Widget_MenuRow_Draw(10, row_y, 220, SETTINGS_ROW_H - 4,
                            buf, (i == selected_row));

        /* Apply current state to the physical GPIO pin. */
        HAL_GPIO_WritePin(_settings_rows[i].gpio_port,
                          _settings_rows[i].gpio_pin,
                          _settings_rows[i].state ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
}

void Screen_Settings_Toggle(uint8_t row)
{
    if (row >= SETTINGS_NUM_ROWS) return;

    /* XOR with 1 flips the bit: 0 -> 1, 1 -> 0. */
    _settings_rows[row].state ^= 1;
}
