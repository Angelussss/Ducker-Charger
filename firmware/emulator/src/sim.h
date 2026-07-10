/**
 * @file    sim.h
 * @brief   Internal API shared between the HAL fake, the IC models and
 *          the board model. Not visible to firmware sources.
 */
#ifndef SIM_H
#define SIM_H

#include <stdint.h>
#include <stdbool.h>

/* ---------------- global lock + time ---------------- */

void     sim_lock(void);
void     sim_unlock(void);
uint32_t sim_now_ms(void);
void     sim_log(const char *fmt, ...);
/* current firmware FSM state as a short name (log column, status print) */
const char *sim_fsm_name(void);

/* firmware thread wake (STOP-mode exit) */
void sim_wake_signal(void);

/* ---------------- GPIO backend (ports: 0=A 1=B 2=C) ---------------- */

int  sim_gpio_get(int port, int pinno);                 /* level 0/1     */
void sim_gpio_drive_input(int port, int pinno, int lv); /* device -> MCU */
/* called (with sim lock held) whenever firmware writes an output pin */
void sim_gpio_output_changed(int port, int pinno, int lv);

/* ---------------- I2C bus ---------------- */

typedef struct SimI2CDev {
    uint16_t addr8;         /* HAL-style 8-bit address */
    const char *name;
    int (*mem_write)(struct SimI2CDev *, uint16_t mem, uint16_t memsize,
                     const uint8_t *d, uint16_t n);
    int (*mem_read)(struct SimI2CDev *, uint16_t mem, uint16_t memsize,
                    uint8_t *d, uint16_t n);
    int (*mtx)(struct SimI2CDev *, const uint8_t *d, uint16_t n);
    int (*mrx)(struct SimI2CDev *, uint8_t *d, uint16_t n);
} SimI2CDev;

void       sim_i2c_attach(int bus, SimI2CDev *dev);
SimI2CDev *sim_i2c_find(int bus, uint16_t addr8);

/* ---------------- ADC (rank order = ADC1 scan sequence) ------------
 * 0..3 NTC zones 1-4, 4 on-board NTC, 5 USB-C input current,
 * 6 battery current (unused by fw), 7 system power                   */
uint16_t board_adc_sample(int rank);

/* ---------------- board model ---------------- */

typedef struct {
    /* battery physics */
    float soc;              /* 0..100 %                       */
    float pack_mv;          /* terminal voltage               */
    float i_batt_ma;        /* + charging / - discharging     */
    float temp_c;           /* pack NTC temperature           */
    float board_temp_c;
    /* attachments (scenario) */
    bool  charger_attached;   /* C1 charger present (we sink)   */
    bool  c1_device_attached; /* C1 sink device (we source/OTG) */
    bool  c2_sink_attached;   /* C2 sink device present         */
    bool  a1_load_attached;
    bool  a2_load_attached;
    bool  lab_load_attached;  /* resistive load on the lab output */
    /* live per-port currents (mA, out of the pack) */
    float a1_ma, a2_ma, c1_out_ma, c2_ma, lab_ma;
    float charge_in_ma;
} BoardState;

extern BoardState board;

void board_init(void);
void board_tick(uint32_t wall_dms);      /* advance battery physics */

/* ---------------- configuration (emu_config.ini) ---------------- */

typedef struct {
    float soc;              /* initial SoC %                   */
    float time_scale;       /* battery-time acceleration       */
    float ambient_c;
    float charge_ma;        /* CC charge current from C1       */
    float a1_load_ma, a2_load_ma;
    float lab_load_ohm;     /* bench load on the lab output    */
    float quiescent_ma;
    int   c1_mv, c1_ma;     /* C1 charger PD contract (sink)   */
    int   c1src_mv, c1src_ma; /* C1 source/OTG contract        */
    int   c2_mv, c2_ma;     /* C2 sink requested PD contract   */
    int   tps_app_mode;     /* 1 = warm boot, already patched  */
    int   tps_strict_len;   /* 1 = length-prefix Mem_* too     */
    int   gauge_provisioned;
    int   bckl_active_low;  /* JP402: 1 = PMOS (faithful), 0 = NMOS */
    int   soh, cycles;
} EmuConfig;

extern EmuConfig cfg;
void EmuConfig_Load(const char *path);

/* ---------------- IC models ---------------- */

void bq34_attach(void);
void bq34_inject_ot(int on);        /* OTC/OTD                        */
void bq34_inject_bathi(int on);
void bq34_inject_batlow(int on);
void bq34_inject_cf(int on);
void bq34_inject_bus_fail(int on);   /* gauge NACKs everything */

void tps_attach(void);
void tps_plug_charger(bool plugged);    /* C1 as sink, drives PB14    */
void tps_plug_device(bool plugged);     /* C1 as source (OTG)         */

void cypd_attach(void);
void cypd_plug_sink(bool plugged);      /* contract / disconnect      */
void cypd_inject(uint8_t hpi_event);    /* OVP/OCP/OTP/HARD_RESET     */

void stpd_attach(void);
void stpd_inject_fault(uint8_t int_stat_bits);
float stpd_vout_mv(void);
float stpd_ilim_ma(void);

/* rotate the TIM3 encoder counter by n detents (2 counts each) */
void sim_encoder_step(int detents);

void ina_attach(void);
void ina_inject_alert(int channel, int on);   /* 1 or 2 */

/* ---------------- display panel model ---------------- */

#define PANEL_W 240
#define PANEL_H 320

void panel_attach(void);
void panel_spi_bytes(const uint8_t *d, uint16_t n);
/* render with inversion / display-off / backlight applied */
void panel_render(uint16_t *out_rgb565);
void panel_dump_ppm(const char *path);

#endif /* SIM_H */
