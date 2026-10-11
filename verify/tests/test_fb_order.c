/*
 * verify/tests/test_fb_order.c -- the PS2's canvas byte order, proved twice
 *
 * The PS2's GS in CT32 mode reads memory as R,G,B,A while a BTRON COLOR is 0xAARRGGBB,
 * i.e. bytes B,G,R,A: the two differ by one involutive exchange of the R and B fields, and
 * no GS register can permute channels.  So the exchange lives in the canvas, and the rule
 * that keeps a canvas coherent is the one in include/btron/types.h -- a colour word ENTERS
 * a pixel array through BTRON_PIXEL_FB(), and a pixel array's word LEAVES for anything that
 * looks at R, G or B through the same macro.  TinyGL adds a second half to that rule,
 * because its pbuf IS the canvas: its whole PIXEL domain is declared in canvas order under
 * BTRON_PS2_TARGET (see src/gl/tinygl/zbuffer.h), so no span loop pays for the swap.
 *
 * Both halves are compile-time decisions, so the only way to check them is to build the
 * same scene twice.  This program renders one scene into three canvases and writes them out
 * normalised -- every word passed back through BTRON_PIXEL_FB(), which is the identity in a
 * hosted build and the exchange on the PS2 -- so:
 *
 *   build without -DBTRON_PS2_TARGET  ->  dump == the scene as COLOR order
 *   build with    -DBTRON_PS2_TARGET  ->  dump == the scene as COLOR order
 *
 * if and only if every store site converted itself and no macro was left behind.  A site
 * that forgot the conversion leaves its PS2 word unswapped, the normalising pass then swaps
 * it, and the two dumps differ at that word: `make test-ps2-fb-order` cmps them.
 *
 * The dumps comparing is not enough on its own -- if -DBTRON_PS2_TARGET never reached the
 * compile, both builds are in COLOR order and the comparison passes vacuously.  So two
 * raw, unnormalised words are also asserted against literals written out per domain: one
 * from fill_rec(COLOR_RED), which measures the store-side convention, and one from a canvas
 * that is nothing but glClearColor(1,0,0), which measures TinyGL's.  Those literals are
 * hand-written rather than derived from BTRON_PIXEL_FB() because deriving them would compare
 * the macro with itself.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <btron/types.h>
#include <btron/dp.h>
#include <btron/troncode.h>
#include <btron/nyan_bitmaps.h>
#include <GL/gl.h>

#include "../../src/gl/tinygl/zbuffer.h"

/* dp_core.c's stage timings ask the platform core for a counter; this link has no core. */
void uart_puts_raw(const char *s) { (void)s; }

#define FB_DP_W 160
#define FB_DP_H 120
#define FB_GL_W 96
#define FB_GL_H 64

static COLOR s_dp[FB_DP_W * FB_DP_H];
static COLOR s_gl[FB_GL_W * FB_GL_H];
static COLOR s_clr[FB_GL_W * FB_GL_H];

/* The cell the store-side control reads: inside the first fill_rec, in device coords. */
#define RED_CX 20
#define RED_CY 12

/* ── scene 1: everything that paints a canvas without GL ─────────────────── */

static void paint_dp(void)
{
    GDEV *dev = opn_dev_vram(FB_DP_W, FB_DP_H, s_dp);
    RECT r;

    if (!dev) {
        fprintf(stderr, "[FBORDER] no device\n");
        exit(1);
    }

    r.left = 4;   r.top = 4;  r.right = 40;  r.bottom = 24;  fill_rec(dev, &r, COLOR_RED);
    r.left = 44;  r.top = 4;  r.right = 80;  r.bottom = 24;  fill_rec(dev, &r, COLOR_YELLOW);
    r.left = 84;  r.top = 4;  r.right = 120; r.bottom = 24;  fill_rec(dev, &r, COLOR_GOLD);
    r.left = 124; r.top = 4;  r.right = 156; r.bottom = 24;  fill_rec(dev, &r, (COLOR)0xFF6633FFu);

    /* drw_lin takes its colour from the device, so set_col() is a store site too */
    set_col(dev, COLOR_CYAN, COLOR_GREEN);
    drw_lin(dev, 4, 30, 150, 52);
    set_col(dev, (COLOR)0xFF0099FFu, COLOR_RED);
    drw_lin(dev, 150, 30, 4, 52);

    drw_tc_string(dev, 6, 56, "B-System 3.20", COLOR_RED, COLOR_LTGRAY);
    drw_tc_string(dev, 6, 72, "orange and blue", (COLOR)0xFF6633FFu, (COLOR)0xFF0099FFu);
    drw_tc_string_underlined(dev, 6, 88, "underlined", COLOR_YELLOW, COLOR_NAVY, TRUE);

    /* The sprite is the visible test: its palette is strongly non-greyscale. */
    draw_nyan_sprite(dev, 4, 96, 3);

    cls_dev(dev);
}

