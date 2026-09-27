/*
 * src/quake/app/quake_ui.c — Quake Menu, Console & Map Loader Implementation
 *
 * Implements:
 *   - Quake In-Game Menu (New Game, Map Select, Controls, Restart, Quit)
 *   - Quake Dropdown Developer Console with command history and logging
 *   - Safe Map Loader / Level Transition (World_ChangeMap)
 *   - Player Combat, Shotgun Viewmodel animation, and Item Pickup detection
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#include "../include/quake_ui.h"
#include "../include/quakedef.h"
#include "../include/render.h"
#include "../include/draw.h"
#include "../include/world.h"
#include "../include/server.h"
#include "../include/texture.h"
#include "../include/r_part.h"
#include "../include/r_light.h"
#include "../include/mathlib.h"
#include "../include/cmd.h"
#include "../include/fs_btron.h"
#include "../include/btron_quake.h"
#include <btron/event.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* UI States */
int g_menu_active    = 0;
int g_console_active = 0;
int g_godmode        = 0;
int g_noclip         = 0;
int g_replay_active  = 0;
static char s_replay_label[40] = "DEMO 1";
static float g_replay_time   = 0.0f;
static int s_ui_w            = 480;
static int s_ui_h            = 360;

/* ── Console State ────────────────────────────────────────────────────── */
#define CON_MAX_LINES 32
#define CON_LINE_LEN  80

static char s_con_lines[CON_MAX_LINES][CON_LINE_LEN];
static int  s_con_num_lines = 0;
static char s_con_input[128] = "";
static int  s_con_input_len = 0;

static char s_con_history[16][128];
static int  s_con_history_count = 0;
static int  s_con_history_pos = -1;

void Con_LogAppend(const char *msg) {
    if (!msg || !*msg) return;
    const char *p = msg;
    while (*p) {
        char linebuf[CON_LINE_LEN];
        int i = 0;
        while (*p && *p != '\n' && i < CON_LINE_LEN - 1) {
            linebuf[i++] = *p++;
        }
        linebuf[i] = '\0';
        if (*p == '\n') p++;

        if (s_con_num_lines < CON_MAX_LINES) {
            strncpy(s_con_lines[s_con_num_lines++], linebuf, CON_LINE_LEN - 1);
        } else {
            /* Shift up */
            memmove(&s_con_lines[0], &s_con_lines[1], sizeof(s_con_lines[0]) * (CON_MAX_LINES - 1));
            strncpy(s_con_lines[CON_MAX_LINES - 1], linebuf, CON_LINE_LEN - 1);
        }
    }
}

/* ── Menu State ───────────────────────────────────────────────────────── */
typedef enum {
    MENU_MAIN     = 0,
    MENU_MAPS     = 1,
    MENU_DEMOS    = 2,
    MENU_CONTROLS = 3
} menu_page_t;

static menu_page_t s_menu_page = MENU_MAIN;
static int s_menu_cursor = 0;

static const char *s_map_list[] = {
    "start", "e1m1", "e1m2", "e1m3", "e1m4", "e1m5", "e1m6", "e1m7", "e1m8"
};

static const char *s_map_titles[] = {
    "1. START: WELCOME TO QUAKE",
    "2. E1M1: SLIPGATE COMPLEX",
    "3. E1M2: CASTLE OF THE DAMNED",
    "4. E1M3: THE NECROPOLIS",
    "5. E1M4: THE GRISLY GROTTO",
    "6. E1M5: GLOOM KEEP",
    "7. E1M6: THE DOOR TO CHTHON",
    "8. E1M7: THE HOUSE OF CHTHON",
    "9. E1M8: ZIGGURAT VERTIGO"
};
#define NUM_MAPS 9

#include "../include/cl_demo.h"

/* The recorded drone runs (verify/tests/test_quake_drone.c, `make test-drone
 * [DRONE_MAP=maps/e1m5.bsp]` or `make test-drone-all`), written next to the pak
 * as loose files so FS_LoadFile's directory search finds them by name.
 * A fixed table rather than a directory scan: the BTRON filesystem has no
 * readdir, and a file that is not there says so when `play` cannot load it. */
static const char *s_drone_files[] = {
    "drone1m1", "drone1m2", "drone1m3", "drone1m4",
    "drone1m5", "drone1m6", "drone1m7", "drone1m8"
};
#define NUM_DRONES ((int)(sizeof s_drone_files / sizeof s_drone_files[0]))

/* One row per drone run, then the stock demo, then the return row — which is
 * drawn and keyed separately as '0', exactly like the level list. */
static const char *s_demo_titles[] = {
    "1. E1M1: SLIPGATE COMPLEX",
    "2. E1M2: CASTLE OF THE DAMNED",
    "3. E1M3: THE NECROPOLIS",
    "4. E1M4: THE GRISLY GROTTO",
    "5. E1M5: GLOOM KEEP",
    "6. E1M6: THE DOOR TO CHTHON",
    "7. E1M7: THE HOUSE OF CHTHON",
    "8. E1M8: ZIGGURAT VERTIGO",
    "9. STOCK DEMO 1: E1M3 FLYTHROUGH"
};
#define NUM_DEMOS 9

/* ── Weapon Combat & Animation State ──────────────────────────────────── */
static int   s_gun_frame = 0;
static float s_gun_anim_time = 0.0f;
static float s_next_attack_time = 0.0f;

/*
 * Weapon table. Slot order matches Quake's number keys: 1 axe .. 8 lightning.
 * Rates and per-hit damage follow the published Quake weapon behaviour; the
 * grenade and rocket launchers trace instantly because the port has no
 * projectile entity yet, so they are given blast damage at the impact point.
 */
typedef struct {
    int         tag;         /* IT_* inventory bit          */
    const char *name;        /* HUD label                   */
    const char *cls;         /* classname fragment to pick it up */
    int         ammo_field;  /* F_AMMO_*, 0 = melee needs no ammo */
    float       cost;        /* ammo spent per shot          */
    float       rate;        /* seconds between shots        */
    int         pellets;     /* rays per shot                */
    float       damage;      /* damage per ray hit           */
    float       spread;      /* per-ray angular deviation    */
    float       kick;        /* pitch recoil in degrees      */
    float       range;       /* ray length                   */
} weapon_def_t;

#define WEAPON_MELEE_RANGE   64.0f
#define WEAPON_SHOT_RANGE  2048.0f

