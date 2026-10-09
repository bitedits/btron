/*
 * B-System (BTRON 3.20) — Global Top-Bar Menu
 *
 * Logical layout (left → right, by function):
 *
 *  ［BTRON］   Deskbar (Tracker app launcher) + ── + Sleep/Restart/Shutdown/Quit
 *  システム(S) System-wide configuration: About, Settings, Display, Sound, Network
 *  アプリ(A)   All launchable applications: General | Cho-Kanji suite
 *  実身・仮身  Real/Virtual-object operations (BTRON object model)
 *  ウィンドウ  Window management + live window list
 *
 * NASA JPL Rule 3: Bounded static state, zero post-boot heap allocation.
 */

#include <btron/global_menu.h>
#include <btron/tracker.h>
#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/troncode.h>
#include <btron/tip.h>
#include <btron/about.h>
#include <btron/app_menu.h>
#include <btron/chokanji.h>

#include <btron/libc_shim.h>
#if BTRON_HOSTED
#include <time.h>
#endif

#if defined(BTRON_TARGET) && BTRON_TARGET == 6
extern void async_rt_format_status(char *buf, size_t len);
extern void async_rt_format_compact_status(char *buf, size_t len);
#endif

/* ── Weak external app launchers ──────────────────────────────── */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak, weak_import)) WND* open_vobj_manager_window(void);
__attribute__((weak, weak_import)) WND* open_control_panel_window(void);
__attribute__((weak, weak_import)) WND* open_t_editor_window(void);
__attribute__((weak, weak_import)) WND* open_gterm_window(void);
__attribute__((weak, weak_import)) WND* open_audio_player_window(void);
__attribute__((weak, weak_import)) WND* open_orchestra_window(void);
__attribute__((weak, weak_import)) WND* open_about_window(void);
__attribute__((weak, weak_import)) WND* open_display_settings_window(void);
__attribute__((weak, weak_import)) WND* open_clarity_window(void);
__attribute__((weak, weak_import)) WND* open_paint_window(void);
__attribute__((weak, weak_import)) WND* open_paint_about_window(void);
__attribute__((weak, weak_import)) WND* open_chat_main_window(void *client);
__attribute__((weak, weak_import)) WND* open_drivesetup_window(void);
__attribute__((weak, weak_import)) WND* open_quake_window(int x, int y, int width, int height);
__attribute__((weak, weak_import)) WND* open_lilcu64_demo_window(void);
__attribute__((weak, weak_import)) WND* open_xmb_window(void);
/* Cho-Kanji suite */
__attribute__((weak, weak_import)) WND* open_chokanji_cabinet_window(void);
__attribute__((weak, weak_import)) WND* open_chokanji_doc_window(void);
__attribute__((weak, weak_import)) WND* open_chokanji_microscript_window(void);
__attribute__((weak, weak_import)) WND* open_chokanji_clock_window(void);
__attribute__((weak, weak_import)) WND* open_chokanji_kconv_window(void);
__attribute__((weak, weak_import)) WND* open_chokanji_xfconv_window(void);
__attribute__((weak, weak_import)) WND* open_chokanji_unpack_window(void);
#else
extern WND* open_quake_window(int x, int y, int width, int height);
extern WND* open_lilcu64_demo_window(void);
extern WND* open_xmb_window(void);
extern WND* open_vobj_manager_window(void);
extern WND* open_control_panel_window(void);
extern WND* open_t_editor_window(void);
extern WND* open_gterm_window(void);
extern WND* open_audio_player_window(void);
extern WND* open_orchestra_window(void);
extern WND* open_about_window(void);
extern WND* open_display_settings_window(void);
extern WND* open_clarity_window(void);
extern WND* open_paint_window(void);
extern WND* open_paint_about_window(void);
extern WND* open_chat_main_window(void *client);
extern WND* open_drivesetup_window(void);
extern WND* open_chokanji_cabinet_window(void);
extern WND* open_chokanji_doc_window(void);
extern WND* open_chokanji_microscript_window(void);
extern WND* open_chokanji_clock_window(void);
extern WND* open_chokanji_kconv_window(void);
extern WND* open_chokanji_xfconv_window(void);
extern WND* open_chokanji_unpack_window(void);
#endif

/* Paint launchers: local weak fallbacks so headless/test builds that omit
 * src/apps/paint.c still link; the real definitions in paint.c win when linked. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak)) WND* open_paint_window(void) { return (void*)0; }
__attribute__((weak)) WND* open_paint_window_with_file(const char *filepath) {
    (void)filepath;
    return (void*)0;
}
__attribute__((weak)) WND* open_paint_about_window(void) { return (void*)0; }
#endif

#define GMENU_DROPDOWN_WIDTH   400
#define GMENU_ROW_HEIGHT        22

/* ── Singleton interaction state ──────────────────────────────── */
typedef struct {
    int  active_menu;   /* -1 = closed, 0..N = open header index */
    int  hover_header;  /* -1 = none */
    int  hover_item;    /* -1 = none */
    BOOL tip_hover;
} GlobalMenuState;

