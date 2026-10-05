/*
 * B-System (BTRON 3.20) Control Panel & Settings Cabinet Explorer: control_panel.c
 * Designed in SONY Precision Industrial Aesthetic with SONY_* color palette,
 * subelement-first geometry calculations, and tactile 10-bar preference applet grid
 * with multi-line word-wrapped descriptions (2-3 lines).
 * Conforms to ./b-system/settings/ specifications.
 */

#include <btron/settings.h>
#include <btron/dp.h>
#include <btron/troncode.h>
#include <btron/wnd.h>
#include <btron/settings_icon.h>

#include <btron/libc_shim.h>

/* SONY Industrial Color Palette (Conforming to About Box) */
#define SONY_COL_CANVAS       0xFF16191E  /* Deep Titanium Slate */
#define SONY_COL_HEADER       0xFF1E222A  /* Brushed Graphite Header */
#define SONY_COL_PANEL        0xFF1A1D24  /* Sub-Panel Background */
#define SONY_COL_INSET        0xFF111317  /* Inset Monitor Black */
#define SONY_COL_BORDER_HI    0xFF3B4454  /* Bevel Highlight Border */
#define SONY_COL_BORDER_LO    0xFF0D0F12  /* Bevel Shadow Border */
#define SONY_COL_BORDER_MID   0xFF2A313D  /* Framing Hairline */
#define SONY_COL_GOLD         0xFFF59E0B  /* SONY Amber / Accent Gold */
#define SONY_COL_CYAN         0xFF38BDF8  /* High-Tech Diagnostic Cyan */
#define SONY_COL_GREEN        0xFF22C55E  /* LED Active Green */
#define SONY_COL_TEXT_WHITE   0xFFF8FAFC  /* Platinum White Text */
#define SONY_COL_TEXT_SILVER  0xFFCBD5E1  /* High-Readability Silver */
#define SONY_COL_TEXT_DIM     0xFF94A3B8  /* Technical Metric Gray */
#define SONY_COL_BTN_BG       0xFF282F3B  /* Tactile Button Face */

static const SETTINGS_APP_INFO g_app_registry[] = {
    { "appearance", "Appearance", "外観",         "[ART]", "Themes, colours, window style, fonts & animations",    open_appearance_settings_window },
    { "desktop",    "Desktop",    "デスクトップ", "[DSK]", "Workbench behaviour, icon layout & background",        open_desktop_settings_window },
    { "display",    "Display",    "画面表示",     "[DSP]", "Resolution, DPI scaling, refresh rate & VESA",         open_display_settings_window },
    { "input",      "Input",      "入力環境",     "[INP]", "Keyboard layouts, key repeat & mouse acceleration",    open_input_settings_window },
    { "language",   "Language",   "言語・文字",   "[LAN]", "TRON Code, TIP Mozc, Tibetan Wylie & shortcuts",       open_language_settings_window },
    { "media",      "Media",      "メディア",     "[MED]", "Default handlers, TAD/PDF/Audio associations",         open_media_settings_window },
    { "network",    "Network",    "通信網",       "[NET]", "Interfaces, DHCP, DNS, VirtIO-Net & XMPP",             open_network_settings_window },
    { "security",   "Security",   "保全・権限",   "[SEC]", "Permissions, real-object ACLs & capability isolation", open_security_settings_window },
    { "sound",      "Sound",      "音響・音声",   "[SND]", "Audio devices, Cassette routing & master volume",      open_sound_settings_window },
    { "system",     "System",     "基本情報",     "[SYS]", "System Plane info, kernel tasks, memory & SMP",        open_system_settings_window },
    { "terminal",   "Terminal",   "端末・通信",   "[TRM]", "Console colours, font size, scrollback buffer, cursor & encoding", open_terminal_settings_window }
};

#define APP_REGISTRY_COUNT (sizeof(g_app_registry) / sizeof(g_app_registry[0]))

const SETTINGS_APP_INFO* settings_get_app_info(SETTINGS_APP_ID app_id) {
    if (app_id >= 1 && app_id <= (SETTINGS_APP_ID)APP_REGISTRY_COUNT) {
        return &g_app_registry[app_id - 1];
    }
    return NULL;
}

int settings_get_app_count(void) {
    return (int)APP_REGISTRY_COUNT;
}

