/**
 * @file    ili9341.h
 * @brief   Low-level SPI driver for TFT displays with ILI9341 or ST7789 controller.
 *
 * This file handles the low-level communication with the display chip.
 * It only sends bytes over SPI: it knows nothing about graphics, text or UI.
 *
 * HARDWARE CONNECTIONS (from main.h):
 *   SPI1 SCK  --> PA5
 *   SPI1 MOSI --> PA7
 *   CS        --> PB0  (DISP_CS_Pin)
 *   DC        --> PB1  (DISP_DC_Pin)
 *   RST       --> PB2  (DISP_RST_Pin)
 *
 * HOW THE DC PIN WORKS (Data/Command):
 *   The ILI9341 controller distinguishes two types of bytes:
 *   - COMMAND: tells the chip what to do (e.g. "set the write window")
 *   - DATA:    the actual content (e.g. the pixels to draw)
 *
 *   Il pin DC distingue tra comandi e dati:
 *   LOW  = command
 *   HIGH = data
 *
 * DEFAULT RESOLUTION: 240 x 320 pixels (portrait mode)
 */

#ifndef __ILI9341_H
#define __ILI9341_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

/* =========================================================
 * DISPLAY DIMENSIONS
 * ========================================================= */

#define ILI9341_WIDTH   240  /**< Width in pixels */
#define ILI9341_HEIGHT  320  /**< Height in pixels */

/* =========================================================
 * PREDEFINED COLORS in RGB565 format
 *
 * RGB565: 5 bits red | 6 bits green | 5 bits blue
 *
 * Each pixel takes 2 bytes. This is the native format of the ILI9341.
 * ========================================================= */

#define COLOR_BLACK     0x0000
#define COLOR_WHITE     0xFFFF
#define COLOR_RED       0xF800
#define COLOR_GREEN     0x07E0
#define COLOR_BLUE      0x001F
#define COLOR_YELLOW    0xFFE0
#define COLOR_CYAN      0x07FF
#define COLOR_MAGENTA   0xF81F
#define COLOR_ORANGE    0xFD20
#define COLOR_GRAY      0x8410
#define COLOR_DARKGRAY  0x4208
#define COLOR_LIGHTGRAY 0xC618

/* UI-specific colours */
#define COLOR_BG      0x0841  /**< Near-black background */
#define COLOR_ACCENT  0x04FF  /**< Teal/cyan for active elements */
#define COLOR_WARN    0xFD00  /**< Orange for warnings */
#define COLOR_DANGER  0xF800  /**< Red for errors */

/**
 * Sentinel value used as bg_color in GFX_DrawChar / GFX_DrawString to
 * signal "transparent background" (do not draw background pixels).
 *
 * Note: transparency is not yet implemented in this basic version;
 * it currently behaves like COLOR_BG.
 */
#define COLOR_TRANSPARENT_MAGIC  COLOR_BG

/* =========================================================
 * PUBLIC API
 * ========================================================= */

/**
 * @brief  Initialise the display: hardware reset + init command sequence.
 * @note   Call once in main() after MX_SPI1_Init(). After this call the
 *         display is ready and shows a black screen.
 * @param  hspi_ptr Pointer to the SPI handle (hspi1)
 */
void ILI9341_Init(SPI_HandleTypeDef *hspi_ptr);

/**
 * @brief  Fill the entire screen with a solid colour.
 * @note   Slow operation (~40 ms without DMA). Use only for a full clear.
 * @param  color RGB565 colour
 */
void ILI9341_FillScreen(uint16_t color);

/**
 * @brief  Fill a rectangle with a solid colour.
 * @param  x, y   Top-left corner (origin = top-left of screen)
 * @param  w, h   Width and height in pixels
 * @param  color  RGB565 colour
 */
void ILI9341_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

/**
 * @brief  Draw a single pixel.
 * @param  x, y   Pixel coordinates
 * @param  color  RGB565 colour
 */
void ILI9341_DrawPixel(uint16_t x, uint16_t y, uint16_t color);

/**
 * @brief  Set the write window: the rectangle into which subsequent pixel
 *         data will be written sequentially.
 *
 *         Concetto importante: dopo aver impostato la window, tutti i pixel
 *         inviati verranno scritti automaticamente all'interno di quell'area.
 *
 * @note   Low-level function used internally by FillRect etc. Exposed as
 *         public to allow direct pixel-buffer writes from outside
 *         (e.g. anti-aliased font rendering).
 *
 * @param  x0, y0 Top-left corner
 * @param  x1, y1 Bottom-right corner (inclusive)
 */
void ILI9341_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

/**
 * @brief  Send a ready-made array of RGB565 pixels directly to the screen.
 * @note   You must have set the window with ILI9341_SetWindow() beforehand.
 *         Pixels are written left-to-right, top-to-bottom.
 *
 * @param  data Pointer to an array of uint16_t (RGB565 colours)
 * @param  len  Number of PIXELS (not bytes)
 */
void ILI9341_SendPixels(uint16_t *data, uint32_t len);

/**
 * @brief  Convert RGB components (0-255 each) to RGB565 format.
 * @note   Utility function to create colours without manual bit-shifting.
 * @retval The colour in 16-bit RGB565 format
 *
 * Example:
 *   uint16_t orange = ILI9341_RGB(255, 165, 0);
 */
uint16_t ILI9341_RGB(uint8_t r, uint8_t g, uint8_t b);

#ifdef __cplusplus
}
#endif

#endif /* __ILI9341_H */