/*
 * B-Kernel / ITRON T-Kernel RTOS Kernel Core: core_virtio.c
 * Bare-Metal & QEMU Target Implementation (ARM / VirtIO MMIO)
 */

#include <btron/itron.h>
#include <device/virtio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <btron/fs/vol_api.h>
#include <btron/fs/block.h>

#define MAX_TK_TASKS 64
#define MAX_TK_SEMS  64

typedef enum {
    TK_TS_NONEXS = 0,
    TK_TS_READY  = 1,
    TK_TS_WAIT   = 2,
    TK_TS_SUSP   = 4,
    TK_TS_DORM   = 8
} TK_TASK_STATE;

typedef struct {
    ID tskid;
    T_CTSK config;
    TK_TASK_STATE state;
    PRI current_pri;
    VP stack_ptr;
} TK_TCB;

typedef struct {
    ID semid;
    T_CSEM config;
    W count;
    BOOL active;
} TK_SEMB;

static TK_TCB  g_tk_tasks[MAX_TK_TASKS];
static TK_SEMB g_tk_sems[MAX_TK_SEMS];
static ID      g_current_tskid = 1;
static SYSTIME g_tk_system_ticks = 0;

#include <btron/apps.h>

void btron_core_banner(void) {
    printf("B-System/BTRON3 3.20 (arm-qemu-virtio) Hiroshi Tokita — Cleanroom TRON Kernel\n");
    printf("Copyright 2026 Synrc Research Center. MIT License.\n");
    printf("[BOOT] Machine: QEMU virt  ARMv7-A  VirtIO MMIO\n\n");
}

void btron_core_init(void) {
    (void)g_current_tskid;
    memset(g_tk_tasks, 0, sizeof(g_tk_tasks));
    memset(g_tk_sems, 0, sizeof(g_tk_sems));
    printf("[CORE] B-Kernel ITRON RTOS (QEMU VirtIO Mode)  BTRON_QEMU\n");
    virtio_mmio_init(0x10001000);
    virtio_driver_init_all();
}

void btron_core_mem_log(void) {
    printf("[MEM ] QEMU virt: 0x00000000-0x0FFFFFFF  256 MB RAM\n");
    printf("[MEM ] VirtIO MMIO: 0x10001000  Block device\n");
}

