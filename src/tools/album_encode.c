/*
 * album_encode — host tool. Captures the four demo scores and writes
 * SYS/<stem>.orch plus SYS/<stem>.mid. Not linked into btron.
 *
 *   make album_encode && ./album_encode
 *
 * Run from the repo root so /SYS falls back to SYS/. The last step copies
 * bells onto SYS/orchestra.orch and SYS/orchestra.mid.
 */
#include <apps/orchestra.h>
#include "lilcu64_score.h"
#include "../media/lukhash_score.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int orch_session_load_buf(const uint8_t *buf, uint32_t n);

int virtio_sound_open(uint32_t sample_rate, uint8_t channels)
{
    (void)sample_rate;
    (void)channels;
    return 0;
}
int virtio_sound_write(const int16_t *samples, size_t frames)
{
    (void)samples;
    (void)frames;
    return 0;
}
void virtio_sound_close(void) {}
bool virtio_sound_is_ready(void) { return false; }
uint32_t virtio_sound_get_queued_bytes(void) { return 0; }
uint32_t btron_render_perf_us(void) { return 0; }
void lilcu64_shim_pulse_bell(float vol_mul, float pitch_mul)
{
    (void)vol_mul;
    (void)pitch_mul;
}
void lilcu64_set_audio_owner(int owner) { (void)owner; }
void lilcu64_synth_set_rate(uint32_t sample_rate) { (void)sample_rate; }
void lilcu64_voice_mix_float(uint8_t bus, float *mono, size_t frames)
{
    (void)bus;
    (void)mono;
    (void)frames;
}
void lilcu64_voice_release(uint8_t bus, uint8_t key)
{
    (void)bus;
    (void)key;
}

enum {
    OFF_TRK = 128,
    TRK_N = 96,
    OFF_SCN = 6272,
    SCN_N = 100,
    OFF_EV = 7872,
    EV_N = 16,
    GRP_MAX = 40
};

static const uint32_t COLS[8] = {
    0xFFF59E0Bu, 0xFF06B6D4u, 0xFFF97316u, 0xFFA855F7u,
    0xFF10B981u, 0xFFEF4444u, 0xFFEC4899u, 0xFFCBD5E1u
};

static const char *const SCN_NAME[15] = {
    "Soft", "Loud", "Dry", "Hall", "Rhythm", "Melody", "Bass up",
    "Drums up", "Narrow", "Wide", "Night", "Day", "Cue A", "Cue B", "Encore"
};

static const int SCN_SCALE[16] = {
    100, 78, 116, 64, 100, 92, 108, 124, 112, 84, 100, 72, 104, 96, 88, 110
};

typedef struct Grp {
    int shim;
    int phrase;
    int gm;
    int muted;
    const char *name;
    int nlane;
    uint32_t lend[8];
    int lch[8];
} Grp;

typedef struct Nv {
    int shim;
    int art;
    int gi;
    int lane;
    int ch;
    int ord;
    uint8_t key;
    uint8_t vel;
    uint32_t start;
    uint32_t dur;
} Nv;

static Grp s_grp[GRP_MAX];
static Nv s_nv[ORCH_MAX_EVENTS];
static int s_ng;
static int s_nn;
static int s_merges;
static uint8_t s_file[ORCH_FILE_BYTES];
static double s_worst;
static int s_clock_bad;
static int s_bad_shim;
static double s_bad_sec;
static uint32_t s_bad_tick;

static void wr16(uint8_t *b, int off, int v)
{
    unsigned u = (unsigned)(v & 0xFFFF);
    b[off] = (uint8_t)(u & 255u);
    b[off + 1] = (uint8_t)((u >> 8) & 255u);
}

static void wr32(uint8_t *b, int off, uint32_t v)
{
    b[off] = (uint8_t)(v & 255u);
    b[off + 1] = (uint8_t)((v >> 8) & 255u);
    b[off + 2] = (uint8_t)((v >> 16) & 255u);
    b[off + 3] = (uint8_t)((v >> 24) & 255u);
}

