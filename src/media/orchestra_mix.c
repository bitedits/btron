#include "orchestra_desk.h"
#include <apps/orch_chk.h>

static WND *s_wnd;
static int s_bank;
static int s_drag;
static int s_anchor;
static int s_anchor_bank;

enum { C_AUX = -2, C_MASTER = -1, SB = 16, DRAG_OFF = 0, DRAG_H = 1 };

static const char *ins_name(int id)
{
    static const char *N[9] = {
        "Byp", "EQ", "Sat", "Dly", "Rev", "Cho", "Gate", "Trm", "Lim"
    };
    orch_check(id < 16, __LINE__);
    orch_check(id > -2, __LINE__);
    if (id < 0 || id > 8) return N[0];
    return N[id];
}

static int col_n(int w, int mode, int *sw)
{
    int aux = 64;
    int mas = 72;
    int n;
    orch_check(w > 0 && w < 8000, __LINE__);
    orch_check(mode >= ORCH_LAY_WIDE && mode <= ORCH_LAY_MIN, __LINE__);
    *sw = (mode == ORCH_LAY_MIN) ? 52 : 68;
    n = (w - aux - mas - 28) / *sw;
    if (n < 1) n = 1;
    if (n > 12) n = 12;
    return n;
}

static void nudge_u8(int field, int cur, int dir, int step, int lo, int hi)
{
    int v;
    orch_check(field >= 0 && field <= ORCH_F_PROGRAM, __LINE__);
    orch_check(step > 0 && step < 64, __LINE__);
    v = cur + dir * step;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    (void)orch_track_set_u8(field, v);
}

static void paint_fader(GDEV *dev, int x, int y, int w, int h, int val)
{
    int fill;
    orch_check(dev != 0, __LINE__);
    orch_check(val >= 0 && val <= 127, __LINE__);
    if (val < 0) val = 0;
    if (val > 127) val = 127;
    orch_fill(dev, x, y, w, h, ORCH_COL_INSET_BG);
    fill = (h * val) / 127;
    if (fill > 0) orch_fill(dev, x + 2, y + h - fill, w - 4, fill, ORCH_COL_GREEN);
}

static void paint_track(GDEV *dev, const OrchView *v, int x, int y, int w, int h, int tr, int mode)
{
    const OrchTrack *t;
    char b[8];
    int yb;
    int show_ins;
    orch_check(dev != 0, __LINE__);
    orch_check(v != 0 && tr >= 0, __LINE__);
    if (!dev || !v || tr < 0 || tr >= (int)v->track_count) return;
    t = &v->trk[tr];
    show_ins = mode != ORCH_LAY_MIN && h >= 180;
    orch_fill(dev, x, y, w - 2, h, tr == (int)v->selected ? ORCH_COL_ROW_A : ORCH_COL_PANEL_BG);
    if (t->accent) orch_fill(dev, x, y, w - 2, 3, (COLOR)t->accent);
    orch_text(dev, x + 4, y + 6, t->name, ORCH_COL_WHITE);
    yb = y + 22;
    if (show_ins) {
        orch_btn(dev, x + 4, yb, w - 10, 20, ins_name((int)t->ins0), 0);
        orch_btn(dev, x + 4, yb + 22, w - 10, 20, ins_name((int)t->ins1), 0);
        yb += 46;
    }
    (void)orch_fmt_u(b, 8, t->pan);
    orch_btn(dev, x + 4, yb, w - 10, 20, b, 0);
    yb += 22;
    orch_btn(dev, x + 4, yb, (w - 12) / 2, 20, "M", t->muted);
    orch_btn(dev, x + 6 + (w - 12) / 2, yb, (w - 12) / 2, 20, "S", t->solo);
    yb += 22;
    if (show_ins) {
        (void)orch_fmt_u(b, 8, t->send);
        orch_btn(dev, x + 4, yb, w - 10, 20, b, 0);
        yb += 22;
    }
    if (h - (yb - y) > 16) paint_fader(dev, x + 8, yb, w - 18, h - (yb - y) - 6, (int)t->volume);
}

