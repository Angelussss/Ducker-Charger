/**
 * @file    widgets.h
 * @brief   Reusable UI components: bars, graphs, labels, icons.
 *          Componenti UI riutilizzabili: barre, grafici, label, icone.
 *
 * A "widget" is a self-contained graphical component that knows how to
 * draw itself and update itself partially.
 * Un "widget" e' un componente grafico autonomo che sa come disegnarsi
 * e aggiornarsi parzialmente.
 *
 * PARTIAL-UPDATE PRINCIPLE / PRINCIPIO DI AGGIORNAMENTO PARZIALE:
 *   To avoid flickering, we never redraw the entire screen. Each widget
 *   typically has a _Draw() function that draws everything the first time,
 *   and an _Update() function that redraws ONLY the parts that changed
 *   (usually just the numbers, not the background).
 *
 *   Per evitare il flickering, non ridisegniamo mai l'intera schermata.
 *   Ogni widget ha tipicamente una _Draw() che disegna tutto la prima volta,
 *   e una _Update() che ridisegna SOLO le parti cambiate (di solito solo i
 *   numeri, non lo sfondo).
 *
 * AUTOMATIC BATTERY COLOUR / COLORE AUTOMATICO BATTERIA:
 *   > 50%  --> Green  / Verde  (COLOR_GREEN)
 *   > 20%  --> Yellow / Giallo (COLOR_YELLOW)
 *   <= 20% --> Red    / Rosso  (COLOR_DANGER)
 */

#ifndef __WIDGETS_H
#define __WIDGETS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include "display/gfx.h"

/* =========================================================
 * WIDGET: BATTERY BAR / BARRA DELLA BATTERIA
 * ========================================================= */

/**
 * @brief  Draw the full battery bar (background + fill + percentage text).
 *         Disegna la barra della batteria completa (sfondo + riempimento + testo %).
 * @param  x, y   Top-left corner of the widget / Angolo in alto a sinistra del widget
 * @param  w, h   Widget dimensions / Dimensioni del widget
 * @param  pct    Charge percentage (0–100) / Percentuale di carica (0–100)
 *
 * Draws / Disegna:
 *   [███████░░░░░░]  85%
 *   Fill colour changes automatically based on pct.
 *   Il colore del riempimento cambia automaticamente in base a pct.
 */
void Widget_BatteryBar_Draw(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t pct);

/**
 * @brief  Update only the fill and text in an already-drawn battery bar.
 *         Aggiorna solo il riempimento e il testo nella barra gia' disegnata.
 * @note   Much faster than _Draw(). Use this in the update loop.
 *         Molto piu' veloce di _Draw(). Usare nel loop di aggiornamento.
 * @param  x, y   Same coordinates used in _Draw() / Stesse coordinate usate in _Draw()
 * @param  w, h   Same dimensions used in _Draw()  / Stesse dimensioni usate in _Draw()
 * @param  pct    New percentage / Nuova percentuale
 */
void Widget_BatteryBar_Update(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t pct);

/* =========================================================
 * WIDGET: LINE GRAPH / GRAFICO A LINEE
 * ========================================================= */

/**
 * @brief  Draw a line graph from a ring-buffer of data samples.
 *         Disegna un grafico a linee da un ring buffer di campioni.
 * @note   Redraws the entire graph area each call. Keep the call rate low
 *         (max ~2 Hz) or call after clearing the area.
 *         Ridisegna l'intera area ogni chiamata. Mantenere la frequenza bassa
 *         (max ~2 Hz) o chiamare dopo aver pulito l'area.
 *
 * @param  x, y       Top-left of the graph area / Angolo in alto a sinistra dell'area
 * @param  w, h       Graph area dimensions in pixels / Dimensioni dell'area in pixel
 * @param  data       Circular buffer of samples (int16_t, can be negative) /
 *                    Buffer circolare dei campioni (int16_t, puo' avere valori negativi)
 * @param  data_len   Total number of slots in the buffer (e.g. TELEMETRY_HISTORY_SIZE) /
 *                    Numero totale di slot nel buffer
 * @param  data_idx   Index of the next free slot (marks where the buffer starts) /
 *                    Indice del prossimo slot libero (segna dove inizia il buffer)
 * @param  val_min    Expected minimum value, used to scale the Y axis /
 *                    Valore minimo atteso, usato per scalare l'asse Y
 * @param  val_max    Expected maximum value / Valore massimo atteso
 * @param  line_color Graph line colour / Colore della linea del grafico
 * @param  bg_color   Graph area background colour / Colore dello sfondo dell'area
 *
 * Example / Esempio (current graph, -5000 mA to +5000 mA):
 *   Widget_LineGraph_Draw(10, 100, 220, 80,
 *                         telemetry.current_history,
 *                         TELEMETRY_HISTORY_SIZE, telemetry.history_idx,
 *                         -5000, 5000, COLOR_CYAN, COLOR_BG);
 */
