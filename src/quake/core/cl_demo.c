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

/* ── Drone track records ──────────────────────────────────────────────── *
 * The tracks this port records (verify/tests/test_quake_drone.c) carry no
 * player movement at all — no forward/side keys, no entity deltas.  Each of
 * their frame packets states the camera outright:
 *
 *     svc_time(7) f32 | 200 f32 x,y,z + f32 pitch,yaw,roll | 201 i32 edict *
 *
 * The recorded origin IS the eye, so it gets applied with no view offset, and
 * nothing here moves the player edict: the route passes through trigger
 * volumes, and walking the edict through one would fire it mid-playback.
 * 200/201/255 are above NetQuake's svc_ range, so a stock demo is never
 * mistaken for a drone track. */
#define DR_SVC_TIME   7
#define DR_FRAME      200
#define DR_USE        201
#define DR_END        255

static float ld_f32(const byte *d) {
    union { uint32_t u; float f; } cv;
    cv.u = (uint32_t)d[0] | ((uint32_t)d[1] << 8) |
           ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
    return cv.f;
}

static int ld_i32(const byte *d) {
    union { uint32_t u; int i; } cv;
    cv.u = (uint32_t)d[0] | ((uint32_t)d[1] << 8) |
           ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
    return cv.i;
}

static int   s_drone_track = 0;

/* The two recorded poses bracketing g_demo.time, and the packets they came
 * from (-1 = not fetched yet). */
static int   s_pose_a_pkt = -1, s_pose_b_pkt = -1;
static float s_pose_a_org[3], s_pose_a_ang[3];
static float s_pose_b_org[3], s_pose_b_ang[3];

/* Packet payload, past its svc_time prefix. */
static const byte *demo_payload(int pkt, int *len) {
    if (pkt < 0 || pkt >= g_demo.total_packets) { *len = 0; return NULL; }
    int ofs = s_packet_ofs[pkt];
    uint32_t msglen = (uint32_t)s_demo_buffer[ofs] |
                      ((uint32_t)s_demo_buffer[ofs+1] << 8) |
                      ((uint32_t)s_demo_buffer[ofs+2] << 16) |
                      ((uint32_t)s_demo_buffer[ofs+3] << 24);
    if (ofs + 16 + (int)msglen > s_demo_len) { *len = 0; return NULL; }
    *len = (int)msglen;
    return s_demo_buffer + ofs + 16;
}

/* Records follow svc_time and its float. */
#define DEMO_BODY(pl, len) ((len) >= 5 && (pl)[0] == DR_SVC_TIME ? 5 : 0)

static int demo_pose_at(int pkt, float org[3], float ang[3]) {
    int len;
    const byte *pl = demo_payload(pkt, &len);
    int body = DEMO_BODY(pl, len);
    if (!body) return 0;
    if (body + 25 > len || pl[body] != DR_FRAME) return 0;
    for (int c = 0; c < 3; c++) org[c] = ld_f32(pl + body + 1 + c * 4);
    for (int c = 0; c < 3; c++) ang[c] = ld_f32(pl + body + 13 + c * 4);
    return 1;
}

/* Doors the track says to open in this packet. */
static void demo_uses_at(int pkt) {
    int len;
    const byte *pl = demo_payload(pkt, &len);
    int body = DEMO_BODY(pl, len);
    if (!body) return;
    if (pl[body] == DR_FRAME) body += 25;
    while (body + 5 <= len && pl[body] == DR_USE) {
        SV_UseEntity(ld_i32(pl + body + 1), 1);
        body += 5;
    }
}

/* Next packet carrying a camera pose, searching forward from `after`. */
static int demo_seek_pose(int after, float org[3], float ang[3], int *pkt_out) {
    for (int p = after + 1; p < g_demo.total_packets; p++) {
        if (demo_pose_at(p, org, ang)) { *pkt_out = p; return 1; }
    }
    return 0;
}

int Demo_IsPlaying(void) {
    return g_demo.is_playing;
}

