/*
 * src/demo/lilcu64_wa.c — WebAudio-equivalent scheduled node engine
 *
 * See lilcu64_wa.h. AudioParams are evaluated at a 32-sample control rate
 * (0.73 ms) with per-sample linear interpolation; oscillator start/stop are
 * sample-accurate against the absolute audio clock.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "lilcu64_wa.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif

#define WA_MAX_NODES 4096
#define WA_MAX_GEV   24   /* frog croak pulse train needs 20 gain events */
#define WA_MAX_FEV   8
#define WA_MAX_QEV   8
#define WA_CTRL      32   /* control-rate block (samples) */

typedef struct {
    float   t;      /* seconds, relative to node start */
    float   v;
    uint8_t type;
} WaEv;

struct WaNode {
    uint8_t used;
    uint8_t type;
    uint8_t ftype;
    uint8_t ngev, nfev, nqev;
    uint8_t coeff_ok;
    double  t_start;
    int64_t s_start, s_stop;
    float   phase;
    WaEv    gev[WA_MAX_GEV];
    WaEv    fev[WA_MAX_FEV];
    WaEv    qev[WA_MAX_QEV];
    float   ffreq, q;
    float   b0, b1, b2, a1, a2, z1, z2;
};

static WaNode   s_nodes[WA_MAX_NODES];
static uint32_t s_sr = 44100;
static int64_t  s_clock = 0;

/* Shared 2-second white noise buffer (getNoiseBuffer() in index.html) */
#define WA_NOISE_LEN (44100 * 2)
static float s_noise[WA_NOISE_LEN];

void wa_init(uint32_t sample_rate) {
    s_sr = sample_rate ? sample_rate : 44100;
    memset(s_nodes, 0, sizeof(s_nodes));
    s_clock = 0;
    uint32_t x = 0x2545F491u;
    for (int i = 0; i < WA_NOISE_LEN; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        s_noise[i] = ((float)(x & 0xFFFFFF) / 8388607.5f) - 1.0f;
    }
    reverb_init();
}

void wa_stop_all(void) {
    for (int i = 0; i < WA_MAX_NODES; i++) s_nodes[i].used = 0;
    reverb_clear();
}

double wa_time(void) {
    return (double)s_clock / (double)s_sr;
}

int wa_active_count(void) {
    int c = 0;
    for (int i = 0; i < WA_MAX_NODES; i++) c += s_nodes[i].used;
    return c;
}

WaNode *wa_osc(int type, double t_start, double t_stop) {
    if (t_stop <= t_start) return NULL;
    for (int i = 0; i < WA_MAX_NODES; i++) {
        WaNode *n = &s_nodes[i];
        if (n->used) continue;
        memset(n, 0, sizeof(*n));
        n->used    = 1;
        n->type    = (uint8_t)type;
        n->t_start = t_start;
        n->s_start = (int64_t)llround(t_start * (double)s_sr);
        n->s_stop  = (int64_t)llround(t_stop * (double)s_sr);
        n->ffreq   = 350.0f;
        n->q       = 1.0f;
        return n;
    }
    return NULL;
}

/* Insert keeping events sorted by time (stable, like the WebAudio timeline). */
static void ev_insert(WaEv *ev, uint8_t *count, int max, int type, float v, float t) {
    if (*count >= max) return;
    int i = *count;
    while (i > 0 && ev[i - 1].t > t) { ev[i] = ev[i - 1]; i--; }
    ev[i].t = t; ev[i].v = v; ev[i].type = (uint8_t)type;
    (*count)++;
}

void wa_freq(WaNode *n, int ev, float value, double t) {
    if (n) ev_insert(n->fev, &n->nfev, WA_MAX_FEV, ev, value, (float)(t - n->t_start));
}

void wa_gain(WaNode *n, int ev, float value, double t) {
    if (n) ev_insert(n->gev, &n->ngev, WA_MAX_GEV, ev, value, (float)(t - n->t_start));
}

void wa_filter(WaNode *n, int type, float freq, float q) {
    if (!n) return;
    n->ftype = (uint8_t)type;
    n->ffreq = freq;
    n->q = q;
    n->coeff_ok = 0;
}

void wa_filter_freq(WaNode *n, int ev, float value, double t) {
    if (n) ev_insert(n->qev, &n->nqev, WA_MAX_QEV, ev, value, (float)(t - n->t_start));
}

