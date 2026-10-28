#include "orchestra_desk.h"
#include <apps/orch_chk.h>

static WND *s_wnd;
static uint32_t s_t0;
static int s_pick = -1;
static int s_zoom = 3;
static int s_row;
static int s_seen_n = -1;
static int s_seen_sel = -2;
static int s_seen_lo = -1;
static int s_seen_hi = -1;
static int s_hold;
static int s_drag;
static int s_anchor;
static int s_anchor_row;
static uint32_t s_anchor_t0;
static uint32_t s_home;
static uint8_t s_seen_state;

enum { SB = 16, DRAG_OFF = 0, DRAG_V = 1, DRAG_H = 2 };

typedef struct ArrGeom {
    int mode;
    int w;
    int h;
    int head;
    int top;
    int lh;
    int lanes;
    int gx;
    int gw;
    int body;
    int vx;
    int hy;
    int tracks;
    int vmax;
    uint32_t bar;
    uint32_t vis;
    uint32_t end;
} ArrGeom;

static const char *zoom_label(void)
{
    static const char *lb[6] = { "32", "16", "8", "4", "2", "1" };
    orch_check(s_zoom >= 0 && s_zoom <= 5, __LINE__);
    orch_check(lb[3][0] == '4', __LINE__);
    if (s_zoom < 0 || s_zoom > 5) return "4";
    return lb[s_zoom];
}

static void zoom_by(int dir)
{
    orch_check(dir == -1 || dir == 1, __LINE__);
    orch_check(s_zoom >= 0 && s_zoom <= 5, __LINE__);
    if (dir < 0 && s_zoom > 0) s_zoom--;
    if (dir > 0 && s_zoom < 5) s_zoom++;
}

static int zoom_bars(int gw)
{
    int bars = 32 >> s_zoom;
    orch_check(s_zoom >= 0 && s_zoom <= 5, __LINE__);
    orch_check(gw > -10 && gw < 8000, __LINE__);
    if (bars < 1) bars = 1;
    if (gw < 240 && bars > 2) bars = 2;
    return bars;
}

