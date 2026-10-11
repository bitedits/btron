/*
 * src/gl/gl_dispatch.c — GL Dispatch Table Global + gl_init()
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "gl_dispatch.h"
#include "backend_virgl.h"
#if defined(BTRON_GL_BACKEND_TINYGL)
#include "backend_tinygl.h"
#endif
#include <btron/btron.h>

extern void uart_puts_raw(const char *s);

/* Active backend — set at gl_init() time */
gl_ops_t *g_gl = NULL;
static GL_BACKEND s_active_backend = GL_BACKEND_VIRGL;

GL_BACKEND gl_active_backend(void) {
    return s_active_backend;
}

/* BTRON_GL_BACKEND_TINYGL says this image links src/gl/tinygl/, and only the
 * Makefile lists that put it in a target's sources define it -- so a request for
 * a backend that is not linked has no case to land in and says so on the console
 * instead of becoming the other one silently.  It is needed because the virgl
 * setup lines claim a 3D context unconditionally, whether or not any device
 * answered, so which rasterizer is really live is not inferable from the log. */
void gl_init(GL_BACKEND backend, int width, int height, void *pixel_buf) {
    switch (backend) {

#if defined(BTRON_GL_BACKEND_TINYGL)
    case GL_BACKEND_TINYGL:
        tinygl_backend_init(width, height, pixel_buf);
        g_gl = &g_tinygl_ops;
        s_active_backend = GL_BACKEND_TINYGL;
        break;
#endif

    case GL_BACKEND_VIRGL:
        virgl_backend_init(width, height, pixel_buf);
        g_gl = &g_virgl_ops;
        s_active_backend = GL_BACKEND_VIRGL;
        break;

    default:
        uart_puts_raw("[GL] requested backend not linked in this build, using virgl\n");
        virgl_backend_init(width, height, pixel_buf);
        g_gl = &g_virgl_ops;
        s_active_backend = GL_BACKEND_VIRGL;
        break;
    }
}

void gl_resize(int width, int height, void *pixel_buf) {
    if (!g_gl) return;
#if defined(BTRON_GL_BACKEND_TINYGL)
    if (gl_active_backend() == GL_BACKEND_TINYGL) {
        tinygl_backend_resize(width, height, pixel_buf);
        return;
    }
#endif
    virgl_backend_resize(width, height, pixel_buf);
}

void gl_shutdown(void) {
    if (!g_gl) return;
#if defined(BTRON_GL_BACKEND_TINYGL)
    if (gl_active_backend() == GL_BACKEND_TINYGL) {
        tinygl_backend_shutdown();
        g_gl = NULL;
        return;
    }
#endif
    virgl_backend_shutdown();
    g_gl = NULL;
}

/* The image's one microsecond, handed to the rasterizer as a service of this boundary
 * rather than as a driver include: clip.c brackets a fill with two of these, and where no
 * clock is installed -- every target but the PS2 and the arm64 core -- dp_core.c's weak
 * default answers 0 and the accumulated time reads as the zero it is. */
unsigned gl_perf_us(void)
{
    return (unsigned)btron_render_perf_us();
}

#if defined(BTRON_GL_BACKEND_TINYGL)
/* Defined in src/gl/tinygl/clip.c, at the fill dispatch.  Declared rather than routed
 * through gl_ops_t because it is telemetry, not a GL entry point: the virgl backend's own
 * rasterizer has no per-path fills to report, and an op-table slot it cannot fill would
 * invite a caller to read zeroes as an answer. */
extern void tgl_fill_stats(gl_fill_stats_t *out, int reset);
#endif

void gl_fill_stats(gl_fill_stats_t *out, int reset)
{
    if (!out)
        return;

    /* Before the dispatch, so a backend that cannot answer still leaves the caller a
     * window that started here rather than one that started at boot. */
    out->scanlines = out->fill_us = out->verts = 0u;

#if defined(BTRON_GL_BACKEND_TINYGL)
    if (gl_active_backend() == GL_BACKEND_TINYGL) {
        tgl_fill_stats(out, reset);
        return;
    }
#endif

    out->tris = out->covered = 0u;
    out->textured_tris = out->textured_covered = 0u;
    out->blended_tris = 0u;
    out->reported = 0;
}


