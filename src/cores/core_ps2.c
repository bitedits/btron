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

#include <btron/types.h>
#include <btron/error.h>
#include <btron/itron.h>
#include <btron/dp.h>
#include <btron/dp_accel.h>
#include <btron/dp_cal.h>
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
#include <libstr.h>

#include "ps2_gs.h"
#include "ps2_sio.h"
#include "ps2_pad.h"
#include "ps2_usb.h"
#include "ps2_iopram.h"

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
static void ps2_kprintf(const char *fmt, ...);

/* 12 MB, because the desktop's own baseline is 5.8 MB and one full-screen GL window
 * costs its surface, the rasterizer's depth plane and the icon atlas twice over
 * (surface + depth + atlas + upload copy) -- 8 MB held the baseline and the surface
 * and nothing else.  The ceiling is the image budget: this array is .bss, and the
 * ASSERT in ps2.ld keeps _end below the stack at 0x01FF0000, which 12 MB does with
 * 2 MB to spare and 16 MB does not. */
#define PS2_HEAP_SIZE (12 * 1024 * 1024)
static uint8_t s_ps2_heap[PS2_HEAP_SIZE] __attribute__((aligned(64)));

#define PS2_HEAP_ALIGN  16u
#define PS2_HEAP_HDR    16u    /* the header's real size, asserted below */

typedef struct ps2_blk {
    struct ps2_blk *next;      /* address order, so neighbours can coalesce */
    size_t          size;      /* payload bytes */
    unsigned        used;
    unsigned        reserve;   /* pads the header to PS2_HEAP_HDR */
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

void* Imalloc(size_t size)
{
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
        s_heap_used += b->size;
        if (s_heap_used > s_heap_peak) s_heap_peak = s_heap_used;

        if (b->size >= PS2_HEAP_LOGLIM) {
            ps2_kprintf("[HEAP] +%u kB -> 0x%x used=%u kB peak=%u kB largest_free=%u kB\n",
                        (unsigned int)(b->size / 1024u), (unsigned int)(uintptr_t)(b + 1),
                        (unsigned int)(s_heap_used / 1024u), (unsigned int)(s_heap_peak / 1024u),
                        (unsigned int)(ps2_heap_largest_free() / 1024u));
        }
        return (void *)(b + 1);
    }

    ps2_kprintf("[HEAP] out of memory asking %u kB: used=%u kB in %u kB, largest free hole %u kB\n",
                (unsigned int)(size / 1024u), (unsigned int)(s_heap_used / 1024u),
                (unsigned int)(PS2_HEAP_SIZE / 1024u),
                (unsigned int)(ps2_heap_largest_free() / 1024u));
    return NULL;
}

void* Icalloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    void *p = Imalloc(total);
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

void* malloc(size_t sz) { return Imalloc(sz); }
void* calloc(size_t n, size_t sz) { return Icalloc(n, sz); }
void  free(void *p) { Ifree(p); }

/* ── Framebuffer & Color Conversion ─────────────────────────────── */

/* 32-bit BTRON Desktop Backbuffer (800x600 @ 32-bit ARGB COLOR) */
static COLOR s_desktop_backbuffer[PS2_SCREEN_WIDTH * PS2_SCREEN_HEIGHT] __attribute__((aligned(128)));

/* Defined below with the console driver, and the source profiles announce
 * themselves before anything reaches it. */
static void ps2_kprintf(const char *fmt, ...);

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
#endif

/* The guest's own px-per-count gain, 8.8 fixed point where 256 is verbatim.  Each
 * source profile installs a default for it and `sens` at the prompt overrides it
 * within a run, which is how any of these claims gets tested.
 *
 * What it is *not* is a fix for a source that is not proportional to the hand --
 * a multiplier can only be right for one speed, and a report whose counts already
 * contain somebody else's acceleration has more than one speed in it.  That case
 * belongs to the profile chosen below, not to a constant. */
#define PS2_MOUSE_MULT_FP 256
static int32_t s_ptr_mult_fp = PS2_MOUSE_MULT_FP;
static int32_t s_ptr_carry_x, s_ptr_carry_y;
static int32_t s_ptr_post_carry_x, s_ptr_post_carry_y;   /* the hw curve's second stage */

/* How far the cursor may travel between two paints, in pixels.
 *
 * Off by default, and that default is the whole lesson of a long tuning session:
 * a cap spends at most PS2_PTR_MAX_STEP px per pass and gives up anything past
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

/* ── Pointer Source Profiles ─────────────────────── */

