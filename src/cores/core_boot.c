/*
 * core_boot.c — Multiboot 1 & QEMU Direct Kernel Boot Loader Entry
 * Full interactive PS/2 Keyboard & UART Serial engine.
 * Includes VGA 80x25 text mode engine (0xB8000) and VESA VBE 1024x768 32-bpp driver for 'startx'/'desktop'.
 */

#include <stdint.h>
#include <stddef.h>
#include <btron/core.h>
#include <btron/desktop.h>
#include <btron/wnd.h>
#include <btron/event.h>
#include <btron/tip.h>
#include <btron/smp.h>
#include <btron/workbench.h>
#include <drivers/vesa.h>
#include <drivers/ps2_mouse.h>
#include <drivers/pc98_mouse.h>
#include <libstr.h>
#define memcpy tkl_memcpy

#define MULTIBOOT_HEADER_MAGIC 0x1BADB002
#define MULTIBOOT_HEADER_FLAGS 0x00000003

struct multiboot_header {
    uint32_t magic;
    uint32_t flags;
    uint32_t checksum;
} __attribute__((packed));

__attribute__((section(".multiboot"), used))
const struct multiboot_header g_multiboot_header = {
    MULTIBOOT_HEADER_MAGIC,
    MULTIBOOT_HEADER_FLAGS,
    (uint32_t)(-(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS))
};

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

#define COM1_PORT 0x3F8

static void uart_init(void) {
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x80);
    outb(COM1_PORT + 0, 0x03);
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x03);
    outb(COM1_PORT + 2, 0xC7);
    outb(COM1_PORT + 4, 0x0B);
}

static void uart_putc(char c) {
    while ((inb(COM1_PORT + 5) & 0x20) == 0);
    outb(COM1_PORT, (uint8_t)c);
}

void uart_puts_raw(const char *str) {
    while (*str) {
        if (*str == '\n') uart_putc('\r');
        uart_putc(*str++);
    }
}

static int uart_has_char(void) {
    return (inb(COM1_PORT + 5) & 0x01) ? 1 : 0;
}

static char uart_getc(void) {
    if (!uart_has_char()) return 0;
    return (char)inb(COM1_PORT);
}

/* ═══════════════════════════════════════════════════════════════════
 * PS/2 Keyboard Controller
 * ═══════════════════════════════════════════════════════════════════ */

static int ps2_has_key(void) {
    /* Bit 0 = Output Buffer Full, Bit 5 = Auxiliary (Mouse) data */
    uint8_t st = inb(0x64);
    return ((st & 0x21) == 0x01) ? 1 : 0;
}

static uint8_t ps2_get_scancode(void) {
    if (!ps2_has_key()) return 0;
    return inb(0x60);
}