static GlobalMenuState g_gmenu;
static H s_gmenu_scr_w = 1280;

void global_menu_set_screen_width(H w) {
    if (w > 0) s_gmenu_scr_w = w;
}

/* ═══════════════════════════════════════════════════════════════
 * Static menu header + item tables
 *
 * Bar pixel layout (1280-wide screen):
 *   [BTRON] 0─88 | システム 96─196 | アプリ 202─316 | 実身・仮身 322─452 | ウィンドウ 458─578
 *
 * Header indices (GMENU_HDR_* constants defined in global_menu.h):
 *   0 ［BTRON］   power strip after Tracker
 *   1 システム(S) system configuration
 *   2 アプリ(A)   all launchable applications
 *   3 実身・仮身  BTRON object operations
 *   4 ウィンドウ  window management
 * ═══════════════════════════════════════════════════════════════*/
static GMenuHeader g_headers[GMENU_HEADER_COUNT] = {

    /* ── 0: ［BTRON］ ─── Deskbar entry + power commands ────── */
    {
        .title = "［BTRON］",
        .rect = { 4, 2, 90, 23 },
        .item_count = 0    /* Tracker draws its own dropdown; power appended */
    },

    /* ── 1: システム(S) ─── System-wide configuration ─────── */
    {
        .title = "システム(S)",
        .rect = { 96, 2, 210, 23 },
        .item_count = 6,
        .items = {
            { "システム情報 (About B-System...)",  "Alt+?", GMENU_CMD_SYS_ABOUT,    FALSE, FALSE, TRUE },
            { "環境設定 (Control Panel...)",        "Alt+P", GMENU_CMD_SYS_SETTINGS, FALSE, FALSE, TRUE },
            { "画面表示 (Display Settings...)",     "",      GMENU_CMD_SYS_DISPLAY,  FALSE, FALSE, TRUE },
            { "音響設定 (Audio Settings...)",       "",      GMENU_CMD_SYS_AUDIO,    FALSE, FALSE, TRUE },
            { "---", "", GMENU_CMD_NONE, TRUE, FALSE, FALSE },
            { "外観: Cho-Kanji Classic / Modern",   "",      GMENU_CMD_APP_STYLE_CLASSIC, FALSE, FALSE, TRUE }
        }
    },

    /* ── 2: アプリ(A) ─── All launchable applications ──────── */
    {
        .title = "アプリ(A)",
        .rect = { 216, 2, 320, 23 },
        .item_count = 22,
        .items = {
            /* General B-System apps */
            { "Editor (文書編集...)",                "Ctrl+E", GMENU_CMD_APP_TEDITOR,    FALSE, FALSE, TRUE },
            { "Terminal (gterm 端末...)",             "Ctrl+T", GMENU_CMD_APP_TERMINAL,   FALSE, FALSE, TRUE },
            { "音響機器 (Cassette Deck...)",          "",       GMENU_CMD_APP_AUDIO,      FALSE, FALSE, TRUE },
            { "管弦楽 (Orchestra DAW...)",            "",       GMENU_CMD_APP_ORCHESTRA,  FALSE, FALSE, TRUE },
            { "Mail & Chat (対話通信...)",            "",       GMENU_CMD_APP_CHAT,       FALSE, FALSE, TRUE },
            { "DriveSetup (ディスク管理...)",         "",       GMENU_CMD_APP_DRIVESETUP, FALSE, FALSE, TRUE },
            { "電子帳票 (Clarity DTP...)",            "",       GMENU_CMD_APP_CLARITY,    FALSE, FALSE, TRUE },
            { "ペイント (Paint Image Viewer...)",     "",       GMENU_CMD_APP_PAINT,      FALSE, FALSE, TRUE },
            { "Quake (3D FPS Game...)",              "Ctrl+Q", GMENU_CMD_APP_QUAKE,      FALSE, FALSE, TRUE },
            { "Lil Cu 64 Demo (3D Demoscene...)",    "Ctrl+L", GMENU_CMD_APP_DEMO,       FALSE, FALSE, TRUE },
            { "XMB (横断メディアメニュー...)",        "",       GMENU_CMD_APP_XMB,        FALSE, FALSE, TRUE },
            { "---", "", GMENU_CMD_NONE, TRUE, FALSE, FALSE },
            /* ── 超漢字 Cho-Kanji Suite ── */
            { "超漢字: キャビネット (Cabinet...)",       "Ctrl+K", GMENU_CMD_APP_CK_CABINET,     FALSE, FALSE, TRUE },
            { "超漢字: 文書編集 (Doc Editor...)",        "Ctrl+D", GMENU_CMD_APP_CK_DOC,         FALSE, FALSE, TRUE },
            { "超漢字: マイクロスクリプト (Script...)",  "Ctrl+M", GMENU_CMD_APP_CK_MICROSCRIPT, FALSE, FALSE, TRUE },
            { "超漢字: 時計 (Clock)",                    "",       GMENU_CMD_APP_CK_CLOCK,       FALSE, FALSE, TRUE },
            { "---", "", GMENU_CMD_NONE, TRUE, FALSE, FALSE },
            /* ── 超漢字 Tools ── */
            { "超漢字: 文字コード変換 (kconv...)",       "",       GMENU_CMD_APP_CK_KCONV,   FALSE, FALSE, TRUE },
            { "超漢字: XF形式変換 (xfconv...)",         "",       GMENU_CMD_APP_CK_XFCONV,  FALSE, FALSE, TRUE },
            { "超漢字: BPK展開 (unpack...)",            "",       GMENU_CMD_APP_CK_UNPACK,  FALSE, FALSE, TRUE },
            { "---", "", GMENU_CMD_NONE, TRUE, FALSE, FALSE },
            /* Appearance */
            { "外観切替: Classic ↔ Modern Card",        "",       GMENU_CMD_APP_STYLE_CLASSIC, FALSE, FALSE, TRUE }
        }
    },

    /* ── 3: 実身・仮身(O) ─── BTRON object operations ───────── */
    {
        .title = "実身・仮身(O)",
        .rect = { 326, 2, 462, 23 },
        .item_count = 4,
        .items = {
            { "実身キャビネット (Open Cabinet)",    "Alt+O",  GMENU_CMD_OBJ_CABINET, FALSE, FALSE, TRUE },
            { "実身・仮身の検索 (Search Fusen)",    "Ctrl+F", GMENU_CMD_OBJ_SEARCH,  FALSE, FALSE, TRUE },
            { "新規実身の作成 (New Real Object)",   "Ctrl+N", GMENU_CMD_OBJ_NEW,     FALSE, FALSE, TRUE },
            { "共有実身保管庫 (Shared Storage)",    "",       GMENU_CMD_OBJ_STORAGE, FALSE, FALSE, TRUE }
        }
    },

    /* ── 4: ウィンドウ(W) ─── Window management + live list ─── */
    {
        .title = "ウィンドウ(W)",
        .rect = { 468, 2, 588, 23 },
        .item_count = 5,   /* Dynamically expanded with open window titles */
        .items = {
            { "重ねて整列 (Cascade Windows)",   "Shift+F5", GMENU_CMD_WND_CASCADE,  FALSE, FALSE, TRUE },
            { "並べて整列 (Tile Windows)",      "Shift+F4", GMENU_CMD_WND_TILE,     FALSE, FALSE, TRUE },
            { "すべて隠す (Hide All)",          "Ctrl+H",   GMENU_CMD_WND_HIDE_ALL, FALSE, FALSE, TRUE },
            { "次のウィンドウ (Cycle Focus)",    "Alt+Tab",  GMENU_CMD_WND_CYCLE,    FALSE, FALSE, TRUE },
            { "---", "", GMENU_CMD_NONE, TRUE, FALSE, FALSE }
        }
    }
};

