/*
 * B-System (BTRON 3.20) Display Primitives Implementation: dp_core.c
 */

#include <btron/dp.h>
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include <stdlib.h>
#include <string.h>
#else
#include <stddef.h>
#include <stdint.h>
extern void* Imalloc(size_t sz);
extern void Ifree(void *ptr);
extern void* Icalloc(size_t nmemb, size_t sz);
#define malloc Imalloc
#define free Ifree
#define calloc Icalloc
static inline int abs(int n) { return n < 0 ? -n : n; }
#endif

GDEV* opn_dev(H w, H h) {
    if (w <= 0 || h <= 0) return NULL;
    GDEV *dev = (GDEV*)calloc(1, sizeof(GDEV));
    if (!dev) return NULL;

    dev->width = w;
    dev->height = h;
    dev->is_vram = 0;

    dev->pixels = (COLOR*)calloc(w * h, sizeof(COLOR));
    if (!dev->pixels) {
        free(dev);
        return NULL;
    }

    dev->clip.left = 0;
    dev->clip.top = 0;
    dev->clip.right = w;
    dev->clip.bottom = h;

    return dev;
}

GDEV* opn_dev_vram(H w, H h, COLOR *vram_buffer) {
    if (w <= 0 || h <= 0 || !vram_buffer) return NULL;
    GDEV *dev = (GDEV*)calloc(1, sizeof(GDEV));
    if (!dev) return NULL;

    dev->width = w;
    dev->height = h;
    dev->is_vram = 1;
    dev->pixels = vram_buffer;

    dev->clip.left = 0;
    dev->clip.top = 0;
    dev->clip.right = w;
    dev->clip.bottom = h;

    return dev;
}

void cls_dev(GDEV *dev) {
    if (dev) {
        if (!dev->is_vram && dev->pixels) {
            free(dev->pixels);
            dev->pixels = NULL;
        }
        free(dev);
    }
}

void set_clip(GDEV *dev, const RECT *clip) {
    if (!dev) return;
    if (clip) {
        dev->clip = *clip;
        if (dev->clip.left < 0) dev->clip.left = 0;
        if (dev->clip.top < 0) dev->clip.top = 0;
        if (dev->clip.right > dev->width) dev->clip.right = dev->width;
        if (dev->clip.bottom > dev->height) dev->clip.bottom = dev->height;
    } else {
        dev->clip.left = 0;
        dev->clip.top = 0;
        dev->clip.right = dev->width;
        dev->clip.bottom = dev->height;
    }
}

ER drw_pnt(GDEV *dev, H x, H y) {
    if (!dev || !dev->pixels) return E_PAR;
    if (x < dev->clip.left || x >= dev->clip.right ||
        y < dev->clip.top || y >= dev->clip.bottom) {
        return E_OK; /* Clipped */
    }

    dev->pixels[y * dev->width + x] = COLOR_BLACK;
    return E_OK;
}

ER drw_lin(GDEV *dev, H x1, H y1, H x2, H y2) {
    if (!dev || !dev->pixels) return E_PAR;

    volatile COLOR *pix = (volatile COLOR*)dev->pixels;
    H width = dev->width;

    /* Fast path: horizontal line */
    if (y1 == y2) {
        if (y1 < dev->clip.top || y1 >= dev->clip.bottom) return E_OK;
        H min_x = x1 < x2 ? x1 : x2;
        H max_x = x1 < x2 ? x2 : x1;
        if (min_x < dev->clip.left) min_x = dev->clip.left;
        if (max_x >= dev->clip.right) max_x = dev->clip.right - 1;
        if (min_x > max_x) return E_OK;
        volatile COLOR *row = pix + y1 * width;
        for (H x = min_x; x <= max_x; x++) {
            row[x] = COLOR_BLACK;
        }
        return E_OK;
    }

    /* Fast path: vertical line */
    if (x1 == x2) {
        if (x1 < dev->clip.left || x1 >= dev->clip.right) return E_OK;
        H min_y = y1 < y2 ? y1 : y2;
        H max_y = y1 < y2 ? y2 : y1;
        if (min_y < dev->clip.top) min_y = dev->clip.top;
        if (max_y >= dev->clip.bottom) max_y = dev->clip.bottom - 1;
        if (min_y > max_y) return E_OK;
        for (H y = min_y; y <= max_y; y++) {
            pix[y * width + x1] = COLOR_BLACK;
        }
        return E_OK;
    }

    H dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
    H dy = -abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
    H err = dx + dy;

    H x = x1, y = y1;

    while (1) {
        if (x >= dev->clip.left && x < dev->clip.right &&
            y >= dev->clip.top && y < dev->clip.bottom) {
            pix[y * width + x] = COLOR_BLACK;
        }

        if (x == x2 && y == y2) break;
        H e2 = 2 * err;
        if (e2 >= dy) { err += dy; x += sx; }
        if (e2 <= dx) { err += dx; y += sy; }
    }

    return E_OK;
}

