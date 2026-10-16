/*
 * src/drivers/ps2/ps2_vu1_geom.c — VU1 Geometry & Lighting Pipeline Implementation
 *
 * Implements L2a (transform/clip/perspective) and L2b (lit-vertex) setup and dispatch
 * for PlayStation 2 Vector Unit 1 (VU1).
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "ps2_vu1_geom.h"
#include <string.h>

#if defined(BTRON_PS2_TARGET)
#define D1_CHCR     (*(volatile uint32_t *)0x10009000)
#define D1_MADR     (*(volatile uint32_t *)0x10009010)
#define D1_QWC      (*(volatile uint32_t *)0x10009020)
#define D_STAT      (*(volatile uint32_t *)0x1000e010)
#define D1_STAT_MIF (1u << 1)
#endif

/* Fallback assembly code symbols when not compiling with native PS2 toolchain */
#if !defined(BTRON_PS2_TARGET)
const uint32_t VU1GeomPipe_CodeStart[1] = { 0x00000000 };
const uint32_t VU1GeomPipe_CodeEnd[1]   = { 0x00000000 };
#endif

/* Active uniform cache */
static float s_mvp[16];
static float s_light_dir[4];
static float s_light_col[4];
static int   s_vu1_inited = 0;

int ps2_vu1_geom_init(void)
{
    /* Initialize default uniforms: Identity matrix */
    memset(s_mvp, 0, sizeof(s_mvp));
    s_mvp[0] = 1.0f;
    s_mvp[5] = 1.0f;
    s_mvp[10] = 1.0f;
    s_mvp[15] = 1.0f;

    /* Default directional light: pointing from above (+Y), ambient 0.2 */
    s_light_dir[0] = 0.0f;
    s_light_dir[1] = 1.0f;
    s_light_dir[2] = 0.0f;
    s_light_dir[3] = 0.2f;

    /* Default light color: white, lighting enabled */
    s_light_col[0] = 1.0f;
    s_light_col[1] = 1.0f;
    s_light_col[2] = 1.0f;
    s_light_col[3] = 1.0f;

    s_vu1_inited = 1;
    return 0;
}

void ps2_vu1_geom_set_mvp(const float mvp[16])
{
    if (!mvp) return;
    memcpy(s_mvp, mvp, sizeof(s_mvp));
}

void ps2_vu1_geom_set_light(const float dir[3], float ambient, const float rgb[3], int enabled)
{
    if (dir) {
        s_light_dir[0] = dir[0];
        s_light_dir[1] = dir[1];
        s_light_dir[2] = dir[2];
    }
    s_light_dir[3] = ambient;

    if (rgb) {
        s_light_col[0] = rgb[0];
        s_light_col[1] = rgb[1];
        s_light_col[2] = rgb[2];
    }
    s_light_col[3] = enabled ? 1.0f : 0.0f;
}

const float* ps2_vu1_geom_get_mvp(void)
{
    return s_mvp;
}

const float* ps2_vu1_geom_get_light_dir(void)
{
    return s_light_dir;
}

const float* ps2_vu1_geom_get_light_col(void)
{
    return s_light_col;
}

int ps2_vu1_geom_is_inited(void)
{
    return s_vu1_inited;
}
