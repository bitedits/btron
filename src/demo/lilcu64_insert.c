#include "lilcu64_insert.h"
#include "lilcu64_synth.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#if defined(BTRON_PS2_TARGET)
#define LC_DELAY_MAX 2048
#else
#define LC_DELAY_MAX 48000
#endif
#define LC_CHORUS_MAX 2400

typedef struct LcIns {
    float b0, b1, b2, a1, a2, z1, z2;
    int hz, qcent, gain, mode, ready;
    float env;
    int hold;
} LcIns;

static LcIns s_ins[LC_INS_MAX];
static float s_delay[LC_LINE_MAX][LC_DELAY_MAX];
static int s_dpos[LC_LINE_MAX];
static float s_chorus[LC_LINE_MAX][LC_CHORUS_MAX];
static int s_cpos[LC_LINE_MAX];
static LcIns s_ins_snap[LC_INS_MAX];
static float s_delay_snap[LC_LINE_MAX][LC_DELAY_MAX];
static int s_dpos_snap[LC_LINE_MAX];
static float s_chorus_snap[LC_LINE_MAX][LC_CHORUS_MAX];
static int s_cpos_snap[LC_LINE_MAX];
static int s_have_snap;

static int clamp_i(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void lc_insert_line_zero(int kind, int line)
{
    if (line < 0 || line >= LC_LINE_MAX) return;
    if (kind == 0) {
        memset(s_delay[line], 0, sizeof s_delay[line]);
        s_dpos[line] = 0;
    } else {
        memset(s_chorus[line], 0, sizeof s_chorus[line]);
        s_cpos[line] = 0;
    }
}

void lc_insert_reset(void)
{
    memset(s_ins, 0, sizeof s_ins);
    memset(s_delay, 0, sizeof s_delay);
    memset(s_dpos, 0, sizeof s_dpos);
    memset(s_chorus, 0, sizeof s_chorus);
    memset(s_cpos, 0, sizeof s_cpos);
}

void lc_insert_state_save(void)
{
    memcpy(s_ins_snap, s_ins, sizeof s_ins);
    memcpy(s_delay_snap, s_delay, sizeof s_delay);
    memcpy(s_dpos_snap, s_dpos, sizeof s_dpos);
    memcpy(s_chorus_snap, s_chorus, sizeof s_chorus);
    memcpy(s_cpos_snap, s_cpos, sizeof s_cpos);
    s_have_snap = 1;
}

void lc_insert_state_restore(void)
{
    if (!s_have_snap) return;
    memcpy(s_ins, s_ins_snap, sizeof s_ins);
    memcpy(s_delay, s_delay_snap, sizeof s_delay);
    memcpy(s_dpos, s_dpos_snap, sizeof s_dpos);
    memcpy(s_chorus, s_chorus_snap, sizeof s_chorus);
    memcpy(s_cpos, s_cpos_snap, sizeof s_cpos);
}

void lc_curve_build(float *dst, int k)
{
    int i;
    float kk;
    if (!dst) return;
    if (k < 0) k = 0;
    if (k > 100) k = 100;
    kk = (float)k;
    for (i = 0; i < 1024; i++) {
        float x = -1.0f + (2.0f * (float)i) / 1023.0f;
        float ax = x < 0.0f ? -x : x;
        float deg = 20.0f * (float)M_PI / 180.0f;
        dst[i] = ((3.0f + kk) * x * deg) / ((float)M_PI + kk * ax);
    }
}

float lc_curve_read(const float *curve, float x)
{
    float t;
    int i;
    if (!curve) return x;
    if (x < -1.0f) x = -1.0f;
    if (x > 1.0f) x = 1.0f;
    t = (x + 1.0f) * 511.5f;
    i = (int)t;
    if (i < 0) i = 0;
    if (i > 1023) i = 1023;
    return curve[i];
}

static void biquad_set(LcIns *s, int mode, float fs, float hz, float q, float gdb)
{
    float w0, a, c, a0, b0, b1, b2, a1, a2, A;
    if (hz < 20.0f) hz = 20.0f;
    if (hz > fs * 0.45f) hz = fs * 0.45f;
    if (q < 0.10f) q = 0.10f;
    if (q > 10.0f) q = 10.0f;
    w0 = 2.0f * (float)M_PI * hz / fs;
    a = sinf(w0) / (2.0f * q);
    c = cosf(w0);
    A = powf(10.0f, gdb / 40.0f);
    if (mode == 1) {
        b0 = (1.0f + c) * 0.5f; b1 = -(1.0f + c); b2 = b0; a0 = 1.0f + a; a2 = 1.0f - a;
    } else if (mode == 2) {
        b0 = a; b1 = 0.0f; b2 = -a; a0 = 1.0f + a; a2 = 1.0f - a;
    } else if (mode == 3) {
        b0 = 1.0f + a * A; b1 = -2.0f * c; b2 = 1.0f - a * A;
        a0 = 1.0f + a / A; a2 = 1.0f - a / A;
    } else {
        b0 = (1.0f - c) * 0.5f; b1 = 1.0f - c; b2 = b0; a0 = 1.0f + a; a2 = 1.0f - a;
    }
    a1 = -2.0f * c;
    s->b0 = b0 / a0; s->b1 = b1 / a0; s->b2 = b2 / a0;
    s->a1 = a1 / a0; s->a2 = a2 / a0;
    s->ready = 1;
}

static void apply_eq(LcIns *s, const int16_t *p, float *x, int n, uint32_t fs)
{
    int mode = clamp_i(p[0], 0, 3);
    int hz = clamp_i(p[1], 20, 20000);
    int qc = clamp_i(p[2], 10, 1000);
    int gdb = clamp_i(p[3], -24, 24);
    int i;
    if (!s->ready || s->mode != mode || s->hz != hz || s->qcent != qc || s->gain != gdb) {
        biquad_set(s, mode, (float)fs, (float)hz, (float)qc / 100.0f, (float)gdb);
        s->mode = mode; s->hz = hz; s->qcent = qc; s->gain = gdb;
    }
    for (i = 0; i < n; i++) {
        float y = s->b0 * x[i] + s->z1;
        s->z1 = s->b1 * x[i] - s->a1 * y + s->z2;
        s->z2 = s->b2 * x[i] - s->a2 * y;
        x[i] = y;
    }
}

static void apply_sat(const float *curve, float *x, int n)
{
    int i;
    for (i = 0; i < n; i++) x[i] = lc_curve_read(curve, x[i]);
}

static int delay_len(int k, uint32_t fs, uint32_t bpm, uint32_t den, int *clamped)
{
    uint64_t num, denm, L;
    uint32_t cap;
    if (bpm < 40) bpm = 40;
    if (den == 0) den = 4;
    k = clamp_i(k, 1, 16);
    num = (uint64_t)k * fs * 15u * den + (uint64_t)2u * bpm;
    denm = (uint64_t)4u * bpm;
    L = num / denm;
    cap = fs < (uint32_t)LC_DELAY_MAX ? fs : (uint32_t)LC_DELAY_MAX;
    *clamped = 0;
    if (L < 1) L = 1;
    if (L > cap) { L = cap; *clamped = 1; }
    return (int)L;
}

static int apply_delay(int line, const int16_t *p, float *x, int n,
                       uint32_t fs, uint32_t bpm, uint32_t den)
{
    int clamped = 0;
    int L, i, pos;
    float fb, wet;
    if (line < 0 || line >= LC_LINE_MAX) return 0;
    L = delay_len(p[0], fs, bpm, den, &clamped);
    fb = (float)clamp_i(p[1], 0, 90) / 100.0f;
    wet = (float)clamp_i(p[2], 0, 100) / 100.0f;
    pos = s_dpos[line];
    if (pos < 0 || pos >= LC_DELAY_MAX) pos = 0;
    for (i = 0; i < n; i++) {
        int r = pos - L;
        float dly, y;
        if (r < 0) r += LC_DELAY_MAX;
        dly = s_delay[line][r];
        y = x[i];
        s_delay[line][pos] = y + dly * fb;
        x[i] = y * (1.0f - wet) + dly * wet;
        pos++;
        if (pos >= LC_DELAY_MAX) pos = 0;
    }
    s_dpos[line] = pos;
    return clamped ? 0 : 1;
}

static void apply_chorus(int line, const int16_t *p, float *x, int n, uint32_t fs, uint64_t pos)
{
    int d0 = clamp_i(p[0], 1, 40);
    int depth = clamp_i(p[1], 0, 10);
    int ft = clamp_i(p[2], 1, 50);
    float mix = (float)clamp_i(p[3], 0, 100) / 100.0f;
    float f_lfo = (float)ft / 10.0f;
    int i, wp;
    if (line < 0 || line >= LC_LINE_MAX || fs == 0) return;
    wp = s_cpos[line];
    for (i = 0; i < n; i++) {
        float ms = (float)d0 + (float)depth * sinf(2.0f * (float)M_PI * f_lfo * (float)(pos + (uint64_t)i) / (float)fs);
        float samp = ms * 0.001f * (float)fs;
        int i0, i1;
        float frac, a, b, dly;
        if (samp < 0.0f) samp = 0.0f;
        if (samp > (float)(LC_CHORUS_MAX - 2)) samp = (float)(LC_CHORUS_MAX - 2);
        i0 = (int)samp;
        frac = samp - (float)i0;
        i1 = i0 + 1;
        a = s_chorus[line][(wp - i0 + LC_CHORUS_MAX) % LC_CHORUS_MAX];
        b = s_chorus[line][(wp - i1 + LC_CHORUS_MAX) % LC_CHORUS_MAX];
        dly = a + (b - a) * frac;
        s_chorus[line][wp] = x[i];
        x[i] = x[i] * (1.0f - mix) + dly * mix;
        wp++;
        if (wp >= LC_CHORUS_MAX) wp = 0;
    }
    s_cpos[line] = wp;
}

static void apply_gate(LcIns *s, const int16_t *p, float *x, int n, uint32_t fs)
{
    int thr_c = clamp_i(p[0], 1, 100);
    int tau = clamp_i(p[1], 1, 500);
    int hold_ms = clamp_i(p[2], 0, 500);
    float thr = (float)thr_c / 100.0f;
    float lambda = expf(-1000.0f / ((float)tau * (float)fs));
    int hold_reload = (int)(((uint32_t)hold_ms * fs) / 1000u);
    int i;
    for (i = 0; i < n; i++) {
        float ax = x[i] < 0.0f ? -x[i] : x[i];
        float decay = s->env * lambda;
        s->env = ax > decay ? ax : decay;
        if (s->env >= thr) {
            s->hold = hold_reload;
        } else if (s->hold > 0) {
            s->hold -= 1;
        } else {
            x[i] = 0.0f;
        }
    }
}

static void apply_trem(const int16_t *p, float *x, int n, uint32_t fs, uint64_t pos)
{
    float d = (float)clamp_i(p[0], 0, 100) / 100.0f;
    float f = (float)clamp_i(p[1], 1, 120) / 10.0f;
    int i;
    for (i = 0; i < n; i++) {
        float g = (1.0f - d) + d * sinf(2.0f * (float)M_PI * f * (float)(pos + (uint64_t)i) / (float)fs);
        x[i] *= g;
    }
}

static void apply_lim(float *x, int n)
{
    int i;
    for (i = 0; i < n; i++) x[i] = master_soft_clamp(x[i]);
}

int lc_insert_apply(int inst, int id, const int16_t *p, float *x, int n,
                    uint32_t fs, uint32_t bpm, uint32_t den,
                    const float *curve, uint64_t pos, int line)
{
    if (!p || !x || n <= 0 || n > 8192 || fs < 8000 || inst < 0 || inst >= LC_INS_MAX) return 0;
    if (id == 1) apply_eq(&s_ins[inst], p, x, n, fs);
    else if (id == 2) apply_sat(curve, x, n);
    else if (id == 3) return apply_delay(line, p, x, n, fs, bpm, den);
    else if (id == 4) return 1;
    else if (id == 5) apply_chorus(line, p, x, n, fs, pos);
    else if (id == 6) apply_gate(&s_ins[inst], p, x, n, fs);
    else if (id == 7) apply_trem(p, x, n, fs, pos);
    else if (id == 8) apply_lim(x, n);
    return 1;
}
