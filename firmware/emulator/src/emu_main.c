/**
 * @file    emu_main.c
 * @brief   Emulator entry point: SDL window + scenario keys + firmware
 *          thread. The unmodified firmware main() (renamed fw_main by
 *          the Makefile) runs in its own thread against the fake HAL.
 *
 * Keys:
 *   arrows / wheel   encoder rotation
 *   SPACE            encoder button (hold = long press)
 *   c                plug / unplug the C1 charger
 *   v                attach / detach a C2 PD sink
 *   1 / 2            attach / detach a load on USB-A1 / A2
 *   l                attach / detach a resistive load on the lab output
 *   o                INA3221 critical alert on A1 (overcurrent)
 *   t/h/u/f          gauge faults: overtemp / BATHI / BATLOW / CF
 *   b                gauge I2C bus failure (NACK everything)
 *   x / z / r        CYPD3175: OVP / OCP / hard reset
 *   g                STPD01 short-circuit fault
 *   k                toggle CHRG_OK line
 *   w                wake from STOP mode
 *   q                shutdown (long-press) -> DEEP_SLEEP (from IDLE/SLEEP)
 *   i                print board/firmware status
 *   s                dump screen to screen.ppm
 *   ESC              quit
 *
 * Headless: emulator --headless <ms> [--at <ms>:<key> ...]
 *           runs without a window, injects scripted keys, dumps
 *           screen.ppm and a status report on exit.
 */
#include "sim.h"

#include "system/charge.h"      /* status getters (real firmware code) */
#include "system/event.h"       /* event_push, EVT_BUTTON_LONG */
#include "system/fsm.h"         /* PB_FSM_ActiveState */
#include "app/telemetry.h"      /* port_stats: what the UI actually shows */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int fw_main(void);              /* firmware main(), renamed */

static void *fw_thread(void *arg)
{
    (void)arg;
    sim_log("[EMU ] firmware thread started");
    fw_main();
    return NULL;
}

/* ---------------- scenario toggles ---------------- */

static int tog_a1_oc, tog_ot, tog_bathi, tog_batlow, tog_cf, tog_stpd,
           tog_gg_bus;

static void set_charger(bool on)
{
    board.charger_attached = on;
    sim_gpio_drive_input(1, 13, on ? 1 : 0);    /* CHRG_OK (BQ25713) */
    tps_plug_charger(on);
}

static void print_status(void)
{
    sim_lock();
    FuelGaugeSensors fg = getFuelGaugeData();
    PDContract c1 = getPrimaryUSBC_Contract();
    PDContract c2 = getSecondaryUSBC_Contract();
    INA3221_Sensors ina = getINA3221_Sensors();
    fprintf(stderr,
        "---------------- STATUS ----------------\n"
        "fsm:    %s\n"
        "board:  SoC %.1f%%  V %.0f mV  I %+.0f mA  T %.1f C\n"
        "gauge:  SoC %.0f%%  V %.0f mV  I %.0f mA  flags[CHG=%d DSG=%d BATHI=%d BATLOW=%d OT=%d]\n"
        "C1:     plugged=%d sink=%d nego=%d  %.0f mV / %.0f mA\n"
        "C2:     plugged=%d nego=%d  %.0f mV / %.0f mA\n"
        "ports:  A1=%d A2=%d C2=%d OTG=%d STPD_EN=%d  INA[%.0f %.0f]\n"
        "lab:    EN=%d load=%d  %.0f mA @ %.0f mV (ILIM %.0f mA)\n"
        "pins:   PD_IRQ=%d STPD_INT=%d CYPD_INT=%d CHRG_OK=%d BCKL=%d\n"
        "-----------------------------------------\n",
        sim_fsm_name(),
        board.soc, board.pack_mv, board.i_batt_ma, board.temp_c,
        fg.SoC, fg.voltage, fg.current,
        fg.flags.CHG, fg.flags.DSG, fg.flags.BATHI, fg.flags.BATLOW,
        fg.flags.OTC || fg.flags.OTD,
        c1.isPlugged, c1.isSink, c1.isNegotiationDone, c1.voltage, c1.maxCurrent,
        c2.isPlugged, c2.isNegotiationDone, c2.voltage, c2.maxCurrent,
        get_USBA1_Status(), get_USBA2_Status(), get_USBC2_Status(),
        get_OTG_Status(), get_STPD01_Enabled(),
        ina.current_channel1, ina.current_channel2,
        sim_gpio_get(0, 11), board.lab_load_attached,
        board.lab_ma, stpd_vout_mv(), stpd_ilim_ma(),
        sim_gpio_get(1, 14), sim_gpio_get(2, 12), sim_gpio_get(2, 3),
        sim_gpio_get(1, 13), sim_gpio_get(1, 8));

    fprintf(stderr, "stats:  energy_out %lu mWh  uptime %lu s  P %ld mW\n",
            (unsigned long)sys_stats.energy_out_mWh,
            (unsigned long)sys_stats.uptime_s, (long)telemetry.power_mW);

    /* what the PORTS page / lab bench page read out of telemetry */
    static const char *pname[5] = { "A1", "A2", "C1", "C2", "LAB" };
    for (int i = 0; i < 5; i++) {
        int nz = 0;
        for (int k = 0; k < TELEMETRY_HISTORY_SIZE; k++)
            if (port_stats[i].history[k]) nz++;
        fprintf(stderr,
                "portmon: %-3s active=%d  %5u mV  %5d mA  hist[idx=%2u nonzero=%2d]\n",
                pname[i], port_stats[i].active,
                port_stats[i].voltage_mv, port_stats[i].current_mA,
                port_stats[i].idx, nz);
    }
    fprintf(stderr, "-----------------------------------------\n");
    sim_unlock();
}

