/*
 * src/demo/lilcu64_score.c — 1:1 transcriptions of the index.html track scores
 *
 * The shims below are line-by-line ports of the WebAudio shims in index.html
 * (shimPulseSquash, shimFrogCroak, shimPulseBell, shimWaddle, shimFloat,
 * shimDance, shimDing, shimKick, shimSnare, shimHiHat, shimGuitar,
 * shimGrandPiano, shimPedalBass, shimSaxFlute) running on the lilcu64_wa node
 * engine. The track bodies are transcribed call-for-call with the same time
 * offsets, frequencies and volumes as window.playTrack() in index.html.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "lilcu64_score.h"
#include "lilcu64_wa.h"
#include <stddef.h>
#include <math.h>

/* ctx.currentTime at the moment playTrack() scheduled the score */
static double s_T = 0.0;

typedef struct { float f, d, v, t, slide; } Note;

#define N(f, d, v, t)        { (f), (d), (v), (t), 0.0f }
#define NS(f, d, v, t, s)    { (f), (d), (v), (t), (s) }
#define NOTES(...)  (const Note[]){ __VA_ARGS__ }, \
                    (int)(sizeof((const Note[]){ __VA_ARGS__ }) / sizeof(Note))
#define CHORD(...)  (const float[]){ __VA_ARGS__ }, \
                    (int)(sizeof((const float[]){ __VA_ARGS__ }) / sizeof(float))

/* ── WebAudio shims (index.html lines 3248–4053) ──────────────────── */

/* [SHIM 1A] Singing harmonic triad */
static void shimPulseSquash(double t, float volMul, float pitchMul) {
    static const float fv[6][3] = {
        { 261.63f, 0.055f, 2.2f }, { 523.25f, 0.070f, 2.4f }, { 659.25f, 0.048f, 2.0f },
        { 783.99f, 0.052f, 2.1f }, { 1046.50f, 0.040f, 1.7f }, { 1318.51f, 0.025f, 1.4f }
    };
    double S = s_T + t;
    for (int i = 0; i < 6; i++) {
        float tf = fv[i][0] * pitchMul, d = fv[i][2];
        WaNode *o = wa_osc(WA_SINE, S, S + d);
        wa_freq(o, WA_SET, tf * 0.97f, S);
        wa_freq(o, WA_EXP, tf, S + 0.10);
        wa_gain(o, WA_SET, 0.0001f, S);
        wa_gain(o, WA_LIN, fv[i][1] * volMul, S + 0.09);
        wa_gain(o, WA_EXP, 0.0001f, S + d);
    }
}

/* [SHIM 1A-Alt] Acoustic frog croak / cuíca rasp (蛙の鳴き声) */
static void emitSingleCroak(double S, float pitchMul, float volMul,
                            double tOff, float baseF, float duration, float intensity) {
    double C = S + tOff;
    float f1 = baseF * pitchMul;
    float f2 = baseF * 2.45f * pitchMul;
    int numPulses = (int)(duration / 0.031f);

    for (int k = 0; k < 2; k++) {
        /* osc1: sawtooth throat fundamental, osc2: triangle mouth overtone.
         * Both go through the same bandpass + pulse-train gain (linear, so
         * per-oscillator copies are exactly equivalent). */
        WaNode *o = wa_osc(k == 0 ? WA_SAWTOOTH : WA_TRIANGLE, C, C + duration + 0.05);
        if (!o) return;
        if (k == 0) {
            wa_freq(o, WA_SET, f1 * 0.88f, C);
            wa_freq(o, WA_EXP, f1 * 1.25f, C + duration * 0.75);
            wa_freq(o, WA_LIN, f1 * 0.95f, C + duration);
        } else {
            wa_freq(o, WA_SET, f2 * 0.90f, C);
            wa_freq(o, WA_EXP, f2 * 1.20f, C + duration * 0.75);
        }
        wa_filter(o, WA_FILT_BANDPASS, baseF * 2.2f * pitchMul, 3.8f);

        wa_gain(o, WA_SET, 0.0001f, C);
        for (int p = 0; p < numPulses; p++) {
            double pT = C + p * 0.031;
            float pulseAmp = 0.24f * volMul * intensity * (1.0f - p * 0.07f);
            wa_gain(o, WA_SET, pulseAmp * 0.2f, pT);
            wa_gain(o, WA_LIN, pulseAmp, pT + 0.009);
            wa_gain(o, WA_LIN, pulseAmp * 0.15f, pT + 0.025);
        }
        wa_gain(o, WA_EXP, 0.0001f, C + duration + 0.04);
    }
}

static void shimFrogCroak(double t, float pitchMul, float volMul, int isDouble) {
    double S = s_T + t;
    emitSingleCroak(S, pitchMul, volMul, 0.00, 125.0f, 0.19f, 1.0f);   /* "Gwaaa-rk" */
    if (isDouble) emitSingleCroak(S, pitchMul, volMul, 0.25, 155.0f, 0.095f, 0.85f); /* "gwuk!" */
}

/* [SHIM 1B] Golden chi bell / sub drone */
static void shimPulseBell(double t, float volMul, float pitchMul) {
    static const float fv[8][3] = {
        { 130.81f, 0.065f, 3.5f }, { 196.00f, 0.050f, 3.2f }, { 261.63f, 0.070f, 3.4f },
        { 392.00f, 0.055f, 3.0f }, { 523.25f, 0.060f, 2.8f }, { 659.25f, 0.040f, 2.5f },
        { 783.99f, 0.035f, 2.3f }, { 1046.50f, 0.025f, 1.9f }
    };
    double S = s_T + t;
    for (int i = 0; i < 8; i++) {
        float tf = fv[i][0] * pitchMul, d = fv[i][2];
        WaNode *o = wa_osc(WA_SINE, S, S + d);
        wa_freq(o, WA_SET, tf * 0.985f, S);
        wa_freq(o, WA_EXP, tf, S + 0.14);
        wa_gain(o, WA_SET, 0.0001f, S);
        wa_gain(o, WA_LIN, fv[i][1] * volMul, S + 0.12);
        wa_gain(o, WA_EXP, 0.0001f, S + d);
    }
}

/* [SHIM 2] Crystal step / Fukui walking bass */
static void shimWaddleD(double t, float pitch, float volMul, float duration) {
    double S = s_T + t;
    WaNode *o = wa_osc(WA_TRIANGLE, S, S + duration);
    wa_freq(o, WA_SET, pitch, S);
    wa_freq(o, WA_EXP, pitch * 0.82f, S + duration);
    wa_gain(o, WA_SET, 0.12f * volMul, S);
    wa_gain(o, WA_EXP, 0.001f, S + duration);
}
static void shimWaddle(double t, float pitch, float volMul) { shimWaddleD(t, pitch, volMul, 0.08f); }

/* [SHIM 3] Glass harp / Imada Rhodes chords (numeric chordNotes form) */
static void shimFloatEx(double t, float baseFreq, float volMul, const float *chord, int nc,
                        float stagger, float noteDuration) {
    double S = s_T + t;
    float ratio = baseFreq / 523.25f;
    Note def[4] = {
        N(523.25f * ratio, 0.55f, 0.12f * volMul, 0.00f),
        N(659.25f * ratio, 0.50f, 0.10f * volMul, 0.06f),
        N(783.99f * ratio, 0.65f, 0.11f * volMul, 0.12f),
        N(1046.50f * ratio, 0.75f, 0.08f * volMul, 0.18f)
    };
    int count = chord ? nc : 4;
    for (int i = 0; i < count; i++) {
        Note n = chord ? (Note)N(chord[i], noteDuration, 0.11f * volMul, i * stagger) : def[i];
        double nT = S + n.t;
        WaNode *o = wa_osc(WA_SINE, nT, nT + n.d);
        wa_freq(o, WA_SET, n.f * 0.985f, nT);
        wa_freq(o, WA_EXP, n.f, nT + 0.04);
        wa_gain(o, WA_SET, 0.0001f, nT);
        wa_gain(o, WA_LIN, n.v, nT + 0.03);
        wa_gain(o, WA_EXP, 0.0001f, nT + n.d);
    }
}
static void shimFloat(double t, float baseFreq, float volMul, const float *chord, int nc) {
    shimFloatEx(t, baseFreq, volMul, chord, nc, 0.035f, 0.65f);
}

/* [SHIM 4] Crystalline cascade / blues runs. With a phrase, volMul is ignored (as in JS). */
static void shimDance(double t, float pitchScale, float volMul, const Note *phrase, int np) {
    double S = s_T + t;
    Note def[5] = {
        N(587.33f * pitchScale, 0.20f, 0.16f * volMul, 0.000f),
        N(739.99f * pitchScale, 0.22f, 0.15f * volMul, 0.085f),
        N(880.00f * pitchScale, 0.25f, 0.16f * volMul, 0.170f),
        N(1174.66f * pitchScale, 0.32f, 0.18f * volMul, 0.255f),
        N(1479.98f * pitchScale, 0.45f, 0.12f * volMul, 0.370f)
    };
    const Note *ns = phrase ? phrase : def;
    int count = phrase ? np : 5;
    for (int i = 0; i < count; i++) {
        double nT = S + ns[i].t;
        WaNode *o = wa_osc(WA_SINE, nT, nT + ns[i].d);
        wa_freq(o, WA_SET, ns[i].f, nT);
        wa_gain(o, WA_SET, 0.0001f, nT);
        wa_gain(o, WA_LIN, ns[i].v, nT + 0.02);
        wa_gain(o, WA_EXP, 0.0001f, nT + ns[i].d);
    }
}

/* [SHIM 5] Crystal chime / swing ride cymbal */
static void shimDing(double t, float freq, float volMul, float decay) {
    double S = s_T + t;
    WaNode *o = wa_osc(WA_SINE, S, S + decay);
    wa_freq(o, WA_SET, freq, S);
    wa_gain(o, WA_SET, 0.0001f, S);
    wa_gain(o, WA_LIN, 0.15f * volMul, S + 0.012);
    wa_gain(o, WA_EXP, 0.0001f, S + decay);
}

/* [SHIM 6] Jazz kick */
static void shimKickD(double t, float pitch, float volMul, float duration) {
    double S = s_T + t;
    WaNode *o = wa_osc(WA_SINE, S, S + duration);
    wa_freq(o, WA_SET, pitch * 2.2f, S);
    wa_freq(o, WA_EXP, pitch * 0.75f, S + duration);
    wa_gain(o, WA_SET, 0.24f * volMul, S);
    wa_gain(o, WA_EXP, 0.001f, S + duration);
}
static void shimKick(double t, float pitch, float volMul) { shimKickD(t, pitch, volMul, 0.16f); }

/* [SHIM 7] Snare / rimshot / brush */
static void shimSnare(double t, float volMul, int isRimshot) {
    double S = s_T + t;
    float baseF = isRimshot ? 220.0f : 175.0f;
    WaNode *o = wa_osc(WA_TRIANGLE, S, S + 0.12);
    wa_freq(o, WA_SET, baseF, S);
    wa_freq(o, WA_EXP, baseF * 0.65f, S + 0.12);
    wa_gain(o, WA_SET, 0.18f * volMul, S);
    wa_gain(o, WA_EXP, 0.001f, S + 0.12);

    float nd = isRimshot ? 0.09f : 0.15f;
    WaNode *n = wa_osc(WA_NOISE, S, S + nd);
    wa_filter(n, WA_FILT_BANDPASS, isRimshot ? 3200.0f : 2400.0f, 2.0f);
    wa_gain(n, WA_SET, 0.16f * volMul, S);
    wa_gain(n, WA_EXP, 0.001f, S + nd);
}

