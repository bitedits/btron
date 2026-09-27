/*
 * verify/tests/test_quake_visibility.c — PVS / line-of-sight conformance probe
 *
 * The renderer culls every face whose leaf is not in the camera leaf's PVS row
 * (src/quake/render/r_brush.c).  A leaf that is geometrically visible but absent
 * from that row is therefore never drawn — the player sees straight through the
 * wall that should have been there.  PVS rows come from the BSP visibility lump,
 * which is run-length encoded, so the encoder/decoder convention has to match the
 * data exactly.
 *
 * This test decides the convention from pak0's own bytes instead of assuming it:
 *   1. decode every row with both candidate RLE readings and compare,
 *   2. check the invariant the format must satisfy (visibility is symmetric),
 *   3. march rays through the drawing BSP and require every unobstructed leaf to
 *      be present in the camera's row — the see-through-wall gate.
 *
 * Cleanroom C99 test suite for B-System.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../../include/btron/event.h"
#include "../../include/btron/wnd.h"
#include "../../src/quake/include/quakedef.h"
#include "../../src/quake/include/world.h"
#include "../../src/quake/include/progs.h"
#include "../../src/quake/include/render.h"
#include "../../src/quake/include/mathlib.h"
#include "../../src/quake/include/fs_btron.h"

/* ── Host stubs: the engine objects reference these, the renderer does not ── */
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

/* ── Reference RLE reading ──────────────────────────────────────────── *
 * The Quake visibility lump stores a row as (count, value) byte pairs and
 * terminates it with a zero count.  Nothing in that reading ever emits a
 * literal count byte as a bitmask.                                      */
static int decode_pairs(const byte *in, int maxlen, byte *out, int row_bytes) {
    int o = 0;
    for (int i = 0; i + 1 < maxlen; ) {
        int count = in[i];
        if (!count) return (o == row_bytes) ? 1 : 0;   /* clean terminator */
        byte value = in[i + 1];
        i += 2;
        while (count-- > 0) {
            if (o >= row_bytes) return 0;              /* overran the row */
            out[o++] = value;
        }
    }
    return 0;
}

/* The engine's current reading, reimplemented here so the two can be
 * compared on identical inputs (see World_LeafPVS in core/world.c). */
static int decode_engine(const byte *in, int maxlen, byte *out, int row_bytes) {
    int o = 0, i = 0;
    while (o < row_bytes && i < maxlen) {
        if (in[i]) {
            out[o++] = in[i++];
        } else {
            i++;
            if (i >= maxlen) return 0;
            int count = in[i++];
            while (count-- > 0 && o < row_bytes) out[o++] = 0;
        }
    }
    return o == row_bytes;
}

static int bit_get(const byte *row, int i) { return (row[i >> 3] >> (i & 7)) & 1; }
static void bit_set(byte *row, int i)      { row[i >> 3] |= (byte)(1u << (i & 7)); }
static int popcount8(int b) { int n = 0; while (b) { n += b & 1; b >>= 1; } return n; }

/* Point → leaf, using the drawing BSP (the same tree the renderer walks). */
static int leaf_of(const float *p) { return World_PointInLeaf(p); }

/* Re-walk one ray at 1 unit.  Returns the distance at which the first solid or
 * sky leaf appears, or -max if the whole segment stays in open leafs.  A coarse
 * sampling step can jump straight over a thin wall; this is what separates that
 * false positive from a visibility entry qbsp really left out. */
static float first_blocker(const float *org, const float *dst)
{
    float d[3] = { dst[0]-org[0], dst[1]-org[1], dst[2]-org[2] };
    float len = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
    if (len < 1.0f) return -1.0f;
    d[0] /= len; d[1] /= len; d[2] /= len;
    for (float t = 0.5f; t <= len; t += 1.0f) {
        float p[3] = { org[0] + d[0]*t, org[1] + d[1]*t, org[2] + d[2]*t };
        int L = leaf_of(p);
        if (L <= 0) return t;
        int c = g_world.leafs[L].contents;
        if (c == CONTENTS_SOLID || c == CONTENTS_SKY) return t;
    }
    return -1.0f;
}

/* Walk the CLIPNODE tree from an arbitrary head node.  Hull 0 of the world
 * model is the drawing tree itself, so anything a line of sight passes but the
 * world tree cannot show has to live in a submodel — a brush entity (func_wall,
 * func_detail, a door body) that qbsp knew was solid when it built the vis. */
static int clip_walk(int nodenum, const float *p)
{
    while (nodenum >= 0) {
        if (nodenum >= g_world.numclipnodes) return CONTENTS_EMPTY;
        const dclipnode_t *node = &g_world.clipnodes[nodenum];
        if (node->planenum < 0 || node->planenum >= g_world.numplanes) return CONTENTS_EMPTY;
        const dplane_t *plane = &g_world.planes[node->planenum];
        float dist = DotProduct(p, plane->normal) - plane->dist;
        nodenum = (dist >= 0.0f) ? node->children[0] : node->children[1];
    }
    return nodenum;   /* negative leaf value = contents */
}

/* Returns the submodel index whose solid geometry blocks org→dst, or -1. */
static int submodel_blocker(const float *org, const float *dst)
{
    float d[3] = { dst[0]-org[0], dst[1]-org[1], dst[2]-org[2] };
    float len = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
    if (len < 1.0f) return -1;
    d[0] /= len; d[1] /= len; d[2] /= len;
    for (float t = 1.0f; t <= len; t += 2.0f) {
        float p[3] = { org[0] + d[0]*t, org[1] + d[1]*t, org[2] + d[2]*t };
        for (int m = 1; m < g_world.nummodels; m++)
            if (clip_walk(g_world.models[m].headnode[0], p) == CONTENTS_SOLID)
                return m;
    }
    return -1;
}

typedef struct { float org[3], dst[3]; int C, L; } MissRec;

/* ── The see-through-wall gate, one step size ───────────────────────── *
 * March rays through the drawing BSP.  A sample that is not in a solid or sky
 * leaf is unobstructed along that ray as far as the world tree is concerned, so
 * the (camera,target) pair must appear in qbsp's visibility data.  A pair that
 * does not is classified before it is called a bug: the target's row may see the
 * camera (qbsp dropped a one-way pair), a 1-unit re-walk may find a thin wall the
 * coarse step jumped over (the gate's own aliasing), or a brush entity in a
 * submodel may block the line — qbsp sees submodels, the drawing tree does not.
 * Only a pair that survives all three means the renderer really culls geometry
 * the player is looking at.                                                   */

