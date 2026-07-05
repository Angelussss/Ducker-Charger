/* native framebuffer behind the ILI9341 API.
 * draws into 240x320 RGB565 buffer; can dump PPM snapshots. */
#include "display/ili9341.h"
#include <stdio.h>
#include <stdlib.h>

uint16_t sim_fb[ILI9341_WIDTH * ILI9341_HEIGHT];
static uint8_t _brightness = 100;

void ILI9341_SetBrightness(uint8_t pct)
{
    if (pct < 10) pct = 10;
    if (pct > 100) pct = 100;
    _brightness = pct;
}
uint8_t ILI9341_GetBrightness(void) { return _brightness; }

void ILI9341_Init(SPI_HandleTypeDef *hspi_ptr) { (void)hspi_ptr; }

void ILI9341_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{ (void)x0; (void)y0; (void)x1; (void)y1; }

void ILI9341_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= ILI9341_WIDTH || y >= ILI9341_HEIGHT) return;
    sim_fb[y * ILI9341_WIDTH + x] = color;
}

void ILI9341_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    for (uint16_t j = 0; j < h; j++)
        for (uint16_t i = 0; i < w; i++)
            ILI9341_DrawPixel(x + i, y + j, color);
}

void ILI9341_FillScreen(uint16_t color)
{ ILI9341_FillRect(0, 0, ILI9341_WIDTH, ILI9341_HEIGHT, color); }

void ILI9341_SendPixels(uint16_t *data, uint32_t len)
{ (void)data; (void)len; }

uint16_t ILI9341_RGB(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/* dump framebuffer as binary PPM (P6) */
void sim_dump_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", ILI9341_WIDTH, ILI9341_HEIGHT);
    for (int i = 0; i < ILI9341_WIDTH * ILI9341_HEIGHT; i++) {
        uint16_t c = sim_fb[i];
        uint8_t rgb[3] = {
            (uint8_t)((((c >> 11) & 0x1F) << 3) * _brightness / 100),
            (uint8_t)((((c >> 5)  & 0x3F) << 2) * _brightness / 100),
            (uint8_t)(((c & 0x1F) << 3) * _brightness / 100)
        };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
