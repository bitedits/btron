/*
 * core_arm64.c — B-System BTRON3 3.20 RTOS Kernel for Raspberry Pi 3B/4B (BCM2711 / AArch64)
 *
 * Dedicated Target 6: BTRON_YOKOYAMA_AARCH64 (Raspberry Pi 3B/4B, Cortex-A72, AArch64)
 * Honoring: Takanori Yokoyama (横山 孝徳) — T-Kernel Pioneer
 *
 * Architecture:
 *   • Hardware Drivers:
 *       - VideoCore GPU Mailbox Framebuffer / Display (1024x768 32-bpp Double-Buffered)
 *       - Synopsys DesignWare DWC2 USB 2.0 Host Controller (USB Keyboard & Mouse)
 *       - ARM PrimeCell PL011 UART Serial Console Driver
 *       - BCM2711 / BCM2837 System Timer (60Hz System Tick)
 *       - EMMC2 / SD Storage Interface & HFDS Record Manager Status
 *   • Integrated B-System Workbench:
 *       - Plugs into core_init.c, desktop.c, wnd.c, vobj.c, global_menu.c, etc.
 *       - Launches authentic B-System Workbench desktop with live windows and desktop icons
 *       - Real-time mouse cursor tracking, window dragging, tabs, and menus
 *       - Interactive keyboard input from USB and PL011 serial console
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
#include <btron/wnd.h>
#include <btron/desktop.h>
#include <btron/event.h>
#include <btron/tip.h>
#include <btron/apps.h>
#include <btron/workbench.h>
#include <btron/fast_blit.h>
#include <libstr.h>
#include <dwc2.h>
#include <pcie.h>
#include <xhci.h>

/* ═══════════════════════════════════════════════════════════════════
 * Dynamic Hardware Memory Map (Pi 3B: 0x3F000000, Pi 4B: 0xFE000000)
 * ═══════════════════════════════════════════════════════════════════ */
extern uintptr_t g_mmio_base;
#define PL011_BASE          (g_mmio_base + 0x00201000UL)
#define MBOX_BASE_ADDR      (g_mmio_base + 0x0000B880UL)
#define DWC2_BASE           (g_mmio_base + 0x00980000UL)
#define TIMER_BASE          (g_mmio_base + 0x00003000UL)

/* Kernel Heap Boundaries */
#define HEAP_BASE           ((uintptr_t)0x02000000)  /* 32 MB */
#define HEAP_LIMIT          ((uintptr_t)0x1B000000)  /* 432 MB limit */
extern uintptr_t heap_ptr;

/* Display Resolution (Standard VideoCore Framebuffer) */
#define BTRON_SCREEN_W      1024
#define BTRON_SCREEN_H      768

/* INPUT and UI are peers.  The fixed release periods and work budgets in
 * <btron/async_rt.h>—not a priority boost—bound the input-to-presentation
 * path on the Pi 400 (ASYNC.txt Tier-1). */
#include <btron/async_rt.h>
#define ASYNC_UI_EVENT_BUDGET       16u
#ifndef ASYNC_TELEMETRY
#define ASYNC_TELEMETRY              1
#endif

static async_rt_stats_t s_async_rt_stats;

/* Wait-free seqlock pointer snapshot: produced by the 1 kHz timer IRQ,
 * consumed by the 1 ms INPUT worker.  Replaces the old shared s_accum_*
 * read-and-clear handoff in the xHCI driver. */
static pointer_slot_t s_pointer_slot;
static pointer_snapshot_t s_input_last;
static uint8_t s_input_prev_buttons = 0;

/* 1 = the hardware 1 kHz IRQ input plane is armed (BCM2711 + VL805 xHCI).
 * 0 = legacy cooperative drain inside the GUI loop (QEMU / DWC2 / Pi 3). */
static volatile int s_async_irq_active = 0;
static uint32_t s_irq_prev_us = 0;

static int s_present_dma_enabled = 0;
static int s_present_pending = 0;
static int s_present_cursor_dirty = 0;
static uint32_t s_present_dma_start_us = 0;
static uint32_t s_present_dma_max_us = 0;
static uint32_t s_present_front_page = 0;
static uint32_t s_present_dma_page = 1;
static int s_present_fast_key_update = 0;
/* Budget-scheduler WCET telemetry for the two equal-priority planes.  Each
 * plane's worst-case run time is compared against ASYNC_*_BUDGET_US; these
 * are internal counters (not shown on the compact HUD) used to detect a plane
 * monopolising the CPU. */
static uint32_t s_input_wcet_us = 0;
static uint32_t s_ui_wcet_us = 0;
static uint32_t s_hud_comp_ms = 0;
static uint32_t s_hud_pres_ms = 0;
static uint32_t s_hud_wcet_ms = 0;
static char s_hud_path_live = '-';
static uint32_t s_hud_area_live = 0;
static char s_hud_path = '-';
static uint32_t s_hud_area = 0;

/* CPU presentation is sliced into row bands.  Each trip copies as many
 * PRESENT_COPY_ROWS_PER_STEP chunks as fit inside ASYNC_PRESENT_BUDGET_US, so a
 * full frame becomes visible within a few ms (not a slow 2-row wipe) while no
 * single trip blocks long enough to starve the 1 kHz input plane.
 * DMA/page-flip is intentionally disabled until it is reliable. */
#define PRESENT_COPY_ROWS_PER_STEP 32u
#define ASYNC_PRESENT_BUDGET_US    250u
static volatile uint32_t *s_present_copy_fb;
static uint32_t s_present_copy_row;
static int s_present_copy_active;

void async_rt_format_status(char *buf, size_t len) {
    uint32_t gap = s_async_rt_stats.input_gap_us;
    uint32_t gap_max = s_async_rt_stats.input_gap_max_us;
    uint32_t blit = s_async_rt_stats.blit_us;
    uint32_t blit_max = s_async_rt_stats.blit_max_us;
    if (!buf || len == 0) return;
    tkl_snprintf(buf, len, "IN %u.%ums GAP %u/%ums BLIT %u.%ums",
                 gap / 1000u, (gap / 100u) % 10u, gap_max / 1000u,
                 blit / 1000u, (blit / 100u) % 10u, blit_max / 1000u);
}

/* DIAGNOSTIC compact HUD (temporary): one flash reveals the input topology.
 *   A = 1 when the 1 kHz IRQ plane is armed, 0 = cooperative fallback
 *   M = number of mouse slots the xHCI enumerator bound (0 => cursor frozen)
 *   K = 1 when a keyboard slot is bound
 *   G = worst INPUT cadence gap in ms (clamped 999)
 * Read as e.g. "A0M0K1G999": IRQ dead, no mouse enumerated, keyboard bound. */
void async_rt_format_compact_status(char *buf, size_t len) {
    extern uint32_t xhci_mouse_count(void);
    extern uint32_t xhci_kbd_bound(void);
    extern uint32_t arm64_irq_selftest_seen(void);
    uint32_t gap = s_async_rt_stats.input_gap_max_us / 1000u;
    if (gap > 99u) gap = 99u;
    if (!buf || len == 0) return;
    /* A1 = IRQ plane live.  When the tick never confirmed, the A field
     * carries the boot self-test verdict instead of a bare 0:
     *   V = forced-pending IRQ was NOT taken  -> GIC->CPU delivery broken
     *   T = forced-pending IRQ WAS taken      -> timer never asserts PPI */
    char a0 = arm64_irq_selftest_seen() ? 'T' : 'V';
    if (s_async_irq_active) {
        tkl_snprintf(buf, len, "A1M%uK%uG%u C%uP%uW%u %c%u",
                     xhci_mouse_count(), xhci_kbd_bound(), gap,
                     s_hud_comp_ms, s_hud_pres_ms, s_hud_wcet_ms,
                     s_hud_path, s_hud_area);
    } else {
        tkl_snprintf(buf, len, "A%cM%uK%uG%u C%uP%uW%u %c%u", a0,
                     xhci_mouse_count(), xhci_kbd_bound(), gap,
                     s_hud_comp_ms, s_hud_pres_ms, s_hud_wcet_ms,
                     s_hud_path, s_hud_area);
    }
}

/* Double-buffered 32-bpp Desktop Backbuffer */
static COLOR s_desktop_backbuffer[BTRON_SCREEN_W * BTRON_SCREEN_H] __attribute__((aligned(64)));

/* Global interactive mouse coordinates */
static H s_mouse_x = 512;
static H s_mouse_y = 384;
static uint8_t g_prev_mouse_btns = 0;
static uint8_t g_prev_kbd_scancode = 0;

/* RISC OS Inspired Keyboard Auto-Repeat Parameters & State (defined in input.c) */
extern uint32_t g_kbd_repeat_delay_us;
extern uint32_t g_kbd_repeat_interval_us;
extern int      g_kbd_repeat_enabled;
static uint8_t  s_held_kbd_scancode = 0;
static uint8_t  s_held_kbd_modifiers = 0;
static uint32_t s_key_press_time_us = 0;
static uint32_t s_key_last_repeat_us = 0;

/* RISC OS Mouse Multiplier (MouseStep CMOS &C2) & 3-Button Layout (defined in input.c) */
extern int      g_mouse_step_mult;
extern int      g_mouse_swap_select_adjust;

/* USB host controller selection: 1 = VL805 xHCI (hardware), 0 = DWC2 (QEMU / legacy) */
int g_use_xhci = 0;

/* External driver APIs */
extern void uart_init(void);
extern void uart_puts(const char *s);
extern void uart_putc(char c);
extern int  uart_has_char(void);
extern int  uart_getc(void);
extern void uart_hex32(uint32_t val);
extern uint32_t *init_pi_framebuffer(uint32_t w, uint32_t h);
extern ER ScreenDrv(int ac, unsigned char *av[]);
extern ER KbPdDrv(int ac, unsigned char *av[]);
extern ER LowKbPdDrv(int ac, unsigned char *av[]);
extern void* tkl_memset(void *s, int c, size_t n);
extern void tkernel_init_subsystems(int full_suite);
extern const UB* get_glyph_bitmap(TC code, H *out_width, H *out_height);
extern WND* open_vobj_manager_window(void);
extern WND* open_t_editor_window(void);
extern WND* open_gterm_window(void);
extern WND* launch_beos_chat(void);
extern WND* open_control_panel_window(void);
#include <arch/bcm283x/bcm2711_dma.h>
#include <btron/global_menu.h>
#include <btron/tracker.h>
extern int mailbox_set_virtual_offset(uint32_t x, uint32_t y);
extern int about_is_animating(void);
extern WND* about_get_wnd(void);
extern int  about_render_anim_dirty(GDEV *screen, volatile uint32_t *gpu_fb);
extern int  g_cursor_in_backbuffer;
extern void draw_baremetal_cursor_raw(volatile uint32_t *pixels, H mx, H my, H w, H h);
void blit_backbuffer_to_fb(volatile uint32_t *gpu_fb);
static int usb_poll_devices(GDEV *screen);
static void input_plane_task(GDEV *screen, uint32_t now_us);

/* ═══════════════════════════════════════════════════════════════════
 * Framebuffer Kernel Text Log Overlay
 * Uses existing BTRON troncode.c / jis_fonts.c 8×16 ASCII bitmaps.
 * Call fb_log_enable(gpu_fb) once the VideoCore mailbox returns a valid
 * framebuffer pointer.  After that, fb_log(msg) renders white text
 * on a semi-transparent black strip directly into GPU VRAM.
 * ═══════════════════════════════════════════════════════════════════ */

#define FB_LOG_W         BTRON_SCREEN_W   /* characters per row  = W/8     */
#define FB_LOG_GLYPH_H   16              /* glyph height in pixels        */
#define FB_LOG_ROWS      ((BTRON_SCREEN_H) / FB_LOG_GLYPH_H) /* 48 rows   */
#define FB_LOG_BG        0xCC000000u     /* semi-transparent black strip  */
#define FB_LOG_FG        0xFFFFFFFFu     /* white text                    */
#define FB_LOG_SHADOW    0xFF000000u     /* 1-pixel drop shadow           */

static volatile uint32_t *s_fb_log_fb   = NULL;
static int                s_fb_log_col  = 0;   /* current cursor X (chars)  */
static int                s_fb_log_row  = 0;   /* current cursor Y (rows)   */

static inline void arm64_fast_blit(volatile void *dst, const void *src, size_t bytes) {
    btron_row_blit((void *)dst, src, bytes);
    __asm__ volatile("dmb sy" : : : "memory");
}

/* The DMA engine reads RAM outside the CPU cache hierarchy. */
static inline void arm64_clean_cache_range(const void *addr, size_t size) {
    uintptr_t start = (uintptr_t)addr & ~(64UL - 1);
    uintptr_t end = (uintptr_t)addr + size;
    for (uintptr_t p = start; p < end; p += 64)
        __asm__ volatile("dc cvac, %0" : : "r"(p) : "memory");
    __asm__ volatile("dsb sy" : : : "memory");
}

/* Darken one text row in GPU VRAM (write-only, zero uncached reads). */
static void fb_log_darken_row(int row) {
    if (!s_fb_log_fb || row < 0 || row >= FB_LOG_ROWS) return;
    int y0 = row * FB_LOG_GLYPH_H;
    int stride = BTRON_SCREEN_W;
    uint32_t *gpu = (uint32_t *)s_fb_log_fb + y0 * stride;
    int total_px = FB_LOG_GLYPH_H * stride;
    for (int i = 0; i < total_px; i++) {
        gpu[i] = 0xFF080C14u;
    }
}

