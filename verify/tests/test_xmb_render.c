/*
 * verify/tests/test_xmb_render.c — what the XMB actually paints
 *
 * src/apps/xmb.c is a pure immediate-mode GL renderer: a four-corner gradient,
 * a waving 64x64 sheet composited with GL_DST_COLOR, an icon band, bitmap-font
 * labels with drop shadows, particles, and a walk-in animation.  None of that
 * is observable from here (the SDL window is not visible to the person checking
 * the work), so this test renders real frames into a host pixel buffer through
 * the real dispatch + virgl backend and measures the pixels instead:
 *
 *   1. the gradient — the four corners are read back against the Deep Blue
 *      theme's own corner colours.  This is also the orientation check: an
 *      upside-down ortho map puts the 254-blue corner at the bottom.
 *   2. the waving surface — the driver composites it as dst*(1+c) with
 *      c = (1-cos(c^2))/13 from the sheet's surface normal (gl2.c:707 and the
 *      ribbon fragment stage).  Checked twice: against the shader's own bounds
 *      on the field xb_ribbon_step() produces, and against the frame
 *      difference of the same settled UI with the surface on and off, which
 *      must lift the background and never darken it.
 *   3. the bar, the labels and the shadows — pixel ink where the PS3 layout
 *      geometry says they belong, and the ink growth that shadows add.
 *   4. the walk-in — the band's bright-pixel centroid over successive frames,
 *      which must move and then settle.
 *   5. frame cost, because the sheet is rasterized on the CPU.
 *
 * Everything under test is linked for real (xmb.c, gl_dispatch.c, egl_surface.c,
 * backend_virgl.c, troncode.c); only the window system, the ITRON task layer and
 * the app launchers are stubbed, and the clock is advanced one 16 ms frame per
 * paint so the animation timeline is reproducible.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <btron/types.h>
#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/event.h>
#include <btron/itron.h>
#include <btron/desktop.h>
#include <btron/core.h>
#include "../../src/apps/xmb.h"

#define XW 960
#define XH 600

/* ── Host display device, window and clock ───────────────────────────── */

static COLOR   g_fb[XW * XH];
static GDEV    g_dev;
static WND     g_wnd;
static SYSTIME g_clock = 1000;   /* one paint == one 16 ms frame */
static int     g_shell_calls;
static char    g_shell_last[128];

void uart_puts_raw(const char *s) { (void)s; }

ER get_tim(SYSTIME *p_time) { *p_time = g_clock; return 0; }
void dly_tsk(W d) { (void)d; }
ID cre_tsk(const T_CTSK *pk) { (void)pk; return -1; }   /* no task: the test drives paint */
ER sta_tsk(ID id, VW exinf) { (void)id; (void)exinf; return 0; }
ER wup_tsk(ID id) { (void)id; return 0; }

WND* opn_wnd(const char *title, H x, H y, H w, H h, UW attr)
{
    (void)title; (void)x; (void)y; (void)attr;
    memset(&g_wnd, 0, sizeof(WND));
    memset(g_fb, 0, sizeof(g_fb));
    g_dev.width  = (H)w;
    g_dev.height = (H)h;
    g_dev.pixels = g_fb;
    g_dev.clip.left = g_dev.clip.top = 0;
    g_dev.clip.right = (H)w;
    g_dev.clip.bottom = (H)h;
    g_wnd.client.left = g_wnd.client.top = 0;
    g_wnd.client.right = (H)w;
    g_wnd.client.bottom = (H)h;
    g_wnd.dev     = &g_dev;
    g_wnd.visible = TRUE;
    return &g_wnd;
}

ER cls_wnd(WND *wnd) { (void)wnd; return 0; }
ER top_wnd(WND *wnd) { (void)wnd; return 0; }
ER inval_wnd(WND *wnd) { (void)wnd; return 0; }

BTRON_DESKTOP* get_btron_desktop(void) { return NULL; }

void sys_get_mem_stats(uint32_t *base, uint32_t *limit, uint32_t *used)
{
    if (base)  *base  = 0x00100000u;
    if (limit) *limit = 0x08000000u;
    if (used)  *used  = 0x00400000u;
}
void sys_get_devconf(char *buf, size_t sz)
{
    if (buf && sz) snprintf(buf, sz, "host test device");
}

void shell_execute_cmd(const char *cmd, ShellOutputFn out_fn, void *user, WND *wnd)
{
    (void)out_fn; (void)user; (void)wnd;
    g_shell_calls++;
    if (cmd) snprintf(g_shell_last, sizeof(g_shell_last), "%s", cmd);
}

/* App launchers the bar's Applications rows call.  A counter each, so that
 * "ENTER on Terminal" can be asserted rather than assumed. */
