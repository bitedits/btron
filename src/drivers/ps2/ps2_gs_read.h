/*
 * ps2_gs_read.h — the words that ask the GS for the pixels back (Local -> Host)
 *
 * LANE 19 rung 2.  Rung 1 -- src/cores/core_ps2.c's `gsquad` verb -- puts two flat sprites on the
 * displayed page and asks a human which ones arrived, because the port could not otherwise tell
 * whether the GS rasterises at all.  That instrument has one unacceptable property: its reader is
 * the user, and a port whose whole subject is what ends up in VRAM cannot depend on someone looking
 * at a window.  PCSX2 2.8.2 has no headless frame-dump option on its command line (checked against
 * its own --help, not recalled), so the only way to census the GS's output without eyes is to make
 * the GS give it back -- which is what these words do.
 *
 * This file is the packet only.  It fills a caller's buffer and touches no register, so every word
 * is comparable with a hand-derived literal on a machine with no PS2 in it -- the ps2_gs_env.c and
 * ps2_gs_poster.c split, and the reason both of those have host gates.  The bus half is
 * ps2_gs_vram_read() in ps2_gs.c, which owns DMAC channel 1.
 *
 * Which channel, and why that is the interesting fact.  A GIF stream reaches the GS over three
 * paths -- PATH1 is the VU1's XGKICK, PATH2 is VIF1's DIRECT/DIRECTHL, PATH3 is DMAC channel 2 --
 * and all three are INPUTS (ps2tek, "Graphics Synthesizer Interface" / "VIF").  Nothing in the GIF
 * moves pixels the other way.  The download is a VIF1 operation: the transfer registers
 * (BITBLTBUF, TRXPOS, TRXREG) are captured from their AD writes however they arrive, TRXDIR with
 * INCD=1 arms a Local->Host request whose size is RRW*RRH*bpp>>7 quadwords (PCSX2
 * Gif_Unit.cpp::Gif_HandlerAD), and the data itself is pulled by DMAC channel 1 with the VIF FIFO
 * reversed (VIF1_STAT.FDR) and the GS bus turned around (GS BUSDIR = 1).  That is the whole recipe
 * of third_party/ps2sdk's `ee/debug/src/screenshot.c`, which is the hardware-tested spelling of it.
 * A channel-2 receive is not a substitute: ps2tek says CHCR.DIR is only effective for VIF1 and SIF2,
 * and PCSX2's channel-2 handler has no receive branch at all, so one would quietly move nothing.
 *
 * Therefore this builder's output is a VIF1 block: one quadword of VIF codes -- four 32-bit commands,
 * which in a caller's uint64_t array is two slots -- then the GIF packet they hand on.  The codes are
 * NOP, NOP, FLUSHA, DIRECT(6): FLUSHA and DIRECT in the same two slots ps2sdk's screenshot.c puts
 * them in (its four are NOP, MSKPATH3, FLUSHA, DIRECT(6)), with the mask slot left a NOP.
 * DIRECT's count is the packet's quadword count,
 * GS_READ_REQ_QWC, and that coupling is what the gate checks: a DIRECT that claims more quadwords than
 * follow it makes VIF1 wait for bytes its caller never wrote, and one that claims fewer leaves part of
 * the request undecoded, i.e. a BITBLTBUF the GS never saw.
 *
 * Why FLUSHA is in the list at all: ps2tek's VIF command table gives it "stalls VIF1 until the VU
 * has finished executing a microprogram, PATH1 and PATH2 are not active, and there is no pending
 * transfer request for PATH3".  That is the condition the receipt depends on -- CSR bit 1 fires at
 * the end of this packet's service, so it means "the GS has taken my transfer registers" only if the
 * paths were clear when the packet went in.  MSKPATH3 is deliberately NOT here, which is the same
 * ps2sdk recipe with one word changed: its block masks PATH3 (0x8000 in the second code slot, which
 * the block above leaves a NOP) and lifts that mask afterwards by writing MSKPATH3(0) straight into
 * VIF1's FIFO register at 0x10005000 -- a cheap enough pair, but a mask the port would have to
 * remember to undo.  The mask exists so a PATH3 stream can queue while VIF1 owns the FIFO, and this
 * port's read runs when nothing else does (ps2_gs_vram_read() refuses while the GIF ring has a
 * transfer queued or in flight), so it has nothing to queue and takes the precondition instead.  A
 * lingering mask is the worse failure: it holds channel 2, and the console draws through channel 2.
 *
 * The five registers, in ps2sdk's order, and what the order buys: BITBLTBUF names the source page,
 * its width in 64-pixel units and its format; TRXPOS names the top-left of the area; TRXREG names
 * its size; FINISH (GP 0x61, its own data word 0) arms the receipt; TRXDIR (INCD=1) starts the
 * request.  FINISH sits BEFORE the directory write, as in the tested recipe, and the receipt fires
 * when the packet has been served -- so waiting on it means the transfer registers are installed and
 * the download size is known, which is exactly what the EE has to know before it reverses the FIFO.
 *
 * Alignment is a law, not a preference.  A Local->Host transfer carries SPL/SPI = 0 -- "take whole
 * quadwords from the start of each row" -- and PCSX2's own comment on the size formula says
 * "games must take care to ensure transfer rectangles are exact multiples of a qword".  So the
 * builder refuses a rectangle whose first pixel is not quadword-aligned, or whose row is not a whole
 * number of quadwords, rather than producing a transfer whose length the hardware rounds DOWN: a
 * silently-short download leaves the caller's tail bytes holding the previous read, and a census
 * that mixes two screens is worse than no census.  The two rules and the register widths are
 * gs_read_request_packet()'s refusals; `gs_read_last_err()` says which one fired.
 *
 * Zero proprietary Sony SDK code: the field tables are in ps2_gs_pack.h, the VIF code numbers are
 * ps2tek's command table, and the download mechanism is cross-read against PCSX2's VIF1/GIF handlers.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef PS2_GS_READ_H
#define PS2_GS_READ_H

#include <stdint.h>

/* Register writes in the request: the three transfer words, the receipt, the directory.  Each is one
 * AD quadword and the tag is one more, so GS_READ_REQ_QWC is 1 + NREG and not 2 + NREG: a GIF tag is
 * a SINGLE quadword -- its own fields in the low 64 bits, the REGS nibble list in the high 64 -- which
 * is the same account gs_poster_qwc() and gs_draw_env_qwc() make, and the reason every QWC in this
 * port is quoted in 16-byte quadwords while a quadword is two uint64_t slots in a caller's buffer. */
