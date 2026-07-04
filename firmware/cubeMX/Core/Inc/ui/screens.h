/**
 * @file    screens.h
 * @brief   Application UI screens declarations.
 *          Dichiarazione delle schermate dell'interfaccia utente.
 *
 * Each screen is defined by two functions:
 * Ogni schermata e' definita da due funzioni:
 *
 *   Screen_XXX_Draw()   --> draws EVERYTHING (background, fixed labels, values).
 *                           Called only when entering the screen.
 *                           Disegna TUTTO (sfondo, etichette fisse, valori).
 *                           Chiamata solo quando si entra nella schermata.
 *
 *   Screen_XXX_Update() --> redraws ONLY the parts that change (numbers).
 *                           Called periodically by the UI loop.
 *                           Ridisegna SOLO le parti che cambiano (i numeri).
 *                           Chiamata periodicamente dal loop UI.
 *
 * This two-phase approach eliminates flickering: the background and fixed
 * labels are drawn once, only the numerical values are overwritten.
 * Questo approccio a due fasi elimina il flickering: lo sfondo e le etichette
 * fisse vengono disegnati una volta sola, poi vengono sovrascritti solo i valori.
 *
 * SCREEN LAYOUTS / LAYOUT DELLE SCHERMATE:
 *
 * MAIN (240x320):
 *   ┌──────────────────────┐
 *   │  DUCKER  [●USB-C]    │  <- header (30 px)
 *   ├──────────────────────┤
 *   │   [████████░░] 85%   │  <- battery bar (40 px)
 *   │      14.75 V         │  <- voltage (40 px)
 *   │     +2450 mA         │  <- current (40 px)
 *   ├──────────────────────┤
 *   │  ~~~~~~~~~~~~~~~~~~~~│  <- current graph (100 px)
 *   ├──────────────────────┤
 *   │  ●  ○  ○  ○          │  <- navigation dots footer (30 px)
 *   └──────────────────────┘
 *
 * DETAIL: 2x3 grid of ValueLabel widgets with all telemetry data.
 *         Griglia 2x3 di widget ValueLabel con tutti i dati di telemetria.
 *
 * GRAPH:  Full-screen graph + time axis + legend.
 *         Grafico fullscreen + asse temporale + legenda.
 *
 * SETTINGS: Scrollable list of ON/OFF toggles navigable with the encoder.
 *           Lista di toggle ON/OFF navigabile con l'encoder.
 */

#ifndef __SCREENS_H
#define __SCREENS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

/* =========================================================
 * SCREEN: BOOT (splash screen)
 * ========================================================= */

/**
 * @brief  Show the project logo/name at startup.
 *         Mostra il logo/nome del progetto all'avvio.
 * @note   No _Update() because the screen is static. After ~1.5 s
 *         ui_state.c automatically navigates to MAIN.
 *         Non ha _Update() perche' e' statica. Dopo ~1.5s
 *         ui_state.c naviga automaticamente a MAIN.
 */
void Screen_Boot_Draw(void);

/* =========================================================
 * SCREEN: MAIN (home)
 * ========================================================= */

/**
 * @brief  Draw the main screen in full.
 *         Disegna la schermata principale completa.
 * @note   Draws background, all labels and the current values. Called only
 *         when entering the screen (e.g. at startup or returning from another).
 *         Disegna sfondo, tutte le etichette e i valori correnti. Chiamata solo
 *         quando si entra nella schermata (es. all'avvio o tornando da un'altra).
 */
void Screen_Main_Draw(void);

/**
 * @brief  Update only the dynamic parts of the main screen.
 *         Aggiorna solo le parti dinamiche della schermata principale.
 * @note   Redraws: SoC percentage, voltage, current, graph.
 *         Does NOT redraw: background, labels, static icons.
 *         Called periodically by UI_Tick().
 *         Ridisegna: percentuale SoC, tensione, corrente, grafico.
 *         NON ridisegna: sfondo, etichette, icone statiche.
 *         Chiamata periodicamente da UI_Tick().
 */
void Screen_Main_Update(void);

