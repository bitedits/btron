/*
 * ps2_gs.c — Cleanroom Sony PlayStation 2 Graphics Synthesizer Implementation
 *
 * Implements hardware display controller initialization, privileged PCRTC registers,
 * and high-performance Host-to-Local GIF DMA VRAM blitting for PCSX2 and real PS2 hardware.
 *
 * Cleanroom implementation from the published standards: the OHCI 1.0, GIF and
 * EE DMAC register layouts were re-derived from the manuals and then confirmed
 * against what the running machine decodes.  Zero proprietary Sony SDK code.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "ps2_gs.h"

#include <btron/types.h>     /* BTRON_PIXEL_FB, the canvas's byte order */
#include "ps2_gs_env.h"      /* the environment packet, and apply's argument type */

/* The canvas the GS is fed from, and the one the desktop composites into.
 *
 * There is no framebuffer here.  Until now this driver owned a second
 * 800x600x4 array of its own and the present copied the desktop's canvas into it
 * one pixel at a time, exchanging red and blue on the way, because the GS's
 * CT32 pixels put R in memory byte 0 and BTRON's COLOR puts B there.  That copy
 * cost 49 ms of a 274 ms whole-canvas present and 1.92 MB of a 32 MB image, and
 * the exchange it performed is now done by BTRON_PIXEL_FB() at the moment each
 * colour is drawn instead (include/btron/types.h), so the canvas can be streamed
 * to the GIF as it lies.
 *
 * core_ps2.c installs its own desktop backbuffer as the very first act of
 * ps2_kernel_main(), before the SIO and GS are initialised, because the boot log
 * is written into the canvas the screen shows. */
static uint32_t *s_gs_canvas;

/* EE DMAC Hardware Registers.  Each channel block is CHCR at +0x00, MADR at
 * +0x10 and QWC at +0x20, and the GIF sits in channel 2; the global control and
 * status block is at 0x1000E000. */
#define D_CTRL      (*(volatile uint32_t *)0x1000E000)
#define D_STAT      (*(volatile uint32_t *)0x1000E010)
#define D2_CHCR     (*(volatile uint32_t *)0x1000A000)
#define D2_MADR     (*(volatile uint32_t *)0x1000A010)
#define D2_QWC      (*(volatile uint32_t *)0x1000A020)

/* Helper to get uncached KSEG1 pointer to bypass EE L1 D-Cache */
#define UNCACHED(p) ((void *)((uintptr_t)(p) | 0x20000000UL))

/* The canvas, in the one alias everything that draws uses.
 *
 * The GIF is a bus master, and its stream and setup packets are read through the
 * uncached alias above.  That is only sound because the port never enables the
 * Emotion Engine's L1 caches: the one CP0 Config write in the image is
 * btron_fp_init()'s CU1 bit (src/drivers/ps2/ps2_builtins.c), and boot_ps2.s
 * touches no other coprocessor register -- which make test-ps2 asserts rather
 * than assumes, because a stale line here is a screen of noise with no cause. */
static inline uint32_t *ps2_fb(void)
{
    return s_gs_canvas;
}

/* Install the canvas the GS is fed from.  core_ps2.c calls this with its desktop
 * backbuffer, and the pixels it hands over are already in the GS's own byte
 * order -- which is the entire reason there is nothing left to convert between
 * this array and VRAM. */
void ps2_gs_set_framebuffer(uint32_t *canvas)
{
    if (canvas) s_gs_canvas = canvas;
}

/* GS Register Encoding Macros */
#define GS_SET_DISPLAY(dx, dy, magh, magv, dw, dh) \
    (((uint64_t)((dx) & 0xFFF) << 0)   | \
     ((uint64_t)((dy) & 0x7FF) << 12)  | \
     ((uint64_t)((magh) & 0x0F) << 23) | \
     ((uint64_t)((magv) & 0x07) << 27) | \
     ((uint64_t)((dw) & 0xFFF) << 32)  | \
     ((uint64_t)((dh) & 0x7FF) << 44))

static int s_current_mode = PS2_MODE_800X600;

int ps2_gs_get_width(void)  { return PS2_SCREEN_WIDTH; }
int ps2_gs_get_height(void) { return PS2_SCREEN_HEIGHT; }

