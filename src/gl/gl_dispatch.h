/*
 * src/gl/gl_dispatch.h — OpenGL ES 1.1 Dispatch Table
 *
 * A function-pointer table that glgears.c uses through macro shims.
 * At gl_init() time, g_gl is pointed at either g_tinygl_ops (Phase 1)
 * or g_virgl_ops (Phase 2).
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef BTRON_GL_DISPATCH_H
#define BTRON_GL_DISPATCH_H

#include <GL/gl.h>

/* ── Backend selector ────────────────────────────────────────────── */
/* Both of these rasterize in the guest CPU.  The name "virgl" is what src/gl/
 * backend_virgl.c does on the QEMU target, where it also flushes the resource to
 * the host's VirtIO-GPU — those calls are guarded by BTRON_QEMU_TARGET, so on
 * every other image that file is a second software rasterizer rather than a
 * driver, and PCSX2 has no virtio device for it to talk to in the first place. */
typedef enum {
    GL_BACKEND_TINYGL = 0,    /* TinyGL  (src/gl/tinygl/, ZBuffer + zgl)  */
    GL_BACKEND_VIRGL  = 1,    /* backend_virgl.c's own triangle raster    */
    GL_BACKEND_I915   = 2,    /* Awe Morris i915 zero-cost proxy (Ph.3)   */
} GL_BACKEND;

