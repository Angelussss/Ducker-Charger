/* fake-data config for simulator, loaded from sim_config.ini */
#ifndef SIM_CONFIG_H
#define SIM_CONFIG_H

typedef struct {
    /* battery */
    int soc;            /* charge % */
    int pack_mv;
    int batt_ma;        /* base current: >0 charging, <0 draining */
    int batt_ripple;    /* wiggle size in mA (0 = flat line) */
    int temp_c;
    int vbus;           /* charger plugged 0/1 */
    int full;           /* battery full flag 0/1 */
    int phase;          /* 0=idle 1=charging (that's all the real UI shows, 
                            PRE/TAPER aren't observable, see telemetry.h) */
    /* ports: 0=A1 1=A2 2=C1(OTG) 3=C2 4=LAB */
    struct { int active; int mv; int ma; int ripple; } port[5];
    /* statistics */
    int cycles, health, cap_full_mah, cap_design_mah;
    int charges, max_temp_c, max_out_ma, max_in_ma;
    int energy_mwh, uptime_s;
    int tte_min, ttf_min;   /* time-to-empty / time-to-full, minutes */
} SimCfg;

extern SimCfg simcfg;

/* load file (key = value, # comments). return 1 when read, 0 when
 * missing (defaults stay). */
int SimConfig_Load(const char *path);

#endif
