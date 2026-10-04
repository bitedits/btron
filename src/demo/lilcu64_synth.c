/*
 * src/demo/lilcu64_synth.c — Lil Cu 64 Procedural DSP Sound Engine & VirtIO Audio
 *
 * Fully autonomous C99 procedural synthesizer implementing 0-byte footprint
 * sound shims and 4-track tracker sequencer for B-System Demoscene.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "lilcu64_synth.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif

/* External VirtIO sound drivers */
extern int  virtio_sound_open(uint32_t sample_rate, uint8_t channels);
extern int  virtio_sound_write(const int16_t *samples, size_t frames);
extern void virtio_sound_close(void);
extern bool virtio_sound_is_ready(void);

#define MAX_VOICES 48
#define VOICE_WAVE_SINE     0
#define VOICE_WAVE_TRIANGLE 1
#define VOICE_WAVE_SAW      2
#define VOICE_WAVE_PULSE    3
#define VOICE_WAVE_NOISE    4
#define VOICE_WAVE_FM_BELL  5

typedef struct {
    bool   active;
    int    wave_type;
    float  phase;
    float  phase_inc;
    float  freq;
    float  target_freq;
    float  freq_ramp_rate;
    float  amp;
    float  decay_rate;
    float  pan;         /* -1.0 (Left) to +1.0 (Right) */
    float  fm_ratio;
    float  fm_index;
    float  fm_phase;
    /* 1-pole filter state */
    float  filter_lp;
    float  filter_coeff;
    /* AM pulse state for frog croak */
    float  am_freq;
    float  am_phase;
    float  am_depth;
} SynthVoice;

static SynthVoice s_voices[MAX_VOICES];
static uint32_t   s_sample_rate = LILCU64_AUDIO_RATE;
static bool       s_muted = false;
static int        s_active_track = 0;
static float      s_track_time = 0.0f;
static float      s_next_step_time = 0.0f;
static int        s_track_step = 0;
static bool       s_virtio_opened = false;

/* PCM double buffer for VirtIO pump */
#define PUMP_CHUNK_FRAMES 1024
static int16_t s_pcm_chunk[PUMP_CHUNK_FRAMES * 2];

/* ── Voice Allocation & Lifecycle ─────────────────────────────────── */

static SynthVoice* alloc_voice(void) {
    /* Find inactive voice */
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!s_voices[i].active) {
            memset(&s_voices[i], 0, sizeof(SynthVoice));
            s_voices[i].active = true;
            s_voices[i].pan = 0.0f;
            return &s_voices[i];
        }
    }
    /* Fallback: steal voice with lowest amplitude */
    int steal_idx = 0;
    float min_amp = 999.0f;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (s_voices[i].amp < min_amp) {
            min_amp = s_voices[i].amp;
            steal_idx = i;
        }
    }
    memset(&s_voices[steal_idx], 0, sizeof(SynthVoice));
    s_voices[steal_idx].active = true;
    s_voices[steal_idx].pan = 0.0f;
    return &s_voices[steal_idx];
}

static void trigger_voice_basic(int wave_type, float freq, float amp, float decay_time, float pan) {
    if (s_muted || amp <= 0.0001f) return;
    SynthVoice *v = alloc_voice();
    v->wave_type = wave_type;
    v->freq = freq;
    v->target_freq = freq;
    v->phase_inc = (float)(2.0 * M_PI * freq / (double)s_sample_rate);
    v->amp = amp;
    v->decay_rate = (decay_time > 0.005f) ? (1.0f / (decay_time * (float)s_sample_rate)) : 0.01f;
    v->pan = pan;
}

/* ── Procedural Sound Shims ───────────────────────────────────────── */

void lilcu64_shim_pulse_squash(float vol_mul, float pitch_mul) {
    if (s_muted) return;
    static const float freqs[] = { 261.63f, 523.25f, 659.25f, 783.99f, 1046.50f, 1318.51f };
    static const float amps[]  = { 0.055f, 0.070f, 0.048f, 0.052f, 0.040f, 0.025f };
    static const float decs[]  = { 1.8f, 1.9f, 1.6f, 1.7f, 1.4f, 1.1f };

    for (int i = 0; i < 6; i++) {
        SynthVoice *v = alloc_voice();
        v->wave_type = VOICE_WAVE_SINE;
        v->freq = freqs[i] * pitch_mul * 0.97f;
        v->target_freq = freqs[i] * pitch_mul;
        v->freq_ramp_rate = 0.0008f;
        v->phase_inc = (float)(2.0 * M_PI * v->freq / (double)s_sample_rate);
        v->amp = amps[i] * vol_mul;
        v->decay_rate = 1.0f / (decs[i] * (float)s_sample_rate);
        v->pan = (i % 2 == 0) ? -0.15f : 0.15f;
    }
}

