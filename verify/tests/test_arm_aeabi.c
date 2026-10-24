/*
 * test_arm_aeabi.c - does src/kernel/libaeabi.c match the host FPU, bit for bit?
 *
 * The ARM32 bare-metal image is linked -nostdlib, and VFPv4 has no instruction
 * that converts a 64-bit integer to or from a floating register.  clang therefore
 * invents the names __aeabi_l2d, __aeabi_ul2d, __aeabi_l2f, __aeabi_ul2f,
 * __aeabi_d2lz and __aeabi_f2ulz at the DAW's sample/nanosecond arithmetic and the
 * link has nothing to answer them with until this file's subject exists.
 *
 * Nothing else in the tree can see those symbols -- they are not called by name
 * anywhere -- so the runtime is compiled into this program and thrown against the
 * host's own casts, comparing IEEE-754 bit patterns exactly, the way
 * test_ps2_softfloat.c grades the MIPS spelling of the same job.  The inputs are
 * chosen to make a rounding rule fail if it is wrong: every power of two and its
 * neighbours (that is where round-half-to-even shows), the integers just below and
 * above each 24- and 53-bit boundary, the subnormal band, NaN and infinity, then a
 * random sweep whose exponent is itself random so the mantissa is exercised across
 * every binade.
 *
 * The out-of-range and negative cases assert the documented law -- saturate, and
 * 0 for a negative operand of an unsigned destination -- rather than libgcc's
 * wrap, because that law is what the PS2 lane already sets and what a call site
 * that overflows should produce: a stalled transport, not a plausible timestamp.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>
#include <float.h>

/* The runtime's source, not a link against it: the helpers are file-static, and on
 * a host the __aeabi_* names would otherwise be resolved by compiler-rt, which
 * would grade its own code instead of ours. */
#include "../../src/kernel/libaeabi.c"

static int s_checks;
static int s_failures;
static unsigned long long s_state = 0x9E3779B97F4A7C15ULL;

static uint64_t rnd64(void)
{
    s_state ^= s_state << 13;
    s_state ^= s_state >> 7;
    s_state ^= s_state << 17;
    return s_state;
}

static uint32_t rnd32(void) { return (uint32_t)(rnd64() >> 32); }

static uint64_t d_bits(double d) { uint64_t u; memcpy(&u, &d, sizeof u); return u; }
static uint32_t f_bits(float f)  { uint32_t u; memcpy(&u, &f, sizeof u); return u; }

static void check(const char *what, uint64_t in, uint64_t got, uint64_t want)
{
    s_checks++;
    if (got == want) return;
    if (s_failures < 12) {
        printf("  FAIL %-18s in=%016llx got=%016llx want=%016llx\n",
               what, (unsigned long long)in, (unsigned long long)got,
               (unsigned long long)want);
    }
    s_failures++;
}

/* ── int -> float / double ────────────────────────────────────────────────── */