static void put_name(uint8_t *d, int cap, const char *src)
{
    int i;
    for (i = 0; i < cap; i++) d[i] = 0;
    if (!src) return;
    for (i = 0; i < cap - 1 && src[i] != 0; i++) d[i] = (uint8_t)src[i];
}

static uint32_t ticks_of(double sec, int bpm, int den, int mindur)
{
    double t = sec * 64.0 * (double)bpm / (double)den;
    long v;
    if (t < 0.0) t = 0.0;
    v = (long)(t + 0.5);
    if (mindur && v < 1) v = 1;
    if (v > 2000000000L) v = 2000000000L;
    return (uint32_t)v;
}

static void note_clock(uint32_t tick, double sec, int bpm, int den, int shim)
{
    uint64_t samp = gst_sample_of_tick(tick, 48000u, (uint32_t)bpm, (uint32_t)den);
    double err = (double)samp - sec * 48000.0;
    if (err < 0.0) err = -err;
    if (err > s_worst) s_worst = err;
    if (err > 96.0 && !s_clock_bad) {
        s_clock_bad = 1;
        s_bad_shim = shim;
        s_bad_sec = sec;
        s_bad_tick = tick;
    }
}

static int key_of(int shim, float hz)
{
    double k;
    int ik;
    if (shim == LC_SHIM_IND_KICK || shim == LC_SHIM_AMIGA_KICK) return 36;
    if (shim == LC_SHIM_SNARE || shim == LC_SHIM_IND_SNARE || shim == LC_SHIM_AMIGA_SNARE || shim == LC_CAP_HAMMER)
        return 38;
    if (shim == LC_SHIM_HAT || shim == LC_SHIM_AMIGA_HAT || shim == LC_CAP_SWOOSH) return 42;
    if (hz < 1.f) return (shim == LC_SHIM_KICK || shim == LC_SHIM_AMIGA_KICK) ? 36 : 60;
    k = 69.0 + 12.0 * log((double)hz / 440.0) / log(2.0);
    ik = (k >= 0.0) ? (int)(k + 0.5) : (int)(k - 0.5);
    if (ik < 0) ik = 0;
    if (ik > 127) ik = 127;
    return ik;
}

static int vel_of(int shim, int art, float vol)
{
    double u = ((double)vol - 0.08) / 0.37;
    int vel;
    if (u < 0.0) u = 0.0;
    if (u > 1.0) u = 1.0;
    vel = (int)(u * 127.0 + 0.5);
    if (vel < 1) vel = 1;
    if (vel > 127) vel = 127;
    if (shim == LC_SHIM_SNARE || shim == LC_SHIM_IND_SNARE || shim == LC_SHIM_AMIGA_SNARE) {
        if (art == LC_CAP_MARK) {
            if (vel < 110) vel = 110;
        } else if (vel > 99) vel = 99;
    }
    if (shim == LC_SHIM_HAT || shim == LC_SHIM_AMIGA_HAT || shim == LC_CAP_SWOOSH) {
        if (art == LC_CAP_MARK) {
            if (vel < 100) vel = 100;
        } else if (vel > 99) vel = 99;
    }
    return vel;
}

