/*
 * src/quake/core/pr_exec.c — QuakeC Bytecode Interpreter
 *
 * Loads progs.dat from the PAK file system and executes QuakeC functions
 * via a stack-based interpreter.  Supports all 66 opcodes from Quake 1
 * plus a builtin dispatch table for engine callbacks (makevectors, setorigin,
 * bprint, print, etc.).
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#include "../include/progs.h"
#include "../include/quakedef.h"
#include "../include/fs_btron.h"
#include "../include/server.h"
#include "../include/world.h"
#include "../include/r_alias.h"
#include "../include/mathlib.h"
#include "../include/render.h"
#include "../include/cvar.h"
#include <stdio.h>

/* ── Global VM state ─────────────────────────────────────────────────── */
prvm_t g_prvm;

/* ── Dynamic string pool for map strings not in progs.dat ───────────── */
#define DYNAMIC_STRING_BASE 1000000
static char s_dynamic_strings[65536];
static int s_dynamic_strings_len = 0;

/* ── String interning helpers ────────────────────────────────────────── */
const char *PR_GetString(int ofs) {
    if (ofs >= DYNAMIC_STRING_BASE && ofs < DYNAMIC_STRING_BASE + s_dynamic_strings_len)
        return s_dynamic_strings + (ofs - DYNAMIC_STRING_BASE);
    if (!g_prvm.strings || ofs < 0 || ofs >= g_prvm.header->num_strings)
        return "";
    return g_prvm.strings + ofs;
}

/* ftos/vtos hand out a fresh string per call; interning them through
 * PR_SetString would rescan every known string each time. */
static int PR_AllocString(const char *s) {
    int len = (int)strlen(s) + 1;
    if (s_dynamic_strings_len + len > (int)sizeof(s_dynamic_strings)) return 0;
    int ofs = s_dynamic_strings_len;
    memcpy(s_dynamic_strings + ofs, s, (size_t)len);
    s_dynamic_strings_len += len;
    return DYNAMIC_STRING_BASE + ofs;
}

int PR_SetString(const char *s) {
    if (!s) return 0;    if (g_prvm.strings && g_prvm.header) {
        const char *p = g_prvm.strings;
        int limit = g_prvm.header->num_strings;
        for (int i = 0; i < limit; i++) {
            if (p[i] && q_strcasecmp(p + i, s) == 0) return i;
        }
    }
    /* Search dynamic string pool */
    int cur = 0;
    while (cur < s_dynamic_strings_len) {
        if (q_strcasecmp(s_dynamic_strings + cur, s) == 0)
            return DYNAMIC_STRING_BASE + cur;
        cur += (int)strlen(s_dynamic_strings + cur) + 1;
    }
    /* Allocate in dynamic string pool */
    return PR_AllocString(s);
}

/* ── Edict helpers ──────────────────────────────────────────────────── */
edict_t *PROG_TO_EDICT(int prog_ent) {
    if (prog_ent <= 0) return &g_prvm.edicts[0];
    if (prog_ent >= (int)sizeof(edict_t)) {
        int idx = prog_ent / (int)sizeof(edict_t);
        if (idx >= 0 && idx < MAX_EDICTS) return &g_prvm.edicts[idx];
    }
    if (prog_ent >= 0 && prog_ent < MAX_EDICTS) return &g_prvm.edicts[prog_ent];
    return &g_prvm.edicts[0];
}

edict_t *EDICT_NUM(int num) {
    return PROG_TO_EDICT(num);
}

int NUM_FOR_EDICT(const edict_t *ed) {
    if (!ed || ed < g_prvm.edicts) return 0;
    int idx = (int)(ed - g_prvm.edicts);
    if (idx < 0 || idx >= MAX_EDICTS) return 0;
    return idx;
}

eval_t *PR_GetEntityField(edict_t *ed, int ofs) {
    if (ofs < 0 || ofs >= EDICT_FIELDS) return &ed->v[0];
    return &ed->v[ofs];
}

/* ── Error handler ───────────────────────────────────────────────────── */
void PR_RunError(const char *fmt, ...) {
    char buf[256];
    /* Simple formatting without vsnprintf in freestanding mode */
    strncpy(buf, "[PRVM] ", 8);
    strncpy(buf + 7, fmt, 248);
    buf[255] = '\0';
    Con_Printf("%s\n", buf);
}

#define PR_STATEMENT_LIMIT 1000000
#define PR_BUILTIN_SLOTS     256

/*
 * Runaway protection. A QC `while` that never exits — typically because a
 * builtin the port does not implement left a stale entity in the global the
 * loop advances on — freezes the whole game: the interpreter is called
 * synchronously from SV_Physics, so nothing else runs until it returns.
 * id's VM turns that into a Host_Error; here it aborts the activation and
 * keeps the frame alive. The counters are what the headless tests read to
 * tell "slow" from "never finishes".
 */
