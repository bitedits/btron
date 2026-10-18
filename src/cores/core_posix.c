#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef __BSD_VISIBLE
#define __BSD_VISIBLE 1
#endif
#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _NETBSD_SOURCE
#define _NETBSD_SOURCE
#endif
/*
 * B-TRON Real-Time Kernel: POSIX Microkernel Abstraction Engine (core_posix.c)
 * Pure POSIX pthread-backed implementation of µITRON & T-Kernel specification APIs.
 *
 * A task here is a host thread, and the host scheduler -- not this file -- decides which CPU
 * runs it and when.  That is the honest meaning of the states btron_list_tasks() reports: RUN
 * is "its thread is live and it is not waiting in the kernel", SLEEP is "it is inside
 * dly_tsk()/slp_tsk()", and nothing here claims to know which core a worker is on.  The old
 * `ps` table printed a core number for four tasks that did not exist; this one prints the
 * tasks that do, without inventing what the kernel cannot see.
 */

#include <btron/itron.h>
#include <btron/dp.h>          /* the btron_render_perf_us() prototype: this core installs that clock */
#include <device/virtio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <btron/fs/vol_api.h>
#include <btron/fs/block.h>

#define MAX_TASKS 64
#define MAX_SEMS  64

typedef struct {
    ID tskid;
    T_CTSK config;
    pthread_t thread;
    BOOL active;
    BOOL sleeping;
    /* TRUE once sta_tsk() gave this slot a thread.  Without it, a created task and a
     * started one look the same in `ps`, and the whole point of the row is to say what the
     * kernel is actually doing with the worker. */
    BOOL started;
    pthread_cond_t cond;
    pthread_mutex_t mutex;
} ITRON_TASK;

typedef struct {
    ID semid;
    T_CSEM config;
    W count;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    BOOL active;
} ITRON_SEM;

static ITRON_TASK g_tasks[MAX_TASKS];
static ITRON_SEM  g_sems[MAX_SEMS];
static pthread_mutex_t g_kernel_mutex = PTHREAD_MUTEX_INITIALIZER;

#include <btron/apps.h>
#ifdef _WIN32
/* MinGW supplies pthreads but not the POSIX sys/utsname.h interface. */
struct utsname {
    char sysname[32];
    char nodename[32];
    char release[32];
    char version[32];
    char machine[32];
};
static int uname(struct utsname *un) {
    if (!un) return -1;
    snprintf(un->sysname, sizeof(un->sysname), "Windows");
    snprintf(un->nodename, sizeof(un->nodename), "MinGW64");
    snprintf(un->release, sizeof(un->release), "hosted");
    snprintf(un->version, sizeof(un->version), "MinGW64");
    snprintf(un->machine, sizeof(un->machine), "x86_64");
    return 0;
}
#else
#include <sys/utsname.h>
#endif

void btron_core_banner(void) {
    printf("B-System/BTRON3 3.20 (posix-hosted) Hiroaki Takada — Cleanroom TRON Kernel\n");
    printf("Copyright 2026 Synrc Research Center. MIT License.\n");
    struct utsname un;
    if (uname(&un) == 0)
        printf("[BOOT] Host: %s %s %s\n\n", un.sysname, un.release, un.machine);
    else
        printf("[BOOT] Machine: POSIX hosted\n\n");
}

void btron_core_init(void) {
    pthread_mutex_lock(&g_kernel_mutex);
    memset(g_tasks, 0, sizeof(g_tasks));
    memset(g_sems, 0, sizeof(g_sems));
    pthread_mutex_unlock(&g_kernel_mutex);
    printf("[CORE] POSIX Microkernel Abstraction Engine  BTRON_POSIX\n");
}

void btron_core_mem_log(void) {
    printf("[MEM ] POSIX hosted: memory managed by host OS allocator\n");
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak)) BlkDev *blk_qcow2_create(const char *path, int read_only) {
    (void)path; (void)read_only;
    return NULL;
}
__attribute__((weak)) BlkDev *blk_mbr_find_btron_partition(BlkDev *dev, UW fs_block_size) {
    (void)dev; (void)fs_block_size;
    return NULL;
}
#endif

