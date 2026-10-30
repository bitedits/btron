/*
 * verify/scratch/capture_quake.c - Automated Quake Screenshot Capture
 * 21:9 format (672x288) for all Episode 1 maps and model portraits.
 */
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
#include "../../src/quake/include/r_light.h"
#include "../../src/gl/gl_dispatch.h"

/* ── Stubs: symbols not supplied by any linked Quake source ───────── */
/* uart_puts_raw: provided by platform UART driver in full builds      */
void uart_puts_raw(const char *s) { (void)s; }
/* btron_render_perf_us: T-Kernel performance counter, N/A on macOS   */
uint32_t btron_render_perf_us(void) { return 0; }
/* close_quake_window: quake_app.c not linked in headless capture      */
void close_quake_window(void) {}
/* quake_is_running: also from quake_app.c                            */
int quake_is_running(void) { return 0; }
/* EGL window stubs — headless capture has no windowing layer */
#include "../../include/btron/wnd.h"
ER   inval_wnd(WND *w){(void)w;return 0;}
void wnd_set_gl_bind(WndGLBind b){(void)b;}
ER   wnd_set_title(WND *w,const char*t){(void)w;(void)t;return 0;}


#define SW 672
#define SH 288
static uint32_t s_fb[SW*SH];

static void dump_raw(const char*path){
    FILE*fp=fopen(path,"wb"); if(!fp){fprintf(stderr,"Cannot open %s\n",path);return;}
    int w=SW,h=SH; fwrite(&w,sizeof(int),1,fp); fwrite(&h,sizeof(int),1,fp);
    fwrite(s_fb,sizeof(uint32_t),SW*SH,fp); fclose(fp);
    printf("  [SHOT] %s (%dx%d)\n",path,SW,SH);
}

static void setup_camera(const float org[3],const float ang[3]){
    float aspect=(float)SW/(float)SH;
    glViewport(0,0,SW,SH);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glFrustum(-0.1f*aspect,0.1f*aspect,-0.1f,0.1f,0.1f,1200.0f);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glRotatef(-90.0f,1.0f,0.0f,0.0f);
    glRotatef( 90.0f,0.0f,0.0f,1.0f);
    glRotatef(-ang[2],1.0f,0.0f,0.0f);
    glRotatef(-ang[0],0.0f,1.0f,0.0f);
    glRotatef(-ang[1],0.0f,0.0f,1.0f);
    glTranslatef(-org[0],-org[1],-org[2]);
}

typedef struct {
    char  classname[64];
    float origin[3];
    float light;
    int   has_origin;
    int   has_light;
} parsed_ent_t;

typedef void (*ent_cb_t)(const parsed_ent_t *ent, void *userdata);

static void for_each_map_entity(ent_cb_t cb, void *userdata) {
    if (!g_world.entities) return;
    const char *p = g_world.entities;
    while (*p) {
        p = strchr(p, '{');
        if (!p) break;
        p++;

        parsed_ent_t ent;
        memset(&ent, 0, sizeof(ent));
        ent.light = 300.0f;  /* Default Quake torch radius when no "light" key present */

        while (*p) {
            while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
            if (*p == '}' || !*p) break;
            if (*p == '\"') {
                p++;
                char key[64] = {0}, val[128] = {0};
                int ki = 0, vi = 0;
                while (*p && *p != '\"' && ki < 63) key[ki++] = *p++;
                if (*p == '\"') p++;
                while (*p && (*p == ' ' || *p == '\t')) p++;
                if (*p == '\"') {
                    p++;
                    while (*p && *p != '\"' && vi < 127) val[vi++] = *p++;
                    if (*p == '\"') p++;
                }
                if (strcmp(key, "classname") == 0) {
                    strncpy(ent.classname, val, sizeof(ent.classname) - 1);
                } else if (strcmp(key, "origin") == 0) {
                    if (sscanf(val, "%f %f %f", &ent.origin[0], &ent.origin[1], &ent.origin[2]) == 3) {
                        ent.has_origin = 1;
                    }
                } else if (strcmp(key, "light") == 0) {
                    float lv = 0;
                    if (sscanf(val, "%f", &lv) == 1 && lv > 0) {
                        ent.light = lv;
                        ent.has_light = 1;
                    }
                }
            } else {
                p++;
            }
        }
        if (*p == '}') p++;
        if (ent.has_origin) {
            cb(&ent, userdata);
        }
    }
}

