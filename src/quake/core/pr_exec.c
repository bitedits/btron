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

/* ── Global VM state ─────────────────────────────────────────────────── */
prvm_t g_prvm;

/* ── String interning helpers ────────────────────────────────────────── */
const char *PR_GetString(int ofs) {
    if (!g_prvm.strings || ofs < 0 || ofs >= g_prvm.header->num_strings)
        return "";
    return g_prvm.strings + ofs;
}

int PR_SetString(const char *s) {
    /* For now return offset from start of string table if found */
    if (!s || !g_prvm.strings) return 0;
    const char *p = g_prvm.strings;
    int limit = g_prvm.header->num_strings;
    for (int i = 0; i < limit; i++) {
        if (p[i] && q_strcasecmp(p + i, s) == 0) return i;
    }
    return 0;
}

/* ── Edict helpers ──────────────────────────────────────────────────── */
edict_t *EDICT_NUM(int num) {
    if (num < 0 || num >= MAX_EDICTS) return &g_prvm.edicts[0];
    return &g_prvm.edicts[num];
}

int NUM_FOR_EDICT(const edict_t *ed) {
    return (int)(ed - g_prvm.edicts);
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

/* ── Built-in dispatch (subset for E1M1 playability) ────────────────── */
void PR_ExecuteBuiltin(int bnum) {
    float *globals = g_prvm.globals;
    (void)globals;

    switch (bnum) {
        case 1:  /* makevectors(angles) */
        case 2:  /* setorigin(ent, origin) — no-op stub */
        case 3:  /* setmodel(ent, model) */
        case 4:  /* setsize(ent, mins, maxs) */
        case 6:  /* break() */
            break;
        case 7:  /* random() → G_FLOAT(OFS_RETURN) */
            /* Simple LCG: avoid stdlib rand() for freestanding builds */
            {
                static unsigned s_rng = 0xDEADBEEFu;
                s_rng = s_rng * 1664525u + 1013904223u;
                globals[1] = (float)(s_rng >> 8) * (1.0f / (float)0xFFFFFF);
            }
            break;
        case 8:  /* sound(ent, channel, sample, vol, atten) — stub */
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
        case 14: /* ceil(f) */
            { int i = (int)globals[4]; globals[1] = (float)(globals[4] > (float)i ? i+1 : i); }
            break;
        case 25: /* print(s) */
            Con_Printf("[QC] %s", PR_GetString((int)globals[4]));
            break;
        case 26: /* bprint(s) */
        case 27: /* sprint(ent, s) */
            break;
        case 36: /* floor(f) */
            { int i = (int)globals[4]; globals[1] = (float)(globals[4] < (float)i ? i-1 : i); }
            break;
        default:
            /* Silently ignore unimplemented builtins */
            break;
    }
}

/* ── Bytecode interpreter ────────────────────────────────────────────── */
void PR_ExecuteProgram(int fnum) {
    if (!g_prvm.is_loaded) return;
    if (fnum <= 0 || fnum >= g_prvm.header->num_functions) return;

    dfunction_t *f = &g_prvm.functions[fnum];

    /* Builtin functions have first_statement <= 0 */
    if (f->first_statement <= 0) {
        PR_ExecuteBuiltin(-f->first_statement);
        return;
    }

    if (g_prvm.depth >= MAX_STACK_DEPTH) {
        PR_RunError("stack overflow");
        return;
    }

    /* Push call frame */
    prstack_t *frame = &g_prvm.stack[g_prvm.depth++];
    frame->s = g_prvm.xstatement;
    frame->f = f;

    float        *globals = g_prvm.globals;
    dstatement_t *stmts   = g_prvm.statements;
    int           s       = f->first_statement - 1;

    while (1) {
        s++;
        if (s < 0 || s >= g_prvm.header->num_statements) {
            PR_RunError("statement out of range");
            break;
        }

        g_prvm.xstatement = s;
        dstatement_t *st = &stmts[s];

#define OPA  (globals + st->a)
#define OPB  (globals + st->b)
#define OPC  (globals + st->c)

        switch (st->op) {
        case OP_ADD_F:    *OPC = *OPA + *OPB; break;
        case OP_ADD_V:
            OPC[0]=OPA[0]+OPB[0]; OPC[1]=OPA[1]+OPB[1]; OPC[2]=OPA[2]+OPB[2];
            break;
        case OP_SUB_F:    *OPC = *OPA - *OPB; break;
        case OP_SUB_V:
            OPC[0]=OPA[0]-OPB[0]; OPC[1]=OPA[1]-OPB[1]; OPC[2]=OPA[2]-OPB[2];
            break;
        case OP_MUL_F:    *OPC = *OPA * *OPB; break;
        case OP_MUL_V:
            *OPC = OPA[0]*OPB[0] + OPA[1]*OPB[1] + OPA[2]*OPB[2]; break;
        case OP_MUL_FV:
            OPC[0]=*OPA*OPB[0]; OPC[1]=*OPA*OPB[1]; OPC[2]=*OPA*OPB[2]; break;
        case OP_MUL_VF:
            OPC[0]=OPA[0]**OPB; OPC[1]=OPA[1]**OPB; OPC[2]=OPA[2]**OPB; break;
        case OP_DIV_F:    *OPC = (*OPB != 0.0f) ? *OPA / *OPB : 0.0f; break;

        case OP_EQ_F:   *OPC = (*OPA == *OPB) ? 1.0f : 0.0f; break;
        case OP_EQ_V:   *OPC = (OPA[0]==OPB[0]&&OPA[1]==OPB[1]&&OPA[2]==OPB[2]) ? 1.f : 0.f; break;
        case OP_EQ_S:   *OPC = (q_strcasecmp(PR_GetString((int)*OPA), PR_GetString((int)*OPB))==0)?1.f:0.f; break;
        case OP_EQ_E:
        case OP_EQ_FNC: *OPC = (*OPA == *OPB) ? 1.0f : 0.0f; break;
        case OP_NE_F:   *OPC = (*OPA != *OPB) ? 1.0f : 0.0f; break;
        case OP_NE_V:   *OPC = (OPA[0]!=OPB[0]||OPA[1]!=OPB[1]||OPA[2]!=OPB[2]) ? 1.f : 0.f; break;
        case OP_NE_S:   *OPC = (q_strcasecmp(PR_GetString((int)*OPA), PR_GetString((int)*OPB))!=0)?1.f:0.f; break;
        case OP_NE_E:
        case OP_NE_FNC: *OPC = (*OPA != *OPB) ? 1.0f : 0.0f; break;
        case OP_LE:     *OPC = (*OPA <= *OPB) ? 1.0f : 0.0f; break;
        case OP_GE:     *OPC = (*OPA >= *OPB) ? 1.0f : 0.0f; break;
        case OP_LT:     *OPC = (*OPA < *OPB)  ? 1.0f : 0.0f; break;
        case OP_GT:     *OPC = (*OPA > *OPB)  ? 1.0f : 0.0f; break;
        case OP_AND:    *OPC = (*OPA && *OPB) ? 1.0f : 0.0f; break;
        case OP_OR:     *OPC = (*OPA || *OPB) ? 1.0f : 0.0f; break;
        case OP_BITAND: *OPC = (float)((int)*OPA & (int)*OPB); break;
        case OP_BITOR:  *OPC = (float)((int)*OPA | (int)*OPB); break;
        case OP_NOT_F:  *OPC = (*OPA == 0.0f) ? 1.0f : 0.0f; break;
        case OP_NOT_V:  *OPC = (!OPA[0]&&!OPA[1]&&!OPA[2]) ? 1.f : 0.f; break;
        case OP_NOT_S:  *OPC = (!*OPA || !PR_GetString((int)*OPA)[0]) ? 1.f : 0.f; break;
        case OP_NOT_ENT:
        case OP_NOT_FNC: *OPC = (*OPA == 0.0f) ? 1.0f : 0.0f; break;

        case OP_STORE_F:
        case OP_STORE_S:
        case OP_STORE_FNC:
        case OP_STORE_ENT:
        case OP_STORE_FLD: *OPB = *OPA; break;
        case OP_STORE_V:   OPB[0]=OPA[0]; OPB[1]=OPA[1]; OPB[2]=OPA[2]; break;

        case OP_STOREP_F:
        case OP_STOREP_S:
        case OP_STOREP_FNC:
        case OP_STOREP_ENT:
        case OP_STOREP_FLD:
            { int ptr = (int)*OPB; if (ptr >= 0 && ptr < g_prvm.header->num_globals) globals[ptr] = *OPA; break; }
        case OP_STOREP_V:
            { int ptr = (int)*OPB; if (ptr >= 0 && ptr+2 < g_prvm.header->num_globals) { globals[ptr]=OPA[0]; globals[ptr+1]=OPA[1]; globals[ptr+2]=OPA[2]; } break; }

        case OP_LOAD_F:
        case OP_LOAD_FLD:
        case OP_LOAD_ENT:
        case OP_LOAD_S:
        case OP_LOAD_FNC: {
            edict_t *ed = EDICT_NUM((int)*OPA);
            int fld = (int)*OPB;
            *OPC = (fld >= 0 && fld < EDICT_FIELDS) ? ed->v[fld].f : 0.0f;
            break; }
        case OP_LOAD_V: {
            edict_t *ed = EDICT_NUM((int)*OPA);
            int fld = (int)*OPB;
            if (fld >= 0 && fld+2 < EDICT_FIELDS) {
                OPC[0]=ed->v[fld].v[0]; OPC[1]=ed->v[fld].v[1]; OPC[2]=ed->v[fld].v[2];
            } break; }
        case OP_ADDRESS: {
            edict_t *ed = EDICT_NUM((int)*OPA);
            int fld = (int)*OPB;
            *OPC = (float)(((char *)&ed->v[fld]) - ((char *)globals));
            break; }

        case OP_IF:     if (*OPA != 0.0f) { s += st->b - 1; } break;
        case OP_IFNOT:  if (*OPA == 0.0f) { s += st->b - 1; } break;
        case OP_GOTO:   s += st->a - 1; break;

        case OP_CALL0: case OP_CALL1: case OP_CALL2: case OP_CALL3:
        case OP_CALL4: case OP_CALL5: case OP_CALL6: case OP_CALL7:
        case OP_CALL8: {
            int callee = (int)*OPA;
            if (callee > 0 && callee < g_prvm.header->num_functions) {
                dfunction_t *cf = &g_prvm.functions[callee];
                if (cf->first_statement <= 0) {
                    PR_ExecuteBuiltin(-cf->first_statement);
                } else {
                    /* Recursive call — push frame and jump */
                    if (g_prvm.depth < MAX_STACK_DEPTH) {
                        prstack_t *fr2 = &g_prvm.stack[g_prvm.depth++];
                        fr2->s = s;
                        fr2->f = cf;
                        s = cf->first_statement - 1;
                        f = cf;
                    }
                }
            }
            break; }

        case OP_RETURN:
            /* Pop call frame */
            if (g_prvm.depth > 0) {
                prstack_t *fr = &g_prvm.stack[--g_prvm.depth];
                s = fr->s;
                f = fr->f;
                if (g_prvm.depth == 0) goto done;
            } else { goto done; }
            break;

        case OP_STATE:   /* animation frame control — no-op for now */ break;
        case OP_DONE:    goto done;

        default: break;
        }

#undef OPA
#undef OPB
#undef OPC
    }
done:
    if (g_prvm.depth > 0) g_prvm.depth--;
}
