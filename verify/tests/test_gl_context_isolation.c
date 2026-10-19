/*
 * verify/tests/test_gl_context_isolation.c -- LANE 24's law: a second GL window must not
 * orphan the first window's objects.
 *
 * The defect this is a gate for is the one the operator described from a monitor: "After running
 * lilcu, then xmb, the Icons glitches (blue and black rects) disappeared", and "Icons show
 * initially fine but get corrupted after FPS ticker changed".  Both sentences are about the same
 * mechanism, and neither is about the present path or about VRAM:
 *
 *   src/gl/egl_surface.c's egl_create_window_surface() calls gl_init() for EVERY GL window it
 *   gives a surface, and gl_init() reached tinygl_backend_init(), which called glInit().  That
 *   begins `gl_ctx = empty_gl_ctx` (src/gl/tinygl/init.c:151) and then initSharedState() -- so the
 *   previous window's texture nodes, display lists and buffers are not deleted and not inherited,
 *   they are ORPHANED, and every name that window still holds now looks up an empty hash table.
 *   An empty slot is not an error to the sampler: it draws the vertex colour instead of the texel.
 *   One flat rectangle per icon and per glyph -- which is the picture, right down to the colours
 *   (0x0000ff where the focused item's vertex colour is the selection blue, black where it is the
 *   shadow, with the untextured art underneath still there).
 *
 *   The teardown half is the same fault: egl_destroy_surface() calls gl_shutdown() for whatever
 *   window it is destroying, and glClose() runs endSharedState(), which deletes every texture in
 *   the process.  So a transient surface closing -- a menu, an overlay, a helper window whose
 *   pixmap is first created when the FPS caption's tab repaint brings it on screen -- destroyed
 *   the launcher's atlas some frames after it looked fine.  That is "initially fine, then
 *   corrupted", and it is why opening the demo FIRST made the glitch go away: the last surface
 *   created owns the only live namespace.
 *
 * So the law is a refcount, and this suite drives it directly: two surfaces in, one texture and
 * one display list minted between them, and the objects must still resolve across the second init
 * and the first shutdown, and only die with the last one.  It also asserts the half that must
 * still CHANGE -- the raster follows the newest surface -- because a "fix" that kept the context by
 * ignoring the new window would draw the second window into the first one's pixmap.
 *
 * There are three guards in the shipped code and each has a control here, run and recorded on
 * 2026-10-18 rather than asserted: disable the join in tinygl_backend_init() and the window-B
 * rows go red, including `a new texture gets a DIFFERENT name than A's (1 vs 1)` -- the name
 * collision that is the flat rectangle; disable the count in tinygl_backend_shutdown() or let
 * gl_shutdown() clear `g_gl` on a non-final close, and the run faults on the survivor's next GL
 * call with the rows up to that point still on the log.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <GL/gl.h>

#include "gl_dispatch.h"
#include "../../src/gl/tinygl/zgl.h"

/* The two host seams every GL suite stubs.  uart_puts_raw() is captured rather than dropped
 * because the backend now announces the two events this suite is about, and an announcement that
 * nobody checks is a diagnostic that can rot: the join row must appear once per extra surface and
 * the keep row once per non-final close. */
static char  s_log[4096];
static int   s_log_n;
static int   s_n_join, s_n_keep;

void uart_puts_raw(const char *s)
{
    int i;
    for (i = 0; s[i] && s_log_n < (int)sizeof(s_log) - 1; i++) s_log[s_log_n++] = s[i];
    s_log[s_log_n] = '\0';
    if (strstr(s, "joins the one context"))      s_n_join++;
    if (strstr(s, "others still live"))          s_n_keep++;
}

unsigned btron_render_perf_us(void) { return 0u; }

static int s_fail;

#define CHECK(cond, fmt, ...)                                                  \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("[FAIL] " fmt "\n", ##__VA_ARGS__);                         \
            s_fail++;                                                          \
        } else {                                                               \
            printf("[ ok ] " fmt "\n", ##__VA_ARGS__);                         \
        }                                                                      \
    } while (0)

