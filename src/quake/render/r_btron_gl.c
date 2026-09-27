/*
 * src/quake/render/r_btron_gl.c — OpenGL ES 1.1 Renderer Backend for Quake
 *
 * Implements 2D UI and 3D scene rasterization using B-System's src/gl/
 * dispatch table (TinyGL softpipe and VirtIO-GPU 3D virgl).
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#include "../include/quakedef.h"
#include "../include/render.h"
#include "../include/draw.h"
#include "../include/world.h"
#include "../include/r_brush.h"
#include "../include/texture.h"
#include "../include/r_light.h"
#include "../include/r_surf.h"
#include "../include/r_alias.h"
#include "../include/progs.h"
#include "../include/server.h"
#include "../include/fs_btron.h"
#include "../include/r_part.h"
#include "../include/mathlib.h"
#include "../include/quake_ui.h"
#include "../../gl/gl_dispatch.h"

refdef_t r_refdef;

static int s_vid_width  = 640;
static int s_vid_height = 480;
static float s_anim_angle = 0.0f;
static int s_v_shot_idx = -1;

/* 8x8 Standard Console Font (ASCII 32..127) */
static const uint8_t s_font8x8[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* Space */
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, /* ! */
    {0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00}, /* " */
    {0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00}, /* # */
    {0x18,0x3E,0x60,0x3C,0x06,0x7C,0x18,0x00}, /* $ */
    {0x00,0x63,0x66,0x0C,0x18,0x33,0x63,0x00}, /* % */
    {0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00}, /* & */
    {0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00}, /* ' */
    {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00}, /* ( */
    {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00}, /* ) */
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, /* * */
    {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00}, /* + */
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}, /* , */
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}, /* - */
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, /* . */
    {0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00}, /* / */
    {0x7C,0xC6,0xCE,0xD6,0xE6,0xC6,0x7C,0x00}, /* 0 */
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00}, /* 1 */
    {0x7C,0xC6,0x06,0x1C,0x30,0x60,0xFE,0x00}, /* 2 */
    {0x7C,0xC6,0x06,0x3C,0x06,0xC6,0x7C,0x00}, /* 3 */
    {0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x1E,0x00}, /* 4 */
    {0xFE,0xC0,0xFC,0x06,0x06,0xC6,0x7C,0x00}, /* 5 */
    {0x78,0x0C,0x06,0x7C,0xC6,0xC6,0x7C,0x00}, /* 6 */
    {0xFE,0xC6,0x0C,0x18,0x30,0x30,0x30,0x00}, /* 7 */
    {0x7C,0xC6,0xC6,0x7C,0xC6,0xC6,0x7C,0x00}, /* 8 */
    {0x7C,0xC6,0xC6,0x7E,0x06,0x0C,0x78,0x00}, /* 9 */
    {0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00}, /* : */
    {0x00,0x18,0x18,0x00,0x18,0x18,0x30,0x00}, /* ; */
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, /* < */
    {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00}, /* = */
    {0x60,0x30,0x18,0x0C,0x18,0x30,0x60,0x00}, /* > */
    {0x7C,0xC6,0x0C,0x18,0x18,0x00,0x18,0x00}, /* ? */
    {0x7C,0xC6,0xDE,0xDE,0xDC,0xC0,0x7C,0x00}, /* @ */
    {0x38,0x6C,0xC6,0xFE,0xC6,0xC6,0xC6,0x00}, /* A */
    {0xFC,0x66,0x66,0x7C,0x66,0x66,0xFC,0x00}, /* B */
    {0x3C,0x66,0xC0,0xC0,0xC0,0x66,0x3C,0x00}, /* C */
    {0xF8,0x6C,0x66,0x66,0x66,0x6C,0xF8,0x00}, /* D */
    {0xFE,0x62,0x68,0x78,0x68,0x62,0xFE,0x00}, /* E */
    {0xFE,0x62,0x68,0x78,0x68,0x60,0xF0,0x00}, /* F */
    {0x3C,0x66,0xC0,0xC0,0xCE,0x66,0x3E,0x00}, /* G */
    {0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x00}, /* H */
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x7E,0x00}, /* I */
    {0x1E,0x0C,0x0C,0x0C,0xCC,0xCC,0x78,0x00}, /* J */
    {0xE6,0x66,0x6C,0x78,0x6C,0x66,0xE6,0x00}, /* K */
    {0xF0,0x60,0x60,0x60,0x62,0x66,0xFE,0x00}, /* L */
    {0xC6,0xEE,0xFE,0xFE,0xD6,0xC6,0xC6,0x00}, /* M */
    {0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0x00}, /* N */
    {0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00}, /* O */
    {0xFC,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00}, /* P */
    {0x7C,0xC6,0xC6,0xC6,0xD6,0x7C,0x0E,0x00}, /* Q */
    {0xFC,0x66,0x66,0x7C,0x6C,0x66,0xE6,0x00}, /* R */
    {0x7C,0xC6,0x60,0x38,0x0C,0xC6,0x7C,0x00}, /* S */
    {0x7E,0x7E,0x5A,0x18,0x18,0x18,0x3C,0x00}, /* T */
    {0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00}, /* U */
    {0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00}, /* V */
    {0xC6,0xC6,0xD6,0xFE,0xEE,0xC6,0xC6,0x00}, /* W */
    {0xC6,0xC6,0x6C,0x38,0x6C,0xC6,0xC6,0x00}, /* X */
    {0x66,0x66,0x66,0x3C,0x18,0x18,0x3C,0x00}, /* Y */
    {0xFE,0xC6,0x8C,0x18,0x32,0x66,0xFE,0x00}, /* Z */
    {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00}, /* [ */
    {0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00}, /* \ */
    {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00}, /* ] */
    {0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00}, /* ^ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00}, /* _ */
    {0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00}, /* ` */
    {0x00,0x00,0x78,0x0C,0x7C,0xCC,0x76,0x00}, /* a */
    {0xE0,0x60,0x7C,0x66,0x66,0x66,0x7C,0x00}, /* b */
    {0x00,0x00,0x3C,0x66,0xC0,0x66,0x3C,0x00}, /* c */
    {0x1C,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00}, /* d */
    {0x00,0x00,0x7C,0xC6,0xFE,0xC0,0x7C,0x00}, /* e */
    {0x38,0x6C,0x60,0xF0,0x60,0x60,0xF0,0x00}, /* f */
    {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0xF8}, /* g */
    {0xE0,0x60,0x6C,0x76,0x66,0x66,0xE6,0x00}, /* h */
    {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00}, /* i */
    {0x0C,0x00,0x1C,0x0C,0x0C,0xCC,0xCC,0x78}, /* j */
    {0xE0,0x60,0x66,0x6C,0x78,0x6C,0xE6,0x00}, /* k */
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, /* l */
    {0x00,0x00,0xEC,0xFE,0xD6,0xD6,0xD6,0x00}, /* m */
    {0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x00}, /* n */
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0x00}, /* o */
    {0x00,0x00,0xDC,0x66,0x66,0x7C,0x60,0xF0}, /* p */
    {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x1E}, /* q */
    {0x00,0x00,0xDC,0x76,0x60,0x60,0xF0,0x00}, /* r */
    {0x00,0x00,0x7C,0xC0,0x7C,0x06,0xFC,0x00}, /* s */
    {0x30,0x30,0xFC,0x30,0x30,0x34,0x18,0x00}, /* t */
    {0x00,0x00,0xCC,0xCC,0xCC,0xCC,0x76,0x00}, /* u */
    {0x00,0x00,0xC6,0xC6,0xC6,0x6C,0x38,0x00}, /* v */
    {0x00,0x00,0xC6,0xD6,0xFE,0xEE,0x6C,0x00}, /* w */
    {0x00,0x00,0xC6,0x6C,0x38,0x6C,0xC6,0x00}, /* x */
    {0x00,0x00,0xC6,0xC6,0xC6,0x7E,0x06,0xFC}, /* y */
    {0x00,0x00,0xFE,0x8C,0x18,0x32,0xFE,0x00}, /* z */
    {0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00}, /* { */
    {0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, /* | */
    {0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00}, /* } */
    {0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00}, /* ~ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}  /* DEL */
};

