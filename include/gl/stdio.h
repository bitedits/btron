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
#define sprintf         tkl_snprintf
#define snprintf        tkl_snprintf
#define vsnprintf       tkl_vsnprintf

#endif /* _GL_STDIO_H_ */
