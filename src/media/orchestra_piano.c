#include "orchestra_desk.h"
#include <apps/orch_chk.h>
#include "../demo/lilcu64_score.h"
#include <math.h>

static WND *s_wnd;
static uint32_t s_t0;
static int s_key0 = 48;
static int s_pick = -1;
static int s_zoom = 2;
static int s_vzoom = 1; /* 0: 12px, 1: 16px, 2: 20px, 3: 26px */
static int s_hold;
static int s_follow = 1;
static int s_fit_tr = -2;
static uint32_t s_home;
static uint8_t s_seen_state;
static int s_audition_key = -1;
static int s_drag;
static int s_anchor;
static int s_anchor_key;
static uint32_t s_anchor_t0;

enum { SB = 16, DRAG_OFF = 0, DRAG_V = 1, DRAG_H = 2 };

typedef struct PianoGeom {
    int mode;
    int w;
    int h;
    int top;
    int rh;
    int rows;
    int body;
    int kw;
    int gx;
    int gw;
    int vx;
    int hy;
    int kmax;
    uint32_t bar;
    uint32_t beat;
    uint32_t vis;
    uint32_t end;
} PianoGeom;

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

static int vzoom_rh(void)
{
    static const int rhs[4] = { 10, 10, 16, 20 };
    orch_check(s_vzoom >= 0 && s_vzoom <= 3, __LINE__);
    orch_check(rhs[1] == 10, __LINE__);
    if (s_vzoom < 0 || s_vzoom > 3) return 10;
    return rhs[s_vzoom];
}

static void vzoom_by(int dir)
{
    orch_check(dir == -1 || dir == 1, __LINE__);
    orch_check(s_vzoom >= 0 && s_vzoom <= 3, __LINE__);
    if (dir < 0 && s_vzoom > 0) s_vzoom--;
    if (dir > 0 && s_vzoom < 3) s_vzoom++;
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
    int rh = vzoom_rh();
    int rows;
    orch_check(avail > -100 && avail < 8000, __LINE__);
    orch_check(rh >= 10 && rh <= 26, __LINE__);
    if (avail < 48) avail = 48;
    rows = avail / rh;
    if (rows < 6) rows = 6;
    if (rows > 48) rows = 48;
    return rows;
}

static void clamp_key(int rows)
{
    orch_check(rows >= 6 && rows <= 48, __LINE__);
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
        orch_btn(dev, 8, 4, 24, 22, "S", v->tool == ORCH_TOOL_SELECT);
        orch_btn(dev, 34, 4, 24, 22, "P", v->tool == ORCH_TOOL_PENCIL);
        orch_btn(dev, 60, 4, 24, 22, "E", v->tool == ORCH_TOOL_ERASE);
        orch_btn(dev, 86, 4, 28, 22, "Fit", 0);
        orch_btn(dev, 116, 4, 28, 22, "Fol", s_follow);
        return;
    }
    orch_btn(dev, 8, 4, 38, 22, "Sel", v->tool == ORCH_TOOL_SELECT);
    orch_btn(dev, 48, 4, 38, 22, "Pen", v->tool == ORCH_TOOL_PENCIL);
    orch_btn(dev, 88, 4, 38, 22, "Era", v->tool == ORCH_TOOL_ERASE);
    orch_btn(dev, 128, 4, 44, 22, snap_name((int)v->snap), 0);
    orch_btn(dev, 174, 4, 44, 22, "Undo", 0);
    orch_btn(dev, 220, 4, 36, 22, "Fit", 0);
    orch_btn(dev, 258, 4, 24, 22, "V-", 0);
    orch_btn(dev, 284, 4, 24, 22, "V+", 0);
    orch_btn(dev, 310, 4, 34, 22, "Fol", s_follow);
}

static int key_w(int mode)
{
    orch_check(mode >= ORCH_LAY_WIDE && mode <= ORCH_LAY_MIN, __LINE__);
    orch_check(mode < 8, __LINE__);
    if (mode == ORCH_LAY_MIN) return 24;
    if (mode == ORCH_LAY_STACK) return 40;
    return 56;
}

static void audition_note(const OrchView *v, int key)
{
    int track;
    int shim;
    float hz;
    orch_check(v != 0, __LINE__);
    orch_check(key >= 0 && key < 128, __LINE__);
    if (!v || key < 0 || key > 127) return;
    track = (int)v->selected;
    shim = (track >= 0 && track < (int)v->track_count) ? (int)v->trk[track].shim : 0;
    if (shim <= 0) shim = LC_SHIM_PIANO;
    hz = 440.0f * powf(2.0f, ((float)key - 69.0f) / 12.0f);
    s_audition_key = key;
    lc_shim_note(shim, 0.0, hz, 0.35f, 0.28f);
}


static int x_of(int gx, int gw, uint32_t vis, uint32_t tick)
{
    orch_check(gw > 0, __LINE__);
    orch_check(vis > 0u, __LINE__);
    if (tick <= s_t0 || gw <= 0 || vis == 0u) return gx;
    return gx + (int)(((uint64_t)(tick - s_t0) * (uint64_t)gw) / vis);
}