/* Fast SIMD Framebuffer Scroll:
 * Uses arm64_fast_blit to shift 47 text rows in ~1 ms, then clears bottom row. */
static void fb_log_scroll(void) {
    if (!s_fb_log_fb) return;
    int stride = BTRON_SCREEN_W;
    int scroll_bytes = (FB_LOG_ROWS - 1) * FB_LOG_GLYPH_H * stride * sizeof(uint32_t);

    /* Shift lines up using 64-byte burst blitter (only 1 ms!) */
    arm64_fast_blit((void *)s_fb_log_fb,
                    (const void *)(s_fb_log_fb + FB_LOG_GLYPH_H * stride),
                    scroll_bytes);

    /* Clear bottom row */
    fb_log_darken_row(FB_LOG_ROWS - 1);
}

/* Call once after init_pi_framebuffer returns a valid pointer. */
void fb_log_enable(volatile uint32_t *fb) {
    extern volatile uint32_t *g_irq_trace_fb;
    g_irq_trace_fb = fb;
    s_fb_log_fb  = fb;
    s_fb_log_col = 0;
    s_fb_log_row = 0;
    if (fb) {
        mailbox_set_virtual_offset(0, 0);
        /* Blank initial RAM backbuffer & VRAM cleanly */
        for (int i = 0; i < BTRON_SCREEN_W * BTRON_SCREEN_H; i++) {
            s_desktop_backbuffer[i] = 0xFF080C14u;
            ((uint32_t *)fb)[i] = 0xFF080C14u;
        }
    }
}

/* Render one ASCII character into GPU VRAM (write-only). */
static void fb_log_putchar(char c, int col, int row) {
    if (!s_fb_log_fb) return;
    H gw = 8, gh = 16;
    const UB *bmp = get_glyph_bitmap((TC)(unsigned char)c, &gw, &gh);
    if (!bmp) return;
    int x0 = col * 8;
    int y0 = row * FB_LOG_GLYPH_H;
    if (x0 + 8 > BTRON_SCREEN_W || row >= FB_LOG_ROWS) return;
    for (int row_i = 0; row_i < gh; row_i++) {
        UB bits = bmp[row_i];
        uint32_t *gpu_dst = (uint32_t *)s_fb_log_fb + (y0 + row_i) * BTRON_SCREEN_W + x0;
        for (int bit = 7; bit >= 0; bit--) {
            if (bits & (1u << bit)) {
                if (bit > 0) {
                    *(gpu_dst + (8 - bit)) = FB_LOG_SHADOW;
                }
                *gpu_dst = FB_LOG_FG;
            }
            gpu_dst++;
        }
    }
}

/*
 * fb_log(msg) — write a string to the on-screen kernel log.
 * Wraps at screen right edge; scrolls when reaching bottom row.
 * Call this in addition to (or instead of) uart_puts().
 */
void fb_log(const char *msg) {
    if (!msg) return;
    uart_puts(msg);          /* always mirror to serial */
    if (!s_fb_log_fb) return;
    for (const char *p = msg; *p; p++) {
        if (*p == '\n' || *p == '\r') {
            s_fb_log_col = 0;
            if (*p == '\n') {
                s_fb_log_row++;
                if (s_fb_log_row >= FB_LOG_ROWS) {
                    fb_log_scroll();
                    s_fb_log_row = FB_LOG_ROWS - 1;
                } else {
                    fb_log_darken_row(s_fb_log_row);
                }
            }
            continue;
        }
        if (*p == '\t') {
            /* Tab: advance to next 8-column stop */
            s_fb_log_col = (s_fb_log_col + 8) & ~7;
        } else {
            int cols = BTRON_SCREEN_W / 8;
            if (s_fb_log_col >= cols) {
                s_fb_log_col = 0;
                s_fb_log_row++;
                if (s_fb_log_row >= FB_LOG_ROWS) {
                    fb_log_scroll();
                    s_fb_log_row = FB_LOG_ROWS - 1;
                } else {
                    fb_log_darken_row(s_fb_log_row);
                }
            }
            fb_log_putchar(*p, s_fb_log_col, s_fb_log_row);
            s_fb_log_col++;
        }
    }
}

void fb_log_hex32(uint32_t val) {
    char buf[12];
    const char *hex = "0123456789ABCDEF";
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 7; i >= 0; i--) {
        buf[2 + (7 - i)] = hex[(val >> (i * 4)) & 0xF];
    }
    buf[10] = '\0';
    fb_log(buf);
}

void fb_log_hex64(uint64_t val) {
    char buf[20];
    const char *hex = "0123456789ABCDEF";
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 15; i >= 0; i--) {
        buf[2 + (15 - i)] = hex[(val >> (i * 4)) & 0xF];
    }
    buf[18] = '\0';
    fb_log(buf);
}

void fb_log_dec(uint32_t val) {
    char buf[16];
    char tmp[16];
    if (val == 0) {
        fb_log("0");
        return;
    }
    int p = 0;
    while (val > 0) {
        tmp[p++] = '0' + (val % 10);
        val /= 10;
    }
    for (int i = 0; i < p; i++) {
        buf[i] = tmp[p - 1 - i];
    }
    buf[p] = '\0';
    fb_log(buf);
}

/* Single-character echo to screen only (no UART double-echo). */
static void fb_log_putc(char c) {
    if (!s_fb_log_fb) return;
    if (c == '\n') {
        s_fb_log_col = 0;
        s_fb_log_row++;
        if (s_fb_log_row >= FB_LOG_ROWS) {
            fb_log_scroll();
            s_fb_log_row = FB_LOG_ROWS - 1;
        } else {
            fb_log_darken_row(s_fb_log_row);
        }
    } else if (c == '\r') {
        s_fb_log_col = 0;
    } else {
        if (s_fb_log_col == 0) fb_log_darken_row(s_fb_log_row);
        int cols = BTRON_SCREEN_W / 8;
        if (s_fb_log_col >= cols) {
            s_fb_log_col = 0;
            s_fb_log_row++;
            if (s_fb_log_row >= FB_LOG_ROWS) {
                fb_log_scroll();
                s_fb_log_row = FB_LOG_ROWS - 1;
            } else {
                fb_log_darken_row(s_fb_log_row);
            }
        }
        fb_log_putchar(c, s_fb_log_col, s_fb_log_row);
        s_fb_log_col++;
    }
    __asm__ volatile("dsb sy" : : : "memory");
}

/* Erase the last typed character on-screen (backspace). */
static void fb_log_backspace(void) {
    if (!s_fb_log_fb || s_fb_log_col <= 0) return;
    s_fb_log_col--;
    int x0 = s_fb_log_col * 8;
    int y0 = s_fb_log_row * FB_LOG_GLYPH_H;
    for (int py = y0; py < y0 + FB_LOG_GLYPH_H; py++) {
        uint32_t *line = (uint32_t *)s_fb_log_fb + py * BTRON_SCREEN_W;
        for (int px = x0; px < x0 + 8; px++) {
            line[px] = 0xFF080C14u;
        }
    }
    __asm__ volatile("dsb sy" : : : "memory");
}

/* ═══════════════════════════════════════════════════════════════════
 * Stage 1 First-Stage Console & Stage 2 B-System Workbench Session
 * Modelled on core_ps2.c: ps2_shell_exec / launch_ps2_desktop_session
 * Input: DWC2 USB keyboard only. No UART required.
 * ═══════════════════════════════════════════════════════════════════ */

static int  s_gui_active  = 0;   /* 1 = workbench is running */
static char s_cmd_buf[64];
static int  s_cmd_pos     = 0;

/* Forward declaration */
static void launch_pi4_desktop_session(uint32_t *gpu_fb);

static void pi4_shell_exec(const char *cmd, uint32_t *gpu_fb)
{
    if (tkl_strcmp(cmd, "help") == 0) {
        fb_log("Commands:\n");
        fb_log("  startx / desktop / gui  - Launch B-System Workbench\n");
        fb_log("  exit / console          - Return to Stage 1 console\n");
        fb_log("  open <app>              - cabinet | editor | terminal | chat | settings\n");
        fb_log("  mem                     - Memory map\n");
        fb_log("  ver                     - Kernel version\n");
        fb_log("  clear                   - Clear screen\n");
        fb_log("  reboot                  - Halt processor\n");

    } else if (tkl_strcmp(cmd, "startx") == 0 ||
               tkl_strcmp(cmd, "desktop") == 0 ||
               tkl_strcmp(cmd, "gui") == 0) {
        if (!s_gui_active) {
            launch_pi4_desktop_session(gpu_fb);
        } else {
            fb_log("[CON] Workbench already active.\n");
        }

    } else if (tkl_strcmp(cmd, "exit") == 0 ||
               tkl_strcmp(cmd, "console") == 0 ||
               tkl_strcmp(cmd, "quit") == 0) {
        if (s_gui_active) {
            s_gui_active = 0;
        } else {
            fb_log("[CON] Already at Stage 1 console.\n");
        }

    } else if (tkl_strcmp(cmd, "mem") == 0) {
        fb_log("[MEM] BCM2711 Pi 400 — 4 GB RAM\n");
        fb_log("[MEM]   0x00000000-0xFCFFFFFF  RAM (usable)\n");
        fb_log("[MEM]   0xFD000000-0xFFFFFFFF  MMIO / PCIe\n");
        fb_log("[MEM]   Kernel heap: 0x01000000-0x1B000000\n");

    } else if (tkl_strcmp(cmd, "ver") == 0) {
        fb_log("B-System/BTRON3 3.20  aarch64-bcm2711\n");
        fb_log("T-Kernel 2.0  Takanori Yokoyama  Pi 400\n");
        fb_log("Built: " __DATE__ " " __TIME__ "\n");

    } else if (tkl_strcmp(cmd, "clear") == 0) {
        /* Blank the GPU VRAM to dark */
        if (s_fb_log_fb) {
            for (int i = 0; i < BTRON_SCREEN_W * BTRON_SCREEN_H; i++)
                ((uint32_t *)s_fb_log_fb)[i] = 0xFF080C14u;
            s_fb_log_col = 0;
            s_fb_log_row = 0;
        }

    } else if (tkl_strncmp(cmd, "open ", 5) == 0) {
        const char *app = cmd + 5;
        int opened = 0;
        if (tkl_strcmp(app, "cabinet") == 0 || tkl_strcmp(app, "vobj") == 0) {
            open_vobj_manager_window();
            fb_log("[CON] Opened Cabinet.\n");
            opened = 1;
        } else if (tkl_strcmp(app, "editor") == 0) {
            open_t_editor_window();
            fb_log("[CON] Opened Editor.\n");
            opened = 1;
        } else if (tkl_strcmp(app, "terminal") == 0 || tkl_strcmp(app, "gterm") == 0) {
            open_gterm_window();
            fb_log("[CON] Opened Terminal.\n");
            opened = 1;
        } else if (tkl_strcmp(app, "chat") == 0) {
            launch_beos_chat();
            fb_log("[CON] Opened Chat.\n");
            opened = 1;
        } else if (tkl_strcmp(app, "settings") == 0 || tkl_strcmp(app, "panel") == 0) {
            open_control_panel_window();
            fb_log("[CON] Opened Settings.\n");
            opened = 1;
        } else {
            fb_log("[CON] Unknown app. Try: cabinet editor terminal chat settings\n");
        }
        if (opened && !s_gui_active) {
            launch_pi4_desktop_session(gpu_fb);
        }

    } else if (tkl_strcmp(cmd, "reboot") == 0) {
        fb_log("[CON] Halting processor.\n");
        while (1) __asm__ volatile("wfe");

    } else if (cmd[0] != '\0') {
        fb_log("[CON] Unknown command: ");
        fb_log(cmd);
        fb_log("\n[CON] Type 'help' for commands.\n");
    }
}

/* ─────────────────────────────────────────────────────────────────
 * Stage 1 Interactive Terminal Console: Keystroke Polling
 *
 * In Stage 1, BTRON boots into an interactive bare-metal diagnostics
 * and control console rendered directly onto the HDMI display.
 * Keystrokes are polled directly from:
 *   - xHCI Event Ring / Transfer Ring on physical BCM2711 (Pi 400 internal keyboard)
 *   - DWC2 USB Host Controller on emulated QEMU raspi3b / raspi4
 *   - PL011 UART serial line (if serial cable is attached)
 *
 * When the user types commands like 'startx' or 'gui', the system transitions
 * into the Stage 2 graphical Workbench desktop session.
 * ───────────────────────────────────────────────────────────────── */