int   g_pr_statements_executed  = 0;
int   g_pr_runaway_aborts       = 0;
int   g_pr_runaway_statement    = -1;
int   g_pr_runaway_function     = -1;
int   g_pr_unimpl_builtin       = 0;
int   g_pr_unimpl_builtin_count = 0;
int   g_pr_builtin_calls[PR_BUILTIN_SLOTS];
int   g_pr_builtin_missing[PR_BUILTIN_SLOTS];

static int PR_FunctionIndex(const dfunction_t *f) {
    if (!f || !g_prvm.functions) return -1;
    return (int)(f - g_prvm.functions);
}

const char *PR_QCFunctionName(int fnum) {
    if (!g_prvm.header || fnum <= 0 || fnum >= g_prvm.header->num_functions) return "?";
    return PR_GetString(g_prvm.functions[fnum].s_name);
}

int PR_GlobalOfs(const char *name) {
    if (!g_prvm.header || !g_prvm.globaldefs || !name) return -1;
    const int n = g_prvm.header->num_globaldefs;
    for (int i = 0; i < n; i++) {
        const char *nm = PR_GetString(g_prvm.globaldefs[i].s_name);
        if (nm && strcmp(nm, name) == 0) return g_prvm.globaldefs[i].ofs;
    }
    return -1;
}

/* ── Progs loader ───────────────────────────────────────────────────── */
int PR_LoadProgs(const char *path) {
    if (g_prvm.is_loaded) return 1;

    int len = 0;
    byte *data = FS_LoadFile(path, &len);
    if (!data || len < (int)sizeof(dprograms_t)) {
        Con_DPrintf("PR_LoadProgs: %s not found\n", path);
        return 0;
    }

    dprograms_t *hdr = (dprograms_t *)data;
    if (hdr->version != PROG_VERSION) {
        Con_DPrintf("PR_LoadProgs: bad version %d (expected %d)\n",
                    hdr->version, PROG_VERSION);
        return 0;
    }

    g_prvm.progs_data   = data;
    g_prvm.progs_len    = len;
    g_prvm.header       = hdr;
    g_prvm.statements   = (dstatement_t *)(data + hdr->ofs_statements);
    g_prvm.globaldefs   = (ddef_t *)(data + hdr->ofs_globaldefs);
    g_prvm.fielddefs    = (ddef_t *)(data + hdr->ofs_fielddefs);
    g_prvm.functions    = (dfunction_t *)(data + hdr->ofs_functions);
    g_prvm.strings      = (char *)(data + hdr->ofs_strings);
    g_prvm.globals      = (float *)(data + hdr->ofs_globals);

    /* Zero all edicts */
    memset(g_prvm.edicts, 0, sizeof(g_prvm.edicts));
    g_prvm.num_edicts = 1;   /* Edict 0 = world entity */

    g_prvm.depth      = 0;
    g_prvm.is_loaded  = 1;

    Con_Printf("PR_LoadProgs: loaded %s (%d funcs, %d statements)\n",
               path, hdr->num_functions, hdr->num_statements);
    return 1;
}

