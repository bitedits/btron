/*
 * B-System (BTRON 3.20) Desktop Compositor: desktop.c
 * Pure Specification-based implementation of Sakamura BTRON / BTRON3 Architecture.
 */

#include <btron/desktop.h>
#include <btron/troncode.h>
#include <btron/vobj.h>
#include <btron/wnd.h>
#include <btron/tracker.h>
#include <btron/settings.h>
#include <btron/settings_icon.h>
#include <btron/apps.h>
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include <stdio.h>
#include <time.h>
#include <string.h>
#else
#include <stddef.h>
#include <stdint.h>
#include <libstr.h>
#define strlen tkl_strlen
#endif

typedef struct {
    const char *id_str;
    const char *label;
    const char *glyph;
    COLOR glyph_col;
    WND* (*action)(void);
} DESKTOP_ICON_DEF;

static const DESKTOP_ICON_DEF s_desktop_icons[] = {
    { "cabinet",   "実身・仮身",   "実身", COLOR_NAVY,  open_vobj_manager_window },
    { "t_editor",  "基本エディタ", "文書", COLOR_BLACK, open_t_editor_window },
    { "terminal",  "端末シェル",   "端末", COLOR_BLACK, open_gterm_window },
    { "sound",     "音響機器",     "音響", COLOR_RED,   open_audio_player_window },
    { "chat",      "会話通信",     "対話", COLOR_NAVY,  launch_beos_chat },
};

static BTRON_DESKTOP g_desktop;

ER init_desktop(H width, H height) {
    g_desktop.width = width;
    g_desktop.height = height;
    g_desktop.screen = opn_dev(width, height);
    g_desktop.running = TRUE;

    init_wnd_mgr(g_desktop.screen);
    init_vobj_sys("./btron_store");
    tracker_init();

    return E_OK;
}

ER init_desktop_vram(H width, H height, COLOR *vram_ptr) {
    g_desktop.width = width;
    g_desktop.height = height;
    g_desktop.screen = opn_dev_vram(width, height, vram_ptr);
    g_desktop.running = TRUE;

    init_wnd_mgr(g_desktop.screen);
    init_vobj_sys("./btron_store");
    tracker_init();

    return E_OK;
}


static void get_desktop_icon_layout(int idx, int *out_dim, RECT *out_plate, int *out_icon_x, int *out_icon_y, int *out_lbl_x, int *out_lbl_y, RECT *out_hit) {
    BTRON_ICON_SIZE sz = appearance_get_icon_size();
    if (sz == BTRON_ICON_SIZE_64) {
        int icon_dim = 64;
        int plate_w = 78;
        int plate_h = 74;
        int start_x = 16;
        int start_y = 44;
        int step_y = 104;
        int top = start_y + idx * step_y;
        if (out_dim) *out_dim = icon_dim;
        if (out_plate) {
            out_plate->left = start_x;
            out_plate->top = top;
            out_plate->right = start_x + plate_w;
            out_plate->bottom = top + plate_h;
        }
        if (out_icon_x) *out_icon_x = start_x + (plate_w - icon_dim) / 2;
        if (out_icon_y) *out_icon_y = top + (plate_h - icon_dim) / 2;
        if (out_lbl_x) *out_lbl_x = (idx == 1) ? 14 : 16;
        if (out_lbl_y) *out_lbl_y = top + plate_h + 6;
        if (out_hit) {
            out_hit->left = 10;
            out_hit->top = top - 2;
            out_hit->right = start_x + plate_w + 16;
            out_hit->bottom = top + plate_h + 24;
        }
    } else {
        int icon_dim = 32;
        int plate_w = 50;
        int plate_h = 45;
        int start_x = 20;
        int start_y = 50;
        int step_y = 80;
        int top = start_y + idx * step_y;
        if (out_dim) *out_dim = icon_dim;
        if (out_plate) {
            out_plate->left = start_x;
            out_plate->top = top;
            out_plate->right = start_x + plate_w;
            out_plate->bottom = top + plate_h;
        }
        if (out_icon_x) *out_icon_x = start_x + (plate_w - icon_dim) / 2;
        if (out_icon_y) *out_icon_y = top + (plate_h - icon_dim) / 2;
        if (out_lbl_x) *out_lbl_x = (idx == 1) ? 10 : 12;
        if (out_lbl_y) *out_lbl_y = top + plate_h + 7;
        if (out_hit) {
            out_hit->left = 8;
            out_hit->top = top - 2;
            out_hit->right = start_x + plate_w + 14;
            out_hit->bottom = top + plate_h + 22;
        }
    }
}

