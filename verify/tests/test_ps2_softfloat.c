/*
 * test_ps2_softfloat.c - is the PS2 port's hand-written FP runtime really IEEE-754?
 *
 * The PS2 image is built -msoft-float because the Emotion Engine's FPU has no
 * double-precision instructions at all, which moves every floating-point
 * operation onto src/drivers/ps2/ps2_builtins.c.  Nothing else in the tree
 * exercises that file: it is reached only through names the compiler invents, so
 * a bug in it shows up as a black window on a real PS2 and as nothing at all on a
 * host.  This is the instrument: the same source is compiled into this program and
 * every one of its 32 symbols is thrown against the host's own hardware FPU on
 * millions of inputs, comparing the returned IEEE-754 bit pattern exactly.
 *
 * The compares are checked the way the generated MIPS code checks them -- the four
 * caller tests measured out of a disassembly of -msoft-float (`< 0` for lt, `< 1`
 * for le, `> -1` for ge, `> 0` for gt, `== 0` / `!= 0` for eq/ne) -- because a
 * compare routine that returns the mathematically right answer in the wrong sign
 * still breaks the renderer.
 *
 * Two assertions are class-level rather than bit-level, and say so: the payload
 * bits a NaN comes back with are a per-CPU policy choice, so a NaN result is
 * checked for being a NaN with the right sign only.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

/* The runtime's own source, not a link against it: its format table and engine are
 * file-static, and only the definitions of the 32 libcalls are exported. */
#include "../../src/drivers/ps2/ps2_builtins.c"

static uint64_t s_state = 0x20261009ABCDEFull;

static uint32_t rnd32(void)
{
    s_state = s_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (uint32_t)(s_state >> 33);
}

static int s_failures;
static int s_checks;
static int s_shown;

static void fail(const char *what, uint64_t a, uint64_t b, uint64_t got, uint64_t want)
{
    s_failures++;
    if (s_shown < 12) {
        s_shown++;
        printf("  [FAIL] %-14s a=0x%016llx b=0x%016llx got=0x%016llx want=0x%016llx\n",
               what, (unsigned long long)a, (unsigned long long)b,
               (unsigned long long)got, (unsigned long long)want);
    }
}

static void check(const char *what, uint64_t a, uint64_t b, uint64_t got, uint64_t want)
{
    s_checks++;
    if (got != want) fail(what, a, b, got, want);
}

/* A NaN result is the one place where "same bits" is the wrong question: which
 * operand's payload survives is a property of the FPU, not of IEEE-754. */
static int nan_ok(uint64_t got, uint64_t want, uint32_t sign_shift, uint32_t exp_max,
                  uint32_t frac_bits, uint64_t mag_mask)
{
    const uint64_t eg = got >> frac_bits, ew = want >> frac_bits;
    if (((eg & exp_max) != exp_max || !(got & mag_mask)) ||
        ((ew & exp_max) != exp_max || !(want & mag_mask))) return 0;
    if ((got >> sign_shift) != (want >> sign_shift)) return 0;   /* both signless */
    return 1;
}

static uint64_t d_bits(double v)   { uint64_t u; memcpy(&u, &v, sizeof u); return u; }
static uint32_t f_bits(float v)    { uint32_t u; memcpy(&u, &v, sizeof u); return u; }
static double d_val(uint64_t u)    { double v; memcpy(&v, &u, sizeof v); return v; }
static float  f_val(uint32_t u)    { float v; memcpy(&v, &u, sizeof v); return v; }

/* Every special value the port is likely to meet, plus the corners of both formats. */
static const double d_pool_init[] = {
    0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 2.0, -2.0, 3.0, 1.0 / 3.0, 2.0 / 3.0,
    1e308, -1e308, 1e-308, -1e-308, 5e-324, -5e-324, 2.2250738585072014e-308,
    4.9406564584124654e-324, 1.7976931348623157e308, 9007199254740992.0,
    9007199254740991.0, 1.0000000000000002, 0x1.0000000000001p+0,
    6.666666666666667e-11, 1.0e-23, 123456.789, -123456.789,
};
#define D_POOL_EXTRA 4
static double g_dpool[sizeof(d_pool_init) / sizeof(d_pool_init[0]) + D_POOL_EXTRA];
static int g_dn;