/* [SHIM 8] Hi-hat chick / sizzle */
static void shimHiHat(double t, int open, float volMul) {
    double S = s_T + t;
    float dur = open ? 0.28f : 0.055f;
    WaNode *n = wa_osc(WA_NOISE, S, S + dur);
    wa_filter(n, WA_FILT_HIGHPASS, 7500.0f, 1.0f);
    wa_gain(n, WA_SET, 0.08f * volMul, S);
    wa_gain(n, WA_EXP, 0.0005f, S + dur);

    WaNode *o = wa_osc(WA_SINE, S, S + dur);
    wa_freq(o, WA_SET, open ? 6200.0f : 8400.0f, S);
    wa_gain(o, WA_SET, 0.04f * volMul, S);
    wa_gain(o, WA_EXP, 0.0005f, S + dur);
}

/* [SHIM 9] Kazumi Watanabe clean jazz guitar */
static void shimGuitar(double t, const Note *ph, int np, float volMul) {
    double S = s_T + t;
    for (int i = 0; i < np; i++) {
        const Note *n = &ph[i];
        double nT = S + n->t;
        WaNode *o = wa_osc(WA_TRIANGLE, nT, nT + n->d);
        wa_freq(o, WA_SET, n->f, nT);
        if (n->slide > 0.0f) wa_freq(o, WA_EXP, n->slide, nT + n->d * 0.7);
        wa_filter(o, WA_FILT_LOWPASS, 2800.0f, 1.0f);
        wa_filter_freq(o, WA_SET, 2800.0f, nT);
        wa_filter_freq(o, WA_EXP, 1400.0f, nT + n->d);
        wa_gain(o, WA_SET, 0.0001f, nT);
        wa_gain(o, WA_LIN, n->v * volMul, nT + 0.015);
        wa_gain(o, WA_EXP, 0.0001f, nT + n->d);
    }
}

/* [SHIM 10] Concert grand piano */
static void shimGrandPiano(double t, float freq, float volMul, float duration) {
    static const float harm[3] = { 1.0f, 2.002f, 3.006f };
    static const float hv[3]   = { 0.16f, 0.08f, 0.035f };
    double S = s_T + t;
    for (int h = 0; h < 3; h++) {
        float hd = (h == 0) ? duration : duration * (1.0f - h * 0.25f);
        WaNode *o = wa_osc(h == 1 ? WA_TRIANGLE : WA_SINE, S, S + hd);
        wa_freq(o, WA_SET, freq * harm[h], S);
        wa_gain(o, WA_SET, 0.0001f, S);
        wa_gain(o, WA_LIN, hv[h] * volMul, S + 0.008);
        wa_gain(o, WA_EXP, 0.0001f, S + hd);
    }
}

/* [SHIM 11] Sub-bass acoustic pedal */
static void shimPedalBass(double t, float freq, float volMul, float duration) {
    double S = s_T + t;
    WaNode *o = wa_osc(WA_TRIANGLE, S, S + duration);
    wa_freq(o, WA_SET, freq, S);
    wa_freq(o, WA_EXP, freq * 0.98f, S + duration);
    wa_gain(o, WA_SET, 0.20f * volMul, S);
    wa_gain(o, WA_EXP, 0.0005f, S + duration);
}

/* [SHIM 18] Breathy jazz woodwind / melodica */
static void shimSaxFlute(double t, const Note *ph, int np, float volMul) {
    double S = s_T + t;
    for (int i = 0; i < np; i++) {
        const Note *n = &ph[i];
        double nT = S + n->t;
        WaNode *o = wa_osc(WA_TRIANGLE, nT, nT + n->d);
        wa_freq(o, WA_SET, n->f, nT);
        if (n->slide > 0.0f) wa_freq(o, WA_EXP, n->slide, nT + n->d * 0.75);
        wa_filter(o, WA_FILT_LOWPASS, 2400.0f, 2.0f);
        wa_gain(o, WA_SET, 0.0001f, nT);
        wa_gain(o, WA_LIN, n->v * volMul, nT + 0.025);
        wa_gain(o, WA_EXP, 0.0001f, nT + n->d);

        WaNode *b = wa_osc(WA_NOISE, nT, nT + n->d);   /* breath noise */
        wa_filter(b, WA_FILT_BANDPASS, n->f * 1.8f, 5.0f);
        wa_gain(b, WA_SET, 0.025f * volMul, nT);
        wa_gain(b, WA_EXP, 0.0005f, nT + n->d);
    }
}

/* [SHIM 12] Industrial Cyber Kick (Trent Reznor / Nine Inch Nails Heavy Distorted Sub-Punch) */
static void shimIndustrialKickEx(double t, float volMul, float duration) {
    double S = s_T + t;
    WaNode *sub = wa_osc(WA_SINE, S, S + duration);
    if (sub) {
        wa_freq(sub, WA_SET, 160.0f, S);
        wa_freq(sub, WA_EXP, 42.0f, S + duration);
        wa_gain(sub, WA_SET, 0.32f * volMul, S);
        wa_gain(sub, WA_EXP, 0.001f, S + duration);
    }
    WaNode *clk = wa_osc(WA_TRIANGLE, S, S + 0.055);
    if (clk) {
        wa_freq(clk, WA_SET, 380.0f, S);
        wa_freq(clk, WA_EXP, 55.0f, S + 0.05);
        wa_filter(clk, WA_FILT_LOWPASS, 1800.0f, 1.0f);
        wa_gain(clk, WA_SET, 0.24f * volMul, S);
        wa_gain(clk, WA_EXP, 0.001f, S + 0.055);
    }
}
static void shimIndustrialKick(double t, float volMul) {
    shimIndustrialKickEx(t, volMul, 0.22f);
}

/* [SHIM 13] Industrial Metallic Snare / Cyber Clank */
static void shimIndustrialSnare(double t, float volMul, int isClank) {
    double S = s_T + t;
    float dur = isClank ? 0.08f : 0.17f;
    float bpF = isClank ? 3600.0f : 2600.0f;
    WaNode *n = wa_osc(WA_NOISE, S, S + dur);
    if (n) {
        wa_filter(n, WA_FILT_BANDPASS, bpF, 2.2f);
        wa_gain(n, WA_SET, 0.24f * volMul, S);
        wa_gain(n, WA_EXP, 0.001f, S + dur);
    }
    static const float fq[2] = { 220.0f, 352.0f };
    static const int wt[2] = { WA_SQUARE, WA_TRIANGLE };
    for (int i = 0; i < 2; i++) {
        WaNode *m = wa_osc(wt[i], S, S + 0.11);
        if (!m) continue;
        wa_freq(m, WA_SET, fq[i], S);
        wa_gain(m, WA_SET, 0.12f * volMul, S);
        wa_gain(m, WA_EXP, 0.001f, S + 0.11);
    }
}

/* [SHIM 14] Industrial Sawtooth Synth Bass (Trent Reznor 16th-Note Analog Overdrive Ostinato) */
static void shimIndustrialBassSlide(double t, float freq, float volMul, float duration, float slideFreq) {
    double S = s_T + t;
    static const int types[2] = { WA_SAWTOOTH, WA_SQUARE };
    static const float fmul[2] = { 1.0f, 1.005f };
    for (int k = 0; k < 2; k++) {
        WaNode *o = wa_osc(types[k], S, S + duration);
        if (!o) continue;
        float f0 = freq * fmul[k];
        wa_freq(o, WA_SET, f0, S);
        if (slideFreq > 0.0f) {
            wa_freq(o, WA_EXP, slideFreq * fmul[k], S + duration * 0.8);
        }
        wa_filter(o, WA_FILT_LOWPASS, 2600.0f, 4.5f);
        wa_filter_freq(o, WA_SET, 2600.0f, S);
        wa_filter_freq(o, WA_EXP, 420.0f, S + duration);
        wa_gain(o, WA_SET, 0.0001f, S);
        wa_gain(o, WA_LIN, 0.11f * volMul, S + 0.008);
        wa_gain(o, WA_EXP, 0.0005f, S + duration);
    }
}
static void shimIndustrialBass(double t, float freq, float volMul, float duration) {
    shimIndustrialBassSlide(t, freq, volMul, duration, 0.0f);
}

/* [SHIM 15] Dewan Swarmatron Drone (Trent Reznor & Atticus Ross Signature 8-Voice Ribbon Cluster) */
static void shimSwarmatron(double t, float centerFreq, float duration, float volMul, float spreadCents) {
    double S = s_T + t;
    static const float mults[8] = { -1.0f, -0.65f, -0.35f, -0.12f, 0.12f, 0.35f, 0.65f, 1.0f };
    float perOscAmp = (0.18f * volMul) / 4.0f;
    for (int i = 0; i < 8; i++) {
        WaNode *o = wa_osc(WA_SAWTOOTH, S, S + duration);
        if (!o) continue;
        float c0 = spreadCents * mults[i];
        float c1 = c0 * 1.5f;
        float c2 = c0 * 0.8f;
        float f0 = centerFreq * powf(2.0f, c0 / 1200.0f);
        float f1 = centerFreq * powf(2.0f, c1 / 1200.0f);
        float f2 = centerFreq * powf(2.0f, c2 / 1200.0f);
        wa_freq(o, WA_SET, f0, S);
        wa_freq(o, WA_LIN, f1, S + duration * 0.5);
        wa_freq(o, WA_LIN, f2, S + duration);

        wa_filter(o, WA_FILT_LOWPASS, 950.0f, 2.5f);
        wa_filter_freq(o, WA_SET, 950.0f, S);
        wa_filter_freq(o, WA_LIN, 1450.0f, S + duration * 0.6);
        wa_filter_freq(o, WA_LIN, 800.0f, S + duration);

        wa_gain(o, WA_SET, 0.0001f, S);
        wa_gain(o, WA_LIN, perOscAmp, S + 0.40);
        wa_gain(o, WA_SET, perOscAmp, S + duration - 0.50);
        wa_gain(o, WA_EXP, 0.0001f, S + duration);
    }
}

/* [SHIM 16] Cyber Glitch Zap / Terminal Data Pulse */
static void shimGlitchZap(double t, float baseFreq, float volMul, float duration) {
    double S = s_T + t;
    WaNode *o = wa_osc(WA_SAWTOOTH, S, S + duration);
    if (!o) return;
    wa_freq(o, WA_SET, baseFreq * 2.5f, S);
    wa_freq(o, WA_EXP, 80.0f, S + duration);
    wa_filter(o, WA_FILT_HIGHPASS, 1800.0f, 1.0f);
    wa_gain(o, WA_SET, 0.18f * volMul, S);
    wa_gain(o, WA_EXP, 0.001f, S + duration);
}

/* [SHIM 17] Nine Inch Nails TRON Cyber Lead */
static void shimCyberLead(double t, const Note *ph, int np, float volMul) {
    double S = s_T + t;
    for (int i = 0; i < np; i++) {
        const Note *n = &ph[i];
        double nT = S + n->t;
        static const int wt[2] = { WA_SAWTOOTH, WA_SQUARE };
        static const float fm[2] = { 1.0f, 1.004f };
        for (int k = 0; k < 2; k++) {
            WaNode *o = wa_osc(wt[k], nT, nT + n->d);
            if (!o) continue;
            float f0 = n->f * fm[k];
            wa_freq(o, WA_SET, f0, nT);
            if (n->slide > 0.0f) {
                wa_freq(o, WA_EXP, n->slide * fm[k], nT + n->d * 0.7);
            }
            wa_filter(o, WA_FILT_LOWPASS, 3800.0f, 4.0f);
            wa_filter_freq(o, WA_SET, 3800.0f, nT);
            wa_filter_freq(o, WA_EXP, 1600.0f, nT + n->d);
            wa_gain(o, WA_SET, 0.0001f, nT);
            wa_gain(o, WA_LIN, n->v * volMul * 0.55f, nT + 0.012);
            wa_gain(o, WA_EXP, 0.0001f, nT + n->d);
        }
    }
}

