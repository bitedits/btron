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


