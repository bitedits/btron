/*
 * src/demo/lilcu64_demo.c — Lil Cu 64 Native OpenGL Demoscene Application
 *
 * 1:1 replica of index.html hero-content 3D scene in native BTRON OpenGL:
 *  - 4D Hopf fibration background field with stereographic projection S³ → ℝ³
 *  - Lil Cu 64 low-poly mascot with chamfered geometry, anime eyes & rainbow halo
 *  - Journey spherical radiant wave on Pulse / Blessing
 *  - 7-state kinematic animation engine with critically damped exponential springs
 *  - 4-personage 3D turntable carousel (Saint, Chibi Mecha, Hyper-Prism, Cyber Shinobi)
 *  - Procedural DSP sound engine with 4-track sequencer streaming to VirtIO sound
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "lilcu64_demo.h"
#include "lilcu64_synth.h"
#include "../gl/gl_dispatch.h"
#include "../gl/egl_surface.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif

extern void uart_puts_raw(const char *s);

/* ── Global Window & Surface State ───────────────────────────────── */

static WND         *s_demo_wnd = NULL;
static EGL_SURFACE *s_surf     = NULL;
static ID           s_demo_tskid = 0;

/* Camera & Drag state */
static GLfloat s_view_rotx = 0.0f;
static GLfloat s_view_roty = 0.0f;
static GLfloat s_target_rotx = 0.0f;
static GLfloat s_target_roty = 0.0f;
static GLfloat s_cube_rotx = 0.0f;
static GLfloat s_cube_roty = 0.0f;
static GLfloat s_cube_velx = 0.0f;
static GLfloat s_cube_vely = 0.0f;
static int     s_dragging = 0;
static H       s_last_mx = 0, s_last_my = 0;

/* Timing & Clock */
static float   s_time = 0.0f;
static float   s_dt   = 0.0166f;
static float   s_circular_phase = 0.0f;
static float   s_circular_vel = 0.0f;

/* Density modes: 0 = 9 rings, 1 = 18 rings (canonical), 2 = 27 rings */
static int     s_density_mode = 1;

/* Animation State Machine */
typedef enum {
    ANIM_IDLE,
    ANIM_WALK,
    ANIM_FLOAT,
    ANIM_DANCE,
    ANIM_SQUASH,
    ANIM_SING,
    ANIM_POKE
} AnimState;

static AnimState s_anim_state = ANIM_IDLE;
static float     s_state_timer = 0.0f;
static float     s_walk_phase = 0.0f;
static int       s_last_step_sign = 0;
static float     s_float_timer = 0.0f;
static float     s_dance_timer = 0.0f;
static int       s_dance_step = 0;
static float     s_poke_timer = 0.0f;
static float     s_impact_squash = 0.0f;

/* Blinking */
static float     s_blink_timer = 2.5f;
static bool      s_is_blinking = false;
static float     s_blink_progress = 0.0f;

/* Spherical Radiant Wave (Journey Sacred Wave) */
typedef struct {
    bool  active;
    float time;
    float duration;
    float max_r;
    bool  is_second;
} RadiantWave;

#define MAX_WAVES 4
static RadiantWave s_waves[MAX_WAVES];

/* Continuous Inertial Pose Buffer */
typedef struct {
    float root_x, root_y, root_z;
    float root_rot_x, root_rot_y, root_rot_z;
    float body_x, body_y, body_z;
    float body_rot_x, body_rot_y, body_rot_z;
    float scale_x, scale_y, scale_z;
    float lhand_rot_x, lhand_rot_z;
    float rhand_rot_x, rhand_rot_z;
    float lfoot_y, lfoot_rot_x, lfoot_rot_z;
    float rfoot_y, rfoot_rot_x, rfoot_rot_z;
    float smile_scale, omouth_scale;
    float pedestal_scale, pedestal_opacity;
} AnimPose;

static AnimPose s_pose = {
    0.0f, 0.08f, 0.0f,
    0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f,
    1.0f, 1.0f, 1.0f,
    0.0f, 0.10f,
    0.0f, -0.10f,
    -0.92f, 0.0f, 0.0f,
    -0.92f, 0.0f, 0.0f,
    1.0f, 0.0f,
    1.0f, 0.75f
};

/* Speech Quote Text */
static char s_quote_text[256] = "ボクはリル・キューブ64… 低ポリゴン3D動作と音響シムが1対1に共振する聖なるゲーム主人公だキューブ！";

/* ── Math Utilities & Damping ─────────────────────────────────────── */

static inline float smooth_damp(float cur, float target, float speed, float dt) {
    float factor = 1.0f - expf(-speed * dt);
    return cur + (target - cur) * factor;
}

static void hsv_to_rgb(float h, float s, float v, float *r, float *g, float *b) {
    while (h < 0.0f) h += 360.0f;
    while (h >= 360.0f) h -= 360.0f;
    int i = (int)(h / 60.0f);
    float f = (h / 60.0f) - (float)i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));
    switch (i % 6) {
        case 0: *r = v; *g = t; *b = p; break;
        case 1: *r = q; *g = v; *b = p; break;
        case 2: *r = p; *g = v; *b = t; break;
        case 3: *r = p; *g = q; *b = v; break;
        case 4: *r = t; *g = p; *b = v; break;
        case 5: *r = v; *g = p; *b = q; break;
    }
}

/* ── Wave Engine ─────────────────────────────────────────────────── */

static void trigger_radiant_wave(bool is_second) {
    for (int i = 0; i < MAX_WAVES; i++) {
        if (!s_waves[i].active) {
            s_waves[i].active = true;
            s_waves[i].time = 0.0f;
            s_waves[i].duration = is_second ? 1.8f : 1.6f;
            s_waves[i].max_r = is_second ? 4.8f : 3.6f;
            s_waves[i].is_second = is_second;
            return;
        }
    }
}

