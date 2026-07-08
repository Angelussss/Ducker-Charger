/**
 * @file    emu_config.c
 * @brief   emu_config.ini parser -> EmuConfig (key = value, # comments).
 */
#include "sim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_defaults(void)
{
    cfg.soc              = 65.0f;
    cfg.time_scale       = 60.0f;    /* 1 min wall = 1 h battery */
    cfg.ambient_c        = 25.0f;
    cfg.charge_ma        = 3000.0f;
    cfg.a1_load_ma       = 1200.0f;
    cfg.a2_load_ma       = 800.0f;
    cfg.quiescent_ma     = 30.0f;
    cfg.c1_mv            = 20000; cfg.c1_ma = 3000;
    cfg.c1src_mv         = 5000;  cfg.c1src_ma = 3000;
    cfg.c2_mv            = 15000; cfg.c2_ma = 2000;
    cfg.tps_app_mode     = 0;        /* cold boot: PTCH -> patch flow */
    cfg.tps_strict_len   = 1;        /* faithful to the TRM            */
    cfg.gauge_provisioned = 0;       /* exercise provisioning.c        */
    cfg.bckl_active_low  = 1;        /* JP402 in PMOS, as ili9341.c says */
    cfg.soh              = 97;
    cfg.cycles           = 42;
}

void EmuConfig_Load(const char *path)
{
    set_defaults();

    FILE *f = fopen(path, "r");
    if (!f) {
        sim_log("[CFG ] %s not found, using defaults", path);
        return;
    }

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char key[64];
        float val;
        if (line[0] == '#' || line[0] == ';' || line[0] == '\n')
            continue;
        if (sscanf(line, " %63[a-zA-Z0-9_] = %f", key, &val) != 2)
            continue;

        if      (!strcmp(key, "soc"))               cfg.soc = val;
        else if (!strcmp(key, "time_scale"))        cfg.time_scale = val;
        else if (!strcmp(key, "ambient_c"))         cfg.ambient_c = val;
        else if (!strcmp(key, "charge_ma"))         cfg.charge_ma = val;
        else if (!strcmp(key, "a1_load_ma"))        cfg.a1_load_ma = val;
        else if (!strcmp(key, "a2_load_ma"))        cfg.a2_load_ma = val;
        else if (!strcmp(key, "quiescent_ma"))      cfg.quiescent_ma = val;
        else if (!strcmp(key, "c1_mv"))             cfg.c1_mv = (int)val;
        else if (!strcmp(key, "c1_ma"))             cfg.c1_ma = (int)val;
        else if (!strcmp(key, "c1src_mv"))          cfg.c1src_mv = (int)val;
        else if (!strcmp(key, "c1src_ma"))          cfg.c1src_ma = (int)val;
        else if (!strcmp(key, "c2_mv"))             cfg.c2_mv = (int)val;
        else if (!strcmp(key, "c2_ma"))             cfg.c2_ma = (int)val;
        else if (!strcmp(key, "tps_app_mode"))      cfg.tps_app_mode = (int)val;
        else if (!strcmp(key, "tps_strict_len"))    cfg.tps_strict_len = (int)val;
        else if (!strcmp(key, "gauge_provisioned")) cfg.gauge_provisioned = (int)val;
        else if (!strcmp(key, "bckl_active_low"))   cfg.bckl_active_low = (int)val;
        else if (!strcmp(key, "soh"))               cfg.soh = (int)val;
        else if (!strcmp(key, "cycles"))            cfg.cycles = (int)val;
        else sim_log("[CFG ] unknown key '%s'", key);
    }
    fclose(f);
    sim_log("[CFG ] loaded %s (soc=%.0f%% strict=%d provisioned=%d tps=%s)",
            path, cfg.soc, cfg.tps_strict_len, cfg.gauge_provisioned,
            cfg.tps_app_mode ? "APP" : "PTCH");
}
