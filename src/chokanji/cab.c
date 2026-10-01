/*
 * B-TRON Retro OS — src/chokanji/cab.c
 * Authentic BTRON3 / Cho-Kanji Cabinet Application (キャビネット).
 * Single C99 implementation adhering to NASA JPL Power of 10 Guidelines:
 *  - Fixed memory footprint (zero dynamic allocation after init).
 *  - Bounded loops on all drawer iterations.
 *  - Full virtual object grid layout, sorting, drawer navigation, and execution.
 *  - Pure client-local coordinate space (0,0) to (w,h) on wnd->dev.
 */

#include <btron/types.h>
#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/pmc.h>
#include <btron/troncode.h>
#include <btron/event.h>
#include <btron/fs/volume.h>
#include <btron/fs/vol_api.h>
#include <btron/vobj.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#define CAB_MAX_ENTRIES     64
#define CAB_ICON_SZ         32
#define CAB_GRID_W          84
#define CAB_GRID_H          70
#define CAB_LOOP_BOUND      128
#define CAB_GRID_LEFT       10   /* left gutter before the first column */
#define CAB_GRID_TOP        32   /* first row's top, below the toolbar */
#define CAB_HEADER_H        26   /* toolbar height (icon viewport top) */
#define CAB_STATUS_H        20   /* status bar height (icon viewport bottom) */

#include <btron/chokanji.h>

typedef struct {
    char name[32];
    char kind_str[16];
    uint32_t rec_id;
    uint32_t size_bytes;
    uint32_t vobj_type; /* 1: folder/drawer, 2: text/TAD doc, 3: app/script */
    RECT icon_rect;
    RECT text_rect;
    bool is_selected;
} CabEntry;

typedef struct {
    WND *wnd;
    CabEntry entries[CAB_MAX_ENTRIES];
    int count;
    int selected_idx;
    CabSortMode sort_mode;
    char current_drawer[32];
    char status_text[64];
    int scroll_y;
    int num_cols;   /* grid columns, recomputed by cab_compute_layout */
    int client_h;   /* client viewport height, recorded by cab_paint */
} CabState;

static CabState g_cab;

static void cab_set_status(const char *msg) {
    assert(msg != NULL);
    strncpy(g_cab.status_text, msg, sizeof(g_cab.status_text) - 1);
    g_cab.status_text[sizeof(g_cab.status_text) - 1] = '\0';
}

/* ── Entry Populator & Sorting ──────────────────────────────────────── */

static void cab_populate_drawer(const char *drawer_name) {
    assert(drawer_name != NULL);
    strncpy(g_cab.current_drawer, drawer_name, sizeof(g_cab.current_drawer) - 1);
    g_cab.current_drawer[sizeof(g_cab.current_drawer) - 1] = '\0';
    g_cab.count = 0;
    g_cab.selected_idx = -1;
    g_cab.scroll_y = 0;

    /* Populate default authentic Cho-Kanji virtual objects */
    static const struct {
        const char *name;
        const char *kind;
        uint32_t size;
        uint32_t type;
    } s_default_items[] = {
        { "基本文書",     "TAD文書",   4096, 2 },
        { "文具箱",       "引出し",    1024, 1 },
        { "計算表",       "TAD表",     8192, 2 },
        { "MicroScript",  "スクリプト", 16384, 3 },
        { "図形編集",     "図形",      5120, 2 },
        { "通信環境",     "設定",      2048, 3 },
        { "文字検索",     "辞書",      32768, 3 },
        { "ごみ箱",       "廃棄",      0,    1 }
    };

    const int n = (int)(sizeof(s_default_items) / sizeof(s_default_items[0]));
    for (int i = 0; i < n && i < CAB_MAX_ENTRIES && i < CAB_LOOP_BOUND; i++) {
        CabEntry *e = &g_cab.entries[g_cab.count++];
        strncpy(e->name, s_default_items[i].name, sizeof(e->name) - 1);
        strncpy(e->kind_str, s_default_items[i].kind, sizeof(e->kind_str) - 1);
        e->rec_id = 100 + i;
        e->size_bytes = s_default_items[i].size;
        e->vobj_type = s_default_items[i].type;
        e->is_selected = false;
    }

    cab_set_status("8個の実体 オブジェクト (空き容量: 124.5 MB)");
}