/* Subelement-First Geometry Calculations */
typedef struct {
    /* Subelement 1: Image / Icon Placeholder Box */
    RECT icon_box;
    H icon_symbol_x;
    H icon_symbol_y;

    /* Subelement 2: Title & Category Label */
    H title_x;
    H title_y;

    /* Subelement 3: Description */
    H desc_x;
    H desc_y;

    /* Subelement 4: Status / ID Badge */
    RECT badge_box;
    H badge_text_x;
    H badge_text_y;

    /* Enclosing Bounding Box for this Bar (Parent of subelements) */
    RECT bar_box;
} ControlBarLayout;

typedef struct {
    /* Parent Enclosing Container Bounding Box (Enclosing all 10 Bars) */
    RECT parent_container_box;

    /* Subelement-calculated bar structures */
    ControlBarLayout bars[APP_REGISTRY_COUNT];
} ControlPanelLayout;

typedef struct {
    WND *wnd;
    int selected_index;
    int hover_index;
} ControlPanelApp;

static ControlPanelApp g_ctrl_panel;

/* Fast beveled box helper */
static void draw_beveled_box(GDEV *dev, const RECT *r, COLOR fill_col, COLOR hi, COLOR lo) {
    if (!dev || !r) return;
    fill_rec(dev, r, fill_col);
    drw_lin(dev, r->left, r->top, r->right - 1, r->top);
    drw_lin(dev, r->left, r->top, r->left, r->bottom - 1);
    drw_lin(dev, r->left + 1, r->bottom - 1, r->right, r->bottom - 1);
    drw_lin(dev, r->right - 1, r->top + 1, r->right - 1, r->bottom);
    (void)hi;
    (void)lo;
}

/* Word-wrap text into up to max_lines lines, each up to max_chars characters */
static int word_wrap_text(const char *src, char lines[][64], int max_lines, int max_chars) {
    if (!src || max_lines <= 0 || max_chars <= 0) return 0;

    int line_count = 0;
    int src_len = (int)strlen(src);
    int pos = 0;

    while (pos < src_len && line_count < max_lines) {
        /* Skip leading whitespace */
        while (pos < src_len && src[pos] == ' ') pos++;
        if (pos >= src_len) break;

        /* Find how many words fit in max_chars */
        int start = pos;
        int last_valid_end = start;
        int cur_end = start;

        while (cur_end <= src_len && (cur_end - start) <= max_chars) {
            if (cur_end == src_len || src[cur_end] == ' ') {
                last_valid_end = cur_end;
            }
            if (cur_end == src_len) break;
            cur_end++;
        }

        /* If even a single word exceeds max_chars, hard break */
        if (last_valid_end == start) {
            last_valid_end = start + max_chars;
            if (last_valid_end > src_len) last_valid_end = src_len;
        }

        int copy_len = last_valid_end - start;
        if (copy_len > 60) copy_len = 60;
        for (int i = 0; i < copy_len; i++) {
            lines[line_count][i] = src[start + i];
        }
        lines[line_count][copy_len] = '\0';
        line_count++;
        pos = last_valid_end;
    }

    return line_count;
}

