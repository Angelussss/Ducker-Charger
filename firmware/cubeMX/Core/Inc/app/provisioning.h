/**
 * @file    provisioning.h
 * @brief   One-time provisioning of the BQ34Z100-R2 fuel gauge data
 *          flash (Ducker-Charger).
 *
 * This is NOT part of the every-boot path: data-flash writes wear the
 * gauge's flash and must happen with the pack at rest. The intended
 * flow is:
 *
 *   boot -> Provisioning_Required()?  -- no --> done
 *                 | yes (marker mismatch)
 *                 v
 *          Provisioning_RunGauge()  (or trigger it from a hidden
 *                                    Test-menu entry in the UI)
 *
 * The patch list shipped here covers the static pack parameters
 * (design capacity, series cells, voltage divider). The Impedance
 * Track learning data (Qmax, Ra tables) comes from the golden image
 * exported with bqStudio after the learning cycle on a sample pack:
 * once available, convert it into additional GaugeDfPatch_t entries.
 */
#ifndef PROVISIONING_H
#define PROVISIONING_H

#include <stdint.h>

/** One data-flash patch: `len` bytes at `offset` of subclass `subclass`. */
typedef struct {
    uint8_t        subclass;
    uint8_t        offset;
    uint8_t        len;
    const uint8_t *data;
} GaugeDfPatch_t;

typedef enum {
    PROV_OK = 0,
    PROV_NOT_NEEDED,    /* marker matches: gauge already provisioned */
    PROV_FAILED,
    PROV_UNSAFE,        /* pack not at rest: refused to write */
} ProvStatus_t;

/** Result of the I2C version check (Provisioning_Check). */
typedef enum {
    PROV_CHECK_OK = 0,      /* right chip, current pack-config revision */
    PROV_CHECK_BLANK,       /* marker absent: factory-fresh gauge */
    PROV_CHECK_OLD_REV,     /* provisioned with an older revision */
    PROV_CHECK_WRONG_DEVICE,/* DEVICE_TYPE mismatch: wrong/absent chip */
    PROV_CHECK_BUS_ERROR,   /* I2C failure */
} ProvCheck_t;

/**
 * @brief Full I2C version check: confirms the device is a BQ34Z100
 *        (Control() DEVICE_TYPE) and reads the provisioning marker.
 * @param rev_out Optional: revision currently stored in the gauge
 *                (0 when blank/unreadable).
 */
ProvCheck_t Provisioning_Check(uint8_t *rev_out);

/** @return 1 when Provisioning_Check() is anything but PROV_CHECK_OK
 *          (and the device answers on the bus). */
uint8_t Provisioning_Required(void);

/** Run the full gauge provisioning sequence (unseal, patch list,
 *  marker, reset). Refuses to run if current flow is not near zero. */
ProvStatus_t Provisioning_RunGauge(void);

#endif /* PROVISIONING_H */