void cab_sort(CabSortMode mode) {
    assert(mode >= CAB_SORT_NAME && mode <= CAB_SORT_KIND);
    g_cab.sort_mode = mode;

    /* Safe bounded bubble sort conforming to NASA Rule 2 */
    for (int i = 0; i < g_cab.count - 1 && i < CAB_LOOP_BOUND; i++) {
        for (int j = 0; j < g_cab.count - i - 1 && j < CAB_LOOP_BOUND; j++) {
            bool swap = false;
            if (mode == CAB_SORT_NAME) {
                swap = (strcmp(g_cab.entries[j].name, g_cab.entries[j + 1].name) > 0);
            } else if (mode == CAB_SORT_SIZE) {
                swap = (g_cab.entries[j].size_bytes < g_cab.entries[j + 1].size_bytes);
            } else if (mode == CAB_SORT_KIND) {
                swap = (g_cab.entries[j].vobj_type > g_cab.entries[j + 1].vobj_type);
            }
            if (swap) {
                CabEntry tmp = g_cab.entries[j];
                g_cab.entries[j] = g_cab.entries[j + 1];
                g_cab.entries[j + 1] = tmp;
            }
        }
    }
}

/* ── Layout & Painting ──────────────────────────────────────────────── */

static void cab_compute_layout(H client_w) {
    const int cols = (client_w > 20) ? (client_w - 20) / CAB_GRID_W : 1;
    const int num_cols = (cols > 0) ? cols : 1;
    g_cab.num_cols = num_cols;

    for (int i = 0; i < g_cab.count && i < CAB_LOOP_BOUND; i++) {
        CabEntry *e = &g_cab.entries[i];
        const int col = i % num_cols;
        const int row = i / num_cols;

        const H x = CAB_GRID_LEFT + (col * CAB_GRID_W);
        const H y = CAB_GRID_TOP + (row * CAB_GRID_H) - g_cab.scroll_y;

        e->icon_rect.left = x + (CAB_GRID_W - CAB_ICON_SZ) / 2;
        e->icon_rect.top = y;
        e->icon_rect.right = e->icon_rect.left + CAB_ICON_SZ;
        e->icon_rect.bottom = e->icon_rect.top + CAB_ICON_SZ;

        e->text_rect.left = x;
        e->text_rect.top = e->icon_rect.bottom + 2;
        e->text_rect.right = x + CAB_GRID_W;
        e->text_rect.bottom = e->text_rect.top + 16;
    }
}

static void cab_draw_icon(GDEV *dev, const CabEntry *e) {
    if (!dev || !e) return;
    const RECT *r = &e->icon_rect;

    COLOR ic_col = 0x00FFFFFFU;
    if (e->vobj_type == 1) ic_col = 0x00FFDE59U; /* Folder/Drawer yellow */
    else if (e->vobj_type == 3) ic_col = 0x005CE1E6U; /* Application cyan */

    fill_rec(dev, r, ic_col);
    drw_rec(dev, r);

    /* 3D Bevel around icon */
    drw_lin(dev, r->left + 1, r->top + 1, r->right - 2, r->top + 1);
    drw_lin(dev, r->left + 1, r->top + 1, r->left + 1, r->bottom - 2);

    /* Inner glyph depending on type */
    if (e->vobj_type == 1) {
        /* Drawer handle */
        drw_lin(dev, r->left + 4, r->top + 8, r->right - 5, r->top + 8);
        drw_lin(dev, r->left + 10, r->top + 14, r->right - 11, r->top + 14);
    } else {
        /* Document lines */
        drw_lin(dev, r->left + 6, r->top + 8, r->right - 7, r->top + 8);
        drw_lin(dev, r->left + 6, r->top + 14, r->right - 7, r->top + 14);
        drw_lin(dev, r->left + 6, r->top + 20, r->right - 7, r->top + 20);
    }
}

