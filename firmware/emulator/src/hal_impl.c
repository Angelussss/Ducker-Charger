/**
 * @file    hal_impl.c
 * @brief   Fake HAL implementation: clock, GPIO+EXTI, I2C routing,
 *          ADC scan sequence, SPI->panel, STOP mode, MX_* stubs.
 */
#include "stm32f4xx_hal.h"
#include "sim.h"
#include "system/fsm.h"     /* PB_FSM_ActiveState: FSM column in the log */

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ---------------- lock / time ---------------- */

/* recursive: device models may re-enter (e.g. an I2C write that raises an
 * IRQ line which triggers the wake signal) */
static pthread_mutex_t g_lock;
static void __attribute__((constructor)) lock_init(void)
{
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&g_lock, &a);
}
void sim_lock(void)   { pthread_mutex_lock(&g_lock); }
void sim_unlock(void) { pthread_mutex_unlock(&g_lock); }

static struct timespec t0;
static int t0_valid = 0;

uint32_t sim_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    if (!t0_valid) { t0 = ts; t0_valid = 1; }
    return (uint32_t)((ts.tv_sec - t0.tv_sec) * 1000
                      + (ts.tv_nsec - t0.tv_nsec) / 1000000);
}

static const char *state_name(State_ID_t st)
{
    static const char *names[STATE_NUMBER] = {
        [STATE_DEEP_SLEEP] = "DEEP_SLEEP", [STATE_SLEEP] = "SLEEP",
        [STATE_IDLE] = "IDLE", [STATE_SAFETY_LOCK] = "SAFETY_LOCK",
        [STATE_LOW_V] = "LOW_V", [STATE_EMERGENCY] = "EMERGENCY",
        [STATE_CHARGING] = "CHARGING", [STATE_MANUAL] = "MANUAL",
        [STATE_ERROR] = "ERROR",
    };
    return (st < STATE_NUMBER && names[st]) ? names[st] : "?";
}

const char *sim_fsm_name(void)
{
    return state_name(PB_FSM_ActiveState());
}

static const char *event_name(Event_t evt)
{
    static const char *names[EVT_NUMBER] = {
        [EVT_CHARGER_CONNECTED]    = "CHARGER_CONNECTED",
        [EVT_CHARGER_DISCONNECTED] = "CHARGER_DISCONNECTED",
        [EVT_SOC_SAFETY]           = "SOC_SAFETY",
        [EVT_SOC_LOWV]             = "SOC_LOWV",
        [EVT_SOC_UNDERV]           = "SOC_UNDERV",
        [EVT_SOC_OK]               = "SOC_OK",
        [EVT_SOC_OVCH]             = "SOC_OVCH",
        [EVT_FAULT_OT]             = "FAULT_OT",
        [EVT_FAULT_CRITICAL]       = "FAULT_CRITICAL",
        [EVT_FAULT_OCC]            = "FAULT_OCC",
        [EVT_ERROR]                = "ERROR",
        [EVT_ERROR_CLEAR]          = "ERROR_CLEAR",
        [EVT_INACTIVITY]           = "INACTIVITY",
        [EVT_BUTTON_SHORT]         = "BUTTON_SHORT",
        [EVT_BUTTON_LONG]          = "BUTTON_LONG",
        [EVT_MANUAL_ENTER]         = "MANUAL_ENTER",
        [EVT_MANUAL_EXIT]          = "MANUAL_EXIT",
        [EVT_LOCK]                 = "LOCK",
        [EVT_UNLOCK]               = "UNLOCK",
    };
    return (evt < EVT_NUMBER && names[evt]) ? names[evt] : "?";
}

/* overrides the weak no-op in fsm.c: every event fired into the FSM ends
 * up in the log, with the transition it caused (or didn't) */