ER drw_rec(GDEV *dev, const RECT *r) {
    if (!dev || !r || !dev->pixels) return E_PAR;

    drw_lin(dev, r->left, r->top, r->right - 1, r->top);
    drw_lin(dev, r->right - 1, r->top, r->right - 1, r->bottom - 1);
    drw_lin(dev, r->left, r->bottom - 1, r->right - 1, r->bottom - 1);
    drw_lin(dev, r->left, r->top, r->left, r->bottom - 1);

    return E_OK;
}

ER fill_rec(GDEV *dev, const RECT *r, COLOR col) {
    if (!dev || !r || !dev->pixels) return E_PAR;

    H left   = r->left < dev->clip.left ? dev->clip.left : r->left;
    H top    = r->top < dev->clip.top ? dev->clip.top : r->top;
    H right  = r->right > dev->clip.right ? dev->clip.right : r->right;
    H bottom = r->bottom > dev->clip.bottom ? dev->clip.bottom : r->bottom;

    if (left >= right || top >= bottom) return E_OK;

    volatile COLOR *pix = (volatile COLOR*)dev->pixels;
    H width = dev->width;
    H span = right - left;

    for (H y = top; y < bottom; y++) {
        volatile COLOR *row = pix + (y * width + left);
        for (H x = 0; x < span; x++) {
            row[x] = col;
        }
    }

    return E_OK;
}

ER drw_ovl(GDEV *dev, const RECT *r) {
    if (!dev || !r) return E_PAR;
    /* Draw approximate ellipse outline bounding box r */
    H rx = (r->right - r->left) / 2;
    H ry = (r->bottom - r->top) / 2;
    H cx = r->left + rx;
    H cy = r->top + ry;

    for (H y = r->top; y < r->bottom; y++) {
        for (H x = r->left; x < r->right; x++) {
            H dx = x - cx;
            H dy = y - cy;
            if (rx > 0 && ry > 0) {
                float val = ((float)(dx*dx)/(rx*rx)) + ((float)(dy*dy)/(ry*ry));
                if (val >= 0.85f && val <= 1.15f) {
                    if (x >= dev->clip.left && x < dev->clip.right &&
                        y >= dev->clip.top && y < dev->clip.bottom) {
                        dev->pixels[y * dev->width + x] = COLOR_BLACK;
                    }
                }
            }
        }
    }
    return E_OK;
}

ER fill_ovl(GDEV *dev, const RECT *r, COLOR col) {
    if (!dev || !r) return E_PAR;
    H rx = (r->right - r->left) / 2;
    H ry = (r->bottom - r->top) / 2;
    H cx = r->left + rx;
    H cy = r->top + ry;

    for (H y = r->top; y < r->bottom; y++) {
        for (H x = r->left; x < r->right; x++) {
            H dx = x - cx;
            H dy = y - cy;
            if (rx > 0 && ry > 0) {
                float val = ((float)(dx*dx)/(rx*rx)) + ((float)(dy*dy)/(ry*ry));
                if (val <= 1.0f) {
                    if (x >= dev->clip.left && x < dev->clip.right &&
                        y >= dev->clip.top && y < dev->clip.bottom) {
                        dev->pixels[y * dev->width + x] = col;
                    }
                }
            }
        }
    }
    return E_OK;
}

void set_col(GDEV *dev, COLOR fg, COLOR bg) {
    (void)dev;
    (void)fg;
    (void)bg;
}

void set_pat(GDEV *dev, const PAT *pat) {
    (void)dev;
    (void)pat;
}

/* Default weak stubs for pointer grab & release */
__attribute__((weak)) void sdl_set_mouse_grab(BOOL grabbed) { (void)grabbed; }
__attribute__((weak)) void sdl_toggle_mouse_grab(void) {}
__attribute__((weak)) BOOL sdl_is_mouse_grabbed(void) { return FALSE; }

