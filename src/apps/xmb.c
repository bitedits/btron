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
 * Invented B-System content: the Applications, Settings, Discs, Games and Commands
 * categories.
 * Applications really launch B-System windows, Commands really run B-System shell
 * builtins through shell_execute_cmd(), and the Settings rows edit the B-System's
 * own state in place - icon size, window frame style, keyboard and mouse kernel
 * variables, terminal settings, input method - rather than opening its Settings
 * applications.  The Discs band is not content at all: its rows are the mounted
 * volumes and their drawers, read from the same virtual file system the "sc" file
 * manager reads (src/clu/vfs.c), one folder at a time and as deep as the volumes go.
 * The Games band is a listing too, of the host's assets/msx folder: one row per
 * cartridge image, each of which opens the B-MSX window with that ROM loaded.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "xmb.h"
#include "../gl/gl_dispatch.h"
#include "../gl/egl_surface.h"
#include "../clu/vfs.h"          /* the VFS that "sc" walks: src/clu/vfs.c */
#include <btron/btron.h>
#include <btron/core.h>
#include <btron/apps.h>
#include <btron/settings.h>   /* icon size, terminal settings */
#include <btron/pmc.h>        /* window frame style           */
#include <btron/tip.h>        /* input method                 */
#include <btron/fs/vol_api.h> /* mounted volumes: names, sizes, free blocks */
#include <btron/libc_shim.h>
#include <math.h>

#if BTRON_HOSTED
#include <dirent.h>      /* the Games band lists the host's assets/msx */
#endif

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif

extern void uart_puts_raw(const char *s);

/* The kernel's live input configuration, defined in src/settings/input.c - which
 * every target that builds this file builds too.  The bar writes these, the input
 * applet reads them, and the pointer and repeat drivers of the bare-metal cores
 * take their timings from the same six variables, so a row of the menu changes the
 * machine rather than a copy of it kept by the menu. */
extern uint32_t g_kbd_repeat_delay_us;
extern uint32_t g_kbd_repeat_interval_us;
extern int      g_kbd_repeat_enabled;
extern int      g_mouse_step_mult;
extern int      g_mouse_swap_select_adjust;
extern int      g_mouse_accel_profile;

/* ── Constants taken from xmb.c ───────────────────────────────────────
 * XMB_RIBBON_ROWS/COLS and XMB_DELAY are the driver's own; so are the two
 * easing ids (XMB_EASING_ALPHA = OUT_CIRC, XMB_EASING_XY = OUT_QUAD) and the
 * GFX_SHADOW_ALPHA of gfx_display.h. */
#define XB_RIBBON_ROWS      64
#define XB_RIBBON_COLS      64
#define XB_DELAY            166.66667f
#define XB_SHADOW_ALPHA     1.00f
#define XB_DRAW_ENTRY_MS    500          /* MENU_DRAW_ENTRY_DELAY (500000 us) */
#define XB_BAR_OFFSET       1.1f         /* xmb.c:3503, the ps3 layout's value */

#define XB_EASE_LINEAR      0
#define XB_EASE_OUT_QUAD    1
#define XB_EASE_OUT_CIRC    2
#define XB_EASE_OUT_EXPO    3
#define XB_EASE_OUT_BOUNCE  4
#define XB_EASE_IN_SINE     5
#define XB_EASING_ALPHA     XB_EASE_OUT_CIRC
#define XB_EASING_XY        XB_EASE_OUT_QUAD

/* xmb_layout_common() (xmb.c:7182-7190). Categories and items happen to carry
 * the same four numbers; xmb_list_open_new() reads the categories_* pair for
 * the vertical rows, which is why both names are kept here. */
#define XB_CATEGORIES_ACTIVE_ALPHA   1.0f
#define XB_CATEGORIES_PASSIVE_ALPHA  0.75f
#define XB_CATEGORIES_ACTIVE_ZOOM    1.0f
#define XB_CATEGORIES_PASSIVE_ZOOM   0.5f
#define XB_ITEMS_ACTIVE_ALPHA        1.0f
#define XB_ITEMS_PASSIVE_ALPHA       0.75f
#define XB_ITEMS_ACTIVE_ZOOM         1.0f
#define XB_ITEMS_PASSIVE_ZOOM        0.5f

/* xmb->icon_size * 10 in both xmb_selection_pointer_changed() and
 * xmb_list_open_new(): a row further off than this is placed, not tweened. */
#define XB_ANIM_THRESHOLD            (s_icon_size * 10.0f)

#define XB_MAX_CATS         6
#define XB_MAX_ITEMS        14
#define XB_MAX_SUB          12
#define XB_MAX_TWEENS       320
#define XB_MAX_MSG_LINES    10
#define XB_MSG_LINE_LEN     72

/* The Discs band's list is not a table but a folder, so its rows are made as the
 * folder is read.  XB_DISC_ROWS is how many entries of one folder the bar can show
 * at a time and XB_DISC_HISTORY how many levels it remembers a cursor for; both are
 * reached only by a folder bigger or deeper than any volume in this system holds,
 * and the bar then stops at the last row it has room for rather than running off
 * the end of a static array. */
#define XB_DISC_ROWS        128
#define XB_DISC_HISTORY     64
#define XB_DISC_SUB         48   /* a volume's description line, in bytes */

#define XB_NEL(a)           ((int)(sizeof(a) / sizeof((a)[0])))

#define XB_ATLAS_GRID       6            /* 6x6 icon cells */
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
    XB_TEXT,         /* a line inside a nested list         */
    XB_DIR,          /* a folder: opens one level deeper    */
    XB_FILE          /* a body: read and shown as text      */
};

/* Launch ids for XB_APP rows */
enum {
    L_NONE = 0, L_GTERM, L_TEDITOR, L_PAINT, L_AUDIO, L_ORCHESTRA,
    L_VOBJ, L_TAD, L_DRIVE, L_CHAT, L_KAGEE, L_QUAKE, L_MSX
};

/* Icon atlas cells */
enum {
    IC_CAT_APPS = 0, IC_CAT_SETTINGS, IC_CAT_COMMANDS, IC_ARROW,
    IC_APP_TERM, IC_APP_EDITOR, IC_APP_PAINT, IC_APP_MUSIC, IC_APP_ORCHESTRA,
    IC_APP_VOBJ, IC_APP_TAD, IC_APP_DRIVE, IC_APP_CHAT, IC_APP_PHOTO,
    IC_GEAR, IC_SLIDER, IC_SPEAKER, IC_INFO, IC_PROMPT,
    IC_DROPLET, IC_FOLDER, IC_APP_QUAKE, IC_CLOCK, IC_BLANK,
    IC_MINIDISC, IC_FILE, IC_CAT_GAMES
};

typedef struct {
    float x, y, alpha, label_alpha, zoom;
} xb_node_t;

typedef struct xb_item_s {
    const char   *label;
    const char   *sub;
    int           kind;
    int           bind;           /* XMB_BIND_*: whose value this row holds */
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
    int         files;            /* the list is a folder, not a table   */
    xb_node_t   node;             /* the band icon */
} xb_cat_t;

/* Rows the renderer reads back, named for the menu that holds them */
enum { THEME_COLOUR = 0, THEME_WAVE, THEME_PARTICLES };
enum { SCR_BRIGHT = 0, SCR_FADE, SCR_SHADOWS };

/* The rows in the Settings band, by their order there.  A test walks the band by
 * these rather than by numbers, because the band is the thing being tested. */
enum { SET_APPEARANCE = 0, SET_THEME, SET_SCREEN, SET_KEYBOARD,
       SET_MOUSE, SET_TERMINAL, SET_LANGUAGE, SET_SYSDATA };

static xb_item_t s_items_apps[] = {
    {"Terminal",       "Console access to the B-System shell",     XB_APP, XMB_BIND_NONE, L_GTERM,     0,0,NULL,NULL,IC_APP_TERM,   0,0,0,0,{0}},
    {"Text Editor",    "Edit plain text and TRON Code documents",  XB_APP, XMB_BIND_NONE, L_TEDITOR,   0,0,NULL,NULL,IC_APP_EDITOR, 0,0,0,0,{0}},
    {"Paint",          "Draw bitmaps into the memory card",        XB_APP, XMB_BIND_NONE, L_PAINT,     0,0,NULL,NULL,IC_APP_PAINT,  0,0,0,0,{0}},
    {"Audio Player",   "Play back music from a volume",            XB_APP, XMB_BIND_NONE, L_AUDIO,     0,0,NULL,NULL,IC_APP_MUSIC,  0,0,0,0,{0}},
    {"Orchestra",      "Sequencer for the sound synthesis unit",   XB_APP, XMB_BIND_NONE, L_ORCHESTRA, 0,0,NULL,NULL,IC_APP_ORCHESTRA,0,0,0,0,{0}},
    {"Object Manager", "Browse the virtual object database",       XB_APP, XMB_BIND_NONE, L_VOBJ,      0,0,NULL,NULL,IC_APP_VOBJ,   0,0,0,0,{0}},
    {"TAD Browser",    "Look inside TAD archives",                 XB_APP, XMB_BIND_NONE, L_TAD,       0,0,NULL,NULL,IC_APP_TAD,    0,0,0,0,{0}},
    {"DriveSetup",     "Partition and format storage devices",     XB_APP, XMB_BIND_NONE, L_DRIVE,     0,0,NULL,NULL,IC_APP_DRIVE,  0,0,0,0,{0}},
    {"Chat",           "Open a BeOS-style chat session",           XB_APP, XMB_BIND_NONE, L_CHAT,      0,0,NULL,NULL,IC_APP_CHAT,   0,0,0,0,{0}},
    {"Photo Viewer",   "Display saved Kagee images",               XB_APP, XMB_BIND_NONE, L_KAGEE,     0,0,NULL,NULL,IC_APP_PHOTO,  0,0,0,0,{0}},
    {"Quake",          "Software-rendered OpenGL demo game",       XB_APP, XMB_BIND_NONE, L_QUAKE,     0,0,NULL,NULL,IC_APP_QUAKE,  0,0,0,0,{0}},
};