/* ── Etched vertical separator helper ────────────────────────── */
static void draw_etched_sep_v(GDEV *dev, H x, H y1, H y2) {
    if (!dev) return;
    RECT s1 = { x, y1, x + 1, y2 };
    RECT s2 = { x + 1, y1, x + 2, y2 };
    fill_rec(dev, &s1, COLOR_DKGRAY);
    fill_rec(dev, &s2, COLOR_WHITE);
}

/* ── Init / close ─────────────────────────────────────────────── */
void global_menu_init(void) {
    g_gmenu.active_menu  = -1;
    g_gmenu.hover_header = -1;
    g_gmenu.hover_item   = -1;
    g_gmenu.tip_hover    = FALSE;
}

void global_menu_close(void) {
    g_gmenu.active_menu = -1;
    g_gmenu.hover_item  = -1;
    if (tracker_is_menu_open()) tracker_close_menu();
}

BOOL global_menu_is_open(void) {
    return (g_gmenu.active_menu >= 0) || tracker_is_menu_open();
}

int global_menu_get_active(void) {
    return g_gmenu.active_menu;
}

int global_menu_get_hover_header(void) {
    return g_gmenu.hover_header;
}

int global_menu_get_hover_item(void) {
    return g_gmenu.hover_item;
}

/* ── Refresh ウィンドウ menu with live open-window titles ─────── */
static void refresh_window_menu(void) {
    GMenuHeader *whdr = &g_headers[GMENU_HDR_WINDOWS];
    whdr->item_count = 5;   /* Reset to the 5 static base items */

    WND *w = get_wnd_list();
    int w_idx = 0;
    while (w && whdr->item_count < GMENU_MAX_ITEMS) {
        if (w->visible) {
            GMenuItem *it = &whdr->items[whdr->item_count];
            snprintf(it->label, sizeof(it->label), "%s%.59s",
                     w->focused ? "[*] " : "[ ] ",
                     w->title[0] ? w->title : "ウィンドウ");
            it->shortcut[0]  = '\0';
            it->cmd_id       = GMENU_CMD_WND_SELECT_BASE + w_idx;
            it->is_separator = FALSE;
            it->is_checked   = w->focused;
            it->enabled      = TRUE;
            whdr->item_count++;
            w_idx++;
        }
        w = w->next;
    }
}

