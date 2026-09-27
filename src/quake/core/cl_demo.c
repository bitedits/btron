/*
 * src/quake/core/cl_demo.c — Quake .dem Demo Replay & E1M1 Walkthrough System
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../include/cl_demo.h"
#include "../include/quakedef.h"
#include "../include/server.h"
#include "../include/world.h"
#include "../include/render.h"
#include "../include/fs_btron.h"
#include "../include/progs.h"
#include "../include/quake_ui.h"

#define MAX_DEMO_PACKETS 8192

demo_state_t g_demo;

static byte  *s_demo_buffer = NULL;
static int    s_demo_len    = 0;
static int    s_packet_ofs[MAX_DEMO_PACKETS];
static float  s_packet_time[MAX_DEMO_PACKETS];

int Demo_IsPlaying(void) {
    return g_demo.is_playing;
}

void Demo_Stop(void) {
    g_demo.is_playing = 0;
    g_demo.is_paused  = 0;
    g_replay_active   = 0;
}

int Demo_Play(const char *demoname) {
    if (!demoname || !demoname[0]) demoname = "e1m1.dem";

    char path[128];
    snprintf(path, sizeof(path), "%s", demoname);

    int file_len = 0;
    byte *data = (byte *)FS_LoadFile(path, &file_len);
    if (!data && !strstr(path, ".dem")) {
        snprintf(path, sizeof(path), "%s.dem", demoname);
        data = (byte *)FS_LoadFile(path, &file_len);
    }
    if (!data && strncmp(path, "assets/quake/", 13) != 0) {
        snprintf(path, sizeof(path), "assets/quake/%s", demoname);
        data = (byte *)FS_LoadFile(path, &file_len);
        if (!data && !strstr(path, ".dem")) {
            snprintf(path, sizeof(path), "assets/quake/%s.dem", demoname);
            data = (byte *)FS_LoadFile(path, &file_len);
        }
    }

    if (!data || file_len <= 16) {
        Con_Printf("Demo_Play: could not load demo '%s'\n", demoname);
        return 0;
    }

    s_demo_buffer = data;
    s_demo_len    = file_len;

    memset(&g_demo, 0, sizeof(g_demo));
    snprintf(g_demo.demoname, sizeof(g_demo.demoname), "%s", demoname);

    /* 1. Parse CD track from first line */
    int ofs = 0;
    while (ofs < file_len && data[ofs] != '\n') ofs++;
    if (ofs < file_len && data[ofs] == '\n') ofs++;

    g_demo.track = q_atoi((const char *)data);

    /* 2. Index demo packets */
    int pkt_count = 0;
    float current_time = 0.0f;
    float base_svc_time = -1.0f;
    g_demo.mapname[0] = 0;

    while (ofs + 16 <= file_len && pkt_count < MAX_DEMO_PACKETS) {
        uint32_t msglen = (uint32_t)data[ofs] |
                          ((uint32_t)data[ofs+1] << 8) |
                          ((uint32_t)data[ofs+2] << 16) |
                          ((uint32_t)data[ofs+3] << 24);

        if (ofs + 16 + (int)msglen > file_len) break;

        s_packet_ofs[pkt_count] = ofs;

        /* Inspect packet payload for svc_time (7) or svc_serverdata (11) */
        const byte *payload = data + ofs + 16;
        if (msglen >= 5 && payload[0] == 7) {
            /* svc_time: read float timestamp */
            union { uint32_t u; float f; } u;
            u.u = (uint32_t)payload[1] | ((uint32_t)payload[2] << 8) |
                  ((uint32_t)payload[3] << 16) | ((uint32_t)payload[4] << 24);
            if (base_svc_time < 0.0f) {
                base_svc_time = u.f;
            }
            current_time = u.f - base_svc_time;
            if (current_time < 0.0f) current_time = 0.0f;
        } else {
            /* Increment synthesized time if missing svc_time */
            current_time += 1.0f / 30.0f;
        }

        s_packet_time[pkt_count] = current_time;

        /* Inspect packet 0 for mapname */
        if (pkt_count == 0 && msglen >= 8 && payload[0] == 11) {
            /* svc_serverdata: search for "maps/" */
            for (uint32_t k = 0; k + 4 < msglen; k++) {
                if (payload[k] == 'm' && payload[k+1] == 'a' && payload[k+2] == 'p' && payload[k+3] == 's') {
                    int end = (int)k;
                    while (end < (int)msglen && payload[end] && payload[end] != ' ') end++;
                    int slen = end - (int)k;
                    if (slen > 0 && slen < (int)sizeof(g_demo.mapname)) {
                        memcpy(g_demo.mapname, payload + k, slen);
                        g_demo.mapname[slen] = 0;
                    }
                    break;
                }
            }
        }

        ofs += 16 + msglen;
        pkt_count++;
    }

    g_demo.total_packets  = pkt_count;
    g_demo.total_duration = current_time;
    g_demo.current_packet = 0;
    g_demo.time           = 0.0f;
    g_demo.is_playing     = 1;
    g_demo.is_paused      = 0;
    g_replay_active       = 1;

    /* If demo specifies a map and it differs from world, load it */
    if (g_demo.mapname[0] && (!g_world.is_loaded || strstr(g_world.name, g_demo.mapname) == NULL)) {
        World_ChangeMap(g_demo.mapname);
    }

    /* Initialize initial demo origin from spawn */
    g_demo.last_origin[0] = g_world.spawn_origin[0];
    g_demo.last_origin[1] = g_world.spawn_origin[1];
    g_demo.last_origin[2] = g_world.spawn_origin[2];

    Con_Printf("Demo_Play: started playback of '%s' (%d packets, %.1fs)\n",
               demoname, pkt_count, current_time);
    Con_Printf("  angles-only: entity-delta positions are not decoded yet, camera stays at level start\n");
    return 1;
}

