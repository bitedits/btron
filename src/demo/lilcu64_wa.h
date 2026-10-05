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
#define WA_SINE     0
#define WA_TRIANGLE 1
#define WA_SAWTOOTH 2
#define WA_SQUARE   3
#define WA_NOISE    4   /* AudioBufferSource playing the shared 2 s noise buffer */

/* BiquadFilterNode types */
#define WA_FILT_NONE     0
#define WA_FILT_LOWPASS  1
#define WA_FILT_HIGHPASS 2
#define WA_FILT_BANDPASS 3

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

/* Adds `frames` mono samples of all scheduled nodes into `mono` and advances the clock. */
void    wa_render(float *mono, size_t frames);

#ifdef __cplusplus
}
#endif

#endif /* LILCU64_WA_H */
