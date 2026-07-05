/**
 * @file    screens.c
 * @brief   all the screens. draw once full, then update moving parts.
 */

#include "ui/screens.h"
#include "ui/widgets.h"
#include "app/telemetry.h"
#include "main.h"       /* GPIO pin definitions */
#include "ui/ui_state.h"
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
#define MAIN_BATTBAR_Y   (CONTENT_Y + 12)
#define MAIN_BATTBAR_W   138
#define MAIN_BATTBAR_H   46
#define MAIN_SOC_X       156
#define MAIN_SOC_Y       (CONTENT_Y + 22)

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

static void draw_header(const char *title)
{
    ILI9341_FillRect(0, HEADER_Y, ILI9341_WIDTH, HEADER_H, COLOR_HEADER);
    GFX_DrawHLine(0, HEADER_Y + HEADER_H - 2, ILI9341_WIDTH, COLOR_ACCENT);
    GFX_DrawHLine(0, HEADER_Y + HEADER_H - 1, ILI9341_WIDTH, COLOR_ACCENT);
    GFX_DrawString(8, HEADER_Y + 10, title, &GFX_FontMedium, COLOR_WHITE, COLOR_HEADER);
    Screen_Header_RefreshIcons();
}

/* called on every periodic tick so icons track live telemetry on all screens */
void Screen_Header_RefreshIcons(void)
{
    ILI9341_FillRect(150, HEADER_Y + 4, 88, HEADER_H - 10, COLOR_HEADER);

    /* thermometer: red when hot, cyan when cold. no icon = temp good */
    if (telemetry.temp_celsius >= 45)
        Widget_StatusIcon_Draw(170, HEADER_Y + 10, ICON_TEMP_WARN, "", 1);
    else if (telemetry.temp_celsius < 10)
        Widget_StatusIcon_Draw(170, HEADER_Y + 10, ICON_TEMP_COLD, "", 1);

    /* blinking "!" = voltage very low. bad. */
    if (volt_alarm_pub(telemetry.voltage_mV) == 2 &&
        ((HAL_GetTick() / 500) & 1))
        Widget_StatusIcon_Draw(155, HEADER_Y + 10, ICON_ALERT, "", 1);

    /* USB-C plug when a supply is attached */
    if (telemetry.vbus_present)
        Widget_StatusIcon_Draw(200, HEADER_Y + 10, ICON_USB_C, "", 1);

    /* Bolt while charging, check when full, down arrow while discharging */
    if (telemetry.is_full)
        Widget_StatusIcon_Draw(185, HEADER_Y + 10, ICON_FULL, "", 1);
    else if (telemetry.is_charging)
        Widget_StatusIcon_Draw(185, HEADER_Y + 10, ICON_CHARGING, "", 1);
    else if (telemetry.current_mA < 0)
        Widget_StatusIcon_Draw(185, HEADER_Y + 10, ICON_DISCHARGING, "", 1);
}

static void draw_footer(uint8_t active_dot)
{
    ILI9341_FillRect(0, FOOTER_Y, ILI9341_WIDTH, FOOTER_H, COLOR_HEADER);

    const uint16_t dot_xs[] = {68, 94, 120, 146, 172};
    for (uint8_t i = 0; i < 5; i++)
    {
        uint16_t color = (i == active_dot) ? COLOR_ACCENT : COLOR_GRAY;
        GFX_DrawCircle(dot_xs[i], FOOTER_Y + 15, 4, color);
        if (i == active_dot)
            ILI9341_FillRect(dot_xs[i] - 2, FOOTER_Y + 13, 5, 5, color);
    }
}

static uint16_t current_color(int16_t current_mA)
{
    return (current_mA >= 0) ? COLOR_GREEN : COLOR_CYAN;
}

/* =========================================================
 * Colour helpers — shared with widgets.c
 * ========================================================= */

/* alarm thresholds, one place only, easy to poke */
#define TH_SOC_ORANGE      15     /* below this % -> orange */
#define TH_SOC_RED          5     /* below this % -> red  */
#define TH_TEMP_ORANGE     45     /* C */
#define TH_TEMP_RED        60     /* C */
#define TH_TEMP_COLD       10     /* below this C -> cyan + warning */
#define TH_VOLT_GREEN_MV   14000  /* 14-17 V: green, all good */
#define TH_VOLT_ORANGE_MV  13000  /* 13-14 V: orange, keep eye on it */
#define TH_VOLT_RED_MV     12000  /* 12-13 V red; below: red that blinks */

uint16_t battery_color_pub(uint8_t pct)
{
    if (pct < TH_SOC_RED)    return COLOR_DANGER;
    if (pct < TH_SOC_ORANGE) return COLOR_ORANGE;
    return COLOR_GREEN;
}

uint16_t temp_color_pub(int16_t temp_c)
{
    if (temp_c >= TH_TEMP_RED)    return COLOR_DANGER;
    if (temp_c >= TH_TEMP_ORANGE) return COLOR_ORANGE;
    if (temp_c <  TH_TEMP_COLD)   return COLOR_CYAN;   /* brrr, cold */
    return COLOR_WHITE;
}

/** 1 when temp outside comfort cave. show warning then. */
uint8_t temp_out_of_range_pub(int16_t temp_c)
{
    return (temp_c > TH_TEMP_RED) || (temp_c < TH_TEMP_COLD);
}

#define TH_CELL_ORANGE_MV  3250   /* per single parallel group */
#define TH_CELL_RED_MV     3050

static uint16_t cell_color(uint16_t mv)
{
    if (mv < TH_CELL_RED_MV)    return COLOR_DANGER;
    if (mv < TH_CELL_ORANGE_MV) return COLOR_ORANGE;
    return COLOR_WHITE;
}

uint16_t volt_color_pub(uint16_t mv)
{
    if (mv < TH_VOLT_RED_MV)          /* critico: rosso lampeggiante */
        return ((HAL_GetTick() / 500) & 1) ? COLOR_DANGER : COLOR_DARKGRAY;
    if (mv < TH_VOLT_ORANGE_MV) return COLOR_DANGER;
    if (mv < TH_VOLT_GREEN_MV)  return COLOR_ORANGE;
    return COLOR_GREEN;
}

/* 0 = fine, 1 = warn (12-13 V), 2 = very bad (<12 V) */
uint8_t volt_alarm_pub(uint16_t mv)
{
    if (mv < TH_VOLT_RED_MV)    return 2;
    if (mv < TH_VOLT_ORANGE_MV) return 1;
    return 0;
}

/* =========================================================
 * SCREEN: BOOT
 * ========================================================= */

#include "display/logo.h"
#include <string.h>

