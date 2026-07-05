/**
 * @file    ili9341.c
 * @brief   ILI9341 / ST7789 SPI display driver implementation.
 *
 * STATUS: init sequence extended for the generic 2" ST7789V modules
 * (MADCTL + inversion) and backlight control added on BCKL_CTRL/PB8.
 * To be verified on real hardware at bring-up.
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

    DISP_RST_LOW();
    HAL_Delay(10);
    DISP_RST_HIGH();
    HAL_Delay(120);

    send_cmd(0x01);   /* SW reset */
    HAL_Delay(150);
    send_cmd(0x11);   /* Sleep Out */
    HAL_Delay(120);

    send_cmd(0x3A);   /* pixel format: 16bpp RGB565 */
    send_data(0x55);

    send_cmd(0x36);   /* MADCTL: portrait, RGB */
    send_data(0x00);

    /* Display inversion ON: the generic 2" ST7789V modules need it,
     * otherwise every colour comes out negated. If bring-up shows
     * inverted colours on an actual ILI9341 panel, drop this command. */
    send_cmd(0x21);

    send_cmd(0x29);   /* Display On */
    HAL_Delay(25);
    ILI9341_SetBrightness(100u);
}

void ILI9341_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    send_cmd(0x2A);  /* CASET */
    send_data(x0 >> 8);  send_data(x0 & 0xFF);
    send_data(x1 >> 8);  send_data(x1 & 0xFF);
    send_cmd(0x2B);  /* RASET */
    send_data(y0 >> 8);  send_data(y0 & 0xFF);
    send_data(y1 >> 8);  send_data(y1 & 0xFF);
    send_cmd(0x2C);  /* RAMWR */
}

void ILI9341_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
    ILI9341_SetWindow(x, y, x, y);
    send_data(color >> 8);   /* RGB565, big-endian */
    send_data(color & 0xFF);
}

void ILI9341_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (w == 0 || h == 0) return;

    ILI9341_SetWindow(x, y, x + w - 1, y + h - 1);

    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;

    DISP_CS_LOW();
    DISP_DC_HIGH();

    uint32_t total_pixels = (uint32_t)w * h;
    for (uint32_t i = 0; i < total_pixels; i++)
    {
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
    /* The panel wants the high byte first; a straight uint16_t* cast on
     * this little-endian MCU would send the bytes swapped. Swap through
     * a small line buffer, chunked. */
    uint8_t chunk[64];

    DISP_CS_LOW();
    DISP_DC_HIGH();

    while (len > 0u) {
        uint32_t n = (len > 32u) ? 32u : len;
        for (uint32_t i = 0; i < n; i++) {
            chunk[2u * i]      = (uint8_t)(data[i] >> 8);
            chunk[2u * i + 1u] = (uint8_t)(data[i] & 0xFFu);
        }
        HAL_SPI_Transmit(_hspi, chunk, (uint16_t)(n * 2u), 100u);
        data += n;
        len  -= n;
    }

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

/* =========================================================
 * BACKLIGHT (BCKL_CTRL / PB8)
 *
 * The backlight FET is selected by jumper JP402: in the PMOS position
 * the pin is ACTIVE LOW (low = backlight on), in the NMOS position it
 * is active high. Set DISP_BCKL_ACTIVE_LOW accordingly at bring-up.
 * Current implementation is plain on/off; true dimming needs PWM on
 * PB8 (TIM10_CH1) — TODO once the UI brightness page is exercised on
 * hardware.
 * ========================================================= */

#define DISP_BCKL_ACTIVE_LOW  (1u)   /* JP402 in PMOS position */

static uint8_t _brightness = 100u;

void ILI9341_SetBrightness(uint8_t pct)
{
    if (pct > 100u) pct = 100u;
    _brightness = pct;

    GPIO_PinState on  = DISP_BCKL_ACTIVE_LOW ? GPIO_PIN_RESET : GPIO_PIN_SET;
    GPIO_PinState off = DISP_BCKL_ACTIVE_LOW ? GPIO_PIN_SET   : GPIO_PIN_RESET;

    HAL_GPIO_WritePin(BCKL_CTRL_GPIO_Port, BCKL_CTRL_Pin,
                      (pct > 0u) ? on : off);
}

uint8_t ILI9341_GetBrightness(void)
{
    return _brightness;
}
