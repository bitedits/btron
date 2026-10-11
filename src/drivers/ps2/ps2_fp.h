#ifndef _PS2_FP_H_
#define _PS2_FP_H_

/*
 * ps2_fp.h - the Emotion Engine's single-precision FPU, and the choice of whether
 * the image uses it
 *
 * ps2_builtins.c holds two engines for the same 14 single-precision compiler-runtime
 * calls: the bit-level one that has always been there, and the COP1 one that became
 * possible once the port knew the EE's FPU is single-precision only (`.d` illegal,
 * `.s` fine, CU1 enabled by the BIOS -- see the `[CPU] CP0.Status` boot row).  Which
 * one runs is a run-time decision, made by btron_fp_init() and recorded in
 * btron_fp_on, so a machine that does not hand over a working FPU costs cycles rather
 * than pixels.  Doubles never go to the FPU at all.
 *
 * What the differential self-test measured on this machine is written once, normatively,
 * in doc/md/PS2.md section "What COP1 actually is, measured" -- the counts, the census and
 * the argument that rules out a rounding mode all live there so they cannot drift from each
 * other.  The three facts this header has to carry are: the bit engine is host-proven exact,
 * so it is the oracle and a disagreement convicts COP1; COP1's add.s/sub.s/mul.s answer one
 * representable step from the oracle on a minority of ordinary pairs, in both directions,
 * never wider and never across zero, while its div.s/sqrt.s/compares/cvt.s.w match it on
 * every clean pair; and the FPU is not where the frame cost is, because the XMB's bake and
 * draw time sit in include/gl/math.h's double-precision series, which this CPU has no
 * instruction for at all.
 *
 * The port's arithmetic assertions therefore sit in the gap table below rather than in the
 * `other` count: a gap never wider than one step and never across zero is a property of
 * this unit to design around, while a wide or sign-flipped one is a broken instruction
 * sequence.  Those two counts are what scripts/ps2_smooth.sh gates.
 *
 * One thing no measurement in this tree can settle: every `[FPU]` number was taken inside
 * PCSX2's Emotion Engine, and the same image over SIO on a real console is what says
 * whether the residue is the emulator or the silicon.  Nothing in the port depends on the
 * answer, but a native GS backend written against this unit's rounding should quote which
 * machine produced it.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdint.h>

extern int btron_fp_on;

/* Enable CU1 if the BIOS left it clear, mask the FPU's exception enables and clear
 * its stale flags, and prove the unit answers by one round trip: 1.0f + 2.0f, which
 * has to come back 0x40400000.  Returns the value left in btron_fp_on.  Call before
 * anything times or rasterises. */
int btron_fp_init(void);

/* The bits that round trip actually returned, so a `COP1 did not answer` row names
 * the value it rejected instead of asserting it.  Zero before the init. */
extern uint32_t btron_fp_seen;

/* CP0.Status, and FCSR through fcsr (may be 0).  Reads 0/0 off the PS2 target. */
uint32_t btron_fp_probe(uint32_t *fcsr);

/* Runs both engines over a fixed vector of bit patterns and reports what they
 * disagree about and what each costs.  Returns the largest gap in units in the last
 * place (0 when the two agree everywhere); ops/bad/hw_ticks/soft_ticks may each be
 * null.  Ticks are CP0 Count, so the caller turns them into microseconds with its own
 * clock constant. */
uint32_t btron_fp_selftest(uint32_t *ops, uint32_t *bad,
                           uint32_t *hw_ticks, uint32_t *soft_ticks);

/* Filled by btron_fp_selftest(): one row per op class, columns
 * [0]=clean arguments run, [1]=clean disagreements, [2]=special arguments run
 * (zero, subnormal, infinite or NaN), [3]=special disagreements.  A total count says
 * an engine is wrong; this table says which op, and whether ordinary pixels are
 * affected or only the non-arithmetic edge cases.  All zero before the self-test runs,
 * and in a build that has no COP1 path.
 *
 * The row numbers and their names live here, not in the caller, so the boot table
 * cannot drift out of step with the engine that fills it. */
enum {
    BTRON_FP_CLS_ADD = 0,     /* add.s    */
    BTRON_FP_CLS_SUB,         /* sub.s    */
    BTRON_FP_CLS_MUL,         /* mul.s    */
    BTRON_FP_CLS_DIV,         /* div.s    */
    BTRON_FP_CLS_LTLT,        /* ltsf2/lesf2, asked as `< 0` and `< 1` */
    BTRON_FP_CLS_GTGTE,       /* gtsf2/gesf2, asked as `> 0` and `> -1` */
    BTRON_FP_CLS_EQNE,        /* eqsf2/nesf2 */
    BTRON_FP_CLS_CVT,         /* cvt.s.w, int -> float */
    BTRON_FP_CLS_SQRT,        /* sqrt.s, by squaring the answer back */
    BTRON_FP_CLS_COUNT
};
#define BTRON_FP_CLS_N BTRON_FP_CLS_COUNT
extern const char *const btron_fp_cls_name[BTRON_FP_CLS_N];
extern uint32_t btron_fp_cls[BTRON_FP_CLS_N][4];

