#include "orchestra_desk.h"
#include <apps/orch_chk.h>

static WND *s_wnd;
static uint32_t s_t0;
static int s_pick = -1;

static const char *snap_name(int snap)
{
    orch_check(snap >= 0, __LINE__);
    orch_check(snap < 8, __LINE__);
    if (snap == ORCH_SNAP_BAR) return "Bar";
    if (snap == ORCH_SNAP_BEAT) return "Beat";
    if (snap == ORCH_SNAP_8TH) return "1/8";
    return "1/16";
}

static int head_w(int mode, int w)
{
    orch_check(mode >= ORCH_LAY_WIDE && mode <= ORCH_LAY_MIN, __LINE__);
    orch_check(w > 0 && w < 8000, __LINE__);
    if (w < 280 || mode == ORCH_LAY_MIN) return 0;
    if (mode == ORCH_LAY_STACK) return 88;
    return 120;
}

static int lane_n(int mode, int avail)
{
    int n = 1;
    orch_check(mode >= ORCH_LAY_WIDE && mode <= ORCH_LAY_MIN, __LINE__);
    orch_check(avail > -100 && avail < 8000, __LINE__);
    if (mode == ORCH_LAY_WIDE) n = avail / 36;
    else if (mode == ORCH_LAY_STACK && avail >= 140) n = 3;
    if (n < 1) n = 1;
    if (n > 5) n = 5;
    return n;
}

static int lane_track(const OrchView *v, int lane, int lanes)
{
    int sel;
    int t;
    orch_check(v != 0, __LINE__);
    orch_check(lane >= 0 && lanes > 0 && lanes <= 5, __LINE__);
    if (!v || v->track_count == 0) return -1;
    sel = (int)v->selected;
    if (sel < 0 || sel >= (int)v->track_count) sel = 0;
    t = sel - lanes / 2 + lane;
    if (t < 0 || t >= (int)v->track_count) return -1;
    return t;
}

static void paint_tools(GDEV *dev, const OrchView *v, int w, int mode)
{
    const char *sn;
    orch_check(dev != 0, __LINE__);
    orch_check(v != 0, __LINE__);
    if (!dev || !v) return;
    sn = snap_name((int)v->snap);
    if (mode == ORCH_LAY_MIN) {
        orch_btn(dev, 8, 4, 28, 24, "S", v->tool == ORCH_TOOL_SELECT);
        orch_btn(dev, 40, 4, 28, 24, "P", v->tool == ORCH_TOOL_PENCIL);
        orch_btn(dev, 72, 4, 28, 24, "E", v->tool == ORCH_TOOL_ERASE);
        if (w >= 180) orch_btn(dev, 108, 4, 56, 24, sn, 0);
        return;
    }
    orch_btn(dev, 8, 4, 56, 24, "Sel", v->tool == ORCH_TOOL_SELECT);
    orch_btn(dev, 68, 4, 56, 24, "Pen", v->tool == ORCH_TOOL_PENCIL);
    orch_btn(dev, 128, 4, 56, 24, "Era", v->tool == ORCH_TOOL_ERASE);
    orch_btn(dev, 192, 4, 64, 24, sn, 0);
    orch_btn(dev, 264, 4, 64, 24, "Undo", 0);
}

static int x_of(int gx, int gw, uint32_t t0, uint32_t vis, uint32_t tick)
{
    uint64_t rel;
    orch_check(gw > 0 && gw < 8000, __LINE__);
    orch_check(vis > 0u, __LINE__);
    if (gw <= 0 || vis == 0u || tick <= t0) return gx;
    rel = (uint64_t)(tick - t0) * (uint64_t)gw;
    return gx + (int)(rel / vis);
}

