#include "orchestra_desk.h"
#include <apps/orch_chk.h>

enum {
    B_PLAY, B_STOP, B_REC, B_PANIC, B_TEMPO, B_NUM, B_DEN, B_CLICK, B_FAM,
    B_SC0, B_STAT = B_SC0 + 16, B_ML, B_MR, B_N
};

static WND *s_wnd;

static void put(int *g, int i, int x, int y, int w, int h, int on)
{
    int o;
    orch_check(g != 0, __LINE__);
    orch_check(i >= 0 && i < B_N, __LINE__);
    if (!g || i < 0 || i >= B_N) return;
    o = i * 5;
    g[o] = x;
    g[o + 1] = y;
    g[o + 2] = w;
    g[o + 3] = h;
    g[o + 4] = on;
}

static void clear_g(int *g, int n)
{
    int i;
    orch_check(g != 0, __LINE__);
    orch_check(n > 0 && n <= B_N, __LINE__);
    if (!g) return;
    for (i = 0; i < n && i < B_N; i++) put(g, i, 0, 0, 0, 0, 0);
}

static void place_scenes(int *g, int x, int y, int aw, int on)
{
    int i;
    int bw;
    orch_check(g != 0, __LINE__);
    orch_check(aw > 0 && aw < 8000, __LINE__);
    bw = aw / 8;
    if (bw < 24) bw = 24;
    if (bw > 120) bw = 120;
    for (i = 0; i < 16; i++) {
        int c = i % 8;
        int r = i / 8;
        put(g, B_SC0 + i, x + c * bw, y + r * 28, bw - 4, 24, on);
    }
}

static void place_min(int *g, int w, int h)
{
    int bw;
    orch_check(g != 0, __LINE__);
    orch_check(w > 0 && h > 0, __LINE__);
    bw = (w - 20) / 4;
    if (bw < 24) bw = 24;
    if (bw > 88) bw = 88;
    put(g, B_PLAY, 8, 26, bw, 24, 1);
    put(g, B_STOP, 12 + bw, 26, bw, 24, 1);
    put(g, B_REC, 16 + 2 * bw, 26, bw, 24, 1);
    put(g, B_PANIC, 20 + 3 * bw, 26, bw, 24, 1);
    put(g, B_STAT, 8, h - 28, w - 16, 22, 1);
}

static void place_stack(int *g, int w, int h)
{
    int y = 26;
    int bw;
    orch_check(g != 0, __LINE__);
    orch_check(h > 0 && w > 0, __LINE__);
    bw = (w - 16) / 3;
    if (bw < 24) bw = 24;
    if (bw > 120) bw = 120;
    put(g, B_PLAY, 8, y, bw - 4, 24, 1);
    put(g, B_STOP, 8 + bw, y, bw - 4, 24, 1);
    put(g, B_REC, 8 + 2 * bw, y, bw - 4, 24, 1);
    y += 28;
    put(g, B_PANIC, 8, y, 96, 24, 1);
    y += 28;
    put(g, B_TEMPO, 8, y, 72, 24, 1);
    put(g, B_NUM, 84, y, 40, 24, 1);
    put(g, B_DEN, 128, y, 40, 24, 1);
    y += 28;
    put(g, B_CLICK, 8, y, 72, 24, 1);
    put(g, B_FAM, 84, y, 120, 24, 1);
    y += 28;
    if (y + 56 < h - 30) {
        place_scenes(g, 8, y, w - 16, 1);
        y += 56;
    }
    put(g, B_STAT, 8, h - 28, w - 48, 22, 1);
    if (h >= 220) {
        put(g, B_ML, w - 36, 26, 12, h - 60, 1);
        put(g, B_MR, w - 20, 26, 12, h - 60, 1);
    }
}

static void place_wide(int *g, int w, int h)
{
    int mx;
    orch_check(g != 0, __LINE__);
    orch_check(w >= 240 && h > 0, __LINE__);
    mx = w - 44;
    put(g, B_PLAY, 8, 26, 64, 28, 1);
    put(g, B_STOP, 76, 26, 64, 28, 1);
    put(g, B_REC, 144, 26, 56, 28, 1);
    put(g, B_PANIC, 208, 26, 72, 28, 1);
    put(g, B_TEMPO, 288, 26, 72, 28, 1);
    put(g, B_NUM, 364, 26, 40, 28, 1);
    put(g, B_DEN, 408, 26, 40, 28, 1);
    put(g, B_CLICK, 456, 26, 64, 28, 1);
    put(g, B_FAM, 524, 26, 112, 28, 1);
    if (mx > 540) place_scenes(g, 8, 60, mx - 8, 1);
    put(g, B_STAT, 8, h - 30, mx - 12, 22, 1);
    put(g, B_ML, w - 36, 60, 14, h - 96, 1);
    put(g, B_MR, w - 18, 60, 14, h - 96, 1);
}