/* Cleanroom EE BIOS Syscall Prototypes implemented in boot_ps2.s */
extern void ps2_set_gs_crt(int16_t interlace, int16_t pal_ntsc, int16_t field);
extern void ps2_gs_put_imr(uint32_t mask);
extern const uint8_t* get_glyph_bitmap(uint16_t code, int16_t *out_width, int16_t *out_height);
extern void* tkl_memmove(void *dst, const void *src, size_t n);

/* Transmit packet of quadwords to Graphics Interface (GIF) via DMAC Channel 2.
 *
 * Not static since increment 3: the drawing environment is a second producer on this same
 * pipe, and giving it a copy of the register dance would be two places to keep right about
 * STR polling.  The blit path above and ps2_gs_draw_env_apply() below are the only callers. */
void ps2_gs_gif_send(const void *packet, uint32_t qwc)
{
    /* Clear Channel 2 status bit in D_STAT */
    D_STAT = (1 << 2);

    /* Write physical address to MADR (strip KSEG bits) */
    D2_MADR = ((uint32_t)(uintptr_t)packet) & 0x1FFFFFFF;
    D2_QWC  = qwc;

    __asm__ volatile("" : : : "memory");

    /* Start DMA: direction = 1 (to peripheral/GIF), STR = 1 (bit 8) */
    D2_CHCR = (1 << 0) | (1 << 8);

    /* Wait for transfer to complete (STR bit clears when done) */
    while (D2_CHCR & (1 << 8))
        ;

    __asm__ volatile("" : : : "memory");
}

/* One Host->Local blit setup packet (5 QWs): where in VRAM, how big, which way */
static void send_trx_setup(int dx, int dy, int w, int h)
{
    static uint64_t setup_pkt[5 * 2] __attribute__((aligned(16)));
    uint64_t *p = (uint64_t *)UNCACHED(setup_pkt);

    /* eDRAM buffer width in units of 64 pixels (800 px -> 13 units = 832) */
    uint32_t fbw = (PS2_SCREEN_WIDTH + 63) / 64;

    /* QW0: GIFTAG (4 registers, EOP=1, FLG=PACKED(0), NREG=1, REGS=A+D(0x0E)) */
    p[0] = (4ULL << 0) | (1ULL << 15) | (0ULL << 58) | (1ULL << 60);
    p[1] = 0x0EULL;

    /* QW1: BITBLTBUF (0x50) - SBA=0, SBW=0, SPSM=0, DBA=0, DBW=13 (832/64), DPSM=0 (CT32) */
    p[2] = ((uint64_t)fbw << 48) | ((uint64_t)GS_PSM_CT32 << 56);
    p[3] = 0x50ULL;

    /* QW2: TRXPOS (0x51) - DSTXPOS=dx, DSTYPOS=dy (SRC is the incoming stream) */
    p[4] = ((uint64_t)(uint32_t)dx << 32) | ((uint64_t)(uint32_t)dy << 48);
    p[5] = 0x51ULL;

    /* QW3: TRXREG (0x52) - RRW=w, RRH=h */
    p[6] = ((uint64_t)(uint32_t)w << 0) | ((uint64_t)(uint32_t)h << 32);
    p[7] = 0x52ULL;

    /* QW4: TRXDIR (0x53) - Host -> Local (0) */
    p[8] = 0ULL;
    p[9] = 0x53ULL;

    ps2_gs_gif_send(setup_pkt, 5);
}

/* Stream pixel data as IMAGE-format packets.  QWC counts quadwords, so a row of
 * w pixels is w/4 of them, and the quadword stream restarts at each chunk. */
static void send_image_stream(const void *src, uint32_t qwc)
{
    #define CHUNK_QWC 8000

    static uint64_t img_tag[2] __attribute__((aligned(16)));
    uint64_t *tag = (uint64_t *)UNCACHED(img_tag);
    const uint8_t *p = (const uint8_t *)UNCACHED(src);

    while (qwc > 0) {
        uint32_t n = (qwc > CHUNK_QWC) ? CHUNK_QWC : qwc;
        tag[0] = ((uint64_t)n << 0) | (1ULL << 15) | (2ULL << 58); /* IMAGE mode, EOP=1 */
        tag[1] = 0ULL;
        ps2_gs_gif_send(img_tag, 1);
        ps2_gs_gif_send(p, n);
        p += (uint32_t)n * 16u;
        qwc -= n;
    }
}

