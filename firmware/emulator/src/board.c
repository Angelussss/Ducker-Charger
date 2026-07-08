/**
 * @file    board.c
 * @brief   Ducker-Charger board model: battery physics, power flow,
 *          NTC dividers and shunts feeding the ADC / gauge / INA3221.
 *
 * Topology (from the schematic):
 *   4S3P Murata/Sony VTC5 pack (7800 mAh) behind BQ7791500 BMS
 *   C1 (TPS25750 + BQ25713): charge input when a charger is attached
 *   A1/A2: 5 V load switches (PC1/PC2) through 10 mOhm INA3221 shunts
 *   C2: STPD01 buck (EN=PC11) + port switch (PA12), sink draws the
 *       negotiated RDO current when both are enabled
 *   NTC zones 1-4 + on-board NTC: 10 k pull-up dividers on ADC1
 */
#include "sim.h"

#include <math.h>
#include <string.h>

BoardState board;
EmuConfig  cfg;

/* 4S pack OCV as a function of SoC (per-cell curve x4) */
static float ocv_mv(float soc)
{
    static const float soc_pts[] = {   0,  5,  10,  25,  50,  75,  90, 100 };
    static const float mv_pts[]  = { 11600, 12600, 13200, 13900, 14700,
                                     15600, 16200, 16800 };
    if (soc <= 0)   return mv_pts[0];
    if (soc >= 100) return mv_pts[7];
    int i = 1;
    while (soc > soc_pts[i]) i++;
    float t = (soc - soc_pts[i - 1]) / (soc_pts[i] - soc_pts[i - 1]);
    return mv_pts[i - 1] + t * (mv_pts[i] - mv_pts[i - 1]);
}

void board_init(void)
{
    memset(&board, 0, sizeof(board));
    board.soc = cfg.soc;
    board.temp_c = cfg.ambient_c;
    board.board_temp_c = cfg.ambient_c;
    board.pack_mv = ocv_mv(board.soc);
}

void board_tick(uint32_t wall_dms)
{
    float dt_s = (float)wall_dms / 1000.0f * cfg.time_scale;

    /* ---- output side: which loads actually draw current ---- */
    int a1_en   = sim_gpio_get(2, 1);    /* PC1                  */
    int a2_en   = sim_gpio_get(2, 2);    /* PC2                  */
    int stpd_en = sim_gpio_get(2, 11);   /* PC11                 */
    int c2_en   = sim_gpio_get(0, 12);   /* PA12                 */

    board.a1_ma = (a1_en && board.a1_load_attached) ? cfg.a1_load_ma : 0.0f;
    board.a2_ma = (a2_en && board.a2_load_attached) ? cfg.a2_load_ma : 0.0f;
    board.c2_ma = (stpd_en && c2_en && board.c2_sink_attached)
                  ? (float)cfg.c2_ma : 0.0f;
    /* C1 sourcing: TPS25750 + BQ25713 run the OTG boost autonomously on
     * their private I2C once the provider contract is in place — no MCU
     * enable is required (EN_OTG/PB15 is only read back by readNCS) */
    board.c1_out_ma = board.c1_device_attached ? (float)cfg.c1src_ma : 0.0f;

    /* ---- input side: CC charge with taper near full ---- */
    if (board.charger_attached && board.soc < 100.0f) {
        float taper = (100.0f - board.soc) / 8.0f;
        if (taper > 1.0f) taper = 1.0f;
        board.charge_in_ma = cfg.charge_ma * (taper < 0.06f ? 0.06f : taper);
    } else {
        board.charge_in_ma = 0.0f;
    }

    /* 5 V rails come from the pack through the bucks: reflect port power
     * back to pack current (assume ~92 % efficiency) */
    float out_pack_ma = (board.a1_ma + board.a2_ma) * (5000.0f / board.pack_mv) / 0.92f
                      + board.c2_ma * (stpd_vout_mv() / board.pack_mv) / 0.92f
                      + board.c1_out_ma * ((float)cfg.c1src_mv / board.pack_mv) / 0.92f
                      + cfg.quiescent_ma;

    board.i_batt_ma = board.charge_in_ma - out_pack_ma;

    /* ---- integrate SoC ---- */
    float cap_mah = 7800.0f * (float)cfg.soh / 100.0f;
    board.soc += board.i_batt_ma * dt_s / 3600.0f / cap_mah * 100.0f;
    if (board.soc > 100.0f) board.soc = 100.0f;
    if (board.soc < 0.0f)   board.soc = 0.0f;

    /* terminal voltage = OCV + IR drop over ~60 mOhm pack resistance */
    board.pack_mv = ocv_mv(board.soc) + board.i_batt_ma * 0.06f;

    /* crude thermal model: heat with |I|, relax to ambient */
    float target = cfg.ambient_c
                 + fabsf(board.i_batt_ma) / 1000.0f * 6.0f;
    board.temp_c += (target - board.temp_c) * (1.0f - expf(-dt_s / 60.0f));
    board.board_temp_c = board.temp_c + 3.0f;
}

/* ---------------- ADC scan sequence ----------------
 * charge.c rank order: NTC1-4, on-board NTC, USB-C input current,
 * battery current (discarded by fw), system power                  */

static uint16_t ntc_raw(float temp_c)
{
    /* NTC 10k B3950 low-side, 10k pull-up to 3.3 V (matches charge.c
     * toResistance/toCelsius constants) */
    float t_k = temp_c + 273.15f;
    float r = 10000.0f * expf(3950.0f * (1.0f / t_k - 1.0f / 298.15f));
    float v = 3300.0f * r / (r + 10000.0f);
    return (uint16_t)(v / 3300.0f * 4095.0f);
}

static uint16_t mv_to_raw(float mv)
{
    if (mv < 0) mv = 0;
    if (mv > 3300.0f) mv = 3300.0f;
    return (uint16_t)(mv / 3300.0f * 4095.0f);
}

uint16_t board_adc_sample(int rank)
{
    switch (rank) {
    case 0: case 1: case 2: case 3:
        return ntc_raw(board.temp_c + rank);          /* zone gradient  */
    case 4:
        return ntc_raw(board.board_temp_c);
    case 5:  /* USB-C input current: 0.2 mV per mA at the sense amp */
        return mv_to_raw(board.charge_in_ma * 0.2f);
    case 6:  /* battery current channel (fw discards it) */
        return mv_to_raw(fabsf(board.i_batt_ma) * 0.2f);
    case 7: { /* system power: fw computes (V/1000)*60 W */
        float p_w = (board.a1_ma * 5.0f + board.a2_ma * 5.0f
                     + board.c2_ma * stpd_vout_mv() / 1000.0f) / 1000.0f;
        return mv_to_raw(p_w / 60.0f * 1000.0f);
    }
    default:
        return 0;
    }
}
