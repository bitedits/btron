#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(BTRON_UEFI_TARGET)
#include_next <stdlib.h>
#else
#ifndef _GL_STDLIB_H_
#define _GL_STDLIB_H_

#include <libstr.h>

extern void* Imalloc(size_t size);
extern void* Icalloc(size_t nmemb, size_t size);
extern void  Ifree(void *ptr);

static inline void* malloc(size_t size) { return Imalloc(size); }
static inline void* calloc(size_t nmemb, size_t size) { return Icalloc(nmemb, size); }
static inline void  free(void *ptr) { Ifree(ptr); }
static inline void  exit(int status) { (void)status; }
static inline void  abort(void) { for (;;) { __asm__ volatile("hlt"); } }

/* Hosted builds get rand() from libc; the Quake sources call it for weapon
 * spread.  Same 24-bit LCG as the QC random() builtin so both targets jitter
 * alike. */
static inline int rand(void) {
    static unsigned s_rnd = 0xDEADBEEFu;
    s_rnd = s_rnd * 1664525u + 1013904223u;
    return (int)(s_rnd >> 8);
}

#endif /* _GL_STDLIB_H_ */
#endif /* HOSTED */

