/**
 * @file    screens.c
 * @brief   all the screens. draw once full, then update moving parts.
 */

#include "ui/screens.h"
#include "ui/widgets.h"
#include "app/telemetry.h"
#include "main.h"       /* GPIO pin definitions */
#include "ui/ui_state.h"
#include "system/fsm.h"    /* PB_FSM_ActiveState: gates + fault screen */
#include "system/charge.h" /* fault-cause getters for the FAULT screen */
#include "app/calibration.h" /* gauge calibration wizard backend */
#include <stdio.h>      /* snprintf */

/* Manual output enables (SETTINGS toggles, OUTPUT page) are honored only
 * while the FSM is in a state where outputs are allowed. In CHARGING and
 * in the protection states (SAFETY_LOCK / LOW_V / EMERGENCY / ERROR) the
 * FSM just closed every output on purpose — a menu click must not reopen
 * them behind its back. Turning an output OFF is always allowed. */
static uint8_t fsm_allows_manual_enable(void)
{
    State_ID_t st = PB_FSM_ActiveState();
    return st == STATE_IDLE || st == STATE_SLEEP || st == STATE_MANUAL;
}

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
/* "gauge not calibrated" cache (see screens.h) */
static uint8_t _nocal;
void Screen_NoCal_Refresh(void) { _nocal = !Calibration_IsDone(); }
uint8_t Screen_NoCal_Get(void)  { return _nocal; }