void R_Init(int width, int height) {
    s_vid_width  = width;
    s_vid_height = height;

    R_InitBrushRenderer();

    /* Phase 3: Initialise texture system — load Quake palette first */
    TEX_Init();

    if (!g_world.is_loaded) {
        if (!World_LoadMap("maps/e1m1.bsp")) {
            World_LoadMap("maps/start.bsp");
        }
    }

    /* Upload BSP miptex textures — use pre-cached lump pointer (zero I/O) */
    if (g_world.is_loaded && g_num_gl_textures == 0 && g_bsp_cache.is_ready) {
        const byte *tex_lump = g_bsp_cache.lumps[LUMP_TEXTURES];
        int         tex_len  = g_bsp_cache.lump_lens[LUMP_TEXTURES];
        if (tex_lump && tex_len > 0) {
            TEX_LoadBSPTextures(tex_lump, tex_len);
        }
    }

    if (g_world.is_loaded) {
        VectorCopy(g_world.spawn_origin, r_refdef.vieworg);
        r_refdef.vieworg[2] += 22.0f; /* Player eye height */
        r_refdef.viewangles[0] = 0.0f;
        r_refdef.viewangles[1] = g_world.spawn_angle;
        r_refdef.viewangles[2] = 0.0f;
    } else {
        VectorClear(r_refdef.vieworg);
        VectorClear(r_refdef.viewangles);
        r_refdef.vieworg[2] = 22.0f; /* Standing height */
    }

    /* Preload first-person shotgun viewmodel */
    if (s_v_shot_idx < 0) {
        s_v_shot_idx = R_LoadAliasModel("progs/v_shot.mdl");
    }

    r_refdef.fov_x = 90.0f;
    r_refdef.fov_y = 70.0f;
}

