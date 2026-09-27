/*
 * src/quake/core/sv_main.c — Server Initialisation, Entity Spawning & Game Loop
 *
 * Implements the Quake server entry point for B-System:
 *   - Entity lump parser: walks the BSP entities string, extracts all
 *     key-value pairs into a fixed per-entity table (no heap).
 *   - QC spawn dispatch: for each entity classname, looks up the
 *     matching function in progs fielddefs and calls PR_ExecuteProgram.
 *   - Player edict: initialises edict[1] from info_player_start.
 *   - SV_ServerFrame: advances physics and think scheduling each tick.
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#include "../include/server.h"
#include "../include/world.h"
#include "../include/progs.h"
#include "../include/render.h"
#include "../include/quakedef.h"
#include "../include/fs_btron.h"

/* ── Entity key-value parser ─────────────────────────────────────────── */
#define MAX_ENT_KEYS  32
#define MAX_KEY_LEN   64

typedef struct {
    char key[MAX_KEY_LEN];
    char val[MAX_KEY_LEN];
} kv_t;

typedef struct {
    kv_t pairs[MAX_ENT_KEYS];
    int  nkeys;
} ent_kv_t;

/* Skip whitespace (spaces, tabs, newlines) */
static const char *skip_ws(const char *p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    return p;
}

/* Read a quoted or unquoted token into buf, return pointer past it */
static const char *read_token(const char *p, char *buf, int bufsz) {
    p = skip_ws(p);
    int i = 0;
    if (*p == '"') {
        p++;
        while (*p && *p != '"' && i < bufsz - 1) buf[i++] = *p++;
        if (*p == '"') p++;
    } else {
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' &&
               *p != '{' && *p != '}' && i < bufsz - 1)
            buf[i++] = *p++;
    }
    buf[i] = '\0';
    return p;
}

/* Parse one entity block { "key" "value" ... } from ents string */
static const char *parse_entity_block(const char *p, ent_kv_t *out) {
    out->nkeys = 0;
    p = skip_ws(p);
    if (*p != '{') return p;
    p++;  /* skip '{' */

    while (*p) {
        p = skip_ws(p);
        if (*p == '}') { p++; break; }
        if (*p == '\0') break;

        if (out->nkeys >= MAX_ENT_KEYS) {
            /* Skip overflow pair */
            char tmp[MAX_KEY_LEN];
            p = read_token(p, tmp, sizeof(tmp));
            p = read_token(p, tmp, sizeof(tmp));
            continue;
        }

        kv_t *kv = &out->pairs[out->nkeys];
        p = read_token(p, kv->key, MAX_KEY_LEN);
        p = read_token(p, kv->val, MAX_KEY_LEN);
        if (kv->key[0]) out->nkeys++;
    }
    return p;
}

/* Look up a key in a parsed entity block */
static const char *kv_find(const ent_kv_t *ent, const char *key) {
    for (int i = 0; i < ent->nkeys; i++) {
        if (q_strcasecmp(ent->pairs[i].key, key) == 0)
            return ent->pairs[i].val;
    }
    return NULL;
}

/* ── Find a QC function by name ───────────────────────────────────────── */
static int PR_FindFunction(const char *name) {
    if (!g_prvm.is_loaded || !name) return 0;
    for (int i = 1; i < g_prvm.header->num_functions; i++) {
        const char *fn = PR_GetString(g_prvm.functions[i].s_name);
        if (fn && q_strcasecmp(fn, name) == 0) return i;
    }
    return 0;
}

