/*
 * ps2_iopram.h — Cleanroom EE->IOP RAM window for the PlayStation 2 port
 *
 * The IOP-side OHCI controller is a bus master of IOP RAM and of nothing else,
 * so every descriptor it walks has to live there.  The Emotion Engine reaches
 * the same 2 MB through the SBUS window below, which is where the EE BIOS
 * kernel copies the IOP kernel image before the R3000A starts executing: the
 * emulator's own EE map says so (pcsx2/Memory.cpp memMapPhy() hands physical
 * 0x1C000000 for 8 MB to iop_memory, whose read/write handlers are
 * iopMemRead32/iopMemWrite32 of (mem & ~0x1C000000)), carrying the comment that
 * apps and games are "supposed" to use the thread-safe SIF instead.  We are
 * using the direct window to find out whether the borrow holds; the frame
 * boundary the OHCI writes is the thing that will tell us.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef PS2_IOPRAM_H
#define PS2_IOPRAM_H

#include <stdint.h>

/* EE physical 0x1C000000, uncached KSEG1 alias.  The window is 8 MB wide and
 * the RAM behind it is 2 MB, so offsets wrap every 2 MB. */
#define PS2_IOP_RAM_BYTES   0x00200000u
#define PS2_IOP_KSEG1       0xBC000000UL

/* Set to an IOP RAM byte offset to skip the search, or 0 to auto-claim. */
#ifndef PS2_IOP_SHADOW_OFF
#define PS2_IOP_SHADOW_OFF  0u
#endif

/* One block, laid out by the USB host engine: 256 B of HCCA, the endpoint and
 * transfer descriptors it walks, and the payload buffers the controller
 * masters in and out of them.  See ps2_usb.c for the offsets. */
#define PS2_IOP_SHADOW_SIZE 0x00000800u

/* The top eighth of the block is never written by the USB engine, so it can
 * answer the question "did the IOP take this RAM back while we were using it". */
#define PS2_IOP_GUARD_OFF   0x00000700u

typedef struct {
    uint32_t first_read;   /* window contents before we wrote anything */
    uint32_t canary;       /* what the first shadow word came back as */
    uint32_t after_settle; /* ...and what it was after the settle wait */
    uint32_t off;          /* claimed offset inside IOP RAM */
    uint32_t granules;     /* 1 KB granules inspected during the search */
    int      readable;     /* one word written through the window came back */
    int      write_ok;     /* pattern came back unchanged */
    int      survived;     /* pattern still there after the settle wait */
} ps2_iopram_t;

/* Raw accessors: byte offsets are inside IOP RAM, as the OHCI sees them. */
volatile uint8_t *ps2_iopram_window(void);
volatile uint32_t *ps2_iopram_at(uint32_t off);
uint32_t ps2_iopram_read32(uint32_t off);
void ps2_iopram_write32(uint32_t off, uint32_t value);

/* Find untouched IOP RAM, claim it, and prove the window works in both
 * directions.  Returns 1 when a shadow block was claimed and verified. */
int ps2_iopram_claim(ps2_iopram_t *out);
const ps2_iopram_t *ps2_iopram_last(void);
uint32_t ps2_iopram_shadow_off(void);   /* 0 until a claim succeeds */
int ps2_iopram_shadow_intact(void);     /* re-checks the sentinels */
uint32_t ps2_iopram_clobbered_at(void); /* byte offset of the first broken sentinel */

#endif /* PS2_IOPRAM_H */
