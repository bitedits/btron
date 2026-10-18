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
#include <btron/dp.h>        /* btron_render_perf_us(), the overlay's clock */
#include <stdlib.h>

/* A per-window frames-per-second readout written into the TITLE BAR of every
 * OpenGL window -- XMB, Lil Cu, glgears, Quake, and any future GL client -- at
 * the one seam they all share: egl_swap_buffers().  It is a debugging
 * instrument, so it is compiled out with -DBTRON_GL_FPS_OVERLAY=0.
 *
 * The readout goes to the caption rather than the framebuffer on purpose: the
 * title bar is drawn by the window manager from wnd->title, so it shows over
 * every backend -- including the GS/eegs layer, which presents from VRAM and
 * would ignore a pixel burn -- and it never fights the app for the client area.
 * Each window reports its own rate because each EGL_SURFACE carries its own
 * counter and its own window. */
#ifndef BTRON_GL_FPS_OVERLAY
#define BTRON_GL_FPS_OVERLAY 1
#endif

#if BTRON_GL_FPS_OVERLAY

/* strcmp without stdio: this TU has to compile into the -nostdlib images too. */
static int fps_streq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

static void fps_copy_base(EGL_SURFACE *surf, const char *title)
{
    int i = 0;
    for (; i < 63 && title[i]; i++) surf->fps_base[i] = title[i];
    surf->fps_base[i] = '\0';
}

/* Advance this window's counter and, when its counting window closes, rewrite the caption as
 * "<app title>  <fps> FPS".  The number is right-aligned in a fixed three-wide field, so the
 * caption is the same length on every update -- the tab width is reserved once, for the
 * "999 FPS" worst case, and never re-flows.
 *
 * The caption is REPLACED, never extended.  That needs two things the first cut of this
 * readout did not have: the app's own caption is captured exactly once (fps_open is a flag,
 * not `fps_win_us != 0`, because a target whose clock is dp_core.c's weak `return 0` sits on
 * tick 0 forever and re-captured the base from the decorated title every frame -- the readout
 * appended to itself), and a rename by the app itself is adopted as the new base rather than
 * overwritten with a stale one. */
static void fps_overlay_tick(EGL_SURFACE *surf)
{
    WND      *wnd = surf->wnd;
    if (!wnd || surf->fps_dead) return;

    uint32_t now = btron_render_perf_us();

    if (!surf->fps_open) {
        surf->fps_open   = 1u;
        surf->fps_last[0] = '\0';
        fps_copy_base(surf, wnd->title);
        surf->fps_win_us = now;
    } else if (!fps_streq(wnd->title,
                         surf->fps_last[0] ? surf->fps_last : surf->fps_base)) {
        /* The caption is neither our own last write nor the base we captured: the app
         * renamed its window (Quake does, on a level change).  Adopt what it wrote, so the
         * readout rides the app's caption instead of fighting it, and reopen the counting
         * window so the frames we adopt are not counted against the old caption's period. */
        fps_copy_base(surf, wnd->title);
        surf->fps_last[0] = '\0';
        surf->fps_win_us  = now;
        surf->fps_frames  = 0u;
    }
    surf->fps_frames++;

    /* Close the counting window on either bound -- 250 ms or 30 frames, whichever comes
     * first -- so the readout stays live for a 60 Hz app and still answers for one whose
     * frame takes a second.  The uint32 clock wraps every ~71 min, far longer than one
     * window, so the subtraction is the real interval.
     *
     * The arithmetic is 32-bit on purpose.  The obvious `frames * 1000000 / elapsed` needs a
     * 64-bit divide, and the i386 UEFI/PC-98 targets link -nostdlib, where __udivdi3 does not
     * exist: clang emits the call and the image will not link.  `frames` is bounded by the
     * window above, so `frames * 1000` only overflows past 4.29M frames in one window. */
    uint32_t elapsed = now - surf->fps_win_us;
    if (elapsed < 250000u && surf->fps_frames < 30u) return;

    /* A full window that measured no time at all is a core with no clock installed --
     * dp_core.c's weak default, which every target but arm64 and PS2 answers with.  Writing
     * a number derived from it would be printing an invented one, so give up here and leave
     * the app's own caption alone; nothing has been written yet, so there is nothing to undo. */
    if (elapsed == 0u) {
        surf->fps_dead = 1u;
        return;
    }

    uint32_t ms = elapsed / 1000u;
    if (ms == 0u) ms = 1u;
    uint32_t fps = (surf->fps_frames * 1000u) / ms;
    if (fps > 999u) fps = 999u;
    surf->fps_shown  = fps;
    surf->fps_frames = 0u;
    surf->fps_win_us = now;

    /* "<base>  <nnn> FPS", number in a fixed 3-wide space-padded field.  Hand-
     * rolled so this TU stays free of stdio for the freestanding targets. */
    char d[3];
    d[0] = (char)('0' + (fps / 100u) % 10u);
    d[1] = (char)('0' + (fps /  10u) % 10u);
    d[2] = (char)('0' +  fps          % 10u);
    if (d[0] == '0') d[0] = ' ';
    if (d[0] == ' ' && d[1] == '0') d[1] = ' ';

    char     t[64];
    int      n = 0;
    for (int i = 0; surf->fps_base[i] && n < 52; i++) t[n++] = surf->fps_base[i];
    t[n++] = ' '; t[n++] = ' ';
    t[n++] = d[0]; t[n++] = d[1]; t[n++] = d[2];
    t[n++] = ' ';
    t[n++] = 'F'; t[n++] = 'P'; t[n++] = 'S';
    t[n]   = '\0';

    wnd_set_title(wnd, t);

    /* Remember exactly what we wrote: the next tick compares wnd->title against this, and
     * anything else there is the app's own doing, not ours. */
    int i = 0;
    for (; i < 63 && t[i]; i++) surf->fps_last[i] = t[i];
    surf->fps_last[i] = '\0';
}
#endif /* BTRON_GL_FPS_OVERLAY */

