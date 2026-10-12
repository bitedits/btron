/*
 * verify/tests/gl_backend_choice_plain_port.c — the backend rule as one `PS2_GL=` value compiles it
 *
 * One translation unit per value of the Makefile's dial, because the rule under test is a
 * preprocessor branch and a single binary can only ever compile one of its arms: a suite that
 * ran only the shipped arm could not tell `PS2_GL=eegs` from a flag the compiler ignored.
 *
 * These port files differ from each other only by the names they give the API; the -D flags
 * that carry the dial's value are passed by the Makefile rule, spelled the same way
 * PS2_CFLAGS spells them, so what is measured is the flag and not a copy of it written here.
 * The rename is what lets four copies of one translation unit link into one program -- and the
 * remembered choice behind them is file-static, which is what makes each port an independent
 * "nobody has chosen yet" instead of one answer shared by four builds.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#define gl_backend_default          plain_default
#define gl_backend_software_default plain_software_default
#define gl_backend_selected         plain_selected
#define gl_backend_select           plain_select

#include "../../src/gl/gl_backend_choice.c"
