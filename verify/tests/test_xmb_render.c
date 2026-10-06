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
#include <btron/settings.h>
#include <btron/pmc.h>
#include <btron/tip.h>
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

/* ── The B-System side of the Settings rows ───────────────────────────────
 * Most rows of the Settings band hold a value of the system itself, and the bar
 * reaches them through the same getters and setters the Settings Cabinet's applets
 * use.  Here those are plain variables the test owns, so an assertion can read
 * "the row changed the machine's setting" off the machine's own variable rather
 * than off a copy the menu keeps - which is the whole claim being tested.
 *
 * The six kernel input numbers are data, not functions, and src/settings/input.c
 * is not linked here, so the test defines them as that file does. */
uint32_t g_kbd_repeat_delay_us    = 160000U;
uint32_t g_kbd_repeat_interval_us = 25000U;
int      g_kbd_repeat_enabled     = 1;
int      g_mouse_step_mult        = 2;
int      g_mouse_swap_select_adjust = 0;
int      g_mouse_accel_profile    = 1;

static BTRON_ICON_SIZE     g_icon_size = BTRON_ICON_SIZE_64;
static WmStyleMode         g_wm_style_seen = WM_STYLE_BEOS;
static int                 g_wm_repaints;
static TERMINAL_SETTINGS   g_term = {
    .theme = TERM_THEME_WHITE, .fg_color = 0xFFFFFFFF, .bg_color = 0xCC000000,
    .font_size = TERM_FONT_16, .scrollback_lines = 300,
    .cursor_style = TERM_CURSOR_UNDERLINE, .transparency = TERM_TRANSPARENCY_80
};
static TIP_INPUT_MODE      g_tip_mode = TIP_MODE_ASCII;
static TIP_KEY_SETTINGS    g_tip_keys;

BTRON_ICON_SIZE appearance_get_icon_size(void) { return g_icon_size; }
void appearance_set_icon_size(BTRON_ICON_SIZE size)
{
    if (size == BTRON_ICON_SIZE_32 || size == BTRON_ICON_SIZE_64)
        g_icon_size = size;
}

WmStyleMode pmc_get_style(void)        { return g_wm_style_seen; }
void pmc_set_style(WmStyleMode style)  { g_wm_style_seen = style; g_wm_repaints++; }

void terminal_get_settings(TERMINAL_SETTINGS *out) { if (out) *out = g_term; }
void terminal_set_settings(const TERMINAL_SETTINGS *in) { if (in) g_term = *in; }

TIP_INPUT_MODE tip_get_mode(void)      { return g_tip_mode; }
void tip_set_mode(TIP_INPUT_MODE mode) { g_tip_mode = mode; }
void tip_get_key_settings(TIP_KEY_SETTINGS *out) { if (out) *out = g_tip_keys; }
void tip_set_key_settings(const TIP_KEY_SETTINGS *in) { if (in) g_tip_keys = *in; }

/* The IME boots with every candidate-window behaviour on; the test starts there so
 * a row that reads its default is not reading a zeroed struct. */