/* Which of the five causes explains the disagreements, counted over every class at
 * once.  The five are not one defect:
 *
 *   daz         an operand is subnormal and the unit reads it as zero (denormals-are-
 *               zero), so the answer is the one for the flushed input;
 *   ftz         both operands are ordinary but the disagreement lives at or below the
 *               underflow boundary, where one engine keeps a subnormal and the other
 *               flushes it to zero.  Whether this machine does that is a measurement, not
 *               an assumption -- the count says;
 *   nonfinite   an operand -- or, for an arithmetic op, one of the two results -- is
 *               infinite or NaN, so this is the unit's law for those and not its
 *               arithmetic.  For the compares it is also the known compromise: the EE
 *               has no unordered test, so a pair that fails both c.olt and c.ole is
 *               answered as if one of the ordered cases had held;
 *   inexact     the conversion cannot be exact at all -- an integer too large for a
 *               float's 23-bit mantissa -- so the two engines only had to pick
 *               different neighbours of the true value;
 *   other       nothing explains it: the two engines gave different numbers to the same
 *               ordinary operands.  Informational, not a gate -- asking it to reach zero
 *               while add.s runs on COP1 asks this unit to round differently than it
 *               does.  The gates on the arithmetic are the gap counts below, and a
 *               non-zero `other` only matters through those.
 *
 * Filled by btron_fp_selftest(); all zero before it runs and in a build with no COP1
 * path. */
enum {
    BTRON_FP_SHAPE_DAZ = 0,
    BTRON_FP_SHAPE_FTZ,
    BTRON_FP_SHAPE_NONFINITE,
    BTRON_FP_SHAPE_INEXACT,
    BTRON_FP_SHAPE_OTHER,
    BTRON_FP_SHAPE_COUNT
};
#define BTRON_FP_SHAPE_N BTRON_FP_SHAPE_COUNT
extern const char *const btron_fp_shape_name[BTRON_FP_SHAPE_N];
extern uint32_t btron_fp_shape[BTRON_FP_SHAPE_N];

/* The name of the op class that first put a count in BTRON_FP_SHAPE_OTHER, or null when
 * the `other` bucket is empty: a row that says the engine is wrong should also say
 * which op was. */
extern const char *btron_fp_other_where;

/* How far apart the two engines' answers were, counted over the clean-operand pairs of
 * the four arithmetic classes only (add/sub/mul/div, where both engines return a float):
 * [0]=1 step, [1]=2 steps, [2]=3 steps up, [3]=opposite signs, and the last two split
 * bucket [0] by direction -- [4]=COP1's magnitude is the smaller one, [5]=the larger.
 * That split is what tells a rounding decision from a bug: a unit that truncates would
 * put every one-step gap in [4], a fixed mode would favour one side, and a gap in [2] or
 * [3] is not a rounding question at all but a different number.  This is where the two
 * arithmetic gates sit -- [2] and [3] must stay zero -- and what the counts on this
 * machine are is in doc/md/PS2.md.  Zeroed and filled by btron_fp_selftest(). */
#define BTRON_FP_GAP_N 6
extern uint32_t btron_fp_gap[BTRON_FP_GAP_N];
extern const char *const btron_fp_gap_name[BTRON_FP_GAP_N];

/* The first BTRON_FP_DUMP_N pairs whose disagreement had no cause, as
 * [row][0]=op class, [1]=operand a, [2]=operand b, [3]=COP1's answer, [4]=the bit
 * engine's, [5]=gap in steps -- all bit patterns except for cvt.s.w, where the operands
 * are the integers being converted.  btron_fp_dump_n says how many rows hold a pair.
 * These are the evidence for the one question the counts cannot answer, which of the two
 * engines is right: the exact sum, product or quotient of a single-precision pair is
 * computable on any host, so a printed pair can be judged there rather than argued about
 * here.  Eight rather than four because the first four let three explanations be tested
 * and rejected (truncation, ties-to-zero, a missing sticky bit); a claim about a unit's
 * rounding law needs enough sampled pairs to falsify it, not just enough to fit a
 * pattern.  Empty when every disagreement was explained. */
#define BTRON_FP_DUMP_N 8
extern uint32_t btron_fp_dump[BTRON_FP_DUMP_N][6];
extern uint32_t btron_fp_dump_n;

#endif /* _PS2_FP_H_ */