/* ── Built-in dispatch (vanilla Quake 1 builtins) ──────────────────── */
void PR_ExecuteBuiltin(int bnum) {
    float *globals = g_prvm.globals;
    if (bnum >= 0 && bnum < PR_BUILTIN_SLOTS) g_pr_builtin_calls[bnum]++;
    eval_t *eglobals = (eval_t *)globals;
    (void)globals;
    (void)eglobals;

    switch (bnum) {
        case 1:  /* makevectors(angles) */
            {
                vec3_t ang = { globals[4], globals[5], globals[6] };
                AngleVectors(ang, globals + 59, globals + 65, globals + 62);
            }
            break;
        case 2:  /* setorigin(ent, origin) */
            {
                edict_t *ed = PROG_TO_EDICT(eglobals[4].i);
                EF(ed, F_ORIGIN_X) = globals[7];
                EF(ed, F_ORIGIN_Y) = globals[8];
                EF(ed, F_ORIGIN_Z) = globals[9];
            }
            break;
        case 3:  /* setmodel(ent, model) */
            {
                edict_t *ed = PROG_TO_EDICT(eglobals[4].i);
                const char *m = PR_GetString(eglobals[7].i);
                if (m && m[0]) {
                    EI(ed, F_MODEL) = eglobals[7].i;
                    if (m[0] == '*') {
                        /* Submodel in BSP */
                        int sub = q_atoi(m + 1);
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
                    } else {
                        int mi = R_LoadAliasModel(m);
                        if (mi >= 0) {
                            EF(ed, F_MODELINDEX) = (float)(mi + 1);
                        }
                    }
                }
            }
            break;
        case 4:  /* setsize(ent, mins, maxs) */
            {
                edict_t *ed = PROG_TO_EDICT(eglobals[4].i);
                EF(ed, F_MINS_X) = globals[7];
                EF(ed, F_MINS_Y) = globals[8];
                EF(ed, F_MINS_Z) = globals[9];
                EF(ed, F_MAXS_X) = globals[10];
                EF(ed, F_MAXS_Y) = globals[11];
                EF(ed, F_MAXS_Z) = globals[12];
                EF(ed, F_SIZE_X) = globals[10] - globals[7];
                EF(ed, F_SIZE_Y) = globals[11] - globals[8];
                EF(ed, F_SIZE_Z) = globals[12] - globals[9];
            }
            break;
        case 6:  /* break() */
            break;
        case 7:  /* random() → G_FLOAT(OFS_RETURN) */
            {
                static unsigned s_rng = 0xDEADBEEFu;
                s_rng = s_rng * 1664525u + 1013904223u;
                globals[1] = (float)(s_rng >> 8) * (1.0f / (float)0xFFFFFF);
            }
            break;
        case 8:  /* sound(ent, channel, sample, vol, atten) */
            break;
        case 9:  /* normalize(v) */
            {
                float x = globals[4], y = globals[5], z = globals[6];
                float len = sqrtf(x*x + y*y + z*z);
                if (len > 0.0f) {
                    globals[1] = x / len;
                    globals[2] = y / len;
                    globals[3] = z / len;
                }
            }
            break;
        case 10: /* error(msg) */
        case 11: /* objerror(msg) */
            Con_DPrintf("[QC] error: %s\n", PR_GetString(eglobals[4].i));
            break;
        case 12: /* vlen(v) */
            {
                float x = globals[4], y = globals[5], z = globals[6];
                globals[1] = sqrtf(x*x + y*y + z*z);
            }
            break;
        case 13: /* vectoyaw(v) */
            {
                float dx = globals[4], dy = globals[5];
                if (dx == 0.0f && dy == 0.0f) {
                    globals[1] = 0.0f;
                } else {
                    float yaw = atan2f(dy, dx) * 180.0f / 3.14159265f;
                    if (yaw < 0.0f) yaw += 360.0f;
                    globals[1] = yaw;
                }
            }
            break;
        case 14: /* spawn() -> entity */
            if (g_prvm.num_edicts < MAX_EDICTS) {
                int idx = g_prvm.num_edicts++;
                memset(&g_prvm.edicts[idx], 0, sizeof(edict_t));
                eglobals[1].i = idx;
            } else {
                eglobals[1].i = 0;
            }
            break;
        case 15: /* remove(ent) */
            {
                edict_t *ed = PROG_TO_EDICT(eglobals[4].i);
                ed->free = 1;
            }
            break;
        case 16: /* traceline(v1, v2, nomonsters, forent) */
            {
                float p1[3] = { globals[4], globals[5], globals[6] };
                float p2[3] = { globals[7], globals[8], globals[9] };
                trace_t tr = SV_Move(p1, NULL, NULL, p2, SOLID_BBOX, NULL);
                globals[68] = (float)tr.allsolid;
                globals[69] = (float)tr.startsolid;
                globals[70] = tr.fraction;
                globals[71] = tr.endpos[0];
                globals[72] = tr.endpos[1];
                globals[73] = tr.endpos[2];
                globals[74] = tr.plane_normal[0];
                globals[75] = tr.plane_normal[1];
                globals[76] = tr.plane_normal[2];
                globals[77] = tr.plane_dist;
                eglobals[78].i = tr.ent ? NUM_FOR_EDICT(tr.ent) : 0;
            }
            break;
        case 17: /* checkclient(e) → the client entity to aim/trace at; single player = edict 1 */
            eglobals[1].i = (g_prvm.num_edicts > 1) ? 1 : 0;
            break;
        case 18: /* find(start, field, match_str) → entity */
            {
                int   start_e = eglobals[4].i;
                int   fofs    = eglobals[7].i;   /* field index into edict (QC field type) */
                const char *val = PR_GetString(eglobals[10].i);
                eglobals[1].i = 0;              /* default: world = not found */
                if (val && val[0] && fofs >= 0 && fofs < EDICT_FIELDS) {
                    for (int i = start_e + 1; i < g_prvm.num_edicts; i++) {
                        edict_t *e = &g_prvm.edicts[i];
                        if (e->free) continue;
                        const char *fs = PR_GetString(e->v[fofs].i);
                        if (fs && strcmp(fs, val) == 0) { eglobals[1].i = i; break; }
                    }
                }
            }
            break;
        case 19: /* precache_sound(s) */
            eglobals[1].i = eglobals[4].i;
            break;
        case 20: /* precache_model(s) */
            {
                const char *m = PR_GetString(eglobals[4].i);
                if (m && m[0] && m[0] != '*') {
                    R_LoadAliasModel(m);
                }
                eglobals[1].i = eglobals[4].i;
            }
            break;
        case 21: /* stuffcmd */ break;
        case 22: /* findradius(origin, radius) → chain of entities in sphere */
            {
                float ox = globals[4], oy = globals[5], oz = globals[6];
                float r  = globals[7], r2 = r * r;
                int head = 0;  /* world entity = empty chain */
                for (int i = g_prvm.num_edicts - 1; i >= 1; i--) {
                    edict_t *e = &g_prvm.edicts[i];
                    if (e->free) continue;
                    float dx = EF(e,F_ORIGIN_X)-ox;
                    float dy = EF(e,F_ORIGIN_Y)-oy;
                    float dz = EF(e,F_ORIGIN_Z)-oz;
                    if (dx*dx + dy*dy + dz*dz <= r2) {
                        EI(e, F_CHAIN) = head;
                        head = i;
                    }
                }
                eglobals[1].i = head;
            }
            break;
        case 23: /* bprint(text) → server console */
        case 24: /* sprint(client, text) → that client's console; one local client */
            Con_Printf("[QC] %s", PR_GetString(eglobals[(bnum == 24) ? 7 : 4].i));
            break;
        case 25: /* dprint(text) → developer-only console */
            Con_DPrintf("[QC] %s", PR_GetString(eglobals[4].i));
            break;
        case 26: /* ftos(f) → string */
            {
                char buf[32];
                snprintf(buf, sizeof buf, "%5.1f", globals[4]);
                eglobals[1].i = PR_AllocString(buf);
            }
            break;
        case 27: /* vtos(v) → string */
            {
                char buf[48];
                snprintf(buf, sizeof buf, "[%5.1f %5.1f %5.1f]",
                         globals[4], globals[5], globals[6]);
                eglobals[1].i = PR_AllocString(buf);
            }
            break;
        case 31: /* eprint(ent) → dump an entity's fields to the console */
            {
                edict_t *ed = PROG_TO_EDICT(eglobals[4].i);
                Con_Printf("[QC] ent %d %s\n", NUM_FOR_EDICT(ed),
                           PR_GetString(EI(ed, F_CLASSNAME)));
            }
            break;
        case 32: /* walkmove(yaw, dist) → bool — move monster one step */
            {
                edict_t *ed = PROG_TO_EDICT(((eval_t *)globals)[28].i); /* self */
                float yaw_rad = globals[4] * (3.14159265f / 180.0f);
                float dist    = globals[7];
                float fwd_x   = cosf(yaw_rad);
                float fwd_y   = sinf(yaw_rad);
                float org[3]  = { EF(ed,F_ORIGIN_X), EF(ed,F_ORIGIN_Y), EF(ed,F_ORIGIN_Z) };
                float mins[3] = { EF(ed,F_MINS_X),   EF(ed,F_MINS_Y),   EF(ed,F_MINS_Z)   };
                float maxs[3] = { EF(ed,F_MAXS_X),   EF(ed,F_MAXS_Y),   EF(ed,F_MAXS_Z)   };
                /* Use default monster hull if bbox not set */
                if (mins[0] == 0.0f && maxs[0] == 0.0f) {
                    mins[0]=-16; mins[1]=-16; mins[2]=-24;
                    maxs[0]= 16; maxs[1]= 16; maxs[2]= 32;
                }
                float end[3] = { org[0]+fwd_x*dist, org[1]+fwd_y*dist, org[2] };
                trace_t tr = SV_Move(org, mins, maxs, end, SOLID_SLIDEBOX, ed);
                if (tr.fraction == 1.0f && !tr.allsolid) {
                    /* Step succeeded — commit new position */
                    EF(ed,F_ORIGIN_X) = tr.endpos[0];
                    EF(ed,F_ORIGIN_Y) = tr.endpos[1];
                    /* Re-snap to floor (step-down up to 18 units) */
                    float new_org[3] = { tr.endpos[0], tr.endpos[1], tr.endpos[2] };
                    float down[3]    = { new_org[0], new_org[1], new_org[2] - 18.0f };
                    trace_t vtr = SV_Move(new_org, mins, maxs, down, SOLID_SLIDEBOX, ed);
                    if (vtr.fraction < 1.0f && vtr.plane_normal[2] >= 0.7f) {
                        EF(ed,F_ORIGIN_Z) = vtr.endpos[2];
                    }
                    globals[1] = 1.0f;
                } else {
                    globals[1] = 0.0f;
                }
            }
            break;
        case 34: /* droptofloor() */
            {
                edict_t *ed = PROG_TO_EDICT(eglobals[28].i);
                float org[3]  = { EF(ed, F_ORIGIN_X), EF(ed, F_ORIGIN_Y), EF(ed, F_ORIGIN_Z) };
                float mins[3] = { EF(ed, F_MINS_X),   EF(ed, F_MINS_Y),   EF(ed, F_MINS_Z) };
                float maxs[3] = { EF(ed, F_MAXS_X),   EF(ed, F_MAXS_Y),   EF(ed, F_MAXS_Z) };
                float end[3]  = { org[0], org[1], org[2] - 256.0f };

                trace_t tr = SV_Move(org, mins, maxs, end, SOLID_BBOX, ed);
                if (tr.fraction == 1.0f || tr.allsolid) {
                    globals[1] = 0.0f;
                } else {
                    EF(ed, F_ORIGIN_X) = tr.endpos[0];
                    EF(ed, F_ORIGIN_Y) = tr.endpos[1];
                    EF(ed, F_ORIGIN_Z) = tr.endpos[2] + 1.0f;
                    int flags = (int)EF(ed, F_FLAGS);
                    EF(ed, F_FLAGS) = (float)(flags | 512); /* FL_ONGROUND */
                    globals[1] = 1.0f;
                }
            }
            break;
        case 35: /* lightstyle */
            break;
        case 36: /* rint(f) */
            {
                float v = globals[4];
                globals[1] = (v >= 0.0f) ? (float)(int)(v + 0.5f) : (float)(int)(v - 0.5f);
            }
            break;
        case 37: /* floor(f) */
            { int i = (int)globals[4]; globals[1] = (float)(globals[4] < (float)i ? i-1 : i); }
            break;
        case 38: /* ceil(f) */
            { int i = (int)globals[4]; globals[1] = (float)(globals[4] > (float)i ? i+1 : i); }
            break;
        case 40: /* checkbottom(ent) → bool — is entity standing on solid ground? */
            {
                edict_t *ed = PROG_TO_EDICT(eglobals[4].i);
                float org[3]  = { EF(ed,F_ORIGIN_X), EF(ed,F_ORIGIN_Y), EF(ed,F_ORIGIN_Z) };
                float mins[3] = { EF(ed,F_MINS_X),   EF(ed,F_MINS_Y),   EF(ed,F_MINS_Z)   };
                float maxs[3] = { EF(ed,F_MAXS_X),   EF(ed,F_MAXS_Y),   EF(ed,F_MAXS_Z)   };
                if (mins[0] == 0.0f && maxs[0] == 0.0f) {
                    mins[0]=-16; mins[1]=-16; mins[2]=-24;
                    maxs[0]= 16; maxs[1]= 16; maxs[2]= 32;
                }
                float down[3] = { org[0], org[1], org[2] + mins[2] - 2.0f };
                trace_t tr = SV_Move(org, mins, maxs, down, SOLID_SLIDEBOX, ed);
                globals[1] = (tr.fraction < 1.0f && tr.plane_normal[2] >= 0.7f) ? 1.0f : 0.0f;
            }
            break;
        case 41: /* pointcontents(v) */
            {
                float p[3] = { globals[4], globals[5], globals[6] };
                globals[1] = (float)SV_PointContents(p);
            }
            break;
        case 43: /* fabs(f) */
            globals[1] = fabsf(globals[4]);
            break;
        case 44: /* aim(ent, speed) → forward vector toward nearest enemy */
            {
                /* Return player's view forward direction */
                vec3_t fwd, right, up;
                AngleVectors(r_refdef.viewangles, fwd, right, up);
                globals[1] = fwd[0]; globals[2] = fwd[1]; globals[3] = fwd[2];
            }
            break;
        case 45: /* cvar(name) → its current value */
            globals[1] = Cvar_VariableValue(PR_GetString(eglobals[4].i));
            break;
        case 46: /* localcmd() → pending client command string, none here */
            eglobals[1].i = 0;
            break;
        case 47: /* nextent(ent) → next non-free entity */
            {
                int idx = eglobals[4].i + 1;
                while (idx < g_prvm.num_edicts && g_prvm.edicts[idx].free) idx++;
                eglobals[1].i = (idx < g_prvm.num_edicts) ? idx : 0;
            }
            break;
        case 48: /* particle — no-op */ break;
        case 49: /* ChangeYaw() — step self toward ideal_yaw at yaw_speed */
            {
                edict_t *ed = PROG_TO_EDICT(((eval_t *)globals)[28].i);
                float cur   = EF(ed, F_ANGLES_Y);
                float ideal = EF(ed, F_IDEAL_YAW);
                float spd   = EF(ed, F_YAW_SPEED);
                if (spd <= 0.0f) spd = 10.0f;
                float delta = ideal - cur;
                while (delta >  180.0f) delta -= 360.0f;
                while (delta < -180.0f) delta += 360.0f;
                if (delta >  spd) delta =  spd;
                if (delta < -spd) delta = -spd;
                EF(ed, F_ANGLES_Y) = cur + delta;
            }
            break;
        case 50: /* unused by this progs.dat (no builtin name at slot 50) */ break;
        /*
         * Write* (52..59) append into the client datagram that multicast /
         * broadcast would open.  This build has no net clients, so there is no
         * destination; accept the calls rather than falling through to the
         * unimplemented path, which leaves stale return globals behind.
         */
        case 52: case 53: case 54: case 55:
        case 56: case 57: case 58: case 59:
            break;
        case 51: /* vectoangles(v) → pitch/yaw angles */
            {
                float fx = globals[4], fy = globals[5], fz = globals[6];
                float pitch, yaw;
                if (fy == 0.0f && fx == 0.0f) {
                    yaw   = 0.0f;
                    pitch = (fz > 0.0f) ? 90.0f : -90.0f;
                } else {
                    yaw   = atan2f(fy, fx) * (180.0f / 3.14159265f);
                    if (yaw < 0.0f) yaw += 360.0f;
                    float horiz = sqrtf(fx*fx + fy*fy);
                    pitch = atan2f(fz, horiz) * (180.0f / 3.14159265f);
                    if (pitch < 0.0f) pitch += 360.0f;
                }
                globals[1] = pitch; globals[2] = yaw; globals[3] = 0.0f;
            }
            break;
        case 67: /* movetogoal(dist) — step monster toward goalentity */
            {
                edict_t *self = PROG_TO_EDICT(((eval_t *)globals)[28].i);
                int gi = EI(self, F_GOALENTITY);
                if (gi > 0 && gi < g_prvm.num_edicts) {
                    edict_t *goal = &g_prvm.edicts[gi];
                    float dx = EF(goal,F_ORIGIN_X) - EF(self,F_ORIGIN_X);
                    float dy = EF(goal,F_ORIGIN_Y) - EF(self,F_ORIGIN_Y);
                    float yaw = atan2f(dy, dx) * (180.0f / 3.14159265f);
                    if (yaw < 0.0f) yaw += 360.0f;
                    /* Invoke walkmove(yaw, dist); walkmove reads parm1 at 4, parm2 at 7 */
                    float dist    = globals[4];
                    globals[4] = yaw;
                    globals[7] = dist;
                    PR_ExecuteBuiltin(32);
                } else {
                    globals[1] = 0.0f;
                }
            }
            break;
        case 72: /* cvar_set(name, value) */
            Cvar_Set(PR_GetString(eglobals[4].i), PR_GetString(eglobals[7].i));
            break;
        case 73: /* centerprint(client, text) → centre of that client's HUD */
            Con_Printf("[QC] %s", PR_GetString(eglobals[7].i));
            break;
        case 74: /* ambientsound(origin, sample, vol, atten) — no audio device yet */
            break;
        default:
            /*
             * An unimplemented builtin leaves the return-value globals at
             * whatever the previous call put there. QC that loops over that
             * value (`while (ent != world) ent = ent.chain`) then never
             * terminates, so this must be loud even though it is ignored.
             */
            g_pr_unimpl_builtin       = bnum;
            g_pr_unimpl_builtin_count++;
            if (bnum >= 0 && bnum < PR_BUILTIN_SLOTS) g_pr_builtin_missing[bnum]++;
            if (g_pr_unimpl_builtin_count <= 64)
                Con_Printf("[PRVM] unimplemented builtin %d ignored\n", bnum);
            break;
    }
}