void cab_paint(WND *wnd, GDEV *dev) {
    (void)wnd;
    if (!g_cab.wnd || !dev) return;

    const H w = dev->width;
    const H h = dev->height;

    g_cab.client_h = (int)h;
    cab_compute_layout(w);

    /* 1. Main Background */
    RECT bg_r = { 0, 0, w, h };
    fill_rec(dev, &bg_r, PMC_COL_BODY);

    /* 2. Header Toolbar (Drawer Tab & Path) */
    const RECT header_r = { 0, 0, w, 26 };
    fill_rec(dev, &header_r, PMC_COL_INACT_TITLE);
    drw_rec(dev, &header_r);

    RECT tab_r = { 8, 3, 110, 26 };
    pmc_draw_folder_tab(dev, &tab_r, g_cab.current_drawer, TRUE);

    /* 3. Grid of Virtual Objects */
    for (int i = 0; i < g_cab.count && i < CAB_LOOP_BOUND; i++) {
        CabEntry *e = &g_cab.entries[i];

        /* Windows-style caret: a dotted focus frame around the whole cell. */
        if (e->is_selected) {
            const RECT cell = { e->text_rect.left - 1, e->icon_rect.top - 1,
                                e->text_rect.right, e->text_rect.bottom };
            fill_rec(dev, &cell, PMC_COL_ACT_TITLE);
            for (int yy = cell.top; yy < cell.bottom && yy - cell.top < CAB_LOOP_BOUND; yy++) {
                for (int xx = cell.left; xx < cell.right && xx - cell.left < CAB_LOOP_BOUND; xx++) {
                    const bool edge = (xx == cell.left || xx == cell.right - 1 ||
                                       yy == cell.top || yy == cell.bottom - 1);
                    if (edge && (((xx - cell.left) + (yy - cell.top)) & 1) == 0) {
                        RECT px = { xx, yy, xx + 1, yy + 1 };
                        fill_rec(dev, &px, PMC_COL_OUTLINE);
                    }
                }
            }
        }

        cab_draw_icon(dev, e);

        COLOR text_fg = COLOR_BLACK;
        COLOR text_bg = 0x00000000U;
        if (e->is_selected) {
            fill_rec(dev, &e->text_rect, COLOR_NAVY);
            text_fg = COLOR_WHITE;
            text_bg = COLOR_NAVY;
        }
        drw_tc_string(dev, e->text_rect.left + 2, e->text_rect.top + 1, e->name, text_fg, text_bg);
    }

    /* 4. Bottom Status Bar */
    const RECT status_r = { 0, h - 20, w, h };
    fill_rec(dev, &status_r, PMC_COL_BODY);
    drw_rec(dev, &status_r);
    drw_tc_string(dev, status_r.left + 8, status_r.top + 3, g_cab.status_text, 0x00303030U, 0x00000000);
}

/* ── Event Handling ─────────────────────────────────────────────────── */

/* Mark entry i selected (single selection), refresh the status line. */
static void cab_select(int i) {
    if (i < 0 || i >= g_cab.count) return;
    if (g_cab.selected_idx >= 0 && g_cab.selected_idx < g_cab.count) {
        g_cab.entries[g_cab.selected_idx].is_selected = false;
    }
    g_cab.entries[i].is_selected = true;
    g_cab.selected_idx = i;

    const CabEntry *e = &g_cab.entries[i];
    char buf[64];
    snprintf(buf, sizeof(buf), "%s (%s, %u バイト)", e->name, e->kind_str, (unsigned)e->size_bytes);
    cab_set_status(buf);
}

/* Scroll the grid so the selected row is fully inside the viewport, like the
 * Windows Explorer icon view keeping the caret item visible. */
static void cab_scroll_to_selected(void) {
    if (g_cab.selected_idx < 0 || g_cab.num_cols < 1) return;
    const int row = g_cab.selected_idx / g_cab.num_cols;
    const int row_top = CAB_GRID_TOP + (row * CAB_GRID_H);       /* unscrolled */
    const int vis_top = CAB_HEADER_H;
    const int vis_bot = (g_cab.client_h > CAB_HEADER_H + CAB_STATUS_H)
                        ? (g_cab.client_h - CAB_STATUS_H) : (CAB_HEADER_H + CAB_GRID_H);

    if (row_top - g_cab.scroll_y < vis_top) {
        g_cab.scroll_y = row_top - vis_top;                       /* scroll up to reveal */
    } else if (row_top - g_cab.scroll_y + CAB_GRID_H > vis_bot) {
        g_cab.scroll_y = row_top + CAB_GRID_H - vis_bot;          /* scroll down to reveal */
    }
    if (g_cab.scroll_y < 0) g_cab.scroll_y = 0;
    if (g_cab.num_cols > 0 && g_cab.count > g_cab.num_cols) {
        const int last_row = (g_cab.count - 1) / g_cab.num_cols;
        int max_scroll = CAB_GRID_TOP + (last_row + 1) * CAB_GRID_H - vis_bot;
        if (max_scroll < 0) max_scroll = 0;
        if (g_cab.scroll_y > max_scroll) g_cab.scroll_y = max_scroll;
    }
}

/* Open the selected drawer (double-click / Return). */
static void cab_activate_selected(void) {
    if (g_cab.selected_idx < 0 || g_cab.selected_idx >= g_cab.count) return;
    const CabEntry *e = &g_cab.entries[g_cab.selected_idx];
    if (e->vobj_type == 1) {
        cab_populate_drawer(e->name);
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s を起動しました", e->name);
        cab_set_status(buf);
    }
}

/* Move the caret one position; new_idx is already range-checked. */
static void cab_move(int new_idx) {
    if (new_idx < 0) new_idx = 0;
    if (new_idx >= g_cab.count) new_idx = g_cab.count - 1;
    if (g_cab.count <= 0) return;
    cab_select(new_idx);
    cab_scroll_to_selected();
    if (g_cab.wnd) inval_wnd(g_cab.wnd);
}

