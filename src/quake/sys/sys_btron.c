/*
 * src/quake/sys/sys_btron.c — B-System Operating System Layer for Quake
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#include "../include/quakedef.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#if BTRON_TARGET == 0
#include <sys/time.h>
#endif

extern void uart_puts_raw(const char *s);
extern void Con_LogAppend(const char *msg);

void Con_Printf(const char *fmt, ...) {
    char buf[512];
    va_list argptr;
    va_start(argptr, fmt);
    vsnprintf(buf, sizeof(buf), fmt, argptr);
    va_end(argptr);

    uart_puts_raw(buf);
    Con_LogAppend(buf);
}

void Con_DPrintf(const char *fmt, ...) {
    /* Debug output */
    char buf[512];
    va_list argptr;
    va_start(argptr, fmt);
    vsnprintf(buf, sizeof(buf), fmt, argptr);
    va_end(argptr);

    uart_puts_raw(buf);
}

void Sys_Error(const char *error, ...) {
    char buf[512];
    va_list argptr;
    va_start(argptr, error);
    vsnprintf(buf, sizeof(buf), error, argptr);
    va_end(argptr);

    uart_puts_raw("\n[FATAL QUAKE ERROR] ");
    uart_puts_raw(buf);
    uart_puts_raw("\n");
}

double Sys_DoubleTime(void) {
#if BTRON_TARGET == 0
    struct timeval tp;
    gettimeofday(&tp, NULL);
    static int secbase = 0;
    if (!secbase) secbase = tp.tv_sec;
    return (double)(tp.tv_sec - secbase) + (double)tp.tv_usec / 1000000.0;
#else
    static double s_fake_time = 0.0;
    s_fake_time += 0.016666; /* 60 Hz best effort */
    return s_fake_time;
#endif
}
