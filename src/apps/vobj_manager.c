/*
 * B-System (BTRON 3.20) BTRON Accessory: Real Body Cabinet & Virtual Body Explorer Window (vobj_manager)
 * Cleanroom implementation of Sakamura BTRON / BTRON3 Architecture & NASA JPL Scope.
 */

#include <btron/wnd.h>
#include <btron/vobj.h>
#include <btron/troncode.h>
#include <btron/dp.h>
#include <btron/event.h>
#include <btron/error.h>
#include <btron/tad_browser.h>
#include <btron/app_menu.h>
#include <btron/settings.h>
#include <btron/settings_icon.h>
#include <btron/dnd.h>

#include <btron/libc_shim.h>
#if BTRON_HOSTED
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak)) WND* open_t_editor_window(void) {
    return (void*)0;
}
__attribute__((weak)) WND* open_t_editor_window_with_file(const char *filepath) {
    (void)filepath;
    return (void*)0;
}
__attribute__((weak)) WND* open_paint_window(void) {
    return (void*)0;
}
__attribute__((weak)) WND* open_paint_window_with_file(const char *filepath) {
    (void)filepath;
    return (void*)0;
}
#else
extern WND* open_t_editor_window(void);
extern WND* open_t_editor_window_with_file(const char *filepath);
extern WND* open_paint_window(void);
extern WND* open_paint_window_with_file(const char *filepath);
#endif

/* Open a cabinet item in the app suited to its type: GIF/PNG to the Paint
 * viewer, TXT/MD to the text editor, everything else to the TAD browser —
 * the same rule the double-click and menu-open paths already follow. */
static void cab_open_path(const char *path, const char *name) {
    int len = strlen(path);
    BOOL is_image = (len > 4 &&
                     (strcmp(path + len - 4, ".gif") == 0 ||
                      strcmp(path + len - 4, ".png") == 0));
    BOOL is_text  = (len > 3 && strcmp(path + len - 3, ".md") == 0) ||
                    (len > 4 && strcmp(path + len - 4, ".txt") == 0);
    if (is_image) {
        open_paint_window_with_file(path);
    } else if (is_text) {
        open_t_editor_window_with_file(path);
    } else {
        open_tad_browser_window(path, name);
    }
}

#define MAX_CABINET_ITEMS 2048

typedef enum {
    CAB_VIEW_LIST = 0,
    CAB_VIEW_GRID = 1
} CAB_VIEW_MODE;

typedef struct {
    ID robj_id;
    VOBJ_TYPE type;
    char name[64];
    char path[128];
    UW size_bytes;
    const char *icon_tag;
    const char *category;
} CABINET_ITEM;

typedef struct {
    CABINET_ITEM items[MAX_CABINET_ITEMS];
    int item_count;
    int selected_idx;
    int hovered_idx;
    int scroll_offset;      /* Scroll item row offset (LIST view) */
    int grid_scroll_rows;   /* First visible row index in GRID icon view */
    CAB_VIEW_MODE view_mode;
    char status_msg[128];

    /* In-Window Application Menu State */
    int active_menu;       /* -1 = closed, 0..4 = active header index */
    int hover_menu;        /* -1 = none, 0..4 = hovered header in closed state */
    int hover_item;        /* -1 = none, 0..N = hovered dropdown item */
    int sort_mode;         /* 0=Name, 1=Date/ID, 2=Size, 3=TOC/Type */
    APP_MENU_BAR menu_bar;
} CABINET_EXPLORER;

static CABINET_EXPLORER g_cabinet;
static BOOL s_cab_mouse_down = FALSE;
static H s_cab_down_x = 0;
static H s_cab_down_y = 0;
static BOOL s_sbar_dragging = FALSE;   /* vertical scrollbar thumb is held down */
static H    s_sbar_drag_y = 0;          /* pointer y when the thumb was grabbed */
static int  s_sbar_drag_off = 0;        /* scroll offset when the thumb was grabbed */

#define CAB_GRID_TOP 26
#define CAB_SBAR_W   16   /* reserved right gutter for the vertical scrollbar */

/* Single source of truth for the icon-grid geometry so the painter, the hit
 * tests and the keyboard caret all agree on column count and cell size.
 * The right gutter is reserved for the scrollbar so column count never changes
 * when the bar appears or disappears (the caret stays under the same column). */
static void cab_grid_metrics(H dev_w, H dev_h, int *out_cols, int *out_col_w, int *out_row_h, int *out_rows_visible) {
    BTRON_ICON_SIZE sz = appearance_get_icon_size();
    int col_w = (sz == BTRON_ICON_SIZE_32) ? 96 : 110;
    int row_h = (sz == BTRON_ICON_SIZE_32) ? 72 : 104;
    int usable = (dev_w - 16 - CAB_SBAR_W);
    if (usable < col_w) usable = col_w;
    int cols = usable / col_w;
    if (cols < 1) cols = 1;
    int content_h = (dev_h > CAB_GRID_TOP + 24) ? (dev_h - CAB_GRID_TOP - 24) : row_h;
    int rows_visible = content_h / row_h;
    if (rows_visible < 1) rows_visible = 1;
    if (out_cols) *out_cols = cols;
    if (out_col_w) *out_col_w = col_w;
    if (out_row_h) *out_row_h = row_h;
    if (out_rows_visible) *out_rows_visible = rows_visible;
}

/* Current scroll extent for whichever view is active, in whole units.
 * LIST scrolls by items (scroll_offset); GRID scrolls by rows (grid_scroll_rows). */
static void cab_scroll_get(H dev_w, H dev_h, int *total_units, int *vis_units, int *off_units) {
    if (g_cabinet.view_mode == CAB_VIEW_GRID) {
        int cols, rows_visible;
        cab_grid_metrics(dev_w, dev_h, &cols, NULL, NULL, &rows_visible);
        int trows = (g_cabinet.item_count + cols - 1) / cols;
        if (trows < 1) trows = 1;
        *total_units = trows;
        *vis_units = rows_visible;
        *off_units = g_cabinet.grid_scroll_rows;
    } else {
        int content_h = (dev_h > CAB_GRID_TOP + 24) ? (dev_h - CAB_GRID_TOP - 24) : 22;
        int vrows = content_h / 22;
        if (vrows < 1) vrows = 1;
        *total_units = g_cabinet.item_count;
        *vis_units = vrows;
        *off_units = g_cabinet.scroll_offset;
    }
}

/* ── Windows 95 style vertical scrollbar ────────────────────────────────────
 * A 16px column in the right gutter: up arrow, page track, draggable thumb,
 * down arrow. Interaction follows the Windows 95 model:
 *   • arrow click  -> step one unit
 *   • track click  -> page by one viewport (thumb does NOT jump to the click)
 *   • thumb drag   -> proportional scroll, only when the thumb is grabbed
 *   • mouse wheel  -> scrolls the content plane, never the slider
 * When the content already fits the thumb stretches to fill the track and the
 * arrows render greyed (disabled) rather than the bar being hidden. */
#define CAB_SBAR_BTN 16

typedef struct {
    RECT  bar;         /* full scrollbar column */
    RECT  up_btn;      /* top arrow button */
    RECT  dn_btn;      /* bottom arrow button */
    RECT  thumb;       /* elevator / thumb */
    int   track_top;   /* page area top (below the up arrow) */
    int   track_bot;   /* page area bottom (above the down arrow) */
    BOOL  overflow;    /* content taller than the viewport */
} cab_sbar_t;

static void cab_sbar_layout(H dev_w, H dev_h, cab_sbar_t *sb) {
    int total, vis, off;
    cab_scroll_get(dev_w, dev_h, &total, &vis, &off);
    int max_off = total - vis;
    if (max_off < 0) max_off = 0;

    sb->bar.left = (int)dev_w - CAB_SBAR_W;
    sb->bar.top = CAB_GRID_TOP;
    sb->bar.right = (int)dev_w;
    sb->bar.bottom = (int)dev_h - 24;

    sb->up_btn.left = sb->bar.left; sb->up_btn.right = sb->bar.right;
    sb->up_btn.top = sb->bar.top;   sb->up_btn.bottom = sb->bar.top + CAB_SBAR_BTN;

    sb->dn_btn.left = sb->bar.left;  sb->dn_btn.right = sb->bar.right;
    sb->dn_btn.top = sb->bar.bottom - CAB_SBAR_BTN; sb->dn_btn.bottom = sb->bar.bottom;

    sb->track_top = sb->bar.top + CAB_SBAR_BTN;
    sb->track_bot = sb->bar.bottom - CAB_SBAR_BTN;
    int track_h = sb->track_bot - sb->track_top;
    if (track_h < 1) track_h = 1;

    sb->overflow = (max_off > 0);
    int th_h, ty;
    if (sb->overflow) {
        th_h = (track_h * vis) / total;
        if (th_h < 14) th_h = 14;
        if (th_h > track_h) th_h = track_h;
        if (off < 0) off = 0;
        if (off > max_off) off = max_off;
        ty = sb->track_top + (off * (track_h - th_h)) / max_off;
    } else {
        th_h = track_h;
        ty = sb->track_top;
    }
    sb->thumb.left = sb->bar.left + 2; sb->thumb.right = sb->bar.right - 2;
    sb->thumb.top = ty; sb->thumb.bottom = ty + th_h;
}

