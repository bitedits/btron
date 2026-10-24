#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(BTRON_UEFI_TARGET)
#include_next <math.h>
#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif
/* The hosted half still owes the two spellings of a float question that the freestanding
 * half defines below, because `src/apps/xmb_wave.c` is compiled both ways and writes the
 * operation, not the instruction.  Here they are the cast and the operator they have always
 * been; the laws they exist to serve are in the comments above the other definitions. */
#define BTRON_F2I(x) ((int)(x))
#define BTRONFLT_LT(a, b) ((a) < (b))
#else
#ifndef _GL_MATH_H_
#define _GL_MATH_H_

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif

/* Two things the freestanding half needs rather than a libm, because there is none in
 * a -nostdlib link: the exponent of a double as an integer, and the scaling that puts
 * it back.  Working on the bits is exact and costs no call.  They sit above the x87
 * split rather than inside the series half because the callers are on both sides of it:
 * the series sqrt normalises with them, and btron_log/btron_exp below the split use
 * them even where pow takes the fyl2x path and never reaches that code. */
static inline int btron_dexp(double x) {
    union { double d; unsigned long long u; } v;
    v.d = x;
    return (int)((v.u >> 52) & 0x7ff) - 1023;
}

static inline double btron_ldexp(double x, int e) {
    union { double d; unsigned long long u; } v;
    v.d = x;
    v.u += (unsigned long long)(long long)e << 52;
    return v.d;
}

/* A float's whole part, as the one statement rather than as whichever instruction the
 * compiler happens to reach for.  `btron_sincosf`'s turn count is the only place this
 * header casts a float to an int, and the cast is a *law* question rather than a style
 * one on the Emotion Engine: a `-mhard-float` translation unit compiles `(int)x` to
 * `trunc.w.s`, which that CPU's COP1 decoder does not answer, and an unanswered compare
 * or conversion does not trap and does not stall -- it leaves the destination register
 * holding the previous result.  A fold that silently turns every angle into the previous
 * one's residue is not a slow wave, it is a wrong one, and the wrongness is invisible in
 * a picture.  So the units built with the FPU flag (`src/drivers/ps2/ps2_builtins.c`,
 * `src/gl/tinygl/vertex_stage.c`, `src/apps/xmb_wave.c`) get the exponent-band truncate
 * that the soft runtime's own `__fixsfsi` already runs, and everyone else gets the plain
 * cast they always had.  This is not a second implementation of that conversion:
 * `ps2_sf_fixsfsi()` is one hand-written truth in `src/drivers/ps2/ps2_fp.h` with three
 * readers, which is why the truncate lives in a header at all. */
#if defined(BTRON_PS2_FP_HW)
#include "ps2_fp.h"                 /* ps2_sf_fixsfsi(), ps2_fp_bits_f(), ps2_fp_lt_s() */
#define BTRON_F2I(x) ((int)ps2_sf_fixsfsi(ps2_fp_bits_f(x)))
/* An ordered float comparison that asks no question of the FPU's condition code.
 *
 * It is not reachable by choosing the operand order: `a < b` compiles to `c.olt.s`, but
 * once one side is a constant LLVM canonicalises the comparison to put the constant on
 * the right, and `x > 3.14f` and `3.14f < x` are then the *same* IR -- which the MIPS
 * backend lowers to `c.ule.s`, the unordered-inclusive form this decoder does not have.
 * Measured on a six-form micro file: `a < b` -> olt, `!(a < b)` -> olt with the branch
 * inverted, `a > b` -> ule, `b < a` with `b` a constant -> ule, `!(a <= b)` -> ole.
 * Of those the only form that keeps C's meaning for a NaN pair is the hand-written ordered
 * one, and an angle that is a NaN must not be folded as if it were a number -- that is the
 * same class of silent-wrong the missing `trunc.w.s` produced, and this port has already had
 * one black screen from a compare that did not answer.
 *
 * The hand-written `c.olt.s` (`ps2_fp_olt_s()`, which `src/gl/tinygl/zgl.h`'s six clip plane
 * tests still route through) is therefore not enough here: it writes `$f0` and `$f2` out of
 * integer operands and cannot say so in a constraint, which is harmless at a call boundary
 * and silently wrong in a fold that gets *inlined* -- and inlining is the whole point of
 * `src/apps/xmb_wave.c`.  So the fold compares bit patterns instead, in integers, for the
 * price of the eleven instructions `ps2_fp_lt_s()` is and no condition code at all.  On a
 * build without the FPU flag this is the plain C operator it always was. */
