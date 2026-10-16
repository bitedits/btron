/*
 * ps2_gs.h — Cleanroom Sony PlayStation 2 Graphics Synthesizer (GS) Driver
 *
 * Direct register definitions and display controller interface.
 * Zero proprietary Sony SDK dependencies.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef PS2_GS_H
#define PS2_GS_H

#include <stdint.h>
#include <stddef.h>

/* GS Privileged Registers (Direct Mapped Address: 0x12000000) */
#define GS_BASE                 0x12000000UL

/* A GS register is written and read as ONE 64-bit access: the GS latches a
 * privileged register on a 64-bit store, so a split pair of 32-bit stores leaves
 * the half that arrived first in the latch.  The port's C is built for a
 * 32-bit-GPR MIPS (Makefile: the PS2 ABI note) and cannot emit an `sd`, so the
 * access goes through ps2_gs_reg.s -- see its header for why that exception is
 * placed there and not in a second ISA of C. */
void ps2_gs_poke64(uint32_t addr, uint32_t lo, uint32_t hi);
uint64_t ps2_gs_peek64(uint32_t addr);

#define GS_REG_POKE(offset, value)                                    \
    do {                                                              \
        const uint64_t gs_reg_value_ = (uint64_t)(value);             \
        ps2_gs_poke64((uint32_t)(GS_BASE + (offset)),                 \
                      (uint32_t)gs_reg_value_,                        \
                      (uint32_t)(gs_reg_value_ >> 32));               \
    } while (0)
#define GS_REG_PEEK(offset)                                           \
    (ps2_gs_peek64((uint32_t)(GS_BASE + (offset))))

#define GS_PMODE_OFFSET         0x0000
#define GS_SMODE2_OFFSET        0x0020
#define GS_DISPFB1_OFFSET       0x0070
#define GS_DISPLAY1_OFFSET      0x0080
#define GS_DISPFB2_OFFSET       0x0090
#define GS_DISPLAY2_OFFSET      0x00A0
#define GS_EXTBUF_OFFSET        0x00B0
#define GS_EXTDATA_OFFSET       0x00C0
#define GS_EXTWRITE_OFFSET      0x00D0
#define GS_BGCOLOR_OFFSET       0x00E0
#define GS_CSR_OFFSET           0x1000
#define GS_IMR_OFFSET           0x1010
#define GS_BUSDIR_OFFSET        0x1040
#define GS_SIGBLID_OFFSET       0x1080

/* Screen Dimensions (High-Resolution 800x600 @ 32-bpp VESA Framebuffer) */
#define PS2_SCREEN_WIDTH        800
#define PS2_SCREEN_HEIGHT       600
#define PS2_SCREEN_BPP          32
#define PS2_SCREEN_PITCH        (PS2_SCREEN_WIDTH * (PS2_SCREEN_BPP / 8))

/* Video Modes.  The name is the canvas we draw into; the CRT timing each one
 * programs is a separate choice, documented at the register writes. */
#define PS2_MODE_800X600        0       /* 800x600 canvas, 720p progressive timing (Default) */
#define PS2_MODE_DTV_480P       1       /* DTV 480p 640x480 Progressive Scan */
#define PS2_MODE_NTSC_448I      2       /* NTSC 640x448 Frame Mode Interlaced */

/* GS Pixel Formats */
#define GS_PSM_CT32             0x00    /* RGBA32 */
#define GS_PSM_CT24             0x01    /* RGB24 */
#define GS_PSM_CT16             0x02    /* RGBA16 (5551) */
#define GS_PSM_CT16S            0x0A
/* ZBUF.ZSM takes a different ordinal set from FRAME.PSM: the depth formats.  3 is 32-bit Z
 * (24.8 fixed), the one this port names -- with TEST.ZTEN clear and ZMSK set it is never read or
 * written, but the register still has to hold a format the hardware accepts. */
#define GS_PSMZ32               0x03

/* Public API */
void ps2_gs_init(uint32_t width, uint32_t height);
void ps2_gs_set_mode(int mode);
int  ps2_gs_get_width(void);
int  ps2_gs_get_height(void);
void ps2_gs_set_bgcolor(uint8_t r, uint8_t g, uint8_t b);
void ps2_gs_vsync(void);
void ps2_gs_wait_vsync(void);
void ps2_gs_swap_buffers(void);
void ps2_gs_flush(void);
/* Copies a rectangle of the RDRAM canvas into the displayed VRAM page with one Host->Local blit.
   (x, y) is the top-left in canvas pixels; w is rounded up to a quadword (4 px) because the upload stream has no row pitch. */
void ps2_gs_upload(int x, int y, int w, int h);

