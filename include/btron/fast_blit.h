#ifndef _BTRON_FAST_BLIT_H_
#define _BTRON_FAST_BLIT_H_

#include <stddef.h>
#include <stdint.h>

static inline void btron_row_blit(void *dst, const void *src, size_t bytes) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

#if defined(__aarch64__)
    /* Damage rects start at an arbitrary pixel column, so a row is usually
     * 4-mod-16 rather than 16-aligned.  Without a peel both alignment tests
     * below fail and the row copies one BYTE per access -- and a store to the
     * non-cacheable framebuffer does not batch, so a present became one bus
     * transaction per byte.  Source and destination always share the same
     * phase here (same rect offset into two row-aligned buffers), so peel to
     * that phase and use the vector loop. */
    if ((((uintptr_t)d ^ (uintptr_t)s) & 15u) == 0u) {
        size_t peel = (16u - ((uintptr_t)d & 15u)) & 15u;
        if (peel > bytes) peel = bytes;
        for (size_t i = 0; i < peel; i++) d[i] = s[i];
        d += peel;
        s += peel;
        bytes -= peel;

        while (bytes >= 64u) {
            __asm__ volatile(
                "ldp q0, q1, [%[src]], #32\n\t"
                "ldp q2, q3, [%[src]], #32\n\t"
                "stp q0, q1, [%[dst]], #32\n\t"
                "stp q2, q3, [%[dst]], #32\n\t"
                : [dst] "+r"(d), [src] "+r"(s)
                :
                : "v0", "v1", "v2", "v3", "memory");
            bytes -= 64u;
        }
    }
#endif

    if ((((uintptr_t)d | (uintptr_t)s) & 7u) == 0) {
        while (bytes >= 8u) {
            *(uint64_t *)d = *(const uint64_t *)s;
            d += 8;
            s += 8;
            bytes -= 8u;
        }
    }
    while (bytes--) *d++ = *s++;
}

#endif /* _BTRON_FAST_BLIT_H_ */