/* ── scene 2: the rasterizer, whose pbuf is a canvas ─────────────────────── */

static void make_texture(unsigned char *tex)
{
    int x, y;
    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++) {
            unsigned char *p = tex + (y * 16 + x) * 4;
            p[0] = (unsigned char)(x * 17);        /* R: 0 at the left, 255 at the right */
            p[1] = (unsigned char)(255 - y * 17);  /* G: the other way round */
            p[2] = (unsigned char)((x ^ y) * 17);  /* B: neither, so a swap shows */
            p[3] = (unsigned char)(x * 17);        /* A: follows R on purpose */
        }
    }
}

/*
 * Both matrices stay identity, so a vertex is a clip-space coordinate and the viewport
 * alone turns it into pixels: no glOrtho, which <GL/gl.h> declares only inside the
 * commented-out block of functions TinyGL does not have.
 */
static void quad_tex(GLfloat x0, GLfloat y0, GLfloat x1, GLfloat y1)
{
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(x0, y0);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(x1, y0);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(x1, y1);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(x0, y1);
    glEnd();
}

static void quad_flat(GLfloat x0, GLfloat y0, GLfloat x1, GLfloat y1)
{
    glBegin(GL_QUADS);
    glVertex2f(x0, y0); glVertex2f(x1, y0); glVertex2f(x1, y1); glVertex2f(x0, y1);
    glEnd();
}

static void quad_grad(GLfloat x0, GLfloat y0, GLfloat x1, GLfloat y1)
{
    glBegin(GL_QUADS);
    glColor3f(0.0f, 0.0f, 1.0f); glVertex2f(x0, y0);
    glColor3f(1.0f, 1.0f, 0.0f); glVertex2f(x1, y0);
    glColor3f(0.0f, 1.0f, 0.0f); glVertex2f(x1, y1);
    glColor3f(1.0f, 0.0f, 1.0f); glVertex2f(x0, y1);
    glEnd();
}

static ZBuffer *s_zb;

static void gl_frame_begin(COLOR *cv)
{
    s_zb = ZB_open(FB_GL_W, FB_GL_H, ZB_MODE_RGBA, cv);
    if (!s_zb) {
        fprintf(stderr, "[FBORDER] ZB_open failed\n");
        exit(1);
    }
    glInit(s_zb);
    glViewport(0, 0, FB_GL_W, FB_GL_H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

/* The app owns the ZBuffer, so glClose() does not release it -- backend_tinygl.c
 * closes it after glClose() for exactly the same reason. */
static void gl_frame_end(void)
{
    glClose();
    ZB_close(s_zb);
    s_zb = NULL;
}

/* Nothing but a clear: the word ZB_clear/memset_l writes, over the whole canvas. */
static void paint_clear(void)
{
    gl_frame_begin(s_clr);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    gl_frame_end();
}

static void paint_gl(void)
{
    unsigned char tex[16 * 16 * 4];
    GLuint tid = 0;

    make_texture(tex);
    gl_frame_begin(s_gl);

    /* 1. clear: ZB_clear builds its word with RGB_TO_PIXEL */
    glClearColor(0.2f, 0.9f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* 2. flat and gradient fills, unblended: the interpolator's packing */
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glColor3f(1.0f, 0.0f, 0.0f);
    quad_flat(-1.0f, 0.5f, -0.4f, 1.0f);
    quad_grad(-0.3f, 0.5f, 0.25f, 1.0f);

    /* 3. the texel's birth: with blending off a fragment is the texel, unchanged, so this
     *    quad is image_util.c's conversion read straight back out of the canvas */
    glGenTextures(1, &tid);
    glBindTexture(GL_TEXTURE_2D, tid);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex);
    glEnable(GL_TEXTURE_2D);
    quad_tex(-1.0f, -1.0f, 0.0f, 0.3f);

    /* 4. the recognised blend: GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA / GL_FUNC_ADD, which
     *    is TGL_BLEND_ALPHA_ADD -- the one pack that names its fields */
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_BLEND);
    quad_tex(0.0f, -1.0f, 1.0f, 0.3f);

    /* 5. the general blend: DST_COLOR / ZERO is the ribbon's modulate, so it runs
     *    TGL_BLEND_FACTOR and RGB_TO_PIXEL instead of the fast path */
    glBlendFunc(GL_DST_COLOR, GL_ZERO);
    glColor3f(0.1f, 0.7f, 1.0f);
    quad_flat(0.3f, 0.4f, 1.0f, 0.48f);

    /* 6. interpolated vertex alpha over a textured destination */
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.9f, 0.2f, 0.1f, 0.5f);
    quad_flat(-0.2f, 0.32f, 0.2f, 0.46f);

    gl_frame_end();
}