void PB_FSM_TraceEvent(State_ID_t from, Event_t event, State_ID_t to,
                       bool blocked)
{
    if (blocked)
        sim_log("[FSM ] %s: %s -> %s BLOCKED by guard",
                event_name(event), state_name(from), state_name(to));
    else if (to == STATE_NUMBER)
        sim_log("[FSM ] %s ignored in %s",
                event_name(event), state_name(from));
    else
        sim_log("[FSM ] %s: %s -> %s",
                event_name(event), state_name(from), state_name(to));
}

void sim_log(const char *fmt, ...)
{
    /* one buffered fputs per line: the firmware thread and the scenario
     * thread both log, split fprintf calls interleave mid-line */
    char line[256];
    int n = snprintf(line, sizeof(line) - 2, "[%8u] [%-11s] ",
                     sim_now_ms(), sim_fsm_name());
    va_list ap;
    va_start(ap, fmt);
    n += vsnprintf(line + n, sizeof(line) - 2 - (size_t)n, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof(line) - 2) n = (int)sizeof(line) - 2;
    line[n] = '\n';
    line[n + 1] = '\0';
    fputs(line, stderr);
}

HAL_StatusTypeDef HAL_Init(void) { (void)sim_now_ms(); return HAL_OK; }
uint32_t HAL_GetTick(void)       { return sim_now_ms(); }

void HAL_Delay(uint32_t ms)
{
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

void HAL_SuspendTick(void) {}
void HAL_ResumeTick(void)  {}

HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef *cfg)
{ (void)cfg; return HAL_OK; }
HAL_StatusTypeDef HAL_RCC_ClockConfig(RCC_ClkInitTypeDef *cfg, uint32_t lat)
{ (void)cfg; (void)lat; return HAL_OK; }

/* ---------------- STOP mode ----------------
 * Blocks the firmware thread until a wake edge arrives (encoder button
 * EXTI on the real board, or the 'w' key in the emulator window). */

static pthread_cond_t wake_cond = PTHREAD_COND_INITIALIZER;
static int wake_flag = 0;

void sim_wake_signal(void)
{
    sim_lock();
    wake_flag = 1;
    pthread_cond_broadcast(&wake_cond);
    sim_unlock();
}

void HAL_PWR_EnterSTOPMode(uint32_t regulator, uint32_t entry)
{
    (void)regulator; (void)entry;
    sim_log("[MCU ] STOP mode entered (deep sleep) — press 'w' or the encoder to wake");
    sim_lock();
    wake_flag = 0;
    while (!wake_flag)
        pthread_cond_wait(&wake_cond, &g_lock);
    sim_unlock();
    sim_log("[MCU ] STOP mode exit");
}

/* ---------------- GPIO ---------------- */

GPIO_TypeDef sim_GPIOA = { 0 }, sim_GPIOB = { 1 }, sim_GPIOC = { 2 };

static uint8_t  pin_level[3][16];
static uint16_t exti_mask[3];       /* pins configured as EXTI */

static const char port_names[3] = { 'A', 'B', 'C' };

static int pin_num(uint16_t pin)
{
    int n = 0;
    while (!(pin & 1u)) { pin >>= 1; n++; }
    return n;
}

int sim_gpio_get(int port, int pinno) { return pin_level[port][pinno]; }

void sim_gpio_drive_input(int port, int pinno, int lv)
{
    /* caller may or may not hold the sim lock; pin table writes are
     * single-byte and the EXTI callback tolerates races like real HW */
    int old = pin_level[port][pinno];
    pin_level[port][pinno] = (uint8_t)(lv ? 1 : 0);
    if (old != (lv ? 1 : 0) && (exti_mask[port] & (1u << pinno))) {
        HAL_GPIO_EXTI_IRQHandler((uint16_t)(1u << pinno));
        sim_wake_signal();
    }
}