/* ── Bytecode interpreter ────────────────────────────────────────────── */
#define LOCALSTACK_SIZE 2048
static int s_localstack[LOCALSTACK_SIZE];
static int s_localstack_used = 0;

static int PR_EnterFunction(dfunction_t *f) {
    if (g_prvm.depth >= MAX_STACK_DEPTH) {
        PR_RunError("stack overflow");
        return f->first_statement - 1;
    }
    g_prvm.stack[g_prvm.depth].s = g_prvm.xstatement;
    g_prvm.stack[g_prvm.depth].f = g_prvm.xfunction;
    g_prvm.depth++;

    int c = f->locals;
    if (s_localstack_used + c > LOCALSTACK_SIZE) {
        PR_RunError("PR_ExecuteProgram: locals stack overflow");
        return f->first_statement - 1;
    }

    eval_t *eglobals = (eval_t *)g_prvm.globals;
    for (int i = 0; i < c; i++) {
        s_localstack[s_localstack_used + i] = eglobals[f->parm_start + i].i;
    }
    s_localstack_used += c;

    int o = f->parm_start;
    for (int i = 0; i < f->numparms; i++) {
        for (int j = 0; j < f->parm_size[i]; j++) {
            eglobals[o++].i = eglobals[4 + i * 3 + j].i;
        }
    }

    g_prvm.xfunction = f;
    return f->first_statement - 1;
}