static const float f_pool_init[] = {
    0.0f, -0.0f, 1.0f, -1.0f, 0.5f, -0.5f, 2.0f, -2.0f, 3.0f, 1.0f / 3.0f,
    3.40282347e+38f, -3.40282347e+38f, 1.17549435e-38f, 1.40129846e-45f,
    -1.40129846e-45f, 1.0e-30f, 1.0e+30f, 16777216.0f, 16777217.0f, 0.1f,
};
#define F_POOL_EXTRA 4
static float g_fpool[sizeof(f_pool_init) / sizeof(f_pool_init[0]) + F_POOL_EXTRA];
static int g_fn;

static void pools_init(void)
{
    int i;
    double d = 1.5;
    float fd = 1.5f;

    (void)i; (void)d; (void)fd;
    g_dn = (int)(sizeof(d_pool_init) / sizeof(d_pool_init[0]));
    memcpy(g_dpool, d_pool_init, sizeof(d_pool_init));
    {   /* +-inf and NaN live in the pool too; the bit patterns say what they are */
        const uint64_t inf = 0x7FF0000000000000ULL, nan = 0x7FF8000000000000ULL;
        const uint64_t nanp = 0x7FF4000000000000ULL;   /* a signaling payload, quieted below */
        g_dpool[g_dn++] = d_val(inf);
        g_dpool[g_dn++] = d_val(-inf);
        g_dpool[g_dn++] = d_val(nan);
        g_dpool[g_dn++] = d_val(nanp);
    }

    g_fn = (int)(sizeof(f_pool_init) / sizeof(f_pool_init[0]));
    memcpy(g_fpool, f_pool_init, sizeof(f_pool_init));
    {
        uint32_t inf = 0x7F800000u, nan = 0x7FC00000u, nanp = 0x7E000000u;
        g_fpool[g_fn++] = f_val(inf);
        g_fpool[g_fn++] = f_val(-inf);
        g_fpool[g_fn++] = f_val(nan);
        g_fpool[g_fn++] = f_val(nanp);
    }
}

static uint64_t rnd_double(void)
{
    if ((rnd32() & 7u) == 0u) return d_bits(g_dpool[rnd32() % (uint32_t)g_dn]);
    /* Uniform exponent, random mantissa: the specials and the subnormals are hit by
     * the pool, this half covers the whole normal range instead of a corner of it. */
    return (((uint64_t)(rnd32() & 0x7FFu) << 52) |
            ((((uint64_t)rnd32() << 20) | rnd32()) & 0xFFFFFFFFFFFFFULL));
}

static uint32_t rnd_float(void)
{
    if ((rnd32() & 7u) == 0u) return f_bits(g_fpool[rnd32() % (uint32_t)g_fn]);
    return ((rnd32() & 0xFFu) << 23) | (rnd32() & 0x7FFFFFu);
}

#define IS_NAN_D(u) ((((u) >> 52) & 0x7FFu) == 0x7FFu && ((u) & 0xFFFFFFFFFFFFFULL) != 0)
#define IS_NAN_F(u) (((u) >> 23 & 0xFFu) == 0xFFu && ((u) & 0x7FFFFFu) != 0)

static void doubles_one_round(int round)
{
    const int n = round ? 300000 : 20000;
    int i;

    for (i = 0; i < n; i++) {
        const uint64_t a = rnd_double(), b = rnd_double();
        const double x = d_val(a), y = d_val(b);
        uint64_t got, want;

        got = __adddf3(a, b); want = d_bits(x + y);
        if (IS_NAN_D(want)) { s_checks++; if (!nan_ok(got, want, 63, 0x7FFu, 52, 0xFFFFFFFFFFFFFULL)) fail("adddf3", a, b, got, want); }
        else check("adddf3", a, b, got, want);

        got = __subdf3(a, b); want = d_bits(x - y);
        if (IS_NAN_D(want)) { s_checks++; if (!nan_ok(got, want, 63, 0x7FFu, 52, 0xFFFFFFFFFFFFFULL)) fail("subdf3", a, b, got, want); }
        else check("subdf3", a, b, got, want);

        got = __muldf3(a, b); want = d_bits(x * y);
        if (IS_NAN_D(want)) { s_checks++; if (!nan_ok(got, want, 63, 0x7FFu, 52, 0xFFFFFFFFFFFFFULL)) fail("muldf3", a, b, got, want); }
        else check("muldf3", a, b, got, want);

        got = __divdf3(a, b); want = d_bits(x / y);
        if (IS_NAN_D(want)) { s_checks++; if (!nan_ok(got, want, 63, 0x7FFu, 52, 0xFFFFFFFFFFFFFULL)) fail("divdf3", a, b, got, want); }
        else check("divdf3", a, b, got, want);

        /* The caller's test, not the mathematical relation: see the note above. */
        check("ltdf2<0",  a, b, (__ltdf2(a, b)  < 0), (x <  y));
        check("ledf2<1",  a, b, (__ledf2(a, b)  < 1), (x <= y));
        check("gtdf2>0",  a, b, (__gtdf2(a, b)  > 0), (x >  y));
        check("gedf2>-1", a, b, (__gedf2(a, b)  > -1), (x >= y));
        check("eqdf2==0", a, b, (__eqdf2(a, b)  == 0), (x == y));
        check("nedf2!=0", a, b, (__nedf2(a, b)  != 0), (x != y));
        check("unorddf2", a, b, (__unorddf2(a, b) != 0), (x != x || y != y));
    }
}

