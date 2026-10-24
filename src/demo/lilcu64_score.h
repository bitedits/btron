/*
 * src/demo/lilcu64_score.h — 1:1 transcriptions of the index.html track scores
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef LILCU64_SCORE_H
#define LILCU64_SCORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Loop length of a timeline track in seconds (0 if the track is not a timeline score). */
double lc_score_length(int track);

/* Schedules the whole track on the WebAudio clock starting at absolute time t0,
 * exactly like window.playTrack(n) does with ctx.currentTime + offsets. */
void   lc_score_schedule(int track, double t0);

/* Paper numbers. 1 is 1A. 21 violin, 22 cello, 23 timpani.
 * Snare and hat read MIDI velocity from hz; they ignore pitch.
 * The dispatcher sits after every static shim body. */
enum {
    LC_SHIM_SQUASH = 1,
    LC_SHIM_FROG = 2,
    LC_SHIM_ADD_BELL = 3,
    LC_SHIM_WADDLE = 4,
    LC_SHIM_FLOAT = 5,
    LC_SHIM_DANCE = 6,
    LC_SHIM_DING = 7,
    LC_SHIM_KICK = 8,
    LC_SHIM_SNARE = 9,
    LC_SHIM_HAT = 10,
    LC_SHIM_GUITAR = 11,
    LC_SHIM_PIANO = 12,
    LC_SHIM_PEDAL = 13,
    LC_SHIM_SAX = 14,
    LC_SHIM_IND_KICK = 15,
    LC_SHIM_IND_SNARE = 16,
    LC_SHIM_IND_BASS = 17,
    LC_SHIM_SWARM = 18,
    LC_SHIM_ZAP = 19,
    LC_SHIM_CYBER = 20,
    LC_SHIM_VIOLIN = 21,
    LC_SHIM_CELLO = 22,
    LC_SHIM_TIMPANI = 23
};

void     lc_set_score_base(double t0);
void     lc_shim_note(int shim_no, double t, float hz, float vol, float duration);
uint32_t lc_shim_refusals(void);

/* Capture is off until lc_cap_begin. The live score then runs unchanged.
 * Art 0 is a tone, 1 is a gesture or a drum articulation, 2 is a dance
 * phrase tone. Hammer and swoosh are capture-only ids 24 and 25. */
enum {
    LC_CAP_TONE = 0,
    LC_CAP_MARK = 1,
    LC_CAP_PHRASE = 2,
    LC_CAP_HAMMER = 24,
    LC_CAP_SWOOSH = 25
};

void lc_cap_begin(void);
void lc_cap_end(void);
int  lc_cap_count(void);
int  lc_cap_overflow(void);
int  lc_cap_at(int i, int *shim, int *art, double *t, float *hz, float *vol, float *dur);

#ifdef __cplusplus
}
#endif

#endif /* LILCU64_SCORE_H */