static char s_sys_lines[XB_MAX_SUB][XB_MSG_LINE_LEN];
static const char *s_sys_live[XB_MAX_SUB];

static xb_item_t s_items_sysdata[] = {
    {"", "", XB_TEXT, XMB_BIND_NONE, L_NONE, 0,0,NULL,NULL,IC_INFO, 0,0,0,0,{0}},
    {"", "", XB_TEXT, XMB_BIND_NONE, L_NONE, 0,0,NULL,NULL,IC_INFO, 0,0,0,0,{0}},
    {"", "", XB_TEXT, XMB_BIND_NONE, L_NONE, 0,0,NULL,NULL,IC_INFO, 0,0,0,0,{0}},
    {"", "", XB_TEXT, XMB_BIND_NONE, L_NONE, 0,0,NULL,NULL,IC_INFO, 0,0,0,0,{0}},
    {"", "", XB_TEXT, XMB_BIND_NONE, L_NONE, 0,0,NULL,NULL,IC_INFO, 0,0,0,0,{0}},
    {"", "", XB_TEXT, XMB_BIND_NONE, L_NONE, 0,0,NULL,NULL,IC_INFO, 0,0,0,0,{0}},
};

/* The Settings band holds menus, not widgets: as on the console, a row of the
 * band opens the list beneath it, and the values change in there.  That is what
 * keeps LEFT/RIGHT on the band a move between bands rather than an edit.
 *
 * The menus follow the B-System's own Settings Cabinet, one row per applet that
 * holds real state, and a row with a bind edits that state where it lives rather
 * than opening the applet.  Both are then views of one variable: change the
 * pointer step here and the Input applet shows it, change it there and this row
 * shows it the next time the menu is opened.  The Cabinet's Desktop, Display,
 * Sound, Network, Media and Security applets hold nothing but their own checkbox
 * state, so there is no value for a row to edit and no row for them. */
static xb_item_t s_items_appearance[] = {
    {"Icon Size",      "Size of the desktop and cabinet icons",  XB_ENUM,  XMB_BIND_ICON_SIZE, L_NONE, 1,1, "32 px|64 px",          NULL, IC_GEAR,  0,0,0,0,{0}},
    {"Window Style",   "Frame drawn around every window",        XB_ENUM,  XMB_BIND_WM_STYLE,  L_NONE, 0,1, "BeOS Tab|Cho-Kanji",   NULL, IC_GEAR,  0,0,0,0,{0}},
};

static xb_item_t s_items_theme[] = {
    {"Colour",         "Background gradient colour",             XB_ENUM,  XMB_BIND_NONE, L_NONE, 0,2, "Deep Blue|Graphite|Gold",  NULL, IC_GEAR,     0,0,0,0,{0}},
    {"Wave Background","Animated waving surface behind the bar", XB_TOGGLE,XMB_BIND_NONE, L_NONE, 1,0, NULL,                       NULL, IC_DROPLET,  0,0,0,0,{0}},
    {"Wave Particles", "Sparkles rising off the waving surface", XB_TOGGLE,XMB_BIND_NONE, L_NONE, 1,0, NULL,                       NULL, IC_DROPLET,  0,0,0,0,{0}},
};

static xb_item_t s_items_screen[] = {
    {"Screen Brightness","Backlight level of the display",       XB_RANGE, XMB_BIND_NONE, L_NONE, 90,100,NULL,                    NULL, IC_SLIDER,   0,0,0,0,{0}},
    {"Edge Fade",        "Fade of list rows near the screen edge",XB_RANGE,XMB_BIND_NONE, L_NONE, 100,100,NULL,                   NULL, IC_SLIDER,   0,0,0,0,{0}},
    {"Icon Shadows",     "Drop shadow under icons and text",     XB_TOGGLE,XMB_BIND_NONE, L_NONE, 1,0, NULL,                       NULL, IC_SLIDER,   0,0,0,0,{0}},
};

static xb_item_t s_items_keyboard[] = {
    {"Auto-Repeat",    "A held key repeats itself",              XB_TOGGLE,XMB_BIND_KBD_REPEAT,L_NONE,1,0,NULL,                   NULL, IC_GEAR,     0,0,0,0,{0}},
    {"Repeat Delay",   "Pause before a held key repeats",        XB_ENUM,  XMB_BIND_KBD_DELAY, L_NONE,0,2,"160 ms|320 ms|500 ms", NULL, IC_GEAR,     0,0,0,0,{0}},
    {"Repeat Rate",    "Speed of a held key's repeat",           XB_ENUM,  XMB_BIND_KBD_RATE,  L_NONE,0,2,"40 cps|25 cps|12 cps", NULL, IC_GEAR,     0,0,0,0,{0}},
};

static xb_item_t s_items_mouse[] = {
    {"Pointer Step",   "How far the pointer travels per move",   XB_ENUM,  XMB_BIND_MOUSE_STEP,   L_NONE,1,3,"1x|2x|3x|4x",           NULL, IC_SLIDER, 0,0,0,0,{0}},
    {"Pointer Curve",  "Acceleration of the pointer",            XB_ENUM,  XMB_BIND_MOUSE_PROFILE,L_NONE,1,1,"Stepped|Continuous",    NULL, IC_SLIDER, 0,0,0,0,{0}},
    {"Lead Hand",      "Which button selects and which adjusts", XB_ENUM,  XMB_BIND_MOUSE_BUTTONS,L_NONE,0,0,"Right|Left",            NULL, IC_SLIDER, 0,0,0,0,{0}},
};

static xb_item_t s_items_terminal[] = {
    {"Console Colours","Ink and paper of the terminal",          XB_ENUM,  XMB_BIND_TERM_THEME,  L_NONE,2,4,"Green|Amber|White|Cyan|Light", NULL, IC_PROMPT, 0,0,0,0,{0}},
    {"Console Font",   "Cell height of terminal text",           XB_ENUM,  XMB_BIND_TERM_FONT,   L_NONE,1,2,"12 pt|16 pt|20 pt",      NULL, IC_PROMPT, 0,0,0,0,{0}},
    {"Console Cursor", "Shape of the terminal cursor",           XB_ENUM,  XMB_BIND_TERM_CURSOR, L_NONE,0,2,"Underline|Block|Bar",    NULL, IC_PROMPT, 0,0,0,0,{0}},
    {"Console Ground", "How much the terminal shows through",    XB_ENUM,  XMB_BIND_TERM_TRANSP, L_NONE,1,2,"Opaque|Dimmed|See-through", NULL, IC_PROMPT, 0,0,0,0,{0}},
};

static xb_item_t s_items_language[] = {
    {"Input Method",   "Script the keyboard types in",           XB_ENUM,  XMB_BIND_TIP_MODE, L_NONE,0,3,"ASCII|Hiragana|Katakana|Tibetan", NULL, IC_GEAR, 0,0,0,0,{0}},
    {"Kana Conversion","Space converts kana into kanji",         XB_TOGGLE,XMB_BIND_TIP_KANA, L_NONE,1,0,NULL,                      NULL, IC_GEAR, 0,0,0,0,{0}},
    {"Kana Popup",     "Tab opens the candidate list",           XB_TOGGLE,XMB_BIND_TIP_TAB,  L_NONE,1,0,NULL,                      NULL, IC_GEAR, 0,0,0,0,{0}},
    {"Arrow Browses",  "Up and Down move through candidates",    XB_TOGGLE,XMB_BIND_TIP_ARROW,L_NONE,1,0,NULL,                      NULL, IC_GEAR, 0,0,0,0,{0}},
    {"Number Picks",   "1 to 9 pick a candidate directly",       XB_TOGGLE,XMB_BIND_TIP_NUMBER,L_NONE,1,0,NULL,                     NULL, IC_GEAR, 0,0,0,0,{0}},
};

static xb_item_t s_items_settings[] = {
    {"Appearance",     "Icon size and window frame style",       XB_SUB,   XMB_BIND_NONE, L_NONE, 0, 0, NULL, NULL, IC_GEAR,
        0, 0, s_items_appearance, 0, {0}},
    {"Theme",          "Colour of the background gradient",      XB_SUB,   XMB_BIND_NONE, L_NONE, 0, 0, NULL, NULL, IC_GEAR,
        0, 0, s_items_theme,      0, {0}},
    {"Screen",         "Brightness, edge fade and shadows",      XB_SUB,   XMB_BIND_NONE, L_NONE, 0, 0, NULL, NULL, IC_SLIDER,
        0, 0, s_items_screen,     0, {0}},
    {"Keyboard",       "Repeat of a held key",                   XB_SUB,   XMB_BIND_NONE, L_NONE, 0, 0, NULL, NULL, IC_GEAR,
        0, 0, s_items_keyboard,   0, {0}},
    {"Mouse",          "Pointer travel, curve and buttons",      XB_SUB,   XMB_BIND_NONE, L_NONE, 0, 0, NULL, NULL, IC_SLIDER,
        0, 0, s_items_mouse,      0, {0}},
    {"Terminal",       "Colours and metrics of the console",     XB_SUB,   XMB_BIND_NONE, L_NONE, 0, 0, NULL, NULL, IC_PROMPT,
        0, 0, s_items_terminal,   0, {0}},
    {"Language",       "Input method and candidate keys",        XB_SUB,   XMB_BIND_NONE, L_NONE, 0, 0, NULL, NULL, IC_GEAR,
        0, 0, s_items_language,   0, {0}},
    {"System Data",    "Version, memory and volume details",     XB_SUB,   XMB_BIND_NONE, L_NONE, 0, 0, NULL, NULL, IC_INFO,
        0, 0, s_items_sysdata,    0, {0}},
};