static const weapon_def_t s_weapons[8] = {
    { IT_AXE,             "AXE",         "axe",        0,             0.0f, 0.55f, 1, 40.0f, 0.00f, 1.0f, WEAPON_MELEE_RANGE },
    { IT_SHOTGUN,         "SHOTGUN",     "shotgun",    F_AMMO_SHELLS, 1.0f, 0.45f, 6,  6.0f, 0.08f, 2.0f, WEAPON_SHOT_RANGE  },
    { IT_SUPERSHOTGUN,    "2X SHOTGUN",  "supershotgun", F_AMMO_SHELLS, 2.0f, 1.00f, 14, 6.0f, 0.11f, 4.0f, WEAPON_SHOT_RANGE },
    { IT_NAILGUN,         "NAILGUN",     "nailgun",    F_AMMO_NAILS,  1.0f, 0.20f, 1, 13.0f, 0.02f, 1.0f, WEAPON_SHOT_RANGE  },
    { IT_SUPER_NAILGUN,   "2X NAILGUN",  "supernailgun", F_AMMO_NAILS, 2.0f, 0.12f, 1, 13.0f, 0.06f, 1.5f, WEAPON_SHOT_RANGE },
    { IT_GRENAD_LAUNCHER, "GRENADES",    "grenade",    F_AMMO_ROCKETS, 1.0f, 0.70f, 1, 60.0f, 0.04f, 3.0f, WEAPON_SHOT_RANGE  },
    { IT_ROCKET_LAUNCHER, "ROCKETS",     "rocket",     F_AMMO_ROCKETS, 1.0f, 0.90f, 1, 70.0f, 0.00f, 4.0f, WEAPON_SHOT_RANGE  },
    { IT_LIGHTNING,       "LIGHTNING",   "lightning",  F_AMMO_CELLS,  2.0f, 0.08f, 1, 15.0f, 0.00f, 1.0f, WEAPON_SHOT_RANGE  },
};

/* Held weapon, as a table index. Quake keeps the axe available from boot. */
static int s_weapon_slot = 1; /* IT_SHOTGUN */
static int s_weapon_request = 0;

static const weapon_def_t *Player_HeldWeapon(void) { return &s_weapons[s_weapon_slot]; }

/* F_CURRENTAMMO mirrors the pool of the held weapon so the HUD stays correct. */
static void Player_SyncAmmo(edict_t *player, const weapon_def_t *w) {
    EF(player, F_CURRENTAMMO) = w->ammo_field ? EF(player, w->ammo_field) : 0.0f;
}

const char *Player_WeaponName(void) {
    if (g_prvm.num_edicts < 2) return s_weapons[s_weapon_slot].name;
    return Player_HeldWeapon()->name;
}

void UI_RequestWeapon(int slot) {
    if (slot >= 1 && slot <= 8) s_weapon_request = slot;
}

/*
 * Select slot 1..8. Pressing a key for a weapon that is not carried falls
 * back to the closest owned weapon, first downwards then upwards.
 */
static int Player_SelectWeapon(int slot) {
    if (g_prvm.num_edicts < 2) return 0;
    edict_t *player = &g_prvm.edicts[1];
    if (player->free) return 0;

    int owned = (int)EF(player, F_ITEMS) | IT_AXE;
    int want  = slot - 1;
    int idx   = -1;

    if (owned & s_weapons[want].tag) {
        idx = want;
    } else {
        for (int i = want - 1; i >= 0; i--)
            if (owned & s_weapons[i].tag) { idx = i; break; }
        for (int i = want + 1; i < 8 && idx < 0; i++)
            if (owned & s_weapons[i].tag) { idx = i; break; }
    }
    if (idx < 0 || idx == s_weapon_slot) return 0;

    s_weapon_slot = idx;
    EF(player, F_WEAPON) = (float)s_weapons[idx].tag;
    Player_SyncAmmo(player, &s_weapons[idx]);
    s_gun_frame = 0;
    s_next_attack_time = g_server.time + 0.25f; /* switching costs a beat */

    char msg[48];
    snprintf(msg, sizeof(msg), "[WEAPON] %s", s_weapons[idx].name);
    Con_LogAppend(msg);
    return 1;
}

int Player_GetGunFrame(void) {
    return s_gun_frame;
}

