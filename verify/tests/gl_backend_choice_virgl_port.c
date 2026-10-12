/*
 * verify/tests/gl_backend_choice_virgl_port.c — `PS2_GL=virgl`, the other software rasteriser
 *
 * See gl_backend_choice_plain_port.c for why the dial needs one translation unit per value.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#define gl_backend_default          virgl_default
#define gl_backend_software_default virgl_software_default
#define gl_backend_selected         virgl_selected
#define gl_backend_select           virgl_select

#include "../../src/gl/gl_backend_choice.c"