static xb_item_t s_items_commands[] = {
    {"List root directory",  "Run the shell builtin: ls /",      XB_CMD, XMB_BIND_NONE, L_NONE, 0,0,NULL, "ls /",  IC_PROMPT,0,0,0,0,{0}},
    {"Volume table",         "Run the shell builtin: fs",        XB_CMD, XMB_BIND_NONE, L_NONE, 0,0,NULL, "fs",    IC_PROMPT,0,0,0,0,{0}},
    {"Storage usage",        "Run the shell builtin: df",        XB_CMD, XMB_BIND_NONE, L_NONE, 0,0,NULL, "df",    IC_PROMPT,0,0,0,0,{0}},
    {"Kernel information",   "Run the shell builtin: info",      XB_CMD, XMB_BIND_NONE, L_NONE, 0,0,NULL, "info",  IC_PROMPT,0,0,0,0,{0}},
    {"Sync all volumes",     "Run the shell builtin: sync",      XB_CMD, XMB_BIND_NONE, L_NONE, 0,0,NULL, "sync",  IC_PROMPT,0,0,0,0,{0}},
};

/* ── The Games band: the cartridge images in the host's assets/msx ─────────
 *
 * One row per ROM, made by reading that folder on the way in, and each row opens
 * the B-MSX window with its own cartridge loaded.  The list is the folder: nothing
 * here copies what a ROM is called, and a machine built without a host file system
 * simply has no rows, the way an empty volume has none.
 */

#define XB_GAME_ROWS   16
#define XB_GAME_NAME   64
#define XB_GAME_DIR    "assets/msx"
#define XB_GAME_SUB    "MSX cartridge: opens in B-MSX"

static char      s_game_names[XB_GAME_ROWS][XB_GAME_NAME];
static xb_item_t s_items_games[XB_GAME_ROWS];   /* filled by xb_games_scan() */
static int       s_game_count;

/* ── The Discs band: the mounted volumes, through the CLU's own VFS ──────
 *
 * The bar keeps no model of the storage.  It asks the same virtual file system the
 * "sc" file manager asks - src/clu/vfs.c, over opn_dir/rd_dir - for one folder at a
 * time, and every row of the list is one entry of that listing.  A folder's contents
 * are therefore never a copy the menu holds: they are read on the way in and read
 * again on the way back, which is what makes the traversal endless.  There is no
 * fixed tree to run out of, only the tree the volumes hold, at whatever depth they
 * hold it.
 *
 * What is remembered per level is only the part a listing cannot give back: which
 * row the cursor was on and the name it was on, exactly as sc's DirHistory does.
 * Going up one level is the append undone - the last path segment dropped, the
 * folder listed again, the cursor put back where it came from.
 */

typedef struct {
    char name[VFS_MAX_NAME];   /* the row that was opened, for the title   */
    int  cursor;               /* its index in the parent's list           */
} xb_disc_mark_t;

typedef struct {
    char      path[VFS_MAX_PATH];               /* the folder on screen */
    xb_item_t items[XB_DISC_ROWS];
    char      names[XB_DISC_ROWS][VFS_MAX_NAME];/* the entry, as the volume spells it */
    char      shown[XB_DISC_ROWS][VFS_MAX_NAME];/* its ASCII face, what the row labels */
    char      subs[XB_DISC_ROWS][XB_DISC_SUB];  /* a volume's own description line */
    uint32_t  fid[XB_DISC_ROWS];                /* the Real Body the row named */
    uint32_t  size[XB_DISC_ROWS];               /* and how much of it it holds */
    int       count;
    int       sel;
} xb_disc_t;

static xb_disc_t         s_disc;
static xb_disc_mark_t    s_disc_marks[XB_DISC_HISTORY];
static int               s_disc_marks_n;   /* levels below the volume list */
static VfsEntry          s_disc_scan[XB_DISC_ROWS];

static xb_cat_t s_cats[] = {
    {"Applications","Launch B-System applications", IC_CAT_APPS,     s_items_apps,     0, 0, 0, {0}},
    {"Settings",    "Configure the B-System",       IC_CAT_SETTINGS, s_items_settings, 0, 0, 0, {0}},
    /* The Discs band has no table of its own: its rows are the folder it holds,
     * so items points at the list buffer and files says the level functions are
     * the ones that answer for it. */
    {"Discs",       "Browse the mounted volumes",   IC_MINIDISC,     s_disc.items,     0, 0, 1, {0}},
    /* The Games band is a listing too, but a flat one: xb_games_scan() makes its
     * rows from assets/msx once, before the first frame. */
    {"Games",       "Play MSX cartridge images",    IC_CAT_GAMES,    s_items_games,    0, 0, 0, {0}},
    {"Commands",    "Run B-System shell commands",  IC_CAT_COMMANDS, s_items_commands, 0, 0, 0, {0}},
};
static int s_cat_count = XB_NEL(s_cats);

enum { BAND_APPS = 0, BAND_SETTINGS, BAND_DISCS, BAND_GAMES, BAND_COMMANDS };

/* ── Bindings ──────────────────────────────────────────────────────────
 * A bound row holds nothing of its own: its value is read out of the system it
 * names and written straight back.  The two functions below are the whole of that
 * translation, and they are the only place in this file that touches another
 * module's state, so what a row can say is exactly what that module can hold.
 *
 * Where the system stores a value in its own units - the repeat delay in
 * microseconds, the icon size in pixels, the font in points - the row shows the
 * legal choices as an enum, and the pair maps index to unit and back.  Where the
 * system stores a number a row can be stepped through - the pointer's step
 * multiplier - the row clamps to the system's own range and the read-back is what
 * stays on screen, so a value the system refuses never shows as if it took. */

/* The Input applet's own index-to-microsecond tables, repeated here because that
 * applet keeps them inside its static state.  Both write the same six kernel
 * variables, so the two stay in step by reading the same numbers back. */
static const uint32_t s_kbd_delay_us[3]  = { 160000U, 320000U, 500000U };
static const uint32_t s_kbd_rate_us[3]   = {  25000U,  40000U,  80000U };

/* A row can only be walked off the end of its list by a race, never by a key, so
 * this is a belt for the array indexes rather than a clamp the user can reach. */
static int xb_index_clamp(int v, int hi)
{
    if (v < 0)  return 0;
    if (v > hi) return hi;
    return v;
}

static int xb_term_read(int bind)
{
    TERMINAL_SETTINGS ts;

    terminal_get_settings(&ts);
    switch (bind) {
    case XMB_BIND_TERM_THEME:  return (int)ts.theme;
    case XMB_BIND_TERM_FONT:   return ((int)ts.font_size - 12) / 4;
    case XMB_BIND_TERM_CURSOR: return (int)ts.cursor_style;
    case XMB_BIND_TERM_TRANSP: return (int)ts.transparency;
    default:                   return 0;
    }
}

static void xb_term_write(int bind, int value)
{
    TERMINAL_SETTINGS ts;

    terminal_get_settings(&ts);
    switch (bind) {
    case XMB_BIND_TERM_THEME:  ts.theme        = (TERM_COLOR_THEME)value; break;
    case XMB_BIND_TERM_FONT:   ts.font_size    = (TERM_FONT_SIZE)(12 + 4 * value); break;
    case XMB_BIND_TERM_CURSOR: ts.cursor_style = (TERM_CURSOR_STYLE)value; break;
    case XMB_BIND_TERM_TRANSP: ts.transparency = (TERM_TRANSPARENCY)value; break;
    default: return;
    }
    terminal_set_settings(&ts);
}

static int xb_tip_read(int bind)
{
    TIP_KEY_SETTINGS ks;

    if (bind == XMB_BIND_TIP_MODE)
        return (int)tip_get_mode();
    tip_get_key_settings(&ks);
    switch (bind) {
    case XMB_BIND_TIP_KANA:   return ks.jp_space_is_convert  ? 1 : 0;
    case XMB_BIND_TIP_TAB:    return ks.jp_tab_is_popup      ? 1 : 0;
    case XMB_BIND_TIP_ARROW:  return ks.arrow_nav_enabled    ? 1 : 0;
    case XMB_BIND_TIP_NUMBER: return ks.num_select_enabled   ? 1 : 0;
    default:                  return 0;
    }
}