#define BTRONFLT_LT(a, b) (ps2_fp_lt_s(ps2_fp_bits_f(a), ps2_fp_bits_f(b)) != 0)
#else
#define BTRON_F2I(x) ((int)(x))
#define BTRONFLT_LT(a, b) ((a) < (b))
#endif

/* sin/cos/sqrt/pow exist twice below, once in x87 and once in arithmetic, because
 * this header is the freestanding math for every bare-metal target and those do not
 * all have an x87: the PS2 (Target 8) and the Malta MIPS (Target 9) are MIPS, and an
 * `fsin` there is an illegal instruction, not a slow path.  The split is on the
 * architecture rather than on a BTRON_ define so that a new target picks the branch
 * that is correct for its ISA without anyone remembering to declare it.  The two
 * branches must agree to the precision a software rasteriser cares about -- a pixel
 * colour or a matrix entry -- so the arithmetic ones are given below with their
 * error stated where the code needs it. */
#if defined(__i386__) || defined(__x86_64__)

static inline double sin(double x) {
    double res;
    __asm__ ("fsin" : "=t" (res) : "0" (x));
    return res;
}

static inline double cos(double x) {
    double res;
    __asm__ ("fcos" : "=t" (res) : "0" (x));
    return res;
}

static inline double sqrt(double x) {
    double res;
    __asm__ ("fsqrt" : "=t" (res) : "0" (x));
    return res;
}

#else /* no x87: everything below is series and exponent arithmetic */

/* Declared here, defined below with the other two that have no float-native form: the tier
 * guards on `sinf`/`cosf` ask for a magnitude, and they come first. */
static inline float fabsf(float);

/* sqrt by Babylonian iteration on a mantissa normalised into [1,4), which is where
 * one starting guess is within 40% for every input and six halvings of the error
 * square reach the double precision TinyGL's z-buffer and light vectors need.
 * O(1) in the value: the exponent is folded, not searched. */
static inline double sqrt(double x) {
    if (x == 0.0) return 0.0;
    if (x < 0.0)  return (x - x) / (x - x);         /* NaN, as a real sqrt answers */
    int e = btron_dexp(x);
    double m = btron_ldexp(x, -e);                  /* [1,2) */
    if (e & 1) m += m;                              /* [1,4), and sqrt pairs the odd e */
    double r = m < 2.0 ? 1.3 : 1.8;
    for (int i = 0; i < 6; i++) r = 0.5 * (r + m / r);
    /* The halved exponent has to be a floor, not a truncation: an odd e means the
     * mantissa was doubled above, so the power to scale back by is (e-1)/2, and for
     * a negative odd e -- every x in [0.5,1) -- C's e/2 lands one power too high and
     * the answer is exactly 2x the square root. */
    int k = (e >= 0 || (e & 1) == 0) ? e / 2 : (e - 1) / 2;
    return btron_ldexp(r, k);
}

/* sin and cos share one reduction: fold the angle into the first eighth of the
 * circle, where both Taylor series converge to ~1e-17 with seven terms, and carry
 * the folds as sign and swap flags.  Rounding through a long long is exact for the
 * |x| a modelview matrix or a spotlight cone actually produces; beyond 2^53 radians
 * any reduction is noise. */
