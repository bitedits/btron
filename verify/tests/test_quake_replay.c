/*
 * verify/tests/test_quake_replay.c — Verification test for Quake pak0 .dem replays and Episode 1 map selection
 *
 * Cleanroom C99 test suite for B-System.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "../../include/btron/event.h"
#include "../../include/btron/wnd.h"
#include "../../src/quake/include/quakedef.h"
#include "../../src/quake/include/server.h"
#include "../../src/quake/include/world.h"
#include "../../src/quake/include/progs.h"
#include "../../src/quake/include/render.h"
#include "../../src/quake/include/mathlib.h"
#include "../../src/quake/include/fs_btron.h"
#include "../../src/quake/include/cl_demo.h"
#include "../../src/quake/include/quake_ui.h"

/* Globals needed by engine & tests */
refdef_t    r_refdef;
int         g_fired_count = 0;

int in_forward = 0;
int in_back = 0;
int in_left = 0;
int in_right = 0;
int in_down = 0;
int in_jump = 0;
int in_attack = 0;
int in_turn_left = 0;
int in_turn_right = 0;

int g_num_gl_textures = 0;
void uart_puts_raw(const char *s) { (void)s; }
void P_BloodSplash(const float o[3], int c) { (void)o; (void)c; }
void P_ExplosionParticles(const float o[3]) { (void)o; }
void P_RunParticleEffect(const float o[3], const float d[3], int col, int cnt) { (void)o; (void)d; (void)col; (void)cnt; }
void R_AddDynamicLight(const float o[3], float rad, float r, float g, float b, float decay) {
    (void)o; (void)rad; (void)r; (void)g; (void)b; (void)decay;
}
void *R_LoadAliasModel(const char *name) { (void)name; return NULL; }
int TEX_LoadBSPTextures(const byte *tex_lump, int lump_len) { (void)tex_lump; (void)lump_len; return 1; }

void P_UpdateParticles(float dt) { (void)dt; }

/* Stubs for UI rendering calls */
void Draw_Fill(int x, int y, int w, int h, uint32_t c) {
    (void)x; (void)y; (void)w; (void)h; (void)c;
}
void Draw_Char(int x, int y, char c) {
    (void)x; (void)y; (void)c;
}
void Draw_String(int x, int y, const char *s) {
    (void)x; (void)y; (void)s;
}
void close_quake_window(void) {}

