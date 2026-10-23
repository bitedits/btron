#include "orchestra_desk.h"
#include <apps/orch_chk.h>

static WND *s_wnd;
static int s_slot;

static const char *alg(int id)
{
    static const char *N[9] = {
        "Bypass", "EQ", "Saturator", "Delay", "Reverb",
        "Chorus", "Gate", "Tremolo", "Limiter"
    };
    orch_check(id > -2, __LINE__);
    orch_check(id < 16, __LINE__);
    if (id < 0 || id > 8) return N[0];
    return N[id];
}

static const char *pname(int id, int p)
{
    static const char *P[9][4] = {
        { "", "", "", "" },
        { "mode", "Hz", "Q", "dB" },
        { "k", "", "", "" },
        { "16ths", "fb", "wet", "" },
        { "send", "room", "damp", "wet" },
        { "ms", "depth", "Hz", "mix" },
        { "thr", "tau", "hold", "" },
        { "depth", "Hz", "", "" },
        { "", "", "", "" }
    };
    orch_check(p >= 0 && p < 4, __LINE__);
    orch_check(id < 16, __LINE__);
    if (id < 0 || id > 8 || p < 0 || p > 3) return "";
    return P[id][p];
}

static int step_of(int id, int p)
{
    orch_check(id >= 0 && id <= 8, __LINE__);
    orch_check(p >= 0 && p < 4, __LINE__);
    if (id == ORCH_INS_EQ && p == 1) return 100;
    if (id == ORCH_INS_EQ && p == 2) return 10;
    if (id == ORCH_INS_GATE && p == 1) return 10;
    return 1;
}

static void fmt_param(char *b, int cap, int id, int p, int v)
{
    int scale = 0;
    int n;
    int w;
    int f;
    orch_check(b != 0, __LINE__);
    orch_check(cap > 8 && cap <= 32, __LINE__);
    if (id == ORCH_INS_EQ && p == 2) scale = 100;
    if (id == ORCH_INS_CHORUS && p == 2) scale = 10;
    if (id == ORCH_INS_TREMOLO && p == 1) scale = 10;
    if (id == ORCH_INS_GATE && p == 0) scale = 100;
    if (id == ORCH_INS_EQ && p == 0) {
        static const char *M[4] = { "LP", "HP", "BP", "Peak" };
        int m = v;
        if (m < 0) m = 0;
        if (m > 3) m = 3;
        orch_copy(b, cap, M[m]);
        return;
    }
    if (scale == 0) {
        (void)orch_fmt_i(b, cap, v);
        return;
    }
    if (v < 0) v = 0;
    w = v / scale;
    f = v % scale;
    n = orch_fmt_u(b, cap, (unsigned)w);
    if (n < 0 || n + 4 >= cap) return;
    b[n++] = '.';
    if (scale == 100) {
        b[n++] = (char)('0' + f / 10);
        b[n++] = (char)('0' + (f % 10));
    } else {
        b[n++] = (char)('0' + f);
    }
    b[n] = 0;
}

static int16_t slot_val(const OrchTrack *t, int slot, int p)
{
    orch_check(t != 0, __LINE__);
    orch_check(p >= 0 && p < 4, __LINE__);
    if (!t || p < 0 || p > 3) return 0;
    return (slot == 0) ? t->p0[p] : t->p1[p];
}

static void paint_row(GDEV *dev, int x, int y, int w, const char *name, const char *val, int hide_name)
{
    orch_check(dev != 0, __LINE__);
    orch_check(w > 0 && w < 8000, __LINE__);
    orch_fill(dev, x, y, w, 28, ORCH_COL_ROW_B);
    if (!hide_name && name && name[0]) orch_text(dev, x + 6, y + 6, name, ORCH_COL_DIM);
    if (val) orch_text(dev, x + (hide_name ? 6 : 72), y + 6, val, ORCH_COL_WHITE);
}

