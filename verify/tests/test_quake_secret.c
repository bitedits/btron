/*
 * verify/tests/test_quake_secret.c — secret doors, triggers and button use
 *
 * The user's report: "secret rooms aren't opening".  A secret in a Quake map
 * is a brush entity (a door) plus a trigger brush the player walks into, wired
 * by name; the trigger's QuakeC touch fires the door.  Four links have to hold
 * for that to work, and this test measures each one on the real e1m1 data
 * before any code is blamed:
 *
 *   1. every classname in the map's entity lump resolves to a function in
 *      progs.dat (an unresolved name spawns a dead, featureless edict);
 *   2. the spawned door/trigger edicts come out of QC with movetype, solid,
 *      size and callbacks set — and with a sane AABB, since the overlap test
 *      that fires a touch uses origin+mins/maxs;
 *   3. standing the player inside a trigger actually reaches its touch
 *      function, and something moves as a result;
 *   4. a button/lever that needs a `use` dispatch gets one — the server has a
 *      player→entity touch loop, so check whether any use path exists at all.
 *
 * Cleanroom C99 test suite for B-System.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "../../include/btron/event.h"
#include "../../include/btron/wnd.h"
#include "../../src/quake/include/quakedef.h"
#include "../../src/quake/include/world.h"
#include "../../src/quake/include/progs.h"
#include "../../src/quake/include/server.h"
#include "../../src/quake/include/render.h"
#include "../../src/quake/include/mathlib.h"
#include "../../src/quake/include/fs_btron.h"
#include "../../src/quake/include/quake_ui.h"

/* ── Host stubs the linked engine objects reference but this test does not */
refdef_t r_refdef;
int      g_fired_count = 0;
int      g_num_gl_textures = 0;

int in_forward = 0, in_back = 0, in_left = 0, in_right = 0;
int in_down = 0, in_jump = 0, in_attack = 0;
int in_turn_left = 0, in_turn_right = 0;