static void to_float(void)
{
    unsigned e;
    int k;

    /* Each power of two and the two values either side of it: a binade boundary is
     * where an off-by-one in the exponent assembly or a lost carry lands.  The
     * negated forms are skipped at 2^63 because -(long long)LLONG_MIN is undefined
     * behaviour; that magnitude is covered on its own below. */
    for (e = 0u; e < 64u; e++) {
        for (k = -1; k <= 1; k++) {
            const uint64_t v = (UINT64_C(1) << e) + (uint64_t)k;
            const long long s = (long long)v;
            check("ul2d", v, d_bits(__aeabi_ul2d(v)), d_bits((double)v));
            check("ul2f", v, (uint64_t)f_bits(__aeabi_ul2f(v)), (uint64_t)f_bits((float)v));
            check("l2d", v, d_bits(__aeabi_l2d(s)), d_bits((double)s));
            check("l2f", v, (uint64_t)f_bits(__aeabi_l2f(s)), (uint64_t)f_bits((float)s));
            if (s != LLONG_MIN) {
                check("l2d neg", v, d_bits(__aeabi_l2d(-s)), d_bits((double)(-s)));
                check("l2f neg", v, (uint64_t)f_bits(__aeabi_l2f(-s)), (uint64_t)f_bits((float)(-s)));
            }
        }
    }

    /* The largest values each width can hold exactly, and the first that cannot:
     * 2^24/2^53 for float/double, plus the ties just above them. */
    {
        const uint64_t edges[] = {
            0u, 1u, 0xFFFFFFu, 0x1000000u, 0x1000001u, 0x1000002u, 0x1000003u,
            0x1677777u, 0x1777777u, 0xFFFFFFFEu, 0xFFFFFFFFu, 0x100000000u,
            UINT64_C(0xFFFFFFFFFFFFF), UINT64_C(0x10000000000000),
            UINT64_C(0x10000000000001), UINT64_C(0xFFFFFFFFFFFFFFFF),
            UINT64_C(0x7FFFFFFFFFFFFFFF), UINT64_C(0x8000000000000000)
        };
        size_t i;
        for (i = 0; i < sizeof edges / sizeof edges[0]; i++) {
            const uint64_t v = edges[i];
            const long long s = (long long)v;
            check("ul2d edge", v, d_bits(__aeabi_ul2d(v)), d_bits((double)v));
            check("ul2f edge", v, (uint64_t)f_bits(__aeabi_ul2f(v)), (uint64_t)f_bits((float)v));
            check("l2d edge", v, d_bits(__aeabi_l2d(s)), d_bits((double)s));
            check("l2f edge", v, (uint64_t)f_bits(__aeabi_l2f(s)), (uint64_t)f_bits((float)s));
            if (s != LLONG_MIN) {
                check("l2d edge neg", v, d_bits(__aeabi_l2d(-s)), d_bits((double)(-s)));
            }
        }
        /* INT64_MIN is the one magnitude no negation can produce, so it goes through
         * the unsigned-subtraction path on its own. */
        check("l2d INT64_MIN", 0u, d_bits(__aeabi_l2d(LLONG_MIN)), d_bits((double)LLONG_MIN));
        check("l2f INT64_MIN", 0u, (uint64_t)f_bits(__aeabi_l2f(LLONG_MIN)),
              (uint64_t)f_bits((float)LLONG_MIN));
    }

    /* A random sweep with a random shift, so the mantissa is exercised at every
     * width rather than only near the extremes. */
    for (k = 0; k < 500000; k++) {
        const uint64_t v = rnd64() >> (rnd32() % 65u);
        const long long s = (long long)v;
        check("ul2d rnd", v, d_bits(__aeabi_ul2d(v)), d_bits((double)v));
        check("ul2f rnd", v, (uint64_t)f_bits(__aeabi_ul2f(v)), (uint64_t)f_bits((float)v));
        check("l2d rnd", v, d_bits(__aeabi_l2d(s)), d_bits((double)s));
        check("l2f rnd", v, (uint64_t)f_bits(__aeabi_l2f(s)), (uint64_t)f_bits((float)s));
    }
}

/* ── float / double -> int ────────────────────────────────────────────────── */

