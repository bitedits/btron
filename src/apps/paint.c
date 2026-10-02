/*
 * B-System (BTRON 3.20) Paint — GIF/PNG Image Viewer (src/apps/paint.c)
 * View-only facilities: zoom, Win95 scrollbars, about box. Cleanroom BTRON3.
 */

#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/event.h>
#include <btron/error.h>
#include <btron/app_menu.h>
#include <btron/troncode.h>
#include <btron/image_decode.h>
#include <stdint.h>

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#else
#include <stddef.h>
#include <libstr.h>
extern void* Imalloc(size_t sz);
extern void  Ifree(void *ptr);
#define malloc Imalloc
#define free   Ifree
#define memset tkl_memset
#define memcpy tkl_memcpy
#define strlen tkl_strlen
#define strcmp tkl_strcmp
#define strcpy tkl_strcpy
#define strncpy tkl_strncpy
#define strstr tkl_strstr
extern int tkl_snprintf(char *str, size_t size, const char *format, ...);
#define snprintf tkl_snprintf

static inline void* local_calloc(size_t nmemb, size_t sz) {
    size_t bytes = nmemb * sz;
    void *p = Imalloc(bytes);
    if (p) tkl_memset(p, 0, bytes);
    return p;
}
#define calloc local_calloc

static inline char* local_strchr(const char *s, int c) {
    if (!s) return (void*)0;
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return (c == 0) ? (char*)s : (void*)0;
}
#define strchr local_strchr

static inline char* local_strrchr(const char *s, int c) {
    if (!s) return (void*)0;
    const char *last = (void*)0;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if (c == 0) return (char*)s;
    return (char*)last;
}
#define strrchr local_strrchr
#endif

/* Weak app launchers shared with the widget library */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak)) WND* open_tad_browser_window(const char *filepath, const char *title);
#else
extern WND* open_tad_browser_window(const char *filepath, const char *title);
#endif

/* ── Viewport chrome geometry (client coords) ────────────────────────────── */
#define PAINT_MENU_H   APP_MENU_BAR_HEIGHT   /* 21 */
#define PAINT_STATUS_H 22
#define PAINT_SB_W     16
#define PAINT_SB_BTN   16
#define PAINT_CANVAS_H 28

#define PAINT_ZOOM_MIN  10
#define PAINT_ZOOM_MAX  800
#define PAINT_MAX_IMGS  64
#define PAINT_PATH_MAX  160
#define PAINT_IMG_NAME  64    /* cascade rows: app_menu wants [][64] */

typedef struct {
    UB   *pixels;        /* decoded RGBA, 4 bytes/px, malloc'd */
    H     img_w, img_h;
    char  path[PAINT_PATH_MAX];
    char  title[64];

    int   zoom;          /* percent, PAINT_ZOOM_MIN..PAINT_ZOOM_MAX */
    int   scroll_x, scroll_y;
    BOOL  canvas_checker;

    APP_MENU_BAR menu_bar;
    BOOL sb_drag_v, sb_drag_h;
    int  sb_drag_start_x, sb_drag_start_y;
    int  sb_drag_start_scroll_x, sb_drag_start_scroll_y;

    char imgs[PAINT_MAX_IMGS][PAINT_IMG_NAME];
    int  img_count;
} PaintViewer;

static PaintViewer g_active_paint;

enum {
    PCMD_NONE = 0,
    /* ファイル */
    PCMD_FILE_OPEN_CASCADE = 10,
    PCMD_FILE_OPEN_PROMPT,
    PCMD_FILE_RELOAD,
    PCMD_FILE_CLOSE_IMG,
    PCMD_FILE_QUIT,
    /* 表示 */
    PCMD_VIEW_ZOOM_IN = 20,
    PCMD_VIEW_ZOOM_OUT,
    PCMD_VIEW_ZOOM_100,
    PCMD_VIEW_ZOOM_FIT,
    PCMD_VIEW_ORIGIN,
    PCMD_VIEW_CANVAS_TOGGLE,
    /* ヘルプ */
    PCMD_HELP_ABOUT = 40,
    PCMD_HELP_DOC
};

static void paint_init_menu_bar(PaintViewer *pv);

/* ── Derived canvas + scroll extents (shared by paint and hit-testing) ────── */
typedef struct {
    int view_x, view_y, view_w, view_h;
    int scaled_w, scaled_h;
    int content_w, content_h;
    int max_scroll_x, max_scroll_y;
} paint_view_t;

static void paint_compute(const PaintViewer *pv, GDEV *dev, paint_view_t *v) {
    int hsb_present = (dev->width >= 120) ? PAINT_SB_W : 0;
    v->view_x = 0;
    v->view_y = PAINT_MENU_H;
    v->view_w = dev->width - PAINT_SB_W;
    v->view_h = dev->height - PAINT_MENU_H - PAINT_STATUS_H - PAINT_SB_W;
    if (v->view_w < 1) v->view_w = 1;
    if (v->view_h < 1) v->view_h = 1;
    (void)hsb_present;

    v->scaled_w = (pv->img_w > 0) ? (pv->img_w * pv->zoom) / 100 : 0;
    v->scaled_h = (pv->img_h > 0) ? (pv->img_h * pv->zoom) / 100 : 0;

    v->content_w = (v->scaled_w < v->view_w) ? v->view_w : v->scaled_w;
    v->content_h = (v->scaled_h < v->view_h) ? v->view_h : v->scaled_h;

    v->max_scroll_x = v->content_w - v->view_w; if (v->max_scroll_x < 0) v->max_scroll_x = 0;
    v->max_scroll_y = v->content_h - v->view_h; if (v->max_scroll_y < 0) v->max_scroll_y = 0;
}

