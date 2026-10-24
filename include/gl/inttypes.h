#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(BTRON_UEFI_TARGET)
#include_next <inttypes.h>
#else
/* Freestanding images have no libc inttypes.h. Clang's own wrapper
 * include_nexts that header and the PS2 link stops. Format macros only:
 * the types and the INT*_C constants stay in stdint.h. */
#ifndef _GL_INTTYPES_H_
#define _GL_INTTYPES_H_

#include <stdint.h>
#include <limits.h>

#if LONG_MAX == 9223372036854775807L
#define _gl_pri64 "l"
#define _gl_scn64 "l"
#elif LLONG_MAX == 9223372036854775807LL
#define _gl_pri64 "ll"
#define _gl_scn64 "ll"
#else
#error "Neither long nor long long is 64 bits"
#endif

#if INT_MAX == 2147483647
#define _gl_pri32 ""
#define _gl_scn32 ""
#elif LONG_MAX == 2147483647L
#define _gl_pri32 "l"
#define _gl_scn32 "l"
#else
#error "Neither int nor long is 32 bits"
#endif

#ifndef PRIx64

#define PRId8   "d"
#define PRId16  "d"
#define PRId32  _gl_pri32 "d"
#define PRId64  _gl_pri64 "d"
#define PRIi8   "i"
#define PRIi16  "i"
#define PRIi32  _gl_pri32 "i"
#define PRIi64  _gl_pri64 "i"
#define PRIo8   "o"
#define PRIo16  "o"
#define PRIo32  _gl_pri32 "o"
#define PRIo64  _gl_pri64 "o"
#define PRIu8   "u"
#define PRIu16  "u"
#define PRIu32  _gl_pri32 "u"
#define PRIu64  _gl_pri64 "u"
#define PRIx8   "x"
#define PRIx16  "x"
#define PRIx32  _gl_pri32 "x"
#define PRIx64  _gl_pri64 "x"
#define PRIX8   "X"
#define PRIX16  "X"
#define PRIX32  _gl_pri32 "X"
#define PRIX64  _gl_pri64 "X"

#define SCNd32  _gl_scn32 "d"
#define SCNu32  _gl_scn32 "u"
#define SCNx32  _gl_scn32 "x"
#define SCNd64  _gl_scn64 "d"
#define SCNu64  _gl_scn64 "u"
#define SCNx64  _gl_scn64 "x"

#endif /* PRIx64 */

#ifndef PRIxPTR
#if UINTPTR_MAX == 0xffffffffu
#define PRIdPTR PRId32
#define PRIuPTR PRIu32
#define PRIxPTR PRIx32
#define PRIXPTR PRIX32
#else
#define PRIdPTR PRId64
#define PRIuPTR PRIu64
#define PRIxPTR PRIx64
#define PRIXPTR PRIX64
#endif
#define PRIdMAX PRId64
#define PRIuMAX PRIu64
#define PRIxMAX PRIx64
#define PRIXMAX PRIX64
#endif

#endif /* _GL_INTTYPES_H_ */
#endif /* HOSTED */
