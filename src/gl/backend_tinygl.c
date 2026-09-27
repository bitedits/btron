/*
 * src/gl/backend_tinygl.c — TinyGL Software Rasterizer Backend
 *
 * Wraps the C-Chads TinyGL fork (third_party/tinygl/) behind the
 * B-System gl_ops_t dispatch table.  ZBuffer pixel output is stored
 * directly into the caller-supplied pixel_buf (XRGB8888), which is
 * the same buffer backing the window's GDEV — zero extra copy on
 * swap_buffers().
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <btron/types.h>
#include <btron/dp.h>          /* COLOR typedef */
#include <btron/btron.h>       /* uart_puts_raw */
#include <btron/wnd.h>

extern void uart_puts_raw(const char *s);

#define BTRON_GL_NO_MACRO_SHIMS 1
#include "backend_tinygl.h"

/* TinyGL internal headers */
#include "tinygl/zbuffer.h"
#include "tinygl/zgl.h"

#ifdef W
#undef W
#endif
#ifdef X
#undef X
#endif
#ifdef Y
#undef Y
#endif
#ifdef Z
#undef Z
#endif

/* ── TinyGL state ────────────────────────────────────────────────── */
static ZBuffer *s_zb        = NULL;
static void    *s_pixel_buf = NULL;
static int      s_width     = 0;
static int      s_height    = 0;

/* ── Forward declarations ─────────────────────────────────────────── */
static void   tgl_begin       (GLenum mode);
static void   tgl_end         (void);
static void   tgl_vertex3f    (GLfloat x, GLfloat y, GLfloat z);
static void   tgl_normal3f    (GLfloat x, GLfloat y, GLfloat z);
static void   tgl_color3f     (GLfloat r, GLfloat g, GLfloat b);
static void   tgl_color3fv    (const GLfloat *v);
static void   tgl_matrix_mode (GLenum mode);
static void   tgl_load_identity(void);
static void   tgl_push_matrix (void);
static void   tgl_pop_matrix  (void);
static void   tgl_rotate_f    (GLfloat a, GLfloat x, GLfloat y, GLfloat z);
static void   tgl_translate_f (GLfloat x, GLfloat y, GLfloat z);
static void   tgl_frustum     (double l, double r, double b,
                               double t, double n, double f);
static void   tgl_viewport    (GLint x, GLint y, GLsizei w, GLsizei h);
static void   tgl_clear       (GLbitfield mask);
static void   tgl_clear_color (GLfloat r, GLfloat g, GLfloat b, GLfloat a);
static void   tgl_enable      (GLenum cap);
static void   tgl_disable     (GLenum cap);
static void   tgl_shade_model (GLenum mode);
static void   tgl_light_fv    (GLenum light, GLenum pname, const GLfloat *p);
static void   tgl_material_fv (GLenum face,  GLenum pname, const GLfloat *p);
static GLuint tgl_gen_lists   (GLsizei range);
static void   tgl_new_list    (GLuint list, GLenum mode);
static void   tgl_end_list    (void);
static void   tgl_call_list   (GLuint list);
static void   tgl_tex_coord2f (GLfloat s, GLfloat t);
static void   tgl_tex_coord2fv(const GLfloat *v);
static void   tgl_gen_textures(GLsizei n, GLuint *textures);
static void   tgl_bind_texture(GLenum target, GLuint texture);
static void   tgl_tex_image_2d(GLenum target, GLint level, GLint components,
                               GLsizei width, GLsizei height, GLint border,
                               GLenum format, GLenum type, const void *pixels);
static void   tgl_tex_parameteri(GLenum target, GLenum pname, GLint param);
static void   tgl_swap_buffers(void);

/* ── Dispatch table ──────────────────────────────────────────────── */
gl_ops_t g_tinygl_ops = {
    .gl_begin        = tgl_begin,
    .gl_end          = tgl_end,
    .gl_vertex3f     = tgl_vertex3f,
    .gl_normal3f     = tgl_normal3f,
    .gl_color3f      = tgl_color3f,
    .gl_color3fv     = tgl_color3fv,
    .gl_matrix_mode  = tgl_matrix_mode,
    .gl_load_identity= tgl_load_identity,
    .gl_push_matrix  = tgl_push_matrix,
    .gl_pop_matrix   = tgl_pop_matrix,
    .gl_rotate_f     = tgl_rotate_f,
    .gl_translate_f  = tgl_translate_f,
    .gl_frustum      = tgl_frustum,
    .gl_viewport     = tgl_viewport,
    .gl_clear        = tgl_clear,
    .gl_clear_color  = tgl_clear_color,
    .gl_enable       = tgl_enable,
    .gl_disable      = tgl_disable,
    .gl_shade_model  = tgl_shade_model,
    .gl_light_fv     = tgl_light_fv,
    .gl_material_fv  = tgl_material_fv,
    .gl_gen_lists    = tgl_gen_lists,
    .gl_new_list     = tgl_new_list,
    .gl_end_list     = tgl_end_list,
    .gl_call_list    = tgl_call_list,
    .gl_tex_coord2f  = tgl_tex_coord2f,
    .gl_tex_coord2fv = tgl_tex_coord2fv,
    .gl_gen_textures = tgl_gen_textures,
    .gl_bind_texture = tgl_bind_texture,
    .gl_tex_image_2d = tgl_tex_image_2d,
    .gl_tex_parameteri= tgl_tex_parameteri,
    .swap_buffers    = tgl_swap_buffers,
};