void Player_UpdateAnimation(float dt) {
    if (s_weapon_request) {
        Player_SelectWeapon(s_weapon_request);
        s_weapon_request = 0;
    }

    if (s_gun_frame > 0) {
        s_gun_anim_time += dt;
        if (s_gun_anim_time >= 0.07f) {
            s_gun_anim_time = 0.0f;
            s_gun_frame++;
            if (s_gun_frame >= 7) {
                s_gun_frame = 0;
            }
        }
    }

    /* Check item proximity for health / armor / ammo */
    if (g_prvm.num_edicts >= 2) {
        edict_t *player = &g_prvm.edicts[1];
        if (!player->free) {
            float px = EF(player, F_ORIGIN_X);
            float py = EF(player, F_ORIGIN_Y);
            float pz = EF(player, F_ORIGIN_Z);

            for (int ei = 2; ei < g_prvm.num_edicts; ei++) {
                edict_t *ed = &g_prvm.edicts[ei];
                if (ed->free) continue;
                float ex = EF(ed, F_ORIGIN_X);
                float ey = EF(ed, F_ORIGIN_Y);
                float ez = EF(ed, F_ORIGIN_Z);

                float dx = px - ex, dy = py - ey, dz = pz - ez;
                if (dx*dx + dy*dy + dz*dz < (48.0f * 48.0f)) {
                    const char *cn = PR_GetString(EI(ed, F_CLASSNAME));
                    if (!cn) continue;

                    if (q_strstr(cn, "health") || q_strstr(cn, "medkit")) {
                        float hp = EF(player, F_HEALTH);
                        if (hp < 100.0f) {
                            hp += 25.0f;
                            if (hp > 100.0f) hp = 100.0f;
                            EF(player, F_HEALTH) = hp;
                            R_AddDynamicLight(r_refdef.vieworg, 180.0f, 1.0f);
                            P_RunParticleEffect(r_refdef.vieworg, (float[]){0,0,1}, 0, 16);
                            Con_LogAppend("[ITEM] Medkit: Health +25");
                            ed->free = 1;
                        }
                    } else if (q_strstr(cn, "weapon_")) {
                        /* Reverse scan: "supershotgun" must win over "shotgun" */
                        const weapon_def_t *w = NULL;
                        int slot = 0;
                        for (int i = 7; i >= 0; i--) {
                            if (q_strstr(cn, s_weapons[i].cls)) { w = &s_weapons[i]; slot = i + 1; break; }
                        }
                        if (!w) continue;

                        char msg[48];
                        int owned = (int)EF(player, F_ITEMS);
                        ed->free = 1;
                        R_AddDynamicLight(r_refdef.vieworg, 180.0f, 1.0f);
                        P_RunParticleEffect(r_refdef.vieworg, (float[]){0,0,1}, 0, 16);
                        if (owned & w->tag) {
                            snprintf(msg, sizeof(msg), "[ITEM] %s ammunition", w->name);
                            Con_LogAppend(msg);
                        } else {
                            EF(player, F_ITEMS) = (float)(owned | w->tag);
                            snprintf(msg, sizeof(msg), "[ITEM] You got the %s!", w->name);
                            Con_LogAppend(msg);
                            Player_SelectWeapon(slot);
                        }
                    } else if (q_strstr(cn, "ammo_")) {
                        static const struct {
                            const char *cls; int field; float amount, cap;
                        } kinds[4] = {
                            { "shell",  F_AMMO_SHELLS,  20.0f, 125.0f },
                            { "nail",   F_AMMO_NAILS,   50.0f, 200.0f },
                            { "rocket", F_AMMO_ROCKETS,  5.0f, 250.0f },
                            { "cell",   F_AMMO_CELLS,   50.0f, 250.0f },
                        };
                        char msg[48];
                        for (int i = 0; i < 4; i++) {
                            if (!q_strstr(cn, kinds[i].cls)) continue;
                            float am = EF(player, kinds[i].field);
                            if (am >= kinds[i].cap) break;
                            am += kinds[i].amount;
                            if (am > kinds[i].cap) am = kinds[i].cap;
                            EF(player, kinds[i].field) = am;
                            Player_SyncAmmo(player, Player_HeldWeapon());
                            R_AddDynamicLight(r_refdef.vieworg, 180.0f, 1.0f);
                            P_RunParticleEffect(r_refdef.vieworg, (float[]){0,0,1}, 0, 16);
                            snprintf(msg, sizeof(msg), "[ITEM] Ammo +%d", (int)kinds[i].amount);
                            Con_LogAppend(msg);
                            ed->free = 1;
                            break;
                        }
                    } else if (q_strstr(cn, "armor")) {
                        EF(player, F_ARMORVALUE) = 100.0f;
                        R_AddDynamicLight(r_refdef.vieworg, 180.0f, 1.0f);
                        P_RunParticleEffect(r_refdef.vieworg, (float[]){0,0,1}, 0, 16);
                        Con_LogAppend("[ITEM] Green Armor: Armor +100");
                        ed->free = 1;
                    }
                }
            }
        }
    }
}

void Player_FireWeapon(void) {
    if (g_prvm.num_edicts < 2) return;
    edict_t *player = &g_prvm.edicts[1];
    if (player->free) return;

    const weapon_def_t *w = Player_HeldWeapon();

    if (g_server.time < s_next_attack_time) return;
    s_next_attack_time = g_server.time + w->rate;

    if (w->ammo_field) {
        float pool = EF(player, w->ammo_field);
        if (pool < w->cost) {
            Con_LogAppend("Out of ammo!");
            return;
        }
        EF(player, w->ammo_field) = pool - w->cost;
        Player_SyncAmmo(player, w);
    }

    /* Start firing animation & recoil screen kick */
    s_gun_frame = 1;
    s_gun_anim_time = 0.0f;
    r_refdef.viewangles[0] -= w->kick; /* Pitch recoil */

    /* Muzzle flash */
    R_AddDynamicLight(r_refdef.vieworg, 260.0f, 1.0f);

    /* Compute ray direction */
    vec3_t forward, right, up;
    AngleVectors(r_refdef.viewangles, forward, right, up);

    for (int p = 0; p < w->pellets; p++) {
        float r_spread = ((float)(rand() % 100) / 100.0f - 0.5f) * w->spread;
        float u_spread = ((float)(rand() % 100) / 100.0f - 0.5f) * w->spread;
        float dir[3] = {
            forward[0] + right[0] * r_spread + up[0] * u_spread,
            forward[1] + right[1] * r_spread + up[1] * u_spread,
            forward[2] + right[2] * r_spread + up[2] * u_spread
        };

        float end[3] = {
            r_refdef.vieworg[0] + dir[0] * w->range,
            r_refdef.vieworg[1] + dir[1] * w->range,
            r_refdef.vieworg[2] + dir[2] * w->range
        };

        trace_t tr = SV_Move(r_refdef.vieworg, NULL, NULL, end, SOLID_BBOX, player);

        /* Spark particles on wall hit */
        if (tr.fraction < 1.0f) {
            P_RunParticleEffect(tr.endpos, tr.plane_normal, 0, 4);
            if (p == 0) {
                R_AddDynamicLight(tr.endpos, 70.0f, 0.8f);
            }
        }

        /* Check monster damage & shootable button/door triggers */
        for (int ei = 2; ei < g_prvm.num_edicts; ei++) {
            edict_t *ed = &g_prvm.edicts[ei];
            if (ed->free) continue;
            float eorg[3] = { EF(ed, F_ORIGIN_X), EF(ed, F_ORIGIN_Y), EF(ed, F_ORIGIN_Z) };
            float dx = tr.endpos[0] - eorg[0];
            float dy = tr.endpos[1] - eorg[1];
            float dz = tr.endpos[2] - eorg[2];
            if (dx*dx + dy*dy + dz*dz < (40.0f * 40.0f)) {
                P_BloodSplash(tr.endpos, 12);
                float hp = EF(ed, F_HEALTH);
                if (hp > 0.0f) {
                    hp -= w->damage;
                    EF(ed, F_HEALTH) = hp;
                    if (hp <= 0.0f) {
                        P_ExplosionParticles(eorg);
                        Con_LogAppend("[COMBAT] Target neutralized!");
                        ed->free = 1;
                    }
                }
                /* Trigger shootable doors or buttons (function fields are .i, not .f) */
                int fn = EI(ed, F_USE);
                if (!fn) fn = EI(ed, F_TOUCH);
                if (fn) {
                    ((eval_t *)g_prvm.globals)[28].i = ei;  /* self  */
                    ((eval_t *)g_prvm.globals)[29].i = 1;   /* other = player */
                    PR_ExecuteProgram(fn);
                }
                break;
            }
        }
    }
}