/* The two things a window holds a name for.  Walked here rather than through find_texture(),
 * which is file-static in texture.c: the hash table is public in zgl.h, and this is the same
 * lookup the sampler makes. */
static GLTexture *texture_of(GLuint handle)
{
    GLTexture *t = gl_ctx.shared_state.texture_hash_table
                       ? gl_ctx.shared_state.texture_hash_table[handle & TEXTURE_HASH_TABLE_MASK]
                       : NULL;

    while (t) {
        if (t->handle == (GLint)handle) return t;
        t = t->next;
    }
    return NULL;
}

static int texture_count(void)
{
    GLSharedState *s = &gl_ctx.shared_state;
    int n = 0, i;

    if (!s->texture_hash_table) return 0;
    for (i = 0; i < TEXTURE_HASH_TABLE_SIZE; i++)
        for (GLTexture *t = s->texture_hash_table[i]; t; t = t->next) n++;
    return n;
}

/* One window's worth of objects: an atlas-shaped texture with real texels, and one retained
 * drawing.  Both are what xb_paint() holds across frames, so both are what a wipe costs. */
static GLuint s_tex, s_list;
static unsigned char *s_atlas;

static void mint_window_objects(void)
{
    glGenTextures(1, &s_tex);
    glBindTexture(GL_TEXTURE_2D, (GLint)s_tex);
    /* 256x256 because that is the extent this TinyGL keeps -- see the note on
     * TEST_XMB_TGL_SRCS in the Makefile: anything else is silently truncated, and a truncated
     * upload would test the truncator rather than the namespace. */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, s_atlas);

    s_list = (GLuint)glGenLists(1);
    glNewList((GLint)s_list, GL_COMPILE);
    glBegin(GL_TRIANGLES);
    glColor3f(1.0f, 0.0f, 0.0f);
    glVertex3f(0.0f, 0.0f, 0.0f);
    glVertex3f(1.0f, 0.0f, 0.0f);
    glVertex3f(0.0f, 1.0f, 0.0f);
    glEnd();
    glEndList();
}

/* The texels behind a name, which is the thing the icon row actually needs. */
static int texture_has_texels(GLuint h)
{
    const GLTexture *t = texture_of(h);

    return t && t->images[0].pixmap != NULL &&
           t->images[0].xsize == 256 && t->images[0].ysize == 256;
}

static int list_alive(void)
{
    return gl_ctx.shared_state.lists &&
           gl_ctx.shared_state.lists[s_list & (MAX_DISPLAY_LISTS - 1)] != NULL;
}

/* ── the picture ────────────────────────────────────────────────────────────────────────────
 * Names resolving is not the symptom; a flat rectangle is.  So the suite draws one textured
 * quad the shape xb_paint() draws an icon with -- see verify/tests/test_xmb_render.c's
 * blit_texture(), the proven form on this raster -- into a window's own pixmap, and reads the
 * pixmap back.
 *
 * The atlas is a two-colour checkerboard and the vertex colour is the selection blue the
 * operator named, so the two answers are unmistakable and are not a colour permutation in
 * disguise: a live sampler gives a checkerboard, an orphaned name gives ONE FLAT BLUE
 * RECTANGLE -- the picture in Screenshot 2026-10-18 at 22.29.42.png, drawn on a host.
 *
 * Three frames are painted, all of them window A's, all of them expected to be identical
 * bytes: once while A is the only GL window, once after B opened (the event that used to
 * wipe the table), once after a surface closed and the survivor was re-bound.  Byte-equality
 * is the law stated as a picture, and it is permutation-independent -- the assertion does
 * not need to know whether a pixel is 0xRRGGBB or 0xAABBGGRR to know that a second window
 * must not change what the first one draws. */
#define PAINT_W 64
#define PAINT_H 64

