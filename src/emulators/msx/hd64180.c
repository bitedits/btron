/*
 * hd64180.c — Hitachi HD64180 CPU emulation for Hitachi HC-90 / HC-95.
 * Hardware emulation source for B-MSX. Clean-room C99.
 * NASA JPL Power of Ten rules compliant.
 */

#include "hd64180.h"
#include <string.h>

hd64180_state_t g_hd64180_state;

void hd64180_init(hd64180_t *cpu)
{
    if (!cpu) return;
    z80_init(cpu);
    hd64180_reset();
}

void hd64180_reset(void)
{
    memset(&g_hd64180_state, 0, sizeof(g_hd64180_state));
    g_hd64180_state.cbar = 0xF0u; /* Common Area = F000-FFFF, Bank Area = 0000-EFFF */
    g_hd64180_state.cbr  = 0x00u;
    g_hd64180_state.bbr  = 0x00u;
    g_hd64180_state.icr  = 0x00u; /* internal I/O default at 0x00-0x3F */
    g_hd64180_state.rldr[0] = 0xFFFFu;
    g_hd64180_state.rldr[1] = 0xFFFFu;
    g_hd64180_state.tmdr[0] = 0xFFFFu;
    g_hd64180_state.tmdr[1] = 0xFFFFu;

    /* ASCI: TDRE (bit 1) = 1 (Transmit ready) */
    g_hd64180_state.asci_stat[0] = 0x02u;
    g_hd64180_state.asci_stat[1] = 0x02u;

    /* CSIO */
    g_hd64180_state.csio_cntr = 0x00u;
    g_hd64180_state.csio_trdr = 0xFFu;

    /* DMAC: DME (bit 0) = 1 (Master DMA enable) */
    g_hd64180_state.dma_dstat = 0x01u;
}

int hd64180_is_internal_io(uint8_t port)
{
    uint8_t base = (uint8_t)(g_hd64180_state.icr & 0xC0u);
    return ((port & 0xC0u) == base);
}