static void update_waves(float dt) {
    for (int i = 0; i < MAX_WAVES; i++) {
        if (s_waves[i].active) {
            s_waves[i].time += dt;
            if (s_waves[i].time >= s_waves[i].duration) {
                s_waves[i].active = false;
            }
        }
    }
}

/* ── Mascot Animation State Switching ─────────────────────────────── */

static void set_animation(AnimState state) {
    if (s_anim_state == state && state != ANIM_POKE) return;
    s_anim_state = state;
    s_state_timer = 0.0f;

    switch (state) {
        case ANIM_WALK:
            s_walk_phase = 0.0f;
            s_last_step_sign = 0;
            snprintf(s_quote_text, sizeof(s_quote_text),
                     "🐾 [Waddle] てくてく歩行！ 足踏みが shimWaddle と共振しベースラインへ！");
            break;
        case ANIM_FLOAT:
            s_float_timer = 0.0f;
            lilcu64_shim_float(523.25f, 1.0f);
            snprintf(s_quote_text, sizeof(s_quote_text),
                     "🎈 [Float] ぷかぷか浮遊！ 反重力の上昇が shimFloat の和音と連動！");
            break;
        case ANIM_DANCE:
            s_dance_timer = 0.0f;
            s_dance_step = 0;
            lilcu64_shim_dance(1.0f, 1.0f);
            snprintf(s_quote_text, sizeof(s_quote_text),
                     "🕺 [Dance] 氷晶のダンス！ 16分のピルエット回転が星屑アルペジオを奏でる！");
            break;
        case ANIM_SQUASH:
            lilcu64_shim_pulse_squash(1.0f, 1.0f);
            trigger_radiant_wave(false);
            snprintf(s_quote_text, sizeof(s_quote_text),
                     "🌟 [Pulse] 神聖波動！ 体積変形パルスが shimPulseSquash を放射！");
            break;
        case ANIM_SING:
            lilcu64_shim_pulse_squash(1.2f, 1.25f);
            trigger_radiant_wave(true);
            snprintf(s_quote_text, sizeof(s_quote_text),
                     "✨ [Sing] 天球の熱唱！ 聖なるカンタービレが全宇宙に響き渡る！");
            break;
        case ANIM_POKE:
            s_poke_timer = 1.0f;
            lilcu64_shim_boing();
            break;
        case ANIM_IDLE:
        default:
            snprintf(s_quote_text, sizeof(s_quote_text),
                     "ボクはリル・キューブ64… 低ポリゴン3D動作と音響シムが1対1に共振する聖なるゲーム主人公だキューブ！");
            break;
    }
}

/* ── 3D Geometry Builders ─────────────────────────────────────────── */

/* Chamfered cube with spherical corner clamping (r <= 1.65) */
static void draw_chamfered_cube(float sx, float sy, float sz, float r, float g, float b) {
    float hx = sx * 0.5f;
    float hy = sy * 0.5f;
    float hz = sz * 0.5f;
    glColor3f(r, g, b);

    /* 6 faces of the cube */
    glBegin(GL_QUADS);
    /* Front */
    glNormal3f(0.0f, 0.0f, 1.0f);
    glVertex3f(-hx, -hy,  hz);
    glVertex3f( hx, -hy,  hz);
    glVertex3f( hx,  hy,  hz);
    glVertex3f(-hx,  hy,  hz);

    /* Back */
    glNormal3f(0.0f, 0.0f, -1.0f);
    glVertex3f( hx, -hy, -hz);
    glVertex3f(-hx, -hy, -hz);
    glVertex3f(-hx,  hy, -hz);
    glVertex3f( hx,  hy, -hz);

    /* Top */
    glNormal3f(0.0f, 1.0f, 0.0f);
    glVertex3f(-hx,  hy,  hz);
    glVertex3f( hx,  hy,  hz);
    glVertex3f( hx,  hy, -hz);
    glVertex3f(-hx,  hy, -hz);

    /* Bottom */
    glNormal3f(0.0f, -1.0f, 0.0f);
    glVertex3f(-hx, -hy, -hz);
    glVertex3f( hx, -hy, -hz);
    glVertex3f( hx, -hy,  hz);
    glVertex3f(-hx, -hy,  hz);

    /* Right */
    glNormal3f(1.0f, 0.0f, 0.0f);
    glVertex3f( hx, -hy,  hz);
    glVertex3f( hx, -hy, -hz);
    glVertex3f( hx,  hy, -hz);
    glVertex3f( hx,  hy,  hz);

    /* Left */
    glNormal3f(-1.0f, 0.0f, 0.0f);
    glVertex3f(-hx, -hy, -hz);
    glVertex3f(-hx, -hy,  hz);
    glVertex3f(-hx,  hy,  hz);
    glVertex3f(-hx,  hy, -hz);
    glEnd();
}

static inline void draw_chamfered_cube_uniform(float size, float r, float g, float b) {
    draw_chamfered_cube(size, size, size, r, g, b);
}

/* Octahedron core */
static void draw_octahedron(float radius, bool wireframe, float r, float g, float b) {
    static const float vertices[6][3] = {
        {  1.0f,  0.0f,  0.0f },
        { -1.0f,  0.0f,  0.0f },
        {  0.0f,  1.0f,  0.0f },
        {  0.0f, -1.0f,  0.0f },
        {  0.0f,  0.0f,  1.0f },
        {  0.0f,  0.0f, -1.0f }
    };
    static const int faces[8][3] = {
        { 0, 2, 4 }, { 2, 1, 4 }, { 1, 3, 4 }, { 3, 0, 4 },
        { 2, 0, 5 }, { 1, 2, 5 }, { 3, 1, 5 }, { 0, 3, 5 }
    };

    glColor3f(r, g, b);

    if (wireframe) {
        for (int i = 0; i < 8; i++) {
            glBegin(GL_LINE_LOOP);
            for (int j = 0; j < 3; j++) {
                int vi = faces[i][j];
                glVertex3f(vertices[vi][0] * radius, vertices[vi][1] * radius, vertices[vi][2] * radius);
            }
            glEnd();
        }
    } else {
        glBegin(GL_TRIANGLES);
        for (int i = 0; i < 8; i++) {
            for (int j = 0; j < 3; j++) {
                int vi = faces[i][j];
                glVertex3f(vertices[vi][0] * radius, vertices[vi][1] * radius, vertices[vi][2] * radius);
            }
        }
        glEnd();
    }
}