static void fit_name(char *dst, int cap, const char *src, int px)
{
    int i = 0;
    int n = 0;
    int w = 0;
    orch_check(dst != 0, __LINE__);
    orch_check(cap >= 2 && cap <= 48, __LINE__);
    dst[0] = 0;
    if (!src || px < 8 || cap < 2) return;
    while (src[n] && i < cap - 1 && n < 46) {
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
        if (w + adv > px || i + step >= cap) break;
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

static void part_text(char *dst, int cap, const OrchView *v, int px)
{
    char raw[48];
    char num[8];
    const char *nm;
    int track;
    int i;
    int k;
    orch_check(dst != 0, __LINE__);
    orch_check(cap >= 2 && cap <= 48, __LINE__);
    dst[0] = 0;
    if (!v || cap < 2) return;
    track = (int)v->selected;
    if (track < 0 || track >= (int)v->track_count || track > 63) return;
    (void)orch_fmt_u(num, 8, (unsigned)(track + 1));
    i = 0;
    for (k = 0; num[k] && i < 46 && k < 4; k++) raw[i++] = num[k];
    if (i < 46) raw[i++] = ' ';
    nm = v->trk[track].name;
    for (k = 0; nm[k] && i < 47 && k < 46; k++) raw[i++] = nm[k];
    raw[i] = 0;
    fit_name(dst, cap, raw, px);
}

static void step_track(const OrchView *v, int dir)
{
    int n;
    int t;
    orch_check(v != 0, __LINE__);
    orch_check(dir == -1 || dir == 1, __LINE__);
    if (!v) return;
    n = (int)v->track_count;
    if (n < 1 || n > 64) return;
    t = (int)v->selected;
    if (t < 0 || t >= n) t = 0;
    if (dir < 0) t = (t == 0) ? n - 1 : t - 1;
    else t = (t + 1 >= n) ? 0 : t + 1;
    s_hold = 0;
    orch_desk_sel(t);
}

static void key_span(const OrchView *v, int track, int *lo, int *hi, int *any)
{
    uint32_t n;
    uint32_t i;
    orch_check(v != 0, __LINE__);
    orch_check(lo != 0 && hi != 0 && any != 0, __LINE__);
    if (lo) *lo = 60;
    if (hi) *hi = 60;
    if (any) *any = 0;
    if (!v || !lo || !hi || !any || track < 0) return;
    n = v->event_count;
    if (n > 4096u) n = 4096u;
    for (i = 0; i < n; i++) {
        const OrchEvent *e = &v->ev[i];
        int key;
        uint8_t tr = e->track;
        uint8_t vel = e->vel;
        if ((int)tr != track || vel == 0) continue;
        key = (int)e->key;
        if (*any == 0 || key < *lo) *lo = key;
        if (*any == 0 || key > *hi) *hi = key;
        *any = 1;
    }
}

static uint32_t first_tick(const OrchView *v, int track, int *any)
{
    uint32_t n;
    uint32_t i;
    uint32_t best = 0;
    orch_check(v != 0, __LINE__);
    orch_check(any != 0, __LINE__);
    if (any) *any = 0;
    if (!v || !any || track < 0) return 0;
    n = v->event_count;
    if (n > 4096u) n = 4096u;
    for (i = 0; i < n; i++) {
        const OrchEvent *e = &v->ev[i];
        uint32_t a;
        uint8_t tr = e->track;
        uint8_t vel = e->vel;
        if ((int)tr != track || vel == 0) continue;
        a = e->start_tick;
        if (*any == 0 || a < best) best = a;
        *any = 1;
    }
    return best;
}

static void tick_span(const OrchView *v, int track, uint32_t *t_first, uint32_t *t_last, int *any)
{
    uint32_t n;
    uint32_t i;
    orch_check(v != 0, __LINE__);
    orch_check(t_first != 0 && t_last != 0 && any != 0, __LINE__);
    if (t_first) *t_first = 0;
    if (t_last) *t_last = 0;
    if (any) *any = 0;
    if (!v || !t_first || !t_last || !any || track < 0) return;
    n = v->event_count;
    if (n > 4096u) n = 4096u;
    for (i = 0; i < n; i++) {
        const OrchEvent *e = &v->ev[i];
        uint32_t a, b, dur;
        uint8_t tr = e->track;
        uint8_t vel = e->vel;
        if ((int)tr != track || vel == 0) continue;
        a = e->start_tick;
        dur = e->dur_tick;
        b = a + (dur ? dur : 1u);
        if (*any == 0 || a < *t_first) *t_first = a;
        if (*any == 0 || b > *t_last) *t_last = b;
        *any = 1;
    }
}

static void cmd_auto_scale(const OrchView *v, int avail_h, uint32_t bar)
{
    int track;
    int lo = 60, hi = 60, any_pitch = 0, any_time = 0;
    uint32_t t_first = 0, t_last = 0;
    int span_keys;
    int rows;
    uint32_t duration_ticks, bars_needed;
    orch_check(v != 0, __LINE__);
    orch_check(avail_h > 0, __LINE__);
    if (!v || bar == 0u) return;
    track = (int)v->selected;
    if (track < 0 || track >= (int)v->track_count) {
        s_key0 = 48;
        s_t0 = 0;
        s_zoom = 2;
        s_vzoom = 1;
        return;
    }
    key_span(v, track, &lo, &hi, &any_pitch);
    tick_span(v, track, &t_first, &t_last, &any_time);
    if (!any_pitch || !any_time) {
        s_key0 = 48;
        s_t0 = 0;
        s_zoom = 2;
        s_vzoom = 1;
        return;
    }
    span_keys = hi - lo + 1;
    if (span_keys < 1) span_keys = 1;
    if (span_keys <= 8) s_vzoom = 3;
    else if (span_keys <= 14) s_vzoom = 2;
    else if (span_keys <= 24) s_vzoom = 1;
    else s_vzoom = 0;
    rows = rows_of(avail_h);
    if (span_keys < rows) s_key0 = lo - (rows - span_keys) / 2;
    else s_key0 = lo;
    clamp_key(rows);
    s_t0 = (t_first / bar) * bar;
    duration_ticks = (t_last > s_t0) ? (t_last - s_t0) : bar;
    bars_needed = (duration_ticks + bar - 1u) / bar;
    if (bars_needed <= 1u) s_zoom = 5;
    else if (bars_needed <= 2u) s_zoom = 4;
    else if (bars_needed <= 4u) s_zoom = 3;
    else if (bars_needed <= 8u) s_zoom = 2;
    else if (bars_needed <= 16u) s_zoom = 1;
    else s_zoom = 0;
    s_hold = 0;
    s_pick = -1;
}

/* Whole span when it fits in the rows; otherwise the middle stays visible. */
static void fit_pitch(const OrchView *v, int rows, uint32_t bar)
{
    int track;
    int lo;
    int hi;
    int any;
    int span;
    int key0;
    uint32_t a;
    orch_check(v != 0, __LINE__);
    orch_check(rows >= 6 && rows <= 48, __LINE__);
    if (!v || rows < 6) return;
    track = (int)v->selected;
    if (track == s_fit_tr) return;
    s_fit_tr = track;
    s_pick = -1;
    if (track < 0 || track >= (int)v->track_count) {
        s_key0 = 48;
        s_t0 = 0;
        return;
    }
    key_span(v, track, &lo, &hi, &any);
    if (!any) {
        s_key0 = 48;
        s_t0 = 0;
        clamp_key(rows);
        return;
    }
    span = hi - lo + 1;
    if (span < 1) span = 1;
    if (span <= rows) key0 = lo - (rows - span) / 2;
    else key0 = lo + (span - rows) / 2;
    s_key0 = key0;
    clamp_key(rows);
    a = first_tick(v, track, &any);
    if (any && bar > 0u) s_t0 = (a / bar) * bar;
}

static void paint_part(GDEV *dev, const OrchView *v, int w, int mode)
{
    char name[32];
    int x0;
    int px;
    int track;
    orch_check(dev != 0, __LINE__);
    orch_check(v != 0, __LINE__);
    if (!dev || !v) return;
    if (mode == ORCH_LAY_MIN) {
        if (w < 200) return;
        orch_btn(dev, 146, 4, 22, 22, "<", 0);
        orch_btn(dev, 170, 4, 22, 22, ">", 0);
        return;
    }
    if (w < 440) return;
    x0 = w - 54;
    px = x0 - 372;
    track = (int)v->selected;
    if (track >= 0 && track < (int)v->track_count) {
        COLOR tc = v->trk[track].accent ? (COLOR)v->trk[track].accent : ORCH_COL_AMBER;
        orch_fill(dev, 356, 10, 8, 10, tc);
    }
    if (px >= 32) {
        part_text(name, 32, v, px);
        if (name[0]) orch_text(dev, 368, 8, name, ORCH_COL_WHITE);
    }
    orch_btn(dev, x0, 4, 24, 22, "<", 0);
    orch_btn(dev, x0 + 26, 4, 24, 22, ">", 0);
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
    if (n > 4096u) n = 4096u;
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

static void geom_of(const OrchView *v, int w, int h, PianoGeom *g)
{
    int mode, top, kw, gx, vx, gw, hy, avail, rh, rows, body, kmax;
    uint32_t bar, beat, vis, end;
    orch_check(v != 0, __LINE__);
    orch_check(g != 0, __LINE__);
    if (!v || !g || w < 1 || h < 1) return;
    mode = orch_lay_mode(w, h);
    top = (mode == ORCH_LAY_MIN) ? 32 : 52;
    kw = key_w(mode);
    gx = kw + 4;
    vx = w - SB - 2;
    if (vx < gx + 20) vx = gx + 20;
    gw = vx - gx - 2;
    if (gw < 8) gw = 8;
    hy = h - SB;
    avail = hy - top - 2;
    if (avail < 48) avail = 48;
    rh = vzoom_rh();
    if (rh < 8) rh = 8;
    rows = avail / rh;
    if (rows < 6) rows = 6;
    if (rows > 48) rows = 48;
    body = hy - top;
    if (body < rows * rh) body = rows * rh;
    kmax = 128 - rows;
    if (kmax < 0) kmax = 0;
    bar = orch_q(v, ORCH_SNAP_BAR);
    if (bar == 0u) bar = 3840u;
    beat = orch_q(v, ORCH_SNAP_BEAT);
    if (beat == 0u) beat = 960u;
    vis = bar * (uint32_t)zoom_bars(gw);
    if (vis == 0u) vis = bar;
    end = span_end(v, bar);
    g->mode = mode;
    g->w = w;
    g->h = h;
    g->top = top;
    g->rh = rh;
    g->rows = rows;
    g->body = body;
    g->kw = kw;
    g->gx = gx;
    g->gw = gw;
    g->vx = vx;
    g->hy = hy;
    g->kmax = kmax;
    g->bar = bar;
    g->beat = beat;
    g->vis = vis;
    g->end = end;
    orch_check(rows >= 6 && rows <= 48, __LINE__);
    orch_check(vis > 0u, __LINE__);
}

static int thumb_v(const PianoGeom *g, int *y, int *h)
{
    int body, rows, th, kmax, top;
    orch_check(g != 0, __LINE__);
    orch_check(y != 0 && h != 0, __LINE__);
    if (!g || !y || !h) return 0;
    body = g->body;
    rows = g->rows;
    top = g->top;
    kmax = g->kmax;
    *y = top;
    *h = (body > 0) ? body : 1;
    if (body < 1 || rows < 1) return 0;
    th = (body * rows) / 128;
    if (th < 12) th = 12;
    if (th > body) th = body;
    *h = th;
    if (kmax <= 0) return 0;
    *y = top + ((body - th) * (kmax - s_key0)) / kmax;
    if (*y < top) *y = top;
    if (*y > top + body - th) *y = top + body - th;
    return 1;
}

static int thumb_h(const PianoGeom *g, int *x, int *w)
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

static void paint_ruler(GDEV *dev, int gx, int gw, uint32_t bar, uint32_t vis)
{
    int i, stop, step;
    orch_check(dev != 0, __LINE__);
    orch_check(bar > 0u && vis >= bar, __LINE__);
    orch_fill(dev, gx, 32, gw, 18, ORCH_COL_PANEL_BG);
    stop = gx + gw;
    if (gw >= 160) stop = gx + gw - 96;
    step = 1;
    if (vis >= bar * 32u) step = 4;
    else if (vis >= bar * 16u) step = (gw < 480) ? 2 : 1;
    for (i = 0; i < 64; i += step) {
        uint32_t t = s_t0 + (uint32_t)i * bar;
        int tx;
        char nb[8];
        if (t >= s_t0 + vis) break;
        tx = x_of(gx, gw, vis, t) + 2;
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

static void paint_vsb(GDEV *dev, const PianoGeom *g)
{
    int ty = 0;
    int th = 0;
    int vx, top, body;
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

static void paint_hsb(GDEV *dev, const PianoGeom *g)
{
    int tx = 0;
    int tw = 0;
    int gx, gw, hy, vx;
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

static int piano_key_at(int x, int y, int top, int rh, int rows, int kw)
{
    static const int blk_k[5] = { 1, 2, 4, 5, 6 };
    static const int blk_note[5] = { 1, 3, 6, 8, 10 };
    static const int w_note[7] = { 0, 2, 4, 5, 7, 9, 11 };
    int bw = (kw * 60) / 100;
    int bh = (12 * rh * 46) / 700 + 3;
    int oct_start = ((s_key0 - 12) / 12) * 12;
    int oct_end = s_key0 + rows + 12;
    int o;
    int key;

    orch_check(rows >= 6 && rows <= 48, __LINE__);
    orch_check(rh >= 8 && rh <= 32, __LINE__);

    if (x < bw) {
        for (o = oct_start; o <= oct_end; o += 12) {
            int r_c = s_key0 + rows - 1 - o;
            int y_c_bot = top + (r_c + 1) * rh;
            int b;
            for (b = 0; b < 5; b++) {
                int k = blk_k[b];
                int dy = y_c_bot - (k * 12 * rh) / 7;
                int by = dy - bh / 2;
                if (y >= by && y < by + bh) {
                    int k_pitch = o + blk_note[b];
                    if (k_pitch >= 0 && k_pitch < 128) return k_pitch;
                }
            }
        }
    }

    for (o = oct_start; o <= oct_end; o += 12) {
        int r_c = s_key0 + rows - 1 - o;
        int y_c_bot = top + (r_c + 1) * rh;
        int k;
        for (k = 0; k < 7; k++) {
            int y_bot = y_c_bot - (k * 12 * rh) / 7;
            int y_top = y_c_bot - ((k + 1) * 12 * rh) / 7;
            if (y >= y_top && y < y_bot) {
                int k_pitch = o + w_note[k];
                if (k_pitch >= 0 && k_pitch < 128) return k_pitch;
            }
        }
    }

    key = s_key0 + rows - 1 - (y - top) / rh;
    if (key < 0) key = 0;
    if (key > 127) key = 127;
    return key;
}

static void paint_keys(GDEV *dev, const OrchView *v, int gx, int top, int gw, int rh, int rows, int kw, int mode)
{
    static const char *wnames[7] = { "C", "D", "E", "F", "G", "A", "B" };
    static const int blk_k[5] = { 1, 2, 4, 5, 6 };
    static const int blk_note[5] = { 1, 3, 6, 8, 10 };
    static const int w_note[7] = { 0, 2, 4, 5, 7, 9, 11 };
    int r;
    int lo = 0;
    int hi = 127;
    int track;
    int bw;
    int bh;
    int oct_start, oct_end;
    int o;

    orch_check(dev != 0, __LINE__);
    orch_check(rows >= 6 && rows <= 48, __LINE__);
    orch_check(rh >= 8 && rh <= 32, __LINE__);
    orch_check(kw >= 20 && kw <= 80, __LINE__);
    if (!dev || !v) return;
    track = (int)v->selected;
    if (track >= 0 && track < (int)v->track_count && track < 64) {
        const OrchTrack *tr = &v->trk[track];
        lo = (int)tr->key_lo;
        hi = (int)tr->key_hi;
    }

    /* Classic piano proportions:
     * - Black key visible length is 60% of white key visible length (90mm / 150mm)
     * - Black key height is ~46% of white key height (10.7mm / 23.5mm)
     * - Black keys sit directly in the middle between adjacent white keys */
    bw = (kw * 60) / 100;
    bh = (12 * rh * 46) / 700 + 3;

    /* Pass 1: Grid lanes */
    for (r = 0; r < rows && r < 48; r++) {
        int key = s_key0 + rows - 1 - r;
        int y = top + r * rh;
        COLOR bg;
        if (key < 0 || key > 127) continue;
        bg = black_key(key) ? 0xFF0B0E14u : 0xFF181D29u;
        if (key < lo || key > hi) bg = ORCH_COL_INSET_BG;
        orch_fill(dev, gx, y, gw, rh - 1, bg);
        orch_fill(dev, gx, y + rh - 1, gw, 1, 0xFF141822u);
    }

    /* Pass 2: Base Ivory Keyboard Surface */
    orch_fill(dev, 0, top, kw - 1, rows * rh, 0xFFF8F9FAu);
    orch_fill(dev, kw - 1, top, 1, rows * rh, 0xFF2D3546u); /* border against grid */

    oct_start = ((s_key0 - 12) / 12) * 12;
    oct_end = s_key0 + rows + 12;

    /* Pass 3: White key audition highlights, labels, and range markers */
    for (o = oct_start; o <= oct_end; o += 12) {
        int r_c = s_key0 + rows - 1 - o;
        int y_c_bot = top + (r_c + 1) * rh;
        int k;

        for (k = 0; k < 7; k++) {
            int y_bot = y_c_bot - (k * 12 * rh) / 7;
            int y_top = y_c_bot - ((k + 1) * 12 * rh) / 7;
            int y_mid = (y_bot + y_top) / 2;
            int key = o + w_note[k];

            /* Highlight auditioned white key */
            if (key == s_audition_key) {
                int cl_top = y_top < top ? top : y_top;
                int cl_bot = y_bot > top + rows * rh ? top + rows * rh : y_bot;
                if (cl_bot > cl_top) {
                    orch_fill(dev, 0, cl_top, kw - 1, cl_bot - cl_top, ORCH_COL_CYAN);
                }
            }

            /* Draw Note Name centered vertically and horizontally in the white key lip */
            if (y_mid - 4 >= top && y_mid + 4 < top + rows * rh && mode != ORCH_LAY_MIN) {
                int lip_w = kw - 1 - bw;
                if (k == 0) {
                    char b[4];
                    int oct = key / 12 - 1;
                    int tlen = 2;
                    int tx;
                    b[0] = 'C';
                    if (oct < 0) {
                        b[1] = '-';
                        b[2] = (char)('0' - oct);
                        b[3] = 0;
                        tlen = 3;
                    } else {
                        b[1] = (char)('0' + oct);
                        b[2] = 0;
                    }
                    tx = bw + (lip_w - tlen * 8) / 2;
                    COLOR ink = (key == s_audition_key) ? ORCH_COL_WHITE : 0xFF1E2430u;
                    orch_text(dev, tx, y_mid - 4, b, ink);
                } else if (rh >= 10 && lip_w >= 10) {
                    int tx = bw + (lip_w - 8) / 2;
                    COLOR ink = (key == s_audition_key) ? ORCH_COL_WHITE : 0xFF707A8Eu;
                    orch_text(dev, tx, y_mid - 4, wnames[k], ink);
                }
            }

            /* Out of track pitch range indicator */
            if ((key < lo || key > hi) && y_bot > top && y_top < top + rows * rh) {
                int cl_top = y_top < top ? top : y_top;
                int cl_bot = y_bot > top + rows * rh ? top + rows * rh : y_bot;
                if (cl_bot > cl_top) {
                    orch_fill(dev, 0, cl_top, 3, cl_bot - cl_top, ORCH_COL_DIM);
                }
            }
        }

        /* Pass 4: White Key Dividers
         * - Natural halftones (E-F at k=3, B-C at k=0 and k=7) have NO black key:
         *   seam runs full width across the keyboard (0 to kw - 1).
         * - Other white keys (C-D, D-E, F-G, G-A, A-B):
         *   seam runs from black key tip (bw) to front edge (kw - 1). */
        for (k = 0; k < 8; k++) {
            int dy = y_c_bot - (k * 12 * rh) / 7;
            if (dy < top || dy >= top + rows * rh) continue;

            if (k == 0 || k == 3 || k == 7) {
                /* Full-width Natural Halftone Divider */
                orch_fill(dev, 0, dy, kw - 1, 1, 0xFF657187u);
            } else {
                /* Front White Key Divider extending from black key center */
                orch_fill(dev, bw, dy, kw - bw - 1, 1, 0xFFB0B9C8u);
            }
        }

        /* Pass 5: Raised 3D Ebony Black Keys
         * Centered directly on the dividers between white keys:
         * k=1 (C# between C & D), k=2 (D# between D & E),
         * k=4 (F# between F & G), k=5 (G# between G & A), k=6 (A# between A & B) */
        for (int b = 0; b < 5; b++) {
            int k = blk_k[b];
            int key = o + blk_note[b];
            int dy = y_c_bot - (k * 12 * rh) / 7;
            int by = dy - bh / 2;
            if (by + bh < top || by >= top + rows * rh) continue;

            if (key == s_audition_key) {
                orch_fill(dev, 0, by, bw, bh, ORCH_COL_CYAN);
            } else {
                orch_fill(dev, 0, by, bw, bh, 0xFF14171Eu);           /* ebony core */
                orch_fill(dev, 0, by, bw - 2, 1, 0xFF4A546Au);         /* top specular highlight */
                orch_fill(dev, bw - 2, by + 1, 2, bh - 1, 0xFF242A38u); /* 3D right bevel */
                orch_fill(dev, 0, by + bh - 1, bw, 1, 0xFF0A0D12u);     /* bottom drop shadow */
            }

            /* Out of track pitch range indicator on black key */
            if (key < lo || key > hi) {
                orch_fill(dev, 0, by, 3, bh, ORCH_COL_DIM);
            }
        }
    }
}

static void paint_bars(GDEV *dev, int gx, int top, int gw, int gh, uint32_t vis, uint32_t bar, uint32_t beat)
{
    uint32_t t;
    int i, px, x, on;
    orch_check(dev != 0, __LINE__);
    orch_check(gw > 0 && vis > 0u, __LINE__);
    if (!dev || gw <= 0 || vis == 0u || bar == 0u || beat == 0u) return;
    if (beat > bar) beat = bar;
    px = (int)(((uint64_t)beat * (uint64_t)gw) / vis);
    t = s_t0 - (s_t0 % beat);
    for (i = 0; i < 200; i++) {
        if (t >= s_t0 + vis) break;
        if (t < s_t0) {
            if (t > 4294967295u - beat) break;
            t += beat;
            continue;
        }
        on = (t % bar) == 0u;
        x = x_of(gx, gw, vis, t);
        if (on || px >= 6)
            orch_fill(dev, x, top, on ? 2 : 1, gh, on ? ORCH_COL_SILVER : ORCH_COL_GRID_LINE);
        if (t > 4294967295u - beat) break;
        t += beat;
    }
}

static void paint_grid(GDEV *dev, const OrchView *v, int gx, int top_y, int gw, int rh, int rows, uint32_t vis)
{
    uint32_t n;
    uint32_t i;
    int track;
    int lo = 0;
    int hi = 127;
    orch_check(dev != 0, __LINE__);
    orch_check(v != 0, __LINE__);
    orch_check(rows >= 6 && rows <= 48, __LINE__);
    if (!dev || !v) return;
    track = (int)v->selected;
    if (track < 0 || track >= (int)v->track_count) return;
    if (track < 64) {
        const OrchTrack *tr = &v->trk[track];
        lo = (int)tr->key_lo;
        hi = (int)tr->key_hi;
    }
    n = v->event_count;
    if (n > 4096u) n = 4096u;
    for (i = 0; i < n; i++) {
        const OrchEvent *e = &v->ev[i];
        int row;
        int x0, x1, hh, top;
        uint32_t a, b, dur;
        uint8_t vel;
        uint8_t key;
        uint8_t et;
        COLOR ink;
        et = e->track;
        vel = e->vel;
        if ((int)et != track || vel == 0) continue;
        key = e->key;
        if ((int)key < s_key0 || (int)key >= s_key0 + rows) continue;
        a = e->start_tick;
        dur = e->dur_tick;
        b = a + (dur ? dur : 1u);
        if (b <= s_t0 || a >= s_t0 + vis) continue;
        row = (s_key0 + rows - 1) - (int)key;
        x0 = x_of(gx, gw, vis, a < s_t0 ? s_t0 : a);
        x1 = x_of(gx, gw, vis, b > s_t0 + vis ? s_t0 + vis : b);
        if (x1 < x0 + 3) x1 = x0 + 3;
        hh = (rh - 2) / 3;
        if (hh < 2) hh = 2;
        hh += ((rh - 2 - hh) * (int)vel) / 127;
        if (hh > rh - 2) hh = rh - 2;
        top = top_y + row * rh + (rh - hh) / 2;
        ink = (vel >= 100) ? ORCH_COL_WHITE : ORCH_COL_AMBER;
        if ((int)key < lo || (int)key > hi) ink = ORCH_COL_DIM;
        if ((int)i == s_pick) ink = ORCH_COL_CYAN;
        orch_fill(dev, x0, top, x1 - x0, hh, ink);
        orch_fill(dev, x1 - 1, top, 1, hh, ORCH_COL_INSET_BG);
    }
}

static void take_home(const OrchView *v, const PianoGeom *g)
{
    uint32_t h;
    orch_check(v != 0, __LINE__);
    orch_check(s_pick >= -1 && s_pick < 4096, __LINE__);
    if (!v) return;
    h = v->home_gen;
    if (h == s_home) return;
    s_home = h;
    s_t0 = 0;
    s_hold = 0;
    s_pick = -1;
    s_fit_tr = -2;
    s_drag = DRAG_OFF;
    if (g) cmd_auto_scale(v, g->body, g->bar);
}

static void time_follow(const OrchView *v, const PianoGeom *g)
{
    uint32_t now, max0, vis, end;
    int chasing, k;
    orch_check(v != 0, __LINE__);
    orch_check(g != 0, __LINE__);
    if (!v || !g) return;
    take_home(v, g);
    fit_pitch(v, g->rows, g->bar);
    vis = g->vis;
    end = g->end;
    if (vis == 0u) return;
    if (v->state == ORCH_GST_PLAYING && s_seen_state != ORCH_GST_PLAYING)
        s_hold = 0;
    s_seen_state = v->state;
    chasing = (v->state == ORCH_GST_PLAYING && s_follow && !s_hold && s_drag != DRAG_H);
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

static void piano_paint(WND *wnd, GDEV *dev)
{
    PianoGeom g;
    OrchView *v = gst_view();
    int w, h, px;
    uint32_t now;
    orch_check(dev != 0, __LINE__);
    orch_check(v != 0, __LINE__);
    if (!dev || !v) return;
    w = (int)dev->width;
    h = (int)dev->height;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    geom_of(v, w, h, &g);
    time_follow(v, &g);
    clamp_key(g.rows);
    orch_fill(dev, 0, 0, w, h, ORCH_COL_CANVAS);
    paint_tools(dev, v, g.mode);
    paint_part(dev, v, w, g.mode);
    if (g.mode != ORCH_LAY_MIN) paint_ruler(dev, g.gx, g.gw, g.bar, g.vis);
    paint_keys(dev, v, g.gx, g.top, g.gw, g.rh, g.rows, g.kw, g.mode);
    paint_bars(dev, g.gx, g.top, g.gw, g.rows * g.rh, g.vis, g.bar, g.beat);
    paint_grid(dev, v, g.gx, g.top, g.gw, g.rh, g.rows, g.vis);
    paint_vsb(dev, &g);
    paint_hsb(dev, &g);
    now = orch_now_tick(v);
    if (now >= s_t0 && now <= s_t0 + g.vis) {
        px = x_of(g.gx, g.gw, g.vis, now);
        if (px > g.gx + g.gw - 2) px = g.gx + g.gw - 2;
        orch_fill(dev, px, g.top, 2, g.rows * g.rh, ORCH_COL_PLAYHEAD);
    }
    (void)wnd;
}

static int tool_click(const OrchView *v, int x, int y, int w, int h, int mode)
{
    orch_check(v != 0, __LINE__);
    orch_check(mode <= ORCH_LAY_MIN, __LINE__);
    if (!v || y > 28) return 0;
    if (mode == ORCH_LAY_MIN) {
        if (x < 34) orch_desk_tool(ORCH_TOOL_SELECT);
        else if (x < 60) orch_desk_tool(ORCH_TOOL_PENCIL);
        else if (x < 86) orch_desk_tool(ORCH_TOOL_ERASE);
        else if (x < 116) cmd_auto_scale(v, h - 32 - SB, orch_q(v, ORCH_SNAP_BAR));
        else if (x < 146) { s_follow = !s_follow; s_hold = 0; }
        else if (w >= 200 && x >= 146 && x < 170) step_track(v, -1);
        else if (w >= 200 && x >= 170 && x < 194) step_track(v, 1);
        else return 0;
        return 1;
    }
    if (x < 48) orch_desk_tool(ORCH_TOOL_SELECT);
    else if (x < 88) orch_desk_tool(ORCH_TOOL_PENCIL);
    else if (x < 128) orch_desk_tool(ORCH_TOOL_ERASE);
    else if (x < 174) orch_desk_snap_cycle();
    else if (x < 220) (void)orch_undo_apply();
    else if (x < 258) cmd_auto_scale(v, h - 52 - SB, orch_q(v, ORCH_SNAP_BAR));
    else if (x < 284) vzoom_by(-1);
    else if (x < 310) vzoom_by(1);
    else if (x < 346) { s_follow = !s_follow; s_hold = 0; }
    else if (w >= 440 && x >= w - 54 && x < w - 28) step_track(v, -1);
    else if (w >= 440 && x >= w - 28) step_track(v, 1);
    else return 0;
    return 1;
}

static void ruler_page(const OrchView *v, int x, const PianoGeom *g)
{
    uint32_t max0, bar, vis, end;
    int gw, gx, x0;
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

static void vsb_click(const PianoGeom *g, int y)
{
    int ty = 0, th = 0, live, rows;
    orch_check(g != 0, __LINE__);
    orch_check(s_drag >= DRAG_OFF && s_drag <= DRAG_H, __LINE__);
    if (!g) return;
    rows = g->rows;
    live = thumb_v(g, &ty, &th);
    if (!live) return;
    if (y >= ty && y < ty + th) {
        s_drag = DRAG_V;
        s_anchor = y;
        s_anchor_key = s_key0;
        return;
    }
    if (y < ty) s_key0 += rows;
    else s_key0 -= rows;
    clamp_key(rows);
}

static void hsb_click(const PianoGeom *g, int x)
{
    int tx = 0, tw = 0, live;
    uint32_t max0, vis, end, n;
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

static void piano_edit(const OrchView *v, int key, int x, int gx, int gw, uint32_t vis)
{
    uint32_t tick, q;
    int track, hit;
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

static void piano_drag(int x, int y, int w, int h)
{
    OrchView *v;
    PianoGeom g;
    int ty = 0, th = 0, tx = 0, tw = 0;
    int travel, key;
    int64_t t;
    uint32_t max0, end, vis;
    orch_check(s_drag == DRAG_V || s_drag == DRAG_H, __LINE__);
    orch_check(w > 0 && h > 0, __LINE__);
    v = gst_view();
    if (!v || w < 1 || h < 1) return;
    geom_of(v, w, h, &g);
    if (s_drag == DRAG_V) {
        (void)thumb_v(&g, &ty, &th);
        travel = g.body - th;
        if (travel < 1) travel = 1;
        key = s_anchor_key - ((y - s_anchor) * g.kmax) / travel;
        if (key < 0) key = 0;
        if (key > g.kmax) key = g.kmax;
        s_key0 = key;
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
    PianoGeom g;
    uint32_t now, vis;
    int was;
    orch_check(s_drag >= DRAG_OFF, __LINE__);
    orch_check(s_drag <= DRAG_H, __LINE__);
    was = s_drag;
    s_drag = DRAG_OFF;
    s_audition_key = -1;
    if (was != DRAG_H || w < 1 || h < 1) return;
    v = gst_view();
    if (!v) return;
    geom_of(v, w, h, &g);
    vis = g.vis;
    now = orch_now_tick(v);
    if (now >= s_t0 && now <= s_t0 + vis) s_hold = 0;
}

static void piano_click(int x, int y, int w, int h)
{
    OrchView *v = gst_view();
    PianoGeom g;
    int key;
    orch_check(v != 0, __LINE__);
    orch_check(w > 0 && h > 0, __LINE__);
    if (!v || w < 1 || h < 1) return;
    geom_of(v, w, h, &g);
    clamp_key(g.rows);
    if (tool_click(v, x, y, w, h, g.mode)) return;
    if (g.mode != ORCH_LAY_MIN && orch_inside(x, y, g.gx, 32, g.gw, 18)) {
        ruler_page(v, x, &g);
        return;
    }
    if (orch_inside(x, y, g.vx, g.top, SB, g.body)) {
        vsb_click(&g, y);
        return;
    }
    if (orch_inside(x, y, g.gx, g.hy, g.gw, SB)) {
        hsb_click(&g, x);
        return;
    }
    if (x < g.kw) {
        if (y >= g.top && y < g.top + g.rows * g.rh) {
            key = piano_key_at(x, y, g.top, g.rh, g.rows, g.kw);
            if (key >= 0 && key < 128) {
                audition_note(v, key);
            }
        }
        return;
    }
    if (orch_inside(x, y, g.gx, g.top, g.gw, g.rows * g.rh)) {
        key = s_key0 + g.rows - 1 - (y - g.top) / g.rh;
        if (key >= 0 && key < 128) {
            piano_edit(v, key, x, g.gx, g.gw, g.vis);
        }
        return;
    }
}

static void piano_event(WND *wnd, const EVT *evt)
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
        piano_drag(x, y, w, h);
        (void)inval_wnd(wnd);
        return;
    }
    if (evt->type != EV_BUT_DOWN) return;
    s_drag = DRAG_OFF;
    piano_click(x, y, w, h);
    (void)inval_wnd(wnd);
}

static void on_destroy(WND *wnd)
{
    orch_check(wnd != 0, __LINE__);
    orch_check(s_wnd == 0 || s_wnd == wnd, __LINE__);
    (void)wnd;
    s_wnd = 0;
    s_drag = DRAG_OFF;
    s_audition_key = -1;
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
    cmd_auto_scale(v, 480 - 52 - SB, orch_q(v, ORCH_SNAP_BAR));
    return w;
}
