/*
 * Session file and the three templates. No windows, no device writes.
 * Field layout is section 7.1: 128 + 64*96 + 16*100 + 4096*16 = 73408.
 */
#include <apps/orchestra.h>
#include <apps/orch_chk.h>
#include "../demo/lilcu64_wa.h"
#include "../demo/lilcu64_insert.h"
#include <stdio.h>
#include <string.h>
#include <btron/libc_shim.h>
#if BTRON_HOSTED
#include <sys/stat.h>
#endif

enum {
    OFF_TRK = 128,
    TRK_N = 96,
    OFF_SCN = 6272,
    SCN_N = 100,
    OFF_EV = 7872,
    EV_N = 16,
    UNDO_CAP = 16
};

enum {
    UNDO_OVER = 0,
    UNDO_INSERT_EV = 1,
    UNDO_DROP_EV = 2
};

typedef struct OrchTrow {
    const char *name;
    uint8_t ch, pr, vol, pan, snd, i0, i1;
    uint32_t col;
    int16_t p0[4];
    int16_t p1[4];
} OrchTrow;

typedef struct UndoSlot {
    uint8_t kind;
    uint8_t track;
    uint16_t index;
    uint8_t n;
    uint8_t mode;
    uint8_t before[48];
} UndoSlot;

enum { ORCH_PATH_N = 128 };

static uint8_t s_file[ORCH_FILE_BYTES];
static char s_orch_path[ORCH_PATH_N];
static char s_orch_alt[ORCH_PATH_N];
static UndoSlot s_undo[UNDO_CAP];
static uint8_t s_undo_len;
static uint8_t s_undo_at;
static uint8_t s_in_undo;

static const char DASH[] = "\xE2\x80\x94";

static const OrchTrow ROCK_ROW[8] = {
    { "Kick", 1, 116, 110, 64, 16, ORCH_INS_GATE, ORCH_INS_EQ, ORCH_COL_RED,
      {8, 30, 20, 0}, {0, 120, 70, 0} },
    { "Snare", 2, 117, 104, 64, 20, ORCH_INS_GATE, ORCH_INS_EQ, ORCH_COL_AMBER,
      {8, 30, 20, 0}, {2, 2400, 200, 0} },
    { "Hat", 3, 118, 90, 80, 24, ORCH_INS_GATE, ORCH_INS_EQ, ORCH_COL_SILVER,
      {8, 30, 20, 0}, {1, 7000, 70, 0} },
    { "Bass", 4, 33, 108, 64, 20, ORCH_INS_SAT, ORCH_INS_EQ, ORCH_COL_ORANGE,
      {25, 0, 0, 0}, {0, 800, 70, 0} },
    { "Guitar", 5, 26, 100, 40, 48, ORCH_INS_SAT, ORCH_INS_DELAY, ORCH_COL_PURPLE,
      {25, 0, 0, 0}, {3, 35, 30, 0} },
    { "Keys", 6, 4, 96, 84, 56, ORCH_INS_EQ, ORCH_INS_CHORUS, ORCH_COL_CYAN,
      {3, 800, 100, 2}, {18, 4, 4, 25} },
    { "Lead", 7, 81, 104, 70, 40, ORCH_INS_SAT, ORCH_INS_DELAY, ORCH_COL_GREEN,
      {25, 0, 0, 0}, {3, 35, 30, 0} },
    { "Voice", 8, 52, 100, 64, 64, ORCH_INS_EQ, ORCH_INS_GATE, ORCH_COL_PINK,
      {2, 1200, 400, 0}, {6, 40, 30, 0} }
};

static const uint8_t ROCK_VOL[8][8] = {
    {100, 96, 84, 0, 0, 0, 0, 0},
    {110, 104, 90, 108, 100, 96, 96, 100},
    {114, 110, 96, 112, 108, 100, 110, 108},
    {100, 96, 80, 104, 112, 90, 120, 70},
    {90, 84, 70, 96, 80, 108, 90, 112},
    {108, 100, 0, 112, 0, 0, 0, 0},
    {100, 96, 84, 100, 96, 90, 90, 96},
    {0, 0, 0, 0, 0, 0, 0, 0}
};

static const OrchTrow CLASSIC_ROW[6] = {
    { "Voice", 1, 52, 96, 64, 80, ORCH_INS_EQ, ORCH_INS_GATE, ORCH_COL_PINK,
      {2, 1200, 400, 0}, {5, 50, 40, 0} },
    { "Piano", 2, 0, 100, 64, 80, ORCH_INS_EQ, ORCH_INS_CHORUS, ORCH_COL_AMBER,
      {3, 400, 70, 2}, {12, 2, 2, 15} },
    { "Flute", 3, 73, 90, 40, 80, ORCH_INS_EQ, ORCH_INS_BYPASS, ORCH_COL_CYAN,
      {2, 2000, 500, 0}, {0, 0, 0, 0} },
    { "Violin", 4, 40, 96, 88, 80, ORCH_INS_EQ, ORCH_INS_BYPASS, ORCH_COL_PURPLE,
      {3, 520, 220, 3}, {0, 0, 0, 0} },
    { "Cello", 5, 42, 96, 72, 80, ORCH_INS_EQ, ORCH_INS_BYPASS, ORCH_COL_GREEN,
      {0, 520, 220, 0}, {0, 0, 0, 0} },
    { "Continuo", 6, 38, 88, 64, 80, ORCH_INS_EQ, ORCH_INS_BYPASS, ORCH_COL_ORANGE,
      {0, 200, 70, 0}, {0, 0, 0, 0} }
};

static const uint8_t CLASSIC_VOL[6][6] = {
    {104, 90, 80, 70, 80, 84},
    {96, 100, 90, 96, 96, 88},
    {108, 104, 96, 104, 100, 92},
    {0, 112, 0, 100, 0, 0},
    {110, 70, 0, 0, 0, 80},
    {0, 0, 0, 0, 0, 0}
};

static const OrchTrow ORCH_ROW[16] = {
    { "Vl1", 1, 40, 64, 40, 64, ORCH_INS_EQ, ORCH_INS_BYPASS, 0,
      {3, 520, 220, 3}, {0, 0, 0, 0} },
    { "Vl2", 2, 40, 64, 88, 64, ORCH_INS_EQ, ORCH_INS_BYPASS, 0,
      {3, 520, 220, 3}, {0, 0, 0, 0} },
    { "Vla", 3, 41, 64, 56, 64, ORCH_INS_EQ, ORCH_INS_BYPASS, 0,
      {3, 520, 220, 3}, {0, 0, 0, 0} },
    { "Vc", 4, 42, 64, 72, 64, ORCH_INS_EQ, ORCH_INS_BYPASS, 0,
      {0, 520, 220, 0}, {0, 0, 0, 0} },
    { "Cb", 5, 43, 64, 48, 64, ORCH_INS_EQ, ORCH_INS_BYPASS, 0,
      {0, 400, 220, 0}, {0, 0, 0, 0} },
    { "Fl", 6, 73, 64, 36, 64, ORCH_INS_EQ, ORCH_INS_BYPASS, 0,
      {2, 2000, 500, 0}, {0, 0, 0, 0} },
    { "Ob", 7, 68, 64, 48, 64, ORCH_INS_EQ, ORCH_INS_BYPASS, 0,
      {2, 1800, 800, 0}, {0, 0, 0, 0} },
    { "Cl", 8, 71, 64, 80, 64, ORCH_INS_EQ, ORCH_INS_BYPASS, 0,
      {2, 1500, 500, 0}, {0, 0, 0, 0} },
    { "Bsn", 9, 70, 64, 96, 64, ORCH_INS_EQ, ORCH_INS_BYPASS, 0,
      {0, 900, 150, 0}, {0, 0, 0, 0} },
    { "Hn", 10, 60, 64, 44, 64, ORCH_INS_EQ, ORCH_INS_BYPASS, 0,
      {0, 800, 200, 0}, {0, 0, 0, 0} },
    { "Tpt", 11, 56, 64, 64, 64, ORCH_INS_EQ, ORCH_INS_BYPASS, 0,
      {0, 1800, 200, 0}, {0, 0, 0, 0} },
    { "Tbn", 12, 57, 64, 84, 64, ORCH_INS_EQ, ORCH_INS_BYPASS, 0,
      {0, 900, 200, 0}, {0, 0, 0, 0} },
    { "Tba", 13, 58, 64, 104, 64, ORCH_INS_EQ, ORCH_INS_BYPASS, 0,
      {0, 400, 150, 0}, {0, 0, 0, 0} },
    { "Timp", 14, 47, 64, 64, 64, ORCH_INS_EQ, ORCH_INS_BYPASS, 0,
      {0, 260, 70, 0}, {0, 0, 0, 0} },
    { "Hp", 15, 46, 64, 32, 64, ORCH_INS_EQ, ORCH_INS_BYPASS, 0,
      {1, 80, 70, 0}, {0, 0, 0, 0} },
    { "Perc", 16, 112, 64, 96, 64, ORCH_INS_EQ, ORCH_INS_BYPASS, 0,
      {1, 2000, 70, 0}, {0, 0, 0, 0} }
};

