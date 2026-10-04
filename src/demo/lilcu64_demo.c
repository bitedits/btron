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

/* Gaze Tracking & Drag state (1:1 with index.html lines 5921, 6970-6990) */
static GLfloat s_target_look_x = 0.0f;
static GLfloat s_target_look_y = 0.0f;
static GLfloat s_cube_rotx = 0.0f;
static GLfloat s_cube_roty = 0.0f;
static GLfloat s_cube_velx = 0.0f;
static GLfloat s_cube_vely = 0.0f;
static int     s_dragging = 0;
static H       s_prev_drag_x = 0, s_prev_drag_y = 0;

/* Hopf Background Tilt & Swirl Dynamics (1:1 with index.html lines 6926-6936) */
static float   s_target_rot_x = 0.22f;
static float   s_target_rot_y = 0.0f;
static float   s_cur_rot_x    = 0.22f;
static float   s_cur_rot_y    = 0.0f;
static float   s_last_pointer_angle = -999.0f;
static float   s_circular_vel = 0.0f;
static float   s_circular_phase = 0.0f;

/* Timing & Clock */
static float   s_time = 0.0f;
static float   s_dt   = 0.0166f;

/* Density modes: 0 = 18 rings (canonical), 1 = 36 rings, 2 = 54 rings (index.html line 6940) */
static int     s_density_mode = 0;

/* Core gem and wireframe octahedron rotations (index.html lines 6095, 7234-7235) */
static float   s_core_rot_x = 0.0f;
static float   s_core_rot_y = 0.0f;
static float   s_wire_rot_x = 0.0f;
static float   s_wire_rot_y = 0.0f;

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
static float     s_sing_halo_curve = 0.0f;

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

/* Continuous Inertial Pose Buffer (1:1 with index.html animPose) */
typedef struct {
    float root_x, root_y, root_z;
    float root_rot_x, root_rot_y, root_rot_z;
    float body_x, body_y, body_z;
    float body_rot_x, body_rot_y, body_rot_z;
    float scale_x, scale_y, scale_z;
    float lhand_rot_x, lhand_rot_y, lhand_rot_z;
    float rhand_rot_x, rhand_rot_y, rhand_rot_z;
    float left_foot_x, lfoot_y, left_foot_z;
    float lfoot_rot_x, lfoot_rot_y, lfoot_rot_z;
    float right_foot_x, rfoot_y, right_foot_z;
    float rfoot_rot_x, rfoot_rot_y, rfoot_rot_z;
    float smile_scale, omouth_scale;
    float pedestal_scale, pedestal_opacity;
} AnimPose;

static AnimPose s_pose = {
    0.0f, 0.08f, 0.0f,
    0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f,
    1.0f, 1.0f, 1.0f,
    0.0f, 0.0f, 0.10f,
    0.0f, 0.0f, -0.10f,
    -0.64f, -0.92f, 0.08f,
    0.0f, 0.0f, 0.0f,
    0.64f, -0.92f, 0.08f,
    0.0f, 0.0f, 0.0f,
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

static inline float smooth_damp_angle(float cur, float target, float speed, float dt) {
    float diff = fmodf(target - cur, (float)(2.0 * M_PI));
    if (diff > (float)M_PI) diff -= (float)(2.0 * M_PI);
    if (diff < (float)-M_PI) diff += (float)(2.0 * M_PI);
    float factor = 1.0f - expf(-speed * dt);
    return cur + diff * factor;
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

/* ── 3D Geometry Builders (1:1 with index.html Three.js Models) ───── */

/* Faceted gem cube: BoxGeometry(2.1, 2.1, 2.1, 2, 2, 2) clamped at R = 1.65 */
static void draw_faceted_gem_cube(float r, float g, float b) {
    glColor3f(r, g, b);

    /* 6 faces with 2x2 grid subdivisions (24 quads total) */
    static const int face_axes[6][3] = {
        { 0, 1, 2 }, /* +Z Front */
        { 0, 1, 2 }, /* -Z Back */
        { 0, 2, 1 }, /* +Y Top */
        { 0, 2, 1 }, /* -Y Bottom */
        { 2, 1, 0 }, /* +X Right */
        { 2, 1, 0 }  /* -X Left */
    };
    static const float face_signs[6] = { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f };

    glBegin(GL_QUADS);
    for (int f = 0; f < 6; f++) {
        int u_ax = face_axes[f][0];
        int v_ax = face_axes[f][1];
        int w_ax = face_axes[f][2];
        float w_val = face_signs[f] * 1.05f;

        for (int u = 0; u < 2; u++) {
            float u0 = (u == 0) ? -1.05f : 0.0f;
            float u1 = (u == 0) ? 0.0f : 1.05f;

            for (int v = 0; v < 2; v++) {
                float v0 = (v == 0) ? -1.05f : 0.0f;
                float v1 = (v == 0) ? 0.0f : 1.05f;

                float quad[4][3];
                quad[0][u_ax] = u0; quad[0][v_ax] = v0; quad[0][w_ax] = w_val;
                quad[1][u_ax] = u1; quad[1][v_ax] = v0; quad[1][w_ax] = w_val;
                quad[2][u_ax] = u1; quad[2][v_ax] = v1; quad[2][w_ax] = w_val;
                quad[3][u_ax] = u0; quad[3][v_ax] = v1; quad[3][w_ax] = w_val;

                /* Apply exact R = 1.65 spherical corner clamp (index.html line 6064) */
                for (int k = 0; k < 4; k++) {
                    float d = sqrtf(quad[k][0]*quad[k][0] + quad[k][1]*quad[k][1] + quad[k][2]*quad[k][2]);
                    if (d > 1.65f) {
                        float factor = 1.65f / d;
                        quad[k][0] *= factor;
                        quad[k][1] *= factor;
                        quad[k][2] *= factor;
                    }
                }

                /* Compute normal */
                float e1x = quad[1][0] - quad[0][0], e1y = quad[1][1] - quad[0][1], e1z = quad[1][2] - quad[0][2];
                float e2x = quad[3][0] - quad[0][0], e2y = quad[3][1] - quad[0][1], e2z = quad[3][2] - quad[0][2];
                float nx = (f % 2 == 0) ? (e1y * e2z - e1z * e2y) : (e2y * e1z - e2z * e1y);
                float ny = (f % 2 == 0) ? (e1z * e2x - e1x * e2z) : (e2z * e1x - e2x * e1z);
                float nz = (f % 2 == 0) ? (e1x * e2y - e1y * e2x) : (e2x * e1y - e2y * e1x);
                float nlen = sqrtf(nx*nx + ny*ny + nz*nz);
                if (nlen > 0.0001f) { nx /= nlen; ny /= nlen; nz /= nlen; }
                glNormal3f(nx, ny, nz);

                if (f % 2 == 0) {
                    glVertex3f(quad[0][0], quad[0][1], quad[0][2]);
                    glVertex3f(quad[1][0], quad[1][1], quad[1][2]);
                    glVertex3f(quad[2][0], quad[2][1], quad[2][2]);
                    glVertex3f(quad[3][0], quad[3][1], quad[3][2]);
                } else {
                    glVertex3f(quad[3][0], quad[3][1], quad[3][2]);
                    glVertex3f(quad[2][0], quad[2][1], quad[2][2]);
                    glVertex3f(quad[1][0], quad[1][1], quad[1][2]);
                    glVertex3f(quad[0][0], quad[0][1], quad[0][2]);
                }
            }
        }
    }
    glEnd();
}

/* Axis-aligned Box Geometry */
static void draw_box(float wx, float wy, float wz, float r, float g, float b) {
    float hx = wx * 0.5f, hy = wy * 0.5f, hz = wz * 0.5f;
    glColor3f(r, g, b);

    glBegin(GL_QUADS);
    /* Front */
    glNormal3f(0.0f, 0.0f, 1.0f);
    glVertex3f(-hx, -hy,  hz); glVertex3f( hx, -hy,  hz);
    glVertex3f( hx,  hy,  hz); glVertex3f(-hx,  hy,  hz);
    /* Back */
    glNormal3f(0.0f, 0.0f, -1.0f);
    glVertex3f( hx, -hy, -hz); glVertex3f(-hx, -hy, -hz);
    glVertex3f(-hx,  hy, -hz); glVertex3f( hx,  hy, -hz);
    /* Top */
    glNormal3f(0.0f, 1.0f, 0.0f);
    glVertex3f(-hx,  hy,  hz); glVertex3f( hx,  hy,  hz);
    glVertex3f( hx,  hy, -hz); glVertex3f(-hx,  hy, -hz);
    /* Bottom */
    glNormal3f(0.0f, -1.0f, 0.0f);
    glVertex3f(-hx, -hy, -hz); glVertex3f( hx, -hy, -hz);
    glVertex3f( hx, -hy,  hz); glVertex3f(-hx, -hy,  hz);
    /* Right */
    glNormal3f(1.0f, 0.0f, 0.0f);
    glVertex3f( hx, -hy,  hz); glVertex3f( hx, -hy, -hz);
    glVertex3f( hx,  hy, -hz); glVertex3f( hx,  hy,  hz);
    /* Left */
    glNormal3f(-1.0f, 0.0f, 0.0f);
    glVertex3f(-hx, -hy, -hz); glVertex3f(-hx, -hy,  hz);
    glVertex3f(-hx,  hy,  hz); glVertex3f(-hx,  hy, -hz);
    glEnd();
}

/* Octahedron core (solid or wireframe) */
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
            /* Face normal */
            int v0 = faces[i][0], v1 = faces[i][1], v2 = faces[i][2];
            float e1x = vertices[v1][0] - vertices[v0][0], e1y = vertices[v1][1] - vertices[v0][1], e1z = vertices[v1][2] - vertices[v0][2];
            float e2x = vertices[v2][0] - vertices[v0][0], e2y = vertices[v2][1] - vertices[v0][1], e2z = vertices[v2][2] - vertices[v0][2];
            float nx = e1y * e2z - e1z * e2y, ny = e1z * e2x - e1x * e2z, nz = e1x * e2y - e1y * e2x;
            float nlen = sqrtf(nx*nx + ny*ny + nz*nz);
            if (nlen > 0.0001f) { nx /= nlen; ny /= nlen; nz /= nlen; }
            glNormal3f(nx, ny, nz);

            glVertex3f(vertices[v0][0] * radius, vertices[v0][1] * radius, vertices[v0][2] * radius);
            glVertex3f(vertices[v1][0] * radius, vertices[v1][1] * radius, vertices[v1][2] * radius);
            glVertex3f(vertices[v2][0] * radius, vertices[v2][1] * radius, vertices[v2][2] * radius);
        }
        glEnd();
    }
}