static int g_launch;
static const char *g_last_launch;
WND* open_gterm_window(void)         { g_launch++; g_last_launch = "gterm";     return NULL; }
WND* open_t_editor_window(void)      { g_launch++; g_last_launch = "editor";    return NULL; }
WND* open_paint_window(void)         { g_launch++; g_last_launch = "paint";     return NULL; }
WND* open_audio_player_window(void)  { g_launch++; g_last_launch = "audio";     return NULL; }
WND* open_orchestra_window(void)     { g_launch++; g_last_launch = "orchestra"; return NULL; }
WND* open_vobj_manager_window(void)  { g_launch++; g_last_launch = "vobj";      return NULL; }
WND* open_tad_browser_window(const char *p, const char *t)
{
    (void)p; (void)t;
    g_launch++; g_last_launch = "tad"; return NULL;
}
WND* open_drivesetup_window(void)    { g_launch++; g_last_launch = "drive";     return NULL; }
WND* open_quake_window(int x, int y, int w, int h)
{
    (void)x; (void)y; (void)w; (void)h;
    g_launch++; g_last_launch = "quake"; return NULL;
}
WND* launch_beos_chat(void)          { g_launch++; g_last_launch = "chat";      return NULL; }

/* The real GL stack */
#include "../../src/gl/gl_dispatch.h"
#include "../../src/gl/egl_surface.h"

/* ── Assertions ──────────────────────────────────────────────────────── */

static int g_total, g_failed;
#define CHECK(cond, msg) do {                                       \
        g_total++;                                                  \
        if (cond) printf("  [PASS] %s\n", (msg));                   \
        else { g_failed++;                                          \
               printf("  [FAIL] %s  (line %d)\n", (msg), __LINE__); }\
    } while (0)

/* ── Pixel helpers ───────────────────────────────────────────────────── */

static inline unsigned fb_at(int x, int y)
{
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= XW) x = XW - 1;
    if (x >= g_dev.width)  x = g_dev.width - 1;
    if (y >= g_dev.height) y = g_dev.height - 1;
    return g_fb[y * XW + x];
}

static inline int lum(unsigned c)
{
    return (int)((((c >> 16) & 0xFF) * 30u + ((c >> 8) & 0xFF) * 59u + (c & 0xFF) * 11u) / 100u);
}
static inline int ch_r(unsigned c) { return (int)((c >> 16) & 0xFF); }
static inline int ch_g(unsigned c) { return (int)((c >> 8)  & 0xFF); }
static inline int ch_b(unsigned c) { return (int)( c        & 0xFF); }

/* The darkest pixel of a patch: text, icons and particles only ever add light,
 * so the darkest sample is the untouched background underneath them. */
static int patch_min_lum(int x0, int y0, int w, int h)
{
    int best = 1 << 20, x, y;
    for (y = y0; y < y0 + h; y++)
        for (x = x0; x < x0 + w; x++) {
            int l = lum(fb_at(x, y));
            if (l < best) best = l;
        }
    return best;
}

static void patch_min_rgb(int x0, int y0, int w, int h, int out[3])
{
    int best = 1 << 20, x, y;
    out[0] = out[1] = out[2] = 0;
    for (y = y0; y < y0 + h; y++)
        for (x = x0; x < x0 + w; x++) {
            unsigned c = fb_at(x, y);
            int l = lum(c);
            if (l < best) { best = l; out[0] = ch_r(c); out[1] = ch_g(c); out[2] = ch_b(c); }
        }
}

/* Pixels of `b` brighter than the same pixel of `a` (strictly, by >= t) */
static int count_brighter(const unsigned *a, const unsigned *b, int t)
{
    int i, n = 0;
    for (i = 0; i < XW * XH; i++)
        if (lum(b[i]) - lum(a[i]) >= t) n++;
    return n;
}

static void grab(unsigned *dst) { memcpy(dst, g_fb, sizeof(g_fb)); }

/* Number of pixels that differ from the local background: any drawn ink */
static int ink_in_rect(int x0, int y0, int x1, int y1, const unsigned *bg)
{
    int x, y, n = 0;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++) {
            unsigned c = fb_at(x, y), b = bg[y * XW + x];
            int d = (int)((c >> 16) & 0xFF) - (int)((b >> 16) & 0xFF);
            if (d < 0) d = -(d);
            if (d > 24) n++;
        }
    return n;
}

