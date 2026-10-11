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
 *   6. the Settings rows that hold a value of the B-System itself, read from and
 *      written back to the kernel, the PMC, the terminal and the input method.
 *   7. the Discs band — the mounted volumes and their drawers, walked through the
 *      real VFS and the real Record-Stream FS: the row list against the VFS's own
 *      listing, the MiniDisc glyph compared pixel-wise with the drawer's, the
 *      category bar's sideways push at every level of a nine-level tree, and the
 *      cursor coming back to the row it came from.
 *   8. the Games band — the host's assets/msx folder listed as one row per
 *      cartridge image, the row list counted against the folder itself, and ENTER
 *      handing the chosen ROM to the B-MSX launcher.
 *
 * Everything under test is linked for real (xmb.c, gl_dispatch.c, egl_surface.c,
 * backend_virgl.c, troncode.c, and for the Discs band src/clu/vfs.c with the FS
 * engine src/fs files, on RAM volumes the test formats itself); only the window
 * system, the ITRON task layer and the app launchers are stubbed, and the clock is
 * advanced one 16 ms frame per paint so the animation timeline is reproducible.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>     /* the Games band lists the host's assets/msx */

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
#include <btron/file.h>
#include <btron/fs/block.h>
#include <btron/fs/vol_api.h>
#include <btron/fs/fs_internal.h>
#include <btron/tad.h>
#include "../../src/apps/xmb.h"
#include "../../src/clu/vfs.h"

#define XW 960
#define XH 600

/* Where the frames this test writes land.  The default is the shared .build/ dir.
 * The same source is also linked against the TinyGL backend the PS2 ships with
 * (`make test-xmb-render-tgl`), and that harness passes its own directory here so
 * the two renderers leave frames beside each other instead of overwriting each
 * other's -- which is the only way to see a defect that exists in one backend and
 * not the other. */
#ifndef XMB_OUT_DIR
#define XMB_OUT_DIR ".build/"
#endif

/* ── Host display device, window and clock ───────────────────────────── */

static COLOR   g_fb[XW * XH];
static GDEV    g_dev;
static WND     g_wnd;
static SYSTIME g_clock = 1000;   /* one paint == one 16 ms frame */
static int     g_shell_calls;
static char    g_shell_last[128];

void uart_puts_raw(const char *s) { (void)s; }

/* xmb.c's load-phase telemetry times itself with the compositor's stage counter:
 * dp_core.c carries the weak 0 and the bare-metal cores the real one, and this
 * link has neither, so it gets the same constant the weak fallback gives. */
uint32_t btron_render_perf_us(void) { return 0; }

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

/* B-MSX is reached with the cartridge's name, so the Games band's rows are
 * checked on that name: the stub below records which ROM it was handed. */
static char g_launch_rom[160];
WND* open_msx_window(void)           { g_launch++; g_last_launch = "msx";
                                       g_launch_rom[0] = '\0'; return NULL; }
WND* open_msx_window_with_rom(const char *path)
{
    g_launch++; g_last_launch = "msx";
    snprintf(g_launch_rom, sizeof(g_launch_rom), "%s", path ? path : "");
    return NULL;
}

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
static void blit_texture(unsigned id, int w, int h)
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
}

