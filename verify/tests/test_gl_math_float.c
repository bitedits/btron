/*
 * test_gl_math_float.c - are the float-native series in include/gl/math.h right, and
 *                        are they the thing being measured?
 *
 * Why this exists at all: the PS2 has no display during development and no libm at link
 * time, so a wrong sqrt cannot be seen and cannot be argued into correctness.  Every
 * number below is a comparison against a real libm, on a machine that has one.
 *
 * What is under test is the no-x87 half of the header -- the float-native sqrtf, sinf,
 * cosf and floorf that the Emotion Engine and the Malta MIPS run -- reached through
 * gl_math_float_port.c, which is the only TU that sees the shim.  Three things are
 * asserted about it, and the third is the one that makes the first two mean something:
 *
 *   1. sqrtf is within 5e-7 relative (~4 ulp of a float) in every one of the 253 binades
 *      from 2^-126 to 2^126, and answers +-0, negatives, +inf, NaN and subnormals the way
 *      a square root must.  Per-binade is the only useful form: a single global maximum
 *      averages away the one binade that is off by a factor of two, and a constant factor
 *      exactly one binade wide is what a truncating instead of flooring exponent halving
 *      looks like.
 *   2. sinf/cosf are within 2e-6 absolute on every region the reduction changes hands --
 *      0..pi/4, pi/4..pi/2 (the swap), pi/2..pi (the sign flip), pi..2pi, 2pi..4pi (the
 *      turn fold) and the negative half -- and floorf is bit-exact, because it indexes.
 *      Absolute, not relative, for the trig pair: sin(x) passes through zero, where a
 *      relative error is meaningless.
 *   3. The measurement is validated on purpose-broken variants of the same functions
 *      (canaries), and the run refuses to call itself a pass if every answer came back
 *      bit-identical to the oracle -- that would mean the compiler folded the series into
 *      a hardware instruction and the header was never involved.  A harness that cannot
 *      fail is not an instrument.
 *
 * The tolerances are the consumer's, not an ideal: these functions feed SDF distances and
 * coverage values in src/apps/xmb.c, where the soft edge spans 0.022 of a cell unit, so
 * four ulp on a distance of a third of a cell is 1e-8 of a pixel.  Nothing here is a claim
 * about bit-exact IEEE transcendentals, which a five-term polynomial does not make.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

int p_series_branch(void);
float p_sqrtf(float);
float p_sinf(float);
float p_cosf(float);
float p_floorf(float);

typedef float (*f1)(float);

static int s_failures;
static long s_checks;
static int s_hints;          /* printed detail is bounded; the count it came from is not */

static void fail(const char *what, const char *detail)
{
    s_failures++;
    if (s_failures <= 14)
        printf("  [FAIL] %-24s %s\n", what, detail);
}

static void hint(const char *what, const char *detail)
{
    if (s_hints++ < 14)
        printf("  [HINT] %-24s %s\n", what, detail);
}

static char buf[192];
static char buf2[192];

/* ---------------------------------------------------------------- error algebra */

/* Never itself NaN: `nan > worst` is false, so an implementation that answered NaN would
 * otherwise be recorded as the most accurate one in the sweep. */
static double relerr(double got, double want)
{
    if (isnan(got) || isnan(want)) return (isnan(got) && isnan(want)) ? 0.0 : 1.0;
    if (want == 0.0) return got == 0.0 ? 0.0 : 1.0;
    double e = (got - want) / want;
    return e < 0.0 ? -e : e;
}

static double abserr(double a, double b)
{
    if (isnan(a) || isnan(b)) return (isnan(a) && isnan(b)) ? 0.0 : 1e9;
    double e = a - b;
    return e < 0.0 ? -e : e;
}

static unsigned int u32(float f)
{
    union { float f; unsigned int u; } v;
    v.f = f;
    return v.u;
}

/* Distance in representable float steps.  One NaN and one number is the widest distance
 * there is, not an unknown one. */
static long ulps(float a, float b)
{
    if (isnan(a) || isnan(b)) return (isnan(a) && isnan(b)) ? 0L : 0x7FFFFFFFL;
    if ((u32(a) >> 31) != (u32(b) >> 31)) return 0x7FFFFFFFL;
    long d = (long)u32(a) - (long)u32(b);
    return d < 0 ? -d : d;
}