void uart_puts_raw(const char *s) { (void)s; }
void P_BloodSplash(const float o[3], int c) { (void)o; (void)c; }
void P_ExplosionParticles(const float o[3]) { (void)o; }
void P_RunParticleEffect(const float o[3], const float d[3], int col, int cnt) {
    (void)o; (void)d; (void)col; (void)cnt;
}
void R_AddDynamicLight(const float o[3], float rad, float r, float g, float b, float decay) {
    (void)o; (void)rad; (void)r; (void)g; (void)b; (void)decay;
}
void *R_LoadAliasModel(const char *name) { (void)name; return NULL; }
int   TEX_LoadBSPTextures(const byte *t, int l) { (void)t; (void)l; return 1; }
void  P_UpdateParticles(float dt) { (void)dt; }
void  Draw_Fill(int x, int y, int w, int h, uint32_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
void  Draw_Char(int x, int y, char c) { (void)x; (void)y; (void)c; }
void  Draw_String(int x, int y, const char *s) { (void)x; (void)y; (void)s; }
void  close_quake_window(void) {}

#define MAX_CLASS 64
typedef struct { char name[48]; int count; int resolves; } class_census_t;
static class_census_t g_classes[MAX_CLASS];
static int g_nclasses;

static class_census_t *class_slot(const char *name) {
    for (int i = 0; i < g_nclasses; i++)
        if (strcmp(g_classes[i].name, name) == 0) return &g_classes[i];
    if (g_nclasses >= MAX_CLASS) return NULL;
    class_census_t *c = &g_classes[g_nclasses++];
    strncpy(c->name, name, sizeof(c->name) - 1);
    c->name[sizeof(c->name) - 1] = '\0';
    c->count = 0; c->resolves = -1;
    return c;
}

/* Is `needle` a whole word inside `hay`, ignoring case?  Used only to group the
 * map's own classname strings, never to read game code. */
static int word_has(const char *hay, const char *needle) {
    size_t hl = strlen(hay), nl = strlen(needle);
    for (size_t i = 0; i + nl <= hl; i++) {
        if (q_strcasecmp(hay + i, needle) != 0 && strncasecmp(hay + i, needle, nl) != 0) continue;
        int left  = (i == 0)        || !isalnum((unsigned char)hay[i-1]);
        int right = (i + nl == hl)  || !isalnum((unsigned char)hay[i+nl]);
        if (left && right) return 1;
    }
    return 0;
}

static int is_doorish(const char *cn) {
    return word_has(cn, "door") || word_has(cn, "wall") || word_has(cn, "plat")
        || word_has(cn, "train") || word_has(cn, "button") || word_has(cn, "lever")
        || word_has(cn, "secret");
}
static int is_trigger(const char *cn) { return word_has(cn, "trigger"); }

/* Parse the raw entity lump for "classname" "value" pairs. */
static void census_entity_lump(void) {
    const char *e = g_world.entities;
    if (!e) return;
    const char *p = e;
    while ((p = q_strstr(p, "\"classname\"")) != NULL) {
        p += 11;
        while (*p && *p != '"') p++;
        if (!*p) break;
        p++;
        const char *s = p;
        while (*p && *p != '"') p++;
        char name[48];
        size_t n = (size_t)(p - s);
        if (n >= sizeof(name)) n = sizeof(name) - 1;
        memcpy(name, s, n); name[n] = '\0';
        class_census_t *c = class_slot(name);
        if (c) c->count++;
    }
}

static int find_fn(const char *name) {
    if (!g_prvm.is_loaded || !name) return 0;
    for (int i = 1; i < g_prvm.header->num_functions; i++) {
        const char *fn = PR_GetString(g_prvm.functions[i].s_name);
        if (fn && strcmp(fn, name) == 0) return i;
    }
    return 0;
}

static void aabb_of(const edict_t *ed, float mn[3], float mx[3]) {
    mn[0] = EF(ed, F_ORIGIN_X) + EF(ed, F_MINS_X);
    mn[1] = EF(ed, F_ORIGIN_Y) + EF(ed, F_MINS_Y);
    mn[2] = EF(ed, F_ORIGIN_Z) + EF(ed, F_MINS_Z);
    mx[0] = EF(ed, F_ORIGIN_X) + EF(ed, F_MAXS_X);
    mx[1] = EF(ed, F_ORIGIN_Y) + EF(ed, F_MAXS_Y);
    mx[2] = EF(ed, F_ORIGIN_Z) + EF(ed, F_MAXS_Z);
}

/* ── Statement disassembly, to read where a callback stops ────────────── */
static const char *op_name(int op) {
    static const char *n[] = {
        "DONE",     "MUL_F",   "MUL_V",   "MUL_FV",  "MUL_VF",  "DIV_F",
        "ADD_F",    "ADD_V",   "SUB_F",   "SUB_V",   "EQ_F",    "EQ_V",
        "EQ_S",     "EQ_E",    "EQ_FNC",  "NE_F",    "NE_V",    "NE_S",
        "NE_E",     "NE_FNC",  "LE",      "GE",      "LT",      "GT",
        "LOAD_F",   "LOAD_V",  "LOAD_S",  "LOAD_ENT","LOAD_FLD","LOAD_FNC",
        "ADDRESS",  "STORE_F", "STORE_V", "STORE_S", "STORE_ENT","STORE_FLD",
        "STORE_FNC","STOREP_F","STOREP_V","STOREP_S","STOREP_ENT","STOREP_FLD",
        "STOREP_FNC","RETURN", "NOT_F",   "NOT_V",   "NOT_S",   "NOT_ENT",
        "NOT_FNC",  "IF",      "IFNOT",   "CALL0",   "CALL1",   "CALL2",
        "CALL3",    "CALL4",   "CALL5",   "CALL6",   "CALL7",   "CALL8",
        "STATE",    "GOTO",    "AND",     "OR",      "BITAND",  "BITOR"
    };
    if (op < 0 || op >= (int)(sizeof(n) / sizeof(n[0]))) return "?";
    return n[op];
}

/* First def of this name-space whose ofs matches.  Globals and fields share one
 * number space here, so an operand can legitimately be read either way: both
 * candidate names are printed, and which one the interpreter means is exactly
 * what the disassembly has to settle. */
static const char *def_named(const ddef_t *defs, int n, int ofs) {
    for (int k = 0; k < n; k++) {
        if (defs[k].ofs != (uint16_t)ofs) continue;
        const char *s = PR_GetString(defs[k].s_name);
        if (s && s[0]) return s;
    }
    return NULL;
}

/* An edict field's index, read out of progs.dat rather than from the engine's
 * own F_* table — that table stops at 119 and has no attack_finished, which is
 * precisely the field the door state machines gate on. */
static int field_ofs(const char *name) {
    const int nf = g_prvm.header ? g_prvm.header->num_fielddefs : 0;
    for (int k = 0; k < nf; k++) {
        const char *nm = PR_GetString(g_prvm.fielddefs[k].s_name);
        if (nm && strcmp(nm, name) == 0) return g_prvm.fielddefs[k].ofs;
    }
    return -1;
}

static void operand_label(int v, char *out, size_t n) {
    const int ng = g_prvm.header ? g_prvm.header->num_globaldefs : 0;
    const int nf = g_prvm.header ? g_prvm.header->num_fielddefs : 0;
    const int ngl = g_prvm.header ? g_prvm.header->num_globals : 0;
    const char *g = def_named(g_prvm.globaldefs, ng, v);
    const char *f = def_named(g_prvm.fielddefs, nf, v);
    char val[16] = "";
    if (v >= 0 && v < ngl) snprintf(val, sizeof val, " =%d", ((eval_t *)g_prvm.globals)[v].i);
    if (g && f) snprintf(out, n, "%d:%s|.%s%s", v, g, f, val);
    else if (g) snprintf(out, n, "%d:%s%s", v, g, val);
    else if (f) snprintf(out, n, "%d:.%s%s", v, f, val);
    else snprintf(out, n, "%d%s", v, val);
}

static void print_statement(int s, const char *mark) {
    dstatement_t *st = &g_prvm.statements[s];
    char la[64], lb[64], lc[64];
    operand_label(st->a, la, sizeof la);
    operand_label(st->b, lb, sizeof lb);
    operand_label(st->c, lc, sizeof lc);
    printf("      %s %5d  %-9s a=%-28s b=%-28s c=%s\n",
           mark, s, op_name(st->op), la, lb, lc);
}

/* Function owning a statement: progs.dat lists functions in statement order. */
static int fn_owning_statement(int s) {
    const int n = g_prvm.header ? g_prvm.header->num_functions : 0;
    int best = -1;
    for (int i = 1; i < n; i++)
        if (g_prvm.functions[i].first_statement > 0 &&
            g_prvm.functions[i].first_statement <= s &&
            (best < 0 || g_prvm.functions[i].first_statement >
                         g_prvm.functions[best].first_statement))
            best = i;
    return best;
}

int main(void) {
    if (!FS_Init()) { printf("pak0.pak not found\n"); return 1; }
    UI_Init();
    World_ChangeMap("maps/e1m1.bsp");
    if (!g_prvm.is_loaded) { printf("progs.dat not loaded\n"); return 1; }

    printf("== e1m1: progs.dat has %d functions, map spawned %d edicts, %d submodels\n",
           g_prvm.header->num_functions, g_prvm.num_edicts, g_world.nummodels);

    /* ── 1. Does every classname in the map resolve to a QC function? ──── */
    printf("\n== 1. entity lump classnames vs the progs.dat function table\n");
    census_entity_lump();
    {
        int total_ents = 0, resolved_types = 0, dead_types = 0;
        for (int i = 0; i < g_nclasses; i++) {
            g_classes[i].resolves = find_fn(g_classes[i].name) > 0;
            total_ents += g_classes[i].count;
            if (g_classes[i].resolves) resolved_types++; else dead_types++;
        }
        printf("    %d distinct classnames in %d entities; %d resolve, %d have no function of that name\n",
               g_nclasses, total_ents, resolved_types, dead_types);
        printf("    --- interactive classes (door/wall/plat/train/button/trigger/secret) ---\n");
        int dead_interactive = 0;
        for (int i = 0; i < g_nclasses; i++) {
            if (!is_doorish(g_classes[i].name) && !is_trigger(g_classes[i].name)) continue;
            if (!g_classes[i].resolves) dead_interactive++;
            printf("      %-28s x%-3d  %-8s %s\n", g_classes[i].name, g_classes[i].count,
                   g_classes[i].resolves ? "RESOLVES" : "DEAD",
                   g_classes[i].resolves ? "" : "<- nothing ever spawns its behaviour");
        }
        printf("    interactive classnames with no spawn function = %d\n", dead_interactive);
        printf("    --- functions in progs.dat whose name mentions secret/trigger/door ---\n");
        int shown = 0;
        for (int i = 1; i < g_prvm.header->num_functions && shown < 40; i++) {
            const char *fn = PR_GetString(g_prvm.functions[i].s_name);
            if (!fn) continue;
            if (word_has(fn, "secret") || word_has(fn, "trigger") || word_has(fn, "door")) {
                printf("      #%d %s\n", i, fn);
                shown++;
            }
        }
        if (!shown) printf("      (none)\n");
    }

    /* ── 2. What did QC actually leave on the spawned edicts? ─────────── */
    printf("\n== 2. spawned edicts: movetype/solid/size/callbacks and AABB sanity\n");
    {
        int doors = 0, triggers = 0, dead = 0, bad_aabb = 0, no_size = 0;
        int with_touch = 0, with_use = 0, with_think = 0, submodel = 0;
        for (int i = 2; i < g_prvm.num_edicts; i++) {
            edict_t *ed = &g_prvm.edicts[i];
            const char *cn = PR_GetString(EI(ed, F_CLASSNAME));
            if (!cn) cn = "?";
            int mt = (int)EF(ed, F_MOVETYPE), so = (int)EF(ed, F_SOLID);
            int tf = EI(ed, F_TOUCH), uf = EI(ed, F_USE), th = EI(ed, F_THINK);
            float mi = EF(ed, F_MODELINDEX);
            if (tf > 0) with_touch++;
            if (uf > 0) with_use++;
            if (th > 0) with_think++;
            if (mi >= 1000.0f) submodel++;
            float mn[3], mx[3];
            aabb_of(ed, mn, mx);
            int degenerate = !(mx[0] - mn[0] > 1.0f && mx[1] - mn[1] > 1.0f && mx[2] - mn[2] > 1.0f);
            if (degenerate) no_size++;
            if (is_trigger(cn) || is_doorish(cn)) {
                if (mt == 0 && so == 0 && tf == 0 && uf == 0 && th == 0 && degenerate) dead++;
                if (!degenerate && (mx[0]-mn[0] > 8192.0f || mx[1]-mn[1] > 8192.0f || mx[2]-mn[2] > 8192.0f))
                    bad_aabb++;
                if (is_doorish(cn)) doors++; else triggers++;
                if (doors + triggers <= 14)
                    printf("      edict %-3d %-24s mt=%-2d solid=%-2d touch=%-4d use=%-4d think=%-4d "
                           "model=%-6.0f box=(%.0f %.0f %.0f)..(%.0f %.0f %.0f)\n",
                           i, cn, mt, so, tf, uf, th, mi, mn[0], mn[1], mn[2], mx[0], mx[1], mx[2]);
            }
        }
        printf("    door-like=%d trigger-like=%d  of which completely inert=%d  absurd AABB=%d\n",
               doors, triggers, dead, bad_aabb);
        printf("    callbacks across all edicts: touch=%d use=%d think=%d, brush models=%d, "
               "degenerate boxes=%d\n", with_touch, with_use, with_think, submodel, no_size);
    }

    /* ── 3. Stand the player inside each interactive edict, one at a time ── */
    printf("\n== 3. occupancy: does the touch run, and does the touched edict itself move?\n");
    printf("   Per edict the world is snapshotted first, so a move is attributed to this\n"
           "   trigger and not to one tested earlier.  'asked' is the QC having changed\n"
           "   the edict's own velocity / nextthink / movetype / solid — the touch ran and\n"
           "   requested motion; 'moved' is its origin actually changing.  asked without\n"
           "   moved names the engine link that is missing, nothing asked names a touch\n"
           "   that never reached this edict.\n");
    {
        edict_t *player = &g_prvm.edicts[1];
        float saved[3] = { EF(player, F_ORIGIN_X), EF(player, F_ORIGIN_Y),
                           EF(player, F_ORIGIN_Z) };
        const int ne = g_prvm.num_edicts;
        float *before = malloc(sizeof(float) * 6 * (size_t)ne);

        int tested = 0, asked_only = 0, moved = 0, inert = 0, spoke = 0;
        for (int i = 2; i < ne; i++) {
            edict_t *t = &g_prvm.edicts[i];
            const char *cn = PR_GetString(EI(t, F_CLASSNAME));
            if (!cn) continue;
            if (!is_trigger(cn) && !is_doorish(cn)) continue;
            if (EI(t, F_TOUCH) <= 0) continue;
            float mn[3], mx[3];
            aabb_of(t, mn, mx);
            if (!(mx[0]-mn[0] > 1.0f && mx[1]-mn[1] > 1.0f && mx[2]-mn[2] > 1.0f)) continue;
            tested++;

            for (int e = 0; e < ne; e++) {
                before[e*6+0] = EF(&g_prvm.edicts[e], F_ORIGIN_X);
                before[e*6+1] = EF(&g_prvm.edicts[e], F_ORIGIN_Y);
                before[e*6+2] = EF(&g_prvm.edicts[e], F_ORIGIN_Z);
                before[e*6+3] = EF(&g_prvm.edicts[e], F_VELOCITY_X);
                before[e*6+4] = EF(&g_prvm.edicts[e], F_NEXTTHINK);
                before[e*6+5] = EF(&g_prvm.edicts[e], F_MOVETYPE);
            }

            EF(player, F_ORIGIN_X) = (mn[0]+mx[0]) * 0.5f;
            EF(player, F_ORIGIN_Y) = (mn[1]+mx[1]) * 0.5f;
            EF(player, F_ORIGIN_Z) = (mn[2]+mx[2]) * 0.5f;

            int miss_before = g_pr_unimpl_builtin_count;
            for (int f = 0; f < 30; f++) SV_ServerFrame(1.0f / 60.0f);
            int miss_after = g_pr_unimpl_builtin_count;

            float self_d[3] = {
                EF(t, F_ORIGIN_X) - before[i*6+0],
                EF(t, F_ORIGIN_Y) - before[i*6+1],
                EF(t, F_ORIGIN_Z) - before[i*6+2],
            };
            float self_speed = EF(t, F_VELOCITY_X) + EF(t, F_VELOCITY_Y) + EF(t, F_VELOCITY_Z);
            int self_moved = (self_d[0]*self_d[0] + self_d[1]*self_d[1] +
                              self_d[2]*self_d[2]) > 1.0f;
            int self_asked = self_moved ||
                             fabsf(EF(t, F_VELOCITY_X) - before[i*6+3]) > 0.5f ||
                             fabsf(EF(t, F_NEXTTHINK) - before[i*6+4]) > 0.001f ||
                             (int)EF(t, F_MOVETYPE) != (int)before[i*6+5];

            int others = 0;
            for (int e = 2; e < ne; e++) {
                if (e == i) continue;
                float dx = EF(&g_prvm.edicts[e], F_ORIGIN_X) - before[e*6+0];
                float dy = EF(&g_prvm.edicts[e], F_ORIGIN_Y) - before[e*6+1];
                float dz = EF(&g_prvm.edicts[e], F_ORIGIN_Z) - before[e*6+2];
                if (dx*dx + dy*dy + dz*dz > 1.0f) others++;
            }

            if (self_moved) moved++;
            else if (self_asked) asked_only++;
            else inert++;
            if (miss_after != miss_before) spoke++;

            printf("      edict %-3d %-22s touch=%-4d self: moved=%-3s asked=%-3s "
                   "vel=%6.1f think=%7.2f mt=%d | other movers=%-3d missing builtins=+%d%s\n",
                   i, cn, EI(t, F_TOUCH), self_moved ? "yes" : "no",
                   self_asked ? "yes" : "no", self_speed,
                   EF(t, F_NEXTTHINK), (int)EF(t, F_MOVETYPE), others,
                   miss_after - miss_before,
                   self_moved ? "" : (self_asked ? "  <- touch ran, engine never moved it"
                                                 : "  <- the touch never reached this edict"));

            EF(player, F_ORIGIN_X) = saved[0];
            EF(player, F_ORIGIN_Y) = saved[1];
            EF(player, F_ORIGIN_Z) = saved[2];
        }
        printf("    interactive edicts with a touch and a real box=%d: self moved=%d, "
               "asked for motion but never moved=%d, never asked at all=%d, hit a missing "
               "builtin=%d\n", tested, moved, asked_only, inert, spoke);
        free(before);
    }

    /* ── 4. Which links can ONLY be fired by a use? ───────────────────── */
    printf("\n== 4. button/lever use path\n");
    {
        /* Group every edict that owns a use function by classname, and record
         * the two facts that decide whether the engine can reach it at all:
         * does it also answer to a touch (the path that exists today), and is
         * it solid enough for a view trace to stop on it. */
        typedef struct {
            char name[48];
            int n_use, n_also_touch, n_traceable, n_use_only_untraceable;
            int sample_edict;
        } use_class_t;
        static use_class_t uc[MAX_CLASS];
        int n_uc = 0;

        int users = 0, also_touch = 0, traceable = 0, unreachable = 0;
        for (int i = 2; i < g_prvm.num_edicts; i++) {
            edict_t *ed = &g_prvm.edicts[i];
            if (ed->free) continue;
            int uf = EI(ed, F_USE);
            if (uf <= 0) continue;
            users++;
            const char *cn = PR_GetString(EI(ed, F_CLASSNAME));
            if (!cn || !cn[0]) cn = "(unnamed)";

            int has_touch = EI(ed, F_TOUCH) > 0;
            int so = (int)EF(ed, F_SOLID);
            /* SV_Move only stops on SOLID_BSP and SOLID_BBOX entities. */
            int can_trace = (so == SOLID_BSP || so == SOLID_BBOX);
            if (has_touch) also_touch++;
            if (can_trace) traceable++;
            if (!has_touch && !can_trace) unreachable++;

            use_class_t *slot = NULL;
            for (int k = 0; k < n_uc; k++)
                if (strcmp(uc[k].name, cn) == 0) { slot = &uc[k]; break; }
            if (!slot && n_uc < MAX_CLASS) {
                slot = &uc[n_uc++];
                strncpy(slot->name, cn, sizeof(slot->name) - 1);
                slot->name[sizeof(slot->name) - 1] = '\0';
                slot->n_use = slot->n_also_touch = slot->n_traceable = 0;
                slot->n_use_only_untraceable = 0;
                slot->sample_edict = i;
            }
            if (slot) {
                slot->n_use++;
                if (has_touch) slot->n_also_touch++;
                if (can_trace) slot->n_traceable++;
                if (!has_touch && !can_trace) slot->n_use_only_untraceable++;
            }
        }

        printf("    edicts carrying a use function=%d\n", users);
        printf("      of those, also reachable by walking into a touch=%d\n", also_touch);
        printf("      solid enough for a view trace to hit (BSP/BBOX)=%d\n", traceable);
        printf("      reachable by NOTHING in the engine (no touch, not traceable)=%d\n",
               unreachable);
        printf("    per classname (use / also-touch / traceable):\n");
        for (int k = 0; k < n_uc; k++)
            printf("      %-26s %3d / %3d / %3d   e.g. edict %d\n",
                   uc[k].name, uc[k].n_use, uc[k].n_also_touch, uc[k].n_traceable,
                   uc[k].sample_edict);
    }

    /* ── 5. The QC globals a touch callback compares against ──────────── */
    printf("\n== 5. interpreter globals the game code judges `other` against\n");
    printf("   QuakeC gates a great many touch functions on `other` being the player or\n"
           "   the world.  Those names are interpreter GLOBALS, filled by the engine, not\n"
           "   by the map: if the engine sets `other` but never names the player, every\n"
           "   such test compares a real edict against whatever the global still holds.\n");
    {
        static const char *want[] = { "self", "other", "world", "player",
                                      "goalentity", "movement", "speed" };
        const int nwant = (int)(sizeof(want) / sizeof(want[0]));
        ddef_t *gd = g_prvm.globaldefs;
        const int ng = g_prvm.header ? g_prvm.header->num_globaldefs : 0;
        eval_t *eg = (eval_t *)g_prvm.globals;

        for (int w = 0; w < nwant; w++) {
            int found = 0;
            for (int k = 0; k < ng; k++) {
                const char *nm = PR_GetString(gd[k].s_name);
                if (!nm || strcmp(nm, want[w]) != 0) continue;
                found = 1;
                printf("      global %-11s def#%-4d type=%d ofs=%-4d value: .i=%d .f=%.1f\n",
                       nm, k, (int)(gd[k].type & 0x3FFF), gd[k].ofs,
                       eg[gd[k].ofs].i, eg[gd[k].ofs].f);
            }
            if (!found) printf("      global %-11s NOT PRESENT in progs.dat globaldefs\n", want[w]);
        }

        /* Which type number means "edict"?  Take the type the defs above agree on. */
        int edict_type = -1, n_edict_defs = 0, n_edict_zero = 0;
        for (int k = 0; k < ng; k++) {
            const char *nm = PR_GetString(gd[k].s_name);
            if (!nm) continue;
            if (strcmp(nm, "self") == 0 || strcmp(nm, "other") == 0 ||
                strcmp(nm, "world") == 0 || strcmp(nm, "player") == 0 ||
                strcmp(nm, "goalentity") == 0) {
                edict_type = (int)(gd[k].type & 0x3FFF);
                break;
            }
        }
        if (edict_type >= 0) {
            for (int k = 0; k < ng; k++) {
                if ((int)(gd[k].type & 0x3FFF) != edict_type) continue;
                n_edict_defs++;
                if (eg[gd[k].ofs].i == 0) n_edict_zero++;
            }
        }
        printf("      edict-typed globals=%d, of which still hold edict 0 (the world)=%d\n",
               n_edict_defs, n_edict_zero);
        printf("      the engine's touch dispatch writes ofs 28 (self) and 29 (other);\n"
               "      the player edict is index 1, so any test of `other != player` fails\n"
               "      unless the global named `player` also holds 1.\n");

        /* No `player` global exists in this progs.dat, so the gates in the trigger
         * callbacks must read edict FIELDS instead — QuakeC asks "is this edict a
         * player?" of the entity itself.  Those fields are set by the engine, never
         * by the map, so print what the player edict actually carries. */
        printf("   --- edict fields that identify the player (engine-set) ---\n");
        static const char *wfield[] = { "isplayer", "client", "sounds", "team",
                                        "oldorigin", "maker", "enemy", "aiment" };
        const int nwfield = (int)(sizeof(wfield) / sizeof(wfield[0]));
        ddef_t *fd = g_prvm.fielddefs;
        const int nf = g_prvm.header ? g_prvm.header->num_fielddefs : 0;
        edict_t *pl = &g_prvm.edicts[1];
        for (int w = 0; w < nwfield; w++) {
            int found = 0;
            for (int k = 0; k < nf; k++) {
                const char *nm = PR_GetString(fd[k].s_name);
                if (!nm || strcmp(nm, wfield[w]) != 0) continue;
                found = 1;
                printf("      field  %-11s type=%d ofs=%-4d player holds .f=%.1f .i=%d\n",
                       nm, (int)(fd[k].type & 0x3FFF), fd[k].ofs,
                       pl->v[fd[k].ofs].f, pl->v[fd[k].ofs].i);
            }
            if (!found) printf("      field  %-11s not in progs.dat fielddefs\n", wfield[w]);
        }
        printf("    fielddefs total=%d\n", nf);

        /* Name the callbacks the silent edicts carry: a low index in this progs.dat
         * is a builtin slot, not a QC routine, and that alone explains a touch that
         * "runs" and does nothing. */
        printf("   --- what the touched edicts' callbacks actually are ---\n");
        static const int probe_fn[] = { 66, 384, 398, 405, 406, 413, 414, 425 };
        for (int k = 0; k < (int)(sizeof(probe_fn)/sizeof(probe_fn[0])); k++)
            printf("      function index %-4d = %s\n", probe_fn[k],
                   PR_QCFunctionName(probe_fn[k]));

        printf("   --- every global whose name mentions player/client ---\n");
        int n_named = 0;
        for (int k = 0; k < ng; k++) {
            const char *nm = PR_GetString(gd[k].s_name);
            if (!nm) continue;
            if (!strstr(nm, "player") && !strstr(nm, "client")) continue;
            printf("      global %-20s type=%d ofs=%-4d .i=%d\n",
                   nm, (int)(gd[k].type & 0x3FFF), gd[k].ofs, eg[gd[k].ofs].i);
            n_named++;
        }
        if (!n_named) printf("      (none)\n");
    }

    /* ── 6. Where does an inert callback actually stop? ──────────────────── */
    printf("\n== 6. the callback bodies, read as bytecode\n");
    printf("   Section 3 says 35 edicts never react, but `never reacts` cannot tell a\n"
           "   touch that was never dispatched from one that ran and returned at a gate.\n"
           "   These are the statements the QC runs, with each operand named in both\n"
           "   number spaces the interpreter could mean (a global index, or an edict\n"
           "   field index); `=n` is the global's current integer value.\n");
    {
        static const int body_fn[] = { 384, 398, 406, 414, 425, 66 };
        for (int k = 0; k < (int)(sizeof(body_fn) / sizeof(body_fn[0])); k++) {
            int fi = body_fn[k];
            dfunction_t *f = &g_prvm.functions[fi];
            printf("   --- #%d %s: locals=%d parm_start=%d numparms=%d first_stmt=%d ---\n",
                   fi, PR_QCFunctionName(fi), f->locals, f->parm_start, f->numparms,
                   f->first_statement);
            int shown = 0;
            for (int s = f->first_statement;
                 s < g_prvm.header->num_statements && shown < 80; s++, shown++) {
                print_statement(s, "   ");
                if (g_prvm.statements[s].op == OP_DONE) break;
            }
        }
    }

    printf("\n== 6b. one direct touch call per callback, each on a pristine map\n");
    printf("   The world is respawned before every call so no door is left in a state a\n"
           "   previous probe put it in.  self/other are written exactly as SV_Physics\n"
           "   writes them, then the callback is executed once.  `steps` counts the\n"
           "   statements the interpreter ran: 0 means the call never entered the\n"
           "   function, a small number means it returned at a gate, and `stop` names\n"
           "   that statement.  `time` is the interpreter's own clock global.\n");
    {
        eval_t *eg = (eval_t *)g_prvm.globals;
        const int o_time = PR_GlobalOfs("time");
        const int f_attack = field_ofs("attack_finished");
        const int f_wait   = field_ofs("wait");
        const int f_target = field_ofs("target");
        edict_t *player = &g_prvm.edicts[1];

        printf("      field indices from progs.dat: attack_finished=%d wait=%d target=%d\n",
               f_attack, f_wait, f_target);

        static const int pick_fn[] = { 384, 398, 406, 414, 425 };
        for (int k = 0; k < (int)(sizeof(pick_fn) / sizeof(pick_fn[0])); k++) {
            int want = pick_fn[k];
            for (int nth = 0; nth < 3; nth++) {
                World_ChangeMap("maps/e1m1.bsp");

                int target = -1, seen = 0;
                for (int i = 2; i < g_prvm.num_edicts; i++) {
                    edict_t *c = &g_prvm.edicts[i];
                    if (EI(c, F_TOUCH) != want) continue;
                    float mn[3], mx[3];
                    aabb_of(c, mn, mx);
                    if (!(mx[0] - mn[0] > 1.0f && mx[1] - mn[1] > 1.0f &&
                          mx[2] - mn[2] > 1.0f)) continue;
                    if (seen == nth) { target = i; break; }
                    seen++;
                }
                if (target < 0) {
                    if (!nth)
                        printf("      (no spawned edict carries #%d with a real box)\n", want);
                    break;
                }

                edict_t *t = &g_prvm.edicts[target];
                float mn[3], mx[3];
                aabb_of(t, mn, mx);
                EF(player, F_ORIGIN_X) = (mn[0] + mx[0]) * 0.5f;
                EF(player, F_ORIGIN_Y) = (mn[1] + mx[1]) * 0.5f;
                EF(player, F_ORIGIN_Z) = (mn[2] + mx[2]) * 0.5f;

                int own = EI(t, F_OWNER);
                float o0[3] = { EF(t, F_ORIGIN_X), EF(t, F_ORIGIN_Y), EF(t, F_ORIGIN_Z) };
                float nt0 = EF(t, F_NEXTTHINK);
                float af0 = f_attack >= 0 ? t->v[f_attack].f : 0.0f;
                float oaf0 = (own > 0 && f_attack >= 0) ? g_prvm.edicts[own].v[f_attack].f : 0.0f;

                eg[28].i = target;               /* self  */
                eg[29].i = 1;                    /* other */
                int st0 = g_pr_statements_executed;
                int miss0 = g_pr_unimpl_builtin_count;
                PR_ExecuteProgram(want);
                int steps = g_pr_statements_executed - st0;
                int missd = g_pr_unimpl_builtin_count - miss0;

                const char *cn = PR_GetString(EI(t, F_CLASSNAME));
                printf("      edict %-4d %-20s #%d %-14s steps=%-5d builtins missed=%d\n",
                       target, cn ? cn : "?", want, PR_QCFunctionName(want), steps, missd);
                printf("        clock: QC global %d `time`=%.2f while the server's own clock is %.2f\n",
                       o_time, o_time >= 0 ? eg[o_time].f : -1.0f, g_server.time);
                if (f_attack >= 0)
                    printf("        gates: self.attack_finished %.2f -> %.2f, wait=%.1f, owner=%d its attack_finished %.2f -> %.2f\n",
                           af0, t->v[f_attack].f, f_wait >= 0 ? t->v[f_wait].f : 0.0f,
                           own, oaf0,
                           (own > 0) ? g_prvm.edicts[own].v[f_attack].f : 0.0f);
                if (steps > 0) {
                    int ofn = fn_owning_statement(g_prvm.xstatement);
                    printf("        stop: stmt %d in %s\n", g_prvm.xstatement,
                           ofn > 0 ? PR_QCFunctionName(ofn) : "?");
                    print_statement(g_prvm.xstatement, "        >");
                } else {
                    printf("        stop: the call never entered the function\n");
                }
                printf("        self: moved=%s origin(%.0f %.0f %.0f) nextthink %.2f->%.2f\n",
                       (o0[0] != EF(t, F_ORIGIN_X) || o0[1] != EF(t, F_ORIGIN_Y) ||
                        o0[2] != EF(t, F_ORIGIN_Z)) ? "yes" : "no",
                       o0[0], o0[1], o0[2], nt0, EF(t, F_NEXTTHINK));
            }
        }
    }

    /* ── 6c. Do the engine's field numbers match the ones QC reads? ──────── */
    printf("\n== 6c. the edict field number spaces\n");
    printf("   The engine writes entity fields through its own F_* macros; the game code\n"
           "   reaches them through progs.dat, whose LOAD_*/STORE_* operands name a field\n"
           "   by a global holding that field's index.  Both have to land on the same\n"
           "   slot, or the engine hands the map's data to a field the game code never\n"
           "   reads.  `mirror` is the value progs.dat stores for the field's selector\n"
           "   global, `fielddefs` is the same name in the field table.\n");
    {
        eval_t *eg = (eval_t *)g_prvm.globals;
        struct { const char *name; int engine_ofs; } tbl[] = {
            { "movetype",      F_MOVETYPE      },
            { "solid",         F_SOLID         },
            { "nextthink",     F_NEXTTHINK     },
            { "classname",     F_CLASSNAME     },
            { "touch",         F_TOUCH         },
            { "use",           F_USE           },
            { "think",         F_THINK         },
            { "blocked",       F_BLOCKED       },
            { "target",        F_TARGET        },
            { "targetname",    F_TARGETNAME    },
            { "owner",         F_OWNER         },
            { "message",       F_MESSAGE       },
            { "speed",         F_SPEED         },
            { "items",         F_ITEMS         },
            { "health",        F_HEALTH        },
            { "noise",         F_NOISE         },
            { "sounds",        F_SOUNDS        },
            { "movedir",       F_MOVEDIR_X     },
            { "spawnflags",    F_SPAWNFLAGS    },
            { "enemy",         F_ENEMY         },
            { "flags",         F_FLAGS         },
            { "team",          F_TEAM          },
        };
        const int ntbl = (int)(sizeof(tbl) / sizeof(tbl[0]));
        int mism = 0, unver = 0;
        for (int k = 0; k < ntbl; k++) {
            int fo = field_ofs(tbl[k].name);
            int mo = PR_GlobalOfs(tbl[k].name);
            int mir = mo >= 0 ? eg[mo].i : -1;
            const char *ver = "ok";
            const int has_sel = (mir >= 0 && mir < 256);
            if (fo < 0) { ver = "not in fielddefs"; unver++; }
            else if (fo != tbl[k].engine_ofs) { ver = "ENGINE AND QC DISAGREE"; mism++; }
            else if (has_sel && mir != fo) { ver = "selector disagrees"; mism++; }
            char fbuf[12];
            if (fo >= 0) snprintf(fbuf, sizeof fbuf, "%d", fo);
            else snprintf(fbuf, sizeof fbuf, "-");
            printf("      %-11s engine=%-4d fielddefs=%-4s selector global=%-4d %s\n",
                   tbl[k].name, tbl[k].engine_ofs, fbuf, mir, ver);
        }
        printf("   fields checked=%d mismatches=%d names not in fielddefs=%d\n",
               ntbl, mism, unver);
    }

    /* ── 6d. Does a `use` dispatch open what a touch cannot? ────────────── */
    printf("\n== 6d. call the use callback directly, then let the world run\n");
    printf("   Section 6 showed that touching one of these doors only prints a message:\n"
           "   door_touch returns unless the door carries a key, and secret_touch sets\n"
           "   an attack_finished and stops.  The routine that actually moves a secret\n"
           "   door is hung on the edict's `use` field, which SV_Physics never reads.\n"
           "   Here the same edict is `used` by hand and the world is stepped 120 frames;\n"
           "   if it moves, the sealed secret is a missing use dispatch and nothing else.\n");
    {
        eval_t *eg = (eval_t *)g_prvm.globals;
        static const char *use_names[] = { "fd_secret_use", "door_use", "button_use",
                                           "multi_use" };
        edict_t *player = &g_prvm.edicts[1];

        for (int w = 0; w < (int)(sizeof(use_names)/sizeof(use_names[0])); w++) {
            int ufn = find_fn(use_names[w]);
            if (ufn <= 0) { printf("      %s: not in progs.dat\n", use_names[w]); continue; }
            for (int nth = 0; nth < 2; nth++) {
                World_ChangeMap("maps/e1m1.bsp");
                int target = -1, seen = 0;
                for (int i = 2; i < g_prvm.num_edicts; i++) {
                    if (EI(&g_prvm.edicts[i], F_USE) != ufn) continue;
                    if (seen++ != nth) continue;
                    target = i;
                    break;
                }
                if (target < 0) {
                    if (!nth) printf("      %-14s #%d: no spawned edict carries it\n",
                                     use_names[w], ufn);
                    break;
                }
                edict_t *t = &g_prvm.edicts[target];
                float mn[3], mx[3];
                aabb_of(t, mn, mx);
                EF(player, F_ORIGIN_X) = (mn[0] + mx[0]) * 0.5f;
                EF(player, F_ORIGIN_Y) = (mn[1] + mx[1]) * 0.5f;
                EF(player, F_ORIGIN_Z) = (mn[2] + mx[2]) * 0.5f;

                float o0[3] = { EF(t, F_ORIGIN_X), EF(t, F_ORIGIN_Y), EF(t, F_ORIGIN_Z) };
                float vel0 = EF(t, F_VELOCITY_Z);
                float nt0 = EF(t, F_NEXTTHINK);

                eg[28].i = target;               /* self  */
                eg[29].i = 1;                    /* other = the activator */
                int st0 = g_pr_statements_executed;
                PR_ExecuteProgram(ufn);
                int steps = g_pr_statements_executed - st0;

                int miss0 = g_pr_unimpl_builtin_count;
                for (int f = 0; f < 120; f++) SV_ServerFrame(1.0f / 60.0f);

                const char *cn = PR_GetString(EI(t, F_CLASSNAME));
                const char *tn = PR_GetString(EI(t, F_TARGETNAME));
                printf("      edict %-4d %-16s use=#%d %-14s steps=%-4d targetname=%-14s\n",
                       target, cn ? cn : "?", ufn, use_names[w], steps, tn ? tn : "-");
                printf("        after 120 frames: moved=%s origin(%.0f %.0f %.0f)->(%.0f %.0f %.0f)"
                       " velocity z %.1f->%.1f nextthink %.2f->%.2f builtins missed=%d\n",
                       (o0[0] != EF(t, F_ORIGIN_X) || o0[1] != EF(t, F_ORIGIN_Y) ||
                        o0[2] != EF(t, F_ORIGIN_Z)) ? "YES" : "no",
                       o0[0], o0[1], o0[2],
                       EF(t, F_ORIGIN_X), EF(t, F_ORIGIN_Y), EF(t, F_ORIGIN_Z),
                       vel0, EF(t, F_VELOCITY_Z), nt0, EF(t, F_NEXTTHINK),
                       g_pr_unimpl_builtin_count - miss0);
            }
        }
    }

    printf("\n");
    return 0;
}
