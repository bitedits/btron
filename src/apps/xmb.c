/*
 * src/apps/xmb.c — XrossMediaBar (PS3 XMB) for B-System, rendered through
 * btron's own GL stack.
 *
 * This is a 1:1 port of the visual/behavioural system of
 * third_party/RetroArch/menu/drivers/xmb.c ("XMB" menu driver) plus its
 * downstack dependencies, resolved into this repository:
 *
 *   RetroArch                                  this file
 *   ─────────────────────────────────────────  ─────────────────────────────
 *   xmb.c:xmb_handle_t (layout + anim members) xb_layout / xb_state globals
 *   xmb.c:xmb_item_y()                         xb_item_y()
 *   xmb.c:xmb_calculate_visible_range()        xb_visible_range()
 *   xmb.c:xmb_draw_bg() + gradient tables      xb_draw_bg() / xb_gradient()
 *   xmb.c:xmb_init_ribbon()                    xb_ribbon_build()
 *   pipeline_xmb_ribbon.cg.h (vertex stage)    xb_ribbon_step()
 *   pipeline_xmb_ribbon.cg.h (fragment stage)  xb_ribbon_shade()
 *   gfx_display.c:gfx_animation_* (tweens)     xb_tween_* + xb_ease()
 *   xmb.c:xmb_draw_icon()                      xb_draw_icon()
 *   xmb.c:xmb_draw_text() / bitmapfont         xb_draw_text() (8x16 atlas)
 *   xmb.c:xmb_list_switch() (row walk)         xb_anim_row_move()
 *   xmb.c:xmb_list_switch_horizontal_list()    xb_anim_band_move()
 *   xmb.c:xmb_list_switch_new()                xb_anim_list_switch()
 *   xmb.c:xmb_list_open_new()/xmb_list_open()  xb_anim_list_open()
 *   xmb.c:xmb_animation_list_alpha()           xb_anim_list_alpha()
 *   xmb.c:xmb_render_messagebox_internal()     xb_draw_message()
 *
 * Everything is drawn with the dispatch table in src/gl/gl_dispatch.h; the
 * virgl backend (src/gl/backend_virgl.c) supplies the per-vertex alpha,
 * per-vertex UV, RGBA textures, orthographic projection and blending that the
 * menu layer needs (glBlendFunc/glColor4f/glVertex2f/glOrtho).
 *
 * Invented B-System content: the Applications / Settings / Volume / Commands
 * categories.  Applications really launch B-System windows, Commands really
 * run B-System shell builtins through shell_execute_cmd().
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "xmb.h"
#include "../gl/gl_dispatch.h"
#include "../gl/egl_surface.h"
#include <btron/btron.h>
#include <btron/core.h>
#include <btron/apps.h>
#include <btron/libc_shim.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif

extern void uart_puts_raw(const char *s);

/* ── Constants taken from xmb.c ───────────────────────────────────────
 * XMB_RIBBON_ROWS/COLS and XMB_DELAY are the driver's own; so are the two
 * easing ids (XMB_EASING_ALPHA = OUT_CIRC, XMB_EASING_XY = OUT_QUAD) and the
 * GFX_SHADOW_ALPHA of gfx_display.h. */
#define XB_RIBBON_ROWS      64
#define XB_RIBBON_COLS      64
#define XB_DELAY            166.66667f
#define XB_SHADOW_ALPHA     1.00f
#define XB_DRAW_ENTRY_MS    500          /* MENU_DRAW_ENTRY_DELAY (500000 us) */

#define XB_EASE_LINEAR      0
#define XB_EASE_OUT_QUAD    1
#define XB_EASE_OUT_CIRC    2
#define XB_EASE_OUT_EXPO    3
#define XB_EASE_OUT_BOUNCE  4
#define XB_EASE_IN_SINE     5
#define XB_EASING_ALPHA     XB_EASE_OUT_CIRC
#define XB_EASING_XY        XB_EASE_OUT_QUAD

#define XB_MAX_CATS         6
#define XB_MAX_ITEMS        14
#define XB_MAX_SUB          12
#define XB_MAX_TWEENS       320
#define XB_MAX_MSG_LINES    10
#define XB_MSG_LINE_LEN     72

#define XB_ATLAS_GRID       5            /* 5x5 icon cells */
#define XB_ICON_CELL        64
#define XB_ICON_TEX         (XB_ATLAS_GRID * XB_ICON_CELL)

#define XB_FONT_COLS        16
#define XB_FONT_ROWS        6            /* ASCII 32..126 = 95 glyphs */
#define XB_FONT_TEX_W       (XB_FONT_COLS * 8)
#define XB_FONT_TEX_H       (XB_FONT_ROWS * 16)

#define XB_PARTICLES        48

/* ── Tweens (gfx_animation port) ────────────────────────────────────── */

typedef struct {
    float      *subject;
    float       start;
    float       target;
    float       elapsed;                 /* ms */
    float       duration;                /* ms */
    int         easing;
    int         used;
    uintptr_t   tag;
} xb_tween_t;

static xb_tween_t s_tweens[XB_MAX_TWEENS];

static float xb_ease(int easing, float t)
{
    float f;
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    switch (easing) {
    case XB_EASE_OUT_QUAD:   return 1.0f - (1.0f - t) * (1.0f - t);
    case XB_EASE_OUT_CIRC:   f = 1.0f - t; return sqrtf(1.0f - f * f);
    case XB_EASE_OUT_EXPO:   return 1.0f - powf(2.0f, -10.0f * t);
    case XB_EASE_IN_SINE:    return 1.0f - cosf(t * (float)M_PI * 0.5f);
    case XB_EASE_OUT_BOUNCE:
        f = t;
        if (f < 1.0f / 2.75f)  return 7.5625f * f * f;
        if (f < 2.0f / 2.75f) {f -= 1.5f  / 2.75f; return 7.5625f * f * f + 0.75f;}
        if (f < 2.5f / 2.75f) {f -= 2.25f / 2.75f; return 7.5625f * f * f + 0.9375f;}
        f -= 2.625f / 2.75f;
        return 7.5625f * f * f + 0.984375f;
    case XB_EASE_LINEAR:
    default:                 return t;
    }
}

/* Retargeting an existing subject is what keeps a fast key repeat from
 * stacking up animations on one float, exactly like gfx_animation_push. */
static void xb_tween_push(float *subject, float target, float duration,
                          int easing, uintptr_t tag)
{
    int i, free_slot = -1;

    if (!subject)
        return;

    for (i = 0; i < XB_MAX_TWEENS; i++) {
        if (s_tweens[i].used && s_tweens[i].subject == subject)
            break;
        if (!s_tweens[i].used && free_slot < 0)
            free_slot = i;
    }

    if (i < XB_MAX_TWEENS && s_tweens[i].used) {
        s_tweens[i].start    = *subject;
        s_tweens[i].target   = target;
        s_tweens[i].elapsed  = 0.0f;
        s_tweens[i].duration = duration;
        s_tweens[i].easing   = easing;
        s_tweens[i].tag      = tag;
        return;
    }

    if (free_slot < 0)
        return;

    s_tweens[free_slot].used     = 1;
    s_tweens[free_slot].subject  = subject;
    s_tweens[free_slot].start    = *subject;
    s_tweens[free_slot].target   = target;
    s_tweens[free_slot].elapsed  = 0.0f;
    s_tweens[free_slot].duration = duration;
    s_tweens[free_slot].easing   = easing;
    s_tweens[free_slot].tag      = tag;
}

static void xb_tween_kill(uintptr_t tag)
{
    int i;
    for (i = 0; i < XB_MAX_TWEENS; i++)
        if (s_tweens[i].used && s_tweens[i].tag == tag) {
            s_tweens[i].used    = 0;
            s_tweens[i].subject = NULL;
        }
}

static void xb_tween_tick(float dt)
{
    int i;
    for (i = 0; i < XB_MAX_TWEENS; i++) {
        xb_tween_t *tw = &s_tweens[i];
        float t;
        if (!tw->used)
            continue;
        tw->elapsed += dt;
        t = tw->duration > 0.0f ? tw->elapsed / tw->duration : 1.0f;
        if (t >= 1.0f) {
            *tw->subject = tw->target;
            tw->used     = 0;
            tw->subject  = NULL;
            continue;
        }
        *tw->subject = tw->start + (tw->target - tw->start) * xb_ease(tw->easing, t);
    }
}

/* ── Content model ──────────────────────────────────────────────────── */

enum {
    XB_APP = 0,      /* launches a B-System window          */
    XB_TOGGLE,
    XB_RANGE,
    XB_ENUM,
    XB_CMD,          /* runs a shell builtin                */
    XB_SUB,          /* opens a nested list (menu depth 2)  */
    XB_TEXT          /* a line inside a nested list         */
};

/* Launch ids for XB_APP rows */
enum {
    L_NONE = 0, L_GTERM, L_TEDITOR, L_PAINT, L_AUDIO, L_ORCHESTRA,
    L_VOBJ, L_TAD, L_DRIVE, L_CHAT, L_KAGEE, L_QUAKE
};

/* Icon atlas cells */
enum {
    IC_CAT_APPS = 0, IC_CAT_SETTINGS, IC_CAT_VOLUME, IC_CAT_COMMANDS, IC_ARROW,
    IC_APP_TERM, IC_APP_EDITOR, IC_APP_PAINT, IC_APP_MUSIC, IC_APP_ORCHESTRA,
    IC_APP_VOBJ, IC_APP_TAD, IC_APP_DRIVE, IC_APP_CHAT, IC_APP_PHOTO,
    IC_GEAR, IC_SLIDER, IC_SPEAKER, IC_INFO, IC_PROMPT,
    IC_DROPLET, IC_FOLDER, IC_APP_QUAKE, IC_CLOCK, IC_BLANK
};

typedef struct {
    float x, y, alpha, label_alpha, zoom;
} xb_node_t;

typedef struct xb_item_s {
    const char   *label;
    const char   *sub;
    int           kind;
    int           launch;         /* XB_APP */
    int           value, max;     /* XB_TOGGLE / XB_RANGE / XB_ENUM index */
    const char   *opts;           /* XB_ENUM, '|' separated */
    const char   *cmd;            /* XB_CMD */
    int           icon;
    const char  **live;           /* XB_TEXT: mutable line storage */
    int           live_count;
    struct xb_item_s *sub_items;  /* XB_SUB */
    int           sub_count;
    xb_node_t     node;
} xb_item_t;

typedef struct {
    const char *label;
    const char *sub;
    int         icon;
    xb_item_t  *items;
    int         count;
    int         selection;
    xb_node_t   node;             /* the band icon */
} xb_cat_t;

/* Settings rows that the renderer itself reads back */
enum {
    S_THEME = 0, S_LANG, S_WAVE, S_PARTICLES, S_SHADOWS, S_BRIGHT,
    S_FADE
};