void lilcu64_shim_frog_croak(float pitch_mul, float vol_mul) {
    if (s_muted) return;
    float base_f = 110.0f * pitch_mul;
    SynthVoice *v = alloc_voice();
    v->wave_type = VOICE_WAVE_SAW;
    v->freq = base_f * 0.88f;
    v->target_freq = base_f * 1.25f;
    v->freq_ramp_rate = 0.0015f;
    v->phase_inc = (float)(2.0 * M_PI * v->freq / (double)s_sample_rate);
    v->amp = 0.22f * vol_mul;
    v->decay_rate = 1.0f / (0.28f * (float)s_sample_rate);
    v->am_freq = 32.0f;
    v->am_depth = 0.85f;
    v->filter_coeff = 0.35f; /* Lowpass throat formant */
}

void lilcu64_shim_pulse_bell(float vol_mul, float pitch_mul) {
    if (s_muted) return;
    SynthVoice *v = alloc_voice();
    v->wave_type = VOICE_WAVE_FM_BELL;
    v->freq = 440.0f * pitch_mul;
    v->phase_inc = (float)(2.0 * M_PI * v->freq / (double)s_sample_rate);
    v->fm_ratio = 2.76f;
    v->fm_index = 2.2f;
    v->amp = 0.20f * vol_mul;
    v->decay_rate = 1.0f / (1.8f * (float)s_sample_rate);
}

void lilcu64_shim_waddle(float pitch, float vol_mul) {
    if (s_muted) return;
    /* Dual triangle with acoustic lowpass filter */
    SynthVoice *v1 = alloc_voice();
    v1->wave_type = VOICE_WAVE_TRIANGLE;
    v1->freq = pitch;
    v1->phase_inc = (float)(2.0 * M_PI * pitch / (double)s_sample_rate);
    v1->amp = 0.28f * vol_mul;
    v1->decay_rate = 1.0f / (0.12f * (float)s_sample_rate);
    v1->filter_coeff = 0.25f;

    SynthVoice *v2 = alloc_voice();
    v2->wave_type = VOICE_WAVE_SINE;
    v2->freq = pitch * 0.5f; /* Sub-octave */
    v2->phase_inc = (float)(2.0 * M_PI * v2->freq / (double)s_sample_rate);
    v2->amp = 0.20f * vol_mul;
    v2->decay_rate = 1.0f / (0.16f * (float)s_sample_rate);
}

void lilcu64_shim_float(float base_freq, float vol_mul) {
    if (s_muted) return;
    /* Fender Rhodes rolling chord (Minor 9th harmonics) */
    float offsets[] = { 1.0f, 1.1892f, 1.4983f, 1.7818f }; /* Root, m3, 5th, m7 */
    for (int i = 0; i < 4; i++) {
        SynthVoice *v = alloc_voice();
        v->wave_type = VOICE_WAVE_SINE;
        v->freq = base_freq * offsets[i];
        v->phase_inc = (float)(2.0 * M_PI * v->freq / (double)s_sample_rate);
        v->amp = 0.08f * vol_mul;
        v->decay_rate = 1.0f / ((0.9f + i * 0.15f) * (float)s_sample_rate);
        v->pan = (i - 1.5f) * 0.35f;
    }
}

void lilcu64_shim_dance(float pitch_scale, float vol_mul) {
    if (s_muted) return;
    /* Star arpeggio chimes */
    trigger_voice_basic(VOICE_WAVE_SINE, 880.0f * pitch_scale, 0.18f * vol_mul, 0.25f, 0.2f);
    trigger_voice_basic(VOICE_WAVE_TRIANGLE, 1760.0f * pitch_scale, 0.08f * vol_mul, 0.18f, -0.2f);
}

void lilcu64_shim_ding(float freq, float vol_mul, float decay) {
    if (s_muted) return;
    SynthVoice *v = alloc_voice();
    v->wave_type = VOICE_WAVE_SINE;
    v->freq = freq;
    v->phase_inc = (float)(2.0 * M_PI * freq / (double)s_sample_rate);
    v->amp = 0.16f * vol_mul;
    v->decay_rate = 1.0f / (decay * (float)s_sample_rate);
}