static void xb_tip_write(int bind, int value)
{
    TIP_KEY_SETTINGS ks;

    if (bind == XMB_BIND_TIP_MODE) {
        tip_set_mode((TIP_INPUT_MODE)value);
        return;
    }
    tip_get_key_settings(&ks);
    switch (bind) {
    case XMB_BIND_TIP_KANA:   ks.jp_space_is_convert = value ? TRUE : FALSE; break;
    case XMB_BIND_TIP_TAB:    ks.jp_tab_is_popup     = value ? TRUE : FALSE; break;
    case XMB_BIND_TIP_ARROW:  ks.arrow_nav_enabled   = value ? TRUE : FALSE; break;
    case XMB_BIND_TIP_NUMBER: ks.num_select_enabled  = value ? TRUE : FALSE; break;
    default: return;
    }
    tip_set_key_settings(&ks);
}

static int xb_bind_read(int bind)
{
    switch (bind) {
    case XMB_BIND_ICON_SIZE:
        return appearance_get_icon_size() == BTRON_ICON_SIZE_64 ? 1 : 0;
    case XMB_BIND_WM_STYLE:
        return pmc_get_style() == WM_STYLE_CHOKANJI ? 1 : 0;
    case XMB_BIND_KBD_REPEAT:
        return g_kbd_repeat_enabled ? 1 : 0;
    case XMB_BIND_KBD_DELAY:
        return g_kbd_repeat_delay_us <= s_kbd_delay_us[0] ? 0
             : (g_kbd_repeat_delay_us <= s_kbd_delay_us[1] ? 1 : 2);
    case XMB_BIND_KBD_RATE:
        return g_kbd_repeat_interval_us <= s_kbd_rate_us[0] ? 0
             : (g_kbd_repeat_interval_us <= s_kbd_rate_us[1] ? 1 : 2);
    case XMB_BIND_MOUSE_STEP:
        if (g_mouse_step_mult < 1) return 0;
        if (g_mouse_step_mult > 4) return 3;
        return g_mouse_step_mult - 1;
    case XMB_BIND_MOUSE_PROFILE:
        return g_mouse_accel_profile ? 1 : 0;
    case XMB_BIND_MOUSE_BUTTONS:
        return g_mouse_swap_select_adjust ? 1 : 0;
    case XMB_BIND_TERM_THEME:
    case XMB_BIND_TERM_FONT:
    case XMB_BIND_TERM_CURSOR:
    case XMB_BIND_TERM_TRANSP:
        return xb_term_read(bind);
    case XMB_BIND_TIP_MODE:
    case XMB_BIND_TIP_KANA:
    case XMB_BIND_TIP_TAB:
    case XMB_BIND_TIP_ARROW:
    case XMB_BIND_TIP_NUMBER:
        return xb_tip_read(bind);
    default:
        return 0;
    }
}

static void xb_bind_write(int bind, int value)
{
    switch (bind) {
    case XMB_BIND_ICON_SIZE:
        appearance_set_icon_size(value ? BTRON_ICON_SIZE_64 : BTRON_ICON_SIZE_32);
        break;
    case XMB_BIND_WM_STYLE:
        pmc_set_style(value ? WM_STYLE_CHOKANJI : WM_STYLE_BEOS);
        break;
    case XMB_BIND_KBD_REPEAT:
        g_kbd_repeat_enabled = value ? 1 : 0;
        break;
    case XMB_BIND_KBD_DELAY:
        g_kbd_repeat_delay_us = s_kbd_delay_us[xb_index_clamp(value, 2)];
        break;
    case XMB_BIND_KBD_RATE:
        g_kbd_repeat_interval_us = s_kbd_rate_us[xb_index_clamp(value, 2)];
        break;
    case XMB_BIND_MOUSE_STEP:
        g_mouse_step_mult = 1 + xb_index_clamp(value, 3);
        break;
    case XMB_BIND_MOUSE_PROFILE:
        g_mouse_accel_profile = value ? 1 : 0;
        break;
    case XMB_BIND_MOUSE_BUTTONS:
        g_mouse_swap_select_adjust = value ? 1 : 0;
        break;
    case XMB_BIND_TERM_THEME:
    case XMB_BIND_TERM_FONT:
    case XMB_BIND_TERM_CURSOR:
    case XMB_BIND_TERM_TRANSP:
        xb_term_write(bind, value);
        break;
    case XMB_BIND_TIP_MODE:
    case XMB_BIND_TIP_KANA:
    case XMB_BIND_TIP_TAB:
    case XMB_BIND_TIP_ARROW:
    case XMB_BIND_TIP_NUMBER:
        xb_tip_write(bind, value);
        break;
    default:
        break;
    }
}

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

/* The bar's bitmap font carries ASCII 32..126, so a name the atlas cannot show is
 * drawn one underscore per character.  Only the face of the name is changed here:
 * the volume's own spelling is kept whole, because the paths walked from it have to
 * name the entry the listing gave and not a rendering of it. */
static void xb_disc_ascii(char *out, size_t out_max, const char *name)
{
    size_t o = 0;
    const unsigned char *p = (const unsigned char *)name;

    if (out_max == 0)
        return;
    while (*p && o + 1 < out_max) {
        if (*p >= 32 && *p <= 126) {
            out[o++] = (char)*p++;
        } else if (*p >= 0xC0) {           /* the lead byte of a UTF-8 sequence */
            p++;
            while ((*p & 0xC0) == 0x80)
                p++;
            out[o++] = '_';
        } else {
            out[o++] = '_';                /* a control byte, or a stray continuation */
            p++;
        }
    }
    out[o] = '\0';
}

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

/* The three functions below are the whole of the bar's idea of where its cursor
 * is, so a band whose list is a folder rather than a table only has to say so here
 * and every walk, animation and drawing pass follows. */
static xb_item_t *xb_cur_items(int *count)
{
    if (s_cats[s_cat].files) {
        *count = s_disc.count;
        return s_disc.items;
    }
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
    if (s_cats[s_cat].files)
        return s_disc.sel;
    return s_depth > 1 ? s_sub_sel : s_cats[s_cat].selection;
}

