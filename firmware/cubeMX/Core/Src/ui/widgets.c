/**
 * @file    widgets.c
 * @brief   Reusable UI widget implementation.
 *          Implementazione dei widget UI riutilizzabili.
 *
 * _Draw() functions render the widget completely (including background).
 * _Update() functions redraw ONLY the dynamic part (numbers/fill).
 * To "erase" the old value before writing the new one, _Update() first
 * fills the value area with the background colour, then writes the new text.
 * This prevents flickering without redrawing the whole screen.
 */

#include "ui/widgets.h"
#include "ui/screens.h"     /* battery_color_pub() */
#include "app/telemetry.h"
#include <stdio.h>    /* snprintf */
#include <string.h>   /* strlen   */

/* =========================================================
 * PRIVATE HELPERS
 * ========================================================= */

/**
 * @brief  Local wrapper around battery_color_pub() from screens.c.
 *         Avoids duplicating the colour logic here.
 */
static uint16_t battery_color(uint8_t pct)
{
    return battery_color_pub(pct);
}

/* =========================================================
 * WIDGET: BATTERY BAR
 * ========================================================= */

void Widget_BatteryBar_Draw(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t pct)
{
    /* Outer border */
    GFX_DrawRoundRect(x, y, w, h, 3, COLOR_GRAY);

    /* Inner fill area width (2 px padding on each side from the border). */
    uint16_t inner_w = w - 4;
    uint16_t fill_w  = (uint16_t)((uint32_t)inner_w * pct / 100);

    /* Draw the empty (dark) background of the bar. */
    ILI9341_FillRect(x + 2, y + 2, inner_w, h - 4, COLOR_DARKGRAY);

    /* Draw the coloured fill proportional to the charge percentage. */
    if (fill_w > 0)
        ILI9341_FillRect(x + 2, y + 2, fill_w, h - 4, battery_color(pct));

    /* Percentage text centred inside the bar. */
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    uint16_t text_x = x + (w / 2) - (strlen(buf) * GFX_FONT_SMALL_W / 2);
    uint16_t text_y = y + (h / 2) - (GFX_FONT_SMALL_H / 2);
    GFX_DrawString(text_x, text_y, buf, &GFX_FontSmall, COLOR_WHITE, COLOR_TRANSPARENT_MAGIC);
}

void Widget_BatteryBar_Update(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t pct)
{
    /* Redraw only the interior without the border — faster than _Draw(). */
    uint16_t inner_w = w - 4;
    uint16_t fill_w  = (uint16_t)((uint32_t)inner_w * pct / 100);

    /* Clear the interior first */
    ILI9341_FillRect(x + 2, y + 2, inner_w, h - 4, COLOR_DARKGRAY);

    /* Redraw the fill */
    if (fill_w > 0)
        ILI9341_FillRect(x + 2, y + 2, fill_w, h - 4, battery_color(pct));

    /* Update the percentage text */
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    uint16_t text_x = x + (w / 2) - (strlen(buf) * GFX_FONT_SMALL_W / 2);
    uint16_t text_y = y + (h / 2) - (GFX_FONT_SMALL_H / 2);
    GFX_DrawString(text_x, text_y, buf, &GFX_FontSmall, COLOR_WHITE, COLOR_DARKGRAY);
}

/* =========================================================
 * WIDGET: LINE GRAPH
 * ========================================================= */