void lilcu64_shim_boing(void) {
    if (s_muted) return;
    SynthVoice *v = alloc_voice();
    v->wave_type = VOICE_WAVE_SINE;
    v->freq = 140.0f;
    v->target_freq = 420.0f;
    v->freq_ramp_rate = 0.003f;
    v->phase_inc = (float)(2.0 * M_PI * v->freq / (double)s_sample_rate);
    v->amp = 0.25f;
    v->decay_rate = 1.0f / (0.35f * (float)s_sample_rate);
}

void lilcu64_shim_footstep(void) {
    if (s_muted) return;
    trigger_voice_basic(VOICE_WAVE_TRIANGLE, 95.0f, 0.14f, 0.06f, 0.0f);
}

void lilcu64_shim_chirp(void) {
    lilcu64_shim_pulse_squash(1.0f, 1.0f);
}

/* Drum shims */
static void drum_kick(float vol) {
    if (s_muted) return;
    SynthVoice *v = alloc_voice();
    v->wave_type = VOICE_WAVE_SINE;
    v->freq = 120.0f;
    v->target_freq = 45.0f;
    v->freq_ramp_rate = -0.004f;
    v->phase_inc = (float)(2.0 * M_PI * v->freq / (double)s_sample_rate);
    v->amp = 0.32f * vol;
    v->decay_rate = 1.0f / (0.16f * (float)s_sample_rate);
}

static void drum_snare(float vol) {
    if (s_muted) return;
    /* Tone burst + noise */
    SynthVoice *v1 = alloc_voice();
    v1->wave_type = VOICE_WAVE_TRIANGLE;
    v1->freq = 175.0f;
    v1->phase_inc = (float)(2.0 * M_PI * 175.0f / (double)s_sample_rate);
    v1->amp = 0.20f * vol;
    v1->decay_rate = 1.0f / (0.10f * (float)s_sample_rate);

    SynthVoice *v2 = alloc_voice();
    v2->wave_type = VOICE_WAVE_NOISE;
    v2->amp = 0.16f * vol;
    v2->decay_rate = 1.0f / (0.14f * (float)s_sample_rate);
    v2->filter_coeff = 0.6f;
}

static void drum_hihat(float vol) {
    if (s_muted) return;
    SynthVoice *v = alloc_voice();
    v->wave_type = VOICE_WAVE_NOISE;
    v->amp = 0.10f * vol;
    v->decay_rate = 1.0f / (0.04f * (float)s_sample_rate);
    v->filter_coeff = 0.85f;
}

/* ── Tracker Score Sequencer ──────────────────────────────────────── */

static void step_track_1(int step) {
    /* Track 1: 鐘のキャロル (Carol of the Bells) — 168 BPM 3/4 Vivace
     * 1 bar = 3 beats = 6 eighth notes. Each step = 1 eighth note (~0.1786s)
     * 4-note motif: G5, F#5, G5, E5
     */
    int pattern_step = step % 48;
    int beat_in_bar = (pattern_step % 6);

    /* Bells */
    if (beat_in_bar == 0) lilcu64_shim_ding(783.99f, 0.85f, 0.65f);      /* G5 */
    else if (beat_in_bar == 2) lilcu64_shim_ding(739.99f, 0.75f, 0.55f); /* F#5 */
    else if (beat_in_bar == 3) lilcu64_shim_ding(783.99f, 0.75f, 0.55f); /* G5 */
    else if (beat_in_bar == 4) lilcu64_shim_ding(659.25f, 0.95f, 0.80f); /* E5 */

    /* Walking bass on beat 1 and beat 3 */
    if (beat_in_bar == 0) {
        float bass_note = (pattern_step < 24) ? 82.41f : 110.00f; /* E2 or A2 */
        lilcu64_shim_waddle(bass_note, 0.9f);
        drum_kick(0.85f);
    } else if (beat_in_bar == 2 || beat_in_bar == 4) {
        drum_hihat(0.6f);
        if (pattern_step >= 12 && beat_in_bar == 4) drum_snare(0.7f);
    }

    /* Chiptune Lead melody in second section */
    if (pattern_step >= 24) {
        if (beat_in_bar == 0) lilcu64_shim_dance(1.189f, 0.7f);
        else if (beat_in_bar == 3) lilcu64_shim_dance(1.334f, 0.7f);
    }
}

