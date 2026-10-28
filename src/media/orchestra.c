#include "orchestra_desk.h"
#include <apps/orch_chk.h>
#include <btron/app_menu.h>
#include <btron/event.h>
#include <btron/troncode.h>
#include <stdint.h>

static APP_MENU_BAR s_menu;
static int s_menu_ok;

enum { OPEN_N = 6 };

static const char k_open_lbl[OPEN_N][64] = {
    "セッション (Session)",
    "鐘のキャロル (Bells)",
    "今田勝 (Caterpillar)",
    "朝日のあたる家 (Rising)",
    "TRON Init",
    "ルクハッシュ (LukHash)"
};

static void orch_menu_scenes(void);

int orch_lay_mode(int w, int h)
{
    orch_check(w > -4000 && w < 20000, __LINE__);
    orch_check(h > -4000 && h < 20000, __LINE__);
    if (h < 200 || w < 240) return ORCH_LAY_MIN;
    if (w < 700) return ORCH_LAY_STACK;
    return ORCH_LAY_WIDE;
}

void orch_fill(GDEV *dev, int x, int y, int w, int h, COLOR col)
{
    RECT r;
    orch_check(dev != 0, __LINE__);
    orch_check(w < 8000 && h < 8000, __LINE__);
    if (!dev || w <= 0 || h <= 0 || x > 4000 || y > 4000) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0) return;
    if (x + w > 4096) w = 4096 - x;
    if (y + h > 4096) h = 4096 - y;
    r.left = (H)x;
    r.top = (H)y;
    r.right = (H)(x + w);
    r.bottom = (H)(y + h);
    (void)fill_rec(dev, &r, col);
}

void orch_text(GDEV *dev, int x, int y, const char *s, COLOR col)
{
    orch_check(dev != 0, __LINE__);
    orch_check(s != 0, __LINE__);
    if (!dev || !s || x < -200 || y < -200 || x > 4000 || y > 4000) return;
    (void)drw_tc_string(dev, (H)x, (H)y, s, col, 0);
}

void orch_btn(GDEV *dev, int x, int y, int w, int h, const char *label, int hot)
{
    COLOR face;
    COLOR ink;
    orch_check(dev != 0, __LINE__);
    orch_check(w < 8000 && h < 8000, __LINE__);
    if (!dev || w < 8 || h < 8) return;
    face = hot ? ORCH_COL_AMBER : ORCH_COL_BTN_FACE;
    ink = hot ? ORCH_COL_INSET_BG : ORCH_COL_WHITE;
    orch_fill(dev, x, y, w, h, ORCH_COL_BORDER_LO);
    orch_fill(dev, x, y, w, h > 1 ? h - 1 : h, face);
    if (label && label[0]) orch_text(dev, x + 4, y + 4, label, ink);
}

void orch_meter(GDEV *dev, int x, int y, int w, int h, int pct)
{
    int fill;
    COLOR col;
    orch_check(dev != 0, __LINE__);
    orch_check(w < 8000 && h < 8000, __LINE__);
    if (!dev || w < 2 || h < 2) return;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    orch_fill(dev, x, y, w, h, ORCH_COL_INSET_BG);
    fill = (h * pct) / 100;
    col = (pct > 90) ? ORCH_COL_RED : ORCH_COL_GREEN;
    if (fill > 0) orch_fill(dev, x + 1, y + h - fill, w - 2, fill, col);
}

int orch_fmt_u(char *dst, int cap, unsigned v)
{
    char tmp[10];
    int n = 0;
    int i;
    orch_check(dst != 0, __LINE__);
    orch_check(cap > 1 && cap <= 96, __LINE__);
    if (!dst || cap < 2) return 0;
    if (v == 0u) {
        dst[0] = '0';
        dst[1] = 0;
        return 1;
    }
    while (v > 0u && n < 10) {
        tmp[n] = (char)('0' + (int)(v % 10u));
        v /= 10u;
        n++;
    }
    if (n >= cap) n = cap - 1;
    for (i = 0; i < n; i++) dst[i] = tmp[n - 1 - i];
    dst[n] = 0;
    return n;
}

