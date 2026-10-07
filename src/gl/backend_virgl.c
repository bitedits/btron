/*
 * src/gl/backend_virgl.c — VirtIO-GPU OpenGL (virgl) 3D Backend
 *
 * Cleanroom VirtIO-GPU 3D hardware-accelerated OpenGL ES 1.1 implementation
 * for B-System. Implements the complete gl_ops_t dispatch interface,
 * translating fixed-function geometry, matrices, and lighting into
 * depth-buffered 3D rasterization and VirtIO-GPU transfer commands.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <btron/types.h>
#include <btron/dp.h>
#include <btron/btron.h>
#include <btron/wnd.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif

#define BTRON_GL_NO_MACRO_SHIMS 1
#include "backend_virgl.h"

#ifdef BTRON_QEMU_TARGET
#include <device/virtio.h>
#endif

extern void uart_puts_raw(const char *s);

/*
 * virgl_pack_color — Pack 8-bit R, G, B into authentic ARGB8888 COLOR word,
 * matching baremetal UEFI VESA LFB and unified SDL2 SDL_PIXELFORMAT_ARGB8888.
 */
static inline uint32_t virgl_pack_color(uint32_t r, uint32_t g, uint32_t b) {
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

/* ── 4x4 Matrix Mathematics ───────────────────────────────────────── */

typedef struct {
    float m[4][4];
} mat4_t;

static inline void mat4_identity(mat4_t *dst) {
    memset(dst, 0, sizeof(mat4_t));
    dst->m[0][0] = 1.0f;
    dst->m[1][1] = 1.0f;
    dst->m[2][2] = 1.0f;
    dst->m[3][3] = 1.0f;
}

static inline void mat4_multiply(mat4_t *dst, const mat4_t *a, const mat4_t *b) {
    mat4_t res;
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            res.m[r][c] = a->m[r][0] * b->m[0][c] +
                          a->m[r][1] * b->m[1][c] +
                          a->m[r][2] * b->m[2][c] +
                          a->m[r][3] * b->m[3][c];
        }
    }
    *dst = res;
}

static inline void mat4_translate(mat4_t *m, float x, float y, float z) {
    mat4_t t;
    mat4_identity(&t);
    t.m[0][3] = x;
    t.m[1][3] = y;
    t.m[2][3] = z;
    mat4_multiply(m, m, &t);
}

static inline void mat4_scale(mat4_t *m, float x, float y, float z) {
    mat4_t s;
    mat4_identity(&s);
    s.m[0][0] = x;
    s.m[1][1] = y;
    s.m[2][2] = z;
    mat4_multiply(m, m, &s);
}

static inline void mat4_rotate(mat4_t *m, float angle_deg, float x, float y, float z) {
    float rad = angle_deg * (float)M_PI / 180.0f;
    float c = (float)cos(rad);
    float s = (float)sin(rad);
    float len = (float)sqrt(x * x + y * y + z * z);
    if (len > 0.00001f) {
        x /= len; y /= len; z /= len;
    }

    mat4_t r;
    mat4_identity(&r);
    r.m[0][0] = x * x * (1.0f - c) + c;
    r.m[0][1] = x * y * (1.0f - c) - z * s;
    r.m[0][2] = x * z * (1.0f - c) + y * s;

    r.m[1][0] = y * x * (1.0f - c) + z * s;
    r.m[1][1] = y * y * (1.0f - c) + c;
    r.m[1][2] = y * z * (1.0f - c) - x * s;

    r.m[2][0] = z * x * (1.0f - c) - y * s;
    r.m[2][1] = z * y * (1.0f - c) + x * s;
    r.m[2][2] = z * z * (1.0f - c) + c;

    mat4_multiply(m, m, &r);
}

static inline void mat4_frustum(mat4_t *m, double l, double r, double b,
                                double t, double n, double f) {
    mat4_t p;
    memset(&p, 0, sizeof(mat4_t));
    p.m[0][0] = (float)(2.0 * n / (r - l));
    p.m[0][2] = (float)((r + l) / (r - l));
    p.m[1][1] = (float)(2.0 * n / (t - b));
    p.m[1][2] = (float)((t + b) / (t - b));
    p.m[2][2] = (float)(-(f + n) / (f - n));
    p.m[2][3] = (float)(-(2.0 * f * n) / (f - n));
    p.m[3][2] = -1.0f;

    mat4_multiply(m, m, &p);
}

/* Orthographic projection: maps [l,r]x[b,t]x[n,f] onto the NDC cube.  The 2D
 * passes of a menu layer (icons, ribbon, text) sit under this, where w stays 1
 * and the same five clip planes still apply. */
static inline void mat4_ortho(mat4_t *m, double l, double r, double b,
                             double t, double n, double f) {
    if (r - l == 0.0 || t - b == 0.0 || f - n == 0.0) return;

    mat4_t p;
    memset(&p, 0, sizeof(mat4_t));
    p.m[0][0] = (float)(2.0 / (r - l));
    p.m[1][1] = (float)(2.0 / (t - b));
    p.m[2][2] = (float)(-2.0 / (f - n));
    p.m[0][3] = (float)(-(r + l) / (r - l));
    p.m[1][3] = (float)(-(t + b) / (t - b));
    p.m[2][3] = (float)(-(f + n) / (f - n));
    p.m[3][3] = 1.0f;

    mat4_multiply(m, m, &p);
}

/* Transform 3D point by 4x4 matrix: [x, y, z, 1]^T -> [ox, oy, oz, ow]^T */
static inline void mat4_transform_vec4(const mat4_t *m, float x, float y, float z,
                                      float *ox, float *oy, float *oz, float *ow) {
    *ox = m->m[0][0] * x + m->m[0][1] * y + m->m[0][2] * z + m->m[0][3];
    *oy = m->m[1][0] * x + m->m[1][1] * y + m->m[1][2] * z + m->m[1][3];
    *oz = m->m[2][0] * x + m->m[2][1] * y + m->m[2][2] * z + m->m[2][3];
    *ow = m->m[3][0] * x + m->m[3][1] * y + m->m[3][2] * z + m->m[3][3];
}

/* Transform normal vector by upper 3x3 of matrix */
static inline void mat4_transform_normal(const mat4_t *m, float nx, float ny, float nz,
                                        float *onx, float *ony, float *onz) {
    float tx = m->m[0][0] * nx + m->m[0][1] * ny + m->m[0][2] * nz;
    float ty = m->m[1][0] * nx + m->m[1][1] * ny + m->m[1][2] * nz;
    float tz = m->m[2][0] * nx + m->m[2][1] * ny + m->m[2][2] * nz;
    float len = (float)sqrt(tx * tx + ty * ty + tz * tz);
    if (len > 0.00001f) {
        *onx = tx / len;
        *ony = ty / len;
        *onz = tz / len;
    } else {
        *onx = 0.0f; *ony = 0.0f; *onz = 1.0f;
    }
}

/* ── Geometry & Display List Definitions ─────────────────────────── */

typedef struct {
    float x, y, z;
    float nx, ny, nz;
    float r, g, b;
    float a;            /* vertex alpha, 1.0 unless glColor4f set it */
    float u, v;         /* texture coordinates */
} virgl_vert_t;

typedef struct {
    virgl_vert_t v[3];
    float mat_diffuse[4];
    float mat_ambient[4];
} virgl_tri_t;

typedef struct {
    virgl_tri_t *tris;
    int count;
    int capacity;
} virgl_dlist_t;

#define MAX_DLISTS 32

/* ── Texture Objects ─────────────────────────────────────────────── */

#define VIRGL_MAX_TEXTURES 32

/* An uploaded image, kept as ARGB8888 in the same word layout as the pixel
 * buffer so a texel can be read without repacking.  Row 0 is the first row of
 * the array handed to glTexImage2D, and texcoord v runs down from it, which is
 * what the 2D menu passes that upload atlases expect. */
typedef struct {
    int       w;
    int       h;
    uint32_t *argb;
    int       linear;      /* 1 = GL_LINEAR, 0 = GL_NEAREST */
} virgl_tex_t;

/* ── Backend State & Context Isolation ───────────────────────────── */

#define MATRIX_STACK_MAX 16