static void paint_notes(GDEV *dev, const OrchView *v, int track, int gx, int y, int gw, int lh, uint32_t t0, uint32_t vis)
{
    uint32_t n;
    uint32_t i;
    int map;
    orch_check(dev != 0, __LINE__);
    orch_check(v != 0, __LINE__);
    if (!dev || !v || track < 0 || gw < 4) return;
    map = lh >= 48;
    n = v->event_count;
    if (n > 4096u) n = 4096u;
    for (i = 0; i < n; i++) {
        const OrchEvent *e = &v->ev[i];
        uint32_t a;
        uint32_t b;
        int x0, x1, y0, hh;
        COLOR col;
        if ((int)e->track != track || e->vel == 0) continue;
        a = e->start_tick;
        b = a + (e->dur_tick ? e->dur_tick : 1u);
        if (b <= t0 || a >= t0 + vis) continue;
        x0 = x_of(gx, gw, t0, vis, a < t0 ? t0 : a);
        x1 = x_of(gx, gw, t0, vis, b > t0 + vis ? t0 + vis : b);
        if (x1 < x0 + 2) x1 = x0 + 2;
        if (map) {
            hh = lh / 16;
            if (hh < 3) hh = 3;
            y0 = y + ((127 - (int)e->key) * (lh - hh)) / 127;
        } else {
            y0 = y + 4;
            hh = lh - 8;
            if (hh < 4) hh = 4;
        }
        col = ((int)i == s_pick) ? ORCH_COL_CYAN : ORCH_COL_AMBER;
        orch_fill(dev, x0, y0, x1 - x0, hh, col);
    }
}

static void paint_lane(GDEV *dev, const OrchView *v, int track, int x, int y, int head, int gw, int lh, uint32_t t0, uint32_t vis, int sel)
{
    COLOR bg;
    uint32_t bar;
    uint32_t t;
    int k;
    orch_check(dev != 0, __LINE__);
    orch_check(v != 0, __LINE__);
    if (!dev || !v) return;
    bg = sel ? ORCH_COL_ROW_A : ORCH_COL_ROW_B;
    orch_fill(dev, x, y, head + gw, lh - 1, bg);
    if (head > 8 && track >= 0) {
        orch_text(dev, x + 4, y + 4, v->trk[track].name, ORCH_COL_WHITE);
        if (v->trk[track].accent)
            orch_fill(dev, x, y, 3, lh - 1, (COLOR)v->trk[track].accent);
    }
    if (track < 0) return;
    bar = orch_q(v, ORCH_SNAP_BAR);
    if (bar == 0u) bar = 3840u;
    for (k = 0; k < 32; k++) {
        t = t0 + (uint32_t)k * bar;
        if (t > t0 && t < t0 + vis)
            orch_fill(dev, x_of(x + head, gw, t0, vis, t), y, 1, lh - 1, ORCH_COL_GRID_LINE);
    }
    paint_notes(dev, v, track, x + head, y, gw, lh, t0, vis);
}

static void paint_head(GDEV *dev, const OrchView *v, int w, int h)
{
    int mode;
    int head;
    int top;
    int lanes;
    int lh;
    int i;
    int gx;
    int gw;
    uint32_t vis;
    uint32_t bar;
    uint32_t now;
    int px;
    orch_check(dev != 0, __LINE__);
    orch_check(v != 0, __LINE__);
    if (!dev || !v) return;
    mode = orch_lay_mode(w, h);
    paint_tools(dev, v, w, mode);
    top = (mode == ORCH_LAY_MIN) ? 32 : 52;
    head = head_w(mode, w);
    lanes = lane_n(mode, h - top);
    lh = (h - top) / lanes;
    if (lh < 24) lh = 24;
    gx = head;
    gw = w - head - 8;
    if (gw < 8) gw = 8;
    bar = orch_q(v, ORCH_SNAP_BAR);
    if (bar == 0u) bar = 3840u;
    vis = bar * 4u;
    if (gw < 240) vis = bar * 2u;
    if (mode != ORCH_LAY_MIN) {
        orch_fill(dev, gx, 32, gw, 18, ORCH_COL_PANEL_BG);
        for (i = 0; i < 8; i++) {
            uint32_t t = s_t0 + (uint32_t)i * bar;
            char nb[8];
            if (t >= s_t0 + vis) break;
            (void)orch_fmt_u(nb, 8, (t / bar) + 1u);
            orch_text(dev, x_of(gx, gw, s_t0, vis, t) + 2, 34, nb, ORCH_COL_DIM);
        }
    }
    for (i = 0; i < lanes && i < 5; i++) {
        int tr = lane_track(v, i, lanes);
        int sel = (tr >= 0 && tr == (int)v->selected);
        paint_lane(dev, v, tr, 0, top + i * lh, head, gw, lh, s_t0, vis, sel);
    }
    now = orch_now_tick(v);
    if (now >= s_t0 && now < s_t0 + vis) {
        px = x_of(gx, gw, s_t0, vis, now);
        orch_fill(dev, px, top, 2, lanes * lh, ORCH_COL_PLAYHEAD);
    }
}

