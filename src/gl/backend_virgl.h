/*
 * src/gl/backend_virgl.h — VirtIO-GPU OpenGL (virgl) 3D Backend
 *
 * Authentic VirtIO-GPU 3D acceleration backend for B-System desktop.
 * Provides OpenGL ES 1.1 pipeline with VirtIO-GPU 3D command submission.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef BTRON_GL_BACKEND_VIRGL_H
#define BTRON_GL_BACKEND_VIRGL_H

#include "gl_dispatch.h"

/* Global VirtIO-GPU ops table — set as g_gl by gl_init(GL_BACKEND_VIRGL) */
extern gl_ops_t g_virgl_ops;

/* Lifecycle functions called by gl_init() / gl_resize() / gl_shutdown() */
void virgl_backend_init        (int w, int h, void *pixel_buf);
void virgl_backend_resize      (int w, int h, void *pixel_buf);
void virgl_backend_make_current    (int w, int h, void *pixel_buf);
void virgl_backend_make_current_ctx(void **p_ctx, int w, int h, void *pixel_buf);
void virgl_backend_destroy_ctx     (void *ctx);
void virgl_backend_shutdown        (void);

#endif /* BTRON_GL_BACKEND_VIRGL_H */