static int map_shim(int shim, int art, int *gm, int *muted, const char **name)
{
    int phrase = (shim == LC_SHIM_DANCE && art == LC_CAP_PHRASE);
    *muted = 0;
    if (shim == LC_SHIM_DANCE && phrase) {
        *gm = 4;
        *name = "Blues run";
        return 1;
    }
    if (shim == LC_SHIM_DANCE) {
        *gm = 102;
        *name = "Crystalline cascade";
        return 1;
    }
    switch (shim) {
    case LC_SHIM_SQUASH: *gm = 80; *name = "Singing harmonic triad"; return 1;
    case LC_SHIM_FROG: *gm = 103; *name = "Acoustic frog croak"; return 1;
    case LC_SHIM_ADD_BELL: *gm = 98; *name = "Golden chi bell"; return 1;
    case LC_SHIM_WADDLE: *gm = 32; *name = "Crystal step"; return 1;
    case LC_SHIM_FLOAT: *gm = 4; *name = "Glass harp"; return 1;
    case LC_SHIM_DING: *gm = 112; *name = "Crystal chime"; return 1;
    case LC_SHIM_KICK: *gm = 116; *name = "Jazz kick"; return 1;
    case LC_SHIM_SNARE: *gm = 117; *name = "Snare"; return 1;
    case LC_SHIM_HAT: *gm = 118; *name = "Hi-hat"; return 1;
    case LC_SHIM_GUITAR: *gm = 26; *name = "Watanabe guitar"; return 1;
    case LC_SHIM_PIANO: *gm = 0; *name = "Concert grand piano"; return 1;
    case LC_SHIM_PEDAL: *gm = 38; *name = "Pedal bass"; return 1;
    case LC_SHIM_SAX: *gm = 52; *name = "Jazz woodwind"; return 1;
    case LC_SHIM_IND_KICK: *gm = 119; *name = "Industrial kick"; return 1;
    case LC_SHIM_IND_SNARE: *gm = 117; *name = "Cyber clank"; return 1;
    case LC_SHIM_IND_BASS: *gm = 33; *name = "Saw bass"; return 1;
    case LC_SHIM_SWARM: *gm = 90; *name = "Swarmatron"; return 1;
    case LC_SHIM_ZAP: *gm = 103; *name = "Glitch zap"; return 1;
    case LC_SHIM_CYBER: *gm = 56; *name = "Cyber lead"; return 1;
    case LC_SHIM_VIOLIN: *gm = 40; *name = "Violin"; return 1;
    case LC_SHIM_CELLO: *gm = 42; *name = "Cello"; return 1;
    case LC_SHIM_TIMPANI: *gm = 47; *name = "Timpani"; return 1;
    case LC_SHIM_AMIGA_KICK: *gm = 119; *name = "Amiga Paula kick"; return 1;
    case LC_SHIM_AMIGA_SNARE: *gm = 117; *name = "Amiga Paula snare"; return 1;
    case LC_SHIM_AMIGA_HAT: *gm = 118; *name = "Amiga Paula hi-hat"; return 1;
    case LC_SHIM_AMIGA_BASS: *gm = 33; *name = "Amiga saw bass"; return 1;
    case LC_SHIM_AMIGA_LEAD: *gm = 56; *name = "Amiga chiptune lead"; return 1;
    case LC_SHIM_AMIGA_CHORD: *gm = 90; *name = "Amiga chord stab"; return 1;
    case LC_SHIM_AMIGA_ARP: *gm = 80; *name = "Amiga protracker arp"; return 1;
    case LC_SHIM_AMIGA_BELL: *gm = 112; *name = "Amiga chime"; return 1;
    case LC_CAP_HAMMER: *gm = 117; *muted = 1; *name = "Piano hammer"; return 1;
    case LC_CAP_SWOOSH: *gm = 118; *muted = 1; *name = "Pedal swoosh"; return 1;
    default: return 0;
    }
}

static int group_of(int shim, int phrase, int gm, int muted, const char *name)
{
    int i;
    Grp *g;
    if (s_ng > GRP_MAX) return -1;
    for (i = 0; i < s_ng; i++) {
        if (s_grp[i].shim == shim && s_grp[i].phrase == phrase) return i;
    }
    if (s_ng >= GRP_MAX) return -1;
    g = &s_grp[s_ng];
    memset(g, 0, sizeof *g);
    g->shim = shim;
    g->phrase = phrase;
    g->gm = gm;
    g->muted = muted;
    g->name = name;
    s_ng++;
    return s_ng - 1;
}