/* [SHIM 19] Concert Grand Piano Wooden Felt Hammer Impact */
static void shimPianoHammer(double t, float volMul) {
    double S = s_T + t;
    WaNode *o = wa_osc(WA_TRIANGLE, S, S + 0.035);
    if (!o) return;
    wa_freq(o, WA_SET, 120.0f, S);
    wa_freq(o, WA_EXP, 40.0f, S + 0.035);
    wa_gain(o, WA_SET, 0.12f * volMul, S);
    wa_gain(o, WA_EXP, 0.0005f, S + 0.035);
}

/* [SHIM 20] Concert Grand Damper Pedal Lift & String Sympathetic Resonance */
static void shimPedalSwoosh(double t, float volMul, float duration) {
    double S = s_T + t;
    WaNode *n = wa_osc(WA_NOISE, S, S + duration);
    if (!n) return;
    wa_filter(n, WA_FILT_BANDPASS, 450.0f, 3.5f);
    wa_filter_freq(n, WA_SET, 450.0f, S);
    wa_filter_freq(n, WA_EXP, 280.0f, S + duration);
    wa_gain(n, WA_SET, 0.0001f, S);
    wa_gain(n, WA_LIN, 0.05f * volMul, S + 0.08);
    wa_gain(n, WA_EXP, 0.0001f, S + duration);
}

/* ── TRACK 1: 鐘のキャロル (Carol of the Bells) — index.html 4410–4700 ── */

#define T1_BEAT      (60.0 / 168.0)
#define T1_BAR       (T1_BEAT * 3.0)
#define T1_EIGHTH    (T1_BEAT / 2.0)
#define T1_SIXTEENTH (T1_BEAT / 4.0)
#define T1_VOL       0.78f

static void playBellMotif(double st, float volScale, float octave) {
    float v = 0.16f * T1_VOL * volScale;
    shimDing(st + 0.000, 783.99f * octave, v * 0.95f, 0.70f);                 /* G5 */
    shimDing(st + T1_BEAT, 739.99f * octave, v * 0.85f, 0.60f);               /* F#5 */
    shimDing(st + T1_BEAT + T1_EIGHTH, 783.99f * octave, v * 0.85f, 0.60f);   /* G5 */
    shimDing(st + T1_BEAT * 2, 659.25f * octave, v * 1.05f, 0.90f);           /* E5 */
}

static void playBassBar(double st, float rootF, float fifthF, float octF, float volScale) {
    float v = 0.22f * T1_VOL * volScale;
    shimWaddle(st + 0.000, rootF, v);
    shimWaddle(st + T1_BEAT, fifthF, v * 0.85f);
    shimWaddle(st + T1_BEAT * 2, octF, v * 0.95f);
}

static void playDrums(double st, int hasKick, int hasSnare) {
    if (hasKick) shimKickD(st, 68.0f, 0.15f * T1_VOL, 0.14f);
    shimHiHat(st, 0, 0.08f * T1_VOL);
    shimHiHat(st + T1_BEAT, 0, 0.07f * T1_VOL);
    if (hasSnare) shimSnare(st + T1_BEAT, 0.12f * T1_VOL, 1);
    shimHiHat(st + T1_BEAT * 2, 0, 0.08f * T1_VOL);
    if (hasSnare) shimSnare(st + T1_BEAT * 2, 0.10f * T1_VOL, 0);
}

static void ding_run(double st, const float *run, float vol, float decay) {
    for (int i = 0; i < 12; i++) shimDing(st + i * T1_SIXTEENTH, run[i], vol * T1_VOL, decay);
}

static void schedule_track1(void) {
    const double beat = T1_BEAT, bar = T1_BAR, sixteenth = T1_SIXTEENTH;
    const float  V = T1_VOL;

    /* NOTE: index.html also calls shimFloat(b, 'm9'|'m7'|'maj7', vol) in bars
     * 9, 10, 11, 21 and 23. A string baseFreq makes the chord argument a number,
     * `chordNotes.map` throws and the shim's try/catch swallows it, so those
     * calls are silent in the browser. They are omitted here for 1:1 output. */

    /* SECTION 1: Stage intro & bell ostinato (bars 1-4) */
    playBellMotif(0.000, 0.95f, 1.0f);
    shimPulseBell(0.000, 0.65f, 0.30f * V);

    playBellMotif(bar * 1, 1.0f, 1.0f);
    shimHiHat(bar * 1 + beat, 0, 0.06f * V);
    shimHiHat(bar * 1 + beat * 2, 0, 0.07f * V);

    playBellMotif(bar * 2, 1.05f, 1.0f);
    playBellMotif(bar * 2, 0.70f, 2.0f);
    playDrums(bar * 2, 1, 0);

    playBellMotif(bar * 3, 1.10f, 1.0f);
    playBellMotif(bar * 3, 0.75f, 2.0f);
    playDrums(bar * 3, 1, 1);
    shimWaddle(bar * 3 + beat * 2, 82.41f, 0.18f * V);

    /* SECTION 2: The BTRON game groove (bars 5-8) */
    double b5 = bar * 4;
    playBellMotif(b5, 1.0f, 1.0f); playBassBar(b5, 82.41f, 123.47f, 164.81f, 1.0f); playDrums(b5, 1, 1);
    double b6 = bar * 5;
    playBellMotif(b6, 1.0f, 1.0f); playBassBar(b6, 73.42f, 110.00f, 146.83f, 1.0f); playDrums(b6, 1, 1);
    double b7 = bar * 6;
    playBellMotif(b7, 1.0f, 1.0f); playBassBar(b7, 65.41f, 98.00f, 130.81f, 1.0f); playDrums(b7, 1, 1);
    double b8 = bar * 7;
    playBellMotif(b8, 1.1f, 1.0f); playBassBar(b8, 61.74f, 92.50f, 123.47f, 1.0f); playDrums(b8, 1, 1);
    shimSnare(b8 + beat * 2 + sixteenth, 0.10f * V, 1);
    shimSnare(b8 + beat * 2 + sixteenth * 2, 0.12f * V, 0);

    /* SECTION 3: Chiptune melodic counterpoint (bars 9-12) */
    double b9 = bar * 8;
    playBellMotif(b9, 0.85f, 1.0f); playBassBar(b9, 82.41f, 123.47f, 164.81f, 1.0f); playDrums(b9, 1, 1);
    shimPulseSquash(b9, 1.25f, 0.15f * V);
    shimDing(b9, 987.77f, 0.12f * V, 0.8f);

    double b10 = bar * 9;
    playBellMotif(b10, 0.85f, 1.0f); playBassBar(b10, 73.42f, 110.00f, 146.83f, 1.0f); playDrums(b10, 1, 1);
    shimDing(b10 + 0.000, 1046.50f, 0.12f * V, 0.5f);
    shimDing(b10 + beat, 987.77f, 0.12f * V, 0.5f);
    shimDing(b10 + beat * 2, 880.00f, 0.14f * V, 0.6f);

    double b11 = bar * 10;
    playBellMotif(b11, 0.85f, 1.0f); playBassBar(b11, 65.41f, 98.00f, 130.81f, 1.0f); playDrums(b11, 1, 1);
    shimDing(b11 + 0.000, 783.99f, 0.12f * V, 0.5f);
    shimDing(b11 + beat, 880.00f, 0.12f * V, 0.5f);
    shimDing(b11 + beat * 2, 987.77f, 0.14f * V, 0.6f);

    double b12 = bar * 11;
    playBellMotif(b12, 0.90f, 1.0f); playBassBar(b12, 61.74f, 92.50f, 123.47f, 1.0f); playDrums(b12, 1, 1);
    shimDing(b12 + 0.000, 659.25f, 0.14f * V, 0.8f);
    shimPulseSquash(b12, 1.0f, 0.14f * V);

    /* SECTION 4: Coin & star arpeggios (bars 13-16) */
    double b13 = bar * 12;
    playBassBar(b13, 82.41f, 123.47f, 164.81f, 1.0f); playDrums(b13, 1, 1);
    shimDance(b13, 1.0f, 0.12f * V, NULL, 0); playBellMotif(b13, 0.8f, 1.0f);
    double b14 = bar * 13;
    playBassBar(b14, 73.42f, 110.00f, 146.83f, 1.0f); playDrums(b14, 1, 1);
    shimDance(b14, 0.89f, 0.12f * V, NULL, 0); playBellMotif(b14, 0.8f, 1.0f);
    double b15 = bar * 14;
    playBassBar(b15, 65.41f, 98.00f, 130.81f, 1.0f); playDrums(b15, 1, 1);
    shimDance(b15, 0.79f, 0.12f * V, NULL, 0); playBellMotif(b15, 0.8f, 1.0f);
    double b16 = bar * 15;
    playBassBar(b16, 61.74f, 92.50f, 123.47f, 1.0f); playDrums(b16, 1, 1);
    shimDing(b16 + 0.000, 987.77f, 0.12f * V, 0.4f);
    shimDing(b16 + sixteenth, 1046.50f, 0.12f * V, 0.4f);
    shimDing(b16 + sixteenth * 2, 1174.66f, 0.13f * V, 0.4f);
    shimDing(b16 + sixteenth * 3, 1244.51f, 0.14f * V, 0.5f);
    shimSnare(b16 + beat * 2, 0.14f * V, 1);

    /* SECTION 5: Virtuoso speedrun (bars 17-20) */
    static const float run17[12] = { 659.25f, 783.99f, 987.77f, 1318.51f, 1567.98f, 1479.98f,
                                     1318.51f, 1174.66f, 1046.50f, 987.77f, 880.00f, 783.99f };
    static const float run18[12] = { 739.99f, 783.99f, 880.00f, 987.77f, 1046.50f, 1174.66f,
                                     1318.51f, 1479.98f, 1567.98f, 1760.00f, 1567.98f, 1479.98f };
    static const float run19[12] = { 1318.51f, 987.77f, 783.99f, 659.25f, 783.99f, 987.77f,
                                     1318.51f, 1567.98f, 1975.53f, 1567.98f, 1318.51f, 987.77f };
    double b17 = bar * 16;
    playBassBar(b17, 82.41f, 82.41f, 164.81f, 1.0f); playDrums(b17, 1, 1);
    ding_run(b17, run17, 0.08f, 0.22f);
    shimSaxFlute(b17, NOTES(N(1318.51f, (float)(beat * 0.8), 0.12f, 0.0f),
                            N(1567.98f, (float)(beat * 0.8), 0.12f, (float)beat),
                            N(1046.50f, (float)(beat * 0.8), 0.12f, (float)(beat * 2))), V);
    double b18 = bar * 17;
    playBassBar(b18, 65.41f, 61.74f, 123.47f, 1.0f); playDrums(b18, 1, 1);
    ding_run(b18, run18, 0.08f, 0.22f);
    double b19 = bar * 18;
    playBassBar(b19, 82.41f, 123.47f, 164.81f, 1.0f); playDrums(b19, 1, 1);
    ding_run(b19, run19, 0.09f, 0.25f);
    double b20 = bar * 19;
    playBassBar(b20, 61.74f, 92.50f, 123.47f, 1.0f); playDrums(b20, 1, 1);
    shimDing(b20 + 0.000, 1244.51f, 0.10f * V, 0.3f);
    shimDing(b20 + sixteenth, 1479.98f, 0.10f * V, 0.3f);
    shimDing(b20 + sixteenth * 2, 1760.00f, 0.11f * V, 0.3f);
    shimDing(b20 + sixteenth * 3, 1975.53f, 0.12f * V, 0.4f);
    shimDing(b20 + beat * 2, 1567.98f, 0.16f * V, 0.8f);
    shimSnare(b20 + beat * 2, 0.14f * V, 1);

    /* SECTION 6: Victory fanfare & coda (bars 21-24) */
    double b21 = bar * 20;
    shimKickD(b21, 68.0f, 0.18f * V, 0.2f);
    shimSnare(b21, 0.14f * V, 1);
    playBassBar(b21, 82.41f, 123.47f, 164.81f, 1.1f);
    playBellMotif(b21, 1.25f, 1.0f);
    playBellMotif(b21, 0.85f, 2.0f);
    shimPulseSquash(b21, 1.0f, 0.14f * V);

    double b22 = bar * 21;
    playBassBar(b22, 65.41f, 61.74f, 82.41f, 1.1f);
    playDrums(b22, 1, 1);
    playBellMotif(b22, 1.15f, 1.0f);
    shimDance(b22, 1.0f, 0.10f * V, NULL, 0);

    double b23 = bar * 22;
    shimWaddle(b23, 82.41f, 0.16f * V);
    shimDing(b23 + 0.000, 1318.51f, 0.12f * V, 1.6f);
    shimDing(b23 + beat * 2, 2637.02f, 0.10f * V, 2.0f);

    double b24 = bar * 23;
    shimDing(b24 + 0.000, 1318.51f, 0.10f * V, 1.2f);
    shimDing(b24 + 0.357, 987.77f, 0.08f * V, 1.2f);
    shimDing(b24 + 0.714, 2637.02f, 0.09f * V, 2.0f);
}