/* Move the active view's scroll offset to an absolute value, clamped to the
 * scrollable range. Never touches the selection caret (Windows behaviour:
 * the scrollbar/wheel scroll the viewport without changing the selection). */
static void cab_set_offset_clamped(int ns, H dev_w, H dev_h) {
    int total, vis, off;
    cab_scroll_get(dev_w, dev_h, &total, &vis, &off);
    int max_off = total - vis;
    if (max_off < 0) max_off = 0;
    if (ns < 0) ns = 0;
    if (ns > max_off) ns = max_off;
    if (g_cabinet.view_mode == CAB_VIEW_GRID) g_cabinet.grid_scroll_rows = ns;
    else g_cabinet.scroll_offset = ns;
}

/* Scroll the icon grid so the selected cell's row is inside the viewport,
 * like Windows Explorer keeping the caret item visible during arrow navigation. */
static void cab_grid_keep_visible(H dev_w, H dev_h) {
    int cols, rows_visible;
    cab_grid_metrics(dev_w, dev_h, &cols, NULL, NULL, &rows_visible);
    if (cols < 1) return;
    const int row = g_cabinet.selected_idx / cols;
    if (row < g_cabinet.grid_scroll_rows) {
        g_cabinet.grid_scroll_rows = row;
    } else if (row >= g_cabinet.grid_scroll_rows + rows_visible) {
        g_cabinet.grid_scroll_rows = row - rows_visible + 1;
    }
    int total, vis, off;
    cab_scroll_get(dev_w, dev_h, &total, &vis, &off);
    if (g_cabinet.grid_scroll_rows > total - vis) {
        g_cabinet.grid_scroll_rows = (total > vis) ? total - vis : 0;
    }
    if (g_cabinet.grid_scroll_rows < 0) g_cabinet.grid_scroll_rows = 0;
}

/* Windows-style 2D caret movement over the icon grid.
 *  Left/Right step one column within the row.
 *  Up/Down change row keeping the column, clamping into a shorter last row.
 *  Home/End jump to the first/last item; Page Up/Down move a viewport of rows. */
static void cab_grid_navigate(UW key, H dev_w, H dev_h) {
    int ncols;
    cab_grid_metrics(dev_w, dev_h, &ncols, NULL, NULL, NULL);
    int sel = g_cabinet.selected_idx;

    if (sel < 0) {
        sel = 0;
    } else if (key == BTRON_KEY_LEFT) {
        const int col = sel % ncols;
        sel = (col > 0) ? sel - 1 : sel;
    } else if (key == BTRON_KEY_RIGHT) {
        const int col = sel % ncols;
        if (col < ncols - 1 && sel + 1 < g_cabinet.item_count) sel = sel + 1;
    } else if (key == BTRON_KEY_UP || key == 'k') {
        const int row = sel / ncols, col = sel % ncols;
        if (row > 0) {
            const int target = (row - 1) * ncols + col;
            sel = (target < g_cabinet.item_count) ? target : g_cabinet.item_count - 1;
        }
    } else if (key == BTRON_KEY_DOWN || key == 'j') {
        const int row = sel / ncols;
        const int last_row = (g_cabinet.item_count - 1) / ncols;
        if (row < last_row) {
            const int target = (row + 1) * ncols + (sel % ncols);
            sel = (target < g_cabinet.item_count) ? target : g_cabinet.item_count - 1;
        }
    } else if (key == BTRON_KEY_HOME) {
        sel = 0;
    } else if (key == BTRON_KEY_END) {
        sel = g_cabinet.item_count - 1;
    } else if (key == BTRON_KEY_PAGE_UP || key == BTRON_KEY_PAGE_DOWN) {
        int rows_visible;
        cab_grid_metrics(dev_w, dev_h, NULL, NULL, NULL, &rows_visible);
        const int delta = rows_visible * ncols;
        sel = (key == BTRON_KEY_PAGE_UP) ? (sel - delta) : (sel + delta);
    } else {
        return;
    }

    if (sel < 0) sel = 0;
    if (sel > g_cabinet.item_count - 1) sel = g_cabinet.item_count - 1;
    g_cabinet.selected_idx = sel;
    cab_grid_keep_visible(dev_w, dev_h);
}

/* Draw the Windows 95 scrollbar: recessed track, raised arrow buttons with
 * triangles (same style as the Terminal), and a raised elevator thumb. When
 * nothing overflows the thumb fills the track and the arrows render greyed. */
static void cab_draw_scrollbar(GDEV *dev, H dev_w, H dev_h) {
    cab_sbar_t sb;
    cab_sbar_layout(dev_w, dev_h, &sb);
    if (sb.bar.bottom <= sb.bar.top + 2 * CAB_SBAR_BTN) return;

    const int sx = sb.bar.left;
    const int sy = sb.bar.top;
    const int sh = sb.bar.bottom - sb.bar.top;
    const COLOR btn_pen = sb.overflow ? COLOR_BLACK : COLOR_DKGRAY;

    /* Recessed face across the whole column. */
    fill_rec(dev, &sb.bar, COLOR_LTGRAY);
    set_col(dev, COLOR_BLACK, COLOR_LTGRAY);
    drw_lin(dev, sx, sy + CAB_SBAR_BTN, sx + CAB_SBAR_W - 1, sy + CAB_SBAR_BTN);
    drw_lin(dev, sx, sy + sh - CAB_SBAR_BTN, sx + CAB_SBAR_W - 1, sy + sh - CAB_SBAR_BTN);

    /* Up arrow button. */
    fill_rec(dev, &sb.up_btn, COLOR_LTGRAY);
    drw_rec(dev, &sb.up_btn);
    set_col(dev, btn_pen, COLOR_LTGRAY);
    drw_lin(dev, sx + 8, sy + 4, sx + 4, sy + 11);
    drw_lin(dev, sx + 8, sy + 4, sx + 12, sy + 11);
    drw_lin(dev, sx + 4, sy + 11, sx + 12, sy + 11);

    /* Down arrow button. */
    const int dy_b = sy + sh - CAB_SBAR_BTN;
    fill_rec(dev, &sb.dn_btn, COLOR_LTGRAY);
    set_col(dev, COLOR_BLACK, COLOR_LTGRAY);
    drw_rec(dev, &sb.dn_btn);
    set_col(dev, btn_pen, COLOR_LTGRAY);
    drw_lin(dev, sx + 4, dy_b + 5, sx + 12, dy_b + 5);
    drw_lin(dev, sx + 4, dy_b + 5, sx + 8, dy_b + 12);
    drw_lin(dev, sx + 12, dy_b + 5, sx + 8, dy_b + 12);

    /* Elevator thumb with a raised bevel. */
    fill_rec(dev, &sb.thumb, sb.overflow ? COLOR_GRAY : COLOR_LTGRAY);
    set_col(dev, COLOR_BLACK, COLOR_LTGRAY);
    drw_rec(dev, &sb.thumb);
    drw_lin(dev, sb.thumb.left, sb.thumb.top, sb.thumb.right - 1, sb.thumb.top);
    drw_lin(dev, sb.thumb.left, sb.thumb.top, sb.thumb.left, sb.thumb.bottom - 1);
    drw_lin(dev, sb.thumb.left, sb.thumb.bottom - 1, sb.thumb.right - 1, sb.thumb.bottom - 1);
    drw_lin(dev, sb.thumb.right - 1, sb.thumb.top, sb.thumb.right - 1, sb.thumb.bottom - 1);
}




