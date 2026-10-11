/*
 * ps2_farfold_probe.c - the shape of include/gl/math.h's large-argument trig fold,
 *                       compiled the way the PS2 image is compiled.
 *
 * Why a probe and not a disassembly of the image: sinf is `static inline`, so in
 * btron-ps2.elf it is inlined into a hundred call sites and no law about "the far
 * tier" can be read off the whole image.  Built alone, with the image's own flags,
 * this TU is one function and its relocations are the law.
 *
 * What it proves: a float angle past the guard is folded by btron_sincosf_far, whose
 * cost is a handful of software double operations and the float polynomial -- and no
 * software double *division*, which is what the guard used to hand the argument to the
 * double series to avoid (btron_sincos has thirteen __divdf3 in it).  A regression back
 * to `(float)sin((double)x)`, or a fold that divides by 2*pi instead of multiplying by
 * its reciprocal, shows up here as a nonzero count.  scripts/test_ps2.sh asserts that.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <math.h>          /* include/gl/math.h, reached by -Iinclude/gl as everywhere else */

float btron_farfold_probe(float x)
{
    float s, c;
    btron_sincosf_far(x, &s, &c);
    return s + c;
}