static void stage_geom(int w, int h, int *g)
{
    int mode;
    orch_check(g != 0, __LINE__);
    orch_check(w > 0 && h > 0, __LINE__);
    clear_g(g, B_N);
    mode = orch_lay_mode(w, h);
    if (mode == ORCH_LAY_MIN) place_min(g, w, h);
    else if (mode == ORCH_LAY_STACK) place_stack(g, w, h);
    else place_wide(g, w, h);
}

static int g_on(const int *g, int i)
{
    orch_check(g != 0, __LINE__);
    orch_check(i >= 0 && i < B_N, __LINE__);
    if (!g || i < 0 || i >= B_N) return 0;
    return g[i * 5 + 4];
}

static void g_box(const int *g, int i, int *x, int *y, int *w, int *h)
{
    int o;
    orch_check(g != 0, __LINE__);
    orch_check(i >= 0 && i < B_N, __LINE__);
    if (!g || !x || !y || !w || !h || i < 0 || i >= B_N) return;
    o = i * 5;
    *x = g[o];
    *y = g[o + 1];
    *w = g[o + 2];
    *h = g[o + 3];
}

static const char *word(const OrchView *v, int id, char *b)
{
    static const char *FAM[3] = { "Rock", "Classic", "Orchestra" };
    int show;
    int n;
    orch_check(v != 0, __LINE__);
    orch_check(b != 0, __LINE__);
    if (!v || !b) return "";
    if (id == B_PLAY) return "PLAY";
    if (id == B_STOP) return "STOP";
    if (id == B_REC) return "REC";
    if (id == B_PANIC) return "PANIC";
    if (id == B_CLICK) return v->click ? "CLICK" : "click";
    if (id == B_TEMPO) {
        (void)orch_fmt_u(b, 8, v->tempo);
        return b;
    }
    if (id == B_NUM) {
        (void)orch_fmt_u(b, 8, v->num);
        return b;
    }
    if (id == B_DEN) {
        (void)orch_fmt_u(b, 8, v->den);
        return b;
    }
    if (id == B_FAM) {
        show = (int)v->family;
        if (show < 0 || show > 2) show = 0;
        if (v->confirm_family >= 0 && v->confirm_family <= 2) show = (int)v->confirm_family;
        orch_copy(b, 16, FAM[show]);
        n = 0;
        while (b[n] && n < 12) n++;
        if (v->confirm_family >= 0 && n < 14) {
            b[n] = '?';
            b[n + 1] = 0;
        }
        return b;
    }
    if (id >= B_SC0 && id < B_SC0 + 16) {
        int s = id - B_SC0;
        if (v->scn[s].name[0]) {
            orch_copy(b, 16, v->scn[s].name);
            return b;
        }
        (void)orch_fmt_u(b, 8, (unsigned)(s + 1));
        return b;
    }
    return "";
}

static int is_hot(const OrchView *v, int id)
{
    orch_check(v != 0, __LINE__);
    orch_check(id >= 0 && id < B_N, __LINE__);
    if (!v) return 0;
    if (id == B_PLAY) return v->state == ORCH_GST_PLAYING;
    if (id == B_STOP) return v->state == ORCH_GST_PAUSED || v->state == ORCH_GST_READY;
    if (id == B_REC) return v->rec != 0;
    if (id == B_CLICK) return v->click != 0;
    if (id >= B_SC0 && id < B_SC0 + 16) return (int)v->active_scene == id - B_SC0;
    return 0;
}

static void paint_one(GDEV *dev, const OrchView *v, const int *g, int id)
{
    int x, y, w, h;
    char b[16];
    orch_check(dev != 0, __LINE__);
    orch_check(v != 0, __LINE__);
    if (!g_on(g, id)) return;
    g_box(g, id, &x, &y, &w, &h);
    if (id == B_STAT) {
        orch_fill(dev, x, y, w, h, ORCH_COL_INSET_BG);
        orch_text(dev, x + 4, y + 4, v->status, ORCH_COL_SILVER);
        return;
    }
    if (id == B_ML) {
        orch_meter(dev, x, y, w, h, (int)v->meter_l);
        return;
    }
    if (id == B_MR) {
        orch_meter(dev, x, y, w, h, (int)v->meter_r);
        return;
    }
    orch_btn(dev, x, y, w, h, word(v, id, b), is_hot(v, id));
}

static void stage_paint(WND *wnd, GDEV *dev)
{
    OrchView *v = gst_view();
    int g[B_N * 5];
    int i;
    int w;
    int h;
    orch_check(dev != 0, __LINE__);
    orch_check(v != 0, __LINE__);
    if (!dev || !v) return;
    w = (int)dev->width;
    h = (int)dev->height;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    orch_fill(dev, 0, 0, w, h, ORCH_COL_CANVAS);
    stage_geom(w, h, g);
    for (i = 0; i < B_N; i++) paint_one(dev, v, g, i);
    orch_menu_paint(dev);
    (void)wnd;
}

