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
}

void wa_stop_all(void) {
    for (int i = 0; i < WA_MAX_NODES; i++) s_nodes[i].used = 0;
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

void wa_render(float *mono, size_t frames) {
    size_t off = 0;
    while (off < frames) {
        size_t n = frames - off;
        if (n > WA_CTRL) n = WA_CTRL;
        render_block(mono + off, n);
        s_clock += (int64_t)n;
        off += n;
    }
}
