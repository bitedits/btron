/*
 * libaeabi.c
 *
 * The ARM EABI conversion libcalls the 32-bit bare-metal lane asks the runtime
 * for: __aeabi_l2d, __aeabi_ul2d, __aeabi_l2f, __aeabi_ul2f, __aeabi_d2lz and
 * __aeabi_f2ulz.
 *
 * Why the lane asks at all.  `ARM32_CC` is
 * `clang --target=arm-none-eabi -mcpu=cortex-a7 -mfpu=vfpv4 -mfloat-abi=softfp`
 * and the link is `-nostdlib`, so there is no libgcc and no compiler-rt behind
 * these names.  VFPv4 converts between a VFP register and a *32-bit* core
 * register only: fhtod/fdtosh and their single-precision siblings.  A 64-bit
 * integer operand has no instruction in either direction, so every
 * `(double)sample_count`, `(float)tick` and `(long long)(seconds * rate)` in the
 * tree becomes a call.  The DAW is where they all live: the transport converts
 * samples to nanoseconds and back (src/media/gst_rt.c -- render_graph, chase_on,
 * click_trig, ring_apply, sound_on) and the demo engine does the same for its
 * step clock (src/demo/lilcu64_synth.c, src/demo/lilcu64_insert.c).
 *
 * The same class of gap was answered for the two IA-32 lanes and for the PS2 in
 * src/kernel/libint64.c (divide, rem) and src/drivers/ps2/ps2_builtins.c (the
 * MIPS-named soft-float set).  This file is the ARM spelling of that answer, and
 * it is deliberately ARM32-only: AArch64 -- the Pi 4B lane -- converts a 64-bit
 * integer natively, needs nothing here, and must not carry dead runtime.
 *
 * Two ABI facts make a plain C definition correct rather than merely convenient:
 *
 *   - Under `-mfloat-abi=softfp` a float argument or result rides s0 and a double
 *     rides d0, which is exactly how the ARM base ABI says these routines pass
 *     their floating operand; a long long rides (r0,r1), low word first.  So a C
 *     function of the matching C type compiles to the convention the caller
 *     already expects.  Nothing here is `__attribute__((naked))` and no register
 *     is touched by hand -- unlike the divide helpers in startup_arm.c, which are
 *     naked because `udiv`/`sdiv` have no C spelling.
 *   - A body written as `return (double)v;` calls the symbol it defines and
 *     recurses forever.  Every conversion below is therefore built from integer
 *     operations and an IEEE bit assembly, so this file references no libcall.
 *
 * The rounding law, and where it is graded:
 *   - int -> float/double: correctly rounded, round-half-to-even, computed in one
 *     step from the exact integer.  The `(float)(double)v` spelling would round
 *     twice, and a uint64 wider than 53 bits is exactly where that shows.
 *   - float/double -> integer: truncate toward zero, as C specifies for a cast.
 *     Outside the destination's range the answer saturates rather than wrapping,
 *     and a negative operand of an unsigned destination gives 0.  libgcc wraps in
 *     both cases and the VFP hardware saturates; saturating is the choice already
 *     made for the MIPS lane by __fixdfdi and __fixunssfdi in ps2_builtins.c, so
 *     the two 32-bit lanes agree, and no call site depends on the difference
 *     because each converts a time or a sample count that is in range by
 *     construction.  A saturated answer reads as a stalled transport; a wrapped
 *     one reads as a plausible timestamp.
 *
 *   make test-arm-aeabi #includes this file and throws all six against the host
 *   FPU -- the exact boundaries, every power-of-two neighbour, the subnormal band
 *   and a random sweep -- so a wrong constant fails on a desk rather than on a
 *   monitor.  `make arm-elf` is the link that needs the answer.
 */

#include <stdint.h>

/* ── The two IEEE layouts ─────────────────────────────────────────────────── */

/*
 * mb = mantissa bits, sw = exponent field width, bias = exponent bias.  The sign
 * sits at bit mb+sw, so it is derived rather than spelled: float is 23+8 = 31 and
 * double is 52+11 = 63.
 */
#define AE_F32_MB   23u
#define AE_F32_SW    8u
#define AE_F32_BIAS 127

#define AE_F64_MB   52u
#define AE_F64_SW   11u
#define AE_F64_BIAS 1023