/* ── Natural String Comparison (Case-Insensitive & Numeric Aware) ───────── */
static int natural_compare(const char *s1, const char *s2) {
    if (!s1 || !s2) return 0;
    while (*s1 && *s2) {
        if (*s1 >= '0' && *s1 <= '9' && *s2 >= '0' && *s2 <= '9') {
            long n1 = 0, n2 = 0;
            while (*s1 >= '0' && *s1 <= '9') {
                n1 = n1 * 10 + (*s1 - '0');
                s1++;
            }
            while (*s2 >= '0' && *s2 <= '9') {
                n2 = n2 * 10 + (*s2 - '0');
                s2++;
            }
            if (n1 != n2) return (n1 < n2) ? -1 : 1;
        } else {
            char c1 = *s1;
            char c2 = *s2;
            if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
            if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
            if (c1 != c2) return (c1 < c2) ? -1 : 1;
            s1++;
            s2++;
        }
    }
    return (*s1 == '\0' && *s2 == '\0') ? 0 : (*s1 == '\0' ? -1 : 1);
}

/* ── Canonical Table of Contents (TOC) Sequence Weighting ───────────────── */
static int get_toc_order(const char *path, const char *name) {
    if (!path) return 1000;

    /* 1. Canonical Foundational Books & Main Portals */
    if (strstr(path, "01_btron3_spec")) return 10;
    if (strstr(path, "02_bcore_book")) return 20;
    if (strstr(path, "03_bfree_os_book")) return 30;
    if (strstr(path, "04_tron_hmi_book")) return 35;

    /* 2. Top-level portal index */
    if (name && strcmp(name, "index.tad") == 0 && strcmp(path, "tad_bin/index.tad") == 0) return 40;

    /* 3. [doc] Shared Data Specifications (Chapter 1..6) */
    if (strstr(path, "shared_data/data_type")) return 100;
    if (strstr(path, "shared_data/tron_code")) return 110;
    if (strstr(path, "shared_data/tad1"))      return 120;
    if (strstr(path, "shared_data/tad2"))      return 130;
    if (strstr(path, "shared_data/tad3"))      return 140;
    if (strstr(path, "shared_data/fd_format")) return 150;
    if (strstr(path, "shared_data/index.tad")) return 160;
    if (strstr(path, "shared_data/indexfig"))  return 170;

    /* 4. [doc] OS Spec Portals */
    if (strstr(path, "os_spec/index.tad"))     return 200;
    if (strstr(path, "os_spec/indexfig.tad"))  return 205;

    /* 5. [doc] μITRON 3.0 Real-Time Kernel Subsystems */
    if (strstr(path, "os_spec/kernel/kernel"))   return 210;
    if (strstr(path, "os_spec/kernel/proc"))     return 220;
    if (strstr(path, "os_spec/kernel/memory"))   return 230;
    if (strstr(path, "os_spec/kernel/file"))     return 240;
    if (strstr(path, "os_spec/kernel/event"))    return 250;
    if (strstr(path, "os_spec/kernel/clk"))      return 260;
    if (strstr(path, "os_spec/kernel/device"))   return 270;
    if (strstr(path, "os_spec/kernel/message"))  return 280;
    if (strstr(path, "os_spec/kernel/taskcomm")) return 290;
    if (strstr(path, "os_spec/kernel/system"))   return 300;
    if (strstr(path, "os_spec/kernel/gname"))    return 310;

    /* 6. [doc] 2D Display Primitives Graphics */
    if (strstr(path, "os_spec/dp/dp.tad"))           return 400;
    if (strstr(path, "os_spec/dp/basic_concept"))    return 410;
    if (strstr(path, "os_spec/dp/basic_func"))       return 420;
    if (strstr(path, "os_spec/dp/character_func"))   return 430;
    if (strstr(path, "os_spec/dp/figure_func"))      return 440;
    if (strstr(path, "os_spec/dp/pointer_func"))     return 450;
    if (strstr(path, "os_spec/dp/intro"))            return 460;

    /* 7. [doc] BTRON3 Graphical User Interface & Shell */
    if (strstr(path, "os_spec/shell/shell.tad"))     return 500;
    if (strstr(path, "os_spec/shell/window"))        return 510;
    if (strstr(path, "os_spec/shell/menu"))          return 520;
    if (strstr(path, "os_spec/shell/panel"))         return 530;
    if (strstr(path, "os_spec/shell/parts"))         return 540;
    if (strstr(path, "os_spec/shell/font_mgr"))      return 550;
    if (strstr(path, "os_spec/shell/printmgr"))      return 560;
    if (strstr(path, "os_spec/shell/tip"))           return 570;
    if (strstr(path, "os_spec/shell/tray"))          return 580;
    if (strstr(path, "os_spec/shell/tcpip"))         return 590;
    if (strstr(path, "os_spec/shell/omgr"))          return 600;
    if (strstr(path, "os_spec/shell/data"))          return 610;

    /* 8. [b-hmi] TRON HMI Design Guidelines & Parts Book */
    if (strstr(path, "b-hmi/index"))      return 640;
    if (strstr(path, "b-hmi/part1"))      return 650;
    if (strstr(path, "b-hmi/part2"))      return 660;
    if (strstr(path, "b-hmi/part_book"))  return 670;
    if (strstr(path, "b-hmi"))            return 680;

    /* 9. [b-free] B-Free OS Architecture & Manifesto */
    if (strstr(path, "b-free/manifest"))   return 700;
    if (strstr(path, "b-free/kernel"))     return 710;
    if (strstr(path, "b-free/posix"))      return 720;
    if (strstr(path, "b-free/btron"))      return 730;
    if (strstr(path, "b-free/boot_arch"))  return 740;
    if (strstr(path, "b-free/source_tree"))return 750;
    if (strstr(path, "b-free/index"))      return 760;

    /* 10. [b-system] B-System POSIX & VirtIO Specs */
    if (strstr(path, "b-system/virtio"))     return 800;
    if (strstr(path, "b-system/btron_spec")) return 810;
    if (strstr(path, "b-system/kernel"))     return 820;
    if (strstr(path, "b-system/license"))    return 830;
    if (strstr(path, "b-system/index"))      return 840;

    /* 11. [b-core] B-Core InterCore Protocol Specs */
    if (strstr(path, "b-core/theorems"))                return 900;
    if (strstr(path, "b-core/index") || strstr(path, "b-core/B-Core")) return 910;

    /* 12. [b-book] B-Book Developer's Manual (12 Subsystems) */
    if (strstr(path, "b-book/B-Book") || strstr(path, "b-book/index")) return 940;
    if (strstr(path, "b-book/kernel"))   return 941;
    if (strstr(path, "b-book/cores"))    return 942;
    if (strstr(path, "b-book/graphics")) return 943;
    if (strstr(path, "b-book/tip"))      return 944;
    if (strstr(path, "b-book/vobject"))  return 945;
    if (strstr(path, "b-book/window"))   return 946;
    if (strstr(path, "b-book/desktop"))  return 947;
    if (strstr(path, "b-book/hmi"))      return 948;
    if (strstr(path, "b-book/font"))     return 949;
    if (strstr(path, "b-book/settings")) return 950;
    if (strstr(path, "b-book/drivers"))  return 951;
    if (strstr(path, "b-book/apps"))     return 952;

    return 1000;
}

/* ── Sort Cabinet Items: Group by First Column (icon_tag) & TOC Order ──── */
static void cabinet_sort_items(CABINET_EXPLORER *cab) {
    if (!cab || cab->item_count <= 1) return;
    for (int i = 0; i < cab->item_count - 1; i++) {
        for (int j = i + 1; j < cab->item_count; j++) {
            int swap = 0;
            if (cab->sort_mode == 1) {
                /* Sort by Name */
                if (natural_compare(cab->items[i].name, cab->items[j].name) > 0) swap = 1;
            } else if (cab->sort_mode == 2) {
                /* Sort by ID / Date */
                if (cab->items[i].robj_id > cab->items[j].robj_id) swap = 1;
            } else if (cab->sort_mode == 3) {
                /* Sort by Size */
                if (cab->items[i].size_bytes < cab->items[j].size_bytes) swap = 1;
            } else {
                /* 0 = Default: Group by First Column (icon_tag) & TOC Order */
                int cmp_grp = strcmp(cab->items[i].icon_tag, cab->items[j].icon_tag);
                if (cmp_grp > 0) {
                    swap = 1;
                } else if (cmp_grp == 0) {
                    int order_i = get_toc_order(cab->items[i].path, cab->items[i].name);
                    int order_j = get_toc_order(cab->items[j].path, cab->items[j].name);
                    if (order_i > order_j) swap = 1;
                    else if (order_i == order_j && natural_compare(cab->items[i].name, cab->items[j].name) > 0) swap = 1;
                }
            }
            if (swap) {
                CABINET_ITEM tmp = cab->items[i];
                cab->items[i] = cab->items[j];
                cab->items[j] = tmp;
            }
        }
    }
}