/* ── Set standard edict fields from a key-value block ─────────────────── */
static void edict_set_kv(edict_t *ed, const ent_kv_t *ent) {
    for (int i = 0; i < ent->nkeys; i++) {
        const char *k = ent->pairs[i].key;
        const char *v = ent->pairs[i].val;

        if (q_strcasecmp(k, "origin") == 0) {
            float x = 0, y = 0, z = 0;
            x = q_atof(v);
            const char *p = v;
            while (*p && *p != ' ') p++;
            while (*p == ' ') p++;
            y = q_atof(p);
            while (*p && *p != ' ') p++;
            while (*p == ' ') p++;
            z = q_atof(p);
            EF(ed, F_ORIGIN_X) = x;
            EF(ed, F_ORIGIN_Y) = y;
            EF(ed, F_ORIGIN_Z) = z;
        } else if (q_strcasecmp(k, "angle") == 0 ||
                   q_strcasecmp(k, "angles") == 0) {
            EF(ed, F_ANGLES_Y) = q_atof(v);  /* Yaw */
        } else if (q_strcasecmp(k, "spawnflags") == 0) {
            EF(ed, F_SPAWNFLAGS) = q_atof(v);
        } else if (q_strcasecmp(k, "speed") == 0) {
            EF(ed, F_SPEED) = q_atof(v);
        } else if (q_strcasecmp(k, "health") == 0) {
            EF(ed, F_HEALTH) = q_atof(v);
        } else if (q_strcasecmp(k, "target") == 0) {
            EI(ed, F_TARGET) = PR_SetString(v);
        } else if (q_strcasecmp(k, "targetname") == 0) {
            EI(ed, F_TARGETNAME) = PR_SetString(v);
        } else if (q_strcasecmp(k, "message") == 0) {
            EI(ed, F_MESSAGE) = PR_SetString(v);
        } else if (q_strcasecmp(k, "model") == 0) {
            EI(ed, F_MODEL) = PR_SetString(v);
            if (v[0] == '*') {
                int sub = q_atoi(v + 1);
                EF(ed, F_MODELINDEX) = (float)(sub + 1000);
                if (g_world.is_loaded && g_world.models && sub >= 0 && sub < g_world.nummodels) {
                    const dmodel_t *mod = &g_world.models[sub];
                    EF(ed, F_MINS_X) = mod->mins[0];
                    EF(ed, F_MINS_Y) = mod->mins[1];
                    EF(ed, F_MINS_Z) = mod->mins[2];
                    EF(ed, F_MAXS_X) = mod->maxs[0];
                    EF(ed, F_MAXS_Y) = mod->maxs[1];
                    EF(ed, F_MAXS_Z) = mod->maxs[2];
                    EF(ed, F_SIZE_X) = mod->maxs[0] - mod->mins[0];
                    EF(ed, F_SIZE_Y) = mod->maxs[1] - mod->mins[1];
                    EF(ed, F_SIZE_Z) = mod->maxs[2] - mod->mins[2];
                }
            }
        }
    }
}

/* ── Spawn all entities from BSP entity lump ─────────────────────────── */
static int SV_SpawnEntities(const char *ents) {
    if (!ents || !g_prvm.is_loaded) return 0;

    int spawned = 0;
    const char *p = ents;

    while (*p) {
        p = skip_ws(p);
        if (!*p) break;
        if (*p != '{') { p++; continue; }

        ent_kv_t ent;
        p = parse_entity_block(p, &ent);

        const char *classname = kv_find(&ent, "classname");
        if (!classname || !classname[0]) continue;

        /* Allocate new edict */
        if (g_prvm.num_edicts >= MAX_EDICTS) {
            Con_DPrintf("SV_SpawnEntities: MAX_EDICTS reached\n");
            break;
        }

        edict_t *ed = &g_prvm.edicts[g_prvm.num_edicts];
        memset(ed, 0, sizeof(*ed));
        ed->free = 0;

        /* Set classname string field */
        EI(ed, F_CLASSNAME) = PR_SetString(classname);

        /* Apply all parsed key-value pairs */
        edict_set_kv(ed, &ent);

        /* Look up and call QC spawn function */
        int fn = PR_FindFunction(classname);
        if (fn > 0) {
            /* Set self = this edict in QC globals (offset 28 = OFS_SELF) */
            ((eval_t *)g_prvm.globals)[28].i = g_prvm.num_edicts;
            PR_ExecuteProgram(fn);
        }

        g_prvm.num_edicts++;
        spawned++;
    }

    Con_Printf("SV_SpawnEntities: spawned %d entities\n", spawned);
    return spawned;
}