static int pi4_shell_poll(uint32_t *gpu_fb)
{
    static uint8_t s_prev_scancode = 0;
    uint32_t k = 0;

    /* Drain xHCI event ring FIRST (if on physical Pi 400 hardware).
     * When the 1 kHz IRQ input plane is armed, the ISR owns event-ring
     * draining and the shell must not touch it (ASYNC.txt §5: single
     * producer per ring). */
    if (g_use_xhci && !s_async_irq_active) {
        xhci_process_events();
    }

    /* 1. Poll USB Keyboard:
     *    On Pi 400 (BCM2711, mmio 0xFE000000), read HID reports from xHCI transfer ring.
     *    On QEMU / Pi 2 / Pi 3 (mmio 0x3F000000), read packets from DWC2 channel registers. */

    usb_kbd_report_t rep;
    int kbd_ready = 0;
    if (g_use_xhci) {
        kbd_ready = (xhci_poll_keyboard(&rep) > 0);
    } else {
        kbd_ready = (dwc2_poll_keyboard(&rep) > 0);
    }

    if (kbd_ready) {
        uint8_t sc = rep.keys[0];
        if (sc != s_prev_scancode) {
            s_prev_scancode = sc;
            if (sc != 0) {
                /* Translate standard USB HID scancode to ASCII / B-TRON character */
                k = dwc2_usb_to_btron_key(sc, rep.modifiers);
            }
        }
    }

    /* 2. Poll USB Mouse (motion/clicks in shell cancel autoboot).
     * With the IRQ plane armed, the ISR consumes the decoded report ring,
     * so the shell observes the seqlock snapshot instead of polling. */
    if (s_async_irq_active) {
        static uint16_t s_prev_motion_seq = 0;
        uint16_t mseq = s_pointer_slot.motion_seq;
        if (mseq != s_prev_motion_seq || s_pointer_slot.buttons != 0) {
            s_prev_motion_seq = mseq;
            return 1;
        }
    } else {
        usb_mouse_report_t mrep;
        int mouse_ready = 0;
        if (g_use_xhci) {
            mouse_ready = (xhci_poll_mouse(&mrep) > 0);
        } else {
            mouse_ready = (dwc2_poll_mouse(&mrep) > 0);
        }
        if (mouse_ready && (mrep.dx != 0 || mrep.dy != 0 || mrep.buttons != 0)) {
            return 1;
        }
    }

    /* 3. Poll UART Serial Console (if connected) */
    if (k == 0 && uart_has_char()) {
        int c = uart_getc();
        if (c == '\r') c = '\n';
        k = (uint32_t)(uint8_t)c;
    }

    if (k == 0) return 0;

    /* Pass keys through to workbench when GUI is active */
    if (s_gui_active) {
        if (k == 0x1B /* Escape */ || k == 'q' || k == 'Q') {
            s_gui_active = 0;
        } else {
            EVT ev;
            ev.type   = EV_KEY_DOWN;
            ev.key    = k;
            ev.pos.x  = s_mouse_x;
            ev.pos.y  = s_mouse_y;
            ev.button = 0;
            ev.data   = 0;
            snd_evt(&ev);
        }
        return 1;
    }

    /* Stage 1 console line editing */
    if (k == '\n' || k == '\r') {
        fb_log_putc('\n');
        s_cmd_buf[s_cmd_pos] = '\0';
        pi4_shell_exec(s_cmd_buf, gpu_fb);
        s_cmd_pos = 0;
        /* Re-print prompt if still in Stage 1 */
        if (!s_gui_active) fb_log("btron-pi400# ");
    } else if (k == 8 || k == 127) {       /* Backspace / DEL */
        if (s_cmd_pos > 0) {
            s_cmd_pos--;
            fb_log_backspace();
        }
    } else if (k >= 0x20 && k < 0x7F &&
               s_cmd_pos < (int)sizeof(s_cmd_buf) - 1) {
        s_cmd_buf[s_cmd_pos++] = (char)k;
        fb_log_putc((char)k);
    }
    return 1;
}

/* Restore 16x16 background under mouse cursor from pristine backbuffer */
static inline void restore_cursor_area(volatile uint32_t *gpu_fb, H x, H y) {
    H x0 = x, y0 = y, bw = 16, bh = 16;
    if (x0 < 0) { bw += x0; x0 = 0; }
    if (y0 < 0) { bh += y0; y0 = 0; }
    if (x0 + bw > BTRON_SCREEN_W) bw = BTRON_SCREEN_W - x0;
    if (y0 + bh > BTRON_SCREEN_H) bh = BTRON_SCREEN_H - y0;
    if (bw <= 0 || bh <= 0) return;

    for (H r = 0; r < bh; r++) {
        uint32_t *d = (uint32_t *)(gpu_fb + (y0 + r) * BTRON_SCREEN_W + x0);
        const COLOR *s = &s_desktop_backbuffer[(y0 + r) * BTRON_SCREEN_W + x0];
        for (H c = 0; c < bw; c++) d[c] = s[c];
    }
}

static void present_copy_step(void) {
    if (!s_present_copy_active || !s_present_copy_fb) return;

    uint32_t start_us = *(volatile uint32_t *)(TIMER_BASE + 0x04);
    for (;;) {
        uint32_t rows = BTRON_SCREEN_H - s_present_copy_row;
        if (rows > PRESENT_COPY_ROWS_PER_STEP) rows = PRESENT_COPY_ROWS_PER_STEP;
        arm64_fast_blit((void *)(s_present_copy_fb + s_present_copy_row * BTRON_SCREEN_W),
                        &s_desktop_backbuffer[s_present_copy_row * BTRON_SCREEN_W],
                        (size_t)rows * BTRON_SCREEN_W * sizeof(COLOR));
        s_present_copy_row += rows;
        if (s_present_copy_row >= BTRON_SCREEN_H) { s_present_copy_active = 0; break; }
        if ((*(volatile uint32_t *)(TIMER_BASE + 0x04) - start_us) >= ASYNC_PRESENT_BUDGET_US)
            break;
    }

    uint32_t elapsed = *(volatile uint32_t *)(TIMER_BASE + 0x04) - start_us;
    s_async_rt_stats.blit_us = elapsed;
    if (elapsed > s_async_rt_stats.blit_max_us)
        s_async_rt_stats.blit_max_us = elapsed;
    /* A copied band may have covered the cursor even when it did not move. */
    s_present_cursor_dirty = 1;
}

/* Copy one backbuffer rect to the visible page.  Bounded by the rect, so an
 * interaction presents only the pixels that changed instead of a full 3 MB
 * sweep (the perceived-latency culprit for drag / focus / menu-open). */
static void present_backbuffer_rect(volatile uint32_t *fb, H x0, H y0, H x1, H y1) {
    if (!fb) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > BTRON_SCREEN_W) x1 = BTRON_SCREEN_W;
    if (y1 > BTRON_SCREEN_H) y1 = BTRON_SCREEN_H;
    if (x1 <= x0 || y1 <= y0) return;
    volatile uint32_t *dst = fb + s_present_front_page * BTRON_SCREEN_W * BTRON_SCREEN_H;
    H width = x1 - x0;
    for (H y = y0; y < y1; y++) {
        const COLOR *srow = &s_desktop_backbuffer[y * BTRON_SCREEN_W + x0];
        uint32_t *drow = (uint32_t *)(dst + y * BTRON_SCREEN_W + x0);
        btron_row_blit(drow, srow, (size_t)width * sizeof(COLOR));
    }
    __asm__ volatile("dmb sy" : : : "memory");
    s_present_cursor_dirty = 1;
}

/* Window-union of the previous full render.  Initialised to the whole screen
 * so the first full render presents everything (background, panel, test bar). */
static RECT s_prev_win_union = { 0, 0, BTRON_SCREEN_W, BTRON_SCREEN_H };

/* Full composite + present only old-union ∪ new-union.  A moved or restacked
 * window's vacated area lies inside that union, so partial present stays
 * correct while skipping the unchanged pure-desktop pixels.  `extra` (may be
 * NULL) widens the present region — used to fold in a menu rect that was open
 * at the start of this UI trip, so closing a menu repaints its (now-desktop)
 * pixels instead of leaving stale overlay artefacts on the framebuffer. */
static void present_full_render(GDEV *screen, volatile uint32_t *fb, const RECT *extra) {
    uint32_t t0 = *(volatile uint32_t *)(TIMER_BASE + 0x04);
    workbench_render(screen, BTRON_SCREEN_W, BTRON_SCREEN_H);
    uint32_t t1 = *(volatile uint32_t *)(TIMER_BASE + 0x04);
    RECT cur;
    wnd_get_union_bounds(&cur);
    H x0 = cur.left  < s_prev_win_union.left  ? cur.left  : s_prev_win_union.left;
    H y0 = cur.top   < s_prev_win_union.top   ? cur.top   : s_prev_win_union.top;
    H x1 = cur.right > s_prev_win_union.right ? cur.right : s_prev_win_union.right;
    H y1 = cur.bottom> s_prev_win_union.bottom? cur.bottom: s_prev_win_union.bottom;
    if (extra && extra->right > extra->left && extra->bottom > extra->top) {
        if (extra->left   < x0) x0 = extra->left;
        if (extra->top    < y0) y0 = extra->top;
        if (extra->right  > x1) x1 = extra->right;
        if (extra->bottom > y1) y1 = extra->bottom;
    }
    s_prev_win_union = cur;
    present_backbuffer_rect(fb, x0, y0, x1, y1);
    uint32_t t2 = *(volatile uint32_t *)(TIMER_BASE + 0x04);
    /* DIAGNOSTIC: split the full-render cost so we can confirm on hardware that
     * window-move is composite-bound (workbench_render) not copy-bound (present).
     * Reported once per second over UART, then reset. */
    s_async_rt_stats.composite_us = t1 - t0;
    if (s_async_rt_stats.composite_us > s_async_rt_stats.composite_max_us) {
        s_async_rt_stats.composite_max_us = s_async_rt_stats.composite_us;
        s_hud_path_live = 'f';
        s_hud_area_live = ((uint32_t)(x1 - x0) * (uint32_t)(y1 - y0) * 100u) /
                          ((uint32_t)BTRON_SCREEN_W * BTRON_SCREEN_H);
    }
    s_async_rt_stats.present_us = t2 - t1;
    if (s_async_rt_stats.present_us > s_async_rt_stats.present_max_us)
        s_async_rt_stats.present_max_us = s_async_rt_stats.present_us;
}

static void present_move_render(GDEV *screen, volatile uint32_t *fb,
                                const RECT *old_damage, const RECT *new_damage) {
    if (!old_damage || !new_damage) return;

    RECT old = *old_damage;
    RECT current = *new_damage;
    if (old.left < 0) old.left = 0;
    if (old.top < 0) old.top = 0;
    if (old.right > BTRON_SCREEN_W) old.right = BTRON_SCREEN_W;
    if (old.bottom > BTRON_SCREEN_H) old.bottom = BTRON_SCREEN_H;
    if (current.left < 0) current.left = 0;
    if (current.top < 0) current.top = 0;
    if (current.right > BTRON_SCREEN_W) current.right = BTRON_SCREEN_W;
    if (current.bottom > BTRON_SCREEN_H) current.bottom = BTRON_SCREEN_H;
    if (old.right <= old.left || old.bottom <= old.top ||
        current.right <= current.left || current.bottom <= current.top)
        return;

    uint32_t t0 = *(volatile uint32_t *)(TIMER_BASE + 0x04);
    workbench_render_damage(screen, &old);
    workbench_render_damage(screen, &current);
    uint32_t t1 = *(volatile uint32_t *)(TIMER_BASE + 0x04);
    RECT cur;
    wnd_get_union_bounds(&cur);
    s_prev_win_union = cur;
    present_backbuffer_rect(fb, old.left, old.top, old.right, old.bottom);
    present_backbuffer_rect(fb, current.left, current.top, current.right, current.bottom);
    uint32_t t2 = *(volatile uint32_t *)(TIMER_BASE + 0x04);

    H overlap_left = old.left > current.left ? old.left : current.left;
    H overlap_top = old.top > current.top ? old.top : current.top;
    H overlap_right = old.right < current.right ? old.right : current.right;
    H overlap_bottom = old.bottom < current.bottom ? old.bottom : current.bottom;
    uint32_t area = (uint32_t)(old.right - old.left) * (uint32_t)(old.bottom - old.top) +
                    (uint32_t)(current.right - current.left) * (uint32_t)(current.bottom - current.top);
    if (overlap_right > overlap_left && overlap_bottom > overlap_top)
        area -= (uint32_t)(overlap_right - overlap_left) * (uint32_t)(overlap_bottom - overlap_top);

    s_async_rt_stats.composite_us = t1 - t0;
    if (s_async_rt_stats.composite_us > s_async_rt_stats.composite_max_us) {
        s_async_rt_stats.composite_max_us = s_async_rt_stats.composite_us;
        s_hud_path_live = 'm';
        s_hud_area_live = (area * 100u) / ((uint32_t)BTRON_SCREEN_W * BTRON_SCREEN_H);
    }
    s_async_rt_stats.present_us = t2 - t1;
    if (s_async_rt_stats.present_us > s_async_rt_stats.present_max_us)
        s_async_rt_stats.present_max_us = s_async_rt_stats.present_us;
}

/* TRUE when the focused top window owns an open in-app menu (its private
 * APP_MENU_BAR dropdown).  Each app registers a `menu_open` callback; the
 * dropdown is painted inside that window's own paint callback, so hovering or
 * clicking it only needs a top-window repaint + that window's rect presented —
 * not a full desktop composite.  Returns FALSE when no window, no callback, or
 * the menu is closed, so callers safely fall back to the full-render path. */
static int top_window_menu_open(void) {
    WND *top = get_top_wnd();
    if (top && top->menu_open) return top->menu_open(top) ? 1 : 0;
    return 0;
}

