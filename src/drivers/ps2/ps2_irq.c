/*
 * ps2_irq.c — the EE-side INPUT plane: Timer0 as a 1 kHz producer
 *
 * Part of the B-System PS2 cleanroom port.  Specification-derived only; no vendor
 * SDK code is imported.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 *
 * Why this exists as its own file: ASYNC.txt Tier-1 says the pointer's distance is
 * integrated on a 1 kHz plane and that the presenter must not be the thing that
 * services input (doc/txt/ASYNC.txt §3.2, §5 "USB progress only in GUI loop").  Until
 * now this port had no periodic interrupt at all -- every HID report waited for the
 * desktop loop to come round, so the input latency was the frame time, and the frame
 * time is what the XMB measures at hundreds of milliseconds.  A plane needs a clock,
 * so this is the clock.
 *
 * It is the BIOS kernel's own interrupt registry that gets used, not a vector table of
 * ours.  This image installs no exception handler: src/drivers/ps2/boot_ps2.s writes
 * no CP0 vector base and no Status, so an exception taken here lands in the PS2 ROM's
 * own kernel, which is what has been servicing the two syscalls the boot log already
 * makes.  Registering a callback with that kernel is therefore the only way to be
 * interrupted at all, and it is the same route the SDK takes -- through syscalls, with
 * no code imported.
 */

#include <stdint.h>

#include <btron/async_rt.h>
#include "ps2_irq.h"

/* The syscall seam lives in assembly.  clang is pointed at -march=mips2 for this image
 * and `syscall`, `sync` and `ei` are not mips2 mnemonics, which is the same reason
 * boot_ps2.s already carries the SetGsCrt and GsPutIMR calls. */
extern int32_t ps2_ee_syscall3(int32_t nr, int32_t a0, int32_t a1, int32_t a2);
extern void ps2_ee_exit_handler(void);      /* sync; CP0.Status EIE set by hand */
extern void ps2_ee_enable_interrupts(void); /* same */
extern uint32_t ps2_count_read(void);       /* CP0 Count */
extern void ps2_delay_cycles(uint32_t count);

/* The EE kernel's syscall numbers.  The convention is the ROM's own: the number in $v1,
 * arguments in $a0..$a3 untouched on the way in, the answer in $v0. */
#define EE_SYS_ADD_INTC_HANDLER 0x10
#define EE_SYS_ENABLE_INTC      0x14

/* INTC source 9 is Timer0.  The number is both the registry key and the bit EnableIntc
 * unmasks in the INTC's own mask register, so it is the only interrupt identity a
 * handler needs. */
#define EE_INTC_TIM0 9

/* Timer0's registers: EE physical 0x10000000, reached through the uncached KSEG1 alias
 * exactly as IOP RAM is reached through PS2_IOP_KSEG1 and OHCI through
 * OHCI_BASE_ADDR.  The alias is the port's habit for anything a bus master or the
 * hardware itself can change under it. */
#define T0_COUNT (*(volatile uint32_t *)0xB0000000u)
#define T0_MODE  (*(volatile uint32_t *)0xB0000010u)
#define T0_COMP  (*(volatile uint32_t *)0xB0000020u)

/* T0_MODE: CLKS in bits 1:0 selects the counting clock, 2 being the bus clock divided
 * by 256 -- 147,456,000/256 = 576,000 Hz.  Bit 6 is the counter's zero-return, bit 7 the
 * counter's update enable, bit 8 asks for an interrupt on compare match, and bits 10 and
 * 11 are the equal and overflow flags, cleared by writing 1 to them.
 *
 * Bit 6 is what makes the cadence the hardware's rather than the handler's.  A compare
 * match does not re-arm the counter on its own: the SDK's Timer3 alarm handler calls
 * SetupTIM3() at the end of every period to write COMP again
 * (ee/kernel/src/srcfile/src/alarm.c), which is only necessary because the counter
 * otherwise free-runs past the compare value and cannot match it a second time.  With the
 * zero-return set the counter comes back to 0 on each match and the period repeats
 * without any programming -- measured here, not assumed: with COMP=576 the counter was
 * seen to wrap twice in 2 ms.  What the zero-return does NOT do is clear the equal flag,
 * and that is the part that has to be acknowledged by hand; see the handler. */
