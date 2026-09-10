/*
 * B-System (BTRON 3.20) TIP: Extended Wylie Tibetan EWTS Converter
 * Ported from https://github.com/longchenpa/wylie/blob/master/src/wylie.erl
 * Authors: Namdak Tonpa (རྣམ་དག་སྟོན་པ), Longchen Nyingthig (ཀློང་ཆེན་སྙིང་ཐིག)
 */

#include <btron/wylie.h>
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#else
#include <stddef.h>
#include <stdint.h>
#include <libstr.h>
#define memset tkl_memset
#define strlen tkl_strlen
#define strncmp tkl_strncmp
#define strcmp tkl_strcmp
#define bool int
#define true 1
#define false 0
#endif

typedef enum {
    VAL_TYPE_NONE = 0,
    VAL_TYPE_TSHEG,       /* " " -> 0x0F0B */
    VAL_TYPE_DOT,         /* "." -> empty, stack=5 */
    VAL_TYPE_UNDERSCORE,  /* "_" -> space 0x0020, stack=P */
    VAL_TYPE_PAIR_SINGLE, /* {BaseCodepoint, SubjoinedCodepoint} */
    VAL_TYPE_PAIR_LIST,   /* {BaseList, SubList} */
    VAL_TYPE_APOSTROPHE,  /* Starts with single quote */
    VAL_TYPE_VOWEL,       /* Vowel single codepoint */
    VAL_TYPE_VOWEL_LIST,  /* Vowel list of codepoints (e.g. 'i, I, U, etc.) */
    VAL_TYPE_PLUS,        /* "+" -> empty, stack=P+1 */
    VAL_TYPE_LIST,        /* generic list of codepoints, stack=P+1 */
    VAL_TYPE_OTHER_INT    /* punctuation, numbers, finals -> stack=5 */
} WylieValKind;

typedef struct {
    const char *key;
    bool is_vowel;
    WylieValKind kind;
    uint32_t val1;
    uint32_t val2;
    uint32_t list1[8];
    uint32_t list2[8];
    int len1;
    int len2;
} WylieRule;