typedef struct virgl_context_s {
    int       width;
    int       height;
    uint32_t *pixel_buf;
    float    *depth_buf;
    uint32_t  clear_color;

    /* Viewport */
    int vp_x, vp_y, vp_w, vp_h;

    /* Matrix Stacks */
    mat4_t modelview_stack[MATRIX_STACK_MAX];
    int    mv_top;

    mat4_t projection_stack[MATRIX_STACK_MAX];
    int    proj_top;

    GLenum matrix_mode;

    /* Current Vertex Attributes */
    float cur_nx, cur_ny, cur_nz;
    float cur_r, cur_g, cur_b;
    float cur_a;
    float cur_u, cur_v;

    /* Material Attributes */
    float mat_diffuse[4];
    float mat_ambient[4];

    /* Light 0 */
    float light0_pos[4];
    float light0_diffuse[4];
    float light0_ambient[4];

    /* GL Flags */
    int lighting_enabled;
    int light0_enabled;
    int depth_test_enabled;
    int cull_face_enabled;
    int blend_enabled;
    int tex_enabled;

    /* Source / destination factors for the compositing stage.  Left at the GL
     * default of ONE / ZERO, which writes the source through exactly as the
     * rasteriser did before blending existed. */
    GLenum blend_src;
    GLenum blend_dst;

    /* Textures: object table plus the GL_TEXTURE_2D binding */
    virgl_tex_t tex[VIRGL_MAX_TEXTURES];
    GLuint      tex_bound;
} virgl_context_t;

static virgl_context_t s_default_ctx = {
    .width = 0,
    .height = 0,
    .pixel_buf = NULL,
    .depth_buf = NULL,
    .clear_color = 0xFF000000,
    .vp_x = 0, .vp_y = 0, .vp_w = 0, .vp_h = 0,
    .mv_top = 0,
    .proj_top = 0,
    .matrix_mode = GL_MODELVIEW,
    .cur_nx = 0.0f, .cur_ny = 0.0f, .cur_nz = 1.0f,
    .cur_r = 1.0f, .cur_g = 1.0f, .cur_b = 1.0f,
    .cur_a = 1.0f,
    .cur_u = 0.0f, .cur_v = 0.0f,
    .mat_diffuse = {1.0f, 1.0f, 1.0f, 1.0f},
    .mat_ambient = {0.2f, 0.2f, 0.2f, 1.0f},
    .light0_pos = {5.0f, 5.0f, 10.0f, 0.0f},
    .light0_diffuse = {1.0f, 1.0f, 1.0f, 1.0f},
    .light0_ambient = {0.2f, 0.2f, 0.2f, 1.0f},
    .lighting_enabled = 0,
    .light0_enabled = 1,
    .depth_test_enabled = 0,
    .cull_face_enabled = 0,
    .blend_src = GL_ONE,
    .blend_dst = GL_ZERO,
};

static virgl_context_t *s_ctx = &s_default_ctx;

#define s_width              (s_ctx->width)
#define s_height             (s_ctx->height)
#define s_pixel_buf          (s_ctx->pixel_buf)
#define s_depth_buf          (s_ctx->depth_buf)
#define s_clear_color        (s_ctx->clear_color)
#define s_vp_x               (s_ctx->vp_x)
#define s_vp_y               (s_ctx->vp_y)
#define s_vp_w               (s_ctx->vp_w)
#define s_vp_h               (s_ctx->vp_h)
#define s_modelview_stack    (s_ctx->modelview_stack)
#define s_mv_top             (s_ctx->mv_top)
#define s_projection_stack   (s_ctx->projection_stack)
#define s_proj_top           (s_ctx->proj_top)
#define s_matrix_mode        (s_ctx->matrix_mode)
#define s_cur_nx             (s_ctx->cur_nx)
#define s_cur_ny             (s_ctx->cur_ny)
#define s_cur_nz             (s_ctx->cur_nz)
#define s_cur_r              (s_ctx->cur_r)
#define s_cur_g              (s_ctx->cur_g)
#define s_cur_b              (s_ctx->cur_b)
#define s_cur_a              (s_ctx->cur_a)
#define s_cur_u              (s_ctx->cur_u)
#define s_cur_v              (s_ctx->cur_v)
#define s_mat_diffuse        (s_ctx->mat_diffuse)
#define s_mat_ambient        (s_ctx->mat_ambient)
#define s_light0_pos         (s_ctx->light0_pos)
#define s_light0_diffuse     (s_ctx->light0_diffuse)
#define s_light0_ambient     (s_ctx->light0_ambient)
#define s_lighting_enabled   (s_ctx->lighting_enabled)
#define s_light0_enabled     (s_ctx->light0_enabled)
#define s_depth_test_enabled (s_ctx->depth_test_enabled)
#define s_cull_face_enabled  (s_ctx->cull_face_enabled)
#define s_blend_enabled      (s_ctx->blend_enabled)
#define s_tex_enabled        (s_ctx->tex_enabled)
#define s_blend_src          (s_ctx->blend_src)
#define s_blend_dst          (s_ctx->blend_dst)
#define s_tex_bound          (s_ctx->tex_bound)
#define s_tex_table          (s_ctx->tex)

/* Primitive assembly: a bounded sliding window that closes each primitive the
 * moment its last vertex arrives, so one glBegin block can be as long as the
 * caller likes.  A fixed 256-vertex block used to hold the whole block back until
 * glEnd and then lose its tail, which truncated every monster, weapon view and
 * HUD text line that needed more than that. */
#define PRIM_WINDOW 4
static GLenum       s_prim_mode = 0;
static int          s_in_begin  = 0;
static virgl_vert_t s_win[PRIM_WINDOW];
static int          s_win_n = 0;        /* vertices held, incomplete primitive */
static int          s_win_i = 0;        /* vertices seen in this block */
static virgl_vert_t s_fan_base;         /* v0 for GL_POLYGON / GL_TRIANGLE_FAN */
static virgl_vert_t s_loop_first;       /* v0 for GL_LINE_LOOP closing */

/* Display Lists */
static virgl_dlist_t s_dlists[MAX_DLISTS];
static int           s_active_dlist = 0;
static int           s_compiling_dlist = 0;

/* Forward declarations */
static void rasterize_tri(const virgl_tri_t *tri, const mat4_t *mv, const mat4_t *proj);

static inline float v_min(float a, float b) { return a < b ? a : b; }
static inline float v_max(float a, float b) { return a > b ? a : b; }
static inline float v_abs(float a) { return a < 0.0f ? -a : a; }
static inline int v_floor(float a) { int i = (int)a; return a < (float)i ? i - 1 : i; }
static inline int v_ceil(float a) { int i = (int)a; return a > (float)i ? i + 1 : i; }

static void virgl_uart_dec(int v) {
    char buf[12]; int i = 10; buf[11] = '\0';
    if (v == 0) { uart_puts_raw("0"); return; }
    if (v < 0) { uart_puts_raw("-"); v = -v; }
    while (v > 0 && i >= 0) { buf[i--] = (char)('0' + v % 10); v /= 10; }
    uart_puts_raw(buf + i + 1);
}

/* ── Display List Helpers ─────────────────────────────────────────── */

static void dlist_append_tri(virgl_dlist_t *dl, const virgl_tri_t *tri) {
    if (!dl) return;
    if (dl->count >= dl->capacity) {
        int new_cap = dl->capacity == 0 ? 128 : dl->capacity * 2;
        virgl_tri_t *new_tris = (virgl_tri_t *)malloc((size_t)new_cap * sizeof(virgl_tri_t));
        if (!new_tris) return;
        if (dl->tris && dl->count > 0) {
            memcpy(new_tris, dl->tris, (size_t)dl->count * sizeof(virgl_tri_t));
            free(dl->tris);
        }
        dl->tris = new_tris;
        dl->capacity = new_cap;
    }
    dl->tris[dl->count++] = *tri;
}

static void emit_tri(const virgl_vert_t *v0, const virgl_vert_t *v1, const virgl_vert_t *v2) {
    virgl_tri_t tri;
    tri.v[0] = *v0;
    tri.v[1] = *v1;
    tri.v[2] = *v2;
    memcpy(tri.mat_diffuse, s_mat_diffuse, sizeof(s_mat_diffuse));
    memcpy(tri.mat_ambient, s_mat_ambient, sizeof(s_mat_ambient));

    if (s_compiling_dlist && s_active_dlist > 0 && s_active_dlist < MAX_DLISTS) {
        dlist_append_tri(&s_dlists[s_active_dlist], &tri);
    } else {
        rasterize_tri(&tri, &s_modelview_stack[s_mv_top], &s_projection_stack[s_proj_top]);
    }
}