void Screen_Boot_Draw(void)
{
    ILI9341_FillScreen(COLOR_BG);

    /* duck logo, middle of upper half */
    GFX_DrawBitmap((ILI9341_WIDTH - LOGO_W) / 2, 42, LOGO_W, LOGO_H, logo_data);

    /* Big name, two words, stairs style, with shadow.
     * Draw shadow first (+3,+3, dark), bright word on top.
     * Careful: shadow bg = COLOR_BG so top word not eat shadow. */
#define BOOT_SHADOW_ORANGE 0x8280   /* dim orange for shadow */
    GFX_DrawStringScaled(27, 163, "DUCKER",  &GFX_FontSmall, 3, BOOT_SHADOW_ORANGE, COLOR_BG);
    GFX_DrawStringScaled(24, 160, "DUCKER",  &GFX_FontSmall, 3, COLOR_ACCENT, COLOR_TRANSPARENT_MAGIC);
    GFX_DrawStringScaled(73, 195, "CHARGER", &GFX_FontSmall, 3, COLOR_DARKGRAY, COLOR_TRANSPARENT_MAGIC);
    GFX_DrawStringScaled(70, 192, "CHARGER", &GFX_FontSmall, 3, COLOR_WHITE, COLOR_TRANSPARENT_MAGIC);

    /* small words under big name */
    GFX_DrawString(78, 232, "Smart UPS 4S3P", &GFX_FontSmall, COLOR_GRAY, COLOR_BG);

    /* decoration lines, stairs like words */
    GFX_DrawHLine(24, 152, 128, COLOR_ACCENT);
    GFX_DrawHLine(24, 154, 96,  COLOR_ACCENT);
    GFX_DrawHLine(88, 246, 128, COLOR_GRAY);
    GFX_DrawHLine(120, 248, 96, COLOR_GRAY);

    /* version number, top-left, big-ish */
    GFX_DrawStringScaled(6, 6, "v0.1", &GFX_FontSmall, 2, COLOR_GRAY, COLOR_BG);

    /* course credit at bottom (font has no accent glyphs, sorry) */
    GFX_DrawString(54, 266, "Project for the course",        &GFX_FontSmall, COLOR_DARKGRAY, COLOR_BG);
    GFX_DrawString(33, 278, "Embedded Software for the IoT", &GFX_FontSmall, COLOR_DARKGRAY, COLOR_BG);
    GFX_DrawString(60, 290, "University of Trento",          &GFX_FontSmall, COLOR_DARKGRAY, COLOR_BG);
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

    /* Big big SoC number next to battery, colour say charge level.
     * Scale 3 so even "100%" fit (4 glyphs x 21 px). */
    {
        char soc[6];
        snprintf(soc, sizeof(soc), "%u%%", telemetry.soc_percent);
        ILI9341_FillRect(MAIN_SOC_X, MAIN_SOC_Y, 240 - MAIN_SOC_X - 4, 26, COLOR_BG);
        GFX_DrawStringScaled(MAIN_SOC_X, MAIN_SOC_Y, soc, &GFX_FontSmall, 3,
                             battery_color_pub(telemetry.soc_percent), COLOR_BG);
    }

    /* how long battery last, gauge say, under SoC number */
    {
        uint16_t m = telemetry.is_charging ? ttf_min : tte_min;
        ILI9341_FillRect(MAIN_SOC_X, MAIN_SOC_Y + 30, 240 - MAIN_SOC_X - 4,
                         GFX_FONT_SMALL_H, COLOR_BG);
        if (m > 0 && m < 6000) {
            snprintf(buf, sizeof(buf), "%uh%02um %s", m / 60, m % 60,
                     telemetry.is_charging ? "to full" : "left");
            GFX_DrawString(MAIN_SOC_X, MAIN_SOC_Y + 30, buf, &GFX_FontSmall,
                           telemetry.is_charging ? COLOR_GREEN : COLOR_LIGHTGRAY,
                           COLOR_BG);
        }
    }

    /* Voltage, 2x scale */
    snprintf(buf, sizeof(buf), "%d.%02dV",
             telemetry.voltage_mV / 1000,
             (telemetry.voltage_mV % 1000) / 10);
    ILI9341_FillRect(MAIN_VOLTAGE_X, MAIN_VOLTAGE_Y + GFX_FONT_SMALL_H + 4,
                     110, GFX_FONT_SMALL_H * 2, COLOR_BG);
    GFX_DrawStringScaled(MAIN_VOLTAGE_X, MAIN_VOLTAGE_Y + GFX_FONT_SMALL_H + 4,
                         buf, &GFX_FontSmall, 2,
                         volt_color_pub(telemetry.voltage_mV), COLOR_BG);

    /* Current, 2x scale, coloured by sign */
    snprintf(buf, sizeof(buf), "%+dmA", telemetry.current_mA);
    ILI9341_FillRect(MAIN_CURRENT_X, MAIN_CURRENT_Y + GFX_FONT_SMALL_H + 4,
                     110, GFX_FONT_SMALL_H * 2, COLOR_BG);
    GFX_DrawStringScaled(MAIN_CURRENT_X, MAIN_CURRENT_Y + GFX_FONT_SMALL_H + 4,
                         buf, &GFX_FontSmall, 2,
                         current_color(telemetry.current_mA), COLOR_BG);

    /* Redraw the current history graph. */
    Widget_LineGraph_Draw(MAIN_GRAPH_X, MAIN_GRAPH_Y,
                          MAIN_GRAPH_W, MAIN_GRAPH_H,
                          telemetry.current_history,
                          TELEMETRY_HISTORY_SIZE,
                          telemetry.history_idx,
                          -15000, 15000,
                          COLOR_CYAN, COLOR_BG);

    /* Header icons are refreshed centrally by ui_state (all screens). */
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
#define DETAIL_CELLS_Y 242
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

    /* grid lines (vline stop above cells box) */
    GFX_DrawVLine(120, CONTENT_Y + 5, 200, COLOR_DARKGRAY);
    GFX_DrawHLine(10,  DETAIL_ROW_2 - 5, 220, COLOR_DARKGRAY);
    GFX_DrawHLine(10,  DETAIL_ROW_3 - 5, 220, COLOR_DARKGRAY);

    /* box for 4 parallel-group voltages */
    GFX_DrawRect(10, DETAIL_CELLS_Y, 220, 40, COLOR_DARKGRAY);
    GFX_DrawString(18, DETAIL_CELLS_Y - 4, " CELLS ",
                   &GFX_FontSmall, COLOR_LIGHTGRAY, COLOR_BG);

    draw_footer(1); /* dot 1 = DETAIL */
    Screen_Detail_Update();
}

/* big value cell: wipe area, draw number at 2x */
static void detail_value(uint16_t x, uint16_t y, const char *txt, uint16_t color)
{
    ILI9341_FillRect(x, y, 108, GFX_FONT_SMALL_H * 2, COLOR_BG);
    GFX_DrawStringScaled(x, y, txt, &GFX_FontSmall, 2, color, COLOR_BG);
}

