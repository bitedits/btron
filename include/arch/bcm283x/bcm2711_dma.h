#ifndef _BCM2711_DMA_H_
#define _BCM2711_DMA_H_

#include <stdint.h>
#include <stddef.h>

/* BCM2711 DMA Controller Register Offsets (relative to DMA channel base) */
#define BCM_DMA_CS             0x00
#define BCM_DMA_CONBLK_AD      0x04
#define BCM_DMA_TI             0x08
#define BCM_DMA_SOURCE_AD      0x0C
#define BCM_DMA_DEST_AD        0x10
#define BCM_DMA_TXFR_LEN       0x14
#define BCM_DMA_STRIDE         0x18
#define BCM_DMA_NEXTCONBK      0x1C
#define BCM_DMA_DEBUG          0x20

/* DMA Control and Status (CS) bits */
#define BCM_DMA_CS_RESET       (1u << 31)
#define BCM_DMA_CS_ABORT       (1u << 30)
#define BCM_DMA_CS_DISDEBUG    (1u << 29)
#define BCM_DMA_CS_WAIT_WR     (1u << 28)
#define BCM_DMA_CS_PANIC_PRI(p) (((p) & 0xF) << 20)
#define BCM_DMA_CS_PRIORITY(p)  (((p) & 0xF) << 16)
#define BCM_DMA_CS_ERROR       (1u << 8)
#define BCM_DMA_CS_WAITING_WR  (1u << 6)
#define BCM_DMA_CS_DREQ_STOPS  (1u << 5)
#define BCM_DMA_CS_PAUSED      (1u << 4)
#define BCM_DMA_CS_DREQ        (1u << 3)
#define BCM_DMA_CS_INT         (1u << 2)
#define BCM_DMA_CS_END         (1u << 1)
#define BCM_DMA_CS_ACTIVE      (1u << 0)

/* Transfer Information (TI) bits */
#define BCM_DMA_TI_NO_WIDE_BURSTS (1u << 26)
#define BCM_DMA_TI_WAITS(w)    (((w) & 0x1F) << 21)
#define BCM_DMA_TI_PERMAP(p)   (((p) & 0x1F) << 16)
#define BCM_DMA_TI_BURST_LEN(b) (((b) & 0x0F) << 12)
#define BCM_DMA_TI_SRC_IGNORE  (1u << 11)
#define BCM_DMA_TI_SRC_DREQ    (1u << 10)
#define BCM_DMA_TI_SRC_WIDTH   (1u << 9)  /* 1 = 128-bit, 0 = 32-bit */
#define BCM_DMA_TI_SRC_INC     (1u << 8)
#define BCM_DMA_TI_DEST_IGNORE (1u << 7)
#define BCM_DMA_TI_DEST_DREQ   (1u << 6)
#define BCM_DMA_TI_DEST_WIDTH  (1u << 5)  /* 1 = 128-bit, 0 = 32-bit */
#define BCM_DMA_TI_DEST_INC    (1u << 4)
#define BCM_DMA_TI_WAIT_RESP   (1u << 3)
#define BCM_DMA_TI_TDMODE      (1u << 1)  /* 2D Stride Mode */
#define BCM_DMA_TI_INTEN       (1u << 0)

/* 2D Stride BitBlt Control Block (32 bytes, 256-bit aligned in non-cacheable RAM) */
typedef struct __attribute__((aligned(32))) {
    uint32_t ti;          /* Transfer Information */
    uint32_t source_ad;   /* Source physical / bus address */
    uint32_t dest_ad;     /* Destination physical / bus address */
    uint32_t txfr_len;    /* In 2D mode: (Y_len << 16) | X_len_bytes */
    uint32_t stride;      /* (dst_stride << 16) | src_stride */
    uint32_t nextconbk;   /* Next Control Block address */
    uint32_t reserved[2];
} bcm2711_dma_cb_t;

/* API Prototypes */
int  bcm2711_dma_init(void);
void bcm2711_dma_wait(int channel);
int  bcm2711_dma_blit2d(int channel,
                        uintptr_t dst_addr, int dst_stride,
                        uintptr_t src_addr, int src_stride,
                        uint32_t width_bytes, uint32_t height_rows);
int  bcm2711_dma_blit(void *dst_phys, const void *src_phys, size_t bytes);
int  bcm2711_dma_blit_linear(int channel,
                             uintptr_t dst_addr,
                             uintptr_t src_addr,
                             uint32_t total_bytes);

#endif /* _BCM2711_DMA_H_ */