static inline void btron_sincos(double x, double *s, double *c) {
    const double pi = 3.14159265358979323846, pi2 = 6.28318530717958647693;
    int neg_sin = 0;
    if (x < 0.0) { x = -x; neg_sin = 1; }               /* sin is odd, cos even */
    if (x > pi) x -= (double)(long long)(x / pi2) * pi2; /* into [0,2pi) */
    if (x > pi) { x = pi2 - x; neg_sin = !neg_sin; }     /* sin(2pi-a) = -sin(a) */
    int flip_cos = 0;
    if (x > pi / 2.0) { x = pi - x; flip_cos = 1; } /* now in [0,pi/2] */
    int swap = 0;
    if (x > pi / 4.0) { x = pi / 2.0 - x; swap = 1; }   /* now in [0,pi/4] */
    double a2 = x * x, term, sum;
    term = x; sum = x;
    for (int n = 3; n <= 13; n += 2) { term *= -a2 / ((double)n * (double)(n - 1)); sum += term; }
    double sn = sum;
    term = 1.0; sum = 1.0;
    for (int n = 2; n <= 14; n += 2) { term *= -a2 / ((double)n * (double)(n - 1)); sum += term; }
    double cs = sum;
    *s = swap ? cs : sn;
    *c = swap ? sn : cs;
    if (flip_cos) *c = -*c;
    if (neg_sin)  *s = -*s;
}

static inline double sin(double x) { double s, c; btron_sincos(x, &s, &c); return s; }
static inline double cos(double x) { double s, c; btron_sincos(x, &s, &c); return c; }

/* The three float-native ones, and why they are not the double functions with a cast
 * around them: a target with no FPU at all (or one, like the Emotion Engine, whose
 * single-precision unit is fast and whose double unit does not exist) pays for every
 * operation in the series twice over -- once in the promotion, once in the software
 * double arithmetic that follows it.  Measured on the PS2 image that turned one
 * XMB icon cell into 14,718 CPU cycles per pixel, because a rounded-box distance calls
 * sqrtf and the gear's eight studs call sinf and cosf per pixel.
 *
 * Each keeps the same shape as its double relative -- fold, then one polynomial -- and
 * hands back the double function for the inputs where a float has no business being
 * exact: zero, subnormal, infinite and NaN for sqrtf, and infinity, NaN and an argument
 * so many turns round that no reduction of it means anything for the trig pair. */
static inline float sqrtf(float x) {
    if (x == 0.0f) return x;                        /* +-0 keeps its own sign, as libm */
    union { float f; unsigned int u; } v;
    v.f = x;
    const unsigned int e = (v.u >> 23) & 0xFFu;
    /* "negative, or NaN" -- the same inputs the plain C test `!(0.0f < x)` picks out, and
     * asked of the bits instead of of a comparison.  That spelling is not a micro-optimisation
     * here: the negation of a strict compare is *only* expressible as `c.ule.s`, because LLVM
     * has to keep it true for a NaN pair, and `c.ule.s` is the unordered family this CPU's COP1
     * decoder does not answer -- an unanswered compare leaves the condition code holding the
     * previous one's result, so a root would be taken or refused on the strength of whatever
     * was tested last.  The set is exactly: sign bit set (every negative, -inf included, as it
     * was) or exponent all ones with a mantissa (NaN, of either sign), and +inf -- exponent set,
     * mantissa clear -- still falls through to the line below and roots itself. */
    if ((v.u >> 31) || (e == 0xFFu && (v.u & 0x7FFFFFu) != 0u))
        return (x - x) / (x - x);                   /* negative and NaN both answer NaN */
    if (e == 0xFFu) return x;                       /* +inf is its own root */
    /* A subnormal has no exponent field for the guess below to halve, and the double
     * relative is no refuge either (its fold assumes a normal value and lands 2x out on
     * a subnormal), so scale by 2^48 -- exact, and its root is 2^24 -- and undo that on
     * the answer rather than on the input. */
    float back = 1.0f;
    if (e == 0u) { v.f = x * 281474976710656.0f; back = 5.96046448e-8f; }
    const float s = v.f;                            /* the value whose root this is */
    /* 0x5F3759DF - (bits >> 1) is the inverse-square-root guess: it halves the exponent
     * and approximates the mantissa's, good to under 4% before any iteration. */
    v.u = 0x5F3759DFu - (v.u >> 1);
    float r = v.f;
    /* Newton on r = 1/sqrt(s) converges by squaring the error, and each step is three
     * multiplies and a subtract -- no division, which on a soft-float target is the
     * most expensive thing in the file.  Three steps carry the 4% guess to under 4 ulp
     * of a float, which is what a distance fed to a soft-edged coverage wants. */
    const float half = s * 0.5f;
    for (int i = 0; i < 3; i++) r = r * (1.5f - half * r * r);
    return back * (half * (r + r));                 /* s * r, without a second multiply */
}