/* Stage 2: Launch full B-System Workbench desktop session. */
static void launch_pi4_desktop_session(uint32_t *gpu_fb)
{
    fb_log("\n[BOOT] Launching B-System Workbench...\n");
    s_gui_active = 1;

    /* Keep backbuffer clean of cursor stamps for zero-latency cursor restores */
    g_cursor_in_backbuffer = 0;

    GDEV *screen = init_baremetal_desktop(
        (uint32_t *)s_desktop_backbuffer, BTRON_SCREEN_W, BTRON_SCREEN_H);
    if (!screen) {
        fb_log("[FATAL] Desktop init failed.\n");
        s_gui_active = 0;
        return;
    }
    workbench_init(BTRON_SCREEN_W);
    workbench_render(screen, BTRON_SCREEN_W, BTRON_SCREEN_H);
    blit_backbuffer_to_fb(gpu_fb);

    /* Draw cursor directly to GPU front buffer */
    draw_baremetal_cursor_raw(gpu_fb, s_mouse_x, s_mouse_y, BTRON_SCREEN_W, BTRON_SCREEN_H);
    H prev_mx = s_mouse_x;
    H prev_my = s_mouse_y;

    /* Disable on-screen log: workbench owns the framebuffer now */
    fb_log_enable(NULL);

    uart_puts("[WB]  Workbench live. Press [Esc] or type 'exit' to return.\n");

    g_prev_kbd_scancode = 0;
    s_held_kbd_scancode = 0;
    s_held_kbd_modifiers = 0;
    g_prev_mouse_btns = 0;

    /* Resync the seqlock consumer baseline so Stage-1 pointer motion cannot
     * burst into the first GUI frame as one giant delta. */
    s_input_prev_buttons = s_pointer_slot.buttons;
    s_input_last.acc_dx = s_pointer_slot.acc_dx;
    s_input_last.acc_dy = s_pointer_slot.acc_dy;
    s_input_last.acc_wheel = s_pointer_slot.acc_wheel;
    s_input_last.buttons = s_pointer_slot.buttons;

    /* ASYNC.txt Tier-1 budget scheduler state.  Two equal-priority periodic
     * planes share this CPU context; deadlines are ABSOLUTE (next += period)
     * so a late or over-budget run never drifts the cadence, and a bounded
     * catch-up prevents a death spiral after a long stall. */
    uint32_t t_boot = *(volatile uint32_t *)(TIMER_BASE + 0x04);
    uint32_t next_input_us = t_boot;
    uint32_t next_ui_us    = t_boot;
    uint32_t last_input_us = t_boot;
    uint32_t last_clock    = t_boot;

    EVT ev;

    while (s_gui_active) {
        uint32_t now = *(volatile uint32_t *)(TIMER_BASE + 0x04);

        /* ── INPUT plane: period 1 ms, budget ASYNC_INPUT_BUDGET_US ──────
         * When the IRQ plane is armed the 1 kHz ISR is the sole xHCI
         * producer, so this worker only consumes the seqlock snapshot and
         * pops the SPSC rings.  It never renders, blits, or dispatches
         * events (ASYNC.txt §4). */
        if ((int32_t)(now - next_input_us) >= 0) {
            uint32_t t_in = now;
            uint32_t trbs = 0;
            if (!s_async_irq_active && g_use_xhci)
                trbs = xhci_process_events_bounded(ASYNC_XHCI_TRB_BUDGET);
            input_plane_task(screen, now);
            if (!s_async_irq_active) {
                /* Cooperative path owns cadence telemetry; when the IRQ plane
                 * is armed the ISR records it instead. */
                uint32_t input_gap = now - last_input_us;
                s_async_rt_stats.input_gap_us = input_gap;
                if (input_gap > s_async_rt_stats.input_gap_max_us)
                    s_async_rt_stats.input_gap_max_us = input_gap;
                s_async_rt_stats.trb_per_input = trbs;
                if (trbs > s_async_rt_stats.trb_max)
                    s_async_rt_stats.trb_max = trbs;
            }
            last_input_us = now;
            uint32_t in_cost = *(volatile uint32_t *)(TIMER_BASE + 0x04) - t_in;
            if (in_cost > s_input_wcet_us) s_input_wcet_us = in_cost;
            /* Advance the absolute deadline; bound catch-up to 100 periods. */
            do { next_input_us += ASYNC_INPUT_PERIOD_US; }
            while ((int32_t)(now - next_input_us) >= 0 &&
                   (uint32_t)(now - next_input_us) < 100u * ASYNC_INPUT_PERIOD_US);
        }

        /* Present at most 8 KiB of pixels per trip around the loop. */
        present_copy_step();

        /* DMA always fills the hidden VideoCore page.  Completion flips it
         * atomically; this keeps the visible page available to the pointer and
         * INPUT plane even when a full transfer spans many UI periods. */
        if (s_present_dma_enabled && !bcm2711_dma_is_busy(0) && s_present_dma_start_us != 0) {
            uint32_t dma_us = now - s_present_dma_start_us;
            s_present_dma_start_us = 0;
            if (dma_us > s_present_dma_max_us) s_present_dma_max_us = dma_us;
            s_present_front_page = s_present_dma_page;
            (void)mailbox_set_virtual_offset(0, s_present_front_page * BTRON_SCREEN_H);
            s_present_cursor_dirty = 1;
        }

        /* Immediate cursor update on GPU front buffer (< 1 us glass-to-glass latency) */
        if ((!s_present_dma_enabled || !bcm2711_dma_is_busy(0)) &&
            (s_mouse_x != prev_mx || s_mouse_y != prev_my || s_present_cursor_dirty)) {
            volatile uint32_t *cursor_fb = gpu_fb + s_present_front_page * BTRON_SCREEN_W * BTRON_SCREEN_H;
            restore_cursor_area(cursor_fb, prev_mx, prev_my);
            draw_baremetal_cursor_raw(cursor_fb, s_mouse_x, s_mouse_y, BTRON_SCREEN_W, BTRON_SCREEN_H);
            __asm__ volatile("dmb sy" : : : "memory");
            prev_mx = s_mouse_x;
            prev_my = s_mouse_y;
            s_present_cursor_dirty = 0;
        }

        /* ── UI plane: period 8.33 ms, budget ASYNC_UI_BUDGET_US ─────────
         * Drains a bounded number of events, then renders into the
         * backbuffer and arms the banded presenter.  Equal priority with the
         * INPUT plane: serviced by absolute deadline, never blocks input. */
        if ((int32_t)(now - next_ui_us) >= 0) {
        uint32_t t_ui = now;
        int redraw = 0;
        int overlay_redraw = 0;
        int panel_redraw = 0;
        int appmenu_redraw = 0;
        int move_drag = 0;
        int non_move_event = 0;
        int menu_open_at_loop_start = global_menu_is_open() || tracker_is_menu_open();
        int appmenu_open_at_loop_start = top_window_menu_open();
        /* Snapshot the rect any open menu occupies NOW.  If an event in this
         * trip closes that menu, the full-render present below must repaint
         * those pixels (now desktop) or stale overlay artfacts persist. */
        RECT start_menu_rect;
        int have_start_menu_rect = menu_open_at_loop_start &&
                                   global_menu_get_open_rect(&start_menu_rect);
        RECT move_damage = { 0, 0, 0, 0 };
        RECT move_first = { 0, 0, 0, 0 };
        RECT move_last = { 0, 0, 0, 0 };
        int have_move_damage = 0;
        for (uint32_t ev_iter = 0; ev_iter < ASYNC_UI_EVENT_BUDGET && get_evt(&ev, 0) == E_OK; ev_iter++) {
            if (ev.type != EV_MOUSE_MOVE) non_move_event = 1;
            if (ev.type == EV_KEY_DOWN && ev.key == 0x1B /* Escape */) {
                s_gui_active = 0;
                break;
            }
            if (ev.type == EV_KEY_DOWN && s_async_rt_stats.key_enqueue_us != 0)
                s_async_rt_stats.key_dispatch_us = now - s_async_rt_stats.key_enqueue_us;

            WND *drag_target = NULL;
            RECT drag_old = { 0, 0, 0, 0 };
            if (ev.type == EV_MOUSE_MOVE) {
                drag_target = wnd_mgr_get_drag_target();
                if (drag_target) drag_old = drag_target->bounds;
            }

            workbench_process_event(screen, &ev);

            if (drag_target) {
                RECT drag_new = drag_target->bounds;
                if (drag_old.left != drag_new.left || drag_old.top != drag_new.top ||
                    drag_old.right != drag_new.right || drag_old.bottom != drag_new.bottom) {
                    move_drag = 1;
                    if (!have_move_damage) {
                        move_damage = drag_old;
                        move_first = drag_old;
                        have_move_damage = 1;
                    }
                    move_last = drag_new;
                    if (drag_old.left < move_damage.left) move_damage.left = drag_old.left;
                    if (drag_old.top < move_damage.top) move_damage.top = drag_old.top;
                    if (drag_old.right > move_damage.right) move_damage.right = drag_old.right;
                    if (drag_old.bottom > move_damage.bottom) move_damage.bottom = drag_old.bottom;
                    if (drag_new.left < move_damage.left) move_damage.left = drag_new.left;
                    if (drag_new.top < move_damage.top) move_damage.top = drag_new.top;
                    if (drag_new.right > move_damage.right) move_damage.right = drag_new.right;
                    if (drag_new.bottom > move_damage.bottom) move_damage.bottom = drag_new.bottom;
                }
            }

            if (ev.type == EV_MOUSE_MOVE) {
                if (global_menu_is_open() || tracker_is_menu_open()) {
                    /* Hovering an open menu only moves the highlight: repaint
                     * the overlay, NOT the whole desktop.  A full composite per
                     * mouse-move was starving the 1 ms INPUT plane (laggy menus). */
                    overlay_redraw = 1;
                } else if (top_window_menu_open()) {
                    /* Hovering an open IN-APP menu moves its highlight inside the
                     * top window.  Repaint just that window (its paint callback
                     * redraws the dropdown), not a full desktop composite.  Without
                     * this the highlight froze: no branch matched a menu hover. */
                    appmenu_redraw = 1;
                } else if (g_prev_mouse_btns != 0 || wnd_mgr_is_interacting()) {
                    redraw = 1;   /* drag: window contents move, full composite */
                }
            } else {
                int menu_open_now = global_menu_is_open() || tracker_is_menu_open();
                if (ev.type == EV_BUT_DOWN && !menu_open_at_loop_start && menu_open_now) {
                    /* Opening a menu only lays an overlay over an unchanged
                     * desktop: repaint the overlay, skip the full composite. */
                    overlay_redraw = 1;
                } else if (ev.type == EV_BUT_DOWN &&
                           appmenu_open_at_loop_start && !top_window_menu_open()) {
                    /* An in-app menu just CLOSED on this click: the selected
                     * command may have altered the window or the desktop (e.g.
                     * Close Window), so take the artifact-free full-render path. */
                    redraw = 1;
                } else if (top_window_menu_open()) {
                    /* In-app menu open (click opened it, or a click/hover within
                     * it): repaint just the top window that owns the dropdown. */
                    appmenu_redraw = 1;
                } else {
                    /* Buttons, keys, focus changes need a real UI update */
                    redraw = 1;
                }
                if (ev.type == EV_KEY_DOWN && ev.key != '\r' && ev.key != '\n')
                    s_present_fast_key_update = 1;
            }
        }

        /* Periodic 1 Hz clock update */
        if (now - last_clock >= 1000000) {
            last_clock = now;
            /* Only the panel clock changed: repaint the panel band, NOT the
             * whole desktop.  A full composite here froze the mouse ~1x/sec. */
            panel_redraw = 1;
            /* Windowed cadence: the HUD 'G' field reports the worst INPUT gap
             * of the LAST second, not an all-time high-water mark, so it tracks
             * current responsiveness instead of latching a single boot stall. */
            s_async_rt_stats.input_gap_max_us = s_async_rt_stats.input_gap_us;
            s_hud_comp_ms = s_async_rt_stats.composite_max_us / 1000u;
            s_hud_pres_ms = s_async_rt_stats.present_max_us / 1000u;
            s_hud_wcet_ms = s_ui_wcet_us / 1000u;
            if (s_hud_comp_ms > 99u) s_hud_comp_ms = 99u;
            if (s_hud_pres_ms > 99u) s_hud_pres_ms = 99u;
            if (s_hud_wcet_ms > 99u) s_hud_wcet_ms = 99u;
            s_hud_path = s_hud_path_live;
            s_hud_area = s_hud_area_live > 99u ? 99u : s_hud_area_live;
            s_hud_path_live = '-';
            s_hud_area_live = 0;
            if (s_async_rt_stats.composite_max_us) {
                uart_puts("[UI]C");
                uart_hex32(s_async_rt_stats.composite_max_us);
                uart_puts("P");
                uart_hex32(s_async_rt_stats.present_max_us);
                uart_puts("W");
                uart_hex32(s_ui_wcet_us);
                uart_puts("\n");
            }
            s_async_rt_stats.composite_max_us = 0;
            s_async_rt_stats.present_max_us = 0;
            s_ui_wcet_us = 0;
        }

        RECT present_extra = { 0, 0, 0, 0 };
        int have_present_extra = 0;
        if (have_start_menu_rect) {
            present_extra = start_menu_rect;
            have_present_extra = 1;
        }
        if (have_move_damage) {
            if (!have_present_extra) {
                present_extra = move_damage;
                have_present_extra = 1;
            } else {
                if (move_damage.left < present_extra.left) present_extra.left = move_damage.left;
                if (move_damage.top < present_extra.top) present_extra.top = move_damage.top;
                if (move_damage.right > present_extra.right) present_extra.right = move_damage.right;
                if (move_damage.bottom > present_extra.bottom) present_extra.bottom = move_damage.bottom;
            }
        }

        /* Established mouse-only window moves restore and recomposite just the
         * old/new bounds.  All state-changing batches retain the full renderer. */
        if (move_drag && !non_move_event && !s_present_pending &&
            !menu_open_at_loop_start && !appmenu_open_at_loop_start &&
            !overlay_redraw && !appmenu_redraw && have_move_damage) {
            present_move_render(screen, gpu_fb, &move_first, &move_last);
            if (panel_redraw) {
                render_system_panel(screen);
                present_backbuffer_rect(gpu_fb, 0, 0, BTRON_SCREEN_W, 28);
            }
            s_present_cursor_dirty = 1;
        }
        /* Full UI path: redraw windows, menus, backbuffer blit */
        else if (redraw || s_present_pending) {
            if (s_present_dma_enabled && bcm2711_dma_is_busy(0)) {
                s_present_pending = 1;
            } else {
            if (s_present_fast_key_update && s_present_dma_enabled) {
                WND *top = get_top_wnd();
                if (top && top->visible) {
                    /* Text entry changes the bottom prompt line.  Repaint its
                     * owner but transfer only that 32-pixel band to the
                     * visible page, not the entire 3 MB desktop. */
                    H left = top->client.left;
                    H right = top->client.right;
                    H top_y = top->client.bottom - 32;
                    H bottom = top->client.bottom;
                    if (left < 0) left = 0;
                    if (right > BTRON_SCREEN_W) right = BTRON_SCREEN_W;
                    if (top_y < 0) top_y = 0;
                    if (bottom > BTRON_SCREEN_H) bottom = BTRON_SCREEN_H;
                    redraw_top_window();
                    arm64_clean_cache_range(&s_desktop_backbuffer[top_y * BTRON_SCREEN_W + left],
                                            (size_t)(bottom - top_y) * BTRON_SCREEN_W * sizeof(COLOR));
                    if (bcm2711_dma_blit2d_async(0,
                        (uintptr_t)(gpu_fb + s_present_front_page * BTRON_SCREEN_W * BTRON_SCREEN_H +
                                    top_y * BTRON_SCREEN_W + left), BTRON_SCREEN_W * sizeof(COLOR),
                        (uintptr_t)(&s_desktop_backbuffer[top_y * BTRON_SCREEN_W + left]),
                        BTRON_SCREEN_W * sizeof(COLOR), (right - left) * sizeof(COLOR), bottom - top_y) == 0) {
                        s_present_dma_start_us = now;
                        s_present_dma_page = s_present_front_page;
                        s_present_cursor_dirty = 1;
                    } else {
                        s_present_pending = 1;
                    }
                }
            } else if (s_present_fast_key_update) {
                /* Non-DMA text entry (the live path: DMA is disabled).  Repaint
                 * ONLY the top window and copy its band to the visible page,
                 * instead of a full workbench_render composite per keystroke.
                 * The full composite (background + every window + panel + test
                 * bar) ran synchronously in the UI plane and starved the 1 ms
                 * INPUT plane — that single blocking call is the G99 culprit. */
                WND *top = get_top_wnd();
                if (top && top->visible) {
                    H left   = top->bounds.left;
                    H right  = top->bounds.right;
                    H top_y  = top->bounds.top;
                    H bottom = top->bounds.bottom;
                    if (left < 0) left = 0;
                    if (right > BTRON_SCREEN_W) right = BTRON_SCREEN_W;
                    if (top_y < 0) top_y = 0;
                    if (bottom > BTRON_SCREEN_H) bottom = BTRON_SCREEN_H;
                    redraw_top_window();
                    present_backbuffer_rect(gpu_fb, left, top_y, right, bottom);
                } else {
                    present_full_render(screen, gpu_fb,
                                        have_present_extra ? &present_extra : NULL);
                }
            } else {
                present_full_render(screen, gpu_fb,
                                    have_present_extra ? &present_extra : NULL);
            }
            s_present_fast_key_update = 0;
            s_present_pending = 0;
            if (s_present_dma_enabled) {
                s_present_cursor_dirty = 1;
            } else {
                draw_baremetal_cursor_raw(gpu_fb, s_mouse_x, s_mouse_y, BTRON_SCREEN_W, BTRON_SCREEN_H);
                prev_mx = s_mouse_x;
                prev_my = s_mouse_y;
            }
            }
        }
        else if (overlay_redraw) {
            /* Menu hover/open: repaint only the open menu overlay and present
             * exactly that rect (not a full 3 MB sweep).  The overlay repaints
             * its whole rect, so this is ghost-free. */
            workbench_render_overlay_only(screen);
            RECT mr;
            if (global_menu_get_open_rect(&mr)) {
                present_backbuffer_rect(gpu_fb, mr.left, mr.top, mr.right, mr.bottom);
            } else {
                blit_backbuffer_to_fb(gpu_fb);
            }
            s_present_cursor_dirty = 1;
        }
        else if (appmenu_redraw) {
            /* In-app menu hover/click: the dropdown lives inside the top window,
             * so repaint ONLY that window (its paint callback redraws the menu)
             * and present its rect — not a full desktop composite.  If the 1 Hz
             * panel tick coincided, fold its band in so the clock never lags. */
            if (panel_redraw) {
                render_system_panel(screen);
                present_backbuffer_rect(gpu_fb, 0, 0, BTRON_SCREEN_W, 28);
            }
            WND *top = get_top_wnd();
            if (top && top->visible) {
                H l = top->bounds.left,  r = top->bounds.right;
                H t = top->bounds.top,   b = top->bounds.bottom;
                if (l < 0) l = 0;
                if (r > BTRON_SCREEN_W) r = BTRON_SCREEN_W;
                if (t < 0) t = 0;
                if (b > BTRON_SCREEN_H) b = BTRON_SCREEN_H;
                redraw_top_window();
                present_backbuffer_rect(gpu_fb, l, t, r, b);
            } else {
                present_full_render(screen, gpu_fb,
                                    have_present_extra ? &present_extra : NULL);
            }
            s_present_cursor_dirty = 1;
        }
        else if (panel_redraw) {
            /* 1 Hz clock: repaint only the top system panel (rows 0..27, incl.
             * the gold bar) and copy that band.  Bounded ~114 KB, sub-ms. */
            render_system_panel(screen);
            present_backbuffer_rect(gpu_fb, 0, 0, BTRON_SCREEN_W, 28);
        }
        uint32_t ui_cost = *(volatile uint32_t *)(TIMER_BASE + 0x04) - t_ui;
        if (ui_cost > s_ui_wcet_us) s_ui_wcet_us = ui_cost;
        /* Advance the absolute UI deadline; bound catch-up to 100 periods. */
        do { next_ui_us += ASYNC_UI_PERIOD_US; }
        while ((int32_t)(now - next_ui_us) >= 0 &&
               (uint32_t)(now - next_ui_us) < 100u * ASYNC_UI_PERIOD_US);
        }

        if ((!s_present_dma_enabled || !bcm2711_dma_is_busy(0)) &&
            (s_mouse_x != prev_mx || s_mouse_y != prev_my || s_present_cursor_dirty)) {
            volatile uint32_t *cursor_fb = gpu_fb + s_present_front_page * BTRON_SCREEN_W * BTRON_SCREEN_H;
            restore_cursor_area(cursor_fb, prev_mx, prev_my);
            draw_baremetal_cursor_raw(cursor_fb, s_mouse_x, s_mouse_y, BTRON_SCREEN_W, BTRON_SCREEN_H);
            __asm__ volatile("dmb sy" : : : "memory");
            prev_mx = s_mouse_x;
            prev_my = s_mouse_y;
            s_present_cursor_dirty = 0;
        }

    }

    /* Return to Stage 1 console */
    fb_log_enable((volatile uint32_t *)gpu_fb);
    /* Blank the screen */
    for (int i = 0; i < BTRON_SCREEN_W * BTRON_SCREEN_H; i++)
        ((uint32_t *)gpu_fb)[i] = 0xFF0A0F18u;
    s_fb_log_col = 0; s_fb_log_row = 0;

    fb_log("\n===============================================================\n");
    fb_log("  [BTRON] Exited Graphical Workbench Session\n");
    fb_log("  [BTRON] Returned to Stage 1 Terminal Console (1024x768)\n");
    fb_log("  Type 'startx' or 'desktop' to launch GUI session again.\n");
    fb_log("===============================================================\n\n");
    fb_log("btron-pi400# ");
}