static void floats_one_round(int round)
{
    const int n = round ? 300000 : 20000;
    int i;

    for (i = 0; i < n; i++) {
        const uint32_t a = rnd_float(), b = rnd_float();
        const float x = f_val(a), y = f_val(b);
        uint32_t got, want;

        got = __addsf3(a, b); want = f_bits(x + y);
        if (IS_NAN_F(want)) { s_checks++; if (!nan_ok(got, want, 31, 0xFFu, 23, 0x7FFFFFu)) fail("addsf3", a, b, got, want); }
        else check("addsf3", a, b, got, want);

        got = __subsf3(a, b); want = f_bits(x - y);
        if (IS_NAN_F(want)) { s_checks++; if (!nan_ok(got, want, 31, 0xFFu, 23, 0x7FFFFFu)) fail("subsf3", a, b, got, want); }
        else check("subsf3", a, b, got, want);

        got = __mulsf3(a, b); want = f_bits(x * y);
        if (IS_NAN_F(want)) { s_checks++; if (!nan_ok(got, want, 31, 0xFFu, 23, 0x7FFFFFu)) fail("mulsf3", a, b, got, want); }
        else check("mulsf3", a, b, got, want);

        got = __divsf3(a, b); want = f_bits(x / y);
        if (IS_NAN_F(want)) { s_checks++; if (!nan_ok(got, want, 31, 0xFFu, 23, 0x7FFFFFu)) fail("divsf3", a, b, got, want); }
        else check("divsf3", a, b, got, want);

        check("ltsf2<0",  a, b, (__ltsf2(a, b)  < 0), (x <  y));
        check("lesf2<1",  a, b, (__lesf2(a, b)  < 1), (x <= y));
        check("gtsf2>0",  a, b, (__gtsf2(a, b)  > 0), (x >  y));
        check("gesf2>-1", a, b, (__gesf2(a, b)  > -1), (x >= y));
        check("eqsf2==0", a, b, (__eqsf2(a, b)  == 0), (x == y));
        check("nesf2!=0", a, b, (__nesf2(a, b)  != 0), (x != y));
    }
}

/* Conversions: the ranges that hold are compared exactly, the ones that do not are
 * compared against this runtime's own law -- saturate, and NaN becomes 0 -- which is
 * what the EE's cvt instructions do and what the old hand-written pair did. */