/* ── Init / resize / shutdown ────────────────────────────────────── */

void tinygl_backend_init(int w, int h, void *pixel_buf) {
    s_width     = w;
    s_height    = h;
    s_pixel_buf = pixel_buf;

    /* Open TinyGL ZBuffer in RGBA 32-bit mode, using pixel_buf directly */
    s_zb = ZB_open(w, h, ZB_MODE_RGBA, pixel_buf);
    if (!s_zb) {
        uart_puts_raw("[TinyGL] ERROR: ZB_open failed (OOM?)\n");
        return;
    }

    /* Hand ZBuffer to TinyGL context */
    glInit(s_zb);

    uart_puts_raw("[GL] TinyGL ZBuffer init\n");
}

void tinygl_backend_resize(int w, int h, void *pixel_buf) {
    if (!s_zb) return;
    s_width     = w;
    s_height    = h;
    s_pixel_buf = pixel_buf;
    ZB_resize(s_zb, pixel_buf, w, h);
}

void tinygl_backend_shutdown(void) {
    if (s_zb) {
        glClose();
        ZB_close(s_zb);
        s_zb = NULL;
    }
}

/* ── TinyGL GL wrappers ───────────────────────────────────────────── */

static void tgl_begin(GLenum mode)                        { glBegin((GLint)mode); }
static void tgl_end(void)                                 { glEnd(); }
static void tgl_vertex3f(GLfloat x, GLfloat y, GLfloat z) { glVertex3f(x, y, z); }
static void tgl_normal3f(GLfloat x, GLfloat y, GLfloat z) { glNormal3f(x, y, z); }
static void tgl_color3f(GLfloat r, GLfloat g, GLfloat b)  { glColor3f(r, g, b); }
static void tgl_color3fv(const GLfloat *v)                { glColor3fv((GLfloat *)v); }
static void tgl_matrix_mode(GLenum mode)                  { glMatrixMode((GLint)mode); }
static void tgl_load_identity(void)                       { glLoadIdentity(); }
static void tgl_push_matrix(void)                         { glPushMatrix(); }
static void tgl_pop_matrix(void)                          { glPopMatrix(); }
static void tgl_rotate_f(GLfloat a, GLfloat x,
                          GLfloat y, GLfloat z)           { glRotatef(a, x, y, z); }
static void tgl_translate_f(GLfloat x, GLfloat y, GLfloat z){ glTranslatef(x, y, z); }
static void tgl_frustum(double l, double r, double b,
                         double t, double n, double f)    { glFrustum(l, r, b, t, n, f); }
static void tgl_viewport(GLint x, GLint y, GLsizei w, GLsizei h) { glViewport(x, y, w, h); }
static void tgl_clear(GLbitfield mask)                    { glClear((GLint)mask); }
static void tgl_clear_color(GLfloat r, GLfloat g,
                             GLfloat b, GLfloat a)        { glClearColor(r, g, b, a); }
static void tgl_enable(GLenum cap)                        { glEnable((GLint)cap); }
static void tgl_disable(GLenum cap)                       { glDisable((GLint)cap); }
static void tgl_shade_model(GLenum mode)                  { glShadeModel((GLint)mode); }
static void tgl_light_fv(GLenum l, GLenum p,
                          const GLfloat *v)               { glLightfv((GLint)l, (GLint)p, (GLfloat *)v); }
static void tgl_material_fv(GLenum f, GLenum p,
                             const GLfloat *v)            { glMaterialfv((GLint)f, (GLint)p, (GLfloat *)v); }
static GLuint tgl_gen_lists(GLsizei range)                { return (GLuint)glGenLists((GLint)range); }
static void tgl_new_list(GLuint list, GLenum mode)        { glNewList((GLint)list, (GLint)mode); }
static void tgl_end_list(void)                            { glEndList(); }
static void tgl_call_list(GLuint list)                    { glCallList((GLint)list); }
static void tgl_tex_coord2f(GLfloat s, GLfloat t)         { glTexCoord2f(s, t); }
static void tgl_tex_coord2fv(const GLfloat *v)            { glTexCoord2fv((GLfloat *)v); }
static void tgl_gen_textures(GLsizei n, GLuint *textures) { glGenTextures(n, (GLuint *)textures); }
static void tgl_bind_texture(GLenum target, GLuint texture) { glBindTexture((GLint)target, (GLint)texture); }
static void tgl_tex_image_2d(GLenum target, GLint level, GLint components,
                             GLsizei width, GLsizei height, GLint border,
                             GLenum format, GLenum type, const void *pixels) {
    glTexImage2D((GLint)target, level, components, width, height, border, (GLint)format, (GLint)type, (void *)pixels);
}
static void tgl_tex_parameteri(GLenum target, GLenum pname, GLint param) {
    glTexParameteri((GLint)target, (GLint)pname, param);
}

/*
 * swap_buffers: TinyGL rendered directly into s_pixel_buf (ZB_MODE_RGBA).
 * Pixel format from TinyGL 32-bit: 0x00RRGGBB (alpha=0).
 * BTRON COLOR is 0xAARRGGBB.  OR in 0xFF000000 to set full opacity.
 */
static void tgl_swap_buffers(void) {
    if (!s_pixel_buf) return;
    COLOR *px = (COLOR *)s_pixel_buf;
    int   n   = s_width * s_height;
    for (int i = 0; i < n; i++) {
        px[i] |= 0xFF000000u;
    }
}
