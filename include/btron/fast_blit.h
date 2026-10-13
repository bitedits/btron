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

    /* The same column the aarch64 peel above pays for, for every other target, and
     * this one is measured rather than reasoned: on 2026-10-13 the PS2's bench moved
     * one 399x16 band down a ladder of loop shapes on the Emotion Engine and got
     * 96.4 ns/px through the byte tail below, 21.2 peeled to the shared phase and
     * taken 8 bytes per iteration, 16.9 at 16 and 16.1 at 32 -- against 27.8 ns/px
     * for the same header's already-aligned form, which was slower than the peeled
     * one because clang inlined it into the compositor's row loop.  A damage rect
     * starts at an arbitrary pixel column, so the tail was the common case, not the
     * exception.  src/cores/core_ps2.c's RAM-to-RAM rows are the instrument, and the
     * width below is what they chose: the step from 16 to 32 is 5% and reproduced at
     * both column parities and at the size `bg=` actually bills (12,800 px: 16.5 ->
     * 15.6), so it is taken, while the step from 8 to 16 is 25% and the step from the
     * tail to 8 is 4.5x. */
    if ((((uintptr_t)d ^ (uintptr_t)s) & 7u) == 0u) {
        size_t peel = (8u - ((uintptr_t)d & 7u)) & 7u;
        if (peel > bytes) peel = bytes;
        for (size_t i = 0; i < peel; i++) d[i] = s[i];
        d += peel;
        s += peel;
        bytes -= peel;
    }

    /* Only when BOTH pointers are 8-aligned, because a 64-bit access is what the
     * body is made of: sharing a phase is enough to peel to one, not enough to
     * take the wide loop.  On MIPS the two halves of the 64-bit access are separate
     * 4-byte loads and stores, so no 16-byte alignment is needed here and none is
     * asked for -- which is also why the 32-byte rung needs no wider alignment than
     * the 8-byte one. */
    if ((((uintptr_t)d | (uintptr_t)s) & 7u) == 0) {
        while (bytes >= 32u) {
            *(uint64_t *)d         = *(const uint64_t *)s;
            *(uint64_t *)(d +  8u) = *(const uint64_t *)(s +  8u);
            *(uint64_t *)(d + 16u) = *(const uint64_t *)(s + 16u);
            *(uint64_t *)(d + 24u) = *(const uint64_t *)(s + 24u);
            d += 32;
            s += 32;
            bytes -= 32u;
        }
        if (bytes >= 16u) {
            *(uint64_t *)d = *(const uint64_t *)s;
            *(uint64_t *)(d + 8u) = *(const uint64_t *)(s + 8u);
            d += 16;
            s += 16;
            bytes -= 16u;
        }
        if (bytes >= 8u) {
            *(uint64_t *)d = *(const uint64_t *)s;
            d += 8;
            s += 8;
            bytes -= 8u;
        }
    }
    while (bytes--) *d++ = *s++;
}

#endif /* _BTRON_FAST_BLIT_H_ */