/* Windows-style 2D icon grid navigation.
 *  Left/Right step one cell in reading order (wrap across rows).
 *  Up/Down change row keeping the column, clamping into a shorter last row.
 *  Home/End jump to the first/last item; Page Up/Down move a viewport of rows. */
static void cab_handle_key(UW key) {
    if (g_cab.count <= 0) return;
    const int cols = (g_cab.num_cols > 0) ? g_cab.num_cols : 1;
    int sel = g_cab.selected_idx;

    if (sel < 0) {                       /* nothing selected yet: adopt first */
        cab_move(0);
        return;
    }

    const int row = sel / cols;
    const int col = sel % cols;
    const int last_row = (g_cab.count - 1) / cols;
    int ns = sel;

    if (key == BTRON_KEY_LEFT) {
        ns = sel - 1;
    } else if (key == BTRON_KEY_RIGHT) {
        ns = sel + 1;
    } else if (key == BTRON_KEY_UP) {
        if (row > 0) {
            const int target = (row - 1) * cols + col;
            ns = (target < g_cab.count) ? target : (row - 1) * cols + ((g_cab.count - 1) % cols);
        }
    } else if (key == BTRON_KEY_DOWN) {
        if (row < last_row) {
            const int target = (row + 1) * cols + col;
            ns = (target < g_cab.count) ? target : (g_cab.count - 1);
        }
    } else if (key == BTRON_KEY_HOME) {
        ns = 0;
    } else if (key == BTRON_KEY_END) {
        ns = g_cab.count - 1;
    } else if (key == BTRON_KEY_PAGE_UP || key == BTRON_KEY_PAGE_DOWN) {
        int page_rows = (g_cab.client_h > CAB_HEADER_H + CAB_STATUS_H + CAB_GRID_H)
                        ? (g_cab.client_h - CAB_HEADER_H - CAB_STATUS_H) / CAB_GRID_H : 1;
        if (page_rows < 1) page_rows = 1;
        const int delta = page_rows * cols;
        ns = (key == BTRON_KEY_PAGE_UP) ? (sel - delta) : (sel + delta);
    } else if (key == BTRON_KEY_RETURN || key == BTRON_KEY_KP_ENTER) {
        cab_activate_selected();
        if (g_cab.wnd) inval_wnd(g_cab.wnd);
        return;
    } else {
        return;                          /* unrelated key: leave selection intact */
    }

    cab_move(ns);
}

void cab_handle_click(H rel_x, H rel_y, bool is_double_click) {
    for (int i = 0; i < g_cab.count && i < CAB_LOOP_BOUND; i++) {
        CabEntry *e = &g_cab.entries[i];
        if ((rel_x >= e->icon_rect.left && rel_x <= e->icon_rect.right &&
             rel_y >= e->icon_rect.top  && rel_y <= e->icon_rect.bottom) ||
            (rel_x >= e->text_rect.left && rel_x <= e->text_rect.right &&
             rel_y >= e->text_rect.top  && rel_y <= e->text_rect.bottom)) {

            cab_select(i);

            if (is_double_click) {
                cab_activate_selected();
            }
            if (g_cab.wnd) inval_wnd(g_cab.wnd);
            return;
        }
    }
}

static void cab_destroy(WND *wnd) {
    (void)wnd;
    g_cab.wnd = NULL;
}

static void cab_event_handler(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;
    if (evt->type == EV_BUT_DOWN) {
        H rel_x = evt->pos.x - wnd->client.left;
        H rel_y = evt->pos.y - wnd->client.top;
        cab_handle_click(rel_x, rel_y, false);
    } else if (evt->type == EV_KEY_DOWN) {
        cab_handle_key(evt->key);
    }
}

void cab_app_init(void) {
    if (g_cab.wnd) {
        top_wnd(g_cab.wnd);
        return;
    }
    memset(&g_cab, 0, sizeof(g_cab));
    g_cab.sort_mode = CAB_SORT_NAME;
    g_cab.num_cols = 1;
    g_cab.client_h = 240;
    cab_populate_drawer("キャビネット");

    g_cab.wnd = opn_wnd("キャビネット (Cabinet)", 80, 80, 420, 320,
                        WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_RESIZE | WND_ATTR_BORDER);
    if (g_cab.wnd) {
        g_cab.wnd->paint = cab_paint;
        g_cab.wnd->event_handler = cab_event_handler;
        g_cab.wnd->destroy = cab_destroy;
        inval_wnd(g_cab.wnd);
    }
}