uint8_t hd64180_io_read(uint8_t port)
{
    uint8_t base = (uint8_t)(g_hd64180_state.icr & 0xC0u);
    if ((port & 0xC0u) != base) return 0xFFu;
    uint8_t reg = (uint8_t)(port & 0x3Fu);

    switch (reg) {
    /* ASCI Channels 0 & 1 */
    case 0x00: return g_hd64180_state.asci_cntla[0];
    case 0x01: return g_hd64180_state.asci_cntla[1];
    case 0x02: return g_hd64180_state.asci_cntlb[0];
    case 0x03: return g_hd64180_state.asci_cntlb[1];
    case 0x04: return g_hd64180_state.asci_stat[0];
    case 0x05: return g_hd64180_state.asci_stat[1];
    case 0x06: return g_hd64180_state.asci_tdr[0];
    case 0x07: return g_hd64180_state.asci_tdr[1];
    case 0x08: {
        uint8_t v = g_hd64180_state.asci_rdr[0];
        g_hd64180_state.asci_stat[0] &= ~0x80u; /* clear RDRF */
        if (g_hd64180_state.asci_rx_count[0] > 0) {
            uint8_t tail = g_hd64180_state.asci_rx_tail[0];
            g_hd64180_state.asci_rdr[0] = g_hd64180_state.asci_rx_fifo[0][tail];
            g_hd64180_state.asci_rx_tail[0] = (uint8_t)((tail + 1u) & 15u);
            g_hd64180_state.asci_rx_count[0]--;
            g_hd64180_state.asci_stat[0] |= 0x80u; /* RDRF set for next byte */
        }
        return v;
    }
    case 0x09: {
        uint8_t v = g_hd64180_state.asci_rdr[1];
        g_hd64180_state.asci_stat[1] &= ~0x80u; /* clear RDRF */
        if (g_hd64180_state.asci_rx_count[1] > 0) {
            uint8_t tail = g_hd64180_state.asci_rx_tail[1];
            g_hd64180_state.asci_rdr[1] = g_hd64180_state.asci_rx_fifo[1][tail];
            g_hd64180_state.asci_rx_tail[1] = (uint8_t)((tail + 1u) & 15u);
            g_hd64180_state.asci_rx_count[1]--;
            g_hd64180_state.asci_stat[1] |= 0x80u;
        }
        return v;
    }

    /* CSIO */
    case 0x0A: return g_hd64180_state.csio_cntr;
    case 0x0B: {
        uint8_t v = g_hd64180_state.csio_trdr;
        g_hd64180_state.csio_cntr &= ~0x80u; /* Reading TRDR clears EF */
        return v;
    }

    /* PRT Timer 0 & 1 */
    case 0x0C: return (uint8_t)(g_hd64180_state.tmdr[0] & 0xFFu);
    case 0x0D: return (uint8_t)(g_hd64180_state.tmdr[0] >> 8);
    case 0x0E: return (uint8_t)(g_hd64180_state.rldr[0] & 0xFFu);
    case 0x0F: return (uint8_t)(g_hd64180_state.rldr[0] >> 8);
    case 0x10: return g_hd64180_state.tcr;
    case 0x14: return (uint8_t)(g_hd64180_state.tmdr[1] & 0xFFu);
    case 0x15: return (uint8_t)(g_hd64180_state.tmdr[1] >> 8);
    case 0x16: return (uint8_t)(g_hd64180_state.rldr[1] & 0xFFu);
    case 0x17: return (uint8_t)(g_hd64180_state.rldr[1] >> 8);

    /* DMAC Channels 0 & 1 */
    case 0x20: return (uint8_t)(g_hd64180_state.dma_sar0 & 0xFFu);
    case 0x21: return (uint8_t)((g_hd64180_state.dma_sar0 >> 8) & 0xFFu);
    case 0x22: return (uint8_t)((g_hd64180_state.dma_sar0 >> 16) & 0x0Fu);
    case 0x23: return (uint8_t)(g_hd64180_state.dma_dar0 & 0xFFu);
    case 0x24: return (uint8_t)((g_hd64180_state.dma_dar0 >> 8) & 0xFFu);
    case 0x25: return (uint8_t)((g_hd64180_state.dma_dar0 >> 16) & 0x0Fu);
    case 0x26: return (uint8_t)(g_hd64180_state.dma_bcr0 & 0xFFu);
    case 0x27: return (uint8_t)(g_hd64180_state.dma_bcr0 >> 8);
    case 0x28: return (uint8_t)(g_hd64180_state.dma_mar1 & 0xFFu);
    case 0x29: return (uint8_t)((g_hd64180_state.dma_mar1 >> 8) & 0xFFu);
    case 0x2A: return (uint8_t)((g_hd64180_state.dma_mar1 >> 16) & 0x0Fu);
    case 0x2B: return (uint8_t)(g_hd64180_state.dma_iar1 & 0xFFu);
    case 0x2C: return (uint8_t)(g_hd64180_state.dma_iar1 >> 8);
    case 0x2E: return (uint8_t)(g_hd64180_state.dma_bcr1 & 0xFFu);
    case 0x2F: return (uint8_t)(g_hd64180_state.dma_bcr1 >> 8);
    case 0x30: return g_hd64180_state.dma_dstat;
    case 0x31: return g_hd64180_state.dma_dmode;
    case 0x32: return g_hd64180_state.dma_dcntl;

    /* MMU */
    case 0x38: return g_hd64180_state.cbr;
    case 0x39: return g_hd64180_state.bbr;
    case 0x3A: return g_hd64180_state.cbar;
    case 0x3F: return g_hd64180_state.icr;
    default:
        return g_hd64180_state.io_regs[reg];
    }
}

