#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(BTRON_UEFI_TARGET)
#include_next <math.h>
#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif
#else
#ifndef _GL_MATH_H_
#define _GL_MATH_H_

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
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

/* Two things the series branches need rather than a libm, because there is none in
 * a -nostdlib link: the exponent of a double as an integer, and the scaling that
 * puts it back.  Working on the bits is exact and costs no call. */
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

/* The Quake sources are float-native, so the x87 helpers above need single
 * wrappers here; the hosted branch gets them from the real math.h. */
static inline float sinf(float x)   { return (float)sin((double)x); }
static inline float cosf(float x)   { return (float)cos((double)x); }
static inline float sqrtf(float x)  { return (float)sqrt((double)x); }
static inline float fabsf(float x)  { return (x < 0.0f) ? -x : x; }
static inline float atan2f(float y, float x) { return (float)atan2((double)y, (double)x); }

static inline double floor(double x) {
    int i = (int)x;
    if (x < (double)i) return (double)(i - 1);
    return (double)i;
}

static inline double btron_log(double x) {
    int e = btron_dexp(x);
    double m = btron_ldexp(x, -e);              /* [1,2) */
    if (m > 1.41421356237309504880) { m *= 0.5; e++; }   /* [sqrt(1/2),sqrt(2)] */
    double z = (m - 1.0) / (m + 1.0);           /* log m = 2*atanh z */
    double z2 = z * z, term = z, sum = z;
    for (int n = 3; n <= 25; n += 2) { term *= z2; sum += term / (double)n; }
    return 2.0 * sum + (double)e * 0.69314718055994530942;
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

static inline float floorf(float x) { return (float)floor((double)x); }
static inline float powf(float x, float y) { return (float)pow((double)x, (double)y); }
static inline long long llround(double x) { return (long long)(x >= 0.0 ? x + 0.5 : x - 0.5); }

#ifndef M_E
#define M_E 2.71828182845904523536
#endif

static inline double exp(double x) { return pow(M_E, x); }
static inline float expf(float x) { return (float)exp((double)x); }
static inline double fmod(double x, double y) {
    if (y == 0.0) return 0.0;
    return x - (double)((long long)(x / y)) * y;
}
static inline float fmodf(float x, float y) { return (float)fmod((double)x, (double)y); }

#endif /* _GL_MATH_H_ */
#endif /* HOSTED */