/* AudioParam value at time t (WebAudio timeline semantics). */
static float ev_eval(const WaEv *ev, int n, float t, float defv) {
    if (n == 0) return defv;
    if (t < ev[0].t) return ev[0].v;
    int i = 0;
    while (i + 1 < n && ev[i + 1].t <= t) i++;
    if (i == n - 1) return ev[i].v;
    const WaEv *a = &ev[i], *b = &ev[i + 1];
    float span = b->t - a->t;
    float x = (span > 0.0f) ? (t - a->t) / span : 1.0f;
    if (b->type == WA_LIN) return a->v + (b->v - a->v) * x;
    if (b->type == WA_EXP) {
        if (a->v == 0.0f || b->v == 0.0f || (a->v > 0.0f) != (b->v > 0.0f)) return a->v;
        return a->v * powf(b->v / a->v, x);
    }
    return a->v; /* next is a setValueAtTime: hold */
}

/* RBJ biquad with WebAudio Q semantics (Q in dB for LP/HP, linear for BP). */
static void biquad_coeffs(WaNode *n, float freq) {
    float nyq = 0.5f * (float)s_sr;
    if (freq < 10.0f) freq = 10.0f;
    if (freq > nyq * 0.99f) freq = nyq * 0.99f;
    float w0 = 2.0f * (float)M_PI * freq / (float)s_sr;
    float cw = cosf(w0), sw = sinf(w0);
    float alpha, b0, b1, b2, a0, a1, a2;
    if (n->ftype == WA_FILT_BANDPASS) {
        float q = n->q > 0.0001f ? n->q : 0.0001f;
        alpha = sw / (2.0f * q);
        b0 = alpha; b1 = 0.0f; b2 = -alpha;
    } else {
        alpha = sw / (2.0f * powf(10.0f, n->q / 20.0f));
        if (n->ftype == WA_FILT_LOWPASS) {
            b0 = (1.0f - cw) * 0.5f; b1 = 1.0f - cw; b2 = b0;
        } else {
            b0 = (1.0f + cw) * 0.5f; b1 = -(1.0f + cw); b2 = b0;
        }
    }
    a0 = 1.0f + alpha; a1 = -2.0f * cw; a2 = 1.0f - alpha;
    n->b0 = b0 / a0; n->b1 = b1 / a0; n->b2 = b2 / a0;
    n->a1 = a1 / a0; n->a2 = a2 / a0;
}

static inline float osc_sample(int type, float p) {
    switch (type) {
        case WA_SINE:     return sinf(2.0f * (float)M_PI * p);
        case WA_TRIANGLE: return (p < 0.25f) ? 4.0f * p : (p < 0.75f) ? 2.0f - 4.0f * p : 4.0f * p - 4.0f;
        case WA_SAWTOOTH: return (p < 0.5f) ? 2.0f * p : 2.0f * p - 2.0f;
        case WA_SQUARE:   return (p < 0.5f) ? 1.0f : -1.0f;
        default:          return 0.0f;
    }
}


/* ── Deep Industrial Reverb & 93 BPM Feedback Delay Engine ────────── */

#define WA_REVERB_COMBS   4
#define WA_REVERB_APS     4

typedef struct {
    float buf[2048];
    int   size;
    int   idx;
    float store;
} WaLbcf;

typedef struct {
    float buf[1024];
    int   size;
    int   idx;
} WaAllpass;

typedef struct {
    int       enabled;
    float     room_size;
    float     damping;
    float     wet;
    float     feedback;

    WaLbcf    combs_l[WA_REVERB_COMBS];
    WaLbcf    combs_r[WA_REVERB_COMBS];
    WaAllpass aps_l[WA_REVERB_APS];
    WaAllpass aps_r[WA_REVERB_APS];

    float     delay_buf_l[44100];
    float     delay_buf_r[44100];
    int       delay_size;
    int       delay_idx;
    float     delay_store_l;
    float     delay_store_r;
} WaReverb;

static WaReverb s_reverb;

static void reverb_init(void) {
    memset(&s_reverb, 0, sizeof(s_reverb));
    s_reverb.delay_size = (int)s_sr;
    if (s_reverb.delay_size > 44100) s_reverb.delay_size = 44100;

    float scale = (float)s_sr / 44100.0f;
    static const int c_l_defs[4] = { 1557, 1491, 1277, 1188 };
    static const int c_r_defs[4] = { 1617, 1422, 1356, 1116 };
    static const int ap_defs[4]  = { 556,  441,  341,  225 };

    for (int i = 0; i < WA_REVERB_COMBS; i++) {
        int sz_l = (int)(c_l_defs[i] * scale);
        if (sz_l > 2047) sz_l = 2047;
        s_reverb.combs_l[i].size = sz_l;

        int sz_r = (int)(c_r_defs[i] * scale);
        if (sz_r > 2047) sz_r = 2047;
        s_reverb.combs_r[i].size = sz_r;
    }
    for (int i = 0; i < WA_REVERB_APS; i++) {
        int sz_l = (int)(ap_defs[i] * scale);
        if (sz_l > 1023) sz_l = 1023;
        s_reverb.aps_l[i].size = sz_l;

        int sz_r = (int)((ap_defs[i] + 23) * scale);
        if (sz_r > 1023) sz_r = 1023;
        s_reverb.aps_r[i].size = sz_r;
    }
}

