/*
 * src/apps/quake.h — Quake 3D Desktop Application for B-System
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef BTRON_APP_QUAKE_H
#define BTRON_APP_QUAKE_H

#include <btron/wnd.h>

/* Opens the native Quake window on the B-System desktop */
WND* open_quake_window(int x, int y, int width, int height);

#endif /* BTRON_APP_QUAKE_H */
