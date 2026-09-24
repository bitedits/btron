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

/* ── Kernel Heap Allocator (8 MB RDRAM pool) ────────────────────── */
#define PS2_HEAP_SIZE (8 * 1024 * 1024)
static uint8_t s_ps2_heap[PS2_HEAP_SIZE] __attribute__((aligned(128)));
static size_t  s_ps2_heap_offset = 0;

uint32_t heap_ptr = 0x00200000;

void* Imalloc(size_t size)
{
    if (size == 0) return NULL;
    size = (size + 15) & ~15; /* 16-byte alignment */
    if (s_ps2_heap_offset + size > PS2_HEAP_SIZE) {
        return NULL;
    }
    void *ptr = &s_ps2_heap[s_ps2_heap_offset];
    s_ps2_heap_offset += size;
    return ptr;
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
    (void)ptr;
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
static int s_gui_active = 0;

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
 * here -- so this stub divides the emulator's gain back out and stops.
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

/* The emulator's own per-axis pointer gain, as set in its ini.  The default guest
 * multiplier divides it back out, so 1 host cursor px becomes 1 guest px. */
#define PS2_EMU_POINTER_SCALE 8

static int s_ptr_src = PS2_PTRSRC_EMU;
static int s_ptr_src_forced;      /* a `ptrsrc` word outranks detection */

/* Same knob the arm64 port feeds dp_ptr_riscos(), and the Settings > Input panel
 * already writes, so the two real machines share one pointer feel. */
extern int g_mouse_step_mult;

static const char *ps2_ptr_src_name(int src)
{
    return src == PS2_PTRSRC_HW ? "hw" : "emu";
}

/* Install a profile's policy.  Kept separate from the choice so detection can run
 * before the first report and a prompt command can run after it with the same
 * effect, and so a switch mid-session cannot leave the previous source's
 * half-spent distance or subpixel remainder behind to be spent as the new one. */
static void ps2_ptr_src_apply(const char *why)
{
    s_ptr_mult_fp = (s_ptr_src == PS2_PTRSRC_HW)
                        ? PS2_MOUSE_MULT_FP
                        : PS2_MOUSE_MULT_FP / PS2_EMU_POINTER_SCALE;
    s_ptr_max_step = PS2_PTR_MAX_STEP;
    s_ptr_carry_x = s_ptr_carry_y = 0;
    s_ptr_post_carry_x = s_ptr_post_carry_y = 0;
    s_ptr_want_x = s_ptr_want_y = 0;
    s_ptr_defer_x = s_ptr_defer_y = 0;
    ps2_kprintf("[PS2] Pointer source %s (%s): %d/256 px per count, %s curve, cap %s\n",
                ps2_ptr_src_name(s_ptr_src), why, (int)s_ptr_mult_fp,
                s_ptr_src == PS2_PTRSRC_HW ? "RISC OS step" : "no",
                s_ptr_max_step > 0 ? "on" : "off");
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

/* Translates BTRON ARGB (0xAARRGGBB) to PS2 GS CT32 RGBA (Byte 0=R, 1=G, 2=B, 3=A) */
static void blit_backbuffer_to_ps2fb(void)
{
    uint32_t *dst = ps2_gs_get_framebuffer();
    const uint32_t *src = (const uint32_t *)s_desktop_backbuffer;
    for (int i = 0; i < PS2_SCREEN_WIDTH * PS2_SCREEN_HEIGHT; i++) {
        uint32_t c = src[i];
        dst[i] = ((c & 0x00FF0000) >> 16) | (c & 0x0000FF00) | ((c & 0x000000FF) << 16) | (c & 0xFF000000);
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

/* ── Kernel Printf via SIO0 and GS Console ──────────────────────── */

/* CP0 Count ticks at half the 294.912 MHz core clock */
#define EE_TICKS_PER_US 147u
static int s_timebase_ok = 0;

static uint32_t ps2_us_since(uint32_t start)
{
    return (uint32_t)(ps2_count_read() - start) / EE_TICKS_PER_US;
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
    snd_evt(&ev);
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
    /* Left Mouse Button (Cross) */
    if (newly_pressed & PAD_CROSS)   ps2_click_mouse(1, 1);
    if (newly_released & PAD_CROSS)  ps2_click_mouse(1, 0);

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

    /* D-Pad: Discrete Navigation & Cursor displacement */
    if (newly_pressed & PAD_UP) {
        ps2_move_mouse(0, -16);
        ps2_inject_key(BTRON_KEY_UP, 1);
        ps2_inject_key(BTRON_KEY_UP, 0);
    }
    if (newly_pressed & PAD_DOWN) {
        ps2_move_mouse(0, 16);
        ps2_inject_key(BTRON_KEY_DOWN, 1);
        ps2_inject_key(BTRON_KEY_DOWN, 0);
    }
    if (newly_pressed & PAD_LEFT) {
        ps2_move_mouse(-16, 0);
        ps2_inject_key(BTRON_KEY_LEFT, 1);
        ps2_inject_key(BTRON_KEY_LEFT, 0);
    }
    if (newly_pressed & PAD_RIGHT) {
        ps2_move_mouse(16, 0);
        ps2_inject_key(BTRON_KEY_RIGHT, 1);
        ps2_inject_key(BTRON_KEY_RIGHT, 0);
    }
}

/* One keystroke into the prompt, from either source.  Defined below with the
 * shell's line editor and forward-declared because the USB hook above it needs
 * it: the decoder runs wherever the poll loop happens to notice a report, not
 * inside the SIO read. */
static void ps2_shell_char(int c);

void ps2_usb_on_key(uint32_t btron_key, int down)
{
#if BTRON_HID_TRACE
    /* The decoder already ran, so this row says the router heard the key and
     * which of its three exits it took: quit the GUI, queue an event, or feed
     * the prompt.  A [KBD] row with no [KEY] row after it is a build whose
     * decoder is not this one. */
    ps2_kprintf("[KEY] k=%x d=%d gui=%d\n", btron_key, down, s_gui_active);
#endif
    if (s_gui_active && down) {
        if (btron_key == BTRON_KEY_ESCAPE || btron_key == 'q' || btron_key == 'Q') {
            s_gui_active = 0;
            return;
        }
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
} ps2_ptrst_t;

static ps2_ptrst_t s_ptrst;

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
        ps2_ptr_shape(dx, dy, &px, &py);
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
    int32_t px, py;

    s_ptr_want_x = s_ptr_want_y = 0;
    px = dp_ptr_limit(ask_x, s_ptr_max_step, &s_ptr_defer_x);
    py = dp_ptr_limit(ask_y, s_ptr_max_step, &s_ptr_defer_y);
    if (!px && !py) return;
    ps2_move_mouse((int)px, (int)py);

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
        ps2_kprintf("  ptrstat <tag>    - Print+reset the pointer counts seen since the last call\n");
        ps2_kprintf("  ptrcal           - Loopback-calibrate the pointer path (no mouse needed)\n");
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
        ps2_kprintf("Kernel Heap :  8388608 bytes (8 MB, used: %u)\n", (unsigned int)s_ps2_heap_offset);
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
        ps2_kprintf("[PS2] Pointer scale %d%% = %d/256 px per count (%s source%s)\n",
                    (int)(s_ptr_mult_fp * 100 / 256), (int)s_ptr_mult_fp,
                    ps2_ptr_src_name(s_ptr_src),
                    s_ptr_src_forced ? "" : ", default for this source");
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
            ps2_kprintf("[PS2] Pointer source %s (%s)\n",
                        ps2_ptr_src_name(s_ptr_src),
                        s_ptr_src_forced ? "forced" : "detected");
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
    ps2_kprintf("[MEM] 8 MB kernel heap pool, %u bytes used  canvas 0x%08x (uncached alias 0x%08x)\n",
                (unsigned int)s_ps2_heap_offset,
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

ER tk_dly_tsk(W dlytim) {
    (void)dlytim;
    return E_OK;
}

/* ── Stage 2: Authentic B-System Graphical Workbench Session ────── */

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
    workbench_init(PS2_SCREEN_WIDTH);

    /* Initial paint & blit to GS eDRAM */
    workbench_render(screen, PS2_SCREEN_WIDTH, PS2_SCREEN_HEIGHT);
    blit_backbuffer_to_ps2fb();
    ps2_gs_flush();

    ps2_kprintf("[PS2] Real B-System Workbench rendered via Host->Local GIF DMA (800x600).\n");
    ps2_kprintf("[PS2] Controls: Mouse/Pad/Kbd. Type 'exit' in shell or press [Esc]/[Q] to return.\n");
    ps2_kprintf("btron-ps2> ");

    /* Interactive Event Loop */
    uint32_t ticks = 0;
    EVT ev;

    while (s_gui_active) {
        int need_redraw = 0;

        ps2_pad_poll();
        ps2_usb_poll();
        /* After the poll and before the dispatch: the reports the drain just
         * turned into queued distance become one frame's movement here, and the
         * move event this raises is what sets need_redraw below, so a pass that
         * spends pointer distance is also a pass that repaints it.  That is what
         * makes "px per pass" the speed the hand sees. */
        ps2_ptr_service();
        ps2_shell_poll();

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
#endif
            workbench_process_event(screen, &ev);
            need_redraw = 1;
        }

#if BTRON_HID_TRACE
        ps2_log_usb_state('G');
#endif

        ticks++;
        if ((ticks % 60) == 0) {
            need_redraw = 1;
        }

        if (need_redraw) {
            workbench_render(screen, PS2_SCREEN_WIDTH, PS2_SCREEN_HEIGHT);
            blit_backbuffer_to_ps2fb();
            ps2_gs_flush();
        }

        ps2_delay_cycles(1000);
    }

    /* Clean return to Stage 1 text console */
    ps2_gs_text_clear();
    ps2_kprintf("[DESK] Exited 800x600 workbench, back to the Stage 1 console. 'startx' again anytime.\n");
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
 * than as arguments because the driver is one of the -march=mips3 objects and
 * this file is -march=mips2: those two disagree about the stack slots a call of
 * more than four arguments uses, and the first rows off this printed the fifth
 * argument as 0xffffff80.
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

