#include "orchestra_desk.h"
#include <apps/orch_chk.h>

static WND *s_wnd;
static int s_bar;
static int s_pick = -1;
static int s_hold;
static uint32_t s_home;
static uint8_t s_seen_state;

enum { ND = 48 };

typedef struct Nd {
    int x, y, step, up, val, dot, idx, sharp;
    int tie, beat, bar_l, bar_r;
} Nd;

static Nd s_nd[ND];
static int s_nn;

static int dia(int midi)
{
    static const int8_t t[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
    int o;
    int p;
    orch_check(midi >= 0 && midi < 128, __LINE__);
    orch_check(midi < 256, __LINE__);
    if (midi < 0) midi = 0;
    if (midi > 127) midi = 127;
    o = midi / 12;
    p = midi - o * 12;
    return o * 7 + (int)t[p];
}

static int sharp_pc(int midi)
{
    int pc;
    orch_check(midi >= 0 && midi < 128, __LINE__);
    orch_check(midi < 256, __LINE__);
    pc = midi % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

static int staff_step(int midi, int bass)
{
    int anchor;
    orch_check(midi >= 0 && midi < 128, __LINE__);
    orch_check(bass == 0 || bass == 1, __LINE__);
    anchor = bass ? 62 : 71;
    return dia(midi) - dia(anchor);
}

static int midi_of(int step, int bass)
{
    int want;
    int m;
    int anchor;
    orch_check(step > -40 && step < 40, __LINE__);
    orch_check(bass == 0 || bass == 1, __LINE__);
    anchor = bass ? 62 : 71;
    want = dia(anchor) + step;
    for (m = 0; m < 128; m++) {
        int pc = m % 12;
        int natural = pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11;
        if (natural && dia(m) == want) return m;
    }
    return 60;
}

static int written(uint32_t dur, uint32_t bar, uint32_t pos, int *dot)
{
    static const uint32_t w[5] = { 3840u, 1920u, 960u, 480u, 240u };
    int i;
    orch_check(dot != 0, __LINE__);
    orch_check(bar > 0u, __LINE__);
    if (dot) *dot = 0;
    for (i = 0; i < 5; i++) {
        if (dur == w[i]) return (int)w[i];
        if (dur * 2u == w[i] * 3u && pos + dur <= bar) {
            if (dot) *dot = 1;
            return (int)w[i];
        }
    }
    for (i = 0; i < 5; i++) if (dur >= w[i]) return (int)w[i];
    return 240;
}

static int bar_count(int mode, int inner)
{
    int bars = 4;
    int i;
    orch_check(mode >= ORCH_LAY_WIDE && mode <= ORCH_LAY_MIN, __LINE__);
    orch_check(inner > -100 && inner < 8000, __LINE__);
    if (mode == ORCH_LAY_MIN) bars = 2;
    for (i = 0; i < 4; i++) {
        if (bars <= 1) break;
        if (inner / bars >= 56) break;
        bars--;
    }
    if (bars < 1) bars = 1;
    return bars;
}

static void draw_clef(GDEV *dev, int x, int mid, int dy, int bass)
{
    orch_check(dev != 0, __LINE__);
    orch_check(dy > 0 && dy < 40, __LINE__);
    if (bass) {
        orch_fill(dev, x + 4, mid - dy * 2, 10, dy * 3, ORCH_COL_SILVER);
        orch_fill(dev, x + 16, mid - dy, 3, 3, ORCH_COL_WHITE);
        orch_fill(dev, x + 16, mid + dy, 3, 3, ORCH_COL_WHITE);
        return;
    }
    orch_fill(dev, x + 10, mid - dy * 5, 2, dy * 8, ORCH_COL_SILVER);
    orch_fill(dev, x + 2, mid - dy, 12, dy * 3, ORCH_COL_WHITE);
    orch_fill(dev, x + 4, mid - dy * 3, 8, dy * 2, ORCH_COL_SILVER);
}

static void ledgers(GDEV *dev, int x, int mid, int dy, int step)
{
    int k;
    orch_check(dev != 0, __LINE__);
    orch_check(dy > 0 && dy < 40, __LINE__);
    for (k = 6; k <= 16; k += 2) {
        if (step >= k)
            orch_fill(dev, x - 4, mid - k * dy, 18, 1, ORCH_COL_SILVER);
        if (step <= -k)
            orch_fill(dev, x - 4, mid - (-k) * dy, 18, 1, ORCH_COL_SILVER);
    }
}

static void draw_tie(GDEV *dev, int tie, int x0, int y0, int hw, int bar_l, int bar_r)
{
    int span;
    orch_check(dev != 0, __LINE__);
    orch_check(hw > 0 && hw < 80, __LINE__);
    if (!dev) return;
    if (tie & 1) {
        span = bar_r - (x0 + hw);
        if (span > 0) orch_fill(dev, x0 + hw, y0, span, 1, ORCH_COL_SILVER);
    }
    if (tie & 2) {
        span = x0 - bar_l;
        if (span > 0) orch_fill(dev, bar_l, y0, span, 1, ORCH_COL_SILVER);
    }
}

static void draw_head(GDEV *dev, const Nd *n, int dy)
{
    int hw;
    int hh;
    int x0;
    int y0;
    int step;
    int tie;
    orch_check(dev != 0, __LINE__);
    orch_check(n != 0, __LINE__);
    if (!dev || !n) return;
    hw = dy + 2;
    hh = dy;
    if (hw < 4) hw = 4;
    if (hh < 3) hh = 3;
    x0 = n->x;
    y0 = n->y;
    step = n->step;
    tie = n->tie;
    orch_fill(dev, x0, y0 - hh / 2, hw, hh, ORCH_COL_WHITE);
    if (n->sharp) orch_text(dev, x0 - 8, y0 - 6, "#", ORCH_COL_SILVER);
    if (n->up) orch_fill(dev, x0 + hw - 1, y0 - dy * 7, 1, dy * 7, ORCH_COL_WHITE);
    else orch_fill(dev, x0, y0, 1, dy * 7, ORCH_COL_WHITE);
    if (n->dot) orch_fill(dev, x0 + hw + 2, y0, 2, 2, ORCH_COL_WHITE);
    {
        int bl = n->bar_l;
        int br = n->bar_r;
        draw_tie(dev, tie, x0, y0 + 2, hw, bl, br);
    }
    ledgers(dev, x0, y0 + step * dy, dy, step);
}

static int tie_end(int gx, int bar_w, int bars, int b, uint32_t end,
                  uint32_t t0, uint32_t t1, uint32_t bar)
{
    uint32_t last;
    int b1;
    orch_check(bar > 0u, __LINE__);
    orch_check(bars > 0 && bars <= 8, __LINE__);
    last = end - 1u;
    b1 = b + 1;
    if (last >= t1) b1 = bars;
    else if (last >= t0) b1 = (int)((last - t0) / bar) + 1;
    if (b1 > bars) b1 = bars;
    return gx + b1 * bar_w;
}

static void push_nd(int bass, int key, int idx, uint32_t a, uint32_t end,
                    int gx, int bar_w, int bars, int mid, int dy,
                    uint32_t bar, uint32_t t0, uint32_t t1, uint32_t beat_q)
{
    uint32_t pos;
    uint32_t show;
    uint32_t room_t;
    int b;
    int tie;
    int step;
    int bl;
    int room;
    int dot = 0;
    Nd *d;
    orch_check(bar > 0u && beat_q > 0u, __LINE__);
    orch_check(s_nn >= 0 && s_nn < ND, __LINE__);
    if (s_nn < 0 || s_nn >= ND || bar == 0u) return;
    if (a < t0) {
        b = 0;
        pos = 0;
        tie = 2;
        show = end - t0;
        if (end > t0 + bar) tie = 3;
        if (show > bar) show = bar;
    } else {
        b = (int)((a - t0) / bar);
        pos = (a - t0) - (uint32_t)b * bar;
        if (pos >= bar) pos = 0;
        room_t = bar - pos;
        show = end - a;
        tie = 0;
        if (end > t0 + (uint32_t)(b + 1) * bar) tie = 1;
        if (show > room_t) show = room_t;
    }
    if (b < 0) b = 0;
    if (b >= bars) b = bars - 1;
    step = staff_step(key, bass);
    bl = gx + b * bar_w;
    room = bar_w - 16;
    if (room < 8) room = 8;
    d = &s_nd[s_nn];
    d->step = step;
    d->up = step < 0;
    d->val = written(show, bar, pos, &dot);
    d->dot = dot;
    d->idx = idx;
    d->sharp = sharp_pc(key);
    d->tie = tie;
    d->beat = (int)(a / beat_q);
    d->y = mid - step * dy;
    d->x = bl + (int)((pos * (uint32_t)room) / bar);
    d->bar_l = (tie & 2) ? 8 : bl;
    d->bar_r = bl + bar_w;
    if ((tie & 1) != 0)
        d->bar_r = tie_end(gx, bar_w, bars, b, end, t0, t1, bar);
    s_nn++;
}

static void collect(const OrchView *v, int track, int bass, int gx, int bar_w, int bars, int mid, int dy, uint32_t bar)
{
    uint32_t n;
    uint32_t i;
    uint32_t t0;
    uint32_t t1;
    uint32_t beat_q;
    orch_check(v != 0, __LINE__);
    orch_check(bar > 0u && bars > 0 && bars <= 8, __LINE__);
    s_nn = 0;
    if (!v || track < 0 || bar == 0u) return;
    t0 = (uint32_t)s_bar * bar;
    t1 = t0 + (uint32_t)bars * bar;
    beat_q = orch_q(v, ORCH_SNAP_BEAT);
    if (beat_q == 0u) beat_q = 960u;
    n = v->event_count;
    if (n > 4096u) n = 4096u;
    for (i = 0; i < n && s_nn < ND; i++) {
        const OrchEvent *e = &v->ev[i];
        uint32_t a;
        uint32_t end;
        if ((int)e->track != track || e->vel == 0) continue;
        a = e->start_tick;
        end = a + e->dur_tick;
        if (end < a) end = 0xffffffffu;
        if (end <= t0 || a >= t1) continue;
        push_nd(bass, (int)e->key, (int)i, a, end, gx, bar_w, bars, mid, dy, bar, t0, t1, beat_q);
    }
}

static void draw_beams(GDEV *dev, int dy)
{
    int i;
    int run;
    orch_check(dev != 0, __LINE__);
    orch_check(dy > 0 && dy < 40, __LINE__);
    run = -1;
    for (i = 0; i < s_nn && i < ND; i++) {
        int beam = s_nd[i].val <= 480;
        if (!beam) {
            run = -1;
            continue;
        }
        if (run < 0) run = i;
        if (i + 1 < s_nn && s_nd[i + 1].val <= 480) {
            int up0 = s_nd[i].up;
            int up1 = s_nd[i + 1].up;
            int b0 = s_nd[i].beat;
            int b1 = s_nd[i + 1].beat;
            int x0 = s_nd[i].x;
            int x1 = s_nd[i + 1].x;
            if (up1 == up0 && b1 == b0 && x1 - x0 < 80) continue;
        }
        if (i > run) {
            int y = s_nd[run].up ? s_nd[run].y - dy * 7 : s_nd[run].y + dy * 7;
            orch_fill(dev, s_nd[run].x, y, s_nd[i].x - s_nd[run].x + 2, 2, ORCH_COL_WHITE);
        }
        run = -1;
    }
}

static int beat_busy(const OrchView *v, int track, uint32_t tick, uint32_t q)
{
    uint32_t n;
    uint32_t i;
    orch_check(v != 0, __LINE__);
    orch_check(q > 0u, __LINE__);
    if (!v || q == 0u || track < 0) return 0;
    n = v->event_count;
    if (n > 4096u) n = 4096u;
    for (i = 0; i < n; i++) {
        const OrchEvent *e = &v->ev[i];
        uint32_t a;
        uint32_t end;
        if ((int)e->track != track || e->vel == 0) continue;
        a = e->start_tick;
        end = a + e->dur_tick;
        if (end < a) end = 0xffffffffu;
        if (a < tick + q && end > tick) return 1;
    }
    return 0;
}

static void draw_rests(GDEV *dev, const OrchView *v, int track, int gx, int bar_w, int bars, int mid, uint32_t bar)
{
    int b;
    int beat;
    int beats;
    uint32_t beat_q;
    uint32_t t0;
    orch_check(dev != 0, __LINE__);
    orch_check(v != 0, __LINE__);
    if (!dev || !v || track < 0 || bar == 0u) return;
    beat_q = orch_q(v, ORCH_SNAP_BEAT);
    if (beat_q == 0u) beat_q = 960u;
    beats = (int)(bar / beat_q);
    if (beats < 1) beats = 1;
    if (beats > 12) beats = 12;
    t0 = (uint32_t)s_bar * bar;
    for (b = 0; b < bars && b < 8; b++) {
        for (beat = 0; beat < beats && beat < 12; beat++) {
            uint32_t tick = t0 + (uint32_t)b * bar + (uint32_t)beat * beat_q;
            int x = gx + b * bar_w + 4;
            if (beat_busy(v, track, tick, beat_q)) continue;
            x += (int)(((uint32_t)beat * beat_q * (uint32_t)(bar_w - 8)) / bar);
            orch_fill(dev, x, mid - 3, 7, 6, ORCH_COL_DIM);
        }
    }
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
    s_bar = 0;
    s_hold = 0;
    s_pick = -1;
}

static void time_follow(const OrchView *v, int bars, uint32_t bar)
{
    uint32_t now;
    uint32_t vis;
    int k;
    orch_check(v != 0, __LINE__);
    orch_check(bar > 0u && bars > 0 && bars <= 8, __LINE__);
    if (!v || bar == 0u || bars < 1) return;
    take_home(v);
    if (v->state == ORCH_GST_PLAYING && s_seen_state != ORCH_GST_PLAYING)
        s_hold = 0;
    s_seen_state = v->state;
    if (v->state != ORCH_GST_PLAYING || s_hold) return;
    if (s_bar < 0) s_bar = 0;
    vis = bar * (uint32_t)bars;
    now = orch_now_tick(v);
    for (k = 0; k < 256 && now < (uint32_t)s_bar * bar; k++) {
        if (s_bar < bars) {
            s_bar = 0;
            break;
        }
        s_bar -= bars;
    }
    for (k = 0; k < 256 && now >= (uint32_t)s_bar * bar + vis; k++) {
        if (s_bar > 100000 - bars) break;
        s_bar += bars;
    }
}

static void draw_system(GDEV *dev, const OrchView *v, int w, int h, int mode)
{
    int bass;
    int track;
    int dy;
    int mid;
    int bars;
    int gx;
    int inner;
    int bar_w;
    int i;
    uint32_t bar;
    uint32_t now;
    orch_check(dev != 0, __LINE__);
    orch_check(v != 0, __LINE__);
    if (!dev || !v) return;
    track = (int)v->selected;
    bass = 0;
    if (track >= 0 && track < (int)v->track_count) bass = v->trk[track].clef ? 1 : 0;
    dy = (mode == ORCH_LAY_MIN) ? 3 : 5;
    mid = h / 2;
    bars = bar_count(mode, w - 64);
    gx = 48;
    inner = w - gx - 8;
    if (inner < 40) inner = 40;
    bar_w = inner / bars;
    bar = orch_q(v, ORCH_SNAP_BAR);
    if (bar == 0u) bar = 3840u;
    time_follow(v, bars, bar);
    for (i = -4; i <= 4; i += 2)
        orch_fill(dev, 8, mid - i * dy, w - 16, 1, ORCH_COL_SILVER);
    draw_clef(dev, 8, mid, dy, bass);
    for (i = 1; i < bars && i < 8; i++)
        orch_fill(dev, gx + i * bar_w, mid - 4 * dy, 1, 8 * dy, ORCH_COL_SILVER);
    orch_fill(dev, gx + bars * bar_w, mid - 4 * dy, 2, 8 * dy, ORCH_COL_WHITE);
    if (mode == ORCH_LAY_WIDE) {
        for (i = 0; i < bars && i < 8; i++) {
            char nb[8];
            (void)orch_fmt_u(nb, 8, (unsigned)(s_bar + i + 1));
            orch_text(dev, gx + i * bar_w + 2, mid - 4 * dy - 14, nb, ORCH_COL_DIM);
        }
    }
    collect(v, track, bass, gx, bar_w, bars, mid, dy, bar);
    for (i = 0; i < s_nn && i < ND; i++) {
        if (s_nd[i].idx == s_pick) orch_fill(dev, s_nd[i].x - 1, s_nd[i].y - dy, dy + 6, dy, ORCH_COL_CYAN);
        draw_head(dev, &s_nd[i], dy);
    }
    draw_beams(dev, dy);
    draw_rests(dev, v, track, gx, bar_w, bars, mid, bar);
    now = orch_now_tick(v);
    if (bar > 0u && now >= (uint32_t)s_bar * bar && now < (uint32_t)(s_bar + bars) * bar) {
        int bx = gx + (int)(((now - (uint32_t)s_bar * bar) * (uint32_t)(bars * bar_w)) / (bar * (uint32_t)bars));
        orch_fill(dev, bx, mid - 4 * dy, 2, 8 * dy, ORCH_COL_PLAYHEAD);
    }
    if (mode != ORCH_LAY_MIN) {
        orch_btn(dev, 8, 4, 24, 22, "<", 0);
        orch_btn(dev, 36, 4, 24, 22, ">", 0);
    }
}

static void score_paint(WND *wnd, GDEV *dev)
{
    OrchView *v = gst_view();
    int w;
    int h;
    orch_check(dev != 0, __LINE__);
    orch_check(v != 0, __LINE__);
    if (!dev || !v) return;
    w = (int)dev->width;
    h = (int)dev->height;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    orch_fill(dev, 0, 0, w, h, ORCH_COL_GRID_BG);
    draw_system(dev, v, w, h, orch_lay_mode(w, h));
    (void)wnd;
}

static int turn_page(int x, int y, int mode)
{
    orch_check(mode >= ORCH_LAY_WIDE && mode <= ORCH_LAY_MIN, __LINE__);
    orch_check(y > -4000 && y < 8000, __LINE__);
    if (mode == ORCH_LAY_MIN || y >= 28) return 0;
    if (x < 64) {
        s_hold = 1;
        if (x < 36) {
            if (s_bar > 0) s_bar--;
        } else if (s_bar < 400) {
            s_bar++;
        }
    }
    return 1;
}

static void score_edit(const OrchView *v, int x, int y, int w, int h, int mode)
{
    int bass;
    int track;
    int dy;
    int mid;
    int bars;
    int gx;
    int bar_w;
    int step;
    int b;
    uint32_t bar;
    uint32_t pos;
    uint32_t tick;
    uint32_t q;
    int key;
    int hit;
    orch_check(v != 0, __LINE__);
    orch_check(w > 0 && h > 0, __LINE__);
    if (turn_page(x, y, mode)) return;
    track = (int)v->selected;
    if (track < 0 || track >= (int)v->track_count) return;
    bass = v->trk[track].clef ? 1 : 0;
    dy = (mode == ORCH_LAY_MIN) ? 3 : 5;
    mid = h / 2;
    if (dy == 0) return;
    step = (mid - y) / dy;
    if (step < -16) step = -16;
    if (step > 16) step = 16;
    bars = bar_count(mode, w - 64);
    gx = 48;
    bar_w = (w - gx - 8) / bars;
    if (bar_w < 8) return;
    bar = orch_q(v, ORCH_SNAP_BAR);
    if (bar == 0u) bar = 3840u;
    b = (x - gx) / bar_w;
    if (b < 0) b = 0;
    if (b >= bars) b = bars - 1;
    pos = (uint32_t)(x - gx - b * bar_w);
    if (bar_w > 16) pos = (pos * bar) / (uint32_t)bar_w;
    else pos = 0;
    if (pos >= bar) pos = bar - 1u;
    if (v->snap > ORCH_SNAP_16TH) return;
    q = orch_q(v, (int)v->snap);
    if (q == 0u) q = 240u;
    tick = (uint32_t)(s_bar + b) * bar + pos;
    tick = orch_snap_tick(v, tick);
    key = midi_of(step, bass);
    if (v->tool == ORCH_TOOL_PENCIL) {
        (void)orch_event_put(track, (uint8_t)key, 100, tick, q);
        return;
    }
    hit = orch_hit_event(track, tick, key, 0);
    if (v->tool == ORCH_TOOL_ERASE) {
        if (hit >= 0) (void)orch_event_del(hit);
        if (hit == s_pick) s_pick = -1;
        return;
    }
    s_pick = hit;
}

static void score_event(WND *wnd, const EVT *evt)
{
    int x, y, w, h;
    OrchView *v;
    orch_check(wnd != 0, __LINE__);
    orch_check(evt != 0, __LINE__);
    if (!wnd || !evt || evt->type != EV_BUT_DOWN) return;
    v = gst_view();
    if (!v) return;
    orch_client(wnd, evt, &x, &y, &w, &h);
    score_edit(v, x, y, w, h, orch_lay_mode(w, h));
    (void)inval_wnd(wnd);
}

static void on_destroy(WND *wnd)
{
    orch_check(wnd != 0, __LINE__);
    orch_check(s_wnd == 0 || s_wnd == wnd, __LINE__);
    (void)wnd;
    s_wnd = 0;
}

void orch_score_inval(void)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (s_wnd) (void)inval_wnd(s_wnd);
}

WND *orch_open_score(void)
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
    w = opn_wnd("Score", 140, 120, 720, 320, attr);
    if (!w) return 0;
    w->paint = score_paint;
    w->event_handler = score_event;
    w->destroy = on_destroy;
    w->menu_open = orch_menu_no;
    s_wnd = w;
    return w;
}
