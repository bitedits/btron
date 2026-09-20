/*
 * B-TRON Specification Compatible Header: dp.h
 * Display Primitives (Graphics Engine) Header.
 */

#ifndef _BTRON_DP_H_
#define _BTRON_DP_H_

#include <btron/types.h>
#include <btron/error.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include <stdio.h>
#endif
#define uart_puts(s) printf("%s", (s))
#else
void uart_puts(const char *s);
#endif

/* Raster Operations (ROP) */
#define ROP_COPY   0
#define ROP_OR     1
#define ROP_XOR    2
#define ROP_AND    3
#define ROP_INVERT 4

#ifndef COLOR_BLACK
/* TRON Standard Palette Colors (0xAARRGGBB format) */
#define COLOR_BLACK     0xFF000000
#define COLOR_WHITE     0xFFFFFFFF
#define COLOR_DKGRAY    0xFF404040
#define COLOR_GRAY      0xFF808080
#define COLOR_LTGRAY    0xFFD4D0C8   /* Classic Retro 3D Chrome Gray */
#define COLOR_TEAL      0xFF008080   /* Classic B-TRON Teal Desktop */
#define COLOR_NAVY      0xFF000080   /* Classic Navy Title Bar */
#define COLOR_BLUE      0xFF0000FF
#define COLOR_YELLOW    0xFFFFFF00
#define COLOR_RED       0xFFFF0000
#define COLOR_GREEN     0xFF00C040
#define COLOR_CYAN      0xFF00FFFF
#define COLOR_GOLD      0xFFFFCC00
#endif

typedef struct {
    H width;
    H height;
    UW is_vram;
    COLOR *pixels;
    RECT clip;
} GDEV;

/* Display Primitive Operations */
GDEV* opn_dev(H w, H h);
GDEV* opn_dev_vram(H w, H h, COLOR *vram_buffer);
void  cls_dev(GDEV *dev);

void  set_col(GDEV *dev, COLOR fg, COLOR bg);
void  set_pat(GDEV *dev, const PAT *pat);
void  set_clip(GDEV *dev, const RECT *clip);

ER    drw_pnt(GDEV *dev, H x, H y);
ER    drw_lin(GDEV *dev, H x1, H y1, H x2, H y2);
ER    drw_rec(GDEV *dev, const RECT *r);
ER    fill_rec(GDEV *dev, const RECT *r, COLOR col);
ER    drw_ovl(GDEV *dev, const RECT *r);
ER    fill_ovl(GDEV *dev, const RECT *r, COLOR col);

/* ── On-device composite stage telemetry ────────────────────────────────────
 * The compact HUD reports one worst-case number per plane: it says a composite
 * cost tens of milliseconds, but not which stage of it did.  Each stage of the
 * damage composite times itself into these maxima so the cost can be attributed
 * on real hardware instead of estimated from the source.  Every field is a
 * microsecond maximum, taken over one second by btron_render_stats_take().
 */
typedef struct {
    uint32_t bg_us;        /* desktop background restore            */
    uint32_t frame_us;     /* window decoration (fills/border/title)*/
    uint32_t paint_us;     /* application paint callbacks           */
    uint32_t blit_us;      /* client-area scan + composite          */
    uint32_t panel_us;     /* top panel + gold bar                  */
    uint32_t bars_us;      /* bottom colour test bars               */
    uint32_t comp_us;      /* whole damage composite                */
    uint32_t tile_max_us;  /* worst single preview tile composite   */
    uint32_t tiles;        /* preview tiles composited              */
    uint32_t wins_walked;  /* window list length of the last walk   */
    uint32_t wins_drawn;   /* windows composited by the last walk   */
} RENDER_STATS;

extern RENDER_STATS g_render_stats;

/* Monotonic counter in microseconds; 0 where the platform supplies none, which
 * leaves every stage reading 0 instead of disturbing the shared build. */
uint32_t btron_render_perf_us(void);

/* Snapshot the maxima into `out` and clear them (one profiling window). */
void btron_render_stats_take(RENDER_STATS *out);

static inline void btron_render_stat_max(uint32_t *field, uint32_t value) {
    if (value > *field) *field = value;
}

/* Pointer Grab & Release Control (like ^G in QEMU) */
void  sdl_set_mouse_grab(BOOL grabbed);
void  sdl_toggle_mouse_grab(void);
BOOL  sdl_is_mouse_grabbed(void);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_DP_H_ */