static void xb_set_selection(int v)
{
    if (s_cats[s_cat].files) {
        s_disc.sel = v;
        /* The band's own cursor is the volume list's, and it survives a trip to
         * another band and back, as every other band's does. */
        if (s_disc_marks_n == 0)
            s_cats[s_cat].selection = v;
        return;
    }
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

    case IC_CAT_GAMES:
        /* The Games band: a cartridge shell with its label rules and the two
         * slots a reader grips. */
        d = STROKE(xb_sd_rbox(x, y, 0.0f, 0.02f, 0.28f, 0.36f, 0.05f), XB_W * 0.8f);
        d = UN(d, xb_sd_capsule(x, y, -0.15f, -0.20f, 0.15f, -0.20f, 0.032f));
        d = UN(d, xb_sd_capsule(x, y, -0.15f, -0.06f, 0.15f, -0.06f, 0.032f));
        d = UN(d, xb_sd_capsule(x, y, -0.15f,  0.08f, 0.02f,  0.08f, 0.032f));
        d = UN(d, xb_sd_rbox(x, y, -0.13f, 0.30f, 0.07f, 0.028f, 0.014f));
        d = UN(d, xb_sd_rbox(x, y,  0.13f, 0.30f, 0.07f, 0.028f, 0.014f));
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

    case IC_MINIDISC:
        /* The MiniDisc case the Discs band carries: a shell with its corner cut,
         * the hub window in the middle and the two notches a drive reads. */
        d = STROKE(SUB(xb_sd_rbox(x, y, 0.0f, 0.0f, 0.28f, 0.34f, 0.06f),
                       xb_sd_tri(x, y, -0.10f, -0.30f,
                                    -0.30f, -0.10f,
                                    -0.62f, -0.62f)), XB_W * 0.8f);
        d = UN(d, xb_sd_ring(x, y, 0.0f, 0.06f, 0.13f, XB_W * 0.8f));
        d = UN(d, xb_sd_disc(x, y, 0.0f, 0.06f, 0.045f));
        d = UN(d, xb_sd_rbox(x, y, -0.17f, 0.28f, 0.05f, 0.025f, 0.015f));
        d = UN(d, xb_sd_rbox(x, y,  0.17f, 0.28f, 0.05f, 0.025f, 0.015f));
        break;

    case IC_FILE:
        /* A body: a page with its top-right corner turned back */
        d = STROKE(SUB(xb_sd_rbox(x, y, 0.0f, 0.02f, 0.24f, 0.32f, 0.03f),
                       xb_sd_tri(x, y,  0.10f, -0.34f,
                                    0.30f, -0.14f,
                                    0.62f, -0.62f)), XB_W * 0.7f);
        d = UN(d, xb_sd_capsule(x, y, -0.12f, 0.12f, 0.12f, 0.12f, 0.032f));
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

/* Bake the icon atlas and the 8x16 bitmap-font atlas, then upload both.
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
    for (cell = 0; cell < XB_ATLAS_GRID * XB_ATLAS_GRID; cell++) {
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

    /* Fragment stage of pipeline_xmb_ribbon.cg.h:
     *   X = ddx(vEC);  Y = -ddy(vEC);  normal = normalize(cross(X, Y));
     *   c = 1 - dot(normal, up), up = (0,0,1);   bright = (1 - cos(c*c)) / 13.
     * ddx/ddy are per *screen* pixel, not per grid step.  Screen x tracks the
     * column index alone (ex = gx) and screen y tracks ey, so inverting the
     * (column,row) -> (x,y) Jacobian gives
     *   X = (1, 0, (az - ay*n) / ax)   with n = bz / by
     *   Y = (0, 1, bz / by)
     * (ex does not change along a row, which is what kills the x-component of
     * X's partner).  cross(X, -Y) then has z = -1, so
     *   dot(normal, up) = -1 / sqrt(M*M + n*n + 1)
     * and c = 1 + 1/len lies in [1, 2], so the sheet can never darken the
     * background and its lift runs from (1-cos 1)/13 = 3.5% at its flattest to
     * the expression's peak, 2/13 = 15.4%, where cc*cc reaches pi on a steep
     * fold (it falls back to (1-cos 4)/13 = 12.7% at the steepest cc of 2). */
    for (r = 0; r < XB_RIBBON_ROWS; r++) {
        int r0 = r > 0 ? r - 1 : r;
        int r1 = r < XB_RIBBON_ROWS - 1 ? r + 1 : r;
        for (c = 0; c < XB_RIBBON_COLS; c++) {
            int c0 = c > 0 ? c - 1 : c;
            int c1 = c < XB_RIBBON_COLS - 1 ? c + 1 : c;
            float ax = ex[r][c1] - ex[r][c0];
            float ay = ey[r][c1] - ey[r][c0];
            float az = ez[r][c1] - ez[r][c0];
            float by = ey[r1][c] - ey[r0][c];
            float bz = ez[r1][c] - ez[r0][c];
            float bright;

            if (fabsf(ax) < 1e-6f || fabsf(by) < 1e-6f) {
                /* Flat in screen y, or the last column: the screen derivatives
                 * blow up and the normal tends to (0,0,-1) - the shader's
                 * minimum lift. */
                bright = (1.0f - cosf(1.0f)) / 13.0f;
            } else {
                float n  = bz / by;
                float m  = (az - ay * n) / ax;
                float cc = 1.0f + 1.0f / sqrtf(m * m + n * n + 1.0f);
                bright   = (1.0f - cosf(cc * cc)) / 13.0f;
            }
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
     * category level the PS3 shows "Category > Item" on the same line.  For the
     * Discs band the item named is the row the level below was opened from, which
     * is what the mark for this level holds - the list under the cursor has moved
     * on into that row's folder by now. */
    {
        float tx = s_title_left
                 + (s_depth > 1 ? s_icon_size * 0.45f : s_icon_size / 2.5f);
        xb_draw_text(cat->label, tx, s_title_top, s_font, 1.0f, 1);
        if (s_depth > 1) {
            char nm[64];
            const char *item;
            if (cat->files) {
                xb_disc_ascii(nm, sizeof(nm), s_disc_marks[s_depth - 2].name);
                item = nm;
            } else {
                item = cat->items[cat->selection].label;
            }
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

    /* xmb.c:6155 - vertical fade of rows approaching the screen edges */
    if (s_fade > 0) {
        float icon_space = s_spacing_v;
        float icon_ratio = icon_space / (float)s_h / icon_space * 4.0f;
        float scr_margin = s_margin_top + (icon_space / icon_ratio / 400.0f);
        float factor     = (float)s_fade / 100.0f / icon_ratio;
        float min_alpha  = 0.01f;
        float max_alpha  = (i == current) ? 1.0f : 0.75f;
        float new_alpha  = n->alpha;

        if (i < current)
            new_alpha = (n->y + scr_margin) / factor;
        else if (i > current)
            new_alpha = ((float)s_h - n->y - scr_margin + icon_space) / factor;

        new_alpha = xb_clampf(new_alpha, min_alpha, max_alpha);
        /* xmb.c:6181 - a row that is still sliding owns its own alpha */
        if (new_alpha < n->alpha || n->x == 0.0f) {
            n->alpha      = new_alpha;
            n->label_alpha = new_alpha;
        }
    }

    /* xmb.c:6270 - the icon colour, after the fade has settled node->alpha.
     * Passive rows are dimmed by 1/1.25 straight off node->alpha, which is
     * how the original differentiates the active entry. */
    a = xb_minf(n->alpha * s_alpha_list, s_alpha);
    if (i != current)
        a = xb_minf(n->alpha / 1.25f, s_alpha);

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

    /* The value of the row the list holds, set a field's width after its label:
     * a toggle, a percentage or one of an enum's choices, never a second copy of
     * what the system already keeps. */
    value[0] = '\0';
    xb_value_text(it, value, sizeof(value));
    if (value[0])
        xb_draw_text(value,
             n->x + s_margin_left + s_spacing_h + s_label_left + s_setting_left,
             s_margin_top + n->y + s_label_top, s_font,
             n->label_alpha * s_alpha_list, 1);

    /* An arrow after the value marks a row that opens a list of its own - a menu
     * in the Settings band, a drawer or volume in the Discs band */
    if (i == current && (it->kind == XB_SUB || it->kind == XB_DIR))
        xb_draw_icon(IC_ARROW,
             n->x + s_margin_left + s_spacing_h + s_label_left + s_setting_left
             + xb_text_w(value, s_font) + s_font,
             s_margin_top + n->y + s_label_top + s_font * 0.5f,
             s_font, 0.6f, n->label_alpha * s_alpha_list);

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
    int count = 0;
    xb_item_t *items = xb_cur_items(&count);
    int current = xb_cur_selection();
    int kind = (items && current >= 0 && current < count)
             ? items[current].kind : XB_APP;

    /* The bitmap font atlas carries codes 32-126 only, so the separator
     * between hints is an ASCII bar rather than a middle dot */
    snprintf(buf, sizeof(buf), "%s",
             s_msg_open ? "ESC Close" :
             (kind == XB_APP)     ? "ENTER Launch  |  ESC Quit" :
             (kind == XB_CMD)     ? "ENTER Run  |  ESC Quit" :
             (kind == XB_SUB)     ? "ENTER Open  |  ESC Back" :
             (kind == XB_DIR)     ? "ENTER Open  |  ESC Back" :
             (kind == XB_FILE)    ? "ENTER View  |  ESC Back" :
                                    "LEFT/RIGHT Adjust  |  ENTER Set");

    /* A folder is a long way from the volume list by the time the bar has gone
     * down several levels, and the levels above the cursor have scrolled off the
     * top of the list, so the path the rows came from is said outright. */
    if (s_cats[s_cat].files) {
        char where[VFS_MAX_PATH / 4];
        char full[128];
        char shown[64];
        size_t len;

        vfs_display_path(s_disc.path, where, sizeof(where));
        xb_disc_ascii(full, sizeof(full), where);
        len = strlen(full);
        if (len + 1 <= sizeof(shown)) {
            memcpy(shown, full, len + 1);
        } else {
            /* A folder nine levels down is a long way from the volume list, and the
             * levels above the cursor are the ones already read: the path is kept
             * from its deep end, at a whole segment, with the front left off. */
            const char *tail = full + len - (sizeof(shown) - 3);
            const char *slash = strchr(tail, '/');
            if (slash)
                tail = slash;
            shown[0] = '.';
            shown[1] = '.';
            snprintf(shown + 2, sizeof(shown) - 2, "%s", tail);
        }
        xb_draw_text(shown, s_margin_left,
                     (float)s_h - s_font * 1.4f - s_font2 * 1.8f, s_font2,
                     0.45f * s_alpha, 0);
    }

    xb_draw_text(buf, s_margin_left, (float)s_h - s_font * 1.4f, s_font2,
                 0.55f * s_alpha, 0);
}

/* ── Animations ─────────────────────────────────────────────────────── */

/* xmb_selection_pointer_changed() and xmb_list_open_new() do not use the drawn
 * window to decide what animates: a row more than ten icons off the screen is
 * placed at its target instead of tweened to it. */
static int xb_row_off_screen(float iy)
{
    float real_iy = iy + s_margin_top;
    return (real_iy < -XB_ANIM_THRESHOLD || real_iy > (float)s_h + XB_ANIM_THRESHOLD)
           ? 1 : 0;
}

/* xmb_selection_pointer_changed(): the row walk */
static void xb_anim_row_move(void)
{
    int count = 0, i;
    int current = xb_cur_selection();
    xb_item_t *items = xb_cur_items(&count);
    uintptr_t tag = (uintptr_t)items;

    if (!items)
        return;

    for (i = 0; i < count; i++) {
        xb_node_t *n = &items[i].node;
        float ia = (i == current) ? XB_ITEMS_ACTIVE_ALPHA
                                  : XB_ITEMS_PASSIVE_ALPHA;
        float iz = (i == current) ? XB_ITEMS_ACTIVE_ZOOM
                                  : XB_ITEMS_PASSIVE_ZOOM;
        float iy = xb_item_y(i, current);

        if (xb_row_off_screen(iy)) {
            n->y     = iy;
            n->alpha = ia;
            n->label_alpha = ia;
            n->zoom  = iz;
            continue;
        }
        /* anim_move_up_down == 0: XMB_DELAY with EASING_OUT_QUAD */
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
        float ia = (i == s_cat) ? XB_CATEGORIES_ACTIVE_ALPHA
                                : XB_CATEGORIES_PASSIVE_ALPHA;
        float iz = (i == s_cat) ? XB_CATEGORIES_ACTIVE_ZOOM
                                : XB_CATEGORIES_PASSIVE_ZOOM;
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
        float ia = (i == current) ? XB_ITEMS_ACTIVE_ALPHA
                                  : XB_ITEMS_PASSIVE_ALPHA;

        n->x           = s_spacing_h * (float)dir;
        n->alpha       = 0.0f;
        n->label_alpha = 0.0f;
        n->y           = xb_item_y(i, current);
        n->zoom        = (i == current) ? XB_ITEMS_ACTIVE_ZOOM
                                        : XB_ITEMS_PASSIVE_ZOOM;

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

/* xmb_list_open_new(): every row is placed at its target y and zoom, and only
 * the fading rows get their alpha and x tweened. dir is +1 going in and -1
 * coming back, which decides where the rows start off to the side and which of
 * them keep an alpha to fade from. */
static void xb_anim_list_open(int dir)
{
    int count = 0, i;
    int current = xb_cur_selection();
    xb_item_t *items = xb_cur_items(&count);
    uintptr_t tag;
    float target_x;

    if (!items)
        return;
    tag = (uintptr_t)items;

    for (i = 0; i < count; i++) {
        xb_node_t *n = &items[i].node;
        float ia;

        if (dir > 0) {
            n->alpha       = 0.0f;
            n->label_alpha = 0.0f;
        } else {
            /* Coming back: the row the cursor is on keeps whatever alpha it
             * already had, so it carries on instead of blinking. */
            if (i != current)
                n->alpha = 0.0f;
            n->label_alpha = 0.0f;
        }

        n->x    = s_icon_size * (float)dir * 2.0f;
        n->y    = xb_item_y(i, current);
        n->zoom = XB_CATEGORIES_PASSIVE_ZOOM;

        if (i == current) {
            n->zoom = XB_CATEGORIES_ACTIVE_ZOOM;
            ia      = XB_ITEMS_ACTIVE_ALPHA;
        } else {
            ia      = XB_ITEMS_PASSIVE_ALPHA;
        }

        if (xb_row_off_screen(n->y)) {
            n->alpha       = ia;
            n->label_alpha = ia;
            n->x           = 0.0f;
            continue;
        }

        /* The fading-in rows start a fifth of wherever they were */
        n->alpha /= 5.0f;
        xb_tween_push(&n->alpha,       ia, XB_DELAY, XB_EASING_ALPHA, tag);
        xb_tween_push(&n->label_alpha, ia, XB_DELAY, XB_EASING_ALPHA, tag);
        xb_tween_push(&n->x,        0.0f, XB_DELAY, XB_EASING_XY, tag);
    }

    /* xmb_list_open(): the whole bar slides aside by one icon per depth, with no
     * ceiling - xmb.c:3906 tweens to icon_size * 1.1 * -(depth * 2 - 2) for the
     * depth the menu is actually at, which is how a DLNA browser carries you off
     * the categories and into the folder stack. */
    target_x = s_icon_size * XB_BAR_OFFSET * -(float)(s_depth * 2 - 2);
    xb_tween_push(&s_x, target_x, XB_DELAY, XB_EASE_OUT_QUAD,
                  (uintptr_t)&s_x);
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
    s_x      = s_icon_size * XB_BAR_OFFSET * -(float)(s_depth * 2 - 2);
}

/* ── Actions ────────────────────────────────────────────────────────── */

/* xb_move_row() walks off the top of an opened menu to close it */
static void xb_go_back(void);

static void xb_refresh_settings(void)
{
    s_theme     = s_items_theme[THEME_COLOUR].value;
    s_wave      = s_items_theme[THEME_WAVE].value;
    s_particles = s_items_theme[THEME_PARTICLES].value;
    s_shadows   = s_items_screen[SCR_SHADOWS].value;
    s_brightness= (float)s_items_screen[SCR_BRIGHT].value / 100.0f;
    s_fade      = s_items_screen[SCR_FADE].value;
}

/* The rows of the list under the cursor show the system as it is now, so a value
 * changed elsewhere - in an applet, by a driver, by the previous test - is on
 * screen the moment the menu opens.  Called wherever the list changes. */
static void xb_sync_list(void)
{
    int count = 0, i;
    xb_item_t *items = xb_cur_items(&count);

    for (i = 0; i < count && items; i++)
        if (items[i].bind != XMB_BIND_NONE)
            items[i].value = xb_bind_read(items[i].bind);
}

/* A row's value goes to the system first, and what the system kept is what the row
 * shows: a value it refuses is not remembered by the menu either. */
static void xb_set_value(xb_item_t *it, int value)
{
    if (it->bind != XMB_BIND_NONE) {
        xb_bind_write(it->bind, value);
        it->value = xb_bind_read(it->bind);
        return;
    }
    it->value = value;
    xb_refresh_settings();
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

/* ── Discs: one folder at a time ────────────────────────────────────────
 * The whole of the band's content comes from vfs_list_dir(), which is the same
 * call "sc" makes: the volume list at "/", and below it a folder's entries with
 * each one's Real Body id carried along so that the row opens the body it names.
 * Nothing here keeps a second model of the storage: leaving and re-entering a
 * folder re-reads it, so the rows are what the volume holds at that moment. */

static char s_disc_text[720];   /* the head of one body, for the message box */

/* Drawers before bodies, then each class in the order sc's compare_files() gives
 * it - by name.  An insertion sort over one folder's listing, bounded by the
 * number of rows the band can show rather than by recursion. */
static int xb_disc_before(const VfsEntry *a, const VfsEntry *b)
{
    if (a->is_dir != b->is_dir)
        return a->is_dir ? -1 : 1;
    return strcmp(a->name, b->name);
}

static void xb_disc_sort(int n)
{
    int i;

    for (i = 1; i < n; i++) {
        VfsEntry keep = s_disc_scan[i];
        int j = i - 1;
        while (j >= 0 && xb_disc_before(&s_disc_scan[j], &keep) > 0) {
            s_disc_scan[j + 1] = s_disc_scan[j];
            j--;
        }
        s_disc_scan[j + 1] = keep;
    }
}

/* One listing into the rows the bar will draw */
static void xb_disc_fill(int n)
{
    int rows = 0, i;
    int at_root = (s_disc.path[0] == '/' && s_disc.path[1] == '\0') ? 1 : 0;

    for (i = 0; i < n && rows < XB_DISC_ROWS; i++) {
        const VfsEntry *e = &s_disc_scan[i];
        xb_item_t *it;

        if (e->name[0] == '\0')
            continue;

        snprintf(s_disc.names[rows], VFS_MAX_NAME, "%s", e->name);
        xb_disc_ascii(s_disc.shown[rows], VFS_MAX_NAME, e->name);
        s_disc.subs[rows][0] = '\0';
        s_disc.fid[rows]  = e->fid;
        s_disc.size[rows] = e->size;

        it = &s_disc.items[rows];
        if (at_root && e->is_dir) {
            /* A mounted volume: the MiniDisc, and the description line the console
             * shows under the row the cursor rests on. */
            Volume *v = vol_find_by_name(e->name);
            it->icon = IC_MINIDISC;
            if (v)
                snprintf(s_disc.subs[rows], XB_DISC_SUB, "%s, %u KiB, %u free",
                         vol_description(v), (unsigned)(e->size / 1024u),
                         (unsigned)vol_free_blocks(v));
            else
                snprintf(s_disc.subs[rows], XB_DISC_SUB, "Mounted volume, %u KiB",
                         (unsigned)(e->size / 1024u));
        } else {
            it->icon = e->is_dir ? IC_FOLDER : IC_FILE;
        }
        it->label = s_disc.shown[rows];
        it->sub   = s_disc.subs[rows];
        it->kind  = e->is_dir ? XB_DIR : XB_FILE;
        it->bind  = XMB_BIND_NONE;
        it->value = 0;
        rows++;
    }

    s_disc.count = rows;
}

static void xb_disc_reload(void)
{
    int n = vfs_list_dir(s_disc.path, s_disc_scan, XB_DISC_ROWS);
    if (n < 0)
        n = 0;
    xb_disc_sort(n);
    xb_disc_fill(n);
}

/* The top of the band: the list of mounted volumes.  Reached on the way in and on
 * every band switch, and the row the band was left on is the row it comes back to. */
static void xb_disc_restart(void)
{
    snprintf(s_disc.path, sizeof(s_disc.path), "%s", "/");
    s_disc_marks_n = 0;
    xb_disc_reload();
    s_disc.sel = s_cats[BAND_DISCS].selection;
    if (s_disc.sel >= s_disc.count)
        s_disc.sel = s_disc.count > 0 ? s_disc.count - 1 : 0;
    s_cats[BAND_DISCS].selection = s_disc.sel;
}

/* ── The Games band's listing ─────────────────────────────────────────── */

#if BTRON_HOSTED
/* A cartridge image is a .rom, whatever case its folder spells it in. */
static BOOL xb_game_is_rom(const char *name)
{
    size_t n = strlen(name);

    if (n < 5 || n >= XB_GAME_NAME || name[n - 4] != '.')
        return FALSE;
    return (name[n - 3] == 'r' || name[n - 3] == 'R')
        && (name[n - 2] == 'o' || name[n - 2] == 'O')
        && (name[n - 1] == 'm' || name[n - 1] == 'M');
}
#endif

/* Read the folder once and make one XB_APP row per ROM, in name order so the bar
 * lands on the same row from one run to the next.  The rows carry the folder's
 * index, which is how xb_launch() names the cartridge to B-MSX with. */
static void xb_games_scan(void)
{
    int rows = 0;

#if BTRON_HOSTED
    DIR *dir = opendir(XB_GAME_DIR);
    struct dirent *de;

    if (dir) {
        while ((de = readdir(dir)) != NULL && rows < XB_GAME_ROWS) {
            if (de->d_name[0] == '.')
                continue;
            if (!xb_game_is_rom(de->d_name))
                continue;
            snprintf(s_game_names[rows], XB_GAME_NAME, "%s", de->d_name);
            rows++;
        }
        closedir(dir);
    }

    for (int i = 1; i < rows; i++) {
        char name[XB_GAME_NAME];
        int j = i - 1;

        snprintf(name, XB_GAME_NAME, "%s", s_game_names[i]);
        while (j >= 0 && strcmp(s_game_names[j], name) > 0) {
            snprintf(s_game_names[j + 1], XB_GAME_NAME, "%s", s_game_names[j]);
            j--;
        }
        snprintf(s_game_names[j + 1], XB_GAME_NAME, "%s", name);
    }
#endif

    for (int i = 0; i < rows; i++) {
        xb_item_t *it = &s_items_games[i];

        memset(it, 0, sizeof(*it));
        it->label  = s_game_names[i];
        it->sub    = XB_GAME_SUB;
        it->kind   = XB_APP;
        it->bind   = XMB_BIND_NONE;
        it->launch = L_MSX;
        it->value  = i;
        it->icon   = IC_CAT_GAMES;
    }
    s_game_count = rows;
}

/* ENTER on a Games row: the cartridge the row names, by the index it carries. */
static void xb_game_launch(const xb_item_t *it)
{
    if (it->value < 0 || it->value >= s_game_count)
        return;
    if (open_msx_window_with_rom)
        open_msx_window_with_rom(s_game_names[it->value]);
}

/* ENTER on a drawer: the level below is the entry's own Real Body, reached through
 * vfs_child_path() so that a folder holding two bodies of one name cannot send the
 * cursor into the first of them. */
static void xb_disc_enter(int row)
{
    char child[VFS_MAX_PATH];
    xb_disc_mark_t *m;

    if (row < 0 || row >= s_disc.count || s_disc_marks_n >= XB_DISC_HISTORY)
        return;

    m = &s_disc_marks[s_disc_marks_n];
    snprintf(m->name, sizeof(m->name), "%s", s_disc.names[row]);
    m->cursor = row;
    s_disc_marks_n++;

    vfs_child_path(child, sizeof(child), s_disc.path,
                   s_disc.names[row], s_disc.fid[row]);
    snprintf(s_disc.path, sizeof(s_disc.path), "%s", child);
    xb_disc_reload();
    s_disc.sel = 0;
    s_depth    = 1 + s_disc_marks_n;
    xb_sync_list();
    xb_anim_list_open(1);
}

/* Up one level: the append undone by dropping the last path segment, which is the
 * volume root at the top of a volume and the volume list above that.  The folder is
 * read again, so its rows are the volume's rather than the bar's, and the cursor
 * goes back to the row it came from - by name first, because that row may have
 * moved, and by the remembered index if the name is gone. */
static void xb_disc_up(void)
{
    char *slash;
    const char *want;
    int i, sel;

    if (s_disc_marks_n <= 0)
        return;                     /* the volume list is the top of the band */

    want = s_disc_marks[s_disc_marks_n - 1].name;
    sel  = s_disc_marks[s_disc_marks_n - 1].cursor;
    s_disc_marks_n--;

    slash = strrchr(s_disc.path, '/');
    if (slash)
        *slash = '\0';
    if (s_disc.path[0] == '\0')
        snprintf(s_disc.path, sizeof(s_disc.path), "%s", "/");

    xb_disc_reload();

    for (i = 0; i < s_disc.count; i++)
        if (strcmp(s_disc.names[i], want) == 0) {
            sel = i;
            break;
        }
    if (sel >= s_disc.count)
        sel = s_disc.count > 0 ? s_disc.count - 1 : 0;
    if (sel < 0)
        sel = 0;

    s_disc.sel = sel;
    s_depth    = 1 + s_disc_marks_n;
    if (s_disc_marks_n == 0)
        s_cats[s_cat].selection = sel;
}

/* ENTER on a body: the bar reads it through the same VFS and shows its head in the
 * message box, which is as far as a menu with no text window can take a file. */
static void xb_disc_view(int row)
{
    char file[VFS_MAX_PATH];
    char head[XB_MSG_LINE_LEN];
    size_t got = 0;
    int i, n = 0;

    if (row < 0 || row >= s_disc.count)
        return;

    vfs_child_path(file, sizeof(file), s_disc.path,
                   s_disc.names[row], s_disc.fid[row]);
    xb_message_begin();
    snprintf(head, sizeof(head), "%s, %u bytes",
             s_disc.shown[row], (unsigned)s_disc.size[row]);
    xb_message_add(head);

    if (vfs_read_file(file, s_disc_text, sizeof(s_disc_text) - 1, &got) != 0) {
        xb_message_add("Nothing here can be read as text.");
        xb_message_show();
        return;
    }
    s_disc_text[got] = '\0';
    for (i = 0; i < (int)got; i++) {
        unsigned char u = (unsigned char)s_disc_text[i];

        if (u == '\n' || u == '\r') {
            head[n] = '\0';
            xb_message_add(head);
            n = 0;
            continue;
        }
        if (u == '\t')
            u = ' ';
        if (u < 32 || u > 126)
            u = '_';
        head[n++] = (char)u;
        if (n == XB_MSG_LINE_LEN - 1) {
            head[n] = '\0';
            xb_message_add(head);
            n = 0;
        }
    }
    if (n) {
        head[n] = '\0';
        xb_message_add(head);
    }
    if (s_msg_lines <= 1)
        xb_message_add("(no text to show)");
    xb_message_show();
}

/* The menu clock, in milliseconds, as of the last tick.  xb_activate() runs
 * from the event handler and needs it to set the launch hold's deadline. */
static unsigned s_now_ms = 0;

/* Kagee's launcher: a local weak fallback, as src/apps/kagee.c is not in every
 * target's source list.  The real definition in kagee.c wins when linked. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak)) WND* open_kagee_window(void) { return (void*)0; }
/* Same for B-MSX: src/apps/msx_app.c is a hosted-core app and not in the
 * bare-metal lists, where these keep the launcher linking. */
__attribute__((weak)) WND* open_msx_window(void) { return (void*)0; }
__attribute__((weak)) WND* open_msx_window_with_rom(const char *path)
{
    (void)path;
    return (void*)0;
}
#endif

/* The row is what launches: a Games row carries the cartridge to load in its
 * value, so the item travels with the id. */
static void xb_launch(const xb_item_t *it)
{
    switch (it->launch) {
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
    case L_MSX:       xb_game_launch(it); break;
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
        xb_launch(it);
        break;
    case XB_TOGGLE:
        xb_set_value(it, it->value ? 0 : 1);
        break;
    case XB_CMD:
        xb_message_begin();
        shell_execute_cmd(it->cmd ? it->cmd : "ls /", xb_shell_output, NULL, s_wnd);
        if (!s_msg_lines)
            xb_message_add("(no output)");
        xb_message_show();
        break;
    case XB_DIR:
        if (s_cats[s_cat].files)
            xb_disc_enter((int)(it - s_disc.items));
        break;
    case XB_FILE:
        if (s_cats[s_cat].files)
            xb_disc_view((int)(it - s_disc.items));
        break;
    case XB_SUB:
        if (it->sub_items && it->sub_count) {
            if (it->sub_items == s_items_sysdata)
                xb_sysdata_fill();
            s_depth    = 2;
            s_sub_sel  = 0;
            xb_sync_list();
            xb_anim_list_open(1);
        }
        break;
    default:
        break;
    }
}

static void xb_adjust(xb_item_t *it, int dir)
{
    int value;

    /* A bound row steps from the system's value, not from whatever the menu last
     * showed, so an edit made elsewhere is never overwritten by this one. */
    if (it->bind != XMB_BIND_NONE)
        it->value = xb_bind_read(it->bind);

    switch (it->kind) {
    case XB_TOGGLE:
        value = dir > 0 ? 1 : 0;
        break;
    case XB_RANGE:
        /* One level at a time: the console's sliders move by a single step, and a
         * value the system keeps is what the row reads back. */
        value = it->value + dir;
        if (value < 0) value = 0;
        if (it->max && value > it->max) value = it->max;
        break;
    case XB_ENUM: {
        int count = 1, i;
        const char *p;
        for (p = it->opts; p && *p; p++)
            if (*p == '|') count++;
        value = it->value + dir;
        if (value < 0) value = count - 1;
        if (value >= count) value = 0;
        (void)i;
        break;
    }
    default:
        return;
    }
    xb_set_value(it, value);
    /* xmb.c: adjusting a value fades the list back in */
    s_alpha_list = 0.0f;
    xb_anim_list_alpha(1);
}

static void xb_move_row(int dir)
{
    int count = 0;
    int sel = xb_cur_selection();
    xb_item_t *items = xb_cur_items(&count);

    if (!items || count <= 0)
        return;
    sel += dir;
    if (sel < 0) {
        /* Walking off the top of an opened menu closes it again */
        if (s_depth > 1)
            xb_go_back();
        return;
    }
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
    /* A band whose list is a folder is re-read on the way in, and always starts
     * again at the volume list: the console does not park you inside a drawer when
     * you leave for another category and come back. */
    if (s_cats[s_cat].files)
        xb_disc_restart();
    xb_sync_list();
    xb_anim_band_move(dir);
    xb_anim_list_switch(dir);
}

/* LEFT/RIGHT.  The console's own rule, and the one the rest of this port
 * assumes: at the band level these keys always switch bands, and a row's value
 * is adjusted only once a menu is open beneath it.  Adjusting at the band level
 * would trap the cursor wherever a band is made of sliders. */
static void xb_side(int dir)
{
    int count = 0;
    xb_item_t *items = s_depth > 1 ? xb_cur_items(&count) : NULL;
    xb_item_t *cur   = items ? &items[xb_cur_selection()] : NULL;

    if (cur && (cur->kind == XB_TOGGLE || cur->kind == XB_RANGE ||
                cur->kind == XB_ENUM)) {
        xb_adjust(cur, dir);
        return;
    }
    xb_move_cat(dir);
}

static void xb_go_back(void)
{
    if (s_msg_open) {
        xb_message_clear();
        return;
    }
    if (s_depth > 1) {
        if (s_cats[s_cat].files) {
            xb_disc_up();
            xb_sync_list();
            xb_anim_list_open(-1);
            return;
        }
        s_depth   = 1;
        s_sub_sel = 0;
        xb_sync_list();
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
        /* MENU_ACTION_SCROLL_UP/DOWN: the host's wheel and trackpad arrive as
         * page keys, because src/window/event.c turns notches into them. */
        case BTRON_KEY_PAGE_UP:
            xb_move_row(-1);
            break;
        case BTRON_KEY_PAGE_DOWN:
            xb_move_row(1);
            break;
        case BTRON_KEY_LEFT:
            xb_side(-1);
            break;
        case BTRON_KEY_RIGHT:
            xb_side(1);
            break;
        case BTRON_KEY_RETURN:
        case BTRON_KEY_KP_ENTER:
        case BTRON_KEY_SPACE:
            if (s_msg_open) {
               xb_message_clear();
               break;
            }
            items = xb_cur_items(&count);
            cur   = NULL;
            /* An empty folder is a listing the volume can hand out - a freshly
             * formatted disc has no rows at all - so the row ENTER acts on is
             * clamped into the list rather than read at an index it may not have. */
            if (items && count > 0) {
                int sel = xb_cur_selection();
                if (sel < 0)
                    sel = 0;
                if (sel >= count)
                    sel = count - 1;
                cur = &items[sel];
            }
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
    /* The two atlases belong to this window's GL context, which
     * egl_destroy_surface() above has just freed along with every texture
     * image it held; only the names need dropping. */
    s_tex_icons = 0;
    s_tex_font  = 0;
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
        node->zoom = (c == s_cat) ? XB_CATEGORIES_ACTIVE_ZOOM
                                  : XB_CATEGORIES_PASSIVE_ZOOM;
    }

    /* Item tables carry their own counts in the array sizes; set them here so
     * the content above can be edited without keeping a count in sync.  The Discs
     * band is the exception: its rows are a listing, not a table.  The Games band
     * is a listing too, read once here rather than on every level change. */
    xb_games_scan();
    s_cats[BAND_APPS].count     = XB_NEL(s_items_apps);
    s_cats[BAND_SETTINGS].count = XB_NEL(s_items_settings);
    s_cats[BAND_GAMES].count    = s_game_count;
    s_cats[BAND_COMMANDS].count = XB_NEL(s_items_commands);

    s_items_settings[SET_APPEARANCE].sub_count = XB_NEL(s_items_appearance);
    s_items_settings[SET_THEME].sub_count      = XB_NEL(s_items_theme);
    s_items_settings[SET_SCREEN].sub_count     = XB_NEL(s_items_screen);
    s_items_settings[SET_KEYBOARD].sub_count   = XB_NEL(s_items_keyboard);
    s_items_settings[SET_MOUSE].sub_count      = XB_NEL(s_items_mouse);
    s_items_settings[SET_TERMINAL].sub_count   = XB_NEL(s_items_terminal);
    s_items_settings[SET_LANGUAGE].sub_count   = XB_NEL(s_items_language);
    s_items_settings[SET_SYSDATA].sub_count    = XB_NEL(s_items_sysdata);

    /* Every value the bar can show starts as the system's, not as the number the
     * table happened to be written with.  For the Discs band that means the volume
     * list, read from the machine before the first frame is drawn. */
    xb_disc_restart();
    xb_sync_list();

    for (c = 0; c < s_cat_count; c++) {
        int rows  = s_cats[c].files ? s_disc.count : s_cats[c].count;
        xb_item_t *items = s_cats[c].files ? s_disc.items : s_cats[c].items;
        for (i = 0; i < rows; i++) {
            xb_node_t *n = &items[i].node;
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
        xb_tween_push(&s_cats[i].node.alpha,
                      (i == s_cat) ? XB_CATEGORIES_ACTIVE_ALPHA
                                   : XB_CATEGORIES_PASSIVE_ALPHA,
                      XB_DELAY, XB_EASING_ALPHA, (uintptr_t)&s_cats[i]);
    xb_anim_list_switch(1);
}

/* ── Calibration hooks ────────────────────────────────────────────────
 * verify/tests/test_xmb_render.c cannot see the SDL window, so it measures
 * pixels instead.  These read back what the bar is actually drawing and where
 * its cursor actually is, so the test never has to mirror the menu's state. */

int xmb_setting(int which)
{
    switch (which) {
    case XMB_SETTING_THEME:      return s_items_theme[THEME_COLOUR].value;
    case XMB_SETTING_WAVE:       return s_items_theme[THEME_WAVE].value;
    case XMB_SETTING_PARTICLES:  return s_items_theme[THEME_PARTICLES].value;
    case XMB_SETTING_BRIGHTNESS: return s_items_screen[SCR_BRIGHT].value;
    case XMB_SETTING_FADE:       return s_items_screen[SCR_FADE].value;
    case XMB_SETTING_SHADOWS:    return s_items_screen[SCR_SHADOWS].value;
    default:                     return -1;
    }
}

/* Where the cursor actually is: the band index, with 0 the leftmost, the depth
 * of the stacked lists - 1 at the band level, and one deeper for every menu,
 * volume or drawer opened from it - and how many rows the list under the cursor
 * holds.  A walk to a row has to know that last count, because UP on the first row
 * of an open menu closes it. */
int xmb_band(void)  { return s_cat; }
int xmb_depth(void) { return s_depth; }
int xmb_bands(void) { return s_cat_count; }

/* The row the cursor rests on, and its label: a walk through the folders is
 * proved by which row the bar ended up on, not by how many it drew. */
int xmb_row(void) { return xb_cur_selection(); }

const char *xmb_label(int row)
{
    int count = 0;
    xb_item_t *items = xb_cur_items(&count);

    if (!items || row < 0 || row >= count)
        return "";
    return items[row].label ? items[row].label : "";
}

/* Which glyph of the icon atlas a row will be drawn with, as its cell in the
 * atlas's grid.  A row's icon is chosen from the storage it came from - a mounted
 * volume, a drawer, a body - so this is how a check can name the glyph it is
 * looking at instead of guessing it from the picture.  -1 for no such row. */
int xmb_row_icon(int row)
{
    int count = 0;
    xb_item_t *items = xb_cur_items(&count);

    if (!items || row < 0 || row >= count)
        return -1;
    return items[row].icon;
}

/* The folder the Discs band is showing, without the Real Body anchors the VFS
 * carries in its paths, and how deep below the volume list it sits.  A test that
 * walks the bar needs the second one to know which level it is looking at, because
 * the list itself only ever holds the folder that is on screen. */
const char *xmb_path(void)
{
    static char shown[128];
    char raw[VFS_MAX_PATH / 4];

    vfs_display_path(s_disc.path, raw, sizeof(raw));
    xb_disc_ascii(shown, sizeof(shown), raw);
    return shown;
}

int xmb_levels(void) { return s_disc_marks_n; }

/* How far the category bar has been pushed sideways, in pixels, once its tween has
 * settled: one icon width and a bit per level of the stack, with no ceiling - which
 * is what makes the categories slide out of the frame as a folder tree goes down,
 * the way the console's own browsers do. */
int xmb_bar_x(void) { return (int)s_x; }

/* What a bound row shows right now, which for those rows is simply what the
 * system holds: the same read the row itself goes through. */
int xmb_bound(int bind)
{
    if (bind <= XMB_BIND_NONE || bind >= XMB_BIND_COUNT)
        return -1;
    return xb_bind_read(bind);
}

int xmb_rows(void)
{
    int count = 0;
    xb_cur_items(&count);
    return count;
}

/* The ribbon's per-vertex brightness for the sheet as it currently stands.
 * pipeline_xmb_ribbon's c = (1 - cos(cc*cc))/13 reads cc as the slope of the
 * surface normal, which for this sheet is always in [1, 2]: cc = 1 over
 * sqrt(m*m + n*n + 1), and that root is at least 1.  Over that range c peaks
 * where cc*cc reaches pi, at 2/13, and bottoms out at (1 - cos 1)/13, so a
 * field outside that pair is a modelling error in xb_ribbon_step() rather than
 * a compositing accident. */
void xmb_ribbon_calibrate(float *min_b, float *max_b, float *mean_b, int *samples)
{
    int r, c, n = 0;
    float lo = 0.0f, hi = 0.0f, sum = 0.0f;

    for (r = 0; r < XB_RIBBON_ROWS; r++)
        for (c = 0; c < XB_RIBBON_COLS; c++) {
            float b = s_rib_bri[r][c];
            if (n == 0 || b < lo) lo = b;
            if (n == 0 || b > hi) hi = b;
            sum += b;
            n++;
        }

    if (min_b)  *min_b  = lo;
    if (max_b)  *max_b  = hi;
    if (mean_b) *mean_b = n ? sum / (float)n : 0.0f;
    if (samples) *samples = n;
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