static int capture(int track, int bpm, int den)
{
    int n;
    int i;
    s_ng = 0;
    s_nn = 0;
    s_merges = 0;
    s_worst = 0.0;
    s_clock_bad = 0;
    lc_cap_begin();
    if (track == 5) {
        lukhash_score_schedule(0.0);
    } else {
        lc_score_schedule(track, 0.0);
    }
    lc_cap_end();
    if (lc_cap_overflow()) {
        fprintf(stderr, "capture overflow on track %d\n", track);
        return 0;
    }
    n = lc_cap_count();
    if (n <= 0 || n > ORCH_MAX_EVENTS) {
        fprintf(stderr, "capture count %d on track %d\n", n, track);
        return 0;
    }
    for (i = 0; i < n; i++) {
        int shim = 0;
        int art = 0;
        int gm = 0;
        int muted = 0;
        int phrase;
        int gi;
        double t = 0.0;
        float hz = 0.f;
        float vol = 0.f;
        float dur = 0.f;
        const char *name = 0;
        Nv *e;
        if (!lc_cap_at(i, &shim, &art, &t, &hz, &vol, &dur)) return 0;
        if (!map_shim(shim, art, &gm, &muted, &name)) {
            fprintf(stderr, "unknown shim %d\n", shim);
            return 0;
        }
        if (shim == LC_SHIM_FLOAT && dur < 0.50f) {
            fprintf(stderr, "float dur %.3f is in the dance-phrase range\n", dur);
            return 0;
        }
        if (shim == LC_SHIM_DANCE && art == LC_CAP_PHRASE && dur >= 0.50f) {
            fprintf(stderr, "dance phrase dur %.3f is in the glass range\n", dur);
            return 0;
        }
        phrase = (shim == LC_SHIM_DANCE && art == LC_CAP_PHRASE) ? 1 : 0;
        gi = group_of(shim, phrase, gm, muted, name);
        if (gi < 0) {
            fprintf(stderr, "too many groups\n");
            return 0;
        }
        e = &s_nv[s_nn];
        e->shim = shim;
        e->art = art;
        e->gi = gi;
        e->lane = 0;
        e->ch = 0;
        e->ord = i;
        e->key = (uint8_t)key_of(shim, hz);
        /* Capture stores the partial peak 0.11*volMul. Velocity carries volMul. */
        if (shim == LC_SHIM_FLOAT && vol > 0.001f) vol = vol / 0.11f;
        e->vel = (uint8_t)vel_of(shim, art, vol);
        e->start = ticks_of(t, bpm, den, 0);
        e->dur = ticks_of((double)dur, bpm, den, 1);
        note_clock(e->start, t, bpm, den, shim);
        s_nn++;
    }
    if (s_clock_bad) {
        fprintf(stderr, "clock shim %d sec %.6f tick %u err %.1f samples\n",
                s_bad_shim, s_bad_sec, s_bad_tick, s_worst);
        return 0;
    }
    return 1;
}

static int cmp_nv(const void *a, const void *b)
{
    const Nv *x = (const Nv *)a;
    const Nv *y = (const Nv *)b;
    if (x->start < y->start) return -1;
    if (x->start > y->start) return 1;
    if (x->ord < y->ord) return -1;
    if (x->ord > y->ord) return 1;
    return 0;
}

static void place_one(Grp *g, Nv *e)
{
    int L;
    if (g->gm == 0) {
        e->lane = 0;
        if (g->nlane < 1) g->nlane = 1;
        return;
    }
    for (L = 0; L < g->nlane && L < 8; L++) {
        if (g->lend[L] <= e->start) {
            e->lane = L;
            if (e->start + e->dur > g->lend[L]) g->lend[L] = e->start + e->dur;
            return;
        }
    }
    if (g->nlane < 8) {
        e->lane = g->nlane;
        g->lend[g->nlane] = e->start + e->dur;
        g->nlane++;
        return;
    }
    e->lane = 0;
    s_merges++;
    if (e->start + e->dur > g->lend[0]) g->lend[0] = e->start + e->dur;
}

static void place_all(void)
{
    int i;
    for (i = 0; i < s_nn; i++) {
        int gi = s_nv[i].gi;
        if (gi < 0 || gi >= s_ng) continue;
        place_one(&s_grp[gi], &s_nv[i]);
    }
}

