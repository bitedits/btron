/*
 * B-System (BTRON 3.20) Kernel TIP: Extended Wylie Tibetan EWTS Converter
 * Ported from https://github.com/longchenpa/wylie/blob/master/src/wylie.erl
 * Authors: Namdak Tonpa (རྣམ་དག་སྟོན་པ), Longchen Nyingthig (ཀློང་ཆེན་སྙིང་ཐིག)
 * Pure C99 Implementation for BTRON TIP Input Method and Multilingual Engine
 */

#ifndef _BTRON_KERNEL_TIP_WYLIE_H_
#define _BTRON_KERNEL_TIP_WYLIE_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Transcode Extended Wylie string into Tibetan UTF-8 text.
 * @param wylie_in   Null-terminated input ASCII/Wylie string (e.g. "oM AH hU~M")
 * @param utf8_out   Buffer to store resulting UTF-8 Tibetan string (e.g. "ཨོཾ་ཨཱཿ་ཧཱུྃ")
 * @param max_out    Maximum size of utf8_out buffer in bytes
 * @return Number of UTF-8 bytes written, or -1 on error.
 */
int wylie_to_tibetan(const char *wylie_in, char *utf8_out, size_t max_out);

/**
 * Run internal Wylie unit tests from wylie.erl.
 * @return 0 on all tests passing, non-zero on failure.
 */
int wylie_run_tests(void);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_KERNEL_TIP_WYLIE_H_ */