#if BTRON_HOSTED

static ID deduce_robj_id(const char *path) {
    if (strstr(path, "01_btron3_spec")) return 101;
    if (strstr(path, "02_bcore_book")) return 102;
    if (strstr(path, "03_bfree_os_book")) return 103;
    if (strstr(path, "04_tron_hmi_book")) return 104;
    if (strstr(path, "b-hmi/index.tad") || strstr(path, "b-hmi/B-HMI.tad")) return 105;
    if (strstr(path, "b-free/manifest.tad")) return 106;
    if (strstr(path, "b-system/virtio.tad")) return 107;
    if (strstr(path, "data_type.tad")) return 111;
    if (strstr(path, "tron_code.tad")) return 112;
    if (strstr(path, "tad1.tad")) return 113;
    if (strstr(path, "tad2.tad")) return 114;
    if (strstr(path, "tad3.tad")) return 115;
    if (strstr(path, "fd_format.tad")) return 116;
    if (strstr(path, "kernel/kernel.tad") || strstr(path, "b-book/kernel")) return 121;
    if (strstr(path, "kernel/proc.tad")) return 122;
    if (strstr(path, "kernel/memory.tad")) return 123;
    if (strstr(path, "dp/dp.tad")) return 131;
    if (strstr(path, "shell/shell.tad")) return 141;
    if (strstr(path, "shell/window.tad")) return 142;
    if (strstr(path, "indexfig.tad")) return 161;

    UW h = 5381;
    for (int i = 0; path[i]; i++) {
        h = ((h << 5) + h) + (UB)path[i];
    }
    /* Keep dynamically assigned IDs outside the canonical 101..999 range.
     * The old 900-slot hash space could collide with a reserved Real Body ID. */
    return 1000 + (h % 99000);
}

static const char* deduce_toc_path(const char *path) {
    if (strstr(path, "01_btron3_spec") || strstr(path, "02_bcore_book") ||
        strstr(path, "03_bfree_os_book") || strstr(path, "04_tron_hmi_book")) {
        return "books/";
    }
    if (strstr(path, "b-book/")) return "b-book/";
    if (strstr(path, "shared_data/")) return "shared_data/";
    if (strstr(path, "os_spec/kernel/")) return "os_spec/kernel/";
    if (strstr(path, "os_spec/dp/")) return "os_spec/dp/";
    if (strstr(path, "os_spec/shell/")) return "os_spec/shell/";
    if (strstr(path, "os_spec/")) return "os_spec/";
    if (strstr(path, "b-hmi/part1")) return "b-hmi/part1/";
    if (strstr(path, "b-hmi/part2")) return "b-hmi/part2/";
    if (strstr(path, "b-hmi/part_book")) return "b-hmi/parts/";
    if (strstr(path, "b-hmi/")) return "b-hmi/";
    if (strstr(path, "b-free/")) return "b-free/";
    if (strstr(path, "b-system/")) return "b-system/";
    if (strstr(path, "b-core/")) return "b-core/";
    return "root/";
}

static const char* deduce_icon_tag(const char *path) {
    if (strstr(path, "04_tron_hmi") || strstr(path, "b-hmi")) return "[b-hmi]";
    if (strstr(path, "03_bfree") || strstr(path, "b-free")) return "[b-free]";
    if (strstr(path, "b-system")) return "[b-system]";
    if (strstr(path, "02_bcore") || strstr(path, "b-core")) return "[b-core]";
    if (strstr(path, "b-book")) return "[doc]";
    if (strstr(path, "01_btron3") || strstr(path, "shared_data") || strstr(path, "os_spec") || strstr(path, "doc")) return "[doc]";
    return "[doc]";
}

/* Check if directory contains any non-index .tad file */
static int dir_has_titled_tad(const char *dir_path) {
    DIR *d = opendir(dir_path);
    if (!d) return 0;
    struct dirent *de;
    int found = 0;
    while ((de = readdir(d)) != NULL) {
        int len = strlen(de->d_name);
        if (len > 4 && strcmp(de->d_name + len - 4, ".tad") == 0 && strcmp(de->d_name, "index.tad") != 0) {
            found = 1;
            break;
        }
    }
    closedir(d);
    return found;
}

/* Deduce a human-friendly document title for Cabinet Explorer */
static void get_friendly_title(const char *sub_path, const char *filename, char *out_name, size_t max_len) {
    if (!sub_path || !filename || !out_name || max_len == 0) return;

    if (strcmp(filename, "index.tad") != 0) {
        strncpy(out_name, filename, max_len - 1);
        out_name[max_len - 1] = '\0';
        return;
    }

    if (strstr(sub_path, "b-book/kernel")) strncpy(out_name, "Kernel.tad", max_len - 1);
    else if (strstr(sub_path, "b-book/cores")) strncpy(out_name, "Cores.tad", max_len - 1);
    else if (strstr(sub_path, "b-book/graphics")) strncpy(out_name, "Graphics.tad", max_len - 1);
    else if (strstr(sub_path, "b-book/tip")) strncpy(out_name, "Tip.tad", max_len - 1);
    else if (strstr(sub_path, "b-book/vobject")) strncpy(out_name, "VObject.tad", max_len - 1);
    else if (strstr(sub_path, "b-book/window")) strncpy(out_name, "Window.tad", max_len - 1);
    else if (strstr(sub_path, "b-book/desktop")) strncpy(out_name, "Desktop.tad", max_len - 1);
    else if (strstr(sub_path, "b-book/hmi")) strncpy(out_name, "HMI.tad", max_len - 1);
    else if (strstr(sub_path, "b-book/font")) strncpy(out_name, "Font.tad", max_len - 1);
    else if (strstr(sub_path, "b-book/settings")) strncpy(out_name, "Settings.tad", max_len - 1);
    else if (strstr(sub_path, "b-book/drivers")) strncpy(out_name, "Drivers.tad", max_len - 1);
    else if (strstr(sub_path, "b-book/apps")) strncpy(out_name, "Apps.tad", max_len - 1);
    else if (strstr(sub_path, "b-book")) strncpy(out_name, "B-Book.tad", max_len - 1);
    else if (strstr(sub_path, "b-hmi/part1")) strncpy(out_name, "Part1.tad", max_len - 1);
    else if (strstr(sub_path, "b-hmi/part2")) strncpy(out_name, "Part2.tad", max_len - 1);
    else if (strstr(sub_path, "b-hmi/part_book")) strncpy(out_name, "Part_Book.tad", max_len - 1);
    else if (strstr(sub_path, "b-hmi")) strncpy(out_name, "B-HMI.tad", max_len - 1);
    else if (strstr(sub_path, "b-core")) strncpy(out_name, "B-Core.tad", max_len - 1);
    else if (strstr(sub_path, "b-system")) strncpy(out_name, "B-System.tad", max_len - 1);
    else if (strstr(sub_path, "b-free")) strncpy(out_name, "B-Free.tad", max_len - 1);
    else if (strstr(sub_path, "shared_data")) strncpy(out_name, "Shared_Data.tad", max_len - 1);
    else if (strstr(sub_path, "os_spec")) strncpy(out_name, "OS_Spec.tad", max_len - 1);
    else strncpy(out_name, "B-System_Portal.tad", max_len - 1);
    out_name[max_len - 1] = '\0';
}

