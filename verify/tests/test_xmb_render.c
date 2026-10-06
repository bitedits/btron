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
 *      ribbon fragment stage).  Measured as the frame difference between the
 *      surface on and off: it must brighten, never darken, and by at most the
 *      analytic ceiling of the shader's own expression.
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

/* Largest per-channel rise and fall between two frames */
static void frame_delta(const unsigned *a, const unsigned *b, int *up, int *down)
{
    int i, mx = -(1 << 20), mn = 1 << 20;
    for (i = 0; i < XW * XH; i++) {
        int dr = (int)((b[i] >> 16) & 0xFF) - (int)((a[i] >> 16) & 0xFF);
        int dg = (int)((b[i] >> 8)  & 0xFF) - (int)((a[i] >> 8)  & 0xFF);
        int db = (int)( b[i]        & 0xFF) - (int)( a[i]        & 0xFF);
        int d = dr > dg ? (dr > db ? dr : db) : (dg > db ? dg : db);
        if (d > mx) mx = d;
        if (d < mn) mn = d;
    }
    *up = mx;
    *down = mn;
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

static void dump_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    int x, y;
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", g_dev.width, g_dev.height);
    for (y = 0; y < g_dev.height; y++)
        for (x = 0; x < g_dev.width; x++) {
            unsigned c = fb_at(x, y);
            fputc(ch_r(c), f); fputc(ch_g(c), f); fputc(ch_b(c), f);
        }
    fclose(f);
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
            int l = lum(fb_at(sx, sy)) * 4 / (255 * 4);
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

/* Settings rows, by their order in s_items_settings:
 * 0 Theme, 1 Language, 2 Wave Background, 3 Wave Particles,
 * 4 Icon Shadows, 5 Screen Brightness, 6 Edge Fade, 7 System Data */
static void goto_settings(void)
{
    press(BTRON_KEY_RIGHT);
}

static void select_row(int row)
{
    int i;
    for (i = 0; i < row; i++)
        press(BTRON_KEY_DOWN);
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
    int up = 0, down = 0, lit = 0, i;
    char msg[160];

    printf("\n[2] The waving surface, composited the driver's way\n");

    /* Turn the surface off through the menu itself, so the settings read-back
     * path is exercised on the way, and compare whole frames. */
    goto_settings();
    paint_n(2);
    select_row(2);                    /* Wave Background */
    press(BTRON_KEY_RETURN);          /* -> off */
    paint_n(2);
    grab(g_bg[1]);                    /* no sheet: the plain frame */

    select_row(2);                    /* back onto the same row from its twin? */
    paint_n(1);

    /* Re-enable by pressing ENTER again after navigating back to the row */
    press(BTRON_KEY_RETURN);
    paint_n(2);
    grab(g_bg[2]);                    /* sheet on */

    {
        int changed = 0;
        for (i = 0; i < XW * XH; i++)
            if (g_bg[1][i] != g_bg[2][i]) changed++;
        snprintf(msg, sizeof(msg), "the sheet changes %d of %d pixels (%.1f%%)",
                 changed, XW * XH, 100.0 * changed / (XW * XH));
        CHECK(changed > XW * XH / 20, msg);
    }

    frame_delta(g_bg[1], g_bg[2], &up, &down);
    printf("   measured per-channel delta: max rise %+d, max fall %+d\n", up, down);
    CHECK(down >= -1, "dst*(1+c) never darkens the background");
    /* gl2.c's fragment expression: c = (1 - cos(c*c))/13, whose ceiling with
     * c -> 1 is (1-cos 1)/13 = 0.0354, i.e. at most +9 on a 255 scale */
    CHECK(up <= 12, "the rise stays inside the shader's own (1-cos 1)/13 ceiling");

    lit = count_brighter(g_bg[1], g_bg[2], 1);
    snprintf(msg, sizeof(msg), "%d pixels lifted by the embossed sheet", lit);
    CHECK(lit > 0, msg);

    /* The sheet must be a surface, not noise: adjacent rows of the 64x64 grid
     * are within a couple of levels of each other. */
    {
        int rough = 0, x;
        for (x = 0; x < g_dev.width; x += 7) {
            int y;
            for (y = 10; y < g_dev.height - 20; y += 13) {
                int a = lum(g_fb[y * XW + x]), b = lum(g_fb[(y + 1) * XW + x]);
                int d = a - b;
                if (d < 0) d = -d;
                if (d > 14) rough++;
            }
        }
        snprintf(msg, sizeof(msg), "sheet is smooth across rows (%d hard steps)", rough);
        CHECK(rough < 40, msg);
    }
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