#define T0_MODE_CLKS_BY_256 2u
#define T0_MODE_ZRET        (1u << 6)
#define T0_MODE_CUE         (1u << 7)
#define T0_MODE_CMPE        (1u << 8)
#define T0_MODE_CLR_FLAGS   ((1u << 10) | (1u << 11))

/* The one MODE word this driver writes, at the arm and again at the end of every period.
 * It is a single name rather than two expressions because the two writes have to agree
 * bit for bit: the register's other fields are the plane's cadence, and a handler that
 * cleared the compare flag by writing a different word would also be choosing the
 * counting clock. */
#define T0_MODE_RUN (T0_MODE_CLKS_BY_256 | T0_MODE_ZRET | T0_MODE_CUE | T0_MODE_CMPE | T0_MODE_CLR_FLAGS)
#define T0_TICKS_PER_SEC    576000u

/* The documented CP0 Count rate -- half the 294.912 MHz core clock, so 147.456 of its
 * ticks to the microsecond -- and it is now only a fallback.  The number every timing in
 * this port is quoted in is measured against the plane below, because this machine's
 * emulator turned out to feed Count at the *core* rate rather than the documented half:
 * over one 50 ms-by-Count window the OHCI frame counter advanced 25 frames and Timer0
 * ticked 24 times, with an inter-tick gap of 1985..2026 of our microseconds.  Two
 * hardware clocks that agree with each other and disagree with Count by exactly 2x put
 * the fault on the divisor and not on either timer, and a divisor copied from a datasheet
 * cannot say which of those it is describing.  A console measures ~147 here and the
 * emulator ~295, so the measurement is what makes one number correct on both. */
#define EE_TICKS_PER_US_DOC 147u

/* Periods the calibration sums before it answers.  Thirty-two of them is 32 ms of the
 * plane's own clock, which is long enough that the ±1 % a single Compare-vs-Count sample
 * carries is small against any budget this port prints, and short enough that the whole
 * window is nowhere near Count's ~14 s wrap at the emulator's rate. */
#define CALIB_PERIODS 32u

/* Spins ps2_irq_calib_wait() is willing to make for the window to fill.  Bound in loop
 * passes rather than microseconds because the thing being waited for is the definition of
 * a microsecond: a timeout written in the unit that is still unknown would be circular.
 * At 200 cycles a pass this is ~136 ms of the emulator's clock, four times the nominal
 * window, and it is the worst case of a plane that never ticks. */
#define CALIB_WAIT_SPINS 200000u

static volatile uint32_t s_ticks;
static volatile uint32_t s_wcet_ticks;
static volatile uint32_t s_overruns;
static volatile uint32_t s_last_count;
static volatile uint32_t s_gap_min_ticks = 0xFFFFFFFFu;
static volatile uint32_t s_gap_max_ticks;
/* The calibration's own running sample: the sum of the first CALIB_PERIODS inter-entry
 * gaps and how many gaps went into it.  s_ticks_per_us is zero until the window is full,
 * which is also why a measured zero -- the answer on a machine whose Count has stopped --
 * cannot be mistaken for a latched value. */
static volatile uint32_t s_calib_gap_ticks;
static volatile uint32_t s_calib_periods;
static volatile uint32_t s_ticks_per_us;
static uint32_t s_period_us;
static uint32_t s_compare;
static uint32_t s_budget_ticks;
static int s_installed;
static volatile ps2_irq_input_hook_t s_hook;

/* The producer.  Integer-only, and that is a hardware requirement rather than a style
 * rule: the EE does not switch floating-point state around a callback, the BIOS
 * dispatcher saves general registers and not the FPRs, and COP1 is live in the
 * compositor -- so a float here would quietly corrupt whatever the interrupted pixel
 * path held in a register.  It ends with the kernel's own epilogue because taking the
 * exception cleared the EE's interrupt-enable bit, and a handler that leaves it cleared
 * has taken its last tick. */