static int track_count(void)
{
    int n = 0;
    int g;
    for (g = 0; g < s_ng; g++) n += s_grp[g].nlane;
    return n;
}

static int track_of(int gi, int lane)
{
    int t = 0;
    int g;
    for (g = 0; g < gi && g < s_ng; g++) t += s_grp[g].nlane;
    return t + lane;
}

static int assign_channels(void)
{
    int gmch[128];
    int next = 1;
    int shared = 0;
    int g;
    int L;
    int i;
    for (i = 0; i < 128; i++) gmch[i] = 0;
    for (g = 0; g < s_ng; g++) {
        for (L = 0; L < s_grp[g].nlane && L < 8; L++) {
            int gm = s_grp[g].gm;
            int ch;
            if (next <= 16) {
                ch = next++;
            } else if (L == 0) {
                ch = (gm >= 0 && gm < 128) ? gmch[gm] : 0;
                if (ch < 1) ch = 1;
                shared++;
                fprintf(stderr, "share %s gm %d -> ch %d\n", s_grp[g].name, gm, ch);
            } else {
                ch = s_grp[g].lch[0];
                shared++;
            }
            if (ch < 1) ch = 1;
            if (ch > 16) ch = 16;
            s_grp[g].lch[L] = ch;
            if (gm >= 0 && gm < 128 && gmch[gm] == 0) gmch[gm] = ch;
        }
    }
    for (i = 0; i < s_nn; i++) {
        int gi = s_nv[i].gi;
        int lane = s_nv[i].lane;
        if (gi < 0 || gi >= s_ng || lane < 0 || lane > 7) return -1;
        s_nv[i].ch = s_grp[gi].lch[lane];
    }
    return shared;
}

static void trim_keys(void)
{
    int i;
    int j;
    for (i = 0; i < s_nn; i++) {
        uint32_t end = s_nv[i].start + s_nv[i].dur;
        for (j = i + 1; j < s_nn; j++) {
            if (s_nv[j].ch != s_nv[i].ch || s_nv[j].key != s_nv[i].key) continue;
            if (s_nv[j].start <= s_nv[i].start) continue;
            if (s_nv[j].start < end) end = s_nv[j].start;
        }
        s_nv[i].dur = (end > s_nv[i].start) ? (end - s_nv[i].start) : 1u;
        if (s_nv[i].dur < 1u) s_nv[i].dur = 1u;
    }
}

static int take_ins(int *used, int id)
{
    if (id < 1 || id > 8) return 0;
    if (used[id] >= 8) return 0;
    used[id]++;
    return 1;
}

static void set4(int16_t *p, int a, int b, int c, int d)
{
    p[0] = (int16_t)a;
    p[1] = (int16_t)b;
    p[2] = (int16_t)c;
    p[3] = (int16_t)d;
}

static void eq_set(int16_t *p, int gi)
{
    int freq = 180 + gi * 220;
    if (freq > 8000) freq = 8000;
    set4(p, gi % 4, freq, 120, (gi % 2) ? -3 : 2);
}