int orch_fmt_i(char *dst, int cap, int v)
{
    unsigned mag;
    int n;
    orch_check(dst != 0, __LINE__);
    orch_check(cap > 2 && cap <= 96, __LINE__);
    if (!dst || cap < 3) return 0;
    if (v >= 0) return orch_fmt_u(dst, cap, (unsigned)v);
    dst[0] = '-';
    mag = (v > -32768) ? (unsigned)(-v) : 32768u;
    n = orch_fmt_u(dst + 1, cap - 1, mag);
    return n + 1;
}

void orch_copy(char *dst, int cap, const char *src)
{
    int i;
    orch_check(dst != 0, __LINE__);
    orch_check(cap > 1 && cap <= 96, __LINE__);
    if (!dst || cap < 2) return;
    if (!src) src = "";
    for (i = 0; i < cap - 1 && i < 95; i++) {
        dst[i] = src[i];
        if (src[i] == 0) return;
    }
    dst[cap - 1] = 0;
}

int orch_inside(int x, int y, int bx, int by, int bw, int bh)
{
    orch_check(bw < 8000, __LINE__);
    orch_check(bh < 8000, __LINE__);
    if (bw <= 0 || bh <= 0) return 0;
    if (x < bx || y < by) return 0;
    if (x >= bx + bw || y >= by + bh) return 0;
    return 1;
}

void orch_client(const WND *wnd, const EVT *evt, int *x, int *y, int *w, int *h)
{
    int cw;
    int ch;
    orch_check(wnd != 0, __LINE__);
    orch_check(x != 0 && w != 0, __LINE__);
    if (!wnd || !x || !y || !w || !h) return;
    cw = (int)wnd->client.right - (int)wnd->client.left;
    ch = (int)wnd->client.bottom - (int)wnd->client.top;
    if (cw < 1) cw = 1;
    if (ch < 1) ch = 1;
    *w = cw;
    *h = ch;
    if (!evt) {
        *x = 0;
        *y = 0;
        return;
    }
    *x = (int)evt->pos.x - (int)wnd->client.left;
    *y = (int)evt->pos.y - (int)wnd->client.top;
}

uint32_t orch_q(const OrchView *v, int snap)
{
    uint8_t den;
    uint8_t num;
    orch_check(v != 0, __LINE__);
    orch_check(snap >= 0 && snap <= ORCH_SNAP_16TH, __LINE__);
    if (!v || snap < 0 || snap > ORCH_SNAP_16TH) return 960u;
    if (v->num > 12u) return 960u;
    num = v->num ? v->num : 4;
    den = v->den;
    if (den != 2u && den != 4u && den != 8u && den != 16u) den = 4u;
    return gst_snap_q((uint8_t)snap, num, den);
}

uint32_t orch_now_tick(const OrchView *v)
{
    uint32_t fs;
    uint8_t den;
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (!v || v->num > 12u) return 0;
    fs = v->sample_rate;
    if (fs != 44100u && fs != 48000u) fs = 48000u;
    den = v->den;
    if (den != 2u && den != 4u && den != 8u && den != 16u) den = 4u;
    return gst_tick_of_sample(v->npos, fs, v->tempo, den);
}

uint32_t orch_snap_tick(const OrchView *v, uint32_t tick)
{
    uint32_t q;
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (!v || v->snap > ORCH_SNAP_16TH) return tick;
    q = orch_q(v, (int)v->snap);
    if (q == 0u) return tick;
    return gst_snap_down(tick, q);
}

int orch_hit_event(int track, uint32_t tick, int key, int use_key)
{
    OrchView *v = gst_view();
    uint32_t n;
    uint32_t i;
    int found = -1;
    orch_check(v != 0, __LINE__);
    orch_check(track < 64, __LINE__);
    if (!v || track < 0 || track > 63) return -1;
    n = v->event_count;
    if (n > 4096u) n = 4096u;
    for (i = 0; i < n; i++) {
        const OrchEvent *e = &v->ev[i];
        uint32_t end;
        if ((int)e->track != track || e->vel == 0) continue;
        end = e->start_tick + e->dur_tick;
        if (tick < e->start_tick || tick >= end) continue;
        if (use_key && (int)e->key != key) continue;
        found = (int)i;
    }
    return found;
}