/* ── Player edict initialisation ─────────────────────────────────────── */
static void SV_InitPlayerEdict(void) {
    /* Edict 0 = world, edict 1 = player */
    if (g_prvm.num_edicts < 2) g_prvm.num_edicts = 2;

    edict_t *player = &g_prvm.edicts[1];
    memset(player, 0, sizeof(*player));
    player->free = 0;

    /* Position at info_player_start */
    EF(player, F_ORIGIN_X)  = g_world.spawn_origin[0];
    EF(player, F_ORIGIN_Y)  = g_world.spawn_origin[1];
    EF(player, F_ORIGIN_Z)  = g_world.spawn_origin[2];
    EF(player, F_ANGLES_Y)  = g_world.spawn_angle;

    /* Movetype and solid */
    EF(player, F_MOVETYPE)  = (float)MOVETYPE_WALK;
    EF(player, F_SOLID)     = (float)SOLID_SLIDEBOX;

    /* Bounding box (player: -16,-16,-24 to +16,+16,+32) */
    EF(player, F_MINS_X)    = -16.0f;
    EF(player, F_MINS_Y)    = -16.0f;
    EF(player, F_MINS_Z)    = -24.0f;
    EF(player, F_MAXS_X)    =  16.0f;
    EF(player, F_MAXS_Y)    =  16.0f;
    EF(player, F_MAXS_Z)    =  32.0f;

    /* Initial stats */
    EF(player, F_HEALTH)        = 100.0f;
    EF(player, F_AMMO_SHELLS)   = 25.0f;
    EF(player, F_CURRENTAMMO)   = 25.0f;
    EF(player, F_WEAPON)        = (float)IT_SHOTGUN;
    EF(player, F_ITEMS)         = (float)(IT_AXE | IT_SHOTGUN);
    EI(player, F_CLASSNAME)     = PR_SetString("player");

    /* progs.dat declares an edict-typed global named `client` that only the
     * engine can fill.  Trigger callbacks compare `other` against it, so while
     * it holds 0 (the world) every one of them returns on its first statement
     * and sealed doors stay sealed. */
    int ofs_client = PR_GlobalOfs("client");
    if (ofs_client >= 0) {
        ((eval_t *)g_prvm.globals)[ofs_client].i = NUM_FOR_EDICT(player);
        Con_Printf("SV_InitPlayerEdict: bound global client -> edict %d\n",
                   NUM_FOR_EDICT(player));
    }

    Con_Printf("SV_InitPlayerEdict: player at (%.0f %.0f %.0f) yaw=%.0f\n",
               g_world.spawn_origin[0], g_world.spawn_origin[1],
               g_world.spawn_origin[2], g_world.spawn_angle);

}

#include "../include/r_part.h"

#include "../include/quake_ui.h"
#include "../include/mathlib.h"

extern int in_forward, in_back, in_left, in_right;
extern int in_jump, in_down, in_attack, in_turn_left, in_turn_right;

