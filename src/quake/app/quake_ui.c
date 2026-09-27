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
int g_menu_active    = 1;
int g_console_active = 0;
int g_godmode        = 0;
int g_noclip         = 0;
int g_replay_active  = 0;
static int g_replay_demo_num = 1;
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
    "e1m1", "e1m2", "e1m3", "e1m4",
    "e1m5", "e1m6", "e1m7", "start"
};

static const char *s_map_titles[] = {
    "1. E1M1: The Slipgate Complex",
    "2. E1M2: Castle of the Damned",
    "3. E1M3: The Necropolis",
    "4. E1M4: The Grisly Grotto",
    "5. E1M5: Gloom Keep",
    "6. E1M6: The Door To Chthon",
    "7. E1M7: The House of Chthon",
    "8. START: The Slipgate Hub"
};
#define NUM_MAPS 8

static const char *s_demo_titles[] = {
    "1. Demo 1: The Necropolis (E1M3)",
    "2. Demo 2: The Grisly Grotto (E1M4)",
    "3. Demo 3: The Door To Chthon (E1M6)",
    "4. Cinematic Camera Flythrough (E1M1)",
    "5. Return to Main Menu"
};
#define NUM_DEMOS 5

/* ── Weapon Combat & Animation State ──────────────────────────────────── */
static int   s_gun_frame = 0;
static float s_gun_anim_time = 0.0f;
static float s_next_attack_time = 0.0f;

int Player_GetGunFrame(void) {
    return s_gun_frame;
}

