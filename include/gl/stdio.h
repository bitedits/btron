#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(BTRON_UEFI_TARGET)
#include_next <stdio.h>
#else

#ifndef _GL_STDIO_H_
#define _GL_STDIO_H_

#include <libstr.h>
#include <stdarg.h>

#ifndef NULL
#define NULL 0
#endif

typedef void FILE;
#define stderr ((FILE *)2)
#define stdout ((FILE *)1)

extern void uart_puts_raw(const char *s);
extern int tkl_vsnprintf(char *str, size_t size, const char *format, va_list ap);
extern int tkl_snprintf(char *str, size_t size, const char *format, ...);

#define printf(...)     ((void)0)
#define fprintf(...)    ((void)0)
#define vfprintf(...)   ((void)0)
#define fputc(...)      ((void)0)
#define snprintf        tkl_snprintf
#define vsnprintf       tkl_vsnprintf

/* sprintf(buf, fmt, ...) is not snprintf.  Aliasing the name to
 * tkl_snprintf made the format string the size argument.  The cap keeps
 * `str + size` from wrapping a 32-bit kseg0 pointer; a disassembly line
 * fits inside it. */
static inline int sprintf(char *str, const char *format, ...)
{
    va_list ap;
    int n;

    va_start(ap, format);
    n = tkl_vsnprintf(str, 8192, format, ap);
    va_end(ap);
    return n;
}

/* The four byte-stream calls, declared here because this is the header every
 * freestanding lane resolves <stdio.h> to (-Iinclude/gl is in PS2_CFLAGS,
 * PC98_CFLAGS, UEFI_CFLAGS, ARM_CFLAGS and the default CFLAGS).  They are
 * answered by src/fs/fs_stdio.c on the lanes that link the record-stream
 * engine, and left undefined — not stubbed — where that engine is absent, so a
 * port that has no /SYS fails at the link instead of reading a zero that looks
 * like an empty file.  The shape law is in include/btron/libc_shim.h. */
extern FILE *fopen(const char *path, const char *mode);
extern size_t fread(void *ptr, size_t size, size_t count, FILE *stream);
extern size_t fwrite(const void *ptr, size_t size, size_t count, FILE *stream);
extern int fclose(FILE *stream);

#endif /* _GL_STDIO_H_ */
#endif /* HOSTED */

