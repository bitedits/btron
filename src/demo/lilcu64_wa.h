/*
 * src/demo/lilcu64_wa.h — WebAudio-equivalent scheduled node engine
 *
 * A tiny sample-accurate re-implementation of the subset of the WebAudio API
 * that index.html uses for the Lil Cu 64 scores:
 *   OscillatorNode (sine / triangle / sawtooth / square) and noise
 *   AudioBufferSource, AudioParam automation (setValueAtTime,
 *   linearRampToValueAtTime, exponentialRampToValueAtTime), BiquadFilterNode
 *   (lowpass / highpass / bandpass, WebAudio Q semantics) and a GainNode.
 *
 * Every node is scheduled ahead on an absolute audio clock (seconds), exactly
 * like `ctx.currentTime + timeOffset` in JS, so the scores play 1:1 and are
 * locked to the VirtIO sound sample clock, not to the UI/scheduler.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef LILCU64_WA_H
#define LILCU64_WA_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Oscillator types */
#define WA_SINE        0
#define WA_TRIANGLE    1
#define WA_SAWTOOTH    2
#define WA_SQUARE      3
#define WA_NOISE       4   /* AudioBufferSource playing the shared 2 s noise buffer */
#define WA_AMIGA_SAW   5   /* 8-bit Paula sawtooth with Zero-Order Hold */
#define WA_AMIGA_PULSE 6   /* 8-bit Paula pulse wave with Zero-Order Hold */
#define WA_AMIGA_NOISE 7   /* 8-bit Paula pseudo-random noise */

/* BiquadFilterNode types */
#define WA_FILT_NONE      0
#define WA_FILT_LOWPASS   1
#define WA_FILT_HIGHPASS  2
#define WA_FILT_BANDPASS  3
#define WA_FILT_PAULA_LED 4   /* Amiga 4.4 kHz 2-pole Butterworth LED lowpass filter */

/* AudioParam automation event types */
#define WA_SET 0   /* setValueAtTime */
#define WA_LIN 1   /* linearRampToValueAtTime */
#define WA_EXP 2   /* exponentialRampToValueAtTime */

typedef struct WaNode WaNode;

void    wa_init(uint32_t sample_rate);
void    wa_stop_all(void);              /* osc.stop() on everything, immediately */
double  wa_time(void);                  /* ctx.currentTime (seconds of rendered audio) */
int     wa_active_count(void);

/* osc.start(t_start); osc.stop(t_stop). Returns NULL if the node pool is full. */
WaNode *wa_osc(int type, double t_start, double t_stop);
void    wa_freq(WaNode *n, int ev, float value, double t);        /* osc.frequency.* */
void    wa_gain(WaNode *n, int ev, float value, double t);        /* gain.gain.* */
void    wa_filter(WaNode *n, int type, float freq, float q);      /* createBiquadFilter() */
void    wa_filter_freq(WaNode *n, int ev, float value, double t); /* filter.frequency.* */
void    wa_vibrato(WaNode *n, float rate_hz, float depth_cents);  /* ProTracker LFO vibrato */

/* Reverb & spatial feedback delay controls (deep industrial cavern model) */
void    wa_set_reverb(int enabled, float room_size, float damping, float feedback, float wet_mix);

/* Stereo rendering with optional deep reverb and spatial feedback delay */
void    wa_render_stereo(float *out_l, float *out_r, size_t frames);

/* Adds `frames` mono samples of bus 0 into `mono` and advances the clock. */
void    wa_render(float *mono, size_t frames);

/* Planes are contiguous: plane b starts at mono + b * frames. No reverb. */
void    wa_render_buses(const uint8_t *buses, int nb, float *mono, size_t frames);
int64_t wa_clock_samples(void);
void    wa_set_clock_samples(int64_t samples);
void    wa_set_node_bus(uint8_t bus);
void    wa_set_note_key(uint8_t key);
uint8_t wa_current_bus(void);
uint8_t wa_current_key(void);
void    wa_release_bus_key(uint8_t bus, uint8_t key);
void    wa_reverb_process(const float *dry, float *out_l, float *out_r, size_t n);
void    wa_reverb_save(void);
void    wa_reverb_restore(void);

#ifdef __cplusplus
}
#endif

#endif /* LILCU64_WA_H */
