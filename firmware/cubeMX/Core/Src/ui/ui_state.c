/**
 * @file    ui_state.c
 * @brief   UI state machine. knob turn, screen change. simple.
 */

#include "ui/ui_state.h"
#include "ui/screens.h"
#include "app/encoder.h"
#include "app/telemetry.h"
#include "system/fsm.h"     /* PB_FSM_ActiveState: FAULT screen takeover */
#include "system/event.h"   /* event_push: FAULT ack -> EVT_BUTTON_SHORT */

uint8_t temp_out_of_range_pub(int16_t temp_c);
uint8_t volt_alarm_pub(uint16_t mv);

/* =========================================================
 * GLOBAL VARIABLES
 * ========================================================= */

UIState_t ui_state;

/* =========================================================
 * STATIC (FILE-PRIVATE) VARIABLES
 * ========================================================= */

/** screen to return to when SETTINGS overlay close */
static UIScreen_t _settings_return = UI_SCREEN_MAIN;

/** warnings (temp / low V / critical V): each one spoken once per boot, no nag */
static uint8_t    _warn_acked[WARN_COUNT] = {0};
static WarnType_t _warn_active = WARN_TEMP;
static UIScreen_t _warn_return = UI_SCREEN_MAIN;

#define BOOT_SCREEN_DURATION_MS  1500

/** auto-sleep: tick of last time user touched knob */
static uint32_t _last_activity = 0;

/** tick when warning opened, for 30 s auto-ack */
static uint32_t _warn_open_tick = 0;

/** screen that opened confirm modal, go back there after */
static UIScreen_t _confirm_return = UI_SCREEN_SETTINGS;

/** screen to bring back when sleep end */
static UIScreen_t _sleep_return = UI_SCREEN_MAIN;

/** screen to go back to when the FSM leaves ERROR (FAULT screen closes) */
static UIScreen_t _fault_return = UI_SCREEN_MAIN;
/** FSM state the FAULT screen was last drawn for (redraw on escalation) */
static State_ID_t _fault_drawn_state = STATE_NUMBER;

/** where boot screen go and how long (reused as wake splash) */
static UIScreen_t _boot_dest     = UI_SCREEN_MAIN;
static uint32_t   _boot_duration = BOOT_SCREEN_DURATION_MS;

static uint32_t _boot_start_tick = 0;

/* =========================================================
 * PUBLIC FUNCTIONS
 * ========================================================= */

void UI_Init(void)
{
    ui_state.current_screen    = UI_SCREEN_BOOT;
    ui_state.prev_screen       = UI_SCREEN_BOOT;
    ui_state.needs_full_redraw = 1;
    ui_state.last_refresh_tick = 0;
    ui_state.btn_press_tick    = 0;
    ui_state.btn_was_held      = 0;

    Screen_Boot_Draw();
    _boot_start_tick = HAL_GetTick();
}