static void atlas_checkerboard(void)
{
    int x, y;

    for (y = 0; y < 256; y++) {
        for (x = 0; x < 256; x++) {
            /* Alternating 32x32 cells, one warm and one cool, both far from blue: an
             * 8x8 cell would let a 64x64 viewport alias onto one colour and a flat
             * canvas would pass as a textured one. */
            int on = (((x / 32) ^ (y / 32)) & 1) != 0;
            unsigned char *p = &s_atlas[(y * 256 + x) * 4];

            p[0] = on ? 0xe8 : 0x20;
            p[1] = on ? 0x30 : 0xd0;
            p[2] = on ? 0x30 : 0x20;
            p[3] = 0xff;
        }
    }
}

/* One icon-sized textured quad, blue vertex colour, into whatever pixmap the raster holds. */
static void paint_textured_quad(int w, int h)
{
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (double)w, (double)h, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, (GLint)s_tex);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(0.0f, 0.0f, 1.0f);            /* the reported flat colour */
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 0.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f((float)w, 0.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f((float)w, (float)h);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, (float)h);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

/* How many different words the pixmap holds, counted for real rather than answered as "uniform
 * or not": a canvas that sampled one texel repeatedly would pass a two-way test and is not a
 * textured icon.  Returns the number of distinct words (capped at the table size) and, through
 * n_of_modal, how many pixels carry the commonest one. */
static int distinct_words(const PIXEL *buf, int w, int h, long *n_of_modal)
{
#define ISO_WORDS 16
    static PIXEL seen[ISO_WORDS];
    static long  cnt[ISO_WORDS];
    int n = 0, i, j, modal_i = 0;

    for (i = 0; i < w * h; i++) {
        for (j = 0; j < n; j++)
            if (seen[j] == buf[i]) { cnt[j]++; break; }
        if (j == n && n < ISO_WORDS) { seen[n] = buf[i]; cnt[n] = 1; n++; }
    }
    for (j = 1; j < n; j++)
        if (cnt[j] > cnt[modal_i]) modal_i = j;
    *n_of_modal = n ? cnt[modal_i] : 0;
    return n;
}

static int frames_equal(const PIXEL *a, const PIXEL *b, int n, int *first_diff)
{
    int i;

    for (i = 0; i < n; i++)
        if (a[i] != b[i]) { *first_diff = i; return 0; }
    *first_diff = -1;
    return 1;
}

/* P6, the same channel order test_xmb_render.c's dumps use, so these PNGs sit beside the
 * XMB's and mean the same thing when opened. */
static void dump_ppm(const char *path, const PIXEL *buf, int w, int h)
{
    FILE *f = fopen(path, "wb");
    int x, y;

    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            unsigned c = (unsigned)buf[y * w + x];
            fputc((c >> 16) & 0xFF, f);
            fputc((c >> 8) & 0xFF, f);
            fputc(c & 0xFF, f);
        }
    fclose(f);
}

static PIXEL s_frame0[PAINT_W * PAINT_H];   /* A alone: the picture to be reproduced */