static void step_track_2(int step) {
    /* Track 2: Green Caterpillar (今田勝 1975 Three Blind Mice Jazz) — 98 BPM 4/4
     * Step = 1 16th note (~0.153s)
     */
    int bar_step = step % 16;

    /* Upright acoustic bass waddle */
    if (bar_step == 0) {
        lilcu64_shim_waddle(73.42f, 0.95f); /* D2 */
        drum_kick(0.7f);
    } else if (bar_step == 4) {
        lilcu64_shim_waddle(87.31f, 0.85f); /* F2 */
    } else if (bar_step == 8) {
        lilcu64_shim_waddle(98.00f, 0.90f); /* G2 */
        drum_snare(0.65f);
    } else if (bar_step == 12) {
        lilcu64_shim_waddle(110.00f, 0.80f); /* A2 */
    }

    /* Masaru Imada Rhodes Chords & Tines */
    if (bar_step == 0 || bar_step == 6 || bar_step == 10) {
        lilcu64_shim_float(293.66f, 0.85f); /* Dm9 */
    }

    /* Ride cymbal on swinging 8ths */
    if (bar_step % 2 == 0) drum_hihat(0.5f);

    /* Frog croak rasp accent on bar 2 & 4 */
    if ((step % 32) == 14) {
        lilcu64_shim_frog_croak(1.0f, 0.85f);
    }
}

static void step_track_3(int step) {
    /* Track 3: 朝日のあたる家 (The House of the Rising Sun / Thomas Krüger) — 76 BPM 6/8
     * Step = 1 eighth note (~0.2368s)
     * Chords: Am - C - D - F - Am - E
     */
    int phrase_step = step % 36;
    int beat_in_bar = phrase_step % 6;
    int bar = phrase_step / 6;

    float root_f = 220.0f;
    if (bar == 0 || bar == 4) root_f = 220.0f;      /* Am */
    else if (bar == 1) root_f = 261.63f;            /* C */
    else if (bar == 2) root_f = 293.66f;            /* D */
    else if (bar == 3) root_f = 174.61f;            /* F */
    else if (bar == 5) root_f = 164.81f;            /* E */

    /* Left-hand rolling grand piano arpeggio */
    float arp_note = root_f;
    if (beat_in_bar == 0) {
        arp_note = root_f * 0.5f;
        /* Low felt hammer bass strike on beat 1 */
        trigger_voice_basic(VOICE_WAVE_SINE, arp_note * 0.5f, 0.35f, 1.2f, -0.4f);
    } else if (beat_in_bar == 1) arp_note = root_f;
    else if (beat_in_bar == 2) arp_note = root_f * 1.189f;
    else if (beat_in_bar == 3) arp_note = root_f * 1.498f;
    else if (beat_in_bar == 4) arp_note = root_f * 2.0f;
    else if (beat_in_bar == 5) arp_note = root_f * 1.498f;

    trigger_voice_basic(VOICE_WAVE_TRIANGLE, arp_note, 0.16f, 0.85f, -0.2f + beat_in_bar * 0.08f);

    /* Singing vocal lead entering */
    if (bar >= 2 && beat_in_bar == 0) {
        lilcu64_shim_pulse_squash(0.85f, root_f / 220.0f);
    }
}

static void step_track_4(int step) {
    /* Track 4: TRON Init (Nine Inch Nails / Trent Reznor) — 132 BPM 4/4
     * Step = 1 16th note (~0.1136s), Key: F# minor (92.50 Hz)
     */
    int bar_step = step % 16;

    /* Aggressive overdriven sawtooth bassline */
    float bass_freq = (bar_step < 12) ? 92.50f : 82.41f; /* F#2 to E2 */
    SynthVoice *v = alloc_voice();
    v->wave_type = VOICE_WAVE_SAW;
    v->freq = bass_freq;
    v->phase_inc = (float)(2.0 * M_PI * bass_freq / (double)s_sample_rate);
    v->amp = 0.26f;
    v->decay_rate = 1.0f / (0.12f * (float)s_sample_rate);
    v->filter_coeff = 0.55f;

    /* Heavy industrial drums */
    if (bar_step == 0 || bar_step == 6 || bar_step == 10) drum_kick(0.95f);
    if (bar_step == 4 || bar_step == 12) drum_snare(0.85f);
    if (bar_step % 2 == 0) drum_hihat(0.45f);

    /* Swarmatron Ribbon Glissando at bar transitions */
    if (bar_step == 0 && (step % 64) == 0) {
        SynthVoice *sw = alloc_voice();
        sw->wave_type = VOICE_WAVE_SAW;
        sw->freq = 185.0f;
        sw->target_freq = 370.0f;
        sw->freq_ramp_rate = 0.0003f;
        sw->phase_inc = (float)(2.0 * M_PI * sw->freq / (double)s_sample_rate);
        sw->amp = 0.18f;
        sw->decay_rate = 1.0f / (3.2f * (float)s_sample_rate);
    }
}