static int PR_LeaveFunction(void) {
    if (g_prvm.depth <= 0) {
        return 0;
    }
    if (g_prvm.xfunction) {
        int c = g_prvm.xfunction->locals;
        s_localstack_used -= c;
        if (s_localstack_used < 0) {
            s_localstack_used = 0;
        }
        eval_t *eglobals = (eval_t *)g_prvm.globals;
        for (int i = 0; i < c; i++) {
            eglobals[g_prvm.xfunction->parm_start + i].i = s_localstack[s_localstack_used + i];
        }
    }

    g_prvm.depth--;
    g_prvm.xfunction = g_prvm.stack[g_prvm.depth].f;
    return g_prvm.stack[g_prvm.depth].s;
}

void PR_ExecuteProgram(int fnum) {
    if (!g_prvm.is_loaded) return;
    if (fnum <= 0 || fnum >= g_prvm.header->num_functions) return;

    dfunction_t *f = &g_prvm.functions[fnum];

    /* Builtin functions have first_statement <= 0 */
    if (f->first_statement <= 0) {
        PR_ExecuteBuiltin(-f->first_statement);
        return;
    }

    int exitdepth = g_prvm.depth;
    int s = PR_EnterFunction(f);
    int stmt_budget = PR_STATEMENT_LIMIT;

    float        *globals = g_prvm.globals;
    dstatement_t *stmts   = g_prvm.statements;

    while (1) {
        s++;
        if (s < 0 || s >= g_prvm.header->num_statements) {
            PR_RunError("statement out of range");
            break;
        }

        if (--stmt_budget <= 0) {
            g_pr_runaway_aborts++;
            g_pr_runaway_statement = s;
            g_pr_runaway_function  = PR_FunctionIndex(f);
            if (g_pr_runaway_aborts <= 3) {
                Con_Printf("[PRVM] runaway loop: %d+ statements in %s at statement %d -> aborted\n",
                           PR_STATEMENT_LIMIT,
                           PR_QCFunctionName(g_pr_runaway_function), s);
            }
            while (g_prvm.depth > exitdepth) PR_LeaveFunction();
            break;
        }
        g_pr_statements_executed++;

        g_prvm.xstatement = s;
        dstatement_t *st = &stmts[s];

#define G_EVAL(idx) ((eval_t *)&globals[(idx)])
#define G_VEC(idx)  (&globals[(idx)])
#define OPA   G_EVAL(st->a)
#define OPB   G_EVAL(st->b)
#define OPC   G_EVAL(st->c)
#define V_OPA G_VEC(st->a)
#define V_OPB G_VEC(st->b)
#define V_OPC G_VEC(st->c)

        switch (st->op) {
        case OP_ADD_F:    OPC->f = OPA->f + OPB->f; break;
        case OP_ADD_V:
            V_OPC[0] = V_OPA[0] + V_OPB[0];
            V_OPC[1] = V_OPA[1] + V_OPB[1];
            V_OPC[2] = V_OPA[2] + V_OPB[2];
            break;
        case OP_SUB_F:    OPC->f = OPA->f - OPB->f; break;
        case OP_SUB_V:
            V_OPC[0] = V_OPA[0] - V_OPB[0];
            V_OPC[1] = V_OPA[1] - V_OPB[1];
            V_OPC[2] = V_OPA[2] - V_OPB[2];
            break;
        case OP_MUL_F:    OPC->f = OPA->f * OPB->f; break;
        case OP_MUL_V:
            OPC->f = V_OPA[0]*V_OPB[0] + V_OPA[1]*V_OPB[1] + V_OPA[2]*V_OPB[2]; break;
        case OP_MUL_FV:
            V_OPC[0] = OPA->f * V_OPB[0];
            V_OPC[1] = OPA->f * V_OPB[1];
            V_OPC[2] = OPA->f * V_OPB[2];
            break;
        case OP_MUL_VF:
            V_OPC[0] = V_OPA[0] * OPB->f;
            V_OPC[1] = V_OPA[1] * OPB->f;
            V_OPC[2] = V_OPA[2] * OPB->f;
            break;
        case OP_DIV_F:    OPC->f = (OPB->f != 0.0f) ? OPA->f / OPB->f : 0.0f; break;

        case OP_EQ_F:   OPC->f = (OPA->f == OPB->f) ? 1.0f : 0.0f; break;
        case OP_EQ_V:   OPC->f = (V_OPA[0]==V_OPB[0] && V_OPA[1]==V_OPB[1] && V_OPA[2]==V_OPB[2]) ? 1.0f : 0.0f; break;
        case OP_EQ_S:   OPC->f = (q_strcasecmp(PR_GetString(OPA->i), PR_GetString(OPB->i))==0)?1.0f:0.0f; break;
        case OP_EQ_E:
        case OP_EQ_FNC: OPC->f = (OPA->i == OPB->i) ? 1.0f : 0.0f; break;
        case OP_NE_F:   OPC->f = (OPA->f != OPB->f) ? 1.0f : 0.0f; break;
        case OP_NE_V:   OPC->f = (V_OPA[0]!=V_OPB[0] || V_OPA[1]!=V_OPB[1] || V_OPA[2]!=V_OPB[2]) ? 1.0f : 0.0f; break;
        case OP_NE_S:   OPC->f = (q_strcasecmp(PR_GetString(OPA->i), PR_GetString(OPB->i))!=0)?1.0f:0.0f; break;
        case OP_NE_E:
        case OP_NE_FNC: OPC->f = (OPA->i != OPB->i) ? 1.0f : 0.0f; break;
        case OP_LE:     OPC->f = (OPA->f <= OPB->f) ? 1.0f : 0.0f; break;
        case OP_GE:     OPC->f = (OPA->f >= OPB->f) ? 1.0f : 0.0f; break;
        case OP_LT:     OPC->f = (OPA->f < OPB->f)  ? 1.0f : 0.0f; break;
        case OP_GT:     OPC->f = (OPA->f > OPB->f)  ? 1.0f : 0.0f; break;
        case OP_AND:    OPC->f = (OPA->f != 0.0f && OPB->f != 0.0f) ? 1.0f : 0.0f; break;
        case OP_OR:     OPC->f = (OPA->f != 0.0f || OPB->f != 0.0f) ? 1.0f : 0.0f; break;
        case OP_BITAND: OPC->f = (float)(OPA->i & OPB->i); break;
        case OP_BITOR:  OPC->f = (float)(OPA->i | OPB->i); break;
        case OP_NOT_F:   OPC->f = (OPA->f == 0.0f) ? 1.0f : 0.0f; break;
        case OP_NOT_V:   OPC->f = (V_OPA[0]==0.0f && V_OPA[1]==0.0f && V_OPA[2]==0.0f) ? 1.0f : 0.0f; break;
        case OP_NOT_S:   OPC->f = (!OPA->i || !PR_GetString(OPA->i)[0]) ? 1.0f : 0.0f; break;
        case OP_NOT_ENT: OPC->f = (OPA->i == 0) ? 1.0f : 0.0f; break;
        case OP_NOT_FNC: OPC->f = (OPA->i == 0) ? 1.0f : 0.0f; break;

        case OP_STORE_F:
        case OP_STORE_S:
        case OP_STORE_FNC:
        case OP_STORE_ENT:
        case OP_STORE_FLD: OPB->i = OPA->i; break;
        case OP_STORE_V:
            V_OPB[0]=V_OPA[0]; V_OPB[1]=V_OPA[1]; V_OPB[2]=V_OPA[2]; break;

        case OP_STOREP_F:
        case OP_STOREP_S:
        case OP_STOREP_FNC:
        case OP_STOREP_ENT:
        case OP_STOREP_FLD: {
            int byte_ofs = OPB->i;
            if (byte_ofs >= 0 && byte_ofs + (int)sizeof(eval_t) <= (int)sizeof(g_prvm.edicts)) {
                eval_t *ptr = (eval_t *)((byte *)g_prvm.edicts + byte_ofs);
                ptr->i = OPA->i;
            }
            break; }
        case OP_STOREP_V: {
            int byte_ofs = OPB->i;
            if (byte_ofs >= 0 && byte_ofs + (int)sizeof(vec3_t) <= (int)sizeof(g_prvm.edicts)) {
                float *ptr = (float *)((byte *)g_prvm.edicts + byte_ofs);
                ptr[0] = V_OPA[0];
                ptr[1] = V_OPA[1];
                ptr[2] = V_OPA[2];
            }
            break; }

        case OP_LOAD_F:
        case OP_LOAD_FLD:
        case OP_LOAD_ENT:
        case OP_LOAD_S:
        case OP_LOAD_FNC: {
            edict_t *ed = PROG_TO_EDICT(OPA->i);
            int fld = OPB->i;
            OPC->i = (fld >= 0 && fld < EDICT_FIELDS) ? ed->v[fld].i : 0;
            break; }
        case OP_LOAD_V: {
            edict_t *ed = PROG_TO_EDICT(OPA->i);
            int fld = OPB->i;
            if (fld >= 0 && fld+2 < EDICT_FIELDS) {
                V_OPC[0] = ed->v[fld].f;
                V_OPC[1] = ed->v[fld+1].f;
                V_OPC[2] = ed->v[fld+2].f;
            } else {
                V_OPC[0] = V_OPC[1] = V_OPC[2] = 0.0f;
            }
            break; }
        case OP_ADDRESS: {
            edict_t *ed = PROG_TO_EDICT(OPA->i);
            int fld = OPB->i;
            OPC->i = (int)((byte *)&ed->v[fld] - (byte *)g_prvm.edicts);
            break; }

        case OP_IF:     if (OPA->i != 0) { s += st->b - 1; } break;
        case OP_IFNOT:  if (OPA->i == 0) { s += st->b - 1; } break;
        case OP_GOTO:   s += st->a - 1; break;

        case OP_CALL0: case OP_CALL1: case OP_CALL2: case OP_CALL3:
        case OP_CALL4: case OP_CALL5: case OP_CALL6: case OP_CALL7:
        case OP_CALL8: {
            int callee = OPA->i;
            if (callee > 0 && callee < g_prvm.header->num_functions) {
                dfunction_t *cf = &g_prvm.functions[callee];
                if (cf->first_statement <= 0) {
                    PR_ExecuteBuiltin(-cf->first_statement);
                } else {
                    s = PR_EnterFunction(cf);
                }
            }
            break; }

        case OP_DONE:
        case OP_RETURN: {
            globals[1] = OPA->f;
            globals[2] = (st->a+1 < g_prvm.header->num_globals) ? globals[st->a+1] : 0.0f;
            globals[3] = (st->a+2 < g_prvm.header->num_globals) ? globals[st->a+2] : 0.0f;
            s = PR_LeaveFunction();
            if (g_prvm.depth == exitdepth) {
                return;
            }
            break; }

        case OP_STATE: {
            edict_t *ed = PROG_TO_EDICT(G_EVAL(28)->i); /* self */
            ed->v[F_FRAME].f = OPA->f;
            ed->v[F_NEXTTHINK].f = globals[31] + 0.1f;  /* time + 0.1 */
            ed->v[F_THINK].i = OPB->i;
            break; }

        default: break;
        }

#undef G_EVAL
#undef G_VEC
#undef OPA
#undef OPB
#undef OPC
#undef V_OPA
#undef V_OPB
#undef V_OPC
    }
}