static void paint_aux(GDEV *dev, const OrchView *v, int x, int y, int w, int h, int mode)
{
    char b[8];
    orch_check(dev != 0, __LINE__);
    orch_check(v != 0, __LINE__);
    if (!dev || !v) return;
    orch_fill(dev, x, y, w - 2, h, ORCH_COL_PANEL_BG);
    orch_text(dev, x + 4, y + 6, "Aux", ORCH_COL_CYAN);
    if (mode == ORCH_LAY_MIN) {
        paint_fader(dev, x + 8, y + 28, w - 18, h - 36, (int)v->wet);
        return;
    }
    (void)orch_fmt_u(b, 8, v->room);
    orch_text(dev, x + 4, y + 28, b, ORCH_COL_DIM);
    (void)orch_fmt_u(b, 8, v->wet);
    orch_text(dev, x + 4, y + 44, b, ORCH_COL_SILVER);
    paint_fader(dev, x + 8, y + 64, w - 18, h - 72, (int)v->wet);
}

static void paint_master(GDEV *dev, const OrchView *v, int x, int y, int w, int h)
{
    char b[8];
    orch_check(dev != 0, __LINE__);
    orch_check(v != 0, __LINE__);
    if (!dev || !v) return;
    orch_fill(dev, x, y, w - 2, h, ORCH_COL_PANEL_BG);
    orch_text(dev, x + 4, y + 6, "Master", ORCH_COL_AMBER);
    orch_btn(dev, x + 4, y + 24, w - 10, 20,
             v->master_lim == ORCH_INS_LIMITER ? "Lim" : "Byp",
             v->master_lim == ORCH_INS_LIMITER);
    (void)orch_fmt_u(b, 8, v->master_vol);
    orch_text(dev, x + 4, y + 48, b, ORCH_COL_WHITE);
    paint_fader(dev, x + 8, y + 64, w - 18, h - 72, (int)v->master_vol);
}

static int thumb_h(const OrchView *v, int gw, int n, int *tx, int *tw)
{
    int tracks, max_bank, th;
    orch_check(v != 0, __LINE__);
    orch_check(tx != 0 && tw != 0, __LINE__);
    if (!v || !tx || !tw || gw < 16) return 0;
    tracks = (int)v->track_count;
    if (tracks < n) tracks = n;
    if (tracks < 1) tracks = 1;
    th = (gw * n) / tracks;
    if (th < 12) th = 12;
    if (th > gw) th = gw;
    *tw = th;
    *tx = 22;
    max_bank = tracks - n;
    if (max_bank <= 0) return 0;
    *tx = 22 + ((gw - th) * s_bank) / max_bank;
    if (*tx < 22) *tx = 22;
    if (*tx > 22 + gw - th) *tx = 22 + gw - th;
    return 1;
}

static void paint_hsb(GDEV *dev, const OrchView *v, int h, int gw, int n)
{
    int tx = 0, tw = 0;
    orch_check(dev != 0, __LINE__);
    orch_check(v != 0, __LINE__);
    if (!dev || !v || gw < 16) return;
    if ((int)v->track_count <= n) return;
    orch_fill(dev, 22, h - SB, gw, SB, ORCH_COL_INSET_BG);
    (void)thumb_h(v, gw, n, &tx, &tw);
    if (tw >= 8) orch_btn(dev, tx, h - SB + 1, tw, SB - 2, "", 0);
}

