#ifndef __EVENT_H__
#define __EVENT_H__

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    EVT_CHARGER_CONNECTED,
    EVT_CHARGER_DISCONNECTED,
    EVT_SOC_SAFETY,      // SoC drops below 15%, IDLE → SAFETY_LOCK
    EVT_SOC_LOWV,        // SoC drops below 10%, SAFETY_LOCK → LOW_V
    EVT_SOC_UNDERV,      // Cell voltage below damage threshold, LOW_V → EMERGENCY
    EVT_SOC_OK,          // SoC recovers above 15%
    EVT_SOC_OVCH,        // BATHI flag set, overcharge detected
    EVT_FAULT_OT,        // Overtemperature (OTC/OTD/OTP), recoverable
    EVT_FAULT_CRITICAL,  // BMS open/dead, unrecoverable
    EVT_FAULT_OCC,       // Overcurrent on charging input
    EVT_ERROR,           // General recoverable error (I2C fail, sensor fault)
    EVT_ERROR_CLEAR,     // Error condition resolved
    EVT_INACTIVITY,      // No user input for INACTIVITY_TIMEOUT_MS
    EVT_BUTTON_SHORT,
    EVT_BUTTON_LONG,
    EVT_MANUAL_ENTER,    // UI: enter LAB mode
    EVT_MANUAL_EXIT,     // UI: exit LAB mode
    EVT_LOCK,            // UI "Lock all": user asks SAFETY_LOCK (sets userLock)
    EVT_UNLOCK,          // UI "Lock all" off: leave SAFETY_LOCK; honored only
                         // if the lock was user-initiated (userLock guard)
    EVT_NUMBER
} Event_t;

/** @brief Push an event onto the queue. Silently drops if the queue is full. */
void event_push(Event_t event);

/** @brief Pop the oldest event from the queue. Returns true and writes to *out if an event was available, false if empty. */
bool event_pop(Event_t *out);

/** @brief Returns true if at least one event is waiting in the queue. */
bool event_pending(void);

#endif // __EVENT_H__
