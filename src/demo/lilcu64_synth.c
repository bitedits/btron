/*
 * src/demo/lilcu64_synth.c — Lil Cu 64 Procedural DSP Sound Engine & VirtIO Audio
 *
 * Fully autonomous C99 procedural synthesizer implementing 0-byte footprint
 * sound shims and 4-track tracker sequencer for B-System Demoscene.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "lilcu64_synth.h"
#include "lilcu64_wa.h"
#include "lilcu64_score.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif

#ifndef RAND_MAX
#define RAND_MAX 2147483647
#endif

/* External VirtIO sound drivers (weak stubs for targets without virtio.c) */
__attribute__((weak)) int  virtio_sound_open(uint32_t sample_rate, uint8_t channels) {
    (void)sample_rate; (void)channels; return -1;
}
__attribute__((weak)) int  virtio_sound_write(const int16_t *samples, size_t frames) {
    (void)samples; (void)frames; return -1;
}
__attribute__((weak)) void     virtio_sound_close(void) {}
__attribute__((weak)) bool     virtio_sound_is_ready(void) { return false; }
__attribute__((weak)) uint32_t virtio_sound_get_queued_bytes(void) { return 0; }

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

/* Timeline (1:1 index.html) track state. The UI thread only bumps
 * s_track_gen; the audio thread applies it in timeline_tick(). */
