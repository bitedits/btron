/*
 * ps2_irq.h — Cleanroom Emotion EE interrupt plane (INTC + Timer0)
 *
 * The EE-side clock for the ASYNC.txt Tier-1 planes: the BIOS kernel's own INTC
 * registry, reached through the same syscall mechanism this image already uses for
 * SetGsCrt and GsPutIMR, with Timer0's compare interrupt as the producer.
 * Specification-derived only; no vendor SDK code is imported.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef PS2_IRQ_H
#define PS2_IRQ_H

#include <stdint.h>

/* Arm Timer0 as the INPUT plane's clock, at the period the machine's plane table
 * names, and register the callback with the BIOS kernel's INTC registry.
 *
 * Returns 0 when the registry accepted the handler and a negative BIOS error when it
 * did not.  A 0 here is still not evidence that ticks arrive: an armed clock is a
 * claim, ps2_irq_ticks() advancing is the measurement, and the caller prints the
 * difference.  Idempotent. */
int ps2_irq_input_arm(void);

/* INPUT periods taken since the arm -- the plane's own heartbeat. */
uint32_t ps2_irq_ticks(void);

/* The handler's worst observed cost, in the machine's own Count ticks, and the number of
 * periods it overspent its budget.  Ticks rather than microseconds because a handler this
 * small spends less than one microsecond's worth of them: the caller prints the fraction.
 * The budget it is charged against (ASYNC.txt §3.2's 50 us) is re-priced by the divisor
 * below the moment that divisor is measured, so the two are in the same unit. */
uint32_t ps2_irq_wcet_ticks(void);
uint32_t ps2_irq_overruns(void);

/* What the handler costs, as the machine measured it: the compare value Timer0 was
 * programmed with, and the period it means. */
uint32_t ps2_irq_input_period_us(void);
uint32_t ps2_irq_timer_compare(void);

/* CP0 Status, read.  The plane's life depends on one bit of it: the Emotion EE's global
 * interrupt enable is bit 16 (EIE), not bit 0, and taking an exception clears it, so a
 * clock that stops is normally an epilogue that failed to set it back.  A caller that
 * prints tick counts should print this beside them, because the two together say which
 * of "never armed" and "armed once" happened. */
uint32_t ps2_irq_status(void);

/* Timer0's own state, read: its MODE word and its counter.  A plane that took one tick
 * and then stopped has failed in one of two places -- the counter, which is a hardware
 * problem, or the delivery of its interrupt, which is a kernel one -- and these two reads
 * separate them: a counter advancing through the compare value with no tick is a masked
 * or unacknowledged source, while a counter sitting above the compare value and not
 * coming back is a clock that free-ran past its own period. */
uint32_t ps2_irq_timer_mode(void);
uint32_t ps2_irq_timer_count(void);

/* The spacing between consecutive ticks, in the machine's own Count ticks, as the
 * handler measured them -- the extremes over every period since the arm, not over one
 * window.  This is the number that tells a plane whose clock is running at the wrong rate
 * from one whose edges are being lost: they look identical in a tick count.
 *
 * There is deliberately no microsecond form of these two.  The divisor below is measured
 * from these gaps, so dividing a gap by the divisor it produced returns the plane's period
 * by construction and carries no information. */
uint32_t ps2_irq_gap_min_ticks(void);
uint32_t ps2_irq_gap_max_ticks(void);

/* The machine's microsecond: how many CP0 Count ticks pass in one real microsecond,
 * measured by the plane above over CALIB periods of its own clock instead of taken from
 * the core clock's datasheet.  This is the single source every duration in the PS2 build
 * divides by -- the present's stages, the pointer's slices, the panel and convergence
 * nets, the USB and IOP-RAM waits, and the ISR's budget.
 *
 * Before the first window closes (and on a machine whose plane never ticks) it answers the
 * documented 147, which is the correct rate on a console, where Count really does run at
 * half the 294.912 MHz core.  btron_ee_ticks_per_us_measured() says which of the two the
 * caller is printing: 1 for the plane's measurement, 0 for the fallback.  The emulator
 * this port's feedback loop runs on measures ~295, because it feeds Count at the core
 * rate rather than the documented half, and every microsecond quoted from 147 there is 2x
 * too long. */
uint32_t btron_ee_ticks_per_us(void);
int btron_ee_ticks_per_us_measured(void);

/* The calibration's raw sample: how many gaps it has taken and their sum in ticks.  A
 * divisor is only worth as much as the window behind it, and a row that prints the
 * division without the pair cannot be checked against the plane's period. */
uint32_t ps2_irq_calib_periods(void);
uint32_t ps2_irq_calib_gap_ticks(void);

/* Spin until the calibration window has closed.  Returns 1 when it did and 0 when the
 * wait ran out, which is the difference between "the divisor below is now the machine's"
 * and "the divisor below is still the datasheet's".  It costs the plane's own window --
 * 32 ms of real time on a live 1 kHz clock -- and it is bounded in spins rather than in
 * microseconds because a timeout in the unit being measured would be circular. */
int ps2_irq_calib_wait(void);

/* The plane's one consumer, called from inside the handler on every period.
 *
 * This is how a driver puts work on the plane without the handler knowing what the work
 * is: the OHCI's periodic collection is the only consumer this port has, and it belongs to
 * `ps2_usb.c`, not to the clock.  The hook runs where the handler runs -- integer only, no
 * syscall, no print, no float -- because the BIOS dispatcher saves general registers and
 * not the FPRs, and because a handler that blocks on the kernel it interrupted is the
 * deadlock this port's whole design avoids.  The argument is CP0 Count read at the
 * handler's entry, so a consumer can timestamp the moment it read the bus without taking a
 * second clock read.
 *
 * Set it to 0 to hand the work back to whichever pass used to do it.  One hook, not a list:
 * two consumers on one 1 kHz period is two parties racing for one deadline, and the budget
 * the [ASYNC] row prints is the sum of whatever is installed. */
typedef void (*ps2_irq_input_hook_t)(uint32_t entry_count);
void ps2_irq_input_set_hook(ps2_irq_input_hook_t hook);

#endif /* PS2_IRQ_H */
