/**
 * @file    ili9341.c
 * @brief   ILI9341 / ST7789 SPI display driver implementation.
 *
 * STATUS: functional stub – functions compile and produce correct
 * SPI output; full init sequence to be verified with real hardware.
 *
 * DISPLAY INIT SEQUENCE (to verify/extend with real hardware):
 *   1. RST low for 10 ms  --> hardware reset
 *   2. RST high, wait 120 ms --> post-reset delay
 *   3. Series of SPI commands to configure:
 *      - Pixel format (RGB565 = 0x55)
 *      - Display orientation (MADCTL register)
 *      - Gamma, timing, etc.
 *   4. Sleep Out (0x11) + wait 120 ms
 *   5. Display On (0x29)
 *
 * HOW TO SEND A COMMAND:
 *   CS low -> DC low -> SPI byte -> CS high
 *
 * HOW TO SEND DATA (pixels):
 *   CS low -> DC high -> SPI byte(s) -> CS high
 */

#include "display/ili9341.h"
#include "main.h"   /* DISP_CS_Pin, DISP_DC_Pin, DISP_RST_Pin, GPIOB */

/* SPI handle pointer saved during Init(). */
static SPI_HandleTypeDef *_hspi = NULL;

/* =========================================================
 * CONTROL PIN MACROS
 *
 * Using macros avoids repeating the HAL_GPIO_WritePin call every time
 * and makes the code easier to read.
 * ========================================================= */

#define DISP_CS_LOW()   HAL_GPIO_WritePin(DISP_CS_GPIO_Port,  DISP_CS_Pin,  GPIO_PIN_RESET)
#define DISP_CS_HIGH()  HAL_GPIO_WritePin(DISP_CS_GPIO_Port,  DISP_CS_Pin,  GPIO_PIN_SET)
#define DISP_DC_LOW()   HAL_GPIO_WritePin(DISP_DC_GPIO_Port,  DISP_DC_Pin,  GPIO_PIN_RESET)
#define DISP_DC_HIGH()  HAL_GPIO_WritePin(DISP_DC_GPIO_Port,  DISP_DC_Pin,  GPIO_PIN_SET)
#define DISP_RST_LOW()  HAL_GPIO_WritePin(DISP_RST_GPIO_Port, DISP_RST_Pin, GPIO_PIN_RESET)
#define DISP_RST_HIGH() HAL_GPIO_WritePin(DISP_RST_GPIO_Port, DISP_RST_Pin, GPIO_PIN_SET)

/* =========================================================
 * PRIVATE FUNCTIONS
 * ========================================================= */

/**
 * @brief  Send a single byte as a COMMAND (DC low).
 */
static void send_cmd(uint8_t cmd)
{
    DISP_CS_LOW();
    DISP_DC_LOW();   /* DC low = command */
    HAL_SPI_Transmit(_hspi, &cmd, 1, HAL_MAX_DELAY);
    DISP_CS_HIGH();
}

/**
 * @brief  Send a single byte as DATA (DC high).
 */
static void send_data(uint8_t data)
{
    DISP_CS_LOW();
    DISP_DC_HIGH();  /* DC high = data */
    HAL_SPI_Transmit(_hspi, &data, 1, HAL_MAX_DELAY);
    DISP_CS_HIGH();
}

/* =========================================================
 * PUBLIC FUNCTIONS
 * ========================================================= */

void ILI9341_Init(SPI_HandleTypeDef *hspi_ptr)
{
    _hspi = hspi_ptr;

    /* Hardware reset: pull RST low for 10 ms, then release. */
    DISP_RST_LOW();
    HAL_Delay(10);
    DISP_RST_HIGH();
    HAL_Delay(120);  /* Wait for the display to come out of reset. */

    send_cmd(0x01);   /* Software reset */
    HAL_Delay(150);

    send_cmd(0x11);   /* Sleep Out: exit low-power sleep mode. */
    HAL_Delay(120);

    /* Set pixel format to 16 bits per pixel (RGB565). */
    send_cmd(0x3A);
    send_data(0x55);  /* 0x55 = 16 bpp */

    send_cmd(0x29);   /* Display On */
    HAL_Delay(25);
}

void ILI9341_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    /* Column Address Set (0x2A): defines the horizontal pixel range. */
    send_cmd(0x2A);
    send_data(x0 >> 8);  send_data(x0 & 0xFF);  /* start column */
    send_data(x1 >> 8);  send_data(x1 & 0xFF);  /* end column   */

    /* Row Address Set (0x2B): defines the vertical pixel range. */
    send_cmd(0x2B);
    send_data(y0 >> 8);  send_data(y0 & 0xFF);  /* start row */
    send_data(y1 >> 8);  send_data(y1 & 0xFF);  /* end row   */

    /* Memory Write (0x2C): subsequent bytes are pixel data. */
    send_cmd(0x2C);
}

void ILI9341_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
    ILI9341_SetWindow(x, y, x, y);

    /* Send the 2-byte RGB565 colour, high byte first (big-endian). */
    send_data(color >> 8);
    send_data(color & 0xFF);
}

void ILI9341_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (w == 0 || h == 0) return;

    ILI9341_SetWindow(x, y, x + w - 1, y + h - 1);

    /* Pre-compute the two bytes of the colour (big-endian). */
    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;

    DISP_CS_LOW();
    DISP_DC_HIGH();  /* All subsequent bytes are pixel data. */

    uint32_t total_pixels = (uint32_t)w * h;
    for (uint32_t i = 0; i < total_pixels; i++)
    {
        /* Each pixel = 2 bytes: high byte then low byte. */
        HAL_SPI_Transmit(_hspi, &hi, 1, HAL_MAX_DELAY);
        HAL_SPI_Transmit(_hspi, &lo, 1, HAL_MAX_DELAY);
    }

    DISP_CS_HIGH();
}

void ILI9341_FillScreen(uint16_t color)
{
    ILI9341_FillRect(0, 0, ILI9341_WIDTH, ILI9341_HEIGHT, color);
}

void ILI9341_SendPixels(uint16_t *data, uint32_t len)
{
    DISP_CS_LOW();
    DISP_DC_HIGH();

    /* Send the entire buffer in a single SPI transfer.
     * len pixels * 2 bytes/pixel = len*2 bytes total.
     */
    HAL_SPI_Transmit(_hspi, (uint8_t *)data, len * 2, HAL_MAX_DELAY);

    DISP_CS_HIGH();
}

uint16_t ILI9341_RGB(uint8_t r, uint8_t g, uint8_t b)
{
    /* RGB888 -> RGB565 conversion:
     *
     *   R: take the 5 most significant bits and place them at bits 11–15.
     *   G: take the 6 most significant bits and place them at bits 5–10.
     *   B: take the 5 most significant bits and place them at bits 0–4.
     */
    return (uint16_t)(((r & 0xF8) << 8) |
                      ((g & 0xFC) << 3) |
                      (b >> 3));
}