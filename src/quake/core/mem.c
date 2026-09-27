/*
 * src/quake/core/mem.c — Deterministic NASA JPL Rule 3 Hunk & Zone Allocator
 *
 * All allocations occur inside a fixed static BSS buffer (32 MiB).
 * Zero runtime malloc() or free() system calls after initialization.
 *
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#include "../include/quakedef.h"

#define HUNK_TOTAL_SIZE (64 * 1024 * 1024)  /* 64 MiB: 18.6 MB pak + BSP + headroom */

static uint8_t s_hunk_buffer[HUNK_TOTAL_SIZE] __attribute__((aligned(4096)));
static size_t  s_hunk_low = 0;
static size_t  s_hunk_high = HUNK_TOTAL_SIZE;

void *Hunk_AllocName(size_t size, const char *name) {
    (void)name;
    /* 16-byte alignment */
    size = (size + 15) & ~15;

    if (s_hunk_low + size > s_hunk_high) {
        Sys_Error("Hunk_Alloc overflow! Requested %u bytes, remaining %u",
                  (unsigned int)size, (unsigned int)(s_hunk_high - s_hunk_low));
        return NULL;
    }

    void *ptr = (void *)&s_hunk_buffer[s_hunk_low];
    s_hunk_low += size;
    memset(ptr, 0, size);
    return ptr;
}

void *Hunk_Alloc(size_t size) {
    return Hunk_AllocName(size, "unnamed");
}

int Hunk_LowMark(void) {
    return (int)s_hunk_low;
}

void Hunk_FreeToLowMark(int mark) {
    if (mark >= 0 && (size_t)mark <= s_hunk_low) {
        s_hunk_low = (size_t)mark;
    }
}

void *Zone_Alloc(size_t size) {
    /* Zone memory is allocated from low hunk */
    return Hunk_AllocName(size, "zone");
}

void Zone_Free(void *ptr) {
    /* NASA JPL Rule 3: static arena does not fragment */
    (void)ptr;
}