static xb_item_t s_items_apps[] = {
    {"Terminal",       "Console access to the B-System shell",     XB_APP, L_GTERM,     0,0,NULL,NULL,IC_APP_TERM,   0,0,0,0,{0}},
    {"Text Editor",    "Edit plain text and TRON Code documents",  XB_APP, L_TEDITOR,   0,0,NULL,NULL,IC_APP_EDITOR, 0,0,0,0,{0}},
    {"Paint",          "Draw bitmaps into the memory card",        XB_APP, L_PAINT,     0,0,NULL,NULL,IC_APP_PAINT,  0,0,0,0,{0}},
    {"Audio Player",   "Play back music from a volume",            XB_APP, L_AUDIO,     0,0,NULL,NULL,IC_APP_MUSIC,  0,0,0,0,{0}},
    {"Orchestra",      "Sequencer for the sound synthesis unit",   XB_APP, L_ORCHESTRA, 0,0,NULL,NULL,IC_APP_ORCHESTRA,0,0,0,0,{0}},
    {"Object Manager", "Browse the virtual object database",       XB_APP, L_VOBJ,      0,0,NULL,NULL,IC_APP_VOBJ,   0,0,0,0,{0}},
    {"TAD Browser",    "Look inside TAD archives",                 XB_APP, L_TAD,       0,0,NULL,NULL,IC_APP_TAD,    0,0,0,0,{0}},
    {"DriveSetup",     "Partition and format storage devices",     XB_APP, L_DRIVE,     0,0,NULL,NULL,IC_APP_DRIVE,  0,0,0,0,{0}},
    {"Chat",           "Open a BeOS-style chat session",           XB_APP, L_CHAT,      0,0,NULL,NULL,IC_APP_CHAT,   0,0,0,0,{0}},
    {"Photo Viewer",   "Display saved Kagee images",               XB_APP, L_KAGEE,     0,0,NULL,NULL,IC_APP_PHOTO,  0,0,0,0,{0}},
    {"Quake",          "Software-rendered OpenGL demo game",       XB_APP, L_QUAKE,     0,0,NULL,NULL,IC_APP_QUAKE,  0,0,0,0,{0}},
};

static char s_sys_lines[XB_MAX_SUB][XB_MSG_LINE_LEN];
static const char *s_sys_live[XB_MAX_SUB];

static xb_item_t s_items_sysdata[] = {
    {"", "", XB_TEXT, L_NONE, 0,0,NULL,NULL,IC_INFO, 0,0,0,0,{0}},
    {"", "", XB_TEXT, L_NONE, 0,0,NULL,NULL,IC_INFO, 0,0,0,0,{0}},
    {"", "", XB_TEXT, L_NONE, 0,0,NULL,NULL,IC_INFO, 0,0,0,0,{0}},
    {"", "", XB_TEXT, L_NONE, 0,0,NULL,NULL,IC_INFO, 0,0,0,0,{0}},
    {"", "", XB_TEXT, L_NONE, 0,0,NULL,NULL,IC_INFO, 0,0,0,0,{0}},
    {"", "", XB_TEXT, L_NONE, 0,0,NULL,NULL,IC_INFO, 0,0,0,0,{0}},
    {"", "", XB_TEXT, L_NONE, 0,0,NULL,NULL,IC_INFO, 0,0,0,0,{0}},
};

static xb_item_t s_items_settings[] = {
    {"Theme",            "Colour of the background gradient",    XB_ENUM,   L_NONE, 0, 2, "Deep Blue|Graphite|Gold", NULL, IC_GEAR,   0,0,0,0,{0}},
    {"Language",         "System display language",              XB_ENUM,   L_NONE, 0, 2, "en-US|ja-JP|zh-CN",       NULL, IC_GEAR,   0,0,0,0,{0}},
    {"Wave Background",  "Animated waving surface behind the bar",XB_TOGGLE,L_NONE, 1, 0, NULL, NULL, IC_SLIDER, 0,0,0,0,{0}},
    {"Wave Particles",   "Sparkles rising off the waving surface",XB_TOGGLE, L_NONE, 1, 0, NULL, NULL, IC_SLIDER, 0,0,0,0,{0}},
    {"Icon Shadows",     "Drop shadow under icons and text",     XB_TOGGLE, L_NONE, 1, 0, NULL, NULL, IC_SLIDER, 0,0,0,0,{0}},
    {"Screen Brightness","Backlight level of the display",       XB_RANGE,  L_NONE, 90,100, NULL, NULL, IC_SLIDER, 0,0,0,0,{0}},
    {"Edge Fade",        "Fade of list rows near the screen edge",XB_RANGE, L_NONE, 100,100,NULL, NULL, IC_SLIDER, 0,0,0,0,{0}},
    {"System Data",      "Version, memory and volume details",   XB_SUB,    L_NONE, 0, 0,  NULL, NULL, IC_INFO,
        0, 0, s_items_sysdata, 0, {0}},
};

static xb_item_t s_items_volume[] = {
    {"Master Volume","Overall output level",      XB_RANGE, L_NONE, 70,100,NULL,NULL,IC_SPEAKER,0,0,0,0,{0}},
    {"Music",       "Background music channel",  XB_RANGE, L_NONE, 60,100,NULL,NULL,IC_SPEAKER,0,0,0,0,{0}},
    {"Effect",      "Sound effects and bling",   XB_RANGE, L_NONE, 80,100,NULL,NULL,IC_SPEAKER,0,0,0,0,{0}},
    {"Voice Alert", "System voice alert level",  XB_RANGE, L_NONE, 50,100,NULL,NULL,IC_SPEAKER,0,0,0,0,{0}},
    {"Microphone",  "Input gain",                XB_RANGE, L_NONE, 30,100,NULL,NULL,IC_SPEAKER,0,0,0,0,{0}},
};

static xb_item_t s_items_commands[] = {
    {"List root directory",  "Run the shell builtin: ls /",      XB_CMD, L_NONE, 0,0,NULL, "ls /",  IC_PROMPT,0,0,0,0,{0}},
    {"Volume table",         "Run the shell builtin: fs",        XB_CMD, L_NONE, 0,0,NULL, "fs",    IC_PROMPT,0,0,0,0,{0}},
    {"Storage usage",        "Run the shell builtin: df",        XB_CMD, L_NONE, 0,0,NULL, "df",    IC_PROMPT,0,0,0,0,{0}},
    {"Kernel information",   "Run the shell builtin: info",      XB_CMD, L_NONE, 0,0,NULL, "info",  IC_PROMPT,0,0,0,0,{0}},
    {"Sync all volumes",     "Run the shell builtin: sync",      XB_CMD, L_NONE, 0,0,NULL, "sync",  IC_PROMPT,0,0,0,0,{0}},
};

static xb_cat_t s_cats[] = {
    {"Applications","Launch B-System applications", IC_CAT_APPS,     s_items_apps,     0, 0, {0}},
    {"Settings",    "Configure the B-System",       IC_CAT_SETTINGS, s_items_settings, 0, 0, {0}},
    {"Volume",      "Audio output and input levels",IC_CAT_VOLUME,   s_items_volume,   0, 0, {0}},
    {"Commands",    "Run B-System shell commands",  IC_CAT_COMMANDS, s_items_commands, 0, 0, {0}},
};
static int s_cat_count = 4;

/* ── Menu state (xmb_handle_t members we port) ──────────────────────── */

static float s_alpha        = 1.0f;   /* xmb->alpha, whole-menu         */
static float s_alpha_list   = 1.0f;   /* xmb->alpha_list, list fade     */
static float s_x            = 0.0f;   /* xmb->x, depth offset           */
static float s_band_x       = 0.0f;   /* xmb->categories_x_pos          */
static int   s_cat          = 0;      /* xmb->categories_active_idx     */
static int   s_depth        = 1;      /* xmb->depth                     */
static int   s_sub_sel      = 0;
static int   s_draw_entry_until = 0;  /* xmb->draw_entry_hold_until     */

/* Layout, recomputed on resize: xmb_layout_ps3() + xmb_layout_common() */
static float s_sf, s_icon_size, s_spacing_h, s_spacing_v, s_margin_top,
             s_margin_left, s_label_left, s_label_top, s_setting_left,
             s_font, s_font2, s_cursor_size, s_shadow_offset,
             s_title_left, s_title_top, s_title_bottom, s_dialog_margin,
             s_above_subitem = 1.5f, s_above_item = -1.0f,
             s_active_factor = 3.0f, s_under_item = 5.0f;
static int   s_w = 0, s_h = 0;

/* Settings read back by the renderer */
static int   s_theme      = 0;
static int   s_wave       = 1;
static int   s_particles  = 1;
static int   s_shadows    = 1;
static float s_brightness = 0.90f;
static int   s_fade       = 100;

/* Textures */
static GLuint s_tex_icons = 0, s_tex_font = 0;

/* Message box */
static char s_msg[XB_MAX_MSG_LINES][XB_MSG_LINE_LEN];
static int  s_msg_lines = 0;
static int  s_msg_more  = 0;
static float s_msg_alpha = 0.0f;
static int  s_msg_open   = 0;

/* Ribbon cache (clip-space sheet + baked brightness) */
static float s_rib_e0[XB_RIBBON_ROWS][XB_RIBBON_COLS];
static float s_rib_e1[XB_RIBBON_ROWS][XB_RIBBON_COLS];
static float s_rib_e2[XB_RIBBON_ROWS][XB_RIBBON_COLS];
static float s_rib_bri[XB_RIBBON_ROWS][XB_RIBBON_COLS];
static float s_effect_time = 0.0f;

typedef struct {
    float x, y, vx, vy, life, ttl, size;
} xb_particle_t;
static xb_particle_t s_parts[XB_PARTICLES];
static float s_part_spawn = 0.0f;

static WND         *s_wnd  = NULL;
static EGL_SURFACE *s_surf = NULL;
static ID           s_tsk  = 0;
static SYSTIME      s_last_time = 0;
static int          s_frame_ms  = 16;

/* ── Small helpers ──────────────────────────────────────────────────── */

static float xb_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float xb_minf(float a, float b) { return a < b ? a : b; }
static float xb_maxf(float a, float b) { return a > b ? a : b; }

/* ── Layout ─────────────────────────────────────────────────────────── */

static void xb_layout(int width, int height)
{
    /* xmb_use_ps3_layout(): the PS3 metrics are used above 320x240 */
    float sf = (float)width / 1920.0f;
    unsigned new_font;
    float margins_title = 30.0f;   /* DEFAULT_XMB_TITLE_MARGIN 3 * 10 */

    (void)height;

    if (sf < 0.1f)
        sf = 0.1f;

    new_font       = (unsigned)(32.0f * sf);
    if (new_font < 7)
        new_font   = 7;

    s_sf           = sf;
    s_font         = (float)new_font;
    s_font2        = xb_maxf(22.0f * sf, 6.0f);
    s_icon_size    = 128.0f * sf;
    s_cursor_size  = 64.0f * sf;
    s_spacing_h    = 192.0f * sf;
    s_spacing_v    = 64.0f * sf;
    s_margin_top   = 272.0f * sf;
    s_margin_left  = 336.0f * sf;
    s_label_left   = 85.0f * sf;
    s_label_top    = s_font / 3.0f;
    s_setting_left = 660.0f * sf;
    s_title_left   = margins_title * sf + 4.0f * sf;
    s_title_top    = margins_title * sf
                   + ((float)new_font - ((float)(new_font / 6) * sf));
    s_title_bottom = margins_title * sf + 4.0f * sf;
    s_dialog_margin= s_font * 2.0f;

    s_shadow_offset = 4.0f * sf;
    if (s_shadow_offset < 1.0f) s_shadow_offset = 1.0f;
    if (s_shadow_offset > 2.0f) s_shadow_offset = 2.0f;

    s_above_subitem = 1.5f;
    s_above_item    = -1.0f;
    s_active_factor = 3.0f;
    s_under_item    = 5.0f;
}

/* xmb_item_y() */
static float xb_item_y(int i, int current)
{
    if (i < current) {
        if (s_depth > 1)
            return s_spacing_v * (i - current + s_above_subitem);
        return s_spacing_v * (i - current + s_above_item);
    }
    if (i == current)
        return s_spacing_v * s_active_factor;
    return s_spacing_v * (i - current + s_under_item);
}