static int32_t ps2_irq_input_on_period(int32_t cause)
{
    const uint32_t t0 = ps2_count_read();
    uint32_t spent, gap;

    (void)cause;

    /* The plane's own cadence, from the inside.  A tick count that comes in at half the
     * period the window owes has two causes and they need different repairs: the clock
     * may be running at 2 ms because this machine divides its counting clock differently
     * from the way the register's field is documented, or it may be running at 1 ms and
     * losing every other edge on the way in.  The gap between consecutive entries is the
     * one number that separates them, and it is taken at entry -- before the acknowledge
     * below -- so it is the interval the hardware kept, not the one we arranged. */
    gap = (uint32_t)(t0 - s_last_count);
    s_last_count = t0;
    s_ticks++;
    if (s_ticks > 1u) {
        if (gap < s_gap_min_ticks) s_gap_min_ticks = gap;
        if (gap > s_gap_max_ticks) s_gap_max_ticks = gap;
        /* The same samples price the machine's microsecond.  Summing the gaps rather
         * than taking one is what makes the answer a rate and not a coincidence: a
         * single period carries the ±1 % that Compare-vs-Count quantisation and a lost
         * edge both look like.  The window is closed in here rather than by a reader
         * because the entries are the only party that knows what the gap between two of
         * them was, and dividing it by (periods x the period the table names) turns it
         * into ticks per real microsecond with no reference to Count at all.
         *
         * The budget is re-priced in the same breath: it is written in microseconds
         * (ASYNC.txt §3.2's 50 us ISR ceiling), so the first periods of the window were
         * charged against the documented divisor and are measured again from here.  The
         * multiply and divide are one 32-bit pair -- 32 gaps of ~295,000 ticks is 9.4e6,
         * and the window is 32,000 us -- and the image links no __udivdi3, so nothing
         * here may be wider. */
        if (s_calib_periods < CALIB_PERIODS) {
            s_calib_gap_ticks += gap;
            if (++s_calib_periods == CALIB_PERIODS) {
                const uint32_t real_us = s_calib_periods * s_period_us;
                s_ticks_per_us = (s_calib_gap_ticks + real_us / 2u) / real_us;
                if (s_ticks_per_us)
                    s_budget_ticks = ASYNC_ISR_BUDGET_US * s_ticks_per_us;
            }
        }
    }

    /* The plane's consumer, on this period, before the acknowledge below so that the
     * epilogue's interrupt enable still comes last.  Its cost is inside the WCET the
     * sample after the acknowledge measures, which is the point: the budget is the
     * handler's, whoever it runs work for. */
    {
        const ps2_irq_input_hook_t hook = s_hook;
        if (hook) hook(t0);
    }

    /* Acknowledge the period.  The compare flag is what the interrupt controller is
     * told about, and it latches: this machine answered a running counter with MODE
     * still reading 0x000005c2, bit 10 standing set, and exactly one tick ever
     * delivered.  So the edge was real and it was never cleared, which is also why the
     * SDK's alarm handler rewrites its timer's MODE with EQUF set on every period
     * (ee/kernel/src/srcfile/src/alarm.c).  Writing T0_MODE_RUN here is the
     * acknowledge, and it is the same word the arm wrote, so nothing but the flag
     * changes.
     *
     * It goes after the work and before the epilogue's interrupt enable: a match that
     * arrives while the handler is running must not be lost, and a flag that is still
     * standing when the clock is re-enabled would fire the source we are leaving. */
    T0_MODE = T0_MODE_RUN;

    /* The plane's WCET, measured rather than assumed, and charged with the acknowledge
     * above because that store is part of every period.  ASYNC.txt §10 makes the
     * handler's worst case a pass criterion, and the only way to know it here is to ask
     * the machine. */
    spent = (uint32_t)(ps2_count_read() - t0);
    if (spent > s_wcet_ticks) s_wcet_ticks = spent;
    if (spent > s_budget_ticks) s_overruns++;

    ps2_ee_exit_handler();
    return 0;      /* 0 keeps the handler installed */
}

