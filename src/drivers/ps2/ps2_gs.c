/*
 * ps2_gs.c — Cleanroom Sony PlayStation 2 Graphics Synthesizer Implementation
 *
 * Implements hardware display controller initialization, privileged PCRTC registers,
 * and high-performance Host-to-Local GIF DMA VRAM blitting for PCSX2 and real PS2 hardware.
 *
 * Cleanroom implementation from the published standards: the OHCI 1.0, GIF and
 * EE DMAC register layouts were re-derived from the manuals and then confirmed
 * against what the running machine decodes.  Zero proprietary Sony SDK code.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "ps2_gs.h"

#include <btron/types.h>     /* BTRON_PIXEL_FB, the canvas's byte order */
#include <btron/dp.h>        /* btron_render_perf_us(), the ring's clock */
#include "ps2_gif_ring.h"    /* LANE 10: the ring whose hardware seam this file is */
#include "ps2_gs_env.h"      /* the environment packet, and apply's argument type */
#include "ps2_gs_pack.h"     /* the field encoders the blit path now builds its words with */
#include "ps2_gs_read.h"     /* LANE 19 rung 2: the pure Local->Host builder this file is the bus for */
#include "ps2_gs_vram.h"     /* gs_vram_fbw(), the 64-pixel width unit a read names in BITBLTBUF */
#include "ps2_gs_tex.h"      /* the texture-plane registry this file is the bus for */

/* The canvas the GS is fed from, and the one the desktop composites into.
 *
 * There is no framebuffer here.  Until now this driver owned a second
 * 800x600x4 array of its own and the present copied the desktop's canvas into it
 * one pixel at a time, exchanging red and blue on the way, because the GS's
 * CT32 pixels put R in memory byte 0 and BTRON's COLOR puts B there.  That copy
 * cost 49 ms of a 274 ms whole-canvas present and 1.92 MB of a 32 MB image, and
 * the exchange it performed is now done by BTRON_PIXEL_FB() at the moment each
 * colour is drawn instead (include/btron/types.h), so the canvas can be streamed
 * to the GIF as it lies.
 *
 * core_ps2.c installs its own desktop backbuffer as the very first act of
 * ps2_kernel_main(), before the SIO and GS are initialised, because the boot log
 * is written into the canvas the screen shows. */
static uint32_t *s_gs_canvas;

/* EE DMAC Hardware Registers.  Each channel block is CHCR at +0x00, MADR at
 * +0x10 and QWC at +0x20, and the GIF sits in channel 2; the global control and
 * status block is at 0x1000E000. */
#define D_CTRL      (*(volatile uint32_t *)0x1000E000)
#define D_STAT      (*(volatile uint32_t *)0x1000E010)
#define D2_CHCR     (*(volatile uint32_t *)0x1000A000)
#define D2_MADR     (*(volatile uint32_t *)0x1000A010)
#define D2_QWC      (*(volatile uint32_t *)0x1000A020)

/* DMAC channel 1 -- VIF1's channel, and the one that takes pixels back.  The block is the same shape
 * as channel 2's (CHCR at +0x00, MADR at +0x10, QWC at +0x20, a 0x1000 stride per channel), at the
 * literal addresses third_party/ps2sdk's ee/debug/src/screenshot.c writes.
 *
 * CHCR bit 0 is DIR and bit 8 is STR, and DIR means something on this channel that it does not mean
 * on channel 2: ps2tek's CHCR table restricts the direction bit to the VIF1 and SIF2 channels, which
 * is exactly why a GS download is a channel-1 operation.  0x101 is "memory -> VIF1" (the request
 * block); 0x100 is "VIF1's read FIFO -> memory" (the pixels). */
#define D1_CHCR     (*(volatile uint32_t *)0x10009000)
#define D1_MADR     (*(volatile uint32_t *)0x10009010)
#define D1_QWC      (*(volatile uint32_t *)0x10009020)
#define D1_STAT_MIF (1u << 1)         /* D_STAT's channel-1 flag, write-1-to-clear */

/* VIF1's status word.  FQC -- bits [28:24] -- is the quadword count still sitting in the VIF FIFO,
 * and FDR -- bit 23 -- is the flag that says the FIFO's contents are destined for main memory instead
 * of the GIF.  A download that does not wait for FQC to reach zero pulls pixels out of a FIFO its own
 * request is still in, which returns data and returns it shifted. */
#define VIF1_STAT           (*(volatile uint32_t *)0x10003C00)
#define VIF1_STAT_FQC_MASK  0x1F000000u
#define VIF1_STAT_FDR       (1u << 23)

/* CSR bit 1 is FINISH, and it is write-1-to-clear -- the same convention ps2_gs_init()'s reset write
 * and ps2_gs_vsync()'s VSYNC clear already use.  Its mask sits eight bits higher in IMR (that is why
 * screenshot.c ORs 0x0200 to hide the event), and the 0xFF00 ps2_gs_init() installs already covers
 * it: no GS interrupt reaches a handler in this port, so nothing but this poll can consume a receipt. */
#define GS_CSR_FINISH       (1ULL << 1)

/* The two bounds on the read's four waits.  A 7-quadword block and a few-hundred-quadword download
 * are microseconds on real hardware, so these are the port's "never wedge the console" bound rather
 * than a timeout anyone expects to hit: STR is given ps2_gs_vsync()'s own 200,000 iterations, and the
 * two waits that depend on the GS rather than on the DMAC -- the FINISH receipt and the FIFO drain --
 * get ten times that. */
#define GS_READ_SPIN_STR    200000u
#define GS_READ_SPIN_GS     2000000u

/* Helper to get uncached KSEG1 pointer to bypass EE L1 D-Cache */
#define UNCACHED(p) ((void *)((uintptr_t)(p) | 0x20000000UL))