void ps2_gs_upload(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) return;
    if (x < 0 || y < 0) return;
    if (x >= PS2_SCREEN_WIDTH || y >= PS2_SCREEN_HEIGHT) return;
    if (x + w > PS2_SCREEN_WIDTH)  w = PS2_SCREEN_WIDTH - x;
    if (y + h > PS2_SCREEN_HEIGHT) h = PS2_SCREEN_HEIGHT - y;

    /* CT32 needs an even RRW and the stream is whole quadwords (4 px) wide;
     * widen to both, which stays inside the 832-pixel VRAM page. */
    x &= ~3;
    w = (w + 3) & ~3;
    if (w > PS2_SCREEN_WIDTH) w = PS2_SCREEN_WIDTH;

    const uint8_t *canvas = (const uint8_t *)ps2_fb();

    if (x == 0 && w == PS2_SCREEN_WIDTH) {
        /* Full rows: the canvas rows are contiguous, so one rectangle and one
         * IMAGE stream cover the whole band. */
        send_trx_setup(0, y, w, h);
        send_image_stream(canvas + (size_t)y * PS2_SCREEN_PITCH,
                          (uint32_t)((size_t)w * (size_t)h / 4u));
    } else {
        /* Host->Local IMAGE data carries no row pitch, so a narrow rectangle
         * has to be uploaded a row at a time. */
        const uint8_t *src = canvas + (size_t)y * PS2_SCREEN_PITCH + (size_t)x * 4u;
        uint32_t qwc = (uint32_t)(w / 4);
        for (int row = 0; row < h; row++, src += PS2_SCREEN_PITCH) {
            send_trx_setup(x, y + row, w, 1);
            send_image_stream(src, qwc);
        }
    }
}

/* Stream entire 800x600 RDRAM framebuffer to GS VRAM via Host->Local GIF DMA */
void ps2_gs_flush(void)
{
    ps2_gs_upload(0, 0, PS2_SCREEN_WIDTH, PS2_SCREEN_HEIGHT);
}

