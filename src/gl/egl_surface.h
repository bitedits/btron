/*
 * src/gl/egl_surface.h — EGL-lite Surface Bridge for B-System
 *
 * Connects a B-System WND / GDEV surface to the active GL backend.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef BTRON_EGL_SURFACE_H
#define BTRON_EGL_SURFACE_H

#include <btron/wnd.h>
#include <btron/dp.h>
#include "gl_dispatch.h"

typedef struct {
    WND    *wnd;
    int     width;
    int     height;
    COLOR  *pixels;
} EGL_SURFACE;

/* Bind or create GL surface for window */
EGL_SURFACE* egl_create_window_surface(WND *wnd);
void         egl_surface_resize(EGL_SURFACE *surf, int width, int height);
void         egl_swap_buffers(EGL_SURFACE *surf);
void         egl_destroy_surface(EGL_SURFACE *surf);

#endif /* BTRON_EGL_SURFACE_H */