typedef struct {
    float origin[3];
    float light;
    float dist;
} cand_light_t;

typedef struct {
    cand_light_t cands[128];
    int count;
} light_collect_t;

static void light_collect_cb(const parsed_ent_t *ent, void *userdata) {
    light_collect_t *col = (light_collect_t *)userdata;
    if (strncmp(ent->classname, "light", 5) != 0) return;
    if (col->count >= 128) return;
    float dx = ent->origin[0] - r_refdef.vieworg[0];
    float dy = ent->origin[1] - r_refdef.vieworg[1];
    float dz = ent->origin[2] - r_refdef.vieworg[2];
    float dist = sqrtf(dx*dx + dy*dy + dz*dz);
    if (dist > 1500.0f) return;
    col->cands[col->count].origin[0] = ent->origin[0];
    col->cands[col->count].origin[1] = ent->origin[1];
    col->cands[col->count].origin[2] = ent->origin[2];
    col->cands[col->count].light = ent->light;
    col->cands[col->count].dist = dist;
    col->count++;
}

static int compare_lights(const void *a, const void *b) {
    const cand_light_t *la = (const cand_light_t *)a;
    const cand_light_t *lb = (const cand_light_t *)b;
    if (la->dist < lb->dist) return -1;
    if (la->dist > lb->dist) return 1;
    return 0;
}

static void spawn_map_lights(void) {
    g_num_dlights = 0;
    light_collect_t col = {0};
    for_each_map_entity(light_collect_cb, &col);
    if (col.count > 1) {
        qsort(col.cands, col.count, sizeof(cand_light_t), compare_lights);
    }
    int limit = col.count;
    if (limit > 28) limit = 28; /* Keep inside MAX_DLIGHTS (32), reserve 2 for fill lights */
    for (int i = 0; i < limit; i++) {
        /* Quake light entity values are in BSP units. Scale by 3.5x so that a
         * default 300-unit torch lights surfaces up to ~1000 units away, which
         * matches the authentic in-game visual range. Intensity 1.0 ensures
         * surfaces at the edge of radius still receive meaningful contribution. */
        float radius = col.cands[i].light * 2.0f;
        if (radius < 300.0f) radius = 300.0f;
        R_AddDynamicLight(col.cands[i].origin, radius, 0.80f);
    }
}

typedef struct {
    int flame_mdl;
    int flame2_mdl;
    int drawn;
} draw_lights_ctx_t;

static void draw_flame_quad(const float org[3], float w, float h, float z_ofs,
                            float r, float g, float b, float yaw, float angle_ofs) {
    float rad = (yaw + angle_ofs) * (3.14159265f / 180.0f);
    float rx = sinf(rad) * (w * 0.5f);
    float ry = -cosf(rad) * (w * 0.5f);
    float base_z = org[2] + z_ofs;

    /* Tapered flame polygon: wider base, curved mid, sharp tip */
    float bl_x = org[0] - rx, bl_y = org[1] - ry, bl_z = base_z;
    float br_x = org[0] + rx, br_y = org[1] + ry, br_z = base_z;
    float ml_x = org[0] - rx * 0.72f, ml_y = org[1] - ry * 0.72f, ml_z = base_z + h * 0.50f;
    float mr_x = org[0] + rx * 0.72f, mr_y = org[1] + ry * 0.72f, mr_z = base_z + h * 0.50f;
    float tip_x = org[0], tip_y = org[1], tip_z = base_z + h;

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glColor3f(r, g, b);

    glBegin(GL_TRIANGLES);
    /* Lower body */
    glVertex3f(bl_x, bl_y, bl_z);
    glVertex3f(br_x, br_y, br_z);
    glVertex3f(mr_x, mr_y, mr_z);

    glVertex3f(bl_x, bl_y, bl_z);
    glVertex3f(mr_x, mr_y, mr_z);
    glVertex3f(ml_x, ml_y, ml_z);

    /* Upper flame tip */
    glVertex3f(ml_x, ml_y, ml_z);
    glVertex3f(mr_x, mr_y, mr_z);
    glVertex3f(tip_x, tip_y, tip_z);
    glEnd();
}

