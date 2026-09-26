/*
 * src/apps/glgears.c — OpenGL ES glgears Desktop Application for B-System
 *
 * Authentic 3D gear train animation ported to B-System window manager (WND).
 * Renders via TinyGL software rasterizer directly into window client pixmap.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "glgears.h"
#include "../gl/gl_dispatch.h"
#include "../gl/egl_surface.h"
#include <math.h>
#include <btron/btron.h>

extern void uart_puts_raw(const char *s);

static WND         *s_glgears_wnd = NULL;
static EGL_SURFACE *s_surf        = NULL;

static GLuint gear1 = 0, gear2 = 0, gear3 = 0;
static GLfloat angle = 0.0f;
static GLfloat view_rotx = 20.0f, view_roty = 30.0f;
static int s_drag_active = 0;
static H s_last_mx = 0, s_last_my = 0;

/* ── Gear geometry builder ───────────────────────────────────────── */

static void build_gear(GLfloat inner_radius, GLfloat outer_radius, GLfloat width,
                       GLint teeth, GLfloat tooth_depth) {
    GLint i;
    GLfloat r0 = inner_radius;
    GLfloat r1 = outer_radius - tooth_depth / 2.0f;
    GLfloat r2 = outer_radius + tooth_depth / 2.0f;
    GLfloat da = 2.0f * (GLfloat)M_PI / (GLfloat)teeth / 4.0f;

    glNormal3f(0.0f, 0.0f, 1.0f);

    /* Draw front face */
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i <= teeth; i++) {
        GLfloat a = (GLfloat)i * 2.0f * (GLfloat)M_PI / (GLfloat)teeth;
        glVertex3f(r0 * (GLfloat)cos(a), r0 * (GLfloat)sin(a), width * 0.5f);
        glVertex3f(r1 * (GLfloat)cos(a), r1 * (GLfloat)sin(a), width * 0.5f);
        glVertex3f(r0 * (GLfloat)cos(a), r0 * (GLfloat)sin(a), width * 0.5f);
        glVertex3f(r1 * (GLfloat)cos(a + 3.0f * da), r1 * (GLfloat)sin(a + 3.0f * da), width * 0.5f);
    }
    glEnd();

    /* Draw front sides of teeth */
    glBegin(GL_QUADS);
    for (i = 0; i < teeth; i++) {
        GLfloat a = (GLfloat)i * 2.0f * (GLfloat)M_PI / (GLfloat)teeth;
        glVertex3f(r1 * (GLfloat)cos(a), r1 * (GLfloat)sin(a), width * 0.5f);
        glVertex3f(r2 * (GLfloat)cos(a + da), r2 * (GLfloat)sin(a + da), width * 0.5f);
        glVertex3f(r2 * (GLfloat)cos(a + 2.0f * da), r2 * (GLfloat)sin(a + 2.0f * da), width * 0.5f);
        glVertex3f(r1 * (GLfloat)cos(a + 3.0f * da), r1 * (GLfloat)sin(a + 3.0f * da), width * 0.5f);
    }
    glEnd();

    glNormal3f(0.0f, 0.0f, -1.0f);

    /* Draw back face */
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i <= teeth; i++) {
        GLfloat a = (GLfloat)i * 2.0f * (GLfloat)M_PI / (GLfloat)teeth;
        glVertex3f(r1 * (GLfloat)cos(a), r1 * (GLfloat)sin(a), -width * 0.5f);
        glVertex3f(r0 * (GLfloat)cos(a), r0 * (GLfloat)sin(a), -width * 0.5f);
        glVertex3f(r1 * (GLfloat)cos(a + 3.0f * da), r1 * (GLfloat)sin(a + 3.0f * da), -width * 0.5f);
        glVertex3f(r0 * (GLfloat)cos(a), r0 * (GLfloat)sin(a), -width * 0.5f);
    }
    glEnd();

    /* Draw back sides of teeth */
    glBegin(GL_QUADS);
    for (i = 0; i < teeth; i++) {
        GLfloat a = (GLfloat)i * 2.0f * (GLfloat)M_PI / (GLfloat)teeth;
        glVertex3f(r1 * (GLfloat)cos(a + 3.0f * da), r1 * (GLfloat)sin(a + 3.0f * da), -width * 0.5f);
        glVertex3f(r2 * (GLfloat)cos(a + 2.0f * da), r2 * (GLfloat)sin(a + 2.0f * da), -width * 0.5f);
        glVertex3f(r2 * (GLfloat)cos(a + da), r2 * (GLfloat)sin(a + da), -width * 0.5f);
        glVertex3f(r1 * (GLfloat)cos(a), r1 * (GLfloat)sin(a), -width * 0.5f);
    }
    glEnd();

    /* Draw outward faces of teeth */
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i < teeth; i++) {
        GLfloat a = (GLfloat)i * 2.0f * (GLfloat)M_PI / (GLfloat)teeth;
        GLfloat u, v, len;

        glVertex3f(r1 * (GLfloat)cos(a), r1 * (GLfloat)sin(a), width * 0.5f);
        glVertex3f(r1 * (GLfloat)cos(a), r1 * (GLfloat)sin(a), -width * 0.5f);
        u = r2 * (GLfloat)cos(a + da) - r1 * (GLfloat)cos(a);
        v = r2 * (GLfloat)sin(a + da) - r1 * (GLfloat)sin(a);
        len = (GLfloat)sqrt(u * u + v * v);
        u /= len;
        v /= len;
        glNormal3f(v, -u, 0.0f);
        glVertex3f(r2 * (GLfloat)cos(a + da), r2 * (GLfloat)sin(a + da), width * 0.5f);
        glVertex3f(r2 * (GLfloat)cos(a + da), r2 * (GLfloat)sin(a + da), -width * 0.5f);
        glNormal3f((GLfloat)cos(a), (GLfloat)sin(a), 0.0f);
        glVertex3f(r2 * (GLfloat)cos(a + 2.0f * da), r2 * (GLfloat)sin(a + 2.0f * da), width * 0.5f);
        glVertex3f(r2 * (GLfloat)cos(a + 2.0f * da), r2 * (GLfloat)sin(a + 2.0f * da), -width * 0.5f);
        u = r1 * (GLfloat)cos(a + 3.0f * da) - r2 * (GLfloat)cos(a + 2.0f * da);
        v = r1 * (GLfloat)sin(a + 3.0f * da) - r2 * (GLfloat)sin(a + 2.0f * da);
        glNormal3f(v, -u, 0.0f);
        glVertex3f(r1 * (GLfloat)cos(a + 3.0f * da), r1 * (GLfloat)sin(a + 3.0f * da), width * 0.5f);
        glVertex3f(r1 * (GLfloat)cos(a + 3.0f * da), r1 * (GLfloat)sin(a + 3.0f * da), -width * 0.5f);
        glNormal3f((GLfloat)cos(a), (GLfloat)sin(a), 0.0f);
    }
    glVertex3f(r1 * (GLfloat)cos(0.0), r1 * (GLfloat)sin(0.0), width * 0.5f);
    glVertex3f(r1 * (GLfloat)cos(0.0), r1 * (GLfloat)sin(0.0), -width * 0.5f);
    glEnd();

    /* Draw inside radius cylinder */
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i <= teeth; i++) {
        GLfloat a = (GLfloat)i * 2.0f * (GLfloat)M_PI / (GLfloat)teeth;
        glNormal3f(-(GLfloat)cos(a), -(GLfloat)sin(a), 0.0f);
        glVertex3f(r0 * (GLfloat)cos(a), r0 * (GLfloat)sin(a), -width * 0.5f);
        glVertex3f(r0 * (GLfloat)cos(a), r0 * (GLfloat)sin(a), width * 0.5f);
    }
    glEnd();
}