/* ── Map Loader ───────────────────────────────────────────────────────── */
int World_ChangeMap(const char *mapname) {
    if (!mapname || !mapname[0]) return 0;

    char fullpath[64];
    char clean[64];
    strncpy(clean, mapname, sizeof(clean) - 1);
    clean[sizeof(clean) - 1] = '\0';

    const char *p = clean;
    if (strncmp(p, "maps/", 5) == 0) p += 5;

    char base[64];
    strncpy(base, p, sizeof(base) - 1);
    base[sizeof(base) - 1] = '\0';
    char *dot = strstr(base, ".bsp");
    if (dot) *dot = '\0';

    snprintf(fullpath, sizeof(fullpath), "maps/%s.bsp", base);

    char logmsg[80];
    snprintf(logmsg, sizeof(logmsg), "Warping to %s...", fullpath);
    Con_LogAppend(logmsg);

    /* 1. Tear down previous world */
    World_UnloadMap();
    g_num_gl_textures = 0;

    /* 2. Load new BSP file into hunk */
    if (!World_LoadMap(fullpath)) {
        snprintf(logmsg, sizeof(logmsg), "Failed to load map: %s", fullpath);
        Con_LogAppend(logmsg);
        return 0;
    }

    /* 3. Upload BSP textures */
    if (g_bsp_cache.is_ready) {
        const byte *tex_lump = g_bsp_cache.lumps[LUMP_TEXTURES];
        int tex_len = g_bsp_cache.lump_lens[LUMP_TEXTURES];
        if (tex_lump && tex_len > 0) {
            TEX_LoadBSPTextures(tex_lump, tex_len);
        }
    }

    /* 4. Spawn server, player and entities */
    SV_SpawnServer(fullpath);
    s_weapon_slot = 1; /* SV_InitPlayerEdict boots the player with axe + shotgun */
    s_weapon_request = 0;
    s_gun_frame = 0;
    s_next_attack_time = 0.0f;

    /* 5. Set camera vieworg and angles */
    VectorCopy(g_world.spawn_origin, r_refdef.vieworg);
    r_refdef.vieworg[2] += 22.0f; /* Standing eye height */
    r_refdef.viewangles[0] = 0.0f;
    r_refdef.viewangles[1] = g_world.spawn_angle;
    r_refdef.viewangles[2] = 0.0f;

    /* Close menus */
    g_menu_active = 0;
    g_console_active = 0;

    snprintf(logmsg, sizeof(logmsg), "Loaded %s (%d edicts)", fullpath, g_prvm.num_edicts);
    Con_LogAppend(logmsg);
    return 1;
}

/* ── Demo / Replay Playback Implementation ─────────────────────────────── */
/* Play one named track.  A failed `play` used to fall through to demo1, which
 * hides the reason: a drone track is only in assets/quake/ once it has been
 * recorded with `make test-drone-all`.  Report and open the console instead. */
int Replay_StartDemoFile(const char *demopath) {
    char msg[112];
    g_menu_active = 0;
    g_console_active = 0;
    if (!Demo_Play(demopath)) {
        snprintf(msg, sizeof(msg), "play: '%s' not found (run `make test-drone-all`)", demopath);
        Con_LogAppend(msg);
        g_console_active = 1;
        return 0;
    }
    snprintf(s_replay_label, sizeof(s_replay_label), "%s", demopath);
    snprintf(msg, sizeof(msg), "Playing %s", demopath);
    Con_LogAppend(msg);
    return 1;
}

void Replay_StartDemo(int demo_num) {
    if (demo_num < 1) demo_num = 1;
    if (demo_num > 4) demo_num = 4;
    g_replay_time = 0.0f;
    g_menu_active = 0;
    g_console_active = 0;

    const char *demoname = "demo1.dem";
    if (demo_num == 1) demoname = "demo1.dem";
    else if (demo_num == 2) demoname = "demo2.dem";
    else if (demo_num == 3) demoname = "demo3.dem";

    snprintf(s_replay_label, sizeof(s_replay_label), "DEMO %d", demo_num);

    if (!Demo_Play(demoname)) {
        const char *mapname = "maps/e1m3.bsp";
        if (demo_num == 1) mapname = "maps/e1m3.bsp";
        else if (demo_num == 2) mapname = "maps/e1m4.bsp";
        else if (demo_num == 3) mapname = "maps/e1m6.bsp";
        World_ChangeMap(mapname);
        g_replay_active = 1;
    }
    g_menu_active = 0;
    Con_LogAppend("=== Demo Replay Started (Press any key to Play) ===");
}

void Replay_Stop(void) {
    Demo_Stop();
    g_replay_active = 0;
    g_menu_active = 0;
}

/* A synthetic camera spot is legal when it sits in open space with floor
 * within reach below; the orbit otherwise clips through level solids. */
static int Replay_PointLegal(const float *p) {
    if (SV_PointContents(p) == CONTENTS_SOLID) return 0;

    float down[3] = { p[0], p[1], p[2] - 512.0f };
    trace_t tr = SV_Move(p, NULL, NULL, down, SOLID_NOT, NULL);
    if (tr.startsolid || tr.allsolid) return 0;
    return tr.fraction < 1.0f;
}

/* ── Cinematic flythrough ─────────────────────────────────────────────── *
 * Used only when no .dem file can be played back; real demo playback goes   *
 * through Demo_Update() above.                                             */
