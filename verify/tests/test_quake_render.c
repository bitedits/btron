/*
 * verify/tests/test_quake_render.c — what actually reaches the pixel buffer
 *
 * Two screenshots came back from the Pi 400/QEMU run: a monster rendered as one
 * enormous dark triangle, and a corridor where whole walls read as void.  Both
 * are rasteriser-side symptoms, not BSP-side ones (the PVS convention is now
 * proven correct by test_quake_visibility for all nine maps in pak0), so this
 * test stops reasoning about the scene graph and measures the last mile:
 *
 *   1. the colour path — the engine computes a per-face brightness from the BSP
 *      lightmap and hands it to glColor3f.  Does that value survive to the
 *      pixel, or does the backend's fixed-function light replace it?  Measured
 *      by drawing a known quad and reading the pixel back, with the lighting
 *      state as the backend defaults it and with it explicitly off.
 *   2. how much of a real e1m1 view is unpainted or painted black, over camera
 *      poses taken from actual vis leafs, again A/B'd on the lighting state.
 *      "Nothing drawn" (clear colour) and "drawn black" look identical to the
 *      eye but have different causes, so they are counted separately.
 *   3. the alias-model vertex stream — how many vertices a monster's single
 *      glBegin(GL_TRIANGLES) emits, and how many triangles carry an out-of-range
 *      vertex index.  Section 6 then measures how many of them the backend
 *      actually rasterizes, because a dropped vertex mid-block re-groups every
 *      later triangle and turns a zombie into one huge wrong-cornered polygon.
 *   4. near-plane handling — the backend discards an entire triangle as soon as
 *      one of its clip w values falls at or below its threshold.  clip w is the
 *      distance a vertex sits in front of the eye, so a BSP face that merely
 *      touches the camera plane contributes nothing at all: that is a wall you
 *      see straight through.  A face that survives with one very close vertex
 *      instead projects to a screen position thousands of pixels wide, which is
 *      the giant skewed dark polygon.  Both counts come from the real PVS set
 *      for real vis-leaf camera poses.
 *   5. the same rule driven the other way — one quad through the real dispatch
 *      table, wholly in front as a control and then with its near edge behind
 *      the eye.  Zero painted pixels there is the wall disappearing outright,
 *      measured on the backend rather than modelled from the BSP.
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
#include "../../src/quake/include/server.h"
#include "../../src/quake/include/render.h"
#include "../../src/quake/include/r_brush.h"
#include "../../src/quake/include/r_light.h"
#include "../../src/quake/include/r_surf.h"
#include "../../src/quake/include/r_alias.h"
#include "../../src/quake/include/texture.h"
#include "../../src/quake/include/mathlib.h"
#include "../../src/quake/include/fs_btron.h"
#include "../../src/quake/include/quake_ui.h"
#include "../../src/gl/gl_dispatch.h"

/* ── Host-side symbols the linked engine objects reference.  Everything the
 * rasteriser test is actually measuring (GL dispatch, r_brush, r_light,
 * r_surf, r_alias, texture) is linked for real, so only the window-system and
 * input half is stubbed here. ───────────────────────────────────────────── */
refdef_t r_refdef;

int in_forward = 0, in_back = 0, in_left = 0, in_right = 0;
int in_down = 0, in_jump = 0, in_attack = 0;
int in_turn_left = 0, in_turn_right = 0;

