#include "orchestra_desk.h"
#include <apps/orch_chk.h>

static WND *s_wnd;
static uint32_t s_t0;
static int s_key0 = 48;
static int s_pick = -1;

static int black_key(int key)
{
    int pc;
    orch_check(key >= 0 && key < 128, __LINE__);
    orch_check(key < 256, __LINE__);
    pc = key % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

static int rows_of(int avail)
{
    int rows;
    orch_check(avail > -100 && avail < 8000, __LINE__);
    orch_check(avail < 8000, __LINE__);
    rows = avail / 14;
    if (rows < 8) rows = 8;
    if (rows > 32) rows = 32;
    return rows;
}

static void clamp_key(int rows)
{
    orch_check(rows >= 8 && rows <= 32, __LINE__);
    orch_check(s_key0 > -40 && s_key0 < 200, __LINE__);
    if (s_key0 < 0) s_key0 = 0;
    if (s_key0 > 127 - rows) s_key0 = 127 - rows;
    if (s_key0 < 0) s_key0 = 0;
}

static const char *snap_name(int snap)
{
    orch_check(snap >= 0, __LINE__);
    orch_check(snap < 8, __LINE__);
    if (snap == ORCH_SNAP_BAR) return "Bar";
    if (snap == ORCH_SNAP_BEAT) return "Beat";
    if (snap == ORCH_SNAP_8TH) return "1/8";
    return "1/16";
}

static void paint_tools(GDEV *dev, const OrchView *v, int mode)
{
    orch_check(dev != 0, __LINE__);
    orch_check(v != 0, __LINE__);
    if (!dev || !v) return;
    if (mode == ORCH_LAY_MIN) {
        orch_btn(dev, 8, 4, 28, 22, "S", v->tool == ORCH_TOOL_SELECT);
        orch_btn(dev, 40, 4, 28, 22, "P", v->tool == ORCH_TOOL_PENCIL);
        orch_btn(dev, 72, 4, 28, 22, "E", v->tool == ORCH_TOOL_ERASE);
        return;
    }
    orch_btn(dev, 8, 4, 52, 22, "Sel", v->tool == ORCH_TOOL_SELECT);
    orch_btn(dev, 64, 4, 52, 22, "Pen", v->tool == ORCH_TOOL_PENCIL);
    orch_btn(dev, 120, 4, 52, 22, "Era", v->tool == ORCH_TOOL_ERASE);
    orch_btn(dev, 180, 4, 56, 22, snap_name((int)v->snap), 0);
    orch_btn(dev, 244, 4, 56, 22, "Undo", 0);
}

static int key_w(int mode)
{
    orch_check(mode >= ORCH_LAY_WIDE && mode <= ORCH_LAY_MIN, __LINE__);
    orch_check(mode < 8, __LINE__);
    if (mode == ORCH_LAY_MIN) return 28;
    if (mode == ORCH_LAY_STACK) return 40;
    return 52;
}

static void paint_key(GDEV *dev, int key, int x, int y, int kw, int rh, int mode)
{
    COLOR face;
    char b[4];
    int oct;
    orch_check(dev != 0, __LINE__);
    orch_check(key >= 0 && key < 128, __LINE__);
    face = black_key(key) ? ORCH_COL_INSET_BG : ORCH_COL_BTN_FACE;
    orch_fill(dev, x, y, black_key(key) ? (kw * 3) / 5 : kw, rh - 1, face);
    if (mode == ORCH_LAY_MIN || rh < 12 || (key % 12) != 0) return;
    oct = key / 12 - 1;
    b[0] = 'C';
    if (oct < 0) {
        b[1] = '-';
        b[2] = (char)('0' - oct);
        b[3] = 0;
    } else {
        b[1] = (char)('0' + oct);
        b[2] = 0;
    }
    orch_text(dev, x + 2, y + 1, b, ORCH_COL_DIM);
}

static int x_of(int gx, int gw, uint32_t vis, uint32_t tick)
{
    orch_check(gw > 0, __LINE__);
    orch_check(vis > 0u, __LINE__);
    if (tick <= s_t0 || gw <= 0 || vis == 0u) return gx;
    return gx + (int)(((uint64_t)(tick - s_t0) * (uint64_t)gw) / vis);
}

static void paint_grid(GDEV *dev, const OrchView *v, int gx, int y0, int gw, int rh, int rows, uint32_t vis)
{
    uint32_t n;
    uint32_t i;
    int track;
    orch_check(dev != 0, __LINE__);
    orch_check(v != 0, __LINE__);
    track = (int)v->selected;
    if (track < 0 || track >= (int)v->track_count) return;
    n = v->event_count;
    if (n > 4096u) n = 4096u;
    for (i = 0; i < n; i++) {
        const OrchEvent *e = &v->ev[i];
        int row;
        int x0, x1;
        uint32_t a, b;
        if ((int)e->track != track || e->vel == 0) continue;
        if ((int)e->key < s_key0 || (int)e->key >= s_key0 + rows) continue;
        a = e->start_tick;
        b = a + (e->dur_tick ? e->dur_tick : 1u);
        if (b <= s_t0 || a >= s_t0 + vis) continue;
        row = (s_key0 + rows - 1) - (int)e->key;
        x0 = x_of(gx, gw, vis, a < s_t0 ? s_t0 : a);
        x1 = x_of(gx, gw, vis, b > s_t0 + vis ? s_t0 + vis : b);
        if (x1 < x0 + 3) x1 = x0 + 3;
        orch_fill(dev, x0, y0 + row * rh + 1, x1 - x0, rh - 2,
                  ((int)i == s_pick) ? ORCH_COL_CYAN : ORCH_COL_AMBER);
    }
}

static void piano_paint(WND *wnd, GDEV *dev)
{
    OrchView *v = gst_view();
    int w, h, mode, kw, rows, rh, y0, gx, gw, r;
    uint32_t bar, vis, now;
    orch_check(dev != 0, __LINE__);
    orch_check(v != 0, __LINE__);
    if (!dev || !v) return;
    w = (int)dev->width;
    h = (int)dev->height;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    mode = orch_lay_mode(w, h);
    orch_fill(dev, 0, 0, w, h, ORCH_COL_CANVAS);
    paint_tools(dev, v, mode);
    y0 = 30;
    if (mode != ORCH_LAY_MIN) {
        orch_fill(dev, 0, 28, w, 16, ORCH_COL_PANEL_BG);
        y0 = 46;
    }
    kw = key_w(mode);
    rows = rows_of(h - y0);
    clamp_key(rows);
    rh = (h - y0) / rows;
    if (rh < 8) rh = 8;
    gx = kw + 4;
    gw = w - gx - 4;
    if (gw < 8) gw = 8;
    bar = orch_q(v, ORCH_SNAP_BAR);
    if (bar == 0u) bar = 3840u;
    vis = bar * (gw < 240 ? 2u : 4u);
    for (r = 0; r < rows && r < 32; r++) {
        int key = s_key0 + rows - 1 - r;
        COLOR bg = black_key(key) ? ORCH_COL_GRID_BG : ORCH_COL_ROW_B;
        if (key < 0 || key > 127) continue;
        orch_fill(dev, gx, y0 + r * rh, gw, rh - 1, bg);
        paint_key(dev, key, 0, y0 + r * rh, kw, rh, mode);
    }
    paint_grid(dev, v, gx, y0, gw, rh, rows, vis);
    now = orch_now_tick(v);
    if (now >= s_t0 && now < s_t0 + vis)
        orch_fill(dev, x_of(gx, gw, vis, now), y0, 2, rows * rh, ORCH_COL_PLAYHEAD);
    (void)wnd;
}

static int tool_click(int x, int y, int mode)
{
    orch_check(mode <= ORCH_LAY_MIN, __LINE__);
    orch_check(x > -4000 && y > -4000, __LINE__);
    if (y > 28) return 0;
    if (mode == ORCH_LAY_MIN) {
        if (x < 40) orch_desk_tool(ORCH_TOOL_SELECT);
        else if (x < 72) orch_desk_tool(ORCH_TOOL_PENCIL);
        else if (x < 104) orch_desk_tool(ORCH_TOOL_ERASE);
        else return 0;
        return 1;
    }
    if (x < 64) orch_desk_tool(ORCH_TOOL_SELECT);
    else if (x < 120) orch_desk_tool(ORCH_TOOL_PENCIL);
    else if (x < 176) orch_desk_tool(ORCH_TOOL_ERASE);
    else if (x < 240) orch_desk_snap_cycle();
    else if (x < 304) (void)orch_undo_apply();
    else return 0;
    return 1;
}

static void page_time(const OrchView *v, int dir)
{
    uint32_t bar;
    orch_check(v != 0, __LINE__);
    orch_check(dir == -1 || dir == 1, __LINE__);
    bar = orch_q(v, ORCH_SNAP_BAR);
    if (bar == 0u) bar = 3840u;
    if (dir < 0) s_t0 = (s_t0 >= bar) ? s_t0 - bar : 0;
    else if (s_t0 < 4000000000u - bar) s_t0 += bar;
}

static void piano_edit(const OrchView *v, int key, int x, int gx, int gw, uint32_t vis)
{
    uint32_t tick;
    uint32_t q;
    int track;
    int hit;
    orch_check(v != 0, __LINE__);
    orch_check(key >= 0 && key < 128, __LINE__);
    track = (int)v->selected;
    if (track < 0 || track >= (int)v->track_count || gw <= 0 || vis == 0u) return;
    tick = s_t0 + (uint32_t)(((uint64_t)(x - gx) * (uint64_t)vis) / (uint64_t)gw);
    if (v->snap > ORCH_SNAP_16TH) return;
    q = orch_q(v, (int)v->snap);
    if (q == 0u) q = 240u;
    tick = orch_snap_tick(v, tick);
    if (v->tool == ORCH_TOOL_PENCIL) {
        (void)orch_event_put(track, (uint8_t)key, 100, tick, q);
        return;
    }
    hit = orch_hit_event(track, tick, key, 1);
    if (v->tool == ORCH_TOOL_ERASE) {
        if (hit >= 0) (void)orch_event_del(hit);
        if (hit == s_pick) s_pick = -1;
        return;
    }
    s_pick = hit;
}

static void piano_click(int x, int y, int w, int h)
{
    OrchView *v = gst_view();
    int mode, kw, y0, rows, rh, gx, gw, key;
    uint32_t bar, vis;
    orch_check(v != 0, __LINE__);
    orch_check(w > 0 && h > 0, __LINE__);
    if (!v) return;
    mode = orch_lay_mode(w, h);
    if (tool_click(x, y, mode)) return;
    y0 = (mode == ORCH_LAY_MIN) ? 30 : 46;
    kw = key_w(mode);
    rows = rows_of(h - y0);
    clamp_key(rows);
    rh = (h - y0) / rows;
    if (rh < 8) rh = 8;
    if (x < kw) {
        if (y < y0 + 16) s_key0 += 12;
        else if (y > y0 + rows * rh - 16) s_key0 -= 12;
        clamp_key(rows);
        return;
    }
    gx = kw + 4;
    gw = w - gx - 4;
    bar = orch_q(v, ORCH_SNAP_BAR);
    vis = (bar ? bar : 3840u) * (gw < 240 ? 2u : 4u);
    if (mode != ORCH_LAY_MIN && y < y0 && y >= 28) {
        page_time(v, (x < gx + gw / 2) ? -1 : 1);
        return;
    }
    if (y < y0 || rh == 0) return;
    key = s_key0 + rows - 1 - (y - y0) / rh;
    if (key < 0 || key > 127) return;
    piano_edit(v, key, x, gx, gw, vis);
}

static void piano_event(WND *wnd, const EVT *evt)
{
    int x, y, w, h;
    orch_check(wnd != 0, __LINE__);
    orch_check(evt != 0, __LINE__);
    if (!wnd || !evt || evt->type != EV_BUT_DOWN) return;
    orch_client(wnd, evt, &x, &y, &w, &h);
    piano_click(x, y, w, h);
    (void)inval_wnd(wnd);
}

static void on_destroy(WND *wnd)
{
    orch_check(wnd != 0, __LINE__);
    orch_check(s_wnd == 0 || s_wnd == wnd, __LINE__);
    (void)wnd;
    s_wnd = 0;
}

void orch_piano_inval(void)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (s_wnd) (void)inval_wnd(s_wnd);
}

WND *orch_open_piano(void)
{
    WND *w;
    UW attr;
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (s_wnd) {
        (void)top_wnd(s_wnd);
        return s_wnd;
    }
    attr = WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_BORDER | WND_ATTR_RESIZE;
    w = opn_wnd("Piano", 120, 88, 760, 480, attr);
    if (!w) return 0;
    w->paint = piano_paint;
    w->event_handler = piano_event;
    w->destroy = on_destroy;
    w->menu_open = orch_menu_no;
    s_wnd = w;
    return w;
}