/* Advance sequencer by delta time */
static void update_sequencer(float dt) {
    if (s_active_track <= 0 || s_muted) return;

    s_track_time += dt;

    float step_interval = 0.1786f; /* Track 1 default: 168 BPM 8th note */
    if (s_active_track == 2) step_interval = 0.1530f;      /* 98 BPM 16th */
    else if (s_active_track == 3) step_interval = 0.2368f; /* 76 BPM 8th */
    else if (s_active_track == 4) step_interval = 0.1136f; /* 132 BPM 16th */

    while (s_track_time >= s_next_step_time) {
        if (s_active_track == 1) step_track_1(s_track_step);
        else if (s_active_track == 2) step_track_2(s_track_step);
        else if (s_active_track == 3) step_track_3(s_track_step);
        else if (s_active_track == 4) step_track_4(s_track_step);

        s_track_step++;
        s_next_step_time += step_interval;
    }
}

/* ── Master Render Frames ─────────────────────────────────────────── */

void lilcu64_synth_render_frames(int16_t *out_pcm, size_t frames) {
    if (!out_pcm || frames == 0) return;

    float dt = (float)frames / (float)s_sample_rate;
    update_sequencer(dt);

    for (size_t f = 0; f < frames; f++) {
        float sample_l = 0.0f;
        float sample_r = 0.0f;

        for (int i = 0; i < MAX_VOICES; i++) {
            SynthVoice *v = &s_voices[i];
            if (!v->active) continue;

            float raw = 0.0f;

            switch (v->wave_type) {
                case VOICE_WAVE_SINE:
                    raw = sinf(v->phase);
                    break;
                case VOICE_WAVE_TRIANGLE:
                    raw = (v->phase < (float)M_PI)
                        ? (-1.0f + 2.0f * (v->phase / (float)M_PI))
                        : (3.0f - 2.0f * (v->phase / (float)M_PI));
                    break;
                case VOICE_WAVE_SAW:
                    raw = 1.0f - 2.0f * (v->phase / (float)(2.0 * M_PI));
                    break;
                case VOICE_WAVE_PULSE:
                    raw = (v->phase < (float)M_PI) ? 0.8f : -0.8f;
                    break;
                case VOICE_WAVE_NOISE:
                    raw = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
                    break;
                case VOICE_WAVE_FM_BELL: {
                    float mod = sinf(v->fm_phase) * v->fm_index;
                    raw = sinf(v->phase + mod);
                    v->fm_phase += v->phase_inc * v->fm_ratio;
                    if (v->fm_phase >= (float)(2.0 * M_PI)) v->fm_phase -= (float)(2.0 * M_PI);
                    break;
                }
                default:
                    raw = 0.0f;
                    break;
            }

            /* Lowpass filter if active */
            if (v->filter_coeff > 0.01f) {
                v->filter_lp += v->filter_coeff * (raw - v->filter_lp);
                raw = v->filter_lp;
            }

            /* AM tremolo modulation */
            if (v->am_freq > 0.1f) {
                float am = 1.0f - v->am_depth * 0.5f * (1.0f + sinf(v->am_phase));
                raw *= am;
                v->am_phase += (float)(2.0 * M_PI * v->am_freq / (double)s_sample_rate);
                if (v->am_phase >= (float)(2.0 * M_PI)) v->am_phase -= (float)(2.0 * M_PI);
            }

            float mono = raw * v->amp;

            /* Constant-power stereo pan */
            float pan_norm = (v->pan + 1.0f) * 0.5f; /* 0..1 */
            if (pan_norm < 0.0f) pan_norm = 0.0f;
            if (pan_norm > 1.0f) pan_norm = 1.0f;
            float gain_l = cosf(pan_norm * (float)(M_PI * 0.5));
            float gain_r = sinf(pan_norm * (float)(M_PI * 0.5));

            sample_l += mono * gain_l;
            sample_r += mono * gain_r;

            /* Advance phase */
            v->phase += v->phase_inc;
            if (v->phase >= (float)(2.0 * M_PI)) v->phase -= (float)(2.0 * M_PI);

            /* Frequency glide */
            if (v->freq_ramp_rate != 0.0f) {
                v->freq += (v->target_freq - v->freq) * v->freq_ramp_rate;
                v->phase_inc = (float)(2.0 * M_PI * v->freq / (double)s_sample_rate);
            }

            /* Decay envelope */
            v->amp -= v->decay_rate;
            if (v->amp <= 0.0001f) {
                v->active = false;
            }
        }

        /* Master soft clipper / saturator */
        if (sample_l > 0.95f) sample_l = 0.95f;
        else if (sample_l < -0.95f) sample_l = -0.95f;
        if (sample_r > 0.95f) sample_r = 0.95f;
        else if (sample_r < -0.95f) sample_r = -0.95f;

        out_pcm[f * 2]     = (int16_t)(sample_l * 30000.0f);
        out_pcm[f * 2 + 1] = (int16_t)(sample_r * 30000.0f);
    }
}

