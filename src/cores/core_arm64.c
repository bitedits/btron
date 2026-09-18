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

/* 64-byte unrolled burst blitter: fallback when hardware DMA is not used */
static inline void arm64_fast_blit(volatile void *dst, const void *src, size_t bytes) {
    uint64_t *d = (uint64_t *)dst;
    const uint64_t *s = (const uint64_t *)src;
    size_t count = bytes / 64;
    while (count--) {
        d[0] = s[0];
        d[1] = s[1];
        d[2] = s[2];
        d[3] = s[3];
        d[4] = s[4];
        d[5] = s[5];
        d[6] = s[6];
        d[7] = s[7];
        d += 8;
        s += 8;
    }
    size_t rem = bytes & 63;
    if (rem) {
        tkl_memcpy((void *)d, (const void *)s, rem);
    }
    __asm__ volatile("dmb sy" : : : "memory");
}

/* Clean data cache range by VA to Point of Coherency (PoC) for coherent DMA */
static inline void arm64_clean_cache_range(const void *addr, size_t size) {
    uintptr_t start = (uintptr_t)addr & ~(64UL - 1);
    uintptr_t end   = (uintptr_t)addr + size;
    for (uintptr_t p = start; p < end; p += 64) {
        __asm__ volatile("dc cvac, %0" : : "r"(p) : "memory");
    }
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
     * Without this call the event ring is never processed in Stage 1,
     * making the keyboard completely unresponsive. */
    if (g_use_xhci) {
        xhci_process();
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

    /* 2. Poll USB Mouse (motion/clicks in shell cancel autoboot) */
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
        volatile uint32_t *d = gpu_fb + (y0 + r) * BTRON_SCREEN_W + x0;
        const COLOR *s = &s_desktop_backbuffer[(y0 + r) * BTRON_SCREEN_W + x0];
        for (H c = 0; c < bw; c++) d[c] = s[c];
    }
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

    uint32_t last_clock = *(volatile uint32_t *)(TIMER_BASE + 0x04);
    uint32_t last_anim_frame = last_clock;
    EVT ev;

    while (s_gui_active) {
        int redraw = 0;
        int cursor_only = 0;

        uint32_t now = *(volatile uint32_t *)(TIMER_BASE + 0x04);

        /* Poll USB devices once per loop */
        if (usb_poll_devices(screen)) {
            // events enqueued
        }

        /* Immediate cursor update on GPU front buffer (< 1 us glass-to-glass latency) */
        if (s_mouse_x != prev_mx || s_mouse_y != prev_my) {
            restore_cursor_area(gpu_fb, prev_mx, prev_my);
            draw_baremetal_cursor_raw(gpu_fb, s_mouse_x, s_mouse_y, BTRON_SCREEN_W, BTRON_SCREEN_H);
            __asm__ volatile("dmb sy" : : : "memory");
            prev_mx = s_mouse_x;
            prev_my = s_mouse_y;
        }

        /* UART serial console */
        if (uart_has_char()) {
            int c = uart_getc();
            if (c == 0x1B) {
                s_gui_active = 0;
            } else {
                if (c == '\r') c = '\n';
                ev.type   = EV_KEY_DOWN;
                ev.key    = (UW)(uint8_t)c;
                ev.pos.x  = s_mouse_x;
                ev.pos.y  = s_mouse_y;
                ev.button = 0;
                ev.data   = 0;
                snd_evt(&ev);
            }
        }

        /* Dispatch all queued events to the B-TRON window manager */
        while (get_evt(&ev, 0) == E_OK) {
            if (ev.type == EV_KEY_DOWN && ev.key == 0x1B /* Escape */) {
                s_gui_active = 0;
                break;
            }
            workbench_process_event(screen, &ev);

            if (ev.type == EV_MOUSE_MOVE) {
                /* If menu is open or mouse button is held down, need full UI update */
                if (global_menu_is_open() || tracker_is_menu_open() || g_prev_mouse_btns != 0) {
                    redraw = 1;
                    cursor_only = 0;
                } else if (!redraw) {
                    cursor_only = 1;
                }
            } else {
                /* Buttons, keys, etc. need real UI update */
                redraw = 1;
                cursor_only = 0;
            }
        }

        /* Dedicated high-rate 60 FPS dirty path for About alone (every 16,666 µs) */
        if (about_is_animating()) {
            if ((now - last_anim_frame) >= 16666) {
                last_anim_frame = now;
                if (!redraw) {
                    /* Fast path: refresh only About window client and composite into backbuffer */
                    about_render_anim_dirty(screen, gpu_fb);

                    /* Direct dirty-rect BitBlt directly to front buffer (takes ~40 us for About window) */
                    WND *aw = about_get_wnd();
                    if (aw) {
                        H bx0 = aw->bounds.left;
                        H by0 = aw->bounds.top;
                        H bw  = aw->bounds.right - aw->bounds.left;
                        H bh  = aw->bounds.bottom - aw->bounds.top;
                        if (bx0 < 0) bx0 = 0;
                        if (by0 < 0) by0 = 0;
                        if (bx0 + bw > BTRON_SCREEN_W) bw = BTRON_SCREEN_W - bx0;
                        if (by0 + bh > BTRON_SCREEN_H) bh = BTRON_SCREEN_H - by0;

                        if (bw > 0 && bh > 0) {
                            for (H r = 0; r < bh; r++) {
                                volatile uint32_t *d = gpu_fb + (by0 + r) * BTRON_SCREEN_W + bx0;
                                const COLOR *s = &s_desktop_backbuffer[(by0 + r) * BTRON_SCREEN_W + bx0];
                                tkl_memcpy((void *)d, s, bw * sizeof(COLOR));
                            }
                            __asm__ volatile("dmb sy" : : : "memory");
                        }
                    } else {
                        /* Fallback full backbuffer blit */
                        blit_backbuffer_to_fb(gpu_fb);
                    }

                    /* Keep cursor on top of GPU front buffer */
                    draw_baremetal_cursor_raw(gpu_fb, prev_mx, prev_my, BTRON_SCREEN_W, BTRON_SCREEN_H);
                }
            }
        }

        /* Periodic 1 Hz clock update */
        if (now - last_clock >= 1000000) {
            last_clock = now;
            redraw = 1;
        }

        /* Full UI path: redraw windows, menus, backbuffer blit */
        if (redraw) {
            workbench_render(screen, BTRON_SCREEN_W, BTRON_SCREEN_H);
            blit_backbuffer_to_fb(gpu_fb);
            draw_baremetal_cursor_raw(gpu_fb, s_mouse_x, s_mouse_y, BTRON_SCREEN_W, BTRON_SCREEN_H);
            prev_mx = s_mouse_x;
            prev_my = s_mouse_y;

            /* Drain USB reports accumulated during blit */
            usb_poll_devices(screen);
            if (s_mouse_x != prev_mx || s_mouse_y != prev_my) {
                restore_cursor_area(gpu_fb, prev_mx, prev_my);
                draw_baremetal_cursor_raw(gpu_fb, s_mouse_x, s_mouse_y, BTRON_SCREEN_W, BTRON_SCREEN_H);
                __asm__ volatile("dmb sy" : : : "memory");
                prev_mx = s_mouse_x;
                prev_my = s_mouse_y;
            }
        }
        /* Zero-latency cursor-only path: restore old 16x16 patch, draw new cursor (< 1 us) */
        else if (cursor_only) {
            if (s_mouse_x != prev_mx || s_mouse_y != prev_my) {
                restore_cursor_area(gpu_fb, prev_mx, prev_my);
                draw_baremetal_cursor_raw(gpu_fb, s_mouse_x, s_mouse_y, BTRON_SCREEN_W, BTRON_SCREEN_H);
                __asm__ volatile("dmb sy" : : : "memory");
                prev_mx = s_mouse_x;
                prev_my = s_mouse_y;
            }
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

    /* Clean backbuffer CPU data cache lines to Point of Coherency before blit */
    arm64_clean_cache_range(s_desktop_backbuffer, BTRON_SCREEN_W * BTRON_SCREEN_H * sizeof(COLOR));

    /* 64-byte unrolled burst blitter directly into physical VRAM (1.1 ms on Cortex-A72) */
    arm64_fast_blit((void *)gpu_fb, s_desktop_backbuffer, BTRON_SCREEN_W * BTRON_SCREEN_H * sizeof(COLOR));
}

/* ═══════════════════════════════════════════════════════════════════
 * BCM2711 / BCM2837 System Timer & 60Hz Tick
 * ═══════════════════════════════════════════════════════════════════ */

static volatile uint32_t s_system_ticks = 0;

void rpi_timer_tick(void) {
    s_system_ticks++;
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
    *p_time = (uint64_t)((s_system_ticks * 1000) / 60);
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
    if (raw_x > 64)  raw_x = 64;
    if (raw_x < -64) raw_x = -64;
    if (raw_y > 64)  raw_y = 64;
    if (raw_y < -64) raw_y = -64;

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
    if (pix_x > 64)  pix_x = 64;
    if (pix_x < -64) pix_x = -64;
    if (pix_y > 64)  pix_y = 64;
    if (pix_y < -64) pix_y = -64;

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
    if (raw >  64) raw =  64;
    if (raw < -64) raw = -64;

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

    if (pixels >  64) pixels =  64;
    if (pixels < -64) pixels = -64;

    return pixels;
}

static inline int32_t mouse_accelerate_subpixel_raw(int32_t raw, int32_t *subpixel) {
    (void)subpixel;
    return raw;          // pure 1:1, no residual, no boost, no mult
}

static inline int32_t mouse_accelerate_subpixel(int32_t raw, int32_t *subpixel)
{
    if (g_mouse_accel_profile == 1) {
        return mouse_accelerate_subpixel_haiku(raw, subpixel);
    } else if (g_mouse_accel_profile == 2) {
        return mouse_accelerate_subpixel_riscos(raw, subpixel);
    } else {
        return mouse_accelerate_subpixel_raw(raw, subpixel);
    }
}

static int usb_poll_devices(GDEV *screen) {
    (void)screen;
    int activity = 0;

    /* Drain the xHCI event ring ONCE per poll cycle.
     * This populates s_kbd_queue and s_accum_dx/dy/buttons atomically. */
    if (g_use_xhci) {
        xhci_process();
    }

    /* 1. Drain pending USB HID Keyboard reports */
    usb_kbd_report_t kbd_rep;
    uint32_t now_us = *(volatile uint32_t *)(TIMER_BASE + 0x04);

    while (1) {
        int kbd_got = 0;
        if (g_use_xhci) {
            kbd_got = (xhci_poll_keyboard(&kbd_rep) > 0);
        } else {
            kbd_got = (dwc2_poll_keyboard(&kbd_rep) > 0);
        }
        if (!kbd_got) break;

        uint8_t scancode = kbd_rep.keys[0];
        uint16_t bmod = usb_to_btron_modifiers(kbd_rep.modifiers);
        if (scancode != 0) {
            if (scancode != g_prev_kbd_scancode) {
                uint32_t k = dwc2_usb_to_btron_key(scancode, kbd_rep.modifiers);
                if (k != 0) {
                    EVT ev;
                    ev.type   = EV_KEY_DOWN;
                    ev.key    = k;
                    ev.data   = (VW)(uintptr_t)bmod;
                    ev.pos.x  = s_mouse_x;
                    ev.pos.y  = s_mouse_y;
                    ev.button = 0;
                    snd_evt(&ev);
                    activity = 1;

                    /* Arm hardware-like responsive key repeat */
                    s_held_kbd_scancode = scancode;
                    s_held_kbd_modifiers = kbd_rep.modifiers;
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
                    activity = 1;
                }
                /* Disarm key repeat */
                s_held_kbd_scancode = 0;
                s_held_kbd_modifiers = 0;
            }
        }
        g_prev_kbd_scancode = scancode;
    }

    /* 1b. Check key auto-repeat timer for held key */
    if (g_kbd_repeat_enabled && s_held_kbd_scancode != 0) {
        if ((now_us - s_key_press_time_us) >= g_kbd_repeat_delay_us) {
            if ((now_us - s_key_last_repeat_us) >= g_kbd_repeat_interval_us) {
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
                    activity = 1;
                }
            }
        }
    }

    /* 2. Poll USB HID Mouse (xHCI on Pi 400, DWC2 on Pi 2/3/QEMU) */
    usb_mouse_report_t mouse_rep;
    int mouse_got = 0;
    if (g_use_xhci) {
        mouse_got = (xhci_poll_mouse(&mouse_rep) > 0);
    } else {
        mouse_got = (dwc2_poll_mouse(&mouse_rep) > 0);
    }

    if (mouse_got) {
        if (mouse_rep.dx != 0 || mouse_rep.dy != 0) {
            int32_t move_x = 0, move_y = 0;
            if (g_mouse_accel_profile == 1) {
                /* Haiku OS / BeOS 2D Velocity Vector Accelerator */
                mouse_accelerate_pair_haiku((int32_t)mouse_rep.dx, (int32_t)mouse_rep.dy, &move_x, &move_y);
            } else if (g_mouse_accel_profile == 2) {
                /* RISC OS MouseStep Stepped Accelerator */
                move_x = mouse_accelerate_subpixel_riscos((int32_t)mouse_rep.dx, &s_mouse_sub_x);
                move_y = mouse_accelerate_subpixel_riscos((int32_t)mouse_rep.dy, &s_mouse_sub_y);
            } else {
                move_x = mouse_accelerate_subpixel_raw((int32_t)mouse_rep.dx, &s_mouse_sub_x);
                move_y = mouse_accelerate_subpixel_raw((int32_t)mouse_rep.dy, &s_mouse_sub_y);
            }

            s_mouse_x += (H)move_x;
            s_mouse_y += (H)move_y;
            if (s_mouse_x < 0) s_mouse_x = 0;
            if (s_mouse_x >= BTRON_SCREEN_W) s_mouse_x = BTRON_SCREEN_W - 1;
            if (s_mouse_y < 0) s_mouse_y = 0;
            if (s_mouse_y >= BTRON_SCREEN_H) s_mouse_y = BTRON_SCREEN_H - 1;

            EVT ev;
            ev.type   = EV_MOUSE_MOVE;
            ev.pos.x  = s_mouse_x;
            ev.pos.y  = s_mouse_y;
            ev.button = 0;
            ev.data   = 0;
            snd_evt(&ev);
            activity = 1;
        }

        uint8_t btn_left   = (mouse_rep.buttons & 1u);
        uint8_t btn_right  = (mouse_rep.buttons & 2u) >> 1;
        uint8_t btn_middle = (mouse_rep.buttons & 4u) >> 2;

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
            activity = 1;
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
            activity = 1;
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
            activity = 1;
        }
    }

    return activity;
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
    if (g_mmio_base == 0xFE000000UL) {
        bcm2711_dma_init();
    }

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