/* =========================================================
 * SCREEN: DETAIL (full telemetry data)
 * ========================================================= */

/**
 * @brief  Draw the screen showing all system data.
 *         Disegna la schermata con tutti i dati del sistema.
 * @note   Shows: SoC, voltage, current, power, temperature, charger state.
 *         Mostra: SoC, tensione, corrente, potenza, temperatura, stato charger.
 */
void Screen_Detail_Draw(void);

/**
 * @brief  Update the numerical values on the detail screen.
 *         Aggiorna i valori numerici nella schermata detail.
 */
void Screen_Detail_Update(void);

/* =========================================================
 * SCREEN: GRAPH (full-screen graph)
 * ========================================================= */

/**
 * @brief  Draw the full-screen graph screen.
 *         Disegna la schermata con il grafico a pieno schermo.
 * @note   The graph shows the last TELEMETRY_HISTORY_SIZE current samples.
 *         X axis = time, Y axis = current in mA.
 *         Il grafico mostra gli ultimi TELEMETRY_HISTORY_SIZE campioni di corrente.
 *         Asse X = tempo, asse Y = corrente in mA.
 */
void Screen_Graph_Draw(void);

/**
 * @brief  Update the graph with new data.
 *         Aggiorna il grafico con i nuovi dati.
 * @note   Redraws the entire graph area (partial update on a line graph
 *         without artifacts is impractical).
 *         Ridisegna l'intera area del grafico (l'update parziale su un
 *         grafico a linee senza artefatti e' poco pratico).
 */
void Screen_Graph_Update(void);

/* =========================================================
 * SCREEN: SETTINGS
 * ========================================================= */

/**
 * @brief  Draw the settings screen.
 *         Disegna la schermata delle impostazioni.
 * @note   Available entries / Voci disponibili:
 *           - USB-A 1:    ON / OFF  (USB_A1_CTRL_Pin)
 *           - USB-A 2:    ON / OFF  (USB_A2_CTTL_Pin)
 *           - Lab output: ON / OFF  (LAB_ENABLER_Pin)
 *           - USB-C 2:    ON / OFF  (USB_C2_ENABLER_Pin)
 * @param  selected_row  Currently highlighted row (0–3) /
 *                       Riga attualmente evidenziata (0–3)
 */
void Screen_Settings_Draw(uint8_t selected_row);

/**
 * @brief  Update the row highlight and the ON/OFF values.
 *         Aggiorna l'evidenziazione della riga e i valori ON/OFF.
 * @param  selected_row  Currently highlighted row / Riga attualmente evidenziata
 */
void Screen_Settings_Update(uint8_t selected_row);

/**
 * @brief  Toggle the ON/OFF state of the output at the given row.
 *         Inverte lo stato ON/OFF dell'uscita alla riga indicata.
 * @note   Updates both the internal state and the physical GPIO pin.
 *         Called from ui_state.c when the button is pressed in SETTINGS.
 *         Aggiorna sia lo stato interno che il pin GPIO fisico.
 *         Chiamata da ui_state.c alla pressione del bottone in SETTINGS.
 * @param  row  Row index (0 = USB-A1, 1 = USB-A2, 2 = Lab, 3 = USB-C2) /
 *              Indice della riga (0 = USB-A1, 1 = USB-A2, 2 = Lab, 3 = USB-C2)
 */
void Screen_Settings_Toggle(uint8_t row);

/**
 * @brief  Return the appropriate colour for a battery charge percentage.
 *         Restituisce il colore adeguato per una percentuale di carica.
 * @note   Shared utility used by screens.c and widgets.c.
 *         >50% = green, >20% = yellow, <=20% = red.
 *         Utilita' condivisa tra screens.c e widgets.c.
 *         >50% = verde, >20% = giallo, <=20% = rosso.
 * @param  pct  Percentage 0–100 / Percentuale 0–100
 * @retval RGB565 colour / Colore RGB565
 */
uint16_t battery_color_pub(uint8_t pct);

#ifdef __cplusplus
}
#endif

#endif /* __SCREENS_H */