/* ── VirtIO Pump ──────────────────────────────────────────────────── */

int lilcu64_synth_pump_virtio(size_t frames) {
    if (!virtio_sound_is_ready()) {
        /* Attempt to open VirtIO sound if not yet ready */
        if (!s_virtio_opened) {
            if (virtio_sound_open(s_sample_rate, LILCU64_AUDIO_CHANNELS) == 0) {
                s_virtio_opened = true;
            }
        }
        if (!virtio_sound_is_ready()) return -1;
    }

    size_t remaining = frames;
    while (remaining > 0) {
        size_t chunk = (remaining > PUMP_CHUNK_FRAMES) ? PUMP_CHUNK_FRAMES : remaining;
        lilcu64_synth_render_frames(s_pcm_chunk, chunk);
        virtio_sound_write(s_pcm_chunk, chunk);
        remaining -= chunk;
    }
    return 0;
}

/* ── Lifecycle & Controls ─────────────────────────────────────────── */

int lilcu64_synth_init(uint32_t sample_rate) {
    s_sample_rate = sample_rate ? sample_rate : LILCU64_AUDIO_RATE;
    memset(s_voices, 0, sizeof(s_voices));
    s_active_track = 0;
    s_track_time = 0.0f;
    s_next_step_time = 0.0f;
    s_track_step = 0;
    s_muted = false;

    if (virtio_sound_open(s_sample_rate, LILCU64_AUDIO_CHANNELS) == 0) {
        s_virtio_opened = true;
    }
    return 0;
}

void lilcu64_synth_close(void) {
    s_active_track = 0;
    memset(s_voices, 0, sizeof(s_voices));
    if (s_virtio_opened) {
        virtio_sound_close();
        s_virtio_opened = false;
    }
}

void lilcu64_synth_play_track(int track_num) {
    if (track_num < 1 || track_num > 4) {
        lilcu64_synth_stop_track();
        return;
    }
    if (s_active_track == track_num) {
        /* Toggle off if clicked again */
        lilcu64_synth_stop_track();
        return;
    }
    s_active_track = track_num;
    s_track_time = 0.0f;
    s_next_step_time = 0.0f;
    s_track_step = 0;
    s_muted = false;
}

void lilcu64_synth_stop_track(void) {
    s_active_track = 0;
    s_track_time = 0.0f;
    s_next_step_time = 0.0f;
    s_track_step = 0;
    for (int i = 0; i < MAX_VOICES; i++) {
        s_voices[i].active = false;
    }
}

void lilcu64_synth_toggle_mute(void) {
    s_muted = !s_muted;
    if (s_muted) {
        for (int i = 0; i < MAX_VOICES; i++) {
            s_voices[i].active = false;
        }
    }
}

bool lilcu64_synth_is_muted(void) {
    return s_muted;
}

int lilcu64_synth_get_active_track(void) {
    return s_active_track;
}

float lilcu64_synth_get_track_time(void) {
    return s_track_time;
}
