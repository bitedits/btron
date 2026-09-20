/*
 * Broadcom BCM2711 Hardware 2D DMA Engine Driver: bcm2711_dma.c
 * Accelerates 2D Rectangular BitBlts and Framebuffer Transfers on Pi 4B / Pi 400
 */

#include <stdint.h>
#include <stddef.h>
#include <arch/bcm283x/bcm2711_dma.h>

extern uintptr_t g_mmio_base;
extern void uart_puts(const char *s);
extern void uart_hex32(uint32_t val);

#define DMA_CHANNELS_BASE   (g_mmio_base + 0x00007000UL)
#define DMA_ENABLE_REG      (g_mmio_base + 0x00007FF0UL)

/* Place DMA Control Blocks in coherent Non-Cacheable RAM (16MB + 0xF400) */
#define DMA_CB_PHYS_BASE    (0x01000000UL + 0xF400UL)

static volatile bcm2711_dma_cb_t * const s_dma_cbs = (volatile bcm2711_dma_cb_t *)DMA_CB_PHYS_BASE;

static volatile uint32_t* dma_chan_regs(int channel) {
    return (volatile uint32_t *)(DMA_CHANNELS_BASE + (channel * 0x100UL));
}

int bcm2711_dma_init(void) {
    /* 1. Enable DMA Channel 0 and Channel 7 in DMA_ENABLE register */
    volatile uint32_t *dma_en = (volatile uint32_t *)DMA_ENABLE_REG;
    *dma_en |= (1u << 0) | (1u << 7);
    __asm__ volatile("dsb sy" : : : "memory");

    /* 2. Reset DMA Channel 0 & Channel 7 */
    volatile uint32_t *chan0 = dma_chan_regs(0);
    chan0[BCM_DMA_CS / 4] = BCM_DMA_CS_RESET;
    volatile uint32_t *chan7 = dma_chan_regs(7);
    chan7[BCM_DMA_CS / 4] = BCM_DMA_CS_RESET;

    for (volatile int to = 0; to < 1000; to++) {
        if (!(chan0[BCM_DMA_CS / 4] & BCM_DMA_CS_RESET) &&
            !(chan7[BCM_DMA_CS / 4] & BCM_DMA_CS_RESET)) break;
    }

    /* 3. Clear residual END / INT bits */
    chan0[BCM_DMA_CS / 4] = BCM_DMA_CS_END | BCM_DMA_CS_INT;
    chan7[BCM_DMA_CS / 4] = BCM_DMA_CS_END | BCM_DMA_CS_INT;
    __asm__ volatile("dsb sy" : : : "memory");

    uart_puts("[DMA] BCM2711 Hardware 2D DMA Engine (Channels 0 & 7) initialized.\n");
    return 0;
}

int bcm2711_dma_wait_timeout(int channel, uint32_t loops) {
    if (channel < 0 || channel > 14 || loops == 0) return -1;
    volatile uint32_t *chan = dma_chan_regs(channel);

    while ((chan[BCM_DMA_CS / 4] & BCM_DMA_CS_ACTIVE) && loops--) {
        __asm__ volatile("nop");
    }
    if (chan[BCM_DMA_CS / 4] & BCM_DMA_CS_ACTIVE) {
        chan[BCM_DMA_CS / 4] = BCM_DMA_CS_RESET;
        __asm__ volatile("dsb sy" : : : "memory");
        return -2;
    }

    if (chan[BCM_DMA_CS / 4] & BCM_DMA_CS_ERROR) {
        uart_puts("[DMA] ERROR in transfer! CS: ");
        uart_hex32(chan[BCM_DMA_CS / 4]);
        uart_puts(" DEBUG: ");
        uart_hex32(chan[BCM_DMA_DEBUG / 4]);
        uart_puts("\n");
        chan[BCM_DMA_CS / 4] = BCM_DMA_CS_RESET;
        __asm__ volatile("dsb sy" : : : "memory");
        return -3;
    }

    chan[BCM_DMA_CS / 4] = BCM_DMA_CS_END;
    __asm__ volatile("dsb sy" : : : "memory");
    return 0;
}

void bcm2711_dma_abort(int channel) {
    if (channel < 0 || channel > 14) return;
    volatile uint32_t *chan = dma_chan_regs(channel);
    chan[BCM_DMA_CS / 4] = BCM_DMA_CS_ABORT;
    __asm__ volatile("dsb sy" : : : "memory");
    chan[BCM_DMA_CS / 4] = BCM_DMA_CS_RESET;
    __asm__ volatile("dsb sy" : : : "memory");
}

void bcm2711_dma_wait(int channel) {
    (void)bcm2711_dma_wait_timeout(channel, 5000000u);
}

int bcm2711_dma_is_busy(int channel) {
    if (channel < 0 || channel > 14) return 0;
    return (dma_chan_regs(channel)[BCM_DMA_CS / 4] & BCM_DMA_CS_ACTIVE) != 0;
}

