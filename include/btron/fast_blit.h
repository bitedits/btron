#ifndef _BTRON_FAST_BLIT_H_
#define _BTRON_FAST_BLIT_H_

#include <stddef.h>
#include <stdint.h>

static inline void btron_row_blit(void *dst, const void *src, size_t bytes) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

#if defined(__aarch64__)
    if ((((uintptr_t)d | (uintptr_t)s) & 15u) == 0) {
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
