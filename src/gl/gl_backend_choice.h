/*
 * src/gl/gl_backend_choice.h — which rasteriser the image hands a frame to
 *
 * The choice has its own translation unit because it has two callers on two sides of a
 * link boundary: the settings applet that draws the row, and the surface bridge that asks
 * for a backend when a window's GL context is created.  Neither of them can pull in the
 * rasterisers themselves, so this holds the selection and nothing else.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef BTRON_GL_BACKEND_CHOICE_H
#define BTRON_GL_BACKEND_CHOICE_H

#include "gl_dispatch.h"

/* What this image links as a rasteriser of its own: TinyGL where src/gl/tinygl/ is in the
 * source list and the Makefile has not asked for the other one (`PS2_GL=virgl`), virgl
 * everywhere else.  This used to be the `EGL_DEFAULT_BACKEND` macro in egl_surface.c; it
 * moved here so the settings row and the first surface read one rule rather than two.
 *
 * The rule for the fallback a GS request resolves to, which is why it is a function of its own
 * rather than a branch of gl_backend_default(): with `PS2_GL=eegs` the default *is* the GS
 * backend, so a placeholder that fell back to the default would call itself. */
GL_BACKEND gl_backend_software_default(void);

/* What this image starts on: the software rasteriser it links, unless its Makefile asked for
 * the Graphics Synthesizer (`PS2_GL=eegs`), which is the same answer the Display applet's row
 * gives and so is answered by the same code path. */
GL_BACKEND gl_backend_default(void);

/* What the user chose, or the build's answer if nobody has.  Read at every surface
 * creation, so a change takes effect at the next window rather than mid-frame. */
GL_BACKEND gl_backend_selected(void);

/* The choice itself.  Remembered, not applied: the caller that has a window to re-install
 * is the one that calls gl_shutdown()/gl_init(). */
void gl_backend_select(GL_BACKEND backend);

#endif /* BTRON_GL_BACKEND_CHOICE_H */