void orch_desk_sel(int idx)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(idx >= -1 && idx < 64, __LINE__);
    if (!v || idx < 0 || idx >= (int)v->track_count || idx > 63) return;
    gst_seq_begin();
    v->selected = (int8_t)idx;
    gst_seq_end();
}

void orch_desk_tool(int tool)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(tool >= 0 && tool <= ORCH_TOOL_ERASE, __LINE__);
    if (!v || tool < 0 || tool > ORCH_TOOL_ERASE) return;
    gst_seq_begin();
    v->tool = (uint8_t)tool;
    gst_seq_end();
}

void orch_desk_snap_cycle(void)
{
    OrchView *v = gst_view();
    int n;
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (!v) return;
    n = (int)v->snap + 1;
    if (n > ORCH_SNAP_16TH) n = ORCH_SNAP_BAR;
    gst_seq_begin();
    v->snap = (uint8_t)n;
    gst_seq_end();
}

void orch_desk_rec_flip(void)
{
    OrchView *v = gst_view();
    uint8_t n;
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (!v) return;
    n = v->rec ? 0 : 1;
    gst_seq_begin();
    v->rec = n;
    gst_seq_end();
}

static int add_s(char *d, int n, int cap, const char *s)
{
    int i = 0;
    orch_check(d != 0, __LINE__);
    orch_check(cap > 1 && cap <= 96, __LINE__);
    if (!d || !s || n < 0 || n >= cap) return n;
    while (s[i] && n < cap - 1 && i < 48) {
        d[n] = s[i];
        n++;
        i++;
    }
    d[n] = 0;
    return n;
}

static int add_u(char *d, int n, int cap, unsigned v)
{
    char tmp[12];
    orch_check(d != 0, __LINE__);
    orch_check(cap > 1, __LINE__);
    (void)orch_fmt_u(tmp, 12, v);
    return add_s(d, n, cap, tmp);
}

void orch_publish_idle(void)
{
    OrchView *v = gst_view();
    char b[96];
    int n = 0;
    static const char *ST[4] = { "NULL", "READY", "PAUSED", "PLAYING" };
    const char *st;
    orch_check(v != 0, __LINE__);
    orch_check(v->status_hold == 0 || v->status_hold == 1, __LINE__);
    if (!v || v->status_hold) return;
    st = (v->state <= ORCH_GST_PLAYING) ? ST[v->state] : ST[0];
    n = add_u(b, n, 96, v->sample_rate);
    n = add_s(b, n, 96, " Hz | q ");
    n = add_u(b, n, 96, v->queued_ms);
    n = add_s(b, n, 96, " ms | drop ");
    n = add_u(b, n, 96, v->drop_audio);
    n = add_s(b, n, 96, " | steal ");
    n = add_u(b, n, 96, v->steal_voice);
    n = add_s(b, n, 96, " | smf ");
    n = add_u(b, n, 96, v->refuse_smf);
    if (n > 0 && n < 70) {
        n = add_s(b, n, 96, " | ");
        n = add_s(b, n, 96, st);
    }
    if (n > 0 && n < 62 && v->active_scene < 16u) {
        if (v->scn[v->active_scene].name[0]) {
            n = add_s(b, n, 96, " | ");
            (void)add_s(b, n, 96, v->scn[v->active_scene].name);
        }
    }
    orch_status_set(b);
}

static void right_ms(unsigned ms)
{
    char b[16];
    int n;
    orch_check(s_menu_ok == 0 || s_menu_ok == 1, __LINE__);
    orch_check(s_menu.header_count <= APP_MENU_MAX_HEADERS, __LINE__);
    if (!s_menu_ok) return;
    if (ms > 999999u) ms = 999999u;
    n = orch_fmt_u(b, 12, ms);
    if (n < 0 || n > 10) return;
    b[n] = ' ';
    b[n + 1] = 'm';
    b[n + 2] = 's';
    b[n + 3] = 0;
    app_menu_set_right_text(&s_menu, b);
}

