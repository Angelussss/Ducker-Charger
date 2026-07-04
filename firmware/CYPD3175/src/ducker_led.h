/**
 * @file    ducker_led.h
 * @brief   Status LED driver for the Ducker-Charger USB-C2 port
 *          (CYPD3175-24LQXQ / EZ-PD CCG3PA).
 *
 * Runs on top of the CCGx Power SDK primitives (gpio.h, timer.h).
 * All timing is done with SDK software timers: never block the PD stack.
 *
 * LED semantics (change here if you prefer other patterns):
 *   OFF         no cable attached
 *   BLINK_SLOW  attached, PD negotiation in progress (500 ms period)
 *   ON          explicit PD contract established, port sourcing
 *   BLINK_FAST  fault latched (OVP/OCP/OTP), 125 ms period
 */
#ifndef DUCKER_LED_H
#define DUCKER_LED_H

#include <stdint.h>

typedef enum {
    DUCKER_LED_OFF = 0,
    DUCKER_LED_BLINK_SLOW,
    DUCKER_LED_ON,
    DUCKER_LED_BLINK_FAST,
} ducker_led_state_t;

/**
 * @brief Configure the LED GPIO (push-pull, off) and internal state.
 *        Call once from solution init, after system_init().
 */
void ducker_led_init(void);

/**
 * @brief Set the LED pattern. Safe to call from the app event handler.
 */
void ducker_led_set(ducker_led_state_t state);

/**
 * @brief Map a PD stack event (app_evt_t, passed as uint8_t to keep this
 *        header free of stack includes) to an LED state. Call once from
 *        the top of app_event_handler(); unrelated events are ignored.
 */
void ducker_led_on_event(uint8_t evt);

#endif /* DUCKER_LED_H */