static void name_fit(char *dst, int cap, const char *src, int px)
{
    int i = 0;
    int n = 0;
    int w = 0;
    orch_check(dst != 0, __LINE__);
    orch_check(cap >= 2 && cap <= 48, __LINE__);
    dst[0] = 0;
    if (!src || px < 8) return;
    while (src[n] && i < cap - 1 && n < 47) {
        unsigned char c = (unsigned char)src[n];
        int adv = 8;
        int step = 1;
        if (c >= 0x80u) {
            adv = 16;
            while (step < 3 && src[n + step] != 0) {
                if (((unsigned char)src[n + step] & 0xC0u) != 0x80u) break;
                step++;
            }
        }
        if (w + adv > px) break;
        while (step > 0 && i < cap - 1) {
            dst[i] = src[n];
            i++;
            n++;
            step--;
        }
        w += adv;
    }
    dst[i] = 0;
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

static int head_w(int mode, int w)
{
    int h = 200;
    orch_check(mode >= ORCH_LAY_WIDE && mode <= ORCH_LAY_MIN, __LINE__);
    orch_check(w > 0 && w < 8000, __LINE__);
    if (w < 280 || mode == ORCH_LAY_MIN) return 0;
    if (mode == ORCH_LAY_STACK) h = 112;
    if (h > w / 2) h = w / 2;
    if (h < 48) h = 48;
    return h;
}

static void lane_fit(int mode, int avail, int n, int *lh, int *lanes)
{
    int base;
    int cap;
    int fit;
    orch_check(lh != 0 && lanes != 0, __LINE__);
    orch_check(n >= 0 && n <= ORCH_MAX_TRACKS, __LINE__);
    if (!lh || !lanes) return;
    base = (mode == ORCH_LAY_MIN) ? 24 : 36;
    cap = (mode == ORCH_LAY_MIN) ? 28 : 64;
    if (avail < 8) avail = 8;
    fit = avail / base;
    if (fit < 1) fit = 1;
    if (fit > ORCH_MAX_TRACKS) fit = ORCH_MAX_TRACKS;
    *lh = base;
    if (n == 0) fit = 1;
    else if (fit > n) {
        int grown = avail / n;
        fit = n;
        if (grown > cap) grown = cap;
        if (grown < base) grown = base;
        *lh = grown;
    }
    *lanes = fit;
}

static uint32_t span_end(const OrchView *v, uint32_t bar)
{
    uint32_t n;
    uint32_t i;
    uint32_t end;
    uint32_t now;
    orch_check(v != 0, __LINE__);
    orch_check(bar > 0u, __LINE__);
    if (!v || bar == 0u) return 3840u;
    end = bar;
    n = v->event_count;
    if (n > ORCH_MAX_EVENTS) n = ORCH_MAX_EVENTS;
    for (i = 0; i < n; i++) {
        const OrchEvent *e = &v->ev[i];
        uint32_t a = e->start_tick;
        uint32_t d = e->dur_tick;
        uint32_t b;
        if (d == 0u) d = 1u;
        b = (a > 0xfff00000u - d) ? 0xfff00000u : a + d;
        if (b > end) end = b;
    }
    now = orch_now_tick(v);
    if (now > end) end = now;
    if (end < 0xfff00000u - bar) end += bar;
    return end;
}

static void cmd_auto_scale(const OrchView *v)
{
    uint32_t bar, end, duration_ticks, bars_needed;
    orch_check(v != 0, __LINE__);
    if (!v) return;
    bar = orch_q(v, ORCH_SNAP_BAR);
    if (bar == 0u) bar = 3840u;
    end = span_end(v, bar);
    duration_ticks = (end > 0u) ? end : bar;
    bars_needed = (duration_ticks + bar - 1u) / bar;
    if (bars_needed <= 1u) s_zoom = 5;
    else if (bars_needed <= 2u) s_zoom = 4;
    else if (bars_needed <= 4u) s_zoom = 3;
    else if (bars_needed <= 8u) s_zoom = 2;
    else if (bars_needed <= 16u) s_zoom = 1;
    else s_zoom = 0;
    s_t0 = 0;
    s_row = 0;
    s_hold = 0;
    s_pick = -1;
}

static void geom_of(const OrchView *v, int w, int h, ArrGeom *g)
{
    int n, avail, lh, lanes, body, head, mode, top, vx, gw, hy;
    uint32_t bar, vis;
    orch_check(v != 0, __LINE__);
    orch_check(g != 0, __LINE__);
    if (!v || !g || w < 1 || h < 1) return;
    mode = orch_lay_mode(w, h);
    head = head_w(mode, w);
    top = (mode == ORCH_LAY_MIN) ? 32 : 52;
    n = (int)v->track_count;
    if (n < 0) n = 0;
    if (n > ORCH_MAX_TRACKS) n = ORCH_MAX_TRACKS;
    avail = h - top - SB - 2;
    lane_fit(mode, avail, n, &lh, &lanes);
    body = lanes * lh;
    if (body > avail && avail > 8) {
        body = avail;
        lh = avail;
        lanes = 1;
    }
    if (lh < 8) lh = 8;
    if (body < lh) body = lh;
    hy = h - SB;
    if (hy < top + body) hy = top + body;
    body = hy - top;
    if (body < lh) body = lh;
    vx = w - SB - 2;
    if (vx < head + 4) vx = head + 4;
    gw = vx - head - 2;
    if (gw < 8) gw = 8;
    bar = orch_q(v, ORCH_SNAP_BAR);
    if (bar == 0u) bar = 3840u;
    vis = bar * (uint32_t)zoom_bars(gw);
    if (vis == 0u) vis = bar;
    g->mode = mode;
    g->w = w;
    g->h = h;
    g->head = head;
    g->top = top;
    g->lh = lh;
    g->lanes = lanes;
    g->tracks = n;
    g->body = body;
    g->vx = vx;
    g->gx = head;
    g->gw = gw;
    g->hy = hy;
    g->bar = bar;
    g->vis = vis;
    g->vmax = (n > lanes) ? n - lanes : 0;
    g->end = span_end(v, bar);
    orch_check(lanes >= 1 && lanes <= ORCH_MAX_TRACKS, __LINE__);
    orch_check(vis > 0u, __LINE__);
}

static void row_reveal(const ArrGeom *g, int sel)
{
    int lanes;
    int vmax;
    orch_check(g != 0, __LINE__);
    orch_check(sel >= 0 && sel < ORCH_MAX_TRACKS, __LINE__);
    if (!g) return;
    lanes = g->lanes;
    vmax = g->vmax;
    if (sel < s_row || sel >= s_row + lanes) {
        s_row = sel;
        if (lanes > 1) s_row = sel - lanes / 2;
    }
    if (s_row < 0) s_row = 0;
    if (s_row > vmax) s_row = vmax;
}

static void row_sync(const OrchView *v, const ArrGeom *g)
{
    int sel, n, lo, hi, dirty, vmax;
    orch_check(v != 0, __LINE__);
    orch_check(g != 0, __LINE__);
    if (!v || !g) return;
    n = g->tracks;
    vmax = g->vmax;
    sel = (int)v->selected;
    if (sel < 0 || sel >= n) sel = (n > 0) ? 0 : -1;
    lo = -1;
    hi = -1;
    if (sel >= 0 && sel < n) {
        const OrchTrack *t = &v->trk[sel];
        lo = (int)t->key_lo;
        hi = (int)t->key_hi;
    }
    dirty = (n != s_seen_n || sel != s_seen_sel || lo != s_seen_lo || hi != s_seen_hi);
    s_seen_n = n;
    s_seen_sel = sel;
    s_seen_lo = lo;
    s_seen_hi = hi;
    if (dirty && sel >= 0) row_reveal(g, sel);
    if (s_row < 0) s_row = 0;
    if (s_row > vmax) s_row = vmax;
}

static void take_home(const OrchView *v)
{
    uint32_t h;
    orch_check(v != 0, __LINE__);
    orch_check(s_pick >= -1 && s_pick < 4096, __LINE__);
    if (!v) return;
    h = v->home_gen;
    if (h == s_home) return;
    s_home = h;
    s_t0 = 0;
    s_row = 0;
    s_hold = 0;
    s_pick = -1;
    s_drag = 0;
    s_seen_n = -1;
    s_seen_sel = -2;
    s_seen_lo = -1;
    s_seen_hi = -1;
    cmd_auto_scale(v);
}

static void time_follow(const OrchView *v, const ArrGeom *g)
{
    uint32_t now;
    uint32_t max0;
    uint32_t vis;
    uint32_t end;
    int chasing;
    int k;
    orch_check(v != 0, __LINE__);
    orch_check(g != 0, __LINE__);
    if (!v || !g) return;
    take_home(v);
    vis = g->vis;
    end = g->end;
    if (vis == 0u) return;
    if (v->state == ORCH_GST_PLAYING && s_seen_state != ORCH_GST_PLAYING)
        s_hold = 0;
    s_seen_state = v->state;
    chasing = (v->state == ORCH_GST_PLAYING && !s_hold && s_drag != DRAG_H);
    if (!chasing) {
        max0 = (end > vis) ? end - vis : 0u;
        if (s_t0 > max0) s_t0 = max0;
        return;
    }
    now = orch_now_tick(v);
    for (k = 0; k < 256 && now < s_t0; k++) {
        if (s_t0 <= vis) {
            s_t0 = 0;
            break;
        }
        s_t0 -= vis;
    }
    for (k = 0; k < 256 && now >= s_t0 + vis; k++) {
        if (s_t0 > 0xfff00000u - vis) break;
        s_t0 += vis;
    }
}

static int thumb_v(const ArrGeom *g, int *y, int *h)
{
    int body, lanes, span, th, vmax, top;
    orch_check(g != 0, __LINE__);
    orch_check(y != 0 && h != 0, __LINE__);
    if (!g || !y || !h) return 0;
    body = g->body;
    lanes = g->lanes;
    top = g->top;
    vmax = g->vmax;
    *y = top;
    *h = (body > 0) ? body : 1;
    if (body < 1 || lanes < 1) return 0;
    span = g->tracks;
    if (span < 1) span = 1;
    th = (body * lanes) / span;
    if (th < 12) th = 12;
    if (th > body) th = body;
    *h = th;
    if (vmax <= 0) return 0;
    *y = top + ((body - th) * s_row) / vmax;
    return 1;
}

static int thumb_h(const ArrGeom *g, int *x, int *w)
{
    uint32_t end, vis, max0;
    int gw, th, gx;
    orch_check(g != 0, __LINE__);
    orch_check(x != 0 && w != 0, __LINE__);
    if (!g || !x || !w) return 0;
    end = g->end;
    vis = g->vis;
    gw = g->gw;
    gx = g->gx;
    if (vis == 0u) vis = 1u;
    if (end < vis) end = vis;
    th = (int)(((uint64_t)gw * (uint64_t)vis) / end);
    if (th < 12) th = 12;
    if (th > gw) th = gw;
    *w = th;
    *x = gx;
    max0 = end - vis;
    if (max0 == 0u) return 0;
    *x = gx + (int)(((uint64_t)(gw - th) * (uint64_t)s_t0) / max0);
    if (*x < gx) *x = gx;
    if (*x > gx + gw - th) *x = gx + gw - th;
    return 1;
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
        orch_btn(dev, 104, 4, 28, 24, "Fit", 0);
        if (w >= 210) orch_btn(dev, 136, 4, 56, 24, sn, 0);
        return;
    }
    orch_btn(dev, 8, 4, 56, 24, "Sel", v->tool == ORCH_TOOL_SELECT);
    orch_btn(dev, 68, 4, 56, 24, "Pen", v->tool == ORCH_TOOL_PENCIL);
    orch_btn(dev, 128, 4, 56, 24, "Era", v->tool == ORCH_TOOL_ERASE);
    orch_btn(dev, 192, 4, 64, 24, sn, 0);
    orch_btn(dev, 264, 4, 64, 24, "Undo", 0);
    orch_btn(dev, 336, 4, 56, 24, "Fit", 0);
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
    uint8_t lo = 0;
    uint8_t hi = 127;
    orch_check(dev != 0, __LINE__);
    orch_check(v != 0, __LINE__);
    if (!dev || !v || track < 0 || gw < 4) return;
    if (track < (int)v->track_count) {
        const OrchTrack *tr = &v->trk[track];
        lo = tr->key_lo;
        hi = tr->key_hi;
    }
    map = lh >= 48;
    n = v->event_count;
    if (n > ORCH_MAX_EVENTS) n = ORCH_MAX_EVENTS;
    for (i = 0; i < n; i++) {
        const OrchEvent *e = &v->ev[i];
        uint32_t a;
        uint32_t b;
        int x0, x1, y0, hh;
        COLOR col;
        uint8_t key;
        uint8_t et = e->track;
        uint8_t vel = e->vel;
        uint32_t dur;
        if ((int)et != track || vel == 0) continue;
        a = e->start_tick;
        dur = e->dur_tick;
        b = a + (dur ? dur : 1u);
        if (b <= t0 || a >= t0 + vis) continue;
        x0 = x_of(gx, gw, t0, vis, a < t0 ? t0 : a);
        x1 = x_of(gx, gw, t0, vis, b > t0 + vis ? t0 + vis : b);
        if (x1 < x0 + 2) x1 = x0 + 2;
        key = e->key;
        if (map) {
            hh = lh / 16;
            if (hh < 3) hh = 3;
            y0 = y + ((127 - (int)key) * (lh - hh)) / 127;
        } else {
            y0 = y + 4;
            hh = lh - 8;
            if (hh < 4) hh = 4;
        }
        if ((int)i == s_pick) col = ORCH_COL_CYAN;
        else if (key < lo || key > hi) col = ORCH_COL_DIM;
        else col = ORCH_COL_AMBER;
        orch_fill(dev, x0, y0, x1 - x0, hh, col);
        if (!map && x1 - x0 >= 48 && track < ORCH_MAX_TRACKS) {
            char fit[48];
            const OrchTrack *tr = &v->trk[track];
            name_fit(fit, 48, tr->name, x1 - x0 - 6);
            if (fit[0]) orch_text(dev, x0 + 3, y0 + 2, fit, ORCH_COL_INSET_BG);
        }
    }
}