/* xmb_calculate_visible_range() */
static void xb_visible_range(int count, int current, int *first, int *last)
{
    int j;
    *first = 0;
    *last  = count > 0 ? count - 1 : 0;

    if (current) {
        for (j = current; j-- > 0;) {
            float bottom = xb_item_y(j, current) + s_margin_top + s_icon_size;
            if (bottom < 0.0f)
                break;
            *first = j;
        }
    }

    for (j = current + 1; j < count; j++) {
        float top = xb_item_y(j, current) + s_margin_top;
        if (top > (float)s_h)
            break;
        *last = j;
    }
}

static xb_item_t *xb_cur_items(int *count)
{
    if (s_depth > 1) {
        xb_item_t *it = &s_cats[s_cat].items[s_cats[s_cat].selection];
        *count = it->sub_count;
        return it->sub_items;
    }
    *count = s_cats[s_cat].count;
    return s_cats[s_cat].items;
}

static int xb_cur_selection(void)
{
    return s_depth > 1 ? s_sub_sel : s_cats[s_cat].selection;
}

static void xb_set_selection(int v)
{
    if (s_depth > 1)
        s_sub_sel = v;
    else
        s_cats[s_cat].selection = v;
}

/* ── Texture baking ─────────────────────────────────────────────────── */

static float xb_sd_box(float x, float y, float cx, float cy, float hw, float hh)
{
    float dx = fabsf(x - cx) - hw, dy = fabsf(y - cy) - hh;
    float mx = dx > 0.0f ? dx : 0.0f;
    float my = dy > 0.0f ? dy : 0.0f;
    float inside = dx > dy ? dx : dy;
    return inside < 0.0f ? inside : sqrtf(mx * mx + my * my);
}

static float xb_sd_rbox(float x, float y, float cx, float cy,
                        float hw, float hh, float r)
{
    return xb_sd_box(x, y, cx, cy, hw - r, hh - r) - r;
}

static float xb_sd_disc(float x, float y, float cx, float cy, float r)
{
    float a = x - cx, b = y - cy;
    return sqrtf(a * a + b * b) - r;
}

static float xb_sd_ring(float x, float y, float cx, float cy, float r, float w)
{
    /* Annulus of half-thickness w: the distance to the circle, widened */
    return fabsf(xb_sd_disc(x, y, cx, cy, r)) - w;
}

static float xb_sd_capsule(float x, float y, float ax, float ay,
                           float bx, float by, float w)
{
    float vx = bx - ax, vy = by - ay;
    float wx = x - ax,  wy = y - ay;
    float len2 = vx * vx + vy * vy;
    float t = len2 > 1e-6f ? (wx * vx + wy * vy) / len2 : 0.0f;
    float px, py;
    t = xb_clampf(t, 0.0f, 1.0f);
    px = x - (ax + t * vx);
    py = y - (ay + t * vy);
    return sqrtf(px * px + py * py) - w;
}

#define UN(a, b)          ((a) < (b)  ? (a) : (b))        /* CSG union        */
#define INTER(a, b)       ((a) > (b)  ? (a) : (b))        /* CSG intersection */
#define SUB(a, b)         ((a) > (-(b)) ? (a) : (-(b)))   /* CSG difference   */
#define STROKE(a, w)      (fabsf(a) - (w))                /* outline          */
#define XB_W              0.05f            /* stroke weight, in cell units    */

/* Distance to one segment, always positive */
static float xb_sd_seg(float x, float y, float ax, float ay, float bx, float by)
{
    float pax = x - ax, pay = y - ay;
    float bax = bx - ax, bay = by - ay;
    float len2 = bax * bax + bay * bay;
    float h = len2 > 1e-6f ? pax * bax + pay * bay : 0.0f;
    float dx, dy;

    h = len2 > 1e-6f ? xb_clampf(h / len2, 0.0f, 1.0f) : 0.0f;
    dx = pax - bax * h;
    dy = pay - bay * h;
    return sqrtf(dx * dx + dy * dy);
}

/* Exact distance to a triangle: nearest of the three edges, signed by the
 * side of each edge the point sits on */
static float xb_sd_tri(float x, float y, float ax, float ay,
                       float bx, float by, float cx, float cy)
{
    float d   = UN(UN(xb_sd_seg(x, y, ax, ay, bx, by),
                      xb_sd_seg(x, y, bx, by, cx, cy)),
                   xb_sd_seg(x, y, cx, cy, ax, ay));
    float s1 = (bx - ax) * (y - ay) - (by - ay) * (x - ax);
    float s2 = (cx - bx) * (y - by) - (cy - by) * (x - bx);
    float s3 = (ax - cx) * (y - cy) - (ay - cy) * (x - cx);
    int neg = (s1 < 0.0f) + (s2 < 0.0f) + (s3 < 0.0f);
    int pos = (s1 > 0.0f) + (s2 > 0.0f) + (s3 > 0.0f);

    return (neg && pos) ? d : -d;
}

/* Coverage of one icon cell, in centred coordinates of -0.5 .. 0.5 with y
 * downwards.  Every glyph is a white silhouette at one stroke weight, which is
 * how the PS3 bar's own pictograms read at icon_size. */
static float xb_icon_cov(int cell, float x, float y)
{
    float d = 1.0f;
    int i;

    switch (cell) {
    case IC_CAT_APPS:
        /* The applications group: four rounded squares */
        d = UN(UN(xb_sd_rbox(x, y, -0.20f, -0.20f, 0.14f, 0.14f, 0.05f),
                  xb_sd_rbox(x, y,  0.20f, -0.20f, 0.14f, 0.14f, 0.05f)),
               UN(xb_sd_rbox(x, y, -0.20f,  0.20f, 0.14f, 0.14f, 0.05f),
                  xb_sd_rbox(x, y,  0.20f,  0.20f, 0.14f, 0.14f, 0.05f)));
        break;

    case IC_CAT_SETTINGS:
    case IC_GEAR:
        /* Ring, eight studs and a hub */
        d = xb_sd_ring(x, y, 0.0f, 0.0f, 0.28f, XB_W);
        for (i = 0; i < 8; i++) {
            float a = (float)i * ((float)M_PI / 4.0f);
            d = UN(d, xb_sd_disc(x, y, cosf(a) * 0.36f, sinf(a) * 0.36f, XB_W));
        }
        d = UN(d, xb_sd_disc(x, y, 0.0f, 0.0f, 0.12f));
        break;

    case IC_CAT_VOLUME:
    case IC_SPEAKER:
        /* Throat, cone and two sound arcs on the right */
        d = xb_sd_rbox(x, y, -0.30f, 0.0f, 0.07f, 0.14f, 0.03f);
        d = UN(d, xb_sd_tri(x, y, -0.20f, -0.14f, -0.20f, 0.14f, 0.04f,  0.32f));
        d = UN(d, xb_sd_tri(x, y, -0.20f, -0.14f,  0.04f,  0.32f, 0.04f, -0.32f));
        d = UN(d, INTER(xb_sd_ring(x, y, 0.06f, 0.0f, 0.30f, 0.04f), x - 0.16f));
        d = UN(d, INTER(xb_sd_ring(x, y, 0.06f, 0.0f, 0.44f, 0.04f), x - 0.16f));
        break;

    case IC_CAT_COMMANDS:
    case IC_PROMPT:
    case IC_APP_TERM:
        /* Window frame with a prompt inside */
        d = STROKE(xb_sd_rbox(x, y, 0.0f, 0.0f, 0.36f, 0.30f, 0.06f), XB_W * 0.7f);
        d = UN(d, xb_sd_capsule(x, y, -0.20f, -0.12f, -0.05f, 0.0f, 0.04f));
        d = UN(d, xb_sd_capsule(x, y, -0.05f,  0.0f, -0.20f, 0.12f, 0.04f));
        d = UN(d, xb_sd_capsule(x, y,  0.03f,  0.14f,  0.22f, 0.14f, 0.04f));
        break;

    case IC_ARROW:
        d = xb_sd_capsule(x, y, -0.12f, -0.24f, 0.16f, 0.0f, 0.06f);
        d = UN(d, xb_sd_capsule(x, y, 0.16f, 0.0f, -0.12f, 0.24f, 0.06f));
        break;

    case IC_APP_EDITOR:
        /* A page ruled with three lines of text */
        d = STROKE(xb_sd_rbox(x, y, 0.0f, -0.02f, 0.28f, 0.34f, 0.04f), XB_W * 0.7f);
        d = UN(d, xb_sd_capsule(x, y, -0.14f, -0.20f, 0.14f, -0.20f, 0.032f));
        d = UN(d, xb_sd_capsule(x, y, -0.14f, -0.06f, 0.14f, -0.06f, 0.032f));
        d = UN(d, xb_sd_capsule(x, y, -0.14f,  0.08f, 0.14f,  0.08f, 0.032f));
        d = UN(d, xb_sd_capsule(x, y, -0.14f,  0.22f, 0.02f,  0.22f, 0.032f));
        break;

    case IC_APP_PAINT:
        /* Palette: a ring with three wells and a brush */
        d = SUB(xb_sd_disc(x, y, 0.0f, 0.04f, 0.34f),
                xb_sd_disc(x, y, 0.0f, 0.04f, 0.34f - XB_W * 1.4f));
        d = UN(d, xb_sd_disc(x, y,  0.16f, -0.16f, 0.06f));
        d = UN(d, xb_sd_disc(x, y, -0.18f, -0.04f, 0.06f));
        d = UN(d, xb_sd_disc(x, y, -0.02f,  0.22f, 0.06f));
        d = UN(d, xb_sd_capsule(x, y, 0.20f, -0.38f, 0.38f, -0.20f, 0.05f));
        break;

    case IC_APP_MUSIC:
        d = UN(xb_sd_disc(x, y, -0.16f,  0.20f, 0.11f),
               xb_sd_disc(x, y,  0.18f,  0.12f, 0.11f));
        d = UN(d, xb_sd_capsule(x, y, -0.06f, 0.20f, -0.06f, -0.26f, 0.04f));
        d = UN(d, xb_sd_capsule(x, y,  0.28f, 0.12f,  0.28f, -0.34f, 0.04f));
        d = UN(d, xb_sd_capsule(x, y, -0.06f, -0.26f,  0.28f, -0.34f, 0.05f));
        break;

    case IC_APP_ORCHESTRA:
        d = xb_sd_capsule(x, y, -0.30f, -0.18f, -0.30f, 0.18f, 0.045f);
        d = UN(d, xb_sd_capsule(x, y, -0.15f, -0.30f, -0.15f, 0.30f, 0.045f));
        d = UN(d, xb_sd_capsule(x, y,  0.00f, -0.12f,  0.00f, 0.12f, 0.045f));
        d = UN(d, xb_sd_capsule(x, y,  0.15f, -0.32f,  0.15f, 0.32f, 0.045f));
        d = UN(d, xb_sd_capsule(x, y,  0.30f, -0.20f,  0.30f, 0.20f, 0.045f));
        break;

    case IC_APP_VOBJ:
        /* Wireframe cube: the virtual object database */
        d = xb_sd_capsule(x, y, 0.0f, -0.32f, 0.30f, -0.14f, 0.04f);
        d = UN(d, xb_sd_capsule(x, y, 0.30f, -0.14f, 0.30f, 0.18f, 0.04f));
        d = UN(d, xb_sd_capsule(x, y, 0.30f, 0.18f, 0.0f, 0.36f, 0.04f));
        d = UN(d, xb_sd_capsule(x, y, 0.0f, 0.36f, -0.30f, 0.18f, 0.04f));
        d = UN(d, xb_sd_capsule(x, y, -0.30f, 0.18f, -0.30f, -0.14f, 0.04f));
        d = UN(d, xb_sd_capsule(x, y, -0.30f, -0.14f, 0.0f, 0.02f, 0.04f));
        d = UN(d, xb_sd_capsule(x, y, 0.0f, 0.02f, 0.30f, -0.14f, 0.04f));
        d = UN(d, xb_sd_capsule(x, y, 0.0f, 0.02f, 0.0f, 0.36f, 0.04f));
        break;

    case IC_APP_TAD:
    case IC_FOLDER:
        d = xb_sd_rbox(x, y, 0.0f, 0.10f, 0.36f, 0.22f, 0.05f);
        d = UN(d, xb_sd_rbox(x, y, -0.18f, -0.18f, 0.16f, 0.08f, 0.03f));
        break;

    case IC_APP_DRIVE:
        d = xb_sd_ring(x, y, 0.0f, 0.0f, 0.32f, 0.10f);
        d = UN(d, xb_sd_disc(x, y, 0.0f, 0.0f, 0.09f));
        break;

    case IC_APP_CHAT:
        d = xb_sd_rbox(x, y, 0.02f, -0.08f, 0.34f, 0.24f, 0.12f);
        d = UN(d, xb_sd_tri(x, y, -0.14f, 0.10f, -0.26f, 0.38f, 0.02f, 0.16f));
        break;

    case IC_APP_PHOTO:
        /* Picture frame, sun and a mountain range inside it */
        d = STROKE(xb_sd_rbox(x, y, 0.0f, 0.0f, 0.36f, 0.28f, 0.04f), XB_W * 0.7f);
        d = UN(d, xb_sd_disc(x, y, 0.20f, -0.12f, 0.06f));
        d = UN(d, SUB(xb_sd_tri(x, y, -0.34f, 0.26f, -0.06f, -0.04f, 0.16f, 0.26f),
                      STROKE(xb_sd_rbox(x, y, 0.0f, 0.0f, 0.36f, 0.28f, 0.04f),
                             XB_W * 0.7f)));
        break;

    case IC_APP_QUAKE:
        d = xb_sd_ring(x, y, 0.0f, 0.0f, 0.28f, 0.045f);
        d = UN(d, xb_sd_capsule(x, y, 0.0f, -0.46f, 0.0f, -0.34f, 0.045f));
        d = UN(d, xb_sd_capsule(x, y, 0.0f,  0.34f, 0.0f,  0.46f, 0.045f));
        d = UN(d, xb_sd_capsule(x, y, -0.46f, 0.0f, -0.34f, 0.0f, 0.045f));
        d = UN(d, xb_sd_capsule(x, y,  0.34f, 0.0f,  0.46f, 0.0f, 0.045f));
        break;

    case IC_INFO:
        d = xb_sd_disc(x, y, 0.0f, -0.28f, 0.075f);
        d = UN(d, xb_sd_capsule(x, y, 0.0f, -0.10f, 0.0f, 0.30f, 0.065f));
        break;

    case IC_CLOCK:
        d = xb_sd_ring(x, y, 0.0f, 0.0f, 0.32f, 0.045f);
        d = UN(d, xb_sd_capsule(x, y, 0.0f, 0.0f, 0.0f, -0.18f, 0.035f));
        d = UN(d, xb_sd_capsule(x, y, 0.0f, 0.0f, 0.14f, 0.06f, 0.035f));
        break;

    case IC_SLIDER:
        d = xb_sd_capsule(x, y, -0.34f, 0.08f, 0.34f, 0.08f, 0.03f);
        d = UN(d, xb_sd_disc(x, y, -0.08f, -0.10f, 0.12f));
        break;

    case IC_DROPLET:
        d = xb_sd_disc(x, y, 0.0f, 0.0f, 0.30f);
        break;

    case IC_BLANK:
    default:
        return 0.0f;
    }

    /* Soft edge: 0.022 cell units of falloff */
    return xb_clampf(0.5f - d / 0.044f, 0.0f, 1.0f);
}

