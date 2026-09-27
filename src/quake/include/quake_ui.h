/*
 * src/quake/include/quake_ui.h — Quake Menu, Console & Map Loader Interface
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#ifndef QUAKE_UI_H
#define QUAKE_UI_H

#include "quakedef.h"
#include <btron/event.h>
#include <btron/wnd.h>

/* UI States */
extern int g_menu_active;
extern int g_console_active;
extern int g_godmode;
extern int g_noclip;
extern int g_replay_active;

/* Menu & Console Lifecycle */
void UI_Init(void);
void UI_Draw(int width, int height);
int  UI_HandleKey(UW key);
int  UI_HandleMouse(int mx, int my, int button_down);

/* Demo & Replay Playback */
void Replay_StartDemo(int demo_num);
int  Replay_StartDemoFile(const char *demopath);
void Replay_Update(float dt);
void Replay_Stop(void);

/* Map Loader */
int  World_ChangeMap(const char *mapname);

/* Combat & Player Action */
void Player_FireWeapon(void);
void Player_UpdateAnimation(float dt);
int  Player_GetGunFrame(void);
void UI_RequestWeapon(int slot);     /* slot 1..8, bound to the number keys */
const char *Player_WeaponName(void); /* HUD label of the held weapon */

#endif /* QUAKE_UI_H */