/* Image top-left within the canvas (centred when it fits the viewport) */
static void paint_canvas_origin(const PaintViewer *pv, GDEV *dev, int *ox, int *oy) {
    paint_view_t v;
    paint_compute(pv, dev, &v);
    int off_x = (v.scaled_w <= v.view_w) ? (v.view_w - v.scaled_w) / 2 : 0;
    int off_y = (v.scaled_h <= v.view_h) ? (v.view_h - v.scaled_h) / 2 : 0;
    *ox = v.view_x + off_x - pv->scroll_x;
    *oy = v.view_y + off_y - pv->scroll_y;
}

static void paint_set_scroll(PaintViewer *pv, GDEV *dev, int nx, int ny) {
    paint_view_t v;
    paint_compute(pv, dev, &v);
    if (nx < 0) nx = 0;
    if (ny < 0) ny = 0;
    if (nx > v.max_scroll_x) nx = v.max_scroll_x;
    if (ny > v.max_scroll_y) ny = v.max_scroll_y;
    pv->scroll_x = nx;
    pv->scroll_y = ny;
}

static void paint_clamp_scroll(PaintViewer *pv, GDEV *dev) {
    paint_set_scroll(pv, dev, pv->scroll_x, pv->scroll_y);
}

static void paint_zoom_to(PaintViewer *pv, int zoom) {
    if (zoom < PAINT_ZOOM_MIN) zoom = PAINT_ZOOM_MIN;
    if (zoom > PAINT_ZOOM_MAX) zoom = PAINT_ZOOM_MAX;
    pv->zoom = zoom;
}

static void paint_zoom_fit(PaintViewer *pv, GDEV *dev) {
    if (pv->img_w <= 0 || pv->img_h <= 0 || !dev) { pv->zoom = 100; return; }
    paint_view_t v;
    paint_compute(pv, dev, &v);
    if (v.view_w < 1 || v.view_h < 1) { pv->zoom = 100; return; }
    int zx = (v.view_w * 100) / pv->img_w;
    int zy = (v.view_h * 100) / pv->img_h;
    int fit = (zx < zy) ? zx : zy;
    paint_zoom_to(pv, fit);
}

/* ── Image loading (hosted only) ─────────────────────────────────────────── */
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
static void paint_free_image(PaintViewer *pv) {
    if (pv->pixels) {
        free(pv->pixels);
        pv->pixels = NULL;
    }
    pv->img_w = 0;
    pv->img_h = 0;
}

static void paint_load(PaintViewer *pv, const char *path) {
    if (!path || path[0] == '\0') return;

    /* Free any previously decoded image; re-selecting reloads from disk. */
    paint_free_image(pv);

    UB *px = NULL;
    H w = 0, h = 0;
    if (decode_image_rgba(path, &px, &w, &h) == 0 && px && w > 0 && h > 0) {
        pv->pixels = px;
        pv->img_w = w;
        pv->img_h = h;
        strncpy(pv->path, path, sizeof(pv->path) - 1);
        pv->path[sizeof(pv->path) - 1] = '\0';

        const char *base = strrchr(path, '/');
        strncpy(pv->title, base ? base + 1 : path, sizeof(pv->title) - 1);
        pv->title[sizeof(pv->title) - 1] = '\0';

        pv->scroll_x = 0;
        pv->scroll_y = 0;
    } else {
        pv->pixels = NULL;
        pv->img_w = pv->img_h = 0;
        pv->path[0] = '\0';
        pv->title[0] = '\0';
    }
}

static int paint_scan_images(PaintViewer *pv) {
    pv->img_count = 0;
    /* Put default 64x64 transparent pixel art Lil Cube 64 first if available */
    const char *default_img = "assets/pixart/lil-cube-64.png";
    FILE *f_def = fopen(default_img, "rb");
    if (!f_def) {
        default_img = "assets/mascot/lil-cube-64.png";
        f_def = fopen(default_img, "rb");
    }
    if (!f_def) {
        default_img = "assets/mascot/lil-cube-n64.jpg";
        f_def = fopen(default_img, "rb");
    }
    if (f_def) {
        fclose(f_def);
        strncpy(pv->imgs[0], default_img, PAINT_IMG_NAME - 1);
        pv->imgs[0][PAINT_IMG_NAME - 1] = '\0';
        pv->img_count = 1;
    }
    const char *dirs[] = { "assets/pixart", "assets/mascot", "assets/icons" };
    for (int d = 0; d < 3 && pv->img_count < PAINT_MAX_IMGS; d++) {
        DIR *dir = opendir(dirs[d]);
        if (!dir) continue;
        struct dirent *de;
        while ((de = readdir(dir)) != NULL && pv->img_count < PAINT_MAX_IMGS) {
            if (de->d_name[0] == '.') continue;
            size_t l = strlen(de->d_name);
            if (l < 5) continue;
            BOOL is_gif = (l > 4 && strcmp(de->d_name + l - 4, ".gif") == 0);
            BOOL is_png = (l > 4 && strcmp(de->d_name + l - 4, ".png") == 0);
            BOOL is_jpg = (l > 4 && strcmp(de->d_name + l - 4, ".jpg") == 0);
            BOOL is_jpeg = (l > 5 && strcmp(de->d_name + l - 5, ".jpeg") == 0);
            if (!is_gif && !is_png && !is_jpg && !is_jpeg) continue;
            char path[PAINT_IMG_NAME];
            snprintf(path, sizeof(path), "%s/%s", dirs[d], de->d_name);
            if (pv->img_count > 0 && strcmp(pv->imgs[0], path) == 0) continue;
            strncpy(pv->imgs[pv->img_count], path, PAINT_IMG_NAME - 1);
            pv->imgs[pv->img_count][PAINT_IMG_NAME - 1] = '\0';
            pv->img_count++;
        }
        closedir(dir);
    }
    if (pv->img_count == 0) {
        strncpy(pv->imgs[0], "assets/pixart/lil-cube-64.png", PAINT_IMG_NAME - 1);
        pv->imgs[0][PAINT_IMG_NAME - 1] = '\0';
        pv->img_count = 1;
    }
    return pv->img_count;
}
#else
static void paint_free_image(PaintViewer *pv) {
    if (pv->pixels) {
        free(pv->pixels);
        pv->pixels = NULL;
    }
    pv->img_w = 0;
    pv->img_h = 0;
}