static void reverb_clear(void) {
    for (int i = 0; i < WA_REVERB_COMBS; i++) {
        memset(s_reverb.combs_l[i].buf, 0, sizeof(s_reverb.combs_l[i].buf));
        memset(s_reverb.combs_r[i].buf, 0, sizeof(s_reverb.combs_r[i].buf));
        s_reverb.combs_l[i].idx = 0; s_reverb.combs_l[i].store = 0.0f;
        s_reverb.combs_r[i].idx = 0; s_reverb.combs_r[i].store = 0.0f;
    }
    for (int i = 0; i < WA_REVERB_APS; i++) {
        memset(s_reverb.aps_l[i].buf, 0, sizeof(s_reverb.aps_l[i].buf));
        memset(s_reverb.aps_r[i].buf, 0, sizeof(s_reverb.aps_r[i].buf));
        s_reverb.aps_l[i].idx = 0;
        s_reverb.aps_r[i].idx = 0;
    }
    memset(s_reverb.delay_buf_l, 0, sizeof(s_reverb.delay_buf_l));
    memset(s_reverb.delay_buf_r, 0, sizeof(s_reverb.delay_buf_r));
    s_reverb.delay_idx = 0;
    s_reverb.delay_store_l = 0.0f;
    s_reverb.delay_store_r = 0.0f;
}

void wa_set_reverb(int enabled, float room_size, float damping, float feedback, float wet_mix) {
    s_reverb.enabled   = enabled;
    s_reverb.room_size = room_size;
    s_reverb.damping   = damping;
    s_reverb.feedback  = feedback;
    s_reverb.wet       = wet_mix;
    if (!enabled) reverb_clear();
}

static inline void reverb_process(const float *dry, float *out_l, float *out_r, size_t n) {
    if (!s_reverb.enabled) {
        for (size_t i = 0; i < n; i++) {
            out_l[i] += dry[i];
            out_r[i] += dry[i];
        }
        return;
    }

    /* 93 BPM 16th note delay: 60 / (93 * 4) = 0.16129 s */
    int delay_tap_l = (int)(0.16129f * (float)s_sr);
    int delay_tap_r = (int)(0.24194f * (float)s_sr); /* dotted 16th */

    float fb   = s_reverb.feedback;
    float room = s_reverb.room_size;
    float damp = s_reverb.damping;
    float wet  = s_reverb.wet;
    int   d_sz = s_reverb.delay_size;

    for (size_t i = 0; i < n; i++) {
        float x = dry[i];

        /* 1. Dual-tap cross-feedback delay with damping */
        int r_l = s_reverb.delay_idx - delay_tap_l;
        if (r_l < 0) r_l += d_sz;
        int r_r = s_reverb.delay_idx - delay_tap_r;
        if (r_r < 0) r_r += d_sz;

        float dl = s_reverb.delay_buf_l[r_l];
        float dr = s_reverb.delay_buf_r[r_r];

        s_reverb.delay_store_l = dl * (1.0f - damp * 0.8f) + s_reverb.delay_store_l * (damp * 0.8f);
        s_reverb.delay_store_r = dr * (1.0f - damp * 0.8f) + s_reverb.delay_store_r * (damp * 0.8f);

        s_reverb.delay_buf_l[s_reverb.delay_idx] = x + s_reverb.delay_store_r * fb;
        s_reverb.delay_buf_r[s_reverb.delay_idx] = x + s_reverb.delay_store_l * fb;
        if (++s_reverb.delay_idx >= d_sz) s_reverb.delay_idx = 0;

        /* 2. Parallel comb filters with lowpass damping */
        float rev_in_l = x + dl * 0.35f;
        float rev_in_r = x + dr * 0.35f;

        float sm_l = 0.0f, sm_r = 0.0f;
        for (int c = 0; c < WA_REVERB_COMBS; c++) {
            WaLbcf *cl = &s_reverb.combs_l[c];
            float out_cl = cl->buf[cl->idx];
            cl->store = out_cl * (1.0f - damp) + cl->store * damp;
            cl->buf[cl->idx] = rev_in_l + cl->store * room;
            if (++cl->idx >= cl->size) cl->idx = 0;
            sm_l += out_cl;

            WaLbcf *cr = &s_reverb.combs_r[c];
            float out_cr = cr->buf[cr->idx];
            cr->store = out_cr * (1.0f - damp) + cr->store * damp;
            cr->buf[cr->idx] = rev_in_r + cr->store * room;
            if (++cr->idx >= cr->size) cr->idx = 0;
            sm_r += out_cr;
        }
        sm_l *= 0.25f;
        sm_r *= 0.25f;

        /* 3. Series allpass diffusers */
        for (int a = 0; a < WA_REVERB_APS; a++) {
            WaAllpass *al = &s_reverb.aps_l[a];
            float b_l = al->buf[al->idx];
            float o_l = -sm_l + b_l;
            al->buf[al->idx] = sm_l + b_l * 0.5f;
            if (++al->idx >= al->size) al->idx = 0;
            sm_l = o_l;

            WaAllpass *ar = &s_reverb.aps_r[a];
            float b_r = ar->buf[ar->idx];
            float o_r = -sm_r + b_r;
            ar->buf[ar->idx] = sm_r + b_r * 0.5f;
            if (++ar->idx >= ar->size) ar->idx = 0;
            sm_r = o_r;
        }

        /* Stereo output: dry + wet reverb + wet rhythmic feedback echoes */
        out_l[i] += x + wet * (sm_l + dl * 0.30f);
        out_r[i] += x + wet * (sm_r + dr * 0.30f);
    }
}