/* ── Dynamic Recursive Filesystem Discovery (Discovered not Hardcoded) ── */
static void cabinet_discover_dir(CABINET_EXPLORER *cab, const char *dir_path) {
    DIR *d = opendir(dir_path);
    if (!d) return;

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;

        char sub_path[256];
        snprintf(sub_path, sizeof(sub_path), "%s/%s", dir_path, de->d_name);

        struct stat st;
        if (stat(sub_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            cabinet_discover_dir(cab, sub_path);
        } else if (S_ISREG(st.st_mode)) {
            int len = strlen(de->d_name);
            BOOL is_tad = (len > 4 && strcmp(de->d_name + len - 4, ".tad") == 0);
            BOOL is_md  = (len > 3 && strcmp(de->d_name + len - 3, ".md") == 0);
            BOOL is_txt = (len > 4 && strcmp(de->d_name + len - 4, ".txt") == 0);
            BOOL is_png = (len > 4 && (strcmp(de->d_name + len - 4, ".png") == 0 || strcmp(de->d_name + len - 4, ".PNG") == 0));
            BOOL is_gif = (len > 4 && (strcmp(de->d_name + len - 4, ".gif") == 0 || strcmp(de->d_name + len - 4, ".GIF") == 0));

            if (is_tad || is_md || is_txt || is_png || is_gif) {
                /* Skip .tad.txt companions or index.tad duplicates */
                if (is_tad && strcmp(de->d_name, "index.tad") == 0 && dir_has_titled_tad(dir_path)) {
                    continue;
                }
                if (is_txt && strstr(de->d_name, ".tad.txt")) {
                    continue;
                }

                if (cab->item_count < MAX_CABINET_ITEMS) {
                    CABINET_ITEM *it = &cab->items[cab->item_count++];
                    it->robj_id = deduce_robj_id(sub_path);
                    if (is_png || is_gif) {
                        it->type = VOBJ_TYPE_DRAW;
                        it->icon_tag = "[img]";
                    } else if (is_md) {
                        it->type = VOBJ_TYPE_TEXT;
                        it->icon_tag = "[md]";
                    } else if (is_txt) {
                        it->type = VOBJ_TYPE_TEXT;
                        it->icon_tag = "[txt]";
                    } else {
                        it->type = VOBJ_TYPE_TEXT;
                        it->icon_tag = deduce_icon_tag(sub_path);
                    }
                    get_friendly_title(sub_path, de->d_name, it->name, sizeof(it->name));
                    strncpy(it->path, sub_path, sizeof(it->path) - 1);
                    it->size_bytes = (UW)st.st_size;
                    it->category = deduce_toc_path(sub_path);
                }
            }
        }
    }
    closedir(d);
}
#endif

static const char* deduce_gif_icon(const CABINET_ITEM *it) {
    if (!it) return "tad_browser";
    if (it->type == VOBJ_TYPE_DRAW) return "paint";
    if (strstr(it->path, "shared_data")) return "notebook";
    if (strstr(it->path, "b-hmi")) return "appearance";
    if (strstr(it->path, "b-core") || strstr(it->path, "kernel")) return "system";
    if (strstr(it->path, "b-free")) return "workbench";
    return "tad_browser";
}

static void cabinet_init_defaults(CABINET_EXPLORER *cab) {
    memset(cab, 0, sizeof(CABINET_EXPLORER));
    cab->selected_idx = 0;
    cab->hovered_idx = -1;
    cab->scroll_offset = 0;
    cab->view_mode = CAB_VIEW_LIST;

#if BTRON_HOSTED
    /* Dynamic discovery: canonical books in tad_bin, plus SYS, doc/md, assets */
    cabinet_discover_dir(cab, "tad_bin");
    cabinet_discover_dir(cab, "SYS");
    cabinet_discover_dir(cab, "doc/md");
    cabinet_discover_dir(cab, "assets/texts");
    cabinet_discover_dir(cab, "assets/icons");
    cabinet_discover_dir(cab, "btron_store");
#endif

    if (cab->item_count > 0) {
        /* Group by first column & sort by TOC order */
        cabinet_sort_items(cab);
        snprintf(cab->status_msg, sizeof(cab->status_msg),
                 "Cabinet Ready. Discovered %d Real Bodys across tad_bin/.", cab->item_count);
        return;
    }

    /* Fallback static list for freestanding embedded environments */
    int n = 0;
    cab->items[n++] = (CABINET_ITEM){ 101, VOBJ_TYPE_TEXT, "01_btron3_spec.tad", "tad_bin/01_btron3_spec.tad", 3996, "[doc]", "books/" };
    cab->items[n++] = (CABINET_ITEM){ 102, VOBJ_TYPE_TEXT, "02_bcore_book.tad", "tad_bin/02_bcore_book.tad", 1377, "[b-core]", "books/" };
    cab->items[n++] = (CABINET_ITEM){ 103, VOBJ_TYPE_TEXT, "03_bfree_os_book.tad", "tad_bin/03_bfree_os_book.tad", 1472, "[b-free]", "books/" };
    cab->items[n++] = (CABINET_ITEM){ 111, VOBJ_TYPE_TEXT, "01_data_type.tad", "tad_bin/shared_data/data_type.tad", 10022, "[doc]", "shared_data/" };
    cab->item_count = n;
    cabinet_sort_items(cab);
    strncpy(cab->status_msg, "Cabinet Ready (Embedded Static Fallback).", sizeof(cab->status_msg) - 1);
}

#define CMENU_HDR_COUNT     5
#define CMENU_HDR_FILE      0
#define CMENU_HDR_EDIT      1
#define CMENU_HDR_VIEW      2
#define CMENU_HDR_VOBJ      3
#define CMENU_HDR_HELP      4

#define CMENU_DROPDOWN_WIDTH 250
#define CMENU_ROW_HEIGHT    20

enum {
    CCMD_NONE = 0,
    /* File */
    CCMD_FILE_OPEN = 10,
    CCMD_FILE_VIEW_TAD,
    CCMD_FILE_NEW,
    CCMD_FILE_DUPLICATE,
    CCMD_FILE_DELETE,
    CCMD_FILE_CLOSE,
    /* Edit */
    CCMD_EDIT_SELECT_ALL = 20,
    CCMD_EDIT_DESELECT,
    CCMD_EDIT_RENAME,
    CCMD_EDIT_PROPERTIES,
    /* View */
    CCMD_VIEW_LIST = 30,
    CCMD_VIEW_GRID,
    CCMD_VIEW_SORT_NAME,
    CCMD_VIEW_SORT_DATE,
    CCMD_VIEW_SORT_SIZE,
    CCMD_VIEW_SORT_TYPE,
    CCMD_VIEW_REFRESH,
    /* VObj */
    CCMD_VOBJ_CREATE_LINK = 40,
    CCMD_VOBJ_MOVE,
    CCMD_VOBJ_GLOBAL_INDEX,
    /* Help */
    CCMD_HELP_ABOUT = 50,
    CCMD_HELP_GUIDE
};

static void cab_sync_menu_state(void) {
    g_cabinet.active_menu = g_cabinet.menu_bar.active_menu;
    g_cabinet.hover_menu = g_cabinet.menu_bar.hover_menu;
    g_cabinet.hover_item = g_cabinet.menu_bar.hover_item;
}

static void cab_init_menu_bar(void) {
    app_menu_init(&g_cabinet.menu_bar, APP_MENU_STYLE_CLASSIC_3D);

    int h0 = app_menu_add_header(&g_cabinet.menu_bar, "ファイル(F)", 104);
    app_menu_add_item(&g_cabinet.menu_bar, h0, "実身を開く (Open)", "Enter", CCMD_FILE_OPEN, TRUE);
    app_menu_add_item(&g_cabinet.menu_bar, h0, "実身を閲覧 (View)", "Space", CCMD_FILE_VIEW_TAD, TRUE);
    app_menu_add_item(&g_cabinet.menu_bar, h0, "新規実身の作成 (New)", "Ctrl+N", CCMD_FILE_NEW, TRUE);
    app_menu_add_separator(&g_cabinet.menu_bar, h0);
    app_menu_add_item(&g_cabinet.menu_bar, h0, "実身の複製 (Duplicate)", "Ctrl+D", CCMD_FILE_DUPLICATE, TRUE);
    app_menu_add_separator(&g_cabinet.menu_bar, h0);
    app_menu_add_item(&g_cabinet.menu_bar, h0, "閉じる (Close)", "Ctrl+W", CCMD_FILE_CLOSE, TRUE);

    int h1 = app_menu_add_header(&g_cabinet.menu_bar, "編集(E)", 72);
    app_menu_add_item(&g_cabinet.menu_bar, h1, "すべて選択 (Select All)", "Ctrl+A", CCMD_EDIT_SELECT_ALL, TRUE);
    app_menu_add_item(&g_cabinet.menu_bar, h1, "選択解除 (Deselect)", "Esc", CCMD_EDIT_DESELECT, TRUE);
    app_menu_add_separator(&g_cabinet.menu_bar, h1);
    app_menu_add_item(&g_cabinet.menu_bar, h1, "名称変更 (Rename)", "F2", CCMD_EDIT_RENAME, TRUE);
    app_menu_add_item(&g_cabinet.menu_bar, h1, "属性 (Properties...)", "Alt+Enter", CCMD_EDIT_PROPERTIES, TRUE);

    int h2 = app_menu_add_header(&g_cabinet.menu_bar, "表示(V)", 72);
    app_menu_add_item(&g_cabinet.menu_bar, h2, "一覧表示 (List View)", "Ctrl+1", CCMD_VIEW_LIST, TRUE);
    app_menu_add_item(&g_cabinet.menu_bar, h2, "アイコン表示 (Icon View)", "Ctrl+2", CCMD_VIEW_GRID, TRUE);
    app_menu_add_separator(&g_cabinet.menu_bar, h2);
    app_menu_add_item(&g_cabinet.menu_bar, h2, "名前順で整列 (Sort Name)", "F6", CCMD_VIEW_SORT_NAME, TRUE);
    app_menu_add_item(&g_cabinet.menu_bar, h2, "日付順で整列 (Sort Date)", "F7", CCMD_VIEW_SORT_DATE, TRUE);
    app_menu_add_item(&g_cabinet.menu_bar, h2, "サイズ順で整列 (Sort Size)", "F8", CCMD_VIEW_SORT_SIZE, TRUE);
    app_menu_add_item(&g_cabinet.menu_bar, h2, "種類順で整列 (Sort Type)", "F9", CCMD_VIEW_SORT_TYPE, TRUE);
    app_menu_add_item(&g_cabinet.menu_bar, h2, "再走査・更新 (Rescan)", "F5", CCMD_VIEW_REFRESH, TRUE);

    int h3 = app_menu_add_header(&g_cabinet.menu_bar, "仮身(O)", 72);
    app_menu_add_item(&g_cabinet.menu_bar, h3, "仮身リンク作成 (New Link)", "Ctrl+L", CCMD_VOBJ_CREATE_LINK, TRUE);
    app_menu_add_item(&g_cabinet.menu_bar, h3, "キャビネット移動 (Move...)", "", CCMD_VOBJ_MOVE, TRUE);
    app_menu_add_item(&g_cabinet.menu_bar, h3, "総索引 (Global Index)", "Ctrl+I", CCMD_VOBJ_GLOBAL_INDEX, TRUE);

    int h4 = app_menu_add_header(&g_cabinet.menu_bar, "ヘルプ(H)", 88);
    app_menu_add_item(&g_cabinet.menu_bar, h4, "キャビネット について (About)", "", CCMD_HELP_ABOUT, TRUE);
    app_menu_add_item(&g_cabinet.menu_bar, h4, "実身・仮身モデル解説 (Guide)", "", CCMD_HELP_GUIDE, TRUE);

    cab_sync_menu_state();
}