/* ── dump: every word leaves the canvas through the convention ───────────── */

static unsigned long dump(FILE *f, const char *tag, const COLOR *cv, unsigned n)
{
    unsigned long sum = 0;
    unsigned i;

    for (i = 0; i < n; i++) {
        COLOR w = BTRON_PIXEL_FB(cv[i]);
        if (fwrite(&w, sizeof w, 1, f) != 1) {
            fprintf(stderr, "[FBORDER] short write on %s\n", tag);
            exit(1);
        }
        sum = (sum * 1000003u) ^ (unsigned long)w;
    }
    return sum;
}

static unsigned count_word(const COLOR *cv, unsigned n, COLOR w)
{
    unsigned i, hits = 0;
    for (i = 0; i < n; i++)
        if (cv[i] == w)
            hits++;
    return hits;
}

/*
 * How many different words a canvas holds.  This is the anti-vacuity measurement for the
 * GL half of the gate: if the quads missed the viewport, the canvas would be nothing but
 * the clear colour in one value, and the two dumps would compare equal while proving
 * nothing about the span loops, the texel conversion or either blend path.
 */
static unsigned count_distinct(const COLOR *cv, unsigned n)
{
    unsigned i, j, distinct = 0;

    for (i = 0; i < n; i++) {
        for (j = 0; j < i; j++)
            if (cv[j] == cv[i])
                break;
        if (j == i)
            distinct++;
    }
    return distinct;
}

static int failures;

static void expect(const char *what, int ok, const char *detail)
{
    fprintf(stderr, "[FBORDER] %s %s: %s\n", ok ? "ok  " : "FAIL", what, detail);
    if (!ok)
        failures++;
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : ".build/fb_order.u32";
    unsigned long dp_sum, gl_sum, clr_sum;
    unsigned gl_distinct, dp_distinct;
    char detail[128];
    FILE *f;

    memset(s_dp, 0, sizeof s_dp);
    memset(s_gl, 0, sizeof s_gl);
    memset(s_clr, 0, sizeof s_clr);
    paint_dp();
    paint_clear();
    paint_gl();

    /*
     * The positive control, and the only assertion here that says which domain compiled.
     * fill_rec(COLOR_RED) stores 0xFFFF0000 where the R field is at bit 16 and 0xFF0000FF
     * where it is in the low byte; a clear to glClearColor(1,0,0) is the same word without
     * the alpha byte, repeated over all FB_GL_W*FB_GL_H of the canvas.
     */
#if defined(BTRON_PS2_TARGET)
    expect("dp red cell", s_dp[RED_CY * FB_DP_W + RED_CX] == (COLOR)0xFF0000FFu,
           "canvas order (R in the low byte)");
    expect("clear canvas", count_word(s_clr, FB_GL_W * FB_GL_H, (COLOR)0x000000FFu) == FB_GL_W * FB_GL_H,
           "every word is TinyGL's red in canvas order");
#else
    expect("dp red cell", s_dp[RED_CY * FB_DP_W + RED_CX] == (COLOR)0xFFFF0000u,
           "COLOR order (R at bit 16)");
    expect("clear canvas", count_word(s_clr, FB_GL_W * FB_GL_H, (COLOR)0x00FF0000u) == FB_GL_W * FB_GL_H,
           "every word is TinyGL's red in COLOR order");
#endif

    f = fopen(out, "wb");
    if (!f) {
        fprintf(stderr, "[FBORDER] cannot open %s\n", out);
        return 2;
    }
    dp_sum  = dump(f, "dp",  s_dp,  FB_DP_W * FB_DP_H);
    clr_sum = dump(f, "clr", s_clr, FB_GL_W * FB_GL_H);
    gl_sum  = dump(f, "gl",  s_gl,  FB_GL_W * FB_GL_H);
    if (fclose(f) != 0) {
        fprintf(stderr, "[FBORDER] write error on %s\n", out);
        return 2;
    }

    snprintf(detail, sizeof detail, "domain=%s dp=%016lX clr=%016lX gl=%016lX",
#if defined(BTRON_PS2_TARGET)
             "canvas(PS2)",
#else
             "COLOR(host)",
#endif
             dp_sum, clr_sum, gl_sum);
    expect("dump", 1, detail);

    /* The GL canvas must be a picture, not a clear colour: see count_distinct(). */
    dp_distinct = count_distinct(s_dp, FB_DP_W * FB_DP_H);
    gl_distinct = count_distinct(s_gl, FB_GL_W * FB_GL_H);
    snprintf(detail, sizeof detail, "%u distinct words (floor 200)", gl_distinct);
    expect("gl scene drew", gl_distinct >= 200, detail);
    snprintf(detail, sizeof detail, "%u distinct words (floor 12)", dp_distinct);
    expect("dp scene drew", dp_distinct >= 12, detail);

    return failures ? 1 : 0;
}