static uint32_t span_tick(const OrchView *v)
{
    uint32_t n;
    uint32_t i;
    uint32_t end;
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (!v) return 0;
    n = v->event_count;
    end = 0;
    if (n > ORCH_MAX_EVENTS) n = ORCH_MAX_EVENTS;
    for (i = 0; i < n; i++) {
        const OrchEvent *e = &v->ev[i];
        uint32_t a;
        uint32_t d;
        uint32_t b;
        if (e->vel == 0) continue;
        a = e->start_tick;
        d = e->dur_tick;
        b = (a > 0xffffffffu - d) ? 0xffffffffu : a + d;
        if (b > end) end = b;
    }
    return end;
}

void orchestra_ui_tick(void)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (!v) return;
    if (v->sample_rate != 44100u && v->sample_rate != 48000u) return;
    orch_build_curve();
    gst_seq_begin();
    v->queued_ms = gst_rt_queued_ms();
    v->loop_tick = span_tick(v);
    gst_seq_end();
    orch_publish_idle();
    right_ms(v->queued_ms);
    if (s_menu_ok) orch_menu_scenes();
    orch_inval_all();
}

void orch_boot_desk(void)
{
    OrchView *v;
    int st;
    v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (!v || !gst_rt_boot(48000u)) return;
    v = gst_view();
    if (!v) return;
    st = (int)v->state;
    if (st == ORCH_GST_READY) (void)gst_rt_set_state(ORCH_GST_PAUSED);
    if (v->track_count == 0 && v->event_count == 0 && v->state != ORCH_GST_PLAYING)
        (void)orch_cmd_family(ORCH_FAM_ROCK);
}

void orch_shutdown_desk(void)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (!v) return;
    (void)gst_rt_set_state(ORCH_GST_NULL);
}

static void tc_text(uint8_t c)
{
    char b[16];
    OrchView *v = gst_view();
    int st;
    orch_check(c < 64u, __LINE__);
    orch_check(v != 0, __LINE__);
    if (!v) return;
    st = (int)v->state;
    if (st > ORCH_GST_PLAYING) return;
    orch_copy(b, 16, "tc ");
    if (c == 29u) orch_copy(b, 16, "tc 29.97");
    else {
        int n = 3;
        n = orch_fmt_u(b + 3, 8, c);
        (void)n;
    }
    orch_status_set(b);
}

void orch_cmd_tc_cycle(void)
{
    OrchView *v = gst_view();
    uint8_t n;
    uint8_t c;
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (!v) return;
    c = v->tc_format;
    if (c == 24u) n = 25u;
    else if (c == 25u) n = 29u;
    else if (c == 29u) n = 30u;
    else n = 24u;
    gst_seq_begin();
    v->tc_format = n;
    gst_seq_end();
    tc_text(n);
}

void orch_cmd_master_clock(void)
{
    OrchView *v = gst_view();
    uint64_t ns;
    unsigned sec;
    unsigned ms;
    char b[32];
    int n;
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (!v) return;
    ns = gst_rt_running_ns();
    sec = (unsigned)(ns / 1000000000ull);
    ms = (unsigned)((ns / 1000000ull) % 1000ull);
    orch_copy(b, 32, "t ");
    n = 2 + orch_fmt_u(b + 2, 12, sec);
    if (n > 2 && n < 20) {
        b[n++] = '.';
        b[n++] = (char)('0' + (int)(ms / 100u));
        b[n++] = (char)('0' + (int)((ms / 10u) % 10u));
        b[n++] = (char)('0' + (int)(ms % 10u));
        b[n++] = ' ';
        b[n++] = 's';
        b[n] = 0;
    }
    orch_status_set(b);
}

void orch_cmd_buffer_text(void)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (!v) return;
    orch_status_set("Q 32 | host 1024");
}

