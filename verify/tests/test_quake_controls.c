/*
 * verify/tests/test_quake_controls.c — Verification test for Quake WASD, Jump & Crouch Controls
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

/* Exported controls from in_btron.c */
extern int in_forward;
extern int in_back;
extern int in_left;
extern int in_right;
extern int in_down;
extern int in_jump;
extern int in_attack;
extern int in_turn_left;
extern int in_turn_right;

void IN_Btron_HandleEvent(WND *wnd, const EVT *evt);
void SV_ServerFrame(float dt);

int g_console_active = 0;
int g_menu_active = 0;
int g_replay_active = 0;
prvm_t g_prvm;
bsp_world_t g_world;
refdef_t r_refdef;

void Con_Printf(const char *fmt, ...) { (void)fmt; }
void Con_DPrintf(const char *fmt, ...) { (void)fmt; }
void Sys_Error(const char *fmt, ...) { (void)fmt; exit(1); }
void PR_ExecuteProgram(int f) { (void)f; }
int PR_SetString(const char *s) { (void)s; return 0; }
const char *PR_GetString(int o) { (void)o; return ""; }
int PR_LoadProgs(const char *n) { (void)n; return 0; }
int PR_GlobalOfs(const char *n) { (void)n; return -1; }
int NUM_FOR_EDICT(const edict_t *ed) { return (int)(ed - g_prvm.edicts); }
void P_UpdateParticles(float dt) { (void)dt; }
void Player_FireWeapon(void) {}
void Player_UpdateAnimation(float dt) { (void)dt; }
void UI_RequestWeapon(int slot) { (void)slot; }
void Replay_Update(float dt) { (void)dt; }

