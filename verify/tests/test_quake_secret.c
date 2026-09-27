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

    /* ── 3. Stand the player in every trigger and see what moves ─────── */
    printf("\n== 3. trigger occupancy: does a touch run, and does anything then move?\n");
    {
        edict_t *player = &g_prvm.edicts[1];
        /* snapshot every entity origin so a move is unambiguous */
        float *before = malloc(sizeof(float) * 3 * (size_t)g_prvm.num_edicts);
        for (int i = 0; i < g_prvm.num_edicts; i++) {
            before[i*3+0] = EF(&g_prvm.edicts[i], F_ORIGIN_X);
            before[i*3+1] = EF(&g_prvm.edicts[i], F_ORIGIN_Y);
            before[i*3+2] = EF(&g_prvm.edicts[i], F_ORIGIN_Z);
        }
        int tested = 0, moved = 0, spoke = 0;
        for (int i = 2; i < g_prvm.num_edicts; i++) {
            edict_t *t = &g_prvm.edicts[i];
            const char *cn = PR_GetString(EI(t, F_CLASSNAME));
            if (!cn) continue;
            if (EI(t, F_TOUCH) <= 0) continue;
            float mn[3], mx[3];
            aabb_of(t, mn, mx);
            if (!(mx[0]-mn[0] > 1.0f && mx[1]-mn[1] > 1.0f && mx[2]-mn[2] > 1.0f)) continue;
            tested++;
            if (tested > 24) continue;

            EF(player, F_ORIGIN_X) = (mn[0]+mx[0]) * 0.5f;
            EF(player, F_ORIGIN_Y) = (mn[1]+mx[1]) * 0.5f;
            EF(player, F_ORIGIN_Z) = (mn[2]+mx[2]) * 0.5f;

            int miss_before = g_pr_unimpl_builtin_count;

            for (int f = 0; f < 30; f++) SV_ServerFrame(1.0f / 60.0f);

            int n_moved = 0;
            for (int e2 = 2; e2 < g_prvm.num_edicts; e2++) {
                float dx = EF(&g_prvm.edicts[e2], F_ORIGIN_X) - before[e2*3+0];
                float dy = EF(&g_prvm.edicts[e2], F_ORIGIN_Y) - before[e2*3+1];
                float dz = EF(&g_prvm.edicts[e2], F_ORIGIN_Z) - before[e2*3+2];
                if (dx*dx + dy*dy + dz*dz > 1.0f) n_moved++;
                before[e2*3+0] = EF(&g_prvm.edicts[e2], F_ORIGIN_X);
                before[e2*3+1] = EF(&g_prvm.edicts[e2], F_ORIGIN_Y);
                before[e2*3+2] = EF(&g_prvm.edicts[e2], F_ORIGIN_Z);
            }
            int miss_after = g_pr_unimpl_builtin_count;
            if (n_moved) moved++;
            if (miss_after != miss_before) spoke++;
            printf("      trigger edict %-3d %-22s at (%.0f %.0f %.0f): entities moved=%d, "
                   "unimplemented builtins hit by its QC=+%d\n",
                   i, cn, mn[0], mn[1], mn[2], n_moved, miss_after - miss_before);
        }
        printf("    triggers with a touch and a usable box=%d  produced motion=%d  "
               "hit a missing builtin=%d\n", tested, moved, spoke);
        printf("    (motion=0 everywhere means the touch chain never reaches a door; "
               "a nonzero missing-builtin delta names the link that breaks)\n");
        free(before);
    }

    /* ── 4. Is there any use dispatch at all? ─────────────────────────── */
    printf("\n== 4. button/lever use path\n");
    {
        int users = 0;
        for (int i = 2; i < g_prvm.num_edicts; i++)
            if (EI(&g_prvm.edicts[i], F_USE) > 0) users++;
        printf("    edicts carrying a use function=%d\n", users);
        printf("    SV_Physics() dispatches F_TOUCH(player,entity) only, so the %d edicts\n"
               "    above can never be fired from this engine: nothing in src/quake/ ever\n"
               "    reads F_USE. A secret wired button -> door is inert at that link.\n", users);
    }

    printf("\n");
    return 0;
}
