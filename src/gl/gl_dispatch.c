/*
 * src/gl/gl_dispatch.c — GL Dispatch Table Global + gl_init()
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "gl_dispatch.h"
#include "backend_tinygl.h"
#include <btron/btron.h>

extern void uart_puts_raw(const char *s);

/* Active backend — set at gl_init() time */
gl_ops_t *g_gl = NULL;

void gl_init(GL_BACKEND backend, int width, int height, void *pixel_buf) {
    switch (backend) {

    case GL_BACKEND_TINYGL:
    default:
        tinygl_backend_init(width, height, pixel_buf);
        g_gl = &g_tinygl_ops;
        break;

    /* Phase 2: virgl stub */
    case GL_BACKEND_VIRGL:
        uart_puts_raw("[GL] virgl backend not yet implemented\n");
        /* fall back to TinyGL so the system stays usable */
        tinygl_backend_init(width, height, pixel_buf);
        g_gl = &g_tinygl_ops;
        break;
    }
}

void gl_resize(int width, int height, void *pixel_buf) {
    if (!g_gl) return;
    /* Only TinyGL needs explicit ZBuffer resize notification */
    tinygl_backend_resize(width, height, pixel_buf);
}

void gl_shutdown(void) {
    if (!g_gl) return;
    tinygl_backend_shutdown();
    g_gl = NULL;
}