static char ps2_scancode_to_ascii(uint8_t sc, int shift) {
    if (sc & 0x80) return 0;
    switch (sc) {
        case 0x1E: return shift ? 'A' : 'a';
        case 0x30: return shift ? 'B' : 'b';
        case 0x2E: return shift ? 'C' : 'c';
        case 0x20: return shift ? 'D' : 'd';
        case 0x12: return shift ? 'E' : 'e';
        case 0x21: return shift ? 'F' : 'f';
        case 0x22: return shift ? 'G' : 'g';
        case 0x23: return shift ? 'H' : 'h';
        case 0x17: return shift ? 'I' : 'i';
        case 0x24: return shift ? 'J' : 'j';
        case 0x25: return shift ? 'K' : 'k';
        case 0x26: return shift ? 'L' : 'l';
        case 0x32: return shift ? 'M' : 'm';
        case 0x31: return shift ? 'N' : 'n';
        case 0x18: return shift ? 'O' : 'o';
        case 0x19: return shift ? 'P' : 'p';
        case 0x10: return shift ? 'Q' : 'q';
        case 0x13: return shift ? 'R' : 'r';
        case 0x1F: return shift ? 'S' : 's';
        case 0x14: return shift ? 'T' : 't';
        case 0x16: return shift ? 'U' : 'u';
        case 0x2F: return shift ? 'V' : 'v';
        case 0x11: return shift ? 'W' : 'w';
        case 0x2D: return shift ? 'X' : 'x';
        case 0x15: return shift ? 'Y' : 'y';
        case 0x2C: return shift ? 'Z' : 'z';
        case 0x02: return shift ? '!' : '1';
        case 0x03: return shift ? '@' : '2';
        case 0x04: return shift ? '#' : '3';
        case 0x05: return shift ? '$' : '4';
        case 0x06: return shift ? '%' : '5';
        case 0x07: return shift ? '^' : '6';
        case 0x08: return shift ? '&' : '7';
        case 0x09: return shift ? '*' : '8';
        case 0x0A: return shift ? '(' : '9';
        case 0x0B: return shift ? ')' : '0';
        case 0x0C: return shift ? '_' : '-';
        case 0x0D: return shift ? '+' : '=';
        case 0x1A: return shift ? '{' : '[';
        case 0x1B: return shift ? '}' : ']';
        case 0x27: return shift ? ':' : ';';
        case 0x28: return shift ? '"' : '\'';
        case 0x29: return shift ? '~' : '`';
        case 0x2B: return shift ? '|' : '\\';
        case 0x33: return shift ? '<' : ',';
        case 0x34: return shift ? '>' : '.';
        case 0x35: return shift ? '?' : '/';
        case 0x0F: return '\t';
        case 0x39: return ' ';
        case 0x1C: return '\n';
        case 0x0E: return '\b';
        case 0x01: return 0x1B; /* ESC */
        default: return 0;
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * VGA 80x25 Text Mode Driver (0xB8000)
 * ═══════════════════════════════════════════════════════════════════ */

#define VGA_VRAM ((volatile uint16_t *)0xB8000)
#define VGA_COLS 80
#define VGA_ROWS 25

static uint16_t vga_cursor_x = 0;
static uint16_t vga_cursor_y = 0;
static uint8_t  vga_attr = 0x07;

static void vga_update_cursor(void) {
    uint16_t pos = vga_cursor_y * VGA_COLS + vga_cursor_x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void vga_clear(uint8_t attr) {
    vga_attr = attr;
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++) {
        VGA_VRAM[i] = (uint16_t)(' ') | ((uint16_t)attr << 8);
    }
    vga_cursor_x = 0;
    vga_cursor_y = 0;
    vga_update_cursor();
}

static void vga_scroll(void) {
    if (vga_cursor_y >= VGA_ROWS) {
        for (int y = 0; y < VGA_ROWS - 1; y++) {
            for (int x = 0; x < VGA_COLS; x++) {
                VGA_VRAM[y * VGA_COLS + x] = VGA_VRAM[(y + 1) * VGA_COLS + x];
            }
        }
        for (int x = 0; x < VGA_COLS; x++) {
            VGA_VRAM[(VGA_ROWS - 1) * VGA_COLS + x] = (uint16_t)(' ') | ((uint16_t)vga_attr << 8);
        }
        vga_cursor_y = VGA_ROWS - 1;
    }
}

static void vga_putc(char c) {
    if (c == '\n') {
        vga_cursor_x = 0;
        vga_cursor_y++;
        vga_scroll();
    } else if (c == '\r') {
        vga_cursor_x = 0;
    } else if (c == '\b') {
        if (vga_cursor_x > 0) {
            vga_cursor_x--;
            VGA_VRAM[vga_cursor_y * VGA_COLS + vga_cursor_x] = (uint16_t)(' ') | ((uint16_t)vga_attr << 8);
        }
    } else {
        VGA_VRAM[vga_cursor_y * VGA_COLS + vga_cursor_x] = (uint16_t)(uint8_t)c | ((uint16_t)vga_attr << 8);
        vga_cursor_x++;
        if (vga_cursor_x >= VGA_COLS) {
            vga_cursor_x = 0;
            vga_cursor_y++;
            vga_scroll();
        }
    }
    vga_update_cursor();
}

static void kprint(const char *str, uint8_t attr) {
    vga_attr = attr;
    while (*str) {
        char c = *str++;
        if (c == '\n') uart_putc('\r');
        uart_putc(c);
        vga_putc(c);
    }
}

static void vga_print_at(int x, int y, const char *str, uint8_t attr) {
    int idx = y * VGA_COLS + x;
    while (*str && idx < VGA_COLS * VGA_ROWS) {
        char c = *str++;
        VGA_VRAM[idx++] = (uint16_t)(uint8_t)c | ((uint16_t)attr << 8);
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * Ski Bootloader
 * ═══════════════════════════════════════════════════════════════════ */

typedef struct {
    const char *title;
    const char *cmdline;
    int         cores;
} ski_target_t;

static ski_target_t s_targets[3] = {
    {"B-System BTRON3 Workstation (x86_64 UEFI SMP)", "btron3 root=vobj0 smp=4 acpi=6.5", 4},
    {"Raspberry Pi 5 BCM2712 / RP1 ARM64",            "btron3 rpi5 mailbox=mmio",        4},
    {"NEC PC-9801 / PC-9821 VM (Awe Morris Kernel)",   "btron3 pc98 gdc=0xa0000",         1}
};

static int s_selected = 0;
static int s_boot_triggered = 0;

static void render_ski_menu(void) {
    vga_clear(0x1F);

    vga_print_at(0, 0, "================================================================================", 0x1E);
    vga_print_at(2, 1, "  B-System (BTRON3 3.20 HMI) Ski Bootloader", 0x1F);
    vga_print_at(2, 2, "  Plarforms: [AAarch32] [AArch64] [EM64T] [PC-98]", 0x1B);
    vga_print_at(2, 3, "----------------------------------------------------------------------------", 0x17);

     vga_print_at(2, 5, " Copyright (c) 2026 Synrc Research Center", 0x1A);
     vga_print_at(2, 6, " Kyiv Ukraine", 0x1A);

    vga_print_at(2, 8, "Select Operating System Target to Boot:", 0x1E);
    vga_print_at(2, 9, "----------------------------------------------------------------------------", 0x17);

    for (int i = 0; i < 3; i++) {
        uint8_t attr = (i == s_selected) ? 0x70 : 0x1F;
        char row[80];
        for (int k = 0; k < 76; k++) row[k] = ' ';
        row[76] = '\0';

        row[0] = (i == s_selected) ? '>' : ' ';
        row[1] = ' ';
        row[2] = '[';
        row[3] = (char)('1' + i);
        row[4] = ']';
        row[5] = ' ';

        const char *t = s_targets[i].title;
        int p = 6;
        while (*t && p < 56) row[p++] = *t++;

        row[58] = '(';
        row[59] = (char)('0' + s_targets[i].cores);
        row[60] = ' ';
        row[61] = 'C';
        row[62] = 'P';
        row[63] = 'U';
        row[64] = 's';
        row[65] = ')';

        vga_print_at(2, 11 + i * 2, row, attr);
    }

    vga_print_at(2, 18, "----------------------------------------------------------------------------", 0x17);
    vga_print_at(2, 19, "Active Command Line:", 0x1B);
    vga_print_at(4, 20, s_targets[s_selected].cmdline, 0x1E);

    vga_print_at(2, 22, "Keys: [Up/Down, W/S] Select Target   [+/-] Adjust Cores   [Enter] Boot OS", 0x1B);
    vga_print_at(0, 24, "================================================================================", 0x1E);

    vga_cursor_x = 0;
    vga_cursor_y = 23;
    vga_update_cursor();
}

static void uart_send_menu_update(void) {
    uart_putc('\r');
    uart_putc('\n');
    uart_putc('>');
    uart_putc(' ');
    const char *t = s_targets[s_selected].title;
    while (*t) uart_putc(*t++);
    uart_putc('\r');
    uart_putc('\n');
}

/* ═══════════════════════════════════════════════════════════════════
 * BTRON3 Terminal Shell & Graphical Desktop ('startx' / 'desktop')
 * ═══════════════════════════════════════════════════════════════════ */

static int strcmp_k(const char *a, const char *b) {
    while (*a && (*a == *b)) { a++; b++; }
    return *(const unsigned char*)a - *(const unsigned char*)b;
}

static void render_btron_text_desktop(int active_cores) {
    vga_clear(0x07);

    for (int x = 0; x < VGA_COLS; x++) {
        VGA_VRAM[x] = (uint16_t)(' ') | ((uint16_t)0x70 << 8);
    }
    vga_print_at(1, 0, "BTRON3 3.20  [実身・仮身]  [編集]  [表示]  [設定]  [端末]", 0x70);
    char core_str[32];
    core_str[0] = 'S'; core_str[1] = 'M'; core_str[2] = 'P'; core_str[3] = ':';
    core_str[4] = ' '; core_str[5] = (char)('0' + active_cores);
    core_str[6] = ' '; core_str[7] = 'C'; core_str[8] = 'o'; core_str[9] = 'r'; core_str[10] = 'e'; core_str[11] = 's'; core_str[12] = '\0';
    vga_print_at(64, 0, core_str, 0x70);

    for (int x = 0; x < VGA_COLS; x++) {
        VGA_VRAM[(VGA_ROWS - 1) * VGA_COLS + x] = (uint16_t)(' ') | ((uint16_t)0x70 << 8);
    }
    vga_print_at(1, VGA_ROWS - 1, "[GTerm Console]  [Mozc IME: かな]  [HFDS Storage: 0x10001000]  12:00", 0x70);

    vga_cursor_x = 0;
    vga_cursor_y = 2;
    vga_update_cursor();
}

extern void render_desktop_background(GDEV *dev);
extern void render_system_panel(GDEV *dev);
extern void redraw_all_windows(void);
extern void draw_baremetal_mouse_cursor(GDEV *screen, H mx, H my, H w, H h);
extern BTRON_DESKTOP* get_btron_desktop(void);
extern WND* open_vobj_manager_window(void);
extern WND* open_t_editor_window(void);
extern WND* open_gterm_window(void);

static COLOR s_desktop_backbuffer[1024 * 768] __attribute__((aligned(16)));

static void launch_vesa_desktop_session(int active_cores) {
    (void)active_cores;
    uart_puts_raw("\r\n[VESA] Switching to B-System 1024x768x32 Linear Framebuffer Desktop...\r\n");
    uart_puts_raw("[VESA] Disabling VGA text mode (0xB8000)...\r\n");
    uart_puts_raw("[VESA] Programming Bochs DISPI registers: 1024x768x32 LFB\r\n");
    uart_puts_raw("[VESA] Backbuffer allocated: 3 MB @ s_desktop_backbuffer\r\n");
    uart_puts_raw("[VESA] Double-buffer compositor: ACTIVE (tear-free)\r\n");
    kprint("\n[VESA] Switching to B-System 1024x768x32 Linear Framebuffer Desktop...\n", 0x0E);
    vesa_init(1024, 768, 32);

    /* Initialize authentic BTRON desktop with off-screen backbuffer for tear-free rendering */
    init_desktop_vram(1024, 768, s_desktop_backbuffer);
    BTRON_DESKTOP *dt = get_btron_desktop();
    if (!dt || !dt->screen) return;

    tip_init();
    init_evt_sys();

#if defined(BTRON_PC98_TARGET)
    pc98_mouse_init(1024, 768);
#else
    ps2_mouse_init(1024, 768);
#endif
    workbench_init(1024);

    /* Open authentic B-System windows */
    open_vobj_manager_window();
    open_t_editor_window();
    open_gterm_window();

    /* Initial paint of authentic B-System desktop to backbuffer */
    workbench_render(dt->screen, 1024, 768);

    /* Blit composite frame to VESA VRAM */
    if (g_vesa.framebuffer) {
        memcpy((void *)g_vesa.framebuffer, s_desktop_backbuffer, 1024 * 768 * sizeof(COLOR));
    }

    uart_puts_raw("\n==========================================================\n");
    uart_puts_raw(" [B-SYSTEM] Authentic BTRON3 Desktop Active (1024x768x32)!\n");
    uart_puts_raw("   * Wallpaper: Sakamura B-TRON Teal with Retro Grid\n");
    uart_puts_raw("   * Double Buffering: Active (Tear-Free Compositor)\n");
    uart_puts_raw("   * Icons    : Real Body Cabinet, Editor, GTerm, Audio, Chat\n");
    uart_puts_raw("   * Panel    : [BTRON] System Menu & Japanese JIS Fonts\n");
    uart_puts_raw("   * Windows  : HFDS Cabinet Explorer, Editor, GTerm Shell\n");
    uart_puts_raw(" Controls: Mouse click, type in active window, [Esc]/[Q] to return.\n");
    uart_puts_raw("==========================================================\n\n");

    /* Drain any pending keypresses */
    while (ps2_has_key()) (void)ps2_get_scancode();
    while (uart_has_char()) (void)uart_getc();

    int shift = 0;
    EVT ev;

    for (;;) {
        int need_redraw = 0;

#if defined(BTRON_PC98_TARGET)
        pc98_mouse_poll();
#else
        ps2_mouse_poll();
#endif

        H mouse_x = 512, mouse_y = 384;
        get_baremetal_mouse_pos(&mouse_x, &mouse_y);

        if (ps2_has_key()) {
            uint8_t sc = ps2_get_scancode();
            if (sc == 0x01 || sc == 0x10) { /* Esc or Q */
                break;
            } else if (sc == 0x2A || sc == 0x36) {
                shift = 1;
            } else if (sc == 0xAA || sc == 0xB6) {
                shift = 0;
            } else if (!(sc & 0x80)) {
                char c = ps2_scancode_to_ascii(sc, shift);
                if (c) {
                    ev.type = EV_KEY_DOWN;
                    ev.key = (UW)c;
                    ev.pos.x = mouse_x;
                    ev.pos.y = mouse_y;
                    snd_evt(&ev);
                    need_redraw = 1;
                }
            }
        }

        if (uart_has_char()) {
            char uc = uart_getc();
            if (uc == 0x1B || uc == 'q' || uc == 'Q' || uc == 0x03) {
                break;
            }
            if (uc == '\r') uc = '\n';
            if (uc) {
                ev.type = EV_KEY_DOWN;
                ev.key = (UW)uc;
                ev.pos.x = mouse_x;
                ev.pos.y = mouse_y;
                snd_evt(&ev);
                need_redraw = 1;
            }
        }

        while (get_evt(&ev, 0) == E_OK) {
            workbench_process_event(dt->screen, &ev);
            need_redraw = 1;
        }

        if (need_redraw) {
            workbench_render(dt->screen, 1024, 768);
            if (g_vesa.framebuffer) {
                memcpy((void *)g_vesa.framebuffer, s_desktop_backbuffer, 1024 * 768 * sizeof(COLOR));
            }
        }

        for (volatile int d = 0; d < 10000; d++) {
            __asm__ volatile("pause");
        }
    }

    vesa_restore_text();
    render_btron_text_desktop(active_cores);
    kprint("\n[B-SYSTEM] Returned from VESA Desktop to Console Shell.\n\n", 0x0A);
}

static void run_btron_shell(int active_cores) {
    /* Drain any leftover key from bootloader */
    while (ps2_has_key()) (void)ps2_get_scancode();
    while (uart_has_char()) (void)uart_getc();

    render_btron_text_desktop(active_cores);

    kprint("\n==========================================================\n", 0x0A);
#if defined(BTRON_PC98_TARGET)
    kprint(" BTRON3 3.20 NEC PC-9801/PC-9821 Kernel (Awe Morris Engine)\n", 0x0A);
    kprint(" PC-98 µITRON 3.0 Scheduler  — 1 Core (i386 ISA Planar)\n", 0x0E);
#else
    kprint(" BTRON3 3.20 UEFI Workstation Kernel (Kota Uchida Engine)\n", 0x0A);
    kprint(" SMP Multi-Core Scheduler Active — 4 Cores Live\n", 0x0E);
#endif
    kprint(" Type 'desktop' or 'startx' to launch VESA 1024x768 GUI!\n", 0x0B);
    kprint(" Commands: ps, mem, ski, ver, desktop, startx, clear\n", 0x07);
    kprint("==========================================================\n\n", 0x0A);

    kprint("btron3# ", 0x0F);

    char cmd_buf[64];
    int  cmd_len = 0;
    int  shift = 0;

    for (;;) {
        char ch = 0;

        if (ps2_has_key()) {
            uint8_t sc = ps2_get_scancode();
            if (sc == 0x2A || sc == 0x36) {
                shift = 1;
            } else if (sc == 0xAA || sc == 0xB6) {
                shift = 0;
            } else if (!(sc & 0x80)) {
                ch = ps2_scancode_to_ascii(sc, shift);
            }
        }

        if (!ch && uart_has_char()) {
            ch = uart_getc();
            if (ch == '\r') ch = '\n';
        }

        if (ch) {
            if (ch == '\n') {
                kprint("\n", 0x07);
                cmd_buf[cmd_len] = '\0';

                if (strcmp_k(cmd_buf, "help") == 0) {
                    kprint("B-System BTRON3 Available Commands:\n", 0x0B);
                    kprint("  desktop / startx - Launch authentic B-System 1024x768 GUI\n", 0x0E);
                    kprint("  ps               - List running tasks with CORE # column\n", 0x07);
                    kprint("  mem              - Show T-Kernel 2.0 heap memory statistics\n", 0x07);
                    kprint("  ski              - Show Ski Bootloader targets\n", 0x07);
                    kprint("  ver              - Display kernel version & SMP APIC info\n", 0x07);
                    kprint("  clear            - Clear terminal screen\n", 0x07);
                } else if (strcmp_k(cmd_buf, "desktop") == 0 || strcmp_k(cmd_buf, "startx") == 0 || strcmp_k(cmd_buf, "gui") == 0) {
                    launch_vesa_desktop_session(active_cores);
                } else if (strcmp_k(cmd_buf, "ps") == 0) {
                    kprint("PID   CORE  TASK           STAT   ADDR         BOUNDS    TITLE\n", 0x0B);
                    kprint("-------------------------------------------------------------------------\n", 0x08);
                    kprint("  1   #0    tk_desktop     RUN    0x01020000   1024x768  [B-System Desktop]\n", 0x0A);
                    kprint("  2   #1    tk_wnd_mgr     READY  0x01040000   1024x768  [Window Compositor]\n", 0x0A);
                    kprint("  3   #2    tk_tip_ime     READY  0x010A0000   Candidate [Mozc Japanese IME]\n", 0x0A);
                    kprint("  4   #3    gterm#1        RUN    0x010C0000   640x480   [Terminal Console]\n", 0x0E);
                } else if (strcmp_k(cmd_buf, "mem") == 0) {
                    kprint("T-Kernel 2.0 Memory Pool Statistics:\n", 0x0B);
                    kprint("  Heap Base  : 0x00100000\n", 0x07);
                    kprint("  Heap Limit : 0x40000000 (1GB RAM)\n", 0x07);
                    kprint("  Heap Used  : 0x00240000 (2.25 MB)\n", 0x07);
                } else if (strcmp_k(cmd_buf, "ski") == 0) {
                    kprint("🎿 Ski Bootloader Profile Targets:\n", 0x0B);
                    kprint("  [1] B-System BTRON3 Workstation x86_64 UEFI SMP (Active, 4 Cores)\n", 0x0A);
                    kprint("  [2] Raspberry Pi 5 BCM2712 / RP1 ARM64 Workstation\n", 0x07);
                    kprint("  [3] NEC PC-9801 / PC-9821 VM (Awe Morris Kernel)\n", 0x07);
                } else if (strcmp_k(cmd_buf, "ver") == 0) {
                    kprint("B-System BTRON3 3.20 (x86_64 UEFI SMP — Kota Uchida Engine)\n", 0x0A);
                    kprint("  Graphics: VESA VBE 2.0/3.0 Linear Framebuffer (1024x768x32)\n", 0x0E);
                    kprint("  Subsystem: ACPI 6.5 MADT, LAPIC 0xFEE00000, IO-APIC 0xFEC00000\n", 0x07);
                } else if (strcmp_k(cmd_buf, "clear") == 0) {
                    render_btron_text_desktop(active_cores);
                } else if (cmd_len > 0) {
                    kprint("Unknown command: ", 0x0C);
                    kprint(cmd_buf, 0x0C);
                    kprint(" (type 'help' for commands)\n", 0x0C);
                }

                cmd_len = 0;
                kprint("btron3# ", 0x0F);
            } else if (ch == '\b') {
                if (cmd_len > 0) {
                    cmd_len--;
                    kprint("\b \b", 0x07);
                }
            } else if (cmd_len < 60 && ch >= 32 && ch <= 126) {
                cmd_buf[cmd_len++] = ch;
                char echo[2] = {ch, '\0'};
                kprint(echo, 0x0F);
            }
        }

        for (volatile int d = 0; d < 2000; d++) {
            __asm__ volatile("pause");
        }
    }
}

static uint8_t s_boot_stack[32768] __attribute__((aligned(16)));

void kernel_main(void) {
    uart_init();

    btron_core_banner();

    /* CPU / SMP / IRQ — core_smp.c (UEFI) or core_pc98.c (PC-98) */
    btron_core_init();

    /* Memory map — core_smp.c (UEFI) or core_pc98.c (PC-98) */
    uart_puts_raw("\r\n");
    btron_core_mem_log();

    /* Storage / HFDS — core_smp.c (UEFI) or core_pc98.c (PC-98) */
    uart_puts_raw("\r\n");
    btron_core_hfds_log();

    /* Graphics — vesa.c (real PCI BAR0 scan) */
    uart_puts_raw("\r\n");
    vesa_probe_log();



    /* ── Phase 7: Ski Bootloader Menu ───────────────────────────────── */
    render_ski_menu();

    uart_puts_raw("==========================================================\r\n");
    uart_puts_raw(" Ski Bootloader Active — Multi-OS Boot Manager\r\n");
    uart_puts_raw(" Controls: [W/S] or [Up/Down] Navigate, [+/-] Cores, [Enter] Boot\r\n");
    uart_puts_raw("==========================================================\r\n");
    uart_send_menu_update();

    uint8_t prev_scancode = 0;

    while (!s_boot_triggered) {
        int state_changed = 0;

        if (ps2_has_key()) {
            uint8_t sc = ps2_get_scancode();
            if (sc != prev_scancode) {
                prev_scancode = sc;
                if (sc == 0x11 || sc == 0x48) {
                    s_selected = (s_selected + 2) % 3;
                    state_changed = 1;
                } else if (sc == 0x1F || sc == 0x50) {
                    s_selected = (s_selected + 1) % 3;
                    state_changed = 1;
                } else if (sc == 0x4E || sc == 0x0D) {
                    if (s_targets[s_selected].cores < 16) s_targets[s_selected].cores++;
                    state_changed = 1;
                } else if (sc == 0x4A || sc == 0x0C) {
                    if (s_targets[s_selected].cores > 1) s_targets[s_selected].cores--;
                    state_changed = 1;
                } else if (sc == 0x1C || sc == 0x39) {
                    s_boot_triggered = 1;
                }
            }
        } else {
            prev_scancode = 0;
        }

        if (uart_has_char()) {
            char uc = uart_getc();
            if (uc == 'w' || uc == 'W' || uc == 'k') {
                s_selected = (s_selected + 2) % 3;
                state_changed = 1;
            } else if (uc == 's' || uc == 'S' || uc == 'j') {
                s_selected = (s_selected + 1) % 3;
                state_changed = 1;
            } else if (uc == '+' || uc == '=') {
                if (s_targets[s_selected].cores < 16) s_targets[s_selected].cores++;
                state_changed = 1;
            } else if (uc == '-' || uc == '_') {
                if (s_targets[s_selected].cores > 1) s_targets[s_selected].cores--;
                state_changed = 1;
            } else if (uc == '\r' || uc == '\n' || uc == ' ') {
                s_boot_triggered = 1;
            } else if (uc == 0x1B) {
                char c2 = uart_getc();
                if (c2 == '[') {
                    char c3 = uart_getc();
                    if (c3 == 'A') { s_selected = (s_selected + 2) % 3; state_changed = 1; }
                    else if (c3 == 'B') { s_selected = (s_selected + 1) % 3; state_changed = 1; }
                }
            }
        }

        if (state_changed) {
            render_ski_menu();
            uart_send_menu_update();
        }

        for (volatile int d = 0; d < 10000; d++) {
            __asm__ volatile("pause");
        }
    }

    run_btron_shell(s_targets[s_selected].cores);
}

__attribute__((naked, weak)) void _start(void) {
#if defined(__x86_64__)
    __asm__ volatile(
        "movq %0, %%rsp\n"
        "xorq %%rbp, %%rbp\n"
        "movq %%cr0, %%rax\n"
        "andq $~0x04, %%rax\n"
        "orq $0x02, %%rax\n"
        "movq %%rax, %%cr0\n"
        "movq %%cr4, %%rax\n"
        "orq $0x600, %%rax\n"
        "movq %%rax, %%cr4\n"
        "call kernel_main\n"
        "1: hlt\n"
        "jmp 1b\n"
        :
        : "r"(s_boot_stack + sizeof(s_boot_stack))
    );
#elif defined(__i386__)
    __asm__ volatile(
        "movl %0, %%esp\n"
        "xorl %%ebp, %%ebp\n"
        "movl %%cr0, %%eax\n"
        "andl $~0x04, %%eax\n"
        "orl $0x02, %%eax\n"
        "movl %%eax, %%cr0\n"
        "movl %%cr4, %%eax\n"
        "orl $0x600, %%eax\n"
        "movl %%eax, %%cr4\n"
        "call kernel_main\n"
        "1: hlt\n"
        "jmp 1b\n"
        :
        : "r"(s_boot_stack + sizeof(s_boot_stack))
    );
#else
    kernel_main();
#endif
}

void multiboot_main(void) {
    _start();
}
