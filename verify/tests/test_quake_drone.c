/*
 * verify/tests/test_quake_drone.c — leaf-graph route planner for drone runs
 *
 * The request: "record drone like footage for each level from start to finish
 * with door openings like a fast run".  Before a .dem file can be written the
 * route has to exist, and the route is pure map data.  This tool measures the
 * only navigation structure the port can build without consulting game sources:
 * a path from the map's start point to its exit that a hull-sized camera can
 * actually travel through, on every BSP rather than by hand.
 *
 * Method (the approach chosen with the user — "leaf-graph path start -> exit"):
 *
 *   1. a handful of interior samples per visleaf.  A leaf's bounds are only its
 *      bounding box, so the box centre of an L-shaped or elongated region lands
 *      in another leaf; the box is bisected until World_PointInLeaf agrees with
 *      the leaf and SV_PointContents is free.  SV_PointContents walks hull 1, the
 *      32x32x56 player clip tree, so "free" already means the camera hull fits,
 *      and the route it produces is one the player could fly too;
 *   2. intra-leaf edges need no trace in principle: a BSP leaf is a convex cell
 *      and shifting its bounding planes inward by the hull support keeps it
 *      convex, so two hull-free samples in one leaf see each other.  The trace is
 *      still run and failures counted, because if that argument does not survive
 *      this clip tree the route would be wrong silently;
 *   3. inter-leaf edges: leaves whose bounds touch are cross-tested sample by
 *      sample with SV_Move.  Touching bounds plus one straight point-to-point line
 *      is not enough — the line between two box-derived points usually clips the
 *      wall beside the opening, which is what the first version of this tool
 *      measured (794 of 1958 touching pairs blocked, one node reachable);
 *   4. Dijkstra from the node nearest the player start to the node nearest the
 *      level exit, weighted by travel distance.
 *
 * Closed doors are deliberately made non-solid for the planning pass: a fast run
 * goes through them and the recorder turns those brushes into `use` events.
 * Which brushes, and when the route reaches them, is the second half of the
 * report.
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

/* ── Planner constants ─────────────────────────────────────────────────── */
#define CAM_SPEED         300.0f  /* units/s of the fast run                   */
#define MAX_SAMPLES       8       /* interior points taken per visleaf         */
#define SAMPLE_MIN_SEP     24.0f  /* two samples closer than this are one spot */
#define SUBDIVISIONS        3     /* box bisection depth                       */
#define MIN_SPAN            32.0f /* stop bisecting below this                 */
#define LEAF_TOUCH_PAD      4.0f  /* slack when testing if two leaves touch    */
#define DOOR_REACH         40.0f  /* a brush this close to the route is passed */
#define MAX_NODES        8192
#define MAX_EDGES      200000
#define MAX_LEAFS_PLANNED 8192
#define MAX_PATH       MAX_NODES
#define MAX_LUMP_ENTS    1024
#define MAX_CLASS          96

typedef struct { int leaf; float p[3]; } pnode_t;
typedef struct { int to, next; float w; } pedge_t;

static pnode_t  g_nodes[MAX_NODES];
static int      g_numnodes;
static pedge_t  g_edges[MAX_EDGES];
static int      g_ehead[MAX_NODES];
static int      g_numedges, g_edge_overflow;

static short    g_leaf_first[MAX_LEAFS_PLANNED];
static short    g_leaf_n[MAX_LEAFS_PLANNED];
static int      g_leaf_sample[MAX_LEAFS_PLANNED][MAX_SAMPLES];
static float    g_leaf_bmin[MAX_LEAFS_PLANNED][3], g_leaf_bmax[MAX_LEAFS_PLANNED][3];
static int      g_visleafs;

static int g_stat_no_point, g_stat_intra, g_stat_intra_fail;
static int g_stat_touch, g_stat_linked, g_stat_blocked, g_stat_probes;

static float ndist(const float *a, const float *b) {
    float d[3] = { a[0]-b[0], a[1]-b[1], a[2]-b[2] };
    return sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
}

static void add_edge(int a, int b, float w) {
    if (g_numedges >= MAX_EDGES) { g_edge_overflow++; return; }
    g_edges[g_numedges].to   = b;
    g_edges[g_numedges].w    = w;
    g_edges[g_numedges].next = g_ehead[a];
    g_ehead[a] = g_numedges++;
}

/* A hull trace that reaches the far end.  SV_Move always sweeps the world's
 * hull 1 clip tree, so this is player-sized rather than a ray.  s_self is passed
 * as passedict: SV_Move's entity loop only skips SOLID_NOT/SOLID_TRIGGER up
 * front, so without it every trace leaving the spawn point starts inside the
 * player's own 32x32x56 box, is reported startsolid, and the start node ends up
 * alone in the graph. */
static edict_t *s_self;

static int trace_clear(const float *from, const float *to) {
    trace_t tr = SV_Move(from, NULL, NULL, to, 0, s_self);
    if (tr.startsolid || tr.allsolid) return 0;
    return tr.fraction >= 0.9999f;
}

static int point_usable(int leaf, const float p[3]) {
    return World_PointInLeaf(p) == leaf && SV_PointContents(p) != CONTENTS_SOLID;
}

/* ── 1. Samples inside one leaf ────────────────────────────────────────── */
static void collect_samples(int leaf, const float bmin[3], const float bmax[3],
                           int depth, int *n) {
    if (*n >= MAX_SAMPLES || depth > SUBDIVISIONS) return;
    float c[3] = { (bmin[0]+bmax[0]) * 0.5f, (bmin[1]+bmax[1]) * 0.5f,
                   (bmin[2]+bmax[2]) * 0.5f };
    if (point_usable(leaf, c)) {
        int dupe = 0;
        for (int k = 0; k < *n; k++)
            if (ndist(c, g_nodes[g_leaf_sample[leaf][k]].p) < SAMPLE_MIN_SEP) dupe = 1;
        if (!dupe && g_numnodes < MAX_NODES) {
            if (*n == 0) g_leaf_first[leaf] = (short)g_numnodes;
            g_leaf_sample[leaf][*n] = g_numnodes;
            g_nodes[g_numnodes].leaf = leaf;
            memcpy(g_nodes[g_numnodes].p, c, sizeof c);
            (*n)++;
            g_leaf_n[leaf] = (short)*n;
            g_numnodes++;
        }
    }
    if (bmax[0]-bmin[0] < MIN_SPAN && bmax[1]-bmin[1] < MIN_SPAN &&
        bmax[2]-bmin[2] < MIN_SPAN) return;
    for (int axis = 0; axis < 3; axis++) {
        float mid = c[axis];
        float a0[3] = { bmin[0], bmin[1], bmin[2] }, a1[3] = { bmax[0], bmax[1], bmax[2] };
        float b0[3] = { bmin[0], bmin[1], bmin[2] }, b1[3] = { bmax[0], bmax[1], bmax[2] };
        a1[axis] = mid;
        b0[axis] = mid;
        collect_samples(leaf, a0, a1, depth + 1, n);
        collect_samples(leaf, b0, b1, depth + 1, n);
    }
}