/* The canvas, in the one alias everything that draws uses.
 *
 * The GIF is a bus master, and its stream and setup packets are read through the
 * uncached alias above.  That is only sound because the port never enables the
 * Emotion Engine's L1 caches: the one CP0 Config write in the image is
 * btron_fp_init()'s CU1 bit (src/drivers/ps2/ps2_builtins.c), and boot_ps2.s
 * touches no other coprocessor register -- which make test-ps2 asserts rather
 * than assumes, because a stale line here is a screen of noise with no cause. */
static inline uint32_t *ps2_fb(void)
{
    return s_gs_canvas;
}

/* Install the canvas the GS is fed from.  core_ps2.c calls this with its desktop
 * backbuffer, and the pixels it hands over are already in the GS's own byte
 * order -- which is the entire reason there is nothing left to convert between
 * this array and VRAM. */
void ps2_gs_set_framebuffer(uint32_t *canvas)
{
    if (canvas) s_gs_canvas = canvas;
}

/* GS Register Encoding Macros */
#define GS_SET_DISPLAY(dx, dy, magh, magv, dw, dh) \
    (((uint64_t)((dx) & 0xFFF) << 0)   | \
     ((uint64_t)((dy) & 0x7FF) << 12)  | \
     ((uint64_t)((magh) & 0x0F) << 23) | \
     ((uint64_t)((magv) & 0x07) << 27) | \
     ((uint64_t)((dw) & 0xFFF) << 32)  | \
     ((uint64_t)((dh) & 0x7FF) << 44))

static int s_current_mode = PS2_MODE_800X600;

int ps2_gs_get_width(void)  { return PS2_SCREEN_WIDTH; }
int ps2_gs_get_height(void) { return PS2_SCREEN_HEIGHT; }

/* Cleanroom EE BIOS Syscall Prototypes implemented in boot_ps2.s */
extern void ps2_set_gs_crt(int16_t interlace, int16_t pal_ntsc, int16_t field);
extern void ps2_gs_put_imr(uint32_t mask);
extern const uint8_t* get_glyph_bitmap(uint16_t code, int16_t *out_width, int16_t *out_height);
extern void* tkl_memmove(void *dst, const void *src, size_t n);

/* Transmit packet of quadwords to Graphics Interface (GIF) via DMAC Channel 2.
 *
 * Not static since increment 3: the drawing environment is a second producer on this same
 * pipe, and giving it a copy of the register dance would be two places to keep right about
 * STR polling.  The blit path above and ps2_gs_env.h's packet used to be its only callers; the
 * environment moved to the ring with LANE 10, so what still arrives here is the canvas stream --
 * the transfers whose buffer this port cannot promise will still be there in one DMA's time.
 *
 * Two things this function now does that it did not do before, both of them LANE 10's:
 *
 *   It hands the ring first.  A synchronous start writes CHCR while the ring may be holding
 *   descriptors that were posted earlier and have not started, and channel 2 takes one transfer at
 *   a time -- so a start here without the drain would put the canvas band in front of primitives
 *   the EE had already committed to the bus, and the layer's picture would draw on the wrong
 *   page of its own history.  reap() also leaves `in flight` false, which is what keeps the ring's
 *   one-flag model honest: a flag the ring set for a transfer this function started would be a
 *   lie, and the next drain() would wait for a transfer that never happened.
 *
 *   It counts its own spin.  The blocked time of a producer that blocks was, until now, the one
 *   number on this pipe nobody printed, and LANE 10's claim is not "no producer ever waits" but
 *   "every wait is measured and bounded".  `sync_sends`/`sync_us`/`sync_max_us` in [GSTS] gif:
 *   are the answer, and they are what says how much of VIOLATION 2 this increment left in.
 */
void ps2_gs_gif_send(const void *packet, uint32_t qwc)
{
    uint32_t t0;

    (void)ps2_gif_reap();

    t0 = ps2_gif_hw_us();
    ps2_gif_hw_start(packet, qwc);

    /* Wait for transfer to complete (STR bit clears when done) */
    while (ps2_gif_hw_busy())
        ;

    ps2_gif_note_sync(ps2_gif_hw_us() - t0);

    __asm__ volatile("" : : : "memory");
}

/* ── LANE 10: the ring's hardware seam ─────────────────────────────────────────────────
 * ps2_gif_ring.c is bookkeeping it can do on any machine, so the three facts it needs from the
 * bus are functions it calls rather than registers it reads.  This is the only place in the port
 * that answers them, which keeps DMAC channel 2 where the image gate can find it: test_ps2.sh
 * reads the MADR/QWC/D_STAT triple out of the disassembly and expects it in the channel-2
 * producers and nowhere else -- including not in the ring, whose purity is that row's other half.
 *
 * The address arithmetic is the same mask the sender has always used: MADR takes a physical
 * address, so the KSEG bits come off, and the packet a descriptor carries is the pointer its
 * producer was given.  Reading the words through a *cached* pointer is safe for one reason only,
 * and the port asserts it rather than trusting it -- the EE's L1 caches are never enabled, so
 * there is no stale line for the bus master to miss (see ps2_fb() above, and the CP0 row in
 * scripts/test_ps2.sh). */
int ps2_gif_hw_busy(void)
{
    return (D2_CHCR & (1 << 8)) != 0u;
}

void ps2_gif_hw_start(const void *packet, uint32_t qwc)
{
    /* Clear Channel 2 status bit in D_STAT, then name the buffer and its length, then start:
     * direction = 1 (to peripheral/GIF), STR = 1 (bit 8).  The barrier is between the address
     * pair and the start bit, because a reorder there starts a transfer of whatever MADR held
     * before this packet was written. */
    D_STAT = (1 << 2);
    D2_MADR = ((uint32_t)(uintptr_t)packet) & 0x1FFFFFFF;
    D2_QWC  = qwc;

    __asm__ volatile("" : : : "memory");

    D2_CHCR = (1 << 0) | (1 << 8);
}

/* Microseconds, from the same folded clock every stage column in [XMBT] and [PSTAT] is read
 * against.  Folded rather than raw because a spin here can exceed a CP0 Count wrap: an XMB frame
 * once did, which is why the row that measures it says `<=`. */
