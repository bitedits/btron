/*
 * ps2_iopram.c — Cleanroom EE->IOP RAM window for the PlayStation 2 port
 *
 * Measurement first: the window is only useful if the EE can both read and
 * write it while the IOP is running, and if the bytes we claim stay ours.
 * Nothing here guesses: every field of the result struct is a value that came
 * back off the bus.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "ps2_iopram.h"

/* EE wall clock helpers, implemented in boot_ps2.s */
extern uint32_t ps2_count_read(void);
extern void ps2_delay_cycles(uint32_t count);

#define EE_COUNTS_PER_US 147u

static int s_count_runs = -1;

static int iopram_count_running(void)
{
    if (s_count_runs < 0) {
        uint32_t a = ps2_count_read();
        for (volatile int i = 0; i < 8000; i++) { }
        s_count_runs = (ps2_count_read() != a) ? 1 : 0;
    }
    return s_count_runs;
}

static void iopram_wait_us(uint32_t us)
{
    if (!iopram_count_running()) {
        ps2_delay_cycles(us * 300u);
        return;
    }
    uint32_t start = ps2_count_read();
    uint32_t budget = us * EE_COUNTS_PER_US;
    for (uint32_t spins = 0; (uint32_t)(ps2_count_read() - start) < budget; spins++) {
        if (spins > 20000000u) break;
    }
}

volatile uint8_t *ps2_iopram_window(void)
{
    return (volatile uint8_t *)(uintptr_t)PS2_IOP_KSEG1;
}

volatile uint32_t *ps2_iopram_at(uint32_t off)
{
    return (volatile uint32_t *)(uintptr_t)(PS2_IOP_KSEG1 +
                                            (off & (PS2_IOP_RAM_BYTES - 1u)));
}

uint32_t ps2_iopram_read32(uint32_t off)
{
    return *ps2_iopram_at(off);
}

void ps2_iopram_write32(uint32_t off, uint32_t value)
{
    *ps2_iopram_at(off) = value;
}

#define SHADOW_WORDS (PS2_IOP_SHADOW_SIZE / 4u)
#define GUARD_WORDS  (SHADOW_WORDS - (PS2_IOP_GUARD_OFF / 4u))
/* Word i of the claimed block carries this tag plus its index, so a partial
 * overwrite names itself. */
#define CANARY_TAG   0xB70A0000u

static ps2_iopram_t s_iopram;
static uint32_t s_shadow_off = 0;
static uint32_t s_clobbered_at = 0xFFFFFFFFu;

static int block_is_zero(uint32_t off)
{
    volatile uint32_t *w = ps2_iopram_at(off);
    for (uint32_t i = 0; i < SHADOW_WORDS; i++) {
        if (w[i] != 0u) return 0;
    }
    return 1;
}

/* The IOP's exception-vector page holds jump instructions installed by its
 * kernel, so a window that decodes reads neither all-zero nor all-ones there.
 * That was the first guess at how to prove the window, and it is no good: one
 * unused vector slot reads zero, which zeroes the AND of the sample and fails a
 * window that is working perfectly.  Decoding is instead proven the only way
 * that cannot depend on what the IOP happens to have stored, by putting a word
 * through the window and reading it back. */
static int scratch_word_decodes(uint32_t off)
{
    volatile uint32_t *w = ps2_iopram_at(off);
    uint32_t was = *w;
    *w = ~was;
    int ok = (*w == ~was);
    *w = was;
    return ok;
}

static volatile uint32_t *guard_word(uint32_t i)
{
    return ps2_iopram_at(s_shadow_off + PS2_IOP_GUARD_OFF) + i;
}

/* What the fill pass leaves behind at guard word i. */
#define GUARD_CANARY(i) (CANARY_TAG + (PS2_IOP_GUARD_OFF / 4u) + (i))

static int shadow_fill_and_check(uint32_t off)
{
    volatile uint32_t *w = ps2_iopram_at(off);
    int ok = 1;
    for (uint32_t i = 0; i < SHADOW_WORDS; i++) w[i] = CANARY_TAG + i;
    __asm__ volatile("" : : : "memory");
    for (uint32_t i = 0; i < SHADOW_WORDS; i++) {
        if (w[i] != CANARY_TAG + i) ok = 0;
    }
    return ok;
}

int ps2_iopram_claim(ps2_iopram_t *out)
{
    ps2_iopram_t p;
    for (unsigned i = 0; i < sizeof(p); i++) ((uint8_t *)&p)[i] = 0;

    p.first_read = ps2_iopram_read32(0x80u);   /* IOP vector page, for the log */

#if PS2_IOP_SHADOW_OFF != 0u
    p.off = PS2_IOP_SHADOW_OFF;
#else
    /* Highest untouched 1 KB first: the IOP kernel keeps its own stacks and
     * data at the very top of the 2 MB, so stop 16 KB short of it and walk
     * down until a granule reads as never having been written. */
    p.off = 0;
    for (uint32_t cand = 0x1FC000u; cand >= 0x100000u; cand -= PS2_IOP_SHADOW_SIZE) {
        p.granules++;
        if (block_is_zero(cand)) { p.off = cand; break; }
        if (cand < PS2_IOP_SHADOW_SIZE) break;
    }
#endif

    s_shadow_off = p.off;
    if (p.off != 0) {
        /* Fill and settle-check only once one word has come back changed: a
         * window that does not decode should not be walked over at all. */
        p.readable = scratch_word_decodes(p.off);
        p.canary   = *guard_word(0);
        if (p.readable) {
            p.write_ok = shadow_fill_and_check(p.off);
            iopram_wait_us(20000);
            p.after_settle = *guard_word(0);
            p.survived = (p.after_settle == GUARD_CANARY(0));
        }
    }

    if (!(p.readable && p.write_ok && p.survived)) s_shadow_off = 0;
    s_iopram = p;
    s_clobbered_at = 0xFFFFFFFFu;
    if (out) *out = p;
    return (s_shadow_off != 0);
}

const ps2_iopram_t *ps2_iopram_last(void) { return &s_iopram; }

uint32_t ps2_iopram_shadow_off(void) { return s_shadow_off; }

/* Cheap spot check, called from the USB poll loop: a shadow the IOP took back
 * mid-session has to be reported rather than walked as descriptors. */
int ps2_iopram_shadow_intact(void)
{
    if (!s_shadow_off) return 0;
    for (uint32_t i = 0; i < GUARD_WORDS; i += 8u) {
        if (*guard_word(i) != GUARD_CANARY(i)) {
            if (s_clobbered_at == 0xFFFFFFFFu) s_clobbered_at = i;
            return 0;
        }
    }
    return 1;
}

/* Byte offset inside the claimed block of the first guard word that came back
 * wrong, or ~0 if the block never lied. */
uint32_t ps2_iopram_clobbered_at(void) { return s_clobbered_at; }