#define GS_READ_REQ_NREG   5u
#define GS_READ_REQ_QWC    (1u + GS_READ_REQ_NREG)

/* What DMAC channel 1 takes for the request: the code quadword plus the packet it hands to the GIF.
 * ps2sdk's screenshot sets D1_QWC to 7 for exactly this block, and the equality is not a coincidence
 * -- it is DIRECT's count plus the tag. */
#define GS_READ_BLOCK_QWC  (1u + GS_READ_REQ_QWC)
#define GS_READ_BLOCK_U64  (2u * GS_READ_BLOCK_QWC)

/* The register widths, as limits, so the refusal says "the field cannot hold it" instead of
 * wrapping: RRW/RRH are 12 bits of pixels, SASX/SASY 11 bits. */
#define GS_READ_MAX_TRX    4095u
#define GS_READ_MAX_SS     2047u

/* The transfer length DMAC can be told about at all: CH_QWC is a 16-bit field, so a rectangle of
 * more than 65,535 quadwords is a request the packet describes correctly and the DMA performs
 * SHORTER -- the count wraps into the field and the caller's buffer ends up holding the first few
 * rows twice, once at the wrong offset.  This is why ps2sdk's screenshot.c walks the screen one row
 * per transfer: with Width=640 and CT32 a row is 160 quadwords, and a row never overflows.  A page
 * is therefore a caller's banding loop, not one packet, and the law is a refusal here rather than a
 * rule a caller has to remember. */
#define GS_READ_MAX_QWC    65535u

/* VIF1 code word: function in [30:24], immediate in [23:0].  DIRECT's count is read from the low
 * 16 bits by the hardware and means GIF quadwords (4 u32 each); NOP and FLUSHA take no immediate.
 * MSKPATH3's spelling is GS_VIF1_CODE(0x06u, mask) with bit 15 of the mask being the mask itself,
 * which is recorded here so the omission above is checkable rather than folklore. */