void btron_core_hfds_log(void) {
    printf("[HFDS] POSIX file I/O: host filesystem passthrough  [OK]\n");
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
    /* ── Mount /ANDERS volume ───────────────────────────────────────── */
    if (!g_anders_vol) {
        BlkDev *anders_blk = blk_file_create("btron_anders.vol", 0, 0);
        if (!anders_blk) anders_blk = blk_file_create("../btron_anders.vol", 0, 0);
        if (anders_blk) {
            g_anders_vol = vol_mount(anders_blk);
            if (g_anders_vol)
                printf("[FS  ] Mounted btron_anders.vol as /ANDERS  [OK]\n");
            else
                blk_destroy(anders_blk);
        }
        if (!g_anders_vol) {
            static unsigned char s_mem_anders_vol[1024 * 1024]; /* 1 MiB */
            BlkDev *mem_blk = blk_mem_create(s_mem_anders_vol, sizeof(s_mem_anders_vol), 0);
            if (mem_blk) {
                vol_format(mem_blk, 256, 1024, "ANDERS");
                g_anders_vol = vol_mount(mem_blk);
                if (g_anders_vol)
                    printf("[FS  ] /ANDERS mounted on RAM disk (1 MiB)  [OK]\n");
            }
        }
    }
    /* ── Mount /CHOKANJI volume (B-right/V 4.02 disk image) ─────────── */
    if (!g_chokanji_vol) {
        const char *qcow2_paths[] = { "hda.qcow2", "../hda.qcow2", "PMC/chokanji_4_qemu/hda.qcow2", "../PMC/chokanji_4_qemu/hda.qcow2", NULL };
        BlkDev *raw_qcow2 = NULL;
        for (int p = 0; qcow2_paths[p]; p++) {
            raw_qcow2 = blk_qcow2_create(qcow2_paths[p], 0 /*read-write*/);
            if (raw_qcow2) break;
        }
        if (raw_qcow2) {
            BlkDev *part = blk_mbr_find_btron_partition(raw_qcow2, 8192);
            if (part) {
                g_chokanji_vol = vol_mount(part);
                if (g_chokanji_vol)
                    printf("[FS  ] Mounted B-right/V 4.02 hda.qcow2 as /CHOKANJI (8192B blocks)  [OK]\n");
                else
                    blk_destroy(part);
            } else {
                blk_destroy(raw_qcow2);
            }
        }
    }
}

void btron_core_print_ver(ShellOutputFn out_fn, void *user_data, const char *arg) {
    if (!out_fn) return;
    if (arg && strcmp(arg, "-a") == 0) {
        struct utsname un;
        if (uname(&un) == 0) {
            char abuf[512];
            snprintf(abuf, sizeof(abuf), "%s %s %s %s %s (BTRON3 3.20 Cleanroom)",
                     un.sysname, un.nodename, un.release, un.version, un.machine);
            out_fn(abuf, COLOR_CYAN, user_data);
        } else {
            out_fn("BTRON3 Sakamura T-Kernel 2.0 (Target 0: POSIX)", COLOR_CYAN, user_data);
        }
    } else if (arg && (strcmp(arg, "-r") == 0 || strcmp(arg, "-v") == 0)) {
        struct utsname un;
        if (uname(&un) == 0) {
            out_fn((strcmp(arg, "-r") == 0) ? un.release : un.version, COLOR_CYAN, user_data);
        }
    } else {
        out_fn("B-System 3.0 Workstation System (BTRON3 Specification 3.20)", COLOR_CYAN, user_data);
        struct utsname un;
        if (uname(&un) == 0) {
            char kbuf[512];
            snprintf(kbuf, sizeof(kbuf), "Host OS / Kernel: %s %s (%s, %s)",
                     un.sysname, un.release, un.machine, un.nodename);
            out_fn(kbuf, COLOR_WHITE, user_data);
        }
        out_fn("B-Kernel Subsystem: POSIX Microkernel Abstraction Mode (Target 0: BTRON_POSIX)", COLOR_GREEN, user_data);
        char build_buf[256];
        snprintf(build_buf, sizeof(build_buf), "Build Timestamp: %s %s [Compiler: %s]", __DATE__, __TIME__, __VERSION__);
        out_fn(build_buf, COLOR_LTGRAY, user_data);
        out_fn("Display Compositor: DP 2D Framebuffer Engine (1024x768 32-bpp)", COLOR_LTGRAY, user_data);
        out_fn("Japanese IME: B-System Mozc / TIP Kana-Kanji Conversion Subsystem", COLOR_LTGRAY, user_data);
    }
}

ID cre_tsk(const T_CTSK *pk_ctsk) {
    if (!pk_ctsk || !pk_ctsk->task) return E_PAR;
    
    pthread_mutex_lock(&g_kernel_mutex);
    for (int i = 0; i < MAX_TASKS; i++) {
        if (!g_tasks[i].active) {
            g_tasks[i].tskid = i + 1;
            g_tasks[i].config = *pk_ctsk;
            g_tasks[i].active = TRUE;
            g_tasks[i].sleeping = FALSE;
            g_tasks[i].started = FALSE;   /* a slot until sta_tsk() gives it a thread */
            pthread_mutex_init(&g_tasks[i].mutex, NULL);
            pthread_cond_init(&g_tasks[i].cond, NULL);
            
            ID id = g_tasks[i].tskid;
            pthread_mutex_unlock(&g_kernel_mutex);
            return id;
        }
    }
    pthread_mutex_unlock(&g_kernel_mutex);
    return E_NOMEM;
}

