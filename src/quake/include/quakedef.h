/*
 * src/quake/include/quakedef.h — Master Quake Engine Definitions
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#ifndef QUAKE_QUAKEDEF_H
#define QUAKE_QUAKEDEF_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include <btron/types.h>
#include <btron/btron.h>
#include "btron_quake.h"
#include "mathlib.h"

typedef unsigned char byte;

#ifndef qboolean
typedef enum { qfalse = 0, qtrue = 1 } qboolean;
#endif

#define MAX_NUM_ARGVS 50
#define MAX_OSPATH     256
#define MAX_QPATH      64

/* Engine Constants */
#define QUAKE_VERSION   1.09f
#define TICRATE         0.0142857f /* 70 Hz physics tick */
#define MAX_EDICTS      600
#define MAX_LIGHTSTYLES 64
#define MAX_MODELS      256
#define MAX_SOUNDS      256

/* Logging & Diagnostics */
void Sys_Error(const char *error, ...);
void Con_Printf(const char *fmt, ...);
void Con_DPrintf(const char *fmt, ...);

/* Memory Allocator (NASA JPL Rule 3 Preallocated Hunk) */
void *Hunk_Alloc(size_t size);
void *Hunk_AllocName(size_t size, const char *name);
int   Hunk_LowMark(void);
void  Hunk_FreeToLowMark(int mark);
void *Zone_Alloc(size_t size);
void  Zone_Free(void *ptr);

static inline int q_tolower(int c) {
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

static inline int q_strcasecmp(const char *s1, const char *s2) {
    if (!s1 || !s2) return (s1 == s2) ? 0 : (s1 ? 1 : -1);
    while (*s1 && *s2) {
        int c1 = q_tolower((unsigned char)*s1);
        int c2 = q_tolower((unsigned char)*s2);
        if (c1 != c2) return c1 - c2;
        s1++; s2++;
    }
    return q_tolower((unsigned char)*s1) - q_tolower((unsigned char)*s2);
}

static inline float q_atof(const char *str) {
    if (!str) return 0.0f;
    while (*str == ' ' || *str == '\t') str++;
    float sign = 1.0f;
    if (*str == '-') { sign = -1.0f; str++; }
    else if (*str == '+') { str++; }
    float val = 0.0f;
    while (*str >= '0' && *str <= '9') {
        val = val * 10.0f + (float)(*str - '0');
        str++;
    }
    if (*str == '.') {
        str++;
        float factor = 0.1f;
        while (*str >= '0' && *str <= '9') {
            val += (float)(*str - '0') * factor;
            factor *= 0.1f;
            str++;
        }
    }
    return val * sign;
}

static inline int q_atoi(const char *str) {
    return (int)q_atof(str);
}

static inline const char *q_strchr(const char *s, int c) {
    if (!s) return NULL;
    while (*s) {
        if (*s == (char)c) return s;
        s++;
    }
    return (c == 0) ? s : NULL;
}

static inline const char *q_strrchr(const char *s, int c) {
    if (!s) return NULL;
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    return (c == 0) ? s : last;
}

static inline const char *q_strstr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
            const char *h = haystack;
            const char *n = needle;
            while (*h && *n && *h == *n) {
                h++;
                n++;
            }
            if (!*n) return haystack;
        }
    }
    return NULL;
}

#endif /* QUAKE_QUAKEDEF_H */