static void paint_load(PaintViewer *pv, const char *path) {
    (void)pv; (void)path;   /* no filesystem on bare-metal targets */
}

static int paint_scan_images(PaintViewer *pv) {
    pv->img_count = 0;
    return 0;
}
#endif

/* ── Win95 scrollbar layout ──────────────────────────────────────────────── */
typedef struct {
    RECT  bar, a_btn, b_btn, thumb;
    int   track_a, track_b;   /* track start/end along the scroll axis */
    int   thickness;
    BOOL  horiz;
    BOOL  overflow;
} paint_sbar_t;

static void paint_sbar_layout(PaintViewer *pv, GDEV *dev, paint_sbar_t *sb, BOOL horiz) {
    paint_view_t v;
    paint_compute(pv, dev, &v);
    sb->horiz = horiz;
    sb->thickness = PAINT_SB_W;

    if (horiz) {
        int bx0 = 0, bx1 = dev->width - PAINT_SB_W;
        int by  = dev->height - PAINT_STATUS_H - PAINT_SB_W;
        sb->bar.left = bx0; sb->bar.top = by; sb->bar.right = bx1; sb->bar.bottom = by + PAINT_SB_W;
        sb->a_btn.left = bx0;        sb->a_btn.right = bx0 + PAINT_SB_BTN;
        sb->a_btn.top = by;          sb->a_btn.bottom = by + PAINT_SB_BTN;
        sb->b_btn.left = bx1 - PAINT_SB_BTN; sb->b_btn.right = bx1;
        sb->b_btn.top = by;          sb->b_btn.bottom = by + PAINT_SB_BTN;
        sb->track_a = bx0 + PAINT_SB_BTN;
        sb->track_b = bx1 - PAINT_SB_BTN;
        int track_w = sb->track_b - sb->track_a; if (track_w < 1) track_w = 1;
        sb->overflow = (v.max_scroll_x > 0 && track_w > 2 * PAINT_SB_BTN);
        int th_w, tx;
        if (sb->overflow && v.content_w > 0) {
            th_w = (v.view_w * track_w) / v.content_w;
            if (th_w < 15) th_w = 15;
            if (th_w > track_w) th_w = track_w;
            tx = sb->track_a + (pv->scroll_x * (track_w - th_w)) / v.max_scroll_x;
        } else {
            th_w = track_w;
            tx = sb->track_a;
        }
        sb->thumb.left = tx; sb->thumb.right = tx + th_w;
        sb->thumb.top = by + 2; sb->thumb.bottom = by + PAINT_SB_W - 2;
    } else {
        int bx  = dev->width - PAINT_SB_W;
        int by0 = PAINT_MENU_H;
        int by1 = dev->height - PAINT_STATUS_H - PAINT_SB_W;
        sb->bar.left = bx; sb->bar.top = by0; sb->bar.right = bx + PAINT_SB_W; sb->bar.bottom = by1;
        sb->a_btn.left = bx; sb->a_btn.right = bx + PAINT_SB_W;
        sb->a_btn.top = by0; sb->a_btn.bottom = by0 + PAINT_SB_BTN;
        sb->b_btn.left = bx; sb->b_btn.right = bx + PAINT_SB_W;
        sb->b_btn.top = by1 - PAINT_SB_BTN; sb->b_btn.bottom = by1;
        sb->track_a = by0 + PAINT_SB_BTN;
        sb->track_b = by1 - PAINT_SB_BTN;
        int track_h = sb->track_b - sb->track_a; if (track_h < 1) track_h = 1;
        sb->overflow = (v.max_scroll_y > 0 && track_h > 2 * PAINT_SB_BTN);
        int th_h, ty;
        if (sb->overflow && v.content_h > 0) {
            th_h = (v.view_h * track_h) / v.content_h;
            if (th_h < 15) th_h = 15;
            if (th_h > track_h) th_h = track_h;
            ty = sb->track_a + (pv->scroll_y * (track_h - th_h)) / v.max_scroll_y;
        } else {
            th_h = track_h;
            ty = sb->track_a;
        }
        sb->thumb.left = bx + 2; sb->thumb.right = bx + PAINT_SB_W - 2;
        sb->thumb.top = ty;      sb->thumb.bottom = ty + th_h;
    }
}