static void conversions(void)
{
    int i;

    for (i = 0; i < 200000; i++) {
        const uint64_t a = rnd_double();
        const double x = d_val(a);

        check("truncdfsf2", a, 0, __truncdfsf2(a), f_bits((float)x));

        if (x == x && x >= -2147483648.0 && x <= 2147483647.0)
            check("fixdfsi", a, 0, (uint32_t)__fixdfsi(a), (uint32_t)(int32_t)x);
        else if (!(x >= -2147483648.0 && x <= 2147483647.0) && x == x) {
            const int32_t want = x > 0 ? 0x7FFFFFFF : (int32_t)0x80000000;
            check("fixdfsi sat", a, 0, (uint32_t)__fixdfsi(a), (uint32_t)want);
        } else check("fixdfsi nan", a, 0, (uint32_t)__fixdfsi(a), 0u);

        if (x == x && x >= -9223372036854775808.0 && x <= 9223372036854775807.0)
            check("fixdfdi", a, 0, __fixdfdi(a), (uint64_t)(int64_t)x);
        else if (x == x) {
            const int64_t want = (x < 0.0) ? (int64_t)0x8000000000000000LL : 0x7FFFFFFFFFFFFFFFULL;
            check("fixdfdi sat", a, 0, (uint64_t)__fixdfdi(a), (uint64_t)want);
        } else check("fixdfdi nan", a, 0, __fixdfdi(a), 0);

        {   const uint32_t s = rnd_float();
            check("extendsfdf2", s, 0, __extendsfdf2(s), d_bits((double)f_val(s)));
            if (f_val(s) == f_val(s) && f_val(s) >= -2147483648.0f && f_val(s) <= 2147483647.0f)
                check("fixsfsi", s, 0, (uint32_t)__fixsfsi(s), (uint32_t)(int32_t)f_val(s));
            else if (f_val(s) == f_val(s)) {
                const int32_t want = f_val(s) > 0 ? 0x7FFFFFFF : (int32_t)0x80000000;
                check("fixsfsi sat", s, 0, (uint32_t)__fixsfsi(s), (uint32_t)want);
            } else check("fixsfsi nan", s, 0, __fixsfsi(s), 0u);

            const float fv = f_val(s);
            if (fv == fv && fv >= 0.0f && fv <= 4294967295.0f)
                check("fixunssfsi", s, 0, __fixunssfsi(s), (uint32_t)fv);
            else if (fv == fv)
                check("fixunssfsi sat", s, 0, __fixunssfsi(s), (fv < 0.0f) ? 0u : 0xFFFFFFFFu);
            else check("fixunssfsi nan", s, 0, __fixunssfsi(s), 0u);
        }
    }

    for (i = 0; i < 200000; i++) {
        const int32_t i32 = (int32_t)((rnd32() << 16) ^ rnd32());
        const uint32_t u32 = (rnd32() << 16) ^ rnd32();
        const int64_t i64 = ((int64_t)u32 << 32) | (uint64_t)(uint32_t)i32;

        check("floatsidf", (uint64_t)(uint32_t)i32, 0, __floatsidf(i32), d_bits((double)i32));
        check("floatunsidf", u32, 0, __floatunsidf(u32), d_bits((double)u32));
        check("floatsisf", (uint64_t)(uint32_t)i32, 0, __floatsisf(i32), f_bits((float)i32));
        check("floatunsisf", u32, 0, __floatunsisf(u32), f_bits((float)u32));
        check("floatdidf", (uint64_t)i64, 0, __floatdidf(i64), d_bits((double)i64));
    }

    /* Powers of two across the whole exponent range of both formats, in both
     * signs: this is where a subnormal boundary or an exponent rebias that is off
     * by one shows up first, and none of it is reachable by random mantissas. */
    {
        int e;
        for (e = -130; e <= 130; e++) {
            const double p = ldexp(1.0, e), np = -p;
            const float q = (float)ldexp(1.0, e > 127 ? 127 : (e < -133 ? -133 : e));
            const uint64_t pb = d_bits(p), nb = d_bits(np), qb = f_bits(q);
            check("add 2^e", pb, nb, __adddf3(pb, nb), 0);            /* exact cancel */
            check("mul 2^e", pb, pb, __muldf3(pb, pb), d_bits(p * p));
            check("div 2^e", pb, nb, __divdf3(pb, nb), d_bits(-1.0));
            check("trunc 2^e", pb, 0, __truncdfsf2(pb), f_bits((float)p));
            check("extend -f", qb, 0, __extendsfdf2(qb), d_bits((double)q));
            check("addf 2^e", qb, qb, __addsf3(qb, qb), f_bits(q + q));
            check("mulf 2^e", qb, qb, __mulsf3(qb, qb), f_bits(q * q));
            check("divf 2^e", qb, qb, __divsf3(qb, qb), f_bits(1.0f));
        }
    }
}

/* The known-exact spot checks that would catch a whole-opposite-sign bug even if
 * the random inputs missed it: the ties-to-even corners, the subnormal band, the
 * overflow and the special algebra, each stated as the bits they must produce. */