/* ---------------------------------------------------------------------- sqrtf */

#define SQRT_REL_TOL 5.0e-7   /* ~4 ulp of a float, at 2^-126 where the seed is worst */
#define TRIG_ABS_TOL 2.0e-6
/* not M_PI: the repo builds the host tests -std=c99, and a strict-ANSI math.h is under
 * no obligation to define it on every libc this tree is compiled on */
#define GL_PI 3.14159265358979323846

typedef struct {
    double worst_rel;
    int worst_binade;
    int bad_binades;
    int bad_specials;
    long differing;
    long samples;
} sqrt_scan;

/* The rows an implementation cannot get right by accident: the two zeros, whose signs
 * a bit trick loses, the two NaN answers a clipped negative fakes as a zero, the fixed
 * point at +inf, and the three magnitudes where the float path hands over to the double
 * one or has to scale the input itself. */
static int sqrt_specials(f1 fn, const char *tag)
{
    int bad = 0;
    enum { WANT_NAN, WANT_BITS, WANT_ULP };
    struct { const char *name; float in; int kind; float want; } t[] = {
        { "+0",         0.0f,            WANT_BITS,  0.0f },
        { "-0",        -0.0f,            WANT_BITS, -0.0f },
        { "-1",        -1.0f,            WANT_NAN,   0.0f },
        { "-FLT_MAX",  -3.4028235e+38f,  WANT_NAN,   0.0f },
        { "+inf",       INFINITY,        WANT_BITS,  INFINITY },
        { "NaN",        NAN,             WANT_NAN,   0.0f },
        { "FLT_MIN",    1.17549435e-38f, WANT_ULP,   0.0f },
        { "1e-40",      1.0e-40f,        WANT_ULP,   0.0f },
        { "FLT_MAX",    3.40282347e+38f, WANT_ULP,   0.0f },
    };
    for (int i = 0; i < (int)(sizeof t / sizeof t[0]); i++) {
        float got = fn(t[i].in);
        long u;
        switch (t[i].kind) {
        case WANT_NAN:
            if (!isnan(got)) {
                bad++;
                snprintf(buf2, sizeof buf2, "%s sqrt(%s) = %.9g, want NaN", tag, t[i].name, (double)got);
                hint("sqrtf special", buf2);
            }
            break;
        case WANT_BITS:
            if (u32(got) != u32(t[i].want)) {
                bad++;
                snprintf(buf2, sizeof buf2, "%s sqrt(%s) = %.9g (%08x), want %.9g (%08x)",
                         tag, t[i].name, (double)got, u32(got), (double)t[i].want, u32(t[i].want));
                hint("sqrtf special", buf2);
            }
            break;
        default:
            u = ulps(got, sqrtf(t[i].in));
            if (u > 2) {
                bad++;
                snprintf(buf2, sizeof buf2, "%s sqrt(%s) off by %ld ulp", tag, t[i].name, u);
                hint("sqrtf special", buf2);
            }
            break;
        }
    }
    /* the rest of the subnormal range, where a scale-and-undo path shows whether it
     * undoes: 2^-45 is the smallest positive float and 5.88e-39 the largest subnormal */
    const float edge[] = { 1.0e-45f, 5.8774718e-39f, 2.0e-38f, 1.0e-30f, 8.507059e+37f };
    for (int i = 0; i < (int)(sizeof edge / sizeof edge[0]); i++) {
        long u = ulps(fn(edge[i]), sqrtf(edge[i]));
        if (u > 2) {
            bad++;
            snprintf(buf2, sizeof buf2, "%s sqrt(%.9g) off by %ld ulp", tag, (double)edge[i], u);
            hint("sqrtf special", buf2);
        }
    }
    return bad;
}