static void zone_tag(GDEV *dev, const OrchTrack *t, int x, int y, int head, int lh)
{
    uint8_t lo;
    uint8_t hi;
    int narrow;
    orch_check(dev != 0, __LINE__);
    orch_check(t != 0, __LINE__);
    if (!dev || !t) return;
    lo = t->key_lo;
    hi = t->key_hi;
    if (lo == 0u && hi >= 127u) return;
    narrow = (hi <= 59u);
    if (head > 48) {
        orch_text(dev, x + head - 26, y + 4, narrow ? "Lo" : "Hi",
                  narrow ? ORCH_COL_CYAN : ORCH_COL_ORANGE);
        return;
    }
    if (t->accent) return;
    orch_fill(dev, x, y, 3, lh > 1 ? lh - 1 : 1,
              narrow ? ORCH_COL_CYAN : ORCH_COL_ORANGE);
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
    orch_fill(dev, x, y, head + gw, lh > 1 ? lh - 1 : 1, bg);
    if (track >= 0 && track < (int)v->track_count) {
        const OrchTrack *tr = &v->trk[track];
        if (head > 8) {
            char fit[48];
            int px = head - 10;
            uint8_t lo = tr->key_lo;
            uint8_t hi = tr->key_hi;
            if (lo != 0u || hi < 127u) px = head - 36;
            if (px < 8) px = 8;
            name_fit(fit, 48, tr->name, px);
            orch_text(dev, x + 4, y + 4, fit, ORCH_COL_WHITE);
            if (tr->accent)
                orch_fill(dev, x, y, 3, lh > 1 ? lh - 1 : 1, (COLOR)tr->accent);
        }
        zone_tag(dev, tr, x, y, head, lh);
    }
    if (track < 0) return;
    bar = orch_q(v, ORCH_SNAP_BAR);
    if (bar == 0u) bar = 3840u;
    for (k = 0; k < 32; k++) {
        t = t0 + (uint32_t)k * bar;
        if (t > t0 && t < t0 + vis)
            orch_fill(dev, x_of(x + head, gw, t0, vis, t), y, 1, lh > 1 ? lh - 1 : 1, ORCH_COL_GRID_LINE);
    }
    paint_notes(dev, v, track, x + head, y, gw, lh, t0, vis);
}