static void init_gears_scene(void) {
    static const GLfloat pos[4]   = { 5.0f, 5.0f, 10.0f, 0.0f };
    static const GLfloat red[4]   = { 0.85f, 0.15f, 0.15f, 1.0f };
    static const GLfloat green[4] = { 0.15f, 0.75f, 0.20f, 1.0f };
    static const GLfloat blue[4]  = { 0.20f, 0.35f, 0.85f, 1.0f };
    static const GLfloat white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, white);
    glLightfv(GL_LIGHT0, GL_SPECULAR, white);
    glEnable(GL_CULL_FACE);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_DEPTH_TEST);
    glShadeModel(GL_SMOOTH);

    /* Gear 1 (Red) */
    gear1 = glGenLists(1);
    glNewList(gear1, GL_COMPILE);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, red);
    build_gear(1.0f, 4.0f, 1.0f, 20, 0.7f);
    glEndList();

    /* Gear 2 (Green) */
    gear2 = glGenLists(1);
    glNewList(gear2, GL_COMPILE);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, green);
    build_gear(0.5f, 2.0f, 2.0f, 10, 0.7f);
    glEndList();

    /* Gear 3 (Blue) */
    gear3 = glGenLists(1);
    glNewList(gear3, GL_COMPILE);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, blue);
    build_gear(1.3f, 2.0f, 0.5f, 10, 0.7f);
    glEndList();

    uart_puts_raw("[GL] glgears: gear geometry built\n");
}

