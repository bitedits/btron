/*
 * src/quake/sys/in_btron.c — Input Event Translation for Quake in B-System
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#include "../include/quakedef.h"
#include "../include/render.h"
#include "../include/mathlib.h"
#include "../include/quake_ui.h"
#include <btron/event.h>
#include <btron/wnd.h>

/* Movement keys state */
int in_forward    = 0;
int in_back       = 0;
int in_left       = 0;
int in_right      = 0;
int in_down       = 0;
int in_jump       = 0;
int in_attack     = 0;
int in_turn_left  = 0;
int in_turn_right = 0;

extern int g_menu_active;
extern int g_console_active;
extern int g_replay_active;

static int s_mouse_init = 0;
static H   s_last_mx    = 0;
static H   s_last_my    = 0;

void IN_Btron_HandleEvent(WND *wnd, const EVT *evt) {
    (void)wnd;
    if (!evt) return;

    if (g_replay_active) {
        in_forward = in_back = in_left = in_right = in_down = in_jump = in_attack = 0;
        in_turn_left = in_turn_right = 0;
        return;
    }

    switch (evt->type) {
    case EV_BUT_DOWN:
        if (evt->button == 3) {
            in_jump = 1;   /* Right click = Jump */
        } else {
            in_attack = 1; /* Left click = Fire weapon */
        }
        break;

    case EV_BUT_UP:
        if (evt->button == 3) {
            in_jump = 0;
        } else {
            in_attack = 0;
        }
        break;

    case EV_MOUSE_MOVE:
        if (!s_mouse_init) {
            s_last_mx = evt->pos.x;
            s_last_my = evt->pos.y;
            s_mouse_init = 1;
            break;
        }
        {
            float dx = (float)(evt->pos.x - s_last_mx);
            float dy = (float)(evt->pos.y - s_last_my);
            s_last_mx = evt->pos.x;
            s_last_my = evt->pos.y;

            if (!g_menu_active && !g_console_active) {
                if (dx > 80.0f)  dx = 80.0f;
                if (dx < -80.0f) dx = -80.0f;
                if (dy > 80.0f)  dy = 80.0f;
                if (dy < -80.0f) dy = -80.0f;

                r_refdef.viewangles[1] -= dx * 0.35f; /* Smooth Yaw */
                r_refdef.viewangles[0] += dy * 0.35f; /* Smooth Pitch */

                /* Clamp pitch */
                if (r_refdef.viewangles[0] > 70.0f)  r_refdef.viewangles[0] = 70.0f;
                if (r_refdef.viewangles[0] < -70.0f) r_refdef.viewangles[0] = -70.0f;

                /* Wrap yaw */
                while (r_refdef.viewangles[1] < 0.0f)   r_refdef.viewangles[1] += 360.0f;
                while (r_refdef.viewangles[1] >= 360.0f) r_refdef.viewangles[1] -= 360.0f;
            }
        }
        break;

    case EV_KEY_DOWN: {
        UW code = evt->key;
        if (code == 'w' || code == 'W') in_forward = 1;
        else if (code == 's' || code == 'S') in_back = 1;
        else if (code == 'a' || code == 'A') in_left = 1;
        else if (code == 'd' || code == 'D') in_right = 1;
        else if (code == 'c' || code == 'C') in_down = 1;
        else if (code == BTRON_KEY_UP || code == 0xFF52 || code == 0x01) in_forward = 1;
        else if (code == BTRON_KEY_DOWN || code == 0xFF54 || code == 0x02) in_back = 1;
        else if (code == BTRON_KEY_LEFT || code == 0xFF51 || code == 0x04) in_turn_left = 1;
        else if (code == BTRON_KEY_RIGHT || code == 0xFF53 || code == 0x03) in_turn_right = 1;
        else if (code == ' ' || code == BTRON_KEY_SPACE) in_jump = 1;
        else if (code == '\r' || code == '\n' || code == 0x11 || code == 'e' || code == 'E' ||
                 code == BTRON_KEY_RETURN || code == BTRON_KEY_KP_ENTER) in_attack = 1;
        else if (code == 0x12) in_jump = 1; /* Alt = Jump */
        else if (code >= '1' && code <= '8') UI_RequestWeapon(code - '0');
        break;
    }

    case EV_KEY_UP: {
        UW code = evt->key;
        if (code == 'w' || code == 'W') in_forward = 0;
        else if (code == 's' || code == 'S') in_back = 0;
        else if (code == 'a' || code == 'A') in_left = 0;
        else if (code == 'd' || code == 'D') in_right = 0;
        else if (code == 'c' || code == 'C') in_down = 0;
        else if (code == BTRON_KEY_UP || code == 0xFF52 || code == 0x01) in_forward = 0;
        else if (code == BTRON_KEY_DOWN || code == 0xFF54 || code == 0x02) in_back = 0;
        else if (code == BTRON_KEY_LEFT || code == 0xFF51 || code == 0x04) in_turn_left = 0;
        else if (code == BTRON_KEY_RIGHT || code == 0xFF53 || code == 0x03) in_turn_right = 0;
        else if (code == ' ' || code == BTRON_KEY_SPACE) in_jump = 0;
        else if (code == '\r' || code == '\n' || code == 0x11 || code == 'e' || code == 'E' ||
                 code == BTRON_KEY_RETURN || code == BTRON_KEY_KP_ENTER) in_attack = 0;
        else if (code == 0x12) in_jump = 0;
        break;
    }

    default:
        break;
    }
}