static void paint_ruler(GDEV *dev, int gx, int gw, uint32_t bar, uint32_t vis)
{
    int i;
    int stop;
    int step = 1;
    orch_check(dev != 0, __LINE__);
    orch_check(bar > 0u && vis >= bar, __LINE__);
    orch_fill(dev, gx, 32, gw, 18, ORCH_COL_PANEL_BG);
    stop = gx + gw;
    if (gw >= 160) stop = gx + gw - 96;
    if (vis >= 32u * bar) step = 4;
    else if (vis >= 16u * bar && gw < 480) step = 2;
    for (i = 0; i < 64; i += step) {
        uint32_t t = s_t0 + (uint32_t)i * bar;
        int tx;
        char nb[8];
        if (t >= s_t0 + vis) break;
        tx = x_of(gx, gw, s_t0, vis, t) + 2;
        if (tx + 16 > stop) break;
        (void)orch_fmt_u(nb, 8, (t / bar) + 1u);
        orch_text(dev, tx, 34, nb, ORCH_COL_DIM);
    }
    if (gw < 160) return;
    orch_fill(dev, gx + gw - 92, 33, 28, 16, ORCH_COL_BTN_FACE);
    orch_text(dev, gx + gw - 90, 33, "Out", ORCH_COL_WHITE);
    orch_fill(dev, gx + gw - 62, 33, 28, 16, ORCH_COL_INSET_BG);
    orch_text(dev, gx + gw - 54, 33, zoom_label(), ORCH_COL_AMBER);
    orch_fill(dev, gx + gw - 32, 33, 28, 16, ORCH_COL_BTN_FACE);
    orch_text(dev, gx + gw - 24, 33, "In", ORCH_COL_WHITE);
}

