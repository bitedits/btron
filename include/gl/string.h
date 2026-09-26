#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(BTRON_UEFI_TARGET)
#include_next <string.h>
#else
#ifndef _GL_STRING_H_
#define _GL_STRING_H_

#include <libstr.h>

#define memset  tkl_memset
#define memcpy  tkl_memcpy
#define memmove tkl_memmove
#define memcmp  tkl_memcmp
#define strlen  tkl_strlen
#define strcpy  tkl_strcpy
#define strncpy tkl_strncpy
#define strcmp  tkl_strcmp
#define strncmp tkl_strncmp
#define strcat  tkl_strcat
#define strncat tkl_strncat
#define strstr  tkl_strstr

#endif /* _GL_STRING_H_ */
#endif /* HOSTED */

