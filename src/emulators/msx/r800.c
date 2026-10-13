/*
 * r800.c — ASCII R800 CPU emulation for MSX turbo R.
 * Hardware emulation source for B-MSX. Clean-room C99.
 */

#include "r800.h"

void r800_init(r800_t *r)
{
    if (!r) return;
    z80_init(r);
}

static inline uint8_t rb(r800_t *r, uint16_t addr)
{
    return r->read_byte(r->userdata, addr);
}

static inline uint16_t get_bc(const r800_t *r) { return (uint16_t)(((uint16_t)r->b << 8) | r->c); }
static inline uint16_t get_de(const r800_t *r) { return (uint16_t)(((uint16_t)r->d << 8) | r->e); }
static inline uint16_t get_hl(const r800_t *r) { return (uint16_t)(((uint16_t)r->h << 8) | r->l); }

static inline void set_de(r800_t *r, uint16_t v)
{
    r->d = (uint8_t)(v >> 8);
    r->e = (uint8_t)(v & 0xFF);
}

static inline void set_hl(r800_t *r, uint16_t v)
{
    r->h = (uint8_t)(v >> 8);
    r->l = (uint8_t)(v & 0xFF);
}

static int r800_check_mul(r800_t *r)
{
    uint8_t op1 = rb(r, r->pc);
    uint8_t op2;
    uint32_t prod;
    uint16_t op_hl, op_ss;

    if (op1 != 0xEDu) return 0;
    op2 = rb(r, (uint16_t)(r->pc + 1u));

    switch (op2) {
    case 0xC1: /* MULUB A, B */
        prod = (uint32_t)r->a * (uint32_t)r->b;
        set_hl(r, (uint16_t)prod);
        r->zf = (prod == 0);
        r->cf = (prod > 0xFFu);
        r->sf = 0; r->pf = 0;
        r->pc = (uint16_t)(r->pc + 2u);
        r->cyc += 14;
        return 1;

    case 0xC9: /* MULUB A, C */
        prod = (uint32_t)r->a * (uint32_t)r->c;
        set_hl(r, (uint16_t)prod);
        r->zf = (prod == 0);
        r->cf = (prod > 0xFFu);
        r->sf = 0; r->pf = 0;
        r->pc = (uint16_t)(r->pc + 2u);
        r->cyc += 14;
        return 1;

    case 0xD1: /* MULUB A, D */
        prod = (uint32_t)r->a * (uint32_t)r->d;
        set_hl(r, (uint16_t)prod);
        r->zf = (prod == 0);
        r->cf = (prod > 0xFFu);
        r->sf = 0; r->pf = 0;
        r->pc = (uint16_t)(r->pc + 2u);
        r->cyc += 14;
        return 1;

    case 0xD9: /* MULUB A, E */
        prod = (uint32_t)r->a * (uint32_t)r->e;
        set_hl(r, (uint16_t)prod);
        r->zf = (prod == 0);
        r->cf = (prod > 0xFFu);
        r->sf = 0; r->pf = 0;
        r->pc = (uint16_t)(r->pc + 2u);
        r->cyc += 14;
        return 1;

    case 0xE1: /* MULUB A, H */
        prod = (uint32_t)r->a * (uint32_t)r->h;
        set_hl(r, (uint16_t)prod);
        r->zf = (prod == 0);
        r->cf = (prod > 0xFFu);
        r->sf = 0; r->pf = 0;
        r->pc = (uint16_t)(r->pc + 2u);
        r->cyc += 14;
        return 1;

    case 0xE9: /* MULUB A, L */
        prod = (uint32_t)r->a * (uint32_t)r->l;
        set_hl(r, (uint16_t)prod);
        r->zf = (prod == 0);
        r->cf = (prod > 0xFFu);
        r->sf = 0; r->pf = 0;
        r->pc = (uint16_t)(r->pc + 2u);
        r->cyc += 14;
        return 1;

    case 0xF1: /* MULUB A, A */
        prod = (uint32_t)r->a * (uint32_t)r->a;
        set_hl(r, (uint16_t)prod);
        r->zf = (prod == 0);
        r->cf = (prod > 0xFFu);
        r->sf = 0; r->pf = 0;
        r->pc = (uint16_t)(r->pc + 2u);
        r->cyc += 14;
        return 1;

    case 0xC3: /* MULUW HL, BC */
        op_hl = get_hl(r);
        op_ss = get_bc(r);
        prod = (uint32_t)op_hl * (uint32_t)op_ss;
        set_hl(r, (uint16_t)(prod & 0xFFFFu));
        set_de(r, (uint16_t)(prod >> 16));
        r->zf = (prod == 0);
        r->cf = (prod > 0xFFFFu);
        r->sf = 0; r->pf = 0;
        r->pc = (uint16_t)(r->pc + 2u);
        r->cyc += 36;
        return 1;

    case 0xD3: /* MULUW HL, DE */
        op_hl = get_hl(r);
        op_ss = get_de(r);
        prod = (uint32_t)op_hl * (uint32_t)op_ss;
        set_hl(r, (uint16_t)(prod & 0xFFFFu));
        set_de(r, (uint16_t)(prod >> 16));
        r->zf = (prod == 0);
        r->cf = (prod > 0xFFFFu);
        r->sf = 0; r->pf = 0;
        r->pc = (uint16_t)(r->pc + 2u);
        r->cyc += 36;
        return 1;

    case 0xE3: /* MULUW HL, HL */
        op_hl = get_hl(r);
        prod = (uint32_t)op_hl * (uint32_t)op_hl;
        set_hl(r, (uint16_t)(prod & 0xFFFFu));
        set_de(r, (uint16_t)(prod >> 16));
        r->zf = (prod == 0);
        r->cf = (prod > 0xFFFFu);
        r->sf = 0; r->pf = 0;
        r->pc = (uint16_t)(r->pc + 2u);
        r->cyc += 36;
        return 1;

    case 0xF3: /* MULUW HL, SP */
        op_hl = get_hl(r);
        op_ss = r->sp;
        prod = (uint32_t)op_hl * (uint32_t)op_ss;
        set_hl(r, (uint16_t)(prod & 0xFFFFu));
        set_de(r, (uint16_t)(prod >> 16));
        r->zf = (prod == 0);
        r->cf = (prod > 0xFFFFu);
        r->sf = 0; r->pf = 0;
        r->pc = (uint16_t)(r->pc + 2u);
        r->cyc += 36;
        return 1;

    default:
        return 0;
    }
}

void r800_step(r800_t *r)
{
    unsigned long before;
    if (!r) return;

    if (!r800_check_mul(r)) {
        before = r->cyc;
        z80_step(r);
        /* R800 executes at 7.16 MHz (2x faster than Z80) */
        r->cyc = before + (r->cyc - before);
    } else {
        z80_process_interrupts(r);
    }
}
