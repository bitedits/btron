/*
 * src/gl/egl_surface.c — EGL-lite Surface Bridge for B-System
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "egl_surface.h"
#include <btron/btron.h>
#include <stdlib.h>

EGL_SURFACE* egl_create_window_surface(WND *wnd) {
    if (!wnd || !wnd->dev || !wnd->dev->pixels) {
        return NULL;
    }

    EGL_SURFACE *surf = (EGL_SURFACE *)calloc(1, sizeof(EGL_SURFACE));
    if (!surf) {
        return NULL;
    }

    surf->wnd = wnd;
    surf->width = wnd->dev->width;
    surf->height = wnd->dev->height;
    surf->pixels = wnd->dev->pixels;

#if defined(BTRON_UEFI_TARGET)
    gl_init(GL_BACKEND_TINYGL, surf->width, surf->height, surf->pixels);
#else
    gl_init(GL_BACKEND_VIRGL, surf->width, surf->height, surf->pixels);
#endif
    return surf;
}

#if !defined(BTRON_UEFI_TARGET)
#include "backend_virgl.h"
#endif

void egl_make_current(EGL_SURFACE *surf) {
    if (!surf || !surf->wnd || !surf->wnd->dev) return;
    surf->pixels = surf->wnd->dev->pixels;
#if !defined(BTRON_UEFI_TARGET)
    virgl_backend_make_current(surf->width, surf->height, surf->pixels);
#endif
}

void egl_surface_resize(EGL_SURFACE *surf, int width, int height) {
    if (!surf || !surf->wnd || !surf->wnd->dev) return;
    surf->width = width;
    surf->height = height;
    surf->pixels = surf->wnd->dev->pixels;
    gl_resize(width, height, surf->pixels);
}

void egl_swap_buffers(EGL_SURFACE *surf) {
    if (!surf) return;
    if (g_gl && g_gl->swap_buffers) {
        g_gl->swap_buffers();
    }
    if (surf->wnd) {
        inval_wnd(surf->wnd);
    }
}

void egl_destroy_surface(EGL_SURFACE *surf) {
    if (!surf) return;
    gl_shutdown();
    free(surf);
}
