#ifndef _BTRON_ARM64_MEM_H_
#define _BTRON_ARM64_MEM_H_

/*
 * Coherent Non-Cacheable RAM window for DMA and VideoCore mailbox structures.
 *
 * arm64_mmu_init() maps exactly this 2 MB range with MAIR attr2 (Normal
 * Non-Cacheable), along with whatever span the VideoCore allocated for the
 * scanout framebuffer; the rest of the cacheable low RAM is attr1 (Normal,
 * inner and outer write-back).  The window used to sit at 16 MB, which is
 * inside .bss, so the
 * tail of the static data segment silently ran uncached; it now lives in the
 * gap between __bss_end and the bump heap, and arm64_mmu_layout_ok() reports
 * whether that is still true (the on-device MEM band prints it as `ok1`).
 */
#define BTRON_NOCACHE_BASE  0x01800000UL   /* 24 MB */
#define BTRON_NOCACHE_SIZE  0x00200000UL   /*  2 MB */
#define BTRON_NOCACHE_END   (BTRON_NOCACHE_BASE + BTRON_NOCACHE_SIZE)

/* Mailbox request buffers shared with the VideoCore property channel.  The
 * boot buffer carries the framebuffer-allocation and power-state requests,
 * whose *responses* are written by the VideoCore into this RAM, so it must
 * never be cacheable.
 *
 * These sit at +512K rather than at the bottom of the window because xHCI
 * claims 0..0x70000 of it (DCBAA, contexts, EP0/EP1 rings for 8 slots,
 * scratch pages) and zeroes 0..0x40000 on every host-controller reset; the
 * old +0xF000 offsets landed inside slot 8's EP0 ring. */
#define BTRON_NOCACHE_MBOX_BOOT  (BTRON_NOCACHE_BASE + 0x80000UL)
#define BTRON_NOCACHE_MBOX_ARM   (BTRON_NOCACHE_BASE + 0x81000UL)
#define BTRON_NOCACHE_MBOX_PCIE  (BTRON_NOCACHE_BASE + 0x82000UL)
#define BTRON_NOCACHE_DMA_CB     (BTRON_NOCACHE_BASE + 0x84000UL)

/* Driver objects go here instead of being addressed by hand: link.ld places the
 * image-less `.nocache` output section at this address, so a buffer declared
 * with  __attribute__((section(".btron.nocache")))  simply has a non-cacheable
 * address.  It must stay above every offset above, because nothing arbitrates
 * the two.  arm64_mmu_layout_ok() reports whether it still does, and _start
 * zeroes the section (it carries no ELF image, so the .bss clear does not). */
#define BTRON_NOCACHE_SECTION    (BTRON_NOCACHE_BASE + 0x90000UL)

#ifndef __ASSEMBLER__
#include <stdint.h>

/* Populate the page tables and turn on the MMU plus both caches.  Must run
 * after .bss is cleared, and after any driver whose buffers the VideoCore or
 * peripheral DMA writes into RAM directly.
 *
 * fb_base/fb_bytes describe the framebuffer the VideoCore actually allocated:
 * the address comes back from a mailbox response (init_pi_framebuffer() masks
 * it into the low 1 GB), so it is not a constant the page tables may assume.
 * Whatever 2 MB range it lands in is mapped Normal Non-Cacheable; the L2 table
 * only covers the first gigabyte, so a framebuffer at or above 1 GB is served
 * by the 3 GB L1 Device block and is left out of L2.  Passing 0 skips the
 * span. */
void arm64_mmu_init(uintptr_t fb_base, uint32_t fb_bytes);

/* MAIR AttrIndx the page tables give this virtual address: 1 = write-back
 * cacheable, 2 = non-cacheable, 0 = Device.  Negative if unmapped. */
int arm64_mmu_attr_of(uintptr_t va);

/* 0 if .bss or the bump heap has grown into the non-cacheable window, or if
 * the .nocache section has grown out of it. */
int arm64_mmu_layout_ok(void);
#endif

#endif /* _BTRON_ARM64_MEM_H_ */