/* ── Dispatch table ──────────────────────────────────────────────── */
typedef struct {
    /* Geometry */
    void   (*gl_begin)        (GLenum mode);
    void   (*gl_end)          (void);
    void   (*gl_vertex3f)     (GLfloat x, GLfloat y, GLfloat z);
    void   (*gl_normal3f)     (GLfloat x, GLfloat y, GLfloat z);
    void   (*gl_color3f)      (GLfloat r, GLfloat g, GLfloat b);
    void   (*gl_color3fv)     (const GLfloat *v);

    /* Matrix */
    void   (*gl_matrix_mode)  (GLenum mode);
    void   (*gl_load_identity)(void);
    void   (*gl_push_matrix)  (void);
    void   (*gl_pop_matrix)   (void);
    void   (*gl_rotate_f)     (GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
    void   (*gl_translate_f)  (GLfloat x, GLfloat y, GLfloat z);
    void   (*gl_frustum)      (double l, double r, double b,
                               double t, double n, double f);
    void   (*gl_viewport)     (GLint x, GLint y, GLsizei w, GLsizei h);

    /* State */
    void   (*gl_clear)        (GLbitfield mask);
    void   (*gl_clear_color)  (GLfloat r, GLfloat g, GLfloat b, GLfloat a);
    void   (*gl_enable)       (GLenum cap);
    void   (*gl_disable)      (GLenum cap);
    void   (*gl_shade_model)  (GLenum mode);

    /* Lighting */
    void   (*gl_light_fv)     (GLenum light, GLenum pname, const GLfloat *params);
    void   (*gl_material_fv)  (GLenum face, GLenum pname, const GLfloat *params);

    /* Display lists */
    GLuint (*gl_gen_lists)    (GLsizei range);
    void   (*gl_new_list)     (GLuint list, GLenum mode);
    void   (*gl_end_list)     (void);
    void   (*gl_call_list)    (GLuint list);

    /* Textures */
    void   (*gl_tex_coord2f)  (GLfloat s, GLfloat t);
    void   (*gl_tex_coord2fv) (const GLfloat *v);
    void   (*gl_gen_textures) (GLsizei n, GLuint *textures);
    void   (*gl_bind_texture) (GLenum target, GLuint texture);
    void   (*gl_tex_image_2d) (GLenum target, GLint level, GLint components,
                               GLsizei width, GLsizei height, GLint border,
                               GLenum format, GLenum type, const void *pixels);
    void   (*gl_tex_parameteri)(GLenum target, GLenum pname, GLint param);

    /* Present */
    void   (*swap_buffers)    (void);

    /* Scale (added at end to preserve ABI offsets of previous members) */
    void   (*gl_scale_f)      (GLfloat x, GLfloat y, GLfloat z);

    /* Alpha compositing, 2D projection and 4-component colour, added at the
     * end for the same reason: the members above keep their offsets. */
    void   (*gl_blend_func)   (GLenum src, GLenum dst);
    void   (*gl_color4f)      (GLfloat r, GLfloat g, GLfloat b, GLfloat a);
    void   (*gl_vertex2f)     (GLfloat x, GLfloat y);
    void   (*gl_ortho)        (double l, double r, double b,
                               double t, double n, double f);
} gl_ops_t;

/* ── Active backend pointer (set by gl_init) ─────────────────────── */
extern gl_ops_t *g_gl;

/* Which backend gl_init() actually installed -- it is not necessarily the one
 * that was asked for, and the surface layer below has to dispatch on the result
 * rather than on the request. */
GL_BACKEND gl_active_backend(void);

/* ── Lifecycle ───────────────────────────────────────────────────── */
void gl_init     (GL_BACKEND backend, int width, int height, void *pixel_buf);
void gl_resize   (int width, int height, void *pixel_buf);
void gl_shutdown (void);

/* ── Macro shims — glgears.c looks like standard GL ─────────────── */
#ifndef BTRON_GL_NO_MACRO_SHIMS
#define glBegin(m)              g_gl->gl_begin(m)
#define glEnd()                 g_gl->gl_end()
#define glVertex3f(x,y,z)       g_gl->gl_vertex3f(x,y,z)
#define glNormal3f(x,y,z)       g_gl->gl_normal3f(x,y,z)
#define glColor3f(r,g,b)        g_gl->gl_color3f(r,g,b)
#define glColor3fv(v)           g_gl->gl_color3fv(v)
#define glMatrixMode(m)         g_gl->gl_matrix_mode(m)
#define glLoadIdentity()        g_gl->gl_load_identity()
#define glPushMatrix()          g_gl->gl_push_matrix()
#define glPopMatrix()           g_gl->gl_pop_matrix()
#define glRotatef(a,x,y,z)      g_gl->gl_rotate_f(a,x,y,z)
#define glTranslatef(x,y,z)     g_gl->gl_translate_f(x,y,z)
#define glScalef(x,y,z)         g_gl->gl_scale_f(x,y,z)
#define glBlendFunc(s,d)        g_gl->gl_blend_func(s,d)
#define glColor4f(r,g,b,a)      g_gl->gl_color4f(r,g,b,a)
#define glVertex2f(x,y)         g_gl->gl_vertex2f(x,y)
#define glOrtho(l,r,b,t,n,f)    g_gl->gl_ortho(l,r,b,t,n,f)
#define glFrustum(l,r,b,t,n,f)  g_gl->gl_frustum(l,r,b,t,n,f)
#define glViewport(x,y,w,h)     g_gl->gl_viewport(x,y,w,h)
#define glClear(m)              g_gl->gl_clear(m)
#define glClearColor(r,g,b,a)   g_gl->gl_clear_color(r,g,b,a)
#define glEnable(c)             g_gl->gl_enable(c)
#define glDisable(c)            g_gl->gl_disable(c)
#define glShadeModel(m)         g_gl->gl_shade_model(m)
#define glLightfv(l,p,v)        g_gl->gl_light_fv(l,p,v)
#define glMaterialfv(f,p,v)     g_gl->gl_material_fv(f,p,v)
#define glGenLists(r)           g_gl->gl_gen_lists(r)
#define glNewList(l,m)          g_gl->gl_new_list(l,m)
#define glEndList()             g_gl->gl_end_list()
#define glCallList(l)           g_gl->gl_call_list(l)
#define glTexCoord2f(s,t)       g_gl->gl_tex_coord2f(s,t)
#define glTexCoord2fv(v)        g_gl->gl_tex_coord2fv(v)
#define glGenTextures(n,t)      g_gl->gl_gen_textures(n,t)
#define glBindTexture(tg,tx)    g_gl->gl_bind_texture(tg,tx)
#define glTexImage2D(tg,l,c,w,h,b,f,tp,px) g_gl->gl_tex_image_2d(tg,l,c,w,h,b,f,tp,px)
#define glTexParameteri(tg,pn,pv) g_gl->gl_tex_parameteri(tg,pn,pv)
#endif /* BTRON_GL_NO_MACRO_SHIMS */

#endif /* BTRON_GL_DISPATCH_H */
