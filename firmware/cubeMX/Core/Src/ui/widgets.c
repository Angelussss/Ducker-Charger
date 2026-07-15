/**
 * @file    widgets.c
 * @brief   small reusable drawings: battery, graphs, rows, buttons, icons.
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
    /* battery shape: round body, 2 px border, nub on right side.
     * last 8 px of `w` belong to nub. */
    uint16_t body_w = w - 8;

    GFX_DrawRoundRect(x,     y,     body_w,     h,     4, COLOR_LIGHTGRAY);
    GFX_DrawRoundRect(x + 1, y + 1, body_w - 2, h - 2, 3, COLOR_LIGHTGRAY);

    /* nub, middle of right side */
    ILI9341_FillRect(x + body_w + 1, y + h / 2 - 8, 5, 16, COLOR_LIGHTGRAY);

    Widget_BatteryBar_Update(x, y, w, h, pct);
}

void Widget_BatteryBar_Update(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t pct)
{
    /* redraw only inside, border stay, faster than _Draw().
     * one solid fill bar inside battery shape. */
    uint16_t inner_x = x + 4, inner_y = y + 4;
    uint16_t inner_w = w - 8 - 8;          /* -nub -border*2 */
    uint16_t inner_h = h - 8;
    uint16_t fill_w  = (uint16_t)((uint32_t)inner_w * pct / 100);

    ILI9341_FillRect(inner_x, inner_y, inner_w, inner_h, COLOR_DARKGRAY);
    if (fill_w > 0)
        ILI9341_FillRect(inner_x, inner_y, fill_w, inner_h, battery_color(pct));

    /* big % number drawn by caller (Screen_Main), not here */
}

/* =========================================================
 * WIDGET: LINE GRAPH
 * ========================================================= */

void Widget_LineGraph_Draw(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                           int16_t *data, uint8_t data_len, uint8_t data_idx,
                           int16_t val_min, int16_t val_max,
                           uint16_t line_color, uint16_t bg_color)
{
    /* wipe graph area */
    ILI9341_FillRect(x, y, w, h, bg_color);

    /* zero line when range cross zero.
     * position: y + h * val_max / (val_max - val_min); top = val_max */
    if (val_min < 0 && val_max > 0)
    {
        int32_t range   = (int32_t)val_max - val_min;
        uint16_t zero_y = (uint16_t)(y + (uint32_t)h * (uint32_t)val_max / (uint32_t)range);
        GFX_DrawHLine(x, zero_y, w, COLOR_DARKGRAY);
    }

    /* thin border around graph */
    GFX_DrawRect(x, y, w, h, COLOR_GRAY);

    Widget_LineGraph_Trace(x, y, w, h, data, data_len, data_idx,
                           val_min, val_max, line_color);
}