void ps2_gs_set_mode(int mode)
{
    s_current_mode = mode;

    if (mode == PS2_MODE_800X600) {
        /* 720p progressive (SMODE1 CMOD=0, LC=22) carrying an unscaled
         * 800x600 window, instead of the VESA 800x600@60 mode word 0x2B.
         *
         * PCSX2 keys its entire CRT geometry off CMOD and LC, and gives every
         * VESA mode the one 640x480 table entry (GSState.cpp
         * VideoModeOffsets[2]), which GetResolution() then clamps the output
         * to.  So a hardware-correct VGA timing can only ever show 640 of our
         * 800 columns and 480 of our 600 rows there, and its DY=25 is read as
         * a -9 line offset because the VESA entry expects baseDY=34.  The
         * 720p entry is 1280x720 with baseDY=24, so the same canvas comes out
         * unclipped and unshifted.  On a real PS2 this paints the 800x600
         * image into the top-left of a 720p frame. */
        ps2_set_gs_crt(0, 0x52, 0);

        /* PMODE: Circuit 1 + 2 enabled, SLBG = 0 (Display Framebuffer) */
        GS_REG_POKE(GS_PMODE_OFFSET, 0xFF63ULL);

        /* SMODE2: Non-interlaced, progressive scan */
        GS_REG_POKE(GS_SMODE2_OFFSET, 0x00000000ULL);

        /* DISPFB1 & DISPFB2: FBP=0, FBW=13 (832/64), PSM=0 (CT32) */
        uint32_t fbw = (PS2_SCREEN_WIDTH + 63) / 64;
        uint64_t dispfb = (0ULL << 0) | ((uint64_t)fbw << 9) | ((uint64_t)GS_PSM_CT32 << 15);
        GS_REG_POKE(GS_DISPFB1_OFFSET, dispfb);
        GS_REG_POKE(GS_DISPFB2_OFFSET, dispfb);

        /* DISPLAY1 & DISPLAY2: the 720p raster's active-area origin, then
         * MAGH=MAGV=0 so the read is one VRAM pixel per output dot and
         * DW/DH stop at the 800x600 canvas rather than the full 1280x720.
         * The rest of the frame is filled with BGCOLOR.
         */
        uint64_t display = GS_SET_DISPLAY(420, 40, 0, 0, PS2_SCREEN_WIDTH - 1, PS2_SCREEN_HEIGHT - 1);
        GS_REG_POKE(GS_DISPLAY1_OFFSET, display);
        GS_REG_POKE(GS_DISPLAY2_OFFSET, display);

    } else if (mode == PS2_MODE_DTV_480P) {
        /* DTV 480p 640x480 Progressive:
         *   interlace = 0, pal_ntsc = 0x50 (480P), field = 0
         */
        ps2_set_gs_crt(0, 0x50, 0);
        GS_REG_POKE(GS_PMODE_OFFSET, 0xFF63ULL);
        GS_REG_POKE(GS_SMODE2_OFFSET, 0x00000000ULL);

        uint64_t dispfb = (0ULL << 0) | (10ULL << 9) | ((uint64_t)GS_PSM_CT32 << 15);
        GS_REG_POKE(GS_DISPFB1_OFFSET, dispfb);
        GS_REG_POKE(GS_DISPFB2_OFFSET, dispfb);

        uint64_t display = GS_SET_DISPLAY(232, 35, 1, 0, 1439, 479);
        GS_REG_POKE(GS_DISPLAY1_OFFSET, display);
        GS_REG_POKE(GS_DISPLAY2_OFFSET, display);

    } else {
        /* NTSC 640x448 Frame Mode:
         *   interlace = 1, pal_ntsc = 2 (NTSC), field = 1 (Frame Mode)
         */
        ps2_set_gs_crt(1, 2, 1);
        GS_REG_POKE(GS_PMODE_OFFSET, 0xFF63ULL);
        GS_REG_POKE(GS_SMODE2_OFFSET, 0x00000003ULL); /* INT=1, FFMD=1 */

        uint64_t dispfb = (0ULL << 0) | (10ULL << 9) | ((uint64_t)GS_PSM_CT32 << 15);
        GS_REG_POKE(GS_DISPFB1_OFFSET, dispfb);
        GS_REG_POKE(GS_DISPFB2_OFFSET, dispfb);

        uint64_t display = GS_SET_DISPLAY(636, 50, 3, 0, 2559, 447);
        GS_REG_POKE(GS_DISPLAY1_OFFSET, display);
        GS_REG_POKE(GS_DISPLAY2_OFFSET, display);
    }

    ps2_gs_set_bgcolor(24, 48, 80);
}

void ps2_gs_init(uint32_t width, uint32_t height)
{
    (void)width;
    (void)height;

    /* 1. Reset GS via CSR */
    GS_REG_POKE(GS_CSR_OFFSET, (1ULL << 9));
    for (volatile int i = 0; i < 1000; i++) {}

    /* 2. Unmask GS interrupts via BIOS syscall */
    ps2_gs_put_imr(0xFF00);

    /* 3. Configure PCRTC video mode (800x600 canvas on a 720p progressive timing) */
    ps2_gs_set_mode(PS2_MODE_800X600);

    /* 4. Enable EE DMAC Controller */
    D_CTRL |= 1; /* DMAE = 1 */
    D2_CHCR = 0;

    /* 5. Clear RDRAM framebuffer to deep console slate navy */
    uint32_t *fb = ps2_fb();
    for (int i = 0; i < PS2_SCREEN_WIDTH * PS2_SCREEN_HEIGHT; i++) {
        fb[i] = BTRON_PIXEL_FB(0xFF121B29u);
    }

    /* 6. Initial hardware flush to GS eDRAM */
    ps2_gs_flush();

    /* 7. Initialize text console cursor */
    ps2_gs_text_init();
}

void ps2_gs_set_bgcolor(uint8_t r, uint8_t g, uint8_t b)
{
    GS_REG_POKE(GS_BGCOLOR_OFFSET, ((uint64_t)r << 0) | ((uint64_t)g << 8) | ((uint64_t)b << 16));
}