static void paint_vsb(GDEV *dev, const ArrGeom *g)
{
    int ty = 0;
    int th = 0;
    int vx;
    int top;
    int body;
    orch_check(dev != 0, __LINE__);
    orch_check(g != 0, __LINE__);
    if (!dev || !g) return;
    vx = g->vx;
    top = g->top;
    body = g->body;
    orch_fill(dev, vx, top, SB, body, ORCH_COL_INSET_BG);
    (void)thumb_v(g, &ty, &th);
    if (th >= 8 && SB >= 10) orch_btn(dev, vx + 1, ty, SB - 2, th, "", 0);
}

static void paint_hsb(GDEV *dev, const ArrGeom *g)
{
    int tx = 0;
    int tw = 0;
    int gx;
    int gw;
    int hy;
    int vx;
    orch_check(dev != 0, __LINE__);
    orch_check(g != 0, __LINE__);
    if (!dev || !g) return;
    gx = g->gx;
    gw = g->gw;
    hy = g->hy;
    vx = g->vx;
    orch_fill(dev, gx, hy, gw, SB, ORCH_COL_INSET_BG);
    orch_fill(dev, vx, hy, SB, SB, ORCH_COL_INSET_BG);
    (void)thumb_h(g, &tx, &tw);
    if (tw >= 8) orch_btn(dev, tx, hy + 1, tw, SB - 2, "", 0);
}