/* The compositor's binder, defined below with egl_make_current() because it calls it. */
static void egl_wnd_gl_bind(WND *wnd);

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

    /* Hand the window its own surface and install the compositor's binder, once, from the
     * first GL window that opens.  From here the compositor makes this context current before
     * every paint callback, so an app never has to call egl_make_current() to be correct --
     * and an app that forgets, as XMB and glgears did, can no longer paint into the window
     * that happened to be bound last. */
    wnd->gl_surf = surf;
    wnd_set_gl_bind(egl_wnd_gl_bind);

    /* Put the shared default context on the seam before installing the pipeline.  gl_init()
     * sets up whatever context is current -- it resets the matrices and re-takes the depth
     * buffer of the virgl context it is handed -- so installing it while an older GL window is
     * current re-initialises that window instead of this one.  That is the same fault the
     * binder exists to stop, one level up.
     *
     * Only where the pipeline about to be installed is the virgl one.  TinyGL and the GS layer
     * keep a single global raster state and never read s_ctx, and asking the virgl backend to
     * make its default current takes a width*height*4 depth buffer with it -- two megabytes
     * that no console heap has to spare, spent on a context nothing will draw with. */
    GL_BACKEND want = gl_backend_selected();
#if defined(BTRON_GL_BACKEND_TINYGL)
    if (want != GL_BACKEND_TINYGL && want != GL_BACKEND_EEGS)
#endif
        virgl_backend_make_current(surf->width, surf->height, surf->pixels);
    gl_init(want, surf->width, surf->height, surf->pixels);
    /* gl_init() answers with the pipeline it could install, not with the one that was asked
     * for, so the layer is claimed only when the GS is actually the rasteriser: an image with
     * no TinyGL linked resolves EEGS to a software backend, and that window's pixmap is the
     * picture and must keep being blitted. */
    if (gl_active_backend() == GL_BACKEND_EEGS) {
        wnd->gl_layer = TRUE;
    }

    /* And make THIS surface current before handing it back, because EGL's own contract is that
     * no GL command is legal until a surface is current, and the apps honour it by uploading
     * right here: xmb.c bakes its icon atlas and font sheet immediately after this call, from
     * outside any paint.  The virgl backend keeps texture objects per context, so an upload
     * made while the default context is current lands in the default context, and every paint
     * -- which now binds this window's own -- finds the slot empty.  An empty slot is not an
     * error to the sampler: it draws the vertex colour instead of the texel, so each icon and
     * each glyph comes out a solid rectangle.  On the TinyGL/GS path there is one context and
     * this is the resize the compositor was about to do anyway. */
    egl_make_current(surf);
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

/* The compositor's binder (see WndGLBind in include/btron/wnd.h): point the GL backend at the
 * context belonging to the window it is about to paint.  The window hands over the surface it
 * was created with, so the mapping is one lookup with no table to keep in sync. */
static void egl_wnd_gl_bind(WND *wnd) {
    if (wnd && wnd->gl_surf) egl_make_current((EGL_SURFACE *)wnd->gl_surf);
}

void egl_surface_resize(EGL_SURFACE *surf, int width, int height) {
    if (!surf || !surf->wnd || !surf->wnd->dev) return;
    surf->width = width;
    surf->height = height;
    surf->pixels = surf->wnd->dev->pixels;
    /* Bind before resizing, for the same reason as above: gl_resize() reaches the backend's
     * current context, and this is a resize of this window's surface.  Resizing while another
     * window happens to be current re-takes that window's raster buffers to this one's size. */
    egl_make_current(surf);
    gl_resize(width, height, surf->pixels);
}

void egl_swap_buffers(EGL_SURFACE *surf) {
    if (!surf) return;
    if (g_gl && g_gl->swap_buffers) {
        g_gl->swap_buffers();
    }
#if BTRON_GL_FPS_OVERLAY
    /* Advance this window's counter and refresh its caption from the measured
     * rate.  Runs after the backend swap; wnd_set_title marks the tab dirty and
     * the inval_wnd below repaints the whole window, so the new caption shows. */
    fps_overlay_tick(surf);
#endif
    if (surf->wnd) {
        inval_wnd(surf->wnd);
    }
}

void egl_destroy_surface(EGL_SURFACE *surf) {
    if (!surf) return;
    /* Detach before freeing: the compositor reads wnd->gl_surf on every paint pass, and a
     * window that outlives its surface must look like a software window, not like a GL
     * window holding a freed context. */
    if (surf->wnd && surf->wnd->gl_surf == surf) surf->wnd->gl_surf = NULL;
    if (gl_tinygl_pipeline()) {
        gl_shutdown();
    } else if (surf->gl_ctx) {
        virgl_backend_destroy_ctx(surf->gl_ctx);
        surf->gl_ctx = NULL;
    }
    free(surf);
}