/* ── TRACK 2: 今田勝 "Green Caterpillar" — index.html 4702–5198 ─────── */

static void schedule_track2(void) {
    /* BAR 00: Atmospheric swamp opening — frog croaks & hi-hat (0.00–2.45s) */
    shimPulseBell(0.00, 0.75f, 0.561f);
    shimPedalBass(0.00, 73.42f, 0.16f, 2.2f);
    shimFrogCroak(0.15, 1.0f, 1.30f, 1);      /* Bullfrog croak 1 */
    shimFrogCroak(0.95, 0.90f, 1.15f, 1);     /* Answering frog croak 2 */
    shimHiHat(1.20, 1, 0.08f);
    shimDing(1.70, 2640.0f, 0.10f, 0.35f);
    shimFrogCroak(1.90, 1.15f, 0.95f, 0);     /* Short throat gulp "Gwuk!" */
    shimSnare(2.15, 0.08f, 0);
    shimSnare(2.30, 0.11f, 0);

    /* BAR 01: Fukui walking bass crawl & Obara swing kit (2.45–4.90s) */
    shimKick(2.45, 65, 0.20f); shimDing(2.45, 1760.0f, 0.12f, 0.25f); shimWaddleD(2.45, 146.83f, 0.16f, 0.14f);
    shimSnare(3.06, 0.18f, 1); shimHiHat(3.06, 0, 0.11f); shimWaddleD(3.06, 174.61f, 0.14f, 0.10f); shimDing(3.06, 1760.0f, 0.10f, 0.20f);
    shimWaddleD(3.37, 196.00f, 0.14f, 0.09f); shimDing(3.37, 2640.0f, 0.08f, 0.12f);
    shimKick(3.67, 65, 0.18f); shimWaddleD(3.67, 207.65f, 0.17f, 0.10f); shimDing(3.67, 1760.0f, 0.10f, 0.20f);
    shimSnare(3.98, 0.09f, 0); shimWaddleD(3.98, 220.00f, 0.15f, 0.09f);
    shimSnare(4.28, 0.20f, 1); shimHiHat(4.28, 0, 0.11f);
    shimFrogCroak(4.28, 1.05f, 0.90f, 0);     /* Cuíca frog rasp */
    shimWaddleD(4.28, 261.63f, 0.14f, 0.10f); shimDing(4.28, 1760.0f, 0.11f, 0.22f);
    shimKick(4.59, 65, 0.16f); shimWaddleD(4.59, 293.66f, 0.15f, 0.09f); shimDing(4.59, 2640.0f, 0.08f, 0.12f);

    /* BAR 02: Rhodes & grand piano comping (4.90–7.35s) */
    shimKick(4.90, 65, 0.18f); shimWaddleD(4.90, 261.63f, 0.14f, 0.10f); shimDing(4.90, 1760.0f, 0.11f, 0.22f);
    shimFloat(5.21, 523.25f, 0.14f, CHORD(349.23f, 440.00f, 523.25f, 659.25f));   /* Dm9 */
    shimGrandPiano(5.21, 523.25f, 0.12f, 0.45f);
    shimSnare(5.51, 0.18f, 1); shimHiHat(5.51, 0, 0.11f); shimWaddleD(5.51, 220.00f, 0.14f, 0.10f); shimDing(5.51, 1760.0f, 0.10f, 0.20f);
    shimDing(5.82, 2640.0f, 0.08f, 0.12f);
    shimKick(6.12, 65, 0.18f);
    shimFloat(6.12, 523.25f, 0.13f, CHORD(349.23f, 493.88f, 659.25f, 880.00f));   /* G13 */
    shimGrandPiano(6.12, 659.25f, 0.11f, 0.40f);
    shimWaddleD(6.12, 196.00f, 0.14f, 0.10f); shimDing(6.12, 1760.0f, 0.10f, 0.20f);
    shimSnare(6.42, 0.09f, 0);
    shimSnare(6.73, 0.18f, 1); shimHiHat(6.73, 0, 0.11f); shimWaddleD(6.73, 174.61f, 0.14f, 0.10f); shimDing(6.73, 1760.0f, 0.11f, 0.22f);
    shimKick(7.04, 65, 0.16f); shimHiHat(7.04, 1, 0.10f);
    shimFloat(7.04, 523.25f, 0.12f, CHORD(392.00f, 466.16f, 587.33f, 739.99f));   /* Ebmaj7#11 */
    shimWaddleD(7.04, 164.81f, 0.13f, 0.09f); shimDing(7.04, 2640.0f, 0.08f, 0.12f);
    shimFrogCroak(7.04, 0.95f, 0.85f, 1);     /* Answering frog croak */

    /* BAR 03: Caterpillar main theme — Head A, part 1 (7.35–9.80s) */
    shimGuitar(7.35, NOTES(N(293.66f, 0.24f, 0.18f, 0.00f), N(349.23f, 0.22f, 0.18f, 0.31f),
                           NS(392.00f, 0.18f, 0.19f, 0.61f, 415.30f), N(440.00f, 0.36f, 0.20f, 0.81f)), 1.0f);
    shimSaxFlute(7.35, NOTES(N(587.33f, 0.24f, 0.14f, 0.00f), N(698.46f, 0.22f, 0.14f, 0.31f),
                             NS(783.99f, 0.18f, 0.15f, 0.61f, 830.61f), N(880.00f, 0.36f, 0.16f, 0.81f)), 1.0f);
    shimPulseSquash(7.35, 0.95f, 1.122f);
    shimKick(7.35, 65, 0.20f); shimWaddleD(7.35, 146.83f, 0.14f, 0.11f); shimDing(7.35, 1760.0f, 0.12f, 0.22f);
    shimWaddleD(7.66, 174.61f, 0.13f, 0.09f);
    shimSnare(7.96, 0.18f, 1); shimHiHat(7.96, 0, 0.11f); shimWaddleD(7.96, 196.00f, 0.14f, 0.10f); shimDing(7.96, 1760.0f, 0.10f, 0.20f);
    shimGuitar(8.57, NOTES(N(440.00f, 0.14f, 0.18f, 0.00f), N(415.30f, 0.14f, 0.18f, 0.10f), N(392.00f, 0.14f, 0.17f, 0.20f),
                           N(349.23f, 0.16f, 0.17f, 0.30f), N(293.66f, 0.28f, 0.20f, 0.42f)), 1.0f);
    shimSaxFlute(8.57, NOTES(N(880.00f, 0.14f, 0.13f, 0.00f), N(830.61f, 0.14f, 0.13f, 0.10f), N(783.99f, 0.14f, 0.13f, 0.20f),
                             N(698.46f, 0.16f, 0.13f, 0.30f), N(587.33f, 0.28f, 0.14f, 0.42f)), 1.0f);
    shimGrandPiano(8.57, 523.25f, 0.11f, 0.40f);
    shimFloat(8.57, 523.25f, 0.10f, CHORD(349.23f, 440.00f, 523.25f, 659.25f));
    shimKick(8.57, 65, 0.18f); shimWaddleD(8.57, 220.00f, 0.14f, 0.10f); shimDing(8.57, 1760.0f, 0.10f, 0.20f);
    shimSnare(9.18, 0.18f, 1); shimHiHat(9.18, 0, 0.11f); shimWaddleD(9.18, 196.00f, 0.13f, 0.09f); shimDing(9.18, 2640.0f, 0.08f, 0.12f);
    shimKick(9.49, 65, 0.16f); shimWaddleD(9.49, 146.83f, 0.15f, 0.11f); shimDing(9.49, 1174.66f, 0.14f, 0.35f);

    /* BAR 04: Head A, part 2 — climbing lyrical high cry (9.80–12.25s) */
    shimKick(9.80, 65, 0.18f);
    shimFloat(9.80, 523.25f, 0.13f, CHORD(349.23f, 440.00f, 523.25f, 659.25f));
    shimWaddleD(9.80, 146.83f, 0.14f, 0.10f); shimDing(9.80, 1760.0f, 0.11f, 0.20f);
    shimGuitar(9.80, NOTES(N(349.23f, 0.14f, 0.18f, 0.00f), N(392.00f, 0.14f, 0.18f, 0.15f), N(440.00f, 0.15f, 0.19f, 0.30f),
                           N(523.25f, 0.16f, 0.20f, 0.45f), N(587.33f, 0.30f, 0.22f, 0.61f)), 1.0f);
    shimSaxFlute(9.80, NOTES(N(698.46f, 0.14f, 0.14f, 0.00f), N(783.99f, 0.14f, 0.14f, 0.15f), N(880.00f, 0.15f, 0.15f, 0.30f),
                             N(1046.50f, 0.16f, 0.16f, 0.45f), N(1174.66f, 0.30f, 0.17f, 0.61f)), 1.0f);
    shimSnare(10.41, 0.18f, 1); shimHiHat(10.41, 0, 0.11f); shimWaddleD(10.41, 174.61f, 0.13f, 0.09f);
    shimGuitar(10.41, NOTES(N(698.46f, 0.24f, 0.21f, 0.00f), N(659.25f, 0.18f, 0.20f, 0.25f), N(587.33f, 0.22f, 0.21f, 0.44f),
                            N(523.25f, 0.18f, 0.20f, 0.68f), NS(587.33f, 0.38f, 0.22f, 0.88f, 659.25f)), 1.0f);
    shimGrandPiano(10.41, 698.46f, 0.16f, 0.50f);
    shimSaxFlute(10.41, NOTES(N(1396.91f, 0.24f, 0.15f, 0.00f), N(1318.51f, 0.18f, 0.15f, 0.25f), N(1174.66f, 0.22f, 0.16f, 0.44f),
                              N(1046.50f, 0.18f, 0.15f, 0.68f), N(1174.66f, 0.38f, 0.16f, 0.88f)), 1.0f);
    shimKick(11.02, 65, 0.18f); shimWaddleD(11.02, 196.00f, 0.14f, 0.10f);
    shimFloat(11.02, 523.25f, 0.12f, CHORD(349.23f, 493.88f, 659.25f, 880.00f));
    shimDing(11.02, 1760.0f, 0.10f, 0.20f);
    shimGuitar(11.45, NOTES(N(523.25f, 0.12f, 0.17f, 0.00f), N(440.00f, 0.12f, 0.17f, 0.10f), N(415.30f, 0.12f, 0.18f, 0.20f),
                            N(392.00f, 0.12f, 0.17f, 0.30f), N(349.23f, 0.14f, 0.18f, 0.40f), N(293.66f, 0.26f, 0.20f, 0.52f)), 1.0f);
    shimSnare(11.63, 0.18f, 1); shimHiHat(11.63, 0, 0.11f); shimWaddleD(11.63, 155.56f, 0.14f, 0.10f);
    shimFloat(11.63, 523.25f, 0.11f, CHORD(392.00f, 466.16f, 587.33f, 739.99f));
    shimDing(11.94, 2640.0f, 0.10f, 0.40f);

    /* BAR 05: Head A' — harmonized parallel thirds & turnaround (12.25–14.70s) */
    shimPulseBell(12.25, 0.95f, 0.749f);
    shimKick(12.25, 65, 0.20f); shimWaddleD(12.25, 146.83f, 0.15f, 0.10f); shimDing(12.25, 1760.0f, 0.12f, 0.20f);
    shimGuitar(12.25, NOTES(N(349.23f, 0.16f, 0.18f, 0.00f), N(392.00f, 0.16f, 0.19f, 0.20f), N(440.00f, 0.18f, 0.20f, 0.40f),
                            N(523.25f, 0.20f, 0.21f, 0.65f), N(587.33f, 0.35f, 0.22f, 0.90f)), 1.0f);
    shimSaxFlute(12.25, NOTES(N(440.00f, 0.16f, 0.15f, 0.00f), N(466.16f, 0.16f, 0.15f, 0.20f), N(523.25f, 0.18f, 0.16f, 0.40f),
                              N(659.25f, 0.20f, 0.17f, 0.65f), N(698.46f, 0.35f, 0.18f, 0.90f)), 1.0f);
    shimGrandPiano(12.25, 587.33f, 0.12f, 0.60f);
    shimWaddleD(12.56, 174.61f, 0.14f, 0.08f);
    shimSnare(12.86, 0.18f, 1); shimHiHat(12.86, 0, 0.11f); shimWaddleD(12.86, 196.00f, 0.15f, 0.08f);
    shimKick(13.17, 65, 0.18f); shimWaddleD(13.17, 207.65f, 0.16f, 0.08f); shimDing(13.17, 1760.0f, 0.11f, 0.20f);
    shimGuitar(13.48, NOTES(N(622.25f, 0.14f, 0.19f, 0.00f), N(587.33f, 0.14f, 0.19f, 0.12f), N(523.25f, 0.14f, 0.18f, 0.24f),
                            N(440.00f, 0.15f, 0.18f, 0.36f), N(392.00f, 0.14f, 0.17f, 0.48f), N(349.23f, 0.15f, 0.18f, 0.60f),
                            N(293.66f, 0.30f, 0.20f, 0.74f)), 1.0f);
    shimSaxFlute(13.48, NOTES(N(622.25f, 0.14f, 0.13f, 0.00f), N(587.33f, 0.14f, 0.14f, 0.12f),
                              N(523.25f, 0.14f, 0.14f, 0.24f), N(440.00f, 0.15f, 0.14f, 0.36f)), 1.0f);
    shimFloat(13.48, 523.25f, 0.12f, CHORD(392.00f, 466.16f, 587.33f, 739.99f));
    shimGrandPiano(13.48, 523.25f, 0.11f, 0.45f);
    shimWaddleD(13.48, 220.00f, 0.15f, 0.08f);
    shimWaddleD(13.78, 261.63f, 0.16f, 0.09f);
    shimWaddleD(14.09, 293.66f, 0.17f, 0.09f);
    shimSnare(14.20, 0.12f, 0); shimSnare(14.30, 0.14f, 0); shimSnare(14.40, 0.16f, 0); shimSnare(14.50, 0.18f, 0);
    shimHiHat(14.60, 1, 0.14f); shimDing(14.60, 2640.0f, 0.14f, 0.25f);

    /* BAR 06: Imada lyrical Rhodes solo & guitar response (14.70–17.15s) */
    shimKick(14.70, 65, 0.20f); shimWaddleD(14.70, 146.83f, 0.15f, 0.12f); shimDing(14.70, 1760.0f, 0.12f, 0.22f);
    shimFloatEx(14.70, 523.25f, 0.18f,
                CHORD(440.00f, 523.25f, 587.33f, 698.46f, 659.25f, 587.33f, 523.25f, 587.33f), 0.10f, 1.4f);
    shimGrandPiano(14.70, 587.33f, 0.15f, 0.80f);
    shimWaddleD(15.01, 174.61f, 0.14f, 0.10f);
    shimSnare(15.31, 0.18f, 1); shimHiHat(15.31, 0, 0.11f); shimWaddleD(15.31, 196.00f, 0.14f, 0.10f); shimDing(15.31, 1760.0f, 0.10f, 0.20f);
    shimGuitar(15.62, NOTES(NS(392.00f, 0.14f, 0.18f, 0.00f, 415.30f), N(440.00f, 0.28f, 0.20f, 0.15f),
                            N(523.25f, 0.16f, 0.18f, 0.44f), N(587.33f, 0.35f, 0.22f, 0.62f)), 1.0f);
    shimSaxFlute(15.62, NOTES(NS(783.99f, 0.14f, 0.13f, 0.00f, 830.61f), N(880.00f, 0.28f, 0.15f, 0.15f),
                              N(1046.50f, 0.16f, 0.14f, 0.44f), N(1174.66f, 0.35f, 0.16f, 0.62f)), 1.0f);
    shimKick(15.62, 65, 0.18f); shimWaddleD(15.62, 220.00f, 0.15f, 0.10f); shimDing(15.62, 2640.0f, 0.10f, 0.20f);
    shimSnare(16.23, 0.18f, 1); shimHiHat(16.23, 0, 0.11f); shimWaddleD(16.23, 261.63f, 0.15f, 0.09f);
    shimSnare(16.60, 0.14f, 0); shimSnare(16.75, 0.16f, 0); shimSnare(16.90, 0.20f, 1);
    shimHiHat(16.90, 1, 0.14f); shimDing(16.90, 2640.0f, 0.12f, 0.25f);

    /* BAR 07: Grand fusion climax — full unison blues hook (17.15–19.60s) */
    shimPulseSquash(17.15, 1.25f, 1.122f);
    shimKick(17.15, 65, 0.22f);
    shimGrandPiano(17.15, 293.66f, 0.18f, 0.40f);
    shimWaddleD(17.15, 146.83f, 0.19f, 0.12f);
    shimDing(17.15, 1174.66f, 0.16f, 0.30f);
    {
        static const Note unison[5] = {
            N(392.00f, 0.12f, 0.20f, 0.00f), N(415.30f, 0.14f, 0.22f, 0.16f), N(440.00f, 0.20f, 0.22f, 0.31f),
            N(523.25f, 0.16f, 0.21f, 0.62f), N(587.33f, 0.32f, 0.24f, 0.93f)
        };
        shimGuitar(17.46, unison, 5, 1.0f);
        shimSaxFlute(17.46, NOTES(N(783.99f, 0.12f, 0.16f, 0.00f), N(830.61f, 0.14f, 0.18f, 0.16f), N(880.00f, 0.20f, 0.18f, 0.31f),
                                  N(1046.50f, 0.16f, 0.17f, 0.62f), N(1174.66f, 0.32f, 0.20f, 0.93f)), 1.0f);
        shimDance(17.46, 1.0f, 1.20f, unison, 5);
    }
    shimGrandPiano(17.46, 392.00f, 0.16f, 0.30f);
    shimSnare(17.46, 0.20f, 1); shimHiHat(17.46, 0, 0.12f); shimWaddleD(17.46, 196.00f, 0.16f, 0.09f);
    shimWaddleD(17.76, 207.65f, 0.18f, 0.09f);
    shimKick(18.06, 65, 0.20f); shimWaddleD(18.06, 220.00f, 0.18f, 0.12f); shimDing(18.06, 1760.0f, 0.14f, 0.25f);
    {
        static const Note desc[5] = {
            N(523.25f, 0.14f, 0.19f, 0.00f), N(440.00f, 0.14f, 0.19f, 0.11f), N(392.00f, 0.14f, 0.18f, 0.22f),
            N(349.23f, 0.16f, 0.18f, 0.33f), N(293.66f, 0.30f, 0.22f, 0.45f)
        };
        shimGuitar(18.37, desc, 5, 1.0f);
        shimSaxFlute(18.37, NOTES(N(1046.50f, 0.14f, 0.15f, 0.00f), N(880.00f, 0.14f, 0.15f, 0.11f), N(783.99f, 0.14f, 0.14f, 0.22f),
                                  N(698.46f, 0.16f, 0.14f, 0.33f), N(587.33f, 0.30f, 0.17f, 0.45f)), 1.0f);
        shimDance(18.37, 1.0f, 1.15f, desc, 5);
    }
    shimGrandPiano(18.37, 523.25f, 0.14f, 0.50f);
    shimSnare(18.68, 0.20f, 1); shimHiHat(18.68, 0, 0.12f);
    shimWaddleD(18.37, 261.63f, 0.15f, 0.09f);
    shimWaddleD(18.59, 220.00f, 0.15f, 0.09f);
    shimWaddleD(18.81, 174.61f, 0.16f, 0.10f);
    shimDing(18.98, 1760.0f, 0.16f, 0.45f);
    shimDing(18.98, 2349.32f, 0.12f, 0.45f);

    /* BAR 08: Extended coda — Three Blind Mice turnaround vamp (19.60–22.05s) */
    shimKick(19.60, 65, 0.16f); shimWaddleD(19.60, 146.83f, 0.15f, 0.12f); shimDing(19.60, 1760.0f, 0.10f, 0.20f);
    shimSnare(20.21, 0.13f, 0); shimWaddleD(20.21, 130.81f, 0.14f, 0.11f); shimDing(20.21, 1760.0f, 0.09f, 0.18f);
    shimDing(20.51, 2640.0f, 0.07f, 0.12f);
    shimKick(20.82, 65, 0.16f); shimWaddleD(20.82, 116.54f, 0.14f, 0.11f); shimDing(20.82, 1760.0f, 0.09f, 0.18f);
    shimSnare(21.43, 0.13f, 0); shimWaddleD(21.43, 110.00f, 0.15f, 0.11f); shimDing(21.43, 1760.0f, 0.10f, 0.20f);
    shimWaddleD(21.74, 103.83f, 0.13f, 0.08f); shimDing(21.74, 2640.0f, 0.07f, 0.12f);
    shimFloat(19.90, 523.25f, 0.13f, CHORD(349.23f, 440.00f, 523.25f, 659.25f));   /* Dm9 */
    shimGrandPiano(19.90, 349.23f, 0.10f, 0.45f);
    shimFloat(20.51, 523.25f, 0.12f, CHORD(329.63f, 392.00f, 493.88f, 587.33f));   /* Cmaj7 */
    shimFloat(21.13, 523.25f, 0.12f, CHORD(293.66f, 349.23f, 440.00f, 523.25f));   /* Bbmaj7 */
    shimFloat(21.74, 523.25f, 0.13f, CHORD(277.18f, 392.00f, 523.25f, 698.46f));   /* A7#9 */

    /* BAR 09: Caterpillar's farewell & distant frog call (22.05–24.50s) */
    shimWaddleD(22.05, 73.42f, 0.17f, 0.16f);
    shimPedalBass(22.05, 73.42f, 0.16f, 2.0f);
    shimPulseBell(22.05, 0.85f, 0.561f);
    shimGuitar(22.35, NOTES(N(349.23f, 0.18f, 0.14f, 0.00f), N(392.00f, 0.18f, 0.15f, 0.14f), N(415.30f, 0.20f, 0.16f, 0.28f),
                            N(440.00f, 0.26f, 0.16f, 0.44f), N(587.33f, 0.42f, 0.18f, 0.65f)), 1.0f);
    shimSaxFlute(22.35, NOTES(N(698.46f, 0.18f, 0.12f, 0.00f), N(783.99f, 0.18f, 0.13f, 0.14f), N(830.61f, 0.20f, 0.14f, 0.28f),
                              N(880.00f, 0.26f, 0.14f, 0.44f), N(1174.66f, 0.42f, 0.15f, 0.65f)), 1.0f);
    shimFrogCroak(23.25, 0.95f, 0.85f, 1);    /* Distant frog across the marsh */
    shimDing(23.85, 1760.0f, 0.10f, 0.30f);

    /* BAR 10: Grand cadence & resolution (24.50–28.00s) */
    shimGrandPiano(24.50, 523.25f, 0.16f, 2.8f);
    shimFloat(24.50, 523.25f, 0.18f, CHORD(146.83f, 293.66f, 349.23f, 440.00f, 523.25f, 659.25f, 783.99f));
    shimPedalBass(24.50, 73.42f, 0.18f, 3.2f);
    shimPulseBell(24.85, 1.35f, 0.561f);
    shimPulseSquash(25.35, 0.75f, 1.122f);
    shimDing(25.75, 1760.0f, 0.16f, 1.50f);
    shimDing(26.55, 2349.32f, 0.14f, 2.00f);
}