static void paint_lanes(GDEV *dev, const OrchView *v, const ArrGeom *g)
{
    int i;
    int lanes, top, lh, head, gw, tracks;
    uint32_t vis;
    int sel_ix;
    orch_check(dev != 0, __LINE__);
    orch_check(v != 0 && g != 0, __LINE__);
    if (!dev || !v || !g) return;
    lanes = g->lanes;
    top = g->top;
    lh = g->lh;
    head = g->head;
    gw = g->gw;
    vis = g->vis;
    tracks = g->tracks;
    sel_ix = (int)v->selected;
    for (i = 0; i < lanes && i < ORCH_MAX_TRACKS; i++) {
        int tr = s_row + i;
        int sel = (tr >= 0 && tr == sel_ix);
        if (tr < 0 || tr >= tracks) tr = -1;
        paint_lane(dev, v, tr, 0, top + i * lh, head, gw, lh, s_t0, vis, sel);
    }
}

static void paint_head(GDEV *dev, const OrchView *v, const ArrGeom *g)
{
    uint32_t now;
    uint32_t vis;
    uint32_t bar;
    int px, gx, gw, mode, ww, top, body, lanes, lh, span;
    orch_check(dev != 0, __LINE__);
    orch_check(v != 0 && g != 0, __LINE__);
    if (!dev || !v || !g) return;
    ww = g->w;
    mode = g->mode;
    paint_tools(dev, v, ww, mode);
    gx = g->gx;
    gw = g->gw;
    vis = g->vis;
    bar = g->bar;
    if (mode != ORCH_LAY_MIN) paint_ruler(dev, gx, gw, bar, vis);
    paint_lanes(dev, v, g);
    now = orch_now_tick(v);
    top = g->top;
    body = g->body;
    lanes = g->lanes;
    lh = g->lh;
    span = lanes * lh;
    if (span > body) span = body;
    if (now >= s_t0 && now <= s_t0 + vis) {
        px = x_of(gx, gw, s_t0, vis, now);
        if (px > gx + gw - 2) px = gx + gw - 2;
        orch_fill(dev, px, top, 2, span, ORCH_COL_PLAYHEAD);
    }
    paint_vsb(dev, g);
    paint_hsb(dev, g);
}

static void arrange_paint(WND *wnd, GDEV *dev)
{
    int w;
    int h;
    ArrGeom g;
    OrchView *v = gst_view();
    orch_check(dev != 0, __LINE__);
    orch_check(v != 0, __LINE__);
    if (!dev || !v) return;
    w = (int)dev->width;
    h = (int)dev->height;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    geom_of(v, w, h, &g);
    time_follow(v, &g);
    if (s_drag != DRAG_V) row_sync(v, &g);
    if (s_row < 0) s_row = 0;
    if (s_row > g.vmax) s_row = g.vmax;
    orch_fill(dev, 0, 0, w, h, ORCH_COL_CANVAS);
    paint_head(dev, v, &g);
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
        else if (orch_inside(x, y, 104, 4, 28, 24)) cmd_auto_scale(v);
        else if (orch_inside(x, y, 136, 4, 56, 24)) orch_desk_snap_cycle();
        else return 0;
        return 1;
    }
    if (orch_inside(x, y, 8, 4, 56, 24)) orch_desk_tool(ORCH_TOOL_SELECT);
    else if (orch_inside(x, y, 68, 4, 56, 24)) orch_desk_tool(ORCH_TOOL_PENCIL);
    else if (orch_inside(x, y, 128, 4, 56, 24)) orch_desk_tool(ORCH_TOOL_ERASE);
    else if (orch_inside(x, y, 192, 4, 64, 24)) orch_desk_snap_cycle();
    else if (orch_inside(x, y, 264, 4, 64, 24)) (void)orch_undo_apply();
    else if (orch_inside(x, y, 336, 4, 56, 24)) cmd_auto_scale(v);
    else return 0;
    return 1;
}

