/**
 * @file    initialization.h
 * @brief   Every-boot initialization of the ICs that lose their
 *          configuration at power-down (Ducker-Charger main board).
 *
 * Covers, in order:
 *   1. TPS25750  — patch/config bundle streamed over I2C3 (mandatory:
 *                  the device is ROM-based, RAM config is volatile)
 *   2. INA3221   — configuration register (averaging, conversion times)
 *   3. STPD01    — safe defaults (VOUT 5 V, ILIM, output disabled)
 *
 * NOT handled here (by design):
 *   - BQ25713: owned by the TPS25750 through its private I2C_EX bus
 *   - CYPD3175/CCG3PA: has its own flash, boots autonomously
 *   - BQ34Z100 data flash: one-time provisioning, see provisioning.h
 *
 * Every step has a timeout and reports a status; a failed non-critical
 * step degrades the boot instead of blocking it. The report table is
 * kept for the UI (boot screen can list failed steps).
 */
#ifndef INITIALIZATION_H
#define INITIALIZATION_H

#include <stdint.h>

typedef enum {
    INIT_OK = 0,
    INIT_FAILED,        /* step failed after retries */
    INIT_SKIPPED,       /* not run (e.g. previous critical failure) */
    INIT_NOT_RUN,       /* boot has not reached this step yet */
} InitStatus_t;

typedef struct {
    const char   *name;     /* short label, fits the boot screen */
    InitStatus_t status;
    uint8_t       critical; /* 1 = failure degrades PD/charge path */
} InitReport_t;

/**
 * @brief Run all every-boot initialization steps, in order.
 * @return Number of failed steps (0 = all good).
 */
uint8_t System_Initialization(void);

/**
 * @brief Access the per-step report (for the UI boot screen / UART log).
 * @param count Out: number of entries.
 * @return Pointer to the internal report table.
 */
const InitReport_t *Init_GetReport(uint8_t *count);

#endif /* INITIALIZATION_H */