/* ── Japanese calendar clock string ──────────────────────────── */
static void get_japanese_calendar_string(char *buf, size_t max_len) {
    if (!buf || max_len < 32) return;
#if BTRON_HOSTED
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    if (!tm_now) { snprintf(buf, max_len, "9月4日(金) 00:00:00"); return; }

    static const char *weekdays_jp[7] = {
        "(日)","(月)","(火)","(水)","(木)","(金)","(土)"
    };
    int wday = tm_now->tm_wday;
    if (wday < 0 || wday > 6) wday = 0;

    snprintf(buf, max_len, "%d月%d日%s %02d:%02d:%02d",
             tm_now->tm_mon + 1, tm_now->tm_mday, weekdays_jp[wday],
             tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec);
#else
    snprintf(buf, max_len, "9月4日(金) 12:00:00");
#endif
}

/* ── Render top bar ───────────────────────────────────────────── */
void global_menu_render_bar(GDEV *dev) {
    if (!dev) return;
    if (dev->width > 0) s_gmenu_scr_w = dev->width;

    /* Background plate */
    RECT bar = { 0, 0, dev->width, 25 };
    fill_rec(dev, &bar, COLOR_LTGRAY);
    drw_lin(dev, 0, 25, dev->width, 25);

    /* Header buttons */
    for (int h = 0; h < GMENU_HEADER_COUNT; h++) {
        const GMenuHeader *hdr = &g_headers[h];
        RECT hr = hdr->rect;

        if (h == GMENU_HDR_BTRON) {
            tracker_render_button(dev);
        } else {
            BOOL is_active = (g_gmenu.active_menu == h);
            BOOL is_hover  = (g_gmenu.hover_header == h);

            if (is_active) {
                fill_rec(dev, &hr, COLOR_NAVY);
                drw_tc_string(dev, hr.left + 8, hr.top + 3, hdr->title, COLOR_WHITE, 0x00000000);
            } else if (is_hover) {
                fill_rec(dev, &hr, COLOR_WHITE);
                drw_rec(dev, &hr);
                drw_tc_string(dev, hr.left + 8, hr.top + 3, hdr->title, COLOR_NAVY, 0x00000000);
            } else {
                drw_tc_string(dev, hr.left + 8, hr.top + 3, hdr->title, COLOR_BLACK, 0x00000000);
            }
        }

        /* Etched separator between headers */
        if (h < GMENU_HEADER_COUNT - 1) {
            H sep_x = (hdr->rect.right + g_headers[h + 1].rect.left) / 2;
            draw_etched_sep_v(dev, sep_x, 3, 23);
        }
    }

    /* Etched separator after last header */
    draw_etched_sep_v(dev, g_headers[GMENU_HEADER_COUNT - 1].rect.right + 4, 3, 23);

    /* Right tray: Japanese calendar clock */
    int tray_w = 214;
    RECT tray_r = { (H)(dev->width - tray_w - 4), 3, (H)(dev->width - 4), 23 };
    fill_rec(dev, &tray_r, COLOR_LTGRAY);
    drw_lin(dev, tray_r.left, tray_r.top,     tray_r.right - 1, tray_r.top);
    drw_lin(dev, tray_r.left, tray_r.top,     tray_r.left, tray_r.bottom - 1);
    drw_lin(dev, tray_r.left + 1, tray_r.bottom - 1, tray_r.right - 1, tray_r.bottom - 1);
    drw_lin(dev, tray_r.right - 1, tray_r.top + 1,   tray_r.right - 1, tray_r.bottom - 1);

    char cal_buf[48];
    get_japanese_calendar_string(cal_buf, sizeof(cal_buf));
    int cal_w = tc_calc_string_width(cal_buf, (int)strlen(cal_buf));
    int cal_x = tray_r.left + (tray_w - cal_w) / 2;
    drw_tc_string(dev, cal_x, 4, cal_buf, COLOR_NAVY, 0x00000000);

    /* TIP mode badge */
    RECT tip_btn = { (H)(tray_r.left - 134), 3, (H)(tray_r.left - 6), 23 };
    COLOR tip_bg = (tip_get_mode() == TIP_MODE_ASCII) ? COLOR_LTGRAY : COLOR_CYAN;
    if (g_gmenu.tip_hover) tip_bg = COLOR_WHITE;
    fill_rec(dev, &tip_btn, tip_bg);
    drw_rec(dev, &tip_btn);

    const char *mode_name = (tip_get_mode() == TIP_MODE_HIRAGANA) ? "あ" :
                            ((tip_get_mode() == TIP_MODE_KATAKANA) ? "ア" :
                             ((tip_get_mode() == TIP_MODE_TIBETAN) ? "བོད" : "A"));
    char tip_str[32];
    snprintf(tip_str, sizeof(tip_str), "[TIP: %s (F10)]", mode_name);
    int tip_w = tc_calc_string_width(tip_str, (int)strlen(tip_str));
    int tip_x = tip_btn.left + (128 - tip_w) / 2;
    drw_tc_string(dev, tip_x, 4, tip_str, COLOR_BLACK, 0x00000000);

#if defined(BTRON_TARGET) && BTRON_TARGET == 6
    if (dev->width >= 1280) {
        char async_buf[48];
        RECT async_r = { (H)(tip_btn.left - 286), 3, (H)(tip_btn.left - 10), 23 };
        async_rt_format_status(async_buf, sizeof(async_buf));
        fill_rec(dev, &async_r, COLOR_LTGRAY);
        drw_rec(dev, &async_r);
        drw_tc_string(dev, async_r.left + 4, 4, async_buf, COLOR_NAVY, 0x00000000);
    } else {
        H left  = g_headers[GMENU_HEADER_COUNT - 1].rect.right + 8;
        H right = tip_btn.left - 6;
        if (right - left >= 72) {
            char async_buf[28];
            RECT async_r = { left, 3, right, 23 };
            async_rt_format_compact_status(async_buf, sizeof(async_buf));
            fill_rec(dev, &async_r, COLOR_LTGRAY);
            drw_rec(dev, &async_r);
            drw_tc_string(dev, async_r.left + 2, 4, async_buf, COLOR_NAVY, 0x00000000);
        }
    }
#endif
}