WND* open_vobj_about_window(void) {
    return app_menu_create_about_dialog("Cabinet", "実身・仮身",
                                        "Cleanroom BTRON Object Manager",
                                        "Brought to B-System by 5HT",
                                        280, 180);
}

static void paint_vobj_manager(WND *wnd, GDEV *dev) {
    if (!wnd || !dev) return;

    /* Background */
    RECT r = { 0, 0, dev->width, dev->height };
    fill_rec(dev, &r, COLOR_WHITE);

    /* ── 1. In-Window Application Menu Bar (y = 0..21) ────────────────────────── */
    if (g_cabinet.menu_bar.header_count == 0) cab_init_menu_bar();
    char count_buf[32];
    snprintf(count_buf, sizeof(count_buf), "%d 実身", g_cabinet.item_count);
    app_menu_set_right_text(&g_cabinet.menu_bar, count_buf);
    app_menu_paint_bar(&g_cabinet.menu_bar, dev);

    /* Quick View Mode Toggle Button on right */
    if (dev->width >= 560) {
        RECT view_btn = { 440, 1, 542, 20 };
        fill_rec(dev, &view_btn, COLOR_WHITE);
        drw_rec(dev, &view_btn);
        const char *v_lbl = (g_cabinet.view_mode == CAB_VIEW_LIST) ? "[一覧表示]" : "[アイコン表示]";
        drw_tc_string(dev, view_btn.left + 8, view_btn.top + 2, v_lbl, COLOR_BLUE, 0x00000000);
    }

    /* ── 2. Content Viewport (starts at y = 26) ────────────────────────────── */
    int start_y = 26;
    int client_h = dev->height - 48;
    int visible_rows = client_h / 22;
    if (visible_rows < 1) visible_rows = 1;

    if (g_cabinet.view_mode == CAB_VIEW_LIST) {
        for (int r_idx = 0; r_idx < visible_rows; r_idx++) {
            int item_idx = g_cabinet.scroll_offset + r_idx;
            if (item_idx >= g_cabinet.item_count) break;

            CABINET_ITEM *it = &g_cabinet.items[item_idx];
            int y = start_y + r_idx * 22;
            RECT row_r = { 4, y, dev->width - 6 - CAB_SBAR_W, y + 20 };

            BOOL is_sel = (g_cabinet.selected_idx == item_idx);
            BOOL is_hov = (g_cabinet.hovered_idx == item_idx);

            if (is_sel) {
                fill_rec(dev, &row_r, COLOR_NAVY);
            } else if (is_hov) {
                fill_rec(dev, &row_r, COLOR_LTGRAY);
            }

            COLOR txt_col = is_sel ? COLOR_WHITE : COLOR_BLACK;

            /* Icon badge */
            drw_tc_string(dev, 8, y + 2, (it->type == 1) ? "[文書]" : "[画像]", is_sel ? COLOR_WHITE : COLOR_BLUE, 0x00000000);

            /* Real Object Name */
            char name_str[80];
            snprintf(name_str, sizeof(name_str), "%s", it->name);
            drw_tc_string(dev, 60, y + 2, name_str, txt_col, 0x00000000);

            /* Real Object ID */
            char id_str[16];
            snprintf(id_str, sizeof(id_str), "#%d", it->robj_id);
            drw_tc_string(dev, dev->width - 150 - CAB_SBAR_W, y + 2, id_str, is_sel ? COLOR_WHITE : COLOR_DKGRAY, 0x00000000);

            /* Byte Size */
            char size_str[16];
            snprintf(size_str, sizeof(size_str), "%u B", it->size_bytes);
            drw_tc_string(dev, dev->width - 80 - CAB_SBAR_W, y + 2, size_str, is_sel ? COLOR_WHITE : COLOR_DKGRAY, 0x00000000);
        }
    } else {
        /* Grid Icon View */
        BTRON_ICON_SIZE sz = appearance_get_icon_size();
        int icon_dim = (sz == BTRON_ICON_SIZE_32) ? 32 : 64;
        int cols, col_w, row_h;
        cab_grid_metrics(dev->width, dev->height, &cols, &col_w, &row_h, NULL);

        for (int i = 0; i < g_cabinet.item_count; i++) {
            CABINET_ITEM *it = &g_cabinet.items[i];
            int col = i % cols;
            int r_idx = i / cols;
            if (r_idx < g_cabinet.grid_scroll_rows) continue;
            int x = 12 + col * col_w;
            int y = start_y + (r_idx - g_cabinet.grid_scroll_rows) * row_h;

            /* Draw the partially-visible row too and let the status bar (painted
             * afterwards) clip it at the bottom edge, Windows desktop style. */
            if (y > dev->height - 24) break;

            BOOL is_sel = (g_cabinet.selected_idx == i);
            RECT box = { x, y, x + col_w - 8, y + row_h - 4 };

            if (is_sel) {
                fill_rec(dev, &box, COLOR_NAVY);
            } else {
                fill_rec(dev, &box, COLOR_WHITE);
                drw_rec(dev, &box);
            }

            COLOR fg = is_sel ? COLOR_WHITE : COLOR_BLACK;

            /* Icon box */
            int icn_box_w = icon_dim + 8;
            int icn_box_h = icon_dim + 8;
            RECT icn = { x + (col_w - 8 - icn_box_w) / 2, y + 4,
                         x + (col_w - 8 + icn_box_w) / 2, y + 4 + icn_box_h };
            fill_rec(dev, &icn, is_sel ? COLOR_WHITE : COLOR_LTGRAY);
            drw_rec(dev, &icn);

            const char *icon_id = deduce_gif_icon(it);
            int icn_x = icn.left + 4;
            int icn_y = icn.top + 4;
            draw_setting_gif_icon_scaled(dev, icon_id, icn_x, icn_y, icon_dim, icon_dim);

            /* Truncated Name */
            char short_name[14];
            strncpy(short_name, it->name, 12);
            short_name[12] = '\0';
            H text_y = y + icn_box_h + 6;
            drw_tc_string(dev, x + 4, text_y, short_name, fg, 0x00000000);
        }
    }

    /* ── 3b. Natural vertical scrollbar (right gutter) ─────────────────────── */
    cab_draw_scrollbar(dev, dev->width, dev->height);

    /* ── 4. Status Bar Footer (Bottom: height-22 .. height) ────────────────── */
    RECT status_r = { 0, dev->height - 22, dev->width, dev->height };
    fill_rec(dev, &status_r, COLOR_LTGRAY);
    drw_lin(dev, 0, dev->height - 22, dev->width, dev->height - 22);

    char foot_text[128];
    const int sidx = g_cabinet.selected_idx;
    if (sidx >= 0 && sidx < g_cabinet.item_count) {
        snprintf(foot_text, sizeof(foot_text), "キャビネット: %d 実身 [tad_bin] | #%d: %s (%u B)",
                 g_cabinet.item_count,
                 g_cabinet.items[sidx].robj_id,
                 g_cabinet.items[sidx].name,
                 g_cabinet.items[sidx].size_bytes);
    } else {
        snprintf(foot_text, sizeof(foot_text), "キャビネット: %d 実身 [tad_bin] | 選択なし",
                 g_cabinet.item_count);
    }
    drw_tc_string(dev, 10, dev->height - 17, foot_text, COLOR_BLACK, 0x00000000);

    /* ── 5. Dropdown Menu Overlay ──────────────────────────────────────────── */
    if (g_cabinet.menu_bar.active_menu >= 0) {
        app_menu_paint_dropdown(&g_cabinet.menu_bar, dev);
    }
}

