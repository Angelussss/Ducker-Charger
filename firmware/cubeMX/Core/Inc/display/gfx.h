/**
 * @file    gfx.h
 * @brief   Graphics primitives: text, lines, circles, rectangles.
 *
 * This module sits above the ILI9341 driver and below the widget layer.
 * It knows how to draw geometric shapes and text, but has no concept of
 * "battery bar" or "graph"; that is the job of the widgets.
 *
 * BITMAP FONTS:
 *   Fixed-size bitmap fonts are included. A bitmap font is a C array where
 *   each character is represented as a grid of bits:
 *   1 = pixel on (foreground colour), 0 = pixel off (background colour).
 *
 *
 * COORDINATE SYSTEM:
 *   Origin (0,0) = TOP-LEFT corner of the screen.
 *   X grows rightward, Y grows downward.
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
 * FONT DIMENSIONS
 * ========================================================= */

/** Small font: 6x8 px per character. For secondary info. */
#define GFX_FONT_SMALL_W   6
#define GFX_FONT_SMALL_H   8

/** Medium font: 8x16 px per character. For normal text.*/
#define GFX_FONT_MEDIUM_W  8
#define GFX_FONT_MEDIUM_H  16

/** Large font: 16x24 px per character. For important numbers (SoC, voltage). */
#define GFX_FONT_LARGE_W   16
#define GFX_FONT_LARGE_H   24

/* =========================================================
 * FONT TYPE DEFINITION
 * ========================================================= */

/**
 * @brief Descriptor for a bitmap font.
 *
 * The `data` field points to an array where each ASCII character
 * (starting from ' ' = 0x20) is stored as a sequence of bytes,
 * one per row, representing the on/off pixels of that row.
 *
 */
typedef struct {
    const uint8_t *data;  /**< Pointer to the bitmap data */
    uint8_t char_w;       /**< Character width in pixels */
    uint8_t char_h;       /**< Character height in pixels */
} GFX_Font_t;

/* =========================================================
 * AVAILABLE FONTS (defined in gfx.c)
 * ========================================================= */

extern const GFX_Font_t GFX_FontSmall;   /**< 6x8  – secondary text */
extern const GFX_Font_t GFX_FontMedium;  /**< 8x16 – normal text */
extern const GFX_Font_t GFX_FontLarge;   /**< 16x24 – large numbers */

/* =========================================================
 * PUBLIC API – TEXT
 * ========================================================= */

/**
 * @brief  Draw a single ASCII character.

 * @param  x, y      Top-left corner of the character
 * @param  c         Character to draw (ASCII 32–126)
 * @param  font      Pointer to the font to use
 * @param  fg_color  Foreground (text) colour
 * @param  bg_color  Background colour drawn behind each glyph 
 */
/* scaled text: every font pixel become scale x scale block. chunky. */
void GFX_DrawStringScaled(uint16_t x, uint16_t y, const char *str,
                          const GFX_Font_t *font, uint8_t scale,
                          uint16_t fg_color, uint16_t bg_color);

/* blit raw RGB565 bitmap (row-major) */
void GFX_DrawBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                    const uint16_t *data);

void GFX_DrawChar(uint16_t x, uint16_t y, char c,
                  const GFX_Font_t *font, uint16_t fg_color, uint16_t bg_color);

/**
 * @brief  Draw a null-terminated ( '\0') text string.

 * @param  x, y      Position of the first character (top-left) /
 * @param  str       Null-terminated string
 * @param  font      Font to use
 * @param  fg_color  Text colour
 * @param  bg_color  Background colour
 *
 * Example
 *   GFX_DrawString(10, 20, "Charge: 85%", &GFX_FontMedium, COLOR_WHITE, COLOR_BG);
 */
void GFX_DrawString(uint16_t x, uint16_t y, const char *str,
                    const GFX_Font_t *font, uint16_t fg_color, uint16_t bg_color);

/**
 * @brief  Draw an integer value formatted as a string.
 * @note   Convenient alternative to calling snprintf() + GFX_DrawString() every time.
 * @param  x, y      Position
 * @param  value     Integer value to display
 * @param  font      Font to use
 * @param  fg_color  Text colour
 * @param  bg_color  Background colour
 */
void GFX_DrawInt(uint16_t x, uint16_t y, int32_t value,
                 const GFX_Font_t *font, uint16_t fg_color, uint16_t bg_color);

/* =========================================================
 * PUBLIC API – SHAPES
 * ========================================================= */

/**
 * @brief  Draw a line between two points (Bresenham's algorithm).
 *
 * @param  x0, y0  Start point
 * @param  x1, y1  End point
 * @param  color   Line colour
 */
void GFX_DrawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);

/**
 * @brief  Draw the outline of a rectangle (not filled).
 *
 * @param  x, y   Top-left corner
 * @param  w, h   Width and height
 * @param  color  Border colour
 */
void GFX_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

/**
 * @brief  Draw a rounded-corner rectangle outline.
 * @param  x, y   Top-left corner
 * @param  w, h   Width and height
 * @param  r      Corner radius in pixels
 * @param  color  Border colour
 */
void GFX_DrawRoundRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                       uint16_t r, uint16_t color);

/**
 * @brief  Draw a filled rounded-corner rectangle.
 * @param  x, y   Top-left corner
 * @param  w, h   Width and height
 * @param  r      Corner radius in pixels
 * @param  color  Fill colour
 */
void GFX_FillRoundRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                       uint16_t r, uint16_t color);

/**
 * @brief  Draw a circle outline (midpoint circle algorithm).
 *
 * @param  cx, cy  Circle centre
 * @param  r       Radius       
 * @param  color   Colour       
 */
void GFX_DrawCircle(uint16_t cx, uint16_t cy, uint16_t r, uint16_t color);

/**
 * @brief  Draw a horizontal line (optimised vs GFX_DrawLine).
 *         
 * @param  x, y   Start point (left side) 
 * @param  w      Length in pixels        
 * @param  color  Colour                  
 */
void GFX_DrawHLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color);

/**
 * @brief  Draw a vertical line (optimised vs GFX_DrawLine).
 *         
 * @param  x, y   Start point (top) 
 * @param  h      Length in pixels  
 * @param  color  Colour            
 */
void GFX_DrawVLine(uint16_t x, uint16_t y, uint16_t h, uint16_t color);

#ifdef __cplusplus
}
#endif

#endif /* __GFX_H */