/* ── Stage 1 Text Mode Terminal Console (100 cols x 37 rows) ───── */
#define TEXT_COLS 100
#define TEXT_ROWS 37

static int s_text_cursor_x = 0;
static int s_text_cursor_y = 0;

/* Rows of the canvas that no longer match VRAM.  Without this the console paid
 * a full 1,920,000-byte upload for every printed line, including the one that
 * echoes each keystroke. */
static int s_dirty_top = PS2_SCREEN_HEIGHT;
static int s_dirty_end = 0;

static void text_mark_dirty(int y, int nrows)
{
    if (y < s_dirty_top) s_dirty_top = y;
    if (y + nrows > s_dirty_end) s_dirty_end = y + nrows;
}

void ps2_gs_text_init(void)
{
    ps2_gs_text_clear();
}

void ps2_gs_text_clear(void)
{
    uint32_t *fb = ps2_fb();
    for (int i = 0; i < PS2_SCREEN_WIDTH * PS2_SCREEN_HEIGHT; i++) {
        fb[i] = BTRON_PIXEL_FB(0xFF121B29u); /* Deep Slate Navy Console */
    }
    s_text_cursor_x = 0;
    s_text_cursor_y = 0;
    text_mark_dirty(0, PS2_SCREEN_HEIGHT);
}

void ps2_gs_text_draw_char(int col, int row, char c, uint32_t fg, uint32_t bg)
{
    if (col < 0 || col >= TEXT_COLS || row < 0 || row >= TEXT_ROWS) return;

    int16_t gw = 8, gh = 16;
    const uint8_t *bmp = get_glyph_bitmap((uint16_t)(uint8_t)c, &gw, &gh);
    uint32_t *fb = ps2_fb();
    int base_x = col * 8;
    int base_y = row * 16;

    for (int y = 0; y < 16; y++) {
        uint8_t bits = (bmp && y < gh) ? bmp[y] : 0;
        int py = base_y + y;
        if (py >= PS2_SCREEN_HEIGHT) break;
        for (int x = 0; x < 8; x++) {
            int px = base_x + x;
            if (px >= PS2_SCREEN_WIDTH) break;
            fb[py * PS2_SCREEN_WIDTH + px] = BTRON_PIXEL_FB((bits & (0x80 >> x)) ? fg : bg);
        }
    }
    text_mark_dirty(base_y, 16);
}

static void ps2_gs_text_scroll(void)
{
    uint32_t *fb = ps2_fb();
    /* Scroll up by 1 text line (16 pixel rows) */
    tkl_memmove(fb, fb + (16 * PS2_SCREEN_WIDTH), (size_t)(PS2_SCREEN_HEIGHT - 16) * PS2_SCREEN_WIDTH * sizeof(uint32_t));
    /* Clear bottom 16 pixel rows */
    for (int i = (PS2_SCREEN_HEIGHT - 16) * PS2_SCREEN_WIDTH; i < PS2_SCREEN_WIDTH * PS2_SCREEN_HEIGHT; i++) {
        fb[i] = BTRON_PIXEL_FB(0xFF121B29u);
    }
    text_mark_dirty(0, PS2_SCREEN_HEIGHT);
}

void ps2_gs_text_putc(char c, uint32_t fg, uint32_t bg)
{
    if (c == '\r') {
        s_text_cursor_x = 0;
        return;
    }
    if (c == '\n') {
        s_text_cursor_x = 0;
        s_text_cursor_y++;
        if (s_text_cursor_y >= TEXT_ROWS) {
            ps2_gs_text_scroll();
            s_text_cursor_y = TEXT_ROWS - 1;
        }
        return;
    }
    if (c == '\b') {
        if (s_text_cursor_x > 0) {
            s_text_cursor_x--;
            ps2_gs_text_draw_char(s_text_cursor_x, s_text_cursor_y, ' ', fg, bg);
        }
        return;
    }
    if (c == '\t') {
        s_text_cursor_x = (s_text_cursor_x + 4) & ~3;
        if (s_text_cursor_x >= TEXT_COLS) {
            s_text_cursor_x = 0;
            s_text_cursor_y++;
            if (s_text_cursor_y >= TEXT_ROWS) {
                ps2_gs_text_scroll();
                s_text_cursor_y = TEXT_ROWS - 1;
            }
        }
        return;
    }

    ps2_gs_text_draw_char(s_text_cursor_x, s_text_cursor_y, c, fg, bg);
    s_text_cursor_x++;
    if (s_text_cursor_x >= TEXT_COLS) {
        s_text_cursor_x = 0;
        s_text_cursor_y++;
        if (s_text_cursor_y >= TEXT_ROWS) {
            ps2_gs_text_scroll();
            s_text_cursor_y = TEXT_ROWS - 1;
        }
    }
}