/* ── integer -> IEEE bits ─────────────────────────────────────────────────── */

/* Index of the most significant set bit of a non-zero word.  Shifts and compares
 * only, so it costs no libcall on a 32-bit register file. */
static uint32_t ae_msb(uint64_t v)
{
    uint32_t p = 0u;
    if (v >> 32u) { p += 32u; v >>= 32u; }
    if (v >> 16u) { p += 16u; v >>= 16u; }
    if (v >>  8u) { p +=  8u; v >>=  8u; }
    if (v >>  4u) { p +=  4u; v >>=  4u; }
    if (v >>  2u) { p +=  2u; v >>=  2u; }
    if (v >>  1u) { p +=  1u; }
    return p;
}

/*
 * The round-to-nearest-even bits of the unsigned value `v` in the format
 * (mb, bias).  One step, with no intermediate in the other precision.
 *
 * A 64-bit unsigned operand never overflows either format -- UINT64_MAX is about
 * 1.8e19, far below float's 3.4e38 -- so the exponent field cannot reach its
 * all-ones value and there is no overflow case to write.
 */
static uint64_t ae_bits_of_u64(uint64_t v, unsigned mb, int bias)
{
    if (v == 0u) return 0u;

    uint32_t p = ae_msb(v);
    if (p <= mb) {
        /* Exactly representable.  The implicit 1 sits at bit p and the low p bits
         * are the fraction -- and the fraction field is left-justified at bit mb,
         * so those p bits travel up by mb-p before they go into the word. */
        const uint64_t frac = (v ^ (UINT64_C(1) << p)) << (mb - p);
        return (((uint64_t)((int32_t)p + bias)) << mb) | frac;
    }

    const uint32_t shift = p - mb;                       /* 1 .. 40 */
    uint64_t keep = v >> shift;                          /* mb+1 bits, msb set */
    const uint64_t rem  = v & ((UINT64_C(1) << shift) - 1u);
    const uint64_t half = UINT64_C(1) << (shift - 1u);

    if (rem > half || (rem == half && (keep & 1u) != 0u)) keep++;
    if ((keep >> (mb + 1u)) != 0u) {                     /* rounding carried up a binade */
        keep >>= 1u;
        p++;
    }
    return (((uint64_t)((int32_t)p + bias)) << mb) | (keep & ((UINT64_C(1) << mb) - 1u));
}

/* |v| without negating a minimum, which is not representable as a signed value. */
static uint64_t ae_mag_of_i64(long long v)
{
    return (v < 0) ? (uint64_t)0u - (uint64_t)v : (uint64_t)v;
}

static double ae_double_of_i64(long long v)
{
    union { uint64_t u; double d; } bits;
    bits.u = ae_bits_of_u64(ae_mag_of_i64(v), AE_F64_MB, AE_F64_BIAS);
    if (v < 0) bits.u |= (UINT64_C(1) << (AE_F64_MB + AE_F64_SW));
    return bits.d;
}

static float ae_float_of_i64(long long v)
{
    union { uint64_t u; float f; } bits;
    bits.u = ae_bits_of_u64(ae_mag_of_i64(v), AE_F32_MB, AE_F32_BIAS);
    if (v < 0) bits.u |= (UINT64_C(1) << (AE_F32_MB + AE_F32_SW));
    return bits.f;
}

/* ── IEEE bits -> integer magnitude ────────────────────────────────────────── */

enum {
    AE_TRUNC_OK = 0,      /* *mag holds the truncated magnitude */
    AE_TRUNC_INF,         /* infinite */
    AE_TRUNC_NAN,
    AE_TRUNC_BIG          /* finite, but at or above 2^64: no 64-bit answer fits */
};

/*
 * Decode a bit pattern of either width and truncate toward zero, returning the
 * magnitude.  A zero or subnormal operand is smaller than one in every case, so
 * it truncates to 0 with no further work.  A normal operand is (1.mant) * 2^e,
 * which is a shift one way or the other; because the significand holds mb+1
 * bits, shifting left by e-mb can never pass bit 63 while e < 64, so the
 * truncated magnitude is exact in 64 bits.
 */