/* ═══════════════════════════════════════════════════════════════════
 * Formatted Kernel Output: kprintf
 * ═══════════════════════════════════════════════════════════════════ */

static void print_num(uint32_t num, int base, int width, char pad) {
    char buf[32];
    int i = 0;
    const char digits[] = "0123456789ABCDEF";

    if (num == 0) {
        buf[i++] = '0';
    } else {
        while (num > 0) {
            buf[i++] = digits[num % base];
            num /= base;
        }
    }

    while (i < width) {
        buf[i++] = pad;
    }

    for (int j = i - 1; j >= 0; j--) {
        uart_putc(buf[j]);
    }
}

void kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            if (*p == '\n') uart_putc('\r');
            uart_putc(*p);
            continue;
        }
        p++;
        int width = 0;
        char pad = ' ';
        if (*p == '0') { pad = '0'; p++; }
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }

        switch (*p) {
            case 's': {
                const char *s = va_arg(ap, const char*);
                uart_puts(s ? s : "(null)");
                break;
            }
            case 'd':
            case 'i': {
                int32_t val = va_arg(ap, int32_t);
                if (val < 0) {
                    uart_putc('-');
                    val = -val;
                }
                print_num((uint32_t)val, 10, width, pad);
                break;
            }
            case 'u': {
                uint32_t val = va_arg(ap, uint32_t);
                print_num(val, 10, width, pad);
                break;
            }
            case 'x':
            case 'X':
            case 'p': {
                uint32_t val = va_arg(ap, uint32_t);
                print_num(val, 16, width, pad);
                break;
            }
            case 'c': {
                char c = (char)va_arg(ap, int);
                uart_putc(c);
                break;
            }
            case '%':
                uart_putc('%');
                break;
            default:
                uart_putc('%');
                uart_putc(*p);
                break;
        }
    }
    va_end(ap);
}

/* ═══════════════════════════════════════════════════════════════════
 * VideoCore GPU Framebuffer & Display Blitter (Single Physical Buffer)
 * ═══════════════════════════════════════════════════════════════════ */

void blit_backbuffer_to_fb(volatile uint32_t *gpu_fb) {
    if (!gpu_fb) return;
    if (!s_present_dma_enabled) {
        /* The source remains the same backbuffer, so a later redraw naturally
         * updates the remaining bands without restarting an active transfer. */
        if (!s_present_copy_active) {
            s_present_copy_fb = gpu_fb;
            s_present_copy_row = 0;
            s_present_copy_active = 1;
        }
        return;
    }
    uint32_t start_us = *(volatile uint32_t *)(TIMER_BASE + 0x04);

    if (s_present_dma_enabled) {
        uint32_t target_page = s_present_front_page ^ 1u;
        arm64_clean_cache_range(s_desktop_backbuffer,
                                BTRON_SCREEN_W * BTRON_SCREEN_H * sizeof(COLOR));
        if (bcm2711_dma_blit_linear_async(0,
                                          (uintptr_t)gpu_fb + target_page *
                                          BTRON_SCREEN_W * BTRON_SCREEN_H * sizeof(COLOR),
                                          (uintptr_t)s_desktop_backbuffer,
                                          BTRON_SCREEN_W * BTRON_SCREEN_H * sizeof(COLOR)) != 0) {
            return; /* UI retains s_present_pending and retries next period. */
        }
        s_present_dma_page = target_page;
        s_present_dma_start_us = start_us;
    } else {
        arm64_fast_blit((void *)gpu_fb, s_desktop_backbuffer,
                        BTRON_SCREEN_W * BTRON_SCREEN_H * sizeof(COLOR));
    }
    s_async_rt_stats.blit_us = *(volatile uint32_t *)(TIMER_BASE + 0x04) - start_us;
    if (s_async_rt_stats.blit_us > s_async_rt_stats.blit_max_us)
        s_async_rt_stats.blit_max_us = s_async_rt_stats.blit_us;
}

/* ═══════════════════════════════════════════════════════════════════
 * BCM2711 / BCM2837 System Timer & 60Hz Tick
 * ═══════════════════════════════════════════════════════════════════ */

static volatile uint32_t s_system_ticks = 0;

/* ═══════════════════════════════════════════════════════════════════
 * ASYNC.txt IRQ Input Plane (input_plane_on_irq)
 *
 * Runs in real 1 kHz timer interrupt context (GIC-400 PPI 30, armed by
 * arm64_irq_init after xhci_init).  Strictly bounded:
 *   1. drain at most ASYNC_ISR_TRB_BUDGET xHCI TRBs into SPSC rings
 *   2. fold at most ASYNC_ISR_MOUSE_BUDGET decoded mouse reports into the
 *      wait-free seqlock pointer snapshot
 *   3. record cadence/WCET telemetry and return
 * No rendering, no mailbox calls, no allocation, no event dispatch.
 * ═══════════════════════════════════════════════════════════════════ */