void ps2_gs_text_puts(const char *str, uint32_t fg)
{
    if (!str) return;
    while (*str) {
        ps2_gs_text_putc(*str++, fg, 0xFF121B29);
    }
}

void ps2_gs_text_flush(void)
{
    if (s_dirty_end <= s_dirty_top) return;
    int top = s_dirty_top;
    int h = s_dirty_end - top;
    if (h > PS2_SCREEN_HEIGHT) h = PS2_SCREEN_HEIGHT;
    ps2_gs_upload(0, top, PS2_SCREEN_WIDTH, h);
    s_dirty_top = PS2_SCREEN_HEIGHT;
    s_dirty_end = 0;
}


void ps2_gs_vsync(void)
{
    /* Clear GS CSR VSync interrupt (bit 3) */
    GS_REG_POKE(GS_CSR_OFFSET, (1ULL << 3));
    for (volatile int i = 0; i < 200000; i++) {
        if (GS_REG_PEEK(GS_CSR_OFFSET) & (1ULL << 3)) {
            break;
        }
    }
}

void ps2_gs_wait_vsync(void)
{
    ps2_gs_vsync();
}

void ps2_gs_swap_buffers(void)
{
    ps2_gs_wait_vsync();
    ps2_gs_flush();
}

uint32_t *ps2_gs_get_framebuffer(void)
{
    return ps2_fb();
}

void ps2_gs_flip(void)
{
    ps2_gs_wait_vsync();
    ps2_gs_flush();
}

/* Rasterize solid filled 2D rectangle directly into RDRAM backing store */
void ps2_gs_draw_rect(int x, int y, int w, int h, uint32_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > PS2_SCREEN_WIDTH)  w = PS2_SCREEN_WIDTH - x;
    if (y + h > PS2_SCREEN_HEIGHT) h = PS2_SCREEN_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    uint32_t *fb = ps2_fb();
    for (int j = y; j < y + h; j++) {
        uint32_t *row = &fb[j * PS2_SCREEN_WIDTH + x];
        for (int i = 0; i < w; i++) {
            row[i] = BTRON_PIXEL_FB(color);
        }
    }
}

void ps2_gs_fill_rect(int x, int y, int w, int h, uint32_t color)
{
    ps2_gs_draw_rect(x, y, w, h, color);
}

void ps2_gs_putpixel(int x, int y, uint32_t color)
{
    if (x >= 0 && x < PS2_SCREEN_WIDTH && y >= 0 && y < PS2_SCREEN_HEIGHT) {
        ps2_fb()[y * PS2_SCREEN_WIDTH + x] = BTRON_PIXEL_FB(color);
    }
}

/* Mouse cursor backing store for non-destructive cursor rendering */
static uint32_t cursor_saved[16 * 16];
static int cursor_saved_x = -1;
static int cursor_saved_y = -1;

void ps2_gs_erase_cursor(int x, int y)
{
    (void)x;
    (void)y;
    if (cursor_saved_x < 0 || cursor_saved_y < 0) return;

    uint32_t *fb = ps2_fb();
    for (int cy = 0; cy < 16; cy++) {
        int py = cursor_saved_y + cy;
        if (py >= PS2_SCREEN_HEIGHT) break;
        for (int cx = 0; cx < 16; cx++) {
            int px = cursor_saved_x + cx;
            if (px >= PS2_SCREEN_WIDTH) break;
            fb[py * PS2_SCREEN_WIDTH + px] = cursor_saved[cy * 16 + cx];
        }
    }
    cursor_saved_x = -1;
    cursor_saved_y = -1;
}