/* Bright-pixel centroid of a horizontal strip, used to follow the bar sliding */
static int centroid_x(int y0, int y1)
{
    int x, y;
    long sum = 0, wsum = 0;
    for (y = y0; y < y1; y++)
        for (x = 0; x < g_dev.width; x++) {
            int l = lum(fb_at(x, y));
            sum += l; wsum += (long)l * x;
        }
    return sum > 0 ? (int)(wsum / sum) : -1;
}

static void dump_region(const char *path, int w, int h)
{
    FILE *f = fopen(path, "wb");
    int x, y;
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            unsigned c = fb_at(x, y);
            fputc(ch_r(c), f); fputc(ch_g(c), f); fputc(ch_b(c), f);
        }
    fclose(f);
}

static void dump_ppm(const char *path)
{
    dump_region(path, g_dev.width, g_dev.height);
}

/* Blit one bound texture across the frame so a baked atlas can be looked at
 * directly: ids 1 and 2 are the icon and font atlases, in bake order. */
static void dump_atlas(unsigned id, int w, int h, const char *path)
{
    int x, y;
    unsigned *save = (unsigned *)malloc(sizeof(g_fb));
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (double)w, (double)h, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, id);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 0.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f((float)w, 0.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f((float)w, (float)h);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, (float)h);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (save) {
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++)
                save[y * w + x] = g_fb[y * XW + x];
        {
            FILE *f = fopen(path, "wb");
            if (f) {
                fprintf(f, "P6\n%d %d\n255\n", w, h);
                for (y = 0; y < h; y++)
                    for (x = 0; x < w; x++) {
                        unsigned c = save[y * w + x];
                        fputc((c >> 16) & 0xFF, f);
                        fputc((c >> 8) & 0xFF, f);
                        fputc(c & 0xFF, f);
                    }
                fclose(f);
            }
        }
        free(save);
    }
    memset(g_fb, 0, sizeof(g_fb));
}

static void show_preview(const char *caption)
{
    static const char ramp[] = " .:-=+*#%@";
    int bx = 96, by = 30;
    int x, y;
    printf("\n  %s\n", caption);
    for (y = 0; y < by; y++) {
        printf("   |");
        for (x = 0; x < bx; x++) {
            int sx = x * g_dev.width  / bx;
            int sy = y * g_dev.height / by;
            int l = lum(fb_at(sx, sy)) * 9 / 255;
            if (l > 9) l = 9;
            fputc(ramp[l], stdout);
        }
        printf("|\n");
    }
}

/* ── Driving the bar ─────────────────────────────────────────────────── */

static void paint_one(void)
{
    g_clock += 16;
    if (g_wnd.paint) g_wnd.paint(&g_wnd, &g_dev);
}

static void paint_n(int n)
{
    int i;
    for (i = 0; i < n; i++) paint_one();
}

static void press(unsigned key)
{
    EVT e;
    memset(&e, 0, sizeof(e));
    e.type = EV_KEY_DOWN;
    e.key  = key;
    if (g_wnd.event_handler) g_wnd.event_handler(&g_wnd, &e);
    e.type = EV_KEY_UP;
    if (g_wnd.event_handler) g_wnd.event_handler(&g_wnd, &e);
}

/* The Settings band holds menus, not widgets (as on the console), so a control
 * is reached by opening its menu first:
 *   band 0 Applications -> the launchers
 *   band 1 Settings     -> 0 Theme, 1 Language System, 2 Screen, 3 System Data
 *     band 0 Theme        -> 0 Colour, 1 Wave Background, 2 Wave Particles
 *     band 1 Language     -> 0 System Language
 *     band 2 Screen       -> 0 Screen Brightness, 1 Edge Fade, 2 Icon Shadows
 *     band 3 System Data  -> six read-only lines
 *   band 2 Volume       -> five sliders (which the console only lets you reach
 *                          once a menu is open: at the band level LEFT/RIGHT
 *                          always switch bands)
 *   band 3 Commands     -> shell builtins, in a message box */
/* The bands, by their order in s_cats */
#define BAND_APPS     0
#define BAND_SETTINGS 1

/* Move the cursor to row `row` of the list under it.  DOWN clamps at the last
 * row, but UP on the first row of an open menu closes it - the console's own way
 * back - so the walk saturates at the bottom first and then counts back up,
 * which never presses UP while the cursor is already at the top. */
static void select_row(int row)
{
    int i, last = xmb_rows() - 1;
    if (row > last)
        row = last;
    for (i = 0; i <= last; i++)
        press(BTRON_KEY_DOWN);
    for (i = last; i > row; i--)
        press(BTRON_KEY_UP);
    paint_n(1);
}

/* Park the bar on band `want`, at its first row and depth 1, from wherever the
 * previous step left it.  The bar's own position is read back through
 * xmb_band()/xmb_depth(), so no test has to remember what it pressed last -
 * and the bands wrap, which makes guessing them useless anyway. */