void hd64180_io_write(uint8_t port, uint8_t val)
{
    uint8_t base = (uint8_t)(g_hd64180_state.icr & 0xC0u);
    if ((port & 0xC0u) != base) return;
    uint8_t reg = (uint8_t)(port & 0x3Fu);
    g_hd64180_state.io_regs[reg] = val;

    switch (reg) {
    /* ASCI Channels 0 & 1 */
    case 0x00: g_hd64180_state.asci_cntla[0] = val; break;
    case 0x01: g_hd64180_state.asci_cntla[1] = val; break;
    case 0x02: g_hd64180_state.asci_cntlb[0] = val; break;
    case 0x03: g_hd64180_state.asci_cntlb[1] = val; break;
    case 0x04:
        g_hd64180_state.asci_stat[0] = (uint8_t)((g_hd64180_state.asci_stat[0] & ~0x39u) | (val & 0x39u));
        break;
    case 0x05:
        g_hd64180_state.asci_stat[1] = (uint8_t)((g_hd64180_state.asci_stat[1] & ~0x39u) | (val & 0x39u));
        break;
    case 0x06:
        g_hd64180_state.asci_tdr[0] = val;
        g_hd64180_state.asci_stat[0] |= 0x02u; /* TDRE remains set in emulation */
        break;
    case 0x07:
        g_hd64180_state.asci_tdr[1] = val;
        g_hd64180_state.asci_stat[1] |= 0x02u;
        break;

    /* CSIO */
    case 0x0A: g_hd64180_state.csio_cntr = val; break;
    case 0x0B:
        g_hd64180_state.csio_trdr = val;
        if (g_hd64180_state.csio_cntr & 0x10u) { /* TE enable */
            g_hd64180_state.csio_cntr |= 0x80u;  /* set EF (End Flag) */
        }
        break;

    /* PRT Timer */
    case 0x0C: g_hd64180_state.tmdr[0] = (uint16_t)((g_hd64180_state.tmdr[0] & 0xFF00u) | val); break;
    case 0x0D: g_hd64180_state.tmdr[0] = (uint16_t)((g_hd64180_state.tmdr[0] & 0x00FFu) | ((uint16_t)val << 8)); break;
    case 0x0E: g_hd64180_state.rldr[0] = (uint16_t)((g_hd64180_state.rldr[0] & 0xFF00u) | val); break;
    case 0x0F: g_hd64180_state.rldr[0] = (uint16_t)((g_hd64180_state.rldr[0] & 0x00FFu) | ((uint16_t)val << 8)); break;
    case 0x10:
        /* Writing 0 to TIF bits clears them; low nibble is control */
        g_hd64180_state.tcr = (uint8_t)((g_hd64180_state.tcr & (val | 0x0Fu)) | (val & 0x0Fu));
        break;
    case 0x14: g_hd64180_state.tmdr[1] = (uint16_t)((g_hd64180_state.tmdr[1] & 0xFF00u) | val); break;
    case 0x15: g_hd64180_state.tmdr[1] = (uint16_t)((g_hd64180_state.tmdr[1] & 0x00FFu) | ((uint16_t)val << 8)); break;
    case 0x16: g_hd64180_state.rldr[1] = (uint16_t)((g_hd64180_state.rldr[1] & 0xFF00u) | val); break;
    case 0x17: g_hd64180_state.rldr[1] = (uint16_t)((g_hd64180_state.rldr[1] & 0x00FFu) | ((uint16_t)val << 8)); break;

    /* DMAC Channels 0 & 1 */
    case 0x20: g_hd64180_state.dma_sar0 = (g_hd64180_state.dma_sar0 & 0xFFF00u) | val; break;
    case 0x21: g_hd64180_state.dma_sar0 = (g_hd64180_state.dma_sar0 & 0xF00FFu) | ((uint32_t)val << 8); break;
    case 0x22: g_hd64180_state.dma_sar0 = (g_hd64180_state.dma_sar0 & 0x0FFFFu) | (((uint32_t)val & 0x0Fu) << 16); break;
    case 0x23: g_hd64180_state.dma_dar0 = (g_hd64180_state.dma_dar0 & 0xFFF00u) | val; break;
    case 0x24: g_hd64180_state.dma_dar0 = (g_hd64180_state.dma_dar0 & 0xF00FFu) | ((uint32_t)val << 8); break;
    case 0x25: g_hd64180_state.dma_dar0 = (g_hd64180_state.dma_dar0 & 0x0FFFFu) | (((uint32_t)val & 0x0Fu) << 16); break;
    case 0x26: g_hd64180_state.dma_bcr0 = (uint16_t)((g_hd64180_state.dma_bcr0 & 0xFF00u) | val); break;
    case 0x27: g_hd64180_state.dma_bcr0 = (uint16_t)((g_hd64180_state.dma_bcr0 & 0x00FFu) | ((uint16_t)val << 8)); break;
    case 0x28: g_hd64180_state.dma_mar1 = (g_hd64180_state.dma_mar1 & 0xFFF00u) | val; break;
    case 0x29: g_hd64180_state.dma_mar1 = (g_hd64180_state.dma_mar1 & 0xF00FFu) | ((uint32_t)val << 8); break;
    case 0x2A: g_hd64180_state.dma_mar1 = (g_hd64180_state.dma_mar1 & 0x0FFFFu) | (((uint32_t)val & 0x0Fu) << 16); break;
    case 0x2B: g_hd64180_state.dma_iar1 = (uint16_t)((g_hd64180_state.dma_iar1 & 0xFF00u) | val); break;
    case 0x2C: g_hd64180_state.dma_iar1 = (uint16_t)((g_hd64180_state.dma_iar1 & 0x00FFu) | ((uint16_t)val << 8)); break;
    case 0x2E: g_hd64180_state.dma_bcr1 = (uint16_t)((g_hd64180_state.dma_bcr1 & 0xFF00u) | val); break;
    case 0x2F: g_hd64180_state.dma_bcr1 = (uint16_t)((g_hd64180_state.dma_bcr1 & 0x00FFu) | ((uint16_t)val << 8)); break;
    case 0x30:
        /* DSTAT: bit 7=DE1, 6=DE0, 5=DWE1, 4=DWE0, 3=DIE1, 2=DIE0, 0=DME */
        if (val & 0x10u) g_hd64180_state.dma_dstat = (uint8_t)((g_hd64180_state.dma_dstat & ~0x40u) | (val & 0x40u));
        if (val & 0x20u) g_hd64180_state.dma_dstat = (uint8_t)((g_hd64180_state.dma_dstat & ~0x80u) | (val & 0x80u));
        g_hd64180_state.dma_dstat = (uint8_t)((g_hd64180_state.dma_dstat & ~0x0Du) | (val & 0x0Du));
        break;
    case 0x31: g_hd64180_state.dma_dmode = val; break;
    case 0x32: g_hd64180_state.dma_dcntl = val; break;

    /* MMU */
    case 0x38: g_hd64180_state.cbr = val; break;
    case 0x39: g_hd64180_state.bbr = val; break;
    case 0x3A: g_hd64180_state.cbar = val; break;
    case 0x3F: g_hd64180_state.icr = val; break;
    }
}

