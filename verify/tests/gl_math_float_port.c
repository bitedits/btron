/*
 * gl_math_float_port.c - the shim's float-native half, compiled as its own TU.
 *
 * include/gl/math.h is the freestanding math for every bare-metal target in this tree,
 * and the PS2 is one of them: the Emotion Engine executes single-precision on COP1 in
 * around twenty cycles and executes double-precision not at all, so a float function
 * written as `(float)double_series(x)` costs the image a software double operation for
 * every multiply in the series.  That is what made one XMB icon cell 14,718 cycles per
 * pixel.  The float-native sqrtf/sinf/cosf/floorf answer it, and this is the TU that
 * puts them where a measurement can reach them.
 *
 * Two things make this file exist rather than being folded into the test:
 *
 *   1. The measuring TU needs the host's libm as its oracle, and the shim's definitions
 *      are `static inline` under the names libm owns, so the two cannot be one file.
 *   2. The branch has to be forced.  The header splits first on hosted/freestanding and
 *      then on x87/no-x87, so `BTRON_UEFI_TARGET` (defined here) plus `-U__x86_64__
 *      -U__i386__` (on the command line, by the rule in the Makefile) is what lands the
 *      compile on the series arithmetic.  With only one of the two the x87 half builds,
 *      the test runs against the CPU it is standing on and passes for the wrong reason.
 *      p_series_branch() reports which half actually compiled, and the test separately
 *      counts how many answers differ from the oracle by at least one representable
 *      step -- a run in which none do measured the CPU it runs on, not this header.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#define BTRON_UEFI_TARGET 1
#include "../../include/gl/math.h"

float p_sqrtf(float x)  { return sqrtf(x); }
float p_sinf(float x)   { return sinf(x); }
float p_cosf(float x)   { return cosf(x); }
float p_floorf(float x) { return floorf(x); }

/* 1 when the series arithmetic compiled, 0 when the x87 asm did. */
int p_series_branch(void)
{
#if defined(__i386__) || defined(__x86_64__)
    return 0;
#else
    return 1;
#endif
}