/* ── TRACK 3: 朝日のあたる家 (House of the Rising Sun) — index.html 5200–5462 ── */

static void playArpeggioBar(double startTime, float rootF, float thirdF, float fifthF, float octF, float bassVol) {
    const double eighth = 0.24;
    shimGrandPiano(startTime + 0.000, rootF, bassVol * 1.1f, (float)(eighth * 2.5));
    shimGrandPiano(startTime + eighth, fifthF, bassVol * 0.9f, (float)(eighth * 2.0));
    shimGrandPiano(startTime + eighth * 2.0, octF, bassVol * 0.95f, (float)(eighth * 2.0));
    shimGrandPiano(startTime + eighth * 3.0, thirdF, bassVol * 0.9f, (float)(eighth * 2.0));
    shimGrandPiano(startTime + eighth * 4.0, fifthF, bassVol * 0.95f, (float)(eighth * 2.0));
    shimGrandPiano(startTime + eighth * 5.0, thirdF, bassVol * 0.85f, (float)(eighth * 2.0));
}

static void playMelodyNote(double startTime, float freq, float durationBeats, float vol, int isOctave) {
    const double eighth = 0.24;
    float dur = (float)(durationBeats * eighth);
    shimGrandPiano(startTime, freq, vol, dur * 1.6f);
    shimPianoHammer(startTime, vol * 1.4f);
    if (isOctave) {
        shimGrandPiano(startTime, freq * 2.0f, vol * 0.72f, dur * 1.3f);
    }
    shimSaxFlute(startTime, NOTES(N(freq, dur * 0.95f, 0.14f, 0.0f)), 1.0f);
}