/* ── Render open dropdown overlay ────────────────────────────── */
void global_menu_render_overlay(GDEV *dev) {
    if (!dev) return;

    /* Tracker / BTRON button */
    if (g_gmenu.active_menu == GMENU_HDR_BTRON || tracker_is_menu_open()) {
        tracker_render_menu(dev);
        return;
    }

    if (g_gmenu.active_menu < 1 || g_gmenu.active_menu >= GMENU_HEADER_COUNT) return;

    const GMenuHeader *hdr = &g_headers[g_gmenu.active_menu];
    H menu_x = hdr->rect.left;
    H menu_y = 25;
    H menu_w = GMENU_DROPDOWN_WIDTH;
    H menu_h = hdr->item_count * GMENU_ROW_HEIGHT + 6;

    RECT mr = { menu_x, menu_y, menu_x + menu_w, menu_y + menu_h };

    APP_MENU_STYLE style = app_menu_get_global_style();
    if (style == APP_MENU_STYLE_CLASSIC_3D) {
        app_menu_draw_3d_bevel_box(dev, &mr);
    } else {
        RECT shadow = { menu_x + 3, menu_y + 3, menu_x + menu_w + 3, menu_y + menu_h + 3 };
        fill_rec(dev, &shadow, COLOR_DKGRAY);
        fill_rec(dev, &mr, COLOR_WHITE);
        drw_rec(dev, &mr);
        drw_lin(dev, mr.left + 1, mr.top + 1, mr.right - 2, mr.top + 1);
        drw_lin(dev, mr.left + 1, mr.top + 1, mr.left + 1,  mr.bottom - 2);
    }

    for (int i = 0; i < hdr->item_count; i++) {
        const GMenuItem *it = &hdr->items[i];
        RECT ir = { menu_x + 3, menu_y + 3 + i * GMENU_ROW_HEIGHT,
                    menu_x + menu_w - 3, menu_y + 3 + (i + 1) * GMENU_ROW_HEIGHT };

        if (it->is_separator) {
            H sep_y = (ir.top + ir.bottom) / 2;
            drw_lin(dev, ir.left + 4, sep_y, ir.right - 4, sep_y);
            continue;
        }

        BOOL is_hov = (g_gmenu.hover_item == i);
        if (is_hov) fill_rec(dev, &ir, COLOR_NAVY);

        COLOR txt_col = is_hov ? COLOR_WHITE : (it->enabled ? COLOR_BLACK : COLOR_GRAY);
        drw_tc_string(dev, ir.left + 10, ir.top + 3, it->label, txt_col, 0x00000000);

        if (it->shortcut[0]) {
            int sc_w = tc_calc_string_width(it->shortcut, (int)strlen(it->shortcut));
            drw_tc_string(dev, ir.right - sc_w - 12, ir.top + 3, it->shortcut, txt_col, 0x00000000);
        }
    }
}

