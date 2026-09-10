/*
 * B-TRON Real-Time Kernel: POSIX Microkernel Abstraction Engine (core_posix.c)
 * Pure POSIX pthread-backed implementation of µITRON & T-Kernel specification APIs.
 */

#include <btron/itron.h>
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
}

void btron_core_print_ver(ShellOutputFn out_fn, void *user_data, const char *arg) {
    if (!out_fn) return;
    if (arg && strcmp(arg, "-a") == 0) {
        struct utsname un;
        if (uname(&un) == 0) {
            char abuf[280];
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
            char kbuf[280];
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
    pthread_mutex_unlock(&g_kernel_mutex);
    return E_OK;
}

void ext_tsk(void) {
    pthread_exit(NULL);
}

ER slp_tsk(void) {
    pthread_t self = pthread_self();
    ITRON_TASK *tsk = NULL;

    pthread_mutex_lock(&g_kernel_mutex);
    for (int i = 0; i < MAX_TASKS; i++) {
        if (g_tasks[i].active && pthread_equal(g_tasks[i].thread, self)) {
            tsk = &g_tasks[i];
            break;
        }
    }
    pthread_mutex_unlock(&g_kernel_mutex);

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

void dly_tsk(W dlytim) {
    if (dlytim > 0) {
        struct timespec ts;
        ts.tv_sec = dlytim / 1000;
        ts.tv_nsec = (dlytim % 1000) * 1000000L;
        nanosleep(&ts, NULL);
    }
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