/* one scenario key, shared by SDL and the headless script */
static int handle_key(char k)
{
    switch (k) {
    case 'c': set_charger(!board.charger_attached); break;
    case 'd':   /* sink device on C1: powerbank sources (OTG path) */
        if (board.charger_attached) {
            sim_log("[EMU ] unplug the charger first ('c')");
            break;
        }
        board.c1_device_attached = !board.c1_device_attached;
        tps_plug_device(board.c1_device_attached);
        break;
    case 'v': board.c2_sink_attached = !board.c2_sink_attached;
              cypd_plug_sink(board.c2_sink_attached); break;
    case '1': board.a1_load_attached = !board.a1_load_attached;
              sim_log("[LOAD] A1 load %s", board.a1_load_attached ? "on" : "off");
              break;
    case '2': board.a2_load_attached = !board.a2_load_attached;
              sim_log("[LOAD] A2 load %s", board.a2_load_attached ? "on" : "off");
              break;
    case 'l': board.lab_load_attached = !board.lab_load_attached;
              sim_log("[LOAD] lab load %s (%.1f ohm)",
                      board.lab_load_attached ? "on" : "off", cfg.lab_load_ohm);
              break;
    case 'o': tog_a1_oc = !tog_a1_oc; ina_inject_alert(1, tog_a1_oc); break;
    case 't': tog_ot = !tog_ot;         bq34_inject_ot(tog_ot);       break;
    case 'h': tog_bathi = !tog_bathi;   bq34_inject_bathi(tog_bathi); break;
    case 'u': tog_batlow = !tog_batlow; bq34_inject_batlow(tog_batlow); break;
    case 'f': tog_cf = !tog_cf;         bq34_inject_cf(tog_cf);       break;
    case 'b': tog_gg_bus = !tog_gg_bus; bq34_inject_bus_fail(tog_gg_bus); break;
    case 'x': cypd_inject(0x83); break;                 /* OVP        */
    case 'z': cypd_inject(0x82); break;                 /* OCP        */
    case 'r': cypd_inject(0x8F); break;                 /* hard reset */
    case 'g': tog_stpd = !tog_stpd;
              stpd_inject_fault(tog_stpd ? (1u << 2) : 0); break;
    case 'k': sim_gpio_drive_input(1, 13, !sim_gpio_get(1, 13)); break;
    case 'w': sim_wake_signal(); break;
    case 'i': print_status(); break;
    case 's': panel_dump_ppm("screen.ppm"); break;
    case '+': sim_encoder_step(+1); break;
    case '-': sim_encoder_step(-1); break;
    case 'p': sim_gpio_drive_input(2, 0, 0); break;     /* btn down */
    case 'P': sim_gpio_drive_input(2, 0, 1); break;     /* btn up   */
    case 'q':
        /* shutdown: push the long-press event straight in, instead of
         * scripting a 3 s hold + release through 'p'/'P'. Real firmware
         * only wires this transition from IDLE/SLEEP (fsm.c transition
         * table) — same gate main.c's actual hold-timer is subject to, so
         * this is a shortcut to the event, not a bypass of the FSM rule. */
        sim_log("[EMU ] shutdown key: pushing EVT_BUTTON_LONG");
        event_push(EVT_BUTTON_LONG);
        break;
    default: return 0;
    }
    return 1;
}