void Widget_LineGraph_Trace(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                            int16_t *data, uint8_t data_len, uint8_t data_idx,
                            int16_t val_min, int16_t val_max, uint16_t line_color)
{
    if (data_len == 0 || val_max == val_min) return;

    int32_t range = (int32_t)val_max - val_min;

    /* draw line sample by sample.
     * data live in ring buffer: oldest at data_idx, newest just before
     * (wrap). draw left (old) to right (new). */
    uint16_t prev_px = 0, prev_py = 0;
    uint8_t  first_point = 1;

    for (uint8_t i = 0; i < data_len; i++)
    {
        /* ring index, start from oldest */
        uint8_t buf_idx = (data_idx + i) % data_len;
        int16_t sample  = data[buf_idx];

        /* squash value into range */
        if (sample > val_max) sample = val_max;
        if (sample < val_min) sample = val_min;

        /* sample number -> pixel X */
        uint16_t px = x + (uint16_t)((uint32_t)i * (w - 1) / (data_len - 1));

        /* sample value -> pixel Y (flip: screen Y grow down,
         * big value must sit high) */
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
    if (active) {
        /* colours mean things: green bolt = eating power, green check =
         * full, white plug = charger attached */
        if (type == ICON_CHARGING || type == ICON_FULL) icon_color = COLOR_GREEN;
        if (type == ICON_USB_C)                         icon_color = COLOR_WHITE;
        if (type == ICON_DISCHARGING)                   icon_color = COLOR_CYAN;
        if (type == ICON_TEMP_WARN)                     icon_color = COLOR_RED;
        if (type == ICON_TEMP_COLD)                     icon_color = COLOR_CYAN;
        if (type == ICON_ALERT)                         icon_color = COLOR_DANGER;
    }

    /* Each icon is drawn as a small geometric shape (8x8 px area).
     * No bitmap images are used to save Flash memory. */
    switch (type)
    {
        case ICON_USB_C:      /* plug outline */
            GFX_DrawRoundRect(x, y + 1, 10, 6, 2, icon_color);
            break;
        case ICON_CHARGING:   /* zap bolt */
            GFX_DrawLine(x + 6, y, x + 2, y + 5, icon_color);
            GFX_DrawLine(x + 2, y + 5, x + 6, y + 4, icon_color);
            GFX_DrawLine(x + 6, y + 4, x + 3, y + 8, icon_color);
            break;
        case ICON_FULL:       /* check mark, belly full */
            GFX_DrawLine(x + 1, y + 4, x + 4, y + 7, icon_color);
            GFX_DrawLine(x + 4, y + 7, x + 9, y + 1, icon_color);
            break;
        case ICON_TEMP_OK:
        case ICON_TEMP_COLD:  /* same shape, colour say hot or cold */
        case ICON_TEMP_WARN:  /* thermometer: stem + bulb */
            GFX_DrawVLine(x + 4, y, 6, icon_color);
            ILI9341_FillRect(x + 3, y + 6, 3, 3, icon_color);
            break;
        case ICON_OUTPUT_ON:  /* full dot */
            ILI9341_FillRect(x + 2, y + 2, 6, 6, icon_color);
            break;
        case ICON_OUTPUT_OFF: /* empty dot */
            GFX_DrawRect(x + 2, y + 2, 6, 6, icon_color);
            break;
        case ICON_ALERT:      /* "!" of danger */
            ILI9341_FillRect(x + 4, y,     3, 6, icon_color);
            ILI9341_FillRect(x + 4, y + 8, 3, 2, icon_color);
            break;
        case ICON_DISCHARGING: /* arrow down, battery giving */
            GFX_DrawVLine(x + 4, y, 6, icon_color);
            GFX_DrawVLine(x + 5, y, 6, icon_color);
            GFX_DrawLine(x + 1, y + 4, x + 4, y + 8, icon_color);
            GFX_DrawLine(x + 8, y + 4, x + 5, y + 8, icon_color);
            break;
        default:
            break;
    }

    /* Text label next to the icon */
    GFX_DrawString(x + 14, y + 1, label, &GFX_FontSmall, icon_color, COLOR_BG);
}

/* =========================================================
 * WIDGET: MENU ROW
 * ========================================================= */

void Widget_Button_Draw(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                        const char *text, uint8_t selected)
{
    uint16_t tw = (uint16_t)(strlen(text) * (GFX_FONT_SMALL_W + 1) - 1);
    uint16_t tx = x + (w > tw ? (w - tw) / 2 : 0);
    uint16_t ty = y + (h > GFX_FONT_SMALL_H ? (h - GFX_FONT_SMALL_H) / 2 : 0);

    if (selected) {
        GFX_FillRoundRect(x, y, w, h, 4, COLOR_ACCENT);
        GFX_DrawString(tx, ty, text, &GFX_FontMedium, COLOR_BLACK, COLOR_ACCENT);
    } else {
        GFX_FillRoundRect(x, y, w, h, 4, COLOR_DARKGRAY);
        GFX_DrawString(tx, ty, text, &GFX_FontMedium, COLOR_WHITE, COLOR_DARKGRAY);
    }
}

void Widget_MenuRow_Draw(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                         const char *text, uint8_t selected)
{
    uint16_t text_y = y + (h > GFX_FONT_SMALL_H ? (h - GFX_FONT_SMALL_H) / 2 : 0);

    if (selected)
    {
        /* Selected row: accent background + dark text. */
        GFX_FillRoundRect(x, y, w, h, 4, COLOR_ACCENT);
        GFX_DrawString(x + 8, text_y, text, &GFX_FontMedium, COLOR_BLACK, COLOR_ACCENT);
    }
    else
    {
        /* Normal row: dark background + light text. */
        GFX_FillRoundRect(x, y, w, h, 4, COLOR_DARKGRAY);
        GFX_DrawString(x + 8, text_y, text, &GFX_FontMedium, COLOR_WHITE, COLOR_DARKGRAY);
    }
}