/* ── Server frame (called once per render frame) ─────────────────────── */
void SV_ServerFrame(float dt) {
    if (!g_server.active) return;

    g_server.frametime = dt;
    g_server.time     += dt;

    extern refdef_t r_refdef;

    if (g_replay_active) {
        Replay_Update(dt);
        if (g_prvm.num_edicts >= 2) {
            edict_t *player = &g_prvm.edicts[1];
            EF(player, F_VELOCITY_X) = 0.0f;
            EF(player, F_VELOCITY_Y) = 0.0f;
            EF(player, F_VELOCITY_Z) = 0.0f;
        }
    } else {
        /* Keyboard turning */
        if (!g_menu_active && !g_console_active) {
            if (in_turn_left)  r_refdef.viewangles[1] += 120.0f * dt;
            if (in_turn_right) r_refdef.viewangles[1] -= 120.0f * dt;
            while (r_refdef.viewangles[1] < 0.0f)   r_refdef.viewangles[1] += 360.0f;
            while (r_refdef.viewangles[1] >= 360.0f) r_refdef.viewangles[1] -= 360.0f;
        }

        /* Player movement input */
        edict_t *player = &g_prvm.edicts[1];
        if (g_prvm.num_edicts >= 2 && !player->free && !g_menu_active && !g_console_active) {
            vec3_t forward, right, up;
            vec3_t move_angles = { 0.0f, r_refdef.viewangles[1], 0.0f };
            AngleVectors(move_angles, forward, right, up);

            float wishspeed = 320.0f;
            float mx = 0.0f, my = 0.0f;
            if (in_forward) { mx += forward[0]; my += forward[1]; }
            if (in_back)    { mx -= forward[0]; my -= forward[1]; }
            if (in_right)   { mx += right[0];   my += right[1]; }
            if (in_left)    { mx -= right[0];   my -= right[1]; }

            float len = sqrtf(mx*mx + my*my);
            if (len > 0.01f) {
                float inv = 1.0f / len;
                EF(player, F_VELOCITY_X) = mx * inv * wishspeed;
                EF(player, F_VELOCITY_Y) = my * inv * wishspeed;
            } else {
                EF(player, F_VELOCITY_X) *= 0.6f;
                EF(player, F_VELOCITY_Y) *= 0.6f;
                if (fabsf(EF(player, F_VELOCITY_X)) < 1.0f) EF(player, F_VELOCITY_X) = 0.0f;
                if (fabsf(EF(player, F_VELOCITY_Y)) < 1.0f) EF(player, F_VELOCITY_Y) = 0.0f;
            }

            /* Noclip disabled: strictly enforce MOVETYPE_WALK */
            EF(player, F_MOVETYPE) = (float)MOVETYPE_WALK;

            /* Jump & Crouch/Down movement */
            int contents = SV_PointContents(r_refdef.vieworg);
            if (contents == CONTENTS_WATER || contents == CONTENTS_SLIME || contents == CONTENTS_LAVA) {
                if (in_jump)      EF(player, F_VELOCITY_Z) =  150.0f;
                else if (in_down) EF(player, F_VELOCITY_Z) = -150.0f;
            } else if (in_jump) {
                int flags = (int)EF(player, F_FLAGS);
                if (flags & 512) { /* FL_ONGROUND */
                    EF(player, F_VELOCITY_Z) = 270.0f;
                    EF(player, F_FLAGS) = (float)(flags & ~512);
                }
            }

            /* Crouch shrinks physical bounding box height */
            EF(player, F_MAXS_Z) = in_down ? 12.0f : 32.0f;

            if (in_attack) {
                Player_FireWeapon();
            }
        }
    }

    /* Advance animations & item pickup checks */
    Player_UpdateAnimation(dt);

    /* Run physics for all edicts */
    SV_Physics();

    /* Advance particle simulation */
    P_UpdateParticles(dt);

    /* Sync camera vieworg from player edict origin when not in cinematic replay */
    if (!g_replay_active) {
        edict_t *player = &g_prvm.edicts[1];
        if (!player->free && g_prvm.num_edicts >= 2) {
            float px = EF(player, F_ORIGIN_X);
            float py = EF(player, F_ORIGIN_Y);
            float pz = EF(player, F_ORIGIN_Z);

            /* Safeguard: if player ever falls below map bounds, rescue to spawn origin */
            if (g_world.is_loaded && pz < g_world.mins[2] - 128.0f) {
                px = g_world.spawn_origin[0];
                py = g_world.spawn_origin[1];
                pz = g_world.spawn_origin[2];
                EF(player, F_ORIGIN_X)   = px;
                EF(player, F_ORIGIN_Y)   = py;
                EF(player, F_ORIGIN_Z)   = pz;
                EF(player, F_VELOCITY_X) = 0.0f;
                EF(player, F_VELOCITY_Y) = 0.0f;
                EF(player, F_VELOCITY_Z) = 0.0f;
                EF(player, F_FLAGS)      = 512.0f; /* FL_ONGROUND */
            }

            if (pz != 0.0f || px != 0.0f || py != 0.0f) {
                r_refdef.vieworg[0] = px;
                r_refdef.vieworg[1] = py;
                float eye_height = in_down ? 8.0f : 22.0f; /* Crouch lowers eye viewpoint */
                r_refdef.vieworg[2] = pz + eye_height;
            }
        }
    }
}

/* ── Main map spawn entry ────────────────────────────────────────────── */
void SV_SpawnServer(const char *mapname) {
    Con_Printf("SV_SpawnServer: loading %s\n", mapname ? mapname : "?");

    /* Load QuakeC bytecode */
    if (!g_prvm.is_loaded) {
        if (!PR_LoadProgs("progs.dat")) {
            Con_Printf("SV_SpawnServer: WARNING — progs.dat missing, "
                       "no game logic\n");
        }
    }

    /* Init server + collision hull from g_world.clipnodes */
    SV_Init();

    /* Clear all edicts: edict 0 = world entity */
    for (int i = 0; i < MAX_EDICTS; i++) {
        memset(&g_prvm.edicts[i], 0, sizeof(edict_t));
        g_prvm.edicts[i].free = 1;
    }
    g_prvm.edicts[0].free  = 0;
    g_prvm.num_edicts       = 1;

    /* Call worldspawn QC function (sets gravity, sky, fog, etc.) */
    int worldspawn_fn = PR_FindFunction("worldspawn");
    if (worldspawn_fn > 0) {
        ((eval_t *)g_prvm.globals)[28].i = 0;  /* self = world */
        PR_ExecuteProgram(worldspawn_fn);
    }

    /* Spawn player at info_player_start */
    SV_InitPlayerEdict();

    /* Spawn all map entities */
    if (g_world.entities && g_world.entlen > 0) {
        SV_SpawnEntities(g_world.entities);
    }

    g_server.active = 1;
    g_server.time   = 0.0f;
    Con_Printf("SV_SpawnServer: done — %d edicts\n", g_prvm.num_edicts);
}
