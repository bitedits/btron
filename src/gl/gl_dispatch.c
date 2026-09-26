/*
 * src/gl/gl_dispatch.c — GL Dispatch Table Global + gl_init()
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "gl_dispatch.h"
#include "backend_virgl.h"
#if defined(BTRON_UEFI_TARGET)
#include "backend_tinygl.h"
#endif
#include <btron/btron.h>

extern void uart_puts_raw(const char *s);

/* Active backend — set at gl_init() time */
gl_ops_t *g_gl = NULL;
static GL_BACKEND s_active_backend = GL_BACKEND_VIRGL;

void gl_init(GL_BACKEND backend, int width, int height, void *pixel_buf) {
    s_active_backend = backend;
    switch (backend) {

#if defined(BTRON_UEFI_TARGET)
    case GL_BACKEND_TINYGL:
        tinygl_backend_init(width, height, pixel_buf);
        g_gl = &g_tinygl_ops;
        break;
#endif

    case GL_BACKEND_VIRGL:
    default:
        virgl_backend_init(width, height, pixel_buf);
        g_gl = &g_virgl_ops;
        break;
    }
}

void gl_resize(int width, int height, void *pixel_buf) {
    if (!g_gl) return;
#if defined(BTRON_UEFI_TARGET)
    if (s_active_backend == GL_BACKEND_TINYGL) {
        tinygl_backend_resize(width, height, pixel_buf);
        return;
    }
#endif
    virgl_backend_resize(width, height, pixel_buf);
}

void gl_shutdown(void) {
    if (!g_gl) return;
#if defined(BTRON_UEFI_TARGET)
    if (s_active_backend == GL_BACKEND_TINYGL) {
        tinygl_backend_shutdown();
        g_gl = NULL;
        return;
    }
#endif
    virgl_backend_shutdown();
    g_gl = NULL;
}


