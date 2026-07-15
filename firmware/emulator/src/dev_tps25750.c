/**
 * @file    dev_tps25750.c
 * @brief   TPS25750 USB-C PD controller model @ I2C3 0x20 (+ burst 0x22).
 *
 * Modeled from the TPS25750 host-interface TRM, NOT from what the
 * firmware expects:
 *   - every register access is length-prefixed on the wire: a read
 *     returns [len][data...], a write payload is [len][data...] after
 *     the register byte. The device cannot distinguish HAL_I2C_Mem_*
 *     from Master_Transmit/Receive, same bus traffic.
 *   - boots in 'PTCH' mode; Patch Bundle Burst Mode (PBMs -> burst
 *     writes to the alternate address -> PBMc) moves it to 'APP '.
 *   - I2Cs_IRQ (PB14, active low) asserted while INT_EVENT1 != 0,
 *     cleared through INT_CLEAR1.
 *
 * cfg.tps_strict_len = 0 relaxes the length prefix on the HAL_I2C_Mem_*
 * path only. It was a workaround for a charge.c framing bug (INT_EVENT1
 * read without skipping the length byte), fixed since: all firmware
 * register access now goes through system/tps25750_io.c, which frames
 * correctly. Keep = 1.
 */
#include "sim.h"

#include <string.h>

#define REG_MODE   0x03
#define REG_CMD1   0x08
#define REG_DATA1  0x09
#define REG_IEV1   0x14
#define REG_IMSK1  0x16
#define REG_ICLR1  0x18
#define REG_PDO    0x34
#define REG_RDO    0x35
#define REG_PSTAT  0x3F

#define FOURCC(a,b,c,d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | \
                         ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

typedef struct {
    uint32_t mode;              /* 'PTCH' or 'APP '                */
    uint32_t cmd1;              /* readback: 0 ok / '!CMD'         */
    uint8_t  data1[64];
    uint8_t  iev[11];           /* INT_EVENT1                      */
    uint8_t  pdo[6];
    uint8_t  rdo[4];
    uint8_t  pstat[2];
    /* patch-bundle burst state */
    uint32_t patch_expect, patch_recv;
    int      patch_open;
    uint8_t  reg_ptr;
    int      plugged;
} Tps;

static Tps tp;

static void irq_update(void)
{
    int pending = 0;
    for (int i = 0; i < 11; i++) pending |= tp.iev[i];
    /* PB14, active low */
    sim_gpio_drive_input(1, 14, pending ? 0 : 1);
}

/* register payload lengths as reported by the device */
static uint8_t reg_len(uint8_t reg)
{
    switch (reg) {
    case REG_MODE:  return 4;
    case REG_CMD1:  return 4;
    case REG_DATA1: return 64;
    case REG_IEV1:
    case REG_IMSK1:
    case REG_ICLR1: return 11;
    case REG_PDO:   return 6;
    case REG_RDO:   return 4;
    case REG_PSTAT: return 2;
    default:        return 0;
    }
}

static const uint8_t *reg_data(uint8_t reg)
{
    switch (reg) {
    case REG_MODE:  return (const uint8_t *)&tp.mode;
    case REG_CMD1:  return (const uint8_t *)&tp.cmd1;
    case REG_DATA1: return tp.data1;
    case REG_IEV1:  return tp.iev;
    case REG_PDO:   return tp.pdo;
    case REG_RDO:   return tp.rdo;
    case REG_PSTAT: return tp.pstat;
    default:        return NULL;
    }
}