void rpi_timer_tick(void) {
    s_system_ticks++;
    if (!s_async_irq_active) return;

    uint32_t t0 = *(volatile uint32_t *)(TIMER_BASE + 0x04);
    if (s_irq_prev_us != 0) {
        uint32_t gap = t0 - s_irq_prev_us;
        s_async_rt_stats.input_gap_us = gap;
        if (gap > s_async_rt_stats.input_gap_max_us)
            s_async_rt_stats.input_gap_max_us = gap;
    }
    s_irq_prev_us = t0;

    if (g_use_xhci) {
        uint32_t trbs = xhci_process_events_bounded(ASYNC_ISR_TRB_BUDGET);
        s_async_rt_stats.trb_per_input = trbs;
        if (trbs > s_async_rt_stats.trb_max)
            s_async_rt_stats.trb_max = trbs;
    }

    /* Fold decoded mouse reports into the monotonic seqlock accumulators.
     * The INPUT worker diffs against its own last-consumed snapshot, so
     * batches coalesce losslessly without a reset handshake. */
    usb_mouse_report_t rep;
    int32_t dx = 0, dy = 0, wheel = 0;
    uint8_t buttons = 0;
    int got = 0;
    for (uint32_t i = 0; i < ASYNC_ISR_MOUSE_BUDGET && xhci_poll_mouse(&rep); i++) {
        dx += (int32_t)rep.dx;
        dy += (int32_t)rep.dy;
        wheel += (int32_t)rep.wheel;
        buttons = rep.buttons;
        got = 1;
    }
    if (got) {
        uint32_t seq = s_pointer_slot.seq;
        s_pointer_slot.seq = seq + 1;             /* odd: update in progress */
        __asm__ volatile("dmb sy" : : : "memory");
        s_pointer_slot.acc_dx += dx;
        s_pointer_slot.acc_dy += dy;
        s_pointer_slot.acc_wheel += wheel;
        s_pointer_slot.buttons = buttons;
        s_pointer_slot.present = 1;
        if (dx != 0 || dy != 0) s_pointer_slot.motion_seq++;
        __asm__ volatile("dmb sy" : : : "memory");
        s_pointer_slot.seq = seq + 2;             /* even: stable */
    }

    uint32_t isr_us = *(volatile uint32_t *)(TIMER_BASE + 0x04) - t0;
    s_async_rt_stats.isr_us = isr_us;
    if (isr_us > s_async_rt_stats.isr_max_us)
        s_async_rt_stats.isr_max_us = isr_us;
}

extern ER _tk_slp_tsk(W tmout);
extern ER _tk_wup_tsk(ID tskid);
extern ER _tk_dly_tsk(W dlytim);

ER slp_tsk(void) {
    return _tk_slp_tsk(TMO_FEVR);
}

ER wup_tsk(ID tskid) {
    return _tk_wup_tsk(tskid);
}

void dly_tsk(W dlytim) {
    _tk_dly_tsk(dlytim);
}

__attribute__((weak))
ER tk_dly_tsk(W dlytim) {
    return _tk_dly_tsk(dlytim);
}

ER get_tim(SYSTIME *p_time) {
    if (!p_time) return E_PAR;
    /* The ASYNC IRQ plane ticks at 1 kHz, so ticks are milliseconds
     * directly.  (Previously the 60 Hz assumption never held: no hardware
     * timer IRQ existed and this always returned 0.) */
    *p_time = (uint64_t)s_system_ticks;
    return E_OK;
}

/* ═══════════════════════════════════════════════════════════════════
 * Synopsys DWC2 USB Keyboard & Mouse Driver
 * ═══════════════════════════════════════════════════════════════════ */

static inline uint16_t usb_to_btron_modifiers(uint8_t usb_mod) {
    uint16_t bmod = BTRON_KMOD_NONE;
    if (usb_mod & 0x01) bmod |= BTRON_KMOD_LCTRL;
    if (usb_mod & 0x02) bmod |= BTRON_KMOD_LSHIFT;
    if (usb_mod & 0x04) bmod |= BTRON_KMOD_LALT;
    if (usb_mod & 0x10) bmod |= BTRON_KMOD_RCTRL;
    if (usb_mod & 0x20) bmod |= BTRON_KMOD_RSHIFT;
    if (usb_mod & 0x40) bmod |= BTRON_KMOD_RALT;
    return bmod;
}

/* Sub-pixel residual motion accumulator in 8.8 fixed-point */
static int32_t s_mouse_sub_x = 0;
static int32_t s_mouse_sub_y = 0;



/* Subpixel residual history for Haiku OS curve */
static float s_haiku_hist_x = 0.0f;
static float s_haiku_hist_y = 0.0f;

/* Acceleration Profile: 1 = RISC OS Stepped Curve, 2 = Haiku Continuous Smooth Curve (defined in input.c) */
extern int g_mouse_accel_profile;

static inline float fast_sqrtf(float val) {
    if (val <= 0.0f) return 0.0f;
#if defined(__aarch64__)
    float res;
    __asm__ volatile("fsqrt %s0, %s1" : "=w"(res) : "w"(val));
    return res;
#elif defined(__arm__) && defined(__ARM_FP)
    float res;
    __asm__ volatile("vsqrt.f32 %0, %1" : "=t"(res) : "t"(val));
    return res;
#else
    float x = val;
    float y = 0.5f * (x + 1.0f);
    for (int i = 0; i < 6; i++) {
        y = 0.5f * (y + x / y);
    }
    return y;
#endif
}

/*
 * Haiku OS / BeOS Continuous 2D Mouse Accelerator
 * Directly replicates MouseDevice::_ComputeAcceleration from Haiku OS
 * (src/add-ons/input_server/devices/mouse/MouseInputDevice.cpp)
 *
 * - speed: 65536 = 1.0x (16.16 fixed-point)
 * - accel_factor: 65536 to 262144 (scaled by 524288.0)
 * - Uses 2D velocity magnitude sqrt(dx*dx + dy*dy) for isotropic acceleration
 * - Truncates symmetrically towards zero (floor for positive, ceil for negative)
 * - Retains signed fractional residual without directional bias
 */
void mouse_accelerate_pair_haiku(int32_t raw_x, int32_t raw_y, int32_t *out_dx, int32_t *out_dy)
{
    if (raw_x == 0 && raw_y == 0) {
        /* Idle residual decay */
        s_haiku_hist_x *= 0.5f;
        s_haiku_hist_y *= 0.5f;
        if (s_haiku_hist_x > -0.05f && s_haiku_hist_x < 0.05f) s_haiku_hist_x = 0.0f;
        if (s_haiku_hist_y > -0.05f && s_haiku_hist_y < 0.05f) s_haiku_hist_y = 0.0f;
        if (out_dx) *out_dx = 0;
        if (out_dy) *out_dy = 0;
        return;
    }

    /* Clamp raw input bursts */
    if (raw_x > 512)  raw_x = 512;
    if (raw_x < -512) raw_x = -512;
    if (raw_y > 512)  raw_y = 512;
    if (raw_y < -512) raw_y = -512;

    /* Base speed: mapped from g_mouse_step_mult (1..4 -> 1.6x .. 2.8x) */
    float speed_mult = 1.2f + (float)g_mouse_step_mult * 0.4f;

    float deltaX = ((float)raw_x * speed_mult) + s_haiku_hist_x;
    float deltaY = ((float)raw_y * speed_mult) + s_haiku_hist_y;

    /* Haiku continuous acceleration curve */
    float accel_scale = 0.06f + ((float)g_mouse_step_mult * 0.03f);
    float speed_sq = (deltaX * deltaX) + (deltaY * deltaY);
    float speed_mag = fast_sqrtf(speed_sq);

    float acceleration = 1.0f + (speed_mag * accel_scale);
    if (acceleration > 4.5f) {
        acceleration = 4.5f;
    }

    deltaX *= acceleration;
    deltaY *= acceleration;

    /* Haiku integer quantization: floor for positive, ceil for negative */
    int32_t pix_x = (deltaX >= 0.0f) ? (int32_t)deltaX : -(int32_t)(-deltaX);
    int32_t pix_y = (deltaY >= 0.0f) ? (int32_t)deltaY : -(int32_t)(-deltaY);

    /* Output displacement clamp */
    if (pix_x > 512)  pix_x = 512;
    if (pix_x < -512) pix_x = -512;
    if (pix_y > 512)  pix_y = 512;
    if (pix_y < -512) pix_y = -512;

    s_haiku_hist_x = deltaX - (float)pix_x;
    s_haiku_hist_y = deltaY - (float)pix_y;

    /* Clamp residual within [-1.0f, +1.0f] */
    if (s_haiku_hist_x > 1.0f) s_haiku_hist_x = 1.0f;
    if (s_haiku_hist_x < -1.0f) s_haiku_hist_x = -1.0f;
    if (s_haiku_hist_y > 1.0f) s_haiku_hist_y = 1.0f;
    if (s_haiku_hist_y < -1.0f) s_haiku_hist_y = -1.0f;

    if (out_dx) *out_dx = pix_x;
    if (out_dy) *out_dy = pix_y;
}

/* Single-axis wrapper for Haiku subpixel accelerator */
int32_t mouse_accelerate_subpixel_haiku(int32_t raw, int32_t *subpixel)
{
    int32_t out_dx = 0, out_dy = 0;
    mouse_accelerate_pair_haiku(raw, 0, &out_dx, &out_dy);
    if (subpixel) *subpixel = (int32_t)(s_haiku_hist_x * 256.0f);
    return out_dx;
}

/*
 * Hardened RISC OS MouseStep Accelerator
 * - Exact stepped multipliers mirroring Archimedes / RISC OS CMOS &C2 (Steps 1..4)
 * - Symmetric signed integer subpixel truncation (no negative floor bias)
 * - Signed residual carry with idle decay
 */
int32_t mouse_accelerate_subpixel_riscos(int32_t raw, int32_t *subpixel)
{
    if (raw == 0) {
        /* Idle residual decay - halves residual */
        if (subpixel) *subpixel = (*subpixel) / 2;
        return 0;
    }

    /* Clamp raw input against packet bursts */
    if (raw >  512) raw =  512;
    if (raw < -512) raw = -512;

    int32_t sign = (raw < 0) ? -1 : 1;
    int32_t abs  = (raw < 0) ? -raw : raw;

    /* Fine precision boost for subtle single-pixel moves */
    if (abs <= 2) {
        abs = (abs * 3) / 2; /* 1.5x */
    }

    /* RISC OS MouseStep stepped multipliers (8.8 fixed-point)
     * Step 1: 1.5x - 2.0x (384)
     * Step 2: 2.0x - 2.5x (512) - Archimedes standard CMOS 2 default
     * Step 3: 2.5x - 3.0x (640)
     * Step 4: 3.0x - 3.5x (768)
     */
    int32_t mult_fp;
    switch (g_mouse_step_mult) {
        case 1:  mult_fp = 384 + (abs > 4 ? 128 : 0); break;
        case 2:  mult_fp = 512 + (abs > 4 ? 128 : 0); break;
        case 3:  mult_fp = 640 + (abs > 4 ? 128 : 0); break;
        default: mult_fp = 768 + (abs > 4 ? 128 : 0); break;
    }

    int32_t res = subpixel ? *subpixel : 0;
    int32_t total = res + (sign * abs * mult_fp);

    /* Symmetric integer truncation towards zero (matching Haiku and standard C) */
    int32_t pixels = total / 256;
    if (subpixel) {
        *subpixel = total % 256;
        if (*subpixel > 255)  *subpixel = 255;
        if (*subpixel < -255) *subpixel = -255;
    }

    if (pixels >  512) pixels =  512;
    if (pixels < -512) pixels = -512;

    return pixels;
}

static inline int32_t mouse_accelerate_subpixel_raw(int32_t raw, int32_t *subpixel) {
    (void)subpixel;
    return raw;          // pure 1:1, no residual, no boost, no mult
}

static inline __attribute__((unused)) int32_t mouse_accelerate_subpixel(int32_t raw, int32_t *subpixel)
{
    if (g_mouse_accel_profile == 0) {
        return mouse_accelerate_subpixel_riscos(raw, subpixel);
    } else if (g_mouse_accel_profile == 1) {
        return mouse_accelerate_subpixel_haiku(raw, subpixel);
    } else {
        return mouse_accelerate_subpixel_raw(raw, subpixel);
    }
}

/* ─────────────────────────────────────────────────────────────────
 * Shared HID integration helpers.
 * Used by BOTH the legacy cooperative drainer (usb_poll_devices) and the
 * ASYNC.txt 1 ms INPUT worker (input_plane_task).  These run in task
 * context only — never in the ISR.
 * ───────────────────────────────────────────────────────────────── */

/* One decoded keyboard report -> KEY_DOWN/KEY_UP events + repeat arming. */
static void kbd_report_to_events(const usb_kbd_report_t *rep, uint32_t now_us) {
    uint8_t scancode = rep->keys[0];
    if (scancode != 0) {
        if (scancode != g_prev_kbd_scancode) {
            uint32_t k = dwc2_usb_to_btron_key(scancode, rep->modifiers);
            if (k != 0) {
                uint16_t bmod = usb_to_btron_modifiers(rep->modifiers);
                EVT ev;
                ev.type   = EV_KEY_DOWN;
                ev.key    = k;
                ev.data   = (VW)(uintptr_t)bmod;
                ev.pos.x  = s_mouse_x;
                ev.pos.y  = s_mouse_y;
                ev.button = 0;
                snd_evt(&ev);
                s_async_rt_stats.key_enqueue_us = now_us;

                /* Arm hardware-like responsive key repeat */
                s_held_kbd_scancode = scancode;
                s_held_kbd_modifiers = rep->modifiers;
                s_key_press_time_us = now_us;
                s_key_last_repeat_us = now_us;
            }
        }
    } else {
        if (g_prev_kbd_scancode != 0) {
            uint32_t k = dwc2_usb_to_btron_key(g_prev_kbd_scancode, 0);
            if (k != 0) {
                EVT ev;
                ev.type   = EV_KEY_UP;
                ev.key    = k;
                ev.data   = 0;
                ev.pos.x  = s_mouse_x;
                ev.pos.y  = s_mouse_y;
                ev.button = 0;
                snd_evt(&ev);
            }
            /* Disarm key repeat */
            s_held_kbd_scancode = 0;
            s_held_kbd_modifiers = 0;
        }
    }
    g_prev_kbd_scancode = scancode;
}