static void dress(const Grp *g, int gi, uint8_t *i0, uint8_t *i1,
                  int16_t *p0, int16_t *p1, int *used)
{
    int gm = g->gm;
    *i0 = 0;
    *i1 = 0;
    if (gm == 116 || gm == 117 || gm == 118 || gm == 119) {
        if (take_ins(used, ORCH_INS_GATE)) {
            *i0 = ORCH_INS_GATE;
            set4(p0, 10, 40, 30, 0);
        }
        return;
    }
    if (gm == 33 || gm == 38) {
        if (take_ins(used, ORCH_INS_SAT)) {
            *i0 = ORCH_INS_SAT;
            set4(p0, 22, 0, 0, 0);
        }
        if (take_ins(used, ORCH_INS_EQ)) {
            *i1 = ORCH_INS_EQ;
            eq_set(p1, gi);
        }
        return;
    }
    if (gm == 26 || gm == 52 || gm == 56) {
        if (take_ins(used, ORCH_INS_EQ)) {
            *i0 = ORCH_INS_EQ;
            eq_set(p0, gi);
        }
        if (take_ins(used, ORCH_INS_DELAY)) {
            *i1 = ORCH_INS_DELAY;
            set4(p1, 4, 28, 22, 0);
        }
        return;
    }
    if (gm == 0 || gm == 4 || gm == 32) {
        if (take_ins(used, ORCH_INS_EQ)) {
            *i0 = ORCH_INS_EQ;
            eq_set(p0, gi);
        }
        if (take_ins(used, ORCH_INS_CHORUS)) {
            *i1 = ORCH_INS_CHORUS;
            set4(p1, 16, 3, 6, 20);
        }
        return;
    }
    if (gm == 98 || gm == 112 || gm == 80 || gm == 102) {
        if (take_ins(used, ORCH_INS_EQ)) {
            *i0 = ORCH_INS_EQ;
            eq_set(p0, gi);
        }
        if (take_ins(used, ORCH_INS_REVERB)) {
            *i1 = ORCH_INS_REVERB;
            set4(p1, 70, 45, 25, 30);
        }
        return;
    }
    if (gm == 90) {
        if (take_ins(used, ORCH_INS_EQ)) {
            *i0 = ORCH_INS_EQ;
            eq_set(p0, gi);
        }
        return;
    }
    if (gm == 103 && take_ins(used, ORCH_INS_SAT)) {
        *i0 = ORCH_INS_SAT;
        set4(p0, 22, 0, 0, 0);
    }
}

static int drum_gm(int gm)
{
    return gm == 116 || gm == 117 || gm == 118 || gm == 119;
}

static void lane_name(char *dst, const char *base, int lane)
{
    int i = 0;
    int room = (lane > 0) ? 45 : 47;
    if (!base) base = "Track";
    while (base[i] && i < room) {
        dst[i] = base[i];
        i++;
    }
    dst[i] = 0;
    if (lane > 0 && i < 45) {
        dst[i++] = ' ';
        dst[i++] = (char)('0' + lane + 1);
        dst[i] = 0;
    }
}

static void pack_tracks(int *used, uint8_t pr[16], uint8_t basev[16], uint8_t seen[16])
{
    int g;
    int L;
    for (g = 0; g < s_ng; g++) {
        for (L = 0; L < s_grp[g].nlane && L < 8; L++) {
            int ti = track_of(g, L);
            uint8_t *d;
            uint8_t i0 = 0;
            uint8_t i1 = 0;
            int16_t p0[4];
            int16_t p1[4];
            int vol;
            int pan;
            int send;
            int ch;
            int k;
            char name[48];
            if (ti < 0 || ti >= 64) continue;
            d = s_file + OFF_TRK + ti * TRK_N;
            memset(p0, 0, sizeof p0);
            memset(p1, 0, sizeof p1);
            if (L == 0) dress(&s_grp[g], g, &i0, &i1, p0, p1, used);
            vol = s_grp[g].muted ? 72 : (drum_gm(s_grp[g].gm) ? 108 : 100);
            pan = 32 + ((g * 9 + L * 11) % 65);
            send = 24 + (g % 17);
            ch = s_grp[g].lch[L];
            lane_name(name, s_grp[g].name, L);
            put_name(d, ORCH_NAME, name);
            d[48] = (uint8_t)ch;
            d[51] = 0;
            d[52] = 127;
            d[57] = (uint8_t)vol;
            d[58] = (uint8_t)pan;
            d[59] = (uint8_t)send;
            d[60] = (uint8_t)s_grp[g].gm;
            d[61] = i0;
            d[62] = i1;
            for (k = 0; k < 4; k++) {
                wr16(d, 64 + k * 2, p0[k]);
                wr16(d, 72 + k * 2, p1[k]);
            }
            wr32(d, 80, COLS[g & 7]);
            d[84] = 1;
            d[55] = s_grp[g].muted ? 1u : 0u;
            if (ch >= 1 && ch <= 16 && !seen[ch - 1]) {
                seen[ch - 1] = 1;
                pr[ch - 1] = (uint8_t)s_grp[g].gm;
                basev[ch - 1] = (uint8_t)vol;
            }
        }
    }
}

