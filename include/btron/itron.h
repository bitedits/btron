/*
 * μITRON Specification Compatible Header: itron.h
 * Cleanroom implementation of μITRON RTOS Kernel API.
 */

#ifndef _BTRON_ITRON_H_
#define _BTRON_ITRON_H_

#include <btron/types.h>
#include <btron/error.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t ATR;
typedef void (*FP)(VW exinf);
typedef int32_t  PRI;
typedef uint64_t SYSTIME;
typedef W        TMOUT;
typedef VW       VP_INT;

#define TMO_POL        (0)
#define TMO_FEVR       (-1)
#define TA_HLNG        (0x00000001U)
#define TSK_DESKTOP_UI ((ID)1)
#define TSK_TIP_CONV   ((ID)2)

typedef struct {
    VW  exinf;     /* Extended Information */
    ATR tskatr;    /* Task Attribute */
    FP  task;      /* Task Entry Function */
    PRI itskpri;   /* Initial Priority */
    W   stksz;     /* Stack Size */
    /* What `ps` and the boot console call this worker.  μITRON has no name field and no
     * task-table walk, and that is exactly why both shells printed a hardcoded four-line
     * InterCore table: there was nothing real to read.  A creator that leaves this NULL is
     * shown by its entry address instead, so an unnamed worker is still observable.  Set it
     * on every spawn -- `T_CTSK ctsk;` is uninitialized storage, so forgetting the field
     * shows garbage where a name should be. */
    const char *name;
} T_CTSK;

/* μITRON Task Primitives */
ID cre_tsk(const T_CTSK *pk_ctsk);
ER sta_tsk(ID tskid, VW exinf);
void ext_tsk(void);
ER slp_tsk(void);
ER wup_tsk(ID tskid);

/* ── InterCore worker census ────────────────────────────────────────────────────────
 * One row of the live task table, as answered by the core that owns the tasks.  This is
 * the only place a worker's existence is recorded for display: the shells read it, they do
 * not keep their own list.
 *
 * `state` is what the core can actually prove.  On a core whose tasks are host threads, RUN
 * means "its thread is live and it is not blocked in a kernel wait" -- the host scheduler
 * decides which CPU runs it, and the guest does not pretend otherwise. */
enum {
    BTRON_TSK_NEW   = 0,   /* created, never started */
    BTRON_TSK_RUN   = 1,   /* live and unblocked */
    BTRON_TSK_SLEEP = 2,   /* blocked in slp_tsk()/dly_tsk() */
    BTRON_TSK_DEAD  = 3    /* its body returned; the slot is being released */
};

typedef struct {
    ID          tskid;
    const char *name;      /* T_CTSK.name, or NULL */
    PRI         pri;
    FP          entry;     /* T_CTSK.task: the address the name stands for */
    W           state;     /* BTRON_TSK_* */
} BTRON_TSK_ROW;

/* Copy up to `max` live task rows into `out`, lowest tskid first, and return how many were
 * written.  A core that keeps no task table returns BTRON_TSK_NOWALK (-1) rather than 0, so
 * `ps` can say "this core cannot answer" instead of the false "there are none".
 *
 * core_init.c carries the default; a core with a real table overrides it (the weak/strong
 * pair is the same one g_num_cpus already uses between core_init.c and core_smp.c). */
#define BTRON_TSK_NOWALK (-1)
W btron_list_tasks(BTRON_TSK_ROW *out, W max);

/* One formatted `ps` row, in the same shape on every shell: returns 1 when it wrote row
 * `idx`, 0 when `idx` is past the end of the table, BTRON_TSK_NOWALK when this core cannot
 * answer.  The formatting lives here, once, because the Stage-1 text console is freestanding
 * and has no printf to do it with. */
W btron_task_row_text(W idx, char *out, W cap);

/* μITRON Semaphore Primitives */
typedef struct {
    VW  exinf;
    ATR sematr;
    W   isemcnt;   /* Initial count */
    W   maxsem;    /* Max count */
} T_CSEM;

ID cre_sem(const T_CSEM *pk_csem);
ER wai_sem(ID semid);
ER sig_sem(ID semid);
ER del_sem(ID semid);

/* System Time Primitive */
ER get_tim(SYSTIME *p_time);
void dly_tsk(W dlytim);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_ITRON_H_ */