/* Every "is this angle past that band edge" test below is written through
 * `BTRONFLT_LT()`, and that is not decoration: on the Emotion EE `a > b` lowers to
 * `c.ule.s` -- the unordered-inclusive family, which is not in that CPU's COP1 decoder at
 * all.  It does not trap and it does not stall; it leaves the condition code holding
 * whatever the previous compare wrote.  A fold whose band test reads a stale flag picks
 * somebody else's residue, and a wrong wave is not something a screenshot shows.  The
 * operand order is not the fix -- LLVM canonicalises a comparison against a constant to
 * the same IR whichever side the constant is written on -- and neither is the hand-written
 * ordered test, because this fold is *inlined* into a unit that keeps its floats in the
 * registers a call may not touch.  So the answer is the integer one: four band edges and no
 * FP condition code anywhere.  The law and its measurement, including the black screen that
 * produced it, are in doc/txt/PS2.txt, "The compares the EE does not have", and
 * `scripts/test_ps2.sh` row 58 byte-scans the linked image for the family. */
static inline void btron_sincosf(float x, float *s, float *c) {
    const float pi = 3.14159274f, pi2 = 6.28318548f;
    const float pio2 = 1.57079637f, pio4 = 0.78539819f;
    /* float(2*pi) is 1.748e-7 too large, and folding a turn with the bare constant
     * moves the angle by exactly that much, so the residue travels with the turn count.
     * Without it the 2pi..4pi region measures 3.6e-7 of absolute error, six times the
     * error of every region that needs no fold. */
    const float pi2_lo = -1.74845553e-7f;
    int neg_sin = 0;
    if (x < 0.0f) { x = -x; neg_sin = 1; }                  /* sin odd, cos even */
    if (BTRONFLT_LT(pi, x)) {
        const float k = (float)BTRON_F2I(x / pi2);
        x -= pi2 * k;
        x -= pi2_lo * k;                                    /* into [0,2pi) */
    }
    /* The residue only buys anything applied to the small result: pi2 + pi2_lo rounds
     * straight back to pi2, since the two are closer than a float's spacing there. */
    if (BTRONFLT_LT(pi, x)) { x = (pi2 - x) + pi2_lo; neg_sin = !neg_sin; }  /* sin(2pi-a) = -sin(a) */
    int flip_cos = 0;
    if (BTRONFLT_LT(pio2, x)) { x = pi - x; flip_cos = 1; }  /* now in [0,pi/2] */
    int swap = 0;
    if (BTRONFLT_LT(pio4, x)) { x = pio2 - x; swap = 1; }    /* now in [0,pi/4] */
    /* At pi/4 the first dropped term is 1.7e-9, which is under a float's last place
     * near 1.0, so these five- and four-term forms are as accurate as a float can be. */
    const float x2 = x * x;
    const float sn = x * (1.0f + x2 * (-1.66666667e-1f + x2 * (8.33333333e-3f +
                        x2 * (-1.98412698e-4f + x2 * 2.75573192e-6f))));
    const float cs = 1.0f + x2 * (-5.0e-1f + x2 * (4.16666667e-2f +
                        x2 * (-1.38888889e-3f + x2 * 2.48015873e-5f)));
    *s = swap ? cs : sn;
    *c = swap ? sn : cs;
    if (flip_cos) *c = -*c;
    if (neg_sin)  *s = -*s;
}

