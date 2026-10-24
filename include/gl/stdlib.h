#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(BTRON_UEFI_TARGET)
#include_next <stdlib.h>
#else
#ifndef _GL_STDLIB_H_
#define _GL_STDLIB_H_

#include <libstr.h>

extern void* Imalloc(size_t size);
extern void* Icalloc(size_t nmemb, size_t size);
extern void  Ifree(void *ptr);
extern void* Irealloc(void *ptr, size_t size);

static inline void* malloc(size_t size) { return Imalloc(size); }
static inline void* calloc(size_t nmemb, size_t size) { return Icalloc(nmemb, size); }
static inline void  free(void *ptr) { Ifree(ptr); }
static inline void* realloc(void *ptr, size_t size) { return Irealloc(ptr, size); }
static inline void  exit(int status) { (void)status; }
/* hlt is an x86 halt.  Every other freestanding target, including the
 * Emotion Engine, rejects that mnemonic, so the spin has to be empty. */
static inline void  abort(void) {
#if defined(__i386__) || defined(__x86_64__)
    for (;;) { __asm__ volatile("hlt"); }
#else
    for (;;) { __asm__ volatile(""); }
#endif
}
/* No process environment on a freestanding image.  Callers treat NULL
 * as "the variable is unset". */
static inline char *getenv(const char *name) { (void)name; return 0; }

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define strtoull tkl_strtoull

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