static void schedule_track3(void) {
    const double eighth = 0.24;
    const double bar = eighth * 6.0;

    /* INTRO: Atmospheric New Orleans Sunrise (0.00s - 2.88s) */
    shimPedalSwoosh(0.00, 1.2f, 0.60f);
    shimPulseBell(0.00, 0.85f, 0.35f);
    shimPedalBass(0.00, 55.00f, 0.18f, 2.8f);

    /* Bar 0 (0.00s, Am) */
    playArpeggioBar(0.000, 110.00f, 261.63f, 164.81f, 220.00f, 0.11f);
    shimDing(0.00, 1760.0f, 0.10f, 0.80f);

    /* Bar 1 (1.44s, C) */
    playArpeggioBar(bar * 1.0, 130.81f, 329.63f, 196.00f, 261.63f, 0.11f);
    shimDing(bar * 1.0, 2637.0f, 0.08f, 0.90f);

    /* VERSE 1: Iconic Vocal Melody (2.88s - 14.40s) */
    /* Bar 2 (2.88s, Am) */
    double b2 = bar * 2.0;
    shimPedalBass(b2, 55.00f, 0.16f, 1.6f);
    playArpeggioBar(b2, 110.00f, 261.63f, 164.81f, 220.00f, 0.10f);
    playMelodyNote(b2 + 0.000, 440.00f, 2.0f, 0.32f, 0);
    playMelodyNote(b2 + eighth * 2.0, 523.25f, 2.0f, 0.32f, 0);
    playMelodyNote(b2 + eighth * 4.0, 587.33f, 2.0f, 0.34f, 0);

    /* Bar 3 (4.32s, C) */
    double b3 = bar * 3.0;
    shimPedalBass(b3, 65.41f, 0.16f, 1.6f);
    playArpeggioBar(b3, 130.81f, 329.63f, 196.00f, 261.63f, 0.10f);
    playMelodyNote(b3 + 0.000, 659.25f, 3.0f, 0.36f, 0);
    playMelodyNote(b3 + eighth * 3.0, 659.25f, 1.0f, 0.30f, 0);
    playMelodyNote(b3 + eighth * 4.0, 587.33f, 2.0f, 0.34f, 0);

    /* Bar 4 (5.76s, D) */
    double b4 = bar * 4.0;
    shimPedalBass(b4, 73.42f, 0.16f, 1.6f);
    playArpeggioBar(b4, 146.83f, 369.99f, 220.00f, 293.66f, 0.10f);
    playMelodyNote(b4 + 0.000, 587.33f, 3.0f, 0.34f, 0);
    playMelodyNote(b4 + eighth * 3.0, 369.99f, 3.0f, 0.32f, 0);

    /* Bar 5 (7.20s, F) */
    double b5 = bar * 5.0;
    shimPedalBass(b5, 87.31f, 0.16f, 1.6f);
    playArpeggioBar(b5, 174.61f, 349.23f, 261.63f, 349.23f, 0.10f);
    playMelodyNote(b5 + 0.000, 349.23f, 2.0f, 0.30f, 0);
    playMelodyNote(b5 + eighth * 2.0, 440.00f, 2.0f, 0.32f, 0);
    playMelodyNote(b5 + eighth * 4.0, 523.25f, 2.0f, 0.34f, 0);

    /* Bar 6 (8.64s, Am) */
    double b6 = bar * 6.0;
    shimPedalBass(b6, 55.00f, 0.16f, 1.6f);
    playArpeggioBar(b6, 110.00f, 261.63f, 164.81f, 220.00f, 0.10f);
    playMelodyNote(b6 + 0.000, 440.00f, 2.0f, 0.32f, 0);
    playMelodyNote(b6 + eighth * 2.0, 523.25f, 2.0f, 0.32f, 0);
    playMelodyNote(b6 + eighth * 4.0, 587.33f, 2.0f, 0.34f, 0);

    /* Bar 7 (10.08s, C) */
    double b7 = bar * 7.0;
    shimPedalBass(b7, 65.41f, 0.16f, 1.6f);
    playArpeggioBar(b7, 130.81f, 329.63f, 196.00f, 261.63f, 0.10f);
    playMelodyNote(b7 + 0.000, 659.25f, 3.0f, 0.36f, 0);
    playMelodyNote(b7 + eighth * 3.0, 659.25f, 3.0f, 0.34f, 0);

    /* Bar 8 (11.52s, E) */
    double b8 = bar * 8.0;
    shimPedalBass(b8, 82.41f, 0.18f, 2.0f);
    playArpeggioBar(b8, 164.81f, 415.30f, 246.94f, 329.63f, 0.11f);
    playMelodyNote(b8 + 0.000, 493.88f, 3.5f, 0.35f, 0);
    playMelodyNote(b8 + eighth * 4.0, 415.30f, 2.0f, 0.32f, 0);

    /* Bar 9 (12.96s, E7) */
    double b9 = bar * 9.0;
    shimPedalSwoosh(b9, 1.2f, 0.50f);
    playArpeggioBar(b9, 164.81f, 415.30f, 293.66f, 329.63f, 0.12f);
    playMelodyNote(b9 + 0.000, 493.88f, 2.0f, 0.33f, 0);
    playMelodyNote(b9 + eighth * 2.0, 587.33f, 2.0f, 0.34f, 0);
    playMelodyNote(b9 + eighth * 4.0, 659.25f, 2.0f, 0.36f, 0);
    shimDing(b9 + eighth * 4.0, 2637.0f, 0.10f, 0.30f);

    /* VERSE 2: Thomas Krüger Forte Octave Chorus (14.40s - 23.04s) */
    /* Bar 10 (14.40s, Am) */
    double b10 = bar * 10.0;
    shimPianoHammer(b10, 1.4f);
    shimPedalBass(b10, 55.00f, 0.24f, 2.4f);
    playArpeggioBar(b10, 110.00f, 261.63f, 164.81f, 220.00f, 0.12f);
    playMelodyNote(b10 + 0.000, 659.25f, 2.0f, 0.38f, 1);
    playMelodyNote(b10 + eighth * 2.0, 659.25f, 2.0f, 0.38f, 1);
    playMelodyNote(b10 + eighth * 4.0, 587.33f, 2.0f, 0.36f, 1);

    /* Bar 11 (15.84s, C) */
    double b11 = bar * 11.0;
    shimPedalBass(b11, 65.41f, 0.22f, 2.2f);
    playArpeggioBar(b11, 130.81f, 329.63f, 196.00f, 261.63f, 0.12f);
    playMelodyNote(b11 + 0.000, 523.25f, 2.0f, 0.36f, 1);
    playMelodyNote(b11 + eighth * 2.0, 587.33f, 2.0f, 0.36f, 1);
    playMelodyNote(b11 + eighth * 4.0, 659.25f, 2.0f, 0.38f, 1);

    /* Bar 12 (17.28s, D) */
    double b12 = bar * 12.0;
    shimPedalBass(b12, 73.42f, 0.22f, 2.2f);
    playArpeggioBar(b12, 146.83f, 369.99f, 220.00f, 293.66f, 0.12f);
    playMelodyNote(b12 + 0.000, 587.33f, 3.0f, 0.38f, 1);
    playMelodyNote(b12 + eighth * 3.0, 739.99f, 1.5f, 0.36f, 1);
    playMelodyNote(b12 + eighth * 4.5, 880.00f, 1.5f, 0.40f, 1);

    /* Bar 13 (18.72s, F) */
    double b13 = bar * 13.0;
    shimPedalBass(b13, 87.31f, 0.22f, 2.0f);
    playArpeggioBar(b13, 174.61f, 349.23f, 261.63f, 349.23f, 0.12f);
    playMelodyNote(b13 + 0.000, 880.00f, 2.0f, 0.38f, 1);
    playMelodyNote(b13 + eighth * 2.0, 698.46f, 2.0f, 0.36f, 1);
    playMelodyNote(b13 + eighth * 4.0, 587.33f, 2.0f, 0.35f, 1);

    /* Bar 14 (20.16s, Am) */
    double b14 = bar * 14.0;
    shimPedalBass(b14, 55.00f, 0.22f, 2.2f);
    playArpeggioBar(b14, 110.00f, 261.63f, 164.81f, 220.00f, 0.12f);
    playMelodyNote(b14 + 0.000, 659.25f, 2.0f, 0.36f, 1);
    playMelodyNote(b14 + eighth * 2.0, 523.25f, 2.0f, 0.35f, 1);
    playMelodyNote(b14 + eighth * 4.0, 440.00f, 2.0f, 0.36f, 1);

    /* Bar 15 (21.60s, E7) */
    double b15 = bar * 15.0;
    shimPedalBass(b15, 82.41f, 0.22f, 2.0f);
    playArpeggioBar(b15, 164.81f, 415.30f, 293.66f, 329.63f, 0.12f);
    playMelodyNote(b15 + 0.000, 493.88f, 3.0f, 0.36f, 1);
    playMelodyNote(b15 + eighth * 3.0, 415.30f, 3.0f, 0.36f, 1);

    /* CLIMAX: "...ONE!" & High Waterfall Cascade (23.04s - 26.50s) */
    /* Bar 16 (23.04s, Am) */
    double b16 = bar * 16.0;
    shimPianoHammer(b16, 1.5f);
    shimPedalBass(b16, 55.00f, 0.25f, 2.5f);
    playMelodyNote(b16 + 0.000, 440.00f, 2.0f, 0.40f, 1);

    shimGrandPiano(b16 + eighth * 1.5, 1318.51f, 0.22f, 0.35f);
    shimGrandPiano(b16 + eighth * 2.2, 1174.66f, 0.22f, 0.35f);
    shimGrandPiano(b16 + eighth * 2.9, 1046.50f, 0.22f, 0.35f);
    shimGrandPiano(b16 + eighth * 3.6,  987.77f, 0.22f, 0.35f);
    shimGrandPiano(b16 + eighth * 4.3,  880.00f, 0.22f, 0.35f);
    shimGrandPiano(b16 + eighth * 5.0,  830.61f, 0.22f, 0.35f);
    shimGrandPiano(b16 + eighth * 5.6,  880.00f, 0.22f, 0.35f);
    shimDing(b16 + eighth * 1.5, 2637.0f, 0.12f, 0.80f);

    /* Bar 17 (24.48s - 26.50s) Grand Sunrise Cadence */
    double b17 = bar * 17.0;
    shimPedalSwoosh(b17, 1.5f, 0.80f);
    shimPianoHammer(b17, 1.5f);
    shimPedalBass(b17, 27.50f, 0.25f, 3.5f);
    shimPedalBass(b17, 55.00f, 0.24f, 3.2f);
    shimGrandPiano(b17, 110.00f, 0.24f, 1.8f);
    shimWaddleD(b17, 55.00f, 0.20f, 0.22f);
    shimWaddleD(b17, 110.00f, 0.18f, 0.18f);

    shimGrandPiano(b17 + 0.10, 440.00f, 0.24f, 2.5f);
    shimFloatEx(b17 + 0.10, 523.25f, 0.18f,
                CHORD(110.00f, 164.81f, 220.00f, 246.94f, 261.63f, 329.63f, 440.00f, 659.25f),
                0.055f, 2.0f);
    shimPulseBell(b17 + 0.40, 1.35f, 0.421f);
    shimPulseSquash(b17 + 0.90, 0.75f, 0.841f);
    shimDing(b17 + 1.20, 1760.0f, 0.16f, 1.60f);
    shimDing(b17 + 1.80, 2637.0f, 0.14f, 2.20f);
}

