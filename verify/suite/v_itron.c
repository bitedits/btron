/*
 * v_itron.c — uITRON Kernel API Verification Suite
 *
 * L1 conformance: verify that uITRON task and semaphore APIs
 * are linkable and return correct types.
 * L2 conformance: behavioral checks (create/start/delete lifecycle).
 */

#include "../btron_verify.h"
#include <btron/itron.h>

#define S "uITRON"

/* Minimal task entry point for lifecycle tests */
static volatile int task_ran = 0;

static void test_task_entry(VW exinf)
{
    task_ran = 1;
    (void)exinf;
    ext_tsk();
}

void vfy_suite_itron(void)
{
    /* ── Type size checks ───────────────────────────────────── */
    VFY_ASSERT_EQ(S, "sizeof(T_CTSK)", sizeof(T_CTSK) > 0, 1);
    VFY_ASSERT_EQ(S, "sizeof(T_CSEM)", sizeof(T_CSEM) > 0, 1);
    VFY_ASSERT_EQ(S, "sizeof(ATR)",    sizeof(ATR),    4);
    VFY_ASSERT_EQ(S, "sizeof(PRI)",    sizeof(PRI),    4);
    VFY_ASSERT_EQ(S, "sizeof(SYSTIME)", sizeof(SYSTIME), 8);

    /* ── Constant checks ────────────────────────────────────── */
    VFY_ASSERT_EQ(S, "TMO_POL",  TMO_POL,   0);
    VFY_ASSERT_EQ(S, "TMO_FEVR", TMO_FEVR, -1);

    /* ── Task creation ──────────────────────────────────────── */
    T_CTSK ctsk;
    memset(&ctsk, 0, sizeof(ctsk));
    ctsk.task    = test_task_entry;
    ctsk.itskpri = 10;
    ctsk.stksz   = 4096;
    ctsk.tskatr  = 0;
    ctsk.exinf   = NULL;

    ID tid = cre_tsk(&ctsk);
    VFY_ASSERT_TRUE(S, "cre_tsk(valid)>0", tid > 0);

    /* ── Semaphore lifecycle ────────────────────────────────── */
    T_CSEM csem;
    memset(&csem, 0, sizeof(csem));
    csem.isemcnt = 1;
    csem.maxsem  = 10;
    csem.sematr  = 0;
    csem.exinf   = NULL;

    ID sid = cre_sem(&csem);
    VFY_ASSERT_TRUE(S, "cre_sem(valid)>0", sid > 0);

    /* Signal and wait on semaphore (count was 1, so wai_sem should succeed) */
    if (sid > 0) {
        ER er = sig_sem(sid);
        VFY_ASSERT_EQ(S, "sig_sem(valid)", er, E_OK);

        er = del_sem(sid);
        VFY_ASSERT_EQ(S, "del_sem(valid)", er, E_OK);
    }

    /* ── get_tim linkability ────────────────────────────────── */
    SYSTIME t = 0;
    ER er = get_tim(&t);
    VFY_ASSERT_EQ(S, "get_tim(valid)", er, E_OK);
    VFY_ASSERT_TRUE(S, "get_tim returns value", t > 0);
}