uint32_t ps2_gif_hw_us(void)
{
    return btron_render_perf_us();
}

/* One Host->Local setup packet (5 QWs) into an arbitrary VRAM block: where in VRAM, how big,
 * which way.  `dba` is in the 256-byte units BITBLTBUF.DBA counts, not the 8 KB units FRAME.FBA
 * does -- the two field units are ps2_gs_vram.h's, and a caller that mixes them is off by 32x.
 *
 * Moved onto the increment-5b encoders from written-out shifts.  The words are the same ones the
 * canvas has been uploaded with since before the encoders existed -- `make test-ps2-gs-pack`
 * compares the encoder output against these literals -- so the already-working path is the
 * instrument for the encoders rather than a second copy of their arithmetic. */
static void send_trx_setup_at(uint32_t dba, uint32_t dbw, int dx, int dy, int w, int h)
{
    static uint64_t setup_pkt[5 * 2] __attribute__((aligned(16)));
    uint64_t *p = (uint64_t *)UNCACHED(setup_pkt);
    const uint64_t regs[4] = { GS_GP_BITBLTBUF, GS_GP_TRXPOS, GS_GP_TRXREG, GS_GP_TRXDIR };
    uint32_t i;

    /* NLOOP 4, EOP, FLG=PACKED, NREG=1 with the single AD nibble, PRE clear because this group
     * draws nothing: the shape four register writes need and no more. */
    p[0] = gs_pack_giftag(4u, 1u, 0u, 0u, GS_FLG_PACKED, 1u);
    p[1] = GS_NIB_AD;

    for (i = 0; i < 4u; i++) {
        uint64_t word;

        switch (i) {
        case 0u: /* source half unused by a Host->Local stream; destination is the given block */
            word = gs_pack_bitbltbuf(0u, 0u, 0u, dba, dbw, GS_PSM_CT32);
            break;
        case 1u:
            word = gs_pack_trxpos(0u, 0u, (uint32_t)dx, (uint32_t)dy, 0u);
            break;
        case 2u:
            word = gs_pack_trxreg((uint32_t)w, (uint32_t)h);
            break;
        default:
            word = gs_pack_trxdir(0u);   /* Host -> Local */
            break;
        }
        p[2u + 2u * i] = word;
        p[3u + 2u * i] = regs[i];
    }

    ps2_gs_gif_send(setup_pkt, 5);
}

/* The canvas's own rectangle: page 0, the width field the screen rounds up to. */
static void send_trx_setup(int dx, int dy, int w, int h)
{
    send_trx_setup_at(0u, (PS2_SCREEN_WIDTH + 63u) / 64u, dx, dy, w, h);
}

/* Stream pixel data as IMAGE-format packets.  QWC counts quadwords, so a row of
 * w pixels is w/4 of them, and the quadword stream restarts at each chunk. */
static void send_image_stream(const void *src, uint32_t qwc)
{
    #define CHUNK_QWC 8000

    static uint64_t img_tag[2] __attribute__((aligned(16)));
    uint64_t *tag = (uint64_t *)UNCACHED(img_tag);
    const uint8_t *p = (const uint8_t *)UNCACHED(src);

    while (qwc > 0) {
        uint32_t n = (qwc > CHUNK_QWC) ? CHUNK_QWC : qwc;
        tag[0] = gs_pack_giftag(n, 1u, 0u, 0u, GS_FLG_IMAGE, 0u);
        tag[1] = 0ULL;
        ps2_gs_gif_send(img_tag, 1);
        ps2_gs_gif_send(p, n);
        p += (uint32_t)n * 16u;
        qwc -= n;
    }
}

void ps2_gs_upload(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) return;
    if (x < 0 || y < 0) return;
    if (x >= PS2_SCREEN_WIDTH || y >= PS2_SCREEN_HEIGHT) return;
    if (x + w > PS2_SCREEN_WIDTH)  w = PS2_SCREEN_WIDTH - x;
    if (y + h > PS2_SCREEN_HEIGHT) h = PS2_SCREEN_HEIGHT - y;

    /* CT32 needs an even RRW and the stream is whole quadwords (4 px) wide;
     * widen to both, which stays inside the 832-pixel VRAM page. */
    x &= ~3;
    w = (w + 3) & ~3;
    if (w > PS2_SCREEN_WIDTH) w = PS2_SCREEN_WIDTH;

    const uint8_t *canvas = (const uint8_t *)ps2_fb();

    if (x == 0 && w == PS2_SCREEN_WIDTH) {
        /* Full rows: the canvas rows are contiguous, so one rectangle and one
         * IMAGE stream cover the whole band. */
        send_trx_setup(0, y, w, h);
        send_image_stream(canvas + (size_t)y * PS2_SCREEN_PITCH,
                          (uint32_t)((size_t)w * (size_t)h / 4u));
    } else {
        /* Host->Local IMAGE data carries no row pitch, so a narrow rectangle
         * has to be uploaded a row at a time. */
        const uint8_t *src = canvas + (size_t)y * PS2_SCREEN_PITCH + (size_t)x * 4u;
        uint32_t qwc = (uint32_t)(w / 4);
        for (int row = 0; row < h; row++, src += PS2_SCREEN_PITCH) {
            send_trx_setup(x, y + row, w, 1);
            send_image_stream(src, qwc);
        }
    }
}

/* Stream entire 800x600 RDRAM framebuffer to GS VRAM via Host->Local GIF DMA */
void ps2_gs_flush(void)
{
    ps2_gs_upload(0, 0, PS2_SCREEN_WIDTH, PS2_SCREEN_HEIGHT);
}