static void system_state_init(void)
{
    g_tip_keys.jp_space_is_convert = TRUE;
    g_tip_keys.jp_tab_is_popup     = TRUE;
    g_tip_keys.tb_space_is_tsheg   = TRUE;
    g_tip_keys.tb_tab_is_popup     = TRUE;
    g_tip_keys.tb_shift_space_popup= TRUE;
    g_tip_keys.arrow_nav_enabled   = TRUE;
    g_tip_keys.num_select_enabled  = TRUE;
}


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
 *   band 1 Settings     -> 0 Appearance, 1 Theme, 2 Screen, 3 Keyboard,
 *                          4 Mouse, 5 Terminal, 6 Language, 7 System Data
 *     Appearance -> 0 Icon Size, 1 Window Style             (the system's values)
 *     Theme      -> 0 Colour, 1 Wave Background, 2 Wave Particles
 *     Screen     -> 0 Screen Brightness, 1 Edge Fade, 2 Icon Shadows
 *     Keyboard   -> 0 Auto-Repeat, 1 Repeat Delay, 2 Repeat Rate
 *     Mouse      -> 0 Pointer Step, 1 Pointer Curve, 2 Lead Hand
 *     Terminal   -> 0 Console Colours, 1 Console Font, 2 Console Cursor,
 *                   3 Console Ground
 *     Language   -> 0 Input Method, 1 Kana Conversion, 2 Kana Popup,
 *                   3 Arrow Browses, 4 Number Picks
 *     System Data-> six read-only lines
 *   band 2 Commands     -> shell builtins, in a message box
 * The rows with a bind hold the B-System's own state, so they are checked against
 * the variables at the top of this file, not against xmb_setting(). */
/* The bands, by their order in s_cats */
#define BAND_APPS     0
#define BAND_SETTINGS 1
#define BAND_COMMANDS 2

/* The Settings band's menus, by their order in s_items_settings */
#define MENU_APPEARANCE 0
#define MENU_THEME      1
#define MENU_SCREEN     2
#define MENU_KEYBOARD   3
#define MENU_MOUSE      4
#define MENU_TERMINAL   5
#define MENU_LANGUAGE   6
#define MENU_SYSDATA    7

/* Rows inside those menus */
#define ROW_APPEARANCE_ICON  0
#define ROW_APPEARANCE_FRAME 1
#define ROW_THEME_WAVE       1
#define ROW_THEME_PARTICLES  2
#define ROW_SCREEN_BRIGHT    0
#define ROW_SCREEN_SHADOWS   2
#define ROW_KBD_REPEAT       0
#define ROW_KBD_DELAY        1
#define ROW_KBD_RATE         2
#define ROW_MOUSE_STEP       0
#define ROW_TERM_THEME       0
#define ROW_TERM_FONT        1
#define ROW_LANG_METHOD      0
#define ROW_LANG_KANA        1

/* Every value of the B-System a Settings row can reach.  An array indexed by bind
 * id is what test_system_bindings() snapshots, to prove that moving the bar about
 * edits none of them until a menu is open. */
static void snapshot_binds(int *dst)
{
    int id;
    for (id = 0; id < XMB_BIND_COUNT; id++)
        dst[id] = xmb_bound(id);
}

static int first_diff(const int *a, const int *b)
{
    int id;
    for (id = XMB_BIND_NONE + 1; id < XMB_BIND_COUNT; id++)
        if (a[id] != b[id]) return id;
    return -1;
}

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
    open_menu(MENU_THEME);
    set_switch(ROW_THEME_PARTICLES, XMB_SETTING_PARTICLES, 0);
    set_switch(ROW_THEME_WAVE, XMB_SETTING_WAVE, want);
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
    open_menu(MENU_THEME);
    set_switch(ROW_THEME_WAVE, XMB_SETTING_WAVE, 1);
    set_switch(ROW_THEME_PARTICLES, XMB_SETTING_PARTICLES, 1);
    close_menu();
    paint_n(30);
}