/* Two stubs, because a count off this bus means something different depending on
 * what is on the other end, and no single guest constant can be right for both.
 * Selected by what actually enumerated, so the shipped binary needs no knowledge
 * of where it is running, and overridable at the prompt for measurement.
 *
 * EMU -- PCSX2's `hidmouse`.  Its X/Y are the host cursor's own movement, which
 * macOS has already accelerated and PCSX2 has already multiplied by [Pad]
 * PointerXScale (8 in the ini this repo runs with, so ~8 counts per host pixel),
 * clamped to +/-127 per event and truncated from a float.  Distance-proportional
 * only if the host's acceleration is off, and never in need of a second curve
 * here -- so this stub takes the emulator's gain back out and then applies a
 * guest-side one, sized below.
 *
 * HW -- a real mouse on a real console: counts proportional to how far the hand
 * moved, at the device's own 10-16 ms interval.  This is the case an accelerator
 * belongs to, and it gets the same RISC OS step curve the arm64/Pi 400 port
 * uses, so the same mouse feels the same on both real machines. */
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

/* Same knob the arm64 port feeds dp_ptr_riscos(), and the Settings > Input panel
 * already writes, so the two real machines share one pointer feel. */
extern int g_mouse_step_mult;

static const char *ps2_ptr_src_name(int src)
{
    return src == PS2_PTRSRC_HW ? "hw" : "emu";
}

/* The live gain, in both units that mean something: a count is what this code
 * multiplies, a host cursor pixel is what a hand is measured against.  Only the
 * emulator's source has a host cursor in its chain, so the hardware row states
 * what its own curve costs instead of a conversion that does not exist.  Ends in
 * a newline so callers can put their own lead-in on it. */
