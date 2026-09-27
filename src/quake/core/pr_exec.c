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

int PR_SetString(const char *s) {
    if (!s) return 0;
    if (g_prvm.strings && g_prvm.header) {
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
    int len = (int)strlen(s) + 1;
    if (s_dynamic_strings_len + len < (int)sizeof(s_dynamic_strings)) {
        int ofs = s_dynamic_strings_len;
        memcpy(s_dynamic_strings + ofs, s, (size_t)len);
        s_dynamic_strings_len += len;
        return DYNAMIC_STRING_BASE + ofs;
    }
    return 0;
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
            Con_DPrintf("[QC] error builtin called\n");
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
        case 21: /* stuffcmd */
            break;
        case 25: /* print(s) */
            Con_Printf("[QC] %s", PR_GetString(eglobals[4].i));
            break;
        case 26: /* bprint(s) */
        case 27: /* sprint(ent, s) */
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
        case 41: /* pointcontents(v) */
            {
                float p[3] = { globals[4], globals[5], globals[6] };
                globals[1] = (float)SV_PointContents(p);
            }
            break;
        case 43: /* fabs(f) */
            globals[1] = fabsf(globals[4]);
            break;
        case 45: /* cvar(s) */
            globals[1] = 0.0f;
            break;
        default:
            /* Silently ignore unimplemented builtins */
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

    float        *globals = g_prvm.globals;
    dstatement_t *stmts   = g_prvm.statements;

    while (1) {
        s++;
        if (s < 0 || s >= g_prvm.header->num_statements) {
            PR_RunError("statement out of range");
            break;
        }

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

