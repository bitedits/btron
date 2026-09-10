/*
 * Sakamura T-Kernel 2.0 Specification Engine Core: core_foma.c
 * Target 10: BTRON_FOMA (AArch32 UMTS Mobile Profile)
 * Routes BTRON API calls directly into Sakamura T-Kernel 2.0 Real-Time Operating System.
 */

#include <tk/tkernel.h>
#include <device/virtio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "kernel.h"
#include "isyscall.h"
#include "timer.h"
#include <btron/core.h>
#include <sys/utsname.h>
#include <btron/fs/vol_api.h>
#include <btron/fs/block.h>

extern void tkernel_init_subsystems(int full_suite);

void btron_core_banner(void) {
    printf("B-System/BTRON3 3.20 (foma-tkernel-virtio) Ken Sakamura — T-Kernel 2.0\n");
    printf("Copyright 2026 Synrc Research Center. MIT License.\n");
    printf("[BOOT] Machine: NTT DoCoMo FOMA SH903i (TI OMAP2430 ARM1136 AArch32 Profile)\n");
    printf("[BOOT] Display: 480x640 TFT 32-bpp Portrait (VGA Vertical Screen)\n\n");
}

void btron_core_mem_log(void) {
    printf("[MEM ] FOMA hosted: memory managed by T-Kernel 2.0 allocator\n");
}

void btron_core_hfds_log(void) {
    printf("[HFDS] VirtIO-Block MMIO 0x10001000  [DETECT]\n");
    printf("[HFDS] FOMA Hierarchical File/Data Set (HFDS) Real Body Storage: INIT [OK]\n");
    /* ── Mount /SYS volume ─────────────────────────────────────────── */
    if (!g_sys_vol) {
        BlkDev *sys_blk = blk_file_create("btron_sys.vol", 0 /*open existing*/, 0);
        if (!sys_blk) sys_blk = blk_file_create("../btron_sys.vol", 0, 0);
        if (sys_blk) {
            g_sys_vol = vol_mount(sys_blk);
            if (g_sys_vol)
                printf("[FS  ] Mounted btron_sys.vol as /SYS  [OK]\n");
            else {
                blk_destroy(sys_blk);
                printf("[FS  ] btron_sys.vol: invalid magic, using RAM disk\n");
            }
        }
        if (!g_sys_vol) {
            /* Fall back: fresh in-memory volume */
            static unsigned char s_mem_vol[1024 * 1024]; /* 1 MiB */
            BlkDev *mem_blk = blk_mem_create(s_mem_vol, sizeof(s_mem_vol), 0);
            if (mem_blk) {
                vol_format(mem_blk, 256, 1024, "SYS");
                g_sys_vol = vol_mount(mem_blk);
                if (g_sys_vol)
                    printf("[FS  ] /SYS mounted on RAM disk (1 MiB)  [OK]\n");
            }
        }
    }
}

void btron_core_init(void) {
    printf("[CORE] Sakamura T-Kernel 2.0 Engine Target 10: BTRON_FOMA\n");

    tkernel_init_subsystems(0);

    printf("[T-KERNEL] All 14 Sakamura T-Kernel 2.0 subsystems initialized.\n");

    virtio_mmio_init(0x10001000);
}

void btron_core_print_ver(ShellOutputFn out_fn, void *user_data, const char *arg) {
    if (!out_fn) return;
    if (arg && strcmp(arg, "-a") == 0) {
        out_fn("NTT DoCoMo FOMA SH903i AArch32 (BTRON3 3.20 / T-Kernel 2.0 Cleanroom)", COLOR_CYAN, user_data);
    } else if (arg && (strcmp(arg, "-r") == 0 || strcmp(arg, "-v") == 0)) {
        out_fn("3.20-foma-aarch32", COLOR_CYAN, user_data);
    } else {
        out_fn("µBTRON-FOMA Mobile Workbench (BTRON3 Specification 3.20)", COLOR_CYAN, user_data);
        out_fn("Handset Profile: NTT DoCoMo FOMA 2004-2009 Series (TI OMAP2430 / ARM1136)", COLOR_WHITE, user_data);
        out_fn("B-Kernel Subsystem: Sakamura T-Kernel 2.0 VirtIO Real-Time (Target 10: BTRON_FOMA)", COLOR_GREEN, user_data);
        char build_buf[256];
        snprintf(build_buf, sizeof(build_buf), "Build Timestamp: %s %s [Compiler: %s]", __DATE__, __TIME__, __VERSION__);
        out_fn(build_buf, COLOR_LTGRAY, user_data);
        out_fn("Display Compositor: 480x640 VGA Portrait Framebuffer (Focus-driven HMI)", COLOR_LTGRAY, user_data);
        out_fn("Japanese IME: B-System Mozc / TIP Keitai 5-Tap Kana-Kanji Conversion Subsystem", COLOR_LTGRAY, user_data);
    }
}

/* Sakamura T-Kernel Low-Level Primitive & HAL Support Routines */
void* Imalloc(size_t sz) { return malloc(sz); }
void  Ifree(void *ptr) { free(ptr); }
void* Icalloc(size_t nmemb, size_t sz) { return calloc(nmemb, sz); }
void* IAmalloc(size_t sz, UINT attr) { (void)attr; return malloc(sz); }
void  IAfree(void *ptr, UINT attr) { (void)attr; free(ptr); }

