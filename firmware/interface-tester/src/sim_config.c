#include "sim_config.h"
#include <stdio.h>
#include <string.h>

/* defaults = the usual demo numbers */
SimCfg simcfg = {
    .soc = 73, .pack_mv = 15420, .batt_ma = 2150, .batt_ripple = 2200,
    .temp_c = 31, .vbus = 1, .full = 0, .phase = 1,
    .port = {
        { 1,  5000, 1800, 350 },   /* A1 */
        { 0,  5000,    0,   0 },   /* A2 */
        { 1,  5000,  900, 200 },   /* C1 OTG */
        { 1, 12000, 2500, 500 },   /* C2 */
        { 0,  5000,    0,   0 },   /* LAB */
    },
    .cycles = 87, .health = 94, .cap_full_mah = 8460, .cap_design_mah = 9000,
    .charges = 3, .max_temp_c = 42, .max_out_ma = 3120, .max_in_ma = 4460,
    .energy_mwh = 18540, .uptime_s = 5315,
    .tte_min = 158, .ttf_min = 72,
};

typedef struct { const char *key; int *dst; } Entry;

int SimConfig_Load(const char *path)
{
    const Entry map[] = {
        {"soc", &simcfg.soc}, {"pack_mv", &simcfg.pack_mv},
        {"batt_ma", &simcfg.batt_ma}, {"batt_ripple", &simcfg.batt_ripple},
        {"temp_c", &simcfg.temp_c}, {"vbus", &simcfg.vbus},
        {"full", &simcfg.full}, {"phase", &simcfg.phase},
        {"a1_active",  &simcfg.port[0].active}, {"a1_mv",  &simcfg.port[0].mv},
        {"a1_ma",      &simcfg.port[0].ma},     {"a1_ripple", &simcfg.port[0].ripple},
        {"a2_active",  &simcfg.port[1].active}, {"a2_mv",  &simcfg.port[1].mv},
        {"a2_ma",      &simcfg.port[1].ma},     {"a2_ripple", &simcfg.port[1].ripple},
        {"c1_active",  &simcfg.port[2].active}, {"c1_mv",  &simcfg.port[2].mv},
        {"c1_ma",      &simcfg.port[2].ma},     {"c1_ripple", &simcfg.port[2].ripple},
        {"c2_active",  &simcfg.port[3].active}, {"c2_mv",  &simcfg.port[3].mv},
        {"c2_ma",      &simcfg.port[3].ma},     {"c2_ripple", &simcfg.port[3].ripple},
        {"lab_active", &simcfg.port[4].active}, {"lab_mv", &simcfg.port[4].mv},
        {"lab_ma",     &simcfg.port[4].ma},     {"lab_ripple", &simcfg.port[4].ripple},
        {"cycles", &simcfg.cycles}, {"health", &simcfg.health},
        {"cap_full_mah", &simcfg.cap_full_mah}, {"cap_design_mah", &simcfg.cap_design_mah},
        {"charges", &simcfg.charges}, {"max_temp_c", &simcfg.max_temp_c},
        {"max_out_ma", &simcfg.max_out_ma}, {"max_in_ma", &simcfg.max_in_ma},
        {"energy_mwh", &simcfg.energy_mwh}, {"uptime_s", &simcfg.uptime_s},
        {"tte_min", &simcfg.tte_min}, {"ttf_min", &simcfg.ttf_min},
    };
    const int nmap = (int)(sizeof(map) / sizeof(map[0]));

    FILE *f = fopen(path, "r");
    if (!f) return 0;

    char line[128], key[64];
    int val, lineno = 0;
    while (fgets(line, sizeof(line), f)) {
        lineno++;
        char *h = strchr(line, '#');       /* comments also inline */
        if (h) *h = '\0';
        if (sscanf(line, " %63[a-z0-9_] = %d", key, &val) != 2) continue;
        int found = 0;
        for (int i = 0; i < nmap; i++)
            if (strcmp(map[i].key, key) == 0) { *map[i].dst = val; found = 1; break; }
        if (!found)
            fprintf(stderr, "sim_config: unknown key '%s' (line %d)\n", key, lineno);
    }
    fclose(f);
    fprintf(stderr, "sim_config: loaded %s\n", path);
    return 1;
}