/* ── Golden Hexagon Saint Halo (Kung-Fu Panda Spirit Realm Finale) ───── */
/* Radiates behind character ONLY during Sing / Sacred Pulse animation   */

static void draw_saint_hex_halo(float time, float intensity) {
    if (intensity <= 0.001f) return;
    glDisable(GL_LIGHTING);

    float rot1 = time * 0.35f;
    float rot2 = -time * 0.28f;
    float rot3 = time * 0.50f;

    float sc = 0.82f + 0.18f * intensity;

    /* 1. Outer Golden Hexagon Ring (R = 2.50 * sc, frames the full character body) */
    glColor3f(1.0f * intensity, 0.84f * intensity, 0.0f * intensity); /* Pure Gold */
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < 6; i++) {
        float a = (float)i * (float)(M_PI / 3.0) + rot1;
        glVertex3f(cosf(a) * (2.50f * sc), sinf(a) * (2.50f * sc), 0.0f);
    }
    glEnd();

    /* 2. Concentric Interlocking Hexagon (R = 2.35 * sc, rotated 30 deg / Sacred Hexagram) */
    glColor3f(1.0f * intensity, 0.93f * intensity, 0.45f * intensity); /* Luminous Chi Gold */
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < 6; i++) {
        float a = (float)i * (float)(M_PI / 3.0) + (float)(M_PI / 6.0) + rot2;
        glVertex3f(cosf(a) * (2.35f * sc), sinf(a) * (2.35f * sc), 0.0f);
    }
    glEnd();

    /* 3. Mid Sacred Hexagon (R = 1.85 * sc) */
    glColor3f(0.98f * intensity, 0.75f * intensity, 0.12f * intensity); /* Deep Amber Gold */
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < 6; i++) {
        float a = (float)i * (float)(M_PI / 3.0) + rot3;
        glVertex3f(cosf(a) * (1.85f * sc), sinf(a) * (1.85f * sc), 0.0f);
    }
    glEnd();

    /* 4. Inner Concentric Hexagon (R = 1.40 * sc) */
    glColor3f(1.0f * intensity, 0.96f * intensity, 0.70f * intensity); /* Brilliant White-Gold */
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < 6; i++) {
        float a = (float)i * (float)(M_PI / 3.0) + rot1 * 1.4f;
        glVertex3f(cosf(a) * (1.40f * sc), sinf(a) * (1.40f * sc), 0.0f);
    }
    glEnd();

    /* 5. 6 Sacred Chi Rays connecting inner and outer vertices */
    glColor3f(1.0f * intensity, 0.88f * intensity, 0.35f * intensity);
    glBegin(GL_LINES);
    for (int i = 0; i < 6; i++) {
        float a1 = (float)i * (float)(M_PI / 3.0) + rot1;
        float a2 = (float)i * (float)(M_PI / 3.0) + rot3;
        glVertex3f(cosf(a2) * (1.40f * sc), sinf(a2) * (1.40f * sc), 0.0f);
        glVertex3f(cosf(a1) * (2.50f * sc), sinf(a1) * (2.50f * sc), 0.0f);
    }
    glEnd();

    /* 6. Twelve Star-Points at outer vertices */
    glColor3f(1.0f * intensity, 1.0f * intensity, 0.88f * intensity);
    glBegin(GL_LINES);
    for (int i = 0; i < 6; i++) {
        float a = (float)i * (float)(M_PI / 3.0) + rot1;
        float x = cosf(a) * (2.50f * sc);
        float y = sinf(a) * (2.50f * sc);
        glVertex3f(x - 0.08f * sc, y, 0.0f);
        glVertex3f(x + 0.08f * sc, y, 0.0f);
        glVertex3f(x, y - 0.08f * sc, 0.0f);
        glVertex3f(x, y + 0.08f * sc, 0.0f);
    }
    glEnd();

    glEnable(GL_LIGHTING);
}

