/**
 * @file    calibration.h
 * @brief   BQ34Z100-R2 on-device calibration backend.
 *
 * Drives the gauge calibration entirely from the board (no bqStudio /
 * EV2400): the CALIBRATION settings page walks the user through four
 * steps, each backed by a function here. The only external tool needed
 * is a multimeter, whose readings the user enters with the encoder.
 *
 * Method: the "ratio method", the TRM does not document the raw ADC
 * registers bqStudio uses, but every measurement is linear in its DF
 * calibration parameter, so the parameter can be corrected from the
 * *reported* value instead:
 *
 *   Voltage Divider' = Voltage Divider * V_multimeter / Voltage()
 *   CC Gain'         = CC Gain         * I_multimeter / Current()
 *   (CC Delta tracks CC Gain by the fixed TRM ratio.)
 *
 * Offsets use the gauge's own internal routines (Control() CC_OFFSET /
 * BOARD_OFFSET), which require the pack current to be truly ~0 mA:
 * done best with the MCU powered from the SWD connector so its own
 * draw does not flow through the shunt.
 *
 * All DF writes go through the same block protocol as provisioning.c
 * and are gated on an explicit call, the wizard never writes flash
 * while merely browsing.
 */

#ifndef __APP_CALIBRATION_H
#define __APP_CALIBRATION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
    CAL_OK = 0,
    CAL_BUS_ERROR,      /* I2C transaction failed                     */
    CAL_WRONG_DEVICE,   /* DEVICE_TYPE mismatch: refuse to touch DF   */
    CAL_BUSY,           /* internal offset routine still running      */
    CAL_TIMEOUT,        /* offset routine did not finish in time      */
    CAL_RANGE,          /* entered value implies an implausible ratio */
} CalStatus_t;

/** Decoded DF Calibration/Data (subclass 104) content. */
typedef struct {
    float    cc_gain;       /* F4 Xemics float                        */
    float    cc_delta;      /* F4 Xemics float                        */
    int16_t  cc_offset;
    int8_t   board_offset;
    int8_t   int_temp_off;  /* 0.1 C units                            */
    int8_t   ext_temp_off;  /* 0.1 C units                            */
    uint16_t divider;       /* Voltage Divider, mV                    */
} CalData_t;

/** Verify the gauge answers (DEVICE_TYPE) and unseal it. */
CalStatus_t Calibration_Begin(void);

/** Read + decode the DF calibration block. */
CalStatus_t Calibration_Read(CalData_t *out);

/** Live gauge readings for the wizard (documented registers). */
CalStatus_t Calibration_Live(uint16_t *mv, int16_t *ma);

/* --- Step 1: offsets (requires true ~0 mA through the shunt) --- */
CalStatus_t Calibration_OffsetStart(void);  /* kicks CC_OFFSET        */
CalStatus_t Calibration_OffsetPoll(void);   /* CAL_BUSY while running;
                                               saves + starts BOARD_OFFSET,
                                               CAL_OK when all done   */

/* --- Step 2: voltage, from a multimeter reading in mV --- */
CalStatus_t Calibration_ApplyVoltage(uint16_t true_mv);

/* --- Step 3: current gain. Capture with the load flowing, write later
 *     (after the load is removed) so the DF write happens at rest. --- */
CalStatus_t Calibration_CaptureCurrent(int16_t true_ma);
CalStatus_t Calibration_CommitCurrent(void);
uint8_t     Calibration_HasPendingCurrent(void);

/* --- Step 4: external temperature offset, from a 0.1 C reading --- */
CalStatus_t Calibration_ApplyTemp(int16_t true_c10);

/* --- Step 5: pack configuration ---
 * VOLTSEL (Pack Configuration MSB bit 3: external divider, mandatory
 * for 4S), Number of Series Cells, Design Capacity. Read-modify-write:
 * already-correct bytes are skipped to spare the flash. */
CalStatus_t Calibration_ApplyPackConfig(void);

/* --- Step 6: enable Impedance Track (Control 0x0021).
 * One-way on the gauge ([QEN] cannot be cleared): do it only after
 * the electrical calibration and pack config are in. --- */
CalStatus_t Calibration_ITEnable(void);

/* --- Step 7: learning-cycle monitor (read-only) --- */
typedef struct {
    uint8_t update_status;  /* DF Gas Gauging/State: 04 / 05 / 06     */
    uint8_t qen, vok;       /* CONTROL_STATUS low byte bits 0 / 1     */
    uint8_t rest, fc, dsg;  /* Flags(): REST, FC, DSG                 */
    int16_t avg_ma;         /* AverageCurrent()                       */
} LearnStatus_t;

CalStatus_t Calibration_LearnStatus(LearnStatus_t *out);

/** 1 when the gauge has been calibrated. Proxy: the [QEN] flag, the
 * wizard's IT-enable step sets it, it is one-way and persists in the
 * gauge, so it doubles as a "production calibration done" latch.
 * Returns 1 on bus error too: a dead gauge is the NO SENSOR path's
 * problem, not a reason to nag about calibration. */
uint8_t Calibration_IsDone(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_CALIBRATION_H */