static void draw_flame_layer(const float org[3], float w, float h, float z_ofs,
                             float r, float g, float b, float yaw) {
    draw_flame_quad(org, w, h, z_ofs, r, g, b, yaw, 0.0f);
    /* Cross planes at ±60 deg for full 3D volumetric flame appearance */
    draw_flame_quad(org, w * 0.88f, h * 0.95f, z_ofs, r * 0.90f, g * 0.80f, b * 0.70f, yaw, 60.0f);
    draw_flame_quad(org, w * 0.88f, h * 0.95f, z_ofs, r * 0.90f, g * 0.80f, b * 0.70f, yaw, -60.0f);
}

static void draw_fire_effect(const float org[3], int type, float cur_yaw) {
    if (type == 0) {
        /* Wall torch: flame rising from wall bracket */
        draw_flame_layer(org, 10.0f, 22.0f, -2.0f, 0.95f, 0.35f, 0.02f, cur_yaw);
        draw_flame_layer(org, 6.5f, 16.0f, 0.0f,  1.0f, 0.75f, 0.10f, cur_yaw);
        draw_flame_layer(org, 3.5f, 10.0f, 2.0f,  1.0f, 0.98f, 0.60f, cur_yaw);
    } else if (type == 1) {
        /* Large fire basin (start room fire pots): big roaring fire */
        draw_flame_layer(org, 26.0f, 44.0f, 2.0f, 0.95f, 0.28f, 0.02f, cur_yaw);
        draw_flame_layer(org, 18.0f, 34.0f, 4.0f, 1.0f, 0.60f, 0.08f, cur_yaw);
        draw_flame_layer(org, 11.0f, 24.0f, 6.0f, 1.0f, 0.90f, 0.30f, cur_yaw);
        draw_flame_layer(org, 5.0f, 14.0f, 8.0f,  1.0f, 1.00f, 0.80f, cur_yaw);
    } else if (type == 2) {
        /* Small yellow flame */
        draw_flame_layer(org, 14.0f, 24.0f, 0.0f, 0.95f, 0.40f, 0.04f, cur_yaw);
        draw_flame_layer(org, 8.5f, 17.0f, 2.0f,  1.0f, 0.80f, 0.18f, cur_yaw);
        draw_flame_layer(org, 4.5f, 10.0f, 3.0f,  1.0f, 0.98f, 0.65f, cur_yaw);
    } else if (type == 3) {
        /* Small white/blue flame */
        draw_flame_layer(org, 14.0f, 24.0f, 0.0f, 0.40f, 0.70f, 1.0f, cur_yaw);
        draw_flame_layer(org, 8.5f, 17.0f, 2.0f,  0.75f, 0.90f, 1.0f, cur_yaw);
        draw_flame_layer(org, 4.5f, 10.0f, 3.0f,  1.0f, 1.0f, 1.0f, cur_yaw);
    }
}

static void draw_light_cb(const parsed_ent_t *ent, void *userdata) {
    draw_lights_ctx_t *ctx = (draw_lights_ctx_t *)userdata;
    int mi = -1;
    int frame = 0;
    int ftype = -1;
    if (strstr(ent->classname, "torch")) {
        mi = ctx->flame_mdl;
        frame = 0;
        ftype = 0;
    } else if (strstr(ent->classname, "flame_large")) {
        mi = ctx->flame2_mdl;
        frame = 1;
        ftype = 1;
    } else if (strstr(ent->classname, "flame_small_white")) {
        mi = ctx->flame2_mdl;
        frame = 0;
        ftype = 3;
    } else if (strstr(ent->classname, "flame_small") || strstr(ent->classname, "flame2")) {
        mi = ctx->flame2_mdl;
        frame = 0;
        ftype = 2;
    }
    if (mi >= 0) {
        float dx = ent->origin[0] - r_refdef.vieworg[0];
        float dy = ent->origin[1] - r_refdef.vieworg[1];
        float dz = ent->origin[2] - r_refdef.vieworg[2];
        float d = sqrtf(dx*dx + dy*dy + dz*dz);
        if (d < 1200.0f) {
            float angles[3] = {0, 0, 0};
            if (ftype == 0) {
                R_DrawAliasModel(mi, frame, ent->origin, angles, 1.5f);
            }
            if (ftype >= 0) {
                draw_fire_effect(ent->origin, ftype, r_refdef.viewangles[1]);
            }
            ctx->drawn++;
        }
    }
}