/* Calculation of subelements before setting rounding/bounding box of parent bar */
static void calculate_bar_layout(ControlBarLayout *bar, H x, H y, H w, H h, const SETTINGS_APP_INFO *info) {
    if (!bar || !info) return;

    BTRON_ICON_SIZE icon_sz = appearance_get_icon_size();
    H icon_box_dim = (icon_sz == BTRON_ICON_SIZE_32) ? 40 : 72;

    /* 1. Calculate Image / Icon Placeholder subelement */
    H icon_w = icon_box_dim;
    H icon_h = icon_box_dim;
    if (w < 90) icon_w = (w > 20) ? (w / 2) : 10;
    if (h < icon_box_dim) icon_h = (h > 12) ? (h - 10) : 6;

    H icon_pad_x = 8;
    H icon_pad_y = (h > icon_h) ? (h - icon_h) / 2 : 2;

    bar->icon_box.left = x + icon_pad_x;
    bar->icon_box.top = y + icon_pad_y;
    bar->icon_box.right = bar->icon_box.left + icon_w;
    bar->icon_box.bottom = bar->icon_box.top + icon_h;

    /* Centered symbol inside image placeholder */
    int sym_len = (int)strlen(info->icon_symbol);
    H sym_text_w = sym_len * 8;
    bar->icon_symbol_x = bar->icon_box.left + (icon_w > sym_text_w ? (icon_w - sym_text_w) / 2 : 2);
    bar->icon_symbol_y = bar->icon_box.top + (icon_h > 12 ? (icon_h - 12) / 2 : 2);

    /* 2. Calculate Text Label subelements */
    H text_pad_left = 10;
    bar->title_x = bar->icon_box.right + text_pad_left;
    bar->title_y = y + ((icon_sz == BTRON_ICON_SIZE_32) ? 5 : 7);

    bar->desc_x = bar->title_x;
    bar->desc_y = y + ((icon_sz == BTRON_ICON_SIZE_32) ? 22 : 25);

    /* 3. Calculate Status / Category Badge subelement */
    H badge_w = 46;
    H badge_h = 16;
    bar->badge_box.right = x + w - 8;
    bar->badge_box.left = (bar->badge_box.right > x + badge_w) ? (bar->badge_box.right - badge_w) : x;
    bar->badge_box.top = bar->title_y;
    bar->badge_box.bottom = bar->badge_box.top + badge_h;

    bar->badge_text_x = bar->badge_box.left + 5;
    bar->badge_text_y = bar->badge_box.top + 2;

    /* 4. Set enclosing bounding box of the Bar item (Parent of subelements) */
    bar->bar_box.left = x;
    bar->bar_box.top = y;
    bar->bar_box.right = x + w;
    bar->bar_box.bottom = y + h;
}

/* Master calculation of entire Control Panel Layout:
 * Calculates all 10 bars and their subelements FIRST,
 * then establishes the parent enclosing container bounding box.
 */
static void calculate_control_panel_layout(ControlPanelLayout *layout, H client_w, H client_h) {
    if (!layout) return;

    if (client_w < 200) client_w = 200;
    if (client_h < 150) client_h = 150;

    BTRON_ICON_SIZE icon_sz = appearance_get_icon_size();
    H item_h = (icon_sz == BTRON_ICON_SIZE_32) ? 52 : 80;

    H start_x = 16;
    H start_y = 60;
    H item_w = (client_w - 48) / 2;
    if (item_w < 80) item_w = 80;

    H min_x = client_w;
    H min_y = client_h;
    H max_x = 0;
    H max_y = 0;

    for (int i = 0; i < (int)APP_REGISTRY_COUNT; i++) {
        int col = i % 2;
        int row = i / 2;
        H x = start_x + col * (item_w + 16);
        H y = start_y + row * (item_h + 8);

        calculate_bar_layout(&layout->bars[i], x, y, item_w, item_h, &g_app_registry[i]);

        if (layout->bars[i].bar_box.left < min_x) min_x = layout->bars[i].bar_box.left;
        if (layout->bars[i].bar_box.top < min_y) min_y = layout->bars[i].bar_box.top;
        if (layout->bars[i].bar_box.right > max_x) max_x = layout->bars[i].bar_box.right;
        if (layout->bars[i].bar_box.bottom > max_y) max_y = layout->bars[i].bar_box.bottom;
    }

    /* Set parent container bounding box based on the calculated extent of child bars */
    layout->parent_container_box.left = (min_x >= 6) ? (min_x - 6) : 0;
    layout->parent_container_box.top = (min_y >= 6) ? (min_y - 6) : 0;
    layout->parent_container_box.right = max_x + 6;
    layout->parent_container_box.bottom = max_y + 6;
}