void Demo_Stop(void) {
    g_demo.is_playing = 0;
    g_demo.is_paused  = 0;
    g_replay_active   = 0;
    s_pose_a_pkt = -1;
    s_pose_b_pkt = -1;
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

    /* Packet 0 is the serverdata banner; a drone track starts posing at 1. */
    s_drone_track = 0;
    s_pose_a_pkt = -1;
    s_pose_b_pkt = -1;
    if (pkt_count > 1) {
        float org[3], ang[3];
        if (demo_pose_at(1, org, ang)) s_drone_track = 1;
    }

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
    if (s_drone_track)
        Con_Printf("  drone track: camera follows the recorded poses, doors fire on their use events\n");
    else
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

        /* Read viewangles */
        union { uint32_t u; float f; } u0, u1, u2;
        u0.u = (uint32_t)s_demo_buffer[ofs+4]  | ((uint32_t)s_demo_buffer[ofs+5] << 8) |
               ((uint32_t)s_demo_buffer[ofs+6] << 16) | ((uint32_t)s_demo_buffer[ofs+7] << 24);
        u1.u = (uint32_t)s_demo_buffer[ofs+8]  | ((uint32_t)s_demo_buffer[ofs+9] << 8) |
               ((uint32_t)s_demo_buffer[ofs+10] << 16) | ((uint32_t)s_demo_buffer[ofs+11] << 24);
        u2.u = (uint32_t)s_demo_buffer[ofs+12] | ((uint32_t)s_demo_buffer[ofs+13] << 8) |
               ((uint32_t)s_demo_buffer[ofs+14] << 16) | ((uint32_t)s_demo_buffer[ofs+15] << 24);

        if (s_drone_track) {
            demo_uses_at(g_demo.current_packet);
        } else {
            /* Stock demo: marker angles only.  The player position lives inside
             * the NetQuake entity-delta stream, which this player does not
             * decode yet, so the camera holds at the recorded start. */
            r_refdef.viewangles[0] = u0.f;
            r_refdef.viewangles[1] = u1.f;
            r_refdef.viewangles[2] = u2.f;

            g_demo.last_angles[0] = u0.f;
            g_demo.last_angles[1] = u1.f;
            g_demo.last_angles[2] = u2.f;

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
        }

        g_demo.current_packet++;
    }

    /* Loop demo when finished */
    if (g_demo.current_packet >= g_demo.total_packets) {
        g_demo.current_packet = 0;
        g_demo.time = 0.0f;
        s_pose_a_pkt = -1;
        s_pose_b_pkt = -1;
    }

    if (!s_drone_track) return;

    /* Camera: the two bracketing recorded poses, lerped at this frame's time.
     * Poses are written at 30 Hz, so interpolation is what keeps the glide
     * smooth on a slower or faster display. */
    float org[3], ang[3];
    if (s_pose_a_pkt < 0 || s_packet_time[s_pose_a_pkt] > g_demo.time) {
        if (!demo_seek_pose(-1, s_pose_a_org, s_pose_a_ang, &s_pose_a_pkt)) return;
        if (!demo_seek_pose(s_pose_a_pkt, s_pose_b_org, s_pose_b_ang, &s_pose_b_pkt)) {
            s_pose_b_pkt = s_pose_a_pkt;
            memcpy(s_pose_b_org, s_pose_a_org, sizeof s_pose_b_org);
            memcpy(s_pose_b_ang, s_pose_a_ang, sizeof s_pose_b_ang);
        }
    }
    while (s_packet_time[s_pose_b_pkt] <= g_demo.time) {
        s_pose_a_pkt = s_pose_b_pkt;
        memcpy(s_pose_a_org, s_pose_b_org, sizeof s_pose_b_org);
        memcpy(s_pose_a_ang, s_pose_b_ang, sizeof s_pose_a_ang);
        if (!demo_seek_pose(s_pose_a_pkt, s_pose_b_org, s_pose_b_ang, &s_pose_b_pkt)) {
            /* Past the last pose: ride it out until the track loops. */
            s_pose_b_pkt = s_pose_a_pkt;
            memcpy(s_pose_b_org, s_pose_a_org, sizeof s_pose_b_org);
            memcpy(s_pose_b_ang, s_pose_a_ang, sizeof s_pose_a_ang);
            break;
        }
    }

    float span = s_packet_time[s_pose_b_pkt] - s_packet_time[s_pose_a_pkt];
    float k = (span > 1e-6f) ? (g_demo.time - s_packet_time[s_pose_a_pkt]) / span : 1.0f;
    if (k < 0.0f) k = 0.0f;
    if (k > 1.0f) k = 1.0f;
    for (int c = 0; c < 3; c++) {
        org[c] = s_pose_a_org[c] + k * (s_pose_b_org[c] - s_pose_a_org[c]);
        ang[c] = s_pose_a_ang[c] + k * (s_pose_b_ang[c] - s_pose_a_ang[c]);
    }

    r_refdef.vieworg[0] = org[0];
    r_refdef.vieworg[1] = org[1];
    r_refdef.vieworg[2] = org[2];   /* the record's origin is already the eye */
    r_refdef.viewangles[0] = ang[0];
    r_refdef.viewangles[1] = ang[1];
    r_refdef.viewangles[2] = ang[2];

    g_demo.last_origin[0] = org[0];
    g_demo.last_origin[1] = org[1];
    g_demo.last_origin[2] = org[2];
    g_demo.last_angles[0] = ang[0];
    g_demo.last_angles[1] = ang[1];
    g_demo.last_angles[2] = ang[2];
}