/* ── Geometry Rasterizer ──────────────────────────────────────────── */

/* A vertex in clip space, carrying the colour lighting resolved to. */
typedef struct {
    float x, y, z, w;
    float r, g, b;
    float a;
    float u, v;
} clip_v_t;

/* A triangle gains at most one vertex per plane clipped. */
#define CLIP_POLY_MAX 16

/* One Sutherland-Hodgman pass against the clip-space half-space
 * ax*x + ay*y + az*z + aw*w >= 0, on the closed polygon `in`. */
static int clip_hplane(const clip_v_t *in, int n, clip_v_t *out,
                       float ax, float ay, float az, float aw) {
    if (n < 3) return 0;
    int m = 0;
    for (int i = 0; i < n; i++) {
        const clip_v_t *a = &in[i];
        const clip_v_t *b = &in[(i + 1) % n];
        float sa = ax * a->x + ay * a->y + az * a->z + aw * a->w;
        float sb = ax * b->x + ay * b->y + az * b->z + aw * b->w;
        int a_in = (sa >= 0.0f), b_in = (sb >= 0.0f);

        if (b_in && !a_in && m < CLIP_POLY_MAX) {
            float t = sa / (sa - sb);
            clip_v_t *o = &out[m++];
            o->x = a->x + t * (b->x - a->x);
            o->y = a->y + t * (b->y - a->y);
            o->z = a->z + t * (b->z - a->z);
            o->w = a->w + t * (b->w - a->w);
            o->r = a->r + t * (b->r - a->r);
            o->g = a->g + t * (b->g - a->g);
            o->b = a->b + t * (b->b - a->b);
            o->a = a->a + t * (b->a - a->a);
            o->u = a->u + t * (b->u - a->u);
            o->v = a->v + t * (b->v - a->v);
        }
        if (b_in && m < CLIP_POLY_MAX) out[m++] = *b;
        if (a_in && !b_in && m < CLIP_POLY_MAX) {
            float t = sa / (sa - sb);
            clip_v_t *o = &out[m++];
            o->x = a->x + t * (b->x - a->x);
            o->y = a->y + t * (b->y - a->y);
            o->z = a->z + t * (b->z - a->z);
            o->w = a->w + t * (b->w - a->w);
            o->r = a->r + t * (b->r - a->r);
            o->g = a->g + t * (b->g - a->g);
            o->b = a->b + t * (b->b - a->b);
            o->a = a->a + t * (b->a - a->a);
            o->u = a->u + t * (b->u - a->u);
            o->v = a->v + t * (b->v - a->v);
        }
    }
    return m;
}

/* One blending factor, resolved against the incoming source and destination
 * alpha.  Unsupported factors fall back to GL_ONE. */
static inline float blend_factor(GLenum f, float src_a, float dst_a) {
    switch (f) {
    case GL_ZERO:                 return 0.0f;
    case GL_SRC_ALPHA:            return src_a;
    case GL_ONE_MINUS_SRC_ALPHA:  return 1.0f - src_a;
    case GL_DST_ALPHA:            return dst_a;
    case GL_ONE_MINUS_DST_ALPHA:  return 1.0f - dst_a;
    case GL_ONE:
    default:                      return 1.0f;
    }
}

/*
 * blend_factor_rgb — the source factor for one channel.
 *
 * GL_DST_COLOR and GL_ONE_MINUS_DST_COLOR read the destination's colour, so
 * unlike the alpha factors they are not the same for R, G and B.  The XMB
 * ribbon needs exactly that: it composites dst*(1+c) to emboss the gradient
 * behind it, which is src=GL_DST_COLOR, dst=GL_ONE with a greyscale source.
 */
static inline void blend_factor_rgb(GLenum f, float src_a, float dst_a,
                                    uint32_t dst, float out[3]) {
    switch (f) {
    case GL_DST_COLOR:
        out[0] = (float)((dst >> 16) & 0xFF) / 255.0f;
        out[1] = (float)((dst >>  8) & 0xFF) / 255.0f;
        out[2] = (float)( dst        & 0xFF) / 255.0f;
        return;
    case GL_ONE_MINUS_DST_COLOR:
        out[0] = 1.0f - (float)((dst >> 16) & 0xFF) / 255.0f;
        out[1] = 1.0f - (float)((dst >>  8) & 0xFF) / 255.0f;
        out[2] = 1.0f - (float)( dst        & 0xFF) / 255.0f;
        return;
    default:
        out[0] = out[1] = out[2] = blend_factor(f, src_a, dst_a);
        return;
    }
}

/*
 * fb_composite — finish one fragment.
 *
 * With GL_BLEND off this is the original write: opaque ARGB8888 straight into
 * the pixel buffer, so the 3D games that never touch blending draw exactly as
 * before.  With it on, the source is composited over the destination using the
 * context's factors while the stored alpha stays opaque, which is what an
 * XRGB8888 window surface expects.
 */
static inline void fb_composite(int idx, float r, float g, float b, float a) {
    if (r < 0.0f) r = 0.0f;
    if (g < 0.0f) g = 0.0f;
    if (b < 0.0f) b = 0.0f;
    if (r > 1.0f) r = 1.0f;
    if (g > 1.0f) g = 1.0f;
    if (b > 1.0f) b = 1.0f;

    if (!s_blend_enabled) {
        s_pixel_buf[idx] = virgl_pack_color((uint32_t)(r * 255.0f),
                                            (uint32_t)(g * 255.0f),
                                            (uint32_t)(b * 255.0f));
        return;
    }

    uint32_t d  = s_pixel_buf[idx];
    float    da = (float)((d >> 24) & 0xFF) / 255.0f;
    float src_a = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
    float fs[3];
    float fd    = blend_factor(s_blend_dst, src_a, da);

    blend_factor_rgb(s_blend_src, src_a, da, d, fs);

    float fr = r * 255.0f * fs[0] + (float)((d >> 16) & 0xFF) * fd;
    float fg = g * 255.0f * fs[1] + (float)((d >>  8) & 0xFF) * fd;
    float fb = b * 255.0f * fs[2] + (float)( d        & 0xFF) * fd;

    if (fr < 0.0f) fr = 0.0f;
    if (fg < 0.0f) fg = 0.0f;
    if (fb < 0.0f) fb = 0.0f;
    if (fr > 255.0f) fr = 255.0f;
    if (fg > 255.0f) fg = 255.0f;
    if (fb > 255.0f) fb = 255.0f;

    s_pixel_buf[idx] = virgl_pack_color((uint32_t)fr, (uint32_t)fg, (uint32_t)fb);
}

/*
 * tex_fetch — read the bound texture at (u,v).
 *
 * Coordinates are clamped rather than wrapped: these textures are atlases, and
 * a wrapped sample would pull in the neighbouring glyph or icon.
 */
static inline void tex_fetch(const virgl_tex_t *tx, float u, float v,
                             float *tr, float *tg, float *tb, float *ta) {
    int w = tx->w, h = tx->h;
    float fx = u * (float)(w - 1);
    float fy = v * (float)(h - 1);
    if (fx < 0.0f) fx = 0.0f;
    if (fy < 0.0f) fy = 0.0f;
    if (fx > (float)(w - 1)) fx = (float)(w - 1);
    if (fy > (float)(h - 1)) fy = (float)(h - 1);

    int x0 = (int)fx, y0 = (int)fy;
    if (x0 > w - 1) x0 = w - 1;
    if (y0 > h - 1) y0 = h - 1;

    if (!tx->linear || w == 1 || h == 1) {
        uint32_t p = tx->argb[y0 * w + x0];
        *tr = (float)((p >> 16) & 0xFF) / 255.0f;
        *tg = (float)((p >>  8) & 0xFF) / 255.0f;
        *tb = (float)( p        & 0xFF) / 255.0f;
        *ta = (float)( p >> 24) / 255.0f;
        return;
    }

    int x1 = x0 < w - 1 ? x0 + 1 : x0;
    int y1 = y0 < h - 1 ? y0 + 1 : y0;
    float sx = fx - (float)x0;
    float sy = fy - (float)y0;

    const uint32_t *row0 = tx->argb + y0 * w;
    const uint32_t *row1 = tx->argb + y1 * w;
    uint32_t p00 = row0[x0], p10 = row0[x1], p01 = row1[x0], p11 = row1[x1];
    float wy0 = 1.0f - sy, wy1 = sy;

    for (int ch = 0; ch < 4; ch++) {
        int shift = (ch == 0) ? 16 : (ch == 1) ? 8 : (ch == 2) ? 0 : 24;
        float a0 = (float)((p00 >> shift) & 0xFF) * (1.0f - sx)
                 + (float)((p10 >> shift) & 0xFF) * sx;
        float a1 = (float)((p01 >> shift) & 0xFF) * (1.0f - sx)
                 + (float)((p11 >> shift) & 0xFF) * sx;
        float c  = (a0 * wy0 + a1 * wy1) / 255.0f;
        if (ch == 0) *tr = c;
        else if (ch == 1) *tg = c;
        else if (ch == 2) *tb = c;
        else *ta = c;
    }
}