static void handle_vobj_manager_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;

    H rel_x = evt->pos.x - (wnd->bounds.left + 4);
    H rel_y = evt->pos.y - (wnd->bounds.top + 26);

    if (evt->type == EV_MOUSE_MOVE) {
        if (app_menu_handle_mouse_move(&g_cabinet.menu_bar, rel_x, rel_y)) {
            cab_sync_menu_state();
            return;
        }
        cab_sync_menu_state();

        /* Thumb drag: proportional to pointer travel. Only active when the
         * thumb itself was grabbed (EV_BUT_DOWN inside the thumb rect). */
        if (s_sbar_dragging) {
            H dw = wnd->dev ? wnd->dev->width : 560;
            H dh = wnd->dev ? wnd->dev->height : 360;
            cab_sbar_t sb;
            cab_sbar_layout(dw, dh, &sb);
            int total, vis, off;
            cab_scroll_get(dw, dh, &total, &vis, &off);
            int max_off = total - vis;
            if (max_off < 0) max_off = 0;
            int track_h = sb.track_bot - sb.track_top;
            int travel = track_h - (sb.thumb.bottom - sb.thumb.top);
            if (travel > 0 && max_off > 0) {
                int dy = (int)rel_y - (int)s_sbar_drag_y;
                cab_set_offset_clamped(s_sbar_drag_off + (dy * max_off) / travel, dw, dh);
                inval_wnd(wnd);
            }
            return;
        }

        /* Item Hover (starts at y = 26) */
        int start_y = 26;
        int idx = -1;
        if (g_cabinet.view_mode == CAB_VIEW_GRID) {
            H dev_w = wnd->dev ? wnd->dev->width : 560;
            H dev_h = wnd->dev ? wnd->dev->height : 360;
            int cols, col_w, row_h;
            cab_grid_metrics(dev_w, dev_h, &cols, &col_w, &row_h, NULL);
            if (rel_x < dev_w - CAB_SBAR_W) {   /* ignore the scrollbar gutter */
                int c = (rel_x - 12) / col_w;
                int r = g_cabinet.grid_scroll_rows + (rel_y - start_y) / row_h;
                if (c >= 0 && c < cols && rel_x >= 12 && rel_y >= start_y) {
                    int calc = r * cols + c;
                    if (calc < g_cabinet.item_count) idx = calc;
                }
            }
        } else {
            int row = (rel_y - start_y) / 22;
            int calc = g_cabinet.scroll_offset + row;
            if (calc >= 0 && calc < g_cabinet.item_count) idx = calc;
        }
        g_cabinet.hovered_idx = idx;

        /* Drag initiation check */
        if (s_cab_mouse_down && !btron_dnd_is_active()) {
            H dx = evt->pos.x - s_cab_down_x;
            H dy = evt->pos.y - s_cab_down_y;
            if (dx * dx + dy * dy >= 144) { /* > 5px drag threshold */
                if (g_cabinet.selected_idx >= 0 && g_cabinet.selected_idx < g_cabinet.item_count) {
                    CABINET_ITEM *it = &g_cabinet.items[g_cabinet.selected_idx];
                    btron_dnd_begin(wnd->id, it->robj_id, it->type, it->name, it->path, evt->pos.x, evt->pos.y);
                    snprintf(g_cabinet.status_msg, sizeof(g_cabinet.status_msg),
                             "Dragging: %s (%s)", it->name, (it->type == VOBJ_TYPE_DRAW) ? "Image" : "Document");
                }
            }
        }
        return;
    }

    if (evt->type == EV_BUT_DOWN) {
        /* Quick toggle button on right of Menu Bar */
        H dev_w = wnd->dev ? wnd->dev->width : 560;
        if (dev_w >= 560 && rel_y >= 0 && rel_y <= 21) {
            if (rel_x >= 440 && rel_x <= 542) {
                g_cabinet.view_mode = (g_cabinet.view_mode == CAB_VIEW_LIST) ? CAB_VIEW_GRID : CAB_VIEW_LIST;
                return;
            }
        }

        int cmd = 0, sub_idx = -1;
        if (app_menu_handle_mouse_down(&g_cabinet.menu_bar, rel_x, rel_y, &cmd, &sub_idx)) {
            cab_sync_menu_state();
            if (cmd != 0) {
                switch (cmd) {
                    case CCMD_FILE_OPEN:
                    case CCMD_FILE_VIEW_TAD:
                        if (g_cabinet.selected_idx >= 0 && g_cabinet.selected_idx < g_cabinet.item_count) {
                            CABINET_ITEM *it = &g_cabinet.items[g_cabinet.selected_idx];
                            cab_open_path(it->path, it->name);
                        }
                        return;
                    case CCMD_FILE_NEW:
                        if (open_t_editor_window) open_t_editor_window();
                        return;
                    case CCMD_FILE_CLOSE:
                        cls_wnd(wnd);
                        return;
                    case CCMD_EDIT_SELECT_ALL:
                        g_cabinet.selected_idx = 0;
                        return;
                    case CCMD_EDIT_DESELECT:
                        g_cabinet.selected_idx = -1;
                        return;
                    case CCMD_VIEW_LIST:
                        g_cabinet.view_mode = CAB_VIEW_LIST;
                        return;
                    case CCMD_VIEW_GRID:
                        g_cabinet.view_mode = CAB_VIEW_GRID;
                        return;
                    case CCMD_VIEW_SORT_NAME:
                        g_cabinet.sort_mode = 0;
                        cabinet_sort_items(&g_cabinet);
                        return;
                    case CCMD_VIEW_SORT_DATE:
                        g_cabinet.sort_mode = 1;
                        cabinet_sort_items(&g_cabinet);
                        return;
                    case CCMD_VIEW_SORT_SIZE:
                        g_cabinet.sort_mode = 2;
                        cabinet_sort_items(&g_cabinet);
                        return;
                    case CCMD_VIEW_SORT_TYPE:
                        g_cabinet.sort_mode = 3;
                        cabinet_sort_items(&g_cabinet);
                        return;
                    case CCMD_VIEW_REFRESH:
                        cabinet_init_defaults(&g_cabinet);
                        return;
                    case CCMD_HELP_ABOUT:
                        open_vobj_about_window();
                        return;
                    default:
                        return;
                }
            }
            return;
        }
        cab_sync_menu_state();

        /* C. Item Selection Click (starts at y = 26) */
        int start_y = 26;
        int idx = -1;
        H dev_w2 = wnd->dev ? wnd->dev->width : 560;
        H dev_h2 = wnd->dev ? wnd->dev->height : 360;

        /* Windows 95 scrollbar hit-test: arrow steps, track pages, thumb drags. */
        cab_sbar_t sb;
        cab_sbar_layout(dev_w2, dev_h2, &sb);
        if (rel_x >= sb.bar.left && rel_x < sb.bar.right &&
            rel_y >= sb.bar.top && rel_y < sb.bar.bottom) {
            if (sb.overflow) {
                int cur_off = (g_cabinet.view_mode == CAB_VIEW_GRID)
                                ? g_cabinet.grid_scroll_rows : g_cabinet.scroll_offset;
                if (rel_y >= sb.up_btn.top && rel_y < sb.up_btn.bottom) {
                    cab_set_offset_clamped(cur_off - 1, dev_w2, dev_h2);
                } else if (rel_y >= sb.dn_btn.top && rel_y < sb.dn_btn.bottom) {
                    cab_set_offset_clamped(cur_off + 1, dev_w2, dev_h2);
                } else if (rel_y < sb.thumb.top || rel_y >= sb.thumb.bottom) {
                    int total, vis, off;
                    cab_scroll_get(dev_w2, dev_h2, &total, &vis, &off);
                    int page = (vis > 1) ? (vis - 1) : 1;
                    cab_set_offset_clamped(cur_off + ((rel_y < sb.thumb.top) ? -page : page),
                                           dev_w2, dev_h2);
                } else {
                    s_sbar_dragging = TRUE;
                    s_sbar_drag_y = rel_y;
                    s_sbar_drag_off = cur_off;
                }
                inval_wnd(wnd);
            }
            return;
        }

        if (g_cabinet.view_mode == CAB_VIEW_GRID) {
            int cols, col_w, row_h;
            cab_grid_metrics(dev_w2, dev_h2, &cols, &col_w, &row_h, NULL);
            int c = (rel_x - 12) / col_w;
            int r = g_cabinet.grid_scroll_rows + (rel_y - start_y) / row_h;
            if (c >= 0 && c < cols && rel_x >= 12 && rel_y >= start_y) {
                int calc = r * cols + c;
                if (calc < g_cabinet.item_count) idx = calc;
            }
        } else {
            int row = (rel_y - start_y) / 22;
            int calc = g_cabinet.scroll_offset + row;
            if (calc >= 0 && calc < g_cabinet.item_count) idx = calc;
        }

        if (idx >= 0 && idx < g_cabinet.item_count) {
            s_cab_mouse_down = TRUE;
            s_cab_down_x = evt->pos.x;
            s_cab_down_y = evt->pos.y;
            static int s_last_click_idx = -1;
            static UW s_last_click_time = 0;
            UW cur_time = (UW)(uintptr_t)evt->data;
            if (cur_time == 0) {
#if BTRON_HOSTED
                struct timeval tv;
                gettimeofday(&tv, NULL);
                cur_time = (UW)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
#endif
            }
            BOOL is_double = (s_last_click_idx == idx && s_last_click_time != 0 && (cur_time - s_last_click_time < 500));

            g_cabinet.selected_idx = idx;

            if (is_double) {
                s_last_click_idx = -1;
                s_last_click_time = 0;
                CABINET_ITEM *it = &g_cabinet.items[idx];
                int path_len = strlen(it->path);
                BOOL is_tad = (path_len > 4 &&
                               (strcmp(it->path + path_len - 4, ".tad") == 0 ||
                                strcmp(it->path + path_len - 4, ".TAD") == 0));
                if (is_tad) {
                    open_tad_browser_window(it->path, it->name);
                } else {
                    cab_open_path(it->path, it->name);
                }
            } else {
                s_last_click_idx = idx;
                s_last_click_time = cur_time;
            }
        }
        return;
    }

    if (evt->type == EV_BUT_UP) {
        s_cab_mouse_down = FALSE;
        s_sbar_dragging = FALSE;
        return;
    }

    if (evt->type == EV_KEY_DOWN) {
        UW key = evt->key;
        if (key == BTRON_KEY_ESCAPE || key == 27) {
            int cmd = 0;
            if (app_menu_handle_key(&g_cabinet.menu_bar, key, (uint16_t)(uintptr_t)evt->data, &cmd)) {
                cab_sync_menu_state();
                return;
            }
            cab_sync_menu_state();
        }

        H dev_w = (wnd->dev) ? wnd->dev->width : 560;
        H dev_h = (wnd->dev) ? wnd->dev->height : 360;

        if (key == '\n' || key == '\r' || key == ' ' || key == BTRON_KEY_KP_ENTER) {
            if (g_cabinet.selected_idx >= 0 && g_cabinet.selected_idx < g_cabinet.item_count) {
                cab_open_path(g_cabinet.items[g_cabinet.selected_idx].path,
                              g_cabinet.items[g_cabinet.selected_idx].name);
            }
            return;
        }

        /* Mouse wheel arrives as PAGE_UP/PAGE_DOWN: move the viewport exactly
         * one unit per notch (no acceleration), without moving the selection. */
        if (key == BTRON_KEY_PAGE_UP || key == BTRON_KEY_PAGE_DOWN) {
            int cur = (g_cabinet.view_mode == CAB_VIEW_GRID)
                        ? g_cabinet.grid_scroll_rows : g_cabinet.scroll_offset;
            int step = (key == BTRON_KEY_PAGE_DOWN) ? 1 : -1;
            cab_set_offset_clamped(cur + step, dev_w, dev_h);
            inval_wnd(wnd);
            return;
        }

        if (g_cabinet.view_mode == CAB_VIEW_GRID) {
            cab_grid_navigate(key, dev_w, dev_h);
        } else {
            int visible_rows = (dev_h > CAB_GRID_TOP + 24) ? (dev_h - CAB_GRID_TOP - 24) / 22 : 1;
            if (visible_rows < 1) visible_rows = 1;
            int ns = (g_cabinet.selected_idx < 0) ? 0 : g_cabinet.selected_idx;
            if (g_cabinet.selected_idx < 0) {
                ns = 0;
            } else if (key == BTRON_KEY_UP || key == 'k') {
                ns = g_cabinet.selected_idx - 1;
            } else if (key == BTRON_KEY_DOWN || key == 'j') {
                ns = g_cabinet.selected_idx + 1;
            } else if (key == BTRON_KEY_HOME) {
                ns = 0;
            } else if (key == BTRON_KEY_END) {
                ns = g_cabinet.item_count - 1;
            } else if (key == BTRON_KEY_PAGE_UP) {
                ns = g_cabinet.selected_idx - visible_rows;
            } else if (key == BTRON_KEY_PAGE_DOWN) {
                ns = g_cabinet.selected_idx + visible_rows;
            } else {
                return;
            }
            if (ns < 0) ns = 0;
            if (ns > g_cabinet.item_count - 1) ns = g_cabinet.item_count - 1;
            g_cabinet.selected_idx = ns;
            if (ns < g_cabinet.scroll_offset) {
                g_cabinet.scroll_offset = ns;
            } else if (ns >= g_cabinet.scroll_offset + visible_rows) {
                g_cabinet.scroll_offset = ns - visible_rows + 1;
            }
            if (g_cabinet.scroll_offset < 0) g_cabinet.scroll_offset = 0;
        }
        inval_wnd(wnd);
    }
}