static void run_cmd(uint32_t cmd)
{
    if (cmd == FOURCC('P','B','M','s')) {
        if (tp.mode != FOURCC('P','T','C','H')) { tp.cmd1 = FOURCC('!','C','M','D'); return; }
        tp.patch_expect = (uint32_t)tp.data1[0] | ((uint32_t)tp.data1[1] << 8)
                        | ((uint32_t)tp.data1[2] << 16) | ((uint32_t)tp.data1[3] << 24);
        tp.patch_recv = 0;
        tp.patch_open = 1;
        tp.cmd1 = 0;
        sim_log("[TPS ] PBMs: expecting %u-byte bundle at 0x%02X",
                tp.patch_expect, tp.data1[4] >> 1);
    } else if (cmd == FOURCC('P','B','M','c')) {
        if (tp.patch_open && tp.patch_recv == tp.patch_expect) {
            tp.mode = FOURCC('A','P','P',' ');
            tp.cmd1 = 0;
            sim_log("[TPS ] PBMc: bundle complete (%u B) -> APP mode", tp.patch_recv);
        } else {
            tp.cmd1 = FOURCC('!','C','M','D');
            sim_log("[TPS ] PBMc REJECTED: got %u of %u bytes",
                    tp.patch_recv, tp.patch_expect);
        }
        tp.patch_open = 0;
    } else if (cmd == FOURCC('P','B','M','e')) {
        tp.patch_open = 0;
        tp.cmd1 = 0;
        sim_log("[TPS ] PBMe: patch aborted");
    } else {
        tp.cmd1 = FOURCC('!','C','M','D');
        sim_log("[TPS ] unknown 4CC command 0x%08X", cmd);
    }
}

