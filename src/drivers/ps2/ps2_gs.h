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
#define GS_REG(offset)          (*(volatile uint64_t *)((uintptr_t)(GS_BASE + (offset))))

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
uint32_t *ps2_gs_get_framebuffer(void);
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