/* Past the float fold's range the turn count is taken in double instead: one
 * reciprocal multiply, one nearest-integer conversion, two double multiplies and two
 * subtracts -- against the thirteen software double divisions (and the other thirty-nine
 * double calls) the series costs when the argument is handed to the double relative.  The
 * XMB's ribbon is the consumer that lives here: it calls cosf(gz + t/10 + gx) per lit
 * pixel and its t advances for the lifetime of the boot, so the arguments leave the fold's
 * range and never come back.
 *
 * The nearest-integer quotient keeps |r| at or below half a turn, so the residue that
 * btron_sincosf would have subtracted itself is already gone and the fold inside it
 * costs nothing.  pi2's own two-part form travels with the turn count, as in the float
 * fold: double(2*pi) sits 2.4493e-16 below 2*pi, and at three hundred million turns
 * that gap is 8e-8 of angle -- a float's last place near 1.0, which is the precision
 * this whole path is trying to keep. */
static inline void btron_sincosf_far(float x, float *s, float *c) {
    const double pi2 = 6.28318530717958647693;
    const double pi2_lo = 2.44929359829470641e-16;
    const double d = x;
    /* Multiply by the reciprocal rather than divide by 2*pi: on a -msoft-float target a
     * division is the most expensive thing in the file.  The constant is good to 1e-17
     * relative, so the quotient can only land on the wrong integer within half a
     * turn's width of an exact half -- and one turn off is still an angle btron_sincosf
     * folds correctly, at the float fold's own accuracy. */
    const double q = d * 0.15915494309189533577;
    const long long kk = (long long)(q + (q < 0.0 ? -0.5 : 0.5));
    const double dk = (double)kk;
    btron_sincosf((float)(d - dk * pi2 - dk * pi2_lo), s, c);
}

/* Three tiers, because the three answers cost very different things.  Inside 4*pi the
 * float fold's error is less than a float's last place and the polynomial runs on its
 * own: the turn count is 0, 1 or 2 and each product is exact where it matters.  From
 * there to 2^31 the fold moves to double (above) and the polynomial stays in float.
 * The bound is the fold's own: its error is half an ulp of the turn product, which at
 * 2^31 is 1.2e-7 -- still worth having -- and past it the angle a float hands back is
 * coarser than any reduction of it.  Only infinity and NaN go further, to the double
 * relative, which answers NaN for both; the long long the double fold rounds through
 * would not survive the magnitudes in between anyway.
 *
 * The middle tier is why a `-mhard-float` unit on the Emotion EE may not call these two:
 * `btron_sincosf_far`'s double multiplies and subtracts are `.d` COP1 forms, which that CPU
 * does not implement at all, so out there the reduction would not be slow but silently
 * absent and the residue handed to the fold would be the previous one.
 * `src/apps/xmb_wave.c` is the consumer that lives at that boundary: it inlines
 * `btron_sincosf` for the arguments the float fold covers and sends everything wider back
 * to the `-msoft-float` unit that owns the animation clock, through bit patterns.
 *
 * Each tier guard is `BTRONFLT_LT(fabsf(x), bound)` rather than the pair of comparisons it
 * replaces, for the reason given above `btron_sincosf`: with a constant on the left, `a < b`
 * is the *same IR* as `b > a` and lowers to `c.ule.s`, which this CPU does not answer -- and a
 * magnitude against a positive bound is the same set of arguments, NaN included. */