int bcm2711_dma_blit2d(int channel,
                        uintptr_t dst_addr, int dst_stride,
                        uintptr_t src_addr, int src_stride,
                        uint32_t width_bytes, uint32_t height_rows) {
    /* Wait for channel to be free */
    bcm2711_dma_wait(channel);
    return bcm2711_dma_blit2d_async(channel, dst_addr, dst_stride, src_addr,
                                    src_stride, width_bytes, height_rows);
}

int bcm2711_dma_blit2d_async(int channel,
                              uintptr_t dst_addr, int dst_stride,
                              uintptr_t src_addr, int src_stride,
                              uint32_t width_bytes, uint32_t height_rows) {
    if (channel < 0 || channel > 14) return -1;
    if (width_bytes == 0 || height_rows == 0) return 0;
    if (dst_stride < (int)width_bytes || src_stride < (int)width_bytes)
        return -1;
    if (bcm2711_dma_is_busy(channel)) return 1;
    bcm2711_dma_wait(channel);

    volatile uint32_t *chan = dma_chan_regs(channel);
    volatile bcm2711_dma_cb_t *cb = &s_dma_cbs[channel];

    /* Fill 2D Stride DMA Control Block */
    cb->ti = BCM_DMA_TI_TDMODE |
             BCM_DMA_TI_SRC_INC |
             BCM_DMA_TI_DEST_INC |
             BCM_DMA_TI_SRC_WIDTH |
             BCM_DMA_TI_DEST_WIDTH |
             BCM_DMA_TI_BURST_LEN(15) |
             BCM_DMA_TI_WAIT_RESP;

    cb->source_ad = (uint32_t)src_addr;
    cb->dest_ad   = (uint32_t)dst_addr;
    cb->txfr_len  = ((height_rows & 0xFFFF) << 16) | (width_bytes & 0xFFFF);
    cb->stride    = (((dst_stride - (int)width_bytes) & 0xFFFF) << 16) |
                  ((src_stride - (int)width_bytes) & 0xFFFF);
    cb->nextconbk = 0;

    __asm__ volatile("dsb sy" : : : "memory");

    /* Point DMA engine to Control Block */
    uint32_t cb_phys = (uint32_t)(uintptr_t)cb;
    chan[BCM_DMA_CONBLK_AD / 4] = cb_phys;

    /* Start transfer with high priority (8) */
    chan[BCM_DMA_CS / 4] = BCM_DMA_CS_ACTIVE | BCM_DMA_CS_WAIT_WR | BCM_DMA_CS_PRIORITY(8);
    __asm__ volatile("dsb sy" : : : "memory");

    return 0;
}

int bcm2711_dma_blit_linear(int channel,
                             uintptr_t dst_addr,
                             uintptr_t src_addr,
                             uint32_t total_bytes) {
    /* Wait for previous transfer on this channel to complete */
    bcm2711_dma_wait(channel);
    return bcm2711_dma_blit_linear_async(channel, dst_addr, src_addr, total_bytes);
}

int bcm2711_dma_blit_linear_async(int channel,
                                   uintptr_t dst_addr,
                                   uintptr_t src_addr,
                                   uint32_t total_bytes) {
    if (channel < 0 || channel > 14) return -1;
    if (total_bytes == 0) return 0;
    if (bcm2711_dma_is_busy(channel)) return 1;

    /* Ack END/error from the prior transfer without waiting for a new one. */
    bcm2711_dma_wait(channel);

    volatile uint32_t *chan = dma_chan_regs(channel);
    volatile bcm2711_dma_cb_t *cb = &s_dma_cbs[channel];

    /* Fill Linear Burst DMA Control Block */
    cb->ti = BCM_DMA_TI_SRC_INC |
             BCM_DMA_TI_DEST_INC |
             BCM_DMA_TI_SRC_WIDTH |
             BCM_DMA_TI_DEST_WIDTH |
             BCM_DMA_TI_BURST_LEN(15) |
             BCM_DMA_TI_WAIT_RESP;

    cb->source_ad = (uint32_t)src_addr;
    cb->dest_ad   = (uint32_t)dst_addr;
    cb->txfr_len  = total_bytes;
    cb->stride    = 0;
    cb->nextconbk = 0;

    __asm__ volatile("dsb sy" : : : "memory");

    /* Point DMA engine to Control Block */
    uint32_t cb_phys = (uint32_t)(uintptr_t)cb;
    chan[BCM_DMA_CONBLK_AD / 4] = cb_phys;

    /* Start transfer */
    chan[BCM_DMA_CS / 4] = BCM_DMA_CS_ACTIVE | BCM_DMA_CS_WAIT_WR | BCM_DMA_CS_PRIORITY(8);
    __asm__ volatile("dsb sy" : : : "memory");

    return 0;
}

int bcm2711_dma_blit(void *dst_phys, const void *src_phys, size_t bytes) {
    return bcm2711_dma_blit_linear(0, (uintptr_t)dst_phys, (uintptr_t)src_phys, (uint32_t)bytes);
}