static COLOR s_cached_bg[1024 * 768] __attribute__((aligned(64)));
static BOOL  s_bg_cached = FALSE;

void desktop_invalidate_background(void) {
    s_bg_cached = FALSE;
}

void render_desktop_background(GDEV *dev) {
    if (!dev) return;

    if (s_bg_cached && dev->width == 1024 && dev->height == 768) {
        /* Instant restore from cached buffer — 0.3 ms instead of 35 ms of LZW decodes */
        for (int i = 0; i < 1024 * 768; i++) {
            dev->pixels[i] = s_cached_bg[i];
        }
        return;
    }

    /* Fill background with classic Sakamura B-TRON Teal palette */
    RECT bg_rect = { 0, 0, dev->width, dev->height };
    fill_rec(dev, &bg_rect, COLOR_TEAL);

    /* Render Retro Grid / Desktop Wallpaper Pattern */
    for (H y = 30; y < dev->height; y += 32) {
        for (H x = 0; x < dev->width; x += 32) {
            drw_pnt(dev, x, y);
        }
    }

    /* Desktop Icons / Pictogram Real Bodys */
    int icon_count = (int)(sizeof(s_desktop_icons) / sizeof(s_desktop_icons[0]));
    for (int i = 0; i < icon_count; i++) {
        int icon_dim, icon_x, icon_y, lbl_x, lbl_y;
        RECT plate;
        get_desktop_icon_layout(i, &icon_dim, &plate, &icon_x, &icon_y, &lbl_x, &lbl_y, NULL);

        /* Icon Plate Background */
        fill_rec(dev, &plate, COLOR_LTGRAY);
        drw_rec(dev, &plate);

        /* Scaled Pictogram Icon (32x32 or 64x64 according to Appearance settings) */
        BOOL icon_drawn = draw_setting_gif_icon_scaled(dev, s_desktop_icons[i].id_str, icon_x, icon_y, icon_dim, icon_dim);

#if !defined(__STDC_HOSTED__) || __STDC_HOSTED__ != 1
        if (!icon_drawn) {
            /* Fallback kanji glyph for freestanding baremetal mode if icon not available */
            drw_tc_string(dev, plate.left + (plate.right - plate.left - 24) / 2,
                               plate.top + (plate.bottom - plate.top - 16) / 2,
                               s_desktop_icons[i].glyph, s_desktop_icons[i].glyph_col, 0x00000000);
        }
#else
        (void)icon_drawn;
#endif

        /* High-contrast label with shadow */
        drw_tc_string(dev, lbl_x + 1, lbl_y + 1, s_desktop_icons[i].label, COLOR_BLACK, 0x00000000);
        drw_tc_string(dev, lbl_x, lbl_y, s_desktop_icons[i].label, COLOR_WHITE, 0x00000000);
    }

    if (dev->width == 1024 && dev->height == 768) {
        for (int i = 0; i < 1024 * 768; i++) {
            s_cached_bg[i] = dev->pixels[i];
        }
        s_bg_cached = TRUE;
    }
}

BOOL desktop_handle_click(H x, H y) {
    int icon_count = (int)(sizeof(s_desktop_icons) / sizeof(s_desktop_icons[0]));
    for (int i = 0; i < icon_count; i++) {
        RECT hit;
        get_desktop_icon_layout(i, NULL, NULL, NULL, NULL, NULL, NULL, &hit);
        if (x >= hit.left && x <= hit.right && y >= hit.top && y <= hit.bottom) {
            if (s_desktop_icons[i].action) {
                s_desktop_icons[i].action();
            }
            return TRUE;
        }
    }
    return FALSE;
}

#include <btron/global_menu.h>

void render_system_panel(GDEV *dev) {
    if (!dev) return;
    global_menu_render_bar(dev);
}


#include <btron/tip.h>
#include <btron/apps.h>
#ifndef ARGB
#define ARGB(a,r,g,b) (((uint32_t)(a)<<24)|((uint32_t)(r)<<16)|((uint32_t)(g)<<8)|(uint32_t)(b))
#endif

static H g_mouse_x = 460;
static H g_mouse_y = 280;

void set_baremetal_mouse_pos(H x, H y) {
    if (x < 0) x = 0;
    if (x >= 1024) x = 1023;
    if (y < 0) y = 0;
    if (y >= 768) y = 767;
    g_mouse_x = x;
    g_mouse_y = y;
}