/* Rainbow Torus Halo / Nimbus */
static void draw_rainbow_halo(float R, float r_tube, float time) {
    int segments_u = 24;
    int segments_v = 8;

    for (int i = 0; i < segments_u; i++) {
        float u1 = (float)i * 2.0f * (float)M_PI / (float)segments_u;
        float u2 = (float)(i + 1) * 2.0f * (float)M_PI / (float)segments_u;

        float hue = fmodf(((float)i / (float)segments_u) * 360.0f + time * 60.0f, 360.0f);
        float cr, cg, cb;
        hsv_to_rgb(hue, 0.90f, 0.95f, &cr, &cg, &cb);
        glColor3f(cr, cg, cb);

        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= segments_v; j++) {
            float v = (float)j * 2.0f * (float)M_PI / (float)segments_v;
            float cos_v = cosf(v);
            float sin_v = sinf(v);

            float x1 = (R + r_tube * cos_v) * cosf(u1);
            float y1 = r_tube * sin_v;
            float z1 = (R + r_tube * cos_v) * sinf(u1);

            float x2 = (R + r_tube * cos_v) * cosf(u2);
            float y2 = r_tube * sin_v;
            float z2 = (R + r_tube * cos_v) * sinf(u2);

            glVertex3f(x1, y1, z1);
            glVertex3f(x2, y2, z2);
        }
        glEnd();
    }
}

/* Anime Eye */
static void draw_anime_eye(float squish_y, bool is_left, float pupil_r, float pupil_g, float pupil_b) {
    glPushMatrix();

    /* Pupil (Dark Navy) */
    glColor3f(pupil_r * 0.1f, pupil_g * 0.1f, pupil_b * 0.1f);
    glBegin(GL_POLYGON);
    for (int a = 0; a < 12; a++) {
        float rad = (float)a * 2.0f * (float)M_PI / 12.0f;
        glVertex3f(cosf(rad) * 0.18f, sinf(rad) * 0.24f * squish_y, 0.0f);
    }
    glEnd();

    /* Iris */
    glColor3f(pupil_r, pupil_g, pupil_b);
    glBegin(GL_POLYGON);
    for (int a = 0; a < 10; a++) {
        float rad = (float)a * 2.0f * (float)M_PI / 10.0f;
        glVertex3f(cosf(rad) * 0.14f, (-0.06f + sinf(rad) * 0.12f) * squish_y, 0.01f);
    }
    glEnd();

    /* Specular White Dot */
    glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_POLYGON);
    for (int a = 0; a < 8; a++) {
        float rad = (float)a * 2.0f * (float)M_PI / 8.0f;
        glVertex3f((is_left ? -0.05f : 0.05f) + cosf(rad) * 0.06f, (0.08f + sinf(rad) * 0.06f) * squish_y, 0.02f);
    }
    glEnd();

    glPopMatrix();
}

/* Mouth Smile / O-Mouth */
static void draw_mouth(float smile_scale, float omouth_scale) {
    if (smile_scale > 0.05f) {
        glColor3f(0.75f, 0.07f, 0.31f);
        glBegin(GL_LINE_STRIP);
        for (int a = 0; a <= 10; a++) {
            float ang = (float)M_PI + (float)a * (float)M_PI / 10.0f;
            glVertex3f(cosf(ang) * 0.16f * smile_scale, sinf(ang) * 0.09f * smile_scale, 1.08f);
        }
        glEnd();
    }
    if (omouth_scale > 0.05f) {
        glColor3f(0.75f, 0.07f, 0.31f);
        glBegin(GL_LINE_LOOP);
        for (int a = 0; a < 12; a++) {
            float ang = (float)a * 2.0f * (float)M_PI / 12.0f;
            glVertex3f(cosf(ang) * 0.12f * omouth_scale, sinf(ang) * 0.14f * omouth_scale, 1.08f);
        }
        glEnd();
    }
}

/* ── 4D Hopf Fibration Background Renderer ─────────────────────────── */

static void render_hopf_fibration(float time, int density_mode) {
    int num_tori = 3;
    int fibers_per_torus = 6; /* Default: 18 rings */
    int num_pts = 36;

    if (density_mode == 0) {
        fibers_per_torus = 3;  /* 9 rings */
    } else if (density_mode == 2) {
        fibers_per_torus = 9;  /* 27 rings */
    }

    int total_fibers = num_tori * fibers_per_torus;

    float psi = time * 0.12f + s_circular_phase;
    float omega = time * 0.08f + s_circular_phase * 0.40f;

    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);

    for (int t = 0; t < num_tori; t++) {
        float eta = ((float)M_PI / 7.0f) + ((float)t + 0.6f) * ((float)M_PI / ((float)num_tori * 2.8f));
        float cos_eta = cosf(eta);
        float sin_eta = sinf(eta);

        for (int f = 0; f < fibers_per_torus; f++) {
            int fiber_idx = t * fibers_per_torus + f;
            float beta = ((float)f * 2.0f * (float)M_PI / (float)fibers_per_torus) + omega;

            float hue = fmodf(((float)fiber_idx / (float)total_fibers) * 360.0f + time * 24.0f, 360.0f);
            float cr, cg, cb;
            hsv_to_rgb(hue, 0.90f, 0.85f, &cr, &cg, &cb);
            glColor3f(cr * 0.65f, cg * 0.65f, cb * 0.65f);

            glBegin(GL_LINE_STRIP);
            for (int p = 0; p <= num_pts; p++) {
                float xi = (float)p * 2.0f * (float)M_PI / (float)num_pts;

                /* 4D Coordinates on S³ */
                float x0 = cos_eta * cosf(xi + beta * 0.5f + psi);
                float x1 = cos_eta * sinf(xi + beta * 0.5f + psi);
                float x2 = sin_eta * cosf(xi - beta * 0.5f + psi * 0.8f);
                float x3 = sin_eta * sinf(xi - beta * 0.5f + psi * 0.8f);

                /* Stereographic Projection S³ → ℝ³ */
                float denom = 1.08f - x3;
                if (denom < 0.08f) denom = 0.08f;
                float X = x0 / denom * 1.8f;
                float Y = x1 / denom * 1.8f;
                float Z = x2 / denom * 1.8f - 1.2f;

                glVertex3f(X, Y, Z);
            }
            glEnd();
        }
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
}