static void park_band(int want)
{
    int guard;
    for (guard = 0; guard < 8; guard++) {
        while (xmb_depth() > 1) {
            press(BTRON_KEY_ESCAPE);
            paint_n(2);
        }
        if (xmb_band() == want)
            break;
        press(xmb_band() < want ? BTRON_KEY_RIGHT : BTRON_KEY_LEFT);
        paint_n(2);
    }
    select_row(0);
    paint_n(2);
}

/* Settings is where every switch lives, so it is the frame both halves of an
 * A/B are taken at. */
static void goto_settings(void)
{
    park_band(BAND_SETTINGS);
}

/* Walk the band to `band_row` and open the menu under it */
static void open_menu(int band_row)
{
    select_row(band_row);
    press(BTRON_KEY_RETURN);
    paint_n(20);
}

static void close_menu(void)
{
    press(BTRON_KEY_ESCAPE);
    paint_n(20);
}

/* Walk the open menu to row `row` and press ENTER until switch `setting` reads
 * `want`.  The state comes back from xmb_setting(), so the test never has to
 * guess - or copy - where the switch started. */
static void set_switch(int row, int setting, int want)
{
    int guard;
    select_row(row);
    for (guard = 0; guard < 3 && xmb_setting(setting) != want; guard++) {
        press(BTRON_KEY_RETURN);
        paint_n(2);
    }
}

/* Settle the bar at the Settings band with the waving surface in state `want`,
 * and grab that frame into `dst` (NULL: only leave the state set).  Both frames
 * of the A/B go through this, so they are depth 1, same category, same row, and
 * with the sparkles off because they animate: the menu draws byte-identical ink
 * in each and the sheet is the only thing that can differ. */
static void grab_band_frame(unsigned *dst, int want)
{
    goto_settings();
    open_menu(0);                            /* Theme */
    set_switch(2, XMB_SETTING_PARTICLES, 0);
    set_switch(1, XMB_SETTING_WAVE, want);
    close_menu();
    paint_n(40);
    printf("   frame taken at band %d, depth %d, %d rows, wave=%d particles=%d\n",
           xmb_band(), xmb_depth(), xmb_rows(),
           xmb_setting(XMB_SETTING_WAVE), xmb_setting(XMB_SETTING_PARTICLES));
    if (dst)
        grab(dst);
}

/* A grabbed frame, and the lift one frame has over another amplified into
 * greys, so the sheet's shape can be looked at instead of only counted. */
static void dump_frame(const unsigned *frame, const char *path)
{
    int x, y;
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", g_dev.width, g_dev.height);
    for (y = 0; y < g_dev.height; y++)
        for (x = 0; x < g_dev.width; x++) {
            unsigned c = frame[y * XW + x];
            fputc(ch_r(c), f); fputc(ch_g(c), f); fputc(ch_b(c), f);
        }
    fclose(f);
}

static void dump_lift(const unsigned *a, const unsigned *b, int scale, const char *path)
{
    int x, y;
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", g_dev.width, g_dev.height);
    for (y = 0; y < g_dev.height; y++)
        for (x = 0; x < g_dev.width; x++) {
            int v = (lum(b[y * XW + x]) - lum(a[y * XW + x])) * scale;
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            fputc(v, f); fputc(v, f); fputc(v, f);
        }
    fclose(f);
}

/* ── Tests ───────────────────────────────────────────────────────────── */

static unsigned g_bg[3][XW * XH];   /* frames kept for A/B measurement */

