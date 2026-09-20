# BTRON VideoCore & 2D DMA Hardware Acceleration Specification (BCM2711 / Pi 400)

## 1. Architectural Overview

This document specifies the hardware-accelerated 2D graphics and display presentation pipeline for BTRON running bare-metal on the Broadcom BCM2711 (Raspberry Pi 4B and Pi 400, Cortex-A72).

To achieve 60 Hz real-time desktop responsiveness and zero-latency terminal scrolling without stalling the CPU, BTRON bifurcates graphics acceleration into two dedicated hardware pipelines:

1. **Stage 1 Text Mode Shell**: **VideoCore VI Hardware Panning & Virtual Offset (Zero-Copy)**.

2. **GUI Workbench Mode**: **BCM2711 Hardware 2D DMA Engine (True Rectangular BitBlt)**.

## 2. Stage 1 Text Console: VideoCore Hardware Panning

### 2.1 Problem Statement

In traditional framebuffer text consoles, scrolling requires moving all scanlines upward in memory:
$$\text{Memory Moved per Line} = \text{Width} \times (\text{Height} - \text{GlyphHeight}) \times \text{BPP}$$
For $1024 \times 768 \times 32\text{ bpp}$, this transfers $3.08\text{ MB}$ of pixel data for every newline (`\n`). On uncached or write-combined bus architectures, this introduces a 15–70 ms CPU pipeline stall per scrolled line.

### 2.2 Hardware Panning Specification

The VideoCore VI Hardware Video Scaler (HVS) and PixelValve display pipeline support dynamic scanout offsets.

1. **Virtual Framebuffer Allocation**:
   Via Mailbox Channel 8 (Property Interface):
   - Physical Display Resolution: $1024 \times 768$
   - Virtual Framebuffer Resolution: $1024 \times 3072$ (4× vertical screen height, supporting a circular scrollback ring of 192 text lines).
   - Pixel Format: 32-bit ARGB8888.

2. **Mailbox Tag `0x00048009` (`SET_VIRTUAL_OFFSET`)**:
   ```
   Word 0: Tag ID        = 0x00048009
   Word 1: Buffer Size   = 8 bytes
   Word 2: Request Code  = 0
   Word 3: X Offset      = 0
   Word 4: Y Offset      = s_virt_y_offset
   ```
3. **Zero-Copy Scrolling Operation**:
   - When printing reaches the bottom row of the visible window:
     $$s\_virt\_y\_offset = (s\_virt\_y\_offset + \text{GLYPH\_H}) \pmod{\text{VIRTUAL\_HEIGHT}}$$
   - Only the single newly exposed text line ($1024 \times 16$ pixels, $64\text{ KB}$) is cleared and drawn.
   - The CPU issues `SET_VIRTUAL_OFFSET(0, s_virt_y_offset)` to VideoCore.
   - **Cost**: $0\text{ ms}$ CPU blit time, $0\text{ bytes}$ moved across the interconnect.

## 3. GUI Workbench Mode: BCM2711 Hardware 2D DMA Engine

### 3.1 DMA Controller Architecture

The BCM2711 features 15 DMA channels mapped into peripheral MMIO space at `g_mmio_base + 0x00007000`:

- **Channels 0–6**: High-performance "normal" DMA channels supporting 2D Stride Mode (`TDMODE`).

- **Channel 0**: Dedicated by BTRON for graphics compositing and backbuffer presentation.

### 3.2 2D Stride Mode Specification

In 2D Stride Mode (`TI_TDMODE = 1`), the DMA engine autonomously iterates across $Y$ rows of $X$ bytes, applying programmable address strides after each row:

- **`SOURCE_AD`**: Pointer to top-left pixel in source buffer (cached RAM backbuffer).

- **`DEST_AD`**: Pointer to top-left pixel in destination VRAM (VideoCore framebuffer).

- **`TXFR_LEN`**:
  - Bits [31:16]: Height ($Y$ scanlines, $1 \dots 65535$)
  - Bits [15:0]: Width ($X$ transfer length in bytes, $1 \dots 65535$)

- **`STRIDE`**:
  - Bits [31:16]: Destination Stride = $\text{Pitch}_{\text{dst}} - X_{\text{len}}$
  - Bits [15:0]: Source Stride = $\text{Pitch}_{\text{src}} - X_{\text{len}}$

### 3.3 DMA Control Block (CB) Memory Format

DMA Control Blocks are 32-byte structures aligned to 256-bit boundaries in coherent non-cacheable DMA RAM (`0x01000000 + 0xF200`):

```c
typedef struct __attribute__((aligned(32))) {
    uint32_t ti;          /* Transfer Information */
    uint32_t source_ad;   /* Source physical address */
    uint32_t dest_ad;     /* Destination physical address */
    uint32_t txfr_len;    /* 2D Mode: (Y_len << 16) | X_len */
    uint32_t stride;      /* (dst_stride << 16) | src_stride */
    uint32_t nextconbk;   /* Next Control Block address (0 for single CB) */
    uint32_t reserved[2];
} bcm2711_dma_cb_t;
```

#### Transfer Information (`TI`) Flags:

- `TI_TDMODE = (1 << 1)`: Enable 2D Stride Mode.
- `TI_SRC_INC = (1 << 8)`: Increment source address.
- `TI_DEST_INC = (1 << 4)`: Increment destination address.
- `TI_SRC_WIDTH = (1 << 9)`: 128-bit source read transfers.
- `TI_DEST_WIDTH = (1 << 5)`: 128-bit destination write transfers.
- `TI_BURST_LENGTH = (15 << 12)`: 16-beat AXI burst mode.

## 4. Performance Targets & Latency Comparison

| Operation | Unoptimized C Loop | 64-bit SIMD Burst | VideoCore / 2D DMA Hardware | Speedup |
| :--- | :--- | :--- | :--- | :--- |
| **Stage 1 Scroll (1024x768)** | 140 ms | 1.8 ms | **0.001 ms (0-copy)** | **140,000x** |
| **GUI Full Frame Blit (3.14 MB)** | 72 ms | 1.1 ms | **0.8 ms (Asynchronous DMA)** | **90x (CPU 100% Free)** |
| **Window Dirty Rect (320x240)** | 7.5 ms | 0.12 ms | **0.08 ms (Hardware 2D BitBlt)** | **93x** |

## 5. Integration Architecture

```
+-------------------------------------------------------------+
|                     B-TRON Window System                    |
|          (desktop.c / workbench.c / dp_core.c)             |
+-------------------------------------------------------------+
                             |
         +-------------------+-------------------+
         |                                       |
  [Dirty Rect / Blit]                     [Stage 1 Log]
         |                                       |
         v                                       v
+-----------------------+               +-----------------------+
|   BCM2711 2D DMA      |               |  VideoCore Mailbox    |
|   (bcm2711_dma.c)     |               |  (startup_arm.c)      |
|   Channel 0 Stride    |               |  SET_VIRTUAL_OFFSET   |
+-----------------------+               +-----------------------+
         |                                       |
         v                                       v
    [AXI Fabric]                            [HVS Engine]
         \                                       /
          v                                     v
       +-------------------------------------------+
       |   VideoCore VI Display Framebuffer (VRAM) |
       +-------------------------------------------+
```