static void* task_wrapper(void *arg) {
    ITRON_TASK *tsk = (ITRON_TASK*)arg;
    if (tsk && tsk->config.task) {
        tsk->config.task(tsk->config.exinf);
    }
    tsk->active = FALSE;
    return NULL;
}

ER sta_tsk(ID tskid, VW exinf) {
    if (tskid <= 0 || tskid > MAX_TASKS) return E_ID;
    
    pthread_mutex_lock(&g_kernel_mutex);
    ITRON_TASK *tsk = &g_tasks[tskid - 1];
    if (!tsk->active) {
        pthread_mutex_unlock(&g_kernel_mutex);
        return E_NOEXS;
    }
    tsk->config.exinf = exinf;
    if (pthread_create(&tsk->thread, NULL, task_wrapper, tsk) != 0) {
        pthread_mutex_unlock(&g_kernel_mutex);
        return E_SYS;
    }
    /* The worker exists now: `ps` shows RUN instead of NEW from this line on. */
    tsk->started = TRUE;
    pthread_mutex_unlock(&g_kernel_mutex);
    return E_OK;
}

/* The task row of the calling thread, or NULL when the caller is not a task this kernel made
 * -- main(), and any host thread outside a cre_tsk worker.  One scan, because slp_tsk() and
 * dly_tsk() both need it and only one of them used to have it. */
static ITRON_TASK *tsk_self(void) {
    pthread_t self = pthread_self();
    pthread_mutex_lock(&g_kernel_mutex);
    for (int i = 0; i < MAX_TASKS; i++) {
        if (g_tasks[i].active && pthread_equal(g_tasks[i].thread, self)) {
            ITRON_TASK *hit = &g_tasks[i];
            pthread_mutex_unlock(&g_kernel_mutex);
            return hit;
        }
    }
    pthread_mutex_unlock(&g_kernel_mutex);
    return NULL;
}

/* The live task table (include/btron/itron.h).  Strong override of the "this core cannot
 * answer" default in core_init.c: here every cre_tsk() slot is real, and a worker's state is
 * whatever the thread's own kernel calls left it in.
 *
 * What it deliberately does not report is which CPU a task runs on.  A POSIX task is a host
 * thread; the host scheduler places it, and the four-line table this replaced printed a "#0..#3"
 * column for tasks that did not exist.  A census that cannot answer a column leaves the column
 * out. */
W btron_list_tasks(BTRON_TSK_ROW *out, W max) {
    if (max < 0) return 0;

    pthread_mutex_lock(&g_kernel_mutex);
    W n = 0;
    for (int i = 0; i < MAX_TASKS && n < max; i++) {
        const ITRON_TASK *t = &g_tasks[i];
        if (!t->active) continue;         /* free slot, or a body that returned */
        if (out) {
            out[n].tskid = t->tskid;
            out[n].name  = t->config.name;
            out[n].pri   = t->config.itskpri;
            out[n].entry = t->config.task;
            out[n].state = !t->started         ? BTRON_TSK_NEW :
                           t->sleeping         ? BTRON_TSK_SLEEP :
                                                BTRON_TSK_RUN;
        }
        n++;
    }
    pthread_mutex_unlock(&g_kernel_mutex);
    return n;
}

void ext_tsk(void) {
    pthread_exit(NULL);
}

ER slp_tsk(void) {
    ITRON_TASK *tsk = tsk_self();
    if (!tsk) return E_OBJ;

    pthread_mutex_lock(&tsk->mutex);
    tsk->sleeping = TRUE;
    while (tsk->sleeping) {
        pthread_cond_wait(&tsk->cond, &tsk->mutex);
    }
    pthread_mutex_unlock(&tsk->mutex);

    return E_OK;
}

ER wup_tsk(ID tskid) {
    if (tskid <= 0 || tskid > MAX_TASKS) return E_ID;

    pthread_mutex_lock(&g_kernel_mutex);
    ITRON_TASK *tsk = &g_tasks[tskid - 1];
    if (!tsk->active) {
        pthread_mutex_unlock(&g_kernel_mutex);
        return E_NOEXS;
    }
    pthread_mutex_unlock(&g_kernel_mutex);

    pthread_mutex_lock(&tsk->mutex);
    if (tsk->sleeping) {
        tsk->sleeping = FALSE;
        pthread_cond_signal(&tsk->cond);
    }
    pthread_mutex_unlock(&tsk->mutex);

    return E_OK;
}