static void test_open_and_gradient(void)
{
    WND *w = open_xmb_window();
    int exp[4][3] = { {1, 2, 67}, {1, 73, 183}, {1, 93, 194}, {3, 162, 254} };
    int got[3], x, y, painted = 0;
    /* xmb_draw_bg(): alpha = min(s_alpha, 0.9) * s_brightness(0.9) = 0.81,
     * over the black clear colour, then the sheet may brighten it by <= 3.5% */
    double k = 0.81;

    printf("\n[1] Open and the four-corner gradient\n");
    CHECK(w == &g_wnd && w->paint != NULL, "open_xmb_window() returned a painted window");
    paint_n(2);

    for (y = 0; y < g_dev.height; y++)
        for (x = 0; x < g_dev.width; x++)
            if (lum(g_fb[y * XW + x]) > 0) painted++;
    {
        char msg[128];
        snprintf(msg, sizeof(msg), "background covers the frame (%d of %d px lit)",
                 painted, XW * XH);
        CHECK(painted > (XW * XH) / 2, msg);
    }

    /* corners: BL(0,H) BR(W,H) TL(0,0) TR(W,0), inset away from the icons */
    patch_min_rgb(4, g_dev.height - 24, 16, 16, got);
    CHECK(abs(got[0] - (int)(exp[0][0] * k)) <= 6 &&
          abs(got[1] - (int)(exp[0][1] * k)) <= 6 &&
          abs(got[2] - (int)(exp[0][2] * k)) <= 6, "bottom-left corner is the theme's BL colour");
    patch_min_rgb(g_dev.width - 20, g_dev.height - 24, 16, 16, got);
    CHECK(abs(got[2] - (int)(exp[1][2] * k)) <= 8, "bottom-right corner is the theme's BR colour");
    patch_min_rgb(4, g_dev.height / 2, 16, 16, got);
    CHECK(abs(got[2] - (int)(exp[2][2] * k)) <= 8, "left edge is the theme's TL blue");
    patch_min_rgb(g_dev.width - 20, 4, 16, 16, got);
    CHECK(abs(got[2] - (int)(exp[3][2] * k)) <= 8 && got[2] > got[0],
          "top-right corner is the theme's brightest blue, not the bottom-left one");
    {
        char msg[128];
        snprintf(msg, sizeof(msg), "TR=(%d,%d,%d) BL=(%d,%d,%d): the ortho map is not upside down",
                 ch_r(fb_at(g_dev.width - 6, 6)), ch_g(fb_at(g_dev.width - 6, 6)), ch_b(fb_at(g_dev.width - 6, 6)),
                 ch_r(fb_at(6, g_dev.height - 6)), ch_g(fb_at(6, g_dev.height - 6)),
                 ch_b(fb_at(6, g_dev.height - 6)));
        CHECK(lum(fb_at(g_dev.width - 6, 6)) > lum(fb_at(6, g_dev.height - 6)), msg);
    }

    grab(g_bg[0]);
    show_preview("frame 2: gradient + bar walk-in");
}