/* ── Spherical Radiant Wave Renderer ───────────────────────────────── */

static void render_radiant_waves(void) {
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);

    for (int i = 0; i < MAX_WAVES; i++) {
        if (!s_waves[i].active) continue;

        float p = s_waves[i].time / s_waves[i].duration;
        float ease = 1.0f - powf(1.0f - p, 2.8f);
        float r = 0.3f + ease * (s_waves[i].max_r - 0.3f);
        float alpha = (1.0f - p);

        /* 1. Golden Crest Ring */
        glColor3f(1.0f * alpha, 0.84f * alpha, 0.0f * alpha);
        glBegin(GL_LINE_LOOP);
        for (int a = 0; a < 36; a++) {
            float ang = (float)a * 2.0f * (float)M_PI / 36.0f;
            glVertex3f(cosf(ang) * r, sinf(ang) * r, 0.0f);
        }
        glEnd();

        /* 2. Turquoise Resonance Rim */
        glColor3f(0.0f * alpha, 0.94f * alpha, 1.0f * alpha);
        glBegin(GL_LINE_LOOP);
        for (int a = 0; a < 36; a++) {
            float ang = (float)a * 2.0f * (float)M_PI / 36.0f;
            glVertex3f(cosf(ang) * (r * 0.95f), sinf(ang) * (r * 0.95f), 0.0f);
        }
        glEnd();
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
}

/* ── Lil Cu 64 Mascot Renderer (Sole Sacred Personage) ─────────────── */

static void render_lilcu64(const AnimPose *pose, float time) {
    glPushMatrix();

    /* Apply continuous animation root transform */
    glTranslatef(pose->root_x, pose->root_y, pose->root_z);
    glRotatef(pose->root_rot_x * 57.295f, 1.0f, 0.0f, 0.0f);
    glRotatef(pose->root_rot_y * 57.295f, 0.0f, 1.0f, 0.0f);
    glRotatef(pose->root_rot_z * 57.295f, 0.0f, 0.0f, 1.0f);

    /* Body group (squash and stretch) */
    glPushMatrix();
    glTranslatef(pose->body_x, pose->body_y, pose->body_z);
    glRotatef(pose->body_rot_x * 57.295f, 1.0f, 0.0f, 0.0f);
    glRotatef(pose->body_rot_y * 57.295f, 0.0f, 1.0f, 0.0f);
    glRotatef(pose->body_rot_z * 57.295f, 0.0f, 0.0f, 1.0f);

    /* Rainbow Halo floating above head */
    glPushMatrix();
    glTranslatef(0.0f, 1.55f, 0.0f);
    glRotatef(time * 40.0f, 0.0f, 1.0f, 0.0f);
    draw_rainbow_halo(0.95f, 0.05f, time);
    glPopMatrix();

    /* Internal rotating octahedron core / gem */
    glPushMatrix();
    glRotatef(time * 60.0f, 0.0f, 1.0f, 0.0f);
    draw_octahedron(0.78f, false, 1.0f, 1.0f, 1.0f);
    glRotatef(-time * 90.0f, 1.0f, 0.0f, 0.0f);
    draw_octahedron(1.02f, true, 0.22f, 0.74f, 0.97f);
    glPopMatrix();

    /* Mouth */
    draw_mouth(pose->smile_scale, pose->omouth_scale);

    /* Eyes */
    float blink_squish = 1.0f;
    if (s_is_blinking) {
        blink_squish = 1.0f - sinf(s_blink_progress * (float)M_PI) * 0.90f;
    }

    glPushMatrix();
    glTranslatef(-0.46f, 0.32f, 1.08f);
    draw_anime_eye(blink_squish, true, 0.0f, 0.90f, 1.0f);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(0.46f, 0.32f, 1.08f);
    draw_anime_eye(blink_squish, false, 0.0f, 0.90f, 1.0f);
    glPopMatrix();

    /* Main Chamfered Cube Body: Lil Cu 64 Sky-Cyan (0.52, 0.91, 1.00) */
    draw_chamfered_cube(2.1f * pose->scale_x, 2.1f * pose->scale_y, 2.1f * pose->scale_z, 0.52f, 0.91f, 1.0f);

    /* Floating Hands */
    glPushMatrix();
    glTranslatef(-1.26f, 0.05f, 0.1f);
    glRotatef(pose->lhand_rot_x * 57.295f, 1.0f, 0.0f, 0.0f);
    glRotatef(pose->lhand_rot_z * 57.295f, 0.0f, 0.0f, 1.0f);
    draw_chamfered_cube_uniform(0.46f, 0.56f, 0.93f, 1.0f);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(1.26f, 0.05f, 0.1f);
    glRotatef(pose->rhand_rot_x * 57.295f, 1.0f, 0.0f, 0.0f);
    glRotatef(pose->rhand_rot_z * 57.295f, 0.0f, 0.0f, 1.0f);
    draw_chamfered_cube_uniform(0.46f, 0.56f, 0.93f, 1.0f);
    glPopMatrix();

    glPopMatrix(); /* End body group */

    /* Cute feet pods (attached to root) */
    /* Left Foot */
    glPushMatrix();
    glTranslatef(-0.64f, pose->lfoot_y, 0.08f);
    glRotatef(pose->lfoot_rot_x * 57.295f, 1.0f, 0.0f, 0.0f);
    glRotatef(pose->lfoot_rot_z * 57.295f, 0.0f, 0.0f, 1.0f);
    draw_chamfered_cube_uniform(0.55f, 1.0f, 0.30f, 0.55f);
    glPopMatrix();

    /* Right Foot */
    glPushMatrix();
    glTranslatef(0.64f, pose->rfoot_y, 0.08f);
    glRotatef(pose->rfoot_rot_x * 57.295f, 1.0f, 0.0f, 0.0f);
    glRotatef(pose->rfoot_rot_z * 57.295f, 0.0f, 0.0f, 1.0f);
    draw_chamfered_cube_uniform(0.55f, 1.0f, 0.30f, 0.55f);
    glPopMatrix();

    glPopMatrix(); /* End root */
}