static sqrt_scan scan_sqrtf(f1 fn, const char *tag)
{
    sqrt_scan r;
    r.worst_rel = 0.0; r.worst_binade = 0; r.bad_binades = 0; r.bad_specials = 0;
    r.differing = 0; r.samples = 0;
    for (int b = -126; b <= 126; b++) {
        double e = 0.0;
        for (int i = 0; i < 512; i++) {
            float x = ldexpf(1.0f + (float)i / 512.0f, b);
            float want = sqrtf(x);
            float got = fn(x);
            double re = relerr((double)got, (double)want);
            if (re > e) e = re;
            if (ulps(got, want) != 0) r.differing++;
            r.samples++;
            s_checks++;
        }
        if (e > SQRT_REL_TOL) r.bad_binades++;
        if (e > r.worst_rel) { r.worst_rel = e; r.worst_binade = b; }
    }
    r.bad_specials = sqrt_specials(fn, tag);
    return r;
}

/* ------------------------------------------------------------------ sinf/cosf */

typedef struct { double worst_sin, worst_cos; int bad_regions; long differing; long samples; } trig_scan;

static trig_scan scan_trig(f1 fn_s, f1 fn_c)
{
    const float pi = 3.14159265f;
    struct { const char *name; float lo, hi; } reg[] = {
        { "0..pi/4",                 0.0f,        pi / 4.0f },
        { "pi/4..pi/2 (swap)",       pi / 4.0f,   pi / 2.0f },
        { "pi/2..pi (cos sign)",     pi / 2.0f,   pi },
        { "pi..2pi (sin sign)",      pi,          2.0f * pi },
        { "2pi..4pi (turn fold)",    2.0f * pi,   4.0f * pi },
        { "-4pi..0 (odd/even)",     -4.0f * pi,   0.0f },
    };
    trig_scan r;
    r.worst_sin = 0.0; r.worst_cos = 0.0; r.bad_regions = 0; r.differing = 0; r.samples = 0;
    for (int k = 0; k < (int)(sizeof reg / sizeof reg[0]); k++) {
        double es = 0.0, ec = 0.0;
        for (int i = 0; i <= 40000; i++) {
            float x = reg[k].lo + (reg[k].hi - reg[k].lo) * (float)i / 40000.0f;
            float ws = sinf(x), wc = cosf(x);
            float gs = fn_s(x), gc = fn_c(x);
            double ds = abserr((double)gs, (double)ws);
            double dc = abserr((double)gc, (double)wc);
            if (ds > es) es = ds;
            if (dc > ec) ec = dc;
            if (ulps(gs, ws) != 0) r.differing++;
            if (ulps(gc, wc) != 0) r.differing++;
            r.samples += 2;
            s_checks += 2;
        }
        if (es > TRIG_ABS_TOL || ec > TRIG_ABS_TOL) r.bad_regions++;
        if (es > r.worst_sin) r.worst_sin = es;
        if (ec > r.worst_cos) r.worst_cos = ec;
    }
    /* the lattice where every flag changes hands, and beyond the guard where the double
     * fold has to take over */
    for (int k = -24; k <= 24; k++) {
        for (int q = 0; q <= 8; q++) {
            float x = (float)((double)k * GL_PI + (double)q * GL_PI / 8.0);
            double ds = abserr((double)fn_s(x), (double)sinf(x));
            double dc = abserr((double)fn_c(x), (double)cosf(x));
            if (ds > r.worst_sin) r.worst_sin = ds;
            if (dc > r.worst_cos) r.worst_cos = dc;
            if (ds > TRIG_ABS_TOL || dc > TRIG_ABS_TOL) r.bad_regions++;
            s_checks += 2;
        }
    }
    const float big[] = { 12.5663707f, 13.0f, 100.0f, 1e4f, 1e6f, 1e7f };
    for (int i = 0; i < (int)(sizeof big / sizeof big[0]); i++) {
        double ds = abserr((double)fn_s(big[i]), (double)sinf(big[i]));
        double dc = abserr((double)fn_c(big[i]), (double)cosf(big[i]));
        if (ds > TRIG_ABS_TOL || dc > TRIG_ABS_TOL) r.bad_regions++;
        s_checks += 2;
    }
    if (!(isnan(fn_s(INFINITY)) && isnan(fn_c(INFINITY)) &&
          isnan(fn_s(NAN)) && isnan(fn_c(NAN)))) r.bad_regions++;
    return r;
}

/* --------------------------------------------------------------------- floorf */

typedef struct { int mismatches; long samples; } floor_scan;