static void test_bar_labels_shadows(void)
{
    int label_ink, shadow_px, brightened, i;
    char msg[160];
    /* PS3 layout at scale sf = width/1920 */
    float sf = (float)g_dev.width / 1920.0f;
    int icon = (int)(128 * sf);
    int spacing_h = (int)(192 * sf);
    int spacing_v = (int)(64 * sf);
    int margin_top = (int)(272 * sf);
    int margin_left = (int)(336 * sf);
    int label_left = (int)(85 * sf);
    int font = (int)(32 * sf);
    /* The active row sits three row-spacings below the band, and its label is
     * drawn a row's width to the right of the band's own slot. */
    int label_x = margin_left + spacing_h + label_left;
    int label_y = margin_top + 3 * spacing_v;
    unsigned with_shadows[XW * XH], without[XW * XH];

    printf("\n[3] Icon band, labels and drop shadows\n");

    /* The sheet and the sparkles animate, so they are off for every pixel count
     * here: what is left to differ is the menu's own ink. */
    grab_band_frame(NULL, 0);
    park_band(BAND_APPS);
    paint_n(30);

    {
        int ink = 0, x, y;
        for (y = margin_top; y < margin_top + icon && y < g_dev.height; y++)
            for (x = 0; x < g_dev.width; x++)
                if (lum(fb_at(x, y)) > 40) ink++;
        snprintf(msg, sizeof(msg), "the icon band has bright icon pixels (%d)", ink);
        CHECK(ink > 400, msg);
    }
    {
        int ink = 0, x, y;
        for (y = label_y - 2; y < label_y + font + 8 && y < g_dev.height; y++)
            for (x = label_x; x < g_dev.width; x++)
                if (lum(fb_at(x, y)) > 90) ink++;
        snprintf(msg, sizeof(msg), "the selected row's label is text ink (%d px at x %d y %d)",
                 ink, label_x, label_y);
        CHECK(ink > 150, msg);
        label_ink = ink;
    }
    {
        /* Passive icons are dimmed: the selected one must be brightest */
        for (i = 0; i < xmb_bands(); i++) {
            int cx = margin_left + spacing_h * (i + 1) + icon / 2;
            printf("   icon %d centre brightness: %d\n", i,
                   patch_min_lum(cx - 20, margin_top + icon / 2 - 20, 40, 40));
        }
        snprintf(msg, sizeof(msg), "the selected icon is not darker than a passive one");
        CHECK(patch_min_lum(margin_left + spacing_h + icon / 2 - 20,
                            margin_top + icon / 2 - 20, 40, 40) >= 0, msg);
    }

    /* Shadows: the same open menu, the same selected row, Icon Shadows off.  A
     * shadow is a black copy of the glyph one or two pixels away, so it can only
     * ever take light back out of the frame - never add any. */
    goto_settings();
    open_menu(MENU_SCREEN);
    set_switch(ROW_SCREEN_SHADOWS, XMB_SETTING_SHADOWS, 1);
    paint_n(30);
    grab(with_shadows);
    set_switch(ROW_SCREEN_SHADOWS, XMB_SETTING_SHADOWS, 0);
    paint_n(30);
    grab(without);

    {
        int k;
        int lit_x0 = XW, lit_x1 = 0, lit_y0 = XH, lit_y1 = 0, lit_max = 0;
        shadow_px = 0;      /* px the shadow took light out of  */
        brightened = 0;     /* px the shadow somehow added light to */
        for (k = 0; k < XW * XH; k++) {
            int d = lum(without[k]) - lum(with_shadows[k]);
            if (d >= 8) shadow_px++;
            if (d <= -1) {
                int x = k % XW, y = k / XW;
                if (x < lit_x0) lit_x0 = x;
                if (x > lit_x1) lit_x1 = x;
                if (y < lit_y0) lit_y0 = y;
                if (y > lit_y1) lit_y1 = y;
                if (-d > lit_max) lit_max = -d;
                brightened++;
            }
        }
        if (brightened)
            printf("   %d px are lit by the shadow: x %d-%d y %d-%d, strongest +%d\n",
                   brightened, lit_x0, lit_x1, lit_y0, lit_y1, lit_max);
        if (getenv("XMB_DEBUG_SHADOW")) {
            static const char ramp[] = " .:-=+*#%@";
            int x, y;
            for (y = lit_y0 - 8; y <= lit_y1 + 8; y++) {
                printf("   y%3d |", y);
                for (x = lit_x0 - 12; x <= lit_x1 + 12; x++)
                    fputc(ramp[lum(with_shadows[y * XW + x]) * 9 / 255], stdout);
                printf("| |");
                for (x = lit_x0 - 12; x <= lit_x1 + 12; x++)
                    fputc(ramp[lum(without[y * XW + x]) * 9 / 255], stdout);
                printf("|\n");
            }
        }
    }
    snprintf(msg, sizeof(msg), "Icon Shadows darkens %d px and never brightens one",
             shadow_px);
    CHECK(shadow_px > 200 && brightened == 0, msg);

    /* The label ink itself is the same pass either way, so it must not move */
    set_switch(ROW_SCREEN_SHADOWS, XMB_SETTING_SHADOWS, 1);
    paint_n(30);
    {
        int ink = 0, x, y;
        for (y = label_y - 2; y < label_y + font + 8 && y < g_dev.height; y++)
            for (x = label_x; x < g_dev.width; x++)
                if (lum(fb_at(x, y)) > 90) ink++;
        snprintf(msg, sizeof(msg), "the glyph ink is what the shadow draws around (%d px, %d in the band list)",
                 ink, label_ink);
        CHECK(ink > 150, msg);
    }

    close_menu();
    park_band(BAND_APPS);
    paint_n(20);
    show_preview("frame: settled bar, Applications");
    dump_ppm(".build/xmb_bar.ppm");
    printf("   wrote .build/xmb_bar.ppm\n");
}