void Widget_LineGraph_Draw(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                           int16_t *data, uint8_t data_len, uint8_t data_idx,
                           int16_t val_min, int16_t val_max,
                           uint16_t line_color, uint16_t bg_color);

/* =========================================================
 * WIDGET: LARGE VALUE LABEL / LABEL VALORE GRANDE
 *
 * Shows a small label above a large numerical value.
 * Mostra una piccola etichetta sopra un valore numerico grande.
 *
 *   VOLTAGE / TENSIONE
 *   14.75 V
 * ========================================================= */

/**
 * @brief  Draw a label with a small header and a large numerical value.
 *         Disegna una label con intestazione piccola e valore numerico grande.
 * @param  x, y         Position / Posizione
 * @param  label        Header text (e.g. "VOLTAGE") / Testo intestazione (es. "TENSIONE")
 * @param  value_str    Value already formatted as a string (e.g. "14.75 V") /
 *                      Valore gia' formattato come stringa (es. "14.75 V")
 * @param  value_color  Colour of the large number / Colore del numero grande
 */
void Widget_ValueLabel_Draw(uint16_t x, uint16_t y,
                            const char *label, const char *value_str,
                            uint16_t value_color);

/**
 * @brief  Update only the numerical value part, without redrawing the label.
 *         Aggiorna solo la parte del valore numerico, senza ridisegnare l'etichetta.
 * @param  x, y         Same position as _Draw() / Stessa posizione di _Draw()
 * @param  value_str    New formatted value / Nuovo valore formattato
 * @param  value_color  Colour of the number / Colore del numero
 */
void Widget_ValueLabel_Update(uint16_t x, uint16_t y,
                              const char *value_str, uint16_t value_color);

/* =========================================================
 * WIDGET: STATUS ICON / INDICATORE DI STATO
 *
 * Shows a small coloured icon with a text label.
 * Mostra una piccola icona colorata con un'etichetta testuale.
 *   [●] USB-C CONNECTED / COLLEGATO
 * ========================================================= */

/** Available icon types / Tipi di icona disponibili */
typedef enum {
    ICON_USB_C = 0,   /**< USB-C connector / Connettore USB-C           */
    ICON_CHARGING,    /**< Lightning bolt (charging) / Fulmine (carica) */
    ICON_FULL,        /**< Checkmark (full) / Spunta (carico)           */
    ICON_TEMP_OK,     /**< Green thermometer / Termometro verde         */
    ICON_TEMP_WARN,   /**< Orange thermometer / Termometro arancione    */
    ICON_OUTPUT_ON,   /**< Active USB output / Uscita USB attiva        */
    ICON_OUTPUT_OFF,  /**< Inactive USB output / Uscita USB inattiva    */
} IconType_t;

/**
 * @brief  Draw a status indicator with icon and text.
 *         Disegna un indicatore di stato con icona e testo.
 * @param  x, y   Position / Posizione
 * @param  type   Icon type (see IconType_t) / Tipo di icona (vedi IconType_t)
 * @param  label  Text next to the icon / Testo accanto all'icona
 * @param  active 1 = active (teal colour), 0 = inactive (grey) /
 *                1 = attivo (colore acqua), 0 = inattivo (grigio)
 */
void Widget_StatusIcon_Draw(uint16_t x, uint16_t y, IconType_t type,
                            const char *label, uint8_t active);

/* =========================================================
 * WIDGET: MENU SELECTION ROW / RIGA DI SELEZIONE MENU
 * Highlights the currently selected entry in a menu.
 * Evidenzia la voce attualmente selezionata in un menu.
 * ========================================================= */

/**
 * @brief  Draw a menu row with optional selection highlight.
 *         Disegna una riga di menu con evidenziazione opzionale.
 * @param  x, y     Position / Posizione
 * @param  w, h     Row dimensions / Dimensioni della riga
 * @param  text     Row text / Testo della voce
 * @param  selected 1 = highlighted (accent background), 0 = normal /
 *                  1 = evidenziata (sfondo accent), 0 = normale
 */
void Widget_MenuRow_Draw(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                         const char *text, uint8_t selected);

#ifdef __cplusplus
}
#endif

#endif /* __WIDGETS_H */
