/*
 * B-System BTRON3 — cluc.h
 * Shared declarations for the CLU terminal applications
 * (tty, lang, term, vfs, tv, sc). The libc/hosted split itself lives in
 * <btron/libc_shim.h>; do not restate it here.
 *
 * Rules the whole CLU terminal stack follows (NASA/JPL "Power of Ten"):
 *   - no recursion, no goto, no setjmp
 *   - every loop has a fixed upper bound
 *   - no dynamic allocation: all storage is static and sized by macros
 *   - every return value that can fail is checked
 *   - all string output goes through bounded snprintf/memcpy
 */
#ifndef CLUC_H
#define CLUC_H

#include <btron/libc_shim.h>

/* CLU code tests hostedness through this name; it is the shim's flag. */
#define CLU_HOSTED BTRON_HOSTED

/* Clamp helper used by layout code. */
#define CLU_CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))

#endif /* CLUC_H */
