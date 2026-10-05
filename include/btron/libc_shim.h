/*
 * B-System — single definition of the hosted / freestanding C-library split.
 *
 * Include this instead of writing your own
 *   #if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 ...
 * block. On a hosted target the C library is real; on a freestanding target
 * (UEFI, PC-98, bare-metal ARM/M68K/PS2/MIPS) the kernel supplies the same
 * names: src/kernel/libstr.c defines weak aliases memcpy/strlen/strcpy/...
 * onto its tkl_* implementations, and Imalloc/Icalloc/Ifree are the heap.
 *
 * So a freestanding translation unit only needs *declarations* — never a
 * #define of strlen onto tkl_strlen. If you call a function this header does
 * not declare, the freestanding builds fail: add it here (and in libstr.c)
 * once, rather than patching eight source files.
 *
 * BTRON_HOSTED is 1 on a hosted target, 0 otherwise, for the cases that
 * genuinely need POSIX (unistd.h, dirent.h, sockets).
 *
 * The one deliberate exception: include/gl/{stdio,stdlib,string}.h hijack those
 * system header names (via -Iinclude/gl on UEFI and PC-98) for third-party
 * sources — TinyGL, Quake, stb — that include <string.h> by its real name and
 * cannot be edited to include this header instead.
 */

#ifndef BTRON_LIBC_SHIM_H
#define BTRON_LIBC_SHIM_H

/* Provided by the compiler in both hosted and freestanding mode. */
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1

#  include <stdio.h>
#  include <stdlib.h>
#  include <string.h>
#  include <ctype.h>
#  define BTRON_HOSTED 1

#else

#  include <libstr.h>

#  define BTRON_HOSTED 0

/* Kernel heap (src/kernel/libstr.c) */
extern void *Imalloc(size_t size);
extern void *Icalloc(size_t nmemb, size_t size);
extern void  Ifree(void *ptr);
#  define malloc   Imalloc
#  define calloc   Icalloc
#  define free     Ifree

/* Kernel <string.h> (weak aliases in src/kernel/libstr.c) */
extern void  *memset(void *, int, size_t);
extern void  *memcpy(void *, const void *, size_t);
extern void  *memmove(void *, const void *, size_t);
extern int    memcmp(const void *, const void *, size_t);
extern size_t strlen(const char *);
extern char  *strcpy(char *, const char *);
extern char  *strncpy(char *, const char *, size_t);
extern int    strcmp(const char *, const char *);
extern int    strncmp(const char *, const char *, size_t);
extern char  *strcat(char *, const char *);
extern char  *strncat(char *, const char *, size_t);
extern char  *strchr(const char *, int);
extern char  *strrchr(const char *, int);
extern char  *strpbrk(const char *, const char *);
extern char  *strstr(const char *, const char *);

/* Kernel <stdio.h> / <stdlib.h> */
extern int snprintf(char *, size_t, const char *, ...);
extern unsigned long int strtoul(const char *, char **, int);
extern int atoi(const char *);

static inline int abs(int n) { return n < 0 ? -n : n; }

/* Kernel <ctype.h> */
#  define isspace(c) ((c)==' '||(c)=='\t'||(c)=='\n'||(c)=='\r'||(c)=='\f'||(c)=='\v')
#  define isdigit(c) ((c)>='0'&&(c)<='9')
#  define isxdigit(c) (((c)>='0'&&(c)<='9')||((c)>='a'&&(c)<='f')||((c)>='A'&&(c)<='F'))
#  define isalpha(c) (((c)>='a'&&(c)<='z')||((c)>='A'&&(c)<='Z'))
#  define isalnum(c) (isalpha(c)||isdigit(c))
#  define isupper(c) ((c)>='A'&&(c)<='Z')
#  define tolower(c) (isupper(c) ? (c)+('a'-'A') : (c))
#  define toupper(c) ((c)>='a'&&(c)<='z' ? (c)-('a'-'A') : (c))

#  ifndef assert
#    define assert(expr) ((void)(expr))
#  endif

#endif /* __STDC_HOSTED__ */

#endif /* BTRON_LIBC_SHIM_H */