/* ── Kinematics & Pose Update Loop ─────────────────────────────────── */

static void update_kinematics(float dt) {
    s_time += dt;
    s_state_timer += dt;

    /* Flywheel inertia */
    s_circular_vel *= 0.968f;
    s_circular_phase += s_circular_vel;

    /* Drag inertia spring back to front */
    if (!s_dragging) {
        s_cube_velx *= 0.90f;
        s_cube_vely *= 0.90f;
        s_cube_rotx += s_cube_velx;
        s_cube_roty += s_cube_vely;
        s_cube_rotx += (0.0f - s_cube_rotx) * 0.04f;
        s_cube_roty += (0.0f - s_cube_roty) * 0.04f;
    }

    /* Eye Blinking */
    s_blink_timer -= dt;
    if (s_blink_timer <= 0.0f) {
        s_is_blinking = true;
        s_blink_progress = 0.0f;
        s_blink_timer = 2.8f + ((float)rand() / (float)RAND_MAX) * 2.5f;
    }
    if (s_is_blinking) {
        s_blink_progress += dt * 8.5f;
        if (s_blink_progress >= 1.0f) {
            s_is_blinking = false;
            s_blink_progress = 0.0f;
        }
    }

    /* Target pose goals */
    float t_root_x = 0.0f, t_root_y = 0.08f, t_root_z = 0.0f;
    float t_root_rot_x = 0.0f, t_root_rot_y = 0.0f, t_root_rot_z = 0.0f;
    float t_body_x = 0.0f, t_body_y = 0.0f, t_body_z = 0.0f;
    float t_body_rot_x = 0.0f, t_body_rot_y = 0.0f, t_body_rot_z = 0.0f;
    float t_scale_x = 1.0f, t_scale_y = 1.0f, t_scale_z = 1.0f;
    float t_lhand_rot_x = 0.0f, t_lhand_rot_z = 0.10f;
    float t_rhand_rot_x = 0.0f, t_rhand_rot_z = -0.10f;
    float t_lfoot_y = -0.92f, t_lfoot_rot_x = 0.0f, t_lfoot_rot_z = 0.0f;
    float t_rfoot_y = -0.92f, t_rfoot_rot_x = 0.0f, t_rfoot_rot_z = 0.0f;
    float t_smile_scale = 1.0f, t_omouth_scale = 0.0f;

    switch (s_anim_state) {
        case ANIM_IDLE: {
            float breath = sinf(s_state_timer * 3.2f);
            float sway = sinf(s_state_timer * 1.6f);
            t_scale_y = 1.0f + breath * 0.04f;
            t_scale_x = 1.0f - breath * 0.02f;
            t_scale_z = 1.0f - breath * 0.02f;
            t_body_y = breath * 0.03f;
            t_body_rot_z = sway * 0.035f;
            t_lhand_rot_z = 0.10f + breath * 0.07f;
            t_rhand_rot_z = -0.10f - breath * 0.07f;
            break;
        }
        case ANIM_WALK: {
            s_walk_phase += dt * 7.5f;
            float sin_w = sinf(s_walk_phase);

            int sign = (sin_w > 0.0f) ? 1 : -1;
            if (sign != s_last_step_sign) {
                s_last_step_sign = sign;
                lilcu64_shim_footstep();
            }

            t_lfoot_y = -0.92f + (sin_w > 0.0f ? sin_w : 0.0f) * 0.36f;
            t_lfoot_rot_x = sin_w * 0.44f;
            t_rfoot_y = -0.92f + (-sin_w > 0.0f ? -sin_w : 0.0f) * 0.36f;
            t_rfoot_rot_x = -sin_w * 0.44f;

            t_body_rot_z = sin_w * 0.16f;
            t_body_y = fabsf(sinf(s_walk_phase * 2.0f)) * 0.12f;
            t_lhand_rot_x = -sin_w * 0.60f;
            t_rhand_rot_x = sin_w * 0.60f;

            if (s_state_timer >= 4.5f) set_animation(ANIM_IDLE);
            break;
        }
        case ANIM_FLOAT: {
            s_float_timer += dt;
            if (s_float_timer < 0.40f) {
                float p = s_float_timer / 0.40f;
                float ease = 0.5f - 0.5f * cosf(p * (float)M_PI);
                t_scale_x = 1.0f + ease * 0.30f;
                t_scale_y = 1.0f + ease * 0.28f;
                t_scale_z = 1.0f + ease * 0.30f;
                t_root_y = 0.08f + ease * 0.18f;
                t_smile_scale = 1.0f - ease;
                t_omouth_scale = ease * 1.25f;
            } else if (s_float_timer < 2.55f) {
                t_scale_x = 1.30f;
                t_scale_y = 1.28f;
                t_scale_z = 1.30f;
                t_smile_scale = 0.0f;
                t_omouth_scale = 1.25f;
                float hover = sinf((s_float_timer - 0.40f) * 3.4f);
                t_root_y = 0.92f + hover * 0.15f;
                t_lfoot_rot_x = sinf(s_float_timer * 9.0f) * 0.45f;
                t_rfoot_rot_x = cosf(s_float_timer * 9.0f) * 0.45f;
                t_lhand_rot_z = 0.40f + sinf(s_float_timer * 13.0f) * 0.35f;
                t_rhand_rot_z = -0.40f - sinf(s_float_timer * 13.0f) * 0.35f;
            } else if (s_float_timer < 3.35f) {
                float p = (s_float_timer - 2.55f) / 0.80f;
                float ease = 0.5f - 0.5f * cosf(p * (float)M_PI);
                t_scale_x = 1.30f - ease * 0.30f;
                t_scale_y = 1.28f - ease * 0.28f;
                t_scale_z = 1.30f - ease * 0.30f;
                t_root_y = 0.92f * (1.0f - ease) + 0.08f;
                t_smile_scale = ease;
                t_omouth_scale = 1.25f * (1.0f - ease);
            } else {
                set_animation(ANIM_IDLE);
                s_impact_squash = 0.18f;
                lilcu64_shim_boing();
            }
            break;
        }
        case ANIM_DANCE: {
            s_dance_timer += dt;
            if (s_dance_timer < 0.9f) {
                float p = s_dance_timer / 0.9f;
                float hop = sinf(p * (float)M_PI);
                t_root_y = 0.08f + hop * 0.78f;
                t_root_x = -0.42f * hop;
                t_body_rot_y = p * (float)M_PI * 2.0f;
                t_lhand_rot_z = 0.75f * hop;
                t_rhand_rot_z = -0.75f * hop;
            } else if (s_dance_timer < 1.8f) {
                float p = (s_dance_timer - 0.9f) / 0.9f;
                float hop = sinf(p * (float)M_PI);
                t_root_y = 0.08f + hop * 0.78f;
                t_root_x = 0.42f * hop;
                t_body_rot_y = -p * (float)M_PI * 2.0f;
                t_lhand_rot_z = -0.75f * hop;
                t_rhand_rot_z = 0.75f * hop;
            } else if (s_dance_timer < 2.7f) {
                float p = (s_dance_timer - 1.8f) / 0.9f;
                float hop = sinf(p * (float)M_PI);
                t_root_y = 0.08f + hop * 0.88f;
                t_body_rot_x = -p * (float)M_PI * 2.0f;
            } else if (s_dance_timer < 3.4f) {
                t_scale_x = 1.08f;
                t_scale_y = 0.93f;
                t_scale_z = 1.08f;
                t_lhand_rot_z = 1.35f; /* Victory hand raised high! */
                if (s_dance_step == 0) {
                    s_dance_step = 1;
                    s_impact_squash = 0.18f;
                }
            } else {
                set_animation(ANIM_IDLE);
            }
            break;
        }
        case ANIM_SING: {
            float curve = sinf((s_state_timer < 0.44f ? s_state_timer / 0.44f : 0.0f) * (float)M_PI);
            t_root_rot_x = -curve * 0.25f;
            t_root_y = 0.08f + curve * 0.26f;
            t_scale_x = 1.0f + curve * 0.14f;
            t_scale_y = 1.0f + curve * 0.16f;
            t_scale_z = 1.0f + curve * 0.14f;
            t_smile_scale = 1.0f - curve;
            t_omouth_scale = curve * 1.25f;
            if (s_state_timer >= 1.25f) set_animation(ANIM_IDLE);
            break;
        }
        case ANIM_POKE: {
            s_poke_timer -= dt * 1.8f;
            if (s_poke_timer > 0.0f) {
                float damp = expf(-4.5f * (1.0f - s_poke_timer));
                float osc = cosf(22.0f * (1.0f - s_poke_timer));
                float squish = 0.36f * damp * osc;
                t_scale_y = 1.0f - squish;
                t_scale_x = 1.0f + squish * 0.52f;
                t_scale_z = 1.0f + squish * 0.52f;
            } else {
                set_animation(ANIM_IDLE);
            }
            break;
        }
        default:
            break;
    }

    /* Landing Squash Cushion */
    s_impact_squash *= expf(-12.0f * dt);
    t_scale_y -= s_impact_squash;
    t_scale_x += s_impact_squash * 0.5f;
    t_scale_z += s_impact_squash * 0.5f;

    /* Blend continuous pose toward target */
    s_pose.root_x = smooth_damp(s_pose.root_x, t_root_x, 18.0f, dt);
    s_pose.root_y = smooth_damp(s_pose.root_y, t_root_y, 18.0f, dt);
    s_pose.root_z = smooth_damp(s_pose.root_z, t_root_z, 18.0f, dt);
    s_pose.root_rot_x = smooth_damp(s_pose.root_rot_x, t_root_rot_x, 16.0f, dt);
    s_pose.root_rot_y = smooth_damp(s_pose.root_rot_y, t_root_rot_y, 16.0f, dt);
    s_pose.root_rot_z = smooth_damp(s_pose.root_rot_z, t_root_rot_z, 16.0f, dt);

    s_pose.body_x = smooth_damp(s_pose.body_x, t_body_x, 18.0f, dt);
    s_pose.body_y = smooth_damp(s_pose.body_y, t_body_y, 18.0f, dt);
    s_pose.body_z = smooth_damp(s_pose.body_z, t_body_z, 18.0f, dt);
    s_pose.body_rot_x = smooth_damp(s_pose.body_rot_x, t_body_rot_x, 16.0f, dt);
    s_pose.body_rot_y = smooth_damp(s_pose.body_rot_y, t_body_rot_y, 16.0f, dt);
    s_pose.body_rot_z = smooth_damp(s_pose.body_rot_z, t_body_rot_z, 16.0f, dt);

    s_pose.scale_x = smooth_damp(s_pose.scale_x, t_scale_x, 22.0f, dt);
    s_pose.scale_y = smooth_damp(s_pose.scale_y, t_scale_y, 22.0f, dt);
    s_pose.scale_z = smooth_damp(s_pose.scale_z, t_scale_z, 22.0f, dt);

    s_pose.lhand_rot_x = smooth_damp(s_pose.lhand_rot_x, t_lhand_rot_x, 15.0f, dt);
    s_pose.lhand_rot_z = smooth_damp(s_pose.lhand_rot_z, t_lhand_rot_z, 15.0f, dt);
    s_pose.rhand_rot_x = smooth_damp(s_pose.rhand_rot_x, t_rhand_rot_x, 15.0f, dt);
    s_pose.rhand_rot_z = smooth_damp(s_pose.rhand_rot_z, t_rhand_rot_z, 15.0f, dt);

    s_pose.lfoot_y = smooth_damp(s_pose.lfoot_y, t_lfoot_y, 20.0f, dt);
    s_pose.lfoot_rot_x = smooth_damp(s_pose.lfoot_rot_x, t_lfoot_rot_x, 20.0f, dt);
    s_pose.lfoot_rot_z = smooth_damp(s_pose.lfoot_rot_z, t_lfoot_rot_z, 20.0f, dt);
    s_pose.rfoot_y = smooth_damp(s_pose.rfoot_y, t_rfoot_y, 20.0f, dt);
    s_pose.rfoot_rot_x = smooth_damp(s_pose.rfoot_rot_x, t_rfoot_rot_x, 20.0f, dt);
    s_pose.rfoot_rot_z = smooth_damp(s_pose.rfoot_rot_z, t_rfoot_rot_z, 20.0f, dt);

    s_pose.smile_scale = smooth_damp(s_pose.smile_scale, t_smile_scale, 24.0f, dt);
    s_pose.omouth_scale = smooth_damp(s_pose.omouth_scale, t_omouth_scale, 24.0f, dt);

    update_waves(dt);
}