/* wire-level write: data = [len][payload...] (strict) */
static void wire_write(uint8_t reg, const uint8_t *data, uint16_t n,
                       int strict)
{
    uint8_t len;
    const uint8_t *p;

    if (strict) {
        if (n == 0) return;
        len = data[0];
        p = data + 1;
        if (len > n - 1) len = (uint8_t)(n - 1);
    } else {
        len = (uint8_t)n;
        p = data;
    }

    switch (reg) {
    case REG_CMD1:
        if (len >= 4)
            run_cmd((uint32_t)p[0] | ((uint32_t)p[1] << 8)
                    | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
        break;
    case REG_DATA1:
        if (len > sizeof(tp.data1)) len = sizeof(tp.data1);
        memcpy(tp.data1, p, len);
        break;
    case REG_ICLR1:
        for (int i = 0; i < len && i < 11; i++)
            tp.iev[i] &= (uint8_t)~p[i];
        irq_update();
        break;
    case REG_IMSK1:
    default:
        break;      /* mask & read-only regs: ignore */
    }
}

static void wire_read(uint8_t reg, uint8_t *buf, uint16_t n, int strict)
{
    uint8_t len = reg_len(reg);
    const uint8_t *d = reg_data(reg);
    uint16_t k = 0;

    if (strict && n > 0)
        buf[k++] = len;
    for (uint8_t i = 0; i < len && k < n; i++)
        buf[k++] = d ? d[i] : 0;
    while (k < n)
        buf[k++] = 0;
}

/* ---- SimI2CDev glue: main address ---- */

static int tps_mem_write(SimI2CDev *dv, uint16_t mem, uint16_t ms,
                         const uint8_t *d, uint16_t n)
{
    (void)dv; (void)ms;
    wire_write((uint8_t)mem, d, n, cfg.tps_strict_len);
    return 0;
}

static int tps_mem_read(SimI2CDev *dv, uint16_t mem, uint16_t ms,
                        uint8_t *d, uint16_t n)
{
    (void)dv; (void)ms;
    wire_read((uint8_t)mem, d, n, cfg.tps_strict_len);
    return 0;
}

/* raw transactions are length-prefixed regardless of leniency: this is
 * the path initialization.c implements correctly */
static int tps_mtx(SimI2CDev *dv, const uint8_t *d, uint16_t n)
{
    (void)dv;
    if (n == 0) return -1;
    tp.reg_ptr = d[0];
    if (n > 1)
        wire_write(d[0], d + 1, (uint16_t)(n - 1), 1);
    return 0;
}

static int tps_mrx(SimI2CDev *dv, uint8_t *d, uint16_t n)
{
    (void)dv;
    wire_read(tp.reg_ptr, d, n, 1);
    return 0;
}

static SimI2CDev tps_dev = {
    .addr8 = (uint16_t)(0x20 << 1), .name = "TPS25750",
    .mem_read = tps_mem_read, .mem_write = tps_mem_write,
    .mtx = tps_mtx, .mrx = tps_mrx,
};

/* ---- burst (patch download) address ---- */

static int burst_mtx(SimI2CDev *dv, const uint8_t *d, uint16_t n)
{
    (void)dv; (void)d;
    if (!tp.patch_open) return -1;      /* NACK outside burst mode */
    tp.patch_recv += n;
    return 0;
}

static SimI2CDev burst_dev = {
    .addr8 = (uint16_t)(0x22 << 1), .name = "TPS25750-burst",
    .mtx = burst_mtx,
};

/* ---- scenario API ---- */

static void encode_contract(int mv, int ma)
{
    uint32_t pdo = (((uint32_t)(mv / 50) & 0x3FF) << 10)
                 |  ((uint32_t)(ma / 10) & 0x3FF);        /* fixed PDO */
    uint32_t rdo = (((uint32_t)(ma / 10) & 0x3FF) << 10); /* op current */
    memset(tp.pdo, 0, sizeof(tp.pdo));
    memcpy(tp.pdo, &pdo, 4);
    memcpy(tp.rdo, &rdo, 4);
}

void tps_plug_charger(bool plugged)
{
    tp.plugged = plugged;
    if (plugged) {
        tp.pstat[0] = 0x03 | (0x02 << 2);   /* connected, sink, 3A TypeC */
        encode_contract(cfg.c1_mv, cfg.c1_ma);
        tp.iev[0] |= 1u << 3;               /* PlugInsertOrRemoval       */
        tp.iev[1] |= 1u << 4;               /* NewContractAsConsumer     */
        tp.iev[3] |= 1u << 0;               /* PowerStatusUpdate         */
        sim_log("[TPS ] charger plugged: contract %d mV / %d mA",
                cfg.c1_mv, cfg.c1_ma);
    } else {
        tp.pstat[0] = 0x00;
        memset(tp.pdo, 0, sizeof(tp.pdo));
        memset(tp.rdo, 0, sizeof(tp.rdo));
        tp.iev[0] |= 1u << 3;
        tp.iev[3] |= 1u << 0;
        sim_log("[TPS ] charger unplugged");
    }
    irq_update();
}

void tps_plug_device(bool plugged)
{
    tp.plugged = plugged;
    if (plugged) {
        /* connection=1, sourceSink=0: WE are the source */
        tp.pstat[0] = 0x01;
        encode_contract(cfg.c1src_mv, cfg.c1src_ma);
        tp.iev[0] |= 1u << 3;               /* PlugInsertOrRemoval       */
        tp.iev[1] |= 1u << 5;               /* NewContractAsProvider     */
        tp.iev[3] |= 1u << 0;               /* PowerStatusUpdate         */
        sim_log("[TPS ] device plugged, sourcing: contract %d mV / %d mA",
                cfg.c1src_mv, cfg.c1src_ma);
    } else {
        tp.pstat[0] = 0x00;
        memset(tp.pdo, 0, sizeof(tp.pdo));
        memset(tp.rdo, 0, sizeof(tp.rdo));
        tp.iev[0] |= 1u << 3;
        tp.iev[3] |= 1u << 0;
        sim_log("[TPS ] device unplugged");
    }
    irq_update();
}

void tps_attach(void)
{
    memset(&tp, 0, sizeof(tp));
    tp.mode = cfg.tps_app_mode ? FOURCC('A','P','P',' ')
                               : FOURCC('P','T','C','H');
    sim_i2c_attach(3, &tps_dev);
    sim_i2c_attach(3, &burst_dev);
}