void Replay_Update(float dt) {
    if (!g_replay_active) return;
    g_replay_time += dt;

    if (Demo_IsPlaying()) {
        Demo_Update(dt);
        return;
    }

    extern refdef_t r_refdef;

    float base_x = g_world.spawn_origin[0];
    float base_y = g_world.spawn_origin[1];
    float base_z = g_world.spawn_origin[2] + 24.0f;

    float ang = g_replay_time * 0.40f;

    /* Shrink the orbit until the camera spot is in open space */
    vec3_t placed = { base_x, base_y, base_z };
    int ok = 0;
    for (int attempt = 0; attempt < 10; attempt++) {
        float r = (160.0f + 60.0f * sinf(g_replay_time * 0.25f)) * (1.0f - attempt * 0.18f);
        float z = base_z + 20.0f * sinf(g_replay_time * 0.6f) - attempt * 16.0f;
        float cand[3] = { base_x + cosf(ang) * r, base_y + sinf(ang) * r, z };
        if (Replay_PointLegal(cand)) {
            placed[0] = cand[0];
            placed[1] = cand[1];
            placed[2] = cand[2];
            ok = 1;
            break;
        }
    }
    if (!ok) {
        /* Nowhere legal near the spawn: hold the eye at the start point */
        placed[0] = base_x;
        placed[1] = base_y;
        placed[2] = base_z;
    }

    r_refdef.vieworg[0] = placed[0];
    r_refdef.vieworg[1] = placed[1];
    r_refdef.vieworg[2] = placed[2];

    r_refdef.viewangles[1] = (ang + 3.14159f) * 180.0f / 3.14159f + 20.0f * sinf(g_replay_time * 0.4f);
    r_refdef.viewangles[0] = -6.0f + 8.0f * sinf(g_replay_time * 0.5f);
    r_refdef.viewangles[2] = 2.0f * sinf(g_replay_time * 0.8f);

    /* Periodically fire weapon to showcase particle effects & gun animation */
    static float s_replay_shot_timer = 0.0f;
    s_replay_shot_timer += dt;
    if (s_replay_shot_timer > 2.2f) {
        s_replay_shot_timer = 0.0f;
        Player_FireWeapon();
    }
}

/* ── Console Commands ─────────────────────────────────────────────────── */
static void Cmd_Map_f(void) {
    if (Cmd_Argc() < 2) {
        Con_LogAppend("Usage: map <mapname> (e.g. e1m1, e1m2, start)");
        return;
    }
    World_ChangeMap(Cmd_Argv(1));
}

static void Cmd_God_f(void) {
    g_godmode = !g_godmode;
    Con_LogAppend(g_godmode ? "God mode ON" : "God mode OFF");
}

static void Cmd_Noclip_f(void) {
    g_noclip = 0;
    if (g_prvm.num_edicts >= 2) {
        EF(&g_prvm.edicts[1], F_MOVETYPE) = (float)MOVETYPE_WALK;
    }
    Con_LogAppend("Noclip is disabled");
}

static void Cmd_Give_f(void) {
    if (g_prvm.num_edicts < 2) return;
    edict_t *player = &g_prvm.edicts[1];
    EF(player, F_HEALTH)      = 100.0f;
    EF(player, F_ARMORVALUE)  = 100.0f;
    EF(player, F_ITEMS)       = (float)IT_ALL_WEAPONS;
    EF(player, F_AMMO_SHELLS) = 125.0f;
    EF(player, F_AMMO_NAILS)  = 200.0f;
    EF(player, F_AMMO_ROCKETS) = 250.0f;
    EF(player, F_AMMO_CELLS)  = 250.0f;
    Player_SyncAmmo(player, Player_HeldWeapon());
    Con_LogAppend("Given every weapon, full health, armor and ammunition!");
}

static void Cmd_Restart_f(void) {
    if (g_world.is_loaded && g_world.name[0]) {
        World_ChangeMap(g_world.name);
    } else {
        World_ChangeMap("maps/e1m1.bsp");
    }
}

static void Cmd_Status_f(void) {
    char buf[80];
    snprintf(buf, sizeof(buf), "Map: %s | Edicts: %d | Time: %.1fs",
             g_world.is_loaded ? g_world.name : "None", g_prvm.num_edicts, g_server.time);
    Con_LogAppend(buf);
    if (g_prvm.num_edicts >= 2) {
        edict_t *p = &g_prvm.edicts[1];
        snprintf(buf, sizeof(buf), "Player pos: (%.0f, %.0f, %.0f) HP: %.0f",
                 EF(p, F_ORIGIN_X), EF(p, F_ORIGIN_Y), EF(p, F_ORIGIN_Z),
                 EF(p, F_HEALTH));
        Con_LogAppend(buf);
        snprintf(buf, sizeof(buf), "Weapon: %s  Ammo: %.0f  Owned: %02x",
                 Player_WeaponName(), EF(p, F_CURRENTAMMO), (int)EF(p, F_ITEMS));
        Con_LogAppend(buf);
    }
}

static void Cmd_Clear_f(void) {
    s_con_num_lines = 0;
}

/* play <track> — the recorded drone runs and the stock demos.  Demo_Play takes
 * the name in any of the forms it searches: `play drone1m1`, `play drone1m5.dem`
 * or a full `play assets/quake/drone1m5.dem` all load the same track. */
static void Cmd_Play_f(void) {
    if (Cmd_Argc() < 2) {
        char line[128];
        line[0] = 0;
        for (int i = 0; i < NUM_DRONES; i++) {
            if (line[0]) strncat(line, " ", sizeof(line) - strlen(line) - 1);
            strncat(line, s_drone_files[i], sizeof(line) - strlen(line) - 1);
        }
        Con_LogAppend("Usage: play <track>");
        Con_LogAppend(line);
        Con_LogAppend("demo1  demo2  demo3  (stock: angles only, camera stays at level start)");
        return;
    }
    Replay_StartDemoFile(Cmd_Argv(1));
}

static void Cmd_Help_f(void) {
    Con_LogAppend("=== Quake 3D Commands ===");
    Con_LogAppend("  map <name>     - Load level (e1m1..e1m8, start)");
    Con_LogAppend("  play <track>   - Drone run (drone1m1..drone1m8) or demo1..3");
    Con_LogAppend("  god            - Toggle godmode");
    Con_LogAppend("  noclip         - Toggle fly/walk through walls");
    Con_LogAppend("  give all       - Own every weapon and fill all ammunition");
    Con_LogAppend("  restart        - Restart current map");
    Con_LogAppend("  status         - Show player coordinates & stats");
    Con_LogAppend("  clear          - Clear console log");
    Con_LogAppend("  help           - Display this help message");
}

