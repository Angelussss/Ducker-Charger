/**
 * @file    gfx.h
 * @brief   Graphics primitives: text, lines, circles, rectangles.
 *          Primitive grafiche: testo, linee, cerchi, rettangoli.
 *
 * This module sits above the ILI9341 driver and below the widget layer.
 * It knows how to draw geometric shapes and text, but has no concept of
 * "battery bar" or "graph" — that is the job of the widgets.
 *
 * Questo modulo sta sopra il driver ILI9341 e sotto i widget.
 * Sa come disegnare forme geometriche e testo, ma non conosce il concetto
 * di "barra della batteria" o "grafico" — quello e' compito dei widget.
 *
 * BITMAP FONTS / FONT BITMAP:
 *   Fixed-size bitmap fonts are included. A bitmap font is a C array where
 *   each character is represented as a grid of bits:
 *   1 = pixel on (foreground colour), 0 = pixel off (background colour).
 *
 *   Sono inclusi font bitmap a dimensione fissa. Un font bitmap e' un array C
 *   dove ogni carattere e' rappresentato come una griglia di bit:
 *   1 = pixel acceso (colore foreground), 0 = pixel spento (background).
 *
 * COORDINATE SYSTEM / SISTEMA DI COORDINATE:
 *   Origin (0,0) = TOP-LEFT corner of the screen.
 *   Origine (0,0) = angolo in ALTO A SINISTRA dello schermo.
 *   X grows rightward, Y grows downward.
 *   X cresce verso destra, Y cresce verso il basso.
 *
 *        (0,0) ──────────────> X (239)
 *          |
 *          |
 *          v
 *         Y (319)
 */

#ifndef __GFX_H
#define __GFX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include "display/ili9341.h"

/* =========================================================
 * FONT DIMENSIONS / DIMENSIONI DEI FONT
 * ========================================================= */

/** Small font: 6x8 px per character. For secondary info. / Font piccolo: 6x8 px per carattere. Per info secondarie. */
#define GFX_FONT_SMALL_W   6
#define GFX_FONT_SMALL_H   8

/** Medium font: 8x16 px per character. For normal text. / Font medio: 8x16 px per carattere. Per testo normale. */
#define GFX_FONT_MEDIUM_W  8
#define GFX_FONT_MEDIUM_H  16

/** Large font: 16x24 px per character. For important numbers (SoC, voltage). / Font grande: 16x24 px per carattere. Per numeri importanti. */
#define GFX_FONT_LARGE_W   16
#define GFX_FONT_LARGE_H   24

/* =========================================================
 * FONT TYPE DEFINITION / DEFINIZIONE DEL TIPO FONT
 * ========================================================= */

/**
 * @brief Descriptor for a bitmap font.
 *        Descrittore di un font bitmap.
 *
 * The `data` field points to an array where each ASCII character
 * (starting from ' ' = 0x20) is stored as a sequence of bytes,
 * one per row, representing the on/off pixels of that row.
 *
 * Il campo `data` punta a un array dove ogni carattere ASCII
 * (a partire da ' ' = 0x20) e' memorizzato come una sequenza di byte,
 * uno per riga, che rappresentano i pixel accesi/spenti di quella riga.
 */
typedef struct {
    const uint8_t *data;  /**< Pointer to the bitmap data / Puntatore ai dati bitmap */
    uint8_t char_w;       /**< Character width in pixels  / Larghezza del carattere in pixel */
    uint8_t char_h;       /**< Character height in pixels / Altezza del carattere in pixel  */
} GFX_Font_t;

/* =========================================================
 * AVAILABLE FONTS (defined in gfx.c) / FONT DISPONIBILI (definiti in gfx.c)
 * ========================================================= */

extern const GFX_Font_t GFX_FontSmall;   /**< 6x8  – secondary text / testo secondario */
extern const GFX_Font_t GFX_FontMedium;  /**< 8x16 – normal text    / testo normale    */
extern const GFX_Font_t GFX_FontLarge;   /**< 16x24 – large numbers / numeri grandi    */

/* =========================================================
 * PUBLIC API – TEXT / API PUBBLICA – TESTO
 * ========================================================= */

/**
 * @brief  Draw a single ASCII character.
 *         Disegna un singolo carattere ASCII.
 * @param  x, y      Top-left corner of the character / Angolo in alto a sinistra del carattere
 * @param  c         Character to draw (ASCII 32–126) / Carattere da disegnare (ASCII 32–126)
 * @param  font      Pointer to the font to use / Puntatore al font da usare
 * @param  fg_color  Foreground (text) colour / Colore del testo (foreground)
 * @param  bg_color  Background colour drawn behind each glyph /
 *                   Colore dello sfondo disegnato dietro ogni glifo
 */