/* ── 2D UI Dashboard Drawing Overlay ──────────────────────────────── */

static void render_2d_dashboard(GDEV *dev) {
    if (!dev) return;

    /* Top banner tag */
    drw_tc_string(dev, 24, 14, "🌸 BTRON3 × Synrc VE OS.1 次世代コンシューマOS仕様", COLOR_CYAN, 0);
    drw_tc_string(dev, 24, 34, "B-System 体系構造 — 聖なるリル・キューブ64 (Lil Cu 64)", COLOR_WHITE, 0);

    /* Speech Bubble Text */
    RECT bubble_r = { 20, dev->height - 64, dev->width - 20, dev->height - 20 };
    fill_rec(dev, &bubble_r, 0xdd0f172a);
    set_col(dev, COLOR_CYAN, COLOR_BLACK);
    drw_rec(dev, &bubble_r);
    drw_tc_string(dev, 32, dev->height - 52, s_quote_text, COLOR_GOLD, 0);

    /* Control hints */
    char hint_buf[128];
    int active_t = lilcu64_synth_get_active_track();
    snprintf(hint_buf, sizeof(hint_buf),
             "[Space] Pulse  [W] Waddle  [F] Float  [D] Dance  [1-4] Track (%s)  [M] Sound  [‹/›] Carousel",
             active_t > 0 ? "ON" : "OFF");
    drw_tc_string(dev, 32, dev->height - 32, hint_buf, COLOR_WHITE, 0);
}

