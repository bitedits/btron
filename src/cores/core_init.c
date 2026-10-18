/*
 * Unified Kernel Initializer for B-System
 * Dispatches polymorphically to active kernel core (btron_core_init & btron_core_print_ver)
 */

#include <btron/itron.h>
#include <btron/apps.h>
#include <device/virtio.h>
#include <libstr.h>
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include <stdio.h>
#endif

__attribute__((weak)) volatile uint32_t g_num_cpus = 1;

void btron_kernel_init(int target_mode) {
    (void)target_mode;
    btron_core_banner();
    btron_core_init();
    btron_core_mem_log();
    btron_core_hfds_log();
}

void btron_unified_init(void) {
    btron_core_banner();
    btron_core_init();
}

/* ═══════════════════════════════════════════════════════════════════
 * Kernel Info & Hardware Query Interfaces
 * ═══════════════════════════════════════════════════════════════════ */

#if (defined(__arm__) || defined(__aarch64__) || defined(__m68k__) || defined(BTRON_PS2_TARGET) || defined(__mips__)) && (!defined(__STDC_HOSTED__) || __STDC_HOSTED__ == 0)
extern uint32_t heap_ptr;
#define K_HEAP_BASE  0x00010000
#define K_HEAP_LIMIT 0x08000000
extern void get_baremetal_mouse_pos(H *x, H *y);
extern void set_baremetal_mouse_pos(H x, H y);
extern void handle_baremetal_mouse_click(GDEV *screen, H x, H y, BOOL press);
#endif

void sys_get_devconf(char *buf, size_t bufsz) {
    if (!buf || bufsz == 0) return;
#if defined(BTRON_PS2_TARGET)
    tkl_strncpy(buf,
        "Registered Device Drivers:\n"
        "  [0] ScreenDrv : GS 4MB eDRAM 640x448 @ 32-bpp RGBA (Active, OK)\n"
        "  [1] SerialDrv : EE SIO0 UART 115200 8N1 (Active, Console)\n"
        "  [2] KBPD      : DualShock 2 & USB OHCI Keyboard/Mouse (Active)\n"
        "  [3] TKernel   : Cleanroom uITRON 3.0 / BTRON 3.20 RTOS (Active)\n"
        "  [4] VObjStore : HyperData HFDS Real Body Storage (Active)", bufsz - 1);
    buf[bufsz - 1] = '\0';
#elif defined(__m68k__)
    tkl_strncpy(buf,
        "Registered Device Drivers:\n"
        "  [0] ScreenDrv : NuBus Slot 9 DAFB Framebuffer 800x600 (Active, OK)\n"
        "  [1] SerialDrv : Zilog Z8530 ESCC Dual UART (Active, Console)\n"
        "  [2] KBPD      : Apple Desktop Bus (ADB) Mouse & Keyboard (Active)\n"
        "  [3] TKernel   : Cleanroom uITRON 3.0 / BTRON 3.20 RTOS (Active)\n"
        "  [4] VObjStore : HyperData HFDS Real Body Storage (Active)", bufsz - 1);
    buf[bufsz - 1] = '\0';
#elif defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
    snprintf(buf, bufsz,
        "Registered Device Drivers:\n"
        "  [0] ScreenDrv : VideoCore IV GPU 1024x768 32-bpp (Active, OK)\n"
        "  [1] SerialDrv : PL011 UART0 115200 8N1 (Active, Console)\n"
        "  [2] KBPD      : Keyboard & Pointing Device (Mouse Cursor Active)\n"
        "  [3] TKernel   : 14 Sakamura T-Kernel 2.0 Subsystems (Active)\n"
        "  [4] VObjStore : HyperData HFDS Real Body Storage (Active)");
#else
    tkl_strncpy(buf,
        "Registered Device Drivers:\n"
        "  [0] ScreenDrv : VideoCore IV GPU 1024x768 32-bpp (Active, OK)\n"
        "  [1] SerialDrv : PL011 UART0 115200 8N1 (Active, Console)\n"
        "  [2] KBPD      : Keyboard & Pointing Device (Mouse Cursor Active)\n"
        "  [3] TKernel   : 14 Sakamura T-Kernel 2.0 Subsystems (Active)\n"
        "  [4] VObjStore : HyperData HFDS Real Body Storage (Active)", bufsz - 1);
    buf[bufsz - 1] = '\0';
#endif
}

void sys_get_mem_stats(uint32_t *base, uint32_t *limit, uint32_t *used) {
#if (defined(__arm__) || defined(__aarch64__) || defined(__m68k__) || defined(BTRON_PS2_TARGET) || defined(__mips__)) && (!defined(__STDC_HOSTED__) || __STDC_HOSTED__ == 0)
    if (base) *base = K_HEAP_BASE;
    if (limit) *limit = K_HEAP_LIMIT;
    if (used) *used = (heap_ptr >= K_HEAP_BASE) ? (heap_ptr - K_HEAP_BASE) : 0x00400000;
#else
    if (base) *base = 0x01000000;
    if (limit) *limit = 0x08000000;
    if (used) *used = 0x00400000;
#endif
}

void sys_mouse_get_pos(H *x, H *y) {
#if (defined(__arm__) || defined(__aarch64__) || defined(__m68k__) || defined(BTRON_PS2_TARGET) || defined(__mips__)) && (!defined(__STDC_HOSTED__) || __STDC_HOSTED__ == 0)
    get_baremetal_mouse_pos(x, y);
#else
    if (x) *x = 640;
    if (y) *y = 400;
#endif
}