static int nudge(int x, int bx, int bw)
{
    orch_check(bw < 8000, __LINE__);
    orch_check(x > -4000, __LINE__);
    if (bw < 2) return 1;
    return (x < bx + bw / 2) ? -1 : 1;
}

static int next_den(int d, int dir)
{
    static const int t[4] = { 2, 4, 8, 16 };
    int i;
    int k = 0;
    orch_check(dir == -1 || dir == 1, __LINE__);
    orch_check(d < 64, __LINE__);
    for (i = 0; i < 4; i++) if (t[i] == d) k = i;
    k += dir;
    if (k < 0) k = 3;
    if (k > 3) k = 0;
    return t[k];
}

static void hit_transport(const OrchView *v, int id, int x, int bx, int bw)
{
    int dir;
    int t;
    int n;
    orch_check(v != 0, __LINE__);
    orch_check(id >= 0 && id < B_N, __LINE__);
    dir = nudge(x, bx, bw);
    if (id == B_PLAY) (void)orch_cmd_play();
    else if (id == B_STOP) (void)orch_cmd_stop();
    else if (id == B_REC) orch_desk_rec_flip();
    else if (id == B_PANIC) (void)orch_cmd_panic();
    else if (id == B_CLICK) (void)orch_cmd_click();
    else if (id == B_TEMPO) {
        t = (int)v->tempo + dir;
        if (t < 40) t = 40;
        if (t > 240) t = 240;
        (void)orch_cmd_tempo(t);
    } else if (id == B_NUM) {
        n = (int)v->num + dir;
        if (n < 1) n = 1;
        if (n > 12) n = 12;
        (void)orch_cmd_num(n);
    } else if (id == B_DEN) {
        (void)orch_cmd_den(next_den((int)v->den, dir));
    } else if (id == B_FAM) {
        if (v->confirm_family >= 0 && v->confirm_family <= 2)
            (void)orch_cmd_family((int)v->confirm_family);
        else {
            n = (int)v->family + 1;
            if (n > 2) n = 0;
            (void)orch_cmd_family(n);
        }
    }
}

static void stage_click(int x, int y, int w, int h)
{
    OrchView *v = gst_view();
    int g[B_N * 5];
    int i;
    orch_check(v != 0, __LINE__);
    orch_check(w > 0 && h > 0, __LINE__);
    if (!v) return;
    stage_geom(w, h, g);
    for (i = B_N - 1; i >= 0; i--) {
        int bx, by, bw, bh;
        if (!g_on(g, i) || i == B_STAT || i == B_ML || i == B_MR) continue;
        g_box(g, i, &bx, &by, &bw, &bh);
        if (!orch_inside(x, y, bx, by, bw, bh)) continue;
        if (i >= B_SC0 && i < B_SC0 + 16) (void)orch_cmd_scene(i - B_SC0);
        else hit_transport(v, i, x, bx, bw);
        return;
    }
}

static void stage_event(WND *wnd, const EVT *evt)
{
    int x, y, w, h;
    orch_check(wnd != 0, __LINE__);
    orch_check(evt != 0, __LINE__);
    if (!wnd || !evt) return;
    orch_client(wnd, evt, &x, &y, &w, &h);
    if (evt->type == EV_MOUSE_MOVE) {
        if (orch_menu_move(x, y)) (void)inval_wnd(wnd);
        return;
    }
    if (evt->type == EV_KEY_DOWN) {
        if (orch_menu_key(evt->key, evt->data)) (void)inval_wnd(wnd);
        return;
    }
    if (evt->type != EV_BUT_DOWN) return;
    if (y < 21 || orch_menu_is_open()) {
        if (orch_menu_down(x, y)) (void)inval_wnd(wnd);
        if (y < 21) return;
    }
    stage_click(x, y, w, h);
    (void)inval_wnd(wnd);
}

static void on_destroy(WND *wnd)
{
    orch_check(wnd != 0, __LINE__);
    orch_check(s_wnd == 0 || s_wnd == wnd, __LINE__);
    (void)wnd;
    s_wnd = 0;
}

static BOOL stage_menu(WND *wnd)
{
    orch_check(wnd != 0, __LINE__);
    orch_check(s_wnd == 0 || s_wnd == wnd, __LINE__);
    (void)wnd;
    return orch_menu_is_open() ? TRUE : FALSE;
}

void orch_stage_inval(void)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (s_wnd) (void)inval_wnd(s_wnd);
}

WND *orch_open_stage(void)
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
    w = opn_wnd("Orchestra", 72, 40, 780, 520, attr);
    if (!w) return 0;
    w->paint = stage_paint;
    w->event_handler = stage_event;
    w->destroy = on_destroy;
    w->menu_open = stage_menu;
    s_wnd = w;
    return w;
}