/* ── UI Lifecycle ─────────────────────────────────────────────────────── */
void UI_Init(void) {
    Cmd_Init();
    Cmd_AddCommand("map",         Cmd_Map_f);
    Cmd_AddCommand("changelevel", Cmd_Map_f);
    Cmd_AddCommand("play",        Cmd_Play_f);
    Cmd_AddCommand("god",         Cmd_God_f);
    Cmd_AddCommand("noclip",      Cmd_Noclip_f);
    Cmd_AddCommand("give",        Cmd_Give_f);
    Cmd_AddCommand("impulse",     Cmd_Give_f);
    Cmd_AddCommand("restart",     Cmd_Restart_f);
    Cmd_AddCommand("status",      Cmd_Status_f);
    Cmd_AddCommand("clear",       Cmd_Clear_f);
    Cmd_AddCommand("help",        Cmd_Help_f);

    Con_LogAppend("Quake 3D for B-System 3.20 initialized.");
    Con_LogAppend("Type 'help' for available commands.");
}

/* ── UI Input Handling ────────────────────────────────────────────────── */
int UI_HandleKey(UW key) {
    /* If in Replay, only ESC or Enter opens menu; all other inputs are ignored */
    if (g_replay_active) {
        if (key == 0x1B || key == 27 || key == BTRON_KEY_ESCAPE ||
            key == '\r' || key == '\n' || key == BTRON_KEY_RETURN) {
            Replay_Stop();
            g_menu_active = 1;
            return 1;
        }
        return 1;
    }

    /* Toggle Console with ~ / ` or Tab */
    if (key == '`' || key == '~' || key == 0x7E || key == '\t') {
        g_console_active = !g_console_active;
        if (g_console_active) {
            g_menu_active = 0;
            g_replay_active = 0;
        }
        return 1;
    }

    /* Toggle Menu with ESC */
    if (key == 0x1B || key == 27 || key == BTRON_KEY_ESCAPE) {
        if (g_console_active) {
            g_console_active = 0;
            return 1;
        }
        if (g_replay_active) {
            Replay_Stop();
            return 1;
        }
        if (s_menu_page != MENU_MAIN) {
            s_menu_page = MENU_MAIN;
            s_menu_cursor = 0;
            return 1;
        }
        g_menu_active = !g_menu_active;
        return 1;
    }

    /* Console Active Input */
    if (g_console_active) {
        if (key == '\r' || key == '\n') {
            if (s_con_input_len > 0) {
                char cmdline[128];
                snprintf(cmdline, sizeof(cmdline), "] %s", s_con_input);
                Con_LogAppend(cmdline);

                /* Save history */
                if (s_con_history_count < 16) {
                    strncpy(s_con_history[s_con_history_count++], s_con_input, 127);
                } else {
                    memmove(&s_con_history[0], &s_con_history[1], sizeof(s_con_history[0]) * 15);
                    strncpy(s_con_history[15], s_con_input, 127);
                }
                s_con_history_pos = s_con_history_count;

                Cmd_ExecuteString(s_con_input);
                s_con_input[0] = '\0';
                s_con_input_len = 0;
            }
            return 1;
        }
        if (key == 0x08 || key == 0x7F) { /* Backspace */
            if (s_con_input_len > 0) {
                s_con_input[--s_con_input_len] = '\0';
            }
            return 1;
        }
        if (key == 0xFF52 || key == BTRON_KEY_UP /* Up arrow - history */) {
            if (s_con_history_pos > 0) {
                s_con_history_pos--;
                strncpy(s_con_input, s_con_history[s_con_history_pos], 127);
                s_con_input_len = (int)strlen(s_con_input);
            }
            return 1;
        }
        if (key == 0xFF54 || key == BTRON_KEY_DOWN /* Down arrow */) {
            if (s_con_history_pos < s_con_history_count - 1) {
                s_con_history_pos++;
                strncpy(s_con_input, s_con_history[s_con_history_pos], 127);
                s_con_input_len = (int)strlen(s_con_input);
            } else {
                s_con_history_pos = s_con_history_count;
                s_con_input[0] = '\0';
                s_con_input_len = 0;
            }
            return 1;
        }
        /* Printable characters */
        if (key >= 32 && key < 127 && s_con_input_len < 120) {
            s_con_input[s_con_input_len++] = (char)key;
            s_con_input[s_con_input_len] = '\0';
            return 1;
        }
        return 1;
    }

    /* Menu Active Input */
    if (g_menu_active) {
        if (s_menu_page == MENU_MAIN) {
            if (key == 0xFF52 || key == BTRON_KEY_UP || key == 'w' || key == 'W') { /* Up */
                s_menu_cursor = (s_menu_cursor + 7) % 8;
                return 1;
            }
            if (key == 0xFF54 || key == BTRON_KEY_DOWN || key == 's' || key == 'S') { /* Down */
                s_menu_cursor = (s_menu_cursor + 1) % 8;
                return 1;
            }
            if (key >= '1' && key <= '8') { s_menu_cursor = key - '1'; key = '\r'; }

            if (key == '\r' || key == '\n' || key == ' ' || key == BTRON_KEY_RETURN || key == BTRON_KEY_KP_ENTER) {
                switch (s_menu_cursor) {
                case 0: /* New Game: E1M1 */
                    g_replay_active = 0;
                    g_menu_active = 0;
                    World_ChangeMap("maps/e1m1.bsp");
                    break;
                case 1: /* Select Map Submenu */
                    s_menu_page = MENU_MAPS;
                    s_menu_cursor = 0;
                    break;
                case 2: /* Demo / Replay Submenu */
                    s_menu_page = MENU_DEMOS;
                    s_menu_cursor = 0;
                    break;
                case 3: /* Controls */
                    s_menu_page = MENU_CONTROLS;
                    break;
                case 4: /* Developer Console */
                    g_console_active = 1;
                    g_menu_active = 0;
                    break;
                case 5: /* Restart Map */
                    g_replay_active = 0;
                    Cmd_Restart_f();
                    g_menu_active = 0;
                    break;
                case 6: /* Resume */
                    g_menu_active = 0;
                    break;
                case 7: /* Quit */
                    close_quake_window();
                    break;
                }
                return 1;
            }
        } else if (s_menu_page == MENU_MAPS) {
            if (key == 0xFF52 || key == BTRON_KEY_UP || key == 'w' || key == 'W') {
                s_menu_cursor = (s_menu_cursor + NUM_MAPS) % (NUM_MAPS + 1);
                return 1;
            }
            if (key == 0xFF54 || key == BTRON_KEY_DOWN || key == 's' || key == 'S') {
                s_menu_cursor = (s_menu_cursor + 1) % (NUM_MAPS + 1);
                return 1;
            }
            if (key >= '1' && key <= '9') {
                s_menu_cursor = key - '1';
                key = '\r';
            }
            if (key == '0') {
                s_menu_cursor = NUM_MAPS;
                key = '\r';
            }
            if (key == '\r' || key == '\n' || key == ' ' || key == BTRON_KEY_RETURN || key == BTRON_KEY_KP_ENTER) {
                if (s_menu_cursor < NUM_MAPS) {
                    g_replay_active = 0;
                    g_menu_active = 0;
                    World_ChangeMap(s_map_list[s_menu_cursor]);
                } else {
                    s_menu_page = MENU_MAIN;
                    s_menu_cursor = 1;
                }
                return 1;
            }
        } else if (s_menu_page == MENU_DEMOS) {
            /* Rows 0..NUM_DEMOS-1 are tracks, row NUM_DEMOS is the return row
               selected by '0' — the same shape as the level list. */
            if (key == 0xFF52 || key == BTRON_KEY_UP || key == 'w' || key == 'W') {
                s_menu_cursor = (s_menu_cursor + NUM_DEMOS) % (NUM_DEMOS + 1);
                return 1;
            }
            if (key == 0xFF54 || key == BTRON_KEY_DOWN || key == 's' || key == 'S') {
                s_menu_cursor = (s_menu_cursor + 1) % (NUM_DEMOS + 1);
                return 1;
            }
            if (key >= '1' && key <= '9') {
                s_menu_cursor = key - '1';
                key = '\r';
            }
            if (key == '0') {
                s_menu_cursor = NUM_DEMOS;
                key = '\r';
            }
            if (key == '\r' || key == '\n' || key == ' ' || key == BTRON_KEY_RETURN || key == BTRON_KEY_KP_ENTER) {
                if (s_menu_cursor < NUM_DRONES) {
                    Replay_StartDemoFile(s_drone_files[s_menu_cursor]);
                } else if (s_menu_cursor == NUM_DRONES) {
                    Replay_StartDemo(1);
                } else {
                    s_menu_page = MENU_MAIN;
                    s_menu_cursor = 2;
                }
                return 1;
            }
        } else if (s_menu_page == MENU_CONTROLS) {
            if (key == '\r' || key == '\n' || key == ' ' || key == 0x1B || key == 27 ||
                key == BTRON_KEY_RETURN || key == BTRON_KEY_ESCAPE) {
                s_menu_page = MENU_MAIN;
                s_menu_cursor = 3;
                return 1;
            }
        }
        return 1;
    }

    return 0;
}