static inline float sinf(float x) {
    float s, c;
    if (BTRONFLT_LT(fabsf(x), 12.5663706f)) { btron_sincosf(x, &s, &c); return s; }
    if (BTRONFLT_LT(fabsf(x), 2147483648.0f)) { btron_sincosf_far(x, &s, &c); return s; }
    return (float)sin((double)x);
}

static inline float cosf(float x) {
    float s, c;
    if (BTRONFLT_LT(fabsf(x), 12.5663706f)) { btron_sincosf(x, &s, &c); return c; }
    if (BTRONFLT_LT(fabsf(x), 2147483648.0f)) { btron_sincosf_far(x, &s, &c); return c; }
    return (float)cos((double)x);
}

/* Exact, and without asking the FPU anything: the exponent field says how many
 * mantissa bits are fractional, and clearing them is the floor for a positive value.
 * A negative one needs one step further away from zero, but only when bits were
 * actually dropped -- which is why the dropped mask is kept. */
static inline float floorf(float x) {
    union { float f; unsigned int u; } v;
    v.f = x;
    const unsigned int e = (v.u >> 23) & 0xFFu;
    if (e >= 127u + 23u) return x;                  /* integral already, or inf/NaN */
    if (e < 127u)                                    /* |x| < 1: 0, or -1 if negative */
        return (v.u & 0x7FFFFFu) == 0u ? x           /* +-0 keeps its own sign */
                                       : (v.u & 0x80000000u) ? -1.0f : 0.0f;
    const unsigned int mask = 0x007FFFFFu >> (e - 127u);
    const unsigned int dropped = v.u & mask;
    v.u &= ~mask;                                        /* truncate toward zero */
    /* One whole step further from zero, and only when bits were actually dropped.  This
     * has to be a float subtract: bumping the exponent field is a doubling, which is what
     * the first version of this line did and why it answered -2000 for -1000.00006.
     * Here |v.f| is below 2^23, so v.f - 1.0f is exact. */
    if (dropped && (v.u & 0x80000000u)) return v.f - 1.0f;
    return v.f;
}

#endif /* x87 or series */

static inline double fabs(double x) {
    return (x < 0.0) ? -x : x;
}

static inline double atan(double x) {
    int neg = 0, inv = 0;
    if (x < 0.0) { neg = 1; x = -x; }
    if (x > 1.0) { inv = 1; x = 1.0 / x; }   /* atan(x) + atan(1/x) = pi/2 */
    int halves = 0;
    /* atan(x) = 2*atan(x / (1 + sqrt(1+x*x))) pulls |x| inside the radius
     * where the alternating series below converges. */
    while (x > 0.4142135624) {
        x = x / (1.0 + sqrt(1.0 + x * x));
        halves++;
    }
    double x2 = x * x, term = x, sum = x;
    for (double i = 3.0; i < 41.0; i += 2.0) { term *= -x2; sum += term / i; }
    double r = sum;
    while (halves--) r += r;
    if (inv) r = M_PI / 2.0 - r;
    return neg ? -r : r;
}

/* No fpatan here: its x87 stack operands are ordered by register constraints
 * that differ between the i686 target and the host, and the guessed pairing
 * put results in the wrong quadrant. */
static inline double atan2(double y, double x) {
    if (x == 0.0) {
        if (y > 0.0) return M_PI / 2.0;
        if (y < 0.0) return -M_PI / 2.0;
        return 0.0;
    }
    double a = atan(y / x);
    if (x > 0.0) return a;
    return a + (y >= 0.0 ? M_PI : -M_PI);
}

/* fabsf and atan2f have no float-native form, so both branches promote.  The four
 * wrappers that do -- sinf/cosf/sqrtf/floorf -- are single-precision polynomials in the
 * series branch above, and only the x87 half asks the double function underneath them;
 * those live below `floor`, which one of them calls. */