void UI_Tick(void)
{
    uint32_t now = HAL_GetTick();

    /* Telemetry is polled by the main loop, before this tick runs: UI_Tick
     * is skipped in SLEEP/DEEP_SLEEP, and polling from here froze uptime,
     * the energy integral and every port history for the whole sleep —
     * which is precisely when a powerbank is feeding a device. */

    /* -------- Boot screen auto-transition -------- */
    if (ui_state.current_screen == UI_SCREEN_BOOT)
    {
        if ((now - _boot_start_tick) >= _boot_duration)
        {
            /* time over: go where boot say
             * (MAIN at power-on, old screen after light-sleep wake) */
            UI_NavigateTo(_boot_dest);
            _boot_dest     = UI_SCREEN_MAIN;
            _boot_duration = BOOT_SCREEN_DURATION_MS;
        }

        return;
    }

    /* -------- FSM fault takeover --------
     * ERROR/EMERGENCY must be visible whatever the UI was doing: open the
     * FAULT screen (even over UI sleep), leave it when the FSM recovers.
     * Placed before the sleep branch so a fault wakes the display. */
    {
        State_ID_t fsm_st = PB_FSM_ActiveState();
        if (fsm_st == STATE_ERROR || fsm_st == STATE_EMERGENCY)
        {
            if (ui_state.current_screen != UI_SCREEN_FAULT)
            {
                /* return somewhere sensible, never to a modal/dark screen */
                _fault_return =
                    (ui_state.current_screen == UI_SCREEN_MAIN     ||
                     ui_state.current_screen == UI_SCREEN_DETAIL   ||
                     ui_state.current_screen == UI_SCREEN_GRAPH    ||
                     ui_state.current_screen == UI_SCREEN_PORTS    ||
                     ui_state.current_screen == UI_SCREEN_STATS)
                    ? ui_state.current_screen : UI_SCREEN_MAIN;
                UI_NavigateTo(UI_SCREEN_FAULT);
            }
            else if (fsm_st != _fault_drawn_state)
            {
                /* escalation while shown (ERROR -> EMERGENCY): redraw */
                ui_state.needs_full_redraw = 1;
            }
        }
        else if (ui_state.current_screen == UI_SCREEN_FAULT)
        {
            /* FSM recovered (ack processed, charger plugged, ...) */
            UI_NavigateTo(_fault_return);
        }
    }

    /* -------- Read encoder input -------- */

    int8_t  enc_delta   = Encoder_GetDelta();
    uint8_t btn_pressed = Encoder_IsPressed();

    /* -------- sleep: any press wake up, everything else ignored -------- */
    if (ui_state.current_screen == UI_SCREEN_SLEEP)
    {
        /* draw once on entry: this early return never reaches the periodic
         * refresh switch below, so without this the panel kept showing the
         * last frame frozen instead of blanking */
        if (ui_state.needs_full_redraw)
        {
            ui_state.needs_full_redraw = 0;
            Screen_Sleep_Draw();
        }

        if (btn_pressed)
            UI_NavigateTo(_sleep_return);
        return;                     /* skip arbiter: wake press make no click */
    }

    /* -------- auto-sleep when user idle too long -------- */
    if (enc_delta != 0 || btn_pressed || Encoder_IsHeld())
        _last_activity = now;
    {
        uint32_t as = Screen_Display_GetAutoSleepMs();
        if (as > 0 && (now - _last_activity) >= as &&
            ui_state.current_screen != UI_SCREEN_BOOT &&
            ui_state.current_screen != UI_SCREEN_WARNING &&
            ui_state.current_screen != UI_SCREEN_CONFIRM &&
            ui_state.current_screen != UI_SCREEN_FAULT)
        {
            _last_activity = now;
            UI_EnterSleep();
            return;
        }
    }

    /* -------- warnings (order: V critical > overcharge > V low > temp) --
     * sensor_ok gate: before the first successful poll telemetry is all
     * zeros, and 0 mV would read as a critical-voltage alarm. */
    if (ui_state.current_screen != UI_SCREEN_BOOT &&
        ui_state.current_screen != UI_SCREEN_WARNING &&
        ui_state.current_screen != UI_SCREEN_FAULT &&
        telemetry.sensor_ok)
    {
        uint8_t va = volt_alarm_pub(telemetry.voltage_mV);
        WarnType_t t = WARN_COUNT;

        /* episode conditions can recover and come back (unlike "once per
         * boot" ones): re-arm their ack whenever the condition clears */
        if (!telemetry.over_volt) _warn_acked[WARN_OVCH] = 0;
        if (!(telemetry.charge_inhibited && telemetry.vbus_present))
            _warn_acked[WARN_CHGINH] = 0;

        if      (va == 2 && !_warn_acked[WARN_VCRIT]) t = WARN_VCRIT;
        else if (telemetry.over_volt &&
                 !_warn_acked[WARN_OVCH])             t = WARN_OVCH;
        else if (telemetry.charge_inhibited && telemetry.vbus_present &&
                 !_warn_acked[WARN_CHGINH])           t = WARN_CHGINH;
        else if (va == 1 && !_warn_acked[WARN_VLOW])  t = WARN_VLOW;
        else if (!_warn_acked[WARN_TEMP] &&
                 temp_out_of_range_pub(telemetry.temp_celsius)) t = WARN_TEMP;

        if (t != WARN_COUNT)
        {
            _warn_active = t;
            Screen_Warning_Open(t);
            _warn_return = ui_state.current_screen;
            _warn_open_tick = now;
            UI_NavigateTo(UI_SCREEN_WARNING);
        }
    }

    /* Long-press detection: if the button is held for more than UI_LONG_PRESS_MS,
     * return to the main screen from anywhere. */
    if (Encoder_IsHeld())
    {
        if (ui_state.btn_press_tick == 0)
        {
            /* finger just landed: remember when */
            ui_state.btn_press_tick = now;
            ui_state.btn_was_held   = 0;
        }
        else if (!ui_state.btn_was_held &&
                 (now - ui_state.btn_press_tick) >= UI_LONG_PRESS_MS)
        {   /* finger stay long enough */
            /* held long enough: open SETTINGS overlay.
             * remember where we came from, Exit bring us back. */
            ui_state.btn_was_held = 1;

            if (ui_state.current_screen != UI_SCREEN_SETTINGS &&
                ui_state.current_screen != UI_SCREEN_OUTPUT)
            {
                _settings_return = ui_state.current_screen;
                UI_NavigateTo(UI_SCREEN_SETTINGS);
            }
            /* inside SETTINGS you leave with "Exit" row (or double click
             * from OUTPUT page): long press do nothing here */
        }
    }
    else
    {
        /* finger gone: forget long-press state */
        ui_state.btn_press_tick = 0;
        ui_state.btn_was_held   = 0;
    }

    /* -------- click arbiter (one click vs two) --------
     * raw press not acted on right away: held pending UI_DOUBLE_CLICK_MS.
     * second press inside window -> DOUBLE click (jump to SETTINGS),
     * single action never fire. window pass -> clean SINGLE click out.
     * long press kill pending click. */
    uint8_t click = 0;   /* clean single click, screens below use this */
    {
        static uint32_t _pend_tick = 0;
        static uint8_t  _pend = 0;

        if (btn_pressed && !ui_state.btn_was_held)
        {
            if (_pend && (now - _pend_tick) <= UI_DOUBLE_CLICK_MS)
            {
                _pend = 0;                       /* two presses = double click */
                if (ui_state.current_screen != UI_SCREEN_SETTINGS)
                {
                    if (ui_state.current_screen != UI_SCREEN_OUTPUT)
                        _settings_return = ui_state.current_screen;
                    UI_NavigateTo(UI_SCREEN_SETTINGS);
                }
            }
            else { _pend = 1; _pend_tick = now; }
        }

        /* single click come out only when button UP
         * (btn_press_tick != 0 = finger still on button: long press
         * in progress must never spit click at +400 ms) */
        if (_pend && ui_state.btn_press_tick == 0 &&
            (now - _pend_tick) > UI_DOUBLE_CLICK_MS)
        { click = 1; _pend = 0; }                /* single click come out */

        if (ui_state.btn_was_held) _pend = 0;    /* long press eat pending click */
    }

    /* -------- per-screen navigation -------- */

    switch (ui_state.current_screen)
    {
        case UI_SCREEN_MAIN:

            /* from MAIN spin knob to move between screens.
             * press do nothing here (nothing to select) */
            if (enc_delta > 0) UI_NavigateTo(UI_SCREEN_DETAIL);
            if (enc_delta < 0) UI_NavigateTo(UI_SCREEN_STATS);   /* wrap around */
            break;

        case UI_SCREEN_DETAIL:
            if (enc_delta > 0) UI_NavigateTo(UI_SCREEN_GRAPH);
            if (enc_delta < 0) UI_NavigateTo(UI_SCREEN_MAIN);
            break;

        case UI_SCREEN_GRAPH:
            if (enc_delta > 0) UI_NavigateTo(UI_SCREEN_PORTS);
            if (enc_delta < 0) UI_NavigateTo(UI_SCREEN_DETAIL);
            if (click) Screen_Graph_CycleScale();
            break;

        case UI_SCREEN_PORTS:
            if (enc_delta > 0) UI_NavigateTo(UI_SCREEN_STATS);
            if (enc_delta < 0) UI_NavigateTo(UI_SCREEN_GRAPH);
            break;

        case UI_SCREEN_STATS:
            if (enc_delta > 0) UI_NavigateTo(UI_SCREEN_MAIN);    /* wrap around */
            if (enc_delta < 0) UI_NavigateTo(UI_SCREEN_PORTS);
            break;

        case UI_SCREEN_SETTINGS:
        {
            /* in settings knob scroll rows, button act on row.
             * static var remember row between calls. */
            static uint8_t settings_row = 0;
            static const uint8_t SETTINGS_ROWS = 8; /* A1, A2, Lab, C2, Lock all,
                                                       Screen off, Light sleep, Exit */

            if (enc_delta > 0)
            {
                /* scroll down, wrap at end */
                settings_row = (settings_row + 1) % SETTINGS_ROWS;
                Screen_Settings_Update(settings_row);
            }
            else if (enc_delta < 0)
            {
                /* scroll up, wrap at start */
                settings_row = (settings_row + SETTINGS_ROWS - 1) % SETTINGS_ROWS;
                Screen_Settings_Update(settings_row);
            }

            /* short press: toggle plain outputs, open channel page
             * for Lab / USB-C2, or do row action */
            if (click)
            {
                switch (settings_row)
                {
                    case 0: case 1:             /* toggle USB-A */
                        Screen_Settings_Toggle(settings_row);
                        Screen_Settings_Update(settings_row);
                        break;
                    case 2: case 3:             /* channel pages */
                        Screen_Output_Open(settings_row - 2);
                        UI_NavigateTo(UI_SCREEN_OUTPUT);
                        break;
                    case 4:                     /* Lock all */
                        if (!Screen_Settings_GetLockAll())
                        {
                            /* turning ON: ask first, big action */
                            Screen_Confirm_Open(CONFIRM_LOCKALL);
                            UI_OpenConfirm();
                        }
                        else
                        {
                            Screen_Settings_LockAll(0);
                            Screen_Settings_Update(settings_row);
                        }
                        break;
                    case 5:                     /* display page */
                        Screen_Display_Open();
                        UI_NavigateTo(UI_SCREEN_DISPLAYPG);
                        break;
                    case 6:                     /* test page */
                        Screen_Test_Open();
                        UI_NavigateTo(UI_SCREEN_TESTPG);
                        break;
                    case 7:                     /* Exit */
                        UI_CloseSettings();
                        break;
                }
            }

            break;
        }

        case UI_SCREEN_OUTPUT:
            if (enc_delta != 0) Screen_Output_OnRotate(enc_delta);
            if (click) Screen_Output_OnPress();
            break;

        case UI_SCREEN_DISPLAYPG:
            if (enc_delta != 0) Screen_Display_OnRotate(enc_delta);
            if (click) Screen_Display_OnPress();
            break;

        case UI_SCREEN_TESTPG:
            if (enc_delta != 0) Screen_Test_OnRotate(enc_delta);
            if (click) Screen_Test_OnPress();
            break;

        case UI_SCREEN_CONFIRM:
            if (enc_delta != 0) Screen_Confirm_OnRotate(enc_delta);
            if (click)
            {
                uint8_t r = Screen_Confirm_OnPress();
                if (r == 1)   /* user said yes */
                {
                    ConfirmAction_t action = Screen_Confirm_GetAction();
                    if (action == CONFIRM_LOCKALL)
                    {
                        Screen_Settings_LockAll(1);
                        UI_NavigateTo(_confirm_return);
                    }
                    else      /* CONFIRM_SHUTDOWN */
                    {
                        /* Same event a real long-press produces; the FSM
                         * gate (fsm.c) decides if it's honored. Navigate
                         * back regardless, so a refused request doesn't
                         * leave the dialog stuck open. */
                        event_push(EVT_BUTTON_LONG);
                        UI_NavigateTo(_confirm_return);
                    }
                }
                else          /* user said no */
                    UI_NavigateTo(_confirm_return);
            }
            break;

        case UI_SCREEN_FAULT:
            /* OK on ERROR pushes the acknowledge event the transition
             * matrix has always expected (ERROR + BUTTON_SHORT -> IDLE) —
             * this screen IS the producer. EMERGENCY has no ack. */
            if (click && PB_FSM_ActiveState() == STATE_ERROR)
                event_push(EVT_BUTTON_SHORT);
            break;

        case UI_SCREEN_WARNING:
            /* auto-ack: 30 s with no touch -> warning confirm itself
             * (header icon keep telling the condition) */
            if (!click && (now - _warn_open_tick) >= 30000)
                click = 1;
            if (click)                          /* OK: acked for this boot */
            {
                /* warnings only inform — they never actuate. The FSM is
                 * the single authority on outputs: at 12 V it goes to
                 * EMERGENCY on its own (and the BQ77915 BMS backs it up
                 * in hardware). The old VCRIT ack used to LockAll + light
                 * sleep here, racing the FSM at the same threshold. */
                _warn_acked[_warn_active] = 1;
                UI_NavigateTo(_warn_return);
            }
            break;

        default:
            break;
    }

    /* screen handlers above can enter SLEEP (warning ack, confirm modal):
     * stop here so the refresh block below can't consume needs_full_redraw
     * before the sleep branch draws the blank frame on the next tick */
    if (ui_state.current_screen == UI_SCREEN_SLEEP)
        return;

    /* -------- periodic UI refresh -------- */

    if ((now - ui_state.last_refresh_tick) >= UI_REFRESH_INTERVAL_MS)
    {
        ui_state.last_refresh_tick = now;

        if (ui_state.needs_full_redraw)
        {
            /* first render of screen: draw all of it */
            ui_state.needs_full_redraw = 0;

            switch (ui_state.current_screen)
            {
                case UI_SCREEN_BOOT:     Screen_Boot_Draw();      break;
                case UI_SCREEN_MAIN:     Screen_Main_Draw();      break;
                case UI_SCREEN_DETAIL:   Screen_Detail_Draw();    break;
                case UI_SCREEN_GRAPH:    Screen_Graph_Draw();     break;
                case UI_SCREEN_PORTS:    Screen_Ports_Draw();     break;
                case UI_SCREEN_STATS:    Screen_Stats_Draw();     break;
                case UI_SCREEN_SETTINGS: Screen_Settings_Draw(0); break;
                case UI_SCREEN_OUTPUT:   Screen_Output_Draw();     break;
                case UI_SCREEN_DISPLAYPG: Screen_Display_Draw();   break;
                case UI_SCREEN_TESTPG:    Screen_Test_Draw();      break;
                case UI_SCREEN_CONFIRM:  Screen_Confirm_Draw();    break;
                case UI_SCREEN_WARNING:  Screen_Warning_Draw();    break;
                case UI_SCREEN_FAULT:
                    Screen_Fault_Draw();
                    _fault_drawn_state = PB_FSM_ActiveState();
                    break;
                /* UI_SCREEN_SLEEP: drawn in the sleep branch above,
                 * its early return never reaches this switch */
                default: break;
            }
        }
        else
        {
            /* header icons follow live telemetry on every screen
             * (except boot, sleep, modals) */
            if (ui_state.current_screen != UI_SCREEN_BOOT &&
                ui_state.current_screen != UI_SCREEN_SLEEP &&
                ui_state.current_screen != UI_SCREEN_WARNING &&
                ui_state.current_screen != UI_SCREEN_CONFIRM &&
                ui_state.current_screen != UI_SCREEN_FAULT)
                Screen_Header_RefreshIcons();

            /* later renders: only redraw moving parts */
            switch (ui_state.current_screen)
            {
                case UI_SCREEN_MAIN:   Screen_Main_Update();   break;
                case UI_SCREEN_DETAIL: Screen_Detail_Update(); break;
                case UI_SCREEN_GRAPH:  Screen_Graph_Update();  break;
                case UI_SCREEN_PORTS:  Screen_Ports_Update();  break;
                case UI_SCREEN_OUTPUT: Screen_Output_Update(); break;
                case UI_SCREEN_STATS:  Screen_Stats_Update();  break;

                /* settings redraw on navigation, not on timer */
                default:
                    break;
            }
        }
    }
}