static void build_nodes(void) {
    g_visleafs = World_VisLeafCount();
    if (g_visleafs > g_world.numleafs) g_visleafs = g_world.numleafs;
    if (g_visleafs > MAX_LEAFS_PLANNED) g_visleafs = MAX_LEAFS_PLANNED;
    for (int L = 0; L < g_visleafs; L++) { g_leaf_first[L] = -1; g_leaf_n[L] = 0; }

    int openleafs = 0;
    for (int L = 1; L < g_visleafs; L++) {
        dleaf_t *lf = &g_world.leafs[L];
        if (lf->contents == CONTENTS_SOLID) continue;
        openleafs++;
        float bmin[3], bmax[3];
        for (int i = 0; i < 3; i++) {
            bmin[i] = lf->mins[i];
            bmax[i] = lf->maxs[i];
            if (bmin[i] < g_world.mins[i]) bmin[i] = g_world.mins[i];
            if (bmax[i] > g_world.maxs[i]) bmax[i] = g_world.maxs[i];
        }
        memcpy(g_leaf_bmin[L], bmin, sizeof bmin);
        memcpy(g_leaf_bmax[L], bmax, sizeof bmax);
        int n = 0;
        collect_samples(L, bmin, bmax, 0, &n);
        if (!n) g_stat_no_point++;
    }
    int usable = openleafs - g_stat_no_point;
    printf("\n== 1. nodes: %d visleafs of %d leafs, %d non-solid, %d with no usable interior point\n",
           g_visleafs, g_world.numleafs, openleafs, g_stat_no_point);
    printf("    %d sample nodes over %d usable leaves (%.1f per leaf)\n",
           g_numnodes, usable, usable ? (float)g_numnodes / usable : 0.0f);
}

/* ── 2. Edges ──────────────────────────────────────────────────────────── */
static int bounds_touch(const float *a_min, const float *a_max,
                        const float *b_min, const float *b_max) {
    for (int i = 0; i < 3; i++) {
        if (a_max[i] + LEAF_TOUCH_PAD < b_min[i]) return 0;
        if (b_max[i] + LEAF_TOUCH_PAD < a_min[i]) return 0;
    }
    return 1;
}

static void build_edges(void) {
    for (int i = 0; i < g_numnodes; i++) g_ehead[i] = -1;

    for (int L = 1; L < g_visleafs; L++) {
        int n = g_leaf_n[L];
        for (int a = 0; a < n; a++)
            for (int b = a + 1; b < n; b++) {
                int na = g_leaf_sample[L][a], nb = g_leaf_sample[L][b];
                g_stat_intra++;
                if (!trace_clear(g_nodes[na].p, g_nodes[nb].p)) { g_stat_intra_fail++; continue; }
                float w = ndist(g_nodes[na].p, g_nodes[nb].p);
                add_edge(na, nb, w);
                add_edge(nb, na, w);
            }
    }

    for (int L1 = 1; L1 < g_visleafs; L1++) {
        if (g_leaf_n[L1] <= 0) continue;
        for (int L2 = L1 + 1; L2 < g_visleafs; L2++) {
            if (g_leaf_n[L2] <= 0) continue;
            if (!bounds_touch(g_leaf_bmin[L1], g_leaf_bmax[L1],
                              g_leaf_bmin[L2], g_leaf_bmax[L2])) continue;
            g_stat_touch++;
            int linked = 0;
            for (int a = 0; a < g_leaf_n[L1] && !linked; a++) {
                int na = g_leaf_sample[L1][a];
                for (int b = 0; b < g_leaf_n[L2]; b++) {
                    int nb = g_leaf_sample[L2][b];
                    g_stat_probes++;
                    if (trace_clear(g_nodes[na].p, g_nodes[nb].p)) {
                        float w = ndist(g_nodes[na].p, g_nodes[nb].p);
                        add_edge(na, nb, w);
                        add_edge(nb, na, w);
                        linked = 1;
                        break;
                    }
                }
            }
            if (linked) g_stat_linked++; else g_stat_blocked++;
        }
    }

    printf("\n== 2. edges: %d nodes, %d directed links\n", g_numnodes, g_numedges);
    printf("    intra-leaf: %d pairs traced, %d failed the convexity argument\n",
           g_stat_intra, g_stat_intra_fail);
    printf("    inter-leaf: %d touching leaf pairs, %d linked, %d blocked (%d traces)\n",
           g_stat_touch, g_stat_linked, g_stat_blocked, g_stat_probes);
    if (g_edge_overflow) printf("    EDGE TABLE FULL: %d links dropped\n", g_edge_overflow);

    int deg0 = 0, maxdeg = 0, sum = 0;
    for (int i = 0; i < g_numnodes; i++) {
        int deg = 0;
        for (int e = g_ehead[i]; e >= 0; e = g_edges[e].next) deg++;
        if (!deg) deg0++;
        if (deg > maxdeg) maxdeg = deg;
        sum += deg;
    }
    printf("    degree: mean %.1f, max %d, isolated nodes %d\n",
           g_numnodes ? (float)sum / g_numnodes : 0.0f, maxdeg, deg0);
}

/* ── 3. Entity lump: classname census and the named points ─────────────── */
typedef struct {
    char  classname[48];
    float origin[3];
    float vert[2][3];   /* brush entities: the two ( x y z ) lines */
    int   nverts;
} lump_ent_t;
static lump_ent_t g_lump_ents[MAX_LUMP_ENTS];
static int        g_num_lump_ents;
static char g_class_names[MAX_CLASS][48];
static int  g_class_counts[MAX_CLASS];
static int  g_nclass;

/* The lump is a stream of { "key" "value" ... } records and values can carry
 * backslash-escaped quotes (worldspawn's "message" does on e1m1), so it has to
 * be tokenised rather than strstr-scanned. */