static void draw_map_lights(void) {
    draw_lights_ctx_t ctx;
    ctx.flame_mdl = R_LoadAliasModel("progs/flame.mdl");
    ctx.flame2_mdl = R_LoadAliasModel("progs/flame2.mdl");
    ctx.drawn = 0;
    if (ctx.flame_mdl >= 0 && ctx.flame2_mdl >= 0) {
        printf("  [LIGHT MDL] flame_mdl=%d tex=%u loaded=%d, flame2_mdl=%d tex=%u loaded=%d\n",
               ctx.flame_mdl, g_alias_models[ctx.flame_mdl].skin_tex[0], g_alias_models[ctx.flame_mdl].is_loaded,
               ctx.flame2_mdl, g_alias_models[ctx.flame2_mdl].skin_tex[0], g_alias_models[ctx.flame2_mdl].is_loaded);
    }
    for_each_map_entity(draw_light_cb, &ctx);
    glColor3f(1.0f, 1.0f, 1.0f);
}

static void capture_level(const char*mapname,const float eye[3],float yaw,const char*raw_out){
    if(!World_ChangeMap(mapname)){fprintf(stderr,"  [SKIP] %s\n",mapname);return;}
    float org[3],ang[3]={0.0f,yaw,0.0f};
    if(eye){org[0]=eye[0];org[1]=eye[1];org[2]=eye[2];}
    else{
        org[0]=g_world.spawn_origin[0]; org[1]=g_world.spawn_origin[1];
        org[2]=g_world.spawn_origin[2]+22.0f; ang[1]=g_world.spawn_angle;
    }
    r_refdef.vieworg[0]=org[0];r_refdef.vieworg[1]=org[1];r_refdef.vieworg[2]=org[2];
    r_refdef.viewangles[0]=ang[0];r_refdef.viewangles[1]=ang[1];r_refdef.viewangles[2]=ang[2];

    /* Animate lights so lightstyles (0=full bright) are populated */
    R_AnimateLights(0.1f);
    /* Populate dynamic lights from map light entities */
    spawn_map_lights();
    /* NOTE: No additional camera-fill dlight: start.bsp and Episode 1 BSPs have
     * fully baked lightmaps that already provide the ambient floor. Adding a
     * camera dlight washes the scene out uniformly. Map dlights only. */

    glClearColor(0.06f,0.05f,0.04f,1.0f);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    setup_camera(org,ang);
    R_DrawWorld();
    draw_map_lights();
    dump_raw(raw_out);
}