static void known_answers(void)
{
    struct dcase { int op; double a, b, want; const char *what; };   /* 0 + 1 * 2 / */
    static const struct dcase cases[] = {
        { 0, 1.0, 1.0, 2.0, "1+1" },
        { 0, 0.1, 0.2, 0.30000000000000004, "0.1+0.2" },
        { 0, 1.0, -1.0, 0.0, "cancelling to +0" },
        { 0, -1.0, 1.0, 0.0, "cancelling the other way" },
        { 0, 1e308, 1e308, 1 / 0.0, "add overflow" },
        { 0, 5e-324, 5e-324, 1e-323, "subnormal sum" },
        { 0, -5e-324, 5e-324, 0.0, "subnormal cancellation" },
        { 0, 1.0, 5e-324, 1.0, "absorbed subnormal" },
        { 0, 9007199254740992.0, 1.0, 9007199254740992.0, "tie goes to even" },
        { 0, 9007199254740994.0, 1.0, 9007199254740996.0, "tie goes to even up" },
        { 0, 1.0 / 3.0, 2.0 / 3.0, 1.0, "thirds" },
        { 1, 0.5, 0.5, 0.25, "half squared" },
        { 1, 1e-200, 1e-200, 0.0, "mul underflow" },
        { 1, 1e300, 1e300, 1 / 0.0, "mul overflow" },
        { 1, 1.0000000000000002, 1.0000000000000002, 1.0000000000000004, "near one squared" },
        { 1, 1.0 / 0.0, 0.0, 0.0 / 0.0, "inf times zero" },
        { 2, 1.0, 0.0, 1 / 0.0, "divide by zero" },
        { 2, 0.0, 0.0, 0.0 / 0.0, "zero over zero" },
        { 2, 1.0 / 3.0, 2.0, 1.0 / 6.0, "third halved" },
        { 2, 1.0, 49.0, 1.0 / 49.0, "one over 49" },
        { 2, 1.0, 3.0, 1.0 / 3.0, "one over three" },
        { 2, 1e-308, 1e308, 0.0, "deep underflow" },
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const uint64_t a = d_bits(cases[i].a), b = d_bits(cases[i].b), w = d_bits(cases[i].want);
        const uint64_t got = cases[i].op == 0 ? __adddf3(a, b)
                         : cases[i].op == 1 ? __muldf3(a, b) : __divdf3(a, b);
        if (IS_NAN_D(w)) {
            s_checks++;
            if (!nan_ok(got, w, 63, 0x7FFu, 52, 0xFFFFFFFFFFFFFULL))
                fail(cases[i].what, a, b, got, w);
        } else check(cases[i].what, a, b, got, w);
    }

    /* The four caller tests against a NaN, spelled out: every one of them has to
     * read false, and each of the three ways it can come back wrong is a
     * different bug. */
    {
        const uint64_t nan = 0x7FF8000000000000ULL, one = d_bits(1.0);
        check("lt nan",  nan, one, (__ltdf2(nan, one) < 0), 0);
        check("le nan",  nan, one, (__ledf2(nan, one) < 1), 0);
        check("gt nan",  nan, one, (__gtdf2(nan, one) > 0), 0);
        check("ge nan",  nan, one, (__gedf2(nan, one) > -1), 0);
        check("eq nan",  nan, one, (__eqdf2(nan, one) == 0), 0);
        check("ne nan",  nan, one, (__nedf2(nan, one) != 0), 1);
        check("un nan",  nan, one, (__unorddf2(nan, one) != 0), 1);
        check("lt nan2", one, nan, (__ltdf2(one, nan) < 0), 0);
        check("gt nan2", one, nan, (__gtdf2(one, nan) > 0), 0);
        check("ge nan2", one, nan, (__gedf2(one, nan) > -1), 0);
        check("lt sf",   0x7FC00000u, d_bits(1.0), (__ltsf2(0x7FC00000u, f_bits(1.0f)) < 0), 0);
        check("gt sf",   0x7FC00000u, d_bits(1.0), (__gtsf2(0x7FC00000u, f_bits(1.0f)) > 0), 0);
        check("ge sf",   f_bits(1.0f), 0x7FC00000u, (__gesf2(f_bits(1.0f), 0x7FC00000u) > -1), 0);
        check("signed zero", d_bits(-0.0), d_bits(0.0), (__eqdf2(d_bits(-0.0), d_bits(0.0)) == 0), 1);
        check("zero lt", d_bits(-0.0), d_bits(0.0), (__ltdf2(d_bits(-0.0), d_bits(0.0)) < 0), 0);
    }
}

int main(void)
{
    int round;

    pools_init();
    printf("==========================================================\n");
    printf(" PS2 soft-float runtime vs the host FPU (bit-exact IEEE-754)\n");
    printf("==========================================================\n");

    known_answers();
    conversions();
    for (round = 0; round < 3; round++) {
        doubles_one_round(round);
        floats_one_round(round);
    }

    printf("  checks=%d failures=%d\n", s_checks, s_failures);
    if (s_failures) {
        printf("[CI-TEST] FAIL - ps2_builtins.c disagrees with IEEE-754\n");
        return 1;
    }
    printf("[CI-TEST] SUCCESS - all 32 libcalls match the host FPU\n");
    return 0;
}
