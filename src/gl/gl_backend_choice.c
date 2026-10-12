/*
 * src/gl/gl_backend_choice.c — the remembered answer to "which rasteriser?"
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "gl_backend_choice.h"

/*
 * The build's own answer, and the only place that knows which rasteriser this image links.
 * BTRON_GL_BACKEND_TINYGL is defined by the Makefile lists that put src/gl/tinygl/ into a
 * target's sources, and the two PREFER macros are the Makefile's `PS2_GL=` dial naming which
 * of the three backends this image should start on: `virgl` asks for the other software
 * rasteriser for a timing comparison, `eegs` asks for the Graphics Synthesizer.
 *
 * Asking for the GS through the build rather than through the Display applet is the same
 * request the row makes, so it goes through the same answer -- and while that answer is a
 * placeholder (gl_dispatch.c installs a software rasteriser and says so), a build with
 * `PS2_GL=eegs` is the image increment 5 has to be tested against: the console line and the
 * active backend both report the pipeline that drew the frame, and neither reports EEGS until
 * an EEGS ops table exists to install.
 */
GL_BACKEND gl_backend_software_default(void)
{
#if defined(BTRON_GL_BACKEND_TINYGL) && !defined(BTRON_GL_PREFER_VIRGL)
    return GL_BACKEND_TINYGL;
#else
    return GL_BACKEND_VIRGL;
#endif
}

GL_BACKEND gl_backend_default(void)
{
#ifdef BTRON_GL_PREFER_EEGS
    return GL_BACKEND_EEGS;
#else
    return gl_backend_software_default();
#endif
}

/* "Nobody has chosen yet" is a state of its own, because the build's default is a legal
 * choice too: selecting TinyGL explicitly must not read as falling back to the default on a
 * target whose default is virgl. */
static GL_BACKEND s_selected;
static int        s_chosen;

GL_BACKEND gl_backend_selected(void)
{
    return s_chosen ? s_selected : gl_backend_default();
}

void gl_backend_select(GL_BACKEND b)
{
    s_selected = b;
    s_chosen   = 1;
}
