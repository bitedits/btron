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

/* Which FPU law this image carries, as the unit that was compiled under it says it: "hw:cop1"
 * when the flag is on and this machine's FPU answered, "hw:bit-engine" when the flag is on and
 * it did not, "soft" when the flag is off.  A boot row needs this rather than the preprocessor
 * because the flag is per object: the core that prints it never sees -DBTRON_PS2_FP_HW, so
 * asking the compiler would answer "soft" about an image whose arithmetic unit is on. */
const char *btron_fp_law(void);

/* How many soft-float libcalls the image has taken, in seven classes:
 * [0] single add/sub/mul, [1] single div, [2] single compare, [3] float -> int,
 * [4] double add/sub/mul, [5] everything double else -- div (which is sqrt(double)'s
 * six-iteration loop, so this column is that function's fingerprint), its compares and
 * its conversions -- and [6] int -> float.
 *
 * The two conversion directions are separate columns because they are separate machines:
 * int -> float is `cvt.s.w` and COP1 answers it, so it costs what an add costs, while
 * float -> int has no instruction on this CPU and always runs the bit engine.  Counted
 * together, a stage's conversion column could not be priced at all.
 *
 * The split is what makes a stage's bill readable: a column that is small in calls and
 * large in cycles is a precision question, not a call count one.
 * Counted only when the image is built with `-DBTRON_PS2_FP_COUNT=1`
 * (`make ps2-bench PS2_FP_COUNT=1`); otherwise these stay zero, because a counter on the
 * hot side of a 28-cycle call is not free.  See ps2_builtins.c. */
extern uint32_t btron_fp_calls[7];
#define BTRON_FP_CALLS_N 7

/* The column names, shared by the TU that fills them and the readers that print them,
 * so a census row cannot drift out of step with the counters -- which is the failure a
 * six-class row that reported 267,488 adds and zero divides looked like. */
enum {
    FP_SF_ARITH = 0,   /* add/sub/mul in single */
    FP_SF_DIV,         /* div.s, the one op with no short form */
    FP_SF_CMP,         /* the six single-precision predicates */
    FP_SF_CVT,         /* float -> int, the direction with no EE instruction */
    FP_DF_ARITH,       /* add/sub/mul in double */
    FP_DF_GEN,         /* double div (sqrt(double)'s fingerprint), compares, conversions */
    FP_SF_CVTI         /* int -> float: cvt.s.w, answered by COP1 */
};

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

/* The one conversion this CPU has no instruction for, written once where both of its
 * consumers can reach it.
 *
 * `trunc.w.s` is what a compiler reaches for when a `-mhard-float` translation unit casts a
 * float to an int, and the Emotion Engine does not have it: asking for one made PCSX2 log
 * `Unrecognized FPU/COP1 op 4600010d` and left `$f4` holding the *previous* result, so the
 * illegal form is silently wrong rather than merely slow (see the note above
 * `fp_hw_cvt_s_w` in ps2_builtins.c, and the image row that byte-scans for that word).  What
 * remains is the bit engine, and it lives in a header rather than in one .c because two units
 * now need it *inline*: ps2_builtins.c's `__fixsfsi`, which is what every -msoft-float TU
 * reaches for a cast, and the FPU-enabled geometry kernel `src/gl/tinygl/vertex_stage.c`,
 * which is compiled -mhard-float and cannot call out for a conversion without something worse
 * happening -- a call makes the compiler preserve its live FPRs across it, and with the O32
 * 32-bit-FPR file those are even/odd pairs, saved and restored by `sdc1` and `ldc1`, two more
 * instructions this CPU does not answer.  Inlining is therefore not a micro-optimisation here,
 * it is what keeps the unit's op set inside the ISA, and it is one hand-written truth with two
 * readers rather than a second copy.
 *
 * A binary32 truncate is two shifts once the exponent field says which band the value is in,
 * and every band below is 32-bit, so no `__lshrdi3`/`__ashldi3` reference appears -- the same
 * self-containment law the runtime obeys.  With sig the 24-bit significand (implicit bit
 * included) and value = sig * 2^(be-150):
 *
 *   be == 0xFF   inf (sat) or NaN (0), by the mantissa field
 *   be <  127    |value| < 1 -- every subnormal and zero included -- so truncation is 0
 *   be <= 149    sig >> (150-be), a shift of 1..23, which drops exactly the fraction
 *   be <= 157    sig << (be-150), a shift of 0..7: an integer, and the largest it reaches
 *                is 0x7FFFFF80, still inside a positive int32
 *   be >= 158    2^31 or beyond -- saturate.  Exactly -2^31 saturates to 0x80000000, which
 *                is also its true value, so the two laws cannot be told apart there.
 *
 * `make test-ps2-softfloat` checks that against the host's own (int32_t) cast over 200,000
 * random bit patterns and over every exponent field x the three mantissas that can land on a
 * band edge x both signs (3,072 comparisons), and the band sweep was shown to fail when one of
 * these lines is moved by one exponent, so it is an instrument and not a formality. */
