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
    volatile uint32_t isr_us;           /* latest ISR wall time              */
    volatile uint32_t isr_max_us;       /* ISR WCET                          */
    volatile uint32_t key_enqueue_us;   /* timestamp of latest key enqueue   */
    volatile uint32_t key_dispatch_us;  /* key enqueue -> UI dispatch        */
} async_rt_stats_t;

#endif /* BTRON_ASYNC_RT_H */