static void arrange_paint(WND *wnd, GDEV *dev)
{
    int w;
    int h;
    OrchView *v = gst_view();
    orch_check(dev != 0, __LINE__);
    orch_check(v != 0, __LINE__);
    if (!dev || !v) return;
    w = (int)dev->width;
    h = (int)dev->height;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    orch_fill(dev, 0, 0, w, h, ORCH_COL_CANVAS);
    paint_head(dev, v, w, h);
    (void)wnd;
}

static int tool_click(const OrchView *v, int x, int y, int mode)
{
    orch_check(v != 0, __LINE__);
    orch_check(mode >= ORCH_LAY_WIDE && mode <= ORCH_LAY_MIN, __LINE__);
    if (y > 30) return 0;
    if (mode == ORCH_LAY_MIN) {
        if (orch_inside(x, y, 8, 4, 28, 24)) orch_desk_tool(ORCH_TOOL_SELECT);
        else if (orch_inside(x, y, 40, 4, 28, 24)) orch_desk_tool(ORCH_TOOL_PENCIL);
        else if (orch_inside(x, y, 72, 4, 28, 24)) orch_desk_tool(ORCH_TOOL_ERASE);
        else if (orch_inside(x, y, 108, 4, 56, 24)) orch_desk_snap_cycle();
        else return 0;
        return 1;
    }
    if (orch_inside(x, y, 8, 4, 56, 24)) orch_desk_tool(ORCH_TOOL_SELECT);
    else if (orch_inside(x, y, 68, 4, 56, 24)) orch_desk_tool(ORCH_TOOL_PENCIL);
    else if (orch_inside(x, y, 128, 4, 56, 24)) orch_desk_tool(ORCH_TOOL_ERASE);
    else if (orch_inside(x, y, 192, 4, 64, 24)) orch_desk_snap_cycle();
    else if (orch_inside(x, y, 264, 4, 64, 24)) (void)orch_undo_apply();
    else return 0;
    return 1;
}

static void ruler_page(const OrchView *v, int x, int gx, int gw)
{
    uint32_t bar;
    orch_check(v != 0, __LINE__);
    orch_check(gw > 0, __LINE__);
    bar = orch_q(v, ORCH_SNAP_BAR);
    if (bar == 0u) bar = 3840u;
    if (x < gx + gw / 3) {
        if (s_t0 >= bar) s_t0 -= bar;
        else s_t0 = 0;
    } else if (x > gx + (2 * gw) / 3) {
        if (s_t0 < 4000000000u - bar) s_t0 += bar;
    }
}

static int key_at(int y, int ly, int lh)
{
    int k;
    orch_check(lh > 0 && lh < 8000, __LINE__);
    orch_check(y > -4000, __LINE__);
    if (lh < 48) return 60;
    k = 127 - ((y - ly) * 128) / lh;
    if (k < 0) k = 0;
    if (k > 127) k = 127;
    return k;
}