static floor_scan scan_floorf(f1 fn, const char *tag)
{
    floor_scan r;
    r.mismatches = 0; r.samples = 0;
    /* every integer a float holds with its two neighbours: the boundary where a floor
     * implementation that truncates, or that steps the wrong way for a negative, shows */
    for (int i = -4000; i <= 4000; i++) {
        float x = (float)i;
        const float n[3] = { nextafterf(x, -INFINITY), x, nextafterf(x, INFINITY) };
        for (int j = 0; j < 3; j++) {
            if (u32(fn(n[j])) != u32(floorf(n[j]))) r.mismatches++;
            r.samples++;
            s_checks++;
        }
    }
    /* fractional values in every exponent from 2^-126 to 2^24, both signs, including
     * subnormals and the exact-zero cases the implementation handles by hand */
    srand(20261010u);
    for (int i = 0; i < 200000; i++) {
        unsigned int u = ((unsigned int)rand() << 17) ^ (unsigned int)rand();
        union { unsigned int u; float f; } v;
        v.u = (u & 0x807FFFFFu) | ((unsigned int)(60 + (rand() % 100)) << 23);
        if (!isfinite(v.f)) continue;
        if (u32(fn(v.f)) != u32(floorf(v.f))) r.mismatches++;
        r.samples++;
        s_checks++;
    }
    const float named[] = { -0.0f, 0.0f, -1e-40f, 1e-40f, -0.99999994f, 0.99999994f,
                            -2.0f, 2.0f, -1000.00006f, 1000.00006f, 16777216.0f,
                            -16777215.0f, INFINITY, -INFINITY };
    for (int i = 0; i < (int)(sizeof named / sizeof named[0]); i++) {
        if (isnan(named[i])) continue;
        if (u32(fn(named[i])) != u32(floorf(named[i]))) {
            r.mismatches++;
            snprintf(buf2, sizeof buf2, "%s floorf(%.9g) = %.9g, want %.9g", tag,
                     (double)named[i], (double)fn(named[i]), (double)floorf(named[i]));
            hint("floorf exactness", buf2);
        }
        s_checks++;
    }
    if (!isnan(fn(NAN))) r.mismatches++;
    return r;
}

/* ------------------------------------------------------------------- canaries */

/* Deliberate wrong answers, built out of the shipped ones so they cannot drift into
 * being a second implementation to maintain.  Each stands for a class of bug the sweeps
 * are supposed to detect, and the run fails if a sweep lets one through:
 *   can_sqrt_2x        a constant factor -- the truncating-exponent-halving shape
 *   can_sqrt_neg_zero  a negative answered as 0 instead of NaN
 *   can_sqrt_ftz       subnormals flushed, i.e. no path for them
 *   can_sin_sign       a lost sign flag from the odd/even fold
 *   can_cos_swap       the eighth-circle swap applied to the wrong side
 *   can_floor_neg      a negative stepped toward zero, i.e. truncation as floor
 */
static float can_sqrt_2x(float x)       { return 2.0f * p_sqrtf(x); }
static float can_sqrt_neg_zero(float x) { return x < 0.0f ? 0.0f : p_sqrtf(x); }
static float can_sqrt_ftz(float x)      { return x < 1.17549435e-38f ? 0.0f : p_sqrtf(x); }
static float can_sin_sign(float x)      { return fabsf(p_sinf(x)); }
static float can_cos_swap(float x)      { return (x > 0.7853981f && x < 1.5707963f) ? p_sinf(x) : p_cosf(x); }
static float can_floor_neg(float x)
{
    float f = p_floorf(x);
    return (x < 0.0f && f != x) ? f + 1.0f : f;
}

/* ----------------------------------------------------------------------- main */