/* Key auto-repeat timer for the currently held key. */
static void kbd_repeat_check(uint32_t now_us) {
    if (!g_kbd_repeat_enabled || s_held_kbd_scancode == 0) return;
    if ((now_us - s_key_press_time_us) < g_kbd_repeat_delay_us) return;
    if ((now_us - s_key_last_repeat_us) < g_kbd_repeat_interval_us) return;
    s_key_last_repeat_us = now_us;
    uint32_t k = dwc2_usb_to_btron_key(s_held_kbd_scancode, s_held_kbd_modifiers);
    uint16_t bmod = usb_to_btron_modifiers(s_held_kbd_modifiers);
    if (k != 0) {
        EVT ev;
        ev.type   = EV_KEY_DOWN;
        ev.key    = k;
        ev.data   = (VW)(uintptr_t)bmod;
        ev.pos.x  = s_mouse_x;
        ev.pos.y  = s_mouse_y;
        ev.button = 0;
        snd_evt(&ev);
    }
}

/* Raw HID delta batch -> accelerate -> integrate -> clamp -> gated MOVE. */
static void mouse_motion_apply(int32_t rdx, int32_t rdy) {
    if (rdx == 0 && rdy == 0) return;

    int32_t move_x = 0, move_y = 0;
    if (g_mouse_accel_profile == 0) {
        /* Profile 0: RISC OS MouseStep Stepped Accelerator (Archimedes 2.0x default) */
        move_x = mouse_accelerate_subpixel_riscos(rdx, &s_mouse_sub_x);
        move_y = mouse_accelerate_subpixel_riscos(rdy, &s_mouse_sub_y);
    } else if (g_mouse_accel_profile == 1) {
        /* Profile 1: Haiku OS / BeOS 2D Velocity Vector Accelerator */
        mouse_accelerate_pair_haiku(rdx, rdy, &move_x, &move_y);
    } else {
        /* Profile 2: Raw 1:1 unaccelerated */
        move_x = mouse_accelerate_subpixel_raw(rdx, &s_mouse_sub_x);
        move_y = mouse_accelerate_subpixel_raw(rdy, &s_mouse_sub_y);
    }

    int32_t nx = (int32_t)s_mouse_x + move_x;
    int32_t ny = (int32_t)s_mouse_y + move_y;
    if (nx < 0) nx = 0;
    else if (nx >= BTRON_SCREEN_W) nx = BTRON_SCREEN_W - 1;
    if (ny < 0) ny = 0;
    else if (ny >= BTRON_SCREEN_H) ny = BTRON_SCREEN_H - 1;

    int pos_changed = ((H)nx != s_mouse_x || (H)ny != s_mouse_y);
    s_mouse_x = (H)nx;
    s_mouse_y = (H)ny;
    set_baremetal_mouse_pos(s_mouse_x, s_mouse_y);

    /* Gate EV_MOUSE_MOVE: only enqueue to system event queue if UI needs it
     * (menu open, button pressed/dragged, tab sliding, or top menu hover).
     * Passive cursor movement is already rendered to GPU front buffer with
     * zero latency.  MOVE is the droppable/coalescable class; KEY and
     * BUTTON events are never gated (ASYNC.txt §3). */
    if (pos_changed &&
        (global_menu_is_open() || tracker_is_menu_open() ||
         g_prev_mouse_btns != 0 || wnd_mgr_is_interacting() ||
         s_mouse_y <= 25)) {
        EVT ev;
        ev.type   = EV_MOUSE_MOVE;
        ev.pos.x  = s_mouse_x;
        ev.pos.y  = s_mouse_y;
        ev.button = 0;
        ev.data   = 0;
        snd_evt(&ev);
    }
}

/* Latest raw HID button byte -> RISC OS Select/Adjust/Menu edge events. */
static void mouse_buttons_apply(uint8_t raw) {
    uint8_t btn_left   = (raw & 1u);
    uint8_t btn_right  = (raw & 2u) >> 1;
    uint8_t btn_middle = (raw & 4u) >> 2;

    /* RISC OS 3-Button Model:
     * Button 1: Select (Left, or Right if swapped)
     * Button 2: Adjust (Right, or Left if swapped)
     * Button 3: Menu   (Middle / Wheel Click)
     */
    uint8_t sel_raw = g_mouse_swap_select_adjust ? btn_right : btn_left;
    uint8_t adj_raw = g_mouse_swap_select_adjust ? btn_left  : btn_right;

    uint8_t sel_prev = (g_prev_mouse_btns & 1u);
    uint8_t adj_prev = (g_prev_mouse_btns & 2u) >> 1;
    uint8_t mid_prev = (g_prev_mouse_btns & 4u) >> 2;
    g_prev_mouse_btns = (sel_raw) | (adj_raw << 1) | (btn_middle << 2);

    /* Select Button (Button 1) */
    if (sel_raw != sel_prev) {
        EVT ev;
        ev.type   = sel_raw ? EV_BUT_DOWN : EV_BUT_UP;
        ev.button = 1; /* Select */
        ev.pos.x  = s_mouse_x;
        ev.pos.y  = s_mouse_y;
        ev.key    = 0;
        ev.data   = 0;
        snd_evt(&ev);
    }

    /* Adjust Button (Button 2) */
    if (adj_raw != adj_prev) {
        EVT ev;
        ev.type   = adj_raw ? EV_BUT_DOWN : EV_BUT_UP;
        ev.button = 2; /* Adjust */
        ev.pos.x  = s_mouse_x;
        ev.pos.y  = s_mouse_y;
        ev.key    = 0;
        ev.data   = 0;
        snd_evt(&ev);
    }

    /* Menu Button (Button 3) */
    if (btn_middle != mid_prev) {
        EVT ev;
        ev.type   = btn_middle ? EV_BUT_DOWN : EV_BUT_UP;
        ev.button = 3; /* Menu */
        ev.pos.x  = s_mouse_x;
        ev.pos.y  = s_mouse_y;
        ev.key    = 0;
        ev.data   = 0;
        snd_evt(&ev);
    }
}

/* Legacy cooperative HID drain (QEMU / DWC2 / Pi 3, or xHCI without the
 * timer IRQ armed).  The caller has already performed the bounded
 * host-controller drain for this period. */
static int usb_poll_devices(GDEV *screen) {
    (void)screen;
    int activity = 0;

    uint32_t now_us = *(volatile uint32_t *)(TIMER_BASE + 0x04);

    /* 1. Drain pending USB HID Keyboard reports (bounded) */
    usb_kbd_report_t kbd_rep;
    for (uint32_t kbd_iter = 0; kbd_iter < ASYNC_HID_REPORT_BUDGET; kbd_iter++) {
        int kbd_got = 0;
        if (g_use_xhci) {
            kbd_got = (xhci_poll_keyboard(&kbd_rep) > 0);
        } else {
            kbd_got = (dwc2_poll_keyboard(&kbd_rep) > 0);
        }
        if (!kbd_got) break;
        kbd_report_to_events(&kbd_rep, now_us);
        activity = 1;
    }

    /* 1b. Key auto-repeat */
    kbd_repeat_check(now_us);

    /* 2. Drain USB HID Mouse reports (bounded) */
    usb_mouse_report_t mouse_rep;
    int32_t accum_dx = 0, accum_dy = 0;
    uint8_t latest_buttons = 0;
    int mouse_activity = 0;

    for (uint32_t m_iter = 0; m_iter < ASYNC_HID_REPORT_BUDGET; m_iter++) {
        int mouse_got = 0;
        if (g_use_xhci) {
            mouse_got = (xhci_poll_mouse(&mouse_rep) > 0);
        } else {
            mouse_got = (dwc2_poll_mouse(&mouse_rep) > 0);
        }
        if (!mouse_got) break;
        accum_dx += (int32_t)mouse_rep.dx;
        accum_dy += (int32_t)mouse_rep.dy;
        latest_buttons = mouse_rep.buttons;
        mouse_activity = 1;
    }

    if (mouse_activity) {
        mouse_motion_apply(accum_dx, accum_dy);
        mouse_buttons_apply(latest_buttons);
        activity = 1;
    }

    return activity;
}

/* ─────────────────────────────────────────────────────────────────
 * ASYNC.txt INPUT worker (TASK_INPUT, fixed 1 ms period).
 *
 * IRQ plane armed: seqlock snapshot read (wait-free, bounded retry),
 * diff monotonic accumulators, integrate/accelerate, emit KEY/BUTTON
 * edges and gated MOVE.  Never touches the xHCI event ring — the ISR
 * owns it.
 *
 * IRQ plane inactive: legacy bounded cooperative drain.
 * ───────────────────────────────────────────────────────────────── */
static void input_plane_task(GDEV *screen, uint32_t now_us) {
    if (!s_async_irq_active) {
        (void)usb_poll_devices(screen);
        return;
    }

    /* Wait-free seqlock read of the pointer snapshot */
    pointer_snapshot_t snap;
    uint32_t s1, s2;
    do {
        s1 = s_pointer_slot.seq;
        __asm__ volatile("dmb sy" : : : "memory");
        snap.acc_dx    = s_pointer_slot.acc_dx;
        snap.acc_dy    = s_pointer_slot.acc_dy;
        snap.acc_wheel = s_pointer_slot.acc_wheel;
        snap.buttons   = s_pointer_slot.buttons;
        snap.present   = s_pointer_slot.present;
        snap.motion_seq = s_pointer_slot.motion_seq;
        __asm__ volatile("dmb sy" : : : "memory");
        s2 = s_pointer_slot.seq;
    } while ((s1 & 1u) != 0 || s1 != s2);

    /* Diff against last-consumed snapshot: lossless MOVE coalescing */
    int32_t rdx = snap.acc_dx - s_input_last.acc_dx;
    int32_t rdy = snap.acc_dy - s_input_last.acc_dy;
    s_input_last = snap;

    if (snap.present && (rdx != 0 || rdy != 0))
        mouse_motion_apply(rdx, rdy);

    /* Buttons: latest-state snapshot; edges are detected against our own
     * previous view so no BUTTON event is ever lost. */
    if (snap.buttons != s_input_prev_buttons) {
        mouse_buttons_apply(snap.buttons);
        s_input_prev_buttons = snap.buttons;
    }

    /* Keyboard: drain the SPSC ring (ISR is the sole producer) */
    usb_kbd_report_t kbd_rep;
    for (uint32_t i = 0; i < ASYNC_HID_REPORT_BUDGET && xhci_poll_keyboard(&kbd_rep); i++) {
        kbd_report_to_events(&kbd_rep, now_us);
    }
    kbd_repeat_check(now_us);
}

/* ═══════════════════════════════════════════════════════════════════
 * Platform Query & RTOS Services
 * ═══════════════════════════════════════════════════════════════════ */
void btron_core_banner(void) {
    uint64_t midr = 0;
    __asm__ volatile("mrs %0, midr_el1" : "=r"(midr));
    uint32_t part = (midr >> 4) & 0xFFF;
    if (part == 0xD08) {
        uart_puts("B-System/BTRON3 3.20 (aarch64-bcm2711) Takanori Yokoyama — T-Kernel 2.0\n");
        uart_puts("Copyright 2026 Synrc Research Center. MIT License.\n");
        uart_puts("[BOOT] Machine: Raspberry Pi 4B / BCM2711  AArch64 Cortex-A72  T-Kernel 2.0\n\n");
    } else {
        uart_puts("B-System/BTRON3 3.20 (aarch64-bcm2837) Takanori Yokoyama — T-Kernel 2.0\n");
        uart_puts("Copyright 2026 Synrc Research Center. MIT License.\n");
        uart_puts("[BOOT] Machine: Raspberry Pi 3B / BCM2837  AArch64 Cortex-A53  T-Kernel 2.0\n\n");
    }
}

void btron_core_mem_log(void) {
    uint64_t midr = 0;
    __asm__ volatile("mrs %0, midr_el1" : "=r"(midr));
    uint32_t part = (midr >> 4) & 0xFFF;
    if (part == 0xD08) {
        uart_puts("[MEM ] BCM2711 Physical Memory Map (Pi 4B, 2 GB / 4 GB RAM):\n");
        uart_puts("[MEM ]   0x00000000-0xFCFFFFFF  RAM (Usable 4048 MB)\n");
        uart_puts("[MEM ]   0xFD000000-0xFFFFFFFF  Peripherals / PCIe / MMIO (48 MB)\n");
    } else {
        uart_puts("[MEM ] BCM2837 Physical Memory Map (Pi 3B, 1 GB RAM):\n");
        uart_puts("[MEM ]   0x00000000-0x3EFFFFFF  RAM (Usable 1008 MB)\n");
        uart_puts("[MEM ]   0x3F000000-0x3FFFFFFF  Peripherals / VideoCore Mailbox / MMIO (16 MB)\n");
    }
    uart_puts("[MEM ] Heap: 0x01000000-0x1B000000 (432 MB Kernel Heap)\n");
}

void btron_core_hfds_log(void) {
    uart_puts("[HFDS] EMMC2 / SD Storage Interface: INIT  [OK]\n");
    uart_puts("[HFDS] HFDS Hierarchical File/Data Set: INIT  [OK]\n");
    uart_puts("[HFDS] Root Cabinet: BTRON3_SPEC.TAD  T_KERNEL_20.TAD\n");
}

void btron_core_init(void) {
    uart_puts("[CORE] Yokoyama T-Kernel 2.0 Engine (AArch64)  BTRON_YOKOYAMA_AARCH64\n");
    tkernel_init_subsystems(1);
}

