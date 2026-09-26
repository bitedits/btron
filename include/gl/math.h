#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(BTRON_UEFI_TARGET)
#include_next <math.h>
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