static void paint_draw_scrollbar(PaintViewer *pv, GDEV *dev, BOOL horiz) {
    paint_sbar_t sb;
    paint_sbar_layout(pv, dev, &sb, horiz);
    if (horiz && dev->width < 2 * PAINT_SB_BTN + 2) return;
    if (!horiz && dev->height <= PAINT_MENU_H + PAINT_STATUS_H + 2 * PAINT_SB_BTN) return;

    const COLOR pen = sb.overflow ? COLOR_BLACK : COLOR_DKGRAY;
    fill_rec(dev, &sb.bar, COLOR_LTGRAY);
    set_col(dev, COLOR_BLACK, COLOR_LTGRAY);

    if (horiz) {
        const int ty = sb.bar.top, by = sb.bar.bottom;
        drw_lin(dev, sb.bar.left, sb.a_btn.right, sb.bar.left, by - 1);
        drw_lin(dev, sb.b_btn.left, ty, sb.b_btn.left, by);

        fill_rec(dev, &sb.a_btn, COLOR_LTGRAY);
        drw_rec(dev, &sb.a_btn);
        set_col(dev, pen, COLOR_LTGRAY);
        int ax = sb.a_btn.left;
        drw_lin(dev, ax + 11, ty + 4,  ax + 4,  ty + 8);
        drw_lin(dev, ax + 4,  ty + 8,  ax + 11, ty + 12);
        drw_lin(dev, ax + 11, ty + 4,  ax + 11, ty + 12);

        fill_rec(dev, &sb.b_btn, COLOR_LTGRAY);
        drw_rec(dev, &sb.b_btn);
        set_col(dev, pen, COLOR_LTGRAY);
        int bx = sb.b_btn.left;
        drw_lin(dev, bx + 5,  ty + 4,  bx + 12, ty + 8);
        drw_lin(dev, bx + 12, ty + 8,  bx + 5,  ty + 12);
        drw_lin(dev, bx + 5,  ty + 4,  bx + 5,  ty + 12);
    } else {
        const int sx = sb.bar.left, ex = sb.bar.right;
        drw_lin(dev, sx, sb.a_btn.bottom, ex - 1, sb.a_btn.bottom);
        drw_lin(dev, sx, sb.b_btn.top, ex - 1, sb.b_btn.top);

        fill_rec(dev, &sb.a_btn, COLOR_LTGRAY);
        drw_rec(dev, &sb.a_btn);
        set_col(dev, pen, COLOR_LTGRAY);
        int ay = sb.a_btn.top;
        drw_lin(dev, sx + 8, ay + 4,  sx + 4,  ay + 11);
        drw_lin(dev, sx + 8, ay + 4,  sx + 12, ay + 11);
        drw_lin(dev, sx + 4, ay + 11, sx + 12, ay + 11);

        fill_rec(dev, &sb.b_btn, COLOR_LTGRAY);
        drw_rec(dev, &sb.b_btn);
        set_col(dev, pen, COLOR_LTGRAY);
        int by = sb.b_btn.top;
        drw_lin(dev, sx + 4,  by + 5,  sx + 12, by + 5);
        drw_lin(dev, sx + 4,  by + 5,  sx + 8,  by + 12);
        drw_lin(dev, sx + 12, by + 5,  sx + 8,  by + 12);
    }

    fill_rec(dev, &sb.thumb, sb.overflow ? COLOR_GRAY : COLOR_LTGRAY);
    set_col(dev, COLOR_BLACK, COLOR_LTGRAY);
    drw_rec(dev, &sb.thumb);
    drw_lin(dev, sb.thumb.left, sb.thumb.top, sb.thumb.right - 1, sb.thumb.top);
    drw_lin(dev, sb.thumb.left, sb.thumb.top, sb.thumb.left, sb.thumb.bottom - 1);
    drw_lin(dev, sb.thumb.left, sb.thumb.bottom - 1, sb.thumb.right - 1, sb.thumb.bottom - 1);
    drw_lin(dev, sb.thumb.right - 1, sb.thumb.top, sb.thumb.right - 1, sb.thumb.bottom - 1);
}

/* ── Canvas backdrop (checkerboard / solid) ──────────────────────────────── */
static void paint_draw_canvas(PaintViewer *pv, GDEV *dev) {
    paint_view_t v;
    paint_compute(pv, dev, &v);
    COLOR base = pv->canvas_checker ? COLOR_LTGRAY : COLOR_WHITE;

    RECT canvas = { v.view_x, v.view_y, v.view_x + v.view_w, v.view_y + v.view_h };
    fill_rec(dev, &canvas, base);

    if (!pv->canvas_checker) return;

    /* Checker only where the canvas is exposed; clipped to the viewport. */
    const int cell = PAINT_CANVAS_H / 2;
    COLOR alt = COLOR_DKGRAY;
    for (int cy = 0; cy < v.view_h; cy += cell) {
        for (int cx = 0; cx < v.view_w; cx += cell) {
            BOOL dark = (((cx / cell) + (cy / cell)) & 1) != 0;
            if (!dark) continue;
            RECT tile = { v.view_x + cx, v.view_y + cy,
                          v.view_x + cx + cell, v.view_y + cy + cell };
            if (tile.right > canvas.right) tile.right = canvas.right;
            if (tile.bottom > canvas.bottom) tile.bottom = canvas.bottom;
            fill_rec(dev, &tile, alt);
        }
    }
}