void Demo_Update(float dt) {
    if (!g_demo.is_playing || g_demo.is_paused || !s_demo_buffer) return;

    g_demo.time += dt;

    /* Process all packets up to current timestamp */
    while (g_demo.current_packet < g_demo.total_packets) {
        if (s_packet_time[g_demo.current_packet] > g_demo.time) break;

        int ofs = s_packet_ofs[g_demo.current_packet];
        uint32_t msglen = (uint32_t)s_demo_buffer[ofs] |
                          ((uint32_t)s_demo_buffer[ofs+1] << 8) |
                          ((uint32_t)s_demo_buffer[ofs+2] << 16) |
                          ((uint32_t)s_demo_buffer[ofs+3] << 24);

        /* Read viewangles */
        union { uint32_t u; float f; } u0, u1, u2;
        u0.u = (uint32_t)s_demo_buffer[ofs+4]  | ((uint32_t)s_demo_buffer[ofs+5] << 8) |
               ((uint32_t)s_demo_buffer[ofs+6] << 16) | ((uint32_t)s_demo_buffer[ofs+7] << 24);
        u1.u = (uint32_t)s_demo_buffer[ofs+8]  | ((uint32_t)s_demo_buffer[ofs+9] << 8) |
               ((uint32_t)s_demo_buffer[ofs+10] << 16) | ((uint32_t)s_demo_buffer[ofs+11] << 24);
        u2.u = (uint32_t)s_demo_buffer[ofs+12] | ((uint32_t)s_demo_buffer[ofs+13] << 8) |
               ((uint32_t)s_demo_buffer[ofs+14] << 16) | ((uint32_t)s_demo_buffer[ofs+15] << 24);

        r_refdef.viewangles[0] = u0.f;
        r_refdef.viewangles[1] = u1.f;
        r_refdef.viewangles[2] = u2.f;

        g_demo.last_angles[0] = u0.f;
        g_demo.last_angles[1] = u1.f;
        g_demo.last_angles[2] = u2.f;

        /* Player position lives inside the NetQuake entity-delta stream, which
         * this player does not decode yet; hold the camera at the recorded
         * start rather than guessing offsets in the packet body. */
        (void)msglen;

        r_refdef.vieworg[0] = g_demo.last_origin[0];
        r_refdef.vieworg[1] = g_demo.last_origin[1];
        r_refdef.vieworg[2] = g_demo.last_origin[2] + 22.0f; /* Eye height offset */

        /* Sync player edict in server world */
        if (g_prvm.is_loaded && g_prvm.num_edicts > 1) {
            edict_t *player = &g_prvm.edicts[1];
            EF(player, F_ANGLES_X) = u0.f;
            EF(player, F_ANGLES_Y) = u1.f;
            EF(player, F_ANGLES_Z) = u2.f;
        }

        g_demo.current_packet++;
    }

    /* Loop demo when finished */
    if (g_demo.current_packet >= g_demo.total_packets) {
        g_demo.current_packet = 0;
        g_demo.time = 0.0f;
    }
}
