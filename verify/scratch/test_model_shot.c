#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../../include/btron/types.h"
#include "../../src/quake/include/quakedef.h"
#include "../../src/quake/include/world.h"
#include "../../src/quake/include/render.h"
#include "../../src/quake/include/r_alias.h"
#include "../../src/quake/include/r_brush.h"
#include "../../src/quake/include/quake_ui.h"
#include "../../src/quake/include/fs_btron.h"
#include "../../src/quake/include/texture.h"
#include "../../src/gl/gl_dispatch.h"

refdef_t r_refdef;
int in_forward = 0, in_back = 0, in_left = 0, in_right = 0;
int in_down = 0, in_jump = 0, in_attack = 0;
int in_turn_left = 0, in_turn_right = 0;
uint32_t btron_render_perf_us(void) { return 0; }
void uart_puts_raw(const char *s) { (void)s; }
void P_BloodSplash(const float o[3], int c) { (void)o; (void)c; }
void P_ExplosionParticles(const float o[3]) { (void)o; }
void P_RunParticleEffect(const float o[3], const float d[3], int col, int cnt) { (void)o; (void)d; (void)col; (void)cnt; }
void P_UpdateParticles(float dt) { (void)dt; }
void Draw_Fill(int x, int y, int w, int h, uint32_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
void Draw_String(int x, int y, const char *s) { (void)x; (void)y; (void)s; }
void close_quake_window(void) {}

#define W 320
#define H 320
static uint32_t s_fb[W * H];

static void dump_raw(const char *path) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return;
    int w = W, h = H;
    fwrite(&w, sizeof(int), 1, fp);
    fwrite(&h, sizeof(int), 1, fp);
    fwrite(s_fb, sizeof(uint32_t), W * H, fp);
    fclose(fp);
}

static void setup_camera(const float *org, const float *angles) {
    float aspect = (float)W / (float)H;
    glViewport(0, 0, W, H);
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

int main(void) {
    if (!FS_Init()) { printf("pak0.pak not found\n"); return 1; }
    gl_init(GL_BACKEND_VIRGL, W, H, s_fb);
    if (!g_gl) { printf("no GL backend\n"); return 1; }

    TEX_LoadPalette("gfx/palette.lmp");

    int mi = R_LoadAliasModel("progs/soldier.mdl");
    if (mi < 0) { printf("failed to load soldier.mdl\n"); return 1; }
    alias_model_t *mdl = &g_alias_models[mi];
    printf("Loaded %s: %d verts, %d tris, %d frames, skin %dx%d\n",
           mdl->name, mdl->numverts, mdl->numtris, mdl->numframes, mdl->skinwidth, mdl->skinheight);

    /* Clear framebuffer to transparent */
    memset(s_fb, 0, sizeof(s_fb));
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);

    /* Viewport & projection */
    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float aspect = (float)W / (float)H;
    glFrustum(-0.1f * aspect, 0.1f * aspect, -0.1f, 0.1f, 0.1f, 1000.0f);

    /* Compute bounding box of frame 0 */
    float mins[3] = {1e9, 1e9, 1e9}, maxs[3] = {-1e9, -1e9, -1e9};
    mdl_vert_t *verts = mdl->frames[0];
    for (int i = 0; i < mdl->numverts; i++) {
        for (int k = 0; k < 3; k++) {
            if (verts[i].v[k] < mins[k]) mins[k] = verts[i].v[k];
            if (verts[i].v[k] > maxs[k]) maxs[k] = verts[i].v[k];
        }
    }
    float center[3] = {
        (mins[0] + maxs[0]) * 0.5f,
        (mins[1] + maxs[1]) * 0.5f,
        (mins[2] + maxs[2]) * 0.5f
    };
    float dx = maxs[0] - mins[0];
    float dy = maxs[1] - mins[1];
    float dz = maxs[2] - mins[2];
    float max_dim = fmaxf(fmaxf(dx, dy), dz);
    float dist = max_dim * 1.05f;

    World_ChangeMap("maps/e1m1.bsp");
    printf("Loaded e1m1.bsp: spawn=(%.1f, %.1f, %.1f), angle=%.1f\n",
           g_world.spawn_origin[0], g_world.spawn_origin[1], g_world.spawn_origin[2], g_world.spawn_angle);

    float eye[3] = { g_world.spawn_origin[0], g_world.spawn_origin[1], g_world.spawn_origin[2] + 22.0f };
    float ang[3] = { 0.0f, g_world.spawn_angle, 0.0f };

    glClearColor(0.08f, 0.06f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    setup_camera(eye, ang);

    r_refdef.vieworg[0] = eye[0];
    r_refdef.vieworg[1] = eye[1];
    r_refdef.vieworg[2] = eye[2];
    r_refdef.viewangles[0] = ang[0];
    r_refdef.viewangles[1] = ang[1];
    r_refdef.viewangles[2] = ang[2];

    R_DrawWorld();

    dump_raw("/tmp/btron_raw_screens/test_e1m1.raw");
    printf("Dumped /tmp/btron_raw_screens/test_e1m1.raw\n");

    gl_shutdown();
    return 0;
}