static void from_float(void)
{
    unsigned e;
    int k;

    /* Integer-valued operands first: the answer is the value itself, and a shift
     * that is one off in either direction shows immediately. */
    for (e = 0u; e < 63u; e++) {
        const double p = ldexp(1.0, (int)e);
        const float q = ldexpf(1.0f, (int)e);
        check("d2lz 2^e", d_bits(p), (uint64_t)__aeabi_d2lz(p), (uint64_t)(long long)p);
        check("d2lz -2^e", d_bits(-p), (uint64_t)__aeabi_d2lz(-p), (uint64_t)(long long)(-p));
        check("d2lz 2^e-1", d_bits(p - 1.0), (uint64_t)__aeabi_d2lz(p - 1.0),
              (uint64_t)(long long)(p - 1.0));
        check("d2lz 2^e+0.5", d_bits(p + 0.5), (uint64_t)__aeabi_d2lz(p + 0.5),
              (uint64_t)(long long)(p + 0.5));
        check("d2lz -2^e-0.5", d_bits(-(p + 0.5)), (uint64_t)__aeabi_d2lz(-(p + 0.5)),
              (uint64_t)(long long)(-(p + 0.5)));
        if (e < 64u) {
            check("f2ulz 2^e", (uint64_t)f_bits(q), __aeabi_f2ulz(q), (uint64_t)(float)q);
        }
    }

    /* Below one, and in the subnormal band, everything truncates to zero. */
    check("d2lz 1/3", 0u, (uint64_t)__aeabi_d2lz(1.0 / 3.0), 0u);
    check("d2lz -1/3", 0u, (uint64_t)__aeabi_d2lz(-1.0 / 3.0), 0u);
    check("d2lz DBL_MIN", 0u, (uint64_t)__aeabi_d2lz(DBL_MIN), 0u);
    check("d2lz -DBL_MIN", 0u, (uint64_t)__aeabi_d2lz(-DBL_MIN), 0u);
    check("d2lz subnormal", 0u, (uint64_t)__aeabi_d2lz(DBL_MIN / 2.0), 0u);
    check("f2ulz 1/3", 0u, __aeabi_f2ulz(1.0f / 3.0f), 0u);
    check("f2ulz subnormal", 0u, __aeabi_f2ulz(FLT_MIN / 2.0f), 0u);
    check("d2lz +0", 0u, (uint64_t)__aeabi_d2lz(0.0), 0u);
    check("d2lz -0", 0u, (uint64_t)__aeabi_d2lz(-0.0), 0u);
    check("f2ulz -0", 0u, __aeabi_f2ulz(-0.0f), 0u);

    /* The documented out-of-range law. */
    check("d2lz >INT64_MAX", 0u, (uint64_t)__aeabi_d2lz(9.223372036854775807e18),
          (uint64_t)LLONG_MAX);
    check("d2lz <-INT64_MIN", 0u, (uint64_t)__aeabi_d2lz(-9.223372036854775809e18),
          (uint64_t)(long long)LLONG_MIN);
    check("d2lz +inf", 0u, (uint64_t)__aeabi_d2lz(INFINITY), (uint64_t)LLONG_MAX);
    check("d2lz -inf", 0u, (uint64_t)__aeabi_d2lz(-INFINITY), (uint64_t)(long long)LLONG_MIN);
    check("d2lz nan", 0u, (uint64_t)__aeabi_d2lz(NAN), 0u);
    check("f2ulz neg", 0u, __aeabi_f2ulz(-1.0f), 0u);
    check("f2ulz +inf", 0u, __aeabi_f2ulz(INFINITY), UINT64_MAX);
    check("f2ulz nan", 0u, __aeabi_f2ulz(NAN), 0u);

    /* 2^63 is above INT64_MAX, so a positive operand saturates upward; its negative
     * is exactly representable and answers INT64_MIN, the one magnitude no
     * negation of a signed value can produce. */
    check("d2lz 2^63", 0u, (uint64_t)__aeabi_d2lz(9223372036854775808.0),
          (uint64_t)LLONG_MAX);
    check("d2lz -2^63", 0u, (uint64_t)__aeabi_d2lz(-9223372036854775808.0),
          (uint64_t)(long long)LLONG_MIN);

    /* Random finite bit patterns whose exponent spans both the in-range and the
     * saturating band: field 900..1119 is 2^-123 to 2^97, and no pattern reaches
     * the all-ones field, so neither infinity nor NaN appears by accident. */
    for (k = 0; k < 500000; k++) {
        const unsigned field = 900u + (rnd32() % 220u);
        uint64_t bits = ((uint64_t)field << 52)
                      | (rnd64() & UINT64_C(0x000FFFFFFFFFFFFF))
                      | ((rnd32() & 1u) ? UINT64_C(0x8000000000000000) : 0u);
        double d;
        float q;
        memcpy(&d, &bits, sizeof d);
        if (d < 9.223372036854775808e18 && d > -9.223372036854775808e18) {
            check("d2lz rnd", bits, (uint64_t)__aeabi_d2lz(d), (uint64_t)(long long)d);
        } else {
            check("d2lz rnd sat", bits, (uint64_t)__aeabi_d2lz(d),
                  (uint64_t)(long long)(d > 0.0 ? LLONG_MAX : LLONG_MIN));
        }
        q = (float)d;
        if (q >= 0.0f && q < 18446744073709551616.0f) {
            check("f2ulz rnd", (uint64_t)f_bits(q), __aeabi_f2ulz(q), (uint64_t)q);
        } else if (q > 0.0f) {
            check("f2ulz rnd sat", (uint64_t)f_bits(q), __aeabi_f2ulz(q), UINT64_MAX);
        } else {
            check("f2ulz rnd neg", (uint64_t)f_bits(q), __aeabi_f2ulz(q), 0u);
        }
    }
}

int main(void)
{
    printf("==========================================================\n");
    printf(" ARM EABI conversion runtime vs the host FPU (bit-exact)\n");
    printf("==========================================================\n");

    to_float();
    from_float();

    printf("  checks=%d failures=%d\n", s_checks, s_failures);
    if (s_failures) {
        printf("[CI-TEST] FAIL - libaeabi.c disagrees with IEEE-754\n");
        return 1;
    }
    printf("[CI-TEST] SUCCESS - all six __aeabi_* conversions match the host\n");
    return 0;
}