/* ── Image blit: nearest-neighbour scale, honouring alpha ────────────────── */
static void paint_draw_image(PaintViewer *pv, GDEV *dev) {
    if (!pv->pixels || pv->img_w <= 0 || pv->img_h <= 0) return;

    paint_view_t v;
    paint_compute(pv, dev, &v);
    if (v.scaled_w < 1 || v.scaled_h < 1) return;

    int ox, oy;
    paint_canvas_origin(pv, dev, &ox, &oy);

    /* Destination run within the viewport */
    int dx0 = ox; if (dx0 < v.view_x) dx0 = v.view_x;
    int dy0 = oy; if (dy0 < v.view_y) dy0 = v.view_y;
    int dx1 = ox + v.scaled_w; if (dx1 > v.view_x + v.view_w) dx1 = v.view_x + v.view_w;
    int dy1 = oy + v.scaled_h; if (dy1 > v.view_y + v.view_h) dy1 = v.view_y + v.view_h;

    /* Bounding the destination to the device keeps the index arithmetic safe;
     * set_clip() alone would still let the loop compute out-of-range rows. */
    if (dx1 > dev->width) dx1 = dev->width;
    if (dy1 > dev->height) dy1 = dev->height;
    if (dx0 < 0) dx0 = 0;
    if (dy0 < 0) dy0 = 0;

    for (int dy = dy0; dy < dy1; dy++) {
        int sy = ((dy - oy) * pv->img_h) / v.scaled_h;
        if (sy < 0) sy = 0;
        if (sy >= pv->img_h) sy = pv->img_h - 1;
        COLOR *row = dev->pixels + (size_t)dy * dev->width;
        const UB *src_row = pv->pixels + (size_t)sy * pv->img_w * 4;
        for (int dx = dx0; dx < dx1; dx++) {
            int sx = ((dx - ox) * pv->img_w) / v.scaled_w;
            if (sx < 0) sx = 0;
            if (sx >= pv->img_w) sx = pv->img_w - 1;
            const UB *p = src_row + (size_t)sx * 4;
            if (p[3] == 0) continue;   /* transparent: show canvas */
            if (p[3] == 255) {
                row[dx] = 0xFF000000 | ((COLOR)p[0] << 16) | ((COLOR)p[1] << 8) | (COLOR)p[2];
            } else {
                COLOR bg = row[dx];
                UB bg_r = (bg >> 16) & 0xFF;
                UB bg_g = (bg >> 8) & 0xFF;
                UB bg_b = bg & 0xFF;
                UB a = p[3];
                UB r = (UB)((p[0] * a + bg_r * (255 - a)) / 255);
                UB g = (UB)((p[1] * a + bg_g * (255 - a)) / 255);
                UB b = (UB)((p[2] * a + bg_b * (255 - a)) / 255);
                row[dx] = 0xFF000000 | ((COLOR)r << 16) | ((COLOR)g << 8) | (COLOR)b;
            }
        }
    }
}

/* ── Status bar ──────────────────────────────────────────────────────────── */
static void paint_draw_status(PaintViewer *pv, GDEV *dev) {
    RECT status = { 0, dev->height - PAINT_STATUS_H, dev->width, dev->height };
    fill_rec(dev, &status, COLOR_LTGRAY);
    drw_lin(dev, 0, dev->height - PAINT_STATUS_H, dev->width, dev->height - PAINT_STATUS_H);

    char msg[256];
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
    if (pv->pixels) {
        snprintf(msg, sizeof(msg), "Paint | %s %dx%d | ズーム %d%% | 位置 (%d,%d)",
                 pv->title, (int)pv->img_w, (int)pv->img_h, pv->zoom,
                 pv->scroll_x, pv->scroll_y);
    } else {
        snprintf(msg, sizeof(msg), "Paint 画像ビューア | 画像を読み込んでください (%d 件利用可)",
                 pv->img_count);
    }
#else
    snprintf(msg, sizeof(msg), "Paint 画像ビューア | ホストビルドでのみ画像を読み込めます");
#endif
    drw_tc_string(dev, 8, dev->height - 17, msg, COLOR_BLACK, 0x00000000);
}

static void paint_sync_menu_state(PaintViewer *pv) {
    if (!pv) return;
    (void)pv;   /* state lives in menu_bar; kept for parity with the shell apps */
}

/* ── Paint bridge ────────────────────────────────────────────────────────── */
static void paint_view(WND *wnd, GDEV *dev) {
    if (!wnd || !dev) return;
    PaintViewer *pv = (wnd->user_data) ? (PaintViewer*)(uintptr_t)wnd->user_data : &g_active_paint;

    RECT canvas_rect = { 0, 0, dev->width, dev->height };
    fill_rec(dev, &canvas_rect, COLOR_WHITE);

    if (pv->menu_bar.header_count == 0) paint_init_menu_bar(pv);

    RECT orig_clip = dev->clip;
    paint_view_t v;
    paint_compute(pv, dev, &v);
    RECT content_clip = { v.view_x, v.view_y, v.view_x + v.view_w, v.view_y + v.view_h };
    set_clip(dev, &content_clip);

    paint_clamp_scroll(pv, dev);
    paint_draw_canvas(pv, dev);
    paint_draw_image(pv, dev);

    set_clip(dev, &orig_clip);

    /* Menu bar */
    char zoom_buf[40];
    snprintf(zoom_buf, sizeof(zoom_buf), "[%d%%] 画像閲覧", pv->zoom);
    app_menu_set_right_text(&pv->menu_bar, zoom_buf);
    app_menu_paint_bar(&pv->menu_bar, dev);

    /* Scrollbars */
    paint_draw_scrollbar(pv, dev, FALSE);
    paint_draw_scrollbar(pv, dev, TRUE);

    /* Status bar */
    paint_draw_status(pv, dev);

    /* Dropdown overlay */
    if (pv->menu_bar.active_menu >= 0) {
        app_menu_paint_dropdown(&pv->menu_bar, dev);
        if (pv->menu_bar.active_menu == 0 && pv->menu_bar.active_submenu >= 0) {
            app_menu_paint_cascading_strings(&pv->menu_bar, dev, pv->imgs, pv->img_count);
        }
    }
}