/* ── LANE 19 rung 2: the Local->Host read ───────────────────────────────────────────────
 * The counterpart of ps2_gs_upload(), and the reason it exists: PCSX2 2.8.2 has no CLI flag that
 * dumps a frame, so the only way this port can be shown what the GS rasterised is for the guest to
 * ask the GS.  ps2_gs_read.c builds the words and refuses the rectangles those words cannot describe;
 * this function is the machine half -- three register dances on DMAC channel 1, with the receipts
 * between them.
 *
 * The order is ps2sdk's screenshot() and each gap in it is a failure mode:
 *
 *   Clear CSR FINISH *before* the block goes out, so the receipt below can only belong to this
 *   packet.  There is no interrupt handler that could race the poll (the IMR 0xFF00 installed at init
 *   masks the event), so the flag is this function's own to consume.
 *
 *   Wait for STR, then FINISH, then FQC.  STR says VIF1 has taken the block's bytes; FINISH says the
 *   GS has decoded the five AD words it carried and therefore knows the download's size; FQC says
 *   VIF1's FIFO is empty, which is the precondition for reversing it.  Reversing before FINISH takes
 *   the pixels of a request nobody has sized, and reversing before FQC reaches zero shifts the stream
 *   by however many quadwords were still queued.
 *
 *   Restore VIF1_STAT and BUSDIR on every exit, including the bounded-wait ones.  A read that returns
 *   with the bus reversed stops the desktop drawing forever, so the diagnostic would be the fault.
 *
 * Nothing here touches the canvas, the GIF ring, or channel 2 -- which is the whole reason the census
 * is honest: the page read is the one PCRTC is showing, and no part of the present path is consulted. */
static gs_read_bus_err s_read_bus_err;

gs_read_bus_err ps2_gs_vram_read_err(void)
{
    return s_read_bus_err;
}

int32_t ps2_gs_vram_read(int x, int y, int w, int h, uint32_t *dst)
{
    static uint64_t s_blk[GS_READ_BLOCK_U64] __attribute__((aligned(16)));
    gs_read_req req;
    uint64_t *blk = (uint64_t *)UNCACHED(s_blk);
    uint32_t iqwc;
    uint32_t i;

    s_read_bus_err = GS_READ_BUS_OK;

    if (dst == 0 || (((uintptr_t)(void *)dst) & 15u) != 0u) {
        /* Quadwords land on 16-byte boundaries; MADR cannot say "start mid-quadword". */
        s_read_bus_err = GS_READ_BUS_E_ARGS;
        return -1;
    }
    if (x < 0 || y < 0 || w <= 0 || h <= 0) {
        s_read_bus_err = GS_READ_BUS_E_ARGS;
        return -1;
    }

    /* The pipe has to be silent, and silent means both halves of it: the ring's queued and in-flight
     * descriptors (a DIRECT arriving behind a PATH3 stream of its own is the case ps2sdk masks PATH3
     * for, and this port takes the precondition instead of the mask -- see ps2_gs_read.h) and either
     * channel's start bit.  reap() gets the ring every chance to finish before it is refused. */
    (void)ps2_gif_reap();
    if (ps2_gif_hw_busy() || ps2_gif_pending() != 0u ||
        (D1_CHCR & (1u << 8)) != 0u || (D2_CHCR & (1u << 8)) != 0u) {
        s_read_bus_err = GS_READ_BUS_E_BUSY;
        return -1;
    }

    req.vram_byte = 0u;                          /* the displayed page: what the census is about */
    req.sbw64     = gs_vram_fbw(PS2_SCREEN_WIDTH);
    req.psm       = GS_PSM_CT32;
    req.x         = (uint32_t)x;
    req.y         = (uint32_t)y;
    req.w         = (uint32_t)w;
    req.h         = (uint32_t)h;

    iqwc = gs_read_image_qwc(req.w, req.h, req.psm);

    if (iqwc == 0u || gs_read_block_packet(&req, blk, GS_READ_BLOCK_QWC) < 0) {
        /* A rectangle too small to fill a quadword and one that breaks a field law are the same exit
         * here, and gs_read_last_err() is what tells them apart from anything on the bus. */
        s_read_bus_err = GS_READ_BUS_E_BUILD;
        return -1;
    }

    GS_REG_POKE(GS_CSR_OFFSET, GS_CSR_FINISH);   /* clear the receipt, arming it for this packet */
    D_STAT = D1_STAT_MIF;

    D1_MADR = ((uint32_t)(uintptr_t)s_blk) & 0x1FFFFFFFu;
    D1_QWC  = GS_READ_BLOCK_QWC;

    __asm__ volatile("" : : : "memory");

    D1_CHCR = 0x101u;                            /* DIR = 1 (memory -> VIF1), STR = 1 */

    for (i = 0; i < GS_READ_SPIN_STR; i++) {
        if ((D1_CHCR & (1u << 8)) == 0u) break;
    }
    if ((D1_CHCR & (1u << 8)) != 0u) {
        s_read_bus_err = GS_READ_BUS_E_STR;
        D1_CHCR = 0u;
        return -1;
    }

    for (i = 0; i < GS_READ_SPIN_GS; i++) {
        if ((GS_REG_PEEK(GS_CSR_OFFSET) & GS_CSR_FINISH) != 0ULL) break;
    }
    if ((GS_REG_PEEK(GS_CSR_OFFSET) & GS_CSR_FINISH) == 0ULL) {
        s_read_bus_err = GS_READ_BUS_E_FINISH;
        D1_CHCR = 0u;
        return -1;
    }
    GS_REG_POKE(GS_CSR_OFFSET, GS_CSR_FINISH);   /* consume it, so the next read starts clean */

    for (i = 0; i < GS_READ_SPIN_GS; i++) {
        if ((VIF1_STAT & VIF1_STAT_FQC_MASK) == 0u) break;
    }
    if ((VIF1_STAT & VIF1_STAT_FQC_MASK) != 0u) {
        s_read_bus_err = GS_READ_BUS_E_FIFO;
        D1_CHCR = 0u;
        return -1;
    }

    /* Both directions reversed: VIF1's FIFO feeds main memory, and the GS bus runs Local->Host.
     * Then the pixels, in a second channel-1 transfer whose length is the image's own quadword count
     * rather than the block's. */
    VIF1_STAT = VIF1_STAT_FDR;
    GS_REG_POKE(GS_BUSDIR_OFFSET, 1ULL);

    D1_MADR = ((uint32_t)(uintptr_t)dst) & 0x1FFFFFFFu;
    D1_QWC  = iqwc;

    __asm__ volatile("" : : : "memory");

    D1_CHCR = 0x100u;                            /* DIR = 0 (peripheral -> memory), STR = 1 */

    for (i = 0; i < GS_READ_SPIN_GS; i++) {
        if ((D1_CHCR & (1u << 8)) == 0u) break;
    }
    if ((D1_CHCR & (1u << 8)) != 0u) s_read_bus_err = GS_READ_BUS_E_DATA;

    /* Fall through to the restore on purpose: a short transfer is reported by the error code and by
     * the caller's own sentinel, and neither is worth a bus left pointing the wrong way. */
    D1_CHCR = 0u;
    VIF1_STAT = 0u;
    GS_REG_POKE(GS_BUSDIR_OFFSET, 0ULL);

    __asm__ volatile("" : : : "memory");

    return (s_read_bus_err == GS_READ_BUS_OK) ? (int32_t)iqwc : -1;
}