static const char *lump_token(const char *p, char *out, size_t n, int *brace) {
    *brace = 0; out[0] = '\0';
    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p) return NULL;
    if (*p == '{' || *p == '}') { *brace = (unsigned char)*p; p++; return p; }
    /* Brush entities are defined by ( x y z ) lines rather than an "origin" key,
     * so they have to tokenise too — otherwise the scan stops at the first one. */
    if (*p == '(') {
        p++;
        size_t k = 0;
        while (*p && *p != ')') {
            if (k + 1 < n) out[k++] = *p;
            p++;
        }
        out[k] = '\0';
        if (*p == ')') p++;
        *brace = '(';
        return p;
    }
    if (*p != '"') return NULL;
    p++;
    size_t k = 0;
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) p++;
        if (k + 1 < n) out[k++] = *p;
        p++;
    }
    out[k] = '\0';
    if (*p == '"') p++;
    return p;
}

static void parse_vec3(const char *s, float out[3]) {
    out[0] = out[1] = out[2] = 0.0f;
    int i = 0;
    while (*s && i < 3) {
        while (*s == ' ' || *s == '\t') s++;
        if (!*s) break;
        char *end;
        out[i++] = strtof(s, &end);
        if (end == s) break;
        s = end;
    }
}

static int class_slot(const char *name) {
    for (int i = 0; i < g_nclass; i++)
        if (strcmp(g_class_names[i], name) == 0) return i;
    if (g_nclass >= MAX_CLASS) return -1;
    strncpy(g_class_names[g_nclass], name, 47);
    g_class_names[g_nclass][47] = '\0';
    g_class_counts[g_nclass] = 0;
    return g_nclass++;
}

/* Where a lump record actually is.  Point entities carry "origin"; brush
 * entities carry two ( x y z ) lines, and the map format says the second one is
 * a size offset rather than the opposite corner.  Both readings are returned so
 * the caller can see which one lands in open space instead of trusting either. */
static int ent_points(const lump_ent_t *e, float out[2][3]) {
    int n = 0;
    if (e->origin[0] || e->origin[1] || e->origin[2]) {
        memcpy(out[n++], e->origin, sizeof out[0]);
        if (e->nverts >= 2) {
            for (int i = 0; i < 3; i++) out[n][i] = e->origin[i] + e->vert[1][i] * 0.5f;
            n++;
        }
        return n;
    }
    if (e->nverts >= 1) {
        for (int i = 0; i < 3; i++) out[0][i] = e->vert[0][i] + e->vert[1 % e->nverts][i] * 0.5f;
        n = 1;
    }
    if (e->nverts >= 2) {
        for (int i = 0; i < 3; i++) out[n][i] = (e->vert[0][i] + e->vert[1][i]) * 0.5f;
        n++;
    }
    return n;
}

static int lump_has_brush_verts(const lump_ent_t *e) {
    return e->nverts > 0;
}

static void parse_entity_lump(void) {
    printf("\n== 3. entity lump\n");
    const char *p = g_world.entities;
    if (!p) { printf("    none\n"); return; }
    char key[64], val[160];
    lump_ent_t *cur = NULL;
    int brace_ents = 0, total_kv = 0, total_verts = 0, brush_ents = 0;
    while (p) {
        int b = 0;
        const char *np = lump_token(p, key, sizeof key, &b);
        if (!np) break;
        p = np;
        if (b == '{') {
            brace_ents++;
            cur = (g_num_lump_ents < MAX_LUMP_ENTS) ? &g_lump_ents[g_num_lump_ents] : NULL;
            if (cur) {
                cur->classname[0] = '\0';
                cur->origin[0] = cur->origin[1] = cur->origin[2] = 0.0f;
                cur->nverts = 0;
            }
            continue;
        }
        if (b == '}') {
            if (cur && cur->classname[0]) {
                int s = class_slot(cur->classname);
                if (s >= 0) g_class_counts[s]++;
                if (cur->nverts) brush_ents++;
                g_num_lump_ents++;
            }
            cur = NULL;
            continue;
        }
        if (b == '(') {
            if (cur && cur->nverts < 2) parse_vec3(key, cur->vert[cur->nverts++]);
            total_verts++;
            continue;
        }
        if (b || !key[0]) continue;
        int b2 = 0;
        np = lump_token(p, val, sizeof val, &b2);
        if (!np || b2) continue;
        p = np;
        total_kv++;
        if (!cur) continue;
        if (strcmp(key, "classname") == 0) strncpy(cur->classname, val, 47);
        else if (strcmp(key, "origin") == 0) parse_vec3(val, cur->origin);
    }
    printf("    %d records, %d with a classname, %d key/value pairs, %d distinct classes\n",
           brace_ents, g_num_lump_ents, total_kv, g_nclass);
    printf("    %d ( x y z ) lines over %d brush-defined records\n", total_verts, brush_ents);
    for (int i = 0; i < g_nclass; i++) {
        const char *cn = g_class_names[i];
        if (!strstr(cn, "player") && !strstr(cn, "level") && !strstr(cn, "change") &&
            !strstr(cn, "end") && !strstr(cn, "secret") && !strstr(cn, "door") &&
            !strstr(cn, "button") && !strstr(cn, "plat")) continue;
        int shown = 0;
        for (int e = 0; e < g_num_lump_ents && shown < 6; e++) {
            if (strcmp(g_lump_ents[e].classname, cn) != 0) continue;
            float cand[2][3];
            int n = ent_points(&g_lump_ents[e], cand);
            printf("    %-22s ent %-4d %-6s", cn, e,
                   lump_has_brush_verts(&g_lump_ents[e]) ? "brush" : "point");
            for (int k = 0; k < n; k++)
                printf("  (%.0f %.0f %.0f)", cand[k][0], cand[k][1], cand[k][2]);
            printf("\n");
            shown++;
        }
    }
}