static void hsb_click(const OrchView *v, int x, int gw, int n)
{
    int tx = 0, tw = 0, live;
    int tracks, max_bank;
    orch_check(v != 0, __LINE__);
    orch_check(n > 0, __LINE__);
    if (!v) return;
    tracks = (int)v->track_count;
    if (tracks < n) tracks = n;
    max_bank = tracks - n;
    live = thumb_h(v, gw, n, &tx, &tw);
    if (!live) return;
    if (x >= tx && x < tx + tw) {
        s_drag = DRAG_H;
        s_anchor = x;
        s_anchor_bank = s_bank;
        return;
    }
    if (x < tx) {
        s_bank -= n;
        if (s_bank < 0) s_bank = 0;
    } else {
        s_bank += n;
        if (s_bank > max_bank) s_bank = max_bank;
    }
}

static void mix_drag(const OrchView *v, int x, int gw, int n)
{
    int tx = 0, tw = 0, tracks, max_bank, travel, b;
    orch_check(v != 0, __LINE__);
    orch_check(s_drag == DRAG_H, __LINE__);
    if (!v || gw < 16) return;
    tracks = (int)v->track_count;
    if (tracks < n) tracks = n;
    max_bank = tracks - n;
    if (max_bank <= 0) return;
    (void)thumb_h(v, gw, n, &tx, &tw);
    travel = gw - tw;
    if (travel < 1) travel = 1;
    b = s_anchor_bank + ((x - s_anchor) * max_bank) / travel;
    if (b < 0) b = 0;
    if (b > max_bank) b = max_bank;
    s_bank = b;
}

static void mix_paint(WND *wnd, GDEV *dev)
{
    OrchView *v = gst_view();
    int w, h, mode, sw, n, i, x, gw, th;
    orch_check(dev != 0, __LINE__);
    orch_check(v != 0, __LINE__);
    if (!dev || !v) return;
    w = (int)dev->width;
    h = (int)dev->height;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    mode = orch_lay_mode(w, h);
    orch_fill(dev, 0, 0, w, h, ORCH_COL_CANVAS);
    n = col_n(w, mode, &sw);
    gw = n * sw;
    th = ((int)v->track_count > n) ? h - 8 - SB : h - 8;
    int sel = (int)v->selected;
    if (sel >= 0 && sel < (int)v->track_count && n > 0) {
        if (sel < s_bank) s_bank = sel;
        else if (sel >= s_bank + n) s_bank = sel - n + 1;
    }
    if (s_bank < 0) s_bank = 0;
    if (s_bank > 63) s_bank = 63;
    orch_btn(dev, 2, 4, 18, th, "<", 0);
    x = 22;
    for (i = 0; i < n && i < 12; i++) {
        int tr = s_bank + i;
        if (tr < (int)v->track_count) paint_track(dev, v, x, 4, sw, th, tr, mode);
        x += sw;
    }
    if (x + 136 < w) {
        paint_aux(dev, v, w - 136, 4, 64, h - 8, mode);
        paint_master(dev, v, w - 70, 4, 68, h - 8);
    } else if (w > 80) {
        paint_master(dev, v, w - 70, 4, 68, h - 8);
    }
    paint_hsb(dev, v, h, gw, n);
    (void)wnd;
}

static int fader_dir(int y, int fy, int fh)
{
    orch_check(fh > -10 && fh < 8000, __LINE__);
    orch_check(y > -4000, __LINE__);
    if (fh < 4) return 1;
    return (y < fy + fh / 2) ? 1 : -1;
}