void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init)
{
    if (init->Mode & 0x10000000u)   /* any GPIO_MODE_IT_* */
        exti_mask[port->idx] |= (uint16_t)init->Pin;
}

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState st)
{
    int n = pin_num(pin);
    int lv = (st == GPIO_PIN_SET) ? 1 : 0;
    if (pin_level[port->idx][n] != lv) {
        pin_level[port->idx][n] = (uint8_t)lv;
        sim_gpio_output_changed(port->idx, n, lv);
    }
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
    return pin_level[port->idx][pin_num(pin)] ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

void HAL_GPIO_EXTI_IRQHandler(uint16_t pin) { HAL_GPIO_EXTI_Callback(pin); }

void HAL_NVIC_SetPriority(IRQn_Type irq, uint32_t p, uint32_t s)
{ (void)irq; (void)p; (void)s; }
void HAL_NVIC_EnableIRQ(IRQn_Type irq) { (void)irq; }

/* output-pin names for the log (indexed lookup, else generic) */
static const char *pin_name(int port, int pinno)
{
    if (port == 2 && pinno == 1)  return "USB_A1_EN";
    if (port == 2 && pinno == 2)  return "USB_A2_EN";
    if (port == 2 && pinno == 11) return "STPD01_EN";
    if (port == 0 && pinno == 12) return "USB_C2_EN";
    if (port == 0 && pinno == 11) return "C2_LAB_EN";
    if (port == 1 && pinno == 15) return "EN_OTG";
    if (port == 1 && pinno == 8)  return "BCKL_CTRL";
    return NULL;
}

void __attribute__((weak)) sim_gpio_output_changed_hook(int p, int n, int lv)
{ (void)p; (void)n; (void)lv; }

void sim_gpio_output_changed(int port, int pinno, int lv)
{
    const char *nm = pin_name(port, pinno);
    if (nm)
        sim_log("[GPIO] %s = %d", nm, lv);
    else if (!(port == 1 && pinno <= 2))   /* mute display CS/DC/RST churn */
        sim_log("[GPIO] P%c%d = %d", port_names[port], pinno, lv);
}

/* ---------------- I2C bus + routing ---------------- */

#define MAX_DEV 8
static SimI2CDev *bus_dev[4][MAX_DEV];   /* index by bus 1 / 3 */
static int        bus_ndev[4];

void sim_i2c_attach(int bus, SimI2CDev *dev)
{
    bus_dev[bus][bus_ndev[bus]++] = dev;
}

SimI2CDev *sim_i2c_find(int bus, uint16_t addr8)
{
    for (int i = 0; i < bus_ndev[bus]; i++)
        if (bus_dev[bus][i]->addr8 == addr8)
            return bus_dev[bus][i];
    return NULL;
}

static SimI2CDev *route(I2C_HandleTypeDef *h, uint16_t addr)
{
    SimI2CDev *d = sim_i2c_find(h->bus, addr);
    if (!d)
        sim_log("[I2C%d] NACK addr 0x%02X (no device)", h->bus, addr >> 1);
    return d;
}

HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *h, uint16_t addr,
                                   uint16_t mem, uint16_t memsize,
                                   uint8_t *buf, uint16_t n, uint32_t to)
{
    (void)to;
    HAL_StatusTypeDef r;
    sim_lock();
    SimI2CDev *d = route(h, addr);
    r = (d && d->mem_read && d->mem_read(d, mem, memsize, buf, n) == 0)
        ? HAL_OK : HAL_ERROR;
    sim_unlock();
    return r;
}

HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *h, uint16_t addr,
                                    uint16_t mem, uint16_t memsize,
                                    uint8_t *buf, uint16_t n, uint32_t to)
{
    (void)to;
    HAL_StatusTypeDef r;
    sim_lock();
    SimI2CDev *d = route(h, addr);
    r = (d && d->mem_write && d->mem_write(d, mem, memsize, buf, n) == 0)
        ? HAL_OK : HAL_ERROR;
    sim_unlock();
    return r;
}

HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *h, uint16_t addr,
                                          uint8_t *buf, uint16_t n, uint32_t to)
{
    (void)to;
    HAL_StatusTypeDef r;
    sim_lock();
    SimI2CDev *d = route(h, addr);
    r = (d && d->mtx && d->mtx(d, buf, n) == 0) ? HAL_OK : HAL_ERROR;
    sim_unlock();
    return r;
}

HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *h, uint16_t addr,
                                         uint8_t *buf, uint16_t n, uint32_t to)
{
    (void)to;
    HAL_StatusTypeDef r;
    sim_lock();
    SimI2CDev *d = route(h, addr);
    r = (d && d->mrx && d->mrx(d, buf, n) == 0) ? HAL_OK : HAL_ERROR;
    sim_unlock();
    return r;
}

/* ---------------- ADC scan sequence ---------------- */

HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *h)
{ h->cur = -1; return HAL_OK; }

HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *h)
{ (void)h; return HAL_OK; }

HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *h, uint32_t to)
{ (void)to; if (h->cur < 7) h->cur++; return HAL_OK; }

uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *h)
{
    uint32_t v;
    sim_lock();
    v = board_adc_sample(h->cur < 0 ? 0 : h->cur);
    sim_unlock();
    return v;
}

/* ---------------- SPI -> display panel ---------------- */

HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *h, uint8_t *d,
                                   uint16_t n, uint32_t to)
{
    (void)h; (void)to;
    sim_lock();
    panel_spi_bytes(d, n);
    sim_unlock();
    return HAL_OK;
}

/* ---------------- TIM / UART ---------------- */

HAL_StatusTypeDef HAL_TIM_Encoder_Start(TIM_HandleTypeDef *h, uint32_t ch)
{ (void)h; (void)ch; return HAL_OK; }

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *h, uint8_t *d,
                                    uint16_t n, uint32_t to)
{ (void)h; (void)to; fwrite(d, 1, n, stderr); return HAL_OK; }

/* ---------------- peripheral handles + MX_* stubs ----------------
 * Replaces CubeMX Core/Src/{adc,gpio,i2c,spi,tim,usart}.c            */

ADC_HandleTypeDef  hadc1  = { -1 };
I2C_HandleTypeDef  hi2c1  = { 1 };
I2C_HandleTypeDef  hi2c3  = { 3 };
SPI_HandleTypeDef  hspi1  = { 1 };
UART_HandleTypeDef huart1 = { 1 };

static TIM_TypeDef tim3_regs;
TIM_HandleTypeDef  htim3  = { &tim3_regs };

void sim_encoder_step(int detents)
{
    /* EC11 in TI12 mode: 2 counts per detent (see encoder.c) */
    tim3_regs.CNT = (uint16_t)(tim3_regs.CNT + 2 * detents);
}

void MX_ADC1_Init(void) {}
void MX_I2C1_Init(void) {}
void MX_I2C3_Init(void) {}
void MX_SPI1_Init(void) {}
void MX_TIM3_Init(void) {}
void MX_USART1_UART_Init(void) {}

void MX_GPIO_Init(void)
{
    /* reset states as on the real board after MX_GPIO_Init():
     * IRQ inputs idle HIGH (open-drain released into pull-ups).
     * CHRG_OK is the BQ25713 power-good output: LOW until a valid
     * adapter is present — faithful even though it means charge.c
     * pushes EVT_ERROR on the very first readCS(). */
    sim_gpio_drive_input(1, 14, 1);  /* PB14 PD_IRQ        */
    sim_gpio_drive_input(2, 12, 1);  /* PC12 STPD01_INT    */
    sim_gpio_drive_input(2, 3,  1);  /* PC3  CYPD3175_INT  */
    sim_gpio_drive_input(1, 13, board.charger_attached ? 1 : 0);
    sim_gpio_drive_input(2, 0,  1);  /* PC0  encoder button (active low) */
}