/* Viewport-only resize — no engine reinit, no texture reload */
void R_Resize(int width, int height) {
    s_vid_width  = width;
    s_vid_height = height;
    r_refdef.fov_x = 90.0f;
    r_refdef.fov_y = (float)height / (float)width * 90.0f;
}

void Draw_Init(void) {
    /* Ready for 2D UI */
}

void Draw_Fill(int x, int y, int w, int h, int color) {
    if (!g_gl) return;

    float r = (float)((color >> 16) & 0xFF) / 255.0f;
    float g = (float)((color >> 8) & 0xFF) / 255.0f;
    float b = (float)(color & 0xFF) / 255.0f;

    /* Map screen coordinates into [-1, 1] NDC */
    float x0 = (2.0f * (float)x / (float)s_vid_width) - 1.0f;
    float y0 = 1.0f - (2.0f * (float)y / (float)s_vid_height);
    float x1 = (2.0f * (float)(x + w) / (float)s_vid_width) - 1.0f;
    float y1 = 1.0f - (2.0f * (float)(y + h) / (float)s_vid_height);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glColor3f(r, g, b);

    glBegin(GL_QUADS);
    glVertex3f(x0, y0, 0.0f);
    glVertex3f(x1, y0, 0.0f);
    glVertex3f(x1, y1, 0.0f);
    glVertex3f(x0, y1, 0.0f);
    glEnd();

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

void Draw_Character(int x, int y, int num) {
    if (!g_gl) return;
    if (num < 32 || num > 127) num = '?';
    int glyph = num - 32;

    float px_w = 2.0f / (float)s_vid_width;
    float px_h = 2.0f / (float)s_vid_height;

    float base_x = (2.0f * (float)x / (float)s_vid_width) - 1.0f;
    float base_y = 1.0f - (2.0f * (float)y / (float)s_vid_height);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glColor3f(0.85f, 0.70f, 0.15f); /* Classic Quake Amber / Gold */

    glBegin(GL_QUADS);
    for (int r = 0; r < 8; r++) {
        uint8_t row = s_font8x8[glyph][r];
        for (int c = 0; c < 8; c++) {
            if (row & (0x80 >> c)) {
                float rx0 = base_x + (float)c * px_w;
                float ry0 = base_y - (float)r * px_h;
                float rx1 = rx0 + px_w;
                float ry1 = ry0 - px_h;

                glVertex3f(rx0, ry0, 0.0f);
                glVertex3f(rx1, ry0, 0.0f);
                glVertex3f(rx1, ry1, 0.0f);
                glVertex3f(rx0, ry1, 0.0f);
            }
        }
    }
    glEnd();

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

void Draw_String(int x, int y, const char *str) {
    if (!str) return;
    int cur_x = x;
    while (*str) {
        Draw_Character(cur_x, y, (int)*str);
        cur_x += 8;
        str++;
    }
}

void Draw_Pic(int x, int y, qpic_t *pic) {
    if (!pic) return;
    Draw_Fill(x, y, pic->width, pic->height, 0x503020);
}

void Draw_FadeScreen(void) {
    Draw_Fill(0, 0, s_vid_width, s_vid_height, 0x200000);
}

void R_BeginFrame(void) {
    if (!g_gl) return;
    glClearColor(0.08f, 0.06f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    /* Advance lightstyle animation and turbulent surface time (~60 FPS) */
    static const float dt = 1.0f / 60.0f;
    R_AnimateLights(dt);
    R_UpdateSurfTime(dt);
    R_DecayLights();
}

/* ── 3D Scene Geometry: Slipgate Complex Entrance Hall ───────────── */
static void render_slipgate_hall(void) {
    /* Floor: Dark slate tiles */
    glColor3f(0.22f, 0.20f, 0.18f);
    glBegin(GL_QUADS);
    glNormal3f(0.0f, 0.0f, 1.0f);
    glVertex3f(-250.0f, -250.0f, 0.0f);
    glVertex3f( 250.0f, -250.0f, 0.0f);
    glVertex3f( 250.0f,  250.0f, 0.0f);
    glVertex3f(-250.0f,  250.0f, 0.0f);
    glEnd();

    /* Ceiling: Dark steel cross-beams */
    glColor3f(0.15f, 0.14f, 0.13f);
    glBegin(GL_QUADS);
    glNormal3f(0.0f, 0.0f, -1.0f);
    glVertex3f(-250.0f,  250.0f, 96.0f);
    glVertex3f( 250.0f,  250.0f, 96.0f);
    glVertex3f( 250.0f, -250.0f, 96.0f);
    glVertex3f(-250.0f, -250.0f, 96.0f);
    glEnd();

    /* Back Wall (Slipgate Arch) */
    glColor3f(0.35f, 0.28f, 0.22f);
    glBegin(GL_QUADS);
    glNormal3f(0.0f, 1.0f, 0.0f);
    glVertex3f(-250.0f, 250.0f, 0.0f);
    glVertex3f( 250.0f, 250.0f, 0.0f);
    glVertex3f( 250.0f, 250.0f, 96.0f);
    glVertex3f(-250.0f, 250.0f, 96.0f);
    glEnd();

    /* Slipgate Portal Teleporter (Glowing Cyan / Teal) */
    glColor3f(0.10f, 0.85f, 0.80f);
    glBegin(GL_QUADS);
    glNormal3f(0.0f, 1.0f, 0.0f);
    glVertex3f(-40.0f, 248.0f, 10.0f);
    glVertex3f( 40.0f, 248.0f, 10.0f);
    glVertex3f( 40.0f, 248.0f, 86.0f);
    glVertex3f(-40.0f, 248.0f, 86.0f);
    glEnd();

    /* Left Wall */
    glColor3f(0.28f, 0.24f, 0.20f);
    glBegin(GL_QUADS);
    glNormal3f(1.0f, 0.0f, 0.0f);
    glVertex3f(-250.0f, -250.0f, 0.0f);
    glVertex3f(-250.0f,  250.0f, 0.0f);
    glVertex3f(-250.0f,  250.0f, 96.0f);
    glVertex3f(-250.0f, -250.0f, 96.0f);
    glEnd();

    /* Right Wall */
    glColor3f(0.28f, 0.24f, 0.20f);
    glBegin(GL_QUADS);
    glNormal3f(-1.0f, 0.0f, 0.0f);
    glVertex3f(250.0f,  250.0f, 0.0f);
    glVertex3f(250.0f, -250.0f, 0.0f);
    glVertex3f(250.0f, -250.0f, 96.0f);
    glVertex3f(250.0f,  250.0f, 96.0f);
    glEnd();

    /* Floating Rotating Armor Item / Quad Damage in center */
    glPushMatrix();
    glTranslatef(0.0f, 100.0f, 28.0f);
    glRotatef(s_anim_angle, 0.0f, 0.0f, 1.0f);
    glRotatef(s_anim_angle * 0.7f, 1.0f, 0.0f, 0.0f);

    /* Octahedron Item */
    glColor3f(0.95f, 0.80f, 0.15f); /* Golden Armor */
    glBegin(GL_TRIANGLES);
    /* Top Pyramid */
    glVertex3f( 0.0f,  0.0f,  16.0f); glVertex3f(-12.0f, -12.0f, 0.0f); glVertex3f( 12.0f, -12.0f, 0.0f);
    glVertex3f( 0.0f,  0.0f,  16.0f); glVertex3f( 12.0f, -12.0f, 0.0f); glVertex3f( 12.0f,  12.0f, 0.0f);
    glVertex3f( 0.0f,  0.0f,  16.0f); glVertex3f( 12.0f,  12.0f, 0.0f); glVertex3f(-12.0f,  12.0f, 0.0f);
    glVertex3f( 0.0f,  0.0f,  16.0f); glVertex3f(-12.0f,  12.0f, 0.0f); glVertex3f(-12.0f, -12.0f, 0.0f);
    /* Bottom Pyramid */
    glVertex3f( 0.0f,  0.0f, -16.0f); glVertex3f( 12.0f, -12.0f, 0.0f); glVertex3f(-12.0f, -12.0f, 0.0f);
    glVertex3f( 0.0f,  0.0f, -16.0f); glVertex3f( 12.0f,  12.0f, 0.0f); glVertex3f( 12.0f, -12.0f, 0.0f);
    glVertex3f( 0.0f,  0.0f, -16.0f); glVertex3f(-12.0f,  12.0f, 0.0f); glVertex3f( 12.0f,  12.0f, 0.0f);
    glVertex3f( 0.0f,  0.0f, -16.0f); glVertex3f(-12.0f, -12.0f, 0.0f); glVertex3f(-12.0f,  12.0f, 0.0f);
    glEnd();

    glPopMatrix();
}

void R_RenderView(void) {
    if (!g_gl) return;

    s_anim_angle += 1.5f;
    if (s_anim_angle >= 360.0f) s_anim_angle -= 360.0f;

    /* ── Perspective 3D Camera Setup ──────────────────────────────── */
    glViewport(0, 0, s_vid_width, s_vid_height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    float aspect = (float)s_vid_width / (float)s_vid_height;
    glFrustum(-0.1f * aspect, 0.1f * aspect, -0.1f, 0.1f, 0.1f, 1000.0f);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    /* Quake coordinate system: X forward, Y left, Z up */
    glRotatef(-90.0f, 1.0f, 0.0f, 0.0f); /* Z-up to Y-up */
    glRotatef(90.0f, 0.0f, 0.0f, 1.0f);
    glRotatef(-r_refdef.viewangles[2], 1.0f, 0.0f, 0.0f); /* Roll */
    glRotatef(-r_refdef.viewangles[0], 0.0f, 1.0f, 0.0f); /* Pitch */
    glRotatef(-r_refdef.viewangles[1], 0.0f, 0.0f, 1.0f); /* Yaw */
    glTranslatef(-r_refdef.vieworg[0], -r_refdef.vieworg[1], -r_refdef.vieworg[2]);

    /* Render 3D Environment */
    if (g_world.is_loaded) {
        R_DrawWorld();

        /* Entity pass: draw all edicts with a valid model index */
        for (int ei = 2; ei < g_prvm.num_edicts; ei++) {
            edict_t *ed = &g_prvm.edicts[ei];
            if (ed->free) continue;
            int modelindex = (int)EF(ed, F_MODELINDEX);
            if (modelindex <= 0) continue;

            float eorg[3] = { EF(ed, F_ORIGIN_X), EF(ed, F_ORIGIN_Y), EF(ed, F_ORIGIN_Z) };
            float eang[3] = { EF(ed, F_ANGLES_X), EF(ed, F_ANGLES_Y), EF(ed, F_ANGLES_Z) };

            if (modelindex >= 1000) {
                /* Brush submodel (doors, platforms, lifts, buttons) */
                int sub = modelindex - 1000;
                R_DrawBModel(sub, eorg, eang);
            } else {
                /* Alias model (weapons, armors, health, monsters) */
                int mi = modelindex - 1;
                if (mi >= 0 && mi < g_num_alias_models) {
                    float lt = R_LightForFace(NULL, eorg);
                    R_DrawAliasModel(mi, (int)EF(ed, F_FRAME), eorg, eang, lt);
                }
            }
        }

        /* Particle pass: camera-aligned billboards */
        vec3_t vforward, vright, vup;
        AngleVectors(r_refdef.viewangles, vforward, vright, vup);
        R_DrawParticles(r_refdef.vieworg, vright, vup);
    } else {
        render_slipgate_hall();
    }

    /* 2D Status Bar & HUD — live player stats */
    Draw_Fill(0, s_vid_height - 36, s_vid_width, 36, 0x1A1410);
    Draw_Fill(0, s_vid_height - 37, s_vid_width, 1, 0x4A3828);
    {
        char hbuf[48];
        int hp  = (g_prvm.num_edicts >= 2) ? (int)EF(&g_prvm.edicts[1], F_HEALTH)      : 100;
        int amm = (g_prvm.num_edicts >= 2) ? (int)EF(&g_prvm.edicts[1], F_CURRENTAMMO) : 25;
        int i = 0; const char *fmt;
        /* "ARMOR: 0" */
        fmt = "ARMOR: "; while(*fmt) hbuf[i++]=*fmt++;
        int a=0, tmp=a; if(tmp==0){hbuf[i++]='0';} else{ char d[8]; int di=0; while(tmp){d[di++]=(char)('0'+tmp%10);tmp/=10;} while(di--)hbuf[i++]=d[di+1];} hbuf[i]=0;
        Draw_String(16, s_vid_height - 28, hbuf);
        /* "HEALTH: N" */
        i=0; fmt="HEALTH: "; while(*fmt) hbuf[i++]=*fmt++;
        tmp=hp>0?hp:0; if(tmp==0){hbuf[i++]='0';} else{char d[8];int di=0;while(tmp){d[di++]=(char)('0'+tmp%10);tmp/=10;}while(di--)hbuf[i++]=d[di+1];} hbuf[i]=0;
        Draw_String(140, s_vid_height - 28, hbuf);
        /* "AMMO: N" */
        i=0; fmt="AMMO: "; while(*fmt) hbuf[i++]=*fmt++;
        tmp=amm>0?amm:0; if(tmp==0){hbuf[i++]='0';} else{char d[8];int di=0;while(tmp){d[di++]=(char)('0'+tmp%10);tmp/=10;}while(di--)hbuf[i++]=d[di+1];} hbuf[i]=0;
        Draw_String(270, s_vid_height - 28, hbuf);
    }

    char map_tag[32];
    if (g_world.is_loaded) {
        /* Format map basename in uppercase */
        const char *mname = g_world.name;
        const char *slash = q_strrchr(mname, '/');
        if (slash) mname = slash + 1;
        int i = 0;
        for (; i < 15 && mname[i] && mname[i] != '.'; i++) {
            char c = mname[i];
            if (c >= 'a' && c <= 'z') c -= ('a' - 'A');
            map_tag[i] = c;
        }
        map_tag[i] = '\0';
    } else {
        strncpy(map_tag, "SLIPGATE", sizeof(map_tag));
    }
    Draw_String(s_vid_width - 150, s_vid_height - 28, map_tag);

    /* Crosshair */
    int cx = s_vid_width / 2;
    int cy = (s_vid_height - 36) / 2;
    Draw_Fill(cx - 3, cy, 7, 1, 0xFFCC00);
    Draw_Fill(cx, cy - 3, 1, 7, 0xFFCC00);

    /* Engine Watermark & Top HUD Controls */
    Draw_String(12, 12, "QUAKE BTRON 3D (OpenGL ES 1.1 / VirtIO-GPU)");
    Draw_Fill(s_vid_width - 180, 8, 82, 18, g_menu_active ? 0x8C2020 : 0x2A2420);
    Draw_String(s_vid_width - 175, 13, "[ESC] MENU");
    Draw_Fill(s_vid_width - 92, 8, 86, 18, g_console_active ? 0x8C2020 : 0x2A2420);
    Draw_String(s_vid_width - 88, 13, "[~] CONSOLE");

    /* First-Person Viewmodel (drawn in camera space) */
    if (s_v_shot_idx >= 0 && g_world.is_loaded && !g_menu_active && !g_console_active) {
        /* Clear depth buffer so viewmodel is never clipped/occluded by nearby world geometry */
        glClear(GL_DEPTH_BUFFER_BIT);

        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        float aspect = (float)s_vid_width / (float)s_vid_height;
        glFrustum(-0.06f * aspect, 0.06f * aspect, -0.06f, 0.06f, 0.1f, 50.0f);

        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        float spd = (g_prvm.num_edicts >= 2) ? sqrtf(EF(&g_prvm.edicts[1], F_VELOCITY_X)*EF(&g_prvm.edicts[1], F_VELOCITY_X) +
                                                     EF(&g_prvm.edicts[1], F_VELOCITY_Y)*EF(&g_prvm.edicts[1], F_VELOCITY_Y)) : 0.0f;
        static float s_gun_bob = 0.0f;
        s_gun_bob += spd > 10.0f ? 0.06f : 0.01f;
        float bob_x = sinf(s_gun_bob * 2.0f) * (spd > 10.0f ? 0.15f : 0.02f);
        float bob_y = -fabsf(cosf(s_gun_bob * 2.0f)) * (spd > 10.0f ? 0.12f : 0.01f);

        glTranslatef(1.2f + bob_x, -1.0f + bob_y, -3.2f);
        glRotatef(-90.0f, 0.0f, 1.0f, 0.0f);
        glRotatef(90.0f, 1.0f, 0.0f, 0.0f);

        int gun_fr = Player_GetGunFrame();
        if (g_prvm.num_edicts >= 2 && EF(&g_prvm.edicts[1], F_WEAPONFRAME) > 0.0f) {
            gun_fr = (int)EF(&g_prvm.edicts[1], F_WEAPONFRAME);
        }

        int active_vmodel = s_v_shot_idx;
        if (g_prvm.num_edicts >= 2) {
            const char *wm = PR_GetString((int)EF(&g_prvm.edicts[1], F_WEAPONMODEL));
            if (wm && wm[0]) {
                int mi = R_LoadAliasModel(wm);
                if (mi >= 0) active_vmodel = mi;
            }
        }

        float gun_lt = R_LightForFace(NULL, r_refdef.vieworg) * 1.3f;
        if (gun_lt < 0.45f) gun_lt = 0.45f;
        if (gun_lt > 1.0f) gun_lt = 1.0f;

        R_DrawAliasModel(active_vmodel, gun_fr, (float[]){0,0,0}, (float[]){0,0,0}, gun_lt);

        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
    }

    /* Menu & Developer Console Overlay */
    UI_Draw(s_vid_width, s_vid_height);
}

void R_EndFrame(void) {
    /* Presentation buffer swap is cleanly handled by egl_swap_buffers */
}
