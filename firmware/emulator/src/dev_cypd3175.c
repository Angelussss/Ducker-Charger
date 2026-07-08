/**
 * @file    dev_cypd3175.c
 * @brief   CYPD3175 (CCG3PA, EZ-PD HPI) PD controller model @ I2C1 0x08.
 *
 * HPI register file with 16-bit addressing:
 *   0x0006 INTR      (write-1-to-clear; bit0 DEV, bit1 PORT0)
 *   0x007E RESPONSE  (event code of the oldest queued port event)
 *   0x100C PORT0 TYPE-C STATUS
 *   0x1010 PORT0 CURRENT PDO (4 B)   0x1014 PORT0 CURRENT RDO (4 B)
 *
 * Events queue up; INTR.PORT0 re-asserts after a clear while the queue
 * is non-empty (HPI behavior). INT line = PC3, active low.
 */
#include "sim.h"

#include <string.h>

#define HPI_INTR      0x0006
#define HPI_RESPONSE  0x007E
#define HPI_TCSTAT    0x100C
#define HPI_PDO       0x1010
#define HPI_RDO       0x1014

#define EVQ_MAX 8

typedef struct {
    uint8_t  intr;
    uint8_t  evq[EVQ_MAX];
    int      evq_n;
    uint8_t  pdo[4], rdo[4], tcstat;
    int      plugged;
} Cypd;

static Cypd cy;

static void int_update(void)
{
    sim_gpio_drive_input(2, 3, cy.intr ? 0 : 1);   /* PC3, active low */
}

static void ev_push(uint8_t code)
{
    if (cy.evq_n < EVQ_MAX)
        cy.evq[cy.evq_n++] = code;
    cy.intr |= 0x02;                                /* PORT0 */
    int_update();
}

static uint8_t reg_read_byte(uint16_t a)
{
    if (a == HPI_INTR)     return cy.intr;
    if (a == HPI_RESPONSE) return cy.evq_n ? cy.evq[0] : 0;
    if (a == HPI_TCSTAT)   return cy.plugged ? 0x01 : 0x00;
    if (a >= HPI_PDO && a < HPI_PDO + 4) return cy.pdo[a - HPI_PDO];
    if (a >= HPI_RDO && a < HPI_RDO + 4) return cy.rdo[a - HPI_RDO];
    return 0;
}

static void reg_write_byte(uint16_t a, uint8_t v)
{
    if (a == HPI_INTR) {
        /* W1C; consuming PORT0 pops the oldest event */
        if ((v & 0x02) && cy.evq_n) {
            memmove(cy.evq, cy.evq + 1, (size_t)(--cy.evq_n));
            if (cy.evq_n)
                return;      /* keep INTR asserted for the next event */
        }
        cy.intr &= (uint8_t)~v;
        int_update();
    }
}

static int cy_mem_read(SimI2CDev *d, uint16_t mem, uint16_t ms,
                       uint8_t *buf, uint16_t n)
{
    (void)d; (void)ms;
    for (uint16_t i = 0; i < n; i++)
        buf[i] = reg_read_byte((uint16_t)(mem + i));
    return 0;
}

static int cy_mem_write(SimI2CDev *d, uint16_t mem, uint16_t ms,
                        const uint8_t *buf, uint16_t n)
{
    (void)d; (void)ms;
    for (uint16_t i = 0; i < n; i++)
        reg_write_byte((uint16_t)(mem + i), buf[i]);
    return 0;
}

static SimI2CDev cy_dev = {
    .addr8 = (uint16_t)(0x08 << 1), .name = "CYPD3175",
    .mem_read = cy_mem_read, .mem_write = cy_mem_write,
};

/* ---- scenario API ---- */

void cypd_plug_sink(bool plugged)
{
    cy.plugged = plugged;
    if (plugged) {
        uint32_t pdo = (((uint32_t)(cfg.c2_mv / 50) & 0x3FF) << 10)
                     |  ((uint32_t)(cfg.c2_ma / 10) & 0x3FF);
        uint32_t rdo = (((uint32_t)(cfg.c2_ma / 10) & 0x3FF) << 10);
        memcpy(cy.pdo, &pdo, 4);
        memcpy(cy.rdo, &rdo, 4);
        ev_push(0x86);                      /* CONTRACT_COMPLETE */
        sim_log("[CYPD] sink attached: contract %d mV / %d mA",
                cfg.c2_mv, cfg.c2_ma);
    } else {
        memset(cy.pdo, 0, 4);
        memset(cy.rdo, 0, 4);
        ev_push(0x85);                      /* DISCONNECT */
        sim_log("[CYPD] sink detached");
    }
}

void cypd_inject(uint8_t hpi_event)
{
    sim_log("[CYPD] inject HPI event 0x%02X", hpi_event);
    ev_push(hpi_event);
}

void cypd_attach(void)
{
    memset(&cy, 0, sizeof(cy));
    sim_i2c_attach(1, &cy_dev);
}