static const WylieRule g_rules[] = {
    /* 1. Constants */
    { "gsh", false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f42, 0x0f64}, {0x0f92, 0x0fb4}, 2, 2 },
    { "gny", false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f42, 0x0f49}, {0x0f92, 0x0f99}, 2, 2 },
    { "gzh", false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f42, 0x0f5e}, {0x0f92, 0x0fae}, 2, 2 },
    { "g+h", false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f42, 0x0fb7}, {0x0f92, 0x0fb7}, 2, 2 },
    { "gc",  false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f42, 0x0f45}, {0x0f92, 0x0f95}, 2, 2 },
    { "gd",  false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f42, 0x0f51}, {0x0f92, 0x0fa1}, 2, 2 },
    { "gn",  false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f42, 0x0f53}, {0x0f92, 0x0fa3}, 2, 2 },
    { "gh",  false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f42, 0x0fb7}, {0x0f92, 0x0fb7}, 2, 2 },
    { "gz",  false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f42, 0x0f5f}, {0x0f92, 0x0faf}, 2, 2 },
    { "gt",  false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f42, 0x0f4f}, {0x0f92, 0x0f9f}, 2, 2 },
    { "gs",  false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f42, 0x0f66}, {0x0f92, 0x0fb6}, 2, 2 },
    { "kh",  false, VAL_TYPE_PAIR_SINGLE, 0x0f41, 0x0f91, {0}, {0}, 0, 0 },
    { "k",   false, VAL_TYPE_PAIR_SINGLE, 0x0f40, 0x0f90, {0}, {0}, 0, 0 },
    { "g",   false, VAL_TYPE_PAIR_SINGLE, 0x0f42, 0x0f92, {0}, {0}, 0, 0 },

    { "ch",  false, VAL_TYPE_PAIR_SINGLE, 0x0f46, 0x0f96, {0}, {0}, 0, 0 },
    { "c",   false, VAL_TYPE_PAIR_SINGLE, 0x0f45, 0x0f95, {0}, {0}, 0, 0 },
    { "j",   false, VAL_TYPE_PAIR_SINGLE, 0x0f47, 0x0f97, {0}, {0}, 0, 0 },
    { "ny",  false, VAL_TYPE_PAIR_SINGLE, 0x0f49, 0x0f99, {0}, {0}, 0, 0 },

    { "Th",   false, VAL_TYPE_PAIR_SINGLE, 0x0f4b, 0x0f9b, {0}, {0}, 0, 0 },
    { "-th",  false, VAL_TYPE_PAIR_SINGLE, 0x0f4b, 0x0f9b, {0}, {0}, 0, 0 },
    { "T",    false, VAL_TYPE_PAIR_SINGLE, 0x0f4a, 0x0f9a, {0}, {0}, 0, 0 },
    { "-t",   false, VAL_TYPE_PAIR_SINGLE, 0x0f4a, 0x0f9a, {0}, {0}, 0, 0 },
    { "Dh",   false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f4c, 0x0fb7}, {0x0f9c, 0x0fb7}, 2, 2 },
    { "D+h",  false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f4c, 0x0fb7}, {0x0f9c, 0x0fb7}, 2, 2 },
    { "-dh",  false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f4c, 0x0fb7}, {0x0f9c, 0x0fb7}, 2, 2 },
    { "-d+h", false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f4c, 0x0fb7}, {0x0f9c, 0x0fb7}, 2, 2 },
    { "D",    false, VAL_TYPE_PAIR_SINGLE, 0x0f4c, 0x0f9c, {0}, {0}, 0, 0 },
    { "-d",   false, VAL_TYPE_PAIR_SINGLE, 0x0f4c, 0x0f9c, {0}, {0}, 0, 0 },
    { "N",    false, VAL_TYPE_PAIR_SINGLE, 0x0f4e, 0x0f9e, {0}, {0}, 0, 0 },
    { "-n",   false, VAL_TYPE_PAIR_SINGLE, 0x0f4e, 0x0f9e, {0}, {0}, 0, 0 },

    { "th",   false, VAL_TYPE_PAIR_SINGLE, 0x0f50, 0x0fa0, {0}, {0}, 0, 0 },
    { "t",    false, VAL_TYPE_PAIR_SINGLE, 0x0f4f, 0x0f9f, {0}, {0}, 0, 0 },
    { "dng",  false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f51, 0x0f44}, {0x0fa1, 0x0f94}, 2, 2 },
    { "dzh",  false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f5b, 0x0fb7}, {0x0fab, 0x0fb7}, 2, 2 },
    { "dz+h", false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f5b, 0x0fb7}, {0x0fab, 0x0fb7}, 2, 2 },
    { "dz",   false, VAL_TYPE_PAIR_SINGLE, 0x0f5b, 0x0fab, {0}, {0}, 0, 0 },
    { "dm",   false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f51, 0x0f58}, {0x0fa1, 0x0fa8}, 2, 2 },
    { "db",   false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f51, 0x0f56}, {0x0fa1, 0x0fa6}, 2, 2 },
    { "dh",   false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f51, 0x0fb7}, {0x0fa1, 0x0fb7}, 2, 2 },
    { "d+h",  false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f51, 0x0fb7}, {0x0fa1, 0x0fb7}, 2, 2 },
    { "dp",   false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f51, 0x0f54}, {0x0fa1, 0x0fa4}, 2, 2 },
    { "dk",   false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f51, 0x0f40}, {0x0fa1, 0x0f90}, 2, 2 },
    { "dg",   false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f51, 0x0f42}, {0x0fa1, 0x0f92}, 2, 2 },
    { "d",    false, VAL_TYPE_PAIR_SINGLE, 0x0f51, 0x0fa1, {0}, {0}, 0, 0 },
    { "n",    false, VAL_TYPE_PAIR_SINGLE, 0x0f53, 0x0fa3, {0}, {0}, 0, 0 },

    { "ngs",  false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f44, 0x0f66}, {0x0f94, 0x0fb6}, 2, 2 },
    { "ng",   false, VAL_TYPE_PAIR_SINGLE, 0x0f44, 0x0f94, {0}, {0}, 0, 0 },

    { "ph",   false, VAL_TYPE_PAIR_SINGLE, 0x0f55, 0x0fa5, {0}, {0}, 0, 0 },
    { "p",    false, VAL_TYPE_PAIR_SINGLE, 0x0f54, 0x0fa4, {0}, {0}, 0, 0 },

    { "brts", false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f56, 0x0f62, 0x0fa9}, {0x0fa6, 0x0fb2, 0x0fa9}, 3, 3 },
    { "brg",  false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f56, 0x0f62, 0x0f92}, {0x0fa6, 0x0fb2, 0x0f92}, 3, 3 },
    { "brt",  false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f56, 0x0f62, 0x0f9f}, {0x0fa6, 0x0fb2, 0x0f9f}, 3, 3 },
    { "brd",  false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f56, 0x0f62, 0x0fa1}, {0x0fa6, 0x0fb2, 0x0fa1}, 3, 3 },
    { "bts",  false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f56, 0x0f59}, {0x0fa6, 0x0fa9}, 2, 2 },
    { "bsh",  false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f56, 0x0f64}, {0x0fa6, 0x0fb4}, 2, 2 },
    { "bzh",  false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f56, 0x0f5e}, {0x0fa6, 0x0fae}, 2, 2 },
    { "b+h",  false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f56, 0x0fb7}, {0x0fa6, 0x0fb7}, 2, 2 },
    { "bk",   false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f56, 0x0f40}, {0x0fa6, 0x0f40}, 2, 2 },
    { "bc",   false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f56, 0x0f45}, {0x0fa6, 0x0f95}, 2, 2 },
    { "bs",   false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f56, 0x0f66}, {0x0fa6, 0x0fb6}, 2, 2 },
    { "bd",   false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f56, 0x0f51}, {0x0fa6, 0x0fa1}, 2, 2 },
    { "bh",   false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f56, 0x0fb7}, {0x0fa6, 0x0fb7}, 2, 2 },
    { "bz",   false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f56, 0x0f5f}, {0x0fa6, 0x0faf}, 2, 2 },
    { "bt",   false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f56, 0x0f4f}, {0x0fa6, 0x0f9f}, 2, 2 },
    { "b",    false, VAL_TYPE_PAIR_SINGLE, 0x0f56, 0x0fa6, {0}, {0}, 0, 0 },

    { "mtsh", false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f58, 0x0f5a}, {0x0faa, 0x0faa}, 2, 2 },
    { "mny",  false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f58, 0x0f49}, {0x0fa8, 0x0f99}, 2, 2 },
    { "mng",  false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f58, 0x0f44}, {0x0fa8, 0x0f94}, 2, 2 },
    { "mch",  false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f58, 0x0f46}, {0x0fa8, 0x0f96}, 2, 2 },
    { "mkh",  false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f58, 0x0f41}, {0x0fa8, 0x0f91}, 2, 2 },
    { "mth",  false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f58, 0x0f50}, {0x0fa8, 0x0fa0}, 2, 2 },
    { "mts",  false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f58, 0x0f59}, {0x0fa8, 0x0fa9}, 2, 2 },
    { "mdz",  false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f58, 0x0f5b}, {0x0fa8, 0x0fab}, 2, 2 },
    { "mg",   false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f58, 0x0f42}, {0x0fa8, 0x0f92}, 2, 2 },
    { "ms",   false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f58, 0x0f66}, {0x0fa8, 0x0fb6}, 2, 2 },
    { "m",    false, VAL_TYPE_PAIR_SINGLE, 0x0f58, 0x0fa8, {0}, {0}, 0, 0 },

    { "tsh",  false, VAL_TYPE_PAIR_SINGLE, 0x0f5a, 0x0faa, {0}, {0}, 0, 0 },
    { "ts",   false, VAL_TYPE_PAIR_SINGLE, 0x0f59, 0x0fa9, {0}, {0}, 0, 0 },
    { "zh",   false, VAL_TYPE_PAIR_SINGLE, 0x0f5e, 0x0fae, {0}, {0}, 0, 0 },
    { "z",    false, VAL_TYPE_PAIR_SINGLE, 0x0f5f, 0x0faf, {0}, {0}, 0, 0 },
    { "w",    false, VAL_TYPE_PAIR_SINGLE, 0x0f5d, 0x0fad, {0}, {0}, 0, 0 },
    { "'",    false, VAL_TYPE_PAIR_SINGLE, 0x0f60, 0x0fb0, {0}, {0}, 0, 0 },
    { "\xe2\x80\x98", false, VAL_TYPE_PAIR_SINGLE, 0x0f60, 0x0fb0, {0}, {0}, 0, 0 }, /* typographic single quote ‘ */
    { "\xe2\x80\x99", false, VAL_TYPE_PAIR_SINGLE, 0x0f60, 0x0fb0, {0}, {0}, 0, 0 }, /* typographic single quote ’ */
    { "y",    false, VAL_TYPE_PAIR_SINGLE, 0x0f61, 0x0fb1, {0}, {0}, 0, 0 },
    { "r",    false, VAL_TYPE_PAIR_SINGLE, 0x0f62, 0x0fb2, {0}, {0}, 0, 0 },
    { "l",    false, VAL_TYPE_PAIR_SINGLE, 0x0f63, 0x0fb3, {0}, {0}, 0, 0 },
    { "sh",   false, VAL_TYPE_PAIR_SINGLE, 0x0f64, 0x0fb4, {0}, {0}, 0, 0 },
    { "Sh",   false, VAL_TYPE_PAIR_SINGLE, 0x0f65, 0x0fb5, {0}, {0}, 0, 0 },
    { "-sh",  false, VAL_TYPE_PAIR_SINGLE, 0x0f65, 0x0fb5, {0}, {0}, 0, 0 },
    { "s",    false, VAL_TYPE_PAIR_SINGLE, 0x0f66, 0x0fb6, {0}, {0}, 0, 0 },
    { "h",    false, VAL_TYPE_PAIR_SINGLE, 0x0f67, 0x0fb7, {0}, {0}, 0, 0 },

    { "W",    false, VAL_TYPE_PAIR_SINGLE, 0x0f5d, 0x0fba, {0}, {0}, 0, 0 },
    { "Y",    false, VAL_TYPE_PAIR_SINGLE, 0x0f61, 0x0fbb, {0}, {0}, 0, 0 },
    { "R",    false, VAL_TYPE_PAIR_SINGLE, 0x0f6a, 0x0fbc, {0}, {0}, 0, 0 },
    { "f",    false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f55, 0x0f39}, {'f'}, 2, 1 },
    { "v",    false, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f56, 0x0f39}, {'v'}, 2, 1 },

    /* 2. Vowels */
    { "'i",   true, VAL_TYPE_VOWEL_LIST, 0, 0, {0x0f60, 0x0f72}, {0}, 2, 0 },
    { "'o",   true, VAL_TYPE_VOWEL_LIST, 0, 0, {0x0f60, 0x0f7c}, {0}, 2, 0 },
    { "+'i",  true, VAL_TYPE_VOWEL, 0x0f72, 0, {0}, {0}, 0, 0 },
    { "'u",   true, VAL_TYPE_VOWEL_LIST, 0, 0, {0x0f60, 0x0f74}, {0}, 2, 0 },
    { "'a",   true, VAL_TYPE_VOWEL, 0x0f71, 0, {0}, {0}, 0, 0 },
    { "ai",   true, VAL_TYPE_VOWEL, 0x0f7b, 0, {0}, {0}, 0, 0 },
    { "au",   true, VAL_TYPE_VOWEL, 0x0f7d, 0, {0}, {0}, 0, 0 },
    { "-I",   true, VAL_TYPE_VOWEL_LIST, 0, 0, {0x0f71, 0x0f80}, {0}, 2, 0 },
    { "-i",   true, VAL_TYPE_VOWEL, 0x0f80, 0, {0}, {0}, 0, 0 },
    { "I",    true, VAL_TYPE_VOWEL_LIST, 0, 0, {0x0f71, 0x0f72}, {0}, 2, 0 },
    { "U",    true, VAL_TYPE_VOWEL_LIST, 0, 0, {0x0f71, 0x0f74}, {0}, 2, 0 },
    { "a",    true, VAL_TYPE_VOWEL, 0x0f68, 0, {0}, {0}, 0, 0 },
    { "A",    true, VAL_TYPE_VOWEL, 0x0f71, 0, {0}, {0}, 0, 0 },
    { "i",    true, VAL_TYPE_VOWEL, 0x0f72, 0, {0}, {0}, 0, 0 },
    { "u",    true, VAL_TYPE_VOWEL, 0x0f74, 0, {0}, {0}, 0, 0 },
    { "e",    true, VAL_TYPE_VOWEL, 0x0f7a, 0, {0}, {0}, 0, 0 },
    { "o",    true, VAL_TYPE_VOWEL, 0x0f7c, 0, {0}, {0}, 0, 0 },

    /* 3. Sanskrit Vowels */
    { "r-I",  true, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f62, 0x0f71, 0x0f80}, {0x0fb2, 0x0f71, 0x0f80}, 3, 3 },
    { "r-i",  true, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f62, 0x0f80}, {0x0fb2, 0x0f80}, 2, 2 },
    { "l-I",  true, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f63, 0x0f71, 0x0f80}, {0x0fb3, 0x0f71, 0x0f80}, 3, 3 },
    { "l-i",  true, VAL_TYPE_PAIR_LIST, 0, 0, {0x0f63, 0x0f80}, {0x0fb3, 0x0f80}, 2, 2 },

    /* 4. Final Diacritics */
    { "~M`",  false, VAL_TYPE_OTHER_INT, 0x0f82, 0, {0}, {0}, 0, 0 },
    { "~M",   false, VAL_TYPE_OTHER_INT, 0x0f83, 0, {0}, {0}, 0, 0 },
    { "~X",   false, VAL_TYPE_OTHER_INT, 0x0f35, 0, {0}, {0}, 0, 0 },
    { "M",    false, VAL_TYPE_OTHER_INT, 0x0f7e, 0, {0}, {0}, 0, 0 },
    { "H",    false, VAL_TYPE_OTHER_INT, 0x0f7f, 0, {0}, {0}, 0, 0 },
    { "?",    false, VAL_TYPE_OTHER_INT, 0x0f84, 0, {0}, {0}, 0, 0 },
    { "X",    false, VAL_TYPE_OTHER_INT, 0x0f37, 0, {0}, {0}, 0, 0 },
    { "^",    false, VAL_TYPE_OTHER_INT, 0x0f39, 0, {0}, {0}, 0, 0 },

    /* 5. Other (Punctuation, Numbers, Operators) */
    { "//",   false, VAL_TYPE_OTHER_INT, 0x0f0e, 0, {0}, {0}, 0, 0 },
    { " ",    false, VAL_TYPE_TSHEG,     0x0f0b, 0, {0}, {0}, 0, 0 },
    { "*",    false, VAL_TYPE_OTHER_INT, 0x0f0c, 0, {0}, {0}, 0, 0 },
    { "/",    false, VAL_TYPE_OTHER_INT, 0x0f0d, 0, {0}, {0}, 0, 0 },
    { ";",    false, VAL_TYPE_OTHER_INT, 0x0f0f, 0, {0}, {0}, 0, 0 },
    { "|",    false, VAL_TYPE_OTHER_INT, 0x0f11, 0, {0}, {0}, 0, 0 },
    { "!",    false, VAL_TYPE_OTHER_INT, 0x0f08, 0, {0}, {0}, 0, 0 },
    { ":",    false, VAL_TYPE_OTHER_INT, 0x0f14, 0, {0}, {0}, 0, 0 },
    { "+",    false, VAL_TYPE_PLUS,      0,      0, {0}, {0}, 0, 0 },
    { "_",    false, VAL_TYPE_UNDERSCORE, 0x0020, 0, {0}, {0}, 0, 0 },
    { ".",    false, VAL_TYPE_DOT,       0,      0, {0}, {0}, 0, 0 },
    { "=",    false, VAL_TYPE_OTHER_INT, 0x0f34, 0, {0}, {0}, 0, 0 },
    { "<",    false, VAL_TYPE_OTHER_INT, 0x0f3a, 0, {0}, {0}, 0, 0 },
    { ">",    false, VAL_TYPE_OTHER_INT, 0x0f3b, 0, {0}, {0}, 0, 0 },
    { "(",    false, VAL_TYPE_OTHER_INT, 0x0f3c, 0, {0}, {0}, 0, 0 },
    { ")",    false, VAL_TYPE_OTHER_INT, 0x0f3d, 0, {0}, {0}, 0, 0 },
    { "@",    false, VAL_TYPE_OTHER_INT, 0x0f04, 0, {0}, {0}, 0, 0 },
    { "#",    false, VAL_TYPE_OTHER_INT, 0x0f05, 0, {0}, {0}, 0, 0 },
    { "$",    false, VAL_TYPE_OTHER_INT, 0x0f06, 0, {0}, {0}, 0, 0 },
    { "%",    false, VAL_TYPE_OTHER_INT, 0x0f07, 0, {0}, {0}, 0, 0 },
    { "0",    false, VAL_TYPE_OTHER_INT, 0x0f20, 0, {0}, {0}, 0, 0 },
    { "1",    false, VAL_TYPE_OTHER_INT, 0x0f21, 0, {0}, {0}, 0, 0 },
    { "2",    false, VAL_TYPE_OTHER_INT, 0x0f22, 0, {0}, {0}, 0, 0 },
    { "3",    false, VAL_TYPE_OTHER_INT, 0x0f23, 0, {0}, {0}, 0, 0 },
    { "4",    false, VAL_TYPE_OTHER_INT, 0x0f24, 0, {0}, {0}, 0, 0 },
    { "5",    false, VAL_TYPE_OTHER_INT, 0x0f25, 0, {0}, {0}, 0, 0 },
    { "6",    false, VAL_TYPE_OTHER_INT, 0x0f26, 0, {0}, {0}, 0, 0 },
    { "7",    false, VAL_TYPE_OTHER_INT, 0x0f27, 0, {0}, {0}, 0, 0 },
    { "8",    false, VAL_TYPE_OTHER_INT, 0x0f28, 0, {0}, {0}, 0, 0 },
    { "9",    false, VAL_TYPE_OTHER_INT, 0x0f29, 0, {0}, {0}, 0, 0 },

    { NULL,   false, VAL_TYPE_NONE,      0,      0, {0}, {0}, 0, 0 }
};