void uart_puts_raw(const char *s) { (void)s; }
void P_BloodSplash(const float o[3], int c) { (void)o; (void)c; }
void P_ExplosionParticles(const float o[3]) { (void)o; }
void P_RunParticleEffect(const float o[3], const float d[3], int col, int cnt) {
    (void)o; (void)d; (void)col; (void)cnt;
}
void P_UpdateParticles(float dt) { (void)dt; }
void Draw_Fill(int x, int y, int w, int h, uint32_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
void Draw_String(int x, int y, const char *s) { (void)x; (void)y; (void)s; }
void close_quake_window(void) {}

#define RW 320
#define RH 240

static uint32_t s_fb[RW * RH];

/* ARGB8888 as virgl_pack_color() writes it */
#define PX_R(p) (((p) >> 16) & 0xFF)
#define PX_G(p) (((p) >> 8) & 0xFF)
#define PX_B(p) ((p) & 0xFF)

/* The view matrix chain R_RenderView() builds.  Duplicated here on purpose: the
 * point of this test is to compare what the rasteriser does with the scene the
 * renderer hands it, so it must use the renderer's own camera construction. */
static void setup_camera(const float *org, const float *angles) {
    float aspect = (float)RW / (float)RH;
    glViewport(0, 0, RW, RH);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.1f * aspect, 0.1f * aspect, -0.1f, 0.1f, 0.1f, 1000.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glRotatef(-90.0f, 1.0f, 0.0f, 0.0f);
    glRotatef(90.0f, 0.0f, 0.0f, 1.0f);
    glRotatef(-angles[2], 1.0f, 0.0f, 0.0f);
    glRotatef(-angles[0], 0.0f, 1.0f, 0.0f);
    glRotatef(-angles[1], 0.0f, 0.0f, 1.0f);
    glTranslatef(-org[0], -org[1], -org[2]);
}

/* ── 1. Colour path: does glColor3f reach the pixel? ─────────────────── */
static void draw_probe_quad(float req_r, float req_g, float req_b,
                            const float *normal, uint32_t out[4]) {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.0, 1.0, -1.0, 1.0, 1.0, 100.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glNormal3f(normal[0], normal[1], normal[2]);
    glColor3f(req_r, req_g, req_b);
    glBegin(GL_QUADS);
    glVertex3f(-2.0f, -2.0f, -5.0f);
    glVertex3f( 2.0f, -2.0f, -5.0f);
    glVertex3f( 2.0f,  2.0f, -5.0f);
    glVertex3f(-2.0f,  2.0f, -5.0f);
    glEnd();

    /* Sample four interior pixels; the quad covers most of the frame. */
    const int sx[4] = { RW/2, RW/3, 2*RW/3, RW/2 };
    const int sy[4] = { RH/2, RH/3, RH/3,   2*RH/3 };
    for (int i = 0; i < 4; i++) out[i] = s_fb[sy[i] * RW + sx[i]];
}

static void measure_color_path(void) {
    printf("== 1. colour path: engine brightness vs delivered pixel\n");
    printf("   (the renderer shades every BSP face with a colour from the map's\n"
           "    lightmaps, then hands it to glColor3f.  If the fixed-function light\n"
           "    in the backend overrides it, that shading never reaches the screen.)\n");

    struct { const char *what; float n[3]; float c[3]; } probes[] = {
        { "face toward the backend's light", {0.0f, 0.0f, 1.0f}, {0.50f, 0.40f, 0.30f} },
        { "face away from it (a dark wall)", {0.0f, 0.0f,-1.0f}, {0.50f, 0.40f, 0.30f} },
        { "side-lit wall",                   {1.0f, 0.0f, 0.0f}, {0.50f, 0.40f, 0.30f} },
        { "bright light panel",              {0.0f, 0.0f, 1.0f}, {0.95f, 0.90f, 0.60f} },
        { "near-black face",                 {0.0f, 0.0f, 1.0f}, {0.05f, 0.04f, 0.03f} },
    };
    const int nprobes = (int)(sizeof(probes) / sizeof(probes[0]));

    /* Pass 0 changes nothing: it reads the state gl_init()/virgl_backend_init()
     * left behind, which is the only state the game ever draws in.  Nothing in
     * src/quake calls glEnable(GL_LIGHTING), so if that pass delivers the colour
     * the lightmap shading is intact and the fixed-function light is a dead code
     * path rather than a live defect. */
    int shipped_ok = 1;
    for (int pass = 0; pass < 3; pass++) {
        static const char *labels[3] = {
            "as left by the backend's init (the only state Quake draws in)",
            "ON (forced; Quake never asks for this)",
            "OFF"
        };
        if (pass == 1) glEnable(GL_LIGHTING);
        else if (pass == 2) glDisable(GL_LIGHTING);
        printf("  --- GL_LIGHTING %s ---\n", labels[pass]);
        for (int i = 0; i < nprobes; i++) {
            uint32_t px[4];
            draw_probe_quad(probes[i].c[0], probes[i].c[1], probes[i].c[2],
                            probes[i].n, px);
            int exact = (PX_R(px[0]) == (int)(probes[i].c[0]*255) &&
                         PX_G(px[0]) == (int)(probes[i].c[1]*255) &&
                         PX_B(px[0]) == (int)(probes[i].c[2]*255));
            if (pass == 0 && !exact) shipped_ok = 0;
            printf("      %-28s asked=(%3d,%3d,%3d) got=(%3d,%3d,%3d)  %s\n",
                   probes[i].what,
                   (int)(probes[i].c[0]*255), (int)(probes[i].c[1]*255), (int)(probes[i].c[2]*255),
                   PX_R(px[0]), PX_G(px[0]), PX_B(px[0]), exact ? "" : "<- colour discarded");
        }
    }
    printf("    verdict: the shipped colour path is %s\n",
           shipped_ok ? "intact — every glColor3f reaches the pixel verbatim"
                      : "BROKEN — the engine's shading is thrown away before the pixel");
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

/* ── 2. A real e1m1 view: how much of it is void? ───────────────────── */
static void classify_pixels(uint32_t clear_px, int *unpainted, int *black,
                            int *dim, int *lit) {
    *unpainted = *black = *dim = *lit = 0;
    for (int i = 0; i < RW * RH; i++) {
        uint32_t p = s_fb[i];
        if (p == clear_px) { (*unpainted)++; continue; }
        int mx = (int)PX_R(p); if ((int)PX_G(p) > mx) mx = (int)PX_G(p);
        if ((int)PX_B(p) > mx) mx = (int)PX_B(p);
        if (mx <= 8) (*black)++;
        else if (mx <= 32) (*dim)++;
        else (*lit)++;
    }
}

/* lighting: 0 leaves the backend's own state alone (what Quake runs with),
 * 1 forces GL_LIGHTING on, 2 forces it off. */
static void render_world_view(const float *org, const float *angles, int lighting) {
    if (lighting == 1) glEnable(GL_LIGHTING);
    else if (lighting == 2) glDisable(GL_LIGHTING);
    glClearColor(0.08f, 0.06f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    setup_camera(org, angles);
    r_refdef.vieworg[0] = org[0];
    r_refdef.vieworg[1] = org[1];
    r_refdef.vieworg[2] = org[2];
    r_refdef.viewangles[0] = angles[0];
    r_refdef.viewangles[1] = angles[1];
    r_refdef.viewangles[2] = angles[2];
    R_DrawWorld();
}

/* Find camera positions the map actually offers.  A random point in the world's
 * bounding box lands outside the shell most of the time (leaf 0), which measures
 * the no-data fallback rather than a real view, so grid the bounding box and
 * keep the samples that land inside a leaf owning a PVS row. */
static int pose_ok(const float *p) {
    int leaf = World_PointInLeaf(p);
    if (leaf <= 0 || leaf >= g_world.numleafs) return 0;
    return g_world.leafs[leaf].visofs >= 0;
}

static int find_poses(float poses[][3], int want) {
    const dmodel_t *wm = &g_world.models[0];
    const int g = 32;
    float sx = (wm->maxs[0] - wm->mins[0]) / (float)g;
    float sy = (wm->maxs[1] - wm->mins[1]) / (float)g;
    float sz = (wm->maxs[2] - wm->mins[2]) / (float)g;
    if (sx <= 0 || sy <= 0 || sz <= 0) return 0;

    int valid = 0;
    for (int i = 0; i < g; i++)
    for (int j = 0; j < g; j++)
    for (int k = 0; k < g; k++) {
        float p[3] = { wm->mins[0] + (i + 0.5f) * sx,
                       wm->mins[1] + (j + 0.5f) * sy,
                       wm->mins[2] + (k + 0.5f) * sz };
        if (pose_ok(p)) valid++;
    }
    if (!valid) return 0;
    int stride = valid / want;
    if (stride < 1) stride = 1;

    int seen = 0, n = 0;
    for (int i = 0; i < g && n < want; i++)
    for (int j = 0; j < g && n < want; j++)
    for (int k = 0; k < g && n < want; k++) {
        float p[3] = { wm->mins[0] + (i + 0.5f) * sx,
                       wm->mins[1] + (j + 0.5f) * sy,
                       wm->mins[2] + (k + 0.5f) * sz };
        if (!pose_ok(p)) continue;
        /* VectorCopy() is a macro that expands its destination three times, so
         * it must not be handed a side-effecting subscript. */
        if (seen++ % stride == 0) {
            poses[n][0] = p[0]; poses[n][1] = p[1]; poses[n][2] = p[2];
            n++;
        }
    }
    return n;
}

static void measure_world_views(void) {
    printf("\n== 2. real e1m1 views: unpainted vs painted-black pixels\n");

    /* Poses the camera can legitimately occupy: the player spawn plus offsets
     * around it, kept only if the point lands in a leaf that has a PVS row.
     * A pose in solid or outside the world has no visibility data and would
     * measure the fallback path instead of the normal one. */
    enum { MAX_POSES = 8 };
    float poses[MAX_POSES][3];
    int npose = find_poses(poses, MAX_POSES);
    printf("   %d of %d grid samples land inside a leaf that owns a PVS row; "
           "%d used\n", npose, 32*32*32, npose);
    if (!npose) { printf("    no valid camera position found in this map\n"); return; }

    uint32_t clear_px = 0xFF000000u | (20u << 16) | (15u << 8) | 12u;
    /* R_RenderView() builds its frustum with zFar = 1000.  The backend clears its
     * depth buffer to 1.0e10 rather than 1.0, so a face past the far plane is not
     * rejected — it is projected with ndc z > 1 and still painted.  Counted here
     * because those faces carry almost no depth precision, not because they are
     * missing. */
    const float FAR_PLANE = 1000.0f;
    static const float yaws[4] = { 0.0f, 90.0f, 180.0f, 270.0f };
    static byte s_pvs[4096];

    static const char *pass_label[2] = {
        "backend's own state (what Quake actually draws in)",
        "GL_LIGHTING forced ON"
    };
    for (int pass = 0; pass < 2; pass++) {
        int lighting = (pass == 0) ? 0 : 1;
        printf("  --- %s ---\n", pass_label[pass]);
        int sum[4] = { 0, 0, 0, 0 };
        int views = 0;

        for (int p = 0; p < npose; p++) {
            for (int y = 0; y < 4; y++) {
                float ang[3] = { 0.0f, yaws[y], 0.0f };
                render_world_view(poses[p], ang, lighting);

                int unp, blk, dm, lt;
                classify_pixels(clear_px, &unp, &blk, &dm, &lt);
                sum[0] += unp; sum[1] += blk; sum[2] += dm; sum[3] += lt;
                views++;

                /* What does the scene graph actually offer this view? */
                int leaf = World_PointInLeaf(poses[p]);
                World_LeafPVS(leaf, s_pvs, sizeof(s_pvs));
                int marked = 0, front = 0, past_far = 0;
                for (int l = 1; l < g_world.numleafs; l++) {
                    int bit = World_LeafVisBit(l);
                    if (bit < 0 || !(s_pvs[bit >> 3] & (1 << (bit & 7)))) continue;
                    const dleaf_t *lf = &g_world.leafs[l];
                    for (int m = 0; m < lf->nummarksurfaces; m++) {
                        int mi = lf->firstmarksurface + m;
                        if (mi >= g_world.nummarksurfaces) break;
                        int fi = g_world.marksurfaces[mi];
                        if (fi < 0 || fi >= g_world.numfaces) continue;
                        marked++;
                        const dface_t *fa = &g_world.faces[fi];
                        if (fa->planenum < 0 || fa->planenum >= g_world.numplanes) continue;
                        const dplane_t *pl = &g_world.planes[fa->planenum];
                        float cd = DotProduct(poses[p], pl->normal) - pl->dist;
                        if (!((fa->side == 0) ? (cd >= -0.1f) : (cd <= 0.1f))) continue;
                        front++;
                        /* Euclidean eye-to-face-centre distance, a generous
                         * proxy for eye-space depth against a spherical far clip */
                        float c[3] = { 0, 0, 0 };
                        int cnt = 0;
                        for (int e = 0; e < fa->numedges && cnt < 3; e++) {
                            int se = fa->firstedge + e;
                            if (se >= g_world.numsurfedges) break;
                            int ev = g_world.surfedges[se];
                            int vi = (ev >= 0) ? g_world.edges[ev].v[0] : g_world.edges[-ev].v[1];
                            if (vi >= g_world.numvertexes) continue;
                            for (int k = 0; k < 3; k++) c[k] += g_world.vertexes[vi].point[k];
                            cnt++;
                        }
                        if (!cnt) continue;
                        float dx = c[0]/cnt - poses[p][0];
                        float dy = c[1]/cnt - poses[p][1];
                        float dz = c[2]/cnt - poses[p][2];
                        if (sqrtf(dx*dx + dy*dy + dz*dz) > FAR_PLANE) past_far++;
                    }
                }
                printf("      pose%d leaf%-5d yaw%-4.0f  painted=%5.1f%% (black=%-5d dim=%-5d)  "
                       "PVS faces=%-5d front=%-5d past zFar=%d: %d\n",
                       p, leaf, yaws[y], 100.0 * (blk + dm + lt) / (RW * RH), blk, dm,
                       marked, front, (int)FAR_PLANE, past_far);
            }
        }
        int all = views * RW * RH;
        printf("    %d views: unpainted=%.1f%%  black=%.1f%%  dim=%.1f%%  lit=%.1f%%\n",
               views, 100.0*sum[0]/all, 100.0*sum[1]/all, 100.0*sum[2]/all,
               100.0*sum[3]/all);
    }
    printf("   'black' is a wall drawn with no light in it; the eye reads that as a\n"
           "   hole exactly like 'unpainted', but the two have different causes.\n");
    /* The forced-ON pass runs last, so restore the shipped state here: leaving
     * GL_LIGHTING enabled would make every section after this one measure the
     * lighting model instead of the colour the renderer asked for. */
    glDisable(GL_LIGHTING);
}

/* ── 3. Alias model vertex stream ───────────────────────────────────── */
static void measure_alias_streams(void) {
    printf("\n== 3. alias models: one glBegin each, and vertex-index sanity\n");
    if (!g_num_alias_models) {
        printf("    no alias models were loaded during map spawn\n");
        return;
    }
    int desync = 0, biggest = 0, longest = 0;
    for (int m = 0; m < g_num_alias_models; m++) {
        alias_model_t *mdl = &g_alias_models[m];
        if (!mdl->is_loaded) continue;
        int emitted = mdl->numtris * 3;
        if (emitted > biggest) { biggest = emitted; longest = m; }
        int bad = 0;
        for (int t = 0; t < mdl->numtris; t++)
            for (int k = 0; k < 3; k++)
                if (mdl->tris[t][k] < 0 || mdl->tris[t][k] >= mdl->numverts) { bad++; break; }
        if (bad) desync++;
        printf("      %-28s verts=%-5d tris=%-5d vertices in one glBegin=%-6d tris w/ bad index=%d\n",
               mdl->name, mdl->numverts, mdl->numtris, emitted, bad);
    }
    printf("    longest single glBegin=%d vertices (%s), models with an out-of-range "
           "triangle index=%d\n",
           biggest, g_alias_models[longest].name, desync);
    printf("   Whether all of those vertices reach the rasteriser is measured directly\n"
           "   in section 6; a dropped vertex mid-block would re-group every later\n"
           "   triangle, which is what turns a zombie into one huge wrong-cornered polygon.\n");
}

/* ── 4. Near-plane handling ────────────────────────────────────────── */
/* The backend throws away a whole triangle the moment one of its clip w values
 * sits at or below its threshold (src/gl/backend_virgl.c:328).  For a GL
 * frustum clip w is just how far in front of the eye that vertex lies —
 * dot(vertex - eye, forward) — so this classification needs no matrix and can be
 * run against exactly the faces the PVS hands the view.  Two outcomes matter:
 *   discarded  a face with one vertex behind the camera contributes nothing, so
 *              a wall you are walking into disappears and you see through it;
 *   stretched  a face that survives with an extremely close vertex projects to a
 *              point thousands of pixels off, i.e. one giant skewed polygon.
 */
static void measure_near_plane(void) {
    printf("\n== 4. near plane: faces the backend discards or stretches\n");

    enum { MAX_POSES = 8 };
    float poses[MAX_POSES][3];
    int npose = find_poses(poses, MAX_POSES);
    if (!npose) { printf("    no valid camera position found in this map\n"); return; }

    const float NEAR_EPS = 0.05f;    /* backend_virgl.c:328 */
    const float STRETCH_LIMIT = 8.0f;
    static const float yaws[8] = { 0.f, 45.f, 90.f, 135.f, 180.f, 225.f, 270.f, 315.f };
    static byte s_pvs[4096];

    int n_discarded = 0, n_stretched = 0, n_normal = 0, n_behind = 0;
    int views = 0, views_discarding = 0, views_stretching = 0;

    for (int p = 0; p < npose; p++) {
        int pd = 0, ps = 0;
        for (int y = 0; y < 8; y++) {
            float ang[3] = { 0.0f, yaws[y], 0.0f };
            vec3_t fwd;
            AngleVectors(ang, fwd, NULL, NULL);

            int leaf = World_PointInLeaf(poses[p]);
            World_LeafPVS(leaf, s_pvs, sizeof(s_pvs));

            int d = 0, s = 0, ok = 0, behind = 0;
            for (int l = 1; l < g_world.numleafs; l++) {
                int bit = World_LeafVisBit(l);
                if (bit < 0 || !(s_pvs[bit >> 3] & (1 << (bit & 7)))) continue;
                const dleaf_t *lf = &g_world.leafs[l];
                for (int m = 0; m < lf->nummarksurfaces; m++) {
                    int mi = lf->firstmarksurface + m;
                    if (mi >= g_world.nummarksurfaces) break;
                    int fi = g_world.marksurfaces[mi];
                    if (fi < 0 || fi >= g_world.numfaces) continue;
                    const dface_t *fa = &g_world.faces[fi];
                    if (fa->planenum < 0 || fa->planenum >= g_world.numplanes) continue;

                    /* same backface test r_brush.c uses */
                    const dplane_t *pl = &g_world.planes[fa->planenum];
                    float cd = DotProduct(poses[p], pl->normal) - pl->dist;
                    if (!((fa->side == 0) ? (cd >= -0.1f) : (cd <= 0.1f))) continue;

                    float minw = 0.0f, maxw = 0.0f;
                    int nv = 0;
                    for (int e = 0; e < fa->numedges; e++) {
                        int se = fa->firstedge + e;
                        if (se >= g_world.numsurfedges) break;
                        int ev = g_world.surfedges[se];
                        int vi = (ev >= 0) ? g_world.edges[ev].v[0]
                                           : g_world.edges[-ev].v[1];
                        if (vi < 0 || vi >= g_world.numvertexes) continue;
                        const float *v = g_world.vertexes[vi].point;
                        float w = (v[0] - poses[p][0]) * fwd[0]
                                + (v[1] - poses[p][1]) * fwd[1]
                                + (v[2] - poses[p][2]) * fwd[2];
                        if (!nv) { minw = maxw = w; }
                        else {
                            if (w < minw) minw = w;
                            if (w > maxw) maxw = w;
                        }
                        nv++;
                    }
                    if (nv < 3) continue;

                    if (maxw <= NEAR_EPS) behind++;
                    else if (minw <= NEAR_EPS) d++;
                    else if (minw < STRETCH_LIMIT) s++;
                    else ok++;
                }
            }
            n_discarded += d; n_stretched += s; n_normal += ok; n_behind += behind;
            pd += d; ps += s;
            views++;
            if (d) views_discarding++;
            if (s) views_stretching++;
        }
        printf("      pose%d (%7.1f %7.1f %7.1f): faces discarded outright=%-5d "
               "stretched by a vertex under %.0f units=%d\n",
               p, poses[p][0], poses[p][1], poses[p][2], pd, STRETCH_LIMIT, ps);
    }

    int total = n_discarded + n_stretched + n_normal + n_behind;
    printf("    %d views, %d front-facing PVS faces considered:\n", views, total);
    printf("      need no clipping at all=%d (%.1f%%)\n",
           n_normal, 100.0 * n_normal / (total ? total : 1));
    printf("      have a vertex under %.0f units in front, so they need clipping=%d (%.1f%%)\n",
           STRETCH_LIMIT, n_stretched, 100.0 * n_stretched / (total ? total : 1));
    printf("      cross the camera plane: whole-triangle rejection lost these=%d (%.1f%%)\n",
           n_discarded, 100.0 * n_discarded / (total ? total : 1));
    printf("      entirely behind the eye (correctly not drawn)=%d\n", n_behind);
    printf("    views where rejection hid at least one wall you should see=%d/%d\n",
           views_discarding, views);
    printf("    views containing at least one stretched polygon=%d/%d\n",
           views_stretching, views);
    printf("   Clipping recovers every face in the third line: each has part of it in\n"
           "   front of the eye, and that part is what should be painted.\n");
}

/* ── 5. The backend's own near-plane rule, at primitive level ────────── */
/* Sections 4 models a rule from the BSP side; this one drives the real dispatch
 * table so the result is unambiguous.  The quad is described in eye space —
 * a rectangle lying in the plane e_y = yl, running from depth e_z = z0 to
 * z1 — and converted with the camera chain's own mapping at yaw zero,
 * e = (-y_w, z_w, -x_w), i.e. world = (-e_z, -e_x, e_y).  The first row is a
 * control for exactly that: if the conversion were wrong it would read 0 too,
 * and then nothing below it would mean anything.
 */
static int paint_eye_quad(float z0, float z1, float x_ext, float y_ext) {
    /* corners: (e_x, e_y, e_z) -> world (-e_z, -e_x, e_y) */
    float c[4][3] = {
        { -z0, -x_ext, y_ext }, { -z0,  x_ext, y_ext },
        { -z1,  x_ext, y_ext }, { -z1, -x_ext, y_ext },
    };

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    float org[3] = { 0.0f, 0.0f, 0.0f }, ang[3] = { 0.0f, 0.0f, 0.0f };
    setup_camera(org, ang);

    glNormal3f(0.0f, 0.0f, 1.0f);
    glColor3f(0.5f, 0.5f, 0.5f);
    glBegin(GL_TRIANGLES);
    glVertex3f(c[0][0], c[0][1], c[0][2]); glVertex3f(c[1][0], c[1][1], c[1][2]);
    glVertex3f(c[2][0], c[2][1], c[2][2]); glVertex3f(c[0][0], c[0][1], c[0][2]);
    glVertex3f(c[2][0], c[2][1], c[2][2]); glVertex3f(c[3][0], c[3][1], c[3][2]);
    glEnd();

    int painted = 0;
    for (int i = 0; i < RW * RH; i++)
        if (s_fb[i] != 0xFF000000u) painted++;
    return painted;
}

static void measure_primitive_near_plane(void) {
    printf("\n== 5. one quad, driven through the real dispatch table\n");

    struct { const char *what; float z0, z1; } probes[] = {
        { "quad wholly in front (control for the basis)", -2.0f, -20.0f },
        { "quad with its near edge behind the eye",        8.0f, -20.0f },
        { "wall whose near edge is 200 units behind",    200.0f, -20.0f },
    };
    const int n = (int)(sizeof(probes) / sizeof(probes[0]));

    for (int i = 0; i < n; i++) {
        /* Same lateral extent in both eye axes, so the row does not depend on
         * which of them a given world axis supplies. */
        int px = paint_eye_quad(probes[i].z0, probes[i].z1, 5.0f, 5.0f);
        printf("      %-42s e_z=[%6.1f .. %6.1f]  painted=%5d px (%.1f%%)  %s\n",
               probes[i].what, probes[i].z0, probes[i].z1, px,
               100.0 * px / (RW * RH),
               px == 0 ? "<- NOTHING REACHES THE SCREEN" : "");
    }
    printf("   A quad that covers most of the frame and merely touches the camera\n"
           "   plane must be clipped at that plane, not discarded.  Zero pixels for\n"
           "   the last two rows is the wall disappearing out from under the player.\n");
}

/* One glBegin holds n-1 grey filler triangles and then, still inside the same
 * block, one red marker over a part of the frame the filler never touches.  If
 * the red is on the screen the block's last primitive reached the rasterizer —
 * binary, with no counts or buffer sizes to model.  player.mdl emits 408
 * triangles in a single block, so that is the size tested.
 *
 * The coordinates below are written in eye space and converted with the camera
 * chain's own mapping at yaw zero, e = (-y_w, z_w, -x_w), i.e.
 * world = (-e_z, -e_x, e_y): a flat quad at e_z = -10 is world x = +10.
 */
static int marker_block_reaches_screen(int n, int *red_px, int *cx, int *cy) {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);   /* these probes identify primitives by their colour */
    float org[3] = { 0, 0, 0 }, ang[3] = { 0, 0, 0 };
    setup_camera(org, ang);

    /* Eye-space corners, converted with the camera chain's own mapping at yaw
     * zero: e = (-y_w, z_w, -x_w), so world = (-e_z, -e_x, e_y). */
    const float ez = -10.0f;
    const float wx = -ez;

    glNormal3f(0.0f, 0.0f, 1.0f);
    glBegin(GL_TRIANGLES);
    for (int i = 0; i < n - 1; i++) {
        glColor3f(0.5f, 0.5f, 0.5f);
        glVertex3f(wx,  1.5f, -1.5f);
        glVertex3f(wx, -1.5f, -1.5f);
        glVertex3f(wx,  0.0f,  0.5f);
    }
    glColor3f(1.0f, 0.0f, 0.0f);
    glVertex3f(wx, -0.5f,  0.5f);
    glVertex3f(wx, -1.4f,  0.5f);
    glVertex3f(wx, -1.4f,  1.3f);
    glEnd();

    int cnt = 0, sx = 0, sy = 0;
    int any = 0, minx = RW, maxx = -1, miny = RH, maxy = -1;
    uint32_t brightest = 0;
    for (int y = 0; y < RH; y++) {
        for (int x = 0; x < RW; x++) {
            uint32_t p = s_fb[y * RW + x];
            if (PX_R(p) > 200 && PX_G(p) < 60 && PX_B(p) < 60) {
                cnt++; sx += x; sy += y;
            }
            if (p != 0xFF000000u) {
                any++;
                if (x < minx) minx = x;
                if (x > maxx) maxx = x;
                if (y < miny) miny = y;
                if (y > maxy) maxy = y;
                if (p > brightest) brightest = p;
            }
        }
    }
    if (getenv("RENDER_PROBE_DUMP") && any) {
        printf("        [dump] non-black=%d bbox x[%d..%d] y[%d..%d] brightest=(%d,%d,%d)\n",
               any, minx, maxx, miny, maxy,
               PX_R(brightest), PX_G(brightest), PX_B(brightest));
    }
    *red_px = cnt;
    *cx = cnt ? sx / cnt : -1;
    *cy = cnt ? sy / cnt : -1;
    return cnt > 0;
}

static void measure_assembly_capacity(void) {
    printf("\n== 6. does the last triangle of a long glBegin reach the screen?\n");

    const int ns[] = { 1, 20, 86, 408, 1000 };
    for (int i = 0; i < 5; i++) {
        int cnt = 0, cx = 0, cy = 0;
        int ok = marker_block_reaches_screen(ns[i], &cnt, &cx, &cy);
        printf("      glBegin with %-5d triangles (%-5d vertices): marker red=%5d px at (%3d,%3d)  last one drew=%s%s\n",
               ns[i], ns[i] * 3, cnt, cx, cy, ok ? "yes" : "NO",
               ok ? "" : "  <- the block's tail is lost");
    }
    printf("   The first row has no filler at all, so it locates the marker by itself:\n"
           "   if it reads 0 the probe geometry is wrong, not the assembler.  The other\n"
           "   rows add grey triangles ahead of it; 86/408/1000 all exceed the 256\n"
           "   vertices the old fixed block could hold, which is how player.mdl (1224)\n"
           "   and dog.mdl (1278) lost their tails.\n");
}

int main(void) {
    if (!FS_Init()) { printf("pak0.pak not found\n"); return 1; }

    /* A real dispatch table, not the int stub the other quake tests use: this
     * test is about the backend itself. */
    gl_init(GL_BACKEND_VIRGL, RW, RH, s_fb);
    if (!g_gl) { printf("no GL backend\n"); return 1; }

    UI_Init();
    World_ChangeMap("maps/e1m1.bsp");
    if (!g_world.is_loaded) { printf("e1m1 did not load\n"); return 1; }

    printf("== e1m1 for the rasteriser: faces=%d leafs=%d models=%d alias models=%d\n",
           g_world.numfaces, g_world.numleafs, g_world.nummodels, g_num_alias_models);

    measure_color_path();
    measure_world_views();
    measure_alias_streams();
    measure_near_plane();
    measure_primitive_near_plane();
    measure_assembly_capacity();

    printf("\n");
    gl_shutdown();
    return 0;
}