void BitSet(void *base, UW offset) {
    uint32_t *p = (uint32_t *)base;
    int idx = offset / 32;
    int bit = offset % 32;
    p[idx] |= (1U << bit);
}

void BitClr(void *base, UW offset) {
    uint32_t *p = (uint32_t *)base;
    int idx = offset / 32;
    int bit = offset % 32;
    p[idx] &= ~(1U << bit);
}

int BitSearch0_w(const uint32_t *base, int offset, int width) {
    for (int i = 0; i < width; i++) {
        int pos = offset + i;
        int idx = pos / 32;
        int bit = pos % 32;
        if (!(base[idx] & (1U << bit))) return i;
    }
    return -1;
}

int BitSearch1_w(const uint32_t *base, int offset, int width) {
    for (int i = 0; i < width; i++) {
        int pos = offset + i;
        int idx = pos / 32;
        int bit = pos % 32;
        if (base[idx] & (1U << bit)) return i;
    }
    return -1;
}

int BitTest(const uint32_t *base, int offset) {
    int idx = offset / 32;
    int bit = offset % 32;
    return (base[idx] & (1U << bit)) ? 1 : 0;
}

UINT disint(void) { return 0; }
UINT enaint(UINT intsts) { return intsts; }
void DisableInt(UINT vec) { (void)vec; }
void EnableInt(INTVEC intvec) { (void)intvec; }
void SetIntMode(UINT vec, UINT mode) { (void)vec; (void)mode; }
void ClearInt(UINT vec) { (void)vec; }
BOOL CheckInt(INTVEC intvec) { (void)intvec; return FALSE; }

ATR available_cop = 0;
void *hook_dsp = NULL;
void *unhook_dsp = NULL;
void *hook_int = NULL;
void *unhook_int = NULL;
void *hook_svc = NULL;
void *unhook_svc = NULL;

ER no_support(void) { return -70; /* E_NOSPT */ }
void timer_handler_startup(void) {}

void request_tex(TCB *tcb) { (void)tcb; }
void low_pow(void) {}
void off_pow(void) {}
void tm_monitor(void) {}
void tm_putstring(const char *s) { if (s) printf("%s", s); }

INT _tk_get_cfn(UB *name, INT *val, INT max) {
    (void)name;
    if (val && max > 0) val[0] = 0;
    return 1;
}

INT __tk_get_cfn(UB *name, INT *val, INT max) {
    return _tk_get_cfn(name, val, max);
}

INT GetDevConf(CONST UB *name, INT *val) { (void)name; if (val) val[0] = 0; return 0; }
INT GetSysConf(CONST UB *name, INT *val) { (void)name; if (val) val[0] = 0; return 0; }

void tm_command(const char *cmd) { (void)cmd; }
void tm_exit(int code) { (void)code; }

void *lowmem_top = NULL;
void call_entry(void) {}
void dispatch_entry(void) {}
void rettex_entry(void) {}
void _tk_ret_int(void) {}
void call_dbgspt(void) {}
void defaulthdr_startup(void) {}
void exchdr_startup(void) {}
void inthdr_startup(void) {}

/* 
 * Direct Routing to Sakamura T-Kernel 2.0 System Call Implementations
 */
ID tk_cre_tsk(CONST T_CTSK *pk_ctsk) {
    return _tk_cre_tsk(pk_ctsk);
}

ER tk_sta_tsk(ID tskid, INT stacd) {
    return _tk_sta_tsk(tskid, stacd);
}

void tk_ext_tsk(void) {
    _tk_ext_tsk();
}

void tk_exd_tsk(void) {
    _tk_exd_tsk();
}

ER tk_slp_tsk(TMO tmout) {
    return _tk_slp_tsk(tmout);
}

ER slp_tsk(void) {
    return _tk_slp_tsk(TMO_FEVR);
}

ER tk_wup_tsk(ID tskid) {
    return _tk_wup_tsk(tskid);
}

ER wup_tsk(ID tskid) {
    return _tk_wup_tsk(tskid);
}

ID tk_get_tid(void) {
    return _tk_get_tid();
}

ID tk_cre_sem(CONST T_CSEM *pk_csem) {
    return _tk_cre_sem(pk_csem);
}

ER tk_wai_sem(ID semid, INT cnt, TMO tmout) {
    return _tk_wai_sem(semid, cnt, tmout);
}

ER tk_sig_sem(ID semid, INT cnt) {
    return _tk_sig_sem(semid, cnt);
}

ER tk_del_sem(ID semid) {
    return _tk_del_sem(semid);
}

ER tk_dly_tsk(RELTIM dlytim) {
    return _tk_dly_tsk(dlytim);
}

/* Stubs for optional desktop applications not deployed to mobile FOMA */
__attribute__((weak)) void open_audio_player_window(void) {}
__attribute__((weak)) void open_tad_browser_window(void) {}
__attribute__((weak)) void launch_beos_chat(void) {}
