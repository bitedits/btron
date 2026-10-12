/*
 * verify/tests/gl_backend_choice.c — the Makefile's `PS2_GL=` dial, all four of its answers at once
 *
 * Increment 5's precondition, written before the GS ops table exists: the backend a PS2 image
 * starts on is now selectable from the Makefile as well as from the Display applet, and the flag
 * that selects it is a preprocessor branch.  A binary compiled with one value of that branch
 * cannot observe the other three, so this suite links the same translation unit four times --
 * once per `PS2_GL=` value, each with the -D flags spelled the way PS2_CFLAGS spells them -- and
 * compares every arm against a literal written from what the dial's name promises, not from the
 * code.
 *
 * The pair that matters most is the last one: on a `PS2_GL=eegs` build the default *is* the GS
 * backend, so the placeholder in gl_dispatch.c that answers such a request has to resolve to the
 * software rule and not to the default, or it calls itself.  That is why
 * gl_backend_software_default() exists and why it is checked here rather than in the dispatch
 * suite.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdio.h>
#include "../../src/gl/gl_dispatch.h"

/* One port per value of the dial; see gl_backend_choice_plain_port.c. */
GL_BACKEND plain_default(void);
GL_BACKEND plain_software_default(void);
GL_BACKEND plain_selected(void);
void       plain_select(GL_BACKEND);

GL_BACKEND virgl_default(void);
GL_BACKEND virgl_software_default(void);
GL_BACKEND virgl_selected(void);

GL_BACKEND eegs_default(void);
GL_BACKEND eegs_software_default(void);
GL_BACKEND eegs_selected(void);
void       eegs_select(GL_BACKEND);

GL_BACKEND nostgl_default(void);

static int s_checks, s_failures;

static void check(const char *what, int ok)
{
    s_checks++;
    if (!ok) {
        s_failures++;
        printf("  [FAIL] %s\n", what);
    } else {
        printf("  [ok]   %s\n", what);
    }
}

int main(void)
{
    printf("==========================================================\n");
    printf(" PS2 GL backend selection: the Makefile's PS2_GL= dial vs the rule\n");
    printf("==========================================================\n");

    /* PS2_GL=tinygl, the shipped image: the default is the rasteriser whose sources are in the
     * target's list, which is the rule that existed before the dial could name a third backend. */
    check("PS2_GL=tinygl defaults to TinyGL", plain_default() == GL_BACKEND_TINYGL);
    check("PS2_GL=tinygl's software rule is TinyGL too, so nothing moved for it",
          plain_software_default() == GL_BACKEND_TINYGL);
    check("PS2_GL=tinygl with nobody chosen reads the default",
          plain_selected() == GL_BACKEND_TINYGL);

    /* PS2_GL=virgl: the other software rasteriser, and still not the GS. */
    check("PS2_GL=virgl defaults to the virgl path", virgl_default() == GL_BACKEND_VIRGL);
    check("PS2_GL=virgl's software rule agrees with its default",
          virgl_software_default() == GL_BACKEND_VIRGL);

    /* A target that links neither of the software sources' lists still answers with virgl, which
     * is the arm every non-PS2 image has been using; the dial must not have changed it. */
    check("a build without BTRON_GL_BACKEND_TINYGL defaults to virgl",
          nostgl_default() == GL_BACKEND_VIRGL);

    /* PS2_GL=eegs: the build starts on the backend the Display row offers, and the software
     * rule it falls back to while that row is a placeholder stays a software rasteriser. */
    check("PS2_GL=eegs defaults to EE/GS", eegs_default() == GL_BACKEND_EEGS);
    check("PS2_GL=eegs still names a software rasteriser as its fallback",
          eegs_software_default() == GL_BACKEND_TINYGL);
    check("the fallback a GS request resolves to is never the GS itself, so it cannot recurse",
          eegs_software_default() != GL_BACKEND_EEGS &&
          plain_software_default() != GL_BACKEND_EEGS &&
          virgl_software_default() != GL_BACKEND_EEGS);
    check("PS2_GL=eegs with nobody chosen reads EE/GS", eegs_selected() == GL_BACKEND_EEGS);

    /* The dial is a default, not a lock: the applet's row still outranks it, in both directions,
     * and each port's remembered choice is its own. */
    eegs_select(GL_BACKEND_TINYGL);
    check("the settings row outranks a PS2_GL=eegs build", eegs_selected() == GL_BACKEND_TINYGL);
    plain_select(GL_BACKEND_EEGS);
    check("and outranks a PS2_GL=tinygl one", plain_selected() == GL_BACKEND_EEGS);
    check("choosing on one build does not move another",
          eegs_selected() == GL_BACKEND_TINYGL && virgl_selected() == GL_BACKEND_VIRGL);

    printf("  checks=%d failures=%d\n", s_checks, s_failures);
    if (s_failures == 0) {
        printf("[CI-TEST] SUCCESS - every PS2_GL= value selects what its name says\n");
        return 0;
    }
    printf("[CI-TEST] FAIL - the Makefile dial and the backend rule disagree\n");
    return 1;
}