void sys_mouse_set_pos(H x, H y) {
#if (defined(__arm__) || defined(__aarch64__) || defined(__m68k__) || defined(BTRON_PS2_TARGET) || defined(__mips__)) && (!defined(__STDC_HOSTED__) || __STDC_HOSTED__ == 0)
    set_baremetal_mouse_pos(x, y);
#else
    (void)x; (void)y;
#endif
}

void sys_mouse_click(H x, H y) {
#if (defined(__arm__) || defined(__aarch64__) || defined(__m68k__) || defined(BTRON_PS2_TARGET) || defined(__mips__)) && (!defined(__STDC_HOSTED__) || __STDC_HOSTED__ == 0)
    set_baremetal_mouse_pos(x, y);
    EVT ev_down;
    ev_down.type = EV_BUT_DOWN;
    ev_down.button = 1;
    ev_down.pos.x = x;
    ev_down.pos.y = y;
    ev_down.key = 0;
    ev_down.data = 0;
    snd_evt(&ev_down);

    EVT ev_up;
    ev_up.type = EV_BUT_UP;
    ev_up.button = 1;
    ev_up.pos.x = x;
    ev_up.pos.y = y;
    ev_up.key = 0;
    ev_up.data = 0;
    snd_evt(&ev_up);
#else
    (void)x; (void)y;
#endif
}

/* ═══════════════════════════════════════════════════════════════════
 * InterCore worker census (include/btron/itron.h)
 * ═══════════════════════════════════════════════════════════════════ */

/* The default answer of a core that keeps no task table it can walk.  Weak, so a core with
 * one overrides it -- the same pair core_smp.c already uses for g_num_cpus.  It returns
 * "cannot answer", not "no tasks": a shell that printed an empty table would be claiming
 * that the system has no workers, which is a different fact and a false one. */
__attribute__((weak)) W btron_list_tasks(BTRON_TSK_ROW *out, W max) {
    (void)out; (void)max;
    return BTRON_TSK_NOWALK;
}

/* Hand-rolled writers, because the file that owns them has to work on the freestanding
 * targets, where there is no printf to format a row with. */
static void tsk_put(char *out, W cap, W *n, const char *s) {
    while (*s && *n < cap - 1) out[(*n)++] = *s++;
}

static void tsk_num(char *out, W cap, W *n, uint32_t v, int hex, int width, int lpad) {
    char     buf[16];
    W        b = 0;
    do {
        uint32_t d = v % (hex ? 16u : 10u);
        buf[b++] = (char)(d < 10u ? '0' + d : 'a' + (d - 10u));
        v /= (hex ? 16u : 10u);
    } while (v && b < 16);
    while (hex && b < width) buf[b++] = '0';
    W pad = width - b;
    if (lpad) while (pad-- > 0 && *n < cap - 1) out[(*n)++] = ' ';
    while (b > 0 && *n < cap - 1) out[(*n)++] = buf[--b];
    if (!lpad) while (pad-- > 0 && *n < cap - 1) out[(*n)++] = ' ';
}

/* One `ps` row, laid out in the columns both shells have always shown:
 *   PID  TASK  STAT  ENTRY  PRI
 * `core` is not printed: no core here can say which CPU a task is on, and the old hardcoded
 * table invented one.  The entry address is printed even when there is a name, because the
 * name is the author's claim and the address is the kernel's.
 *
 * Returns 1 when it wrote a row, 0 when `idx` is past the end of the table, and
 * BTRON_TSK_NOWALK when this core cannot answer at all -- the distinction the shells print,
 * because "there are none" and "nobody knows" are different claims about the machine. */
#define BTRON_TASK_ROW_WINDOW 16
W btron_task_row_text(W idx, char *out, W cap) {
    BTRON_TSK_ROW rows[BTRON_TASK_ROW_WINDOW];
    W             n = 0, got;

    if (!out || cap < 2 || idx < 0) return 0;
    if (idx >= BTRON_TASK_ROW_WINDOW) return 0;

    got = btron_list_tasks(rows, idx + 1);
    if (got == BTRON_TSK_NOWALK) return BTRON_TSK_NOWALK;
    if (got <= idx) return 0;

    {
        const BTRON_TSK_ROW *rp = &rows[idx];
        tsk_num(out, cap, &n, (uint32_t)(rp->tskid < 0 ? 0 : rp->tskid), 0, 3, 1);
        tsk_put(out, cap, &n, "   ");
        tsk_put(out, cap, &n, rp->name ? rp->name : "(unnamed)");
        /* Pad to column 22 rather than to a 12-wide field: "glgears_task" is twelve characters
         * long, and at that width the name would run into STAT with no space between them.
         * A name longer than the field still overflows it -- the row keeps its columns for the
         * names this tree uses, and `ENTRY` after it is what identifies an unfamiliar one. */
        while (n < 22 && n < cap - 1) out[n++] = ' ';
        tsk_put(out, cap, &n,
                rp->state == BTRON_TSK_RUN   ? "RUN  " :
                rp->state == BTRON_TSK_SLEEP ? "SLEEP" :
                rp->state == BTRON_TSK_DEAD  ? "DEAD " : "NEW  ");
        tsk_put(out, cap, &n, " 0x");
        tsk_num(out, cap, &n, (uint32_t)(uintptr_t)rp->entry, 1, 8, 0);
        tsk_put(out, cap, &n, "  pri ");
        tsk_num(out, cap, &n, (uint32_t)(rp->pri < 0 ? 0 : rp->pri), 0, 2, 0);
        out[n] = '\0';
    }
    return 1;
}