/* Cross one texture plane into VRAM (row 5b).
 *
 * The whole padded plane transfers, not the uploaded extent: `TEX0` describes the padded one, so
 * the padding has to be this plane's own transparent black rather than whoever's texels happen to
 * sit past RRW/RRH.  Nothing converts the pixels on the way, which is ps2_gs_tex.h's second law.
 *
 * One rectangle and one stream: a plane is contiguous in both memories, at the same row pitch
 * (`TBW` x 64 is the padded width, which is what the plane is), so unlike a narrow canvas band it
 * needs no per-row setup.
 *
 * The source is TinyGL's own `calloc`'d plane, and a DMAC quadword transfer needs it 16-byte
 * aligned: the PS2 heap returns every payload on a 16-byte boundary and checks that in the
 * translation unit that allocates it (src/cores/core_ps2.c's `ps2_heap_header_is_16`), which is
 * what lets these three lines stream a heap buffer instead of staging one. */
static void ps2_gs_tex_upload(const gs_tex_t *t)
{
    if (t == 0 || t->plane == 0 || t->qwc == 0u) return;

    send_trx_setup_at(t->tba, t->tbw, 0, 0, (int)t->pad_w, (int)t->pad_h);
    send_image_stream(t->plane, t->qwc);
}

/* The registry's sink: the plane whose bytes just came into existence crosses now, before any
 * primitive can be recorded that samples it, and its entry stops being `pending` the moment the
 * transfer has been taken.  Installed by ps2_gs_init() below, which is the only place in this
 * driver that knows channel 2 exists -- ps2_gs_tex.c plans words and never sends them. */
static void ps2_gs_tex_sink(uint32_t index)
{
    ps2_gs_tex_upload(gs_tex_at(index));
    gs_tex_uploaded(index);
}

void ps2_gs_set_mode(int mode)
{
    s_current_mode = mode;

    if (mode == PS2_MODE_800X600) {
        /* 720p progressive (SMODE1 CMOD=0, LC=22) carrying an unscaled
         * 800x600 window, instead of the VESA 800x600@60 mode word 0x2B.
         *
         * PCSX2 keys its entire CRT geometry off CMOD and LC, and gives every
         * VESA mode the one 640x480 table entry (GSState.cpp
         * VideoModeOffsets[2]), which GetResolution() then clamps the output
         * to.  So a hardware-correct VGA timing can only ever show 640 of our
         * 800 columns and 480 of our 600 rows there, and its DY=25 is read as
         * a -9 line offset because the VESA entry expects baseDY=34.  The
         * 720p entry is 1280x720 with baseDY=24, so the same canvas comes out
         * unclipped and unshifted.  On a real PS2 this paints the 800x600
         * image into the top-left of a 720p frame. */
        ps2_set_gs_crt(0, 0x52, 0);

        /* PMODE: Circuit 1 + 2 enabled, SLBG = 0 (Display Framebuffer) */
        GS_REG_POKE(GS_PMODE_OFFSET, 0xFF63ULL);

        /* SMODE2: Non-interlaced, progressive scan */
        GS_REG_POKE(GS_SMODE2_OFFSET, 0x00000000ULL);

        /* DISPFB1 & DISPFB2: FBP=0, FBW=13 (832/64), PSM=0 (CT32) */
        uint32_t fbw = (PS2_SCREEN_WIDTH + 63) / 64;
        uint64_t dispfb = (0ULL << 0) | ((uint64_t)fbw << 9) | ((uint64_t)GS_PSM_CT32 << 15);
        GS_REG_POKE(GS_DISPFB1_OFFSET, dispfb);
        GS_REG_POKE(GS_DISPFB2_OFFSET, dispfb);

        /* DISPLAY1 & DISPLAY2: the 720p raster's active-area origin, then
         * MAGH=MAGV=0 so the read is one VRAM pixel per output dot and
         * DW/DH stop at the 800x600 canvas rather than the full 1280x720.
         * The rest of the frame is filled with BGCOLOR.
         */
        uint64_t display = GS_SET_DISPLAY(420, 40, 0, 0, PS2_SCREEN_WIDTH - 1, PS2_SCREEN_HEIGHT - 1);
        GS_REG_POKE(GS_DISPLAY1_OFFSET, display);
        GS_REG_POKE(GS_DISPLAY2_OFFSET, display);

    } else if (mode == PS2_MODE_DTV_480P) {
        /* DTV 480p 640x480 Progressive:
         *   interlace = 0, pal_ntsc = 0x50 (480P), field = 0
         */
        ps2_set_gs_crt(0, 0x50, 0);
        GS_REG_POKE(GS_PMODE_OFFSET, 0xFF63ULL);
        GS_REG_POKE(GS_SMODE2_OFFSET, 0x00000000ULL);

        uint64_t dispfb = (0ULL << 0) | (10ULL << 9) | ((uint64_t)GS_PSM_CT32 << 15);
        GS_REG_POKE(GS_DISPFB1_OFFSET, dispfb);
        GS_REG_POKE(GS_DISPFB2_OFFSET, dispfb);

        uint64_t display = GS_SET_DISPLAY(232, 35, 1, 0, 1439, 479);
        GS_REG_POKE(GS_DISPLAY1_OFFSET, display);
        GS_REG_POKE(GS_DISPLAY2_OFFSET, display);

    } else {
        /* NTSC 640x448 Frame Mode:
         *   interlace = 1, pal_ntsc = 2 (NTSC), field = 1 (Frame Mode)
         */
        ps2_set_gs_crt(1, 2, 1);
        GS_REG_POKE(GS_PMODE_OFFSET, 0xFF63ULL);
        GS_REG_POKE(GS_SMODE2_OFFSET, 0x00000003ULL); /* INT=1, FFMD=1 */

        uint64_t dispfb = (0ULL << 0) | (10ULL << 9) | ((uint64_t)GS_PSM_CT32 << 15);
        GS_REG_POKE(GS_DISPFB1_OFFSET, dispfb);
        GS_REG_POKE(GS_DISPFB2_OFFSET, dispfb);

        uint64_t display = GS_SET_DISPLAY(636, 50, 3, 0, 2559, 447);
        GS_REG_POKE(GS_DISPLAY1_OFFSET, display);
        GS_REG_POKE(GS_DISPLAY2_OFFSET, display);
    }

    ps2_gs_set_bgcolor(24, 48, 80);
}