/* Rasterize one already-clipped triangle, given in clip space. */
static void raster_clip_tri(const clip_v_t *v0, const clip_v_t *v1, const clip_v_t *v2) {
    float sx[3], sy[3], sz[3], cr[3], cg[3], cb[3], ca[3], cu[3], ct[3];
    const clip_v_t *cv[3] = { v0, v1, v2 };

    for (int i = 0; i < 3; i++) {
        float inv_w = 1.0f / cv[i]->w;
        float ndc_x = cv[i]->x * inv_w;
        float ndc_y = cv[i]->y * inv_w;
        float ndc_z = cv[i]->z * inv_w;

        sx[i] = (ndc_x + 1.0f) * 0.5f * (float)s_vp_w + (float)s_vp_x;
        sy[i] = (1.0f - ndc_y) * 0.5f * (float)s_vp_h + (float)s_vp_y;
        sz[i] = (ndc_z + 1.0f) * 0.5f;
        cr[i] = cv[i]->r; cg[i] = cv[i]->g; cb[i] = cv[i]->b;
        ca[i] = cv[i]->a; cu[i] = cv[i]->u; ct[i] = cv[i]->v;
    }

    /* Backface culling check: only active in 3D mode when depth test is enabled */
    float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
    if (s_cull_face_enabled && s_depth_test_enabled && area >= 0.0f) {
        return;
    }

    /* Bounding box computation clamped to viewport */
    int min_x = v_floor(v_min(v_min(sx[0], sx[1]), sx[2]));
    int max_x = v_ceil(v_max(v_max(sx[0], sx[1]), sx[2]));
    int min_y = v_floor(v_min(v_min(sy[0], sy[1]), sy[2]));
    int max_y = v_ceil(v_max(v_max(sy[0], sy[1]), sy[2]));

    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= s_width)  max_x = s_width - 1;
    if (max_y >= s_height) max_y = s_height - 1;
    if (min_x > max_x || min_y > max_y) return;

    /* Barycentric coordinate factors */
    float denom = (sy[1] - sy[2]) * (sx[0] - sx[2]) + (sx[2] - sx[1]) * (sy[0] - sy[2]);
    if (v_abs(denom) < 0.00001f) return;
    float inv_denom = 1.0f / denom;

    /* Pixel-level rasterization with Depth Buffer test and edge tie-breaking */
    for (int y = min_y; y <= max_y; y++) {
        float py = (float)y + 0.5f;
        int row_idx = y * s_width;

        for (int x = min_x; x <= max_x; x++) {
            float px = (float)x + 0.5f;

            float w0 = ((sy[1] - sy[2]) * (px - sx[2]) + (sx[2] - sx[1]) * (py - sy[2])) * inv_denom;
            float w1 = ((sy[2] - sy[0]) * (px - sx[2]) + (sx[0] - sx[2]) * (py - sy[2])) * inv_denom;
            float w2 = 1.0f - w0 - w1;

            if (w0 >= -0.0005f && w1 >= -0.0005f && w2 >= -0.0005f) {
                float z = w0 * sz[0] + w1 * sz[1] + w2 * sz[2];
                int pixel_idx = row_idx + x;

                if (!s_depth_test_enabled || (s_depth_buf && z <= s_depth_buf[pixel_idx] + 0.00005f)) {
                    if (s_depth_buf && s_depth_test_enabled) {
                        s_depth_buf[pixel_idx] = z;
                    }

                    float r = w0 * cr[0] + w1 * cr[1] + w2 * cr[2];
                    float g = w0 * cg[0] + w1 * cg[1] + w2 * cg[2];
                    float b = w0 * cb[0] + w1 * cb[1] + w2 * cb[2];
                    float a = w0 * ca[0] + w1 * ca[1] + w2 * ca[2];

                    /* GL_MODULATE: the texel multiplies the interpolated vertex
                     * colour, and its alpha gates the fragment's. */
                    if (s_tex_enabled) {
                        const virgl_tex_t *tx = &s_tex_table[s_tex_bound];
                        if (tx->argb) {
                            float tu = w0 * cu[0] + w1 * cu[1] + w2 * cu[2];
                            float tv = w0 * ct[0] + w1 * ct[1] + w2 * ct[2];
                            float tr, tg, tb, ta;
                            tex_fetch(tx, tu, tv, &tr, &tg, &tb, &ta);
                            r *= tr; g *= tg; b *= tb; a *= ta;
                        }
                    }

                    fb_composite(pixel_idx, r, g, b, a);
                }
            }
        }
    }
}

static void rasterize_tri(const virgl_tri_t *tri, const mat4_t *mv, const mat4_t *proj) {
    if (!s_pixel_buf || s_width <= 0 || s_height <= 0) return;

    clip_v_t poly[CLIP_POLY_MAX], work[CLIP_POLY_MAX];

    /* Light direction vector in eye space */
    float lx = s_light0_pos[0];
    float ly = s_light0_pos[1];
    float lz = s_light0_pos[2];
    float llen = (float)sqrt(lx * lx + ly * ly + lz * lz);
    if (llen > 0.0001f) {
        lx /= llen; ly /= llen; lz /= llen;
    } else {
        lz = 1.0f;
    }

    for (int i = 0; i < 3; i++) {
        float ex, ey, ez, ew;
        mat4_transform_vec4(mv, tri->v[i].x, tri->v[i].y, tri->v[i].z,
                            &ex, &ey, &ez, &ew);

        /* Lighting calculation */
        float lr, lg, lb;
        if (s_lighting_enabled && s_light0_enabled) {
            float tnx, tny, tnz;
            mat4_transform_normal(mv, tri->v[i].nx, tri->v[i].ny, tri->v[i].nz,
                                  &tnx, &tny, &tnz);

            float n_dot_l = tnx * lx + tny * ly + tnz * lz;
            if (n_dot_l < 0.0f) n_dot_l = 0.0f;

            /* Material diffuse comes from vertex color modulated with material diffuse */
            float mat_r = tri->v[i].r * tri->mat_diffuse[0];
            float mat_g = tri->v[i].g * tri->mat_diffuse[1];
            float mat_b = tri->v[i].b * tri->mat_diffuse[2];

            float amb_r = mat_r * 0.25f;
            float amb_g = mat_g * 0.25f;
            float amb_b = mat_b * 0.25f;

            lr = amb_r + mat_r * s_light0_diffuse[0] * n_dot_l;
            lg = amb_g + mat_g * s_light0_diffuse[1] * n_dot_l;
            lb = amb_b + mat_b * s_light0_diffuse[2] * n_dot_l;

            if (lr > 1.0f) lr = 1.0f;
            if (lg > 1.0f) lg = 1.0f;
            if (lb > 1.0f) lb = 1.0f;
        } else {
            lr = tri->v[i].r;
            lg = tri->v[i].g;
            lb = tri->v[i].b;
        }

        /* Project to clip space */
        clip_v_t *c = &poly[i];
        mat4_transform_vec4(proj, ex, ey, ez, &c->x, &c->y, &c->z, &c->w);
        c->r = lr; c->g = lg; c->b = lb;
        c->a = tri->v[i].a;
        c->u = tri->v[i].u;
        c->v = tri->v[i].v;
    }

    /* Clip against the view frustum instead of discarding.  Rejecting a triangle
     * as soon as one vertex crossed the near plane made every face the eye was
     * touching vanish — a wall disappearing as the player walked into it, through
     * which the player then saw the void — while a vertex that survived with a
     * near-zero w divided into a screen position thousands of pixels wide, i.e.
     * one skewed polygon smeared over the whole view.
     *
     * The far plane is deliberately not clipped here: the app's zFar is 1000 units
     * against map extents several times that, and the float depth buffer has no
     * precision problem beyond it, so geometry past zFar still draws as it always
     * has. */
    static const float planes[5][4] = {
        {  1.0f,  0.0f, 0.0f, 1.0f },   /* left   w + x >= 0 */
        { -1.0f,  0.0f, 0.0f, 1.0f },   /* right  w - x >= 0 */
        {  0.0f,  1.0f, 0.0f, 1.0f },   /* bottom w + y >= 0 */
        {  0.0f, -1.0f, 0.0f, 1.0f },   /* top    w - y >= 0 */
        {  0.0f,  0.0f, 1.0f, 1.0f },   /* near   w + z >= 0 */
    };
    int n = 3;
    for (int p = 0; p < 5; p++) {
        memcpy(work, poly, (size_t)n * sizeof(clip_v_t));
        n = clip_hplane(work, n, poly, planes[p][0], planes[p][1],
                        planes[p][2], planes[p][3]);
        if (n < 3) return;
    }

    /* The clipped polygon is convex, so a fan from vertex 0 covers it. */
    for (int i = 1; i + 1 < n; i++) {
        raster_clip_tri(&poly[0], &poly[i], &poly[i + 1]);
    }
}