void Player_UpdateAnimation(float dt) {
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
                    const char *cn = PR_GetString((int)EF(ed, F_CLASSNAME));
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
                    } else if (q_strstr(cn, "shell") || q_strstr(cn, "ammo")) {
                        float am = EF(player, F_CURRENTAMMO);
                        if (am < 100.0f) {
                            am += 20.0f;
                            EF(player, F_CURRENTAMMO) = am;
                            EF(player, F_AMMO_SHELLS) = am;
                            R_AddDynamicLight(r_refdef.vieworg, 180.0f, 1.0f);
                            P_RunParticleEffect(r_refdef.vieworg, (float[]){0,0,1}, 0, 16);
                            Con_LogAppend("[ITEM] Shotgun Shells: Ammo +20");
                            ed->free = 1;
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

    if (g_server.time < s_next_attack_time) return;
    s_next_attack_time = g_server.time + 0.45f; /* ~2.2 shots/sec */

    float ammo = EF(player, F_CURRENTAMMO);
    if (ammo <= 0.0f) {
        Con_LogAppend("Out of ammo!");
        return;
    }
    EF(player, F_CURRENTAMMO) = ammo - 1.0f;
    EF(player, F_AMMO_SHELLS) = ammo - 1.0f;

    /* Start firing animation & recoil screen kick */
    s_gun_frame = 1;
    s_gun_anim_time = 0.0f;
    r_refdef.viewangles[0] -= 2.0f; /* Pitch recoil */

    /* Muzzle flash */
    R_AddDynamicLight(r_refdef.vieworg, 260.0f, 1.0f);

    /* Compute ray direction */
    vec3_t forward, right, up;
    AngleVectors(r_refdef.viewangles, forward, right, up);

    /* Emit 6 shotgun pellets */
    for (int p = 0; p < 6; p++) {
        float r_spread = ((float)(rand() % 100) / 100.0f - 0.5f) * 0.08f;
        float u_spread = ((float)(rand() % 100) / 100.0f - 0.5f) * 0.08f;
        float dir[3] = {
            forward[0] + right[0] * r_spread + up[0] * u_spread,
            forward[1] + right[1] * r_spread + up[1] * u_spread,
            forward[2] + right[2] * r_spread + up[2] * u_spread
        };

        float end[3] = {
            r_refdef.vieworg[0] + dir[0] * 2048.0f,
            r_refdef.vieworg[1] + dir[1] * 2048.0f,
            r_refdef.vieworg[2] + dir[2] * 2048.0f
        };

        trace_t tr = SV_Move(r_refdef.vieworg, NULL, NULL, end, SOLID_BBOX, player);

        /* Spark particles on wall hit */
        if (tr.fraction < 1.0f) {
            P_RunParticleEffect(tr.endpos, tr.plane_normal, 0, 4);
            if (p == 0) {
                R_AddDynamicLight(tr.endpos, 70.0f, 0.8f);
            }
        }

        /* Check monster damage */
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
                hp -= 6.0f;
                EF(ed, F_HEALTH) = hp;
                if (hp <= 0.0f) {
                    P_ExplosionParticles(eorg);
                    Con_LogAppend("[COMBAT] Target neutralized!");
                    ed->free = 1;
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
    if (q_strstr(mapname, "maps/")) {
        strncpy(fullpath, mapname, sizeof(fullpath) - 1);
    } else {
        snprintf(fullpath, sizeof(fullpath), "maps/%s.bsp", mapname);
    }
    fullpath[sizeof(fullpath) - 1] = '\0';

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
void Replay_StartDemo(int demo_num) {
    if (demo_num < 1) demo_num = 1;
    if (demo_num > 4) demo_num = 4;
    g_replay_demo_num = demo_num;
    g_replay_time = 0.0f;
    g_menu_active = 0;
    g_console_active = 0;

    const char *mapname = "maps/e1m3.bsp";
    if (demo_num == 1) mapname = "maps/e1m3.bsp";
    else if (demo_num == 2) mapname = "maps/e1m4.bsp";
    else if (demo_num == 3) mapname = "maps/e1m6.bsp";
    else if (demo_num == 4) mapname = "maps/e1m1.bsp";

    World_ChangeMap(mapname);
    g_menu_active = 0;
    g_replay_active = 1;
    Con_LogAppend("=== Demo Replay Started (Press ESC for Menu) ===");
}

void Replay_Stop(void) {
    g_replay_active = 0;
    g_menu_active = 1;
}

void Replay_Update(float dt) {
    if (!g_replay_active) return;
    g_replay_time += dt;

    extern refdef_t r_refdef;

    float base_x = g_world.spawn_origin[0];
    float base_y = g_world.spawn_origin[1];
    float base_z = g_world.spawn_origin[2] + 24.0f;

    /* Smooth 3D flythrough path around the level */
    float r = 160.0f + 60.0f * sinf(g_replay_time * 0.25f);
    float ang = g_replay_time * 0.40f;

    r_refdef.vieworg[0] = base_x + cosf(ang) * r;
    r_refdef.vieworg[1] = base_y + sinf(ang) * r;
    r_refdef.vieworg[2] = base_z + 20.0f * sinf(g_replay_time * 0.6f);

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
    g_noclip = !g_noclip;
    if (g_prvm.num_edicts >= 2) {
        EF(&g_prvm.edicts[1], F_MOVETYPE) = g_noclip ? (float)MOVETYPE_NOCLIP : (float)MOVETYPE_WALK;
    }
    Con_LogAppend(g_noclip ? "Noclip ON" : "Noclip OFF");
}

static void Cmd_Give_f(void) {
    if (g_prvm.num_edicts < 2) return;
    edict_t *player = &g_prvm.edicts[1];
    EF(player, F_HEALTH)        = 100.0f;
    EF(player, F_ARMORVALUE)    = 100.0f;
    EF(player, F_CURRENTAMMO)   = 100.0f;
    EF(player, F_AMMO_SHELLS)   = 100.0f;
    Con_LogAppend("Given full health, armor, and shotgun ammo!");
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
        snprintf(buf, sizeof(buf), "Player pos: (%.0f, %.0f, %.0f) HP: %.0f AMMO: %.0f",
                 EF(p, F_ORIGIN_X), EF(p, F_ORIGIN_Y), EF(p, F_ORIGIN_Z),
                 EF(p, F_HEALTH), EF(p, F_CURRENTAMMO));
        Con_LogAppend(buf);
    }
}

static void Cmd_Clear_f(void) {
    s_con_num_lines = 0;
}

static void Cmd_Help_f(void) {
    Con_LogAppend("=== Quake 3D Commands ===");
    Con_LogAppend("  map <name>     - Load level (e1m1..e1m8, start)");
    Con_LogAppend("  god            - Toggle godmode");
    Con_LogAppend("  noclip         - Toggle fly/walk through walls");
    Con_LogAppend("  give all       - Maximize health, armor, ammo");
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
    /* If in Replay, ESC or Space or Enter opens menu */
    if (g_replay_active) {
        if (key == 0x1B || key == '\r' || key == '\n' || key == ' ') {
            Replay_Stop();
            return 1;
        }
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
    if (key == 0x1B) {
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
        if (key == 0xFF52 /* Up arrow - history */) {
            if (s_con_history_pos > 0) {
                s_con_history_pos--;
                strncpy(s_con_input, s_con_history[s_con_history_pos], 127);
                s_con_input_len = (int)strlen(s_con_input);
            }
            return 1;
        }
        if (key == 0xFF54 /* Down arrow */) {
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
            if (key == 0xFF52 || key == 'w' || key == 'W') { /* Up */
                s_menu_cursor = (s_menu_cursor + 7) % 8;
                return 1;
            }
            if (key == 0xFF54 || key == 's' || key == 'S') { /* Down */
                s_menu_cursor = (s_menu_cursor + 1) % 8;
                return 1;
            }
            if (key >= '1' && key <= '8') { s_menu_cursor = key - '1'; key = '\r'; }

            if (key == '\r' || key == '\n' || key == ' ') {
                switch (s_menu_cursor) {
                case 0: /* New Game: E1M1 */
                    g_replay_active = 0;
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
            if (key == 0xFF52 || key == 'w' || key == 'W') {
                s_menu_cursor = (s_menu_cursor + NUM_MAPS) % (NUM_MAPS + 1);
                return 1;
            }
            if (key == 0xFF54 || key == 's' || key == 'S') {
                s_menu_cursor = (s_menu_cursor + 1) % (NUM_MAPS + 1);
                return 1;
            }
            if (key >= '1' && key <= '8') {
                s_menu_cursor = key - '1';
                key = '\r';
            }
            if (key == '\r' || key == '\n' || key == ' ') {
                if (s_menu_cursor < NUM_MAPS) {
                    g_replay_active = 0;
                    World_ChangeMap(s_map_list[s_menu_cursor]);
                } else {
                    s_menu_page = MENU_MAIN;
                    s_menu_cursor = 1;
                }
                return 1;
            }
        } else if (s_menu_page == MENU_DEMOS) {
            if (key == 0xFF52 || key == 'w' || key == 'W') {
                s_menu_cursor = (s_menu_cursor + NUM_DEMOS - 1) % NUM_DEMOS;
                return 1;
            }
            if (key == 0xFF54 || key == 's' || key == 'S') {
                s_menu_cursor = (s_menu_cursor + 1) % NUM_DEMOS;
                return 1;
            }
            if (key >= '1' && key <= '5') {
                s_menu_cursor = key - '1';
                key = '\r';
            }
            if (key == '\r' || key == '\n' || key == ' ') {
                if (s_menu_cursor < 4) {
                    Replay_StartDemo(s_menu_cursor + 1);
                } else {
                    s_menu_page = MENU_MAIN;
                    s_menu_cursor = 2;
                }
                return 1;
            }
        } else if (s_menu_page == MENU_CONTROLS) {
            if (key == '\r' || key == '\n' || key == ' ' || key == 0x1B) {
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
        } else {
            g_menu_active = !g_menu_active;
            if (g_menu_active) g_console_active = 0;
        }
        return 1;
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
                for (int i = 0; i < NUM_MAPS + 1; i++) {
                    int item_y = cy + 32 + i * 18 + (i == NUM_MAPS ? 6 : 0);
                    if (my >= item_y - 2 && my <= item_y + 15) {
                        s_menu_cursor = i;
                        UI_HandleKey('\r');
                        return 1;
                    }
                }
            }
        } else if (s_menu_page == MENU_DEMOS) {
            int cx = s_ui_w / 2 - 140;
            int cy = s_ui_h / 2 - 110;
            if (mx >= cx - 20 && mx <= cx + 290) {
                for (int i = 0; i < NUM_DEMOS; i++) {
                    int item_y = cy + 36 + i * 20 + (i == NUM_DEMOS - 1 ? 6 : 0);
                    if (my >= item_y - 2 && my <= item_y + 16) {
                        s_menu_cursor = i;
                        UI_HandleKey('\r');
                        return 1;
                    }
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
        snprintf(rep_buf, sizeof(rep_buf), "*** REPLAY: DEMO %d [%s] (ESC FOR MENU) ***",
                 g_replay_demo_num, g_world.name);
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
                "1. New Game (E1M1)",
                "2. Select Mission Level",
                "3. Play Replay / Demo",
                "4. Controls & Cheats",
                "5. Developer Console",
                "6. Restart Current Map",
                "7. Resume Game",
                "8. Quit to Desktop"
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

            Draw_String(cx - 10, cy + 205, "[UP/DOWN] Select  [ENTER] Confirm");
        } else if (s_menu_page == MENU_MAPS) {
            int cx = width / 2 - 140;
            int cy = height / 2 - 110;

            Draw_String(cx + 40, cy, "SELECT MISSION LEVEL");
            Draw_Fill(cx, cy + 18, 280, 2, 0x8C2020);

            for (int i = 0; i < NUM_MAPS; i++) {
                int item_y = cy + 32 + i * 18;
                if (i == s_menu_cursor) {
                    Draw_Fill(cx - 12, item_y - 2, 304, 15, 0x3A2814);
                    Draw_String(cx, item_y, s_map_titles[i]);
                } else {
                    Draw_String(cx, item_y, s_map_titles[i]);
                }
            }

            int back_y = cy + 32 + NUM_MAPS * 18 + 8;
            if (s_menu_cursor == NUM_MAPS) {
                Draw_Fill(cx - 12, back_y - 2, 304, 15, 0x3A2814);
                Draw_String(cx, back_y, "9. Return to Main Menu");
            } else {
                Draw_String(cx, back_y, "9. Return to Main Menu");
            }
        } else if (s_menu_page == MENU_DEMOS) {
            int cx = width / 2 - 140;
            int cy = height / 2 - 110;

            Draw_String(cx + 40, cy, "SELECT DEMO / REPLAY");
            Draw_Fill(cx, cy + 18, 280, 2, 0x8C2020);

            for (int i = 0; i < NUM_DEMOS; i++) {
                int item_y = cy + 36 + i * 20 + (i == NUM_DEMOS - 1 ? 6 : 0);
                if (i == s_menu_cursor) {
                    Draw_Fill(cx - 12, item_y - 2, 304, 16, 0x3A2814);
                    Draw_String(cx, item_y, s_demo_titles[i]);
                } else {
                    Draw_String(cx, item_y, s_demo_titles[i]);
                }
            }
        } else if (s_menu_page == MENU_CONTROLS) {
            int cx = width / 2 - 160;
            int cy = height / 2 - 110;

            Draw_String(cx + 60, cy, "CONTROLS & COMMANDS");
            Draw_Fill(cx, cy + 18, 320, 2, 0x8C2020);

            Draw_String(cx, cy + 36, "WASD / Arrows  : Move forward/strafe/turn");
            Draw_String(cx, cy + 54, "Mouse Drag     : 360-degree mouselook");
            Draw_String(cx, cy + 72, "Left Click / E : Fire Shotgun / Interact");
            Draw_String(cx, cy + 90, "Spacebar       : Jump over obstacles");
            Draw_String(cx, cy + 108,"~ or Tab       : Toggle Developer Console");
            Draw_String(cx, cy + 126,"ESC            : Open / Close this Menu");

            Draw_Fill(cx, cy + 150, 320, 1, 0x444455);
            Draw_String(cx, cy + 160, "Console Cheats: god, noclip, give all");
            Draw_String(cx + 60, cy + 195, "[PRESS ENTER OR ESC TO RETURN]");
        }
    }
}
