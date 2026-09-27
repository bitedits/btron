/*
 * src/quake/sys/in_btron.c — Input Event Translation for Quake in B-System
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#include "../include/quakedef.h"
#include "../include/render.h"
#include "../include/mathlib.h"
#include <btron/event.h>
#include <btron/wnd.h>

/* Movement keys state */
int in_forward    = 0;
int in_back       = 0;
int in_left       = 0;
int in_right      = 0;
int in_jump       = 0;
int in_attack     = 0;
int in_turn_left  = 0;
int in_turn_right = 0;

static int s_mouse_down = 0;
static H   s_last_mx    = 0;
static H   s_last_my    = 0;

void IN_Btron_HandleEvent(WND *wnd, const EVT *evt) {
    (void)wnd;
    if (!evt) return;

    switch (evt->type) {
    case EV_BUT_DOWN:
        s_mouse_down = 1;
        s_last_mx = evt->pos.x;
        s_last_my = evt->pos.y;
        in_attack = 1; /* Left click fires weapon */
        break;

    case EV_BUT_UP:
        s_mouse_down = 0;
        in_attack = 0;
        break;

    case EV_MOUSE_MOVE:
        if (s_mouse_down) {
            float dx = (float)(evt->pos.x - s_last_mx);
            float dy = (float)(evt->pos.y - s_last_my);
            s_last_mx = evt->pos.x;
            s_last_my = evt->pos.y;

            r_refdef.viewangles[1] -= dx * 0.4f; /* Yaw */
            r_refdef.viewangles[0] += dy * 0.4f; /* Pitch */

            /* Clamp pitch */
            if (r_refdef.viewangles[0] > 70.0f)  r_refdef.viewangles[0] = 70.0f;
            if (r_refdef.viewangles[0] < -70.0f) r_refdef.viewangles[0] = -70.0f;

            /* Wrap yaw */
            while (r_refdef.viewangles[1] < 0.0f)   r_refdef.viewangles[1] += 360.0f;
            while (r_refdef.viewangles[1] >= 360.0f) r_refdef.viewangles[1] -= 360.0f;
        }
        break;

    case EV_KEY_DOWN: {
        UW code = evt->key;
        if (code == 'w' || code == 'W') in_forward = 1;
        else if (code == 's' || code == 'S') in_back = 1;
        else if (code == 'a' || code == 'A') in_left = 1;
        else if (code == 'd' || code == 'D') in_right = 1;
        else if (code == 0xFF52 || code == 0x01) in_forward = 1;   /* Up Arrow */
        else if (code == 0xFF54 || code == 0x02) in_back = 1;      /* Down Arrow */
        else if (code == 0xFF51 || code == 0x04) in_turn_left = 1; /* Left Arrow */
        else if (code == 0xFF53 || code == 0x03) in_turn_right = 1;/* Right Arrow */
        else if (code == ' ') in_jump = 1;
        else if (code == '\r' || code == '\n' || code == 0x11 || code == 'e' || code == 'E') in_attack = 1;
        break;
    }

    case EV_KEY_UP: {
        UW code = evt->key;
        if (code == 'w' || code == 'W') in_forward = 0;
        else if (code == 's' || code == 'S') in_back = 0;
        else if (code == 'a' || code == 'A') in_left = 0;
        else if (code == 'd' || code == 'D') in_right = 0;
        else if (code == 0xFF52 || code == 0x01) in_forward = 0;
        else if (code == 0xFF54 || code == 0x02) in_back = 0;
        else if (code == 0xFF51 || code == 0x04) in_turn_left = 0;
        else if (code == 0xFF53 || code == 0x03) in_turn_right = 0;
        else if (code == ' ') in_jump = 0;
        else if (code == '\r' || code == '\n' || code == 0x11 || code == 'e' || code == 'E') in_attack = 0;
        break;
    }

    default:
        break;
    }
}