uint32_t hd64180_translate_address(uint16_t logical_addr)
{
    uint8_t page = (uint8_t)(logical_addr >> 12);
    uint8_t ba   = (uint8_t)(g_hd64180_state.cbar & 0x0Fu);
    uint8_t ca   = (uint8_t)(g_hd64180_state.cbar >> 4);

    if (page < ba) {
        /* Common Area 0: Physical = Logical */
        return (uint32_t)logical_addr;
    } else if (page < ca) {
        /* Bank Area: Physical = Logical + (BBR << 12) */
        uint32_t base = (uint32_t)g_hd64180_state.bbr << 12;
        return (base + logical_addr) & 0xFFFFFu;
    } else {
        /* Common Area 1: Physical = Logical + (CBR << 12) */
        uint32_t base = (uint32_t)g_hd64180_state.cbr << 12;
        return (base + logical_addr) & 0xFFFFFu;
    }
}

static inline uint8_t hd64180_in0(hd64180_t *cpu, uint8_t port)
{
    if (hd64180_is_internal_io(port)) {
        return hd64180_io_read(port);
    }
    return cpu->port_in(cpu, port);
}

static inline void hd64180_out0(hd64180_t *cpu, uint8_t port, uint8_t val)
{
    if (hd64180_is_internal_io(port)) {
        hd64180_io_write(port, val);
        return;
    }
    cpu->port_out(cpu, port, val);
}

