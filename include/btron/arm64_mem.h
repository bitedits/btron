#ifndef _BTRON_ARM64_MEM_H_
#define _BTRON_ARM64_MEM_H_

/*
 * Coherent Non-Cacheable RAM window for DMA and VideoCore mailbox structures.
 *
 * arm64_mmu_init() maps exactly this 2 MB range with MAIR attr2 (Normal
 * Non-Cacheable); the rest of the cacheable low RAM is attr1 (Normal, inner
 * and outer write-back), apart from the GPU framebuffer tiles at 960 MB.
 * The window used to sit at 16 MB, which is inside .bss, so the
 * tail of the static data segment silently ran uncached; it now lives in the
 * gap between __bss_end and the bump heap, and arm64_mmu_layout_ok() reports
 * whether that is still true (the on-device MEM band prints it as `ok1`).
 */
#define BTRON_NOCACHE_BASE  0x01800000UL   /* 24 MB */
#define BTRON_NOCACHE_SIZE  0x00200000UL   /*  2 MB */
#define BTRON_NOCACHE_END   (BTRON_NOCACHE_BASE + BTRON_NOCACHE_SIZE)

/* Mailbox request buffers shared with the VideoCore property channel. */
#define BTRON_NOCACHE_DMA_CB   (BTRON_NOCACHE_BASE + 0xF400UL)
#define BTRON_NOCACHE_MBOX_ARM (BTRON_NOCACHE_BASE + 0xF600UL)
#define BTRON_NOCACHE_MBOX_PCIE (BTRON_NOCACHE_BASE + 0xF800UL)

#ifndef __ASSEMBLER__
#include <stdint.h>

/* MAIR AttrIndx the MMU table gives this virtual address: 1 = write-back
 * cacheable, 2 = non-cacheable, 0 = Device.  Negative if unmapped. */
int arm64_mmu_attr_of(uintptr_t va);

/* 0 if .bss or the bump heap has grown into the non-cacheable window. */
int arm64_mmu_layout_ok(void);
#endif

#endif /* _BTRON_ARM64_MEM_H_ */
