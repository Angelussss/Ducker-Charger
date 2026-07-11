/**
 * @file    widgets.h
 * @brief   Reusable UI components: bars, graphs, labels, icons.
 *
 * A "widget" is a self-contained graphical component that knows how to
 * draw itself and update itself partially.
 *
 * PARTIAL-UPDATE PRINCIPLE:
 *   To avoid flickering, we never redraw the entire screen. Each widget
 *   typically has a _Draw() function that draws everything the first time,
 *   and an _Update() function that redraws ONLY the parts that changed
 *   (usually just the numbers, not the background).
 *
 * AUTOMATIC BATTERY COLOUR:
 *   > 50%  --> Green  (COLOR_GREEN)
 *   > 20%  --> Yellow (COLOR_YELLOW)
 *   <= 20% --> Red    (COLOR_DANGER)
 */

#ifndef __WIDGETS_H
#define __WIDGETS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include "display/gfx.h"

/* =========================================================
 * WIDGET: BATTERY BAR
 * ========================================================= */

/**
 * @brief  Draw the full battery bar (background + fill + percentage text).
 * @param  x, y   Top-left corner of the widget
 * @param  w, h   Widget dimensions
 * @param  pct    Charge percentage (0–100)
 *
 * Draws:
 *   [███████░░░░░░]  85%
 *
 * Fill colour changes automatically based on pct.
 */
void Widget_BatteryBar_Draw(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t pct);

/**
 * @brief  Update only the fill and text in an already-drawn battery bar.
 * @note   Much faster than _Draw(). Use this in the update loop.
 * @param  x, y   Same coordinates used in _Draw()
 * @param  w, h   Same dimensions used in _Draw()
 * @param  pct    New percentage
 */
void Widget_BatteryBar_Update(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t pct);

/* =========================================================
 * WIDGET: LINE GRAPH
 * ========================================================= */

/**
 * @brief  Draw a line graph from a ring-buffer of data samples.
 * @note   Redraws the entire graph area each call. Keep the call rate low
 *         (max ~2 Hz) or call after clearing the area.
 *
 * @param  x, y       Top-left of the graph area
 * @param  w, h       Graph area dimensions in pixels
 * @param  data       Circular buffer of samples (int16_t, can be negative)
 * @param  data_len   Total number of slots in the buffer
 * @param  data_idx   Index of the next free slot (marks where the buffer starts)
 * @param  val_min    Expected minimum value, used to scale the Y axis
 * @param  val_max    Expected maximum value
 * @param  line_color Graph line colour
 * @param  bg_color   Graph area background colour
 *
 * Example (current graph, -5000 mA to +5000 mA):
 *   Widget_LineGraph_Draw(10, 100, 220, 80,
 *                         telemetry.current_history,
 *                         TELEMETRY_HISTORY_SIZE, telemetry.history_idx,
 *                         -5000, 5000, COLOR_CYAN, COLOR_BG);
 */
/* like _Draw but no wipe, no border, no zero-line:
 * for stacking many traces on one graph */
void Widget_LineGraph_Trace(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                            int16_t *data, uint8_t data_len, uint8_t data_idx,
                            int16_t val_min, int16_t val_max, uint16_t line_color);

void Widget_LineGraph_Draw(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                           int16_t *data, uint8_t data_len, uint8_t data_idx,
                           int16_t val_min, int16_t val_max,
                           uint16_t line_color, uint16_t bg_color);

/* =========================================================
 * WIDGET: LARGE VALUE LABEL
 *
 * Shows a small label above a large numerical value.
 *
 *   VOLTAGE
 *   14.75 V
 * ========================================================= */

/**
 * @brief  Draw a label with a small header and a large numerical value.
 * @param  x, y         Position
 * @param  label        Header text (e.g. "VOLTAGE")
 * @param  value_str    Value already formatted as a string (e.g. "14.75 V")
 * @param  value_color  Colour of the large number
 */
void Widget_ValueLabel_Draw(uint16_t x, uint16_t y,
                            const char *label, const char *value_str,
                            uint16_t value_color);

/**
 * @brief  Update only the numerical value part, without redrawing the label.
 * @param  x, y         Same position as _Draw()
 * @param  value_str    New formatted value
 * @param  value_color  Colour of the number
 */
void Widget_ValueLabel_Update(uint16_t x, uint16_t y,
                              const char *value_str, uint16_t value_color);

/* =========================================================
 * WIDGET: STATUS ICON
 *
 * Shows a small coloured icon with a text label.
 *
 *   [●] USB-C CONNECTED
 * ========================================================= */

/** Available icon types */
typedef enum {
    ICON_USB_C = 0,   /**< USB-C connector */
    ICON_CHARGING,    /**< Lightning bolt (charging) */
    ICON_FULL,        /**< Checkmark (full) */
    ICON_TEMP_OK,     /**< Green thermometer */
    ICON_TEMP_WARN,   /**< Orange thermometer */
    ICON_OUTPUT_ON,   /**< Active USB output */
    ICON_OUTPUT_OFF,  /**< Inactive USB output */
    ICON_DISCHARGING, /**< Down arrow: running on battery */
    ICON_TEMP_COLD,   /**< Blue thermometer: too cold */
    ICON_ALERT,       /**< Red "!": critically low voltage */
} IconType_t;

/**
 * @brief  Draw a status indicator with icon and text.
 * @param  x, y   Position
 * @param  type   Icon type (see IconType_t)
 * @param  label  Text next to the icon
 * @param  active 1 = active (teal colour), 0 = inactive (grey)
 */
void Widget_StatusIcon_Draw(uint16_t x, uint16_t y, IconType_t type,
                            const char *label, uint8_t active);

/* =========================================================
 * WIDGET: MENU SELECTION ROW
 * Highlights the currently selected entry in a menu.
 * ========================================================= */

/**
 * @brief  Draw a menu row with optional selection highlight.
 * @param  x, y     Position
 * @param  w, h     Row dimensions
 * @param  text     Row text
 * @param  selected 1 = highlighted (accent background), 0 = normal
 */
void Widget_MenuRow_Draw(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                         const char *text, uint8_t selected);

/* like MenuRow but text centred: for buttons (OK, Exit, ...)
 * where left-align look wrong */
void Widget_Button_Draw(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                        const char *text, uint8_t selected);

#ifdef __cplusplus
}
#endif

#endif /* __WIDGETS_H */