/* interactive UI simulator (SDL2).
 * arrows / wheel = encoder · space = press · hold space 1 s = SETTINGS
 * double space = SETTINGS shortcut · ESC = leave cave */
#include <SDL2/SDL.h>
#include <math.h>
#include "ui/ui_state.h"
#include "app/telemetry.h"
#include "display/ili9341.h"
#include "sim_config.h"

extern uint16_t sim_fb[];
void sim_advance(uint32_t ms);
void sim_encoder_rotate(signed char d);
void sim_encoder_press(void);
void sim_encoder_hold(uint8_t h);
void sim_telemetry_fill(void);
void sim_ports_fill(void);
void sim_stats_fill(void);
void sim_ports_push(void);
void sim_stats_tick(uint32_t ms);

#define SCALE 2

static void push_sample(int16_t s)
{
    telemetry.current_history[telemetry.history_idx] = s;
    telemetry.history_idx = (telemetry.history_idx + 1) % TELEMETRY_HISTORY_SIZE;
    telemetry.current_mA = s;
    telemetry.power_mW   = (int32_t)telemetry.voltage_mV * s / 1000;
    telemetry.is_charging = (s > 0);
}

int main(int argc, char **argv)
{
    SimConfig_Load(argc > 1 ? argv[1] : "sim_config.ini");
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window *win = SDL_CreateWindow("Ducker-Charger UI sim",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        ILI9341_WIDTH * SCALE, ILI9341_HEIGHT * SCALE, 0);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565,
        SDL_TEXTUREACCESS_STREAMING, ILI9341_WIDTH, ILI9341_HEIGHT);

    sim_telemetry_fill();
    sim_ports_fill();
    sim_stats_fill();
    UI_Init();

    uint32_t last = SDL_GetTicks(), last_sample = last;
    double phase = 0.0;
    int running = 1;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                switch (e.key.keysym.sym) {
                    case SDLK_ESCAPE: running = 0; break;
                    case SDLK_RIGHT:
                    case SDLK_UP:    sim_encoder_rotate(+1); break;
                    case SDLK_LEFT:
                    case SDLK_DOWN:  sim_encoder_rotate(-1); break;
                    case SDLK_SPACE: sim_encoder_press(); sim_encoder_hold(1); break;
                }
            }
            else if (e.type == SDL_KEYUP && e.key.keysym.sym == SDLK_SPACE)
                sim_encoder_hold(0);
            else if (e.type == SDL_MOUSEWHEEL)
                sim_encoder_rotate(e.wheel.y > 0 ? +1 : -1);
        }

        uint32_t now = SDL_GetTicks();
        sim_advance(now - last);
        sim_stats_tick(now - last);
        last = now;

        /* live fake current: sine + slow drift, one sample each 500 ms */
        if (now - last_sample >= 500) {
            last_sample = now;
            phase += 0.35;
            push_sample((int16_t)(simcfg.batt_ma + simcfg.batt_ripple * sin(phase)));
            sim_ports_push();
        }

        UI_Tick();

        SDL_UpdateTexture(tex, NULL, sim_fb, ILI9341_WIDTH * 2);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
    }

    SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