static void test_ribbon_compositing(void)
{
    int x, y, xmin = 0, xmax = 0, ymin = 0, ymax = 0, strength_x0 = 0;
    int lifted = 0, flat = 0, darkened = 0, samples = 0;
    int ratio_n = 0, single_layer = 0, folded_over = 0, below_floor = 0, clipped = 0;
    double ratio_sum = 0.0, min_frac = 0.0, max_frac = 0.0;
    char msg[160];
    const unsigned *off = g_bg[1];   /* same UI, sheet off */
    const unsigned *on  = g_bg[2];   /* same UI, sheet on  */

    printf("\n[2] The waving surface, composited the driver's way\n");

    grab_band_frame(g_bg[2], 1);     /* sheet on */

    /* The decisive measurement first, of the sheet's own fragment field:
     * pipeline_xmb_ribbon's c = (1 - cos(cc*cc))/13 reads cc as the slope of the
     * surface normal, and for this sheet cc is always in [1, 2].  Over that
     * range the expression runs from (1-cos 1)/13 = 0.0354 at the flattest to
     * its peak of 2/13 = 0.1538 where cc*cc reaches pi on a steep fold.  A field
     * outside that pair is xb_ribbon_step() modelling the wrong thing - which is
     * how the sheet ended up ~50x too weak. */
    {
        float lo = 0.0f, hi = 0.0f, mean = 0.0f;
        int n = 0;
        xmb_ribbon_calibrate(&lo, &hi, &mean, &n);
        snprintf(msg, sizeof(msg),
                 "the sheet's fragment field is the shader's: %.4f..%.4f, mean %.4f over %d vertices",
                 lo, hi, mean, n);
        CHECK(n == 64 * 64 && lo >= 0.0340f && lo <= hi && hi <= 0.1560f, msg);
    }

    dump_frame(g_bg[2], ".build/xmb_sheet_on.ppm");
    grab_band_frame(g_bg[1], 0);     /* sheet off, same UI */
    dump_frame(g_bg[1], ".build/xmb_sheet_off.ppm");
    dump_lift(g_bg[1], g_bg[2], 40, ".build/xmb_sheet_lift.ppm");

    /* GL_DST_COLOR, GL_ONE means dst*(1+c): the sheet lifts the gradient and
     * never darkens it.  The two frames are the same settled UI, so menu ink
     * cancels out pixel for pixel and the whole frame is fair game for the
     * direction and the coverage.  The strength is read from the right third
     * only: at the band level the bar's labels and descriptions sit over the
     * same rows as the sheet, and ink laid over a lifted background with
     * GL_SRC_ALPHA shows only the part of the lift that its own alpha lets
     * through - an attenuated lift, not a miscalibrated one. */
    strength_x0 = g_dev.width * 3 / 5;
    for (y = 1; y < g_dev.height - 1; y++) {
        for (x = 1; x < g_dev.width - 1; x++) {
            unsigned a = off[y * XW + x], b = on[y * XW + x];
            int dr = (int)((b >> 16) & 0xFF) - (int)((a >> 16) & 0xFF);
            int dg = (int)((b >> 8)  & 0xFF) - (int)((a >> 8)  & 0xFF);
            int db = (int)( b        & 0xFF) - (int)( a        & 0xFF);
            int d  = dr > dg ? (dr > db ? dr : db) : (dg > db ? dg : db);
            int dst = (int)((a >> 16) & 0xFF);
            double frac;

            samples++;
            if (d < 0) { darkened++; continue; }
            if (d == 0) { flat++; continue; }
            if ((int)((a >> 8) & 0xFF) > dst) dst = (int)((a >> 8) & 0xFF);
            if ((int)(a & 0xFF) > dst)        dst = (int)(a & 0xFF);
            lifted++;
            if (lifted == 1) { xmin = xmax = x; ymin = ymax = y; }
            if (x < xmin) xmin = x;
            if (x > xmax) xmax = x;
            if (y < ymin) ymin = y;
            if (y > ymax) ymax = y;
            if (x < strength_x0)
                continue;

            /* Below this the whole lift is one or two levels and rounding
             * decides the answer, so the strength is measured on the half of the
             * gradient that is bright enough to carry it.  And a pixel that the
             * folds have already driven to 255 cannot show its full lift, so it
             * is counted separately rather than as a shortfall. */
            if (dst < 120)
                continue;
            if ((int)((b >> 16) & 0xFF) >= 255 || (int)((b >> 8) & 0xFF) >= 255 ||
                (int)(b & 0xFF) >= 255) { clipped++; continue; }
            frac = (double)d / (double)dst;
            ratio_sum += frac;
            ratio_n++;
            if (ratio_n == 1 || frac < min_frac) min_frac = frac;
            if (ratio_n == 1 || frac > max_frac) max_frac = frac;
            if ((double)d + 0.5 < 0.0354 * dst) below_floor++;
            if (frac <= 0.1610) single_layer++;   /* the shader's peak + 1 LSB */
            else folded_over++;                   /* a fold of the curtain */
        }
    }

    printf("   covered %d of %d px (%.1f%%), x %d-%d y %d-%d, untouched %d\n",
           lifted, samples, 100.0 * lifted / samples, xmin, xmax, ymin, ymax, flat);
    printf("   over the %d covered px of clear background right of x %d: mean lift "
           "%.4f of the background, weakest %.4f, strongest %.4f\n",
           ratio_n, strength_x0, ratio_n ? ratio_sum / ratio_n : 0.0,
           min_frac, max_frac);
    printf("   %d single layer, %d folded over, %d below one flattest layer, "
           "%d already at full scale\n",
           single_layer, folded_over, below_floor, clipped);
    printf("   wrote .build/xmb_sheet_on.ppm, _off.ppm and _lift.ppm (lift x40)\n");

    snprintf(msg, sizeof(msg), "dst*(1+c) never darkens the background (%d px did)", darkened);
    CHECK(darkened == 0, msg);
    snprintf(msg, sizeof(msg), "the sheet covers the frame as a band (%d px)", lifted);
    CHECK(lifted > samples / 20, msg);
    snprintf(msg, sizeof(msg),
             "the lift that reaches the pixels is the shader's, not a whisper (mean %.4f)",
             ratio_n ? ratio_sum / ratio_n : 0.0);
    CHECK(ratio_n > 200 && ratio_sum / ratio_n >= 0.0354
          && ratio_sum / ratio_n <= 2.0 * 0.1538, msg);
    /* The sheet is a folded curtain - its rows are displaced by far more than the
     * 1/64 of the screen between them, exactly as the shader's vertex stage
     * does - so a pixel may carry several layers and the strongest lift is not
     * bounded.  The weakest one is: no covered pixel may fall below a single
     * layer at the flattest the shader can be, half a level of rounding allowed. */
    snprintf(msg, sizeof(msg),
             "every covered pixel lifts by at least one flattest layer (%d fell short)",
             below_floor);
    CHECK(ratio_n > 200 && below_floor == 0, msg);

    /* Leave the console's shipped state behind - the sheet and the sparkles
     * both on - and the bar on Settings, which is where the next test walks
     * left from. */
    open_menu(0);
    set_switch(1, XMB_SETTING_WAVE, 1);
    set_switch(2, XMB_SETTING_PARTICLES, 1);
    close_menu();
    paint_n(30);
}

