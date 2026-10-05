/*
 * src/demo/lilcu64_score.h — 1:1 transcriptions of the index.html track scores
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef LILCU64_SCORE_H
#define LILCU64_SCORE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Loop length of a timeline track in seconds (0 if the track is not a timeline score). */
double lc_score_length(int track);

/* Schedules the whole track on the WebAudio clock starting at absolute time t0,
 * exactly like window.playTrack(n) does with ctx.currentTime + offsets. */
void   lc_score_schedule(int track, double t0);

#ifdef __cplusplus
}
#endif

#endif /* LILCU64_SCORE_H */