static int ae_trunc_bits(uint64_t bits, unsigned mb, unsigned sw, int bias,
                         uint32_t *sign, uint64_t *mag)
{
    const uint64_t exp_all = (UINT64_C(1) << sw) - 1u;
    const uint64_t mant_mask = (UINT64_C(1) << mb) - 1u;
    const uint64_t exp_field = (bits >> mb) & exp_all;

    *sign = (uint32_t)(bits >> (mb + sw));
    *mag = 0u;

    if (exp_field == exp_all) return (bits & mant_mask) ? AE_TRUNC_NAN : AE_TRUNC_INF;
    if (exp_field == 0u) return AE_TRUNC_OK;             /* zero or subnormal: < 1 */

    const int e = (int)exp_field - bias;
    const uint64_t mant = (bits & mant_mask) | (UINT64_C(1) << mb);

    if (e >= 64) return AE_TRUNC_BIG;
    if (e >= (int)mb) {
        *mag = mant << (uint32_t)(e - (int)mb);
    } else {
        const unsigned drop = (unsigned)((int)mb - e);    /* 1 .. 52 */
        *mag = (drop >= 64u) ? 0u : (mant >> drop);
    }
    return AE_TRUNC_OK;
}

#define AE_I64_MAX_MAG UINT64_C(0x7FFFFFFFFFFFFFFF)      /*  2^63 - 1 */
#define AE_I64_MIN_MAG UINT64_C(0x8000000000000000)      /*  2^63     */

static long long ae_i64_of_trunc(uint32_t sign, uint64_t mag, int cls)
{
    if (cls == AE_TRUNC_NAN) return 0LL;
    if (sign) {
        if (cls != AE_TRUNC_OK || mag > AE_I64_MIN_MAG) return (long long)(~AE_I64_MAX_MAG);
        return (long long)((uint64_t)0u - mag);           /* mag == 2^63 lands on INT64_MIN */
    }
    if (cls != AE_TRUNC_OK || mag > AE_I64_MAX_MAG) return (long long)AE_I64_MAX_MAG;
    return (long long)mag;
}

static uint64_t ae_u64_of_trunc(uint32_t sign, uint64_t mag, int cls)
{
    /* A negative operand of an unsigned destination answers 0 -- the law
     * __fixunssfdi already sets for the MIPS lane. */
    if (cls == AE_TRUNC_NAN || sign) return 0u;
    if (cls != AE_TRUNC_OK) return UINT64_MAX;
    return mag;
}

/* ── The six libcalls ─────────────────────────────────────────────────────── */

double __aeabi_l2d(long long v)   { return ae_double_of_i64(v); }
float  __aeabi_l2f(long long v)   { return ae_float_of_i64(v); }

double __aeabi_ul2d(unsigned long long v)
{
    union { uint64_t u; double d; } bits;
    bits.u = ae_bits_of_u64((uint64_t)v, AE_F64_MB, AE_F64_BIAS);
    return bits.d;
}

float __aeabi_ul2f(unsigned long long v)
{
    union { uint64_t u; float f; } bits;
    bits.u = ae_bits_of_u64((uint64_t)v, AE_F32_MB, AE_F32_BIAS);
    return bits.f;
}

long long __aeabi_d2lz(double d)
{
    union { double d; uint64_t u; } bits;
    uint32_t sign = 0u;
    uint64_t mag = 0u;
    int cls;
    bits.d = d;
    /* Three separate statements, not one nested call: C leaves the order in which
     * arguments are read unspecified, so passing `sign` and `mag` in the same
     * expression that fills them through a pointer would let a compiler hand over
     * the values from before the decode. */
    cls = ae_trunc_bits(bits.u, AE_F64_MB, AE_F64_SW, AE_F64_BIAS, &sign, &mag);
    return ae_i64_of_trunc(sign, mag, cls);
}

unsigned long long __aeabi_f2ulz(float f)
{
    union { uint32_t u; float f; } bits;
    uint32_t sign = 0u;
    uint64_t mag = 0u;
    int cls;
    bits.f = f;
    cls = ae_trunc_bits((uint64_t)bits.u, AE_F32_MB, AE_F32_SW, AE_F32_BIAS, &sign, &mag);
    return (unsigned long long)ae_u64_of_trunc(sign, mag, cls);
}