static void render_block(float *out, size_t n) {
    const int64_t b0 = s_clock, b1 = s_clock + (int64_t)n;
    const double  sr = (double)s_sr;

    for (int k = 0; k < WA_MAX_NODES; k++) {
        WaNode *nd = &s_nodes[k];
        if (!nd->used) continue;
        if (nd->s_stop <= b0) { nd->used = 0; continue; }
        if (nd->s_start >= b1) continue;

        int i0 = (nd->s_start > b0) ? (int)(nd->s_start - b0) : 0;
        int i1 = (nd->s_stop < b1) ? (int)(nd->s_stop - b0) : (int)n;
        float ta = (float)((double)(b0 + i0) / sr - nd->t_start);
        float tb = (float)((double)b1 / sr - nd->t_start);

        float g0 = ev_eval(nd->gev, nd->ngev, ta, 1.0f);
        float g1 = ev_eval(nd->gev, nd->ngev, tb, 1.0f);
        float f0 = ev_eval(nd->fev, nd->nfev, ta, 440.0f);
        float f1 = ev_eval(nd->fev, nd->nfev, tb, 440.0f);

        if (nd->ftype != WA_FILT_NONE) {
            if (nd->nqev > 0) biquad_coeffs(nd, ev_eval(nd->qev, nd->nqev, ta, nd->ffreq));
            else if (!nd->coeff_ok) { biquad_coeffs(nd, nd->ffreq); nd->coeff_ok = 1; }
        }

        float span = (float)((int)n - i0);
        float dg = (g1 - g0) / span;
        float df = (f1 - f0) / span;
        float g = g0, f = f0;
        float inv_sr = 1.0f / (float)s_sr;

        for (int i = i0; i < i1; i++) {
            float x;
            if (nd->type == WA_NOISE) {
                int64_t idx = b0 + i - nd->s_start;
                x = (idx < WA_NOISE_LEN) ? s_noise[idx] : 0.0f;
            } else {
                x = osc_sample(nd->type, nd->phase);
                nd->phase += f * inv_sr;
                nd->phase -= floorf(nd->phase);
            }
            if (nd->ftype != WA_FILT_NONE) {
                float y = nd->b0 * x + nd->z1;
                nd->z1 = nd->b1 * x - nd->a1 * y + nd->z2;
                nd->z2 = nd->b2 * x - nd->a2 * y;
                x = y;
            }
            out[i] += x * g;
            g += dg;
            f += df;
        }
        if (nd->s_stop <= b1) nd->used = 0;
    }
}

void wa_render_stereo(float *out_l, float *out_r, size_t frames) {
    float dry_block[WA_CTRL];
    size_t off = 0;
    while (off < frames) {
        size_t n = frames - off;
        if (n > WA_CTRL) n = WA_CTRL;
        memset(dry_block, 0, n * sizeof(float));
        render_block(dry_block, n);
        reverb_process(dry_block, out_l + off, out_r + off, n);
        s_clock += (int64_t)n;
        off += n;
    }
}

void wa_render(float *mono, size_t frames) {
    if (!s_reverb.enabled) {
        size_t off = 0;
        while (off < frames) {
            size_t n = frames - off;
            if (n > WA_CTRL) n = WA_CTRL;
            render_block(mono + off, n);
            s_clock += (int64_t)n;
            off += n;
        }
        return;
    }
    float out_l[WA_CTRL], out_r[WA_CTRL];
    size_t off = 0;
    while (off < frames) {
        size_t n = frames - off;
        if (n > WA_CTRL) n = WA_CTRL;
        memset(out_l, 0, n * sizeof(float));
        memset(out_r, 0, n * sizeof(float));
        wa_render_stereo(out_l, out_r, n);
        for (size_t i = 0; i < n; i++) {
            mono[off + i] += 0.5f * (out_l[i] + out_r[i]);
        }
        off += n;
    }
}
