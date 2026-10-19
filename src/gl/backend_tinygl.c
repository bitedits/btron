/*
 * src/gl/backend_tinygl.c — TinyGL Software Rasterizer Backend
 *
 * Wraps the C-Chads TinyGL fork (third_party/tinygl/) behind the
 * B-System gl_ops_t dispatch table.  ZBuffer pixel output is stored
 * directly into the caller-supplied pixel_buf, already carrying its
 * opaque alpha byte, so there is no swap: the window's GDEV is the
 * frame the compositor reads.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <btron/types.h>
#include <btron/dp.h>          /* COLOR typedef */
#include <btron/btron.h>       /* uart_puts_raw */
#include <btron/wnd.h>
#include <stdlib.h>
#include <string.h>

extern void uart_puts_raw(const char *s);

#define BTRON_GL_NO_MACRO_SHIMS 1
#include "backend_tinygl.h"

/* TinyGL internal headers */
#include "tinygl/zbuffer.h"
#include "tinygl/zgl.h"

/* TinyGL's own entry points that <GL/gl.h> leaves undeclared. */
extern void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
extern void glVertex2f(GLfloat x, GLfloat y);

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

/* How many window surfaces have been handed the one pipeline.  Counted because the fact is the
 * bug this file used to have: `egl_create_window_surface()` calls gl_init() for EVERY GL window,
 * and gl_init() used to reach glInit() every time -- see tinygl_backend_init().  A second GL
 * window is normal (XMB plus an app), so the count is the row that says whether a texture name
 * still means what it meant when it was minted. */
static int      s_surfaces  = 0;

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
static void   tgl_blend_func   (GLenum src, GLenum dst);
static void   tgl_color4f      (GLfloat r, GLfloat g, GLfloat b, GLfloat a);
static void   tgl_vertex2f     (GLfloat x, GLfloat y);
static void   tgl_ortho        (double l, double r, double b,
                                double t, double n, double f);

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
    .gl_scale_f      = glScalef,
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
    /* swap_buffers is left NULL: TinyGL rasterises into the window's own pixel array,
     * so a frame is already where it has to be.  egl_swap_buffers() handles the NULL. */
    .gl_blend_func   = tgl_blend_func,
    .gl_color4f      = tgl_color4f,
    .gl_vertex2f     = tgl_vertex2f,
    .gl_ortho        = tgl_ortho,
};

/* ── Init / resize / shutdown ────────────────────────────────────── */

/*
 * Once per surface, not once per frame: give the whole pixel array its opaque alpha byte.
 *
 * The rasteriser now writes that byte with every pixel it stores (FB_PIXEL, and the law in
 * src/gl/tinygl/zbuffer.h), so what is left here is the area no primitive happens to cover.
 * A window's pixel array arrives zeroed, and a zeroed top byte is a transparent pixel to
 * anything that reads the canvas the way the GS does, so an unstamped surface shows its
 * uncovered area as black rather than as the clear colour it was cleared to.
 *
 * It is called when the array is new -- at init, and at a resize that actually changed the
 * array or its size -- because egl_make_current() routes through the resize path every time
 * a context is made current, and a walk of the surface there would be the per-frame pass
 * this replaced.
 */
static void s_stamp_surface_alpha(void) {
    if (!s_pixel_buf) return;
    COLOR *px = (COLOR *)s_pixel_buf;
    int    n  = s_width * s_height;
    for (int i = 0; i < n; i++) {
        px[i] |= 0xFF000000u;
    }
}