static void paint_control_panel(WND *wnd, GDEV *dev) {
    if (!wnd || !dev || dev->width < 10 || dev->height < 10) return;

    /* Pre-calculate layout: Subelements first, then parent bounding boxes */
    ControlPanelLayout layout;
    calculate_control_panel_layout(&layout, dev->width, dev->height);

    /* 1. Main SONY Titanium Canvas */
    RECT bg_r = { 0, 0, dev->width, dev->height };
    fill_rec(dev, &bg_r, SONY_COL_CANVAS);
    drw_rec(dev, &bg_r);

    /* 2. SONY Brushed Graphite Header (0 to 52px) */
    H hdr_h = (dev->height > 60) ? 52 : (dev->height / 2);
    RECT hdr_r = { 0, 0, dev->width, hdr_h };
    fill_rec(dev, &hdr_r, SONY_COL_HEADER);
    drw_lin(dev, 0, hdr_h, dev->width, hdr_h);

    /* SONY Brand Badge */
    drw_tc_string(dev, 14, 8, "S O N Y", SONY_COL_TEXT_WHITE, SONY_COL_HEADER);
    if (dev->width > 220) {
        drw_tc_string(dev, 80, 8, "|  SETTINGS CABINET & CONTROL PANEL", SONY_COL_GOLD, SONY_COL_HEADER);
    }

    /* Sub-Model & Specification Badges */
    if (hdr_h >= 32 && dev->width > 300) {
        drw_tc_string(dev, 14, 24, "SUBSYSTEM: PREFERENCES & ENVIRONMENT MANAGER (環境設定)", SONY_COL_TEXT_SILVER, SONY_COL_HEADER);
    }

    /* Status Indicators (LEDs) */
    if (hdr_h >= 46 && dev->width > 200) {
        drw_tc_string(dev, 14, 38, "* SYSTEM: ONLINE", SONY_COL_GREEN, SONY_COL_HEADER);
        if (dev->width > 340) {
            drw_tc_string(dev, 160, 38, "* APPLETS: 10 READY", SONY_COL_CYAN, SONY_COL_HEADER);
        }
        if (dev->width > 480) {
            drw_tc_string(dev, 320, 38, "* CONFIG: SYNCED", SONY_COL_GOLD, SONY_COL_HEADER);
        }
    }

    /* 3. Parent Enclosing Container Box (Enclosing all 10 Bars) */
    if (layout.parent_container_box.right > layout.parent_container_box.left &&
        layout.parent_container_box.bottom > layout.parent_container_box.top) {
        fill_rec(dev, &layout.parent_container_box, SONY_COL_INSET);
        drw_rec(dev, &layout.parent_container_box);
    }

    /* 4. Render 10 Control Panel Bars with Multi-Line Descriptions */
    for (int i = 0; i < (int)APP_REGISTRY_COUNT; i++) {
        const ControlBarLayout *b = &layout.bars[i];
        if (b->bar_box.top >= dev->height) continue;
        BOOL is_sel = (i == g_ctrl_panel.selected_index);

        COLOR bar_bg = is_sel ? SONY_COL_BTN_BG : SONY_COL_PANEL;
        COLOR border_hi = is_sel ? SONY_COL_GOLD : SONY_COL_BORDER_HI;
        COLOR border_lo = is_sel ? SONY_COL_GOLD : SONY_COL_BORDER_LO;

        /* Enclosing Bar bounding box */
        draw_beveled_box(dev, &b->bar_box, bar_bg, border_hi, border_lo);

        if (is_sel) {
            /* Active selection indicator bar on left edge */
            drw_lin(dev, b->bar_box.left + 1, b->bar_box.top + 2, b->bar_box.left + 1, b->bar_box.bottom - 2);
            drw_lin(dev, b->bar_box.left + 2, b->bar_box.top + 2, b->bar_box.left + 2, b->bar_box.bottom - 2);
            drw_lin(dev, b->bar_box.left + 3, b->bar_box.top + 2, b->bar_box.left + 3, b->bar_box.bottom - 2);
        }

        /* Image Placeholder Box (Subelement) */
        if (b->icon_box.right > b->icon_box.left && b->icon_box.bottom > b->icon_box.top) {
            COLOR icon_bg = is_sel ? SONY_COL_HEADER : SONY_COL_INSET;
            COLOR icon_border = is_sel ? SONY_COL_GOLD : SONY_COL_CYAN;
            fill_rec(dev, &b->icon_box, icon_bg);
            drw_rec(dev, &b->icon_box);

            /* Icon accent corners */
            drw_lin(dev, b->icon_box.left, b->icon_box.top, b->icon_box.left + 3, b->icon_box.top);
            drw_lin(dev, b->icon_box.right - 4, b->icon_box.bottom - 1, b->icon_box.right - 1, b->icon_box.bottom - 1);

            /* Draw GIF icon scaled to current preference (32×32 or 64×64) */
            BTRON_ICON_SIZE icon_sz = appearance_get_icon_size();
            int icon_dim = (icon_sz == BTRON_ICON_SIZE_32) ? 32 : 64;
            H icon_draw_x = b->icon_box.left + (H)(((int)(b->icon_box.right - b->icon_box.left) - icon_dim) / 2);
            H icon_draw_y = b->icon_box.top  + (H)(((int)(b->icon_box.bottom - b->icon_box.top)  - icon_dim) / 2);
            draw_setting_gif_icon_scaled(dev, g_app_registry[i].id_str, (int)icon_draw_x, (int)icon_draw_y, icon_dim, icon_dim);
            /* Fallback text glyph when icon did not render (freestanding or missing file) */
            if (b->icon_box.right - b->icon_box.left < 32) {
                drw_tc_string(dev, b->icon_symbol_x, b->icon_symbol_y,
                              g_app_registry[i].icon_symbol, icon_border, icon_bg);
            }
        }

        /* Title (Subelement) */
        char title_buf[96];
        snprintf(title_buf, sizeof(title_buf), "%s (%s)", g_app_registry[i].title, g_app_registry[i].title_ja);
        drw_tc_string(dev, b->title_x, b->title_y, title_buf, is_sel ? SONY_COL_GOLD : SONY_COL_TEXT_WHITE, bar_bg);

        /* Status Badge (Subelement) */
        if (b->badge_box.right > b->badge_box.left && b->badge_box.bottom > b->badge_box.top &&
            b->badge_box.left > b->icon_box.right + 20) {
            fill_rec(dev, &b->badge_box, is_sel ? SONY_COL_HEADER : SONY_COL_INSET);
            drw_rec(dev, &b->badge_box);
            drw_tc_string(dev, b->badge_text_x, b->badge_text_y, is_sel ? "OPEN" : "READY", is_sel ? SONY_COL_GOLD : SONY_COL_CYAN, is_sel ? SONY_COL_HEADER : SONY_COL_INSET);
        }

        /* Description Subelement: Multi-Line Word-Wrapped (2-3 lines) */
        H avail_desc_w = b->bar_box.right - b->desc_x - 8;
        int max_desc_chars = (avail_desc_w > 0) ? (avail_desc_w / 8) : 0;
        if (max_desc_chars > 6) {
            char desc_lines[3][64];
            int num_lines = word_wrap_text(g_app_registry[i].desc, desc_lines, 3, max_desc_chars);
            H line_step = 15;
            for (int l = 0; l < num_lines; l++) {
                H ly = b->desc_y + l * line_step;
                if (ly + 12 > b->bar_box.bottom - 2) break;
                drw_tc_string(dev, b->desc_x, ly, desc_lines[l], is_sel ? SONY_COL_TEXT_SILVER : SONY_COL_TEXT_DIM, bar_bg);
            }
        }
    }

    /* 5. High-Tech Telemetry Footer Bar */
    if (dev->height > 50) {
        RECT ftr_r = { 0, dev->height - 36, dev->width, dev->height };
        fill_rec(dev, &ftr_r, SONY_COL_HEADER);
        drw_lin(dev, 0, dev->height - 36, dev->width, dev->height - 36);

        H btn_h = 22;
        H btn_y = dev->height - 29;

        H close_w = 80;
        H open_w = 100;
        if (dev->width < 250) {
            close_w = 50;
            open_w = 60;
        }

        RECT btn_close = { dev->width - close_w - 10, btn_y, dev->width - 10, btn_y + btn_h };
        draw_beveled_box(dev, &btn_close, SONY_COL_BTN_BG, SONY_COL_BORDER_HI, SONY_COL_BORDER_LO);
        drw_tc_string(dev, btn_close.left + 16, btn_y + 3, "Close", SONY_COL_TEXT_WHITE, SONY_COL_BTN_BG);

        RECT btn_open = { btn_close.left - open_w - 10, btn_y, btn_close.left - 10, btn_y + btn_h };
        draw_beveled_box(dev, &btn_open, SONY_COL_BTN_BG, SONY_COL_BORDER_HI, SONY_COL_BORDER_LO);
        drw_tc_string(dev, btn_open.left + 7, btn_y + 3, "Open (開く)", SONY_COL_CYAN, SONY_COL_BTN_BG);

        /* Telemetry readout (only if room permits) */
        if (btn_open.left > 180) {
            char telem[64];
            snprintf(telem, sizeof(telem), "WS-9800 | 10 APPS | NVRAM: SYNCED");
            drw_tc_string(dev, 14, dev->height - 24, telem, SONY_COL_TEXT_DIM, SONY_COL_HEADER);
        }
    }
}