/* ── Overlay bounding rect (for dirty-region invalidation) ────── */
BOOL global_menu_get_open_rect(RECT *out) {
    if (!out) return FALSE;
    if (g_gmenu.active_menu == GMENU_HDR_BTRON || tracker_is_menu_open())
        return tracker_get_menu_rect(out);
    if (g_gmenu.active_menu < 1 || g_gmenu.active_menu >= GMENU_HEADER_COUNT) return FALSE;
    const GMenuHeader *hdr = &g_headers[g_gmenu.active_menu];
    out->left   = hdr->rect.left;
    out->top    = 25;
    out->right  = hdr->rect.left + GMENU_DROPDOWN_WIDTH + 3;
    out->bottom = 25 + hdr->item_count * GMENU_ROW_HEIGHT + 6 + 3;
    return TRUE;
}

/* ── Command execution ────────────────────────────────────────── */
static void global_menu_execute_cmd(int cmd) {
    global_menu_close();

    /* Toggle PMC style helper */
    static BOOL s_style_classic = TRUE;

    switch (cmd) {

        /* ── システム(S) ── */
        case GMENU_CMD_SYS_ABOUT:
            if (open_about_window) open_about_window();
            break;
        case GMENU_CMD_SYS_SETTINGS:
            if (open_control_panel_window) open_control_panel_window();
            break;
        case GMENU_CMD_SYS_DISPLAY:
            if (open_display_settings_window) open_display_settings_window();
            break;
        case GMENU_CMD_SYS_AUDIO:
            if (open_audio_player_window) open_audio_player_window();
            break;

        /* ── ［BTRON］ power strip ── */
        case GMENU_CMD_SYS_SLEEP:
            break;   /* Platform-specific hook */
        case GMENU_CMD_SYS_RESTART:
            break;
        case GMENU_CMD_SYS_SHUTDOWN:
            break;
        case GMENU_CMD_SYS_QUIT:
            break;

        /* ── アプリ(A) — general apps ── */
        case GMENU_CMD_APP_TEDITOR:
            if (open_t_editor_window) open_t_editor_window();
            break;
        case GMENU_CMD_APP_TERMINAL:
            if (open_gterm_window) open_gterm_window();
            break;
        case GMENU_CMD_APP_AUDIO:
            if (open_audio_player_window) open_audio_player_window();
            break;
        case GMENU_CMD_APP_ORCHESTRA:
            if (open_orchestra_window) open_orchestra_window();
            break;
        case GMENU_CMD_APP_CHAT:
            if (open_chat_main_window) open_chat_main_window(NULL);
            break;
        case GMENU_CMD_APP_DRIVESETUP:
            if (open_drivesetup_window) open_drivesetup_window();
            break;
        case GMENU_CMD_APP_CLARITY:
            if (open_clarity_window) open_clarity_window();
            break;
        case GMENU_CMD_APP_PAINT:
            if (open_paint_window) open_paint_window();
            break;
        case GMENU_CMD_APP_QUAKE:
            if (open_quake_window) open_quake_window(80, 40, 560, 420);
            break;
        case GMENU_CMD_APP_DEMO:
            if (open_lilcu64_demo_window) open_lilcu64_demo_window();
            break;
        case GMENU_CMD_APP_XMB:
            if (open_xmb_window) open_xmb_window();
            break;

        /* ── アプリ(A) — Cho-Kanji suite ── */
        case GMENU_CMD_APP_CK_CABINET:
            if (open_chokanji_cabinet_window) open_chokanji_cabinet_window();
            break;
        case GMENU_CMD_APP_CK_DOC:
            if (open_chokanji_doc_window) open_chokanji_doc_window();
            break;
        case GMENU_CMD_APP_CK_MICROSCRIPT:
            if (open_chokanji_microscript_window) open_chokanji_microscript_window();
            break;
        case GMENU_CMD_APP_CK_CLOCK:
            if (open_chokanji_clock_window) open_chokanji_clock_window();
            break;
        case GMENU_CMD_APP_CK_KCONV:
            if (open_chokanji_kconv_window) open_chokanji_kconv_window();
            break;
        case GMENU_CMD_APP_CK_XFCONV:
            if (open_chokanji_xfconv_window) open_chokanji_xfconv_window();
            break;
        case GMENU_CMD_APP_CK_UNPACK:
            if (open_chokanji_unpack_window) open_chokanji_unpack_window();
            break;

        /* ── Appearance (both menus share this cmd) ── */
        case GMENU_CMD_APP_STYLE_CLASSIC:
            s_style_classic = !s_style_classic;
            app_menu_set_global_style(s_style_classic
                ? APP_MENU_STYLE_CLASSIC_3D
                : APP_MENU_STYLE_MODERN_CARD);
            break;

        /* ── 実身・仮身(O) ── */
        case GMENU_CMD_OBJ_CABINET:
        case GMENU_CMD_OBJ_SEARCH:
        case GMENU_CMD_OBJ_STORAGE:
            if (open_vobj_manager_window) open_vobj_manager_window();
            break;
        case GMENU_CMD_OBJ_NEW:
            if (open_t_editor_window) open_t_editor_window();
            break;

        /* ── ウィンドウ(W) ── */
        case GMENU_CMD_WND_CASCADE:
            wnd_cascade_all();
            break;
        case GMENU_CMD_WND_TILE:
            wnd_tile_all();
            break;
        case GMENU_CMD_WND_HIDE_ALL:
            wnd_hide_all();
            break;
        case GMENU_CMD_WND_CYCLE:
            wnd_cycle_focus();
            break;

        default:
            if (cmd >= GMENU_CMD_WND_SELECT_BASE) {
                int target = cmd - GMENU_CMD_WND_SELECT_BASE;
                WND *w = get_wnd_list();
                int idx = 0;
                while (w) {
                    if (w->visible) {
                        if (idx == target) { top_wnd(w); break; }
                        idx++;
                    }
                    w = w->next;
                }
            }
            break;
    }
}

