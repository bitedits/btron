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
}

#endif /* _GL_MATH_H_ */
#endif /* HOSTED */