static int nearest_node(const float *p) {
    int best = -1;
    float bd = 1e30f;
    for (int i = 0; i < g_numnodes; i++) {
        float d = ndist(g_nodes[i].p, p);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

/* Where a spawned edict sits.  Point entities carry an origin; brush entities
 * carry a "*N" submodel whose bounds are already absolute when the origin is
 * zero, which is how this port's edict_set_kv leaves them. */
static int edict_center(const edict_t *ed, float out[3]) {
    float org[3] = { EF(ed, F_ORIGIN_X), EF(ed, F_ORIGIN_Y), EF(ed, F_ORIGIN_Z) };
    int mi = (int)EF(ed, F_MODELINDEX);
    int sub = (mi >= 1000) ? (mi - 1000) : 0;
    if (sub > 0 && sub < g_world.nummodels) {
        const dmodel_t *sm = &g_world.models[sub];
        for (int i = 0; i < 3; i++)
            out[i] = org[i] + (sm->mins[i] + sm->maxs[i]) * 0.5f;
        return 1;
    }
    if (org[0] || org[1] || org[2]) {
        for (int i = 0; i < 3; i++) out[i] = org[i];
        return 1;
    }
    return 0;
}

/* Candidate destinations.  A level can mark its finish in several ways and this
 * port can only use the ones it can place in the world, so every marker of every
 * kind is collected and the choice is made after the graph has been measured —
 * deepest reachable wins, which is what a start-to-finish run means when the map
 * does not name its end. */
#define MAX_GOAL_CAND 64
typedef struct { int node; float euclid; const char *kind; } goal_cand_t;
static goal_cand_t g_goals[MAX_GOAL_CAND];
static int         g_ngoals;

static void add_goal(const char *kind, const float *pt, int *placed) {
    int node = nearest_node(pt);
    if (node < 0 || ndist(g_nodes[node].p, pt) > 128.0f) return;
    if (g_ngoals >= MAX_GOAL_CAND) return;
    g_goals[g_ngoals].node  = node;
    g_goals[g_ngoals].kind  = kind;
    g_goals[g_ngoals].euclid = ndist(g_nodes[node].p, g_world.spawn_origin);
    g_ngoals++;
    if (placed) (*placed)++;
}

/* Counts how many markers of each name exist, how many could be placed, and why
 * the rest could not: a trigger_changelevel record in a compiled lump carries
 * neither an "origin" nor a "*N" model, so this engine has no way to know which
 * room it seals. */
static void collect_goals(const char **names, int nnames) {
    for (int k = 0; k < nnames; k++) {
        int edict_seen = 0, placed = 0;
        for (int i = 2; i < g_prvm.num_edicts; i++) {
            edict_t *ed = &g_prvm.edicts[i];
            if (ed->free) continue;
            const char *cn = PR_GetString(EI(ed, F_CLASSNAME));
            if (!cn || strcmp(cn, names[k]) != 0) continue;
            edict_seen++;
            float c[3];
            if (!edict_center(ed, c)) continue;
            add_goal(names[k], c, &placed);
        }
        int lump_seen = 0;
        for (int i = 0; i < g_num_lump_ents; i++) {
            if (strcmp(g_lump_ents[i].classname, names[k]) != 0) continue;
            lump_seen++;
            float cand[2][3];
            int n = ent_points(&g_lump_ents[i], cand);
            for (int c = 0; c < n; c++) add_goal(names[k], cand[c], &placed);
        }
        int in_census = -1;
        for (int c = 0; c < g_nclass; c++)
            if (strcmp(g_class_names[c], names[k]) == 0) { in_census = c; break; }
        printf("    %-22s %d in the lump, %d spawned, %d placeable as a goal\n",
               names[k], in_census >= 0 ? g_class_counts[in_census] : lump_seen,
               edict_seen, placed);
    }
}

/* ── 4. Dijkstra ───────────────────────────────────────────────────────── */
static float s_dist[MAX_NODES];
static int   s_parent[MAX_NODES];
static char  s_done[MAX_NODES];

static void dijkstra(int src) {
    for (int i = 0; i < g_numnodes; i++) { s_dist[i] = 1e30f; s_parent[i] = -1; s_done[i] = 0; }
    s_dist[src] = 0.0f;
    for (;;) {
        int u = -1;
        float best = 1e30f;
        for (int i = 0; i < g_numnodes; i++)
            if (!s_done[i] && s_dist[i] < best) { best = s_dist[i]; u = i; }
        if (u < 0) break;
        s_done[u] = 1;
        for (int e = g_ehead[u]; e >= 0; e = g_edges[e].next) {
            int v = g_edges[e].to;
            float alt = s_dist[u] + g_edges[e].w;
            if (alt < s_dist[v]) { s_dist[v] = alt; s_parent[v] = u; }
        }
    }
}

static void seg_point_dist(const float *a, const float *b, const float *q,
                          float *out_d, float *out_t) {
    float ab[3] = { b[0]-a[0], b[1]-a[1], b[2]-a[2] };
    float len2 = ab[0]*ab[0] + ab[1]*ab[1] + ab[2]*ab[2];
    float t = 0.0f;
    if (len2 > 1e-6f) {
        t = ((q[0]-a[0])*ab[0] + (q[1]-a[1])*ab[1] + (q[2]-a[2])*ab[2]) / len2;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
    }
    float c[3] = { a[0] + ab[0]*t, a[1] + ab[1]*t, a[2] + ab[2]*t };
    *out_d = ndist(c, q);
    *out_t = t;
}

/* ── 5. Recorder: the port-native drone track ──────────────────────────── *
 *
 * The framing is NetQuake's, so Demo_Play's packet index works on these files
 * unchanged:
 *
 *     uint32 msglen | 12-byte marker | payload[msglen]
 *
 * The marker is the pose's viewangles, which is the one thing Demo_Update
 * applies today.  Every payload opens with svc_time (7) and the absolute
 * second, so each packet is indexed at exactly its own time rather than the
 * 1/30 s the indexer guesses for packets without it, and drone records follow:
 *
 *     200  float x,y,z  float pitch,yaw,roll   camera pose, origin IS the eye
 *     201  int32 edict                          fire that entity's use function
 *     255                                        end of track
 *
 * 200/201/255 are above NetQuake's svc_ range, so a stock demo can never be
 * mistaken for one of these; packet 0 keeps svc_serverdata (11) plus the
 * "maps/…" string for the existing mapname scan.
 *
 * Two deliberate choices.  The recorded origin is the planner's sample point
 * itself, not that point plus an eye offset: every sample is free of the
 * 32x32x56 clip hull, so the hull centre is where the camera is known to fit.
 * And no record moves the player edict — the route runs through trigger
 * volumes, and walking the edict through a teleport or changelevel trigger
 * would fire it mid-playback.  Doors open because the track says `use`. */

#define DR_SVC_SERVERDATA   11
#define DR_SVC_TIME          7
#define DR_FRAME           200
#define DR_USE             201
#define DR_END             255
#define DEMO_RATE       30.0f
#define LOOK_AHEAD      0.40f   /* seconds the camera aims ahead along the path */
#define MAX_DEMO_BYTES (1 << 20)
#define DEMO_MAX_PKTS   8192    /* cl_demo.c's MAX_DEMO_PACKETS index limit */
#define MAX_DOOR_EV          96

static byte s_dem[MAX_DEMO_BYTES];
static int  s_dem_ofs, s_dem_overflow;

static void st_f32(byte *d, float f) {
    union { uint32_t u; float f; } cv;
    cv.f = f;
    d[0] = (byte)(cv.u); d[1] = (byte)(cv.u >> 8);
    d[2] = (byte)(cv.u >> 16); d[3] = (byte)(cv.u >> 24);
}

static void st_i32(byte *d, int v) {
    union { uint32_t u; int i; } cv;
    cv.i = v;
    d[0] = (byte)(cv.u); d[1] = (byte)(cv.u >> 8);
    d[2] = (byte)(cv.u >> 16); d[3] = (byte)(cv.u >> 24);
}

static float ld_f32(const byte *d) {
    union { uint32_t u; float f; } cv;
    cv.u = (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
    return cv.f;
}

static int ld_i32(const byte *d) {
    union { uint32_t u; int i; } cv;
    cv.u = (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
    return cv.i;
}

/* One packet: 4-byte length, the 12-byte angle marker the player already reads,
 * then the payload. */
static void dem_pkt(const byte *payload, int plen, const float ang[3]) {
    if (s_dem_ofs + 16 + plen > MAX_DEMO_BYTES) { s_dem_overflow = 1; return; }
    st_i32(s_dem + s_dem_ofs, plen); s_dem_ofs += 4;
    st_f32(s_dem + s_dem_ofs, ang[0]); st_f32(s_dem + s_dem_ofs + 4, ang[1]);
    st_f32(s_dem + s_dem_ofs + 8, ang[2]);
    s_dem_ofs += 12;
    memcpy(s_dem + s_dem_ofs, payload, plen);
    s_dem_ofs += plen;
}

static float s_cum[MAX_PATH];

/* Point at arc length d along the waypoint polyline, clamped at both ends. */
static void path_at(const float *const *pts, int n, float d, float out[3]) {
    if (d <= 0.0f) { out[0] = pts[0][0]; out[1] = pts[0][1]; out[2] = pts[0][2]; return; }
    if (d >= s_cum[n - 1]) {
        const float *p = pts[n - 1], *q = n > 1 ? pts[n - 2] : p;
        out[0] = p[0]; out[1] = p[1]; out[2] = p[2];
        /* The last stretch's direction, extended past the final node. */
        if (n > 1) {
            float k = (d - s_cum[n - 1]) / (s_cum[n - 1] - s_cum[n - 2] + 1e-6f);
            out[0] += k * (p[0] - q[0]); out[1] += k * (p[1] - q[1]); out[2] += k * (p[2] - q[2]);
        }
        return;
    }
    for (int i = 0; i + 1 < n; i++) {
        if (d > s_cum[i + 1]) continue;
        float seg = s_cum[i + 1] - s_cum[i];
        float t = (seg > 1e-6f) ? (d - s_cum[i]) / seg : 0.0f;
        for (int c = 0; c < 3; c++)
            out[c] = pts[i][c] + t * (pts[i + 1][c] - pts[i][c]);
        return;
    }
}

/* maps/e1m1.bsp -> assets/quake/drone1m1.dem: the name the request used, and
 * one rule that keeps working for every episode. */
static void dem_outpath(const char *mappath, char *out, int n) {
    const char *ov = getenv("DRONE_OUT");
    if (ov && *ov) { snprintf(out, n, "%s", ov); return; }
    const char *base = strrchr(mappath, '/');
    base = base ? base + 1 : mappath;
    char stem[64];
    snprintf(stem, sizeof(stem), "%s", base);
    char *dot = strchr(stem, '.');
    if (dot) *dot = 0;
    if (stem[0] == 'e' && stem[1]) memmove(stem, stem + 1, strlen(stem));
    snprintf(out, n, "assets/quake/drone%s.dem", stem);
}

/* Write the track: one packet per DEMO_RATE frame, plus the use events that
 * come due in it.  Returns the number of pose packets, or -1 on failure. */
static int record_dem(const char *mappath, const int *path, int npath,
                      const int *ev_edict, const float *ev_t, int n_ev,
                      char *out_path, int outsz, float *out_dur, int *out_bytes) {
    dem_outpath(mappath, out_path, outsz);

    static const float *pts[MAX_PATH];
    s_cum[0] = 0.0f;
    for (int i = 0; i < npath; i++) {
        pts[i] = g_nodes[path[i]].p;
        if (i) s_cum[i] = s_cum[i - 1] + ndist(pts[i - 1], pts[i]);
    }
    float total = s_cum[npath - 1];
    float dur = total / CAM_SPEED;
    int frames = (int)(dur * DEMO_RATE) + 1;
    if (frames > MAX_PATH) frames = MAX_PATH;

    s_dem_ofs = 0;
    s_dem_overflow = 0;

    /* Demo_Play skips everything up to the first newline, so the file has to
     * start with a text line; it also reads an integer off it as the CD track,
     * so the line must not begin with a digit. */
    char hdr[128];
    int hlen = snprintf(hdr, sizeof(hdr), "BTRON-DRONE 1 %s %.2f %d frames %d doors\n",
                        mappath, dur, frames, n_ev);
    memcpy(s_dem, hdr, hlen);
    s_dem_ofs = hlen;

    byte p[64];
    int n = 0;
    p[n++] = DR_SVC_SERVERDATA;
    memcpy(p + n, mappath, strlen(mappath) + 1);
    n += (int)strlen(mappath) + 1;
    dem_pkt(p, n, (const float[3]){ 0.0f, 0.0f, 0.0f });

    float ang[3] = { 0.0f, 0.0f, 0.0f };
    float last_yaw = 0.0f;
    int have_yaw = 0;
    int due = 0, poses = 0;

    for (int k = 0; k < frames; k++) {
        float t = (float)k / DEMO_RATE;
        float pos[3], aim[3];
        path_at(pts, npath, CAM_SPEED * t, pos);
        path_at(pts, npath, CAM_SPEED * (t + LOOK_AHEAD), aim);

        float dir[3] = { aim[0] - pos[0], aim[1] - pos[1], aim[2] - pos[2] };
        float dl = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        if (dl > 1e-3f) {
            float z = dir[2] / dl;
            if (z > 1.0f) z = 1.0f;
            if (z < -1.0f) z = -1.0f;
            /* AngleVectors has forward[2] = -sin(pitch), so an upward dir is a
             * negative pitch. */
            ang[0] = -asinf(z) * (180.0f / (float)M_PI);
            float yaw = atan2f(dir[1], dir[0]) * (180.0f / (float)M_PI);
            /* Keep yaw continuous across the +-180 seam: a later interpolating
             * player must not see a 358 degree snap on a straight turn. */
            if (have_yaw) {
                while (yaw - last_yaw > 180.0f)  yaw -= 360.0f;
                while (yaw - last_yaw < -180.0f) yaw += 360.0f;
            }
            have_yaw = 1;
            last_yaw = yaw;
            ang[1] = yaw;
            ang[2] = 0.0f;
        }

        n = 0;
        p[n++] = DR_SVC_TIME;
        st_f32(p + n, t); n += 4;
        p[n++] = DR_FRAME;
        for (int c = 0; c < 3; c++) { st_f32(p + n, pos[c]); n += 4; }
        for (int c = 0; c < 3; c++) { st_f32(p + n, ang[c]); n += 4; }
        poses++;

        while (due < n_ev && ev_t[due] <= t + 1.0f / DEMO_RATE) {
            p[n++] = DR_USE;
            st_i32(p + n, ev_edict[due]); n += 4;
            due++;
        }

        dem_pkt(p, n, ang);
    }

    while (due < n_ev) {   /* an event the route reached after the last frame */
        n = 0;
        p[n++] = DR_SVC_TIME;
        st_f32(p + n, dur); n += 4;
        p[n++] = DR_USE;
        st_i32(p + n, ev_edict[due]); n += 4;
        dem_pkt(p, n, ang);
        due++;
    }

    n = 0;
    p[n++] = DR_END;
    st_f32(p + n, dur); n += 4;
    dem_pkt(p, n, ang);

    *out_dur = dur;
    *out_bytes = s_dem_ofs;
    if (s_dem_overflow) return -1;

    FILE *f = fopen(out_path, "wb");
    if (!f) return -1;
    size_t wr = fwrite(s_dem, 1, (size_t)s_dem_ofs, f);
    fclose(f);
    if ((int)wr != s_dem_ofs) return -1;
    return poses;
}

/* Read the file back the way Demo_Play does — same skip-to-newline, same
 * length/marker framing, same svc_time indexing — and check that every pose it
 * would put the camera in is free of the world hull. */
static int verify_dem(const char *out_path, int want_poses, int want_uses) {
    int len = 0;
    byte *d = (byte *)FS_LoadFile(out_path, &len);
    if (!d) { printf("    FAIL: %s is not readable through FS_LoadFile\n", out_path); return 0; }

    int ofs = 0;
    while (ofs < len && d[ofs] != '\n') ofs++;
    if (ofs < len && d[ofs] == '\n') ofs++;

    int pkts = 0, poses = 0, uses = 0, bad_time = 0, in_solid = 0, overflow = 0;
    float first_t = -1.0f, last_t = -1.0f, timed = 0.0f, end_dur = -1.0f;
    float first_pose[3] = { 0, 0, 0 }, last_pose[3] = { 0, 0, 0 };

    while (ofs + 16 <= len) {
        int msglen = ld_i32(d + ofs);
        if (msglen < 0 || ofs + 16 + msglen > len) break;
        const byte *pl = d + ofs + 16;
        float t;
        if (msglen >= 5 && pl[0] == DR_SVC_TIME) {
            t = ld_f32(pl + 1);
            if (first_t < 0.0f) first_t = t;
            last_t = t;
        } else {
            timed += 1.0f / DEMO_RATE;
            t = timed;
            if (pl[0] != DR_SVC_SERVERDATA && pl[0] != DR_END) bad_time++;
        }

        int body = 0;
        if (msglen >= 1 && pl[0] == DR_SVC_SERVERDATA) body = msglen;  /* header, no pose records */
        else if (msglen >= 5 && pl[0] == DR_SVC_TIME) body = 5;
        while (body + 1 <= msglen) {
            byte rec = pl[body];
            if (rec == DR_FRAME) {
                if (body + 25 > msglen) { overflow++; break; }
                float p[3] = { ld_f32(pl + body + 1), ld_f32(pl + body + 5), ld_f32(pl + body + 9) };
                if (poses == 0) { first_pose[0] = p[0]; first_pose[1] = p[1]; first_pose[2] = p[2]; }
                last_pose[0] = p[0]; last_pose[1] = p[1]; last_pose[2] = p[2];
                if (SV_PointContents(p) == CONTENTS_SOLID) in_solid++;
                poses++;
                body += 25;
            } else if (rec == DR_USE) {
                if (body + 5 > msglen) { overflow++; break; }
                uses++;
                body += 5;
            } else if (rec == DR_END) {
                if (body + 5 <= msglen) end_dur = ld_f32(pl + body + 1);
                body = msglen;
            } else {
                overflow++;
                body = msglen;
            }
        }

        ofs += 16 + msglen;
        pkts++;
        if (pkts >= DEMO_MAX_PKTS) { printf("    FAIL: more packets than the player indexes\n"); break; }
    }

    printf("    read back: %d packets in %d bytes, %.2f s of track, %d poses, %d use events\n",
           pkts, len, end_dur >= 0.0f ? end_dur : last_t - (first_t < 0 ? 0 : first_t),
           poses, uses);
    printf("    first pose (%.0f %.0f %.0f)  last pose (%.0f %.0f %.0f)\n",
           first_pose[0], first_pose[1], first_pose[2],
           last_pose[0], last_pose[1], last_pose[2]);
    printf("    poses inside world geometry: %d, unframed records: %d, packets off svc_time: %d\n",
           in_solid, overflow, bad_time);

    int ok = (poses == want_poses) && (uses == want_uses) && !in_solid && !overflow &&
             !bad_time && end_dur > 0.0f;
    printf("    %s\n", ok ? "PASS: the track re-reads through the player's own framing"
                          : "FAIL: the written track does not re-read cleanly");
    return ok;
}

int main(void) {
    if (!FS_Init()) { printf("pak0.pak not found\n"); return 1; }
    UI_Init();
    const char *map = getenv("DRONE_MAP");
    if (!map || !*map) map = "maps/e1m1.bsp";
    World_ChangeMap(map);
    if (!g_world.is_loaded) {
        /* 77 so `make test-drone-all` reports a skip: the pak0.pak in the tree is
         * the shareware one, which carries episode 1 only. */
        printf("SKIP: %s is not in pak0.pak\n", map);
        return 77;
    }
    s_self = (g_prvm.num_edicts > 1) ? &g_prvm.edicts[1] : NULL;

    printf("=== drone route planner: %s ===\n", map);
    printf("map bounds (%.0f %.0f %.0f) .. (%.0f %.0f %.0f), spawn (%.0f %.0f %.0f)\n",
           g_world.mins[0], g_world.mins[1], g_world.mins[2],
           g_world.maxs[0], g_world.maxs[1], g_world.maxs[2],
           g_world.spawn_origin[0], g_world.spawn_origin[1], g_world.spawn_origin[2]);
    printf("%d edicts spawned, %d models, %d clipnodes\n",
           g_prvm.num_edicts, g_world.nummodels, g_world.numclipnodes);

    parse_entity_lump();
    build_nodes();
    if (!g_numnodes) { printf("no nodes — nothing to plan\n"); return 1; }

    /* Route planning is a question about the map's *geometry*.  Monsters, crates
     * and doors are edicts that happen to be in the way at spawn time, and SV_Move
     * sweeps every solid edict, so with them active the graph is cut to pieces —
     * the measured symptom was one reachable node out of 3458.  Take all of them
     * out for the planning pass; the brush subset is put back afterwards so the
     * report can say which legs a closed door actually blocks. */
    static int   s_ent_idx[MAX_EDICTS];
    static float s_ent_solid[MAX_EDICTS];
    static int   s_bsp_idx[MAX_EDICTS];
    int n_clear = 0, n_bsp = 0;
    for (int i = 2; i < g_prvm.num_edicts && n_clear < MAX_EDICTS; i++) {
        edict_t *ed = &g_prvm.edicts[i];
        if (ed->free) continue;
        float so = EF(ed, F_SOLID);
        if ((int)so == SOLID_NOT) continue;
        s_ent_idx[n_clear]    = i;
        s_ent_solid[n_clear]  = so;
        n_clear++;
        if ((int)so == SOLID_BSP && n_bsp < MAX_EDICTS) s_bsp_idx[n_bsp++] = i;
        EF(ed, F_SOLID) = SOLID_NOT;
    }
    printf("\n    planning with %d solid edicts ignored (%d of them SOLID_BSP brushes)\n",
           n_clear, n_bsp);

    build_edges();

    /* Brushes back, everything else still out: a leg that fails now is blocked by
     * a door, not by a monster standing in the hall. */
    for (int k = 0; k < n_bsp; k++)
        EF(&g_prvm.edicts[s_bsp_idx[k]], F_SOLID) = SOLID_BSP;

    int start = nearest_node(g_world.spawn_origin);

    printf("\n== 4. endpoints\n");
    printf("    start  node %d (leaf %d) at (%.0f %.0f %.0f)\n", start,
           start >= 0 ? g_nodes[start].leaf : -1,
           start >= 0 ? g_nodes[start].p[0] : 0,
           start >= 0 ? g_nodes[start].p[1] : 0,
           start >= 0 ? g_nodes[start].p[2] : 0);
    const char *goal_names[] = { "info_player_end", "trigger_changelevel", "info_player_deathmatch" };
    collect_goals(goal_names, (int)(sizeof goal_names / sizeof goal_names[0]));
    printf("    %d candidate goal nodes\n", g_ngoals);

    dijkstra(start);

    int reachable = 0;
    for (int i = 0; i < g_numnodes; i++) if (s_dist[i] < 1e29f) reachable++;
    printf("    reachable nodes from start: %d of %d (%.0f%%)\n",
           reachable, g_numnodes, 100.0f * reachable / g_numnodes);

    /* Deepest reachable marker: for a linear Quake level the marker furthest along
     * the graph is the one the run is meant to finish at, and it keeps working on
     * maps that mark their end several times. */
    int goal = -1;
    const char *goalkind = "none";
    for (int i = 0; i < g_ngoals; i++) {
        int n = g_goals[i].node;
        if (s_dist[n] >= 1e29f) continue;
        if (goal < 0 || s_dist[n] > s_dist[goal]) { goal = n; goalkind = g_goals[i].kind; }
        printf("    candidate %-22s node %4d (leaf %4d) euclid %6.0f  path %8.0f%s\n",
               g_goals[i].kind, n, g_nodes[n].leaf, g_goals[i].euclid, s_dist[n],
               s_dist[n] >= 1e29f ? "  UNREACHABLE" : "");
    }
    if (goal < 0) {
        float bd = -1;
        for (int i = 0; i < g_numnodes; i++)
            if (s_dist[i] < 1e29f && s_dist[i] > bd) { bd = s_dist[i]; goal = i; }
        goalkind = "farthest reachable node";
        if (goal < 0) { printf("    nothing reachable — aborting\n"); return 1; }
        printf("    no marker reachable — using %s node %d, %.0f units of path\n",
               goalkind, goal, bd);
    }
    printf("    goal   node %d (leaf %d, from %s) at (%.0f %.0f %.0f)\n", goal,
           g_nodes[goal].leaf, goalkind,
           g_nodes[goal].p[0], g_nodes[goal].p[1], g_nodes[goal].p[2]);

    static int path[MAX_PATH];
    int npath = 0;
    for (int v = goal; v >= 0 && npath < MAX_PATH; v = s_parent[v]) path[npath++] = v;
    for (int i = 0; i < npath / 2; i++) {
        int t = path[i]; path[i] = path[npath-1-i]; path[npath-1-i] = t;
    }

    printf("\n== 5. route (%s)\n", goalkind);
    printf("    %d waypoints, %.0f units, %.1f s at %.0f u/s\n",
           npath, s_dist[goal], s_dist[goal] / CAM_SPEED, CAM_SPEED);
    float dzmin = 1e30f, dzmax = -1e30f;
    for (int i = 0; i < npath; i++) {
        float z = g_nodes[path[i]].p[2];
        if (z < dzmin) dzmin = z;
        if (z > dzmax) dzmax = z;
    }
    printf("    z range %.0f .. %.0f\n", dzmin, dzmax);

    /* Re-check every leg against the restored brushes: a leg that only worked
     * because a door was ignored is exactly the leg that needs a use event, so
     * this count and the census below have to agree.  Monsters stay non-solid for
     * the duration so that only geometry is being measured. */
    int legs_blocked = 0, printed = 0;
    printf("    legs blocked by a closed brush:");
    for (int w = 0; w + 1 < npath; w++) {
        if (trace_clear(g_nodes[path[w]].p, g_nodes[path[w+1]].p)) continue;
        legs_blocked++;
        if (printed < 15) { printf(" %d", w); printed++; }
    }
    printf(" = %d of %d\n", legs_blocked, npath - 1);

    for (int k = 0; k < n_clear; k++)
        EF(&g_prvm.edicts[s_ent_idx[k]], F_SOLID) = s_ent_solid[k];

    printf("\n== 6. brushes within %.0f units of the route (these become the run's door openings)\n",
           DOOR_REACH);
    static int   s_ev_edict[MAX_DOOR_EV];
    static float s_ev_time[MAX_DOOR_EV];
    static int   s_n_ev;
    int shown = 0, within = 0, with_use = 0;
    for (int i = 2; i < g_prvm.num_edicts; i++) {
        edict_t *ed = &g_prvm.edicts[i];
        if (ed->free || (int)EF(ed, F_SOLID) != SOLID_BSP) continue;
        int mi  = (int)EF(ed, F_MODELINDEX);
        int sub = (mi >= 1000) ? (mi - 1000) : 0;
        if (sub <= 0 || sub >= g_world.nummodels) continue;
        const dmodel_t *sm = &g_world.models[sub];
        float org[3] = { EF(ed, F_ORIGIN_X), EF(ed, F_ORIGIN_Y), EF(ed, F_ORIGIN_Z) };
        float c[3] = {
            org[0] + (sm->mins[0] + sm->maxs[0]) * 0.5f,
            org[1] + (sm->mins[1] + sm->maxs[1]) * 0.5f,
            org[2] + (sm->mins[2] + sm->maxs[2]) * 0.5f,
        };
        float best = 1e30f, best_t = 0;
        int best_leg = -1;
        for (int w = 0; w + 1 < npath; w++) {
            float d, t;
            seg_point_dist(g_nodes[path[w]].p, g_nodes[path[w+1]].p, c, &d, &t);
            if (d < best) { best = d; best_leg = w; best_t = t; }
        }
        if (best_leg < 0 || best > DOOR_REACH) continue;
        within++;
        int has_use = EI(ed, F_USE) > 0;
        if (has_use) with_use++;

        /* When the run reaches this brush, in seconds: the recorder's use time. */
        float run = 0.0f;
        for (int w = 0; w < best_leg; w++)
            run += ndist(g_nodes[path[w]].p, g_nodes[path[w+1]].p);
        run += best_t * ndist(g_nodes[path[best_leg]].p, g_nodes[path[best_leg+1]].p);
        run /= CAM_SPEED;
        if (has_use && s_n_ev < MAX_DOOR_EV) {
            s_ev_edict[s_n_ev] = i;
            s_ev_time[s_n_ev]  = run;
            s_n_ev++;
        }

        if (shown < 40) {
            const char *cn = PR_GetString(EI(ed, F_CLASSNAME));
            /* Probe straight through the brush along the direction of travel: that
             * says whether the drone's line of flight actually crosses the closed
             * leaf, which is what decides if this needs a use event at all. */
            const float *pa = g_nodes[path[best_leg]].p, *pb = g_nodes[path[best_leg+1]].p;
            float d[3] = { pb[0]-pa[0], pb[1]-pa[1], pb[2]-pa[2] };
            float dl = ndist(pa, pb);
            float probe_a[3], probe_b[3];
            if (dl > 1.0f) {
                for (int i = 0; i < 3; i++) { d[i] /= dl; probe_a[i] = c[i] - 64.0f*d[i]; probe_b[i] = c[i] + 64.0f*d[i]; }
            } else {
                for (int i = 0; i < 3; i++) { probe_a[i] = c[i]; probe_b[i] = c[i]; probe_b[2] += 64.0f; }
            }
            trace_t pr = SV_Move(probe_a, NULL, NULL, probe_b, 0, s_self);
            int hit = pr.ent ? (int)(pr.ent - g_prvm.edicts) : 0;
            /* Inconclusive by construction, and that has been measured: the
             * segment is +-64 along the direction of travel from the submodel's
             * bounds centre, so a frac of 1.00 says only that the probe missed the
             * plate — a *31 model's box is not the plate.  Brush entities do
             * collide: e1m2 edict 291 gives frac=0.00 hit=291 on this same line,
             * and e1m1 edict 143 gives 0.50.  Nothing is concluded from it. */
            printf("    edict %-4d %-18s dist %4.0f leg %3d t=%6.2fs use=%-4d "
                   "plate probe frac=%.2f hit=%-3d model=%d hn0=%d hn1=%d\n",
                   i, cn ? cn : "?", best, best_leg, run,
                   EI(ed, F_USE), pr.fraction, hit, sub,
                   sm->headnode[0], sm->headnode[1]);
            shown++;
        }
    }
    printf("    brushes within %.0f units of the route: %d, with a use function: %d\n",
           DOOR_REACH, within, with_use);

    /* The frames are emitted in time order, so the event list has to be too. */
    for (int a = 1; a < s_n_ev; a++) {
        int   e = s_ev_edict[a];
        float t = s_ev_time[a];
        int b = a;
        while (b > 0 && s_ev_time[b - 1] > t) {
            s_ev_edict[b] = s_ev_edict[b - 1];
            s_ev_time[b]  = s_ev_time[b - 1];
            b--;
        }
        s_ev_edict[b] = e; s_ev_time[b] = t;
    }

    printf("\n== 7. record\n");
    char out_path[256];
    float dur = 0.0f;
    int bytes = 0;
    int nposes = record_dem(map, path, npath, s_ev_edict, s_ev_time, s_n_ev,
                            out_path, (int)sizeof(out_path), &dur, &bytes);
    if (nposes < 0) {
        printf("    FAIL: could not write %s\n", out_path);
        return 1;
    }
    printf("    %s: %d bytes, %.2f s at %.0f fps, %d doors to open\n",
           out_path, bytes, dur, DEMO_RATE, s_n_ev);
    for (int a = 0; a < s_n_ev && a < 12; a++) {
        const char *cn = PR_GetString(EI(&g_prvm.edicts[s_ev_edict[a]], F_CLASSNAME));
        printf("    t=%6.2fs  use edict %-4d %s\n", s_ev_time[a], s_ev_edict[a],
               cn ? cn : "?");
    }
    if (s_n_ev > 12) printf("    ... %d more\n", s_n_ev - 12);

    printf("\n== 8. read it back\n");
    if (!verify_dem(out_path, nposes, s_n_ev)) return 1;

    printf("\n== 9. waypoints\n");
    for (int i = 0; i < npath; i++) {
        if (npath > 24 && i >= 12 && i < npath - 6) {
            if (i == 12) printf("    ... %d more ...\n", npath - 18);
            continue;
        }
        printf("    %3d  leaf %-5d (%7.0f %7.0f %7.0f)  d=%7.0f  t=%6.2fs\n",
               i, g_nodes[path[i]].leaf,
               g_nodes[path[i]].p[0], g_nodes[path[i]].p[1], g_nodes[path[i]].p[2],
               s_dist[path[i]], s_dist[path[i]] / CAM_SPEED);
    }
    return 0;
}