static void launch_and_center_applet(int index) {
    if (index >= 0 && index < (int)APP_REGISTRY_COUNT && g_app_registry[index].open_func) {
        WND *opened = g_app_registry[index].open_func();
        if (!opened) {
            opened = get_top_wnd();
        }
        if (opened) {
            GDEV *scr = wnd_mgr_get_screen();
            H scr_w = scr ? scr->width : 800;
            H scr_h = scr ? scr->height : 600;
            H w = opened->bounds.right - opened->bounds.left;
            H h = opened->bounds.bottom - opened->bounds.top;
            H cx = (scr_w - w) / 2;
            H cy = (scr_h - h) / 2;
            if (cx < 10) cx = 10;
            if (cy < 30) cy = 30;
            mov_wnd(opened, cx, cy);
        }
    }
}

static void handle_control_panel_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;

    if (evt->type == EV_BUT_DOWN) {
        H rel_x = evt->pos.x - wnd->client.left;
        H rel_y = evt->pos.y - wnd->client.top;
        H client_w = wnd->client.right - wnd->client.left;
        H client_h = wnd->client.bottom - wnd->client.top;
        if (client_w < 10 || client_h < 10) return;

        /* Calculate exact bar bounding boxes */
        ControlPanelLayout layout;
        calculate_control_panel_layout(&layout, client_w, client_h);

        /* Check Grid Clicks via calculated bar bounding boxes */
        for (int i = 0; i < (int)APP_REGISTRY_COUNT; i++) {
            const RECT *bar = &layout.bars[i].bar_box;
            if (rel_x >= bar->left && rel_x <= bar->right &&
                rel_y >= bar->top && rel_y <= bar->bottom) {
                g_ctrl_panel.selected_index = i;
                /* Launch and center applet */
                launch_and_center_applet(i);
                redraw_all_windows();
                return;
            }
        }

        /* Check Footer Buttons */
        H btn_h = 22;
        H btn_y = client_h - 29;
        H close_w = 80;
        H open_w = 100;
        if (client_w < 250) {
            close_w = 50;
            open_w = 60;
        }

        RECT btn_close = { client_w - close_w - 10, btn_y, client_w - 10, btn_y + btn_h };
        RECT btn_open = { btn_close.left - open_w - 10, btn_y, btn_close.left - 10, btn_y + btn_h };

        if (rel_y >= btn_y - 2 && rel_y <= btn_y + btn_h + 2) {
            if (rel_x >= btn_open.left && rel_x <= btn_open.right) {
                launch_and_center_applet(g_ctrl_panel.selected_index);
                return;
            }
            if (rel_x >= btn_close.left && rel_x <= btn_close.right) {
                cls_wnd(wnd);
                return;
            }
        }
    }
}

WND* open_control_panel_window(void) {
    g_ctrl_panel.selected_index = 0;
    BTRON_ICON_SIZE sz = appearance_get_icon_size();
    GDEV *scr = wnd_mgr_get_screen();
    H scr_w = scr ? scr->width : 1280;
    H scr_h = scr ? scr->height : 800;
    int win_w = 680;
    int win_h = (sz == BTRON_ICON_SIZE_32) ? 520 : 660;
    H win_x = (scr_w - win_w) / 2;
    H win_y = (scr_h - win_h) / 2;
    if (win_x < 10) win_x = 10;
    if (win_y < 28) win_y = 28;
    WND *wnd = opn_wnd("環境設定コントロールパネル (Preferences Panel)",
                       win_x, win_y, win_w, win_h,
                       WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_BORDER);
    if (!wnd) return NULL;
    g_ctrl_panel.wnd = wnd;
    wnd->user_data = (VW)(uintptr_t)&g_ctrl_panel;
    wnd->paint = paint_control_panel;
    wnd->event_handler = handle_control_panel_event;
    return wnd;
}