ID cre_sem(const T_CSEM *pk_csem) {
    if (!pk_csem) return E_PAR;

    pthread_mutex_lock(&g_kernel_mutex);
    for (int i = 0; i < MAX_SEMS; i++) {
        if (!g_sems[i].active) {
            g_sems[i].semid = i + 1;
            g_sems[i].config = *pk_csem;
            g_sems[i].count = pk_csem->isemcnt;
            g_sems[i].active = TRUE;
            pthread_mutex_init(&g_sems[i].mutex, NULL);
            pthread_cond_init(&g_sems[i].cond, NULL);

            ID id = g_sems[i].semid;
            pthread_mutex_unlock(&g_kernel_mutex);
            return id;
        }
    }
    pthread_mutex_unlock(&g_kernel_mutex);
    return E_NOMEM;
}

ER wai_sem(ID semid) {
    if (semid <= 0 || semid > MAX_SEMS) return E_ID;
    ITRON_SEM *sem = &g_sems[semid - 1];
    if (!sem->active) return E_NOEXS;

    pthread_mutex_lock(&sem->mutex);
    while (sem->count <= 0) {
        pthread_cond_wait(&sem->cond, &sem->mutex);
    }
    sem->count--;
    pthread_mutex_unlock(&sem->mutex);

    return E_OK;
}

ER sig_sem(ID semid) {
    if (semid <= 0 || semid > MAX_SEMS) return E_ID;
    ITRON_SEM *sem = &g_sems[semid - 1];
    if (!sem->active) return E_NOEXS;

    pthread_mutex_lock(&sem->mutex);
    if (sem->count < sem->config.maxsem) {
        sem->count++;
        pthread_cond_signal(&sem->cond);
    }
    pthread_mutex_unlock(&sem->mutex);

    return E_OK;
}

ER del_sem(ID semid) {
    if (semid <= 0 || semid > MAX_SEMS) return E_ID;
    ITRON_SEM *sem = &g_sems[semid - 1];
    if (!sem->active) return E_NOEXS;

    pthread_mutex_lock(&sem->mutex);
    sem->active = FALSE;
    pthread_cond_broadcast(&sem->cond);
    pthread_mutex_unlock(&sem->mutex);

    pthread_mutex_destroy(&sem->mutex);
    pthread_cond_destroy(&sem->cond);
    return E_OK;
}

ER get_tim(SYSTIME *p_time) {
    if (!p_time) return E_PAR;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    *p_time = (SYSTIME)tv.tv_sec * 1000 + (tv.tv_usec / 1000);
    return E_OK;
}

/* The one microsecond clock the render telemetry asks for.  dp_core.c owns a weak
 * `return 0` for cores that have no clock to offer, and every caller that measures a delta
 * against it -- the fill timings in tinygl/clip.c, the compositor's [PSTAT] split, the FPS
 * caption in a window title -- reads that zero as an answer.  This core is hosted, so it
 * installs the host's monotonic clock instead: monotonic because a wall-clock step (NTP, the
 * user changing the date) must not move a measured interval backwards, and microseconds
 * truncated to uint32 because that is the ~71 min window every caller already assumes.
 *
 * get_tim() stays on gettimeofday because the spec asks `SYSTIME` for the wall clock; the two
 * are answering different questions and are not interchangeable. */
uint32_t btron_render_perf_us(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint32_t)(((uint64_t)ts.tv_sec * 1000000u) + ((uint64_t)ts.tv_nsec / 1000u));
}

void dly_tsk(W dlytim) {
    /* Record the wait, so `ps` can tell an app worker that is idle until its next frame from
     * one that is burning CPU.  Every GL app task spends its life in here -- inval_wnd() plus
     * dly_tsk() is the whole heartbeat -- so without this the STAT column would say RUN for
     * all of them and say nothing.  Threads the kernel did not create (main(), the host SDL
     * thread) have no row, and nothing is recorded for them. */
    ITRON_TASK *tsk = tsk_self();
    if (tsk) tsk->sleeping = TRUE;

    if (dlytim > 0) {
        struct timespec ts;
        ts.tv_sec = dlytim / 1000;
        ts.tv_nsec = (dlytim % 1000) * 1000000L;
        nanosleep(&ts, NULL);
    }

    if (tsk) tsk->sleeping = FALSE;
}

/* T-Kernel function aliases */
ID tkernel_cre_tsk(const T_CTSK *pk_ctsk) {
    return cre_tsk(pk_ctsk);
}

ER tkernel_sta_tsk(ID tskid, VW exinf) {
    return sta_tsk(tskid, exinf);
}

void tkernel_dispatch(void) {
    /* POSIX scheduling managed by OS pthreads */
}

void uart_puts_raw(const char *str) {
    if (!str) return;
    fputs(str, stdout);
    fflush(stdout);
}

