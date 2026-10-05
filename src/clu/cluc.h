/*
 * B-System BTRON3 — cluc.h
 * Common freestanding/hosted shim for the CLU terminal applications
 * (tty, lang, term, vfs, tv, sc).
 *
 * Rules the whole CLU terminal stack follows (NASA/JPL "Power of Ten"):
 *   - no recursion, no goto, no setjmp
 *   - every loop has a fixed upper bound
 *   - no dynamic allocation: all storage is static and sized by macros
 *   - every return value that can fail is checked
 *   - all string output goes through bounded snprintf/memcpy
 */
#ifndef CLUC_H
#define CLUC_H

#include <stddef.h>
#include <stdint.h>

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#  include <string.h>
#  include <stdio.h>
#  include <stdlib.h>
#  define CLU_HOSTED 1
#else
#  define CLU_HOSTED 0
extern void *Imalloc(size_t sz);
extern void  Ifree(void *ptr);
extern void *Icalloc(size_t nmemb, size_t sz);
#  define malloc  Imalloc
#  define free    Ifree
#  define calloc  Icalloc
extern void  *tkl_memset(void *, int, size_t);
extern void  *tkl_memcpy(void *, const void *, size_t);
extern void  *tkl_memmove(void *, const void *, size_t);
extern int    tkl_memcmp(const void *, const void *, size_t);
extern size_t tkl_strlen(const char *);
extern int    tkl_strcmp(const char *, const char *);
extern int    tkl_strncmp(const char *, const char *, size_t);
extern int    snprintf(char *, size_t, const char *, ...);
#  define memset   tkl_memset
#  define memcpy   tkl_memcpy
#  define memmove  tkl_memmove
#  define memcmp   tkl_memcmp
#  define strlen   tkl_strlen
#  define strcmp   tkl_strcmp
#  define strncmp  tkl_strncmp
#endif

/* Clamp helper used by layout code. */
#define CLU_CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))

#endif /* CLUC_H */