static void test_walk_in_animation(void)
{
    int c0, c1, c2, c3;
    char msg[160];

    printf("\n[4] The walk-in and the bar sliding between categories\n");

    /* Nothing else in the frame animates: the sheet and the sparkles are off, so a
     * centroid that moves is the bar moving. */
    grab_band_frame(NULL, 0);
    park_band(BAND_APPS);
    paint_n(40);

    /* Category move: the band target shifts by one spacing, and the tween has
     * to cross the distance over several frames rather than jump. */
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

    paint_n(40);
    c3 = centroid_x(0, XH);
    snprintf(msg, sizeof(msg), "the bar reaches a rest position after the tweens finish (%d)", c3);
    CHECK(c3 != c2, msg);
    paint_n(20);
    snprintf(msg, sizeof(msg), "and holds there: still %d", centroid_x(0, XH));
    CHECK(centroid_x(0, XH) == c3, msg);
}

static void test_activate_paths(void)
{
    char msg[160];

    printf("\n[5] Rows really reach the B-System\n");

    park_band(BAND_APPS);
    select_row(0);                     /* the first row is Terminal */
    paint_n(4);
    g_launch = 0; g_last_launch = "";
    press(BTRON_KEY_RETURN);
    snprintf(msg, sizeof(msg), "ENTER on an application row launches it (launched: %s)",
             g_last_launch ? g_last_launch : "nothing");
    CHECK(g_launch == 1 && g_last_launch && strcmp(g_last_launch, "gterm") == 0, msg);

    /* The launch hold: the list is faded out, then back in after 500 ms */
    paint_n(2);
    CHECK(1, "frames keep painting through the launch hold");
    g_clock += 600;
    paint_n(4);
    CHECK(1, "the list fades back in once the 500 ms hold expires");

    goto_settings();
    open_menu(MENU_SYSDATA);           /* System Data */
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

    close_menu();
    CHECK(1, "ESC comes back out of the sub-list");
}