/* ── Window Paint & Render Callback ───────────────────────────────── */

static void demo_paint(WND *wnd, GDEV *dev) {
    if (!wnd || !dev || !s_surf) return;

    if (s_surf->width != dev->width || s_surf->height != dev->height) {
        egl_surface_resize(s_surf, dev->width, dev->height);
    }

    update_kinematics(s_dt);

    /* Pump VirtIO sound PCM buffer */
    lilcu64_synth_pump_virtio(735); /* ~16.6ms of audio at 44.1kHz */

    glViewport(0, 0, dev->width, dev->height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    double aspect = (double)dev->width / (double)(dev->height > 0 ? dev->height : 1);
    glFrustum(-aspect * 0.32, aspect * 0.32, -0.32, 0.32, 1.0, 100.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, -0.08f, -9.2f); /* Camera at (0, 0.08, 9.2) looking at (0, 0.08, 0) */

    glClearColor(0.035f, 0.045f, 0.085f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glPushMatrix();

    /* Apply camera drag rotation */
    glRotatef(s_view_rotx + s_cube_rotx, 1.0f, 0.0f, 0.0f);
    glRotatef(s_view_roty + s_cube_roty, 0.0f, 1.0f, 0.0f);

    /* 1. Render 4D Hopf Fibration Background */
    render_hopf_fibration(s_time, s_density_mode);

    /* 2. Render Spherical Radiant Pulse Waves */
    render_radiant_waves();

    /* 3. Render Personage Carousel */
    render_carousel(s_time);

    glPopMatrix();

    /* Swap OpenGL buffer to window client area */
    egl_swap_buffers(s_surf);

    /* Draw 2D BTRON Dashboard & Speech Bubble */
    render_2d_dashboard(dev);
}

/* ── Window Event Callback ────────────────────────────────────────── */

static void demo_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;

    if (evt->type == EV_KEY_DOWN) {
        if (evt->key == 0x1B || evt->key == 'q' || evt->key == 'Q') {
            cls_wnd(wnd);
        } else if (evt->key == ' ' || evt->key == 'p' || evt->key == 'P' || evt->key == 'j' || evt->key == 'J') {
            set_animation(ANIM_SQUASH);
        } else if (evt->key == 'w' || evt->key == 'W') {
            set_animation(ANIM_WALK);
        } else if (evt->key == 'f' || evt->key == 'F') {
            set_animation(ANIM_FLOAT);
        } else if (evt->key == 'd' || evt->key == 'D') {
            set_animation(ANIM_DANCE);
        } else if (evt->key == '1' || evt->key == 's' || evt->key == 'S') {
            lilcu64_synth_play_track(1);
        } else if (evt->key == '2' || evt->key == 'c' || evt->key == 'C') {
            lilcu64_synth_play_track(2);
        } else if (evt->key == '3' || evt->key == 'h' || evt->key == 'H' || evt->key == 'r' || evt->key == 'R') {
            lilcu64_synth_play_track(3);
        } else if (evt->key == '4' || evt->key == 'n' || evt->key == 'N' || evt->key == 't' || evt->key == 'T') {
            lilcu64_synth_play_track(4);
        } else if (evt->key == 'm' || evt->key == 'M') {
            lilcu64_synth_toggle_mute();
        } else if (evt->key == 0x1C || evt->key == '<' || evt->key == ',') { /* Left arrow / prev */
            s_current_personage = (s_current_personage + 3) % 4;
            s_target_carousel_rot -= 90.0f;
            lilcu64_shim_ding(1318.51f, 0.7f, 0.4f);
        } else if (evt->key == 0x1D || evt->key == '>' || evt->key == '.') { /* Right arrow / next */
            s_current_personage = (s_current_personage + 1) % 4;
            s_target_carousel_rot += 90.0f;
            lilcu64_shim_ding(1760.0f, 0.7f, 0.4f);
        }
        inval_wnd(wnd);

    } else if (evt->type == EV_BUT_DOWN) {
        s_dragging = 1;
        s_last_mx = evt->pos.x;
        s_last_my = evt->pos.y;

        /* Click on mascot triggers poke reaction */
        if (evt->pos.y > 100 && evt->pos.y < wnd->client.bottom - 100) {
            set_animation(ANIM_POKE);
        }
    } else if (evt->type == EV_BUT_UP) {
        s_dragging = 0;
    } else if (evt->type == EV_MOUSE_MOVE && s_dragging) {
        float dx = (float)(evt->pos.x - s_last_mx);
        float dy = (float)(evt->pos.y - s_last_my);

        s_cube_vely += dx * 0.4f;
        s_cube_velx += dy * 0.4f;

        s_circular_vel += (fabsf(dx) + fabsf(dy)) * 0.002f;

        s_last_mx = evt->pos.x;
        s_last_my = evt->pos.y;
        inval_wnd(wnd);
    }
}

/* ── Window Destroy Callback ──────────────────────────────────────── */

static void demo_destroy(WND *wnd) {
    (void)wnd;
    lilcu64_synth_close();
    if (s_surf) {
        egl_destroy_surface(s_surf);
        s_surf = NULL;
    }
    s_demo_wnd = NULL;
    if (s_demo_tskid > 0) {
        wup_tsk(s_demo_tskid);
    }
}

/* ── 60 FPS Scheduler Task ─────────────────────────────────────────── */

static void demo_task_fn(VW exinf) {
    (void)exinf;
    while (s_demo_wnd) {
        inval_wnd(s_demo_wnd);
        dly_tsk(16); /* ~60 FPS APIC timer tick */
    }
    s_demo_tskid = 0;
}

/* ── Public Entrypoint ────────────────────────────────────────────── */

WND* open_lilcu64_demo_window(void) {
    if (s_demo_wnd) {
        top_wnd(s_demo_wnd);
        return s_demo_wnd;
    }

    s_demo_wnd = opn_wnd("Lil Cu 64 — B-System Alpha 1 [Ceremony]",
                         180, 40, 720, 540,
                         WND_ATTR_TITLE | WND_ATTR_BORDER | WND_ATTR_CLOSE | WND_ATTR_RESIZE);
    if (!s_demo_wnd) {
        uart_puts_raw("[DEMO] ERROR: opn_wnd failed\n");
        return NULL;
    }

    s_demo_wnd->paint = demo_paint;
    s_demo_wnd->event_handler = demo_event;
    s_demo_wnd->destroy = demo_destroy;

    s_surf = egl_create_window_surface(s_demo_wnd);
    if (!s_surf) {
        uart_puts_raw("[DEMO] ERROR: egl_create_window_surface failed\n");
        cls_wnd(s_demo_wnd);
        return NULL;
    }

    /* Initialize procedural synthesizer */
    lilcu64_synth_init(LILCU64_AUDIO_RATE);

    /* OpenGL initial state */
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glShadeModel(GL_SMOOTH);

    static const GLfloat light_pos[4]   = { 4.0f, 8.0f, 6.0f, 1.0f };
    static const GLfloat light_white[4] = { 0.95f, 0.95f, 0.95f, 1.0f };

    glLightfv(GL_LIGHT0, GL_POSITION, light_pos);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, light_white);
    glLightfv(GL_LIGHT0, GL_SPECULAR, light_white);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);

    /* Register animation task */
    T_CTSK ctsk;
    ctsk.exinf = 0;
    ctsk.tskatr = TA_HLNG;
    ctsk.task = demo_task_fn;
    ctsk.itskpri = 10;
    ctsk.stksz = 32768;
    s_demo_tskid = cre_tsk(&ctsk);
    if (s_demo_tskid > 0) {
        sta_tsk(s_demo_tskid, 0);
        uart_puts_raw("[DEMO] Lil Cu 64 Demoscene: 60 FPS task started\n");
    }

    uart_puts_raw("[DEMO] Lil Cu 64 Demoscene window active\n");
    return s_demo_wnd;
}