void UI_NavigateTo(UIScreen_t screen)
{
    if (screen >= UI_SCREEN_COUNT) return;  /* bad screen, refuse */

    ui_state.prev_screen       = ui_state.current_screen;
    ui_state.current_screen    = screen;
    ui_state.needs_full_redraw = 1;  /* next tick redraw everything */
}

void UI_EnterSleep(void)
{
    _sleep_return = (ui_state.current_screen == UI_SCREEN_SETTINGS ||
                     ui_state.current_screen == UI_SCREEN_OUTPUT ||
                     ui_state.current_screen == UI_SCREEN_DISPLAYPG ||
                     ui_state.current_screen == UI_SCREEN_TESTPG ||
                     ui_state.current_screen == UI_SCREEN_CONFIRM)
                    ? _settings_return : ui_state.current_screen;
    UI_NavigateTo(UI_SCREEN_SLEEP);
}

void UI_OpenConfirm(void)   /* pages call this: remember way home */
{
    _confirm_return = ui_state.current_screen;
    UI_NavigateTo(UI_SCREEN_CONFIRM);
}

void UI_CloseSettings(void)
{
    UI_NavigateTo(_settings_return);
}

void UI_RearmWarning(void)
{
    for (uint8_t i = 0; i < WARN_COUNT; i++) _warn_acked[i] = 0;
}

void UI_OnDeepSleepWake(void)
{
    /* swallow the wake press and any rotation queued while stopped */
    (void)Encoder_GetDelta();
    (void)Encoder_IsPressed();
    ui_state.btn_press_tick = 0;
    ui_state.btn_was_held   = 0;

    /* restart from the splash, like the light-sleep wake path */
    _boot_dest       = UI_SCREEN_MAIN;
    _boot_duration   = 1200;
    _boot_start_tick = HAL_GetTick();
    _last_activity   = HAL_GetTick();
    UI_NavigateTo(UI_SCREEN_BOOT);
}

UIScreen_t UI_GetCurrentScreen(void)
{
    return ui_state.current_screen;
}