/* ── Menu construction ───────────────────────────────────────────────────── */
static void paint_init_menu_bar(PaintViewer *pv) {
    if (!pv) return;
    app_menu_init(&pv->menu_bar, APP_MENU_STYLE_CLASSIC_3D);

    int h0 = app_menu_add_header(&pv->menu_bar, "ファイル(F)", 104);
    app_menu_add_submenu_item(&pv->menu_bar, h0, "開く (Open Image...) ▶", PCMD_FILE_OPEN_CASCADE, 1);
    app_menu_add_item(&pv->menu_bar, h0, "パス指定で開く (Open Path...)", "Ctrl+O", PCMD_FILE_OPEN_PROMPT, TRUE);
    app_menu_add_item(&pv->menu_bar, h0, "再読込 (Reload)", "Ctrl+R", PCMD_FILE_RELOAD, TRUE);
    app_menu_add_separator(&pv->menu_bar, h0);
    app_menu_add_item(&pv->menu_bar, h0, "画像を閉じる (Close Image)", "Ctrl+W", PCMD_FILE_CLOSE_IMG, TRUE);
    app_menu_add_item(&pv->menu_bar, h0, "終了 (Quit Window)", "Ctrl+Q", PCMD_FILE_QUIT, TRUE);

    int h1 = app_menu_add_header(&pv->menu_bar, "表示(V)", 72);
    app_menu_add_item(&pv->menu_bar, h1, "拡大 (Zoom In)", "+", PCMD_VIEW_ZOOM_IN, TRUE);
    app_menu_add_item(&pv->menu_bar, h1, "縮小 (Zoom Out)", "-", PCMD_VIEW_ZOOM_OUT, TRUE);
    app_menu_add_item(&pv->menu_bar, h1, "原寸大 (100%)", "0", PCMD_VIEW_ZOOM_100, TRUE);
    app_menu_add_item(&pv->menu_bar, h1, "ウィンドウに合わせる (Fit)", "F", PCMD_VIEW_ZOOM_FIT, TRUE);
    app_menu_add_separator(&pv->menu_bar, h1);
    app_menu_add_item(&pv->menu_bar, h1, "左上へ (Scroll Origin)", "Home", PCMD_VIEW_ORIGIN, TRUE);
    app_menu_add_item(&pv->menu_bar, h1, "背景: 市松/白 (Canvas)", "B", PCMD_VIEW_CANVAS_TOGGLE, TRUE);

    int h2 = app_menu_add_header(&pv->menu_bar, "ヘルプ(H)", 88);
    app_menu_add_item(&pv->menu_bar, h2, "ペイント について (About)", "", PCMD_HELP_ABOUT, TRUE);
    app_menu_add_item(&pv->menu_bar, h2, "実身ドキュメント (Paint Doc...)", "", PCMD_HELP_DOC, TRUE);

    paint_sync_menu_state(pv);
}

WND* open_paint_about_window(void) {
    return app_menu_create_about_dialog("Paint", "画像閲覧",
                                        "Cleanroom BTRON GIF/PNG Viewer",
                                        "Brought to B-System by 5HT",
                                        260, 180);
}

/* ── Event handling ──────────────────────────────────────────────────────── */
static void paint_apply_cmd(WND *wnd, PaintViewer *pv, int cmd, int sub_idx, GDEV *dev) {
    switch (cmd) {
        case PCMD_FILE_OPEN_CASCADE:
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
            if (sub_idx >= 0 && sub_idx < pv->img_count) {
                paint_load(pv, pv->imgs[sub_idx]);
                if (strstr(pv->imgs[sub_idx], "lil-cube-64") || strstr(pv->imgs[sub_idx], "lil-cube-n64")) {
                    pv->zoom = 450;
                }
                if (dev) paint_clamp_scroll(pv, dev);
                if (wnd) {
                    strncpy(wnd->title, pv->title[0] ? pv->title : "Paint 画像ビューア",
                            sizeof(wnd->title) - 1);
                    wnd->title[sizeof(wnd->title) - 1] = '\0';
                    inval_wnd(wnd);
                }
            }
#endif
            break;
        case PCMD_FILE_RELOAD:
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
            if (pv->path[0]) paint_load(pv, pv->path);
            if (wnd) inval_wnd(wnd);
#endif
            break;
        case PCMD_FILE_CLOSE_IMG:
            paint_free_image(pv);
            pv->scroll_x = pv->scroll_y = 0;
            if (wnd) {
                strncpy(wnd->title, "Paint 画像ビューア", sizeof(wnd->title) - 1);
                wnd->title[sizeof(wnd->title) - 1] = '\0';
                inval_wnd(wnd);
            }
            break;
        case PCMD_FILE_QUIT:
            if (wnd) cls_wnd(wnd);
            break;
        case PCMD_VIEW_ZOOM_IN:
            paint_zoom_to(pv, pv->zoom + (pv->zoom < 100 ? 10 : 25));
            if (dev) paint_clamp_scroll(pv, dev);
            if (wnd) inval_wnd(wnd);
            break;
        case PCMD_VIEW_ZOOM_OUT:
            paint_zoom_to(pv, pv->zoom - (pv->zoom <= 100 ? 10 : 25));
            if (dev) paint_clamp_scroll(pv, dev);
            if (wnd) inval_wnd(wnd);
            break;
        case PCMD_VIEW_ZOOM_100:
            pv->zoom = 100;
            if (dev) paint_clamp_scroll(pv, dev);
            if (wnd) inval_wnd(wnd);
            break;
        case PCMD_VIEW_ZOOM_FIT:
            if (dev) {
                paint_zoom_fit(pv, dev);
                pv->scroll_x = pv->scroll_y = 0;
                inval_wnd(wnd);
            }
            break;
        case PCMD_VIEW_ORIGIN:
            pv->scroll_x = pv->scroll_y = 0;
            if (wnd) inval_wnd(wnd);
            break;
        case PCMD_VIEW_CANVAS_TOGGLE:
            pv->canvas_checker = !pv->canvas_checker;
            if (wnd) inval_wnd(wnd);
            break;
        case PCMD_HELP_ABOUT:
            open_paint_about_window();
            break;
        case PCMD_HELP_DOC:
            if (open_tad_browser_window) {
                open_tad_browser_window("b-system/apps/Paint.tad", "Paint 使用説明");
            }
            break;
        default:
            break;
    }
}

static BOOL paint_point_in(const RECT *r, int x, int y) {
    return (x >= r->left && x < r->right && y >= r->top && y < r->bottom) ? TRUE : FALSE;
}