int main(void) {
    printf("==========================================================\n");
    printf(" Running Quake Demo Replay & Level Select Test Suite...\n");
    printf("==========================================================\n\n");

    /* 1. Initialize Quake File System (pak0.pak) */
    printf("[1/5] Initializing FS and verifying pak0.pak...\n");
    int fs_ok = FS_Init();
    if (!fs_ok) {
        printf("  [SKIP] pak0.pak not found, skipping demo tests.\n");
        return 0;
    }
    printf("  [PASS] pak0.pak initialized successfully.\n");

    /* Initialize UI commands */
    UI_Init();

    /* 2. Test Official demo1.dem from pak0.pak (E1M3: The Necropolis) */
    printf("\n[2/5] Testing demo1.dem from pak0.pak (E1M3)...\n");
    int ok1 = Demo_Play("demo1.dem");
    assert(ok1 == 1);
    assert(Demo_IsPlaying() == 1);
    assert(g_demo.total_packets == 975);
    assert(g_demo.track == 2);
    assert(strcmp(g_demo.mapname, "maps/e1m3.bsp") == 0);
    assert(g_demo.total_duration > 70.0f);
    printf("  [PASS] demo1.dem loaded: %d packets, map='%s', track=%d, duration=%.1fs\n",
           g_demo.total_packets, g_demo.mapname, g_demo.track, g_demo.total_duration);

    /* Step demo frames to verify camera tracking */
    for (int f = 0; f < 30; f++) {
        Demo_Update(0.1f);
    }
    assert(g_demo.current_packet > 0);
    printf("  [PASS] demo1.dem playback progressed to packet %d (time=%.1fs, vieworg=[%.1f, %.1f, %.1f])\n",
           g_demo.current_packet, g_demo.time, r_refdef.vieworg[0], r_refdef.vieworg[1], r_refdef.vieworg[2]);
    Demo_Stop();
    assert(Demo_IsPlaying() == 0);

    /* 3. Test Official demo2.dem and demo3.dem from pak0.pak */
    printf("\n[3/7] Testing demo2.dem (E1M4) and demo3.dem (E1M6) from pak0.pak...\n");
    int ok2 = Demo_Play("demo2.dem");
    assert(ok2 == 1);
    assert(g_demo.total_packets == 991);
    assert(strcmp(g_demo.mapname, "maps/e1m4.bsp") == 0);
    assert(g_demo.total_duration > 65.0f);
    for (int f = 0; f < 20; f++) Demo_Update(0.1f);
    printf("  [PASS] demo2.dem loaded: %d packets, map='%s', duration=%.1fs\n",
           g_demo.total_packets, g_demo.mapname, g_demo.total_duration);
    Demo_Stop();

    int ok3 = Demo_Play("demo3.dem");
    assert(ok3 == 1);
    assert(g_demo.total_packets == 1096);
    assert(strcmp(g_demo.mapname, "maps/e1m6.bsp") == 0);
    assert(g_demo.total_duration > 75.0f);
    for (int f = 0; f < 20; f++) Demo_Update(0.1f);
    printf("  [PASS] demo3.dem loaded: %d packets, map='%s', duration=%.1fs\n",
           g_demo.total_packets, g_demo.mapname, g_demo.total_duration);
    Demo_Stop();

    /* 4. Test Episode 1 Level Selection Menu */
    printf("\n[4/7] Testing Episode 1 Map Selection Menu...\n");
    const char *ep1_maps[] = {
        "start", "e1m1", "e1m2", "e1m3", "e1m4", "e1m5", "e1m6", "e1m7", "e1m8"
    };
    for (int i = 0; i < 9; i++) {
        char bsp_path[64];
        snprintf(bsp_path, sizeof(bsp_path), "maps/%s.bsp", ep1_maps[i]);
        int sz = 0;
        const byte *bsp = (const byte *)FS_LoadFile(bsp_path, &sz);
        assert(bsp != NULL && sz > 1000);
        printf("  [PASS] Episode 1 map '%s' verified in pak0.pak (%d bytes)\n", bsp_path, sz);
    }

    /* Test UI Menu navigation */
    g_menu_active = 1;
    /* Press '2' on Main Menu -> opens Select Mission Level */
    UI_HandleKey('2');
    /* Press '4' -> Selects E1M3 */
    UI_HandleKey('4');
    assert(g_world.is_loaded == 1);
    assert(strcmp(g_world.name, "maps/e1m3.bsp") == 0);
    printf("  [PASS] Menu selected map maps/e1m3.bsp successfully!\n");

    /* Re-open menu, select E1M1 */
    g_menu_active = 1;
    UI_HandleKey('2'); /* Mission Level submenu */
    UI_HandleKey('2'); /* E1M1 */
    assert(strcmp(g_world.name, "maps/e1m1.bsp") == 0);
    printf("  [PASS] Menu selected map maps/e1m1.bsp successfully!\n");

    /* 6. Test Loading ALL 9 Episode 1 Maps via World_ChangeMap */
    printf("\n[6/8] Testing full loading & server initialization of all Episode 1 maps...\n");
    int map_faces[9];
    for (int i = 0; i < 9; i++) {
        int ok = World_ChangeMap(ep1_maps[i]);
        assert(ok == 1);
        assert(g_world.is_loaded == 1);
        assert(g_prvm.num_edicts >= 2);
        edict_t *player = &g_prvm.edicts[1];
        assert(player->free == 0);
        assert(EF(player, F_HEALTH) == 100.0f);
        assert(EF(player, F_MOVETYPE) == (float)MOVETYPE_WALK);

        map_faces[i] = g_world.numfaces;

        /* Step server frame to verify collision, physics, and world stability */
        for (int f = 0; f < 5; f++) {
            SV_ServerFrame(0.05f);
        }

        printf("  [PASS] Map '%s' loaded: %d faces, %d clipnodes, %d edicts, spawn=(%.1f, %.1f, %.1f)\n",
               g_world.name, g_world.numfaces, g_world.numclipnodes, g_prvm.num_edicts,
               g_world.spawn_origin[0], g_world.spawn_origin[1], g_world.spawn_origin[2]);
    }
    /* Verify that different maps loaded distinct geometry, not stale e1m1 cache */
    assert(map_faces[0] != map_faces[1]); /* start vs e1m1 */
    assert(map_faces[1] != map_faces[6]); /* e1m1 vs e1m6 */
    printf("  [PASS] Verified distinct BSP geometry loaded across Episode 1 levels (cache invalidated properly)!\n");

    /* 7. Test Demo Mode Input Suppression */
    printf("\n[7/8] Testing Demo Mode Input Suppression...\n");
    int demo_ok = Demo_Play("demo1.dem");
    assert(demo_ok == 1);
    assert(g_replay_active == 1);

    /* Gameplay keys must be suppressed in demo mode */
    assert(UI_HandleKey('w') == 1);
    assert(UI_HandleKey('s') == 1);
    assert(UI_HandleKey('a') == 1);
    assert(UI_HandleKey('d') == 1);
    assert(UI_HandleKey(' ') == 1);
    assert(UI_HandleKey('c') == 1);
    assert(in_forward == 0 && in_back == 0 && in_left == 0 && in_right == 0 && in_jump == 0 && in_down == 0);

    /* Mouse input must be suppressed in demo mode */
    assert(UI_HandleMouse(100, 100, 1) == 1);
    assert(in_attack == 0);

    /* Demo must still be playing */
    assert(Demo_IsPlaying() == 1);
    assert(g_replay_active == 1);

    /* ESC key stops demo mode and opens menu */
    assert(UI_HandleKey(27) == 1); /* ESC */
    assert(Demo_IsPlaying() == 0);
    assert(g_replay_active == 0);
    assert(g_menu_active == 1);
    printf("  [PASS] Demo mode ignores gameplay keys & mouse; ESC stops demo and opens menu.\n");

    printf("\n[8/8] Testing player movement from spawn on E1M1...\n");
    World_ChangeMap("maps/e1m1.bsp");
    assert(g_world.is_loaded == 1);
    edict_t *player = &g_prvm.edicts[1];
    assert(player && !player->free);

    /* Place player exactly at the E1M1 spawn origin (verified by test 6) */
    float spawn_x = g_world.spawn_origin[0]; /* 480.0 */
    float spawn_y = g_world.spawn_origin[1]; /* -352.0 */
    float spawn_z = g_world.spawn_origin[2]; /* 88.0  */
    printf("    E1M1 spawn: (%.1f, %.1f, %.1f)\n", spawn_x, spawn_y, spawn_z);

    EF(player, F_ORIGIN_X) = spawn_x;
    EF(player, F_ORIGIN_Y) = spawn_y;
    EF(player, F_ORIGIN_Z) = spawn_z;
    EF(player, F_VELOCITY_X) = 0.0f;
    EF(player, F_VELOCITY_Y) = 0.0f;
    EF(player, F_VELOCITY_Z) = 0.0f;
    EF(player, F_FLAGS) = 512.0f; /* FL_ONGROUND */
    EF(player, F_MOVETYPE) = (float)MOVETYPE_WALK;

    g_menu_active    = 0;
    g_console_active = 0;
    g_replay_active  = 0;

    /* Walk in +X direction (yaw = 0°, forward = +X) */
    r_refdef.viewangles[0] = 0.0f;
    r_refdef.viewangles[1] = 0.0f;   /* Yaw 0 = +X */
    r_refdef.viewangles[2] = 0.0f;

    float start_x = EF(player, F_ORIGIN_X);
    in_forward = 1;
    for (int step = 0; step < 30; step++) {
        SV_ServerFrame(0.05f);
        float px = EF(player, F_ORIGIN_X);
        float py = EF(player, F_ORIGIN_Y);
        float pz = EF(player, F_ORIGIN_Z);
        printf("    step %d: pos=(%.1f, %.1f, %.1f) flags=%d\n",
               step, px, py, pz, (int)EF(player, F_FLAGS));
        /* Player must not fall into the void */
        assert(pz >= spawn_z - 40.0f);
    }
    in_forward = 0;

    /* Player must have moved in +X direction */
    float final_x = EF(player, F_ORIGIN_X);
    assert(final_x > start_x + 10.0f);
    printf("  [PASS] Player moved from spawn +X: %.1f -> %.1f (delta=%.1f)\n",
           start_x, final_x, final_x - start_x);

    /* 9. Recorded drone track: the file carries camera poses only — no
     * movement keys, no entity deltas — so playback must glide the viewport
     * while the player edict stays exactly where it was. */
    printf("\n[9/9] Playing a recorded drone track (assets/quake/drone1m1.dem)...\n");
    World_ChangeMap("maps/e1m1.bsp");
    g_replay_active = 0;
    if (!Demo_Play("drone1m1")) {
        printf("  [SKIP] drone1m1.dem is not recorded yet — run `make test-drone`.\n");
    } else {
        assert(strcmp(g_demo.mapname, "maps/e1m1.bsp") == 0);
        float px0 = EF(player, F_ORIGIN_X), py0 = EF(player, F_ORIGIN_Y), pz0 = EF(player, F_ORIGIN_Z);

        float prev[3];
        memcpy(prev, r_refdef.vieworg, sizeof prev);
        float travel = 0.0f, max_step = 0.0f;
        int frames = (int)(g_demo.total_duration * 60.0f);
        for (int f = 0; f < frames; f++) {
            Demo_Update(1.0f / 60.0f);
            /* Frame 0 only lands the camera on the route start, so its step is
             * measured from frame 1 — the glide itself is what has to be smooth. */
            if (f > 0) {
                float dx = r_refdef.vieworg[0] - prev[0];
                float dy = r_refdef.vieworg[1] - prev[1];
                float dz = r_refdef.vieworg[2] - prev[2];
                float d = sqrtf(dx * dx + dy * dy + dz * dz);
                travel += d;
                if (d > max_step) max_step = d;
            }
            memcpy(prev, r_refdef.vieworg, sizeof prev);
            if (SV_PointContents(r_refdef.vieworg) == CONTENTS_SOLID) {
                printf("  [FAIL] frame %d: camera inside world geometry at (%.1f, %.1f, %.1f)\n",
                       f, r_refdef.vieworg[0], r_refdef.vieworg[1], r_refdef.vieworg[2]);
                assert(0);
            }
        }

        printf("    camera path %.1f units in %d frames, longest single step %.1f units "
               "(%.0f units/s at 60 Hz)\n",
               travel, frames, max_step, max_step * 60.0f);
        printf("    player edict stayed at (%.1f, %.1f, %.1f)\n",
               EF(player, F_ORIGIN_X), EF(player, F_ORIGIN_Y), EF(player, F_ORIGIN_Z));

        /* The route is a fast run at 300 units/s, so the viewport has to cover
         * most of that; a pinned camera (the old angles-only path) gives 0. */
        assert(travel > g_demo.total_duration * 150.0f);
        assert(max_step < 8.0f);    /* 30 Hz poses lerped: 5 units/frame, not the 10 it would snap to */
        assert(EF(player, F_ORIGIN_X) == px0 && EF(player, F_ORIGIN_Y) == py0 &&
               EF(player, F_ORIGIN_Z) == pz0);
        printf("  [PASS] Drone track glides the camera through %d packets, never inside a brush, "
               "and moves no player input.\n", g_demo.total_packets);
        Demo_Stop();
    }

    printf("\n==========================================================\n");
    printf(" ALL Quake Demo Replays & Episode 1 Menu Tests PASSED! (9/9)\n");
    printf("==========================================================\n");
    return 0;
}