/* Lil Cube Anime Eyes (1:1 with index.html lines 5964-6000) */
static void draw_anime_eye(float squish_y, bool is_left) {
    (void)is_left;
    glPushMatrix();

    /* 1. Pupil: Cylinder/disc radius 0.18, height 0.48 in 0x081226 (0.03, 0.07, 0.15) */
    glColor3f(0.031f, 0.070f, 0.149f);
    glBegin(GL_POLYGON);
    for (int a = 0; a < 12; a++) {
        float rad = (float)a * 2.0f * (float)M_PI / 12.0f;
        glVertex3f(cosf(rad) * 0.18f, sinf(rad) * 0.24f * squish_y, 0.0f);
    }
    glEnd();

    /* 2. Iris: Cylinder/disc radius 0.15 at (0, -0.11, 0.015) in 0x00e5ff (0.0, 0.898, 1.0) */
    glColor3f(0.0f, 0.898f, 1.0f);
    glBegin(GL_POLYGON);
    for (int a = 0; a < 10; a++) {
        float rad = (float)a * 2.0f * (float)M_PI / 10.0f;
        glVertex3f(cosf(rad) * 0.15f, (-0.11f + sinf(rad) * 0.11f) * squish_y, 0.015f);
    }
    glEnd();

    /* 3. Specular highlight: Circle radius 0.09 at (-0.04, 0.11, 0.025) in 0xffffff */
    glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_POLYGON);
    for (int a = 0; a < 8; a++) {
        float rad = (float)a * 2.0f * (float)M_PI / 8.0f;
        glVertex3f(-0.04f + cosf(rad) * 0.09f, (0.11f + sinf(rad) * 0.09f) * squish_y, 0.025f);
    }
    glEnd();

    glPopMatrix();
}

/* Cheeks: Blush discs radius 0.22 in 0xff4081 (1:1 with index.html lines 6002-6012) */
static void draw_cheek_blush(void) {
    glColor3f(1.0f, 0.251f, 0.506f);
    glBegin(GL_POLYGON);
    for (int a = 0; a < 10; a++) {
        float rad = (float)a * 2.0f * (float)M_PI / 10.0f;
        glVertex3f(cosf(rad) * 0.22f, sinf(rad) * 0.22f, 0.0f);
    }
    glEnd();
}

/* Mouth Smile / O-Mouth: 0xbf1250 (1:1 with index.html lines 5950-5961) */
static void draw_mouth(float smile_scale, float omouth_scale) {
    glColor3f(0.749f, 0.071f, 0.314f);

    if (smile_scale > 0.05f) {
        /* Torus arc R=0.16, r=0.038 */
        glBegin(GL_LINE_STRIP);
        for (int a = 0; a <= 10; a++) {
            float ang = (float)M_PI + (float)a * (float)M_PI / 10.0f;
            glVertex3f(cosf(ang) * 0.16f * smile_scale, sinf(ang) * 0.09f * smile_scale, 1.08f);
        }
        glEnd();
    }
    if (omouth_scale > 0.05f) {
        /* Torus ring R=0.15, r=0.05 */
        glBegin(GL_LINE_LOOP);
        for (int a = 0; a < 12; a++) {
            float ang = (float)a * 2.0f * (float)M_PI / 12.0f;
            glVertex3f(cosf(ang) * 0.15f * omouth_scale, sinf(ang) * 0.15f * omouth_scale, 1.08f);
        }
        glEnd();
    }
}

/* ── 4D Hopf Fibration Background Renderer ─────────────────────────── */

