/*
 * verify/tests/gl_backend_choice_eegs_port.c — `PS2_GL=eegs`, the build that starts on the GS
 *
 * See gl_backend_choice_plain_port.c for why the dial needs one translation unit per value.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#define gl_backend_default          eegs_default
#define gl_backend_software_default eegs_software_default
#define gl_backend_selected         eegs_selected
#define gl_backend_select           eegs_select

#include "../../src/gl/gl_backend_choice.c"