static inline uint32_t ps2_sf_fixsfsi(uint32_t a)
{
    const uint32_t sign = a >> 31;
    const uint32_t be = (a >> 23) & 0xFFu;
    const uint32_t frac = a & 0x7FFFFFu;
    uint32_t mag;

    if (be == 0xFFu) return frac ? 0u : (sign ? 0x80000000u : 0x7FFFFFFFu);
    if (be < 127u) return 0u;
    if (be >= 158u) return sign ? 0x80000000u : 0x7FFFFFFFu;

    if (be >= 150u)
        mag = (frac | 0x800000u) << (int)(be - 150u);      /* the shift is 0..7 */
    else
        mag = (frac | 0x800000u) >> (int)(150u - be);      /* the shift is 1..23 */
    return sign ? (uint32_t)0u - mag : mag;
}
/* The one comparison a `-mhard-float` translation unit may ask this CPU for, written once where
 * its consumers can reach it.
 *
 * What the XMB's black screen under `PS2_FP=hw` measured is in `gl_clipcode()`
 * (src/gl/tinygl/zgl.h): with the geometry kernel compiled for the FPU, clang lowered its six
 * plane tests into three `c.olt.s` and three `c.ule.s`, and the faces that came back clipped were
 * exactly the ones whose `pc` values sat inside every plane -- `pc = -1.0/-0.514/0.0` against a
 * `w` of 1.00001 with `cc = XMAX|YMAX|ZMAX`.  The three tests written with the ordered form
 * answered correctly and the three written with the unordered-inclusive form did not, which is
 * the same split this header already carries for `trunc.w.s` and for `c.un.s`: an instruction the
 * Emotion Engine's COP1 decoder does not recognise here does not raise and does not stall, it
 * leaves the condition code holding whatever the previous compare wrote, so the branch goes the
 * way the *last* comparison decided.  Every face through the funnel then reads as outside the
 * volume, no triangle reaches a fill, and the screen is black while the arithmetic in the same
 * function -- `pc.X/Y/Z/W` and the viewport mapping beside it -- is provably right.
 *
 * So the legal form is the even one, and the pair is moved into the FPU the way the engine in
 * ps2_builtins.c already does it: `mtc1` from general registers, one `nop` after the pair (the
 * FPU needs a cycle to see the new operands), one `nop` between the compare and its branch (the
 * condition code is written one cycle after the compare retires), and a `nop` in the delay slot.
 * The arguments are bit patterns rather than floats for that reason: a float operand under
 * `-mhard-float` arrives in an FPR, which `mtc1` cannot read, and the two moves that would fix it
 * are the `mov.s`/`mfc1` pair this unit does answer -- they cost less here than a wrong answer.
 *
 * Ordered means ordered: a NaN pair makes `c.olt.s` come out false, which is what `a < b` means in
 * C, so a vertex whose coordinate is a NaN gets clip code 0 here exactly as it does through the
 * `-msoft-float` path's `__ltsf2`.  The two dials therefore agree on every input, not only on the
 * ordinary ones. */
static inline int ps2_fp_olt_s(uint32_t a, uint32_t b)
{
    int r = 0;

    __asm__ ("mtc1 %1, $f0\n\tmtc1 %2, $f2\n\tnop\n\tnop\n\t"
             "c.olt.s $f0, $f2\n\t"
             "nop\n\t"
             "bc1f 1f\n\t"
             "nop\n\t"
             "li %0, 1\n\t"
             "1:\n\t"
             : "+r" (r) : "r" (a), "r" (b));
    return r;
}

/* A float's bits in a general register, for the helper above.  Under `-mhard-float` this is one
 * `mfc1`, which the EE answers; under every other law it is the store-and-reload the compiler
 * would do anyway for a bit pattern that has to reach an integer instruction. */
static inline uint32_t ps2_fp_bits_f(float f)
{
    uint32_t u;
    __builtin_memcpy(&u, &f, sizeof u);
    return u;
}

#endif /* _PS2_FP_H_ */