static inline uint8_t rb(hd64180_t *cpu, uint16_t addr)
{
    return cpu->read_byte(cpu->userdata, addr);
}

static inline uint16_t get_hl(const hd64180_t *c) { return (uint16_t)(((uint16_t)c->h << 8) | c->l); }

static inline void set_bc(hd64180_t *c, uint16_t v) { c->b = (uint8_t)(v >> 8); c->c = (uint8_t)(v & 0xFF); }
static inline void set_de(hd64180_t *c, uint16_t v) { c->d = (uint8_t)(v >> 8); c->e = (uint8_t)(v & 0xFF); }
static inline void set_hl(hd64180_t *c, uint16_t v) { c->h = (uint8_t)(v >> 8); c->l = (uint8_t)(v & 0xFF); }

static inline int calc_parity(uint8_t val)
{
    uint8_t bits = (uint8_t)(val ^ (val >> 4));
    bits ^= (uint8_t)(bits >> 2);
    bits ^= (uint8_t)(bits >> 1);
    return !(bits & 1u);
}

static inline void exec_tst(hd64180_t *cpu, uint8_t val)
{
    uint8_t res = (uint8_t)(cpu->a & val);
    cpu->sf = (res & 0x80u) != 0;
    cpu->zf = (res == 0);
    cpu->hf = 1;
    cpu->pf = calc_parity(res);
    cpu->nf = 0;
    cpu->cf = 0;
    cpu->xf = (res & 0x08u) != 0;
    cpu->yf = (res & 0x20u) != 0;
}