void Screen_Header_RefreshIcons(void)
{
    ILI9341_FillRect(150, HEADER_Y + 4, 88, HEADER_H - 10, COLOR_HEADER);

    /* gauge unreachable: every battery number on screen is the last good
     * read, not live. Say so instead of drawing icons from stale data. */
    if (!telemetry.sensor_ok) {
        GFX_DrawString(152, HEADER_Y + 11, "NO SENSOR",
                       &GFX_FontSmall, COLOR_DANGER, COLOR_HEADER);
        return;
    }

    /* thermometer: red when hot, cyan when cold. no icon = temp good */
    if (telemetry.temp_celsius >= 45)
        Widget_StatusIcon_Draw(170, HEADER_Y + 10, ICON_TEMP_WARN, "", 1);
    else if (telemetry.temp_celsius < 10)
        Widget_StatusIcon_Draw(170, HEADER_Y + 10, ICON_TEMP_COLD, "", 1);

    /* blinking "!" = voltage very low. bad. */
    if (volt_alarm_pub(telemetry.voltage_mV) == 2 &&
        ((HAL_GetTick() / 500) & 1))
        Widget_StatusIcon_Draw(155, HEADER_Y + 10, ICON_ALERT, "", 1);
    /* "CAL" = gauge never calibrated (same slot, alert wins) */
    else if (_nocal)
        GFX_DrawString(152, HEADER_Y + 11, "CAL",
                       &GFX_FontSmall, COLOR_ORANGE, COLOR_HEADER);

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

    /* gauge says the pack must not be charged (temperature window) */
    if (telemetry.charge_inhibited)
        GFX_DrawString(216, HEADER_Y + 11, "INH",
                       &GFX_FontSmall, COLOR_ORANGE, COLOR_HEADER);
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
/* Thermal ladder, coordinated with the rest of the system:
 * 45 C header icon -> 50 C UI warning (here) -> 55 C gauge OT = FSM ERROR
 * (fault screen). The warning must come BEFORE the fault, not after. */
#define TH_TEMP_RED        50     /* C */
#define TH_TEMP_COLD       10     /* below this C -> cyan + warning */
#define TH_VOLT_GREEN_MV   14000  /* 14-17 V: green, all good */
#define TH_VOLT_ORANGE_MV  13000  /* 13-14 V: orange, keep eye on it */
#define TH_VOLT_RED_MV     12000  /* red + blink; FSM EMERGENCY fires here */
/* UI critical-battery warning: fires ABOVE the hard limits so it is a real
 * heads-up — the gauge raises BATLOW around 12.2 V and the FSM goes
 * EMERGENCY (terminal, fault screen) at 12.0 V. The UI only warns;
 * the FSM is the one that acts. */
#define TH_VOLT_CRIT_MV    12500

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

uint16_t volt_color_pub(uint16_t mv)
{
    if (mv < TH_VOLT_RED_MV)          /* critico: rosso lampeggiante */
        return ((HAL_GetTick() / 500) & 1) ? COLOR_DANGER : COLOR_DARKGRAY;
    if (mv < TH_VOLT_ORANGE_MV) return COLOR_DANGER;
    if (mv < TH_VOLT_GREEN_MV)  return COLOR_ORANGE;
    return COLOR_GREEN;
}

/* 0 = fine, 1 = warn (12.5-13 V), 2 = critical (<12.5 V, EMERGENCY at 12 V) */
uint8_t volt_alarm_pub(uint16_t mv)
{
    if (mv < TH_VOLT_CRIT_MV)   return 2;
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

    /* grid lines */
    GFX_DrawVLine(120, CONTENT_Y + 5, 200, COLOR_DARKGRAY);
    GFX_DrawHLine(10,  DETAIL_ROW_2 - 5, 220, COLOR_DARKGRAY);
    GFX_DrawHLine(10,  DETAIL_ROW_3 - 5, 220, COLOR_DARKGRAY);

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

    /* Charger state: charge_phase is only ever IDLE/FAST — the BQ25713 knows
     * PRE/TAPER but sits on a private I2C bus this MCU can't reach. */
    const char *phase_str[] = {"IDLE", "FAST"};
    const char *state_str   = telemetry.vbus_present
                              ? phase_str[telemetry.charge_phase & 0x01]
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
#define PORTS_I_MAX      5000   /* C1 OTG can reach 5 A, the widest of the five */

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
    {
        char buf[8];
        snprintf(buf, sizeof(buf), "%uA", PORTS_I_MAX / 1000u);
        GFX_DrawString(PORTS_GRAPH_X, PORTS_GRAPH_Y - 10, buf,
                       &GFX_FontSmall, COLOR_GRAY, COLOR_BG);
    }
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

/* auto-sleep: index into {Off, 1, 2, 15 min}. Default matches
 * INACTIVITY_TIMEOUT_MS (defines.h) so the FSM's own inactivity sleep
 * doesn't pre-empt this setting before it ever gets a chance to fire. */
static const uint16_t _asleep_min[] = { 0, 1, 2, 15 };
static uint8_t _asleep_idx = 2;   /* default 2 min */
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
    Widget_MenuRow_Draw(10, SET_LIST_START + 126, 220, 36, "Shutdown",    (_disp_row == 3));
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
        /* live: SetBrightness reprograms the TIM10 PWM duty on the spot */
        ILI9341_SetBrightness((uint8_t)(v < 10 ? 10 : (v > 100 ? 100 : v)));
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
        case 3: Screen_Confirm_Open(CONFIRM_SHUTDOWN);
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
    telemetry.is_charging = 0;
    UI_RearmWarning();
}
static void t_vcrit(void)
{
    telemetry.voltage_mV = 11800; telemetry.soc_percent = 3;
    telemetry.current_mA = -1200;
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
 * SCREEN: CALIBRATION PAGE — gauge calibration wizard
 * Four steps, each backed by app/calibration.c; the multimeter
 * readings are entered with the encoder. DF is written only on
 * the explicit APPLY/SAVE actions, never while browsing.
 * ========================================================= */

typedef enum {
    CALSTEP_INTRO = 0,   /* gauge check + current DF values          */
    CALSTEP_OFFSET,      /* internal CC + board offset routines      */
    CALSTEP_VOLT,        /* multimeter pack voltage -> divider       */
    CALSTEP_CURR,        /* multimeter series current -> CC gain     */
    CALSTEP_TEMP,        /* ambient temperature -> ext temp offset   */
    CALSTEP_PACK,        /* VOLTSEL + series cells + design capacity */
    CALSTEP_ITEN,        /* Impedance Track enable (one-way)         */
    CALSTEP_LEARN,       /* learning-cycle monitor (read-only)       */
    CALSTEP_DONE,        /* review                                   */
} CalStep_t;

static CalStep_t _cal_step;
static uint8_t   _cal_row;      /* 0 value, 1 action, 2 next, 3 exit */
static uint8_t   _cal_edit;     /* encoder edits the value row       */
static int32_t   _cal_val;      /* entered value (mV / mA / 0.1 C)   */
static uint8_t   _cal_ready;    /* Calibration_Begin() succeeded     */
static uint8_t   _cal_running;  /* offset routine in flight          */
static char      _cal_msg[24];  /* one-line status feedback          */

#define CAL_ROWS 4

static const char *_cal_title[] = {
    [CALSTEP_INTRO]  = "1/9 GAUGE CHECK",
    [CALSTEP_OFFSET] = "2/9 ZERO OFFSET",
    [CALSTEP_VOLT]   = "3/9 VOLTAGE",
    [CALSTEP_CURR]   = "4/9 CURRENT",
    [CALSTEP_TEMP]   = "5/9 TEMPERATURE",
    [CALSTEP_PACK]   = "6/9 PACK CONFIG",
    [CALSTEP_ITEN]   = "7/9 ENABLE IT",
    [CALSTEP_LEARN]  = "8/9 LEARNING",
    [CALSTEP_DONE]   = "9/9 REVIEW",
};

/* step instructions: imperative, action-first (4 short lines) */
static const char *_cal_help[][4] = {
    [CALSTEP_INTRO]  = { "No tools needed here.",
                         "Press CHECK GAUGE: unlocks the",
                         "gauge config memory.",
                         "Nothing gets written yet." },
    [CALSTEP_OFFSET] = { "1. Unplug charger + all loads",
                         "2. Power the MCU from SWD 3V3",
                         "3. Press START, wait ~20s",
                         "(gauge needs true 0mA to zero)" },
    [CALSTEP_VOLT]   = { "1. Multimeter on DC volts",
                         "2. Probe the pack + and - pins",
                         "3. Enter the exact reading",
                         "4. Press APPLY" },
    [CALSTEP_CURR]   = { "1. Multimeter on 10A, in series",
                         "   with any load on USB-A1",
                         "2. Enter the amps, press CAPTURE",
                         "3. Remove the load, press SAVE" },
    [CALSTEP_TEMP]   = { "1. Read the room temperature",
                         "   (any thermometer, +-2C fine)",
                         "2. Enter it below",
                         "3. Press APPLY" },
    [CALSTEP_PACK]   = { "No tools needed.",
                         "Press APPLY to describe the",
                         "pack to the gauge: 4S cells,",
                         "7800mAh, ext voltage divider." },
    [CALSTEP_ITEN]   = { "Finish steps 2-6 FIRST:",
                         "this switch is permanent.",
                         "Then press ENABLE IT to start",
                         "the self-learning algorithm." },
    [CALSTEP_LEARN]  = { "1. Charge the pack to full",
                         "2. Rest 2h (nothing plugged)",
                         "3. Discharge ~1.5A until empty",
                         "4. Rest 5h. Live status:" },
    [CALSTEP_DONE]   = { "Nothing more to do.",
                         "Values are stored in the gauge",
                         "flash: permanent, they survive",
                         "power loss and reboots." },
};

/* only the multimeter-entry steps use the value row */
static uint8_t cal_has_value(void)
{
    return _cal_step == CALSTEP_VOLT || _cal_step == CALSTEP_CURR ||
           _cal_step == CALSTEP_TEMP;
}

/* seed the value row from the live reading so the user only dials
 * in the (small) difference the multimeter shows */
static void cal_seed_value(void)
{
    uint16_t mv; int16_t ma;

    if (Calibration_Live(&mv, &ma) != CAL_OK)
        { _cal_val = 0; return; }
    switch (_cal_step) {
        case CALSTEP_VOLT: _cal_val = mv;  break;
        case CALSTEP_CURR: _cal_val = ma;  break;
        case CALSTEP_TEMP: _cal_val = 250; break;   /* 25.0 C */
        default:           _cal_val = 0;   break;
    }
}

static void cal_draw_rows(void)
{
    char buf[28];
    uint16_t mv = 0; int16_t ma = 0;
    uint8_t live_ok = (Calibration_Live(&mv, &ma) == CAL_OK);

    /* info block: step title + numbered instructions + live gauge
     * reading (shares the line with the status message) */
    ILI9341_FillRect(0, CONTENT_Y, ILI9341_WIDTH, 96, COLOR_BG);
    GFX_DrawString(10, CONTENT_Y + 2, _cal_title[_cal_step],
                   &GFX_FontSmall, COLOR_WHITE, COLOR_BG);
    for (uint8_t i = 0; i < 4u; i++)
        GFX_DrawString(10, CONTENT_Y + 16 + i * 13, _cal_help[_cal_step][i],
                       &GFX_FontSmall, COLOR_LIGHTGRAY, COLOR_BG);

    if (live_ok)
        snprintf(buf, sizeof(buf), "%u.%02uV %+dmA",
                 mv / 1000u, (mv % 1000u) / 10u, (int)ma);
    else
        snprintf(buf, sizeof(buf), "no answer");
    GFX_DrawString(10, CONTENT_Y + 72, buf,
                   &GFX_FontSmall, COLOR_ACCENT, COLOR_BG);
    GFX_DrawString(110, CONTENT_Y + 72, _cal_msg,
                   &GFX_FontSmall, COLOR_WARN, COLOR_BG);

    /* LEARN: the two input slots become a live learning-cycle monitor */
    if (_cal_step == CALSTEP_LEARN) {
        LearnStatus_t ls;
        ILI9341_FillRect(10, CONTENT_Y + 96, 220, 64, COLOR_BG);
        if (Calibration_LearnStatus(&ls) == CAL_OK) {
            snprintf(buf, sizeof(buf), "Upd 0x%02X  QEN:%u VOK:%u",
                     ls.update_status, ls.qen, ls.vok);
            GFX_DrawString(10, CONTENT_Y + 100, buf,
                           &GFX_FontSmall, COLOR_WHITE, COLOR_BG);
            snprintf(buf, sizeof(buf), "FC:%u REST:%u  %+dmA",
                     ls.fc, ls.rest, (int)ls.avg_ma);
            GFX_DrawString(10, CONTENT_Y + 118, buf,
                           &GFX_FontSmall, COLOR_WHITE, COLOR_BG);
            const char *hint =
                !ls.qen                  ? "IT off: run step 7 first" :
                ls.update_status >= 0x06 ? "DONE: Qmax + Ra learned" :
                ls.update_status == 0x05 ? "Qmax OK: recharge full"  :
                ls.fc                    ? "Full: rest 2h, then load" :
                ls.dsg                   ? "Discharging: to term V"   :
                ls.rest                  ? "Resting: wait it out"     :
                                           "Charge the pack full";
            GFX_DrawString(10, CONTENT_Y + 136, hint,
                           &GFX_FontSmall, COLOR_ACCENT, COLOR_BG);
        } else {
            GFX_DrawString(10, CONTENT_Y + 100, "Status read failed",
                           &GFX_FontSmall, COLOR_DANGER, COLOR_BG);
        }
        Widget_MenuRow_Draw(10, CONTENT_Y + 164, 220, 30, "Next >",
                            (_cal_row == 2));
        Widget_MenuRow_Draw(10, CONTENT_Y + 198, 220, 30, "< Exit",
                            (_cal_row == 3));
        ILI9341_FillRect(0, FOOTER_Y, ILI9341_WIDTH, FOOTER_H, COLOR_HEADER);
        GFX_DrawString(10, FOOTER_Y + 11, "Live status, updates alone",
                       &GFX_FontSmall, COLOR_GRAY, COLOR_HEADER);
        return;
    }

    /* REVIEW: the two input slots become the DF readout the user is
     * meant to copy into the provisioning patch list */
    if (_cal_step == CALSTEP_DONE) {
        CalData_t cd;
        LearnStatus_t ls;
        ILI9341_FillRect(10, CONTENT_Y + 96, 220, 64, COLOR_BG);
        if (Calibration_Read(&cd) == CAL_OK) {
            int gw = (int)cd.cc_gain;
            int gf = (int)((cd.cc_gain - (float)gw) * 10000.0f + 0.5f);
            snprintf(buf, sizeof(buf), "Gain %d.%04d  Div %u",
                     gw, gf, (unsigned)cd.divider);
            GFX_DrawString(10, CONTENT_Y + 100, buf,
                           &GFX_FontSmall, COLOR_WHITE, COLOR_BG);
            snprintf(buf, sizeof(buf), "CCoff %d  Boff %d",
                     (int)cd.cc_offset, (int)cd.board_offset);
            GFX_DrawString(10, CONTENT_Y + 118, buf,
                           &GFX_FontSmall, COLOR_WHITE, COLOR_BG);
            snprintf(buf, sizeof(buf), "Toff %d/%d",
                     (int)cd.int_temp_off, (int)cd.ext_temp_off);
            if (Calibration_LearnStatus(&ls) == CAL_OK) {
                size_t l = strlen(buf);
                snprintf(buf + l, sizeof(buf) - l, "  Upd 0x%02X IT:%u",
                         ls.update_status, ls.qen);
            }
            GFX_DrawString(10, CONTENT_Y + 136, buf,
                           &GFX_FontSmall, COLOR_WHITE, COLOR_BG);
        } else {
            GFX_DrawString(10, CONTENT_Y + 100, "DF read failed",
                           &GFX_FontSmall, COLOR_DANGER, COLOR_BG);
        }
        Widget_MenuRow_Draw(10, CONTENT_Y + 164, 220, 30, "Restart wizard",
                            (_cal_row == 2));
        Widget_MenuRow_Draw(10, CONTENT_Y + 198, 220, 30, "< Exit",
                            (_cal_row == 3));
        ILI9341_FillRect(0, FOOTER_Y, ILI9341_WIDTH, FOOTER_H, COLOR_HEADER);
        GFX_DrawString(10, FOOTER_Y + 11, "Calibration complete",
                       &GFX_FontSmall, COLOR_GRAY, COLOR_HEADER);
        return;
    }

    /* value row: only the steps with a multimeter entry have one; on
     * the others the slot stays blank and navigation skips it */
    if (cal_has_value()) {
        switch (_cal_step) {
            case CALSTEP_VOLT:
                snprintf(buf, sizeof(buf), _cal_edit ? "DMM <%2d.%02dV>"
                                                     : "DMM  %2d.%02dV",
                         (int)(_cal_val / 1000), (int)((_cal_val % 1000) / 10));
                break;
            case CALSTEP_CURR:
                snprintf(buf, sizeof(buf), _cal_edit ? "DMM <%+dmA>"
                                                     : "DMM  %+dmA",
                         (int)_cal_val);
                break;
            default: /* CALSTEP_TEMP */
                snprintf(buf, sizeof(buf), _cal_edit ? "Temp <%d.%dC>"
                                                     : "Temp  %d.%dC",
                         (int)(_cal_val / 10), (int)(_cal_val % 10));
                break;
        }
        Widget_MenuRow_Draw(10, CONTENT_Y + 96, 220, 30, buf, (_cal_row == 0));
    } else {
        ILI9341_FillRect(10, CONTENT_Y + 96, 220, 30, COLOR_BG);
    }

    /* action row */
    switch (_cal_step) {
        case CALSTEP_INTRO:  snprintf(buf, sizeof(buf), _cal_ready
                                      ? "Gauge OK  (re-check)" : "CHECK GAUGE"); break;
        case CALSTEP_OFFSET: snprintf(buf, sizeof(buf), _cal_running
                                      ? "RUNNING..." : "START (needs 0mA)");    break;
        case CALSTEP_VOLT:   snprintf(buf, sizeof(buf), "APPLY");               break;
        case CALSTEP_CURR:   snprintf(buf, sizeof(buf),
                                      Calibration_HasPendingCurrent()
                                      ? "SAVE (remove load)" : "CAPTURE");      break;
        case CALSTEP_TEMP:   snprintf(buf, sizeof(buf), "APPLY");               break;
        case CALSTEP_PACK:   snprintf(buf, sizeof(buf), "APPLY PACK CONFIG");   break;
        case CALSTEP_ITEN:   snprintf(buf, sizeof(buf), "ENABLE IT (one-way)"); break;
        default:             snprintf(buf, sizeof(buf), "-");                   break;
    }
    Widget_MenuRow_Draw(10, CONTENT_Y + 130, 220, 30, buf, (_cal_row == 1));

    Widget_MenuRow_Draw(10, CONTENT_Y + 164, 220, 30,
                        (_cal_step == CALSTEP_DONE) ? "Restart wizard" : "Next >",
                        (_cal_row == 2));
    Widget_MenuRow_Draw(10, CONTENT_Y + 198, 220, 30, "< Exit",
                        (_cal_row == 3));

    ILI9341_FillRect(0, FOOTER_Y, ILI9341_WIDTH, FOOTER_H, COLOR_HEADER);
    GFX_DrawString(10, FOOTER_Y + 11,
                   _cal_edit ? "ROTATE=value  PRESS=set" : "PRESS=select",
                   &GFX_FontSmall, COLOR_GRAY, COLOR_HEADER);
}

void Screen_Cal_Open(void)
{
    _cal_step = CALSTEP_INTRO;
    _cal_row = 1;
    _cal_edit = 0;
    _cal_ready = 0;
    _cal_running = 0;
    _cal_msg[0] = '\0';
}

void Screen_Cal_Draw(void)
{
    ILI9341_FillScreen(COLOR_BG);
    draw_header("CALIBRATION");
    cal_draw_rows();
}

void Screen_Cal_Update(void)
{
    /* poll the in-flight offset routine; refresh live readings */
    if (_cal_running) {
        CalStatus_t st = Calibration_OffsetPoll();
        if (st != CAL_BUSY) {
            _cal_running = 0;
            snprintf(_cal_msg, sizeof(_cal_msg), (st == CAL_OK)
                     ? "Offsets saved" : "Offset FAILED");
        }
    }
    cal_draw_rows();
}

static void cal_next_step(void)
{
    _cal_step = (_cal_step == CALSTEP_DONE)
                ? CALSTEP_INTRO : (CalStep_t)(_cal_step + 1);
    _cal_row  = (_cal_step == CALSTEP_DONE ||
                 _cal_step == CALSTEP_LEARN) ? 2u : 1u;
    _cal_edit = 0;
    _cal_msg[0] = '\0';
    cal_seed_value();
    Screen_Cal_Draw();
}

void Screen_Cal_OnRotate(int8_t d)
{
    if (_cal_edit) {
        /* granularity: 10 mV / 10 mA / 0.5 C per click */
        int32_t step = (_cal_step == CALSTEP_TEMP) ? 5 : 10;
        _cal_val += (d > 0 ? step : -step);
    } else {
        /* skip the rows a step doesn't have: value row on the
         * non-entry steps, value+action on LEARN/REVIEW */
        uint8_t first = (_cal_step == CALSTEP_DONE ||
                         _cal_step == CALSTEP_LEARN) ? 2u
                        : (cal_has_value() ? 0u : 1u);
        uint8_t n     = (uint8_t)(CAL_ROWS - first);
        _cal_row = (uint8_t)(first + ((_cal_row - first
                                       + (d > 0 ? 1u : n - 1u)) % n));
    }
    cal_draw_rows();
}

static void cal_do_action(void)
{
    CalStatus_t st;

    switch (_cal_step) {
        case CALSTEP_INTRO:
            st = Calibration_Begin();
            _cal_ready = (st == CAL_OK);
            snprintf(_cal_msg, sizeof(_cal_msg), _cal_ready
                     ? "Unsealed, DF readable"
                     : (st == CAL_WRONG_DEVICE ? "Wrong device!"
                                               : "Bus error"));
            break;

        case CALSTEP_OFFSET:
            if (_cal_running) break;
            st = Calibration_OffsetStart();
            _cal_running = (st == CAL_OK);
            snprintf(_cal_msg, sizeof(_cal_msg), _cal_running
                     ? "Running (~20 s)..." : "Bus error");
            break;

        case CALSTEP_VOLT:
            st = Calibration_ApplyVoltage((uint16_t)_cal_val);
            snprintf(_cal_msg, sizeof(_cal_msg),
                     (st == CAL_OK)    ? "Divider written" :
                     (st == CAL_RANGE) ? "Out of range (>15%)" : "Bus error");
            break;

        case CALSTEP_CURR:
            if (Calibration_HasPendingCurrent()) {
                st = Calibration_CommitCurrent();
                snprintf(_cal_msg, sizeof(_cal_msg), (st == CAL_OK)
                         ? "Gain written" : "Write failed");
            } else {
                st = Calibration_CaptureCurrent((int16_t)_cal_val);
                snprintf(_cal_msg, sizeof(_cal_msg),
                         (st == CAL_OK)    ? "Captured: unload+SAVE" :
                         (st == CAL_RANGE) ? "Out of range (>15%)" : "Bus error");
            }
            break;

        case CALSTEP_TEMP:
            st = Calibration_ApplyTemp((int16_t)_cal_val);
            snprintf(_cal_msg, sizeof(_cal_msg),
                     (st == CAL_OK)    ? "Offset written" :
                     (st == CAL_RANGE) ? "Diff >12C: not offset" : "Bus error");
            break;

        case CALSTEP_PACK:
            st = Calibration_ApplyPackConfig();
            snprintf(_cal_msg, sizeof(_cal_msg), (st == CAL_OK)
                     ? "Pack config written" : "Write failed");
            break;

        case CALSTEP_ITEN:
            st = Calibration_ITEnable();
            snprintf(_cal_msg, sizeof(_cal_msg), (st == CAL_OK)
                     ? "IT enabled (QEN set)" : "Bus error");
            Screen_NoCal_Refresh();   /* header CAL flag + wake warning off */
            break;

        default:
            break;
    }
}

void Screen_Cal_OnPress(void)
{
    if (_cal_edit) { _cal_edit = 0; cal_draw_rows(); return; }

    switch (_cal_row) {
        case 0:     /* value row: enter edit on the entry steps */
            if (_cal_step == CALSTEP_VOLT || _cal_step == CALSTEP_CURR ||
                _cal_step == CALSTEP_TEMP) {
                _cal_edit = 1;
            }
            cal_draw_rows();
            break;
        case 1:
            cal_do_action();
            cal_draw_rows();
            break;
        case 2:
            cal_next_step();
            break;
        case 3:
            UI_NavigateTo(UI_SCREEN_SETTINGS);
            break;
    }
}

/* =========================================================
 * SCREEN: CONFIRM — are-you-sure gate for big actions
 * ========================================================= */

static ConfirmAction_t _confirm_action = CONFIRM_LOCKALL;
static uint8_t         _confirm_sel    = 1;   /* 0=OK 1=Cancel (safe default) */

static const char *_confirm_title[] = {
    [CONFIRM_LOCKALL]   = "LOCK ALL PORTS?",
    [CONFIRM_SHUTDOWN]  = "SHUTDOWN?",
    [CONFIRM_CALIBRATE] = "CALIBRATE GAUGE?",
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
        GFX_DrawString(45, 140, "All outputs closed.",
                       &GFX_FontSmall, COLOR_WHITE, COLOR_BG),
        GFX_DrawString(35, 152, "USB-C1 keeps charging in.",
                       &GFX_FontSmall, COLOR_WHITE, COLOR_BG);
    else if (_confirm_action == CONFIRM_CALIBRATE)
        GFX_DrawString(30, 140, "Previous calibration data",
                       &GFX_FontSmall, COLOR_WARN, COLOR_BG),
        GFX_DrawString(50, 152, "will be overwritten.",
                       &GFX_FontSmall, COLOR_WARN, COLOR_BG);
    else /* CONFIRM_SHUTDOWN */
        GFX_DrawString(35, 140, "Everything off. Press the",
                       &GFX_FontSmall, COLOR_WHITE, COLOR_BG),
        GFX_DrawString(30, 152, "encoder to power back on.",
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
        { "Battery almost empty:", "below 12 V the system",
          "shuts down. Recharge now." } },
    [WARN_OVCH]  = { "BATTERY FULL",
        { "Battery is overcharged:", "unplug the charger and",
          "discharge to recover." } },
    [WARN_CHGINH] = { "CHARGE INHIBITED",
        { "Battery outside its charge", "temperature window:",
          "unplug the charger." } },
    [WARN_NOCAL] = { "NOT CALIBRATED",
        { "Gauge readings unreliable", "until calibrated. Run:",
          "Settings > Calibration." } },
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

    /* the number that matters (none for NOT CALIBRATED) */
    if (_warn_type == WARN_TEMP || _warn_type == WARN_CHGINH) {
        snprintf(buf, sizeof(buf), "%d C", telemetry.temp_celsius);
        GFX_DrawStringScaled(96, 240, buf, &GFX_FontSmall, 2,
                             temp_color_pub(telemetry.temp_celsius), COLOR_BG);
    } else if (_warn_type != WARN_NOCAL) {
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
 * SCREEN: FAULT — FSM in ERROR/EMERGENCY, cause from charge layer
 * ========================================================= */

/* The event does not carry the fault cause (by design, see FSM.md):
 * read whichever charge-layer flag is still set, most severe first. */
static const char *fault_cause(void)
{
    STPD01_Status    s  = getSTPD01_Status();
    FuelGaugeSensors fg = getFuelGaugeData();
    INA3221_Sensors  in = getINA3221_Sensors();

    switch (getCYPD_LastFaultEvent()) {
        case CYPD3175_EVT_OVP: return "USB-C2 overvoltage";
        case CYPD3175_EVT_OCP: return "USB-C2 overcurrent";
        case CYPD3175_EVT_OTP: return "USB-C2 overtemperature";
        default: break;
    }
    if (s.shortCircuitProtection)          return "STPD01 short circuit";
    if (s.overVoltageProtection)           return "STPD01 overvoltage";
    if (s.inductorPeakCurrentProtection)   return "STPD01 current peak";
    if (s.overTemperatureProtection)       return "STPD01 overtemperature";
    if (fg.flags.OTC || fg.flags.OTD)      return "Battery overtemperature";
    if (fg.flags.BATLOW)                   return "Battery undervoltage";
    if (in.critical_alert_channel1)        return "Overcurrent on USB-A1";
    if (in.critical_alert_channel2)        return "Overcurrent on USB-A2";
    if (fg.flags.CF)                       return "Gauge needs calibration";
    return "Sensor / I2C fault";
}

void Screen_Fault_Draw(void)
{
    char buf[24];
    uint8_t emergency = (PB_FSM_ActiveState() == STATE_EMERGENCY);
    const char *title = emergency ? "EMERGENCY" : "SYSTEM ERROR";
    const char *cause = fault_cause();

    ILI9341_FillScreen(COLOR_BG);

    /* same triangle as the warning screen, always danger-red */
    for (uint8_t t = 0; t < 3; t++) {
        GFX_DrawLine(120, 52 + t, 68 + t, 148 - t, COLOR_DANGER);
        GFX_DrawLine(120, 52 + t, 172 - t, 148 - t, COLOR_DANGER);
        GFX_DrawLine(70, 146 + t, 170, 146 + t, COLOR_DANGER);
    }
    GFX_DrawStringScaled(109, 90, "!", &GFX_FontSmall, 5, COLOR_DANGER, COLOR_BG);

    GFX_DrawStringScaled((uint16_t)((240 - strlen(title) * 14) / 2), 168,
                         title, &GFX_FontSmall, 2, COLOR_DANGER, COLOR_BG);
    GFX_DrawString((uint16_t)((240 - strlen(cause) * 6) / 2), 200,
                   cause, &GFX_FontSmall, COLOR_WHITE, COLOR_BG);

    /* the numbers that matter: pack voltage and temperature */
    snprintf(buf, sizeof(buf), "%u.%02u V   %d C",
             telemetry.voltage_mV / 1000, (telemetry.voltage_mV % 1000) / 10,
             telemetry.temp_celsius);
    GFX_DrawString((uint16_t)((240 - strlen(buf) * 6) / 2), 224, buf,
                   &GFX_FontSmall, COLOR_LIGHTGRAY, COLOR_BG);

    if (emergency)
        /* terminal state: nothing to acknowledge */
        GFX_DrawString(37, 252, "Disconnect loads, power off.",
                       &GFX_FontSmall, COLOR_DANGER, COLOR_BG);
    else
        Widget_Button_Draw(80, 272, 80, 30, "OK", 1);
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
    /* enabling is refused while the FSM has outputs locked
     * (CHARGING / protection states); disabling always goes through */
    if (_out_ch[ch].enabled && !fsm_allows_manual_enable()) {
        _out_ch[ch].enabled = 0;
        return;
    }

    /* interlock: two loads share one STPD01 rail, never both on */
    if (_out_ch[ch].enabled && _out_ch[!ch].enabled) {
        _out_ch[!ch].enabled = 0;
        if (ch == 1)
            event_push(EVT_MANUAL_EXIT);   /* lab goes down through the FSM */
        else
            HAL_GPIO_WritePin(_out_ch[1].gpio_port, _out_ch[1].gpio_pin,
                              GPIO_PIN_RESET);
    }

    if (ch == 0) {
        /* LAB channel = FSM MANUAL state. EVT_MANUAL_ENTER: Manual_Enter
         * raises C2_LAB_EN; EVT_MANUAL_EXIT: Manual_Exit tears down
         * STPD01 + C2 + the LAB pin. The STPD01 itself is programmed
         * through the charge layer (see FSM.md: in MANUAL the UI drives
         * setupSTPD01/enable_STPD01, never naked GPIO pokes). */
        if (_out_ch[0].enabled) {
            event_push(EVT_MANUAL_ENTER);
            if (setupSTPD01(_out_ch[0].voltage_mv, _out_ch[0].ilim_ma))
                enable_STPD01();
            else
                event_push(EVT_ERROR);
        } else {
            event_push(EVT_MANUAL_EXIT);
        }
    } else {
        HAL_GPIO_WritePin(_out_ch[1].gpio_port, _out_ch[1].gpio_pin,
                          _out_ch[1].enabled ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
}

static void out_fmt_volt(char *buf, size_t n, uint16_t mv)
{
    snprintf(buf, n, "%2u.%uV", mv / 1000, (mv % 1000) / 100);
}

const char *Screen_Output_RowStatus(uint8_t channel)
{
    static char st[2][16];
    char v[8];
    if (channel == 1) {
        /* C2: live pin + what STPD01 is actually programmed to (never the
         * cached ceiling — the connection interrupt enables this
         * autonomously, so a cached flag would lag behind reality). */
        uint8_t on = get_USBC2_Status() ? 1u : 0u;
        out_fmt_volt(v, sizeof(v), on ? (uint16_t)getSTPD01_SetpointVoltage()
                                      : _out_ch[1].voltage_mv);
        snprintf(st[1], sizeof(st[1]), "%s %s", on ? "ON " : "OFF", v);
        return st[1];
    }
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

    /* live draw from the port monitor (derived from the system-power ADC) */
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

    /* Row 0: enable — live pin, not the cached flag (only C2 reaches
     * here; see Screen_Output_RowStatus). */
    snprintf(buf, sizeof(buf), "%-12s [%s]", "Enable",
             get_USBC2_Status() ? "ON " : "OFF");
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
        GFX_DrawString(10, CONTENT_Y - 8, "PD source ceiling - CYPD3175",
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
        /* lab channel live: reprogram the STPD01 through the charge layer
         * (setupSTPD01 disables the converter first, then we re-enable) */
        if (_out_cur == 0 && _out_ch[0].enabled) {
            if (setupSTPD01(_out_ch[0].voltage_mv, _out_ch[0].ilim_ma))
                enable_STPD01();
            else
                event_push(EVT_ERROR);
        } else if (_out_cur == 1) {
            /* C2 ceiling: never above what PD negotiated, only below it —
             * the charge layer clamps and, if a device is already
             * connected, re-applies immediately (see charge.c). */
            setSecondaryUSBC_VoltageCeiling(_out_ch[1].voltage_mv);
            setSecondaryUSBC_CurrentCeiling(_out_ch[1].ilim_ma);
        }
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

#define SET_LIST_ROW_H      26   /* 8 rows + Exit button must fit CONTENT_H */
#define SET_LIST_START      (CONTENT_Y + 6)

static uint8_t _lock_all = 0;

void Screen_Settings_LockAll(uint8_t on)
{
    /* The FSM owns the outputs: "Lock all" is a SAFETY_LOCK injector, not
     * a pile of GPIO pokes. SafetyLock_Enter closes every port; EVT_UNLOCK
     * brings back IDLE — but only for a user-initiated lock (the userLock
     * guard in the FSM: a low-SoC SAFETY_LOCK cannot be dismissed here). */
    _lock_all = on;
    if (on) _out_ch[0].enabled = _out_ch[1].enabled = 0;  /* menu mirrors */
    event_push(on ? EVT_LOCK : EVT_UNLOCK);
}

uint8_t Screen_Settings_GetLockAll(void)
{
    /* live truth: the menu reflects the FSM, whatever path locked it */
    _lock_all = (PB_FSM_ActiveState() == STATE_SAFETY_LOCK) ? 1u : 0u;
    return _lock_all;
}

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

    /* Exit button, bottom right (virtual row 8) */
    Widget_Button_Draw(SETTINGS_EXIT_X, SETTINGS_EXIT_Y,
                       SETTINGS_EXIT_W, SETTINGS_EXIT_H,
                       "Exit >", (selected_row == 8));

    for (uint8_t i = 0; i < 8; i++)
    {
        uint16_t row_y = SET_LIST_START + i * SET_LIST_ROW_H;

        if (i < 2) {
            /* plain on/off toggles for USB-A. Drawing must never actuate:
             * the FSM and the charge layer also drive these pins, so the
             * menu reads the live pin state instead of pushing its own
             * (previously opening SETTINGS force-wrote stale row states,
             * silently shutting A1/A2 off). The pin is written in
             * Screen_Settings_Toggle only, on an explicit user click. */
            _settings_rows[i].state =
                (HAL_GPIO_ReadPin(_settings_rows[i].gpio_port,
                                  _settings_rows[i].gpio_pin) == GPIO_PIN_SET);
            snprintf(buf, sizeof(buf), "%-12s [%s]",
                     _settings_rows[i].name,
                     _settings_rows[i].state ? "ON " : "OFF");
        }
        else if (i < 4) {
            /* channel pages Lab / USB-C2 */
            snprintf(buf, sizeof(buf), "%-11s %s >",
                     _settings_rows[i].name, Screen_Output_RowStatus(i - 2));
        }
        else if (i == 4) {
            snprintf(buf, sizeof(buf), "%-12s [%s]", "Lock all",
                     Screen_Settings_GetLockAll() ? "ON " : "OFF");
        }
        else if (i == 5) {
            snprintf(buf, sizeof(buf), "%-12s %3u%% >", "Display",
                     ILI9341_GetBrightness());
        }
        else if (i == 6) {
            snprintf(buf, sizeof(buf), "%s", "Calibration >");
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

    /* enabling is refused while the FSM has outputs locked
     * (CHARGING / protection states); disabling always goes through */
    if (!_settings_rows[row].state && !fsm_allows_manual_enable())
        return;

    _settings_rows[row].state ^= 1;
    HAL_GPIO_WritePin(_settings_rows[row].gpio_port,
                      _settings_rows[row].gpio_pin,
                      _settings_rows[row].state ? GPIO_PIN_SET
                                                : GPIO_PIN_RESET);

    /* user turn port back on by hand -> lock mode over */
    if (_settings_rows[row].state) _lock_all = 0;
}