static void lane_edit(const OrchView *v, int track, int x, int y, int gx, int gy, int gw, int lh, uint32_t vis)
{
    uint32_t tick;
    uint32_t q;
    int key;
    int hit;
    uint64_t rel;
    orch_check(v != 0, __LINE__);
    orch_check(track >= 0 && track < 64, __LINE__);
    if (!v || gw <= 0 || vis == 0u) return;
    rel = (uint64_t)(x - gx) * (uint64_t)vis;
    tick = s_t0 + (uint32_t)(rel / (uint64_t)gw);
    if (v->snap > ORCH_SNAP_16TH) return;
    q = orch_q(v, (int)v->snap);
    if (q == 0u) q = 240u;
    tick = orch_snap_tick(v, tick);
    key = key_at(y, gy, lh);
    orch_desk_sel(track);
    if (v->tool == ORCH_TOOL_PENCIL) {
        (void)orch_event_put(track, (uint8_t)key, 100, tick, q);
        return;
    }
    hit = orch_hit_event(track, tick, key, lh >= 48);
    if (v->tool == ORCH_TOOL_ERASE) {
        if (hit >= 0) (void)orch_event_del(hit);
        if (hit == s_pick) s_pick = -1;
        return;
    }
    s_pick = hit;
}

static void arrange_click(int x, int y, int w, int h)
{
    OrchView *v = gst_view();
    int mode;
    int head;
    int top;
    int lanes;
    int lh;
    int gw;
    int i;
    uint32_t bar;
    uint32_t vis;
    orch_check(v != 0, __LINE__);
    orch_check(w > 0 && h > 0, __LINE__);
    if (!v) return;
    mode = orch_lay_mode(w, h);
    if (tool_click(v, x, y, mode)) return;
    head = head_w(mode, w);
    top = (mode == ORCH_LAY_MIN) ? 32 : 52;
    if (mode != ORCH_LAY_MIN && orch_inside(x, y, head, 32, w - head, 18)) {
        ruler_page(v, x, head, w - head - 8);
        return;
    }
    lanes = lane_n(mode, h - top);
    lh = (h - top) / lanes;
    if (lh < 24) lh = 24;
    gw = w - head - 8;
    if (gw < 8) gw = 8;
    bar = orch_q(v, ORCH_SNAP_BAR);
    vis = (bar ? bar : 3840u) * ((gw < 240) ? 2u : 4u);
    for (i = 0; i < lanes && i < 5; i++) {
        int tr = lane_track(v, i, lanes);
        int ly = top + i * lh;
        if (tr < 0) continue;
        if (!orch_inside(x, y, head, ly, gw, lh)) continue;
        lane_edit(v, tr, x, y, head, ly, gw, lh, vis);
        return;
    }
}

static void arrange_event(WND *wnd, const EVT *evt)
{
    int x, y, w, h;
    orch_check(wnd != 0, __LINE__);
    orch_check(evt != 0, __LINE__);
    if (!wnd || !evt || evt->type != EV_BUT_DOWN) return;
    orch_client(wnd, evt, &x, &y, &w, &h);
    arrange_click(x, y, w, h);
    (void)inval_wnd(wnd);
}

static void on_destroy(WND *wnd)
{
    orch_check(wnd != 0, __LINE__);
    orch_check(s_wnd == 0 || s_wnd == wnd, __LINE__);
    (void)wnd;
    s_wnd = 0;
}

void orch_arrange_inval(void)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (s_wnd) (void)inval_wnd(s_wnd);
}

WND *orch_open_arrange(void)
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
    w = opn_wnd("Arrange", 96, 72, 760, 420, attr);
    if (!w) return 0;
    w->paint = arrange_paint;
    w->event_handler = arrange_event;
    w->destroy = on_destroy;
    w->menu_open = orch_menu_no;
    s_wnd = w;
    return w;
}