int main(void) {
    printf("=== Test Quake Controls: WASD, Space, C ===\n");

    /* 1. Test IN_Btron_HandleEvent key mapping */
    EVT ev;
    memset(&ev, 0, sizeof(ev));

    /* W / S forward & back */
    ev.type = EV_KEY_DOWN; ev.key = 'w';
    IN_Btron_HandleEvent(NULL, &ev);
    assert(in_forward == 1);

    ev.type = EV_KEY_UP; ev.key = 'w';
    IN_Btron_HandleEvent(NULL, &ev);
    assert(in_forward == 0);

    ev.type = EV_KEY_DOWN; ev.key = 's';
    IN_Btron_HandleEvent(NULL, &ev);
    assert(in_back == 1);

    ev.type = EV_KEY_UP; ev.key = 's';
    IN_Btron_HandleEvent(NULL, &ev);
    assert(in_back == 0);

    /* A / D strafe */
    ev.type = EV_KEY_DOWN; ev.key = 'a';
    IN_Btron_HandleEvent(NULL, &ev);
    assert(in_left == 1);

    ev.type = EV_KEY_UP; ev.key = 'a';
    IN_Btron_HandleEvent(NULL, &ev);
    assert(in_left == 0);

    ev.type = EV_KEY_DOWN; ev.key = 'd';
    IN_Btron_HandleEvent(NULL, &ev);
    assert(in_right == 1);

    ev.type = EV_KEY_UP; ev.key = 'd';
    IN_Btron_HandleEvent(NULL, &ev);
    assert(in_right == 0);

    /* SPACE jump */
    ev.type = EV_KEY_DOWN; ev.key = ' ';
    IN_Btron_HandleEvent(NULL, &ev);
    assert(in_jump == 1);

    ev.type = EV_KEY_UP; ev.key = ' ';
    IN_Btron_HandleEvent(NULL, &ev);
    assert(in_jump == 0);

    /* C crouch / down */
    ev.type = EV_KEY_DOWN; ev.key = 'c';
    IN_Btron_HandleEvent(NULL, &ev);
    assert(in_down == 1);

    ev.type = EV_KEY_UP; ev.key = 'c';
    IN_Btron_HandleEvent(NULL, &ev);
    assert(in_down == 0);

    /* Uppercase variants */
    ev.type = EV_KEY_DOWN; ev.key = 'W';
    IN_Btron_HandleEvent(NULL, &ev);
    assert(in_forward == 1);
    ev.type = EV_KEY_UP; ev.key = 'W';
    IN_Btron_HandleEvent(NULL, &ev);
    assert(in_forward == 0);

    ev.type = EV_KEY_DOWN; ev.key = 'S';
    IN_Btron_HandleEvent(NULL, &ev);
    assert(in_back == 1);
    ev.type = EV_KEY_UP; ev.key = 'S';
    IN_Btron_HandleEvent(NULL, &ev);
    assert(in_back == 0);

    ev.type = EV_KEY_DOWN; ev.key = 'C';
    IN_Btron_HandleEvent(NULL, &ev);
    assert(in_down == 1);
    ev.type = EV_KEY_UP; ev.key = 'C';
    IN_Btron_HandleEvent(NULL, &ev);
    assert(in_down == 0);

    printf("  [PASS] Key event translation (WASD, Space, C)\n");

    /* 2. Test Player physics with W/S and A/D */
    g_server.active = 1;
    g_server.frametime = 1.0f / 60.0f;
    g_server.time = 0.0f;
    g_prvm.num_edicts = 2;

    edict_t *player = &g_prvm.edicts[1];
    memset(player, 0, sizeof(*player));
    player->free = 0;
    EF(player, F_MOVETYPE) = (float)MOVETYPE_WALK;
    EF(player, F_FLAGS)    = 512.0f; /* FL_ONGROUND */
    EF(player, F_ORIGIN_X) = 100.0f;
    EF(player, F_ORIGIN_Y) = 200.0f;
    EF(player, F_ORIGIN_Z) = 50.0f;

    /* Facing North (yaw = 90 deg) */
    r_refdef.viewangles[0] = 0.0f;
    r_refdef.viewangles[1] = 90.0f;
    r_refdef.viewangles[2] = 0.0f;

    /* Press 'W' (Forward) -> when facing North (yaw=90), forward is +Y */
    in_forward = 1;
    SV_ServerFrame(1.0f / 60.0f);
    in_forward = 0;

    assert(EF(player, F_ORIGIN_Y) > 200.0f);
    printf("  [PASS] Forward (W) translates player origin Y from 200.0 -> %.2f\n",
           EF(player, F_ORIGIN_Y));

    /* Press 'S' (Back) */
    float cur_y = EF(player, F_ORIGIN_Y);
    in_back = 1;
    SV_ServerFrame(1.0f / 60.0f);
    in_back = 0;
    assert(EF(player, F_ORIGIN_Y) < cur_y);
    printf("  [PASS] Back (S) translates player origin Y from %.2f -> %.2f\n",
           cur_y, EF(player, F_ORIGIN_Y));

    /* Press 'D' (Strafe Right) -> facing North, right is East (+X) */
    in_right = 1;
    SV_ServerFrame(1.0f / 60.0f);
    in_right = 0;
    assert(EF(player, F_ORIGIN_X) > 100.0f);
    printf("  [PASS] Strafe Right (D) translates player origin X from 100.0 -> %.2f\n",
           EF(player, F_ORIGIN_X));

    /* Press 'A' (Strafe Left) */
    float cur_x = EF(player, F_ORIGIN_X);
    in_left = 1;
    SV_ServerFrame(1.0f / 60.0f);
    in_left = 0;
    assert(EF(player, F_ORIGIN_X) < cur_x);
    printf("  [PASS] Strafe Left (A) translates player origin X from %.2f -> %.2f\n",
           cur_x, EF(player, F_ORIGIN_X));

    /* 3. Test SPACE (Jump) */
    EF(player, F_FLAGS) = 512.0f; /* On ground */
    in_jump = 1;
    SV_ServerFrame(1.0f / 60.0f);
    in_jump = 0;
    assert(EF(player, F_ORIGIN_Z) > 50.0f);
    assert(EF(player, F_VELOCITY_Z) > 200.0f);
    assert(!((int)EF(player, F_FLAGS) & 512));
    printf("  [PASS] Jump (SPACE) triggers vertical lift: origin Z from 50.0 -> %.2f (vel Z=%.2f)\n",
           EF(player, F_ORIGIN_Z), EF(player, F_VELOCITY_Z));

    /* 4. Test C (Crouch / Down) */
    in_down = 1;
    SV_ServerFrame(1.0f / 60.0f);
    assert(EF(player, F_MAXS_Z) == 12.0f);
    assert(r_refdef.vieworg[2] == EF(player, F_ORIGIN_Z) + 8.0f);
    printf("  [PASS] Crouch (C) sets bbox maxs_z=12.0f and viewpoint eye_height=8.0f\n");

    in_down = 0;
    SV_ServerFrame(1.0f / 60.0f);
    assert(EF(player, F_MAXS_Z) == 32.0f);
    assert(r_refdef.vieworg[2] == EF(player, F_ORIGIN_Z) + 22.0f);
    printf("  [PASS] Uncrouch restores bbox maxs_z=32.0f and viewpoint eye_height=22.0f\n");

    /* 5. Test Demo Mode Input Suppression */
    printf("\n  Testing Demo Mode Input Suppression...\n");
    g_replay_active = 1;

    /* Key events when g_replay_active == 1 should NOT set input flags */
    ev.type = EV_KEY_DOWN; ev.key = 'w';
    IN_Btron_HandleEvent(NULL, &ev);
    assert(in_forward == 0);

    ev.type = EV_KEY_DOWN; ev.key = 's';
    IN_Btron_HandleEvent(NULL, &ev);
    assert(in_back == 0);

    ev.type = EV_KEY_DOWN; ev.key = 'a';
    IN_Btron_HandleEvent(NULL, &ev);
    assert(in_left == 0);

    ev.type = EV_KEY_DOWN; ev.key = 'd';
    IN_Btron_HandleEvent(NULL, &ev);
    assert(in_right == 0);

    ev.type = EV_KEY_DOWN; ev.key = ' ';
    IN_Btron_HandleEvent(NULL, &ev);
    assert(in_jump == 0);

    ev.type = EV_KEY_DOWN; ev.key = 'c';
    IN_Btron_HandleEvent(NULL, &ev);
    assert(in_down == 0);

    /* Mouse buttons should NOT set attack/jump */
    ev.type = EV_BUT_DOWN; ev.button = 1;
    IN_Btron_HandleEvent(NULL, &ev);
    assert(in_attack == 0);

    ev.type = EV_BUT_DOWN; ev.button = 3;
    IN_Btron_HandleEvent(NULL, &ev);
    assert(in_jump == 0);

    /* Mouse movement should NOT alter viewangles */
    float saved_yaw = r_refdef.viewangles[1];
    float saved_pitch = r_refdef.viewangles[0];
    ev.type = EV_MOUSE_MOVE; ev.pos.x = 200; ev.pos.y = 200;
    IN_Btron_HandleEvent(NULL, &ev);
    assert(r_refdef.viewangles[1] == saved_yaw);
    assert(r_refdef.viewangles[0] == saved_pitch);

    /* Server frame in demo mode should ignore player movement controls */
    float old_px = EF(player, F_ORIGIN_X);
    float old_py = EF(player, F_ORIGIN_Y);
    SV_ServerFrame(1.0f / 60.0f);
    assert(EF(player, F_ORIGIN_X) == old_px);
    assert(EF(player, F_ORIGIN_Y) == old_py);

    g_replay_active = 0;
    printf("  [PASS] Demo mode ignores all keyboard, mouse and server movement inputs\n");

    printf("\n>>> ALL QUAKE CONTROLS TESTS PASSED SUCCESSFULLY! <<<\n");
    return 0;
}