static void capture_model(const char*mdl_path,int frame,float yaw,const char*raw_out){
    int mi=R_LoadAliasModel(mdl_path);
    if(mi<0){fprintf(stderr,"  [SKIP] %s\n",mdl_path);return;}
    alias_model_t*mdl=&g_alias_models[mi];
    printf("  [MODEL] %s: mi=%d tex=%u numframes=%d numtris=%d\n",
           mdl_path, mi, mdl->skin_tex[0], mdl->numframes, mdl->numtris);
    int fr=(frame<0||frame>=mdl->numframes)?0:frame;
    mdl_vert_t*verts=mdl->frames[fr]; if(!verts)return;
    float mins[3]={1e9f,1e9f,1e9f},maxs[3]={-1e9f,-1e9f,-1e9f};
    for(int i=0;i<mdl->numverts;i++)for(int k=0;k<3;k++){
        if(verts[i].v[k]<mins[k])mins[k]=verts[i].v[k];
        if(verts[i].v[k]>maxs[k])maxs[k]=verts[i].v[k];
    }
    /* Bounding box centroid and maximum extent */
    float cx=(mins[0]+maxs[0])*0.5f,cy=(mins[1]+maxs[1])*0.5f,cz=(mins[2]+maxs[2])*0.5f;
    float dx=maxs[0]-mins[0],dy=maxs[1]-mins[1],dz=maxs[2]-mins[2];
    float max_dim=fmaxf(fmaxf(dx,dy),dz);
    /* Camera distance: keep model fully in frustum */
    float dist=max_dim*1.2f+30.0f;
    /* Transparent background — clear color pixels, keep GL depth buffer reset */
    memset(s_fb,0,sizeof(s_fb));
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    /* Projection — fairly tight FOV so model fills the 21:9 frame */
    glViewport(0,0,SW,SH);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    float aspect=(float)SW/(float)SH;
    float near_p=1.0f, far_p=dist*4.0f;
    float top = near_p * tanf(30.0f * 3.14159265f / 180.0f);
    glFrustum(-top*aspect, top*aspect, -top, top, near_p, far_p);
    /* Modelview: standard Quake camera axes, eye at -dist along world +X */
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glRotatef(-90.0f,1.0f,0.0f,0.0f); /* GL Y-up → Quake Z-up           */
    glRotatef( 90.0f,0.0f,0.0f,1.0f); /* GL +Z view → Quake +X view     */
    glRotatef(-10.0f,0.0f,1.0f,0.0f); /* gentle 10° pitch down           */
    glTranslatef(dist,0.0f,0.0f);     /* move eye to -dist along +X      */
    /* Shift model centroid to world origin so the model faces the camera  */
    glTranslatef(-cx,-cy,-cz);
    /* Draw at world origin with requested yaw */
    float origin[3]={0.0f,0.0f,0.0f},angles[3]={0.0f,yaw,0.0f};
    R_DrawAliasModel(mi,fr,origin,angles,1.0f);
    dump_raw(raw_out);
}