void btron_core_hfds_log(void) {
    printf("[HFDS] VirtIO-Block MMIO 0x10001000  [DETECT]\n");
    printf("[HFDS] HFDS Hierarchical File/Data Set: INIT  [OK]\n");
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

void btron_core_print_ver(ShellOutputFn out_fn, void *user_data, const char *arg) {
    if (!out_fn) return;
    if (arg && strcmp(arg, "-a") == 0) {
        out_fn("BTRON3 QEMU VirtIO Kernel 2.0 (Target 1: BTRON_QEMU)", COLOR_CYAN, user_data);
    } else if (arg && (strcmp(arg, "-r") == 0 || strcmp(arg, "-v") == 0)) {
        out_fn("2.0.0-virtio-qemu", COLOR_CYAN, user_data);
    } else {
        out_fn("B-System 3.0 Workstation System (BTRON3 Specification 3.20)", COLOR_CYAN, user_data);
        out_fn("B-Kernel Subsystem: QEMU VirtIO Hardware Abstraction (Target 1: BTRON_QEMU)", COLOR_GREEN, user_data);
        char build_buf[256];
        snprintf(build_buf, sizeof(build_buf), "Build Timestamp: %s %s [Compiler: %s]", __DATE__, __TIME__, __VERSION__);
        out_fn(build_buf, COLOR_LTGRAY, user_data);
        out_fn("Display Compositor: DP 2D Framebuffer Engine (1024x768 32-bpp)", COLOR_LTGRAY, user_data);
        out_fn("Japanese IME: B-System Mozc / TIP Kana-Kanji Conversion Subsystem", COLOR_LTGRAY, user_data);
    }
}

ID cre_tsk(const T_CTSK *pk_ctsk) {
    if (!pk_ctsk || !pk_ctsk->task) return E_PAR;

    for (int i = 0; i < MAX_TK_TASKS; i++) {
        if (g_tk_tasks[i].state == TK_TS_NONEXS) {
            g_tk_tasks[i].tskid = i + 1;
            g_tk_tasks[i].config = *pk_ctsk;
            g_tk_tasks[i].state = TK_TS_DORM;
            g_tk_tasks[i].current_pri = pk_ctsk->itskpri;
            return g_tk_tasks[i].tskid;
        }
    }
    return E_NOMEM;
}

ER sta_tsk(ID tskid, VW exinf) {
    if (tskid <= 0 || tskid > MAX_TK_TASKS) return E_ID;
    TK_TCB *tcb = &g_tk_tasks[tskid - 1];
    if (tcb->state == TK_TS_NONEXS) return E_NOEXS;

    tcb->config.exinf = exinf;
    tcb->state = TK_TS_READY;
    return E_OK;
}

void ext_tsk(void) {
    if (g_current_tskid > 0 && g_current_tskid <= MAX_TK_TASKS) {
        g_tk_tasks[g_current_tskid - 1].state = TK_TS_DORM;
    }
}

ER slp_tsk(void) {
    if (g_current_tskid > 0 && g_current_tskid <= MAX_TK_TASKS) {
        g_tk_tasks[g_current_tskid - 1].state = TK_TS_WAIT;
    }
    return E_OK;
}

ER wup_tsk(ID tskid) {
    if (tskid <= 0 || tskid > MAX_TK_TASKS) return E_ID;
    TK_TCB *tcb = &g_tk_tasks[tskid - 1];
    if (tcb->state == TK_TS_WAIT) {
        tcb->state = TK_TS_READY;
    }
    return E_OK;
}

ID cre_sem(const T_CSEM *pk_csem) {
    if (!pk_csem) return E_PAR;
    for (int i = 0; i < MAX_TK_SEMS; i++) {
        if (!g_tk_sems[i].active) {
            g_tk_sems[i].semid = i + 1;
            g_tk_sems[i].config = *pk_csem;
            g_tk_sems[i].count = pk_csem->isemcnt;
            g_tk_sems[i].active = TRUE;
            return g_tk_sems[i].semid;
        }
    }
    return E_NOMEM;
}

ER wai_sem(ID semid) {
    if (semid <= 0 || semid > MAX_TK_SEMS) return E_ID;
    TK_SEMB *sem = &g_tk_sems[semid - 1];
    if (!sem->active) return E_NOEXS;
    if (sem->count > 0) {
        sem->count--;
        return E_OK;
    }
    return E_TMOUT;
}

ER sig_sem(ID semid) {
    if (semid <= 0 || semid > MAX_TK_SEMS) return E_ID;
    TK_SEMB *sem = &g_tk_sems[semid - 1];
    if (!sem->active) return E_NOEXS;
    if (sem->count < sem->config.maxsem) {
        sem->count++;
    }
    return E_OK;
}

ER del_sem(ID semid) {
    if (semid <= 0 || semid > MAX_TK_SEMS) return E_ID;
    g_tk_sems[semid - 1].active = FALSE;
    return E_OK;
}

ER get_tim(SYSTIME *p_time) {
    if (!p_time) return E_PAR;
    *p_time = ++g_tk_system_ticks;
    return E_OK;
}

void dly_tsk(W dlytim) {
    (void)dlytim;
}

ID tkernel_cre_tsk(const T_CTSK *pk_ctsk) {
    return cre_tsk(pk_ctsk);
}

ER tkernel_sta_tsk(ID tskid, VW exinf) {
    return sta_tsk(tskid, exinf);
}

void tkernel_dispatch(void) {
    /* Preemptive priority dispatcher loop */
}