void tinygl_backend_init(int w, int h, void *pixel_buf) {
    /* ONE CONTEXT PER PROCESS, and this is the line that enforces it.
     *
     * egl_surface.c calls gl_init() from egl_create_window_surface(), so this ran once for every
     * GL window the desktop opened.  glInit() begins with `gl_ctx = empty_gl_ctx` (tinygl/init.c)
     * -- it overwrites the single global context with a static template and then allocates a fresh
     * shared_state, so the previous window's texture nodes, display lists and buffers are neither
     * deleted nor inherited: they are orphaned, and every name that window still holds now looks
     * up an empty hash table.  An empty slot is not an error to the sampler; it draws the vertex
     * colour instead of the texel, so each icon and each glyph becomes one flat rectangle.  That
     * is the reported defect, measured: an icon row whose cells are solid 0x0000ff where the
     * focused item's vertex colour is the selection blue, and solid black where it is the shadow,
     * with the art underneath intact -- a lost texture, not a lost quad.
     *
     * It also explains the two things the operator reported about timing, which no present-path
     * or VRAM story could:
     *   "running lilcu, then xmb, made the glitch disappear" -- xmb opened LAST, so its atlas was
     *   minted after the final glInit() and was the only texture namespace still alive;
     *   "icons show initially fine but get corrupted after the FPS ticker changed" -- the first
     *   caption tick is a repaint of a window whose surface had not been created yet, and creating
     *   it is what erased the launcher's.
     *
     * So a pipeline that is already installed is re-pointed at the new surface, never rebuilt.
     * The per-surface parts of the old call -- the ZBuffer's pixel array and its extent -- are
     * exactly what tinygl_backend_resize() re-takes, including the alpha stamp for the area no
     * primitive covers.  What survives is the shared namespace: glGenTextures() hands out
     * max(handle)+1 over the live hash table, so with no wipe two windows can never be given the
     * same name, and the only way a name is reused is by the highest-numbered texture dying.
     *
     * A real backend SWITCH still rebuilds, because it does not come through here with a live
     * s_zb: gl_shutdown() -> tinygl_backend_shutdown() closes the ZBuffer and NULLs it first. */
    if (s_zb) {
        s_surfaces++;
        uart_puts_raw("[GL] TinyGL: second window surface joins the one context -- textures and "
                      "lists kept, raster re-pointed\n");
        if (gl_current_context == &gl_ctx) {
            tinygl_backend_resize(w, h, pixel_buf);
        }
        return;
    }

    s_surfaces  = 1;
    s_width     = w;
    s_height    = h;
    s_pixel_buf = pixel_buf;

    /* Open TinyGL ZBuffer in RGBA 32-bit mode, using pixel_buf directly */
    s_zb = ZB_open(w, h, ZB_MODE_RGBA, pixel_buf);
    if (!s_zb) {
        s_surfaces = 0;
        uart_puts_raw("[TinyGL] ERROR: ZB_open failed (OOM?)\n");
        return;
    }

    s_stamp_surface_alpha();

    /* Hand ZBuffer to TinyGL context */
    glInit(s_zb);

    uart_puts_raw("[GL] TinyGL ZBuffer init\n");
}

void tinygl_backend_resize(int w, int h, void *pixel_buf) {
    if (!s_zb) return;
    const int fresh = (pixel_buf != s_pixel_buf) || (w != s_width) || (h != s_height);
    if (!fresh) return;
    s_width     = w;
    s_height    = h;
    s_pixel_buf = pixel_buf;
    ZB_resize(s_zb, pixel_buf, w, h);
    s_stamp_surface_alpha();

    /* Re-take the viewport with the surface, because the two used to be changed by one call.
     *
     * Before the shared-context law, every new window surface ran glInit(), and glInit() ends by
     * copying the ZBuffer's extent into the viewport and evaluating it (src/gl/tinygl/init.c).  The
     * re-pointing path that replaced it takes only the raster's pixel array, so a second, smaller
     * window inherited the first one's transform: its geometry was scaled to the other window's
     * width and height and clipped against them.  That is the same class of bug this file just
     * fixed, on the other half of the state -- one process-wide context means each per-surface
     * fact has to be re-asserted at the switch, and the list is exactly two: where the pixels go,
     * and what maps onto them.
     *
     * An app that sets its own viewport every frame -- xb_paint() does -- never noticed.  The one
     * that does not, or that uploads a texture or draws before its first paint, does. */
    glViewport(0, 0, w, h);
}