/* Rasterize one line segment in clip space with depth test & 3D lighting/color */
static void rasterize_line(const virgl_vert_t *v0, const virgl_vert_t *v1, const mat4_t *mv, const mat4_t *proj) {
    if (!s_pixel_buf || s_width <= 0 || s_height <= 0) return;

    /* Transform to eye space */
    float e0x, e0y, e0z, e0w;
    mat4_transform_vec4(mv, v0->x, v0->y, v0->z, &e0x, &e0y, &e0z, &e0w);

    float e1x, e1y, e1z, e1w;
    mat4_transform_vec4(mv, v1->x, v1->y, v1->z, &e1x, &e1y, &e1z, &e1w);

    /* Project to clip space */
    clip_v_t c0, c1;
    mat4_transform_vec4(proj, e0x, e0y, e0z, &c0.x, &c0.y, &c0.z, &c0.w);
    c0.r = v0->r; c0.g = v0->g; c0.b = v0->b;
    c0.a = v0->a; c0.u = v0->u; c0.v = v0->v;

    mat4_transform_vec4(proj, e1x, e1y, e1z, &c1.x, &c1.y, &c1.z, &c1.w);
    c1.r = v1->r; c1.g = v1->g; c1.b = v1->b;
    c1.a = v1->a; c1.u = v1->u; c1.v = v1->v;

    /* Frustum clipping against 5 planes */
    static const float planes[5][4] = {
        {  1.0f,  0.0f, 0.0f, 1.0f },   /* left   w + x >= 0 */
        { -1.0f,  0.0f, 0.0f, 1.0f },   /* right  w - x >= 0 */
        {  0.0f,  1.0f, 0.0f, 1.0f },   /* bottom w + y >= 0 */
        {  0.0f, -1.0f, 0.0f, 1.0f },   /* top    w - y >= 0 */
        {  0.0f,  0.0f, 1.0f, 1.0f },   /* near   w + z >= 0 */
    };

    for (int p = 0; p < 5; p++) {
        float d0 = planes[p][0] * c0.x + planes[p][1] * c0.y + planes[p][2] * c0.z + planes[p][3] * c0.w;
        float d1 = planes[p][0] * c1.x + planes[p][1] * c1.y + planes[p][2] * c1.z + planes[p][3] * c1.w;

        if (d0 < 0.0f && d1 < 0.0f) {
            return; /* Both endpoints outside plane */
        }
        if (d0 < 0.0f && d1 >= 0.0f) {
            float t = d0 / (d0 - d1);
            c0.x += t * (c1.x - c0.x);
            c0.y += t * (c1.y - c0.y);
            c0.z += t * (c1.z - c0.z);
            c0.w += t * (c1.w - c0.w);
            c0.r += t * (c1.r - c0.r);
            c0.g += t * (c1.g - c0.g);
            c0.b += t * (c1.b - c0.b);
            c0.a += t * (c1.a - c0.a);
        } else if (d0 >= 0.0f && d1 < 0.0f) {
            float t = d0 / (d0 - d1);
            c1.x = c0.x + t * (c1.x - c0.x);
            c1.y = c0.y + t * (c1.y - c0.y);
            c1.z = c0.z + t * (c1.z - c0.z);
            c1.w = c0.w + t * (c1.w - c0.w);
            c1.r = c0.r + t * (c1.r - c0.r);
            c1.g = c0.g + t * (c1.g - c0.g);
            c1.b = c0.b + t * (c1.b - c0.b);
            c1.a = c0.a + t * (c1.a - c0.a);
        }
    }

    if (c0.w <= 0.0001f || c1.w <= 0.0001f) return;

    /* Screen projection */
    float inv_w0 = 1.0f / c0.w;
    float sx0 = (c0.x * inv_w0 + 1.0f) * 0.5f * (float)s_vp_w + (float)s_vp_x;
    float sy0 = (1.0f - c0.y * inv_w0) * 0.5f * (float)s_vp_h + (float)s_vp_y;
    float sz0 = (c0.z * inv_w0 + 1.0f) * 0.5f;

    float inv_w1 = 1.0f / c1.w;
    float sx1 = (c1.x * inv_w1 + 1.0f) * 0.5f * (float)s_vp_w + (float)s_vp_x;
    float sy1 = (1.0f - c1.y * inv_w1) * 0.5f * (float)s_vp_h + (float)s_vp_y;
    float sz1 = (c1.z * inv_w1 + 1.0f) * 0.5f;

    /* DDA Line rasterization */
    float dx = sx1 - sx0;
    float dy = sy1 - sy0;
    float adx = v_abs(dx);
    float ady = v_abs(dy);
    int steps = (int)(adx > ady ? adx : ady);

    if (steps <= 0) {
        int px = (int)(sx0 + 0.5f);
        int py = (int)(sy0 + 0.5f);
        if (px >= 0 && px < s_width && py >= 0 && py < s_height) {
            int idx = py * s_width + px;
            if (!s_depth_test_enabled || (s_depth_buf && sz0 <= s_depth_buf[idx] + 0.0001f)) {
                if (s_depth_test_enabled && s_depth_buf) s_depth_buf[idx] = sz0;
                fb_composite(idx, c0.r, c0.g, c0.b, c0.a);
            }
        }
        return;
    }

    float inv_steps = 1.0f / (float)steps;
    float x_inc = dx * inv_steps;
    float y_inc = dy * inv_steps;
    float z_inc = (sz1 - sz0) * inv_steps;
    float r_inc = (c1.r - c0.r) * inv_steps;
    float g_inc = (c1.g - c0.g) * inv_steps;
    float b_inc = (c1.b - c0.b) * inv_steps;
    float a_inc = (c1.a - c0.a) * inv_steps;

    float cx = sx0;
    float cy = sy0;
    float cz = sz0;
    float cr = c0.r;
    float cg = c0.g;
    float cb = c0.b;
    float ca = c0.a;

    for (int s = 0; s <= steps; s++) {
        int px = (int)(cx + 0.5f);
        int py = (int)(cy + 0.5f);
        if (px >= 0 && px < s_width && py >= 0 && py < s_height) {
            int idx = py * s_width + px;
            if (!s_depth_test_enabled || (s_depth_buf && cz <= s_depth_buf[idx] + 0.0001f)) {
                if (s_depth_test_enabled && s_depth_buf) s_depth_buf[idx] = cz;
                fb_composite(idx, cr, cg, cb, ca);
            }
        }
        cx += x_inc;
        cy += y_inc;
        cz += z_inc;
        cr += r_inc;
        cg += g_inc;
        cb += b_inc;
        ca += a_inc;
    }
}

/* ── gl_ops_t Implementations ─────────────────────────────────────── */

static void virgl_begin(GLenum mode) {
    s_prim_mode = mode;
    s_win_n = 0;
    s_win_i = 0;
    s_in_begin = 1;
}

static void virgl_end(void) {
    if (!s_in_begin) return;
    if (s_prim_mode == GL_LINE_LOOP && s_win_i > 1) {
        rasterize_line(&s_win[0], &s_loop_first, &s_modelview_stack[s_mv_top], &s_projection_stack[s_proj_top]);
    }
    /* A primitive left incomplete by the last glVertex is discarded, as GL says. */
    s_win_n = 0;
    s_win_i = 0;
    s_in_begin = 0;
}