int main(void)
{
    /* Two windows' pixel arrays, both big enough for the sizes asked below.  PIXEL rather than
     * bytes because that is what a ZBuffer's pbuf is, and the identity of the pixmap is half of
     * what these assertions read. */
    static PIXEL bufA[64 * 64];
    static PIXEL bufB[32 * 32];
    int live0;

    /* Unbuffered, because the failure this suite exists to catch can also be a crash: with the
     * pre-fix dispatch (`g_gl = NULL` on any close) the run dies on the survivor's next GL call,
     * and a block-buffered stdout then loses every row already printed -- the gate reports a
     * segfault and no diagnosis, which is the thing that made the LANE 24 controls so expensive.
     * Every row printed before the fault survives in the log if the log is flushed as it is
     * written. */
    setbuf(stdout, NULL);

    s_atlas = (unsigned char *)malloc(256 * 256 * 4);
    if (!s_atlas) { printf("[FAIL] no memory for the atlas\n"); return 1; }
    atlas_checkerboard();

    /* ── window A opens, bakes its atlas, and is the only GL window in the session ───────── */
    gl_init(GL_BACKEND_TINYGL, 64, 64, bufA);
    CHECK(gl_active_backend() == GL_BACKEND_TINYGL, "window A: the TinyGL pipeline is installed");
    CHECK(gl_ctx.zb && gl_ctx.zb->pbuf == bufA, "window A: the raster draws into A's pixmap");

    mint_window_objects();
    CHECK(texture_of(s_tex) != NULL, "window A: its texture name resolves (handle %u)", s_tex);
    CHECK(texture_has_texels(s_tex), "window A: the texture carries its texels");
    CHECK(list_alive(), "window A: its display list is retained (list %u)", s_list);
    live0 = texture_count();

    /* The reference picture, drawn while nothing else could interfere.  Everything after this
     * is judged against these bytes, so the gate asks a question about the screen rather than
     * about a hash table. */
    paint_textured_quad(PAINT_W, PAINT_H);
    {
        long modal = 0;
        int  nd = distinct_words(bufA, PAINT_W, PAINT_H, &modal);

        CHECK(nd > 1, "window A: the quad is TEXTURED, not flat -- %d distinct words, "
                      "modal colour %ld of %d px", nd, modal, PAINT_W * PAINT_H);
        memcpy(s_frame0, bufA, sizeof s_frame0);
        dump_ppm(".build/gl_iso_A_alone.ppm", bufA, PAINT_W, PAINT_H);
    }

    /* ── window B opens.  This is the event: the old code re-ran glInit() here. ──────────── */
    gl_init(GL_BACKEND_TINYGL, 32, 32, bufB);
    CHECK(texture_count() == live0,
          "window B: the shared context kept every texture (was %d, still %d)",
          live0, texture_count());
    CHECK(texture_of(s_tex) != NULL,
          "window B: A's texture name STILL resolves -- a lost name is the flat rectangle");
    CHECK(texture_has_texels(s_tex), "window B: A's texels are still behind it");
    CHECK(list_alive(), "window B: A's display list is still retained");
    CHECK(gl_ctx.zb && gl_ctx.zb->pbuf == bufB,
          "window B: but the raster DID move to B's pixmap -- keeping the context is not "
          "ignoring the new window");
    CHECK(gl_ctx.viewport.xsize == 32 && gl_ctx.viewport.ysize == 32,
          "window B: the viewport follows the window that just became current (%d x %d)",
          (int)gl_ctx.viewport.xsize, (int)gl_ctx.viewport.ysize);

    /* A second mint must not collide with the first: glGenTextures answers max(handle)+1 over the
     * LIVE table, which is only a safe rule now that nothing wipes the table. */
    {
        GLuint other = 0;
        glGenTextures(1, &other);
        CHECK(other != s_tex && other != 0u,
              "window B: a new texture gets a DIFFERENT name than A's (%u vs %u)", other, s_tex);
        glBindTexture(GL_TEXTURE_2D, (GLint)other);
        CHECK(texture_of(other) != NULL && texture_of(s_tex) != NULL,
              "window B: both windows' textures are live at once");
    }

    /* A repaints, through the seam the compositor uses: egl_make_current() on this pipeline IS
     * gl_resize() (src/gl/egl_surface.c:214-221), so binding A again is the same call the
     * desktop makes before xb_paint().  This is the frame the operator watched go flat. */
    gl_resize(PAINT_W, PAINT_H, bufA);
    paint_textured_quad(PAINT_W, PAINT_H);
    {
        long modal = 0;
        int  nd = distinct_words(bufA, PAINT_W, PAINT_H, &modal);
        int  diff = -1;

        CHECK(nd > 1, "window B open, A repainting: still TEXTURED (%d distinct words, "
                      "modal colour %ld of %d px)", nd, modal, PAINT_W * PAINT_H);
        CHECK(frames_equal(s_frame0, bufA, PAINT_W * PAINT_H, &diff),
              "window B open, A repainting: the picture is BYTE-IDENTICAL to A alone "
              "(first difference at px %d)", diff);
        dump_ppm(".build/gl_iso_A_with_B.ppm", bufA, PAINT_W, PAINT_H);
    }

    /* ── one window closes.  The other is still on the screen. ───────────────────────────── */
    gl_shutdown();
    CHECK(texture_count() == live0 + 1,
          "one surface closed with another live: nothing was deleted (%d nodes)",
          texture_count());
    CHECK(texture_of(s_tex) != NULL, "one surface closed: A's texture still resolves");
    CHECK(list_alive(), "one surface closed: A's display list still retained");
    CHECK(gl_ctx.zb && gl_ctx.zb->pbuf == NULL,
          "one surface closed: the raster is DETACHED, not left pointing at a freed pixmap");

    /* And the survivor still paints: this is the frame that used to come out flat.  The close
     * above detached the raster, so the survivor's bind is what re-attaches it -- and the
     * picture must come back the same one, from a context no surface now owns but the last. */
    glBindTexture(GL_TEXTURE_2D, (GLint)s_tex);
    CHECK(texture_of(s_tex) != NULL, "the survivor re-binds A's texture without losing it");
    gl_resize(PAINT_W, PAINT_H, bufA);
    paint_textured_quad(PAINT_W, PAINT_H);
    {
        long modal = 0;
        int  nd = distinct_words(bufA, PAINT_W, PAINT_H, &modal);
        int  diff = -1;

        CHECK(nd > 1, "after a surface closed, A repainting: still TEXTURED (%d distinct "
                      "words, modal colour %ld of %d px)", nd, modal, PAINT_W * PAINT_H);
        CHECK(frames_equal(s_frame0, bufA, PAINT_W * PAINT_H, &diff),
              "after a surface closed, A repainting: the picture is BYTE-IDENTICAL to A alone "
              "(first difference at px %d)", diff);
        dump_ppm(".build/gl_iso_A_after_close.ppm", bufA, PAINT_W, PAINT_H);
    }

    /* ── the last window closes: now, and only now, the context goes. ───────────────────── */
    gl_shutdown();
    CHECK(texture_of(s_tex) == NULL,
          "last surface closed: the context is torn down and its names die with it (%d nodes)",
          texture_count());

    /* Re-opening after a full close must give a working pipeline again -- the refcount may not
     * strand the process with a dead context. */
    gl_init(GL_BACKEND_TINYGL, 64, 64, bufA);
    CHECK(gl_ctx.zb && gl_ctx.zb->pbuf == bufA, "a session after a full close installs again");
    CHECK(s_n_join == 1, "the join was announced once, not silently and not twice (%d rows)", s_n_join);
    CHECK(s_n_keep == 1, "the non-final close was announced once (%d rows)", s_n_keep);
    mint_window_objects();
    CHECK(texture_of(s_tex) != NULL, "and its first window can mint a texture again");
    paint_textured_quad(PAINT_W, PAINT_H);
    {
        int diff = -1;

        CHECK(frames_equal(s_frame0, bufA, PAINT_W * PAINT_H, &diff),
              "a session after a full close draws the SAME picture the first session drew "
              "(first difference at px %d)", diff);
        dump_ppm(".build/gl_iso_A_second_session.ppm", bufA, PAINT_W, PAINT_H);
    }
    gl_shutdown();

    free(s_atlas);

    if (s_fail) {
        printf("[FAIL] %d assertion(s) failed: GL window surfaces share one context, "
               "and that context must outlive every surface but the last\n", s_fail);
        return 1;
    }
    printf("[ ok ] all: one context, N surfaces -- no orphaned names, no stranded raster\n");
    printf("   wrote .build/gl_iso_A_alone.ppm, _with_B.ppm, _after_close.ppm and "
           "_second_session.ppm -- all four are the same 64x64 checkerboard, and a flat blue "
           "square in any of them is the operator's screenshot\n");
    return 0;
}