/* Droplets use a radial falloff instead of a hard SDF edge */
static float xb_droplet_cov(float x, float y)
{
    float r = sqrtf(x * x + y * y) * 2.0f;
    float v = 1.0f - r;
    if (v < 0.0f) v = 0.0f;
    return v * v;
}

static void xb_put_rgba(UB *px, int texw, int cx, int cy,
                        UB r, UB g, UB b, UB a)
{
    int o = (cy * texw + cx) * 4;
    px[o + 0] = r;
    px[o + 1] = g;
    px[o + 2] = b;
    px[o + 3] = a;
}

/* Bake the 5x5 icon atlas and the 8x16 bitmap-font atlas, then upload both.
 * Row 0 of the uploaded image is texture row 0 (v = 0), the convention
 * backend_virgl.c documents.  glTexImage2D copies what it is given, so the two
 * bake buffers are freed as soon as the upload is done rather than kept in
 * .bss for the life of the image. */
static void xb_bake_textures(void)
{
    GLuint tex[2];
    size_t icon_bytes = (size_t)XB_ICON_TEX * XB_ICON_TEX * 4;
    size_t font_bytes = (size_t)XB_FONT_TEX_W * XB_FONT_TEX_H * 4;
    UB *icons = (UB *)malloc(icon_bytes);
    UB *font  = (UB *)malloc(font_bytes);
    int cell, px, py, i;

    if (!icons || !font) {
        if (icons) free(icons);
        if (font)  free(font);
        uart_puts_raw("[GL] xmb: no memory for the icon atlas\n");
        return;
    }

    memset(icons, 0, icon_bytes);
    for (cell = 0; cell < IC_BLANK; cell++) {
        int ox = (cell % XB_ATLAS_GRID) * XB_ICON_CELL;
        int oy = (cell / XB_ATLAS_GRID) * XB_ICON_CELL;
        for (py = 0; py < XB_ICON_CELL; py++) {
            for (px = 0; px < XB_ICON_CELL; px++) {
                float x = (float)px / (float)(XB_ICON_CELL - 1) - 0.5f;
                float y = (float)py / (float)(XB_ICON_CELL - 1) - 0.5f;
                float cov = (cell == IC_DROPLET) ? xb_droplet_cov(x, y)
                                                 : xb_icon_cov(cell, x, y);
                if (cov <= 0.0f)
                    continue;
                xb_put_rgba(icons, XB_ICON_TEX, ox + px, oy + py,
                            248, 250, 255, (UB)(cov * 255.0f));
            }
        }
    }

    memset(font, 0, font_bytes);
    for (i = 0; i < XB_FONT_COLS * XB_FONT_ROWS; i++) {
        int code = 32 + i;
        H gw = 8, gh = 16;
        const UB *bmp;
        int gx, gy, ox, oy;
        if (code > 126)
            break;
        bmp = get_glyph_bitmap((TC)code, &gw, &gh);
        if (!bmp || gw != 8 || gh != 16)
            continue;
        ox = (i % XB_FONT_COLS) * 8;
        oy = (i / XB_FONT_COLS) * 16;
        for (gy = 0; gy < 16; gy++) {
            for (gx = 0; gx < 8; gx++) {
                if (bmp[gy] & (0x80 >> gx))
                    xb_put_rgba(font, XB_FONT_TEX_W, ox + gx, oy + gy,
                                255, 255, 255, 255);
            }
        }
    }

    glGenTextures(2, tex);
    s_tex_icons = tex[0];
    s_tex_font  = tex[1];

    glBindTexture(GL_TEXTURE_2D, s_tex_icons);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, XB_ICON_TEX, XB_ICON_TEX, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, icons);

    glBindTexture(GL_TEXTURE_2D, s_tex_font);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, XB_FONT_TEX_W, XB_FONT_TEX_H, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, font);

    glBindTexture(GL_TEXTURE_2D, 0);

    free(icons);
    free(font);
}

/* ── Ribbon: the waving surface ─────────────────────────────────────── */

/* iqhash/noise/xmb_noise2 from pipeline_xmb_ribbon.cg.h, and the vertex stage
 * of that shader, evaluated per grid point.  vEC is kept so the fragment
 * stage's normal can be recovered from grid differences. */
static float xb_frac(float v) { return v - floorf(v); }

static float xb_iqhash(float n) { return xb_frac(sinf(n) * 43758.5453f); }

static float xb_noise3(float x, float y, float z)
{
    float px = floorf(x), py = floorf(y), pz = floorf(z);
    float fx = xb_frac(x), fy = xb_frac(y), fz = xb_frac(z);
    float n  = px + py * 57.0f + 113.0f * pz;
    float a, b, c1, c2;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    a  = xb_iqhash(n) + (xb_iqhash(n + 1.0f) - xb_iqhash(n)) * fx;
    b  = xb_iqhash(n + 57.0f)
       + (xb_iqhash(n + 58.0f) - xb_iqhash(n + 57.0f)) * fx;
    c1 = a + (b - a) * fy;
    a  = xb_iqhash(n + 113.0f)
       + (xb_iqhash(n + 114.0f) - xb_iqhash(n + 113.0f)) * fx;
    b  = xb_iqhash(n + 170.0f)
       + (xb_iqhash(n + 171.0f) - xb_iqhash(n + 170.0f)) * fx;
    c2 = a + (b - a) * fy;
    return c1 + (c2 - c1) * fz;
}

static void xb_ribbon_step(float t)
{
    int r, c;
    static float ex[XB_RIBBON_ROWS][XB_RIBBON_COLS];
    static float ey[XB_RIBBON_ROWS][XB_RIBBON_COLS];
    static float ez[XB_RIBBON_ROWS][XB_RIBBON_COLS];

    for (r = 0; r < XB_RIBBON_ROWS; r++) {
        float gz = (float)r / (float)(XB_RIBBON_ROWS - 1) * 2.0f - 1.0f;
        for (c = 0; c < XB_RIBBON_COLS; c++) {
            float gx = (float)c / (float)(XB_RIBBON_COLS - 1) * 2.0f - 1.0f;
            float nb = cosf(gz * 4.0f) * cosf(gz + t / 10.0f + gx);
            float v3x = (gx - t / 5.0f) / 4.0f;
            float v3y = -t / 100.0f;
            float v3z = gz - t / 10.0f;
            float nz  = xb_noise3(v3x * 7.0f, v3y * 7.0f, v3z * 7.0f);
            float vy  = nb / 8.0f - nz / 15.0f
                      - cosf(gx * 2.0f - t / 2.0f) / 5.0f + 0.3f;
            ex[r][c] = gx;
            ey[r][c] = -vy;
            ez[r][c] = gz - nz / 15.0f;
        }
    }

    /* Fragment stage: normal = cross(ddx(vEC), -ddy(vEC)); the sheet is
     * parameterised by the grid, so the screen-space derivatives are the grid
     * derivatives mapped through the (gx, ey) -> (sx, sy) Jacobian.  That
     * mapping divides by d(ey)/d(row), which leaves the normal's z component
     * positive everywhere, i.e. |cross|.z of the grid differences. */
    for (r = 0; r < XB_RIBBON_ROWS; r++) {
        int r0 = r > 0 ? r - 1 : r;
        int r1 = r < XB_RIBBON_ROWS - 1 ? r + 1 : r;
        for (c = 0; c < XB_RIBBON_COLS; c++) {
            int c0 = c > 0 ? c - 1 : c;
            int c1 = c < XB_RIBBON_COLS - 1 ? c + 1 : c;
            float ax = ex[r][c1] - ex[r][c0];
            float ay = ey[r][c1] - ey[r][c0];
            float az = ez[r][c1] - ez[r][c0];
            float bx = ex[r1][c] - ex[r0][c];
            float by = ey[r1][c] - ey[r0][c];
            float bz = ez[r1][c] - ez[r0][c];
            float nx = ay * bz - az * by;
            float ny = az * bx - ax * bz;
            float nz = ax * by - ay * bx;
            float len = sqrtf(nx * nx + ny * ny + nz * nz);
            float dot_up, cc, bright;
            if (len < 1e-6f)
                len = 1e-6f;
            dot_up = fabsf(nz) / len;
            cc     = 1.0f - dot_up;
            bright = (1.0f - cosf(cc * cc)) / 13.0f;
            s_rib_bri[r][c] = bright;
        }
    }

    /* Keep the sheet itself, in clip space, for drawing and for the particles */
    for (r = 0; r < XB_RIBBON_ROWS; r++)
        for (c = 0; c < XB_RIBBON_COLS; c++) {
            s_rib_e0[r][c] = ex[r][c];
            s_rib_e1[r][c] = ey[r][c];
            s_rib_e2[r][c] = ez[r][c];
        }
}

