/*
 * src/quake/app/quake_app.c — Quake 3D Desktop Application for B-System
 *
 * Runs Quake inside a native B-System Window (WND) with EGL-lite presentation
 * directly into the Workbench compositor. Driven by T-Kernel scheduler task.
 *
 * Shutdown safety: quake_destroy() sets s_quake_wnd = NULL then wakes the
 * animation task exactly once; the task checks the flag, exits its loop, and
 * calls Quake_Shutdown() which drains all transient hunk state before
 * letting the task stack be reclaimed by del_tsk().
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#include "../include/btron_quake.h"
#include "../include/quakedef.h"
#include "../include/render.h"
#include "../include/world.h"
#include "../include/texture.h"
#include "../include/r_light.h"
#include "../include/r_alias.h"
#include "../include/fs_btron.h"
#include "../../gl/gl_dispatch.h"
#include "../../gl/egl_surface.h"

#include <btron/wnd.h>
#include <btron/itron.h>
#include <btron/btron.h>

extern void uart_puts_raw(const char *s);
extern void IN_Btron_HandleEvent(WND *wnd, const EVT *evt);

/* ── Module state ─────────────────────────────────────────────────── */
static WND         *s_quake_wnd    = NULL;
static EGL_SURFACE *s_surf         = NULL;
static ID           s_quake_tskid  = 0;
static int          s_initialised  = 0;  /* FS_Init / R_Init guard */
static int          s_shutdown_req = 0;  /* Set by quake_destroy, read by task */

/* ── Proper engine shutdown ───────────────────────────────────────── */
static void Quake_Shutdown(void) {
    /* Tear down world data — clears g_world and entity string copy */
    World_UnloadMap();

    /* Invalidate the BSP pre-cache so FS_CacheBSP re-reads on next launch */
    memset(&g_bsp_cache, 0, sizeof(g_bsp_cache));

    /* Clear texture table — GL objects are already gone with the surface */
    g_num_gl_textures = 0;

    /* Mark engine as uninitialised — FS_Init will re-run on next launch */
    s_initialised  = 0;
    s_shutdown_req = 0;

    uart_puts_raw("[QUAKE] Engine shutdown complete\n");
}

/* ── T-Kernel animation task ──────────────────────────────────────── */
static void quake_task_fn(VW exinf) {
    (void)exinf;
    /*
     * Drive ~60 FPS repaints while the window is alive.
     * When quake_destroy() sets s_quake_wnd = NULL and s_shutdown_req = 1
     * we exit cleanly, run shutdown, then the task returns.
     */
    while (!s_shutdown_req) {
        if (s_quake_wnd) {
            SV_ServerFrame(1.0f / 60.0f); /* advance physics 1 tick */
            inval_wnd(s_quake_wnd);
        }
        dly_tsk(16);  /* ~60 FPS */
    }

    /* Window is gone — safe to tear down engine state here */
    Quake_Shutdown();
    s_quake_tskid = 0;
    /* Task naturally exits by returning */
}

/* ── Paint callback (called by compositor on inval_wnd) ──────────── */
static void quake_paint(WND *wnd, GDEV *dev) {
    /* Guard against paint arriving after destroy (race between task & compositor) */
    if (!wnd || !dev || !s_surf || s_shutdown_req) return;

    egl_make_current(s_surf);

    if (s_surf->width != dev->width || s_surf->height != dev->height) {
        egl_surface_resize(s_surf, dev->width, dev->height);
        /* Only re-init the GL viewport/projection, NOT the full engine */
        R_Resize(dev->width, dev->height);
    }

    R_BeginFrame();
    R_RenderView();
    R_EndFrame();

    egl_swap_buffers(s_surf);
}

#include "../include/quake_ui.h"

/* ── Input event callback ─────────────────────────────────────────── */
static void quake_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt || s_shutdown_req) return;

    if (evt->type == EV_KEY_DOWN) {
        if (UI_HandleKey(evt->key)) {
            inval_wnd(wnd);
            return;
        }
    } else if (evt->type == EV_BUT_DOWN) {
        int lx = evt->pos.x - wnd->client.left;
        int ly = evt->pos.y - wnd->client.top;
        if (UI_HandleMouse(lx, ly, 1)) {
            inval_wnd(wnd);
            return;
        }
    }

    IN_Btron_HandleEvent(wnd, evt);
    inval_wnd(wnd);
}

/* ── Destroy callback (compositor thread, on window close) ─────────── */
static void quake_destroy(WND *wnd) {
    (void)wnd;

    /* Signal animation task to stop and run shutdown */
    s_shutdown_req = 1;
    s_quake_wnd    = NULL;

    /* Destroy EGL surface — must happen on compositor side */
    if (s_surf) {
        egl_destroy_surface(s_surf);
        s_surf = NULL;
    }

    if (s_quake_tskid > 0) {
        wup_tsk(s_quake_tskid);
    }
}

/* ── Public API ───────────────────────────────────────────────────── */

WND *open_quake_window(int x, int y, int width, int height) {
    /* Prevent double-open */
    if (s_quake_wnd) {
        top_wnd(s_quake_wnd);
        return s_quake_wnd;
    }

    if (width  <= 0) width  = 480;
    if (height <= 0) height = 360;

    s_shutdown_req = 0;

    s_quake_wnd = opn_wnd("Quake - OpenGL ES 1.1",
                          x > 0 ? x : 120, y > 0 ? y : 40,
                          width, height,
                          WND_ATTR_TITLE | WND_ATTR_BORDER |
                          WND_ATTR_CLOSE | WND_ATTR_RESIZE);
    if (!s_quake_wnd) {
        uart_puts_raw("[QUAKE] ERROR: opn_wnd failed\n");
        return NULL;
    }

    s_quake_wnd->paint         = quake_paint;
    s_quake_wnd->event_handler = quake_event;
    s_quake_wnd->destroy       = quake_destroy;

    s_surf = egl_create_window_surface(s_quake_wnd);
    if (!s_surf) {
        uart_puts_raw("[QUAKE] ERROR: egl_create_window_surface failed\n");
        cls_wnd(s_quake_wnd);
        s_quake_wnd = NULL;
        return NULL;
    }

    /* ── One-shot engine initialisation ──────────────────────────── */
    if (!s_initialised) {
        FS_Init();
        R_Init(width, height);
        /* Spawn all entities & start Quake server */
        SV_SpawnServer("maps/e1m1.bsp");
        UI_Init();
        s_initialised = 1;
    }

    /* ── Start animation task ────────────────────────────────────── */
    T_CTSK ctsk;
    ctsk.exinf   = 0;
    ctsk.tskatr  = TA_HLNG;
    ctsk.task    = quake_task_fn;
    ctsk.itskpri = 10;
    ctsk.stksz   = 16384;

    s_quake_tskid = cre_tsk(&ctsk);
    if (s_quake_tskid > 0) {
        sta_tsk(s_quake_tskid, 0);
        uart_puts_raw("[QUAKE] Quake 3D engine task started (~60 FPS)\n");
    }

    uart_puts_raw("[QUAKE] Window opened successfully\n");
    return s_quake_wnd;
}

void close_quake_window(void) {
    if (s_quake_wnd) {
        cls_wnd(s_quake_wnd);
    }
}

int quake_is_running(void) {
    return (s_quake_wnd != NULL);
}