void orch_cmd_dsp_text(void)
{
    OrchView *v = gst_view();
    char b[40];
    int n;
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (!v) return;
    orch_copy(b, 40, "wcet ");
    n = 5 + orch_fmt_u(b + 5, 12, gst_rt_wcet_us());
    if (n > 5 && n < 24) {
        b[n++] = ' ';
        b[n++] = 'T';
        b[n++] = ' ';
        (void)orch_fmt_u(b + n, 10, gst_rt_period_us());
    }
    orch_status_set(b);
}

void orch_cmd_diag_text(void)
{
    OrchView *v = gst_view();
    static const char *ST[4] = { "NULL", "READY", "PAUSED", "PLAYING" };
    char b[40];
    int n;
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (!v) return;
    orch_copy(b, 40, "elem ");
    n = 5 + orch_fmt_u(b + 5, 8, (unsigned)gst_rt_element_count());
    if (n > 5 && n < 20 && v->state <= ORCH_GST_PLAYING) {
        b[n++] = ' ';
        b[n] = 0;
        (void)add_s(b, n, 40, ST[v->state]);
    }
    orch_status_set(b);
}

void orch_cmd_port_text(void)
{
    OrchView *v = gst_view();
    int i;
    int found = 0;
    char b[32];
    int n;
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (!v) return;
    for (i = 0; i < 32; i++) {
        if (v->port[i].is_input && v->port[i].is_active) {
            found = i;
            break;
        }
    }
    orch_copy(b, 32, "port ");
    n = 5 + orch_fmt_u(b + 5, 8, (unsigned)found);
    if (n > 5 && n < 16) {
        b[n++] = ' ';
        b[n++] = 'n';
        b[n++] = ' ';
        (void)orch_fmt_u(b + n, 12, v->port[found].message_count);
    }
    orch_status_set(b);
}

void orch_cmd_latency_text(void)
{
    OrchView *v = gst_view();
    unsigned ms;
    char b[24];
    int n;
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (!v) return;
    ms = gst_rt_queued_ms();
    gst_seq_begin();
    v->queued_ms = ms;
    gst_seq_end();
    orch_copy(b, 24, "q ");
    n = 2 + orch_fmt_u(b + 2, 12, ms);
    if (n > 2 && n < 16) {
        b[n++] = ' ';
        b[n++] = 'm';
        b[n++] = 's';
        b[n] = 0;
    }
    orch_status_set(b);
    right_ms(ms);
}

int orch_cmd_wav(void)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (!v) return 0;
    return gst_rt_export_wav(ORCH_PATH_WAV);
}

static int add_hdr(const char *title, int w)
{
    int h;
    orch_check(title != 0, __LINE__);
    orch_check(w > 8 && w < 200, __LINE__);
    h = app_menu_add_header(&s_menu, title, (H)w);
    return h;
}

static void add_item(int h, const char *label, const char *accel, int cmd)
{
    orch_check(h >= 0 && h < 8, __LINE__);
    orch_check(cmd > 0 && cmd < 1000, __LINE__);
    if (h < 0 || !label || !accel) return;
    (void)app_menu_add_item(&s_menu, h, label, accel, cmd, TRUE);
}

static void add_open(int h)
{
    int ix;
    orch_check(h == 0, __LINE__);
    orch_check(s_menu.header_count == 1, __LINE__);
    ix = app_menu_add_submenu_item(&s_menu, h, "開く (Open) ▶", 102, 1);
    if (ix < 0) return;
    orch_copy(s_menu.headers[h].items[ix].accel, 16, "Ctrl+O");
}

static void menu_file(void)
{
    int h = add_hdr("ファイル(F)", 104);
    orch_check(h == 0, __LINE__);
    orch_check(s_menu.header_count == 1, __LINE__);
    add_item(h, "新規作成 (New)", "Ctrl+N", 101);
    add_open(h);
    add_item(h, "上書き保存 (Save)", "Ctrl+S", 103);
    (void)app_menu_add_separator(&s_menu, h);
    add_item(h, "SMFを読み込む (Load SMF)", "Ctrl+L", 104);
    add_item(h, "SMFを書き出す (Save SMF)", "Ctrl+M", 105);
    add_item(h, "WAVを書き出す (Export WAV)", "", 106);
    (void)app_menu_add_separator(&s_menu, h);
    add_item(h, "閉じる (Close)", "Ctrl+W", 107);
}

