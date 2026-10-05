/*
 * B-System (BTRON 3.20) Real-Time Task Scheduling of KKC Engines: tip_task.c
 * Cleanroom implementation conforming to btron-tip.tex Section 5.2.
 */

#include <btron/tip.h>
#include <btron/mozc_engine.h>
#include <btron/itron.h>

#include <btron/libc_shim.h>

/*
 * TIP Asynchronous Conversion Task Lifecycle (btron-tip.tex Section 5.2)
 */
void tip_conversion_task(VP_INT exinf) {
    (void)exinf;

    while (1) {
        /* Sleep until triggered by Input Front-End (IFE) */
        slp_tsk();

        /* If composition reading is active, perform morphological lattice search */
        const char *reading = tip_get_reading();
        if (reading && reading[0] != '\0') {
            TIP_CLAUSE clauses[TIP_MAX_CLAUSES];
            int num_clauses = 0;
            mozc_lattice_search(reading, clauses, &num_clauses, TIP_MAX_CLAUSES);
        }

        /* Post completion event / wake up desktop UI task */
        wup_tsk(TSK_DESKTOP_UI);
    }
}