int main(void){
    if(!FS_Init()){printf("pak0.pak not found\n");return 1;}
    gl_init(GL_BACKEND_VIRGL,SW,SH,s_fb);
    if(!g_gl){printf("no GL backend\n");return 1;}
    TEX_LoadPalette("gfx/palette.lmp");
    UI_Init();
    printf("=== Quake 21:9 Screenshot Pipeline (%dx%d) ===\n",SW,SH);

    /* ---- Maps ---- */
    printf("--- Episode 1 Maps ---\n");
    static const struct{const char*map;float fwd,side,vert,extra_yaw;}maps[]={
        {"maps/start.bsp",20, 0, 8,  0},
        {"maps/e1m1.bsp", 80,20, 8, 15},
        {"maps/e1m2.bsp", 80, 0, 8,  0},
        {"maps/e1m3.bsp", 80, 0, 8,  0},
        {"maps/e1m4.bsp", 80,20, 8,-15},
        {"maps/e1m5.bsp", 80, 0, 8,  0},
        {"maps/e1m6.bsp", 80,20, 8,  0},
        {"maps/e1m7.bsp", 80, 0, 8, 20},
        {"maps/e1m8.bsp", 80, 0, 8,-20},
    };
    static const char*map_raws[]={
        "/tmp/btron_raw_screens/Quake_Start.raw",
        "/tmp/btron_raw_screens/Quake_E1M1.raw",
        "/tmp/btron_raw_screens/Quake_E1M2.raw",
        "/tmp/btron_raw_screens/Quake_E1M3.raw",
        "/tmp/btron_raw_screens/Quake_E1M4.raw",
        "/tmp/btron_raw_screens/Quake_E1M5.raw",
        "/tmp/btron_raw_screens/Quake_E1M6.raw",
        "/tmp/btron_raw_screens/Quake_E1M7.raw",
        "/tmp/btron_raw_screens/Quake_E1M8.raw",
    };
    for(int i=0;i<9;i++){
        if(!World_ChangeMap(maps[i].map)){printf("  [SKIP] %s\n",maps[i].map);continue;}
        float sp_yaw = g_world.spawn_angle + maps[i].extra_yaw;
        float rad = sp_yaw * (3.14159265f / 180.0f);
        float eye[3]={
            g_world.spawn_origin[0]+maps[i].fwd*cosf(rad)-maps[i].side*sinf(rad),
            g_world.spawn_origin[1]+maps[i].fwd*sinf(rad)+maps[i].side*cosf(rad),
            g_world.spawn_origin[2]+maps[i].vert+22.0f
        };
        capture_level(maps[i].map,eye,sp_yaw,map_raws[i]);
    }

    /* ---- Enemies ---- */


    printf("--- Enemies ---\n");
    World_ChangeMap("maps/e1m1.bsp");
    static const struct{const char*path;int frame;float yaw;const char*out;}enemies[]={
        {"progs/dog.mdl",     0,130,"/tmp/btron_raw_screens/Quake_Enemy_Dog.raw"},
        {"progs/soldier.mdl", 0,140,"/tmp/btron_raw_screens/Quake_Enemy_Soldier.raw"},
        {"progs/knight.mdl",  0,125,"/tmp/btron_raw_screens/Quake_Enemy_Knight.raw"},
        {"progs/ogre.mdl",    0,135,"/tmp/btron_raw_screens/Quake_Enemy_Ogre.raw"},
        {"progs/zombie.mdl",  0,120,"/tmp/btron_raw_screens/Quake_Enemy_Zombie.raw"},
        {"progs/wizard.mdl",  0,140,"/tmp/btron_raw_screens/Quake_Enemy_Wizard.raw"},
        {"progs/demon.mdl",   0,130,"/tmp/btron_raw_screens/Quake_Enemy_Demon.raw"},
        {"progs/shambler.mdl",0,135,"/tmp/btron_raw_screens/Quake_Enemy_Shambler.raw"},
        {"progs/boss.mdl",    0,120,"/tmp/btron_raw_screens/Quake_Enemy_Chthon.raw"},
    };
    for(int i=0;i<9;i++){g_num_alias_models=0;capture_model(enemies[i].path,enemies[i].frame,enemies[i].yaw,enemies[i].out);}

    /* ---- Artifacts ---- */
    printf("--- Artifacts ---\n");
    static const struct{const char*path;int frame;float yaw;const char*out;}artifacts[]={
        {"progs/quaddama.mdl",0,30,"/tmp/btron_raw_screens/Quake_Art_Quad.raw"},
        {"progs/invulner.mdl",0,30,"/tmp/btron_raw_screens/Quake_Art_Invulner.raw"},
        {"progs/invisibl.mdl",0,30,"/tmp/btron_raw_screens/Quake_Art_Invisible.raw"},
        {"progs/suit.mdl",    0,30,"/tmp/btron_raw_screens/Quake_Art_Suit.raw"},
        {"progs/w_s_key.mdl", 0,30,"/tmp/btron_raw_screens/Quake_Art_SilverKey.raw"},
        {"progs/w_g_key.mdl", 0,30,"/tmp/btron_raw_screens/Quake_Art_GoldKey.raw"},
        {"progs/end1.mdl",    0,30,"/tmp/btron_raw_screens/Quake_Art_Rune.raw"},
        {"progs/armor.mdl",   0,30,"/tmp/btron_raw_screens/Quake_Art_Armor.raw"},
    };
    for(int i=0;i<8;i++){g_num_alias_models=0;capture_model(artifacts[i].path,artifacts[i].frame,artifacts[i].yaw,artifacts[i].out);}

    /* ---- Detailing ---- */
    printf("--- Detailing Objects ---\n");
    static const struct{const char*path;int frame;float yaw;const char*out;}details[]={
        {"progs/flame.mdl",   0,30,"/tmp/btron_raw_screens/Quake_Det_Flame.raw"},
        {"progs/flame2.mdl",  0,30,"/tmp/btron_raw_screens/Quake_Det_Flame2.raw"},
        {"progs/lavaball.mdl",0,30,"/tmp/btron_raw_screens/Quake_Det_Lavaball.raw"},
        {"progs/s_light.mdl", 0,30,"/tmp/btron_raw_screens/Quake_Det_SLight.raw"},
        {"progs/g_light.mdl", 0,30,"/tmp/btron_raw_screens/Quake_Det_GLight.raw"},
        {"progs/backpack.mdl",0,30,"/tmp/btron_raw_screens/Quake_Det_Backpack.raw"},
        {"progs/spike.mdl",   0,30,"/tmp/btron_raw_screens/Quake_Det_Spike.raw"},
    };
    for(int i=0;i<7;i++){g_num_alias_models=0;capture_model(details[i].path,details[i].frame,details[i].yaw,details[i].out);}

    printf("=== Done ===\n");
    gl_shutdown(); return 0;
}