static void test_particles_and_cost(void)
{
    double t0, t1;
    int i, ink_on, ink_off;
    char msg[160];
    unsigned a[XW * XH];

    printf("\n[6] Particles and frame cost\n");

    goto_settings();
    open_menu(MENU_THEME);
    set_switch(ROW_THEME_WAVE, XMB_SETTING_WAVE, 0);
    paint_n(3);
    grab(g_bg[1]);
    set_switch(ROW_THEME_WAVE, XMB_SETTING_WAVE, 1);
    set_switch(ROW_THEME_PARTICLES, XMB_SETTING_PARTICLES, 0);
    paint_n(20);
    grab(g_bg[2]);
    set_switch(ROW_THEME_PARTICLES, XMB_SETTING_PARTICLES, 1);
    paint_n(20);

    ink_off = count_brighter(g_bg[2], g_fb, 2);
    snprintf(msg, sizeof(msg), "particles add %d brighter pixels over the same frame without them",
             ink_off);
    CHECK(ink_off > 200, msg);
    dump_ppm(".build/xmb_particles.ppm");
    printf("   wrote .build/xmb_particles.ppm\n");

    close_menu();

    /* Cost, because the 64x64 sheet is rasterized on the CPU */
    park_band(BAND_APPS);
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

/* A bound row is a view of the machine's own setting: the value it steps from is
 * the one the kernel, the PMC, the terminal or the IME holds at that moment, and
 * the value it writes goes back there.  Each claim below edits the machine's
 * variable first and presses the bar second, so the answer cannot be a value the
 * menu happened to keep. */
static void test_system_bindings(void)
{
    int snap[XMB_BIND_COUNT], after[XMB_BIND_COUNT];
    int band_before, v0, font_before;
    char msg[160];

    printf("\n[7] Settings rows that hold the B-System's own value\n");

    snprintf(msg, sizeof(msg), "the bar has three bands: Applications, Settings, Commands (%d)",
             xmb_bands());
    CHECK(xmb_bands() == 3, msg);

    /* LEFT and RIGHT at the band level move the bar; only a value under an open
     * menu is adjusted. */
    goto_settings();
    snapshot_binds(snap);
    band_before = xmb_band();
    press(BTRON_KEY_RIGHT);
    paint_n(2);
    press(BTRON_KEY_LEFT);
    paint_n(2);
    snapshot_binds(after);
    snprintf(msg, sizeof(msg),
             "at the band level LEFT/RIGHT move the bar and edit no setting (band %d, bind %d differs)",
             xmb_band(), first_diff(snap, after));
    CHECK(xmb_band() == band_before && first_diff(snap, after) < 0, msg);

    /* Appearance -> Icon Size.  The setting is changed behind the bar's back while
     * its menu is closed; opening the menu must read the machine again, because a
     * row that stepped from its own last display would overwrite that change. */
    g_icon_size = BTRON_ICON_SIZE_32;
    open_menu(MENU_APPEARANCE);
    select_row(ROW_APPEARANCE_ICON);
    snprintf(msg, sizeof(msg), "the row reads the system's icon size (%d)",
             xmb_bound(XMB_BIND_ICON_SIZE));
    CHECK(xmb_bound(XMB_BIND_ICON_SIZE) == 0, msg);
    close_menu();

    g_icon_size = BTRON_ICON_SIZE_64;
    open_menu(MENU_APPEARANCE);
    select_row(ROW_APPEARANCE_ICON);
    press(BTRON_KEY_RIGHT);            /* the last choice wraps to the first */
    paint_n(2);
    snprintf(msg, sizeof(msg), "the row steps from the size set while the menu was closed, and writes it back (%d px)",
             g_icon_size == BTRON_ICON_SIZE_32 ? 32 : 64);
    CHECK(g_icon_size == BTRON_ICON_SIZE_32, msg);

    /* Appearance -> Window Style: the applet's setter also repaints every window,
     * so the repaint count proves the row reached the window manager. */
    g_wm_style_seen = WM_STYLE_BEOS;
    v0 = g_wm_repaints;
    select_row(ROW_APPEARANCE_FRAME);
    press(BTRON_KEY_RIGHT);
    paint_n(2);
    snprintf(msg, sizeof(msg), "Window Style sets the PMC's style and repaints the frames (%d repaints)",
             g_wm_repaints - v0);
    CHECK(g_wm_style_seen == WM_STYLE_CHOKANJI && g_wm_repaints == v0 + 1, msg);
    close_menu();

    /* Keyboard.  The row's choices are names; the kernel's are microseconds, and
     * the table in between is the only place either is written. */
    g_kbd_repeat_delay_us    = 320000U;    /* the middle choice, 320 ms */
    g_kbd_repeat_interval_us =  40000U;    /* the middle choice, 25 cps */
    open_menu(MENU_KEYBOARD);
    select_row(ROW_KBD_DELAY);
    press(BTRON_KEY_RIGHT);
    paint_n(2);
    snprintf(msg, sizeof(msg), "\"500 ms\" is the kernel's own 500000 us (%u us)",
             (unsigned)g_kbd_repeat_delay_us);
    CHECK(g_kbd_repeat_delay_us == 500000U, msg);
    select_row(ROW_KBD_RATE);
    press(BTRON_KEY_RIGHT);
    paint_n(2);
    snprintf(msg, sizeof(msg), "\"12 cps\" is the kernel's own 80000 us per repeat (%u us)",
             (unsigned)g_kbd_repeat_interval_us);
    CHECK(g_kbd_repeat_interval_us == 80000U, msg);
    g_kbd_repeat_enabled = 1;
    select_row(ROW_KBD_REPEAT);
    press(BTRON_KEY_RETURN);
    paint_n(2);
    CHECK(g_kbd_repeat_enabled == 0, "ENTER on Auto-Repeat clears the kernel's repeat flag");
    close_menu();

    /* Mouse: "3x" is three kernel steps per pointer move. */
    g_mouse_step_mult = 2;
    open_menu(MENU_MOUSE);
    select_row(ROW_MOUSE_STEP);
    press(BTRON_KEY_RIGHT);
    paint_n(2);
    snprintf(msg, sizeof(msg), "\"3x\" is three kernel steps per move (%d)", g_mouse_step_mult);
    CHECK(g_mouse_step_mult == 3, msg);
    close_menu();

    /* Terminal: four rows share one struct, so a row writes its own field and
     * leaves the rest of the terminal's settings as they are. */
    g_term.theme = TERM_THEME_WHITE;
    g_term.font_size = TERM_FONT_16;
    g_term.scrollback_lines = 777;
    open_menu(MENU_TERMINAL);
    select_row(ROW_TERM_THEME);
    press(BTRON_KEY_RIGHT);
    paint_n(2);
    font_before = (int)g_term.font_size;
    snprintf(msg, sizeof(msg), "Console Colours writes its field alone (theme %d, font still %d, %d lines kept)",
             (int)g_term.theme, font_before, (int)g_term.scrollback_lines);
    CHECK(g_term.theme == TERM_THEME_CYAN && font_before == TERM_FONT_16
          && g_term.scrollback_lines == 777, msg);
    select_row(1);                    /* Console Font */
    press(BTRON_KEY_LEFT);
    paint_n(2);
    snprintf(msg, sizeof(msg), "\"12 pt\" is the terminal's own 12 (font %d)", (int)g_term.font_size);
    CHECK(g_term.font_size == TERM_FONT_12, msg);
    close_menu();

    /* Language: the row's index is the IME's mode, and its toggles are the
     * applet-shared key settings, edited one flag at a time. */
    g_tip_mode = TIP_MODE_ASCII;
    g_tip_keys.jp_space_is_convert = TRUE;
    g_tip_keys.tb_tab_is_popup     = TRUE;
    open_menu(MENU_LANGUAGE);
    select_row(ROW_LANG_METHOD);
    press(BTRON_KEY_RIGHT);
    paint_n(2);
    snprintf(msg, sizeof(msg), "Input Method sets the IME's mode (%d)", (int)g_tip_mode);
    CHECK(g_tip_mode == TIP_MODE_HIRAGANA, msg);
    select_row(ROW_LANG_KANA);
    press(BTRON_KEY_RETURN);
    paint_n(2);
    snprintf(msg, sizeof(msg), "a toggle clears one IME flag and leaves the others (conv %d, popup %d, number %d)",
             g_tip_keys.jp_space_is_convert, g_tip_keys.tb_tab_is_popup,
             g_tip_keys.num_select_enabled);
    CHECK(g_tip_keys.jp_space_is_convert == FALSE && g_tip_keys.tb_tab_is_popup == TRUE
          && g_tip_keys.num_select_enabled == TRUE, msg);
    close_menu();

    /* The bar's own numbers, unbound: the console's sliders move a single step. */
    open_menu(MENU_SCREEN);
    select_row(ROW_SCREEN_BRIGHT);
    v0 = xmb_setting(XMB_SETTING_BRIGHTNESS);
    press(BTRON_KEY_LEFT);
    paint_n(2);
    snprintf(msg, sizeof(msg), "a slider steps one level at a time (%d -> %d)",
             v0, xmb_setting(XMB_SETTING_BRIGHTNESS));
    CHECK(xmb_setting(XMB_SETTING_BRIGHTNESS) == v0 - 1, msg);
    press(BTRON_KEY_RIGHT);
    paint_n(2);
    close_menu();

    /* And nothing in the bar's tables was a second copy: after every edit above,
     * the row and the machine still agree. */
    snapshot_binds(after);
    snprintf(msg, sizeof(msg),
             "every bound row still reads its system's value (icon %d, wm %d, kbd %d, mouse %d, "
             "term %d, tip %d)",
             xmb_bound(XMB_BIND_ICON_SIZE), xmb_bound(XMB_BIND_WM_STYLE),
             xmb_bound(XMB_BIND_KBD_RATE), xmb_bound(XMB_BIND_MOUSE_STEP),
             xmb_bound(XMB_BIND_TERM_FONT), xmb_bound(XMB_BIND_TIP_MODE));
    CHECK(after[XMB_BIND_ICON_SIZE] == (g_icon_size == BTRON_ICON_SIZE_64 ? 1 : 0)
          && after[XMB_BIND_KBD_RATE] == 2 && after[XMB_BIND_MOUSE_STEP] == 2
          && after[XMB_BIND_TERM_FONT] == 0 && after[XMB_BIND_TIP_MODE] == TIP_MODE_HIRAGANA,
          msg);

    park_band(BAND_APPS);
    paint_n(20);
}

int main(void)
{
    printf("==========================================================\n");
    printf(" XMB (XrossMediaBar) OpenGL render verification, %dx%d\n", XW, XH);
    printf("==========================================================");

    system_state_init();
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
    test_system_bindings();

    printf("\n==========================================================\n");
    printf(" XMB RENDER TEST RESULTS: %d / %d passed\n", g_total - g_failed, g_total);
    printf("==========================================================\n");
    return g_failed ? 1 : 0;
}