#define GS_VIF1_CODE(func, imm) (((uint32_t)(func) << 24) | ((uint32_t)(imm) & 0x00FFFFFFu))
#define GS_VIF1_NOP             GS_VIF1_CODE(0x00u, 0u)
#define GS_VIF1_MSKPATH3(mask)  GS_VIF1_CODE(0x06u, (mask))
#define GS_VIF1_FLUSHA          GS_VIF1_CODE(0x13u, 0u)
#define GS_VIF1_DIRECT(n)       GS_VIF1_CODE(0x50u, (n))

/* The read, in the units its fields take: a VRAM byte offset (turned into the 14-bit 256-byte page
 * index SBA wants), a source buffer width in 64-pixel units (SBW, the row pitch the transfer walks),
 * a pixel format, and the rectangle inside that buffer. */
typedef struct {
    uint32_t vram_byte;   /* source base: a whole 256-byte page, or the packet is refused */
    uint32_t sbw64;       /* source buffer width in units of 64 pixels */
    uint32_t psm;         /* GS_PSM_CT32 and friends -- the format whose bpp sizes the download */
    uint32_t x, y;        /* SASX/SASY: top-left of the area, in pixels of that buffer */
    uint32_t w, h;        /* RRW/RRH: the area's size, in pixels and lines */
} gs_read_req;

/* Which of gs_read_request_packet()'s laws a refusal fired on.  `0` means the last call built
 * something.  This exists because "there is no census" and "the census is black" are different
 * diagnoses, and a caller that cannot tell them apart will go looking in the wrong place. */
typedef enum {
    GS_READ_OK = 0,
    GS_READ_E_ARGS,     /* a null packet, a null request, or no room for one */
    GS_READ_E_PSM,      /* a format with no byte-per-pixel rule, i.e. not a raster this reads */
    GS_READ_E_SIZE,     /* a zero or over-12-bit rectangle */
    GS_READ_E_QWC,      /* an image longer than DMAC's 16-bit quadword count: band it instead */
    GS_READ_E_POS,      /* an origin past the 11-bit source field */
    GS_READ_E_ALIGN,    /* the area does not begin or end on a GIF quadword */
    GS_READ_E_PITCH,    /* the area reaches past the source buffer's own row */
    GS_READ_E_PAGE      /* the base is not a whole 256-byte page, so SBA cannot name it */
} gs_read_err;

/* Bytes-per-pixel of a PSM's low three bits: CT32 32, CT24 24, CT16 16, the 8-bit class 8, anything
 * else 0 -- which is the emulator's own table and the reason 0 means "refuse". */
uint32_t gs_read_bpp(uint32_t psm);

/* Quadwords the downloaded image occupies: RRW*RRH*bpp>>7, the same formula the GS uses for the
 * request's size, so the QWC a caller puts on channel 1 and the size the transfer has are one number
 * rather than two that must agree.  Returns 0 for a format gs_read_bpp() refuses. */
uint32_t gs_read_image_qwc(uint32_t w, uint32_t h, uint32_t psm);

/* Fill exactly GS_READ_REQ_QWC quadwords at `qw` with the five-write request for `r`: a PACKED group
 * of NLOOP 5, NREG 1, every nibble AD, EOP set because this packet is a DMA of its own.  Returns the
 * quadwords written (GS_READ_REQ_QWC) or -1, having left the buffer alone; -1 also names itself via
 * gs_read_last_err(). */
int32_t gs_read_request_packet(const gs_read_req *r, uint64_t *qw, uint32_t cap_qw);

/* Fill exactly GS_READ_BLOCK_QWC quadwords at `qw` with the whole VIF1 block: the code quadword
 * (NOP, FLUSHA, DIRECT(GS_READ_REQ_QWC), NOP) then gs_read_request_packet()'s five writes.  This is
 * what goes in the buffer channel 1's MADR points at for the request leg; the image leg's MADR points
 * at the caller's pixel buffer instead and takes gs_read_image_qwc() quadwords.  Same return law, and
 * a refusal leaves the code quadword unwritten as well as the packet. */
int32_t gs_read_block_packet(const gs_read_req *r, uint64_t *qw, uint32_t cap_qw);

/* The law the last gs_read_request_packet() / gs_read_block_packet() call fired on.  Cleared to
 * GS_READ_OK by every call, including a successful one. */
gs_read_err gs_read_last_err(void);

#endif /* PS2_GS_READ_H */
