# Integrating `ducker_led` into the CCGx reference project

Target: `CYPD3175-24LQXQ_pa_direct_fb` (or `CYPD3171-24LQXQ_pb`) from the
EZ-PD CCGx Power SDK, opened in PSoC Creator 4.x.

## 1. Add the files

Copy `ducker_led.c` / `ducker_led.h` next to the solution sources
(same folder as `solution.c` / `app.c`) and add them to the PSoC Creator
project (right-click project → *Add → Existing Item*).

## 2. Initialise

In the solution init path (the reference calls it from `main()` after
`system_init()`, look for where other one-time init like
`app_init()` happens) add:

```c
#include "ducker_led.h"
...
ducker_led_init();
```

## 3. Hook the PD events

In `app.c`, inside `app_event_handler(uint8_t port, app_evt_t evt, ...)`,
add to the existing `switch (evt)` (case names below exist in the SDK,
keep the reference's own code in each case, just append the LED call):

```c
case APP_EVT_CONNECT:
case APP_EVT_PE_STARTED:               /* if present in your SDK rev */
    ducker_led_set(DUCKER_LED_BLINK_SLOW);
    break;   /* NB: append to existing case body, do not replace it */

case APP_EVT_PD_CONTRACT_NEGOTIATION_COMPLETE:
    ducker_led_set(DUCKER_LED_ON);
    break;

case APP_EVT_DISCONNECT:
case APP_EVT_HARD_RESET_COMPLETE:
    ducker_led_set(DUCKER_LED_OFF);
    break;

/* fault events: exact names vary slightly per SDK revision; grep
 * app_evt_t for VBUS_OVP / VBUS_OCP / VBUS_SCP / OTP */
case APP_EVT_VBUS_OVP_FAULT:
case APP_EVT_VBUS_OCP_FAULT:
    ducker_led_set(DUCKER_LED_BLINK_FAST);
    break;
```

## 4. Before the first build: the three CHECKs

1. **`DUCKER_LED_GPIO`**: set from the final schematic. Free pins only:
   stay clear of CC1/CC2, VBUS_MON/VBUS_FB, and the I2C pair if HPI to
   the STM32 is enabled.
2. **`DUCKER_LED_TIMER_ID`**: pick from the app/user range in
   `src/system/timer_id.h` and grep the workspace to confirm no
   collision.
3. **GPIO API signatures**: `gpio_hsiom_set_config()` /
   `gpio_set_value()` against `src/system/gpio.h` of the SDK revision
   you installed (written against the documented CCGx Power SDK API;
   minor signature drift between SDK releases is possible).

## 5. Build & flash

- Build in PSoC Creator (Debug or Release).
- First flash on a blank chip: SWD with MiniProg3/4 via PSoC
  Programmer.
- Later updates: EZ-PD Configuration Utility can reflash through the
  bootloader.

## Not covered here (separate work items)

- **PDO / voltage / protection values**: no code; set them in the
  config table, see `../config/c2_pd_config.md`.
- **Power-stage adaptation**: the `pa_direct_fb` reference drives the
  regulator through CCG3PA's direct-feedback path. If the new PCB
  copies the CCG3PA reference topology, nothing to do; if the STPD01
  I2C chain is kept, the psource layer needs real work.
- **HPI** for dynamic PDO ceiling from the STM32 (the UI's "Max PD
  volt" page): build option + I2C wiring, to be scoped once the
  schematic is settled.