/* ---------------- headless mode ---------------- */

typedef struct { uint32_t at_ms; char key; } ScriptEv;

/* --rec <ms>: dump a numbered frame to frames/ every <ms>, in both the
 * headless and the SDL loop. record.sh assembles the dumps into a gif. */
static uint32_t g_rec_ms;

static void rec_tick(uint32_t now)
{
    static uint32_t last_rec;
    static int frame;
    if (!g_rec_ms || now - last_rec < g_rec_ms)
        return;
    char name[64];
    snprintf(name, sizeof name, "frames/f%04d.ppm", frame++);
    panel_dump_ppm(name);
    last_rec = now;
}

static int run_headless(uint32_t run_ms, ScriptEv *ev, int nev)
{
    uint32_t last = sim_now_ms();
    int next = 0;
    while (sim_now_ms() < run_ms) {
        usleep(10000);
        uint32_t now = sim_now_ms();
        rec_tick(now);
        while (next < nev && ev[next].at_ms <= now) {
            sim_log("[EMU ] script key '%c'", ev[next].key);
            if (ev[next].key == ' ') {          /* short press */
                sim_gpio_drive_input(2, 0, 0);
                usleep(80000);
                sim_gpio_drive_input(2, 0, 1);
            } else {
                handle_key(ev[next].key);
            }
            next++;
        }
        sim_lock();
        board_tick(now - last);
        sim_unlock();
        last = now;
    }
    panel_dump_ppm("screen.ppm");
    print_status();
    return 0;
}

/* ---------------- SDL mode ---------------- */

#ifndef EMU_NO_SDL
#include <SDL2/SDL.h>

#define SCALE 2

static int run_sdl(void)
{
    static uint16_t fb[PANEL_W * PANEL_H];

    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window *win = SDL_CreateWindow("Ducker-Charger PCB emulator",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        PANEL_W * SCALE, PANEL_H * SCALE, 0);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565,
        SDL_TEXTUREACCESS_STREAMING, PANEL_W, PANEL_H);

    uint32_t last = sim_now_ms();
    int running = 1;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                SDL_Keycode k = e.key.keysym.sym;
                if      (k == SDLK_ESCAPE) running = 0;
                else if (k == SDLK_UP || k == SDLK_RIGHT) sim_encoder_step(+1);
                else if (k == SDLK_DOWN || k == SDLK_LEFT) sim_encoder_step(-1);
                else if (k == SDLK_SPACE) sim_gpio_drive_input(2, 0, 0);
                else if (k >= 0x20 && k < 0x80) handle_key((char)k);
            }
            else if (e.type == SDL_KEYUP && e.key.keysym.sym == SDLK_SPACE)
                sim_gpio_drive_input(2, 0, 1);
            else if (e.type == SDL_MOUSEWHEEL)
                sim_encoder_step(e.wheel.y > 0 ? +1 : -1);
        }

        uint32_t now = sim_now_ms();
        sim_lock();
        board_tick(now - last);
        panel_render(fb);
        sim_unlock();
        last = now;
        rec_tick(now);

        SDL_UpdateTexture(tex, NULL, fb, PANEL_W * 2);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
    }
    SDL_Quit();
    return 0;
}
#endif

/* ---------------- entry ---------------- */

int main(int argc, char **argv)
{
    uint32_t headless_ms = 0;
    ScriptEv script[64];
    int nscript = 0;
    const char *ini = "emu_config.ini";

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--headless") && i + 1 < argc)
            headless_ms = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--at") && i + 1 < argc && nscript < 64) {
            unsigned ms; char k;
            if (sscanf(argv[++i], "%u:%c", &ms, &k) == 2)
                script[nscript++] = (ScriptEv){ ms, k };
        }
        else if (!strcmp(argv[i], "--config") && i + 1 < argc)
            ini = argv[++i];
        else if (!strcmp(argv[i], "--rec") && i + 1 < argc)
            g_rec_ms = (uint32_t)atoi(argv[++i]);
    }

    EmuConfig_Load(ini);
    board_init();

    /* populate the PCB */
    bq34_attach();
    tps_attach();
    cypd_attach();
    stpd_attach();
    ina_attach();
    panel_attach();

    pthread_t th;
    pthread_create(&th, NULL, fw_thread, NULL);

    if (headless_ms)
        return run_headless(headless_ms, script, nscript);
#ifndef EMU_NO_SDL
    return run_sdl();
#else
    return run_headless(60000, NULL, 0);
#endif
}