static void insert_paint(WND *wnd, GDEV *dev)
{
    OrchView *v = gst_view();
    const OrchTrack *t;
    int w, h, mode, id, p, tr;
    char b[16];
    orch_check(dev != 0, __LINE__);
    orch_check(v != 0, __LINE__);
    if (!dev || !v) return;
    w = (int)dev->width;
    h = (int)dev->height;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    mode = orch_lay_mode(w, h);
    orch_fill(dev, 0, 0, w, h, ORCH_COL_CANVAS);
    tr = (int)v->selected;
    if (tr < 0 || tr >= (int)v->track_count) {
        orch_text(dev, 12, 16, "no track", ORCH_COL_DIM);
        (void)wnd;
        return;
    }
    t = &v->trk[tr];
    orch_text(dev, 12, 8, t->name, ORCH_COL_WHITE);
    orch_btn(dev, 12, 28, 64, 24, "Slot 1", s_slot == 0);
    orch_btn(dev, 80, 28, 64, 24, "Slot 2", s_slot == 1);
    id = (s_slot == 0) ? (int)t->ins0 : (int)t->ins1;
    orch_btn(dev, 12, 58, w > 40 ? w - 24 : 40, 28, alg(id), id != 0);
    if (id == ORCH_INS_BYPASS || id == ORCH_INS_LIMITER) {
        (void)wnd;
        return;
    }
    for (p = 0; p < 4; p++) {
        const char *nm = pname(id, p);
        if (!nm[0]) continue;
        if (mode == ORCH_LAY_MIN && p > 1) continue;
        fmt_param(b, 16, id, p, (int)slot_val(t, s_slot, p));
        paint_row(dev, 12, 96 + p * 32, w - 24, nm, b, mode == ORCH_LAY_MIN);
    }
    (void)h;
    (void)wnd;
}

static void row_delta(int id, int p, int x, int rx, int rw)
{
    int dir;
    int step;
    orch_check(p >= 0 && p < 4, __LINE__);
    orch_check(rw > 0, __LINE__);
    dir = (x < rx + rw / 2) ? -1 : 1;
    step = step_of(id, p);
    (void)orch_ins_param(s_slot, p, dir * step);
}

static void insert_click(int x, int y, int w, int h)
{
    OrchView *v = gst_view();
    const OrchTrack *t;
    int tr;
    int id;
    int p;
    int mode;
    orch_check(v != 0, __LINE__);
    orch_check(w > 0 && h > 0, __LINE__);
    if (!v) return;
    tr = (int)v->selected;
    if (tr < 0 || tr >= (int)v->track_count) return;
    t = &v->trk[tr];
    if (orch_inside(x, y, 12, 28, 64, 24)) { s_slot = 0; return; }
    if (orch_inside(x, y, 80, 28, 64, 24)) { s_slot = 1; return; }
    if (orch_inside(x, y, 12, 58, w - 24, 28)) {
        (void)orch_ins_cycle(s_slot);
        return;
    }
    id = (s_slot == 0) ? (int)t->ins0 : (int)t->ins1;
    if (id == ORCH_INS_BYPASS || id == ORCH_INS_LIMITER) return;
    mode = orch_lay_mode(w, h);
    for (p = 0; p < 4; p++) {
        if (!pname(id, p)[0]) continue;
        if (mode == ORCH_LAY_MIN && p > 1) continue;
        if (!orch_inside(x, y, 12, 96 + p * 32, w - 24, 28)) continue;
        row_delta(id, p, x, 12, w - 24);
        return;
    }
}

static void insert_event(WND *wnd, const EVT *evt)
{
    int x, y, w, h;
    orch_check(wnd != 0, __LINE__);
    orch_check(evt != 0, __LINE__);
    if (!wnd || !evt || evt->type != EV_BUT_DOWN) return;
    orch_client(wnd, evt, &x, &y, &w, &h);
    insert_click(x, y, w, h);
    (void)inval_wnd(wnd);
}

static void on_destroy(WND *wnd)
{
    orch_check(wnd != 0, __LINE__);
    orch_check(s_wnd == 0 || s_wnd == wnd, __LINE__);
    (void)wnd;
    s_wnd = 0;
}

void orch_insert_inval(void)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (s_wnd) (void)inval_wnd(s_wnd);
}

WND *orch_open_insert(void)
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
    w = opn_wnd("Insert", 168, 96, 420, 460, attr);
    if (!w) return 0;
    w->paint = insert_paint;
    w->event_handler = insert_event;
    w->destroy = on_destroy;
    w->menu_open = orch_menu_no;
    s_wnd = w;
    return w;
}
