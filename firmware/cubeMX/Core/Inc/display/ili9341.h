/**
 * @file    ili9341.h
 * @brief   Low-level SPI driver for TFT displays with ILI9341 or ST7789 controller.
 *          Driver SPI a basso livello per display TFT con controller ILI9341 o ST7789.
 *
 * This file handles the low-level communication with the display chip.
 * It only sends bytes over SPI: it knows nothing about graphics, text or UI.
 *
 * Questo file gestisce la comunicazione a basso livello con il chip del display.
 * Si occupa solo di mandare byte via SPI: non sa nulla di grafica, testo o UI.
 *
 * HARDWARE CONNECTIONS / CONNESSIONI HARDWARE (from main.h / da main.h):
 *   SPI1 SCK  --> PA5
 *   SPI1 MOSI --> PA7
 *   CS        --> PB0  (DISP_CS_Pin)
 *   DC        --> PB1  (DISP_DC_Pin)   HIGH = data, LOW = command / HIGH = dato, LOW = comando
 *   RST       --> PB2  (DISP_RST_Pin)  Active LOW reset / Reset attivo basso
 *
 * HOW THE DC PIN WORKS / COME FUNZIONA IL PIN DC (Data/Command):
 *   The ILI9341 controller distinguishes two types of bytes:
 *   Il controller ILI9341 distingue due tipi di byte:
 *   - COMMAND: tells the chip what to do (e.g. "set the write window").
 *              Dice al chip cosa fare (es. "imposta la finestra di scrittura").
 *   - DATA:    the actual content (e.g. the pixels to draw).
 *              Il contenuto effettivo (es. i pixel da disegnare).
 *   The DC pin indicates which type we are sending.
 *   Il pin DC indica quale dei due stiamo mandando.
 *
 * DEFAULT RESOLUTION / RISOLUZIONE DEFAULT: 240 x 320 pixels (portrait mode)
 */

#ifndef __ILI9341_H
#define __ILI9341_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

/* =========================================================
 * DISPLAY DIMENSIONS / DIMENSIONI DISPLAY
 * ========================================================= */

#define ILI9341_WIDTH   240  /**< Width in pixels  / Larghezza in pixel */
#define ILI9341_HEIGHT  320  /**< Height in pixels / Altezza in pixel   */

/* =========================================================
 * PREDEFINED COLORS in RGB565 format / COLORI PREDEFINITI in formato RGB565
 *
 * RGB565: 5 bits red | 6 bits green | 5 bits blue
 *         5 bit rosso | 6 bit verde | 5 bit blu
 * Each pixel takes 2 bytes. This is the native format of the ILI9341.
 * Ogni pixel occupa 2 byte. E' il formato nativo dell'ILI9341.
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

/* UI-specific colours / Colori specifici per la nostra UI */
#define COLOR_BG      0x0841  /**< Near-black background / Quasi nero per lo sfondo */
#define COLOR_ACCENT  0x04FF  /**< Teal/cyan for active elements / Verde-acqua per elementi attivi */
#define COLOR_WARN    0xFD00  /**< Orange for warnings / Arancione per avvisi */
#define COLOR_DANGER  0xF800  /**< Red for errors / Rosso per errori */

/**
 * Sentinel value used as bg_color in GFX_DrawChar / GFX_DrawString to
 * signal "transparent background" (do not draw background pixels).
 * Note: transparency is not yet implemented in this basic version;
 *       it currently behaves like COLOR_BG.
 *
 * Valore sentinella usato come bg_color in GFX_DrawChar/GFX_DrawString per
 * indicare "sfondo trasparente" (non disegnare i pixel di sfondo).
 * Nota: la trasparenza non e' ancora implementata; funziona come COLOR_BG.
 */
#define COLOR_TRANSPARENT_MAGIC  COLOR_BG

/* =========================================================
 * PUBLIC API / API PUBBLICA
 * ========================================================= */

