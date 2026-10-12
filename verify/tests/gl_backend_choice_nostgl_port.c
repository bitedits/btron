/*
 * verify/tests/gl_backend_choice_nostgl_port.c — a target that links no src/gl/tinygl/ list
 *
 * See gl_backend_choice_plain_port.c for why the dial needs one translation unit per value.
 * This is the arm every non-PS2 image compiles under, and it is here because a third value on
 * the dial must not have moved the rule for the targets that never ask for one.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#define gl_backend_default          nostgl_default
#define gl_backend_software_default nostgl_software_default
#define gl_backend_selected         nostgl_selected
#define gl_backend_select           nostgl_select

#include "../../src/gl/gl_backend_choice.c"