BOOL cabinet_handle_click(int mouse_x, int mouse_y, BOOL is_double_click, ID *out_robj_id, char *out_path) {
    (void)mouse_x;
    if (mouse_y >= 28 && mouse_y <= 52) {
        /* Toolbar click */
        if (mouse_x >= 275 && mouse_x <= 375) {
            g_cabinet.view_mode = (g_cabinet.view_mode == CAB_VIEW_LIST) ? CAB_VIEW_GRID : CAB_VIEW_LIST;
            return FALSE;
        } else if (mouse_x >= 380 && mouse_x <= 500) {
            cabinet_init_defaults(&g_cabinet);
            return FALSE;
        }
    }

    /* Keep this test-facing helper aligned with the native list viewport. */
    int start_y = 26;
    int row = (mouse_y - start_y) / 22;
    int idx = g_cabinet.scroll_offset + row;
    if (idx >= 0 && idx < g_cabinet.item_count) {
        g_cabinet.selected_idx = idx;
        if (out_robj_id) *out_robj_id = g_cabinet.items[idx].robj_id;
        if (out_path) strncpy(out_path, g_cabinet.items[idx].path, 127);

        if (is_double_click) {
            cab_open_path(g_cabinet.items[idx].path, g_cabinet.items[idx].name);
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL vobj_menu_open(WND *wnd) {
    (void)wnd;
    return (g_cabinet.menu_bar.active_menu >= 0) ? TRUE : FALSE;
}

WND* open_vobj_manager_window(void) {
    if (g_cabinet.item_count == 0) {
        cabinet_init_defaults(&g_cabinet);
    }
    WND *wnd = opn_wnd("実身キャビネット (Cabinet Explorer)", 80, 60, 560, 360,
                       WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_RESIZE | WND_ATTR_BORDER);
    if (wnd) {
        wnd->paint = paint_vobj_manager;
        wnd->event_handler = handle_vobj_manager_event;
        wnd->menu_open = vobj_menu_open;
    }
    return wnd;
}
