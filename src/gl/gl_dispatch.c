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
/* The EE/GS case answers with the build's own default, so it needs the rule that says what
 * that is -- which lives in its own TU precisely so this file and the Display applet cannot
 * drift apart. */
#include "gl_backend_choice.h"
#include <btron/btron.h>

extern void uart_puts_raw(const char *s);

/* Active backend — set at gl_init() time */
gl_ops_t *g_gl = NULL;
static GL_BACKEND s_active_backend = GL_BACKEND_VIRGL;

GL_BACKEND gl_active_backend(void) {
    return s_active_backend;
}

void gl_set_active_backend(GL_BACKEND b) {
    s_active_backend = b;
}

/* Which pipeline is installed, asked as one question because two backends share it.  The
 * GS rasterises the geometry but the vertex transform, the state and the record of what to
 * draw are TinyGL's, so an EEGS image dispatches through the same ops table a TINYGL image
 * does -- and every call site that used to compare against GL_BACKEND_TINYGL would otherwise
 * have to grow the same `|| EEGS` clause, six of which is six chances to miss one. */
int gl_tinygl_pipeline(void) {
    return s_active_backend == GL_BACKEND_TINYGL || s_active_backend == GL_BACKEND_EEGS;
}

/* The same answer as the log tag.  It lives here because this file is the one that installs a
 * backend and therefore the one whose answer has to be what a row names: the XMB's frame row and
 * the boot's `[CFG]` row used to each carry their own ternary over the three enum values, which is
 * two encodings of one fact and one of them was already missing a case. */
const char *gl_backend_name(GL_BACKEND b)
{
    switch (b) {
    case GL_BACKEND_EEGS:   return "eegs";
    case GL_BACKEND_TINYGL: return "tinygl";
    case GL_BACKEND_VIRGL:  return "virgl";
    case GL_BACKEND_I915:   return "i915";
    }
    return "unknown";
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

    /* Increment 5 of the plan of record (doc/txt/PS2.txt): the GS answers the fills, but the
     * geometry still goes through TinyGL's pipeline, which is where the GIF recorder's groups
     * come from.  So the ops table is the same one and only the *name* differs -- and the name
     * matters, because egl_surface.c and core_ps2.c dispatch on it to decide whether to blit a
     * software surface into the canvas or let the GS own that rectangle.
     *
     * pixel_buf is still handed to tinygl_backend_init(): glClear is not offloaded yet, so the
     * one path that writes the surface still needs it, and the present layer reads the same
     * buffer for whatever that path leaves there. */
    case GL_BACKEND_EEGS:
        tinygl_backend_init(width, height, pixel_buf);
        g_gl = &g_tinygl_ops;
        s_active_backend = GL_BACKEND_EEGS;
        break;
#endif

#if !defined(BTRON_GL_BACKEND_TINYGL)
    /* An image with no TinyGL has no pipeline for the GS to drive, so the request is answered
     * with the software rule and says so.  It falls back to that rule and not to
     * gl_backend_default(), because on a `PS2_GL=eegs` build the default *is* this case, and
     * the answer would call itself. */
    case GL_BACKEND_EEGS:
        uart_puts_raw("[GL] EE/GS requested but this image links no pipeline to record: "
                      "installing the linked software rasterizer\n");
        gl_init(gl_backend_software_default(), width, height, pixel_buf);
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
    if (gl_tinygl_pipeline()) {
        tinygl_backend_resize(width, height, pixel_buf);
        return;
    }
#endif
    virgl_backend_resize(width, height, pixel_buf);
}

void gl_shutdown(void) {
    if (!g_gl) return;
#if defined(BTRON_GL_BACKEND_TINYGL)
    if (gl_tinygl_pipeline()) {
        /* The pipeline answers whether it is still usable, because several window surfaces now
         * share one context: closing a window that is not the last closes nothing above it.
         * Clearing `g_gl` anyway was a crash waiting for the survivor -- every GL entry point in
         * include/gl.h is a `g_gl->…` macro, so the next paint after a benign window close follows
         * a NULL pointer.  The ops table is the context's, so it lives and dies with the context. */
        if (tinygl_backend_shutdown())
            return;
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
    if (gl_tinygl_pipeline()) {
        tgl_fill_stats(out, reset);
        return;
    }
#endif

    out->tris = out->covered = 0u;
    out->textured_tris = out->textured_covered = 0u;
    out->blended_tris = 0u;
    out->reported = 0;
}