static void render_hopf_fibration(float time, int density_mode) {
    /* 1:1 with densityConfigs in index.html line 6940:
     * Mode 0: tori=2, fibersPerTorus=9,  pts=40 (18 rings)
     * Mode 1: tori=3, fibersPerTorus=12, pts=46 (36 rings)
     * Mode 2: tori=3, fibersPerTorus=18, pts=52 (54 rings)
     */
    int num_tori = 2;
    int fibers_per_torus = 9;
    int num_pts = 40;

    if (density_mode == 1) {
        num_tori = 3;
        fibers_per_torus = 12;
        num_pts = 46;
    } else if (density_mode == 2) {
        num_tori = 3;
        fibers_per_torus = 18;
        num_pts = 52;
    }

    int total_fibers = num_tori * fibers_per_torus;

    /* Serene, majestic motion speeds & 4D isoclinic rotation (index.html lines 7698-7705) */
    float cosX = cosf(s_cur_rot_x);
    float sinX = sinf(s_cur_rot_x);
    float cosY = cosf(s_cur_rot_y + time * 0.06f + s_circular_phase * 0.25f);
    float sinY = sinf(s_cur_rot_y + time * 0.06f + s_circular_phase * 0.25f);
    float cosZ = cosf(time * 0.04f);
    float sinZ = sinf(time * 0.04f);

    float psi = time * 0.12f + s_circular_phase;
    float omega = time * 0.08f + s_circular_phase * 0.40f;

    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);

    /* Check wave resonance from radiant waves */
    float wave_resonance = 0.0f;
    for (int w = 0; w < MAX_WAVES; w++) {
        if (!s_waves[w].active) continue;
        float p = s_waves[w].time / s_waves[w].duration;
        wave_resonance += (1.0f - p) * 0.7f;
    }
    if (wave_resonance > 1.0f) wave_resonance = 1.0f;

    for (int t = 0; t < num_tori; t++) {
        float eta = ((float)M_PI / 7.0f) + ((float)t + 0.6f) * ((float)M_PI / ((float)num_tori * 2.8f));
        float cos_eta = cosf(eta);
        float sin_eta = sinf(eta);

        for (int f = 0; f < fibers_per_torus; f++) {
            int fiber_idx = t * fibers_per_torus + f;
            float beta = ((float)f * 2.0f * (float)M_PI / (float)fibers_per_torus) + omega;

            /* Spectral rainbow dispersion */
            float hue = fmodf(((float)fiber_idx / (float)total_fibers) * 360.0f + time * 24.0f, 360.0f);
            float cr, cg, cb;
            hsv_to_rgb(hue, 0.95f, 0.78f, &cr, &cg, &cb);

            float r = cr * (1.0f - wave_resonance) + 1.0f * wave_resonance;
            float g = cg * (1.0f - wave_resonance) + 0.92f * wave_resonance;
            float b = cb * (1.0f - wave_resonance) + 0.43f * wave_resonance;

            glColor3f(r * 0.75f, g * 0.75f, b * 0.75f);

            glBegin(GL_LINE_STRIP);
            for (int p = 0; p <= num_pts; p++) {
                float xi = (float)p * 2.0f * (float)M_PI / (float)num_pts;

                /* 4D Coordinates on S³ */
                float x0 = cos_eta * cosf(xi + beta * 0.5f + psi);
                float x1 = cos_eta * sinf(xi + beta * 0.5f + psi);
                float x2 = sin_eta * cosf(xi - beta * 0.5f + psi * 0.8f);
                float x3 = sin_eta * sinf(xi - beta * 0.5f + psi * 0.8f);

                /* Stereographic Projection S³ → ℝ³ from (0,0,0,1) */
                float denom = 1.08f - x3;
                if (denom < 0.08f) denom = 0.08f;
                float X3d = x0 / denom;
                float Y3d = x1 / denom;
                float Z3d = x2 / denom;

                /* 3D Rotations (Yaw -> Pitch -> Roll) 1:1 with index.html lines 7736-7747 */
                float x_yaw = X3d * cosY + Z3d * sinY;
                float y_yaw = Y3d;
                float z_yaw = -X3d * sinY + Z3d * cosY;

                float x_pitch = x_yaw;
                float y_pitch = y_yaw * cosX - z_yaw * sinX;
                float z_pitch = y_yaw * sinX + z_yaw * cosX;

                float x_rot = x_pitch * cosZ - y_pitch * sinZ;
                float y_rot = x_pitch * sinZ + y_pitch * cosZ;
                float z_rot = z_pitch;

                glVertex3f(x_rot * 1.8f, y_rot * 1.8f, z_rot * 1.8f - 1.2f);
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

/* ── Lil Cu 64 Mascot Renderer (1:1 with index.html lines 6055-6113) ─ */

static void render_lilcu64(const AnimPose *pose, float time) {
    (void)time;
    glPushMatrix();

    /* Root Group (balances optical center at (0, 0, 0), scale (0.60, 0.60, 0.60)) */
    glTranslatef(pose->root_x, pose->root_y, pose->root_z);
    glRotatef((pose->root_rot_x + s_cube_rotx + s_target_look_y * 0.35f) * 57.29578f, 1.0f, 0.0f, 0.0f);
    glRotatef((pose->root_rot_y + s_cube_roty + s_target_look_x * 0.45f) * 57.29578f, 0.0f, 1.0f, 0.0f);
    glRotatef(pose->root_rot_z * 57.29578f, 0.0f, 0.0f, 1.0f);
    glScalef(0.60f, 0.60f, 0.60f);

    /* 0. Golden Hexagon Saint Halo: appears ONLY during Sing animation (framing character body on back layer) */
    if (s_sing_halo_curve > 0.001f) {
        glPushMatrix();
        glTranslatef(0.0f, 0.12f, -0.60f);
        draw_saint_hex_halo(time, s_sing_halo_curve);
        glPopMatrix();
    }

    /* Body Group (child of rootGroup) */
    glPushMatrix();
    glTranslatef(pose->body_x, pose->body_y, pose->body_z);
    glRotatef(pose->body_rot_x * 57.29578f, 1.0f, 0.0f, 0.0f);
    glRotatef(pose->body_rot_y * 57.29578f, 0.0f, 1.0f, 0.0f);
    glRotatef(pose->body_rot_z * 57.29578f, 0.0f, 0.0f, 1.0f);
    glScalef(pose->scale_x, pose->scale_y, pose->scale_z);

    /* 1. Main Faceted Gem Cube at (0, 0.12, 0), color 0x85e8ff (0.522, 0.910, 1.0) */
    glPushMatrix();
    glTranslatef(0.0f, 0.12f, 0.0f);
    draw_faceted_gem_cube(0.522f, 0.910f, 1.0f);

    /* 2. Internal rotating Octahedron Core at (0, 0.12, 0), radius 0.78, solid white */
    glPushMatrix();
    glRotatef(s_core_rot_x * 57.29578f, 1.0f, 0.0f, 0.0f);
    glRotatef(s_core_rot_y * 57.29578f, 0.0f, 1.0f, 0.0f);
    draw_octahedron(0.78f, false, 1.0f, 1.0f, 1.0f);
    glPopMatrix();

    /* 3. Wireframe Octahedron at (0, 0.12, 0), radius 1.02, cyan 0x38bdf8 */
    glPushMatrix();
    glRotatef(s_wire_rot_x * 57.29578f, 1.0f, 0.0f, 0.0f);
    glRotatef(s_wire_rot_y * 57.29578f, 0.0f, 1.0f, 0.0f);
    draw_octahedron(1.02f, true, 0.220f, 0.741f, 0.973f);
    glPopMatrix();

    glPopMatrix(); /* End main cube & octahedra */



    /* 5. Mouth at (0, -0.06, 1.08) */
    glPushMatrix();
    glTranslatef(0.0f, -0.06f, 1.08f);
    draw_mouth(pose->smile_scale, pose->omouth_scale);
    glPopMatrix();

    /* 6. Anime Eyes */
    float blink_squish = 1.0f;
    if (s_is_blinking) {
        blink_squish = 1.0f - sinf(s_blink_progress * (float)M_PI) * 0.90f;
    }
    glPushMatrix();
    glTranslatef(-0.46f, 0.32f, 1.08f);
    draw_anime_eye(blink_squish, true);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(0.46f, 0.32f, 1.08f);
    draw_anime_eye(blink_squish, false);
    glPopMatrix();

    /* 7. Blush Cheeks at (-0.76, -0.06, 1.07) and (0.76, -0.06, 1.07) */
    glPushMatrix();
    glTranslatef(-0.76f, -0.06f, 1.07f);
    draw_cheek_blush();
    glPopMatrix();

    glPushMatrix();
    glTranslatef(0.76f, -0.06f, 1.07f);
    draw_cheek_blush();
    glPopMatrix();

    /* 8. Little Cube Hands at pivots (-1.26, 0.05, 0.1) and (1.26, 0.05, 0.1) */
    /* Left Hand */
    glPushMatrix();
    glTranslatef(-1.26f, 0.05f, 0.1f);
    glRotatef(pose->lhand_rot_x * 57.29578f, 1.0f, 0.0f, 0.0f);
    glRotatef(pose->lhand_rot_y * 57.29578f, 0.0f, 1.0f, 0.0f);
    glRotatef(pose->lhand_rot_z * 57.29578f, 0.0f, 0.0f, 1.0f);
    draw_box(0.46f, 0.46f, 0.46f, 0.565f, 0.929f, 1.0f);
    glPopMatrix();

    /* Right Hand */
    glPushMatrix();
    glTranslatef(1.26f, 0.05f, 0.1f);
    glRotatef(pose->rhand_rot_x * 57.29578f, 1.0f, 0.0f, 0.0f);
    glRotatef(pose->rhand_rot_y * 57.29578f, 0.0f, 1.0f, 0.0f);
    glRotatef(pose->rhand_rot_z * 57.29578f, 0.0f, 0.0f, 1.0f);
    draw_box(0.46f, 0.46f, 0.46f, 0.565f, 0.929f, 1.0f);
    glPopMatrix();

    glPopMatrix(); /* End Body Group */

    /* 9. Little Cube Feet (Direct children of rootGroup at pivots (-0.64, -0.92, 0.08)) */
    /* Left Foot */
    glPushMatrix();
    glTranslatef(pose->left_foot_x, pose->lfoot_y, pose->left_foot_z);
    glRotatef(pose->lfoot_rot_x * 57.29578f, 1.0f, 0.0f, 0.0f);
    glRotatef(pose->lfoot_rot_y * 57.29578f, 0.0f, 1.0f, 0.0f);
    glRotatef(pose->lfoot_rot_z * 57.29578f, 0.0f, 0.0f, 1.0f);
    glTranslatef(0.0f, -0.20f, 0.06f);
    draw_box(0.72f, 0.48f, 0.88f, 1.0f, 0.302f, 0.553f);
    glPopMatrix();

    /* Right Foot */
    glPushMatrix();
    glTranslatef(pose->right_foot_x, pose->rfoot_y, pose->right_foot_z);
    glRotatef(pose->rfoot_rot_x * 57.29578f, 1.0f, 0.0f, 0.0f);
    glRotatef(pose->rfoot_rot_y * 57.29578f, 0.0f, 1.0f, 0.0f);
    glRotatef(pose->rfoot_rot_z * 57.29578f, 0.0f, 0.0f, 1.0f);
    glTranslatef(0.0f, -0.20f, 0.06f);
    draw_box(0.72f, 0.48f, 0.88f, 1.0f, 0.302f, 0.553f);
    glPopMatrix();

    glPopMatrix(); /* End Root Group */
}

/* ── Kinematics & Pose Update Loop ─────────────────────────────────── */

static void update_kinematics(float dt) {
    s_time += dt;
    s_state_timer += dt;

    /* Silky, critically damped flywheel inertia & tilt low-pass filter (index.html lines 7671-7676) */
    s_circular_vel *= 0.968f;
    s_circular_phase += s_circular_vel;

    s_cur_rot_x += (s_target_rot_x - s_cur_rot_x) * 0.028f;
    s_cur_rot_y += (s_target_rot_y - s_cur_rot_y) * 0.028f;

    /* Drag inertia spring back to front (index.html lines 7153-7161) */
    if (!s_dragging) {
        s_cube_velx *= 0.90f;
        s_cube_vely *= 0.90f;
        s_cube_rotx += s_cube_velx;
        s_cube_roty += s_cube_vely;
        s_cube_rotx += (0.0f - s_cube_rotx) * 0.04f;
        s_cube_roty += (0.0f - s_cube_roty) * 0.04f;
    } else {
        /* While holding mouse button still, gently damp residual velocity */
        s_cube_velx *= 0.80f;
        s_cube_vely *= 0.80f;
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

    /* Inner core and wireframe octahedron rotation (1:1 with index.html lines 6095, 7234) */
    if (s_anim_state == ANIM_SING) {
        s_core_rot_x += dt * 3.5f;
        s_core_rot_y += dt * 5.0f;
    } else {
        s_core_rot_x += dt * 0.5f;
        s_core_rot_y += dt * 0.7f;
    }
    s_wire_rot_x += dt * -0.7f;
    s_wire_rot_y += dt * 0.5f;

    /* Autonomous animation progression: cycle smoothly when mascot is idle */
    if (s_anim_state == ANIM_IDLE && s_state_timer >= 3.8f) {
        static int s_auto_anim = 0;
        s_auto_anim = (s_auto_anim + 1) % 4;
        if (s_auto_anim == 0) set_animation(ANIM_WALK);
        else if (s_auto_anim == 1) set_animation(ANIM_FLOAT);
        else if (s_auto_anim == 2) set_animation(ANIM_DANCE);
        else set_animation(ANIM_SING);
    }

    /* Target pose goals */
    float t_root_x = 0.0f, t_root_y = 0.08f, t_root_z = 0.0f;
    float t_root_rot_x = 0.0f, t_root_rot_y = 0.0f, t_root_rot_z = 0.0f;
    float t_body_x = 0.0f, t_body_y = 0.0f, t_body_z = 0.0f;
    float t_body_rot_x = 0.0f, t_body_rot_y = 0.0f, t_body_rot_z = 0.0f;
    float t_scale_x = 1.0f, t_scale_y = 1.0f, t_scale_z = 1.0f;
    float t_lhand_rot_x = 0.0f, t_lhand_rot_y = 0.0f, t_lhand_rot_z = 0.10f;
    float t_rhand_rot_x = 0.0f, t_rhand_rot_y = 0.0f, t_rhand_rot_z = -0.10f;
    float t_left_foot_x = -0.64f, t_lfoot_y = -0.92f, t_left_foot_z = 0.08f;
    float t_lfoot_rot_x = 0.0f, t_lfoot_rot_y = 0.0f, t_lfoot_rot_z = 0.0f;
    float t_right_foot_x = 0.64f, t_rfoot_y = -0.92f, t_right_foot_z = 0.08f;
    float t_rfoot_rot_x = 0.0f, t_rfoot_rot_y = 0.0f, t_rfoot_rot_z = 0.0f;
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
            t_lfoot_rot_z = sway * 0.03f;
            t_rfoot_rot_z = -sway * 0.03f;
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
            t_lfoot_rot_z = 0.06f;

            t_rfoot_y = -0.92f + (-sin_w > 0.0f ? -sin_w : 0.0f) * 0.36f;
            t_rfoot_rot_x = -sin_w * 0.44f;
            t_rfoot_rot_z = -0.06f;

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
                /* Phase 1: Buoyant anticipation & gradual expansion (0 to 0.40s) */
                float p = s_float_timer / 0.40f;
                float ease_inhale = 0.5f - 0.5f * cosf(p * (float)M_PI);
                t_scale_x = 1.0f + ease_inhale * 0.30f;
                t_scale_y = 1.0f + ease_inhale * 0.28f;
                t_scale_z = 1.0f + ease_inhale * 0.30f;
                t_root_y = 0.08f + ease_inhale * 0.18f;
                t_smile_scale = 1.0f - ease_inhale;
                t_omouth_scale = ease_inhale * 1.25f;

                t_lhand_rot_z = 0.10f + ease_inhale * 0.30f;
                t_rhand_rot_z = -0.10f - ease_inhale * 0.30f;
                t_lfoot_y = -0.92f - ease_inhale * 0.12f;
                t_rfoot_y = -0.92f - ease_inhale * 0.12f;
            } else if (s_float_timer < 2.55f) {
                /* Phase 2: Aerial float & flutter kick (0.40s to 2.55s) */
                t_scale_x = 1.30f;
                t_scale_y = 1.28f;
                t_scale_z = 1.30f;
                t_smile_scale = 0.0f;
                t_omouth_scale = 1.25f;

                float hover = sinf((s_float_timer - 0.40f) * 3.4f);
                t_root_y = 0.92f + hover * 0.15f;

                t_lfoot_rot_x = sinf(s_float_timer * 9.0f) * 0.45f;
                t_lfoot_y = -1.04f;
                t_rfoot_rot_x = cosf(s_float_timer * 9.0f) * 0.45f;
                t_rfoot_y = -1.04f;

                t_lhand_rot_z = 0.40f + sinf(s_float_timer * 13.0f) * 0.35f;
                t_rhand_rot_z = -0.40f - sinf(s_float_timer * 13.0f) * 0.35f;
            } else if (s_float_timer < 3.35f) {
                /* Phase 3: Smooth exhale & gradual deceleration descent (2.55s to 3.35s) */
                float p = (s_float_timer - 2.55f) / 0.80f;
                float ease_descend = 0.5f - 0.5f * cosf(p * (float)M_PI);
                float flutter_damp = 1.0f - ease_descend;

                t_scale_x = 1.30f - ease_descend * 0.30f;
                t_scale_y = 1.28f - ease_descend * 0.28f;
                t_scale_z = 1.30f - ease_descend * 0.30f;
                t_root_y = 0.92f * (1.0f - ease_descend) + 0.08f;

                t_smile_scale = ease_descend;
                t_omouth_scale = 1.25f * (1.0f - ease_descend);

                t_lfoot_rot_x = sinf(s_float_timer * 9.0f * flutter_damp) * 0.45f * flutter_damp;
                t_rfoot_rot_x = cosf(s_float_timer * 9.0f * flutter_damp) * 0.45f * flutter_damp;
                t_lfoot_y = -1.04f + ease_descend * 0.12f;
                t_rfoot_y = -1.04f + ease_descend * 0.12f;

                t_lhand_rot_z = 0.40f * flutter_damp + 0.10f;
                t_rhand_rot_z = -0.40f * flutter_damp - 0.10f;
            } else {
                set_animation(ANIM_IDLE);
                s_impact_squash = 0.16f;
                lilcu64_shim_boing();
            }
            break;
        }
        case ANIM_DANCE: {
            s_dance_timer += dt;
            if (s_dance_timer < 0.9f) {
                /* Hop 1 (Left 360 spin): 0 to 0.9s */
                float p = s_dance_timer / 0.9f;
                float hop = sinf(p * (float)M_PI);
                t_root_y = 0.08f + hop * 0.78f;
                t_root_x = -0.42f * hop;
                t_body_rot_y = p * (float)M_PI * 2.0f;
                t_lfoot_y = -0.92f + hop * 0.22f;
                t_rfoot_y = -0.68f;
                t_lhand_rot_z = 0.75f * hop;
                t_rhand_rot_z = -0.75f * hop;
            } else if (s_dance_timer < 1.8f) {
                /* Hop 2 (Right 360 reverse spin): 0.9 to 1.8s */
                float p = (s_dance_timer - 0.9f) / 0.9f;
                float hop = sinf(p * (float)M_PI);
                t_root_y = 0.08f + hop * 0.78f;
                t_root_x = 0.42f * hop;
                t_body_rot_y = -p * (float)M_PI * 2.0f;
                t_lfoot_y = -0.68f;
                t_rfoot_y = -0.92f + hop * 0.22f;
                t_lhand_rot_z = -0.75f * hop;
                t_rhand_rot_z = 0.75f * hop;
            } else if (s_dance_timer < 2.7f) {
                /* Hop 3 (Celebratory backflip): 1.8 to 2.7s */
                float p = (s_dance_timer - 1.8f) / 0.9f;
                float hop = sinf(p * (float)M_PI);
                t_root_x = 0.0f;
                t_root_y = 0.08f + hop * 0.88f;
                t_body_rot_x = -p * (float)M_PI * 2.0f;
                t_lhand_rot_z = 0.35f + hop * 0.55f;
                t_rhand_rot_z = -0.35f - hop * 0.55f;
            } else if (s_dance_timer < 3.4f) {
                /* Triumphant victory pose hold & sparkle (2.7s to 3.4s) */
                t_root_x = 0.0f;
                t_root_y = 0.08f;
                t_body_rot_x = 0.0f;
                t_body_rot_y = 0.0f;
                t_scale_x = 1.08f;
                t_scale_y = 0.93f;
                t_scale_z = 1.08f;
                t_lfoot_rot_z = 0.32f;
                t_rfoot_rot_z = -0.32f;
                t_lhand_rot_z = 1.35f; /* Left hand raised high in victory! */
                t_lhand_rot_x = -0.15f;
                t_rhand_rot_z = -0.28f;

                if (s_dance_step == 0) {
                    s_dance_step = 1;
                    s_impact_squash = 0.18f;
                }
            } else if (s_dance_timer < 4.1f) {
                /* Organic smooth deceleration return to idle (3.4s to 4.1s) */
                float p = (s_dance_timer - 3.4f) / 0.70f;
                float s = 0.5f - 0.5f * cosf(p * (float)M_PI);

                t_root_x = 0.0f;
                t_root_y = 0.08f;
                t_body_rot_x = 0.0f;
                t_body_rot_y = 0.0f;

                t_scale_x = 1.08f * (1.0f - s) + 1.0f * s;
                t_scale_y = 0.93f * (1.0f - s) + 1.0f * s;
                t_scale_z = 1.08f * (1.0f - s) + 1.0f * s;

                t_lfoot_rot_z = 0.32f * (1.0f - s);
                t_rfoot_rot_z = -0.32f * (1.0f - s);
                t_lhand_rot_z = 1.35f * (1.0f - s) + 0.10f * s;
                t_lhand_rot_x = -0.15f * (1.0f - s);
                t_rhand_rot_z = -0.28f * (1.0f - s) - 0.10f * s;
            } else {
                set_animation(ANIM_IDLE);
            }
            break;
        }
        case ANIM_SING: {
            /* Two-phase singing chime (index.html lines 7488-7528) */
            float curve = 0.0f;
            if (s_state_timer < 0.44f) {
                float p = s_state_timer / 0.44f;
                curve = sinf(p * (float)M_PI);
            } else if (s_state_timer >= 0.68f && s_state_timer < 1.15f) {
                float p = (s_state_timer - 0.68f) / 0.47f;
                curve = sinf(p * (float)M_PI) * 0.85f;
            } else {
                curve = 0.0f;
            }

            t_root_rot_x = -curve * 0.25f;
            t_root_y = 0.08f + curve * 0.26f;
            t_scale_x = 1.0f + curve * 0.14f;
            t_scale_y = 1.0f + curve * 0.16f;
            t_scale_z = 1.0f + curve * 0.14f;

            t_lhand_rot_z = curve * 1.15f;
            t_lhand_rot_x = -curve * 0.30f;
            t_rhand_rot_z = -curve * 1.15f;
            t_rhand_rot_x = -curve * 0.30f;

            t_lfoot_y = -0.92f + curve * 0.10f;
            t_rfoot_y = -0.92f + curve * 0.10f;

            t_smile_scale = 1.0f - curve;
            t_omouth_scale = curve * 1.25f;
            s_sing_halo_curve = curve;

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
        case ANIM_SQUASH:
            s_impact_squash = 0.25f;
            lilcu64_shim_pulse_squash(1.0f, 1.0f);
            trigger_radiant_wave(false);
            set_animation(ANIM_IDLE);
            break;
        default:
            break;
    }

    /* Landing Squash Cushion */
    s_impact_squash *= expf(-12.0f * dt);
    t_scale_y -= s_impact_squash;
    t_scale_x += s_impact_squash * 0.5f;
    t_scale_z += s_impact_squash * 0.5f;

    /* Critically Damped Exponential Blend toward Target Pose (Zero Pops) */
    s_pose.root_x = smooth_damp(s_pose.root_x, t_root_x, 18.0f, dt);
    s_pose.root_y = smooth_damp(s_pose.root_y, t_root_y, 18.0f, dt);
    s_pose.root_z = smooth_damp(s_pose.root_z, t_root_z, 18.0f, dt);
    s_pose.root_rot_x = smooth_damp_angle(s_pose.root_rot_x, t_root_rot_x, 16.0f, dt);
    s_pose.root_rot_y = smooth_damp_angle(s_pose.root_rot_y, t_root_rot_y, 16.0f, dt);
    s_pose.root_rot_z = smooth_damp_angle(s_pose.root_rot_z, t_root_rot_z, 16.0f, dt);

    s_pose.body_x = smooth_damp(s_pose.body_x, t_body_x, 18.0f, dt);
    s_pose.body_y = smooth_damp(s_pose.body_y, t_body_y, 18.0f, dt);
    s_pose.body_z = smooth_damp(s_pose.body_z, t_body_z, 18.0f, dt);
    s_pose.body_rot_x = smooth_damp_angle(s_pose.body_rot_x, t_body_rot_x, 16.0f, dt);
    s_pose.body_rot_y = smooth_damp_angle(s_pose.body_rot_y, t_body_rot_y, 16.0f, dt);
    s_pose.body_rot_z = smooth_damp_angle(s_pose.body_rot_z, t_body_rot_z, 16.0f, dt);

    s_pose.scale_x = smooth_damp(s_pose.scale_x, t_scale_x, 22.0f, dt);
    s_pose.scale_y = smooth_damp(s_pose.scale_y, t_scale_y, 22.0f, dt);
    s_pose.scale_z = smooth_damp(s_pose.scale_z, t_scale_z, 22.0f, dt);

    s_pose.lhand_rot_x = smooth_damp_angle(s_pose.lhand_rot_x, t_lhand_rot_x, 15.0f, dt);
    s_pose.lhand_rot_y = smooth_damp_angle(s_pose.lhand_rot_y, t_lhand_rot_y, 15.0f, dt);
    s_pose.lhand_rot_z = smooth_damp_angle(s_pose.lhand_rot_z, t_lhand_rot_z, 15.0f, dt);

    s_pose.rhand_rot_x = smooth_damp_angle(s_pose.rhand_rot_x, t_rhand_rot_x, 15.0f, dt);
    s_pose.rhand_rot_y = smooth_damp_angle(s_pose.rhand_rot_y, t_rhand_rot_y, 15.0f, dt);
    s_pose.rhand_rot_z = smooth_damp_angle(s_pose.rhand_rot_z, t_rhand_rot_z, 15.0f, dt);

    s_pose.left_foot_x = smooth_damp(s_pose.left_foot_x, t_left_foot_x, 20.0f, dt);
    s_pose.lfoot_y = smooth_damp(s_pose.lfoot_y, t_lfoot_y, 20.0f, dt);
    s_pose.left_foot_z = smooth_damp(s_pose.left_foot_z, t_left_foot_z, 20.0f, dt);
    s_pose.lfoot_rot_x = smooth_damp_angle(s_pose.lfoot_rot_x, t_lfoot_rot_x, 20.0f, dt);
    s_pose.lfoot_rot_y = smooth_damp_angle(s_pose.lfoot_rot_y, t_lfoot_rot_y, 20.0f, dt);
    s_pose.lfoot_rot_z = smooth_damp_angle(s_pose.lfoot_rot_z, t_lfoot_rot_z, 20.0f, dt);

    s_pose.right_foot_x = smooth_damp(s_pose.right_foot_x, t_right_foot_x, 20.0f, dt);
    s_pose.rfoot_y = smooth_damp(s_pose.rfoot_y, t_rfoot_y, 20.0f, dt);
    s_pose.right_foot_z = smooth_damp(s_pose.right_foot_z, t_right_foot_z, 20.0f, dt);
    s_pose.rfoot_rot_x = smooth_damp_angle(s_pose.rfoot_rot_x, t_rfoot_rot_x, 20.0f, dt);
    s_pose.rfoot_rot_y = smooth_damp_angle(s_pose.rfoot_rot_y, t_rfoot_rot_y, 20.0f, dt);
    s_pose.rfoot_rot_z = smooth_damp_angle(s_pose.rfoot_rot_z, t_rfoot_rot_z, 20.0f, dt);

    s_pose.smile_scale = smooth_damp(s_pose.smile_scale, t_smile_scale, 24.0f, dt);
    s_pose.omouth_scale = smooth_damp(s_pose.omouth_scale, t_omouth_scale, 24.0f, dt);

    if (s_anim_state != ANIM_SING) {
        s_sing_halo_curve *= expf(-10.0f * dt);
        if (s_sing_halo_curve < 0.001f) s_sing_halo_curve = 0.0f;
    }

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
    char hint_buf[160];
    int active_t = lilcu64_synth_get_active_track();
    snprintf(hint_buf, sizeof(hint_buf),
             "[Space/P] Sacred Pulse & Sing  [W] Waddle  [F] Float  [D] Dance  [1/S] Trk 1 (%s)  [2/C] Trk 2  [3/H] Trk 3  [4/T] Trk 4  [M] Mute",
             active_t > 0 ? "ON" : "OFF");
    drw_tc_string(dev, 32, dev->height - 32, hint_buf, COLOR_WHITE, 0);
}

/* ── Window Paint & Render Callback ───────────────────────────────── */

static void demo_paint(WND *wnd, GDEV *dev) {
    if (!wnd || !dev || !s_surf) return;

    /* CRITICAL: restore EGL surface so OpenGL operations target this window's buffer */
    egl_make_current(s_surf);

    if (s_surf->width != dev->width || s_surf->height != dev->height) {
        egl_surface_resize(s_surf, dev->width, dev->height);
    }

    update_kinematics(s_dt);

    /* Pump VirtIO sound PCM buffer */
    lilcu64_synth_pump_virtio(735); /* ~16.6ms of audio at 44.1kHz */

    glViewport(0, 0, dev->width, dev->height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    /* 1:1 PerspectiveCamera(36, w/h, 0.1, 100) from index.html line 6300 */
    double aspect = (double)dev->width / (double)(dev->height > 0 ? dev->height : 1);
    glFrustum(-aspect * 0.3249, aspect * 0.3249, -0.3249, 0.3249, 1.0, 100.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    /* Camera at (0, 0.08, 9.2) looking at (0, 0.08, 0) */
    glTranslatef(0.0f, -0.08f, -9.2f);

    glClearColor(0.035f, 0.045f, 0.085f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glPushMatrix();

    /* 1. Render 4D Hopf Fibration Background */
    render_hopf_fibration(s_time, s_density_mode);

    /* 2. Render Spherical Radiant Pulse Waves */
    render_radiant_waves();

    /* 3. Render Lil Cu 64 Mascot (1:1 with index.html) */
    render_lilcu64(&s_pose, s_time);

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
            set_animation(ANIM_SING);
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
        } else if (evt->key == 0x1C || evt->key == '<' || evt->key == ',') { /* Prev density mode */
            s_density_mode = (s_density_mode + 2) % 3;
            lilcu64_shim_ding(1318.51f, 0.7f, 0.4f);
        } else if (evt->key == 0x1D || evt->key == '>' || evt->key == '.') { /* Next density mode */
            s_density_mode = (s_density_mode + 1) % 3;
            lilcu64_shim_ding(1760.0f, 0.7f, 0.4f);
        }
        inval_wnd(wnd);

    } else if (evt->type == EV_BUT_DOWN) {
        s_dragging = 1;
        s_prev_drag_x = evt->pos.x;
        s_prev_drag_y = evt->pos.y;
        s_cube_velx = 0.0f;
        s_cube_vely = 0.0f;

        /* Click on mascot triggers poke reaction */
        if (evt->pos.y > 100 && evt->pos.y < wnd->client.bottom - 100) {
            set_animation(ANIM_POKE);
        }
    } else if (evt->type == EV_BUT_UP) {
        s_dragging = 0;
        s_cube_velx *= 0.5f;
        s_cube_vely *= 0.5f;
        s_last_pointer_angle = -999.0f;
    } else if (evt->type == EV_MOUSE_MOVE) {
        float cx = (float)wnd->client.left + (float)(wnd->client.right - wnd->client.left) * 0.5f;
        float cy = (float)wnd->client.top + (float)(wnd->client.bottom - wnd->client.top) * 0.38f;
        float width = (float)(wnd->client.right - wnd->client.left);
        float height = (float)(wnd->client.bottom - wnd->client.top);
        if (width <= 0.0f) width = 720.0f;
        if (height <= 0.0f) height = 540.0f;

        float dx = (float)evt->pos.x - cx;
        float dy = (float)evt->pos.y - cy;
        float dist = sqrtf(dx * dx + dy * dy);
        float angle = atan2f(dy, dx);

        /* 1. Gaze tracking for Lil Cube (index.html lines 6971-6972) */
        s_target_look_x = dx / 320.0f;
        if (s_target_look_x < -0.45f) s_target_look_x = -0.45f;
        if (s_target_look_x >  0.45f) s_target_look_x =  0.45f;
        s_target_look_y = -(dy / 320.0f);
        if (s_target_look_y < -0.35f) s_target_look_y = -0.35f;
        if (s_target_look_y >  0.35f) s_target_look_y =  0.35f;

        /* 2. 3D Tilt for Hopf Fibration Background (index.html lines 6975-6978) */
        float max_radius = (width < height ? width : height) * 0.45f;
        float norm_dist = dist / (max_radius > 80.0f ? max_radius : 80.0f);
        if (norm_dist > 1.0f) norm_dist = 1.0f;

        s_target_rot_x = (dy / (height * 0.5f)) * 0.22f + 0.22f;
        s_target_rot_y = (dx / (width * 0.5f)) * 0.35f;

        /* 3. Circular Swirl Detection for 4D Hopf Acceleration (index.html lines 6993-7011) */
        if (s_last_pointer_angle > -900.0f) {
            float d_theta = angle - s_last_pointer_angle;
            while (d_theta > (float)M_PI) d_theta -= (float)(M_PI * 2.0);
            while (d_theta < -(float)M_PI) d_theta += (float)(M_PI * 2.0);

            if (fabsf(d_theta) > 0.008f) {
                float clamped_d_theta = d_theta;
                if (clamped_d_theta > 0.25f) clamped_d_theta = 0.25f;
                if (clamped_d_theta < -0.25f) clamped_d_theta = -0.25f;
                s_circular_vel += clamped_d_theta * 0.35f * norm_dist;
                if (s_circular_vel > 0.12f) s_circular_vel = 0.12f;
                if (s_circular_vel < -0.12f) s_circular_vel = -0.12f;
            }
        }
        s_last_pointer_angle = angle;

        /* 4. Smooth 3D Drag if mascot is grabbed (index.html lines 6981-6990) */
        if (s_dragging) {
            float dragDeltaX = (float)(evt->pos.x - s_prev_drag_x);
            float dragDeltaY = (float)(evt->pos.y - s_prev_drag_y);

            /* Ergonomically calibrated drag sensitivities */
            s_cube_vely = dragDeltaX * 0.005f;
            s_cube_velx = dragDeltaY * 0.004f;

            if (s_cube_vely > 0.04f) s_cube_vely = 0.04f;
            if (s_cube_vely < -0.04f) s_cube_vely = -0.04f;
            if (s_cube_velx > 0.04f) s_cube_velx = 0.04f;
            if (s_cube_velx < -0.04f) s_cube_velx = -0.04f;

            s_cube_roty += s_cube_vely;
            s_cube_rotx += s_cube_velx;

            s_prev_drag_x = evt->pos.x;
            s_prev_drag_y = evt->pos.y;
        }
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

    /* Initialize procedural synthesizer & start Track 1 */
    lilcu64_synth_init(LILCU64_AUDIO_RATE);
    lilcu64_synth_play_track(1);

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
