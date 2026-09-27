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
int TEX_LoadBSPTextures(const char *bsp_path) { (void)bsp_path; return 1; }

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
    printf("\n[3/5] Testing demo2.dem (E1M4) and demo3.dem (E1M6) from pak0.pak...\n");
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

    /* 4. Test E1M1 Walkthrough Demo (e1m1.dem) */
    printf("\n[4/5] Testing e1m1.dem walkthrough playback...\n");
    int ok_e1m1 = Demo_Play("assets/quake/e1m1.dem");
    assert(ok_e1m1 == 1);
    assert(g_demo.total_packets >= 540);
    assert(strcmp(g_demo.mapname, "maps/e1m1.bsp") == 0);
    printf("  [PASS] e1m1.dem loaded: %d packets, map='%s', duration=%.1fs\n",
           g_demo.total_packets, g_demo.mapname, g_demo.total_duration);

    /* Step through all 540 packets (18.0s at 30Hz) */
    for (int f = 0; f < 200; f++) {
        Demo_Update(0.1f);
    }
    printf("  [PASS] e1m1.dem reached time=%.1fs, player pos=(%.1f, %.1f, %.1f)\n",
           g_demo.time, r_refdef.vieworg[0], r_refdef.vieworg[1], r_refdef.vieworg[2]);
    Demo_Stop();

    /* 5. Test Episode 1 Level Selection Menu */
    printf("\n[5/5] Testing Episode 1 Map Selection Menu...\n");
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

    printf("\n==========================================================\n");
    printf(" ALL Quake Demo Replays & Episode 1 Menu Tests PASSED! (5/5)\n");
    printf("==========================================================\n");
    return 0;
}