/* ── Window callbacks ────────────────────────────────────────────── */

static void glgears_paint(WND *wnd, GDEV *dev) {
    if (!wnd || !dev || !s_surf) return;

    if (s_surf->width != dev->width || s_surf->height != dev->height) {
        egl_surface_resize(s_surf, dev->width, dev->height);
    }

    glViewport(0, 0, dev->width, dev->height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    double aspect = (double)dev->width / (double)(dev->height > 0 ? dev->height : 1);
    glFrustum(-aspect, aspect, -1.0, 1.0, 5.0, 60.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -40.0f);

    glClearColor(0.06f, 0.08f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glPushMatrix();
    glRotatef(view_rotx, 1.0f, 0.0f, 0.0f);
    glRotatef(view_roty, 0.0f, 1.0f, 0.0f);

    /* Gear 1 */
    glPushMatrix();
    glTranslatef(-3.0f, -2.0f, 0.0f);
    glRotatef(angle, 0.0f, 0.0f, 1.0f);
    glCallList(gear1);
    glPopMatrix();

    /* Gear 2 */
    glPushMatrix();
    glTranslatef(3.1f, -2.0f, 0.0f);
    glRotatef(-2.0f * angle - 9.0f, 0.0f, 0.0f, 1.0f);
    glCallList(gear2);
    glPopMatrix();

    /* Gear 3 */
    glPushMatrix();
    glTranslatef(-3.1f, 4.2f, 0.0f);
    glRotatef(-2.0f * angle - 25.0f, 0.0f, 0.0f, 1.0f);
    glCallList(gear3);
    glPopMatrix();

    glPopMatrix();

    /* Swap / present and mark invalid for continuous animation */
    egl_swap_buffers(s_surf);
    angle += 2.0f;
}

static void glgears_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;

    if (evt->type == EV_KEY_DOWN) {
        if (evt->key == 0x1B || evt->key == 'q' || evt->key == 'Q') {
            cls_wnd(wnd);
        }
    } else if (evt->type == EV_BUT_DOWN) {
        s_drag_active = 1;
        s_last_mx = evt->pos.x;
        s_last_my = evt->pos.y;
    } else if (evt->type == EV_BUT_UP) {
        s_drag_active = 0;
    } else if (evt->type == EV_MOUSE_MOVE && s_drag_active) {
        view_roty += (GLfloat)(evt->pos.x - s_last_mx);
        view_rotx += (GLfloat)(evt->pos.y - s_last_my);
        s_last_mx = evt->pos.x;
        s_last_my = evt->pos.y;
        inval_wnd(wnd);
    }
}

static void glgears_destroy(WND *wnd) {
    (void)wnd;
    if (s_surf) {
        egl_destroy_surface(s_surf);
        s_surf = NULL;
    }
    s_glgears_wnd = NULL;
}

WND* open_glgears_window(void) {
    if (s_glgears_wnd) {
        top_wnd(s_glgears_wnd);
        return s_glgears_wnd;
    }

    s_glgears_wnd = opn_wnd("glgears - OpenGL ES 1.1", 340, 60, 320, 240,
                            WND_ATTR_TITLE | WND_ATTR_BORDER | WND_ATTR_CLOSE | WND_ATTR_RESIZE);
    if (!s_glgears_wnd) {
        uart_puts_raw("[GL] ERROR: open_glgears_window failed\n");
        return NULL;
    }

    s_glgears_wnd->paint = glgears_paint;
    s_glgears_wnd->event_handler = glgears_event;
    s_glgears_wnd->destroy = glgears_destroy;

    s_surf = egl_create_window_surface(s_glgears_wnd);
    if (!s_surf) {
        uart_puts_raw("[GL] ERROR: egl_create_window_surface failed\n");
        cls_wnd(s_glgears_wnd);
        return NULL;
    }

    init_gears_scene();

    uart_puts_raw("[GL] glgears window open\n");
    return s_glgears_wnd;
}
