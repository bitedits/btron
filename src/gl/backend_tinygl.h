/*
 * src/gl/backend_tinygl.h — TinyGL Software Rasterizer Backend
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef BTRON_GL_BACKEND_TINYGL_H
#define BTRON_GL_BACKEND_TINYGL_H

#include "gl_dispatch.h"

/* Global TinyGL ops table — set as g_gl by gl_init(GL_BACKEND_TINYGL) */
extern gl_ops_t g_tinygl_ops;

/* Called by gl_init() / gl_resize() to (re-)create the ZBuffer */
void tinygl_backend_init  (int w, int h, void *pixel_buf);
void tinygl_backend_resize(int w, int h, void *pixel_buf);

/* Answer with whether the pipeline is still usable afterwards, because that is now a real
 * question: several window surfaces share one context, so closing one of them is not closing
 * the rasterizer.  gl_shutdown() dispatches on the answer -- a context that outlives the surface
 * has to keep its ops table with it, or the survivor's very next GL call follows a NULL `g_gl`.
 * 1 = surfaces still live, 0 = the last one went (or there was nothing to close). */
int  tinygl_backend_shutdown(void);

#endif /* BTRON_GL_BACKEND_TINYGL_H */