void ps2_gs_init(uint32_t width, uint32_t height)
{
    (void)width;
    (void)height;

    /* 1. Reset GS via CSR */
    GS_REG_POKE(GS_CSR_OFFSET, (1ULL << 9));
    for (volatile int i = 0; i < 1000; i++) {}

    /* 2. Unmask GS interrupts via BIOS syscall */
    ps2_gs_put_imr(0xFF00);

    /* 3. Configure PCRTC video mode (800x600 canvas on a 720p progressive timing) */
    ps2_gs_set_mode(PS2_MODE_800X600);

    /* 4. Enable EE DMAC Controller */
    D_CTRL |= 1; /* DMAE = 1 */
    D2_CHCR = 0;

    /* 4b. Initialize GS Primitive Mode Control & Color Clamp */
    {
        static uint64_t mode_pkt[3 * 2] __attribute__((aligned(16)));
        uint64_t *p = (uint64_t *)UNCACHED(mode_pkt);
        p[0] = gs_pack_giftag(2u, 1u, 0u, 0u, GS_FLG_PACKED, 1u);
        p[1] = GS_NIB_AD;
        p[2] = 1ULL; /* PRMODECONT: AC=1 (use PRIM register from GIFTAG) */
        p[3] = GS_GP_PRMODECONT;
        p[4] = 1ULL; /* COLCLAMP: enable clamping */
        p[5] = GS_GP_COLCLAMP;
        ps2_gs_gif_send(mode_pkt, 3);
    }

    /* 5. The texture-plane registry, hung on this channel: one cursor for the canvas reservation
     * and every plane, and a sink so a plane crosses the moment its bytes exist rather than at
     * some later present that would already have sampled it. */
    gs_tex_init();
    gs_tex_install_sink(ps2_gs_tex_sink);

    /* 6. Clear RDRAM framebuffer to deep console slate navy */
    uint32_t *fb = ps2_fb();
    for (int i = 0; i < PS2_SCREEN_WIDTH * PS2_SCREEN_HEIGHT; i++) {
        fb[i] = BTRON_PIXEL_FB(0xFF121B29u);
    }

    /* 7. Initial hardware flush to GS eDRAM */
    ps2_gs_flush();

    /* 8. Initialize text console cursor */
    ps2_gs_text_init();
}

void ps2_gs_set_bgcolor(uint8_t r, uint8_t g, uint8_t b)
{
    GS_REG_POKE(GS_BGCOLOR_OFFSET, ((uint64_t)r << 0) | ((uint64_t)g << 8) | ((uint64_t)b << 16));
}

/* ── Stage 1 Text Mode Terminal Console (100 cols x 37 rows) ───── */
#define TEXT_COLS 100
#define TEXT_ROWS 37

static int s_text_cursor_x = 0;
static int s_text_cursor_y = 0;

/* Rows of the canvas that no longer match VRAM.  Without this the console paid
 * a full 1,920,000-byte upload for every printed line, including the one that
 * echoes each keystroke. */
static int s_dirty_top = PS2_SCREEN_HEIGHT;
static int s_dirty_end = 0;

static void text_mark_dirty(int y, int nrows)
{
    if (y < s_dirty_top) s_dirty_top = y;
    if (y + nrows > s_dirty_end) s_dirty_end = y + nrows;
}

void ps2_gs_text_init(void)
{
    ps2_gs_text_clear();
}

void ps2_gs_text_clear(void)
{
    uint32_t *fb = ps2_fb();
    for (int i = 0; i < PS2_SCREEN_WIDTH * PS2_SCREEN_HEIGHT; i++) {
        fb[i] = BTRON_PIXEL_FB(0xFF121B29u); /* Deep Slate Navy Console */
    }
    s_text_cursor_x = 0;
    s_text_cursor_y = 0;
    text_mark_dirty(0, PS2_SCREEN_HEIGHT);
}

void ps2_gs_text_draw_char(int col, int row, char c, uint32_t fg, uint32_t bg)
{
    if (col < 0 || col >= TEXT_COLS || row < 0 || row >= TEXT_ROWS) return;

    int16_t gw = 8, gh = 16;
    const uint8_t *bmp = get_glyph_bitmap((uint16_t)(uint8_t)c, &gw, &gh);
    uint32_t *fb = ps2_fb();
    int base_x = col * 8;
    int base_y = row * 16;

    for (int y = 0; y < 16; y++) {
        uint8_t bits = (bmp && y < gh) ? bmp[y] : 0;
        int py = base_y + y;
        if (py >= PS2_SCREEN_HEIGHT) break;
        for (int x = 0; x < 8; x++) {
            int px = base_x + x;
            if (px >= PS2_SCREEN_WIDTH) break;
            fb[py * PS2_SCREEN_WIDTH + px] = BTRON_PIXEL_FB((bits & (0x80 >> x)) ? fg : bg);
        }
    }
    text_mark_dirty(base_y, 16);
}

