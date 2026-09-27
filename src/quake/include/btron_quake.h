/*
 * src/quake/include/btron_quake.h — Quake 3D Desktop Application for B-System
 *
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#ifndef BTRON_QUAKE_H
#define BTRON_QUAKE_H

#include <btron/wnd.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opens the native Quake window on the B-System desktop */
WND *open_quake_window(int x, int y, int width, int height);

/* Closes the Quake window and stops the local engine */
void close_quake_window(void);

/* Runs a single frame of the Quake engine inside the window */
void quake_frame_step(void);

/* Check if Quake engine is currently running */
int quake_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* BTRON_QUAKE_H */
