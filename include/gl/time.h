#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(BTRON_UEFI_TARGET)
#include_next <time.h>
#else
/* The NASM disassembler stores a time_t and a struct tm. Freestanding
 * images have no libc time.h, and nothing in this image calls the
 * clock functions. */
#ifndef _GL_TIME_H_
#define _GL_TIME_H_

typedef long time_t;

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

#endif /* _GL_TIME_H_ */
#endif /* HOSTED */