static void ruler_page(const OrchView *v, int x, const ArrGeom *g)
{
    uint32_t max0;
    uint32_t bar;
    uint32_t vis;
    uint32_t end;
    int gw;
    int gx;
    int x0;
    orch_check(v != 0, __LINE__);
    orch_check(g != 0, __LINE__);
    if (!v || !g) return;
    gw = g->gw;
    gx = g->gx;
    if (gw < 1) return;
    if (gw >= 160) {
        x0 = gx + gw - 92;
        if (x >= x0) {
            if (x < x0 + 30) zoom_by(-1);
            else if (x >= x0 + 60) zoom_by(1);
            return;
        }
    }
    bar = g->bar;
    vis = g->vis;
    end = g->end;
    max0 = (end > vis) ? end - vis : 0u;
    if (x < gx + gw / 3) {
        if (s_t0 >= bar) s_t0 -= bar;
        else s_t0 = 0u;
        s_hold = 1;
    } else if (x > gx + (2 * gw) / 3) {
        if (s_t0 < max0) {
            s_t0 += bar;
            if (s_t0 > max0) s_t0 = max0;
        }
        s_hold = 1;
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
    orch_check(track >= 0 && track < ORCH_MAX_TRACKS, __LINE__);
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

static void vsb_click(const ArrGeom *g, int y)
{
    int ty = 0;
    int th = 0;
    int live;
    int lanes;
    int vmax;
    orch_check(g != 0, __LINE__);
    orch_check(s_drag >= DRAG_OFF && s_drag <= DRAG_H, __LINE__);
    if (!g) return;
    lanes = g->lanes;
    vmax = g->vmax;
    live = thumb_v(g, &ty, &th);
    if (!live) return;
    if (y >= ty && y < ty + th) {
        s_drag = DRAG_V;
        s_anchor = y;
        s_anchor_row = s_row;
        return;
    }
    if (y < ty) s_row -= lanes;
    else s_row += lanes;
    if (s_row < 0) s_row = 0;
    if (s_row > vmax) s_row = vmax;
}

static void hsb_click(const ArrGeom *g, int x)
{
    int tx = 0;
    int tw = 0;
    int live;
    uint32_t max0;
    uint32_t vis;
    uint32_t end;
    uint32_t n;
    orch_check(g != 0, __LINE__);
    orch_check(s_drag >= DRAG_OFF && s_drag <= DRAG_H, __LINE__);
    if (!g) return;
    vis = g->vis;
    end = g->end;
    if (vis == 0u) return;
    live = thumb_h(g, &tx, &tw);
    max0 = (end > vis) ? end - vis : 0u;
    if (!live) return;
    if (x >= tx && x < tx + tw) {
        s_drag = DRAG_H;
        s_anchor = x;
        s_anchor_t0 = s_t0;
        s_hold = 1;
        return;
    }
    if (x < tx) n = (s_t0 > vis) ? s_t0 - vis : 0u;
    else {
        n = s_t0 + vis;
        if (n < s_t0 || n > max0) n = max0;
    }
    s_t0 = n;
    s_hold = 1;
}

static void lane_click(const OrchView *v, const ArrGeom *g, int x, int y)
{
    int i;
    int head;
    int lanes;
    int lh;
    int top;
    orch_check(v != 0, __LINE__);
    orch_check(g != 0, __LINE__);
    if (!v || !g) return;
    lh = g->lh;
    if (lh < 1) return;
    head = g->head;
    lanes = g->lanes;
    top = g->top;
    if (head > 0 && x < head && y >= top) {
        int lane = (y - top) / lh;
        int tr;
        if (lane < 0 || lane >= lanes) return;
        tr = s_row + lane;
        if (tr >= 0 && tr < g->tracks) orch_desk_sel(tr);
        return;
    }
    for (i = 0; i < lanes && i < ORCH_MAX_TRACKS; i++) {
        int tr = s_row + i;
        int ly = top + i * lh;
        int gx = g->gx;
        int gw = g->gw;
        if (tr < 0 || tr >= g->tracks) continue;
        if (!orch_inside(x, y, gx, ly, gw, lh)) continue;
        lane_edit(v, tr, x, y, gx, ly, gw, lh, g->vis);
        return;
    }
}

static void arrange_drag(int x, int y, int w, int h)
{
    OrchView *v;
    ArrGeom g;
    int ty = 0;
    int th = 0;
    int tx = 0;
    int tw = 0;
    int travel;
    int row;
    int64_t t;
    uint32_t max0;
    uint32_t end;
    uint32_t vis;
    orch_check(s_drag == DRAG_V || s_drag == DRAG_H, __LINE__);
    orch_check(w > 0 && h > 0, __LINE__);
    v = gst_view();
    if (!v || w < 1 || h < 1) return;
    geom_of(v, w, h, &g);
    if (s_drag == DRAG_V) {
        (void)thumb_v(&g, &ty, &th);
        travel = g.body - th;
        if (travel < 1) travel = 1;
        row = s_anchor_row + ((y - s_anchor) * g.vmax) / travel;
        if (row < 0) row = 0;
        if (row > g.vmax) row = g.vmax;
        s_row = row;
        return;
    }
    (void)thumb_h(&g, &tx, &tw);
    travel = g.gw - tw;
    if (travel < 1) travel = 1;
    end = g.end;
    vis = g.vis;
    max0 = (end > vis) ? end - vis : 0u;
    t = (int64_t)s_anchor_t0;
    t += ((int64_t)(x - s_anchor) * (int64_t)max0) / (int64_t)travel;
    if (t < 0) t = 0;
    if (t > (int64_t)max0) t = (int64_t)max0;
    s_t0 = (uint32_t)t;
    s_hold = 1;
}

static void drag_up(int w, int h)
{
    OrchView *v;
    ArrGeom g;
    uint32_t now;
    uint32_t vis;
    int was;
    orch_check(s_drag >= DRAG_OFF, __LINE__);
    orch_check(s_drag <= DRAG_H, __LINE__);
    was = s_drag;
    s_drag = DRAG_OFF;
    if (was != DRAG_H || w < 1 || h < 1) return;
    v = gst_view();
    if (!v) return;
    geom_of(v, w, h, &g);
    vis = g.vis;
    now = orch_now_tick(v);
    if (now >= s_t0 && now <= s_t0 + vis) s_hold = 0;
}

static void arrange_click(int x, int y, int w, int h)
{
    OrchView *v = gst_view();
    ArrGeom g;
    int mode;
    int top;
    orch_check(v != 0, __LINE__);
    orch_check(w > 0 && h > 0, __LINE__);
    if (!v || w < 1 || h < 1) return;
    geom_of(v, w, h, &g);
    row_sync(v, &g);
    mode = g.mode;
    if (tool_click(v, x, y, mode)) return;
    top = g.top;
    if (mode != ORCH_LAY_MIN && orch_inside(x, y, g.gx, 32, g.gw, 18)) {
        ruler_page(v, x, &g);
        return;
    }
    if (orch_inside(x, y, g.vx, top, SB, g.body)) {
        vsb_click(&g, y);
        return;
    }
    if (orch_inside(x, y, g.gx, g.hy, g.gw, SB)) {
        hsb_click(&g, x);
        return;
    }
    lane_click(v, &g, x, y);
}

static void arrange_event(WND *wnd, const EVT *evt)
{
    int x, y, w, h;
    orch_check(wnd != 0, __LINE__);
    orch_check(evt != 0, __LINE__);
    if (!wnd || !evt) return;
    orch_client(wnd, evt, &x, &y, &w, &h);
    if (evt->type == EV_BUT_UP) {
        drag_up(w, h);
        (void)inval_wnd(wnd);
        return;
    }
    if (evt->type == EV_MOUSE_MOVE) {
        if (s_drag == DRAG_OFF) return;
        arrange_drag(x, y, w, h);
        (void)inval_wnd(wnd);
        return;
    }
    if (evt->type != EV_BUT_DOWN) return;
    s_drag = DRAG_OFF;
    arrange_click(x, y, w, h);
    (void)inval_wnd(wnd);
}

static void on_destroy(WND *wnd)
{
    orch_check(wnd != 0, __LINE__);
    orch_check(s_wnd == 0 || s_wnd == wnd, __LINE__);
    (void)wnd;
    s_wnd = 0;
    s_drag = DRAG_OFF;
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
    cmd_auto_scale(v);
    return w;
}