int tinygl_backend_shutdown(void) {
    if (!s_zb) return 0;

    /* The other half of the same law as tinygl_backend_init(): a context shared by several window
     * surfaces is closed when the LAST of them goes, not the first.
     *
     * egl_destroy_surface() calls gl_shutdown() on this pipeline for whatever window it is
     * destroying, and glClose() runs endSharedState() -- which deletes every texture node and
     * every display list in the process and frees the planes behind them.  So before this guard,
     * closing any GL window silently destroyed every other GL window's textures, and each of the
     * survivors' next paint drew the vertex colour instead of the texel: one flat rectangle per
     * icon and per glyph.  A transient surface -- a menu, an overlay, a helper window -- opening
     * and closing is exactly the "later event" the operator dated the corruption to.
     *
     * What the surviving surfaces still need is the namespace, so the count goes down and the
     * context stays.  The raster is detached from the dying window's pixmap here rather than left
     * pointing at freed bytes: s_pixel_buf going NULL makes the next bind's `fresh` test answer
     * yes whatever the survivor's geometry is, so tinygl_backend_resize() re-takes pbuf before
     * anything can draw.  ZBuffer's own frame_buffer_allocated stays 0, so no replacement pixmap
     * is malloc'd for a window that has none.
     *
     * The return value answers the question the caller cannot ask any other way -- is there still
     * a context here? -- and gl_dispatch.c keeps its ops table with the answer, because every GL
     * entry point an app calls is a `g_gl->…` macro. */
    if (s_surfaces > 1) {
        s_surfaces--;
        s_zb->pbuf  = NULL;
        s_pixel_buf = NULL;
        s_width     = 0;
        s_height    = 0;
        uart_puts_raw("[GL] TinyGL: one window surface closed with others still live -- context "
                      "kept, raster detached until the next bind\n");
        return 1;
    }

    s_surfaces  = 0;
    glClose();
    ZB_close(s_zb);
    s_zb        = NULL;
    s_pixel_buf = NULL;
    s_width     = 0;
    s_height    = 0;
    return 0;
}

/* ── Per-surface context isolation (matching VirGL / POSIX architecture) ── */

typedef struct tinygl_context_s {
    GLContext  gl;
    ZBuffer   *zb;
    int        w;
    int        h;
    void      *pixel_buf;
} tinygl_context_t;