static volatile int    s_track_gen = 0;
static int             s_applied_gen = 0;
static volatile int    s_tl_track = 0;
static volatile double s_tl_base = 0.0;

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
__attribute__((unused)) static void drum_kick(float vol) {
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

__attribute__((unused)) static void drum_snare(float vol) {
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

__attribute__((unused)) static void drum_hihat(float vol) {
    if (s_muted) return;
    SynthVoice *v = alloc_voice();
    v->wave_type = VOICE_WAVE_NOISE;
    v->amp = 0.10f * vol;
    v->decay_rate = 1.0f / (0.04f * (float)s_sample_rate);
    v->filter_coeff = 0.85f;
}

/* ── Tracker Score Sequencer (Legacy, all tracks now WebAudio scores) ── */

static void update_sequencer(float dt) {
    (void)dt;
    /* Tracks 1–4 are all scheduled via lilcu64_score / lilcu64_wa */
}

/* ── Timeline (1:1 index.html) Tracks ─────────────────────────────── */

/* Runs on the audio thread at every 32-frame block: applies play/stop requests
 * from the UI thread and loops the score exactly like scheduleTrackTimeout(). */
static void timeline_tick(void) {
    int gen = s_track_gen;
    if (gen != s_applied_gen) {
        s_applied_gen = gen;
        wa_stop_all();                         /* stopAllTracks(true) */
        int trk = s_active_track;
        s_tl_track = (lc_score_length(trk) > 0.0) ? trk : 0;
        if (s_tl_track) {
            s_tl_base = wa_time();
            lc_score_schedule(s_tl_track, s_tl_base);
        }
        return;
    }
    if (s_tl_track) {
        double len = lc_score_length(s_tl_track);
        if (wa_time() >= s_tl_base + len) {    /* playTrack(n, true) */
            wa_stop_all();
            s_tl_base += len;
            lc_score_schedule(s_tl_track, s_tl_base);
        }
    }
}

/* ── Master Render Frames ─────────────────────────────────────────── */

#define RENDER_BLOCK 32

static void render_block(int16_t *out_pcm, size_t frames) {
    float wa_mix[RENDER_BLOCK];
    memset(wa_mix, 0, sizeof(wa_mix));

    timeline_tick();
    wa_render(wa_mix, frames);                 /* advances the audio clock */
    if (s_muted) memset(wa_mix, 0, sizeof(wa_mix));

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

        /* Legacy voices keep their previous level (x30000); the WebAudio
         * graph goes straight to ctx.destination, which clamps at +/-1. */
        sample_l = sample_l * (30000.0f / 32767.0f) + wa_mix[f];
        sample_r = sample_r * (30000.0f / 32767.0f) + wa_mix[f];
        if (sample_l > 1.0f) sample_l = 1.0f;
        else if (sample_l < -1.0f) sample_l = -1.0f;
        if (sample_r > 1.0f) sample_r = 1.0f;
        else if (sample_r < -1.0f) sample_r = -1.0f;

        out_pcm[f * 2]     = (int16_t)(sample_l * 32767.0f);
        out_pcm[f * 2 + 1] = (int16_t)(sample_r * 32767.0f);
    }
}

void lilcu64_synth_render_frames(int16_t *out_pcm, size_t frames) {
    if (!out_pcm || frames == 0) return;
    size_t off = 0;
    while (off < frames) {
        size_t n = frames - off;
        if (n > RENDER_BLOCK) n = RENDER_BLOCK;
        render_block(out_pcm + off * 2, n);
        off += n;
    }
}

/* ── VirtIO Pump ──────────────────────────────────────────────────── */

int lilcu64_synth_pump_virtio(size_t frames) {
    static volatile int s_pumping = 0;
    if (__sync_lock_test_and_set(&s_pumping, 1)) {
        return 0; /* Already pumping on another thread */
    }

    if (!virtio_sound_is_ready()) {
        /* Attempt to open VirtIO sound if not yet ready */
        if (!s_virtio_opened) {
            if (virtio_sound_open(s_sample_rate, LILCU64_AUDIO_CHANNELS) == 0) {
                s_virtio_opened = true;
            }
        }
        if (!virtio_sound_is_ready()) {
            __sync_lock_release(&s_pumping);
            return -1;
        }
    }

    /* Target audio buffer: ~100ms = 4410 frames = 17640 bytes at 44.1kHz stereo S16.
     * We refill whatever VirtIO sound has consumed so playback stays 100% locked to real time. */
    const uint32_t TARGET_BYTES = 17640;
    uint32_t queued = virtio_sound_get_queued_bytes();
    if (queued >= TARGET_BYTES) {
        __sync_lock_release(&s_pumping);
        return 0;
    }

    uint32_t missing_bytes = TARGET_BYTES - queued;
    size_t missing_frames = missing_bytes / (sizeof(int16_t) * LILCU64_AUDIO_CHANNELS);
    if (frames > missing_frames) {
        missing_frames = frames;
    }
    /* Cap single pump burst to at most 120ms (5292 frames) */
    if (missing_frames > 5292) missing_frames = 5292;

    size_t remaining = missing_frames;
    while (remaining > 0) {
        size_t chunk = (remaining > PUMP_CHUNK_FRAMES) ? PUMP_CHUNK_FRAMES : remaining;
        lilcu64_synth_render_frames(s_pcm_chunk, chunk);
        virtio_sound_write(s_pcm_chunk, chunk);
        remaining -= chunk;
    }

    __sync_lock_release(&s_pumping);
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
    s_track_gen = 0;
    s_applied_gen = 0;
    s_tl_track = 0;
    s_tl_base = 0.0;

    wa_init(s_sample_rate);

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
    /* Reset legacy sequencer state */
    s_track_time = 0.0f;
    s_next_step_time = 0.0f;
    s_track_step = 0;
    s_muted = false;
    /* Tell audio thread to reschedule the score on next render block */
    __sync_fetch_and_add(&s_track_gen, 1);
}

void lilcu64_synth_stop_track(void) {
    s_active_track = 0;
    s_track_time = 0.0f;
    s_next_step_time = 0.0f;
    s_track_step = 0;
    for (int i = 0; i < MAX_VOICES; i++) {
        s_voices[i].active = false;
    }
    /* Tell audio thread to clear WA nodes too */
    __sync_fetch_and_add(&s_track_gen, 1);
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
    /* For timeline tracks 1 & 2, return seconds elapsed within the current
     * loop iteration so the mascot choreography aligns with the music. */
    if (s_tl_track && lc_score_length(s_tl_track) > 0.0) {
        double t = wa_time() - s_tl_base;
        double len = lc_score_length(s_tl_track);
        while (t < 0.0) t += len;
        while (t >= len) t -= len;
        return (float)t;
    }
    return s_track_time;
}