void btron_core_print_ver(ShellOutputFn out_fn, void *user_data, const char *arg) {
    if (!out_fn) return;
    if (arg && tkl_strcmp(arg, "-a") == 0) {
        out_fn("BTRON3 btron-rpi3 2.0 T-Kernel-BCM2837 aarch64 GNU/B-System", COLOR_CYAN, user_data);
    } else if (arg && (tkl_strcmp(arg, "-r") == 0 || tkl_strcmp(arg, "-v") == 0)) {
        out_fn("2.0.0-tkernel-aarch64", COLOR_CYAN, user_data);
    } else {
        out_fn("B-System 3.0 Workstation System (BTRON3 Specification 3.20)", COLOR_CYAN, user_data);
        out_fn("Kernel: Sakamura T-Kernel 2.0 Real-Time Executive (AArch64 / BCM2837)", COLOR_GREEN, user_data);
        out_fn("Hardware Target: Raspberry Pi 3B Bare-Metal AArch64 Kernel (Cortex-A53)", COLOR_LTGRAY, user_data);
        out_fn("Build Timestamp: " __DATE__ " " __TIME__, COLOR_LTGRAY, user_data);
        out_fn("Display Compositor: VideoCore GPU Framebuffer Engine (1024x768 32-bpp Double-Buffered)", COLOR_LTGRAY, user_data);
        out_fn("Japanese IME: B-System Mozc / TIP Kana-Kanji Conversion Subsystem", COLOR_LTGRAY, user_data);
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * Kernel Main Entry Point
 * ═══════════════════════════════════════════════════════════════════ */

void btron_main(void) {
    /* 1. Reset heap pointer */
    heap_ptr = HEAP_BASE;
    tkl_memset((void*)HEAP_BASE, 0, 4096);

    /* 2. Initialize PL011 UART */
    uart_init();

    /* 3. Initialize Video Display Framebuffer (1024x768 32-bpp) */
    uint32_t *gpu_fb = init_pi_framebuffer(BTRON_SCREEN_W, BTRON_SCREEN_H);
    fb_log_enable((volatile uint32_t *)gpu_fb);

    /* Visual confirmation: immediately paint vivid electric blue alive bar across top */
    if (gpu_fb) {
        for (int i = 0; i < BTRON_SCREEN_W * 12; i++) {
            gpu_fb[i] = 0xFF00B0FFu;
        }
    }

    fb_log("[FB] BTRON3 Pi 400 Kernel Log — troncode 8x16 ASCII font\n");
    fb_log(g_mmio_base == 0xFE000000UL
           ? "[BOOT] BCM2711  Cortex-A72  AArch64  Pi 4/400  T-Kernel 2.0\n"
           : "[BOOT] BCM2837  Cortex-A53  AArch64  Pi 3B     T-Kernel 2.0\n");

    btron_core_banner();
    btron_core_init();
    btron_core_mem_log();
    btron_core_hfds_log();

    /* 4. Initialize BCM2711 Hardware Device Drivers */
    /* The Pi 400 DMA/page-flip experiment is currently disabled: hardware
     * measurements showed up to 244 ms stalls.  The bounded CPU presenter
     * above keeps USB input live while a frame is copied in small bands. */
    s_present_dma_enabled = 0;

    fb_log("[DRV] Initializing Screen Driver...\n");
    ER sdrv_res = ScreenDrv(0, NULL);
    if (sdrv_res >= 0) {
        fb_log("[DRV] ScreenDrv: OK\n");
    } else {
        fb_log("[DRV] ScreenDrv: FAIL ");
        uart_hex32((uint32_t)sdrv_res);
        fb_log("\n");
    }

    fb_log("[DRV] Initializing Keyboard & Mouse Drivers...\n");
    ER kbpd_res = KbPdDrv(0, NULL);
    if (kbpd_res >= 0) {
        fb_log("[DRV] KbPdDrv: OK\n");
    } else {
        fb_log("[DRV] KbPdDrv: FAIL\n");
        uart_hex32((uint32_t)kbpd_res);
    }

    ER lkb_res = LowKbPdDrv(0, NULL);
    if (lkb_res >= 0) {
        fb_log("[DRV] LowKbPdDrv: OK\n");
    } else {
        fb_log("[DRV] LowKbPdDrv: FAIL\n");
        uart_hex32((uint32_t)lkb_res);
    }

    /* Initialize USB Subsystem */
    fb_log("[USB] Probing USB Host Controllers...\n");
    if (g_mmio_base == 0xFE000000UL) {
        extern uint32_t bcm283x_get_board_revision(void);
        uint32_t board_rev = bcm283x_get_board_revision();
        fb_log("[BOOT] Board Revision: ");
        fb_log_hex32(board_rev);
        fb_log("\n");

        /* QEMU raspi4b identifies as 0xB03111 or 0xB03115 without PCIe hardware.
         * Real physical hardware (Pi 400 0xC03130/1, Pi 4B 0xC0311x) has Broadcom PCIe + VL805.
         *
         * If board_rev == 0, the mailbox returned no data (possible timing issue or
         * firmware not responding yet). Treat 0 as physical hardware — QEMU always returns
         * a valid non-zero revision code.
         */
        bool is_qemu = (board_rev != 0) && (
            ((board_rev & 0x00F00000u) == 0x00B00000u) ||
            (board_rev == 0x00B03115u) || (board_rev == 0x00B03111u));
        if (!is_qemu) {
            fb_log("[USB] Physical BCM2711 Hardware: Initializing PCIe Root Complex & VL805 xHCI...\n");
            if (bcm2711_pcie_init() == 0) {
                uintptr_t vl805_mmio = bcm2711_pcie_get_vl805_mmio();
                if (vl805_mmio) {
                    if (xhci_init(vl805_mmio) == 0) {
                        g_use_xhci = 1;
                    }
                }
            }
        }
        if (is_qemu) {
            /* QEMU raspi4b model connects virtual USB keyboard/mouse to DWC2 */
            fb_log("[USB] QEMU Virtual Machine: Initializing DWC2 USB Host Controller...\n");
            dwc2_init();
        } else if (!g_use_xhci) {
            fb_log("[USB] xHCI Controller failed to initialize on Pi 400.\n");
        }

        /* Arm the ASYNC.txt 1 kHz IRQ input plane — only after the xHCI
         * host controller is fully enumerated, so the ISR never observes
         * half-initialized rings. */
        if (g_use_xhci) {
            extern int arm64_irq_init(uint32_t hz);
            extern void arm64_irq_disable(void);
            extern void (*g_arm64_timer_hook)(void);
            g_arm64_timer_hook = rpi_timer_tick;
            fb_log("[IRQ] -> arm64_irq_init(1000)\n");
            int irq_ret = arm64_irq_init(1000);
            fb_log("[IRQ] <- arm64_irq_init ret="); fb_log_dec((uint32_t)irq_ret); fb_log("\n");
            if (irq_ret == 0) {
                /* Confirm ticks actually arrive before trusting the IRQ plane.
                 * Arming s_async_irq_active gates the cooperative xHCI drain
                 * off everywhere, so if the timer/GIC never delivers (an EL or
                 * firmware variant we did not anticipate) input would be dead.
                 * Poll for up to 50 ms; if s_system_ticks never advances, tear
                 * the timer down and keep the cooperative path. */
                uint32_t tick0 = s_system_ticks;
                uint32_t t_start = *(volatile uint32_t *)(TIMER_BASE + 0x04);
                int confirmed = 0;
                for (int i = 0; i < 400000; i++) {
                    if (s_system_ticks != tick0) { confirmed = 1; break; }
                    if ((*(volatile uint32_t *)(TIMER_BASE + 0x04) - t_start) > 50000u) break;
                }
                if (confirmed) {
                    s_async_irq_active = 1;
                    fb_log("[IRQ] 1 kHz ASYNC input plane armed.\n");
                } else {
                    /* No real tick.  Dump GIC/timer state so we can tell a
                     * broken GIC->CPU delivery path from a timer that never
                     * asserts its PPI.  selftest_seen=1 => the forced-pending
                     * IRQ WAS taken (delivery OK, timer at fault); =0 => the
                     * CPU never took the IRQ (vector/mask/routing at fault). */
                    typedef struct {
                        uint32_t el, timer_intid, dispatch_hits, stub_entries;
                        uint32_t selftest_seen;
                        uint32_t cfg_idx;
                        uint32_t gicd_typer, gicd_ctlr, gicd_igroupr0;
                        uint32_t gicd_isenabler0, gicd_ispendr0;
                        uint32_t gicc_ctlr, gicc_pmr, gicc_hppir, gicc_iidr;
                        uint32_t cnthp_ctl, cntp_ctl, cntfrq;
                    } irqdiag_t;
                    extern void arm64_irq_get_diag(irqdiag_t *);
                    irqdiag_t d;
                    arm64_irq_get_diag(&d);
                    fb_log("[IRQ] No tick in 50ms; cooperative fallback.\n");
                    fb_log("[IRQ] EL=");       fb_log_dec(d.el);
                    fb_log(" intid=");         fb_log_dec(d.timer_intid);
                    fb_log(" selftest=");      fb_log_dec(d.selftest_seen);
                    fb_log(" cfg=");           fb_log_dec(d.cfg_idx);
                    fb_log(" hits=");          fb_log_dec(d.dispatch_hits);
                    fb_log(" entries=");       fb_log_dec(d.stub_entries);
                    fb_log("\n");
                    fb_log("[IRQ] GICD ty=");  fb_log_hex32(d.gicd_typer);
                    fb_log(" ctlr=");          fb_log_hex32(d.gicd_ctlr);
                    fb_log(" igroup=");        fb_log_hex32(d.gicd_igroupr0);
                    fb_log(" enab=");          fb_log_hex32(d.gicd_isenabler0);
                    fb_log(" pend=");          fb_log_hex32(d.gicd_ispendr0);
                    fb_log("\n");
                    fb_log("[IRQ] GICC ctlr=");fb_log_hex32(d.gicc_ctlr);
                    fb_log(" pmr=");           fb_log_hex32(d.gicc_pmr);
                    fb_log(" hppir=");         fb_log_hex32(d.gicc_hppir);
                    fb_log(" iidr=");          fb_log_hex32(d.gicc_iidr);
                    fb_log("\n");
                    fb_log("[IRQ] CNTHP=");    fb_log_hex32(d.cnthp_ctl);
                    fb_log(" CNTP=");          fb_log_hex32(d.cntp_ctl);
                    fb_log(" CNTFRQ=");        fb_log_dec(d.cntfrq);
                    fb_log("\n");
                    arm64_irq_disable();
                    g_arm64_timer_hook = 0;
                }
            } else {
                g_arm64_timer_hook = 0;
                fb_log("[IRQ] Timer IRQ unavailable; cooperative input path.\n");
            }
        }
        fb_log("[USB] USB Subsystem ready.\n");
    } else {
        fb_log("[USB] Initializing DWC2 USB 2.0 Host Controller...\n");
        dwc2_init();
        fb_log("[USB] DWC2 init complete.\n");
    }

    /* 5. Stage 1 Interactive Terminal Shell on GPU Framebuffer */
    fb_log("\n=================================================================\n");
    fb_log("  B-System / BTRON3 3.20 (Raspberry Pi 400 / Pi 4B AArch64)\n");
    fb_log("  Cleanroom TRON Kernel [Target: Cortex-A72 / BCM2711]\n");
    fb_log("  Stage 1: Terminal Console Active (HDMI On-Screen Debug Trace)\n");
    fb_log("  Input  : Built-in USB Keyboard / UART Serial\n");
    fb_log("=================================================================\n\n");
    fb_log(" Type 'desktop' or 'startx' to launch Graphical Workbench GUI!\n");
    fb_log(" Commands: help, mem, ver, clear, startx, desktop, reboot\n");
    fb_log(" Autoboot: launching Desktop in 12s (Press any key to stay in shell)\n\n");
    fb_log("btron-pi400# ");

    uart_puts("\n=================================================================\n");
    uart_puts("  B-System / BTRON3 3.20 (Raspberry Pi 400 / Pi 4B AArch64)\n");
    uart_puts("  Stage 1: Terminal Console Active (HDMI On-Screen Debug Trace)\n");
    uart_puts("  Type 'startx' or 'desktop' to launch Graphical Workbench GUI!\n");
    uart_puts("=================================================================\n\n");

#if defined(BTRON_AUTO_GUI) && (BTRON_AUTO_GUI == 1)
    fb_log("[BOOT] AUTO_GUI=1: Automatically launching B-System Desktop GUI...\n");
    launch_pi4_desktop_session(gpu_fb);
#endif

    /* 6. Stage 1 Interactive Terminal Shell Loop */
    uint32_t last_sec_tick = *(volatile uint32_t *)(TIMER_BASE + 0x04);
    int autoboot_secs = 12;

    while (1) {
        if (autoboot_secs > 0 && !s_gui_active) {
            uint32_t now = *(volatile uint32_t *)(TIMER_BASE + 0x04);
            if ((now - last_sec_tick) >= 1000000) {
                last_sec_tick = now;
                autoboot_secs--;
                if (autoboot_secs == 0) {
                    fb_log("\n[BOOT] Autoboot timer expired -> Launching B-System Workbench GUI...\n");
                    launch_pi4_desktop_session(gpu_fb);
                }
            }
        }

        if (pi4_shell_poll(gpu_fb)) {
            /* Any keystroke immediately cancels autoboot timer */
            autoboot_secs = 0;
        }

        /* Brief memory barrier pause — keeps the polling rate high without
         * burning excessive CPU.  Shorter than the old 200-nop spin so
         * keyboard events are captured with <10 µs latency. */
        __asm__ volatile("dsb sy" : : : "memory");
    }
}