static void paint_handle_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;
    PaintViewer *pv = (wnd->user_data) ? (PaintViewer*)(uintptr_t)wnd->user_data : &g_active_paint;
    GDEV *dev = wnd->dev;

    H rel_x = evt->pos.x - (wnd->bounds.left + 4);
    H rel_y = evt->pos.y - (wnd->bounds.top + 26);

    if (evt->type == EV_MOUSE_MOVE) {
        if (app_menu_handle_mouse_move(&pv->menu_bar, rel_x, rel_y)) {
            inval_wnd(wnd);
            return;
        }
        if (dev && (pv->sb_drag_v || pv->sb_drag_h)) {
            paint_sbar_t sb;
            if (pv->sb_drag_v) {
                paint_sbar_layout(pv, dev, &sb, FALSE);
                paint_view_t v;
                paint_compute(pv, dev, &v);
                int travel = (sb.track_b - sb.track_a) - (sb.thumb.bottom - sb.thumb.top);
                if (travel > 0 && v.max_scroll_y > 0) {
                    int dy = rel_y - pv->sb_drag_start_y;
                    paint_set_scroll(pv, dev, pv->scroll_x,
                                     pv->sb_drag_start_scroll_y + (dy * v.max_scroll_y) / travel);
                    inval_wnd(wnd);
                }
            } else {
                paint_sbar_layout(pv, dev, &sb, TRUE);
                paint_view_t v;
                paint_compute(pv, dev, &v);
                int travel = (sb.track_b - sb.track_a) - (sb.thumb.right - sb.thumb.left);
                if (travel > 0 && v.max_scroll_x > 0) {
                    int dx = rel_x - pv->sb_drag_start_x;
                    paint_set_scroll(pv, dev,
                                     pv->sb_drag_start_scroll_x + (dx * v.max_scroll_x) / travel,
                                     pv->scroll_y);
                    inval_wnd(wnd);
                }
            }
        }
        return;
    }

    if (evt->type == EV_BUT_DOWN) {
        int cmd = 0, sub_idx = -1;
        if (app_menu_handle_mouse_down(&pv->menu_bar, rel_x, rel_y, &cmd, &sub_idx)) {
            if (cmd != 0) paint_apply_cmd(wnd, pv, cmd, sub_idx, dev);
            inval_wnd(wnd);
            return;
        }

        if (!dev) return;

        /* Vertical scrollbar */
        paint_sbar_t vsb;
        paint_sbar_layout(pv, dev, &vsb, FALSE);
        if (paint_point_in(&vsb.bar, rel_x, rel_y)) {
            if (vsb.overflow) {
                paint_view_t v;
                paint_compute(pv, dev, &v);
                if (paint_point_in(&vsb.a_btn, rel_x, rel_y)) {
                    paint_set_scroll(pv, dev, pv->scroll_x, pv->scroll_y - PAINT_CANVAS_H);
                } else if (paint_point_in(&vsb.b_btn, rel_x, rel_y)) {
                    paint_set_scroll(pv, dev, pv->scroll_x, pv->scroll_y + PAINT_CANVAS_H);
                } else if (rel_y < vsb.thumb.top || rel_y >= vsb.thumb.bottom) {
                    int pg = v.view_h - 2 * PAINT_SB_BTN; if (pg < 1) pg = 1;
                    paint_set_scroll(pv, dev, pv->scroll_x,
                                     pv->scroll_y + ((rel_y < vsb.thumb.top) ? -pg : pg));
                } else {
                    pv->sb_drag_v = TRUE;
                    pv->sb_drag_start_y = rel_y;
                    pv->sb_drag_start_scroll_y = pv->scroll_y;
                }
                inval_wnd(wnd);
            }
            return;
        }

        /* Horizontal scrollbar */
        paint_sbar_t hsb;
        paint_sbar_layout(pv, dev, &hsb, TRUE);
        if (paint_point_in(&hsb.bar, rel_x, rel_y)) {
            if (hsb.overflow) {
                paint_view_t v;
                paint_compute(pv, dev, &v);
                if (paint_point_in(&hsb.a_btn, rel_x, rel_y)) {
                    paint_set_scroll(pv, dev, pv->scroll_x - PAINT_CANVAS_H, pv->scroll_y);
                } else if (paint_point_in(&hsb.b_btn, rel_x, rel_y)) {
                    paint_set_scroll(pv, dev, pv->scroll_x + PAINT_CANVAS_H, pv->scroll_y);
                } else if (rel_x < hsb.thumb.left || rel_x >= hsb.thumb.right) {
                    int pg = v.view_w - 2 * PAINT_SB_BTN; if (pg < 1) pg = 1;
                    paint_set_scroll(pv, dev,
                                     pv->scroll_x + ((rel_x < hsb.thumb.left) ? -pg : pg),
                                     pv->scroll_y);
                } else {
                    pv->sb_drag_h = TRUE;
                    pv->sb_drag_start_x = rel_x;
                    pv->sb_drag_start_scroll_x = pv->scroll_x;
                }
                inval_wnd(wnd);
            }
            return;
        }

        /* Canvas click: nothing to select in a viewer. */
        return;
    }

    if (evt->type == EV_BUT_UP) {
        if (pv->sb_drag_v || pv->sb_drag_h) {
            pv->sb_drag_v = FALSE;
            pv->sb_drag_h = FALSE;
            inval_wnd(wnd);
        }
        return;
    }

    if (evt->type == EV_KEY_DOWN) {
        UW key = evt->key;

        if (key == BTRON_KEY_ESCAPE) {
            int cmd = 0;
            app_menu_handle_key(&pv->menu_bar, key, (uint16_t)(uintptr_t)evt->data, &cmd);
            inval_wnd(wnd);
            return;
        }

        int cmd = 0;
        if (app_menu_handle_key(&pv->menu_bar, key, (uint16_t)(uintptr_t)evt->data, &cmd)) {
            if (cmd != 0) paint_apply_cmd(wnd, pv, cmd, -1, dev);
            inval_wnd(wnd);
            return;
        }

        const int STEP = PAINT_CANVAS_H;
        if (key == '+' || key == '=') {
            paint_apply_cmd(wnd, pv, PCMD_VIEW_ZOOM_IN, -1, dev);
        } else if (key == '-') {
            paint_apply_cmd(wnd, pv, PCMD_VIEW_ZOOM_OUT, -1, dev);
        } else if (key == '0') {
            paint_apply_cmd(wnd, pv, PCMD_VIEW_ZOOM_100, -1, dev);
        } else if (key == 'f' || key == 'F') {
            paint_apply_cmd(wnd, pv, PCMD_VIEW_ZOOM_FIT, -1, dev);
        } else if (key == 'b' || key == 'B') {
            paint_apply_cmd(wnd, pv, PCMD_VIEW_CANVAS_TOGGLE, -1, dev);
        } else if (key == BTRON_KEY_HOME) {
            paint_apply_cmd(wnd, pv, PCMD_VIEW_ORIGIN, -1, dev);
        } else if (key == BTRON_KEY_UP) {
            paint_set_scroll(pv, dev, pv->scroll_x, pv->scroll_y - STEP); inval_wnd(wnd);
        } else if (key == BTRON_KEY_DOWN) {
            paint_set_scroll(pv, dev, pv->scroll_x, pv->scroll_y + STEP); inval_wnd(wnd);
        } else if (key == BTRON_KEY_LEFT) {
            paint_set_scroll(pv, dev, pv->scroll_x - STEP, pv->scroll_y); inval_wnd(wnd);
        } else if (key == BTRON_KEY_RIGHT) {
            paint_set_scroll(pv, dev, pv->scroll_x + STEP, pv->scroll_y); inval_wnd(wnd);
        } else if (key == BTRON_KEY_PAGE_UP) {
            paint_view_t v; if (dev) paint_compute(pv, dev, &v); else { v.view_h = 0; v.view_w = 0; }
            paint_set_scroll(pv, dev, pv->scroll_x, pv->scroll_y - (v.view_h - 2 * STEP));
            inval_wnd(wnd);
        } else if (key == BTRON_KEY_PAGE_DOWN || key == BTRON_KEY_SPACE) {
            paint_view_t v; if (dev) paint_compute(pv, dev, &v); else { v.view_h = 0; v.view_w = 0; }
            paint_set_scroll(pv, dev, pv->scroll_x, pv->scroll_y + (v.view_h - 2 * STEP));
            inval_wnd(wnd);
        }
        return;
    }
}

