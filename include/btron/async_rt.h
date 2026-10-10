/*
 * B-System ASYNC.txt Tier-1 Input/Presentation Plane Contracts
 *
 * Equal-priority INPUT (1 kHz) and UI (~120 Hz) workers with fixed periods
 * and per-period budgets.  The timer ISR is bounded: it drains at most
 * ASYNC_ISR_TRB_BUDGET xHCI TRBs, publishes a wait-free pointer snapshot,
 * and returns.  No rendering, mailbox calls, allocation, or event dispatch
 * ever happen in IRQ context.
 */

#ifndef BTRON_ASYNC_RT_H
#define BTRON_ASYNC_RT_H

#include <stdint.h>

/* ASYNC.txt §7 fixed configuration (periods and budgets, not priorities) */
#define ASYNC_INPUT_PERIOD_US   1000u   /* INPUT worker period: 1 ms          */
#define ASYNC_UI_PERIOD_US      8333u   /* UI worker period: ~120 Hz          */
#define ASYNC_ISR_TRB_BUDGET    32u     /* max xHCI TRBs consumed per IRQ     */
#define ASYNC_INPUT_EVT_BUDGET  16u     /* max HID reports per INPUT period   */
#define ASYNC_ISR_MOUSE_BUDGET  8u      /* max mouse reports folded per IRQ   */

/* Per-period CPU budgets for the equal-priority planes (ASYNC.txt §3).  The
 * scheduler runs a plane when its absolute deadline is due and records its
 * WCET against these budgets; a plane that overruns is flagged but never
 * blocks the other plane, which is serviced on the next loop trip. */
#define ASYNC_INPUT_BUDGET_US   500u    /* INPUT plane soft CPU budget        */
#define ASYNC_UI_BUDGET_US      4000u   /* UI plane soft CPU budget           */


/* ── The machine's plane table ─────────────────────────────────────────────
 * Periods belong to the machine, never to whichever rasterizer the image
 * happens to link.  virgl, TinyGL and a native GS backend all run the same
 * INPUT and UI planes as the same two tasks with the same priorities; only
 * their measured cost differs, and that cost is what a budget is charged
 * against.  Reading the periods off the renderer instead is what makes a slow
 * rasterizer an architecture problem: on the PS2 the pass that paints is also
 * the pass that spends the pointer's distance, so every "px per pass" the
 * cursor is tuned with is really the frame time in disguise.
 *
 * A core installs one table at boot, in its own machine units.  A core that
 * installs nothing keeps pacing itself and is enforced by nothing. */
typedef struct {
    uint32_t input_period_us;      /* HID integration cadence: one USB frame   */
    uint32_t ui_period_us;         /* one present, at the display's own rate   */
    uint32_t panel_refresh_us;     /* the clock rows go stale on their own     */
    uint32_t full_convergence_us;  /* whole-canvas repaint safety net         */
    uint16_t input_slices_max;     /* periods caught up per pass, not all of   */
                                   /* them: a long paint must not turn into a  */
                                   /* long loop just because time passed       */
} btron_plane_cfg_t;

void btron_planes_install(const btron_plane_cfg_t *cfg);
const btron_plane_cfg_t *btron_planes(void);

/* Legacy spelling kept for existing call sites */
#define ASYNC_XHCI_TRB_BUDGET   ASYNC_ISR_TRB_BUDGET
#define ASYNC_HID_REPORT_BUDGET ASYNC_INPUT_EVT_BUDGET

/* Wait-free seqlock pointer snapshot (ASYNC.txt §4, §6).
 *
 * Single producer: the 1 kHz timer IRQ (input_plane_on_irq in core_arm64.c).
 * Single consumer: the 1 ms INPUT worker (input_plane_task).
 *
 * The accumulators are monotonically increasing sums of per-report deltas.
 * The consumer diffs against its own last-consumed copy, so MOVE batches
 * coalesce losslessly without any reset handshake: a reader that falls behind
 * simply sees a larger delta, never a torn or lost one.  Buttons carry the
 * latest raw HID button byte; button edges are never dropped because the
 * consumer compares against its own previous snapshot. */
typedef struct {
    volatile uint32_t seq;        /* even = stable, odd = update in progress */
    volatile int32_t  acc_dx;     /* monotonic sum of clamped per-report dx  */
    volatile int32_t  acc_dy;     /* monotonic sum of clamped per-report dy  */
    volatile int32_t  acc_wheel;  /* monotonic sum of wheel deltas           */
    volatile uint8_t  buttons;    /* latest raw HID button bits              */
    volatile uint8_t  present;    /* a mouse report has been published       */
    volatile uint16_t motion_seq; /* bumped on every non-zero delta batch    */
    volatile uint32_t pad;
} __attribute__((aligned(16))) pointer_slot_t;

typedef struct {
    uint32_t seq;
    int32_t  acc_dx;
    int32_t  acc_dy;
    int32_t  acc_wheel;
    uint8_t  buttons;
    uint8_t  present;
    uint16_t motion_seq;
} pointer_snapshot_t;

/* Written by the IRQ plane, formatted from the UI plane; never allocate or
 * format text in the input path. */
typedef struct {
    volatile uint32_t input_gap_us;     /* latest INPUT cadence gap          */
    volatile uint32_t input_gap_max_us; /* worst gap (IRQ jitter when armed) */
    volatile uint32_t trb_per_input;    /* TRBs drained on latest IRQ        */
    volatile uint32_t trb_max;          /* worst TRB count per IRQ           */
    volatile uint32_t blit_us;          /* latest present band copy time     */
    volatile uint32_t blit_max_us;      /* worst band copy time              */
    volatile uint32_t composite_us;     /* latest workbench_render time      */
    volatile uint32_t composite_max_us; /* worst workbench_render since reset*/
    volatile uint32_t present_us;       /* latest present_backbuffer_rect    */
    volatile uint32_t present_max_us;   /* worst present rect since reset    */
    volatile uint32_t isr_us;           /* latest ISR wall time              */
    volatile uint32_t isr_max_us;       /* ISR WCET                          */
    volatile uint32_t key_enqueue_us;   /* timestamp of latest key enqueue   */
    volatile uint32_t key_dispatch_us;  /* key enqueue -> UI dispatch        */
} async_rt_stats_t;

#endif /* BTRON_ASYNC_RT_H */
