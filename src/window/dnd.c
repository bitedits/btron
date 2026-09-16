/*
 * B-System (BTRON 3.20) Direct Manipulation Drag-and-Drop Implementation
 */

#include <btron/dnd.h>
#include <btron/troncode.h>

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include <string.h>
#include <stdio.h>
#else
#include <stddef.h>
#define memset tkl_memset
#define memcpy tkl_memcpy
#define strlen tkl_strlen
#define strncpy tkl_strncpy
#endif

static BTRON_DND g_dnd;

void btron_dnd_init(void) {
    memset(&g_dnd, 0, sizeof(BTRON_DND));
}

void btron_dnd_begin(ID source_wndid, ID robj_id, VOBJ_TYPE type, const char *name, const char *path, H start_x, H start_y) {
    g_dnd.active = TRUE;
    g_dnd.source_wndid = source_wndid;
    g_dnd.robj_id = robj_id;
    g_dnd.type = type;
    if (name) {
        strncpy(g_dnd.name, name, sizeof(g_dnd.name) - 1);
        g_dnd.name[sizeof(g_dnd.name) - 1] = '\0';
    } else {
        g_dnd.name[0] = '\0';
    }
    if (path) {
        strncpy(g_dnd.path, path, sizeof(g_dnd.path) - 1);
        g_dnd.path[sizeof(g_dnd.path) - 1] = '\0';
    } else {
        g_dnd.path[0] = '\0';
    }
    g_dnd.start_x = start_x;
    g_dnd.start_y = start_y;
    g_dnd.drag_x = start_x;
    g_dnd.drag_y = start_y;
}

void btron_dnd_update(H x, H y) {
    if (g_dnd.active) {
        g_dnd.drag_x = x;
        g_dnd.drag_y = y;
    }
}

void btron_dnd_end(void) {
    g_dnd.active = FALSE;
}

BOOL btron_dnd_is_active(void) {
    return g_dnd.active;
}

const BTRON_DND* btron_dnd_get(void) {
    return &g_dnd;
}

void btron_dnd_render_ghost(GDEV *dev) {
    if (!dev || !g_dnd.active) return;

    int gx = g_dnd.drag_x + 14;
    int gy = g_dnd.drag_y + 14;
    int text_len = (int)strlen(g_dnd.name);
    int badge_w = 32 + text_len * 9;
    if (badge_w < 80) badge_w = 80;
    if (badge_w > 260) badge_w = 260;
    int badge_h = 24;

    /* Bounds check */
    if (gx + badge_w >= (int)dev->width) gx = dev->width - badge_w - 4;
    if (gy + badge_h >= (int)dev->height) gy = dev->height - badge_h - 4;

    /* Shadow */
    RECT shadow = { (H)(gx + 2), (H)(gy + 2), (H)(gx + badge_w + 2), (H)(gy + badge_h + 2) };
    fill_rec(dev, &shadow, (COLOR)0x40000000);

    /* Badge Background (Soft Ice Blue / Cream) */
    RECT bg = { (H)gx, (H)gy, (H)(gx + badge_w), (H)(gy + badge_h) };
    COLOR bg_col = (g_dnd.type == VOBJ_TYPE_DRAW) ? (COLOR)0xFFFFF6E8 : (COLOR)0xFFEDF5FF;
    fill_rec(dev, &bg, bg_col);

    /* Outline */
    set_col(dev, (COLOR)0xFF003366, COLOR_WHITE);
    drw_rec(dev, &bg);

    /* Icon prefix badge */
    RECT icon_box = { (H)(gx + 3), (H)(gy + 3), (H)(gx + 18), (H)(gy + badge_h - 3) };
    COLOR icon_bg = (g_dnd.type == VOBJ_TYPE_DRAW) ? (COLOR)0xFFCC6600 : (COLOR)0xFF0055AA;
    fill_rec(dev, &icon_box, icon_bg);

    /* Draw text title inside ghost badge */
    drw_tc_string(dev, (H)(gx + 22), (H)(gy + 4), g_dnd.name, (COLOR)0xFF001133, 0x00000000);
}