void ps2_gs_draw_cursor(int x, int y)
{
    if (cursor_saved_x >= 0) {
        ps2_gs_erase_cursor(0, 0);
    }
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= PS2_SCREEN_WIDTH - 16) x = PS2_SCREEN_WIDTH - 16;
    if (y >= PS2_SCREEN_HEIGHT - 16) y = PS2_SCREEN_HEIGHT - 16;

    uint32_t *fb = ps2_fb();
    for (int cy = 0; cy < 16; cy++) {
        int py = y + cy;
        if (py >= PS2_SCREEN_HEIGHT) break;
        for (int cx = 0; cx < 16; cx++) {
            int px = x + cx;
            if (px >= PS2_SCREEN_WIDTH) break;
            cursor_saved[cy * 16 + cx] = fb[py * PS2_SCREEN_WIDTH + px];
        }
    }
    cursor_saved_x = x;
    cursor_saved_y = y;

    static const uint16_t cursor_mask[16] = {
        0b1000000000000000, 0b1100000000000000, 0b1110000000000000, 0b1111000000000000,
        0b1111100000000000, 0b1111110000000000, 0b1111111000000000, 0b1111111100000000,
        0b1111111110000000, 0b1111110000000000, 0b1101111000000000, 0b1000111100000000,
        0b0000011110000000, 0b0000001111000000, 0b0000000111000000, 0b0000000011000000
    };
    static const uint16_t cursor_outline[16] = {
        0b1100000000000000, 0b1010000000000000, 0b1001000000000000, 0b1000100000000000,
        0b1000010000000000, 0b1000001000000000, 0b1000000100000000, 0b1000000010000000,
        0b1000000001000000, 0b1000011111100000, 0b1101010000000000, 0b0110101000000000,
        0b0000010100000000, 0b0000001010000000, 0b0000000101000000, 0b0000000011000000
    };

    for (int cy = 0; cy < 16; cy++) {
        uint16_t m = cursor_mask[cy];
        uint16_t o = cursor_outline[cy];
        int py = y + cy;
        if (py >= PS2_SCREEN_HEIGHT) break;
        for (int cx = 0; cx < 16; cx++) {
            int px = x + cx;
            if (px >= PS2_SCREEN_WIDTH) break;
            if ((o >> (15 - cx)) & 1) {
                fb[py * PS2_SCREEN_WIDTH + px] = BTRON_PIXEL_FB(0xFF000000u); /* Outline: Black */
            } else if ((m >> (15 - cx)) & 1) {
                fb[py * PS2_SCREEN_WIDTH + px] = BTRON_PIXEL_FB(0xFFFFFFFFu); /* Body: White */
            }
        }
    }
}

/* Program the GS drawing environment (doc/md/PS2.md, EE/GS plan of record, increment 3).
 *
 * The packet's contents are ps2_gs_env.c's business and are gated word for word on the host;
 * this is the part that can only be done here, because channel 2 is.  There is deliberately no
 * ps2_gs_poke64() on this path: the GIF takes the whole environment in one DMA, in order, and
 * nine individual stores would each take a bus turn and none of them would be ordered against
 * the primitive stream that follows.  make test-ps2 reads that off the image -- tests 39 to 41
 * find the call into the builder, no GS poke, and the DMAC window this write goes to.  Note that
 * the sender itself is not what the image gate looks for: it is in this translation unit and
 * -O2 folds it in, so the gate names the registers written rather than the call.
 *
 * Nothing calls it yet -- increment 4 is where an app's primitives arrive -- so the image is
 * unchanged for everything the user can see. */
int ps2_gs_draw_env_apply(const gs_draw_env *env)
{
    /* Tag pair plus one quadword per register, in quadword-aligned storage: the DMA reads
     * 16 bytes at a time and the GIF decodes the tag before it will take a data word. */
    static uint64_t env_pkt[(GS_ENV_NREG + 1u) * 2u] __attribute__((aligned(16)));
    uint64_t *p = (uint64_t *)UNCACHED(env_pkt);

    if (gs_draw_env_packet(env, p) != 0) return -1;

    ps2_gs_gif_send(env_pkt, gs_draw_env_qwc());
    return 0;
}