int UI_HandleMouse(int mx, int my, int button_down) {
    if (!button_down) return 0;

    /* Top HUD Button [ESC] MENU */
    if (mx >= s_ui_w - 185 && mx <= s_ui_w - 95 && my >= 6 && my <= 28) {
        if (g_replay_active) {
            Replay_Stop();
            g_menu_active = 1;
        } else {
            g_menu_active = !g_menu_active;
            if (g_menu_active) g_console_active = 0;
        }
        return 1;
    }

    if (g_replay_active) {
        return 1; /* In demo mode, ignore all other mouse interactions */
    }

    /* Top HUD Button [~] CONSOLE */
    if (mx >= s_ui_w - 94 && mx <= s_ui_w - 4 && my >= 6 && my <= 28) {
        g_console_active = !g_console_active;
        if (g_console_active) {
            g_menu_active = 0;
            g_replay_active = 0;
        }
        return 1;
    }

    if (g_replay_active) {
        Replay_Stop();
        return 1;
    }

    if (g_menu_active) {
        if (s_menu_page == MENU_MAIN) {
            int cx = s_ui_w / 2 - 120;
            int cy = s_ui_h / 2 - 105;
            if (mx >= cx - 20 && mx <= cx + 260) {
                for (int i = 0; i < 8; i++) {
                    int item_y = cy + 36 + i * 20;
                    if (my >= item_y - 2 && my <= item_y + 16) {
                        s_menu_cursor = i;
                        UI_HandleKey('\r');
                        return 1;
                    }
                }
            }
        } else if (s_menu_page == MENU_MAPS) {
            int cx = s_ui_w / 2 - 140;
            int cy = s_ui_h / 2 - 110;
            if (mx >= cx - 20 && mx <= cx + 290) {
                for (int i = 0; i < NUM_MAPS; i++) {
                    int item_y = cy + 24 + i * 16;
                    if (my >= item_y - 2 && my <= item_y + 14) {
                        s_menu_cursor = i;
                        UI_HandleKey('\r');
                        return 1;
                    }
                }
                int back_y = cy + 24 + NUM_MAPS * 16 + 6;
                if (my >= back_y - 2 && my <= back_y + 14) {
                    s_menu_cursor = NUM_MAPS;
                    UI_HandleKey('\r');
                    return 1;
                }
            }
        } else if (s_menu_page == MENU_DEMOS) {
            int cx = s_ui_w / 2 - 140;
            int cy = s_ui_h / 2 - 110;
            if (mx >= cx - 20 && mx <= cx + 290) {
                for (int i = 0; i < NUM_DEMOS; i++) {
                    int item_y = cy + 24 + i * 16;
                    if (my >= item_y - 2 && my <= item_y + 14) {
                        s_menu_cursor = i;
                        UI_HandleKey('\r');
                        return 1;
                    }
                }
                int back_y = cy + 24 + NUM_DEMOS * 16 + 6;
                if (my >= back_y - 2 && my <= back_y + 14) {
                    s_menu_cursor = NUM_DEMOS;
                    UI_HandleKey('\r');
                    return 1;
                }
            }
        }
        return 1;
    }

    return 0;
}

