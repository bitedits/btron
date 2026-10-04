/*
 * src/demo/lilcu64_demo.h — Lil Cu 64 Native OpenGL Demoscene Application
 *
 * B-System Alpha 1 [Ceremony] Desktop Application presenting 4D Hopf Fibrations,
 * Low-Poly Mascot Kinematics, Personage Carousel, and VirtIO Procedural Sound.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef LILCU64_DEMO_H
#define LILCU64_DEMO_H

#include <btron/btron.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Opens the Lil Cu 64 3D Demoscene window.
 * Returns pointer to the created WND, or NULL on failure.
 */
WND* open_lilcu64_demo_window(void);

#ifdef __cplusplus
}
#endif

#endif /* LILCU64_DEMO_H */