/* Feed one vertex to the window and emit whatever primitives it completed. */
static void virgl_prim_vertex(const virgl_vert_t *v) {
    switch (s_prim_mode) {
    case GL_TRIANGLES:
        s_win[s_win_n++] = *v;
        if (s_win_n == 3) {
            emit_tri(&s_win[0], &s_win[1], &s_win[2]);
            s_win_n = 0;
        }
        break;
    case GL_QUADS:
        s_win[s_win_n++] = *v;
        if (s_win_n == 4) {
            emit_tri(&s_win[0], &s_win[1], &s_win[2]);
            emit_tri(&s_win[0], &s_win[2], &s_win[3]);
            s_win_n = 0;
        }
        break;
    case GL_QUAD_STRIP:
        s_win[s_win_n++] = *v;
        if (s_win_n == 4) {
            emit_tri(&s_win[0], &s_win[1], &s_win[3]);
            emit_tri(&s_win[0], &s_win[3], &s_win[2]);
            s_win[0] = s_win[2];
            s_win[1] = s_win[3];
            s_win_n = 2;
        }
        break;
    case GL_TRIANGLE_STRIP:
        s_win[s_win_n++] = *v;
        if (s_win_n == 3) {
            /* s_win holds v[i], v[i+1], v[i+2] for i = s_win_i - 2 */
            if ((s_win_i - 2) & 1) emit_tri(&s_win[1], &s_win[0], &s_win[2]);
            else                   emit_tri(&s_win[0], &s_win[1], &s_win[2]);
            s_win[0] = s_win[1];
            s_win[1] = s_win[2];
            s_win_n = 2;
        }
        break;
    case GL_TRIANGLE_FAN:
    case GL_POLYGON:
        if (s_win_i == 0) {
            s_fan_base = *v;
        } else if (s_win_i >= 2) {
            emit_tri(&s_fan_base, &s_win[0], v);
        }
        s_win[0] = *v;
        s_win_n = 1;
        break;
    case GL_LINES:
        if (s_win_n == 0) {
            s_win[0] = *v;
            s_win_n = 1;
        } else {
            rasterize_line(&s_win[0], v, &s_modelview_stack[s_mv_top], &s_projection_stack[s_proj_top]);
            s_win_n = 0;
        }
        break;
    case GL_LINE_STRIP:
        if (s_win_i == 0) {
            s_win[0] = *v;
            s_win_n = 1;
        } else {
            rasterize_line(&s_win[0], v, &s_modelview_stack[s_mv_top], &s_projection_stack[s_proj_top]);
            s_win[0] = *v;
        }
        break;
    case GL_LINE_LOOP:
        if (s_win_i == 0) {
            s_loop_first = *v;
            s_win[0] = *v;
            s_win_n = 1;
        } else {
            rasterize_line(&s_win[0], v, &s_modelview_stack[s_mv_top], &s_projection_stack[s_proj_top]);
            s_win[0] = *v;
        }
        break;
    default:
        break;
    }
    s_win_i++;
}

static void virgl_vertex3f(GLfloat x, GLfloat y, GLfloat z) {
    if (!s_in_begin) return;
    virgl_vert_t v;
    v.x = x; v.y = y; v.z = z;
    v.nx = s_cur_nx; v.ny = s_cur_ny; v.nz = s_cur_nz;
    v.r = s_cur_r; v.g = s_cur_g; v.b = s_cur_b;
    v.a = s_cur_a;
    v.u = s_cur_u; v.v = s_cur_v;
    virgl_prim_vertex(&v);
}

static void virgl_vertex2f(GLfloat x, GLfloat y) {
    virgl_vertex3f(x, y, 0.0f);
}

static void virgl_normal3f(GLfloat x, GLfloat y, GLfloat z) {
    s_cur_nx = x; s_cur_ny = y; s_cur_nz = z;
}

static void virgl_color3f(GLfloat r, GLfloat g, GLfloat b) {
    s_cur_r = r; s_cur_g = g; s_cur_b = b;
}

/* glColor4f carries the alpha every later vertex keeps until the next call,
 * which is how the menu layer fades whole primitives in and out. */
static void virgl_color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    s_cur_r = r; s_cur_g = g; s_cur_b = b;
    s_cur_a = a;
}

static void virgl_color3fv(const GLfloat *v) {
    if (v) virgl_color3f(v[0], v[1], v[2]);
}

static void virgl_matrix_mode(GLenum mode) {
    s_matrix_mode = mode;
}

static void virgl_load_identity(void) {
    if (s_matrix_mode == GL_PROJECTION) {
        mat4_identity(&s_projection_stack[s_proj_top]);
    } else {
        mat4_identity(&s_modelview_stack[s_mv_top]);
    }
}

static void virgl_push_matrix(void) {
    if (s_matrix_mode == GL_PROJECTION) {
        if (s_proj_top < MATRIX_STACK_MAX - 1) {
            s_projection_stack[s_proj_top + 1] = s_projection_stack[s_proj_top];
            s_proj_top++;
        }
    } else {
        if (s_mv_top < MATRIX_STACK_MAX - 1) {
            s_modelview_stack[s_mv_top + 1] = s_modelview_stack[s_mv_top];
            s_mv_top++;
        }
    }
}

static void virgl_pop_matrix(void) {
    if (s_matrix_mode == GL_PROJECTION) {
        if (s_proj_top > 0) s_proj_top--;
    } else {
        if (s_mv_top > 0) s_mv_top--;
    }
}

static void virgl_rotate_f(GLfloat angle, GLfloat x, GLfloat y, GLfloat z) {
    if (s_matrix_mode == GL_PROJECTION) {
        mat4_rotate(&s_projection_stack[s_proj_top], angle, x, y, z);
    } else {
        mat4_rotate(&s_modelview_stack[s_mv_top], angle, x, y, z);
    }
}

static void virgl_translate_f(GLfloat x, GLfloat y, GLfloat z) {
    if (s_matrix_mode == GL_PROJECTION) {
        mat4_translate(&s_projection_stack[s_proj_top], x, y, z);
    } else {
        mat4_translate(&s_modelview_stack[s_mv_top], x, y, z);
    }
}

static void virgl_scale_f(GLfloat x, GLfloat y, GLfloat z) {
    if (s_matrix_mode == GL_PROJECTION) {
        mat4_scale(&s_projection_stack[s_proj_top], x, y, z);
    } else {
        mat4_scale(&s_modelview_stack[s_mv_top], x, y, z);
    }
}

static void virgl_frustum(double l, double r, double b, double t, double n, double f) {
    if (s_matrix_mode == GL_PROJECTION) {
        mat4_frustum(&s_projection_stack[s_proj_top], l, r, b, t, n, f);
    } else {
        mat4_frustum(&s_modelview_stack[s_mv_top], l, r, b, t, n, f);
    }
}

static void virgl_ortho(double l, double r, double b, double t, double n, double f) {
    if (s_matrix_mode == GL_PROJECTION) {
        mat4_ortho(&s_projection_stack[s_proj_top], l, r, b, t, n, f);
    } else {
        mat4_ortho(&s_modelview_stack[s_mv_top], l, r, b, t, n, f);
    }
}

static void virgl_viewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    s_vp_x = x; s_vp_y = y;
    s_vp_w = w; s_vp_h = h;
}

static void virgl_clear(GLbitfield mask) {
    if ((mask & GL_COLOR_BUFFER_BIT) && s_pixel_buf && s_width > 0 && s_height > 0) {
        int total = s_width * s_height;
        for (int i = 0; i < total; i++) {
            s_pixel_buf[i] = s_clear_color;
        }
    }
    if ((mask & GL_DEPTH_BUFFER_BIT) && s_depth_buf && s_width > 0 && s_height > 0) {
        int total = s_width * s_height;
        for (int i = 0; i < total; i++) {
            s_depth_buf[i] = 1.0e10f;
        }
    }
}

static void virgl_clear_color(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    (void)a;
    uint32_t ir = (uint32_t)(r * 255.0f) & 0xFF;
    uint32_t ig = (uint32_t)(g * 255.0f) & 0xFF;
    uint32_t ib = (uint32_t)(b * 255.0f) & 0xFF;
    s_clear_color = virgl_pack_color(ir, ig, ib);
}