static void pack_scenes(int bpm, int num, int den, const char *title, const char *stem,
                        const uint8_t pr[16], const uint8_t basev[16], const uint8_t seen[16])
{
    int s;
    for (s = 0; s < 16; s++) {
        uint8_t *d = s_file + OFF_SCN + s * SCN_N;
        int c;
        const char *nm = (s == 0) ? title : SCN_NAME[s - 1];
        put_name(d, ORCH_SCENE_NAME, nm);
        wr16(d, 32, bpm);
        d[34] = (uint8_t)num;
        d[35] = (uint8_t)den;
        for (c = 0; c < 16; c++) {
            int vol = 0;
            d[36 + c] = 255;
            d[52 + c] = 0;
            if (!seen[c]) continue;
            d[36 + c] = pr[c];
            vol = (basev[c] * SCN_SCALE[s] + 50) / 100;
            if (vol < 0) vol = 0;
            if (vol > 127) vol = 127;
            d[52 + c] = (uint8_t)vol;
        }
        put_name(d + 68, ORCH_CUE, stem);
    }
}

static void pack_events(void)
{
    int i;
    for (i = 0; i < s_nn && i < ORCH_MAX_EVENTS; i++) {
        uint8_t *d = s_file + OFF_EV + i * EV_N;
        int tr = track_of(s_nv[i].gi, s_nv[i].lane);
        d[0] = (uint8_t)tr;
        d[1] = s_nv[i].key;
        d[2] = s_nv[i].vel;
        d[3] = 0;
        wr32(d, 4, s_nv[i].start);
        wr32(d, 8, s_nv[i].dur);
        wr32(d, 12, 0);
    }
}

static int pack(int bpm, int num, int den, const char *title, const char *stem, int ntrk)
{
    int used[9];
    uint8_t pr[16];
    uint8_t basev[16];
    uint8_t seen[16];
    int i;
    const char *lit = ORCH_PATH_ORCH;
    memset(s_file, 0, sizeof s_file);
    memset(used, 0, sizeof used);
    memset(pr, 255, sizeof pr);
    memset(basev, 0, sizeof basev);
    memset(seen, 0, sizeof seen);
    s_file[0] = 'O';
    s_file[1] = 'R';
    s_file[2] = 'C';
    s_file[3] = 'H';
    wr16(s_file, 4, 1);
    wr16(s_file, 6, bpm);
    wr32(s_file, 8, 48000u);
    s_file[12] = (uint8_t)num;
    s_file[13] = (uint8_t)den;
    s_file[14] = ORCH_FAM_CLASSIC;
    s_file[15] = 0;
    s_file[16] = 0;
    s_file[17] = (uint8_t)ntrk;
    s_file[18] = 16;
    s_file[19] = 100;
    s_file[20] = ORCH_INS_LIMITER;
    s_file[23] = ORCH_SNAP_16TH;
    wr32(s_file, 24, (uint32_t)s_nn);
    s_file[28] = 42;
    s_file[29] = 55;
    s_file[30] = 18;
    s_file[31] = 28;
    for (i = 0; lit[i] != 0 && i < 95; i++) s_file[32 + i] = (uint8_t)lit[i];
    pack_tracks(used, pr, basev, seen);
    pack_scenes(bpm, num, den, title, stem, pr, basev, seen);
    pack_events();
    return 1;
}

static int write_exact(const char *path, const uint8_t *b, uint32_t n)
{
    FILE *fp = fopen(path, "wb");
    size_t w;
    int closed;
    if (!fp) return 0;
    w = fwrite(b, 1, n, fp);
    closed = fclose(fp);
    return closed == 0 && w == (size_t)n;
}