/* xmb_init_ribbon(): a zigzag triangle strip over the grid, with the odd rows
 * walked in reverse so the strip stays connected.
 *
 * The compositing is the driver's own: gfx_drv gl2 sets
 * glBlendFunc(GL_DST_COLOR, GL_ONE) for the ribbon programs, so the sheet
 * brightens whatever the gradient left behind it by (1 + c) rather than laying
 * colour over it.  That is what makes the waves read as an embossed cloth. */
static void xb_draw_ribbon(float alpha)
{
    int r, c;

    if (alpha <= 0.0f)
        return;

    glBlendFunc(GL_DST_COLOR, GL_ONE);
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_TRIANGLE_STRIP);
    for (r = 0; r < XB_RIBBON_ROWS - 1; r++) {
        for (c = 0; c < XB_RIBBON_COLS; c++) {
            int col = (r & 1) ? (XB_RIBBON_COLS - 1 - c) : c;
            float b = s_rib_bri[r][col];
            glColor4f(b, b, b, 1.0f);
            glVertex2f((s_rib_e0[r][col] + 1.0f) * 0.5f * (float)s_w,
                       (1.0f - s_rib_e1[r][col]) * 0.5f * (float)s_h);
            b = s_rib_bri[r + 1][col];
            glColor4f(b, b, b, 1.0f);
            glVertex2f((s_rib_e0[r + 1][col] + 1.0f) * 0.5f * (float)s_w,
                       (1.0f - s_rib_e1[r + 1][col]) * 0.5f * (float)s_h);
        }
    }
    glEnd();
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

/* Where the sheet passes a column, in pixels - the particles ride on it */
static void xb_ribbon_surface(int c, float *px, float *py)
{
    int r = XB_RIBBON_ROWS - 1;
    if (c < 0) c = 0;
    if (c >= XB_RIBBON_COLS) c = XB_RIBBON_COLS - 1;
    *px = (s_rib_e0[r][c] + 1.0f) * 0.5f * (float)s_w;
    *py = (1.0f - s_rib_e1[r][c]) * 0.5f * (float)s_h;
}

static void xb_particles_update(float dt, int spawn)
{
    int i;

    s_part_spawn += dt;
    if (spawn && s_part_spawn > 90.0f) {
        s_part_spawn = 0.0f;
        for (i = 0; i < XB_PARTICLES; i++) {
            if (s_parts[i].ttl > 0.0f)
                continue;
            {
                int c = (int)((unsigned)(i * 7919u + (unsigned)s_effect_time * 100u)
                              % (unsigned)XB_RIBBON_COLS);
                float px, py;
                xb_ribbon_surface(c, &px, &py);
                s_parts[i].x = px + ((float)(i & 7) - 3.5f) * 2.0f * s_sf;
                s_parts[i].y = py;
                s_parts[i].vx = ((float)(i % 5) - 2.0f) * 0.006f;
                s_parts[i].vy = -(0.010f + (float)(i % 13) * 0.0016f);
                s_parts[i].ttl = 1400.0f + (float)(i % 17) * 90.0f;
                s_parts[i].life = s_parts[i].ttl;
                s_parts[i].size = (5.0f + (float)(i % 7) * 2.2f) * s_sf;
            }
            break;
        }
    }

    for (i = 0; i < XB_PARTICLES; i++) {
        xb_particle_t *p = &s_parts[i];
        if (p->ttl <= 0.0f)
            continue;
        p->ttl -= dt;
        p->x   += p->vx * dt;
        p->y   += p->vy * dt;
        p->vy  *= 0.999f;
        if (p->ttl <= 0.0f) {
            p->ttl = 0.0f;
            p->life = 0.0f;
        }
    }
}

/* Texel-centre UVs of one icon cell: virgl samples at u * (w - 1), so the
 * corners of a cell have to land on the first and last texel of that cell. */
static void xb_uv(int cell, float *u0, float *v0, float *u1, float *v1)
{
    float cw = (float)XB_ICON_CELL, tw = (float)XB_ICON_TEX - 1.0f;
    float cx = (float)(cell % XB_ATLAS_GRID) * cw;
    float cy = (float)(cell / XB_ATLAS_GRID) * cw;
    *u0 = cx / tw;
    *v0 = cy / tw;
    *u1 = (cx + cw - 1.0f) / tw;
    *v1 = (cy + cw - 1.0f) / tw;
}