static void menu_concert(void)
{
    int h = add_hdr("コンサート(C)", 120);
    orch_check(h == 1, __LINE__);
    orch_check(s_menu.header_count == 2, __LINE__);
    add_item(h, "再生 (Play)", "Space", 320);
    add_item(h, "停止 (Stop)", "", 321);
    add_item(h, "録音 (Record)", "Ctrl+R", 322);
    add_item(h, "パニック (Panic)", "Ctrl+K", 317);
    (void)app_menu_add_separator(&s_menu, h);
    add_item(h, "クリック (Click)", "", 323);
    add_item(h, "ロック (Rock)", "", 801);
    add_item(h, "クラシック (Classic)", "", 802);
    add_item(h, "オーケストラ (Orchestra)", "", 803);
}

static void menu_scene(void)
{
    int h = add_hdr("シーン(S)", 88);
    orch_check(h == 2, __LINE__);
    orch_check(s_menu.header_count == 3, __LINE__);
    add_item(h, "送信 (Send)", "", 318);
    add_item(h, "オクターブ (Octave)", "", 319);
}

static void menu_track(void)
{
    int h = add_hdr("トラック(T)", 104);
    orch_check(h == 3, __LINE__);
    orch_check(s_menu.header_count == 4, __LINE__);
    add_item(h, "追加 (Add)", "Ctrl+T", 201);
    add_item(h, "削除 (Delete)", "Ctrl+D", 205);
    add_item(h, "分割 (Split)", "Ctrl+E", 202);
    add_item(h, "ベロシティ (Velocity)", "", 208);
    add_item(h, "チャンネル (Channel)", "", 204);
    add_item(h, "リターン (AUX)", "", 206);
}

static void menu_num(char *b, int n)
{
    orch_check(b != 0, __LINE__);
    orch_check(n >= 1 && n <= 16, __LINE__);
    if (n < 10) {
        b[0] = (char)('0' + n);
        b[1] = 0;
        return;
    }
    b[0] = (char)('0' + n / 10);
    b[1] = (char)('0' + n % 10);
    b[2] = 0;
}

static void menu_device(void)
{
    int h = add_hdr("装置(D)", 72);
    orch_check(h == 4, __LINE__);
    orch_check(s_menu.header_count == 5, __LINE__);
    add_item(h, "バッファ (Buffer)", "", 404);
    add_item(h, "DSP", "", 405);
    add_item(h, "診断 (Diag)", "", 406);
    (void)app_menu_add_separator(&s_menu, h);
    add_item(h, "SMPTE", "", 501);
    add_item(h, "クロック (Clock)", "", 502);
    (void)app_menu_add_separator(&s_menu, h);
    add_item(h, "ポート (Port)", "", 902);
    add_item(h, "遅延 (Latency)", "", 903);
}

static void menu_window(void)
{
    int h = add_hdr("ウィンドウ(W)", 120);
    orch_check(h == 5, __LINE__);
    orch_check(s_menu.header_count == 6, __LINE__);
    add_item(h, "ステージ (Stage)", "", 701);
    add_item(h, "アレンジ (Arrange)", "", 702);
    add_item(h, "ピアノ (Piano)", "", 703);
    add_item(h, "スコア (Score)", "", 704);
    add_item(h, "ミキサー (Mix)", "", 705);
    add_item(h, "インサート (Insert)", "", 706);
    add_item(h, "アバウト (About)", "", 707);
}

static void menu_build(void)
{
    orch_check(s_menu_ok == 0 || s_menu_ok == 1, __LINE__);
    orch_check(s_menu.header_count >= 0, __LINE__);
    if (s_menu_ok) return;
    app_menu_init(&s_menu, APP_MENU_STYLE_CLASSIC_3D);
    menu_file();
    menu_concert();
    menu_scene();
    menu_track();
    menu_device();
    menu_window();
    app_menu_set_right_text(&s_menu, "0 ms");
    s_menu_ok = 1;
    orch_menu_scenes();
}

