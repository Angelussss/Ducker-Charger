/**
 * @file    ducker_led.c
 * @brief   Status LED driver implementation. See ducker_led.h for the
 *          state semantics.
 *
 * VERIFY BEFORE BUILDING (SDK-version dependent, marked with CHECK):
 *   - The exact GPIO pin: set DUCKER_LED_GPIO from the final schematic.
 *   - gpio_hsiom_set_config() / gpio_set_value() signatures against
 *     <sdk>/src/system/gpio.h of the installed CCGx Power SDK.
 *   - A free software-timer ID: the PD stack and the app layer reserve
 *     ranges; pick one from the user/app range in timer_id.h and make
 *     sure it does not collide (grep the workspace for the value).
 */

#include "ducker_led.h"

/* CCGx Power SDK headers (paths as used inside the reference projects) */
#include <gpio.h>
#include <timer.h>
#include <timer_id.h>
#include <pd.h>          /* app_evt_t values for ducker_led_on_event() */

/* ------------------------------------------------------------------ */
/* Board / SDK bindings — review each CHECK before the first build.    */
/* ------------------------------------------------------------------ */

/* CHECK (the only one left): LED pin, to be fixed when the CCG3PA
 * section of the schematic is drawn. Must be a pin with no HSIOM role
 * in the reference design (clear of CC1/CC2, VBUS_MON/FB, HPI I2C). */
#define DUCKER_LED_GPIO         (GPIO_PORT_1_PIN_4)

/* Verified against SDK 3.5: USER_TIMERS_START_ID (140) onwards is the
 * range reserved for solution-level usage. */
#define DUCKER_LED_TIMER_ID     ((timer_id_t)(USER_TIMERS_START_ID + 4u))

/* LED drive polarity: 1 = pin high turns the LED on. Flip if the LED
 * is wired to VDD with the pin sinking current. */
#define DUCKER_LED_ACTIVE_HIGH  (1u)

/* Blink half-periods in ms */
#define DUCKER_LED_SLOW_MS      (250u)
#define DUCKER_LED_FAST_MS      (62u)

/* Timer module instance: CCG3PA is a single-port device. */
#define DUCKER_LED_PORT         (0u)

/* ------------------------------------------------------------------ */

static ducker_led_state_t led_state = DUCKER_LED_OFF;
static uint8_t            led_phys  = 0u;   /* current physical level  */

static void led_write(uint8_t on)
{
    led_phys = on;
    gpio_set_value(DUCKER_LED_GPIO,
                   DUCKER_LED_ACTIVE_HIGH ? (on != 0u) : (on == 0u));
}

/* Software-timer callback: toggles the LED and re-arms itself while a
 * blinking pattern is active. Runs in the SDK timer context: keep it
 * short, no PD calls from here. */
static void led_timer_cb(uint8_t port, timer_id_t id)
{
    (void)port;
    (void)id;

    uint16_t half;

    switch (led_state)
    {
        case DUCKER_LED_BLINK_SLOW: half = DUCKER_LED_SLOW_MS; break;
        case DUCKER_LED_BLINK_FAST: half = DUCKER_LED_FAST_MS; break;
        default:
            return;                 /* steady state: do not re-arm */
    }

    led_write(!led_phys);
    timer_start(DUCKER_LED_PORT, DUCKER_LED_TIMER_ID, half, led_timer_cb);
}

void ducker_led_init(void)
{
    /* Plain strong-drive GPIO, LED off. */
    gpio_hsiom_set_config(DUCKER_LED_GPIO, HSIOM_MODE_GPIO,
                          GPIO_DM_STRONG,
                          DUCKER_LED_ACTIVE_HIGH ? false : true);
    led_state = DUCKER_LED_OFF;
    led_phys  = 0u;
}

void ducker_led_set(ducker_led_state_t state)
{
    if (state == led_state)
        return;                     /* no churn on repeated events */

    led_state = state;
    timer_stop(DUCKER_LED_PORT, DUCKER_LED_TIMER_ID);

    switch (state)
    {
        case DUCKER_LED_ON:
            led_write(1u);
            break;

        case DUCKER_LED_OFF:
            led_write(0u);
            break;

        case DUCKER_LED_BLINK_SLOW:
        case DUCKER_LED_BLINK_FAST:
            led_write(1u);          /* start visible, then toggle */
            timer_start(DUCKER_LED_PORT, DUCKER_LED_TIMER_ID,
                        (state == DUCKER_LED_BLINK_SLOW)
                            ? DUCKER_LED_SLOW_MS : DUCKER_LED_FAST_MS,
                        led_timer_cb);
            break;

        default:
            break;
    }
}

void ducker_led_on_event(uint8_t evt)
{
    switch (evt)
    {
        case APP_EVT_CONNECT:
            ducker_led_set(DUCKER_LED_BLINK_SLOW);
            break;

        case APP_EVT_PD_CONTRACT_NEGOTIATION_COMPLETE:
            ducker_led_set(DUCKER_LED_ON);
            break;

        case APP_EVT_DISCONNECT:
        case APP_EVT_VBUS_PORT_DISABLE:
        case APP_EVT_HARD_RESET_COMPLETE:
        case APP_EVT_TYPE_C_ERROR_RECOVERY:
            ducker_led_set(DUCKER_LED_OFF);
            break;

        case APP_EVT_VBUS_OVP_FAULT:
        case APP_EVT_VBUS_OCP_FAULT:
        case APP_EVT_VBUS_UVP_FAULT:
        case APP_EVT_VBUS_SCP_FAULT:
            ducker_led_set(DUCKER_LED_BLINK_FAST);
            break;

        default:
            break;              /* event not LED-relevant: ignore */
    }
}