/* ── UI Drawing ───────────────────────────────────────────────────────── */
void UI_Draw(int width, int height) {
    s_ui_w = width;
    s_ui_h = height;

    /* Replay Banner when replay is running */
    if (g_replay_active) {
        Draw_Fill(0, 0, width, 26, 0x181410);
        Draw_Fill(0, 26, width, 1, 0x6A4828);
        char rep_buf[80];
        snprintf(rep_buf, sizeof(rep_buf), "*** REPLAY: %s [%s] (ESC FOR MENU) ***",
                 s_replay_label, g_world.name);
        Draw_String(width / 2 - 160, 8, rep_buf);
    }

    /* 1. Developer Console Overlay */
    if (g_console_active) {
        int con_h = height / 2;
        /* Charcoal translucent background */
        Draw_Fill(0, 0, width, con_h, 0x121216);
        /* Red/Gold dividing line */
        Draw_Fill(0, con_h - 2, width, 2, 0xD49A24);

        Draw_String(16, 12, "=== QUAKE DEVELOPER CONSOLE (BTRON 3.20) ===");

        /* Draw recent log lines */
        int start_y = con_h - 40;
        int drawn = 0;
        for (int i = s_con_num_lines - 1; i >= 0 && drawn < 12; i--) {
            Draw_String(16, start_y - drawn * 14, s_con_lines[i]);
            drawn++;
        }

        /* Prompt */
        char prompt_buf[140];
        snprintf(prompt_buf, sizeof(prompt_buf), "] %s_", s_con_input);
        Draw_String(16, con_h - 18, prompt_buf);
        return;
    }

    /* 2. Menu Overlay */
    if (g_menu_active) {
        /* Semi-transparent dark background */
        Draw_Fill(0, 0, width, height, 0x0A0A0E);

        if (s_menu_page == MENU_MAIN) {
            int cx = width / 2 - 120;
            int cy = height / 2 - 105;

            Draw_String(cx + 40, cy, "Q U A K E");
            Draw_Fill(cx, cy + 18, 240, 2, 0x8C2020);

            const char *items[] = {
                "1. NEW GAME (E1M1)",
                "2. SELECT MISSION LEVEL",
                "3. PLAY REPLAY / DEMO",
                "4. CONTROLS & COMMANDS",
                "5. DEVELOPER CONSOLE",
                "6. RESTART CURRENT MAP",
                "7. RESUME GAME",
                "8. QUIT TO DESKTOP"
            };

            for (int i = 0; i < 8; i++) {
                int item_y = cy + 36 + i * 20;
                if (i == s_menu_cursor) {
                    Draw_Fill(cx - 16, item_y - 2, 272, 16, 0x3A2814);
                    char buf[64];
                    snprintf(buf, sizeof(buf), "> %s <", items[i]);
                    Draw_String(cx - 4, item_y, buf);
                } else {
                    Draw_String(cx, item_y, items[i]);
                }
            }

            Draw_String(cx - 10, cy + 205, "[UP/DOWN/W/S] SELECT  [ENTER] CONFIRM");
        } else if (s_menu_page == MENU_MAPS) {
            int cx = width / 2 - 140;
            int cy = height / 2 - 110;

            Draw_String(cx + 40, cy, "SELECT MISSION LEVEL");
            Draw_Fill(cx, cy + 18, 280, 2, 0x8C2020);

            for (int i = 0; i < NUM_MAPS; i++) {
                int item_y = cy + 24 + i * 16;
                if (i == s_menu_cursor) {
                    Draw_Fill(cx - 12, item_y - 2, 304, 15, 0x3A2814);
                    Draw_String(cx, item_y, s_map_titles[i]);
                } else {
                    Draw_String(cx, item_y, s_map_titles[i]);
                }
            }

            int back_y = cy + 24 + NUM_MAPS * 16 + 6;
            if (s_menu_cursor == NUM_MAPS) {
                Draw_Fill(cx - 12, back_y - 2, 304, 15, 0x3A2814);
                Draw_String(cx, back_y, "0. RETURN TO MAIN MENU");
            } else {
                Draw_String(cx, back_y, "0. RETURN TO MAIN MENU");
            }
        } else if (s_menu_page == MENU_DEMOS) {
            int cx = width / 2 - 140;
            int cy = height / 2 - 110;

            Draw_String(cx + 40, cy, "SELECT DRONE RUN");
            Draw_Fill(cx, cy + 18, 280, 2, 0x8C2020);

            for (int i = 0; i < NUM_DEMOS; i++) {
                int item_y = cy + 24 + i * 16;
                if (i == s_menu_cursor) {
                    Draw_Fill(cx - 12, item_y - 2, 304, 15, 0x3A2814);
                    Draw_String(cx, item_y, s_demo_titles[i]);
                } else {
                    Draw_String(cx, item_y, s_demo_titles[i]);
                }
            }

            int back_y = cy + 24 + NUM_DEMOS * 16 + 6;
            if (s_menu_cursor == NUM_DEMOS) {
                Draw_Fill(cx - 12, back_y - 2, 304, 15, 0x3A2814);
                Draw_String(cx, back_y, "0. RETURN TO MAIN MENU");
            } else {
                Draw_String(cx, back_y, "0. RETURN TO MAIN MENU");
            }
        } else if (s_menu_page == MENU_CONTROLS) {
            int cx = width / 2 - 160;
            int cy = height / 2 - 125;

            Draw_String(cx + 60, cy, "CONTROLS & COMMANDS");
            Draw_Fill(cx, cy + 18, 320, 2, 0x8C2020);

            Draw_String(cx, cy + 30, "W / S          : MOVE FORWARD / BACK");
            Draw_String(cx, cy + 44, "A / D          : STRAFE LEFT / RIGHT");
            Draw_String(cx, cy + 58, "SPACE / R-CLICK: JUMP OVER OBSTACLES");
            Draw_String(cx, cy + 72, "C              : CROUCH / MOVE DOWN");
            Draw_String(cx, cy + 86, "MOUSE MOVE     : 360-DEGREE MOUSELOOK");
            Draw_String(cx, cy + 100,"LEFT CLICK / E : FIRE WEAPON / INTERACT");
            Draw_String(cx, cy + 114,"1 - 8          : SELECT WEAPON");
            Draw_String(cx, cy + 128,"DOORS & LIFTS  : WALK INTO / TOUCH OR SHOOT");
            Draw_String(cx, cy + 142,"BUTTONS        : STEP ON OR SHOOT");
            Draw_String(cx, cy + 156,"ARROWS         : MOVE & TURN (CLASSICAL)");
            Draw_String(cx, cy + 170,"~ OR TAB       : DEVELOPER CONSOLE");
            Draw_String(cx, cy + 184,"ESC            : OPEN / CLOSE THIS MENU");

            Draw_Fill(cx, cy + 192, 320, 1, 0x444455);
            Draw_String(cx, cy + 200, "CONSOLE CHEATS : GOD, NOCLIP, GIVE ALL");
            Draw_String(cx + 40, cy + 225, "[PRESS ENTER OR ESC TO RETURN]");
        }
    }
}
