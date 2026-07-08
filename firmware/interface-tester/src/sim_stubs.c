/* fake HAL / encoder / telemetry for native UI sim.
 * encoder driven by scripted (or interactive) event queue. */
#include "stm32f4xx_hal.h"
#include "app/encoder.h"
#include "app/telemetry.h"
#include "sim_config.h"
#include <math.h>
#include <string.h>

GPIO_TypeDef SIM_GPIOA, SIM_GPIOB, SIM_GPIOC;

/* ---- fake clock ---- */
static uint32_t sim_tick = 0;
uint32_t HAL_GetTick(void) { return sim_tick; }
void HAL_Delay(uint32_t ms) { sim_tick += ms; }
void sim_advance(uint32_t ms) { sim_tick += ms; }

/* ---- GPIO: remember last write so UI can read it back ---- */
static GPIO_PinState pin_state[3][16];
static int port_idx(GPIO_TypeDef *p) {
    if (p == &SIM_GPIOA) return 0;
    if (p == &SIM_GPIOB) return 1;
    return 2;
}
static int pin_num(uint16_t pin) { int n = 0; while (!(pin & 1)) { pin >>= 1; n++; } return n; }
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState st)
{ pin_state[port_idx(port)][pin_num(pin)] = st; }
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{ return pin_state[port_idx(port)][pin_num(pin)]; }

HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *h, uint8_t *d, uint16_t n, uint32_t t)
{ (void)h; (void)d; (void)n; (void)t; return HAL_OK; }
HAL_StatusTypeDef HAL_TIM_Encoder_Start(TIM_HandleTypeDef *h, uint32_t ch)
{ (void)h; (void)ch; return HAL_OK; }

/* ---- encoder stub: harness push values in ---- */
static int8_t  enc_next_delta = 0;
static uint8_t enc_next_press = 0;
static uint8_t enc_held       = 0;
void sim_encoder_rotate(int8_t d) { enc_next_delta = d; }
void sim_encoder_press(void)      { enc_next_press = 1; }
void sim_encoder_hold(uint8_t h)  { enc_held = h; }

void   Encoder_Init(TIM_HandleTypeDef *htim_ptr) { (void)htim_ptr; }
int8_t Encoder_GetDelta(void)  { int8_t d = enc_next_delta; enc_next_delta = 0; return d; }
uint8_t Encoder_IsPressed(void){ uint8_t p = enc_next_press; enc_next_press = 0; return p; }
uint8_t Encoder_IsHeld(void)   { return enc_held; }

/* ---- telemetry stub: believable fake numbers + sine history ---- */
SystemTelemetry_t telemetry;
uint16_t cell_mv[4];
uint16_t tte_min, ttf_min;
PortStats_t port_stats[5];
SystemStats_t sys_stats;

void sim_stats_fill(void)
{
    sys_stats.cycle_count        = simcfg.cycles;
    sys_stats.state_of_health    = simcfg.health;
    sys_stats.full_cap_mAh       = simcfg.cap_full_mah;
    sys_stats.design_cap_mAh     = simcfg.cap_design_mah;
    sys_stats.charge_sessions    = simcfg.charges;
    sys_stats.max_temp_c         = simcfg.max_temp_c;
    sys_stats.max_current_out_mA = simcfg.max_out_ma;
    sys_stats.max_current_in_mA  = simcfg.max_in_ma;
    sys_stats.energy_out_mWh     = simcfg.energy_mwh;
    sys_stats.uptime_s           = simcfg.uptime_s;
}

void sim_stats_tick(uint32_t ms)
{
    static uint32_t acc = 0;
    acc += ms;
    while (acc >= 1000) { acc -= 1000; sys_stats.uptime_s++; }
    if (telemetry.temp_celsius > sys_stats.max_temp_c)
        sys_stats.max_temp_c = telemetry.temp_celsius;
    if (telemetry.current_mA > sys_stats.max_current_in_mA)
        sys_stats.max_current_in_mA = telemetry.current_mA;
    if (-telemetry.current_mA > sys_stats.max_current_out_mA)
        sys_stats.max_current_out_mA = -telemetry.current_mA;
}

void sim_ports_fill(void)
{
    for (int pI = 0; pI < 5; pI++) {
        port_stats[pI].voltage_mv = simcfg.port[pI].active ? simcfg.port[pI].mv : 0;
        port_stats[pI].active = simcfg.port[pI].active;
        for (int i = 0; i < TELEMETRY_HISTORY_SIZE; i++) {
            double ph = (double)i / TELEMETRY_HISTORY_SIZE;
            double val = simcfg.port[pI].ma
                       + simcfg.port[pI].ripple * sin(ph * 6.28318 * (2.0 + pI));
            if (val < 0) val = 0;
            port_stats[pI].history[i] = (int16_t)val;
        }
        port_stats[pI].idx = 0;
        port_stats[pI].current_mA = port_stats[pI].history[TELEMETRY_HISTORY_SIZE - 1];
    }
}

void sim_ports_push(void)
{
    static double ph = 0.0;
    ph += 0.3;
    for (int pI = 0; pI < 5; pI++) {
        if (!port_stats[pI].active) continue;
        double val = simcfg.port[pI].ma
                   + simcfg.port[pI].ripple * sin(ph * (1.0 + 0.4 * pI));
        if (val < 0) val = 0;
        port_stats[pI].history[port_stats[pI].idx] = (int16_t)val;
        port_stats[pI].idx = (port_stats[pI].idx + 1) % TELEMETRY_HISTORY_SIZE;
        port_stats[pI].current_mA = (int16_t)val;
    }
}

void sim_telemetry_fill(void)
{
    memset(&telemetry, 0, sizeof(telemetry));
    telemetry.soc_percent  = (uint8_t)simcfg.soc;
    telemetry.voltage_mV   = (uint16_t)simcfg.pack_mv;
    telemetry.current_mA   = (int16_t)simcfg.batt_ma;
    telemetry.temp_celsius = (int16_t)simcfg.temp_c;
    telemetry.is_charging  = (simcfg.batt_ma > 0);
    telemetry.over_temp    = (simcfg.temp_c >= 60);   /* demo threshold */
    for (int i = 0; i < 4; i++)
        cell_mv[i] = simcfg.cell_mv[i] ? (uint16_t)simcfg.cell_mv[i]
                                       : (uint16_t)(simcfg.pack_mv / 4);
    tte_min = (uint16_t)simcfg.tte_min;
    ttf_min = (uint16_t)simcfg.ttf_min;
    telemetry.vbus_present = (uint8_t)simcfg.vbus;
    telemetry.is_full      = (uint8_t)simcfg.full;
    telemetry.charge_phase = (uint8_t)simcfg.phase;
    telemetry.power_mW     = (int32_t)telemetry.voltage_mV * telemetry.current_mA / 1000;
    telemetry.sensor_ok    = 1;
    /* starting history: base + ripple (0 = flat) */
    for (int i = 0; i < TELEMETRY_HISTORY_SIZE; i++) {
        double ph = (double)i / TELEMETRY_HISTORY_SIZE;
        telemetry.current_history[i] = (int16_t)(simcfg.batt_ma
            + simcfg.batt_ripple * sin(ph * 6.28318 * 1.5));
    }
    telemetry.history_idx  = 0;
    telemetry.history_full = 1;
}

void Telemetry_Init(I2C_HandleTypeDef *a, I2C_HandleTypeDef *b) { (void)a; (void)b; sim_telemetry_fill(); sim_ports_fill(); sim_stats_fill(); }
uint8_t Telemetry_Poll(void) { return 0; }
void Telemetry_ForcePoll(void) { }