/* ── Mouse move (hot-tracking, dropdown hover) ───────────────── */
BOOL global_menu_handle_mouse_move(H x, H y) {
    H sw = s_gmenu_scr_w > 0 ? s_gmenu_scr_w : 1280;
    RECT tip_btn = { (H)(sw - 214 - 134 - 4), 3, (H)(sw - 214 - 4 - 6), 23 };
    g_gmenu.tip_hover = (x >= tip_btn.left && x <= tip_btn.right &&
                         y >= tip_btn.top  && y <= tip_btn.bottom);

    if (g_gmenu.active_menu >= 0) {
        /* Switch header when pointer moves across the bar while a menu is open */
        if (y >= 0 && y <= 25) {
            for (int h = 0; h < GMENU_HEADER_COUNT; h++) {
                if (x >= g_headers[h].rect.left && x <= g_headers[h].rect.right) {
                    if (g_gmenu.active_menu != h) {
                        g_gmenu.active_menu  = h;
                        g_gmenu.hover_header = h;
                        g_gmenu.hover_item   = -1;
                        if (h == GMENU_HDR_BTRON) {
                            tracker_open_menu();
                        } else {
                            if (tracker_is_menu_open()) tracker_close_menu();
                            if (h == GMENU_HDR_WINDOWS) refresh_window_menu();
                        }
                    }
                    return TRUE;
                }
            }
        }

        /* Hover inside active dropdown */
        if (g_gmenu.active_menu > 0 && g_gmenu.active_menu < GMENU_HEADER_COUNT) {
            const GMenuHeader *hdr = &g_headers[g_gmenu.active_menu];
            H menu_x = hdr->rect.left;
            H menu_y = 25;
            H menu_h = hdr->item_count * GMENU_ROW_HEIGHT + 6;

            if (x >= menu_x && x <= menu_x + GMENU_DROPDOWN_WIDTH &&
                y >= menu_y && y <= menu_y + menu_h) {
                int item_idx = (y - (menu_y + 3)) / GMENU_ROW_HEIGHT;
                if (item_idx >= 0 && item_idx < hdr->item_count &&
                    !hdr->items[item_idx].is_separator && hdr->items[item_idx].enabled) {
                    g_gmenu.hover_item = item_idx;
                } else {
                    g_gmenu.hover_item = -1;
                }
                return TRUE;
            } else {
                g_gmenu.hover_item = -1;
            }
        }

        /* Tracker hover */
        if (g_gmenu.active_menu == GMENU_HDR_BTRON && tracker_is_menu_open()) {
            RECT tr;
            if (tracker_get_menu_rect(&tr) &&
                x >= tr.left && x <= tr.right && y >= tr.top && y <= tr.bottom) {
                return tracker_handle_mouse_move(x, y);
            }
        }

        if (y < 0 || y > 25) { global_menu_close(); return TRUE; }
        return TRUE;
    }

    /* No menu open — track header hover highlight */
    if (y >= 0 && y <= 25) {
        for (int h = 0; h < GMENU_HEADER_COUNT; h++) {
            if (x >= g_headers[h].rect.left && x <= g_headers[h].rect.right) {
                g_gmenu.hover_header = h;
                return TRUE;
            }
        }
    }
    g_gmenu.hover_header = -1;
    return FALSE;
}