static void ps2_gs_text_scroll(void)
{
    uint32_t *fb = ps2_fb();
    /* Scroll up by 1 text line (16 pixel rows) */
    tkl_memmove(fb, fb + (16 * PS2_SCREEN_WIDTH), (size_t)(PS2_SCREEN_HEIGHT - 16) * PS2_SCREEN_WIDTH * sizeof(uint32_t));
    /* Clear bottom 16 pixel rows */
    for (int i = (PS2_SCREEN_HEIGHT - 16) * PS2_SCREEN_WIDTH; i < PS2_SCREEN_WIDTH * PS2_SCREEN_HEIGHT; i++) {
        fb[i] = BTRON_PIXEL_FB(0xFF121B29u);
    }
    text_mark_dirty(0, PS2_SCREEN_HEIGHT);
}

void ps2_gs_text_putc(char c, uint32_t fg, uint32_t bg)
{
    if (c == '\r') {
        s_text_cursor_x = 0;
        return;
    }
    if (c == '\n') {
        s_text_cursor_x = 0;
        s_text_cursor_y++;
        if (s_text_cursor_y >= TEXT_ROWS) {
            ps2_gs_text_scroll();
            s_text_cursor_y = TEXT_ROWS - 1;
        }
        return;
    }
    if (c == '\b') {
        if (s_text_cursor_x > 0) {
            s_text_cursor_x--;
            ps2_gs_text_draw_char(s_text_cursor_x, s_text_cursor_y, ' ', fg, bg);
        }
        return;
    }
    if (c == '\t') {
        s_text_cursor_x = (s_text_cursor_x + 4) & ~3;
        if (s_text_cursor_x >= TEXT_COLS) {
            s_text_cursor_x = 0;
            s_text_cursor_y++;
            if (s_text_cursor_y >= TEXT_ROWS) {
                ps2_gs_text_scroll();
                s_text_cursor_y = TEXT_ROWS - 1;
            }
        }
        return;
    }

    ps2_gs_text_draw_char(s_text_cursor_x, s_text_cursor_y, c, fg, bg);
    s_text_cursor_x++;
    if (s_text_cursor_x >= TEXT_COLS) {
        s_text_cursor_x = 0;
        s_text_cursor_y++;
        if (s_text_cursor_y >= TEXT_ROWS) {
            ps2_gs_text_scroll();
            s_text_cursor_y = TEXT_ROWS - 1;
        }
    }
}

void ps2_gs_text_puts(const char *str, uint32_t fg)
{
    if (!str) return;
    while (*str) {
        ps2_gs_text_putc(*str++, fg, 0xFF121B29);
    }
}

void ps2_gs_text_flush(void)
{
    if (s_dirty_end <= s_dirty_top) return;
    int top = s_dirty_top;
    int h = s_dirty_end - top;
    if (h > PS2_SCREEN_HEIGHT) h = PS2_SCREEN_HEIGHT;
    ps2_gs_upload(0, top, PS2_SCREEN_WIDTH, h);
    s_dirty_top = PS2_SCREEN_HEIGHT;
    s_dirty_end = 0;
}


void ps2_gs_vsync(void)
{
    /* Clear GS CSR VSync interrupt (bit 3) */
    GS_REG_POKE(GS_CSR_OFFSET, (1ULL << 3));
    for (volatile int i = 0; i < 200000; i++) {
        if (GS_REG_PEEK(GS_CSR_OFFSET) & (1ULL << 3)) {
            break;
        }
    }
}

void ps2_gs_wait_vsync(void)
{
    ps2_gs_vsync();
}

void ps2_gs_swap_buffers(void)
{
    ps2_gs_wait_vsync();
    ps2_gs_flush();
}

uint32_t *ps2_gs_get_framebuffer(void)
{
    return ps2_fb();
}

void ps2_gs_flip(void)
{
    ps2_gs_wait_vsync();
    ps2_gs_flush();
}

/* Rasterize solid filled 2D rectangle directly into RDRAM backing store */
void ps2_gs_draw_rect(int x, int y, int w, int h, uint32_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > PS2_SCREEN_WIDTH)  w = PS2_SCREEN_WIDTH - x;
    if (y + h > PS2_SCREEN_HEIGHT) h = PS2_SCREEN_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    uint32_t *fb = ps2_fb();
    for (int j = y; j < y + h; j++) {
        uint32_t *row = &fb[j * PS2_SCREEN_WIDTH + x];
        for (int i = 0; i < w; i++) {
            row[i] = BTRON_PIXEL_FB(color);
        }
    }
}

void ps2_gs_fill_rect(int x, int y, int w, int h, uint32_t color)
{
    ps2_gs_draw_rect(x, y, w, h, color);
}

void ps2_gs_putpixel(int x, int y, uint32_t color)
{
    if (x >= 0 && x < PS2_SCREEN_WIDTH && y >= 0 && y < PS2_SCREEN_HEIGHT) {
        ps2_fb()[y * PS2_SCREEN_WIDTH + x] = BTRON_PIXEL_FB(color);
    }
}

/* Mouse cursor backing store for non-destructive cursor rendering */
static uint32_t cursor_saved[16 * 16];
static int cursor_saved_x = -1;
static int cursor_saved_y = -1;

