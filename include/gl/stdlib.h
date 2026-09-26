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

#endif /* _GL_STDLIB_H_ */