int ps2_irq_input_arm(void)
{
    const btron_plane_cfg_t *pl = btron_planes();
    int32_t r;

    if (s_installed) return 0;

    s_period_us = pl ? pl->input_period_us : ASYNC_INPUT_PERIOD_US;
    s_budget_ticks = ASYNC_ISR_BUDGET_US * btron_ee_ticks_per_us();
    /* A period in microseconds to counter ticks: 1000 us at 576,000 Hz is 576.  Kept in
     * 32-bit form on purpose -- the image is -nostdlib and links no __muldi3, so a
     * 64-bit product here would be an undefined symbol the shape gate catches and the
     * boot does not. */
    s_compare = (s_period_us * (T0_TICKS_PER_SEC / 1000u)) / 1000u;
    if (s_compare < 1u || s_compare > 0xFFFFu) return -1;

    r = ps2_ee_syscall3(EE_SYS_ADD_INTC_HANDLER, EE_INTC_TIM0,
                        (int32_t)(unsigned long)ps2_irq_input_on_period, -1);
    if (r < 0) return r;

    /* Stop, program the compare, then start with the interrupt enabled and both flags
     * cleared.  The order is the hardware's: the counter is taken from zero only while
     * the update-enable bit is clear, so a MODE write that starts the timer first would
     * compare against whatever the counter happened to hold. */
    T0_MODE = 0u;
    T0_COUNT = 0u;
    T0_COMP = s_compare;
    T0_MODE = T0_MODE_RUN;

    /* EnableIntc's return is not the verdict: whether the plane exists is answered by
     * ticks arriving against the period they should have arrived at, which is what the
     * [ASYNC] row prints.  A mask register the BIOS accepted but never wired to the
     * counter would look exactly like success here and like DEAD -- or like one tick,
     * then STALLED -- there. */
    ps2_ee_syscall3(EE_SYS_ENABLE_INTC, EE_INTC_TIM0, 0, 0);
    ps2_ee_enable_interrupts();

    s_installed = 1;
    return 0;
}

uint32_t ps2_irq_ticks(void) { return s_ticks; }
uint32_t ps2_irq_input_period_us(void) { return s_period_us; }
uint32_t ps2_irq_timer_compare(void) { return s_compare; }

uint32_t ps2_irq_wcet_ticks(void) { return s_wcet_ticks; }
uint32_t ps2_irq_overruns(void) { return s_overruns; }

uint32_t ps2_irq_status(void)
{
    uint32_t st;
    __asm__ volatile ("mfc0 %0, $12, 0" : "=r" (st));
    return st;
}

uint32_t ps2_irq_timer_mode(void)  { return T0_MODE; }
uint32_t ps2_irq_timer_count(void) { return T0_COUNT; }

uint32_t ps2_irq_gap_min_ticks(void)
{
    return s_gap_min_ticks == 0xFFFFFFFFu ? 0u : s_gap_min_ticks;
}
uint32_t ps2_irq_gap_max_ticks(void) { return s_gap_max_ticks; }

/* The port's one microsecond.  Everything that quotes a duration in this build -- the
 * present's stage attribution, the pointer's slices, the panel and convergence nets, the
 * USB waits and the ISR's own budget -- divides CP0 Count by this, so the four copies of
 * 147 that used to answer that question are gone and the answer comes off the hardware
 * instead: see the calibration in the handler.
 *
 * Until the window is full this is the documented rate, which is the datasheet's opinion
 * of the core clock divide and the best available guess on a machine whose plane has not
 * yet ticked. */
uint32_t btron_ee_ticks_per_us(void)
{
    const uint32_t measured = s_ticks_per_us;
    return measured ? measured : EE_TICKS_PER_US_DOC;
}

int btron_ee_ticks_per_us_measured(void) { return s_ticks_per_us ? 1 : 0; }

uint32_t ps2_irq_calib_periods(void) { return s_calib_periods; }
uint32_t ps2_irq_calib_gap_ticks(void) { return s_calib_gap_ticks; }

int ps2_irq_calib_wait(void)
{
    uint32_t spin;

    for (spin = 0; spin < CALIB_WAIT_SPINS; spin++) {
        if (s_ticks_per_us) return 1;
        ps2_delay_cycles(200u);
    }
    /* No answer.  Either the plane is not ticking -- in which case the arm's own row
     * says DEAD and nothing here can invent a rate -- or Count has stopped, which makes
     * every gap zero and zero a value that cannot be a divisor.  Both cases want the
     * documented rate, and the caller gets to say which one it printed. */
    return s_ticks_per_us ? 1 : 0;
}

void ps2_irq_input_set_hook(ps2_irq_input_hook_t hook)
{
    s_hook = hook;
}
