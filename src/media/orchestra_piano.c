#include "orchestra_desk.h"
#include <apps/orch_chk.h>

static WND *s_wnd;
static uint32_t s_t0;
static int s_key0 = 48;
static int s_pick = -1;
static int s_zoom = 2;
static int s_hold;
static int s_fit_tr = -2;
static uint32_t s_home;
static uint8_t s_seen_state;

static const char *zoom_label(void)
{
    static const char *lb[5] = { "16", "8", "4", "2", "1" };
    orch_check(s_zoom >= 0 && s_zoom <= 4, __LINE__);
    orch_check(lb[2][0] == '4', __LINE__);
    if (s_zoom < 0 || s_zoom > 4) return "4";
    return lb[s_zoom];
}

static void zoom_by(int dir)
{
    orch_check(dir == -1 || dir == 1, __LINE__);
    orch_check(s_zoom >= 0 && s_zoom <= 4, __LINE__);
    if (dir < 0 && s_zoom > 0) s_zoom--;
    if (dir > 0 && s_zoom < 4) s_zoom++;
}

static int zoom_bars(int gw)
{
    int bars = 16 >> s_zoom;
    orch_check(s_zoom >= 0 && s_zoom <= 4, __LINE__);
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
    orch_check(rows >= 8 && rows <= 32, __LINE__);
    if (!v || rows < 8) return;
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
    orch_check(dev != 0, __LINE__);
    orch_check(v != 0, __LINE__);
    if (!dev || !v) return;
    if (mode == ORCH_LAY_MIN) {
        if (w < 168) return;
        orch_btn(dev, 108, 4, 24, 22, "[", 0);
        orch_btn(dev, 136, 4, 24, 22, "]", 0);
        return;
    }
    if (w < 400) return;
    x0 = w - 64;
    px = x0 - 312;
    if (px >= 48) {
        part_text(name, 32, v, px);
        if (name[0]) orch_text(dev, 308, 8, name, ORCH_COL_WHITE);
    }
    orch_btn(dev, x0, 4, 24, 22, "[", 0);
    orch_btn(dev, x0 + 32, 4, 24, 22, "]", 0);
}

static void paint_zoom(GDEV *dev, int gx, int gw, int mode)
{
    int x0;
    orch_check(dev != 0, __LINE__);
    orch_check(gw > 0 && gw < 8000, __LINE__);
    if (!dev || mode == ORCH_LAY_MIN || gw < 160) return;
    x0 = gx + gw - 92;
    orch_fill(dev, x0, 29, 28, 16, ORCH_COL_BTN_FACE);
    orch_text(dev, x0 + 2, 29, "Out", ORCH_COL_WHITE);
    orch_fill(dev, x0 + 30, 29, 28, 16, ORCH_COL_INSET_BG);
    orch_text(dev, x0 + 38, 29, zoom_label(), ORCH_COL_AMBER);
    orch_fill(dev, x0 + 60, 29, 28, 16, ORCH_COL_BTN_FACE);
    orch_text(dev, x0 + 68, 29, "In", ORCH_COL_WHITE);
}

static void paint_keys(GDEV *dev, const OrchView *v, int gx, int y0, int gw, int rh, int rows, int kw, int mode)
{
    int r;
    int lo = 0;
    int hi = 127;
    int track;
    orch_check(dev != 0, __LINE__);
    orch_check(rows >= 8 && rows <= 32, __LINE__);
    if (!dev || !v) return;
    track = (int)v->selected;
    if (track >= 0 && track < (int)v->track_count && track < 64) {
        const OrchTrack *tr = &v->trk[track];
        lo = (int)tr->key_lo;
        hi = (int)tr->key_hi;
    }
    for (r = 0; r < rows && r < 32; r++) {
        int key = s_key0 + rows - 1 - r;
        int y = y0 + r * rh;
        COLOR bg;
        if (key < 0 || key > 127) continue;
        bg = black_key(key) ? ORCH_COL_GRID_BG : ORCH_COL_ROW_B;
        if (key < lo || key > hi) bg = ORCH_COL_INSET_BG;
        orch_fill(dev, gx, y, gw, rh - 1, bg);
        paint_key(dev, key, 0, y, kw, rh, mode);
        if (key < lo || key > hi) orch_fill(dev, 0, y, 3, rh - 1, ORCH_COL_DIM);
    }
}

static void paint_bars(GDEV *dev, int gx, int y0, int gw, int gh, uint32_t vis, uint32_t bar, uint32_t beat, int mode)
{
    uint32_t t;
    int i;
    int px;
    char nb[8];
    orch_check(dev != 0, __LINE__);
    orch_check(gw > 0 && vis > 0u, __LINE__);
    if (!dev || gw <= 0 || vis == 0u || bar == 0u || beat == 0u) return;
    if (beat > bar) beat = bar;
    px = (int)(((uint64_t)beat * (uint64_t)gw) / vis);
    t = s_t0 - (s_t0 % beat);
    for (i = 0; i < 200; i++) {
        int x;
        int on;
        if (t >= s_t0 + vis) break;
        if (t < s_t0) {
            if (t > 4294967295u - beat) break;
            t += beat;
            continue;
        }
        on = (t % bar) == 0u;
        x = x_of(gx, gw, vis, t);
        if (on || px >= 6)
            orch_fill(dev, x, y0, on ? 2 : 1, gh, on ? ORCH_COL_SILVER : ORCH_COL_GRID_LINE);
        if (on && mode != ORCH_LAY_MIN && x < gx + gw - 120) {
            (void)orch_fmt_u(nb, 8, (unsigned)(t / bar) + 1u);
            orch_text(dev, x + 3, 30, nb, ORCH_COL_DIM);
        }
        if (t > 4294967295u - beat) break;
        t += beat;
    }
}