static inline float fabsf(float x)  { return (x < 0.0f) ? -x : x; }
static inline float atan2f(float y, float x) { return (float)atan2((double)y, (double)x); }

static inline double floor(double x) {
    int i = (int)x;
    if (x < (double)i) return (double)(i - 1);
    return (double)i;
}

#if defined(__i386__) || defined(__x86_64__)
static inline float sinf(float x)   { return (float)sin((double)x); }
static inline float cosf(float x)   { return (float)cos((double)x); }
static inline float sqrtf(float x)  { return (float)sqrt((double)x); }
static inline float floorf(float x) { return (float)floor((double)x); }
#endif

static inline double btron_log(double x) {
    int e = btron_dexp(x);
    double m = btron_ldexp(x, -e);              /* [1,2) */
    if (m > 1.41421356237309504880) { m *= 0.5; e++; }   /* [sqrt(1/2),sqrt(2)] */
    double z = (m - 1.0) / (m + 1.0);           /* log m = 2*atanh z */
    double z2 = z * z, term = z, sum = z;
    for (int n = 3; n <= 25; n += 2) { term *= z2; sum += term / (double)n; }
    return 2.0 * sum + (double)e * 0.69314718055994530942;
}

/* Common logarithm, derived from btron_log rather than re-implemented: log10(x) is
 * log(x) * (1/ln 10), and the reciprocal is written as one rounded double constant
 * because a second series here would be a second model of the same curve.  `pow`
 * guards x <= 0 the same way -- a negative argument has no bit-pattern to read, so
 * btron_log on its own would return garbage rather than an error. */
static inline double log10(double x) {
    if (x <= 0.0) return 0.0;
    return btron_log(x) * 0.43429448190325182765;
}

/* exp scaled by 2^n from the bit pattern, so the series only ever runs on
 * |f| <= ln2/2 -- thirteen terms there is good to the last place of a double. */
static inline double btron_exp(double x) {
    double n = (double)(long long)(x * 1.44269504088896340736);
    double f = x - n * 0.69314718055994530942;
    double term = 1.0, sum = 1.0;
    for (int i = 1; i <= 13; i++) { term *= f / (double)i; sum += term; }
    return btron_ldexp(sum, (int)n);
}

static inline double pow(double x, double y) {
    if (x <= 0.0) return 0.0;
    if (y == 0.0) return 1.0;
    int yi = (int)y;
    if (y == (double)yi && yi > 0) {
        double r = 1.0;
        double b = x;
        while (yi > 0) {
            if (yi & 1) r *= b;
            b *= b;
            yi >>= 1;
        }
        return r;
    }
#if defined(__i386__) || defined(__x86_64__)
    double res;
    __asm__ ("fyl2x\n\t"
             "fld %%st(0)\n\t"
             "frndint\n\t"
             "fsubr %%st, %%st(1)\n\t"
             "fxch\n\t"
             "f2xm1\n\t"
             "fld1\n\t"
             "faddp\n\t"
             "fscale\n\t"
             "fstp %%st(1)"
             : "=t" (res) : "0" (x), "u" (y) : "st(1)");
    return res;
#else
    return btron_exp(y * btron_log(x));
#endif
}

static inline float powf(float x, float y) { return (float)pow((double)x, (double)y); }
static inline long long llround(double x) { return (long long)(x >= 0.0 ? x + 0.5 : x - 0.5); }

#ifndef M_E
#define M_E 2.71828182845904523536
#endif

static inline double exp(double x) { return pow(M_E, x); }
static inline float expf(float x) { return (float)exp((double)x); }
static inline float log10f(float x) { return (float)log10((double)x); }
static inline double fmod(double x, double y) {
    if (y == 0.0) return 0.0;
    return x - (double)((long long)(x / y)) * y;
}
static inline float fmodf(float x, float y) { return (float)fmod((double)x, (double)y); }

#endif /* _GL_MATH_H_ */
#endif /* HOSTED */