int main(void)
{
    printf("== gl/math.h float-native series vs host libm ==\n");

    if (p_series_branch() != 1) {
        printf("  [FAIL] wrong branch compiled: the x87 half was built, so this run\n");
        printf("         measured the host CPU.  The rule in the Makefile must pass\n");
        printf("         -U__x86_64__ -U__i386__ to the port TU.\n");
        return 1;
    }
    printf("  [INFO] branch: freestanding, no-x87 series (the one the PS2 links)\n");

    /* 1. sqrtf */
    sqrt_scan s = scan_sqrtf(p_sqrtf, "shipped");
    snprintf(buf, sizeof buf, "max rel err %.3e (%.1f ulp) at 2^%d, %d binades over %.1e, %d special rows wrong",
             s.worst_rel, s.worst_rel / 5.9604645e-8, s.worst_binade, s.bad_binades,
             SQRT_REL_TOL, s.bad_specials);
    printf("  [INFO] sqrtf  %s\n", buf);
    if (s.bad_binades || s.bad_specials) fail("sqrtf vs libm", buf);

    /* 2. the instrument's teeth, on purpose-broken inputs */
    struct { const char *name; f1 fn; } can[] = {
        { "sqrtf constant factor", can_sqrt_2x },
        { "sqrtf negative as 0",   can_sqrt_neg_zero },
        { "sqrtf subnormals lost", can_sqrt_ftz },
    };
    for (int i = 0; i < (int)(sizeof can / sizeof can[0]); i++) {
        sqrt_scan c = scan_sqrtf(can[i].fn, can[i].name);
        if (c.bad_binades == 0 && c.bad_specials == 0) {
            snprintf(buf, sizeof buf, "sweep accepted a %s -- the sweep is not an instrument", can[i].name);
            fail("positive control", buf);
        } else {
            printf("  [INFO] control: %-24s caught (%d binade rows, %d special rows)\n",
                   can[i].name, c.bad_binades, c.bad_specials);
        }
    }

    /* 3. sinf/cosf */
    trig_scan t = scan_trig(p_sinf, p_cosf);
    snprintf(buf, sizeof buf, "max abs err sin %.3e cos %.3e, %d regions over %.1e",
             t.worst_sin, t.worst_cos, t.bad_regions, TRIG_ABS_TOL);
    printf("  [INFO] trig   %s\n", buf);
    if (t.bad_regions) fail("sinf/cosf vs libm", buf);

    struct { const char *name; f1 fn; } tcan[] = {
        { "sinf lost sign", can_sin_sign },
    };
    for (int i = 0; i < (int)(sizeof tcan / sizeof tcan[0]); i++) {
        trig_scan c = scan_trig(tcan[i].fn, p_cosf);
        if (c.bad_regions == 0) {
            snprintf(buf, sizeof buf, "sweep accepted a %s", tcan[i].name);
            fail("positive control", buf);
        } else
            printf("  [INFO] control: %-24s caught (%d region rows)\n", tcan[i].name, c.bad_regions);
    }
    trig_scan c2 = scan_trig(p_sinf, can_cos_swap);
    if (c2.bad_regions == 0) fail("positive control", "sweep accepted a cosf with the swap on the wrong side");
    else printf("  [INFO] control: %-24s caught (%d region rows)\n", "cosf swapped wrongly", c2.bad_regions);

    /* 4. floorf, bit-exact */
    floor_scan fl = scan_floorf(p_floorf, "shipped");
    snprintf(buf, sizeof buf, "%ld values, %d bit mismatches", fl.samples, fl.mismatches);
    printf("  [INFO] floorf %s\n", buf);
    if (fl.mismatches) fail("floorf vs libm", buf);

    floor_scan fc = scan_floorf(can_floor_neg, "control");
    if (fc.mismatches == 0) fail("positive control", "sweep accepted a floorf that truncates negatives");
    else printf("  [INFO] control: %-24s caught (%d mismatches)\n", "floorf truncates", fc.mismatches);

    /* 5. was the header involved at all?  A series that agreed with the oracle on every
     * single input would be a hardware instruction wearing a source file. */
    double pd = (double)s.differing / (double)s.samples;
    double pt = (double)t.differing / (double)t.samples;
    printf("  [INFO] differs from libm by >=1 ulp: sqrtf %.1f%%, trig %.1f%% of samples\n",
           pd * 100.0, pt * 100.0);
    if (pd < 0.001 || pt < 0.001) {
        snprintf(buf, sizeof buf, "answers were bit-identical to libm (%.4f%%, %.4f%%) -- nothing was measured",
                 pd * 100.0, pt * 100.0);
        fail("instrument", buf);
    }

    printf("  checks=%ld failures=%d\n", s_checks, s_failures);
    if (s_failures) { printf("[RESULT] FAIL\n"); return 1; }
    printf("[RESULT] PASS float-native sqrtf/sinf/cosf/floorf, and the sweeps can fail\n");
    return 0;
}