static void paint_grid(GDEV *dev, const OrchView *v, int gx, int y0, int gw, int rh, int rows, uint32_t vis)
{
    uint32_t n;
    uint32_t i;
    int track;
    int lo = 0;
    int hi = 127;
    orch_check(dev != 0, __LINE__);
    orch_check(v != 0, __LINE__);
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
        top = y0 + row * rh + (rh - hh) / 2;
        ink = (vel >= 100) ? ORCH_COL_WHITE : ORCH_COL_AMBER;
        if ((int)key < lo || (int)key > hi) ink = ORCH_COL_DIM;
        if ((int)i == s_pick) ink = ORCH_COL_CYAN;
        orch_fill(dev, x0, top, x1 - x0, hh, ink);
    }
}

static void time_follow(const OrchView *v, uint32_t vis, int rows, uint32_t bar);
static void piano_paint(WND *wnd, GDEV *dev)
{
    OrchView *v = gst_view();
    int w, h, mode, kw, rows, rh, y0, gx, gw;
    uint32_t bar, beat, vis, now;
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
    paint_part(dev, v, w, mode);
    y0 = 30;
    if (mode != ORCH_LAY_MIN) {
        orch_fill(dev, 0, 28, w, 16, ORCH_COL_PANEL_BG);
        y0 = 46;
    }
    kw = key_w(mode);
    rows = rows_of(h - y0);
    rh = (h - y0) / rows;
    if (rh < 8) rh = 8;
    gx = kw + 4;
    gw = w - gx - 4;
    if (gw < 8) gw = 8;
    bar = orch_q(v, ORCH_SNAP_BAR);
    if (bar == 0u) bar = 3840u;
    beat = orch_q(v, ORCH_SNAP_BEAT);
    if (beat == 0u) beat = 960u;
    vis = bar * (uint32_t)zoom_bars(gw);
    if (vis == 0u) vis = bar;
    time_follow(v, vis, rows, bar);
    clamp_key(rows);
    paint_zoom(dev, gx, gw, mode);
    paint_keys(dev, v, gx, y0, gw, rh, rows, kw, mode);
    paint_bars(dev, gx, y0, gw, rows * rh, vis, bar, beat, mode);
    paint_grid(dev, v, gx, y0, gw, rh, rows, vis);
    now = orch_now_tick(v);
    if (now >= s_t0 && now < s_t0 + vis)
        orch_fill(dev, x_of(gx, gw, vis, now), y0, 2, rows * rh, ORCH_COL_PLAYHEAD);
    (void)wnd;
}

static int tool_click(const OrchView *v, int x, int y, int w, int mode)
{
    orch_check(v != 0, __LINE__);
    orch_check(mode <= ORCH_LAY_MIN, __LINE__);
    if (!v || y > 28) return 0;
    if (mode == ORCH_LAY_MIN) {
        if (x < 40) orch_desk_tool(ORCH_TOOL_SELECT);
        else if (x < 72) orch_desk_tool(ORCH_TOOL_PENCIL);
        else if (x < 104) orch_desk_tool(ORCH_TOOL_ERASE);
        else if (w >= 168 && x < 136) step_track(v, -1);
        else if (w >= 168 && x < 164) step_track(v, 1);
        else return 0;
        return 1;
    }
    if (x < 64) orch_desk_tool(ORCH_TOOL_SELECT);
    else if (x < 120) orch_desk_tool(ORCH_TOOL_PENCIL);
    else if (x < 176) orch_desk_tool(ORCH_TOOL_ERASE);
    else if (x < 240) orch_desk_snap_cycle();
    else if (x < 304) (void)orch_undo_apply();
    else if (w >= 400 && x >= w - 64) step_track(v, x < w - 32 ? -1 : 1);
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
    s_hold = 1;
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
    s_hold = 0;
    s_pick = -1;
    s_fit_tr = -2;
}

static void time_follow(const OrchView *v, uint32_t vis, int rows, uint32_t bar)
{
    uint32_t now;
    int k;
    orch_check(v != 0, __LINE__);
    orch_check(vis > 0u, __LINE__);
    if (!v || vis == 0u) return;
    take_home(v);
    fit_pitch(v, rows, bar);
    if (v->state == ORCH_GST_PLAYING && s_seen_state != ORCH_GST_PLAYING)
        s_hold = 0;
    s_seen_state = v->state;
    if (v->state != ORCH_GST_PLAYING || s_hold) return;
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
    if (tool_click(v, x, y, w, mode)) return;
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
    if (gw < 8) gw = 8;
    bar = orch_q(v, ORCH_SNAP_BAR);
    if (bar == 0u) bar = 3840u;
    vis = bar * (uint32_t)zoom_bars(gw);
    if (mode != ORCH_LAY_MIN && y < y0 && y >= 28) {
        if (gw >= 160 && x >= gx + gw - 92) {
            if (x < gx + gw - 62) zoom_by(-1);
            else if (x >= gx + gw - 32) zoom_by(1);
            return;
        }
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
