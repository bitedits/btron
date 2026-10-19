/*
 * core_ps2.c — B-System BTRON3 3.20 RTOS Kernel for Sony PlayStation 2
 *
 * Full Authentic B-System Workbench Desktop Integration:
 *   • Target: PlayStation 2 Emotion Engine (R5900 MIPS-III Little-Endian)
 *   • Display: Graphics Synthesizer (GS) 800x600 @ 32-bpp RGBA via GIF DMA
 *   • Compositor: Real B-System Desktop (src/desktop/desktop.c, workbench.c, wnd.c)
 *   • Event Distribution: Full EVENTING.md Workbench Coordinator
 *   • Multi-Window Apps: Real Body Cabinet, T-Editor, GTerm Shell, Control Panel
 *   • Desktop Icons: Cabinet, Editor, Terminal, Sound, Chat Pictograms
 *   • Input: USB HID (OHCI probe), DualShock 2 decoder, SIO0 terminal
 *
 * Specification-derived cleanroom port: the OHCI, GIF and SIO0 register layouts
 * come from the published standards.  No vendor SDK code is imported.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#include <btron/async_rt.h>

#include <btron/types.h>
#include <btron/error.h>
#include <btron/itron.h>
#include <btron/dp.h>
#include <btron/dp_accel.h>
#include <btron/dp_cal.h>
#include <btron/fast_blit.h>    /* the bench's RAM-to-RAM rows call the same leaf the compositor does */
#include <btron/fused_row.h>    /* ... and, from `blit ship` on, the compositor's own row rule */
#include <btron/wnd.h>
#include <btron/desktop.h>
#include <btron/workbench.h>
#include <btron/global_menu.h>
#include <btron/tracker.h>
#include <btron/vobj.h>
#include <btron/tip.h>
#include <btron/event.h>
#include <btron/apps.h>
#include <btron/settings.h>
#include <btron/fs/vol_api.h>
#include <libstr.h>

#include "ps2_gs.h"
#include "ps2_gif_ring.h"
/* Task #165: the per-app paint policy.  Its own translation unit for the same reason LANE 10's ring
 * has one -- the claims are about states a live pass cannot conjure (a slice smaller than one
 * window's price, the same window asked twice in a pass, a job too expensive to ever be admitted),
 * and a policy that has only ever run inside a paint pass has not been shown to refuse anything.
 * The file declares no hardware and calls nothing; see its header for what it is for. */
#include "ps2_paint_worker.h"
#include "ps2_gs_tex.h"
#include "ps2_gs_env.h"     /* LANE 19 rung 1: the `gsquad` probe programs the draw environment */
#include "ps2_gs_vram.h"    /* ... and names the frame and depth pages in the units they take */
#include "ps2_gs_poster.h"  /* ... and sends its one SPRITE through LANE 9b's emitter */
#include "ps2_gs_pack.h"    /* LANE 19 rung 7: `gsalpha` programs the ALPHA register both candidate ways */
#include "ps2_gs_read.h"    /* LANE 19 rung 2: `gsread` reads the page back through the pure builder's error codes */
#include "ps2_sio.h"
#include "ps2_pad.h"
#include "ps2_fp.h"
#include "ps2_usb.h"
#include "ps2_iopram.h"
#include "ps2_irq.h"
#include "ps2_ptr_stroke.h"   /* the two-speed slide pair: which layer owns the pointer's distance */

/* The EE/GS present (doc/txt/PS2.txt, increment 5c): stage the frame's GIF stream during the
 * composite, send it after this region's canvas upload.  Relative path because that is how this
 * file reaches its sources -- src/drivers/ps2/ above is the same convention. */
#include "../gl/eegs_present.h"
/* The recorder's front end, included for LANE 19 rung 12a alone: the census draws its shading control
 * through `eegs_triangle()`, i.e. through the same group builder the XMB's stream is made of, and not
 * through the SPRITE poster that every earlier control used. */
#include "../gl/backend_eegs.h"
/* The build's own answer to "which rasteriser?", for the `[CFG]` row: this file installs no GL
 * context and must not pretend to know which one is live, so it prints the name of the backend
 * the Makefile's `PS2_GL=` dial selected, from the unit that holds that rule. */
#include "../gl/gl_backend_choice.h"
/* The [MSX] phase's own instruments: the volume reader the XMB Games band and
 * B-MSX both go through, and the machine they fill.  Relative paths, as above. */
#include "../clu/vfs.h"
#include <emulators/tronmsx.h>
/* <btron/libc_shim.h>, which vfs.h reaches through cluc.h, rewrites malloc/calloc/
 * free into the kernel names on a bare-metal target -- and this file is the one
 * that defines those names.  The reader's prototypes are wanted, the renamers are
 * not, so they come off again straight after the include. */
#undef malloc
#undef calloc
#undef free

extern void ps2_delay_cycles(uint32_t count);
extern void ps2_halt(void);
extern uint32_t ps2_count_read(void);

/* External B-System Desktop Hooks from src/desktop/desktop.c */
extern GDEV* init_baremetal_desktop(uint32_t *fb, uint32_t w, uint32_t h);
extern void  redraw_baremetal_desktop(GDEV *screen, H w, H h);
extern void  set_baremetal_mouse_pos(H x, H y);
extern void  get_baremetal_mouse_pos(H *x, H *y);
extern void  draw_baremetal_mouse_cursor(GDEV *screen, H mx, H my, H w, H h);

/* ── Kernel heap: a first-fit pool that gives memory back ───────────────
 *
 * This was a bump pointer whose Ifree() did nothing, which stayed harmless while
 * everything that allocated lived as long as the desktop.  The GL downstack broke
 * that: a window surface and its depth plane are allocated when an app opens and
 * freed when it closes, so the second open asks for bytes the first open never
 * returned -- and xmb's icon atlas failed for want of 576 kB in an 8 MB pool that
 * already held 5.8 MB.
 *
 * The pool needs no lock and must not get one: this kernel is single-stack and
 * cooperative (see the task layer near the end of this file), but an app body does
 * allocate while a desktop pass is in flight, so the free list has to tolerate
 * reentrant calls rather than assume a call finishes before another begins.
 *
 * Declared again here because the large-allocation rows below are the pool's own
 * diagnostics; the console driver itself is defined with the rest of this file. */
void ps2_kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* The pool's ceiling is not the RAM end, it is the stack's floor: boot_ps2.s sets $sp to
 * 0x01FF0000 and this array is .bss, so every kB the pool takes is a kB the deepest call chain
 * loses -- and ps2.ld's ASSERT (`_end <= 0x01FF0000`) only catches the case that runs off the end,
 * not the one that quietly overwrites the heap's own tail.  So the number below is arithmetic
 * against a measurement, not a guess:
 *
 *   RDRAM under the stack, ship image:  0x01FF0000 - _end(9 MB pool) = 2,478 kB
 *   stack high-water, measured 2026-10-17 by the probe below, through a full XMB session: 22 kB
 *
 * 11 MB therefore leaves 430 kB of stack -- 19x what the session used.  The demand side is the
 * same census's own argument: at 9 MB the pool held 8,800 kB of named owners (window GDEVs 4,201,
 * the Terminal's surface 2,631, ZB_open 750, the XMB's GL surface 576, the Editor 513) and could
 * not serve the icon atlas's 1,024 kB ask, so the XMB opened with no texture plane.
 *
 * The MSX build keeps a smaller pool because B-MSX's own statics (4 MiB-class cartridge buffer,
 * 256 KiB V9938 VRAM, S1990, Geo3D) are competing for the same 32 MB. */
#if defined(BTRON_PS2_MSX) && (BTRON_PS2_MSX)
/* 7500 KB leaves ~1.65 MB free hole at boot for GL window surfaces (XMB/MSX/Quake). */
#define PS2_HEAP_SIZE (7500 * 1024)
#else
#define PS2_HEAP_SIZE (11 * 1024 * 1024)
#endif
static uint8_t s_ps2_heap[PS2_HEAP_SIZE] __attribute__((aligned(64)));

#define PS2_HEAP_ALIGN  16u
#define PS2_HEAP_HDR    16u    /* the header's real size, asserted below */

typedef struct ps2_blk {
    struct ps2_blk *next;      /* address order, so neighbours can coalesce */
    size_t          size;      /* payload bytes */
    unsigned        used;
    /* The allocation site, not padding: an operator's console can say `used=9210 kB in
     * 9216 kB, largest free hole 2 kB` -- pool EXHAUSTED, not fragmented -- and still name
     * nobody, because the +rows are gated on PS2_HEAP_LOGLIM and a 16 kB ask that leaks 32 kB
     * a pass never prints.  The header is 16 bytes either way, so this costs no pool byte and
     * no instruction on the success path; it buys the holder census below. */
    unsigned        ra;
} ps2_blk;

/* Every payload starts at (block + PS2_HEAP_HDR), so the header has to be exactly
 * that wide or the 16-byte alignment of the payloads is a lie. */
typedef char ps2_heap_header_is_16[(sizeof(ps2_blk) == PS2_HEAP_HDR) ? 1 : -1];

static ps2_blk *s_heap_list;
static size_t   s_heap_used;
static size_t   s_heap_peak;

/* core_init.c's k_heap_usage() reads this on every target; on this one the pool
 * above is the whole heap, so the value is only a base it subtracts from. */
uint32_t heap_ptr = 0x00200000;
/* The row threshold is what keeps the log short: the pool's 5.8 MB baseline is made
 * of small blocks, and only an allocation of this size can be the one a GL window
 * cannot find room for. */
#define PS2_HEAP_LOGLIM (96u * 1024u)
/* The most blocks the pool can ever hold, which is what a list walk compares its
 * step count against to tell a long list from a cyclic one. */
#define PS2_HEAP_BLOCKS_MAX (PS2_HEAP_SIZE / (PS2_HEAP_HDR + PS2_HEAP_ALIGN))

static void ps2_heap_init(void)
{
    s_heap_list = (ps2_blk *)s_ps2_heap;
    s_heap_list->next = NULL;
    s_heap_list->size = PS2_HEAP_SIZE - PS2_HEAP_HDR;
    s_heap_list->used = 0;
    s_heap_used = 0;
    s_heap_peak = 0;
}

/* The biggest single hole, which is what an allocation fails against -- a pool with
 * 3 MB free in 4 kB fragments cannot serve a 2 MB depth plane. */
static size_t ps2_heap_largest_free(void)
{
    size_t largest = 0;
    for (const ps2_blk *f = s_heap_list; f; f = f->next)
        if (!f->used && f->size > largest) largest = f->size;
    return largest;
}

/* Walk the pool the way the allocator's own invariants require: blocks in address
 * order, none overlapping, no two adjacent ones free (coalescing must have merged
 * them), and their sizes plus their headers accounting for every byte of the array.
 * A free list that breaks is worse than a bump pointer that leaks -- it hands out
 * one block twice -- so the bench asks this after each app open rather than trust
 * that the run did not crash.  Returns NULL when the pool holds. */
#if BTRON_PS2_BENCH
static const char *ps2_heap_check(void)
{
    size_t covered = 0;
    const ps2_blk *prev = NULL;
    unsigned n = 0;

    if (!s_heap_list) return NULL;
    if ((const uint8_t *)s_heap_list != s_ps2_heap) return "first block is not the array";

    for (const ps2_blk *b = s_heap_list; b; prev = b, b = b->next) {
        const uint8_t *self = (const uint8_t *)b;

        if (++n > PS2_HEAP_BLOCKS_MAX) return "more blocks than the pool can hold (cycle)";
        /* Before the arithmetic below, whose bounds would wrap on a corrupt size. */
        if (b->size > PS2_HEAP_SIZE) return "block larger than the whole pool";
        if (self < s_ps2_heap || self + PS2_HEAP_HDR + b->size > s_ps2_heap + PS2_HEAP_SIZE)
            return "block outside the array";
        if (b->size % PS2_HEAP_ALIGN) return "block size not a multiple of the alignment";
        if (prev) {
            /* Equality is the normal case: a block begins where the one below it ends. */
            if (self < (const uint8_t *)prev + PS2_HEAP_HDR + prev->size) return "blocks overlap";
            if (!prev->used && !b->used) return "two adjacent free blocks never coalesced";
        }
        covered += PS2_HEAP_HDR + b->size;
    }
    if (covered != PS2_HEAP_SIZE) return "block sizes do not account for the pool";
    return NULL;
}

/* The list itself, for the row after a check that says something is wrong: where
 * each block starts, how big it is, and whether it is spoken for. */
static void ps2_heap_dump(void)
{
    unsigned n = 0;

    for (const ps2_blk *b = s_heap_list; b && n < 40u; b = b->next, n++)
        ps2_kprintf("[HEAP]  b%-2d 0x%x %u B %s\n", n, (unsigned int)(uintptr_t)b,
                    (unsigned int)b->size, b->used ? "used" : "free");
}
#endif

/* Stack high-water, because the pool's ceiling is the stack's floor.
 *
 * ps2.ld's ASSERT keeps `_end` below boot_ps2.s's $sp (0x01FF0000), and this pool is the bulk of
 * `_end`, so every kB the pool gains is a kB the deepest call chain loses.  "It linked" proves
 * nothing about that: an image can leave 400 kB of stack and still die in the XMB's own recursion.
 * So arm() paints the memory below the first C frame with a word nothing else writes, and depth()
 * reports how much of it came back dirty.  Then the pool question is arithmetic, not a guess. */
#define PS2_STACK_PROBE_BYTES (512u * 1024u)
#define PS2_STACK_PROBE_MARK  0xA55AA55Au
static uint32_t *s_stack_probe;       /* lowest painted word; NULL until arm() runs */

static void ps2_stack_probe_arm(void)
{
    uintptr_t frame = (uintptr_t)__builtin_frame_address(0) & ~(uintptr_t)(PS2_HEAP_ALIGN - 1u);
    uintptr_t lo;
    uint32_t *w;

    if (frame < PS2_STACK_PROBE_BYTES + 0x10000u) return;   /* nowhere sane to paint: give up quietly */
    lo = frame - PS2_STACK_PROBE_BYTES;
    s_stack_probe = (uint32_t *)(void *)lo;
    for (w = s_stack_probe; (uintptr_t)w < frame; w++) *w = PS2_STACK_PROBE_MARK;
}

/* kB of the probed region a call has written since arm(); 0 when never armed or never touched.
 * Saturating at PS2_STACK_PROBE_BYTES is the row that says "widen the probe", not "the stack fits"
 * -- so the caller prints the probe size next to it. */
static unsigned ps2_stack_probe_depth(void)
{
    uint32_t * const end = s_stack_probe ? s_stack_probe + (PS2_STACK_PROBE_BYTES / 4u) : NULL;
    uint32_t *w;

    if (!s_stack_probe) return 0u;
    for (w = s_stack_probe; w < end; w++) {
        if (*w != PS2_STACK_PROBE_MARK)
            return (unsigned)((PS2_STACK_PROBE_BYTES - (uintptr_t)((char *)w - (char *)s_stack_probe)) / 1024u);
    }
    return 0u;
}

/* Who holds the pool, bucketed by the site that asked for it.  On 2026-10-17 the operator's own
 * `make run-ps2 PS2_GL=eegs PS2_FP=hw PS2_MSX=0` console reads `used=9210 kB in 9216 kB, largest
 * free hole 2 kB` and then a 16 kB ask failing twice per present pass, every ~55 ms: the pool is
 * EXHAUSTED, not fragmented, and the +rows cannot name it, because PS2_HEAP_LOGLIM is 96 kB so a
 * 16 kB leak stays silent until the pool is gone.  One row per holder answers, and the address is
 * resolved offline (`scripts/ps2_heap_who.sh`) -- no symbol table has to live on the box. */
#define PS2_HEAP_HOLDER_SLOTS 24
static unsigned s_heap_ooms;          /* failed asks since boot */
static unsigned s_heap_oom_site;      /* the last one's caller */
static size_t   s_heap_oom_size;
static size_t   s_heap_census_mark;   /* used at the last census: what the re-arm compares against */

static void ps2_heap_holders(const char *why)
{
    unsigned ras[PS2_HEAP_HOLDER_SLOTS];
    size_t   tot[PS2_HEAP_HOLDER_SLOTS];
    size_t   big[PS2_HEAP_HOLDER_SLOTS];
    unsigned cnt[PS2_HEAP_HOLDER_SLOTS];
    int n = 0;
    /* The overflow is its OWN row.  The first version folded the tail into slot 0 and cleared that
     * slot's address, which printed `0x0 holds 4082 kB in 14 block(s)`: one number that is
     * anonymous, unresolvable, and smaller than the truth it swallowed. */
    size_t rest_tot = 0, rest_big = 0;
    unsigned rest_cnt = 0;

    s_heap_census_mark = s_heap_used;

    for (const ps2_blk *b = s_heap_list; b; b = b->next) {
        int i, hit = -1;
        if (!b->used) continue;
        for (i = 0; i < n; i++) if (ras[i] == b->ra) { hit = i; break; }
        if (hit < 0 && n < PS2_HEAP_HOLDER_SLOTS) {
            hit = n++;
            ras[hit] = b->ra; tot[hit] = 0; cnt[hit] = 0; big[hit] = 0;
        }
        if (hit < 0) {
            rest_tot += b->size; rest_cnt++;
            if (b->size > rest_big) rest_big = b->size;
            continue;
        }
        tot[hit] += b->size; cnt[hit]++;
        if (b->size > big[hit]) big[hit] = b->size;
    }

    ps2_kprintf("[HEAP] holder census %s: %d site(s), used=%u kB of %u kB, failed asks=%u, "
                "stack used=%u kB of %u kB probed\n", why, n,
                (unsigned int)(s_heap_used / 1024u), (unsigned int)(PS2_HEAP_SIZE / 1024u),
                s_heap_ooms, ps2_stack_probe_depth(), (unsigned int)(PS2_STACK_PROBE_BYTES / 1024u));
    if (s_heap_ooms) {
        ps2_kprintf("[HEAP]   last failed ask: %u kB from 0x%x\n",
                    (unsigned int)(s_heap_oom_size / 1024u), s_heap_oom_site);
    }
    /* Biggest first, by selection over a couple of dozen slots -- a census a human reads.  The
     * biggest single block is on the row because that is the one a contiguous GL plane needs. */
    for (int pass = 0; pass < n; pass++) {
        int best = -1;
        for (int i = 0; i < n; i++) {
            if (tot[i] == 0u) continue;
            if (best < 0 || tot[i] > tot[best]) best = i;
        }
        if (best < 0) break;
        ps2_kprintf("[HEAP]   0x%x holds %u kB in %u block(s), biggest %u kB\n",
                    ras[best], (unsigned int)(tot[best] / 1024u), cnt[best],
                    (unsigned int)(big[best] / 1024u));
        tot[best] = 0u;
    }
    if (rest_cnt) {
        ps2_kprintf("[HEAP]   +%u block(s) of %u kB (biggest %u kB) at sites past the %d slots"
                    " -- raise PS2_HEAP_HOLDER_SLOTS to name them\n",
                    rest_cnt, (unsigned int)(rest_tot / 1024u), (unsigned int)(rest_big / 1024u),
                    PS2_HEAP_HOLDER_SLOTS);
    }
}

static int s_heap_census_done;

static void* ps2_heap_take(size_t size, const void *ra)
{
    const unsigned site = (unsigned)(uintptr_t)ra;

    if (size == 0) return NULL;
    size = (size + PS2_HEAP_ALIGN - 1u) & ~(size_t)(PS2_HEAP_ALIGN - 1u);
    if (!s_heap_list) ps2_heap_init();

    for (ps2_blk *b = s_heap_list; b; b = b->next) {
        if (b->used || b->size < size) continue;

        if (b->size >= size + PS2_HEAP_HDR + PS2_HEAP_ALIGN) {  /* split, keep the rest free */
            ps2_blk *tail = (ps2_blk *)(void *)((uint8_t *)b + PS2_HEAP_HDR + size);
            tail->next = b->next;
            tail->size = b->size - size - PS2_HEAP_HDR;
            tail->used = 0;
            b->next = tail;
            b->size = size;
        }
        b->used = 1;
        b->ra = site;
        s_heap_used += b->size;
        if (s_heap_used > s_heap_peak) s_heap_peak = s_heap_used;
        /* Re-arm once a tenth of the pool has come back since the last census.  The first version
         * re-armed below half the pool, which this build can never reach: its baseline alone is
         * 5.85 MB of a 9 MB pool, so the guard was on, permanently, and only the first fill of the
         * session was ever named. */
        if (s_heap_census_mark && s_heap_used + PS2_HEAP_SIZE / 10u <= s_heap_census_mark)
            s_heap_census_done = 0;

        if (b->size >= PS2_HEAP_LOGLIM) {
            ps2_kprintf("[HEAP] +%u kB -> 0x%x used=%u kB peak=%u kB largest_free=%u kB\n",
                        (unsigned int)(b->size / 1024u), (unsigned int)(uintptr_t)(b + 1),
                        (unsigned int)(s_heap_used / 1024u), (unsigned int)(s_heap_peak / 1024u),
                        (unsigned int)(ps2_heap_largest_free() / 1024u));
        }
        return (void *)(b + 1);
    }

    {
        const size_t hole = ps2_heap_largest_free();

        s_heap_ooms++;
        s_heap_oom_site = site;
        s_heap_oom_size = size;
        ps2_kprintf("[HEAP] out of memory asking %u kB from 0x%x: used=%u kB in %u kB, largest free hole %u kB\n",
                    (unsigned int)(size / 1024u), site,
                    (unsigned int)(s_heap_used / 1024u),
                    (unsigned int)(PS2_HEAP_SIZE / 1024u),
                    (unsigned int)(hole / 1024u));
        /* Two ways to be unable to serve an ask, and only one of them is "the pool is full": a
         * pool at half capacity with its largest hole under 64 kB cannot hand a GL surface its
         * plane either.  Both get the census, once. */
        if (!s_heap_census_done &&
            (s_heap_used >= (PS2_HEAP_SIZE / 4u) * 3u || hole < 64u * 1024u)) {
            s_heap_census_done = 1;
            ps2_heap_holders("at the first unservable ask");
        }
    }
    return NULL;
}

void* Imalloc(size_t size)
{
    return ps2_heap_take(size, __builtin_return_address(0));
}

void* Icalloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    /* The wrapper's own return address, so the stamped site is the caller of Icalloc and not
       this function -- the same reason malloc()/calloc() below do not call Imalloc(). */
    void *p = ps2_heap_take(total, __builtin_return_address(0));
    if (p) tkl_memset(p, 0, total);
    return p;
}

void Ifree(void *ptr)
{
    if (!ptr) return;

    for (ps2_blk *b = s_heap_list, *prev = NULL; b; prev = b, b = b->next) {
        if ((void *)(b + 1) != ptr) continue;
        if (!b->used) break;                    /* already free: do not double-return */

        b->used = 0;
        s_heap_used -= b->size;
        if (b->size >= PS2_HEAP_LOGLIM) {
            ps2_kprintf("[HEAP] -%u kB at 0x%x returned, used=%u kB\n",
                        (unsigned int)(b->size / 1024u), (unsigned int)(uintptr_t)ptr,
                        (unsigned int)(s_heap_used / 1024u));
        }

        if (b->next && !b->next->used) {        /* absorb the free block above */
            ps2_blk *n = b->next;
            b->size += PS2_HEAP_HDR + n->size;
            b->next = n->next;
        }
        if (prev && !prev->used) {              /* and the one below, now adjacent */
            prev->size += PS2_HEAP_HDR + b->size;
            prev->next = b->next;
        }
        return;
    }
    ps2_kprintf("[HEAP] free(0x%x) is not a pool block\n", (unsigned int)(uintptr_t)ptr);
}

/* Each wrapper stamps ITS OWN return address rather than calling Imalloc(): a holder census that
 * named Imalloc/calloc/ps2_heap_take would name the allocator instead of the owner, which is the
 * whole point of the census. */
void* malloc(size_t sz) { return ps2_heap_take(sz, __builtin_return_address(0)); }
void* calloc(size_t n, size_t sz)
{
    size_t total = n * sz;
    void *p = ps2_heap_take(total, __builtin_return_address(0));
    if (p) tkl_memset(p, 0, total);
    return p;
}
void  free(void *p) { Ifree(p); }

/* ── Framebuffer & Color Conversion ─────────────────────────────── */

/* 32-bit BTRON Desktop Backbuffer (800x600 @ 32-bit ARGB COLOR) */
static COLOR s_desktop_backbuffer[PS2_SCREEN_WIDTH * PS2_SCREEN_HEIGHT] __attribute__((aligned(128)));

/* Defined below with the console driver, and the source profiles announce
 * themselves before anything reaches it. */
void ps2_kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Global Mouse Coordinates */
static int s_mouse_x = 400;
static int s_mouse_y = 300;
/* The position the sprite was last *presented* at, which is not the same as
 * s_mouse_x/y as soon as a pass spends distance without painting.  The damage a
 * cursor step leaves behind is the span between these two rows and the new ones,
 * because the old sprite is still on the screen until the band that covers it is
 * sent. */
static int s_shown_x = 400;
static int s_shown_y = 300;
static int s_gui_active = 0;

/* Why a rect that reaches the page from the canvas was allowed INSIDE a live GS layer's client
 * rect.  Rung 20w proved such uploads exist and named two of them; this enum separates them by the
 * condition that let them out, so the next census says which one to fix rather than leaving it to
 * be argued from a rect's size.  The first three are the pass's own canvas upload; the fourth is
 * the second-order re-lay rung 20w removed. */
#define PS2_WHY_STREAM   0u     /* a stream is staged and goes out after this canvas: the order rule */
#define PS2_WHY_CUT      1u     /* no stream, and the layer's rect was cut out of this region */
#define PS2_WHY_NO_LAYER 2u     /* no GL layer window exists, so no pixels needed protecting */
#define PS2_WHY_NO_RECT  3u     /* a layer window exists but its rect could not be computed */
#define PS2_WHY_RELAY    4u     /* the staged stream turned out to hold no groups */
#define PS2_WHY_COVER    5u     /* a cover rect re-laid above the stream, by contract */
#define PS2_WHY_PLATED   6u     /* a stream IS staged, and the last one plated this same rect, so the
                                 * layer's pixels are the stream's to paint and the canvas has no
                                 * picture of them to lay: rung 20y's cut on a pass that sends */
#define PS2_WHY_NOCUT    7u     /* a layer's rect crosses this upload and this build lets the canvas
                                 * lay over it: the HEAD order rule, which is the production path
                                 * since rung 24 refuted the plate test as a licence to cut */

#if BTRON_PS2_BENCH
/* What the pass's present did, per region: the rect, whether it took the whole-canvas
 * branch, and whether the sprite append ran and where it drew.  The cursor phase needs
 * all four to tell "no band covered the sprite" from "the band ran and the composite
 * did not put the sprite back" -- the two look identical in the surfaces afterwards. */
#define PS2_PAINT_TRACE_MAX 8
static int      s_paint_union[4];
static int      s_paint_rect[PS2_PAINT_TRACE_MAX][4];
static int      s_paint_full[PS2_PAINT_TRACE_MAX];
static int      s_paint_appended[PS2_PAINT_TRACE_MAX];
static int      s_paint_appended_at[PS2_PAINT_TRACE_MAX][2];
static unsigned s_paint_pix[PS2_PAINT_TRACE_MAX];
/* The sprite's nine-pixel signature read out of the canvas on both sides of the append.
 * pre=0 post=1 is the append landing.  post=0 with appended=1 is the writer not writing,
 * which is what a broken call looks like: the append is a five-argument call, and before
 * the whole image was given one O32 calling convention its fifth argument (the canvas
 * height) arrived as garbage from this file, so every row failed its own clip test and
 * the sprite appeared only on a whole-canvas pass, drawn from inside desktop.c. */
static int      s_paint_sig[PS2_PAINT_TRACE_MAX];
static int      s_paint_pre[PS2_PAINT_TRACE_MAX];
static int      s_paint_post[PS2_PAINT_TRACE_MAX];
static uint32_t s_paint_calls;
static BOOL ps2_cursor_sig_at(const uint32_t *p, int x, int y);

/* LANE 19 rung 6c -- the cover uploads, kept as a ring because rung 6b named an eraser and could
 * not name its author.  Rung 6b reads: ink arrives (client ink 383,951 of 384,000 px), holds for
 * one census (erased=0), then leaves -- erased=96,220 over 283 row(s), client y 52..334,
 * newly laid=0.  "Arrives then leaves, nothing laid in its place" excludes animation, and the two
 * uploaders that could do it are separated by this ring rather than argued about:
 *
 *   the pass's own canvas upload, which is whole-width and would therefore have emptied EVERY
 *     client row, and the stream is sent after it by the ordering rule of src/gl/eegs_present.h;
 *   `ps2_upload_cover_in_layer()`, which deliberately re-lays the CANVAS inside the layer's rect
 *     after the stream -- for whatever the composite drew above the layer -- and whose row
 *     `cover=4` counts the list, not the pieces that survived the intersection with the client.
 *
 * Only the second can leave a hole with an edge at row 334, and on the 12v image it did: the census
 * named 4 cover uploads of one rect (16,52..355,335, 95,937 px each) as covering all 279 lost rows,
 * with the Tracker menu open -- twice per pass, because `global_menu_get_open_rect()` answers the
 * Tracker's own rect whenever the Tracker menu is what is open and `workbench_cover_overlays()` asks
 * both getters.  That is rung 20's #124.
 *
 * The first was never a measurement, only an exclusion by width.  Rung 20w makes it one: the ring
 * now takes the pass's own upload too, with its kind and whether a GS stream followed it in the same
 * pass.  That is the question the operator's two symptoms turn on -- a canvas upload that lands
 * inside the layer's rect with no stream behind it lays pixels the canvas never had a picture of,
 * which is grey where the blue should be and a stripe that flickers as a band passes over it. */
#define PS2_COVER_TRACE_N 64      /* two censuses are ~4 passes apart and each pass now puts its own
                                   * rect in as well as a handful of cover rects, so the ring has to
                                   * be wider than a pass -- and the census says when it wrapped. */
#define PS2_UPLOAD_CANVAS 0u      /* the pass's own region upload: the desktop's pixels */
#define PS2_UPLOAD_COVER  1u      /* a rect an overlay asked to be re-laid above the stream */
static int      s_cover_rect[PS2_COVER_TRACE_N][4];
static uint32_t s_cover_pass[PS2_COVER_TRACE_N];
static uint8_t  s_cover_kind[PS2_COVER_TRACE_N];    /* PS2_UPLOAD_CANVAS / PS2_UPLOAD_COVER */
static uint8_t  s_cover_stream[PS2_COVER_TRACE_N];  /* did this pass put a GS stream on the bus */
static uint8_t  s_cover_why[PS2_COVER_TRACE_N];     /* which condition let it out: PS2_WHY_* */
static uint32_t s_cover_writes;       /* every rect that went out; the ring wraps, this does not */
static uint32_t s_cover_pass_no;      /* one increment per ps2_paint_region() call */

/* One entry per rect that reaches the page FROM the canvas, recorded at the send with the rect's own
 * edges so what the ring holds is what went out -- not the list it came from. */
static void ps2_upload_trace(int x0, int y0, int x1, int y1, uint8_t kind, int stream, uint8_t why)
{
    const uint32_t c = s_cover_writes++ % PS2_COVER_TRACE_N;

    s_cover_rect[c][0] = x0;    s_cover_rect[c][1] = y0;
    s_cover_rect[c][2] = x1;    s_cover_rect[c][3] = y1;
    s_cover_pass[c] = s_cover_pass_no;
    s_cover_kind[c] = kind;
    s_cover_stream[c] = stream ? 1u : 0u;
    s_cover_why[c] = why;
}
#endif

/* The guest's own px-per-count gain, 8.8 fixed point where 256 is verbatim.  Each
 * source profile installs a default for it and `sens` at the prompt overrides it
 * within a run, which is how any of these claims gets tested.
 *
 * What it is *not* is a fix for a source that is not proportional to the hand --
 * a multiplier can only be right for one speed, and a report whose counts already
 * contain somebody else's acceleration has more than one speed in it.  That case
 * belongs to the profile chosen below, not to a constant.
 *
 * LAW 13d (doc/txt/PS2.txt): this gain is the ONLY thing between a count and a
 * cursor pixel on this target.  Nothing here reads how fast the counts arrived --
 * not a step curve, not a cap (PS2_PTR_MAX_STEP below is off for the same
 * reason) -- because a PS2 hand has no way to ask for 1:1 if some stage multiplies
 * by speed and offers no setting at which it does not. */
#define PS2_MOUSE_MULT_FP 256
static int32_t s_ptr_mult_fp = PS2_MOUSE_MULT_FP;
static int32_t s_ptr_carry_x, s_ptr_carry_y;
static int32_t s_ptr_gain_seen;   /* the dial's value already installed */

/* How far the cursor may travel in one INPUT period, in pixels -- the periods a pass
 * owes come from the plane table, so the cap is a speed and not a frame count.
 *
 * Off by default, and that default is the whole lesson of a long tuning session:
 * a cap spends at most PS2_PTR_MAX_STEP px per period and gives up anything past
 * DP_PTR_LIMIT_DEBT steps of it, so guest travel becomes a function of how fast
 * the hand moved rather than how far.  That is precisely non-proportional, and no
 * constant downstream can put the distance back.  The limiter stays compiled in
 * because it is the only way to measure the difference (`maxstep 8` at the
 * prompt), not because the shipped pointer uses it.
 *
 * The reason a cap was reaching for at all -- the pointer being too fast -- is
 * answered instead by the source profiles below, which is a per-source gain
 * rather than a speed-dependent one. */
#define PS2_PTR_MAX_STEP 0
static int32_t s_ptr_max_step = PS2_PTR_MAX_STEP;
static int32_t s_ptr_want_x, s_ptr_want_y;     /* counts that arrived since the last pass */
static int32_t s_ptr_defer_x, s_ptr_defer_y;   /* pixels the cap has not spent yet */
static uint32_t s_ptr_input_due;               /* Count at the last INPUT slice taken */

/* ── Pointer Source Profiles ─────────────────────── */

/* Two stubs, because a count off this bus means something different depending on
 * what is on the other end, and no single guest constant can be right for both.
 * Selected by what actually enumerated, so the shipped binary needs no knowledge
 * of where it is running, and overridable at the prompt for measurement.
 *
 * EMU -- PCSX2's `hidmouse`.  Its X/Y are the host cursor's own movement, which
 * macOS has already accelerated: one count per pixel the host cursor travelled,
 * clamped to +/-127 per report with the remainder carried into the next (see
 * PS2_EMU_POINTER_SCALE below, which is settled from the emulator's input path
 * rather than from its ini).  Distance-proportional only if the host's acceleration
 * is off, and never in need of a second curve here -- so this stub applies a
 * guest-side gain and nothing else, sized below.
 *
 * HW -- a real mouse on a real console: counts proportional to how far the hand
 * moved, at the device's own 10-16 ms interval.  This used to be the case an
 * accelerator belonged to, and it ran the same RISC OS step curve the arm64/Pi 400
 * port uses so that one mouse felt the same on both real machines.  LAW 13d took
 * that out of this target: dp_ptr_riscos()'s floor is 384/256 at the lowest step
 * the Settings panel offers and 768/256 at the highest, so on a PS2 a hand could
 * not get a proportional pointer at ANY setting.  The curve itself stays exactly
 * where it is -- src/graphics/dp_accel.c is shared and core_arm64.c calls it for
 * the Pi 400's feel -- and what changes is only which of its callers this file
 * has. */
#define PS2_PTRSRC_EMU 0
#define PS2_PTRSRC_HW  1

/* ps2_usb_dev_t.desc_id is idVendor | idProduct << 16, and the emulator's HID
 * Mouse is device 0627:0001, which is what this compares against. */
#define PS2_DEVID_PCSX2_MOUSE 0x00010627u

/* Counts the emulator spends per pixel the host cursor travelled.
 *
 * Read from PCSX2 v2.8.2's own input path, not from its ini: a pointer binding of
 * USB Type::Pointer takes the raw delta (`InputManager.cpp:1398-1403` hands the
 * callback `delta`, not the scaled `value`), that delta is
 * `QCursor::pos() - window centre` (`DisplayWidget.cpp:321-326`, warped back to the
 * centre each event), and it reaches the HID report as `int_clamp(xdx, -127, 127)`
 * with the remainder carried into the next report (`hid.cpp:632-635`).  So one count
 * is one host cursor pixel, and `[Pad] PointerXScale` / `PointerYScale` -- which this
 * file used to believe it was dividing out -- do not exist anywhere in that tree.
 * They are inert keys in the repo's inis; the guest is the only place a gain is
 * applied, which is what makes the constant below the whole transfer function. */
#define PS2_EMU_POINTER_SCALE 1

/* The emulator profile's applied gain in 256ths -- guest pixels per count, which at
 * the scale of 1 above is also guest pixels per pixel the hand travelled.
 *
 * 96 is the speed the cursor has already been moving at, and keeping it is
 * deliberate.  The three values a hand has judged were 0.125 px per count (a stroke
 * could not walk the 800 px canvas), 1.0 (it sat pinned against a wall) and 0.375
 * (what shipped).  What the corrected scale changes is not the cursor's speed but
 * the account of where it came from: those two ends were named against a host gain
 * the emulator does not have, so they were counts-per-pixel fictions and the physical
 * feel is the part worth keeping.
 *
 * `sens` retunes this live for one run and `ptg` prints the measured chain.  256 is
 * 1:1 with the hand, and the verdict against it is worth re-taking for one reason:
 * it was given while the desktop painted 27 cursor positions a second, so a fast
 * stroke overshot into a trail -- and the present that caused it now costs a tenth of
 * what it did. */
#define PS2_EMU_MULT_FP 96

static int s_ptr_src = PS2_PTRSRC_EMU;
static int s_ptr_src_forced;      /* a `ptrsrc` word outranks detection */

/* `g_mouse_step_mult` -- the Settings > Input knob the arm64 port feeds to
 * dp_ptr_riscos() -- is deliberately NOT read here any more.  It used to be this
 * file's second stage on the hw profile, which is what LAW 13d removed; leaving
 * the extern in place would keep a dial wired up to nothing and let a boot row
 * print a step value this port no longer applies.  Since LAW 13e it is not
 * defined for this target at all, so a PS2 row that reached for one of the two
 * dead knobs fails at the link instead of surviving as scenery.
 *
 * What this file does read is `g_ptr_gain_fp` -- the same variable `sens` sets,
 * the panel's gain block writes on Apply and the launcher's row writes on leave,
 * and the only multiplier between a count and a cursor pixel here (0 = this
 * source profile's own default).  ps2_ptr_gain_install() is the one place that
 * turns it into s_ptr_mult_fp, which is what keeps "one gain, one home" from
 * being a claim about a comment. */

static const char *ps2_ptr_src_name(int src)
{
    return src == PS2_PTRSRC_HW ? "hw" : "emu";
}

/* The live gain, in both units that mean something: a count is what this code
 * multiplies, a host cursor pixel is what a hand is measured against.  Only the
 * emulator's source has a host cursor in its chain, so the hardware row states
 * its own constant instead of a conversion that does not exist.  Both rows say
 * what LAW 13d guarantees: there is no curve in front of either gain.  Ends in a
 * newline so callers can put their own lead-in on it. */
static void ps2_ptr_gain_row(void)
{
    if (s_ptr_src == PS2_PTRSRC_HW)
        ps2_kprintf("%d/256 px per count, proportional (no curve), cap %s\n",
                    (int)s_ptr_mult_fp,
                    s_ptr_max_step > 0 ? "on" : "off");
    else {
        /* In 100ths, because the second unit is a fraction of a pixel now that a
         * count is one host cursor px: 96/256 of a pixel reads as 0 in integers, and
         * a row that prints the shipped gain as zero is worse than no row. */
        const int pp100 = (int)(s_ptr_mult_fp * PS2_EMU_POINTER_SCALE * 100 / PS2_MOUSE_MULT_FP);

        ps2_kprintf("%d/256 px per count = %d.%02d px per host cursor px, no curve, cap %s\n",
                    (int)s_ptr_mult_fp, pp100 / 100, pp100 % 100,
                    s_ptr_max_step > 0 ? "on" : "off");
    }
}

/* Install the gain the dial asks for, or this profile's own default while the
 * dial is on Auto (LAW 13e).
 *
 * The subpixel remainders always go with the old gain, for the reason `ptrsrc`
 * already gives: a carry is a fraction of the scale that made it, and leaving it
 * behind would put a fraction of a pixel of somebody else's stroke on the next
 * report.  The unspent DISTANCE goes only when reset_flow says so.  It is whole
 * pixels this port already produced for a movement the hand made, so a gain
 * change would be losing travel -- which is the one fault every row of chapter
 * 13 is written against -- while a SOURCE change is a different device's counts
 * arriving, and there the old profile's queue really is stale. */
static void ps2_ptr_gain_install(int reset_flow)
{
    s_ptr_mult_fp = (g_ptr_gain_fp > 0)
                        ? g_ptr_gain_fp
                        : (s_ptr_src == PS2_PTRSRC_HW ? PS2_MOUSE_MULT_FP : PS2_EMU_MULT_FP);
    s_ptr_carry_x = s_ptr_carry_y = 0;
    if (!reset_flow) return;
    s_ptr_want_x = s_ptr_want_y = 0;
    s_ptr_defer_x = s_ptr_defer_y = 0;
    s_ptr_input_due = 0u;   /* re-arm the INPUT slice clock rather than let it
                             * spend the whole idle interval as catch-up */
}

/* Adopt a gain this file did not set: the panel's Apply and the launcher's row
 * both write g_ptr_gain_fp, and the desktop's own pass is where that is noticed.
 * Prints only on a change, in the same words the boot row and `sens` use, so a
 * hand that changed the cursor's speed from a window reads the same confirmation
 * as a hand that changed it from the Stage-1 prompt. */
static void ps2_ptr_gain_sync(void)
{
    if (g_ptr_gain_fp == s_ptr_gain_seen) return;
    s_ptr_gain_seen = g_ptr_gain_fp;
    ps2_ptr_gain_install(0);
    ps2_kprintf("[PS2] Pointer gain, dialed: ");
    ps2_ptr_gain_row();
}

/* Install a profile's policy.  Kept separate from the choice so detection can run
 * before the first report and a prompt command can run after it with the same
 * effect, and so a switch mid-session cannot leave the previous source's
 * half-spent distance or subpixel remainder behind to be spent as the new one. */
static void ps2_ptr_src_apply(const char *why)
{
    s_ptr_max_step = PS2_PTR_MAX_STEP;
    s_ptr_gain_seen = g_ptr_gain_fp;    /* the dial outranks a profile default */
    ps2_ptr_gain_install(1);
    ps2_kprintf("[PS2] Pointer source %s (%s): ", ps2_ptr_src_name(s_ptr_src), why);
    ps2_ptr_gain_row();
}

/* Which stub is live is a fact about the device, not about where the code was
 * built: the emulator answers with its own USB identity, and anything else on the
 * bus is a mouse made by somebody who sells mice. */
static void ps2_ptr_src_detect(void)
{
    int src = PS2_PTRSRC_EMU;
    int found = 0;

    if (s_ptr_src_forced) return;
    for (int i = 0; i < 2; i++) {
        const ps2_usb_dev_t *d = ps2_usb_dev(i);
        if (!d || d->is_keyboard || !d->desc_id) continue;
        found = 1;
        if (d->desc_id != PS2_DEVID_PCSX2_MOUSE) src = PS2_PTRSRC_HW;
    }
    s_ptr_src = src;
    ps2_ptr_src_apply(found ? "detected" : "no mouse yet, defaulting");
}

/* One report's pair of counts through the live source's shaping, in pixels.
 *
 * One stage, for both profiles: LAW 13d.  A count is multiplied by the source's
 * gain and nothing else, so what a report is worth on screen is a function of how
 * FAR the hand moved and not of how FAST -- the property every instrument in
 * doc/txt/PS2.txt 13.1 assumes when it reads a `[CAL]` or `[PSTAT]` gain row as a
 * constant.  The hw profile used to put dp_ptr_riscos() in front of its gain; that
 * curve's lowest setting was 1.5x and its shipped default 2.0-2.5x, with no value
 * at which it was proportional, which is why this target declined it rather than
 * dialing it.  It remains the Pi 400's feel, called from core_arm64.c.
 *
 * The profiles differ only in s_ptr_mult_fp, installed by ps2_ptr_src_apply(), and
 * they share one subpixel carry because there is now one stage whose remainder
 * needs carrying. */
static void ps2_ptr_shape(int dx, int dy, int32_t *px, int32_t *py)
{
    *px = dp_ptr_scale(dx, s_ptr_mult_fp, &s_ptr_carry_x);
    *py = dp_ptr_scale(dy, s_ptr_mult_fp, &s_ptr_carry_y);
}

/* Measured GIF upload costs, printed once on the boot log */
static uint32_t s_full_upload_us;
static uint32_t s_band_upload_us;
static uint32_t s_ohci_probe_us;
static uint32_t s_gs_init_us;

/* Forward declaration */

void launch_ps2_desktop_session(void);
static void ps2_log_ohci_probe(const ps2_ohci_probe_t *p);
static int ps2_log_host(void);

/* ── Present Bands ───────────────────────────────────────────── */

/* The part of the canvas the GS is owed after a pass.  Everything a present does
 * is linear in this range: the composite clips to it, the colour shuffle walks one
 * word per pixel of it, and the GIF IMAGE stream sends it.  So the region presented
 * is what sets how soon the loop looks at the USB ring again -- which is the
 * pointer's frame rate, and also the difference between the device still holding a
 * report and having dropped it: the interrupt ring absorbs seven of them and an HID
 * pointer holds fewer still.
 *
 * Ranges, plural, and merged when they touch: the union of the panel's 28 rows and
 * a cursor at y=300 is a band of 316 rows, half a canvas, which is exactly the cost
 * this is here to avoid.  Two bands that do not overlap are two setup packets and
 * two streams, and 44 rows between them.
 *
 * A band is a rectangle, bounded in x as well as in y, because the machine priced
 * the two shapes (ps2_bench_run() is that pricing): presenting 800x16 after a
 * sideways sweep costs 5370 us, the same 16 rows clipped to the sprite's own 16
 * columns cost 528 us, and the narrow region's one GIF setup per row -- which is
 * what `ps2_gs_upload()` has to issue as soon as the width is not the canvas's --
 * is 0.6 us of that.  The per-row setup is real and it is not the term that
 * matters; the pixels are.  A full-width band therefore costs ten times the damage
 * that was actually done to it, which is also the whole of the difference between
 * the two axes this file used to have to instrument to find. */
#define PS2_CURSOR_ROWS 16          /* draw_baremetal_cursor_raw() is a 16x16 sprite */
#define PS2_CURSOR_COLS 16          /* ... and this is the width of that same sprite */
#define PS2_PANEL_ROWS  28          /* system panel plus the gold accent bar */
#define PS2_BAND_MAX    4u

typedef struct { int x0, x1, top, end; } ps2_band_t;
typedef struct { ps2_band_t b[PS2_BAND_MAX]; unsigned n; int overflow; } ps2_bandset_t;

static int ps2_band_is_full(const ps2_band_t *b)
{
    return b->x0 == 0 && b->x1 == PS2_SCREEN_WIDTH &&
           b->top == 0 && b->end == PS2_SCREEN_HEIGHT;
}

static void ps2_bandset_full(ps2_bandset_t *set)
{
    set->b[0].x0 = 0;
    set->b[0].x1 = PS2_SCREEN_WIDTH;
    set->b[0].top = 0;
    set->b[0].end = PS2_SCREEN_HEIGHT;
    set->n = 1u;
    set->overflow = 0;
}

/* Widen an existing band, or insert a new one.  The set stays ordered by `top`, so
 * the loop below stops at the first band that does not lie wholly above the range
 * being added, and a range that bridges two existing bands leaves one band behind
 * rather than painting the same rows twice.
 *
 * Bands that touch in y become one and their columns union, so the pair is then
 * presented as the wider of the two spans rather than as the sum of their areas:
 * that is the price of one GIF setup per band rather than one per rectangle, and it
 * is why the panel's full-width band is worth keeping separate from the cursor's
 * wherever the two do not already share rows. */
static void ps2_bandset_add(ps2_bandset_t *set, int x0, int x1, int y0, int y1)
{
    unsigned i, k;

    if (x0 < 0) x0 = 0;
    if (x1 > PS2_SCREEN_WIDTH) x1 = PS2_SCREEN_WIDTH;
    if (y0 < 0) y0 = 0;
    if (y1 > PS2_SCREEN_HEIGHT) y1 = PS2_SCREEN_HEIGHT;
    if (x1 <= x0 || y1 <= y0) return;

    for (i = 0; i < set->n; i++) {
        if (y0 > set->b[i].end) continue;      /* lies wholly below this band */
        if (y1 < set->b[i].top) break;         /* wholly above it: insert here */
        if (y0 < set->b[i].top) set->b[i].top = y0;
        if (y1 > set->b[i].end) set->b[i].end = y1;
        if (x0 < set->b[i].x0) set->b[i].x0 = x0;
        if (x1 > set->b[i].x1) set->b[i].x1 = x1;
        while (i + 1u < set->n && set->b[i + 1u].top <= set->b[i].end) {
            if (set->b[i + 1u].end > set->b[i].end) set->b[i].end = set->b[i + 1u].end;
            if (set->b[i + 1u].x0 < set->b[i].x0) set->b[i].x0 = set->b[i + 1u].x0;
            if (set->b[i + 1u].x1 > set->b[i].x1) set->b[i].x1 = set->b[i + 1u].x1;
            for (k = i + 1u; k + 1u < set->n; k++) set->b[k] = set->b[k + 1u];
            set->n--;
        }
        return;
    }
    if (set->n >= PS2_BAND_MAX) {
        set->overflow = 1;         /* present the canvas rather than drop damage */
        return;
    }
    for (k = set->n; k > i; k--) set->b[k] = set->b[k - 1u];
    set->b[i].x0 = x0;
    set->b[i].x1 = x1;
    set->b[i].top = y0;
    set->b[i].end = y1;
    set->n++;
}

/* ── Per-page damage debt: the rotation LANE 21 rung 4 needs ──────────
 *
 * With one page there is one present and it carries the whole canvas, so a band set is
 * thrown away the moment it has been drawn.  With two, the set describes the CANVAS, and
 * a page only shows the canvas as of the last time it was plated.  A pass that damages D
 * and paints D into the hidden page before flipping leaves the other page holding an
 * older canvas: the next flip puts a frame that missed D on the screen, and the flicker
 * this rung exists to remove comes back as a stale frame instead of a torn one.
 *
 * So each page remembers what the canvas did to it while it was hidden.  Three lines,
 * in this order, and the order is the whole design:
 *
 *   owed[other] := owed[other] union D  -- D is the pass' OWN decision, before any debt
 *   P           = D union owed[drawn]   -- what this pass has to plate
 *   owed[drawn] := {}                   -- plated now
 *
 * The first line is the one that must not move.  Debt arriving through `owed` is a
 * catch-up, not a change to the canvas, so the regions it covers must not be re-owed to
 * the other page or the loop never converges: a full pass puts `full` in one page's debt,
 * that page pays it as P=full, and re-owing P would put `full` back in the other page's
 * debt forever -- every pass whole-canvas, which is worse than the single buffer it
 * replaced.  Consuming the debt in the second line and clearing it in the third is what
 * makes a forced-full pass cost exactly ONE extra full plate per page and return to
 * banded.
 *
 * D here is the post-decision set -- the canvas whenever a force rule fired -- because a
 * forced pass is the compositor saying "I do not know what else moved", and a restacked
 * window is exactly a canvas change the other page has to be told about.
 *
 * owed[] is indexed by PAGE INDEX, not by "hidden", because the indices are what flip
 * commits and what a census names -- gs_disp_drawn()/shown() say which is which now. */
static ps2_bandset_t s_page_owed[GS_DISP_PAGES];

/* Both pages owe the whole canvas.  Called whenever the page model changes under them --
 * a format dial, a double-buffer dial, a session start -- because after any of those the
 * pages hold content that does not describe the present, and the only fix is to plate
 * both.  Costs exactly one extra full present per page, then decays. */
static void ps2_rot_owe_all(void)
{
    for (unsigned i = 0u; i < GS_DISP_PAGES; i++) ps2_bandset_full(&s_page_owed[i]);
}

static void ps2_rot_clear(void)
{
    for (unsigned i = 0u; i < GS_DISP_PAGES; i++) {
        s_page_owed[i].n = 0u;
        s_page_owed[i].overflow = 0;
    }
}

/* Fold the damage `d` into the debt of the page that is about to be hidden.  Takes D and
 * not P: see the law above.  A debt that cannot be held in PS2_BAND_MAX bands is not
 * dropped, it is a full page -- the same rule ps2_bandset_add() applies to live damage. */
static void ps2_rot_owe(uint32_t other, const ps2_bandset_t *d)
{
    ps2_bandset_t *o = &s_page_owed[other % GS_DISP_PAGES];

    if (d->overflow || !d->n) {
        if (d->overflow) ps2_bandset_full(o);
        return;
    }
    for (unsigned i = 0u; i < d->n; i++)
        ps2_bandset_add(o, d->b[i].x0, d->b[i].x1, d->b[i].top, d->b[i].end);
    if (o->overflow) ps2_bandset_full(o);
}

/* The page being drawn now has been plated: fold its debt into `set` and clear it.
 * Returns 1 when it merged anything, 0 when the page owed nothing and `set` is exactly
 * the damage it was.  Whether the pass has something to present is not this function's
 * question -- the caller's `if (!set.n)` already answers that, and it answers it AFTER
 * this call, which is the point: a pass with no new damage but a page in debt must paint
 * and flip, and a pass with neither must not flip an unplated page. */
static int ps2_rot_take(uint32_t page, ps2_bandset_t *set)
{
    ps2_bandset_t *o = &s_page_owed[page % GS_DISP_PAGES];

    if (o->overflow) ps2_bandset_full(o);
    if (!o->n) return 0;

    for (unsigned i = 0u; i < o->n; i++)
        ps2_bandset_add(set, o->b[i].x0, o->b[i].x1, o->b[i].top, o->b[i].end);
    if (set->overflow) ps2_bandset_full(set);
    o->n = 0u;
    o->overflow = 0;
    return 1;
}

/* How many bands a page owes -- and 1 is not the whole answer, since a page that owes the
 * canvas is one band too.  Read by the `page` row: a desktop that is not flipping, or a merge
 * that keeps re-owing, shows up here as debt that never drains, which is the failure this
 * rotation could have and a log full of good frames would not say. */
static int ps2_rot_debt(uint32_t page)
{
    const ps2_bandset_t *o = &s_page_owed[page % GS_DISP_PAGES];
    if (o->overflow) return -1;
    if (o->n == 1u && ps2_band_is_full(&o->b[0])) return -1;   /* the canvas, in one band */
    return (int)o->n;
}

/* One rotation step, in the order the law above needs it: the pass' own decision goes to the
 * page about to be hidden, and that page's debt is folded INTO the set about to be plated.  It
 * returns 1 when it merged a catch-up, which is the bit the [CYCLE] row prints.
 *
 * A function rather than three calls at the site, because the bench phase has to drive the same
 * three calls in the same order to measure the rotation at all -- the headless census run has no
 * desktop to compose -- and two copies of a union's order is the one way this law could be
 * established by a test that does not describe the shipped path.  With the page model off this is
 * a no-op that returns 0, which is what makes it safe to call from a pass that does not know. */
static int ps2_rot_step(ps2_bandset_t *set)
{
    uint32_t drawn, other;

    if (!gs_disp_is_double()) return 0;
    drawn = gs_disp_drawn()->index;
    other = gs_disp_shown()->index;
    ps2_rot_owe(other, set);
    return ps2_rot_take(drawn, set);
}

/* Dual-output console character emitter (SIO0 UART + GS Framebuffer Text) */
static void ps2_console_putc(char c)
{
    ps2_sio_putc(c);
    if (!s_gui_active) {
        ps2_gs_text_putc(c, 0xFFE0E0E0, 0xFF121B29);
    }
}

/* The GL downstack (backend_tinygl.c, backend_virgl.c, gl_dispatch.c) and the apps
 * that sit on it (xmb.c, glgears.c, quake's sys_btron.c) all log through this one
 * name.  It is the same console the rest of this file uses, so a [TinyGL] or a quake
 * Con_Printf row lands on SIO0 and never paints itself over the desktop. */
void uart_puts_raw(const char *s)
{
    if (!s) return;
    while (*s) ps2_console_putc(*s++);
}

/* ── Kernel Printf via SIO0 and GS Console ──────────────────────── */

/* The tick-to-microsecond law is not a constant in this port any more: it is measured at
 * boot by the INPUT plane and published by btron_ee_ticks_per_us()
 * (src/drivers/ps2/ps2_irq.{c,h}).  Every duration below divides by it, and the four
 * copies of 147 they used to divide by are what made this machine's console rows, its
 * budgets and its bench gates disagree with the two hardware clocks that do not use them.
 */
static int s_timebase_ok = 0;

static uint32_t ps2_us_since(uint32_t start)
{
    return (uint32_t)(ps2_count_read() - start) / btron_ee_ticks_per_us();
}

/* ── The machine's plane table ─────────────────────────────────────────────
 * ASYNC.txt Tier-1, in this machine's own units (include/btron/async_rt.h).
 *
 * The reason the numbers live here rather than at the two places that read them is
 * that a period is not a property of the rasterizer: the pass that paints is also
 * the pass that spends the pointer's distance, so every "pixels per pass" the
 * cursor is tuned with is the frame time in disguise, and a literal in front of
 * `btron_ee_ticks_per_us()` is a machine constant with nothing to say where it came from.
 *
 * ps2_planes_install() runs in ps2_kernel_main() before the desktop session, the
 * shell loop and the GS console can call ps2_gui_pass(), so the readers below take
 * the table as given rather than testing it for NULL: an absent table here is a
 * boot-order bug, and self-pacing is the arm64 port's business, not this one's. */
static const btron_plane_cfg_t s_ps2_planes = {
    /* An OHCI frame is a millisecond, and ps2_usb_frame_number() is that clock.
     * It is the interval a real mouse's counts are stamped at, and the same clock
     * the `loop:` and [PTR] rows already age reports against, so the pointer's
     * cadence and the bus's agree instead of being two opinions about it. */
    .input_period_us = 1000u,
    /* The programmed display mode's own rate: 800x600 inside one 720p frame at
     * 60 Hz.  It is the deadline the async UI plane is charged against -- every
     * presenting pass is measured against it by ps2_plane_ui_deadline(), which is what
     * makes the plane's lateness a count rather than a description -- and it belongs to
     * the display rather than to however many passes a second the desktop loop happens
     * to make. */
    .ui_period_us = 16667u,
    /* The system panel owns 28 rows and its clock changes with no event and no
     * damage at all, so it is owed those rows on a schedule of its own. */
    .panel_refresh_us = 200000u,
    /* Whole-canvas convergence: the safety net for anything a banded present can
     * get wrong -- an app that paints without invalidating, a hover highlight
     * inside a window.  It is convergence and not animation, so two seconds of it
     * is invisible while two frames of it is not free. */
    .full_convergence_us = 2000000u,
    /* Sixteen periods is one 60 Hz frame of pointer catch-up, and it bounds the
     * work rather than the debt: a pass coming back from a 683 ms XMB frame must
     * not turn into a long loop just because time passed. */
    .input_slices_max = 16u
};

static void ps2_planes_install(void)
{
    btron_planes_install(&s_ps2_planes);
    /* One row, because these are the numbers every other timing row is charged
     * against, and a budget gate that cannot name its period is an opinion. */
    ps2_kprintf("[PLANE] input=%u us  ui=%u us  panel=%u us  converge=%u us  slices=%u\n",
                (unsigned int)s_ps2_planes.input_period_us,
                (unsigned int)s_ps2_planes.ui_period_us,
                (unsigned int)s_ps2_planes.panel_refresh_us,
                (unsigned int)s_ps2_planes.full_convergence_us,
                (unsigned int)s_ps2_planes.input_slices_max);
}

/* ── The two worker planes, charged against their budgets ─────────────────
 * A plane has three things: a period, a budget, and the consequence that a run longer
 * than the budget is a deadline miss.  include/btron/async_rt.h states that consequence
 * as a flag rather than a preemption -- "a plane that overruns is flagged but never
 * blocks the other plane" -- and this port already had the instrument for one of the
 * three: ps2_irq.c measures the handler's own WCET against ASYNC_ISR_BUDGET_US and
 * counts its overruns, which is why the [PSTAT] plane row can answer "1.68 us of the
 * 50 us budget, overruns=0" instead of asserting it.
 *
 * What had no reader was the two WORKER budgets.  ASYNC_INPUT_BUDGET_US and
 * ASYNC_UI_BUDGET_US are named as this machine's budgets in the port's own ledger while
 * nothing measured a run against either, and ui_period_us was read only to be printed --
 * so "the presentation plane runs at the display's rate" was an opinion about however
 * many passes a second the loop happened to make.  Charging them costs two Count reads
 * per pass, buys each plane its WCET and its miss count, and gives ui_period_us the
 * reader that makes it a deadline instead of a label.
 *
 * Measurement, not enforcement, and the row says so: the shim is still cooperative --
 * sta_tsk() runs the body on the caller's stack, dly_tsk() is a busy-hold -- so a miss
 * here means "the plane was late and the machine knows it", not "the plane was cut off".
 * That distinction is lane 11's, and a fix for a number nobody can see is not a fix:
 * these counters are the baseline it has to be measured against.
 *
 * Two fields each, both monotone, and deliberately no mean: a sum of pass times would
 * overflow 32 bits a few minutes into the rate this loop makes, while a WCET beside a
 * miss ratio is exactly what a budget gate reads. */
typedef struct {
    uint32_t runs;      /* charges taken since boot                                  */
    uint32_t us_max;    /* the plane's WCET on this machine, µs                      */
    uint32_t over;      /* runs that spent more than the plane's budget              */
} ps2_plane_run_t;

static ps2_plane_run_t s_plane_input;   /* the worker half: the bus and the pointer   */
static ps2_plane_run_t s_plane_ui;      /* one presenting pass, dispatch to stream    */
static uint32_t s_plane_ui_last;        /* Count at the end of the last presenting pass */
static uint32_t s_plane_ui_late;        /* presenting passes that came due after ui   */

static void ps2_plane_charge(ps2_plane_run_t *c, uint32_t t0, uint32_t budget_us)
{
    const uint32_t tpu = btron_ee_ticks_per_us();
    uint32_t us;

    if (!s_timebase_ok || tpu == 0u) return;
    us = (uint32_t)(ps2_count_read() - t0) / tpu;
    c->runs++;
    if (us > c->us_max) c->us_max = us;
    if (us > budget_us) c->over++;
}

/* The presentation plane's absolute deadline, which is the field of the table that turns
 * "how fast is the loop" into "how late is the plane".  Only a pass that presented resets
 * it: a pass with no damage owed nothing and cannot be late, so counting it would make an
 * idle desktop read as a missed one.  The interval runs from the END of the last
 * presenting pass to the end of this one -- the period the body and the eye both see,
 * including whatever the hold asked for.  Count is 32 bits, so a gap wider than one wrap
 * (14.5 s on this machine) would come back small and be counted as on time; a pass that
 * long is the folded clock's business, and the [XMBT] cycle row is what names it. */
static void ps2_plane_ui_deadline(void)
{
    const uint32_t tpu = btron_ee_ticks_per_us();
    const uint32_t now = ps2_count_read();
    const uint32_t period_ticks = btron_planes()->ui_period_us * tpu;

    if (!s_timebase_ok || tpu == 0u) return;
    if (s_plane_ui_last && (uint32_t)(now - s_plane_ui_last) > period_ticks) s_plane_ui_late++;
    s_plane_ui_last = now;
}

/* Printed with the plane's other evidence, because the three rows answer one question in
 * three parts: who took the reports off the bus, how long they waited, and whether either
 * worker plane stayed inside the budget its period is supposed to fit in. */
static void ps2_plane_run_print(void)
{
    if (!s_timebase_ok) {
        ps2_kprintf("[PSTAT] budget: no Count -- neither worker plane is charged\n");
        return;
    }
    ps2_kprintf("[PSTAT] budget input: runs=%u max=%u us against the %u us budget, misses=%u\n",
                (unsigned int)s_plane_input.runs, (unsigned int)s_plane_input.us_max,
                (unsigned int)ASYNC_INPUT_BUDGET_US, (unsigned int)s_plane_input.over);
    ps2_kprintf("[PSTAT] budget ui: passes=%u max=%u us against the %u us budget, misses=%u | "
                "deadline=%u us, late=%u\n",
                (unsigned int)s_plane_ui.runs, (unsigned int)s_plane_ui.us_max,
                (unsigned int)ASYNC_UI_BUDGET_US, (unsigned int)s_plane_ui.over,
                (unsigned int)btron_planes()->ui_period_us,
                (unsigned int)s_plane_ui_late);
}

/* dp_core.c answers this with a weak stub that returns zero, and this port never
 * overrode it -- which is why every stage of the shared compositor reads 0 in the
 * Pi 400's HUD-style split while the PS2 had only the three numbers measured above.
 * Answering it from the CP0 Count turns the compositor's own attribution on here:
 * background restore, window decoration, application paint, client blit, panel and
 * bars, each of which has a different cure.
 *
 * Count is 32 bits and wraps every 2^32/tpu microseconds -- 29 s at the documented rate,
 * 14.5 s at the one this machine measures -- and callers take an absolute difference, so
 * the wrap is folded into a high word rather than leaking a four-billion microsecond
 * maximum into a stage counter.  The fold's per-wrap step is therefore derived from the
 * measured divisor rather than written beside it: the arithmetic stays 32-bit on purpose
 * (this target links no runtime for wider division), and the residue below one
 * microsecond per wrap costs nothing a 1 ms budget can see. */
uint32_t btron_render_perf_us(void)
{
    static uint32_t s_ticks_hi;
    static uint32_t s_ticks_last;
    const uint32_t tpu = btron_ee_ticks_per_us();
    const uint32_t now = ps2_count_read();

    if (now < s_ticks_last) s_ticks_hi++;
    s_ticks_last = now;
    return s_timebase_ok ? s_ticks_hi * (0xFFFFFFFFu / tpu) + now / tpu : 0u;
}

/* ── One Task-Driven Frame Cycle, Stage By Stage ─────────────────── */

/* "The app paints in 184 ms and a frame arrives every 380 ms" is a question about the
 * other 196 ms, and this file is where the answer lives: the app's own `[XMBT] frame`
 * row stops at `egl_swap_buffers`, so everything else in the period -- the bus polls,
 * the pointer service, the event dispatch, the composite that wraps the app's paint
 * callback, the GIF stream, the pass' own trailing delay, and the hold the body asked
 * for -- is invisible to it.  `ps2_paint_bands()` already splits the present into
 * render/swap/upload; what was missing were the pass' boundaries and the hold, which is
 * this.  The one number that decides where a cut belongs is `render=` beside the app's
 * `draw+swap`: the remainder is the compositor's, not the app's.
 *
 * Stamped inside ps2_gui_pass(), printed by ps2_task_dly() after the hold has been
 * spent, because the hold is part of the period the body sees and nothing else times it.
 * Ticks are raw Count, not the folded clock: a cycle is far under the 14.5 s this
 * machine's Count takes to wrap, so the fold would only add a division -- and a row that
 * did wrap would read as a negative stage, which is loud enough to notice.
 *
 * Not behind the bench flag, the way ps2_paint_note() below is not either: an unarmed
 * profiler costs one branch per boundary and 96 bytes of .bss, and gating it would buy
 * only those cycles back while making the row unavailable to whoever has the machine.
 * Arming is `left`, which the app phase sets. */
typedef struct {
    uint32_t left;        /* cycles still owed a row; armed by the app phase */
    uint32_t rows;        /* rows printed since the arm */
    uint32_t prof;        /* this pass is being timed */
    uint32_t pn;          /* boundaries taken so far this pass */
    uint32_t pc[7];       /* Count ticks at those boundaries */
    uint32_t stage[6];    /* poll, hook, evt, net, paint, note -- µs */
    uint32_t pass_us;     /* first tick to last, so the stages sum to it */
    uint32_t tail_us;     /* the pass' end to the hold: its own delay and the s_shown pair */
    uint32_t hold_us;     /* what ps2_task_hold() actually spent */
    uint32_t t_end;       /* the pass' last tick, which the tail is measured from */
    uint32_t r_us, s_us, u_us, px;
    uint32_t stamped;     /* a pass has filled the fields above */
    uint32_t why;         /* the PS2_CYCLE_WHY_* bits that were set when it presented */
    int bands, full;
    int bx0, by0, bx1, by1;   /* the damage band set held before it went whole-canvas */
    int ndmg;                 /* how many bands that damage was spread over */
    RENDER_STATS st;          /* the shared compositor's stages for this one pass */
} ps2_cycle_t;

/* Which rule made this pass a whole-canvas one.  These are the ways ps2_gui_pass() can
 * decide to repaint everything, and they have four different cures: a stray key in the
 * queue is a bench artefact, the convergence timer is by design, an overlay that never
 * closed is a bug in whatever opened it, and a band set that merged into the canvas is
 * damage the app did not ask for.  Printed as one hex field because a pass can carry
 * more than one.
 *
 * The overlay test is split into its four conditions rather than one bit, because the
 * first run of this instrument returned `why=4` on every steady frame and the four
 * conditions have nothing in common: a menu the user opened is correct, a window
 * manager still claiming a drag several hundred milliseconds after the button went up
 * is the bug this row was written to find. */
#define PS2_CYCLE_WHY_EVENT    1u    /* an event that was not a mouse move came through */
#define PS2_CYCLE_WHY_TIMER    2u    /* full_convergence_us came due */
#define PS2_CYCLE_WHY_GMENU    4u    /* global_menu_is_open() */
#define PS2_CYCLE_WHY_TRACKER  8u    /* tracker_is_menu_open() */
#define PS2_CYCLE_WHY_WM      16u    /* wnd_mgr_is_interacting(): a drag or resize in flight */
#define PS2_CYCLE_WHY_TIP     32u    /* the IME converting or showing candidates */
#define PS2_CYCLE_WHY_OVERFLOW 64u   /* more damage than PS2_BAND_MAX bands hold */
#define PS2_CYCLE_WHY_MERGED  128u   /* band 0 already spanned the canvas, no rule fired */
#define PS2_CYCLE_WHY_ROTATE  256u   /* rung 4: the band set grew because a page owed a catch-up */
#define PS2_CYCLE_WHY_PAINTW  512u   /* #165: the pass deferred the app's frame on its own price */

static ps2_cycle_t s_cycle;

/* One stage boundary.  A function rather than an open-coded read so the six call sites
 * read as one instrument; for a pass that owes no row it is one branch. */
static void ps2_cycle_mark(void)
{
    if (!s_cycle.prof || s_cycle.pn >= 7u) return;
    s_cycle.pc[s_cycle.pn++] = ps2_count_read();
}

/* Begin a pass.  Only a task-driven one can be an app frame: the session loop makes
 * thousands of the other kind a second, and none of them owes a row.  The attribution
 * fields are cleared here rather than at the print because the gate below only ever
 * writes when this pass is being timed, so an unprofiled pass could otherwise leave a
 * bit -- or a stale damage rect -- for the next frame to print as its own. */
/* The EE/GS layer's totals for one pass (doc/txt/PS2.txt, increment 5c).  Accumulated by
 * ps2_paint_region across the bands it presents and printed as its own row by ps2_cycle_print,
 * because that row is the only evidence on the console that a GIF stream went out at all -- and
 * `drop=`, `unsplit=` and `texmiss=` are the three counters that have to read zero for the
 * picture to be the one the application asked for.  `cover=` counts the canvas rects re-uploaded
 * over the layer and `coverdrop=` the ones the window manager's eight-entry list could not hold:
 * a buried panel is a silent wrong picture unless something numbers it.  `covermerge=` is the
 * third case, and it is not a loss: an adder asked for a rect an earlier entry already covered
 * opaquely, so the list refused the duplicate upload -- rung 20w's #124, whose cost was two uploads
 * of the same 95,937 px for one open Tracker menu.  Zeroed at ps2_cycle_begin(). */
static uint32_t s_gs_groups, s_gs_qw, s_gs_us, s_gs_cover, s_gs_cover_drop, s_gs_cover_merge;
/* How many times this cycle a pass took a staged stream that had no groups in it.  Before rung 20x
 * that case re-laid the layer's whole client rect from the canvas, which is a desktop pixel where
 * the picture was; now it is the same no-stream pass the cut already handles, and this column is
 * the reading that says the case exists at all. */
static uint32_t s_gs_empty_stream;
/* Rung 20y's two columns: how many of this cycle's passes were allowed to skip the layer's rect in
 * their canvas upload because the last stream that went out plated it, and how many pixels that
 * refused to send.  On the operator's route the second is 384,000 per app frame -- the whole client --
 * and it is the desktop grey the PCRTC scans before the stream overwrites it. */
static uint32_t s_gs_plated_passes, s_gs_plated_px;
static eegs_present_stats s_gs_st;

static void ps2_cycle_begin(int task_driven)
{
    s_cycle.prof = (s_cycle.left != 0u && task_driven) ? 1u : 0u;
    s_cycle.pn = 0u;
    s_cycle.why = 0u;
    s_cycle.ndmg = 0;
    s_cycle.bx0 = s_cycle.by0 = s_cycle.bx1 = s_cycle.by1 = 0;
    s_gs_groups = s_gs_qw = s_gs_us = 0u;
    s_gs_cover = s_gs_cover_drop = s_gs_cover_merge = 0u;
    s_gs_empty_stream = 0u;
    s_gs_plated_passes = s_gs_plated_px = 0u;
    ps2_cycle_mark();
}

/* Which rule fired.  ORed, because a frame can legitimately be owed the canvas twice
 * (an app that keys and a convergence timer that came due in the same pass), and the
 * row that shows two bits is the one that says so. */
static void ps2_cycle_why(uint32_t bits)
{
    if (s_cycle.prof) s_cycle.why |= bits;
}

/* The damage the pass actually asked for, taken before ps2_bandset_full() overwrites
 * it.  Beside `why` this is the pair that answers the question the row exists for:
 * a 16x16 cursor rect plus a stray event is a dispatch problem, and a damage set that
 * already spanned the canvas is the app invalidating more than it drew. */
static void ps2_cycle_damage(const ps2_bandset_t *set)
{
    if (!s_cycle.prof || set->n == 0u) return;
    s_cycle.bx0 = set->b[0].x0;
    s_cycle.by0 = set->b[0].top;
    s_cycle.bx1 = set->b[0].x1;
    s_cycle.by1 = set->b[0].end;
    s_cycle.ndmg = (int)set->n;
}

/* One rule is not a decision ps2_gui_pass() makes, so it cannot be marked where the
 * others are: the band set can reach the canvas on its own, because a cursor swept
 * across the screen unions two bands whose columns cover it.  Named here, at the point
 * that knows the set's final shape, and only when no rule above already claimed it. */
static void ps2_cycle_merged(const ps2_bandset_t *set)
{
    if (!s_cycle.prof || s_cycle.why != 0u) return;
    if (ps2_band_is_full(&set->b[0])) s_cycle.why = PS2_CYCLE_WHY_MERGED;
}

/* Commit the ticks this pass took.  A pass that returned early reached fewer boundaries
 * than seven, so the stages it never reached are measured against its last tick and read
 * zero rather than whatever the array held before it.
 *
 * `stamped` means "this pass owes a row", which is a different question from "a pass
 * finished": the session loop's passes reach here too, and a row for one of them would
 * print the previous frame's stages.  So everything below is gated on the pass having
 * been armed at its start, and ps2_cycle_hold_end() prints only what set this.
 *
 * The shared compositor's own stage counters are taken here, in the same breath, and
 * only for an armed pass: they are maxima since the last take, so taking one per app
 * frame makes them that frame's, which is the split the three stages above cannot give
 * -- `paint=` is this port's flush, and the app's rasterising arrives inside it as the
 * compositor's `paint` stage.  Passing on the unarmed ones is what keeps [BENCH]'s own
 * compositor rows measuring their whole phase rather than this file's last pass. */
static void ps2_cycle_stamp(uint32_t r_us, uint32_t s_us, uint32_t u_us, uint32_t px,
                            int bands, int full)
{
    const uint32_t tpu = btron_ee_ticks_per_us();
    const int armed = s_cycle.prof != 0u;
    const int n = (int)s_cycle.pn;
    int i;

    s_cycle.prof = 0u;
    s_cycle.stamped = 0u;
    if (!armed || n < 2 || tpu == 0u) return;
    for (i = 1; i < 7; i++) {
        const int hi = i < n ? i : n - 1;
        const int lo = i - 1 < n ? i - 1 : n - 1;
        s_cycle.stage[i - 1] = (uint32_t)(s_cycle.pc[hi] - s_cycle.pc[lo]) / tpu;
    }
    s_cycle.t_end = s_cycle.pc[n - 1];
    s_cycle.pass_us = (uint32_t)(s_cycle.pc[n - 1] - s_cycle.pc[0]) / tpu;
    s_cycle.r_us = r_us;  s_cycle.s_us = s_us;  s_cycle.u_us = u_us;
    s_cycle.px = px;  s_cycle.bands = bands;  s_cycle.full = full;
    btron_render_stats_take(&s_cycle.st);
    s_cycle.stamped = 1u;
}

/* How many [GSCHAIN] tables this run has printed.  File-scope rather than the print block's own
 * static because LANE 20 rung 24's cutoff is armed in the paint pass and takes effect on the frame
 * AFTER the one it lands in, and the table of that pre-arm frame -- 25 chains, limit already printed
 * as 2 -- is the one row a reader would take for the bisect's own picture.  Arming resets it. */
static uint32_t s_gschain_printed;

static void ps2_cycle_print(int asked_ms)
{
    /* `ovl=` and `rst=` are LANE 12i's pair, and they are on this row because the row is the
     * frame partition: `rst=` is `render - total - ovl`, the arithmetic that says whether the
     * residue is fully named.  `render=` is summed across the pass' bands while `total=` and
     * `ovl=` are the compositor's own per-call maxima, so the identity is exact on the pass that
     * presents one band -- which is every app frame this row has ever printed, `nb=1` -- and
     * reads low on a multi-band pass, which `nb=` names.  `rst=` is signed on purpose: a
     * negative value means a column came from a different pass, and that is worth seeing rather
     * than being clamped to zero, the same law the pass' wrapped stages read by. */
    const int rst = (int)s_cycle.r_us - (int)s_cycle.st.comp_us
                  - (int)s_cycle.st.overlay_us;

    ps2_kprintf("[XMBT] cycle %u period=%u us pass=%u tail=%u hold=%u/%d | "
                "poll=%u hook=%u evt=%u net=%u paint=%u note=%u | "
                "render=%u swap=%u upload=%u px=%u nb=%d full=%d why=%x dmg=%d,%d,%d,%d/%d | "
                "comp bg=%u frame=%u paint=%u blit=%u panel=%u bars=%u total=%u ovl=%u rst=%d "
                "bfn=%u wins=%u/%u "
                "body=%upx skip=%upx\n",
                (unsigned int)s_cycle.rows,
                (unsigned int)(s_cycle.pass_us + s_cycle.tail_us + s_cycle.hold_us),
                (unsigned int)s_cycle.pass_us, (unsigned int)s_cycle.tail_us,
                (unsigned int)s_cycle.hold_us, asked_ms,
                (unsigned int)s_cycle.stage[0], (unsigned int)s_cycle.stage[1],
                (unsigned int)s_cycle.stage[2], (unsigned int)s_cycle.stage[3],
                (unsigned int)s_cycle.stage[4], (unsigned int)s_cycle.stage[5],
                (unsigned int)s_cycle.r_us, (unsigned int)s_cycle.s_us,
                (unsigned int)s_cycle.u_us, (unsigned int)s_cycle.px,
                s_cycle.bands, s_cycle.full, (unsigned int)s_cycle.why,
                s_cycle.bx0, s_cycle.by0, s_cycle.bx1, s_cycle.by1, s_cycle.ndmg,
                (unsigned int)s_cycle.st.bg_us, (unsigned int)s_cycle.st.frame_us,
                (unsigned int)s_cycle.st.paint_us, (unsigned int)s_cycle.st.blit_us,
                (unsigned int)s_cycle.st.panel_us, (unsigned int)s_cycle.st.bars_us,
                (unsigned int)s_cycle.st.comp_us, (unsigned int)s_cycle.st.overlay_us,
                rst, (unsigned int)s_cycle.st.bg_full_calls,
                (unsigned int)s_cycle.st.wins_drawn, (unsigned int)s_cycle.st.wins_walked,
                (unsigned int)s_cycle.st.frame_body_px,
                (unsigned int)s_cycle.st.frame_body_skip_px);
    /* The EE/GS layer's own row, and only when this pass sent a stream: a pass that painted no
     * layer has nothing to say, and a row of zeroes would read as "the GS is idle" rather than
     * "this frame was desktop only".  drop=/unsplit=/texmiss=/coverdrop= are the four that must
     * stay 0 -- each one is a piece of the picture that the application drew and the screen does
     * not have.  plate= is the fifth: it says whether the stream's first primitive group spans the
     * rect the environment packet scissors it to, which is what lets the window walk leave that
     * rect's plate unfilled in the canvas -- `covered`, or `SHORT` (the canvas shows through the
     * app's own picture), or `none` (a frame that recorded no vertex group at all).
     *
     * `pchg=` is the PRIM re-declarations this frame sent, and it is on this row because it is what
     * `groups=` is made of: each re-declaration closes a group, and `EEGS_MAX_GROUPS` -- 256, checked
     * against LANE 10's ring capacity -- is the budget those groups are poured into.  A `pchg=` near
     * `groups=` says the boundaries, not the vertices, are the term to attack, and `drop=` above is
     * what outrunning the budget costs.  LANE 12e's four `chain_*` columns are gone with the rung
     * they measured; its verdict is in doc/txt/PS2.txt, and a re-lived strip continuation shows up
     * here as `verts=` leaving 3.00 x `prims=` without needing new columns.
     *
     * `batch=`/`slow=` are LANE 12f rung (ii)'s two exits, and they are on this row because the fast
     * one does not clip: a retained mesh whose mapped pixels the GS can represent is recorded as one
     * strip and the GS's own SCISSOR cuts whatever leaves the rect, while any mesh with a point past
     * the vertex word's range is drawn by the immediate-mode walk that clips.  The ratio is the only
     * headless evidence that that gate is refusing the frames it should and letting through the ones
     * it should not -- `slow=` climbing on a window that never resized says the gate is turning work
     * away, and `batch=` beside a sheet missing an edge says the clip is not.  Neither is a
     * substitute for the picture, which is the user's to observe.
     *
     * `why=` names WHICH condition made the last `slow=` (0 none, 1 a null argument, 2 the model-
     * projection product not this window's yet, 3 lighting, 4 TEXTURE_2D bound and enabled, 5 a W
     * that depends on the vertex, 6 a projection that couples X to Y, 7 retired -- a mapped point
     * outside the clip rect, which used to refuse and is now the GS's job, 8 a map that was never
     * captured, 9 a mapped point outside the -2048..+2047 px the vertex word carries).  It was added
     * because the batch path's first live run printed `batch=0 slow=1` on every frame and the pair
     * could not say whether the mesh left the window or the app's GL state was not in the shape a
     * two-constant map can represent -- and those two answers send the work to different files.  The
     * second run answered `why=7`, which sent the work to `xmb.c`: the sheet is projected with the
     * window's 776x536 and drawn into the client's 768x500, so its last column is 8 px outside on
     * every frame, and a gate that refuses on that never batches the ribbon at all.
     *
     * `over=` is what that column became: the points of this frame's TAKEN batches that lie outside
     * the clip rect and inside the vertex word, i.e. the clipping the GS is being asked to do.  The
     * ribbon's 32 a frame is the row's standing proof that the SCISSOR is exercised and not merely
     * trusted -- nonzero `over=` with the sheet's right edge intact is the fast path working, and
     * nonzero `over=` with 8 px of sheet painted over the window's border is the same number saying
     * the register does not mean what its documentation says it does.
     *
     * `colchg=/verts=` is LANE 19 rung 11, and it exists because rung 10's page-as-picture found a
     * client that is ONE colour on the GS page -- 270,436 px of the gradient's top-left corner, with
     * flat blocks where the icons should be -- and `in_win=` cannot see that, because it counts
     * pixels and not colours.  The two readings have opposite remedies, so the row names which one
     * this frame is: a `colchg` of the same order as `verts` says the per-vertex colours reached the
     * stream and the GS is not interpolating them (IIP in PRIM, and the state around it), while
     * `colchg=0` with `c0` equal to `cN` says the application's colours never got here at all.
     * `c0=`/`cN=` are the first and last colour words of the frame, in the vertex layout -- r in
     * bits 7:0 -- not the framebuffer's, so read them against `gs_vertex()` and not against the
     * [GSDUMP] legend.
     *
     * `iip=/decl=` is the same rung's second half and the direct answer: of the PRIM words the frame
     * declared, how many set the GS's Gouraud bit.  Since rung 12v the recorder hands every vertex chain a
     * write of its own PRIM register in front of it, so these declarations are what the rasteriser was
     * asked for at the source and `pwr=` is what reached it.  `iip=0/25` beside a `colchg` of a thousand
     * says the flat page is this port asking for flat shading, and the GS environment is not the suspect.
     * `tme=` is the same reading for texture mapping, which rung 10 also found absent: the icons arrived as
     * flat blocks of one grey.
     *
     * `colchg=` is a FRAME total, and that is the hole rung 12b fills.  The XMB background is 15
     * vertices of a 4,138-vertex frame, so `colchg=1852/4138` says nothing about it: the ratio is the
     * same whether the gradient's triangles each carry three corner colours or each carry one.  The
     * `wide=`/`big=` segment selects by AREA instead, which picks the background out for free because
     * it is the only layer whose triangles are big.  `wide=n/m` is m triangles of at least 32,768
     * doubled px and n of them arriving with all three colours equal; `big=` is the doubled area of
     * the widest one and `c=` the three colours it was handed, with `sm=` the IIP it asked for.  Read
     * it this way: three different words with `sm=1` and a flat page on the [GSDUMP] is the GS not
     * interpolating, and the suspect is the packet or the environment; one word in all three places
     * is the EE, and the clipper's colour plumbing is.  Which of the two this frame is belongs to the
     * run that reads this row, not to the paragraph that grew it.
     *
     * `pwr=` is rung 12v's own column and the one that certifies the fix is on the bus: the recorder writes
     * PRIM as an AD group in front of every vertex chain, because rung 12u measured that a tag's PRIM field
     * with PRE set never programs that register on this GS.  One write per chain, so `pwr=` is the frame's
     * count of chains and `groups - pwr` is the groups that move state instead of geometry -- the texture
     * switches `texsw=` names.  `pwr=0` beside `verts>0` would be a stream of chains that went out with no
     * primitive named in front of any of them, which is the shape that drew the XMB as rectangles. */
    if (s_gs_groups || s_gs_empty_stream) {
        ps2_kprintf("[GSTS] groups=%u pwr=%u qw=%u verts=%u prims=%u gs=%u us | "
                    "pchg=%u batch=%u slow=%u why=%u over=%u | "
                    "drop=%u unsplit=%u texsw=%u texmiss=%u | colchg=%u/%u iip=%u/%u tme=%u "
                    "c0=%08x cN=%08x | wide=%u/%u big=%u c=%08x/%08x/%08x sm=%u | "
                    "cover=%u coverdrop=%u covermerge=%u emptystream=%u plated=%u px=%u | "
                    "clip=%ux%u b0=%d,%d,%d,%d plate=%s\n",
                    (unsigned int)s_gs_groups, (unsigned int)s_gs_st.prim_writes,
                    (unsigned int)s_gs_qw,
                    (unsigned int)s_gs_st.verts, (unsigned int)s_gs_st.prims,
                    (unsigned int)s_gs_us,
                    (unsigned int)s_gs_st.prim_moves,
                    (unsigned int)s_gs_st.grid_batch, (unsigned int)s_gs_st.grid_slow,
                    (unsigned int)s_gs_st.grid_why,
                    (unsigned int)s_gs_st.grid_over,
                    (unsigned int)s_gs_st.dropped,
                    (unsigned int)s_gs_st.unsplittable, (unsigned int)s_gs_st.tex_switches,
                    (unsigned int)s_gs_st.tex_missing,
                    (unsigned int)s_gs_st.colour_changes, (unsigned int)s_gs_st.colour_verts,
                    (unsigned int)s_gs_st.prim_iip, (unsigned int)s_gs_st.prim_decl,
                    (unsigned int)s_gs_st.prim_tme,
                    (unsigned int)s_gs_st.colour_first, (unsigned int)s_gs_st.colour_last,
                    (unsigned int)s_gs_st.wide_flat, (unsigned int)s_gs_st.wide_tri,
                    (unsigned int)s_gs_st.big_area,
                    (unsigned int)s_gs_st.big_c0, (unsigned int)s_gs_st.big_c1,
                    (unsigned int)s_gs_st.big_c2, (unsigned int)s_gs_st.big_smooth,
                    (unsigned int)s_gs_cover, (unsigned int)s_gs_cover_drop,
                    (unsigned int)s_gs_cover_merge,
                    (unsigned int)s_gs_empty_stream,
                    (unsigned int)s_gs_plated_passes, (unsigned int)s_gs_plated_px,
                    (unsigned int)s_gs_st.clip_w, (unsigned int)s_gs_st.clip_h,
                    (int)s_gs_st.b0_l, (int)s_gs_st.b0_t,
                    (int)s_gs_st.b0_r, (int)s_gs_st.b0_b,
                    s_gs_st.has_bbox ? (s_gs_st.plate_covers ? "covered" : "SHORT") : "none");

        /* LANE 20 rung 22: the frame broken into its layers, one row per PRIM declaration.
         *
         * Rung 21b's map of the XMB's client came back 48.8% the word 0x00000000 and the rest
         * horizontal dashes one pixel tall, and no frame total can explain that.  `colchg=1852/4138`
         * says colours vary SOMEWHERE in the frame; it cannot say whether the chain that carries the
         * gradient is textured, whether it blends, or how many vertices it has.  Those answers sit in
         * different files -- a chain with TME set and a plane that is not the one its texels came
         * from is `eegs_texture()`'s, one with ABE clear over a sheet with transparent margins is the
         * blend's, one whose OP is not TRI is `prim_word()`'s -- so the row names them per chain:
         * the decoded PRIM word, the geometry and box it reaches, and the colours at its ends.
         *
         * Three frames, then silence: LANE 12h measured this stream near-invariant frame to frame, so
         * a fourth row is a fourth copy of one layer table. */
        {
            const uint32_t nc = eegs_chain_count();
            uint32_t i;

            if (nc && s_gschain_printed < 3u) {
                s_gschain_printed++;
                ps2_kprintf("[GSCHAIN] frame %u: %u chain(s) closed, %u past the %u-entry table, "
                            "limit=%u refused=%u.  "
                            "op 0=POINT 1=LINE 2=LINESTRIP 3=TRI 4=TRISTRIP 5=TRIFAN 6=SPRITE.  "
                            "bbox is local px, the client's own origin, so read it against the "
                            "[GSLIVE] client= rect.  c0/cN are VERTEX colours (r in bits 7:0).\n",
                            (unsigned int)s_gschain_printed, (unsigned int)nc,
                            (unsigned int)eegs_chain_dropped(), (unsigned int)EEGS_CHAIN_MAX,
                            (unsigned int)eegs_chain_limit(),
                            (unsigned int)eegs_chain_refused());
                for (i = 0u; i < nc; i++) {
                    const eegs_chain_rec *c = eegs_chain_at(i);
                    const uint32_t p = c->prim;

                    ps2_kprintf("[GSCHAIN] %2u op=%u iip=%u tme=%u abe=%u ctxt=%u | "
                                "verts=%u prims=%u | bbox=%d,%d..%d,%d %dx%d | c0=%08x cN=%08x\n",
                                (unsigned int)i, (unsigned int)(p & 7u),
                                (p >> 3) & 1u, (p >> 4) & 1u, (p >> 6) & 1u, (p >> 9) & 1u,
                                (unsigned int)c->nverts, (unsigned int)c->nprims,
                                (int)c->l, (int)c->t, (int)c->r, (int)c->b,
                                c->l <= c->r ? (int)(c->r - c->l + 1) : 0,
                                c->l <= c->r ? (int)(c->b - c->t + 1) : 0,
                                (unsigned int)c->c0, (unsigned int)c->clast);
                }
            }
        }
    }

#if defined(BTRON_PS2_GS_IDENT) && BTRON_PS2_GS_IDENT
    /* LANE 12h's deciding row, and it answers exactly one question: is the stream this frame
     * recorded the byte string the previous presented frame recorded?  Read it against the lane,
     * not as a timing -- `sameg=`/`sameqw=` are the groups and quadwords a replay could re-send
     * where they sit, `diffg=`/`diffqw=` the ones that moved, and `at=` names the indices that
     * differ so they land against the `g=` column the app's `[XMBT]` phase markers print (the groups
     * staged by that boundary), which is what makes "the menu is invariant" a reading rather than an
     * inference.  `tailg=` is the groups only one of the two frames has: the compare is positional
     * on purpose, because a span that shifted index is a span a replay would have to rebuild, so a
     * frame that gained one group reads as everything after it in the tail rather than as a match.
     * `n=1` has no predecessor and is not evidence.  Its error is one-directional -- identical bytes
     * always hash identical, so `same=` can only be wrong by a collision while `diff=` is exact.
     *
     * `xyz=`/`rgb=`/`st=`/`shape=` are the same positional compare split by GS register, over the
     * frame's `v=` vertex groups (since rung 12v, `g=` minus `v=` is the AD groups: the frame's texture
     * switches plus one write of PRIM in front of every chain, and `pwr=` on the [GSTS] row is that second
     * term), and they are the columns
     * that say WHAT moved rather than only that something did: a group counted in `xyz=` had every
     * one of its drawing kicks byte-identical to the same index last frame, so its geometry is the
     * same mesh re-shaded, and a group counted in none of them is a vertex transform being re-solved.
     * `shape=` folds the DIFFERENCE between consecutive vertices instead of the coordinate, so a
     * group counted there is a rigid TRANSLATION of last frame's -- the reading that tells a moved
     * mesh from a re-solved one, and the one that decides whether a retained mesh plus a per-group
     * offset can carry the frame at all.  Four answers, four different files to change.  AD groups --
     * a texture switch -- carry no such registers and are in none of the four, so compare these
     * against `v=` rather than against `g=`. */
    if (s_gs_groups) {
        ps2_kprintf("[GSTID] n=%u g=%u v=%u prev=%u | same=%u g %u qw | diff=%u g %u qw | "
                    "xyz=%u rgb=%u st=%u shape=%u | tail=%u g %u qw | at=%s first=%u last=%u more=%u\n",
                    (unsigned int)s_gs_st.ident_frames, (unsigned int)s_gs_groups,
                    (unsigned int)s_gs_st.ident_vertex_groups,
                    (unsigned int)s_gs_st.ident_groups_prev,
                    (unsigned int)s_gs_st.same_groups, (unsigned int)s_gs_st.same_qw,
                    (unsigned int)s_gs_st.diff_groups, (unsigned int)s_gs_st.diff_qw,
                    (unsigned int)s_gs_st.ident_xyz, (unsigned int)s_gs_st.ident_rgb,
                    (unsigned int)s_gs_st.ident_st, (unsigned int)s_gs_st.ident_shape,
                    (unsigned int)s_gs_st.tail_groups, (unsigned int)s_gs_st.tail_qw,
                    s_gs_st.ident_frames > 1u ? eegs_present_ident_at() : "-",
                    (unsigned int)s_gs_st.diff_first, (unsigned int)s_gs_st.diff_last,
                    (unsigned int)s_gs_st.diff_more);
    }
#endif

    /* LANE 10's row, and it is the row the lane was opened for: `what has no instrument is the
     * sender's own blocked time`.  One profile since the last print -- and since boot for this
     * row's first appearance in a run, so a large first `blocked=` is an accumulated life, not one
     * pass -- which is why `blocked=` reads as this pass's microseconds rather than the machine's
     * whole life only from the second row on.  Read the three together:
     *
     *   `posted=` is what the presentation plane committed to the bus without waiting for it, and
     *     `drop=` is the S_FULL receipt -- the transfers it was refused because the GIF had not
     *     freed a descriptor.  With the ring bigger than the stage's group table this can only
     *     mean the consumer fell behind, and it has to read 0 (the fifth zero-column of the row
     *     above, of the same kind as drop=/unsplit=/texmiss=/coverdrop=).
     *   `blocked=` is the wait at the yield -- `eegs_present_begin()` reaping its own stage -- and
     *     `hold=` the longest a posted packet sat in the queue.  Both are the same quantity the
     *     old sender paid at every single transfer, now paid once per pass and counted.
     *   `sync=` is the half of VIOLATION 2 this increment did NOT take away: the canvas band and
     *     the boot path still block, because their buffer is the canvas and every console character
     *     writes it.  That number is what tells the next reader whether the remaining blocking is
     *     worth a second, wider ring -- and `gs=` above is the phase it lives in. */
    {
        ps2_gif_stats_t gif;

        ps2_gif_stats_take(&gif);
        if (gif.posted || gif.sync_sends) {
            ps2_kprintf("[GSTS] gif: posted=%u started=%u queued=%u inflight=%u cap=%u | "
                        "drop=%u hold=%u us | reap=%u blocked=%u us max=%u us | "
                        "sync=%u blocked=%u us max=%u us\n",
                        (unsigned int)gif.posted, (unsigned int)gif.started,
                        (unsigned int)gif.queued, (unsigned int)gif.in_flight,
                        (unsigned int)gif.cap, (unsigned int)gif.refused,
                        (unsigned int)gif.hold_max_us, (unsigned int)gif.waits,
                        (unsigned int)gif.wait_us, (unsigned int)gif.wait_max_us,
                        (unsigned int)gif.sync_sends, (unsigned int)gif.sync_us,
                        (unsigned int)gif.sync_max_us);
        }
    }
    s_cycle.rows++;
    s_cycle.stamped = 0u;
    if (s_cycle.left) s_cycle.left--;
}

/* The hold the body asked for is timed across the pass it completes: begin() returns the
 * entry tick, or 0 when nothing is owed, so an unprofiled delay costs one test.  The
 * pass' tail -- its own closing delay and the two shown-position stores -- is the gap
 * between the pass' last tick and that entry tick, which is why it is measured here
 * rather than by a boundary inside ps2_gui_pass(). */
static uint32_t ps2_cycle_hold_begin(void)
{
    return s_cycle.stamped ? ps2_count_read() : 0u;
}

static void ps2_cycle_hold_end(uint32_t t0, int asked_ms)
{
    const uint32_t tpu = btron_ee_ticks_per_us();

    if (t0 == 0u || !s_cycle.stamped || tpu == 0u) return;
    s_cycle.tail_us = (uint32_t)(t0 - s_cycle.t_end) / tpu;
    s_cycle.hold_us = (uint32_t)(ps2_count_read() - t0) / tpu;
    ps2_cycle_print(asked_ms);
}

/* One repaint of one present region, timed by stage.  The stages are never folded
 * into one number because they have different cures: the render walks the damaged
 * part of the desktop, the swap is the byte-order pass over it, and the upload is
 * the GIF stream that carries it to the GS.  The swap now costs nothing and measures
 * near zero, because the canvas is already in the GS's own byte order --
 * BTRON_PIXEL_FB() performs the exchange as each colour is drawn
 * (include/btron/types.h) -- and this row is the instrument that says so.
 *
 * Both callers use it -- the loop, and the cold paint that 'startx' performs on its
 * own, which is the attribution that needs no hand protocol at all.  The loop's bands
 * and ps2_bench_run() hand it the region's own extents, so the two agree by
 * construction and the bench's rows describe the present the desktop makes rather
 * than a second one. */
/* The one difference between `cover minus layer` and `cover`: the layer's rect.  A cover rect is
 * re-uploaded *inside* the layer because that is where the stream buried it; outside the layer the
 * band's own upload already put it there, and a primitive cannot reach past SCISSOR to bury it
 * again.  So each cover rect is intersected with the client rect and the piece that survives is one
 * ps2_gs_upload(). */
static void ps2_upload_cover_in_layer(const wnd_gl_cover *cover, const RECT *layer)
{
    int i;

    for (i = 0; i < cover->n; i++) {
        const RECT *r = &cover->r[i];
        const H x0 = r->left   > layer->left   ? r->left   : layer->left;
        const H y0 = r->top    > layer->top    ? r->top    : layer->top;
        const H x1 = r->right  < layer->right  ? r->right  : layer->right;
        const H y1 = r->bottom < layer->bottom ? r->bottom : layer->bottom;

        if (x1 > x0 && y1 > y0) {
#if BTRON_PS2_BENCH
            /* Recorded BEFORE the send.  A cover rect goes out only from the `if (gs_layer)` branch
             * below, so its stream flag is always true -- which is the whole difference between this
             * kind and PS2_UPLOAD_CANVAS: a cover lays pixels the composite really drew above the
             * layer, and it is followed by nothing because the stream already went under it. */
            ps2_upload_trace((int)x0, (int)y0, (int)x1, (int)y1, PS2_UPLOAD_COVER, 1,
                             PS2_WHY_COVER);
#endif
            ps2_gs_upload(x0, y0, x1 - x0, y1 - y0);
        }
    }
}

#if BTRON_PS2_BENCH_GS >= 5
/* LANE 20 rung 25 (BENCH_GS=5): the frame's own stage, read at the word level, on the seam where
 * the words are still the ones that went on the bus.
 *
 * The [GSCHAIN] table can only report the ASK: its bbox is built from the `x_px`/`y_px` integers
 * `emit_vertex()` was handed, and the words the GS sees are made downstream of that -- by
 * `gs_rec_vertex()`'s packing and by the group's own tag.  The client's ink stops at local y=47
 * while the ask says 500, and the two candidate authors are one seam apart: a bad ask, or a packet
 * whose NLOOP/NREG/register stride puts a vertex somewhere other than where the recorder wrote it.
 * A tag's fields decide the second directly, so this prints them, and prints the XYZ2 register of
 * each group's first vertices with both 16-bit fields decoded back to the pixel each claims --
 * `px = (field >> 4) - 2048`, the arithmetic the +2048 page bias in gs_vertex_coord16() implies.
 *
 * Four groups, because groups made from chains 0 and 1 are the clear and the gradient and the
 * first icon quad is what the map says DOES land in place: one row therefore separates "the
 * whole-client TRI shape is wrong" from "the stream's framing is wrong" without a second run.
 *
 * It is called from the send, not from the cycle printer, because the stage belongs to the frame
 * that was just presented: the next paint pass calls `eegs_present_begin()`, which resets the group
 * count, and a table printed later would report zero groups -- which is what the first attempt at
 * this instrument measured, and is a fact about when it asked, not about the stream. */
static void ps2_bench_dump_groups(void)
{
    static uint32_t s_25_frames;
    const uint32_t ng = eegs_present_groups_sent();
    uint32_t g;

    if (s_25_frames >= 3u) return;
    s_25_frames++;

    ps2_kprintf("[GS25] frame %u: %u group(s) on the bus.  A GIF quadword is 16 bytes = 2 u64 "
                "registers; half 0 is the tag, half 1 the REGS list.  XYZ2 field -> ask px is "
                "(field>>4)-2048.  nreg 2 = RGBAQ+XYZ2, 3 = RGBAQ+ST/UV+XYZ2.\n",
                (unsigned int)s_25_frames, (unsigned int)ng);

    for (g = 0u; g < ng && g < 4u; g++) {
        static uint64_t w[2u * 16u];
        const uint32_t got = eegs_present_group_peek(g, w,
                                        (uint32_t)(sizeof w / sizeof w[0]));
        uint32_t nloop, nreg, flg, k, pos = 0u, nv;

        if (got < 2u) {
            ps2_kprintf("[GS25]  g%u: %u word(s), no tag pair staged\n",
                        (unsigned int)g, (unsigned int)got);
            continue;
        }
        nloop = (uint32_t)(w[0] & 0x7FFFu);
        flg   = (uint32_t)((w[0] >> 58) & 3u);
        nreg  = (uint32_t)((w[0] >> 60) & 0xFu);
        for (k = 0u; k < nreg && k < 10u; k++) {
            if (((w[1] >> (4u * k)) & 0xFu) == GS_NIB_XYZ2) pos = k;
        }
        ps2_kprintf("[GS25]  g%u tag nloop=%u eop=%u pre=%u flg=%u nreg=%u "
                    "prim(op=%u iip=%u tme=%u abe=%u) | u64words=%u\n",
                    (unsigned int)g, (unsigned int)nloop,
                    (unsigned int)((w[0] >> 15) & 1u),
                    (unsigned int)((w[0] >> 46) & 1u), (unsigned int)flg,
                    (unsigned int)nreg,
                    (unsigned int)((w[0] >> 47) & 7u),
                    (unsigned int)((w[0] >> 50) & 1u),
                    (unsigned int)((w[0] >> 51) & 1u),
                    (unsigned int)((w[0] >> 53) & 1u),
                    (unsigned int)got);
        if (flg != GS_FLG_IMAGE && nreg != 0u) {
            nv = (nloop < 4u) ? nloop : 4u;
            for (k = 0u; k < nv; k++) {
                const uint32_t idx = 2u + k * nreg + pos;
                uint32_t xf, yf;

                if (idx >= got) break;
                xf = (uint32_t)(w[idx] & 0xFFFFu);
                yf = (uint32_t)((w[idx] >> 16) & 0xFFFFu);
                ps2_kprintf("[GS25]      v%u X=%04x Y=%04x -> ask %d,%d  Z=%08x\n",
                            (unsigned int)k, (unsigned int)xf, (unsigned int)yf,
                            (int)(xf >> 4) - 2048, (int)(yf >> 4) - 2048,
                            (unsigned int)(w[idx] >> 32));
            }
        }
    }
}

/* LANE 20 rung 26: is the missing ink missing, or merely late?
 *
 * Rung 25 certified the two whole-client chains as bytes: the tag says TRI, NLOOP=6, NREG=2, the AD
 * group before each one writes the same PRIM, and the XYZ2 words decode back to the ask the recorder
 * was given -- 0,0 / 768,0 / 768,500.  So neither the ask nor the packet can be the author of a page
 * whose stream ink stops at local y=47, and the one remaining candidate upstream of the census is
 * WHEN the census looks.
 *
 * A GIF transfer finishing is not a primitive being drawn.  The EE's `busy` poll reads DMAC channel
 * 2's STR bit, which says the bytes left the EE; the GS then takes them out of its own FIFO and
 * rasterises them, and `ps2_gs_vram_read()`'s Local<-Host request goes in through VIF1 (channel 1),
 * a different road into the same machine.  If the GS is still working through a frame's primitives
 * when the read arrives, the read sees a page that is half-painted and the census calls the rest of
 * it "unwritten" -- which is exactly what a top band with a straight lower edge looks like.  And the
 * same lag seen from the CRTC instead of from the census is the flicker the operator reports, so
 * either answer here is worth having.
 *
 * The probe therefore reads the same six small boxes twice and prints both counts: once as soon as
 * the ring is empty (the read's own precondition, so "as late as the EE can make it without
 * waiting"), and once after a vsync, which is the only settle this port has that cannot be undone
 * by the EE's clock.  Six boxes down the client column, the top two being the positive control --
 * ink there in both passes is the harness proving it can see a painted row, and a row that gains
 * ink across the wait is the race, measured in pixels rather than argued.
 *
 * Two frames turned into fourteen: frame 1 carries the wait, and every frame after it is one settled
 * read on one row, because the answer at the send came back "the page was already complete" and the
 * question that is left is what the same boxes hold by the time the census reads them.  The `cut=`
 * column says whether this pass let the canvas lay over the client rect or skipped it, which is the
 * only difference between the two present orders this lane has been arguing about.
 *
 * Forcing the settle here is a perturbation and is owned as one: the pipe becomes synchronous for
 * these fourteen passes, so the rows measure what the GS wrote, not how late the EE could get a look
 * at it. */
#define GS26_BW  64u                        /* a read's width, and a multiple of the 4 px the */
#define GS26_BH  16u                        /* Local<-Host laws ask for                     */
#define GS26_N   6u
#define GS26_FRAMES 14u                     /* enough to cover the frames the census reads on */
#define GS26_SENT 0x00FF00FFu               /* GSREAD_SENT; its own define lives far below this
                                               function, and a fill word must not survive a read */

static const uint16_t s_26_local[GS26_N] = { 4u, 40u, 96u, 200u, 320u, 460u };

/* The column every rung-26/27 box is read out of, decided once so the census's cross-check can name
 * the same pixels the send did rather than recompute an offset that may have drifted. */
static int s_26_x = -1;

/* Force the GIF quiet, bounded the way every other spin in this port is.  Returns 1 if it never
 * settled.  `drain()` deliberately returns the moment a transfer is on the bus rather than waiting
 * for it, so one call is not a settle: it empties the queue and leaves the last group still
 * transferring. */
static uint32_t ps2_bench_ink_settle(void)
{
    uint32_t spins;

    for (spins = 0u; spins < 100000u; spins++) {
        (void)ps2_gif_drain();
        (void)ps2_gif_reap();
        if (ps2_gif_pending() == 0u && !ps2_gif_hw_busy()) break;
    }
    return (ps2_gif_pending() != 0u || ps2_gif_hw_busy()) ? 1u : 0u;
}

/* Count one box of an image that is already in main memory -- the census's own page buffer, or the
 * EE canvas -- with the same alpha law the VRAM read uses, so the two columns of rung 27's table are
 * the same measurement taken through two different roads.
 *
 * Counted by ALPHA, not by "any ink": the canvas that went out earlier in the pass fills the same
 * rect with 0xff______ whenever this build did not cut it, so a nonzero count says nothing about the
 * stream.  A=0xfe is the stream's own byte -- rung 21c established no other writer on this path
 * produces it -- and 0x00000000 is either a word nobody has written or a word the stream itself
 * wrote as its plate, which is what rung 27's canvas column separates those two readings apart.
 *
 * Rung 28 adds the fourth column, and it is not a refinement: a brightened pixel stores the vertex's
 * own As ([GS28]: "a blended fragment stores its own alpha in the page"), so the wave's px carry
 * A=9-ish, which is neither 0xfe nor 0xff nor zero.  Counting only `fe` would read a successfully
 * brightened ribbon as an unwritten page and make the fix look like the bug.  `br` is that ink: any
 * nonzero alpha below 0xfe, which no writer on this path produces except a blend whose As arrived. */
static void ps2_bench_ink_one(uint32_t v, uint32_t *fe, uint32_t *ff, uint32_t *z, uint32_t *br)
{
    if (v == 0u || v == GS26_SENT) (*z)++;         /* sentinel = a px the read under-delivered */
    else if ((v >> 24) == 0xfeu) (*fe)++;
    else if ((v >> 24) == 0xffu) (*ff)++;
    else if ((v >> 24) != 0u) (*br)++;
}

static void ps2_bench_ink_tally(const uint32_t *img, uint32_t stride, int x, int y,
                                uint32_t *fe, uint32_t *ff, uint32_t *z, uint32_t *br)
{
    uint32_t j, i, nfe = 0u, nff = 0u, nz = 0u, nbr = 0u;

    for (j = 0u; j < GS26_BH; j++) {
        const uint32_t *row = img + (size_t)(y + (int)j) * (size_t)stride + (size_t)x;

        for (i = 0u; i < GS26_BW; i++) ps2_bench_ink_one(row[i], &nfe, &nff, &nz, &nbr);
    }
    *fe = nfe; *ff = nff; *z = nz; *br = nbr;
}

/* One silent read of the six boxes, after the caller has settled the bus.  A whole box is 1024
 * words; 0xFFFFFFFF in a column means that box's read failed and the row says so as `-`.
 *
 * The column is decided on the first call and kept: the send-side probe and the census's cross-check
 * have to name the SAME pixels or the two columns of rung 27 are not the same measurement. */
static void ps2_bench_ink_boxes(const RECT *cl, uint32_t *fe, uint32_t *ff, uint32_t *z,
                                uint32_t *br)
{
    static uint32_t s_26_box[GS26_BW * GS26_BH] __attribute__((aligned(16)));
    uint32_t j, i;

    if (s_26_x < 0) {
        s_26_x = cl->left + 8;
        if (s_26_x + (int)GS26_BW > cl->right) s_26_x = cl->right - (int)GS26_BW;
        if (s_26_x < cl->left) s_26_x = cl->left;
        /* A read lands on quadword boundaries, so the column must be a multiple of 4 px. */
        s_26_x -= s_26_x % 4;
    }

    for (j = 0u; j < GS26_N; j++) {
        const int y = cl->top + (int)s_26_local[j];
        uint32_t nfe, nff, nz, nbr;

        for (i = 0u; i < GS26_BW * GS26_BH; i++) s_26_box[i] = GS26_SENT;
        (void)ps2_gs_vram_read(s_26_x, y, (int)GS26_BW, (int)GS26_BH, s_26_box);
        if (ps2_gs_vram_read_err() != GS_READ_BUS_OK) {
            fe[j] = ff[j] = z[j] = br[j] = 0xFFFFFFFFu;
            continue;
        }
        ps2_bench_ink_tally(s_26_box, GS26_BW, 0, 0, &nfe, &nff, &nz, &nbr);
        fe[j] = nfe; ff[j] = nff; z[j] = nz; br[j] = nbr;
    }
}

/* Rung 27's first half: the same six boxes read at the live-page census's FIRST act, before it
 * allocates, uploads a strip, draws a control or reads a page.  Held silently -- a print here would
 * repaint the console band between the census and the page it is about to read, which is the law
 * rung 12r was built to keep. */
static uint32_t s_27_fe[GS26_N], s_27_ff[GS26_N], s_27_z[GS26_N], s_27_br[GS26_N];
static uint32_t s_27_census[GS26_N][4];  /* the census's own road: [box][fe, ff, z, br] */
static uint32_t s_27_cov[GS26_N];        /* px of that box the walk reached, whatever their alpha */
static uint32_t s_27_settle_failed;
static int s_27_taken;
static uint32_t s_27_walk_on;             /* armed by the sampled census, discharged by the table */
static RECT s_27_rect;                  /* the client the boxes were placed in, kept for the walk */

static void ps2_bench_ink_at_census(const RECT *cl)
{
    uint32_t j;

    if (s_27_taken) return;                       /* one sample per census, on the first call's rect */

    s_27_taken = 1;
    s_27_rect = *cl;
    for (j = 0u; j < GS26_N; j++) {
        s_27_census[j][0] = s_27_census[j][1] = s_27_census[j][2] = s_27_census[j][3] = 0u;
        s_27_cov[j] = 0u;
    }
    s_27_settle_failed = ps2_bench_ink_settle();
    ps2_bench_ink_boxes(cl, s_27_fe, s_27_ff, s_27_z, s_27_br);
    s_27_walk_on = 1u;                            /* from here, this census's walk is the second road */
}

/* Rung 27's second road: the SAME six boxes, classified as the census's own whole-page walk passes
 * over them.  This is the column that used to be lifted out of `buf` after the read loop, which was
 * wrong twice over -- `buf` holds one 150-row band, not a page, so the slice was the heap (the same
 * bug that emptied the rung-21 maps), and the walk had already moved past those rows by the time a
 * slice could be taken.  Counting inside the walk costs one x comparison for 64 of 800 columns. */
static void ps2_bench_ink_census_px(uint32_t x, uint32_t y, uint32_t v)
{
    uint32_t j;

    if (!s_27_walk_on) return;
    if ((int)x < s_26_x || (int)x >= s_26_x + (int)GS26_BW) return;

    for (j = 0u; j < GS26_N; j++) {
        const int by = s_27_rect.top + (int)s_26_local[j];

        if ((int)y >= by && (int)y < by + (int)GS26_BH) break;
    }
    if (j == GS26_N) return;
    s_27_cov[j]++;
    ps2_bench_ink_one(v, &s_27_census[j][0], &s_27_census[j][1], &s_27_census[j][2],
                      &s_27_census[j][3]);
}

/* Rung 26's own read of the same column, at the send. */
static void ps2_bench_gs_race_probe(const RECT *cl, int cut)
{
    static uint32_t s_26_frames;
    uint32_t cfe[2u][GS26_N];
    uint32_t cz[2u][GS26_N];
    uint32_t cff[2u][GS26_N];
    uint32_t cbr[2u][GS26_N];
    uint32_t nz_fail[2u];
    uint32_t passes, pass, j;

    if (s_26_frames >= GS26_FRAMES) return;
    s_26_frames++;

    /* Frame 1 is the A/B that asks whether the census's page was simply read too early; every frame
     * after it is a single settled read, because the answer turned out to be "the page was finished"
     * and the next question is when that ink stops being there. */
    passes = (s_26_frames == 1u) ? 2u : 1u;

    for (pass = 0u; pass < passes; pass++) {
        /* The read refuses to start while anything is queued, so pass 0's wait is the wait the census
         * already pays -- and it is the boundary of the question: after it every byte of the frame
         * has been handed to the GS, and nothing says the GS has eaten them.  Pass 1 adds the only
         * settle this port has that the EE's clock cannot undo: a vsync. */
        if (pass == 0u) nz_fail[0u] = ps2_bench_ink_settle();
        else { ps2_gs_wait_vsync(); nz_fail[1u] = 0u; }

        ps2_bench_ink_boxes(cl, cfe[pass], cff[pass], cz[pass], cbr[pass]);
    }

    if (s_26_frames == 1u) {
        ps2_kprintf("[GS26] frame 1: box x=%d, 64x16 px, at client-local y", s_26_x);
        for (j = 0u; j < GS26_N; j++) ps2_kprintf(" %u", (unsigned int)s_26_local[j]);
        ps2_kprintf(".  fe = A=0xfe stream ink, ff = A=0xff upload, z = unwritten or undelivered, "
                    "br = A=1..0xfd, the alpha a BLEND stored ([GS28]) -- a brightened ribbon is `br`, "
                    "not `fe`; a whole box is 1024 words, -1 = the read failed.\n");
    }

    for (pass = 0u; pass < passes; pass++) {
        if (passes == 2u) ps2_kprintf("[GS26]  %s:  ", pass == 0u ? "bus empty, no wait "
                                                                   : "after one vsync ");
        else ps2_kprintf("[GS26] f%-2u cut=%u:                    ", (unsigned int)s_26_frames,
                         (unsigned int)cut);
        for (j = 0u; j < GS26_N; j++) {
            if (cfe[pass][j] == 0xFFFFFFFFu) ps2_kprintf("[  -   -   -   -] ");
            else ps2_kprintf("[%4u %4u %4u %4u] ", (unsigned int)cfe[pass][j],
                             (unsigned int)cff[pass][j], (unsigned int)cz[pass][j],
                             (unsigned int)cbr[pass][j]);
        }
        ps2_kprintf("\n");
    }

    if (passes == 1u) return;
    {
        uint32_t gained = 0u, same = 1u;

        for (j = 0u; j < GS26_N; j++) {
            if (cfe[0u][j] != cfe[1u][j] || cz[0u][j] != cz[1u][j] || cbr[0u][j] != cbr[1u][j])
                same = 0u;
            if (cfe[1u][j] > cfe[0u][j] || cbr[1u][j] > cbr[0u][j] || cz[1u][j] < cz[0u][j])
                gained = 1u;
        }
        ps2_kprintf("[GS26] verdict: %s\n",
                    (nz_fail[0u] || nz_fail[1u])
                        ? "SKIP -- the bus did not settle inside the spin bound, so the first read "
                          "is not a clean 'everything handed to the GS' sample"
                        : same
                            ? "identical -- the GS had already written every word it was going to "
                              "write when the census looked, so the missing rows were never rasterised"
                            : gained
                                ? "GAINED stream ink across the wait -- the census read a page still "
                                  "being drawn; the rows below are late, not lost"
                                : "CHANGED without gaining stream ink -- a settle moved the page, "
                                  "which is another writer, not a completion");
    }
}

/* LANE 20 rung 27: the same pixels, measured three ways at once, inside the live-page census.
 *
 * This rung was written to settle a contradiction that turned out to be an instrument bug: rung 26's
 * send-side probe found 1024/1024 words of A=0xfe stream ink in all six boxes on fourteen consecutive
 * frames, while the census's alpha map over the same client reported 11,714 cells of the word
 * 0x00000000 -- including the rows the deepest two boxes live in.  The map was reading past its own
 * band buffer (see `gspeak_rows`), so the two roads never disagreed about the page; they disagreed
 * because one of them was not looking at the page at all.
 *
 * The cross-check still earns its keep, because the bug it was built to catch is a real class: a
 * Local<-Host request 64 px wide and a request 800 px wide covering the same pixels are two different
 * geometries, and only a table that prints both says whether they return the same words.  So:
 *
 *   `read`   -- a 64x16 Local<-Host request of the box, taken at the census's FIRST act, before it
 *               allocates, uploads its strip, draws its control or reads its page;
 *   `walk`   -- the census's own 800-px-wide band reads, classified pixel by pixel as the walk passes
 *               the same coordinates;
 *   `canvas` -- the EE pixels a canvas upload would have laid there, which is what tells a genuinely
 *               unwritten page from a plate the present order put back.
 *
 * `read` vs `walk` differing is a read-geometry bug and makes every other map in this file a statement
 * about the read.  `read`=ink with `walk`=zero is a present that erased between the two.  A zero in
 * both over a coloured canvas word is a page nobody ever wrote. */
static void ps2_bench_ink_cross(const RECT *cl, uint32_t stride, const uint32_t *canvas)
{
    uint32_t j, n_agree = 0u, n_ink = 0u;

    if (!s_27_walk_on) return;              /* one table, for the one census that took the sample */

    ps2_kprintf("[GS27] one law, three roads, x=%d 64x16 px.  `read` = a Local<-Host request of this "
                "box, taken at the census's first act; `walk` = the same coordinates as this census's "
                "own 800-px-wide band reads passed them; `canvas` = the EE pixels a canvas upload "
                "would lay there.  read-vs-walk disagreeing is a read-geometry bug; walk-zero with a "
                "coloured canvas is an unwritten page; walk-zero with a zero canvas is the plate being "
                "re-laid; read=ink with walk=zero is a present that erased between the two.  "
                "fe=A=0xfe stream ink, ff=A=0xff upload, br=A=1..0xfd a blend that stored its own As, "
                "z=zero or undelivered; a whole box is 1024 px and the four columns sum to it.  The "
                "wave's px should live in `br` once the brighten law lands: a fix that moves ink from "
                "`fe` to `br` is a fix, not a regression.\n", s_26_x);
    if (s_27_settle_failed) {
        ps2_kprintf("[GS27] SKIP: the bus did not settle inside the spin bound, so the `read` column "
                    "is not a clean sample of the page the census found.\n");
    }

    for (j = 0u; j < GS26_N; j++) {
        uint32_t cfe = 0u, cff = 0u, cz = 0u, cbr = 0u;
        const int y = cl->top + (int)s_26_local[j];
        const int agree = (s_27_fe[j] == s_27_census[j][0]
                           && s_27_ff[j] == s_27_census[j][1]
                           && s_27_z[j] == s_27_census[j][2]
                           && s_27_br[j] == s_27_census[j][3]);

        ps2_bench_ink_tally(canvas, stride, s_26_x, y, &cfe, &cff, &cz, &cbr);
        if (s_27_fe[j] + s_27_br[j] == GS26_BW * GS26_BH) n_ink++;
        if (s_27_cov[j] == GS26_BW * GS26_BH && agree) n_agree++;

        if (s_27_fe[j] == 0xFFFFFFFFu) ps2_kprintf("[GS27] y=%-4d read[  -   -   -   -]", y);
        else ps2_kprintf("[GS27] y=%-4d read[%4u %4u %4u %4u]", y, (unsigned int)s_27_fe[j],
                         (unsigned int)s_27_ff[j], (unsigned int)s_27_z[j],
                         (unsigned int)s_27_br[j]);
        ps2_kprintf(" walk[%4u %4u %4u %4u] cov=%4u canvas[%4u %4u %4u %4u]%s\n",
                    (unsigned int)s_27_census[j][0], (unsigned int)s_27_census[j][1],
                    (unsigned int)s_27_census[j][2], (unsigned int)s_27_census[j][3],
                    (unsigned int)s_27_cov[j],
                    (unsigned int)cfe, (unsigned int)cff, (unsigned int)cz, (unsigned int)cbr,
                    s_27_z[j] == 0xFFFFFFFFu ? "  (read failed)"
                        : (s_27_cov[j] != GS26_BW * GS26_BH)
                            ? "  <== the walk did not reach every px of this box"
                        : agree ? "" : "  <== the two roads DISAGREE");
    }

    ps2_kprintf("[GS27] verdict: %u of %u boxes gave the SAME four counts down both roads, and %u of "
                "%u are 1024/1024 stream-ink px (fe + br) in `read`.  Until the first number is %u this "
                "table is a statement about the two read geometries, not about the page; once it is, the "
                "second says whether the GS layer's client is the stream's ink or a canvas upload's.\n",
                (unsigned int)n_agree, (unsigned int)GS26_N,
                (unsigned int)n_ink, (unsigned int)GS26_N, (unsigned int)GS26_N);
    s_27_walk_on = 0u;
}
#endif

/* One rect's worth of canvas onto the page, and the one seam that keeps the ring's record and the
 * send from disagreeing about what went out. */
static void ps2_upload_piece(int x0, int y0, int x1, int y1, int stream, uint8_t why)
{
    if (x1 <= x0 || y1 <= y0) return;
#if BTRON_PS2_BENCH
    ps2_upload_trace(x0, y0, x1, y1, PS2_UPLOAD_CANVAS, stream, why);
#else
    (void)stream;     /* the ring that records it is the bench build's; the send is the same */
    (void)why;
#endif
    ps2_gs_upload(x0, y0, x1 - x0, y1 - y0);
}

/* Rung 20y's memory, and it is a memory of a measurement rather than of a hope: `s_20y_plated` is set
 * only from the stats of a stream that actually went out and whose first vertex group provably spanned
 * the rect the environment packet scissors it to -- eegs_present_send()'s plate_covers, the same reading
 * that prints `plate=covered` on the [GSTS] row.  With that true, the layer's pixels belong to the
 * stream, the canvas holds no picture of them, and uploading the canvas inside the rect is a frame of
 * desktop grey on the page: the PCRTC scans page 0 while the GIF is still writing the client's 384,000
 * px, and the stream only overwrites it afterwards.  The rect therefore travels with the memory, and any
 * move or resize voids it -- stale ink in the wrong place is worse than grey in the right one.
 *
 * It is cleared on the same seam the cut is decided: a pass that sends no stream lays desktop inside the
 * layer (there is nothing left to protect those pixels), so the memory cannot survive a desktop pass and
 * come back to life under a stream that has not plated since. */
static RECT s_20y_rect;
static uint32_t s_20y_plated;
#if BTRON_PS2_BENCH_GS >= 3
static BOOL s_23_announced;         /* rung 23's A/B says once that it is armed */
#endif

/* The pass's own canvas upload, with a live layer's rect cut out of it when this pass is not going
 * to send a stream -- the refusal rung 20w names, and the reason it is arithmetic and not a policy:
 * the canvas holds no picture of the layer, so a canvas pixel inside that rect is a desktop pixel
 * wearing the window's place.
 *
 * A rect minus a rect is up to four rects: the strips above and below span the region's full width,
 * and the two beside the hole take only the hole's own rows.  A region that misses the layer costs
 * exactly one upload, the same call the uncut path makes, so the common band pays nothing.
 *
 * The GS's CT32 stream only accepts a column base that is a multiple of four and a width that is a
 * whole number of quadwords, and ps2_gs_upload() rounds both outward.  The hole's own columns are
 * therefore cut INWARD -- `hx0 & ~3` and `(hx1 + 3) & ~3` -- so rounding can only ever skip canvas
 * columns at the layer's edge, never lay canvas over them.  Skipping is the safe direction here:
 * those columns keep the ink the last stream put there. */
static void ps2_upload_canvas(int x0, int y0, int x1, int y1, const RECT *hole, int stream,
                              uint8_t why)
{
    int hx0 = 0, hy0 = 0, hx1 = 0, hy1 = 0;

    if (hole) {
        hx0 = hole->left   > x0 ? hole->left   : x0;
        hy0 = hole->top    > y0 ? hole->top    : y0;
        hx1 = hole->right  < x1 ? hole->right  : x1;
        hy1 = hole->bottom < y1 ? hole->bottom : y1;
        if (hx1 <= hx0 || hy1 <= hy0) hole = NULL;    /* the region misses the layer entirely */
    }

    if (!hole) {
        ps2_upload_piece(x0, y0, x1, y1, stream, why);
        return;
    }

    ps2_upload_piece(x0, y0, x1, hy0, stream, why);
    ps2_upload_piece(x0, hy1, x1, y1, stream, why);
    ps2_upload_piece(x0, hy0, hx0 & ~3, hy1, stream, why);
    ps2_upload_piece((hx1 + 3) & ~3, hy0, x1, hy1, stream, why);
}

/* ── Task #165: the paint worker, in the compositor's own terms ──────────────────────
 * The policy is in src/drivers/ps2/ps2_paint_worker.{h,c}; what lives here is the handshake, and
 * the two claims it can actually make on this machine.
 *
 * First, what this is NOT: there is no scheduler here.  `sta_tsk()` runs an app's body on the
 * caller's stack and `slp_tsk()` is a no-op (see the task-layer comment further down this file), the
 * EE has one core, and the app's body only ever invalidates its window -- the compositor paints.  So
 * "offload painting to a per-app worker" cannot mean "in parallel", and anything that claimed it did
 * would be a word, not a feature.  What a worker CAN mean on a single core is the two things the
 * compositor currently does not do for a GS layer:
 *
 *   (1) ONE paint per pass.  A layer window is exempt from the staleness rule that makes every
 *   other window paint once and get blited after that -- `must_repaint` gives it the callback
 *   whenever a band reaches it, because its picture is the stream rather than a pixmap.  On a pass
 *   whose damage splits into four bands and crosses the client, that is four identical XMB frames
 *   recorded, four streams staged, and four of them SENT.  The latch that fixes this is
 *   `wnd_gl_freeze()`: raised once the pass's first stream is on the page, and the rest of the walk
 *   treats the layer like a window whose image is valid.  Its rect is still cut out of every later
 *   canvas upload by `wnd_gl_layer_rect()`, which is built to answer on a pass that did not
 *   re-record, so the pixels the callback would have drawn are pixels the page already carries.
 *
 *   (2) A PRICE, and a frame that can refuse it.  Each window's paint is timed (this pass's render
 *   bracket is the whole of it) and folded into an estimate of the next one's cost.  A pass asks
 *   before it composites, and if the frame's remaining slice cannot afford the app's frame, the
 *   app's frame is not painted this pass -- the desktop keeps moving, the pointer keeps moving, and
 *   the application shows its last picture.  The starve override in the policy module is what makes
 *   that safe rather than a way to make a window disappear: an age the slice cannot explain forces
 *   the admission, and `forced=` counts it.
 *
 * The slice is `ui_period_us` minus what this pass has already spent (dispatch, the band decision)
 * minus what a present of this shape usually costs on the bus (the EWMA of the last passes' upload
 * column, which the worker does not control).  It is deliberately a rough number: the question is
 * "can this frame afford an XMB repaint", and the answer only has to be wrong by less than a frame.
 *
 * Compiled only with `PS2_PW=1`.  With the dial at 0 none of this is in the image -- the module is
 * not linked, the freeze is never raised, and the present path is the one every measurement in the
 * book was taken with. */
#if BTRON_PS2_PAINT_WORKER
static int      s_pw_on;            /* the runtime arm; seeded at session entry from the dial */
static uint32_t s_pw_upload_us;     /* EWMA of the pass's own bus column, the unschedulable part */
static const void *s_pw_layer;      /* the window this pass asked about, or NULL */
static int      s_pw_recorded;      /* a band of THIS pass has already sent the layer's stream */
static uint32_t s_pw_passes;        /* passes planned, for the row's own rate */
static uint32_t s_pw_defer_passes;  /* of those, the passes that refused the app */

static void ps2_pw_row(const ps2_pw_stats_t *p, int taken);

/* The one place the freeze is raised or cleared, so a pass that leaves it standing cannot make the
 * next one skip the app forever. */
static void ps2_pw_freeze(int frozen)
{
    if (!frozen) s_pw_recorded = 0;
    wnd_gl_freeze(frozen);
}

static uint32_t ps2_pw_us(uint32_t t0, uint32_t t1)
{
    const uint32_t tpu = btron_ee_ticks_per_us();
    return (s_timebase_ok && tpu) ? (t1 - t0) / tpu : 0u;
}

/* Decide this pass before its first band runs: how much of the frame is still unspent, whether the
 * layer's next frame fits in it, and how many bands would otherwise have recorded the same frame.
 * Called from ps2_gui_pass() with the band set already final, including rung 4's catch-up bands. */
static void ps2_pw_plan(const ps2_bandset_t *set, uint32_t spent_us)
{
    RECT   r;
    WND   *l;
    uint32_t touch = 0u, slice;
    unsigned i;
    const uint32_t now = btron_render_perf_us();
    const uint32_t period = btron_planes()->ui_period_us;

    ps2_pw_freeze(0);
    s_pw_layer = NULL;
    s_pw_passes++;
    if (!s_pw_on) return;

    l = wnd_gl_layer();
    if (!l || !wnd_gl_layer_rect(&r)) return;      /* no app on the page: nothing to schedule */

    for (i = 0; i < set->n; i++) {
        const ps2_band_t *b = &set->b[i];
        if (b->end <= r.top || b->top >= r.bottom) continue;
        if (b->x1 <= r.left || b->x0 >= r.right) continue;
        touch++;
    }
    if (!touch) return;                            /* this pass's damage misses the client */

    slice = spent_us + s_pw_upload_us < period ? period - spent_us - s_pw_upload_us : 0u;
    ps2_pw_pass_begin(slice, now);
    if (ps2_pw_ask(l, 1, now) == PS2_PW_PAINT) {
        s_pw_layer = (const void *)l;
        /* Claim (1), counted where it happens: the bands after the one that records the stream. */
        if (touch > 1u) ps2_pw_credit(touch - 1u);
    } else {
        /* Claim (2): nothing in this pass re-records, so the walk takes its blit path everywhere
         * and the canvas upload keeps the client cut out.  The page shows the app's last frame. */
        ps2_pw_freeze(1);
        s_pw_defer_passes++;
        ps2_cycle_why(PS2_CYCLE_WHY_PAINTW);
    }
}

void ps2_shell_paintw(const char *arg);
static void ps2_pw_apply(uint32_t on);   /* beside the prompt command that reads the same counters */
#endif /* BTRON_PS2_PAINT_WORKER */

static void ps2_paint_region(GDEV *screen, int x0, int y0, int x1, int y1, int full,
                             uint32_t *r_us, uint32_t *s_us, uint32_t *u_us)
{
    const uint32_t t0 = ps2_count_read();
    RECT gl_client;
    wnd_gl_cover gl_cover;
    int gs_groups = 0;
    int gs_layer = FALSE;

#if BTRON_PS2_BENCH
    const int s_paint_i = (int)(s_paint_calls < PS2_PAINT_TRACE_MAX ? s_paint_calls
                                                                    : PS2_PAINT_TRACE_MAX - 1u);
    if (s_paint_calls == 0u) {
        s_paint_union[0] = x0;  s_paint_union[1] = y0;
        s_paint_union[2] = x1;  s_paint_union[3] = y1;
    } else {
        if (x0 < s_paint_union[0]) s_paint_union[0] = x0;
        if (y0 < s_paint_union[1]) s_paint_union[1] = y0;
        if (x1 > s_paint_union[2]) s_paint_union[2] = x1;
        if (y1 > s_paint_union[3]) s_paint_union[3] = y1;
    }
    s_paint_rect[s_paint_i][0] = x0;  s_paint_rect[s_paint_i][1] = y0;
    s_paint_rect[s_paint_i][2] = x1;  s_paint_rect[s_paint_i][3] = y1;
    s_paint_full[s_paint_i] = full;
    s_paint_appended[s_paint_i] = 0;
    s_paint_appended_at[s_paint_i][0] = s_paint_appended_at[s_paint_i][1] = -1;
    s_paint_sig[s_paint_i] = -1;
    s_paint_pre[s_paint_i] = s_paint_post[s_paint_i] = -1;
    s_paint_pix[s_paint_i] = 0u;
    s_paint_calls++;
    s_cover_pass_no++;
#endif

    /* The GS layer's frame bracket.  The composite below runs the layer window's paint
     * callback, and that callback is what records the primitives, so recording has to be open
     * for the whole of it -- and nothing may reach the bus before the canvas does (the rule in
     * src/gl/eegs_present.h), which is why this begins rather than sends. */
    eegs_present_begin();

    if (full) {
        workbench_render(screen, PS2_SCREEN_WIDTH, PS2_SCREEN_HEIGHT);
#if BTRON_PS2_BENCH
        s_paint_appended[s_paint_i] = 2;   /* the canvas composite draws the sprite itself */
        {
            H fmx = 0, fmy = 0;
            get_baremetal_mouse_pos(&fmx, &fmy);
            s_paint_appended_at[s_paint_i][0] = (int)fmx;
            s_paint_appended_at[s_paint_i][1] = (int)fmy;
            s_paint_pix[s_paint_i] = (unsigned)(uintptr_t)screen->pixels;
            s_paint_sig[s_paint_i] = ps2_cursor_sig_at((const uint32_t *)screen->pixels,
                                                       (int)fmx, (int)fmy);
        }
#endif
    } else {
        /* The same composite clipped to the region.  workbench_render_damage()
         * leaves out the menu overlays and the sprite by design, and a banded pass
         * is only ever taken with every overlay closed -- so the cursor is put back
         * here, unconditionally: where the region does not cross the sprite the
         * write is a no-op, and where it does, the composite has just erased it and
         * this is what draws it again. */
        RECT d = { (H)x0, (H)y0, (H)x1, (H)y1 };

        if (wnd_damage_needs_paint(&d)) workbench_render_damage_paint(screen, &d);
        else                            workbench_render_damage(screen, &d);
        if (g_cursor_in_backbuffer) {
            H mx = 0, my = 0;
            get_baremetal_mouse_pos(&mx, &my);
#if BTRON_PS2_BENCH
            /* The signature read on both sides of the append, out of the very buffer the
             * append is given: post=0 while appended=1 means the writer did not write,
             * and pre=1 would mean the damage composite had not erased the old copy. */
            s_paint_pre[s_paint_i] = ps2_cursor_sig_at((const uint32_t *)screen->pixels, mx, my);
#endif
            draw_baremetal_mouse_cursor(screen, mx, my, PS2_SCREEN_WIDTH, PS2_SCREEN_HEIGHT);
#if BTRON_PS2_BENCH
            s_paint_post[s_paint_i] = ps2_cursor_sig_at((const uint32_t *)screen->pixels, mx, my);
            s_paint_appended[s_paint_i] = 1;
            s_paint_appended_at[s_paint_i][0] = (int)mx;
            s_paint_appended_at[s_paint_i][1] = (int)my;
            s_paint_pix[s_paint_i] = (unsigned)(uintptr_t)screen->pixels;
#endif
        }
#if BTRON_PS2_BENCH
        else {
            s_paint_appended[s_paint_i] = 0;
        }
#endif
    }

    /* The sprite is deliberately not a cover producer.  It writes its own white and black pixels
     * into the canvas and leaves the rest of its 16x16 box as the canvas had it -- and under a GS
     * layer the canvas holds no picture of the layer, so re-uploading that box would replace the
     * layer's own pixels with the desktop that used to be underneath.  The pointer is therefore
     * invisible while it is inside a GS window: increment 5c's documented limit, and 5d's first
     * job, which is a 16x16 CT32 cursor plane and one textured quad (doc/txt/PS2.txt). */

    /* Closes the recorder's last group into the staging buffer.  No DMA yet. */
    eegs_present_end();

    const uint32_t t1 = ps2_count_read();
    /* The byte-order stage, and it is empty.
     *
     * This bracket used to wrap a sweep -- one load and one R/B-exchanging store per
     * pixel of the damaged region, out of the desktop's canvas and into a second
     * framebuffer the GS driver owned.  The exchange now happens as each colour is
     * drawn, through BTRON_PIXEL_FB() (include/btron/types.h), and the GIF reads the
     * compositor's own array, so the sweep is gone along with the 49,094 us it cost
     * out of a 274,524 us whole-canvas present and the 1.92 MB of .bss it drew from.
     * The two reads are kept so this stage is still printed: `swap=0` on a full canvas
     * is the measurement that says nothing came back, and a non-zero value would mean
     * a conversion pass is live in the present path again. */
    const uint32_t t2 = ps2_count_read();

    /* The layer question, asked before the canvas answers it.
     *
     * `wnd_gl_layer_take()` says whether this composite re-recorded the layer, which is the only
     * case in which a GS stream is about to repaint the layer's rect -- and the canvas holds no
     * picture of that rect, because rung 9a makes the composite skip the body every frame (the
     * XMB's own row says `skip=384000px`).  So a canvas upload that lands inside it with no stream
     * behind it lays the desktop that used to be there over the picture the GS drew: grey where the
     * blue should be, and a stripe that flickers as a band crosses it.  The cure is arithmetic
     * rather than a policy -- upload the region minus the live layer's rect.
     *
     * Where a stream IS staged, the order rule of src/gl/eegs_present.h holds unchanged: the canvas
     * goes first and whole, and the stream paints over it.
     *
     * Rung 20x makes "IS staged" mean what it says.  The take only answers that the composite ran
     * the paint callback; the recorder's own count says whether that callback put any group in the
     * staging buffer.  A pass that staged nothing repaints nothing, so it does not license the
     * canvas inside the layer either -- and the previous version of this function then re-laid the
     * whole client rect from the canvas as a second-order refusal, which is a 384,000 px excursion
     * to desktop between two streams: the grey the operator sees, and a flash of it every time the
     * case repeats.
     *
     * Rung 20y takes the same arithmetic to the passes that DO send a stream, which the measurement
     * above says is every pass that has a layer at all on this route (`emptystream=0`, `cut=0`), and so
     * is where the operator's grey actually lives: the canvas goes out whole -- 384,000 px of it inside
     * the client -- and only then does the stream overwrite those pixels, so the screen spends the GIF's
     * own duration (gs=116..2,234 us on these frames, against a 16,667 us deadline the loop already
     * misses 18 times in 21) showing the desktop where the wave should be.  The rect is cut on those
     * passes too, but only while the memory above says the last stream provably plated this very rect:
     * then the pixels the canvas would have laid are pixels the stream is going to lay, and skipping
     * them costs the page nothing and saves it the grey. */
    const int gs_took = wnd_gl_layer_take(&gl_client);
    const uint32_t gs_staged = eegs_present_groups_staged();
    const int gs_paints = gs_took && gs_staged != 0u &&
                          gl_client.right > gl_client.left && gl_client.bottom > gl_client.top;
    const int gs_replates = gs_paints && s_20y_plated &&
                            s_20y_rect.left   == gl_client.left &&
                            s_20y_rect.top    == gl_client.top &&
                            s_20y_rect.right  == gl_client.right &&
                            s_20y_rect.bottom == gl_client.bottom;
    RECT gl_hole;
    int gl_hole_live = FALSE;
    uint8_t gl_why = PS2_WHY_STREAM;

    if (gs_took && !gs_paints) s_gs_empty_stream++;

    if (!gs_paints) {
        s_20y_plated = 0u;    /* desktop owns those pixels now: re-earn the memory */
        if (wnd_gl_layer_rect(&gl_hole)) {
            /* The protective half of rung 20x, and the measurement says it is not on this route's
             * critical path: every pass that takes the layer also stages a stream (`emptystream=0` on
             * all nine [GSTS] rows of the pinned run), so this branch fires zero times there.  It stays
             * armed because a no-stream pass that DID land would be desktop grey over a live layer. */
            gl_hole_live = TRUE;
            gl_why = PS2_WHY_CUT;
        } else if (wnd_gl_layer()) {
            gl_why = PS2_WHY_NO_RECT;   /* a layer is on the screen and its rect is unknowable */
        } else {
            gl_why = PS2_WHY_NO_LAYER;  /* nothing to protect: this pass is desktop all over */
        }
    } else if (gs_replates) {
        /* Rung 20y's licence is A/B'd but NOT taken in production: the operator ran a build that
         * skipped the canvas inside the client and the flicker and the icon black-rectangles did
         * NOT go away.  That falsifies the "canvas lays desktop over the client mid-present" model
         * as the author of what the operator sees, so the canvas is left to lay the rect here and
         * the real writer still has to be named.  The cut stays compiled behind the dial (rung 23,
         * BENCH_GS>=3) so the A/B remains reproducible. */
        gl_hole = gl_client;
#if BTRON_PS2_BENCH_GS >= 3
        gl_hole_live = TRUE;
        gl_why = PS2_WHY_PLATED;
        s_gs_plated_passes++;
        s_gs_plated_px += (uint32_t)(gl_client.right - gl_client.left) *
                          (uint32_t)(gl_client.bottom - gl_client.top);
#else
        gl_why = PS2_WHY_NOCUT;   /* the rect is crossed, and this build lets the canvas lay it */
#endif
    }

#if BTRON_PS2_BENCH_GS >= 3
    /* LANE 20 rung 23's A/B switch (BENCH_GS=3): the layer's rect is cut out of EVERY canvas upload
     * that crosses it, on every kind of pass, and the cover re-upload is withheld.
     *
     * This exists to answer one question that the maps so far cannot: is the client's missing ink
     * the stream's failure to RASTERISE, or the canvas's success in RE-LAYING over it?  The rung 21c
     * alpha map said the page at census time is 49% the word 0x00000000 in full-width row bands,
     * 8% pure A=0xfe stream ink in the top band, and the rest A=0xff uploads and mixed bands --
     * which is a band structure, not a triangle's diagonal edge, and bands are what the compositor
     * uploads in.  With the rect unconditionally protected, a band can no longer reach the client:
     * the zeros either go away (the canvas was the eraser, and the stream's own coverage is then
     * whatever is left in the `k` cells) or they stay exactly where they are (the GS never filled
     * them, and the fix is in the stream, not the present order).
     *
     * Deliberately compiled only under the bench dial: on a real desktop a pass with no layer must
     * be allowed to repaint those pixels, or a closed window leaves the GS's last frame standing. */
    if (wnd_gl_layer_rect(&gl_hole)) {
        gl_hole_live = TRUE;
        gl_why = PS2_WHY_CUT;
        if (!s_23_announced) {
            s_23_announced = TRUE;
            ps2_kprintf("[GS23] A/B ON: the layer rect is cut from every canvas upload and covers "
                        "are withheld -- `k` cells that survive THIS run are the stream's own "
                        "coverage hole, not a re-laid plate\n");
        }
    }
#endif

#if BTRON_PS2_BENCH_GS >= 4
    /* LANE 20 rung 24's bisect (BENCH_GS=4 refuses from chain 2 on, 5 from 3, 6 from 4): keep the
     * frame's first `BENCH_GS-2` chains and throw every later declaration away at the recorder.
     *
     * The rung 21c map, read with rung 23's cut so nothing of the desktop's is in the page, says the
     * client is A=0xfe stream ink for 48 full-width rows and the word 0x00000000 for 124 of them,
     * with a mixed-alpha smear in between -- and the two laws that produce that picture are one file
     * apart.  Either chains 0 and 1, the whole-client clear and the whole-client gradient, stop
     * rasterising at local y=48; or they fill all 500 rows and a later chain erases them, which this
     * port's own ALPHA word makes plausible: (Cs-Cd)*As+Cd with a black source and a partial alpha
     * multiplies the destination toward zero, alpha byte included, and 22 textured chains do it
     * 1,200-odd times over the same pixels.
     *
     * With the cutoff below the first icon chain, `k` cells that remain are the stream's own failure
     * to cover; `k` cells that turn blue are the blend eating what it drew.  Nothing else in this
     * build changes between the two runs, which is the whole value of the dial. */
    {
        static BOOL s_24_announced;
        const uint32_t lim = (uint32_t)BTRON_PS2_BENCH_GS - 2u;

        if (!s_24_announced) {
            s_24_announced = TRUE;
            s_gschain_printed = 0u;      /* the tables must be the bisected frames', not this one's */
            eegs_chain_set_limit(lim);
            ps2_kprintf("[GS24] BISECT armed: chains 0..%u of every later frame are recorded and "
                        "sent, every declaration after them is refused at the recorder -- "
                        "0=clear, 1=gradient, 2=ribbon, 3=first icon quad.  Read [GSCHAIN]'s "
                        "`refused=` column and the [GSPIXA] map together.\n",
                        (unsigned int)(lim - 1u));
        }
    }
#endif

    ps2_upload_canvas(x0, y0, x1, y1, gl_hole_live ? &gl_hole : NULL, gs_paints, gl_why);

    /* Everything the GS is given after the canvas it draws on: the layer's stream, then the
     * canvas regions that stream buried.  Timed together because they are one phase -- `gs=` is
     * the cost of drawing a frame on the GS rather than in the canvas, and it is the number that
     * has to stay under the frame budget for increment 5 to be worth anything. */
    {
        const uint32_t tg0 = ps2_count_read();
        int n_cover;

        /* Once per session: the walk's plate skip is conditional on the stream covering its own
         * rect, and the condition is measured here rather than assumed from the application's
         * source.  A frame that fails it is a frame where the canvas shows through the picture, so
         * the row says so at the moment it is true instead of leaving it to be noticed as a patch
         * of desktop inside a window. */
        static BOOL s_plate_warned;
        eegs_present_stats st;

        /* Taken above, before the canvas went out: the answer is what decided whether that upload
         * was allowed to cover the layer's rect.  A band that did not re-record the layer has no
         * stream to send, and now no canvas pixels inside the layer either. */
        if (gs_paints) {
            gs_groups = eegs_present_send(&gl_client);
            if (gs_groups > 0) {
                s_gs_groups += (uint32_t)gs_groups;
                eegs_present_get_stats(&st);
                s_gs_st = st;
                s_gs_qw += st.qw;
                /* Rung 20y's memory, written from the frame that just went out -- and only from a frame
                 * that provably plated the rect AND had nothing refused by the ring, because a stream
                 * whose tail was dropped does not cover the rows its own bbox claims. */
                s_20y_plated = (st.plate_covers && st.dropped == 0u) ? 1u : 0u;
                s_20y_rect = gl_client;
#if BTRON_PS2_BENCH_GS >= 5
                ps2_bench_dump_groups();    /* rung 25: the stage while it is still this frame's */
                ps2_bench_gs_race_probe(&gl_client, gs_replates);  /* rung 26: late, or never drawn */
#endif
#if BTRON_PS2_BENCH_GS >= 3
                if (gs_replates && !s_20y_plated) {
                    /* The case the memory cannot predict: this frame's canvas pixels inside the rect were
                     * skipped on the strength of the last one, and this one did not plate the rect.  The
                     * page keeps the previous frame's ink there rather than desktop -- the milder of the
                     * two failures, and the one that says the app's own plate is the thing to fix. */
                    static BOOL s_20y_warned;
                    if (!s_20y_warned) {
                        s_20y_warned = TRUE;
                        ps2_kprintf("[GS20Y] a frame whose canvas was skipped did not plate the rect: "
                                    "clip=%ux%u b0=%d,%d,%d,%d drop=%u -- those pixels kept the previous "
                                    "frame's ink; the memory is dropped, so the next pass uploads the "
                                    "rect again\n",
                                    (unsigned int)st.clip_w, (unsigned int)st.clip_h,
                                    (int)st.b0_l, (int)st.b0_t, (int)st.b0_r, (int)st.b0_b,
                                    (unsigned int)st.dropped);
                    }
                }
#endif
                if (st.has_bbox && !st.plate_covers && !s_plate_warned) {
                    s_plate_warned = TRUE;
                    ps2_kprintf("[GSPLATE] the stream does not plate its rect: "
                                "clip=%ux%u b0=%d,%d,%d,%d -- a layer's body fill stays required\n",
                                (unsigned int)st.clip_w, (unsigned int)st.clip_h,
                                (int)st.b0_l, (int)st.b0_t, (int)st.b0_r, (int)st.b0_b);
                }
                gs_layer = TRUE;
            }
        }

        /* The case the previous version of this function tried to cure here -- the composite
         * re-recorded the layer, the staged stream turned out to hold no groups, and the pass's own
         * canvas upload had been allowed inside the layer's rect on the strength of a stream that
         * never came -- is now refused where the decision is made: `gs_paints` above reads the
         * recorder's staged count before the canvas goes anywhere, so such a pass takes the cut like
         * any other no-stream pass and no desktop pixel is laid there in the first place.  What is
         * left is the column: `emptystream=` says how often a paint callback records nothing, which
         * is an application question, not a present-order one. */

        /* Taken whether or not anything was sent: the list describes this composite, and a
         * present that left it standing would have the next frame's rects appended to these. */
        n_cover = wnd_gl_cover_take(&gl_cover);
        s_gs_cover_merge += (uint32_t)gl_cover.merged;   /* counted whether or not this pass sends */
        if (gs_layer) {
#if BTRON_PS2_BENCH_GS >= 3
            /* Rung 23's other half: a cover is a canvas rect uploaded INSIDE the layer because the
             * stream buried it -- exactly the write this A/B is trying to exclude, so the experiment
             * withholds it and counts nothing into `cover=`. */
            (void)ps2_upload_cover_in_layer;
            (void)gl_cover;
#else
            ps2_upload_cover_in_layer(&gl_cover, &gl_client);
#endif
            s_gs_cover += (uint32_t)n_cover;
            s_gs_cover_drop += (uint32_t)gl_cover.dropped;
        }

        {
            const uint32_t tg1 = ps2_count_read();
            if (s_timebase_ok && btron_ee_ticks_per_us())
                s_gs_us += (tg1 - tg0) / btron_ee_ticks_per_us();
        }
    }

    const uint32_t t3 = ps2_count_read();

    if (s_timebase_ok) {
        const uint32_t tpu = btron_ee_ticks_per_us();
        *r_us = (t1 - t0) / tpu;
        *s_us = (t2 - t1) / tpu;
        *u_us = (t3 - t2) / tpu;
    } else {
        *r_us = *s_us = *u_us = 0u;
    }

#if BTRON_PS2_PAINT_WORKER
    /* #165: the app's frame is priced by the band that actually put it on the page, and that band
     * is the only one this pass is allowed to be.  `gs_layer` is the strongest true thing available
     * here -- a stream went out, so the client now holds this frame's pixels -- and `*r_us` is the
     * render bracket, i.e. the composite that ran the callback, which is the cost the policy is
     * being asked to predict.  Raising the latch is what stops the next band recording the same
     * frame a second time; it is cleared at the end of the pass by ps2_pw_freeze(0). */
    if (s_pw_on && gs_layer && !s_pw_recorded && s_pw_layer &&
        (const void *)wnd_gl_layer() == s_pw_layer) {
        s_pw_recorded = 1;
        ps2_pw_done(s_pw_layer, *r_us, btron_render_perf_us());
        ps2_pw_freeze(1);
    }
#endif
}

static void ps2_paint_bands(GDEV *screen, const ps2_bandset_t *set,
                            uint32_t *r_us, uint32_t *s_us, uint32_t *u_us,
                            uint32_t *px_out)
{
    uint32_t acc_r = 0u, acc_s = 0u, acc_u = 0u, px = 0u;

    for (unsigned i = 0; i < set->n; i++) {
        const ps2_band_t *band = &set->b[i];
        uint32_t r, s, u;

        px += (uint32_t)(band->x1 - band->x0) * (uint32_t)(band->end - band->top);
        ps2_paint_region(screen, band->x0, band->top, band->x1, band->end,
                         ps2_band_is_full(band), &r, &s, &u);
        acc_r += r;  acc_s += s;  acc_u += u;
    }

    /* Timed per band and totalled for the pass, because the pass is the thing whose
     * cost decides the loop rate and the bands of one pass are one cost.  With no
     * Count to read, all three are zero and only the pixel count means anything. */
    *r_us = acc_r; *s_us = acc_s; *u_us = acc_u;
    *px_out = px;
}

/* The region this pass owes the GS: the box the sprite was in, and the box it is in
 * now, in x as well as in y.  Both, always: sending only the new one leaves the old
 * cursor printed under the new one, which is a trail, and a trail reads as the
 * pointer being slower than it is.
 *
 * Its own function rather than the loop's text because ps2_bench_run() has to ask
 * the same question, and a second copy of these rules would measure a present the
 * desktop does not make. */
static void ps2_bands_for_pass(ps2_bandset_t *set)
{
    RECT dmg;

    if (s_mouse_x != s_shown_x || s_mouse_y != s_shown_y) {
        /* The sprite's own columns, swept from where it was shown to where it is:
         * the composite has to erase the old copy and draw the new one, so the
         * present's x span is |dx| + the sprite's width and not the canvas's
         * width.  Clamping is bandset_add()'s, so a cursor at either border costs
         * the part that is on screen. */
        const int cx0 = s_shown_x < s_mouse_x ? s_shown_x : s_mouse_x;
        const int cx1 = (s_shown_x > s_mouse_x ? s_shown_x : s_mouse_x) + PS2_CURSOR_COLS;

        ps2_bandset_add(set, cx0, cx1, s_shown_y, s_shown_y + PS2_CURSOR_ROWS);
        ps2_bandset_add(set, cx0, cx1, s_mouse_y, s_mouse_y + PS2_CURSOR_ROWS);
        /* Hovering the top bar is the one motion that repaints somewhere the
         * sprite is not: global_menu_handle_mouse_move() moves the highlight.
         * Twenty-eight full-width rows is the honest price of that, because the bar
         * tracks no column -- it knows where the highlight went, not where it left,
         * and bounding this band by the cursor's own columns would leave the old
         * highlight painted.  It stays a separate band rather than a union with the
         * cursor's, so a sweep across the middle of the screen never pays for the
         * bar at all. */
        if (s_shown_y < PS2_PANEL_ROWS || s_mouse_y < PS2_PANEL_ROWS)
            ps2_bandset_add(set, 0, PS2_SCREEN_WIDTH, 0, PS2_PANEL_ROWS);
    }

    /* Whatever an app or the window manager invalidated since the last present.
     * Taken every pass, including one that is going to repaint the canvas
     * anyway: the accumulator is consumed on read, and a pass that skipped
     * taking it would drop the rect. */
    if (wnd_take_inval_damage(&dmg)) {
        ps2_bandset_add(set, dmg.left, dmg.right, dmg.top, dmg.bottom);
    }
}

static void ps2_console_flush(void)
{
    if (!s_gui_active) ps2_gs_text_flush();
}

static void ps2_emit_num(uint32_t val, unsigned base, unsigned width,
                         int zero_pad, int upper, int negative)
{
    char buf[12];
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0;

    if (val == 0) buf[n++] = '0';
    while (val != 0 && n < (int)sizeof(buf)) {
        buf[n++] = digits[val % base];
        val /= base;
    }

    unsigned filled = (unsigned)n + (negative ? 1u : 0u);
    for (unsigned i = filled; i < width; i++) ps2_console_putc(zero_pad ? '0' : ' ');
    if (negative) ps2_console_putc('-');
    while (n > 0) ps2_console_putc(buf[--n]);
}

void ps2_kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    while (*fmt) {
        if (*fmt != '%') {
            ps2_console_putc(*fmt++);
            continue;
        }
        fmt++;
        if (*fmt == '\0') break;

        if (*fmt == '%') {
            ps2_console_putc('%');
            fmt++;
            continue;
        }

        int zero_pad = 0;
        int left = 0;
        unsigned width = 0;
        /* A flag the formatter does not implement has to be consumed here, or
         * its characters reach the console as text and every argument after it
         * is read one slot out of place. */
        if (*fmt == '-') { left = 1; fmt++; }
        if (*fmt == '0') { zero_pad = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10u + (unsigned)(*fmt - '0');
            fmt++;
        }

        switch (*fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            unsigned n = 0;
            if (!s) s = "(null)";
            while (*s) { ps2_console_putc(*s++); n++; }
            while (left && n++ < width) ps2_console_putc(' ');
            break;
        }
        case 'c':
            ps2_console_putc((char)va_arg(ap, int));
            break;
        case 'd':
        case 'i': {
            int v = va_arg(ap, int);
            uint32_t mag = (v < 0) ? (uint32_t)0 - (uint32_t)v : (uint32_t)v;
            ps2_emit_num(mag, 10, width, zero_pad, 0, v < 0);
            break;
        }
        case 'u':
            ps2_emit_num(va_arg(ap, unsigned int), 10, width, zero_pad, 0, 0);
            break;
        case 'x':
            ps2_emit_num(va_arg(ap, unsigned int), 16, width, zero_pad, 0, 0);
            break;
        case 'X':
            ps2_emit_num(va_arg(ap, unsigned int), 16, width, zero_pad, 1, 0);
            break;
        default:
            break;
        }
        fmt++;
    }
    va_end(ap);

    ps2_console_flush();
}


/* ── Hardware Event Dispatchers ─────────────────────────────────── */

static void ps2_move_mouse(int dx, int dy)
{
    s_mouse_x += dx;
    s_mouse_y += dy;
    if (s_mouse_x < 0) s_mouse_x = 0;
    if (s_mouse_x >= PS2_SCREEN_WIDTH) s_mouse_x = PS2_SCREEN_WIDTH - 1;
    if (s_mouse_y < 0) s_mouse_y = 0;
    if (s_mouse_y >= PS2_SCREEN_HEIGHT) s_mouse_y = PS2_SCREEN_HEIGHT - 1;

    set_baremetal_mouse_pos((H)s_mouse_x, (H)s_mouse_y);

    EVT ev;
    tkl_memset(&ev, 0, sizeof(EVT));
    ev.type = EV_MOUSE_MOVE;
    ev.pos.x = (H)s_mouse_x;
    ev.pos.y = (H)s_mouse_y;
    snd_evt(&ev);
}

static void ps2_click_mouse(int button, int down)
{
    EVT ev;
    tkl_memset(&ev, 0, sizeof(EVT));
    ev.type = down ? EV_BUT_DOWN : EV_BUT_UP;
    ev.pos.x = (H)s_mouse_x;
    ev.pos.y = (H)s_mouse_y;
    ev.button = button;
#if BTRON_HID_TRACE
    ER r = snd_evt(&ev);
    /* The key path has had this pair of rows for a while; the button path has not,
     * and "the cursor moves but a click does nothing" is exactly the question the
     * pair answers.  r=0 is the queue taking the edge; xy is the position the WM
     * will hit-test, which is the last painted cursor position, not the one in the
     * report that carried this button byte. */
    ps2_kprintf("[EVT] b=%d d=%d r=%d xy=%d,%d\n", button, down, (int)r, s_mouse_x, s_mouse_y);
#else
    snd_evt(&ev);
#endif
}

static ER ps2_inject_key(UW keycode, int down)
{
    EVT ev;
    tkl_memset(&ev, 0, sizeof(EVT));
    ev.type = down ? EV_KEY_DOWN : EV_KEY_UP;
    ev.pos.x = (H)s_mouse_x;
    ev.pos.y = (H)s_mouse_y;
    ev.key = keycode;
    ER r = snd_evt(&ev);
#if BTRON_HID_TRACE
    /* r=0 is the queue taking it; anything else and the key died here rather
     * than at the window that was supposed to read it. */
    if (down) ps2_kprintf("[EVT] k=%x r=%d xy=%d,%d\n", keycode, (int)r, s_mouse_x, s_mouse_y);
#endif
    return r;
}

/* ── Pad & USB Driver Hooks ─────────────────────────────────────── */

void ps2_pad_on_move(int dx, int dy)
{
    ps2_move_mouse(dx, dy);
}

void ps2_pad_on_button(uint16_t newly_pressed, uint16_t newly_released)
{
    /* A menu is navigated by its selection, not by the pointer, so while one is open
     * the D-pad must not displace the cursor: a mouse move over an open menu
     * recomputes the highlight from the pixel it landed on (tracker_handle_mouse_move,
     * global_menu_handle_mouse_move), and a 16 px step against a 20 px row pitch picks
     * whichever row the pointer reached rather than the row the arrow asked for.
     * Cross becomes Return for the same reason -- a click would be delivered at the
     * pointer, not at the highlighted row. */
    const int menu_nav = tracker_is_menu_open() || global_menu_is_open();

    /* Left Mouse Button (Cross), or Enter while a menu is open */
    if (menu_nav) {
        if (newly_pressed & PAD_CROSS) {
            ps2_inject_key(BTRON_KEY_RETURN, 1);
            ps2_inject_key(BTRON_KEY_RETURN, 0);
        }
    } else {
        if (newly_pressed & PAD_CROSS)   ps2_click_mouse(1, 1);
        if (newly_released & PAD_CROSS)  ps2_click_mouse(1, 0);
    }

    /* Right Mouse Button (Square) */
    if (newly_pressed & PAD_SQUARE)  ps2_click_mouse(2, 1);
    if (newly_released & PAD_SQUARE) ps2_click_mouse(2, 0);

    /* Triangle: Cycle Japanese TIP/IME Mode */
    if (newly_pressed & PAD_TRIANGLE) {
        tip_toggle_mode();
        ps2_kprintf("[PS2-PAD] TIP Mode toggled -> %s\n", tip_get_mode_str());
    }

    /* Circle: Escape / Dismiss active menus & dialogs */
    if (newly_pressed & PAD_CIRCLE) {
        ps2_inject_key(BTRON_KEY_ESCAPE, 1);
        ps2_inject_key(BTRON_KEY_ESCAPE, 0);
    }

    /* Start: Toggle Tracker Start Menu in GUI, or launch GUI from Stage 1 */
    if (newly_pressed & PAD_START) {
        if (s_gui_active) {
            tracker_toggle_menu();
        } else {
            launch_ps2_desktop_session();
        }
    }

    /* Select: Cycle active window focus */
    if (newly_pressed & (PAD_SELECT | PAD_L1 | PAD_R1)) {
        wnd_cycle_focus();
    }

    /* D-Pad: discrete cursor displacement, or menu navigation while a menu is open */
    if (newly_pressed & PAD_UP) {
        if (!menu_nav) ps2_move_mouse(0, -16);
        ps2_inject_key(BTRON_KEY_UP, 1);
        ps2_inject_key(BTRON_KEY_UP, 0);
    }
    if (newly_pressed & PAD_DOWN) {
        if (!menu_nav) ps2_move_mouse(0, 16);
        ps2_inject_key(BTRON_KEY_DOWN, 1);
        ps2_inject_key(BTRON_KEY_DOWN, 0);
    }
    if (newly_pressed & PAD_LEFT) {
        if (!menu_nav) ps2_move_mouse(-16, 0);
        ps2_inject_key(BTRON_KEY_LEFT, 1);
        ps2_inject_key(BTRON_KEY_LEFT, 0);
    }
    if (newly_pressed & PAD_RIGHT) {
        if (!menu_nav) ps2_move_mouse(16, 0);
        ps2_inject_key(BTRON_KEY_RIGHT, 1);
        ps2_inject_key(BTRON_KEY_RIGHT, 0);
    }
}

/* One keystroke into the prompt, from either source.  Defined below with the
 * shell's line editor and forward-declared because the USB hook above it needs
 * it: the decoder runs wherever the poll loop happens to notice a report, not
 * inside the SIO read. */
static void ps2_shell_char(int c);

/* HID modifier byte: left and right Control.  Only Control is named here because
 * only Control is used to qualify a binding; Shift is already folded into the
 * decoded key by ps2_usb_hid_to_btron_key(). */
#define PS2_MOD_CTRL 0x11u

/* Defined with the pointer statistics it prints, below ps2_paint_print(). */
static void ps2_ptr_chord(uint32_t key);

/* Defined beside the `page` dial that reads and writes it, below ps2_shell_page()'s preamble. */
static int ps2_page_double_apply(uint32_t on);

/* ── Keyboard Auto-Repeat ───────────────────────────────────────── */

/* The three numbers this reads are not this port's invention: they are the ones
 * `src/settings/input.c:16-18` defines for every target (16 cs = 160 ms before the
 * first repeat, 40 cps = 25 ms after that, repeat on), the same three the Input
 * applet's radio rows and `xmb.c`'s settings bindings write, and the same three
 * `core_arm64.c:2703-2705` already obeys on the Pi 400.  What PS2 was missing was the
 * *reader*, not the setting: `ps2_usb_process_keyboard_report()` decodes **edges** --
 * `s_prev_keys` turns a key held for a second into exactly one down event -- while on
 * the posix target a held key repeats because SDL forwards the host's own auto-repeat
 * as further down events.  So here one press typed one character, and holding
 * Backspace deleted one character at a time as fast as the hand could tap it.
 *
 * Repeating in the guest is the layer that can do it, and it is the layer arm64 chose:
 * the HID edge says *what* is held, the folded microsecond clock says *for how long*,
 * and the applet's three variables say *how often*.  The emulator's own repeat is not
 * reachable -- PCSX2 hands over the device, not the host's keyboard settings -- and
 * asking the human to tap is not a fix.
 *
 * One key repeats: the most recent one pressed and still down.  A later press re-arms
 * the delay, an up of the armed key disarms it, and an up of any *other* key leaves the
 * armed one running.  Two keys held at once therefore repeat only the newer one, which
 * is what a host keyboard does and is not a case auto-repeat exists for. */
extern uint32_t g_kbd_repeat_delay_us;
extern uint32_t g_kbd_repeat_interval_us;
extern int      g_kbd_repeat_enabled;

static uint32_t s_kbd_held_key;           /* 0 == nothing armed to repeat */
static uint8_t  s_kbd_held_mod;           /* kept for the trace rows, not for the key */
static uint32_t s_kbd_press_us;
static uint32_t s_kbd_repeat_us;
static uint32_t s_kbd_repeats;            /* events injected here since boot */

static void ps2_kbd_arm(uint32_t btron_key, uint8_t mod)
{
    s_kbd_held_key = btron_key;
    s_kbd_held_mod = mod;
    s_kbd_press_us = s_kbd_repeat_us = btron_render_perf_us();
}

static void ps2_kbd_disarm(uint32_t btron_key)
{
    if (s_kbd_held_key != 0u && s_kbd_held_key == btron_key) s_kbd_held_key = 0u;
}

/* Repeats one pass owes, and how many a single pass may deliver.  A tick that fired at
 * most one repeat per pass would repeat at the frame rate rather than at *FX 12: the XMB's
 * measured cycle period is ~140 ms against a 25 ms interval, so the applet's 40 cps and
 * 12.5 cps rows would feel identical and the setting would be decorative.  A tick
 * therefore pays every interval that elapsed, and the schedule advances by those intervals
 * instead of by the clock, which is what keeps the *mean* rate the setting's rate.  The cap
 * bounds the burst so a long stall cannot flood the event queue; at a 140 ms pass and a
 * 25 ms interval the due count is 5, so the cap is above the working case and below the
 * queue's size, and a backlog longer than the cap is dropped rather than paid out late. */
#define PS2_KBD_REPEAT_MAX 8u

/* The arithmetic of the law, kept apart from its state so a headless run can price it
 * without a hand on a key.  `since_us` is the gap between the schedule pointer and now;
 * `*advance_us` comes back as what the schedule moves by -- the intervals actually paid,
 * not the whole gap -- because paying out of the schedule is what makes the *mean* rate
 * equal the setting's.  A gap longer than the cap can pay is dropped whole rather than
 * banked, so a stalled pass resynchronises instead of repeating at the cap forever.
 * Pure: reads no clock, touches no state. */
static uint32_t ps2_kbd_due(uint32_t since_us, uint32_t interval_us, uint32_t *advance_us)
{
    if (interval_us == 0u) { *advance_us = 0u; return 0u; }

    const uint32_t due = since_us / interval_us;
    if (due == 0u) { *advance_us = 0u; return 0u; }
    if (due > PS2_KBD_REPEAT_MAX) { *advance_us = since_us; return PS2_KBD_REPEAT_MAX; }

    *advance_us = due * interval_us;
    return due;
}

/* One pass' worth of repeat, on a host's terms: the delay is measured from the press and
 * the interval from the schedule, never from the pass.  The clock is the folded one, whose
 * 32-bit microseconds wrap every 2^32/tpu us -- 14.5 s at this machine's measured 295
 * ticks/us -- and every test below is an unsigned difference, so a wrap between the press
 * and a tick costs one missed repeat rather than a burst: the intervals that matter here
 * are 25 ms and 500 ms, five orders of magnitude inside the wrap.  If the timebase was
 * never measured the clock reads a constant zero, every difference reads zero, `due` is
 * zero, and repeat stays off; a port with no clock does not invent a cadence. */
static void ps2_kbd_repeat_tick(void)
{
    const uint32_t now = btron_render_perf_us();

    if (!g_kbd_repeat_enabled || s_kbd_held_key == 0u) return;
    if ((uint32_t)(now - s_kbd_press_us) < g_kbd_repeat_delay_us) return;

    uint32_t advance_us;
    const uint32_t due = ps2_kbd_due((uint32_t)(now - s_kbd_repeat_us),
                                     g_kbd_repeat_interval_us, &advance_us);
    if (due == 0u) return;

    s_kbd_repeat_us += advance_us;
    s_kbd_repeats += due;
#if BTRON_HID_TRACE
    /* The cadence, on the same dial the rest of the input evidence uses: this row is
     * what separates "repeat is off" from "repeat is firing faster or slower than the
     * applet says", and it reports the burst the pass paid as well as the total, so the
     * frame rate's effect on the cadence is visible instead of being assumed away. */
    ps2_kprintf("[KBD] rep k=%x m=%x n=%u due=%u since_press=%u us\n", (unsigned int)s_kbd_held_key,
                (unsigned int)s_kbd_held_mod, (unsigned int)s_kbd_repeats, (unsigned int)due,
                (unsigned int)(now - s_kbd_press_us));
#endif
    for (uint32_t i = 0u; i < due; i++) {
        if (s_gui_active) {
            /* Press only: the key really is still down, and a typematic stream from
             * hardware is a run of downs ending in one up. */
            ps2_inject_key((UW)s_kbd_held_key, 1);
        } else if (s_kbd_held_key < 0x100u) {
            /* Stage 1 has no event consumer, so the prompt takes the character exactly as
             * ps2_usb_on_key() gave it the first time.  Function and navigation codes from
             * 0x100 up have no binding at the prompt and must not be sent as a byte. */
            ps2_shell_char((int)s_kbd_held_key);
        }
    }
}

#if BTRON_PS2_BENCH
/* The repeat law priced against this machine's frame, with no hand on a key.  The tick
 * runs once per pass, so what the Input applet's three *FX 12 rows actually deliver is
 * this pass period divided by the interval.  A run that prints 5/3/1 shows the three
 * settings are distinguishable on this build; one that prints 1/1/1 says the frame is
 * slower than every rate the menu offers, which is a fact about the frame loop and not
 * about the keyboard.  The period is the last stamped cycle -- the same sum the [XMBT]
 * row prints -- so these numbers and the frame rows came from one image. */
static void ps2_bench_kbd_cadence(void)
{
    const uint32_t period = s_cycle.pass_us + s_cycle.tail_us + s_cycle.hold_us;
    uint32_t adv;

    ps2_kprintf("[BENCH] kbd cadence: enabled=%d delay=%u us pass=%u us -> "
                "*FX 12 25000=%u 40000=%u 80000=%u reps/pass (cap %u)\n",
                g_kbd_repeat_enabled ? 1 : 0, (unsigned int)g_kbd_repeat_delay_us,
                (unsigned int)period,
                (unsigned int)ps2_kbd_due(period, 25000u, &adv),
                (unsigned int)ps2_kbd_due(period, 40000u, &adv),
                (unsigned int)ps2_kbd_due(period, 80000u, &adv),
                (unsigned int)PS2_KBD_REPEAT_MAX);
}
#endif /* BTRON_PS2_BENCH */

void ps2_usb_on_key(uint32_t btron_key, int down, uint8_t mod)
{
#if BTRON_HID_TRACE
    /* The decoder already ran, so this row says the router heard the key and
     * which of its three exits it took: quit the GUI, queue an event, or feed
     * the prompt.  A [KBD] row with no [KEY] row after it is a build whose
     * decoder is not this one. */
    ps2_kprintf("[KEY] k=%x m=%x d=%d gui=%d\n", btron_key, (unsigned int)mod,
                down, s_gui_active);
#endif
    /* Ctrl+Q is the only thing that gives up the desktop session.
     *
     * A bare 'q' used to be the binding, and that is why a terminal window on this
     * port cannot type the letter: the test ran before the key was routed, so the
     * keystroke was spent by the compositor instead of reaching the window under
     * the pointer.  Escape was in the same condition and cost any application that
     * wants it.  Both now travel on to the windows like any other key, and the way
     * out is a chord no text editor binds. */
    if (s_gui_active && down && btron_key == 'q' && (mod & PS2_MOD_CTRL) != 0u) {
        s_gui_active = 0;
        return;
    }
    /* The pointer's gain and its measurement, on three keystrokes that work inside
     * the desktop.
     *
     * They are here rather than only at the prompt because every other dial this
     * port has -- `sens`, `maxstep`, `ptrstat`, `ptgain` -- is a Stage 1 command,
     * and a session that goes straight to the workbench cannot reach one without
     * leaving the desktop first.  A hand that cannot put the cursor anywhere is
     * being asked to type, which is the wrong order.
     *
     * Swallowed before the inject, on the same terms as Ctrl+Q: a chord the window
     * under the pointer also acts on is a chord that changes the gain twice. */
    if (s_gui_active && down && (mod & PS2_MOD_CTRL) != 0u &&
        (btron_key == '[' || btron_key == ']' || btron_key == '\\')) {
        ps2_ptr_chord(btron_key);
        return;
    }
    /* Ctrl+P turns LANE 21 rung 4's two-page present on and off, in the desktop, on the live
     * image.  It is here rather than only at the prompt because the question rung 4 answers is
     * "is the flicker gone", and a question about a flicker has to be answerable while the
     * flicker is happening: leaving the desktop to type `page double 1` ends the very frame
     * sequence whose smoothness is being judged, and coming back repaints the canvas cold.  One
     * chord, one keystroke between the two pictures, and the operator is the instrument.
     *
     * It is a chord and not a bare letter for the same reason Ctrl+Q is: a terminal window binds
     * what its owner types. */
    if (s_gui_active && down && btron_key == 'p' && (mod & PS2_MOD_CTRL) != 0u) {
        ps2_page_double_apply(gs_disp_is_double() ? 0u : 1u);
        return;
    }
#if BTRON_PS2_PAINT_WORKER
    /* Ctrl+O arms and disarms task #165's paint worker, in the desktop, on the live image, for the
     * same reason Ctrl+P exists: the question it answers is "does the app animate as smoothly when
     * the compositor is choosing whether to paint it", and that is a picture, not a log line.  One
     * image carries both behaviours; the compile-time `PS2_PW=1` is what puts the choice in it.
     *
     * With the dial at 0 at build time this chord does not exist and Ctrl+O reaches the window under
     * the pointer like any other key, which is what keeps the shipped image's input plane alone. */
    if (s_gui_active && down && btron_key == 'o' && (mod & PS2_MOD_CTRL) != 0u) {
        ps2_pw_apply(s_pw_on ? 0u : 1u);
        return;
    }
#endif
    if (s_gui_active) {
        ps2_inject_key((UW)btron_key, down);
        /* Armed only for a key that was actually routed: the chords swallowed above are
         * the compositor's, and repeating them would move the cursor or change the gain
         * once every 25 ms. */
        if (down) ps2_kbd_arm(btron_key, mod);
        else      ps2_kbd_disarm(btron_key);
        return;
    }
    /* Stage 1 has no event consumer: the prompt builds its line from characters,
     * so a decoded key that only becomes an event is heard and then thrown away.
     * Codes from 0x100 up are the BTRON function and navigation keys, which the
     * console has no binding for yet and which must not be mistaken for a byte. */
    if (down && btron_key < 0x100u) {
        ps2_shell_char((int)btron_key);
        ps2_kbd_arm(btron_key, mod);
    } else if (!down) {
        ps2_kbd_disarm(btron_key);
    }
}

/* ── Pointer Source Statistics ─────────────────────── */

/* Which layer owns the motion is decided by one comparison, not by a guess: slide
 * the mouse the same physical distance slowly and then fast, and compare the
 * totals.  Equal means the wire carries distance -- raw device counts, or an
 * accelerator that is switched off -- and a single guest gain is the right tool.
 * Bigger for the fast run means the numbers are cursor pixels that a third party
 * has already multiplied by speed, and then no constant can be correct, because
 * the constant would have to change with how hard the hand is moving.
 *
 * So this collects what a printed log cannot: the [RAW] rows are throttled to one
 * in sixty-four to keep the console readable, and the whole point of the question
 * is what happens between those samples. */
#define PS2_PTRST_NB 7        /* |counts|: 0, 1, 2-3, 4-7, 8-15, 16-63, 64+ */
typedef struct {
    uint32_t reports;
    uint32_t still;                                /* both axes zero */
    uint32_t hx[PS2_PTRST_NB], hy[PS2_PTRST_NB];
    int32_t  sum_x, sum_y;                         /* signed: where it ended up */
    uint32_t sum_ax, sum_ay;                       /* unsigned: how far it went */
    uint32_t sat_x, sat_y;                          /* |count| == 127: byte ceiling */
    uint32_t ax_max, ay_max;
    uint32_t f_first, f_last;
    /* What the screen border did, in guest pixels and per paint pass.  See
     * ps2_ptrst_pass(). */
    uint32_t passes, spent_x, spent_y, lost_x, lost_y, wall_x, wall_y;
} ps2_ptrst_t;

static ps2_ptrst_t s_ptrst;

/* Counts since the last `ptgain`, kept outside the ptrstat window on purpose: the
 * one slide that is worth measuring is the slide across the whole display, and it
 * should survive being labelled with a `ptrstat` in the same session rather than
 * have to be performed twice.  Same caveat as that window, though: `ptrcal` feeds
 * reports through the handler below, so it must not run between the sweep and the
 * call that reads it. */
static uint32_t s_ptg_cx, s_ptg_cy;

/* The same session's two-speed pair, kept outside the ptrstat window for the same
 * reason as those counters and for one more: this test's whole difficulty is that
 * the two slides have to be *seen by the same reader*, and a window that resets
 * between them throws the first sample away.  See src/cores/ps2_ptr_stroke.h. */
static ps2_stroke_t s_ptr_stroke;

/* The row the pair earns, printed against a window's own counts.  Nothing here is
 * typed: the hand does the slide twice at two speeds and this is what the session
 * already knows about it.  The units are the ones the layer question is asked in --
 * counts carried, and the frames it took to carry them -- so a reader can check the
 * spread for themselves instead of taking the verdict on trust. */
static void ps2_ptr_stroke_row(void)
{
    uint32_t ratio = 0u;
    int v;

    /* The stroke under the hand counts, or the row would miss the one slide that
     * decides it -- a sweep ends when the hand stops, and nothing tells this code
     * that it did. */
    ps2_stroke_flush(&s_ptr_stroke);
    v = ps2_stroke_verdict(&s_ptr_stroke, &ratio);
    const uint32_t slow_spd = s_ptr_stroke.slow_ms
        ? (unsigned int)ps2_stroke_speed1000(s_ptr_stroke.slow_counts, s_ptr_stroke.slow_ms) : 0u;
    const uint32_t fast_spd = s_ptr_stroke.fast_ms
        ? (unsigned int)ps2_stroke_speed1000(s_ptr_stroke.fast_counts, s_ptr_stroke.fast_ms) : 0u;

    ps2_kprintf("[PSTAT] stroke pair: samples=%u slow=%u counts in %u ms (%u.%02u/ms), "
                "fast=%u in %u ms (%u.%02u/ms)",
                (unsigned int)s_ptr_stroke.samples,
                (unsigned int)s_ptr_stroke.slow_counts, (unsigned int)s_ptr_stroke.slow_ms,
                (unsigned int)(slow_spd / 1000u), (unsigned int)(slow_spd % 1000u / 10u),
                (unsigned int)s_ptr_stroke.fast_counts, (unsigned int)s_ptr_stroke.fast_ms,
                (unsigned int)(fast_spd / 1000u), (unsigned int)(fast_spd % 1000u / 10u));
    /* A ratio of zero is not a measurement of zero, and printing it as one would
     * put a number in the log that the verdict below then contradicts. */
    if (ratio) ps2_kprintf(" -> %u.%02ux\n", (unsigned int)(ratio / 100u),
                           (unsigned int)(ratio % 100u));
    else       ps2_kprintf(" -> no ratio yet\n");
    switch (v) {
    case PS2_STROKE_WIRE:
        ps2_kprintf("[PSTAT]   the same slide carried the same counts at both speeds: distance is on\n"
                    "[PSTAT]   the wire, so one guest gain is right and `sens` is where the feel is set\n");
        break;
    case PS2_STROKE_ACCEL:
        ps2_kprintf("[PSTAT]   the fast slide carried more counts for the same travel: a multiplier\n"
                    "[PSTAT]   above this port is scaling by speed, and no constant here can be right\n"
                    "[PSTAT]   for both.  Read it as macOS's own cursor curve -- `ptrsrc` to check\n"
                    "[PSTAT]   which stub is answering, and see 'Which layer owns the pointer'.\n");
        break;
    case PS2_STROKE_SHRINK:
        ps2_kprintf("[PSTAT]   the fast slide carried FEWER counts: something divides by speed, which\n"
                    "[PSTAT]   is not an accelerator and wants naming before a gain ships.\n");
        break;
    case PS2_STROKE_SPREAD:
        ps2_kprintf("[PSTAT]   both samples ran at nearly one speed, so this is not the experiment.\n"
                    "[PSTAT]   Do the same long slide again, and do it quickly the second time.\n");
        break;
    case PS2_STROKE_SILENT:
        ps2_kprintf("[PSTAT]   the mouse handler has seen NO report since boot, so no protocol can take a\n"
                    "[PSTAT]   pair off a silent wire.  This row is proof the bus and the input plane are\n"
                    "[PSTAT]   alive -- a key report printed it -- so what is missing is motion on the\n"
                    "[PSTAT]   pointer device: move the HOST cursor inside the emulator window and slide\n"
                    "[PSTAT]   it, then read this row again.  `ptrsrc` says which stub is answering.\n");
        break;
    default:
        ps2_kprintf("[PSTAT]   no pair yet: %u mouse reports seen, longest stroke %u counts, and a sample\n"
                    "[PSTAT]   needs %u of them in one slide plus a second slide of the same travel at a\n"
                    "[PSTAT]   clearly different speed.  Slide sideways across the display slowly, then do\n"
                    "[PSTAT]   the same slide fast, then read this row again.\n",
                    (unsigned int)s_ptr_stroke.reports,
                    (unsigned int)s_ptr_stroke.max_counts,
                    (unsigned int)PS2_STROKE_MIN);
        break;
    }
}

static int ps2_ptrst_bucket(uint32_t a)
{
    if (a == 0u) return 0;
    if (a == 1u) return 1;
    if (a <= 3u) return 2;
    if (a <= 7u) return 3;
    if (a <= 15u) return 4;
    if (a <= 63u) return 5;
    return 6;
}

static void ps2_ptrst_add(int dx, int dy)
{
    const uint32_t ax = (uint32_t)(dx < 0 ? -dx : dx);
    const uint32_t ay = (uint32_t)(dy < 0 ? -dy : dy);
    const uint32_t f = ps2_usb_frame_number();

    if (!s_ptrst.reports) s_ptrst.f_first = f;
    s_ptrst.f_last = f;
    s_ptrst.reports++;
    if (!ax && !ay) s_ptrst.still++;
    s_ptrst.hx[ps2_ptrst_bucket(ax)]++;
    s_ptrst.hy[ps2_ptrst_bucket(ay)]++;
    if (ax == 127u) s_ptrst.sat_x++;
    if (ay == 127u) s_ptrst.sat_y++;
    if (ax > s_ptrst.ax_max) s_ptrst.ax_max = ax;
    if (ay > s_ptrst.ay_max) s_ptrst.ay_max = ay;
    s_ptrst.sum_x += dx;   s_ptrst.sum_y += dy;
    s_ptrst.sum_ax += ax;  s_ptrst.sum_ay += ay;
    s_ptg_cx += ax;        s_ptg_cy += ay;
    /* The stroke splitter reads the same report the histogram does, and it reads
     * the count before this port's gain touches it: the question is what the wire
     * carried, and a gain applied first would answer it with arithmetic.  `f` is
     * the OHCI frame number, which this port measures at 1 ms per frame, so a
     * stroke's duration needs no CP0 Count and therefore survives a slide longer
     * than the tick counter's wrap. */
    ps2_stroke_feed(&s_ptr_stroke, f, ax);
}

/* One paint pass as the screen border saw it: what the hand's distance was worth
 * in pixels, what the cursor was allowed to spend, and whether it ended up
 * resting on a wall.
 *
 * This is the row that answers "why does the cursor stick to the edges", and the
 * count rows above cannot.  A relative pointer's position is the *integral* of
 * its counts, so pixels the border discards are gone for good, while pushing
 * further into the wall costs nothing and produces nothing.  An edge is therefore
 * a one-way sink: getting out of it takes exactly as much host travel as going
 * into it gave for free.  Whether that is what the hand is fighting is a ratio,
 * not a feel -- `lost` near zero says the walls are just where the cursor happens
 * to rest, and `lost` comparable to `spent` says the gain is spending the canvas
 * faster than the hand can cross the desktop it sits on. */
static void ps2_ptrst_pass(int32_t ask_x, int32_t ask_y, int32_t spent_x, int32_t spent_y)
{
    const uint32_t lax = (uint32_t)(ask_x < 0 ? -ask_x : ask_x);
    const uint32_t lay = (uint32_t)(ask_y < 0 ? -ask_y : ask_y);
    const uint32_t lsx = (uint32_t)(spent_x < 0 ? -spent_x : spent_x);
    const uint32_t lsy = (uint32_t)(spent_y < 0 ? -spent_y : spent_y);

    s_ptrst.passes++;
    s_ptrst.spent_x += lsx;
    s_ptrst.spent_y += lsy;
    if (lax > lsx) s_ptrst.lost_x += lax - lsx;
    if (lay > lsy) s_ptrst.lost_y += lay - lsy;
    if (s_mouse_x == 0 || s_mouse_x == PS2_SCREEN_WIDTH - 1) s_ptrst.wall_x++;
    if (s_mouse_y == 0 || s_mouse_y == PS2_SCREEN_HEIGHT - 1) s_ptrst.wall_y++;
}

/* Straightness is the second question the same table answers, and it is the one
 * that settles an axis complaint with evidence rather than opinion: move only
 * sideways and the other axis's total should be nothing.  macOS pointer inertia
 * is not axis-independent -- it curves and it snaps -- so a live sideways drag
 * that leaks counts into Y says something the synthetic per-axis sweep in ptrcal
 * structurally cannot see, because the synthetic sweep never moves both axes the
 * way a hand does.
 *
 * The window before this one is kept so that the comparison which decides the
 * layer question is printed rather than transcribed: the human moves the mouse
 * slowly, calls this, moves the same slide quickly, calls it again, and reads one
 * number.  1.0x means distance is what the wire carries.  Anything appreciably
 * above it means the counts were already multiplied by speed by something this
 * port does not control, and no guest constant can be right for both runs. */
static uint32_t s_prev_sum_ax, s_prev_sum_ay;

/* One axis's chain, in the one unit a hand can supply on purpose: total counts
 * against total pixels, so a slide of known host cursor travel divides to the gain
 * without any of the intermediate constants being trusted.  Whole-and-remainder
 * rather than a pre-multiplied numerator because this formatter has no fractional
 * conversion of its own and the multiply would have had to be trusted not to
 * overflow on a long window.
 *
 * Both axes get the row because the complaint that opened this was that the two feel
 * different, and PCSX2 has no per-axis gain for a HID mouse to blame -- its Pointer
 * bindings take the raw delta and `[Pad] PointerXScale`/`PointerYScale` are keys that
 * exist only in the ini file, not in the v2.8.2 source.  So an asymmetry upstream of
 * this port can only be macOS's own cursor curve, and this pair is where it would
 * show: equal px/count on the two rows with the hand still reporting a difference is
 * the OS, and unequal is the counts arriving unequal. */
static void ps2_chain_print(const char *axis, uint32_t counts, uint32_t spent)
{
    const uint32_t hostv = spent * PS2_EMU_POINTER_SCALE;

    ps2_kprintf("[PSTAT] chain %s: counts=%u spent=%u -> %u.%02u px/count = %u.%02u px/host px @scale%d\n",
                axis, (unsigned int)counts, (unsigned int)spent,
                counts ? (unsigned int)(spent / counts) : 0u,
                counts ? (unsigned int)((spent % counts) * 100u / counts) : 0u,
                counts ? (unsigned int)(hostv / counts) : 0u,
                counts ? (unsigned int)((hostv % counts) * 100u / counts) : 0u,
                PS2_EMU_POINTER_SCALE);
}

static void ps2_ptrst_print(const char *label)
{
    const ps2_ptrst_t *s = &s_ptrst;
    /* HcFmNumber is a 16-bit counter, so a window that happens to cross its
     * wrap would otherwise read as 65 seconds and report a rate a hundred times
     * too low.  Masking keeps the row trustworthy for any window a person can
     * hold a mouse still or slide in one go. */
    const uint32_t frames = (s->f_last - s->f_first) & 0xFFFFu;
    int b;

    ps2_kprintf("[PSTAT] %s: reports=%u still=%u over=%u frames -> %u/s\n",
                label, (unsigned int)s->reports, (unsigned int)s->still,
                (unsigned int)frames,
                frames ? (unsigned int)(s->reports * 1000u / frames) : 0u);
    for (b = 0; b < PS2_PTRST_NB; b++)
        ps2_kprintf("%u ", (unsigned int)s->hx[b]);
    ps2_kprintf("| x max=%u sat=%u\n", (unsigned int)s->ax_max, (unsigned int)s->sat_x);
    for (b = 0; b < PS2_PTRST_NB; b++)
        ps2_kprintf("%u ", (unsigned int)s->hy[b]);
    ps2_kprintf("| y max=%u sat=%u\n", (unsigned int)s->ay_max, (unsigned int)s->sat_y);
    /* Both totals, because the comparison that decides the layer question is
     * sum|x| of the slow run against sum|x| of the fast run for the same slide. */
    ps2_kprintf("[PSTAT] total |x|=%u |y|=%u  net x=%d y=%d\n",
                (unsigned int)s->sum_ax, (unsigned int)s->sum_ay,
                (int)s->sum_x, (int)s->sum_y);
    /* The border's own arithmetic, as percentages of what the pass asked for:
     * `discarded` is the share of the hand's distance the wall ate, and `onwall`
     * is the share of passes the cursor spent touching one.  See ps2_ptrst_pass().
     * `loop` is the same window's pass rate read against the frame counter, which
     * is what turns "the pointer is steppy" from an opinion into a number: the
     * cursor can only ever be moved once per pass, so this is its frame rate. */
    ps2_kprintf("[PSTAT] border: spent=%u,%u lost=%u,%u discarded x=%u%% y=%u%% onwall x=%u%% y=%u%%\n",
                (unsigned int)s->spent_x, (unsigned int)s->spent_y,
                (unsigned int)s->lost_x, (unsigned int)s->lost_y,
                (unsigned int)(s->spent_x + s->lost_x
                                   ? s->lost_x * 100u / (s->spent_x + s->lost_x) : 0u),
                (unsigned int)(s->spent_y + s->lost_y
                                   ? s->lost_y * 100u / (s->spent_y + s->lost_y) : 0u),
                (unsigned int)(s->passes ? s->wall_x * 100u / s->passes : 0u),
                (unsigned int)(s->passes ? s->wall_y * 100u / s->passes : 0u));
    ps2_kprintf("[PSTAT] loop: %u passes over %u frames -> %u.%u Hz\n",
                (unsigned int)s->passes, (unsigned int)frames,
                (unsigned int)(frames ? s->passes * 1000u / frames : 0u),
                (unsigned int)(frames ? s->passes * 10000u / frames % 10u : 0u));
    /* Which layer took the reports off the bus, and how long they waited afterwards -- the
     * two rows ASYNC.txt Tier-1 asks this port to prove, because the deliverable is not that
     * a 1 kHz clock exists but that the pointer is serviced by it.  Everything here is
     * cumulative since the handover, not this window's: `pass` has to stay at the handful of
     * reports the boot drained before the plane was installed, and any growth in it beside
     * `plane` says the pass is reading the rings again.  `collected on N periods` is the row
     * that carries the claim with no hand on the mouse: a report needs motion, a period needs
     * only the clock, so N climbing at the plane's rate with reports at zero is an idle
     * collector and not an absent one.  `held` is the queue's own wait -- the distance between
     * the bus and the paint -- and §10's 2 ms pointer-update gap is what it answers to. */
    {
        const ps2_usb_plane_stats_t *p = ps2_usb_plane_stats();
        const uint32_t wcet_100 = ps2_irq_wcet_ticks() * 100u / btron_ee_ticks_per_us();

        ps2_kprintf("[PSTAT] plane: reports=%u on the input plane, %u in a pass, dropped=%u queue max=%u of %u, collected on %u periods\n",
                    (unsigned int)p->plane_reports, (unsigned int)p->pass_reports,
                    (unsigned int)p->dropped, (unsigned int)p->queue_max,
                    (unsigned int)p->queue_slots, (unsigned int)p->plane_periods);
        ps2_kprintf("[PSTAT] plane: held max=%u us mean=%u us over %u delivered -- isr %u.%02u us of the %u us budget, overruns=%u\n",
                    (unsigned int)p->held_max_us,
                    (unsigned int)(p->held_n ? p->held_sum_us / p->held_n : 0u),
                    (unsigned int)p->held_n,
                    (unsigned int)(wcet_100 / 100u), (unsigned int)(wcet_100 % 100u),
                    (unsigned int)ASYNC_ISR_BUDGET_US,
                    (unsigned int)ps2_irq_overruns());
    }
    /* The handler's budget is above; these are the two worker planes' own budgets, and
     * the pair is what makes "equal-priority planes" a checkable claim on this machine
     * rather than a description of the design. */
    ps2_plane_run_print();
    /* Both axes, from ps2_chain_print() above.  The pair is the answer to a
     * difference in feel that is a difference in gain rather than in paint cost:
     * the two rows should read the same px/count for the same host slide, and the
     * [PSTAT] axis rows in ps2_paint_print() are what says so when they do not. */
    ps2_chain_print("x", s->sum_ax, s->spent_x);
    ps2_chain_print("y", s->sum_ay, s->spent_y);
    /* Tenths, because this formatter has no fractional conversion of its own.  A
     * window with nothing in it is not a comparison, so `ptrstat boot` closes
     * silently and the first real slide is the first row here. */
    if (s_prev_sum_ax || s_prev_sum_ay) {
        const uint32_t rx = s_prev_sum_ax ? s->sum_ax * 10u / s_prev_sum_ax : 0u;
        const uint32_t ry = s_prev_sum_ay ? s->sum_ay * 10u / s_prev_sum_ay : 0u;
        ps2_kprintf("[PSTAT] vs previous window: x=%u.%ux  y=%u.%ux\n",
                    (unsigned int)(rx / 10u), (unsigned int)(rx % 10u),
                    (unsigned int)(ry / 10u), (unsigned int)(ry % 10u));
    }
    /* The two-speed pair, from every slide the session has seen rather than from
     * this window.  It sits here because it is the same question the row above
     * asks -- which layer multiplied the distance -- and it answers it without
     * asking the hand to type between its own two slides. */
    ps2_ptr_stroke_row();
    s_prev_sum_ax = s->sum_ax;
    s_prev_sum_ay = s->sum_ay;
    s_ptrst = (ps2_ptrst_t){ 0 };
}

/* ── What One Paint Pass Costs, And How Old The Pointer Was ──────── */

/* "Too much latency" is two different complaints and they need different fixes,
 * so both are measured here rather than inferred: how long a pass takes, split
 * across the three sweeps it makes over the band set (ps2_paint_bands() times
 * them),
 * and how long a hand movement waits between arriving on the bus and being on the
 * screen.
 *
 * The age is stamped at the report and read after the flush, so it is the latency
 * the eye gets -- queueing plus the paint -- and not the queueing alone that the
 * `loop:` row implies.  It is the row that tells the two complaints apart: a large
 * average with a small maximum is the paint being uniformly slow, and a large
 * maximum on a small average is one stall in the loop. */
typedef struct {
    uint32_t n;                              /* passes timed */
    uint32_t full_n;                         /* ... of which repainted the canvas */
    uint32_t px_sum, px_max;                 /* area of each present, in pixels */
    uint32_t bands_sum, bands_max;           /* separate presents inside one pass */
    uint32_t render_sum, render_max;
    uint32_t swap_sum,   swap_max;
    uint32_t upload_sum, upload_max;
    uint32_t lat_n;                          /* painted moves, a subset of n */
    uint32_t lat_sum, lat_max;               /* ms, OHCI frames */
    /* The same two totals split by which axis the pass spent most of its pixels
     * on: [0] sideways, [1] up-or-down.
     *
     * This is where the axis complaint about the cursor was settled, and it was
     * settled against the full-width band model this replaced.  There, a sideways
     * pass charged 800x16 px whatever the distance was, because the sprite's old and
     * new rows always shared one band; a vertical one charged 800x17 px for a 4-count
     * move and 800x32 px in two bands for a 64-count one -- 5369 us per pass against
     * 10738 us, 27 painted cursor positions per second against 13.  The pixels the
     * two axes moved matched exactly (1, 6, 24 and 47 per pass at 4, 16, 64 and 127
     * counts), so the gain was symmetric and the present was not: the one combination
     * that makes an axis feel different while the counts are identical.
     *
     * A band is a rectangle now, so both axes pay for the damage they did and these
     * two rows should read the same cost per pixel.  They are kept for that: drift
     * apart again means the present has become asymmetric in some other way, and the
     * `chain` rows in ps2_ptrst_print() say whether it is the counts instead. */
    uint32_t ax_n[2], ax_px[2], ax_us[2];
    uint32_t ax_lat_n[2], ax_lat_sum[2], ax_lat_max[2];
} ps2_paint_t;

static ps2_paint_t s_paint;
/* The axis of the move the current pass owes its paint for, -1 for a pass that
 * painted without spending a move (a keystroke, an app's invalidate, a timer net).
 * Set by ps2_ptr_service(), which is where the pass's spent pixels are known. */
static int s_paint_dom = -1;
/* Set by ps2_ptr_service() when the pass spent pointer distance, and consumed by
 * the paint that renders it, so the age spans from the oldest report the pass
 * spent to the flush that made it visible. */
static uint32_t s_paint_age_f0;
static int s_paint_owed;

static void ps2_paint_note(uint32_t px, uint32_t bands, int full, int dom,
                           uint32_t render_us, uint32_t swap_us, uint32_t upload_us,
                           uint32_t lat_ms, int lat_valid)
{
    s_paint.n++;
    if (full) s_paint.full_n++;
    s_paint.px_sum += px;  if (px > s_paint.px_max) s_paint.px_max = px;
    s_paint.bands_sum += bands; if (bands > s_paint.bands_max) s_paint.bands_max = bands;
    s_paint.render_sum += render_us;  if (render_us > s_paint.render_max) s_paint.render_max = render_us;
    s_paint.swap_sum   += swap_us;    if (swap_us   > s_paint.swap_max)   s_paint.swap_max   = swap_us;
    s_paint.upload_sum += upload_us;  if (upload_us > s_paint.upload_max) s_paint.upload_max = upload_us;
    if (dom == 0 || dom == 1) {
        s_paint.ax_n[dom]++;
        s_paint.ax_px[dom] += px;
        s_paint.ax_us[dom]   += render_us + swap_us + upload_us;
    }
    if (!lat_valid) return;
    s_paint.lat_n++;
    s_paint.lat_sum += lat_ms;
    if (lat_ms > s_paint.lat_max) s_paint.lat_max = lat_ms;
    if (dom == 0 || dom == 1) {
        s_paint.ax_lat_n[dom]++;
        s_paint.ax_lat_sum[dom] += lat_ms;
        if (lat_ms > s_paint.ax_lat_max[dom]) s_paint.ax_lat_max[dom] = lat_ms;
    }
}

/* Averaged over the window's passes, with the worst pass beside it: an average
 * alone cannot tell a uniformly slow repaint from an occasional 40 ms stall, and
 * those are different bugs.  The band column is what makes the two present paths
 * attributable: `swap`/`upload` scale with `px`, so a pass rate that is still
 * low with a small average band is the composite or the bus, not the flush. */
static void ps2_paint_print(void)
{
    const ps2_paint_t *p = &s_paint;

    if (!p->n) {
        ps2_kprintf("[PSTAT] paint: no pass timed -- 'startx' first, then move the mouse\n");
        return;
    }
    ps2_kprintf("[PSTAT] paint: n=%u passes presented (%u whole canvas, %u banded) px %u/%u of %u (max/avg)\n",
                (unsigned int)p->n, (unsigned int)p->full_n,
                (unsigned int)(p->n - p->full_n),
                (unsigned int)p->px_max, (unsigned int)(p->px_sum / p->n),
                (unsigned int)(PS2_SCREEN_WIDTH * PS2_SCREEN_HEIGHT));
    /* Two bands in one pass is what the sprite's two positions do when they no
     * longer share rows, which vertical travel produces and sideways travel does
     * not: sideways merges into one band whose x span is the sweep, while vertical
     * pays a composite, a swap and a GIF stream for each.  Under the full-width
     * model this column was the axis asymmetry; at rect-limited bands it is a
     * count of setups, and the axis rows below say whether the two still differ. */
    ps2_kprintf("[PSTAT] paint: bands/pass %u.%02u avg, %u max\n",
                (unsigned int)(p->bands_sum / p->n),
                (unsigned int)(p->bands_sum * 100u / p->n % 100u),
                (unsigned int)p->bands_max);
    ps2_kprintf("[PSTAT] paint: n=%u us/pass  render %u/%u  swap %u/%u  upload %u/%u  (max/avg)\n",
                (unsigned int)p->n,
                (unsigned int)p->render_max, (unsigned int)(p->render_sum / p->n),
                (unsigned int)p->swap_max,   (unsigned int)(p->swap_sum / p->n),
                (unsigned int)p->upload_max, (unsigned int)(p->upload_sum / p->n));
    if (p->lat_n) {
        ps2_kprintf("[PSTAT] lat: mouse->on screen avg=%u ms max=%u ms over %u moves\n",
                    (unsigned int)(p->lat_sum / p->lat_n), (unsigned int)p->lat_max,
                    (unsigned int)p->lat_n);
    } else {
        ps2_kprintf("[PSTAT] lat: no move was painted in this window\n");
    }
    /* The two halves of the same window, one per axis, from the pass's dominant
     * travel.  This is the row set that reads "vertical feels different from
     * horizontal" as a number, and it is the row set that settled it: the vertical
     * row rose to 800x32 px and 10738 us per pass while the horizontal row sat at
     * 800x16 px and 5369 us, on identical per-axis distance.  The band model was
     * charging for the axis, which is what the rect-limited present above removes --
     * so these two rows should now read the same us per px, and a difference between
     * them after this is a new bug rather than the old one.  If the two rows
     * cost the same but the hand still reports a difference, the counts themselves
     * did not match the distance, which is the `chain` pair in ps2_ptrst_print()
     * instead.  A window with only one direction in it reads zero passes on the
     * other, so sweep one axis at a time and this tells the two apart by itself. */
    {
        int k;
        static const char *const names[2] = { "horiz", "vert " };

        for (k = 0; k < 2; k++) {
            if (!p->ax_n[k]) continue;
            ps2_kprintf("[PSTAT] axis %s: n=%u px/avg=%u us/pass=%u lat %u/%u ms over %u\n",
                        names[k], (unsigned int)p->ax_n[k],
                        (unsigned int)(p->ax_px[k] / p->ax_n[k]),
                        (unsigned int)(p->ax_us[k] / p->ax_n[k]),
                        (unsigned int)(p->ax_lat_n[k] ? p->ax_lat_sum[k] / p->ax_lat_n[k] : 0u),
                        (unsigned int)p->ax_lat_max[k],
                        (unsigned int)p->ax_lat_n[k]);
        }
    }
    /* `bfn` is the Pi 400 HUD's name for the same counter, and it is the row that
     * says whether a band was a band: the compositor clips only out of its cached
     * background, and a miss rebuilds the whole desktop -- fill, grid, five LZW icon
     * decodes -- before clipping anything.  The cold paint latches the cache, so a
     * healthy session reads zero; every count above that is a band that cost a whole
     * canvas, and the `render` column above is where it shows up. */
    ps2_kprintf("[PSTAT] compositor: bfn=%u background cache misses (0 = every band clipped)\n",
                (unsigned int)g_render_stats.bg_full_calls);
    /* The pass above is timed in three stages that are this port's own; these are
     * the stages the shared compositor times inside itself, and they are the ones
     * that say what a still-expensive band was doing.  `bg` is the one that moves
     * when the cache latches, `wins` is how much of the stack the walk drew out of
     * how long it is, and a `blit` that dominates with few windows is a different
     * bug from a `frame` that dominates with many. */
    ps2_kprintf("[PSTAT] compositor: worst us  bg=%u (%u px)  frame=%u  paint=%u  blit=%u  panel=%u  bars=%u  comp=%u  wins=%u/%u\n",
                (unsigned int)g_render_stats.bg_us, (unsigned int)g_render_stats.bg_worst_px,
                (unsigned int)g_render_stats.frame_us, (unsigned int)g_render_stats.paint_us,
                (unsigned int)g_render_stats.blit_us, (unsigned int)g_render_stats.panel_us,
                (unsigned int)g_render_stats.bars_us, (unsigned int)g_render_stats.comp_us,
                (unsigned int)g_render_stats.wins_drawn, (unsigned int)g_render_stats.wins_walked);
    s_paint = (ps2_paint_t){ 0 };
}

/* ── The Pointer's Own Chords ─────────────────────────────────────── */

/* Ctrl+[ slower, Ctrl+] faster, Ctrl+\ reads the window out.
 *
 * A ladder rather than a free number, because the complaint these answer is "the
 * cursor outruns the hand", and settling that is a series of tries in one session:
 * step, move, step again.  Each of the four prompt commands that can do the same
 * needs Ctrl+Q out of the desktop first, which throws away the very session whose
 * feel is being judged.
 *
 * 16/256 per step puts the shipped 96 five steps from a quarter of a pixel per
 * count and ten from 1:1, and 256 is the verbatim count -- the bound a hand has
 * already called too fast, so it is the ceiling here rather than a guess at one.
 *
 * The carries and the unspent queue go with the gain for the reason `sens` already
 * gives: a subpixel remainder belongs to the scale that made it, and distance the
 * old gain had not finished spending would arrive as movement the hand no longer
 * wants. */
#define PS2_PTR_CHORD_STEP 16
#define PS2_PTR_CHORD_MIN  16
#define PS2_PTR_CHORD_MAX  PS2_MOUSE_MULT_FP

static void ps2_ptr_chord(uint32_t key)
{
    if (key == '\\') {
        /* The same two calls `ptrstat` makes, on the same window it closes: the
         * histogram and the chain rows answer whether the wire carries distance or
         * somebody else's speed, which is the question a gain cannot be set
         * without.  Printed into the emulator's log, which is where the console
         * rows already go. */
        ps2_kprintf("[PS2] Pointer gain ");
        ps2_ptr_gain_row();
        ps2_ptrst_print("chord");
        ps2_paint_print();
        return;
    }

    {
        int32_t next = s_ptr_mult_fp + (key == ']' ? PS2_PTR_CHORD_STEP
                                                   : -PS2_PTR_CHORD_STEP);
        if (next < PS2_PTR_CHORD_MIN) next = PS2_PTR_CHORD_MIN;
        if (next > PS2_PTR_CHORD_MAX) next = PS2_PTR_CHORD_MAX;
        /* The chord is a fourth hand on the same dial (LAW 13e), which is what
         * lets Ctrl+\ print a number the Settings panel then shows selected. */
        g_ptr_gain_fp = next;
        s_ptr_gain_seen = next;
        ps2_ptr_gain_install(0);
    }
    ps2_kprintf("[PS2] Pointer %s: ", key == ']' ? "faster" : "slower");
    ps2_ptr_gain_row();
    ps2_kprintf("[PS2] To ship this, set PS2_EMU_MULT_FP = %d  (`sens %d` for one run)\n",
                (int)s_ptr_mult_fp,
                (int)((s_ptr_mult_fp * 100 + PS2_MOUSE_MULT_FP / 2) / PS2_MOUSE_MULT_FP));
    ps2_kprintf("[PS2] Ctrl+\\ prints the counts behind this gain.\n");
}

/* ── Calibration Against A Known Host Movement ───────────────────── */

/* Every other number here counts what the wire carried; this one asks what the
 * *hand* did, which is the only quantity a person can supply on purpose and the
 * only denominator the shipped gain actually needs.
 *
 * The chain is px = counts x mult/256, and host px = counts / cpg, so
 *
 *     px per host px = (mult/256) x cpg        and therefore        mult = target_fp x host px / counts
 *
 * with target_fp = target x 256.  One multiplication of two bounded numbers and
 * one division, so no 64-bit intermediate is needed: the arguments are clamped
 * below and the gain above, and the widest product here is 2048 x 65535.
 *
 * What this was for has been answered from the other side: PCSX2 v2.8.2's own input
 * path spends one count per host cursor pixel (the delta is `QCursor::pos() - window
 * centre` at `DisplayWidget.cpp:321-326`, clamped to +-127 with the remainder carried
 * at `hid.cpp:632-635`), so PS2_EMU_POINTER_SCALE is 1 and the ini gain this constant
 * used to divide out does not exist in that tree.  The measurement still earns its
 * keep: the emulator's source says what a count is worth in the emulator's units, and
 * only a hand says what that is worth on this Mac, whose own cursor acceleration sits
 * in front of every one of those counts.
 *
 * Only x is calibrated, because a display's width is the one host distance that is
 * written on the box; y counts print alongside so a slide that was not straight is
 * visible instead of silently absorbed into the gain. */
static void ps2_ptg_cal(int hostpx, int pct)
{
    uint32_t cx = s_ptg_cx, cy = s_ptg_cy;

    if (hostpx == 0) {
        /* The escape hatch, because these counters run from boot: a window that
         * contains yesterday's mouse is not a measurement of one sweep. */
        s_ptg_cx = s_ptg_cy = 0;
        /* The pair is cleared with them, on the same terms: a session that has
         * yesterday's mouse in it is not a measurement of today's hand. */
        ps2_stroke_reset(&s_ptr_stroke);
        ps2_kprintf("[PTG] Counts cleared.  Sweep the host cursor, then `ptgain <host px>`.\n");
        return;
    }
    if (!cx) {
        ps2_kprintf("[PTG] No counts since the last call.  Sweep the host cursor straight\n");
        ps2_kprintf("[PTG] across the display, then ask again with its width.\n");
        return;
    }
    ps2_kprintf("[PTG] counts x=%u y=%u%s\n", (unsigned int)cx, (unsigned int)cy,
                cy > cx / 8u ? "  <-- sweep was not straight; re-do it" : "");
    /* There is no longer a per-profile caveat to print here: under LAW 13d both
     * profiles reach this arithmetic through the same single gain, so what this
     * command settles is the transfer function itself and not a scale sitting
     * behind somebody's curve. */
    if (cx > 4000000u) {
        ps2_kprintf("[PTG] That is far too many counts for one sweep.  `ptgain 0`, then re-sweep.\n");
        return;
    }
    if (hostpx < 0) {
        ps2_kprintf("[PTG] Now say how far that was on the Mac: `ptgain <host px> 300`,\n");
        ps2_kprintf("[PTG] where <host px> is the display's logical width for a left-to-right\n");
        ps2_kprintf("[PTG] sweep, and 300 is the px-per-host-px target x 100.\n");
        return;
    }
    if (hostpx > 65535) hostpx = 65535;
    if (pct > 800) pct = 800;

    /* Whole-and-remainder rather than a pre-multiplied numerator: two divisions
     * keep this exact at hundredth resolution without ever holding counts*100,
     * which is the one product here a long session could push out of 32 bits.
     * Capped, because 655 counts per host pixel is a mistyped width rather than a
     * measurement, and this figure is the multiplicand of every number below. */
    const uint32_t h = (uint32_t)hostpx;
    uint32_t cpg100 = (cx / h) * 100u + (cx % h) * 100u / h;
    if (cpg100 > 65535u) cpg100 = 65535u;

    ps2_kprintf("[PTG] measured %u.%02u counts per host px  (emu profile assumes %d)\n",
                (unsigned int)(cpg100 / 100u), (unsigned int)(cpg100 % 100u),
                PS2_EMU_POINTER_SCALE);

    /* px per host px = (px per count) x (counts per host px), so the hundredths
     * come from the two figures this command already has and from nothing else --
     * not from the ini, not from the compile-time scale. */
    {
        const uint32_t pxph100 = (uint32_t)s_ptr_mult_fp * cpg100 / 256u;
        if (pct <= 0) {
            ps2_kprintf("[PTG] gain %d/256 px per count = %u.%02u px per host px now\n",
                        (int)s_ptr_mult_fp, (unsigned int)(pxph100 / 100u),
                        (unsigned int)(pxph100 % 100u));
            ps2_kprintf("[PTG] Set it with `ptgain %d <percent>`, percent = px per host px x 100\n",
                        hostpx);
            return;
        }
    }

    {
        int32_t target_fp = (int32_t)pct * PS2_MOUSE_MULT_FP / 100;
        int32_t mult;
        uint32_t got100;
        const char *note = "";

        if (target_fp < 1) target_fp = 1;
        /* Rounded to nearest rather than truncated: the whole point of this
         * command is a gain that comes out of a measurement, and a measurement
         * whose counts happen to be small would otherwise lose a fifth of itself
         * to the floor at every call. */
        mult = (int32_t)((target_fp * (int32_t)h + (int32_t)(cx / 2u)) / (int32_t)cx);
        if (mult < 1) { mult = 1; note = " (clamped: below one pixel per count)"; }
        /* dp_ptr_scale() caps a report at DP_PTR_MAX_PIXELS, and the largest
         * count the wire can carry is 127, so above ~1030 the top of a fast
         * stroke would be quietly eaten by a clamp inside the shaper instead of
         * being set here where it is visible. */
        if (mult > 1024) { mult = 1024; note = " (clamped at 4 px per count)"; }
        got100 = (uint32_t)mult * cpg100 / 256u;
        /* A measured gain goes on the same dial a hand can pick from, so the
         * panel opens showing the stop nearest the reading and `ptgain`'s answer
         * is not a fifth copy of the number (LAW 13e).  A new gain also makes the
         * old gain's subpixel remainder the wrong shape; leaving it would put a
         * fraction of a pixel of somebody else's stroke onto the next report. */
        g_ptr_gain_fp = mult;
        s_ptr_gain_seen = mult;
        ps2_ptr_gain_install(0);
        /* The window is spent: it was one deliberate sweep, and repeating it by
         * accident would double the gain. */
        s_ptg_cx = s_ptg_cy = 0;
        ps2_stroke_reset(&s_ptr_stroke);
        ps2_kprintf("[PTG] gain set to %d/256 px per count = %u.%02u px per host px, asked for %u.%02u%s\n",
                    (int)s_ptr_mult_fp, (unsigned int)(got100 / 100u),
                    (unsigned int)(got100 % 100u),
                    (unsigned int)(pct / 100u), (unsigned int)(pct % 100u), note);
        ps2_kprintf("[PTG] To ship this, set PS2_EMU_MULT_FP to the px per count above; the\n");
        ps2_kprintf("[PTG] counts per host px it used to divide by is settled at 1 from PCSX2's source.\n");
        ps2_kprintf("[PTG] `sens`, this command and the panel's dial all last the session only.\n");
    }
}

void ps2_usb_on_mouse(int dx, int dy, uint8_t buttons)
{
    static uint8_t s_prev_btn = 0;
#if BTRON_HID_TRACE
    /* Report counters alone cannot tell "nothing arrived" from "something
     * arrived and moved the pointer nowhere".  This row answers the second, with
     * the bytes as the device sent them and the distance they added to the queue. */
    static uint32_t s_reports;
    const uint32_t report_no = ++s_reports;
#endif
    /* Tallied before anything else touches them: these are the bytes as the bus
     * delivered them, which is the layer being asked about.  Statistics run on
     * every report, not on a one-in-sixty-four print, so the buckets see the
     * reports the log skips. */
    ps2_ptrst_add(dx, dy);
    /* Accumulate, do not move.  A report is one slice of the source's motion, and
     * how many of them land in one of our passes is set by how long the last paint
     * took -- so the distance a report is worth has to be spent by the pass that
     * renders it.  With the cap off by default this is now the whole policy, but
     * the pair still belongs here rather than in ps2_move_mouse() because a
     * profile's subpixel remainder has to survive between reports, and a screen
     * border would have eaten it. */
    {
        int32_t px, py;
        const int queue_was_empty = (s_ptr_want_x == 0 && s_ptr_want_y == 0);
        ps2_ptr_shape(dx, dy, &px, &py);
        /* Stamp the age on the report that opens a queue, not on every report:
         * what the hand waits for is the first of the batch, and the ones behind
         * it are painted by the same pass.  A queue that sums to zero from
         * equal-and-opposite counts re-stamps, which is the right answer -- that
         * batch paints nothing. */
        if (queue_was_empty && (px || py)) s_paint_age_f0 = ps2_usb_frame_number();
        s_ptr_want_x += px;
        s_ptr_want_y += py;
    }

    /* Buttons are not deferred.  A press that waits for the next pass is a press
     * whose click event carries a position one frame old, and the edge itself can
     * be gone by then; a click landing one frame late is invisible, a click
     * dropped is a lost window. */
    if ((buttons & 1) && !(s_prev_btn & 1)) ps2_click_mouse(1, 1);
    if (!(buttons & 1) && (s_prev_btn & 1)) ps2_click_mouse(1, 0);

    if ((buttons & 2) && !(s_prev_btn & 2)) ps2_click_mouse(2, 1);
    if (!(buttons & 2) && (s_prev_btn & 2)) ps2_click_mouse(2, 0);

    s_prev_btn = buttons;

#if BTRON_HID_TRACE
    /* What the report asked for and what is now waiting to be spent.  The spent
     * distance is the pass's row, below. */
    if (report_no <= 12u || (report_no & 63u) == 0) {
        ps2_kprintf("[PTR] #%u %d,%d>%d,%d queued=%d,%d btn=%x at %d,%d f=%u\n", (unsigned int)report_no,
                    dx, dy, (int)s_ptr_want_x, (int)s_ptr_want_y,
                    (int)s_ptr_defer_x, (int)s_ptr_defer_y,
                    (unsigned int)buttons, s_mouse_x, s_mouse_y,
                    (unsigned int)ps2_usb_frame_number());
    }
#endif
}

/* How many INPUT periods this pass owes, taken from the plane table rather than
 * from the pass counter.  A pointer step is a per-period thing -- that is what the
 * bus's 1 ms frame means -- and the loop makes a very different number of passes a
 * second depending on what is painting, so the budget one pass may spend has to be
 * the periods that actually elapsed or "px per pass" is really frame time.
 *
 * It advances by the whole periods it consumed rather than to `now`, so a pass that
 * runs early cannot borrow against the next one, and it floors at one so a caller
 * that services the pointer itself -- the loopback calibrator, one report at a
 * time -- is never starved by a clock that has not reached a millisecond yet.  The
 * clamp is the table's bound on the work: coming back from a long paint costs one
 * pass, not a catch-up run.  With no Count to read there is no cadence to price,
 * and one period is what a pass has always been worth. */
static int32_t ps2_ptr_input_slices(void)
{
    const uint32_t period_ticks = btron_planes()->input_period_us * btron_ee_ticks_per_us();
    uint32_t slices, now;

    if (!s_timebase_ok) return 1;
    now = ps2_count_read();
    if (!s_ptr_input_due) {
        s_ptr_input_due = now;
        return 1;
    }
    slices = (uint32_t)(now - s_ptr_input_due) / period_ticks;
    if (slices < 1u) slices = 1u;
    if (slices > btron_planes()->input_slices_max) slices = btron_planes()->input_slices_max;
    s_ptr_input_due += slices * period_ticks;
    return (int32_t)slices;
}

/* Spend the accumulated pointer distance, at most the periods that have passed
 * since the last service could spend.  Called once per pass of each event loop,
 * before the events are dispatched and the screen is painted, so the movement and
 * the paint agree on one position. */
static void ps2_ptr_service(void)
{
    const int32_t ask_x = s_ptr_want_x, ask_y = s_ptr_want_y;
#if BTRON_HID_TRACE
    static uint32_t s_moves;
#endif
    const int x0 = s_mouse_x, y0 = s_mouse_y;
    const int32_t slices = ps2_ptr_input_slices();

    /* The dial first: a hand that settled the gain from the Settings panel or the
     * launcher is answered by the next pass, which is also the pass that would
     * otherwise spend the counts under the OLD gain.  No-ops when nothing moved
     * the variable, so the cost is one compare per pass. */
    ps2_ptr_gain_sync();
    /* The cap is pixels per INPUT period, so a pass owed four periods may spend
     * four of them.  With the shipped cap off (PS2_PTR_MAX_STEP 0) this product is
     * zero and dp_ptr_limit() treats a non-positive limit as no limit at all, so
     * the slicing is the identity exactly when the limiter is -- which is the
     * point: the shipped pointer is proportional, and `maxstep N` measures the
     * alternative against the machine's clock instead of against the loop rate. */
    const int32_t step = s_ptr_max_step * slices;
    int32_t px, py;

    s_ptr_want_x = s_ptr_want_y = 0;
    px = dp_ptr_limit(ask_x, step, &s_ptr_defer_x);
    py = dp_ptr_limit(ask_y, step, &s_ptr_defer_y);
    if (px || py) ps2_move_mouse((int)px, (int)py);
    /* Every pass, including a pass that moved nothing: a cursor parked on a wall
     * while the hand pushes into it is the case being measured, and it is exactly
     * the one that spends no pixels.  With the cap off -- the shipped default --
     * dp_ptr_limit() is the identity, so anything here that was not spent was
     * taken by the border. */
    ps2_ptrst_pass(ask_x, ask_y, s_mouse_x - x0, s_mouse_y - y0);
    if (!px && !py) return;
    /* Which way this pass went, in the pixels it actually spent rather than the
     * counts that asked for them: the border can take one axis away entirely, and
     * what the present then dirties is what the pixels did, not what the hand
     * wanted.  Consumed by ps2_paint_note() through the pass that follows. */
    s_paint_dom = ((py < 0 ? -py : py) > (px < 0 ? -px : px)) ? 1 : 0;
    /* This pass owes a paint, and the age of the movement is only finished when
     * that paint reaches the GS.  Only the desktop loop reads and clears it; a
     * pass that never paints leaves the stamp alone, so the age then spans the
     * wait it really was rather than being shortened by a paint that did not
     * happen. */
    if (s_paint_age_f0) s_paint_owed = 1;

#if BTRON_HID_TRACE
    /* The pass's own row, because this is the pair the hand judges: everything
     * that arrived since the last paint, and how far the cursor actually
     * travelled.  `left` non-zero across consecutive rows is the limiter still
     * paying a stroke out -- expected during a sweep, and the defect if it is
     * still nonzero after the mouse has stopped, because that is the pointer
     * walking off on its own. */
    if (s_moves < 12u || (s_moves & 63u) == 0) {
        ps2_kprintf("[MOVE] ask=%d,%d px=%d,%d left=%d,%d at %d,%d f=%u\n",
                    (int)ask_x, (int)ask_y, (int)px, (int)py,
                    (int)s_ptr_defer_x, (int)s_ptr_defer_y,
                    s_mouse_x, s_mouse_y, (unsigned int)ps2_usb_frame_number());
    }
    s_moves++;
#endif
}

/* ── Pointer Loopback Calibration ───────────────────────────────── */

/* Four hooks and a canvas size are the whole contract between the shared
 * measurement in src/graphics/dp_cal.c and this port.  The seam for feeding a
 * report is the driver's own entry rather than the position setter, so a
 * synthetic count travels the same road a count off the bus travels: decoded,
 * scaled, bordered, and posted as an event.  Which is the point -- the thing
 * being measured is this port, and anything the shortcut would skip is a place a
 * bug could be hiding.  */
static void ptr_cal_send(int dx, int dy)
{
    uint8_t report[4];
    report[0] = 0;
    report[1] = (uint8_t)(int8_t)dx;
    report[2] = (uint8_t)(int8_t)dy;
    report[3] = 0;
    ps2_usb_process_mouse_report(report);
    /* And spend it.  The measurement waits for the position the report produced,
     * and since the cap the two are no longer the same instant -- without this
     * row the calibrator would read the cursor where it was before the report. */
    ps2_ptr_service();
}

static void ptr_cal_get(int *x, int *y)
{
    *x = s_mouse_x;
    *y = s_mouse_y;
}

static void ptr_cal_set(int x, int y)
{
    ps2_move_mouse(x - s_mouse_x, y - s_mouse_y);
}

static void ptr_cal_row(const char *tag, long v0, long v1, long v2, long v3)
{
    /* Four conversions and five arguments, no %ld: the row is a fixed shape so
     * the columns line up across ports, and the numbers are small enough by
     * construction that the width buys nothing. */
    ps2_kprintf("[CAL] %-5s %4d %6d %6d %6d\n", tag,
                (int)v0, (int)v1, (int)v2, (int)v3);
}

static const dp_cal_port_t s_ptr_cal_port = {
    ptr_cal_send, ptr_cal_get, ptr_cal_set,
    PS2_SCREEN_WIDTH, PS2_SCREEN_HEIGHT,
    ptr_cal_row,
};

#if BTRON_HID_TRACE
/* The state of the input source, printed on a count of poll-loop iterations
 * rather than on a clock, so that the two silences which look the same from the
 * outside come apart: rows stopping entirely is the loop stopping, and rows
 * whose f= field stops moving is the controller's frame engine stopping while we
 * keep asking it.
 *
 * The [SCH0] and [SCH1] halves are each device's interrupt endpoint as the
 * controller's own memory says it looks.  A queued and walkable descriptor has h= equal to td= apart from
 * the two low bits (h bit0 is the halt the controller sets on the endpoint, bit1
 * its data toggle); h= == t= means the queue is empty, which is a poll that will
 * never be answered; and a cc= or an nx= pointing at a descriptor we never
 * queued says it was walked and refused rather than never walked at all. */
static void ps2_log_usb_state(int stage)
{
    static uint32_t last_fm;
    static uint32_t since_calls;   /* rows counted while the controller stayed put */
    static int last_stage;
    uint32_t fm = ps2_usb_frame_number();
    int due;

    /* One row per second of the controller's own clock, not per N loop
     * iterations: the shell and the desktop iterate a thousand times apart in
     * speed, and a heartbeat tied to iterations floods the console in one and
     * goes silent in the other.  When that clock stops, count calls instead --
     * a controller that stopped producing frames is exactly the row worth
     * having.  And always print the first row of a stage, because the moment
     * input dies at 'startx' is the transition, not what came before it. */
    if (fm != last_fm) {
        due = ((uint32_t)(fm - last_fm) >= 1000u);
    } else {
        due = (++since_calls >= 50000u);
    }
    if (!due && stage == last_stage) return;
    last_fm = fm;
    last_stage = stage;
    since_calls = 0;
    const ps2_usb_dev_t *d0 = ps2_usb_dev(0);
    const ps2_usb_dev_t *d1 = ps2_usb_dev(1);

    ps2_kprintf("[DEV] %c up=%d sh=%d r=%u f=%x ic=%x ctl=%x dn=%x l=%d,%d e=%u,%u p=%u,%u\n",
                stage, ps2_usb_host_up(), ps2_usb_shadow_lost(),
                (unsigned int)ps2_usb_kbd_reports(),
                (unsigned int)ps2_usb_frame_number(),
                (unsigned int)ps2_usb_reg(OHCI_REG_INTSTATUS),
                (unsigned int)ps2_usb_reg(OHCI_REG_CONTROL),
                (unsigned int)ps2_usb_reg(OHCI_REG_DONE_HEAD),
                d0 ? d0->live : -1, d1 ? d1->live : -1,
                (unsigned int)(d0 ? d0->errors : 0u),
                (unsigned int)(d1 ? d1->errors : 0u),
                (unsigned int)(d0 ? d0->polls : 0u),
                (unsigned int)(d1 ? d1->polls : 0u));
    /* The two trailing counters are the cures this driver has already applied:
     * the frame engine restarted, and a held packet cancelled.  Both belong to a
     * control transfer, so neither should move once enumeration is over. */
    ps2_kprintf("[SCH0] id=%x h=%x t=%x td=%x nx=%x cc=%x cbp=%x pd=%u sl=%u ra=%x rl=%x\n",
                (unsigned int)(d0 ? d0->desc_id : 0u),
                (unsigned int)ps2_usb_intr_word(0, INTR_W_ED_HEAD),
                (unsigned int)ps2_usb_intr_word(0, INTR_W_ED_TAIL),
                (unsigned int)ps2_usb_intr_word(0, INTR_W_TD_IOP),
                (unsigned int)ps2_usb_intr_word(0, INTR_W_TD_NEXT),
                (unsigned int)ps2_usb_intr_word(0, INTR_W_TD_CC),
                (unsigned int)ps2_usb_intr_word(0, INTR_W_TD_CBP),
                (unsigned int)ps2_usb_intr_word(0, INTR_W_PENDING),
                (unsigned int)ps2_usb_intr_word(0, INTR_W_SLOT),
                (unsigned int)ps2_usb_engine_rearms(),
                (unsigned int)ps2_usb_async_releases());
    /* The second device gets the same row because it is the one the pointer
     * rides on, and its p= above says only that nothing retired: a queue the
     * controller walks and a device that NAKs every visit look the same from
     * that column.  With the queue here, h= != t= and a nonzero cc= that never
     * moves is a refusal, and h= == t= is a ring this driver never armed. */
    ps2_kprintf("[SCH1] id=%x h=%x t=%x td=%x nx=%x cc=%x cbp=%x pd=%u sl=%u mps=%u\n",
                (unsigned int)(d1 ? d1->desc_id : 0u),
                (unsigned int)ps2_usb_intr_word(1, INTR_W_ED_HEAD),
                (unsigned int)ps2_usb_intr_word(1, INTR_W_ED_TAIL),
                (unsigned int)ps2_usb_intr_word(1, INTR_W_TD_IOP),
                (unsigned int)ps2_usb_intr_word(1, INTR_W_TD_NEXT),
                (unsigned int)ps2_usb_intr_word(1, INTR_W_TD_CC),
                (unsigned int)ps2_usb_intr_word(1, INTR_W_TD_CBP),
                (unsigned int)ps2_usb_intr_word(1, INTR_W_PENDING),
                (unsigned int)ps2_usb_intr_word(1, INTR_W_SLOT),
                (unsigned int)(d1 ? d1->mps : 0u));
}
#endif

/* ── Interactive Shell (Stage 1 Console & Stage 2 GUI Shell) ────── */

/* ── LANE 19 rung 1: `gsquad`, two squares over two senders, as a test of the GS rasteriser ─────
 *
 * Everything this port has ever confirmed on a screen -- the desktop, a window's chrome, a
 * terminal glyph, the boot log -- reached VRAM through ps2_gs_upload(), a Host->Local IMAGE stream
 * that copies the canvas and asks the GS to draw nothing.  The recorded eegs stream is posted and
 * DMAC says it was consumed (`posted=52 started=52 drop=0`), and that receipt proves only that
 * quadwords left the EE.  So the question increment 5 cannot be closed without is the one this
 * verb answers with a single look: does the GS's rasteriser put any pixels at all on the page the
 * PCRTC displays?
 *
 * It is a Stage-1 command on purpose.  At the prompt nothing is compositing and no layer is
 * recording, so the primitive is the only variable left -- and the block cannot be the software
 * path's work, because this function touches no canvas byte.  A red pixel on the monitor is a
 * pixel nobody drew.
 *
 * The four calls are the ones the production path uses, spelled the same way, so a PASS here is
 * evidence about the shipped stream rather than about a second and easier pipeline:
 *
 *   ps2_gs_draw_env_apply() -- the same nine-register AD packet src/gl/eegs_present.c puts in front
 *     of every layer, with win_x/win_y at 0 so an absolute vertex lands where it says and SCISSOR
 *     over the whole canvas, so nothing about the rect's survival can be blamed on the clip.
 *   gs_poster_packet() -- LANE 9b's SPRITE emitter, host-gated word for word against literals in
 *     verify/tests/test_ps2_gs_poster.c, which is what lets this rung claim the packet is right and
 *     leave the screen as the only unknown.
 *   ps2_gs_gif_send() -- the synchronous sender, the one every console upload has already proven on
 *     this bus.  It reaps first, so the environment it follows has already gone out.
 *   ps2_gif_post() + ps2_gif_drain() + ps2_gif_reap() -- the LANE 10 ring, which is the path the
 *     recorded eegs stream rides, and which at rung 1 had never carried a drawing group in front of
 *     a witness.  `gsread` has since put the dense SPRITE group on it and found that group on the
 *     page, so this half of the pair is measured rather than assumed.  The drain and reap are the
 *     ring's own lifetime law honoured: the packet it was given is a static this command may rebuild,
 *     so it waits for the transfer to finish.
 *
 * ALPHA and TEST are both zero, which is the plainest thing the GS can be asked to do: the emitter's
 * PRIM word has ABE clear, so the blend stage is bypassed and a fragment is the source colour --
 * ALPHA's four selects only name operands of an equation that is not running -- and with ZTE clear
 * there is no depth reference to fail against.  Both colours are canvas words in the canvas's own
 * order, A[31:24] B[23:16] G[15:8] R[7:0]: 0xff0000ff is opaque RED and 0xffff0000 is opaque BLUE,
 * and each has the A byte every uploaded canvas pixel carries, which keeps the PCRTC's treatment of
 * them identical to a canvas pixel's.  The pair is also this verb's own byte-order control, by
 * position rather than by name: RED is the UPPER rect and BLUE the LOWER one, so a port that read
 * RGBAQ in the BTRON order would show them exchanged -- visible in one look, where a single-colour
 * probe's swap could only be argued about.
 *
 * The print goes first and the fill second, and nothing at all is printed after it.  This is not
 * tidiness, it is the probe's own failure mode, and it has two halves -- the first image of this
 * verb fell for the second one and reported `posted=1 started=1` beside a screen with no red on it,
 * which is "no evidence", not "no rasteriser":
 *
 *   ps2_kprintf() ends in ps2_console_flush(), which uploads the dirty text band at the CANVAS'S
 *   FULL WIDTH (ps2_gs_text_flush() calls ps2_gs_upload(0, top, PS2_SCREEN_WIDTH, h)), so a row
 *   printed after the sprite paints a full-width navy strip over it.  Hence: the receipt is printed
 *   in front of the send, and the send is self-evidencing anyway -- it is the synchronous one, so
 *   the prompt coming back at all is the proof the quadwords left.
 *
 *   The shell prints its own prompt after EVERY command returns (ps2_shell_exec's last line), and
 *   the first image of this verb had already printed five rows and left the cursor below them, so
 *   that prompt was a full-width repaint of its own band right on top of the block -- invisible in
 *   the source of this function, because the offending print is not here.  The cure is the clear
 *   below: ps2_gs_text_clear() homes the cursor at row 0 and marks the whole canvas dirty, so the
 *   first row printed after it repaints the ENTIRE canvas navy -- before the send -- and every row
 *   after that repaints only its own sixteen pixels.  Five short rows put the cursor at row 5, the
 *   prompt takes row 5, and the first rect starts at row 9: three rows of slack, so a keystroke or
 *   two and a couple of ENTERs cannot reach it.  A scroll would (it marks all rows dirty), which is
 *   why the last row says to answer briefly.
 *
 * Two blocks, because one look should answer two questions.  This port has exactly two ways to put a
 * packet on channel 2 -- ps2_gs_gif_send(), the synchronous spin that every console IMAGE upload
 * uses, and the LANE 10 ring, which is what the recorded eegs stream actually rides.  They are not
 * the same code path: the ring hands the quadwords to ps2_gif_hw_start() from a descriptor it owns,
 * the sender hands them over itself and waits.  So the RED rect goes out the synchronous way and the
 * BLUE rect the ring way, and the four readings of one screenshot partition the space that used to
 * need two boots: both blocks = the GS rasterises and the emptiness is in the stream's content; RED
 * only = the ring loses a group the sender delivers; BLUE only = the reverse; neither = no SPRITE
 * group reaches the displayed page however it is sent, which is what rung 2's read-back is for.
 */
#define GSQUAD_X0       32
#define GSQUAD_X1       672
#define GSQUAD_RED_Y0   144   /* row 9: below the five printed rows and the prompt's own row */
#define GSQUAD_RED_Y1   320
#define GSQUAD_BLUE_Y0  336
#define GSQUAD_BLUE_Y1  496
#define GSQUAD_RED      0xff0000ffu   /* canvas order: A=ff B=00 G=00 R=ff -- opaque red */
#define GSQUAD_BLUE     0xffff0000u   /* canvas order: A=ff B=ff G=00 R=00 -- opaque blue */

/* ── LANE 19 rungs 4 and 5: the reference shapes, their colours, and where they live ────────
 *
 * The census can now say "no pixel of either magic colour is anywhere in page 0", which retires the
 * misplaced-draw explanations (a wrong FRAME pitch, a window origin that is not (0,0), a rect that
 * left the scissor) and leaves three variables standing INSIDE the packet: the GIF format this port
 * emits (FLG=PACKED with one quadword per register, and PRE=1 so the tag also carries the primitive
 * word), the sprite's corner nudge, and the SPRITE primitive itself.  They cannot be told apart by
 * reading the manual -- but ps2sdk is in this tree, and its `ee/draw/src/draw2d.c` ships flat
 * sprites and flat triangles that every SDK program has ever put on a real GS, in FLG=REGLIST with
 * PRE=0 and two or three dense data quadwords.  So rung 4 draws shapes beyond gsquad's pair, each a
 * single-variable step from the packet that fails, and the same page walk reads the fork.
 *
 * The first run (2026-10-16, 8 s of PCSX2, no window and no hand) landed CYAN at 7360/7360 px in its
 * exact box, MAGENTA at 7567 and GREEN at 3525 of ~3600, and left RED at 0/112640 and BLUE at
 * 0/102400 over all 480,000 pixels -- which is what a control set is for: it split "the format" into
 * the two things that had been living in one sentence.  The tag differs, and so does the STRIDE: a
 * group of four 64-bit registers is TWO quadwords of data per loop, so the failing packet's four
 * handed the GIF each even-numbered register as the zero sitting above its neighbour -- RGBAQ read as
 * A=0 Q=0, the sprite's second XYZ2 read as 0, i.e. the field's own origin -- and left two quadwords
 * over for the GIF to decode as the next tag.  A transparent colour and a degenerate sprite draw
 * nothing at a packet length that looks entirely legal, which is precisely what rung 3 measured.
 *
 * The second run retired that arithmetic, and it did so by hanging the machine: its fourth control
 * was this port's PACKED/PRE=1 tag over the SDK's dense two-quadword body -- the stride corrected and
 * nothing else changed -- and the three GIF reads that followed all came back `block-never-finished`.
 * The GIF was still waiting for data at the end of the transfer, so a PACKED group with NREG=4 wants
 * FOUR quadwords, which is exactly what gs_poster_packet() already sends.  The shipping packet is
 * therefore well-formed for its own tag and still draws nothing: the stride is innocent, and the
 * variable that is left is the shape of the group -- a PACKED loop carrying a four-register REGS list
 * and PRE=1 -- against the SDK's two precedents, a REGLIST list and a group of single-register AD
 * writes.  YELLOW below is the second of those.
 *
 *   CYAN    gs_poster_ref_sprite_packet(nudge=0) -- the SDK's tag and dense stride, this
 *           file's corners.  PROVEN to land.
 *   MAGENTA the same with the SDK's own corner offsets (START/END_OFFSET, -7/+9 in 1/16-px units).
 *           PROVEN to land, so the nudge is not what a SPRITE needs here.
 *   GREEN   gs_poster_ref_tri_packet() -- the SDK's flat TRIANGLE (NREG=6, three vertices, plain page
 *           offset).  PROVEN to land, so the three-vertex pipeline is fine.
 *   YELLOW  gs_poster_ref_sprite_ad_packet() -- the same four registers as four AD writes, the shape
 *           send_trx_setup_at() uses for every console upload.  PROVEN to land by the third run at
 *           its exact 7,360 px, so PACKED is innocent when its group is one register long, and what
 *           is left between the packet that draws and the packet that ships is a four-register REGS
 *           list CARRIED BY PRE=1 -- one sentence again, and rung 5 is the run that splits it.
 *
 * rung 5 added the fifth control for exactly that split: gs_poster_ref_sprite_packed_packet, the
 * shipping packet's own body quadword for quadword (PACKED, NREG=4, the same four nibbles, four sparse
 * data quadwords) with PRE the only argument the caller changes -- and the poster gate asserted that
 * with pre=1 those five quadwords WERE gs_poster_packet()'s output, word for word.  So the census got
 * its RED row as a second copy of itself rather than as a look-alike, and the one-bit question had a
 * one-bit answer: orange=0/1840, with the page fully accounted for.  PRE=0 over the retired body drew
 * nothing, exactly as PRE=1 did, so the tag's PRIM carry was never the defect and a multi-register
 * PACKED group does not kick on this GS whatever its primitive comes from.  The second of its two
 * branches is the settled one, and both emitters now write CYAN's shape: FLG=REGLIST, PRE=0, PRIM as
 * the loop's own first register, TWO quadwords per loop instead of four.  The gate's word-for-word
 * equality became a NEGATIVE assertion with that change -- what ships must differ from the retired
 * packet in FLG, in PRE and in the stride, and if those two ever agree again the format has been
 * reverted while its host gate still passes (verify/tests/test_ps2_gs_poster.c).  The retired packet
 * itself stays in the census, as the instrument's negative control:
 *
 *   VIOLET  gs_poster_ref_sprite_packed_packet(pre=1) in a 16-pixel gutter of its own, 224..239.
 *           EXPECTED TO DRAW NOTHING.  A census that reports a violet pixel is reporting something
 *           other than the shape it names, and every positive row of that run would have to be
 *           re-read -- which is why a control known not to draw is still worth a box, a colour and a
 *           row: it is the only row on the run that can say the read itself is telling the truth.
 *   ORANGE  is no longer a look-alike of RED.  It is the shipping emitter ITSELF at n=2: two rects side
 *           by side inside the same 40x46 box, in ONE group whose tag says NLOOP 2 -- five quadwords,
 *           one tag and two dense loops.  RED and BLUE are the same emitter at NLOOP 1, so ORANGE is
 *           the one variable that still separates "this format rasterises" from "a group with more
 *           than one loop in it rasterises", and that is the question the recorder's stream lives on:
 *           its groups are three, twelve and up to four thousand loops long.
 *
 *     1840 px  both loops drew: a multi-loop dense group is legal here, and the recorded strip can
 *              ship as one group per primitive run.
 *     920 px   only the group's FIRST loop rasterised.  The format is right and the batch is not, the
 *              recorder has to close its group at every vertex, and the eegs stream's triangles are
 *              arriving on the bus complete and rasterising one apiece.
 *     0 px     no loop drew where a single-rect packet does -- a third finding, and one to read against
 *              RED's own count and the refused mask before quoting it as the GS's answer.
 *
 * All of them go out the SYNCHRONOUS sender: the sender was rung 1's variable, and rung 3 showed
 * neither sender delivers the current shape, so making it a variable again would confound the ones
 * that matter now.  They sit in rows 544..590 -- below the control strip at 512..527, which has to
 * stay the canvas's own navy for the census to mean anything, and above the page's last row.  ORANGE
 * is the one box in that band LEFT of CYAN, at x 16..55, because the band is otherwise spoken for
 * from 64 to 768 and a control whose pixels could be confused with a neighbour's box would cost the
 * run its own discriminator.  VIOLET is the 16-pixel gutter between CYAN and MAGENTA, the only
 * stretch rung 4 left free; it is expected to hold no pixel at all, so a neighbour's ink landing in
 * its count would be the finding rather than a confusion.
 *
 * The names follow the words, not the other way round: a canvas word is A[31:24] B[23:16] G[15:8]
 * R[7:0], so 0xffffff00 is R=0 G=255 B=255, which is cyan, and 0xff00ffff is R=255 G=255 B=0, which
 * is yellow.  The first draft of this block called the former yellow; the census compares the exact
 * word either way, but a row that prints `yellow=` over a cyan square is a row somebody will later
 * read as a colour claim.  0xff0080ff follows the same rule: R=255 G=128 B=0, which is orange, and
 * 0xffa020a0 is R=160 G=32 B=160, which is violet -- the only class here whose green is neither 0 nor
 * 255, so a photo of the band cannot read one control's square as another's. */
#define GSQUAD_CYAN     0xffffff00u   /* A=ff B=ff G=ff R=00 -- opaque cyan */
#define GSQUAD_MAGENTA  0xffff00ffu   /* A=ff B=ff G=00 R=ff -- opaque magenta */
#define GSQUAD_GREEN    0xff00ff00u   /* A=ff B=00 G=ff R=00 -- opaque green */
#define GSQUAD_YELLOW   0xff00ffffu   /* A=ff B=00 G=ff R=ff -- opaque yellow */
#define GSQUAD_ORANGE   0xff0080ffu   /* A=ff B=00 G=80 R=ff -- opaque orange: the two-rect batch */
#define GSQUAD_VIOLET   0xffa020a0u   /* A=ff B=a0 G=20 R=a0 -- opaque violet: the retired shape */
#define GSREF_Y0        544
#define GSREF_Y1        590
#define GSREF_ORA_X0    16    /* the batch's own box, split into two 20-wide rects at 16..36, 36..56 */
#define GSREF_ORA_X1    56
#define GSREF_ORA_SPLIT 36    /* first pixel of the batch's SECOND rect */
#define GSREF_VIO_X0    224   /* rung 4 left this gutter; the negative control lives in it now */
#define GSREF_VIO_X1    240
#define GSREF_CYA_X0    64
#define GSREF_CYA_X1    224
#define GSREF_MAG_X0    240
#define GSREF_MAG_X1    400
#define GSREF_TRI_X0    432   /* the triangle's base row is GSREF_Y1-1 and its apex GSREF_Y0 */
#define GSREF_TRI_X1    512
#define GSREF_TRI_X2    592
#define GSREF_YEL_X0    608   /* the four-register AD control: right of the triangle */
#define GSREF_YEL_X1    768

/* ── LANE 19 rung 7: the ALPHA discriminator's own page band ────────────────────────────────
 *
 * A band of its own, low on the page and clear of gsread's control row (y 544..590) and of any verb's
 * printed glyphs (rows 0..143), so this census reads only pixels this probe drew over the navy canvas
 * and nothing else can account for them.  Five boxes, each 40 x 40, with a gutter of their own
 * between them: Z is the ABE=0 reference (a plain sprite with the alpha byte forced -- it proves the
 * builder's geometry and the read path reach VRAM), P is ABE=1 under the word the LIVE environment
 * ships (gs_pack_alpha(CS,CD,AS,CD,fix=0)), H is ABE=1 under the hardware-order word
 * gs_pack_alpha(3,0,1,1,fix=128), and M and X are rung 28's two candidate LAWS for the XMB ribbon
 * (see the probe's own header).  All five carry the same source colour, so the ONLY thing that can
 * separate them is the ALPHA register. */
#define GSALPHA_Y0      200
#define GSALPHA_Y1      240
#define GSALPHA_Z_X0    64
#define GSALPHA_P_X0    176
#define GSALPHA_H_X0    288
#define GSALPHA_M_X0    400   /* rung 28: (Cd-0)*As>>7 + Cd -- destination brightened by the vertex alpha */
#define GSALPHA_X_X0    512   /* rung 28: (Cs-0)*FIX + Cd -- destination plus the source colour */
#define GSALPHA_BW      40    /* every box's width; x1 = x0 + GSALPHA_BW */
/* Opaque-ish source, distinct from navy, from console ink (0xFFE0E0E0) and from the read sentinel:
 * R=0 G=255 B=0 in the canvas word's own byte order.  The vertex alpha is NOT 0xFF: a blend of a fully
 * opaque source clamps back to the source (`(Cs-Cd)*255/128 + Cd` saturates to Cs on every channel),
 * which would make a real blend and a bypassed blender read identically.  A mid alpha keeps the three
 * outcomes three colours -- destination, a genuine mid mix, or the raw source -- and that separation is
 * the entire instrument. */
#define GSALPHA_CS      0x0000ff00u
#define GSALPHA_AS      0x40u   /* 64: the blender's multiplier, `(Cs-Cd)*64>>7` = half, so a real mix is mid */

/* ── LANE 19 rung 8: the texture-stage discriminator's own page band ────────────────────────
 *
 * Rung 7 cleared ALPHA and rung 6's live [GSTS] row cleared everything else the recorded eegs stream
 * could be dying on -- `texmiss=0` (its planes resolve, so it is not demoted to flat), `cover=1`
 * (not the cover re-upload), `reg_test=0` (no depth/alpha fragment reject), `plate=covered drop=0`
 * (geometry present, delivered).  Yet `[GSLIVE]` still reads the page as its canvas: 2,705 textured
 * triangles lay NO new pixel, while rung 7's one FLAT untextured sprite in the identical env laid its
 * colour.  The only thing the flat sprite does not exercise and the dead stream does is the TEXTURE
 * stage -- TEX0/TEX1 sampling and the CT32 modulate `ps2_gs_tex.c` packs (TCC=1, TFUNC=0).  That is
 * what this band decides.
 *
 * Same band as rung 7 (it runs after it and repaints navy first), three 40x40 boxes.  The whole trick
 * is that each textured plane is filled UNIFORMLY, so the exact ST/UV the sprite samples is
 * irrelevant -- whatever coordinate lands, the texel it reads is the plane's one colour.  That makes
 * the read a pure statement about whether the sampler multiplies vertex by texel at all:
 *   U = an UNTEXTURED sprite, opaque green -- the positive control, the shape rung 7 already proved
 *       lands.  If U does not paint, this run measured no geometry and the two textured rows say nothing.
 *   R = a TEXTURED sprite, white vertex, plane = opaque RED  -> red  means texturing works.
 *   B = a TEXTURED sprite, white vertex, plane = opaque BLUE -> blue means it works, and that R and B
 *       differ proves the fragment is the plane's colour, not the vertex's (a shared result would mean
 *       the sampler is being ignored).
 * The white vertex (all channels 0xFF) means the modulate is out = 1 * texel = texel, so a textured box
 * that reads WHITE is the sampler bypassed (TME not honoured / TEX0 not applied), and one that reads
 * BLACK is a plane read as zero (a format/PSM mismatch between the upload and TEX0). */
#define GS8_Y0        200
#define GS8_Y1        240
#define GS8_U_X0      64
#define GS8_R_X0      176
#define GS8_B_X0      288
#define GS8_BW        40
#define GS8_PLANE     64u   /* each plane is 64x64 CT32, whole thing one colour so any coord reads it */
#define GS8_VERTEX    0xffffffffu  /* R=G=B=A=255: modulate by 1, so a textured fragment == its texel */

/* The drawing environment both Stage-1 GS verbs program, and the one place in this core that names
 * the displayed page for a draw.  The page comes from gs_disp_shown() -- the page the PCRTC is
 * reading -- because these verbs are a closed pair: `gsquad` draws and `gsread` censuses, and the
 * census reads whatever ps2_gs_vram_read() has been pointed at, which is the shown page too.  Draw
 * and read agreeing under any value of LANE 21's dials is the property; today both are page 0 in
 * CT32, so this is the same word the literals made.  It is deliberately NOT gs_disp_drawn(), the
 * writer's page eegs_present.c uses: once rung 4 puts production on the hidden page, a probe that
 * drew there would be censused as black, because nothing would have flipped to show it.
 *
 * The depth page is the top page of the 4 MB, which TEST=0 and ZMSK=1 leave untouched but
 * which still has to hold a format the hardware accepts.  win_x/win_y are 0 so an absolute vertex
 * lands exactly where it says, and the scissor is the whole canvas so nothing about a rect's survival
 * can be blamed on the clip.  ALPHA and TEST are clear: the blend and depth stages are bypassed, a
 * fragment is its source colour, and there is no reference to fail against.  One function holds it
 * because `gsquad` and `gsread` must not be two models of the same page -- a census taken against an
 * environment that differs from the draw it censuses is reading a page nobody painted. */
static void ps2_stage1_gs_env(gs_draw_env *env)
{
    const gs_disp_page_t *pg = gs_disp_shown();

    env->frame_byte  = pg->byte;
    env->frame_px_w  = pg->fbw * 64u;
    env->frame_psm   = pg->psm;
    env->frame_fmsk  = 0u;
    env->zbuf_byte   = GS_VRAM_BYTES - GS_PAGE_FRAME;
    env->zbuf_psm    = GS_PSMZ32;
    env->zbuf_zmsk   = 1u;
    env->win_x = 0u;
    env->win_y = 0u;
    env->clip_x0 = 0u;
    env->clip_y0 = 0u;
    env->clip_x1 = (uint32_t)(PS2_SCREEN_WIDTH - 1);
    env->clip_y1 = (uint32_t)(PS2_SCREEN_HEIGHT - 1);
    env->reg_test  = 0u;
    env->reg_alpha = 0u;
    env->reg_clamp = 0u;
    env->reg_tex0  = 0u;
    env->reg_tex1  = 0u;
}

static void ps2_gsquad_probe(void)
{
    /* One tag pair plus GS_POSTER_REGS_N data quadwords, 16-byte aligned because the DMA reads
     * quadwords.  Sized by the emitter's own macro and never by a literal, so the packet cannot be
     * longer than the buffer that holds it.  The same static is reused for the second rect, and it
     * may be: the first send is synchronous, and the second is reaped before this returns, so no
     * transfer is ever reading a buffer a later call is rewriting. */
    static uint64_t s_pkt[2u * GS_POSTER_QWC(1u)] __attribute__((aligned(16)));
    gs_poster_rect r;
    gs_draw_env env;
    ps2_gif_stats_t st;
    int32_t qw_red, qw_blue;

    /* The canvas is repainted navy here, in front of everything, because the repaint is the point:
     * it is the last full-canvas upload this probe allows itself, and it leaves the page in the one
     * state where a red block has no rival explanation.  See the ordering law above. */
    ps2_gs_text_clear();

    r.x0 = GSQUAD_X0;  r.y0 = GSQUAD_RED_Y0;
    r.x1 = GSQUAD_X1;  r.y1 = GSQUAD_RED_Y1;

    /* The displayed page, named exactly the way src/gl/eegs_present.c names it: frame page 0 at
     * the width the PCRTC reads it with, the top page of VRAM for a depth buffer that TEST=0 and
     * ZMSK=1 leave untouched, the page origin at (0,0) so the emitter's gs_vertex_coord16() pixels
     * are screen pixels, and a scissor over the whole canvas.  One function holds it because this
     * verb and `gsread` must not be two models of the same page -- a census taken against an
     * environment that differs from the draw it censuses is reading a page nobody painted. */
    ps2_stage1_gs_env(&env);

    qw_red = gs_poster_packet(&r, 1u, GSQUAD_RED, s_pkt, gs_poster_qwc(1u));

    ps2_kprintf("[GSQUAD] two SPRITES at x=%d..%d. This command writes no canvas byte.\n",
                GSQUAD_X0, GSQUAD_X1 - 1);
    ps2_kprintf("[GSQUAD] RED  y=%d..%d 0x%08x -- the synchronous sender\n",
                GSQUAD_RED_Y0, GSQUAD_RED_Y1 - 1, (unsigned int)GSQUAD_RED);
    ps2_kprintf("[GSQUAD] BLUE y=%d..%d 0x%08x -- the LANE 10 ring, drained\n",
                GSQUAD_BLUE_Y0, GSQUAD_BLUE_Y1 - 1, (unsigned int)GSQUAD_BLUE);

    if (qw_red < 0) {
        ps2_kprintf("[GSQUAD] the emitter refused the rect (dropped=%u): nothing was sent\n",
                    (unsigned int)gs_poster_dropped());
        return;
    }

    if (ps2_gs_draw_env_apply(&env) != 0) {
        ps2_kprintf("[GSQUAD] the environment was refused (-1): no draw page, so no sprite is sent\n");
        return;
    }

    /* The receipt, and it is the last thing printed -- the senders follow and nothing prints after
     * them.  What each column can and cannot mean: `posted`/`started` moving says the environment
     * left through the ring, `refused` says a producer was turned away, and none of them says a
     * pixel appeared anywhere.  That is the blocks on the monitor, which is the reason this verb
     * exists at all. */
    ps2_gif_stats(&st);
    ps2_kprintf("[GSQUAD] env on the bus: posted=%u started=%u queued=%u inflight=%u refused=%u sync=%u\n",
                (unsigned int)st.posted, (unsigned int)st.started,
                (unsigned int)st.queued, (unsigned int)st.in_flight,
                (unsigned int)st.refused, (unsigned int)st.sync_sends);
    ps2_kprintf("[GSQUAD] sending now. both=GS draws | RED only=the ring | BLUE only=sync | none=neither\n");

    /* Rung 1's first half: the SPRITE group over the sender every console upload has already used,
     * so the only new thing on the bus is a four-register REGLIST group carried as one loop. */
    ps2_gs_gif_send(s_pkt, (uint32_t)qw_red);

    /* ...and the second half over the path the shipped stream rides.  The one buffer is rebuilt
     * here only because the send above has already completed, and the post below is drained and
     * reaped before returning, so no transfer is ever reading bytes a later call rewrites. */
    r.y0 = GSQUAD_BLUE_Y0;
    r.y1 = GSQUAD_BLUE_Y1;
    qw_blue = gs_poster_packet(&r, 1u, GSQUAD_BLUE, s_pkt, gs_poster_qwc(1u));
    if (qw_blue < 0) {
        ps2_kprintf("[GSQUAD] the emitter refused the BLUE rect: only RED was sent\n");
        return;
    }
    if (ps2_gif_post(s_pkt, (uint32_t)qw_blue) != 0) {
        /* Printed on the ring's own failure path only: at this point the RED block is already on its
         * way and the page is about to be repainted, so the answer this run can still give is
         * "RED, and BLUE was refused", which is a ring-capacity reading rather than a raster one. */
        ps2_kprintf("[GSQUAD] the ring refused the BLUE packet (ring full)\n");
        return;
    }
    (void)ps2_gif_drain();
    (void)ps2_gif_reap();
}

/* ── LANE 19 rung 2: `gsread`, the read-back that answers rung 1 without a witness ─────
 *
 * rung 1 asks a human one question -- "is there red, and is there blue?" -- and this verb takes the
 * answer itself.  It is the reason the rung exists: PCSX2 2.8.2 has no CLI flag that dumps a frame,
 * so without a Local->Host transfer the only instrument for what the GS rasterised is somebody's
 * monitor.  With one, the guest can census the page PCRTC is showing and print that census into the
 * log a headless run already writes.
 *
 * The draw is gsquad's, unchanged: the same two SPRITE rects, the same two senders (the synchronous
 * ps2_gs_gif_send() for RED, the LANE 10 ring for BLUE), the same environment -- ps2_stage1_gs_env()
 * is one function precisely so a census cannot be taken against a page the draw did not name.  What
 * is new is the read: three 64x16 strips of CT32 out of page 0, all at one x, at y=208 (inside RED),
 * at y=344 (inside BLUE) and at y=512 (below both, where the desktop's own navy should still be).
 * The third strip is the census's control, and its expectation is read out of the canvas rather than
 * written as a literal: the question is "does VRAM match what the compositor painted there", and a
 * hand-typed word would be a second model of a byte order this port has already been wrong about
 * once.
 *
 * Four things this verb must not do, and every one of them is a failure mode of the read rather than
 * a style preference:
 *
 *   Nothing may be UPLOADED between a draw and its read.  ps2_kprintf() ends in a full-width band
 *   repaint (see rung 1's ordering law above), so the prints that sit between these reads are only
 *   safe because each of them lands in text rows 3..8 -- y <= 143 -- while the lowest strip still
 *   unread is at y=208.  Add a row, widen the console, or move a strip up and the ordering breaks
 *   silently: the census would read back its own prompt and call the page navy.
 *
 *   The buffer is filled with GSREAD_SENT before every read.  A download the hardware rounded down
 *   would otherwise hand back the PREVIOUS strip's pixels and report a match -- which is exactly how a
 *   quarter-length transfer disguises itself as a working instrument.  `sent` is the count left
 *   untouched, and it is the only column that tells "the GS has this colour here" apart from "nothing
 *   was written here".
 *
 *   A failed read prints a stage name, never a colour.  ps2_gs_vram_read() can refuse or time out at
 *   seven different points and each means something different -- bus-busy is this port's own
 *   pipelining, builder-refused is a rectangle these registers cannot describe (with the law named out
 *   of gs_read_last_err()), block-never-finished is channel 1, no-finish-receipt is the request never
 *   reaching the GS, vif-fifo-never-drained is VIF1.  "no census" and "the census is black" are the two
 *   answers most worth telling apart, and a stage name is the only thing that separates them.
 *
 *   The verdict reads the control before it reads the sprites.  Both plus a navy control means the
 *   rasteriser works and the emptiness is in the stream's content; neither plus a navy control means
 *   a drawing group reaches the pipe but not the page; and a control that is NOT navy means this
 *   census is not evidence about anything, which the row says out loud rather than leaving a verdict
 *   to be inferred from it.
 *
 * What it cannot answer, and what rung 3 below is for: three 64x16 windows at one x say nothing about
 * the other 478,016 pixels of the page, so "the strip is navy" is equally a sprite that never rasterised
 * and a sprite that rasterised at the wrong pitch or offset.
 */
#define GSREAD_X       GSQUAD_X0    /* 32: a multiple of 4, which is CT32's quadword law */
#define GSREAD_W       64
#define GSREAD_H       16
#define GSREAD_RED_Y   208           /* inside RED's 144..319 */
#define GSREAD_BLUE_Y  344           /* inside BLUE's 336..495 */
#define GSREAD_CTRL_Y  512           /* below both: the desktop's own navy */
#define GSREAD_SENT    0x00FF00FFu   /* a word neither the sprites nor the navy background can be */

typedef struct {
    uint32_t match;        /* words that read back as the expected colour */
    uint32_t sent;         /* words the download never touched */
    uint32_t other;        /* words that are neither of those */
    uint32_t first_other;  /* the first of those, so a shift is recognisable as a value */
    uint32_t qw;           /* quadwords the read asked for, or 0 when it failed */
    gs_read_bus_err err;
} gsread_strip_t;

static const char *gsread_bus_name(gs_read_bus_err e)
{
    switch (e) {
    case GS_READ_BUS_OK:       return "ok";
    case GS_READ_BUS_E_ARGS:   return "bad-args";
    case GS_READ_BUS_E_BUSY:   return "bus-busy";
    case GS_READ_BUS_E_BUILD:  return "builder-refused";
    case GS_READ_BUS_E_STR:    return "block-never-finished";
    case GS_READ_BUS_E_FINISH: return "no-finish-receipt";
    case GS_READ_BUS_E_FIFO:   return "vif-fifo-never-drained";
    case GS_READ_BUS_E_PAGE:   return "page-not-decodable";
    default:                   return "data-never-finished";
    }
}

static const char *gsread_law_name(gs_read_err e)
{
    switch (e) {
    case GS_READ_OK:      return "none";
    case GS_READ_E_ARGS:  return "args";
    case GS_READ_E_PSM:   return "psm";
    case GS_READ_E_SIZE:  return "RRW/RRH";
    case GS_READ_E_QWC:   return "over-16-bit-count";
    case GS_READ_E_POS:   return "SSAX/SSAY";
    case GS_READ_E_ALIGN: return "quadword-alignment";
    case GS_READ_E_PITCH: return "SBW/row-end";
    default:              return "page-extent";
    }
}

/* What the page being scanned answers for a canvas word.
 *
 * A census compares whole 32-bit words, and on a PSMCT16 display page the word that comes back is
 * the expansion of five bits per channel, not the word the CPU wrote: the full-scale primaries are
 * exact both ways -- which is why RED, BLUE, CYAN, MAGENTA, GREEN and YELLOW have always matched --
 * but the console's navy is not (its green field is 0x1B, and 27 of 8 bits becomes 24 of 5 then
 * 24 again), and neither are ORANGE or VIOLET.  Asking a half-width page for the canvas's own words
 * would count a correctly-drawn screen as `other=` and report a broken screen where the expectation
 * is what broke, so every expectation goes through here.  On a 32-bit page -- the default, and the
 * shipped path -- this is the identity.
 *
 * The phase is x=0,y=0 because the dither matrix's cell there is 4, the value the no-dither branch
 * uses, so the answer is the plain truncation and the centre of the spread rather than one of its
 * edges.  A dithered page does NOT hold one word across a flat field: ±4 around the centre tips
 * some of the sixteen phase cells into the neighbouring 5-bit value, so on a 16-bit page with the
 * dither on, `navy=` counts the pixels that land on the centre phase and the rest show up as
 * `other=` of a colour one step away.  That is what an error-diffused 16-bit background is, and
 * `page dither 0` is the dial that removes it when the number has to mean pixels. */
static uint32_t gsread_expect(uint32_t canvas_word)
{
    const gs_disp_page_t *pg = gs_disp_shown();

    if (pg == NULL || pg->bytes_pp != 2u) return canvas_word;
    return gs_unpack_5551(gs_pack_5551(canvas_word, 0u, 0u, ps2_gs_dither()));
}

/* One strip out of page 0 and one row about it.  The fields are stored into *out one at a time rather
 * than by assigning a finished struct: a struct assignment in a hard-float TU of this port has put
 * sdc1/ldc1 -- illegal on the R5900 -- into the image before, and `make test-ps2` is what catches it.
 * Nothing prints between the fill and the read, and a failed read reports a stage, not a colour. */
static void ps2_gsread_strip(const char *label, int y, uint32_t want,
                             uint32_t *buf, uint32_t n, gsread_strip_t *out)
{
    uint32_t i;
    uint32_t t0;
    int32_t got;

    /* Asked in canvas words, compared in the page's own: see gsread_expect().  The row below prints
     * this transformed value, so a `want=` on a 16-bit page is the number the read can actually
     * match rather than the one the canvas holds. */
    want = gsread_expect(want);

    for (i = 0; i < n; i++) buf[i] = GSREAD_SENT;

    t0 = btron_render_perf_us();
    got = ps2_gs_vram_read(GSREAD_X, y, GSREAD_W, GSREAD_H, buf);
    out->err = ps2_gs_vram_read_err();
    out->qw = (got > 0) ? (uint32_t)got : 0u;

    if (out->err != GS_READ_BUS_OK) {
        out->match = 0u;
        out->sent = n;
        out->other = 0u;
        out->first_other = 0u;
        ps2_kprintf("[GSREAD] %s y=%d FAILED: %s", label, y, gsread_bus_name(out->err));
        if (out->err == GS_READ_BUS_E_BUILD) {
            ps2_kprintf(" (law: %s)", gsread_law_name(gs_read_last_err()));
        }
        ps2_kprintf("\n");
        return;
    }

    out->match = 0u;
    out->sent = 0u;
    out->other = 0u;
    out->first_other = 0u;
    for (i = 0; i < n; i++) {
        if (buf[i] == want) {
            out->match++;
        } else if (buf[i] == GSREAD_SENT) {
            out->sent++;
        } else {
            if (out->other == 0u) out->first_other = buf[i];
            out->other++;
        }
    }
    ps2_kprintf("[GSREAD] %s y=%-4d want=0x%08x match=%u/%u sent=%u other=%u first=0x%08x %u us %u qw\n",
                label, y, (unsigned int)want, (unsigned int)out->match, (unsigned int)n,
                (unsigned int)out->sent, (unsigned int)out->other,
                (unsigned int)out->first_other,
                (unsigned int)(btron_render_perf_us() - t0), (unsigned int)out->qw);
}

/* ── LANE 19 rung 3: the whole displayed page, in bands ────────────────────────────────
 *
 * What the three strips can say is "this 64x16 window at x=32 holds the canvas colour".  What they
 * cannot say is the thing that decides the next move: whether the SPRITE wrote no pixel anywhere,
 * or wrote them somewhere the strips do not cover -- a wrong FRAME pitch, a window origin that is
 * not (0,0), a rect that left the scissor.  "Nothing landed" and "it landed elsewhere" are
 * different bugs with different fixes, and a strip census reads identically for both.
 *
 * It cannot be one transfer, and the reason is a register width: CH_QWC is 16 bits and an 800x600
 * CT32 page is 120,000 quadwords, so the count the DMA would have been handed is 120,000 modulo
 * 65,536.  That is why gs_read_request_packet() now refuses GS_READ_E_QWC, and why this walks the
 * canvas in 150-row bands of 30,000 -- the same discipline ps2sdk's screenshot() keeps by taking one
 * row per transfer.  Each band is filled with the sentinel before its read, for the strips' own
 * reason: a short download would otherwise hand back the PREVIOUS band's real pixels and print a
 * census of a page it never finished reading.
 *
 * The classes are the ones the page can hold: the canvas colour the clear uploaded, the two magic
 * colours only gsquad's two rects carry, rung 4's four reference-shape colours, the two-rect batch's
 * ORANGE, the retired shape's VIOLET, and the sentinel.
 * Pixels that are none of those split by row, because rows 0..143 are the verb's own printed text and
 * its ink (0xFFE0E0E0, ps2_kprintf's own pair at core_ps2.c:695) is legitimately not navy:
 * text_other is the census's positive control -- it shows the read can see something the console
 * wrote -- and below_other is the bucket a misplaced sprite would land in.  Each hunt keeps its own
 * bounding box, so a moved draw arrives as numbers rather than as a hypothesis.
 *
 * The ref colours are counted with the same care as red and blue and for a different reason.  rung 3's
 * negative retired "it landed somewhere else"; rung 4 asks "does ANY packet land", so each control's
 * own count is a bit of the answer, and a control that landed in an unexpected row would otherwise be
 * reported as a control that did not land.  Their expected counts are therefore printed beside their
 * boxes: a sprite's count is exact arithmetic, and a triangle's is the mathematical area, which the
 * GS's own edge rules can put a few pixels off -- hence the `~`.  The first run confirmed that
 * reading: green came back 3525 of ~3600 with its box starting at x=434 rather than 432, and both
 * un-nudged and nudged sprites filled their boxes' every pixel.
 */
#define GSREAD_BAND_ROWS 150u   /* four bands over the 600 rows, and 480,000 bytes of buffer */
#define GSREAD_BAND_QWC  (((uint32_t)PS2_SCREEN_WIDTH * GSREAD_BAND_ROWS * 32u) >> 7)
#define GSREAD_RED_PX    ((uint32_t)(GSQUAD_X1 - GSQUAD_X0) * (uint32_t)(GSQUAD_RED_Y1 - GSQUAD_RED_Y0))
#define GSREAD_BLUE_PX   ((uint32_t)(GSQUAD_X1 - GSQUAD_X0) * (uint32_t)(GSQUAD_BLUE_Y1 - GSQUAD_BLUE_Y0))
#define GSREAD_TEXT_ROWS ((uint32_t)GSQUAD_RED_Y0)   /* at or below this row, not-navy is glyphs */
#define GSREAD_CYAN_PX   ((uint32_t)(GSREF_CYA_X1 - GSREF_CYA_X0) * (uint32_t)(GSREF_Y1 - GSREF_Y0))
#define GSREAD_MAGENTA_PX ((uint32_t)(GSREF_MAG_X1 - GSREF_MAG_X0) * (uint32_t)(GSREF_Y1 - GSREF_Y0))
#define GSREAD_YELLOW_PX ((uint32_t)(GSREF_YEL_X1 - GSREF_YEL_X0) * (uint32_t)(GSREF_Y1 - GSREF_Y0))
#define GSREAD_ORANGE_PX ((uint32_t)(GSREF_ORA_X1 - GSREF_ORA_X0) * (uint32_t)(GSREF_Y1 - GSREF_Y0))
#define GSREAD_VIOLET_PX ((uint32_t)(GSREF_VIO_X1 - GSREF_VIO_X0) * (uint32_t)(GSREF_Y1 - GSREF_Y0))
/* One rect of the batch, derived from where the batch's SECOND rect starts and not from
 * GSREAD_ORANGE_PX / 2: the reading this names is "the group's first loop rasterised", so the
 * expectation has to be the first rect's own area, and a split that moved would otherwise move the
 * expectation with it.  The two rects are equal by construction here, so the two spellings agree. */
#define GSREAD_ORANGE_ONE ((uint32_t)(GSREF_ORA_SPLIT - GSREF_ORA_X0) * (uint32_t)(GSREF_Y1 - GSREF_Y0))
/* base x height / 2, i.e. the area the three vertices enclose -- an expectation to read a count
 * against, not a bit-exact promise about a rasteriser's half-pixel edges. */
#define GSREAD_GREEN_PX  ((uint32_t)(GSREF_TRI_X2 - GSREF_TRI_X0) * \
                          (uint32_t)(GSREF_Y1 - GSREF_Y0 - 1) / 2u)

typedef struct {
    uint32_t navy, sent, text_other, below_other;
    uint32_t red, blue, first_below;
    uint32_t cyan, magenta, green, yellow;   /* rung 4: REGLIST / + nudge / TRIANGLE / PACKED+AD */
    uint32_t orange;      /* the shipping emitter at NLOOP 2: the batch's two rects */
    uint32_t violet;      /* the retired PACKED/NREG 4 shape, which must produce no pixel */
    uint32_t qw, us;
    int red_box[4];          /* x0 y0 x1 y1, all -1 when the count is 0 */
    int blue_box[4];
    int cyan_box[4];
    int magenta_box[4];
    int green_box[4];
    int yellow_box[4];
    int orange_box[4];
    int violet_box[4];
    int below_box[4];
    gs_read_bus_err err;
} gsread_page_t;

static void gsread_box(int *box, uint32_t x, uint32_t y)
{
    if (box[0] < 0 || (int)x < box[0]) box[0] = (int)x;
    if (box[1] < 0 || (int)y < box[1]) box[1] = (int)y;
    if ((int)x > box[2]) box[2] = (int)x;
    if ((int)y > box[3]) box[3] = (int)y;
}

/* Every field stored one at a time, as the strip helper's note says: this TU is compiled -mhard-float
 * and a struct assignment here has put sdc1/ldc1 -- illegal on the R5900 -- into the image before. */
static void gsread_page_census(uint32_t *buf, uint32_t navy, gsread_page_t *o)
{
    const uint32_t width = (uint32_t)PS2_SCREEN_WIDTH;
    const uint32_t height = (uint32_t)PS2_SCREEN_HEIGHT;
    uint32_t y_start, i, total = 0u;
    uint32_t t0;
    /* The nine references, asked in the page's own words.  gsread_expect() is the identity on a
     * 32-bit page -- the default, and every run that has ever printed these rows -- and on a 16-bit
     * one it is the decode the read performs, so `navy=`/`orange=` count what the census can match
     * instead of what the canvas holds. */
    uint32_t exp_red, exp_blue, exp_cyan, exp_magenta, exp_green, exp_yellow;
    uint32_t exp_orange, exp_violet;

    navy = gsread_expect(navy);           /* the caller handed us the canvas's word */
    exp_red = gsread_expect(GSQUAD_RED);
    exp_blue = gsread_expect(GSQUAD_BLUE);
    exp_cyan = gsread_expect(GSQUAD_CYAN);
    exp_magenta = gsread_expect(GSQUAD_MAGENTA);
    exp_green = gsread_expect(GSQUAD_GREEN);
    exp_yellow = gsread_expect(GSQUAD_YELLOW);
    exp_orange = gsread_expect(GSQUAD_ORANGE);
    exp_violet = gsread_expect(GSQUAD_VIOLET);

    o->navy = 0u; o->sent = 0u; o->text_other = 0u; o->below_other = 0u;
    o->red = 0u;  o->blue = 0u;  o->first_below = 0u;
    o->cyan = 0u; o->magenta = 0u; o->green = 0u; o->yellow = 0u; o->orange = 0u;
    o->violet = 0u;
    o->qw = 0u;   o->us = 0u;    o->err = GS_READ_BUS_OK;
    for (i = 0u; i < 4u; i++) {
        o->red_box[i] = -1; o->blue_box[i] = -1; o->below_box[i] = -1;
        o->cyan_box[i] = -1; o->magenta_box[i] = -1;
        o->green_box[i] = -1; o->yellow_box[i] = -1; o->orange_box[i] = -1;
        o->violet_box[i] = -1;
    }

    t0 = btron_render_perf_us();
    for (y_start = 0u; y_start < height; y_start += GSREAD_BAND_ROWS) {
        const uint32_t rows = (height - y_start < GSREAD_BAND_ROWS)
                                ? (height - y_start) : GSREAD_BAND_ROWS;
        int32_t got;

        for (i = 0u; i < width * rows; i++) buf[i] = GSREAD_SENT;
        got = ps2_gs_vram_read(0, (int)y_start, (int)width, (int)rows, buf);
        o->err = ps2_gs_vram_read_err();
        if (o->err != GS_READ_BUS_OK) {
            ps2_kprintf("[GSREAD] page y=%u FAILED: %s", (unsigned int)y_start,
                        gsread_bus_name(o->err));
            if (o->err == GS_READ_BUS_E_BUILD)
                ps2_kprintf(" (law: %s)", gsread_law_name(gs_read_last_err()));
            ps2_kprintf("\n");
            return;
        }
        o->qw += (uint32_t)got;
        total += width * rows;

        for (i = 0u; i < width * rows; i++) {
            const uint32_t v = buf[i];
            uint32_t x, y;

            if (v == navy)           { o->navy++; continue; }
            if (v == GSREAD_SENT)    { o->sent++; continue; }
            x = i % width;
            y = y_start + i / width;
            if (v == exp_red)   { o->red++;     gsread_box(o->red_box, x, y);   continue; }
            if (v == exp_blue)  { o->blue++;    gsread_box(o->blue_box, x, y);  continue; }
            if (v == exp_cyan)    { o->cyan++;    gsread_box(o->cyan_box, x, y);    continue; }
            if (v == exp_magenta) { o->magenta++; gsread_box(o->magenta_box, x, y); continue; }
            if (v == exp_green)   { o->green++;   gsread_box(o->green_box, x, y);   continue; }
            if (v == exp_yellow)  { o->yellow++;  gsread_box(o->yellow_box, x, y);  continue; }
            if (v == exp_orange)  { o->orange++;  gsread_box(o->orange_box, x, y);  continue; }
            if (v == exp_violet)  { o->violet++;  gsread_box(o->violet_box, x, y);  continue; }
            if (y < GSREAD_TEXT_ROWS) { o->text_other++; continue; }
            if (o->below_other == 0u) o->first_below = v;
            o->below_other++;
            gsread_box(o->below_box, x, y);
        }
    }
    o->us = btron_render_perf_us() - t0;

    ps2_kprintf("[GSREAD] page %ux%u: navy=%u/%u text_other=%u below_other=%u first=0x%08x "
                "sent=%u %u us %u qw\n",
                (unsigned int)width, (unsigned int)height,
                (unsigned int)o->navy, (unsigned int)total,
                (unsigned int)o->text_other, (unsigned int)o->below_other,
                (unsigned int)o->first_below, (unsigned int)o->sent,
                (unsigned int)o->us, (unsigned int)o->qw);
    ps2_kprintf("[GSREAD] page hunts: red=%u/%u bbox=%d,%d..%d,%d blue=%u/%u bbox=%d,%d..%d,%d "
                "other_bbox=%d,%d..%d,%d\n",
                (unsigned int)o->red, (unsigned int)GSREAD_RED_PX,
                o->red_box[0], o->red_box[1], o->red_box[2], o->red_box[3],
                (unsigned int)o->blue, (unsigned int)GSREAD_BLUE_PX,
                o->blue_box[0], o->blue_box[1], o->blue_box[2], o->blue_box[3],
                o->below_box[0], o->below_box[1], o->below_box[2], o->below_box[3]);
    /* Two rows rather than one because each control's box is part of its answer: a count that landed
     * in the wrong rows would otherwise read as the branch that says nothing landed.  They are two
     * rows and not one long one because the console wraps at 100 columns, and a wrapped row is a row
     * of ink at y 144 -- inside RED's own rows, in the one bucket the fork below reads. */
    ps2_kprintf("[GSREAD] page refs: cyan=%u/%u bbox=%d,%d..%d,%d magenta=%u/%u bbox=%d,%d..%d,%d\n",
                (unsigned int)o->cyan, (unsigned int)GSREAD_CYAN_PX,
                o->cyan_box[0], o->cyan_box[1], o->cyan_box[2], o->cyan_box[3],
                (unsigned int)o->magenta, (unsigned int)GSREAD_MAGENTA_PX,
                o->magenta_box[0], o->magenta_box[1], o->magenta_box[2], o->magenta_box[3]);
    ps2_kprintf("[GSREAD] page refs: green=%u/~%u bbox=%d,%d..%d,%d yellow=%u/%u bbox=%d,%d..%d,%d\n",
                (unsigned int)o->green, (unsigned int)GSREAD_GREEN_PX,
                o->green_box[0], o->green_box[1], o->green_box[2], o->green_box[3],
                (unsigned int)o->yellow, (unsigned int)GSREAD_YELLOW_PX,
                o->yellow_box[0], o->yellow_box[1], o->yellow_box[2], o->yellow_box[3]);
    /* ORANGE and VIOLET on rows of their own rather than bolted onto the yellow one, because they are
     * this run's two questions and not rung 4's precedents: ORANGE is the shipping emitter at NLOOP 2,
     * VIOLET is the emitter's retired shape and carries `want=0` in the row because its box area is a
     * ceiling and never an expectation -- a denominator there would read as "736 is the number to hit".
     * Nothing here is at risk of putting ink in a censused row -- this function prints only after every
     * band has been read, and the next run's text_clear() repaints the page -- but each row is still
     * kept inside the console's 100 columns, which is the budget the pre-draw rows are measured against
     * and the reason the boxes are several rows and not one. */
    ps2_kprintf("[GSREAD] page refs: orange=%u/%u bbox=%d,%d..%d,%d (the NLOOP 2 batch)\n",
                (unsigned int)o->orange, (unsigned int)GSREAD_ORANGE_PX,
                o->orange_box[0], o->orange_box[1], o->orange_box[2], o->orange_box[3]);
    ps2_kprintf("[GSREAD] page refs: violet=%u want=0 box=%u bbox=%d,%d..%d,%d (retired shape)\n",
                (unsigned int)o->violet, (unsigned int)GSREAD_VIOLET_PX,
                o->violet_box[0], o->violet_box[1], o->violet_box[2], o->violet_box[3]);

    /* The reading, in the order that keeps an incomplete census from being quoted as a negative
     * result: a page with sentinel left in it is a page this loop did not finish reading, whatever
     * the counts above look like.  After that guard the controls are read as the bits they are, and
     * the shape they used to decide between is decided -- rung 4's four shapes and PRE are findings,
     * this verb's remaining job is to re-measure them and answer the batch.
     *
     *   `retired` is VIOLET, gs_poster_ref_sprite_packed_packet() at PRE=1 -- the packet that shipped
     *     from LANE 9b until rung 5 measured it as drawing nothing at either value of PRE.  It is read
     *     FIRST because it is the only row that tests the read rather than the GS: its expectation is
     *     zero by measurement and not by arithmetic, so a violet pixel anywhere says this census is
     *     reporting something other than the shapes its labels name.
     *   `regl` is the SDK's dense four-register REGLIST list (cyan) and that plus its corner nudge
     *     (magenta); `ad` is the same four registers as four single-register AD writes; `tri` is the
     *     SDK's six-nibble triangle list.  All three landed on every run so far, so each is now a
     *     positive control: what its absence says is that this verb lost a send, not that the GS
     *     changed its mind.
     *   `ship` is RED and BLUE -- gs_poster_packet() itself, the emitter as it now stands, at NLOOP 1
     *     on each of the two senders.  Their silence was the defect and their landing is its repair,
     *     so a run in which they do not fill their rects exactly is a run to re-read the image of.
     *   `batch` is ORANGE, the same emitter with NLOOP 2 in one packet.  It is the open question: the
     *     recorder's groups are three, twelve and up to four thousand loops long, and nothing measured
     *     so far says a multi-loop dense group rasterises past its first loop.
     *
     * The order of the branches is the order of what they buy: the read's own truth, then the
     * controls that say the draw half works, then the emitter, then the batch -- because each branch
     * below the emitter is only quotable if the emitter itself landed. */
    if (o->sent != 0u) {
        ps2_kprintf("[GSREAD] page: INCOMPLETE -- %u words the download never touched, so the rows "
                    "above are not a census of the page\n", (unsigned int)o->sent);
    } else {
        const uint32_t regl = (uint32_t)((o->cyan != 0u) | ((o->magenta != 0u) << 1));
        const uint32_t ad = (uint32_t)(o->yellow != 0u);
        const uint32_t tri = (uint32_t)(o->green != 0u);
        const uint32_t retired = (uint32_t)(o->violet != 0u);
        const uint32_t ship = (uint32_t)((o->red != 0u) | ((o->blue != 0u) << 1));
        const int red_in_box = (o->red == GSREAD_RED_PX && o->red_box[0] == GSQUAD_X0
                                && o->red_box[1] == GSQUAD_RED_Y0
                                && o->red_box[2] == GSQUAD_X1 - 1 && o->red_box[3] == GSQUAD_RED_Y1 - 1);
        const int blue_in_box = (o->blue == GSREAD_BLUE_PX && o->blue_box[0] == GSQUAD_X0
                                 && o->blue_box[1] == GSQUAD_BLUE_Y0
                                 && o->blue_box[2] == GSQUAD_X1 - 1
                                 && o->blue_box[3] == GSQUAD_BLUE_Y1 - 1);

        if (retired != 0u) {
            ps2_kprintf("[GSREAD] page: THE READ IS NOT TELLING THE TRUTH -- %u violet pixels, in a "
                        "box whose shape rung 5 measured as drawing nothing on this GS at either value "
                        "of PRE.  Nothing on this run is evidence about a packet until the violet "
                        "count is 0: read the navy/sent/other rows, then the refused row for a shape "
                        "this verb never sent\n", (unsigned int)o->violet);
        } else if (ship == 0u) {
            ps2_kprintf("[GSREAD] page: THE FIXED EMITTER DRAWS NOTHING -- gs_poster_packet() at NLOOP "
                        "1 leaves no red and no blue anywhere, where CYAN's identical dense shape fills "
                        "its box.  Either this image does not carry the fix -- make test-ps2-gs-poster "
                        "asserts the shipped tag against the retired one -- or the defect is below the "
                        "packet: the environment, the sender, or the GIF->GS path\n");
        } else if (ship != 3u) {
            ps2_kprintf("[GSREAD] page: ONE SENDER DELIVERED AND THE OTHER DID NOT (red=%u blue=%u) -- "
                        "the shape is fine on whichever path landed, so the loss is in the sender the "
                        "recorded eegs stream rides, not in the format\n",
                        (unsigned int)o->red, (unsigned int)o->blue);
        } else if (!red_in_box || !blue_in_box) {
            ps2_kprintf("[GSREAD] page: THE DRAW LANDS WHERE ITS RECT DOES NOT SAY -- both magic "
                        "colours are in page 0, but not at the count and corners their own rects name "
                        "(see the bbox columns).  That is the pitch, the window origin or the scissor, "
                        "and no verdict below this one can be quoted until it is read\n");
        } else if ((regl & 1u) == 0u || ad == 0u || tri == 0u) {
            /* CYAN alone is enough to keep the positive controls alive: MAGENTA differs from it by the
             * nudge and has its own branch at the bottom, so a run that lost only MAGENTA is a run with
             * a nudge finding, not a run with a broken verb. */
            ps2_kprintf("[GSREAD] page: A POSITIVE CONTROL IS EMPTY -- cyan=%u magenta=%u green=%u "
                        "yellow=%u, and every one of those landed on the run that settled the format.  "
                        "This verb's own sends are suspect: read the refused row before quoting any "
                        "count on this page as the GS's answer\n",
                        (unsigned int)o->cyan, (unsigned int)o->magenta,
                        (unsigned int)o->green, (unsigned int)o->yellow);
        } else if (o->orange == GSREAD_ORANGE_PX) {
            ps2_kprintf("[GSREAD] page: BOTH LOOPS OF THE BATCH DREW -- %u/%u orange px, one tag and "
                        "two dense SPRITE loops in the same group, so a multi-loop dense REGLIST group "
                        "rasterises on this GS.  The recorder may ship one group per primitive run, as "
                        "it does today, and the eegs stream's emptiness is NOT a group-length effect\n",
                        (unsigned int)o->orange, (unsigned int)GSREAD_ORANGE_PX);
        } else if (o->orange == GSREAD_ORANGE_ONE) {
            ps2_kprintf("[GSREAD] page: ONLY THE GROUP'S FIRST LOOP RASTERISED -- %u orange px of %u, "
                        "i.e. one of the two rects inside one NLOOP 2 tag.  The format is right and the "
                        "BATCH is not: a loop after the first is dropped somewhere between the tag and "
                        "the raster, so ps2_gs_rec.c must close its group at every vertex and the "
                        "recorded stream's triangles are arriving complete and drawing one apiece\n",
                        (unsigned int)o->orange, (unsigned int)GSREAD_ORANGE_PX);
        } else if (o->orange == 0u) {
            ps2_kprintf("[GSREAD] page: THE BATCH DRAWS NOTHING -- no orange px in a box the same "
                        "emitter fills at NLOOP 1 on both senders.  A two-loop group is inert where a "
                        "one-loop group lands, which is a third finding and not the second one's: read "
                        "the bbox columns and the refused row, then the GS's own receipts, before "
                        "quoting it as why the eegs XMB is empty inside\n");
        } else {
            ps2_kprintf("[GSREAD] page: A PARTIAL BATCH -- %u orange px, which is neither the pair of "
                        "rects (%u) nor one of them (%u) nor none.  Its bbox says which: a count "
                        "between the two is a rect clipped, not a loop dropped\n",
                        (unsigned int)o->orange, (unsigned int)GSREAD_ORANGE_PX,
                        (unsigned int)GSREAD_ORANGE_ONE);
        }

        /* The nudge, read separately because it is orthogonal to every branch above: cyan and
         * magenta differ only by the SDK's START_OFFSET/END_OFFSET, so they can disagree while the
         * group-shape verdict stands.  It is a smaller finding than the shape and a real one: it says
         * a SPRITE whose edges sit exactly on whole pixels can be the shape that draws nothing. */
        if (regl == 1u || regl == 2u) {
            ps2_kprintf("[GSREAD] page: NUDGE -- %s sprite drew and the other did not, so the SDK's "
                        "-7/+9 corner offsets are what makes a SPRITE rasterise here, independent "
                        "of the shape answer above\n",
                        regl == 1u ? "the un-nudged (cyan)" : "the nudged (magenta)");
        }
        /* And the triangle, for the same reason: it shares the sprite's tag and format but not its
         * two-corner loop, so a lone disagreement points at the vertex pipeline rather than at
         * anything the poster emits. */
        if (tri != 0u && regl == 0u) {
            ps2_kprintf("[GSREAD] page: TRIANGLE-DISAGREES -- REGLIST with NREG 6 draws and REGLIST "
                        "with NREG 4 does not, so the fault is in the two-quadword sprite group "
                        "itself, not in the format the recorder would have to adopt\n");
        }
        /* Orthogonal to every branch above and worth its own row even now: ink below the text rows in
         * a class this verb does not own.  The branches read the colours they name; this one reads the
         * page that is not navy, which is what a texture write, a second canvas or another task's
         * upload would look like. */
        if (o->below_other != 0u) {
            ps2_kprintf("[GSREAD] page: FOREIGN INK -- %u pixels below the text rows are none of the "
                        "nine classes this verb draws or clears (first=0x%08x at %d,%d..%d,%d).  "
                        "Whatever verdict is printed above was reached on the named colours only\n",
                        (unsigned int)o->below_other, (unsigned int)o->first_below,
                        o->below_box[0], o->below_box[1], o->below_box[2], o->below_box[3]);
        }
    }
}

static void ps2_gsread_probe(void)
{
    static uint64_t s_pkt[2u * GS_POSTER_QWC(2u)] __attribute__((aligned(16)));
    /* rung 4's controls and rung 5's retired shape share one buffer, and may: each is sent before the
     * next is built, and ps2_gs_gif_send() spins until its own transfer is done.  Sized by the longest
     * of the shapes -- the AD packet and the retired PACKED control are both five quadwords, the dense
     * sprite three and the triangle four -- so no control can be handed a buffer shorter than its
     * packet.  s_pkt is sized by the batch, which is the longest thing this verb builds. */
    static uint64_t s_ref[2u * GS_REF_AD_QWC] __attribute__((aligned(16)));
    static uint32_t s_buf[GSREAD_W * GSREAD_H] __attribute__((aligned(16)));
    const uint32_t n = (uint32_t)GSREAD_W * (uint32_t)GSREAD_H;
    gs_poster_rect r;
    gs_poster_rect ora[2];
    gs_draw_env env;
    gsread_strip_t red, blue, ctrl;
    gsread_page_t page;
    uint32_t *page_buf = 0;
    uint32_t navy;
    uint32_t ref_refused = 0u;   /* bit 1 cyan, 2 magenta, 4 GREEN, 8 yellow, 16 ORANGE, 32 VIOLET */
    uint32_t ora_dropped = 0u;   /* rects gs_poster_packet() refused out of the NLOOP 2 batch */
    int32_t tri[6];
    int32_t qw;

    /* Rung 1's ordering law, inherited whole: this clear is the last full-canvas upload the verb
     * allows itself, and it comes before the draws. */
    ps2_gs_text_clear();

    /* The control's expectation: the canvas's own word at the strip's centre, taken after the clear
     * and before anything is drawn into the canvas -- i.e. the exact word the flush below uploads. */
    navy = ps2_gs_get_framebuffer()[(uint32_t)(GSREAD_CTRL_Y + GSREAD_H / 2)
                                    * (uint32_t)PS2_SCREEN_WIDTH
                                    + (uint32_t)(GSREAD_X + GSREAD_W / 2)];

    r.x0 = GSREAD_X;   r.y0 = GSQUAD_RED_Y0;
    r.x1 = GSQUAD_X1;  r.y1 = GSQUAD_RED_Y1;
    ps2_stage1_gs_env(&env);

    /* Two lines and one line, deliberately: the console is 100 columns x 16-px rows, the page's text
     * is censused as text only above GSREAD_TEXT_ROWS (row 9 = y 144), and the pre-draw prompt, the
     * three strip rows and these three together fill eight of those nine rows.  A third intro line
     * would put ink at y 144..159 -- inside the RED rect's own rows, in the one bucket the fork
     * reads -- so the post-fix wording is compressed rather than grown.  What it says is also shorter
     * now: the shape question that took three rungs to split is a finding, and this run asks one
     * question of the emitter it replaced. */
    ps2_kprintf("[GSREAD] draws the shipping emitter 2x NLOOP 1 (RED sync, BLUE ring) + once at\n");
    ps2_kprintf("[GSREAD] NLOOP 2 (ORANGE), 4 SDK refs, VIOLET=retired shape (want 0), 3 strips.\n");

    qw = gs_poster_packet(&r, 1u, GSQUAD_RED, s_pkt, gs_poster_qwc(1u));
    if (qw < 0) {
        ps2_kprintf("[GSREAD] the emitter refused the RED rect: nothing was drawn, nothing is read\n");
        return;
    }
    if (ps2_gs_draw_env_apply(&env) != 0) {
        ps2_kprintf("[GSREAD] the environment was refused: no draw page, so nothing is read\n");
        return;
    }

    /* rung 3's buffer, asked for HERE rather than at the census: Imalloc() prints a [HEAP] row for a
     * block over its own 96 kB threshold, and a print between a draw and its read is the failure mode
     * this verb exists to avoid.  It is freed at every exit below for the same reason in reverse --
     * the free prints too, and only once the reads are done does that stop mattering. */
    page_buf = (uint32_t *)Imalloc((size_t)GSREAD_BAND_QWC * 16u);

    /* The last row before the reads.  After this it is draws, then reads, then the census rows -- and
     * the reason to be strict is this verb's own failure mode: an upload that follows a draw erases
     * it, so a census printed too early would read back its own prompt and report the page as navy. */
    ps2_kprintf("[GSREAD] sends RED sync, BLUE ring, CYAN/MAG/GREEN/YEL/VIO/ORANGE sync; no print\n");

    ps2_gs_gif_send(s_pkt, (uint32_t)qw);

    r.y0 = GSQUAD_BLUE_Y0;
    r.y1 = GSQUAD_BLUE_Y1;
    qw = gs_poster_packet(&r, 1u, GSQUAD_BLUE, s_pkt, gs_poster_qwc(1u));
    if (qw < 0) {
        ps2_kprintf("[GSREAD] the emitter refused the BLUE rect: reading RED and the control only\n");
    } else if (ps2_gif_post(s_pkt, (uint32_t)qw) != 0) {
        ps2_kprintf("[GSREAD] the ring refused the BLUE packet (ring full)\n");
    } else {
        (void)ps2_gif_drain();
        (void)ps2_gif_reap();
    }

    /* ── the four SDK-shaped controls, the batch and the retired shape, one variable each ────────
     *
     * CYAN is the SDK's sprite tag and dense stride with this port's own corner words; MAGENTA is
     * CYAN plus the SDK's -7/+9 sprite nudge; GREEN is the SDK's three-vertex list at the plain page
     * offset; YELLOW is the same four registers delivered as four AD writes, the shape this port's own
     * console uploads use.  All four drew on every run that measured them, which is why they are still
     * sent: a census is only as trustworthy as the rows that agree with what is already known.  They go
     * out the SYNCHRONOUS sender because the sender was rung 1's variable and rung 3 answered it --
     * neither sender delivered the shape of the day -- so re-varying it here would confound the
     * variable that is open now.
     *
     * ORANGE is that variable.  It is gs_poster_packet(), the emitter as it ships, with TWO rects in
     * one group instead of one: one tag, NLOOP 2, four dense quadwords.  RED and BLUE are the same call
     * at NLOOP 1, and they are expected to land now, so ORANGE's count is a reading about loop count
     * and nothing else -- and the recorder's groups are three, twelve and up to four thousand loops
     * long, which is the thing this run cannot infer from any of the others.
     *
     * VIOLET is the packet that used to ship, kept as the instrument's negative control: PRE=1 over
     * four sparse quadwords, which rung 5 measured as drawing nothing at either value of PRE.  Its box
     * is the 16-pixel gutter CYAN and MAGENTA leave between them, and any pixel that comes back from it
     * is a finding about this read rather than about the GS.
     *
     * The geometry is a compile-time constant, so a refusal here is this verb's own bug and not the
     * GS's; it is counted in a mask and said AFTER the reads, because a print inside this window
     * repaints its own row and would erase the draw above it -- the failure mode rung 1's ordering
     * law exists to keep out. */
    r.x0 = GSREF_CYA_X0; r.y0 = GSREF_Y0;
    r.x1 = GSREF_CYA_X1; r.y1 = GSREF_Y1;
    qw = gs_poster_ref_sprite_packet(&r, GSQUAD_CYAN, 0u, s_ref, GS_REF_SPRITE_QWC);
    if (qw < 0) ref_refused |= 1u;
    else ps2_gs_gif_send(s_ref, (uint32_t)qw);

    r.x0 = GSREF_MAG_X0; r.x1 = GSREF_MAG_X1;
    qw = gs_poster_ref_sprite_packet(&r, GSQUAD_MAGENTA, 1u, s_ref, GS_REF_SPRITE_QWC);
    if (qw < 0) ref_refused |= 2u;
    else ps2_gs_gif_send(s_ref, (uint32_t)qw);

    /* Six separate stores, not an initializer list: this TU is -mhard-float and a bulk copy of a
     * local aggregate is the shape that has put sdc1/ldc1 -- illegal on the R5900 -- into the image.
     * The same law covers the field-by-field fills below, including the two-rect batch's.  The base
     * corners are GSREF_Y1-1 and the apex GSREF_Y0, so the enclosed area the census reads against
     * GSREAD_GREEN_PX is 160 x 45 / 2. */
    tri[0] = GSREF_TRI_X0;  tri[1] = GSREF_Y1 - 1;
    tri[2] = GSREF_TRI_X2;  tri[3] = GSREF_Y1 - 1;
    tri[4] = GSREF_TRI_X1;  tri[5] = GSREF_Y0;
    qw = gs_poster_ref_tri_packet(tri, GSQUAD_GREEN, s_ref, GS_REF_TRI_QWC);
    if (qw < 0) ref_refused |= 4u;
    else ps2_gs_gif_send(s_ref, (uint32_t)qw);

    /* YELLOW's rect is the right-hand strip of the same row band, and its four VALUES are CYAN's to
     * the bit -- the corners come from the same helper and the colour from the same canvas word -- so
     * a census that has CYAN and YELLOW disagreeing is a statement about the register list's shape
     * and nothing else.  Its expected count is CYAN's: 160 x 46, un-nudged, at 608..767 x 544..589. */
    r.x0 = GSREF_YEL_X0; r.x1 = GSREF_YEL_X1;
    qw = gs_poster_ref_sprite_ad_packet(&r, GSQUAD_YELLOW, 0u, s_ref, GS_REF_AD_QWC);
    if (qw < 0) ref_refused |= 8u;
    else ps2_gs_gif_send(s_ref, (uint32_t)qw);

    /* VIOLET, and the one draw on this page whose answer is known in advance.  The `pre` argument is
     * 1, i.e. the packet exactly as LANE 9b shipped it; with 0 this call would rebuild the packet rung
     * 5 also measured as inert, and one negative control per retired shape is enough for a census. */
    r.x0 = GSREF_VIO_X0; r.x1 = GSREF_VIO_X1;
    qw = gs_poster_ref_sprite_packed_packet(&r, GSQUAD_VIOLET, 1u, s_ref, GS_REF_PACKED_QWC);
    if (qw < 0) ref_refused |= 32u;
    else ps2_gs_gif_send(s_ref, (uint32_t)qw);

    /* ORANGE last among the draws, and through the shipping emitter rather than a reference builder --
     * which is the point: what this verb sends is what the poster and the recorder send.  A dropped
     * rect is read off gs_poster_dropped() the instant the packet is built, because that counter is
     * cleared by the next call and a batch the GS never saw would otherwise be censused as a batch the
     * GS refused. */
    ora[0].x0 = GSREF_ORA_X0; ora[0].y0 = GSREF_Y0;
    ora[0].x1 = GSREF_ORA_SPLIT; ora[0].y1 = GSREF_Y1;
    ora[1].x0 = GSREF_ORA_SPLIT; ora[1].y0 = GSREF_Y0;
    ora[1].x1 = GSREF_ORA_X1; ora[1].y1 = GSREF_Y1;
    qw = gs_poster_packet(ora, 2u, GSQUAD_ORANGE, s_pkt, gs_poster_qwc(2u));
    ora_dropped = gs_poster_dropped();
    if (qw < 0) ref_refused |= 16u;
    else ps2_gs_gif_send(s_pkt, (uint32_t)qw);

    ps2_gsread_strip("red",  GSREAD_RED_Y,  GSQUAD_RED,  s_buf, n, &red);
    ps2_gsread_strip("blue", GSREAD_BLUE_Y, GSQUAD_BLUE, s_buf, n, &blue);
    ps2_gsread_strip("ctrl", GSREAD_CTRL_Y, navy,        s_buf, n, &ctrl);

    if (ctrl.err != GS_READ_BUS_OK) {
        ps2_kprintf("[GSREAD] verdict: NO EVIDENCE -- the control read failed, so nothing here is known\n");
        if (page_buf) Ifree(page_buf);
        return;
    }
    if (ctrl.match != n) {
        /* Said out loud, because every verdict below would otherwise be inferred from a census this
         * row has already disqualifed. */
        ps2_kprintf("[GSREAD] verdict: UNTRUSTED -- control is not the canvas (match=%u/%u",
                    (unsigned int)ctrl.match, (unsigned int)n);
        if (ctrl.sent == n)      ps2_kprintf(", the download wrote NOTHING");
        else if (ctrl.first_other == 0u) ps2_kprintf(", it reads ZERO: the GS returned no data");
        else                     ps2_kprintf(", first other=0x%08x", (unsigned int)ctrl.first_other);
        ps2_kprintf(")\n");
        if (page_buf) Ifree(page_buf);
        return;
    }

    /* Said here rather than among the draws, because by now there is nothing left for a print to
     * erase.  A non-zero mask means this verb built a shape it could not send, so the census's zero
     * for that colour is this file's bug and not the GS's answer -- the one reading of a `refs` row
     * that would otherwise be mistaken for "that shape does not land either".  ORANGE's `dropped` rides
     * the same row because it does the same job the other way round: a non-zero count there means the
     * emitter kept a rect out of the batch, so a 920 px census is one rect refusing to be a square and
     * NOT the group's second loop failing to rasterise -- the reading this verb exists to make. */
    if (ref_refused || ora_dropped) {
        ps2_kprintf("[GSREAD] refs: refused%s%s%s%s%s%s orange dropped=%u -- NOT a GS answer\n",
                    (ref_refused & 1u) ? " CYAN" : "",
                    (ref_refused & 2u) ? " MAGENTA" : "",
                    (ref_refused & 4u) ? " GREEN" : "",
                    (ref_refused & 8u) ? " YELLOW" : "",
                    (ref_refused & 16u) ? " ORANGE" : "",
                    (ref_refused & 32u) ? " VIOLET" : "",
                    (unsigned int)ora_dropped);
    }

    /* rung 3, and only now: the page walk is worthless until the control has proved the read is
     * reading the canvas, and it is worthless twice over if a strip refused -- the strips are the
     * cheap look at where the rects claim to be, the bands are the expensive look at everywhere
     * else, and both go through the same channel with the same preconditions. */
    if (page_buf) gsread_page_census(page_buf, navy, &page);
    else ps2_kprintf("[GSREAD] page: SKIPPED -- the pool refused the %u kB band\n",
                     (unsigned int)((GSREAD_BAND_QWC * 16u) / 1024u));

    /* The strips' own answer, and it is the cheap one: three 64x16 windows at one x, inside RED, inside
     * BLUE, and below both.  Their expectations flipped with the format fix -- both colours are what a
     * repaired emitter owes this run -- so what each branch can still say is which SENDER delivered,
     * which is the column the recorder's ring lives on.  The navy control outranks all of it: a census
     * whose control is not the canvas is not evidence about anything. */
    if (red.err == GS_READ_BUS_OK && red.match == n &&
        blue.err == GS_READ_BUS_OK && blue.match == n) {
        ps2_kprintf("[GSREAD] verdict: BOTH strips full -- the dense emitter rasterised on the "
                    "synchronous sender AND on the LANE 10 ring, so the emptiness is in the stream's "
                    "content and the open page question is ORANGE's loop count\n");
    } else if (red.err == GS_READ_BUS_OK && red.match == n) {
        ps2_kprintf("[GSREAD] verdict: RED only -- the synchronous sender delivered the dense group, "
                    "the LANE 10 ring did not reach the strip, and the recorded stream rides that ring\n");
    } else if (blue.err == GS_READ_BUS_OK && blue.match == n) {
        ps2_kprintf("[GSREAD] verdict: BLUE only -- the ring delivered and the synchronous sender did "
                    "not, which is the reverse of what this port's console uploads do\n");
    } else {
        ps2_kprintf("[GSREAD] verdict: NEITHER strip holds its colour, though the navy control does, so "
                    "the read works -- the page rows above are what say whether the dense emitter landed "
                    "somewhere else or nowhere at all\n");
    }

    /* Last, because the free prints ([HEAP] again, over the same threshold) and a print after the
     * reads is only harmless once there is nothing left to erase. */
    if (page_buf) Ifree(page_buf);
}

/* Rung 28's census of one box, and the arithmetic it is asked to match.
 *
 * The five boxes differ ONLY in the ALPHA word, so the question each answers is "which word came
 * back", and the reading is a tally, not a picture.  `other` is the interesting bucket -- a pixel that
 * is neither the destination nor the raw source is a real mix -- and the two expectations name WHICH
 * mix: with As = 0x40 = 64, a `>>7` divisor predicts dest*3/2 and a `/255` divisor predicts dest*5/4,
 * so the brightening box says both whether the law painted and which scaling this GS applies.
 *
 * The compares are per channel, +-2, and ignore the alpha byte: the stored alpha of a blended
 * fragment is this port's RGBAQ business, not the blend equation's, and rung 28's subject is colour. */
typedef struct {
    uint32_t dest, src, other, m1, m2, first_other;
} gsalpha_box_t;

static uint32_t gsalpha_bright(uint32_t w, uint32_t num, uint32_t den)
{
    uint32_t out = 0u;
    uint32_t i;
    for (i = 0u; i < 24u; i += 8u) {
        uint32_t c = ((w >> i) & 0xffu) * num / den;
        if (c > 255u) c = 255u;
        out |= c << i;
    }
    return out;
}

/* The saturated sum of two canvas words, channel by channel -- the additive wash's prediction. */
static uint32_t gsalpha_add(uint32_t a, uint32_t b)
{
    uint32_t out = 0u;
    uint32_t i;
    for (i = 0u; i < 24u; i += 8u) {
        uint32_t c = ((a >> i) & 0xffu) + ((b >> i) & 0xffu);
        if (c > 255u) c = 255u;
        out |= c << i;
    }
    return out;
}

static int gsalpha_near(uint32_t a, uint32_t b)
{
    uint32_t i;
    for (i = 0u; i < 24u; i += 8u) {
        const int d = (int)((a >> i) & 0xffu) - (int)((b >> i) & 0xffu);
        if (d > 2 || d < -2) return 0;
    }
    return 1;
}

/* Read one box and sort every pixel into destination / raw source / something else, counting how many
 * of the "else" land on each of the two predictions.  -1 when the block did not come back at all,
 * which the caller treats exactly like the three tallies this helper replaced: an unread box is a
 * SKIP, not a zero. */
static int gsalpha_read_box(uint32_t *buf, int x0, uint32_t dest, uint32_t cs_stored,
                            uint32_t want1, uint32_t want2, gsalpha_box_t *t)
{
    const uint32_t n = (uint32_t)GSALPHA_BW * (uint32_t)(GSALPHA_Y1 - GSALPHA_Y0);
    uint32_t i;

    t->dest = t->src = t->other = t->m1 = t->m2 = 0u;
    t->first_other = 0u;

    if (ps2_gs_vram_read(x0, GSALPHA_Y0, GSALPHA_BW, GSALPHA_Y1 - GSALPHA_Y0, buf) <= 0 ||
        ps2_gs_vram_read_err() != GS_READ_BUS_OK)
        return -1;

    for (i = 0u; i < n; i++) {
        const uint32_t v = buf[i];
        if (v == dest)             t->dest++;
        else if (v == cs_stored)   t->src++;
        else {
            if (t->other == 0u) t->first_other = v;
            t->other++;
            if (want1 != 0u && gsalpha_near(v, want1)) t->m1++;
            if (want2 != 0u && gsalpha_near(v, want2)) t->m2++;
        }
    }
    return 0;
}

/* The canvas word under one box, taken AFTER the clear and BEFORE its draw -- the destination the
 * blender will read.  Each box asks about its own ground rather than about Z's, because the boxes are
 * 112 px apart on a canvas this port does not promise is flat; a shared word would make a blend on a
 * sloping background indistinguishable from a no-op. */
static uint32_t gsalpha_dest_at(int x0)
{
    return ps2_gs_get_framebuffer()[(uint32_t)(GSALPHA_Y0 + GSALPHA_Y1) / 2u
                                    * (uint32_t)PS2_SCREEN_WIDTH + x0 + GSALPHA_BW / 2];
}

/* Paint one box: program `env` with this ALPHA word, draw the one ABE-controlled sprite over
 * x0..x0+GSALPHA_BW, and drain the ring so the environment and its draw land in FIFO order and the
 * read's idle precondition holds.  The draws go out the ring (ps2_gif_post) rather than the
 * synchronous sender because the ENV rides the same ring; keeping both on it is what makes "env
 * before its draw" a property of the FIFO instead of a hope about two senders.  Five boxes ran this
 * shape as five copies until rung 28; the only thing that ever differed was the word and ABE. */
static int gsalpha_draw(int x0, uint32_t abe, uint64_t alpha_word, uint64_t *pkt, gs_draw_env *env)
{
    gs_poster_rect r;
    int32_t qw;

    r.y0 = GSALPHA_Y0; r.y1 = GSALPHA_Y1;
    r.x0 = x0;         r.x1 = x0 + GSALPHA_BW;

    env->reg_alpha = alpha_word;
    qw = gs_poster_ref_sprite_blend_packet(&r, GSALPHA_CS, GSALPHA_AS, abe, pkt, GS_REF_SPRITE_QWC);
    if (qw < 0 || ps2_gs_draw_env_apply(env) != 0 || ps2_gif_post(pkt, (uint32_t)qw) != 0)
        return -1;
    (void)ps2_gif_drain();
    (void)ps2_gif_reap();
    return 0;
}

/* ── LANE 19 rung 7: which field-role model does this GS's ALPHA register implement? ──────────
 *
 * The blackout rung 6 could not explain is a blend question, and rung 6 cannot even see it: every
 * control through rung 6 is ABE=0, and a blended fragment whose equation collapses to `+ color3(=Cd)`
 * writes the destination's own word back -- indistinguishable, on a page-vs-canvas read, from a
 * fragment that never arrived.  So `diff_all=0` surviving the CTXT fix is exactly what a no-op ALPHA
 * register predicts, and it is also what the cover-after-send burial predicts.  This probe picks
 * between them at the register itself.
 *
 * The one encoding, two readings.  gs_pack_alpha(a,b,c,d,fix) puts a->ALPHA[1:0], b->[3:2], c->[5:4],
 * d->[7:6], fix->[39:32] -- the packing ps2sdk's GS_SET_ALPHA does too, verbatim.  What is in dispute
 * is what those fields MEAN:
 *
 *   the port/ps2sdk header reading -- `result = (a - b) * sel(c) + d`, with `sel(c)` picking As/Ad/FIX
 *     out of field C[5:4].  The live env's word (CS,CD,AS,CD,fix=0) is then (Cs-Cd)*As+Cd, i.e. a
 *     visible source-over IF the vertex alpha reaches the blender.
 *   the hardware-manual reading -- `result = (b - c) * sel(a) + d`, with `sel(a)` picking the alpha
 *     MULTIPLIER out of field A[1:0].  The SAME word is then a=0 -> FIX, and the env's fix is 0, so
 *     (Cd-Cs)*0+Cd = Cd -- the fragment paints the destination back and leaves no trace.
 *
 * Three boxes decide the field roles, all with the same opaque source colour, so only the ALPHA word
 * varies:
 *
 *   Z  ABE=0, any ALPHA -- the reference.  Must paint the source exactly; if it does not, this run
 *      measured nothing and every row below is a SKIP.
 *   P  ABE=1, the LIVE word gs_pack_alpha(CS,CD,AS,CD,0) -- the register eegs_present.c actually ships.
 *   H  ABE=1, the hardware-order word gs_pack_alpha(3,0,1,1,128) -- the src-over the manual writes.
 *
 * Read the pixels: P back as the destination (navy) says the hardware reading holds and the live env
 * is the bug; P a colour that is neither source nor destination says the port reading holds and the
 * live env is innocent, moving the search to the cover-after-send burial.  H confirms the fix paints.
 *
 * Rung 28 adds two more boxes, and they ask a different question -- not "which roles do the fields
 * have" but "which of the equations the register CAN program is the one the XMB's wave needs".  The
 * operator's report of 2026-10-18 is that the 3D surface wave renders grey where it should be blue,
 * and the live stream names why without a screen: `[GSCHAIN] 2 op=4 iip=1 abe=1 verts=1984
 * bbox=0,241..767,357 c0=fe121212` is the ribbon -- a 1,982-triangle strip confined to the wave band,
 * blend enabled, and vertex colours that are GREY by design, because `xb_draw_ribbon()` sets
 * `glBlendFunc(GL_DST_COLOR, GL_ONE)` and paints a grey luminance map.  That GL blend means
 * `dst * (1 + src)`: the grey never appears as a colour, it only modulates the blue behind it, which
 * is what makes the sheet read as an embossed blue cloth.  The GS's blender has no per-channel colour
 * times colour term -- its equation is `(A - B) * alpha + D` with a SCALAR alpha -- so this port maps
 * "blended" onto source-over, `(Cs - Cd) * As + Cd`, and the ribbon's vertices all carry As = 0xfe =
 * 254, which makes that equation `2 * Cs - Cd`: the grey WINS, and the blue is erased wherever the
 * wave is.  That is the grey wave, and it is a blend-law question, not a delivery question --
 * geometry, colour and page all arrive, `[GS27]` measures 6/6 boxes as 1024/1024 stream ink.
 *
 * The two candidates, both expressible with one ALPHA word and no per-pixel work:
 *
 *   M  gs_pack_alpha(CD, ZERO, AS, CD, 0)   ->  (Cd - 0) * As >> 7 + Cd = Cd * (1 + As/128).
 *      The destination's hue survives exactly and only its brightness moves, which IS what
 *      `dst * (1 + src)` asks for -- the same law, with the ribbon's per-vertex brightness having to
 *      ride in the alpha byte instead of in the (irrelevant-here, because M ignores Cs) colour
 *      triple.  With As = 0x40 the prediction is 1.5 x dest; a `/255` divisor would give 1.25 x dest,
 *      and the box counts both so the run says which divisor this GS uses.
 *   X  gs_pack_alpha(CS, ZERO, FIX, CD, 255) ->  Cs * 255 >> 7 + Cd ~= 2*Cs + Cd: an ADDITIVE wash.
 *      Cheaper on the app side -- the grey stays in Cs and the vertex alpha never changes -- but it
 *      adds the grey rather than multiplying by it, so a bright sheet desaturates toward the source.
 *      With a pure-green source and FIX=255 both scalings predict the same word (G saturates, R and B
 *      stay the destination's), which is why X's row prints one expectation and the raw first word.
 *
 * M is the root-cause candidate, X the fallback.  Neither is shipped on a reading: this run's job is to
 * say which one paints and at which divisor, and the app-side half (carrying the ribbon's brightness
 * into RGBAQ's alpha) is only worth pricing once the register has answered. */
static void ps2_gsalpha_probe(void)
{
    static uint64_t s_pkt[2u * GS_REF_SPRITE_QWC] __attribute__((aligned(16)));
    static uint32_t s_buf[GSALPHA_BW * (GSALPHA_Y1 - GSALPHA_Y0)] __attribute__((aligned(16)));
    const uint32_t cs = GSALPHA_CS;        /* opaque green source, the one place the word is spelled */
    const uint32_t src_alpha = GSALPHA_AS; /* mid As: the blend's multiplier operand, not a stray A byte */
    const uint32_t cs_stored = (src_alpha << 24) | (cs & 0x00ffffffu);
    /* The word an UNBLENDED fragment leaves in CT32: the renderer writes the fragment's RGBAQ
     * straight out, so the destination's alpha byte becomes the fragment's own `src_alpha`.  Comparing
     * the raw source (`cs`, A=00) would count every unblended pixel as a blend; this is the exact word
     * the reference box must come back as. */
    const uint32_t n = (uint32_t)GSALPHA_BW * (uint32_t)(GSALPHA_Y1 - GSALPHA_Y0);
    gs_draw_env env;
    gsalpha_box_t z, p, h, m, x;
    uint32_t zd, pd, hd, md, xd;
    int trusted = 1;

    /* Destinations first, all five, before anything is drawn: the canvas this verb uploaded last is
     * what the blender will read, and the GS draws go to VRAM, not to that mirror. */
    ps2_gs_text_clear();
    zd = gsalpha_dest_at(GSALPHA_Z_X0);
    pd = gsalpha_dest_at(GSALPHA_P_X0);
    hd = gsalpha_dest_at(GSALPHA_H_X0);
    md = gsalpha_dest_at(GSALPHA_M_X0);
    xd = gsalpha_dest_at(GSALPHA_X_X0);

    ps2_kprintf("[GSALPHA] five 40x40 boxes at y=%d..%d, src=0x%08x As=0x%02x\n",
                GSALPHA_Y0, GSALPHA_Y1 - 1, (unsigned int)cs, (unsigned int)src_alpha);
    ps2_kprintf("[GSALPHA] dest: Z=0x%08x P=0x%08x H=0x%08x M=0x%08x X=0x%08x\n",
                (unsigned int)zd, (unsigned int)pd, (unsigned int)hd, (unsigned int)md,
                (unsigned int)xd);
    ps2_kprintf("[GSALPHA] Z=ABE0 ref  P=ABE1 live-word  H=ABE1 hardware-word  "
                "M=ABE1 dst-brighten  X=ABE1 additive; navy=write-back, blend=real mix\n");

    ps2_stage1_gs_env(&env);

    /* Z -- ABE=0 reference, under the live word (ABE=0 ignores it, so this also proves that claim). */
    if (gsalpha_draw(GSALPHA_Z_X0, 0u,
                     gs_pack_alpha(GS_BLEND_CS, GS_BLEND_CD, GS_ALPHA_AS, GS_BLEND_CD, 0u),
                     s_pkt, &env) < 0)
        trusted = 0;

    /* P -- ABE=1 under the word the shipping environment programs. */
    if (trusted &&
        gsalpha_draw(GSALPHA_P_X0, 1u,
                     gs_pack_alpha(GS_BLEND_CS, GS_BLEND_CD, GS_ALPHA_AS, GS_BLEND_CD, 0u),
                     s_pkt, &env) < 0)
        trusted = 0;

    /* H -- ABE=1 under the hardware-order src-over word (multiplier As in [1:0], FIX=128). */
    if (trusted && gsalpha_draw(GSALPHA_H_X0, 1u, gs_pack_alpha(3u, 0u, 1u, 1u, 128u),
                                s_pkt, &env) < 0)
        trusted = 0;

    /* M -- rung 28's destination-brighten: (Cd - 0) * As >> 7 + Cd. */
    if (trusted &&
        gsalpha_draw(GSALPHA_M_X0, 1u,
                     gs_pack_alpha(GS_BLEND_CD, GS_BLEND_ZERO, GS_ALPHA_AS, GS_BLEND_CD, 0u),
                     s_pkt, &env) < 0)
        trusted = 0;

    /* X -- rung 28's additive wash: (Cs - 0) * FIX + Cd with FIX at full. */
    if (trusted &&
        gsalpha_draw(GSALPHA_X_X0, 1u,
                     gs_pack_alpha(GS_BLEND_CS, GS_BLEND_ZERO, GS_ALPHA_FIX, GS_BLEND_CD, 255u),
                     s_pkt, &env) < 0)
        trusted = 0;

    if (!trusted) {
        ps2_kprintf("[GSALPHA] SKIP: a draw or the environment was refused, so the boxes are not all "
                    "on the bus -- nothing here names the ALPHA model\n");
        return;
    }

    /* Census each box into destination / raw source / a real mix, and -- for the two rung-28 boxes --
     * which mix.  A pixel that is neither navy nor the source colour is the whole answer to rung 7;
     * whether that pixel is the destination scaled is the whole answer to rung 28. */
    if (gsalpha_read_box(s_buf, GSALPHA_Z_X0, zd, cs_stored, 0u, 0u, &z) < 0 ||
        gsalpha_read_box(s_buf, GSALPHA_P_X0, pd, cs_stored, 0u, 0u, &p) < 0 ||
        gsalpha_read_box(s_buf, GSALPHA_H_X0, hd, cs_stored, 0u, 0u, &h) < 0 ||
        gsalpha_read_box(s_buf, GSALPHA_M_X0, md, cs_stored,
                         gsalpha_bright(md, 3u, 2u), gsalpha_bright(md, 5u, 4u), &m) < 0 ||
        gsalpha_read_box(s_buf, GSALPHA_X_X0, xd, cs_stored,
                         gsalpha_add(xd, cs), 0u, &x) < 0) {
        ps2_kprintf("[GSALPHA] SKIP: a page read failed -- the tallies are not a picture\n");
        return;
    }

    ps2_kprintf("[GSALPHA] Z ref : dest=%u src=%u blend=%u /%u\n",
                (unsigned int)z.dest, (unsigned int)z.src, (unsigned int)z.other, (unsigned int)n);
    ps2_kprintf("[GSALPHA] P live: dest=%u src=%u blend=%u first_blend=0x%08x /%u\n",
                (unsigned int)p.dest, (unsigned int)p.src, (unsigned int)p.other,
                (unsigned int)p.first_other, (unsigned int)n);
    ps2_kprintf("[GSALPHA] H hw  : dest=%u src=%u blend=%u first_blend=0x%08x /%u\n",
                (unsigned int)h.dest, (unsigned int)h.src, (unsigned int)h.other,
                (unsigned int)h.first_other, (unsigned int)n);
    ps2_kprintf("[GS28] M dstmul: dest=%u src=%u blend=%u first=0x%08x x3/2=%u x5/4=%u /%u\n",
                (unsigned int)m.dest, (unsigned int)m.src, (unsigned int)m.other,
                (unsigned int)m.first_other, (unsigned int)m.m1, (unsigned int)m.m2,
                (unsigned int)n);
    ps2_kprintf("[GS28] X add   : dest=%u src=%u blend=%u first=0x%08x add=%u /%u\n",
                (unsigned int)x.dest, (unsigned int)x.src, (unsigned int)x.other,
                (unsigned int)x.first_other, (unsigned int)x.m1, (unsigned int)n);

    if (z.src != n) {
        /* The reference did not paint, so the run measured no pipeline at all. */
        ps2_kprintf("[GSALPHA] SKIP: the ABE=0 reference is not solid source (src=%u/%u) -- the box never "
                    "reached the page, so the blend rows say nothing about the GS\n",
                    (unsigned int)z.src, (unsigned int)n);
        return;
    }

    /* The reference proves the geometry and the read, so the fork on P is a statement about the ALPHA
    ** register and nothing else. */
    if (p.dest == n) {
        ps2_kprintf("[GSALPHA] verdict: HARDWARE FIELD ROLES. The live word writes the destination back "
                    "(P is dest everywhere), so the port's gs_pack_alpha(CS,CD,AS,CD,fix=0) is a NO-OP and "
                    "is the blackout -- a blended fragment paints Cd and looks like no fragment to rung 6. ");
        if (h.other > 0u && h.dest == 0u)
            ps2_kprintf("H blends (%u px) -> the fix is gs_pack_alpha(3,0,1,1,128) at eegs_present.c:457.\n",
                        (unsigned int)h.other);
        else
            ps2_kprintf("H blend=%u dest=%u -- the fix word did NOT blend either, so re-read the "
                        "As path before shipping anything.\n",
                        (unsigned int)h.other, (unsigned int)h.dest);
    } else if (p.other > 0u) {
        ps2_kprintf("[GSALPHA] verdict: PORT FIELD ROLES. The live word blends (P has %u mixed px, none "
                    "left as dest), so eegs_present.c:457 is NOT the blackout -- the stream reaches a real "
                    "pixel and the emptiness must be the cover re-upload after send (core_ps2.c:1456) or a "
                    "prim state no rung varied. Run the re-send probe next.\n",
                    (unsigned int)p.other);
    } else if (p.src == n) {
        ps2_kprintf("[GSALPHA] verdict: ABE had no effect -- P is solid source like the reference, so the "
                    "blend stage was bypassed; the ALPHA register is not on this path and the blackout is "
                    "elsewhere.\n");
    } else {
        ps2_kprintf("[GSALPHA] verdict: MIXED -- P is %u dest / %u src / %u blend, which no single model "
                    "predicts; read the words against the tallies before believing this run.\n",
                    (unsigned int)p.dest, (unsigned int)p.src, (unsigned int)p.other);
    }

    /* Rung 28's own verdict, deliberately after the field-role fork because it is a question about a
     * LAW and not about the encoding: the ribbon needs the destination's brightness to move and its
     * hue to survive.  `blend == n` with the scaled-destination bucket filled says that happened;
     * `src == n` says the word had no effect and Cs was written, and `dest == n` says the box was a
     * no-op.  All three are answers about the grey wave; only the first is a way to fix it. */
    if (m.other == n && (m.m1 > 0u || m.m2 > 0u)) {
        ps2_kprintf("[GS28] verdict: the destination-brighten law PAINTS (%u/%u px are a mix, %u at "
                    "dest*3/2, %u at dest*5/4) and leaves the source colour out of it -- the ribbon's "
                    "dst*(1+src) is expressible as gs_pack_alpha(CD,ZERO,AS,CD,0) once the mesh carries "
                    "its brightness in As.  Divisor: %s\n",
                    (unsigned int)m.other, (unsigned int)n, (unsigned int)m.m1, (unsigned int)m.m2,
                    m.m1 >= m.m2 ? "As/128 (>>7)" : "As/255");
    } else if (x.other == n && x.m1 > 0u) {
        ps2_kprintf("[GS28] verdict: M did not hold the destination scaled (blend=%u, x3/2=%u) but X is "
                    "the destination PLUS the source on %u/%u px -- additive is the law this GS gives a "
                    "blended ribbon, and the wave's hue survives only as a wash.\n",
                    (unsigned int)m.other, (unsigned int)m.m1, (unsigned int)x.other,
                    (unsigned int)n);
    } else {
        ps2_kprintf("[GS28] verdict: neither candidate painted what its equation predicts -- M is "
                    "%u dest / %u src / %u blend (first 0x%08x), X is %u dest / %u src / %u blend "
                    "(first 0x%08x).  The grey wave is not explained by a brightening law this register "
                    "can program; read those words before shipping either.\n",
                    (unsigned int)m.dest, (unsigned int)m.src, (unsigned int)m.other,
                    (unsigned int)m.first_other, (unsigned int)x.dest, (unsigned int)x.src,
                    (unsigned int)x.other, (unsigned int)x.first_other);
    }
}

/* ── LANE 19 rung 8: `gstex`, the texture-stage discriminator ─────────────────────────────
 *
 * One textured SPRITE over a plane whose every texel is one known colour, with a white vertex, read
 * back on the headless Stage-1 path.  A WHITE vertex makes the CT32 modulate `out = vertex * texel`
 * collapse to `out = texel`, so the box comes back the plane's colour if the sampler ran.  If it did
 * not, the failure is NOT one symptom: an ignored sampler lays the WHITE vertex -- a visible pixel,
 * which is NOT the XMB's symptom -- while only a discarded fragment lays nothing and holds its
 * destination, and that (the `dest` fork below) is the [GSLIVE] shape rung 6 saw.  This probe
 * distinguishes the outcomes; it does not assume which one the app is hitting.
 *
 * Three boxes, run in the same env rung 7 proved delivers a flat sprite:
 *   U  UNTEXTURED opaque green -- the positive.  This is rung 4's CYAN shape; if it fails to paint, the
 *      run measured no geometry and neither textured row is evidence.
 *   R  TEXTURED white-over-RED  plane, PRIM written by its AD group and a TEXFLUSH before the draw.
 *   B  TEXTURED white-over-BLUE plane, PRIM written by its AD group and NO TEXFLUSH before the draw.
 *      R and B now differ by exactly one register write, so their two readings name whether that
 *      write is what latches the sampler: the colour fork (red plane vs blue plane) is still the
 *      control against an ignored TME -- two matching results would say the box is the vertex, not the
 *      page, whatever single colour the plane held -- and the flush fork is the control against
 *      TEXFLUSH mattering, the question rung 29 opened because eegs_texture() ships the texture trio
 *      with no flush and the old probe could not separate the two.
 *
 * Each plane is 64x64 and filled UNIFORMLY, so the texel coordinate the sprite samples cannot matter --
 * any coordinate inside, or wrapping through CLAMP/REPEAT, reads the one colour.  That makes this read
 * a clean statement about whether TME/TEX0 sampling happens at all, and not about ST arithmetic, which
 * a FST=0 sprite would otherwise make it.  A textured plane that reads its colour -> texturing works and
 * the blackout is upstream (the colours the app feeds into RGBAQ, or geometry); a textured plane that
 * reads white -> the sampler is bypassed; black -> the page read as zero (an upload-vs-TEX0 format
 * mismatch); navy (its destination) -> the fragment was discarded, which is the [GSLIVE] symptom.
 *
 * The planes go up through gs_tex_register()'s installed sink (a synchronous Host->Local image stream
 * on DMAC channel 2, the same path the app's atlas uses), BEFORE the env is applied, so "the page is
 * filled before a primitive samples it" is a property of this order and not a hope about two senders. */
/* A textured SPRITE that carries its OWN texture state, not just the environment's.
 *
 * The first version of this probe put TEX0/TEX1/CLAMP only in the drawing environment and got the
 * WHITE vertex back -- the sampler never latched.  That is exactly what the recorder does NOT do:
 * eegs_texture() sends the three registers as a mid-stream AD group (gs_rec_ad) and ps2_gs_tex.c's
 * header states the law plainly -- "a texture switch is a mid-stream state write; TEX0 cannot live
 * only [in the environment]."  A GS texture-page/CLUT cache is reloaded on the register write that
 * names a new TBA, and an env write that precedes a PRIM by a full DMA may sit in the wrong cache
 * state, so a TEXFLUSH is what forces the sampler to read the page a draw is about to name.  This
 * builder therefore emits, in ONE transfer:
 *   an AD group writing CLAMP_1 / TEX0_1 / TEX1_1, then PRIM, then (for the flush box) TEXFLUSH, then
 *   a PRE=1 REGLIST sprite loop (RGBAQ, ST, XYZ2, XYZ2) over that plane.
 * Keeping the state and the draw in one posted buffer is what makes "the page is named before the
 * primitive samples it" a property of this FIFO order and not a hope.
 *
 * LANE 19 rung 29 (2026-10-18): PRIM is now written by this AD group, exactly as ps2_gs_rec.c's
 * open_group() does.  The previous shape put PRIM only in the sprite's GIFTAG, and rung 12u/12v
 * MEASURED that a tag's PRIM field -- PRE set or not -- never programs the GS's PRIM register on
 * this machine; a chain that inherits the last AD-written primitive drew the WHITE vertex because
 * TME was never set, which the earlier probe mis-read as "the sampler is IGNORED."  That verdict
 * measured the probe's own delivery, not the eegs stream, which ships the AD write.  So the probe
 * now mirrors the recorder and can answer the real question.  The `flush` argument is the A/B: box R
 * names the plane and flushes before the draw, box B names it and does not, on the same uploaded
 * page in the same run -- if R reads its plane and B reads white, TEXFLUSH is required for the
 * sampler to latch, and `eegs_texture()` (which writes CLAMP/TEX0/TEX1 but no TEXFLUSH) is missing
 * it; if both read their plane, TEXFLUSH is a non-issue and the icon artefact is elsewhere. */
#define GS8_PACKET_QW 9u    /* 6 for the flush AD group (tag + PRIM/CLAMP/TEX0/TEX1/TEXFLUSH pairs), 3 for the sprite */

static int32_t gs8_textured_sprite(const gs_poster_rect *r, uint32_t vertex, const gs_tex_t *t,
                                   uint32_t flush, uint64_t *qw, uint32_t cap_qw)
{
    /* TME=1, ABE=0 (opaque), CTXT=0 (the _1 context these AD writes program).  PRE=1 carries
     * this PRIM in the tag so the register list is exactly the four that pack into two dense
     * quadwords: RGBAQ, ST, XYZ2, XYZ2 -- a sprite kicks on its second XYZ2.  The tag's PRIM is
     * inert on this GS (rung 12u); the AD write of the same word below is what sets TME. */
    const uint64_t prim = gs_pack_prim(GS_OP_SPRITE, 0u, 1u, 0u, 0u, 0u);
    static const uint32_t ad_regs[1] = { GS_NIB_AD };
    static const uint32_t sp_regs[4] = { GS_NIB_RGBAQ, GS_NIB_ST, GS_NIB_XYZ2, GS_NIB_XYZ2 };
    uint64_t *c = qw;
    uint32_t nad = 4u + (flush ? 1u : 0u);   /* CLAMP, TEX0, TEX1, PRIM, + TEXFLUSH when asked for */

    if (r == 0 || qw == 0 || t == 0) return -1;
    if (cap_qw < GS8_PACKET_QW) return -1;
    if (r->x1 <= r->x0 || r->y1 <= r->y0 || r->x0 < 0 || r->y0 < 0) return -1;

    /* AD group: NLOOP `nad`, one register each, EOP clear (the sprite loop follows in this same
     * DMA).  PRIM is written last among the state so it is the freshest AD word before the kick;
     * TEXFLUSH, when present, sits between the texture trio and PRIM. */
    *c++ = gs_pack_giftag(nad, 0u, 0u, 0u, GS_FLG_PACKED, 1u);
    *c++ = gs_pack_regs(ad_regs, 1u);
    c = gs_pack_pair(c, t->clamp, GS_GP_CLAMP_1);
    c = gs_pack_pair(c, t->tex0,  GS_GP_TEX0_1);
    c = gs_pack_pair(c, t->tex1,  GS_GP_TEX1_1);
    if (flush) c = gs_pack_pair(c, 1u, GS_GP_TEXFLUSH);
    c = gs_pack_pair(c, prim,     GS_GP_PRIM);

    /* The sprite loop, EOP ends the whole transfer. */
    *c++ = gs_pack_giftag(1u, 1u, 1u, prim, GS_FLG_REGLIST, 4u);
    *c++ = gs_pack_regs(sp_regs, 4u);
    *c++ = gs_pack_rgbaq(vertex & 0xFFu, (vertex >> 8) & 0xFFu,
                         (vertex >> 16) & 0xFFu, (vertex >> 24) & 0xFFu, GS_Q_ONE);
    *c++ = gs_pack_st(32.0f, 32.0f);
    *c++ = gs_pack_xyz2(gs_vertex_coord16(r->x0), gs_vertex_coord16(r->y0), 0u);
    *c++ = gs_pack_xyz2(gs_vertex_coord16(r->x1), gs_vertex_coord16(r->y1), 0u);

    return (int32_t)(c - qw) / 2;   /* GIF quadwords, the unit ps2_gif_post takes */
}

static void ps2_gstex_probe(void)
{
    static uint64_t s_pkt[2u * GS8_PACKET_QW] __attribute__((aligned(16)));
    static uint32_t s_buf[GS8_BW * (GS8_Y1 - GS8_Y0)] __attribute__((aligned(16)));
    static uint32_t s_plane_r[GS8_PLANE * GS8_PLANE] __attribute__((aligned(16)));
    static uint32_t s_plane_b[GS8_PLANE * GS8_PLANE] __attribute__((aligned(16)));
    gs_poster_rect r;
    gs_draw_env env;
    const gs_tex_t *tr, *tb;
    const uint32_t bw = GS8_BW, bh = (uint32_t)(GS8_Y1 - GS8_Y0);
    const uint32_t n = bw * bh;
    const uint32_t red_low = GSQUAD_RED & 0x00ffffffu,
                   blue_low = GSQUAD_BLUE & 0x00ffffffu, white_low = 0x00ffffffu;
    uint32_t dest;
    uint32_t i, u_green, r_plane, r_white, r_black, r_dest, b_plane, b_white, b_black, b_dest, b_other;
    uint32_t first_b_other = 0u;
    int trusted = 1;
    int32_t qw;

    ps2_gs_text_clear();
    dest = ps2_gs_get_framebuffer()[(uint32_t)(GS8_Y0 + GS8_Y1) / 2u
                                    * (uint32_t)PS2_SCREEN_WIDTH + GS8_R_X0 + (int)(bw / 2u)];

    /* Fill each plane with ONE canvas-order colour, verbatim: ps2_gs_tex_upload() streams these bytes
     * with no conversion and TEX0 says CT32, so a texel's word IS what a full-coverage modulate reads. */
    for (i = 0u; i < GS8_PLANE * GS8_PLANE; i++) { s_plane_r[i] = GSQUAD_RED; s_plane_b[i] = GSQUAD_BLUE; }

    /* Register -- the installed sink uploads each plane's bytes synchronously before this returns. */
    tr = gs_tex_register(s_plane_r, GS8_PLANE, GS8_PLANE, GS8_PLANE, GS8_PLANE);
    tb = gs_tex_register(s_plane_b, GS8_PLANE, GS8_PLANE, GS8_PLANE, GS8_PLANE);
    if (tr == 0 || tb == 0) {
        ps2_kprintf("[GSTEX] SKIP: a plane was refused (VRAM full?), so nothing can sample it -- "
                    "this run measured no texture stage\n");
        return;
    }

    ps2_kprintf("[GSTEX] three 40x40 boxes at y=%d..%d, dest=0x%08x; U=flat green, "
                "R=textured-over-red-plane+TEXFLUSH, B=textured-over-blue-plane-NOFLUSH, "
                "vertex=0x%08x\n",
                GS8_Y0, GS8_Y1 - 1, (unsigned int)dest, (unsigned int)GS8_VERTEX);
    ps2_kprintf("[GSTEX] plane=the sampler latched, white=TME/TEX0 did not take (or no flush "
                "latched it), black=page read as zero, navy=fragment discarded\n");

    /* U -- untextured opaque green, the geometry+read positive (rung 4's shape, unchanged). */
    r.y0 = GS8_Y0; r.y1 = GS8_Y1;
    r.x0 = GS8_U_X0; r.x1 = GS8_U_X0 + (int)bw;
    ps2_stage1_gs_env(&env);   /* reg_tex0/1/clamp are 0; a TME=0 sprite reads none of them */
    qw = gs_poster_ref_sprite_packet(&r, GSQUAD_GREEN, 0u, s_pkt, GS_REF_SPRITE_QWC);
    if (qw < 0 || ps2_gs_draw_env_apply(&env) != 0 || ps2_gif_post(s_pkt, (uint32_t)qw) != 0) {
        trusted = 0;
    } else { (void)ps2_gif_drain(); (void)ps2_gif_reap(); }

    /* R -- textured over the red plane, PRIM AD-written, TEXFLUSH before the draw. */
    r.x0 = GS8_R_X0; r.x1 = GS8_R_X0 + (int)bw;
    if (trusted) {
        ps2_stage1_gs_env(&env);
        env.reg_tex0 = tr->tex0; env.reg_tex1 = tr->tex1; env.reg_clamp = tr->clamp;
        qw = gs8_textured_sprite(&r, GS8_VERTEX, tr, 1u, s_pkt, GS8_PACKET_QW);
        if (qw < 0 || ps2_gs_draw_env_apply(&env) != 0 || ps2_gif_post(s_pkt, (uint32_t)qw) != 0) {
            trusted = 0;
        } else { (void)ps2_gif_drain(); (void)ps2_gif_reap(); }
    }

    /* B -- textured over the blue plane, same everything EXCEPT no TEXFLUSH: the flush fork. */
    r.x0 = GS8_B_X0; r.x1 = GS8_B_X0 + (int)bw;
    if (trusted) {
        ps2_stage1_gs_env(&env);
        env.reg_tex0 = tb->tex0; env.reg_tex1 = tb->tex1; env.reg_clamp = tb->clamp;
        qw = gs8_textured_sprite(&r, GS8_VERTEX, tb, 0u, s_pkt, GS8_PACKET_QW);
        if (qw < 0 || ps2_gs_draw_env_apply(&env) != 0 || ps2_gif_post(s_pkt, (uint32_t)qw) != 0) {
            trusted = 0;
        } else { (void)ps2_gif_drain(); (void)ps2_gif_reap(); }
    }

    if (!trusted) {
        ps2_kprintf("[GSTEX] SKIP: a draw or the environment was refused, so the boxes are not all on "
                    "the bus -- nothing here names the texture stage\n");
        return;
    }

    u_green = r_plane = r_white = r_black = r_dest = 0u;
    b_plane = b_white = b_black = b_dest = b_other = 0u;

    /* Tally the textured boxes on RGB only (mask off the A byte, which TCC=1 sets from the plane). */
    r.x0 = GS8_U_X0;
    (void)ps2_gs_vram_read(r.x0, GS8_Y0, (int)bw, (int)bh, s_buf);
    if (ps2_gs_vram_read_err() != GS_READ_BUS_OK) trusted = 0;
    else { for (i = 0u; i < n; i++) if (s_buf[i] == GSQUAD_GREEN) u_green++; }

    r.x0 = GS8_R_X0;
    if (trusted) (void)ps2_gs_vram_read(r.x0, GS8_Y0, (int)bw, (int)bh, s_buf);
    if (trusted && ps2_gs_vram_read_err() != GS_READ_BUS_OK) trusted = 0;
    else if (trusted) {
        for (i = 0u; i < n; i++) {
            uint32_t rgb = s_buf[i] & 0x00ffffffu;
            if (rgb == red_low) r_plane++;
            else if (rgb == white_low) r_white++;
            else if (rgb == 0u) r_black++;
            else if (s_buf[i] == dest) r_dest++;
        }
    }

    r.x0 = GS8_B_X0;
    if (trusted) (void)ps2_gs_vram_read(r.x0, GS8_Y0, (int)bw, (int)bh, s_buf);
    if (trusted && ps2_gs_vram_read_err() != GS_READ_BUS_OK) trusted = 0;
    else if (trusted) {
        for (i = 0u; i < n; i++) {
            uint32_t rgb = s_buf[i] & 0x00ffffffu;
            if (rgb == blue_low) b_plane++;
            else if (rgb == white_low) b_white++;
            else if (rgb == 0u) b_black++;
            else if (s_buf[i] == dest) b_dest++;
            else { if (b_other == 0u) first_b_other = s_buf[i]; b_other++; }
        }
    }

    if (!trusted) {
        ps2_kprintf("[GSTEX] SKIP: a page read failed -- the tallies are not a picture\n");
        return;
    }

    ps2_kprintf("[GSTEX] U flat-green : green=%u /%u\n", (unsigned int)u_green, (unsigned int)n);
    ps2_kprintf("[GSTEX] R text-red   : plane=%u white=%u black=%u dest=%u /%u\n",
                (unsigned int)r_plane, (unsigned int)r_white, (unsigned int)r_black,
                (unsigned int)r_dest, (unsigned int)n);
    ps2_kprintf("[GSTEX] B text-blue  : plane=%u white=%u black=%u dest=%u other=%u first=0x%08x /%u\n",
                (unsigned int)b_plane, (unsigned int)b_white, (unsigned int)b_black,
                (unsigned int)b_dest, (unsigned int)b_other, (unsigned int)first_b_other,
                (unsigned int)n);

    if (u_green != n) {
        ps2_kprintf("[GSTEX] SKIP: the untextured control painted only %u/%u px -- the box never reached "
                    "the page, so the textured rows say nothing about the GS\n",
                    (unsigned int)u_green, (unsigned int)n);
        return;
    }

    /* The control proves geometry and the read.  R names its plane WITH a flush, B names a different
     * plane WITHOUT one; the two forks together (colour, then flush) separate three questions the old
     * single-shape probe could only blur. */
    if (r_plane == n && b_plane == n) {
        ps2_kprintf("[GSTEX] verdict: TEXTURING WORKS, TEXFLUSH NOT REQUIRED. Both textured boxes came "
                    "back their own plane (R red, B blue), and B got there with no TEXFLUSH, so an "
                    "AD-written PRIM/TEX0 group latches the sampler on this GS without a flush.  The "
                    "eegs icon artefact is therefore NOT the sampler -- read the RGBAQ/geometry and the "
                    "filter/wrap the app feeds, next.\n");
    } else if (r_plane == n && b_white == n) {
        ps2_kprintf("[GSTEX] verdict: THE SAMPLER NEEDS TEXFLUSH. R flushed and read its red plane; B "
                    "named its plane but never flushed and read the WHITE vertex.  So a freshly-pointed "
                    "TEX0 does not reach the sampler until a TEXFLUSH forces the page reload -- and "
                    "eegs_texture() (backend_eegs.c) writes CLAMP/TEX0/TEX1 with NO flush, which is the "
                    "icon bug.  Add GS_GP_TEXFLUSH to that AD trio.\n");
    } else if (r_white == n && b_white == n) {
        ps2_kprintf("[GSTEX] verdict: TME/TEX0 STILL DO NOT TAKE even with PRIM AD-written -- both "
                    "boxes read the WHITE vertex.  The texture state is not the fix; the sampler never "
                    "engages on this send path at all, so look upstream of TEX0 (the upload, or whether "
                    "the dense chain carries its ST at all).\n");
    } else if (r_black == n && b_black == n) {
        ps2_kprintf("[GSTEX] verdict: the page reads ZERO -- both textured boxes came back black, so an "
                    "uploaded plane is sampled as 0.  The upload vs TEX0 format (PSM/pitch/byte order) is "
                    "the fault.\n");
    } else if (r_dest == n && b_dest == n) {
        ps2_kprintf("[GSTEX] verdict: the textured fragments are DISCARDED -- both boxes still hold their "
                    "destination, which is the [GSLIVE] symptom reproduced at the sprite level.\n");
    } else {
        ps2_kprintf("[GSTEX] verdict: MIXED -- R %u/%u/%u/%u (flush) and B %u/%u/%u/%u (no flush) plane/white/black/dest; "
                    "no single model predicts this, read the tallies against the words before believing "
                    "the run.\n",
                    (unsigned int)r_plane, (unsigned int)r_white, (unsigned int)r_black,
                    (unsigned int)r_dest, (unsigned int)b_plane, (unsigned int)b_white,
                    (unsigned int)b_black, (unsigned int)b_dest);
    }
}

/* `gsstat` -- the GS half of the question `heapstat` answers about the pool, and the reason they
 * are two verbs is that they answer different halves of the same empty window.  `heapstat` says
 * whether the machine had the RAM for the icon atlas; this says whether the atlas that did get
 * RAM ever reached the Synthesizer, and -- by arming the cycle printer below -- whether any
 * primitive has since been drawn with no plane to sample.
 *
 * This exists because on 2026-10-17 the census and the operator disagreed about the same image:
 * the census counted ink inside the XMB's client rect and the operator saw dots.  Both readings
 * were of that rectangle, and only one of them was of the picture.  `texmiss=` is the column that
 * separates them -- a textured primitive whose plane has no VRAM address is demoted and drawn
 * FLAT, which on a monitor is a fan of colour blocks where the icons should be -- and until now
 * it was armed only by the bench (`s_cycle.left` is set in no ship path), so the ship console
 * could print `swap=0 us` forever without ever saying whether the ink it sent was the right ink.
 *
 * `registered=` is the plane table read back, `pending=` counts planes whose Host->Local IMAGE the
 * bus has not taken, and `vram_left=` is the allocator's own budget in bytes -- the two shipped
 * sheets (the 512x512 icon atlas and the font sheet) are predicted to leave 1,081,344.
 *
 * Six passes, not a row per frame: the instrument is armed by asking, so a normal session's
 * console stays the product's.
 *
 * Not static, because the desktop's Terminal reaches it too (src/apps/gterm.c, PS2-only branch):
 * arming needs task-driven passes, and a task-driven pass is an app frame, so the verb has to be
 * reachable while the XMB is animating -- which is inside the GUI session, not at Stage 1. */
void ps2_shell_gsstat(void)
{
    uint32_t i;

    ps2_kprintf("[GS] planes: registered=%u pending=%u vram_left=%u\n",
                (unsigned int)gs_tex_count(), (unsigned int)gs_tex_pending(),
                (unsigned int)gs_tex_vram_left());
    for (i = 0; i < gs_tex_count(); i++) {
        const gs_tex_t *t = gs_tex_at(i);

        ps2_kprintf("[GS] plane %u: %ux%u bytes at %u, TBA %u, %u qwc%s\n",
                    (unsigned int)i, (unsigned int)t->px_w, (unsigned int)t->px_h,
                    (unsigned int)t->byte, (unsigned int)t->tba, (unsigned int)t->qwc,
                    t->pending ? ", PENDING (the bus has not taken its DMA)" : "");
    }
    if (gs_tex_count() == 0u) {
        ps2_kprintf("[GS] no plane is registered, so every textured primitive is being drawn "
                    "flat -- this is \"the window shows dots, not icons\".  Run `heapstat`: the "
                    "ask that failed is the 1024 kB icon atlas in gl_image_alloc().\n");
    }
    s_cycle.left = 6u;
    ps2_kprintf("[GS] armed: the next 6 task-driven passes print [XMBT] cycle + [GSTS].  "
                "Keep the XMB animating, then read texmiss=/drop=/plate= on the [GSTS] row -- "
                "any of the three non-zero is a piece of the picture the app drew and the "
                "screen does not have.\n");
}

/* `page` -- LANE 21's display-page dial: what format the canvas's VRAM page is in, and (rung 4's
 * half of it) whether two of them alternate.
 *
 * Runtime rather than build-time because every question it answers is a picture, and the only
 * honest way to compare two pictures is to change one thing and look: the operator flips between
 * PSMCT32 and PSMCT16 in one session, on one image, and says which one is flickering-free, which
 * is worth more than any number of builds made with a different flag.  A build-time dial would
 * also have been stale by the time it was read -- see the memory note "PS2_GL stamp reset by ANY
 * make" -- and `page` cannot be stale, because it reads the live configuration back.
 *
 * What it does NOT do is re-derive the CRT timing.  Changing a page's format changes one register
 * pair, DISPFB1/2's PSM field, and the bytes of the page itself; SMODE1 is the same 720p timing
 * whatever the canvas is stored as.  So the store goes through ps2_gs_dispfb_store() and the
 * canvas goes out whole, because the old page's bytes are this format's noise.
 *
 * `double` is the other half of this dial, and it is the half that cannot be applied blindly.
 * Turning the page model on moves every writer -- including ps2_gs_text_flush(), which is how the
 * Stage-1 prompt puts its own characters on the screen -- onto the page nobody is scanning, and
 * nothing flips it back.  A session that ran `page double 1` at the prompt would be a console that
 * takes commands and never shows an echo.  So the model is a COMPOSITOR present policy: this is the
 * dial, ps2_page_double_apply() is the single place that touches the hardware, and it runs at GUI
 * session entry -- where ps2_gui_pass() exists to do the flipping the dial promises -- and again at
 * session exit.  Setting it at the prompt is remembered and applied the moment a desktop starts,
 * which is also when the operator can see whether it worked. */
static uint32_t s_page_double_wanted;

/* Returns 1 when the page model is now `on`.  Refuses -- by name, with the dial that fixes it --
 * when the format has no second page inside the display reservation, which is PSMCT32's answer and
 * the one thing an operator at a serial console cannot see for themselves. */
static int ps2_page_double_apply(uint32_t on)
{
    if (gs_disp_is_double() == (on ? 1u : 0u)) { s_page_double_wanted = on; return (int)on; }
    if (gs_disp_set_double(on) != 0) {
        if (on) ps2_kprintf("[PAGE] double refused at %s: a second page would run past the display "
                            "reservation into the texture planes.  `page 16` first -- two 16-bit "
                            "pages are exactly the space one 32-bit page takes.\n",
                            gs_disp_psm() == GS_PSM_CT16 ? "PSMCT16" : "PSMCT32");
        return 0;
    }
    s_page_double_wanted = on;
    /* Both pages now hold content that does not describe this present: one is what DISPFB points at,
     * the other has never been written (or was written in the format just left behind).  Owed in
     * full they are plated in two passes, and the rotation is banded from there. */
    if (on) ps2_rot_owe_all();
    ps2_gs_dispfb_store();
    ps2_kprintf("[PAGE] double=%u: drawing page %u, showing page %u.  A present now plates the page "
                "nobody is scanning and flips at the frame boundary; what each page owes while it is "
                "hidden travels with it.\n",
                (unsigned int)on, (unsigned int)gs_disp_drawn()->index,
                (unsigned int)gs_disp_shown()->index);
    return (int)on;
}

void ps2_shell_page(const char *arg)
{
    const gs_disp_page_t *shown = gs_disp_shown();
    const gs_disp_page_t *drawn = gs_disp_drawn();
    uint32_t changed = 0u;

    if (arg == 0 || *arg == 0) {
        /* The question also answers itself, in the units the registers use. */
    } else if (tkl_strcmp(arg, "16") == 0 || tkl_strcmp(arg, "ct16") == 0) {
        changed = gs_disp_set_psm(GS_PSM_CT16) == 0 ? 1u : 0u;
        if (!changed) ps2_kprintf("[PAGE] PSMCT16 refused: gs_disp_page_span() cannot lay a page out "
                                  "in it.  Nothing changed.\n");
    } else if (tkl_strcmp(arg, "32") == 0 || tkl_strcmp(arg, "ct32") == 0) {
        changed = gs_disp_set_psm(GS_PSM_CT32) == 0 ? 1u : 0u;
        if (!changed) ps2_kprintf("[PAGE] PSMCT32 refused.  Nothing changed.\n");
    } else if (tkl_strncmp(arg, "dither", 6) == 0) {
        const char *v = arg + 6;
        while (*v == ' ') v++;
        if (*v == '1') { ps2_gs_set_dither(1u); changed = 1u; }
        else if (*v == '0') { ps2_gs_set_dither(0u); changed = 1u; }
        else ps2_kprintf("[PAGE] dither takes 0 or 1: 1 is the ordered 4x4, 0 truncates.\n");
    } else if (tkl_strncmp(arg, "double", 6) == 0) {
        /* Rung 4's half of this dial, and it is live for exactly one reason: the flicker is a
         * picture, and the only instrument that can tell a torn frame from a stale one is the
         * operator's monitor.  Being able to turn the two-page present on and off WITHOUT a rebuild
         * is what makes that a question with an answer, so the same image carries both.
         *
         * Applying it here, mid-session, is safe and is what makes an A/B possible, because
         * ps2_gui_pass() re-reads gs_disp_is_double() every pass: the desktop's next present is a
         * flip.  At the prompt it is a promise kept later -- see the comment above ps2_shell_page(). */
        const char *v = arg + 6;
        while (*v == ' ') v++;
        if (*v == '1') ps2_page_double_apply(1u);
        else if (*v == '0') ps2_page_double_apply(0u);
        else ps2_kprintf("[PAGE] double takes 0 or 1: 1 draws the hidden page and flips at vsync, "
                    "0 is the single-buffer present that flickers.\n");
    } else {
        ps2_kprintf("[PAGE] usage: page [32|16|dither 0|1|double 0|1]\n");
    }

    if (changed) {
        /* Re-point the CRT at the page as it now is, then re-plate the whole canvas into it: half
         * the bytes of the picture it held are now a different picture's pixels, and no band of the
         * normal present would rewrite the rest.  A GUI session then needs a full damage rect,
         * because its canvas is only re-composited where something was invalidated, and the GS
         * layer's ink lives in the stream, which the next present re-sends over the whole client. */
        ps2_gs_dispfb_store();
        ps2_gs_flush();
        /* Both pages' bytes are now this format's noise, so both owe the canvas -- see the law above
         * ps2_rot_owe_all().  Without this a rotation that survived the dial would keep presenting
         * one page's stale pixels and calling it a frame. */
        ps2_rot_owe_all();
        if (s_gui_active) {
            /* Fields one at a time, as this file's rule for a hard-float TU says: a struct move here
             * is what put sdc1/ldc1 -- illegal on the R5900 -- into the image before. */
            RECT full;
            full.left = 0; full.top = 0;
            full.right = (H)PS2_SCREEN_WIDTH; full.bottom = (H)PS2_SCREEN_HEIGHT;
            wnd_inval_damage_rect(&full);
        }
        shown = gs_disp_shown();
        drawn = gs_disp_drawn();
    }

    ps2_kprintf("[PAGE] psm=%s (%u bytes/px, %u px/qword) shown=%u@0x%x drawn=%u@0x%x fbw=%u "
                "page=%u B\n",
                gs_disp_psm() == GS_PSM_CT16 ? "PSMCT16" :
                gs_disp_psm() == GS_PSM_CT32 ? "PSMCT32" : "other",
                (unsigned int)shown->bytes_pp, (unsigned int)shown->px_per_qw,
                (unsigned int)shown->index, (unsigned int)shown->byte,
                (unsigned int)drawn->index, (unsigned int)drawn->byte,
                (unsigned int)shown->fbw, (unsigned int)shown->bytes);
    ps2_kprintf("[PAGE] 16-bit packer dither=%s  double=%u  vram_left=%u\n",
                ps2_gs_dither() ? "4x4 ordered" : "off (truncate)",
                (unsigned int)gs_disp_is_double(),
                (unsigned int)gs_tex_vram_left());
    /* The flip's own three numbers, and the middle one is the reason they are printed: a flip
     * whose vsync wait timed out lands its DISPFB store at an unknown point of the raster, which
     * is a torn frame with a good-looking log behind it.  `unaligned` non-zero says PCSX2's GS is
     * not raising the bit this port polls, and that is a different bug from a flicker.
     *
     * `owed` is the rotation's health, and -1 means the canvas: a pass whose present is a catch-up
     * pays it and clears it, so debt that never drains is either a merge rule gone wrong or a
     * compositor that is not flipping at all. */
    ps2_kprintf("[PAGE] flips=%u  unaligned=%u  longest wait=%u us  owed: p0=%d p1=%d\n",
                (unsigned int)ps2_gs_flip_count(),
                (unsigned int)ps2_gs_flip_unaligned(),
                (unsigned int)ps2_gs_flip_wait_max_us(),
                ps2_rot_debt(0u), ps2_rot_debt(1u));
}

#if BTRON_PS2_PAINT_WORKER
/* The row #165 is judged by.  `credit=` is claim (1) -- the composite re-records the pass did not
 * run -- and `slice_left=`/`longest=` are claim (2): what the frame had left when the app's frame
 * went out, and the worst single paint it had to fit.  `forced=` is the override firing, and a
 * worker whose whole saving comes with `forced` equal to `defers` is a worker that deferred nothing
 * at all, which is why both are printed on the same line rather than one under the other. */
static void ps2_pw_row(const ps2_pw_stats_t *p, int taken)
{
    ps2_kprintf("[PW] %s pass=%u ask=%u paint=%u defer=%u forced=%u dup=%u credit=%u | "
                "slice=%u left=%u paint_us=%u longest=%u jobs=%u\n",
                s_pw_on ? "on " : "off",
                (unsigned int)s_pw_passes, (unsigned int)p->asks, (unsigned int)p->paints,
                (unsigned int)p->defers, (unsigned int)p->forceds, (unsigned int)p->dups,
                (unsigned int)p->credit, (unsigned int)p->slice_us,
                (unsigned int)p->slice_left_us, (unsigned int)p->paint_us,
                (unsigned int)p->longest_us, (unsigned int)p->jobs);
    if (!taken) {
        /* The counters the operator is being shown are the ones since the last `paintw stat`, so a
         * second row of the same shape reads as "nothing happened in between" rather than as a bug. */
        ps2_kprintf("[PW]  (totals since boot; `stat` takes one window and clears it)\n");
    }
}

/* One line per priced window: the name is the app's own caption, and `est` is the number the next
 * pass's admission is made against, so a row that reads `defer=900 est=9000` says the app costs a
 * frame and a half and is being refused for it. */
static void ps2_pw_jobs(void)
{
    ps2_pw_job_t j;
    uint32_t i;

    for (i = 0u; ps2_pw_job_at(i, &j); i++) {
        const WND *w = (const WND *)j.wnd;
        /* Plain `%s`, because ps2_kprintf() parses a field width but not a precision: `%-16.16s`
         * makes it consume the `.` as an unknown conversion, print the rest of the specifier as
         * text, and read every argument after it one slot out of place -- which is how the first
         * version of this row came back with an `est=` of 2,888,526 us for a 21 ms paint. */
        ps2_kprintf("[PW]   %s %s est=%u last=%u paints=%u us=%u defer=%u forced=%u\n",
                    w && w->title[0] ? w->title : "?",
                    j.layer ? "layer" : "canvas",
                    (unsigned int)j.est_us, (unsigned int)j.last_us, (unsigned int)j.paints,
                    (unsigned int)j.us_total, (unsigned int)j.defers, (unsigned int)j.forceds);
    }
}

/* The runtime arm.  Off, the present path is the shipped one: the latch is dropped, the layer is
 * re-recorded by every band that reaches it, and no ask happens -- which is what makes this chord an
 * A/B on one image instead of a rebuild per question, the same reason Ctrl+P exists for rung 4. */
static void ps2_pw_apply(uint32_t on)
{
    s_pw_on = (int)on;
    s_pw_upload_us = 0u;
    s_pw_layer = NULL;
    ps2_pw_freeze(0);
    if (on) ps2_pw_reset(0u);   /* 0 keeps the module's own bound; a stale price cannot cross an off period */
    ps2_kprintf("[PW] paint worker %s: %s\n", on ? "ON" : "off",
                on ? "the layer records once per pass and the app's frame is admitted against the "
                     "frame's remaining slice"
                   : "every band re-records the layer, as the shipped image does");
}

void ps2_shell_paintw(const char *arg)
{
    ps2_pw_stats_t st;

    if (arg && *arg) {
        if (tkl_strcmp(arg, "1") == 0)      { ps2_pw_apply(1u); return; }
        if (tkl_strcmp(arg, "0") == 0)      { ps2_pw_apply(0u); return; }
        if (tkl_strncmp(arg, "stat", 4) == 0) {
            ps2_pw_stats_take(&st);        /* one profiling window, maxima included */
            ps2_pw_row(&st, 1);
            ps2_pw_jobs();
            return;
        }
        if (tkl_strncmp(arg, "all", 3) == 0) {
            ps2_pw_stats(&st);
            ps2_pw_row(&st, 0);
            ps2_pw_jobs();
            return;
        }
    }
    ps2_kprintf("[PW] usage: paintw [0|1|stat|all]   Ctrl+O does the 0/1 part inside a desktop.\n");
    ps2_kprintf("[PW]   what this is: a per-app PAINT BUDGET on one core, not a second thread.  The\n"
                "                     EE has no scheduler to move work onto (sta_tsk runs a body on\n"
                "                     the caller's stack), so the wins are one recording per pass\n"
                "                     however many bands reach the app, and a frame that can refuse\n"
                "                     a paint it cannot afford.  `PS2_PW=1` at build time is what\n"
                "                     puts it in the image at all; this dial only arms and disarms.\n");
    ps2_kprintf("[PW]   default starve bound=%u us (a job older than this is admitted whatever it costs)\n",
                (unsigned int)ps2_pw_starve_us());
}
#endif /* BTRON_PS2_PAINT_WORKER */

static char cmd_buf[64];
static int cmd_pos = 0;
static int esc_state = 0;
static int esc_num = 0;

static void ps2_shell_exec(const char *cmd)
{
    if (tkl_strcmp(cmd, "help") == 0) {
        ps2_kprintf("Available commands:\n");
        ps2_kprintf("  desktop / startx - Launch authentic B-System 800x600 GUI session\n");
        ps2_kprintf("  exit / console   - Exit GUI session and return to Stage 1 shell\n");
        ps2_kprintf("  res              - Display active resolution and GS PCRTC mode\n");
        ps2_kprintf("  page [32|16|dither 0|1|double 0|1]\n"
                    "                     - The canvas's VRAM page: its format, the order its pixels\n"
                    "                     are packed in, and whether two of them alternate.  16 bpp is\n"
                    "                     what fits TWO pages where one 32-bit page sits today, and\n"
                    "                     `double 1` draws the hidden one and flips at the frame edge\n"
                    "                     (LANE 21 rungs 3-4).  Ctrl+P does the same inside a desktop.\n");
#if BTRON_PS2_PAINT_WORKER
        ps2_kprintf("  paintw [0|1|stat|all]\n"
                    "                     - Task #165's per-app paint budget: the GS layer records ONE\n"
                    "                     frame per pass however many bands reach it, and an app's frame\n"
                    "                     is admitted against the microseconds this pass has left.\n"
                    "                     `stat` prints the window since the last ask, `all` the totals.\n"
                    "                     Ctrl+O does the 0/1 part inside a desktop.  (Not a thread: the\n"
                    "                     EE has one core and this port no scheduler -- see the header.)\n");
#endif
        ps2_kprintf("  usb [probe]      - Show OHCI + HID host state (probe = re-measure and re-enumerate)\n");
        ps2_kprintf("  help             - Display this command list\n");
        ps2_kprintf("  info             - Display PS2 system and hardware specifications\n");
        ps2_kprintf("  apps             - List all desktop applications\n");
        ps2_kprintf("  open <app>       - Open app: cabinet, editor, terminal, sound, chat, settings\n");
        ps2_kprintf("  tip [mode]       - Set or cycle TIP/IME (ascii, hira, kata, tibetan)\n");
        ps2_kprintf("  tasks            - List active RTOS tasks\n");
        ps2_kprintf("  mem              - Show memory breakdown\n");
        ps2_kprintf("  heapstat         - Who holds the kernel heap, by allocating site, + stack\n"
                    "                     high-water.  Resolve a printed 0xADDR on the host:\n"
                    "                     scripts/ps2_heap_who.sh 0xADDR   (or pass it the log)\n");
        ps2_kprintf("  gsstat           - Which texture pages the GS actually holds (registered/pending/\n"
                    "                     vram_left), then arm 6 [GSTS] frame rows: texmiss= names a\n"
                    "                     textured primitive drawn FLAT for want of a page -- the dots\n"
                    "                     a window shows instead of its icons.  Type it in the Terminal\n"
                    "                     with the app running, not here.\n");
        ps2_kprintf("  status           - Show mouse position, TIP mode, and system status\n");
        ps2_kprintf("  mouse <x> <y>    - Move cursor to absolute position (0..800, 0..600)\n");
        ps2_kprintf("  move <dx> <dy>   - Move cursor relative by (dx, dy)\n");
        ps2_kprintf("  sens [pct]       - Show or set the USB pointer scale (1..400%%, 100 = as reported)\n");
        ps2_kprintf("  maxstep [px]     - Show or set the pointer's max px per INPUT period (0 = no cap)\n");
        ps2_kprintf("  ptrsrc [emu|hw|auto] - Pointer policy: emulator device, real mouse, or re-detect\n");
        ps2_kprintf("  ptrstat <tag>    - Print+reset pointer counts, paint cost, and mouse->screen ms\n");
        ps2_kprintf("  ptrcal           - Loopback-calibrate the pointer path (no mouse needed)\n");
        ps2_kprintf("  ptgain [w] [pct] - Gain from a host sweep of width w (pct = px/host px x100)\n");
        ps2_kprintf("  gsquad           - Draw RED+BLUE blocks with the GS alone: does it raster, and\n"
                    "                     which sender delivered them? (sync DMA vs the LANE 10 ring)\n");
        ps2_kprintf("  gsread           - gsquad's two blocks + four SDK-shaped refs and rung 5's PRE\n"
                    "                     control (CYAN/MAGENTA/GREEN/YELLOW/ORANGE), then READ page 0\n"
                    "                     back over Local->Host: three strips and the whole page in\n"
                    "                     bands.  The census forks the register STRIDE, the group's\n"
                    "                     shape, the sprite nudge, the primitive and PRE, with no\n"
                    "                     monitor and no witness (LANE 19 rungs 2, 3, 4 and 5)\n");
        ps2_kprintf("  gsalpha          - rung 7: five 40x40 ABE boxes over the ALPHA register's two field\n"
                    "                     readings; P=live word, H=hardware word, Z=ABE=0 reference.\n"
                    "                     Names whether this GS blends or writes the destination back.\n"
                    "                     Rung 28 adds M=(Cd-0)*As+Cd and X=(Cs-0)*FIX+Cd, the two laws\n"
                    "                     that could carry the XMB ribbon's dst-colour brighten.\n");
        ps2_kprintf("  gstex            - rung 8: one UNTEXTURED sprite (positive) and two TEXTURED sprites\n"
                    "                     over a white vertex, each sampling a plane filled one colour\n"
                    "                     (red, blue).  The box coming back the plane's colour names\n"
                    "                     whether TME/TEX0 sampling happens at all -- the one stage a flat\n"
                    "                     sprite never exercises (LANE 19 rung 8)\n");
        ps2_kprintf("  click [1|2]      - Click Left (1) or Right (2) mouse button\n");
        ps2_kprintf("  key <char>       - Inject key event into active window\n");
        ps2_kprintf("  type <text>      - Type string into active window\n");
        ps2_kprintf("  pad <hex> [lx ly] - Simulate DualShock 2 controller state\n");
        ps2_kprintf("  clear            - Clear console screen\n");
        ps2_kprintf("  reboot           - Halt/Reset the Emotion Engine\n");
    } else if (tkl_strcmp(cmd, "desktop") == 0 || tkl_strcmp(cmd, "startx") == 0 || tkl_strcmp(cmd, "gui") == 0) {
        if (!s_gui_active) {
            launch_ps2_desktop_session();
        } else {
            ps2_kprintf("[PS2] Desktop GUI is already active.\n");
        }
    } else if (tkl_strcmp(cmd, "exit") == 0 || tkl_strcmp(cmd, "console") == 0 || tkl_strcmp(cmd, "quit") == 0) {
        if (s_gui_active) {
            s_gui_active = 0;
            return;
        } else {
            ps2_kprintf("[PS2] Already at Stage 1 console.\n");
        }
    } else if (tkl_strcmp(cmd, "res") == 0) {
        ps2_kprintf("PS2 Graphics Synthesizer Display Status:\n");
        ps2_kprintf("  Resolution : %dx%d @ 60Hz Non-Interlaced Progressive (1x)\n", PS2_SCREEN_WIDTH, PS2_SCREEN_HEIGHT);
        ps2_kprintf("  Color Depth: 32-bpp RGBA (GS CT32)\n");
        ps2_kprintf("  PCRTC Mode : VESA 800x600 (omode 0x2B, field 0)\n");
        ps2_kprintf("  eDRAM Alloc: 1,920,000 bytes (FBW=13 / 832 px stride)\n");
        ps2_kprintf("  GIF DMA Bus: 1.2 GB/s Host->Local (15 chunks x 8000 QWs)\n");
    } else if (tkl_strcmp(cmd, "usb") == 0) {
        ps2_log_ohci_probe(ps2_usb_last_probe());
        (void)ps2_log_host();
    } else if (tkl_strcmp(cmd, "usb probe") == 0) {
        /* Re-measuring means putting the controller back through a reset, which
         * also tears down whatever host engine was running on it -- so the one
         * has to be followed by the other or the prompt comes back deaf. */
        int verdict = ps2_usb_probe(NULL);
        ps2_log_ohci_probe(ps2_usb_last_probe());
        if (verdict == PS2_OHCI_IOP_DMA) ps2_usb_host_start();
        (void)ps2_log_host();
        /* Enumeration is what tells the two stubs apart, so it is re-read here for
         * the same reason the host is restarted: a device that appeared after boot
         * changes what a count means. */
        ps2_ptr_src_detect();
    } else if (tkl_strcmp(cmd, "clear") == 0) {
        if (!s_gui_active) {
            ps2_gs_text_clear();
            ps2_gs_text_flush();
        }
    } else if (tkl_strcmp(cmd, "info") == 0) {
        ps2_kprintf("B-System / BTRON3 3.20 [Sony PlayStation 2 Cleanroom Port]\n");
        ps2_kprintf("  CPU     : Emotion Engine MIPS R5900 @ 294.912 MHz\n");
        ps2_kprintf("  Memory  : 32 MB RDRAM (0x00000000..0x01FFFFFF)\n");
        ps2_kprintf("  Display : GS 4MB eDRAM (800x600 @ 32-bpp RGBA VESA Progressive Scan)\n");
        ps2_kprintf("  Desktop : Authentic B-System Compositor with Real Body Icons\n");
        ps2_kprintf("  Console : EE SIO0 UART @ 115200 8N1 + GS Framebuffer Text\n");
        ps2_kprintf("  Cursor  : (%d, %d)\n", s_mouse_x, s_mouse_y);
    } else if (tkl_strcmp(cmd, "apps") == 0) {
        ps2_kprintf("Desktop Applications:\n");
        ps2_kprintf("  1. Cabinet      - Real Body & Virtual Object Manager\n");
        ps2_kprintf("  2. Editor       - Full Multi-Line B-Editor\n");
        ps2_kprintf("  3. Terminal     - GTerm Shell\n");
        ps2_kprintf("  4. Sound        - Audio Player\n");
        ps2_kprintf("  5. Chat         - Interactive Dialogue\n");
        ps2_kprintf("  6. Settings     - Control Panel System Settings\n");
        ps2_kprintf("  7. XMB          - Cross Media Bar (PS3 Media Menu)\n");
        ps2_kprintf("  8. MSX Games    - B-MSX Cartridge Player System\n");
        ps2_kprintf("  9. Quake        - Quake 3D FPS Game\n");
    } else if (tkl_strncmp(cmd, "open ", 5) == 0) {
        const char *app = cmd + 5;
        if (tkl_strcmp(app, "cabinet") == 0 || tkl_strcmp(app, "vobj") == 0) {
            open_vobj_manager_window();
            ps2_kprintf("[PS2-UI] Opened Real Body Cabinet.\n");
        } else if (tkl_strcmp(app, "editor") == 0 || tkl_strcmp(app, "text") == 0) {
            open_t_editor_window();
            ps2_kprintf("[PS2-UI] Opened Text Editor.\n");
        } else if (tkl_strcmp(app, "terminal") == 0 || tkl_strcmp(app, "gterm") == 0 || tkl_strcmp(app, "cli") == 0) {
            open_gterm_window();
            ps2_kprintf("[PS2-UI] Opened Terminal Shell.\n");
        } else if (tkl_strcmp(app, "sound") == 0 || tkl_strcmp(app, "audio") == 0) {
            open_audio_player_window();
            ps2_kprintf("[PS2-UI] Opened Audio Player.\n");
        } else if (tkl_strcmp(app, "chat") == 0) {
            launch_beos_chat();
            ps2_kprintf("[PS2-UI] Opened Chat Dialog.\n");
        } else if (tkl_strcmp(app, "settings") == 0 || tkl_strcmp(app, "panel") == 0) {
            open_control_panel_window();
            ps2_kprintf("[PS2-UI] Opened Control Panel Settings.\n");
        } else if (tkl_strcmp(app, "xmb") == 0) {
            open_xmb_window();
            ps2_kprintf("[PS2-UI] Opened Cross Media Bar (XMB).\n");
        } else if (tkl_strcmp(app, "msx") == 0 || tkl_strcmp(app, "game") == 0) {
            open_msx_window();
            ps2_kprintf("[PS2-UI] Opened B-MSX Game System.\n");
        } else if (tkl_strcmp(app, "quake") == 0) {
            open_quake_window(120, 60, 480, 360);
            ps2_kprintf("[PS2-UI] Opened Quake 3D.\n");
        } else {
            ps2_kprintf("Unknown application: '%s'\n", app);
        }
    } else if (tkl_strncmp(cmd, "tip", 3) == 0) {
        const char *arg = cmd + 3;
        while (*arg == ' ') arg++;
        if (*arg == '\0') {
            tip_toggle_mode();
        } else if (tkl_strcmp(arg, "ascii") == 0) tip_set_mode(TIP_MODE_ASCII);
        else if (tkl_strcmp(arg, "hira") == 0)    tip_set_mode(TIP_MODE_HIRAGANA);
        else if (tkl_strcmp(arg, "kata") == 0)    tip_set_mode(TIP_MODE_KATAKANA);
        else if (tkl_strcmp(arg, "tibetan") == 0) tip_set_mode(TIP_MODE_TIBETAN);
        ps2_kprintf("[PS2-UI] TIP mode set to %s\n", tip_get_mode_str());
    } else if (tkl_strcmp(cmd, "tasks") == 0) {
        ps2_kprintf("TID  NAME           PRI  STAT   STACK BASE\n");
        ps2_kprintf("  1  ps2_idle         0  READY  0x01FE0000\n");
        ps2_kprintf("  2  ps2_desktop      5  RUN    0x01FD0000\n");
        ps2_kprintf("  3  ps2_sio_shell    4  WAIT   0x01FC0000\n");
    } else if (tkl_strcmp(cmd, "mem") == 0) {
        ps2_kprintf("RDRAM Total : 33554432 bytes (32 MB)\n");
        ps2_kprintf("Kernel Heap :  %u bytes (%u MB, used: %u, largest hole: %u)\n",
                    (unsigned int)PS2_HEAP_SIZE, (unsigned int)(PS2_HEAP_SIZE / (1024u * 1024u)),
                    (unsigned int)s_heap_used, (unsigned int)ps2_heap_largest_free());
        ps2_kprintf("VRAM eDRAM  :  4194304 bytes (4 MB)\n");
    } else if (tkl_strcmp(cmd, "heapstat") == 0) {
        /* The same census the allocator prints by itself when an ask dies, on demand: who holds the
         * pool, by the address that asked, plus the stack high-water that bounds how big the pool
         * may be.  `scripts/ps2_heap_who.sh` turns the addresses into names on the host. */
        ps2_heap_holders("asked for by heapstat");
    } else if (tkl_strcmp(cmd, "status") == 0) {
        ps2_kprintf("Mouse Cursor: (%d, %d)\n", s_mouse_x, s_mouse_y);
        ps2_kprintf("TIP Mode    : %s\n", tip_get_mode_str());
    } else if (tkl_strncmp(cmd, "mouse ", 6) == 0) {
        int x = 0, y = 0;
        const char *p = cmd + 6;
        while (*p >= '0' && *p <= '9') { x = x * 10 + (*p - '0'); p++; }
        while (*p == ' ') p++;
        while (*p >= '0' && *p <= '9') { y = y * 10 + (*p - '0'); p++; }
        s_mouse_x = x;
        s_mouse_y = y;
        ps2_move_mouse(0, 0);
        ps2_kprintf("[PS2] Cursor moved to (%d, %d)\n", s_mouse_x, s_mouse_y);
    } else if (tkl_strncmp(cmd, "move ", 5) == 0) {
        int sign_x = 1, dx = 0, sign_y = 1, dy = 0;
        const char *p = cmd + 5;
        if (*p == '-') { sign_x = -1; p++; }
        while (*p >= '0' && *p <= '9') { dx = dx * 10 + (*p - '0'); p++; }
        while (*p == ' ') p++;
        if (*p == '-') { sign_y = -1; p++; }
        while (*p >= '0' && *p <= '9') { dy = dy * 10 + (*p - '0'); p++; }
        ps2_move_mouse(dx * sign_x, dy * sign_y);
        ps2_kprintf("[PS2] Cursor moved by (%d, %d) -> now at (%d, %d)\n", dx * sign_x, dy * sign_y, s_mouse_x, s_mouse_y);
    } else if (tkl_strncmp(cmd, "sens", 4) == 0) {
        const char *p = cmd + 4;
        while (*p == ' ') p++;
        if (*p >= '0' && *p <= '9') {
            int pct = 0;
            while (*p >= '0' && *p <= '9') { pct = pct * 10 + (*p - '0'); p++; }
            if (pct < 1) pct = 1;
            if (pct > 400) pct = 400;
            /* LAW 13e: this writes the same variable the Settings panel's radios
             * and the launcher's row write, so `sens 38` and a dial showing
             * 96/256 are one setting seen from two hands rather than two
             * settings that can disagree.  The leftover belongs to the scale
             * that made it. */
            g_ptr_gain_fp = (int32_t)pct * 256 / 100;
            s_ptr_gain_seen = g_ptr_gain_fp;
            ps2_ptr_gain_install(0);
        }
        ps2_kprintf("[PS2] Pointer scale %d%%, %s source, gain from %s: ",
                    (int)(s_ptr_mult_fp * 100 / 256), ps2_ptr_src_name(s_ptr_src),
                    g_ptr_gain_fp > 0 ? "the dial" : "the profile default");
        ps2_ptr_gain_row();
    } else if (tkl_strncmp(cmd, "maxstep", 7) == 0) {
        const char *p = cmd + 7;
        while (*p == ' ') p++;
        if (*p >= '0' && *p <= '9') {
            int px = 0;
            while (*p >= '0' && *p <= '9') { px = px * 10 + (*p - '0'); p++; }
            /* Nothing bounds this from below except 0 itself, and 0 is the word
             * for "no cap" rather than for "frozen": dp_ptr_limit takes a
             * non-positive step as pass-through, which is how the limiter gets
             * tested off rather than guessed at. */
            if (px > 512) px = 512;
            s_ptr_max_step = (int32_t)px;
            /* A cap being lifted or lowered leaves no reason to keep the old
             * shape's distance waiting: that distance is why the cursor keeps
             * going after the hand has stopped. */
            s_ptr_want_x = s_ptr_want_y = 0;
            s_ptr_defer_x = s_ptr_defer_y = 0;
        }
        ps2_kprintf("[PS2] Pointer cap %d px per INPUT period%s\n",
                    (int)s_ptr_max_step, s_ptr_max_step > 0 ? "" : " (off)");
    } else if (tkl_strncmp(cmd, "ptrsrc", 6) == 0) {
        const char *p = cmd + 6;
        while (*p == ' ') p++;
        /* The choice is normally made from the device's own USB identity, so this
         * exists to overrule it in one direction that cannot be observed any
         * other way: running the emulator's stub against a hardware-shaped count
         * stream, or the other way round, in a single session. */
        if (tkl_strncmp(p, "hw", 2) == 0) {
            s_ptr_src_forced = 1;
            s_ptr_src = PS2_PTRSRC_HW;
            ps2_ptr_src_apply("forced");
        } else if (tkl_strncmp(p, "emu", 3) == 0) {
            s_ptr_src_forced = 1;
            s_ptr_src = PS2_PTRSRC_EMU;
            ps2_ptr_src_apply("forced");
        } else if (tkl_strncmp(p, "auto", 4) == 0) {
            s_ptr_src_forced = 0;
            ps2_ptr_src_detect();
        } else {
            /* Asking the question also answers it in the unit the hand is using,
             * because this is the row to read while moving the mouse. */
            ps2_kprintf("[PS2] Pointer source %s (%s): ", ps2_ptr_src_name(s_ptr_src),
                        s_ptr_src_forced ? "forced" : "detected");
            ps2_ptr_gain_row();
        }
    } else if (tkl_strncmp(cmd, "ptrstat", 7) == 0) {
        const char *p = cmd + 7;
        while (*p == ' ') p++;
        /* Print the window that just closed, then start a new one.  So the first
         * call of a session reports the warm-up rather than the slide: close a
         * throwaway window first (`ptrstat boot`), move the mouse, then label the
         * window that contains the move.
         *
         * ptrcal drives the same seam with synthetic reports, so it must not run
         * inside a measurement window; each ptrstat resets, so one keystroke
         * recovers from forgetting. */
        ps2_ptrst_print(*p ? p : "run");
        /* Same window, one question the count rows cannot answer: the counts say
         * what the hand sent, these say when the screen got it. */
        ps2_paint_print();
    } else if (tkl_strncmp(cmd, "ptgain", 6) == 0) {
        const char *p = cmd + 6;
        int hostpx = -1, pct = -1;   /* -1 = not given, 0 = the explicit reset */

        /* Two optional numbers because the measurement and the decision are two
         * different acts: the first call reads out what the sweep was worth, the
         * second says what the pointer should feel like. */
        while (*p == ' ') p++;
        if (*p >= '0' && *p <= '9') {
            hostpx = 0;
            while (*p >= '0' && *p <= '9') { hostpx = hostpx * 10 + (*p - '0'); p++; }
        }
        while (*p == ' ') p++;
        if (*p >= '0' && *p <= '9') {
            pct = 0;
            while (*p >= '0' && *p <= '9') { pct = pct * 10 + (*p - '0'); p++; }
        }
        ps2_ptg_cal(hostpx, pct);
    } else if (tkl_strcmp(cmd, "ptrcal") == 0) {
        int32_t saved_step = s_ptr_max_step;
        /* The transfer function belongs to the live source stub, and the two give
         * different answers to the same synthetic counts by design, so a [CAL]
         * log copied out of a session is incomplete without the name on it.
         *
         * `curve=` stays in the header even though LAW 13d makes its answer a
         * constant: a [CAL] gain row that is non-linear is now a fault rather
         * than a policy, and the row that lets a reader tell the two apart is
         * this one.  It used to print `riscos` and a step number here. */
        ps2_kprintf("[CAL] Pointer loopback: counts in, pixels out, no hand involved.\n");
        ps2_kprintf("[CAL] source=%s curve=none (LAW 13d) gain=%d/256\n",
                    ps2_ptr_src_name(s_ptr_src), (int)s_ptr_mult_fp);
        ps2_kprintf("[CAL] tag     amp     px  stray counts    EDGEx: wall counts back short\n");
        /* The cap is a policy about how fast a person may be moved, and dp_cal is
         * a measurement of how a count becomes a pixel.  Left in place it would
         * hold back the high-amplitude gain rows and the EDGE strokes, so the run
         * would report the limiter as a broken mapping.  Off for the measurement,
         * back on afterwards. */
        s_ptr_max_step = 0;
        s_ptr_want_x = s_ptr_want_y = 0;
        s_ptr_defer_x = s_ptr_defer_y = 0;
        (void)dp_cal_run(&s_ptr_cal_port);
        s_ptr_max_step = saved_step;
        /* The gain rows are this port's own transfer function, so they read the
         * same however the mouse was moving; a stray on the axis nobody asked
         * for is an axis bug rather than a setting to turn. */
        ps2_kprintf("[CAL] scale=%d%% px per count, cap=%d px per ms. DRIFT is where the\n"
                    "[CAL] pointer ended after equal and opposite counts; DIAG asks both axes\n"
                    "[CAL] the same question.\n",
                    (int)(s_ptr_mult_fp * 100 / 256), (int)s_ptr_max_step);
    } else if (tkl_strcmp(cmd, "gsstat") == 0) {
        ps2_shell_gsstat();
    } else if (tkl_strncmp(cmd, "page", 4) == 0) {
        const char *p = cmd + 4;
        while (*p == ' ') p++;
        ps2_shell_page(p);
#if BTRON_PS2_PAINT_WORKER
    } else if (tkl_strncmp(cmd, "paintw", 6) == 0) {
        const char *p = cmd + 6;
        while (*p == ' ') p++;
        ps2_shell_paintw(p);
#endif
    } else if (tkl_strcmp(cmd, "gsquad") == 0) {
        if (s_gui_active) {
            ps2_kprintf("[GSQUAD] note: the desktop composites over the canvas every frame, so from\n"
                        "[GSQUAD] a GUI session these blocks live one repaint. Ctrl+Q to Stage 1 for a clean answer.\n");
        }
        ps2_gsquad_probe();
    } else if (tkl_strcmp(cmd, "gsread") == 0) {
        if (s_gui_active) {
            /* Worth saying in full here, because a GUI session does not merely repaint the blocks: the
             * desktop's own present keeps posting to the ring, and ps2_gs_vram_read() refuses while
             * the ring has anything queued -- so from a GUI session this verb's honest output is a
             * bus-busy row, which is a pipelining fact and not a picture. */
            ps2_kprintf("[GSREAD] note: the desktop presents every frame, so its uploads erase these\n"
                        "[GSREAD] blocks and its ring traffic makes a read refuse. Ctrl+Q to Stage 1.\n");
        }
        ps2_gsread_probe();
    } else if (tkl_strcmp(cmd, "gsalpha") == 0) {
        if (s_gui_active) {
            ps2_kprintf("[GSALPHA] note: the desktop presents every frame, so its uploads erase these\n"
                        "[GSALPHA] boxes and its ring traffic makes a read refuse. Ctrl+Q to Stage 1.\n");
        }
        ps2_gsalpha_probe();
    } else if (tkl_strcmp(cmd, "gstex") == 0) {
        if (s_gui_active) {
            ps2_kprintf("[GSTEX] note: the desktop presents every frame, so its uploads erase these\n"
                        "[GSTEX] boxes and its ring traffic makes a read refuse. Ctrl+Q to Stage 1.\n");
        }
        ps2_gstex_probe();
    } else if (tkl_strcmp(cmd, "click") == 0 || tkl_strcmp(cmd, "click 1") == 0) {
        ps2_click_mouse(1, 1);
        ps2_click_mouse(1, 0);
    } else if (tkl_strcmp(cmd, "click 2") == 0) {
        ps2_click_mouse(2, 1);
        ps2_click_mouse(2, 0);
    } else if (tkl_strncmp(cmd, "key ", 4) == 0) {
        char ch = cmd[4];
        ps2_inject_key((UW)(uint8_t)ch, 1);
        ps2_inject_key((UW)(uint8_t)ch, 0);
        ps2_kprintf("[PS2-INPUT] Injected key: '%c' (0x%02X)\n", ch, (uint8_t)ch);
    } else if (tkl_strncmp(cmd, "type ", 5) == 0) {
        const char *p = cmd + 5;
        while (*p) {
            ps2_inject_key((UW)(uint8_t)*p, 1);
            ps2_inject_key((UW)(uint8_t)*p, 0);
            p++;
        }
        ps2_kprintf("[PS2-INPUT] Typed string into active window\n");
    } else if (tkl_strncmp(cmd, "pad ", 4) == 0) {
        uint16_t btns = 0xFFFF;
        uint32_t val = 0;
        const char *p = cmd + 4;
        while (*p == ' ') p++;
        while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) {
            int d = (*p >= '0' && *p <= '9') ? (*p - '0') :
                    (*p >= 'a' && *p <= 'f') ? (*p - 'a' + 10) : (*p - 'A' + 10);
            val = (val << 4) | d;
            p++;
        }
        if (val != 0) btns = (uint16_t)val;
        int lx = 128, ly = 128;
        while (*p == ' ') p++;
        if (*p >= '0' && *p <= '9') {
            lx = 0;
            while (*p >= '0' && *p <= '9') { lx = lx * 10 + (*p - '0'); p++; }
        }
        while (*p == ' ') p++;
        if (*p >= '0' && *p <= '9') {
            ly = 0;
            while (*p >= '0' && *p <= '9') { ly = ly * 10 + (*p - '0'); p++; }
        }
        ps2_pad_set_state(btns, (uint8_t)lx, (uint8_t)ly, 128, 128);
        ps2_pad_poll();
        ps2_kprintf("[PS2-PAD] Pad state set: btns=0x%04X lx=%d ly=%d\n", btns, lx, ly);
    } else if (tkl_strcmp(cmd, "reboot") == 0) {
        ps2_kprintf("[PS2] System halting...\n");
        ps2_delay_cycles(100000);
        ps2_halt();
    } else if (cmd[0] != '\0') {
        ps2_kprintf("Unknown command: '%s'. Type 'help' for commands.\n", cmd);
    }
    ps2_kprintf(s_gui_active ? "btron-ps2> " : "btron-ps2# ");
}

static void ps2_shell_poll(void)
{
    while (ps2_sio_has_char()) {
        int c = ps2_sio_getc();
        if (c < 0) break;
        ps2_shell_char(c);
    }
}

/* One character of console input, whichever device it came from: the SIO ring
 * hands over host-terminal bytes, the USB HID decoder hands over keys it has
 * already resolved to a character.  The escape state machine is deliberately
 * left out here rather than in the SIO loop, because a terminal's arrow key
 * arrives as three separate bytes and needs the same memory between them
 * whoever is reading. */
static void ps2_shell_char(int c)
{
    /* ANSI Escape Sequence State Machine for Navigation Keys */
    if (esc_state == 0) {
        if (c == 0x1B) { /* ESC */
            esc_state = 1;
            return;
        }
    } else if (esc_state == 1) {
        if (c == '[') {
            esc_state = 2;
            return;
        } else {
            esc_state = 0;
        }
    } else if (esc_state == 2) {
        if (c == 'A') {      /* Up Arrow */
            esc_state = 0;
            ps2_move_mouse(0, -16);
            ps2_inject_key(BTRON_KEY_UP, 1);
            ps2_inject_key(BTRON_KEY_UP, 0);
            return;
        } else if (c == 'B') { /* Down Arrow */
            esc_state = 0;
            ps2_move_mouse(0, 16);
            ps2_inject_key(BTRON_KEY_DOWN, 1);
            ps2_inject_key(BTRON_KEY_DOWN, 0);
            return;
        } else if (c == 'C') { /* Right Arrow */
            esc_state = 0;
            ps2_move_mouse(16, 0);
            ps2_inject_key(BTRON_KEY_RIGHT, 1);
            ps2_inject_key(BTRON_KEY_RIGHT, 0);
            return;
        } else if (c == 'D') { /* Left Arrow */
            esc_state = 0;
            ps2_move_mouse(-16, 0);
            ps2_inject_key(BTRON_KEY_LEFT, 1);
            ps2_inject_key(BTRON_KEY_LEFT, 0);
            return;
        } else if (c == 'H') { /* Home */
            esc_state = 0;
            ps2_inject_key(BTRON_KEY_HOME, 1);
            ps2_inject_key(BTRON_KEY_HOME, 0);
            return;
        } else if (c == 'F') { /* End */
            esc_state = 0;
            ps2_inject_key(BTRON_KEY_END, 1);
            ps2_inject_key(BTRON_KEY_END, 0);
            return;
        } else if (c >= '0' && c <= '9') {
            esc_num = c - '0';
            esc_state = 3;
            return;
        } else {
            esc_state = 0;
        }
    } else if (esc_state == 3) {
        esc_state = 0;
        if (c == '~') {
            if (esc_num == 1) {
                ps2_inject_key(BTRON_KEY_HOME, 1);
                ps2_inject_key(BTRON_KEY_HOME, 0);
            } else if (esc_num == 3) {
                ps2_inject_key(BTRON_KEY_DELETE, 1);
                ps2_inject_key(BTRON_KEY_DELETE, 0);
            } else if (esc_num == 4) {
                ps2_inject_key(BTRON_KEY_END, 1);
                ps2_inject_key(BTRON_KEY_END, 0);
            } else if (esc_num == 5) {
                ps2_inject_key(BTRON_KEY_PAGE_UP, 1);
                ps2_inject_key(BTRON_KEY_PAGE_UP, 0);
            } else if (esc_num == 6) {
                ps2_inject_key(BTRON_KEY_PAGE_DOWN, 1);
                ps2_inject_key(BTRON_KEY_PAGE_DOWN, 0);
            }
        }
        return;
    }

    /* Standard character and line editing */
    if (c == '\r' || c == '\n') {
        ps2_console_putc('\n');
        if (!s_gui_active) ps2_gs_text_flush();
        cmd_buf[cmd_pos] = '\0';
        ps2_shell_exec(cmd_buf);
        cmd_pos = 0;
    } else if (c == 0x08 || c == 0x7F) {
        if (cmd_pos > 0) {
            cmd_pos--;
            ps2_console_putc('\b');
            if (!s_gui_active) ps2_gs_text_flush();
        }
        if (s_gui_active) {
            ps2_inject_key(BTRON_KEY_BACKSPACE, 1);
            ps2_inject_key(BTRON_KEY_BACKSPACE, 0);
        }
    } else if (c >= 0x20 && c <= 0x7E) {
        if (s_gui_active) {
            ps2_inject_key((UW)(uint8_t)c, 1);
            ps2_inject_key((UW)(uint8_t)c, 0);
        }
        if (cmd_pos < (int)sizeof(cmd_buf) - 1) {
            cmd_buf[cmd_pos++] = (char)c;
            ps2_console_putc((char)c);
            if (!s_gui_active) ps2_gs_text_flush();
        }
    }
}

/* ── Platform Query & RTOS Services ─────────────────────────────── */

void btron_core_banner(void) {
    ps2_kprintf("[CORE] B-System / BTRON3 3.20  Sony PlayStation 2  [Emotion Engine R5900, 32 MB RDRAM]\n");
    ps2_kprintf("[CORE] Cleanroom TRON kernel, Stage 1 terminal console.  Display: GS 800x600 CT32\n");
    ps2_kprintf("[CORE] Commands: help, usb, info, res, mem, status, tip, apps, type, pad, clear\n");
    ps2_kprintf("[CORE] Workbench GUI is not autobooted: type 'startx' for the 800x600 desktop.\n");
}

void btron_core_mem_log(void) {
    ps2_kprintf("[MEM] 32 MB RDRAM: 0x00000000-0x01ffffff  GS regs 0x12000000  ohci 0x1f801600\n");
    ps2_kprintf("[MEM] %u MB kernel heap pool, %u bytes used (%u kB peak, largest hole %u kB)  canvas 0x%08x (uncached alias 0x%08x)\n",
                (unsigned int)(PS2_HEAP_SIZE / (1024u * 1024u)),
                (unsigned int)s_heap_used,
                (unsigned int)(s_heap_peak / 1024u),
                (unsigned int)(ps2_heap_largest_free() / 1024u),
                (unsigned)(uintptr_t)s_desktop_backbuffer,
                (unsigned)(((uintptr_t)s_desktop_backbuffer) | 0x20000000u));
}

/* The /SYS volume's bytes, carried into .data by src/drivers/ps2/ps2_sys_vol.s.  They are
 * btron_sys.vol as `mkbtronfs` writes it from src/tools/manifest.txt -- the same image the hosted
 * targets mount and `make test-fs` reads -- so this file describes no record, no name and no size
 * of its own, and the two symbols below are the only knowledge of it the kernel has: an address and
 * a length.  The length is taken from the labels rather than from a `.set` absolute symbol because
 * an absolute symbol the linker is free to place elsewhere is not a fact about this buffer. */
extern unsigned char ps2_sys_vol_image[];
extern unsigned char ps2_sys_vol_image_end[];

void btron_core_hfds_log(void) {
    const long img_bytes = ps2_sys_vol_image_end - ps2_sys_vol_image;
    BlkDev *dev;

    if (!g_sys_vol) {
        /* Writable, because .data is RDRAM once the loader has put it there: `mkf` and `vol_sync`
         * write into the copy the image arrived in, which needs no second buffer and buys no
         * persistence -- the next boot re-extracts what the disc carried. */
        dev = blk_mem_create(ps2_sys_vol_image, (UW)img_bytes, 0);
        if (!dev) {
            ps2_kprintf("[FS  ] /SYS NOT mounted: blk_mem_create wants at least one %u-byte block and the image holds %ld\n",
                        (unsigned int)BTRON_BLOCK_SIZE, img_bytes);
            return;
        }
        g_sys_vol = vol_mount(dev);
        if (!g_sys_vol) {
            blk_destroy(dev);
            ps2_kprintf("[FS  ] /SYS NOT mounted: vol_mount rejected the header of the %ld-byte image in the ELF\n",
                        img_bytes);
            return;
        }
    }

    /* Read back off the mount and not off the build: every number in this row is what the volume
     * says it is, so the row cannot drift from the image the way a hand-written list of record
     * names did. */
    ps2_kprintf("[HFDS] /SYS mounted on the volume inside the image: \"%s\"  %u kB of %u-byte blocks, %u used of %u, %u records max\n",
                vol_name(g_sys_vol),
                (unsigned int)(vol_total_blocks(g_sys_vol) * vol_block_size(g_sys_vol) / 1024u),
                (unsigned int)vol_block_size(g_sys_vol),
                (unsigned int)(vol_total_blocks(g_sys_vol) - vol_free_blocks(g_sys_vol)),
                (unsigned int)vol_total_blocks(g_sys_vol),
                (unsigned int)vol_nfmax(g_sys_vol));
}

void btron_core_init(void) {
    ps2_kprintf("[CORE] Cleanroom uITRON 3.0 / BTRON 3.20 Engine (ps2-pcsx2)\n");
}

void btron_core_print_ver(ShellOutputFn out_fn, void *user_data, const char *arg) {
    if (!out_fn) return;
    if (arg && tkl_strcmp(arg, "-a") == 0) {
        out_fn("BTRON3 btron-ps2 3.20 (ps2-pcsx2) Emotion Engine MIPS R5900", COLOR_CYAN, user_data);
    } else if (arg && (tkl_strcmp(arg, "-r") == 0 || tkl_strcmp(arg, "-v") == 0)) {
        out_fn("3.20.0-ps2-pcsx2", COLOR_CYAN, user_data);
    } else {
        out_fn("B-System 3.0 Workstation System (BTRON3 Specification 3.20)", COLOR_CYAN, user_data);
        out_fn("Kernel: Cleanroom uITRON 3.0 / BTRON 3.20 (Emotion Engine R5900)", COLOR_GREEN, user_data);
        out_fn("Hardware Target: Sony PlayStation 2 (GS eDRAM, DualShock 2, OHCI USB)", COLOR_LTGRAY, user_data);
        out_fn("Build Timestamp: " __DATE__ " " __TIME__, COLOR_LTGRAY, user_data);
        out_fn("Display Compositor: GIF DMA Host->Local Blitter (800x600 32-bpp CT32)", COLOR_LTGRAY, user_data);
        out_fn("Japanese IME: B-System Mozc / TIP Kana-Kanji Conversion Subsystem", COLOR_LTGRAY, user_data);
    }
}

ER slp_tsk(void) {
    return E_OK;
}

ER wup_tsk(ID tskid) {
    (void)tskid;
    return E_OK;
}


#if BTRON_PS2_BENCH
/* ── Automated Present & Pointer Bench ────────────────────────────── */

/* Why this exists instead of another protocol for a hand: the present used to
 * repaint a full-width band of rows, and a band of that shape is not symmetric
 * between the two directions -- vertical travel adds rows, because the sprite's old
 * rows and its new ones part company, while horizontal travel never does.  Whether
 * that was worth fixing by presenting a rect instead is a question about what 800x16
 * costs against 32x16 on this hardware, including the per-row GIF setup a narrow
 * region pays (ps2_gs_upload() has to issue one transfer per row as soon as the
 * width is not the canvas's).  Only the machine can answer that, so this asks it, in
 * a build that needs no window, no keyboard and no protocol: make ps2 BENCH=1.
 *
 * And it did answer, on 2026-10-08: 800x16 = 5370 us against 16x16 = 528 us and
 * 80x16 = 1033 us, with upload -- the term the per-row setup lives in -- at 45 us
 * against 10 us.  The narrow region is an order of magnitude cheaper even paying
 * one setup per row, so the bands became rectangles.  This stays for the same
 * reason the [PSTAT] axis split stays: the shape is now the assumption, and only
 * the machine says when it stops holding. */

/* One present of one region, repeated, timed by stage and against the wall.  The
 * wall figure is the honest one, since it covers whatever the three stages do not
 * time, and the first repetition stays inside the average on purpose: a real pass
 * also begins with cold caches, and a table that warmed the region first would be
 * read as favouring it. */
static void ps2_bench_region(GDEV *screen, const char *tag,
                             int x0, int y0, int x1, int y1, int full, uint32_t reps)
{
    uint32_t r_sum = 0u, s_sum = 0u, u_sum = 0u;
    uint32_t r_max = 0u, s_max = 0u, u_max = 0u;
    const uint32_t t_start = ps2_count_read();
    RENDER_STATS st;

    for (uint32_t i = 0; i < reps; i++) {
        uint32_t r, s, u;

        ps2_paint_region(screen, x0, y0, x1, y1, full, &r, &s, &u);
        r_sum += r; s_sum += s; u_sum += u;
        if (r > r_max) r_max = r;
        if (s > s_max) s_max = s;
        if (u > u_max) u_max = u;
    }
    ps2_kprintf("[BENCH] %-9s %4dx%-4d total=%u us  render %u/%u  swap %u/%u  upload %u/%u  (avg/max)\n",
                tag, x1 - x0, y1 - y0,
                (unsigned int)(ps2_us_since(t_start) / reps),
                (unsigned int)(r_sum / reps), (unsigned int)r_max,
                (unsigned int)(s_sum / reps), (unsigned int)s_max,
                (unsigned int)(u_sum / reps), (unsigned int)u_max);
    /* The composite's own split for the same region, because the three stages above
     * are this port's and these are the shared compositor's: `paint` nonzero in a
     * region that only needed the blit is application callbacks being run, and a
     * non-zero `bfn` is the background cache not holding, which is the defect the
     * PS2's bands used to pay on every pass. */
    btron_render_stats_take(&st);
    ps2_kprintf("[BENCH]           compositor: bg=%u(%u px) frame=%u(%u px skip=%u) paint=%u blit=%u(%u px) comp=%u bfn=%u wins=%u/%u\n",
                (unsigned int)st.bg_us, (unsigned int)st.bg_worst_px,
                (unsigned int)st.frame_us, (unsigned int)st.frame_body_px,
                (unsigned int)st.frame_body_skip_px,
                (unsigned int)st.paint_us,
                (unsigned int)st.blit_us, (unsigned int)st.blit_worst_px,
                (unsigned int)st.comp_us, (unsigned int)st.bg_full_calls,
                (unsigned int)st.wins_drawn, (unsigned int)st.wins_walked);
}

/* ── RAM-to-RAM: the loop, not the DRAM ───────────────────────────────────────
 *
 * The half of row 9 that is still open is a choice between two lanes -- shave the
 * EE's own pixel loops, or hand the pixels to the GS.  Until 2026-10-13 that choice
 * was priced as if one side of it were already shut.  The book's row billed a pixel
 * copied RAM to RAM at 14.8 CYCLES -- 50.2 ns -- and its argument was that `blit=`
 * comes out at that same price from 768 px to 613,440 px, three orders of magnitude
 * of size at one price, so the pixel must be paying for the DRAM rather than for the
 * loop.  That reading was never falsifiable: a byte-per-iteration tail is size-flat
 * as well, so flatness cannot tell a bandwidth from a per-iteration cost, and nothing
 * had ever moved the same bytes at two different instruction costs.
 *
 * This phase is that control, and the answer came back the other way -- the loop, and
 * specifically a scan in front of it.  What the old flat row was pricing is the
 * compositor's opacity pass, not its copy: `blit=` bills 39.8 ns/px, of which 15.7 is
 * the copy at its floor and ~24 is a SECOND traversal of the same bytes.  Both halves
 * of that decomposition are now measured rather than subtracted -- `blit scan` 39.86
 * against `copy even` 15.96, and `blit wide`, which makes the same decision two pixels
 * per load, at 36.57.  The book keeps the arithmetic in 10.1.
 *
 * This port's copy used to make exactly that difference by accident.  `btron_row_blit()`
 * took its 8-byte loop only when BOTH pointers were 8-aligned, and a damage rect starts
 * at an arbitrary pixel column, so an odd `left` left every row of that rectangle
 * 4-mod-8 and the whole of it went through the byte loop: same bytes, several times the
 * instructions.  The aarch64 half of that header has always known this column is worth
 * peeling to the shared phase -- its own comment says a present became one bus
 * transaction per byte without the peel -- and the generic half, which is the half the
 * PS2 compiles, now peels too and takes 32 bytes at a time.  So the table below prices a
 * FIXED defect, not a live one: `copy even` and `copy odd` are the shipped header, and
 * the rungs above it are the shapes it replaced, kept so that a landing's price stays
 * auditable against the rows that bought it.
 *
 * So the machine is asked three ways, because the three answers together decide the
 * lane and no one of them does:
 *
 *   copy even vs copy odd      -- if the two differ, the price is instructions and
 *                                the peel is money lying on the table; if they are
 *                                equal, no EE-side shave can help and the GS is the
 *                                only lane left.
 *   copy odd vs copy odd+peel  -- the prospective fix priced before it is landed, in
 *                                the same run as the defect it fixes, so the saving
 *                                is not an estimate smuggled into a commit.
 *   fill vs copy, same rect    -- a copy is one load and one store per pixel and a
 *                                fill is one store.  If the store alone costs what
 *                                the pair costs, accesses are serializing and the
 *                                load is free, which is the bus's signature; if a
 *                                fill is worth about half a copy, both are issue
 *                                slots, which is the loop's.  `fill even` and
 *                                `fill odd` are the control on the control: stores
 *                                are 4-aligned at any pixel column, so the two must
 *                                agree, and if they do not something else is moving
 *                                between the rows.
 *   the size sweep             -- the book's own claim, tested at the phase that can
 *                                never fall into the tail, over the range it quotes
 *                                and out to the canvas's whole 480,000 px, which is
 *                                what `bg=` bills per frame (as a copy of the cached
 *                                background, per desktop.c's
 *                                render_desktop_background_rect(), and what `frame=`
 *                                bills as fill_rec() plates).
 *
 * Copies go through btron_row_blit() one row at a time, the shape
 * btron_row_copy_twopass() calls it in, and fills through fill_rec(), the compositor's
 * hottest leaf.  Both bands are the canvas's own rows -- src above, dst below, the
 * same phase relationship production has -- so the largest disjoint copy is
 * 300 rows x 800 px = 240,000 px, and the sweep stops there and says so.  A
 * checksum is folded out of the destination after each shape and printed, because
 * that is what proves the loop ran on memory the machine can see rather than being
 * deleted as dead, and that `copy odd` moved the bytes `copy even` moved -- and for
 * a copy the same fold of the SOURCE band is printed beside it as chk=, because a row
 * whose destination does not equal its source is not the shape its tag claims.
 *
 * This phase owns nothing the next one needs: it scribbles the canvas the way the
 * region table above it already does, and every phase below repaints.
 *
 * And the machine answered, on 2026-10-13, in a ladder of loop shapes on the same
 * 399x16 band: 96.4 ns/px through the byte tail, 21.2 peeled to the shared 8-byte
 * phase and taken 8 bytes per iteration, 16.9 at 16, 16.1 at 32.  Its aligned twin
 * cost 27.8 at the body width this header used to carry, and it is the same loop as
 * the peeled one 1.31 times the price, because clang inlines the header's body into
 * the compositor's row loop with two `addu`s per iteration instead of the two
 * `addiu`s it compiles when the body stands in a function of its own: 11 instructions
 * per 8 bytes against 9.  Fills, which cannot be misaligned because they store 4-byte
 * pixels, cost 18.34 on a strip and 17.72 over a 12,800 px band; this table's own
 * leaf, with the row pointer incremented instead of recomputed, priced 10.40 four
 * stores deep and 9.14 eight deep.  Read as instruction counts per pixel -- 20 for
 * the tail, 4 at 8 bytes, 3 at 16, 2.4 at 32 -- the first three steps land at the
 * same 4.7 ns an instruction costs here and a fixed 2.4 ns a pixel, and the fourth
 * step is 2.5 ns/px smaller than that model allows.  So the ladder is real and it has
 * a floor: this machine stops charging a copy at less than ~16 ns/px and a fill at
 * less than ~8.7, and the last measured gap between them, 16.1 against 9.5 once both
 * leaves are fixed, is one load per pixel at 6.6 ns -- 1.4 instructions' worth, not
 * the 2x a serializing bus would make it.  Every figure that decided a landing is a
 * different instruction count per pixel at one price per instruction, which is what
 * an issue-bound machine looks like until it stops being one.  The book's "it is the
 * DRAM, not the loop" was falsified at the only price that mattered -- the tail -- and
 * the two leaves were rewritten from these rows.
 *
 * Both landings were then re-measured from the image that carries them, on
 * 2026-10-13 again with PS2_GL=eegs PS2_FP=hw, and the floors held: `copy even` 15.96
 * and `copy odd` 16.17 on the 400x16 band, the sweep flat at 15.65 / 15.68 / 15.69
 * from 12,800 px through 80,000 px to the largest copy this canvas can hold disjointly
 * at 240,000 px, and `fill` at 8.76 / 8.80 / 8.79 over the same sizes out to the whole
 * 480,000 px.  The byte tail that opened the ladder at 96.4 is no longer a row in the
 * table -- the peel shipped, so getting that rung back means un-landing the fix -- and
 * it stays quoted above as the rung whose price decided the header.  The one figure
 * that did NOT carry over is the old production price itself: the compositor's `blit=`
 * column read 30,755 us over 613,440 px -- the `[BENCH] compositor:` whole-canvas row
 * of the build that measured this phase but did not yet carry either leaf, 50.2 ns/px --
 * and the
 * same column of the same walk reads 24,426 us over the same 613,440 px, 39.8 ns/px,
 * in the run that has both leaves landed.  That is the row the book's flat-price
 * argument was built on, moving by 20.6% while its pixel count did not move at all,
 * which is what a loop's signature looks like on a machine that was said to be out of
 * bandwidth.
 *
 * How the top of that ladder was nearly lost, recorded because it is the reason the
 * chk= field exists: the dispatcher in ps2_bench_ram() read `mode >= 2` where it meant
 * `mode == 2`, so the 16- and 32-byte copy rungs fell into the fill branch and printed
 * fill_rec()'s price under a copy tag -- the same 0xf9f6d730 the fill rows print, three
 * rows in a row, in every one of the first four runs.  Nothing in the log said so; the
 * fold disagreed and nobody was reading it.  The predicate is now the mode it names, a
 * copy row folds its source band the same way it folds its destination and the row
 * prints whether the two agree, and the two widths were then re-run interleaved at 256
 * repetitions because the gap being read is 5% while the drift between adjacent rows is
 * about 1%: 16.94 and 16.93 against 16.13 and 16.14, with the repeat pair after the
 * other width has run, so a width that wins only in the first slot shows up as one.
 * 32 bytes went into the header on the strength of that and of `copy 12800px` falling
 * from 16.53 to 15.58 -- the size `bg=` actually bills -- and eight stores went into
 * fill_rec() on 9.14 against 10.40, which fill_rec() then matched: 9.31 on a strip,
 * 8.74 at 12,800 px, 8.76 over the whole 480,000 px canvas.  What the compositor bills
 * for a whole-canvas present is 41,774 us against the 56,948 the same table printed
 * before any of this, and against 44,162 with only the first rung of each landed.
 * These rows stay for the reason they went in: the floors at 16 and 8.7 are now the
 * assumption, and only the machine says when they stop holding. */

/* The 8-byte rung of the fix, kept as the step two below what shipped: peel the row
 * byte-wise to the next 8-byte boundary when the two pointers share a phase -- which
 * they always do here, two row-aligned buffers addressed at the same rect offset --
 * and take 8 bytes per iteration.  It is the middle of the ladder, and the reason the
 * ladder is printed rather than assumed: this rung measures 21.2 ns/px, the header's
 * 16-byte form 16.9 and its shipped 32-byte form 16.1, so the price of a pixel here
 * is a curve that flattens, and where it flattens is a measurement and not a guess. */
static void ps2_bench_row_blit_peeled(void *dst, const void *src, size_t bytes)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    if ((((uintptr_t)d ^ (uintptr_t)s) & 7u) == 0u) {
        size_t peel = (8u - ((uintptr_t)d & 7u)) & 7u;
        if (peel > bytes) peel = bytes;
        for (size_t i = 0; i < peel; i++) d[i] = s[i];
        d += peel; s += peel; bytes -= peel;
    }
    while (bytes >= 8u) {
        *(uint64_t *)d = *(const uint64_t *)s;
        d += 8; s += 8; bytes -= 8u;
    }
    while (bytes--) *d++ = *s++;
}

/* The same peel with the body taken 16 or 32 bytes per iteration, because the
 * first run of this phase (2026-10-13) found that loop shape, not just loop
 * alignment, is what the pixel is charged for: the header's 8-byte loop costs the
 * machine 11 instructions per iteration when clang inlines it into the
 * compositor's row loop and 9 when it sits in a function of its own, and those two
 * forms move identical bytes at 27.7 and 21.2 ns/px.  A width is therefore not
 * something to reason about; it is something to time. */
static void ps2_bench_row_blit_wide(void *dst, const void *src, size_t bytes, int per32)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    if ((((uintptr_t)d ^ (uintptr_t)s) & 7u) == 0u) {
        size_t peel = (8u - ((uintptr_t)d & 7u)) & 7u;
        if (peel > bytes) peel = bytes;
        for (size_t i = 0; i < peel; i++) d[i] = s[i];
        d += peel; s += peel; bytes -= peel;
    }
    if (per32) {
        while (bytes >= 32u) {
            *(uint64_t *)d = *(const uint64_t *)s;
            *(uint64_t *)(d + 8u) = *(const uint64_t *)(s + 8u);
            *(uint64_t *)(d + 16u) = *(const uint64_t *)(s + 16u);
            *(uint64_t *)(d + 24u) = *(const uint64_t *)(s + 24u);
            d += 32; s += 32; bytes -= 32u;
        }
    } else {
        while (bytes >= 16u) {
            *(uint64_t *)d = *(const uint64_t *)s;
            *(uint64_t *)(d + 8u) = *(const uint64_t *)(s + 8u);
            d += 16; s += 16; bytes -= 16u;
        }
    }
    while (bytes >= 8u) {
        *(uint64_t *)d = *(const uint64_t *)s;
        d += 8; s += 8; bytes -= 8u;
    }
    while (bytes--) *d++ = *s++;
}

/* fill_rec()'s leaf, with its two known instruction costs taken out: the row
 * address, which it recomputes as y*width+left every row (one mult per row that
 * the row pointer could be incremented instead), and one volatile store per pixel
 * inside a loop that pays a compare and a branch with it.  Same canvas, same
 * volatile stores, four per iteration -- or eight, when `per8` asks the question
 * the 32-byte copy rung asks on the other side: whether the loop's overhead is
 * still being paid at the width that shipped. */
static void ps2_bench_fill_rows(GDEV *screen, int left, int band, int w, int h, int per8)
{
    volatile COLOR *row = (volatile COLOR *)screen->pixels
                          + (size_t)band * (size_t)screen->width + (size_t)left;
    const COLOR fbcol = BTRON_PIXEL_FB(COLOR_LTGRAY);
    const size_t stride = (size_t)screen->width;

    for (int y = 0; y < h; y++, row += stride) {
        int x = 0;
        if (per8) {
            for (; x + 8 <= w; x += 8) {
                row[x] = fbcol; row[x + 1] = fbcol;
                row[x + 2] = fbcol; row[x + 3] = fbcol;
                row[x + 4] = fbcol; row[x + 5] = fbcol;
                row[x + 6] = fbcol; row[x + 7] = fbcol;
            }
        }
        for (; x + 4 <= w; x += 4) {
            row[x] = fbcol; row[x + 1] = fbcol;
            row[x + 2] = fbcol; row[x + 3] = fbcol;
        }
        for (; x < w; x++) row[x] = fbcol;
    }
}

/* The decision wnd.c's client-area walk takes before it copies a row: read every
 * pixel, and bail at the first transparent one, because a transparent pixel must
 * leave whatever is below it on the canvas rather than be copied over it.  This is
 * the shape production's blit= column is in, and it is a SECOND pass over the same
 * bytes the copy is about to make -- which is what the copy rows above do not model,
 * leaving 15.7 ns/px priced against the compositor's 39.9. */
static BOOL ps2_bench_span_scan(const COLOR *src, int span)
{
    for (int cx = 0; cx < span; cx++) {
        if (src[cx] == 0x00000000) return FALSE;
    }
    return TRUE;
}

/* The same decision, two pixels per 64-bit load, with the same phase rule as the
 * copy: peel to an even pixel when the row starts at an odd byte phase and take the
 * pairs after it.  A half of the word is transparent exactly when it is zero, so the
 * test is two 32-bit compares and a load per two pixels -- 1.4 instructions a pixel
 * against the scan above's 3, and no change to what the decision means. */
static BOOL ps2_bench_span_scan_wide(const COLOR *src, int span)
{
    const uint8_t *p = (const uint8_t *)src;
    int n = span;

    if (((uintptr_t)p & 7u) != 0u) {
        if (src[0] == 0x00000000) return FALSE;
        src++;
        n--;
    }
    for (int i = 0; i + 1 < n; i += 2) {
        const uint64_t v = *(const uint64_t *)(src + i);
        if ((uint32_t)v == 0u || (uint32_t)(v >> 32) == 0u) return FALSE;
    }
    if (n & 1) return src[n - 1] != 0x00000000;
    return TRUE;
}

/* The fused shapes, and they are the reason LANE 9's `Shave` cell exists.  What the
 * two rows above price is a walk that reads a band TWICE: `blit scan` reads every pixel
 * to decide, then the shipped header reads the same bytes again to copy them.  Taking
 * 3.3 ns/px off the decision by reading two pixels per load (`blit wide`) leaves that
 * second read entirely in place -- 15.96 of the 39.86 is paid twice over.  A fused pass
 * reads once and spends the same load on the decision and on the store, so its ceiling
 * is the copy floor itself and not an opinion: it cannot cost less than the copy it
 * replaces.
 *
 * Every shape here writes exactly what wnd.c's walk writes: a pixel that is not the
 * transparent sentinel lands on the canvas, and one that is leaves whatever was below
 * it.  That is not a claim, it is the structure -- a group that fails its test falls to
 * the per-pixel fixup over its OWN pixels using the words already loaded, which is the
 * shipped else-branch's rule applied to fewer pixels -- and it is what the host
 * differential gate in verify/tests/ has to prove before any of it ships.
 *
 * The shapes differ only in how many pixels one decision covers, and in whether the
 * decision is a compare per word or one arithmetic test per group.  10.1's whole
 * finding is that a LOOP shape is priced rather than reasoned -- 96.4 ns/px through a
 * byte tail against 16.1 at 32 bytes on identical bytes -- so all of them get measured
 * and the fastest one ships, including if the fastest is the narrowest. */
static void ps2_bench_span_fuse2(COLOR *dst, const COLOR *src, int span)
{
    int i = 0;

    /* The same phase rule the copy's peel uses: a damage rect starts at an arbitrary
     * pixel column, so the row is usually 4-mod-8 and the pairs come after the peel. */
    if ((((uintptr_t)src & 7u) != 0u) && span > 0) {
        if (src[0] != 0x00000000) dst[0] = src[0];
        i = 1;
    }
    for (; i + 1 < span; i += 2) {
        const uint64_t v = *(const uint64_t *)(src + i);
        const COLOR a = (COLOR)v, b = (COLOR)(v >> 32);

        if (a != 0x00000000 && b != 0x00000000) {
            *(uint64_t *)(dst + i) = v;
        } else {
            if (a != 0x00000000) dst[i] = a;
            if (b != 0x00000000) dst[i + 1] = b;
        }
    }
    if (i < span && src[i] != 0x00000000) dst[i] = src[i];
}

static void ps2_bench_span_fuse4(COLOR *dst, const COLOR *src, int span)
{
    int i = 0;

    if ((((uintptr_t)src & 7u) != 0u) && span > 0) {
        if (src[0] != 0x00000000) dst[0] = src[0];
        i = 1;
    }
    for (; i + 3 < span; i += 4) {
        const uint64_t v0 = *(const uint64_t *)(src + i);
        const uint64_t v1 = *(const uint64_t *)(src + i + 2);
        const COLOR a = (COLOR)v0, b = (COLOR)(v0 >> 32);
        const COLOR c = (COLOR)v1, d = (COLOR)(v1 >> 32);

        if (a && b && c && d) {
            *(uint64_t *)(dst + i) = v0;
            *(uint64_t *)(dst + i + 2) = v1;
        } else {
            if (a) dst[i] = a;
            if (b) dst[i + 1] = b;
            if (c) dst[i + 2] = c;
            if (d) dst[i + 3] = d;
        }
    }
    for (; i < span; i++) {
        if (src[i] != 0x00000000) dst[i] = src[i];
    }
}

/* Eight pixels per decision, and the decision is one test per WORD accumulated into a
 * single flag, so the clean path is one branch for the group rather than eight. */
static void ps2_bench_span_fuse8(COLOR *dst, const COLOR *src, int span)
{
    int i = 0;

    if ((((uintptr_t)src & 7u) != 0u) && span > 0) {
        if (src[0] != 0x00000000) dst[0] = src[0];
        i = 1;
    }
    for (; i + 7 < span; i += 8) {
        const uint64_t v0 = *(const uint64_t *)(src + i);
        const uint64_t v1 = *(const uint64_t *)(src + i + 2);
        const uint64_t v2 = *(const uint64_t *)(src + i + 4);
        const uint64_t v3 = *(const uint64_t *)(src + i + 6);
        const COLOR w[8] = {
            (COLOR)v0, (COLOR)(v0 >> 32), (COLOR)v1, (COLOR)(v1 >> 32),
            (COLOR)v2, (COLOR)(v2 >> 32), (COLOR)v3, (COLOR)(v3 >> 32)
        };
        int dirty = 0;

        for (int k = 0; k < 8; k++) dirty |= (w[k] == 0x00000000);
        if (!dirty) {
            *(uint64_t *)(dst + i) = v0;
            *(uint64_t *)(dst + i + 2) = v1;
            *(uint64_t *)(dst + i + 4) = v2;
            *(uint64_t *)(dst + i + 6) = v3;
        } else {
            for (int k = 0; k < 8; k++) {
                if (w[k] != 0x00000000) dst[i + k] = w[k];
            }
        }
    }
    for (; i < span; i++) {
        if (src[i] != 0x00000000) dst[i] = src[i];
    }
}

/* The same group as the rung above, decided without any per-word branch at all: for a
 * 32-bit word, `(w - 1) & ~w` has its top bit set EXACTLY when w is zero, because the
 * bits a subtraction flips 0 -> 1 are the run below w's lowest set bit, which can reach
 * bit 31 only when there is no lowest set bit.  So the test is arithmetic, it is exact
 * rather than conservative, and it is the shape that would let the whole group stay a
 * straight line.  Whether EE scalar costs fewer cycles for it than for eight compares
 * is what the row answers -- not the reasoning above, which is only why the rung exists. */
static void ps2_bench_span_fuse8_bits(COLOR *dst, const COLOR *src, int span)
{
    int i = 0;

    if ((((uintptr_t)src & 7u) != 0u) && span > 0) {
        if (src[0] != 0x00000000) dst[0] = src[0];
        i = 1;
    }
    for (; i + 7 < span; i += 8) {
        const uint64_t v0 = *(const uint64_t *)(src + i);
        const uint64_t v1 = *(const uint64_t *)(src + i + 2);
        const uint64_t v2 = *(const uint64_t *)(src + i + 4);
        const uint64_t v3 = *(const uint64_t *)(src + i + 6);
        const uint32_t w[8] = {
            (uint32_t)v0, (uint32_t)(v0 >> 32), (uint32_t)v1, (uint32_t)(v1 >> 32),
            (uint32_t)v2, (uint32_t)(v2 >> 32), (uint32_t)v3, (uint32_t)(v3 >> 32)
        };
        uint32_t zero = 0u;

        for (int k = 0; k < 8; k++) {
            zero |= ((w[k] - 1u) & ~w[k] & 0x80000000u);
        }
        if (zero == 0u) {
            *(uint64_t *)(dst + i) = v0;
            *(uint64_t *)(dst + i + 2) = v1;
            *(uint64_t *)(dst + i + 4) = v2;
            *(uint64_t *)(dst + i + 6) = v3;
        } else {
            for (int k = 0; k < 8; k++) {
                if (w[k] != 0u) dst[i + k] = (COLOR)w[k];
            }
        }
    }
    for (; i < span; i++) {
        if (src[i] != 0x00000000) dst[i] = src[i];
    }
}

/* The rung that would actually SHIP, and it is here because the bench's two bands share
 * a stride while a real client pixmap does not: `wnd->dev->width` can be odd, so a
 * window's row and the canvas's row at the same damage column are not in the same 8-byte
 * phase, and a 64-bit access valid for one is unaligned for the other.  This shape reads
 * and writes one 32-bit word per pixel with the sentinel test inline, so it needs no
 * phase at all -- 4-byte alignment is what a COLOR array always is.  If it loses to
 * `blit wide` it stays printed here as the reason the pair shapes do not ship. */
static void ps2_bench_span_fuse4w(COLOR *dst, const COLOR *src, int span)
{
    int i = 0;

    for (; i + 3 < span; i += 4) {
        const COLOR a = src[i], b = src[i + 1], c = src[i + 2], d = src[i + 3];

        if (a && b && c && d) {
            dst[i] = a;
            dst[i + 1] = b;
            dst[i + 2] = c;
            dst[i + 3] = d;
        } else {
            if (a) dst[i] = a;
            if (b) dst[i + 1] = b;
            if (c) dst[i + 2] = c;
            if (d) dst[i + 3] = d;
        }
    }
    for (; i < span; i++) {
        if (src[i] != 0x00000000) dst[i] = src[i];
    }
}

/* The dirty-content regime's period, read by every row and set by the row list around
 * a block of rows: 0 leaves a copy row reading the desktop's own pixels, and 8 puts the
 * transparent sentinel into one pixel in eight of the source band for the duration of
 * that row.  It is a period and not a flag because the same rect has to be measured on
 * both contents, or the difference between the two rows is a different rect rather than
 * a different content. */
static uint32_t s_ram_holes_period;

/* The two rungs that ask the question `f8` cannot answer on its own.  `f8` beats
 * `f4w` at the same group size, but it takes the pixels in and out as 64-bit pairs,
 * which is legal only while the window's row and the canvas's row share an 8-byte
 * phase -- and in wnd.c they do not whenever the window's left edge is odd.  These two
 * are the SAME shape with 32-bit accesses, so the only variable between `f8` and `f8w`
 * is the width of the memory operation, and the only variable between `f8w` and
 * `f16w` is how many pixels one decision covers.  If `f8w` matches `f8`, the shipped
 * form is the phase-free one and nothing is paid for the portability. */
static void ps2_bench_span_fuse8w(COLOR *dst, const COLOR *src, int span)
{
    int i = 0;

    for (; i + 7 < span; i += 8) {
        const COLOR w[8] = { src[i], src[i + 1], src[i + 2], src[i + 3],
                             src[i + 4], src[i + 5], src[i + 6], src[i + 7] };
        int dirty = 0;

        for (int k = 0; k < 8; k++) dirty |= (w[k] == 0x00000000);
        if (!dirty) {
            for (int k = 0; k < 8; k++) dst[i + k] = w[k];
        } else {
            for (int k = 0; k < 8; k++) {
                if (w[k] != 0x00000000) dst[i + k] = w[k];
            }
        }
    }
    for (; i < span; i++) {
        if (src[i] != 0x00000000) dst[i] = src[i];
    }
}

static void ps2_bench_span_fuse16w(COLOR *dst, const COLOR *src, int span)
{
    int i = 0;

    for (; i + 15 < span; i += 16) {
        const COLOR w[16] = {
            src[i], src[i + 1], src[i + 2], src[i + 3],
            src[i + 4], src[i + 5], src[i + 6], src[i + 7],
            src[i + 8], src[i + 9], src[i + 10], src[i + 11],
            src[i + 12], src[i + 13], src[i + 14], src[i + 15]
        };
        int dirty = 0;

        for (int k = 0; k < 16; k++) dirty |= (w[k] == 0x00000000);
        if (!dirty) {
            for (int k = 0; k < 16; k++) dst[i + k] = w[k];
        } else {
            for (int k = 0; k < 16; k++) {
                if (w[k] != 0x00000000) dst[i + k] = w[k];
            }
        }
    }
    for (; i < span; i++) {
        if (src[i] != 0x00000000) dst[i] = src[i];
    }
}

static void ps2_bench_ram(GDEV *screen, const char *tag, int mode,
                          int left, int w, int h, uint32_t reps)
{
    /* Which of these modes move bytes rather than paint them.  A copy needs a source
     * band as large as its destination and its destination's fold must equal that
     * band's, so the same predicate drives both the fit test below and the chk= the
     * row prints; a fill needs neither.  The fused rungs (9 through 13) are copies in
     * exactly that sense: one read, one write, and the destination must end up holding
     * the source wherever the source is not the sentinel. */
    const int is_copy = (mode == 0 || mode == 1 || mode == 3 || mode == 4 ||
                         mode == 7 || mode == 8 || mode == 9 || mode == 10 ||
                         mode == 11 || mode == 12 || mode == 13 || mode == 14 ||
                         mode == 15 || mode == 16);
    /* The second content regime, and it is a period rather than a flag because the row
     * that needs it is the one that says how many holes it had.  Zero means the band is
     * the desktop's own pixels; non-zero means one pixel in `s_ram_holes_period` of this
     * row's source band has been replaced by the transparent sentinel for the duration
     * of the reps and put back before the fold is taken.  It exists because a fused
     * shape that only wins on an all-opaque row is not a win: the group test that pays
     * off on the clean path is also the test a dirty row pays twice, once in the group
     * and once in its own fixup. */
    const uint32_t holes = s_ram_holes_period;
    COLOR *pix = screen->pixels;
    const int stride = screen->width;
    uint32_t us_sum = 0u, us_max = 0u, sum = 0u;
    const char *chk = "fill";
    const RECT saved_clip = screen->clip;
    const int src_band = 0;
    int dst_band;

    if (!pix || w <= 0 || h <= 0 || reps == 0u) return;
    if (left < 0) left = 0;
    if (left + w > stride) w = stride - left;
    if (w <= 0) return;

    /* Two disjoint bands, src in the top half and dst in the bottom, so a copy
     * reads what a previous frame painted rather than its own just-written bytes --
     * which caps a copy at half the canvas (240,000 px here) and is the reason the
     * sweep's largest rows are fills.  A fill needs no source band, so it can take
     * the whole 800x600. */
    dst_band = screen->height - h;
    if (dst_band < 0 || (is_copy && dst_band < src_band + h)) {
        ps2_kprintf("[BENCH] ram: rect %dx%d does not fit the canvas, skipped\n", w, h);
        return;
    }

    /* Puncture, time, restore.  The save is a per-row gather because the band's rows
     * are `stride` apart, and the restore is not decoration: a source band left
     * swiss-cheesed would silently price every LATER row of this table on content the
     * desktop does not have -- the same class of contamination this book caught in the
     * payload.  The hole is column 3 mod the period, chosen so that the fold's own
     * sampling (every 64th pixel) never lands on one, which is what keeps chk= the tag
     * check it is meant to be in BOTH regimes rather than only in the easy one. */
    COLOR *hole_save = NULL;
    if (holes >= 2u && is_copy) {
        hole_save = (COLOR *)malloc((size_t)w * (size_t)h * sizeof(COLOR));
        if (!hole_save) {
            ps2_kprintf("[BENCH] ram: no room to puncture %s, row skipped\n", tag);
            return;
        }
        for (int y = 0; y < h; y++) {
            COLOR *row = pix + (size_t)(src_band + y) * (size_t)stride + (size_t)left;

            for (int x = 0; x < w; x++) {
                hole_save[(size_t)y * (size_t)w + (size_t)x] = row[x];
                if ((x % (int)holes) == 3) row[x] = 0x00000000;
            }
        }
    }

    if (mode == 2) {
        /* fill_rec() clips, so the leaf is only the leaf if the clip is the canvas.
         * The copy rungs ignore the clip entirely -- they address the canvas
         * directly, which is also why widening it for them would be noise. */
        RECT full = { 0, 0, screen->width, screen->height };
        screen->clip = full;
    }

    for (uint32_t i = 0; i < reps; i++) {
        const uint32_t t0 = ps2_count_read();

        if (mode == 5 || mode == 6) {
            /* The whole rect in one call, because that is the shape being priced:
             * fill_rec() is entered once per strip, and this is the same strip with
             * its row pointer incremented instead of recomputed.  Mode 6 is that
             * leaf again with eight stores per iteration instead of four. */
            ps2_bench_fill_rows(screen, left, dst_band, w, h, mode == 6);
        } else if (mode == 7 || mode == 8) {
            /* wnd.c's client-area walk, row for row, in the order it walks it: the
             * opacity decision first, then either the wide copy or the pixel test
             * that a transparent row forces.  Mode 7 takes that decision one pixel at
             * a time as wnd.c does today; mode 8 takes it two pixels per load.  The
             * copy under both is the shipped header's, so the difference between the
             * two rows is the scan and nothing else. */
            for (int y = 0; y < h; y++) {
                COLOR *d = pix + (size_t)(dst_band + y) * (size_t)stride + (size_t)left;
                const COLOR *s = pix + (size_t)(src_band + y) * (size_t)stride + (size_t)left;
                const size_t bytes = (size_t)w * sizeof(COLOR);
                const BOOL opaque = (mode == 8) ? ps2_bench_span_scan_wide(s, w)
                                                : ps2_bench_span_scan(s, w);

                if (opaque) {
                    btron_row_blit(d, s, bytes);
                } else {
                    for (int cx = 0; cx < w; cx++) {
                        if (s[cx] != 0x00000000) d[cx] = s[cx];
                    }
                }
            }
        } else if (mode >= 9 && mode <= 16) {
            /* The fused shapes: no scan pass at all, because the decision and the store
             * read the same load.  Modes 9 to 12 take the pairs the copy takes (the
             * bench's two bands share a stride, so a 64-bit access is legal on both);
             * mode 13 is the one that survives a window whose stride is not a multiple
             * of two pixels, and it is the shape that can ship.  A mode that fell
             * through to the plain copy below would print a copy's number under a fuse
             * tag and the fold would call it `ok`, because a copy IS what it did -- the
             * dispatch is therefore exhaustive by construction here, and the host gate
             * is what says the fused output equals the walk's. */
            for (int y = 0; y < h; y++) {
                COLOR *d = pix + (size_t)(dst_band + y) * (size_t)stride + (size_t)left;
                const COLOR *s = pix + (size_t)(src_band + y) * (size_t)stride + (size_t)left;

                if (mode == 9)       ps2_bench_span_fuse2(d, s, w);
                else if (mode == 10) ps2_bench_span_fuse4(d, s, w);
                else if (mode == 11) ps2_bench_span_fuse8(d, s, w);
                else if (mode == 12) ps2_bench_span_fuse8_bits(d, s, w);
                else if (mode == 13) ps2_bench_span_fuse4w(d, s, w);
                else if (mode == 14) ps2_bench_span_fuse8w(d, s, w);
                else if (mode == 15) ps2_bench_span_fuse16w(d, s, w);
                else btron_row_copy_skip_zero(d, s, w);
            }
        } else for (int y = 0; y < h; y++) {
            COLOR *d = pix + (size_t)(dst_band + y) * (size_t)stride + (size_t)left;

            /* `mode == 2`, not `mode >= 2`: the first four runs of this phase used a
             * >= here, so the two wide-copy rungs fell into the fill branch and printed
             * fill_rec()'s price under a copy tag, three times, and the row that was
             * supposed to decide the shipped body width never ran.  The dispatch is
             * now what the modes mean, and the checksum below is what catches the next
             * time a branch and a tag disagree. */
            if (mode == 2) {
                const RECT r = { (H)left, (H)(dst_band + y),
                                 (H)(left + w), (H)(dst_band + y + 1) };
                fill_rec(screen, &r, COLOR_LTGRAY);
            } else {
                const COLOR *s = pix + (size_t)(src_band + y) * (size_t)stride + (size_t)left;
                const size_t bytes = (size_t)w * sizeof(COLOR);

                if (mode == 1)      ps2_bench_row_blit_peeled(d, s, bytes);
                else if (mode == 3) ps2_bench_row_blit_wide(d, s, bytes, 0);
                else if (mode == 4) ps2_bench_row_blit_wide(d, s, bytes, 1);
                else                btron_row_blit(d, s, bytes);
            }
        }
        {
            const uint32_t e = ps2_us_since(t0);
            us_sum += e;
            if (e > us_max) us_max = e;
        }
    }
    screen->clip = saved_clip;
    if (hole_save) {
        for (int y = 0; y < h; y++) {
            COLOR *row = pix + (size_t)(src_band + y) * (size_t)stride + (size_t)left;

            for (int x = 0; x < w; x++) row[x] = hole_save[(size_t)y * (size_t)w + (size_t)x];
        }
        free(hole_save);
    }

    /* Every 64th pixel of every 4th row of the destination -- a third of a percent
     * of a full-canvas rect, so not what the row is measuring, but the thing that
     * says the stores landed in memory rather than being deleted as dead, and that
     * `copy odd` moved the bytes `copy even` moved.
     *
     * For a copy the same sampling of the source band is the row's own tag check:
     * if the destination's fold equals the source's fold, the branch taken was the
     * copy's, and if it does not the tag is lying.  The first four runs of this
     * phase printed fill prices under copy tags, three rows in a row, and nothing
     * in the log said so -- only the checksum disagreed.  So the disagreement is
     * printed now. */
    {
        uint32_t ref = 0u;

        for (int y = 0; y < h; y += 4) {
            const COLOR *d = pix + (size_t)(dst_band + y) * (size_t)stride + (size_t)left;
            for (int x = 0; x < w; x += 64) sum += (uint32_t)d[x];
        }
        if (is_copy) {
            for (int y = 0; y < h; y += 4) {
                const COLOR *s = pix + (size_t)(src_band + y) * (size_t)stride + (size_t)left;
                for (int x = 0; x < w; x += 64) ref += (uint32_t)s[x];
            }
        }
        chk = !is_copy ? "fill" : (sum == ref ? "ok" : "MISMATCH");
    }

    /* ns/px with two decimals, in the same %u.%02u idiom the move table uses, and
     * all in 32-bit because this image has no 64-bit divisor: `__udivdi3` is not in
     * ps2_builtins.c, so a `uint64_t / uint64_t` here is a link error rather than an
     * arithmetic step.  The ordering that keeps it 32-bit is to divide the
     * nanoseconds by the pixels first and pay for the two decimals out of the
     * remainder -- ns_sum peaks at one-tenth of the overflow, and the remainder is
     * smaller than the pixel count, which never reaches a million here. */
    {
        const uint32_t px_rep = (uint32_t)w * (uint32_t)h;
        const uint32_t px_total = px_rep * reps;
        const uint32_t ns_sum = us_sum * 1000u;
        const uint32_t ns_px = px_total ? ns_sum / px_total : 0u;
        const uint32_t frac100 = px_total ? ((ns_sum % px_total) * 100u) / px_total : 0u;

        ps2_kprintf("[BENCH] %-13s %4dx%-4d %8upx/rep %6u us/rep max %6u us  %u.%02u ns/px  sum=0x%08x chk=%s\n",
                    tag, w, h, px_rep,
                    (unsigned int)(us_sum / reps), (unsigned int)us_max,
                    (unsigned int)ns_px, (unsigned int)frac100,
                    (unsigned int)sum, chk);
    }
}

/* The shape scan, and it is ONE function because the cached plate's census and a live
 * canvas's census have to be the same arithmetic or the difference between them means
 * nothing.  `want` is passed in rather than read off the image: a poster's colour is decided
 * once, from the plate the compositor owns, and a caller then asks each canvas pixel whether
 * it equals that colour.  Reading `pix[0]` here would make the live canvas answer a different
 * question the moment anything is drawn at its top-left corner.
 *
 * Per row, the first and last pixel that differs from `want` bound that row's art, and only
 * the runs OUTSIDE that box are fillable; consecutive rows with the same bound merge into one
 * rect.  `rects` therefore counts the groups a poster would carry in the general shape,
 * `bands` the simpler one that fills only whole flat rows and leaves every art row to the EE,
 * and the flat area inside an art row's box is what that shape gives back.  `poster_ctl` is
 * the control: the identical scan told to believe `want + 1`, a colour no pixel can be, so a
 * scan that was really counting rows rather than matching pixels would print the same big
 * number for it and one that works collapses to nothing.  Two of the accumulators are ONE
 * fact, and it is said here so a reader does not go looking for a second measurement: a row's
 * posterisable pixels are `w - (x1 - x0 + 1)`, so summing that over the canvas makes the
 * complement of `poster` identical BY CONSTRUCTION to `art_bbox`, the sum of the per-row art
 * boxes.  The pair is kept because the equality is this scan's own arithmetic check -- the two
 * accumulators are filled on opposite arms of the row test, so a broken one shows up as the
 * columns disagreeing rather than as a tautology. */
typedef struct {
    uint32_t poster, poster_ctl;
    uint32_t flat_px, art_bbox_px;
    uint32_t rects, bands, rows_art;
    int witness_row;
} ps2_poster_shape;

static void ps2_bench_poster_scan(const COLOR *pix, COLOR want, ps2_poster_shape *o)
{
    const int w = PS2_SCREEN_WIDTH, h = PS2_SCREEN_HEIGHT;
    const COLOR wrong = (COLOR)((uint32_t)want + 1u);
    int prev_x0 = -2, prev_x1 = -2, prev_flat = -1;
    int y, x;

    o->poster = o->poster_ctl = 0u;
    o->flat_px = o->art_bbox_px = 0u;
    o->rects = o->bands = o->rows_art = 0u;
    o->witness_row = -1;
    for (y = 0; y < h; y++) {
        const COLOR *row = &pix[(size_t)y * w];
        int x0 = -1, x1 = -1, wx0 = -1, wx1 = -1;
        uint32_t nf = 0u, nfw = 0u;

        for (x = 0; x < w; x++) {
            if (row[x] != want) { nf++; if (x0 < 0) x0 = x; x1 = x; }
            if (row[x] != wrong) { nfw++; if (wx0 < 0) wx0 = x; wx1 = x; }
        }
        if (nf == 0u && o->witness_row < 0) o->witness_row = y;
        o->flat_px += (uint32_t)(w - (int)nf);
        if (nf) {
            o->rows_art++;
            o->art_bbox_px += (uint32_t)(x1 - x0 + 1);
            o->poster += (uint32_t)(x0 + (w - 1 - x1));
        } else {
            o->poster += (uint32_t)w;
        }
        o->poster_ctl += nfw ? (uint32_t)(wx0 + (w - 1 - wx1)) : (uint32_t)w;

        /* A new signature is a new rect in the general shape, and a new fully-flat
         * run is a new band in the simple one. */
        if (x0 != prev_x0 || x1 != prev_x1) o->rects++;
        prev_x0 = x0; prev_x1 = x1;
        {
            const int is_flat = (nf == 0u) ? 1 : 0;
            if (is_flat && !prev_flat) o->bands++;
            prev_flat = is_flat;
        }
    }
}

/* LANE 9b's first measurement, and it is not about the GS.  A SPRITE poster carries ONE
 * colour and no picture -- `vram_left=1081344` is 132 of the allocator's 8 KB pages and this
 * canvas's background alone is 1,920,000 bytes -- so the lane's question is asked of the
 * background, not of the primitive: how much of `bg=` is a flat field, and into how many
 * rects does the art break it up.  The answer is taken from the painted plate rather than
 * from a second copy of desktop.c's layout constants, because two encodings of one truth
 * is the defect this port's own laws name.  A GDEV copied from the session's screen but
 * pointed at a scratch array has the same width, height and icon size, so
 * bg_cache_holds() answers yes and render_desktop_background() hands back the cached
 * plate -- the pure background, before any window, deskbar or cursor pixel is in it.  No
 * shared file is modified, and the real canvas is not touched.
 *
 * Then the same arithmetic is asked of the canvas a present would actually upload, because
 * that is the set a caller can collect: whatever the desktop put on the canvas after the
 * background blit -- the pointer's box, the deskbar, a window's body -- has stopped being the
 * flat colour and left the posterisable set on its own, with no rect list and no knowledge of
 * windows needed from the caller.  The gap between the two `posterisable=` columns IS the
 * exclusion set, measured rather than enumerated, which is the answer LANE 9's Gate cell asks
 * the caller to make.  And the census prices its own decision: a caller that learns the flat
 * runs by scanning pays a pass over the canvas, so the row prints that cost in the same
 * ns/px units as the copy floor it is being set against.  ANSWERED 2026-10-14, and the answer is
 * the refutation: the decision eats the saving -- 11,498 us for one compare per pixel against a
 * `bg=` of 7,499 us for the same 480,000 px, and 53,935 us for the scan that would return spans --
 * so no per-pass census can pay, whatever the canvas is showing, and the lane closed as REFUTED
 * rather than as a rect list.  LANE 9's Price cell carries the rows and the reading; this
 * instrument stays so the inequality can be re-taken if the copy floor ever moves. */
static void ps2_bench_poster_price(GDEV *screen)
{
    const int w = PS2_SCREEN_WIDTH, h = PS2_SCREEN_HEIGHT;
    const uint32_t total = (uint32_t)w * (uint32_t)h;
    const uint32_t reps = 16u;            /* the canvas scan's own timing reps */
    ps2_poster_shape bg_shape, live_shape;
    uint32_t saved_us, scan_us = 0u, ns_px, frac100, r;
    uint32_t lean_us = 0u, lean_ns = 0u;
    COLOR *plate;
    H mx = 0, my = 0;

    ps2_kprintf("\n[BENCH] poster price -- how much of `bg=` is one flat colour\n");
    plate = (COLOR *)malloc((size_t)total * sizeof(COLOR));
    if (!plate) {
        ps2_kprintf("[BENCH] poster price: SKIP -- the heap holds no %u KiB plate (largest free %u B)\n",
                    (unsigned int)(total * sizeof(COLOR) / 1024u),
                    (unsigned int)ps2_heap_largest_free());
        return;
    }
    {
        GDEV ref = *screen;
        ref.pixels = plate;
        render_desktop_background(&ref);   /* the cache copy, not a procedural rebuild */
    }

    ps2_bench_poster_scan(plate, plate[0], &bg_shape);

    /* The first row that is entirely one colour is the fill colour's witness: the
     * dot grid starts at y=30, so a plate whose row 0 is uniform is a plate painted
     * by fill_rec() and nothing else has been asked of it. */
    ps2_kprintf("[BENCH] poster price: flat=0x%08x (witness row %d of %d rows) flat=%u px of %u\n",
                (unsigned int)plate[0], bg_shape.witness_row, h,
                (unsigned int)bg_shape.flat_px, (unsigned int)total);

    saved_us = bg_shape.poster * 157u / 10000u;   /* 15.7 ns/px, the copy floor, in µs */
    ps2_kprintf("[BENCH] poster price: posterisable=%u px  ee keeps=%u px  trapped=%u px (inside an art row's box)\n",
                (unsigned int)bg_shape.poster, (unsigned int)(total - bg_shape.poster),
                (unsigned int)(bg_shape.flat_px - bg_shape.poster));
    ps2_kprintf("[BENCH] poster price: rows flat=%u art=%u  rects=%u  bands=%u  art bbox=%u px\n",
                (unsigned int)((uint32_t)h - bg_shape.rows_art), (unsigned int)bg_shape.rows_art,
                (unsigned int)bg_shape.rects, (unsigned int)bg_shape.bands,
                (unsigned int)bg_shape.art_bbox_px);
    ps2_kprintf("[BENCH] poster price: at the 15.7 ns/px copy floor the poster takes %u us off one whole-canvas bg= pass\n",
                (unsigned int)saved_us);
    ps2_kprintf("[BENCH] poster price: control, same scan told to believe 0x%08x -- posterisable=%u px (a broken match would print the same big number)\n",
                (unsigned int)((uint32_t)plate[0] + 1u), (unsigned int)bg_shape.poster_ctl);

    /* The census, on the live canvas and with the PLATE's colour as the truth -- the caller
     * decides the colour once and then only asks what equals it.  Timed over `reps` because
     * one pass of this table is what the machine's own drift, not the result, would decide. */
    {
        const uint32_t t0 = ps2_count_read();
        for (r = 0u; r < reps; r++) ps2_bench_poster_scan(screen->pixels, plate[0], &live_shape);
        scan_us = ps2_us_since(t0) / reps;      /* one full-canvas decision, in µs */
    }
    /* And the cheapest decision a caller could possibly write -- one compare per pixel,
     * no control arm, no per-row box.  It is timed because the refutation this phase is
     * about to make must not rest on my instrument's double match.  `flat_run` is summed
     * over the reps and printed so the loop cannot be folded away. */
    {
        const COLOR *cv0 = (const COLOR *)screen->pixels;
        const COLOR want = plate[0];
        const uint32_t t0 = ps2_count_read();
        uint32_t flat_run = 0u, i;

        for (r = 0u; r < reps; r++) {
            for (i = 0u; i < total; i++) {
                if (cv0[i] == want) flat_run++;
            }
        }
        lean_us = ps2_us_since(t0) / reps;
        lean_ns = (lean_us * 1000u) / total;
        ps2_kprintf("[BENCH] poster census: the cheapest possible decision, one compare per pixel, costs %u us = %u ns/px and it counts %u flat px per pass\n",
                    (unsigned int)lean_us, (unsigned int)lean_ns,
                    (unsigned int)(flat_run / reps));
    }
    get_baremetal_mouse_pos(&mx, &my);
    ps2_kprintf("[BENCH] poster census (live canvas): posterisable=%u px  ee keeps=%u px  trapped=%u px  flat=%u px of %u\n",
                (unsigned int)live_shape.poster, (unsigned int)(total - live_shape.poster),
                (unsigned int)(live_shape.flat_px - live_shape.poster),
                (unsigned int)live_shape.flat_px, (unsigned int)total);
    ps2_kprintf("[BENCH] poster census (live canvas): rows flat=%u art=%u  rects=%u  bands=%u  art bbox=%u px  pointer at (%d,%d)\n",
                (unsigned int)((uint32_t)h - live_shape.rows_art), (unsigned int)live_shape.rows_art,
                (unsigned int)live_shape.rects, (unsigned int)live_shape.bands,
                (unsigned int)live_shape.art_bbox_px, (int)mx, (int)my);
    ps2_kprintf("[BENCH] poster census: the plate offers %u px and the canvas %u px -- %u px is foreground the canvas carries and the cached plate does not, and that is the exclusion set counted rather than enumerated\n",
                (unsigned int)bg_shape.poster, (unsigned int)live_shape.poster,
                (unsigned int)(bg_shape.poster - live_shape.poster));
    ps2_kprintf("[BENCH] poster census: control, same scan told to believe 0x%08x -- posterisable=%u px\n",
                (unsigned int)((uint32_t)plate[0] + 1u), (unsigned int)live_shape.poster_ctl);
    /* Two witnesses that say WHICH canvas this is, because the census's first readings
     * returned a canvas that is almost not the plate at all, and a number that surprising
     * is not quoted until it is identified.  `differs=` is a plain per-pixel inequality
     * against the plate the compositor paints from.  `its own colour=` asks the same shape
     * scan what the canvas's OWN top-left value is worth: if the live canvas carried the
     * same picture in a different byte order -- which would make the plate's colour
     * unmatchable without the canvas being busy -- that shows up as a large posterisable
     * there beside a large `differs=` above it.  Identified 2026-10-14: the canvas's own
     * colour is flat over 14,705 px and the swapped word appears in 0 px, so the big
     * `differs=` is a busy workbench, not a second byte order. */
    {
        const COLOR *cv = (const COLOR *)screen->pixels;
        ps2_poster_shape own;
        uint32_t diff_px = 0u, i;

        for (i = 0u; i < total; i++) {
            if (cv[i] != plate[i]) diff_px++;
        }
        ps2_bench_poster_scan(cv, cv[0], &own);
        ps2_kprintf("[BENCH] poster census: canvas[0]=0x%08x plate[0]=0x%08x  differs=%u px of %u\n",
                    (unsigned int)cv[0], (unsigned int)plate[0],
                    (unsigned int)diff_px, (unsigned int)total);
        /* The byte-order question, in one number: if the canvas is the same picture with
         * R and B exchanged, the R/B-swapped plate word is the canvas's dominant match and
         * `differs=` above is the swap rather than the foreground.  ANSWERED 2026-10-14: the
         * swapped word appears in 0 px, 374,925 px differ in RGB and 170 differ only in alpha,
         * so it is the SCENE and the byte-order law (2.1) is not in question here. */
        {
            const COLOR want = plate[0];
            const COLOR swap = (COLOR)((want & 0xff00ff00u) |
                                       ((want & 0xffu) << 16) | ((want >> 16) & 0xffu));
            uint32_t n_swap = 0u, n_rgb = 0u;

            for (i = 0u; i < total; i++) {
                if (cv[i] == swap) n_swap++;
                if (((uint32_t)cv[i] ^ (uint32_t)plate[i]) & 0x00ffffffu) n_rgb++;
            }
            ps2_kprintf("[BENCH] poster census: the canvas holds the R/B-swapped plate word 0x%08x in %u px -- its centre is 0x%08x where the plate's centre is 0x%08x\n",
                        (unsigned int)swap, (unsigned int)n_swap,
                        (unsigned int)cv[(size_t)(h / 2) * w + w / 2],
                        (unsigned int)plate[(size_t)(h / 2) * w + w / 2]);
            /* The alpha question, in the same shape: `differs=` above counts a pixel as
             * different if ANY of its 32 bits differ, so a canvas that carries the plate's
             * picture with a different alpha byte would look like a canvas covered in
             * foreground.  This is the second decisive reading and it separates the two:
             * pixels whose RED/GREEN/BLUE differ. */
            ps2_kprintf("[BENCH] poster census: pixels whose RGB differs from the plate's: %u of %u -- so of the %u words that differ by all 32 bits, %u differ ONLY in the alpha byte\n",
                        (unsigned int)n_rgb, (unsigned int)total,
                        (unsigned int)diff_px, (unsigned int)(diff_px - n_rgb));
        }
        ps2_kprintf("[BENCH] poster census: the canvas's OWN colour 0x%08x gives posterisable=%u px flat=%u px witness row %d rects=%u -- if this is the big number and differs is one too, the plate and the canvas are the same picture in two orders\n",
                    (unsigned int)cv[0], (unsigned int)own.poster, (unsigned int)own.flat_px,
                    own.witness_row, (unsigned int)own.rects);
    }
    {
        const uint32_t ns_sum = scan_us * 1000u;
        ns_px = ns_sum / total;
        frac100 = ((ns_sum % total) * 100u) / total;
    }
    ps2_kprintf("[BENCH] poster census: deciding costs %u us for the %u px of one canvas = %u.%02u ns/px -- the scan a caller pays to learn which runs are flat -- and the poster takes %u us off bg=, so the net is %s%u us\n",
                (unsigned int)scan_us, (unsigned int)total,
                (unsigned int)ns_px, (unsigned int)frac100, (unsigned int)saved_us,
                saved_us >= scan_us ? "" : "MINUS ",
                (unsigned int)(saved_us >= scan_us ? saved_us - scan_us : scan_us - saved_us));
    free(plate);
}

/* The phase table.  `even`/`odd` are pixel columns, and on this canvas an even
 * column is 8-aligned in bytes while an odd one is 4-mod-8 -- the whole defect, in
 * one character of the tag. */
static void ps2_bench_ram_ops(GDEV *screen)
{
    /* The same column asked of the IMAGE it copies rather than of the leaf that copies
     * it: what a GS poster could take off `bg=`, and the shape it would have to take it
     * in.  This is lane 9b's pre-gate -- the price, before the primitive.
     *
     * It runs FIRST in this phase, and not by preference: every ps2_bench_ram() row below
     * writes into the canvas the census wants to read, and the run that first took the
     * census proved it -- called after them, the live canvas came back `flat=0 px of
     * 480000, rows art=600 rects=1`, which is one uniform colour, and it was the last
     * fill row's own test colour rather than the desktop.  Ahead of the table the canvas
     * still holds the pass the loop last presented, which is the only canvas a present
     * could have uploaded. */
    ps2_kprintf("\n[BENCH] RAM-to-RAM, the loop or the DRAM -- same bytes at two instruction costs\n");
    ps2_bench_poster_price(screen);

    /* One rect, five shapes.  A 400x16 band is the size a real window's damage
     * column costs and the shape both of the compositor's hot leaves use; the odd
     * rows are 399 wide so that the even and odd pairs move a difference of one
     * pixel rather than one pixel's worth of extra claim. */
    ps2_bench_ram(screen, "copy even",     0, 400, 400, 16,  64u);
    ps2_bench_ram(screen, "copy odd",      0, 401, 399, 16,  64u);
    ps2_bench_ram(screen, "copy odd+peel", 1, 401, 399, 16,  64u);
    ps2_bench_ram(screen, "fill even",     2, 400, 400, 16,  64u);
    ps2_bench_ram(screen, "fill odd",      2, 401, 399, 16,  64u);

    /* The prospective fixes, priced against the defects they would replace: 16 and
     * 32 bytes per iteration for the copy, and fill_rec()'s strip with its row
     * pointer incremented instead of recomputed and its stores four-deep and then
     * eight-deep.  Whatever wins here is what goes into fast_blit.h and dp_core.c;
     * whatever loses stays printed here as the reason it did not.
     *
     * The pairs are interleaved and taken at 256 repetitions rather than run once
     * each, because the first pass of this table decided the shipped copy width from
     * a single row per width, and the machine's own drift between adjacent rows is
     * around 1% while the gap being read is around 5%.  `+16b`/`+32b` are the same
     * two shapes measured again after the other one has run, so a width that wins
     * only in the first slot is a position effect, not a result. */
    ps2_bench_ram(screen, "copy even+16", 3, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "copy even+32", 4, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "copy odd+16",  3, 401, 399, 16, 256u);
    ps2_bench_ram(screen, "copy odd+32",  4, 401, 399, 16, 256u);
    ps2_bench_ram(screen, "copy odd+16b", 3, 401, 399, 16, 256u);
    ps2_bench_ram(screen, "copy odd+32b", 4, 401, 399, 16, 256u);
    ps2_bench_ram(screen, "fillunr even", 5, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "fillunr8 even",6, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "fillunr odd",  5, 401, 399, 16, 256u);
    ps2_bench_ram(screen, "fillunr8 odd", 6, 401, 399, 16, 256u);

    /* The compositor's third hot column is neither of the two leaves above: it is
     * wnd.c's client-area walk, and that walk reads every pixel of every row to
     * decide whether the row may be copied at all (src/window/wnd.c:703-719).  The
     * copy rows price 15.7 ns/px and production's blit= column bills around 40, and
     * the gap is this second pass, not the DRAM.  `blit scan` is the walk as it
     * stands; `blit wide` is the same decision two pixels per load -- and BOTH of
     * them still read the band twice, which is what the fused rungs below are for.
     *
     * The ladder is read the way 10.1's width pairs are read: interleaved, and with
     * the shapes taken again in the other order (` b` rows) after every other shape
     * has run, because the drift between adjacent rows here is about 1% and a fused
     * rung that beats `blit wide` only in its own slot is a position effect.  `f4w`
     * is the shape that can ship -- one 32-bit access per pixel, so no shared 8-byte
     * phase between a window's row and the canvas's row is required -- and the
     * pair-width rungs are what say whether that costs anything.
     *
     * Then the same shapes on a band with holes in it, one pixel in eight replaced by
     * the transparent sentinel: the regime where every group test fails and a fused
     * rung pays its test and its fixup.  A rung that wins the clean rows and loses the
     * hole rows is not taken, which is why both blocks are in one phase rather than
     * one of them being a claim. */
    ps2_bench_ram(screen, "blit scan",   7, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit wide",   8, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit f2",     9, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit f4",    10, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit f8",    11, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit f8b",   12, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit f4w",   13, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit f8w",   14, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit f16w",  15, 400, 400, 16, 256u);
    /* The shipped row, priced next to the bench's lookalike: mode 16 calls the function
     * wnd.c's walk calls, so `blit ship` and `blit f16w` differ only in the source the
     * compiler saw.  If they disagree, the number that ships is the one on `ship`. */
    ps2_bench_ram(screen, "blit ship",  16, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit wide b", 8, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit scan b", 7, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit f8 b",  11, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit f4w b", 13, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit f8w b", 14, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit ship b", 16, 400, 400, 16, 256u);

    /* The odd column phase, where the row starts 4-mod-8 and the pair shapes have to
     * peel one pixel before they can load a pair -- the same defect fast_blit.h's
     * comment names, priced on the fused shapes rather than on the copy. */
    ps2_bench_ram(screen, "blit o scan", 7, 401, 399, 16, 256u);
    ps2_bench_ram(screen, "blit o wide", 8, 401, 399, 16, 256u);
    ps2_bench_ram(screen, "blit o f4",  10, 401, 399, 16, 256u);
    ps2_bench_ram(screen, "blit o f4w", 13, 401, 399, 16, 256u);
    ps2_bench_ram(screen, "blit o f8w", 14, 401, 399, 16, 256u);
    ps2_bench_ram(screen, "blit o f16w", 15, 401, 399, 16, 256u);
    ps2_bench_ram(screen, "blit o ship", 16, 401, 399, 16, 256u);

    /* The hole regime, at the same rect and the same repetition count. */
    s_ram_holes_period = 8u;
    ps2_bench_ram(screen, "blit h scan",  7, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit h wide",  8, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit h f4",   10, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit h f8",   11, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit h f8b",  12, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit h f4w",  13, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit h f8w",  14, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit h f16w", 15, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit h scanb",  7, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit h ship", 16, 400, 400, 16, 256u);
    ps2_bench_ram(screen, "blit h f4w b", 13, 400, 400, 16, 256u);
    s_ram_holes_period = 0u;

    /* The sweep, at the phase that can never fall into the tail, over the range the
     * book's flat-price claim covers.  The two shapes are the compositor's own two
     * hot columns: `bg=` is a btron_row_blit() of the cached background over the
     * damage rect (480,000 px for a whole canvas, 9.9 ms measured), and `frame=` is
     * fill_rec() plates (715,360 px, 13.1 ms), so the copy rows can be read against
     * the first and the fill rows against the second.  A copy needs a source band as
     * large as its destination, so the canvas only holds two of them up to 300 rows:
     * 240,000 px is as far as a copy can be taken here, half of what `bg=` bills,
     * and the whole-canvas 480,000 px row is a fill for that reason, not by choice. */
    ps2_kprintf("[BENCH]   -- size sweep, even phase, load+store and store-only\n");
    ps2_bench_ram(screen, "copy 768px",    0, 400,  48,  16, 256u);
    ps2_bench_ram(screen, "copy 12800px",  0,   0, 800,  16,  64u);
    ps2_bench_ram(screen, "copy 12800+32", 4,   0, 800,  16,  64u);
    ps2_bench_ram(screen, "blit 12800",    7,   0, 800,  16,  64u);
    ps2_bench_ram(screen, "blit 12800w",   8,   0, 800,  16,  64u);
    ps2_bench_ram(screen, "blit 12800fw", 13,   0, 800,  16,  64u);
    ps2_bench_ram(screen, "blit 12800f8w",14,   0, 800,  16,  64u);
    ps2_bench_ram(screen, "blit 12800f16w",15,  0, 800,  16,  64u);
    ps2_bench_ram(screen, "blit 12800ship",16,  0, 800,  16,  64u);
    ps2_bench_ram(screen, "copy 80000px",  0,   0, 800, 100,   8u);
    ps2_bench_ram(screen, "copy 240000px", 0,   0, 800, 300,   3u);
    ps2_bench_ram(screen, "fill 768px",    2, 400,  48,  16, 256u);
    ps2_bench_ram(screen, "fill 12800px",  2,   0, 800,  16,  64u);
    ps2_bench_ram(screen, "fill 80000px",  2,   0, 800, 100,   8u);
    ps2_bench_ram(screen, "fill 480000px", 2,   0, 800, 600,   2u);
    /* And the same canvas through the unrolled leaf, at the count `bg=` bills, so
     * the fill fix is priced at its real size and not only at a strip, four-deep
     * against eight. */
    ps2_bench_ram(screen, "fillunr 480k",  5,   0, 800, 600,   2u);
    ps2_bench_ram(screen, "fillunr8 480k", 6,   0, 800, 600,   2u);
}

/* One direction's worth of the loop's own pass -- report, spend, dispatch, bands,
 * present -- in that order, so the rows, bands and cost columns mean the same thing
 * here that they mean in a live session's [PSTAT] window.
 *
 * The move alternates sign every pass: a one-way sweep reaches the border in a
 * handful of passes and then measures the border rather than the present, and the
 * `wall` column is what says when that has happened anyway. */
static void ps2_bench_move(GDEV *screen, const char *tag, int dx, int dy, uint32_t passes)
{
    uint32_t area_sum = 0u, area_max = 0u, bands_sum = 0u, bands_max = 0u;
    uint32_t r_sum = 0u, s_sum = 0u, u_sum = 0u, px_sum = 0u;
    uint32_t empty = 0u, wall = 0u;
    const uint32_t t_start = ps2_count_read();
    RENDER_STATS st;
    EVT ev;

    /* Mid-canvas, and one present of wherever that leaves the sprite, so the
     * re-centring's own cost is not charged to the phase. */
    ptr_cal_set(PS2_SCREEN_WIDTH / 2, PS2_SCREEN_HEIGHT / 2);
    while (get_evt(&ev, 0) == E_OK) workbench_process_event(screen, &ev);
    {
        ps2_bandset_t warm = { { { 0, 0, 0, 0 } }, 0, 0 };
        uint32_t r, s, u, px;

        ps2_bands_for_pass(&warm);
        if (warm.n) ps2_paint_bands(screen, &warm, &r, &s, &u, &px);
    }
    btron_render_stats_take(&st);
    s_shown_x = s_mouse_x;
    s_shown_y = s_mouse_y;

    for (uint32_t i = 0; i < passes; i++) {
        ps2_bandset_t set = { { { 0, 0, 0, 0 } }, 0, 0 };
        const int x0 = s_mouse_x, y0 = s_mouse_y;
        const int sign = (i & 1u) ? -1 : 1;
        uint32_t r, s, u, px;

        ptr_cal_send(dx * sign, dy * sign);
        while (get_evt(&ev, 0) == E_OK) workbench_process_event(screen, &ev);
        ps2_bands_for_pass(&set);
        if (!set.n) { empty++; continue; }
        {
            const int mx = s_mouse_x - x0, my = s_mouse_y - y0;
            px_sum += (uint32_t)(mx < 0 ? -mx : mx) + (uint32_t)((my < 0 ? -my : my));
            if (!mx && !my) wall++;
        }
        ps2_paint_bands(screen, &set, &r, &s, &u, &px);
        area_sum += px;  if (px > area_max) area_max = px;
        bands_sum += (uint32_t)set.n;  if ((uint32_t)set.n > bands_max) bands_max = (uint32_t)set.n;
        r_sum += r; s_sum += s; u_sum += u;
        s_shown_x = s_mouse_x;
        s_shown_y = s_mouse_y;
    }

    ps2_kprintf("[BENCH] %-9s move %d,%d  passes=%u moved px/pass=%u.%02u wall=%u empty=%u  present px %u/%u  bands %u.%02u/%u\n",
                tag, dx, dy, (unsigned int)passes,
                (unsigned int)(px_sum / passes), (unsigned int)(px_sum * 100u / passes % 100u),
                (unsigned int)wall, (unsigned int)empty,
                (unsigned int)area_max, (unsigned int)(area_sum / passes),
                (unsigned int)(bands_sum / passes), (unsigned int)(bands_sum * 100u / passes % 100u),
                (unsigned int)bands_max);
    /* passes/s is the number the cursor's frame rate actually is: one pass paints
     * one position, so the present's cost divided into a second is the pointer's
     * refresh, in both directions, from the same machine.
     *
     * The numerator is 1e6 and not a tick count because ps2_us_since() has already spent
     * the divisor on the way out; the 147000 this row carried was ticks-per-millisecond
     * over a microsecond denominator, which priced every direction 6.8x slower than the
     * `us/pass` column beside it.  Staying 32-bit costs nothing here: the phase's own pass
     * count is 40, and the numerator only overflows past 4294 of them.
     *
     * The wall is tested rather than the Count reading because a direction whose every
     * pass found nothing to paint returns inside one tick, and this image divides by
     * hardware: a zero denominator is a `divu` nobody traps, which prints a rate rather
     * than the absence of one. */
    const uint32_t wall_us = ps2_us_since(t_start);
    ps2_kprintf("[BENCH]           us/pass render %u swap %u upload %u total %u  -> %u passes/s\n",
                (unsigned int)(r_sum / passes), (unsigned int)(s_sum / passes),
                (unsigned int)(u_sum / passes),
                (unsigned int)((r_sum + s_sum + u_sum) / passes),
                (unsigned int)(wall_us ? passes * 1000000u / wall_us : 0u));
    btron_render_stats_take(&st);
    ps2_kprintf("[BENCH]           compositor: bg=%u(%u px) frame=%u(%u px skip=%u) paint=%u blit=%u(%u px) comp=%u bfn=%u wins=%u/%u\n",
                (unsigned int)st.bg_us, (unsigned int)st.bg_worst_px,
                (unsigned int)st.frame_us, (unsigned int)st.frame_body_px,
                (unsigned int)st.frame_body_skip_px,
                (unsigned int)st.paint_us,
                (unsigned int)st.blit_us, (unsigned int)st.blit_worst_px,
                (unsigned int)st.comp_us, (unsigned int)st.bg_full_calls,
                (unsigned int)st.wins_drawn, (unsigned int)st.wins_walked);
}

static void ps2_gui_pass(GDEV *screen);

/* Drain the queue and dispatch it, counting what came out by shape.  This is the
 * same pair of calls the session loop makes, laid out so a measurement can see the
 * middle of it: the live host device is still sending reports during a bench run --
 * the 2026-10-09 log has the emulator pushing 0,-127 until the cursor pins itself to
 * the top wall -- so a phase that only asked "did the menu open" could not tell its
 * own injected byte from that traffic.  Counting the edges it dispatched, and printing
 * the position they carried, makes the two distinguishable. */
static int ps2_bench_drain(GDEV *screen, int *moves, int *buttons, int *bx, int *by)
{
    EVT ev;
    int n = 0;

    *moves = *buttons = 0;
    while (n < 512 && get_evt(&ev, 0) == E_OK) {
        if (ev.type == EV_MOUSE_MOVE) {
            (*moves)++;
        } else if (ev.type == EV_BUT_DOWN || ev.type == EV_BUT_UP) {
            (*buttons)++;
            *bx = ev.pos.x;  *by = ev.pos.y;
        }
        /* The first thing to come out of a queue is the one that says whether this
         * TU and the queue's agree on what a type is: a number outside EV_NONE..EV_TIMER
         * here, with the counts still zero, is a layout mismatch rather than a lost
         * event, and the two need different fixes. */
        if (n == 0) ps2_kprintf("[BENCH] drain: first out type=%d (EVT is %d bytes, move=%d but=%d,%d)\n",
                                (int)ev.type, (int)sizeof(EVT),
                                (int)EV_MOUSE_MOVE, (int)EV_BUT_DOWN, (int)EV_BUT_UP);
        workbench_process_event(screen, &ev);
        n++;
    }
    return n;
}

/* ── Click phase: is the guest's own event chain alive? ─────────────────────
 *
 * "The cursor moves but clicking does nothing" has two authors, and a live session
 * cannot tell them apart from the inside: the emulator never publishing a button
 * byte, or this port's dispatch being dead.  A moving cursor is not evidence about
 * the second one -- ps2_move_mouse() paints the sprite itself, so the cursor can be
 * perfectly smooth while no event reaches any window.  So this injects a report's
 * worth of buttons with no motion in it at all (ps2_usb_inject_mouse() is the same
 * entry point a report uses) and asks the deskbar whether it opened.
 *
 * The first run, 2026-10-09, printed `menu_open=0` for a click the queue had accepted
 * (`r=0`) with no `[WM] b=` row behind it anywhere in 46 s of log -- while the hover
 * column showed a move *had* been dispatched.  Both cannot be true of one queue, so
 * the phase now measures the queue itself instead of inferring it from a row the pass
 * may or may not have printed. */
static void ps2_bench_click(GDEV *screen)
{
    int moves, buttons, bx = -1, by = -1;
    int dbx = -1, dby = -1;
    int hover_park, hover_click, opened, active, still_open, got_down;

    ps2_kprintf("\n[BENCH] synthetic deskbar click -- button byte only, no motion\n");

    /* The queue alone, with nothing between the two calls: no poll, no pass, no
     * report.  This is the smallest question this port can ask of event.c -- does an
     * event I put in come back out with the fields I put on it -- and the 2026-10-09
     * run earned it by accepting fifteen events (`r=0`) and handing none of them to
     * get_evt for 46 seconds.  A type or xy that comes back wrong is a struct layout
     * the two translation units disagree about; E_OK going in and not E_OK coming
     * straight back is the queue itself. */
    {
        EVT one, out;
        ER in_r, out_r;

        tkl_memset(&one, 0, sizeof(one));
        one.type = EV_BUT_DOWN;
        one.pos.x = 60;  one.pos.y = 12;  one.button = 1;
        in_r = snd_evt(&one);
        out_r = get_evt(&out, 0);
        ps2_kprintf("[BENCH] echo: snd=%d get=%d type=%d xy=%d,%d btn=%d (%d byte EVT, move=%d but=%d,%d)\n",
                    (int)in_r, (int)out_r, (int)out.type, out.pos.x, out.pos.y,
                    (int)out.button, (int)sizeof(EVT),
                    (int)EV_MOUSE_MOVE, (int)EV_BUT_DOWN, (int)EV_BUT_UP);
    }

    ps2_bench_drain(screen, &moves, &buttons, &bx, &by);
    ps2_kprintf("[BENCH] click: backlog drained %d moves %d buttons (last at %d,%d)\n",
                moves, buttons, bx, by);

    /* ［BTRON］ is g_headers[0].rect = {4,2,90,23}; the row is the launcher, so the
     * same coordinates answer the "add Terminal to the launcher" question with a
     * window open on screen.  Park there and dispatch that move: a hover highlight
     * is the click's own hit-test one stage earlier, so the two together say whether
     * the deskbar can see the pointer at all. */
    ptr_cal_set(60, 12);
    ps2_bench_drain(screen, &moves, &buttons, &bx, &by);
    hover_park = global_menu_get_hover_header();
    ps2_kprintf("[BENCH] click: parked at 60,12 -- dispatched %d moves, hover_header=%d\n",
                moves, hover_park);

    ps2_usb_inject_mouse(1, 0, 0);           /* left DOWN, zero counts */
    ps2_bench_drain(screen, &moves, &buttons, &bx, &by);
    got_down = buttons;  dbx = bx;  dby = by;
    opened = tracker_is_menu_open();
    active = global_menu_get_active();
    hover_click = global_menu_get_hover_header();
    ps2_gui_pass(screen);                    /* and show it: the menu is an overlay,
                                              * so this is the pass that would paint it */

    ps2_usb_inject_mouse(0, 0, 0);           /* and UP at the same pixel */
    ps2_bench_drain(screen, &moves, &buttons, &bx, &by);
    still_open = tracker_is_menu_open();     /* the menu opens on the press, so it
                                              * must still be up after the release */

    /* Leave the desktop as found: a second edge at the same header is this bar's own
     * toggle, and a bench that ends with a menu over the icon grid is measuring the
     * next phase's screen, not this one's. */
    ps2_usb_inject_mouse(1, 0, 0);
    ps2_bench_drain(screen, &moves, &buttons, &bx, &by);
    ps2_usb_inject_mouse(0, 0, 0);
    ps2_bench_drain(screen, &moves, &buttons, &bx, &by);
    ps2_gui_pass(screen);

    ps2_kprintf("[BENCH] click: down_dispatched=%d at %d,%d active=%d hover %d->%d menu_open=%d still=%d\n",
                got_down, dbx, dby, active, hover_park, hover_click, opened, still_open);
    ps2_kprintf("[BENCH] click: %s\n",
                opened ? "guest chain OK -- a button byte does open the launcher, so a live click is lost before this port sees it"
                       : (got_down ? "DISPATCHED BUT DEAD -- the queue gave the deskbar a button edge and nothing opened"
                                   : "NOT IN THE QUEUE -- the edge was accepted by snd_evt and never came back out"));
}

/* The sprite's own nine leading pixels, straight out of cur_mask[] and cur_outline[] in
 * desktop.c: row 0 white,black; row 1 white,white,black; row 2 white,white,white,black.
 * A single white-then-black pair is not enough to call it the cursor -- window text is
 * exactly that pattern, and the first version of this probe reported a hit at 468,106
 * where no cursor is.  Nine pixels over three rows in that order is nothing else this
 * desktop composes.  COLOR_WHITE and COLOR_BLACK are both fixed points of
 * BTRON_PIXEL_FB(), so this test reads a canvas word as it lies. */
static BOOL ps2_cursor_sig_at(const uint32_t *p, int x, int y)
{
    const uint32_t W = (uint32_t)COLOR_WHITE, B = (uint32_t)COLOR_BLACK;

    if (x + 4 >= PS2_SCREEN_WIDTH || y + 3 >= PS2_SCREEN_HEIGHT) return FALSE;

    const uint32_t *r0 = p + (uint32_t)y * PS2_SCREEN_WIDTH + (uint32_t)x;
    const uint32_t *r1 = r0 + PS2_SCREEN_WIDTH;
    const uint32_t *r2 = r1 + PS2_SCREEN_WIDTH;

    return r0[0] == W && r0[1] == B &&
           r1[0] == W && r1[1] == W && r1[2] == B &&
           r2[0] == W && r2[1] == W && r2[2] == W && r2[3] == B;
}

/* Every hit of that signature on a surface, up to `want`, as x,y pairs.  A whole
 * canvas walk is 480,000 tests; the bench has no frame budget, a live pass does not. */
static int ps2_cursor_scan(const uint32_t *p, int *out, int want)
{
    int n = 0;

    for (int y = 0; y < PS2_SCREEN_HEIGHT && n < want; y++) {
        for (int x = 0; x < PS2_SCREEN_WIDTH && n < want; x++) {
            if (!ps2_cursor_sig_at(p, x, y)) continue;
            out[2 * n] = x;  out[2 * n + 1] = y;
            n++;
        }
    }
    return n;
}

/* ── Cursor phase: does the sprite reach the screen memory at all? ───────────
 *
 * "The mouse is not even visible on move" has three authors on this target: the sprite
 * is never drawn into s_desktop_backbuffer, it is drawn but the pass's present region
 * misses it, or it reaches the GS frame buffer and the display setup does not show it.
 * The guest can separate the first two by itself, because both surfaces are plain RDRAM
 * it owns and both are readable after the pass -- which is why this phase asks where the
 * sprite is, rather than whether the user can see it.
 *
 * The author it found on 2026-10-09 was none of those three and is not in this file:
 * the banded append is a five-argument call into desktop.c, the image was linked from
 * half -march=mips3 and half -march=mips2 objects, and those two pass argument five in
 * different places ($t0 versus the stack), so the sprite's canvas height arrived as
 * garbage and every row of the draw failed its own clip.  A whole-canvas pass still
 * showed the pointer, because that branch draws it from inside desktop.c, where the call
 * stays within one ISA.  The per-region pre=/post= pair is what distinguishes that
 * failure from a present that misses: post=0 with the append flagged as run is the
 * writer not writing, and the verdict says so by name.  The link is one calling
 * convention now (see the ABI note in the Makefile), which is what the post=1 rows in a
 * current run say. */
static void ps2_bench_cursor(GDEV *screen)
{
    static const int park[2][2] = { { 300, 320 }, { 420, 320 } };
    const uint32_t *bb = (const uint32_t *)s_desktop_backbuffer;
    const uint32_t *fb = ps2_gs_get_framebuffer();
    int moves, buttons, px = -1, py = -1;
    int hb[8];
    int bare_bb = 0, elsewhere = 0, append_failed = 0;

    ps2_kprintf("\n[BENCH] cursor presence -- is the sprite in the canvas?\n");
    /* One canvas now, so the two addresses are the same number and the row is the
     * boot-time proof of it: the DMA and the compositor share one array, and a sprite
     * that is in one is in the other.  The question this probe used to answer with a
     * second surface -- "drawn but never presented" -- has moved to the region rows
     * below, which say whether the present ever went where the sprite was drawn. */
    ps2_kprintf("[BENCH] cursor: surfaces bb=0x%x fb=0x%x\n",
                (unsigned)(uintptr_t)bb, (unsigned)(uintptr_t)fb);

    for (int k = 0; k < 2; k++) {
        const int x = park[k][0], y = park[k][1];
        H bmx = 0, bmy = 0;

        /* Drain before parking as well as after: the queue and this port's report
         * accumulator both still hold the earlier phases' motion, and workbench_process_event
         * rewrites desktop.c's position from every event it pops.  Parking into that
         * backlog is how the first run of this probe came to paint at 326,180 while
         * testing at the 300,320 it had asked for. */
        ps2_bench_drain(screen, &moves, &buttons, &px, &py);
        ptr_cal_set(x, y);
        s_paint_calls = 0u;
        ps2_gui_pass(screen);
        const uint32_t bands = s_paint_calls;

        /* Tested at desktop.c's position, because that is the only coordinate either
         * render branch uses: workbench_render() and the banded branch both ask
         * get_baremetal_mouse_pos() for the sprite's top-left.  s_mouse (what the band
         * math swept) and s_shown (what the port believes was last presented) are
         * printed next to it, so a disagreement between the three is readable without
         * another build -- and a sprite that is genuinely absent at the drawn position
         * cannot be excused by one. */
        get_baremetal_mouse_pos(&bmx, &bmy);
        bare_bb = ps2_cursor_sig_at(bb, (int)bmx, (int)bmy);

        const int nb = bare_bb ? 0 : ps2_cursor_scan(bb, hb, 4);
        elsewhere += (!bare_bb && nb > 0);

        ps2_kprintf("[BENCH] cursor: park %d,%d drawn=%d,%d mouse=%d,%d shown=%d,%d in_bb=%d | sig at drawn: bb=%d\n",
                    x, y, (int)bmx, (int)bmy, s_mouse_x, s_mouse_y, s_shown_x, s_shown_y,
                    g_cursor_in_backbuffer, bare_bb);
        /* A whole-canvas walk is only worth its cost when the sprite is not where the
         * port says it drew it: then the row names the places it is instead. */
        if (!bare_bb) {
            ps2_kprintf("[BENCH] cursor: canvas hits bb=%d(%d,%d %d,%d %d,%d %d,%d)\n",
                        nb, hb[0], hb[1], hb[2], hb[3], hb[4], hb[5], hb[6], hb[7]);
        }
        /* What the pass actually covered.  If its union does not contain the sprite's
         * own box at the position that was drawn, the sprite is not missing because the
         * composite forgot to draw it -- the present never went there.  If it does and
         * the signature is still absent, the draw itself is the thing not happening.
         * (The union is a bounding box, so with more than one region it is generous;
         * the region count is printed beside it for exactly that reason.) */
        const int covers = bands > 0u &&
                           s_paint_union[0] <= (int)bmx &&
                           s_paint_union[1] <= (int)bmy &&
                           s_paint_union[2] >= (int)bmx + 16 &&
                           s_paint_union[3] >= (int)bmy + 16;
        ps2_kprintf("[BENCH] cursor: pass made %u region(s), union %d,%d..%d,%d covers sprite %s\n",
                    (unsigned int)bands,
                    s_paint_union[0], s_paint_union[1], s_paint_union[2], s_paint_union[3],
                    covers ? "YES" : "no");
        /* One row per region of this pass.  `sprite=` is the position the cursor draw
         * used inside that region (2 = the whole-canvas composite, which draws it too),
         * so a sprite that is missing from the surfaces has a named region that failed
         * to draw it rather than an inference from the pass's total. */
        for (unsigned i = 0; i < bands && i < PS2_PAINT_TRACE_MAX; i++) {
            ps2_kprintf("[BENCH] cursor:  r%u %d,%d..%d,%d full=%d sprite=%d at %d,%d\n",
                        i,
                        s_paint_rect[i][0], s_paint_rect[i][1],
                        s_paint_rect[i][2], s_paint_rect[i][3],
                        s_paint_full[i], s_paint_appended[i],
                        s_paint_appended_at[i][0], s_paint_appended_at[i][1]);
            ps2_kprintf("[BENCH] cursor:     pixels=0x%x | sig pre=%d post=%d\n",
                        s_paint_pix[i], s_paint_pre[i], s_paint_post[i]);
            if (s_paint_appended[i] == 1 && s_paint_post[i] == 0) append_failed = 1;
        }

        /* What is actually there, as a picture rather than a hex dump: W, B or . over
         * the sprite's own 16x16 box.  The shape is unmistakable, and its absence is
         * too -- which is the point of reading the grid instead of more numbers. */
        if (!bare_bb) {
            for (int r = 0; r < 16; r++) {
                char line[20];
                for (int c = 0; c < 16; c++) {
                    const uint32_t v = bb[(uint32_t)(bmy + r) * PS2_SCREEN_WIDTH + (uint32_t)(bmx + c)];
                    line[c] = (v == (uint32_t)COLOR_WHITE) ? 'W'
                            : (v == (uint32_t)COLOR_BLACK) ? 'B' : '.';
                }
                line[16] = '\0';
                ps2_kprintf("[BENCH] cursor bb  %2d %s\n", r, line);
            }
        }
    }

    ps2_kprintf("[BENCH] cursor: %s\n",
                bare_bb
                    ? "sprite is at the drawn position in the canvas -- the guest paints the words the GIF is given, so an invisible cursor is downstream of this port's paint"
                    : (append_failed ? "the append ran and wrote nothing -- a five-argument call into desktop.c whose fifth argument arrived corrupted, which is what a mixed-MIPS-ABI link does (see the ABI note in the Makefile)"
                                     : (elsewhere ? "sprite is on the canvas but not at the position the append read back -- the two coordinate sources disagree"
                                                  : "sprite is nowhere on the canvas -- it is not being drawn")));
}

/* ── Launch phase: does a launcher row open the application it names? ────────
 *
 * The ［BTRON］ start menu used to be only a window switcher plus four power items;
 * it now carries 端末 (Terminal) and 横断メディアメニュー (XMB) rows too.  A row is
 * pure arithmetic -- tracker_handle_mouse_down() turns a y into an index and calls
 * that item's opener -- so the question worth asking on the target is whether the
 * window the label promises actually appears in the window table.
 *
 * Rows are found by type rather than by index because the list above them is the
 * live window list: a hardcoded row number would measure whatever layout the
 * earlier phases happened to leave behind.
 *
 * Only Terminal is clicked through to completion.  open_xmb_window() is idempotent
 * through a static handle, so a window this phase opened and left standing would
 * make the app phase's own call return early, start no task, and print a false
 * "NO task-driven frame" -- the XMB row is therefore proven to its dispatch (the
 * pointer lands on it, the hover index follows, xmb.c is in the image) and the menu
 * is then dismissed.  gterm is safe to click: it is event-driven, with no task body
 * to run inline on the pass this phase is standing inside. */

/* Windows in the table, and how many of them carry `needle` in the title. */
static int ps2_bench_windows(int *named_out, const char *needle)
{
    const WND *w = get_wnd_list();
    int total = 0, named = 0;

    while (w) {
        total++;
        if (tkl_strstr(w->title, needle)) named++;
        w = w->next;
    }
    *named_out = named;
    return total;
}

/* The launcher, opened the way a hand opens it: park on ［BTRON］ and press. */
static BOOL ps2_bench_launcher_open(GDEV *screen)
{
    int moves, buttons, bx = -1, by = -1;

    ptr_cal_set(60, 12);
    ps2_bench_drain(screen, &moves, &buttons, &bx, &by);
    ps2_usb_inject_mouse(1, 0, 0);
    ps2_bench_drain(screen, &moves, &buttons, &bx, &by);
    ps2_usb_inject_mouse(0, 0, 0);
    ps2_bench_drain(screen, &moves, &buttons, &bx, &by);
    return tracker_is_menu_open();
}

/* Park the pointer on the centre of the first row of this type and dispatch the
 * motion, so the deskbar's own hover says whether the pixel landed on the row. */
static BOOL ps2_bench_row(GDEV *screen, TRACKER_CMD_TYPE type,
                          int *row, int *hover, int *rx, int *ry)
{
    const TRACKER *t = tracker_get_state();
    int moves, buttons, bx = -1, by = -1;
    int idx = -1;

    for (H i = 0; i < t->item_count; i++) {
        if (t->items[i].type == type) { idx = i; break; }
    }
    *row = idx;
    if (idx < 0) return FALSE;

    /* The same arithmetic tracker_handle_mouse_down() applies: rows begin 3 px below
     * menu_rect.top and are TRACKER_ITEM_HEIGHT tall. */
    *rx = t->menu_rect.left + 20;
    *ry = t->menu_rect.top + 3 + idx * TRACKER_ITEM_HEIGHT + TRACKER_ITEM_HEIGHT / 2;

    ptr_cal_set(*rx, *ry);
    ps2_bench_drain(screen, &moves, &buttons, &bx, &by);
    *hover = (int)tracker_get_state()->hover_index;
    return TRUE;
}

static void ps2_bench_press(GDEV *screen)
{
    int moves, buttons, bx = -1, by = -1;

    ps2_usb_inject_mouse(1, 0, 0);
    ps2_bench_drain(screen, &moves, &buttons, &bx, &by);
    ps2_usb_inject_mouse(0, 0, 0);
    ps2_bench_drain(screen, &moves, &buttons, &bx, &by);
    ps2_gui_pass(screen);
}

/* ── Launcher ink: which row bands have anything drawn in them? ──────────────
 *
 * "xmb is not added to start launch" is a claim about the screen, and the canvas is
 * now the one array the GS is fed from, so a band with ink in it is a band the screen
 * can show -- what a band blank here means is that the row was never drawn, and a
 * band that is blank while the item table has a label for it names the label, not the
 * present, as the thing the user cannot find.  Whether the present *covered* a drawn
 * band is a different question and the paint trace answers it: the region rows in
 * ps2_bench_cursor() report the union the pass uploaded.
 *
 * The pointer is parked far outside the menu first.  Motion alone does not dismiss
 * the launcher (only a press does, tracker_handle_mouse_down), and a hover-filled
 * NAVY row would report ink for its fill rather than for its glyphs. */
static void ps2_bench_menu_ink(GDEV *screen, int picture_row_a, int picture_row_b)
{
    const TRACKER *t = tracker_get_state();
    const uint32_t *bb = (const uint32_t *)s_desktop_backbuffer;
    int moves, buttons, bx = -1, by = -1;
    int blank = 0;

    if (!tracker_is_menu_open()) {
        ps2_kprintf("[BENCH] menu ink: launcher is closed, nothing to read\n");
        return;
    }

    ptr_cal_set(PS2_SCREEN_WIDTH - 40, PS2_SCREEN_HEIGHT - 40);
    ps2_bench_drain(screen, &moves, &buttons, &bx, &by);
    ps2_gui_pass(screen);

    const int x0 = t->menu_rect.left + 5;
    const int x1 = t->menu_rect.right - 2;

    ps2_kprintf("[BENCH] menu ink: menu %d,%d..%d,%d, %d row(s), band x %d..%d, hover=%d\n",
                t->menu_rect.left, t->menu_rect.top, t->menu_rect.right, t->menu_rect.bottom,
                (int)t->item_count, x0, x1, (int)t->hover_index);

    for (H i = 0; i < t->item_count; i++) {
        const int y0 = t->menu_rect.top + 3 + i * TRACKER_ITEM_HEIGHT;
        int ink_bb = 0;

        for (int y = y0; y < y0 + TRACKER_ITEM_HEIGHT && y < PS2_SCREEN_HEIGHT; y++) {
            for (int x = x0; x < x1 && x < PS2_SCREEN_WIDTH; x++) {
                const uint32_t b = bb[(uint32_t)y * PS2_SCREEN_WIDTH + (uint32_t)x];
                if (b != (uint32_t)BTRON_PIXEL_FB(COLOR_WHITE)) ink_bb++;
            }
        }
        ps2_kprintf("[BENCH] menu ink: r%-2d type=%-2d y=%-3d bb_ink=%-5d\n",
                    i, (int)t->items[i].type, y0, ink_bb);
        if (t->items[i].type != TRACKER_CMD_SEPARATOR && i != t->hover_index && ink_bb == 0) blank++;

        if (i == picture_row_a || i == picture_row_b) {
            for (int r = 0; r < TRACKER_ITEM_HEIGHT; r += 2) {
                char line[128];
                int n = 0;

                for (int x = x0; x < x1 && n < 120; x += 4) {
                    int lit = 0;
                    for (int sx = 0; sx < 4 && !lit; sx++)
                        for (int sy = 0; sy < 2 && !lit; sy++) {
                            const int px = x + sx, py = y0 + r + sy;
                            if (px >= PS2_SCREEN_WIDTH || py >= PS2_SCREEN_HEIGHT) continue;
                            if (bb[(uint32_t)py * PS2_SCREEN_WIDTH + (uint32_t)px] != (uint32_t)COLOR_WHITE)
                                lit = 1;
                        }
                    line[n++] = lit ? '#' : '.';
                }
                line[n] = '\0';
                ps2_kprintf("[BENCH] menu ink  r%-2d %2d %s\n", i, r, line);
            }
        }
    }

    ps2_kprintf("[BENCH] menu ink: %s\n",
                blank > 0
                    ? "a labelled row is blank in the compositor's own canvas -- the item exists but its label never reaches the screen, which is what \"not added to the launcher\" looks like from outside"
                    : "every labelled row carries ink in the canvas, and the canvas is what the GIF is given");
    ps2_kprintf("[BENCH] menu ink: blank_rows=%d\n", blank);
}

/* ── Key route: is the launcher operable with no pointer at all? ─────────────
 *
 * On this target "can't run xmb from menu" and "the mouse is not usable to a human"
 * are one complaint: the row is in the menu -- the phase above reads it out of the
 * item table, draws it and presents it -- and the only remaining way to it is Start,
 * arrows, Cross.  So drive the pad itself rather than the tracker's API: a press
 * report, a release report, and the queued events dispatched between them, which is
 * the same chain a hand moves.  The button word is active-low, hence the inversion.
 *
 * Activation is then demonstrated on the Terminal row, not on the XMB row, because
 * open_xmb_window() does not return while its window is open -- that one is the app
 * phase's job, with a frame budget and an injected Escape.  What is proved here is
 * that Cross acts on the *selected* row, which is the thing a pointer-less user needs.
 */
static void ps2_bench_pad(GDEV *screen, uint16_t mask)
{
    int moves, buttons, bx = -1, by = -1;

    ps2_pad_set_state((uint16_t)~mask, 128, 128, 128, 128);
    ps2_pad_poll();
    ps2_bench_drain(screen, &moves, &buttons, &bx, &by);
    ps2_pad_set_state(0xFFFFu, 128, 128, 128, 128);
    ps2_pad_poll();
    ps2_bench_drain(screen, &moves, &buttons, &bx, &by);
    ps2_gui_pass(screen);
}

static int ps2_bench_row_of(const TRACKER *t, TRACKER_CMD_TYPE type)
{
    for (H i = 0; i < t->item_count; i++)
        if (t->items[i].type == type) return (int)i;
    return -1;
}

static void ps2_bench_launcher_key_route(GDEV *screen)
{
    const TRACKER *t;
    int hover0, hops = 0, want_row, term_row;
    UW launched0, launched1;
    H px0, py0, px1, py1;

    ps2_kprintf("\n[BENCH] launch by key -- Start, arrows, Cross, pointer not used\n");
    get_baremetal_mouse_pos(&px0, &py0);
    ps2_bench_pad(screen, PAD_START);
    if (!tracker_is_menu_open()) {
        ps2_kprintf("[BENCH] key: Start did not open the launcher, route unmeasured\n");
        return;
    }

    /* The indices are read from the table the open just rebuilt, not carried over
     * from the mouse route: the live-window rows ahead of the application rows change
     * when a window opens, so a number taken before gterm arrived is a different row. */
    t = tracker_get_state();
    want_row = ps2_bench_row_of(t, TRACKER_CMD_XMB);
    term_row = ps2_bench_row_of(t, TRACKER_CMD_TERMINAL);
    hover0 = (int)t->hover_index;
    while (want_row >= 0 && (int)t->hover_index < want_row &&
           hops <= (int)t->item_count + 1) {
        ps2_bench_pad(screen, PAD_DOWN);
        hops++;
        t = tracker_get_state();
    }
    ps2_kprintf("[BENCH] key: hover %d->%d in %d DOWN hops, want row %d, selected='%s'\n",
                hover0, (int)t->hover_index, hops, want_row,
                (t->hover_index >= 0 && t->hover_index < t->item_count)
                    ? t->items[t->hover_index].label : "(none)");
    /* Which surface held the key: workbench_process_event() asks the deskbar first, and
     * global_menu_is_open() answers true for the launcher as well, so a dropdown-less
     * deskbar that claims the arrow leaves the launcher's selection where it was.  The
     * hop count alone cannot separate "arrow not injected" from "arrow swallowed". */
    ps2_kprintf("[BENCH] key: launcher=%d deskbar_open=%d dropdown=%d\n",
                (int)tracker_is_menu_open(), (int)global_menu_is_open(),
                global_menu_get_active());
    ps2_kprintf("[BENCH] key: %s\n",
                (int)t->hover_index == want_row
                    ? "the XMB row is reachable by arrow alone"
                    : "arrow did not land on the XMB row -- the selection is the defect, not the row");

    while (term_row >= 0 && (int)t->hover_index > term_row &&
           hops <= (int)t->item_count * 3) {
        ps2_bench_pad(screen, PAD_UP);
        hops++;
        t = tracker_get_state();
    }
    launched0 = t->launch_count;
    /* The row Cross is about to act on, recorded before the press: the activation closes
     * the menu and hover goes to -1, so reading the table afterwards cannot say which
     * row was chosen.  The UP hops above parked the selection back on the Terminal row
     * deliberately -- XMB's opener does not return while its window is open, so proving
     * the click on the blocking row would hang this phase instead of measuring it. */
    int cross_row = (int)t->hover_index;
    const char *cross_label = (cross_row >= 0 && cross_row < t->item_count)
                                ? t->items[cross_row].label : "(none)";
    ps2_bench_pad(screen, PAD_CROSS);
    launched1 = tracker_get_state()->launch_count;
    get_baremetal_mouse_pos(&px1, &py1);
    ps2_kprintf("[BENCH] key: Cross on row %d '%s' launched %u->%u menu=%d pointer %d,%d->%d,%d\n",
                cross_row, cross_label,
                (unsigned int)launched0, (unsigned int)launched1,
                (int)tracker_is_menu_open(), (int)px0, (int)py0, (int)px1, (int)py1);
    ps2_kprintf("[BENCH] key: %s\n",
                launched1 > launched0
                    ? "Cross activated the highlighted row -- a menu is operable with no pointer"
                    : "Cross activated nothing");
    ps2_kprintf("[BENCH] key: %s\n",
                (px0 == px1 && py0 == py1)
                    ? "the arrows moved the selection only; the pointer stayed put, so nothing recomputed the highlight from a pixel"
                    : "the arrows also displaced the pointer, which re-hovers a row by pixel");
}

static void ps2_bench_launch(GDEV *screen)
{
    int row, hover, rx, ry;
    int named0 = 0, named1 = 0, total0, total1;

    ps2_kprintf("\n[BENCH] launcher rows -- does a row open the application it names?\n");

    if (!ps2_bench_launcher_open(screen)) {
        ps2_kprintf("[BENCH] launch: launcher would not open on a button byte, rows unmeasured\n");
        return;
    }

    total0 = ps2_bench_windows(&named0, "Terminal");
    if (!ps2_bench_row(screen, TRACKER_CMD_TERMINAL, &row, &hover, &rx, &ry)) {
        ps2_kprintf("[BENCH] launch: no 端末 (Terminal) row in the launcher\n");
        return;
    }
    ps2_bench_press(screen);
    total1 = ps2_bench_windows(&named1, "Terminal");
    ps2_kprintf("[BENCH] launch: terminal row=%d xy=%d,%d hover=%d wins %d->%d terminal %d->%d menu=%d\n",
                row, rx, ry, hover, total0, total1, named0, named1, (int)tracker_is_menu_open());
    ps2_kprintf("[BENCH] launch: %s\n",
                named1 > named0 ? "Terminal row opens gterm -- the launcher launches"
                                : "row landed and dispatched, no gterm window in the table");

    if (ps2_bench_launcher_open(screen)) {
        int xmb_hover, xmb_row = -1, xr, yr;
        if (ps2_bench_row(screen, TRACKER_CMD_XMB, &xmb_row, &xmb_hover, &xr, &yr)) {
            ps2_kprintf("[BENCH] launch: xmb row=%d xy=%d,%d hover=%d opener=%s\n",
                        xmb_row, xr, yr, xmb_hover,
                        open_xmb_window ? "linked" : "absent");
        } else {
            ps2_kprintf("[BENCH] launch: no 横断メディアメニュー (XMB) row in the launcher\n");
        }
        /* Dismiss and reopen before reading the bands: a fresh tracker_open_menu()
         * leaves hover on row 0 only, so an ink count is ink for the label and not for
         * a NAVY fill on the row the pointer was last parked on. */
        ptr_cal_set(400, 450);
        ps2_bench_press(screen);
        if (ps2_bench_launcher_open(screen)) {
            ps2_bench_menu_ink(screen, row, xmb_row);
            ptr_cal_set(400, 450);            /* outside the menu: dismiss, as a user would */
            ps2_bench_press(screen);
        }
    }
    ps2_bench_launcher_key_route(screen);
    ps2_kprintf("[BENCH] launch: menu left %d\n", (int)tracker_is_menu_open());
}

/* The closing row prints once, by whichever of the two gets there first: the app
 * phase's frame hook, when the app outlives its frame budget, or the end of
 * ps2_bench_run().  It sits outside the app guard below because that guard decides who
 * may print the row, not whether the table needs one -- with the app phase compiled
 * out, the end of the run is the only printer and still has to know nobody beat it. */
static int s_bench_sentinel;

#if BTRON_PS2_BENCH_GS == 1 || BTRON_PS2_BENCH_GS >= 6
/* ── Stage-1 census rung: the GS read-back with no hand and no window ────────
 *
 * The read was written to answer one question without a screen to look at -- does the
 * GS rasterise an SPRITE into the page that is displayed -- and the answer is a
 * console row, so the only thing a hand was still doing was typing `gsread`.  This
 * types it.  PCSX2 writes the emulated EE console to stdout under -nogui, and the
 * harness waits for the sentinel on that stream, so the census rides the same headless
 * path the present-cost table does: no window, no keyboard, no protocol for anyone to
 * follow, and the rows come back to be read rather than photographed.
 *
 * It replaces the desktop run rather than joining it, and that is not a convenience:
 * `ps2_bench_run()` executes with the GUI up, and inside a GUI session the desktop
 * presents every frame, so its uploads erase the blocks between the draw and the read
 * and its ring traffic trips the read's idle precondition.  The control strip would
 * report UNTRUSTED and the verdict rows would mean nothing.  A census run therefore
 * boots, draws, reads, prints, and then sits in the Stage-1 shell loop -- the guest
 * cannot shut PCSX2 down, so the harness stops it at the sentinel, exactly as it does
 * for the table.
 *
 * BENCH_GS=6 takes the same branch as 1 rather than a GUI one, for the same reason written twice
 * above: rung 3a's whole-page reads are refused while the desktop is presenting, and its plate is a
 * canvas upload, which the desktop's own upload would erase between the plate and the read.  It is a
 * value the census rung answers and nothing else does. */
#if BTRON_PS2_BENCH_GS >= 6
static void ps2_bench_page16(void);    /* the rung itself, defined with its own phase below */
#endif
#if BTRON_PS2_BENCH_GS >= 7
static void ps2_bench_flip(void);      /* rung 4: the two-page rotation, same headless path */
#endif
#if BTRON_PS2_BENCH_GS >= 7
static void ps2_bench_flip(void);      /* rung 4: the two-page rotation, same headless path */
#endif
static void ps2_bench_gs_census(void)
{
    ps2_kprintf("\n[BENCH] Stage 1 GS census rung -- no desktop, no hand, no window\n");
    ps2_gsread_probe();
    /* rung 7 rides the same headless path: it is the one census that turns ABE on, and ABE is the
     * stage every earlier rung bypassed, so the ALPHA register has never been measured without a
     * hand.  It clears and repaints its own boxes, so it must run AFTER gsread's census, not before. */
    ps2_gsalpha_probe();
    /* rung 8 rides the same path AFTER rung 7 (both repaint navy, so neither sees the other's boxes):
     * rung 7 cleared ALPHA and this clears the last stage the flat sprite does not exercise -- whether a
     * textured fragment samples its uploaded page at all.  It is the rung that names what is left between
     * "a flat sprite lays its colour" and "2,705 textured triangles lay none". */
    ps2_gstex_probe();
#if BTRON_PS2_BENCH_GS >= 6
    /* Last, because it is the only rung here that changes the page the others read: it plates a
     * square, uploads the canvas into a PSMCT16 page, reads it back twice, and restores CT32 with the
     * plate painted out.  Run before the probes it would leave a dial moved; run after them it leaves
     * the page, the packer and the canvas as it found them, which is what the sentinel's log needs to
     * mean the same thing as every census rung before it. */
    ps2_bench_page16();
#endif
#if BTRON_PS2_BENCH_GS >= 7
    /* After rung 3a, which leaves the page CT32 and the canvas clean: rung 4 needs a 16-bit page
     * to fit two of them, dials it itself, and restores both on the way out. */
    ps2_bench_flip();
#endif
    /* Before anything downstream could print a second one, and marked as spent for the
     * same reason the app phase's frame hook marks it: the harness stops the emulator
     * on this row, so two copies would tell it the run ended in two places. */
    if (!s_bench_sentinel) {
        ps2_kprintf("\n[BENCH] run complete\n");
        s_bench_sentinel = 1;
    }
}
#endif /* BTRON_PS2_BENCH_GS == 1 || BTRON_PS2_BENCH_GS >= 6 */

#if BTRON_PS2_BENCH_APP
/* ── App phase: is the common GL downstack actually driving frames here? ─────
 *
 * Everything above this line measures this port's present in isolation, with no
 * window in it.  The OpenGL enablement (TinyGL, egl_surface, xmb.c) is the first
 * code in this build that runs a float sine and a z-buffer on the Emotion Engine,
 * and the only way to know it works without a hand on a mouse is to open the app and
 * count the frames the task shim gives it: xmb's body is
 * `while (s_wnd) { inval_wnd(s_wnd); dly_tsk(s_frame_ms); }`, so a pass here exists
 * only because dly_tsk() made one, and the `paint=` column is only nonzero because
 * the compositor ran xb_paint() and something drew into its surface.
 *
 * The phase ends the way a user would: one Escape at depth 1, which is xmb.c's own
 * go-back, and xb_destroy() drops the window and clears the handle the body loops on.
 * Injecting that key from inside the pass is what lets the app be opened and closed
 * by the machine, on a target with no second stack. */
static int      s_bench_app_active; /* the phase is running, so count its passes */
static uint32_t s_bench_app_left;   /* frames owed until the injected Escape */
static uint32_t s_bench_app_made;
static uint32_t s_bench_app_t0;     /* folded µs clock at the open, for the frame heartbeat */
static uint32_t s_bench_app_over;   /* frames made past the budget's Escape, 0 = not yet spent */

extern WND *open_xmb_window(void);
extern int  xmb_depth(void);        /* the depth xb_go_back() is standing at */

/* "xmb still black" measured instead of reported: xb_paint() renders through the GL
 * backend into the window's own device pixels, so a black bar means the rasterizer
 * wrote nothing there.  open_xmb_window() is idempotent through its live handle, so
 * asking it for the window mid-loop costs nothing but a top_wnd().
 *
 * The two columns are read differently because the canvas is not in COLOR order here
 * (include/btron/types.h, BTRON_PIXEL_FB).  black= is a comparison against
 * COLOR_BLACK, whose 0xFF000000 exchanges two zero fields, so it counts the same pixels
 * in either order.  first_colour= is not: it is printed for a human to identify, so it
 * is passed back out through the macro and reads as the colour it is -- a red pixel
 * prints 0xFFFF0000 on every target, where the raw word on the PS2 is 0xFF0000FF. */
static void ps2_bench_xmb_surface(const char *when)
{
    const WND *w = open_xmb_window();
    const GDEV *dev = w ? w->dev : NULL;

    if (!dev || !dev->pixels) {
        ps2_kprintf("[BENCH] xmb surface %s: no window device to read\n", when);
        return;
    }

    unsigned int black = 0, other = 0, first = 0;
    int x0 = -1, x1 = -1, y0 = -1, y1 = -1;
    for (int y = 0; y < dev->height; y++) {
        for (int x = 0; x < dev->width; x++) {
            const COLOR v = BTRON_PIXEL_FB(dev->pixels[(uint32_t)y * (uint32_t)dev->width + (uint32_t)x]);
            if (v == (uint32_t)COLOR_BLACK) { black++; continue; }
            other++;
            if (!first) first = v;
            if (x0 < 0 || x < x0) x0 = x;
            if (x > x1) x1 = x;
            if (y0 < 0 || y < y0) y0 = y;
            if (y > y1) y1 = y;
        }
    }
    ps2_kprintf("[BENCH] xmb surface %s: %dx%d black=%u other=%u first_colour=0x%08x box=%d,%d..%d,%d\n",
                when, dev->width, dev->height, black, other, first, x0, y0, x1, y1);
}

/* LANE 12's census, and the one measurement the frame's biggest term cannot be argued
 * without.  Under PS2_GL=eegs the application's pixels never enter the canvas:
 * `xmb surface frame 3: black=384000` says the XMB window's own device is empty, every
 * `[XMBT] raster` row says `fill 0 us` for an app stage (the EE sends primitives and the
 * GS paints them), and row 9a refuses to blit that empty plate over the desktop.  The
 * compositor still rebuilds all 480,000 px of the canvas each pass and the GIF still
 * re-sends all of them -- 28,044 us of bg+frame+blit and 4,745 us of upload in the run
 * that printed those rows.  Either those bytes change every frame and the rebuild is
 * honest work, or they do not and a third of the frame is copying a picture that is
 * already there.  No column in this log tells the two readings apart, so this one reads
 * the canvas.
 *
 * The instrument is one FNV-1a hash per row, compared against the previous sample's
 * array.  A hash answers "which rows differ", not "how many words differ", and rows is
 * the unit that matters here: the present's band set is made of rows, so a changed-row
 * count maps directly onto the bands a damage-limited transfer would owe.  Equality is
 * taken on the RAW word, with no BTRON_PIXEL_FB() exchange, because byte order has no
 * bearing on whether a word moved.
 *
 * Two confounders are excluded by construction rather than by reading.  (i) The console
 * cannot change the canvas under inspection: ps2_console_putc() emits GS text only while
 * `!s_gui_active` (this file's dual-output emitter), and a GUI pass has it set.  (ii) The
 * sample is taken at the app hook, which runs BEFORE the dispatch that paints, so what is
 * read is the canvas as the previous pass left it -- two consecutive samples are exactly
 * one frame apart, and the third is the control that separates the wave from a
 * once-a-second clock row.  The instrument's own cost is printed because it reads the
 * whole canvas and therefore lengthens the pass it runs in: compare a pair of these rows
 * with each other, never with an uninstrumented pass.
 */
static uint32_t s_census_prev[PS2_SCREEN_HEIGHT];
static int      s_census_have;

static void ps2_bench_canvas_census(const GDEV *screen, unsigned int pass)
{
    unsigned int changed = 0, y, x, lo = 0, hi = 0, w, h;
    uint32_t fp = 0;                    /* xor of the row hashes: the canvas fingerprint */
    const uint32_t t0 = ps2_count_read();

    if (!screen || !screen->pixels) {
        ps2_kprintf("[BENCH] canvas census pass %u: no canvas to read\n", pass);
        return;
    }
    w = (unsigned int)screen->width;
    h = (unsigned int)screen->height;
    if (w == 0u || h == 0u) {
        ps2_kprintf("[BENCH] canvas census pass %u: device is %ux%u\n", pass, w, h);
        return;
    }
    if (h > PS2_SCREEN_HEIGHT) {        /* the array is sized for this port's device */
        ps2_kprintf("[BENCH] canvas census pass %u: device is %u rows, the array holds %u\n",
                    pass, h, (unsigned int)PS2_SCREEN_HEIGHT);
        return;
    }
    for (y = 0; y < h; y++) {
        const uint32_t *row = (const uint32_t *)screen->pixels + (uint32_t)y * w;
        uint32_t hv = 2166136261u;
        for (x = 0; x < w; x++)
            hv = (hv ^ row[x]) * 16777619u;
        fp ^= hv;
        if (s_census_have && hv != s_census_prev[y]) {
            if (!changed) lo = y;
            hi = y;
            changed++;
        }
        s_census_prev[y] = hv;
    }
    s_census_have = 1;
    /* `px<=` is an UPPER bound on the damage, and it is the bound a damage-limited
     * present would have to pay for; a lower bound needs a second pass over the same
     * bytes for a number no decision here turns on. */
    ps2_kprintf("[BENCH] canvas census pass %u: rows=%u changed=%u span=%d..%d "
                "px<=%u of %u fp=%08x cost=%u us\n",
                pass, h, changed,
                changed ? (int)lo : -1, changed ? (int)hi : -1,
                changed * w, w * h, (unsigned int)fp,
                (unsigned int)ps2_us_since(t0));
}

/* Row 7's verdict, and the row that settles what no log line could: `black=384000` with no
 * `[XMBT] raster` line says the rasteriser covered nothing, but four different things produce
 * that reading -- a face whose screen area is zero, a winding that is culled, a volume the
 * geometry never got inside, and a clipper that threw the remainder away.  `tgl_face_stats()`
 * (src/gl/tinygl/clip.c) separates them at the one funnel every face passes, and the second row
 * prints the state those exits are decided by: the viewport's own scale and translation, the
 * three corners of the model-projection matrix an ortho sets, and the three flags.  The floats
 * are printed as bit patterns because the console has no float formatter to trust; an unapplied
 * glOrtho shows as m00=0x3f800000 (1.0f, i.e. still the identity) instead of the fraction a
 * 768-pixel-wide surface divides by.
 *
 * Cumulative since the pipeline was installed, not per frame, so the row reads against the
 * frames the census has already seen.
 *
 * The three rows say a face died and where, but not why: `out=5406` with a correctly applied ortho
 * has two readings that need different fixes -- the projected coordinates are genuinely outside the
 * volume, which makes the transform the thing to audit, or they are inside it and `gl_clipcode()`
 * said so anyway, which makes the comparisons the thing.  `tgl_face_dump_copy()` hands over the
 * first faces as they stood at that funnel: the three input coordinates, the four the transform
 * returned, the clip-code OR the funnel saw, the viewport mapping stored beside them, and the
 * vertex's own clip code.  A row is only worth printing while it is short, so four faces and one
 * line each. */
extern void tgl_face_stats(unsigned *out, int reset);
extern void tgl_face_dump_copy(unsigned *out, unsigned *n_out);

#define PS2_BENCH_FACE_DUMP_W 12

static void ps2_bench_face_stats(const char *when)
{
    unsigned f[20], d[4 * PS2_BENCH_FACE_DUMP_W], n, i;

    tgl_face_stats(f, 0);
    ps2_kprintf("[BENCH] faces %s: total=%u degen=%u cull=%u out=%u partial=%u drawn=%u "
                "clipor=0x%x\n",
                when, f[0], f[1], f[2], f[3], f[4], f[5], f[6]);
    ps2_kprintf("[BENCH] faces %s: vp=%ux%u scaleX=0x%08x transX=0x%08x scaleY=0x%08x "
                "transY=0x%08x\n", when, f[7], f[8], f[9], f[10], f[11], f[12]);
    ps2_kprintf("[BENCH] faces %s: m00=0x%08x m55=0x%08x m15=0x%08x poly=%u cull=%u "
                "front=%u noW=%u\n", when, f[13], f[14], f[15], f[16], f[17], f[18], f[19]);

    tgl_face_dump_copy(d, &n);
    for (i = 0; i < n; i++)
        ps2_kprintf("[BENCH] face %u %s: coord=%08x/%08x/%08x pc=%08x/%08x/%08x W=%08x "
                    "co=%x zp=%d,%d,%d cc=%x\n",
                    i, when,
                    d[i * PS2_BENCH_FACE_DUMP_W + 0], d[i * PS2_BENCH_FACE_DUMP_W + 1],
                    d[i * PS2_BENCH_FACE_DUMP_W + 2], d[i * PS2_BENCH_FACE_DUMP_W + 3],
                    d[i * PS2_BENCH_FACE_DUMP_W + 4], d[i * PS2_BENCH_FACE_DUMP_W + 5],
                    d[i * PS2_BENCH_FACE_DUMP_W + 6], d[i * PS2_BENCH_FACE_DUMP_W + 7],
                    (int)d[i * PS2_BENCH_FACE_DUMP_W + 8], (int)d[i * PS2_BENCH_FACE_DUMP_W + 9],
                    (int)d[i * PS2_BENCH_FACE_DUMP_W + 10], d[i * PS2_BENCH_FACE_DUMP_W + 11]);
}

/* Row 5b's own decisive row, and the reason it exists: every other 5b assertion is about words
 * (a host suite) or about linkage (an image row), and neither can tell me that a plane's bytes
 * actually crossed on this machine.  registered=0 would mean the upload seam is dead -- compiled,
 * linked, called by nothing -- and 5c would then be emitting a stream that samples pages holding
 * whatever the last upload left there.  pending= means a plane the bus never took, and
 * vram_left= is the allocator's own budget read back, which is the number the two shipped sheets
 * are predicted to leave: 1,081,344. */
static void ps2_bench_gs_planes(void)
{
    uint32_t i;

    ps2_kprintf("[BENCH] gs planes: registered=%u pending=%u vram_left=%u\n",
                (unsigned int)gs_tex_count(), (unsigned int)gs_tex_pending(),
                (unsigned int)gs_tex_vram_left());
    for (i = 0; i < gs_tex_count(); i++) {
        const gs_tex_t *t = gs_tex_at(i);

        ps2_kprintf("[BENCH] gs plane %u: upload %ux%u into %ux%u at byte %u TBA %u qwc %u%s\n",
                    (unsigned int)i, (unsigned int)t->px_w, (unsigned int)t->px_h,
                    (unsigned int)t->pad_w, (unsigned int)t->pad_h,
                    (unsigned int)t->byte, (unsigned int)t->tba, (unsigned int)t->qwc,
                    t->pending ? " PENDING" : "");
    }
}

/* How many app frames the bench phase asks for before it injects its own Escape.
 * Sixty is the right number for a table and the wrong number for a feedback loop:
 * at the frame cost this app has, 60 frames is 24 minutes of emulator and every
 * harness that waits for the closing sentinel gets killed at frame 3.  A loop that
 * wants the steady-state per-frame cost only needs the third one. */
#ifndef BTRON_PS2_BENCH_APP_FRAMES
#define BTRON_PS2_BENCH_APP_FRAMES 60u
#endif

/* Frames the launcher-by-key CYCLE phase runs before it injects its own Escape -- and therefore the
 * budget that ends a whole bench run, since this phase is the one that prints the closing sentinel
 * (`ps2_bench_app()`'s longer ask only begins after it).  Three is right for that phase's own
 * question, "did the row open and draw", and useless for LANE 22's, which is a RATE: how many passes
 * refused the app, against how many priced the refusal.  `BENCH_LAUNCH_FRAMES=40` is the dial
 * #165's measurement turns; the default keeps every existing run's rows and cadence identical. */
#ifndef BTRON_PS2_BENCH_LAUNCH_FRAMES
#define BTRON_PS2_BENCH_LAUNCH_FRAMES 3u
#endif

static void ps2_bench_app(uint32_t frames)
{
    RENDER_STATS st;
    const uint32_t t_start = btron_render_perf_us();  /* folded: one frame exceeds a Count wrap */
    WND *w;

    ps2_kprintf("\n[BENCH] xmb opened through the task shim -- %u frames asked of dly_tsk()\n",
                (unsigned int)frames);
    btron_render_stats_take(&st);       /* the region table's counters are not this row's */
    s_bench_app_active = 1;
    /* Six cycles.  The first of them is the app's first painted frame and is expected to
     * carry whatever the GL backend does once (texture uploads, a lazily built table);
     * the rows after it are the period the user would feel.  The profiler costs nothing
     * until this is non-zero, and one branch per boundary after that. */
    s_cycle.left = 6u;
    s_bench_app_left = frames;
    s_bench_app_made = 0u;
    s_bench_app_over = 0u;
    s_bench_app_t0 = t_start;
    /* Blocks until the injected Escape has closed the window: the frame loop is the
     * body's from here, not the session loop's.  The two heap rows are the pool's
     * state either side of open_xmb_window(), largest hole included -- a GL surface
     * needs one contiguous block, so "bytes free" alone can be an innocent answer. */
    const char *pool_bad = ps2_heap_check();
    ps2_kprintf("[BENCH] heap before xmb: used=%u of %u kB, largest hole %u kB, stack=%u kB, pool %s\n",
                (unsigned int)(s_heap_used / 1024u), (unsigned int)(PS2_HEAP_SIZE / 1024u),
                (unsigned int)(ps2_heap_largest_free() / 1024u), ps2_stack_probe_depth(),
                pool_bad ? pool_bad : "sound");
    if (pool_bad) ps2_heap_dump();
    w = open_xmb_window();
    s_bench_app_active = 0;
    s_bench_app_left = 0u;
    pool_bad = ps2_heap_check();
    ps2_kprintf("[BENCH] heap after  xmb: used=%u of %u kB, largest hole %u kB (peak %u kB), stack=%u kB, pool %s\n",
                (unsigned int)(s_heap_used / 1024u), (unsigned int)(PS2_HEAP_SIZE / 1024u),
                (unsigned int)(ps2_heap_largest_free() / 1024u),
                (unsigned int)(s_heap_peak / 1024u),
                ps2_stack_probe_depth(),
                pool_bad ? pool_bad : "sound");
    if (pool_bad) ps2_heap_dump();

    {
        /* passes * 1e6 / us.  The folded clock is already in microseconds, so a second
         * of them is the whole numerator and no tick constant appears here -- which is
         * the form that survives a long phase, where the tick version overflowed the
         * 32-bit product at 60 frames. */
        const uint32_t us = btron_render_perf_us() - t_start;
        /* `<=` because the fold loses a Count wrap for every 2^32/tpu of blocked time
         * between its calls -- 14.6 s at the rate this machine's plane measures, 29.1 s at
         * the documented one -- and a frame of this app is longer than that: the elapsed
         * column is short, so the derived rate is an upper bound, not a measurement. */
        ps2_kprintf("[BENCH] xmb: open returned %s, %u passes in %u ms folded -> <= %u passes/s, window still %s\n",
                    w ? "with a window" : "with no window",
                    (unsigned int)s_bench_app_made,
                    (unsigned int)(us / 1000u),
                    (unsigned int)(us ? s_bench_app_made * 1000000u / us : 0u),
                    get_top_wnd() ? "up" : "closed");
    }
    btron_render_stats_take(&st);
    ps2_kprintf("[BENCH]           compositor: bg=%u(%u px) frame=%u(%u px skip=%u) paint=%u blit=%u(%u px) comp=%u bfn=%u wins=%u/%u\n",
                (unsigned int)st.bg_us, (unsigned int)st.bg_worst_px,
                (unsigned int)st.frame_us, (unsigned int)st.frame_body_px,
                (unsigned int)st.frame_body_skip_px,
                (unsigned int)st.paint_us,
                (unsigned int)st.blit_us, (unsigned int)st.blit_worst_px,
                (unsigned int)st.comp_us, (unsigned int)st.bg_full_calls,
                (unsigned int)st.wins_drawn, (unsigned int)st.wins_walked);
    /* Zero passes is the failure this phase exists to catch: either the window never
     * opened, or a body that asks for a frame got nothing.  A nonzero `paint=` with
     * zero passes is impossible, so the two rows together say which of the two it was.
     * 0 frames is not a comparison, so the row is labelled rather than inferred. */
    ps2_kprintf("[BENCH] xmb: %s\n", s_bench_app_made ? "task-driven frames present"
                                                      : "NO task-driven frame -- dly_tsk() gave the body nothing");
}

/* The user's own action, measured: Start, walk to the XMB row, Cross.
 *
 * The phase above opens the app by calling open_xmb_window() directly, which proves the
 * GL downstack draws but says nothing about the launcher row a hand presses.  This
 * presses it, so the surface rows below belong to this phase.  The frame budget is armed
 * around the press because the row's opener does not return while its window is up --
 * the injected Escape is what lets the run finish -- and the pool is read on both sides,
 * since "no window" and "out of memory" reach the user as one complaint.
 *
 * It runs here rather than from the launcher phase because that phase is last and opens
 * a second terminal first; a pool that has already refused one 2,631 kB request is not a
 * fair place to ask whether the row works. */
static void ps2_bench_launch_xmb_by_key(GDEV *screen)
{
    const TRACKER *t;
    int xmb_row, hops = 0;
    UW launched0;

    ps2_kprintf("\n[BENCH] launch XMB by key -- the row the user presses, activated\n");

    if (!tracker_is_menu_open()) ps2_bench_pad(screen, PAD_START);
    t = tracker_get_state();
    xmb_row = ps2_bench_row_of(t, TRACKER_CMD_XMB);
    if (!tracker_is_menu_open() || xmb_row < 0) {
        ps2_kprintf("[BENCH] xmb by key: launcher=%d xmb_row=%d -- there is no row to press\n",
                    (int)tracker_is_menu_open(), xmb_row);
        return;
    }
    while ((int)t->hover_index < xmb_row && hops <= xmb_row + 2) {
        ps2_bench_pad(screen, PAD_DOWN);
        hops++;
        t = tracker_get_state();
    }
    ps2_kprintf("[BENCH] xmb by key: %d DOWN hops put the highlight on row %d, launcher=%d\n",
                hops, (int)t->hover_index, (int)tracker_is_menu_open());
    if ((int)t->hover_index != xmb_row || !tracker_is_menu_open()) {
        ps2_kprintf("[BENCH] xmb by key: the highlight never reached the XMB row, Cross skipped\n");
        return;
    }

    launched0 = t->launch_count;
    /* The counter is this phase's own: ps2_gui_pass() raises it once per task-driven
     * pass and samples the surface at 3, so arming a budget of 3 both prints the ink row
     * and injects the Escape that ends the body. */
    s_bench_app_t0 = btron_render_perf_us();
    s_bench_app_made = 0u;
    s_bench_app_over = 0u;
    s_bench_app_active = 1;
    s_cycle.left = 3u;
    s_bench_app_left = 3u;
    ps2_kprintf("[BENCH] xmb by key: heap before the press -- used=%u of %u kB, largest hole %u kB, pool %s\n",
                (unsigned int)(s_heap_used / 1024u), (unsigned int)(PS2_HEAP_SIZE / 1024u),
                (unsigned int)(ps2_heap_largest_free() / 1024u),
                ps2_heap_check() ? ps2_heap_check() : "sound");
    ps2_bench_pad(screen, PAD_CROSS);
    s_bench_app_active = 0;
    s_bench_app_left = 0u;
    ps2_kprintf("[BENCH] xmb by key: Cross on row %d launched %u->%u, %u task-driven frames, top window %s\n",
                xmb_row, (unsigned int)launched0,
                (unsigned int)tracker_get_state()->launch_count,
                (unsigned int)s_bench_app_made,
                get_top_wnd() ? "up" : "closed");
    ps2_kprintf("[BENCH] xmb by key: heap after -- used=%u of %u kB, largest hole %u kB (peak %u kB), pool %s\n",
                (unsigned int)(s_heap_used / 1024u), (unsigned int)(PS2_HEAP_SIZE / 1024u),
                (unsigned int)(ps2_heap_largest_free() / 1024u),
                (unsigned int)(s_heap_peak / 1024u),
                ps2_heap_check() ? ps2_heap_check() : "sound");
    ps2_kprintf("[BENCH] xmb by key: %s\n",
                s_bench_app_made
                    ? "the launcher's XMB row drove the app through its own task loop -- the row works"
                    : "the row was activated but the app never got a frame -- this is what \"can't run xmb from menu\" is");
}

/* ── The g_term verb, on a pool nobody has spent yet ────────────────────────
 *
 * "Row is there, picks nothing" is a claim about a third route, and neither phase
 * above can answer it: the deskbar row and this port's direct call both reach
 * open_xmb_window() themselves.  What the user types is `xmb` at the g_term prompt,
 * which is gterm.c:860 -> shell_execute_cmd() -> clu.c's one app table ->
 * open_xmb_window(), and the chain has three answers that reach the screen as shell
 * text -- "Not linked into this build", "Failed to open window surface", "Window
 * opened on the Workbench desktop" -- plus a fourth the user reports: nothing at
 * all.  Only a call through shell_execute_cmd() with an out_fn that echoes can tell
 * those four apart, which is what this phase is.
 *
 * It runs ahead of every pixel phase for one reason, and the 2026-10-15 eegs run is
 * the reason: by the time the click and cursor phases have finished this image has
 * spent 4,054 kB more of the 11 MB pool than a booted desktop has, the largest hole
 * is then 1,359 kB, and the XMB's client pixmap asks 1,500 kB -- so both app phases
 * printed `[GL] ERROR: xmb: egl_create_window_surface failed` while the production
 * image of the same dials ran the XMB at 19.4 ms/frame on a 5,413 kB hole.  The
 * harness had starved its own pool before asking the app to open, and a verdict of
 * "the row works" printed beside that failure measured the dispatch, not the app.
 * Measured at boot, this phase sees the same pool the user's machine has.
 *
 * The census is the row that makes the difference visible instead of inferred: every
 * used block big enough to matter, named by the window that owns it where the window
 * table can say so, and every hole the pool is holding.
 *
 * Three cycles rather than one call, because the complaint is not "the first open fails"
 * -- the 2026-10-15 production log shows a first open at 19.4 ms/frame -- it is "I open it
 * and after a while it picks nothing", and that is a statement about closes.  One open
 * prices the open; the second and third show whether the bytes the first borrowed came
 * back, and on a first-fit pool a hole that shrinks below the ask is the exact moment the
 * verb starts answering with nothing.  The measured answer, before this ledger existed, was
 * 1,088 kB kept per cycle (the 512x512 icon plane and the 128x128 font plane), which is why
 * the pool census runs at boot and after the last close rather than once. */

/* The client pixmap a GL window is about to ask for, computed the way the app
 * computes it (xmb.c's own sizing) and the way the window manager cuts it (opn_wnd():
 * a caption of WND_TITLE_HEIGHT and 4 px of border per side for a titled, bordered
 * window).  Not a constant copied from the OOM row -- it moves with the desktop size,
 * which is why it is derived here. */
static size_t ps2_bench_gl_surface_ask(void)
{
    BTRON_DESKTOP *d = get_btron_desktop();
    int w = 960, h = 600;

    if (d && d->width > 160 && d->height > 160) {
        w = d->width - 24;
        h = d->height - 64;
        if (w > 1440) w = 1440;
        if (h > 900)  h = 900;
    }
    return (size_t)(w - 8) * (size_t)(h - WND_TITLE_HEIGHT - 8) * sizeof(COLOR);
}

static void ps2_bench_pool_census(const char *tag)
{
    unsigned n = 0, big = 0, free_n = 0;
    size_t hole = 0;

    for (const ps2_blk *b = s_heap_list; b; b = b->next, n++) {
        if (b->used) continue;
        free_n++;
        if (b->size > hole) hole = b->size;
    }
    ps2_kprintf("[BENCH] pool %s: %u block(s), %u free, used=%u kB of %u kB, largest hole %u kB, a GL window asks %u kB\n",
                tag, n, free_n, (unsigned int)(s_heap_used / 1024u),
                (unsigned int)(PS2_HEAP_SIZE / 1024u),
                (unsigned int)(hole / 1024u),
                (unsigned int)(ps2_bench_gl_surface_ask() / 1024u));

    /* One row per block at or above the pool's own log threshold, so the names of the
     * big holders are in the log rather than a total.  `unmatched` is not a defect: it
     * is a buffer some subsystem asked for that no window owns -- the compositor's
     * cache, a texture page, a task stack. */
    for (const ps2_blk *b = s_heap_list; b; b = b->next) {
        const WND *w;
        const char *who = "unmatched";

        if (b->size < PS2_HEAP_LOGLIM) continue;
        big++;
        if (!b->used) {
            ps2_kprintf("[BENCH] pool %s: free %5u kB at 0x%x\n", tag,
                        (unsigned int)(b->size / 1024u),
                        (unsigned int)(uintptr_t)(b + 1));
            continue;
        }
        for (w = get_wnd_list(); w; w = w->next) {
            if (w->dev && (const void *)w->dev->pixels == (const void *)(b + 1)) {
                who = w->title[0] ? w->title : "(untitled)";
                break;
            }
        }
        ps2_kprintf("[BENCH] pool %s: used %5u kB at 0x%x  holds '%s'\n", tag,
                    (unsigned int)(b->size / 1024u),
                    (unsigned int)(uintptr_t)(b + 1), who);
    }
    ps2_kprintf("[BENCH] pool %s: %u block(s) at or above %u kB\n", tag, big,
                (unsigned int)(PS2_HEAP_LOGLIM / 1024u));
}

/* What the terminal would have shown, on the console instead, with the three answers
 * clu_launch_app() can give sorted into flags as they go by. */
static int s_bench_shell_lines;
static int s_bench_shell_not_linked;
static int s_bench_shell_no_surface;
static int s_bench_shell_opened;

static void ps2_bench_shell_out(const char *line, COLOR col, void *user_data)
{
    (void)col; (void)user_data;

    s_bench_shell_lines++;
    if (tkl_strstr(line, "Not linked"))       s_bench_shell_not_linked++;
    if (tkl_strstr(line, "Failed to open"))   s_bench_shell_no_surface++;
    if (tkl_strstr(line, "Window opened"))    s_bench_shell_opened++;
    ps2_kprintf("[BENCH] shell> %s\n", line ? line : "(null line)");
}

/* The window this route is supposed to have opened, by title rather than by handle --
 * the handle is the app's own static and is NULL on every path that failed. */
static WND *ps2_bench_wnd_named(const char *needle)
{
    for (WND *w = get_wnd_list(); w; w = w->next)
        if (tkl_strstr(w->title, needle)) return w;
    return NULL;
}

/* `BENCH_GS=2` is this census and not the Stage-1 one: it needs a live desktop, a window that only
 * the app phase opens, and the presents whose pages it reads.  Value 1 keeps the standalone rung
 * that replaces the desktop, which rungs 1..5 were measured with.  Value 3 is the SAME census with
 * LANE 20 rung 23's A/B armed, so the two runs differ by one present-order decision and nothing
 * else; the census gate is therefore `>= 2`. */
#if BTRON_PS2_BENCH_GS >= 2
/* ── LANE 19 rung 6: is the live stream's ink in the page the eye reads? ──────────────────
 *
 * rungs 1..5 settled the PACKET and settled it on the machine: gs_poster_packet()'s dense
 * four-register REGLIST group rasterises on this GS at one loop and at two, on the synchronous
 * sender and on the LANE 10 ring, and the retired PACKED shape still draws nothing -- which is the
 * negative control that says the read is reporting the shapes its labels name.  None of that is
 * evidence about the shipped frame, because that census REPLACES the desktop: it draws its own
 * rects and reads its own rects back, while the complaint on the monitor is still the one reported
 * on 2026-10-15 and again on 2026-10-16 under `make run-ps2 PS2_GL=eegs` -- the window's frame is
 * there and its inside is not.
 *
 * So this rung reads the page a real frame made, and asks it the one question that needs no palette
 * theory.  A present is an IMAGE upload of the bound canvas (`ps2_gs_set_framebuffer()` binds the
 * desktop's own array for a live session, and eegs_present.c's header says the layer "draws into
 * those same VRAM pixels AFTER the upload"), so a word of page 0 that is not its canvas word is a
 * word something else wrote -- and in this build exactly two somebodies exist:
 *
 *   the recorded GS stream, which is what this census is looking for, and
 *   ps2_kprintf's own text band.  The console writes through `ps2_fb()` too -- that IS the bound
 *   canvas -- so its words normally compare equal and need no filtering; the pair it is called
 *   with, 0xFFE0E0E0 on 0xFF121B29 at core_ps2.c:695, is counted separately only because a text
 *   upload repaints its rows after the canvas underneath them may have moved on, which makes the
 *   page hold console ink the canvas no longer holds.  Those two words are therefore reported as
 *   `console=` and excluded from `ink=`, and the exclusion is a class, not a silence: a colour
 *   histogram row is printed for every other word that differs.
 *
 * This rung's own control lives in a box that is excluded by COORDINATE rather than by colour, so
 * a magenta UI pixel elsewhere is still ink and a control that did not land still fails the count.
 *
 * Counting the two apart is the whole instrument, and the three readings are not one diagnosis:
 *
 *   ink = 0 ................ the page IS its canvas: the stream lays no pixel anywhere, so the
 *                            defect is below what rungs 1..5 proved -- the live environment the
 *                            present applies (win/clip/ALPHA with ABE), the order of its groups
 *                            against the upload, or a prim state no census has varied yet.
 *   ink > 0, in-window = 0 . the stream paints, but not inside its own client rect, which is an
 *                            origin, XYOFFSET or SCISSOR answer, and the two boxes name which.
 *   in-window > 0 ........... the stream's ink IS in the client area, so "empty inside" is a
 *                            CONTENT question -- the ink histogram says what arrived.
 *
 * The control is drawn, not assumed: one flat SPRITE of a colour no UI element of this desktop
 * carries, through the shipping emitter, in a corner the chrome does not reach, sent synchronously
 * immediately before the read with nothing printed between the two.  If it comes back short, the
 * read is not trustworthy inside a live session -- the frame's own traffic may still hold the GIF,
 * or an upload may have landed between the last present and the first band -- and every count above
 * it is quoted as SKIP, which is what keeps a truncated read from being filed as a negative answer.
 *
 * It runs at a chosen frame of the app phase rather than after the phase, because the phase's own
 * end injects the Escape that closes the window: by the time `ps2_bench_app()` returns there is no
 * XMB to ask about, and a WND kept past that close prints geometry for memory somebody else owns
 * (the shell phase's note above is the precedent for the same trap).  Needs both dials:
 * `BENCH_APP=1 BENCH_GS=2 scripts/ps2_bench.sh`.
 */
#define GSLIVE_TOP      8u      /* the hottest inks kept; a colour that cannot get a slot is `rare` */
#define PS2_HIST_RECTS  6       /* cover rects this census keeps for its own `eraser:` rows */
/* LANE 19 rung 12s moved the whole instrument -- the control and the six boxes -- from rows 560..575 to
 * rows 32..47.  The reason is the finding, stated here once so no later rung re-picks the old strip:
 * `desktop_render()` fills the BOTTOM 40 px of the page (rows h-40..h = 560..599) with an 8-swatch Colour
 * Test Bar, one swatch every w/8 = 100 px, and two of those swatches are the palette's own red and blue --
 * dp.h `COLOR_RED` 0xFFFF0000 and the bar's `ARGB(0xFF,0x00,0x00,0xFF)` blue.  `fill_rec()` stores every
 * colour through `BTRON_PIXEL_FB()`, which swaps R and B for this target, so those two words land in the
 * canvas BIT-EQUAL to this census's `GSQUAD_RED` and `GSQUAD_BLUE`: swatch 5 (x500..599) reads as red and
 * swatch 6 (x600..699) as blue, whole-height, straight-edged at the SWATCH borders.  The band therefore sat
 * on top of desktop chrome for every rung from 12a to 12r, and the corner-word key counted that chrome as
 * ink.  The arithmetic says so on its own, before any new read: D's window (x536..623) holds 64 red columns
 * and 24 blue ones = 1024 + 384 = the 1408 px every rung since 12j called its block; E's (x632..719) holds
 * 68 blue columns = 1088 = exactly what 12r found pre-draw; and C's 320 is the bar intruding on its last 20
 * columns (x500..519), which 12j's bbox already showed as `R 400-519` while C's own cube cannot reach x500.
 * Rows 32..47 are NOT a chrome-free strip -- there is no 16-row one anywhere on this page, because the XMB's
 * own tab rect covers rows 20..47 (client.top 52 = bounds.top + WND_TITLE_HEIGHT 28 + border 4, so
 * bounds.top 20).  Rows 32..47 are that title bar.  What makes them usable is the other half of the law: the
 * corner-word key is what turns chrome into ink, and a title bar is drawn in `COLOR_GOLD`/`LTGRAY` with black
 * text -- no word of it bit-equal to `GSQUAD_RED`/`GREEN`/`BLUE` -- so its pixels fall to the canvas compare
 * and count as `same`, not as painted.  It is also not a registered cover rect, so unlike the panel (0..28)
 * and the bar (560..599) nothing re-lays it over this census's ink between its sends and its read.  Rung 12s
 * reads the strip BEFORE the first send and prints the page against the canvas, so both halves of that claim
 * are measured rather than argued from wnd.c. */
#define GSLIVE_X0       32      /* the control's own box: 64 x 16 = 1,024 px, in the rung-12s strip */
#define GSLIVE_Y0       32      /* the XMB's title bar: chrome whose words are not corner words, and  */
#define GSLIVE_X1       96
#define GSLIVE_Y1       48      /* no cover rect -- so no present re-lays it over this census's ink */
#define GSLIVE_PX       ((uint32_t)(GSLIVE_X1 - GSLIVE_X0) * (uint32_t)(GSLIVE_Y1 - GSLIVE_Y0))
#define GSLIVE_INK      BTRON_PIXEL_FB(0xFFE0E0E0u)  /* ps2_kprintf's pair, in the order the canvas */
#define GSLIVE_BG       BTRON_PIXEL_FB(0xFF121B29u)  /* holds it: the macro ps2_gs_text_putc() calls
                                                      * is the only spelling that can be compared
                                                      * against a page read back.  Hand-swapping the
                                                      * bytes here instead would split the console
                                                      * class on a colour the console never writes. */

/* LANE 19 rung 12a -- the shading pair's geometry.  Rung 12a chose rows 560..575 because they were the
 * rung-6 magenta box's rows and `ctrl=1024` looked like a certification of the strip; it was not one, and
 * rung 12s named what actually lives there (the Colour Test Bar above).  The rows now come from GSLIVE_SH_Y0
 * and the pre-draw read of 12s is the certification 12a wanted: it tallies every box window BEFORE the first
 * send, so a window that already holds a corner word says so and the rung that reads it is skipped, not
 * believed.  The x ranges keep clear of the magenta box (32..95) and of each other, and both boxes are
 * 128 x 16 so the triangle inside each one covers half of it -- which is why the rows print the box's px
 * count beside the colour tallies. */
#define GSLIVE_SH_Y0       32
#define GSLIVE_SH_Y1       48
/* Rung 12s keeps reading the strip 12a..12r used, over the same six x ranges, in the same pre-draw pass --
 * not as a band but as a control on the finding itself.  If the block is the Colour Test Bar then the page
 * holds the corner words BECAUSE the canvas holds them, at the same positions, and `match` equals `tot`;
 * a corner word the page holds where the canvas holds something else is a writer this file does not own and
 * would still be unnamed after this rung.  Reading it costs one more 16-row VRAM read and settles the claim
 * with a measurement instead of with an offline reading of desktop.c's source. */
#define GSLIVE_OLD_BAND_Y0 560u   /* rows 560..575: the Colour Test Bar's top half, 12s's control band */
#define GSLIVE_SH_A_X0     112   /* IIP=1: the three corner colours ask the GS to interpolate them */
#define GSLIVE_SH_A_X1     240
#define GSLIVE_SH_B_X0     256   /* IIP=0: the same three colours with flat shading, the control */
#define GSLIVE_SH_B_X1     384
#define GSLIVE_SH_QWC_MAX  16u   /* one tag pair plus three vertices at two quadwords = 7 per group */

/* LANE 19 rung 12c/12d/12e -- the hand-built shapes, in the boxes right of rung 12a's pair and in the
 * same certified rows.  A, B, C, D and E keep the x ranges they were measured with on 2026-10-18, so a
 * later run's rows read directly against that run's; C, D and E are now POSITIVE CONTROLS (all three
 * measured as interpolating, to the pixel, on three censuses of one run -- see the 12c/12d comment
 * below for the shapes and the numbers).  F is rung 12e's one new question, in the 64 px the band has
 * left: 64x16 holds a 512-px triangle and the gradient test is a QUARTER of a box's painted px, so a
 * narrow box still has hundreds of interior px -- enough that "flat" and "gradient" cannot be confused
 * for a size artefact. */
#define GSLIVE_SH_N        6u
#define GSLIVE_SH_C_X0     400   /* ps2sdk's own Gouraud cube: AD(PRIM,RGBAQ) then a {RGBAQ,XYZ2} chain */
#define GSLIVE_SH_C_X1     520
#define GSLIVE_SH_D_X0     536   /* C's chain, PRIM from the tag instead of from the AD write */
#define GSLIVE_SH_D_X1     624
#define GSLIVE_SH_D_ASK_W  24    /* rung 12n narrowed D to this; 12s retired the narrowing (see below) */
/* Rung 12o moved D's ask 16 rows BELOW the band, into rows 576..591, to see whether the band's block
 * followed the ask.  It did not, and rung 12s names why: rows 560..599 are the Colour Test Bar, so BOTH of
 * 12o's readings -- "the block stood" and "384 red px filled the moved rect" -- were the bar's own swatch 5
 * (red, x500..599, rows 560..599), not this census's ink and not a foreign GS writer.  The dial's question
 * is therefore answered and its rows are gone: D is asked its whole window in the band again, which makes
 * `out=` identically zero, and 12o's and 12n's rows self-gate to SKIP. */
#define GSLIVE_SH_E_X0     632   /* C's chain plus an ST slot between the colour and the kick */
#define GSLIVE_SH_E_X1     720
#define GSLIVE_SH_F_X0     728   /* E's three-register chain with the recorder's OWN tag: PRE=1, no AD */
#define GSLIVE_SH_F_X1     792

/* LANE 19 rung 12j -- the SPILL window: the rows around the band that no box is read out of.
 *
 * 12i answered "the variable is the frame's ORDER", but the numbers underneath that answer never added up
 * as a shading answer.  Box D, the frame's first triangle, came back `painted=1408` -- its WHOLE rect --
 * split red=1024 blue=384, and 1024 px in an 88x16 box is a 64-wide column at full height, which is not a
 * shape a triangle makes.  Box B came back `painted=1920` of a 2,048-px rect in TWO colours (red 975,
 * blue 945), which is two triangles' coverage in one box where one packet went out.  A box that holds a
 * rectangle and two flat colours is not a box whose gradient failed; it is a box holding geometry that
 * came from somewhere else, and the 12i grid would then be reading WHERE the transfers land rather than
 * HOW they shade.
 *
 * So the same read now records WHERE each corner colour is, per box (a bbox and a count say whether the
 * colour fills its box or a stripe of it), and it records the corner colours OUTSIDE every box, in the
 * rows above and below the band -- the gaps between the boxes (96..111, 241..255, 385..399, 521..535,
 * 625..631, 721..727) and the band's neighbours, which nothing else in this census draws.  A triangle
 * assembled from stale vertex history leaves its colour there, and a triangle that merely failed to
 * interpolate leaves nothing: `spill=0` is therefore the row that makes 12i's answer mean shading, and a
 * non-zero spill is the row that makes it mean geometry.
 *
 * The window is keyed on the three corner WORDS, exactly, and on the pixel differing from the canvas.  The
 * claim this rung was built on -- that a UI pixel cannot enter the tally because "the desktop's own colours
 * are not bit-equal to GSQUAD_RED/GREEN/BLUE" -- is FALSE, and rung 12s proved it: dp.h spells COLOR_RED as
 * 0xFFFF0000 and COLOR_BLUE as 0xFF0000FF, which are GSQUAD_BLUE and GSQUAD_RED in the other reading of the
 * same word, and `desktop_render()` fills the bottom 40 rows of the page with a bar of eight 100-px
 * swatches that uses both.  That is why the spill window now sits in the strip above the client rather than
 * across the bar, and why every pre-draw count of 12s reports what the CANVAS holds at the same positions:
 * a corner word the canvas also holds is chrome, and chrome must never be printable as ink. */
/* The spill window is 16 rows above the band and 8 below it.  Its top reaches into the panel's own rows
 * 16..28, which the present re-lays as chrome -- chrome the desktop draws in gold, grey and navy, none of
 * which is a corner word, so it can only ever land in the `equal-canvas` column, never in R/G/B. */
#define GSLIVE_SP_Y0       16
#define GSLIVE_SP_Y1       56
#define GSLIVE_BB_NONE     0xFFFFFFFFu

/* LANE 19 rung 12k -- a PICTURE of the band, because rung 12j's aggregates were being argued about.
 *
 * 12j settled that the census's corner colours are not where its triangles are: box D holds two
 * full-height colour COLUMNS (red x536-599, blue x600-623, together its whole 88x16 rect), box B holds a
 * blue rect of exactly 63x15 laid over a red region of 51% coverage, and 240 red px fill the 16x15 GAP
 * between boxes B and C, which no box in this census draws.  A bounding box and a count cannot say what
 * shape makes a column, and three readings of those numbers were on the table in one conversation.  So
 * the band is now printed as a picture: one line per row, one character per four pixels of x, for every
 * box, with each cell holding the SET of classes that landed in it.
 *
 * The classes are the three categories rung 12a's grading already uses -- a corner colour, the
 * interpolated field (`other`) and the canvas value the pixel was read out of -- and a cell holds the
 * SET of classes that landed in it, not its majority.  Uppercase is a cell holding only corner colours,
 * lowercase the same set once it shares the cell with field ink, because a cell that saw red AND the
 * field is an edge of a ramp while a cell that saw only red is a flat span, and telling those two apart
 * is the question this rung exists to answer. */
#define GSLIVE_PIC_SHIFT   2u                        /* 4 px per column */
#define GSLIVE_PIC_COLS    (128u >> GSLIVE_PIC_SHIFT) /* the widest box is 128 px (A and B) */
#define GSLIVE_PIC_ROWS    (GSLIVE_SH_Y1 - GSLIVE_SH_Y0)
#define GSLIVE_PIC_R       1u
#define GSLIVE_PIC_G       2u
#define GSLIVE_PIC_B       4u
#define GSLIVE_PIC_FIELD   8u   /* painted, and neither a corner colour nor the canvas value */
#define GSLIVE_PIC_CANVAS 16u   /* the pixel still holds the canvas it was read against */

/* corner-colour + field bits -> character.  Index is the low four class bits, so every combination names
 * itself: 0-7 are whole corner colours (Y=R+G, M=R+B, C=G+B, W=all three) and 8-15 the same sets once
 * sharing a cell with field ink (`f` is field alone).  The canvas bit is deliberately NOT in this table:
 * it is consulted only when a cell saw nothing but the canvas, so an untouched cell reads `.` and a
 * column past this box's width reads a space, while a cell that mixes ink and canvas reads as its ink --
 * 12j's per-box `canvas=` count is what says how much of a box was never touched. */
static const char s_pic_chr[16] = {
    /* 0x0 no class seen (only reachable as `.` or a space, via the canvas bit below) */
    /* 0x1..0x7 whole corner colours, alone and in pairs */
    ' ', 'R', 'G', 'Y', 'B', 'M', 'C', 'W',
    /* 0x8 field alone; 0x9..0xf a corner colour sharing its 4-px cell with field ink:
     * r=R+f g=G+f y=R+G+f b=B+f m=R+B+f c=G+B+f w=all three+f */
    'f', 'r', 'g', 'y', 'b', 'm', 'c', 'w'
};

/* One band picture, in the order the boxes are drawn.  6 x 16 x 32 bytes = 3 kB of image; rung 10's two
 * tile grids already cost 30 kB, and the alternative -- a 4-px-per-column mask of the whole 800-px page --
 * is 16 kB the pool does not have. */
static uint8_t s_band_pic[GSLIVE_SH_N][GSLIVE_PIC_ROWS][GSLIVE_PIC_COLS];

/* Fold one pixel into a {minx, maxx, miny, maxy} box held in page coordinates, an inclusive max, and
 * GSLIVE_BB_NONE until the box has seen a pixel -- the empty case has to be distinguishable from the box
 * at (0,0), which is where the console's own corner colour lives. */
static void gslive_bbox(uint32_t bb[4], uint32_t x, uint32_t y)
{
    if (bb[0] == GSLIVE_BB_NONE || x < bb[0]) bb[0] = x;
    if (bb[1] == GSLIVE_BB_NONE || x > bb[1]) bb[1] = x;
    if (bb[2] == GSLIVE_BB_NONE || y < bb[2]) bb[2] = y;
    if (bb[3] == GSLIVE_BB_NONE || y > bb[3]) bb[3] = y;
}

/* Which condition let a canvas rect out INSIDE a live layer's client -- rung 20x's name for the
 * producer, spelled into the eraser list so a rect does not have to be recognised by its size. */
static const char *ps2_bench_why_name(uint8_t why)
{
    switch (why) {
    case PS2_WHY_STREAM:   return "/before-stream";
    case PS2_WHY_CUT:      return "/cut";
    case PS2_WHY_NO_LAYER: return "/no-layer";
    case PS2_WHY_NO_RECT:  return "/no-rect";
    case PS2_WHY_RELAY:    return "/staged-none";
    case PS2_WHY_COVER:    return "/cover";
    case PS2_WHY_PLATED:   return "/plated-repaint";
    case PS2_WHY_NOCUT:    return "/canvas-lays-over-layer";
    default:               return "/?";
    }
}

/* Does this overlay's rect -- after the present's own clip to the layer's client, which is what the
 * ring recorded -- name the cover rect the census is holding?  The comparison is the intersection
 * and not the raw rect because ps2_upload_cover_in_layer() clips before it sends. */
static int ps2_bench_cover_is(const RECT *r, const int *clip_rect, int cx0, int cy0, int cx1, int cy1)
{
    const int x0 = r->left > cx0 ? r->left : cx0;
    const int y0 = r->top  > cy0 ? r->top  : cy0;
    const int x1 = r->right < cx1 ? r->right : cx1;
    const int y1 = r->bottom < cy1 ? r->bottom : cy1;

    return x1 > x0 && y1 > y0 &&
           x0 == clip_rect[0] && y0 == clip_rect[1] &&
           x1 == clip_rect[2] && y1 == clip_rect[3];
}

/* LANE 19 rung 10 -- the page as a PICTURE, in the log.
 *
 * Why a picture and not another count: rung 6 can print `in_win=383,933 of 384,000 px differ from
 * the canvas`, and on 2026-10-17 the operator looked at that same window and saw dots on gray.  Both
 * statements were about the same rectangle and only one of them was about the picture.  What the
 * census has never printed is WHICH colour is WHERE -- and its own histogram came close (`ink:
 * 0xfe9d4c01 n=356,087 box=16,52..782,550`) without being readable as an image: 356,087 px of ONE
 * colour inside the client rect is 93% of the window flat, and that IS the empty window, told as a
 * number nobody looked at.
 *
 * So this keeps the whole page -- the 800x600 rung 3 already reads back over Local->Host -- and
 * reduces it to a grid of 16x16 tiles, each holding its majority colour, printed twice: P for the GS
 * page as read, C for the EE canvas the compositor painted.  50 columns by 38 rows, one character per
 * tile, the characters indexing a legend of the colours themselves.  `scripts/ps2_page_pic.py` turns
 * those blocks back into two PNGs, which is where the page can be compared with the operator's
 * photograph instead of argued with.
 *
 * The reduction is stated, not hidden: the majority is Boyer-Moore's candidate -- exact for a tile the
 * picture fills with one colour, an approximation for a tile that straddles an edge -- a colour the
 * 32-slot legend cannot hold prints as `.` and is counted in `other=`, and a tile whose pixels all
 * came back SENT prints as a space.  A busy screen therefore says it was flattened rather than being
 * silently flattened.  Costs: 30 kB of BSS for the two grids, one compare per pixel over the 480,000
 * the read already walks, and about 2 kB of log per picture. */
#define GSPIK_TILE  16u
#define GSPIK_COLS  ((uint32_t)PS2_SCREEN_WIDTH / GSPIK_TILE)   /* 50 across an 800 px page */
#define GSPIK_ROWS  (((uint32_t)PS2_SCREEN_HEIGHT + GSPIK_TILE - 1u) / GSPIK_TILE)  /* 38 for 600 */
#define GSPIK_PAL   32u
#define GSPIK_CHARS "0123456789abcdefghijklmnopqrstuv"          /* the 32 legend characters */

typedef struct {
    uint32_t v[GSPIK_ROWS][GSPIK_COLS];   /* the tile's majority candidate, as the page holds it */
    uint32_t n[GSPIK_ROWS][GSPIK_COLS];   /* its weight; 0 until the tile's first pixel arrives */
} gspeak_t;

static gspeak_t s_spk_page, s_spk_canvas;
static uint32_t s_spk_pal[GSPIK_PAL], s_spk_pal_n;
static uint32_t s_spk_other_p, s_spk_other_c;   /* tiles whose colour found no legend slot */
static uint32_t s_spk_blank_p, s_spk_blank_c;   /* tiles with nothing read: SENT pixels */
static uint32_t s_spk_sent;                     /* px the read did not deliver, whole page */
static uint32_t s_spk_printed;                  /* pictures per run: two, then no more */

static void gspeak_add(gspeak_t *g, uint32_t x, uint32_t y, uint32_t v)
{
    const uint32_t r = y / GSPIK_TILE;
    const uint32_t c = x / GSPIK_TILE;

    if (r >= GSPIK_ROWS || c >= GSPIK_COLS) return;
    if (g->n[r][c] == 0u || g->v[r][c] == v) {
        g->v[r][c] = v;
        g->n[r][c]++;
    } else {
        g->n[r][c]--;
    }
}

static void gspeak_reset(void)
{
    uint32_t r, c;

    for (r = 0u; r < GSPIK_ROWS; r++)
        for (c = 0u; c < GSPIK_COLS; c++) {
            s_spk_page.n[r][c] = 0u;
            s_spk_canvas.n[r][c] = 0u;
        }
    s_spk_pal_n = 0u;
    s_spk_other_p = s_spk_other_c = 0u;
    s_spk_blank_p = s_spk_blank_c = 0u;
    s_spk_sent = 0u;
}

/* The legend's index for a colour, taking a slot if this is the first tile that carries it.  -1 means
 * the legend is full: the tile prints as `.` and the caller's `other=` says how many did, so a page
 * richer than 32 tile colours is reported as flattened rather than being quietly one. */
static int gspeak_pal(uint32_t v)
{
    uint32_t i;

    for (i = 0u; i < s_spk_pal_n; i++)
        if (s_spk_pal[i] == v) return (int)i;
    if (s_spk_pal_n < GSPIK_PAL) {
        s_spk_pal[s_spk_pal_n] = v;
        return (int)s_spk_pal_n++;
    }
    return -1;
}

/* One grid, as rows of legend characters.  The legend must already be closed -- gspeak_dump() walks
 * both grids first -- so nothing here can add a colour and leave an earlier row's character meaning
 * something else. */
static void gspeak_print(const char *tag, const gspeak_t *g, uint32_t blanks)
{
    char row[GSPIK_COLS + 2];
    uint32_t r, c;

    ps2_kprintf("[GSDUMP] %s: %u tile(s) had no pixel read back (printed ' ')\n",
                tag, (unsigned int)blanks);
    for (r = 0u; r < GSPIK_ROWS; r++) {
        for (c = 0u; c < GSPIK_COLS; c++) {
            int idx;

            if (g->n[r][c] == 0u) { row[c] = ' '; continue; }
            idx = gspeak_pal(g->v[r][c]);
            row[c] = idx < 0 ? '.' : GSPIK_CHARS[idx];
        }
        row[GSPIK_COLS] = '\0';
        ps2_kprintf("[GSDUMP] %s %u %s\n", tag, (unsigned int)r, row);
    }
}

static void gspeak_dump(void)
{
    uint32_t r, c, i;

    ps2_kprintf("[GSDUMP] the page as a picture: %ux%u px per tile, %ux%u grid.  "
                "P = the GS page as read back, C = the EE canvas the compositor painted.  "
                "One character per tile = its majority colour; scripts/ps2_page_pic.py renders the "
                "P and C blocks as PNGs and as text.  "
                "WORDS ARE 0xAABBGGRR, NOT 0xAARRGGBB: the GS stores a CT32 pixel with r in the low "
                "byte and the canvas holds the same order (BTRON_PIXEL_FB, include/btron/types.h), "
                "so decoding these as ARGB exchanges red and blue and turns the XMB's deep blue into "
                "a brown field -- the wrong-diagnosis trap this row now names out loud.\n",
                (unsigned int)GSPIK_TILE, (unsigned int)GSPIK_TILE,
                (unsigned int)GSPIK_COLS, (unsigned int)GSPIK_ROWS);
    ps2_kprintf("[GSDUMP] coverage: %u px of the page came back SENT (the read did not deliver them)\n",
                (unsigned int)s_spk_sent);

    /* Pass one closes the legend over both grids, P first, and tallies what it could not hold. */
    for (r = 0u; r < GSPIK_ROWS; r++) {
        for (c = 0u; c < GSPIK_COLS; c++) {
            if (s_spk_page.n[r][c] != 0u) {
                if (gspeak_pal(s_spk_page.v[r][c]) < 0) s_spk_other_p++;
            } else s_spk_blank_p++;
            if (s_spk_canvas.n[r][c] != 0u) {
                if (gspeak_pal(s_spk_canvas.v[r][c]) < 0) s_spk_other_c++;
            } else s_spk_blank_c++;
        }
    }
    for (i = 0u; i < s_spk_pal_n; i += 8u) {
        uint32_t j;

        ps2_kprintf("[GSDUMP] pal %c=%08x", GSPIK_CHARS[(int)i], (unsigned int)s_spk_pal[i]);
        for (j = i + 1u; j < s_spk_pal_n && j < i + 8u; j++)
            ps2_kprintf(" %c=%08x", GSPIK_CHARS[(int)j], (unsigned int)s_spk_pal[j]);
        ps2_kprintf("\n");
    }
    ps2_kprintf("[GSDUMP] other: P=%u C=%u tile(s) printed `.` (the legend holds %u colour(s))\n",
                (unsigned int)s_spk_other_p, (unsigned int)s_spk_other_c,
                (unsigned int)s_spk_pal_n);

    /* Pass two prints, and the characters it writes index the legend the lines above just named. */
    gspeak_print("P", &s_spk_page, s_spk_blank_p);
    gspeak_print("C", &s_spk_canvas, s_spk_blank_c);
}

/* LANE 20 rung 21: one rect of the page at ONE PIXEL PER PIXEL, because rung 10's grid cannot show
 * what an icon is doing wrong.
 *
 * Rung 10 flattens the page to a character per 16x16 tile and its legend holds 32 colours.  On the
 * eegs backend the page carries hundreds -- the honest run of 20y/20z printed `other: P=1278` of the
 * 1,900 tiles, i.e. the picture of the very backend this lane exists to look at came back a field of
 * legend-overflow magenta.  And a count cannot answer the operator's actual question: a half-texel
 * bleed off an atlas cell, one flat texel where a picture should be, an edge one pixel late and a
 * missing alpha ramp are all the SAME number of "ink px inside the client".  Only the pixels can say
 * which of the four it is.
 *
 * So this prints the raw words of one small square -- by default the 96x96 at the CENTRE of the GS
 * layer's client, which is where the XMB puts its selected icon -- eight hex digits per pixel, and
 * scripts/ps2_page_pix.py turns them back into a PNG at 1:1 and prints the same grid as text.  The
 * rect is named in its own header row rather than assumed, so a dump that missed the icon says so.
 *
 * Cost: 96x96 px is 73 kB of log, at most two dumps per run, on rung 10's own gate. */
#define GSPIX_SIDE      96u     /* the square, in page pixels */
#define GSPIX_PER_LINE  24u     /* 24 words + the y/x prefix is ~210 chars, the house line width */

static uint32_t s_gspix_printed;

/* The page rows a rung-21 map is about to print, read into the caller's band scratch at exactly the
 * geometry the census's own counts use: x = 0, the full screen width, at most GSREAD_BAND_ROWS rows
 * per call, and the row that comes back at index 0 is the `y0` argument.
 *
 * This exists because all three maps used to index the census's `buf` by ABSOLUTE page y while
 * `buf` is a BAND buffer -- GSREAD_BAND_ROWS rows of the one band most recently read, which after
 * the census's read loop is rows 450..599.  A map walking the client at y=52..552 therefore read
 * 3.7x past the end of a 480 kB allocation, and the 11,714 "cells of the word 0x00000000" that
 * rungs 21c, 23 and 24 were argued on are that memory's own zeros, not the page's.  Every coverage
 * claim those rungs restated is re-opened by this line, and rung 26's correctly-indexed box reads
 * are the measurement it replaces. */
static int gspeak_rows(uint32_t *scratch, uint32_t cap_px, uint32_t width, int y0, int rows)
{
    uint32_t i;

    if (rows <= 0 || (uint64_t)(uint32_t)rows * width > (uint64_t)cap_px) return -1;

    for (i = 0u; i < width * (uint32_t)rows; i++) scratch[i] = GSREAD_SENT;
    if (ps2_gs_vram_read(0, y0, (int)width, rows, scratch) <= 0) return -1;
    return ps2_gs_vram_read_err() == GS_READ_BUS_OK ? 0 : -1;
}

static void gspeak_pixels(uint32_t *buf, uint32_t cap_px, uint32_t width,
                          int cx0, int cy0, int cx1, int cy1)
{
    const int side = (int)GSPIX_SIDE;
    const int x0 = (cx0 + cx1 - side) / 2;
    const int y0 = (cy0 + cy1 - side) / 2;
    int y, x;

    /* Refuse rather than clamp: a rect that runs off the client would print desktop chrome beside the
     * window's own pixels and leave the reader to work out which is which. */
    if (x0 < cx0 || y0 < cy0 || x0 + side > cx1 || y0 + side > cy1) {
        ps2_kprintf("[GSPX] SKIP: the client %d,%d..%d,%d is smaller than the %u px square, so there "
                    "is no centered rect to name\n", cx0, cy0, cx1, cy1, (unsigned int)GSPIX_SIDE);
        return;
    }
    if (gspeak_rows(buf, cap_px, width, y0, side) < 0) {
        ps2_kprintf("[GSPX] SKIP: the square's %d rows at y=%d did not come back, so the words below "
                    "would not be the page's\n", side, y0);
        return;
    }

    ps2_kprintf("[GSPX] rect=%d,%d..%d,%d %dx%d px, %u words per line, 8 hex digits per px.  "
                "WORDS ARE 0xAABBGGRR, the order [GSDUMP]'s palette prints.  A word equal to "
                "0x%08x is GSREAD_SENT: the read did not deliver that pixel.\n",
                x0, y0, x0 + side, y0 + side, side, side,
                (unsigned int)GSPIX_PER_LINE, (unsigned int)GSREAD_SENT);
    for (y = y0; y < y0 + side; y++) {
        const uint32_t *row = buf + (size_t)(y - y0) * (size_t)width;

        for (x = x0; x < x0 + side; x += (int)GSPIX_PER_LINE) {
            uint32_t i;

            ps2_kprintf("[GSPX] y=%-4d x=%-4d", y, x);
            for (i = 0u; i < GSPIX_PER_LINE; i++)
                ps2_kprintf(" %08x", (unsigned int)row[(size_t)x + (size_t)i]);
            ps2_kprintf("\n");
        }
    }
}

/* LANE 20 rung 21b: the same page as a map over the WHOLE client, because the square above answers
 * "what does one icon look like" and cannot answer "where does the stream's ink stop".
 *
 * The first run of `gspeak_pixels()` is why this exists: the 96x96 at the client's centre came back
 * 6,869 px of the word 0x00000000 -- a pixel with no colour and no alpha, i.e. VRAM nobody wrote --
 * and 938 px of one near-white word, in horizontal dashes one and two pixels tall.  That is not an
 * icon and not a gradient, and a 96 px square cannot say what it is: the question is now how big the
 * unpainted region is and what shape its edge has, which is a whole-client question.
 *
 * So this walks the client at 4 px per character and prints THREE maps of it, because one word has
 * three separate questions in it: `gspeak_lum_map`'s original ramp says HOW BRIGHT a cell is (`k`
 * when all 16 of its pixels are the zero word, `?` when any is the read's own SENT sentinel, else a
 * ten-step luminance ramp); the ALPHA map says WHO painted it; and the HUE map plus a word histogram
 * says WHICH CHAIN's colour it is.  A ramp rather than the rung 10 legend because a ramp cannot
 * saturate -- the eegs page holds hundreds of distinct words and a 32-slot colour legend overflows at
 * 1,278 of its 1,900 tiles, which is how rung 10's picture came back one flat magenta.
 *
 * Each of the three maps now reads the page itself, in blocks of at most `GSREAD_BAND_ROWS` rows,
 * into the caller's band buffer and with buffer indices relative to the block it just read.  It used
 * to walk `buf` by ABSOLUTE page y, on the belief that the census had left a whole page there; the
 * census leaves one 150-row band, so every row below the fourth was the heap past the end of a
 * 480 kB allocation.  That is the whole of the "11,714 cells of the zero word" the maps reported --
 * `k` filled the bottom of the client uniformly, across all 192 columns, and stopped within a cell of
 * page y=150, which is the buffer's own end and not a feature of the page.  Costs: 192 chars x 125
 * rows x 3 maps = 72 kB of log, and 12 Local<-Host reads, once per run. */
#define GSPIX_MAP_STEP  4u                      /* page px per character, both axes */
#define GSPIX_MAP_COLS  196u                    /* a 784 px client at 4 px/char is 196 chars */

static uint32_t s_gspix_mapped;

/* The alpha byte, which is the whole of what the second map below is for.  Both the stream's ink
 * and the compositor's plate carry a distinctive one: the gradient and the ribbon are built from
 * vertices whose RGBA is 0xfe______ (the XMB's own `sf`-scaled alpha), a canvas upload writes 0xff,
 * and the skipped body plate under a GL layer is the zero word.  So the alpha alone says WHO painted
 * a cell, which the luminance cannot when the ink and the plate are both dark. */
#define GSPIX_ALPHA(v)  ((uint32_t)((v) >> 24))

/* The three rung-21 maps of the GS layer's client, read in `GSREAD_BAND_ROWS` blocks. */
static void gspeak_page_map(uint32_t *buf, uint32_t cap_px, uint32_t width,
                            int cx0, int cy0, int cx1, int cy1)
{
    static const char ramp[] = " .:-=+*#%@";    /* dark to bright; index 0 is a real space */
    static char line[GSPIX_MAP_COLS + 1];
    int x, y, c, chunk;

    ps2_kprintf("[GSPIXM] client=%d,%d..%d,%d at %u px per char: `k` = all 16 px are the zero word "
                "(nobody wrote this VRAM), `?` = a pixel the read did not deliver, else a 10-step "
                "luminance ramp of the cell.  Rows are page y, columns start at x=%d.  Each %u-row "
                "block below is its own Local<-Host read.\n",
                cx0, cy0, cx1, cy1, (unsigned int)GSPIX_MAP_STEP, cx0,
                (unsigned int)GSREAD_BAND_ROWS);

    for (chunk = cy0; chunk < cy1; chunk += (int)GSREAD_BAND_ROWS) {
        const int rows = (cy1 - chunk < (int)GSREAD_BAND_ROWS) ? cy1 - chunk
                                                              : (int)GSREAD_BAND_ROWS;

        if (gspeak_rows(buf, cap_px, width, chunk, rows) < 0) {
            ps2_kprintf("[GSPIXM] SKIP at y=%d: the block did not come back, so the ramp below it "
                        "would be the heap, not the page\n", chunk);
            break;
        }

        for (y = chunk; y < chunk + rows; y += (int)GSPIX_MAP_STEP) {
            c = 0;
            for (x = cx0; x + (int)GSPIX_MAP_STEP <= cx1 && c < (int)GSPIX_MAP_COLS;
                 x += (int)GSPIX_MAP_STEP) {
                uint32_t lum = 0u, n = 0u;
                int all_zero = 1, any_sent = 0, dy, dx;

                for (dy = 0; dy < (int)GSPIX_MAP_STEP; dy++) {
                    const uint32_t *row = buf + (size_t)(y + dy - chunk) * (size_t)width;

                    for (dx = 0; dx < (int)GSPIX_MAP_STEP; dx++) {
                        const uint32_t v = row[(size_t)x + (size_t)dx];

                        if (v == GSREAD_SENT) any_sent = 1;
                        if (v != 0u) all_zero = 0;
                        /* 0xAABBGGRR, the order the page holds: the ITU weights, integer. */
                        lum += ((v & 0xFFu) * 30u + ((v >> 8) & 0xFFu) * 59u +
                                ((v >> 16) & 0xFFu) * 11u) / 100u;
                        n++;
                    }
                }
                if (any_sent) line[c++] = '?';
                else if (all_zero) line[c++] = 'k';
                else line[c++] = ramp[(int)((n ? lum / n : 0u) * 9u / 255u)];
            }
            line[c] = '\0';
            ps2_kprintf("[GSPIXM] y=%-4d %s\n", y, line);
        }
    }

    /* Rung 21c: the same client, classified by WHO painted it rather than by how bright it is.
     *
     * What this was argued on is gone.  The first version of both maps indexed the census's band
     * buffer by ABSOLUTE page y, so everything past the fourth band was heap, and the claim the alpha
     * map then "proved" -- that the stream covers only about a quarter of its own client and the rest
     * is the zero word -- is that heap's zeros.  Rung 26, which reads 64x16 boxes at the block-local
     * geometry a band buffer forces, found stream ink (A=0xfe) in every box across the client on 14
     * consecutive frames, and the census's own correctly-indexed counts agree with it: `survival:
     * census #1 client ink=380861 canvas=3139 of 384000 px`.  So the open question is no longer
     * WHETHER the page is inked -- it is 99% inked -- but what colour that ink is, because what the
     * operator sees is a grey wave where the XMB's blue gradient should be.
     *
     * The alpha still separates the painters, and that is still worth a map: the gradient's own
     * vertices arrive with A=0xfe and nothing else on this path writes that byte, a canvas upload
     * writes 0xff, and a skipped body plate is the zero word.  `e` cells are the stream's ink; `k`
     * cells are nobody's.  A map that is `e` where the ribbon and gradient were recorded says the GS
     * drew; one that is `f` there says a canvas upload landed on top of it after the stream. */
    ps2_kprintf("[GSPIXA] same client by ALPHA owner: `k` zero word (plate: nobody wrote it), "
                "`e` A=0xfe (the stream's own ink), `f` A=0xff (a canvas or opaque upload), "
                "`0` A=0x00 with colour, `m` mixed alpha in the cell, `o` other, `?` undelivered.\n");

    {
        uint32_t n_k = 0u, n_e = 0u, n_f = 0u, n_0 = 0u, n_m = 0u, n_o = 0u, n_q = 0u, n_cells = 0u;
        uint32_t bad = 0u;

        for (chunk = cy0; chunk < cy1; chunk += (int)GSREAD_BAND_ROWS) {
            const int rows = (cy1 - chunk < (int)GSREAD_BAND_ROWS) ? cy1 - chunk
                                                                  : (int)GSREAD_BAND_ROWS;

            if (gspeak_rows(buf, cap_px, width, chunk, rows) < 0) {
                bad++;
                ps2_kprintf("[GSPIXA] SKIP at y=%d: the block did not come back\n", chunk);
                break;
            }

            for (y = chunk; y < chunk + rows; y += (int)GSPIX_MAP_STEP) {
                uint32_t a_fe = 0u, a_ff = 0u, a_00 = 0u, a_other = 0u;
                char cls;
                c = 0;

                for (x = cx0; x + (int)GSPIX_MAP_STEP <= cx1 && c < (int)GSPIX_MAP_COLS;
                     x += (int)GSPIX_MAP_STEP) {
                    int all_zero = 1, any_sent = 0, dy, dx;

                    a_fe = a_ff = a_00 = a_other = 0u;
                    for (dy = 0; dy < (int)GSPIX_MAP_STEP; dy++) {
                        const uint32_t *row = buf + (size_t)(y + dy - chunk) * (size_t)width;

                        for (dx = 0; dx < (int)GSPIX_MAP_STEP; dx++) {
                            const uint32_t v = row[(size_t)x + (size_t)dx];

                            if (v == GSREAD_SENT) any_sent = 1;
                            if (v != 0u) {
                                all_zero = 0;
                                if (GSPIX_ALPHA(v) == 0xfeu) a_fe++;
                                else if (GSPIX_ALPHA(v) == 0xffu) a_ff++;
                                else if (GSPIX_ALPHA(v) == 0x00u) a_00++;
                                else a_other++;
                            }
                        }
                    }
                    n_cells++;
                    if (any_sent) cls = '?';
                    else if (all_zero) cls = 'k';
                    else if (a_other + a_00 > GSPIX_MAP_STEP * GSPIX_MAP_STEP / 2u) cls = 'o';
                    else if (a_other != 0u || a_00 != 0u || (a_fe != 0u && a_ff != 0u)) cls = 'm';
                    else if (a_fe) cls = 'e';
                    else cls = 'f';

                    if (a_00) n_0++;
                    line[c++] = cls;
                    if (cls == '?') n_q++;
                    else if (cls == 'k') n_k++;
                    else if (cls == 'e') n_e++;
                    else if (cls == 'f') n_f++;
                    else if (cls == 'm') n_m++;
                    else n_o++;
                }
                line[c] = '\0';
                ps2_kprintf("[GSPIXA] y=%-4d %s\n", y, line);
            }
        }

        ps2_kprintf("[GSPIXA] cells=%u: k(zero)=%u e(A=0xfe stream ink)=%u f(A=0xff upload)=%u "
                    "m=%u o=%u cells-holding-A=0x00=%u ?(sent)=%u blocks-unread=%u\n",
                    n_cells, n_k, n_e, n_f, n_m, n_o, n_0, n_q, bad);
    }

    /* Rung 21d: the same client by HUE, and the 12 words it is actually made of.
     *
     * The alpha map above cannot finish the job because the alpha byte is shared: chain 0's
     * `eegs_clear` writes 0xff000000 and a canvas upload writes 0xffRRGGBB, so its `f` says "an
     * opaque write" and cannot say which.  What it CAN no longer say is anything about coverage --
     * see the note above the alpha map for why its `k` count was the heap and not the page, and why
     * rung 23's identical 11,714 under the cut proves only that the same heap was still there.
     *
     * Hue is the map that answers the question the operator is actually asking.  The XMB's chains are
     * colour-coded by what they are: the gradient is blue (0x014c9d-ish), the clear is black, the
     * ribbon is dark grey, the icons and labels are near-white.  So "the wave is grey, not blue" is
     * a sentence this map can read directly: a client that is `B` where the gradient was recorded and
     * `D`/`G` elsewhere says the gradient's own words never reached the page, and a client that is
     * `B` but still looks grey says the page is right and the DISPLAY is not.  `M` marks a cell that
     * holds two classes, which is a real edge rather than a fog.  The histogram samples one pixel per
     * cell (24,000 of the 384,000) and prints the dozen words that recur most, so a band's owner can
     * be read as a word and then looked up in the [GSCHAIN] table instead of guessed at. */
    ps2_kprintf("[GSPXH] same client by HUE: `k` zero word, `D` dark (all channels < 24), "
                "`W` near-white (all >= 180), `B` blue-dominant, `R` red-dominant, `G` grey, "
                "`O` other, `M` a cell holding two of them, `?` undelivered.\n");

    {
        static const char hue_letters[] = "DWBRGO";    /* bit 1<<i of a pixel's class, in order */
        static uint32_t hist_w[256], hist_n[256];
        uint32_t n_cells2 = 0u, n_ovf = 0u, bad = 0u;
        int i;

        for (i = 0; i < 256; i++) { hist_w[i] = 0xFFFFFFFFu; hist_n[i] = 0u; }

        for (chunk = cy0; chunk < cy1; chunk += (int)GSREAD_BAND_ROWS) {
            const int rows = (cy1 - chunk < (int)GSREAD_BAND_ROWS) ? cy1 - chunk
                                                                  : (int)GSREAD_BAND_ROWS;

            if (gspeak_rows(buf, cap_px, width, chunk, rows) < 0) {
                bad++;
                ps2_kprintf("[GSPXH] SKIP at y=%d: the block did not come back\n", chunk);
                break;
            }

            for (y = chunk; y < chunk + rows; y += (int)GSPIX_MAP_STEP) {
                char cls;

                c = 0;
                for (x = cx0; x + (int)GSPIX_MAP_STEP <= cx1 && c < (int)GSPIX_MAP_COLS;
                     x += (int)GSPIX_MAP_STEP) {
                    uint32_t cell_bits = 0u;
                    int cell_zero = 1, cell_sent = 0, dy, dx;

                    for (dy = 0; dy < (int)GSPIX_MAP_STEP; dy++) {
                        const uint32_t *row = buf + (size_t)(y + dy - chunk) * (size_t)width;

                        for (dx = 0; dx < (int)GSPIX_MAP_STEP; dx++) {
                            const uint32_t v = row[(size_t)x + (size_t)dx];
                            const uint32_t r = v & 0xFFu, g = (v >> 8) & 0xFFu, b = (v >> 16) & 0xFFu;
                            const uint32_t hi = r > g ? (r > b ? r : b) : (g > b ? g : b);
                            const uint32_t lo = r < g ? (r < b ? r : b) : (g < b ? g : b);

                            if (v == GSREAD_SENT) { cell_sent = 1; continue; }
                            if (v == 0u) continue;
                            cell_zero = 0;

                            if (hi < 24u) cell_bits |= 1u;                            /* D dark */
                            else if (lo >= 180u) cell_bits |= 2u;                      /* W near-white */
                            else if (b >= r + 24u && b >= g + 24u) cell_bits |= 4u;    /* B blue */
                            else if (r >= g + 24u && r >= b + 24u) cell_bits |= 8u;    /* R red */
                            else if (hi - lo < 16u) cell_bits |= 16u;                  /* G grey */
                            else cell_bits |= 32u;                                     /* O other */
                        }
                    }

                    if (cell_sent) cls = '?';
                    else if (cell_zero) cls = 'k';
                    else if ((cell_bits & (cell_bits - 1u)) != 0u) cls = 'M';
                    else {
                        uint32_t bit = cell_bits, k = 0u;

                        while ((bit & 1u) == 0u) { bit >>= 1; k++; }
                        cls = hue_letters[k];
                    }
                    line[c++] = cls;
                    n_cells2++;

                    /* One sampled word per cell, for the histogram. */
                    if (!cell_zero && !cell_sent) {
                        const uint32_t v = buf[(size_t)(y + (int)GSPIX_MAP_STEP / 2 - chunk)
                                                     * (size_t)width +
                                               (size_t)x + (int)GSPIX_MAP_STEP / 2];
                        const uint32_t h = (v * 2654435761u) >> 24u;   /* 8-bit slot */
                        int probe;

                        for (probe = 0; probe < 8; probe++) {
                            const uint32_t s = (h + (uint32_t)probe) & 255u;

                            if (hist_w[s] == v) { hist_n[s]++; break; }
                            if (hist_w[s] == 0xFFFFFFFFu) { hist_w[s] = v; hist_n[s] = 1u; break; }
                        }
                        if (probe == 8) n_ovf++;   /* eight collisions: this sample is not in the table */
                    }
                }
                line[c] = '\0';
                ps2_kprintf("[GSPXH] y=%-4d %s\n", y, line);
            }
        }

        ps2_kprintf("[GSPXH] cells=%u of 16 px = %u px sampled at 1 word per cell; "
                    "table-overflow=%u blocks-unread=%u\n",
                    n_cells2, n_cells2 * GSPIX_MAP_STEP * GSPIX_MAP_STEP, n_ovf, bad);

        /* The dozen most frequent words, by repeated max selection over the 256 slots: no sort,
         * no allocation, and the table is small enough that twelve passes are nothing. */
        ps2_kprintf("[GSPXH] the client's most frequent words (0xAABBGGRR):\n");
        for (i = 0; i < 12; i++) {
            uint32_t best = 0u, bi = 0xFFFFFFFFu;
            int k;

            for (k = 0; k < 256; k++) {
                if (hist_w[k] == 0xFFFFFFFFu) continue;
                if (hist_n[k] > best) { best = hist_n[k]; bi = (uint32_t)k; }
            }
            if (bi == 0xFFFFFFFFu || best == 0u) break;
            ps2_kprintf("[GSPXH]   #%02d 0x%08x  %u cell-sample(s)\n", i + 1, hist_w[bi], best);
            hist_n[bi] = 0u;
        }
    }
}

/* LANE 19 rung 12a -- does THIS GS interpolate the three colours THIS STREAM hands it?
 *
 * Everything below the question has been answered by measurement.  The recorded group's shape draws:
 * rung 5's dense two-quadword REGLIST filled its box to the pixel, and rung 6's census counts
 * hundreds of thousands of client pixels that differ from their canvas.  The colours are not all the
 * same at the top of the pipeline either: `colchg=` counts changes in the vertex stream.  What the
 * operator sees is one flat plate where the XMB's four-corner gradient should be, and the flat word
 * that census named (0xfe9d4c01) is exactly the FIRST vertex of `xb_draw_bg()`'s quad scaled by its
 * alpha -- which is what a GS with IIP off, or with only one RGBAQ in its registers, paints.  The one
 * register between those two facts is the interpolation flag, and no rung so far has varied it
 * deliberately: rung 11 found `iip=24/25` at the PRIM declarations, which says the word was ASKED
 * for, not that the rasteriser honoured it or that the three colours arrived distinct.
 *
 * So this draws the pair directly through `eegs_triangle()` -- the same recorder, the same group, the
 * same PRE, the same stride as the XMB's own stream -- and not through the SPRITE poster every earlier
 * control used, because a SPRITE has no second vertex to interpolate with.  Box A is one triangle with
 * red at its bottom-left, green at its bottom-right and blue at its apex, asked with IIP=1.  Box B is
 * the same three vertices asked with IIP=0, and it is the control: it is expected to come back ONE
 * colour, and which of the three it comes back as names the vertex whose RGBAQ the GS kept.
 *
 *   A holds all three corners plus a field of words that are neither ....... the GS interpolates
 *                                                                            per-vertex RGBAQ
 *                                                                            through this exact
 *                                                                            group shape
 *   A is one corner colour, and B is the same one ........................... the flag is not what
 *                                                                            makes the page flat:
 *                                                                            the colours the three
 *                                                                            vertices carry are not
 *                                                                            three distinct words in
 *                                                                            the rasteriser, which is
 *                                                                            a packet question, or
 *                                                                            the XMB never asked --
 *                                                                            read `wide=`/`big=`'s
 *                                                                            three `c=` words next
 *   A is one colour and B is another ......................................... the flag is honoured,
 *                                                                            and the XMB's own
 *                                                                            triangles are not asking
 *                                                                            for it -- `sm=` in the
 *                                                                            [GSTS] row is then the
 *                                                                            answer, and it is an EE
 *                                                                            bug
 *   neither box holds a pixel that is not its canvas .......................... the recorded TRIANGLE
 *                                                                            lays nothing at all, in
 *                                                                            any shading mode, which
 *                                                                            retires every shading
 *                                                                            hypothesis at once
 *
 * It prints nothing between its own draw and the read, for the reason the buffer allocation above
 * gives: a console row repaints the band the control sits in.  A refused group is therefore counted
 * into `s_sh_refused` and reported after the read, not printed where it happened.
 *
 * MEASURED, 2026-10-18 run (`ctrl=1024/1024`, so the read was certified when this printed):
 *
 *   A iip=1 tot=2048 painted=960 red=960 green=0 blue=0 other=0 canvas=1088
 *   B iip=0 tot=2048 painted=945 red=0 green=0 blue=945 other=0 canvas=1103   2 groups, 0 refused
 *
 * So neither reading in the table above is the one that happened, and the pair says something none
 * of them predicted: the flag IS honoured (A and B come back different words, which retires "the
 * tag's PRIM is ignored" outright), the three RGBAQ words DO reach the register (box B paints the
 * KICK vertex's blue, which is only possible if the register held blue at the third XYZ2), the
 * geometry rasterises (960 px of a 2,048 px box is the triangle, to the pixel) -- and yet the shaded
 * box is ONE colour, bit-exactly v0's, with no intermediate word anywhere.  A and B together say the
 * GS reads the whole triangle's colour from one register and picks WHICH register by IIP: the first
 * vertex's when shading is asked for, the last one's when it is not.  That is a flat triangle twice
 * over, and it reproduces the XMB's plate exactly, because the plate's v0 is the word the census
 * found painted over 356,087 px.
 *
 * What this rung cannot decide is whether some OTHER packet shape makes this GS interpolate.  That was
 * rung 12c, and it ANSWERED: the shape ps2sdk's own Gouraud cube sends (`draw3d.c:11-53` +
 * `teapot.c:193-202`: an AD pair carrying PRIM and RGBAQ, then a `FLG=REGLIST`, `PRE=0`, NREG=2 chain
 * of {RGBAQ, XYZ2} pairs -- no ST, no NOP, no PRIM in the tag) lays a gradient on this GS, box C of
 * the run above.  So the recorder changes its group and the plate follows -- no EE-side pre-shading,
 * which was the alternative this rung left open.  What is still undecided is WHICH of C's three
 * differences from A is the one that matters, and that is rung 12d, because the recorder has to change
 * one thing it can say WHY about, not a shape it merely happens to resemble. */
static uint32_t s_sh_groups;
static uint32_t s_sh_refused;

/* ── LANE 19 rung 12g: the words that actually went out ───────────────────────────────────────────
 *
 * rung 12f made a claim the 12c..12e table says it has to honour: the recorder's untextured group IS
 * the hand-built chain -- `PRE=1`, PRIM in the tag, NREG=2, {RGBAQ, XYZ2}, three RGBAQ/XYZ2 pairs,
 * Q = 1.0f, the same three corners, the same synchronous sender, the same applied draw environment.  The
 * machine says otherwise: the re-run on the landed change (2026-10-18, `ctrl=1024/1024`, 4 censuses, all
 * four identical) still reads `A ... red=960 other=0 -> one FLAT colour R` while the hand-built box `D
 * ... other=609 -> INTERPOLATES`.  One of the premises under that claim is therefore false, and none of
 * them is decidable from the source any more -- the host suite already asserts the tag, the REGS literal,
 * the half order and the size law word for word.  So this captures the two packets AS THE SINK SAW THEM
 * and prints them, which is the only instrument that can tell "the recorder did not change" from "the
 * recorder changed and the difference lies elsewhere".
 *
 * RUNG 12g ANSWERED (2026-10-18, `build/repro/20261017T234101Z`, both runs identical): the two packets
 * carry the SAME header words -- tag 0x2405c00000008003, NLOOP=3, EOP=1, PRE=1, FLG=1(REGLIST), NREG=2,
 * PRIM=0x0b, REGS=0x51 -- eight words apiece, and the only words that differ are the three vertex XYZ2s,
 * which is the geometry: box A is at x 112..240 and box D at 536..624.  So the ENCODING is excluded as
 * the variable, and what is left is WHERE the triangle lands and WHO put it there.  Rung 12h therefore
 * crosses them -- the recorder's group is now also sent at D's coordinates and the hand chain at A's --
 * and this capture moves with it: both packets are now taken at BOX A'S coordinates, so the diff below
 * has no excuse left and a clean answer is literally the same bytes.
 *
 * The capture is eight 64-bit WORDS, which is the whole of either packet at this size -- a GIF quadword
 * is two of them, so word 0 is the tag's low half (where NLOOP, EOP, PRE, PRIM, FLG and NREG all live),
 * word 1 is the REGS list, and words 2..7 are the three RGBAQ/XYZ2 pairs.  Two spare slots cost nothing
 * and make a longer packet visible as a longer packet rather than as a truncation. */
#define GSLIVE_CAP_WORDS 8u
/* Rung 12i's send dump: 12h's cell G was the recorder's THIRD group of one session -- three transfers
 * sharing a buffer, cut at every PRIM change -- so the census had never seen what the recorder hands the
 * GIF for a triangle of its own, and had never SEPARATED the sender from the frame's order.  Every group
 * of this run's three one-triangle sessions is kept, in the order they leave, so a row can say "this
 * session sent one group and it was this packet" instead of leaving the count inferred.  Box A's group is
 * the one rung 12g compares the hand chain with, and because A now goes LAST its slot is not send 0, so
 * `s_cap_rec_at` names where it landed.
 *
 * Rung 12v moved that count: the recorder now hands its sink TWO groups per triangle -- the AD write that
 * programs PRIM, which is what 12u measured as the only thing that does program it, and then the vertex
 * chain.  Six of them now leave a three-session grid, so the array is six deep and every reader that wants
 * a box's CHAIN -- 12g's tag xor, 12o's vertex decode, 12u's replay -- gets the index of its first
 * REGLIST group, not of its first send.  `s_sh_sess_first[]` therefore changed meaning to that index, and
 * `s_sh_sess_owner[]` was added because the dumps need to attribute the preamble too. */
#define GSLIVE_CAP_SENDS 6u
static uint64_t s_cap_send[GSLIVE_CAP_SENDS][GSLIVE_CAP_WORDS];
static uint32_t s_cap_send_words[GSLIVE_CAP_SENDS];
static uint32_t s_cap_sends;
static uint32_t s_cap_rec_at;
#define s_cap_rec       (s_cap_send[s_cap_rec_at])
#define s_cap_rec_words (s_cap_send_words[s_cap_rec_at])
static uint64_t s_cap_hand[GSLIVE_CAP_WORDS];    /* box A's triangle built by the OTHER sender */
static uint32_t s_cap_hand_words;
static uint32_t s_sh_sess_first[GSLIVE_SH_N];    /* index in s_cap_send[] of this box's session's first VERTEX group */
static uint32_t s_sh_sess_groups[GSLIVE_SH_N];   /* groups this box's session handed its sink (chain + its PRIM write) */
static uint8_t  s_sh_sess_owner[GSLIVE_CAP_SENDS]; /* 1 + the box a captured send came from, 0 = no session of the census */

/* GIFTAG fields, read out of a captured word the way the GIF reads them.  The unit deliberately
 * duplicates `verify/tests/gs_group_view.h`: this row is a claim about what the machine was handed, and
 * reading it with a copy of the decoder rather than with the test's view is what keeps the test suite
 * and the census from agreeing with each other and not with the GS. */
static uint32_t gslive_nloop_of(const uint64_t *g) { return (uint32_t)(g[0] & 0x7FFFu); }
static uint32_t gslive_eop_of(const uint64_t *g) { return (uint32_t)((g[0] >> 15) & 1u); }
static uint32_t gslive_pre_of(const uint64_t *g) { return (uint32_t)((g[0] >> 46) & 1u); }
static uint32_t gslive_prim_of(const uint64_t *g) { return (uint32_t)((g[0] >> 47) & 0x7FFu); }
static uint32_t gslive_flg_of(const uint64_t *g) { return (uint32_t)((g[0] >> 58) & 3u); }
static uint32_t gslive_nreg_of(const uint64_t *g) { return (uint32_t)(g[0] >> 60); }
/* Rung 12o's packet decode: the exact inverse of `gs_vertex_coord16()` in src/drivers/ps2/ps2_gs_pack.c,
 * which writes a vertex axis as `((px * 16) + GS_XYOFFSET_PAGE) & 0xFFFF` into a 16-bit 12.4 field (X bits
 * 0-15, Y bits 16-31 of an XYZ2 quadword).  Subtracting the page back off therefore recovers the pixel the
 * encoder was handed -- valid for any px below GS_POSTER_MAX_PX (2047), where (px+2048)*16 still fits the
 * field, which every coordinate in this census is.  The census env sets win_x = win_y = 0, so XYOFFSET
 * itself is the plain page offset and no window term enters either side of that law.  In a two-register
 * {RGBAQ,XYZ2} group the XYZ2 words are the capture's indices 3, 5 and 7, one vertex per quadword. */
static int32_t gslive_xyz_axis(const uint64_t w, int is_y)
{
    const uint32_t f = (is_y) ? (uint32_t)((w >> 16) & 0xFFFFu) : (uint32_t)(w & 0xFFFFu);

    return (int32_t)(f >> 4) - 2048;
}

static uint32_t s_sh_box;                 /* which box the session now open draws; the sink tags its sends */

/* ── LANE 19 rung 12l: the one bit that says whose PRIM the transfer is drawn under ─────────────
 *
 * 12k's pictures split the six boxes by SHAPE, not by colour: box D and box B were laid as RECTS
 * (D = red x536-599 beside blue x600-623, every one of its 16 rows identical; B = red x256-319 beside
 * blue x320-382, 15 rows), while A and F were laid as correct apex-up triangles whose whole interior is
 * interpolated field, and C and E as a solid box with that same triangle carved into it.  The shape
 * tracks one thing only -- position: transfers 1 and 2 are rects, 3 and 4 box-plus-triangle, 5 and 6
 * triangle.  12g had already certified D's and A's bytes as identical, so the packet cannot explain it
 * and the latch can: `third_party/ps2sdk/common/include/gif_tags.h` names bit 46 exactly this way --
 * `GIF_PRE_DISABLE 0x00` = "Enable PRIM field output", `GIF_PRE_ENABLE 0x01` = "Disable PRIM field
 * output", with `GIF_SET_TAG` putting PRE at bit 46 and PRIM at 47..57.  So a PRE=1 tag does NOT write
 * the GS's PRIM register; it draws under whatever the last PRE=0 tag or PRIM write left there.  Every
 * group in this census leaves the recorder PRE=1, and the one PRE=0 thing before box D is the census's
 * own magenta SPRITE poster -- which is also why rung 12c's AD(PRIM,...) shape (C) was the one that
 * shaded: it asks for its own primitive.
 *
 * The rung is therefore one bit on one box.  Box D, the frame's first triangle transfer, goes out with
 * PRE cleared, so its own PRIM=0x0b is honoured and its 8 words stay byte-for-byte what 12g certified.
 * Box B is deliberately NOT touched: it is PRE=1 and now follows a tag that wrote TRI+IIP into the
 * register, so it is the propagation cell -- if the latch is the whole story B changes too, and if only
 * D changes then B's flatness is its own answer.  A keeps PRE=1 and keeps 12g's byte identity intact.
 *
 * Grading needs a SHAPE law and not the colour law, because a fixed box B is a FLAT TRIANGLE -- one
 * colour, and still not a field -- so `INTERPOLATES` cannot see that it got better.  The top row of a
 * box settles it: an apex-up triangle covers one or two pixels of the row its apex sits on, and a rect
 * covers the whole width, and that stays true whatever the shading flag does.  So each box's painted px
 * in the band's first row and its last row are tallied and printed, and the verdict reads the rect
 * count. */
static uint32_t s_pre0_box = GSLIVE_SH_N;   /* the box whose tag has PRE cleared; SH_N = no patch */
/* Rung 12m's second dial, and it exists because 12l's negative is CONDITIONAL.  Clearing PRE only
 * changes what a group draws if the PRIM register it would otherwise inherit DIFFERS from the TRI+IIP
 * the tag carries -- and this census never measured that register, so "PRE is not the variable" is only
 * proven against a latch that may already have held the right value.  12m removes the condition instead
 * of measuring the register: box D's tag now also has its PRIM FIELD (bits 47-57) zeroed, which is
 * POINT, flat, untextured, context 1, no FIX.  A tag that asks for points and still lays a solid 88x16
 * block says the field is not what rasterises this group; a tag that asks for points and lays points
 * says it is, and 12l's block was therefore laid by TRI+IIP, which moves the diagnosis to the vertices
 * and the clipping rather than to the primitive type.  One box, and the same 8 words otherwise. */
static uint32_t s_prim_pt_box = GSLIVE_SH_N; /* the box whose tag PRIM field is zeroed; SH_N = no patch */
/* Rung 12n's variable is the GEOMETRY, and it exists because of what 12l and 12m MEASURED together.  Both
 * fields that a GIF tag uses to say what to draw have now been changed on purpose on box D -- PRE (12l)
 * and the whole PRIM field (12m) -- and both times the captured tag proved the patch left the EE, and both
 * times box D laid the same 88x16 block, `top=88/88 painted=1408 RECT nofield`, byte-for-byte unchanged.
 * A measurement that refuses to move when two of its inputs are edited is not a measurement of those
 * inputs, so what has to be doubted is the thing every rung since 12i has assumed without testing: that the
 * block in D's window is D's GROUP's ink.  It may belong to another writer -- the poster, the environment
 * stream, the desktop's own upload -- and if so all of 12i's ORDER/PLACE/SESSION grid and both of 12l/12m's
 * negatives were read off someone else's pixels.
 * The one dial that settles it is the geometry: ask D's triangle 24 px wide at the LEFT of the 88-px window
 * the census reads, change nothing else, and count painted px in that window at an x that was never asked
 * for.  Ink that follows the ask is this group's; a block that holds while the ask moves is not.  So the
 * session records what each box was ASKED beside the window it is READ at, and the two being different is
 * this rung's experiment rather than a coordinate typo.  `0` means never asked -- the hand controls C, E
 * and F are drawn by the shape matrix and have no ask of their own, and the prints fall back to the window
 * for them so a missing ask cannot read as a mismatch. */
static int32_t s_sh_ask_x0[GSLIVE_SH_N], s_sh_ask_x1[GSLIVE_SH_N];
/* Rung 12o adds the other axis to that record, because 12n's answer -- the block held while the ask moved
 * in x -- has exactly two explanations and they are told apart by y.  Either this group's ink does not
 * follow its own vertices at all, or it follows them in one axis and not the other, which is what a
 * per-axis override (a CLAMP, a double-added XYOFFSET, a writer that keeps the y it is given and re-derives
 * x) looks like from a read-back.  So the session records the RECT it was asked for, and 12o asks D a rect
 * entirely below the band: if the block in the band goes away, the y dial bit; if ink also appears in the
 * asked rect, this group did draw.  The two are separate readings, and only the pair of them can say
 * whether the band's block is this group's ink at a wrong x or somebody else's ink entirely. */
static int32_t s_sh_ask_y0[GSLIVE_SH_N], s_sh_ask_y1[GSLIVE_SH_N];
/* Rung 12p's dial is the GIF seam itself, and it exists because 12o gave two answers at once and named
 * nothing.  Moving box D's ask 16 rows down made 384 red px fill the asked 24x16 bbox AND changed the
 * control box B from 12n's `top=128/128 px=1920 RECT` to `top=63/128 px=1073 PARTIAL` -- in all eight
 * censuses of both pinned runs, deterministically.  Neither reading can be attributed from that run: the
 * ink at the moved ask might be this transfer's rasterisation or a foreign writer that happens to paint
 * those rows, and B might be coupled to D's bytes or might be answering the instrument.
 * So this dial edits the one thing 12n and 12o left alone: whether the bytes go out at all.  D's session
 * runs, its group is captured and decoded exactly as 12o certified it, and then the sink returns WITHOUT
 * calling `ps2_gs_gif_send()`.  Every other box is asked, encoded and sent as in 12o, and the transfers the
 * GIF receives are the same ones in the same order.  Two questions with independent answers follow: ink at
 * the moved ask that survives the bytes being withheld is NOT this group's, and a B that returns to 12n's
 * number the moment D's bytes stop arriving is coupled to D's delivery -- which is the mechanism 12l's
 * PRE=1 latch pointed at, and the reason every per-box count here is really a whole-frame count.
 * `GSLIVE_SH_N` (the default, mask 0) hands everything to the GIF, so the ship route is untouched by this
 * file.
 * Rung 12p's answer turned the dial's meaning inside out, so 12q widens it into a mask.  Holding ONLY box
 * D's bytes left D's band block (1408 px) and D's moved ask (384 px) both standing at their 12o numbers,
 * while the one box whose group DID still go out -- B -- emptied completely (`painted=0`, its foreign blue
 * 945 included).  That says the per-box counts are not per-box: withholding one transfer's bytes rewrote a
 * neighbour's picture.  Two explanations survive and they need the opposite experiment.  Either the ink the
 * census counts is almost entirely the app's own XMB stream and no census triangle has ever rasterised --
 * in which case holding every recorder session changes nothing except the timing -- or the band is the
 * census's own ink and each box's count answers the whole frame.  So the dial is a MASK now: 12q holds all
 * three recorder sessions (A, B and D), sends nothing from this file's geometry, and keeps the hand-built
 * shape boxes C/E/F plus the moved ask as witnesses.  If a recorder box's counted ink stands while its own
 * bytes never reached the GIF, that ink was never the census's to lay. */
static uint32_t s_hold_gif_mask;               /* bit per box whose bytes the sink keeps to itself */
static uint32_t s_sh_held;                     /* groups captured and withheld, by box or in total */
/* Rung 12t's dial, the opposite of 12p/12q's: instead of withholding one box's bytes from a census that
 * still sends six things, it lets ONE session reach the GS and withholds everything else -- the magenta
 * control, the other two recorder sessions, and the hand chain C/E/F.  The question it answers is the one
 * 12s left open on a band certified clean: the recorder's group at box D lays 660 FLAT red px as a 44-wide
 * by 15-tall block, while the packet 12i captured as differing from it in exactly three position words and
 * no colour word lays 891 px of a correct apex-up gradient triangle at box A -- and 12j's spill says D's
 * block is really one continuous flat red rect over x384..579, wider than D's own 536..623 ask, reaching
 * through box C's whole window and both 16-px gutters beside it.  Two claims fit that, with opposite cures:
 * the packet alone rasterises flat and mis-shaped, in which case the recorder's own encoding or the GS state
 * it lands in is the bug; or a NEIGHBOUR's transfer drew the block and D only owns the part its window
 * happens to cover, in which case the packet is fine and the frame that carries it is not.  A census that
 * sends seven things cannot tell them apart, because every count in it is a sum of seven.  GSLIVE_SH_N means
 * the dial is off and the census runs 12i's whole grid. */
static uint32_t s_census_only = GSLIVE_SH_N;   /* < GSLIVE_SH_N: only that box's session is sent */
static uint32_t s_t_lone_n;                    /* the LONE census's band ink, kept for the grid to compare */
static uint32_t s_t_lone_field;
static uint32_t s_t_lone_bbox[4] = { GSLIVE_BB_NONE, GSLIVE_BB_NONE,
                                     GSLIVE_BB_NONE, GSLIVE_BB_NONE };
static uint32_t s_t_lone_done;                 /* 1 = a LONE census has been measured in this process */
/* Rung 12u's accumulators: the same lone packet replayed four times, each on a band the upload put back to
 * its canvas, each read before anything prints.  See `ps2_gslive_u_probe()` for what the four modes are and
 * what answer each one buys. */
static uint32_t s_u_ok;                          /* 1 = all four variants went out and came back readable */
static uint32_t s_u_read_ok[4];                  /* per variant: this variant's band read filled its words */
static uint32_t s_u_send[4];                     /* quadwords this variant handed the GIF, dial included */
static uint32_t s_u_n[4];                        /* band px that differ from the canvas */
static uint32_t s_u_corner[4];                   /* ... of those, the three corner words */
static uint32_t s_u_field[4];                    /* ... and the words that are none of them: interpolated px */
static uint32_t s_u_top[4];                      /* painted px in band row 0 INSIDE D's window: the shape key */
static uint32_t s_u_bbox[4][4];                  /* the whole band's ink extent per variant */
/* Rung 12q answered, and its answer re-opens the assumption every rung since 12a has been built on.  With
 * A, B and D all withheld (`to the bus=0`), A and B emptied exactly as their own deliveries predict (891 ->
 * 0 and 1073 -> 0), so those two boxes really are the census's ink -- while D's band block (1408 px) and
 * D's moved rect (384 red) stood at their 12o numbers with NO census byte ever reaching the GS for them.
 * One box is therefore the instrument's and one is somebody else's, and the only way to tell which is
 * which is to look at the page BEFORE this census puts anything on it.  That is what 12a's own note claims
 * -- "rows 560..575 hold nothing but its own control" -- but the census never tested it: `ctrl=1024` says
 * the magenta box came back whole, which says nothing about the other five windows or about the rows below
 * the band.  These hold the pre-draw tally, read once per census before the control is sent.
 *
 * One accumulator type, filled by the read loop's OWN class order -- the three corner words tested before
 * the canvas compare, the canvas compare before the field -- so a number from here and the number in the
 * `shade` row beside it are the same measurement taken at two times and nothing else differs.  `painted` is
 * derived as `tot - same`, exactly as every rung since 12a derives it.  `top` is 12l's shape law on row 0,
 * so a pre-existing block can be called a rect or an apex without waiting for 12k's picture.  `sent` counts
 * words the read never filled: a window with a non-zero `sent` has no answer, which must not be printable
 * as a blank one.  `s_pre_pass` numbers the censuses of THIS process, because the census re-runs on every
 * app open and only the first of them looks at a page this file has never drawn on -- at pass 2 a held block
 * could be this file's own earlier triangle, and the two readings would mean opposite things.
 *
 * Rung 12s adds the last four fields, because 12r's first look was real and its conclusion was still wrong.
 * `painted` cannot see whether the canvas holds the SAME corner word at the SAME position: a corner word is
 * counted before the canvas compare, so desktop chrome drawn in the palette's red or blue is indistinguishable
 * from this census's ink in that column.  `cv_r/cv_g/cv_b` therefore count the CANVAS words in the same window
 * and `match` counts the positions whose two words are equal -- the pixel-by-pixel agreement that says
 * "the page is the canvas here".  A window with corner words in the page and none in the canvas is a real
 * foreign GS writer; one where both hold them and `match == tot` is chrome, and chrome is what 12a..12r were
 * measuring.  `match` is a separate count on purpose, not `same`: `same` is what `painted` subtracts and the
 * two must stay comparable with the post-draw column. */
typedef struct {
    uint32_t tot, same, other, top, sent;
    uint32_t r, g, b;
    uint32_t cv_r, cv_g, cv_b, match;
} gslive_pre_t;

static uint32_t s_pre_pass;                      /* 1 = the first census this process has ever run */
static int s_pre_ok;                             /* the pre-draw read of the band came back GS_READ_BUS_OK */
static int s_pre_old_ok;                         /* the same, for the read of the strip 12a..12r used */
static gslive_pre_t s_pre[GSLIVE_SH_N];          /* the six box windows, before any census send */
static gslive_pre_t s_pre_old[GSLIVE_SH_N];      /* the same six x ranges at rows 560..575, 12s's control */
/* 12r's own control, and the row that decides whether the rung may be believed at all.  The measured
 * pre-draw read (build/repro/20261018T064640Z, census #1 of the process, `read=OK, words never filled: 0`)
 * came back with D's window ALREADY holding red 1024 + blue 384 as a whole-width RECT, D's moved ask already
 * holding 384 red, and C/E already 320/1088 -- while A, B and F came back clean.  That is either the finding
 * 12q forced (the two D windows are another writer's) or a read that is not looking at the page it claims
 * to.  Nothing in the rung so far separates those, so this control asks the read about a window whose ONLY
 * writer is this census and whose draw is known to come after the read: the magenta box.  It answered on the
 * first try (`magenta=0 painted=0` at build/repro/20261018T065254Z): the read IS a first look, and the block
 * is not this census's.  12s then named its author offline from that same run -- the ink stops at x599 and
 * x699, the Colour Test Bar's swatch edges, not at 623 and 719, the census's own box edges.  The control
 * stays, because it is the only thing that tells a first look from a late one. */
static uint32_t s_pre_ctrl_mag, s_pre_ctrl_paint;

static void ps2_gslive_sh_sink(void *user, const uint64_t *qw, uint32_t qwc)
{
    /* The recorder's own buffer is a `uint64_t` array, which the ABI guarantees eight bytes of, and
     * channel 2 reads sixteen.  Every real sender therefore stages a copy first -- `eegs_present.c`
     * does it into an aligned static -- so this sink does the same rather than DMA-ing words that were
     * never aligned to send. */
    static uint64_t s_pkt[2u * GSLIVE_SH_QWC_MAX] __attribute__((aligned(16)));
    uint32_t i;

    (void)user;
    if (qwc == 0u || qwc > GSLIVE_SH_QWC_MAX) {
        s_sh_refused++;
        return;
    }
    for (i = 0u; i < 2u * qwc; i++) s_pkt[i] = qw[i];
    /* Rung 12l's one bit, applied BEFORE the capture so every dump in this census describes the bytes
     * the machine was actually handed rather than the bytes the recorder meant to send. */
    if (s_sh_box < GSLIVE_SH_N && s_sh_box == s_pre0_box)
        s_pkt[0] &= ~((uint64_t)1u << 46);
    /* Rung 12m's field, on the same box and for the same reason: with PRE already 0 the tag's own PRIM
     * is the thing that ought to rule, so zeroing its 11 bits (op 0 = POINT, IIP 0, TME 0, USEST 0,
     * CONTEXT 0, FIX 0) is an ask the machine cannot be accused of never receiving.  Everything else in
     * the packet -- NLOOP, EOP, FLG, NREG, the REGS list, all six vertex words -- is left exactly as the
     * recorder built it, and 12g's byte identity of box A's two senders is untouched because A is not
     * patched. */
    if (s_sh_box < GSLIVE_SH_N && s_sh_box == s_prim_pt_box)
        s_pkt[0] &= ~(0x7FFull << 47);
    if (s_cap_sends < GSLIVE_CAP_SENDS) {
        /* Keep EVERY group, in the order it left, rather than the first one.  Rung 12i's question is
         * which group belongs to which session and what went out beside it, and a capture that kept
         * only send 0 could not answer either.  Copy before the send, because the recorder reuses its
         * buffer for the next group the moment this one is handed over. */
        for (i = 0u; i < 2u * qwc && i < GSLIVE_CAP_WORDS; i++)
            s_cap_send[s_cap_sends][i] = s_pkt[i];
        s_cap_send_words[s_cap_sends] = 2u * qwc;
        if (s_sh_box < GSLIVE_SH_N) s_sh_sess_owner[s_cap_sends] = (uint8_t)(s_sh_box + 1u);
        /* Rung 12v: a session now has two groups, and every reader of `s_sh_sess_first[]` wants the
         * chain -- 12g and 12l/12m read its tag, 12o decodes its vertex words, 12u replays it -- so the
         * index names the session's first REGLIST group.  A PACKED group is the AD write of PRIM and
         * never the thing being read; `s_cap_rec_at` is set from the same event, which is what keeps
         * 12g's identity a comparison of two chains rather than of a chain against a preamble. */
        if (s_sh_box < GSLIVE_SH_N && gslive_flg_of(s_pkt) == GS_FLG_REGLIST &&
            s_sh_sess_first[s_sh_box] >= GSLIVE_CAP_SENDS) {
            s_sh_sess_first[s_sh_box] = s_cap_sends;
            if (s_sh_box == 0u) s_cap_rec_at = s_cap_sends;
        }
    }
    if (s_sh_box < GSLIVE_SH_N) s_sh_sess_groups[s_sh_box]++;
    s_cap_sends++;
    /* Rung 12p, widened to a mask by 12q: the bytes stop here for every armed box.  Everything above -- the
     * staged copy, the tag dials, the capture and the per-session bookkeeping -- ran as it always did, so
     * the census still holds the packets it can decode and still knows which group each was; what the GS
     * never gets is the transfer itself.  `s_sh_groups` counts the groups that reached the bus, and
     * `s_sh_held` the ones this dial kept, so a row can say "one went out, two did not" instead of hiding
     * the difference. */
    if (s_sh_box < GSLIVE_SH_N && (s_hold_gif_mask & (1u << s_sh_box)) != 0u) {
        s_sh_held++;
        return;
    }
    ps2_gs_gif_send(s_pkt, qwc);
    s_sh_groups++;
}

/* One triangle, three known colours, at the page position the census will read back.  The vertex
 * order is the XMB's own -- v0 first, the kick on v2 -- because "which vertex wins when the flag is
 * off" is only answerable if the order matches the stream being diagnosed. */
static void ps2_gslive_sh_tri(int32_t x0, int32_t y1, int32_t x1, int32_t apex_x,
                              int32_t y0, uint32_t smooth)
{
    eegs_vertex_t v[3];
    eegs_tri_state_t st;

    v[0].x_px = x0;      v[0].y_px = y1;   v[0].rgba = GSQUAD_RED;
    v[1].x_px = x1 - 1;  v[1].y_px = y1;   v[1].rgba = GSQUAD_GREEN;
    v[2].x_px = apex_x;  v[2].y_px = y0;   v[2].rgba = GSQUAD_BLUE;
    v[0].z = v[1].z = v[2].z = 0u;
    v[0].s = v[1].s = v[2].s = 0.0f;
    v[0].t = v[1].t = v[2].t = 0.0f;

    st.smooth = smooth ? 1u : 0u;
    st.textured = 0u;
    st.blended = 0u;
    st.brighten = 0u;              /* named: `st` is a local, and rung 28 gave it a field */
    st.depth_test = 0u;
    eegs_triangle(v, st);
}

/* Rung 12i: the recorder draws ONE triangle per session, and the census puts those sessions at
 * opposite ends of the frame.
 *
 * 12h's grid printed SENDER, but the same run says something else just as cleanly: the recorder's three
 * groups were triangle transfers 1, 2 and 3 and the hand chain's four were 4, 5, 6 and 7, so "the first
 * three triangles of this frame are flat and the next four lay a field" fits every one of the eight
 * counts as well as "the recorder is flat and the hand chain shades".  Nothing in that run separates
 * them, and the two answers have opposite cures -- a sender answer means the recorder's own send is the
 * bug, an order answer means the packet is fine and the frame that carries it is not.
 *
 * This rung separates them by moving each sender across the split while holding one box fixed.  Box A's
 * triangle is now asked LAST, after three hand triangles, which is the position and the box where rung
 * 12h measured the hand chain interpolate (other=890) and the box where the recorder's ask at the front
 * of the frame is measured FLAT four times over -- so A-early against A-late is one encoding, one box,
 * one variable: where in the frame it went out.  Box D's group moves to the FRONT, as the frame's first
 * triangle transfer, where 12h's cell G had it third.  And box A gets one writer again: the hand chain
 * that overdrew it in 12h is built below and never sent, so `painted=` on A's row is the recorder's
 * triangle alone rather than whichever of two senders covered more of the rect.
 *
 * One triangle per session, not three per session: `gs_rec_prim()` closes the open group whenever the
 * PRIM word changes, so A (IIP=1), B (IIP=0) and D (IIP=1) were three groups of one session and every
 * group after the first left on a cut rather than on `gs_rec_end()`.  A session per box makes each group
 * the first and only thing its session ever handed the GIF, and makes every one of them leave the same
 * way. */
static void ps2_gslive_shading_session(uint32_t box, int32_t x0, int32_t x1,
                                       int32_t y0, int32_t y1, uint32_t smooth)
{
    /* Rung 12t: in a LONE census every other box is withheld BEFORE its ask is even recorded, so a skipped
     * box stays `never asked` in 12n's bookkeeping and its `out=` column keeps its zero meaning. */
    if (s_census_only < GSLIVE_SH_N && box != s_census_only) return;

    s_sh_box = box;
    /* Rung 12n: record the span this box was ASKED for, as distinct from the span the census READS, so a
     * narrowed ask is legible to the read loop instead of being inferred from the picture.  One triangle
     * per session (12i's rule), so one record per box per census; `ps2_gslive_shading_reset()` puts every
     * box back to 0 = never asked, which is what makes the hand controls fall back to their window.
     * Rung 12o extends the record to the other axis, because 12n's answer needs the y dial read too. */
    if (box < GSLIVE_SH_N) {
        s_sh_ask_x0[box] = x0;
        s_sh_ask_x1[box] = x1;
        s_sh_ask_y0[box] = y0;
        s_sh_ask_y1[box] = y1;
    }
    /* Rung 12g's identity is box A's chain against the hand chain built at A's coordinates, and A's
     * session is now the frame's LAST, so its slot in the capture array is wherever the count stands
     * when it opens -- not send 0.  Since rung 12v that slot is named in the sink, at the moment A's
     * REGLIST group is copied, because a session's first send is now its PRIM write and the two are
     * different packets with different words. */
    eegs_frame_begin(ps2_gslive_sh_sink, 0);
    ps2_gslive_sh_tri(x0, y1 - 1, x1, (x0 + x1) / 2, y0, smooth);
    eegs_frame_end();
}

static void ps2_gslive_shading_reset(void)
{
    uint32_t b;

    s_sh_groups = 0u;
    s_sh_refused = 0u;
    s_sh_held = 0u;
    s_sh_box = GSLIVE_SH_N;
    s_pre0_box = GSLIVE_SH_N;
    s_prim_pt_box = GSLIVE_SH_N;
    s_hold_gif_mask = 0u;         /* disarmed: the sink hands every group to the GIF */
    for (b = 0u; b < GSLIVE_SH_N; b++) {
        /* 0 = no session has asked anything at this box yet (rung 12n's bookkeeping). */
        s_sh_ask_x0[b] = 0;
        s_sh_ask_x1[b] = 0;
        s_sh_ask_y0[b] = 0;
        s_sh_ask_y1[b] = 0;
    }
    s_cap_sends = 0u;
    /* Rung 12v: the sink names A's chain slot when A's session opens, so the reset value has to be a slot
     * that is valid to read but empty to describe -- the last one.  A LONE census never opens A, and there
     * 12g reads words=0 and prints its own `SKIP -- a packet was not captured`, rather than reading box
     * D's preamble and calling it box A's triangle. */
    s_cap_rec_at = GSLIVE_CAP_SENDS - 1u;
    s_cap_hand_words = 0u;
    for (b = 0u; b < GSLIVE_CAP_SENDS; b++) {
        s_cap_send_words[b] = 0u;
        s_sh_sess_owner[b] = 0u;
    }
    for (b = 0u; b < GSLIVE_SH_N; b++) {
        s_sh_sess_first[b] = GSLIVE_CAP_SENDS;   /* no vertex chain of this box's session captured yet */
        s_sh_sess_groups[b] = 0u;
    }
}

/* ── LANE 19 rung 12c/12d: the shape that shades, and which bit of it does it ────────────────
 *
 * rung 12a's answer was that the recorder's group is not the suspect: through it, an IIP=1 ask paints
 * its FIRST vertex's colour over the whole triangle and an IIP=0 ask paints its LAST one's, which
 * says the tag's PRIM is read, the per-vertex RGBAQ words arrive, the geometry rasterises -- and the
 * interpolation never happens.  What that left undecided is whether the encoding is a variable at
 * all, and rung 12c settled it by sending the same three coloured vertices in the shapes other
 * people's GS code uses and reading back which one laid a field of intermediate words.  MEASURED,
 * 2026-10-18, `ctrl=1024/1024`, 3 shapes sent, 0 refused:
 *
 *   C  ps2sdk's own Gouraud cube: `draw_prim_start()`/`draw_prim_end()` in draw3d.c build an AD pair
 *      carrying PRIM then RGBAQ (NLOOP=2, PACKED, PRE=0, EOP=0) and then a `FLG=REGLIST`, `PRE=0`,
 *      NREG=2 chain whose REGS list is {RGBAQ, XYZ2} -- teapot.c:193-202 and cube.c:185-194 fill that
 *      chain with `prim.shading = PRIM_SHADE_GOURAUD`.  No NOP, no ST, no PRIM in the tag, and the
 *      colour written immediately before the kick it belongs to.  THIS IS THE ONE THAT SHADED: 840 of
 *      its 1,121 painted px are words that are none of its three corners, so an encoding that
 *      interpolates on this GS exists and a recorder change can be the fix.
 *   D  the recorder's THEN dense four-register chain -- rung 12f retired it -- with PRIM INSIDE the loop
 *      instead of in the tag:
 *      REGLIST, PRE=0, NREG=4, REGS={PRIM, RGBAQ, ST, XYZ2}.  Flat -- red=1024 AND blue=896 in one
 *      box, which is what a per-loop PRIM looks like: each of the first two vertices kicks a triangle
 *      of its own and the third XYZ2 closes a degenerate one.  Answered: PRE=0 does not rescue a
 *      four-register list, so PRE is off the list of variables.
 *   E  shape C's chain with FLG=PACKED and the PRIM in the tag: PRE=1, NLOOP=3, NREG=2.  Flat at
 *      blue=448 -- it paints, which retires rung 5's "multi-register PACKED draws nothing" as a
 *      finding about this port's NREG=4 group rather than about PACKED -- but it does not shade, so
 *      PACKED is off the list too.
 *
 * C vs A is therefore three differences written as one shape: the chain's REGS contents
 * ({RGBAQ,XYZ2} against {NOP,RGBAQ,ST,XYZ2}), PRE, and how PRIM is delivered.  D and E remove PRE and
 * FLG from that list and leave the two things a recorder change has to be able to name, which is what
 * rung 12d measured -- four boxes, C, D and E kept as the rows that prove a gradient is still
 * readable.  MEASURED, 2026-10-18 second run, `ctrl=1024/1024`, 2 + 4 groups sent, 0 refused, and the
 * same words on all three censuses of that run:
 *
 *   D-slot  C's chain with NO AD group: `FLG=REGLIST`, `PRE=1`, PRIM in the tag, the same NREG=2
 *           {RGBAQ, XYZ2} list.  SHADED: other=609 of painted=1,408.  So the AD write is NOT required
 *           and the recorder can keep the tag it already emits -- the fix is the REGS list alone.
 *   E-slot  C's AD group and a NREG=3 {RGBAQ, ST, XYZ2} chain -- `DRAW_STQ_REGLIST`, draw3d.h:33-37,
 *           ps2sdk's own textured Gouraud list -- plus the trailing pad texture.c:259-263 writes for
 *           it.  SHADED: other=609 of painted=1,145.  An ST slot between the colour and the kick does
 *           not cost the interpolation, so one chain can carry both the XMB's textured icons and its
 *           gradient plate.
 *   F-slot  E-slot with FOG in the middle slot instead of ST, the parity control E needed: a
 *           three-register loop is one and a half quadwords, so E changed both the register in the
 *           loop's second slot AND the loop's parity, and a flat box there would have said only "one
 *           of those two".  F shaded (other=441), and since E shaded too, the control was never going
 *           to be needed -- which is what lets F's slot be reused for the question the recorder asks
 *           NEXT, below.
 *
 * One thing this table measured and cannot yet explain, recorded because it changes how the rows are
 * to be read: C, D and E paint MORE than the triangle they ask for.  D's box holds 1,408 px and all of
 * them differ from the canvas, and E 1,145 of 1,408, where each box's own triangle is 704; C overruns
 * by 161.  A and B, drawn through the recorder, cover 960 and 945 of 2,048 -- the triangle, to the
 * pixel.  So the hand-built packets land geometry none of them asked for, and this census has no
 * instrument that says which pixels of a box are its triangle.  It does not touch the shading answer --
 * `other` counts 600-odd words that are neither corner in four boxes and EXACTLY ZERO in the two that
 * use the recorder's list, and no residue mechanism explains a difference that clean -- but it does
 * mean the gate on the recorder change is box A, whose geometry is measured clean, and not the box
 * coverage of C, D or E.
 *
 *   F-slot, rung 12e  E's three-register chain with the RECORDER'S OWN tag: `PRE=1`, PRIM in the tag,
 *           no AD group, NREG=3 {RGBAQ, ST, XYZ2} and the same pad.  D proved PRE=1 carries a
 *           two-register chain and E proved a three-register chain carries ST, but each of them holds
 *           the other one's variable: D is 2 regs + PRE=1, E is 3 regs + PRE=0-with-AD.  A recorder
 *           that shrinks its TEXTURED list to three registers has to do both at once, and that
 *           combination is the one box on this table that has never been drawn.  If F shades, the
 *           textured group sheds its NOP too and a vertex is one and a half quadwords; if it is flat,
 *           the textured path keeps a two-or-four-register list and the untextured one alone changes.
 *
 * rung 12e ANSWERED, 2026-10-18: F shaded (`other=441`), so both lists shed their NOP and the recorder
 * changed -- rung 12f, `src/drivers/ps2/ps2_gs_rec.c` + `.h`, certified host-side by `test-ps2-gs-rec`
 * 233 checks, `test-eegs-raster` 253 and `test-eegs-present` 136, all with a falsifying control run for
 * each new claim.  An untextured group is now `PRE=1`, PRIM in the tag, NREG=2, REGS={RGBAQ, XYZ2}; a
 * textured one is the same tag with NREG=3, {RGBAQ, ST, XYZ2} and the odd-chain pad.
 *
 * Which is what made THIS table's first row a question, and rung 12h is the run that answered it.  Box A
 * goes through the production recorder, and after 12f its packet is the hand chain's packet word for
 * word: same tag (`NLOOP=3`, `EOP=1`, `PRE=1`, PRIM 0x0B, `FLG=REGLIST`, `NREG=2`), same REGS literal
 * 0x51, same three RGBAQ/XYZ2 halves in the same order, same three corners -- and rung 12g printed that
 * identity as MEASURED, at one box, `differ=0x00`, all eight words (2026-10-17T23:56:05Z, `ctrl=1024`,
 * 4 censuses, both runs identical).  Rung 12h then crossed the sender with the place and the grid came
 * out clean: recorder@A FLAT (painted=960), hand@A INTERPOLATES (other=890), hand@D INTERPOLATES
 * (other=609), recorder@D FLAT (painted=1408, other=0).  It printed SENDER, and the encoding is closed.
 *
 * What 12h did NOT close is the thing its own frame order confounds with the sender.  Read the eight
 * transfers in the order they went out: the recorder's A, B and D were triangle transfers 1, 2 and 3 --
 * each group leaving on the next one's PRIM change, or on `gs_rec_end()` -- and the hand chain's C, A, E,
 * F were 4, 5, 6 and 7.  Every flat box is in the first three and every gradient in the last four, so
 * "the front of this frame cannot shade" explains the table exactly as well as "the recorder cannot
 * shade", and the two answers have opposite cures: a sender answer indicts the recorder's own send, an
 * order answer leaves the packet and the code alone and indicts the frame that carries it.
 *
 * Rung 12i separates them by moving one sender across the split while holding one box still.  Box A is
 * now asked at BOTH ends -- its group LAST, after three hand triangles, and box D's FIRST, before any --
 * and each goes out alone in a session of its own, so nothing follows it, nothing precedes it inside its
 * session, and every group leaves on a `gs_rec_end()` rather than on a cut.  A's early answer is 12h's
 * measured constant (FLAT, four censuses, painted=960) and A's late answer is this run's; the two share a
 * box, a packet and an ask, and differ in one thing only, which is where in the frame they went out.
 *
 * Every box asks IIP=1, TME=0, ABE=0, CTXT=0, FIX=0 -- except B, which asks IIP=0, and is still expected
 * to come back one flat corner colour (measured blue, v2, the kick vertex) because a read that cannot
 * tell a gradient from a corner cannot grade any of the cells either.  Like rung 12a, these draw inside
 * the magenta box's certified band and print nothing until the page has been read. */
static uint32_t s_shape_sent;
static uint32_t s_shape_refused;

static void ps2_gslive_corners(const int32_t v[6], uint64_t rgba[3], uint64_t xyz[3])
{
    static const uint32_t c[3] = { GSQUAD_RED, GSQUAD_GREEN, GSQUAD_BLUE };
    int i;

    for (i = 0; i < 3; i++) {
        /* The same four bytes `emit_vertex()` hands `gs_rec_color()`: the corner colours are written
         * as the page holds them, so a pixel that comes back equal to GSQUAD_RED is red by the read's
         * own unit and not by a swap this function would have had to make twice. */
        rgba[i] = gs_pack_rgbaq(c[i] & 0xFFu, (c[i] >> 8) & 0xFFu,
                                (c[i] >> 16) & 0xFFu, (c[i] >> 24) & 0xFFu, GS_Q_ONE);
        xyz[i] = gs_pack_xyz2(gs_vertex_coord16(v[2 * i]),
                              gs_vertex_coord16(v[2 * i + 1]), 0u);
    }
}

static uint32_t ps2_gslive_shape_packet(int shape, const int32_t v[6], uint64_t *qw)
{
    const uint64_t prim = gs_pack_prim(GS_OP_TRI, 1u, 0u, 0u, 0u, 0u);
    const uint64_t st = gs_pack_st(0.0f, 0.0f);
    const uint32_t chain2[2] = { GS_NIB_RGBAQ, GS_NIB_XYZ2 };
    const uint32_t chain3[3] = { GS_NIB_RGBAQ, GS_NIB_ST, GS_NIB_XYZ2 };
    const uint32_t pre = (uint32_t)((shape == 1 || shape == 3) ? 1u : 0u);
    uint64_t rgba[3], xyz[3];
    uint32_t n = 0u;
    int i;

    ps2_gslive_corners(v, rgba, xyz);

    /* C's AD group, which shape 2 repeats and shapes 1 and 3 withhold: draw3d.c:11-31 writes PRIM and
     * the first RGBAQ as AD pairs under one PACKED tag whose EOP is CLEAR, because the chain that
     * follows is the part that ends the transfer. */
    if (shape == 0 || shape == 2) {
        const uint32_t ad[1] = { GS_NIB_AD };

        qw[n++] = gs_pack_giftag(2u, 0u, 0u, 0u, GS_FLG_PACKED, 1u);
        qw[n++] = gs_pack_regs(ad, 1u);
        qw[n++] = prim;                qw[n++] = GS_GP_PRIM;
        qw[n++] = rgba[0];             qw[n++] = GS_GP_RGBAQ;
    }

    /* One difference between 0 and 1, and between 2 and 3: where PRIM comes from.  With `pre` set the
     * tag's own 11-bit PRIM field programs the register at packet start, which is how this port's
     * recorder delivers it, so those two chains ride the recorder's tag. */
    if (shape == 0 || shape == 1) {
        qw[n++] = gs_pack_giftag(3u, 1u, pre, pre ? prim : 0u, GS_FLG_REGLIST, 2u);
        qw[n++] = gs_pack_regs(chain2, 2u);
        for (i = 0; i < 3; i++) { qw[n++] = rgba[i]; qw[n++] = xyz[i]; }
    } else {
        /* Three registers to a loop, which is one and a half quadwords, so the nine data words stop
         * half way through a quadword and the GIF's transfer unit does not.  texture.c:259-263 appends
         * `if ((u32)dw % 16) *dw++ = 0;` for exactly that -- the pad lands in a fourth slot that no
         * nibble of a three-nibble REGS list names, so nothing reads it -- and draw2d.c:409 uses the
         * same idiom for the same reason. */
        qw[n++] = gs_pack_giftag(3u, 1u, pre, pre ? prim : 0u, GS_FLG_REGLIST, 3u);
        qw[n++] = gs_pack_regs(chain3, 3u);
        for (i = 0; i < 3; i++) { qw[n++] = rgba[i]; qw[n++] = st; qw[n++] = xyz[i]; }
        qw[n++] = 0u;
    }
    return n / 2u;
}

static void ps2_gslive_shape_tri(int shape, int x0, int x1)
{
    static uint64_t s_qw[2u * GSLIVE_SH_QWC_MAX] __attribute__((aligned(16)));
    int32_t v[6];

    if (s_census_only < GSLIVE_SH_N) return;   /* rung 12t: a LONE census sends one group, this is not it */

    v[0] = x0;             v[1] = GSLIVE_SH_Y1 - 1;
    v[2] = x1 - 1;         v[3] = GSLIVE_SH_Y1 - 1;
    v[4] = (x0 + x1) / 2;  v[5] = GSLIVE_SH_Y0;

    {
        const uint32_t q = ps2_gslive_shape_packet(shape, v, s_qw);

        if (q == 0u || q > GSLIVE_SH_QWC_MAX) {
            s_shape_refused++;
            return;
        }
        ps2_gs_gif_send(s_qw, q);
        s_shape_sent++;
    }
}

static void ps2_gslive_shape_matrix(void)
{
    s_shape_sent = 0u;
    s_shape_refused = 0u;

    /* The three controls, at the coordinates that measured them: shape 0 is box C (ps2sdk's AD pair plus
     * a PRE=0 two-register chain), shape 2 is box E (the same with an ST slot), shape 3 is box F (the
     * recorder's own tag with the three-register chain).  These are the frame's triangle transfers 3, 4
     * and 5 -- in front of them go the recorder's D and B sessions, behind them the recorder's A session,
     * and box A's hand chain is built by `ps2_gslive_shape_hold()` instead of drawn, because rung 12h's
     * overdraw of A (other=890) made a two-writer rect of the one box whose early/late pair decides this
     * rung. */
    ps2_gslive_shape_tri(0, GSLIVE_SH_C_X0, GSLIVE_SH_C_X1);
    ps2_gslive_shape_tri(2, GSLIVE_SH_E_X0, GSLIVE_SH_E_X1);
    ps2_gslive_shape_tri(3, GSLIVE_SH_F_X0, GSLIVE_SH_F_X1);
}

/* Rung 12i: build box A's triangle the way the hand builder builds it, and DO NOT SEND IT.
 *
 * Rung 12g's claim is an identity between two packets -- the recorder's group for box A and this chain --
 * and the census has to keep printing both of them to hold that claim open on every run.  12h proved the
 * identity by drawing the hand chain over box A as well, which put two writers in one rect: its read
 * describes whichever one covered more pixels, and the recorder's answer there is not separable from it.
 * 12i needs box A as a single-writer rect -- A early against A late is the rung's whole measurement -- so
 * the hand chain stops being drawn and starts being captured.  Its eight words go to `s_cap_hand` exactly
 * as 12h sent them, and the row that compares them with the recorder's captured A group is unchanged: a
 * gradient at A now belongs to one sender and so does a flat colour. */
static void ps2_gslive_shape_hold(int shape, int x0, int x1)
{
    static uint64_t s_qw[2u * GSLIVE_SH_QWC_MAX] __attribute__((aligned(16)));
    int32_t v[6];
    uint32_t c, q;

    v[0] = x0;             v[1] = GSLIVE_SH_Y1 - 1;
    v[2] = x1 - 1;         v[3] = GSLIVE_SH_Y1 - 1;
    v[4] = (x0 + x1) / 2;  v[5] = GSLIVE_SH_Y0;

    q = ps2_gslive_shape_packet(shape, v, s_qw);
    if (q == 0u || q > GSLIVE_SH_QWC_MAX) {
        s_shape_refused++;
        return;
    }
    for (c = 0u; c < 2u * q && c < GSLIVE_CAP_WORDS; c++) s_cap_hand[c] = s_qw[c];
    s_cap_hand_words = 2u * q;
}

/* ── LANE 19 rung 12u: what programs a lone triangle's TYPE and its IIP? ──────────────────
 *
 * 12t answered 12s's open question, and answered it against this lane's own verdict label.  Box D's recorder
 * group, sent with NOTHING else in the census on a band the upload had just put back to its canvas, lays
 * ONE pixel -- at (580,32), which is the packet's own apex vertex v2.  The same eight words in 12i's grid lay
 * 660 flat red px, and the grid's band as a whole lays 6803 px over x117..788.  So the block is not the
 * packet's answer: a lone group does not even make a triangle.  That is what `PRE=1` means -- GIF_PRE_ENABLE
 * disables the tag's PRIM field output -- so the group's vertices are kicked under whatever OP and IIP the
 * PRIM register already held, and in a lone census nobody wrote it.
 *
 * The four variants below make that register write the ONLY variable, with no neighbour anywhere:
 *
 *   V0  the captured bytes, undialled: PRE=1, the tag's field reading TRI+IIP.  This is 12t's census redone
 *       inside the same strip reset, so the rung's own baseline is measured rather than inherited.
 *   V1  the same bytes with bit 46 CLEARED, so the tag's own 11-bit field is written at packet start.  If a
 *       triangle appears, the field rules when it is allowed to, and the cure is the recorder's PRE bit.
 *       12l dialed exactly this and got "no change" -- on the Colour Test Bar, which 12s has since proved
 *       was chrome, so that negative is void and gets re-measured here on a certified band.
 *   V2  a ps2sdk-shaped AD(PRIM) write of the SAME ask (TRI, IIP set) ending its own transfer, then the
 *       captured group as it came: the shape box C has always drawn with and 12c watched shade.
 *   V3  V2's preamble with IIP CLEAR and nothing else changed: the falsifying control on V2.  If the field
 *       in V2 is real, V3 lays the same triangle without it.
 *
 * V1 versus V2 says whether the tag field or an AD write programs OP; V2 versus V3 says whether IIP in that
 * word is what turns interpolation on.  Each variant gets its own upload, its own send, its own band read
 * and no print between the two, which is the census's standing law. */
static void ps2_gslive_u_band(uint32_t *buf, const uint32_t *canvas, uint32_t width, uint32_t slot)
{
    uint32_t x, r;

    s_u_n[slot] = 0u;
    s_u_corner[slot] = 0u;
    s_u_field[slot] = 0u;
    s_u_top[slot] = 0u;
    for (x = 0u; x < 4u; x++) s_u_bbox[slot][x] = GSLIVE_BB_NONE;

    for (r = 0u; r < (uint32_t)GSLIVE_PIC_ROWS; r++) {
        const uint32_t *row = buf + (size_t)r * width;
        const uint32_t *crow = canvas + (uint32_t)(GSLIVE_SH_Y0 + r) * width;

        for (x = 0u; x < width; x++) {
            /* 12s's class order, kept: a sentinel means the read did not land, and the canvas compare comes
             * BEFORE the corner-word test so desktop chrome in the palette's red is never counted as ink. */
            if (row[x] == GSREAD_SENT || row[x] == crow[x]) continue;
            s_u_n[slot]++;
            gslive_bbox(s_u_bbox[slot], x, (uint32_t)GSLIVE_SH_Y0 + r);
            if (row[x] == GSQUAD_RED || row[x] == GSQUAD_GREEN || row[x] == GSQUAD_BLUE) s_u_corner[slot]++;
            else s_u_field[slot]++;
            if (r == 0u && x >= (uint32_t)GSLIVE_SH_D_X0 && x < (uint32_t)GSLIVE_SH_D_X1) s_u_top[slot]++;
        }
    }
}

/* Hand D's captured group to the GIF with this variant's dial on it, and return the quadwords sent for it
 * (the AD preamble is counted in `s_u_send` because it is part of what this variant put on the bus).
 *
 * Rung 12v moved the census this replay reads from: the recorder now writes PRIM in front of every chain,
 * so by the time the probe runs the register holds the triangle the frame drew, and a bare replay would
 * draw a triangle on the strength of that write rather than on the strength of nothing.  V0 therefore gets
 * its own neutralising write -- PRIM=POINT, an OP that cannot make a triangle from three kicks -- so that
 * "what does the chain do with no ask of its own" stays a question with an answer.  V1 needs no preamble:
 * clearing PRE lets its own tag write PRIM, which is the dial. */
static uint32_t ps2_gslive_u_variant(uint32_t mode, const uint64_t *g, uint32_t words)
{
    static uint64_t s_qw[2u * GSLIVE_SH_QWC_MAX] __attribute__((aligned(16)));
    static uint64_t s_ad[4] __attribute__((aligned(16)));
    const uint32_t ad[1] = { GS_NIB_AD };
    uint32_t i;

    if (mode == 0u || mode >= 2u) {
        /* NLOOP=1, EOP=1, PACKED, PRE=0, one AD slot: the value lands in PRIM and the transfer ends.  Same
         * four words ps2_gslive_shape_packet() builds for box C, with EOP raised because nothing follows. */
        s_ad[0] = gs_pack_giftag(1u, 1u, 0u, 0u, GS_FLG_PACKED, 1u);
        s_ad[1] = gs_pack_regs(ad, 1u);
        s_ad[2] = (mode == 0u) ? gs_pack_prim(GS_OP_POINT, 0u, 0u, 0u, 0u, 0u)
                               : gs_pack_prim(GS_OP_TRI, (mode == 2u) ? 1u : 0u, 0u, 0u, 0u, 0u);
        s_ad[3] = GS_GP_PRIM;
        ps2_gs_gif_send(s_ad, 2u);
    }
    for (i = 0u; i < words; i++) s_qw[i] = g[i];
    if (mode == 1u) s_qw[0] &= ~((uint64_t)1u << 46);   /* PRE=0: let the tag's own field rule */
    ps2_gs_gif_send(s_qw, words / 2u);
    return (mode == 1u) ? (words / 2u) : (words / 2u) + 2u;
}

static void ps2_gslive_u_probe(uint32_t *buf, const uint32_t *canvas, uint32_t width)
{
    uint32_t slot, words, v, i;

    s_u_ok = 0u;
    /* Only the LONE census is replayable: in 12i's grid `s_cap_send` holds six recorder groups -- two per
     * session since 12v -- and the chain at D's session index would be drawn beside five others, which is
     * the state this rung exists to exclude. */
    if (s_census_only != 3u) return;
    if (s_sh_sess_first[3u] >= GSLIVE_CAP_SENDS) return;
    slot = s_sh_sess_first[3u];
    words = s_cap_send_words[slot];
    /* Rung 12v: the index above is a vertex chain, not the session's first send, and this is the replay
     * that has to say what a chain does on its own -- V0 is "these eight words and nothing in front of
     * them".  A group with no vertices in it would make that sentence false, so the gate now asks the
     * chain's own two questions: its tag says REGLIST, and it is long enough to hold three vertices.
     * Four halves is what box D's AD write of PRIM measures, and replaying that would report "no ink"
     * for all four variants and read as a verdict about the GS. */
    if (words < 8u || words > GSLIVE_CAP_WORDS || (words & 1u) != 0u) return;
    if (gslive_flg_of(s_cap_send[slot]) != GS_FLG_REGLIST) return;

    for (v = 0u; v < 4u; v++) {
        /* The order is part of the instrument: V0 goes out FIRST so no later variant's own PRIM write can be
         * what it draws under, and V1/V2/V3 each program PRIM before their kick, so nothing they lay is
         * inherited.  A probe that ran V3 then V0 would report "the group lays a triangle alone" for V0 on
         * the strength of V3's write. */
        s_u_read_ok[v] = 0u;
        ps2_gs_upload(0, GSLIVE_SH_Y0, PS2_SCREEN_WIDTH, GSLIVE_PIC_ROWS);
        (void)ps2_gif_drain();
        s_u_send[v] = ps2_gslive_u_variant(v, s_cap_send[slot], words);
        (void)ps2_gif_drain();
        for (i = 0u; i < width * (uint32_t)GSLIVE_PIC_ROWS; i++) buf[i] = GSREAD_SENT;
        (void)ps2_gs_vram_read(0, GSLIVE_SH_Y0, (int)width, GSLIVE_PIC_ROWS, buf);
        if (ps2_gs_vram_read_err() != GS_READ_BUS_OK) return;
        s_u_read_ok[v] = 1u;
        ps2_gslive_u_band(buf, canvas, width, v);
    }
    /* Leave the strip as this census found it, so the next census's own 12s pre-draw control still means
     * "nothing of mine is on this band" rather than being made true by a leftover triangle. */
    ps2_gs_upload(0, GSLIVE_SH_Y0, PS2_SCREEN_WIDTH, GSLIVE_PIC_ROWS);
    s_u_ok = 1u;
}

/* The six boxes, in the order they go out on the bus (A last), and what each one holds since rung 12i.
 * The label is what the row prints, and since 12i it has to name THREE things -- the encoding, the place
 * and the position in the frame -- because 12h proved that a row naming only two of them can be read two
 * ways: its grid said SENDER and said "the first three transfers of the frame" with the same eight
 * counts.  C, E and F keep the CONTROL tag: they are the shapes that measured as interpolating, and if
 * one of them comes back flat the READ has stopped working and the whole table is void.  A and D are this
 * rung's cells, both the recorder's, both alone in a session of their own: A is asked LAST, behind three
 * hand triangles, at the box where 12h measured the recorder flat at the front of the frame, so A's pair
 * is the frame's order with the box, the packet and the sender held still; D is asked FIRST, as the
 * frame's first triangle transfer, where 12h's cell G had the same group third in one session.  B is
 * unchanged from 12a and still names which vertex wins with IIP off.
 *
 * Since rung 12l that sentence needed one more fact, and 12l then measured it: 12k's pictures graded D and
 * B as RECTS and A, C, E, F as triangles, in the order D, B, C, E, F, A went out, and the hypothesis that
 * fit was that D's and B's PRE=1 tags left them drawing under the PRIM the last PRE=0 tag wrote -- in this
 * census, the magenta SPRITE poster.  12l cleared PRE on D's tag and on no other, and the machine said no:
 * D's tag left as PRE=0 PRIM=0x0b (the A-to-D tag xor is bit 46 alone), and D laid the same 88x16 block,
 * 1408 painted px, no field.  So PRE is not the variable -- CONDITIONALLY, which is the finding that
 * matters for the next rung: clearing PRE only bites if the inherited PRIM register differs from the
 * TRI+IIP the tag carries, and nothing here ever read that register.  12m therefore keeps PRE=0 on D and
 * zeroes D's tag PRIM FIELD as well, so the one authoritative ask in the frame is POINT; box B stays
 * unpatched, and what B does is no longer the question -- B is the control that says the census has not
 * moved.
 *
 * 12m answered that one too, and the two negatives together are the finding: the tag's PRE bit did not move
 * D's ink, and the tag's PRIM field did not move D's ink either, both times with the captured tag proving
 * the patch left the EE.  Two of the three things a tag can say about a transfer have now been edited on
 * purpose with the same 88x16 block, 1408 px, as the answer -- so the assumption carrying all of it, that
 * the block is box D's GROUP's ink, is the one thing left untested.  Rung 12n tests it by the one input no
 * rung has moved since 12h: the geometry.  D is asked 24 px wide at the left edge of its own window and
 * read across the whole 88 px, so the ask and the window differ by coordinate, and painted px can be
 * counted outside what was asked for.  Both tag dials go off for it, which is what makes D's row comparable
 * to 12k's unpatched baseline (top=88/88 painted=1408) rather than to a patched one.
 *
 * 12n answered, and it answered in the same 8 censuses as 12m: asked 24 px, D still laid
 * `top=88/88 px=1408 out=1024 RECT` -- the identical red x536-599 plus blue x600-623 block, and 1024 of its
 * painted px at an x never asked for.  The ask is therefore not the variable either: PRE did not move D's
 * ink, the PRIM field did not, and neither did the geometry.  Three inputs a group can hand the GIF have now
 * been edited on purpose with one unchanging answer, so rung 12o stops dialling the band and asks where
 * THIS group's ink goes at all: D's ask moves OUT of the band into the empty rows below it, inside the
 * spill window, and the census reads both places -- the band, to see whether the block survives the group
 * leaving it, and the moved rect, to see whether anything lands where the packet asked.
 *
 * 12o answered, in all eight censuses of both pinned runs and identically in every one: the packet is the
 * ask (`v0=(536,591) v1=(559,591) v2=(548,576) -> PACKET == ASK`), 384 red px fill the asked 24x16 bbox at
 * x536-559 y576-591 (the whole rect, not the 192 px a triangle would cover, and not one green px anywhere),
 * and the band's block is unchanged at 1408 px with out=1408, so 12n's THE BLOCK HELD was right and that ink
 * is a foreign writer's.  It also moved the control: box B went 12n's `top=128/128 px=1920 RECT` to
 * `top=63/128 px=1073 PARTIAL`, which is why 12o's own verdict is CONTROL MOVED and nothing above can be
 * attributed until that movement has a cause.  Rung 12p supplies the one input none of 12n/12o moved --
 * whether the bytes go out at all -- by withholding D's captured group at the GIF seam and reading the
 * same two answers off the same page.
 *
 * 12p answered NEITHER, and answered with a third fact: with D's bytes withheld, D's band block stood at
 * 1408 px and D's moved rect stood at 384 red px (so neither picture is this transfer's in any causal
 * sense), while box B -- whose own group DID go out -- read `painted=0` and lost its foreign blue rect with
 * it.  One withheld transfer emptied a neighbour, so no per-box number on this page can be read as that
 * box's raster.  Rung 12q therefore holds the dial as a MASK over all three recorder sessions (A, B and D)
 * and asks whether ANY of the census's counted ink depends on the census's own bytes: if every recorder
 * window stands at its 12o number with nothing sent, rung 12 has been reading the app's stream since the
 * beginning, and the instrument -- not the GS -- is what needs fixing next. */
static const uint32_t s_sh_x0[GSLIVE_SH_N] = { GSLIVE_SH_A_X0, GSLIVE_SH_B_X0, GSLIVE_SH_C_X0,
                                               GSLIVE_SH_D_X0, GSLIVE_SH_E_X0, GSLIVE_SH_F_X0 };
static const uint32_t s_sh_x1[GSLIVE_SH_N] = { GSLIVE_SH_A_X1, GSLIVE_SH_B_X1, GSLIVE_SH_C_X1,
                                               GSLIVE_SH_D_X1, GSLIVE_SH_E_X1, GSLIVE_SH_F_X1 };
static const char *const s_sh_label[GSLIVE_SH_N] = {
    "A recorder NREG2 chain, LAST, SENT (12r)",
    "B recorder PRE1 NREG2 IIP0, SENT (12r)",
    "C CONTROL 12c winner: AD(PRIM,RGBAQ) + PRE0 NREG2 {RGBAQ,XYZ2}",
    "D recorder NREG2 chain, FIRST, whole window (12s)",
    "E CONTROL AD group + PRE0 NREG3 {RGBAQ,ST,XYZ2} + pad",
    "F CONTROL no AD group, PRE1 NREG3 {RGBAQ,ST,XYZ2} + pad (recorder tag)"
};
/* The shape word both 12l's and 12m's rows print, kept in one table because the two rungs grade the same
 * top-row law and a second copy of the words would let them drift apart.  `PARTIAL` is the honest middle:
 * a top row that is neither one pixel nor edge-to-edge is neither shape, and 12k's pictures show what that
 * is -- C and E lay a solid rect AND a gradient triangle over the same box. */
static const char *const gslive_sh_kind[3] = { "TRIANGLE", "PARTIAL", "RECT" };

/* Rung 12r: tally one rectangle of a read band with the census's own class order -- corner words first,
 * then the canvas compare, then the field -- so the pre-draw picture and the post-draw picture differ ONLY
 * in when they were taken.  It writes its own accumulator and never touches `sh_*`, `mv_*` or the gspeak
 * grids: those hold this census's answer, and a shared accumulator would make the two readings one number.
 * `buf` is the band this read filled, whose row 0 is absolute `y0`; `canvas` is the desktop canvas the census
 * compares against, indexed by absolute row, so both have to be handed the same `width`.
 *
 * Rung 12s adds the four fields on the right, and they are the reason the class order above is now known to
 * be a liability rather than a convention: a corner word is counted before the canvas compare, so a page
 * pixel that is the desktop's own palette red is indistinguishable, in `r`/`b`/`painted`, from this census's
 * ink.  `cv_r/cv_g/cv_b` count the canvas's words by the same three constants, at the same positions, and
 * `match` counts positions where the page word equals the canvas word whatever they are -- the agreement
 * that separates "chrome uploaded through the same buffer" from "a GS-side writer this file does not own". */
static void ps2_gslive_pre_rect(gslive_pre_t *acc, const uint32_t *buf, uint32_t width,
                                const uint32_t *canvas, uint32_t x0, uint32_t x1,
                                uint32_t y0, uint32_t rows)
{
    uint32_t rr, x;

    acc->tot = acc->same = acc->other = acc->top = acc->sent = 0u;
    acc->r = acc->g = acc->b = 0u;
    acc->cv_r = acc->cv_g = acc->cv_b = acc->match = 0u;
    for (rr = 0u; rr < rows; rr++) {
        const uint32_t *row = buf + (size_t)rr * width;
        const uint32_t *crow = canvas + (size_t)(y0 + rr) * width;

        for (x = x0; x < x1; x++) {
            const uint32_t v = row[x];
            const uint32_t c = crow[x];

            if (v == c) acc->match++;                       /* before the class order: pure agreement */
            if (c == GSQUAD_RED)        acc->cv_r++;
            else if (c == GSQUAD_GREEN) acc->cv_g++;
            else if (c == GSQUAD_BLUE)  acc->cv_b++;
            if (v == GSREAD_SENT) { acc->sent++; continue; }
            acc->tot++;
            if (v == GSQUAD_RED)        { acc->r++;     if (rr == 0u) acc->top++; }
            else if (v == GSQUAD_GREEN) { acc->g++;     if (rr == 0u) acc->top++; }
            else if (v == GSQUAD_BLUE)  { acc->b++;     if (rr == 0u) acc->top++; }
            else if (v == c)              acc->same++;
            else                        { acc->other++; if (rr == 0u) acc->top++; }
        }
    }
}

static void ps2_bench_gs_live_census(uint32_t frame)
{
    static uint64_t s_pkt[2u * GS_POSTER_QWC(1u)] __attribute__((aligned(16)));
    gs_poster_rect r;
    gs_draw_env env;
    const uint32_t *canvas = ps2_gs_get_framebuffer();
    const WND *w = ps2_bench_wnd_named("Cross Media Bar");
    const uint32_t width = (uint32_t)PS2_SCREEN_WIDTH;
    const uint32_t height = (uint32_t)PS2_SCREEN_HEIGHT;
    uint32_t *buf;
    uint32_t ink_v[GSLIVE_TOP], ink_n[GSLIVE_TOP], ink_in[GSLIVE_TOP];
    uint32_t read_px = 0u, sent = 0u, same = 0u, win_px = 0u;
    uint32_t diff_all = 0u, diff_console = 0u, diff_ink = 0u, diff_win = 0u;
    uint32_t ctrl = 0u, box_px = 0u, rare = 0u, bins = 0u, qw_sent = 0u, us;
    /* Rung 12a's and 12d's tallies, one slot per box -- see `s_sh_label[]` for which box is which
     * shape.  A box is `tot` px of which `same` are the canvas the desktop put there, the three corner
     * colours are counted where they land whole, and `other` is everything painted that is not one of
     * the three -- which is the interpolated field, and the only reading that says a shape shaded. */
    uint32_t sh_tot[GSLIVE_SH_N];
    uint32_t sh_r[GSLIVE_SH_N], sh_g[GSLIVE_SH_N], sh_b[GSLIVE_SH_N];
    uint32_t sh_other[GSLIVE_SH_N], sh_same[GSLIVE_SH_N], sh_px = 0u;
    /* Rung 12j's two extra reads, on the same walk: `sh_bbox[b][c]` is where colour c of box b's own
     * corners actually is, as {minx, maxx, miny, maxy} with an inclusive max, and the spill arrays are the
     * same three colours found OUTSIDE every box inside GSLIVE_SP_Y0..Y1.  A box of count 0 keeps its
     * bbox at the impossible 0xFFFFFFFF so the print can say `none` instead of inventing coordinates. */
    uint32_t sh_bbox[GSLIVE_SH_N][3][4];
    uint32_t sp_n[3], sp_bbox[3][4], sp_same = 0u;
    /* Rung 12l's shape tally: painted px in the band's FIRST row and in its LAST row.  A rect covers the
     * whole width of the row its top edge sits on; an apex-up triangle covers one or two pixels of it.
     * That is the only shape law that also grades a FLAT triangle, which is what a fixed box B is
     * supposed to become -- the `other=` field test cannot see that one, because flat means no field. */
    uint32_t sh_top[GSLIVE_SH_N], sh_bot[GSLIVE_SH_N];
    /* Rung 12n's attribution tally: painted px in a box's READ window that sit outside the span that box
     * was ASKED to draw.  With the ask equal to the window it is identically zero, which is what makes it
     * a measurement rather than an argument -- the only way it grows is the geometry dial. */
    uint32_t sh_outside[GSLIVE_SH_N];
    /* Rung 12t(iii): the band's ink counted over the FULL width and in every non-canvas class at once.  The
     * per-box tallies cannot answer 12t's question, because they attribute by coordinate: a block that
     * starts on box B's right edge, crosses box C's whole window and both 16-px gutters and ends on D's apex
     * gets split between 12j's spill count, C's `painted` and D's `painted`, and that split is exactly what
     * hides the fact that it is ONE rectangle.  These counters make no attribution: `bp_n` is every band
     * pixel that disagrees with the canvas under it, `bp_corner`/`bp_field` split it by the same corner-word
     * law the boxes use, and `bp_bbox` is their extent.  The magenta control's rect is not in here -- the
     * walk excludes it by coordinate before this branch is reached -- and rows outside the band are not in
     * here either, so `bp_bbox` is a statement about the strip the six boxes live in. */
    uint32_t bp_n = 0u, bp_corner = 0u, bp_field = 0u, bp_bbox[4];
    /* Rung 12o's read of a MOVED ask.  12n moved D's ask within the band and the band block did not move
     * with it; this rung moves the ask OUT of the band, into rows the box branch never reads, and so needs a
     * second place to look.  These counters therefore classify, in the spill window, exactly the pixels that
     * sit inside a session's asked rect while that rect lies outside the band -- the only ink this census can
     * attribute to the dialled group by coordinate alone.  `mv_box` names which box owns the rect, or stays
     * GSLIVE_SH_N when nothing was asked outside the band, which is what lets the print say SKIP instead of
     * reading an unasked rect as an answer.  A pixel claimed here is NOT added to 12j's off-box aggregate:
     * the two rows must describe different regions or 12j's bbox becomes a merge of an unknown writer and
     * this rung's named one. */
    uint32_t mv_box = GSLIVE_SH_N;
    uint32_t mv_tot = 0u, mv_same = 0u, mv_other = 0u, mv_painted = 0u;
    uint32_t mv_r = 0u, mv_g = 0u, mv_b = 0u, mv_bbox[4];
    /* Rung 6b -- SURVIVAL, added because rung 6 and the operator disagree.  Rung 6 answers "did the
     * stream's ink arrive"; the complaint is about a window that is empty when it is looked at, and
     * those are different questions -- ink that arrives and is then re-laid over by the next present
     * reads as a full client rect at the instant of the census and shows nothing one frame later.
     * The two reads this run already made differ that way (in_win=383951 at one census, 287731 at
     * the next, with `same` rising by the difference), and `xmb surface ... black=384000 other=0`
     * says what the replacement value is: the app's CPU canvas, which the eegs path never paints.
     * So every census keeps the per-row ink count of the window's client rect and compares it with
     * the previous census's.  A per-pixel mask would be exact and cost 60 kB of a pool this image
     * has already spent down to a 350 kB hole; a per-row count costs 4.8 kB and answers the only
     * question asked of it -- did the ink stay, and in which rows did it leave. */
    static uint32_t s_live_prev_ink[PS2_SCREEN_HEIGHT];
    static uint32_t s_live_cur_ink[PS2_SCREEN_HEIGHT];
    static uint32_t s_live_cur_same[PS2_SCREEN_HEIGHT];
    static uint32_t s_live_census_no;
    static int s_live_prev_valid;
    /* Rung 6c -- WHO put the canvas back, and in which rows.  The per-row ink delta says where the
     * eraser reached; the cover ring above (file statics, recorded at the send) says what went out.
     * This is the per-client-row px those two agree on, and it is a row count rather than a rect
     * list because the comparison the verdict needs is "did every lost row have an upload". */
    static uint32_t s_live_cover_row[PS2_SCREEN_HEIGHT];
    static uint32_t s_live_nostream_row[PS2_SCREEN_HEIGHT];
    static uint32_t s_live_cover_since;
    uint32_t ink_now = 0u, same_now = 0u, ink_then = 0u, erased = 0u, laid = 0u;
    uint32_t lost_rows = 0u, lost_first = 0u, lost_last = 0u;
    uint32_t lost_rows_covered = 0u, cover_px = 0u, cover_rects = 0u, cover_missed = 0u;
    /* Rung 20w's half of the ring: the pass's own canvas uploads, and how many of them went out on a
     * pass with no stream behind them.  The clip in ps2_upload_canvas() is what makes the second
     * number zero, and the row is written so that it says so rather than being inferred from a hole
     * that the cover list does explain. */
    uint32_t canvas_rects = 0u, canvas_px = 0u, nostream_rects = 0u, lost_rows_nostream = 0u;
    /* Rung 20x: the same number split by the condition that let the upload out, because "the cut is
     * not holding" is three different bugs -- a stream that staged nothing, a layer whose rect the
     * window manager could not name, and a pass with no layer to protect (which is correct and must
     * not read as a failure). */
    uint32_t ns_why[8];   /* PS2_WHY_*; room for rung 20y's PLATED, which is never counted here
                           * because a plated upload always has a stream behind it */
    /* The geometry of the uploads in this census's own window, kept because a rect is what names its
     * producer: `desktop.c`'s deskbar rows and panel rows, `workbench.c`'s open menus, `tip_ife.c`'s
     * popup and the cursor's own box are all cover-adders, and they are told apart by where their
     * rect is -- not by the count. */
    int hc_rect[PS2_HIST_RECTS][4];
    uint32_t hc_pass[PS2_HIST_RECTS], hc_n = 0u;
    uint8_t hc_kind[PS2_HIST_RECTS];
    uint8_t hc_why[PS2_HIST_RECTS];
    /* Rung 6c's other half: the colour the eraser LEAVES.  A hole filled with the app's CPU pixmap
     * (black), with the console's navy, with the desktop's own plate and with a stale wallpaper are
     * four different authors, and the per-row delta cannot tell them apart -- it only knows the page
     * came back to the canvas.  So the client pixels that equal the canvas get the same top-N
     * treatment the ink does. */
    uint32_t cv_v[GSLIVE_TOP], cv_n[GSLIVE_TOP], cv_bins = 0u, cv_rare = 0u;
    int cv_box[GSLIVE_TOP][4];
    int cv_printed[GSLIVE_TOP];
    int have_prev;
    int all_box[4] = { -1, -1, -1, -1 };
    int win_box[4] = { -1, -1, -1, -1 };
    int ink_box[GSLIVE_TOP][4];
    int ctrl_box[4] = { -1, -1, -1, -1 };
    int cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;
    uint32_t i, y_start, k, best, col, pr;
    int printed[GSLIVE_TOP];
    int32_t got, qw;
    gs_read_bus_err err;

#if BTRON_PS2_BENCH_GS >= 5
    /* Rung 27, half 1: the six boxes as this census FOUND the page -- before its strip upload, its
     * magenta control, its six shading groups and its own page read, which are the four things this
     * file could otherwise be accused of.  Silent on purpose: the note above `buf` explains that a
     * print here would repaint the console band between this census and the page it grades. */
    if (w && w->gl_layer) ps2_bench_ink_at_census(&w->client);
#endif

    for (k = 0u; k < GSLIVE_TOP; k++) {
        ink_v[k] = 0u; ink_n[k] = 0u; ink_in[k] = 0u;
        ink_box[k][0] = ink_box[k][1] = ink_box[k][2] = ink_box[k][3] = -1;
        printed[k] = 0;
        cv_v[k] = 0u; cv_n[k] = 0u; cv_printed[k] = 0;
        cv_box[k][0] = cv_box[k][1] = cv_box[k][2] = cv_box[k][3] = -1;
    }
    for (k = 0u; k < PS2_SCREEN_HEIGHT; k++) {
        s_live_cur_ink[k] = 0u;
        s_live_cur_same[k] = 0u;
        s_live_cover_row[k] = 0u;
        s_live_nostream_row[k] = 0u;
    }
    for (k = 0u; k < sizeof ns_why / sizeof ns_why[0]; k++) ns_why[k] = 0u;
    for (k = 0u; k < GSLIVE_SH_N; k++) {
        sh_tot[k] = 0u; sh_r[k] = 0u; sh_g[k] = 0u; sh_b[k] = 0u;
        sh_other[k] = 0u; sh_same[k] = 0u;
        sh_top[k] = 0u; sh_bot[k] = 0u;
        sh_outside[k] = 0u;
        for (col = 0u; col < 3u; col++) {
            sh_bbox[k][col][0] = sh_bbox[k][col][1] = GSLIVE_BB_NONE;
            sh_bbox[k][col][2] = sh_bbox[k][col][3] = GSLIVE_BB_NONE;
        }
        for (pr = 0u; pr < GSLIVE_PIC_ROWS; pr++) {
            for (col = 0u; col < GSLIVE_PIC_COLS; col++) s_band_pic[k][pr][col] = 0u;
        }
    }
    for (col = 0u; col < 3u; col++) {
        sp_n[col] = 0u;
        sp_bbox[col][0] = sp_bbox[col][1] = GSLIVE_BB_NONE;
        sp_bbox[col][2] = sp_bbox[col][3] = GSLIVE_BB_NONE;
    }
    sp_same = 0u;
    bp_n = 0u; bp_corner = 0u; bp_field = 0u;
    bp_bbox[0] = bp_bbox[1] = bp_bbox[2] = bp_bbox[3] = GSLIVE_BB_NONE;
    mv_tot = 0u; mv_same = 0u; mv_other = 0u; mv_painted = 0u;
    mv_r = 0u; mv_g = 0u; mv_b = 0u;
    mv_bbox[0] = mv_bbox[1] = mv_bbox[2] = mv_bbox[3] = GSLIVE_BB_NONE;

    ps2_kprintf("[GSLIVE] live page vs its canvas at frame %u: one MAGENTA control and\n",
                (unsigned int)frame);
    ps2_kprintf("[GSLIVE] six triangles into six boxes -- rung 12i separates the frame's\n");
    ps2_kprintf("[GSLIVE] ORDER from its SENDER, which 12h never did: its recorder groups\n");
    ps2_kprintf("[GSLIVE] were transfers 1,2,3 and the hand chain's 4,5,6,7, so every flat\n");
    ps2_kprintf("[GSLIVE] box was also an early one.  Each recorder triangle now goes alone\n");
    ps2_kprintf("[GSLIVE] in a session of its own: box D's FIRST, box A's LAST, B's IIP=0\n");
    ps2_kprintf("[GSLIVE] control early, and C/E/F are the hand controls that say a gradient\n");
    ps2_kprintf("[GSLIVE] is readable in this band.  Box A's hand chain is built, not sent.\n");
    ps2_kprintf("[GSLIVE] Rung 12j reads the SAME page again for WHERE each corner colour is:\n");
    ps2_kprintf("[GSLIVE] a bbox per box per colour, plus the corner words outside every box,\n");
    ps2_kprintf("[GSLIVE] because 1408 px in an 88x16 box is not a triangle's coverage.\n");
    ps2_kprintf("[GSLIVE] Rung 12k prints that same band as a picture, 4 px per character,\n");
    ps2_kprintf("[GSLIVE] so a rect's straight edge and a triangle's hypotenuse cannot be\n");
    ps2_kprintf("[GSLIVE] confused for each other the way two readings of a bbox can be.\n");
    ps2_kprintf("[GSLIVE] 12k's pictures said the two FLAT boxes are RECTS, not bad shading,\n");
    ps2_kprintf("[GSLIVE] and that the recorder chain interpolates when it goes out LAST.\n");
    ps2_kprintf("[GSLIVE] Rung 12l clears PRE on box D's tag alone.  ps2sdk says PRE=1 means\n");
    ps2_kprintf("[GSLIVE] the tag's PRIM field is not output, so D has been drawing under the\n");
    ps2_kprintf("[GSLIVE] SPRITE poster's latched PRIM.  B is NOT patched: it is the test of\n");
    ps2_kprintf("[GSLIVE] whether that latch carries to the next PRE=1 group or not.\n");
    ps2_kprintf("[GSLIVE] 12l answered NO, and the answer is conditional: clearing PRE only\n");
    ps2_kprintf("[GSLIVE] bites if the PRIM register D inherits differs from the TRI+IIP its\n");
    ps2_kprintf("[GSLIVE] tag carries, and no rung here read that register.  Rung 12m therefore\n");
    ps2_kprintf("[GSLIVE] zeroes D's whole PRIM FIELD too, so the one authoritative ask this\n");
    ps2_kprintf("[GSLIVE] frame makes is for POINT: points laid means 12l's block was laid by\n");
    ps2_kprintf("[GSLIVE] TRI+IIP and the answer is geometry; the same block laid means no tag\n");
    ps2_kprintf("[GSLIVE] PRIM field of this group reaches the rasteriser at all.\n");
    ps2_kprintf("[GSLIVE] 12m answered THAT too: the same 1408-px block under a tag that asks\n");
    ps2_kprintf("[GSLIVE] for POINT and under one that asks for TRI+IIP.  Two tag fields edited on\n");
    ps2_kprintf("[GSLIVE] purpose, no answer, so rung 12n doubts what both of them assumed: that\n");
    ps2_kprintf("[GSLIVE] the block in D's window is D's triangle's ink.  D is now asked 24 px\n");
    ps2_kprintf("[GSLIVE] wide at the left of the 88-px window it is read across, and painted px\n");
    ps2_kprintf("[GSLIVE] outside the ask are counted: ink that follows its ask is this group's,\n");
    ps2_kprintf("[GSLIVE] a block that holds while the ask moves is another writer's ink.\n");
    ps2_kprintf("[GSLIVE] 12n held too: asked 24 px, it laid top=88/88 painted=1408 with\n");
    ps2_kprintf("[GSLIVE] out=1024 -- a thousand painted px at an x never asked for.  Three\n");
    ps2_kprintf("[GSLIVE] inputs edited on purpose, three identical answers, so rung 12o\n");
    ps2_kprintf("[GSLIVE] asks the one question none of them could: where does THIS group's\n");
    ps2_kprintf("[GSLIVE] ink go?  D's ask moves out of the band into rows 576-591, which no\n");
    ps2_kprintf("[GSLIVE] box is read out of, and the census reads both the emptied band and\n");
    ps2_kprintf("[GSLIVE] the asked rect -- after decoding D's own XYZ2 words, so the ask is\n");
    ps2_kprintf("[GSLIVE] certified as bytes that left the EE and not as an argument.\n");
    ps2_kprintf("[GSLIVE] 12o answered, and with two surprises: the packet IS the ask, and 384\n");
    ps2_kprintf("[GSLIVE] red px fill the moved 24x16 rect -- while the band's 1408-px block\n");
    ps2_kprintf("[GSLIVE] stands unchanged with the group gone, so it was never D's.  And B\n");
    ps2_kprintf("[GSLIVE] moved: its blue rect (945 px, foreign since 12j) held, but its OWN red\n");
    ps2_kprintf("[GSLIVE] went from the left half of rows 560-574 (975 px) to row 575 alone\n");
    ps2_kprintf("[GSLIVE] (128 px).  A neighbour's y rewrote this triangle's scanlines, so rung\n");
    ps2_kprintf("[GSLIVE] 12p edits the one input left: DELIVERY.  D's group is still captured\n");
    ps2_kprintf("[GSLIVE] and decoded, and the sink then hands it to nobody.  If the moved ink\n");
    ps2_kprintf("[GSLIVE] and the band block both stand, neither is D's; and if B's red comes\n");
    ps2_kprintf("[GSLIVE] back with D's bytes absent, one transfer's delivery rules another's.\n");
    ps2_kprintf("[GSLIVE] 12p answered NEITHER: with D's bytes withheld both its windows stood\n");
    ps2_kprintf("[GSLIVE] (band 1408, moved 384) and box B, whose own group DID go out, read\n");
    ps2_kprintf("[GSLIVE] painted=0, its foreign blue rect included.  So one withheld transfer\n");
    ps2_kprintf("[GSLIVE] emptied a neighbour: no per-box count here means what 12a assumed.\n");
    ps2_kprintf("[GSLIVE] 12q arms the mask on all three recorder sessions (A, B, D) and asks\n");
    ps2_kprintf("[GSLIVE] whether ANY of its ink survives with no census bytes sent at all.\n");
    ps2_kprintf("[GSLIVE] 12q answered SPLIT: A fell 891 -> 0 and B fell 1073 -> 0, so those two\n");
    ps2_kprintf("[GSLIVE] really are this file's delivery, while D's band block (1408) and its\n");
    ps2_kprintf("[GSLIVE] moved rect (384 red) stood with `held=3, to the bus=0` -- no census byte\n");
    ps2_kprintf("[GSLIVE] reached the GS for them.  No send-side dial can now say who laid them,\n");
    ps2_kprintf("[GSLIVE] because every dial so far graded a page this census had drawn on first.\n");
    ps2_kprintf("[GSLIVE] Rung 12r dials the one input never touched -- WHEN the page is\n");
    ps2_kprintf("[GSLIVE] read: the mask is disarmed so every send goes out as at 12o, and the\n");
    ps2_kprintf("[GSLIVE] band is read once BEFORE the control is sent, by the same law, and\n");
    ps2_kprintf("[GSLIVE] printed beside the post-draw numbers.  Census #1 of the process is the\n");
    ps2_kprintf("[GSLIVE] only look at a page this file has not drawn on, so it alone carries a\n");
    ps2_kprintf("[GSLIVE] verdict -- and it came back with its own falsifier: the magenta control\n");
    ps2_kprintf("[GSLIVE] held magenta=0 painted=0, so the read IS a first look.  At that first\n");
    ps2_kprintf("[GSLIVE] look D's window already held a whole-width red-1024/blue-384 rect, C 320\n");
    ps2_kprintf("[GSLIVE] and E 1088, while A, B and F came back clean.\n");
    ps2_kprintf("[GSLIVE] That is not a foreign GS writer -- it is the desktop's own Colour Test\n");
    ps2_kprintf("[GSLIVE] Bar.  `desktop_render()` fills the bottom 40 rows (560..599) with 8\n");
    ps2_kprintf("[GSLIVE] swatches of w/8 = 100 px, and swatch 5 is `COLOR_RED`, swatch 6 its blue:\n");
    ps2_kprintf("[GSLIVE] `fill_rec()` stores them through `BTRON_PIXEL_FB()`, which swaps R and B,\n");
    ps2_kprintf("[GSLIVE] so they land bit-equal to this census's GSQUAD_RED and GSQUAD_BLUE.  The\n");
    ps2_kprintf("[GSLIVE] key tests corner words BEFORE the canvas compare, so it has counted that\n");
    ps2_kprintf("[GSLIVE] chrome as ink since 12a: D's 1408 is 64 red columns plus 24 blue ones,\n");
    ps2_kprintf("[GSLIVE] E's 1088 is 68 blue columns, C's 320 is the bar in its last 20 columns.\n");
    ps2_kprintf("[GSLIVE] Rung 12s therefore moves the instrument to rows 32..47 -- the XMB's title\n");
    ps2_kprintf("[GSLIVE] bar, chrome whose words are not corner words and which no cover rect\n");
    ps2_kprintf("[GSLIVE] re-lays -- retires D's moved ask, and reads BOTH strips before the first\n");
    ps2_kprintf("[GSLIVE] send: the new one to certify it, the old one to test the bar explanation\n");
    ps2_kprintf("[GSLIVE] with the page against its own canvas instead of with a reading of the\n");
    ps2_kprintf("[GSLIVE] source.  Every verdict below is gated on that certification.\n");
    ps2_kprintf("[GSLIVE] Rung 12t puts that certified strip to the question 12s left open.  Box A's\n");
    ps2_kprintf("[GSLIVE] recorder group, sent LAST, lays 891 px of apex-up triangle whose every cell is a\n");
    ps2_kprintf("[GSLIVE] ramp pixel; box D's -- sent FIRST, and 12i captured it as differing from A's in\n");
    ps2_kprintf("[GSLIVE] three position words and no colour word -- lays 660 px of pure red as a 44-wide\n");
    ps2_kprintf("[GSLIVE] block with a 44-px apex row, so flat AND mis-shaped; and 12j says the same red\n");
    ps2_kprintf("[GSLIVE] runs x384..579, box B's right edge through box C's whole window, both 16-px\n");
    ps2_kprintf("[GSLIVE] gutters and D's left half: 2940 px of one rectangle no session ever asked for.\n");
    ps2_kprintf("[GSLIVE] Either this packet alone does that, in which case the recorder's encoding or the\n");
    ps2_kprintf("[GSLIVE] GS state its first group lands in is the bug, or a NEIGHBOUR drew the rectangle\n");
    ps2_kprintf("[GSLIVE] and D owns only the part its window covers, in which case 12i's ORDER answer is\n");
    ps2_kprintf("[GSLIVE] about register latching, not shading.  A census that sends seven things cannot\n");
    ps2_kprintf("[GSLIVE] tell them apart -- every count in it is a sum of seven.  So census #1 now\n");
    ps2_kprintf("[GSLIVE] uploads the strip from its own canvas, sends box D's group ALONE (control, B, A\n");
    ps2_kprintf("[GSLIVE] and the hand chain C/E/F all withheld) and reads the whole band's ink extent;\n");
    ps2_kprintf("[GSLIVE] censuses 2..4 run 12i's grid on a strip reset the same way, which is also why\n");
    ps2_kprintf("[GSLIVE] 12s's verdict is no longer confined to whichever census happened to be first.\n");
    ps2_kprintf("[GSLIVE] 12t ANSWERED, and against its own verdict label: `build/repro/20261018T074741Z`\n");
    ps2_kprintf("[GSLIVE] census #1 sent box D's group alone on a reset band and got painted=1 at x580 y32\n");
    ps2_kprintf("[GSLIVE] -- the packet's own apex vertex -- where the grid gets 660 px in that window and\n");
    ps2_kprintf("[GSLIVE] 6803 px across the band.  A lone group does not even make a TRIANGLE, which is\n");
    ps2_kprintf("[GSLIVE] what PRE=1 means: GIF_PRE_ENABLE disables the tag's PRIM output, so OP and IIP\n");
    ps2_kprintf("[GSLIVE] come from whatever the last PRIM WRITE latched, and this port's recorder groups\n");
    ps2_kprintf("[GSLIVE] never write it.  12u therefore makes that write the only variable: the same eight\n");
    ps2_kprintf("[GSLIVE] words replayed four times on the same reset band -- V0 undialled, V1 with PRE\n");
    ps2_kprintf("[GSLIVE] cleared so the tag's own field can rule, V2 with a ps2sdk AD(PRIM) write of the\n");
    ps2_kprintf("[GSLIVE] Gouraud ask in front, V3 the same preamble with IIP clear as V2's control.  Tag\n");
    ps2_kprintf("[GSLIVE] field alone puts a triangle down => flip PRE in the recorder; only the AD write\n");
    ps2_kprintf("[GSLIVE] does => the recorder must program PRIM per group; V2 fields and V3 does not =>\n");
    ps2_kprintf("[GSLIVE] IIP is the shading lever, which is what the flat gears on the monitor are.\n");
    ps2_kprintf("[GSLIVE] 12u ANSWERED that, on the machine: the AD write rules and the tag field does not,\n");
    ps2_kprintf("[GSLIVE] and V2's field against V3's zero named IIP as the lever.  Rung 12v LANDS it --\n");
    ps2_kprintf("[GSLIVE] ps2_gs_rec.c's open_group() now sends a PACKED NLOOP=1/NREG=1 AD write of the same\n");
    ps2_kprintf("[GSLIVE] PRIM word its tag carries, in front of every vertex chain, and gs_rec_prim_writes()\n");
    ps2_kprintf("[GSLIVE] counts them as the [GSTS] row's pwr= column.  So this run is the fix's measurement\n");
    ps2_kprintf("[GSLIVE] and has three numbers that decide it: box D's own census row must lay a TRIANGLE\n");
    ps2_kprintf("[GSLIVE] rather than the 1 px 12t found, 12i's grid must call the recorder's cell\n");
    ps2_kprintf("[GSLIVE] INTERPOLATES, and box B -- the same chain asked with IIP clear -- must stay one\n");
    ps2_kprintf("[GSLIVE] flat colour.  B is the falsifying control: if it shades, the read is broken and\n");
    ps2_kprintf("[GSLIVE] nothing else on this page means anything.  12u's replay keeps its bite by putting\n");
    ps2_kprintf("[GSLIVE] a write of POINT in front of V0, because the census now DOES program PRIM and a\n");
    ps2_kprintf("[GSLIVE] bare chain would otherwise draw under its own frame's ask rather than under none.\n");

    if (!canvas) {
        ps2_kprintf("[GSLIVE] no bound canvas -- SKIP, the page has nothing to be compared against\n");
        return;
    }
    if (w) {
        cx0 = (int)w->client.left; cy0 = (int)w->client.top;
        cx1 = (int)w->client.right; cy1 = (int)w->client.bottom;
        if (cx1 > cx0 && cy1 > cy0) win_px = (uint32_t)(cx1 - cx0) * (uint32_t)(cy1 - cy0);
    }

    /* Rung 6c, taken before the read because the read is the long part: every cover upload that has
     * gone out since the previous census is a candidate eraser, and each is already intersected with
     * the layer's client rect by ps2_upload_cover_in_layer(), so its whole area is in the hole.  The
     * window is [s_live_cover_since, s_cover_writes); if the ring wrapped inside that window the
     * older rects are named as missed rather than silently dropped, because a verdict of "the cover
     * path did it" has to say how much of the set it actually saw. */
    if (win_px) {
        const uint32_t writes_now = s_cover_writes;
        uint32_t t = s_live_cover_since;

        cover_missed = (writes_now > t + PS2_COVER_TRACE_N)
                         ? (writes_now - t - PS2_COVER_TRACE_N) : 0u;
        if (writes_now > PS2_COVER_TRACE_N &&
            t < writes_now - PS2_COVER_TRACE_N)
            t = writes_now - PS2_COVER_TRACE_N;
        for (; t < writes_now; t++) {
            const int *r = s_cover_rect[t % PS2_COVER_TRACE_N];
            const int x0 = r[0] > cx0 ? r[0] : cx0;
            const int x1 = r[2] < cx1 ? r[2] : cx1;
            const int y0 = r[1] > cy0 ? r[1] : cy0;
            const int y1 = r[3] < cy1 ? r[3] : cy1;
            const int xw = x1 > x0 ? x1 - x0 : 0;
            int rows = y1 > y0 ? y1 - y0 : 0;
            int rr;

            if (y0 - cy0 >= PS2_SCREEN_HEIGHT) rows = 0;
            else if (y0 - cy0 + rows > PS2_SCREEN_HEIGHT)
                rows = PS2_SCREEN_HEIGHT - (y0 - cy0);
            if (xw <= 0 || rows <= 0) continue;
            if (s_cover_kind[t % PS2_COVER_TRACE_N] == PS2_UPLOAD_COVER) {
                cover_rects++;
                cover_px += (uint32_t)xw * (uint32_t)rows;   /* rung 6c's test is about this kind alone */
            } else {
                canvas_rects++;
                canvas_px += (uint32_t)xw * (uint32_t)rows;
                if (!s_cover_stream[t % PS2_COVER_TRACE_N]) {
                    const uint8_t why = s_cover_why[t % PS2_COVER_TRACE_N];

                    nostream_rects++;
                    if (why < sizeof ns_why / sizeof ns_why[0]) ns_why[why]++;
                    for (rr = 0; rr < rows; rr++)
                        s_live_nostream_row[(uint32_t)(y0 - cy0 + rr)] += (uint32_t)xw;
                }
            }
            if (hc_n < PS2_HIST_RECTS) {
                hc_rect[hc_n][0] = x0;  hc_rect[hc_n][1] = y0;
                hc_rect[hc_n][2] = x1;  hc_rect[hc_n][3] = y1;
                hc_pass[hc_n] = s_cover_pass[t % PS2_COVER_TRACE_N];
                hc_kind[hc_n] = s_cover_kind[t % PS2_COVER_TRACE_N];
                hc_why[hc_n]  = s_cover_why[t % PS2_COVER_TRACE_N];
                hc_n++;
            }
            for (rr = 0; rr < rows; rr++)
                s_live_cover_row[(uint32_t)(y0 - cy0 + rr)] += (uint32_t)xw;
        }
        s_live_cover_since = writes_now;
    }

    /* The band buffer is asked for before the control is drawn for the same reason gsread allocates
     * before it sends: Imalloc() prints a [HEAP] row over its own threshold, and a print repaints
     * the console band at full width, which is the one upload that would land between this rung's
     * draw and its read. */
    buf = (uint32_t *)Imalloc((size_t)GSREAD_BAND_QWC * 16u);
    if (!buf) {
        ps2_kprintf("[GSLIVE] no %u B band buffer -- SKIP, nothing was read\n",
                    (unsigned int)(GSREAD_BAND_QWC * 16u));
        return;
    }
    gspeak_reset();     /* rung 10's two grids: every pixel of this read goes into them */

    /* Whatever the frame's own presents queued has to be on the GS before a read is meaningful --
     * and before a synchronous send is allowed to interleave with it. */
    (void)ps2_gif_drain();
    (void)ps2_gif_reap();

    /* Rung 12t(i): put the band back to its canvas, then look.
     *
     * 12s could only grade census #1, because VRAM is not cleared between app opens and a dirty window at
     * census #2 is equally this file's own triangle left over from census #1.  That limit is what stops any
     * rung that wants a different SEND set from getting a clean page: the first census has one page and
     * every later one inherits it.  So the census now writes the strip first -- `ps2_gs_upload()` is a
     * Host->Local IMAGE stream of these very canvas rows, synchronous through `ps2_gs_gif_send()`, and the
     * identical thing the present does every frame, so it is the port's own law being applied rather than a
     * special state the bench invents.  Rows GSLIVE_SP_Y0..Y1 go back to the canvas: the band the six boxes
     * are read out of AND the spill strip around them, because 12j's off-box block is half of the question.
     * The old colour-bar strip at 560..575 is deliberately NOT reset: its page-vs-canvas agreement is 12s's
     * independent control on the chrome explanation, and an upload would make that agreement true by
     * construction instead of by measurement.
     *
     * The reset does not have to be trusted.  The pre-draw read below is the check on it: `band_dirty` counts
     * the six windows that still disagree with their canvas or still hold a sentinel, and the 12s verdict
     * takes case 3 when that count is non-zero, which now means the upload failed rather than that some
     * desktop writer is ahead of this census. */
    ps2_gs_upload(0, GSLIVE_SP_Y0, PS2_SCREEN_WIDTH, GSLIVE_SP_Y1 - GSLIVE_SP_Y0);

    /* Rung 12r: look at the page BEFORE this census puts anything on it.  12q's answer split the band down
     * the middle -- A emptied 891 -> 0 and B emptied 1073 -> 0 with their own bytes withheld, while D's
     * 1408-px block and D's 384-px moved rect stood at their 12o numbers with NOTHING of this file's ever
     * reaching the GS -- so the two readings of the same column are now opposite claims about the same pixel
     * count, and no send-side dial can settle them: every dial still graded a page this census had already
     * drawn on.  This read is the other half of the instrument.  The GIF is drained and reaped above, so
     * what comes back is the page as the app, the console and any EARLIER census left it; the magenta
     * control has not been sent yet and neither has any triangle.  Nothing prints before it (`Imalloc()`
     * already happened above, and a print repaints the console band), and nothing prints between it and the
     * draw below, so no upload of this file's sits between the look and the page it describes.
     *
     * `s_pre_pass` is what makes the reading decidable: the census re-runs on every app open, and at pass 2
     * a held block may be this file's OWN triangle from pass 1 -- VRAM is not cleared between opens -- which
     * is a different finding from "the app laid it".  Only pass 1 can say that, so only pass 1 is graded.
     *
     * Rung 12s reads TWO bands in that one pre-draw pass, because 12r's first look was real and its conclusion
     * was still wrong: the block it found in D is the desktop's own Colour Test Bar (see the note at GSLIVE_X0),
     * which the corner-word key counted as ink because it tests colours BEFORE the canvas compare.  The
     * instrument therefore moved to rows 32..47, and the old strip stays in the read as a control on that
     * explanation: a window whose page words ARE the canvas words at the same positions is chrome, and only a
     * window where the two disagree is a writer this file has not named. */
    s_pre_pass++;
    /* Rung 12t(ii): census #1 of the process runs LONE -- box D's recorder session and nothing else -- and
     * censuses 2..4 run 12i's whole grid, each on a band the reset above just made equal to its canvas.
     * D is the box because D is the anomaly: it is the frame's FIRST transfer, it is the one box whose count
     * 12q withheld-and-it-stood, and the block inside it reaches outside its own ask. */
    s_census_only = (s_pre_pass == 1u) ? 3u : (uint32_t)GSLIVE_SH_N;
    s_pre_ok = 0;
    s_pre_old_ok = 0;
    {
        const uint32_t pre_y[2]  = { (uint32_t)GSLIVE_SH_Y0, GSLIVE_OLD_BAND_Y0 };
        gslive_pre_t *pre_box[2] = { s_pre, s_pre_old };
        uint32_t pb, pre_b, pre_x;
        int ok;

        for (pb = 0u; pb < 2u; pb++) {
            ok = 0;
            for (pre_b = 0u; pre_b < (uint32_t)GSLIVE_PIC_ROWS; pre_b++)
                for (pre_x = 0u; pre_x < width; pre_x++)
                    buf[(size_t)pre_b * width + pre_x] = GSREAD_SENT;
            if (ps2_gs_vram_read(0, (int)pre_y[pb], (int)width,
                                 (int)GSLIVE_PIC_ROWS, buf) > 0
                    && ps2_gs_vram_read_err() == GS_READ_BUS_OK) {
                ok = 1;
                for (pre_b = 0u; pre_b < GSLIVE_SH_N; pre_b++)
                    ps2_gslive_pre_rect(&pre_box[pb][pre_b], buf, width, canvas,
                                        s_sh_x0[pre_b], s_sh_x1[pre_b],
                                        pre_y[pb], (uint32_t)GSLIVE_PIC_ROWS);
            }
            if (pb == 0u) s_pre_ok = ok; else s_pre_old_ok = ok;
        }

        /* 12r's own control, on the band that actually holds it: the magenta box has exactly one writer --
         * the send below -- so any magenta here is this read looking at ink that has not been laid yet.
         * Rung 12s makes it certify more than the read's timing: the control now sits INSIDE the band, at
         * x32..95 of rows 32..47, so a clean read there is 12a's missing proof that the strip the six boxes
         * are read out of holds nothing before this census draws on it. */
        s_pre_ctrl_mag = 0u;
        s_pre_ctrl_paint = 0u;
        if (s_pre_ok) {
            for (pre_b = 0u; pre_b < (uint32_t)GSLIVE_PIC_ROWS; pre_b++) {
                const uint32_t *row = buf + (size_t)pre_b * width;
                const uint32_t *crow = canvas + (uint32_t)(GSLIVE_SH_Y0 + pre_b) * width;

                for (pre_x = (uint32_t)GSLIVE_X0; pre_x < (uint32_t)GSLIVE_X1; pre_x++) {
                    if (row[pre_x] == GSQUAD_MAGENTA) s_pre_ctrl_mag++;
                    if (row[pre_x] != crow[pre_x]) s_pre_ctrl_paint++;
                }
            }
        }
    }

    r.x0 = GSLIVE_X0; r.y0 = GSLIVE_Y0;
    r.x1 = GSLIVE_X1; r.y1 = GSLIVE_Y1;
    qw = gs_poster_packet(&r, 1u, GSQUAD_MAGENTA, s_pkt, gs_poster_qwc(1u));
    ps2_stage1_gs_env(&env);
    if (qw < 0 || ps2_gs_draw_env_apply(&env) != 0) {
        ps2_kprintf("[GSLIVE] the control was refused (qw=%d) -- SKIP, its count cannot certify a read\n",
                    (int)qw);
        Ifree(buf);
        return;
    }
    /* Rung 12t: the environment the control is drawn under is applied in a LONE census too -- that is the
     * state D's lone group will rasterise in, and it has to be the same state the grid censuses give it --
     * but the magenta quad itself is one of the sends this census exists to withhold. */
    if (s_census_only >= GSLIVE_SH_N) ps2_gs_gif_send(s_pkt, (uint32_t)qw);

    /* Rung 12a rides the same window as the magenta box: the GIF is drained, the environment is the
     * one the box was drawn with, and nothing prints between this draw and the read below.  Rung 12i
     * puts the recorder's own triangles at both ends of it -- D first, A last -- because the order the
     * transfers leave in is now the variable being measured. */
    ps2_gslive_shading_reset();
    /* Rung 12q asked whether ANY of this census's counted ink depends on its own bytes, and got a split
     * answer: A emptied 891 -> 0 and B emptied 1073 -> 0, so those two really are delivered by this file,
     * while D's band block (1408) and D's moved rect (384 red) stood at their 12o numbers with
     * `mask=0xb, held=3, to the bus=0` -- no census byte ever reached the GS for them.  C moved again
     * (1221 -> 1121, the same shift it made in 12p when only D was held), so the hand shapes answer to
     * D's delivery too, which is a coupling no per-box model has.  That leaves two claims about the same
     * 1408 px and no send-side dial that can separate them: every dial so far has graded a page this
     * census had ALREADY drawn on.  So 12r puts the dial back where the claim lives -- time, not delivery.
     * The mask stays DISARMED (the reset above leaves it 0), which is what makes the whole rung honest:
     * every session goes out as it did at 12o, the shading grid grades again instead of self-gating to
     * SKIP, and the pre-draw read taken above is the only new input.  Rung 12s then moved the geometry: the
     * ask is D's WHOLE window in the new band again, because the narrowing and the y576-591 rect were both
     * dials on a strip that turned out to be the Colour Test Bar, and a dial that grades chrome answers
     * nothing.  With ask == window, 12n's `out=` is identically zero and 12o's moved-rect rows self-gate to
     * SKIP, which is the honest price of the finding. */
    ps2_gslive_shading_session(3u, GSLIVE_SH_D_X0, GSLIVE_SH_D_X1,
                               GSLIVE_SH_Y0, GSLIVE_SH_Y1, 1u);
    ps2_gslive_shading_session(1u, GSLIVE_SH_B_X0, GSLIVE_SH_B_X1,
                               GSLIVE_SH_Y0, GSLIVE_SH_Y1, 0u);
    ps2_gslive_shape_matrix();
    ps2_gslive_shape_hold(1, GSLIVE_SH_A_X0, GSLIVE_SH_A_X1);
    ps2_gslive_shading_session(0u, GSLIVE_SH_A_X0, GSLIVE_SH_A_X1,
                               GSLIVE_SH_Y0, GSLIVE_SH_Y1, 1u);

    us = btron_render_perf_us();
    for (y_start = 0u; y_start < height; y_start += GSREAD_BAND_ROWS) {
        const uint32_t rows = (height - y_start < GSREAD_BAND_ROWS)
                                ? (height - y_start) : GSREAD_BAND_ROWS;

        for (i = 0u; i < width * rows; i++) buf[i] = GSREAD_SENT;
        got = ps2_gs_vram_read(0, (int)y_start, (int)width, (int)rows, buf);
        err = ps2_gs_vram_read_err();
        if (err != GS_READ_BUS_OK) {
            ps2_kprintf("[GSLIVE] page y=%u FAILED: %s -- SKIP, the counts above are a part-page\n",
                        (unsigned int)y_start, gsread_bus_name(err));
            Ifree(buf);
            return;
        }
        qw_sent += (uint32_t)got;
        read_px += width * rows;

        for (i = 0u; i < width * rows; i++) {
            const uint32_t v = buf[i];
            uint32_t x, y;
            int inside;

            if (v == GSREAD_SENT) { sent++; continue; }
            x = i % width;
            y = y_start + i / width;
#if BTRON_PS2_BENCH_GS >= 5
            ps2_bench_ink_census_px(x, y, v);     /* rung 27's second road, on this same read */
#endif
            /* Rung 10 keeps every pixel this loop sees, including the control box and the console's
             * own rows: the magenta box coming back magenta in the P grid is the read's alignment
             * proving itself in the picture, which is a stronger statement than `ctrl=1024` in a
             * count column. */
            gspeak_add(&s_spk_page, x, y, v);
            gspeak_add(&s_spk_canvas, x, y, canvas[y * width + x]);
            if (x >= GSLIVE_X0 && x < GSLIVE_X1 && y >= GSLIVE_Y0 && y < GSLIVE_Y1) {
                /* This rung's own ink, excluded by COORDINATE and not by colour: the box is the
                 * certification and nothing else, so a magenta UI pixel outside it stays census
                 * material and a control that partly failed still shows as ctrl < GSLIVE_PX.  The
                 * exclusion is counted, so `read = sent + box + same + diff_all` is a partition the
                 * row below can be checked against rather than a set of numbers that merely look
                 * plausible next to each other. */
                box_px++;
                if (v == GSQUAD_MAGENTA) { ctrl++; gsread_box(ctrl_box, x, y); }
                continue;
            }
            if (y >= GSLIVE_SH_Y0 && y < GSLIVE_SH_Y1) {
                uint32_t sh;

                /* Rung 12t: attributed to no box, this pixel is band ink, and it is either a corner word or
                 * a ramp pixel.  The box walk below still counts it its own way; nothing is moved here. */
                if (v != canvas[y * width + x]) {
                    bp_n++;
                    gslive_bbox(bp_bbox, x, y);
                    if (v == GSQUAD_RED || v == GSQUAD_GREEN || v == GSQUAD_BLUE) bp_corner++;
                    else bp_field++;
                }
                for (sh = 0u; sh < GSLIVE_SH_N; sh++) {
                    if (x >= s_sh_x0[sh] && x < s_sh_x1[sh]) break;
                }
                if (sh < GSLIVE_SH_N) {
                    /* Counted by coordinate and excluded from the page's own partition the same way
                     * the magenta box is: this rung's ink is its own evidence, and a red pixel that
                     * also entered `ink_v[]` would spend one of eight histogram slots on the
                     * instrument.  The gspeak grids above still hold these pixels, so rung 10's
                     * picture shows the pair and can be looked at rather than read as counts. */
                    const uint32_t prow = y - (uint32_t)GSLIVE_SH_Y0;
                    const uint32_t pcol = (x - s_sh_x0[sh]) >> GSLIVE_PIC_SHIFT;
                    uint32_t bit;

                    sh_tot[sh]++;
                    sh_px++;
                    if (v == GSQUAD_RED)        { bit = GSLIVE_PIC_R;     sh_r[sh]++;
                                                  gslive_bbox(sh_bbox[sh][0], x, y); }
                    else if (v == GSQUAD_GREEN) { bit = GSLIVE_PIC_G;     sh_g[sh]++;
                                                  gslive_bbox(sh_bbox[sh][1], x, y); }
                    else if (v == GSQUAD_BLUE)  { bit = GSLIVE_PIC_B;     sh_b[sh]++;
                                                  gslive_bbox(sh_bbox[sh][2], x, y); }
                    else if (v == canvas[y * width + x]) { bit = GSLIVE_PIC_CANVAS; sh_same[sh]++; }
                    else                                  { bit = GSLIVE_PIC_FIELD;  sh_other[sh]++; }
                    /* Rung 12l's shape tally, straight off the class bit: the first and last rows of a
                     * box separate a rect (its top edge is the whole width) from an apex-up triangle
                     * (one or two px), and they do it whatever the shading flag says, so this grades a
                     * flat triangle as well as a gradient one. */
                    if (bit != GSLIVE_PIC_CANVAS) {
                        if (prow == 0u) sh_top[sh]++;
                        else if (prow == (uint32_t)GSLIVE_PIC_ROWS - 1u) sh_bot[sh]++;
                        /* Rung 12n's attribution count, widened by 12o to the other axis: this pixel is
                         * painted, and it sits outside the RECT the session asked.  Boxes with no ask of
                         * their own -- the hand controls are not drawn by a session -- are read AT their
                         * window, so their count is 0 by definition and `out=` can only grow where a
                         * geometry dial moved.  With 12o's ask entirely below the band, every painted band
                         * pixel of that box is outside it, so out= == px is 12o's reading of the block
                         * holding; out= == 0 would mean the band emptied itself, which is the other answer. */
                        if (s_sh_ask_x1[sh] != 0
                                && ((int32_t)x < s_sh_ask_x0[sh]
                                    || (int32_t)x >= s_sh_ask_x1[sh]
                                    || (int32_t)y < s_sh_ask_y0[sh]
                                    || (int32_t)y >= s_sh_ask_y1[sh]))
                            sh_outside[sh]++;
                    }
                    if (prow < GSLIVE_PIC_ROWS && pcol < GSLIVE_PIC_COLS)
                        s_band_pic[sh][prow][pcol] =
                            (uint8_t)(s_band_pic[sh][prow][pcol] | bit);
                    continue;
                }
            }
            if (y >= GSLIVE_SP_Y0 && y < GSLIVE_SP_Y1) {
                /* Rung 12j: this pixel is outside every box, in the rows the band does not read, and is
                 * bit-exactly one of the three corner words -- so either one of this rung's triangles was
                 * rasterised HERE, or the desktop painted this exact word and put it on its own canvas
                 * too.  The second case is what `sp_same` counts, and it is why the tally asks the canvas
                 * rather than calling every corner-coloured pixel outside a box a spill: an uploaded UI
                 * pixel is page == canvas, ink this census did not lay is not.
                 *
                 * No `continue` here, unlike the box branch above: a spill pixel is still a page pixel,
                 * and the partition `read = sent + box + same + diff_all` has to stay checkable. */
                int sc = -1;
                int claimed = 0;   /* rung 12o: inside a moved ask, so mv_* names this pixel already */
                uint32_t mb;

                /* Rung 12o: a session asked for a rect OUTSIDE the band (only the dialled box can be in
                 * that state, because every other ask is at the band's own rows), and this pixel lies inside
                 * it.  Classed by the same three corner words and the same `differs from the canvas` law the
                 * box branch uses, so the two readings are the same measurement at two positions. */
                for (mb = 0u; mb < GSLIVE_SH_N; mb++) {
                    if (s_sh_ask_x1[mb] == 0) continue;            /* never asked */
                    if (s_sh_ask_y0[mb] >= (int32_t)GSLIVE_SH_Y0
                            && s_sh_ask_y1[mb] <= (int32_t)GSLIVE_SH_Y1) continue; /* asked IN the band */
                    if ((int32_t)x >= s_sh_ask_x0[mb] && (int32_t)x < s_sh_ask_x1[mb]
                            && (int32_t)y >= s_sh_ask_y0[mb] && (int32_t)y < s_sh_ask_y1[mb]) {
                        claimed = 1;
                        mv_box = mb;
                        mv_tot++;
                        if (v == GSQUAD_RED)        { mv_r++; mv_painted++; gslive_bbox(mv_bbox, x, y); }
                        else if (v == GSQUAD_GREEN) { mv_g++; mv_painted++; gslive_bbox(mv_bbox, x, y); }
                        else if (v == GSQUAD_BLUE)  { mv_b++; mv_painted++; gslive_bbox(mv_bbox, x, y); }
                        else if (v == canvas[y * width + x]) mv_same++;
                        else { mv_other++; mv_painted++; gslive_bbox(mv_bbox, x, y); }
                        break;
                    }
                }
                if (sc < 0 && !claimed) {
                    /* 12j's aggregate keeps its original meaning -- corner colours off-box and NOT named by
                     * any ask of this census -- so its bbox cannot merge an unknown writer with a dialled
                     * one.  A corner word inside the moved rect is counted once, by mv_*. */
                    if (v == GSQUAD_RED)               sc = 0;
                    else if (v == GSQUAD_GREEN)        sc = 1;
                    else if (v == GSQUAD_BLUE)         sc = 2;
                    if (sc >= 0) {
                        if (v == canvas[y * width + x]) sp_same++;
                        else { sp_n[sc]++; gslive_bbox(sp_bbox[sc], x, y); }
                    }
                }
            }
            if (win_px && (int)x >= cx0 && (int)x < cx1 && (int)y >= cy0 && (int)y < cy1) {
                /* Rung 6b's per-row tally, restricted to the client rect and keyed on the row rather
                 * than on the band: the two reads have to be comparable frame to frame even though
                 * the bands themselves are a read-stride detail of this file, not a property of the
                 * picture.  A pixel equal to the canvas is either still untouched or has been put
                 * BACK to the canvas, and telling those two apart is the whole question, so it is
                 * answered by the row's count moving down and not by the pixel's colour. */
                const uint32_t row = (uint32_t)((int)y - cy0);
                if (row < PS2_SCREEN_HEIGHT) {
                    if (v == canvas[y * width + x]) {
                        s_live_cur_same[row]++;
                        /* Rung 6c: what the page was put BACK to, colour by colour.  The ink
                         * histogram below cannot see these -- they are the pixels that agree with
                         * the canvas -- and they are the whole of the eraser's evidence trail. */
                        for (k = 0u; k < cv_bins; k++) {
                            if (cv_v[k] != v) continue;
                            cv_n[k]++;
                            gsread_box(cv_box[k], x, y);
                            break;
                        }
                        if (k >= cv_bins) {
                            if (cv_bins < GSLIVE_TOP) {
                                cv_v[cv_bins] = v;
                                cv_n[cv_bins] = 1u;
                                cv_box[cv_bins][0] = cv_box[cv_bins][2] = (int)x;
                                cv_box[cv_bins][1] = cv_box[cv_bins][3] = (int)y;
                                cv_bins++;
                            } else cv_rare++;
                        }
                    } else s_live_cur_ink[row]++;
                }
            }
            if (v == canvas[y * width + x]) { same++; continue; }            diff_all++;
            gsread_box(all_box, x, y);
            if (v == GSLIVE_INK || v == GSLIVE_BG) { diff_console++; continue; }
            diff_ink++;
            inside = ((int)x >= cx0 && (int)x < cx1 && (int)y >= cy0 && (int)y < cy1);
            if (inside) { diff_win++; gsread_box(win_box, x, y); }

            for (k = 0u; k < bins; k++) {
                if (ink_v[k] != v) continue;
                ink_n[k]++;
                if (inside) ink_in[k]++;
                gsread_box(ink_box[k], x, y);
                break;
            }
            if (k < bins) continue;
            if (bins < GSLIVE_TOP) {
                ink_v[bins] = v;
                ink_n[bins] = 1u;
                ink_in[bins] = inside ? 1u : 0u;
                ink_box[bins][0] = ink_box[bins][2] = (int)x;
                ink_box[bins][1] = ink_box[bins][3] = (int)y;
                bins++;
            } else {
                /* No slot: counted, not evicted.  Evicting a kept colour would make the rows below
                 * disagree with themselves frame to frame, and a census whose histogram is not the
                 * same histogram next time cannot be quoted against a fix. */
                rare++;
            }
        }
    }
    us = btron_render_perf_us() - us;
#if BTRON_PS2_BENCH_GS >= 5
    /* Rung 27, half 3: the entry read and this walk's own road, for the same pixels.  It prints here
     * rather than next to the maps because the walk above is the second road's source, and because
     * the maps' block reads would otherwise be a third thing between the two samples. */
    if (w && w->gl_layer) ps2_bench_ink_cross(&w->client, width, canvas);
#endif
    /* Rung 21's pixels, on rung 10's gate, and BEFORE the replay below for the reason the replay's own
     * comment gives: it sends and re-reads bands into `buf`, so after it the buffer is not this
     * present's page any more. */
    if (sent == 0u && w && w->gl_layer && win_px != 0u &&
        diff_win > win_px / 2u && s_gspix_printed < 2u) {
        s_gspix_printed++;
        gspeak_pixels(buf, GSREAD_BAND_QWC * 4u, width, cx0, cy0, cx1, cy1);
    }
    /* Rung 21b/21c/21d: the whole client as luminance, alpha-owner and hue maps, once, on the same
     * gate and for the same reason -- before the replay, which rewrites `buf`. */
    if (sent == 0u && w && w->gl_layer && win_px != 0u &&
        diff_win > win_px / 2u && s_gspix_mapped < 1u) {
        s_gspix_mapped++;
        gspeak_page_map(buf, GSREAD_BAND_QWC * 4u, width, cx0, cy0, cx1, cy1);
    }
    /* Rung 12u: replay D's captured lone group four times under four different PRIM sources.  It runs here,
     * after this census's own page has been graded into the counters above and before `buf` goes back, so it
     * cannot disturb any count the rows above print -- and it prints nothing, because a print would upload
     * the console band between one variant's send and that variant's read. */
    ps2_gslive_u_probe(buf, canvas, width);
    Ifree(buf);

    ps2_kprintf("[GSLIVE] win: client=%d,%d..%d,%d px=%u gl_layer=%d img_valid=%d visible=%d\n",
                cx0, cy0, cx1, cy1, (unsigned int)win_px,
                w ? (int)w->gl_layer : -1, w ? (int)w->img_valid : -1,
                w ? (int)w->visible : -1);
    ps2_kprintf("[GSLIVE] page: read=%u same=%u diff_all=%u console=%u ink=%u box=%u ctrl=%u "
                "shading=%u sent=%u %u us %u qw\n",
                (unsigned int)read_px, (unsigned int)same, (unsigned int)diff_all,
                (unsigned int)diff_console, (unsigned int)diff_ink, (unsigned int)box_px,
                (unsigned int)ctrl, (unsigned int)sh_px, (unsigned int)sent,
                (unsigned int)us, (unsigned int)qw_sent);
    ps2_kprintf("[GSLIVE] boxes: all=%d,%d..%d,%d window=%d,%d..%d,%d in_win=%u control=%u/%u "
                "%d,%d..%d,%d rare=%u\n",
                all_box[0], all_box[1], all_box[2], all_box[3],
                win_box[0], win_box[1], win_box[2], win_box[3],
                (unsigned int)diff_win, (unsigned int)ctrl, (unsigned int)GSLIVE_PX,
                ctrl_box[0], ctrl_box[1], ctrl_box[2], ctrl_box[3], (unsigned int)rare);
    /* The four hottest inks, each named ONCE.  The search has to seed itself on an UNPRINTED bin: it
     * only ever compares a candidate against `best`, so starting `best` at bin 0 re-selects bin 0 on
     * every pass from the second row onward whenever bin 0 is the hottest -- which, being the largest
     * bin, it always is.  The rung then printed the same colour four times, and its own closing
     * instruction ("read the ink colours and boxes above") had one colour to read.  That is the whole
     * difference between "this page is one flat plate" and "this page is this plate plus these inks",
     * and only the histogram can answer the second question. */
    for (k = 0u; k < 4u && k < bins; k++) {
        int seeded = 0;

        best = 0u;
        for (i = 0u; i < bins; i++) {
            if (printed[i]) continue;
            if (!seeded || ink_n[i] > ink_n[best]) { best = i; seeded = 1; }
        }
        if (!seeded) break;
        printed[best] = 1;
        ps2_kprintf("[GSLIVE] ink: 0x%08x n=%u in=%u box=%d,%d..%d,%d\n",
                    (unsigned int)ink_v[best], (unsigned int)ink_n[best],
                    (unsigned int)ink_in[best],
                    ink_box[best][0], ink_box[best][1], ink_box[best][2], ink_box[best][3]);
    }

    /* Rung 12a's and 12d's reading, printed where its counts can be checked against the certification
     * above.  `painted` is the box minus the pixels that still hold their canvas value, and it is the
     * denominator every reading needs: a triangle covers half its rect, so A's and B's 2,048-px boxes
     * hold ~1,024 painted px and C/D/E's 1,920-px boxes ~960, and a threshold in whole pixels would
     * call a half-drawn box a flat one.  A box is GRADIENT when a quarter of its painted px are none of
     * its three corners: an interpolated triangle's interior is everything between its corners, so the
     * field is most of the box and any threshold well under a half separates the two readings without
     * needing to know how the rasteriser walks a span. */
    {
        char grad[16];
        uint32_t n_grad = 0u, n_empty = 0u, n_flat = 0u, gn = 0u;
        uint32_t b;

        for (b = 0u; b < GSLIVE_SH_N; b++) {
            const uint32_t painted = sh_tot[b] - sh_same[b];
            const char *kind;
            char win = '-';   /* for a flat box, the corner that won -- and since red is v0, green is
                               * v1 and blue is v2, the same letter names WHICH VERTEX this shape's GS
                               * drew with, the half of rung 12a's answer a packet cannot show. */

            if (painted == 0u) {
                kind = "lays NOTHING";
                n_empty++;
            } else if (sh_other[b] > painted / 4u) {
                kind = "INTERPOLATES";
                n_grad++;
                if (gn + 1u < sizeof(grad)) grad[gn++] = (char)('A' + b);
            } else {
                win = (sh_r[b] >= sh_g[b] && sh_r[b] >= sh_b[b]) ? 'R'
                    : ((sh_g[b] >= sh_b[b]) ? 'G' : 'B');
                kind = "one FLAT colour";
                n_flat++;
            }
            ps2_kprintf("[GSLIVE] shade %s: tot=%u painted=%u red=%u green=%u blue=%u other=%u "
                        "canvas=%u -> %s %c\n", s_sh_label[b],
                        (unsigned int)sh_tot[b], (unsigned int)painted,
                        (unsigned int)sh_r[b], (unsigned int)sh_g[b], (unsigned int)sh_b[b],
                        (unsigned int)sh_other[b], (unsigned int)sh_same[b],
                        kind, win);
        }
        grad[gn] = '\0';
        ps2_kprintf("[GSLIVE] shapes: recorder %u group(s) / %u refused, %u captured send(s),"
                    " hand matrix %u sent / %u refused; %u gradient (%s), %u flat, %u empty\n",
                    (unsigned int)s_sh_groups, (unsigned int)s_sh_refused,
                    (unsigned int)s_cap_sends,
                    (unsigned int)s_shape_sent, (unsigned int)s_shape_refused,
                    (unsigned int)n_grad, grad,
                    (unsigned int)n_flat, (unsigned int)n_empty);

        /* Rung 12j -- WHERE, not how many.  The counts above grade a box as a gradient or a flat colour,
         * and 12i's answer ("the variable is the frame's ORDER") only means shading if the flat boxes are
         * flat AT THEIR OWN COORDINATES.  They did not look like that: box D came back `painted=1408`,
         * its whole 88x16 rect, split red=1024 blue=384, and a 1,024-px red region inside an 88-wide box
         * is a 64-wide full-height column, which no triangle in this census has.  Box B came back
         * `painted=1920` of 2,048 in TWO colours, which is two triangles' coverage for one packet.  So
         * the same walk now records the bounding box of each corner colour inside its box, and of the
         * three corner words OUTSIDE every box, and this row prints both.
         *
         * `%c %u-%ux%u` is the x range and the number of rows that colour spans; a triangle's own corner
         * colour spans the full 16 rows only at its wide end and a few rows at the apex, so a colour that
         * spans 16 rows across a stripe of x is a rect, and the SOLID rows below name exactly that.  A
         * box that never got its colour prints `%c n=0` rather than an invented range. */
        {
            static const char sp_chr[3] = { 'R', 'G', 'B' };
            static const char *const sp_word[3] = { "red", "green", "blue" };
            uint32_t b2, sp_all = 0u, sp_i;

            for (b2 = 0u; b2 < GSLIVE_SH_N; b2++) {
                ps2_kprintf("[GSLIVE] 12j %c:", s_sh_label[b2][0]);
                for (col = 0u; col < 3u; col++) {
                    const uint32_t cnt = (col == 0u) ? sh_r[b2] : ((col == 1u) ? sh_g[b2] : sh_b[b2]);

                    if (cnt == 0u) {
                        ps2_kprintf(" %c n=0", sp_chr[col]);
                        continue;
                    }
                    ps2_kprintf(" %c %u-%ux%u n=%u", sp_chr[col],
                                (unsigned int)sh_bbox[b2][col][0], (unsigned int)sh_bbox[b2][col][1],
                                (unsigned int)(sh_bbox[b2][col][3] - sh_bbox[b2][col][2] + 1u),
                                (unsigned int)cnt);
                }
                ps2_kprintf("\n");
            }
            /* A colour that fills its own bounding box solidly is not a corner of a triangle.  Quoted per
             * box so the row that says `one FLAT colour` can be read against the SHAPE it was counted on:
             * SOLID means the geometry is wrong, no SOLID row with a full box means the rasteriser laid a
             * real triangle and only the interpolation failed. */
            for (b2 = 0u; b2 < GSLIVE_SH_N; b2++) {
                for (col = 0u; col < 3u; col++) {
                    const uint32_t cnt = (col == 0u) ? sh_r[b2] : ((col == 1u) ? sh_g[b2] : sh_b[b2]);
                    const uint32_t area = (sh_bbox[b2][col][1] - sh_bbox[b2][col][0] + 1u)
                                        * (sh_bbox[b2][col][3] - sh_bbox[b2][col][2] + 1u);

                    if (cnt >= area && cnt != 0u) {
                        ps2_kprintf("[GSLIVE] 12j SOLID %c %s: x%u-%u y%u-%u n=%u fills its own box\n",
                                    s_sh_label[b2][0], sp_word[col],
                                    (unsigned int)sh_bbox[b2][col][0], (unsigned int)sh_bbox[b2][col][1],
                                    (unsigned int)sh_bbox[b2][col][2], (unsigned int)sh_bbox[b2][col][3],
                                    (unsigned int)cnt);
                    }
                }
            }
            for (sp_i = 0u; sp_i < 3u; sp_i++) sp_all += sp_n[sp_i];
            ps2_kprintf("[GSLIVE] 12j spill off-box rows %u..%u: R=%u G=%u B=%u (equal-canvas %u)\n",
                        (unsigned int)GSLIVE_SP_Y0, (unsigned int)GSLIVE_SP_Y1 - 1u,
                        (unsigned int)sp_n[0], (unsigned int)sp_n[1], (unsigned int)sp_n[2],
                        (unsigned int)sp_same);
            for (sp_i = 0u; sp_i < 3u; sp_i++) {
                if (sp_n[sp_i] == 0u) continue;
                ps2_kprintf("[GSLIVE] 12j spill %s: x%u-%u y%u-%u n=%u\n", sp_word[sp_i],
                            (unsigned int)sp_bbox[sp_i][0], (unsigned int)sp_bbox[sp_i][1],
                            (unsigned int)sp_bbox[sp_i][2], (unsigned int)sp_bbox[sp_i][3],
                            (unsigned int)sp_n[sp_i]);
            }
            ps2_kprintf("[GSLIVE] 12j verdict: %s\n",
                        (sp_all == 0u)
                          ? "nothing this rung laid is outside its own box, so a FLAT box is a SHADING "
                              "answer and 12i's ORDER reading stands"
                          : "corner-coloured ink landed off-box, so at least one triangle was rasterised "
                              "somewhere other than its own rect -- the flat boxes are a GEOMETRY answer, "
                              "not a shading one, and 12i's grid has to be re-read that way");
        }

        /* Rung 12k -- the band as a PICTURE, because 12j's numbers were being read three ways in one
         * conversation.  A bounding box says how far a colour reached; only a picture says whether it
         * reached as a triangle's hypotenuse or as the straight edge of a rect, and that distinction is
         * what separates "the interpolator never ran" from "the geometry landed wrong".  Each box gets
         * the corners it ASKED for on the header line, so the reader can hold the picture up against the
         * triangle it should be: v0 red at the box's left on the bottom row, v1 green at the right on the
         * same row, v2 blue at the apex on the band's first row.  `want` is what was sent, not what was
         * read back -- 12a's certification is that the packet left, so anything else on the picture is the
         * GS's answer to it.
         *
         * One character per FOUR pixels of x, one line per band row: a 128-px box is 32 columns and the
         * whole band prints in six headers and 96 rows.  The resolution is the compromise -- at one char
         * per pixel a 128-px line is 137 chars and wraps the 80-col console, and at 8 px/col a triangle's
         * 1-px-per-row hypotenuse disappears.  At 4 px a column is a span of 4 rows of the hypotenuse, so
         * a slope still reads as a slope. */
        {
            uint32_t b3, pc;
            char pic[GSLIVE_PIC_COLS + 1];

            ps2_kprintf("[GSLIVE] 12k mask: R red  G green  B blue  Y=R+G  M=R+B  C=G+B  W all three\n");
            ps2_kprintf("[GSLIVE] 12k f = field only; lowercase r g y b m c w = the same, in a cell\n");
            ps2_kprintf("[GSLIVE]     that also holds interpolated field ink (a ramp's edge)\n");
            ps2_kprintf("[GSLIVE] 12k '.' = nothing but the canvas here; a space = past this box\n");
            for (b3 = 0u; b3 < GSLIVE_SH_N; b3++) {
                const uint32_t painted = sh_tot[b3] - sh_same[b3];
                const char *kind = (painted == 0u) ? "NOTHING"
                                   : ((sh_other[b3] > painted / 4u) ? "INTERP" : "FLAT");
                const uint32_t cols = (s_sh_x1[b3] - s_sh_x0[b3]) >> GSLIVE_PIC_SHIFT;
                /* Rung 12n makes `want` mean what it always claimed to mean -- the corners that were SENT --
                 * by reading them off the ask rather than off the read window.  A box with no session ask
                 * falls back to its window, which is what every box was until this rung, so the five rows
                 * that are not dialled keep the header 12k printed. */
                const uint32_t ax0 = (s_sh_ask_x1[b3] != 0u)
                                       ? (uint32_t)s_sh_ask_x0[b3] : s_sh_x0[b3];
                const uint32_t ax1 = (s_sh_ask_x1[b3] != 0u)
                                       ? (uint32_t)s_sh_ask_x1[b3] : s_sh_x1[b3];
                const uint32_t ay0 = (s_sh_ask_y1[b3] != 0u)
                                       ? (uint32_t)s_sh_ask_y0[b3] : (uint32_t)GSLIVE_SH_Y0;
                const uint32_t ay1 = (s_sh_ask_y1[b3] != 0u)
                                       ? (uint32_t)s_sh_ask_y1[b3] - 1u
                                       : (uint32_t)GSLIVE_SH_Y1 - 1u;
                const int ask_moved = (ax0 != s_sh_x0[b3] || ax1 != s_sh_x1[b3]
                                        || ay0 != (uint32_t)GSLIVE_SH_Y0
                                        || ay1 != (uint32_t)GSLIVE_SH_Y1 - 1u);

                ps2_kprintf("[GSLIVE] 12k %c: x%u-%u %s  want R(%u,%u) G(%u,%u) B(%u,%u)\n",
                            s_sh_label[b3][0], (unsigned int)s_sh_x0[b3],
                            (unsigned int)s_sh_x1[b3] - 1u, kind,
                            (unsigned int)ax0, (unsigned int)ay1,
                            (unsigned int)ax1 - 1u, (unsigned int)ay1,
                            (unsigned int)((ax0 + ax1) / 2u), (unsigned int)ay0);
                if (ask_moved) {
                    ps2_kprintf("[GSLIVE] 12k %c: ASKED x%u-%u y%u-%u, READ at x%u-%u y%u-%u\n",
                                s_sh_label[b3][0], (unsigned int)ax0, (unsigned int)ax1 - 1u,
                                (unsigned int)ay0, (unsigned int)ay1,
                                (unsigned int)s_sh_x0[b3], (unsigned int)s_sh_x1[b3] - 1u,
                                (unsigned int)GSLIVE_SH_Y0, (unsigned int)GSLIVE_SH_Y1 - 1u);
                }
                for (pr = 0u; pr < GSLIVE_PIC_ROWS; pr++) {
                    for (pc = 0u; pc < cols; pc++) {
                        const uint32_t m = s_band_pic[b3][pr][pc];

                        pic[pc] = (m & 15u) ? s_pic_chr[m & 15u]
                                            : ((m & GSLIVE_PIC_CANVAS) ? '.' : ' ');
                    }
                    pic[cols] = '\0';
                    ps2_kprintf("[GSLIVE] 12k %c y%u %s\n", s_sh_label[b3][0],
                                (unsigned int)(GSLIVE_SH_Y0 + pr), pic);
                }
            }
        }

        /* Rung 12l's own reading -- the SHAPE rows, because the colour law cannot see a fixed box B (a
         * flat triangle has no field, so it still grades FLAT) and 12k's pictures cannot be diffed
         * between runs by eye.  `top=` is painted px in the band's first row: 12k measured D with its
         * whole 88-px top edge laid, B with its whole 128, and A with exactly one pixel (the apex).
         * `bot=` is the last row, which 12k showed as the ramp boxes' unwritten base -- it is printed
         * because a rect that reaches y575 and a triangle that stops at y574 is itself a difference the
         * next rung may need. */
        {
            static const char *const v_tag[4] = {
                "PRE IS THE LATCH", "PRE FIXED D ONLY", "PRE FIXED B, NOT D", "PRE IS NOT THE VARIABLE"
            };
            static const char *const v_body[4][2] = {
                { "D and B are both triangles now: the register D's tag wrote",
                  "carried to the next PRE=1 group -- the latch is the cause" },
                { "only D moved: B kept its rect with TRI already in the",
                  "register, so B's flatness is IIP=0 or position, not PRE" },
                { "only B moved: D still laid its rect with its own PRIM",
                  "honoured, so PRE is not the front-of-frame variable" },
                { "neither moved -- but this rung cannot fire unless the inherited",
                  "PRIM differs from TRI+IIP, and 12m tests the tag field directly" }
            };
            uint32_t b4;
            const uint32_t a_at = s_sh_sess_first[0u], d_at = s_sh_sess_first[3u];
            const int d_tri = (sh_top[3u] <= 4u);
            const int b_tri = (sh_top[1u] <= 4u);
            const int case_i = (d_tri && b_tri) ? 0 : ((d_tri && !b_tri) ? 1
                                                   : ((!d_tri && b_tri) ? 2 : 3));
            /* Rung 12n retires this dial, and a retired dial must not keep voting: 12l's four-way answer
             * is only this run's answer if D went out patched AND asked at its whole window.  With either
             * changed, the shape rows above are 12n's evidence, not 12l's, and the verdict says SKIP. */
            const int armed = (s_pre0_box == 3u && s_prim_pt_box == GSLIVE_SH_N
                                && s_sh_ask_x0[3u] == (int32_t)s_sh_x0[3u]
                                && s_sh_ask_x1[3u] == (int32_t)s_sh_x1[3u]
                                && s_sh_ask_x0[1u] == (int32_t)s_sh_x0[1u]
                                && s_sh_ask_x1[1u] == (int32_t)s_sh_x1[1u]);

            ps2_kprintf("[GSLIVE] 12l dial %s: D's tag bit 46 PRE 1->0, so its own PRIM is\n",
                        armed ? "ARMED" : "OFF");
            ps2_kprintf("[GSLIVE]   honoured; ps2sdk gif_tags.h names that bit GIF_PRE_ENABLE =\n");
            ps2_kprintf("[GSLIVE]   \"Disable PRIM field output\", so a PRE=1 group draws under the\n");
            ps2_kprintf("[GSLIVE]   LAST tag's PRIM.  B and A keep PRE=1: B is the latch cell.\n");
            ps2_kprintf("[GSLIVE]   12k's baseline: D laid top=88/88, B laid top=128/128, A top=1.\n");
            ps2_kprintf("[GSLIVE] 12l shape (row %u = a rect's whole width, a triangle's 1-2 px):\n",
                        (unsigned int)GSLIVE_SH_Y0);
            for (b4 = 0u; b4 < GSLIVE_SH_N; b4++) {
                const uint32_t w = s_sh_x1[b4] - s_sh_x0[b4];
                const int kind = (sh_top[b4] <= 4u) ? 0 : ((sh_top[b4] * 2u >= w) ? 2 : 1);

                ps2_kprintf("[GSLIVE] 12l %c: top=%u/%u bot=%u/%u -> %s\n", s_sh_label[b4][0],
                            (unsigned int)sh_top[b4], (unsigned int)w,
                            (unsigned int)sh_bot[b4], (unsigned int)w, gslive_sh_kind[kind]);
            }
            if (a_at < GSLIVE_CAP_SENDS && d_at < GSLIVE_CAP_SENDS) {
                const uint64_t hx = s_cap_send[d_at][0] ^ s_cap_send[a_at][0];

                ps2_kprintf("[GSLIVE] 12l tag check: D PRE=%u PRIM=0x%02x, A PRE=%u PRIM=0x%02x\n",
                            (unsigned int)gslive_pre_of(s_cap_send[d_at]),
                            (unsigned int)gslive_prim_of(s_cap_send[d_at]),
                            (unsigned int)gslive_pre_of(s_cap_send[a_at]),
                            (unsigned int)gslive_prim_of(s_cap_send[a_at]));
                if (armed) {
                    ps2_kprintf("[GSLIVE] 12l tag xor: hi=0x%08x lo=0x%08x -> %s\n",
                                (unsigned int)(hx >> 32), (unsigned int)(hx & 0xFFFFFFFFu),
                                (hx == ((uint64_t)1u << 46)) ? "exactly bit 46, nothing else moved"
                                                             : "MORE THAN bit 46 moved");
                } else {
                    /* With the dial off, neither tag is patched and this xor says nothing about PRE -- it
                     * is the two boxes' ordinary difference, and printing it as though 12l had fired is how
                     * a retired rung got read as a live measurement once.  The check row above still stands:
                     * it is what certifies the dials really are off. */
                    ps2_kprintf("[GSLIVE] 12l tag xor: not printed -- the dial is off, so this xor is\n");
                    ps2_kprintf("[GSLIVE]   not 12l's evidence; D's PRE above just says unpatched\n");
                }
            } else {
                ps2_kprintf("[GSLIVE] 12l tag check: SKIP -- D's or A's group was not captured\n");
            }
            if (!armed) {
                ps2_kprintf("[GSLIVE] 12l verdict: SKIP -- 12l's dial is not the one armed here (D's\n");
                ps2_kprintf("[GSLIVE]   PRE patch is off, or its ask moved), so the shape rows above are\n");
                ps2_kprintf("[GSLIVE]   12n's evidence.  12l measured NO on 2026-10-18, conditionally.\n");
            } else {
                ps2_kprintf("[GSLIVE] 12l verdict: %s\n", v_tag[case_i]);
                ps2_kprintf("[GSLIVE]   %s\n", v_body[case_i][0]);
                ps2_kprintf("[GSLIVE]   %s\n", v_body[case_i][1]);
            }
        }

        /* Rung 12m's reading, and the reason it exists is that 12l's verdict has a hole in it.  Clearing
         * PRE changes what a group draws only if the PRIM register the group would otherwise inherit
         * DIFFERS from the TRI+IIP its tag carries -- and no rung here ever read that register, so "PRE is
         * not the variable" is proven against a latch that may already have held the right value.  12m
         * closes the hole by making the tag's ask WRONG on purpose: box D goes out PRE=0 with its whole
         * PRIM field zeroed, which the SDK's own op table reads as POINT, flat, untextured, context 1, no
         * FIX.  A tag that asks for points and lays points means the field rules and 12l's block was laid
         * by TRI+IIP, so the flat boxes are a VERTEX or CLIP answer; a tag that asks for points and still
         * lays the same 88x16 block means no PRIM field of this group reaches the rasteriser, and the next
         * rung has to write the register itself.  Either way the diagnosis narrows, which is what the rung
         * was designed to buy.
         *
         * B is no longer a propagation cell, because 12m's answer does not depend on it: it is the control
         * that says the census has not moved, and if it stops being the RECT 12l measured then neither
         * verdict can be read from this run.  The verdict is also gated on the captured tag decoding as
         * patched, so a patch that never left the EE cannot come back as a measurement. */
        {
            static const char *const m_tag[3] = {
                "THE TAG'S PRIM FIELD RULES", "THE TAG'S PRIM FIELD IS IGNORED", "CONTROL MOVED: READ NOTHING"
            };
            static const char *const m_body[3][2] = {
                { "an ask for POINT laid points, so 12l's block WAS laid by TRI+IIP:",
                  "the flat boxes are a vertex/clip answer, not a primitive one" },
                { "an ask for POINT laid the same block, so this group's tag PRIM field",
                  "is not what the GS rasterises by -- write the register and re-read" },
                { "B is not the RECT that 12l measured, so this census is not 12l's and",
                  "neither answer above can be taken from these rows" }
            };
            const uint32_t wd = s_sh_x1[3u] - s_sh_x0[3u];
            const uint32_t wb = s_sh_x1[1u] - s_sh_x0[1u];
            const uint32_t dm = s_sh_sess_first[3u], am = s_sh_sess_first[0u];
            const int have_dm = (dm < GSLIVE_CAP_SENDS && s_cap_send_words[dm] != 0u);
            const int have_am = (am < GSLIVE_CAP_SENDS && s_cap_send_words[am] != 0u);
            const uint32_t dpre = have_dm ? gslive_pre_of(s_cap_send[dm]) : 2u;
            const uint32_t dprim = have_dm ? gslive_prim_of(s_cap_send[dm]) : 0x7FFu;
            /* Two things must hold for this row to be 12m's answer: the tag that left the EE is the tag
             * 12m patched (measured, not assumed), AND the geometry the patched tag asked for is the box
             * the shape numbers are read across -- rung 12n narrows exactly that, so 12m's "same block"
             * comparison has to stop claiming once the ask and the window are different things. */
            const int prim_dial = (s_prim_pt_box == 3u);
            const int same_geo = (s_sh_ask_x0[3u] == (int32_t)s_sh_x0[3u]
                                   && s_sh_ask_x1[3u] == (int32_t)s_sh_x1[3u]);
            const int certified = (have_dm && dpre == 0u && dprim == 0u
                                    && prim_dial && same_geo);
            const int scattered = (sh_top[3u] * 2u <= wd);
            const int b_stable = (sh_top[1u] >= wb);
            const int case_m = !certified ? -1 : (!b_stable ? 2 : (scattered ? 0 : 1));
            uint32_t b5;

            ps2_kprintf("[GSLIVE] 12m dial %s: D's tag ALSO has PRIM (bits 47-57) zeroed, so the\n",
                        prim_dial ? "ARMED" : "OFF");
            ps2_kprintf("[GSLIVE]   authoritative ask in this frame is POINT -- flat, untextured,\n");
            ps2_kprintf("[GSLIVE]   ctx1, no FIX.  NLOOP, EOP, FLG, NREG, the REGS list and all six\n");
            ps2_kprintf("[GSLIVE]   vertex words are the recorder's own, unchanged from 12l.\n");
            ps2_kprintf("[GSLIVE]   12l's xor row now reads MORE THAN bit 46, and that is this field.\n");
            ps2_kprintf("[GSLIVE]   12l laid D as top=88/88 bot=88/88 painted=1408 nofield: the ask\n");
            ps2_kprintf("[GSLIVE]   to compare against is TRI+IIP -> block, POINT -> ?\n");
            ps2_kprintf("[GSLIVE] 12m shape and field, two words because one hid that C and E each\n");
            ps2_kprintf("[GSLIVE]   lay a solid rect AND a gradient triangle in the same box:\n");
            for (b5 = 0u; b5 < GSLIVE_SH_N; b5++) {
                const uint32_t w5 = s_sh_x1[b5] - s_sh_x0[b5];
                const uint32_t p5 = sh_tot[b5] - sh_same[b5];
                const int kind = (sh_top[b5] <= 4u) ? 0 : ((sh_top[b5] * 2u >= w5) ? 2 : 1);
                const int fld = (p5 != 0u && sh_other[b5] > p5 / 4u);

                ps2_kprintf("[GSLIVE] 12m %c: top=%u/%u painted=%u %s %s\n", s_sh_label[b5][0],
                            (unsigned int)sh_top[b5], (unsigned int)w5, (unsigned int)p5,
                            gslive_sh_kind[kind], fld ? "FIELD" : "nofield");
            }
            if (have_dm && have_am) {
                ps2_kprintf("[GSLIVE] 12m tag decode: D PRE=%u PRIM=0x%03x, A PRE=%u PRIM=0x%03x\n",
                            (unsigned int)dpre, (unsigned int)dprim,
                            (unsigned int)gslive_pre_of(s_cap_send[am]),
                            (unsigned int)gslive_prim_of(s_cap_send[am]));
            } else {
                ps2_kprintf("[GSLIVE] 12m tag decode: SKIP -- D's or A's group was not captured\n");
            }
            if (case_m < 0) {
                ps2_kprintf("[GSLIVE] 12m verdict: SKIP -- this run is not 12m's run: the dial is off or\n");
                ps2_kprintf("[GSLIVE]   the tag that left is not the tag it patched, or rung 12n moved the\n");
                ps2_kprintf("[GSLIVE]   geometry the shape rows measure.  12m measured IGNORED.\n");
            } else {
                ps2_kprintf("[GSLIVE] 12m verdict: %s\n", m_tag[case_m]);
                ps2_kprintf("[GSLIVE]   %s\n", m_body[case_m][0]);
                ps2_kprintf("[GSLIVE]   %s\n", m_body[case_m][1]);
            }
        }

        /* Rung 12n's own reading -- WHOSE ink.  12l changed one bit of box D's tag and 12m changed the
         * tag's whole primitive ask, and both times the captured bytes proved the patch left the EE and D
         * laid the same 88x16 block, 1408 px, no field.  Two of the three things a tag can say about a
         * transfer have therefore been edited on purpose without moving the picture, which leaves exactly
         * one assumption untested since 12i: that the block in D's window is D's GROUP's ink.  It may
         * belong to another writer -- the poster that sets the environment, the AD stream, the desktop's
         * own upload -- and if so the ORDER/PLACE/SESSION grid of 12h and 12i and both of 12l/12m's
         * negatives were read off somebody else's pixels.
         *
         * The dial is the geometry: D is asked a 24-px triangle at the LEFT edge of its own 88-px window
         * and read across the whole window, so the ask and the ruler differ by coordinate and painted px
         * can be counted at an x that was never asked for.  Three readings, and the third one is the one
         * that costs a new rung either way:
         *   out=0 and ink present -- the ink moved with the ask, so the block IS this group's, and the
         *     flat full-width box is this group's coverage of its own bounding box;
         *   the 88-px top edge still laid -- the block held while the ask moved, so it is NOT this group's
         *     ink and the tag rungs have been grading a writer that was never the recorder;
         *   anything in between -- two writers in one box, and 12k's picture is the evidence.
         * B is 12m's control unchanged: it must still lay its whole 128-px top edge, or this census is not
         * 12k's census with one number changed and none of the readings above can be taken from it. */
        {
            static const char *const n_tag[5] = {
                "THE INK FOLLOWS THE ASK", "THE BLOCK HELD: NOT THIS GROUP'S INK",
                "CONTROL MOVED: READ NOTHING", "SPLIT: TWO WRITERS IN ONE BOX",
                "THE INK VANISHED WITH THE WIDE ASK"
            };
            static const char *const n_body[5][2] = {
                { "asked 24 px, laid only 24: box D's block is this recorder group's",
                  "rasterisation, and the flat box is its OWN bbox -- a setup answer" },
                { "asked 24 px, laid the same 88-px block: that ink is another writer's",
                  "and 12i-12m read their tag answers off pixels this group never laid" },
                { "B is not the RECT 12m measured, so the census itself moved and",
                  "nothing here can say whose pixels D's window holds" },
                { "some of the ink followed the ask and some of it stayed: two senders",
                  "share box D, and 12k's picture of it is what to read next" },
                { "the narrowed ask laid nothing where the wide ask laid 1408 px, so the",
                  "ask does rule -- and a wide ask is what turns it into a block" }
            };
            const uint32_t wd = s_sh_x1[3u] - s_sh_x0[3u];
            const uint32_t wb = s_sh_x1[1u] - s_sh_x0[1u];
            const uint32_t pd = sh_tot[3u] - sh_same[3u];
            const uint32_t aw = (s_sh_ask_x1[3u] > s_sh_ask_x0[3u])
                                  ? (uint32_t)(s_sh_ask_x1[3u] - s_sh_ask_x0[3u]) : 0u;
            const int dialled = (aw != 0u && aw < wd);
            /* Rung 12o takes this box's ask out of the band, and 12n's law -- painted px at an x never
             * asked for, measured across the window the group was asked to fill -- only means what 12n
             * measured while the ask is still IN the band.  out= then reads 1408 simply because every band
             * row is a row that was never asked, which is 12o's evidence, not a split. */
            const int in_band = (s_sh_ask_y0[3u] == (int32_t)GSLIVE_SH_Y0
                                    && s_sh_ask_y1[3u] == (int32_t)GSLIVE_SH_Y1);
            const int b_stable = (sh_top[1u] >= wb);
            const int confined = (sh_outside[3u] == 0u);
            const int block_kept = (sh_top[3u] * 2u >= wd);
            int case_n;
            uint32_t b6;

            if (!dialled || !in_band) {
                case_n = -1;
            } else if (!b_stable) {
                case_n = 2;
            } else if (pd == 0u) {
                case_n = 4;
            } else if (confined) {
                case_n = 0;
            } else if (block_kept) {
                case_n = 1;
            } else {
                case_n = 3;
            }

            ps2_kprintf("[GSLIVE] 12n dial: box D is ASKED x%u-%u, %u px wide (apex %u), and READ\n",
                        (unsigned int)s_sh_ask_x0[3u], (unsigned int)s_sh_ask_x1[3u] - 1u,
                        (unsigned int)aw,
                        (unsigned int)(((uint32_t)s_sh_ask_x0[3u]
                                        + (uint32_t)s_sh_ask_x1[3u]) / 2u));
            ps2_kprintf("[GSLIVE]   across x%u-%u, %u px, because its window is the ruler.  Both tag\n",
                        (unsigned int)s_sh_x0[3u], (unsigned int)s_sh_x1[3u] - 1u, (unsigned int)wd);
            ps2_kprintf("[GSLIVE]   asked rows y%u-%u, band rows y%u-%u%s\n",
                        (unsigned int)s_sh_ask_y0[3u], (unsigned int)s_sh_ask_y1[3u] - 1u,
                        (unsigned int)GSLIVE_SH_Y0, (unsigned int)GSLIVE_SH_Y1 - 1u,
                        in_band ? "" : "  <- 12o moved it");
            ps2_kprintf("[GSLIVE]   dials are OFF, so the ask is the one thing that moved since 12k,\n");
            ps2_kprintf("[GSLIVE]   which laid D top=88/%u painted=1408 RECT nofield at the wide ask.\n",
                        (unsigned int)wd);
            ps2_kprintf("[GSLIVE]   out= counts painted px in the window at an x never asked for; it\n");
            ps2_kprintf("[GSLIVE]   is 0 wherever ask == window, so only a dialled box can show it.\n");
            ps2_kprintf("[GSLIVE] 12n rows: ask span, painted px in the top row, painted px, out=:\n");
            for (b6 = 0u; b6 < GSLIVE_SH_N; b6++) {
                const uint32_t w6 = s_sh_x1[b6] - s_sh_x0[b6];
                const uint32_t p6 = sh_tot[b6] - sh_same[b6];
                const int kind = (sh_top[b6] <= 4u) ? 0 : ((sh_top[b6] * 2u >= w6) ? 2 : 1);
                const int fld = (p6 != 0u && sh_other[b6] > p6 / 4u);
                const uint32_t q0 = (s_sh_ask_x1[b6] != 0u)
                                      ? (uint32_t)s_sh_ask_x0[b6] : s_sh_x0[b6];
                const uint32_t q1 = (s_sh_ask_x1[b6] != 0u)
                                      ? (uint32_t)s_sh_ask_x1[b6] : s_sh_x1[b6];

                ps2_kprintf("[GSLIVE] 12n %c: ask %u-%u top=%u/%u px=%u out=%u %s %s\n",
                            s_sh_label[b6][0], (unsigned int)q0, (unsigned int)q1 - 1u,
                            (unsigned int)sh_top[b6], (unsigned int)w6, (unsigned int)p6,
                            (unsigned int)sh_outside[b6], gslive_sh_kind[kind],
                            fld ? "FIELD" : "nofield");
            }
            ps2_kprintf("[GSLIVE] 12n inside the ask: %u of D's %u painted px.  The asked bbox is\n",
                        (unsigned int)(pd - sh_outside[3u]), (unsigned int)pd);
            ps2_kprintf("[GSLIVE]   %u px (%u wide x %u rows): that many means the bbox was FILLED,\n",
                        (unsigned int)(aw * (uint32_t)GSLIVE_PIC_ROWS), (unsigned int)aw,
                        (unsigned int)GSLIVE_PIC_ROWS);
            ps2_kprintf("[GSLIVE]   half of it (%u px) means a triangle, 0 means the ink is elsewhere.\n",
                        (unsigned int)(aw * (uint32_t)GSLIVE_PIC_ROWS / 2u));
            if (case_n < 0) {
                ps2_kprintf("[GSLIVE] 12n verdict: SKIP -- this run does not hold 12n's condition (D is\n");
                ps2_kprintf("[GSLIVE]   asked across its whole window, or 12o moved its ask out of the\n");
                ps2_kprintf("[GSLIVE]   band), so out= is 12o's reading, not a split.  12n measured\n");
                ps2_kprintf("[GSLIVE]   THE BLOCK HELD on 2026-10-18, eight censuses, both runs.\n");
            } else {
                ps2_kprintf("[GSLIVE] 12n verdict: %s\n", n_tag[case_n]);
                ps2_kprintf("[GSLIVE]   %s\n", n_body[case_n][0]);
                ps2_kprintf("[GSLIVE]   %s\n", n_body[case_n][1]);
            }
        }

        /* Rung 12o's own reading -- WHERE this group's ink goes, and whether the ask is even the ask.
         *
         * 12n narrowed D's ask to 24 px at the left of its own 88-px window and the block stayed
         * byte-for-byte the same, so a group's geometry joined PRE (12l) and its PRIM field (12m) as a third
         * input that does not move this picture.  Three answers of "no" in a row are a suspicious coincidence
         * unless the thing being measured has one property none of the rungs tested: it was never this
         * group's to move.  So this rung stops dialling inside the band and moves D's ask OUT of it -- the
         * same 24x16 triangle, 16 rows lower, at rows the band branch never reads -- and asks two questions
         * that can now be answered separately: did the band's block go away when the group left the band, and
         * did ink arrive inside the rect the packet asked for?  The four combinations are four different
         * diagnoses, which is why both halves are needed rather than the one 12n read.
         *
         * The third row is the rung's insurance: the captured group's own XYZ2 words are decoded by the
         * inverse of the encoder this file uses, so "the ask" is certified as bytes that left the EE rather
         * than as a C argument.  If the decoded corners differ from the ask, the geometry was rewritten
         * between the session and the packet and no verdict below can be taken from this run -- that itself
         * would be the finding, and it is why the rows print both. */
        {
            static const char *const o_tag[5] = {
                "Y IS HONOURED: BAND EMPTIED, INK LANDED",
                "TWO WRITERS: INK LANDED, BAND BLOCK STAYED",
                "THIS GROUP DRAWS NOTHING: no ink landed",
                "BAND EMPTIED, NO INK: read 12j's spill bbox",
                "CONTROL MOVED: READ NOTHING"
            };
            static const char *const o_body[5][2] = {
                { "the block followed the ask in y and not in x, so an x-specific",
                  "override (clip, clamp, re-derived x) is what 12n was hiding" },
                { "this group's ink is in the asked rect and the band block is a",
                  "different writer's: the census graded a stranger's pixels" },
                { "nothing landed in the asked rect and the band is unchanged: no",
                  "vertex of this group rasterises, so its stream is the dead one" },
                { "the ask rules and the answer is emptiness -- the ink went",
                  "somewhere this rung did not name; 12j's bbox is next" },
                { "B is not the RECT 12k measured, so the census moved and",
                  "neither reading above can be attributed to this dial" }
            };
            const uint32_t od = s_sh_sess_first[3u];
            const int have_d = (od < GSLIVE_CAP_SENDS && s_cap_send_words[od] >= 8u);
            const int nreg_d = have_d ? (int)gslive_nreg_of(s_cap_send[od]) : 0;
            const int32_t vx0 = have_d ? gslive_xyz_axis(s_cap_send[od][3], 0) : -2048;
            const int32_t vy0 = have_d ? gslive_xyz_axis(s_cap_send[od][3], 1) : -2048;
            const int32_t vx1 = have_d ? gslive_xyz_axis(s_cap_send[od][5], 0) : -2048;
            const int32_t vy1 = have_d ? gslive_xyz_axis(s_cap_send[od][5], 1) : -2048;
            const int32_t vx2 = have_d ? gslive_xyz_axis(s_cap_send[od][7], 0) : -2048;
            const int32_t vy2 = have_d ? gslive_xyz_axis(s_cap_send[od][7], 1) : -2048;
            const int sent_ok = (have_d && nreg_d == 2
                                    && vx0 == s_sh_ask_x0[3u]
                                    && vy0 == s_sh_ask_y1[3u] - 1
                                    && vx1 == s_sh_ask_x1[3u] - 1
                                    && vy1 == s_sh_ask_y1[3u] - 1
                                    && vx2 == (s_sh_ask_x0[3u] + s_sh_ask_x1[3u]) / 2
                                    && vy2 == s_sh_ask_y0[3u]);
            const uint32_t wb_o = s_sh_x1[1u] - s_sh_x0[1u];
            const uint32_t pd_o = sh_tot[3u] - sh_same[3u];
            const int dialled = (mv_box < GSLIVE_SH_N);
            const int b_stable = (sh_top[1u] >= wb_o);
            const int block_gone = (pd_o == 0u);
            const int ink_found = (mv_painted != 0u);
            int case_o;

            if (!dialled || !sent_ok) case_o = -1;
            else if (!b_stable) case_o = 4;
            else if (block_gone && ink_found) case_o = 0;
            else if (ink_found) case_o = 1;
            else if (!block_gone) case_o = 2;
            else case_o = 3;

            ps2_kprintf("[GSLIVE] 12o dial: D's ask is x%u-%u y%u-%u -- below the band (y%u-%u),\n",
                        (unsigned int)s_sh_ask_x0[3u], (unsigned int)s_sh_ask_x1[3u] - 1u,
                        (unsigned int)s_sh_ask_y0[3u], (unsigned int)s_sh_ask_y1[3u] - 1u,
                        (unsigned int)GSLIVE_SH_Y0, (unsigned int)GSLIVE_SH_Y1 - 1u);
            ps2_kprintf("[GSLIVE]   inside the spill window (%u-%u), same 24x16 triangle as 12n, and\n",
                        (unsigned int)GSLIVE_SP_Y0, (unsigned int)GSLIVE_SP_Y1 - 1u);
            ps2_kprintf("[GSLIVE]   both tag dials off.  Two questions: did the band block go, and did\n");
            ps2_kprintf("[GSLIVE]   ink arrive in the asked rect.  12n's baseline: painted=1408 RECT.\n");
            ps2_kprintf("[GSLIVE] 12o packet: D's group NREG=%u words=%u; XYZ2 at 3/5/7 decoded as\n",
                        (unsigned int)nreg_d,
                        (unsigned int)(have_d ? s_cap_send_words[od] : 0u));
            ps2_kprintf("[GSLIVE]   (field >> 4) - 2048, the encoder's own law inverted:\n");
            ps2_kprintf("[GSLIVE] 12o got v0=(%d,%d) v1=(%d,%d) v2=(%d,%d)\n",
                        (int)vx0, (int)vy0, (int)vx1, (int)vy1, (int)vx2, (int)vy2);
            ps2_kprintf("[GSLIVE] 12o ask v0=(%d,%d) v1=(%d,%d) v2=(%d,%d) -> %s\n",
                        (int)s_sh_ask_x0[3u], (int)s_sh_ask_y1[3u] - 1,
                        (int)s_sh_ask_x1[3u] - 1, (int)s_sh_ask_y1[3u] - 1,
                        (int)((s_sh_ask_x0[3u] + s_sh_ask_x1[3u]) / 2), (int)s_sh_ask_y0[3u],
                        sent_ok ? "PACKET == ASK" : "PACKET DIFFERS: SKIP");
            ps2_kprintf("[GSLIVE] 12o band D: top=%u/%u painted=%u out=%u  <- 12n laid 1408 here\n",
                        (unsigned int)sh_top[3u], (unsigned int)(s_sh_x1[3u] - s_sh_x0[3u]),
                        (unsigned int)pd_o, (unsigned int)sh_outside[3u]);
            if (dialled) {
                const uint32_t mv_area = ((uint32_t)(s_sh_ask_x1[mv_box] - s_sh_ask_x0[mv_box]))
                                       * ((uint32_t)(s_sh_ask_y1[mv_box] - s_sh_ask_y0[mv_box]));

                ps2_kprintf("[GSLIVE] 12o moved %c: tot=%u/%u painted=%u R=%u G=%u B=%u field=%u\n",
                            s_sh_label[mv_box][0], (unsigned int)mv_tot, (unsigned int)mv_area,
                            (unsigned int)mv_painted, (unsigned int)mv_r, (unsigned int)mv_g,
                            (unsigned int)mv_b, (unsigned int)mv_other);
                ps2_kprintf("[GSLIVE]   same-as-canvas there: %u of %u px (ink %u + field %u)\n",
                            (unsigned int)mv_same, (unsigned int)mv_tot,
                            (unsigned int)(mv_r + mv_g + mv_b), (unsigned int)mv_other);
                if (mv_painted != 0u) {
                    ps2_kprintf("[GSLIVE] 12o moved ink bbox: x%u-%u y%u-%u\n",
                                (unsigned int)mv_bbox[0], (unsigned int)mv_bbox[1],
                                (unsigned int)mv_bbox[2], (unsigned int)mv_bbox[3]);
                } else {
                    ps2_kprintf("[GSLIVE] 12o moved ink bbox: none -- every px equals its canvas\n");
                }
            } else {
                ps2_kprintf("[GSLIVE] 12o moved: SKIP -- no box was asked outside the band this run\n");
            }
            ps2_kprintf("[GSLIVE] 12o control B: top=%u/%u painted=%u (12k: 128/128, 1920)\n",
                        (unsigned int)sh_top[1u], (unsigned int)wb_o,
                        (unsigned int)(sh_tot[1u] - sh_same[1u]));
            if (case_o < 0) {
                ps2_kprintf("[GSLIVE] 12o verdict: SKIP -- either nothing was asked below the band, or\n");
                ps2_kprintf("[GSLIVE]   D's packet is missing/too short/NREG!=2, or its decoded corners\n");
                ps2_kprintf("[GSLIVE]   are not the ask (the row above says which).\n");
            } else {
                ps2_kprintf("[GSLIVE] 12o verdict: %s\n", o_tag[case_o]);
                ps2_kprintf("[GSLIVE]   %s\n", o_body[case_o][0]);
                ps2_kprintf("[GSLIVE]   %s\n", o_body[case_o][1]);
            }
        }

        /* Rung 12p: the SAME BYTES, WITHHELD.  Every rung from 12i to 12o has edited something the transfer
         * says -- PRE (12l), its PRIM field (12m), its x ask (12n), its y ask (12o) -- and the census has
         * never asked the one question that separates "this group's picture" from "a picture in this
         * group's neighbourhood": does the group reach the GS at all?  12o's answer makes that question
         * urgent twice over.  It found 384 red px filling D's moved 24x16 ask -- which it could only call
         * "ink where the packet asked", not "ink THIS packet laid" -- and it found the control box B change
         * shape, eight times in a row and identically in both pinned runs, for a dial that named nothing
         * about B.
         *
         * B's own picture says what moved, and it is not the pair of shapes 12l/12n's counts suggested: the
         * blue rect is INVARIANT at 945 px (x320-382, rows 560-574 -- 12j's "exactly 63x15", a foreign
         * writer) and only B's red moved.  With D's ask in the band the red is the left half of rows
         * 560-574 (975 px); with D's ask 16 rows lower the red is row 575 alone, full width (128 px).  So a
         * neighbour's y rewrote which scanlines of this triangle rasterised, which is either a real
         * cross-group coupling or an artifact of the instrument -- opposite cures, and nothing in 12o can
         * tell them apart.
         *
         * So this rung changes the delivery and nothing else: D's session still opens, its group is still
         * captured and decoded, and the sink returns before `ps2_gs_gif_send()`, so the GIF receives two of
         * the census's three recorder groups and the page never sees D's bytes.  Two questions, both answerable
         * from one page: did anything survive at the two places 12o read (the band's block and the moved
         * rect), and did B's red come back?  They are printed as two verdicts, because they are two
         * measurements -- an ink answer says who laid a picture, and a B answer says whether one transfer's
         * DELIVERY can move another's raster, or whether the EE-side recorder state did it before the sink
         * ever saw a byte.  Both are graded only when the dial is armed and the sink held exactly one group:
         * a run that withheld nothing, or withheld three, is not this rung's run. */
        {
            static const char *const p_tag[4] = {
                "BOTH VANISH: ONE AUTHOR WAS D",
                "BLOCK WENT, INK STAYED: D DRAWS ITS ASK",
                "INK WENT, BLOCK STAYED: 12n CONFIRMED",
                "NEITHER IS D'S: STRANGERS EVERYWHERE"
            };
            static const char *const p_body[4][2] = {
                { "with D's bytes gone, the band's 1408 px and the moved 384 are both",
                  "empty: both pictures were this transfer's, so 12n graded its own ink" },
                { "the band block empties with the bytes and the moved rect still",
                  "fills: the ink follows the ask, and 12n's HELD was a coincidence" },
                { "the moved rect empties -- those 384 px WERE this transfer's --",
                  "while the band block stands, so that block is a foreign writer's" },
                { "both pictures stand without this group's bytes: band block AND",
                  "moved ink are other writers', and no per-box row is a recorder row" }
            };
            static const char *const p_btag[3] = {
                "B'S RED IS BACK (975): D'S BYTES MOVED IT",
                "B STAYS RED-LESS: NOT THE GIF",
                "B IS A THIRD STATE: READ THE PICTURE"
            };
            static const char *const p_bbody[3][2] = {
                { "the only difference is whether D's words reached the GS, and B's left",
                  "half returns: one transfer's delivery rules another's scanlines" },
                { "B still has 128 red on row 575 without D on the bus, so the coupling",
                  "is EE-side (the recorder's own state) or a foreign writer, not the GIF" },
                { "B matches neither 975 nor 128 red, so this run put the page somewhere",
                  "no rung has described; 12k's picture of B is the row to read next" }
            };
            const uint32_t od_p = s_sh_sess_first[3u];
            const int captured = (od_p < GSLIVE_CAP_SENDS && s_cap_send_words[od_p] >= 8u);
            /* Rung 12v: a session is now two groups, so withholding D's packet withholds two transfers.
             * A run that held one of the two has D's chain without its PRIM write, which is 12u's V0, not
             * 12p's dial -- so the law counts 2 and says so rather than grading a different experiment. */
            const int armed_p = (s_hold_gif_mask == (1u << 3) && s_sh_held == 2u);
            const uint32_t bp_p = s_sh_x1[1u] - s_sh_x0[1u];
            const uint32_t pd_p = sh_tot[3u] - sh_same[3u];
            const uint32_t pb_p = sh_tot[1u] - sh_same[1u];
            const int band_kept = (pd_p != 0u);
            const int ink_kept = (mv_painted != 0u);
            /* B is classified by its OWN red count and not by `painted=`, because the blue 945 is a foreign
             * rect in both baselines: red=975 is 12n/12k's B and red=128 is 12o's, and either of them can
             * coexist with any blue.  A third number is a third state and says so rather than being read
             * as one of the two. */
            const int b_state = (sh_r[1u] == 975u) ? 0
                              : ((sh_r[1u] == 128u && pb_p == 1073u) ? 1 : 2);
            int case_p;

            ps2_kprintf("[GSLIVE] 12p dial: D's group is captured and decoded, and NOT handed to the\n");
            ps2_kprintf("[GSLIVE]   GIF.  ask x%u-%u y%u-%u, held=%u, to the bus=%u group(s).\n",
                        (unsigned int)s_sh_ask_x0[3u], (unsigned int)s_sh_ask_x1[3u] - 1u,
                        (unsigned int)s_sh_ask_y0[3u], (unsigned int)s_sh_ask_y1[3u] - 1u,
                        (unsigned int)s_sh_held, (unsigned int)s_sh_groups);
            ps2_kprintf("[GSLIVE]   12o, with those bytes SENT: band 1408, moved ink 384 red,\n");
            ps2_kprintf("[GSLIVE]     B red=975 (left half, rows 560-574) -> red=128 (row 575).\n");
            ps2_kprintf("[GSLIVE] 12p band D: top=%u/%u painted=%u   moved %c: tot=%u painted=%u\n",
                        (unsigned int)sh_top[3u], (unsigned int)(s_sh_x1[3u] - s_sh_x0[3u]),
                        (unsigned int)pd_p, s_sh_label[mv_box < GSLIVE_SH_N ? mv_box : 3u][0],
                        (unsigned int)mv_tot, (unsigned int)mv_painted);
            ps2_kprintf("[GSLIVE]   moved ink R=%u G=%u B=%u field=%u bbox x%u-%u y%u-%u\n",
                        (unsigned int)mv_r, (unsigned int)mv_g, (unsigned int)mv_b,
                        (unsigned int)mv_other,
                        (mv_painted != 0u) ? (unsigned int)mv_bbox[0] : 0u,
                        (mv_painted != 0u) ? (unsigned int)mv_bbox[1] : 0u,
                        (mv_painted != 0u) ? (unsigned int)mv_bbox[2] : 0u,
                        (mv_painted != 0u) ? (unsigned int)mv_bbox[3] : 0u);
            ps2_kprintf("[GSLIVE] 12p control B: top=%u/%u painted=%u red=%u blue=%u  (12n 975,\n",
                        (unsigned int)sh_top[1u], (unsigned int)bp_p, (unsigned int)pb_p,
                        (unsigned int)sh_r[1u], (unsigned int)sh_b[1u]);
            ps2_kprintf("[GSLIVE]   12o 128; the blue 945 is a foreign rect in both)\n");
            if (!armed_p || !captured) {
                ps2_kprintf("[GSLIVE] 12p verdict: SKIP -- the dial is %s, the sink held %u group(s)\n",
                            ((s_hold_gif_mask & (1u << 3)) != 0u) ? "D-armed" : "NOT D-armed",
                            (unsigned int)s_sh_held);
                ps2_kprintf("[GSLIVE]   of D's %u, and its packet %s.  This is not 12p's run.\n",
                            (unsigned int)s_sh_sess_groups[3u],
                            captured ? "is captured" : "is missing or short");
            } else {
                case_p = (band_kept ? 2 : 0) + (ink_kept ? 1 : 0);
                ps2_kprintf("[GSLIVE] 12p verdict INK: %s\n", p_tag[case_p]);
                ps2_kprintf("[GSLIVE]   %s\n", p_body[case_p][0]);
                ps2_kprintf("[GSLIVE]   %s\n", p_body[case_p][1]);
                ps2_kprintf("[GSLIVE] 12p verdict B: %s\n", p_btag[b_state]);
                ps2_kprintf("[GSLIVE]   %s\n", p_bbody[b_state][0]);
                ps2_kprintf("[GSLIVE]   %s\n", p_bbody[b_state][1]);
            }
        }

        /* Rung 12q: the mask, because 12p answered "nobody" and left a worse question behind.  Withholding
         * ONE transfer's bytes emptied a box whose own transfer went out -- B, from `painted=1073` (its red
         * left half plus the foreign blue rect) to `painted=0`, blue included -- while the two windows that
         * were supposedly B's neighbour, D's band block and D's moved rect, kept their 12o counts exactly
         * (1408 and 384).  A per-box reading of this census cannot survive that pair of facts: it means the
         * number in one box's slot is answered by a different box's delivery.  So this rung holds the mask
         * to ALL THREE recorder sessions (bits 0, 1 and 3: A, B and D) and asks the only question that can
         * re-base the instrument: is there ANY census ink left when no census triangle reaches the GS?
         * The hand shapes C/E/F do not pass through this sink, so they are the within-frame witness -- C
         * moved between 12n and 12o with nothing but D's ask changed, which is why it is read here rather
         * than assumed constant.  The law is per box against this page's OWN 12o baselines (A 891, B 1073
         * with red 128, D's band 1408, D's moved rect 384): four STAND says every rung-12 count has been
         * the app's stream and the census must be re-based on a window the app never paints; four FELL says
         * the band really is this file's ink and the cross-box coupling of 12p is a GS-side mechanism worth
         * chasing. Anything in between names itself. */
        {
            static const char *const q_tag[5] = {
                "ALL FELL: THE BAND IS THIS FILE'S INK",
                "1 OF 4 STOOD: ONE WINDOW IS NOT OURS",
                "SPLIT: D STANDS, A AND B FALL",
                "3 OF 4 STOOD: ALMOST NONE OF IT IS OURS",
                "ALL STOOD: NO CENSUS TRIANGLE RASTERISED"
            };
            static const char *const q_body[5][2] = {
                { "every one of the four empties with its transfer, so 12a-on's",
                  "counts really were this census's own rasterisation" },
                { "one window keeps its 12o number with no bytes and three do",
                  "not, so that one is the foreign writer -- the rows name it" },
                { "the two D windows stand while A and B empty: the instrument",
                  "works, and D's 1408-px block is another writer's ink" },
                { "three of the four stand with nothing sent: at most one box",
                  "here is ours, and every law built on the others graded ink" },
                { "all three sessions withheld and all four windows stand, so",
                  "rung 12 never measured its own ink -- re-base the read" }
            };
            const uint32_t qa = sh_tot[0u] - sh_same[0u];
            const uint32_t qb = sh_tot[1u] - sh_same[1u];
            const uint32_t qc = sh_tot[2u] - sh_same[2u];
            const uint32_t qe = sh_tot[4u] - sh_same[4u];
            const uint32_t qf = sh_tot[5u] - sh_same[5u];
            const uint32_t qd = sh_tot[3u] - sh_same[3u];
            const int armed_q = (s_hold_gif_mask == ((1u << 0) | (1u << 1) | (1u << 3)) &&
                                 s_sh_held == 6u);   /* 12v: three sessions, two groups each */
            const int a_stands = (qa == 891u);
            const int b_stands = (qb == 1073u);
            const int d_stands = (qd == 1408u);
            const int m_stands = (mv_painted == 384u);
            const int stand_count = a_stands + b_stands + d_stands + m_stands;

            ps2_kprintf("[GSLIVE] 12q dial: mask=0x%x, held=%u, to the bus=%u group(s); the hand\n",
                        (unsigned int)s_hold_gif_mask, (unsigned int)s_sh_held,
                        (unsigned int)s_sh_groups);
            ps2_kprintf("[GSLIVE]   shapes C/E/F and the magenta control still go out.  12o, all SENT:\n");
            ps2_kprintf("[GSLIVE]   A 891, B 1073 (red 128), D band 1408, D moved 384, C 1221.\n");
            if (!armed_q) {
                ps2_kprintf("[GSLIVE] 12q verdict: SKIP -- mask=0x%x held %u of %u session group(s),\n",
                            (unsigned int)s_hold_gif_mask, (unsigned int)s_sh_held,
                            (unsigned int)(s_sh_groups + s_sh_held));
                ps2_kprintf("[GSLIVE]   %u send(s) captured.  This is not 12q's run.\n",
                            (unsigned int)s_cap_sends);
            } else {
                ps2_kprintf("[GSLIVE] 12q A band: painted=%u, 12o 891 -> %s\n",
                            (unsigned int)qa, a_stands ? "STOOD" : "FELL");
                ps2_kprintf("[GSLIVE] 12q B band: painted=%u, 12o 1073 -> %s (red %u)\n",
                            (unsigned int)qb, b_stands ? "STOOD" : "FELL",
                            (unsigned int)sh_r[1u]);
                ps2_kprintf("[GSLIVE] 12q D band: painted=%u, 12o 1408 -> %s\n",
                            (unsigned int)qd, d_stands ? "STOOD" : "FELL");
                ps2_kprintf("[GSLIVE] 12q D moved: painted=%u, 12o 384 -> %s (red %u)\n",
                            (unsigned int)mv_painted, m_stands ? "STOOD" : "FELL",
                            (unsigned int)mv_r);
                ps2_kprintf("[GSLIVE] 12q witness C: painted=%u, 12o 1221 -> %s\n",
                            (unsigned int)qc, (qc == 1221u) ? "SAME" : "MOVED");
                ps2_kprintf("[GSLIVE]   E: %u vs 1145 %s   F: %u vs 442 %s\n",
                            (unsigned int)qe, (qe == 1145u) ? "same" : "moved",
                            (unsigned int)qf, (qf == 442u) ? "same" : "moved");
                ps2_kprintf("[GSLIVE] 12q verdict: %d of 4 stand -- %s\n",
                            stand_count, q_tag[stand_count]);
                ps2_kprintf("[GSLIVE]   %s\n", q_body[stand_count][0]);
                ps2_kprintf("[GSLIVE]   %s\n", q_body[stand_count][1]);
            }
        }

        /* Rung 12s: the two pre-draw reads, printed as the CONTROL on the finding rather than as a verdict
         * about D.  12r's first look was real -- the magenta control came back empty -- and its conclusion was
         * still wrong: the block it found in D is the desktop's own Colour Test Bar (the note at GSLIVE_X0 has
         * the arithmetic), and the corner-word key counted that chrome as ink because it tests colours BEFORE
         * the canvas compare.  So this rung reads the strip the instrument now lives in AND the strip it used
         * to live in, and asks each of them the same question: where the page holds a corner word, is it the
         * canvas's own word?  Where the answer is yes the ink is chrome and no rung may claim it; where it is
         * no, a GS-side writer really does exist and 12s has not named it either.
         *
         * The pass number WAS the thing that made that reading decidable -- the census re-runs on every app
         * open, VRAM is not cleared between opens, and from pass 2 onward a dirty window is equally this
         * file's own triangle left over from pass 1, so only census #1 could carry a verdict.  Rung 12t took
         * the limitation away by putting the band back to its canvas before the look (see the upload above),
         * which makes every census's first look a real one and lets the verdict be graded on the censuses that
         * run the grid instead of on whichever census happened to be first.  The old strip at 560..575 is NOT
         * uploaded, so its page-vs-canvas agreement stays a measurement of the desktop rather than a restatement
         * of this file's own copy. */
        {
            static const char *const s_tag[4] = {
                "BAND CLEAN, OLD BLOCK IS THE DESKTOP'S OWN COLOUR BAR",
                "BAND CLEAN, A GS-SIDE WRITER STAYS UNNAMED",
                "BAND CLEAN, OLD STRIP EMPTY TOO",
                "THE NEW BAND IS NOT CLEAN EITHER"
            };
            static const char *const s_body[4][2] = {
                { "the six band windows hold nothing before the draws, and the old strip's",
                  "words ARE the canvas's -- 12a..12r graded the colour bar, not the GS" },
                { "the band is clean, but the old strip holds corner words the canvas does",
                  "not hold at those positions: a GS-side writer is still unnamed" },
                { "the band is clean and the old strip is empty, so every block the rungs",
                  "saw came from THIS census's draws and chrome was never the answer" },
                { "12t's canvas upload did NOT take: a band window still disagrees with its own",
                  "canvas, or came back full of sentinels, before this census sent anything" }
            };
            uint32_t post[GSLIVE_SH_N], pre_paint[GSLIVE_SH_N];
            uint32_t band_dirty = 0u, sent_band = 0u, sent_old = 0u;
            uint32_t band_canvas = 0u, old_page = 0u, old_canvas = 0u;
            uint32_t old_match = 0u, old_tot = 0u;
            int k_s, b_s, case_s;

            for (k_s = 0; k_s < (int)GSLIVE_SH_N; k_s++) {
                post[k_s] = sh_tot[k_s] - sh_same[k_s];
                pre_paint[k_s] = s_pre[k_s].tot - s_pre[k_s].same;
                if (pre_paint[k_s] != 0u || s_pre[k_s].sent != 0u) band_dirty++;
                sent_band += s_pre[k_s].sent;
                sent_old += s_pre_old[k_s].sent;
                band_canvas += s_pre[k_s].cv_r + s_pre[k_s].cv_g + s_pre[k_s].cv_b;
                old_page += s_pre_old[k_s].r + s_pre_old[k_s].g + s_pre_old[k_s].b;
                old_canvas += s_pre_old[k_s].cv_r + s_pre_old[k_s].cv_g + s_pre_old[k_s].cv_b;
                old_match += s_pre_old[k_s].match;
                old_tot += s_pre_old[k_s].tot;
            }
            ps2_kprintf("[GSLIVE] 12s pre-draw: the band (rows %u..%u) and the old strip\n",
                        (unsigned int)GSLIVE_SH_Y0, (unsigned int)GSLIVE_SH_Y1 - 1u);
            ps2_kprintf("[GSLIVE]   (rows %u..%u) read with the GIF drained BEFORE this census\n",
                        (unsigned int)GSLIVE_OLD_BAND_Y0,
                        (unsigned int)(GSLIVE_OLD_BAND_Y0 + GSLIVE_PIC_ROWS - 1u));
            ps2_kprintf("[GSLIVE]   sent anything: census #%u of this process, band=%s old=%s\n",
                        (unsigned int)s_pre_pass, s_pre_ok ? "OK" : "FAILED",
                        s_pre_old_ok ? "OK" : "FAILED");
            ps2_kprintf("[GSLIVE]   words never filled: band=%u old=%u\n",
                        (unsigned int)sent_band, (unsigned int)sent_old);
            ps2_kprintf("[GSLIVE] 12s self-check: control x%u-%u y%u-%u held magenta=%u\n",
                        (unsigned int)GSLIVE_X0, (unsigned int)GSLIVE_X1 - 1u,
                        (unsigned int)GSLIVE_Y0, (unsigned int)GSLIVE_Y1 - 1u,
                        (unsigned int)s_pre_ctrl_mag);
            ps2_kprintf("[GSLIVE]   painted=%u BEFORE its only sender ran: magenta>0 means this read\n",
                        (unsigned int)s_pre_ctrl_paint);
            ps2_kprintf("[GSLIVE]   is not a first look, and it voids every number below.\n");
            if (!s_pre_ok || !s_pre_old_ok) {
                ps2_kprintf("[GSLIVE] 12s verdict: SKIP -- %s did not come back GS_READ_BUS_OK,\n",
                            (!s_pre_ok) ? "the band read" : "the old-strip read");
                ps2_kprintf("[GSLIVE]   so there is no first look to grade and no chrome to name.\n");
            } else if (s_pre_ctrl_mag != 0u) {
                ps2_kprintf("[GSLIVE] 12s verdict: VOID -- %u of the control's own %u px were already\n",
                            (unsigned int)s_pre_ctrl_mag,
                            (unsigned int)((GSLIVE_X1 - GSLIVE_X0) * GSLIVE_PIC_ROWS));
                ps2_kprintf("[GSLIVE]   magenta before the control was sent, so this read saw the page\n");
                ps2_kprintf("[GSLIVE]   AFTER the draws.  Nothing on this page is attributable.\n");
            } else {
                ps2_kprintf("[GSLIVE]   band pre/post painted: A %u/%u  B %u/%u  C %u/%u\n",
                            (unsigned int)pre_paint[0u], (unsigned int)post[0u],
                            (unsigned int)pre_paint[1u], (unsigned int)post[1u],
                            (unsigned int)pre_paint[2u], (unsigned int)post[2u]);
                ps2_kprintf("[GSLIVE]                    D %u/%u  E %u/%u  F %u/%u\n",
                            (unsigned int)pre_paint[3u], (unsigned int)post[3u],
                            (unsigned int)pre_paint[4u], (unsigned int)post[4u],
                            (unsigned int)pre_paint[5u], (unsigned int)post[5u]);
                ps2_kprintf("[GSLIVE]   the band's CANVAS holds %u corner words across the six\n",
                            (unsigned int)band_canvas);
                ps2_kprintf("[GSLIVE]   windows (0 = the post-draw `same` column cannot be chrome).\n");
                for (b_s = 0; b_s < (int)GSLIVE_SH_N; b_s++) {
                    ps2_kprintf("[GSLIVE] 12s old %c: page r/g/b=%u/%u/%u canvas r/g/b=%u/%u/%u\n",
                                s_sh_label[b_s][0],
                                (unsigned int)s_pre_old[b_s].r, (unsigned int)s_pre_old[b_s].g,
                                (unsigned int)s_pre_old[b_s].b,
                                (unsigned int)s_pre_old[b_s].cv_r, (unsigned int)s_pre_old[b_s].cv_g,
                                (unsigned int)s_pre_old[b_s].cv_b);
                    ps2_kprintf("[GSLIVE]        %c: match=%u/%u field=%u top=%u/%u\n",
                                s_sh_label[b_s][0], (unsigned int)s_pre_old[b_s].match,
                                (unsigned int)s_pre_old[b_s].tot,
                                (unsigned int)s_pre_old[b_s].other,
                                (unsigned int)s_pre_old[b_s].top,
                                (unsigned int)(s_sh_x1[b_s] - s_sh_x0[b_s]));
                }
                if (s_census_only < GSLIVE_SH_N) {
                    ps2_kprintf("[GSLIVE] 12s verdict: SKIP for the grid -- census #%u runs 12t's LONE mode,\n",
                                (unsigned int)s_pre_pass);
                    ps2_kprintf("[GSLIVE]   box D's session is the only send in it, so the six pre/post pairs\n");
                    ps2_kprintf("[GSLIVE]   describe one box and five windows nobody drew in.  The cleanliness\n");
                    ps2_kprintf("[GSLIVE]   still counts: band_dirty=%u of 6 is 12t's upload checking itself.\n",
                                (unsigned int)band_dirty);
                } else {
                    case_s = (band_dirty != 0u) ? 3
                           : (old_page == 0u) ? 2
                           : ((old_match == old_tot && old_page == old_canvas) ? 0 : 1);
                    ps2_kprintf("[GSLIVE] 12s verdict: %s\n", s_tag[case_s]);
                    ps2_kprintf("[GSLIVE]   %s\n", s_body[case_s][0]);
                    ps2_kprintf("[GSLIVE]   %s\n", s_body[case_s][1]);
                    if (case_s != 3) {
                        ps2_kprintf("[GSLIVE]   the shading grid therefore grades this census's own ink:\n");
                        ps2_kprintf("[GSLIVE]   read 12m's INTERPOLATES/FLAT and 12l's shape word per box.\n");
                    }
                }
            }
        }

        /* Rung 12t: one send on the bus, one look at the page.
         *
         * 12s moved the instrument onto a strip this census can certify empty, and what it certified is the
         * sharpest pair in the lane.  Box A's recorder group -- sent LAST, behind the hand chain -- lays 891 px
         * whose every cell reads `f`, an apex-up triangle with a real gradient in it (12k's picture, 12m's
         * INTERPOLATES).  Box D's group -- sent FIRST, and captured by 12i as differing from A's in exactly three
         * position words and no colour word -- lays 660 px of pure GSQUAD_RED: `other=0`, no ramp anywhere, and
         * an apex row holding 44 of 88 columns.  A triangle asked with its apex at the TOP does not have a 44-px
         * top row, so that answer is wrong twice over -- flat AND mis-shaped.  Worse, it is not even D's alone:
         * 12j's spill says the same red runs from x384 to x579, which is box B's right edge through box C's whole
         * window, both 16-px gutters and D's left half -- 480 + 1800 + 660 = 2940 px of one rectangle that no
         * session of this census ever asked for.
         *
         * Two claims fit that, and their cures are opposite.  Either the packet ALONE rasterises flat and
         * mis-shaped -- then the recorder's encoding, or the GS state its first group lands in, is the bug -- or a
         * NEIGHBOUR drew the rectangle and D owns only the part its window covers -- then the packet is fine, the
         * frame that carries it is not, and 12i's "a gradient needs a transfer in front of it" is a statement about
         * register latching rather than about shading.  Every rung since 12i has graded a census that sends seven
         * things, so every count has been a sum of seven and neither claim could be falsified there.  This rung
         * sends ONE: census #1 of the process keeps box D's recorder session and withholds the magenta control,
         * the B and A sessions and the hand chain C/E/F, on a band the upload above laid back to its canvas.
         * `groups to the bus` and `hand sent` are the check that the withholding actually happened -- a dial that
         * does not dial is the failure mode this lane has been bitten by, so the dial prints its own effect.
         * Censuses 2..4 then run 12i's whole grid on the same reset band, and the two extents are compared: what
         * the neighbours add is the block's author, named in pixels instead of argued. */
        {
            static const char *const t_tag[5] = {
                "NOTHING: THE LONE GROUP LAID NO BAND INK AT ALL",
                "FOREIGN COORDINATES: THE LONE GROUP DREW OUTSIDE ITS ASK",
                "THE PACKET ALONE: FLAT AND MIS-SHAPED WITH NO NEIGHBOUR",
                "THE PACKET ALONE: A TRIANGLE WITH A FIELD, SHADING AND ALL",
                "NO TRIANGLE AT ALL: THE LONE PACKET LAYS A FEW PX, NOT ITS ASK"
            };
            static const char *const t_body[5][2] = {
                { "with the control, B, A and the hand chain all withheld, D's group paints nothing: the",
                  "660 px and the 2940-px block are a neighbour's, and 12i graded the wrong variable" },
                { "one packet, no neighbours, and ink at coordinates no session of this census named -- the",
                  "bbox's own edges are the law to chase now: a clip, an offset or a context, not a shade" },
                { "the block reproduces with nothing in front of it: corner words only, no ramp, a wide apex",
                  "row.  ORDER cannot be what explains it; the encoding or the GS state it lands in does" },
                { "the same eight words alone lay an apex-up triangle full of interpolated px: flatness and",
                  "the block both need a transfer IN FRONT, which is a frame problem and not an encoder one" },
                { "a few px and nothing else: three vertex kicks, no triangle -- no OP was ever programmed,",
                  "because PRE=1 forbids this tag from writing PRIM.  12u names who does; first seen 2026-10-18" }
            };
            const int t_lone = (s_census_only < GSLIVE_SH_N);
            const uint32_t t_dwin = sh_tot[3u] - sh_same[3u];   /* D's own window, the same unit 12m prints */
            int t_case = -1;
            int t_inside;

            t_inside = (bp_n != 0u && bp_bbox[0] >= (uint32_t)GSLIVE_SH_D_X0
                        && bp_bbox[1] < (uint32_t)GSLIVE_SH_D_X1
                        && bp_bbox[2] >= (uint32_t)GSLIVE_SH_Y0
                        && bp_bbox[3] < (uint32_t)GSLIVE_SH_Y1);

            ps2_kprintf("[GSLIVE] 12t mode: census #%u runs %s.\n",
                        (unsigned int)s_pre_pass, t_lone ? "LONE" : "GRID");
            if (t_lone) {
                ps2_kprintf("[GSLIVE]   box D's session is the ONLY thing sent: 12v makes that two groups,\n");
                ps2_kprintf("[GSLIVE]   its AD write of PRIM and then its vertex chain.  The magenta\n");
                ps2_kprintf("[GSLIVE]   control, the B and A sessions and the hand chain C/E/F are withheld.\n");
            } else {
                ps2_kprintf("[GSLIVE]   12i's whole grid: control, D, B, hand C/E/F, then A -- every\n");
                ps2_kprintf("[GSLIVE]   transfer every rung since 12i has been counting as one sum, with\n");
                ps2_kprintf("[GSLIVE]   each recorder session now handing two of them.\n");
            }
            ps2_kprintf("[GSLIVE] 12t reset: rows %u..%u uploaded from the canvas before the read, so the\n",
                        (unsigned int)GSLIVE_SP_Y0, (unsigned int)GSLIVE_SP_Y1 - 1u);
            ps2_kprintf("[GSLIVE]   12s pre-draw pairs above are that upload checking itself.\n");
            ps2_kprintf("[GSLIVE] 12t dial proof: groups to the bus=%u hand sent=%u refused=%u\n",
                        (unsigned int)s_sh_groups, (unsigned int)s_shape_sent,
                        (unsigned int)s_shape_refused);
            if (bp_n == 0u) {
                ps2_kprintf("[GSLIVE] 12t band ink: painted=0 -- the strip came back its own canvas.\n");
            } else {
                ps2_kprintf("[GSLIVE] 12t band ink: painted=%u corner=%u field=%u\n",
                            (unsigned int)bp_n, (unsigned int)bp_corner, (unsigned int)bp_field);
                ps2_kprintf("[GSLIVE]   bbox x%u-%u y%u-%u (%u wide)   ask D = x%u-%u y%u-%u\n",
                            (unsigned int)bp_bbox[0], (unsigned int)bp_bbox[1],
                            (unsigned int)bp_bbox[2], (unsigned int)bp_bbox[3],
                            (unsigned int)(bp_bbox[1] - bp_bbox[0] + 1u),
                            (unsigned int)GSLIVE_SH_D_X0, (unsigned int)GSLIVE_SH_D_X1 - 1u,
                            (unsigned int)GSLIVE_SH_Y0, (unsigned int)GSLIVE_SH_Y1 - 1u);
                ps2_kprintf("[GSLIVE]   all of it inside D's ask: %s | D's window: painted=%u top=%u/%u\n",
                            t_inside ? "yes" : "NO",
                            (unsigned int)(sh_tot[3u] - sh_same[3u]),
                            (unsigned int)sh_top[3u],
                            (unsigned int)(GSLIVE_SH_D_X1 - GSLIVE_SH_D_X0));
            }
            if (t_lone) {
                /* 12t's first run (build/repro/20261018T074741Z) graded painted=1 as case 2, "FLAT AND
                 * MIS-SHAPED WITH NO NEIGHBOUR", because the law only asked whether the band held ANY ink.
                 * One px is not a mis-shaped triangle, it is no triangle: the ask is 88x16 = 1408 px and the
                 * answer was 1.  So the point-like extent is now its own case, graded on D's window because
                 * that is the window the ask lives in. */
                t_case = (bp_n == 0u) ? 0
                       : (!t_inside) ? 1
                       : (t_dwin <= 8u) ? 4
                       : ((bp_field != 0u && sh_top[3u] <= 4u) ? 3 : 2);
                ps2_kprintf("[GSLIVE] 12t verdict: %s\n", t_tag[t_case]);
                ps2_kprintf("[GSLIVE]   %s\n", t_body[t_case][0]);
                ps2_kprintf("[GSLIVE]   %s\n", t_body[t_case][1]);
                ps2_kprintf("[GSLIVE]   12k's D picture and 12m's D rows above are this census's own.\n");
                s_t_lone_n = bp_n;
                s_t_lone_field = bp_field;
                s_t_lone_bbox[0] = bp_bbox[0];
                s_t_lone_bbox[1] = bp_bbox[1];
                s_t_lone_bbox[2] = bp_bbox[2];
                s_t_lone_bbox[3] = bp_bbox[3];
                s_t_lone_done = 1u;
            } else if (s_t_lone_done == 0u) {
                ps2_kprintf("[GSLIVE] 12t verdict: SKIP -- no LONE census has been measured in this process,\n");
                ps2_kprintf("[GSLIVE]   so the grid has no single-send extent to be compared against.\n");
            } else {
                const uint32_t lone_w = (s_t_lone_n == 0u) ? 0u
                                        : (s_t_lone_bbox[1] - s_t_lone_bbox[0] + 1u);
                const uint32_t grid_w = (bp_n == 0u) ? 0u : (bp_bbox[1] - bp_bbox[0] + 1u);

                ps2_kprintf("[GSLIVE] 12t grid-vs-lone: lone painted=%u field=%u wide=%u | grid painted=%u\n",
                            (unsigned int)s_t_lone_n, (unsigned int)s_t_lone_field,
                            (unsigned int)lone_w, (unsigned int)bp_n);
                ps2_kprintf("[GSLIVE]   grid field=%u wide=%u bbox x%u-%u: the neighbours add %s\n",
                            (unsigned int)bp_field, (unsigned int)grid_w,
                            (unsigned int)bp_bbox[0], (unsigned int)bp_bbox[1],
                            (grid_w > lone_w) ? "WIDTH no lone packet asks for"
                                              : "nothing beyond the lone extent");
                ps2_kprintf("[GSLIVE]   if this census's D picture matches the LONE one, the block is the\n");
                ps2_kprintf("[GSLIVE]   packet's own answer; if it is wider, 12i measured a neighbour.\n");
            }
        }

        /* Rung 12u: the four PRIM sources, replayed from the LONE census's own captured group.  Every row here
         * had its band uploaded back to the canvas first and its page read before anything printed, so no
         * neighbour stands behind any of these pixels -- which is the sentence 12t's `painted=1` forced this
         * file to go and measure rather than infer from 12c. */
        {
            static const char *const u_name[4] = {
                "V0 POINT write, then chain",
                "V1 PRE=0, tag field kept  ",
                "V2 AD(PRIM) TRI+IIP then V0",
                "V3 AD(PRIM) TRI flat then V0"
            };
            static const char *const u_ask[4] = {
                "PRIM=POINT in front: no OP that can make a triangle, and the chain as captured (PRE=1)",
                "the tag's own 11-bit field is allowed to write PRIM at packet start",
                "an AD write programs PRIM with IIP SET, then the group as captured",
                "an AD write programs PRIM with IIP CLEAR, then the group as captured"
            };
            uint32_t v;

            if (s_census_only != 3u) {
                ps2_kprintf("[GSLIVE] 12u: SKIP -- this census is 12i's grid; 12u replays a LONE group and\n");
                ps2_kprintf("[GSLIVE]   measures it against nothing, which is the whole point of the dial.\n");
            } else if (s_u_ok == 0u) {
                ps2_kprintf("[GSLIVE] 12u verdict: SKIP -- the probe did not run: box D's group was not\n");
                ps2_kprintf("[GSLIVE]   captured in a form this file can re-send, or a band read failed.\n");
            } else {
                int v0_tri = 0, v1_tri = 0, v2_tri = 0, v3_tri = 0;

                ps2_kprintf("[GSLIVE] 12u: the same captured chain, four times, each on a band the upload put\n");
                ps2_kprintf("[GSLIVE]   back to its canvas, with nothing but its own session's transfers in\n");
                ps2_kprintf("[GSLIVE]   front of it -- and, since 12v, a write of the OP it is being asked\n");
                ps2_kprintf("[GSLIVE]   about in front of the replay, so no inherited PRIM is ever the cause.\n");
                for (v = 0u; v < 4u; v++) {
                    const uint32_t p = s_u_n[v];
                    const uint32_t w = (p == 0u) ? 0u : (s_u_bbox[v][1] - s_u_bbox[v][0] + 1u);
                    const char *kind;

                    kind = (p == 0u) ? "NO INK"
                           : (p <= 8u) ? "POINT-LIKE: THREE KICKS, NO TRIANGLE"
                           : (s_u_top[v] <= 4u) ? "TRIANGLE"
                           : (s_u_top[v] * 2u >= w) ? "RECT" : "SCATTERED";
                    ps2_kprintf("[GSLIVE] 12u %s qw=%u: painted=%-5u corner=%-5u field=%-5u top=%-3u/%u "
                                "bbox x%u-%u y%u-%u -> %s\n",
                                u_name[v], (unsigned int)s_u_send[v], (unsigned int)p,
                                (unsigned int)s_u_corner[v], (unsigned int)s_u_field[v],
                                (unsigned int)s_u_top[v],
                                (unsigned int)(GSLIVE_SH_D_X1 - GSLIVE_SH_D_X0),
                                (unsigned int)(p ? s_u_bbox[v][0] : 0u),
                                (unsigned int)(p ? s_u_bbox[v][1] : 0u),
                                (unsigned int)(p ? s_u_bbox[v][2] : 0u),
                                (unsigned int)(p ? s_u_bbox[v][3] : 0u), kind);
                    ps2_kprintf("[GSLIVE]      dial: %s\n", u_ask[v]);
                    /* "A triangle" is the one predicate every verdict below is built from: more than a few
                     * px of ink, and an apex row of at most 4 px, which is 12l's shape law. */
                    if (s_u_n[v] > 8u && s_u_top[v] <= 4u) {
                        if (v == 0u) v0_tri = 1;
                        else if (v == 1u) v1_tri = 1;
                        else if (v == 2u) v2_tri = 1;
                        else v3_tri = 1;
                    }
                }
                ps2_kprintf("[GSLIVE] 12u triangle alone?  V0 (PRIM=POINT in front) %s | V1 (tag field) %s |\n",
                            v0_tri ? "YES" : "NO", v1_tri ? "YES" : "NO");
                ps2_kprintf("[GSLIVE]   V2 (AD write, Gouraud) %s | V3 (AD write, flat) %s\n",
                            v2_tri ? "YES" : "NO", v3_tri ? "YES" : "NO");
                if (!v1_tri && !v2_tri && !v3_tri) {
                    ps2_kprintf("[GSLIVE] 12u verdict: NEITHER dial puts a triangle on the page -- what a lone\n");
                    ps2_kprintf("[GSLIVE]   group needs is in neither this packet nor either write, so the GS\n");
                    ps2_kprintf("[GSLIVE]   environment this port applies per present is the next thing to read.\n");
                } else if (v1_tri && !v2_tri && !v3_tri) {
                    ps2_kprintf("[GSLIVE] 12u verdict: THE TAG FIELD RULES WHEN PRE IS CLEAR -- ps2sdk's own\n");
                    ps2_kprintf("[GSLIVE]   PRE=0 tag is enough, and the fix is one bit in the recorder.\n");
                } else if (v1_tri && (v2_tri || v3_tri)) {
                    ps2_kprintf("[GSLIVE] 12u verdict: BOTH program PRIM, so the recorder's PRE=1 is what throws\n");
                    ps2_kprintf("[GSLIVE]   the ask away: one bit per group, and the frame stops mattering.\n");
                } else if (!v1_tri && (v2_tri || v3_tri)) {
                    ps2_kprintf("[GSLIVE] 12u verdict: THE AD WRITE RULES AND THE TAG FIELD DOES NOT -- the\n");
                    ps2_kprintf("[GSLIVE]   draw_prim_start() shape is the fix: a PRIM write in front of each\n");
                    ps2_kprintf("[GSLIVE]   chain, which is what box C has always done and 12c watched shade.\n");
                } else {
                    ps2_kprintf("[GSLIVE] 12u verdict: read the four rows above by hand.  V0 now carries a\n");
                    ps2_kprintf("[GSLIVE]   write of POINT in front of the chain, so V0 laying a triangle is\n");
                    ps2_kprintf("[GSLIVE]   the tag's own PRIM field being honoured with PRE SET -- which is\n");
                    ps2_kprintf("[GSLIVE]   what 12u measured as NO, and if it comes back YES the census was\n");
                    ps2_kprintf("[GSLIVE]   not lone after all: 12t's dial proof row is the thing to check.\n");
                }
                ps2_kprintf("[GSLIVE] 12u shading: V2 asked Gouraud and laid field=%u | V3 asked FLAT and laid\n",
                            (unsigned int)s_u_field[2]);
                ps2_kprintf("[GSLIVE]   field=%u, and the two differ in IIP alone: %s\n",
                            (unsigned int)s_u_field[3],
                            (s_u_field[2] != 0u && s_u_field[2] > s_u_field[3])
                                ? "IIP IN THE PRIM WORD IS THE LEVER -- that bit is what the GS interpolates on"
                                : (s_u_field[2] == 0u && s_u_field[3] == 0u)
                                    ? "neither lays a field, so the interpolation measured at A does not come"
                                      " from PRIM's IIP"
                                    : "both lay a field, so IIP did not switch it and the lever is elsewhere");
                /* Rung 12v: this probe is now the before-and-after of the fix.  The census's OWN box D row
                 * above is V2's shape drawn by the recorder itself -- the session writes its own PRIM and
                 * then its chain -- while V0 is the same chain with an OP that cannot make a triangle put in
                 * front of it.  Reading the two against each other is the measurement of what the write
                 * bought in the frame rather than in this replay, which is why both numbers print here. */
                ps2_kprintf("[GSLIVE] 12u x 12v: box D in the FRAME (its session's own AD write + chain):\n");
                ps2_kprintf("[GSLIVE]   painted=%u top=%u/%u | REPLAYED as V0 (chain after a POINT write):"
                            " painted=%u\n",
                            (unsigned int)(sh_tot[3u] - sh_same[3u]),
                            (unsigned int)sh_top[3u],
                            (unsigned int)(s_sh_x1[3u] - s_sh_x0[3u]),
                            (unsigned int)s_u_n[0]);
                ps2_kprintf("[GSLIVE]   %s\n",
                            ((sh_tot[3u] - sh_same[3u]) > 8u && s_u_n[0] <= 8u)
                                ? "the frame lays a shape and a chain with POINT in front of it does not: the"
                                  " write of the REAL primitive is what the stream was missing -- 12v measured"
                                : ((sh_tot[3u] - sh_same[3u]) <= 8u && s_u_n[0] <= 8u)
                                    ? "both point-like: the frame's own write did not take, so read 12t's row"
                                      " and the [GSTS] pwr= column before quoting anything else"
                                    : "both lay a shape: V0 has a POINT write in front of it, so its triangle"
                                      " came from the tag -- re-read 12u's verdict above");
            }
        }

        /* rung 12g, and the row that decides what the two rows above mean.  12f's claim is an identity
         * between two packets, so the census prints both of them as they left their own builders:
         * word 0 is the GIF tag (NLOOP, EOP, PRE, PRIM, FLG and NREG all live in its low 64 bits), word 1
         * is the REGS list, and words 2..7 are the three RGBAQ/XYZ2 pairs.  `words=` is the FULL count
         * even when the capture stopped at eight, so a four-register group that costs sixteen words is
         * visible as one instead of being silently trimmed.
         *
         * The first run of this row (2026-10-18) answered the tag and the REGS list outright -- both
         * packets `NLOOP=3 EOP=1 PRE=1 FLG=1(REGLIST) NREG=2 PRIM=0x0b REGS=0x51`, byte for byte -- and
         * then found the first difference at WORD 3, which is a vertex's XYZ2: the two boxes were at
         * different x then, so that difference was not a finding, it was the geometry.  A verdict that
         * stops at the first difference therefore cannot be read, so this classifies every word by the
         * register the group's own NREG says it holds.  Rung 12h moved the hand chain onto box A's
         * coordinates, which removes the geometry excuse, and 12i keeps them there but stops sending the
         * hand chain: A's recorder group is now the frame's LAST send and its slot is named by
         * `s_cap_rec_at`, while the chain comes from `ps2_gslive_shape_hold()`, so the two captures are
         * still one triangle at one box as two builders made it -- and the expected answer is still
         * `SAME BYTES`.  Rung 12v changed what that last session hands over: two groups, the AD write of
         * its PRIM then the chain, and `s_cap_rec_at` is set at the chain, because a chain compared with
         * a hand chain is the identity 12g measured and a chain compared with a preamble is not. */
        /* Rung 12t: in a LONE census the hand chain never went out and box A's session never opened, so the
         * hand capture is zeroed here and 12g's own `SKIP -- a packet was not captured` row becomes the true
         * statement about the census.  Since 12v the recorder-side dump describes whatever `s_cap_rec_at` was
         * reset to -- an empty slot, `words=0` -- rather than box D's preamble, so the row cannot read one
         * box's packet as another's; 12i's per-send rows still name each captured group's owner. */
        if (s_census_only < GSLIVE_SH_N) s_cap_hand_words = 0u;
        {
            uint32_t k, mask = 0u, colour_diff = 0u, coord_diff = 0u;
            const uint32_t nreg = gslive_nreg_of(s_cap_rec);
            uint32_t n = s_cap_rec_words < s_cap_hand_words ? s_cap_rec_words : s_cap_hand_words;

            if (n > GSLIVE_CAP_WORDS) n = GSLIVE_CAP_WORDS;
            for (k = 2u; k < n; k++) {
                if (s_cap_rec[k] == s_cap_hand[k]) continue;
                mask |= 1u << k;
                /* In a dense REGLIST the data words run in the order the REGS list names them, so a
                 * word's register is its position modulo the group's NREG: register 0 is always RGBAQ,
                 * and for a two-register chain register 1 is the drawing kick XYZ2. */
                if (nreg != 0u && (k - 2u) % nreg == 0u) colour_diff++;
                else coord_diff++;
            }
            ps2_kprintf("[GSLIVE] 12g A rec(LAST)  words=%u tag=0x%08x%08x NLOOP=%u EOP=%u PRE=%u FLG=%u"
                        " NREG=%u PRIM=0x%03x REGS=0x%08x%08x\n",
                        (unsigned int)s_cap_rec_words,
                        (unsigned int)(s_cap_rec[0] >> 32), (unsigned int)s_cap_rec[0],
                        (unsigned int)gslive_nloop_of(s_cap_rec), (unsigned int)gslive_eop_of(s_cap_rec),
                        (unsigned int)gslive_pre_of(s_cap_rec), (unsigned int)gslive_flg_of(s_cap_rec),
                        (unsigned int)gslive_nreg_of(s_cap_rec), (unsigned int)gslive_prim_of(s_cap_rec),
                        (unsigned int)(s_cap_rec[1] >> 32), (unsigned int)s_cap_rec[1]);
            ps2_kprintf("[GSLIVE] 12g A(hand)     words=%u tag=0x%08x%08x NLOOP=%u EOP=%u PRE=%u FLG=%u"
                        " NREG=%u PRIM=0x%03x REGS=0x%08x%08x\n",
                        (unsigned int)s_cap_hand_words,
                        (unsigned int)(s_cap_hand[0] >> 32), (unsigned int)s_cap_hand[0],
                        (unsigned int)gslive_nloop_of(s_cap_hand), (unsigned int)gslive_eop_of(s_cap_hand),
                        (unsigned int)gslive_pre_of(s_cap_hand), (unsigned int)gslive_flg_of(s_cap_hand),
                        (unsigned int)gslive_nreg_of(s_cap_hand), (unsigned int)gslive_prim_of(s_cap_hand),
                        (unsigned int)(s_cap_hand[1] >> 32), (unsigned int)s_cap_hand[1]);
            ps2_kprintf("[GSLIVE] 12g words 0..%u differ=0x%02x: %u colour register(s), %u kick/position"
                        " register(s); header words %s\n",
                        (unsigned int)(n ? n - 1u : 0u), (unsigned int)mask,
                        (unsigned int)colour_diff, (unsigned int)coord_diff,
                        (s_cap_rec[0] == s_cap_hand[0] && s_cap_rec[1] == s_cap_hand[1])
                            ? "MATCH" : "DIFFER");
            if (s_cap_rec_words == 0u || s_cap_hand_words == 0u)
                ps2_kprintf("[GSLIVE] 12g: SKIP -- a packet was not captured (recorder %u word(s),"
                            " hand %u), so no identity is being claimed\n",
                            (unsigned int)s_cap_rec_words, (unsigned int)s_cap_hand_words);
            else if (mask == 0u && s_cap_rec_words == s_cap_hand_words)
                ps2_kprintf("[GSLIVE] 12g: the recorder's LAST group and the hand chain ask the SAME %u"
                            " WORDS at box A -- same tag, same REGS, same three colours, same vertices,"
                            " so the packet is excluded and A-early against A-late is one box, one"
                            " encoding and one variable: where in the frame it went out\n",
                            (unsigned int)s_cap_rec_words);
            else if (colour_diff == 0u && coord_diff != 0u)
                ps2_kprintf("[GSLIVE] 12g: same tag, same REGS, same three colours, but %u POSITION"
                            " word(s) differ AT ONE AND THE SAME BOX -- so the two builders disagree"
                            " about where the triangle is, not about shading, and A's two cells are not"
                            " one triangle.  Fix the geometry before quoting the verdict below\n",
                            (unsigned int)coord_diff);
            else
                ps2_kprintf("[GSLIVE] 12g: the COLOUR PAYLOAD differs at %u register(s) (mask 0x%02x) --"
                            " so the recorder and the hand chain do not hand the GS the same triangle\n",
                            (unsigned int)colour_diff, (unsigned int)mask);
        }

        /* Rung 12i's send dump.  The verdict below reads box D's group against box A's as ONE packet in
         * two places in the frame, and that claim is only good if each of the two really is one group of
         * one triangle, with nothing else riding out beside it.  So the census prints every group its
         * three one-triangle sessions handed the sink, in the order they left, and names the box each came
         * from -- a session that flushed twice, or a group that went out behind a texture AD trio, shows
         * here instead of being inferred from the pixel counts.  Rung 12v makes that row load-bearing in a
         * new way: every session now DOES hand two groups, so the dump is the place a reader can see the
         * AD write of PRIM and the chain that follows it as the two transfers they are, and `CHAIN` /
         * `AD(PRIM)` are read off each group's own FLG rather than asserted from the recorder's source. */
        {
            uint32_t k, n_diff = 0u, n_pos = 0u, n_col = 0u;
            const uint32_t a_at = s_sh_sess_first[0u];
            const uint32_t d_at = s_sh_sess_first[3u];
            const int have_a = (a_at < GSLIVE_CAP_SENDS && s_cap_send_words[a_at] != 0u);
            const int have_d = (d_at < GSLIVE_CAP_SENDS && s_cap_send_words[d_at] != 0u);

            ps2_kprintf("[GSLIVE] 12i %u send(s) from 3 one-triangle sessions: A=%u B=%u D=%u group(s)"
                        " (12v: 2 = one PRIM write + one chain)\n",
                        (unsigned int)s_cap_sends, (unsigned int)s_sh_sess_groups[0u],
                        (unsigned int)s_sh_sess_groups[1u], (unsigned int)s_sh_sess_groups[3u]);
            for (k = 0u; k < s_cap_sends && k < GSLIVE_CAP_SENDS; k++) {
                const uint8_t own = s_sh_sess_owner[k];
                char owner = (own != 0u && own - 1u < GSLIVE_SH_N) ? (char)('A' + own - 1u) : '?';
                const char *pos = "none";
                const char *kind = (gslive_flg_of(s_cap_send[k]) == GS_FLG_REGLIST) ? "CHAIN" : "AD(PRIM)";

                if (owner == 'A') pos = "LAST";
                else if (owner == 'B') pos = "2ND";
                else if (owner == 'D') pos = "EARLY";
                ps2_kprintf("[GSLIVE] 12i send %u %c %s %s words=%u NLOOP=%u EOP=%u PRE=%u FLG=%u"
                            " NREG=%u PRIM=0x%02x\n",
                            (unsigned int)k, owner, pos, kind,
                            (unsigned int)s_cap_send_words[k],
                            (unsigned int)gslive_nloop_of(s_cap_send[k]),
                            (unsigned int)gslive_eop_of(s_cap_send[k]),
                            (unsigned int)gslive_pre_of(s_cap_send[k]),
                            (unsigned int)gslive_flg_of(s_cap_send[k]),
                            (unsigned int)gslive_nreg_of(s_cap_send[k]),
                            (unsigned int)gslive_prim_of(s_cap_send[k]));
            }
            if (!have_a || !have_d) {
                ps2_kprintf("[GSLIVE] 12i: SKIP -- D's or A's group was not captured (D have=%d A have=%d"
                            " of %u send(s)), so the two cells are not compared\n",
                            have_d, have_a, (unsigned int)s_cap_sends);
            } else {
                const uint32_t nreg_d = gslive_nreg_of(s_cap_send[d_at]);
                uint32_t n = s_cap_send_words[d_at] < s_cap_send_words[a_at]
                               ? s_cap_send_words[d_at] : s_cap_send_words[a_at];

                if (n > GSLIVE_CAP_WORDS) n = GSLIVE_CAP_WORDS;
                for (k = 2u; k < n; k++) {
                    if (s_cap_send[d_at][k] == s_cap_send[a_at][k]) continue;
                    n_diff++;
                    if (nreg_d != 0u && (k - 2u) % nreg_d == 0u) n_col++;
                    else n_pos++;
                }
                ps2_kprintf("[GSLIVE] 12i D-vs-A: header words %s, %u data word(s) differ = %u position,"
                            " %u colour -- %s\n",
                            (s_cap_send[d_at][0] == s_cap_send[a_at][0] &&
                             s_cap_send[d_at][1] == s_cap_send[a_at][1]) ? "MATCH" : "DIFFER",
                            (unsigned int)n_diff, (unsigned int)n_pos, (unsigned int)n_col,
                            (s_cap_send[d_at][0] == s_cap_send[a_at][0] && n_col == 0u)
                                ? "one packet, two boxes" : "NOT one packet"
                            );
            }
        }

        /* Rung 12i's reading: two cells, both the recorder's own group, each alone in a session of one
         * triangle -- box A asked LAST, behind three hand triangles, and box D asked FIRST, in front of
         * everything.  The constants they are read against are 12h's, measured in this same band: that
         * group at A's coordinates at the FRONT of the frame was FLAT four censuses running
         * (painted=960) and the hand chain at A's coordinates behind three transfers laid a field
         * (other=890).  A-late against A-early is therefore one box, one packet, two positions; D-first
         * against D-third is one box, one sender, one group alone versus one of three.  Four combinations
         * come back and they mean four different things to fix, so the row names which one this run gave
         * instead of leaving it to be worked out from two counts. */
        const int ok_c = (sh_other[2u] > (sh_tot[2u] - sh_same[2u]) / 4u);
        const int ok_e = (sh_other[4u] > (sh_tot[4u] - sh_same[4u]) / 4u);
        const int ok_f = (sh_other[5u] > (sh_tot[5u] - sh_same[5u]) / 4u);
        const uint32_t a_painted = sh_tot[0u] - sh_same[0u];   /* recorder, alone, LAST */
        const uint32_t b_painted = sh_tot[1u] - sh_same[1u];
        const uint32_t d_painted = sh_tot[3u] - sh_same[3u];   /* recorder, alone, FIRST */
        const int cell_a_late = (sh_other[0u] > a_painted / 4u);
        const int cell_d_early = (sh_other[3u] > d_painted / 4u);
        const int ok_b = (b_painted != 0u && sh_other[1u] <= b_painted / 4u);
        const int win_b = (sh_r[1u] >= sh_g[1u] && sh_r[1u] >= sh_b[1u]) ? 'R'
                          : ((sh_g[1u] >= sh_b[1u]) ? 'G' : 'B');

        if (s_census_only < GSLIVE_SH_N)
            ps2_kprintf("[GSLIVE] shading verdict: SKIP -- this is 12t's LONE census.  The control, box B, "
                        "box A and the hand chain were withheld, so one box has a packet behind it and the "
                        "2x2 cannot be graded; 12t's own verdict above is this census's answer\n");
        else if (ctrl != GSLIVE_PX)
            ps2_kprintf("[GSLIVE] shading verdict: SKIP -- the magenta control in this same band did "
                        "not come back whole (%u/%u), so the page these boxes were read out of is not "
                        "certified and no shading answer above may be quoted\n",
                        (unsigned int)ctrl, (unsigned int)GSLIVE_PX);
        else if (s_sh_refused != 0u || s_shape_refused != 0u)
            ps2_kprintf("[GSLIVE] shading verdict: SKIP -- a group was refused on the way out, so at "
                        "least one box on the page is a part-draw\n");
        else if (s_sh_groups + s_sh_held != 6u || s_cap_sends != 6u || s_sh_held != 0u ||
                 s_sh_sess_groups[0u] != 2u || s_sh_sess_groups[1u] != 2u ||
                 s_sh_sess_groups[3u] != 2u || s_shape_sent != 3u)
            ps2_kprintf("[GSLIVE] shading verdict: SKIP -- the recorder sent %u group(s) and 12q's mask "
                        "held %u, in %u captured send(s) (A=%u B=%u D=%u) and the matrix %u.  Since 12v "
                        "each box must be TWO groups -- one write of its PRIM and one chain -- EVERY one "
                        "must go out, or a cell above has no packet behind it -- which is 12q's dial, not "
                        "a failure\n",
                        (unsigned int)s_sh_groups, (unsigned int)s_sh_held,
                        (unsigned int)s_cap_sends,
                        (unsigned int)s_sh_sess_groups[0u], (unsigned int)s_sh_sess_groups[1u],
                        (unsigned int)s_sh_sess_groups[3u], (unsigned int)s_shape_sent);
        else if (!(ok_c && ok_e && ok_f))
            ps2_kprintf("[GSLIVE] shading verdict: SKIP -- a CONTROL went flat (C %d, E %d, F %d; the "
                        "three measured other=840, 609 and 441), so this run's read or environment "
                        "differs from the one the recorder change was designed against and the grid "
                        "below would be an answer about the instrument, not about the GS\n",
                        ok_c, ok_e, ok_f);
        else if (!ok_b)
            ps2_kprintf("[GSLIVE] shading verdict: SKIP -- box B, asked IIP=0 on the same chain as the "
                        "recorder's cells, came back with a field (painted=%u other=%u) instead of one "
                        "corner, so this read cannot tell a gradient from a flat colour and a cell "
                        "answer means nothing\n",
                        (unsigned int)b_painted, (unsigned int)sh_other[1u]);
        else if (s_cap_rec_at != s_sh_sess_first[0u] || s_cap_send_words[s_cap_rec_at] == 0u)
            ps2_kprintf("[GSLIVE] shading verdict: SKIP -- A's group is not in the capture array at the "
                        "slot its session names (captured slot %u, session slot %u), so 12g's identity "
                        "is being read out of some other box's packet\n",
                        (unsigned int)s_cap_rec_at, (unsigned int)s_sh_sess_first[0u]);
        else {
            ps2_kprintf("[GSLIVE] shading verdict: the grid is readable (C/E/F interpolated, B is one "
                        "flat colour %c).  Box D, the recorder's group ALONE and FIRST, %s "
                        "(painted=%u other=%u).  Box A, the same group ALONE and LAST, %s "
                        "(painted=%u other=%u).\n",
                        win_b, cell_d_early ? "INTERPOLATES" : "is FLAT",
                        (unsigned int)d_painted, (unsigned int)sh_other[3u],
                        cell_a_late ? "INTERPOLATES" : "is FLAT",
                        (unsigned int)a_painted, (unsigned int)sh_other[0u]);
            ps2_kprintf("[GSLIVE] grid: recorder@A first FLAT (12h, 4 censuses, painted=960), hand@A "
                        "behind 3 INTERPOLATES (12h, other=890), recorder@D third FLAT (12h,"
                        " painted=1408), recorder@D alone-first %s, recorder@A alone-last %s -- %s\n",
                        cell_d_early ? "INTERPOLATES" : "FLAT",
                        cell_a_late ? "INTERPOLATES" : "FLAT",
                        (!cell_d_early && !cell_a_late) ? "SENDER: order and session both excluded"
                            : (cell_d_early && cell_a_late) ? "SESSION: position excluded"
                                : (cell_a_late && !cell_d_early) ? "ORDER: a gradient needs transfers"
                                    : "PLACE: the box decides, and only for the recorder");
            if (!cell_d_early && !cell_a_late)
                ps2_kprintf("[GSLIVE] shading verdict: the variable is the SENDER.  The recorder's group "
                            "is flat at the front of the frame (D: painted=%u, other=%u) and at the back "
                            "of it (A: %u, %u), alone in a session and shared in one, while the hand "
                            "chain -- the same eight bytes (12g) -- shades at A's coordinates from this "
                            "same late position.  Where it goes out and what goes out beside it move "
                            "nothing, so what is left is the code that stages and hands the packet: the "
                            "sink's aligned copy, the qwc it passes, the buffer it lives in.  Rung 12k "
                            "takes the recorder's CAPTURED bytes back through the hand's own send at "
                            "box A, which is the last split between the packet and the sender.\n",
                            (unsigned int)d_painted, (unsigned int)sh_other[3u],
                            (unsigned int)a_painted, (unsigned int)sh_other[0u]);
            else if (cell_d_early && cell_a_late)
                ps2_kprintf("[GSLIVE] shading verdict: the variable is the SESSION.  Alone in a session "
                            "of its own the recorder's group shades at BOTH ends of the frame (D "
                            "painted=%u other=%u, A %u/%u), and in 12h the same group shared one session "
                            "and went flat at both -- the only difference left is that a session's "
                            "second and third groups are cut by `gs_rec_prim()` out of a buffer the "
                            "first one already used.  The production recorder draws whole frames of "
                            "triangles in one session, so this is the answer that matters, and rung 12k "
                            "puts TWO triangles in one session at two boxes and reads whether the second "
                            "goes flat behind the first\n",
                            (unsigned int)d_painted, (unsigned int)sh_other[3u],
                            (unsigned int)a_painted, (unsigned int)sh_other[0u]);
            else if (cell_a_late)
                ps2_kprintf("[GSLIVE] shading verdict: the variable is the frame's ORDER.  Box A shades "
                            "once three transfers have gone out in front of it (painted=%u other=%u) and "
                            "box D, first in the frame with nothing in front, does not (%u/%u) -- so a "
                            "gradient here needs a triangle transfer BEFORE it, not a different packet.  "
                            "That is a frame problem, not an encoder problem, and it says the production "
                            "stream has to put something in front of its first shaded triangle -- but "
                            "rung 12j's bbox and spill rows above have to be read FIRST, because a flat "
                            "box holding its whole rect is a geometry answer and not a shading one\n",
                            (unsigned int)a_painted, (unsigned int)sh_other[0u],
                            (unsigned int)d_painted, (unsigned int)sh_other[3u]);
            else
                ps2_kprintf("[GSLIVE] shading verdict: the variable is the PLACE, for the recorder alone. "
                            "Box D's group shades at the front of the frame (painted=%u other=%u) and "
                            "box A's is flat at the back of it (%u/%u) after being flat at the front in "
                            "12h, so the recorder shades at one x in this band and not at another while "
                            "the hand chain shades at both.  In 12k the two boxes differed in width (A "
                            "128 px, apex 176; D 88 px, apex 580), which is why 12k swept the "
                            "recorder's x at one encoding -- and rung 12n now narrows D's ask inside its "
                            "own window, so read that sweep as 12k's and this row as 12n's\n",
                            (unsigned int)d_painted, (unsigned int)sh_other[3u],
                            (unsigned int)a_painted, (unsigned int)sh_other[0u]);
        }
    }

    have_prev = s_live_prev_valid;
    for (i = 0u; i < (uint32_t)PS2_SCREEN_HEIGHT; i++) {
        ink_now += s_live_cur_ink[i];
        same_now += s_live_cur_same[i];
        if (!have_prev) continue;
        ink_then += s_live_prev_ink[i];
        if (s_live_cur_ink[i] < s_live_prev_ink[i]) {
            erased += s_live_prev_ink[i] - s_live_cur_ink[i];
            if (s_live_cover_row[i]) lost_rows_covered++;
            if (s_live_nostream_row[i]) lost_rows_nostream++;
            if (lost_rows++ == 0u) lost_first = i;
            lost_last = i;
        } else if (s_live_cur_ink[i] > s_live_prev_ink[i]) {
            laid += s_live_cur_ink[i] - s_live_prev_ink[i];
        }
    }
    /* What the client area holds that is NOT ink, in the same top-N shape as the ink rows above, and
     * read against them rather than alone: a hole of 0x00000000 is the app's own CPU pixmap having
     * been blitted and uploaded, a hole of 0xFF121B29 is the console, a hole of the desktop's plate
     * colour is the composite's frame stage, a hole of the wallpaper is the background cache.  Those
     * are four different files to change, and rung 6b's row count cannot tell them apart -- it only
     * knows the page came back to the canvas. */
    ps2_kprintf("[GSLIVE] canvas in the client: %u px, %u bin(s), rare=%u\n",
                (unsigned int)same_now, (unsigned int)cv_bins, (unsigned int)cv_rare);
    for (k = 0u; k < 3u && k < cv_bins; k++) {
        int seeded = 0;

        best = 0u;
        for (i = 0u; i < cv_bins; i++) {
            if (cv_printed[i]) continue;
            if (!seeded || cv_n[i] > cv_n[best]) { best = i; seeded = 1; }
        }
        if (!seeded) break;
        cv_printed[best] = 1;
        ps2_kprintf("[GSLIVE] canvas: 0x%08x n=%u box=%d,%d..%d,%d\n",
                    (unsigned int)cv_v[best], (unsigned int)cv_n[best],
                    cv_box[best][0], cv_box[best][1], cv_box[best][2], cv_box[best][3]);
    }

    ps2_kprintf("[GSLIVE] survival: census #%u client ink=%u canvas=%u of %u px%s\n",
                (unsigned int)(s_live_census_no + 1u), (unsigned int)ink_now,
                (unsigned int)same_now, (unsigned int)win_px,
                win_px ? "" : " (no client rect: no XMB window, so nothing to tally)");
    if (have_prev && win_px) {
        uint32_t cover_rows = 0u;

        for (i = 0u; i < (uint32_t)PS2_SCREEN_HEIGHT; i++)
            if (s_live_cover_row[i]) cover_rows++;

        /* Rung 6c's prints follow the survival rows they are read against. */
        ps2_kprintf("[GSLIVE] survival: ink %u -> %u px between census #%u and #%u -- erased=%u over "
                    "%u row(s) (client y %d..%d), newly laid=%u\n",
                    (unsigned int)ink_then, (unsigned int)ink_now,
                    (unsigned int)s_live_census_no, (unsigned int)(s_live_census_no + 1u),
                    (unsigned int)erased, (unsigned int)lost_rows,
                    lost_rows ? cy0 + (int)lost_first : -1,
                    lost_rows ? cy0 + (int)lost_last : -1, (unsigned int)laid);
        ps2_kprintf("[GSLIVE] survival verdict: %s\n",
                    erased * 10u > ink_then
                        ? "the ink ARRIVES AND THEN LEAVES -- a present between the two reads re-lays "
                          "the canvas over most of what the stream drew, which is the one reading that "
                          "puts the census (client rect full) and the operator (window empty) both right, "
                          "and it makes the eraser, not the stream, the next thing to price"
                        : (erased == 0u && ink_now == ink_then
                               ? "the ink HOLDS, row for row, between two presents -- what the census sees "
                                 "is what the page keeps, so a screen that disagrees is a display-environment "
                                 "question (which page, which buffer), not a present-order one"
                               : "the ink mostly holds; the rows that moved are animation, not erasure"));
        /* Rung 6c: WHO, and does the row set match.  The cover list is the only uploader that puts
         * the canvas INSIDE a layer's rect after the stream -- by design, for whatever the composite
         * drew above the layer -- so this is a direct test of it, and it is written to fail: a lost
         * row with no upload in it falsifies the reading and points back at the pass's own whole-width
         * upload, or at a stream that stopped reaching those rows.  `cover_px` is an upper bound by
         * construction (two passes can cover one row), which is why the test is `>=` and never `==`. */
        ps2_kprintf("[GSLIVE] eraser: %u cover upload(s) since census #%u laid %u px back to the "
                    "canvas over %u row(s); %u of %u lost row(s) sit inside them%s\n",
                    (unsigned int)cover_rects, (unsigned int)s_live_census_no,
                    (unsigned int)cover_px, (unsigned int)cover_rows,
                    (unsigned int)lost_rows_covered, (unsigned int)lost_rows,
                    cover_missed ? " (the ring wrapped: older rects in this window were not counted)"
                                 : "");
        ps2_kprintf("[GSLIVE] eraser verdict: %s\n",
                    erased == 0u
                        ? "NOTHING WAS ERASED between the two reads, so the cover ring is a control "
                          "here rather than a suspect -- it put px on the bus anyway and the stream "
                          "kept its ink"
                        : (lost_rows != 0u && lost_rows_covered == lost_rows &&
                           cover_px >= erased)
                            ? "THE COVER UPLOADS ACCOUNT FOR THE WHOLE HOLE -- "
                              "ps2_upload_cover_in_layer() re-laid the canvas inside the client AFTER "
                              "the stream, which is exactly what the operator calls an empty window. "
                              "The cure is LANE 9d's predicate: refuse a cover rect whose canvas holds "
                              "nothing above the layer, the way 9a refuses the body plate"
                        : (lost_rows_covered == 0u)
                            ? "FALSIFIED for the cover path: no upload touched a single lost row, so "
                              "the eraser is the pass's own whole-width canvas upload landing after the "
                              "stream, or a stream that stopped covering those rows -- read the canvas "
                              "colour rows above to tell those two apart"
                            : "PARTLY accounted: the cover path explains some lost rows and not the "
                              "rest, so at least a second uploader has to be named before any fix");
        for (i = 0u; i < hc_n; i++)
            ps2_kprintf("[GSLIVE] eraser: %s%s rect %u = %d,%d..%d,%d (%u px) sent at pass %u\n",
                        hc_kind[i] == PS2_UPLOAD_COVER ? "cover" : "canvas",
                        hc_kind[i] == PS2_UPLOAD_COVER ? "" : ps2_bench_why_name(hc_why[i]),
                        (unsigned int)i, hc_rect[i][0], hc_rect[i][1], hc_rect[i][2], hc_rect[i][3],
                        (unsigned int)((hc_rect[i][2] - hc_rect[i][0]) *
                                       (hc_rect[i][3] - hc_rect[i][1])),
                        (unsigned int)hc_pass[i]);

        /* Rung 20w: the other uploader, named rather than excluded by width.  A canvas rect inside
         * the client with a stream behind it is the order rule working -- the page gets the
         * desktop and then the picture over it.  One with no stream behind it is the picture gone:
         * the canvas has no image of the layer, so those pixels are whatever the wallpaper was, and
         * that is the grey the operator sees where the wave should be.  ps2_upload_canvas() cuts the
         * layer's rect out of exactly those passes, so `with no stream` is the number that grades
         * the cut, and the invariant is that it is zero while a hole still exists.
         *
         * Rung 20x splits that one number by the condition that produced it, because three of its
         * meanings are not the same bug and one is not a bug at all: a pass with no layer window on
         * the screen is desktop by contract, and the rows it re-lays left the page because the
         * window went away, not because the cut failed.
         *
         * The control that makes `cut=0` mean something (2026-10-18): with `gs_paints` forced false
         * while the layer stays up -- so the rect is known and the stream is not -- this same build
         * prints `cut=2 ... the cut failed on 2 of 63` and the census's ink collapses to 0 px, and the
         * grader fails both rows.  The honest build prints `cut=0` on every census, which is therefore
         * a measurement and not a tautology: on the operator's route every pass that takes the layer
         * also stages a stream (`emptystream=0` on all nine [GSTS] rows).  So the eraser this rung
         * named is a hazard that is now refused rather than a bug that was happening, and the grey the
         * operator sees lives on the passes that DO send one -- the canvas goes out whole, 384,000 px
         * of it inside the client, and the page shows desktop until the stream overwrites it.  That is
         * rung 20y's subject, and its instrument is the `plated=` column on the [GSTS] row. */
        {
            const uint32_t ns_wrong = ns_why[PS2_WHY_CUT] + ns_why[PS2_WHY_NO_RECT] +
                                      ns_why[PS2_WHY_RELAY];
            ps2_kprintf("[GSLIVE] eraser 20w: %u canvas upload(s) crossed the client, %u of them on a "
                        "pass that sent no stream (%u px); %u of %u lost row(s) sit inside a no-stream "
                        "upload (cover uploads beside them: %u, %u px)\n",
                        (unsigned int)canvas_rects, (unsigned int)nostream_rects,
                        (unsigned int)canvas_px, (unsigned int)lost_rows_nostream,
                        (unsigned int)lost_rows, (unsigned int)cover_rects, (unsigned int)cover_px);
            ps2_kprintf("[GSLIVE] eraser 20x: no-stream uploads by producer -- cut=%u no-layer=%u "
                        "no-rect=%u staged-none=%u, so the cut failed on %u of %u\n",
                        (unsigned int)ns_why[PS2_WHY_CUT], (unsigned int)ns_why[PS2_WHY_NO_LAYER],
                        (unsigned int)ns_why[PS2_WHY_NO_RECT], (unsigned int)ns_why[PS2_WHY_RELAY],
                        (unsigned int)ns_wrong, (unsigned int)nostream_rects);
            ps2_kprintf("[GSLIVE] 20w verdict: %s\n",
                        erased == 0u
                            ? "nothing left the page between the two reads, so the cut is not being "
                              "tested here -- it needs a hole to explain"
                            : (ns_wrong != 0u
                                   ? "THE CUT IS NOT HOLDING: a canvas upload put desktop pixels "
                                     "inside the layer's rect on a pass with no stream to repaint "
                                     "them -- read the 20x row for which producer and the rect list "
                                     "for where"
                                   : (nostream_rects != 0u
                                          ? "the cut holds: every no-stream upload inside the client "
                                            "came from a pass with NO layer window, so those rows "
                                            "left the page because the window closed or had not "
                                            "opened yet -- desktop by contract, not an eraser"
                                          : (canvas_rects != 0u
                                                 ? "the cut holds: every canvas upload inside the "
                                                   "client was followed by a stream, so the hole is "
                                                   "either a cover rect the composite really drew "
                                                   "above the layer or a stream that stopped covering "
                                                   "those rows"
                                                 : "the cut holds and no canvas upload crossed the "
                                                   "client at all, so the hole is the cover list or "
                                                   "the stream itself"))));
        }

        /* The author, by measurement rather than by archaeology.  The cover list has five producers
         * (panel rows, bar rows, the global menu, the Tracker menu, the TIP candidate window) and the
         * geometry says which kind of thing the rect is; the getters say which one of them is actually
         * open THIS frame.  A match is the good outcome -- it means the present re-laid what the
         * composite really drew above the layer, and the hole is desktop chrome rather than a bug.  No
         * match means an adder put a rect on the list that nothing above the layer owns, and that is
         * the LANE 9d refusal case. */
        {
            RECT gm, tm;
            const BOOL gm_open = global_menu_get_open_rect(&gm);
            const BOOL tm_open = tracker_get_menu_rect(&tm);
            int who = 0;

            if (hc_n && ps2_bench_cover_is(&gm, hc_rect[0], cx0, cy0, cx1, cy1)) who = 1;
            else if (hc_n && ps2_bench_cover_is(&tm, hc_rect[0], cx0, cy0, cx1, cy1)) who = 2;
            ps2_kprintf("[GSLIVE] eraser author: global_menu=%d,%d..%d,%d(%s) "
                        "tracker_menu=%d,%d..%d,%d(%s) tip_state=%d\n",
                        gm_open ? (int)gm.left : -1, gm_open ? (int)gm.top : -1,
                        gm_open ? (int)gm.right : -1, gm_open ? (int)gm.bottom : -1,
                        gm_open ? "OPEN" : "closed",
                        tm_open ? (int)tm.left : -1, tm_open ? (int)tm.top : -1,
                        tm_open ? (int)tm.right : -1, tm_open ? (int)tm.bottom : -1,
                        tm_open ? "OPEN" : "closed", (int)tip_get_state());
            ps2_kprintf("[GSLIVE] eraser author: %s\n",
                        who == 1 ? "THE GLOBAL MENU -- open, and its rect is the hole: the present "
                                   "re-laid desktop chrome the composite really drew above the layer"
                                 : (who == 2 ? "THE TRACKER MENU -- open, and its rect is the hole"
                                             : (hc_n == 0u
                                                ? "NO COVER UPLOAD went out between the two reads, so "
                                                  "there is no hole for an overlay to author"
                                                : "NO OPEN OVERLAY MATCHES the recorded rect, so "
                                                  "either the TIP window added it or a stale rect is "
                                                  "on the cover list -- the refusal case, and the one "
                                                  "the fix is for")));
        }
    }    for (i = 0u; i < (uint32_t)PS2_SCREEN_HEIGHT; i++) s_live_prev_ink[i] = s_live_cur_ink[i];
    s_live_prev_valid = 1;
    s_live_census_no++;

    /* The fork, in the order that stops an incomplete or uncertified read from being filed as an
     * answer about the GS: the read's own completion, then the control's, then the window's absence,
     * then the three readings the header names. */
    if (sent != 0u) {
        ps2_kprintf("[GSLIVE] SKIP -- %u words the download never touched, so this is a part-page and "
                    "not a census\n", (unsigned int)sent);
    } else if (ctrl != GSLIVE_PX) {
        ps2_kprintf("[GSLIVE] SKIP -- the control came back %u/%u, so a live-session read is not "
                    "certified here and no count above may be quoted as the stream's absence.  "
                    "An upload between the present and the band, or the GIF still busy with the "
                    "frame, is what this row means\n",
                    (unsigned int)ctrl, (unsigned int)GSLIVE_PX);
    } else if (!w) {
        ps2_kprintf("[GSLIVE] SKIP -- no `Cross Media Bar` window on the table at this frame, so "
                    "there is no client rect to split the ink by; the whole-page counts above stand "
                    "and the control says the read works\n");
    } else if (w->gl_layer == 0) {
        ps2_kprintf("[GSLIVE] THE WINDOW IS NOT A GS LAYER in this build: the compositor blits its "
                    "pixmap and no stream is expected in its rect, so `empty inside` is a dispatch "
                    "or settings answer (which backend gl_dispatch.c installed) and not a GS one.  "
                    "ink=%u of which in_win=%u\n",
                    (unsigned int)diff_ink, (unsigned int)diff_win);
    } else if (diff_ink == 0u) {
        ps2_kprintf("[GSLIVE] THE PAGE IS ITS CANVAS -- outside this rung's own control box, every "
                    "word the page holds is the word the canvas holds (console=%u), so the recorded "
                    "stream lays NO pixel anywhere.  The packet is proved elsewhere in this census; "
                    "what is left between the frame and the screen is the live environment "
                    "(win/clip/ALPHA-ABE), the order of its groups against the upload, or a prim "
                    "state no rung has varied yet\n",
                    (unsigned int)diff_console);
    } else if (diff_win == 0u) {
        ps2_kprintf("[GSLIVE] THE STREAM LANDS OUTSIDE ITS OWN WINDOW -- %u ink words and none of "
                    "them inside %d,%d..%d,%d.  That is XYOFFSET, SCISSOR or the client rect the "
                    "present hands eegs_present_send(), and the all-box above says where they went\n",
                    (unsigned int)diff_ink, cx0, cy0, cx1, cy1);
    } else if (win_px != 0u && diff_win * 100u < win_px) {
        ps2_kprintf("[GSLIVE] THE STREAM PAINTS UNDER 1%% OF ITS CLIENT AREA -- %u of %u px.  Delivery "
                    "works; what the eye calls an empty inside is a nearly-empty picture, and the "
                    "ink rows are the content question\n",
                    (unsigned int)diff_win, (unsigned int)win_px);
    } else {
        ps2_kprintf("[GSLIVE] THE STREAM PAINTS ITS CLIENT AREA -- %u/%u px inside its own rect, so "
                    "what arrives is on the page and the remaining question is WHICH picture it is; "
                    "read the ink colours and boxes above against what the XMB should show\n",
                    (unsigned int)diff_win, (unsigned int)win_px);
    }

    /* LANE 19 rung 10, and the reading every rung above stops short of: the page as something a
     * person can LOOK at rather than count.  Gate -- a complete read (a part-page would print a
     * picture with holes in it and call it the app's), a GS-layer window that is mostly ink (an
     * early census reads a window that has not painted yet, and its picture is the desktop's), and
     * at most two per run, because the row is 2 kB of log and the second one is what shows whether
     * the picture moved between presents. */
    if (sent == 0u && w && w->gl_layer && win_px != 0u &&
        diff_win > win_px / 2u && s_spk_printed < 2u) {
        s_spk_sent = sent;
        s_spk_printed++;
        gspeak_dump();
    }
}
#endif /* BTRON_PS2_BENCH_GS >= 2 */

/* The window's own state, sampled by the frame hook while it is still on the table.
 *
 * The phase cannot read it after `shell_execute_cmd()` returns: an app opener here does
 * not return while its window is up -- that is why every app phase is armed around the
 * call -- so by the time the shell has answered, the window is closed and its `WND` is
 * somebody else's memory.  A pointer kept past that prints plausible geometry after the
 * thing it names is gone, which is worse than no row at all, so these are values, taken
 * once, and `s_bench_shell_watch` says which phase may fill them. */
static int s_bench_shell_watch;
static int s_bench_shell_window_seen;
static int s_bench_shell_geom[8];     /* client w, client h, visible, gl_layer, img_valid, paint,
                                       * gs planes registered, gs VRAM kB left */

/* One `xmb` verb, the frames it drew, and the close that followed it -- with the pool read
 * on both sides, because the complaint under investigation is "the row is there and it
 * picks nothing", and on a first-fit pool that answer is a hole size, not a program counter. */
typedef struct {
    int lines, not_linked, no_surface, opened, window_seen;
    uint32_t frames;
    size_t used_before, used_after, hole_before, hole_after;
    const char *why;
} ps2_bench_cycle_t;

static void ps2_bench_one_cycle(GDEV *screen, const size_t ask, ps2_bench_cycle_t *c)
{
    int guard;

    c->lines = c->not_linked = c->no_surface = c->opened = 0;
    c->window_seen = 0;
    for (guard = 0; guard < 8; guard++) s_bench_shell_geom[guard] = -1;
    c->frames = 0u;

    c->used_before = s_heap_used;
    c->hole_before = ps2_heap_largest_free();

    s_bench_shell_lines = 0;
    s_bench_shell_not_linked = s_bench_shell_no_surface = s_bench_shell_opened = 0;
    s_bench_shell_window_seen = 0;
    s_bench_app_t0 = btron_render_perf_us();
    s_bench_app_made = 0u;
    s_bench_app_over = 0u;
    s_bench_shell_watch = 1;
    s_bench_app_active = 1;
    s_cycle.left = BTRON_PS2_BENCH_LAUNCH_FRAMES;
    s_bench_app_left = BTRON_PS2_BENCH_LAUNCH_FRAMES;
    /* The same three arguments the prompt hands the shell: the terminal's own state and
     * window are only read by the `tv`/`sc` verbs, both of which test for them, so NULL
     * is what `xmb` actually needs. */
    shell_execute_cmd("xmb", ps2_bench_shell_out, NULL, NULL);
    /* The task's own loop spends the budget from inside `dly_tsk()` while the opener is
     * still on the stack, and when it does not -- a call that returned with the window up
     * or a budget nothing consumed -- the frames have to be asked for here.  Bounded twice
     * over: by the budget itself, because the pass that takes `s_bench_app_left` to 0 is
     * the one that injects the Escape and walking past it would enter the frame hook's
     * grace frames and end the whole run, and by the window's presence, because a verb
     * that opened nothing has no frames to wait for. */
    for (guard = 0; guard < (int)BTRON_PS2_BENCH_LAUNCH_FRAMES + 5 && s_bench_app_left &&
                    ps2_bench_wnd_named("Cross Media Bar"); guard++)
        ps2_gui_pass(screen);
    s_bench_shell_watch = 0;
    s_bench_app_active = 0;
    s_bench_app_left = 0u;

    c->lines = s_bench_shell_lines;
    c->not_linked = s_bench_shell_not_linked;
    c->no_surface = s_bench_shell_no_surface;
    c->opened = s_bench_shell_opened;
    c->window_seen = s_bench_shell_window_seen;
    c->frames = s_bench_app_made;
    c->used_after = s_heap_used;
    c->hole_after = ps2_heap_largest_free();

    /* The verdict, in the order that rules the harness out before it rules the product
     * out.  `window_seen` outranks the shell's own line on purpose: the Escape this phase
     * injects lands while `open_xmb_window()` is still on the stack, so its `return s_wnd`
     * hands back the NULL that `xb_destroy()` left there and clu prints "Failed to open
     * window surface" for a window that demonstrably opened and drew.  That row is the
     * harness' doing, and only a window never seen makes it the product's. */
    if (!c->lines) {
        c->why = "the shell printed NOTHING for the verb -- the line never reached clu's table, which is \"picks nothing\" measured";
    } else if (c->window_seen) {
        c->why = "the verb opened the window and its task drew frames off it -- typing xmb runs the app";
    } else if (c->not_linked) {
        c->why = "SKIP for the app -- clu says xmb is not linked into this image, so the verb table and its opener disagree";
    } else if (c->no_surface) {
        c->why = c->hole_before < ask
                     ? "no surface, no frames, and the pool could not serve the ask -- out of memory, which is \"picks nothing\" on the screen"
                     : "no surface and no frames with room in the pool -- the opener itself failed";
    } else if (c->opened) {
        c->why = c->frames
                     ? "the shell says a window opened and passes ran after the call returned"
                     : "the shell says a window is open and NOT one frame ran -- a window with no frame is \"picks nothing\" on the screen";
    } else {
        c->why = "the shell answered with lines none of them a verdict this phase knows -- read the shell> rows above";
    }
}

static void ps2_bench_shell_verb(GDEV *screen)
{
    const size_t ask = ps2_bench_gl_surface_ask();
    const size_t used_boot = s_heap_used;
    const size_t hole_boot = ps2_heap_largest_free();
    ps2_bench_cycle_t c;
    unsigned int leak_kb = 0;       /* kB the first cycle kept: the per-open price */
    size_t smallest_hole = hole_boot;
    int starved_at = 0, refused_at = 0;
    size_t leaked_total = 0;
    int cycle;

    ps2_kprintf("\n[BENCH] g_term verb -- `xmb` at the prompt, the route the user takes, three cycles\n");

    ps2_bench_pool_census("at boot");

    /* Control before the measurement, because an out_fn the console never reached
     * would make the real call read as "the shell printed nothing" -- the very symptom
     * under investigation.  An unknown verb is the same two calls with no app in them,
     * and its answer is two lines this port writes itself. */
    s_bench_shell_lines = 0;
    shell_execute_cmd("no_such_verb", ps2_bench_shell_out, NULL, NULL);
    ps2_kprintf("[BENCH] shell control: an unknown verb printed %d line(s) -- the echo path %s\n",
                s_bench_shell_lines,
                s_bench_shell_lines ? "works, so a later zero is the shell's and not the console's"
                                    : "is DEAD, so every `printed nothing` verdict below is unmeasured");

    /* Three cycles.  One prices the open; two show whether the bytes the first borrowed
     * came back; the third is where a kept plane stops being a number in a row and becomes
     * the refusal the user sees -- the 2026-10-15 run of this ledger OOM'd the 1,024 kB
     * icon plane on its third `xmb` and drew the bar with `texmiss=646`, no icons at all. */
    for (cycle = 1; cycle <= 3; cycle++) {
        ps2_bench_one_cycle(screen, ask, &c);

        ps2_kprintf("[BENCH] shell cycle %d: %d line(s), window present at frame 1 = %s, %u frame(s), top '%s'\n",
                    cycle, c.lines, c.window_seen ? "YES" : "no",
                    (unsigned int)c.frames,
                    get_top_wnd() ? get_top_wnd()->title : "(none)");
        if (c.window_seen)
            ps2_kprintf("[BENCH] shell cycle %d: its client %dx%d visible=%d gl_layer=%d img_valid=%d paint_installed=%d, and the GS holds %d texture plane(s) with %d kB of its VRAM left -- all sampled while it was up\n",
                        cycle, s_bench_shell_geom[0], s_bench_shell_geom[1],
                        s_bench_shell_geom[2], s_bench_shell_geom[3],
                        s_bench_shell_geom[4], s_bench_shell_geom[5],
                        s_bench_shell_geom[6], s_bench_shell_geom[7]);
        ps2_kprintf("[BENCH] shell %d: %s\n", cycle, c.why);
        if (c.window_seen && c.no_surface)
            ps2_kprintf("[BENCH] shell %d: the `Failed to open window surface` above is this harness' Escape, injected while open_xmb_window() was still on the stack, so its `return s_wnd` saw the NULL xb_destroy() left -- not a product failure\n",
                        cycle);
        /* No `%+d`: this console's kprintf has no sign flag, and the first version of this
         * row ate the conversion and shifted every column after it, which printed a 2,950 kB
         * hole as the ask and a 1,088 kB leak as the hole.  The sign is chosen in C. */
        ps2_kprintf("[BENCH] shell cycle %d: pool used %u -> %u kB, largest hole %u -> %u kB, the %u kB ask at the open %s\n",
                    cycle, (unsigned int)(c.used_before / 1024u),
                    (unsigned int)(c.used_after / 1024u),
                    (unsigned int)(c.hole_before / 1024u),
                    (unsigned int)(c.hole_after / 1024u),
                    (unsigned int)(ask / 1024u),
                    c.hole_before >= ask ? "had a hole to serve it" : "had no hole big enough to serve it");
        if (c.used_after > c.used_before) {
            const size_t kept_kb = (c.used_after - c.used_before) / 1024u;

            ps2_kprintf("[BENCH] shell cycle %d: the close kept %u kB -- %s\n", cycle,
                        (unsigned int)kept_kb,
                        leak_kb == 0u
                            ? "that is the leak this ledger exists to price, and the next cycle pays for it"
                            : (kept_kb < leak_kb
                                   ? "a smaller keep than the first cycle, so an ask inside the open was refused: read the [HEAP] rows above this one"
                                   : "the same planes the first cycle kept"));
            if (leak_kb == 0u) leak_kb = (unsigned int)kept_kb;
        } else {
            ps2_kprintf("[BENCH] shell cycle %d: the close gave the pool back every byte the verb took -- used is %u kB again\n",
                        cycle, (unsigned int)(c.used_after / 1024u));
        }
        if (!starved_at && c.hole_before < ask) starved_at = cycle;
        if (!refused_at && leak_kb && c.used_after > c.used_before &&
            (c.used_after - c.used_before) / 1024u < leak_kb) refused_at = cycle;
        if (c.hole_before < smallest_hole) smallest_hole = c.hole_before;
        leaked_total += c.used_after > c.used_before ? c.used_after - c.used_before : 0;
    }

    /* The ledger's own sentence, because the cycle rows above are the shape of the defect
     * and this is its size: bytes kept per open, and which ask first met a hole too small
     * for it.  Note the two asks are not the same number -- a client pixmap the window
     * manager cuts for you (1,500 kB here) and a texture plane the GL backend allocates
     * inside the open (1,024 kB for a 512x512 padded atlas) -- and the one that starves
     * first is the plane, because the plane is asked for when the window's own 2,250 kB is
     * already in hand.  The phases that run after this one spend the same pool, so a `no
     * surface` in them is this row's answer rather than a second defect to chase. */
    ps2_bench_pool_census("after the ledger");
    /* The holder census, run on purpose rather than only after a death: this is the row the operator
     * reads by hand as `heapstat`, and the bench is the only place it can be proven to print.  The
     * `at the first unservable ask` path above fires on an OOM; with the pool sized to the session
     * (11 MB, 2026-10-17) there is no OOM left to trigger it, and an instrument that only speaks
     * when the machine is already broken is not an instrument. */
    ps2_heap_holders("from the bench, after the ledger");
    ps2_kprintf("[BENCH] shell ledger: %d cycle(s) on a boot hole of %u kB -- pool used %u kB at boot, %u kB after the last close, so a cycle that gets its planes keeps %u kB and the ledger kept %u kB%s\n",
                3, (unsigned int)(hole_boot / 1024u), (unsigned int)(used_boot / 1024u),
                (unsigned int)(s_heap_used / 1024u), leak_kb,
                (unsigned int)(leaked_total / 1024u),
                leaked_total ? " -- that is what makes a later `xmb` pick nothing"
                             : " -- every cycle gave the pool back what it took, so no open is starved by the ones before it");
    if (refused_at)
        ps2_kprintf("[BENCH] shell ledger: cycle %d kept less than cycle 1 because an allocation inside its open was refused outright -- the [HEAP] out of memory row above names it, and the window that drew after had no icon plane to bind\n",
                    refused_at);
    ps2_kprintf("[BENCH] shell ledger: the %u kB client ask met its smallest hole of %u kB -- %s; the pool is %s\n",
                (unsigned int)(ask / 1024u), (unsigned int)(smallest_hole / 1024u),
                starved_at ? "an open found no hole big enough for it, which is the refusal the user sees"
                           : "every open could be served, so the client pixmap is not what starves",
                ps2_heap_check() ? ps2_heap_check() : "sound");
}
#endif /* BTRON_PS2_BENCH_APP */

#if BTRON_PS2_BENCH_GS >= 6
/* ── LANE 21 rung 3a: is the picture the same on a 16-bit page? ──────────────────────────
 *
 * The host gates prove the arithmetic (13,400 checks: the packer, the decode, its two error
 * bounds and the in-place expansion's ordering) and `test-eegs-present` proves the 32-bit wire
 * is byte-for-byte what it was.  Neither of them has ever run the CT16 branch of
 * ps2_gs_upload(), because that branch only exists in a guest that dials `page 16`.  This phase
 * is the machine saying so: it plates one rect into the canvas, uploads the whole canvas into a
 * page the allocator describes as PSMCT16, reads that page back through VIF1, and counts the
 * plate -- the same census `gsread` prints, on three page descriptions, on one image, with no
 * hand and no monitor.
 *
 * Four readings, because the rung makes two separate claims, the third of these is the control and
 * the fourth is the shape rung 4 will actually send:
 *
 *   CT32 ................. the shipped path.  Its counts are the baseline the other two are
 *                          quoted against, and if THIS one loses the plate the phase measured
 *                          nothing about rung 3a at all.
 *   CT16, dithered ....... the picture the GS's own DIMX law would give: a flat field splits
 *                          across the 4x4 phases, so `navy=` is expected to fall and `other=`
 *                          to rise by the same amount.  A count that collapses to zero, halves,
 *                          or leaves `sent=` above zero says something else -- the upload's
 *                          geometry, not its palette.
 *   CT16, truncated ...... the same page with the term pinned at its centre.  Here the field is
 *                          one word again, so `navy=` and `red=` must agree with the 32-bit
 *                          census EXACTLY.  That equality is this phase's gate: it is what a
 *                          wrong px_per_qw (4 instead of 8), a stage whose row pitch leaked
 *                          into the stream, or a half-width page fill would break -- a CT16
 *                          upload that got its quadword law wrong cannot draw a 64x64 square at
 *                          320,272 and have it come back as 4,096 pixels in that box.
 *   CT16, truncated BAND   rung 4 will present bands, not pages, so the fourth reading uploads
 *                          only rows 450..599 of a page that already carries the first plate: the
 *                          plate outside the band has to SURVIVE the upload and the one inside it
 *                          has to ARRIVE.  This is the reading that says the chunk loop advances
 *                          its source row, and it earned its place: the first run of this phase,
 *                          on 2026-10-18, failed the three readings above with a page of flawless
 *                          uniform navy and no plate anywhere, because upload_ct16() packed the
 *                          canvas's first five rows once per chunk -- a field of the right colour
 *                          and none of the right ink, which is the failure a colour count alone
 *                          calls success and the reason the gate below sums to a whole page.
 *
 * The plate is drawn into the canvas rather than through the GS: the question is the upload and
 * the read, and a stream would answer it with the rasteriser's behaviour on top.  The rects live
 * below GSREAD_TEXT_ROWS so the console's own glyphs cannot be mistaken for them, and both are
 * repainted back to the background word on the way out, because this phase runs before the pixel
 * phases and a red square left in the canvas would travel into every row after it.
 */
#define BENCH_P16_X0    320u    /* inside the page, below the text band, and a multiple of 8 px: */
#define BENCH_P16_Y0    272u    /* the 16-bit read's own alignment law */
#define BENCH_P16_X1     96u    /* the band reading's second plate: same law, different rows */
#define BENCH_P16_Y1    472u
#define BENCH_P16_SIDE   64u
#define BENCH_P16_PX   ((uint32_t)BENCH_P16_SIDE * (uint32_t)BENCH_P16_SIDE)
#define BENCH_P16_PAGE  ((uint32_t)PS2_SCREEN_WIDTH * (uint32_t)PS2_SCREEN_HEIGHT)
#define BENCH_P16_BAND_Y   450u    /* contains Y1..Y1+SIDE, and does not touch Y0..Y0+SIDE */
#define BENCH_P16_BAND_H   150u

static void bench_p16_plate(uint32_t *cv, uint32_t colour, uint32_t x0, uint32_t y0)
{
    uint32_t y, x;

    for (y = 0u; y < BENCH_P16_SIDE; y++) {
        uint32_t *row = cv + (y0 + y) * (uint32_t)PS2_SCREEN_WIDTH + x0;

        for (x = 0u; x < BENCH_P16_SIDE; x++) row[x] = colour;
    }
}

/* Plate, upload, census.  gsread_page_census() prints its own [GSREAD] rows -- this hands the
 * verdict row the counters they were printed from, so no one has to diff three log lines by eye. */
static void bench_p16_census(uint32_t *buf, uint32_t navy, gsread_page_t *o)
{
    uint32_t *cv = ps2_gs_get_framebuffer();

    ps2_gs_text_clear();
    bench_p16_plate(cv, GSQUAD_RED, BENCH_P16_X0, BENCH_P16_Y0);
    ps2_gs_flush();
    gsread_page_census(buf, navy, o);
}

/* What the allocator ACTUALLY built after a dial.  Printed, and gated on, because the alternative is
 * a phase that asks for PSMCT16, gets refused, and then measures a 32-bit page under two labels that
 * say otherwise -- a green verdict row that means nothing.  `set` is gs_disp_set_psm()'s own answer. */
typedef struct {
    int set;
    uint32_t psm, bytes_pp, px_per_qw;
} bench_p16_pg_t;

static void bench_p16_set(uint32_t psm, uint32_t dither, bench_p16_pg_t *o)
{
    const gs_disp_page_t *pg;

    o->set = gs_disp_set_psm(psm);
    ps2_gs_set_dither(dither);
    ps2_gs_dispfb_store();
    pg = gs_disp_shown();
    o->psm = gs_disp_psm();
    o->bytes_pp = pg != NULL ? pg->bytes_pp : 0u;
    o->px_per_qw = pg != NULL ? pg->px_per_qw : 0u;
}

/* A plate's own box, all four corners: a count alone could be met by pixels that landed somewhere
 * else, and a 16-bit upload that gets its row advance wrong moves the square rather than losing it
 * -- or, as it turned out, loses the ink and keeps the colour, which is what the sums below are for. */
static int bench_p16_at(const int *box, uint32_t x0, uint32_t y0)
{
    return box[0] == (int)x0 && box[1] == (int)y0
        && box[2] == (int)(x0 + BENCH_P16_SIDE - 1u)
        && box[3] == (int)(y0 + BENCH_P16_SIDE - 1u);
}

static int bench_p16_box(const gsread_page_t *c)
{
    return bench_p16_at(c->red_box, BENCH_P16_X0, BENCH_P16_Y0);
}

static void ps2_bench_page16(void)
{
    gsread_page_t c32, c16_d, c16_t, c_band;
    bench_p16_pg_t p32, p16_d, p16_t, p_band, p_out;
    uint32_t *buf = (uint32_t *)Imalloc((size_t)GSREAD_BAND_QWC * 16u);
    uint32_t navy;
    const char *why;
    int red_ok, navy_ok, no_sent, errs_ok, pp_ok, band_ok;

    ps2_kprintf("\n[BENCH] LANE 21 rung 3a -- one plate, three page descriptions plus a band: "
                "CT32 / CT16 dithered / CT16 truncated / CT16 band\n");
    if (!buf) {
        ps2_kprintf("[BENCH] rung 3a: SKIP -- the pool refused the %u B band buffer, so nothing "
                    "was read\n", (unsigned int)(GSREAD_BAND_QWC * 16u));
        return;
    }

    /* The expectation is read out of the canvas, the way gsread reads it: after the clear, before
     * anything is plated into it, so the word is the one the upload will send. */
    ps2_gs_text_clear();
    navy = ps2_gs_get_framebuffer()[(uint32_t)(GSREAD_CTRL_Y + GSREAD_H / 2)
                                    * (uint32_t)PS2_SCREEN_WIDTH
                                    + (uint32_t)(GSREAD_X + GSREAD_W / 2)];

    bench_p16_set(GS_PSM_CT32, 1u, &p32);
    bench_p16_census(buf, navy, &c32);

    bench_p16_set(GS_PSM_CT16, 1u, &p16_d);
    bench_p16_census(buf, navy, &c16_d);

    bench_p16_set(GS_PSM_CT16, 0u, &p16_t);
    bench_p16_census(buf, navy, &c16_t);

    /* The fourth reading, on the truncated page: plate A goes out with a whole flush so the page is
     * navy-plus-A, plate B is written into the canvas and crosses ONLY in the band upload -- so B is
     * the pixel that a chunk loop which does not advance its source cannot deliver, and A is the
     * pixel that one which restarts over the wrong rows would lose. */
    bench_p16_set(GS_PSM_CT16, 0u, &p_band);
    {
        uint32_t *cv = ps2_gs_get_framebuffer();

        ps2_gs_text_clear();
        bench_p16_plate(cv, GSQUAD_RED, BENCH_P16_X0, BENCH_P16_Y0);
        ps2_gs_flush();
        bench_p16_plate(cv, GSQUAD_BLUE, BENCH_P16_X1, BENCH_P16_Y1);
        ps2_gs_upload(0, (int)BENCH_P16_BAND_Y, PS2_SCREEN_WIDTH, (int)BENCH_P16_BAND_H);
        gsread_page_census(buf, navy, &c_band);
    }

    /* Out: the page and the packer as the run found them, and the canvas without this phase's
     * squares in it. */
    bench_p16_set(GS_PSM_CT32, 1u, &p_out);
    ps2_gs_text_clear();
    bench_p16_plate(ps2_gs_get_framebuffer(), navy, BENCH_P16_X0, BENCH_P16_Y0);
    bench_p16_plate(ps2_gs_get_framebuffer(), navy, BENCH_P16_X1, BENCH_P16_Y1);
    ps2_gs_flush();
    Ifree(buf);

    ps2_kprintf("[BENCH] rung 3a pages (asked/got, set=0 means the dial was accepted): "
                "CT32 set=%d psm=0x%x %ubpp %upx/qw | CT16+dither set=%d psm=0x%x %ubpp %upx/qw | "
                "CT16+trunc set=%d psm=0x%x %ubpp %upx/qw | restored set=%d %ubpp\n",
                p32.set, (unsigned int)p32.psm, (unsigned int)p32.bytes_pp,
                (unsigned int)p32.px_per_qw,
                p16_d.set, (unsigned int)p16_d.psm, (unsigned int)p16_d.bytes_pp,
                (unsigned int)p16_d.px_per_qw,
                p16_t.set, (unsigned int)p16_t.psm, (unsigned int)p16_t.bytes_pp,
                (unsigned int)p16_t.px_per_qw, p_out.set, (unsigned int)p_out.bytes_pp);

    ps2_kprintf("[BENCH] rung 3a counts (plate %u px at %u,%u): "
                "CT32 navy=%u red=%u sent=%u other=%u | CT16+dither navy=%u red=%u sent=%u other=%u | "
                "CT16-trunc navy=%u red=%u sent=%u other=%u\n",
                (unsigned int)BENCH_P16_PX, (unsigned int)BENCH_P16_X0, (unsigned int)BENCH_P16_Y0,
                (unsigned int)c32.navy, (unsigned int)c32.red, (unsigned int)c32.sent,
                (unsigned int)c32.below_other,
                (unsigned int)c16_d.navy, (unsigned int)c16_d.red, (unsigned int)c16_d.sent,
                (unsigned int)c16_d.below_other,
                (unsigned int)c16_t.navy, (unsigned int)c16_t.red, (unsigned int)c16_t.sent,
                (unsigned int)c16_t.below_other);
    ps2_kprintf("[BENCH] rung 3a read cost: CT32=%u us  CT16=%u us for the same %dx%d page "
                "(the 16-bit download carries half the bytes)\n",
                (unsigned int)c32.us, (unsigned int)c16_t.us,
                (unsigned int)PS2_SCREEN_WIDTH, (unsigned int)PS2_SCREEN_HEIGHT);
    /* The transfer's own length, because it is the row that says which branch of the read ran:
     * gs_read_image_qwc() is the bytes of the PAGE's format over 16, so a 16-bit census that came
     * back with the 32-bit count did not download a 16-bit page at all -- it re-read whatever was
     * still in VRAM, and every count below would be the previous row's. */
    ps2_kprintf("[BENCH] rung 3a quadwords: CT32=%u  CT16+dither=%u  CT16+trunc=%u "
                "(the 16-bit page halves this by construction)\n",
                (unsigned int)c32.qw, (unsigned int)c16_d.qw, (unsigned int)c16_t.qw);

    /* The fourth reading's own row, and the one rung 4 needs: A crossed on the whole flush and sits
     * OUTSIDE the band, B was written into the canvas after it and crosses ONLY inside the band.
     * So `blue=0` names a source pointer that never advanced (the pre-fix bug: the band repacks the
     * canvas's first rows), and `red=0` or a short sum names one that restarted over wrong rows. */
    ps2_kprintf("[BENCH] rung 3a band (rows %u..%u of the truncated page held %u px at %u,%u; "
                "a second plate went out ONLY in the band at %u,%u): A kept red=%u bbox=%d,%d..%d,%d | "
                "B arrived blue=%u bbox=%d,%d..%d,%d | navy=%u sent=%u other=%u\n",
                (unsigned int)BENCH_P16_BAND_Y,
                (unsigned int)(BENCH_P16_BAND_Y + BENCH_P16_BAND_H - 1u),
                (unsigned int)BENCH_P16_PX, (unsigned int)BENCH_P16_X0, (unsigned int)BENCH_P16_Y0,
                (unsigned int)BENCH_P16_X1, (unsigned int)BENCH_P16_Y1,
                (unsigned int)c_band.red, c_band.red_box[0], c_band.red_box[1],
                c_band.red_box[2], c_band.red_box[3],
                (unsigned int)c_band.blue, c_band.blue_box[0], c_band.blue_box[1],
                c_band.blue_box[2], c_band.blue_box[3],
                (unsigned int)c_band.navy, (unsigned int)c_band.sent,
                (unsigned int)(c_band.text_other + c_band.below_other));

    errs_ok = (c32.err == GS_READ_BUS_OK && c16_d.err == GS_READ_BUS_OK &&
               c16_t.err == GS_READ_BUS_OK && c_band.err == GS_READ_BUS_OK);
    /* The non-vacuity check: the two rows labelled CT16 have to be on a page that is 16 bits and
     * therefore carries eight pixels a quadword, not four -- bytes_pp is the exact predicate both
     * ps2_gs_upload()'s 16-bit branch and ps2_gs_vram_read()'s decode test -- and the download that
     * ran has to have been half as long as the 32-bit one's. */
    pp_ok = (p32.set == 0 && p16_d.set == 0 && p16_t.set == 0 && p_band.set == 0 &&
             p_out.set == 0 && p32.bytes_pp == 4u && p32.px_per_qw == 4u &&
             p16_d.bytes_pp == 2u && p16_d.px_per_qw == 8u &&
             p16_t.bytes_pp == 2u && p16_t.px_per_qw == 8u &&
             p_band.bytes_pp == 2u && p_band.px_per_qw == 8u && p_out.bytes_pp == 4u &&
             c16_d.qw * 2u == c32.qw && c16_t.qw * 2u == c32.qw &&
             c_band.qw * 2u == c32.qw);
    red_ok = (c32.red == (uint32_t)BENCH_P16_PX && c16_d.red == (uint32_t)BENCH_P16_PX &&
              c16_t.red == (uint32_t)BENCH_P16_PX &&
              bench_p16_box(&c32) && bench_p16_box(&c16_d) && bench_p16_box(&c16_t));
    /* The truncated field is the rung's exact claim; the dithered one is allowed to be smaller,
     * and only by the pixels the phase spread into a neighbouring word.  The two sums are the other
     * half of the same claim: a page that is neither background nor plate is a third colour, and a
     * 16-bit upload that packed the wrong canvas rows produces exactly that -- on 2026-10-18 it
     * produced it as a page of clean navy with the plate missing, which a count alone reads as
     * success.  So: navy plus red must be the whole page, on the baseline and on the exact page. */
    navy_ok = (c32.navy == c16_t.navy && c16_d.navy <= c32.navy &&
               c32.navy + c32.red == BENCH_P16_PAGE &&
               c16_t.navy + c16_t.red == BENCH_P16_PAGE);
    no_sent = (c32.sent == 0u && c16_d.sent == 0u && c16_t.sent == 0u && c_band.sent == 0u);
    /* Rung 4 sends bands, so the rung's claim is only landed once a partial upload leaves the rest
     * of the page alone and delivers the rect inside it.  Both plates are full-scale primaries, so
     * on the truncated page each is exactly 4,096 pixels at its own box, and the field plus the two
     * plates is the whole page -- which is what makes this reading a partition and not a hunt. */
    band_ok = (c_band.red == (uint32_t)BENCH_P16_PX &&
               bench_p16_at(c_band.red_box, BENCH_P16_X0, BENCH_P16_Y0) &&
               c_band.blue == (uint32_t)BENCH_P16_PX &&
               bench_p16_at(c_band.blue_box, BENCH_P16_X1, BENCH_P16_Y1) &&
               c_band.navy + 2u * (uint32_t)BENCH_P16_PX == (uint32_t)BENCH_P16_PAGE);

    /* One sentence, in the order a reader needs it: which failure it is, before whether there is
     * one.  Every branch names the rows above, because this phase's answer is read from them. */
    if (!pp_ok) {
        why = "the page this dial asked for is not the page the counts describe -- either "
              "gs_disp_set_psm refused and the labels are on a 32-bit page, or the download came back "
              "at 32-bit length, which means it read the VRAM that was already there.  Read the pages "
              "and quadwords rows above, not the counts";
    } else if (!errs_ok) {
        why = "a census refused or timed out, so no count above describes a page -- read the "
              "FAILED row's stage name, not a colour";
    } else if (!no_sent) {
        why = "pixels the DMA never wrote came back as the sentinel, so a band was cut short and "
              "the counts are a partial page, not a picture";
    } else if (!red_ok) {
        why = "the plate did not come back as 4,096 pixels at its own box on every page -- the "
              "16-bit upload's geometry (px_per_qw, the stage's row pitch, the page's FBW) is what "
              "the three [GSREAD] rows above disagree about";
    } else if (!navy_ok) {
        why = "the 16-bit page is not the canvas: either its truncated field differs from the 32-bit "
              "one -- the packer's law failing, not the dither's, so compare navy= on the last two "
              "counts -- or navy and red do not sum to the whole page on one of them, which means a "
              "third colour is on the page and the plate is not where the canvas put it";
    } else if (!band_ok) {
        why = "a band upload did not leave the page as a band: the plate outside rows 450..599 did "
              "not survive it, or the one written inside it did not arrive, or the field and the two "
              "plates do not sum to the page.  Read the band row's red= and blue= -- blue=0 is the "
              "chunk loop's unadvanced source pointer that this phase found on 2026-10-18, red=0 is "
              "one that re-sent the canvas's first rows over the plate below the band";
    } else {
        why = "the same plate at the same box and the same background field, at 32 bits, at 16 "
              "with the dither and at 16 without it, and a second plate that crossed only in a band "
              "while the first one stayed put outside it: the page halves and the picture does not "
              "move";
    }

    ps2_kprintf("[BENCH] rung 3a verdict: %s -- %s\n",
                (pp_ok && errs_ok && red_ok && navy_ok && no_sent && band_ok) ? "MEASURED"
                                                                             : "NOT MEASURED",
                why);
}
#endif /* BTRON_PS2_BENCH_GS >= 6 */

#if BTRON_PS2_BENCH_GS >= 7
/* ── LANE 21 rung 4: does the rotation put the right picture on the right page? ───────────
 *
 * Rung 3a proved a 16-bit page can carry this canvas.  Rung 4 puts TWO of them under the
 * compositor, and its claim is not a colour count -- it is an ORDERING: the page nobody is
 * scanning is plated, the debt a page accumulates while hidden travels with it, and the page
 * becomes visible at a frame boundary.  Nothing about that is visible in a single-page census,
 * and none of it is reachable from a host gate, because it is a bus turn on a page index.
 *
 * So this phase drives the same three calls ps2_gui_pass() does -- ps2_rot_step(), the band
 * upload, ps2_gs_flip() -- with a canvas it paints itself, and censuses the page the flip
 * claims is now being scanned.  Four plate colours, four boxes that do not touch, and one
 * reading that would fail if the merge were removed:
 *
 *   1. CONTROL, at PSMCT32: gs_disp_set_double(1) must be REFUSED.  This is the reservation's
 *      own law -- two 32-bit pages do not fit under the display -- and a phase that skipped it
 *      would happily run the rest on a configuration the shipped port says is impossible.
 *   2. Dial to CT16, truncate, then accept double.  The page that was being scanned keeps its
 *      pixels (RED, from the pre-dial flush); the newly writeable page is empty VRAM.
 *   3. Plate BLUE into the canvas and present once.  BLUE's damage goes to the page being shown
 *      as DEBT, and the hidden page is plated with the whole canvas -- RED and BLUE both -- then
 *      flipped.  The census of the shown page now carries a box that was only ever in the canvas
 *      once and only ever uploaded once.
 *   4. Present again with ORANGE: the page that was just shown owes BLUE and gets it as a
 *      catch-up, so its census shows RED + BLUE + ORANGE -- the rotation working, because the
 *      alternative is a page with a hole in it.  A third present with VIOLET puts all four
 *      colours on the other page, and the sums say each page is a complete picture rather than
 *      the union of whichever bands happened to land there.
 *
 * What this phase CANNOT see is the PCRTC.  gsread reads gs_disp_shown()'s base, and DISPFB is
 * built from the same page, so a census proves the state machine and the VRAM agree -- it does
 * not prove the CRT followed them, and only the operator's monitor can say that.  That is what
 * Ctrl+P is for, and the alignment row below is printed as its own labelled line rather than
 * folded into the verdict for the same reason: whether PCSX2 raises CSR.VSYNC at all in this
 * configuration is a fact about the emulator, and a rung that timed out waiting for it would
 * still pass every count above.
 */
#define BENCH_ROT_ORG_X  448u    /* orange's box: 8-px aligned, clear of RED at 320,272 and BLUE at 96,472 */
#define BENCH_ROT_VIO_X  672u    /* violet's box, same row as orange's -- the fourth colour needs a fourth place */
#define BENCH_ROT_Y1     272u
#define BENCH_ROT_Y2     472u

/* The present this phase makes: the band loop ps2_gui_pass() runs after its rotation step,
 * without the compositor's render half -- there is no desktop here, and the rung's claim is the
 * page, not the pixels on it.  Times the upload the same way so the row below can say what a
 * banded catch-up costs against a whole canvas. */
static uint32_t bench_rot_present(const ps2_bandset_t *set)
{
    uint32_t us = 0u;

    for (unsigned i = 0u; i < set->n; i++) {
        const ps2_band_t *b = &set->b[i];
        const uint32_t t0 = btron_render_perf_us();

        ps2_gs_upload(b->x0, b->top, b->x1 - b->x0, b->end - b->top);
        us += btron_render_perf_us() - t0;
    }
    return us;
}

/* One pass of the rotation and its present, shared by the three readings below so the phase
 * cannot drift from the order ps2_gui_pass() uses: step, upload, flip.  `owed` is the merge's
 * own answer and `bands` the set the pass ended up plating -- the two columns that say, from
 * the driver's side, whether a catch-up happened and how big it was. */
static uint32_t bench_rot_pass(ps2_bandset_t *set, uint32_t *up_us, int *owed, unsigned *bands)
{
    uint32_t wait;

    *owed = ps2_rot_step(set);
    *bands = set->n;
    *up_us = bench_rot_present(set);
    wait = ps2_gs_flip(1);
    return wait;
}

static void ps2_bench_flip(void)
{
    gsread_page_t pre, s1, s2, s3;
    bench_p16_pg_t p_ctl, p_16, p_out;
    ps2_bandset_t set;
    uint32_t *buf, navy, up1 = 0u, up2 = 0u, up3 = 0u, w1, w2, w3;
    unsigned b1 = 0u, b2 = 0u, b3 = 0u;
    int ctl_refused, dbl_ok, hidden_ok, flip1_ok, seed_ok, rot_ok, sums_ok, no_sent, errs_ok;
    int owed1 = 0, owed2 = 0, owed3 = 0;
    const char *why;

    ps2_kprintf("\n[BENCH] LANE 21 rung 4 -- two pages, one rotation: control / first flip / "
                "seeded catch-up / damage catch-up\n");
    buf = (uint32_t *)Imalloc((size_t)GSREAD_BAND_QWC * 16u);
    if (!buf) {
        ps2_kprintf("[BENCH] rung 4: SKIP -- the pool refused the %u B band buffer, so no page "
                    "was read\n", (unsigned int)(GSREAD_BAND_QWC * 16u));
        return;
    }

    /* 1. The control, on the shipped format: a second page has no room, and the dial has to say
     * so.  gs_disp_is_double() is read back afterwards because a refusal that still set the flag
     * would make every reading below a lie about a configuration that cannot exist. */
    bench_p16_set(GS_PSM_CT32, 1u, &p_ctl);
    ctl_refused = (gs_disp_set_double(1u) != 0 && !gs_disp_is_double());
    if (!ctl_refused)
        ps2_kprintf("[BENCH] rung 4 CONTROL FAILED: PSMCT32 accepted a second page.  The "
                    "reservation is not what ps2_gs_vram.h says it is.\n");

    /* 2. The format that fits, with the term pinned so the counts are exact, and a canvas that
     * holds RED before any of this exists as a two-page configuration. */
    bench_p16_set(GS_PSM_CT16, 0u, &p_16);
    ps2_gs_text_clear();
    bench_p16_plate(ps2_gs_get_framebuffer(), GSQUAD_RED, BENCH_P16_X0, BENCH_P16_Y0);
    ps2_gs_flush();
    navy = ps2_gs_get_framebuffer()[(uint32_t)(GSREAD_CTRL_Y + GSREAD_H / 2)
                                    * (uint32_t)PS2_SCREEN_WIDTH
                                    + (uint32_t)(GSREAD_X + GSREAD_W / 2)];

    /* Entering the two-page model through the dial the desktop uses, so what is measured is the
     * path a Ctrl+P takes -- and its refusal, if any, is the row an operator would see. */
    ps2_page_double_apply(1u);
    dbl_ok = gs_disp_is_double() == 1u &&
             gs_disp_drawn()->index != gs_disp_shown()->index &&
             p_16.bytes_pp == 2u && p_16.px_per_qw == 8u;

    /* The page being scanned, as it is before any rotation: RED from the flush above, and no
     * BLUE anywhere -- the plate that goes out under the model below has never been sent here. */
    gsread_page_census(buf, navy, &pre);

    /* 3. First rotation pass: BLUE into the canvas, damage = BLUE's box.  The drawn page owes the
     * whole canvas from the dial, so this pass uploads the canvas -- and its flip is the first the
     * port has ever made. */
    bench_p16_plate(ps2_gs_get_framebuffer(), GSQUAD_BLUE, BENCH_P16_X1, BENCH_P16_Y1);
    set = (ps2_bandset_t){ { { 0, 0, 0, 0 } }, 0, 0 };
    ps2_bandset_add(&set, (int)BENCH_P16_X1, (int)(BENCH_P16_X1 + BENCH_P16_SIDE),
                    (int)BENCH_P16_Y1, (int)(BENCH_P16_Y1 + BENCH_P16_SIDE));
    w1 = bench_rot_pass(&set, &up1, &owed1, &b1);
    gsread_page_census(buf, navy, &s1);

    /* 4. Second pass: ORANGE.  The page being drawn is the one that was scanned at the dial, and
     * its debt is still the seeded whole canvas, so this too is a full plate -- the reading that
     * says BOTH pages end up holding the picture, not only the one the model started on. */
    bench_p16_plate(ps2_gs_get_framebuffer(), GSQUAD_ORANGE, BENCH_ROT_ORG_X, BENCH_ROT_Y2);
    set = (ps2_bandset_t){ { { 0, 0, 0, 0 } }, 0, 0 };
    ps2_bandset_add(&set, (int)BENCH_ROT_ORG_X, (int)(BENCH_ROT_ORG_X + BENCH_P16_SIDE),
                    (int)BENCH_ROT_Y2, (int)(BENCH_ROT_Y2 + BENCH_P16_SIDE));
    w2 = bench_rot_pass(&set, &up2, &owed2, &b2);
    gsread_page_census(buf, navy, &s2);

    /* 5. Third pass: VIOLET, and this is the one that pays for the rung.  The page drawn now was
     * shown when ORANGE was damaged, so the ONLY way ORANGE reaches it is ps2_rot_owe() having
     * folded pass 4's one-rect damage into its debt: this pass' own damage is VIOLET alone, and a
     * rotation that forgot the merge leaves this page with RED, BLUE and VIOLET and a hole where
     * the orange box is -- which on a screen is a frame that loses a window every other flip.
     *
     * The uploaded band count is the same claim from the other side: with the merge this pass
     * plates two separate rects, without it one, and the row prints it. */
    bench_p16_plate(ps2_gs_get_framebuffer(), GSQUAD_VIOLET, BENCH_ROT_VIO_X, BENCH_ROT_Y1);
    set = (ps2_bandset_t){ { { 0, 0, 0, 0 } }, 0, 0 };
    ps2_bandset_add(&set, (int)BENCH_ROT_VIO_X, (int)(BENCH_ROT_VIO_X + BENCH_P16_SIDE),
                    (int)BENCH_ROT_Y1, (int)(BENCH_ROT_Y1 + BENCH_P16_SIDE));
    w3 = bench_rot_pass(&set, &up3, &owed3, &b3);
    gsread_page_census(buf, navy, &s3);

    /* Out: single-buffered, both pages cleared of debt, the format and the packer as the run
     * found them, and the canvas repainted navy -- the same four restorations rung 3a makes, and
     * for the same reason: the phases after this one read this page. */
    ps2_page_double_apply(0u);
    ps2_rot_clear();
    bench_p16_set(GS_PSM_CT32, 1u, &p_out);
    ps2_gs_text_clear();
    ps2_gs_flush();
    Ifree(buf);

    /* The three readings' counts, one row each, in the order the script makes them. */
    ps2_kprintf("[BENCH] rung 4 shown page before any flip: red=%u blue=%u orange=%u violet=%u "
                "navy=%u other=%u sent=%u\n",
                (unsigned int)pre.red, (unsigned int)pre.blue, (unsigned int)pre.orange,
                (unsigned int)pre.violet, (unsigned int)pre.navy,
                (unsigned int)(pre.text_other + pre.below_other), (unsigned int)pre.sent);
    ps2_kprintf("[BENCH] rung 4 flip 1 (owed=%d bands=%u up=%u us wait=%u us): red=%u blue=%u "
                "orange=%u violet=%u navy=%u other=%u sent=%u\n",
                owed1, (unsigned int)b1, (unsigned int)up1, (unsigned int)w1,
                (unsigned int)s1.red, (unsigned int)s1.blue, (unsigned int)s1.orange,
                (unsigned int)s1.violet, (unsigned int)s1.navy,
                (unsigned int)(s1.text_other + s1.below_other), (unsigned int)s1.sent);
    ps2_kprintf("[BENCH] rung 4 flip 2 (owed=%d bands=%u up=%u us wait=%u us): red=%u blue=%u orange=%u "
                "violet=%u navy=%u other=%u sent=%u\n",
                owed2, (unsigned int)b2, (unsigned int)up2, (unsigned int)w2,
                (unsigned int)s2.red, (unsigned int)s2.blue, (unsigned int)s2.orange,
                (unsigned int)s2.violet, (unsigned int)s2.navy,
                (unsigned int)(s2.text_other + s2.below_other), (unsigned int)s2.sent);
    ps2_kprintf("[BENCH] rung 4 flip 3 (owed=%d bands=%u up=%u us wait=%u us): red=%u blue=%u orange=%u "
                "violet=%u navy=%u other=%u sent=%u\n",
                owed3, (unsigned int)b3, (unsigned int)up3, (unsigned int)w3,
                (unsigned int)s3.red, (unsigned int)s3.blue, (unsigned int)s3.orange,
                (unsigned int)s3.violet, (unsigned int)s3.navy,
                (unsigned int)(s3.text_other + s3.below_other), (unsigned int)s3.sent);

    /* The flip's hardware half, printed as its own labelled row and NOT folded into the verdict:
     * a wait that ran to its bound is a statement about this emulator's GS, and the counts above
     * would all still pass with the display never having changed page.  `flips=` is the driver's
     * own count, so this row also says the phase made the three it claims to have made. */
    ps2_kprintf("[BENCH] rung 4 ALIGNMENT: flips=%u unaligned=%u longest wait=%u us  "
                "(a 16,667 us frame is the ceiling a wait should land under)\n",
                (unsigned int)ps2_gs_flip_count(), (unsigned int)ps2_gs_flip_unaligned(),
                (unsigned int)ps2_gs_flip_wait_max_us());

    errs_ok = (pre.err == GS_READ_BUS_OK && s1.err == GS_READ_BUS_OK &&
               s2.err == GS_READ_BUS_OK && s3.err == GS_READ_BUS_OK);
    no_sent = (pre.sent == 0u && s1.sent == 0u && s2.sent == 0u && s3.sent == 0u);
    /* The scanned page before the first flip holds RED and nothing else of this phase's: a BLUE
     * already there would mean the writer went to the page being shown, which is the bug rung 4
     * exists to remove, and would make the two readings below say nothing. */
    hidden_ok = (pre.red == (uint32_t)BENCH_P16_PX && bench_p16_box(&pre) &&
                 pre.blue == 0u && pre.orange == 0u && pre.violet == 0u);
    /* After flip 1 the shown page is the one that was plated: both plates, at their own boxes,
     * and the field is the rest of the page.  This is the flip moving the read target, which is
     * the same page DISPFB was re-pointed at. */
    flip1_ok = (s1.red == (uint32_t)BENCH_P16_PX && bench_p16_at(s1.red_box, BENCH_P16_X0, BENCH_P16_Y0) &&
                s1.blue == (uint32_t)BENCH_P16_PX && bench_p16_at(s1.blue_box, BENCH_P16_X1, BENCH_P16_Y1) &&
                s1.orange == 0u && s1.violet == 0u);
    /* Pass 2's page is the one the dial found already being scanned: its debt was the seeded whole
     * canvas, so after this flip BOTH pages hold the picture.  That is not the rung's interesting
     * claim and it is still a gate -- a rotation that only ever completes one page is a desktop that
     * shows a hole on alternate frames. */
    seed_ok = (s2.red == (uint32_t)BENCH_P16_PX && s2.blue == (uint32_t)BENCH_P16_PX &&
               s2.orange == (uint32_t)BENCH_P16_PX &&
               bench_p16_at(s2.orange_box, BENCH_ROT_ORG_X, BENCH_ROT_Y2) &&
               s2.violet == 0u);
    /* THE rotation's gate, and the one a no-merge build fails.  ORANGE was damaged in pass 2, when
     * this page was being SHOWN; pass 3's own damage was VIOLET alone.  So orange=4096 here can only
     * be ps2_rot_owe() having folded it into this page's debt, and orange=0 -- with violet there and
     * correct -- is the merge missing.  `bands=2` on the flip-3 row is the same fact from the bus:
     * two separate rects went out where one rect was asked for. */
    rot_ok = (s3.red == (uint32_t)BENCH_P16_PX && s3.blue == (uint32_t)BENCH_P16_PX &&
              s3.orange == (uint32_t)BENCH_P16_PX && s3.violet == (uint32_t)BENCH_P16_PX &&
              bench_p16_at(s3.violet_box, BENCH_ROT_VIO_X, BENCH_ROT_Y1) &&
              b3 == 2u && owed3 == 1);
    /* Each page is a complete picture: field plus the plates it holds, to the pixel.  A page with
     * a third colour on it is a plate that landed at the wrong address, and a short sum is a band
     * that did not cover what it claimed to. */
    sums_ok = (pre.navy + pre.red == (uint32_t)BENCH_P16_PAGE) &&
              (s1.navy + s1.red + s1.blue == (uint32_t)BENCH_P16_PAGE) &&
              (s2.navy + s2.red + s2.blue + s2.orange == (uint32_t)BENCH_P16_PAGE) &&
              (s3.navy + s3.red + s3.blue + s3.orange + s3.violet == (uint32_t)BENCH_P16_PAGE);

    if (!ctl_refused) {
        why = "PSMCT32 accepted a second page, so the reservation this whole lane is built on is "
              "not the one ps2_gs_vram.h describes -- read its gs_disp_reserve_bytes() before any "
              "count below";
    } else if (!dbl_ok) {
        why = "the two-page model did not come up on a 16-bit page, or did not separate the write "
              "target from the scanned one: the [PAGE] row above ps2_page_double_apply() names "
              "which";
    } else if (!errs_ok) {
        why = "a census refused or timed out, so the counts are a partial page -- read the "
              "FAILED row's stage name, not a colour";
    } else if (!no_sent) {
        why = "the read's own sentinel came back, so a band was cut short and the sums below "
              "describe a page nobody plated";
    } else if (!hidden_ok) {
        why = "the page being scanned was written by the rotation, or does not hold the plate it "
              "already had -- a writer still on the scanned page is the flicker's root cause and "
              "makes every reading after it meaningless";
    } else if (!flip1_ok) {
        why = "after a flip the census did not name the page that was just plated: "
              "gs_disp_flip_commit() and ps2_gs_dispfb_store() disagree with each other, or the "
              "plated page is not the one the counts describe";
    } else if (!seed_ok) {
        why = "the page the dial found being scanned never got the whole canvas: read the flip-2 "
              "row's blue= and orange= -- one of the two is the seeded debt not being paid";
    } else if (!rot_ok) {
        why = "the catch-up did not arrive: this pass' damage was one rect and the page it drew "
              "still owed another, so orange=0 on the flip-3 row (with violet present) is "
              "ps2_rot_owe() not running -- a hidden page with a hole in it, and the flicker back "
              "as a stale frame every other flip";
    } else if (!sums_ok) {
        why = "a page is not field-plus-plates: something on it is a colour this phase never "
              "sent, which is a plate at the wrong address rather than a missing one";
    } else {
        why = "the hidden page took the picture, the flip moved what is scanned to it, and the "
              "damage each page owed while hidden arrived on it one pass later -- four colours, "
              "two pages, exact sums";
    }
    ps2_kprintf("[BENCH] rung 4 verdict: %s -- %s\n",
                (ctl_refused && dbl_ok && errs_ok && no_sent && hidden_ok && flip1_ok &&
                 seed_ok && rot_ok && sums_ok) ? "MEASURED" : "NOT MEASURED", why);
}
#endif /* BTRON_PS2_BENCH_GS >= 7 */


/* The price a float op costs the image is not the price the `[FPU]` chain row prints.
 * That chain runs inside ps2_builtins.c -- the one TU built -mhard-float -- so its
 * add.s is mtc1/op/mfc1 and no call.  Every other TU here is -msoft-float, where the
 * same source expression is a `jal __addsf3`.  This is that chain: four ops an
 * iteration, the same four the self-test times, from a soft-float caller, with the
 * operands behind a volatile load so nothing folds and no propagation empties the loop.
 * The difference between the two rows is exactly what a batched kernel can reclaim,
 * because a kernel is the arithmetic with one call instead of one call per op. */
static volatile float s_fpcall_op[4];
#if BTRON_PS2_FP_COUNT
extern uint32_t btron_fp_calls[7];   /* the same census the [XMBT] rows read */
#endif

static uint32_t ps2_bench_fpcall_chain(int use_cop1, uint32_t *cls3)
{
    const int saved = btron_fp_on;
    uint32_t c0, t;
    float acc = 1.0f;
    int j;
#if BTRON_PS2_FP_COUNT
    uint32_t calls0[7];
    int i;

    for (i = 0; i < 7; i++) calls0[i] = btron_fp_calls[i];
#endif

    s_fpcall_op[0] = 1.0000001f;   /* add */
    s_fpcall_op[1] = 0.9999999f;   /* mul */
    s_fpcall_op[2] = 1.0000002f;   /* div */
    s_fpcall_op[3] = 2.0f;         /* compare */

    btron_fp_on = use_cop1 ? 1 : 0;
    c0 = ps2_count_read();
    for (j = 0; j < 4000; j++) {
        acc = acc + s_fpcall_op[0];             /* __addsf3 */
        acc = acc * s_fpcall_op[1];             /* __mulsf3 */
        acc = acc / s_fpcall_op[2];             /* __divsf3 */
        if (acc < s_fpcall_op[3])               /* __ltsf2 */
            acc = s_fpcall_op[0];               /* a load: the compare is consumed */
    }
    t = (uint32_t)(ps2_count_read() - c0);
    btron_fp_on = saved;
    if (acc == 0.0f)
        acc = 1.0f;                             /* keep the chain live */
#if BTRON_PS2_FP_COUNT
    cls3[0] = btron_fp_calls[0] - calls0[0];    /* add + mul: 8000 if nothing folded */
    cls3[1] = btron_fp_calls[1] - calls0[1];    /* div: 4000 */
    cls3[2] = btron_fp_calls[2] - calls0[2];    /* compare: 4000 */
#else
    cls3[0] = cls3[1] = cls3[2] = 0u;
#endif
    return t;
}

/* The census' conversion column is two machines counted as one.  int -> float is
 * `cvt.s.w`, which COP1 answers in the time of an add; float -> int has no instruction
 * on this CPU at all, so it always runs the bit engine -- and a stage's 76,008
 * conversions say nothing until each direction's own price is known.  Two loops, one
 * class each: the operand comes from a volatile load and the answer goes to a volatile
 * store, so the body is the call plus two memory ops and no arithmetic can be folded
 * out of it or smuggled into it.  Counted calls, when the census is compiled in, are
 * the proof that 4000 of the right libcall really ran. */
static volatile float   s_cvt_fsrc;
static volatile int32_t s_cvt_isrc;
static volatile int32_t s_cvt_isink;
static volatile float   s_cvt_fsink;

#define BTRON_CVT_OPS 4000u

static uint32_t ps2_bench_trunc_loop(uint32_t *calls)
{
    uint32_t c0;
    unsigned long j;
#if BTRON_PS2_FP_COUNT
    const uint32_t before = btron_fp_calls[FP_SF_CVT];
#endif

    s_cvt_fsrc = 1234.5f;
    c0 = ps2_count_read();
    for (j = 0; j < BTRON_CVT_OPS; j++)
        s_cvt_isink = (int32_t)s_cvt_fsrc;
    c0 = (uint32_t)(ps2_count_read() - c0);
#if BTRON_PS2_FP_COUNT
    *calls = btron_fp_calls[FP_SF_CVT] - before;
#else
    (void)calls;
#endif
    return c0;
}

static uint32_t ps2_bench_promote_loop(uint32_t *calls)
{
    uint32_t c0;
    unsigned long j;
#if BTRON_PS2_FP_COUNT
    const uint32_t before = btron_fp_calls[FP_SF_CVTI];
#endif

    s_cvt_isrc = 1234;
    c0 = ps2_count_read();
    for (j = 0; j < BTRON_CVT_OPS; j++)
        s_cvt_fsink = (float)s_cvt_isrc;
    c0 = (uint32_t)(ps2_count_read() - c0);
#if BTRON_PS2_FP_COUNT
    *calls = btron_fp_calls[FP_SF_CVTI] - before;
#else
    (void)calls;
#endif
    return c0;
}

/* ── MSX phase: are C-BIOS and the cartridges really readable out of the volume
 *      this ELF carries?
 *
 * The XMB Games band lists MSX_ROM_CATALOGUE and B-MSX loads the row it lands on,
 * both through src/clu/vfs.c against the /SYS volume in .data.  There is no folder
 * on this target, so "the ROM is in the image" is a claim about three separate
 * reads, and the first one is exactly what broke: tronmsx.c initialised its BIOS
 * directory to the host folder, bios_load() only falls back to the volume drawer
 * when handed nothing, and the window answered "C-BIOS が読み込めません" while the
 * bytes sat in the image.  A row per read, in the order the app needs them:
 *   rows=   the drawer lists, and what the band will show,
 *   cbios=  msx_init() got a BIOS, i.e. the machine can boot at all,
 *   rom=    the body reads whole and the core accepts it (0 = MSX_OK).
 * The `$` header column is the first six bytes of the cartridge, which is where an
 * MSX ROM says its name; printable ones only, so a binary never garbles the log. */
static void ps2_bench_msx(void)
{
#ifdef BTRON_PS2_MSX
    static VfsEntry entries[16];
    uint8_t *rom = (uint8_t *)Imalloc((size_t)MSX_ROM_SIZE + 1u);
    char path[VFS_MAX_PATH];
    const char *first = "";
    uint32_t got = 0u;
    int rows, rc, lrc = MSX_ERR_SIZE;

    if (!rom) {
        ps2_kprintf("[MSX] failed to allocate rom buffer -- SKIP\n");
        return;
    }

    rows = vfs_list_dir(MSX_ROM_CATALOGUE, entries, 16);
    if (rows > 0) first = entries[0].name;
    ps2_kprintf("[MSX] catalogue %s: rows=%d first='%s'\n",
                MSX_ROM_CATALOGUE, rows, first);

    rc = msx_init(NULL);
    ps2_kprintf("[MSX] cbios %s: msx_init -> %d%s\n", MSX_BIOS_DEFAULT_DIR, rc,
                rc == MSX_OK ? "" : " (MSX_ERR_BIOS: the main body did not read)");

    if (rows > 0 && rc == MSX_OK) {
        size_t n = 0u;
        int i;

        vfs_child_path(path, sizeof(path), MSX_ROM_CATALOGUE,
                       entries[0].name, entries[0].fid);
        if (vfs_read_blob(path, rom, (size_t)MSX_ROM_SIZE + 1u, &n) == 0) {
            char hdr[8];
            got = (uint32_t)n;
            for (i = 0; i < 6; i++)
                hdr[i] = (rom[i] >= 0x20 && rom[i] < 0x7f) ? (char)rom[i] : '?';
            hdr[6] = '\0';
            lrc = msx_load_rom(rom, got, 0);
            ps2_kprintf("[MSX] rom %s: %u bytes hdr='%s' -> msx_load_rom %d\n",
                        entries[0].name, (unsigned)got, hdr, lrc);
        } else {
            ps2_kprintf("[MSX] rom %s: body unreadable at %s\n",
                        entries[0].name, path);
        }
    }

    Ifree(rom);
    msx_shutdown();     /* leave no machine instance for the phases below */
#else
    /* Parked, not absent: the row prints either way so a log line can never be read as
     * "the phase forgot to run".  PS2_MSX=0 keeps the tronMSX core and its 10.6 MB of
     * statics out of a 32 MB machine whose .bss already sums past the ps2.ld ceiling --
     * see the dial's own note in the Makefile and M17 in doc/txt/PS2.txt. */
    ps2_kprintf("[MSX] parked: this image links no tronMSX (PS2_MSX=0); the Games row "
                "opens xmb.c's weak stub, so rows/cbios/rom are SKIP, not FAIL\n");
#endif
}

static void ps2_bench_run(GDEV *screen)
{
    static const struct { const char *tag; int dx, dy; } mv[] = {
        { "h1",   1,   0 }, { "h4",   4,  0 }, { "h16",  16,  0 },
        { "h64", 64,   0 }, { "h127", 127, 0 },
        { "v1",   0,   1 }, { "v4",   0,  4 }, { "v16",   0, 16 },
        { "v64",  0,  64 }, { "v127", 0, 127 },
    };

    /* Ahead of everything that costs emulator seconds: this phase answers a storage
     * question in milliseconds, and the phases after it have been known to run out
     * of harness time before printing anything. */
    ps2_bench_msx();

    /* First phase, ahead of the present table, because every number after it is a
     * float: the two single-precision engines are run over the same fixed vector of
     * bit patterns, and if they disagree or the FPU is not answering, the timings
     * below belong to the wrong engine.  The two rows are the proof and the price --
     * worst=0 ulp with disagree=0 is the correctness half, and the cycles-per-op row
     * is what a 24 s XMB frame is made of. */
    {
        uint32_t ops = 0u, bad = 0u, hw_t = 0u, soft_t = 0u;
        const uint32_t worst_ulp = btron_fp_selftest(&ops, &bad, &hw_t, &soft_t);
        unsigned int c;
        ps2_kprintf("\n[BENCH] float engines -- COP1 against the bit engine, same vector\n");
        ps2_kprintf("[FPU] checked=%u disagree=%u worst=%u ulp  path in use: %s\n",
                    (unsigned int)ops, (unsigned int)bad, (unsigned int)worst_ulp,
                    btron_fp_on ? "Emotion Engine COP1" : "bit engine");
        ps2_kprintf("[FPU] 16000-op chain: COP1 %u cycles/op  bit engine %u cycles/op\n",
                    (unsigned int)(hw_t ? hw_t / 16000u : 0u),
                    (unsigned int)(soft_t ? soft_t / 16000u : 0u));
        /* The same 16,000 ops asked through the ABI the rest of the image uses.
         * `counted` is the chain read back through the libcalls' own census
         * (FP_COUNT=1), which is what makes this row an instrument rather than an
         * expression: 16000 says the compiled loop really holds four calls an
         * iteration and the cycles per op is therefore the cycles per call.  Any
         * smaller number says the arithmetic was partly folded away and the price is
         * too high; 0 says the census is not in this build. */
        {
            uint32_t ch, cs, nh[3], ns[3];
            ch = ps2_bench_fpcall_chain(1, nh);
            cs = ps2_bench_fpcall_chain(0, ns);
            ps2_kprintf("[FPU] 16000-op soft-float caller: COP1 %u cycles/op  bit engine %u cycles/op\n",
                        (unsigned int)(ch / 16000u), (unsigned int)(cs / 16000u));
            ps2_kprintf("[FPU] counted calls: COP1 arith=%u div=%u cmp=%u  bit %u/%u/%u of 16000\n",
                        (unsigned int)nh[0], (unsigned int)nh[1], (unsigned int)nh[2],
                        (unsigned int)ns[0], (unsigned int)ns[1], (unsigned int)ns[2]);
            ps2_kprintf("[FPU] per-op call overhead: COP1 %u  bit engine %u cycles\n",
                        (unsigned int)(ch / 16000u + (ch % 16000u ? 1u : 0u)
                                       - (hw_t ? hw_t / 16000u : 0u)),
                        (unsigned int)(cs / 16000u + (cs % 16000u ? 1u : 0u)
                                       - (soft_t ? soft_t / 16000u : 0u)));
        }
        /* The two conversion directions, priced apart, because the census' conversion
         * column is both of them added: int -> float goes to COP1 when the FPU is on and
         * float -> int never can, so one number in that column is worth something and the
         * other is worth about ten times it. */
        {
            const int saved = btron_fp_on;
            uint32_t tsi = 0u, tis = 0u, tis_sf = 0u, n_si = 0u, n_is = 0u, n_is2 = 0u;
            btron_fp_on = 1;
            tsi = ps2_bench_trunc_loop(&n_si);
            tis = ps2_bench_promote_loop(&n_is);
            btron_fp_on = 0;
            tis_sf = ps2_bench_promote_loop(&n_is2);
            btron_fp_on = saved;
            ps2_kprintf("[FPU] conversion price: float->int %u cycles/op (%u calls)  int->float %u on COP1 / %u bit engine (%u/%u)\n",
                        (unsigned int)(tsi / BTRON_CVT_OPS), (unsigned int)n_si,
                        (unsigned int)(tis / BTRON_CVT_OPS),
                        (unsigned int)(tis_sf / BTRON_CVT_OPS),
                        (unsigned int)n_is, (unsigned int)n_is2);
        }
        /* The same disagreement split by op and by argument class, because `bad=1910`
         * on its own does not say whether the arithmetic is wrong (clean column, every
         * pixel affected) or only the non-arithmetic edge law (special column, NaN
         * payload and flags, which the rasterizer never touches).  Rows are printed
         * whether or not they are clean: `clean=400 bad=0` is the evidence that the
         * mtc1/op/mfc1 sequence works at all, and its absence is what left the last
         * run's answer unverifiable. */
        for (c = 0u; c < BTRON_FP_CLS_N; c++)
            ps2_kprintf("[FPU] op %-6s clean=%u bad=%u  special=%u bad=%u\n",
                        btron_fp_cls_name[c],
                        (unsigned int)btron_fp_cls[c][0], (unsigned int)btron_fp_cls[c][1],
                        (unsigned int)btron_fp_cls[c][2], (unsigned int)btron_fp_cls[c][3]);
        /* And the same disagreements sorted by cause, because the causes are not one
         * defect: daz/ftz say a unit that flushes subnormals, nonfinite says its law for
         * infinities and NaNs (the EE has no unordered compare, so a NaN pair is one of
         * these by construction), inexact says a conversion that has no exact answer.
         * `other` is the bucket with no explanation, and the one the gate reads; the row
         * after it names the op that filled it first. */
        ps2_kprintf("[FPU] shape daz=%u ftz=%u nonfinite=%u inexact=%u other=%u\n",
                    (unsigned int)btron_fp_shape[BTRON_FP_SHAPE_DAZ],
                    (unsigned int)btron_fp_shape[BTRON_FP_SHAPE_FTZ],
                    (unsigned int)btron_fp_shape[BTRON_FP_SHAPE_NONFINITE],
                    (unsigned int)btron_fp_shape[BTRON_FP_SHAPE_INEXACT],
                    (unsigned int)btron_fp_shape[BTRON_FP_SHAPE_OTHER]);
        if (btron_fp_shape[BTRON_FP_SHAPE_OTHER])
            ps2_kprintf("[FPU] unexplained first=%s\n",
                        btron_fp_other_where ? btron_fp_other_where : "(unnamed)");
        /* And how far off, for the clean arithmetic pairs.  This is the row that decides
         * what an unexplained disagreement *is*: one step is the same number rounded the
         * other way -- invisible in 8-bit pixels, and a property of the unit rather than
         * of the port -- while a wide or sign-flipped gap is a different number and
         * cannot be used for anything.  The bit engine's own claim to be right is the host
         * differential in verify/tests/test_ps2_softfloat.c (15.2M checks, bit-exact), so
         * a gap here is measured against arithmetic that has been proven, not assumed. */
        ps2_kprintf("[FPU] clean gap");
        for (c = 0u; c < BTRON_FP_GAP_N; c++)
            ps2_kprintf(" %s=%u", btron_fp_gap_name[c], (unsigned int)btron_fp_gap[c]);
        ps2_kprintf("\n");
        /* The bytes behind the counts, for as long as the counts are unexplained: with a
         * printed pair the question `which engine is wrong` stops being a matter of
         * opinion, because the exact answer for a single-precision pair is computable on
         * any host.  Nothing prints here while every disagreement has a cause. */
        for (c = 0u; c < btron_fp_dump_n; c++)
            ps2_kprintf("[FPU] pair %s a=0x%08x b=0x%08x cop1=0x%08x bits=0x%08x gap=%u\n",
                        btron_fp_cls_name[btron_fp_dump[c][0]],
                        (unsigned int)btron_fp_dump[c][1], (unsigned int)btron_fp_dump[c][2],
                        (unsigned int)btron_fp_dump[c][3], (unsigned int)btron_fp_dump[c][4],
                        (unsigned int)btron_fp_dump[c][5]);
    }

#if BTRON_PS2_BENCH_APP
    /* Ahead of every pixel phase, on the boot-time pool, for the reason in the phase's
     * own note: the phases below spend 4 MB of an 11 MB pool that a booted desktop has
     * not spent yet, and a GL window's client pixmap asks 1.5 MB of it.  Run after the
     * click phase and the app answers "no memory" for the harness' own footprint
     * instead of the product's. */
    ps2_bench_shell_verb(screen);
#endif

    /* First of the pixel phases, and before the region table rather than after it:
     * its answer decides which of the two lanes row 9's open half is worth taking,
     * and a row that decides is a row that has to be in the log even if the run gets
     * cut off downstream. */
    ps2_bench_ram_ops(screen);

    ps2_kprintf("\n[BENCH] present cost by region -- no input involved, 16 reps each\n");
    ps2_bench_region(screen, "full", 0, 0, PS2_SCREEN_WIDTH, PS2_SCREEN_HEIGHT, 1, 2u);
    ps2_bench_region(screen, "band16", 0, 300, PS2_SCREEN_WIDTH, 316, 0, 16u);
    ps2_bench_region(screen, "band32", 0, 300, PS2_SCREEN_WIDTH, 332, 0, 16u);
    ps2_bench_region(screen, "band64", 0, 300, PS2_SCREEN_WIDTH, 364, 0, 16u);
    /* The shapes a rect-limited present would actually use: the 16x16 sprite's own
     * box, and the swept boxes of a 16-px move in each direction.  Read the band
     * rows and these against each other and the answer to "should the present be a
     * rect" is in the table rather than in an argument. */
    ps2_bench_region(screen, "box16", 400, 300, 416, 316, 0, 16u);
    ps2_bench_region(screen, "hbox32", 400, 300, 432, 316, 0, 16u);
    ps2_bench_region(screen, "vbox32", 400, 300, 416, 332, 0, 16u);
    ps2_bench_region(screen, "hbox80", 400, 300, 480, 316, 0, 16u);
    ps2_bench_region(screen, "vbox80", 400, 300, 416, 380, 0, 16u);

    /* 127 counts is the largest one report can carry, and at the emulator's own
     * 8 counts per host pixel it is what one fast host pixel is worth -- so the
     * h127/v127 pair is the fast-sweep half of the hand protocol, driven here
     * without a hand. */
    ps2_kprintf("\n[BENCH] loop pass cost by direction -- 40 alternating passes each\n");
    for (unsigned i = 0; i < sizeof(mv) / sizeof(mv[0]); i++)
        ps2_bench_move(screen, mv[i].tag, mv[i].dx, mv[i].dy, 40u);

    /* Own phase, not an appendix to the move table: the rows above are about pixels
     * per pass and this one is about whether a pass dispatches anything at all. */
    ps2_bench_click(screen);

    /* Separate from the click phase on purpose: that one asks whether a button byte
     * reaches the deskbar, this one asks whether the sprite reaches the screen.  A
     * session can have one working and the other not. */
    ps2_bench_cursor(screen);

    /* The sentinel scripts/ps2_bench.sh waits for before shutting the emulator
     * down: a guest that has nothing to shut the VM with, and a host that cannot
     * tell a finished table from one still being printed, otherwise means a fixed
     * sleep and a log read in the middle of a row. */
#if BTRON_PS2_BENCH_APP
    /* `BENCH_APP=1 make ps2-bench`.  Opt-in because the phase owns the frame loop
     * while it lasts, and today the app it opens never reaches its own exit: the
     * 2026-10-09 run got as far as `[GL] VirtIO-GPU ... backend init: 952x564` and
     * `[GL] xmb: no memory for the icon atlas` and then stalled, so the script's
     * sentinel never printed and the run had to be killed at its timeout.  The
     * dispatch's default backend is virtio (gl_dispatch.c:18) and this target has no
     * virtio GPU, so the phase is a harness waiting for the GL downstack to be
     * selected correctly -- which is exactly what it is here to measure. */
    /* First, because a frame of this app costs tens of seconds of emulator time: the
     * rows that answer "can the launcher actually run XMB" have to be in the log before
     * the 60-frame phase below is cut off by the harness' timeout. */
    ps2_bench_launch_xmb_by_key(screen);
    ps2_bench_app(BTRON_PS2_BENCH_APP_FRAMES);
#endif

    /* Runs last of the phases, so nothing downstream inherits the windows it opens;
     * see the phase's own note on open_xmb_window()'s static handle. */
    ps2_bench_launch(screen);

    /* Before the sentinel, never after it: the harness stops the emulator on that row,
     * so a row downstream of it is a row that may never be flushed. */
    ps2_bench_kbd_cadence();
    /* The two planes' budgets as this run left them.  A session prints the same two rows
     * on exit, but the headless run never exits -- the harness stops it at the sentinel --
     * and this is the run whose frames a budget gate is quoted against, so the receipt has
     * to be here too.  Cumulative since boot, like every other plane counter, and once
     * only: the frame hook prints it ahead of a sentinel it had to raise itself. */
    if (!s_bench_sentinel) ps2_plane_run_print();

    /* The pointer's two-speed pair, printed here for one reason and not for its
     * verdict: this is the headless run's only chance to show that a count off the
     * seam really does reach the splitter, since the harness stops the emulator at
     * the sentinel and the session's own exit row is therefore never printed on
     * this path.  What it says is normally "no pair yet", and that is the correct
     * answer -- a synthetic stream has one speed per phase, and the test this row
     * exists for is two speeds of the same slide, which only a hand performs.  The
     * arithmetic itself is gated on the host, 46 checks and two opposite-answer
     * controls, by `make test-ps2-pointer`. */
    if (!s_bench_sentinel) ps2_ptr_stroke_row();

    /* Not twice: the app phase's frame hook prints this row when the app outlives its
     * budget, and a second copy would tell the harness the table ended in two places. */
    if (!s_bench_sentinel)
        ps2_kprintf("\n[BENCH] run complete\n");
}
#endif /* BTRON_PS2_BENCH */

/* ── Stage 2: Authentic B-System Graphical Workbench Session ────── */

/* The two timers stay at file scope because the session loop and ps2_task_dly()
 * must advance one pair of deadlines, not two: a task-driven frame and a
 * loop-driven frame have to age the same clock.  The event slot deliberately does
 * not -- see ps2_gui_pass(). */
static uint32_t s_pass_panel_due, s_pass_full_due;
static GDEV *s_gui_screen;

/* The body depth, defined with the task layer below.  Declared here because the cycle
 * profiler needs it to tell the one kind of pass that is an app frame -- the one a body
 * asked for from inside itself -- from the session loop's, of which it makes thousands
 * a second and none of which owes a row. */
static int s_task_driving;

/* One desktop pass: poll the bus, spend what the pointer owes, dispatch what the
 * queue holds, and present the rectangles that changed.  It is a function rather
 * than the body of the session loop because a task-driven app (xmb.c, quake_app.c,
 * glgears.c) has to be able to ask for one frame from inside its own task body --
 * on a target with no threads, dly_tsk() is the only thing between its invalidation
 * and the screen.  Nothing in here is app-aware: the app marks its window damaged,
 * this presents it, and the timers below are shared rather than duplicated so a
 * task-driven frame and a loop-driven frame age the same clock. */
static void ps2_gui_pass(GDEV *screen)
{
    ps2_bandset_t set = { { { 0, 0, 0, 0 } }, 0, 0 };
    EVT ev;
    int force_full = 0;
    uint32_t in_t0, ui_t0;   /* the two worker planes' own charge boundaries */

    ps2_cycle_begin(s_task_driving);
    /* The INPUT plane's worker starts here: the two bus polls, the repeat that the poll
     * can now owe, and the pointer spending the distance the plane collected.  Charged
     * before the shell and before the dispatch, so what the budget sees is input work and
     * not the compositor's. */
    in_t0 = ps2_count_read();
    ps2_pad_poll();
    ps2_usb_poll();
    /* The edge report has now been turned into events, so what is still held is known,
     * and the key that owes a repeat is added to the queue before the dispatch below
     * reads it -- the same pass that could have seen the press sees the repeat, and a
     * release heard by this poll disarms before it can fire one more time. */
    ps2_kbd_repeat_tick();
    /* After the poll and before the dispatch: the reports the drain just
     * turned into queued distance become one frame's movement here, and the
     * move event this raises is what puts the cursor's rows in the set below,
     * so a pass that spends pointer distance is also a pass that repaints it.
     * That is what makes "px per pass" the speed the hand sees. */
    ps2_ptr_service();
    ps2_plane_charge(&s_plane_input, in_t0, ASYNC_INPUT_BUDGET_US);
    /* The presentation plane's run is everything from here to the stream that carries
     * the band: the dispatch, the app's paint callback, the band decision and the
     * present.  Taken here rather than at the top of the pass so that the two planes'
     * charges partition the pass instead of overlapping. */
    ui_t0 = ps2_count_read();
    ps2_shell_poll();
    ps2_cycle_mark();

#if BTRON_PS2_BENCH_APP
    /* The app phase's frame budget, spent here rather than in the phase itself
     * because this is the only place a task-driven pass is known to have happened.
     * Injected before the dispatch below, so the key is consumed by the pass that
     * owes it and the body returns without one more frame. */
    if (s_bench_app_active) {
        s_bench_app_made++;
        /* The shell phase's one honest witness: the window either was on the table while
         * its task drew, or it never was.  Sampling the first three passes rather than
         * only the first covers a pass the compositor takes before the opener has put the
         * window up, and taking values rather than the pointer is deliberate -- the WND
         * does not outlive the close this phase's own Escape causes, and reading it after
         * the call returned would print geometry for a window that is already gone. */
        if (s_bench_shell_watch && !s_bench_shell_window_seen && s_bench_app_made <= 3u) {
            const WND *w = ps2_bench_wnd_named("Cross Media Bar");

            if (w) {
                s_bench_shell_window_seen = 1;
                s_bench_shell_geom[0] = (int)(w->client.right - w->client.left);
                s_bench_shell_geom[1] = (int)(w->client.bottom - w->client.top);
                s_bench_shell_geom[2] = (int)w->visible;
                s_bench_shell_geom[3] = (int)w->gl_layer;
                s_bench_shell_geom[4] = (int)w->img_valid;
                s_bench_shell_geom[5] = w->paint ? 1 : 0;
                /* The GS side of the same two planes, because it creeps the way the pool
                 * does: a freed EE plane's address is not handed back to the same slot by a
                 * fragmented first-fit pool, so the next open registers a *new* pointer for
                 * the same geometry and the registry takes a fresh VRAM span and abandons
                 * the old one.  Two entries and a steady cursor is the fixed shape. */
                s_bench_shell_geom[6] = (int)gs_tex_count();
                s_bench_shell_geom[7] = (int)(gs_tex_vram_left() / 1024u);
            }
        }
        /* The task census, taken from inside a pass the app's own body asked for.  `ps` needs
         * a prompt the desktop is not holding, so without this row the PS2's btron_list_tasks()
         * would only ever have been exercised by a host build of the same formatter.  What the
         * state column means here is decided by where the pass is: RUN is the body on the stack
         * and not in its hold, SLEEP would mean the hold is marked around the wrong span, NEW
         * at frame 3 would mean the counted passes are the session loop's and not the body's,
         * and no row at all would mean cre_tsk() never got a slot. */
        if (s_bench_app_made == 3u) {
            char trow[64];

            /* Walked to the end rather than to PS2_MAX_TASKS: that macro belongs to the task
             * layer further down this file, and a second literal for the table's size here
             * would be a number that can disagree with the real one.  The formatter answers 0
             * past the last row and BTRON_TSK_NOWALK when the core has no table, so both
             * endings stop this loop on their own. */
            for (int ti = 0; ; ti++) {
                W trc = btron_task_row_text((W)ti, trow, (W)sizeof(trow));
                if (trc == 1) {
                    ps2_kprintf("[BENCH] task: %s\n", trow);
                    continue;
                }
                if (trc == BTRON_TSK_NOWALK) ps2_kprintf("[BENCH] task: this core answers no census\n");
                break;
            }
        }
        /* A heartbeat, because a phase that prints nothing for minutes is ambiguous
         * between a renderer that is slow and one that is stuck, and the elapsed
         * column is the number that tells them apart.  Sparse on purpose: the console
         * write itself costs bus time, and this phase measures a frame. */
        if (s_bench_app_made <= 3u || (s_bench_app_made % 10u) == 0u)
            /* Folded clock, not ps2_us_since(): a raw Count delta is 32 bits at
             * 147.18 MHz and wraps every 29.1 s, which made the first version of this
             * row print 14446, 24884, 6107 ms for three equally spaced frames.  The
             * fold is only exact while something polls between wraps, and one frame of
             * this app outlives two of them, so the column is monotonic but short: the
             * wall cadence is the emulator's own prefix on these lines, ~78 s a frame. */
            ps2_kprintf("[BENCH] xmb frame %u at %u ms folded\n",
                        (unsigned int)s_bench_app_made,
                        (unsigned int)((btron_render_perf_us() - s_bench_app_t0) / 1000u));
#if BTRON_PS2_PAINT_WORKER
        /* LANE 22's own heartbeat, on the same cadence as the frame one and for the same reason:
         * the numbers that decide #165 are a RATIO over many passes -- asks against dups, defers
         * against forced -- and a single closing row cannot say whether the policy was deciding all
         * along or only at the end.  `take()` clears, so each row is the window between two of them.
         * Gated on s_pw_on, because a row that reads `off` in the middle of a measured run would be
         * read as the policy refusing to engage rather than as the dial being down. */
        if (s_pw_on && (s_bench_app_made <= 3u || (s_bench_app_made % 10u) == 0u)) {
            ps2_pw_stats_t pw;
            ps2_pw_stats_take(&pw);
            ps2_pw_row(&pw, 1);
        }
#endif
        /* Twice, because a surface that is black at frame 3 and full at frame 30 is a
         * renderer that needed its textures baked, and one black both times never drew. */
        if (s_bench_app_made == 3u) { ps2_bench_xmb_surface("frame 3 "); ps2_bench_face_stats("frame 3 "); ps2_bench_gs_planes(); }
        if (s_bench_app_made == 30u) { ps2_bench_xmb_surface("frame 30"); ps2_bench_face_stats("frame 30"); }
        /* LANE 12's census runs on three consecutive passes.  It samples the canvas the
         * previous pass finished, so the delta between two of these rows is exactly one
         * XMB frame, and the third row is the control: a wave that moves the picture and
         * a clock that moves a row once a second give different answers over three.
         * Whether the 28 ms rebuild and the 4.7 ms transfer are work or redundancy is
         * the number the frame's second-biggest term turns on. */
        if (s_bench_app_made >= 4u && s_bench_app_made <= 6u)
            ps2_bench_canvas_census(screen, (unsigned int)s_bench_app_made);
#if BTRON_PS2_BENCH_GS >= 2
        if (s_bench_app_made == 3u)
            ps2_bench_gs_live_census(s_bench_app_made);
#endif
    }
    if (s_bench_app_left && --s_bench_app_left == 0) {
        const ER injected = ps2_inject_key(BTRON_KEY_ESCAPE, 1);
        ps2_inject_key(BTRON_KEY_ESCAPE, 0);
        /* The two numbers that decide whether the app's own quit key can close it:
         * what the event queue answered, and the depth xb_go_back() is standing at.
         * r!=0 means the key died in the queue; r==0 with depth>1 means it was taken
         * as a go-back, which is not a close. */
        ps2_kprintf("[BENCH] xmb: budget spent, escape r=%d depth=%d top=%s made=%u\n",
                    (int)injected, xmb_depth(), get_top_wnd() ? "up" : "closed",
                    (unsigned int)s_bench_app_made);
        s_bench_app_over = 1u;
    } else if (s_bench_app_active && s_bench_app_over &&
               ++s_bench_app_over > 2u && !s_bench_sentinel) {
        /* The frame budget ends the run when the app does not end itself.  Without
         * this the closing sentinel is only reachable through the app's exit, and the
         * 2026-10-10 PCSX2 run of a 3-frame ask was still printing frame 40 when the
         * harness' own timeout killed it with nothing scored.  Two grace frames let an
         * in-flight close land first. */
        s_bench_sentinel = 1;
        ps2_kprintf("[BENCH] xmb: %u frames past the budget, the app took no Escape -- "
                    "ending the run from the frame hook\n",
                    (unsigned int)(s_bench_app_over - 1u));
        ps2_bench_kbd_cadence();      /* the last stamped period is this app's own */
        ps2_plane_run_print();        /* ... and so are the two planes' budgets */
        ps2_ptr_stroke_row();         /* ... and the pointer's pair, which is the row
                                       * a killed run would otherwise never show */
#if BTRON_PS2_PAINT_WORKER
        /* The whole session's decision table, not one window's: the harness kills the VM seconds
         * after this sentinel, so a run that wanted `paintw stat` never gets to it, and the
         * per-job lines are the only place a reader can see WHAT the policy priced the app's frame
         * at -- which is the number that says whether a `defer` was a real refusal or an estimate
         * of zero. */
        {
            ps2_pw_stats_t pw;
            ps2_pw_stats_take(&pw);
            ps2_pw_row(&pw, 1);
            ps2_pw_jobs();
            ps2_kprintf("[PW] pass plan: planned=%u deferred=%u (%u%% of passes refused the app)\n",
                        (unsigned int)s_pw_passes, (unsigned int)s_pw_defer_passes,
                        (unsigned int)(s_pw_passes ? (100u * s_pw_defer_passes) / s_pw_passes : 0u));
        }
#endif
        ps2_kprintf("\n[BENCH] run complete\n");
    }
#endif
    ps2_cycle_mark();

    /* Dispatch queued BTRON events through unified workbench dispatcher */
    while (get_evt(&ev, 0) == E_OK) {
#if BTRON_HID_TRACE
        if (ev.type == EV_KEY_DOWN) {
            /* top=0 is a desktop with no window to take the key; a nonzero
             * top whose handler is the gterm one means the key arrived and
             * the app itself declined it. */
            WND *tk = get_top_wnd();
            ps2_kprintf("[WM] k=%x top=%x h=%x\n", ev.key,
                        (unsigned)(uintptr_t)tk,
                        tk ? (unsigned)(uintptr_t)tk->event_handler : 0u);
        }
        if (ev.type == EV_BUT_DOWN || ev.type == EV_BUT_UP) {
            /* The same three answers for a click: whether the WM has a window at
             * all, whose handler will be asked, and which edge this is.  A [EVT]
             * row with no [WM] row behind it is the queue losing the edge; a [WM]
             * row with top=0 is a click that hit the desktop, not a window. */
            WND *tk = get_top_wnd();
            ps2_kprintf("[WM] b=%d d=%d top=%x h=%x xy=%d,%d\n", ev.button,
                        ev.type == EV_BUT_DOWN,
                        (unsigned)(uintptr_t)tk,
                        tk ? (unsigned)(uintptr_t)tk->event_handler : 0u,
                        ev.pos.x, ev.pos.y);
        }
#endif
        workbench_process_event(screen, &ev);
        /* Only a move leaves the desktop's own art alone: the sprite is the
         * whole change, and it is where the band says it is.  Any other event
         * can restack a window, hand a key to an app that paints without
         * invalidating, or open a menu -- all of which are cheaper to get right
         * by repainting everything than to enumerate. */
        if (ev.type != EV_MOUSE_MOVE) {
            force_full = 1;
            ps2_cycle_why(PS2_CYCLE_WHY_EVENT);
        }
    }

#if BTRON_HID_TRACE
    ps2_log_usb_state('G');
#endif
    ps2_cycle_mark();

    ps2_bands_for_pass(&set);

    /* Two timers, because two different things go stale on their own.
     *
     * The panel's clock changes with no event and no damage at all, and it owns
     * 28 rows, so it is owed those rows on its own period -- the plane table's
     * panel_refresh_us, not a literal in front of the tick constant.  Whole canvas
     * is owed to anything a banded present could have got wrong -- an app that
     * paints without invalidating, a hover highlight inside a window -- and it is
     * convergence, not animation, so the table's full_convergence_us of it is
     * invisible.
     *
     * Paced on the clock and not on a pass count because a banded pass is cheap
     * enough that the loop now makes very many of them a second, and every
     * sixtieth pass would be a repaint several times over.  With no Count to
     * read, both nets are skipped: the present is then driven purely by damage,
     * which is correct for anything that invalidates, and the only loss is a
     * clock that stops advancing. */
    if (s_timebase_ok) {
        const uint32_t now = ps2_count_read();
        const uint32_t tpu = btron_ee_ticks_per_us();
        if ((uint32_t)(now - s_pass_panel_due) >=
                (uint32_t)(btron_planes()->panel_refresh_us * tpu)) {
            ps2_bandset_add(&set, 0, PS2_SCREEN_WIDTH, 0, PS2_PANEL_ROWS);
            s_pass_panel_due = now;
        }
        if ((uint32_t)(now - s_pass_full_due) >=
                (uint32_t)(btron_planes()->full_convergence_us * tpu)) {
            force_full = 1;
            ps2_cycle_why(PS2_CYCLE_WHY_TIMER);
            s_pass_full_due = now;
        }
    }

    /* An overlay -- the global menu, a tracker dropdown, the IME candidate
     * window -- is drawn by the whole-canvas composite only, and the IME's is
     * drawn at the caret rather than at a rect anyone tracks.  With any of them
     * up there is no band worth arguing about, so paint the canvas.  Each
     * condition is a bit of its own so the row says which one it was. */
    uint32_t ov = 0u;
    if (global_menu_is_open())    ov |= PS2_CYCLE_WHY_GMENU;
    if (tracker_is_menu_open())   ov |= PS2_CYCLE_WHY_TRACKER;
    if (wnd_mgr_is_interacting()) ov |= PS2_CYCLE_WHY_WM;
    if (tip_get_state() == TIP_STATE_CONVERTING ||
        tip_get_state() == TIP_STATE_CANDIDATE_SELECT) ov |= PS2_CYCLE_WHY_TIP;

    /* What the app and the pointer actually dirtied, recorded before the decision
     * below can replace it with the canvas. */
    ps2_cycle_why(ov);
    if (set.overflow) ps2_cycle_why(PS2_CYCLE_WHY_OVERFLOW);
    ps2_cycle_damage(&set);

    if (force_full || set.overflow || ov) ps2_bandset_full(&set);

    /* LANE 21 rung 4: with two pages this pass's set is not the whole ask.  The page about
     * to be drawn owes whatever the canvas did while it was hidden, and the page about to be
     * hidden is owed what this pass did -- in that order, because the debt that arrives here
     * is a catch-up and must not be re-owed (see the law above s_page_owed).
     *
     * Read every pass rather than latched at session entry, which is what makes Ctrl+P an
     * A/B at all: the present that follows the keystroke is already the flipping one. */
    const int dbl = (int)gs_disp_is_double();
    uint32_t flip_wait_us = 0u;
    if (dbl && ps2_rot_step(&set)) ps2_cycle_why(PS2_CYCLE_WHY_ROTATE);
    ps2_cycle_merged(&set);

    if (!set.n) {
        ps2_cycle_stamp(0u, 0u, 0u, 0u, 0, 0);   /* nothing for the GS to be told */
        ps2_delay_cycles(1000);                  /* ... and the tail is timed with it */
        return;
    }

    ps2_cycle_mark();

#if BTRON_PS2_PAINT_WORKER
    /* #165, before the first band: the pass's own remaining slice is `ui_period_us` minus what the
     * dispatch and the band decision already took, and the app's next frame is admitted against it.
     * `s_pw_upload_us` is the bus column of the pass before this one -- the part of a present the
     * policy cannot schedule away, because the canvas that has just been composited has to go out. */
    ps2_pw_plan(&set, ps2_pw_us(ui_t0, ps2_count_read()));
#endif

    {
        uint32_t r_us, s_us, u_us, px;
        uint32_t lat = 0;
        int lat_ok = 0;
        const int dom = s_paint_owed ? s_paint_dom : -1;

        ps2_paint_bands(screen, &set, &r_us, &s_us, &u_us, &px);

#if BTRON_PS2_PAINT_WORKER
        {
            /* Fold the bus column first: it is the estimate the NEXT pass's slice is cut from, and
             * a 3:1 EWMA is the same shape the worker uses for a window's price.  Then drop the
             * latch, which is a fact about this band set and dies with it -- leaving it raised
             * would make the next pass paint nothing. */
            if (s_pw_on)
                s_pw_upload_us = s_pw_upload_us ? ((3u * s_pw_upload_us + u_us + 2u) >> 2) : u_us;
            ps2_pw_freeze(0);
            s_pw_layer = NULL;
        }
#endif

        /* The page this pass just plated is complete, so it may be shown.  Before the latency
         * stamp below, because that stamp's whole question is "how old is the pixel the eye
         * gets", and a present that waits for the frame boundary is older by exactly what it
         * waited -- a number worth SEEING in `lat:` rather than hiding in a footnote.  It is
         * not in r_us/s_us/u_us: those three are this pass's work, and the wait is the
         * display's clock, which is why it comes back separately. */
        if (dbl) flip_wait_us = ps2_gs_flip(1);

        if (s_paint_owed && s_paint_age_f0) {
            /* Frames, not cycles: an OHCI frame is a millisecond and it is the
             * same clock the reports are stamped with, so this row and the
             * `loop:` row cannot drift apart.  Read after the flush, so the
             * age is the one the eye gets: queueing plus this paint. */
            lat = (ps2_usb_frame_number() - s_paint_age_f0) & 0xFFFFu;
            lat_ok = 1;
            s_paint_age_f0 = 0;
            s_paint_owed = 0;
        }
        ps2_cycle_mark();
        ps2_paint_note(px, set.n, ps2_band_is_full(&set.b[0]), dom,
                       r_us, s_us, u_us, lat, lat_ok);
        if (ps2_band_is_full(&set.b[0])) s_pass_full_due = ps2_count_read();
        ps2_cycle_mark();
        ps2_cycle_stamp(r_us, s_us, u_us, px, (int)set.n, ps2_band_is_full(&set.b[0]));
    }
    /* This pass presented, so it is one run of the presentation plane: charged against
     * ASYNC_UI_BUDGET_US for the budget, and against the table's ui_period_us for the
     * deadline.  A pass that found no damage returns above and is charged nowhere -- it
     * owed nothing, so it cannot be late, and counting idle passes as misses would make a
     * still desktop read as a broken one.
     *
     * The flip's wait is moved OUT of that charge, and it is the one term in this port with a
     * right to be: the plane's 4 ms budget is a question about how much bus time the
     * compositor takes, and sitting still until the frame boundary takes none -- the GS is
     * scanning, not being spoken to.  Advancing the start boundary by the wait is what says so
     * without inventing a second budget: `us = (now - t0)/tpu`, so a t0 pushed forward by
     * wait*tpu measures the work and not the clock.  With rung 4 off, flip_wait_us is 0 and
     * this is the same arithmetic it was.  What the wait costs is not lost, it is on the
     * `lat:` row above, which is where a viewer would look for it. */
    ps2_plane_charge(&s_plane_ui,
                     ui_t0 + flip_wait_us * btron_ee_ticks_per_us(),
                     ASYNC_UI_BUDGET_US);
    ps2_plane_ui_deadline();
    s_shown_x = s_mouse_x;
    s_shown_y = s_mouse_y;

    ps2_delay_cycles(1000);
}



/* ── Kernel: µITRON task layer, pass-driven ───────────────────────── */

/* There is no second stack here, so a task is not a thread: `sta_tsk` runs the body
 * on the caller's stack and the body's `dly_tsk()` is what performs the frame.  That
 * shape is not a shortcut -- an app's task body only ever invalidates its window (see
 * xb_task() in xmb.c:2833) and it is the compositor that paints, so the one thing a
 * body can want from the kernel is "present what I just marked, and give me the next
 * slice of bus time", which is exactly one desktop pass.  Two consequences are part
 * of the contract rather than bugs:
 *
 *   - `stksz` is ignored.  The body runs on the loop's own stack, so a task cannot
 *     be given one; the four apps that use this (xmb.c, glgears.c, quake_app.c,
 *     lilcu64_demo.c) are all reached from the desktop's event dispatch, which is
 *     already on that stack.
 *   - the loop is driven by whichever body is running, so a second `sta_tsk` nests
 *     inside the first: the inner app's passes happen during the outer body's
 *     `dly_tsk`, and both apps still get presented, at half the frame rate.  The row
 *     below prints so the nesting is visible from the console rather than inferred
 *     from a slow cursor.
 *
 * Nothing above this line in this file has to know any of it: `ps2_gui_pass` takes
 * the screen from `s_gui_screen` precisely so a body can ask for one without the
 * session loop's locals. */
#define PS2_MAX_TASKS 4

typedef struct {
    T_CTSK config;
    int    active;
    /* What the census can prove about this body.  There is no thread here to be running or
     * blocked, so the states are stated in the only terms this kernel has -- a slot on the
     * caller's stack, and whether the body is presently inside the part of `dly_tsk()` that
     * gives bus time back.
     *
     *   started   sta_tsk() has entered the body.  False only for a created, never started one.
     *   holding   inside ps2_task_hold(): no pass runs, nothing is polled, the body is waiting
     *             for its next slice.  This is the honest equivalent of a μITRON DLY wait, and
     *             the only wait this core has -- `slp_tsk()` is a no-op.
     *
     * A body that came back has its slot cleared rather than being marked DEAD, because that
     * is what core_posix.c does (`task_wrapper()` drops `active` on return) and its census has
     * never emitted DEAD either; a μITRON DTS task is one awaiting `del_tsk()`, and this port
     * has no such caller.  It is also the only shape that works here: `sta_tsk()` is synchronous,
     * so the pool is what bounds how many apps a session can open, and the measured bench run
     * put four sequential XMB opens through four slots and left nothing for a fifth.
     *
     * Everything else about a started body is RUN -- it is on the stack, either executing or
     * driving the pass it just asked for.  Two rows can read RUN at once, and that is not a
     * contradiction on a machine whose bodies nest: it says both are live and unblocked, which
     * is what the header's law for RUN means.  It does not claim either holds a CPU. */
    int    started;
    int    holding;
} PS2_TASK;

static PS2_TASK  s_ps2_tasks[PS2_MAX_TASKS];
/* Which body is innermost.  Bodies nest (the contract above), so the one that is waiting is
 * always the last one started, and a depth-indexed stack answers "whose wait is this" with no
 * thread-local storage and no lock.  Slot [0] is the session loop and holds no task id. */
static ID        s_ps2_task_at[PS2_MAX_TASKS + 1];
static uint32_t  s_task_ms;          /* ms since the desktop session, folded for Count wrap */
static uint32_t  s_task_us_rem;      /* µs not yet worth a millisecond, always < 1000 */
static uint32_t  s_task_count_last;
static int       s_task_clock_running;
/* `s_task_driving` -- body depth, 0 while the session loop drives -- is declared up at
 * ps2_gui_pass(), which is the only reader that comes before this point in the file. */

/* The body that owns the call being made, or NULL when the session loop makes it.  A census
 * row is only ever taken from the loop or from a pass inside one, and a hold from the loop
 * belongs to no task, so the NULL is the answer, not a failure. */
static PS2_TASK* ps2_task_self(void)
{
    ID id;
    if (s_task_driving <= 0 || s_task_driving > PS2_MAX_TASKS) return NULL;
    id = s_ps2_task_at[s_task_driving];
    if (id <= 0 || id > PS2_MAX_TASKS) return NULL;
    return &s_ps2_tasks[id - 1];
}

ID cre_tsk(const T_CTSK *pk_ctsk)
{
    if (!pk_ctsk || !pk_ctsk->task) return E_PAR;
    for (int i = 0; i < PS2_MAX_TASKS; i++) {
        if (s_ps2_tasks[i].active) continue;
        s_ps2_tasks[i].config   = *pk_ctsk;
        s_ps2_tasks[i].active   = 1;
        s_ps2_tasks[i].started  = 0;
        s_ps2_tasks[i].holding  = 0;
        return i + 1;
    }
    return E_LIMIT;
}

ER sta_tsk(ID tskid, VW exinf)
{
    if (tskid <= 0 || tskid > PS2_MAX_TASKS) return E_ID;
    PS2_TASK *t = &s_ps2_tasks[tskid - 1];
    if (!t->active || !t->config.task) return E_NOEXS;

    if (s_task_driving) {
        ps2_kprintf("[KERN] task %d started while task %d drives the frame loop; "
                    "the new app runs nested, at half rate\n",
                    (int)tskid, s_task_driving);
    }
    if (exinf != 0) t->config.exinf = exinf;
    /* Returns when the body returns, which for these apps is when their window
     * closes; the desktop loop resumes from the pass that started it. */
    s_ps2_task_at[s_task_driving + 1] = tskid;
    t->started = 1;
    s_task_driving++;
    t->config.task(t->config.exinf);
    s_task_driving--;
    s_ps2_task_at[s_task_driving + 1] = 0;
    /* The body came back between two of its own frames, so it holds nothing and is finished
     * with.  Give the slot straight back: here a started task runs to its end inside this
     * call, so a slot left `active` past the return is a slot no later app open can get, and
     * the pool -- not the machine -- becomes the limit on how many windows a session can run.
     * core_posix.c reclaims in exactly this place (`task_wrapper()` clears `active` when its
     * thread's body returns), so the two cores now answer `ps` under one law. */
    t->started  = 0;
    t->holding  = 0;
    t->active   = 0;
    return E_OK;
}

/* ms since the desktop started, folded the same way btron_render_perf_us() folds
 * Count: the modulo difference between two calls is only trusted while it is under
 * one wrap (~29 s), and every caller here is a frame or two apart.  Everything here
 * stays 32-bit, remainder kept in µs, because this link has no libgcc and a 64-bit
 * divide is a call to __udivdi3 that nothing answers. */
static uint32_t ps2_task_ms(void)
{
    if (s_timebase_ok) {
        const uint32_t now = ps2_count_read();
        if (s_task_clock_running) {
            const uint32_t us =
                (uint32_t)(now - s_task_count_last) / btron_ee_ticks_per_us() + s_task_us_rem;
            s_task_ms += us / 1000u;
            s_task_us_rem = us % 1000u;
        } else {
            s_task_clock_running = 1;
        }
        s_task_count_last = now;
    }
    return s_task_ms;
}

ER get_tim(SYSTIME *p_time)
{
    if (!p_time) return E_PAR;
    *p_time = (SYSTIME)ps2_task_ms();
    return E_OK;
}

/* Hold the rest of the frame period on Count.  Capped at 250 ms for two reasons that
 * are both about being able to see the screen: ms * 1000 * ticks-per-us overflows the
 * 32-bit compare past ~14.5 s at the rate this machine measures (29 s at the documented
 * one), and no pass -- so no USB poll, no pointer, no present -- runs while a body holds,
 * which makes a long delay a black screen rather than a slow one.
 * With no Count to read there is nothing to time against, so the pass is not held. */
static void ps2_task_hold(uint32_t ms)
{
    PS2_TASK *self = ps2_task_self();

    if (!s_timebase_ok || ms == 0) return;
    if (ms > 250u) ms = 250u;
    const uint32_t start  = ps2_count_read();
    const uint32_t budget = ms * (1000u * btron_ee_ticks_per_us());
    /* The only wait this kernel can observe, so the only one the census may report.  Set
     * around the loop and not around ps2_task_dly(): the pass that precedes the hold is the
     * body doing work, and a row that called that sleeping would hide exactly the frame cost
     * `ps` is read for. */
    if (self) self->holding = 1;
    while ((uint32_t)(ps2_count_read() - start) < budget)
        ps2_delay_cycles(200u);
    if (self) self->holding = 0;
}

/* One frame for a task body: present what it just invalidated, then give back the
 * time it asked for.  Called with s_gui_screen unset only if a body is started before
 * 'startx' has built the desktop, and such a body gets its delay without a screen. */
static void ps2_task_dly(W dlytim)
{
    uint32_t t0;

    if (s_gui_screen) ps2_gui_pass(s_gui_screen);
    t0 = ps2_cycle_hold_begin();      /* the pass' tail is timed from its last tick */
    ps2_task_hold(dlytim > 0 ? (uint32_t)dlytim : 0u);
    ps2_cycle_hold_end(t0, dlytim > 0 ? (int)dlytim : 0);
}

void dly_tsk(W dlytim)
{
    /* Advance the folded clock here rather than only in get_tim(): a body that
     * delays but never reads the time would otherwise leave a gap wider than one
     * Count wrap, and the fold below it would silently lose that time. */
    (void)ps2_task_ms();
    ps2_task_dly(dlytim);
}

ER tk_dly_tsk(W dlytim)
{
    ps2_task_dly(dlytim);
    return E_OK;
}

/* The InterCore worker census (include/btron/itron.h), answered from the table above rather
 * than from the canned four-line InterCore table `ps` used to print.  Slots are walked in the
 * order cre_tsk() hands them out, which is ascending tskid, so the shell's rows are stable
 * across a session and a worker keeps its line when another opens.
 *
 * Zero here is a true answer on a PS2 that has just booted -- no app has called cre_tsk() yet
 * -- and gterm's `ps` prints its own line for that case.  This function never returns
 * BTRON_TSK_NOWALK: this core does have a table, and the weak default in core_init.c was the
 * only thing saying it did not. */
W btron_list_tasks(BTRON_TSK_ROW *out, W max)
{
    W n = 0;

    if (!out || max <= 0) return 0;
    for (int i = 0; i < PS2_MAX_TASKS && n < max; i++) {
        const PS2_TASK *t = &s_ps2_tasks[i];
        if (!t->active) continue;
        out[n].tskid = (ID)(i + 1);
        out[n].name  = t->config.name;
        out[n].pri   = t->config.itskpri;
        out[n].entry = t->config.task;
        out[n].state = !t->started ? BTRON_TSK_NEW :
                       t->holding ? BTRON_TSK_SLEEP : BTRON_TSK_RUN;
        n++;
    }
    return n;
}

void launch_ps2_desktop_session(void)
{
    ps2_kprintf("\n[PS2] Switching to Authentic B-System 800x600 Workbench Desktop...\n");

    s_gui_active = 1;

    /* Initialize baremetal desktop screen */
    GDEV *screen = init_baremetal_desktop((uint32_t *)s_desktop_backbuffer, PS2_SCREEN_WIDTH, PS2_SCREEN_HEIGHT);
    if (!screen) {
        ps2_kprintf("[FATAL] Failed to initialize B-System Workbench screen!\n");
        s_gui_active = 0;
        return;
    }
    /* The bare-metal init path leaves BTRON_DESKTOP zeroed, so an app that asks the
     * desktop its size gets 0x0 and takes its own fallback -- xmb's is 960x600, which
     * on this canvas is a window wider than the screen and 2 MB of surface and depth
     * plane for a 1.6 MB client.  The [GL] backend row naming 952x564 was that bug. */
    btron_desktop_note_size(PS2_SCREEN_WIDTH, PS2_SCREEN_HEIGHT);
    workbench_init(PS2_SCREEN_WIDTH);

    /* LANE 21 rung 4: the present policy the `page double` dial asked for is applied HERE, at
     * the one point where a flipping compositor exists to honour it, and cleared on the way out
     * (see the session end below).  Both halves are load-bearing: the GS text path writes the
     * drawn page, so leaving the model on after Ctrl+Q is a Stage-1 console that takes input and
     * shows nothing, and turning it on before the cold paint below is a cold paint into a page
     * nobody is scanning. */
    ps2_page_double_apply(s_page_double_wanted);
    ps2_rot_owe_all();

#if BTRON_PS2_PAINT_WORKER
    /* Task #165: a build made with `PS2_PW=1` enters the desktop with the worker ARMED, because that
     * is what the operator asked for by building it -- and the table is reset here rather than at
     * boot because a price learned about a previous session's XMB window is a price about freed
     * memory.  Ctrl+O turns it off, `paintw 0` at the console turns it off, and either way the next
     * pass is the shipped present path. */
    ps2_pw_reset(0u);
    s_pw_on  = 1;
    s_pw_passes = 0u;
    s_pw_defer_passes = 0u;
    ps2_kprintf("[PS2] Paint worker #165 armed (this image was built PS2_PW=1): one layer record per\n");
    ps2_kprintf("[PS2] pass, and an app's frame admitted against the frame's remaining slice.\n");
    ps2_kprintf("[PS2] [Ctrl]+[O] turns it off and on; 'paintw stat' prints what it decided.\n");
#endif

    /* Initial paint & blit to GS eDRAM.  Timed and printed, because this is the
     * cost a whole-canvas pass pays and it needs no protocol to observe: whatever
     * the cursor's latency turns out to be, this row is the denominator for every
     * banded pass below it. */
    {
        ps2_bandset_t whole = { { { 0, 0, 0, 0 } }, 0, 0 };
        uint32_t r_us, s_us, u_us, px;
        ps2_bandset_full(&whole);
        ps2_paint_bands(screen, &whole, &r_us, &s_us, &u_us, &px);
        s_shown_x = s_mouse_x;
        s_shown_y = s_mouse_y;
        ps2_kprintf("[PS2] whole-canvas paint cost: render=%u us  swap=%u us  upload=%u us  total=%u.%03u ms\n",
                    (unsigned int)r_us, (unsigned int)s_us, (unsigned int)u_us,
                    (unsigned int)((r_us + s_us + u_us) / 1000u),
                    (unsigned int)((r_us + s_us + u_us) % 1000u));
    }

    /* The other page has to hold the same desktop before the first flip, or the first frame the
     * operator sees is the page this session never wrote -- VRAM that the previous format's bytes
     * describe as noise.  Cold-starting into noise is not a flicker, but it is a wrong picture for
     * two frames and it is the one thing that would make a rung-4 A/B unreadable.  So: plate what
     * the cold paint just drew, show it, plate the page it moved off of, and leave the debt clear
     * -- two full presents at entry, which is exactly what the rotation is supposed to pay once and
     * never again. */
    if (gs_disp_is_double()) {
        ps2_bandset_t whole = { { { 0, 0, 0, 0 } }, 0, 0 };
        uint32_t r_us, s_us, u_us, px;

        ps2_gs_flip(1);
        ps2_bandset_full(&whole);
        ps2_paint_bands(screen, &whole, &r_us, &s_us, &u_us, &px);
        ps2_rot_clear();
        ps2_kprintf("[PS2] second page plated at entry (rung 4): render=%u us  swap=%u us  "
                    "upload=%u us  flips=%u\n", (unsigned int)r_us, (unsigned int)s_us,
                    (unsigned int)u_us, (unsigned int)ps2_gs_flip_count());
    }

    ps2_kprintf("[PS2] Real B-System Workbench rendered via Host->Local GIF DMA (800x600).\n");
    ps2_kprintf("[PS2] Controls: Mouse/Pad/Kbd.  [Ctrl]+[Q] returns to this console.\n");
    ps2_kprintf("[PS2] [Ctrl]+[ [ ] and [Ctrl]+[ ] ] set the cursor's speed without leaving the\n");
    ps2_kprintf("[PS2] desktop, and [Ctrl]+[ \\\\ ] prints the counts behind it.  The gain row\n");
    ps2_kprintf("[PS2] names the constant to ship; 'ptrstat' and 'sens' at Stage 1 are the same numbers.\n");
    ps2_kprintf("[PS2] [Ctrl]+[P] turns the two-page present on and off -- the flicker A/B, in the\n");
    ps2_kprintf("[PS2] desktop, on this image.  The row it prints names which half is live.\n");
#if BTRON_PS2_PAINT_WORKER
    ps2_kprintf("[PS2] [Ctrl]+[O] arms and disarms the paint worker (#165): once per pass, priced.\n");
#endif
    ps2_kprintf("btron-ps2> ");

    /* Interactive Event Loop */
    s_pass_panel_due = ps2_count_read();
    s_pass_full_due  = ps2_count_read();
    s_gui_screen     = screen;

    /* Both counters start the session empty, so the rows printed on the way out
     * describe this desktop and nothing before it: the boot log's enumeration and
     * the cold paint above would otherwise be folded into the first measurement. */
    s_ptrst = (ps2_ptrst_t){ 0 };
    s_paint = (ps2_paint_t){ 0 };
    /* The compositor's own counters are shared with the boot log's paints, and
     * `bfn` below only means something for this desktop: a miss during the cold
     * paint is the cache latching, not a band that failed to clip.  take() is the
     * only clear the shared profiler offers, and this drops the snapshot with it. */
    {
        RENDER_STATS discard;
        btron_render_stats_take(&discard);
    }

#if BTRON_PS2_BENCH
    /* The measurement runs instead of the session, and the session ends so that a
     * headless PCSX2 (-nogui, therefore -batch) shuts itself down with the log
     * flushed.  Everything above this line is identical to the shipped desktop, so
     * the numbers below describe the present the desktop makes. */
    ps2_bench_run(screen);
    s_gui_active = 0;
#endif

    while (s_gui_active) ps2_gui_pass(screen);

    /* Rung 4's present policy ends with the session, before the console is cleared -- and the
     * order is not stylistic.  Text mode writes to gs_disp_drawn(), and with the model left on
     * that is the page the PCRTC is not scanning: a Stage-1 prompt that takes every command and
     * shows none of them.  Turning it off puts the writer back on the page being read and re-points
     * DISPFB at it, which is what makes the exit row below readable at all.
     *
     * `s_page_double_wanted` is deliberately left as the operator set it -- this is the session
     * ending, not the dial being reset -- and the entry above re-applies it next time. */
    ps2_page_double_apply(0u);
    ps2_rot_clear();
    ps2_kprintf("[PS2] flip totals this session: %u flips, %u not vsync-aligned, longest wait %u us\n",
                (unsigned int)ps2_gs_flip_count(), (unsigned int)ps2_gs_flip_unaligned(),
                (unsigned int)ps2_gs_flip_wait_max_us());
#if BTRON_PS2_PAINT_WORKER
    {
        ps2_pw_stats_t pw;

        /* The session's own decisions, in the form the two claims are judged in: `credit` is the
         * re-records that did not happen, `defer` the frames the budget refused, and `forced` the
         * ones the age rule rescued anyway.  Printed at exit for the same reason [PSTAT] is -- the
         * prompt takes no keystrokes while the desktop owns them, so the only window that can hold
         * this session's numbers is the one that closes it. */
        ps2_pw_stats(&pw);
        ps2_pw_row(&pw, 0);
        ps2_kprintf("[PS2] paint worker this session: %u of %u passes refused the app (off: %u)\n",
                    (unsigned int)s_pw_defer_passes, (unsigned int)s_pw_passes,
                    (unsigned int)(s_pw_on ? 0u : 1u));
        ps2_pw_jobs();
        ps2_pw_freeze(0);
        s_pw_on = 0;
    }
#endif

    /* Clean return to Stage 1 text console */
    ps2_gs_text_clear();
    ps2_kprintf("[DESK] Exited 800x600 workbench, back to the Stage 1 console. 'startx' again anytime.\n");

    /* The session's own pointer and paint totals, printed where they can be read.
     * `ptrstat` is a Stage 1 prompt command and the prompt takes no keystrokes
     * while the desktop owns them, so a run that wants its numbers has to be
     * measured by leaving it.  This is the row set that separates the two
     * complaints about the cursor: a low `loop:` Hz with a large `upload` average
     * is the whole-canvas repaint costing the frame rate, and a healthy loop rate
     * with the same low `loop:`-relative report count is the source itself. */
    ps2_ptrst_print("session");
    ps2_paint_print();
    ps2_kprintf("btron-ps2# ");
}

/* ── Hardware Bring-up Log (one row per subsystem, Stage 1) ─────── */

/* The controller only ever touches the bytes it is told to master, so these
 * rows say which IOP-RAM patch we borrowed and whether the borrow held.  The
 * decode column is one word written through the window and read straight back,
 * because nothing about what the IOP keeps at an address can prove the EE is
 * reaching it: a granule that reads all-zero looks free whether the window is
 * live or dead, which is exactly the trap that rejected this borrow before. */
static void ps2_log_iopram(const ps2_iopram_t *r)
{
    ps2_kprintf("[SBUS] iop-ram@0x%08x decode=%u vector@0x80=0x%08x granules=%u\n",
                (unsigned int)PS2_IOP_KSEG1, (unsigned int)r->readable,
                r->first_read, (unsigned int)r->granules);
#if BTRON_HID_TRACE
    ps2_kprintf("[SBUS] shadow %u B @0x%06x: canary 0x%08x->0x%08x write=%u settle=%u\n",
                (unsigned int)PS2_IOP_SHADOW_SIZE, r->off, r->canary, r->after_settle,
                (unsigned int)r->write_ok, (unsigned int)r->survived);
#endif
}

static void ps2_log_ohci_probe(const ps2_ohci_probe_t *p)
{
    ps2_kprintf("[USB] ohci@0x%08x rev=0x%02x ports=%u fmInt=0x%08x\n",
                (unsigned int)OHCI_BASE_ADDR, p->rev & 0xFF,
                (unsigned int)(p->rh_a & 0x1Fu), p->fminterval);
#if BTRON_HID_TRACE
    /* The rest of this is the controller's own register file before and after the
     * probe touched it -- the rows that answered "can this thing master memory
     * the EE can read", which the verdict line below now carries as its answer.
     * They are one-off at boot, so they cost nothing but console rows, and those
     * are the thing this log has to fit on a screen. */
    ps2_kprintf("[USB] after reset+enable: rev 0x%02x->0x%02x  frames 0x%08x->0x%08x  int 0x%08x/0x%08x\n",
                p->rev & 0xFF, p->rev_enabled & 0xFF, p->fm_before, p->fm_after,
                p->intstatus, p->intenable);
    /* Port status as the probe found it and as it left it.  The connect bit is
     * one no driver write can set, so a controller reset is something this port
     * cannot afford to do once a device is on the bus; this pair is the check
     * that the probe really does leave the ports alone. */
    ps2_kprintf("[USB] rh=0x%x  p1 0x%x>0x%x  p2 0x%x>0x%x\n",
                p->rh_status, p->port1, p->port1_after,
                p->port2, p->port2_after);
    if (p->hcca_iop) {
        /* frame 0xa5a5 means the controller never wrote the HCCA we pointed it
         * at; anything else is its frame counter, stamped by its own bus master. */
        ps2_kprintf("[USB] hcca@0x%06x: frame 0x%04x done 0x%08x intAfter 0x%08x\n",
                    p->hcca_iop, p->hcca_frame, p->hcca_done, p->int_after);
    }
#endif
    ps2_kprintf("[USB] verdict: %s\n", ps2_usb_verdict_name(ps2_usb_verdict()));
}

/* The USB driver's per-transfer row.  The record arrives as one pointer rather
 * than as arguments for the reason the ABI note in the Makefile records: when
 * this row was written the driver was a -march=mips3 object and this file a
 * -march=mips2 one, those two disagreed about the stack slots a call of more
 * than four arguments uses, and the first rows off this printed the fifth
 * argument as 0xffffff80.  The image is one calling convention now, and the
 * pointer stays because twelve fields do not travel well as arguments.
 *
 * `t` names which call of enumeration this was (0 ordinary, 1 SET_ADDRESS,
 * 2 the device descriptor read at address zero, 3 the same read at the assigned
 * address).  `sv` is a bitmask, one bit per descriptor, of the host having moved
 * a descriptor's buffer pointer to the end -- the only per-descriptor evidence
 * that survives a host that retires a descriptor without delivering it.  `d0` is
 * the first four bytes of the data payload, which starts at zero every transfer
 * and so is non-zero only when bytes arrived.  `q` is how many frames the host
 * took to stop looking at the control list before this ring was published. */
void ps2_usb_log_tx(const ps2_usb_tx_t *t)
{
#if BTRON_HID_TRACE
    ps2_kprintf("[TX] p%u t%u %04x n%u r=%d hd=%x dn=%x st=%u cc=%u sv=%x d0=%x q%d\n",
                t->port, t->tag, t->req, t->ntd, (int)t->result, t->head, t->done,
                t->stuck, t->cc, t->svc, t->data0, (int)t->quiet);
#else
    (void)t;
#endif
}

/* The one row that proves the periodic list moves: everything else about
 * polling is a register read, and a NAK'd interrupt descriptor leaves no memory
 * trace at all, so a headless run cannot tell a live ring from a dead one until
 * a report actually arrives. */
void ps2_usb_report_landed(uint32_t kbd, uint32_t mouse)
{
    ps2_kprintf("[HID] first report landed, frame 0x%04x kbd=%u mouse=%u\n",
                (unsigned int)ps2_usb_frame_number(), kbd, mouse);
}

/* One row per key the decoder produced, whatever became of it afterwards.
 * `c` is the HID usage code and `k` the BTRON key it decodes to, so a key that
 * arrives but maps to nothing reads differently from one the emulator never
 * delivered: the first has a row here and no echo, the second has no row at all.
 * `n` is this device's total accepted reports, which is what says whether the
 * polling ring kept answering after this one.
 *
 * These two dumpers are HIDTRACE=1 only: a keypress row costs the console a line
 * and a scroll, so left compiled in it is the thing that makes the prompt
 * unreadable while it is being typed into.  The hid= field of the [LOG] row says
 * which build produced a screen, so missing rows read as a flag rather than as a
 * dead bus. */
void ps2_usb_log_kbd(uint32_t mod, uint32_t code, uint32_t key, uint32_t reports)
{
#if BTRON_HID_TRACE
    ps2_kprintf("[KBD] mod=%02x c=%02x k=%x n=%u\n", mod, code, key, reports);
#else
    (void)mod; (void)code; (void)key; (void)reports;
#endif
}

/* The four bytes of a pointer report in the order they sat in memory, printed
 * before anything is read out of them, so that [RAW] and the [PTR] row sharing a
 * # number is the pair that says where a wrong-way move came from: same bytes and
 * a wrong axis is this port's parse, and bytes that already put the horizontal
 * travel in the third slot is the device's.  Moves on one axis only differ from
 * the other by which of the middle two bytes changes, so a run that sweeps one
 * axis at a time reads the report layout outright. */
void ps2_usb_log_mouse_raw(uint32_t reports, uint32_t b3b0)
{
#if BTRON_HID_TRACE
    if (reports <= 12u || (reports & 63u) == 0) {
        ps2_kprintf("[RAW] #%u %02x %02x %02x %02x\n", (unsigned int)reports,
                    (unsigned int)(b3b0 & 0xffu),
                    (unsigned int)((b3b0 >> 8) & 0xffu),
                    (unsigned int)((b3b0 >> 16) & 0xffu),
                    (unsigned int)((b3b0 >> 24) & 0xffu));
    }
#else
    (void)reports; (void)b3b0;
#endif
}

/* What one pump pass of the pointer's interrupt ring drained.  The row is the
 * difference between two stories that otherwise look identical on screen: a
 * cursor that moves too far too late because reports were queued faster than this
 * port collected them, and one that moves exactly as far as a wrong scale factor
 * says it should.  `max` is the high-water mark, so a single row near the ring's
 * capacity is enough to establish which story this run is. */
void ps2_usb_log_ring(uint32_t dev, uint32_t got, uint32_t burst, uint32_t frame)
{
#if BTRON_HID_TRACE
    const ps2_usb_dev_t *d = ps2_usb_dev((int)dev);
    static uint32_t s_ring_rows;

    if (!d || d->is_keyboard || d->mps >= 8u) return;   /* the keyboard's ring has its own rows */
    /* Gated on its own count rather than on the report counter the other rows
     * use: a pass that drains several reports moves that counter past the every-
     * sixty-fourth value, and a diagnostic that goes quiet exactly when the pump
     * is behind is worse than no diagnostic at all. */
    if (s_ring_rows < 12u || (s_ring_rows & 63u) == 0u) {
        ps2_kprintf("[RING] n=%u max=%u f=%u\n", (unsigned int)got,
                    (unsigned int)burst, (unsigned int)frame);
    }
    s_ring_rows++;
#else
    (void)dev; (void)got; (void)burst; (void)frame;
#endif
}

/* One row per root-hub port the host engine addressed, plus what came of it.
 * The step column is the point of the whole row: it separates the ways
 * enumeration fails -- nothing attached, the port never came out of reset, the
 * device answered at address zero but not at its own, the endpoint it advertises
 * is not the one we asked for, a second device sharing the first one's address
 * -- and none of them look like the others here. */
static int ps2_log_host(void)
{
    static const char *const steps[] = {
        "none", "reset", "desc0", "addr", "cfg", "setcfg", "poll", "dupadd"
    };
    int live = 0;

    /* The numbers that read a failed enumeration apart: `st` is the first
     * descriptor of the last control transfer the controller never wrote back (1
     * setup, 2 data, 3 status, 0 all of them came back) and `w` what the drain
     * wait returned (-1 the controller called itself dead, -2 it ran out of
     * frames).  `ad` is the function address this device is queued at, and no
     * two polled rows share one: this host leaves every device answering zero,
     * which is why the second of them reads as step dupadd instead of polling.
     * `id` is the descriptor's vendor and product words, which name the device:
     * this host's pointer answers 10627 and its keyboard binding 20510, while
     * every HID device opens a descriptor with the same first eight bytes and so
     * cannot be told apart there.  `pl`/`er` are the descriptors this device's
     * interrupt ring retired, clean and with a condition code: pl climbing with
     * reports at zero is a host that answers our polls with something we reject,
     * and pl stuck at zero after step poll is a periodic list nobody walks. */
    for (int i = 0; i < 2; i++) {
        const ps2_usb_dev_t *d = ps2_usb_dev(i);
        if (!d) continue;
        if (d->live) live++;
        ps2_kprintf("[HID] p%u %-6s ep=%u mps=%u cc=%u st=%u w=%d ad=%u id=%x pl=%u er=%u\n",
                    d->port, steps[d->step <= 7u ? d->step : 0u], d->ep, d->mps,
                    d->cc, d->stuck_td, d->wait_ret, d->addr, d->desc_id,
                    d->polls, d->errors);
    }
    /* `rel` counts the transfers that started by cancelling a descriptor the
     * controller was still holding from the transfer before it, which is this
     * host's one way to lose a completed transfer; `rearm` counts the transfers
     * that started by restarting a frame engine that had stopped taking
     * boundaries.  Neither should climb once enumeration is moving.  `done` is
     * the host's own done-queue head, so a value inside the descriptor block is
     * the host saying outright that it retired a descriptor -- the one piece of
     * evidence about progress that is not read out of memory we share with it. */
#if BTRON_HID_TRACE
    ps2_kprintf("[USB] after host: ctl=0x%x cmd=0x%x int=0x%x frm=0x%x done=0x%x rel=%u rearm=%u\n",
                ps2_usb_reg(OHCI_REG_CONTROL), ps2_usb_reg(OHCI_REG_CMDSTATUS),
                ps2_usb_reg(OHCI_REG_INTSTATUS), ps2_usb_reg(OHCI_REG_FMNUMBER),
                ps2_usb_reg(OHCI_REG_DONE_HEAD),
                ps2_usb_async_releases(), ps2_usb_engine_rearms());
#endif
    ps2_kprintf("[HID] %s, %d us, frame 0x%04x  reports kbd=%u mouse=%u\n",
                live ? "host engine up" : "no device left polling",
                (int)ps2_usb_host_us(), (unsigned int)ps2_usb_frame_number(),
                (unsigned int)ps2_usb_kbd_reports(), (unsigned int)ps2_usb_mouse_reports());
    return live;
}

/* The rows before the probe are printed as work completes, so the last one on
 * screen names whatever step did not finish.  The upload timings matter because
 * without a dirty row band the console paid a full-screen GIF transfer per
 * keystroke. */
static void ps2_log_boot_head(void)
{
    /* Which artifact produced this log: a build stamp is the only way a symptom
     * quoted from a screen can be tied to the source that made it.  The fmt
     * columns beside it are the vararg self-test -- this formatter's own proof
     * that it reads its arguments in order, which a -march mismatch between the
     * driver and the console once broke silently, and which only the diagnostic
     * build has any reason to print. */
    ps2_kprintf("[LOG] build %s %s\n", __DATE__, __TIME__);
#if BTRON_HID_TRACE
    ps2_kprintf("[LOG] fmt %02x %u %06x   want cd 164 000800   hid=%u\n",
                0xCDu, 164u, (unsigned int)PS2_IOP_SHADOW_SIZE,
                (unsigned int)BTRON_HID_TRACE);
#endif
    ps2_kprintf("[BOOT] B-System / BTRON3 3.20  Emotion Engine R5900 (MIPS-III LE)  32 MB RDRAM  no MMU\n");
    ps2_kprintf("[GS] SetGsCrt omode=0x52 720p  DISPLAY 800x600 mag1x  CT32  VRAM page 0 pitch 832 px (1.90 MB)\n");
    ps2_kprintf("[GS] init %u us   full canvas upload 120000 QW %u us   one console row-band %u us\n",
                (unsigned int)s_gs_init_us, (unsigned int)s_full_upload_us, (unsigned int)s_band_upload_us);
    ps2_kprintf("[CPU] CP0 Count %s  (%u ticks/us, %s)\n",
                s_timebase_ok ? "running" : "STOPPED, counted delays",
                (unsigned int)btron_ee_ticks_per_us(),
                btron_ee_ticks_per_us_measured()
                    ? "measured against the input plane"
                    : "documented rate, plane gave no window");
    {
        /* The float question, answered rather than assumed: CU1 (bit 29) says whether
         * the loader handed over the EE's FPU, btron_fp_on says whether this image
         * then used it.  The two can disagree on purpose -- btron_fp_init() sets CU1
         * itself when the BIOS left it clear, and falls back to the bit engine if the
         * FPU does not answer, so a machine with no usable FPU costs time and not
         * pixels.  FCSR is printed because its rounding field has to agree with the
         * engine's round-nearest-even for the two to be comparable. */
        uint32_t fcsr = 0u;
        const uint32_t st = btron_fp_probe(&fcsr);
        ps2_kprintf("[CPU] CP0.Status=0x%08x  CU3..CU0=%d%d%d%d  IE=%d  EXL=%d\n",
                    (unsigned int)st,
                    (int)((st >> 31) & 1u), (int)((st >> 30) & 1u),
                    (int)((st >> 29) & 1u), (int)((st >> 28) & 1u),
                    (int)(st & 1u), (int)((st >> 1) & 1u));
        ps2_kprintf("[FPU] single precision: %s  proof add(1.0f,2.0f)=0x%08x want 0x40400000  FCSR=0x%08x\n",
                    btron_fp_on ? "Emotion Engine COP1" : "bit engine, COP1 did not answer",
                    (unsigned int)btron_fp_seen, (unsigned int)fcsr);
        /* What the image was *built* for, on one row, next to what this machine answered.
         *
         * The two dials are Makefile variables with defaults (`PS2_GL ?= tinygl`, `PS2_FP ?= hw`),
         * and `make run-ps2` with no arguments silently rebuilds under them -- including after a
         * session that was run with a dial, because the stamp files then flip the objects that read
         * it back.  So a symptom reported from that run ("the XMB is black") arrives with no record
         * of which pipeline and which FPU law produced it, and the two have been known to disagree
         * about exactly that.  The FP half cannot be asked of the preprocessor here: the flag is
         * per object and this file does not carry it, which is why btron_fp_law() lives in the unit
         * that does. */
        ps2_kprintf("[CFG] this image: PS2_FP=%s  PS2_GL=%s  (the dials it was built under; "
                    "an empty `make run-ps2` rebuilds with their defaults)\n",
                    btron_fp_law(), gl_backend_name(gl_backend_default()));
    }
}

static void ps2_log_boot_tail(int hid_devs)
{
    ps2_kprintf("[USB] probe pass took %u us\n", (unsigned int)s_ohci_probe_us);
    ps2_kprintf("[SIO] SIO0 console is write-only here: PCSX2 wires no host terminal to it\n");
    ps2_kprintf("[PAD] DualShock decoder ready, no SIF/IOP pad client yet  input: %d USB HID\n",
                hid_devs);
    /* The keyboard's cadence, on the console, because a setting that is never read is
     * indistinguishable from one that was never set.  These are the applet's own three
     * variables -- the same defaults the posix and Pi 400 targets ship -- and the row
     * says which of them is currently in force, so "the key will not repeat" can be
     * answered by looking at enabled= rather than by guessing at the code path. */
    ps2_kprintf("[KBD] auto-repeat: enabled=%d  delay=%u us  interval=%u us  (Input applet's "
                "*FX 11/12; a held HID key repeats from here)\n",
                g_kbd_repeat_enabled ? 1 : 0,
                (unsigned int)g_kbd_repeat_delay_us,
                (unsigned int)g_kbd_repeat_interval_us);
}

/* ── Kernel Entry & Stage 1 Dispatch ────────────────────────────── */

void ps2_kernel_main(void)
{
    /* -1. The stack's own watermark, before anything can recurse.  This has to be the first act
     *     with a frame, because the probe can only paint memory no call has used yet, and the
     *     answer is what licenses every kB the pool takes from the tail of RAM (see
     *     ps2_stack_probe_arm).  Anything it misses is a call that ran before this one -- boot's
     *     own handful of stores. */
    ps2_stack_probe_arm();

    /* 0. The canvas, before anything can draw into it or be measured.  The GS driver
     *    used to own a second 800x600x4 array and the present copied this one into it
     *    one pixel at a time; now the DMA reads the array the compositor paints, so the
     *    pointer has to be installed before ps2_sio_init() mirrors the boot log to the
     *    screen.  See BTRON_PIXEL_FB in include/btron/types.h for the byte order the
     *    pixels those rows draw are already in. */
    ps2_gs_set_framebuffer((uint32_t *)s_desktop_backbuffer);

    /* 1. SIO0 first: every row below is mirrored to it, and it is also the only
     *    way to see a hang that happens before the GS console exists. */
    ps2_sio_init();

    /* 1a. The plane table, before anything that can poll a HID report: the rows
     *     every later budget is charged against live here, and a reader that ran
     *     first would be reading the desktop loop's pass count as if it were a
     *     period. */
    ps2_planes_install();

    /* 1b. The FPU decision, before anything is timed or rasterised: btron_fp_init()
     *     proves the EE's single-precision unit answers and switches to it, or leaves
     *     the bit engine in charge.  It cannot come later than here -- the two engines
     *     differ by an order of magnitude, so a phase timed before the call is not the
     *     same phase timed after it, and every row below is a timing. */
    btron_fp_init();

    /* 2. Establish the timebase before anything is timed. */
    uint32_t c0 = ps2_count_read();
    for (volatile int i = 0; i < 20000; i++) { }
    s_timebase_ok = (ps2_count_read() != c0);

    /* 2a. The machine's microsecond, before the first thing that is timed with it.
     *     Arming the INPUT plane here rather than at 4b costs its calibration window --
     *     32 periods, 32 ms of the plane's own clock -- and buys every row after it:
     *     the GS's upload costs, the OHCI probe, the present's stage attribution, the
     *     pointer's slices and the bench's budgets all divide Count by what this
     *     measures, so a port that armed the clock after printing them would be quoting
     *     its own timings in the rate it had just disproved.  The plane itself is then
     *     already running when 4b takes its evidence window, which is the point: a clock
     *     that only exists for the row that proves it exists proves nothing.
     *
     *     Nothing here waits when Count has stopped, because the gaps that answer the
     *     question are Count's own numbers and a stopped counter reports zero for every
     *     one of them; the divisor stays at the documented rate and says so. */
    if (s_timebase_ok && ps2_irq_input_arm() == 0) (void)ps2_irq_calib_wait();

    /* 3. Graphics Synthesizer + GIF DMA, so the boot log lands on screen. */
    uint32_t gs0 = ps2_count_read();
    ps2_gs_init(PS2_SCREEN_WIDTH, PS2_SCREEN_HEIGHT);
    uint32_t gs1 = ps2_count_read();
    ps2_gs_flush();
    s_full_upload_us = ps2_us_since(gs1);
    uint32_t band0 = ps2_count_read();
    ps2_gs_upload(0, 0, PS2_SCREEN_WIDTH, 16);
    s_band_upload_us = ps2_us_since(band0);
    s_gs_init_us = ps2_us_since(gs0);

    ps2_log_boot_head();

    /* 4. Input.  The OHCI probe is a measurement, not an initialisation step:
     *    it decides whether a keyboard and mouse are reachable from this core
     *    at all, and the pad driver stays a decoder until it says so.  The
     *    controller is a bus master of IOP RAM and nothing else, so the first
     *    thing it needs is a patch of IOP RAM the EE can reach and see. */
    ps2_pad_init();
    ps2_iopram_claim(NULL);
    ps2_log_iopram(ps2_iopram_last());
    uint32_t probe0 = ps2_count_read();
    int verdict = ps2_usb_probe(NULL);
    s_ohci_probe_us = ps2_us_since(probe0);
    ps2_log_ohci_probe(ps2_usb_last_probe());

    /* The probe only says the controller can master the block we handed it.
     * Turning that into a keyboard means descriptors it reads for itself, the
     * enumeration requests, and an interrupt endpoint left queued -- so the
     * engine runs here and says per-port how far it got. */
    if (verdict == PS2_OHCI_IOP_DMA) ps2_usb_host_start();
    ps2_log_boot_tail(ps2_log_host());

    /* Now that the bus has said who answered, the pointer can be told what a
     * count from them is worth.  Before this row the desktop is reachable but
     * deaf, so the profile can never be applied to a stream it did not measure. */
    ps2_ptr_src_detect();

    /* 4b. The INPUT plane's evidence (doc/txt/ASYNC.txt Tier-1).  Until this port had no
     *     periodic interrupt at all: every HID report waited for the pass that paints, so
     *     the pointer's latency simply WAS the frame time -- which is hundreds of
     *     milliseconds with the XMB open.  Arming a clock is a claim; the ticks that
     *     arrive inside a measured window are the evidence, and the row prints the
     *     difference rather than assuming it.  The window is 50 ms because that is fifty
     *     periods of a 1 ms plane, enough to tell a live clock from a register that
     *     accepted a handler and never fired.  The plane was armed at 2a and has been
     *     ticking through everything above, so this window measures a clock that was
     *     already doing the work, and ps2_irq_input_arm() here only answers for the
     *     registry's verdict (it is idempotent and returns the same rejection).
     *
     *     CP0 Status is printed across the window because the first run on this machine
     *     took exactly one tick in fifty, and the tick count alone cannot say whether
     *     the clock never fired or fired once and could not be re-armed: taking an
     *     exception clears the EE's interrupt-enable bit, so those two failures differ
     *     only in this register. */
    {
        const int arm_rc = ps2_irq_input_arm();
        const uint32_t st0 = ps2_irq_status();
        const uint32_t win0 = ps2_count_read();
        const uint32_t tick0 = ps2_irq_ticks();
        const uint32_t fm0 = ps2_usb_frame_number();
        uint32_t window_us, got, owed, frames;
        uint32_t st1;

        ps2_task_hold(50u);
        window_us = ps2_us_since(win0);
        got = ps2_irq_ticks() - tick0;
        owed = window_us / ps2_irq_input_period_us();
        st1 = ps2_irq_status();

        /* The USB host's frame counter is this machine's third clock.  OHCI frames are
         * 1 ms by definition and are kept by the controller, not by CP0 Count, so
         * counting them across the same window is the check on the divisor the rows below
         * are printed with: it is the one opinion here that did not go into measuring it.
         * That is the row that settled the question this port had -- Timer0's periods came
         * in 1985..2026 of our microseconds apart while this counter said 25 frames had
         * passed in what Count called 50 ms, two hardware clocks agreeing against the
         * port's own divide by exactly a factor of two, which is the emulator feeding
         * Count at the core clock rather than the documented half of it.  Agreement here
         * now is the calibration holding. */
        frames = ps2_usb_frame_number() - fm0;
        ps2_kprintf("[ASYNC] input plane Timer0 (INTC 9): period=%u us compare=%u ticks=%u of the %u this %u us window owes -> %s%s\n",
                    (unsigned int)ps2_irq_input_period_us(),
                    (unsigned int)ps2_irq_timer_compare(),
                    (unsigned int)got, (unsigned int)owed,
                    (unsigned int)window_us,
                    arm_rc < 0 ? "the INTC registry REJECTED the handler: " : "",
                    (arm_rc < 0 || got == 0u) ? "DEAD"
                                              : ((got + 1u >= owed) ? "LIVE" : "STALLED"));

        /* A clock is not a plane until something works on it.  Handing the OHCI's periodic
         * collection to the handler is the step that makes the input plane the thing that
         * services the pointer rather than the pass that paints it, and it is taken only on
         * the verdict the row above measured: on a machine whose plane is DEAD the pass keeps
         * polling the rings itself, because a queue nobody fills is a keyboard that types
         * nothing -- and on one that is STALLED the same, since a plane delivering 3 of the 50
         * periods it owes would collect 3 reports a second while the ring it is the only
         * reader of piled the rest up.
         *
         * It goes here, before the rows below, because the ISR budget printed at the end of
         * this block has to be charged with the work the plane now does: the cost of an empty
         * ring is two uncached reads, and the number worth printing is the one taken after
         * the collection joined the handler. */
        if (arm_rc >= 0 && got + 1u >= owed) {
            ps2_usb_input_plane(1);
            ps2_kprintf("[ASYNC] hid collection: the input plane reads the OHCI rings and a pass delivers them\n");
        }
        /* The divisor itself, with the window behind it: the count of periods, the sum of
         * their gaps, and the mean that division was taken from.  The OHCI row below is
         * the sample that did not go into this number. */
        ps2_kprintf("[ASYNC] machine microsecond = %u Count ticks, measured over %u periods of the plane (%u ticks summed, mean %u) -- %s\n",
                    (unsigned int)btron_ee_ticks_per_us(),
                    (unsigned int)ps2_irq_calib_periods(),
                    (unsigned int)ps2_irq_calib_gap_ticks(),
                    (unsigned int)(ps2_irq_calib_periods()
                                       ? ps2_irq_calib_gap_ticks() / ps2_irq_calib_periods() : 0u),
                    btron_ee_ticks_per_us_measured()
                        ? "measured, so every microsecond this image prints is real time"
                        : "FALLBACK to the documented 147: no window, timings are the datasheet's opinion");
        /* The plane's period jitter, in the raw ticks the handler stamped rather than in
         * microseconds, because a gap divided by the divisor measured from gaps is the
         * period by construction and would carry no information.  The µs conversion of the
         * pair is still printed, and the two are read together: the mean is 1 ms to within
         * 0.1 % while the extremes this machine took were 33 us and 14 ms.
         *
         * That pair is measured; what follows is the inference, and it is the shape the
         * numbers have rather than a cause found.  A clock running at the wrong rate would
         * move the mean, and it has not moved: a wide short edge and a wide long edge
         * around a correct mean is a handler that was kept out of the processor for 14 ms
         * and then took the edge it had missed together with the next one.  The boot is
         * where both were taken -- this plane was armed before the GS and the USB probe, and
         * those phases are the port's own longest busy work -- so the desktop loop's
         * jitter is a separate question, and it is the one the [PSTAT] rows have to answer
         * for a hand that is actually moving a cursor. */
        ps2_kprintf("[ASYNC] gaps since the arm: %u..%u ticks = %u..%u us, against the %u us the period means\n",
                    (unsigned int)ps2_irq_gap_min_ticks(),
                    (unsigned int)ps2_irq_gap_max_ticks(),
                    (unsigned int)(ps2_irq_gap_min_ticks() / btron_ee_ticks_per_us()),
                    (unsigned int)(ps2_irq_gap_max_ticks() / btron_ee_ticks_per_us()),
                    (unsigned int)ps2_irq_input_period_us());
        ps2_kprintf("[ASYNC] the same window on the OHCI's frame counter: %u frames of the %u a 1 ms frame says this is -- %s\n",
                    (unsigned int)frames, (unsigned int)owed,
                    frames == 0u ? "the controller is not counting, so it is no yardstick"
                    : (frames + frames / 2u < owed ? "CP0 Count is running FAST: the port's microsecond is too short"
                    : (owed + owed / 2u < frames ? "CP0 Count is running SLOW: the port's microsecond is too long"
                                                 : "agree -- the measured divisor and the bus's own frame counter say the same 50 ms")));
        ps2_kprintf("[ASYNC] CP0.Status 0x%08x at the arm, 0x%08x after it: EIE(bit16)=%d -> %d, IEc(bit0)=%d -> %d\n",
                    (unsigned int)st0, (unsigned int)st1,
                    (int)((st0 >> 16) & 1u), (int)((st1 >> 16) & 1u),
                    (int)(st0 & 1u), (int)(st1 & 1u));
            /* Where the clock stopped, as the hardware itself answers it.  A counter that
             * moves but produces no tick is a source the kernel is not delivering; a
             * counter parked above the compare value is one that ran past its period and
             * has nothing to match until it wraps.  The two need different fixes and the
             * tick count alone cannot tell them apart.
             *
             * What this machine's read actually returns is a counter that has barely
             * moved: 140 -> 140 on one run and 276 -> 277 on the next, over a hold the
             * measured microsecond says was 2 ms, where two periods of a COMP=576 counter
             * is 1152 of its own counts.  Measured, that is the register's behaviour.
             * Inferred, this emulator keeps the timer's schedule somewhere other than the
             * word at 0xB0000000 and lets a plain read find only the residue of the event
             * it last processed -- because the same timer's compare interrupt arrived 50
             * times in a 50 ms window that the OHCI's frame counter also called 50 ms, and
             * a counter standing still cannot do that.
             *
             * So this row is not the cadence's evidence, the tick count above it is, and
             * what this read is worth is the MODE word: 0x1c2 is CLKS=2 with the
             * zero-return, the update enable, the compare interrupt and both flags
             * acknowledged, which is the word the arm wrote and the handler rewrites every
             * period, and a period that lost that acknowledge is the one failure this
             * register would show. */
            const uint32_t cnt0 = ps2_irq_timer_count();
            uint32_t cnt1;

            ps2_task_hold(2u);
            cnt1 = ps2_irq_timer_count();
            ps2_kprintf("[ASYNC] timer0 MODE=0x%08x (clk256+zret+cue+cmpe+ack) count %u -> %u over a 2 ms hold, compare=%u, ticks=%u total\n",
                        (unsigned int)ps2_irq_timer_mode(),
                        (unsigned int)cnt0, (unsigned int)cnt1,
                        (unsigned int)ps2_irq_timer_compare(),
                        (unsigned int)ps2_irq_ticks());
        /* The handler's own cost, in the hundredths of a microsecond: this machine's whole
         * ISR is a third of one, and a column that rounds it to `0 us of the 50 us budget`
         * would be the same row whether the handler were free or forty times more
         * expensive.  The two worker planes are charged where their work happens, not here --
         * at boot neither has run yet, so the honest thing to print is which budget each
         * will be read against and the row that will say so. */
        {
            const uint32_t wcet_100 = ps2_irq_wcet_ticks() * 100u / btron_ee_ticks_per_us();
            ps2_kprintf("[ASYNC] isr wcet=%u ticks = %u.%02u us of the %u us budget, overruns=%u  "
                        "workers: input %u us, ui %u us against a %u us deadline -- [PSTAT] budget\n",
                        (unsigned int)ps2_irq_wcet_ticks(),
                        (unsigned int)(wcet_100 / 100u), (unsigned int)(wcet_100 % 100u),
                        (unsigned int)ASYNC_ISR_BUDGET_US,
                        (unsigned int)ps2_irq_overruns(),
                        (unsigned int)ASYNC_INPUT_BUDGET_US,
                        (unsigned int)ASYNC_UI_BUDGET_US,
                        (unsigned int)btron_planes()->ui_period_us);
        }
    }

    /* 5. Storage, before the banner and before anything that can be typed: the CLU layer and the
     *    Terminal window's in-window `sc`/`tv` both begin at vfs_init(), which on this target asks
     *    one question -- is a volume mounted -- and the answer is only yes after this call.  It was
     *    not called until now: btron_kernel_init() is the only caller in the tree and it lives in a
     *    TU entered from src/desktop/main.c, which is not in PS2_SRCS, so the [HFDS] row this port
     *    prints today is the first one any PS2 boot has ever printed. */
    btron_core_hfds_log();

    /* 6. Banner last, so the prompt is the bottom row and nothing interesting
     *    scrolls away underneath it. */
    btron_core_banner();
    ps2_kprintf("\n");

#if BTRON_PS2_BENCH_GS == 1 || BTRON_PS2_BENCH_GS >= 6
    /* The census instead of the desktop -- see the rung's note on why a read is only
     * honest at Stage 1.  The shell loop below still runs, because nothing here can
     * stop PCSX2: the harness stops it when the sentinel reaches the log.  Value 2 is
     * deliberately NOT this branch: rung 6 reads a live desktop's page, so it has to
     * boot the session the census rung refuses to start.  6 IS this branch, because
     * rung 3a reads a page nothing else may be writing -- see its own note. */
    ps2_bench_gs_census();
#elif defined(BTRON_AUTO_GUI) && (BTRON_AUTO_GUI == 1)
    ps2_kprintf("[BOOT] AUTO_GUI=1: launching the B-System Desktop workbench\n");
    launch_ps2_desktop_session();
#else
    ps2_kprintf("btron-ps2# ");
#endif

    /* 7. Stage 1 Interactive Terminal Shell Loop */
    int shadow_warned = 0;
#if BTRON_HID_TRACE
    ps2_log_usb_state('1');      /* the queue as enumeration left it */
#endif
    while (1) {
        ps2_pad_poll();
        ps2_usb_poll();
        ps2_kbd_repeat_tick();    /* the prompt's line editor has no event queue to
                                   * drain, so the repeat goes straight to
                                   * ps2_shell_char() from here */
        ps2_ptr_service();      /* stage 1 has no pointer to draw, but the same
                                 * queue has to be spent or it is one big stale
                                 * jump the first time the GUI opens */
        if (ps2_usb_shadow_lost() && !shadow_warned) {
            shadow_warned = 1;
            ps2_kprintf("[USB] IOP took the shadow block back; HID input stopped\n");
        }
        ps2_shell_poll();
#if BTRON_HID_TRACE
        ps2_log_usb_state('1');
#endif
        ps2_delay_cycles(2000);
    }
}

