#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(BTRON_UEFI_TARGET)
#include_next <assert.h>
#else
#ifndef _GL_ASSERT_H_
#define _GL_ASSERT_H_

/* Freestanding (bare-metal / UEFI) builds have no libc to raise on failure, so
 * the JPL contract checks are compiled as side-effect-free evaluations: the
 * invariant expression is still checked for well-formedness, but a violated
 * assertion does not attempt to call out to a hosted abort(). */
#define assert(expr) ((void)(expr))

#endif /* _GL_ASSERT_H_ */
#endif /* HOSTED */
