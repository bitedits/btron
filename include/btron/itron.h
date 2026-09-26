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
} T_CTSK;

/* μITRON Task Primitives */
ID cre_tsk(const T_CTSK *pk_ctsk);
ER sta_tsk(ID tskid, VW exinf);
void ext_tsk(void);
ER slp_tsk(void);
ER wup_tsk(ID tskid);

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