static void dump_atlas(unsigned id, int w, int h, const char *path)
{
    int x, y;
    unsigned *save = (unsigned *)malloc(sizeof(g_fb));

    blit_texture(id, w, h);
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

/* Look at one cell of the icon atlas: its ink count and a grey raster of it, so
 * the glyph a row is drawn with can be recognised as well as counted.  The frame
 * buffer is left cleared, as dump_atlas does; the bar repaints every frame. */
static int atlas_cell_look(int cell, const char *caption)
{
    static const char ramp[] = " .:-=+*#%@";
    const int grid = 6, tex = 384, cw = tex / grid;      /* xmb.c's own atlas */
    int x, y, ink = 0;
    int ox = (cell % grid) * cw, oy = (cell / grid) * cw;

    if (cell < 0) return -1;
    blit_texture(1, tex, tex);
    for (y = 0; y < cw; y++)
        for (x = 0; x < cw; x++)
            if (lum(fb_at(ox + x, oy + y)) > 40) ink++;

    printf("   atlas cell %d (x %d y %d), %d ink px -- %s\n", cell, ox, oy, ink, caption);
    for (y = 0; y < cw; y += 2) {
        printf("   |");
        for (x = 0; x < cw; x++) {
            int l = lum(fb_at(ox + x, oy + y)) * 9 / 255;
            if (l > 9) l = 9;
            fputc(ramp[l], stdout);
        }
        printf("|\n");
    }
    memset(g_fb, 0, sizeof(g_fb));
    return ink;
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
 *   band 2 Discs        -> the mounted volumes, and below them their drawers
 *   band 3 Games        -> the host's assets/msx folder, one row per cartridge
 *   band 4 Commands     -> shell builtins, in a message box
 * The rows with a bind hold the B-System's own state, so they are checked against
 * the variables at the top of this file, not against xmb_setting(). */
/* The bands, by their order in s_cats */
#define BAND_APPS     0
#define BAND_SETTINGS 1
#define BAND_DISCS    2
#define BAND_GAMES    3
#define BAND_COMMANDS 4

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

/* Icon Shadows is the only switch that changes how much ink the bar puts down, so
 * its A/B is taken twice through here: set the switch, close the menu, and park
 * the bar on the Applications band.  With the menu shut the switch's own "On" or
 * "Off" word is not drawn anywhere in either frame, which leaves the shadow as the
 * only thing that can differ between them. */
static void set_shadows_and_park(unsigned *dst, int want)
{
    goto_settings();
    open_menu(MENU_SCREEN);
    set_switch(ROW_SCREEN_SHADOWS, XMB_SETTING_SHADOWS, want);
    close_menu();
    park_band(BAND_APPS);
    paint_n(30);
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
        /* The law is the field, not the mesh: XB_RIBBON_ROWS/COLS is a measured
         * quality/cost dial in xmb.c, so the density assertion is a floor -- a sheet
         * sampled at fewer than 32x32 points is no longer the same surface -- while
         * the range clause below is what catches the ~50x-too-weak modelling. */
        CHECK(n >= 32 * 32 && lo >= 0.0340f && lo <= hi && hi <= 0.1560f, msg);
    }

    dump_frame(g_bg[2], XMB_OUT_DIR "xmb_sheet_on.ppm");
    grab_band_frame(g_bg[1], 0);     /* sheet off, same UI */
    dump_frame(g_bg[1], XMB_OUT_DIR "xmb_sheet_off.ppm");
    dump_lift(g_bg[1], g_bg[2], 40, XMB_OUT_DIR "xmb_sheet_lift.ppm");

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

    /* Shadows: a shadow is a black copy of the glyph one or two pixels away, so it
     * can only ever take light back out of the frame - never add any.
     *
     * Both frames are taken with the menu closed, on the Applications band: the
     * row that switches shadows has a word of its own, "On" in one state and "Off"
     * in the other, and while its menu is open the two frames would be pictures of
     * different text rather than of the shadow. */
    set_shadows_and_park(with_shadows, 1);
    set_shadows_and_park(without, 0);

    {
        int k;
        int lit_x0 = XW, lit_x1 = 0, lit_y0 = XH, lit_y1 = 0, lit_max = 0;
        int ink_with = 0, ink_without = 0;
        shadow_px = 0;      /* px the shadow took light out of  */
        brightened = 0;     /* px the shadow somehow added light to */
        for (k = 0; k < XW * XH; k++) {
            int d = lum(without[k]) - lum(with_shadows[k]);
            /* Counted at the brightness the bar paints its glyphs and icons at, so
             * that the count is the ink itself rather than the background the
             * shadow is allowed to darken. */
            if (lum(with_shadows[k]) >= 200) ink_with++;
            if (lum(without[k])        >= 200) ink_without++;
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
        printf("   the shadow darkens %d px by 8 or more; full-brightness ink counts"
               " %d with, %d without (the band's own label holds %d)\n",
               shadow_px, ink_with, ink_without, label_ink);
        snprintf(msg, sizeof(msg), "Icon Shadows darkens %d px and never brightens one",
                 shadow_px);
        CHECK(shadow_px > 200 && brightened == 0, msg);
        /* What the shadow costs the ink itself: a glyph's outermost coverage is
         * only partly opaque, so the black run under it can take a pixel or two
         * back below full brightness.  It can never lift one, and the count of
         * full-brightness pixels moves by well under a percent. */
        snprintf(msg, sizeof(msg), "the ink it is drawn under is all but untouched: %d px at"
                 " full brightness with the shadow, %d without", ink_with, ink_without);
        CHECK(ink_with <= ink_without && (ink_without - ink_with) * 100 < ink_without
              && ink_with > 500, msg);
    }

    set_shadows_and_park(NULL, 1);      /* the shipped state, bar back on Applications */
    paint_n(20);
    show_preview("frame: settled bar, Applications");
    dump_ppm(XMB_OUT_DIR "xmb_bar.ppm");
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
    dump_ppm(XMB_OUT_DIR "xmb_sysdata.ppm");
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
    int i, lit_by_sparkles;
    char msg[160];

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

    lit_by_sparkles = count_brighter(g_bg[2], g_fb, 2);
    snprintf(msg, sizeof(msg), "particles add %d brighter pixels over the same frame without them",
             lit_by_sparkles);
    CHECK(lit_by_sparkles > 200, msg);
    dump_ppm(XMB_OUT_DIR "xmb_particles.ppm");
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

    snprintf(msg, sizeof(msg), "the bar has five bands: Applications, Settings, Discs, Games, "
             "Commands (%d)", xmb_bands());
    CHECK(xmb_bands() == 5, msg);

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

/* How different two cells of the atlas are, in pixels.  A row's icon being another
 * *number* is not the claim - two cells could hold the same glyph - so the two
 * silhouettes are compared where they are drawn. */
static int atlas_cell_diff(int a, int b)
{
    const int grid = 6, tex = 384, cw = tex / grid;
    int x, y, diff = 0;

    if (a < 0 || b < 0)
        return -1;
    blit_texture(1, tex, tex);
    for (y = 0; y < cw; y++)
        for (x = 0; x < cw; x++) {
            int pa = lum(fb_at((a % grid) * cw + x, (a / grid) * cw + y)) / 16;
            int pb = lum(fb_at((b % grid) * cw + x, (b / grid) * cw + y)) / 16;
            if (pa != pb)
                diff++;
        }
    memset(g_fb, 0, sizeof(g_fb));
    return diff;
}

/* ── The Discs band's storage: real volumes, real drawers ─────────────────
 * The band's rows are whatever src/clu/vfs.c lists, so the test does not stub the
 * VFS or describe the storage a second time: it formats two RAM volumes, files
 * bodies in the Cho-Kanji one, and gives them parentage with the same 16-byte link
 * records the FS engine reads (verify/tests/test_fs.c builds those bytes).  Then
 * every claim below is about what the bar shows while walking that volume. */

#define DISC_IMG_BLOCKS 256                      /* 256 KiB, 1 KiB blocks */
#define DISC_IMG_BYTES  (DISC_IMG_BLOCKS * 1024)
#define DISC_DEPTH      8                        /* drawers the bar is walked down */

static unsigned char s_disc_img[DISC_IMG_BYTES];
static unsigned char s_sys_img[DISC_IMG_BYTES];

static Volume *disc_vol_make(unsigned char *img, const char *name)
{
    BlkDev *dev;

    memset(img, 0, DISC_IMG_BYTES);
    dev = blk_mem_create(img, DISC_IMG_BYTES, 0);
    if (!dev)
        return NULL;
    if (vol_format(dev, 64, DISC_IMG_BLOCKS, name) != 0)
        return NULL;
    return vol_mount(dev);
}

/* One Virtual Body: the target's FID big-endian, the name's length at [14..15],
 * then the name itself.  The record's type is what makes it a link. */
static int link_rec(ID dir_fd, unsigned int idx, FID target, const char *name)
{
    unsigned char buf[16 + 64];
    size_t n = strlen(name);

    if (n > sizeof(buf) - 16)
        return -1;
    memset(buf, 0, sizeof(buf));
    buf[0] = (unsigned char)(target >> 24);
    buf[1] = (unsigned char)(target >> 16);
    buf[2] = (unsigned char)(target >> 8);
    buf[3] = (unsigned char)(target);
    buf[14] = (unsigned char)(n >> 8);
    buf[15] = (unsigned char)(n);
    memcpy(buf + 16, name, n);
    if (ins_rec(dir_fd, (W)idx, buf, (W)(16 + n)) != 0)
        return -1;
    fil_set_rec_type(dir_fd, (W)idx, (UH)RT_LINK);
    return 0;
}

/* Bodies are created flat, by name, and get their parentage from whoever links to
 * them - which is the whole of the 2-level Record-Stream model. */
static FID body_make(const char *path, const char *text)
{
    ID fd = cre_fil(path, 0x0002);
    FID fid;

    if (fd < 0)
        return FID_INVALID;
    if (text) {
        ins_rec(fd, 0, text, (W)strlen(text));
        fil_set_rec_type(fd, 0, (UH)RT_TADDATA);
    }
    fid = g_open_files[(int)fd].fid;
    cls_fil(fd);
    return fid;
}

static FID drawer_make(const char *path, const char *child_name, FID child)
{
    ID fd = cre_fil(path, 0x0002);
    FID fid;

    if (fd < 0)
        return FID_INVALID;
    if (link_rec(fd, 0, child, child_name) != 0) {
        cls_fil(fd);
        return FID_INVALID;
    }
    fid = g_open_files[(int)fd].fid;
    cls_fil(fd);
    return fid;
}

/* The volume's own top drawer, which is the folder "/" names once the volume is
 * chosen: FID 0, addressed by the volume name alone. */
static int root_link(const char *volpath, unsigned int idx, const char *name, FID target)
{
    ID fd = opn_fil(volpath, 0x0002);
    int r;

    if (fd < 0)
        return -1;
    r = link_rec(fd, idx, target, name);
    cls_fil(fd);
    return r;
}

/* The text a body is filed with, and the Japanese name one link carries: the
 * atlas has no glyph past '~', so the bar must show that row as underscores while
 * still opening the body it names. */
static const char DISC_TEXT[] =
    "The Discs band reads this body through src/clu/vfs.c.\r\n"
    "Its head is what the message box shows.\r\n"
    "Third line, for the wrapping of a long one into the box.";
static const char DISC_JP_NAME[] = "\xE8\xB6\x85\xE6\xBC\xA2\xE5\xAD\x97";   /* Cho-Kanji */

/* Returns 1 when the storage was built, 0 when the FS itself refuses, so the
 * section can say SKIP rather than fail on a machine problem. */
static int disc_storage_build(void)
{
    FID notes, xmbc, todo, src, docs, chain[DISC_DEPTH + 1];
    char path[48], link[48];
    int k;

    /* Whatever the earlier sections left mounted is not this test's business; the
     * volume list is its own, so the machine starts from two volumes only. */
    while (vol_mounted_count() > 0) {
        Volume *v = vol_get_mounted(0);
        if (!v)
            break;
        vol_umount(v);
    }
    g_sys_vol = NULL;

    if (!disc_vol_make(s_sys_img, "SYS") || !disc_vol_make(s_disc_img, "CHOKANJI"))
        return 0;

    notes = body_make("/CHOKANJI/Notes", DISC_TEXT);
    xmbc  = body_make("/CHOKANJI/xmb.c", "void xb_paint(void) { /* the bar itself */ }");
    todo  = body_make("/CHOKANJI/todo.txt", "- keep the band honest");
    if (notes == FID_INVALID || xmbc == FID_INVALID || todo == FID_INVALID)
        return 0;

    src  = drawer_make("/CHOKANJI/src", "xmb.c", xmbc);
    docs = drawer_make("/CHOKANJI/Documents", "todo.txt", todo);

    /* A chain DISC_DEPTH drawers deep, the last of them holding the Notes body */
    snprintf(path, sizeof(path), "/CHOKANJI/Drawer %d", DISC_DEPTH);
    chain[DISC_DEPTH] = drawer_make(path, "Notes", notes);
    for (k = DISC_DEPTH - 1; k >= 1; k--) {
        snprintf(path, sizeof(path), "/CHOKANJI/Drawer %d", k);
        snprintf(link, sizeof(link), "Drawer %d", k + 1);
        chain[k] = drawer_make(path, link, chain[k + 1]);
    }
    if (src == FID_INVALID || docs == FID_INVALID || chain[1] == FID_INVALID)
        return 0;

    /* The top level: three drawers, the body, and the same body under a Japanese
     * name - which is exactly the aliasing a name-only path cannot tell apart. */
    if (root_link("/CHOKANJI", 0, "src", src) != 0
        || root_link("/CHOKANJI", 1, "Documents", docs) != 0
        || root_link("/CHOKANJI", 2, "Drawer 1", chain[1]) != 0
        || root_link("/CHOKANJI", 3, "Notes", notes) != 0
        || root_link("/CHOKANJI", 4, DISC_JP_NAME, notes) != 0)
        return 0;

    return 1;
}

/* The row whose label is `want`, or -1 */
static int row_of(const char *want)
{
    int i;

    for (i = 0; i < xmb_rows(); i++)
        if (strcmp(xmb_label(i), want) == 0)
            return i;
    return -1;
}

/* How many entries the VFS itself lists for a folder.  The band is checked against
 * this, because both are the same call on the same folder: if the bar ever kept a
 * copy of a listing, the two numbers would part. */
static int vfs_rows(const char *path)
{
    VfsEntry ent[32];

    return vfs_list_dir(path, ent, 32);
}

static void test_discs_band(void)
{
    char msg[200];
    int row, k, rows;
    unsigned before[XW * XH];

    printf("\n[8] The Discs band: the mounted volumes, walked as far as they go\n");

    if (!disc_storage_build()) {
        printf("   SKIP: the FS itself would not build the volumes and drawers\n");
        CHECK(1, "Discs section skipped because no storage could be formatted");
        return;
    }

    /* The band is where the user put it: Applications, Settings, Discs, Games, Commands */
    grab_band_frame(NULL, 0);
    park_band(BAND_DISCS);
    snprintf(msg, sizeof(msg), "the third band is the Discs one, and it is showing the "
             "mounted volumes (%d rows, path \"%s\")", xmb_rows(), xmb_path());
    CHECK(xmb_band() == BAND_DISCS && xmb_depth() == 1 && xmb_levels() == 0
          && xmb_rows() == 2 && strcmp(xmb_path(), "/") == 0, msg);

    /* The rows are the VFS's own listing, in the order sc gives a folder: drawers
     * before bodies, each class by name.  Both volumes are drawers, so the two rows
     * are the two names sorted. */
    snprintf(msg, sizeof(msg), "the volume list is the VFS listing, ordered as sc orders a "
             "folder (0 \"%s\", 1 \"%s\")", xmb_label(0), xmb_label(1));
    CHECK(strcmp(xmb_label(0), "CHOKANJI") == 0 && strcmp(xmb_label(1), "SYS") == 0, msg);

    /* Each volume carries the MiniDisc, not the folder glyph a drawer gets. */
    {
        int disc_cell = xmb_row_icon(0);
        int drawer_cell, minidisc_ink, cell_diff;

        /* Down into CHOKANJI once and back, so a drawer row can be read: the same
         * band, a different kind of row. */
        select_row(0);
        press(BTRON_KEY_RETURN);
        paint_n(20);
        drawer_cell = xmb_rows() > 0 ? xmb_row_icon(0) : -1;
        press(BTRON_KEY_ESCAPE);
        paint_n(20);
        snprintf(msg, sizeof(msg), "a volume is drawn with its own glyph, and a drawer with "
                 "another (volume cell %d, drawer cell %d)", disc_cell, drawer_cell);
        CHECK(disc_cell >= 0 && drawer_cell >= 0 && disc_cell != drawer_cell, msg);
        minidisc_ink = atlas_cell_look(disc_cell,
                                       "the MiniDisc a mounted volume is drawn with");
        snprintf(msg, sizeof(msg), "the MiniDisc is baked into the atlas, not blank "
                 "(%d ink px)", minidisc_ink);
        CHECK(minidisc_ink > 60, msg);
        cell_diff = atlas_cell_diff(disc_cell, drawer_cell);
        snprintf(msg, sizeof(msg), "the volume's glyph is a different silhouette from a "
                 "drawer's, not the same picture twice (%d px differ)", cell_diff);
        CHECK(cell_diff > 300, msg);
    }

    /* Into the Cho-Kanji volume: the band's rows become that volume's top drawer. */
    select_row(0);
    press(BTRON_KEY_RETURN);
    paint_n(20);
    rows = xmb_rows();
    snprintf(msg, sizeof(msg), "ENTER opens the volume: depth %d, levels %d, path \"%s\", "
             "%d rows", xmb_depth(), xmb_levels(), xmb_path(), rows);
    CHECK(xmb_depth() == 2 && xmb_levels() == 1 && strcmp(xmb_path(), "/CHOKANJI") == 0
          && rows == 5, msg);

    snprintf(msg, sizeof(msg), "the folder's rows are the VFS's own listing of the same "
             "folder, not a copy (bar %d, VFS %d)", xmb_rows(), vfs_rows(xmb_path()));
    CHECK(xmb_rows() == vfs_rows(xmb_path()) && xmb_rows() == 5, msg);

    snprintf(msg, sizeof(msg), "drawers come before bodies and each class is sorted by name "
             "(0 \"%s\" 1 \"%s\" 2 \"%s\" 3 \"%s\" 4 \"%s\")",
             xmb_label(0), xmb_label(1), xmb_label(2), xmb_label(3), xmb_label(4));
    CHECK(strcmp(xmb_label(0), "Documents") == 0
          && strcmp(xmb_label(1), "Drawer 1") == 0
          && strcmp(xmb_label(2), "src") == 0
          && strcmp(xmb_label(3), "Notes") == 0
          && strcmp(xmb_label(4), "___") == 0, msg);

    /* The console's way back, by name: the cursor returns to the row it came from,
     * wherever that row is in the re-read listing. */
    row = row_of("src");
    select_row(row);
    press(BTRON_KEY_RETURN);
    paint_n(20);
    snprintf(msg, sizeof(msg), "a drawer opens onto its own child (\"%s\", %d row(s), "
             "\"%s\")", xmb_label(0), xmb_rows(), xmb_path());
    CHECK(xmb_depth() == 3 && xmb_levels() == 2 && strcmp(xmb_label(0), "xmb.c") == 0
          && xmb_rows() == 1, msg);
    press(BTRON_KEY_ESCAPE);
    paint_n(20);
    snprintf(msg, sizeof(msg), "ESC re-reads the parent and puts the cursor back on the row "
             "it came from (row %d \"%s\", depth %d)", xmb_row(), xmb_label(xmb_row()),
             xmb_depth());
    CHECK(xmb_depth() == 2 && xmb_row() == row && strcmp(xmb_label(xmb_row()), "src") == 0,
          msg);

    /* A row whose name the bitmap font has no glyph for is shown as underscores, and
     * it is the same body the ASCII row names - which the anchor in the path is what
     * makes certain, since the volume holds two links to one Real Body here. */
    {
        char alias[VFS_MAX_PATH];
        char head[96];
        size_t got = 0;

        snprintf(alias, sizeof(alias), "/CHOKANJI/%s", DISC_JP_NAME);
        snprintf(msg, sizeof(msg), "the Japanese-named row and \"Notes\" name one body "
                 "(its head reads back: %.24s)",
                 vfs_read_file(alias, head, sizeof(head), &got) == 0 ? head : "(unreadable)");
        CHECK(strncmp(head, DISC_TEXT, 24) == 0, msg);
    }
    row = row_of("___");
    select_row(row);
    grab(before);
    press(BTRON_KEY_RETURN);
    paint_n(6);
    {
        int lit = count_brighter(before, g_fb, 8);
        snprintf(msg, sizeof(msg), "the underscore row opens that body rather than the first "
                 "of its name (%d px of message ink, depth still %d)", lit, xmb_depth());
        CHECK(lit > 300 && xmb_depth() == 2 && xmb_row() == row, msg);
    }
    press(BTRON_KEY_ESCAPE);
    paint_n(6);

    /* ENTER on a body fills the message box with its head - the bar has no text
     * window, so that is as far as a menu can take a file. */
    row = row_of("Notes");
    select_row(row);
    paint_n(20);
    grab(before);
    press(BTRON_KEY_RETURN);
    paint_n(6);
    {
        int lit = count_brighter(before, g_fb, 8);
        snprintf(msg, sizeof(msg), "ENTER on a body opens its head in the message box "
                 "(%d px added, depth still %d)", lit, xmb_depth());
        CHECK(lit > 300 && xmb_depth() == 2, msg);
    }
    show_preview("frame: a body's head in the message box");
    dump_ppm(XMB_OUT_DIR "xmb_disc_notes.ppm");
    press(BTRON_KEY_ESCAPE);
    paint_n(4);

    /* Infinity: the bar goes down as many levels as the volume has, and the category
     * bar keeps sliding one icon width per level with no ceiling - the divergence
     * from menu/drivers/xmb.c:3906 that used to stop the indent at depth 2. */
    select_row(row_of("Drawer 1"));
    press(BTRON_KEY_RETURN);
    paint_n(24);
    {
        int x_first = xmb_bar_x();
        int deep_ok = 1;
        int last_x = x_first;

        for (k = 2; k <= DISC_DEPTH; k++) {
            select_row(0);
            press(BTRON_KEY_RETURN);
            paint_n(24);
            if (xmb_depth() != k + 2 || xmb_levels() != k + 1)
                deep_ok = 0;
            if (xmb_bar_x() >= last_x)
                deep_ok = 0;
            last_x = xmb_bar_x();
        }
        snprintf(msg, sizeof(msg), "%d drawers below the volume: depth %d, levels %d, "
                 "\"%s\", bar pushed to x %d px from its stop at x %d, one step further "
                 "at every level", DISC_DEPTH, xmb_depth(), xmb_levels(), xmb_path(),
                 xmb_bar_x(), x_first);
        CHECK(deep_ok && xmb_depth() == DISC_DEPTH + 2 && xmb_levels() == DISC_DEPTH + 1
              && xmb_bar_x() < x_first, msg);
    }
    snprintf(msg, sizeof(msg), "the deepest folder is still the volume's own row list "
             "(\"%s\", %d row(s), VFS %d)", xmb_label(0), xmb_rows(), vfs_rows(xmb_path()));
    CHECK(xmb_rows() == 1 && strcmp(xmb_label(0), "Notes") == 0
          && xmb_rows() == vfs_rows(xmb_path()), msg);
    show_preview("frame: the deepest drawer of the volume");
    dump_ppm(XMB_OUT_DIR "xmb_disc_deep.ppm");

    /* And the way out, level by level: ESC pops one folder at a time, all the way
     * from the bottom of the tree back to the mounted-volume list. */
    for (k = DISC_DEPTH; k >= 1; k--) {
        press(BTRON_KEY_ESCAPE);
        paint_n(3);
    }
    paint_n(20);
    snprintf(msg, sizeof(msg), "ESC pops every drawer back to the volume itself "
             "(depth %d, levels %d, path \"%s\", %d rows)", xmb_depth(), xmb_levels(),
             xmb_path(), xmb_rows());
    CHECK(xmb_depth() == 2 && xmb_levels() == 1 && strcmp(xmb_path(), "/CHOKANJI") == 0
          && xmb_rows() == 5, msg);
    press(BTRON_KEY_ESCAPE);
    paint_n(40);
    snprintf(msg, sizeof(msg), "one more ESC is the mounted-volume list itself "
             "(depth %d, path \"%s\", %d rows)", xmb_depth(), xmb_path(), xmb_rows());
    CHECK(xmb_depth() == 1 && strcmp(xmb_path(), "/") == 0 && xmb_rows() == 2, msg);
    snprintf(msg, sizeof(msg), "the categories are back at their own stop as the stack "
             "closes (x %d px)", xmb_bar_x());
    CHECK(xmb_bar_x() == 0, msg);

    /* An empty volume is a listing the machine can hand out, and the bar has to
     * survive it: no rows, so ENTER opens nothing. */
    select_row(row_of("SYS"));
    press(BTRON_KEY_RETURN);
    paint_n(20);
    g_launch = 0;
    g_last_launch = "";
    press(BTRON_KEY_RETURN);
    paint_n(4);
    snprintf(msg, sizeof(msg), "an empty volume opens to no rows and ENTER acts on none of "
             "them (%d rows, launched %s)", xmb_rows(),
             g_last_launch[0] ? g_last_launch : "nothing");
    CHECK(xmb_depth() == 2 && xmb_rows() == 0 && g_launch == 0, msg);
    press(BTRON_KEY_ESCAPE);
    paint_n(10);

    /* LEFT/RIGHT on a band that is a folder leave it, and the console does not park
     * you inside a drawer when you come back. */
    select_row(0);
    press(BTRON_KEY_RETURN);
    paint_n(20);
    press(BTRON_KEY_RIGHT);
    paint_n(20);
    snprintf(msg, sizeof(msg), "a value key on a folder row switches the band instead "
             "(band %d, depth %d)", xmb_band(), xmb_depth());
    CHECK(xmb_band() == BAND_GAMES && xmb_depth() == 1, msg);
    press(BTRON_KEY_LEFT);
    paint_n(20);
    snprintf(msg, sizeof(msg), "and the band restarts at the volume list (path \"%s\", "
             "%d rows, depth %d)", xmb_path(), xmb_rows(), xmb_depth());
    CHECK(strcmp(xmb_path(), "/") == 0 && xmb_rows() == 2 && xmb_depth() == 1, msg);

    /* The frame the user described: the volume list, MiniDisc and all. */
    park_band(BAND_DISCS);
    paint_n(30);
    show_preview("frame: the Discs band, mounted volumes");
    dump_ppm(XMB_OUT_DIR "xmb_disc_volumes.ppm");
    printf("   wrote .build/xmb_disc_volumes.ppm, xmb_disc_notes.ppm, xmb_disc_deep.ppm\n");
}

/* ── The Games band: the host's cartridge folder ─────────────────────────
 * The band is assets/msx read once before the first frame, so the claim worth
 * checking is that the rows are the folder's own names in the folder's own order,
 * and that a row hands its own cartridge down to B-MSX.  The folder is counted
 * here the way the band counts it - same suffix test, same order - because a
 * hand-written list of six names would prove nothing about the listing. */
#define GAME_NAME      64        /* XB_GAME_NAME in xmb.c */
#define GAME_ROWS_MAX  16        /* XB_GAME_ROWS in xmb.c */

static BOOL game_is_rom(const char *name)
{
    size_t n = strlen(name);

    if (n < 5 || n >= GAME_NAME || name[n - 4] != '.')
        return FALSE;
    return (name[n - 3] == 'r' || name[n - 3] == 'R')
        && (name[n - 2] == 'o' || name[n - 2] == 'O')
        && (name[n - 1] == 'm' || name[n - 1] == 'M');
}

/* -> the number of cartridge images in assets/msx, names sorted; -1 when the
 * folder cannot be opened. */
static int games_host_roms(char names[][GAME_NAME], int max)
{
    DIR *dir = opendir("assets/msx");
    struct dirent *de;
    int n = 0, i;

    if (!dir)
        return -1;
    while (n < max && (de = readdir(dir)) != NULL) {
        if (de->d_name[0] == '.' || !game_is_rom(de->d_name))
            continue;
        snprintf(names[n], GAME_NAME, "%s", de->d_name);
        n++;
    }
    closedir(dir);

    for (i = 1; i < n; i++) {
        char keep[GAME_NAME];
        int j = i - 1;

        snprintf(keep, GAME_NAME, "%s", names[i]);
        while (j >= 0 && strcmp(names[j], keep) > 0) {
            snprintf(names[j + 1], GAME_NAME, "%s", names[j]);
            j--;
        }
        snprintf(names[j + 1], GAME_NAME, "%s", keep);
    }
    return n;
}

static void test_games_band(void)
{
    char names[GAME_ROWS_MAX][GAME_NAME];
    char msg[240];
    int n, i, mismatch, row, ink, cell, disc_cell, diff;

    printf("\n[9] The Games band: the cartridges in assets/msx, opened in B-MSX\n");

    n = games_host_roms(names, GAME_ROWS_MAX);
    if (n < 0) {
        printf("   SKIP: assets/msx cannot be opened from here\n");
        CHECK(1, "Games section skipped because assets/msx is not readable");
        return;
    }

    /* The MiniDisc glyph is the neighbouring band's own row, read on the way past. */
    park_band(BAND_DISCS);
    disc_cell = xmb_rows() > 0 ? xmb_row_icon(0) : -1;

    park_band(BAND_GAMES);
    snprintf(msg, sizeof(msg), "the fourth band is the Games one, and it is showing the "
             "folder as it stands (%d rows, folder holds %d cartridge%s)",
             xmb_rows(), n, n == 1 ? "" : "s");
    CHECK(xmb_band() == BAND_GAMES && xmb_depth() == 1 && xmb_rows() == n, msg);

    if (n == 0) {
        printf("   SKIP: assets/msx holds no cartridge image, so there is no row to open\n");
        CHECK(1, "Games section has no ROM to launch");
        return;
    }

    /* Row for row, the bar's labels are the folder's own names, in its own order. */
    mismatch = -1;
    for (i = 0; i < n; i++) {
        if (strcmp(xmb_label(i), names[i]) != 0) {
            mismatch = i;
            break;
        }
    }
    snprintf(msg, sizeof(msg), "every Games row is one file of assets/msx, ordered as the "
             "folder sorts it (row %d is \"%s\", folder says \"%s\")",
             mismatch < 0 ? n - 1 : mismatch,
             xmb_label(mismatch < 0 ? n - 1 : mismatch),
             names[mismatch < 0 ? n - 1 : mismatch]);
    CHECK(mismatch < 0, msg);

    /* ENTER hands the row's own cartridge to B-MSX, not always the first one. */
    row = n > 1 ? 1 : 0;
    select_row(row);
    g_launch = 0;
    g_last_launch = "";
    g_launch_rom[0] = '\0';
    press(BTRON_KEY_RETURN);
    paint_n(4);
    snprintf(msg, sizeof(msg), "ENTER on a Games row opens B-MSX with that row's ROM "
             "(launched %s, ROM \"%s\", wanted \"%s\")",
             g_last_launch[0] ? g_last_launch : "nothing", g_launch_rom, names[row]);
    CHECK(g_launch == 1 && strcmp(g_last_launch, "msx") == 0
          && strcmp(g_launch_rom, names[row]) == 0, msg);

    /* The first row too, so the index a row carries is shown to move. */
    select_row(0);
    g_launch = 0;
    g_launch_rom[0] = '\0';
    press(BTRON_KEY_RETURN);
    paint_n(4);
    snprintf(msg, sizeof(msg), "the first row opens its own ROM (\"%s\")", g_launch_rom);
    CHECK(g_launch == 1 && strcmp(g_launch_rom, names[0]) == 0, msg);

    /* The band's glyph is a cartridge of its own: baked into the atlas, and a
     * different silhouette from the MiniDisc the band above it draws with. */
    cell = xmb_row_icon(0);
    ink = atlas_cell_look(cell, "the cartridge a Games row is drawn with");
    snprintf(msg, sizeof(msg), "the Games glyph is baked into the icon atlas, not blank "
             "(cell %d, %d ink px)", cell, ink);
    CHECK(cell >= 0 && ink > 60, msg);
    diff = cell >= 0 && disc_cell >= 0 ? atlas_cell_diff(cell, disc_cell) : -1;
    snprintf(msg, sizeof(msg), "a cartridge is drawn as something else than a mounted "
             "volume (%d px differ)", diff);
    CHECK(diff > 300, msg);

    paint_n(30);
    show_preview("frame: the Games band, cartridges in assets/msx");
    dump_ppm(XMB_OUT_DIR "xmb_games_band.ppm");
    printf("   wrote .build/xmb_games_band.ppm\n");
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
    /* 384 = the atlas's own 6x6 grid of 64 px cells (XB_ATLAS_GRID in xmb.c) */
    dump_atlas(1, 384, 384, XMB_OUT_DIR "xmb_atlas_icons.ppm");
    dump_atlas(2, 128, 96, XMB_OUT_DIR "xmb_atlas_font.ppm");
    printf("   wrote .build/xmb_atlas_icons.ppm and .build/xmb_atlas_font.ppm\n");
    test_ribbon_compositing();
    test_bar_labels_shadows();
    test_walk_in_animation();
    test_activate_paths();
    test_particles_and_cost();
    test_system_bindings();
    test_discs_band();
    test_games_band();

    printf("\n==========================================================\n");
    printf(" XMB RENDER TEST RESULTS: %d / %d passed\n", g_total - g_failed, g_total);
    printf("==========================================================\n");
    return g_failed ? 1 : 0;
}