void get_baremetal_mouse_pos(H *x, H *y) {
    if (x) *x = g_mouse_x;
    if (y) *y = g_mouse_y;
}

void draw_baremetal_mouse_cursor(GDEV *screen, H mx, H my, H w, H h) {
    if (!screen) return;
    static const uint16_t cur_mask[16] = {
        0x8000, 0xC000, 0xE000, 0xF000,
        0xF800, 0xFC00, 0xFE00, 0xFF00,
        0xFF80, 0xFE00, 0xDF00, 0x8F80,
        0x0780, 0x03C0, 0x0180, 0x0000
    };
    static const uint16_t cur_outline[16] = {
        0xC000, 0xE000, 0xF000, 0xF800,
        0xFC00, 0xFE00, 0xFF00, 0xFF80,
        0xFFC0, 0xFFE0, 0xFF80, 0xDFC0,
        0xCFE0, 0x07E0, 0x03C0, 0x0180
    };
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            H px = mx + x;
            H py = my + y;
            if (px < 0 || px >= w || py < 0 || py >= h) continue;
            uint16_t bit = (0x8000 >> x);
            if (cur_mask[y] & bit) {
                screen->pixels[py * w + px] = COLOR_WHITE;
            } else if (cur_outline[y] & bit) {
                screen->pixels[py * w + px] = COLOR_BLACK;
            }
        }
    }
}

void redraw_baremetal_desktop(GDEV *screen, H w, H h) {
    if (!screen) return;
    render_desktop_background(screen);
    redraw_all_windows();

    /* Floating candidate window for active window */
    WND *top = get_top_wnd();
    if (top && top->focused && (tip_get_state() == TIP_STATE_CONVERTING || tip_get_state() == TIP_STATE_CANDIDATE_SELECT)) {
        tip_render_candidate_window(screen, tip_get_caret_x(), tip_get_caret_y());
    }

    render_system_panel(screen);

    /* Gold accent bar below top panel */
    RECT gold_bar = { 0, 26, (H)w, 28 };
    fill_rec(screen, &gold_bar, COLOR_GOLD);

    /* Color Test Bar (bottom 40px) */
    COLOR bars[8] = {
        COLOR_WHITE,
        COLOR_YELLOW,
        COLOR_CYAN,
        COLOR_GREEN,
        ARGB(0xFF, 0xFF, 0x00, 0xFF), /* Magenta */
        COLOR_RED,
        ARGB(0xFF, 0x00, 0x00, 0xFF), /* Blue */
        COLOR_BLACK
    };
    H bar_h = 40;
    H bar_y = (H)h - bar_h;
    for (int bi = 0; bi < 8; bi++) {
        H bx0 = (bi * (H)w) / 8;
        H bx1 = ((bi + 1) * (H)w) / 8;
        RECT br = { bx0, bar_y, bx1, (H)h };
        fill_rec(screen, &br, bars[bi]);
    }

    /* Dynamic Classic B-TRON Mouse Cursor Rendering */
    draw_baremetal_mouse_cursor(screen, g_mouse_x, g_mouse_y, w, h);

    /* Data Cache Barrier */
#if defined(__aarch64__)
    __asm__ volatile("dsb sy" : : : "memory");
#elif defined(__arm__)
    __asm__ volatile("dsb" : : : "memory");
#elif defined(__m68k__)
    __asm__ volatile("nop" : : : "memory");
#elif defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("mfence" : : : "memory");
#else
    __asm__ volatile("" : : : "memory");
#endif
}

GDEV* init_baremetal_desktop(uint32_t *fb, uint32_t w, uint32_t h) {
    if (!fb) return NULL;
    GDEV *screen = opn_dev_vram((H)w, (H)h, (COLOR*)fb);
    if (!screen) return NULL;

    init_wnd_mgr(screen);
    init_vobj_sys("./btron_store");
    tip_init();
    init_evt_sys();

    open_vobj_manager_window();
    open_t_editor_window();
    WND *w_cli = open_gterm_window();
    if (w_cli) {
        top_wnd(w_cli);
        w_cli->focused = TRUE;
    }

    /* Initial paint to backbuffer (caller blits to GPU VRAM after this returns) */
    redraw_baremetal_desktop(screen, w, h);
    return screen;
}

void draw_btron_pattern(uint32_t *fb, uint32_t w, uint32_t h) {
    init_baremetal_desktop(fb, w, h);
}


/* End baremetal desktop block */

BTRON_DESKTOP* get_btron_desktop(void) {
    return &g_desktop;
}