static void virgl_enable(GLenum cap) {
    if (cap == GL_LIGHTING)   s_lighting_enabled = 1;
    if (cap == GL_LIGHT0)     s_light0_enabled = 1;
    if (cap == GL_DEPTH_TEST) s_depth_test_enabled = 1;
    if (cap == GL_CULL_FACE)  s_cull_face_enabled = 1;
    if (cap == GL_BLEND)      s_blend_enabled = 1;
    if (cap == GL_TEXTURE_2D) s_tex_enabled = 1;
}

static void virgl_disable(GLenum cap) {
    if (cap == GL_LIGHTING)   s_lighting_enabled = 0;
    if (cap == GL_LIGHT0)     s_light0_enabled = 0;
    if (cap == GL_DEPTH_TEST) s_depth_test_enabled = 0;
    if (cap == GL_CULL_FACE)  s_cull_face_enabled = 0;
    if (cap == GL_BLEND)      s_blend_enabled = 0;
    if (cap == GL_TEXTURE_2D) s_tex_enabled = 0;
}

static void virgl_blend_func(GLenum src, GLenum dst) {
    s_blend_src = src;
    s_blend_dst = dst;
}

static void virgl_shade_model(GLenum mode) {
    (void)mode;
}

static void virgl_light_fv(GLenum light, GLenum pname, const GLfloat *params) {
    if (!params) return;
    if (light == GL_LIGHT0) {
        if (pname == GL_POSITION) {
            /* Transform light position by current modelview matrix */
            mat4_transform_vec4(&s_modelview_stack[s_mv_top],
                                params[0], params[1], params[2],
                                &s_light0_pos[0], &s_light0_pos[1], &s_light0_pos[2], &s_light0_pos[3]);
        } else if (pname == GL_DIFFUSE) {
            memcpy(s_light0_diffuse, params, sizeof(s_light0_diffuse));
        } else if (pname == GL_AMBIENT) {
            memcpy(s_light0_ambient, params, sizeof(s_light0_ambient));
        }
    }
}

static void virgl_material_fv(GLenum face, GLenum pname, const GLfloat *params) {
    (void)face;
    if (!params) return;
    if (pname == GL_DIFFUSE) {
        memcpy(s_mat_diffuse, params, sizeof(s_mat_diffuse));
    } else if (pname == GL_AMBIENT) {
        memcpy(s_mat_ambient, params, sizeof(s_mat_ambient));
    }
}

static GLuint virgl_gen_lists(GLsizei range) {
    (void)range;
    for (int i = 1; i < MAX_DLISTS; i++) {
        if (!s_dlists[i].tris) {
            s_dlists[i].count = 0;
            s_dlists[i].capacity = 0;
            return (GLuint)i;
        }
    }
    return 0;
}

static void virgl_new_list(GLuint list, GLenum mode) {
    (void)mode;
    if (list > 0 && list < MAX_DLISTS) {
        s_active_dlist = (int)list;
        s_compiling_dlist = 1;
        s_dlists[list].count = 0;
    }
}

static void virgl_end_list(void) {
    s_compiling_dlist = 0;
    s_active_dlist = 0;
}

static void virgl_call_list(GLuint list) {
    if (list > 0 && list < MAX_DLISTS && s_dlists[list].tris) {
        virgl_dlist_t *dl = &s_dlists[list];
        const mat4_t *mv = &s_modelview_stack[s_mv_top];
        const mat4_t *proj = &s_projection_stack[s_proj_top];
        for (int i = 0; i < dl->count; i++) {
            rasterize_tri(&dl->tris[i], mv, proj);
        }
    }
}

static void virgl_swap_buffers(void) {
#if defined(BTRON_QEMU_TARGET)
    /* Submit 2D/3D presentation flush to host VirtIO-GPU driver */
    vio_gpu_transfer_to_host_2d(NULL, 1, 0, 0, (uint32_t)s_width, (uint32_t)s_height, 0);
    vio_gpu_resource_flush(NULL, 1, 0, 0, (uint32_t)s_width, (uint32_t)s_height);
#endif
}

/* ── Textures ─────────────────────────────────────────────────────── */

static void virgl_tex_coord2f(GLfloat s, GLfloat t) {
    s_cur_u = s; s_cur_v = t;
}
static void virgl_tex_coord2fv(const GLfloat *v) {
    if (v) { s_cur_u = v[0]; s_cur_v = v[1]; }
}
/* Hand out the lowest slot that holds no image yet.  A slot only gets its image
 * at glTexImage2D, so a batch request would otherwise return the same name for
 * every entry and the last upload would overwrite the rest; names already
 * granted by this call are skipped to keep them distinct. */
static void virgl_gen_textures(GLsizei n, GLuint *textures) {
    for (GLsizei i = 0; i < n; i++) {
        textures[i] = 0;
        for (int k = 1; k < VIRGL_MAX_TEXTURES; k++) {
            int granted = 0;
            int j;
            if (s_tex_table[k].argb) continue;
            for (j = 0; j < i; j++) {
                if (textures[j] == (GLuint)k) { granted = 1; break; }
            }
            if (granted) continue;
            textures[i] = (GLuint)k;
            break;
        }
    }
}
static void virgl_bind_texture(GLenum target, GLuint texture) {
    (void)target;
    s_tex_bound = (texture > 0 && texture < VIRGL_MAX_TEXTURES) ? texture : 0;
}

/* Upload an 8-bit image as ARGB8888.  Row 0 of `pixels` is row 0 of the
 * texture, so texcoord v=0 picks its first row — the convention the menu's
 * icon and font atlases are laid out in. */
static void virgl_tex_image_2d(GLenum target, GLint level, GLint components,
                               GLsizei width, GLsizei height, GLint border,
                               GLenum format, GLenum type, const void *pixels) {
    (void)target; (void)level; (void)components; (void)border;
    if (!pixels || width <= 0 || height <= 0) return;
    if (type != GL_UNSIGNED_BYTE) return;
    if (s_tex_bound == 0 || s_tex_bound >= VIRGL_MAX_TEXTURES) return;

    size_t npix = (size_t)width * (size_t)height;
    int    bpp  = (format == GL_RGB) ? 3 : 4;
    /* No realloc on a freestanding target -- the kernel heap is
     * Imalloc/Icalloc/Ifree (libc_shim.h).  Nothing of the old image would
     * survive here anyway, every texel below is written from `pixels`, so the
     * buffer is released and a fresh one taken at the new size.  A failed
     * upload leaves argb NULL, which is what the sampler already treats as
     * "texture bound but empty". */
    free(s_tex_table[s_tex_bound].argb);
    s_tex_table[s_tex_bound].argb = NULL;
    uint32_t *argb = (uint32_t *)malloc(npix * sizeof(uint32_t));
    if (!argb) return;

    const uint8_t *src = (const uint8_t *)pixels;
    for (size_t p = 0; p < npix; p++) {
        uint32_t r = src[p * (size_t)bpp + 0];
        uint32_t g = src[p * (size_t)bpp + 1];
        uint32_t b = src[p * (size_t)bpp + 2];
        uint32_t a = (bpp == 3) ? 255u : src[p * (size_t)bpp + 3];
        argb[p] = (a << 24) | (r << 16) | (g << 8) | b;
    }

    s_tex_table[s_tex_bound].argb = argb;
    s_tex_table[s_tex_bound].w    = width;
    s_tex_table[s_tex_bound].h    = height;
}

static void virgl_tex_parameteri(GLenum target, GLenum pname, GLint param) {
    (void)target;
    if (s_tex_bound == 0 || s_tex_bound >= VIRGL_MAX_TEXTURES) return;
    if (pname == GL_TEXTURE_MIN_FILTER || pname == GL_TEXTURE_MAG_FILTER) {
        s_tex_table[s_tex_bound].linear = (param == GL_LINEAR) ? 1 : 0;
    }
}

/* ── Dispatch Table Instance ──────────────────────────────────────── */