static const uint16_t ORCH_MASK[8] = {
    0xFFFFu, 0x001Fu, 0x01E0u, 0x1E00u, 0x0001u, 0x4000u, 0x0000u, 0x001Fu
};
static const uint8_t ORCH_LV[8] = { 64, 64, 64, 64, 64, 64, 0, 40 };

static uint32_t rd32(const uint8_t *b, uint32_t off)
{
    uint32_t v;
    orch_check(b != 0, __LINE__);
    orch_check(off + 3u < ORCH_FILE_BYTES, __LINE__);
    v = (uint32_t)b[off] | ((uint32_t)b[off + 1u] << 8);
    v |= ((uint32_t)b[off + 2u] << 16) | ((uint32_t)b[off + 3u] << 24);
    return v;
}
static uint16_t rd16(const uint8_t *b, uint32_t off)
{
    orch_check(b != 0, __LINE__);
    orch_check(off + 1u < ORCH_FILE_BYTES, __LINE__);
    return (uint16_t)((uint16_t)b[off] | ((uint16_t)b[off + 1u] << 8));
}
static void wr32(uint8_t *b, uint32_t off, uint32_t v)
{
    orch_check(b != 0, __LINE__);
    orch_check(off + 3u < ORCH_FILE_BYTES, __LINE__);
    b[off] = (uint8_t)v;
    b[off + 1u] = (uint8_t)(v >> 8);
    b[off + 2u] = (uint8_t)(v >> 16);
    b[off + 3u] = (uint8_t)(v >> 24);
}
static void wr16(uint8_t *b, uint32_t off, uint32_t v)
{
    orch_check(b != 0, __LINE__);
    orch_check(off + 1u < ORCH_FILE_BYTES, __LINE__);
    b[off] = (uint8_t)v;
    b[off + 1u] = (uint8_t)(v >> 8);
}
static int span_zero(const uint8_t *p, int n)
{
    int i;
    orch_check(p != 0, __LINE__);
    orch_check(n >= 0 && n <= TRK_N, __LINE__);
    for (i = 0; i < n; i++) {
        if (p[i] != 0) return 0;
    }
    return 1;
}
static int den_ok(unsigned den)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    if (!v) return 0;
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    return den == 2u || den == 4u || den == 8u || den == 16u;
}
static int bpm_ok(unsigned bpm)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    if (!v) return 0;
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    return bpm >= 40u && bpm <= 240u;
}
static int refuse_session(void)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    if (!v) return 0;
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    orch_count_sat(&v->refuse_session);
    orch_status_hold("session");
    return 0;
}
static int refuse_tempo(void)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    if (!v) return 0;
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    orch_count_sat(&v->refuse_tempo);
    orch_status_hold("tempo");
    return 0;
}
static int playing(void)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    if (!v) return 0;
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    return v->state == ORCH_GST_PLAYING;
}
static int path_field(const uint8_t *p)
{
    const char *lit = ORCH_PATH_ORCH;
    int i, n = 0;
    orch_check(p != 0, __LINE__);
    while (lit[n] != 0 && n < 90) n++;
    orch_check(n > 0 && n < 90, __LINE__);
    for (i = 0; i < 96; i++) {
        uint8_t want = (i <= n) ? (uint8_t)lit[i] : 0;
        if (p[i] != want) return 0;
    }
    return 1;
}
static int hdr_ok(const uint8_t *b)
{
    OrchView *v = gst_view();
    uint32_t fs, evn;
    unsigned tempo, den, num;
    orch_check(b != 0, __LINE__);
    orch_check(v != 0, __LINE__);
    if (!v) return 0;
    if (b[0] != 'O' || b[1] != 'R' || b[2] != 'C' || b[3] != 'H') return 0;
    if (rd16(b, 4) != 1u) return 0;
    tempo = rd16(b, 6);
    fs = rd32(b, 8);
    num = b[12];
    den = b[13];
    if (!bpm_ok(tempo) || !den_ok(den) || num < 1u || num > 12u) return 0;
    if (fs != 44100u && fs != 48000u) return 0;
    if (v->state != ORCH_GST_NULL && fs != v->sample_rate) return 0;
    if (b[14] > ORCH_FAM_ORCHESTRA) return 0;
    if (b[15] > 1u || b[16] > 15u || b[17] > 64u || b[18] != 16u) return 0;
    if (b[19] > 127u) return 0;
    if (b[20] != ORCH_INS_BYPASS && b[20] != ORCH_INS_LIMITER) return 0;
    if (b[22] > ORCH_TOOL_ERASE || b[23] > ORCH_SNAP_16TH) return 0;
    evn = rd32(b, 24);
    if (evn > ORCH_MAX_EVENTS) return 0;
    if (b[28] > 100u || b[29] > 100u || b[30] > 100u || b[31] > 100u) return 0;
    if (!path_field(b + 32)) return 0;
    return 1;
}
static int one_track_ok(const uint8_t *p, int index)
{
    uint8_t pr, i0, i1;
    orch_check(p != 0, __LINE__);
    orch_check(index >= 0 && index < 64, __LINE__);
    if (!span_zero(p + 85, 11)) return 0;
    if (p[48] < 1u || p[48] > 16u) return 0;
    if (p[49] > 31u || p[51] > 127u || p[52] > 127u) return 0;
    if (p[51] > p[52]) return 0;
    if (p[54] > 2u || p[55] > 1u || p[56] > 1u) return 0;
    if (p[57] > 127u || p[58] > 127u || p[59] > 127u) return 0;
    pr = p[60];
    i0 = p[61];
    i1 = p[62];
    if (!gst_program_known(pr)) return 0;
    if (i0 > ORCH_INS_LIMITER || i1 > ORCH_INS_LIMITER) return 0;
    return 1;
}
static int type_over(const uint8_t *b, int ntrk)
{
    int id, i, n;
    orch_check(b != 0, __LINE__);
    orch_check(ntrk >= 0 && ntrk <= 64, __LINE__);
    for (id = 1; id <= ORCH_INS_LIMITER; id++) {
        n = 0;
        for (i = 0; i < ntrk && i < 64; i++) {
            const uint8_t *p = b + OFF_TRK + i * TRK_N;
            if (p[61] == (uint8_t)id) n++;
            if (p[62] == (uint8_t)id) n++;
        }
        if (n > 8) return 1;
    }
    return 0;
}
static int tracks_ok(const uint8_t *b)
{
    int ntrk, i;
    orch_check(b != 0, __LINE__);
    orch_check(b[17] <= 64u, __LINE__);
    ntrk = (int)b[17];
    for (i = 0; i < 64; i++) {
        const uint8_t *p = b + OFF_TRK + i * TRK_N;
        if (i >= ntrk) {
            if (!span_zero(p, TRK_N)) return 0;
        } else if (!one_track_ok(p, i)) {
            return 0;
        }
    }
    if (type_over(b, ntrk)) return 0;
    return 1;
}
static int scenes_ok(const uint8_t *b)
{
    OrchView *v = gst_view();
    int i, k;
    orch_check(b != 0, __LINE__);
    orch_check(v != 0, __LINE__);
    if (!v) return 0;
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    for (i = 0; i < 16; i++) {
        const uint8_t *p = b + OFF_SCN + i * SCN_N;
        unsigned tempo = rd16(p, 32);
        if (!bpm_ok(tempo) || p[34] < 1u || p[34] > 12u) return 0;
        if (!den_ok(p[35])) return 0;
        for (k = 0; k < 16; k++) {
            uint8_t pr = p[36 + k];
            if (p[52 + k] > 127u) return 0;
            if (pr != 255u && !gst_program_known(pr)) return 0;
        }
    }
    return 1;
}
static int events_ok(const uint8_t *b)
{
    uint32_t n, i;
    int ntrk;
    orch_check(b != 0, __LINE__);
    orch_check(b[17] <= 64u, __LINE__);
    n = rd32(b, 24);
    ntrk = (int)b[17];
    if (n > ORCH_MAX_EVENTS) return 0;
    for (i = 0; i < ORCH_MAX_EVENTS; i++) {
        const uint8_t *p = b + OFF_EV + i * EV_N;
        if (i >= n) {
            if (!span_zero(p, EV_N)) return 0;
            continue;
        }
        if (p[2] == 0u || p[3] != 0u) return 0;
        if (p[0] >= (uint8_t)ntrk || p[1] > 127u) return 0;
        if (rd32(p, 12) != 0u) return 0;
    }
    return 1;
}
static void copy_name(char *dst, int cap, const char *src)
{
    int i;
    orch_check(dst != 0, __LINE__);
    orch_check(cap > 1 && cap <= ORCH_NAME, __LINE__);
    for (i = 0; i < cap; i++) dst[i] = 0;
    if (!src) return;
    for (i = 0; i < cap - 1 && src[i] != 0; i++) dst[i] = src[i];
}
static void put_i16s(int16_t *dst, const uint8_t *p)
{
    int k;
    orch_check(dst != 0, __LINE__);
    orch_check(p != 0, __LINE__);
    for (k = 0; k < 4; k++) dst[k] = (int16_t)rd16(p, (uint32_t)(k * 2));
}
static void take_track(OrchTrack *t, const uint8_t *p)
{
    orch_check(t != 0, __LINE__);
    orch_check(p != 0, __LINE__);
    memset(t, 0, sizeof *t);
    memcpy(t->name, p, ORCH_NAME);
    t->name[ORCH_NAME - 1] = 0;
    t->channel = p[48];
    t->in_port = p[49];
    t->out_port = p[50];
    t->key_lo = p[51];
    t->key_hi = p[52];
    t->transpose = (int8_t)p[53];
    t->vel_curve = p[54];
    t->muted = p[55];
    t->solo = p[56];
    t->volume = p[57];
    t->pan = p[58];
    t->send = p[59];
    t->program = p[60];
    t->ins0 = p[61];
    t->ins1 = p[62];
    t->clef = p[63];
    put_i16s(t->p0, p + 64);
    put_i16s(t->p1, p + 72);
    t->accent = rd32(p, 80);
    t->poly_max = p[84];
}
static void take_scene(OrchScene *s, const uint8_t *p)
{
    int k;
    orch_check(s != 0, __LINE__);
    orch_check(p != 0, __LINE__);
    memset(s, 0, sizeof *s);
    memcpy(s->name, p, ORCH_SCENE_NAME);
    s->name[ORCH_SCENE_NAME - 1] = 0;
    s->tempo = rd16(p, 32);
    s->num = p[34];
    s->den = p[35];
    for (k = 0; k < 16; k++) {
        s->program[k] = p[36 + k];
        s->volume[k] = p[52 + k];
    }
    memcpy(s->lighting, p + 68, ORCH_CUE);
}
static void take_event(OrchEvent *e, const uint8_t *p)
{
    orch_check(e != 0, __LINE__);
    orch_check(p != 0, __LINE__);
    e->track = p[0];
    e->key = p[1];
    e->vel = p[2];
    e->flags = 0;
    e->start_tick = rd32(p, 4);
    e->dur_tick = rd32(p, 8);
    e->reserved = 0;
}
static void apply_buf(const uint8_t *b)
{
    OrchView *v = gst_view();
    int i, ntrk;
    uint32_t evn, k;
    orch_check(b != 0, __LINE__);
    orch_check(v != 0, __LINE__);
    if (!v) return;
    ntrk = (int)b[17];
    evn = rd32(b, 24);
    v->tempo = rd16(b, 6);
    v->sample_rate = rd32(b, 8);
    v->num = b[12];
    v->den = b[13];
    v->family = b[14];
    v->click = b[15];
    v->active_scene = b[16];
    v->track_count = b[17];
    v->scene_count = 16;
    v->master_vol = b[19];
    v->master_lim = b[20];
    v->tc_format = b[21];
    v->tool = b[22];
    v->snap = b[23];
    v->event_count = evn;
    v->room = b[28];
    v->damping = b[29];
    v->feedback = b[30];
    v->wet = b[31];
    v->confirm_family = -1;
    v->selected = (ntrk > 0) ? 0 : -1;
    memset(v->trk, 0, sizeof v->trk);
    memset(v->scn, 0, sizeof v->scn);
    memset(v->ev, 0, sizeof v->ev);
    memset(v->ev_live, 0, sizeof v->ev_live);
    for (i = 0; i < ntrk && i < 64; i++) {
        take_track(&v->trk[i], b + OFF_TRK + i * TRK_N);
        gst_resolve_track(&v->trk[i]);
    }
    for (i = 0; i < 16; i++) take_scene(&v->scn[i], b + OFF_SCN + i * SCN_N);
    for (k = 0; k < evn && k < ORCH_MAX_EVENTS; k++) {
        take_event(&v->ev[k], b + OFF_EV + k * EV_N);
    }
}
static void commit_buf(const uint8_t *b)
{
    OrchView *v = gst_view();
    orch_check(b != 0, __LINE__);
    orch_check(v != 0, __LINE__);
    if (!v) return;
    gst_seq_begin();
    apply_buf(b);
    gst_seq_end();
    wa_set_reverb(1, (float)v->room / 100.0f, (float)v->damping / 100.0f,
                  (float)v->feedback / 100.0f, (float)v->wet / 100.0f);
    lc_insert_reset();
    gst_rt_return_zero();
    gst_insert_touch();
    orch_status_ok();
}
static int accept_buf(const uint8_t *b)
{
    OrchView *v = gst_view();
    orch_check(b != 0, __LINE__);
    orch_check(v != 0, __LINE__);
    if (!v) return 0;
    if (!hdr_ok(b) || !tracks_ok(b) || !scenes_ok(b) || !events_ok(b)) {
        return refuse_session();
    }
    commit_buf(b);
    return 1;
}
static void put_name(uint8_t *d, int cap, const char *src)
{
    int i;
    orch_check(d != 0, __LINE__);
    orch_check(cap > 1 && cap <= ORCH_NAME, __LINE__);
    for (i = 0; i < cap; i++) d[i] = 0;
    if (!src) return;
    for (i = 0; i < cap && src[i] != 0; i++) d[i] = (uint8_t)src[i];
}
static void pack_track(uint8_t *d, const OrchTrack *t)
{
    int k;
    orch_check(d != 0, __LINE__);
    orch_check(t != 0, __LINE__);
    put_name(d, ORCH_NAME, t->name);
    d[48] = t->channel;
    d[49] = t->in_port;
    d[50] = t->out_port;
    d[51] = t->key_lo;
    d[52] = t->key_hi;
    d[53] = (uint8_t)t->transpose;
    d[54] = t->vel_curve;
    d[55] = t->muted;
    d[56] = t->solo;
    d[57] = t->volume;
    d[58] = t->pan;
    d[59] = t->send;
    d[60] = t->program;
    d[61] = t->ins0;
    d[62] = t->ins1;
    d[63] = t->clef;
    for (k = 0; k < 4; k++) {
        wr16(d, (uint32_t)(64 + k * 2), (uint32_t)(uint16_t)t->p0[k]);
        wr16(d, (uint32_t)(72 + k * 2), (uint32_t)(uint16_t)t->p1[k]);
    }
    wr32(d, 80, t->accent);
    d[84] = t->poly_max;
}
static void pack_scene(uint8_t *d, const OrchScene *s)
{
    int k;
    orch_check(d != 0, __LINE__);
    orch_check(s != 0, __LINE__);
    put_name(d, ORCH_SCENE_NAME, s->name);
    wr16(d, 32, s->tempo);
    d[34] = s->num;
    d[35] = s->den;
    for (k = 0; k < 16; k++) {
        d[36 + k] = s->program[k];
        d[52 + k] = s->volume[k];
    }
    memcpy(d + 68, s->lighting, ORCH_CUE);
}
static void pack_event(uint8_t *d, const OrchEvent *e)
{
    orch_check(d != 0, __LINE__);
    orch_check(e != 0, __LINE__);
    d[0] = e->track;
    d[1] = e->key;
    d[2] = e->vel;
    d[3] = 0;
    wr32(d, 4, e->start_tick);
    wr32(d, 8, e->dur_tick);
    wr32(d, 12, 0);
}
static void pack_view(uint8_t *b)
{
    OrchView *v = gst_view();
    int i;
    uint32_t k;
    const char *lit = ORCH_PATH_ORCH;
    orch_check(b != 0, __LINE__);
    orch_check(v != 0, __LINE__);
    if (!v) return;
    memset(b, 0, ORCH_FILE_BYTES);
    b[0] = 'O';
    b[1] = 'R';
    b[2] = 'C';
    b[3] = 'H';
    wr16(b, 4, 1u);
    wr16(b, 6, v->tempo);
    wr32(b, 8, v->sample_rate);
    b[12] = v->num;
    b[13] = v->den;
    b[14] = v->family;
    b[15] = v->click;
    b[16] = v->active_scene;
    b[17] = v->track_count;
    b[18] = 16;
    b[19] = v->master_vol;
    b[20] = v->master_lim;
    b[21] = v->tc_format;
    b[22] = v->tool;
    b[23] = v->snap;
    wr32(b, 24, v->event_count);
    b[28] = v->room;
    b[29] = v->damping;
    b[30] = v->feedback;
    b[31] = v->wet;
    for (i = 0; lit[i] != 0 && i < 95; i++) b[32 + i] = (uint8_t)lit[i];
    for (i = 0; i < (int)v->track_count && i < 64; i++) {
        pack_track(b + OFF_TRK + i * TRK_N, &v->trk[i]);
    }
    for (i = 0; i < 16; i++) pack_scene(b + OFF_SCN + i * SCN_N, &v->scn[i]);
    for (k = 0; k < v->event_count && k < ORCH_MAX_EVENTS; k++) {
        pack_event(b + OFF_EV + k * EV_N, &v->ev[k]);
    }
}
int orch_session_load_buf(const uint8_t *buf, uint32_t n)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    if (!v) return 0;
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (playing()) return refuse_session();
    if (!buf || n != ORCH_FILE_BYTES) return refuse_session();
    memcpy(s_file, buf, ORCH_FILE_BYTES);
    return accept_buf(s_file);
}
int orch_session_save_buf(uint8_t *buf, uint32_t cap)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    if (!v) return 0;
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (playing()) return refuse_session();
    if (!buf || cap < ORCH_FILE_BYTES) return refuse_session();
    pack_view(buf);
    return 1;
}
/* Hosted desktop: "/SYS/x" is also "SYS/x" beside the binary. Runtime only. */
static void copy_path(char *dst, const char *src)
{
    uint32_t i = 0;
    orch_check(dst != 0, __LINE__);
    orch_check(src != 0, __LINE__);
    for (i = 0; i + 1u < ORCH_PATH_N; i++) {
        dst[i] = src[i];
        if (src[i] == 0) return;
    }
    dst[ORCH_PATH_N - 1u] = 0;
}
static int path_stat(const char *p, struct stat *st)
{
    orch_check(p != 0, __LINE__);
    orch_check(st != 0, __LINE__);
    return stat(p, st) == 0;
}
static void resolve_orch_path(void)
{
    const char *alt = 0;
    struct stat st;
    uint32_t n = 0;
    while (ORCH_PATH_ORCH[n] != 0 && n < ORCH_PATH_N) n++;
    orch_check(n >= 2u, __LINE__);
    orch_check(n + 1u < ORCH_PATH_N, __LINE__);
    copy_path(s_orch_path, ORCH_PATH_ORCH);
    alt = (ORCH_PATH_ORCH[0] == '/') ? &ORCH_PATH_ORCH[1] : ORCH_PATH_ORCH;
    copy_path(s_orch_alt, alt);
    if (path_stat(s_orch_path, &st)) return;
    if (s_orch_path[0] != '/') return;
    copy_path(s_orch_path, s_orch_alt);
}
static const char *open_lit(int idx)
{
    static const char *const lit[5] = {
        "/SYS/orchestra.orch",
        "/SYS/bells.orch",
        "/SYS/caterpillar.orch",
        "/SYS/rising.orch",
        "/SYS/tron.orch"
    };
    orch_check(lit[0][0] == '/', __LINE__);
    orch_check(lit[4][0] == '/', __LINE__);
    if (idx < 0 || idx > 4) return 0;
    return lit[idx];
}
static int pick_host(char *dst, const char *lit)
{
    struct stat st;
    orch_check(dst != 0, __LINE__);
    orch_check(lit != 0, __LINE__);
    copy_path(dst, lit);
    if (path_stat(dst, &st)) return 1;
    if (lit[0] != '/') return 0;
    copy_path(dst, lit + 1);
    return path_stat(dst, &st) ? 1 : 0;
}
static int read_orch(const char *path)
{
    FILE *fp;
    struct stat st;
    size_t n;
    int closed;
    orch_check(path != 0, __LINE__);
    orch_check(path[0] != 0, __LINE__);
    if (playing()) return refuse_session();
    if (!path_stat(path, &st)) return refuse_session();
    if ((unsigned long long)st.st_size != (unsigned long long)ORCH_FILE_BYTES) {
        return refuse_session();
    }
    fp = fopen(path, "rb");
    if (!fp) return refuse_session();
    n = fread(s_file, 1, ORCH_FILE_BYTES, fp);
    closed = fclose(fp);
    if (closed != 0 || n != ORCH_FILE_BYTES) return refuse_session();
    return accept_buf(s_file);
}
int orch_cmd_open_at(int idx)
{
    const char *lit;
    char dst[ORCH_PATH_N];
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (!v) return 0;
    lit = open_lit(idx);
    if (!lit) return refuse_session();
    if (!pick_host(dst, lit)) return refuse_session();
    return read_orch(dst);
}
int orch_cmd_open(void)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (!v) return 0;
    return orch_cmd_open_at(0);
}
int orch_cmd_save(void)
{
    FILE *fp;
    size_t n;
    int closed;
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    if (!v) return 0;
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (playing()) return refuse_session();
    resolve_orch_path();
    pack_view(s_file);
    fp = fopen(s_orch_path, "wb");
    if (!fp) return refuse_session();
    n = fwrite(s_file, 1, ORCH_FILE_BYTES, fp);
    closed = fclose(fp);
    if (closed != 0 || n != ORCH_FILE_BYTES) return refuse_session();
    orch_status_ok();
    return 1;
}
static void put_row(OrchTrack *t, const OrchTrow *r)
{
    int k;
    orch_check(t != 0, __LINE__);
    orch_check(r != 0, __LINE__);
    memset(t, 0, sizeof *t);
    copy_name(t->name, ORCH_NAME, r->name);
    t->channel = r->ch;
    t->program = r->pr;
    t->volume = r->vol;
    t->pan = r->pan;
    t->send = r->snd;
    t->ins0 = r->i0;
    t->ins1 = r->i1;
    t->accent = r->col;
    t->key_hi = 127;
    for (k = 0; k < 4; k++) {
        t->p0[k] = r->p0[k];
        t->p1[k] = r->p1[k];
    }
}
static void scene_progs(OrchScene *s, const OrchTrack *t, int ntrk)
{
    int i;
    orch_check(s != 0, __LINE__);
    orch_check(ntrk >= 0 && ntrk <= 16, __LINE__);
    for (i = 0; i < 16; i++) s->program[i] = 255;
    for (i = 0; i < ntrk; i++) {
        int c = (int)t[i].channel - 1;
        if (c >= 0 && c < 16) s->program[c] = t[i].program;
    }
}
static void fill_scene(OrchScene *s, const char *name, uint16_t bpm,
                       const uint8_t *vol, int ncol, const OrchTrack *t, int ntrk)
{
    int i;
    orch_check(s != 0, __LINE__);
    orch_check(ncol >= 0 && ncol <= 16, __LINE__);
    memset(s, 0, sizeof *s);
    copy_name(s->name, ORCH_SCENE_NAME, name);
    s->tempo = bpm;
    s->num = 4;
    s->den = 4;
    for (i = 0; i < ncol; i++) s->volume[i] = vol[i];
    scene_progs(s, t, ntrk);
}
static void fill_mask_scene(OrchScene *s, const char *name, uint16_t mask, uint8_t level,
                            const OrchTrack *t)
{
    int i;
    orch_check(s != 0, __LINE__);
    orch_check(level <= 127u, __LINE__);
    memset(s, 0, sizeof *s);
    copy_name(s->name, ORCH_SCENE_NAME, name);
    s->tempo = 96;
    s->num = 4;
    s->den = 4;
    for (i = 0; i < 16; i++) {
        uint16_t bit = (uint16_t)(1u << i);
        s->volume[i] = (mask & bit) ? level : 0;
    }
    scene_progs(s, t, 16);
}
static void load_rows(OrchView *v, const OrchTrow *rows, int n)
{
    int i;
    orch_check(v != 0, __LINE__);
    orch_check(n > 0 && n <= 16, __LINE__);
    v->track_count = (uint8_t)n;
    for (i = 0; i < n; i++) put_row(&v->trk[i], &rows[i]);
}
static void load_rock(OrchView *v)
{
    static const char *const nm[8] = {
        "Count", "Verse", "Chorus", "Solo", "Bridge", "Break", "Outro", "End"
    };
    int i;
    orch_check(v != 0, __LINE__);
    orch_check(v->track_count <= 64u, __LINE__);
    load_rows(v, ROCK_ROW, 8);
    for (i = 0; i < 16; i++) {
        int src = (i < 8) ? i : 7;
        const char *name = (i < 8) ? nm[i] : DASH;
        fill_scene(&v->scn[i], name, 128, ROCK_VOL[src], 8, v->trk, 8);
    }
}
static void load_classic(OrchView *v)
{
    static const char *const nm[6] = {
        "Aria", "Verse", "Chorus", "Cadenza", "Recit", "End"
    };
    int i;
    orch_check(v != 0, __LINE__);
    orch_check(v->track_count <= 64u, __LINE__);
    load_rows(v, CLASSIC_ROW, 6);
    for (i = 0; i < 16; i++) {
        int src = (i < 6) ? i : 5;
        const char *name = (i < 6) ? nm[i] : DASH;
        fill_scene(&v->scn[i], name, 84, CLASSIC_VOL[src], 6, v->trk, 6);
    }
}
static void load_orch(OrchView *v)
{
    static const char *const nm[8] = {
        "Tutti", "Strings", "Winds", "Brass", "Solo", "Piano", "Silence", "Bow"
    };
    int i;
    orch_check(v != 0, __LINE__);
    orch_check(v->track_count <= 64u, __LINE__);
    load_rows(v, ORCH_ROW, 16);
    for (i = 0; i < 16; i++) {
        int src = (i < 8) ? i : 6;
        const char *name = (i < 8) ? nm[i] : DASH;
        fill_mask_scene(&v->scn[i], name, ORCH_MASK[src], ORCH_LV[src], v->trk);
    }
}
static void blank_music(OrchView *v)
{
    orch_check(v != 0, __LINE__);
    orch_check(v->track_count <= 64u, __LINE__);
    memset(v->trk, 0, sizeof v->trk);
    memset(v->scn, 0, sizeof v->scn);
    memset(v->ev, 0, sizeof v->ev);
    memset(v->ev_live, 0, sizeof v->ev_live);
    v->track_count = 0;
    v->event_count = 0;
}
static void family_fields(OrchView *v, int fam)
{
    static const uint8_t hall[3][4] = {
        {40, 40, 30, 25},
        {75, 35, 55, 45},
        {85, 40, 60, 40}
    };
    static const uint16_t bpm[3] = { 128, 84, 96 };
    int i, n;
    orch_check(v != 0, __LINE__);
    orch_check(fam >= 0 && fam <= 2, __LINE__);
    v->family = (uint8_t)fam;
    v->tempo = bpm[fam];
    v->num = 4;
    v->den = 4;
    v->click = 0;
    v->active_scene = 0;
    v->scene_count = 16;
    v->master_vol = 100;
    v->master_lim = ORCH_INS_LIMITER;
    v->room = hall[fam][0];
    v->damping = hall[fam][1];
    v->feedback = hall[fam][2];
    v->wet = hall[fam][3];
    v->confirm_family = -1;
    v->selected = 0;
    n = (int)v->track_count;
    for (i = 0; i < n && i < 64; i++) gst_resolve_track(&v->trk[i]);
}
static int apply_fam(int fam)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(fam >= 0 && fam <= 2, __LINE__);
    if (!v) return 0;
    gst_seq_begin();
    blank_music(v);
    if (fam == ORCH_FAM_ROCK) load_rock(v);
    else if (fam == ORCH_FAM_CLASSIC) load_classic(v);
    else load_orch(v);
    family_fields(v, fam);
    gst_seq_end();
    wa_set_reverb(1, (float)v->room / 100.0f, (float)v->damping / 100.0f,
                  (float)v->feedback / 100.0f, (float)v->wet / 100.0f);
    lc_insert_reset();
    gst_rt_return_zero();
    gst_insert_touch();
    orch_status_ok();
    return 1;
}
int orch_cmd_family(int fam)
{
    OrchView *v = gst_view();
    int empty;
    orch_check(v != 0, __LINE__);
    orch_check(fam < 8, __LINE__);
    if (!v || fam < 0 || fam > ORCH_FAM_ORCHESTRA) return 0;
    empty = (v->track_count == 0 && v->event_count == 0);
    if (!empty && v->confirm_family != (int8_t)fam) {
        gst_seq_begin();
        v->confirm_family = (int8_t)fam;
        gst_seq_end();
        orch_status_ok();
        return 1;
    }
    if (v->state == ORCH_GST_PLAYING) return refuse_tempo();
    return apply_fam(fam);
}
static void push_programs(OrchView *v, const OrchScene *sc)
{
    int i;
    orch_check(v != 0, __LINE__);
    orch_check(sc != 0, __LINE__);
    for (i = 0; i < 64 && i < (int)v->track_count; i++) {
        int c = (int)v->trk[i].channel - 1;
        uint8_t pr;
        if (c < 0 || c > 15) continue;
        pr = sc->program[c];
        if (pr == 255u) continue;
        if (!gst_program_known(pr)) {
            orch_count_sat(&v->refuse_program);
            continue;
        }
        v->trk[i].program = pr;
        gst_resolve_track(&v->trk[i]);
    }
}
static void push_volumes(OrchView *v, const OrchScene *sc)
{
    int i;
    orch_check(v != 0, __LINE__);
    orch_check(sc != 0, __LINE__);
    for (i = 0; i < 64 && i < (int)v->track_count; i++) {
        int c = (int)v->trk[i].channel - 1;
        if (c < 0 || c > 15) continue;
        v->trk[i].volume = sc->volume[c];
    }
}
int orch_cmd_scene(int idx)
{
    OrchView *v = gst_view();
    OrchScene *sc;
    orch_check(v != 0, __LINE__);
    orch_check(idx < 32, __LINE__);
    if (!v || idx < 0 || idx > 15) return 0;
    sc = &v->scn[idx];
    gst_seq_begin();
    v->active_scene = (uint8_t)idx;
    if (v->state != ORCH_GST_PLAYING) {
        if (bpm_ok(sc->tempo)) v->tempo = sc->tempo;
        if (sc->num >= 1u && sc->num <= 12u) v->num = sc->num;
        if (den_ok(sc->den)) v->den = sc->den;
    }
    push_volumes(v, sc);
    push_programs(v, sc);
    gst_seq_end();
    if (v->state != ORCH_GST_PLAYING) gst_insert_touch();
    orch_status_ok();
    return 1;
}
int orch_cmd_new(void)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (!v || v->state == ORCH_GST_NULL) return 0;
    if (v->state != ORCH_GST_PAUSED) {
        if (!gst_rt_set_state(ORCH_GST_PAUSED)) return 0;
    }
    gst_seq_begin();
    v->track_count = 0;
    v->event_count = 0;
    v->confirm_family = -1;
    gst_seq_end();
    return orch_cmd_family(ORCH_FAM_ROCK);
}
int orch_cmd_tempo(int bpm)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(bpm > -1000 && bpm < 1000, __LINE__);
    if (!v || !bpm_ok((unsigned)bpm)) return 0;
    if (v->state == ORCH_GST_PLAYING) return refuse_tempo();
    gst_seq_begin();
    v->tempo = (uint16_t)bpm;
    gst_seq_end();
    gst_insert_touch();
    orch_status_ok();
    return 1;
}
int orch_cmd_num(int num)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(num > -100 && num < 100, __LINE__);
    if (!v || num < 1 || num > 12) return 0;
    if (v->state == ORCH_GST_PLAYING) return refuse_tempo();
    gst_seq_begin();
    v->num = (uint8_t)num;
    gst_seq_end();
    gst_insert_touch();
    orch_status_ok();
    return 1;
}
int orch_cmd_den(int den)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(den > -100 && den < 100, __LINE__);
    if (!v || den < 0 || !den_ok((unsigned)den)) return 0;
    if (v->state == ORCH_GST_PLAYING) return refuse_tempo();
    gst_seq_begin();
    v->den = (uint8_t)den;
    gst_seq_end();
    gst_insert_touch();
    orch_status_ok();
    return 1;
}
int orch_cmd_click(void)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(v->click <= 1u, __LINE__);
    if (!v) return 0;
    gst_seq_begin();
    v->click = (uint8_t)(v->click ? 0 : 1);
    gst_seq_end();
    return 1;
}
static OrchTrack *sel_track(void)
{
    OrchView *v = gst_view();
    int s;
    orch_check(v != 0, __LINE__);
    orch_check(v->track_count <= 64u, __LINE__);
    if (!v) return 0;
    s = (int)v->selected;
    if (s < 0 || s >= (int)v->track_count) return 0;
    return &v->trk[s];
}
static int refuse_track(void)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(v->track_count <= 64u, __LINE__);
    orch_status_hold("track");
    return 0;
}
int orch_cmd_add_track(void)
{
    OrchView *v = gst_view();
    OrchTrack *t;
    int n;
    orch_check(v != 0, __LINE__);
    orch_check(v->track_count <= 64u, __LINE__);
    if (!v) return 0;
    n = (int)v->track_count;
    if (n >= 64) return refuse_track();
    gst_seq_begin();
    t = &v->trk[n];
    memset(t, 0, sizeof *t);
    copy_name(t->name, ORCH_NAME, "Track");
    t->channel = (uint8_t)((n % 16) + 1);
    t->volume = 100;
    t->pan = 64;
    t->program = 0;
    t->key_hi = 127;
    v->track_count = (uint8_t)(n + 1);
    v->selected = (int8_t)n;
    gst_resolve_track(t);
    gst_seq_end();
    return 1;
}
static void events_drop_track(OrchView *v, int idx)
{
    int i, guard;
    orch_check(v != 0, __LINE__);
    orch_check(idx >= 0 && idx < 64, __LINE__);
    for (guard = 0, i = 0; guard < 4096 && i < (int)v->event_count; guard++) {
        if (v->ev[i].track == (uint8_t)idx) {
            int k;
            for (k = i; k < (int)v->event_count - 1 && k < 4095; k++) {
                v->ev[k] = v->ev[k + 1];
                v->ev_live[k] = v->ev_live[k + 1];
            }
            memset(&v->ev[v->event_count - 1u], 0, sizeof v->ev[0]);
            v->ev_live[v->event_count - 1u] = 0;
            v->event_count -= 1u;
            continue;
        }
        if (v->ev[i].track > (uint8_t)idx) v->ev[i].track = (uint8_t)(v->ev[i].track - 1u);
        i++;
    }
}
int orch_cmd_del_track(void)
{
    OrchView *v = gst_view();
    int idx, n, i;
    orch_check(v != 0, __LINE__);
    orch_check(v->track_count <= 64u, __LINE__);
    if (!v) return 0;
    if (v->state == ORCH_GST_PLAYING) return refuse_track();
    n = (int)v->track_count;
    idx = (int)v->selected;
    if (n <= 1 || idx < 0 || idx >= n) return refuse_track();
    gst_seq_begin();
    events_drop_track(v, idx);
    for (i = idx; i < n - 1 && i < 63; i++) v->trk[i] = v->trk[i + 1];
    memset(&v->trk[n - 1], 0, sizeof v->trk[0]);
    v->track_count = (uint8_t)(n - 1);
    if ((int)v->selected >= (int)v->track_count) v->selected = (int8_t)(v->track_count - 1);
    gst_seq_end();
    return 1;
}
int orch_cmd_split(void)
{
    OrchTrack *t = sel_track();
    int z = 0;
    orch_check(t == 0 || t->key_hi <= 127u, __LINE__);
    orch_check(gst_view()->track_count <= 64u, __LINE__);
    if (!t) return 0;
    if (t->key_lo == 0 && t->key_hi == 59) z = 1;
    else if (t->key_lo == 60 && t->key_hi == 127) z = 2;
    z = (z + 1) % 3;
    gst_seq_begin();
    if (z == 1) {
        t->key_lo = 0;
        t->key_hi = 59;
    } else if (z == 2) {
        t->key_lo = 60;
        t->key_hi = 127;
    } else {
        t->key_lo = 0;
        t->key_hi = 127;
    }
    gst_seq_end();
    return 1;
}
int orch_cmd_velocity(void)
{
    OrchTrack *t = sel_track();
    orch_check(t == 0 || t->vel_curve < 16u, __LINE__);
    orch_check(gst_view()->track_count <= 64u, __LINE__);
    if (!t) return 0;
    gst_seq_begin();
    t->vel_curve = (uint8_t)((t->vel_curve >= 2u) ? 0u : t->vel_curve + 1u);
    gst_seq_end();
    return 1;
}
int orch_cmd_channel(void)
{
    OrchTrack *t = sel_track();
    uint8_t ch, port;
    orch_check(t == 0 || t->channel <= 16u, __LINE__);
    orch_check(gst_view()->track_count <= 64u, __LINE__);
    if (!t) return 0;
    ch = t->channel;
    port = t->in_port;
    if (ch >= 16u) {
        ch = 1;
        port = (uint8_t)((port + 1u) % 32u);
    } else {
        ch = (uint8_t)(ch + 1u);
    }
    gst_seq_begin();
    t->channel = ch;
    t->in_port = port;
    gst_seq_end();
    return 1;
}
int orch_cmd_octave(void)
{
    OrchView *v = gst_view();
    int i, n;
    orch_check(v != 0, __LINE__);
    orch_check(v->track_count <= 64u, __LINE__);
    if (!v) return 0;
    n = (int)v->track_count;
    gst_seq_begin();
    for (i = 0; i < n && i < 64; i++) {
        int tr = (int)v->trk[i].transpose + 12;
        if (tr > 36) tr = 36;
        if (tr < -36) tr = -36;
        v->trk[i].transpose = (int8_t)tr;
    }
    gst_seq_end();
    return 1;
}
int orch_cmd_send_programs(void)
{
    OrchView *v = gst_view();
    int idx;
    orch_check(v != 0, __LINE__);
    orch_check(v->scene_count <= 16u, __LINE__);
    if (!v) return 0;
    idx = (int)v->active_scene;
    if (idx < 0 || idx > 15) return 0;
    gst_seq_begin();
    push_programs(v, &v->scn[idx]);
    gst_seq_end();
    return 1;
}
void orch_cmd_aux_show(void)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    orch_status_set("aux 1");
}
static void undo_push(int kind, int track, int index, const void *before, int n, int mode)
{
    UndoSlot *u;
    const uint8_t *src;
    int c;
    orch_check(kind >= ORCH_UNDO_EVENT && kind <= ORCH_UNDO_INSERT, __LINE__);
    orch_check(n >= 0 && n <= 48, __LINE__);
    u = &s_undo[s_undo_at];
    memset(u, 0, sizeof *u);
    u->kind = (uint8_t)kind;
    u->track = (uint8_t)track;
    u->index = (uint16_t)index;
    u->n = (uint8_t)n;
    u->mode = (uint8_t)mode;
    src = (const uint8_t *)before;
    if (src) {
        for (c = 0; c < n && c < 48; c++) u->before[c] = src[c];
    }
    s_undo_at = (uint8_t)((s_undo_at + 1u) % UNDO_CAP);
    if (s_undo_len < UNDO_CAP) s_undo_len++;
}
int orch_undo_push_event(int kind, int track, int index, const void *before, int n)
{
    orch_check(kind < 16, __LINE__);
    orch_check(n < 64, __LINE__);
    if (kind < ORCH_UNDO_EVENT || kind > ORCH_UNDO_INSERT) return 0;
    if (n < 0 || n > 48 || !before) return 0;
    undo_push(kind, track, index, before, n, UNDO_OVER);
    return 1;
}
static void undo_name(OrchView *v, const UndoSlot *u)
{
    int k;
    OrchTrack *t;
    orch_check(v != 0, __LINE__);
    orch_check(u != 0, __LINE__);
    if (u->track >= v->track_count) return;
    t = &v->trk[u->track];
    for (k = 0; k < ORCH_NAME; k++) t->name[k] = (char)u->before[k];
}
static void undo_u8(OrchView *v, const UndoSlot *u)
{
    OrchTrack *t;
    uint8_t val;
    orch_check(v != 0, __LINE__);
    orch_check(u != 0, __LINE__);
    if (u->track >= v->track_count) return;
    t = &v->trk[u->track];
    val = u->before[0];
    if (u->index == ORCH_F_VOL && val <= 127u) t->volume = val;
    else if (u->index == ORCH_F_PAN && val <= 127u) t->pan = val;
    else if (u->index == ORCH_F_MUTE && val <= 1u) t->muted = val;
    else if (u->index == ORCH_F_SOLO && val <= 1u) t->solo = val;
    else if (u->index == ORCH_F_SEND && val <= 127u) t->send = val;
    else if (u->index == ORCH_F_PROGRAM && gst_program_known(val)) {
        t->program = val;
        gst_resolve_track(t);
    }
}
static void undo_insert(OrchView *v, const UndoSlot *u)
{
    OrchTrack *t;
    int k;
    orch_check(v != 0, __LINE__);
    orch_check(u->n >= 18u, __LINE__);
    if (u->track >= v->track_count) return;
    t = &v->trk[u->track];
    t->ins0 = u->before[0];
    t->ins1 = u->before[1];
    for (k = 0; k < 4; k++) {
        t->p0[k] = (int16_t)rd16(u->before, (uint32_t)(2 + k * 2));
        t->p1[k] = (int16_t)rd16(u->before, (uint32_t)(10 + k * 2));
    }
    gst_insert_touch();
}
static int undo_event(OrchView *v, const UndoSlot *u)
{
    uint32_t idx;
    int i;
    orch_check(v != 0, __LINE__);
    orch_check(u != 0, __LINE__);
    if (v->state == ORCH_GST_PLAYING) return 0;
    idx = u->index;
    if (u->mode == UNDO_DROP_EV) {
        if (v->event_count == 0 || idx + 1u != v->event_count) return 0;
        memset(&v->ev[idx], 0, sizeof v->ev[0]);
        v->ev_live[idx] = 0;
        v->event_count -= 1u;
        return 1;
    }
    if (u->mode == UNDO_INSERT_EV) {
        if (v->event_count >= ORCH_MAX_EVENTS || idx > v->event_count) return 0;
        for (i = (int)v->event_count; i > (int)idx && i > 0; i--) {
            v->ev[i] = v->ev[i - 1];
            v->ev_live[i] = v->ev_live[i - 1];
        }
        take_event(&v->ev[idx], u->before);
        v->ev_live[idx] = 0;
        v->event_count += 1u;
        return 1;
    }
    if (idx >= v->event_count) return 0;
    take_event(&v->ev[idx], u->before);
    return 1;
}
int orch_undo_apply(void)
{
    OrchView *v = gst_view();
    UndoSlot *u;
    int at, ok = 1;
    orch_check(v != 0, __LINE__);
    orch_check(s_undo_len <= UNDO_CAP, __LINE__);
    if (!v || s_undo_len == 0) return 0;
    at = (int)((s_undo_at + UNDO_CAP - 1u) % UNDO_CAP);
    u = &s_undo[at];
    if (u->kind == ORCH_UNDO_EVENT && v->state == ORCH_GST_PLAYING) return 0;
    s_in_undo = 1;
    gst_seq_begin();
    if (u->kind == ORCH_UNDO_EVENT) ok = undo_event(v, u);
    else if (u->kind == ORCH_UNDO_NAME) undo_name(v, u);
    else if (u->kind == ORCH_UNDO_TRACK_U8) undo_u8(v, u);
    else if (u->kind == ORCH_UNDO_INSERT) undo_insert(v, u);
    else ok = 0;
    gst_seq_end();
    s_in_undo = 0;
    if (!ok) return 0;
    s_undo_at = (uint8_t)at;
    s_undo_len--;
    return 1;
}
static uint8_t *field_at(OrchTrack *t, int field)
{
    orch_check(t != 0, __LINE__);
    orch_check(field >= 0 && field <= ORCH_F_PROGRAM, __LINE__);
    if (field == ORCH_F_VOL) return &t->volume;
    if (field == ORCH_F_PAN) return &t->pan;
    if (field == ORCH_F_MUTE) return &t->muted;
    if (field == ORCH_F_SOLO) return &t->solo;
    if (field == ORCH_F_SEND) return &t->send;
    if (field == ORCH_F_PROGRAM) return &t->program;
    return 0;
}
static int field_ok(int field, int value)
{
    orch_check(field < 16, __LINE__);
    orch_check(value > -200 && value < 400, __LINE__);
    if (field == ORCH_F_MUTE || field == ORCH_F_SOLO) return value == 0 || value == 1;
    if (field == ORCH_F_VOL || field == ORCH_F_PAN || field == ORCH_F_SEND) {
        return value >= 0 && value <= 127;
    }
    if (field == ORCH_F_PROGRAM) return value >= 0 && value <= 127;
    return 0;
}
int orch_track_set_u8(int field, int value)
{
    OrchView *v = gst_view();
    OrchTrack *t = sel_track();
    uint8_t *p;
    uint8_t old;
    orch_check(v != 0, __LINE__);
    orch_check(field < 16, __LINE__);
    if (!v || !t || !field_ok(field, value)) return 0;
    if (field == ORCH_F_PROGRAM && !gst_program_known((uint8_t)value)) {
        orch_count_sat(&v->refuse_program);
        orch_status_hold("program");
        return 0;
    }
    p = field_at(t, field);
    if (!p) return 0;
    old = *p;
    if (!s_in_undo) undo_push(ORCH_UNDO_TRACK_U8, (int)v->selected, field, &old, 1, UNDO_OVER);
    gst_seq_begin();
    *p = (uint8_t)value;
    if (field == ORCH_F_PROGRAM) gst_resolve_track(t);
    gst_seq_end();
    return 1;
}
static int type_count(uint8_t id)
{
    OrchView *v = gst_view();
    int i, n = 0;
    orch_check(v != 0, __LINE__);
    orch_check(id <= ORCH_INS_LIMITER, __LINE__);
    if (!v || id == 0) return 0;
    for (i = 0; i < 64 && i < (int)v->track_count; i++) {
        if (v->trk[i].ins0 == id) n++;
        if (v->trk[i].ins1 == id) n++;
    }
    return n;
}
static void clamp_one(int16_t *p, int idx, int lo, int hi)
{
    int v;
    orch_check(p != 0, __LINE__);
    orch_check(idx >= 0 && idx < 4, __LINE__);
    v = (int)p[idx];
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    p[idx] = (int16_t)v;
}
static void clamp_row(uint8_t id, int16_t *p)
{
    orch_check(p != 0, __LINE__);
    orch_check(id <= ORCH_INS_LIMITER, __LINE__);
    if (id == ORCH_INS_EQ) {
        clamp_one(p, 0, 0, 3);
        clamp_one(p, 1, 20, 20000);
        clamp_one(p, 2, 10, 1000);
        clamp_one(p, 3, -24, 24);
        return;
    }
    if (id == ORCH_INS_SAT) {
        clamp_one(p, 0, 0, 100);
        clamp_one(p, 1, 0, 0);
        clamp_one(p, 2, 0, 0);
        clamp_one(p, 3, 0, 0);
        return;
    }
    if (id == ORCH_INS_DELAY) {
        clamp_one(p, 0, 1, 16);
        clamp_one(p, 1, 0, 90);
        clamp_one(p, 2, 0, 100);
        clamp_one(p, 3, 0, 0);
        return;
    }
    if (id == ORCH_INS_REVERB) {
        clamp_one(p, 0, 0, 127);
        clamp_one(p, 1, 0, 100);
        clamp_one(p, 2, 0, 100);
        clamp_one(p, 3, 0, 100);
        return;
    }
    if (id == ORCH_INS_CHORUS) {
        clamp_one(p, 0, 1, 40);
        clamp_one(p, 1, 0, 10);
        clamp_one(p, 2, 1, 50);
        clamp_one(p, 3, 0, 100);
        return;
    }
    if (id == ORCH_INS_GATE) {
        clamp_one(p, 0, 1, 100);
        clamp_one(p, 1, 1, 500);
        clamp_one(p, 2, 0, 500);
        clamp_one(p, 3, 0, 0);
        return;
    }
    if (id == ORCH_INS_TREMOLO) {
        clamp_one(p, 0, 0, 100);
        clamp_one(p, 1, 1, 120);
        clamp_one(p, 2, 0, 0);
        clamp_one(p, 3, 0, 0);
        return;
    }
    clamp_one(p, 0, 0, 0);
    clamp_one(p, 1, 0, 0);
    clamp_one(p, 2, 0, 0);
    clamp_one(p, 3, 0, 0);
}
static void snap_ins(const OrchTrack *t, uint8_t *d)
{
    int k;
    orch_check(t != 0, __LINE__);
    orch_check(d != 0, __LINE__);
    d[0] = t->ins0;
    d[1] = t->ins1;
    for (k = 0; k < 4; k++) {
        int a = (int)t->p0[k];
        int b = (int)t->p1[k];
        d[2 + k * 2] = (uint8_t)a;
        d[3 + k * 2] = (uint8_t)(a >> 8);
        d[10 + k * 2] = (uint8_t)b;
        d[11 + k * 2] = (uint8_t)(b >> 8);
    }
}
static int16_t *slot_p(OrchTrack *t, int slot)
{
    orch_check(t != 0, __LINE__);
    orch_check(slot == 0 || slot == 1, __LINE__);
    return (slot == 0) ? t->p0 : t->p1;
}
int orch_ins_cycle(int slot)
{
    OrchView *v = gst_view();
    OrchTrack *t = sel_track();
    uint8_t *id;
    uint8_t nxt, raw[18];
    int16_t *row;
    orch_check(v != 0, __LINE__);
    orch_check(slot < 4, __LINE__);
    if (!v || !t || (slot != 0 && slot != 1)) return 0;
    id = (slot == 0) ? &t->ins0 : &t->ins1;
    nxt = (uint8_t)((*id + 1u) % 9u);
    if (nxt != 0 && type_count(nxt) >= 8) {
        nxt = ORCH_INS_BYPASS;
        orch_count_sat(&v->refuse_insert);
        orch_status_hold("insert");
    }
    if (!s_in_undo) {
        snap_ins(t, raw);
        undo_push(ORCH_UNDO_INSERT, (int)v->selected, slot, raw, 18, UNDO_OVER);
    }
    gst_seq_begin();
    *id = nxt;
    row = slot_p(t, slot);
    clamp_row(nxt, row);
    gst_seq_end();
    gst_insert_touch();
    return 1;
}
int orch_ins_param(int slot, int p, int delta)
{
    OrchView *v = gst_view();
    OrchTrack *t = sel_track();
    int16_t *row;
    uint8_t id, raw[18];
    int vnow;
    orch_check(v != 0, __LINE__);
    orch_check(p < 8, __LINE__);
    if (!v || !t || (slot != 0 && slot != 1) || p < 0 || p > 3) return 0;
    id = (slot == 0) ? t->ins0 : t->ins1;
    if (id == ORCH_INS_BYPASS || id == ORCH_INS_LIMITER) return 0;
    row = slot_p(t, slot);
    if (!s_in_undo) {
        snap_ins(t, raw);
        undo_push(ORCH_UNDO_INSERT, (int)v->selected, slot, raw, 18, UNDO_OVER);
    }
    vnow = (int)row[p] + delta;
    if (vnow > 32767) vnow = 32767;
    if (vnow < -32768) vnow = -32768;
    gst_seq_begin();
    row[p] = (int16_t)vnow;
    clamp_row(id, row);
    gst_seq_end();
    gst_insert_touch();
    return 1;
}
int orch_master_cycle(void)
{
    OrchView *v = gst_view();
    uint8_t nxt;
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (!v) return 0;
    if (v->master_lim != ORCH_INS_BYPASS && v->master_lim != ORCH_INS_LIMITER) {
        orch_count_sat(&v->refuse_insert);
        orch_status_hold("insert");
        return 0;
    }
    nxt = (v->master_lim == ORCH_INS_LIMITER) ? ORCH_INS_BYPASS : ORCH_INS_LIMITER;
    gst_seq_begin();
    v->master_lim = nxt;
    gst_seq_end();
    gst_insert_touch();
    return 1;
}
int orch_master_vol(int vol)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(vol > -200 && vol < 400, __LINE__);
    if (!v || vol < 0 || vol > 127) return 0;
    gst_seq_begin();
    v->master_vol = (uint8_t)vol;
    gst_seq_end();
    return 1;
}
static void ev_bytes(uint8_t *d, const OrchEvent *e)
{
    orch_check(d != 0, __LINE__);
    orch_check(e != 0, __LINE__);
    memset(d, 0, 16);
    d[0] = e->track;
    d[1] = e->key;
    d[2] = e->vel;
    d[4] = (uint8_t)e->start_tick;
    d[5] = (uint8_t)(e->start_tick >> 8);
    d[6] = (uint8_t)(e->start_tick >> 16);
    d[7] = (uint8_t)(e->start_tick >> 24);
    d[8] = (uint8_t)e->dur_tick;
    d[9] = (uint8_t)(e->dur_tick >> 8);
    d[10] = (uint8_t)(e->dur_tick >> 16);
    d[11] = (uint8_t)(e->dur_tick >> 24);
}
int orch_event_put(int track, uint8_t key, uint8_t vel, uint32_t start, uint32_t dur)
{
    OrchView *v = gst_view();
    OrchEvent *e;
    uint32_t q;
    uint8_t raw[16];
    orch_check(v != 0, __LINE__);
    orch_check(track < 128, __LINE__);
    if (!v) return 0;
    if (v->state == ORCH_GST_PLAYING) {
        orch_status_hold("note");
        return 0;
    }
    if (vel == 0) return 0;
    if (track < 0 || track >= (int)v->track_count || key > 127u) return 0;
    if (v->event_count >= ORCH_MAX_EVENTS) {
        orch_count_sat(&v->refuse_note_cap);
        orch_status_hold("note");
        return 0;
    }
    q = gst_snap_q(v->snap, v->num, v->den);
    if (q > 0u && dur < q) dur = q;
    if (!s_in_undo) {
        memset(raw, 0, sizeof raw);
        undo_push(ORCH_UNDO_EVENT, track, (int)v->event_count, raw, 16, UNDO_DROP_EV);
    }
    gst_seq_begin();
    e = &v->ev[v->event_count];
    memset(e, 0, sizeof *e);
    e->track = (uint8_t)track;
    e->key = key;
    e->vel = vel;
    e->start_tick = start;
    e->dur_tick = dur;
    v->ev_live[v->event_count] = 0;
    v->event_count += 1u;
    gst_seq_end();
    return 1;
}
int orch_event_del(int index)
{
    OrchView *v = gst_view();
    uint8_t raw[16];
    int i;
    orch_check(v != 0, __LINE__);
    orch_check(index < 5000, __LINE__);
    if (!v) return 0;
    if (v->state == ORCH_GST_PLAYING) {
        orch_status_hold("note");
        return 0;
    }
    if (index < 0 || (uint32_t)index >= v->event_count) return 0;
    if (!s_in_undo) {
        ev_bytes(raw, &v->ev[index]);
        undo_push(ORCH_UNDO_EVENT, v->ev[index].track, index, raw, 16, UNDO_INSERT_EV);
    }
    gst_seq_begin();
    for (i = index; i < (int)v->event_count - 1 && i < 4095; i++) {
        v->ev[i] = v->ev[i + 1];
        v->ev_live[i] = v->ev_live[i + 1];
    }
    memset(&v->ev[v->event_count - 1u], 0, sizeof v->ev[0]);
    v->ev_live[v->event_count - 1u] = 0;
    v->event_count -= 1u;
    gst_seq_end();
    return 1;
}
int orch_cmd_play(void)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    return gst_rt_set_state(ORCH_GST_PLAYING);
}
int orch_cmd_stop(void)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (!v) return 0;
    if (v->state == ORCH_GST_PLAYING) return gst_rt_set_state(ORCH_GST_PAUSED);
    gst_rt_return_zero();
    return 1;
}
int orch_cmd_panic(void)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    gst_rt_panic();
    return 1;
}
void orch_resolve_track(OrchTrack *t)
{
    orch_check(t != 0, __LINE__);
    orch_check(gst_view()->family < 16u, __LINE__);
    gst_resolve_track(t);
}
void orch_build_curve(void)
{
    OrchView *v = gst_view();
    int i, k = 25;
    orch_check(v != 0, __LINE__);
    orch_check(v->track_count <= 64u, __LINE__);
    if (!v) return;
    for (i = 0; i < 64 && i < (int)v->track_count; i++) {
        if (v->trk[i].ins0 == ORCH_INS_SAT) k = (int)v->trk[i].p0[0];
        if (v->trk[i].ins1 == ORCH_INS_SAT) k = (int)v->trk[i].p1[0];
    }
    if (k < 0) k = 0;
    if (k > 100) k = 100;
    if ((int)v->curve_k == k) return;
    lc_curve_build(v->curve, k);
    v->curve_k = (int16_t)k;
}