static int hd64180_check_ext(hd64180_t *cpu)
{
    uint8_t op1 = rb(cpu, cpu->pc);
    uint8_t op2, port, val;
    uint16_t prod;

    if (op1 != 0xEDu) return 0;
    op2 = rb(cpu, (uint16_t)(cpu->pc + 1u));

    switch (op2) {
    /* ── 8-bit Multiplies (MLT) ─────────────────────────── */
    case 0x4C: /* MLT BC */
        prod = (uint16_t)((uint16_t)cpu->b * (uint16_t)cpu->c);
        set_bc(cpu, prod);
        cpu->pc = (uint16_t)(cpu->pc + 2u);
        cpu->cyc += 17;
        return 1;

    case 0x5C: /* MLT DE */
        prod = (uint16_t)((uint16_t)cpu->d * (uint16_t)cpu->e);
        set_de(cpu, prod);
        cpu->pc = (uint16_t)(cpu->pc + 2u);
        cpu->cyc += 17;
        return 1;

    case 0x6C: /* MLT HL */
        prod = (uint16_t)((uint16_t)cpu->h * (uint16_t)cpu->l);
        set_hl(cpu, prod);
        cpu->pc = (uint16_t)(cpu->pc + 2u);
        cpu->cyc += 17;
        return 1;

    case 0x7C: /* MLT SP */
        prod = (uint16_t)((uint16_t)(cpu->sp >> 8) * (uint16_t)(cpu->sp & 0xFFu));
        cpu->sp = prod;
        cpu->pc = (uint16_t)(cpu->pc + 2u);
        cpu->cyc += 17;
        return 1;

    /* ── Internal I/O Input (IN0 r, (m)) ────────────────── */
    case 0x00: /* IN0 B, (m) */
    case 0x08: /* IN0 C, (m) */
    case 0x10: /* IN0 D, (m) */
    case 0x18: /* IN0 E, (m) */
    case 0x20: /* IN0 H, (m) */
    case 0x28: /* IN0 L, (m) */
    case 0x30: /* IN0 (m) - test only */
    case 0x38: /* IN0 A, (m) */
        port = rb(cpu, (uint16_t)(cpu->pc + 2u));
        val = hd64180_in0(cpu, port);
        cpu->sf = (val & 0x80u) != 0;
        cpu->zf = (val == 0);
        cpu->hf = 0;
        cpu->pf = calc_parity(val);
        cpu->nf = 0;
        /* cf unchanged */
        cpu->xf = (val & 0x08u) != 0;
        cpu->yf = (val & 0x20u) != 0;
        switch (op2) {
        case 0x00: cpu->b = val; break;
        case 0x08: cpu->c = val; break;
        case 0x10: cpu->d = val; break;
        case 0x18: cpu->e = val; break;
        case 0x20: cpu->h = val; break;
        case 0x28: cpu->l = val; break;
        case 0x30: break; /* test only */
        case 0x38: cpu->a = val; break;
        }
        cpu->pc = (uint16_t)(cpu->pc + 3u);
        cpu->cyc += 12;
        return 1;

    /* ── Internal I/O Output (OUT0 (m), r) ──────────────── */
    case 0x01: /* OUT0 (m), B */
    case 0x09: /* OUT0 (m), C */
    case 0x11: /* OUT0 (m), D */
    case 0x19: /* OUT0 (m), E */
    case 0x21: /* OUT0 (m), H */
    case 0x29: /* OUT0 (m), L */
    case 0x39: /* OUT0 (m), A */
        port = rb(cpu, (uint16_t)(cpu->pc + 2u));
        switch (op2) {
        case 0x01: val = cpu->b; break;
        case 0x09: val = cpu->c; break;
        case 0x11: val = cpu->d; break;
        case 0x19: val = cpu->e; break;
        case 0x21: val = cpu->h; break;
        case 0x29: val = cpu->l; break;
        case 0x39: val = cpu->a; break;
        default:   val = 0; break;
        }
        hd64180_out0(cpu, port, val);
        cpu->pc = (uint16_t)(cpu->pc + 3u);
        cpu->cyc += 13;
        return 1;

    /* ── Register / Immediate Test (TST) ────────────────── */
    case 0x04: exec_tst(cpu, cpu->b); cpu->pc = (uint16_t)(cpu->pc + 2u); cpu->cyc += 7; return 1;
    case 0x0C: exec_tst(cpu, cpu->c); cpu->pc = (uint16_t)(cpu->pc + 2u); cpu->cyc += 7; return 1;
    case 0x14: exec_tst(cpu, cpu->d); cpu->pc = (uint16_t)(cpu->pc + 2u); cpu->cyc += 7; return 1;
    case 0x1C: exec_tst(cpu, cpu->e); cpu->pc = (uint16_t)(cpu->pc + 2u); cpu->cyc += 7; return 1;
    case 0x24: exec_tst(cpu, cpu->h); cpu->pc = (uint16_t)(cpu->pc + 2u); cpu->cyc += 7; return 1;
    case 0x2C: exec_tst(cpu, cpu->l); cpu->pc = (uint16_t)(cpu->pc + 2u); cpu->cyc += 7; return 1;
    case 0x3C: exec_tst(cpu, cpu->a); cpu->pc = (uint16_t)(cpu->pc + 2u); cpu->cyc += 7; return 1;

    case 0x34: /* TST (HL) */
        val = rb(cpu, get_hl(cpu));
        exec_tst(cpu, val);
        cpu->pc = (uint16_t)(cpu->pc + 2u);
        cpu->cyc += 13;
        return 1;

    case 0x64: /* TST imm */
        val = rb(cpu, (uint16_t)(cpu->pc + 2u));
        exec_tst(cpu, val);
        cpu->pc = (uint16_t)(cpu->pc + 3u);
        cpu->cyc += 9;
        return 1;

    case 0x74: /* TSTIO imm */
        val = hd64180_in0(cpu, cpu->c);
        port = rb(cpu, (uint16_t)(cpu->pc + 2u));
        exec_tst(cpu, (uint8_t)(val & port));
        cpu->pc = (uint16_t)(cpu->pc + 3u);
        cpu->cyc += 12;
        return 1;

    /* ── Sleep (SLP) ────────────────────────────────────── */
    case 0x76: /* SLP */
        cpu->halted = 1;
        cpu->pc = (uint16_t)(cpu->pc + 2u);
        cpu->cyc += 8;
        return 1;

    /* ── Block I/O ──────────────────────────────────────── */
    case 0x83: /* OOTIM: out0 (C), (HL); HL++; B-- */
        val = rb(cpu, get_hl(cpu));
        hd64180_out0(cpu, cpu->c, val);
        set_hl(cpu, (uint16_t)(get_hl(cpu) + 1u));
        cpu->b--;
        cpu->zf = (cpu->b == 0);
        cpu->nf = 1;
        cpu->pc = (uint16_t)(cpu->pc + 2u);
        cpu->cyc += 15;
        return 1;

    case 0x93: /* OOTIMR */
        val = rb(cpu, get_hl(cpu));
        hd64180_out0(cpu, cpu->c, val);
        set_hl(cpu, (uint16_t)(get_hl(cpu) + 1u));
        cpu->b--;
        if (cpu->b != 0) {
            cpu->cyc += 15;
            /* repeat */
        } else {
            cpu->zf = 1;
            cpu->nf = 1;
            cpu->pc = (uint16_t)(cpu->pc + 2u);
            cpu->cyc += 14;
        }
        return 1;

    case 0x8B: /* OTDM: out0 (C), (HL); HL--; B-- */
        val = rb(cpu, get_hl(cpu));
        hd64180_out0(cpu, cpu->c, val);
        set_hl(cpu, (uint16_t)(get_hl(cpu) - 1u));
        cpu->b--;
        cpu->zf = (cpu->b == 0);
        cpu->nf = 1;
        cpu->pc = (uint16_t)(cpu->pc + 2u);
        cpu->cyc += 15;
        return 1;

    case 0x9B: /* OTDMR */
        val = rb(cpu, get_hl(cpu));
        hd64180_out0(cpu, cpu->c, val);
        set_hl(cpu, (uint16_t)(get_hl(cpu) - 1u));
        cpu->b--;
        if (cpu->b != 0) {
            cpu->cyc += 15;
        } else {
            cpu->zf = 1;
            cpu->nf = 1;
            cpu->pc = (uint16_t)(cpu->pc + 2u);
            cpu->cyc += 14;
        }
        return 1;

    default:
        return 0;
    }
}