static void destroy_paint_wnd(WND *wnd) {
    if (wnd && wnd->user_data) {
        PaintViewer *pv = (PaintViewer*)(uintptr_t)wnd->user_data;
        paint_free_image(pv);
        free(pv);
        wnd->user_data = 0;
    }
}

static BOOL paint_menu_open(WND *wnd) {
    if (!wnd) return FALSE;
    PaintViewer *pv = (wnd->user_data) ? (PaintViewer*)(uintptr_t)wnd->user_data : &g_active_paint;
    return (pv && pv->menu_bar.active_menu >= 0) ? TRUE : FALSE;
}

static WND* open_paint_wnd(const char *filepath) {
    PaintViewer *pv = (PaintViewer*)calloc(1, sizeof(PaintViewer));
    if (!pv) return NULL;

    pv->zoom = 100;
    pv->canvas_checker = TRUE;
    pv->menu_bar.active_menu = -1;
    pv->menu_bar.hover_menu = -1;
    pv->menu_bar.hover_item = -1;
    pv->menu_bar.active_submenu = -1;
    pv->menu_bar.hover_subitem = -1;
    paint_init_menu_bar(pv);

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
    paint_scan_images(pv);
    if (filepath && filepath[0]) {
        paint_load(pv, filepath);
    } else {
        paint_load(pv, "assets/pixart/lil-cube-64.png");
        if (pv->pixels == NULL) {
            paint_load(pv, "assets/mascot/lil-cube-64.png");
        }
        if (pv->pixels == NULL) {
            paint_load(pv, "assets/mascot/lil-cube-n64.jpg");
        }
        if (pv->pixels == NULL) {
            paint_load(pv, "assets/pixart/nyan-cat.gif");
        }
        if (pv->pixels == NULL && pv->img_count > 0) {
            paint_load(pv, pv->imgs[0]);
        }
    }
    if (pv->path[0] && (strstr(pv->path, "lil-cube-64") || strstr(pv->path, "lil-cube-n64"))) {
        pv->zoom = 450;
    } else if (!filepath || !filepath[0]) {
        pv->zoom = 450;
    }
#else
    (void)filepath;
#endif

    const char *w_title = (pv->title[0]) ? pv->title : "Paint 画像ビューア";
    WND *wnd = opn_wnd(w_title, 80, 60, 720, 520,
                       WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_RESIZE | WND_ATTR_BORDER);
    if (wnd) {
        wnd->user_data = (VW)(uintptr_t)pv;
        wnd->paint = paint_view;
        wnd->event_handler = paint_handle_event;
        wnd->destroy = destroy_paint_wnd;
        wnd->menu_open = paint_menu_open;
    } else {
        paint_free_image(pv);
        free(pv);
    }
    return wnd;
}

WND* open_paint_window(void) {
    return open_paint_wnd(NULL);
}

WND* open_paint_window_with_file(const char *filepath) {
    return open_paint_wnd(filepath);
}
