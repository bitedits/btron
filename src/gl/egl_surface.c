/*
 * src/gl/egl_surface.c — EGL-lite Surface Bridge for B-System
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "egl_surface.h"
#include "gl_dispatch.h"
#include "backend_virgl.h"
#if defined(BTRON_GL_BACKEND_TINYGL)
#include "backend_tinygl.h"
#endif
#include <btron/btron.h>
#include <stdlib.h>

/* The backend a new surface asks for.  An image gets the choice it links: only
 * the target lists that carry src/gl/tinygl/ define BTRON_GL_BACKEND_TINYGL, and
 * the PS2 port is the one image that links both software rasterizers, so it is
 * the one that can be pointed at the other one from the Makefile (PS2_GL=virgl)
 * for a timing comparison without touching this file. */
#if defined(BTRON_GL_BACKEND_TINYGL) && !defined(BTRON_GL_PREFER_VIRGL)
#  define EGL_DEFAULT_BACKEND GL_BACKEND_TINYGL
#else
#  define EGL_DEFAULT_BACKEND GL_BACKEND_VIRGL
#endif

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

    gl_init(EGL_DEFAULT_BACKEND, surf->width, surf->height, surf->pixels);
    return surf;
}

void egl_make_current(EGL_SURFACE *surf) {
    if (!surf || !surf->wnd || !surf->wnd->dev) return;
    surf->pixels = surf->wnd->dev->pixels;
#if defined(BTRON_GL_BACKEND_TINYGL)
    if (gl_active_backend() == GL_BACKEND_TINYGL) {
        tinygl_backend_resize(surf->width, surf->height, surf->pixels);
        return;
    }
#endif
    virgl_backend_make_current_ctx(&surf->gl_ctx, surf->width, surf->height,
                                   surf->pixels);
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
    if (gl_active_backend() == GL_BACKEND_TINYGL) {
        gl_shutdown();
    } else if (surf->gl_ctx) {
        virgl_backend_destroy_ctx(surf->gl_ctx);
        surf->gl_ctx = NULL;
    }
    free(surf);
}