void tinygl_backend_make_current_ctx(void **p_ctx, int w, int h, void *pixel_buf) {
    if (!p_ctx) {
        tinygl_backend_resize(w, h, pixel_buf);
        return;
    }

    tinygl_context_t *ctx = (tinygl_context_t *)(*p_ctx);
    if (!ctx) {
        ctx = (tinygl_context_t *)calloc(1, sizeof(tinygl_context_t));
        if (!ctx) {
            tinygl_backend_resize(w, h, pixel_buf);
            return;
        }
        ctx->w = w;
        ctx->h = h;
        ctx->pixel_buf = pixel_buf;
        ctx->zb = ZB_open(w, h, ZB_MODE_RGBA, pixel_buf);
        if (!ctx->zb) {
            free(ctx);
            tinygl_backend_resize(w, h, pixel_buf);
            return;
        }
        if (pixel_buf) {
            COLOR *px = (COLOR *)pixel_buf;
            int n = w * h;
            for (int i = 0; i < n; i++) px[i] |= 0xFF000000u;
        }
        gl_init_context(&ctx->gl, ctx->zb);

        /* Share display lists and texture hash table across all contexts (OpenGL spec / LANE 24) */
        if (gl_ctx.shared_state.lists) {
            if (ctx->gl.shared_state.lists && ctx->gl.shared_state.lists != gl_ctx.shared_state.lists) {
                gl_free(ctx->gl.shared_state.lists);
            }
            if (ctx->gl.shared_state.texture_hash_table && ctx->gl.shared_state.texture_hash_table != gl_ctx.shared_state.texture_hash_table) {
                gl_free(ctx->gl.shared_state.texture_hash_table);
            }
            if (ctx->gl.shared_state.buffers && ctx->gl.shared_state.buffers != gl_ctx.shared_state.buffers) {
                gl_free(ctx->gl.shared_state.buffers);
            }
            ctx->gl.shared_state = gl_ctx.shared_state;
            ctx->gl.current_texture = gl_ctx.current_texture;
        }

        *p_ctx = ctx;
    }

    /* Switch active context and raster buffer */
    gl_current_context = &ctx->gl;
    s_zb = ctx->zb;
    s_pixel_buf = pixel_buf;
    s_width = w;
    s_height = h;

    if (!ctx->zb->pbuf || ctx->zb->pbuf != pixel_buf || ctx->w != w || ctx->h != h || ctx->pixel_buf != pixel_buf) {
        ctx->w = w;
        ctx->h = h;
        ctx->pixel_buf = pixel_buf;
        ZB_resize(ctx->zb, pixel_buf, w, h);
        if (pixel_buf) {
            COLOR *px = (COLOR *)pixel_buf;
            int n = w * h;
            for (int i = 0; i < n; i++) px[i] |= 0xFF000000u;
        }
        glViewport(0, 0, w, h);
    }
    ctx->zb->pbuf = pixel_buf;
    ctx->gl.zb = ctx->zb;
}

void tinygl_backend_destroy_ctx(void *ctx_ptr) {
    if (!ctx_ptr) return;
    tinygl_context_t *ctx = (tinygl_context_t *)ctx_ptr;
    if (gl_current_context == &ctx->gl) {
        gl_current_context = &gl_ctx;
        s_zb = gl_ctx.zb;
    }
    /* If sharing shared_state with gl_ctx, do not free the shared tables */
    if (ctx->gl.shared_state.lists == gl_ctx.shared_state.lists) {
        ctx->gl.shared_state.lists = NULL;
        ctx->gl.shared_state.texture_hash_table = NULL;
        ctx->gl.shared_state.buffers = NULL;
    }
    gl_close_context(&ctx->gl);
    if (ctx->zb) {
        ZB_close(ctx->zb);
        ctx->zb = NULL;
    }
    free(ctx);
    if (s_surfaces > 1) {
        s_surfaces--;
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
 * Alpha compositing is TinyGL's own: glEnable(GL_BLEND) plus glBlendFunc set the
 * ZBuffer's source/destination factors, so the wrappers stay thin.
 */
static void tgl_blend_func(GLenum src, GLenum dst)              { glBlendFunc((GLint)src, (GLint)dst); }
static void tgl_color4f(GLfloat r, GLfloat g,
                        GLfloat b, GLfloat a)                   { glColor4f(r, g, b, a); }
static void tgl_vertex2f(GLfloat x, GLfloat y)                  { glVertex2f(x, y); }

/* TinyGL has no glOrtho; the projection is an ordinary matrix multiply, so the
 * row-major mat4 layout <GL/gl.h> apps expect is transcribed here. */
static void tgl_ortho(double l, double r, double b,
                      double t, double n, double f) {
    GLfloat m[16];
    double rl = r - l, tb = t - b, fn = f - n;
    if (rl == 0.0 || tb == 0.0 || fn == 0.0) return;
    memset(m, 0, sizeof(m));
    m[0]  = (GLfloat)(2.0 / rl);
    m[5]  = (GLfloat)(2.0 / tb);
    m[10] = (GLfloat)(-2.0 / fn);
    m[12] = (GLfloat)(-(r + l) / rl);
    m[13] = (GLfloat)(-(t + b) / tb);
    m[14] = (GLfloat)(-(f + n) / fn);
    m[15] = 1.0f;
    glMultMatrixf(m);
}