static int copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    FILE *out;
    uint8_t b[4096];
    int i;
    if (!in) return 0;
    out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return 0;
    }
    for (i = 0; i < 10000; i++) {
        size_t n = fread(b, 1, sizeof b, in);
        if (n == 0) break;
        if (fwrite(b, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            return 0;
        }
    }
    fclose(in);
    return fclose(out) == 0;
}

static int piece(int track, int bpm, int num, int den, const char *title, const char *stem)
{
    char orch[64];
    char mid[64];
    int ntrk;
    int shared;
    int ev0;
    int ev1;
    OrchView *v;
    snprintf(orch, sizeof orch, "SYS/%s.orch", stem);
    snprintf(mid, sizeof mid, "SYS/%s.mid", stem);
    if (!capture(track, bpm, den)) return 0;
    qsort(s_nv, (size_t)s_nn, sizeof s_nv[0], cmp_nv);
    place_all();
    ntrk = track_count();
    if (ntrk < 1 || ntrk > 64) {
        fprintf(stderr, "%s track count %d\n", stem, ntrk);
        return 0;
    }
    shared = assign_channels();
    if (shared < 0) return 0;
    trim_keys();
    pack(bpm, num, den, title, stem, ntrk);
    if (!write_exact(orch, s_file, ORCH_FILE_BYTES)) {
        fprintf(stderr, "write %s failed\n", orch);
        return 0;
    }
    if (!orch_session_load_buf(s_file, ORCH_FILE_BYTES)) {
        v = gst_view();
        fprintf(stderr, "%s load refused status=%s refuse=%u\n",
                stem, v ? v->status : "", v ? v->refuse_session : 0u);
        return 0;
    }
    v = gst_view();
    ev0 = v ? (int)v->event_count : 0;
    if (ev0 <= 0 || !orch_cmd_smf_save()) {
        fprintf(stderr, "%s smf save failed ev=%d\n", stem, ev0);
        return 0;
    }
    if (!copy_file("SYS/orchestra.mid", mid)) {
        fprintf(stderr, "copy mid %s failed\n", mid);
        return 0;
    }
    if (!orch_cmd_smf_load()) {
        fprintf(stderr, "%s smf load failed\n", stem);
        return 0;
    }
    v = gst_view();
    ev1 = v ? (int)v->event_count : 0;
    printf("%s notes=%d tracks=%d shared=%d merges=%d orch=%d smf=%d clock=%.1f\n",
           stem, s_nn, ntrk, shared, s_merges, ev0, ev1, s_worst);
    if (ev1 <= 0) return 0;
    if (shared == 0 && ev0 != ev1) {
        fprintf(stderr, "%s event count %d -> %d\n", stem, ev0, ev1);
        return 0;
    }
    return 1;
}

static int at_repo(void)
{
    struct stat st;
    if (stat("SYS", &st) == 0 && S_ISDIR(st.st_mode)) return 1;
    if (chdir("/Users/tonpa/depot/bitedits/btron") != 0) return 0;
    return stat("SYS", &st) == 0 && S_ISDIR(st.st_mode);
}

int main(void)
{
    if (!at_repo()) {
        fprintf(stderr, "run from the btron repo\n");
        return 1;
    }
    if (!piece(1, 168, 3, 4, "Carol of the Bells", "bells")) return 1;
    if (!piece(2, 120, 4, 4, "Green Caterpillar", "caterpillar")) return 1;
    if (!piece(3, 125, 3, 4, "House of the Rising Sun", "rising")) return 1;
    if (!piece(4, 93, 4, 4, "TRON Init", "tron")) return 1;
    if (!piece(5, 112, 4, 4, "LukHash (Amiga Outtake)", "lukhash")) return 1;
    if (!copy_file("SYS/bells.orch", "SYS/orchestra.orch")) return 1;
    if (!copy_file("SYS/bells.mid", "SYS/orchestra.mid")) return 1;
    printf("desk SYS/orchestra.orch + SYS/orchestra.mid = bells\n");
    return 0;
}
