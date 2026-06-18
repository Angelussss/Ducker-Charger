/**
 * @file    ui_state.c
 * @brief   UI state machine implementation.
 *          Implementazione della macchina a stati UI.
 */

#include "ui/ui_state.h"
#include "ui/screens.h"
#include "app/encoder.h"
#include "app/telemetry.h"

/* =========================================================
 * GLOBAL VARIABLES
 * ========================================================= */

UIState_t ui_state;

/* =========================================================
 * STATIC (FILE-PRIVATE) VARIABLES
 * ========================================================= */

/** Tick at which the boot screen was shown, used for the auto-timeout. */
static uint32_t _boot_start_tick = 0;

/** Duration of the splash screen before automatically moving to MAIN. */
#define BOOT_SCREEN_DURATION_MS  1500

/* =========================================================
 * PUBLIC FUNCTIONS
 * ========================================================= */

void UI_Init(void)
{
    /* Zero-initialise the state struct. */
    ui_state.current_screen    = UI_SCREEN_BOOT;
    ui_state.prev_screen       = UI_SCREEN_BOOT;
    ui_state.needs_full_redraw = 1;
    ui_state.last_refresh_tick = 0;
    ui_state.btn_press_tick    = 0;
    ui_state.btn_was_held      = 0;

    /* Show the boot/splash screen and record the start time. */
    Screen_Boot_Draw();
    _boot_start_tick = HAL_GetTick();
}

void UI_Tick(void)
{
    uint32_t now = HAL_GetTick();

    /* -------- Boot screen auto-transition -------- */
    if (ui_state.current_screen == UI_SCREEN_BOOT)
    {
        if ((now - _boot_start_tick) >= BOOT_SCREEN_DURATION_MS)
        {
            /* Timeout expired: navigate to the main screen. */
            UI_NavigateTo(UI_SCREEN_MAIN);
        }

        /* Do nothing else while the boot screen is shown. */
        return;
    }

    /* -------- Read encoder input -------- */

    int8_t  enc_delta   = Encoder_GetDelta();
    uint8_t btn_pressed = Encoder_IsPressed();

    /* Long-press detection: if the button is held for more than UI_LONG_PRESS_MS,
     * return to the main screen from anywhere. */
    if (Encoder_IsHeld())
    {
        if (ui_state.btn_press_tick == 0)
        {
            /* Button just pressed: save the tick. */
            ui_state.btn_press_tick = now;
            ui_state.btn_was_held   = 0;
        }
        else if (!ui_state.btn_was_held &&
                 (now - ui_state.btn_press_tick) >= UI_LONG_PRESS_MS)
        {
            /* Long-press threshold reached: go home. */
            ui_state.btn_was_held = 1;

            if (ui_state.current_screen != UI_SCREEN_MAIN)
                UI_NavigateTo(UI_SCREEN_MAIN);
        }
    }
    else
    {
        /* Button released: reset the long-press timer. */
        ui_state.btn_press_tick = 0;
    }

    /* -------- Per-screen navigation logic -------- */

    switch (ui_state.current_screen)
    {
        case UI_SCREEN_MAIN:

            /* From main, rotating the encoder navigates to adjacent screens.
             * Pressing has no effect (there is no selection in MAIN). */
            if (enc_delta > 0) UI_NavigateTo(UI_SCREEN_DETAIL);
            if (enc_delta < 0) UI_NavigateTo(UI_SCREEN_SETTINGS);
            break;

        case UI_SCREEN_DETAIL:
            if (enc_delta > 0) UI_NavigateTo(UI_SCREEN_GRAPH);
            if (enc_delta < 0) UI_NavigateTo(UI_SCREEN_MAIN);
            break;

        case UI_SCREEN_GRAPH:
            if (enc_delta > 0) UI_NavigateTo(UI_SCREEN_SETTINGS);
            if (enc_delta < 0) UI_NavigateTo(UI_SCREEN_DETAIL);
            break;

        case UI_SCREEN_SETTINGS:
        {
            /* In settings, the encoder scrolls through menu rows.
             * The button toggles the selected output ON/OFF.
             * The static variable retains its value between calls. */
            static uint8_t settings_row = 0;
            static const uint8_t SETTINGS_ROWS = 4; /* USB-A1, USB-A2, Lab, USB-C2 */

            if (enc_delta > 0)
            {
                /* Scroll down (wrap around at the end). */
                settings_row = (settings_row + 1) % SETTINGS_ROWS;
                Screen_Settings_Update(settings_row);
            }
            else if (enc_delta < 0)
            {
                /* Scroll up (wrap around at the beginning). */
                settings_row = (settings_row + SETTINGS_ROWS - 1) % SETTINGS_ROWS;
                Screen_Settings_Update(settings_row);
            }

            /* Short press: toggle the selected output.
             * btn_was_held guard prevents firing after a long press. */
            if (btn_pressed && !ui_state.btn_was_held)
            {
                Screen_Settings_Toggle(settings_row);
                Screen_Settings_Update(settings_row);
            }

            break;
        }

        default:
            break;
    }

    /* -------- Telemetry polling -------- */

    /* Telemetry_Poll() checks the interval internally and returns immediately
     * if it is too early for the next reading. */
    Telemetry_Poll();

    /* -------- Periodic UI refresh -------- */

    if ((now - ui_state.last_refresh_tick) >= UI_REFRESH_INTERVAL_MS)
    {
        ui_state.last_refresh_tick = now;

        if (ui_state.needs_full_redraw)
        {
            /* First render of this screen: draw everything. */
            ui_state.needs_full_redraw = 0;

            switch (ui_state.current_screen)
            {
                case UI_SCREEN_MAIN:     Screen_Main_Draw();      break;
                case UI_SCREEN_DETAIL:   Screen_Detail_Draw();    break;
                case UI_SCREEN_GRAPH:    Screen_Graph_Draw();     break;
                case UI_SCREEN_SETTINGS: Screen_Settings_Draw(0); break;
                default: break;
            }
        }
        else
        {
            /* Subsequent renders: update only the dynamic parts. */
            switch (ui_state.current_screen)
            {
                case UI_SCREEN_MAIN:   Screen_Main_Update();   break;
                case UI_SCREEN_DETAIL: Screen_Detail_Update(); break;
                case UI_SCREEN_GRAPH:  Screen_Graph_Update();  break;

                /* Settings: update is triggered by navigation above,
                 * not by the periodic timer. */
                default:
                    break;
            }
        }
    }
}

void UI_NavigateTo(UIScreen_t screen)
{
    if (screen >= UI_SCREEN_COUNT) return;  /* input validation */

    ui_state.prev_screen       = ui_state.current_screen;
    ui_state.current_screen    = screen;
    ui_state.needs_full_redraw = 1;  /* force full redraw on next tick */
}

UIScreen_t UI_GetCurrentScreen(void)
{
    return ui_state.current_screen;
}