static void ps2_ptr_gain_row(void)
{
    if (s_ptr_src == PS2_PTRSRC_HW)
        ps2_kprintf("%d/256 px per count, RISC OS step %d curve, cap %s\n",
                    (int)s_ptr_mult_fp, g_mouse_step_mult,
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

/* Install a profile's policy.  Kept separate from the choice so detection can run
 * before the first report and a prompt command can run after it with the same
 * effect, and so a switch mid-session cannot leave the previous source's
 * half-spent distance or subpixel remainder behind to be spent as the new one. */
static void ps2_ptr_src_apply(const char *why)
{
    s_ptr_mult_fp = (s_ptr_src == PS2_PTRSRC_HW)
                        ? PS2_MOUSE_MULT_FP
                        : PS2_EMU_MULT_FP;
    s_ptr_max_step = PS2_PTR_MAX_STEP;
    s_ptr_carry_x = s_ptr_carry_y = 0;
    s_ptr_post_carry_x = s_ptr_post_carry_y = 0;
    s_ptr_want_x = s_ptr_want_y = 0;
    s_ptr_defer_x = s_ptr_defer_y = 0;
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
 * Two stages on the hardware side -- the step curve, then `sens` -- because the
 * curve is the feel and `sens` is the speed, and folding the second into the first
 * would take away the only way to set speed without also changing the curve.  At
 * the default 256 the second stage passes whole pixels through untouched. */
static void ps2_ptr_shape(int dx, int dy, int32_t *px, int32_t *py)
{
    if (s_ptr_src == PS2_PTRSRC_HW) {
        const int32_t cx = dp_ptr_riscos(dx, g_mouse_step_mult, &s_ptr_carry_x);
        const int32_t cy = dp_ptr_riscos(dy, g_mouse_step_mult, &s_ptr_carry_y);
        *px = dp_ptr_scale(cx, s_ptr_mult_fp, &s_ptr_post_carry_x);
        *py = dp_ptr_scale(cy, s_ptr_mult_fp, &s_ptr_post_carry_y);
    } else {
        *px = dp_ptr_scale(dx, s_ptr_mult_fp, &s_ptr_carry_x);
        *py = dp_ptr_scale(dy, s_ptr_mult_fp, &s_ptr_carry_y);
    }
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

/* Translates BTRON ARGB (0xAARRGGBB) to PS2 GS CT32 RGBA (Byte 0=R, 1=G, 2=B, 3=A).
 * A rect rather than a row range so that any width can be presented: the band path
 * passes the damaged columns and the bench passes others, which is how the two
 * shapes were priced against each other on the hardware instead of in the source. */
static void blit_backbuffer_rect_to_ps2fb(int x0, int y0, int x1, int y1)
{
    uint32_t *dst = ps2_gs_get_framebuffer();
    const uint32_t *src = (const uint32_t *)s_desktop_backbuffer;

    for (int y = y0; y < y1; y++) {
        const uint32_t i0 = (uint32_t)y * PS2_SCREEN_WIDTH + (uint32_t)x0;
        const uint32_t i1 = (uint32_t)y * PS2_SCREEN_WIDTH + (uint32_t)x1;

        for (uint32_t i = i0; i < i1; i++) {
            uint32_t c = src[i];
            dst[i] = ((c & 0x00FF0000) >> 16) | (c & 0x0000FF00) | ((c & 0x000000FF) << 16) | (c & 0xFF000000);
        }
    }
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

/* CP0 Count ticks at half the 294.912 MHz core clock */
#define EE_TICKS_PER_US 147u
static int s_timebase_ok = 0;

static uint32_t ps2_us_since(uint32_t start)
{
    return (uint32_t)(ps2_count_read() - start) / EE_TICKS_PER_US;
}

/* dp_core.c answers this with a weak stub that returns zero, and this port never
 * overrode it -- which is why every stage of the shared compositor reads 0 in the
 * Pi 400's HUD-style split while the PS2 had only the three numbers measured above.
 * Answering it from the CP0 Count turns the compositor's own attribution on here:
 * background restore, window decoration, application paint, client blit, panel and
 * bars, each of which has a different cure.
 *
 * Count is 32 bits and wraps every 29 seconds at 147 ticks/us, and callers take an
 * absolute difference, so the wrap is folded into a high word rather than leaking a
 * four-billion microsecond maximum into a stage counter.  The fold is 32-bit
 * arithmetic on purpose -- 2^32/147 is 29155706 us, and the residue below one
 * microsecond per wrap costs nothing a 1 ms budget can see -- because this target
 * links no runtime for wider division. */
uint32_t btron_render_perf_us(void)
{
    static uint32_t s_ticks_hi;
    static uint32_t s_ticks_last;
    const uint32_t now = ps2_count_read();

    if (now < s_ticks_last) s_ticks_hi++;
    s_ticks_last = now;
    return s_timebase_ok ? s_ticks_hi * 29155706u + now / EE_TICKS_PER_US : 0u;
}

/* One repaint of a present band, timed by stage.  The three have different cures,
 * so they are never folded into one number: the render walks the damaged part of
 * the desktop, the swap touches one word per pixel of it, and only the upload has
 * ever been measured here (1638 us for the whole canvas at boot).  Both callers use
 * it -- the loop, and the cold paint that 'startx' performs on its own, which is the
 * attribution that needs no hand protocol at all. */
/* One repaint of one present region, timed by stage.  The three have different
 * cures, so they are never folded into one number: the render walks the damaged
 * part of the desktop, the swap touches one word per pixel of it, and the upload is
 * the GIF stream that carries it to the GS.
 *
 * This is the whole present: the loop's bands and ps2_bench_run() both hand it the
 * region's own extents, so the two agree by construction and the bench's rows
 * describe the present the desktop makes rather than a second one. */
static void ps2_paint_region(GDEV *screen, int x0, int y0, int x1, int y1, int full,
                             uint32_t *r_us, uint32_t *s_us, uint32_t *u_us)
{
    const uint32_t t0 = ps2_count_read();

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
#endif

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
    const uint32_t t1 = ps2_count_read();
    blit_backbuffer_rect_to_ps2fb(x0, y0, x1, y1);
    const uint32_t t2 = ps2_count_read();
    ps2_gs_upload(x0, y0, x1 - x0, y1 - y0);
    const uint32_t t3 = ps2_count_read();

    if (s_timebase_ok) {
        *r_us = (t1 - t0) / EE_TICKS_PER_US;
        *s_us = (t2 - t1) / EE_TICKS_PER_US;
        *u_us = (t3 - t2) / EE_TICKS_PER_US;
    } else {
        *r_us = *s_us = *u_us = 0u;
    }
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

static void ps2_kprintf(const char *fmt, ...)
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
    if (s_gui_active) {
        ps2_inject_key((UW)btron_key, down);
        return;
    }
    /* Stage 1 has no event consumer: the prompt builds its line from characters,
     * so a decoded key that only becomes an event is heard and then thrown away.
     * Codes from 0x100 up are the BTRON function and navigation keys, which the
     * console has no binding for yet and which must not be mistaken for a byte. */
    if (down && btron_key < 0x100u) ps2_shell_char((int)btron_key);
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
    if (s_ptr_src == PS2_PTRSRC_HW) {
        ps2_kprintf("[PTG] Note: the hw source puts the RISC OS curve in front of the gain,\n");
        ps2_kprintf("[PTG] so this sets a scale after a speed-dependent step, not the gain itself.\n");
    }
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
        s_ptr_mult_fp = mult;
        /* A new gain makes the old gain's subpixel remainder the wrong shape;
         * leaving it would put a fraction of a pixel of somebody else's stroke
         * onto the next report. */
        s_ptr_carry_x = s_ptr_carry_y = 0;
        s_ptr_post_carry_x = s_ptr_post_carry_y = 0;
        /* The window is spent: it was one deliberate sweep, and repeating it by
         * accident would double the gain. */
        s_ptg_cx = s_ptg_cy = 0;
        ps2_kprintf("[PTG] gain set to %d/256 px per count = %u.%02u px per host px, asked for %u.%02u%s\n",
                    (int)s_ptr_mult_fp, (unsigned int)(got100 / 100u),
                    (unsigned int)(got100 % 100u),
                    (unsigned int)(pct / 100u), (unsigned int)(pct % 100u), note);
        ps2_kprintf("[PTG] To ship this, set PS2_EMU_MULT_FP to the px per count above; the\n");
        ps2_kprintf("[PTG] counts per host px it used to divide by is settled at 1 from PCSX2's source.\n");
        ps2_kprintf("[PTG] `sens` alone only lasts a run.\n");
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

/* Spend the accumulated pointer distance, at most a frame's worth of it.  Called
 * once per pass of each event loop, before the events are dispatched and the
 * screen is painted, so the movement and the paint agree on one position. */
static void ps2_ptr_service(void)
{
    const int32_t ask_x = s_ptr_want_x, ask_y = s_ptr_want_y;
#if BTRON_HID_TRACE
    static uint32_t s_moves;
#endif
    const int x0 = s_mouse_x, y0 = s_mouse_y;
    int32_t px, py;

    s_ptr_want_x = s_ptr_want_y = 0;
    px = dp_ptr_limit(ask_x, s_ptr_max_step, &s_ptr_defer_x);
    py = dp_ptr_limit(ask_y, s_ptr_max_step, &s_ptr_defer_y);
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
        ps2_kprintf("  usb [probe]      - Show OHCI + HID host state (probe = re-measure and re-enumerate)\n");
        ps2_kprintf("  help             - Display this command list\n");
        ps2_kprintf("  info             - Display PS2 system and hardware specifications\n");
        ps2_kprintf("  apps             - List all desktop applications\n");
        ps2_kprintf("  open <app>       - Open app: cabinet, editor, terminal, sound, chat, settings\n");
        ps2_kprintf("  tip [mode]       - Set or cycle TIP/IME (ascii, hira, kata, tibetan)\n");
        ps2_kprintf("  tasks            - List active RTOS tasks\n");
        ps2_kprintf("  mem              - Show memory breakdown\n");
        ps2_kprintf("  status           - Show mouse position, TIP mode, and system status\n");
        ps2_kprintf("  mouse <x> <y>    - Move cursor to absolute position (0..800, 0..600)\n");
        ps2_kprintf("  move <dx> <dy>   - Move cursor relative by (dx, dy)\n");
        ps2_kprintf("  sens [pct]       - Show or set the USB pointer scale (1..400%%, 100 = as reported)\n");
        ps2_kprintf("  maxstep [px]     - Show or set the pointer's max px per paint (0 = no cap)\n");
        ps2_kprintf("  ptrsrc [emu|hw|auto] - Pointer policy: emulator device, real mouse, or re-detect\n");
        ps2_kprintf("  ptrstat <tag>    - Print+reset pointer counts, paint cost, and mouse->screen ms\n");
        ps2_kprintf("  ptrcal           - Loopback-calibrate the pointer path (no mouse needed)\n");
        ps2_kprintf("  ptgain [w] [pct] - Gain from a host sweep of width w (pct = px/host px x100)\n");
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
            s_ptr_mult_fp = (int32_t)pct * 256 / 100;
            /* The leftover belongs to the scale that made it. */
            s_ptr_carry_x = s_ptr_carry_y = 0;
            s_ptr_post_carry_x = s_ptr_post_carry_y = 0;
        }
        ps2_kprintf("[PS2] Pointer scale %d%%, %s source%s: ",
                    (int)(s_ptr_mult_fp * 100 / 256), ps2_ptr_src_name(s_ptr_src),
                    s_ptr_src_forced ? "" : " default");
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
        ps2_kprintf("[PS2] Pointer cap %d px per paint%s\n",
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
         * log copied out of a session is incomplete without the name on it. */
        ps2_kprintf("[CAL] Pointer loopback: counts in, pixels out, no hand involved.\n");
        ps2_kprintf("[CAL] source=%s curve=%s step=%d\n", ps2_ptr_src_name(s_ptr_src),
                    s_ptr_src == PS2_PTRSRC_HW ? "riscos" : "none",
                    g_mouse_step_mult);
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
        ps2_kprintf("[CAL] scale=%d%% px per count, cap=%d px per paint. DRIFT is where the\n"
                    "[CAL] pointer ended after equal and opposite counts; DIAG asks both axes\n"
                    "[CAL] the same question.\n",
                    (int)(s_ptr_mult_fp * 100 / 256), (int)s_ptr_max_step);
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

void btron_core_hfds_log(void) {
    ps2_kprintf("[HFDS] Real Body storage init [OK]  BTRON3_SPEC.TAD  Readme.tad  Cabinet.vobj\n");
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
    ps2_kprintf("[BENCH]           compositor: bg=%u(%u px) frame=%u paint=%u blit=%u(%u px) comp=%u bfn=%u wins=%u/%u\n",
                (unsigned int)st.bg_us, (unsigned int)st.bg_worst_px,
                (unsigned int)st.frame_us, (unsigned int)st.paint_us,
                (unsigned int)st.blit_us, (unsigned int)st.blit_worst_px,
                (unsigned int)st.comp_us, (unsigned int)st.bg_full_calls,
                (unsigned int)st.wins_drawn, (unsigned int)st.wins_walked);
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
     * refresh, in both directions, from the same machine. */
    ps2_kprintf("[BENCH]           us/pass render %u swap %u upload %u total %u  -> %u passes/s\n",
                (unsigned int)(r_sum / passes), (unsigned int)(s_sum / passes),
                (unsigned int)(u_sum / passes),
                (unsigned int)((r_sum + s_sum + u_sum) / passes),
                (unsigned int)(ps2_count_read() > t_start
                                   ? passes * 147000u / ps2_us_since(t_start) : 0u));
    btron_render_stats_take(&st);
    ps2_kprintf("[BENCH]           compositor: bg=%u(%u px) frame=%u paint=%u blit=%u(%u px) comp=%u bfn=%u wins=%u/%u\n",
                (unsigned int)st.bg_us, (unsigned int)st.bg_worst_px,
                (unsigned int)st.frame_us, (unsigned int)st.paint_us,
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
 * desktop composes.  COLOR_WHITE and COLOR_BLACK both survive the ARGB -> RGBA byte
 * swap the present does, so one test reads either surface. */
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
    int hb[8], hf[8];
    int bare_bb = 0, bare_fb = 0, elsewhere = 0, append_failed = 0;

    ps2_kprintf("\n[BENCH] cursor presence -- where, if anywhere, is the sprite drawn?\n");
    /* The two surfaces by address, so that a per-region `pixels=` below can be compared
     * with the one this phase reads without another build. */
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
        bare_fb = ps2_cursor_sig_at(fb, (int)bmx, (int)bmy);

        const int nb = bare_bb ? 0 : ps2_cursor_scan(bb, hb, 4);
        const int nf = bare_fb ? 0 : ps2_cursor_scan(fb, hf, 4);
        elsewhere += (!bare_bb && nb > 0);

        ps2_kprintf("[BENCH] cursor: park %d,%d drawn=%d,%d mouse=%d,%d shown=%d,%d in_bb=%d | sig at drawn: bb=%d fb=%d\n",
                    x, y, (int)bmx, (int)bmy, s_mouse_x, s_mouse_y, s_shown_x, s_shown_y,
                    g_cursor_in_backbuffer, bare_bb, bare_fb);
        /* A whole-canvas walk is only worth its cost when the sprite is not where the
         * port says it drew it: then the row names the places it is instead. */
        if (!bare_bb || !bare_fb) {
            ps2_kprintf("[BENCH] cursor: canvas hits bb=%d(%d,%d %d,%d %d,%d %d,%d) fb=%d(%d,%d %d,%d %d,%d %d,%d)\n",
                        nb, hb[0], hb[1], hb[2], hb[3], hb[4], hb[5], hb[6], hb[7],
                        nf, hf[0], hf[1], hf[2], hf[3], hf[4], hf[5], hf[6], hf[7]);
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
                (bare_bb && bare_fb)
                    ? "sprite is at the drawn position in BOTH surfaces -- the guest paints and presents it, so an invisible cursor is downstream of this port's paint"
                    : (bare_bb ? "sprite is in the backbuffer but not in the GS frame buffer -- the present region or the blit misses it"
                               : (append_failed ? "the append ran and wrote nothing -- a five-argument call into desktop.c whose fifth argument arrived corrupted, which is what a mixed-MIPS-ABI link does (see the ABI note in the Makefile)"
                                                : (elsewhere ? "sprite is on the canvas but not at the position the append read back -- the two coordinate sources disagree"
                                                             : "sprite is nowhere on the canvas -- it is not being drawn"))));
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

/* The blit's own ARGB->CT32 transform, so a backbuffer pixel and the frame buffer
 * pixel it became can be compared byte for byte. */
static uint32_t ps2_bench_swizzle(uint32_t c)
{
    return ((c & 0x00FF0000u) >> 16) | (c & 0x0000FF00u) |
           ((c & 0x000000FFu) << 16) | (c & 0xFF000000u);
}

/* ── Launcher ink: is a row drawn, and is what was drawn presented? ──────────
 *
 * "xmb is not added to start launch" is a claim about the screen, and the guest can
 * test the two halves of it separately with the same method that closed the cursor
 * thread: read the compositor's canvas and the frame buffer the GS is scanning, over
 * each row band.  A band with ink in bb and no ink in fb is a present that missed.
 * A band with no ink in either is a row that was never drawn -- and a band that is
 * blank here while the item table has a label for it means the label, not the row,
 * is what the user cannot find.
 *
 * The pointer is parked far outside the menu first.  Motion alone does not dismiss
 * the launcher (only a press does, tracker_handle_mouse_down), and a hover-filled
 * NAVY row would report ink for its fill rather than for its glyphs. */
static void ps2_bench_menu_ink(GDEV *screen, int picture_row_a, int picture_row_b)
{
    const TRACKER *t = tracker_get_state();
    const uint32_t *bb = (const uint32_t *)s_desktop_backbuffer;
    const uint32_t *fb = ps2_gs_get_framebuffer();
    int moves, buttons, bx = -1, by = -1;
    int blank = 0, stale_total = 0;

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
        int ink_bb = 0, ink_fb = 0, stale = 0;

        for (int y = y0; y < y0 + TRACKER_ITEM_HEIGHT && y < PS2_SCREEN_HEIGHT; y++) {
            for (int x = x0; x < x1 && x < PS2_SCREEN_WIDTH; x++) {
                const uint32_t b = bb[(uint32_t)y * PS2_SCREEN_WIDTH + (uint32_t)x];
                const uint32_t f = fb[(uint32_t)y * PS2_SCREEN_WIDTH + (uint32_t)x];
                if (b != (uint32_t)COLOR_WHITE) ink_bb++;
                if (f != (uint32_t)COLOR_WHITE) ink_fb++;
                if (f != ps2_bench_swizzle(b)) stale++;
            }
        }
        ps2_kprintf("[BENCH] menu ink: r%-2d type=%-2d y=%-3d bb_ink=%-5d fb_ink=%-5d stale=%d\n",
                    i, (int)t->items[i].type, y0, ink_bb, ink_fb, stale);
        if (t->items[i].type != TRACKER_CMD_SEPARATOR && i != t->hover_index && ink_bb == 0) blank++;
        stale_total += stale;

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
                    : (stale_total > 0
                        ? "every row is drawn, and some band of the frame buffer does not match the canvas -- the present misses part of the menu"
                        : "every labelled row carries ink in the canvas and the frame buffer matches it pixel for pixel -- the launcher rows are on the display surface"));
    ps2_kprintf("[BENCH] menu ink: blank_rows=%d stale_pixels=%d\n", blank, stale_total);
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

extern WND *open_xmb_window(void);

/* "xmb still black" measured instead of reported: xb_paint() renders through the GL
 * backend into the window's own device pixels, so a black bar means the rasterizer
 * wrote nothing there.  open_xmb_window() is idempotent through its live handle, so
 * asking it for the window mid-loop costs nothing but a top_wnd(). */
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
            const uint32_t v = dev->pixels[(uint32_t)y * (uint32_t)dev->width + (uint32_t)x];
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

static void ps2_bench_app(GDEV *screen, uint32_t frames)
{
    RENDER_STATS st;
    const uint32_t t_start = btron_render_perf_us();  /* folded: one frame exceeds a Count wrap */
    WND *w;

    ps2_kprintf("\n[BENCH] xmb opened through the task shim -- %u frames asked of dly_tsk()\n",
                (unsigned int)frames);
    btron_render_stats_take(&st);       /* the region table's counters are not this row's */
    s_bench_app_active = 1;
    s_bench_app_left = frames;
    s_bench_app_made = 0u;
    s_bench_app_t0 = t_start;
    /* Blocks until the injected Escape has closed the window: the frame loop is the
     * body's from here, not the session loop's.  The two heap rows are the pool's
     * state either side of open_xmb_window(), largest hole included -- a GL surface
     * needs one contiguous block, so "bytes free" alone can be an innocent answer. */
    const char *pool_bad = ps2_heap_check();
    ps2_kprintf("[BENCH] heap before xmb: used=%u of %u kB, largest hole %u kB, pool %s\n",
                (unsigned int)(s_heap_used / 1024u), (unsigned int)(PS2_HEAP_SIZE / 1024u),
                (unsigned int)(ps2_heap_largest_free() / 1024u),
                pool_bad ? pool_bad : "sound");
    if (pool_bad) ps2_heap_dump();
    w = open_xmb_window();
    s_bench_app_active = 0;
    s_bench_app_left = 0u;
    pool_bad = ps2_heap_check();
    ps2_kprintf("[BENCH] heap after  xmb: used=%u of %u kB, largest hole %u kB (peak %u kB), pool %s\n",
                (unsigned int)(s_heap_used / 1024u), (unsigned int)(PS2_HEAP_SIZE / 1024u),
                (unsigned int)(ps2_heap_largest_free() / 1024u),
                (unsigned int)(s_heap_peak / 1024u),
                pool_bad ? pool_bad : "sound");
    if (pool_bad) ps2_heap_dump();

    {
        /* passes * 1e6 / us, and not the tick form ps2_bench_move() uses: 60 frames
         * already overflow a 32-bit numerator at 147 ticks per microsecond. */
        const uint32_t us = btron_render_perf_us() - t_start;
        /* `<=` because the fold loses a Count wrap for every 29.1 s of blocked time
         * between its calls, and a frame of this app is longer than that: the elapsed
         * column is short, so the derived rate is an upper bound, not a measurement. */
        ps2_kprintf("[BENCH] xmb: open returned %s, %u passes in %u ms folded -> <= %u passes/s, window still %s\n",
                    w ? "with a window" : "with no window",
                    (unsigned int)s_bench_app_made,
                    (unsigned int)(us / 1000u),
                    (unsigned int)(us ? s_bench_app_made * 1000000u / us : 0u),
                    get_top_wnd() ? "up" : "closed");
    }
    btron_render_stats_take(&st);
    ps2_kprintf("[BENCH]           compositor: bg=%u(%u px) frame=%u paint=%u blit=%u(%u px) comp=%u bfn=%u wins=%u/%u\n",
                (unsigned int)st.bg_us, (unsigned int)st.bg_worst_px,
                (unsigned int)st.frame_us, (unsigned int)st.paint_us,
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
    s_bench_app_active = 1;
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

#endif /* BTRON_PS2_BENCH_APP */

static void ps2_bench_run(GDEV *screen)
{
    static const struct { const char *tag; int dx, dy; } mv[] = {
        { "h1",   1,   0 }, { "h4",   4,  0 }, { "h16",  16,  0 },
        { "h64", 64,   0 }, { "h127", 127, 0 },
        { "v1",   0,   1 }, { "v4",   0,  4 }, { "v16",   0, 16 },
        { "v64",  0,  64 }, { "v127", 0, 127 },
    };
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
    ps2_bench_app(screen, 60u);
#endif

    /* Runs last of the phases, so nothing downstream inherits the windows it opens;
     * see the phase's own note on open_xmb_window()'s static handle. */
    ps2_bench_launch(screen);

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

    ps2_pad_poll();
    ps2_usb_poll();
    /* After the poll and before the dispatch: the reports the drain just
     * turned into queued distance become one frame's movement here, and the
     * move event this raises is what puts the cursor's rows in the set below,
     * so a pass that spends pointer distance is also a pass that repaints it.
     * That is what makes "px per pass" the speed the hand sees. */
    ps2_ptr_service();
    ps2_shell_poll();

#if BTRON_PS2_BENCH_APP
    /* The app phase's frame budget, spent here rather than in the phase itself
     * because this is the only place a task-driven pass is known to have happened.
     * Injected before the dispatch below, so the key is consumed by the pass that
     * owes it and the body returns without one more frame. */
    if (s_bench_app_active) {
        s_bench_app_made++;
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
        /* Twice, because a surface that is black at frame 3 and full at frame 30 is a
         * renderer that needed its textures baked, and one black both times never drew. */
        if (s_bench_app_made == 3u)  ps2_bench_xmb_surface("frame 3 ");
        if (s_bench_app_made == 30u) ps2_bench_xmb_surface("frame 30");
    }
    if (s_bench_app_left && --s_bench_app_left == 0) {
        ps2_inject_key(BTRON_KEY_ESCAPE, 1);
        ps2_inject_key(BTRON_KEY_ESCAPE, 0);
    }
#endif

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
        if (ev.type != EV_MOUSE_MOVE) force_full = 1;
    }

#if BTRON_HID_TRACE
    ps2_log_usb_state('G');
#endif

    ps2_bands_for_pass(&set);

    /* Two timers, because two different things go stale on their own.
     *
     * The panel's clock changes with no event and no damage at all, and it owns
     * 28 rows, so it is owed those rows every 200 ms.  Whole canvas is owed to
     * anything a banded present could have got wrong -- an app that paints
     * without invalidating, a hover highlight inside a window -- and it is
     * convergence, not animation, so two seconds of it is invisible.
     *
     * Paced on the clock and not on a pass count because a banded pass is cheap
     * enough that the loop now makes very many of them a second, and every
     * sixtieth pass would be a repaint several times over.  With no Count to
     * read, both nets are skipped: the present is then driven purely by damage,
     * which is correct for anything that invalidates, and the only loss is a
     * clock that stops advancing. */
    if (s_timebase_ok) {
        const uint32_t now = ps2_count_read();
        if ((uint32_t)(now - s_pass_panel_due) >= (uint32_t)(200u * EE_TICKS_PER_US)) {
            ps2_bandset_add(&set, 0, PS2_SCREEN_WIDTH, 0, PS2_PANEL_ROWS);
            s_pass_panel_due = now;
        }
        if ((uint32_t)(now - s_pass_full_due) >= (uint32_t)(2000u * EE_TICKS_PER_US)) {
            force_full = 1;
            s_pass_full_due = now;
        }
    }

    /* An overlay -- the global menu, a tracker dropdown, the IME candidate
     * window -- is drawn by the whole-canvas composite only, and the IME's is
     * drawn at the caret rather than at a rect anyone tracks.  With any of them
     * up there is no band worth arguing about, so paint the canvas. */
    if (force_full || set.overflow || global_menu_is_open() ||
        tracker_is_menu_open() || wnd_mgr_is_interacting() ||
        tip_get_state() == TIP_STATE_CONVERTING ||
        tip_get_state() == TIP_STATE_CANDIDATE_SELECT) {
        ps2_bandset_full(&set);
    }

    if (!set.n) {
        ps2_delay_cycles(1000);
		return;			/* nothing for the GS to be told */
    }

    {
        uint32_t r_us, s_us, u_us, px;
        uint32_t lat = 0;
        int lat_ok = 0;
        const int dom = s_paint_owed ? s_paint_dom : -1;

        ps2_paint_bands(screen, &set, &r_us, &s_us, &u_us, &px);

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
        ps2_paint_note(px, set.n, ps2_band_is_full(&set.b[0]), dom,
                       r_us, s_us, u_us, lat, lat_ok);
        if (ps2_band_is_full(&set.b[0])) s_pass_full_due = ps2_count_read();
    }
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
} PS2_TASK;

static PS2_TASK  s_ps2_tasks[PS2_MAX_TASKS];
static uint32_t  s_task_ms;          /* ms since the desktop session, folded for Count wrap */
static uint32_t  s_task_us_rem;      /* µs not yet worth a millisecond, always < 1000 */
static uint32_t  s_task_count_last;
static int       s_task_clock_running;
static int       s_task_driving;     /* body depth, 0 while the session loop drives */

ID cre_tsk(const T_CTSK *pk_ctsk)
{
    if (!pk_ctsk || !pk_ctsk->task) return E_PAR;
    for (int i = 0; i < PS2_MAX_TASKS; i++) {
        if (s_ps2_tasks[i].active) continue;
        s_ps2_tasks[i].config = *pk_ctsk;
        s_ps2_tasks[i].active = 1;
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
    s_task_driving++;
    t->config.task(t->config.exinf);
    s_task_driving--;
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
                (uint32_t)(now - s_task_count_last) / EE_TICKS_PER_US + s_task_us_rem;
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
 * are both about being able to see the screen: ms * 147 * 1000 overflows the 32-bit
 * compare past ~14 s, and no pass -- so no USB poll, no pointer, no present -- runs
 * while a body holds, which makes a long delay a black screen rather than a slow one.
 * With no Count to read there is nothing to time against, so the pass is not held. */
static void ps2_task_hold(uint32_t ms)
{
    if (!s_timebase_ok || ms == 0) return;
    if (ms > 250u) ms = 250u;
    const uint32_t start  = ps2_count_read();
    const uint32_t budget = ms * (1000u * EE_TICKS_PER_US);
    while ((uint32_t)(ps2_count_read() - start) < budget)
        ps2_delay_cycles(200u);
}

/* One frame for a task body: present what it just invalidated, then give back the
 * time it asked for.  Called with s_gui_screen unset only if a body is started before
 * 'startx' has built the desktop, and such a body gets its delay without a screen. */
static void ps2_task_dly(W dlytim)
{
    if (s_gui_screen) ps2_gui_pass(s_gui_screen);
    ps2_task_hold(dlytim > 0 ? (uint32_t)dlytim : 0u);
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

    ps2_kprintf("[PS2] Real B-System Workbench rendered via Host->Local GIF DMA (800x600).\n");
    ps2_kprintf("[PS2] Controls: Mouse/Pad/Kbd.  [Ctrl]+[Q] returns to this console.\n");
    ps2_kprintf("[PS2] 'ptrstat' prints the pointer and paint numbers, and runs at the\n");
    ps2_kprintf("[PS2] Stage 1 prompt only -- so the session's own totals print on the way out.\n");
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
    ps2_kprintf("[CPU] CP0 Count %s  (%u ticks/us)\n",
                s_timebase_ok ? "running" : "STOPPED, counted delays",
                (unsigned int)EE_TICKS_PER_US);
}

static void ps2_log_boot_tail(int hid_devs)
{
    ps2_kprintf("[USB] probe pass took %u us\n", (unsigned int)s_ohci_probe_us);
    ps2_kprintf("[SIO] SIO0 console is write-only here: PCSX2 wires no host terminal to it\n");
    ps2_kprintf("[PAD] DualShock decoder ready, no SIF/IOP pad client yet  input: %d USB HID\n",
                hid_devs);
}

/* ── Kernel Entry & Stage 1 Dispatch ────────────────────────────── */

void ps2_kernel_main(void)
{
    /* 1. SIO0 first: every row below is mirrored to it, and it is also the only
     *    way to see a hang that happens before the GS console exists. */
    ps2_sio_init();

    /* 2. Establish the timebase before anything is timed. */
    uint32_t c0 = ps2_count_read();
    for (volatile int i = 0; i < 20000; i++) { }
    s_timebase_ok = (ps2_count_read() != c0);

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

    /* 5. Banner last, so the prompt is the bottom row and nothing interesting
     *    scrolls away underneath it. */
    btron_core_banner();
    ps2_kprintf("\n");

#if defined(BTRON_AUTO_GUI) && (BTRON_AUTO_GUI == 1)
    ps2_kprintf("[BOOT] AUTO_GUI=1: launching the B-System Desktop workbench\n");
    launch_ps2_desktop_session();
#else
    ps2_kprintf("btron-ps2# ");
#endif

    /* 6. Stage 1 Interactive Terminal Shell Loop */
    int shadow_warned = 0;
#if BTRON_HID_TRACE
    ps2_log_usb_state('1');      /* the queue as enumeration left it */
#endif
    while (1) {
        ps2_pad_poll();
        ps2_usb_poll();
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