void GFX_DrawChar(uint16_t x, uint16_t y, char c,
                  const GFX_Font_t *font, uint16_t fg_color, uint16_t bg_color);

/**
 * @brief  Draw a null-terminated text string.
 *         Disegna una stringa di testo terminata da '\0'.
 * @param  x, y      Position of the first character (top-left) /
 *                   Posizione del primo carattere (angolo alto-sinistra)
 * @param  str       Null-terminated string / Stringa terminata da '\0'
 * @param  font      Font to use / Font da usare
 * @param  fg_color  Text colour / Colore testo
 * @param  bg_color  Background colour / Colore sfondo
 *
 * Example / Esempio:
 *   GFX_DrawString(10, 20, "Charge: 85%", &GFX_FontMedium, COLOR_WHITE, COLOR_BG);
 */
void GFX_DrawString(uint16_t x, uint16_t y, const char *str,
                    const GFX_Font_t *font, uint16_t fg_color, uint16_t bg_color);

/**
 * @brief  Draw an integer value formatted as a string.
 *         Disegna un valore intero formattato come stringa.
 * @note   Convenient alternative to calling snprintf() + GFX_DrawString() every time.
 *         Alternativa comoda a chiamare snprintf() + GFX_DrawString() ogni volta.
 * @param  x, y      Position / Posizione
 * @param  value     Integer value to display / Valore intero da visualizzare
 * @param  font      Font to use / Font da usare
 * @param  fg_color  Text colour / Colore testo
 * @param  bg_color  Background colour / Colore sfondo
 */
void GFX_DrawInt(uint16_t x, uint16_t y, int32_t value,
                 const GFX_Font_t *font, uint16_t fg_color, uint16_t bg_color);

/* =========================================================
 * PUBLIC API – SHAPES / API PUBBLICA – FORME GEOMETRICHE
 * ========================================================= */

/**
 * @brief  Draw a line between two points (Bresenham's algorithm).
 *         Disegna una linea tra due punti (algoritmo di Bresenham).
 * @param  x0, y0  Start point / Punto di partenza
 * @param  x1, y1  End point   / Punto di arrivo
 * @param  color   Line colour / Colore della linea
 */
void GFX_DrawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);

/**
 * @brief  Draw the outline of a rectangle (not filled).
 *         Disegna il bordo di un rettangolo (non riempito).
 * @param  x, y   Top-left corner / Angolo in alto a sinistra
 * @param  w, h   Width and height / Larghezza e altezza
 * @param  color  Border colour   / Colore del bordo
 */
void GFX_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

/**
 * @brief  Draw a rounded-corner rectangle outline.
 *         Disegna il bordo di un rettangolo con angoli arrotondati.
 * @param  x, y   Top-left corner         / Angolo in alto a sinistra
 * @param  w, h   Width and height        / Larghezza e altezza
 * @param  r      Corner radius in pixels / Raggio degli angoli in pixel
 * @param  color  Border colour           / Colore del bordo
 */
void GFX_DrawRoundRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                       uint16_t r, uint16_t color);

/**
 * @brief  Draw a filled rounded-corner rectangle.
 *         Disegna un rettangolo riempito con angoli arrotondati.
 * @param  x, y   Top-left corner         / Angolo in alto a sinistra
 * @param  w, h   Width and height        / Larghezza e altezza
 * @param  r      Corner radius in pixels / Raggio degli angoli in pixel
 * @param  color  Fill colour             / Colore di riempimento
 */
void GFX_FillRoundRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                       uint16_t r, uint16_t color);

/**
 * @brief  Draw a circle outline (midpoint circle algorithm).
 *         Disegna il bordo di un cerchio (algoritmo midpoint).
 * @param  cx, cy  Circle centre / Centro del cerchio
 * @param  r       Radius        / Raggio
 * @param  color   Colour        / Colore
 */
void GFX_DrawCircle(uint16_t cx, uint16_t cy, uint16_t r, uint16_t color);

/**
 * @brief  Draw a horizontal line (optimised vs GFX_DrawLine).
 *         Disegna una linea orizzontale (ottimizzata rispetto a DrawLine).
 * @param  x, y   Start point (left side) / Punto di partenza (sinistra)
 * @param  w      Length in pixels        / Lunghezza in pixel
 * @param  color  Colour                  / Colore
 */
void GFX_DrawHLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color);

/**
 * @brief  Draw a vertical line (optimised vs GFX_DrawLine).
 *         Disegna una linea verticale (ottimizzata rispetto a DrawLine).
 * @param  x, y   Start point (top) / Punto di partenza (in alto)
 * @param  h      Length in pixels  / Lunghezza in pixel
 * @param  color  Colour            / Colore
 */
void GFX_DrawVLine(uint16_t x, uint16_t y, uint16_t h, uint16_t color);

#ifdef __cplusplus
}
#endif

#endif /* __GFX_H */