static void xb_particles_draw(float alpha)
{
    int i;
    float u0, v0, u1, v1;

    if (alpha <= 0.0f)
        return;

    xb_uv(IC_DROPLET, &u0, &v0, &u1, &v1);
    glBindTexture(GL_TEXTURE_2D, s_tex_icons);
    glEnable(GL_TEXTURE_2D);
    /* The droplets only ever lighten the sheet behind them.  SRC_ALPHA on the
     * source keeps the soft edge the baked alpha carries. */
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    glBegin(GL_QUADS);
    for (i = 0; i < XB_PARTICLES; i++) {
        xb_particle_t *p = &s_parts[i];
        float a, s;
        if (p->ttl <= 0.0f)
            continue;
        a = alpha * (p->ttl / p->life);
        a = a * a;
        if (a < 0.01f)
            continue;
        s = p->size * (0.6f + 0.4f * (p->ttl / p->life));
        glColor4f(0.55f * s_brightness, 0.72f * s_brightness,
                  1.00f * s_brightness, a);
        glTexCoord2f(u0, v0); glVertex2f(p->x - s, p->y - s);
        glTexCoord2f(u1, v0); glVertex2f(p->x + s, p->y - s);
        glTexCoord2f(u1, v1); glVertex2f(p->x + s, p->y + s);
        glTexCoord2f(u0, v1); glVertex2f(p->x - s, p->y + s);
    }
    glEnd();

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

/* ── Drawing primitives ─────────────────────────────────────────────── */

/* xmb_draw_icon(): centred on (cx, cy) in top-down pixels, scaled about its
 * own centre by zoom, with the drop shadow drawn first. */
static void xb_draw_icon(int cell, float cx, float cy, float size,
                         float zoom, float alpha)
{
    float w, h, u0, v0, u1, v1;
    float br = s_brightness;

    if (alpha <= 0.0f)
        return;

    w = size * zoom * 0.5f;
    h = w;

    if (cx < -w || cx > (float)s_w + w || cy < -h || cy > (float)s_h + h)
        return;

    xb_uv(cell, &u0, &v0, &u1, &v1);

    glBindTexture(GL_TEXTURE_2D, s_tex_icons);
    glEnable(GL_TEXTURE_2D);

    if (s_shadows) {
        /* xmb.c:1205 - gfx_display_set_alpha(shadow_color,
         * color[3] * GFX_SHADOW_ALPHA * 0.75f).  The icon texture is the
         * shadow's mask: modulating it with black leaves only its alpha, which
         * is exactly the silhouette the PS3 bar casts. */
        glColor4f(0.0f, 0.0f, 0.0f, alpha * XB_SHADOW_ALPHA * 0.75f);
        glBegin(GL_QUADS);
        glTexCoord2f(u0, v0); glVertex2f(cx - w + s_shadow_offset, cy - h + s_shadow_offset);
        glTexCoord2f(u1, v0); glVertex2f(cx + w + s_shadow_offset, cy - h + s_shadow_offset);
        glTexCoord2f(u1, v1); glVertex2f(cx + w + s_shadow_offset, cy + h + s_shadow_offset);
        glTexCoord2f(u0, v1); glVertex2f(cx - w + s_shadow_offset, cy + h + s_shadow_offset);
        glEnd();
    }

    glColor4f(br, br, br, alpha);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex2f(cx - w, cy - h);
    glTexCoord2f(u1, v0); glVertex2f(cx + w, cy - h);
    glTexCoord2f(u1, v1); glVertex2f(cx + w, cy + h);
    glTexCoord2f(u0, v1); glVertex2f(cx - w, cy + h);
    glEnd();

    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

/* One row of the bitmap font, at `h` pixels tall, left anchored at x. */
static float xb_text_w(const char *str, float h)
{
    return (float)strlen(str) * (h * 0.5f);
}

/* One glyph of the baked 8x16 bitmap font, at texel centres */
static void xb_font_uv(int idx, float *u0, float *v0, float *u1, float *v1)
{
    float tw  = (float)XB_FONT_TEX_W - 1.0f;
    float th  = (float)XB_FONT_TEX_H - 1.0f;
    float col = (float)(idx % XB_FONT_COLS) * 8.0f;
    float row = (float)(idx / XB_FONT_COLS) * 16.0f;
    *u0 = col       / tw;
    *u1 = (col + 7.0f)  / tw;
    *v0 = row       / th;
    *v1 = (row + 15.0f) / th;
}

static void xb_draw_text_run(const char *str, float x, float y, float h,
                             float r, float g, float b, float a, float off)
{
    float step = h * 0.5f;          /* 8 wide / 16 tall glyphs are half as wide */
    const char *p = str;
    float cx = x + off;

    glBegin(GL_QUADS);
    for (; *p; p++) {
        int idx = (unsigned char)*p - 32;
        float u0, v0, u1, v1;
        if (idx < 0 || idx >= XB_FONT_COLS * XB_FONT_ROWS)
            idx = 0;
        xb_font_uv(idx, &u0, &v0, &u1, &v1);
        glColor4f(r, g, b, a);
        glTexCoord2f(u0, v0); glVertex2f(cx,           y + off);
        glTexCoord2f(u1, v0); glVertex2f(cx + step,    y + off);
        glTexCoord2f(u1, v1); glVertex2f(cx + step,    y + off + h);
        glTexCoord2f(u0, v1); glVertex2f(cx,           y + off + h);
        cx += step;
    }
    glEnd();
}

/* xmb_draw_text(): one row of the bitmap font, `h` pixels tall, left anchored
 * at x, with the same drop shadow as the icons. */
static void xb_draw_text(const char *str, float x, float y, float h,
                         float alpha, int shadow)
{
    float a, br = s_brightness;

    /* xmb.c: a fully transparent run is skipped outright */
    if ((int)(0xFF * xb_minf(alpha, s_alpha)) == 0)
        return;
    if (alpha > s_alpha)
        alpha = s_alpha;
    a = alpha;

    glBindTexture(GL_TEXTURE_2D, s_tex_font);
    glEnable(GL_TEXTURE_2D);

    if (shadow && s_shadows)
        xb_draw_text_run(str, x, y, h, 0.0f, 0.0f, 0.0f,
                         a * XB_SHADOW_ALPHA * 0.75f, s_shadow_offset);

    xb_draw_text_run(str, x, y, h, br, br, br, a, 0.0f);

    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

/* xmb_gradient_ident(): four colours, ordered BL, BR, TL, TR */
static void xb_gradient(float g[4][4])
{
    static const float blue[4][3] = {
        {  1/255.0f,   2/255.0f,  67/255.0f},
        {  1/255.0f,  73/255.0f, 183/255.0f},
        {  1/255.0f,  93/255.0f, 194/255.0f},
        {  3/255.0f, 162/255.0f, 254/255.0f},
    };
    static const float gray[4][3] = {
        { 16/255.0f, 16/255.0f, 16/255.0f},
        { 32/255.0f, 32/255.0f, 32/255.0f},
        { 32/255.0f, 32/255.0f, 32/255.0f},
        { 64/255.0f, 66/255.0f,  72/255.0f},
    };
    static const float gold[4][3] = {
        {174/255.0f, 123/255.0f, 44/255.0f},
        {205/255.0f, 174/255.0f, 84/255.0f},
        { 58/255.0f,  43/255.0f, 24/255.0f},
        { 58/255.0f,  43/255.0f, 24/255.0f},
    };
    const float (*src)[3] = blue;
    int i;

    if (s_theme == 1) src = gray;
    if (s_theme == 2) src = gold;

    for (i = 0; i < 4; i++) {
        g[i][0] = src[i][0];
        g[i][1] = src[i][1];
        g[i][2] = src[i][2];
        g[i][3] = 1.0f;
    }
}

/* gfx_display_draw_bg(): the gradient quad, faded by the wallpaper opacity */
static void xb_draw_bg(float alpha)
{
    float g[4][4];
    float a = alpha * s_brightness;

    xb_gradient(g);
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
    glColor4f(g[0][0], g[0][1], g[0][2], a); glVertex2f(0.0f, (float)s_h);
    glColor4f(g[1][0], g[1][1], g[1][2], a); glVertex2f((float)s_w, (float)s_h);
    glColor4f(g[2][0], g[2][1], g[2][2], a); glVertex2f((float)s_w, 0.0f);
    glColor4f(g[3][0], g[3][1], g[3][2], a); glVertex2f(0.0f, 0.0f);
    glEnd();
}

/* ── Message box ────────────────────────────────────────────────────── */

static void xb_message_clear(void)
{
    s_msg_lines  = 0;
    s_msg_more   = 0;
    s_msg_open   = 0;
    s_msg_alpha  = 0.0f;
    s_msg[0][0]  = '\0';
}

static void xb_message_show(void)
{
    s_msg_open  = 1;
    s_msg_alpha = 0.0f;
    xb_tween_push(&s_msg_alpha, 1.0f, XB_DELAY, XB_EASING_ALPHA, (uintptr_t)NULL);
}

static void xb_message_add(const char *line)
{
    if (s_msg_lines < XB_MAX_MSG_LINES) {
        strncpy(s_msg[s_msg_lines], line, XB_MSG_LINE_LEN - 1);
        s_msg[s_msg_lines][XB_MSG_LINE_LEN - 1] = '\0';
        s_msg_lines++;
    } else
        s_msg_more++;
}

static void xb_message_begin(void)
{
    xb_message_clear();
    for (s_msg_lines = 0; s_msg_lines < XB_MAX_MSG_LINES; s_msg_lines++)
        s_msg[s_msg_lines][0] = '\0';
    s_msg_lines = 0;
}

/* xmb_render_messagebox_internal(), without the line ticker */
static void xb_draw_message(void)
{
    float w = 0.0f, h, x, y, a;
    int i;

    if (!s_msg_open || s_msg_alpha <= 0.0f)
        return;

    for (i = 0; i < s_msg_lines; i++) {
        float lw = xb_text_w(s_msg[i], s_font2);
        if (lw > w) w = lw;
    }
    if (s_msg_more) {
        char more[32];
        snprintf(more, sizeof(more), "... %d more lines", s_msg_more);
        {
            float lw = xb_text_w(more, s_font2);
            if (lw > w) w = lw;
        }
    }

    w += s_dialog_margin * 2.0f;
    h  = (float)(s_msg_lines + (s_msg_more ? 1 : 0)) * (s_font2 * 1.6f)
       + s_dialog_margin * 2.0f;
    x  = ((float)s_w - w) * 0.5f;
    y  = ((float)s_h - h) * 0.5f;
    a  = s_msg_alpha * s_alpha;

    glDisable(GL_TEXTURE_2D);
    glColor4f(0.0f, 0.0f, 0.0f, 0.82f * a);
    glBegin(GL_QUADS);
    glVertex2f(x, y); glVertex2f(x + w, y); glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();

    glColor4f(0.35f, 0.55f, 0.85f, 0.55f * a);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x + 1.0f, y + 1.0f);
    glVertex2f(x + w - 1.0f, y + 1.0f);
    glVertex2f(x + w - 1.0f, y + h - 1.0f);
    glVertex2f(x + 1.0f, y + h - 1.0f);
    glEnd();

    for (i = 0; i < s_msg_lines; i++)
        xb_draw_text(s_msg[i], x + s_dialog_margin,
              y + s_dialog_margin + (float)i * (s_font2 * 1.6f),
              s_font2, a, 0);
    if (s_msg_more) {
        char more[32];
        snprintf(more, sizeof(more), "... %d more lines", s_msg_more);
        xb_draw_text(more, x + s_dialog_margin,
              y + s_dialog_margin + (float)i * (s_font2 * 1.6f),
              s_font2, a * 0.7f, 0);
    }
}

/* shell_execute_cmd() sink */
static void xb_shell_output(const char *line, COLOR col, void *user_data)
{
    (void)col; (void)user_data;
    if (!line)
        return;
    xb_message_add(line);
}

/* ── Menu drawing ───────────────────────────────────────────────────── */

static void xb_draw_title(void)
{
    float icon_size = s_icon_size / 3.5f;
    float cx, cy;
    xb_cat_t *cat = &s_cats[s_cat];

    /* xmb.c:9528 - the current menu's icon, scaled down, at the title margin */
    cx = s_title_left - (icon_size / 0.75f);
    cx += (s_icon_size - icon_size) / 2.0f;
    if (cx < icon_size / 2.0f)
        cx = icon_size / 2.0f;
    cy = s_title_top + icon_size / 6.0f;

    xb_draw_icon(cat->icon, cx + icon_size * 0.5f, cy + icon_size * 0.5f,
                 icon_size, 1.0f, s_alpha * s_alpha_list);

    /* ... and the title text, offset past the icon (xmb.c:10237).  Below the
     * category level the PS3 shows "Category > Item" on the same line. */
    {
        float tx = s_title_left
                 + (s_depth > 1 ? s_icon_size * 0.45f : s_icon_size / 2.5f);
        xb_draw_text(cat->label, tx, s_title_top, s_font, 1.0f, 1);
        if (s_depth > 1) {
            const char *item = cat->items[cat->selection].label;
            tx += xb_text_w(cat->label, s_font) + xb_text_w("> ", s_font);
            xb_draw_text(item, tx, s_title_top, s_font, 1.0f, 1);
        }
    }
}

static void xb_draw_band(void)
{
    int i;

    for (i = 0; i < s_cat_count; i++) {
        xb_node_t *n = &s_cats[i].node;
        float x = s_x + s_band_x + s_margin_left
                + s_spacing_h * (float)(i + 1) - s_icon_size / 2.0f;
        float y = s_margin_top;
        float a = xb_minf(n->alpha, s_alpha);
        xb_draw_icon(s_cats[i].icon, x + s_icon_size / 2.0f,
                     y + s_icon_size / 2.0f, s_icon_size, n->zoom, a);
    }

    /* The cursor that marks the active category, at the band's left stop */
    {
        float a = xb_minf(0.75f * s_alpha_list, s_alpha);
        xb_draw_icon(IC_ARROW,
              s_x + s_band_x + s_margin_left + s_spacing_h,
              s_margin_top + s_icon_size * 1.35f,
              s_cursor_size, 0.5f, a);
    }
}

static void xb_value_text(xb_item_t *it, char *buf, size_t n)
{
    buf[0] = '\0';
    switch (it->kind) {
    case XB_TOGGLE:
        snprintf(buf, n, "%s", it->value ? "On" : "Off");
        break;
    case XB_RANGE:
        snprintf(buf, n, "%d%%", it->value);
        break;
    case XB_ENUM: {
        const char *p = it->opts;
        int idx = it->value, i;
        for (i = 0; i < idx && p; i++) {
            p = strchr(p, '|');
            if (p) p++;
        }
        if (p) {
            const char *end = strchr(p, '|');
            size_t len = end ? (size_t)(end - p) : strlen(p);
            if (len >= n) len = n - 1;
            memcpy(buf, p, len);
            buf[len] = '\0';
        }
        break;
    }
    default:
        break;
    }
}

/* xmb_draw_item() for one row of the horizontal list */
static int xb_draw_item(int i, int current)
{
    xb_item_t *items = NULL;
    xb_item_t *it;
    xb_node_t *n;
    xb_cat_t *cat = &s_cats[s_cat];
    float half = s_icon_size / 2.0f;
    float icon_y, icon_x, label_offset, a;
    int count = 0;
    char value[48];

    items = xb_cur_items(&count);
    if (i < 0 || i >= count)
        return 0;
    it  = &items[i];
    n   = &it->node;

    icon_y = s_margin_top + n->y + half;
    if (icon_y < half)
        return 0;
    if (icon_y > (float)s_h + s_icon_size)
        return -1;
    icon_x = n->x + s_margin_left + s_spacing_h - half;
    if (icon_x < -half || icon_x > (float)s_w)
        return 0;

    /* xmb.c:6205 - vertical fade of rows approaching the screen edges */
    a = n->alpha * s_alpha_list;
    a = xb_minf(a, s_alpha);
    if (s_fade > 0 && n->x == 0.0f) {
        float icon_space = s_spacing_v;
        float icon_ratio = icon_space / (float)s_h / icon_space * 4.0f;
        float scr_margin = s_margin_top + (icon_space / icon_ratio / 400.0f);
        float factor     = (float)s_fade / 100.0f / icon_ratio;
        float min_alpha  = 0.01f;
        float max_alpha  = (i == current) ? 1.0f : 0.75f;
        float new_alpha  = (i < current)
             ? (n->y + scr_margin) / factor
             : ((float)s_h - n->y - scr_margin + icon_space) / factor;
        new_alpha = xb_clampf(new_alpha, min_alpha, max_alpha);
        if (new_alpha < n->alpha) {
            a = xb_minf(new_alpha * s_alpha_list, s_alpha);
            n->alpha = new_alpha;
            n->label_alpha = new_alpha;
        }
    }

    if (i != current)
       a *= 1.0f / 1.25f;   /* passive entry icons are dimmed */

    xb_draw_icon(it->icon, icon_x + half, icon_y, s_icon_size, n->zoom, a);

    label_offset = s_label_top;
    if (i == current && s_depth == 1 && it->sub && *it->sub)
        label_offset = -s_label_top;

    if (it->label && *it->label)
        xb_draw_text(it->label,
             n->x + s_margin_left + s_spacing_h + s_label_left,
             s_margin_top + n->y + label_offset, s_font,
             n->label_alpha * s_alpha_list, 1);

    if (i == current && it->sub && *it->sub && s_depth == 1)
        xb_draw_text(it->sub,
             n->x + s_margin_left + s_spacing_h + s_label_left,
             s_margin_top + n->y + s_label_top * 3.5f, s_font2,
             n->label_alpha * 0.7f * s_alpha_list, 1);

    value[0] = '\0';
    if (s_depth == 1)
        xb_value_text(it, value, sizeof(value));
    if (value[0])
        xb_draw_text(value,
             n->x + s_margin_left + s_spacing_h + s_label_left + s_setting_left,
             s_margin_top + n->y + s_label_top, s_font,
             n->label_alpha * s_alpha_list, 1);

    if (i == current && it->kind == XB_SUB)
        xb_draw_icon(IC_ARROW,
             n->x + s_margin_left + s_spacing_h + s_label_left
             + s_setting_left + xb_text_w(">", s_font) * 0.0f + s_font,
             s_margin_top + n->y + s_label_top + s_font * 0.5f,
             s_font, 0.6f, n->label_alpha * s_alpha_list);

    (void)cat;
    return 1;
}

static void xb_draw_items(void)
{
    int count = 0;
    int current = xb_cur_selection();
    int i;

    xb_cur_items(&count);
    for (i = 0; i < count; i++)
        if (xb_draw_item(i, current) < 0)
            break;
}

static void xb_draw_footer(void)
{
    char buf[64];
    xb_item_t *items = xb_cur_items(&(int){0});
    int current = xb_cur_selection();
    int kind = items && items[current].kind ? items[current].kind : XB_APP;

    snprintf(buf, sizeof(buf), "%s",
             s_msg_open ? "ESC Close" :
             (kind == XB_APP)     ? "ENTER Launch  \xC2\xB7  ESC Quit" :
             (kind == XB_CMD)     ? "ENTER Run  \xC2\xB7  ESC Quit" :
             (kind == XB_SUB)     ? "ENTER Open  \xC2\xB7  ESC Back" :
                                    "LEFT/RIGHT Adjust  \xC2\xB7  ENTER Set");
    xb_draw_text(buf, s_margin_left, (float)s_h - s_font * 1.4f, s_font2,
                 0.55f * s_alpha, 0);
}

/* ── Animations ─────────────────────────────────────────────────────── */

static void xb_anim_row_move(void)
{
    int count = 0, first = 0, last = 0, i;
    int current = xb_cur_selection();
    xb_item_t *items = xb_cur_items(&count);
    uintptr_t tag = (uintptr_t)items;

    if (!items)
        return;
    xb_visible_range(count, current, &first, &last);

    for (i = 0; i < count; i++) {
        xb_node_t *n = &items[i].node;
        float ia = (i == current) ? 1.0f : 0.75f;   /* active / passive alpha */
        float iz = (i == current) ? 1.0f : 0.5f;    /* active / passive zoom  */
        float iy = xb_item_y(i, current);

        if (i < first || i > last) {
            n->y     = iy;
            n->alpha = ia;
            n->label_alpha = ia;
            n->zoom  = iz;
            continue;
        }
        xb_tween_push(&n->alpha,       ia, XB_DELAY, XB_EASE_OUT_QUAD, tag);
        xb_tween_push(&n->label_alpha, ia, XB_DELAY, XB_EASE_OUT_QUAD, tag);
        xb_tween_push(&n->zoom,        iz, XB_DELAY, XB_EASE_OUT_QUAD, tag);
        xb_tween_push(&n->y,           iy, XB_DELAY, XB_EASE_OUT_QUAD, tag);
    }
}

/* xmb_list_switch_horizontal_list() + the category scroll */
static void xb_anim_band_move(int dir)
{
    int i;
    uintptr_t tag_pos = (uintptr_t)&s_band_x;

    xb_tween_kill(tag_pos);
    for (i = 0; i < s_cat_count; i++) {
        xb_node_t *n = &s_cats[i].node;
        float ia = (i == s_cat) ? 1.0f : 0.75f;
        float iz = (i == s_cat) ? 1.0f : 0.5f;
        xb_tween_push(&n->alpha, ia, XB_DELAY, XB_EASE_OUT_QUAD,
                      (uintptr_t)&s_cats[i]);
        xb_tween_push(&n->zoom,  iz, XB_DELAY, XB_EASE_OUT_QUAD,
                      (uintptr_t)&s_cats[i]);
        (void)dir;
    }

    xb_tween_push(&s_band_x, -s_spacing_h * (float)s_cat,
                  XB_DELAY, XB_EASING_XY, tag_pos);
}

/* xmb_list_switch_new(): the new list walks in from the side */
static void xb_anim_list_switch(int dir)
{
    int count = 0, first = 0, last = 0, i;
    int current = xb_cur_selection();
    xb_item_t *items = xb_cur_items(&count);
    uintptr_t tag;

    if (!items)
        return;
    tag = (uintptr_t)items;
    xb_visible_range(count, current, &first, &last);

    for (i = 0; i < count; i++) {
        xb_node_t *n = &items[i].node;
        float ia = (i == current) ? 1.0f : 0.75f;

        n->x           = s_spacing_h * (float)dir;
        n->alpha       = 0.0f;
        n->label_alpha = 0.0f;
        n->y           = xb_item_y(i, current);
        n->zoom        = (i == current) ? 1.0f : 0.5f;

        if (i >= first && i <= last) {
            /* xmb_push_animations(): the fading-in row is divided by 5 */
            n->alpha /= 5.0f;
            xb_tween_push(&n->alpha,       ia, XB_DELAY, XB_EASING_ALPHA, tag);
            xb_tween_push(&n->label_alpha, ia, XB_DELAY, XB_EASING_ALPHA, tag);
            xb_tween_push(&n->x,        0.0f, XB_DELAY, XB_EASING_XY, tag);
        } else {
            n->x           = 0.0f;
            n->alpha       = ia;
            n->label_alpha = ia;
        }
    }
}

/* xmb_list_open_new() + xmb_list_open(): entering or leaving a sub list */
static void xb_anim_list_open(int dir)
{
    int count = 0, first = 0, last = 0, i;
    int current = xb_cur_selection();
    xb_item_t *items = xb_cur_items(&count);
    uintptr_t tag;
    float target_x;

    if (!items)
        return;
    tag = (uintptr_t)items;
    xb_visible_range(count, current, &first, &last);

    for (i = 0; i < count; i++) {
        xb_node_t *n = &items[i].node;
        float ia = (i == current) ? 1.0f : 0.75f;

        n->alpha = (dir > 0 || i == current) ? 0.0f : ia;
        if (dir > 0)
           n->alpha /= 5.0f;
        n->label_alpha = 0.0f;
        n->x    = s_icon_size * (float)dir * 2.0f;
        n->y    = xb_item_y(i, current);
        n->zoom = (i == current) ? 1.0f : 0.5f;

        if (i >= first && i <= last) {
            xb_tween_push(&n->alpha,       ia, XB_DELAY, XB_EASING_ALPHA, tag);
            xb_tween_push(&n->label_alpha, ia, XB_DELAY, XB_EASING_ALPHA, tag);
            xb_tween_push(&n->x,        0.0f, XB_DELAY, XB_EASING_XY, tag);
        } else {
            n->x           = 0.0f;
            n->alpha       = ia;
            n->label_alpha = ia;
        }
    }

    /* xmb_list_open(): the whole bar slides aside by one icon per depth */
    target_x = s_icon_size * 0.7f * -(float)(s_depth * 2 - 2);
    if (s_depth <= 2) {
        static int once;
        xb_tween_push(&s_x, target_x, XB_DELAY, XB_EASE_OUT_QUAD,
                      (uintptr_t)&s_x);
        (void)once;
    }
}

/* xmb_animation_list_alpha() */
static void xb_anim_list_alpha(int fade_in)
{
    xb_tween_push(&s_alpha_list, fade_in ? 1.0f : 0.0f,
                  XB_DELAY * 4.0f, XB_EASING_ALPHA, (uintptr_t)NULL);
}

static void xb_layout_nodes(void)
{
    int count = 0, i;
    int current = xb_cur_selection();
    xb_item_t *items = xb_cur_items(&count);

    for (i = 0; i < count && items; i++) {
        xb_node_t *n = &items[i].node;
        n->y = xb_item_y(i, current);
    }
    s_band_x = -s_spacing_h * (float)s_cat;
    s_x      = s_icon_size * 0.7f * -(float)(s_depth * 2 - 2);
}

/* ── Actions ────────────────────────────────────────────────────────── */

static void xb_refresh_settings(void)
{
    xb_item_t *items = s_cats[1].items;
    s_theme     = items[S_THEME].value;
    s_wave      = items[S_WAVE].value;
    s_particles = items[S_PARTICLES].value;
    s_shadows   = items[S_SHADOWS].value;
    s_brightness= (float)items[S_BRIGHT].value / 100.0f;
    s_fade      = items[S_FADE].value;
}

static void xb_sysdata_fill(void)
{
    uint32_t base = 0, limit = 0, used = 0;
    int i;

    for (i = 0; i < XB_MAX_SUB; i++) {
        s_sys_lines[i][0] = '\0';
        s_sys_live[i]     = s_sys_lines[i];
    }
    s_items_sysdata[0].live     = s_sys_live;
    s_items_sysdata[0].live_count = 6;

    snprintf(s_sys_lines[0], XB_MSG_LINE_LEN, "B-System (BTRON 3.20) replica");
    snprintf(s_sys_lines[1], XB_MSG_LINE_LEN, "Kernel: Itron task kernel, %d ticks/s", 1000);
    sys_get_mem_stats(&base, &limit, &used);
    snprintf(s_sys_lines[2], XB_MSG_LINE_LEN, "Memory base 0x%08X  limit 0x%08X",
             (unsigned)base, (unsigned)limit);
    snprintf(s_sys_lines[3], XB_MSG_LINE_LEN, "Memory used %u KiB of %u KiB",
             (unsigned)(used / 1024u), (unsigned)((limit - base) / 1024u));
    sys_get_devconf(s_sys_lines[4], XB_MSG_LINE_LEN);
    snprintf(s_sys_lines[5], XB_MSG_LINE_LEN, "Display %d x %d, %d FPS target",
             s_w, s_h, 1000 / (s_frame_ms > 0 ? s_frame_ms : 16));

    s_items_sysdata[0].label = s_sys_lines[0];
    for (i = 1; i < 6; i++)
        s_items_sysdata[i].label = s_sys_lines[i];
    s_items_sysdata[0].sub_count = 0;
}

/* The menu clock, in milliseconds, as of the last tick.  xb_activate() runs
 * from the event handler and needs it to set the launch hold's deadline. */
static unsigned s_now_ms = 0;

static void xb_launch(int id)
{
    switch (id) {
    case L_GTERM:     if (open_gterm_window)     open_gterm_window(); break;
    case L_TEDITOR:   if (open_t_editor_window)  open_t_editor_window(); break;
    case L_PAINT:     if (open_paint_window)     open_paint_window(); break;
    case L_AUDIO:     if (open_audio_player_window) open_audio_player_window(); break;
    case L_ORCHESTRA: if (open_orchestra_window) open_orchestra_window(); break;
    case L_VOBJ:      if (open_vobj_manager_window) open_vobj_manager_window(); break;
    case L_TAD:       if (open_tad_browser_window)  open_tad_browser_window("/", "TAD"); break;
    case L_DRIVE:     if (open_drivesetup_window)  open_drivesetup_window(); break;
    case L_CHAT:      if (launch_beos_chat)        launch_beos_chat(); break;
    case L_KAGEE:     if (open_kagee_window)       open_kagee_window(); break;
    case L_QUAKE:     if (open_quake_window)       open_quake_window(120, 60, 640, 480); break;
    default: break;
    }
}

static void xb_activate(xb_item_t *it)
{
    switch (it->kind) {
    case XB_APP:
        /* xmb.c:7046 - single-click launching holds the list out for 500 ms */
        s_alpha_list = 0.0f;
        s_draw_entry_until = (int)(s_now_ms + XB_DRAW_ENTRY_MS);
        xb_launch(it->launch);
        break;
    case XB_TOGGLE:
        it->value = !it->value;
        xb_refresh_settings();
        break;
    case XB_CMD:
        xb_message_begin();
        shell_execute_cmd(it->cmd ? it->cmd : "ls /", xb_shell_output, NULL, s_wnd);
        if (!s_msg_lines)
            xb_message_add("(no output)");
        xb_message_show();
        break;
    case XB_SUB:
        if (it->sub_items && it->sub_count == 0) {
            it->sub_items = s_items_sysdata;
            it->sub_count = 6;
        }
        if (it->sub_items && it->sub_count) {
            xb_sysdata_fill();
            s_depth    = 2;
            s_sub_sel  = 0;
            xb_anim_list_open(1);
        }
        break;
    default:
        break;
    }
}

static void xb_adjust(xb_item_t *it, int dir)
{
    switch (it->kind) {
    case XB_TOGGLE:
        it->value = dir > 0 ? 1 : 0;
        break;
    case XB_RANGE:
        it->value += dir * 5;
        if (it->value < 0) it->value = 0;
        if (it->max && it->value > it->max) it->value = it->max;
        break;
    case XB_ENUM: {
        int count = 1, i;
        const char *p;
        for (p = it->opts; p && *p; p++)
            if (*p == '|') count++;
        it->value += dir;
        if (it->value < 0) it->value = count - 1;
        if (it->value >= count) it->value = 0;
        (void)i;
        break;
    }
    default:
        return;
    }
    /* xmb.c: adjusting a value fades the list back in */
    s_alpha_list = 0.0f;
    xb_anim_list_alpha(1);
    xb_refresh_settings();
}

static void xb_move_row(int dir)
{
    int count = 0;
    int sel = xb_cur_selection();
    xb_item_t *items = xb_cur_items(&count);

    if (!items || count <= 0)
        return;
    sel += dir;
    if (sel < 0)
        sel = 0;
    if (sel >= count)
        sel = count - 1;
    if (sel == xb_cur_selection())
        return;
    xb_set_selection(sel);
    xb_anim_row_move();
}

static void xb_move_cat(int dir)
{
    int next = s_cat + dir;
    if (next < 0)
        next = s_cat_count - 1;
    if (next >= s_cat_count)
        next = 0;
    if (next == s_cat)
        return;
    s_cat = next;
    if (s_depth > 1) {
        s_depth = 1;
        s_x     = 0.0f;
    }
    xb_anim_band_move(dir);
    xb_anim_list_switch(dir);
}

static void xb_go_back(void)
{
    if (s_msg_open) {
        xb_message_clear();
        return;
    }
    if (s_depth > 1) {
        s_depth   = 1;
        s_sub_sel = 0;
        xb_anim_list_open(-1);
        return;
    }
    cls_wnd(s_wnd);
}

/* ── Window callbacks ───────────────────────────────────────────────── */

static void xb_task(VW exinf)
{
    (void)exinf;
    while (s_wnd) {
        inval_wnd(s_wnd);
        dly_tsk(s_frame_ms);
    }
    s_tsk = 0;
}

static void xb_paint(WND *wnd, GDEV *dev)
{
    SYSTIME now = 0;
    float dt;

    if (!wnd || !dev || !s_surf)
        return;

    if (s_surf->width != dev->width || s_surf->height != dev->height) {
        egl_surface_resize(s_surf, dev->width, dev->height);
        xb_layout(dev->width, dev->height);
        xb_layout_nodes();
    }

    get_tim(&now);
    dt = (float)(unsigned)(now - s_last_time);
    if (dt < 0.0f || dt > 250.0f)
        dt = 16.0f;
    s_last_time = now;
    s_now_ms    = (unsigned)now;

    /* The launch hold of xmb.c:9366 */
    if (s_alpha_list == 0.0f && s_draw_entry_until &&
        (int)now >= s_draw_entry_until) {
        s_draw_entry_until = 0;
        xb_anim_list_alpha(1);
    }

    xb_tween_tick(dt);

    /* gfx_display.c:874 - the effect clock advances once per drawn frame */
    s_effect_time += 0.01f;
    if (s_effect_time > 65536.0f)
        s_effect_time = 0.0f;
    if (s_wave)
        xb_ribbon_step(s_effect_time);
    if (s_particles && s_wave)
        xb_particles_update(dt, 1);

    glViewport(0, 0, dev->width, dev->height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (double)dev->width, (double)dev->height, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_LIGHT0);
    glDisable(GL_CULL_FACE);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* xmb_draw_bg(): gradient, then the waving surface over it */
    xb_draw_bg(xb_minf(s_alpha, 0.90f));
    if (s_wave)
        xb_draw_ribbon(xb_minf(s_alpha, 0.90f));
    if (s_particles && s_wave)
        xb_particles_draw(s_alpha);

    xb_draw_title();
    xb_draw_band();
    xb_draw_items();
    xb_draw_footer();
    xb_draw_message();

    egl_swap_buffers(s_surf);
}

static void xb_event(WND *wnd, const EVT *evt)
{
    int count = 0;
    xb_item_t *items;
    xb_item_t *cur;

    if (!wnd || !evt)
        return;

    if (evt->type == EV_KEY_DOWN) {
        switch (evt->key) {
        case BTRON_KEY_ESCAPE:
            xb_go_back();
            break;
        case BTRON_KEY_UP:
            xb_move_row(-1);
            break;
        case BTRON_KEY_DOWN:
            xb_move_row(1);
            break;
        case BTRON_KEY_LEFT:
            items = xb_cur_items(&count);
            cur   = items ? &items[xb_cur_selection()] : NULL;
            if (cur && (cur->kind == XB_TOGGLE || cur->kind == XB_RANGE ||
                        cur->kind == XB_ENUM))
               xb_adjust(cur, -1);
            else
               xb_move_cat(-1);
            break;
        case BTRON_KEY_RIGHT:
            items = xb_cur_items(&count);
            cur   = items ? &items[xb_cur_selection()] : NULL;
            if (cur && (cur->kind == XB_TOGGLE || cur->kind == XB_RANGE ||
                        cur->kind == XB_ENUM))
               xb_adjust(cur, 1);
            else
               xb_move_cat(1);
            break;
        case BTRON_KEY_RETURN:
        case BTRON_KEY_KP_ENTER:
        case BTRON_KEY_SPACE:
            if (s_msg_open) {
               xb_message_clear();
               break;
            }
            items = xb_cur_items(&count);
            cur   = items ? &items[xb_cur_selection()] : NULL;
            if (cur)
               xb_activate(cur);
            break;
        default:
            break;
        }
        inval_wnd(wnd);
        return;
    }

    if (evt->type == EV_MOUSE_MOVE || evt->type == EV_BUT_DOWN) {
        /* The band is clickable, as in xmb_pointer_treatment() */
        if (evt->type == EV_BUT_DOWN) {
            float y = (float)evt->pos.y;
            float x = (float)evt->pos.x;
            if (y >= s_margin_top && y <= s_margin_top + s_icon_size) {
                int i;
                for (i = 0; i < s_cat_count; i++) {
                    float bx = s_x + s_band_x + s_margin_left
                             + s_spacing_h * (float)(i + 1) - s_icon_size / 2.0f;
                    if (x >= bx && x <= bx + s_icon_size && i != s_cat) {
                        while (i != s_cat)
                            xb_move_cat(i > s_cat ? 1 : -1);
                        break;
                    }
                }
            } else if (s_msg_open)
                xb_message_clear();
        }
        inval_wnd(wnd);
    }
}

static void xb_destroy(WND *wnd)
{
    (void)wnd;
    if (s_surf) {
        egl_destroy_surface(s_surf);
        s_surf = NULL;
    }
    if (s_tex_icons) {
        glBindTexture(GL_TEXTURE_2D, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, NULL);
        s_tex_icons = 0;
    }
    s_tex_font = 0;
    s_wnd      = NULL;
    if (s_tsk > 0)
        wup_tsk(s_tsk);
}

/* Initial state: every node starts transparent at its layout slot, then the
 * bar walks in - xmb_toggle() + the first xmb_list_open_*() pass. */
static void xb_init_state(void)
{
    int c, i;

    s_alpha      = 1.0f;
    s_alpha_list = 1.0f;
    s_depth      = 1;
    s_cat        = 0;
    s_sub_sel    = 0;

    for (c = 0; c < s_cat_count; c++) {
        xb_node_t *node = &s_cats[c].node;
        node->x = 0.0f;
        node->y = 0.0f;
        node->alpha = 0.0f;
        node->label_alpha = 0.0f;
        node->zoom = (c == s_cat) ? 1.0f : 0.5f;
    }

    /* Item tables carry their own counts in the array sizes; set them here so
     * the content above can be edited without keeping a count in sync. */
    s_cats[0].count = (int)(sizeof(s_items_apps)     / sizeof(xb_item_t));
    s_cats[1].count = (int)(sizeof(s_items_settings) / sizeof(xb_item_t));
    s_cats[2].count = (int)(sizeof(s_items_volume)   / sizeof(xb_item_t));
    s_cats[3].count = (int)(sizeof(s_items_commands) / sizeof(xb_item_t));

    for (c = 0; c < s_cat_count; c++) {
        for (i = 0; i < s_cats[c].count; i++) {
            xb_node_t *n = &s_cats[c].items[i].node;
            n->x = 0.0f;
            n->y = 0.0f;
            n->alpha = 0.0f;
            n->label_alpha = 0.0f;
            n->zoom = 0.5f;
        }
    }

    xb_refresh_settings();
    xb_layout(s_w > 0 ? s_w : 960, s_h > 0 ? s_h : 600);
    xb_layout_nodes();

    /* First-frame walk-in */
    for (i = 0; i < s_cat_count; i++)
        xb_tween_push(&s_cats[i].node.alpha, (i == s_cat) ? 1.0f : 0.75f,
                      XB_DELAY, XB_EASING_ALPHA, (uintptr_t)&s_cats[i]);
    xb_anim_list_switch(1);
}

WND* open_xmb_window(void)
{
    BTRON_DESKTOP *desk;
    int x = 16, y = 16, w = 960, h = 600;
    T_CTSK ctsk;

    if (s_wnd) {
        top_wnd(s_wnd);
        return s_wnd;
    }

    desk = get_btron_desktop();
    if (desk && desk->width > 160 && desk->height > 160) {
        w = desk->width - 24;
        h = desk->height - 64;
        x = 12;
        y = 20;
        if (w > 1440) w = 1440;
        if (h > 900)  h = 900;
    }

    s_wnd = opn_wnd("Cross Media Bar", x, y, w, h,
                    WND_ATTR_TITLE | WND_ATTR_BORDER | WND_ATTR_CLOSE |
                    WND_ATTR_RESIZE);
    if (!s_wnd) {
        uart_puts_raw("[GL] ERROR: open_xmb_window failed\n");
        return NULL;
    }

    s_w = w;
    s_h = h;

    s_wnd->paint         = xb_paint;
    s_wnd->event_handler = xb_event;
    s_wnd->destroy       = xb_destroy;

    s_surf = egl_create_window_surface(s_wnd);
    if (!s_surf) {
        uart_puts_raw("[GL] ERROR: xmb: egl_create_window_surface failed\n");
        cls_wnd(s_wnd);
        s_wnd = NULL;
        return NULL;
    }

    xb_bake_textures();
    xb_init_state();

    get_tim(&s_last_time);

    ctsk.exinf   = 0;
    ctsk.tskatr  = TA_HLNG;
    ctsk.task    = xb_task;
    ctsk.itskpri = 10;
    ctsk.stksz   = 32768;
    s_tsk = cre_tsk(&ctsk);
    if (s_tsk > 0)
        sta_tsk(s_tsk, 0);

    uart_puts_raw("[GL] xmb: XrossMediaBar open\n");
    return s_wnd;
}