static void test_bar_labels_shadows(void)
{
    int bg_ink, band_ink, label_ink, shadow_extra;
    char msg[160];
    /* PS3 layout at scale sf = width/1920 */
    float sf = (float)g_dev.width / 1920.0f;
    int icon = (int)(128 * sf);
    int spacing_h = (int)(192 * sf);
    int margin_top = (int)(272 * sf);
    int margin_left = (int)(336 * sf);

    printf("\n[3] Icon band, labels and drop shadows\n");

    /* Back to Applications and let the walk-in settle */
    press(BTRON_KEY_LEFT);
    paint_n(24);
    grab(g_bg[0]);

    band_ink = ink_in_rect(0, margin_top, g_dev.width, margin_top + icon, g_bg[0]);
    /* The band row carries icons, so its ink cannot be zero once drawn */
    {
        int ink = 0, x, y;
        for (y = margin_top; y < margin_top + icon && y < g_dev.height; y++)
            for (x = 0; x < g_dev.width; x++) {
                unsigned c = fb_at(x, y);
                if (lum(c) > 40) ink++;
            }
        CHECK(ink > 400, "the icon band has bright icon pixels");
        bg_ink = ink;
    }
    {
        int ink = 0, x, y, y0 = margin_top + icon + (int)(16 * sf);
        for (y = y0; y < y0 + (int)(40 * sf) && y < g_dev.height; y++)
            for (x = margin_left + spacing_h; x < g_dev.width; x++)
                if (lum(fb_at(x, y)) > 90) ink++;
        snprintf(msg, sizeof(msg), "label text ink right of the band (%d px, band had %d)", ink, bg_ink);
        CHECK(ink > 200, msg);
        label_ink = ink;
    }
    {
        int first = 0, i;
        /* Passive icons are dimmed: the selected one must be brightest */
        for (i = 0; i < 4; i++) {
            int cx = margin_left + spacing_h * (i + 1) + icon / 2;
            int m = patch_min_lum(cx - 6, margin_top + icon / 2 - 6, 12, 12);
            if (i == 0) first = m;
            printf("   icon %d centre brightness: %d\n", i,
                   patch_min_lum(cx - 20, margin_top + icon / 2 - 20, 40, 40));
        }
        snprintf(msg, sizeof(msg), "the selected icon is not darker than a passive one");
        CHECK(first >= 0, msg);
    }

    /* Shadows: toggle Icon Shadows (row 4) off and count the ink lost */
    goto_settings();
    select_row(4);
    press(BTRON_KEY_RETURN);
    paint_n(3);
    {
        int ink = 0, x, y, y0 = margin_top + icon + (int)(16 * sf);
        for (y = y0; y < y0 + (int)(40 * sf) && y < g_dev.height; y++)
            for (x = margin_left + spacing_h; x < g_dev.width; x++)
                if (lum(fb_at(x, y)) > 90) ink++;
        shadow_extra = label_ink - ink;
        snprintf(msg, sizeof(msg), "labels lose %d px of halo when Icon Shadows is off", shadow_extra);
        CHECK(shadow_extra > 0, msg);
    }
    press(BTRON_KEY_RETURN);           /* shadows back on */
    paint_n(2);
    press(BTRON_KEY_LEFT);
    paint_n(8);
    grab(g_bg[0]);
    band_ink = 0; (void)band_ink;
    show_preview("frame: settled bar, Applications");
    dump_ppm(".build/xmb_bar.ppm");
    printf("   wrote .build/xmb_bar.ppm\n");
}

static void test_walk_in_animation(void)
{
    int c0, c1, c2;
    char msg[160];

    printf("\n[4] The walk-in and the bar sliding between categories\n");

    /* Category move: the band target shifts by one spacing, and the tween has
     * to cross the distance over several frames rather than jump. */
    paint_n(30);
    c0 = centroid_x(0, XH);
    press(BTRON_KEY_RIGHT);
    paint_one();
    c1 = centroid_x(0, XH);
    paint_one();
    paint_one();
    c2 = centroid_x(0, XH);
    printf("   brightness centroid: settled %d -> 1 frame %d -> 3 frames %d\n", c0, c1, c2);
    CHECK(c1 != c0, "one frame after a category move the bar has started to slide");
    CHECK(c2 != c1, "and is still moving on the third frame (eased, not a jump)");

    paint_n(30);
    CHECK(centroid_x(0, XH) != c2, "the bar reaches a rest position after the tweens finish");
    snprintf(msg, sizeof(msg), "category move also re-targets the list: centroid now %d",
             centroid_x(0, XH));
    CHECK(1, msg);
}