static void orch_menu_scenes(void)
{
    OrchView *v = gst_view();
    int i;
    char b[4];
    orch_check(v != 0, __LINE__);
    orch_check(s_menu_ok == 0 || s_menu_ok == 1, __LINE__);
    if (!v || !s_menu_ok || s_menu.header_count < 3) return;
    if (s_menu.headers[2].item_count < 16) return;
    for (i = 0; i < 16; i++) {
        const char *nm = v->scn[i].name;
        if (nm[0] == 0) {
            menu_num(b, i + 1);
            orch_copy(s_menu.headers[2].items[i].label, 64, b);
        } else {
            orch_copy(s_menu.headers[2].items[i].label, 64, nm);
        }
    }
}

void orch_menu_paint(GDEV *dev)
{
    orch_check(dev != 0, __LINE__);
    orch_check(s_menu_ok == 0 || s_menu_ok == 1, __LINE__);
    if (!dev || !s_menu_ok) return;
    app_menu_paint_bar(&s_menu, dev);
    app_menu_paint_dropdown(&s_menu, dev);
    if (s_menu.active_submenu >= 0) {
        app_menu_paint_cascading_strings(&s_menu, dev, k_open_lbl, OPEN_N);
    }
}

int orch_menu_move(int x, int y)
{
    orch_check(s_menu_ok == 0 || s_menu_ok == 1, __LINE__);
    orch_check(x > -4000 && y > -4000, __LINE__);
    if (!s_menu_ok) return 0;
    return app_menu_handle_mouse_move(&s_menu, (H)x, (H)y) ? 1 : 0;
}

static void run_cmd(int cmd);

int orch_menu_down(int x, int y)
{
    int cmd = 0;
    int sub = 0;
    orch_check(s_menu_ok == 0 || s_menu_ok == 1, __LINE__);
    orch_check(x > -4000 && y > -4000, __LINE__);
    if (!s_menu_ok) return 0;
    if (!app_menu_handle_mouse_down(&s_menu, (H)x, (H)y, &cmd, &sub)) return 0;
    if (cmd == 102 && sub >= 0) {
        (void)orch_cmd_open_at(sub);
        orch_inval_all();
    } else if (cmd > 0) {
        run_cmd(cmd);
    }
    return 1;
}

int orch_menu_is_open(void)
{
    orch_check(s_menu_ok == 0 || s_menu_ok == 1, __LINE__);
    orch_check(s_menu.active_menu < 8, __LINE__);
    return (s_menu_ok && s_menu.active_menu >= 0) ? 1 : 0;
}

BOOL orch_menu_no(WND *wnd)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    (void)wnd;
    return FALSE;
}

static void cmd_file(int cmd)
{
    orch_check(cmd >= 101 && cmd <= 107, __LINE__);
    orch_check(gst_view() != 0, __LINE__);
    if (cmd == 101) (void)orch_cmd_new();
    else if (cmd == 102) (void)orch_cmd_open();
    else if (cmd == 103) (void)orch_cmd_save();
    else if (cmd == 104) (void)orch_cmd_smf_load();
    else if (cmd == 105) (void)orch_cmd_smf_save();
    else if (cmd == 106) (void)orch_cmd_wav();
    else (void)orch_shutdown_desk();
    orch_menu_scenes();
}

static void cmd_track(int cmd)
{
    orch_check(cmd >= 201 && cmd <= 208, __LINE__);
    orch_check(gst_view() != 0, __LINE__);
    if (cmd == 201) (void)orch_cmd_add_track();
    else if (cmd == 205) (void)orch_cmd_del_track();
    else if (cmd == 206) orch_cmd_aux_show();
    else if (cmd == 202) (void)orch_cmd_split();
    else if (cmd == 208) (void)orch_cmd_velocity();
    else if (cmd == 204) (void)orch_cmd_channel();
}