void Screen_Detail_Update(void)
{
    char buf[20];
    /* Offset below the small label to where the large value starts.*/
    uint16_t val_y = GFX_FONT_SMALL_H + 6;

    /* SoC */
    snprintf(buf, sizeof(buf), "%d%%", telemetry.soc_percent);
    detail_value(DETAIL_COL_A, DETAIL_ROW_1 + val_y,
                 buf, battery_color_pub(telemetry.soc_percent));

    /* Voltage */
    snprintf(buf, sizeof(buf), "%d.%02dV",
             telemetry.voltage_mV / 1000,
             (telemetry.voltage_mV % 1000) / 10);
    detail_value(DETAIL_COL_B, DETAIL_ROW_1 + val_y, buf,
                 volt_color_pub(telemetry.voltage_mV));

    /* Current */
    snprintf(buf, sizeof(buf), "%+dmA", telemetry.current_mA);
    detail_value(DETAIL_COL_A, DETAIL_ROW_2 + val_y,
                 buf, current_color(telemetry.current_mA));

    /* Power */
    snprintf(buf, sizeof(buf), "%+ldmW", (long)telemetry.power_mW);
    detail_value(DETAIL_COL_B, DETAIL_ROW_2 + val_y, buf, COLOR_YELLOW);

    /* Temperature */
    snprintf(buf, sizeof(buf), "%d C", telemetry.temp_celsius);
    detail_value(DETAIL_COL_A, DETAIL_ROW_3 + val_y,
                 buf, temp_color_pub(telemetry.temp_celsius));

    /* one voltage per group, colour from per-cell thresholds */
    for (uint8_t i = 0; i < 4; i++)
    {
        uint16_t cx = 16 + i * 54;
        snprintf(buf, sizeof(buf), "%u.%02u", cell_mv[i] / 1000,
                 (cell_mv[i] % 1000) / 10);
        GFX_DrawString(cx, DETAIL_CELLS_Y + 8, (const char[]){ '1' + i, 0 },
                       &GFX_FontSmall, COLOR_GRAY, COLOR_BG);
        ILI9341_FillRect(cx, DETAIL_CELLS_Y + 22, 30, GFX_FONT_SMALL_H, COLOR_BG);
        GFX_DrawString(cx, DETAIL_CELLS_Y + 22, buf,
                       &GFX_FontSmall, cell_color(cell_mv[i]), COLOR_BG);
    }

    /* Charger state */
    const char *phase_str[] = {"IDLE", "PRE", "FAST", "TAPER"};
    const char *state_str   = telemetry.vbus_present
                              ? phase_str[telemetry.charge_phase & 0x03]
                              : "NO INPUT";
    detail_value(DETAIL_COL_B, DETAIL_ROW_3 + val_y,
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

/* full-scale cycles on press: 1 / 5 / 15 A */
static const int16_t _graph_fs[] = { 1000, 5000, 15000 };
static uint8_t _graph_fs_idx = 2;

void Screen_Graph_CycleScale(void)
{
    _graph_fs_idx = (uint8_t)((_graph_fs_idx + 1) % 3);
    Screen_Graph_Draw();
}

void Screen_Graph_Draw(void)
{
    char lbl[8];
    ILI9341_FillScreen(COLOR_BG);
    draw_header("CURRENT GRAPH");

    /* Y-axis labels (max and min values). */
    snprintf(lbl, sizeof(lbl), "+%dA", _graph_fs[_graph_fs_idx] / 1000);
    GFX_DrawString(GRAPH_AREA_X, GRAPH_AREA_Y, lbl,
                   &GFX_FontSmall, COLOR_GRAY, COLOR_BG);
    snprintf(lbl, sizeof(lbl), "-%dA", _graph_fs[_graph_fs_idx] / 1000);
    GFX_DrawString(GRAPH_AREA_X,
                   GRAPH_AREA_Y + GRAPH_AREA_H - GFX_FONT_SMALL_H, lbl,
                   &GFX_FontSmall, COLOR_GRAY, COLOR_BG);

    /* X-axis label + scale hint */
    GFX_DrawString(55, GRAPH_AREA_Y + GRAPH_AREA_H + 5,
                   "< 30 seconds >  PRESS=scale", &GFX_FontSmall, COLOR_GRAY, COLOR_BG);

    draw_footer(2); /* dot 2 = GRAPH */
    Screen_Graph_Update();
}

void Screen_Graph_Update(void)
{
    /* Full-screen current graph, range -15 A .. +15 A.
     * The first 26 px on the left are reserved for the Y-axis labels. */
    Widget_LineGraph_Draw(GRAPH_AREA_X + 26, GRAPH_AREA_Y,
                          GRAPH_AREA_W - 26, GRAPH_AREA_H,
                          telemetry.current_history,
                          TELEMETRY_HISTORY_SIZE,
                          telemetry.history_idx,
                          (int16_t)-_graph_fs[_graph_fs_idx],
                          _graph_fs[_graph_fs_idx],
                          COLOR_CYAN, COLOR_BG);

    /* Instantaneous current value top-right of the graph area.*/
    char buf[16];
    snprintf(buf, sizeof(buf), "%+d mA", telemetry.current_mA);
    ILI9341_FillRect(140, CONTENT_Y + 5, 90, GFX_FONT_MEDIUM_H, COLOR_BG);
    GFX_DrawString(140, CONTENT_Y + 5, buf, &GFX_FontMedium,
                   current_color(telemetry.current_mA), COLOR_BG);
}

/* =========================================================
 * SCREEN: PORTS — one graph, five output traces
 * (USB-A1, USB-A2, USB-C1 OTG, USB-C2, Lab) + legend below
 * ========================================================= */

#define PORTS_GRAPH_X    10
#define PORTS_GRAPH_Y    (CONTENT_Y + 14)
#define PORTS_GRAPH_W    220
#define PORTS_GRAPH_H    130
#define PORTS_LEG_Y      (PORTS_GRAPH_Y + PORTS_GRAPH_H + 12)
#define PORTS_LEG_ROW_H  19
#define PORTS_I_MAX      3000

static const struct { const char *name; uint16_t color; } _port_meta[5] = {
    { "USB-A 1",  COLOR_PORT_A1 },
    { "USB-A 2",  COLOR_PORT_A2 },
    { "USB-C 1",  COLOR_WHITE   },
    { "USB-C 2",  COLOR_PORT_C2 },
    { "LAB",      COLOR_PORT_LAB },
};

static void ports_graph(void)
{
    /* Clear + frame once, then overlay one trace per active port. */
    ILI9341_FillRect(PORTS_GRAPH_X, PORTS_GRAPH_Y,
                     PORTS_GRAPH_W, PORTS_GRAPH_H, COLOR_BG);
    GFX_DrawRect(PORTS_GRAPH_X, PORTS_GRAPH_Y,
                 PORTS_GRAPH_W, PORTS_GRAPH_H, COLOR_GRAY);

    for (uint8_t i = 0; i < 5; i++)
    {
        if (!port_stats[i].active) continue;
        Widget_LineGraph_Trace(PORTS_GRAPH_X + 1, PORTS_GRAPH_Y + 1,
                               PORTS_GRAPH_W - 2, PORTS_GRAPH_H - 2,
                               port_stats[i].history,
                               TELEMETRY_HISTORY_SIZE, port_stats[i].idx,
                               0, PORTS_I_MAX, _port_meta[i].color);
    }
}

static void ports_legend_row(uint8_t i)
{
    char buf[24];
    uint16_t y = PORTS_LEG_Y + i * PORTS_LEG_ROW_H;
    PortStats_t *ps = &port_stats[i];
    uint16_t c = ps->active ? _port_meta[i].color : COLOR_DARKGRAY;

    /* colour square, same colour as trace */
    ILI9341_FillRect(10, y, 10, 10, c);

    GFX_DrawString(26, y + 1, _port_meta[i].name, &GFX_FontSmall,
                   ps->active ? COLOR_WHITE : COLOR_GRAY, COLOR_BG);

    if (ps->active)
        snprintf(buf, sizeof(buf), "%2u.%uV %4dmA",
                 ps->voltage_mv / 1000, (ps->voltage_mv % 1000) / 100,
                 ps->current_mA);
    else
        snprintf(buf, sizeof(buf), "%12s", "idle");
    ILI9341_FillRect(130, y + 1, 104, GFX_FONT_SMALL_H, COLOR_BG);
    GFX_DrawString(130, y + 1, buf, &GFX_FontSmall,
                   ps->active ? c : COLOR_GRAY, COLOR_BG);
}

void Screen_Ports_Draw(void)
{
    ILI9341_FillScreen(COLOR_BG);
    draw_header("PORT MONITOR");

    /* axes: full scale and time window */
    GFX_DrawString(PORTS_GRAPH_X, PORTS_GRAPH_Y - 10, "3A",
                   &GFX_FontSmall, COLOR_GRAY, COLOR_BG);
    GFX_DrawString(160, PORTS_GRAPH_Y + PORTS_GRAPH_H + 2, "< 30 s >",
                   &GFX_FontSmall, COLOR_GRAY, COLOR_BG);

    ports_graph();
    for (uint8_t i = 0; i < 5; i++) ports_legend_row(i);
    draw_footer(3); /* dot 3 = PORTS */
}

void Screen_Ports_Update(void)
{
    ports_graph();
    for (uint8_t i = 0; i < 5; i++) ports_legend_row(i);
}

/* =========================================================
 * SCREEN: STATS — lifetime and session numbers
 * ========================================================= */

#define STATS_ROW_H     26
#define STATS_START_Y   (CONTENT_Y + 24)
#define STATS_VAL_X     150

typedef struct { const char *label; void (*fmt)(char *, size_t); uint16_t color; } StatsRow_t;

static void f_cycles(char *b, size_t n)  { snprintf(b, n, "%u",        sys_stats.cycle_count); }
static void f_soh(char *b, size_t n)     { snprintf(b, n, "%u%%",      sys_stats.state_of_health); }
static void f_cap(char *b, size_t n)     { snprintf(b, n, "%u/%umAh",  sys_stats.full_cap_mAh, sys_stats.design_cap_mAh); }
static void f_sess(char *b, size_t n)    { snprintf(b, n, "%u",        sys_stats.charge_sessions); }
static void f_maxt(char *b, size_t n)    { snprintf(b, n, "%d C",      sys_stats.max_temp_c); }
static void f_maxout(char *b, size_t n)  { snprintf(b, n, "%d mA",     sys_stats.max_current_out_mA); }
static void f_maxin(char *b, size_t n)   { snprintf(b, n, "%d mA",     sys_stats.max_current_in_mA); }
static void f_energy(char *b, size_t n)  { snprintf(b, n, "%lu.%luWh", (unsigned long)(sys_stats.energy_out_mWh / 1000), (unsigned long)((sys_stats.energy_out_mWh % 1000) / 100)); }
static void f_uptime(char *b, size_t n)  { snprintf(b, n, "%luh %02lum", (unsigned long)(sys_stats.uptime_s / 3600), (unsigned long)((sys_stats.uptime_s / 60) % 60)); }

static const StatsRow_t _stats_rows[] = {
    { "Charge cycles",  f_cycles, COLOR_WHITE  },
    { "Battery health", f_soh,    COLOR_GREEN  },
    { "Capacity",       f_cap,    COLOR_WHITE  },
    { "Charges (boot)", f_sess,   COLOR_WHITE  },
    { "Max temp",       f_maxt,   COLOR_YELLOW },
    { "Max out",        f_maxout, COLOR_CYAN   },
    { "Max in",         f_maxin,  COLOR_GREEN  },
    { "Energy out",     f_energy, COLOR_CYAN   },
    { "Uptime",         f_uptime, COLOR_GRAY   },
};
#define STATS_NUM_ROWS (sizeof(_stats_rows) / sizeof(_stats_rows[0]))

#define STATS_SECTION_GAP 18
static uint16_t stats_row_y(uint8_t i)
{
    return STATS_START_Y + i * STATS_ROW_H + (i >= 3 ? STATS_SECTION_GAP : 0);
}

void Screen_Stats_Update(void)
{
    char buf[20];
    for (uint8_t i = 0; i < STATS_NUM_ROWS; i++)
    {
        uint16_t y = stats_row_y(i);
        _stats_rows[i].fmt(buf, sizeof(buf));
        uint16_t col = _stats_rows[i].color;
        if (_stats_rows[i].fmt == f_maxt)          /* temp threshold colour */
            col = temp_color_pub(sys_stats.max_temp_c);
        ILI9341_FillRect(STATS_VAL_X, y, ILI9341_WIDTH - STATS_VAL_X - 4,
                         GFX_FONT_SMALL_H, COLOR_BG);
        GFX_DrawString(STATS_VAL_X, y, buf, &GFX_FontSmall, col, COLOR_BG);
    }
}

void Screen_Stats_Draw(void)
{
    ILI9341_FillScreen(COLOR_BG);
    draw_header("OVERALL DATA");

    /* section labels: gauge remember forever vs since-boot only */
    GFX_DrawString(10, CONTENT_Y + 8, "BATTERY (lifetime)",
                   &GFX_FontSmall, COLOR_ACCENT, COLOR_BG);

    for (uint8_t i = 0; i < STATS_NUM_ROWS; i++)
    {
        uint16_t y = stats_row_y(i);
        if (i == 3)   /* line between gauge section and session section */
        {
            GFX_DrawHLine(10, y - STATS_SECTION_GAP + 4, 220, COLOR_DARKGRAY);
            GFX_DrawString(10, y - STATS_SECTION_GAP + 8, "SESSION",
                           &GFX_FontSmall, COLOR_ACCENT, COLOR_BG);
        }
        GFX_DrawString(10, y, _stats_rows[i].label,
                       &GFX_FontSmall, COLOR_LIGHTGRAY, COLOR_BG);
    }

    Screen_Stats_Update();
    draw_footer(4); /* dot 4 = STATS */
}

/* =========================================================
 * SCREEN: DISPLAY PAGE — brightness, screen off, light sleep
 * ========================================================= */

#define SET_LIST_START      (CONTENT_Y + 6)   /* same value as settings list */

static uint8_t _disp_row  = 0;   /* 0=Lum 1=AutoSleep 2=Screen off 3=Light sleep 4=Back */
static uint8_t _disp_edit = 0;

/* auto-sleep: index into {Off, 1, 5, 15 min} */
static const uint16_t _asleep_min[] = { 0, 1, 5, 15 };
static uint8_t _asleep_idx = 2;   /* default 5 min */
uint32_t Screen_Display_GetAutoSleepMs(void)
{ return (uint32_t)_asleep_min[_asleep_idx] * 60000u; }

static void disp_draw_rows(void)
{
    char buf[28], as[8];

    if (_asleep_min[_asleep_idx] == 0) snprintf(as, sizeof(as), "Off");
    else snprintf(as, sizeof(as), "%um", _asleep_min[_asleep_idx]);

    snprintf(buf, sizeof(buf), (_disp_edit && _disp_row == 0)
             ? "%-10s <%3u%%>" : "%-12s %3u%%",
             "Luminosity", ILI9341_GetBrightness());
    Widget_MenuRow_Draw(10, SET_LIST_START,       220, 36, buf, (_disp_row == 0));

    snprintf(buf, sizeof(buf), (_disp_edit && _disp_row == 1)
             ? "%-10s <%s>" : "%-12s %s", "Auto sleep", as);
    Widget_MenuRow_Draw(10, SET_LIST_START + 42,  220, 36, buf, (_disp_row == 1));

    Widget_MenuRow_Draw(10, SET_LIST_START + 84,  220, 36, "Screen off",  (_disp_row == 2));
    Widget_MenuRow_Draw(10, SET_LIST_START + 126, 220, 36, "Light sleep", (_disp_row == 3));
    Widget_MenuRow_Draw(10, SET_LIST_START + 168, 220, 36, "< Back",      (_disp_row == 4));

    ILI9341_FillRect(0, FOOTER_Y, ILI9341_WIDTH, FOOTER_H, COLOR_HEADER);
    GFX_DrawString(10, FOOTER_Y + 11,
                   _disp_edit ? "ROTATE=value  PRESS=set" : "PRESS=select",
                   &GFX_FontSmall, COLOR_GRAY, COLOR_HEADER);
}

void Screen_Display_Open(void) { _disp_row = 0; _disp_edit = 0; }

void Screen_Display_Draw(void)
{
    ILI9341_FillScreen(COLOR_BG);
    draw_header("DISPLAY");
    disp_draw_rows();
}

void Screen_Display_OnRotate(int8_t d)
{
    if (_disp_edit && _disp_row == 0) {
        int v = ILI9341_GetBrightness() + d * 10;
        ILI9341_SetBrightness((uint8_t)(v < 10 ? 10 : (v > 100 ? 100 : v)));
        /* on real board: poke backlight PWM duty here */
    } else if (_disp_edit && _disp_row == 1) {
        _asleep_idx = (uint8_t)((_asleep_idx + (d > 0 ? 1 : 3)) % 4);
    } else {
        _disp_row = (uint8_t)((_disp_row + (d > 0 ? 1 : 4)) % 5);
    }
    disp_draw_rows();
}

void Screen_Display_OnPress(void)
{
    if (_disp_edit) { _disp_edit = 0; disp_draw_rows(); return; }
    switch (_disp_row) {
        case 0:
        case 1: _disp_edit = 1; disp_draw_rows(); break;
        case 2: UI_EnterSleep(); break;
        case 3: Screen_Confirm_Open(CONFIRM_LIGHTSLEEP);
                UI_OpenConfirm(); break;
        case 4: UI_NavigateTo(UI_SCREEN_SETTINGS); break;
    }
}

/* =========================================================
 * SCREEN: TEST PAGE — pretend emergencies
 * write fake numbers into telemetry, watch UI react
 * (colours, warnings, graphs). sim-only concept.
 * ========================================================= */

static uint8_t _test_row = 0;

typedef struct { const char *name; void (*apply)(void); } TestRow_t;

static void t_lowv(void)
{
    telemetry.voltage_mV = 12500; telemetry.soc_percent = 12;
    telemetry.current_mA = -900;
    for (int i = 0; i < 4; i++) cell_mv[i] = 3125;
    telemetry.is_charging = 0;
    UI_RearmWarning();
}
static void t_vcrit(void)
{
    telemetry.voltage_mV = 11800; telemetry.soc_percent = 3;
    telemetry.current_mA = -1200;
    for (int i = 0; i < 4; i++) cell_mv[i] = 2950;
    telemetry.is_charging = 0;
    UI_RearmWarning();
}
static void t_lowt(void)  { telemetry.temp_celsius = -8; UI_RearmWarning(); }
static void t_hight(void) { telemetry.temp_celsius = 72; UI_RearmWarning(); }
static void t_overc(void)
{
    telemetry.current_mA = -14500;
    for (int i = 0; i < TELEMETRY_HISTORY_SIZE; i++)
        telemetry.current_history[i] = (int16_t)(-14500 + (i * 137) % 900);
    telemetry.is_charging = 0;
}
static void t_reset(void)
{
    Telemetry_ForcePoll();
}

static const TestRow_t _test_rows[] = {
    { "Low voltage",    t_lowv  },
    { "Critical low V", t_vcrit },
    { "Low temp",       t_lowt  },
    { "High temp",      t_hight },
    { "Overcurrent",    t_overc },
    { "Reset data",     t_reset },
    { "< Back",         0       },
};
#define TEST_NUM_ROWS (sizeof(_test_rows) / sizeof(_test_rows[0]))

static void test_draw_rows(void)
{
    for (uint8_t i = 0; i < TEST_NUM_ROWS; i++)
        Widget_MenuRow_Draw(10, SET_LIST_START + i * 34, 220, 30,
                            _test_rows[i].name, (i == _test_row));

    ILI9341_FillRect(0, FOOTER_Y, ILI9341_WIDTH, FOOTER_H, COLOR_HEADER);
    GFX_DrawString(10, FOOTER_Y + 11, "PRESS=apply scenario",
                   &GFX_FontSmall, COLOR_GRAY, COLOR_HEADER);
}

void Screen_Test_Open(void) { _test_row = 0; }

void Screen_Test_Draw(void)
{
    ILI9341_FillScreen(COLOR_BG);
    draw_header("TEST MODES");
    test_draw_rows();
}

void Screen_Test_OnRotate(int8_t d)
{
    _test_row = (uint8_t)((_test_row + (d > 0 ? 1 : TEST_NUM_ROWS - 1))
                          % TEST_NUM_ROWS);
    test_draw_rows();
}

void Screen_Test_OnPress(void)
{
    if (_test_rows[_test_row].apply == 0) {      /* Back */
        UI_NavigateTo(UI_SCREEN_SETTINGS);
        return;
    }
    _test_rows[_test_row].apply();
    /* feedback: row flash so user know it happened */
    Widget_Button_Draw(10, SET_LIST_START + _test_row * 34, 220, 30,
                       "APPLIED", 1);
}

/* =========================================================
 * SCREEN: CONFIRM — are-you-sure gate for big actions
 * ========================================================= */

static ConfirmAction_t _confirm_action = CONFIRM_LOCKALL;
static uint8_t         _confirm_sel    = 1;   /* 0=OK 1=Cancel (default sicuro) */

static const char *_confirm_title[] = {
    [CONFIRM_LOCKALL]    = "LOCK ALL PORTS?",
    [CONFIRM_LIGHTSLEEP] = "LIGHT SLEEP?",
};
void Screen_Confirm_Open(ConfirmAction_t action)
{
    _confirm_action = action;
    _confirm_sel    = 1;
}

ConfirmAction_t Screen_Confirm_GetAction(void) { return _confirm_action; }

static void confirm_draw_buttons(void)
{
    Widget_Button_Draw(25, 200, 90, 34, "OK",     (_confirm_sel == 0));
    Widget_Button_Draw(125, 200, 90, 34, "Cancel", (_confirm_sel == 1));
}

void Screen_Confirm_Draw(void)
{
    ILI9341_FillScreen(COLOR_BG);

    const char *t = _confirm_title[_confirm_action];
    GFX_DrawStringScaled((uint16_t)((240 - strlen(t) * 14) / 2), 90, t,
                         &GFX_FontSmall, 2, COLOR_ACCENT, COLOR_BG);

    if (_confirm_action == CONFIRM_LOCKALL)
        GFX_DrawString(30, 140, "All outputs except USB-C1",
                       &GFX_FontSmall, COLOR_WHITE, COLOR_BG),
        GFX_DrawString(75, 152, "will be closed.",
                       &GFX_FontSmall, COLOR_WHITE, COLOR_BG);
    else
        GFX_DrawString(27, 140, "Ports locked and screen off",
                       &GFX_FontSmall, COLOR_WHITE, COLOR_BG),
        GFX_DrawString(48, 152, "until encoder press.",
                       &GFX_FontSmall, COLOR_WHITE, COLOR_BG);

    confirm_draw_buttons();
}

void Screen_Confirm_OnRotate(int8_t d)
{
    (void)d;
    _confirm_sel ^= 1;
    confirm_draw_buttons();
}

uint8_t Screen_Confirm_OnPress(void)
{
    return (_confirm_sel == 0) ? 1 : 2;
}

/* =========================================================
 * SCREEN: WARNING — something wrong, tell user once per boot
 * OK to acknowledge. critical voltage also force light sleep.
 * ========================================================= */

static WarnType_t _warn_type = WARN_TEMP;

void Screen_Warning_Open(WarnType_t type) { _warn_type = type; }

typedef struct {
    const char *title;
    const char *line[3];
} WarnText_t;

static const WarnText_t _warn_text[WARN_COUNT] = {
    [WARN_TEMP]  = { "TEMP WARNING",
        { "Battery outside its comfort", "range: performance and",
          "capacity may be reduced." } },
    [WARN_VLOW]  = { "LOW VOLTAGE",
        { "Battery voltage is low:", "recharge soon.", "" } },
    [WARN_VCRIT] = { "CRITICAL BATTERY",
        { "Critically low battery.", "Going into light sleep",
          "to avoid cell damage." } },
};

void Screen_Warning_Draw(void)
{
    char buf[24];
    const WarnText_t *w = &_warn_text[_warn_type];
    uint16_t wc = (_warn_type == WARN_VCRIT) ? COLOR_DANGER : COLOR_ORANGE;

    ILI9341_FillScreen(COLOR_BG);

    /* warning triangle, thick border (3 passes), big "!" inside */
    for (uint8_t t = 0; t < 3; t++) {
        GFX_DrawLine(120, 52 + t, 68 + t, 148 - t, wc);
        GFX_DrawLine(120, 52 + t, 172 - t, 148 - t, wc);
        GFX_DrawLine(70, 146 + t, 170, 146 + t, wc);
    }
    GFX_DrawStringScaled(109, 90, "!", &GFX_FontSmall, 5, wc, COLOR_BG);

    /* centred title: x = (240 - strlen*14) / 2 (scale 2 glyph = 14 px) */
    GFX_DrawStringScaled((uint16_t)((240 - strlen(w->title) * 14) / 2), 168,
                         w->title, &GFX_FontSmall, 2, wc, COLOR_BG);

    /* centred lines: x = (240 - strlen*6) / 2 */
    for (uint8_t i = 0; i < 3; i++)
        if (w->line[i][0])
            GFX_DrawString((uint16_t)((240 - strlen(w->line[i]) * 6) / 2),
                           200 + i * 12, w->line[i],
                           &GFX_FontSmall, COLOR_WHITE, COLOR_BG);

    /* the number that matters */
    if (_warn_type == WARN_TEMP) {
        snprintf(buf, sizeof(buf), "%d C", telemetry.temp_celsius);
        GFX_DrawStringScaled(96, 240, buf, &GFX_FontSmall, 2,
                             temp_color_pub(telemetry.temp_celsius), COLOR_BG);
    } else {
        snprintf(buf, sizeof(buf), "%u.%02u V", telemetry.voltage_mV / 1000,
                 (telemetry.voltage_mV % 1000) / 10);
        GFX_DrawStringScaled(85, 240, buf, &GFX_FontSmall, 2,
                             COLOR_DANGER, COLOR_BG);
    }

    /* OK always selected: press = done */
    Widget_Button_Draw(80, 272, 80, 30, "OK", 1);
}

/* =========================================================
 * SCREEN: SLEEP — screen dark, encoder press wake it
 * ========================================================= */

void Screen_Sleep_Draw(void)
{
    /* on real board: send DISPOFF (0x28) + kill backlight PB8 here,
     * on wake DISPON (0x29) + backlight on. in sim: just black. */
    ILI9341_FillScreen(COLOR_BLACK);
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
    { "USB-A 2",    USB_A2_CTRL_GPIO_Port,    USB_A2_CTRL_Pin,    0 },
    { "Lab output", C2_LAB_EN_GPIO_Port,    C2_LAB_EN_Pin,    0 },
    { "USB-C 2",    C2_PORT_EN_GPIO_Port, C2_PORT_EN_Pin, 0 },
};

/* =========================================================
 * OUTPUT CHANNEL PAGE (Lab / USB-C2)
 *
 * one programmable buck (STPD01) feed both loads through two
 * FET switches -> channels never both on.
 *   - Lab:     set output voltage direct (STPD01 VOUT, 3-20 V)
 *   - USB-C2:  ceiling on PD voltage bargain (PDO mask)
 * ========================================================= */

typedef struct {
    const char   *title;
    const char   *volt_label;   /* "Voltage" vs "Max PD volt" */
    GPIO_TypeDef *gpio_port;    /* enable FET switch */
    uint16_t      gpio_pin;
    uint8_t       enabled;
    uint16_t      voltage_mv;
    uint16_t      ilim_ma;      /* current limit, STPD01 reg ILIM 0x01 */
    uint8_t       pdo_idx;      /* only used by C2 */
} OutputChannel_t;

#define ILIM_MIN_MA   100
#define ILIM_MAX_MA  3000
#define ILIM_STEP_MA  100

#define SETTINGS_ROW_H      50
#define SETTINGS_ROW_START  (CONTENT_Y + 10)

static const uint16_t _c2_pdo_mv[] = { 5000, 9000, 12000, 15000, 20000 };
#define C2_PDO_COUNT  (sizeof(_c2_pdo_mv) / sizeof(_c2_pdo_mv[0]))
#define LAB_V_MIN   3000
#define LAB_V_MAX  20000
#define LAB_V_STEP   100

static OutputChannel_t _out_ch[2] = {
    { "LAB OUTPUT", "Voltage",     C2_LAB_EN_GPIO_Port,    C2_LAB_EN_Pin,    0, 5000,  3000, 0 },
    { "USB-C 2",    "Max PD volt", C2_PORT_EN_GPIO_Port, C2_PORT_EN_Pin, 0, 20000, 3000, C2_PDO_COUNT - 1 },
};

static uint8_t _out_cur  = 0;   /* which channel the page is showing */
static uint8_t _out_row  = 0;   /* 0=ENABLE 1=VOLTAGE 2=CURRENT LIM 3=BACK */
static uint8_t _out_edit = 0;   /* 1 = encoder edits the selected value */
#define OUT_ROWS 4

static void out_apply_enable(uint8_t ch)
{
    /* interlock: two loads share one STPD01 rail, never both on */
    if (_out_ch[ch].enabled) {
        _out_ch[!ch].enabled = 0;
        HAL_GPIO_WritePin(_out_ch[!ch].gpio_port, _out_ch[!ch].gpio_pin, GPIO_PIN_RESET);
    }
    HAL_GPIO_WritePin(_out_ch[ch].gpio_port, _out_ch[ch].gpio_pin,
                      _out_ch[ch].enabled ? GPIO_PIN_SET : GPIO_PIN_RESET);
    /* TODO hardware: call charge-management module here
     * (talk PD via STPD01 / STUSB4710), not naked GPIO poke. */
}

static void out_fmt_volt(char *buf, size_t n, uint16_t mv)
{
    snprintf(buf, n, "%2u.%uV", mv / 1000, (mv % 1000) / 100);
}

const char *Screen_Output_RowStatus(uint8_t channel)
{
    static char st[2][16];
    char v[8];
    out_fmt_volt(v, sizeof(v), _out_ch[channel].voltage_mv);
    snprintf(st[channel], sizeof(st[channel]), "%s %s",
             _out_ch[channel].enabled ? "ON " : "OFF", v);
    return st[channel];
}

uint8_t Screen_Settings_IsOutputRow(uint8_t row) { return row >= 2; }

void Screen_Output_Open(uint8_t channel)
{
    _out_cur  = (channel < 2) ? channel : 0;
    _out_row  = 0;
    _out_edit = 0;
}

/* ---- LAB page, bench power supply style ----
 * Encoder rows: 0=OUTPUT 1=V-SET 2=I-LIM 3=Back (same FSM as C2 page).
 * Small CV/CC model for demo: load want more than I-LIM -> current
 * stuck at limit, voltage sag. that is CC. */
static void lab_bench_render(void)
{
    OutputChannel_t *ch = &_out_ch[0];
    char buf[24];

    /* fake load from port monitor (0 when port idle in config) */
    int32_t i_load = port_stats[4].current_mA;
    if (i_load < 0) i_load = 0;

    uint8_t  cc    = (ch->enabled && i_load >= ch->ilim_ma);
    int32_t  i_out = ch->enabled ? (cc ? ch->ilim_ma : i_load) : 0;
    int32_t  v_out = !ch->enabled ? 0
                   : (cc ? (int32_t)ch->voltage_mv * ch->ilim_ma / i_load
                         : ch->voltage_mv);
    int32_t  w_out = v_out * i_out / 1000;   /* mW */

    /* wipe content area */
    ILI9341_FillRect(0, CONTENT_Y + 2, ILI9341_WIDTH, FOOTER_Y - CONTENT_Y - 2,
                     COLOR_BG);

    /* mode badge */
    {
        const char *m = !ch->enabled ? "OFF" : (cc ? "CC" : "CV");
        uint16_t   mc = !ch->enabled ? COLOR_DARKGRAY
                                     : (cc ? COLOR_ORANGE : COLOR_GREEN);
        GFX_FillRoundRect(178, CONTENT_Y + 8, 52, 22, 4, mc);
        GFX_DrawString(178 + (52 - strlen(m) * 7) / 2, CONTENT_Y + 15, m,
                       &GFX_FontSmall, COLOR_BLACK, mc);
    }
    GFX_DrawString(10, CONTENT_Y + 15, "Bench supply - STPD01",
                   &GFX_FontSmall, COLOR_GRAY, COLOR_BG);

    /* big live numbers: V / A / W */
    snprintf(buf, sizeof(buf), "%2ld.%02ld", (long)(v_out / 1000),
             (long)((v_out % 1000) / 10));
    GFX_DrawStringScaled(24, CONTENT_Y + 44, buf, &GFX_FontSmall, 4,
                         ch->enabled ? COLOR_WHITE : COLOR_DARKGRAY, COLOR_BG);
    GFX_DrawStringScaled(178, CONTENT_Y + 52, "V", &GFX_FontSmall, 2,
                         COLOR_GRAY, COLOR_BG);

    snprintf(buf, sizeof(buf), "%2ld.%02ld", (long)(i_out / 1000),
             (long)((i_out % 1000) / 10));
    GFX_DrawStringScaled(24, CONTENT_Y + 84, buf, &GFX_FontSmall, 4,
                         !ch->enabled ? COLOR_DARKGRAY
                                      : (cc ? COLOR_ORANGE : COLOR_CYAN), COLOR_BG);
    GFX_DrawStringScaled(178, CONTENT_Y + 92, "A", &GFX_FontSmall, 2,
                         COLOR_GRAY, COLOR_BG);

    snprintf(buf, sizeof(buf), "%2ld.%01ld W", (long)(w_out / 1000),
             (long)((w_out % 1000) / 100));
    GFX_DrawStringScaled(24, CONTENT_Y + 126, buf, &GFX_FontSmall, 2,
                         ch->enabled ? COLOR_YELLOW : COLOR_DARKGRAY, COLOR_BG);

    GFX_DrawHLine(10, CONTENT_Y + 156, 220, COLOR_DARKGRAY);

    /* setpoints V-SET and I-LIM, <> marks the one in edit */
    if (_out_edit && _out_row == 1)
        snprintf(buf, sizeof(buf), "<%u.%uV>", ch->voltage_mv / 1000,
                 (ch->voltage_mv % 1000) / 100);
    else
        snprintf(buf, sizeof(buf), "%u.%uV", ch->voltage_mv / 1000,
                 (ch->voltage_mv % 1000) / 100);
    GFX_DrawString(10, CONTENT_Y + 166, "V-SET", &GFX_FontSmall,
                   (_out_row == 1) ? COLOR_ACCENT : COLOR_LIGHTGRAY, COLOR_BG);
    GFX_DrawStringScaled(10, CONTENT_Y + 178, buf, &GFX_FontSmall, 2,
                         (_out_row == 1) ? COLOR_ACCENT : COLOR_WHITE, COLOR_BG);

    if (_out_edit && _out_row == 2)
        snprintf(buf, sizeof(buf), "<%u.%uA>", ch->ilim_ma / 1000,
                 (ch->ilim_ma % 1000) / 100);
    else
        snprintf(buf, sizeof(buf), "%u.%uA", ch->ilim_ma / 1000,
                 (ch->ilim_ma % 1000) / 100);
    GFX_DrawString(130, CONTENT_Y + 166, "I-LIM", &GFX_FontSmall,
                   (_out_row == 2) ? COLOR_ACCENT : COLOR_LIGHTGRAY, COLOR_BG);
    GFX_DrawStringScaled(130, CONTENT_Y + 178, buf, &GFX_FontSmall, 2,
                         (_out_row == 2) ? COLOR_ACCENT : COLOR_WHITE, COLOR_BG);

    /* buttons: OUTPUT and Back */
    Widget_Button_Draw(10, CONTENT_Y + 212, 105, 34,
                       ch->enabled ? "OUT [ON]" : "OUT [OFF]", (_out_row == 0));
    Widget_Button_Draw(125, CONTENT_Y + 212, 105, 34, "< Back", (_out_row == 3));

    /* hint bar */
    ILI9341_FillRect(0, FOOTER_Y, ILI9341_WIDTH, FOOTER_H, COLOR_HEADER);
    GFX_DrawString(10, FOOTER_Y + 11,
                   _out_edit ? "ROTATE=value  PRESS=set"
                             : "PRESS=select  2xCLICK=back",
                   &GFX_FontSmall, COLOR_GRAY, COLOR_HEADER);
}

static void out_draw_rows(void)
{
    if (_out_cur == 0) { lab_bench_render(); return; }

    OutputChannel_t *ch = &_out_ch[_out_cur];
    char buf[32], v[8];

    /* Row 0: enable */
    snprintf(buf, sizeof(buf), "%-12s [%s]", "Enable", ch->enabled ? "ON " : "OFF");
    Widget_MenuRow_Draw(10, SETTINGS_ROW_START, 220, SETTINGS_ROW_H - 4,
                        buf, (_out_row == 0));

    /* Row 1: voltage — in edit mode the value gets < > markers */
    out_fmt_volt(v, sizeof(v), ch->voltage_mv);
    if (_out_edit && _out_row == 1)
        snprintf(buf, sizeof(buf), "%-10s <%s>", ch->volt_label, v);
    else
        snprintf(buf, sizeof(buf), "%-12s %s", ch->volt_label, v);
    Widget_MenuRow_Draw(10, SETTINGS_ROW_START + SETTINGS_ROW_H, 220, SETTINGS_ROW_H - 4,
                        buf, (_out_row == 1));

    /* Row 2: current limit (STPD01 ILIM) */
    if (_out_edit && _out_row == 2)
        snprintf(buf, sizeof(buf), "%-10s <%u.%uA>", "Curr lim",
                 ch->ilim_ma / 1000, (ch->ilim_ma % 1000) / 100);
    else
        snprintf(buf, sizeof(buf), "%-12s %u.%uA", "Curr lim",
                 ch->ilim_ma / 1000, (ch->ilim_ma % 1000) / 100);
    Widget_MenuRow_Draw(10, SETTINGS_ROW_START + 2 * SETTINGS_ROW_H, 220, SETTINGS_ROW_H - 4,
                        buf, (_out_row == 2));

    /* Row 3: back */
    Widget_MenuRow_Draw(10, SETTINGS_ROW_START + 3 * SETTINGS_ROW_H, 220, SETTINGS_ROW_H - 4,
                        "< Back", (_out_row == 3));

    /* Footer hint */
    ILI9341_FillRect(0, FOOTER_Y, ILI9341_WIDTH, FOOTER_H, COLOR_DARKGRAY);
    GFX_DrawString(10, FOOTER_Y + 11,
                   _out_edit ? "ROTATE=value  PRESS=set"
                             : "PRESS=select",
                   &GFX_FontSmall, COLOR_GRAY, COLOR_DARKGRAY);
}

void Screen_Output_Draw(void)
{
    ILI9341_FillScreen(COLOR_BG);
    draw_header(_out_ch[_out_cur].title);

    if (_out_cur != 0)
        GFX_DrawString(10, CONTENT_Y - 8, "PD source ceiling - STUSB4710",
                       &GFX_FontSmall, COLOR_GRAY, COLOR_BG);

    out_draw_rows();
}

void Screen_Output_Update(void)
{
    /* periodic refresh of live numbers (Lab page only) */
    if (_out_cur == 0) lab_bench_render();
}

void Screen_Output_OnRotate(int8_t delta)
{
    OutputChannel_t *ch = &_out_ch[_out_cur];

    if (_out_edit) {
        /* speed boost: spin fast (big delta) -> step x5 */
        int16_t d = delta;
        if (d >= 3 || d <= -3) d *= 5;

        if (_out_row == 2) {   /* current limit, same buck feeds both channels */
            int32_t v = (int32_t)ch->ilim_ma + d * ILIM_STEP_MA;
            if (v < ILIM_MIN_MA) v = ILIM_MIN_MA;
            if (v > ILIM_MAX_MA) v = ILIM_MAX_MA;
            ch->ilim_ma = (uint16_t)v;
        }
        else if (_out_cur == 0) {   /* Lab: continuous 3-20 V, 0.1 V steps */
            int32_t v = (int32_t)ch->voltage_mv + d * LAB_V_STEP;
            if (v < LAB_V_MIN) v = LAB_V_MIN;
            if (v > LAB_V_MAX) v = LAB_V_MAX;
            ch->voltage_mv = (uint16_t)v;
        } else {               /* C2: discrete PDO steps */
            int8_t i = (int8_t)ch->pdo_idx + (delta > 0 ? 1 : -1);
            if (i < 0) i = 0;
            if (i >= (int8_t)C2_PDO_COUNT) i = C2_PDO_COUNT - 1;
            ch->pdo_idx = (uint8_t)i;
            ch->voltage_mv = _c2_pdo_mv[i];
        }
    } else {
        _out_row = (uint8_t)((_out_row + (delta > 0 ? 1 : OUT_ROWS - 1)) % OUT_ROWS);
    }
    out_draw_rows();
}

void Screen_Output_OnPress(void)
{
    OutputChannel_t *ch = &_out_ch[_out_cur];

    if (_out_edit) {                 /* confirm value */
        _out_edit = 0;
        /* TODO hardware: Charge_SetLabVoltage(ch->voltage_mv) /
         *                Charge_SetC2MaxPDO(ch->pdo_idx) /
         *                Charge_SetIlim(ch->ilim_ma) -> STPD01 reg ILIM 0x01 */
        out_draw_rows();
        return;
    }
    switch (_out_row) {
        case 0:                      /* toggle enable (with interlock) */
            ch->enabled ^= 1;
            out_apply_enable(_out_cur);
            Screen_Output_Draw();    /* full redraw: header state may change */
            break;
        case 1:                      /* edit voltage */
        case 2:                      /* edit current limit */
            _out_edit = 1;
            out_draw_rows();
            break;
        case 3:                      /* back to settings */
            UI_NavigateTo(UI_SCREEN_SETTINGS);
            break;
    }
}
#define SETTINGS_NUM_ROWS  (sizeof(_settings_rows) / sizeof(_settings_rows[0]))

#define SET_LIST_ROW_H      28
#define SET_LIST_START      (CONTENT_Y + 6)

static uint8_t _lock_all = 0;

void Screen_Settings_LockAll(uint8_t on)
{
    _lock_all = on;
    if (!on) return;
    /* close every output except primary USB-C (C1). C1 sacred. */
    for (uint8_t i = 0; i < 2; i++) {           /* A1, A2 */
        _settings_rows[i].state = 0;
        HAL_GPIO_WritePin(_settings_rows[i].gpio_port,
                          _settings_rows[i].gpio_pin, GPIO_PIN_RESET);
    }
    for (uint8_t c = 0; c < 2; c++) {           /* Lab, C2 */
        _out_ch[c].enabled = 0;
        HAL_GPIO_WritePin(_out_ch[c].gpio_port, _out_ch[c].gpio_pin,
                          GPIO_PIN_RESET);
    }
}

uint8_t Screen_Settings_GetLockAll(void) { return _lock_all; }

void Screen_Settings_Draw(uint8_t selected_row)
{
    ILI9341_FillScreen(COLOR_BG);
    draw_header("SETTINGS");

    /* SETTINGS is overlay, not carousel member: hint bar
     * instead of navigation dots. */
    ILI9341_FillRect(0, FOOTER_Y, ILI9341_WIDTH, FOOTER_H, COLOR_HEADER);
    GFX_DrawString(10, FOOTER_Y + 11,
                   "PRESS=toggle/select",
                   &GFX_FontSmall, COLOR_GRAY, COLOR_HEADER);

    Screen_Settings_Update(selected_row);
}

#define SETTINGS_EXIT_X   130
#define SETTINGS_EXIT_Y   (FOOTER_Y - 40)
#define SETTINGS_EXIT_W   100
#define SETTINGS_EXIT_H   32

void Screen_Settings_Update(uint8_t selected_row)
{
    char buf[32];

    /* Exit button, bottom right (virtual row 7) */
    Widget_Button_Draw(SETTINGS_EXIT_X, SETTINGS_EXIT_Y,
                       SETTINGS_EXIT_W, SETTINGS_EXIT_H,
                       "Exit >", (selected_row == 7));

    for (uint8_t i = 0; i < 7; i++)
    {
        uint16_t row_y = SET_LIST_START + i * SET_LIST_ROW_H;

        if (i < 2) {
            /* plain on/off toggles for USB-A */
            snprintf(buf, sizeof(buf), "%-12s [%s]",
                     _settings_rows[i].name,
                     _settings_rows[i].state ? "ON " : "OFF");
            HAL_GPIO_WritePin(_settings_rows[i].gpio_port,
                              _settings_rows[i].gpio_pin,
                              _settings_rows[i].state ? GPIO_PIN_SET
                                                      : GPIO_PIN_RESET);
        }
        else if (i < 4) {
            /* channel pages Lab / USB-C2 */
            snprintf(buf, sizeof(buf), "%-11s %s >",
                     _settings_rows[i].name, Screen_Output_RowStatus(i - 2));
        }
        else if (i == 4) {
            snprintf(buf, sizeof(buf), "%-12s [%s]", "Lock all",
                     _lock_all ? "ON " : "OFF");
        }
        else if (i == 5) {
            snprintf(buf, sizeof(buf), "%-12s %3u%% >", "Display",
                     ILI9341_GetBrightness());
        }
        else {
            snprintf(buf, sizeof(buf), "%s", "Test modes >");
        }

        Widget_MenuRow_Draw(10, row_y, 220, SET_LIST_ROW_H - 4,
                            buf, (i == selected_row));
    }
}

void Screen_Settings_Toggle(uint8_t row)
{
    if (row >= SETTINGS_NUM_ROWS) return;

    _settings_rows[row].state ^= 1;

    /* user turn port back on by hand -> lock mode over */
    if (_settings_rows[row].state) _lock_all = 0;
}