/* Why the last Local->Host read did not deliver pixels.  The two families are different facts:
   GS_READ_BUS_E_BUILD says the rectangle cannot be described by these registers at all -- which law
   is in gs_read_last_err() -- and a timeout says it could be described but the machine never
   answered.  Neither is "the download ran and the page is black", which is the answer the census was
   asked for; that distinction is the only reason a refused read can be told apart from a black
   screen, so no exit in this enum may be reported as a colour. */
typedef enum {
    GS_READ_BUS_OK = 0,
    GS_READ_BUS_E_ARGS,      /* null destination, or one that is not 16-byte aligned */
    GS_READ_BUS_E_BUSY,      /* the GIF ring, channel 2 or channel 1 still owns the bus */
    GS_READ_BUS_E_BUILD,     /* the builder refused; gs_read_last_err() names the law */
    GS_READ_BUS_E_STR,       /* the request block's channel-1 transfer never finished */
    GS_READ_BUS_E_FINISH,    /* the CSR FINISH receipt never arrived for this packet */
    GS_READ_BUS_E_FIFO,      /* VIF1's FIFO (STAT.FQC) never drained */
    GS_READ_BUS_E_DATA       /* the pixel download's channel-1 transfer never finished */
} gs_read_bus_err;

/* Reads a rectangle of GS VRAM back into main memory: the Local->Host direction, and the only way
 * this port can see what the GS drew when nobody is watching a window.  ps2_gs_read.h's pure builder
 * makes the words; this is the bus turn that hands them to VIF1 on DMAC channel 1, collects the
 * FINISH receipt, reverses the FIFO (VIF1_STAT.FDR) and the GS bus (BUSDIR=1), and takes the pixels.
 *
 * Every wait is bounded, and every bounded exit restores VIF1_STAT and BUSDIR on the way out: a read
 * that leaves the bus reversed is a screen that never draws again, which is a worse failure than the
 * one it was meant to diagnose.  Nothing is printed and nothing is uploaded -- the caller must not
 * either, because the receipt it is waiting for belongs to a packet that has to be the only thing on
 * the pipe (see ps2_gif_hw_busy() and ps2_gif_pending(), both refused on here).
 *
 * (x, y, w, h) are canvas pixels in the displayed page (VRAM page 0, the CT32 format the canvas and
 * VRAM agree on byte for byte) and must satisfy the builder's laws -- for CT32 that is x and w
 * multiples of 4 px.  `dst` must be 16-byte aligned and hold gs_read_image_qwc(w, h, GS_PSM_CT32)
 * quadwords, and the caller should fill it with a sentinel first: a transfer the hardware rounds down
 * leaves the tail holding whatever was there, which is the failure this signature cannot report.
 * Returns the quadwords asked for, or -1 with the reason in ps2_gs_vram_read_err(). */
int32_t ps2_gs_vram_read(int x, int y, int w, int h, uint32_t *dst);
gs_read_bus_err ps2_gs_vram_read_err(void);
/* The one DMAC channel-2 sender that still blocks, shared by every producer whose buffer the port
 * cannot promise survives until the next yield: the canvas blit stream above, and everything the
 * boot path sends before a ring exists to hold a packet.  Declared rather than left accidentally
 * global, because a second copy of the STR poll is the thing this exists to prevent and the next
 * producer needs to find the first one.  The producer that CAN make that promise posts to
 * ps2_gif_ring.h instead -- see LANE 10 in doc/txt/PS2.txt -- and the ring's hardware seam is
 * defined next to this sender, in this file. */
void ps2_gs_gif_send(const void *packet, uint32_t qwc);
uint32_t *ps2_gs_get_framebuffer(void);
/* Install the canvas the GS is fed from.  core_ps2.c calls this with its desktop
 * backbuffer as the first act of ps2_kernel_main(), before ps2_gs_init(): the boot
 * log is written into the canvas the screen shows, so the driver cannot own a buffer
 * of its own and still have the two agree.  The canvas's words are already in the
 * GS's byte order -- see BTRON_PIXEL_FB in include/btron/types.h -- which is what
 * lets ps2_gs_upload() stream it without a conversion pass. */
void ps2_gs_set_framebuffer(uint32_t *canvas);
void ps2_gs_flip(void);
void ps2_gs_putpixel(int x, int y, uint32_t color);
void ps2_gs_fill_rect(int x, int y, int w, int h, uint32_t color);
void ps2_gs_draw_rect(int x, int y, int w, int h, uint32_t color);
void ps2_gs_draw_cursor(int x, int y);
void ps2_gs_erase_cursor(int x, int y);

/* Text Mode Console (Stage 1) */
void ps2_gs_text_init(void);
void ps2_gs_text_clear(void);
void ps2_gs_text_draw_char(int col, int row, char c, uint32_t fg, uint32_t bg);
void ps2_gs_text_putc(char c, uint32_t fg, uint32_t bg);
void ps2_gs_text_puts(const char *str, uint32_t fg);
void ps2_gs_text_flush(void);

#endif /* PS2_GS_H */