static void cmd_stage(int cmd)
{
    orch_check(cmd >= 301 && cmd <= 319, __LINE__);
    orch_check(gst_view() != 0, __LINE__);
    if (cmd >= 301 && cmd <= 316) (void)orch_cmd_scene(cmd - 301);
    else if (cmd == 318) (void)orch_cmd_send_programs();
    else if (cmd == 317) (void)orch_cmd_panic();
    else if (cmd == 319) (void)orch_cmd_octave();
}

static void cmd_concert(int cmd)
{
    orch_check(cmd >= 320 && cmd <= 323, __LINE__);
    orch_check(gst_view() != 0, __LINE__);
    if (cmd == 320) (void)orch_cmd_play();
    else if (cmd == 321) (void)orch_cmd_stop();
    else if (cmd == 322) orch_desk_rec_flip();
    else (void)orch_cmd_click();
}

static void cmd_pipe(int cmd)
{
    orch_check(cmd >= 404 && cmd <= 406, __LINE__);
    orch_check(gst_view() != 0, __LINE__);
    if (cmd == 404) orch_cmd_buffer_text();
    else if (cmd == 405) orch_cmd_dsp_text();
    else orch_cmd_diag_text();
}

static void cmd_win(int cmd)
{
    int id = cmd - 701;
    orch_check(id >= 0 && id < 7, __LINE__);
    orch_check(cmd >= 701 && cmd <= 707, __LINE__);
    if (id == 0) (void)orch_open_stage();
    else if (id == 1) (void)orch_open_arrange();
    else if (id == 2) (void)orch_open_piano();
    else if (id == 3) (void)orch_open_score();
    else if (id == 4) (void)orch_open_mix();
    else if (id == 5) (void)orch_open_insert();
    else (void)orch_open_about();
}

static void run_cmd(int cmd)
{
    orch_check(cmd > 0 && cmd < 1000, __LINE__);
    orch_check(gst_view() != 0, __LINE__);
    if (cmd >= 101 && cmd <= 107) cmd_file(cmd);
    else if (cmd >= 201 && cmd <= 208) cmd_track(cmd);
    else if (cmd >= 301 && cmd <= 319) cmd_stage(cmd);
    else if (cmd >= 320 && cmd <= 323) cmd_concert(cmd);
    else if (cmd >= 404 && cmd <= 406) cmd_pipe(cmd);
    else if (cmd == 501) orch_cmd_tc_cycle();
    else if (cmd == 502) orch_cmd_master_clock();
    else if (cmd >= 801 && cmd <= 803) (void)orch_cmd_family(cmd - 801);
    else if (cmd >= 701 && cmd <= 707) cmd_win(cmd);
    else if (cmd == 901) (void)orch_open_about();
    else if (cmd == 902) orch_cmd_port_text();
    else if (cmd == 903) orch_cmd_latency_text();
    orch_inval_all();
}

int orch_menu_key(UW key, VW data)
{
    int cmd = 0;
    uint16_t mod;
    OrchView *v;
    orch_check(s_menu_ok == 0 || s_menu_ok == 1, __LINE__);
    orch_check(s_menu.header_count <= APP_MENU_MAX_HEADERS, __LINE__);
    if (!s_menu_ok) return 0;
    mod = (uint16_t)(uintptr_t)data;
    if (app_menu_handle_key(&s_menu, key, mod, &cmd)) {
        if (cmd > 0) run_cmd(cmd);
        return 1;
    }
    if (key != (UW)' ' && key != (UW)BTRON_KEY_SPACE) return 0;
    if ((mod & (BTRON_KMOD_CTRL | BTRON_KMOD_ALT)) != 0) return 0;
    v = gst_view();
    if (!v) return 0;
    if (v->state == ORCH_GST_PLAYING) (void)orch_cmd_stop();
    else (void)orch_cmd_play();
    orch_inval_all();
    return 1;
}

void orch_inval_all(void)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    orch_stage_inval();
    orch_arrange_inval();
    orch_piano_inval();
    orch_score_inval();
    orch_mix_inval();
    orch_insert_inval();
    orch_about_inval();
}

WND *open_orchestra_window(void)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    orch_boot_desk();
    menu_build();
    return orch_open_stage();
}

WND *open_orchestra_about_window(void)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    return orch_open_about();
}
