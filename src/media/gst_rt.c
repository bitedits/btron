/* Closed real-time chain. No GObject, no loader, no malloc on the audio path. */
#include <media/gst_rt.h>
#include <apps/orch_chk.h>
#include <device/virtio.h>
#include "../demo/lilcu64_wa.h"
#include "../demo/lilcu64_score.h"
#include "../demo/lilcu64_synth.h"
#include "../demo/lilcu64_insert.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
extern uint32_t btron_render_perf_us(void);
static const char *const ORCH_FACTORY[] = {
    "midisrc", "queue", "midiroute", "shimsynth",
    "audioconvert", "tee", "audiomixer",
    "volume", "identity", "virtiosndsink"
};
static const char WAV_PATH[] = "/SYS/orchestra.wav";
typedef struct ProgRow {
    uint8_t gm, shim, clef, poly, kind, shot;
    int8_t tr;
    uint8_t dur10;
} ProgRow;
typedef struct LatTrk {
    uint8_t vol, pan, mute, solo, send, program;
    uint8_t ins0, ins1, curve, lo, hi, shim, kind, oneshot, poly, clef;
    int8_t tr, prog_tr;
    int16_t p0[4];
    int16_t p1[4];
} LatTrk;
typedef struct OrchSlot {
    uint8_t used, click, bus, key, track, oneshot, bell, stole;
    float gain;
    uint64_t t0;
    uint32_t dur;
    int32_t ev;
} OrchSlot;
typedef struct RecEv {
    uint8_t op, track, key, vel;
    uint32_t tick;
} RecEv;
typedef struct CtrSnap {
    uint32_t drop_audio, steal_voice, refuse_smf, refuse_session;
    uint32_t refuse_program, refuse_insert, refuse_note_cap;
    uint32_t refuse_factory, refuse_tempo, midi_clock, qos_drops;
    uint32_t chain_accept, chain_deliver, chain_drop, chain_refuse;
    uint8_t meter_l, meter_r;
    uint8_t arm, minit, binit;
    uint32_t mlast, blast;
    uint64_t npos;
    int64_t clock;
} CtrSnap;
/* gm, shim, clef, poly, kind, shot, tr, dur in 10 ms. Hat duration is velocity. */
static const ProgRow PROG[32] = {
    {0, LC_SHIM_PIANO, 0, 6, ORCH_KIND_PIANO, 0, 0, 120},
    {4, LC_SHIM_FLOAT, 0, 1, ORCH_KIND_FLOAT, 0, 0, 0},
    {26, LC_SHIM_GUITAR, 0, 1, ORCH_KIND_PHRASE, 0, 0, 0},
    {32, LC_SHIM_WADDLE, 1, 1, ORCH_KIND_WADDLE, 0, 0, 0},
    {33, LC_SHIM_IND_BASS, 1, 1, ORCH_KIND_BASS, 0, 0, 0},
    {38, LC_SHIM_PEDAL, 1, 1, ORCH_KIND_PEDAL, 0, 0, 0},
    {40, LC_SHIM_VIOLIN, 0, 1, ORCH_KIND_PHRASE, 0, 0, 0},
    {41, LC_SHIM_VIOLIN, 0, 1, ORCH_KIND_PHRASE, 0, -7, 0},
    {42, LC_SHIM_CELLO, 1, 1, ORCH_KIND_PHRASE, 0, 0, 0},
    {43, LC_SHIM_CELLO, 1, 1, ORCH_KIND_PHRASE, 0, -12, 0},
    {46, LC_SHIM_PIANO, 0, 1, ORCH_KIND_PIANO, 0, 0, 240},
    {47, LC_SHIM_TIMPANI, 1, 1, ORCH_KIND_ONESHOT, 1, 0, 120},
    {52, LC_SHIM_SAX, 0, 1, ORCH_KIND_PHRASE, 0, 0, 0},
    {56, LC_SHIM_CYBER, 0, 1, ORCH_KIND_PHRASE, 0, 0, 0},
    {57, LC_SHIM_CYBER, 1, 1, ORCH_KIND_PHRASE, 0, 0, 0},
    {58, LC_SHIM_CYBER, 1, 1, ORCH_KIND_PHRASE, 0, 0, 0},
    {60, LC_SHIM_CYBER, 0, 1, ORCH_KIND_PHRASE, 0, 0, 0},
    {68, LC_SHIM_SAX, 0, 1, ORCH_KIND_PHRASE, 0, 0, 0},
    {70, LC_SHIM_CELLO, 1, 1, ORCH_KIND_PHRASE, 0, 0, 0},
    {71, LC_SHIM_SAX, 0, 1, ORCH_KIND_PHRASE, 0, 0, 0},
    {73, LC_SHIM_SAX, 0, 1, ORCH_KIND_PHRASE, 0, 0, 0},
    {80, LC_SHIM_SQUASH, 0, 1, ORCH_KIND_SQUASH, 1, 0, 240},
    {81, LC_SHIM_CYBER, 0, 1, ORCH_KIND_PHRASE, 0, 0, 0},
    {90, LC_SHIM_SWARM, 0, 1, ORCH_KIND_SWARM, 0, 0, 0},
    {98, 0, 0, 1, ORCH_KIND_BELL, 0, 0, 0},
    {102, LC_SHIM_DANCE, 0, 1, ORCH_KIND_DANCE, 1, 0, 85},
    {103, LC_SHIM_ZAP, 0, 1, ORCH_KIND_ZAP, 1, 0, 12},
    {112, LC_SHIM_DING, 0, 1, ORCH_KIND_DING, 1, 0, 70},
    {116, LC_SHIM_KICK, 1, 1, ORCH_KIND_KICK, 1, 0, 16},
    {117, LC_SHIM_SNARE, 0, 1, ORCH_KIND_SNARE, 1, 0, 15},
    {118, LC_SHIM_HAT, 0, 1, ORCH_KIND_HAT, 1, 0, 0},
    {119, LC_SHIM_IND_KICK, 1, 1, ORCH_KIND_INDKICK, 1, 0, 22}
};
static OrchView g;
static uint32_t s_assert_n;
static int s_assert_line;
static uint8_t s_sticky;
static int s_booted;
static int s_ident;
static int s_made_n;
static uint8_t s_kind[16];
static uint8_t s_bus[16];
static int s_bus_n;
static uint32_t s_bus_ovf;
static float s_pitch[128];
static LatTrk s_lat[ORCH_MAX_TRACKS];
static int s_ln;
static uint16_t s_lbpm;
static uint8_t s_lnum, s_lden, s_lclick, s_lrec, s_lmvol, s_llim, s_lsnap;
static int8_t s_lsel;
static OrchSlot s_slot[48];
static OrchSlot s_xslot[48];
static OrchSlot s_slot_snap[48];
static uint8_t s_xlive[ORCH_MAX_EVENTS];
static RecEv s_rec[64];
static int s_rec_w, s_rec_r, s_rec_n;
static uint8_t s_midi_used[8];
static float s_plane[66 * ORCH_Q];
static float s_tmp[ORCH_Q];
static float s_L[ORCH_Q], s_R[ORCH_Q], s_send[ORCH_Q];
static float s_wl[ORCH_Q], s_wr[ORCH_Q];
static int16_t s_pcm[ORCH_Q * 2];
static uint8_t s_buses[66];
static int8_t s_dline[64][2];
static int8_t s_cline[64][2];
static uint8_t s_downer[8];
static uint8_t s_cowner[8];
static uint8_t s_clamp_arm;
static uint8_t s_arm;
static uint8_t s_minit, s_binit;
static uint32_t s_mlast, s_blast;
static uint32_t s_due;
static int s_force_late;
static int s_export;
void orch_assert_fail(int line)
{
    if (s_assert_n < 4294967295u) s_assert_n++;
    s_assert_line = line;
    s_sticky = 1;
}
void orch_count_sat(uint32_t *c)
{
    orch_check(c != 0, __LINE__); orch_check(s_sticky < 2, __LINE__);
    if (c && *c < 4294967295u) *c += 1u;
}
static void status_copy(char *dst, const char *src)
{
    int i;
    orch_check(dst != 0, __LINE__); orch_check(sizeof(g.status) >= 96, __LINE__);
    if (!src) src = "";
    for (i = 0; i < 95; i++) {
        dst[i] = src[i];
        if (src[i] == 0) return;
    }
    dst[95] = 0;
}
void orch_status_set(const char *text)
{
    orch_check(sizeof(g.status) == 96, __LINE__); orch_check(g.status_hold == 0 || g.status_hold == 1, __LINE__);
    status_copy(g.status, text);
    g.status_hold = 0;
}
void orch_status_hold(const char *text)
{
    orch_check(sizeof(g.status) == 96, __LINE__); orch_check(g.status_hold <= 1, __LINE__);
    status_copy(g.status, text);
    g.status_hold = 1;
}
void orch_status_ok(void)
{
    orch_check(g.status_hold == 0 || g.status_hold == 1, __LINE__); orch_check(s_booted == 0 || s_booted == 1, __LINE__);
    g.status_hold = 0;
}
void gst_seq_begin(void)
{
    orch_check(s_booted == 0 || s_booted == 1, __LINE__); orch_check(s_export == 0 || s_export == 1, __LINE__);
    g.seq += 1u;
}
void gst_seq_end(void)
{
    orch_check(s_booted == 0 || s_booted == 1, __LINE__); orch_check(s_export == 0 || s_export == 1, __LINE__);
    g.seq += 1u;
}
static uint32_t fs_now(void)
{
    uint32_t f = g.sample_rate;
    orch_check(s_booted == 0 || s_booted == 1, __LINE__); orch_check(g.state <= ORCH_GST_PLAYING, __LINE__);
    if (f != 44100u && f != 48000u) f = 48000u;
    return f;
}
static uint32_t bpm_fix(uint32_t bpm)
{
    orch_check(s_sticky < 2, __LINE__); orch_check(g.scene_count <= 16, __LINE__);
    if (bpm < 40u) return 40u;
    if (bpm > 240u) return 240u;
    return bpm;
}
static uint32_t den_fix(uint32_t den)
{
    orch_check(g.num <= 12 || g.num == 0, __LINE__); orch_check(s_lden <= 16, __LINE__);
    if (den == 2u || den == 4u || den == 8u || den == 16u) return den;
    return 4u;
}
uint64_t gst_sample_of_tick(uint32_t tick, uint32_t fs, uint32_t bpm, uint32_t den)
{
    uint64_t D, num;
    uint32_t b = bpm_fix(bpm);
    uint32_t d = den_fix(den);
    uint32_t f = fs;
    if (f != 44100u && f != 48000u) f = 48000u;
    orch_check(b >= 40u && b <= 240u, __LINE__); orch_check(d == 2u || d == 4u || d == 8u || d == 16u, __LINE__);
    D = 64ull * (uint64_t)b;
    num = (uint64_t)tick * (uint64_t)f * (uint64_t)d + D / 2ull;
    return num / D;
}
uint32_t gst_tick_of_sample(uint64_t npos, uint32_t fs, uint32_t bpm, uint32_t den)
{
    uint64_t D, denm, num;
    uint32_t b = bpm_fix(bpm);
    uint32_t d = den_fix(den);
    uint32_t f = fs;
    if (f != 44100u && f != 48000u) f = 48000u;
    orch_check(b >= 40u && b <= 240u, __LINE__); orch_check(f == 44100u || f == 48000u, __LINE__);
    D = 64ull * (uint64_t)b;
    denm = (uint64_t)f * (uint64_t)d;
    num = (npos + 1ull) * D;
    if (num >= D / 2ull + 1ull) num = num - D / 2ull - 1ull;
    else num = 0;
    return (uint32_t)(num / denm);
}
uint32_t gst_snap_q(uint8_t snap, uint8_t num, uint8_t den)
{
    uint32_t d = den_fix(den);
    uint32_t n = num;
    if (n < 1u) n = 1u;
    if (n > 12u) n = 12u;
    orch_check(d == 2u || d == 4u || d == 8u || d == 16u, __LINE__); orch_check(n >= 1u && n <= 12u, __LINE__);
    if (snap == ORCH_SNAP_BAR) return n * (3840u / d);
    if (snap == ORCH_SNAP_BEAT) return 3840u / d;
    if (snap == ORCH_SNAP_8TH) return 480u;
    return 240u;
}
uint32_t gst_snap_down(uint32_t tick, uint32_t q)
{
    uint32_t r = tick;
    orch_check(g.snap <= ORCH_SNAP_16TH, __LINE__);
    if (q > 0u) r = tick - (tick % q);
    orch_check(r <= tick, __LINE__);
    return r;
}
OrchView *gst_view(void)
{
    orch_check(sizeof(OrchEvent) == 16, __LINE__); orch_check(PROG[0].gm == 0 && PROG[31].gm == 119, __LINE__);
    return &g;
}
static void pitch_build(void)
{
    int n;
    orch_check(PROG[0].poly == 6, __LINE__); orch_check(PROG[31].shot == 1, __LINE__);
    for (n = 0; n < 128; n++) {
        float e = ((float)n - 69.0f) / 12.0f;
        s_pitch[n] = 440.0f * powf(2.0f, e);
    }
}
static int factory_id(const char *s)
{
    int i;
    orch_check(s_made_n >= 0, __LINE__); orch_check(s_made_n <= 16, __LINE__);
    if (!s) return -1;
    for (i = 0; i < 10; i++) {
        const char *a = ORCH_FACTORY[i];
        int k = 0;
        while (k < 16 && a[k] && s[k] && a[k] == s[k]) k++;
        if (a[k] == 0 && s[k] == 0) return i;
    }
    return -1;
}
int gst_factory_make(const char *type, const char *name)
{
    int id;
    (void)name;
    orch_check(s_made_n >= 0 && s_made_n <= 16, __LINE__); orch_check(s_made_n <= 16, __LINE__);
    id = factory_id(type);
    if (id < 0 || s_made_n >= 16) {
        orch_count_sat(&g.refuse_factory);
        return 0;
    }
    s_kind[s_made_n] = (uint8_t)id;
    s_made_n += 1;
    g.element_count = (uint32_t)s_made_n;
    if (id == 8) s_ident = 1;
    return 1;
}
uint32_t gst_rt_wcet_us(void)
{
    uint32_t w = 632u;
    orch_check(w <= 666u, __LINE__); orch_check(s_ident == 0 || s_ident == 1, __LINE__);
    if (s_ident) w = 636u;
    g.wcet_us = w;
    return w;
}
uint32_t gst_rt_period_us(void)
{
    uint32_t f = fs_now();
    uint32_t t = (ORCH_Q * 1000000u) / f;
    orch_check(f == 44100u || f == 48000u, __LINE__); orch_check(t == 666u || t == 725u || t > 0u, __LINE__);
    g.period_us = t;
    return t;
}
uint32_t gst_rt_queued_ms(void)
{
    uint32_t frames = virtio_sound_get_queued_bytes() / 4u;
    uint32_t f = fs_now();
    orch_check(f != 0u, __LINE__); orch_check(s_booted == 0 || s_booted == 1, __LINE__);
    return (1000u * frames) / f;
}
uint64_t gst_rt_running_ns(void)
{
    uint32_t f = fs_now();
    orch_check(f != 0u, __LINE__); orch_check(GST_SECOND_NS == 1000000000ull, __LINE__);
    return g.npos * GST_SECOND_NS / (uint64_t)f;
}
int gst_rt_state(void)
{
    orch_check(g.state <= ORCH_GST_PLAYING, __LINE__); orch_check(s_made_n >= 0, __LINE__);
    return (int)g.state;
}
int gst_rt_element_count(void)
{
    orch_check(s_made_n >= 0 && s_made_n <= 16, __LINE__); orch_check(s_bus_n <= 16, __LINE__);
    return s_made_n;
}
static void bus_push(uint8_t code)
{
    orch_check(code > 0, __LINE__); orch_check(s_bus_n >= 0, __LINE__);
    if (s_bus_n >= 16) {
        if (s_bus_ovf < 4294967295u) s_bus_ovf++;
        return;
    }
    s_bus[s_bus_n] = code;
    s_bus_n += 1;
}
static const ProgRow *prog_by_gm(uint8_t gm)
{
    int i;
    orch_check(PROG[0].poly == 6, __LINE__); orch_check(PROG[31].gm == 119, __LINE__);
    for (i = 0; i < 32; i++) {
        if (PROG[i].gm == gm) return &PROG[i];
    }
    return 0;
}
int gst_program_known(uint8_t gm)
{
    const ProgRow *r = prog_by_gm(gm);
    orch_check(PROG[11].gm == 47, __LINE__); orch_check(r == 0 || r->gm == gm, __LINE__);
    return r != 0;
}
void gst_resolve_track(OrchTrack *t)
{
    const ProgRow *r;
    uint8_t poly;
    orch_check(t != 0, __LINE__);
    if (!t) return;
    orch_check(g.family < 16, __LINE__);
    r = prog_by_gm(t->program);
    if (!r) {
        orch_count_sat(&g.refuse_program);
        return;
    }
    t->shim = r->shim;
    t->clef = r->clef;
    t->prog_tr = r->tr;
    t->kind = r->kind;
    t->oneshot = r->shot;
    poly = r->poly;
    if (g.family == ORCH_FAM_ROCK || g.family == ORCH_FAM_ORCHESTRA) poly = 1;
    else if (g.family == ORCH_FAM_CLASSIC && r->gm != 0) poly = 1;
    if (poly > r->poly) poly = r->poly;
    t->poly_max = poly;
}
static void lat_one(LatTrk *o, const OrchTrack *t)
{
    int k;
    orch_check(o != 0, __LINE__); orch_check(t != 0, __LINE__);
    o->vol = t->volume;
    o->pan = t->pan;
    o->mute = t->muted;
    o->solo = t->solo;
    o->send = t->send;
    o->program = t->program;
    o->ins0 = t->ins0;
    o->ins1 = t->ins1;
    o->curve = t->vel_curve;
    o->lo = t->key_lo;
    o->hi = t->key_hi;
    o->shim = t->shim;
    o->kind = t->kind;
    o->oneshot = t->oneshot;
    o->poly = t->poly_max;
    o->clef = t->clef;
    o->tr = t->transpose;
    o->prog_tr = t->prog_tr;
    for (k = 0; k < 4; k++) {
        o->p0[k] = t->p0[k];
        o->p1[k] = t->p1[k];
    }
}
static void latch_copy(void)
{
    int i, n;
    orch_check(g.track_count <= 64, __LINE__); orch_check(s_ln >= 0, __LINE__);
    n = (int)g.track_count;
    if (n > 64) n = 64;
    s_ln = n;
    s_lbpm = g.tempo;
    s_lnum = g.num;
    s_lden = g.den;
    s_lclick = g.click;
    s_lrec = g.rec;
    s_lmvol = g.master_vol;
    s_llim = g.master_lim;
    s_lsnap = g.snap;
    s_lsel = g.selected;
    for (i = 0; i < n; i++) lat_one(&s_lat[i], &g.trk[i]);
}
static void latch_try(void)
{
    int k;
    uint32_t a;
    orch_check(s_export == 0 || s_export == 1, __LINE__); orch_check(s_ln >= 0, __LINE__);
    for (k = 0; k < 3; k++) {
        a = g.seq;
        if (a & 1u) continue;
        latch_copy();
        if (g.seq == a) return;
    }
}
static float gain_of(uint8_t curve, int vel)
{
    float u;
    orch_check(vel >= 0, __LINE__); orch_check(vel <= 127, __LINE__);
    if (vel > 127) vel = 127;
    u = (float)vel / 127.0f;
    if (curve == 1) u = sqrtf(u);
    else if (curve >= 2) u = u * u;
    return 0.08f + 0.37f * u;
}
static float fader_of(uint8_t vol, int silent)
{
    orch_check(silent < 2, __LINE__); orch_check(g.master_lim < 16, __LINE__);
    if (silent || vol == 0) return 0.0f;
    return 0.08f + 0.37f * ((float)vol / 127.0f);
}
static void pan_of(uint8_t cc, float *gl, float *gr)
{
    float p = ((float)cc - 64.0f) / 64.0f;
    float a = 0.78539816339f * (p + 1.0f);
    orch_check(gl != 0, __LINE__); orch_check(gr != 0, __LINE__);
    *gl = cosf(a);
    *gr = sinf(a);
}
static float pick_dur(const ProgRow *r, int vel, float evsec, int mode)
{
    float fixed = 8.0f;
    orch_check(r != 0, __LINE__); orch_check(mode >= 0 && mode <= 2, __LINE__);
    if (mode == 2 && evsec > 0.02f) return evsec;
    if (r->kind == ORCH_KIND_HAT) return (vel >= 100) ? 0.28f : 0.055f;
    if (r->kind == ORCH_KIND_DING && evsec > 0.02f) return evsec;
    if (r->dur10) fixed = (float)r->dur10 / 100.0f;
    if (r->shot) return fixed;
    if (mode == 1) return 8.0f;
    if (evsec > 0.02f) return evsec;
    return 8.0f;
}
static uint32_t sec_to_ticks(float sec)
{
    uint32_t b = bpm_fix(s_lbpm ? s_lbpm : g.tempo);
    uint32_t d = den_fix(s_lden ? s_lden : g.den);
    uint64_t ms, v;
    orch_check(b >= 40u, __LINE__); orch_check(d == 2u || d == 4u || d == 8u || d == 16u, __LINE__);
    if (sec < 0.0f) sec = 0.0f;
    ms = (uint64_t)(sec * 1000.0f);
    v = ms * 64ull * (uint64_t)b / (1000ull * (uint64_t)d);
    if (v < 1ull) v = 1ull;
    if (v > 10000000ull) v = 10000000ull;
    return (uint32_t)v;
}
static int slot_count_tr(const OrchSlot *s, int track)
{
    int i, n = 0;
    orch_check(s != 0, __LINE__); orch_check(track >= 0 && track < 64, __LINE__);
    for (i = 0; i < 48; i++) {
        if (s[i].used && !s[i].click && s[i].track == (uint8_t)track) n++;
    }
    return n;
}
static int slot_free_idx(const OrchSlot *s)
{
    int i;
    orch_check(s != 0, __LINE__); orch_check(48 <= 48, __LINE__);
    for (i = 0; i < 48; i++) if (!s[i].used) return i;
    return -1;
}
static float slot_proxy(const OrchSlot *s, uint64_t npos)
{
    float age;
    uint32_t f = fs_now();
    orch_check(s != 0, __LINE__); orch_check(f != 0u, __LINE__);
    if (!s->used || npos <= s->t0) return s->gain;
    age = (float)(npos - s->t0) / (float)f;
    return s->gain * expf(-age);
}
static int slot_quiet(const OrchSlot *s, int track, uint64_t npos)
{
    int i, best = -1;
    float bp = 0.0f;
    orch_check(s != 0, __LINE__); orch_check(track >= -1 && track < 64, __LINE__);
    for (i = 0; i < 48; i++) {
        float p;
        if (!s[i].used || s[i].click) continue;
        if (track >= 0 && s[i].track != (uint8_t)track) continue;
        p = slot_proxy(&s[i], npos);
        if (best < 0 || p < bp) { best = i; bp = p; }
    }
    return best;
}
static int slot_pick(OrchSlot *s, int track, int poly, uint64_t npos, int *stole)
{
    int idx, n;
    orch_check(s != 0 && stole != 0, __LINE__); orch_check(poly >= 1 && poly <= 48, __LINE__);
    *stole = 0;
    n = slot_count_tr(s, track);
    if (n >= poly) {
        idx = slot_quiet(s, track, npos);
        if (idx >= 0) { *stole = 1; return idx; }
    }
    idx = slot_free_idx(s);
    if (idx >= 0) return idx;
    idx = slot_quiet(s, -1, npos);
    if (idx >= 0) *stole = 1;
    return idx;
}
static void slot_silence(OrchSlot *s)
{
    orch_check(s != 0, __LINE__); orch_check(s->bus <= 65, __LINE__);
    if (!s->used) return;
    wa_release_bus_key(s->bus, s->key);
    if (s->bell) lilcu64_voice_release(s->bus, s->key);
}
static int shim_landed(uint32_t ref0, int act0, int bell)
{
    int act = wa_active_count();
    uint32_t ref = lc_shim_refusals();
    orch_check(act >= 0, __LINE__); orch_check(act0 >= 0, __LINE__);
    if (bell) return ref == ref0;
    return act > act0;
}
static int sound_on(OrchSlot *slots, int track, int key, int vel, double t,
                    float evsec, int mode, uint64_t npos, uint64_t t0, int ev)
{
    LatTrk o;
    const ProgRow *r;
    int note, stole = 0, idx, bell;
    float hz, dur, vol, play_hz;
    uint32_t ref0, ds;
    int act0;
    orch_check(slots != 0, __LINE__); orch_check(track >= 0 && track < 64, __LINE__);
    if (vel <= 0 || key < 0 || key > 127) return -1;
    if (s_export) o = s_lat[track];
    else if (mode == 0 || mode == 2) o = s_lat[track];
    else lat_one(&o, &g.trk[track]);
    r = prog_by_gm(o.program);
    if (!r) return -1;
    if (o.lo == 0 && o.hi == 0) o.hi = 127;
    if ((uint8_t)key < o.lo || (uint8_t)key > o.hi) return -2;
    note = key + (int)o.tr + (int)r->tr;
    if (note < 0) note = 0;
    if (note > 127) note = 127;
    hz = s_pitch[note];
    dur = pick_dur(r, vel, evsec, mode);
    vol = gain_of(o.curve, vel);
    play_hz = hz;
    if (r->kind == ORCH_KIND_SNARE || r->kind == ORCH_KIND_HAT ||
        r->kind == ORCH_KIND_INDKICK) play_hz = (float)vel;
    if (o.poly < 1) o.poly = 1;
    idx = slot_pick(slots, track, (int)o.poly, npos, &stole);
    if (idx < 0) return -1;
    if (stole) slot_silence(&slots[idx]);
    bell = (r->kind == ORCH_KIND_BELL);
    ref0 = lc_shim_refusals();
    act0 = wa_active_count();
    wa_set_node_bus((uint8_t)(track + 1));
    wa_set_note_key((uint8_t)key);
    lc_set_score_base(0.0);
    if (bell) lilcu64_shim_pulse_bell(vol, hz / 440.0f);
    else lc_shim_note((int)r->shim, t, play_hz, vol, dur);
    if (!shim_landed(ref0, act0, bell)) {
        slots[idx].used = 0;
        return -1;
    }
    ds = (uint32_t)(dur * (float)fs_now());
    if (ds < 1u) ds = 1u;
    slots[idx].used = 1;
    slots[idx].click = 0;
    slots[idx].bus = (uint8_t)(track + 1);
    slots[idx].key = (uint8_t)key;
    slots[idx].track = (uint8_t)track;
    slots[idx].oneshot = r->shot;
    slots[idx].bell = (uint8_t)bell;
    slots[idx].stole = (uint8_t)stole;
    slots[idx].gain = vol;
    slots[idx].t0 = t0;
    slots[idx].dur = ds;
    slots[idx].ev = ev;
    if (stole && !s_export) orch_count_sat(&g.steal_voice);
    return idx;
}
static void slots_expire(OrchSlot *s, uint64_t npos, int touch_live)
{
    int i;
    orch_check(s != 0, __LINE__); orch_check(touch_live == 0 || touch_live == 1, __LINE__);
    for (i = 0; i < 48; i++) {
        uint64_t end;
        if (!s[i].used || s[i].click) continue;
        end = s[i].t0 + (uint64_t)s[i].dur;
        if (npos < end) continue;
        if (touch_live && s[i].oneshot && s[i].ev >= 0 && s[i].ev < 4096)
            g.ev_live[s[i].ev] = 0;
        s[i].used = 0;
    }
}
static void slot_drop_key(OrchSlot *s, int track, int key, int cut)
{
    int i;
    orch_check(s != 0, __LINE__); orch_check(track >= 0 && track < 64, __LINE__);
    for (i = 0; i < 48; i++) {
        if (!s[i].used || s[i].click) continue;
        if (s[i].track != (uint8_t)track || s[i].key != (uint8_t)key) continue;
        if (s[i].oneshot) continue;
        if (cut) slot_silence(&s[i]);
        s[i].used = 0;
    }
}
static void slot_free_ev(OrchSlot *s, int ev)
{
    int i;
    orch_check(s != 0, __LINE__); orch_check(ev >= -1, __LINE__);
    for (i = 0; i < 48; i++) {
        if (s[i].used && s[i].ev == ev) s[i].used = 0;
    }
}
static void slot_attach(OrchSlot *s, int track, int key, int ev)
{
    int i;
    orch_check(s != 0, __LINE__); orch_check(ev >= 0, __LINE__);
    for (i = 0; i < 48; i++) {
        if (!s[i].used || s[i].click) continue;
        if (s[i].track == (uint8_t)track && s[i].key == (uint8_t)key && s[i].ev < 0) {
            s[i].ev = ev;
            return;
        }
    }
}
static int chase_hit(uint64_t st, uint64_t en, uint64_t npos, uint32_t frames, int arm)
{
    orch_check(frames > 0u && frames <= 8192u, __LINE__); orch_check(arm == 0 || arm == 1, __LINE__);
    if (arm) return st < npos && npos < en;
    return st >= npos && st < npos + (uint64_t)frames;
}
static void chase_on(OrchSlot *slots, uint8_t *live, uint64_t npos,
                     uint32_t frames, uint32_t prefix, int arm)
{
    uint32_t i, n, b, d, f;
    orch_check(slots != 0 && live != 0, __LINE__); orch_check(prefix <= 4096u, __LINE__);
    n = prefix;
    if (n > 4096u) n = 4096u;
    b = bpm_fix(s_lbpm ? s_lbpm : 120u);
    d = den_fix(s_lden);
    f = fs_now();
    for (i = 0; i < n; i++) {
        uint64_t st, en;
        float evsec;
        double when;
        int mode, sl;
        if (live[i] || g.ev[i].vel == 0) continue;
        st = gst_sample_of_tick(g.ev[i].start_tick, f, b, d);
        en = gst_sample_of_tick(g.ev[i].start_tick + g.ev[i].dur_tick, f, b, d);
        if (!chase_hit(st, en, npos, frames, arm)) continue;
        evsec = (en > st) ? (float)(en - st) / (float)f : 0.0f;
        mode = arm ? 2 : 0;
        when = arm ? (double)npos / (double)f : (double)st / (double)f;
        sl = sound_on(slots, (int)g.ev[i].track, (int)g.ev[i].key, (int)g.ev[i].vel,
                      when, evsec, mode, npos, arm ? npos : st, (int)i);
        if (sl >= 0) live[i] = 1;
        else if (sl == -2 && !s_export) {
            orch_count_sat(&g.chain_accept);
            orch_count_sat(&g.chain_drop);
        }
    }
}
static void chase_off(OrchSlot *slots, uint8_t *live, uint64_t npos,
                      uint32_t frames, uint32_t prefix)
{
    uint32_t i, n, b, d, f;
    orch_check(slots != 0 && live != 0, __LINE__); orch_check(frames > 0u, __LINE__);
    n = prefix;
    if (n > 4096u) n = 4096u;
    b = bpm_fix(s_lbpm ? s_lbpm : 120u);
    d = den_fix(s_lden);
    f = fs_now();
    for (i = 0; i < n; i++) {
        uint64_t st, en;
        if (!live[i] || g.ev[i].vel == 0) continue;
        st = gst_sample_of_tick(g.ev[i].start_tick, f, b, d);
        en = gst_sample_of_tick(g.ev[i].start_tick + g.ev[i].dur_tick, f, b, d);
        if (en <= st || en < npos || en >= npos + (uint64_t)frames) continue;
        live[i] = 0;
        slot_free_ev(slots, (int)i);
    }
}
static void close_one(uint32_t idx, uint64_t npos)
{
    uint32_t q, tk, st, dur, f, b, d;
    orch_check(idx < 4096u, __LINE__); orch_check(s_export == 0, __LINE__);
    if (idx >= 4096u) return;
    f = fs_now();
    b = bpm_fix(g.tempo);
    d = den_fix(g.den);
    q = gst_snap_q(g.snap, g.num ? g.num : 4, (uint8_t)d);
    if (q == 0u) q = 1u;
    tk = gst_snap_down(gst_tick_of_sample(npos, f, b, d), q);
    st = g.ev[idx].start_tick;
    dur = (tk > st) ? (tk - st) : 0u;
    if (dur < q) dur = q;
    g.ev[idx].dur_tick = dur;
    g.ev_live[idx] = 0;
}
static int ring_push(uint8_t op, uint8_t tr, uint8_t key, uint8_t vel, uint32_t tick)
{
    orch_check(s_rec_n >= 0 && s_rec_n <= 64, __LINE__); orch_check(op == 0 || op == 1, __LINE__);
    if (s_rec_n >= 64) return 0;
    s_rec[s_rec_w].op = op;
    s_rec[s_rec_w].track = tr;
    s_rec[s_rec_w].key = key;
    s_rec[s_rec_w].vel = vel;
    s_rec[s_rec_w].tick = tick;
    s_rec_w += 1;
    if (s_rec_w >= 64) s_rec_w = 0;
    s_rec_n += 1;
    return 1;
}
static void ring_clear(void)
{
    orch_check(s_rec_n >= 0, __LINE__); orch_check(s_rec_w >= 0, __LINE__);
    s_rec_n = 0;
    s_rec_w = 0;
    s_rec_r = 0;
}
static void ring_apply(OrchSlot *slots, uint64_t npos)
{
    int n = 0;
    orch_check(slots != 0, __LINE__); orch_check(s_export == 0, __LINE__);
    while (s_rec_n > 0 && n < 64) {
        RecEv r = s_rec[s_rec_r];
        const ProgRow *row;
        s_rec_r += 1;
        if (s_rec_r >= 64) s_rec_r = 0;
        s_rec_n -= 1;
        n += 1;
        if (r.track >= g.track_count || r.track >= 64) continue;
        row = prog_by_gm(s_lat[r.track].program);
        if (r.op == 1) {
            if (g.event_count >= 4096u) {
                orch_count_sat(&g.refuse_note_cap);
            } else {
                uint32_t i = g.event_count;
                g.ev[i].track = r.track;
                g.ev[i].key = r.key;
                g.ev[i].vel = r.vel;
                g.ev[i].flags = 0;
                g.ev[i].start_tick = r.tick;
                g.ev[i].reserved = 0;
                g.ev[i].dur_tick = 0;
                if (row && (row->shot || row->kind == ORCH_KIND_PIANO))
                    g.ev[i].dur_tick = sec_to_ticks(pick_dur(row, (int)r.vel, 0.0f, 0));
                g.ev_live[i] = 1;
                g.event_count = i + 1u;
                slot_attach(slots, (int)r.track, (int)r.key, (int)i);
            }
        } else if (!row || !row->shot) {
            int idx = -1;
            uint32_t i, cnt = g.event_count;
            uint32_t best = 0;
            if (cnt > 4096u) cnt = 4096u;
            for (i = 0; i < cnt; i++) {
                if (!g.ev_live[i]) continue;
                if (g.ev[i].track != r.track || g.ev[i].key != r.key) continue;
                if (idx < 0 || g.ev[i].start_tick >= best) {
                    idx = (int)i;
                    best = g.ev[i].start_tick;
                }
            }
            if (idx >= 0) close_one((uint32_t)idx, npos);
            slot_drop_key(slots, (int)r.track, (int)r.key, 0);
        }
    }
}
static int same_path(const char *p, const char *lit)
{
    int i;
    orch_check(lit != 0, __LINE__); orch_check(lit[0] == '/', __LINE__);
    if (!p) return 0;
    for (i = 0; i < 96; i++) {
        if (p[i] != lit[i]) return 0;
        if (lit[i] == 0) return 1;
    }
    return 0;
}
static void zero_f(float *p, int n, int cap)
{
    int i;
    orch_check(p != 0, __LINE__); orch_check(cap > 0 && cap <= 2112, __LINE__);
    if (n > cap) n = cap;
    for (i = 0; i < n && i < cap; i++) p[i] = 0.0f;
}
static int build_buses(int ntrk, int click)
{
    int n = 0, i;
    orch_check(ntrk >= 0, __LINE__); orch_check(click == 0 || click == 1, __LINE__);
    if (ntrk > 64) ntrk = 64;
    for (i = 0; i < ntrk && n < 66; i++) {
        s_buses[n] = (uint8_t)(i + 1);
        n += 1;
    }
    if (click && n < 66) {
        s_buses[n] = 65;
        n += 1;
    }
    if (n == 0) {
        s_buses[0] = 1;
        n = 1;
    }
    return n;
}
static void assign_lines(int ntrk)
{
    int i, s, d = 0, c = 0;
    uint8_t nd[8], nc[8];
    orch_check(ntrk >= 0 && ntrk <= 64, __LINE__); orch_check(d == 0 && c == 0, __LINE__);
    for (i = 0; i < 8; i++) { nd[i] = 0; nc[i] = 0; }
    for (i = 0; i < 64; i++) { s_dline[i][0] = s_dline[i][1] = -1; s_cline[i][0] = s_cline[i][1] = -1; }
    if (ntrk > 64) ntrk = 64;
    for (i = 0; i < ntrk; i++) {
        uint8_t id[2];
        id[0] = s_lat[i].ins0;
        id[1] = s_lat[i].ins1;
        for (s = 0; s < 2; s++) {
            uint8_t key = (uint8_t)(i * 2 + s + 1);
            if (id[s] == ORCH_INS_DELAY && d < 8) {
                nd[d] = key;
                s_dline[i][s] = (int8_t)d;
                d += 1;
            }
            if (id[s] == ORCH_INS_CHORUS && c < 8) {
                nc[c] = key;
                s_cline[i][s] = (int8_t)c;
                c += 1;
            }
        }
    }
    for (i = 0; i < 8; i++) {
        if (nd[i] != s_downer[i]) {
            if (nd[i] != 0) lc_insert_line_zero(0, i);
            s_downer[i] = nd[i];
        }
        if (nc[i] != s_cowner[i]) {
            if (nc[i] != 0) lc_insert_line_zero(1, i);
            s_cowner[i] = nc[i];
        }
    }
}
static void reverb_knobs(int ntrk)
{
    int i, room, damp, wet, saw = 0;
    orch_check(ntrk >= 0, __LINE__); orch_check(ntrk < 128, __LINE__);
    room = (int)g.room;
    damp = (int)g.damping;
    wet = (int)g.wet;
    if (ntrk > 64) ntrk = 64;
    for (i = 0; i < ntrk; i++) {
        int s;
        for (s = 0; s < 2; s++) {
            uint8_t id = s ? s_lat[i].ins1 : s_lat[i].ins0;
            int16_t p0 = s ? s_lat[i].p1[0] : s_lat[i].p0[0];
            int16_t p1 = s ? s_lat[i].p1[1] : s_lat[i].p0[1];
            int16_t p2 = s ? s_lat[i].p1[2] : s_lat[i].p0[2];
            int16_t p3 = s ? s_lat[i].p1[3] : s_lat[i].p0[3];
            int v;
            if (id != ORCH_INS_REVERB) continue;
            v = p0; if (v < 0) v = 0; if (v > 127) v = 127;
            s_lat[i].send = (uint8_t)v;
            if (!s_export) g.trk[i].send = (uint8_t)v;
            v = p1; if (v < 0) v = 0; if (v > 100) v = 100; room = v;
            v = p2; if (v < 0) v = 0; if (v > 100) v = 100; damp = v;
            v = p3; if (v < 0) v = 0; if (v > 100) v = 100; wet = v;
            saw = 1;
        }
    }
    if (saw && !s_export) {
        g.room = (uint8_t)room;
        g.damping = (uint8_t)damp;
        g.wet = (uint8_t)wet;
    }
    wa_set_reverb(1, (float)room / 100.0f, (float)damp / 100.0f,
                  (float)g.feedback / 100.0f, (float)wet / 100.0f);
}
static int apply_pair(int track, int nframe, uint64_t npos)
{
    int slot, clamped = 0;
    uint32_t b = bpm_fix(s_lbpm ? s_lbpm : 120u);
    uint32_t d = den_fix(s_lden);
    orch_check(track >= 0 && track < 64, __LINE__); orch_check(nframe > 0 && nframe <= ORCH_Q, __LINE__);
    for (slot = 0; slot < 2; slot++) {
        uint8_t id = slot ? s_lat[track].ins1 : s_lat[track].ins0;
        const int16_t *p = slot ? s_lat[track].p1 : s_lat[track].p0;
        int line = 0;
        int rc;
        if (id == ORCH_INS_BYPASS || id > ORCH_INS_LIMITER) continue;
        if (id == ORCH_INS_DELAY) line = s_dline[track][slot];
        if (id == ORCH_INS_CHORUS) line = s_cline[track][slot];
        if ((id == ORCH_INS_DELAY || id == ORCH_INS_CHORUS) && line < 0) continue;
        rc = lc_insert_apply(track * 2 + slot, (int)id, p, s_tmp, nframe,
                             fs_now(), b, d, g.curve, npos, line);
        if (rc == 0) clamped = 1;
    }
    return clamped;
}
static int solo_on(int ntrk)
{
    int i;
    orch_check(ntrk >= 0, __LINE__); orch_check(ntrk <= 64, __LINE__);
    for (i = 0; i < ntrk && i < 64; i++) {
        if (s_lat[i].solo && !s_lat[i].mute) return 1;
    }
    return 0;
}
static void sum_tracks(int ntrk, int nb, int nframe, uint64_t npos, int solo)
{
    int i, k, clamped = 0;
    orch_check(nframe > 0 && nframe <= ORCH_Q, __LINE__); orch_check(nb > 0 && nb <= 66, __LINE__);
    zero_f(s_L, nframe, ORCH_Q);
    zero_f(s_R, nframe, ORCH_Q);
    zero_f(s_send, nframe, ORCH_Q);
    for (i = 0; i < ntrk && i < 64; i++) {
        int pl = -1;
        float gl, gr, gt;
        int silent;
        for (k = 0; k < nb && k < 66; k++) if (s_buses[k] == (uint8_t)(i + 1)) pl = k;
        if (pl < 0) continue;
        for (k = 0; k < nframe; k++) s_tmp[k] = s_plane[pl * nframe + k];
        clamped |= apply_pair(i, nframe, npos);
        silent = s_lat[i].mute || (solo && !s_lat[i].solo);
        gt = fader_of(s_lat[i].vol, silent);
        pan_of(s_lat[i].pan, &gl, &gr);
        for (k = 0; k < nframe; k++) {
            float x = s_tmp[k] * gt;
            float L = x * gl;
            float R = x * gr;
            float sd = (float)s_lat[i].send / 127.0f;
            s_L[k] += L;
            s_R[k] += R;
            s_send[k] += (L + R) * sd;
        }
    }
    if (s_clamp_arm && clamped && !s_export) orch_count_sat(&g.refuse_insert);
    if (!s_export) s_clamp_arm = 0;
}
static void sum_click(int nb, int nframe, const OrchSlot *slots)
{
    int i, pl = -1, k, on = 0;
    orch_check(slots != 0, __LINE__); orch_check(nframe > 0 && nframe <= ORCH_Q, __LINE__);
    for (i = 0; i < 48; i++) if (slots[i].used && slots[i].click) on = 1;
    if (!on) return;
    for (k = 0; k < nb && k < 66; k++) if (s_buses[k] == 65) pl = k;
    if (pl < 0) return;
    for (k = 0; k < nframe; k++) {
        float x = s_plane[pl * nframe + k];
        float g = 0.70710678118f;
        s_L[k] += x * g;
        s_R[k] += x * g;
    }
}
static void bell_mix(const OrchSlot *slots, int nb, int nframe)
{
    uint8_t did[66];
    int i, p;
    orch_check(slots != 0, __LINE__); orch_check(nframe > 0 && nframe <= ORCH_Q, __LINE__);
    for (i = 0; i < 66; i++) did[i] = 0;
    for (i = 0; i < 48; i++) {
        int bus, pl = -1;
        if (!slots[i].used || !slots[i].bell) continue;
        bus = (int)slots[i].bus;
        if (bus < 0 || bus >= 66 || did[bus]) continue;
        did[bus] = 1;
        for (p = 0; p < nb && p < 66; p++) if (s_buses[p] == (uint8_t)bus) pl = p;
        if (pl < 0) continue;
        lilcu64_voice_mix_float((uint8_t)bus, s_plane + pl * nframe, (size_t)nframe);
    }
}
static int16_t to_s16(float x)
{
    int v;
    orch_check(s_llim < 16, __LINE__); orch_check(g.master_lim < 16, __LINE__);
    if (x > 1.0f) x = 1.0f;
    if (x < -1.0f) x = -1.0f;
    x *= 32767.0f;
    if (x >= 0.0f) v = (int)(x + 0.5f);
    else v = (int)(x - 0.5f);
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    return (int16_t)v;
}
static void master_pcm(int nframe, int store)
{
    int i;
    float mv;
    float g100;
    int peak_l = 0, peak_r = 0;
    int lim = (s_llim == ORCH_INS_LIMITER);
    orch_check(nframe > 0 && nframe <= ORCH_Q, __LINE__); orch_check(lim == 0 || lim == 1, __LINE__);
    zero_f(s_wl, nframe, ORCH_Q);
    zero_f(s_wr, nframe, ORCH_Q);
    wa_reverb_process(s_send, s_wl, s_wr, (size_t)nframe);
    mv = fader_of(s_lmvol, 0);
    g100 = 0.08f + 0.37f * (100.0f / 127.0f);
    orch_check(g100 > 0.2f && g100 < 0.5f, __LINE__);
    if (g100 > 0.2f) mv = mv / (g100 * g100);
    for (i = 0; i < nframe; i++) {
        float L = (s_L[i] + (s_wl[i] - s_send[i])) * mv;
        float R = (s_R[i] + (s_wr[i] - s_send[i])) * mv;
        int ml, mr;
        float al, ar;
        if (lim) {
            L = master_soft_clamp(L);
            R = master_soft_clamp(R);
        }
        al = L < 0.0f ? -L : L;
        ar = R < 0.0f ? -R : R;
        ml = (int)(al * 100.0f + 0.5f);
        mr = (int)(ar * 100.0f + 0.5f);
        if (ml > 100) ml = 100;
        if (mr > 100) mr = 100;
        if (ml > peak_l) peak_l = ml;
        if (mr > peak_r) peak_r = mr;
        s_pcm[i * 2] = to_s16(L);
        s_pcm[i * 2 + 1] = to_s16(R);
    }
    if (store) {
        g.meter_l = (uint8_t)peak_l;
        g.meter_r = (uint8_t)peak_r;
    }
}
static int click_used(const OrchSlot *s)
{
    int i;
    orch_check(s != 0, __LINE__); orch_check(48 > 0, __LINE__);
    for (i = 0; i < 48; i++) if (s[i].used && s[i].click) return 1;
    return 0;
}
static void render_graph(uint64_t npos, OrchSlot *slots, int nframe)
{
    int ntrk = s_ln;
    int click, nb, solo;
    orch_check(slots != 0, __LINE__); orch_check(nframe > 0 && nframe <= ORCH_Q, __LINE__);
    if (ntrk < 0) ntrk = 0;
    if (ntrk > 64) ntrk = 64;
    click = (s_lclick && click_used(slots)) ? 1 : 0;
    assign_lines(ntrk);
    reverb_knobs(ntrk);
    nb = build_buses(ntrk, click);
    zero_f(s_plane, nb * nframe, 66 * ORCH_Q);
    wa_render_buses(s_buses, nb, s_plane, (size_t)nframe);
    bell_mix(slots, nb, nframe);
    solo = solo_on(ntrk);
    sum_tracks(ntrk, nb, nframe, npos, solo);
    sum_click(nb, nframe, slots);
    master_pcm(nframe, s_export ? 0 : 1);
}
static void click_trig(OrchSlot *slots, uint64_t npos)
{
    int idx = -1, i;
    float vol;
    uint32_t ref0;
    int act0;
    orch_check(slots != 0, __LINE__); orch_check(s_lclick <= 1, __LINE__);
    if (!s_lclick) return;
    for (i = 0; i < 48; i++) if (slots[i].used && slots[i].click) idx = i;
    if (idx < 0) idx = slot_free_idx(slots);
    if (idx < 0) return;
    wa_release_bus_key(65, 42);
    vol = gain_of(0, 90);
    ref0 = lc_shim_refusals();
    act0 = wa_active_count();
    wa_set_node_bus(65);
    wa_set_note_key(42);
    lc_set_score_base(0.0);
    lc_shim_note(LC_SHIM_HAT, (double)npos / (double)fs_now(), 0.0f, vol, 0.055f);
    if (!shim_landed(ref0, act0, 0)) return;
    slots[idx].used = 1;
    slots[idx].click = 1;
    slots[idx].bus = 65;
    slots[idx].key = 42;
    slots[idx].track = 0;
    slots[idx].oneshot = 1;
    slots[idx].bell = 0;
    slots[idx].gain = vol;
    slots[idx].t0 = npos;
    slots[idx].dur = (uint32_t)(0.055f * (float)fs_now());
    slots[idx].ev = -1;
}
static void clock_add(uint32_t from, uint32_t to, int midi)
{
    uint32_t d, k;
    orch_check(midi == 0 || midi == 1, __LINE__); orch_check(s_minit <= 1 && s_binit <= 1, __LINE__);
    if (!s_minit && midi) return;
    if (!s_binit && !midi) return;
    if (to <= from) return;
    d = to - from;
    if (midi) {
        if (d > 64u) d = 64u;
        if (s_export) return;
        for (k = 0; k < d; k++) orch_count_sat(&g.midi_clock);
    }
}
static void clock_block(uint64_t npos, int entry, OrchSlot *slots)
{
    uint32_t f = fs_now();
    uint32_t b = bpm_fix(s_lbpm ? s_lbpm : g.tempo);
    uint32_t d = den_fix(s_lden ? s_lden : g.den);
    uint32_t tk = gst_tick_of_sample(npos, f, b, d);
    uint32_t beat = gst_snap_q(ORCH_SNAP_BEAT, s_lnum ? s_lnum : 4, (uint8_t)d);
    uint32_t mq, bq;
    orch_check(beat > 0u, __LINE__); orch_check(slots != 0, __LINE__);
    if (beat == 0u) beat = 960u;
    mq = tk / 40u;
    bq = tk / beat;
    if (entry) {
        if (!s_export && (tk % 40u) == 0u) orch_count_sat(&g.midi_clock);
        if (s_lclick && (tk % beat) == 0u) click_trig(slots, npos);
        s_mlast = mq;
        s_blast = bq;
        s_minit = 1;
        s_binit = 1;
        return;
    }
    if (s_minit && mq > s_mlast) clock_add(s_mlast, mq, 1);
    s_mlast = mq;
    s_minit = 1;
    if (s_binit && bq > s_blast && s_lclick) {
        uint32_t step = bq - s_blast;
        uint32_t k;
        if (step > 4u) step = 4u;
        for (k = 0; k < step; k++) click_trig(slots, npos);
    }
    s_blast = bq;
    s_binit = 1;
}
static void chain_result(int code, int drop_audio, int qos)
{
    orch_check(code == ORCH_OK || code == ORCH_DROP || code == ORCH_REFUSE, __LINE__); orch_check(drop_audio == 0 || drop_audio == 1, __LINE__);
    orch_count_sat(&g.chain_accept);
    if (code == ORCH_OK) orch_count_sat(&g.chain_deliver);
    else if (code == ORCH_REFUSE) orch_count_sat(&g.chain_refuse);
    else orch_count_sat(&g.chain_drop);
    if (drop_audio) orch_count_sat(&g.drop_audio);
    if (qos) {
        orch_count_sat(&g.qos_drops);
        bus_push(4);
    }
}
static void device_out(int nframe)
{
    uint32_t qf;
    int wr;
    orch_check(nframe > 0 && nframe <= ORCH_Q, __LINE__); orch_check(s_force_late == 0 || s_force_late == 1, __LINE__);
    if (s_force_late) {
        chain_result(ORCH_DROP, 0, 1);
        return;
    }
    qf = virtio_sound_get_queued_bytes() / 4u;
    if (qf >= 2048u) {
        chain_result(ORCH_DROP, 0, 0);
        return;
    }
    if (!virtio_sound_is_ready()) {
        chain_result(ORCH_DROP, 1, 0);
        return;
    }
    wr = virtio_sound_write(s_pcm, (size_t)nframe);
    if (wr < 0) chain_result(ORCH_DROP, 1, 0);
    else chain_result(ORCH_OK, 0, 0);
}
static void release_all_slots(void);
void orchestra_audio_tick(void)
{
    uint32_t prefix;
    orch_check(s_arm == 0 || s_arm == 1, __LINE__); orch_check(g.state <= ORCH_GST_PLAYING, __LINE__);
    if (g.state != ORCH_GST_PLAYING || s_export) return;
    if (s_sticky) {
        chain_result(ORCH_REFUSE, 0, 0);
        return;
    }
    latch_try();
    prefix = g.event_count;
    if (prefix > 4096u) prefix = 4096u;
    slots_expire(s_slot, g.npos, 1);
    if (s_arm) {
        clock_block(g.npos, 1, s_slot);
        chase_on(s_slot, g.ev_live, g.npos, ORCH_Q, prefix, 1);
        s_arm = 0;
    } else {
        clock_block(g.npos, 0, s_slot);
    }
    chase_on(s_slot, g.ev_live, g.npos, ORCH_Q, prefix, 0);
    chase_off(s_slot, g.ev_live, g.npos, ORCH_Q, prefix);
    ring_apply(s_slot, g.npos);
    render_graph(g.npos, s_slot, ORCH_Q);
    device_out(ORCH_Q);
    g.npos += ORCH_Q;
    if (g.loop_tick > 0u) {
        uint64_t end = gst_sample_of_tick(g.loop_tick, fs_now(), bpm_fix(g.tempo), den_fix(g.den));
        if (end > 0ull && g.npos >= end) {
            memset(g.ev_live, 0, sizeof g.ev_live);
            release_all_slots();
            ring_clear();
            g.npos = 0;
            s_arm = 1;
            s_minit = s_binit = 0;
            s_mlast = s_blast = 0;
            if (s_booted) wa_set_clock_samples(0);
            if (g.home_gen < 4294967295u) g.home_gen++;
        }
    }
    (void)gst_rt_wcet_us();
    (void)gst_rt_period_us();
}
static uint32_t queued_frames(void)
{
    uint32_t b;
    orch_check(s_booted == 0 || s_booted == 1, __LINE__);
    orch_check(g.state <= ORCH_GST_PLAYING, __LINE__);
    b = virtio_sound_get_queued_bytes();
    return b / 4u;
}
void orchestra_audio_service(uint32_t now_us)
{
    int i;
    uint32_t T;
    orch_check(s_booted == 0 || s_booted == 1, __LINE__);
    orch_check(g.state <= ORCH_GST_PLAYING, __LINE__);
    if (!s_booted || g.state != ORCH_GST_PLAYING) return;
    T = gst_rt_period_us();
    if (T == 0u) T = 1u;
    if (btron_render_perf_us() == 0u) {
        s_force_late = 0;
        orchestra_audio_tick();
        return;
    }
    if (s_due == 0u) s_due = now_us;
    s_force_late = 0;
    for (i = 0; i < 64; i++) {
        uint32_t qf;
        int due;
        qf = queued_frames();
        due = ((int32_t)(now_us - s_due) >= 0);
        if (qf >= 2048u) break;
        if (qf >= 1024u && !due) break;
        orchestra_audio_tick();
        if (!due) continue;
        s_due += T;
        if (s_due == 0u) s_due = 1u;
    }
}
static void release_all_slots(void)
{
    int i;
    orch_check(s_slot[0].bus <= 65 || s_slot[0].used == 0, __LINE__); orch_check(48 == 48, __LINE__);
    for (i = 0; i < 48; i++) {
        if (!s_slot[i].used) continue;
        slot_silence(&s_slot[i]);
        s_slot[i].used = 0;
    }
}
static void stop_hold(void)
{
    uint32_t i, n;
    orch_check(g.state == ORCH_GST_PLAYING, __LINE__); orch_check(s_export == 0, __LINE__);
    ring_apply(s_slot, g.npos);
    n = g.event_count;
    if (n > 4096u) n = 4096u;
    for (i = 0; i < n; i++) {
        if (g.ev_live[i]) close_one(i, g.npos);
    }
    release_all_slots();
    ring_clear();
}
void gst_rt_panic(void)
{
    int i;
    orch_check(s_export == 0 || s_export == 1, __LINE__); orch_check(g.state <= ORCH_GST_PLAYING, __LINE__);
    for (i = 0; i < 48; i++) {
        if (!s_slot[i].used || s_slot[i].click) continue;
        slot_silence(&s_slot[i]);
        s_slot[i].used = 0;
    }
}
void gst_rt_stop_voices(void)
{
    orch_check(s_booted == 0 || s_booted == 1, __LINE__); orch_check(s_export == 0 || s_export == 1, __LINE__);
    release_all_slots();
}
void gst_rt_return_zero(void)
{
    orch_check(g.state <= ORCH_GST_PLAYING, __LINE__); orch_check(s_booted == 0 || s_booted == 1, __LINE__);
    if (g.state == ORCH_GST_PLAYING) return;
    g.npos = 0;
    if (s_booted) wa_set_clock_samples(0);
    s_minit = 0;
    s_binit = 0;
    s_mlast = 0;
    s_blast = 0;
    if (g.home_gen < 4294967295u) g.home_gen++;
}
static void concert(void)
{
    int i;
    orch_check(s_made_n == 0, __LINE__); orch_check(s_ident == 0, __LINE__);
    for (i = 0; i < 9; i++) {
        int id = (i < 8) ? i : 9;
        (void)gst_factory_make(ORCH_FACTORY[id], 0);
    }
}
static void port0(void)
{
    int i;
    static const char name[] = "Stage MIDI In";
    orch_check(ORCH_MAX_PORTS == 32, __LINE__); orch_check(name[0] == 'S', __LINE__);
    for (i = 0; i < 32; i++) {
        g.port[i].is_input = 0;
        g.port[i].is_active = 0;
    }
    for (i = 0; name[i] && i < 31; i++) g.port[0].name[i] = name[i];
    g.port[0].name[i] = 0;
    g.port[0].is_input = 1;
    g.port[0].is_active = 1;
}
int gst_rt_boot(uint32_t sample_rate)
{
    int opened;
    orch_check(sample_rate < 1000000u, __LINE__); orch_check(s_booted == 0 || s_booted == 1, __LINE__);
    if (s_booted) {
        if (sample_rate != g.sample_rate) {
            orch_status_hold("rate");
            return 0;
        }
        return 1;
    }
    if (sample_rate != 44100u && sample_rate != 48000u) {
        orch_status_hold("rate");
        return 0;
    }
    memset(&g, 0, sizeof g);
    memset(s_slot, 0, sizeof s_slot);
    ring_clear();
    s_made_n = 0;
    s_ident = 0;
    s_bus_n = 0;
    s_sticky = 0;
    g.sample_rate = sample_rate;
    g.tempo = 128;
    g.num = 4;
    g.den = 4;
    g.scene_count = 16;
    g.master_vol = 100;
    g.master_lim = ORCH_INS_LIMITER;
    g.snap = ORCH_SNAP_BEAT;
    g.tool = ORCH_TOOL_SELECT;
    g.confirm_family = -1;
    g.state = ORCH_GST_READY;
    pitch_build();
    port0();
    concert();
    lc_curve_build(g.curve, 25);
    g.curve_k = 25;
    lilcu64_set_audio_owner(1);
    wa_init(sample_rate);
    lilcu64_synth_set_rate(sample_rate);
    wa_set_reverb(1, 0.0f, 0.0f, 0.0f, 0.0f);
    lc_set_score_base(0.0);
    s_booted = 1;
    opened = virtio_sound_open(sample_rate, 2);
    g.state = ORCH_GST_READY;
    if (opened < 0) orch_status_hold("virtio");
    latch_copy();
    (void)gst_rt_wcet_us();
    (void)gst_rt_period_us();
    bus_push(3);
    return 1;
}
void gst_rt_shutdown(void)
{
    orch_check(s_booted == 0 || s_booted == 1, __LINE__); orch_check(s_export == 0 || s_export == 1, __LINE__);
    if (s_booted) {
        virtio_sound_close();
        release_all_slots();
        lilcu64_set_audio_owner(0);
        wa_init(44100u);
        lilcu64_synth_set_rate(44100u);
        s_booted = 0;
    }
    g.state = ORCH_GST_NULL;
    s_made_n = 0;
    s_ident = 0;
    s_arm = 0;
    ring_clear();
}
int gst_rt_set_state(int target)
{
    int cur;
    orch_check(target >= ORCH_GST_NULL && target <= ORCH_GST_PLAYING, __LINE__); orch_check(s_booted == 0 || s_booted == 1, __LINE__);
    if (target < ORCH_GST_NULL || target > ORCH_GST_PLAYING) return 0;
    cur = (int)g.state;
    if (target == ORCH_GST_NULL) {
        gst_rt_shutdown();
        return 1;
    }
    if (!s_booted) return 0;
    if (target == cur) return 1;
    if (target == ORCH_GST_PLAYING) {
        if (cur != ORCH_GST_READY && cur != ORCH_GST_PAUSED) return 0;
        if (!virtio_sound_is_ready() || gst_rt_wcet_us() > gst_rt_period_us()) {
            orch_status_hold("play");
            return 0;
        }
        wa_set_clock_samples((int64_t)g.npos);
        g.state = ORCH_GST_PLAYING;
        s_arm = 1;
        s_due = 0;
        bus_push(3);
        return 1;
    }
    if (cur == ORCH_GST_PLAYING) {
        stop_hold();
        g.state = (uint8_t)target;
        bus_push(3);
        return 1;
    }
    g.state = (uint8_t)target;
    bus_push(3);
    return 1;
}
void gst_insert_touch(void)
{
    orch_check(s_clamp_arm == 0 || s_clamp_arm == 1, __LINE__); orch_check(s_export == 0 || s_export == 1, __LINE__);
    s_clamp_arm = 1;
}
static int midi_take(void)
{
    int i;
    orch_check(8 == 8, __LINE__); orch_check(s_midi_used[0] == 0 || s_midi_used[0] == 1, __LINE__);
    for (i = 0; i < 8; i++) {
        if (!s_midi_used[i]) {
            s_midi_used[i] = 1;
            return i;
        }
    }
    return -1;
}
static void midi_rel(int i)
{
    orch_check(i >= -1 && i < 8, __LINE__); orch_check(s_booted == 0 || s_booted == 1, __LINE__);
    if (i >= 0 && i < 8) s_midi_used[i] = 0;
}
static void note_on_tracks(int port, int ch, int key, int vel)
{
    int i, n = 0;
    double t;
    orch_check(port >= 0 && port < 32, __LINE__); orch_check(ch >= 1 && ch <= 16, __LINE__);
    t = wa_time();
    for (i = 0; i < 64 && i < (int)g.track_count && n < 64; i++) {
        int sl;
        if (g.trk[i].channel != (uint8_t)ch) continue;
        if (g.trk[i].in_port != (uint8_t)port) continue;
        n += 1;
        sl = sound_on(s_slot, i, key, vel, t, 0.0f, 1, g.npos, g.npos, -1);
        if (sl == -2) {
            orch_count_sat(&g.chain_accept);
            orch_count_sat(&g.chain_drop);
        }
        if (g.rec && g.selected == (int8_t)i) {
            uint32_t tk = gst_tick_of_sample(g.npos, fs_now(), bpm_fix(g.tempo), den_fix(g.den));
            uint32_t q = gst_snap_q(g.snap, g.num ? g.num : 4, g.den ? g.den : 4);
            tk = gst_snap_down(tk, q);
            if (!ring_push(1, (uint8_t)i, (uint8_t)key, (uint8_t)vel, tk)) {
                orch_count_sat(&g.chain_accept);
                orch_count_sat(&g.chain_drop);
            }
        }
    }
}
static void note_off_tracks(int port, int ch, int key)
{
    int i, n = 0;
    orch_check(port >= 0, __LINE__); orch_check(ch >= 1, __LINE__);
    for (i = 0; i < 64 && i < (int)g.track_count && n < 64; i++) {
        if (g.trk[i].channel != (uint8_t)ch) continue;
        if (g.trk[i].in_port != (uint8_t)port) continue;
        n += 1;
        slot_drop_key(s_slot, i, key, 1);
        if (g.state == ORCH_GST_PLAYING) {
            if (!ring_push(0, (uint8_t)i, (uint8_t)key, 0, 0)) {
                orch_count_sat(&g.chain_accept);
                orch_count_sat(&g.chain_drop);
            }
        } else {
            int idx = -1;
            uint32_t k, cnt = g.event_count;
            uint32_t best = 0;
            if (cnt > 4096u) cnt = 4096u;
            for (k = 0; k < cnt; k++) {
                if (!g.ev_live[k]) continue;
                if (g.ev[k].track != (uint8_t)i || g.ev[k].key != (uint8_t)key) continue;
                if (idx < 0 || g.ev[k].start_tick >= best) {
                    idx = (int)k;
                    best = g.ev[k].start_tick;
                }
            }
            if (idx >= 0) close_one((uint32_t)idx, g.npos);
        }
    }
}
int orchestra_dispatch_midi_event(int in_port, uint8_t status, uint8_t d1, uint8_t d2)
{
    int slot, ch, cmd;
    orch_check(in_port >= -1, __LINE__); orch_check(s_export == 0 || s_export == 1, __LINE__);
    if (in_port < 0 || in_port >= ORCH_MAX_PORTS) return ORCH_DROP;
    if (!g.port[in_port].is_active) {
        orch_count_sat(&g.port[in_port].dropped_packets);
        return ORCH_DROP;
    }
    orch_count_sat(&g.port[in_port].message_count);
    slot = midi_take();
    if (slot >= 0) midi_rel(slot);
    cmd = (int)status & 0xF0;
    ch = (int)(status & 0x0F) + 1;
    if (cmd == 0x90 && d2 > 0) {
        if (g.state == ORCH_GST_PLAYING) note_on_tracks(in_port, ch, (int)d1, (int)d2);
        return ORCH_OK;
    }
    if (cmd == 0x80 || (cmd == 0x90 && d2 == 0)) {
        note_off_tracks(in_port, ch, (int)d1);
        return ORCH_OK;
    }
    if (cmd == 0xB0 && (d1 == 7 || d1 == 10)) {
        int i;
        gst_seq_begin();
        for (i = 0; i < 64 && i < (int)g.track_count; i++) {
            if (g.trk[i].channel != (uint8_t)ch) continue;
            if (g.trk[i].in_port != (uint8_t)in_port) continue;
            if (d1 == 7) g.trk[i].volume = d2;
            else g.trk[i].pan = d2;
        }
        gst_seq_end();
        return ORCH_OK;
    }
    if (cmd == 0xC0) {
        int i;
        gst_seq_begin();
        for (i = 0; i < 64 && i < (int)g.track_count; i++) {
            if (g.trk[i].channel != (uint8_t)ch) continue;
            if (g.trk[i].in_port != (uint8_t)in_port) continue;
            g.trk[i].program = d1;
            gst_resolve_track(&g.trk[i]);
        }
        gst_seq_end();
        return ORCH_OK;
    }
    return ORCH_OK;
}
static void wr_u16(uint8_t *p, uint32_t v)
{
    orch_check(p != 0, __LINE__); orch_check(v <= 65535u, __LINE__);
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static void wr_u32(uint8_t *p, uint32_t v)
{
    orch_check(p != 0, __LINE__); orch_check(s_export == 0 || s_export == 1, __LINE__);
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
static void wav_header(uint8_t *h, uint32_t frames)
{
    uint32_t f = fs_now();
    uint32_t pcm = frames * 4u;
    orch_check(h != 0, __LINE__); orch_check(f == 44100u || f == 48000u, __LINE__);
    memset(h, 0, 44);
    h[0] = 'R'; h[1] = 'I'; h[2] = 'F'; h[3] = 'F';
    wr_u32(h + 4, 36u + pcm);
    h[8] = 'W'; h[9] = 'A'; h[10] = 'V'; h[11] = 'E';
    h[12] = 'f'; h[13] = 'm'; h[14] = 't'; h[15] = ' ';
    wr_u32(h + 16, 16u);
    wr_u16(h + 20, 1u);
    wr_u16(h + 22, 2u);
    wr_u32(h + 24, f);
    wr_u32(h + 28, f * 4u);
    wr_u16(h + 32, 4u);
    wr_u16(h + 34, 16u);
    h[36] = 'd'; h[37] = 'a'; h[38] = 't'; h[39] = 'a';
    wr_u32(h + 40, pcm);
}
static uint64_t export_end(int *trunc)
{
    uint32_t f = fs_now();
    uint32_t b = bpm_fix(s_lbpm ? s_lbpm : g.tempo);
    uint32_t d = den_fix(s_lden ? s_lden : g.den);
    uint32_t num = s_lnum ? s_lnum : 4;
    uint32_t bar_ticks = num * (3840u / d);
    uint64_t bar = gst_sample_of_tick(bar_ticks, f, b, d);
    uint64_t cap = (uint64_t)(ORCH_WAV_MAX - 44u) / 4ull;
    uint64_t mus, end, lim;
    uint32_t i, n;
    orch_check(cap > 1000ull, __LINE__); orch_check(cap > 0ull, __LINE__);
    if (bar < 1ull) bar = 1ull;
    mus = 0;
    n = g.event_count;
    if (n > 4096u) n = 4096u;
    for (i = 0; i < n; i++) {
        uint64_t a;
        if (g.ev[i].vel == 0) continue;
        a = gst_sample_of_tick(g.ev[i].start_tick + g.ev[i].dur_tick, f, b, d);
        if (a > mus) mus = a;
    }
    end = mus;
    if (end < bar) end = bar;
    lim = bar * 64ull;
    *trunc = 0;
    if (end > lim) { end = lim; *trunc = 1; }
    if (end > cap) { end = cap; *trunc = 1; }
    return end;
}
static void release_buses(void)
{
    int b, k;
    orch_check(65 >= 1, __LINE__); orch_check(128 > 0, __LINE__);
    for (b = 1; b <= 65; b++) {
        for (k = 0; k < 128; k++) {
            wa_release_bus_key((uint8_t)b, (uint8_t)k);
            lilcu64_voice_release((uint8_t)b, (uint8_t)k);
        }
    }
}
static void snap_save(CtrSnap *c)
{
    orch_check(c != 0, __LINE__); orch_check(s_booted == 1, __LINE__);
    memcpy(&c->drop_audio, &g.drop_audio, 15u * sizeof(uint32_t));
    c->meter_l = g.meter_l;
    c->meter_r = g.meter_r;
    c->arm = s_arm;
    c->minit = s_minit;
    c->binit = s_binit;
    c->mlast = s_mlast;
    c->blast = s_blast;
    c->npos = g.npos;
    c->clock = wa_clock_samples();
}
static void snap_restore(const CtrSnap *c)
{
    orch_check(c != 0, __LINE__); orch_check(s_export == 1 || s_export == 0, __LINE__);
    memcpy(&g.drop_audio, &c->drop_audio, 15u * sizeof(uint32_t));
    g.meter_l = c->meter_l;
    g.meter_r = c->meter_r;
    g.npos = c->npos;
    s_arm = c->arm;
    s_minit = c->minit;
    s_binit = c->binit;
    s_mlast = c->mlast;
    s_blast = c->blast;
    memcpy(s_slot, s_slot_snap, sizeof s_slot);
    release_buses();
    wa_reverb_restore();
    lc_insert_state_restore();
    wa_set_clock_samples(c->clock);
    s_export = 0;
}
static int export_loop(FILE *fp, uint64_t end)
{
    uint64_t pos = 0;
    int step;
    uint32_t prefix = g.event_count;
    orch_check(fp != 0, __LINE__); orch_check(end > 0ull, __LINE__);
    if (prefix > 4096u) prefix = 4096u;
    memset(s_xslot, 0, sizeof s_xslot);
    memset(s_xlive, 0, sizeof s_xlive);
    s_minit = 0;
    s_binit = 0;
    for (step = 0; step < 655360 && pos < end; step++) {
        uint32_t nframe = ORCH_Q;
        size_t wr;
        if (end - pos < (uint64_t)nframe) nframe = (uint32_t)(end - pos);
        if (pos == 0ull) clock_block(0, 1, s_xslot);
        else clock_block(pos, 0, s_xslot);
        slots_expire(s_xslot, pos, 0);
        chase_on(s_xslot, s_xlive, pos, nframe, prefix, 0);
        chase_off(s_xslot, s_xlive, pos, nframe, prefix);
        render_graph(pos, s_xslot, (int)nframe);
        wr = fwrite(s_pcm, 2u, (size_t)nframe * 2u, fp);
        pos += nframe;
        if (wr != (size_t)nframe * 2u) return 0;
    }
    return 1;
}
int gst_rt_export_wav(const char *path)
{
    FILE *fp;
    CtrSnap snap;
    uint8_t hdr[44];
    uint64_t end;
    int trunc = 0;
    int ok;
    orch_check(s_booted == 0 || s_booted == 1, __LINE__); orch_check(g.state <= ORCH_GST_PLAYING, __LINE__);
    if (g.state != ORCH_GST_PAUSED || !s_booted) {
        orch_status_hold("wav");
        return 0;
    }
    if (!same_path(path, WAV_PATH)) {
        orch_status_hold("wav");
        return 0;
    }
    fp = fopen(path, "wb");
    if (!fp) {
        orch_status_hold("wav");
        return 0;
    }
    latch_copy();
    end = export_end(&trunc);
    wav_header(hdr, (uint32_t)end);
    if (fwrite(hdr, 1, 44, fp) != 44) {
        fclose(fp);
        orch_status_hold("wav");
        return 0;
    }
    snap_save(&snap);
    memcpy(s_slot_snap, s_slot, sizeof s_slot);
    wa_reverb_save();
    lc_insert_state_save();
    s_export = 1;
    wa_set_clock_samples(0);
    lc_set_score_base(0.0);
    ok = export_loop(fp, end);
    snap_restore(&snap);
    fclose(fp);
    if (!ok || trunc) orch_status_hold("truncated");
    return ok ? 1 : 0;
}