void hd64180_step(hd64180_t *cpu)
{
    if (!cpu) return;

    if (!hd64180_check_ext(cpu)) {
        z80_step(cpu);
    } else {
        z80_process_interrupts(cpu);
    }
}

void hd64180_advance(hd64180_t *cpu, uint32_t t_states)
{
    if (!cpu || t_states == 0) return;

    for (int ch = 0; ch < 2; ch++) {
        g_hd64180_state.timer_cycles[ch] += t_states;
        while (g_hd64180_state.timer_cycles[ch] >= 20u) {
            g_hd64180_state.timer_cycles[ch] -= 20u;
            if (g_hd64180_state.tmdr[ch] == 0) {
                g_hd64180_state.tmdr[ch] = g_hd64180_state.rldr[ch];
                /* Set TIF flag in TCR (bit 4 for ch0, bit 5 for ch1) */
                g_hd64180_state.tcr |= (uint8_t)(1u << (4 + ch));
                /* Check if interrupt enabled (bit 0 for ch0, bit 1 for ch1) */
                if ((g_hd64180_state.tcr & (1u << ch)) && cpu->iff1) {
                    z80_gen_int(cpu, 0x38u);
                }
            } else {
                g_hd64180_state.tmdr[ch]--;
            }
        }
    }
}

void hd64180_asci_rx(int ch, uint8_t byte)
{
    if (ch < 0 || ch > 1) return;
    if (!(g_hd64180_state.asci_cntla[ch] & 0x20u)) return; /* RE (Receiver Enable) */

    if (!(g_hd64180_state.asci_stat[ch] & 0x80u)) {
        /* RDR is empty */
        g_hd64180_state.asci_rdr[ch] = byte;
        g_hd64180_state.asci_stat[ch] |= 0x80u; /* RDRF = 1 */
    } else if (g_hd64180_state.asci_rx_count[ch] < 16) {
        /* Enqueue into FIFO */
        uint8_t head = g_hd64180_state.asci_rx_head[ch];
        g_hd64180_state.asci_rx_fifo[ch][head] = byte;
        g_hd64180_state.asci_rx_head[ch] = (uint8_t)((head + 1u) & 15u);
        g_hd64180_state.asci_rx_count[ch]++;
    } else {
        /* Overrun */
        g_hd64180_state.asci_stat[ch] |= 0x40u; /* OVRN = 1 */
    }
}