void ps2_gs_erase_cursor(int x, int y)
{
    (void)x;
    (void)y;
    if (cursor_saved_x < 0 || cursor_saved_y < 0) return;

    uint32_t *fb = ps2_fb();
    for (int cy = 0; cy < 16; cy++) {
        int py = cursor_saved_y + cy;
        if (py >= PS2_SCREEN_HEIGHT) break;
        for (int cx = 0; cx < 16; cx++) {
            int px = cursor_saved_x + cx;
            if (px >= PS2_SCREEN_WIDTH) break;
            fb[py * PS2_SCREEN_WIDTH + px] = cursor_saved[cy * 16 + cx];
        }
    }
    cursor_saved_x = -1;
    cursor_saved_y = -1;
}

void ps2_gs_draw_cursor(int x, int y)
{
    if (cursor_saved_x >= 0) {
        ps2_gs_erase_cursor(0, 0);
    }
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= PS2_SCREEN_WIDTH - 16) x = PS2_SCREEN_WIDTH - 16;
    if (y >= PS2_SCREEN_HEIGHT - 16) y = PS2_SCREEN_HEIGHT - 16;

    uint32_t *fb = ps2_fb();
    for (int cy = 0; cy < 16; cy++) {
        int py = y + cy;
        if (py >= PS2_SCREEN_HEIGHT) break;
        for (int cx = 0; cx < 16; cx++) {
            int px = x + cx;
            if (px >= PS2_SCREEN_WIDTH) break;
            cursor_saved[cy * 16 + cx] = fb[py * PS2_SCREEN_WIDTH + px];
        }
    }
    cursor_saved_x = x;
    cursor_saved_y = y;

    static const uint16_t cursor_mask[16] = {
        0b1000000000000000, 0b1100000000000000, 0b1110000000000000, 0b1111000000000000,
        0b1111100000000000, 0b1111110000000000, 0b1111111000000000, 0b1111111100000000,
        0b1111111110000000, 0b1111110000000000, 0b1101111000000000, 0b1000111100000000,
        0b0000011110000000, 0b0000001111000000, 0b0000000111000000, 0b0000000011000000
    };
    static const uint16_t cursor_outline[16] = {
        0b1100000000000000, 0b1010000000000000, 0b1001000000000000, 0b1000100000000000,
        0b1000010000000000, 0b1000001000000000, 0b1000000100000000, 0b1000000010000000,
        0b1000000001000000, 0b1000011111100000, 0b1101010000000000, 0b0110101000000000,
        0b0000010100000000, 0b0000001010000000, 0b0000000101000000, 0b0000000011000000
    };

    for (int cy = 0; cy < 16; cy++) {
        uint16_t m = cursor_mask[cy];
        uint16_t o = cursor_outline[cy];
        int py = y + cy;
        if (py >= PS2_SCREEN_HEIGHT) break;
        for (int cx = 0; cx < 16; cx++) {
            int px = x + cx;
            if (px >= PS2_SCREEN_WIDTH) break;
            if ((o >> (15 - cx)) & 1) {
                fb[py * PS2_SCREEN_WIDTH + px] = BTRON_PIXEL_FB(0xFF000000u); /* Outline: Black */
            } else if ((m >> (15 - cx)) & 1) {
                fb[py * PS2_SCREEN_WIDTH + px] = BTRON_PIXEL_FB(0xFFFFFFFFu); /* Body: White */
            }
        }
    }
}

/* Program the GS drawing environment (doc/txt/PS2.txt ch. 16, increment 3).
 *
 * The packet's contents are ps2_gs_env.c's business and are gated word for word on the host;
 * this is the part that can only be done here, because channel 2 is.  There is deliberately no
 * ps2_gs_poke64() on this path: the GIF takes the whole environment in one DMA, in order, and
 * nine individual stores would each take a bus turn and none of them would be ordered against
 * the primitive stream that follows.  make test-ps2 reads that off the image -- tests 39 to 41
 * find the call into the builder, no GS poke, and the way out to DMAC channel 2.  Note that the
 * sender itself is not what the image gate looks for: with LANE 10 the way out is a post, which
 * is a call into another translation unit and therefore readable as a jal, where the register
 * writes it turns into are inlined here and gone from this function's range.
 *
 * POSTED, and the only producer that posts in this file -- src/gl/eegs_present.c's stage is the
 * other one, and it is reaped at that unit's begin().  `env_pkt` is a static, so the
 * packet this pass sends is the packet the next apply overwrites -- and the next apply happens
 * after src/gl/eegs_present.c's begin(), which is exactly where the ring is reaped.  That is the
 * buffer-lifetime law in ps2_gif_ring.h, and it is why the canvas stream below this function is
 * still synchronous: the canvas has a writer in every console character, and no reap is scheduled
 * between one and the next upload.
 *
 * A refusal is returned as -1, and the caller stops there.  Sending a layer's primitives under
 * the *previous* layer's clip is a picture with the wrong rectangle, which is worse than the
 * frame that never happened -- and unreachable by construction anyway, since the ring holds more
 * descriptors than the stage holds groups. */
int ps2_gs_draw_env_apply(const gs_draw_env *env)
{
    /* Tag pair plus one quadword per register, in quadword-aligned storage: the DMA reads
     * 16 bytes at a time and the GIF decodes the tag before it will take a data word. */
    static uint64_t env_pkt[(GS_ENV_NREG + 1u) * 2u] __attribute__((aligned(16)));
    uint64_t *p = (uint64_t *)UNCACHED(env_pkt);

    if (gs_draw_env_packet(env, p) != 0) return -1;

    if (ps2_gif_post(env_pkt, gs_draw_env_qwc()) != 0) return -1;
    return 0;
}

int ps2_gs_draw_dual_env_apply(const gs_draw_env *ctx1, const gs_draw_env *ctx2)
{
    static uint64_t dual_env_pkt[(GS_DUAL_ENV_NREG + 1u) * 2u] __attribute__((aligned(16)));
    uint64_t *p = (uint64_t *)UNCACHED(dual_env_pkt);

    if (gs_draw_dual_env_packet(ctx1, ctx2, p) != 0) return -1;

    if (ps2_gif_post(dual_env_pkt, gs_draw_dual_env_qwc()) != 0) return -1;
    return 0;
}