static int write_utf8(uint32_t cp, char *out, size_t max, size_t *pos) {
    if (cp == 0) return 0;
    if (cp < 0x80) {
        if (*pos + 1 >= max) return -1;
        out[(*pos)++] = (char)cp;
    } else if (cp < 0x800) {
        if (*pos + 2 >= max) return -1;
        out[(*pos)++] = (char)(0xC0 | (cp >> 6));
        out[(*pos)++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        if (*pos + 3 >= max) return -1;
        out[(*pos)++] = (char)(0xE0 | (cp >> 12));
        out[(*pos)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[(*pos)++] = (char)(0x80 | (cp & 0x3F));
    } else {
        if (*pos + 4 >= max) return -1;
        out[(*pos)++] = (char)(0xF0 | (cp >> 18));
        out[(*pos)++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[(*pos)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[(*pos)++] = (char)(0x80 | (cp & 0x3F));
    }
    return 0;
}

int wylie_to_tibetan(const char *wylie_in, char *utf8_out, size_t max_out) {
    if (!wylie_in || !utf8_out || max_out == 0) return -1;

    size_t out_pos = 0;
    size_t in_len = strlen(wylie_in);
    size_t i = 0;
    int P = 0; /* Stack counter: 0 = base, >0 = subjoined */

    while (i < in_len) {
        /* Clear stack when P > 4 (matches wylie.erl: t/5 when P > 4 -> t(N, S, D, L, 0)) */
        if (P > 4) {
            P = 0;
        }

        const WylieRule *matched = NULL;
        size_t match_len = 0;

        /* Longest prefix match from 4 down to 1 */
        for (int n = 4; n >= 1; n--) {
            if (i + n > in_len) continue;
            for (int k = 0; g_rules[k].key != NULL; k++) {
                if (strlen(g_rules[k].key) == (size_t)n &&
                    strncmp(&wylie_in[i], g_rules[k].key, n) == 0) {
                    matched = &g_rules[k];
                    match_len = n;
                    break;
                }
            }
            if (matched) break;
        }

        if (!matched) {
            /* Fallback: write single char literally and reset stack */
            write_utf8((uint8_t)wylie_in[i], utf8_out, max_out, &out_pos);
            i++;
            P = 0;
            continue;
        }

        const char *key = matched->key;
        int next_stack = P;

        switch (matched->kind) {
            case VAL_TYPE_TSHEG:
                /* Space -> Tsheg 0x0F0B, resets syllable stack to 0 */
                write_utf8(0x0f0b, utf8_out, max_out, &out_pos);
                next_stack = 0;
                break;

            case VAL_TYPE_DOT:
                /* "." -> explicit non-stacking delimiter, stack=0 */
                next_stack = 0;
                break;

            case VAL_TYPE_UNDERSCORE:
                /* "_" -> space 0x0020, resets syllable stack to 0 */
                write_utf8(0x0020, utf8_out, max_out, &out_pos);
                next_stack = 0;
                break;

            case VAL_TYPE_PLUS:
                /* "+" -> empty, stack=P+1 */
                next_stack = P + 1;
                break;

            case VAL_TYPE_PAIR_SINGLE: {
                uint32_t A = matched->val1;
                uint32_t B = matched->val2;
                if (P == 0) {
                    write_utf8(A, utf8_out, max_out, &out_pos);
                    next_stack = (key[0] == '\'') ? 5 : (P + 1);
                } else {
                    write_utf8(B, utf8_out, max_out, &out_pos);
                    next_stack = (key[0] == '\'') ? 5 : (P + 1);
                }
                break;
            }

            case VAL_TYPE_PAIR_LIST: {
                if (P == 0) {
                    for (int l = 0; l < matched->len1; l++) {
                        write_utf8(matched->list1[l], utf8_out, max_out, &out_pos);
                    }
                    next_stack = (key[0] == '\'') ? 5 : (P + 1);
                } else {
                    for (int l = 0; l < matched->len2; l++) {
                        write_utf8(matched->list2[l], utf8_out, max_out, &out_pos);
                    }
                    next_stack = (key[0] == '\'') ? 5 : (P + 1);
                }
                break;
            }

            case VAL_TYPE_VOWEL: {
                uint32_t val = matched->val1;
                if (strcmp(key, "a") == 0) {
                    if (P == 0) {
                        /* Standalone 'a' at start of syllable -> 0x0F68 (a-chen) */
                        write_utf8(0x0f68, utf8_out, max_out, &out_pos);
                        next_stack = 5;
                    } else {
                        /* Inherent 'a' after consonant: omitted */
                        next_stack = 5;
                    }
                } else {
                    if (P == 0) {
                        /* Standalone vowel at start of syllable -> a-chen + vowel diacritic */
                        write_utf8(0x0f68, utf8_out, max_out, &out_pos);
                        write_utf8(val, utf8_out, max_out, &out_pos);
                        next_stack = 5;
                    } else {
                        /* Vowel diacritic on active consonant */
                        write_utf8(val, utf8_out, max_out, &out_pos);
                        next_stack = 5;
                    }
                }
                break;
            }

            case VAL_TYPE_VOWEL_LIST: {
                if (P == 0) {
                    /* Standalone initial composite vowel (e.g. I, U, 'i) */
                    for (int l = 0; l < matched->len1; l++) {
                        write_utf8(matched->list1[l], utf8_out, max_out, &out_pos);
                    }
                    next_stack = 5;
                } else {
                    for (int l = 0; l < matched->len1; l++) {
                        write_utf8(matched->list1[l], utf8_out, max_out, &out_pos);
                    }
                    next_stack = 5;
                }
                break;
            }

            case VAL_TYPE_OTHER_INT: {
                uint32_t val = matched->val1;
                write_utf8(val, utf8_out, max_out, &out_pos);
                next_stack = 5;
                break;
            }

            case VAL_TYPE_LIST: {
                for (int l = 0; l < matched->len1; l++) {
                    write_utf8(matched->list1[l], utf8_out, max_out, &out_pos);
                }
                next_stack = P + 1;
                break;
            }

            default:
                break;
        }

        i += match_len;
        P = next_stack;
    }

    if (out_pos < max_out) {
        utf8_out[out_pos] = '\0';
    } else {
        utf8_out[max_out - 1] = '\0';
    }
    return (int)out_pos;
}

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
int wylie_run_tests(void) {
    char buf[512];
    int failed = 0;

    struct TestCase {
        const char *wylie;
        const char *expected_utf8;
    } tests[] = {
        { "oM AH hU~M", "ཨོཾ་ཨཱཿ་ཧཱུྃ" },
        { "klong chen snying thig", "ཀློང་ཆེན་སྙིང་ཐིག" },
        { "rnam dag ston pa", "རྣམ་དག་སྟོན་པ" },
        { "oM ar+g+haM pad+yaM puSh+pe d+hup+pe a lo ke gan+d+he nai wit+ye shab+ta pra tits+tsha ye swA hA:",
          "ཨོཾ་ཨརྒྷཾ་པདྱཾ་པུཥྤེ་དྷུཔྤེ་ཨ་ལོ་ཀེ་གནྡྷེ་ནཻ་ཝིཏྱེ་ཤབྟ་པྲ་ཏིཙྪ་ཡེ་སྭཱ་ཧཱ༔" },
        { "tad+ya thA:_oM mu ne mu ne ma hA mu ne shAkya mu ne ye swA hA:",
          "ཏདྱ་ཐཱ༔ ཨོཾ་མུ་ནེ་མུ་ནེ་མ་ཧཱ་མུ་ནེ་ཤཱཀྱ་མུ་ནེ་ཡེ་སྭཱ་ཧཱ༔" },
        { "badz+ra sa ma yA dza:_dza: hU~M baM ho:_pad+ma ka ma la ye stwaM:",
          "བཛྲ་ས་མ་ཡཱ་ཛ༔ ཛ༔་ཧཱུྃ་བཾ་ཧོ༔ པདྨ་ཀ་མ་ལ་ཡེ་སྟྭཾ༔" },
        { "oM ye d+har+mA he tu pra b+ha wA he tun te Shan+ta thA ga to ha+ya wa data",
          "ཨོཾ་ཡེ་དྷརྨཱ་ཧེ་ཏུ་པྲ་བྷ་ཝཱ་ཧེ་ཏུན་ཏེ་ཥནྟ་ཐཱ་ག་ཏོ་ཧྱ་ཝ་དཏ" },
        { NULL, NULL }
    };

    printf("====================================================\n");
    printf(" Kernel TIP Extended Wylie (EWTS) Converter Unit Tests\n");
    printf("====================================================\n");

    for (int i = 0; tests[i].wylie != NULL; i++) {
        memset(buf, 0, sizeof(buf));
        wylie_to_tibetan(tests[i].wylie, buf, sizeof(buf));
        if (strcmp(buf, tests[i].expected_utf8) == 0) {
            printf("  [PASS] Test %d: '%s' -> '%s'\n", i + 1, tests[i].wylie, buf);
        } else {
            printf("  [FAIL] Test %d: '%s'\n         Got:      '%s'\n         Expected: '%s'\n",
                   i + 1, tests[i].wylie, buf, tests[i].expected_utf8);
            failed++;
        }
    }

    printf("====================================================\n");
    printf(" Test Results: %d / %d tests passed (%.1f%%)\n",
           7 - failed, 7, ((7.0 - failed) / 7.0) * 100.0);
    printf("====================================================\n");
    return failed;
}

#else
int wylie_run_tests(void) { return 0; }
#endif
