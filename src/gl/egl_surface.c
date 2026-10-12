/*
 * src/gl/egl_surface.c — EGL-lite Surface Bridge for B-System
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "egl_surface.h"
#include "gl_dispatch.h"
#include "gl_backend_choice.h"
#include "backend_virgl.h"
#if defined(BTRON_GL_BACKEND_TINYGL)
#include "backend_tinygl.h"
#endif
#include <btron/btron.h>
#include <stdlib.h>

/* The backend a new surface asks for is the user's choice where the image offers one, and
 * the build's answer otherwise -- both rules live in src/gl/gl_backend_choice.c, which
 * knows that only the targets listing src/gl/tinygl/ define BTRON_GL_BACKEND_TINYGL, that
 * `PS2_GL=virgl` asks for the other one, and that the Display applet's "OpenGL" row can
 * now outrank either without a rebuild. */

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

    /* gl_init() answers with the pipeline it could install, not with the one that was asked
     * for, so the layer is claimed only when the GS is actually the rasteriser: an image with
     * no TinyGL linked resolves EEGS to a software backend, and that window's pixmap is the
     * picture and must keep being blitted. */
    gl_init(gl_backend_selected(), surf->width, surf->height, surf->pixels);
    if (gl_active_backend() == GL_BACKEND_EEGS) {
        wnd->gl_layer = TRUE;
    }
    return surf;
}

void egl_make_current(EGL_SURFACE *surf) {
    if (!surf || !surf->wnd || !surf->wnd->dev) return;
    surf->pixels = surf->wnd->dev->pixels;
#if defined(BTRON_GL_BACKEND_TINYGL)
    if (gl_tinygl_pipeline()) {
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
    if (gl_tinygl_pipeline()) {
        gl_shutdown();
    } else if (surf->gl_ctx) {
        virgl_backend_destroy_ctx(surf->gl_ctx);
        surf->gl_ctx = NULL;
    }
    free(surf);
}
