/*
 * src/demo/lilcu64_synth.h — Lil Cu 64 Procedural DSP Sound Engine & VirtIO Audio
 *
 * 0-byte footprint procedural sound synthesizer implementing the Audio-Visual
 * Shim Principle and 4-track sequencer conforming to SOUND.txt and index.html.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef LILCU64_SYNTH_H
#define LILCU64_SYNTH_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LILCU64_AUDIO_RATE 44100
#define LILCU64_AUDIO_CHANNELS 2

/* ── Lifecycle & Playback Controls ────────────────────────────────── */

int  lilcu64_synth_init(uint32_t sample_rate);
void lilcu64_synth_close(void);
void lilcu64_synth_play_track(int track_num);
void lilcu64_synth_stop_track(void);
void lilcu64_synth_toggle_mute(void);
bool lilcu64_synth_is_muted(void);
int  lilcu64_synth_get_active_track(void);
float lilcu64_synth_get_track_time(void);

/* ── Procedural Sound Shims (Triggered by Kinematic Events) ───────── */

void lilcu64_shim_pulse_squash(float vol_mul, float pitch_mul);
void lilcu64_shim_frog_croak(float pitch_mul, float vol_mul);
void lilcu64_shim_pulse_bell(float vol_mul, float pitch_mul);
void lilcu64_shim_waddle(float pitch, float vol_mul);
void lilcu64_shim_float(float base_freq, float vol_mul);
void lilcu64_shim_dance(float pitch_scale, float vol_mul);
void lilcu64_shim_ding(float freq, float vol_mul, float decay);
void lilcu64_shim_boing(void);
void lilcu64_shim_footstep(void);
void lilcu64_shim_chirp(void);

/* ── Audio Rendering & VirtIO Audio Pump ──────────────────────────── */

/* Renders stereo 16-bit interleaved PCM samples */
void lilcu64_synth_render_frames(int16_t *out_pcm, size_t frames);

/* Pumps rendered audio buffer into virtio_sound_write() if available */
int  lilcu64_synth_pump_virtio(size_t frames);

#ifdef __cplusplus
}
#endif

#endif /* LILCU64_SYNTH_H */
