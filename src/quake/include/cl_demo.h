/*
 * src/quake/include/cl_demo.h — Quake .dem Demo Player & Walkthrough Replay
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#ifndef QUAKE_CL_DEMO_H
#define QUAKE_CL_DEMO_H

#include "quakedef.h"

typedef struct {
    int   is_playing;
    int   is_paused;
    float time;
    float total_duration;
    int   track;
    int   current_packet;
    int   total_packets;
    char  demoname[64];
    char  mapname[64];
    float last_angles[3];
    float last_origin[3];
} demo_state_t;

extern demo_state_t g_demo;

int  Demo_Play(const char *demoname);
void Demo_Stop(void);
void Demo_Update(float dt);
int  Demo_IsPlaying(void);

#endif /* QUAKE_CL_DEMO_H */