/**
 * @brief  Initialise the display: hardware reset + init command sequence.
 *         Inizializza il display: reset hardware + sequenza di comandi di init.
 * @note   Call once in main() after MX_SPI1_Init(). After this call the
 *         display is ready and shows a black screen.
 *         Chiamare una volta in main() dopo MX_SPI1_Init(). Al termine il
 *         display e' pronto e mostra schermo nero.
 * @param  hspi_ptr  Pointer to the SPI handle (hspi1) /
 *                   Puntatore all'handle SPI (hspi1)
 */
void ILI9341_Init(SPI_HandleTypeDef *hspi_ptr);

/**
 * @brief  Fill the entire screen with a solid colour.
 *         Riempie l'intero schermo con un colore solido.
 * @note   Slow operation (~40 ms without DMA). Use only for a full clear.
 *         Operazione lenta (~40ms senza DMA). Usare solo per un clear completo.
 * @param  color  RGB565 colour / Colore in formato RGB565
 */
void ILI9341_FillScreen(uint16_t color);

/**
 * @brief  Fill a rectangle with a solid colour.
 *         Riempie un rettangolo con un colore solido.
 * @param  x, y   Top-left corner (origin = top-left of screen) /
 *                Angolo in alto a sinistra (origine = angolo in alto a sinistra)
 * @param  w, h   Width and height in pixels / Larghezza e altezza in pixel
 * @param  color  RGB565 colour / Colore in formato RGB565
 */
void ILI9341_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

/**
 * @brief  Draw a single pixel.
 *         Disegna un singolo pixel.
 * @param  x, y   Pixel coordinates / Coordinate del pixel
 * @param  color  RGB565 colour / Colore in formato RGB565
 */
void ILI9341_DrawPixel(uint16_t x, uint16_t y, uint16_t color);

/**
 * @brief  Set the write window: the rectangle into which subsequent pixel
 *         data will be written sequentially.
 *         Imposta la finestra di scrittura: il rettangolo in cui i prossimi
 *         dati pixel verranno scritti in sequenza.
 * @note   Low-level function used internally by FillRect etc. Exposed as
 *         public to allow direct pixel-buffer writes from outside
 *         (e.g. anti-aliased font rendering).
 *         Funzione di basso livello usata internamente da FillRect ecc.
 *         Esposta come pubblica per permettere scrittura di buffer pixel
 *         dall'esterno (es. rendering font con anti-alias).
 * @param  x0, y0  Top-left corner / Angolo in alto a sinistra
 * @param  x1, y1  Bottom-right corner (inclusive) / Angolo in basso a destra (incluso)
 */
void ILI9341_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

/**
 * @brief  Send a ready-made array of RGB565 pixels directly to the screen.
 *         Manda un array di pixel RGB565 gia' pronti direttamente allo schermo.
 * @note   You must have set the window with ILI9341_SetWindow() beforehand.
 *         Pixels are written left-to-right, top-to-bottom.
 *         Bisogna aver impostato la finestra con ILI9341_SetWindow() prima.
 *         I pixel vengono scritti da sinistra a destra, dall'alto al basso.
 * @param  data  Pointer to an array of uint16_t (RGB565 colours) /
 *               Puntatore all'array di uint16_t (colori RGB565)
 * @param  len   Number of PIXELS (not bytes!) / Numero di PIXEL (non byte!)
 */
void ILI9341_SendPixels(uint16_t *data, uint32_t len);

/**
 * @brief  Convert RGB components (0–255 each) to RGB565 format.
 *         Converte componenti RGB (0–255 ciascuno) in formato RGB565.
 * @note   Utility function to create colours without manual bit-shifting.
 *         Funzione di utilita' per creare colori senza calcoli manuali.
 * @retval The colour in 16-bit RGB565 format / Il colore nel formato RGB565 a 16 bit
 *
 * Example / Esempio:
 *   uint16_t orange = ILI9341_RGB(255, 165, 0);
 */
uint16_t ILI9341_RGB(uint8_t r, uint8_t g, uint8_t b);

#ifdef __cplusplus
}
#endif

#endif /* __ILI9341_H */