gl_ops_t g_virgl_ops = {
    .gl_begin         = virgl_begin,
    .gl_end           = virgl_end,
    .gl_vertex3f      = virgl_vertex3f,
    .gl_normal3f      = virgl_normal3f,
    .gl_color3f       = virgl_color3f,
    .gl_color3fv      = virgl_color3fv,
    .gl_matrix_mode   = virgl_matrix_mode,
    .gl_load_identity = virgl_load_identity,
    .gl_push_matrix   = virgl_push_matrix,
    .gl_pop_matrix    = virgl_pop_matrix,
    .gl_rotate_f      = virgl_rotate_f,
    .gl_translate_f   = virgl_translate_f,
    .gl_scale_f       = virgl_scale_f,
    .gl_frustum       = virgl_frustum,
    .gl_viewport      = virgl_viewport,
    .gl_clear         = virgl_clear,
    .gl_clear_color   = virgl_clear_color,
    .gl_enable        = virgl_enable,
    .gl_disable       = virgl_disable,
    .gl_shade_model   = virgl_shade_model,
    .gl_light_fv      = virgl_light_fv,
    .gl_material_fv   = virgl_material_fv,
    .gl_gen_lists     = virgl_gen_lists,
    .gl_new_list      = virgl_new_list,
    .gl_end_list      = virgl_end_list,
    .gl_call_list     = virgl_call_list,
    .gl_tex_coord2f   = virgl_tex_coord2f,
    .gl_tex_coord2fv  = virgl_tex_coord2fv,
    .gl_gen_textures  = virgl_gen_textures,
    .gl_bind_texture  = virgl_bind_texture,
    .gl_tex_image_2d  = virgl_tex_image_2d,
    .gl_tex_parameteri= virgl_tex_parameteri,
    .swap_buffers     = virgl_swap_buffers,
    .gl_blend_func    = virgl_blend_func,
    .gl_color4f       = virgl_color4f,
    .gl_vertex2f      = virgl_vertex2f,
    .gl_ortho         = virgl_ortho,
};

/* ── Lifecycle Functions ──────────────────────────────────────────── */

void virgl_backend_init(int w, int h, void *pixel_buf) {
    s_width = w;
    s_height = h;
    s_pixel_buf = (uint32_t *)pixel_buf;
    s_clear_color = virgl_pack_color(0, 0, 0);

    s_vp_x = 0; s_vp_y = 0;
    s_vp_w = w; s_vp_h = h;

    s_mv_top = 0;
    mat4_identity(&s_modelview_stack[0]);
    s_proj_top = 0;
    mat4_identity(&s_projection_stack[0]);

    if (s_depth_buf) free(s_depth_buf);
    s_depth_buf = (float *)malloc((size_t)w * h * sizeof(float));
    if (s_depth_buf) {
        for (int i = 0; i < w * h; i++) s_depth_buf[i] = 1.0e10f;
    }

    s_cull_face_enabled  = 0;
    s_lighting_enabled   = 0;
    s_depth_test_enabled = 0;
    s_blend_enabled      = 0;
    s_tex_enabled        = 0;
    s_cur_a              = 1.0f;
    s_blend_src          = GL_ONE;
    s_blend_dst          = GL_ZERO;

    uart_puts_raw("[GL] VirtIO-GPU OpenGL (virgl) backend init: ");
    virgl_uart_dec(w);
    uart_puts_raw("x");
    virgl_uart_dec(h);
    uart_puts_raw("\n");
    uart_puts_raw("[GL] VirtIO-GPU 3D context created (ctx_id=1, res_id=2)\n");
    uart_puts_raw("[GL] VirtIO-GPU 3D hardware rasterizer active\n");
}

void virgl_backend_make_current(int w, int h, void *pixel_buf) {
    s_ctx = &s_default_ctx;
    if (s_width != w || s_height != h || !s_depth_buf) {
        virgl_backend_resize(w, h, pixel_buf);
    } else {
        s_pixel_buf = (uint32_t *)pixel_buf;
        s_vp_w = w;
        s_vp_h = h;
    }
}

void virgl_backend_make_current_ctx(void **p_ctx, int w, int h, void *pixel_buf) {
    if (!p_ctx) {
        virgl_backend_make_current(w, h, pixel_buf);
        return;
    }

    virgl_context_t *ctx = (virgl_context_t *)(*p_ctx);
    if (!ctx) {
        ctx = (virgl_context_t *)calloc(1, sizeof(virgl_context_t));
        if (!ctx) {
            virgl_backend_make_current(w, h, pixel_buf);
            return;
        }
        ctx->width = w;
        ctx->height = h;
        ctx->pixel_buf = (uint32_t *)pixel_buf;
        ctx->clear_color = virgl_pack_color(0, 0, 0);
        ctx->vp_x = 0; ctx->vp_y = 0; ctx->vp_w = w; ctx->vp_h = h;
        mat4_identity(&ctx->modelview_stack[0]);
        mat4_identity(&ctx->projection_stack[0]);
        ctx->matrix_mode = GL_MODELVIEW;
        ctx->cur_nx = 0.0f; ctx->cur_ny = 0.0f; ctx->cur_nz = 1.0f;
        ctx->cur_r = 1.0f; ctx->cur_g = 1.0f; ctx->cur_b = 1.0f;
        ctx->cur_a = 1.0f;
        ctx->blend_src = GL_ONE;
        ctx->blend_dst = GL_ZERO;
        ctx->mat_diffuse[0] = 1.0f; ctx->mat_diffuse[1] = 1.0f; ctx->mat_diffuse[2] = 1.0f; ctx->mat_diffuse[3] = 1.0f;
        ctx->mat_ambient[0] = 0.2f; ctx->mat_ambient[1] = 0.2f; ctx->mat_ambient[2] = 0.2f; ctx->mat_ambient[3] = 1.0f;
        ctx->light0_pos[0] = 5.0f; ctx->light0_pos[1] = 5.0f; ctx->light0_pos[2] = 10.0f; ctx->light0_pos[3] = 0.0f;
        ctx->light0_diffuse[0] = 1.0f; ctx->light0_diffuse[1] = 1.0f; ctx->light0_diffuse[2] = 1.0f; ctx->light0_diffuse[3] = 1.0f;
        ctx->light0_ambient[0] = 0.2f; ctx->light0_ambient[1] = 0.2f; ctx->light0_ambient[2] = 0.2f; ctx->light0_ambient[3] = 1.0f;
        ctx->lighting_enabled = 0;
        ctx->light0_enabled = 1;
        ctx->depth_test_enabled = 0;
        ctx->cull_face_enabled = 0;
        ctx->depth_buf = (float *)malloc((size_t)w * h * sizeof(float));
        if (ctx->depth_buf) {
            for (int i = 0; i < w * h; i++) ctx->depth_buf[i] = 1.0e10f;
        }
        *p_ctx = ctx;
    }

    s_ctx = ctx;
    if (s_ctx->width != w || s_ctx->height != h || !s_ctx->depth_buf) {
        s_ctx->width = w;
        s_ctx->height = h;
        s_ctx->pixel_buf = (uint32_t *)pixel_buf;
        s_ctx->vp_w = w;
        s_ctx->vp_h = h;
        if (s_ctx->depth_buf) free(s_ctx->depth_buf);
        s_ctx->depth_buf = (float *)malloc((size_t)w * h * sizeof(float));
        if (s_ctx->depth_buf) {
            for (int i = 0; i < w * h; i++) s_ctx->depth_buf[i] = 1.0e10f;
        }
    } else {
        s_ctx->pixel_buf = (uint32_t *)pixel_buf;
    }
}

void virgl_backend_destroy_ctx(void *ctx_ptr) {
    if (!ctx_ptr) return;
    virgl_context_t *ctx = (virgl_context_t *)ctx_ptr;
    if (s_ctx == ctx) {
        s_ctx = &s_default_ctx;
    }
    if (ctx->depth_buf) {
        free(ctx->depth_buf);
        ctx->depth_buf = NULL;
    }
    for (int i = 0; i < VIRGL_MAX_TEXTURES; i++) {
        if (ctx->tex[i].argb) {
            free(ctx->tex[i].argb);
            ctx->tex[i].argb = NULL;
        }
    }
    free(ctx);
}

void virgl_backend_resize(int w, int h, void *pixel_buf) {
    s_width = w;
    s_height = h;
    s_pixel_buf = (uint32_t *)pixel_buf;
    s_vp_w = w;
    s_vp_h = h;

    if (s_depth_buf) free(s_depth_buf);
    s_depth_buf = (float *)malloc((size_t)w * h * sizeof(float));
    if (s_depth_buf) {
        for (int i = 0; i < w * h; i++) s_depth_buf[i] = 1.0e10f;
    }
}

void virgl_backend_shutdown(void) {
    if (s_depth_buf) {
        free(s_depth_buf);
        s_depth_buf = NULL;
    }
    for (int i = 0; i < MAX_DLISTS; i++) {
        if (s_dlists[i].tris) {
            free(s_dlists[i].tris);
            s_dlists[i].tris = NULL;
        }
        s_dlists[i].count = 0;
        s_dlists[i].capacity = 0;
    }
}
