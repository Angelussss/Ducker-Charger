/**
 * @file    dev_ili9341.c
 * @brief   ILI9341/ST7789 panel model driven by the REAL firmware driver.
 *
 * Unlike interface-tester (which replaces the ILI9341_* API), this decodes
 * the actual SPI command/data stream produced by display/ili9341.c:
 * CS/DC/RST sampled from the GPIO table (PB0/PB1/PB2), commands CASET /
 * RASET / RAMWR / MADCTL / INVON / SLPOUT / DISPON handled with window
 * addressing and auto-increment, pixels into a 240x320 RGB565 GRAM.
 * Backlight = PB8, active low (JP402 in PMOS position).
 */
#include "sim.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    uint16_t gram[PANEL_W * PANEL_H];
    uint8_t  cmd;               /* current command                */
    uint8_t  args[8];
    int      argn;
    uint16_t x0, x1, y0, y1;    /* address window                 */
    uint16_t cx, cy;            /* write cursor                   */
    int      pix_phase;         /* RAMWR byte assembler           */
    uint8_t  pix_hi;
    int      sleeping, disp_on, inverted;
} Panel;

static Panel pn;

static void window_apply(void)
{
    pn.cx = pn.x0; pn.cy = pn.y0;
}

static void pixel_write(uint16_t c)
{
    if (pn.cx < PANEL_W && pn.cy < PANEL_H)
        pn.gram[pn.cy * PANEL_W + pn.cx] = c;
    if (pn.cx >= pn.x1) {
        pn.cx = pn.x0;
        pn.cy = (pn.cy >= pn.y1) ? pn.y0 : (uint16_t)(pn.cy + 1);
    } else {
        pn.cx++;
    }
}

static void command(uint8_t c)
{
    pn.cmd = c;
    pn.argn = 0;
    pn.pix_phase = 0;
    switch (c) {
    case 0x01:   /* SWRESET */
        pn.sleeping = 1; pn.disp_on = 0; pn.inverted = 0;
        pn.x0 = 0; pn.x1 = PANEL_W - 1; pn.y0 = 0; pn.y1 = PANEL_H - 1;
        window_apply();
        break;
    case 0x11: pn.sleeping = 0;  break;   /* SLPOUT */
    case 0x20: pn.inverted = 0;  break;   /* INVOFF */
    case 0x21: pn.inverted = 1;  break;   /* INVON  */
    case 0x28: pn.disp_on = 0;   break;   /* DISPOFF*/
    case 0x29: pn.disp_on = 1;
        sim_log("[DISP] display ON (inv=%d)", pn.inverted);
        break;
    case 0x2C: window_apply();   break;   /* RAMWR  */
    default: break;
    }
}

static void data(uint8_t d)
{
    switch (pn.cmd) {
    case 0x2A:   /* CASET */
        if (pn.argn < 4) pn.args[pn.argn++] = d;
        if (pn.argn == 4) {
            pn.x0 = (uint16_t)((pn.args[0] << 8) | pn.args[1]);
            pn.x1 = (uint16_t)((pn.args[2] << 8) | pn.args[3]);
        }
        break;
    case 0x2B:   /* RASET */
        if (pn.argn < 4) pn.args[pn.argn++] = d;
        if (pn.argn == 4) {
            pn.y0 = (uint16_t)((pn.args[0] << 8) | pn.args[1]);
            pn.y1 = (uint16_t)((pn.args[2] << 8) | pn.args[3]);
        }
        break;
    case 0x2C:   /* RAMWR pixel stream, big-endian RGB565 */
        if (pn.pix_phase == 0) { pn.pix_hi = d; pn.pix_phase = 1; }
        else { pixel_write((uint16_t)((pn.pix_hi << 8) | d)); pn.pix_phase = 0; }
        break;
    default:
        break;   /* MADCTL / COLMOD args: orientation fixed at portrait */
    }
}

void panel_spi_bytes(const uint8_t *d, uint16_t n)
{
    /* CS PB0 (active low), DC PB1, RST PB2 (low = held in reset) */
    if (sim_gpio_get(1, 0) != 0) return;      /* not selected */
    if (sim_gpio_get(1, 2) == 0) return;      /* in reset     */
    int dc = sim_gpio_get(1, 1);
    for (uint16_t i = 0; i < n; i++) {
        if (dc) data(d[i]);
        else    command(d[i]);
    }
}

void panel_render(uint16_t *out)
{
    /* PB8 polarity per JP402 position (see finding #6 in the README) */
    int backlight_on = cfg.bckl_active_low ? (sim_gpio_get(1, 8) == 0)
                                           : (sim_gpio_get(1, 8) == 1);
    if (!pn.disp_on || pn.sleeping || !backlight_on) {
        memset(out, 0, sizeof(pn.gram));
        return;
    }
    if (pn.inverted) {
        for (int i = 0; i < PANEL_W * PANEL_H; i++)
            out[i] = (uint16_t)~pn.gram[i];
    } else {
        memcpy(out, pn.gram, sizeof(pn.gram));
    }
}

static void dump(const uint16_t *fb, const char *path);

void panel_dump_ppm(const char *path)
{
    static uint16_t fb[PANEL_W * PANEL_H];
    panel_render(fb);
    dump(fb, path);
    dump(pn.gram, "gram.ppm");   /* raw GRAM, ignores backlight/on-off */
}

static void dump(const uint16_t *fb, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", PANEL_W, PANEL_H);
    for (int i = 0; i < PANEL_W * PANEL_H; i++) {
        uint16_t c = fb[i];
        uint8_t rgb[3] = {
            (uint8_t)(((c >> 11) & 0x1F) << 3),
            (uint8_t)(((c >> 5)  & 0x3F) << 2),
            (uint8_t)((c & 0x1F) << 3),
        };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    sim_log("[DISP] snapshot -> %s", path);
}

void panel_attach(void)
{
    memset(&pn, 0, sizeof(pn));
    pn.sleeping = 1;
    pn.x1 = PANEL_W - 1;
    pn.y1 = PANEL_H - 1;
}