/* ── TRACK 4: TRON Init (Nine Inch Nails / Trent Reznor) — index.html 5463–5819 ── */

static void schedule_track4(void) {
    /* MEASURE 01: System Init & Harsh Distorted Shockwave (0.00s - 2.58s) */
    shimGlitchZap(0.00, 3200.0f, 1.3f, 0.12f);
    shimIndustrialKickEx(0.00, 1.4f, 0.35f);
    shimSwarmatron(0.05, 55.0f, 3.8f, 0.22f, 32.0f);
    shimPedalBass(0.00, 55.00f, 0.20f, 2.5f);

    shimGlitchZap(0.64, 4200.0f, 0.8f, 0.06f);
    shimPulseSquash(0.64, 0.6f, 0.5f);

    shimGlitchZap(1.29, 2800.0f, 0.9f, 0.07f);
    shimPulseBell(1.29, 0.8f, 0.5f);

    shimGlitchZap(1.93, 4800.0f, 1.0f, 0.08f);
    shimIndustrialSnare(2.25, 0.14f, 1);
    shimIndustrialSnare(2.41, 0.20f, 1);

    /* MEASURE 02: 93 BPM Industrial Groove Ignites (2.58s - 5.16s) */
    shimIndustrialKick(2.58, 1.1f);
    shimDing(2.58, 1760.0f, 0.10f, 0.15f);
    shimIndustrialBass(2.58, 55.00f, 1.0f, 0.22f);
    shimIndustrialBass(2.74, 55.00f, 1.0f, 0.18f);
    shimHiHat(2.74, 0, 0.10f);
    shimIndustrialBass(2.90, 65.41f, 1.0f, 0.20f);
    shimIndustrialBass(3.06, 55.00f, 1.0f, 0.18f);
    shimHiHat(3.06, 0, 0.10f);

    shimIndustrialSnare(3.22, 0.24f, 1);
    shimIndustrialBass(3.22, 73.42f, 1.0f, 0.22f);
    shimIndustrialBass(3.38, 65.41f, 1.0f, 0.20f);
    shimHiHat(3.38, 0, 0.10f);
    shimIndustrialBass(3.55, 55.00f, 1.0f, 0.18f);
    shimIndustrialBass(3.71, 49.00f, 1.0f, 0.18f);
    shimHiHat(3.71, 0, 0.10f);

    shimIndustrialKick(3.87, 1.0f);
    shimIndustrialBass(3.87, 55.00f, 1.0f, 0.22f);
    shimIndustrialBass(4.03, 55.00f, 1.0f, 0.18f);
    shimHiHat(4.03, 0, 0.10f);
    shimIndustrialBass(4.19, 65.41f, 1.0f, 0.20f);
    shimIndustrialBass(4.35, 55.00f, 1.0f, 0.18f);
    shimHiHat(4.35, 0, 0.10f);

    shimIndustrialSnare(4.51, 0.24f, 1);
    shimIndustrialBass(4.51, 43.65f, 1.0f, 0.22f);
    shimIndustrialBass(4.67, 49.00f, 1.0f, 0.20f);
    shimHiHat(4.67, 0, 0.10f);
    shimIndustrialBass(4.84, 55.00f, 1.0f, 0.22f);
    shimIndustrialBassSlide(5.00, 55.00f, 1.0f, 0.24f, 65.41f);
    shimHiHat(5.00, 1, 0.12f);

    /* MEASURE 03: Driving Industrial Acceleration & Glitch Data Fills (5.16s - 7.74s) */
    shimIndustrialKick(5.16, 1.1f);
    shimDing(5.16, 1760.0f, 0.10f, 0.15f);
    shimIndustrialBass(5.16, 55.00f, 1.0f, 0.22f);
    shimIndustrialBass(5.32, 55.00f, 1.0f, 0.18f);
    shimHiHat(5.32, 0, 0.10f);
    shimIndustrialBass(5.48, 65.41f, 1.0f, 0.20f);
    shimIndustrialBass(5.64, 55.00f, 1.0f, 0.18f);

    shimIndustrialSnare(5.80, 0.24f, 1);
    shimGlitchZap(5.80, 2400.0f, 0.16f, 0.06f);
    shimIndustrialBass(5.80, 73.42f, 1.0f, 0.22f);
    shimIndustrialBass(5.96, 65.41f, 1.0f, 0.20f);
    shimHiHat(5.96, 0, 0.10f);

    shimIndustrialKick(6.13, 0.95f);
    shimIndustrialBass(6.13, 55.00f, 1.0f, 0.20f);
    shimIndustrialBass(6.29, 49.00f, 1.0f, 0.18f);

    shimIndustrialKick(6.45, 1.1f);
    shimIndustrialBass(6.45, 55.00f, 1.0f, 0.22f);
    shimIndustrialBass(6.61, 55.00f, 1.0f, 0.18f);
    shimHiHat(6.61, 0, 0.10f);
    shimGlitchZap(6.77, 3600.0f, 0.16f, 0.07f);
    shimIndustrialBass(6.77, 65.41f, 1.0f, 0.20f);
    shimIndustrialBass(6.93, 55.00f, 1.0f, 0.18f);

    shimIndustrialSnare(7.10, 0.24f, 1);
    shimIndustrialBass(7.10, 43.65f, 1.0f, 0.22f);
    shimIndustrialBass(7.26, 49.00f, 1.0f, 0.20f);
    shimGlitchZap(7.42, 4800.0f, 0.18f, 0.08f);
    shimIndustrialBass(7.42, 55.00f, 1.0f, 0.22f);
    shimIndustrialBassSlide(7.58, 55.00f, 1.0f, 0.24f, 73.42f);
    shimHiHat(7.58, 1, 0.12f);

    /* MEASURE 04: Swarmatron Ascension & Cyber Lead Teaser (7.74s - 10.32s) */
    shimSwarmatron(7.74, 110.0f, 3.2f, 0.22f, 28.0f);
    shimIndustrialKick(7.74, 1.1f);
    shimDing(7.74, 1760.0f, 0.11f, 0.20f);
    shimIndustrialBass(7.74, 55.00f, 1.0f, 0.22f);
    shimIndustrialBass(7.90, 55.00f, 1.0f, 0.18f);
    shimHiHat(7.90, 0, 0.10f);
    shimIndustrialBass(8.06, 65.41f, 1.0f, 0.20f);
    shimIndustrialBass(8.22, 55.00f, 1.0f, 0.18f);

    shimIndustrialSnare(8.38, 0.24f, 1);
    shimIndustrialBass(8.38, 73.42f, 1.0f, 0.22f);
    shimIndustrialBass(8.54, 65.41f, 1.0f, 0.20f);
    shimHiHat(8.54, 0, 0.10f);
    shimIndustrialBass(8.70, 55.00f, 1.0f, 0.18f);
    shimIndustrialBass(8.86, 49.00f, 1.0f, 0.18f);

    shimIndustrialKick(9.03, 1.05f);
    shimIndustrialBass(9.03, 55.00f, 1.0f, 0.22f);
    shimIndustrialBass(9.19, 55.00f, 1.0f, 0.18f);
    shimCyberLead(9.35, NOTES(N(440.00f, 0.18f, 0.16f, 0.0f), N(523.25f, 0.22f, 0.18f, 0.16f)), 1.0f);
    shimIndustrialBass(9.35, 65.41f, 1.0f, 0.20f);
    shimIndustrialBass(9.51, 55.00f, 1.0f, 0.18f);

    shimIndustrialSnare(9.68, 0.24f, 1);
    shimIndustrialBass(9.68, 43.65f, 1.0f, 0.22f);
    shimIndustrialBass(9.84, 49.00f, 1.0f, 0.20f);
    shimIndustrialSnare(10.00, 0.16f, 0);
    shimIndustrialSnare(10.16, 0.22f, 1);
    shimHiHat(10.16, 1, 0.14f);

    /* MEASURE 05: The Searing TRON: Ares Cyber Lead Drops! (10.32s - 12.90s) */
    shimIndustrialKick(10.32, 1.2f);
    shimDing(10.32, 1760.0f, 0.12f, 0.25f);
    shimCyberLead(10.32, NOTES(
        N(440.00f, 0.60f, 0.22f, 0.00f),
        N(523.25f, 0.30f, 0.20f, 0.64f),
        N(493.88f, 0.30f, 0.20f, 0.96f),
        N(392.00f, 0.30f, 0.20f, 1.29f),
        N(440.00f, 0.30f, 0.22f, 1.61f),
        NS(659.25f, 0.55f, 0.24f, 1.93f, 698.46f)
    ), 1.0f);

    shimIndustrialBass(10.32, 55.00f, 1.0f, 0.22f);
    shimIndustrialBass(10.48, 55.00f, 1.0f, 0.18f);
    shimHiHat(10.48, 0, 0.10f);
    shimIndustrialBass(10.64, 65.41f, 1.0f, 0.20f);
    shimIndustrialBass(10.80, 55.00f, 1.0f, 0.18f);

    shimIndustrialSnare(10.96, 0.24f, 1);
    shimIndustrialBass(10.96, 73.42f, 1.0f, 0.22f);
    shimIndustrialBass(11.12, 65.41f, 1.0f, 0.20f);
    shimHiHat(11.12, 0, 0.10f);
    shimIndustrialBass(11.28, 55.00f, 1.0f, 0.18f);
    shimIndustrialBass(11.44, 49.00f, 1.0f, 0.18f);

    shimIndustrialKick(11.61, 1.1f);
    shimIndustrialBass(11.61, 55.00f, 1.0f, 0.22f);
    shimIndustrialBass(11.77, 55.00f, 1.0f, 0.18f);
    shimHiHat(11.77, 0, 0.10f);
    shimIndustrialBass(11.93, 65.41f, 1.0f, 0.20f);
    shimIndustrialBass(12.09, 55.00f, 1.0f, 0.18f);

    shimIndustrialSnare(12.25, 0.24f, 1);
    shimIndustrialBass(12.25, 43.65f, 1.0f, 0.22f);
    shimIndustrialBass(12.41, 49.00f, 1.0f, 0.20f);
    shimHiHat(12.41, 0, 0.10f);
    shimIndustrialBass(12.57, 55.00f, 1.0f, 0.22f);
    shimIndustrialBassSlide(12.73, 55.00f, 1.0f, 0.24f, 65.41f);
    shimHiHat(12.73, 1, 0.12f);

    /* MEASURE 06: Theme Resolution & Harmonic Expansion (12.90s - 15.48s) */
    shimSwarmatron(12.90, 165.0f, 3.2f, 0.20f, 30.0f);
    shimIndustrialKick(12.90, 1.15f);
    shimDing(12.90, 1760.0f, 0.11f, 0.20f);
    shimCyberLead(12.90, NOTES(
        N(587.33f, 0.60f, 0.22f, 0.00f),
        N(523.25f, 0.60f, 0.20f, 0.64f),
        N(587.33f, 0.60f, 0.22f, 1.29f),
        NS(659.25f, 0.60f, 0.24f, 1.93f, 880.00f)
    ), 1.0f);

    shimIndustrialBass(12.90, 73.42f, 1.0f, 0.22f);
    shimIndustrialBass(13.06, 73.42f, 1.0f, 0.18f);
    shimHiHat(13.06, 0, 0.10f);
    shimIndustrialBass(13.22, 65.41f, 1.0f, 0.20f);
    shimIndustrialBass(13.38, 55.00f, 1.0f, 0.18f);

    shimIndustrialSnare(13.54, 0.24f, 1);
    shimIndustrialBass(13.54, 65.41f, 1.0f, 0.20f);
    shimHiHat(13.70, 0, 0.10f);
    shimIndustrialBass(13.86, 49.00f, 1.0f, 0.18f);
    shimIndustrialBass(14.02, 55.00f, 1.0f, 0.18f);

    shimIndustrialKick(14.19, 1.1f);
    shimIndustrialBass(14.19, 73.42f, 1.0f, 0.22f);
    shimIndustrialBass(14.35, 73.42f, 1.0f, 0.18f);
    shimHiHat(14.35, 0, 0.10f);
    shimIndustrialBass(14.51, 82.41f, 1.0f, 0.20f);
    shimIndustrialBass(14.67, 73.42f, 1.0f, 0.18f);

    shimIndustrialSnare(14.83, 0.26f, 1);
    shimIndustrialBass(14.83, 82.41f, 1.0f, 0.22f);
    shimIndustrialBass(14.99, 98.00f, 1.0f, 0.20f);
    shimHiHat(14.99, 0, 0.10f);
    shimIndustrialSnare(15.15, 0.16f, 0);
    shimIndustrialSnare(15.31, 0.22f, 1);
    shimHiHat(15.31, 1, 0.14f);

    /* MEASURE 07: Full Industrial Climax / Fortissimo Octaves (15.48s - 18.06s) */
    shimSwarmatron(15.48, 220.0f, 3.4f, 0.25f, 36.0f);
    shimIndustrialKick(15.48, 1.3f);
    shimDing(15.48, 2640.0f, 0.14f, 0.30f);
    shimPedalBass(15.48, 55.00f, 0.24f, 2.5f);
    shimCyberLead(15.48, NOTES(
        N(880.00f, 0.60f, 0.26f, 0.00f),
        N(440.00f, 0.60f, 0.20f, 0.00f),
        N(1046.50f, 0.60f, 0.26f, 0.64f),
        N(523.25f, 0.60f, 0.20f, 0.64f),
        N(1174.66f, 0.60f, 0.28f, 1.29f),
        N(587.33f, 0.60f, 0.21f, 1.29f),
        N(1318.51f, 0.60f, 0.30f, 1.93f),
        N(659.25f, 0.60f, 0.22f, 1.93f)
    ), 1.0f);

    shimIndustrialBass(15.48, 55.00f, 1.0f, 0.26f);
    shimIndustrialKick(15.80, 1.0f);
    shimIndustrialBass(15.80, 55.00f, 1.0f, 0.22f);
    shimHiHat(15.80, 0, 0.12f);

    shimIndustrialSnare(16.12, 0.26f, 1);
    shimIndustrialBass(16.12, 65.41f, 1.0f, 0.24f);
    shimHiHat(16.44, 0, 0.12f);
    shimIndustrialBass(16.44, 55.00f, 1.0f, 0.22f);

    shimIndustrialKick(16.77, 1.25f);
    shimIndustrialKick(17.10, 1.0f);
    shimIndustrialBass(16.77, 73.42f, 1.0f, 0.24f);
    shimIndustrialBass(17.10, 65.41f, 1.0f, 0.22f);
    shimHiHat(17.10, 0, 0.12f);

    shimIndustrialSnare(17.42, 0.28f, 1);
    shimIndustrialBass(17.42, 82.41f, 1.0f, 0.26f);
    shimHiHat(17.74, 1, 0.14f);
    shimIndustrialBass(17.74, 98.00f, 1.0f, 0.24f);

    /* MEASURE 08: Climax Cascade & Digital Stutter Fills (18.06s - 20.64s) */
    shimIndustrialKick(18.06, 1.2f);
    shimDing(18.06, 2640.0f, 0.12f, 0.25f);
    shimCyberLead(18.06, NOTES(
        N(1567.98f, 0.60f, 0.28f, 0.00f),
        N(783.99f, 0.60f, 0.20f, 0.00f),
        N(1318.51f, 0.60f, 0.26f, 0.64f),
        N(659.25f, 0.60f, 0.20f, 0.64f),
        N(1174.66f, 0.60f, 0.24f, 1.29f),
        N(587.33f, 0.60f, 0.19f, 1.29f),
        NS(1046.50f, 0.60f, 0.24f, 1.93f, 880.00f),
        NS(523.25f, 0.60f, 0.18f, 1.93f, 440.00f)
    ), 1.0f);

    shimIndustrialBass(18.06, 98.00f, 1.0f, 0.24f);
    shimIndustrialBass(18.22, 98.00f, 1.0f, 0.20f);
    shimHiHat(18.22, 0, 0.12f);
    shimIndustrialBass(18.38, 82.41f, 1.0f, 0.22f);
    shimIndustrialBass(18.54, 73.42f, 1.0f, 0.20f);

    shimIndustrialSnare(18.70, 0.26f, 1);
    shimIndustrialBass(18.70, 82.41f, 1.0f, 0.24f);
    shimIndustrialBass(18.86, 73.42f, 1.0f, 0.20f);
    shimHiHat(18.86, 0, 0.12f);
    shimIndustrialBass(19.02, 65.41f, 1.0f, 0.20f);
    shimIndustrialBass(19.18, 55.00f, 1.0f, 0.20f);

    shimIndustrialKick(19.35, 1.15f);
    shimIndustrialBass(19.35, 73.42f, 1.0f, 0.22f);
    shimIndustrialBass(19.51, 65.41f, 1.0f, 0.20f);
    shimHiHat(19.51, 0, 0.12f);
    shimIndustrialBass(19.67, 55.00f, 1.0f, 0.20f);
    shimIndustrialBass(19.83, 49.00f, 1.0f, 0.20f);

    /* Digital Stutter Glitch Bursts (19.99s - 20.64s) */
    shimGlitchZap(19.99, 4800.0f, 0.20f, 0.04f);
    shimGlitchZap(20.15, 3600.0f, 0.20f, 0.04f);
    shimGlitchZap(20.31, 2400.0f, 0.22f, 0.04f);
    shimGlitchZap(20.47, 5200.0f, 0.25f, 0.06f);
    shimIndustrialSnare(20.47, 0.26f, 1);

    /* MEASURE 09: Ares Terminal Execution & Sub-Bass Breakdown (20.64s - 23.22s) */
    shimPedalBass(20.64, 27.50f, 0.30f, 4.5f);
    shimPedalBass(20.64, 55.00f, 0.26f, 4.0f);
    shimSwarmatron(20.70, 55.0f, 3.8f, 0.18f, 6.0f);
    shimPulseBell(20.64, 1.4f, 0.50f);

    shimGlitchZap(21.28, 1600.0f, 0.14f, 0.07f);
    shimGlitchZap(21.93, 2400.0f, 0.12f, 0.06f);
    shimGlitchZap(22.57, 3200.0f, 0.10f, 0.05f);

    /* MEASURE 10: Grid Convergence, Pure Silence & Resolution (23.22s - 25.80s) */
    shimGlitchZap(23.22, 4800.0f, 0.10f, 0.04f);
    shimDing(23.80, 1760.0f, 0.12f, 1.80f);
    shimDing(24.45, 2637.0f, 0.09f, 1.40f);
}

/* ── Public API ───────────────────────────────────────────────────── */

double lc_score_length(int track) {
    switch (track) {
        case 1: return 25.8;   /* scheduleTrackTimeout(loop, 25800) */
        case 2: return 28.0;   /* scheduleTrackTimeout(loop, 28000) */
        case 3: return 26.8;   /* scheduleTrackTimeout(loop, 26800) */
        case 4: return 25.8;   /* scheduleTrackTimeout(loop, 25800) */
        default: return 0.0;
    }
}

void lc_score_schedule(int track, double t0) {
    s_T = t0;
    if (track == 1) schedule_track1();
    else if (track == 2) schedule_track2();
    else if (track == 3) schedule_track3();
    else if (track == 4) schedule_track4();
}