static void track_click(const OrchView *v, int tr, int x, int y, int rx, int h, int mode)
{
    int yb;
    int show_ins;
    int dir;
    orch_check(v != 0, __LINE__);
    orch_check(tr >= 0 && tr < 64, __LINE__);
    if (!v || tr >= (int)v->track_count) return;
    orch_desk_sel(tr);
    show_ins = mode != ORCH_LAY_MIN && h >= 180;
    if (y < 26) return;
    yb = 26;
    if (show_ins) {
        if (y < yb + 22) { (void)orch_ins_cycle(0); return; }
        if (y < yb + 44) { (void)orch_ins_cycle(1); return; }
        yb += 46;
    }
    dir = (x < rx + 20) ? -1 : 1;
    if (y < yb + 22) {
        nudge_u8(ORCH_F_PAN, (int)v->trk[tr].pan, dir, 4, 0, 127);
        return;
    }
    yb += 22;
    if (y < yb + 22) {
        if (x < rx + 24) (void)orch_track_set_u8(ORCH_F_MUTE, v->trk[tr].muted ? 0 : 1);
        else (void)orch_track_set_u8(ORCH_F_SOLO, v->trk[tr].solo ? 0 : 1);
        return;
    }
    yb += 22;
    if (show_ins && y < yb + 22) {
        nudge_u8(ORCH_F_SEND, (int)v->trk[tr].send, dir, 4, 0, 127);
        return;
    }
    if (show_ins) yb += 22;
    dir = fader_dir(y, yb, h - yb - 6);
    nudge_u8(ORCH_F_VOL, (int)v->trk[tr].volume, dir, 4, 0, 127);
}

static void master_click(const OrchView *v, int y, int h)
{
    int vol;
    int dir;
    orch_check(v != 0, __LINE__);
    orch_check(h > 0, __LINE__);
    if (y >= 24 && y < 46) {
        (void)orch_master_cycle();
        return;
    }
    dir = fader_dir(y, 64, h - 72);
    vol = (int)v->master_vol + dir * 4;
    if (vol < 0) vol = 0;
    if (vol > 127) vol = 127;
    (void)orch_master_vol(vol);
}

static void mix_click(int x, int y, int w, int h)
{
    OrchView *v = gst_view();
    int mode, sw, n, i, cx, gw;
    orch_check(v != 0, __LINE__);
    orch_check(w > 0 && h > 0, __LINE__);
    if (!v) return;
    mode = orch_lay_mode(w, h);
    n = col_n(w, mode, &sw);
    gw = n * sw;
    if ((int)v->track_count > n && y >= h - SB && x >= 22 && x < 22 + gw) {
        hsb_click(v, x, gw, n);
        return;
    }
    if (x < 22) {
        if (y < h / 2) {
            if (s_bank > 0) s_bank--;
        } else if (s_bank < 63) s_bank++;
        return;
    }
    cx = 22;
    for (i = 0; i < n && i < 12; i++) {
        if (x >= cx && x < cx + sw) {
            track_click(v, s_bank + i, x, y, cx, h, mode);
            return;
        }
        cx += sw;
    }
    if (x >= w - 136 && x < w - 70) orch_cmd_aux_show();
    else if (x >= w - 70) master_click(v, y, h);
}

static void mix_event(WND *wnd, const EVT *evt)
{
    int x, y, w, h, mode, sw, n, gw;
    OrchView *v;
    orch_check(wnd != 0, __LINE__);
    orch_check(evt != 0, __LINE__);
    if (!wnd || !evt) return;
    v = gst_view();
    if (!v) return;
    orch_client(wnd, evt, &x, &y, &w, &h);
    mode = orch_lay_mode(w, h);
    n = col_n(w, mode, &sw);
    gw = n * sw;
    if (evt->type == EV_BUT_UP) {
        s_drag = DRAG_OFF;
        (void)inval_wnd(wnd);
        return;
    }
    if (evt->type == EV_MOUSE_MOVE) {
        if (s_drag == DRAG_OFF) return;
        mix_drag(v, x, gw, n);
        (void)inval_wnd(wnd);
        return;
    }
    if (evt->type != EV_BUT_DOWN) return;
    s_drag = DRAG_OFF;
    mix_click(x, y, w, h);
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

void orch_mix_inval(void)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (s_wnd) (void)inval_wnd(s_wnd);
}

WND *orch_open_mix(void)
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
    w = opn_wnd("Mix", 48, 64, 780, 460, attr);
    if (!w) return 0;
    w->paint = mix_paint;
    w->event_handler = mix_event;
    w->destroy = on_destroy;
    w->menu_open = orch_menu_no;
    s_wnd = w;
    return w;
}