uint8_t hd64180_asci_get_tx(int ch)
{
    if (ch < 0 || ch > 1) return 0;
    return g_hd64180_state.asci_tdr[ch];
}

void hd64180_dma_exec(hd64180_t *cpu, int ch)
{
    if (!cpu) return;
    if (!(g_hd64180_state.dma_dstat & 0x01u)) return; /* DME = 0 */

    if (ch == 0) {
        if (!(g_hd64180_state.dma_dstat & 0x40u)) return; /* DE0 = 0 */

        uint32_t sar = g_hd64180_state.dma_sar0 & 0xFFFFFu;
        uint32_t dar = g_hd64180_state.dma_dar0 & 0xFFFFFu;
        uint32_t count = g_hd64180_state.dma_bcr0 ? (uint32_t)g_hd64180_state.dma_bcr0 : 65536u;
        uint8_t  sm = (uint8_t)((g_hd64180_state.dma_dmode >> 2) & 3u);
        uint8_t  dm = (uint8_t)((g_hd64180_state.dma_dmode >> 4) & 3u);

        while (count > 0) {
            uint8_t val;
            if (sm == 3u) {
                val = cpu->port_in ? cpu->port_in(cpu, (uint8_t)sar) : 0xFFu;
            } else {
                val = cpu->read_byte ? cpu->read_byte(cpu->userdata, (uint16_t)sar) : 0xFFu;
            }

            if (dm == 3u) {
                if (cpu->port_out) cpu->port_out(cpu, (uint8_t)dar, val);
            } else {
                if (cpu->write_byte) cpu->write_byte(cpu->userdata, (uint16_t)dar, val);
            }

            if (sm == 0u) sar = (sar + 1u) & 0xFFFFFu;
            else if (sm == 1u) sar = (sar - 1u) & 0xFFFFFu;

            if (dm == 0u) dar = (dar + 1u) & 0xFFFFFu;
            else if (dm == 1u) dar = (dar - 1u) & 0xFFFFFu;

            count--;
            cpu->cyc += 6;
        }

        g_hd64180_state.dma_sar0 = sar;
        g_hd64180_state.dma_dar0 = dar;
        g_hd64180_state.dma_bcr0 = 0;
        g_hd64180_state.dma_dstat &= ~0x40u; /* Clear DE0 */

        /* Interrupt on completion if DIE0 set */
        if ((g_hd64180_state.dma_dstat & 0x04u) && cpu->iff1) {
            z80_gen_int(cpu, 0x38u);
        }
    } else if (ch == 1) {
        if (!(g_hd64180_state.dma_dstat & 0x80u)) return; /* DE1 = 0 */

        uint32_t mar = g_hd64180_state.dma_mar1 & 0xFFFFFu;
        uint16_t iar = g_hd64180_state.dma_iar1;
        uint32_t count = g_hd64180_state.dma_bcr1 ? (uint32_t)g_hd64180_state.dma_bcr1 : 65536u;

        while (count > 0) {
            uint8_t val = cpu->read_byte ? cpu->read_byte(cpu->userdata, (uint16_t)mar) : 0xFFu;
            if (cpu->port_out) cpu->port_out(cpu, (uint8_t)iar, val);
            mar = (mar + 1u) & 0xFFFFFu;
            count--;
            cpu->cyc += 6;
        }

        g_hd64180_state.dma_mar1 = mar;
        g_hd64180_state.dma_bcr1 = 0;
        g_hd64180_state.dma_dstat &= ~0x80u; /* Clear DE1 */

        if ((g_hd64180_state.dma_dstat & 0x08u) && cpu->iff1) {
            z80_gen_int(cpu, 0x38u);
        }
    }
}