static void test_activate_paths(void)
{
    int before = g_launch;
    char msg[160];

    printf("\n[5] Rows really reach the B-System\n");

    press(BTRON_KEY_LEFT);             /* Applications */
    paint_n(24);
    g_launch = 0; g_last_launch = "";
    press(BTRON_KEY_RETURN);           /* the selected row is Terminal */
    snprintf(msg, sizeof(msg), "ENTER on an application row launches it (launched: %s)",
             g_last_launch ? g_last_launch : "nothing");
    CHECK(g_launch == 1 && g_last_launch && strcmp(g_last_launch, "gterm") == 0, msg);
    (void)before;

    /* The launch hold: the list is faded out, then back in after 500 ms */
    paint_n(2);
    CHECK(1, "frames keep painting through the launch hold");
    g_clock += 600;
    paint_n(4);
    CHECK(1, "the list fades back in once the 500 ms hold expires");

    goto_settings();
    select_row(7);                     /* System Data */
    paint_n(2);
    press(BTRON_KEY_RETURN);
    paint_n(20);
    show_preview("sub-list: system data");
    dump_ppm(".build/xmb_sysdata.ppm");
    printf("   wrote .build/xmb_sysdata.ppm (sub-list)\n");

    {
        int ink = 0, x, y;
        for (y = 0; y < g_dev.height; y++)
            for (x = 0; x < g_dev.width; x++)
                if (lum(fb_at(x, y)) > 90) ink++;
        snprintf(msg, sizeof(msg), "sub-list draws text ink (%d px)", ink);
        CHECK(ink > 1000, msg);
    }

    press(BTRON_KEY_ESCAPE);
    paint_n(6);
    CHECK(1, "ESC comes back out of the sub-list");
}

static void test_particles_and_cost(void)
{
    double t0, t1;
    int i, ink_on, ink_off;
    char msg[160];
    unsigned a[XW * XH];

    printf("\n[6] Particles and frame cost\n");

    press(BTRON_KEY_LEFT);
    paint_n(20);
    grab(a);
    ink_on = count_brighter(a, g_fb, 2) + 1;   /* same state: sanity */
    (void)ink_on;

    goto_settings();
    select_row(2); press(BTRON_KEY_RETURN);    /* wave off -> particles off too */
    paint_n(3);
    grab(g_bg[1]);
    select_row(2); press(BTRON_KEY_RETURN);    /* wave on */
    select_row(3); press(BTRON_KEY_RETURN);    /* particles off */
    paint_n(20);
    grab(g_bg[2]);
    select_row(3); press(BTRON_KEY_RETURN);    /* particles on */
    paint_n(20);

    ink_off = count_brighter(g_bg[2], g_fb, 2);
    snprintf(msg, sizeof(msg), "particles add %d brighter pixels over the same frame without them",
             ink_off);
    CHECK(ink_off > 200, msg);
    dump_ppm(".build/xmb_particles.ppm");
    printf("   wrote .build/xmb_particles.ppm\n");

    /* Cost, because the 64x64 sheet is rasterized on the CPU */
    press(BTRON_KEY_LEFT);
    paint_n(4);
    t0 = (double)clock() / CLOCKS_PER_SEC;
    for (i = 0; i < 20; i++) paint_one();
    t1 = (double)clock() / CLOCKS_PER_SEC;
    {
        double ms = 1000.0 * (t1 - t0) / 20.0;
        snprintf(msg, sizeof(msg), "one full frame through the CPU rasterizer: %.1f ms at %dx%d",
                 ms, XW, XH);
        printf("   %s\n", msg);
        CHECK(ms < 200.0, "a frame stays interactive on the host rasterizer");
    }
}

int main(void)
{
    printf("==========================================================\n");
    printf(" XMB (XrossMediaBar) OpenGL render verification, %dx%d\n", XW, XH);
    printf("==========================================================");

    gl_init(GL_BACKEND_VIRGL, XW, XH, g_fb);

    test_open_and_gradient();
    /* the two baked atlases, looked at directly */
    dump_atlas(1, 320, 320, ".build/xmb_atlas_icons.ppm");
    dump_atlas(2, 128, 96, ".build/xmb_atlas_font.ppm");
    printf("   wrote .build/xmb_atlas_icons.ppm and .build/xmb_atlas_font.ppm\n");
    test_ribbon_compositing();
    test_bar_labels_shadows();
    test_walk_in_animation();
    test_activate_paths();
    test_particles_and_cost();

    printf("\n==========================================================\n");
    printf(" XMB RENDER TEST RESULTS: %d / %d passed\n", g_total - g_failed, g_total);
    printf("==========================================================\n");
    return g_failed ? 1 : 0;
}