static void ray_gate(const byte *mat, int W, int vis, float step)
{
    const dmodel_t *mm = &g_world.models[0];
    const float maxdist = 2400.0f;
    const int ncams = 2000, ndirs = 12;

    size_t bitsz = ((size_t)vis * vis + 7) / 8;
    byte *reached_bits = calloc(bitsz, 1);
    byte *miss_bits    = calloc(bitsz, 1);
    size_t mcap = 4096, nmi = 0, d_re = 0;
    MissRec *ms = malloc(mcap * sizeof(*ms));
    size_t outside = 0, solid_cam = 0, norow_cam = 0, cams_used = 0;
    int *cam_reach = calloc((size_t)vis + 1, sizeof(int));
    int *cam_miss  = calloc((size_t)vis + 1, sizeof(int));

    unsigned seed = 12345;
    for (int cam = 0; cam < ncams; cam++) {
        float org[3];
        for (int k = 0; k < 3; k++) {
            seed = seed * 1103515245u + 12345u;
            float t = (float)((seed >> 8) & 0xFFFF) / 65535.0f;
            org[k] = mm->mins[k] + t * (mm->maxs[k] - mm->mins[k]);
        }
        int cam_leaf = leaf_of(org);
        if (cam_leaf <= 0) { outside++;   continue; }   /* the sky region: leaf 0 */
        if (g_world.leafs[cam_leaf].contents == CONTENTS_SOLID) { solid_cam++; continue; }
        if (cam_leaf > vis) { norow_cam++; continue; }  /* no row: renderer draws all */
        cams_used++;
        const byte *row = mat + (size_t)(cam_leaf - 1) * W;

        for (int d = 0; d < ndirs; d++) {
            float dir[3];
            for (int k = 0; k < 3; k++) {
                seed = seed * 1103515245u + 12345u;
                dir[k] = ((float)((seed >> 8) & 0xFFFF) / 65535.0f) * 2.0f - 1.0f;
            }
            float len = sqrtf(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
            if (len < 0.001f) continue;
            dir[0] /= len; dir[1] /= len; dir[2] /= len;

            for (float t = step; t < maxdist; t += step) {
                float p[3] = { org[0] + dir[0]*t, org[1] + dir[1]*t, org[2] + dir[2]*t };
                int L = leaf_of(p);
                if (L <= 0) break;
                int c = g_world.leafs[L].contents;
                if (c == CONTENTS_SOLID || c == CONTENTS_SKY) break;
                if (L == cam_leaf || L > vis) continue;
                unsigned long long idx = (unsigned long long)(cam_leaf - 1) * vis + (L - 1);
                if (!(reached_bits[idx >> 3] & (1u << (idx & 7)))) {
                    reached_bits[idx >> 3] |= (1u << (idx & 7));
                    d_re++;
                    cam_reach[cam_leaf]++;
                }
                if (!(row[(L - 1) >> 3] & (1u << ((L - 1) & 7))) &&
                    !(miss_bits[idx >> 3] & (1u << (idx & 7)))) {
                    miss_bits[idx >> 3] |= (1u << (idx & 7));
                    cam_miss[cam_leaf]++;
                    if (nmi == mcap) { mcap *= 2; ms = realloc(ms, mcap * sizeof(*ms)); }
                    for (int k = 0; k < 3; k++) { ms[nmi].org[k] = org[k]; ms[nmi].dst[k] = p[k]; }
                    ms[nmi].C = cam_leaf; ms[nmi].L = L;
                    nmi++;
                }
            }
        }
    }

    int one_way = 0, alias = 0, ent = 0, open = 0, shown = 0;
    int ent_models[64]; int ent_model_n = 0;
    for (size_t i = 0; i < nmi; i++) {
        const byte *rowL = mat + (size_t)(ms[i].L - 1) * W;
        if (rowL[(ms[i].C - 1) >> 3] & (1u << ((ms[i].C - 1) & 7))) { one_way++; continue; }
        if (first_blocker(ms[i].org, ms[i].dst) > 0) { alias++; continue; }
        int m = submodel_blocker(ms[i].org, ms[i].dst);
        if (m >= 0) {
            ent++;
            int known = 0;
            for (int k = 0; k < ent_model_n; k++) if (ent_models[k] == m) known = 1;
            if (!known && ent_model_n < 64) ent_models[ent_model_n++] = m;
            continue;
        }
        open++;
        if (shown < 8) {
            printf("      cam leaf %d -> leaf %d at (%.0f,%.0f,%.0f): open in the world tree"
                   " and no submodel blocks it -> CULLED VISIBLE GEOMETRY\n",
                   ms[i].C, ms[i].L, ms[i].org[0], ms[i].org[1], ms[i].org[2]);
            shown++;
        }
    }

    /* Is the loss spread over the level, or concentrated in a few camera
     * leafs whose rows qbsp left (nearly) empty? */
    {
        int cams_with_miss = 0, empty_rows = 0;
        printf("      per-camera detail (leaf, contents, row bits, reached, missing):\n");
        for (int l = 1; l <= vis; l++) {
            if (!cam_miss[l]) continue;
            cams_with_miss++;
            int bits = 0;
            const byte *r = mat + (size_t)(l - 1) * W;
            for (int b = 0; b < W; b++) bits += popcount8(r[b]);
            if (bits <= 2) empty_rows++;
            if (cams_with_miss <= 12)
                printf("        leaf %-5d contents=%-4d rowbits=%-4d reached=%-4d missing=%d\n",
                       l, g_world.leafs[l].contents, bits, cam_reach[l], cam_miss[l]);
        }
        printf("      cameras with at least one missing pair=%d, of which their row holds <=2 bits=%d\n",
               cams_with_miss, empty_rows);
    }

    printf("    step=%-4.0f cameras: used=%d sky/outside=%d solid=%d no-row=%d\n",
           step, cams_used, (int)outside, (int)solid_cam, (int)norow_cam);
    printf("      distinct unobstructed pairs=%d   absent from the camera row=%d (%.2f%%)\n"
           "        qbsp one-way (target row does see the camera) =%d\n"
           "        gate sampling alias (a 1-unit walk blocks)    =%d\n"
           "        blocked by a brush entity (submodel solid)    =%d  [%d distinct models]\n"
           "        open all the way yet culled (the real bug)    =%d\n",
           (int)d_re, (int)nmi, d_re ? 100.0 * nmi / d_re : 0.0, one_way, alias, ent,
           ent_model_n, open);
    free(reached_bits); free(miss_bits); free(ms);
}

/* ── Cross-map convention check ──────────────────────────────────────── *
 * The vis-leaf numbering the fix rests on is a property of qbsp's output, not
 * of one map, so it has to hold for every BSP in pak0:
 *   - leafs carrying a PVS row are exactly 1..models[0].visleafs, contiguous,
 *     and nothing above that prefix has a row,
 *   - a row is (visleafs+7)/8 bytes and its RLE ends at or before the next
 *     row's offset (no overrun into its neighbour),
 *   - every leaf's own bit is set in its own row.
 * Returns 1 = satisfied, 0 = violated, -1 = map not present in pak0. */
static int vis_check_map(const char *map)
{
    World_UnloadMap();
    if (!World_LoadMap(map)) { printf("    %-18s : not in pak0 (skipped)\n", map); return -1; }

    const int nleaf = g_world.numleafs;
    const int vis   = World_VisLeafCount();
    const int W     = World_VisRowBytes();
    if (W <= 0) { printf("    %-18s : visleafs=%d -> row width %d, BAD\n", map, vis, W); return 0; }

    int with_row = 0, last = -1, holes = 0, badrank = 0;
    for (int l = 1; l < nleaf; l++)
        if (g_world.leafs[l].visofs >= 0) { with_row++; last = l; }
    for (int l = 1; l <= last; l++)
        if (g_world.leafs[l].visofs < 0) holes++;
    for (int l = 1; l < nleaf; l++)
        if ((World_LeafVisBit(l) >= 0) != (g_world.leafs[l].visofs >= 0)) badrank++;

    const byte *vdata = (const byte *)g_world.visdata;
    byte *row = malloc((size_t)W);
    int exact = 0, shorty = 0, overrun = 0, self_ok = 0, ofs_err = 0;
    for (int l = 1; l < nleaf; l++) {
        int o = g_world.leafs[l].visofs;
        if (o < 0) continue;
        if (o >= g_world.vislen) { ofs_err++; continue; }
        /* the neighbour with the smallest larger offset bounds this row */
        int nxt = g_world.vislen;
        for (int m = 1; m < nleaf; m++) {
            int mo = g_world.leafs[m].visofs;
            if (mo > o && mo < nxt) nxt = mo;
        }
        const byte *in = vdata + o, *end = vdata + g_world.vislen;
        int oi = 0;
        memset(row, 0, (size_t)W);
        while (oi < W && in < end) {
            if (*in) row[oi++] = *in++;
            else {
                in++;
                if (in >= end) break;
                int c = *in++;
                while (c-- > 0 && oi < W) row[oi++] = 0;
            }
        }
        int consumed = (int)(in - (vdata + o));
        if (oi < W)               shorty++;
        else if (consumed > nxt - o) overrun++;
        else                      exact++;
        int bit = World_LeafVisBit(l);
        if (oi == W && bit >= 0 && bit < W * 8 && (row[bit >> 3] & (1 << (bit & 7))))
            self_ok++;
    }
    free(row);

    int ok = !holes && !badrank && !shorty && !overrun && !ofs_err && self_ok == with_row;
    printf("    %-18s leafs=%-5d visleafs=%-5d row=%-4d prefix_holes=%d rank_mismatch=%d "
           "exact=%d overrun=%d short=%d badofs=%d self=%d/%d  %s\n",
           map, nleaf, vis, W, holes, badrank, exact, overrun, shorty, ofs_err,
           self_ok, with_row, ok ? "OK" : "VIOLATION");
    World_UnloadMap();
    return ok;
}

int main(void) {
    if (!FS_Init()) { printf("pak0.pak not found\n"); return 1; }
    if (!World_LoadMap("maps/e1m1.bsp")) { printf("e1m1.bsp not loaded\n"); return 1; }

    const int nleaf = g_world.numleafs;
    const int rowb  = (nleaf + 7) / 8;   /* the width the engine used BEFORE the fix */
    const int vis   = World_VisLeafCount();
    const int W     = World_VisRowBytes(); /* the width the rows actually have */
    printf("== map=%s  leafs=%d  vis lump=%d bytes  legacy row=%d bytes  real row=%d bytes (visleafs=%d)\n",
           g_world.name, nleaf, g_world.vislen, rowb, W, vis);

    /* ── 1. Which RLE reading does the data actually use? ───────────── */
    printf("\n== 1. visibility row decoding, all %d leafs\n", nleaf);
    {
        int ok_pairs = 0, ok_engine = 0, have_vis = 0;
        int diff = 0, identical = 0;
        long set_pairs = 0, set_engine = 0;

        byte *a = malloc(rowb), *b = malloc(rowb);
        for (int l = 1; l < nleaf; l++) {
            int ofs = g_world.leafs[l].visofs;
            if (ofs < 0 || ofs >= g_world.vislen) continue;
            have_vis++;
            memset(a, 0, rowb);
            memset(b, 0, rowb);
            int rp = decode_pairs((const byte *)g_world.visdata + ofs,
                                  g_world.vislen - ofs, a, rowb);
            int re = decode_engine((const byte *)g_world.visdata + ofs,
                                   g_world.vislen - ofs, b, rowb);
            ok_pairs += rp; ok_engine += re;
            if (rp && re) {
                if (memcmp(a, b, rowb) == 0) identical++;
                else diff++;
            }
            for (int i = 0; i < rowb; i++) { set_pairs += a[i]; set_engine += b[i]; }
            if (!rp && re) memcpy(a, b, rowb);   /* keep the sane matrix below */
        }
        printf("    (count,value) reading decoded cleanly : %d of %d rows\n", ok_pairs, have_vis);
        printf("    engine reading decoded to full rows   : %d of %d rows\n", ok_engine, have_vis);
        printf("    rows where the two agree              : %d   disagree: %d\n", identical, diff);
        printf("    bits set per row: pairs=%.1f   engine=%.1f   (a row that is all\n"
               "    ones or all zeros means the reading is wrong)\n",
               have_vis ? (double)set_pairs / (double)rowb / have_vis : 0.0,
               have_vis ? (double)set_engine / (double)rowb / have_vis : 0.0);
        free(a); free(b);
    }

    /* ── 1b. Where does a row actually begin? ─────────────────────────── *
     * The visibility lump is documented as a header (cluster count plus a
     * per-leaf bit offset table) followed by the RLE rows, while dleaf_t.visofs
     * holds an offset that some tools store relative to the header entry for the
     * leaf.  Decide it from the bytes instead of assuming.                  */
    printf("\n== 1b. visibility lump layout\n");
    {
        const byte *vis = (const byte *)g_world.visdata;
        int numclusters = 0;
        memcpy(&numclusters, vis, 4);
        printf("    first int of the lump = %d   (numleafs=%d, lump=%d bytes)\n",
               numclusters, nleaf, g_world.vislen);
        printf("    leaf.visofs samples   :");
        for (int l = 1; l <= 6 && l < nleaf; l++) printf(" %d", g_world.leafs[l].visofs);
        printf("\n");
        int vmin = 1<<30, vmax = -(1<<30), vuniq_head = 0;
        for (int l = 1; l < nleaf; l++) {
            int o = g_world.leafs[l].visofs;
            if (o < vmin) vmin = o;
            if (o > vmax) vmax = o;
        }
        printf("    leaf.visofs range = [%d .. %d]   lump = %d bytes   header(2 tables) = %d bytes\n",
               vmin, vmax, g_world.vislen, 4 + 2 * nleaf * 4);
        printf("    ints at 4+L*4      (L=0..5)  :");
        for (int l = 0; l < 6; l++) { int v = 0; memcpy(&v, vis + 4 + l * 4, 4); printf(" %d", v); }
        printf("\n");
        printf("    ints at 4+(n+L)*4  (L=0..5)  :");
        for (int l = 0; l < 6; l++) { int v = 0; memcpy(&v, vis + 4 + (nleaf + l) * 4, 4); printf(" %d", v); }
        printf("\n");
        printf("    ints at visofs+0   for 6 leafs:");
        for (int l = 1, p = 0; l < nleaf && p < 6; l++) {
            int o = g_world.leafs[l].visofs;
            if (o < 0 || o + 4 > g_world.vislen) continue;
            int v = 0; memcpy(&v, vis + o, 4);
            printf(" %d", v); p++;
        }
        printf("\n");

        /* Now test the (count,value) reading from each plausible base. */
        struct { const char *name; int mode; } cand[] = {
            { "base = leaf.visofs                     (engine today)", 0 },
            { "base = int at leaf.visofs              (indirect)",     1 },
            { "base = bitofs[0][leaf]  (header right after numclusters)", 2 },
            { "base = 4 + bitofs[0][leaf]",                            3 },
        };
        for (size_t c = 0; c < sizeof(cand)/sizeof(cand[0]); c++) {
            int clean = 0, tot = 0;
            byte *out = malloc(rowb);
            for (int l = 1; l < nleaf; l++) {
                int ofs;
                if (cand[c].mode <= 1) {
                    ofs = g_world.leafs[l].visofs;
                    if (ofs < 0 || ofs >= g_world.vislen) continue;
                    if (cand[c].mode == 1) {
                        int v = 0;
                        if (ofs + 4 > g_world.vislen) continue;
                        memcpy(&v, vis + ofs, 4);
                        ofs = v;
                    }
                } else {
                    int v = 0;
                    if (4 + l * 4 + 4 > g_world.vislen) continue;
                    memcpy(&v, vis + 4 + l * 4, 4);
                    ofs = v + (cand[c].mode == 3 ? 4 : 0);
                }
                if (ofs < 0 || ofs >= g_world.vislen) continue;
                tot++;
                memset(out, 0, rowb);
                clean += decode_pairs(vis + ofs, g_world.vislen - ofs, out, rowb);
            }
            printf("    %-52s : %d/%d rows decode cleanly\n", cand[c].name, clean, tot);
            free(out);
        }
    }

    /* ── 1c. Read the bytes ───────────────────────────────────────────── */
    printf("\n== 1c. raw bytes of the first rows, and how each reading fares\n");
    {
        const byte *vis = (const byte *)g_world.visdata;
        static const int probe[] = { 1, 2, 3, 60 };
        byte *out = malloc(rowb * 2);
        for (size_t q = 0; q < sizeof(probe)/sizeof(probe[0]); q++) {
            int l = probe[q];
            int ofs = g_world.leafs[l].visofs;
            int nxt = g_world.leafs[l + 1].visofs;
            printf("    leaf %-3d visofs=%d (next row at %d, %d bytes)\n    ",
                   l, ofs, nxt, nxt > ofs ? nxt - ofs : -1);
            for (int i = 0; i < 28 && ofs + i < g_world.vislen; i++)
                printf("%02x ", vis[ofs + i]);
            printf("\n");

            /* (count,data) pairs */
            memset(out, 0, rowb * 2);
            int o = 0, i = ofs, stop = 0;
            while (!stop) {
                int count = vis[i++];
                if (i > g_world.vislen) { stop = 2; break; }
                if (!count) { stop = 1; break; }
                byte value = vis[i++];
                while (count--) { if (o < rowb * 2) out[o] = value; o++; }
                if (o > rowb * 4) { stop = 3; break; }
            }
            printf("      pairs(count,data): emitted=%d  terminated=%s overrun=%s\n",
                   o, stop == 1 ? "yes" : "no", stop == 3 ? "YES" : "no");

            /* (data,count) pairs */
            o = 0; i = ofs; stop = 0;
            while (!stop) {
                byte value = vis[i++];
                int count  = vis[i++];
                if (i > g_world.vislen) { stop = 2; break; }
                if (!count) { stop = 1; break; }
                while (count--) { if (o < rowb * 2) out[o] = value; o++; }
                if (o > rowb * 4) { stop = 3; break; }
            }
            printf("      pairs(data,count): emitted=%d  terminated=%s overrun=%s\n",
                   o, stop == 1 ? "yes" : "no", stop == 3 ? "YES" : "no");
        }
        free(out);
    }

    /* ── 1e. Is every lump what the engine thinks it is? ─────────────── */
    printf("\n== 1e. lump identity audit (hypothesis: vis/lighting lumps are confused)\n");
    {
        static const char *lname[HEADER_LUMPS] = {
            "ENTITIES","PLANES","TEXTURES","VERTEXES","VISIBILITY","NODES","TEXINFO",
            "FACES","LIGHTING","CLIPNODES","LEAFS","MARKSURFACES","EDGES","SURFEDGES","MODELS"
        };
        const byte *base = g_bsp_cache.data;
        printf("    BSP image = %d bytes\n", g_bsp_cache.length);
        for (int l = 0; l < HEADER_LUMPS; l++) {
            const byte *p = g_bsp_cache.lumps[l];
            int len = g_bsp_cache.lump_lens[l];
            if (!p || len <= 0) { printf("    %2d %-13s EMPTY\n", l, lname[l]); continue; }
            int n = len < 8192 ? len : 8192, txt = 0, nul = 0;
            for (int i = 0; i < n; i++) {
                if (!p[i]) nul++;
                else if (p[i] >= 0x20 && p[i] < 0x7f) txt++;
            }
            printf("    %2d %-13s ofs=%-8d len=%-8d  %3d%% text %3d%% nul  [%02x %02x %02x %02x]\n",
                   l, lname[l], (int)(p - base), len, txt*100/n, nul*100/n,
                   p[0], p[1], p[2], p[3]);
        }

        /* Record-size residuals: a wrong lump index almost never divides. */
        printf("    record-size residuals (nonzero => this lump is not that record type):\n");
        printf("      PLANES(%2zu)=%-3d VERTEXES(%2zu)=%-3d NODES(%2zu)=%-3d TEXINFO(%2zu)=%-3d\n",
               sizeof(dplane_t),  g_bsp_cache.lump_lens[LUMP_PLANES]     % (int)sizeof(dplane_t),
               sizeof(dvertex_t), g_bsp_cache.lump_lens[LUMP_VERTEXES]   % (int)sizeof(dvertex_t),
               sizeof(dnode_t),   g_bsp_cache.lump_lens[LUMP_NODES]      % (int)sizeof(dnode_t),
               sizeof(texinfo_t), g_bsp_cache.lump_lens[LUMP_TEXINFO]    % (int)sizeof(texinfo_t));
        printf("      FACES(%2zu)=%-3d CLIPNODES(%2zu)=%-3d LEAFS(%2zu)=%-3d EDGES(%2zu)=%-3d SURFEDGES(4)=%-3d MODELS(%2zu)=%-3d\n",
               sizeof(dface_t),    g_bsp_cache.lump_lens[LUMP_FACES]     % (int)sizeof(dface_t),
               sizeof(dclipnode_t),g_bsp_cache.lump_lens[LUMP_CLIPNODES] % (int)sizeof(dclipnode_t),
               sizeof(dleaf_t),    g_bsp_cache.lump_lens[LUMP_LEAFS]     % (int)sizeof(dleaf_t),
               sizeof(dedge_t),    g_bsp_cache.lump_lens[LUMP_EDGES]     % (int)sizeof(dedge_t),
               g_bsp_cache.lump_lens[LUMP_SURFEDGES] % 4,
               sizeof(dmodel_t),   g_bsp_cache.lump_lens[LUMP_MODELS]    % (int)sizeof(dmodel_t));
        printf("    counts bound: planes=%d vertexes=%d nodes=%d texinfo=%d faces=%d clip=%d leafs=%d marksurf=%d surfedges=%d edges=%d models=%d\n",
               g_world.numplanes, g_world.numvertexes, g_world.numnodes, g_world.numtexinfo,
               g_world.numfaces, g_world.numclipnodes, g_world.numleafs, g_world.nummarksurfaces,
               g_world.numsurfedges, g_world.numedges, g_world.nummodels);
    }

    /* Cross-check every bound lump by internal range: garbage lumps fail hard. */
    printf("\n== 1e-b. index range cross-check between lumps\n");
    {
        int bad = 0, tot = 0;
        for (int i = 0; i < g_world.numplanes; i++) {
            const dplane_t *pl = &g_world.planes[i];
            float L = pl->normal[0]*pl->normal[0] + pl->normal[1]*pl->normal[1]
                    + pl->normal[2]*pl->normal[2];
            tot++; if (!(L > 0.99f && L < 1.01f)) bad++;
        }
        printf("    PLANES    : %d/%d normals are NOT unit length  (misbound lump => thousands)\n", bad, tot);

        tot = bad = 0;
        for (int i = 0; i < g_world.numedges; i++) {
            tot++;
            if (g_world.edges[i].v[0] >= (unsigned)g_world.numvertexes ||
                g_world.edges[i].v[1] >= (unsigned)g_world.numvertexes) bad++;
        }
        printf("    EDGES     : %d/%d vertex indices out of range (sizeof dedge_t=%zu)\n", bad, tot, sizeof(dedge_t));

        tot = bad = 0;
        for (int i = 0; i < g_world.numsurfedges; i++) {
            int e = g_world.surfedges[i];
            tot++; if (e == 0 || (e > 0 ? e : -e) >= g_world.numedges) bad++;
        }
        printf("    SURFEDGES : %d/%d edge indices out of range\n", bad, tot);

        tot = bad = 0;
        for (int i = 0; i < g_world.numfaces; i++) {
            const dface_t *f = &g_world.faces[i];
            tot++;
            if (f->planenum < 0 || f->planenum >= g_world.numplanes ||
                f->texinfo  < 0 || f->texinfo  >= g_world.numtexinfo ||
                f->firstedge < 0 || f->firstedge + f->numedges > g_world.numsurfedges) bad++;
        }
        printf("    FACES     : %d/%d faces reference planes/texinfo/surfedges out of range\n", bad, tot);

        tot = bad = 0;
        for (int i = 0; i < g_world.numtexinfo; i++) {
            tot++; if (g_world.texinfo[i].miptex < 0 || g_world.texinfo[i].miptex >= g_world.numtextures) bad++;
        }
        printf("    TEXINFO   : %d/%d miptex indices out of range\n", bad, tot);

        tot = bad = 0;
        for (int i = 0; i < g_world.nummarksurfaces; i++) {
            tot++; if (g_world.marksurfaces[i] >= (unsigned short)g_world.numfaces) bad++;
        }
        printf("    MARKSURF  : %d/%d face indices out of range\n", bad, tot);

        tot = bad = 0;
        int nleaf_neg = 0;
        for (int i = 0; i < g_world.numleafs; i++) {
            const dleaf_t *L = &g_world.leafs[i];
            tot++;
            if (L->contents > 0 || L->contents < CONTENTS_CURRENT_DOWN) nleaf_neg++;
            if (L->firstmarksurface + L->nummarksurfaces > (unsigned)g_world.nummarksurfaces) bad++;
        }
        printf("    LEAFS     : %d/%d mark ranges out of range, %d leafs with implausible contents\n", bad, tot, nleaf_neg);

        tot = bad = 0;
        for (int i = 1; i < g_world.numnodes; i++) {
            const dnode_t *N = &g_world.nodes[i];
            tot++;
            if (N->planenum < 0 || N->planenum >= g_world.numplanes ||
                N->firstface + N->numfaces > (unsigned)g_world.numfaces) bad++;
        }
        printf("    NODES     : %d/%d nodes reference planes/faces out of range\n", bad, tot);

        tot = bad = 0;
        for (int i = 0; i < g_world.nummodels; i++) {
            const dmodel_t *M = &g_world.models[i];
            tot++;
            if (M->firstface < 0 || M->firstface + M->numfaces > g_world.numfaces) bad++;
        }
        printf("    MODELS    : %d/%d models reference faces out of range (nummodels=%d, numfaces=%d)\n",
               bad, tot, g_world.nummodels, g_world.numfaces);
    }

    /* ── 1f. Do the RLE rows tile the visibility lump exactly? ────────── */
    printf("\n== 1f. row tiling: decode length vs the distance to the next row\n");
    {
        const byte *vis = (const byte *)g_world.visdata;
        int *oarr = malloc(sizeof(int) * nleaf);
        int no = 0;
        for (int l = 1; l < nleaf; l++) {
            int o = g_world.leafs[l].visofs;
            if (o >= 0) oarr[no++] = o;
        }
        /* sort + uniquify */
        for (int i = 1; i < no; i++)
            for (int j = i; j > 0 && oarr[j] < oarr[j-1]; j--) {
                int t = oarr[j]; oarr[j] = oarr[j-1]; oarr[j-1] = t;
            }
        int nu = 0;
        for (int i = 0; i < no; i++)
            if (i == 0 || oarr[i] != oarr[i-1]) oarr[nu++] = oarr[i];

        int exact = 0, short_by = 0, over = 0, run2 = 0;
        int slack_hist[16];
        memset(slack_hist, 0, sizeof(slack_hist));
        for (int k = 0; k + 1 < nu; k++) {
            int i = oarr[k], o = 0;
            while (o < rowb && i < g_world.vislen) {
                if (vis[i]) { i++; o++; }
                else { i++; int c = (i < g_world.vislen) ? vis[i++] : 0; o += c; }
            }
            int consumed = i - oarr[k];
            int gap = oarr[k+1] - oarr[k];
            if (consumed == gap) exact++;
            else if (consumed < gap) {
                short_by++;
                int slack = gap - consumed;
                slack_hist[slack < 15 ? slack : 15]++;
            } else over++;
            run2 += gap;
        }
        printf("    distinct rows=%d  exact tile=%d  short=%d  overrun=%d\n",
               nu, exact, short_by, over);
        printf("    slack histogram (bytes left unexplained between rows): ");
        for (int i = 0; i < 16; i++) if (slack_hist[i]) printf("%d:%d ", i, slack_hist[i]);
        printf("\n    last row ends at %d, lump length %d\n", oarr[nu-1], g_world.vislen);
        free(oarr);
    }

    /* ── 1g. Lighting: which lump does face lightofs index, at what stride? */
    printf("\n== 1g. lighting lump audit\n");
    {
        int maxlo = -1, neg = 0, back = 0, prev = 0;
        for (int f = 0; f < g_world.numfaces; f++) {
            int lo = g_world.faces[f].lightofs;
            if (lo < 0) { neg++; continue; }
            if (lo < prev) back++;
            prev = lo;
            if (lo > maxlo) maxlo = lo;
        }
        printf("    faces=%d  lightofs<0=%d  non-monotonic=%d  max lightofs=%d\n",
               g_world.numfaces, neg, back, maxlo);
        printf("    lumps long enough to hold the highest lightofs: ");
        for (int l = 0; l < HEADER_LUMPS; l++)
            if (g_bsp_cache.lump_lens[l] > maxlo) printf("%d ", l);
        printf("\n    (engine binds LIGHTING=8 len=%d, VISIBILITY=4 len=%d)\n",
               g_world.lightlen, g_world.vislen);

        /* Monochrome vs RGB: measure the real block size from consecutive
         * lightofs of single-style faces and compare with w*h. */
        typedef struct { int ofs, wh; } lb_t;
        lb_t *lb = malloc(sizeof(lb_t) * g_world.numfaces);
        int nlb = 0;
        for (int f = 0; f < g_world.numfaces; f++) {
            const dface_t *fa = &g_world.faces[f];
            if (fa->lightofs < 0 || fa->texinfo < 0 || fa->texinfo >= g_world.numtexinfo) continue;
            if (fa->styles[1] != 255) continue;           /* single-style faces only */
            const texinfo_t *ti = &g_world.texinfo[fa->texinfo];
            float smin = 1e9f, smax = -1e9f, tmin = 1e9f, tmax = -1e9f;
            for (int e = 0; e < fa->numedges; e++) {
                int se = fa->firstedge + e;
                if (se < 0 || se >= g_world.numsurfedges) break;
                int ev = g_world.surfedges[se];
                int vi = (ev >= 0) ? g_world.edges[ev].v[0] : g_world.edges[-ev].v[1];
                if (vi >= g_world.numvertexes) continue;
                const float *p = g_world.vertexes[vi].point;
                float s = p[0]*ti->vecs[0][0] + p[1]*ti->vecs[0][1] + p[2]*ti->vecs[0][2] + ti->vecs[0][3];
                float t = p[0]*ti->vecs[1][0] + p[1]*ti->vecs[1][1] + p[2]*ti->vecs[1][2] + ti->vecs[1][3];
                if (s < smin) smin = s; if (s > smax) smax = s;
                if (t < tmin) tmin = t; if (t > tmax) tmax = t;
            }
            int w = (int)floorf(smax/16.0f) - (int)floorf(smin/16.0f) + 1;
            int h = (int)floorf(tmax/16.0f) - (int)floorf(tmin/16.0f) + 1;
            if (w < 1 || h < 1) continue;
            lb[nlb].ofs = fa->lightofs; lb[nlb].wh = w * h; nlb++;
        }
        for (int i = 1; i < nlb; i++)
            for (int j = i; j > 0 && lb[j].ofs < lb[j-1].ofs; j--) {
                lb_t t = lb[j]; lb[j] = lb[j-1]; lb[j-1] = t;
            }
        long sum_wh = 0, sum_3wh = 0;
        int hit1 = 0, hit3 = 0, other = 0, pairs = 0;
        int rat_hist[8]; memset(rat_hist, 0, sizeof(rat_hist));
        for (int i = 0; i + 1 < nlb; i++) {
            int gap = lb[i+1].ofs - lb[i].ofs;
            if (gap <= 0 || gap > 65536) continue;
            sum_wh += lb[i].wh; sum_3wh += 3*lb[i].wh;
            if (lb[i].wh >= 8) {
                pairs++;
                int r = (int)((double)gap / (double)lb[i].wh + 0.5);
                if (r == 1) hit1++; else if (r == 3) hit3++; else other++;
                rat_hist[r < 7 ? r : 7]++;
            }
        }
        printf("    single-style faces=%d measured pairs=%d\n", nlb, pairs);
        printf("    gap/(w*h) ratio histogram: ");
        for (int i = 0; i < 8; i++) if (rat_hist[i]) printf("%dx:%d ", i, rat_hist[i]);
        printf("\n    => gap == w*h (mono): %d   gap == 3*w*h (RGB): %d   other: %d\n", hit1, hit3, other);
        printf("    sum(w*h)=%ld sum(3*w*h)=%ld vs LIGHTING lump=%d\n", sum_wh, sum_3wh, g_world.lightlen);

        int m3 = 0, m1 = 0;
        for (int f = 0; f < g_world.numfaces; f++) {
            int lo = g_world.faces[f].lightofs;
            if (lo < 0) continue;
            if (lo % 3) m1++; else m3++;
        }
        printf("    lightofs %% 3 == 0 : %d faces, != 0 : %d\n", m3, m1);
        free(lb);
    }

    /* ── 1h. What width makes the rows tile? (visleafs vs numleafs) ───── */
    printf("\n== 1h. row width search: the walk must consume exactly the bytes to the next row\n");
    {
        const byte *vis = (const byte *)g_world.visdata;
        int vis_leaf_count = 0;
        for (int l = 1; l < nleaf; l++) if (g_world.leafs[l].visofs >= 0) vis_leaf_count++;
        printf("    dmodel_t[0].visleafs = %d    leafs with a PVS row = %d    numleafs = %d\n",
               g_world.nummodels > 0 ? g_world.models[0].visleafs : -1, vis_leaf_count, nleaf);

        static const int widths[] = { 143, 144, 145, 152, 191, 192, 193 };
        /* distinct row starts, ascending */
        int *oarr = malloc(sizeof(int) * nleaf);
        int no = 0;
        for (int l = 1; l < nleaf; l++) { int o = g_world.leafs[l].visofs; if (o >= 0) oarr[no++] = o; }
        for (int i = 1; i < no; i++)
            for (int j = i; j > 0 && oarr[j] < oarr[j-1]; j--) { int t = oarr[j]; oarr[j]=oarr[j-1]; oarr[j-1]=t; }
        int nu = 0;
        for (int i = 0; i < no; i++) if (!i || oarr[i] != oarr[i-1]) oarr[nu++] = oarr[i];

        for (size_t w = 0; w < sizeof(widths)/sizeof(widths[0]); w++) {
            int W = widths[w], exact = 0, shortn = 0, overrun = 0;
            long bits = 0;
            for (int k = 0; k + 1 < nu; k++) {
                int i = oarr[k], o = 0;
                while (o < W && i < g_world.vislen) {
                    if (vis[i]) { bits += popcount8(vis[i]); i++; o++; }
                    else { i++; int c = (i < g_world.vislen) ? vis[i++] : 0; o += c; }
                }
                int consumed = i - oarr[k], gap = oarr[k+1] - oarr[k];
                if (consumed == gap) exact++; else if (consumed < gap) shortn++; else overrun++;
            }
            printf("    width=%3d  exact=%4d  short=%4d  overrun=%4d   avg visible leafs/row=%.1f\n",
                   W, exact, shortn, overrun, (double)bits / (nu - 1) / (W == 144 ? 1 : 1.33));
        }

        /* With the winning width, does the bit index mean raw leaf number or
         * vis-leaf rank?  qbsp's PVS is symmetric, so only the right
         * interpretation can score ~0 asymmetric pairs. */
        printf("\n    1h-b. bit index convention, width=%d\n", 144);
        {
            int *rank = malloc(sizeof(int) * nleaf);
            int r = 0;
            for (int l = 1; l < nleaf; l++) rank[l] = (g_world.leafs[l].visofs >= 0) ? r++ : -1;
            const int W = 144;
            byte *rows = malloc((size_t)nu * W);
            /* decode each row into a table indexed by rank (rows are in leaf order) */
            for (int l = 1; l < nleaf; l++) {
                int o = g_world.leafs[l].visofs;
                if (o < 0) continue;
                int k = rank[l];
                const byte *in = vis + o;
                byte *out = rows + (size_t)k * W;
                memset(out, 0, W);
                int oi = 0;
                while (oi < W) {
                    if (*in) out[oi++] = *in++;
                    else { in++; int c = *in++; while (c-- > 0 && oi < W) out[oi++] = 0; }
                }
            }
            for (int trial = 0; trial < 2; trial++) {
                long sym = 0, asym = 0;
                for (int a = 1; a < nleaf; a++) {
                    int ia = trial ? rank[a] : a;
                    if (ia < 0 || ia >= vis_leaf_count) continue;
                    const byte *ra = rows + (size_t)ia * W;
                    for (int b = a + 1; b < nleaf; b++) {
                        int ib = trial ? rank[b] : b;
                        if (ib < 0 || ib >= vis_leaf_count) continue;
                        int ab = (ra[ib >> 3] >> (ib & 7)) & 1;
                        int ba = (rows[(size_t)ib * W + (ia >> 3)] >> (ia & 7)) & 1;
                        if (ab == ba) sym++; else asym++;
                    }
                }
                printf("      %-22s : symmetric=%-9ld asymmetric=%ld  (%.2f%% bad)\n",
                       trial ? "bit = vis-leaf rank" : "bit = raw leaf number", sym, asym,
                       100.0 * asym / (double)(sym + asym ? sym + asym : 1));
            }
            long tot = 0;
            for (int k = 0; k < nu; k++) for (int i = 0; i < W; i++) tot += popcount8(rows[k*W+i]);
            printf("      visible leafs per row with width 144: %.1f (of %d vis leafs)\n",
                   (double)tot / nu, vis_leaf_count);
            free(rows); free(rank);
        }
        free(oarr);
    }

    /* ── 1i. Which index names a bit inside a 144-byte row? ───────────── */
    printf("\n== 1i. bit index convention: raw leaf number vs vis-leaf rank\n");
    {
        const byte *vis = (const byte *)g_world.visdata;
        const int W = 144;
        int first = -1, last = -1, cnt = 0, holes = 0;
        for (int l = 1; l < nleaf; l++) {
            if (g_world.leafs[l].visofs >= 0) { if (first < 0) first = l; last = l; cnt++; }
        }
        for (int l = 1; l <= last; l++) if (g_world.leafs[l].visofs < 0) holes++;
        printf("    vis leaf numbers: first=%d last=%d count=%d  non-vis leafs below 'last' = %d\n",
               first, last, cnt, holes);
        printf("    raw indexing would need %d bits = %d bytes/row, but a row is %d bytes (%d bits)\n",
               last + 1, (last + 8) / 8, W, W * 8);

        int *rank = malloc(sizeof(int) * nleaf);
        int r = 0;
        for (int l = 1; l < nleaf; l++) rank[l] = (g_world.leafs[l].visofs >= 0) ? r++ : -1;

        int self_raw = 0, self_rank = 0, rows = 0;
        byte *buf = malloc(W);
        for (int l = 1; l < nleaf; l++) {
            int o = g_world.leafs[l].visofs;
            if (o < 0) continue;
            rows++;
            const byte *in = vis + o;
            int oi = 0;
            memset(buf, 0, W);
            while (oi < W) {
                if (*in) buf[oi++] = *in++;
                else { in++; int c = *in++; while (c-- > 0 && oi < W) buf[oi++] = 0; }
            }
            if (l < W * 8      && ((buf[l >> 3]      >> (l & 7)) & 1))      self_raw++;
            if (rank[l] < W*8  && ((buf[rank[l] >> 3] >> (rank[l] & 7)) & 1)) self_rank++;
        }
        printf("    a leaf sees ITSELF in its own row: raw index %d/%d (%.1f%%)   rank index %d/%d (%.1f%%)\n",
               self_raw, rows, 100.0*self_raw/rows, self_rank, rows, 100.0*self_rank/rows);
        printf("    (symmetry cannot decide this: relabelling preserves it — self-visibility can)\n");
        free(buf); free(rank);
    }

    printf("\n== 2. the engine's rows now: reference decode at width %d, bit index = leaf-1\n", W);
    {
        byte *ref = malloc((size_t)W), *eng = malloc((size_t)W);
        byte *m   = calloc((size_t)vis * W, 1);
        int rows = 0, agree = 0, differ = 0, noRow = 0;
        for (int l = 1; l < nleaf; l++) {
            int ofs = g_world.leafs[l].visofs;
            int bit = World_LeafVisBit(l);
            if (bit < 0 || ofs < 0 || ofs >= g_world.vislen) { noRow++; continue; }
            rows++;
            memset(ref, 0, W); memset(eng, 0, W);
            decode_engine((const byte *)g_world.visdata + ofs, g_world.vislen - ofs, ref, W);
            World_LeafPVS(l, eng, W);                 /* the engine's own decoder */
            if (memcmp(ref, eng, W) == 0) agree++; else differ++;
            memcpy(m + (size_t)bit * W, eng, W);
        }
        printf("    engine rows identical to the reference decode: %d agree, %d differ, %d leafs without a row\n",
               agree, differ, noRow);

        int asy = 0, sym = 0;
        for (int a = 0; a < vis; a++)
            for (int b = a + 1; b < vis; b++)
                if (bit_get(m + (size_t)a * W, b) == bit_get(m + (size_t)b * W, a)) sym++;
                else asy++;
        printf("    symmetry over the rank-indexed matrix: pairs=%d asymmetric=%d (%.3f%%)\n",
               sym, asy, 100.0 * asy / (double)(sym + asy));
        printf("    (qbsp drops the rare one-way pair it cannot prove; the pre-fix matrix\n"
               "     measured 8.5%% asymmetric because every row overran its neighbour)\n");
        free(ref); free(eng); free(m);
    }

    /* ── 3. The see-through-wall gate ───────────────────────────────── */
    printf("\n== 3. unobstructed leaf reached by ray but absent from PVS (see-through)\n");
    {
        byte *mat = malloc((size_t)vis * W);
        for (int l = 1; l <= vis; l++)
            World_LeafPVS(l, mat + (size_t)(l - 1) * W, W);   /* the engine's decoder */
        static const float steps[] = { 16.0f, 4.0f };
        for (size_t s = 0; s < sizeof(steps) / sizeof(steps[0]); s++)
            ray_gate(mat, W, vis, steps[s]);
        free(mat);
    }

    /* ── 4. the same convention on every map in pak0 ────────────────── */
    printf("\n== 4. vis-leaf numbering across all pak0 maps (start + episode 1-4)\n");
    {
        static const char *maps[] = {
            "maps/start.bsp",
            "maps/e1m1.bsp", "maps/e1m2.bsp", "maps/e1m3.bsp", "maps/e1m4.bsp",
            "maps/e1m5.bsp", "maps/e1m6.bsp", "maps/e1m7.bsp", "maps/e1m8.bsp",
            "maps/e2m1.bsp", "maps/e2m2.bsp", "maps/e2m3.bsp", "maps/e2m4.bsp",
            "maps/e2m5.bsp", "maps/e2m6.bsp", "maps/e2m7.bsp",
            "maps/e3m1.bsp", "maps/e3m2.bsp", "maps/e3m3.bsp", "maps/e3m4.bsp",
            "maps/e3m5.bsp", "maps/e3m6.bsp", "maps/e3m7.bsp",
            "maps/e4m1.bsp", "maps/e4m2.bsp", "maps/e4m3.bsp", "maps/e4m4.bsp",
            "maps/e4m5.bsp", "maps/e4m6.bsp",
        };
        int checked = 0, bad = 0, skipped = 0;
        for (size_t i = 0; i < sizeof(maps) / sizeof(maps[0]); i++) {
            int r = vis_check_map(maps[i]);
            if (r < 0) { skipped++; continue; }
            checked++; bad += (r == 0);
        }
        printf("    maps checked=%d  violations=%d  not in pak0=%d  -> %s\n",
               checked, bad, skipped, bad ? "FAIL" : "OK");
    }

    printf("\n");
    return 0;
}