/* ── Mouse down ───────────────────────────────────────────────── */
BOOL global_menu_handle_mouse_down(H x, H y) {
    H sw = s_gmenu_scr_w > 0 ? s_gmenu_scr_w : 1280;
    RECT tip_btn = { (H)(sw - 214 - 134 - 4), 3, (H)(sw - 214 - 4 - 6), 23 };
    if (x >= tip_btn.left && x <= tip_btn.right &&
        y >= tip_btn.top  && y <= tip_btn.bottom) {
        tip_toggle_mode();
        return TRUE;
    }

    /* Top header bar */
    if (y >= 0 && y <= 25) {
        for (int h = 0; h < GMENU_HEADER_COUNT; h++) {
            if (x >= g_headers[h].rect.left && x <= g_headers[h].rect.right) {
                if (g_gmenu.active_menu == h) {
                    global_menu_close();
                } else {
                    g_gmenu.active_menu  = h;
                    g_gmenu.hover_header = h;
                    g_gmenu.hover_item   = -1;
                    if (h == GMENU_HDR_BTRON) {
                        tracker_open_menu();
                    } else {
                        if (tracker_is_menu_open()) tracker_close_menu();
                        if (h == GMENU_HDR_WINDOWS) refresh_window_menu();
                    }
                }
                return TRUE;
            }
        }
    }

    /* Click inside active dropdown */
    if (g_gmenu.active_menu > 0 && g_gmenu.active_menu < GMENU_HEADER_COUNT) {
        const GMenuHeader *hdr = &g_headers[g_gmenu.active_menu];
        H menu_x = hdr->rect.left;
        H menu_y = 25;
        H menu_h = hdr->item_count * GMENU_ROW_HEIGHT + 6;

        if (x >= menu_x && x <= menu_x + GMENU_DROPDOWN_WIDTH &&
            y >= menu_y && y <= menu_y + menu_h) {
            int item_idx = (y - (menu_y + 3)) / GMENU_ROW_HEIGHT;
            if (item_idx >= 0 && item_idx < hdr->item_count) {
                const GMenuItem *it = &hdr->items[item_idx];
                if (!it->is_separator && it->enabled)
                    global_menu_execute_cmd(it->cmd_id);
            }
            return TRUE;
        }
    }

    /* Tracker root menu click */
    if (g_gmenu.active_menu == GMENU_HDR_BTRON && tracker_is_menu_open()) {
        if (tracker_handle_mouse_down(x, y)) {
            if (!tracker_is_menu_open()) g_gmenu.active_menu = -1;
            return TRUE;
        }
    }

    if (g_gmenu.active_menu >= 0) { global_menu_close(); return TRUE; }
    return FALSE;
}

/* ── Keyboard navigation ──────────────────────────────────────── */
BOOL global_menu_handle_key(UW key, VW mod) {
    (void)mod;
    if (!global_menu_is_open()) return FALSE;

    if (key == BTRON_KEY_ESCAPE) { global_menu_close(); return TRUE; }

    /* Arrows move the highlight, so an open dropdown is operable without a pointer.
     * Two encodings for one key for the same reason tracker_handle_key() has them: a
     * windowed host forwards its own keysym (SDLK_UP/DOWN == 0x111/0x112) while the
     * bare-metal HID drivers inject the BTRON code.
     *
     * It is consumed only when a dropdown actually took the move.  global_menu_is_open()
     * is true whenever the launcher is open too, and workbench_process_event() asks this
     * handler first, so claiming the arrow with no dropdown active would swallow it one
     * stage before tracker_handle_key() -- which is exactly what the first PS2 run of
     * this measured: 14 D-pad DOWN hops moved the launcher's highlight not at all. */
    if (key == BTRON_KEY_DOWN || key == 0x112 ||
        key == BTRON_KEY_UP   || key == 0x111) {
        const GMenuHeader *hdr;
        int dir, i, n;

        if (g_gmenu.active_menu > 0 && g_gmenu.active_menu < GMENU_HEADER_COUNT) {
            hdr = &g_headers[g_gmenu.active_menu];
            dir = (key == BTRON_KEY_DOWN || key == 0x112) ? 1 : -1;
            n = hdr->item_count;
            i = g_gmenu.hover_item;
            for (int step = 0; step < n; step++) {
                i = (i < 0) ? (dir > 0 ? 0 : n - 1) : (i + dir + n) % n;
                if (!hdr->items[i].is_separator && hdr->items[i].enabled) break;
            }
            if (i >= 0 && i < n && !hdr->items[i].is_separator && hdr->items[i].enabled)
                g_gmenu.hover_item = i;
            return TRUE;
        }
    }

    if (key == '\r' || key == '\n' || key == ' ') {
        if (g_gmenu.active_menu > 0 && g_gmenu.active_menu < GMENU_HEADER_COUNT) {
            const GMenuHeader *hdr = &g_headers[g_gmenu.active_menu];
            if (g_gmenu.hover_item >= 0 && g_gmenu.hover_item < hdr->item_count) {
                global_menu_execute_cmd(hdr->items[g_gmenu.hover_item].cmd_id);
                return TRUE;
            }
        }
    }
    return FALSE;
}