void Widget_LineGraph_Draw(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                           int16_t *data, uint8_t data_len, uint8_t data_idx,
                           int16_t val_min, int16_t val_max,
                           uint16_t line_color, uint16_t bg_color)
{
    /* Clear the graph area */
    ILI9341_FillRect(x, y, w, h, bg_color);

    /* Draw zero-line if the range spans both positive and negative values.
     *
     * Position: y + h * (val_max / (val_max - val_min))
     * The top of the area (y) corresponds to val_max. */
    if (val_min < 0 && val_max > 0)
    {
        int32_t range   = (int32_t)val_max - val_min;
        uint16_t zero_y = (uint16_t)(y + (uint32_t)h * (uint32_t)val_max / (uint32_t)range);
        GFX_DrawHLine(x, zero_y, w, COLOR_DARKGRAY);
    }

    /* Thin border around the graph area */
    GFX_DrawRect(x, y, w, h, COLOR_GRAY);

    if (data_len == 0 || val_max == val_min) return;

    int32_t range = (int32_t)val_max - val_min;

    /* Draw the line sample by sample.
     *
     * The data is in a ring buffer: the oldest sample is at data_idx,
     * the newest is at data_idx - 1 (with wrap-around).
     * We draw left (oldest) to right (newest). */
    uint16_t prev_px = 0, prev_py = 0;
    uint8_t  first_point = 1;

    for (uint8_t i = 0; i < data_len; i++)
    {
        /* Index into the ring buffer, starting from the oldest sample. */
        uint8_t buf_idx = (data_idx + i) % data_len;
        int16_t sample  = data[buf_idx];

        /* Clamp value to the display range */
        if (sample > val_max) sample = val_max;
        if (sample < val_min) sample = val_min;

        /* Map sample value to pixel X coordinate (horizontal). */
        uint16_t px = x + (uint16_t)((uint32_t)i * (w - 1) / (data_len - 1));

        /* Map sample value to pixel Y coordinate (vertical, inverted because
         * Y grows downward but larger values should appear higher). */
        uint16_t py = (uint16_t)(y + h - 1 -
                      (uint32_t)((int32_t)sample - val_min) * (h - 1) / range);

        if (!first_point)
            GFX_DrawLine(prev_px, prev_py, px, py, line_color);
        else
            first_point = 0;

        prev_px = px;
        prev_py = py;
    }
}

/* =========================================================
 * WIDGET: VALUE LABEL
 * ========================================================= */

#define LABEL_H  (GFX_FONT_SMALL_H + 2)
#define VALUE_H  (GFX_FONT_MEDIUM_H + 2)

void Widget_ValueLabel_Draw(uint16_t x, uint16_t y,
                            const char *label, const char *value_str,
                            uint16_t value_color)
{
    /* Small grey label on top */
    GFX_DrawString(x, y, label, &GFX_FontSmall, COLOR_LIGHTGRAY, COLOR_BG);

    /* Large value below the label */
    GFX_DrawString(x, y + LABEL_H, value_str, &GFX_FontMedium, value_color, COLOR_BG);
}

void Widget_ValueLabel_Update(uint16_t x, uint16_t y,
                              const char *value_str, uint16_t value_color)
{
    /* Erase only the value row (not the label, which is static).
     * 120 px is wide enough to cover the longest possible value string
     * (e.g. "-5000 mA" = 8 chars * 8 px = 64 px; 120 px for safety). */
    ILI9341_FillRect(x, y + LABEL_H, 120, VALUE_H, COLOR_BG);

    /* Write the new value */
    GFX_DrawString(x, y + LABEL_H, value_str, &GFX_FontMedium, value_color, COLOR_BG);
}

/* =========================================================
 * WIDGET: STATUS ICON
 * ========================================================= */

void Widget_StatusIcon_Draw(uint16_t x, uint16_t y, IconType_t type,
                            const char *label, uint8_t active)
{
    uint16_t icon_color = active ? COLOR_ACCENT : COLOR_DARKGRAY;

    /* Each icon is drawn as a small geometric shape (8x8 px area).
     * No bitmap images are used to save Flash memory. */
    switch (type)
    {
        ...
    }

    /* Text label next to the icon */
    GFX_DrawString(x + 14, y + 1, label, &GFX_FontSmall, icon_color, COLOR_BG);
}

/* =========================================================
 * WIDGET: MENU ROW
 * ========================================================= */

void Widget_MenuRow_Draw(...)
{
    if (selected)
    {
        /* Selected row: accent background + dark text. */
        ...
    }
    else
    {
        /* Normal row: dark background + light text. */
        ...
    }
}