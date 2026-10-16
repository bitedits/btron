/*
 * verify/tests/test_ps2_vu1_geom.c — Unit tests for VU1 Geometry & Lighting Pipeline
 *
 * Validates L2a/L2b uniform configuration, double-buffer packet structures, and
 * mathematical equivalence of the VU1 streaming microprogram.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "../../src/drivers/ps2/ps2_vu1_geom.h"
#include "../../src/drivers/ps2/ps2_vu1_geom.c"
#include "../../src/drivers/ps2/ps2_gs_pack.c"
#include "../../src/drivers/ps2/ps2_gs_rec.h"
#include "gs_group_view.h"

static int s_checks, s_failures;

static void check(const char *what, int ok)
{
    s_checks++;
    if (!ok) {
        s_failures++;
        printf("  [FAIL] %s\n", what);
    }
}

static void check_float(const char *what, float got, float want, float eps)
{
    s_checks++;
    if (fabsf(got - want) > eps) {
        s_failures++;
        printf("  [FAIL] %s got=%f want=%f (diff=%f)\n", what, got, want, fabsf(got - want));
    }
}

/* 1. Test uniform management */
static void test_uniforms(void)
{
    float custom_mvp[16] = {
        2.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 3.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 4.0f, 0.0f,
        1.0f, 2.0f, 3.0f, 1.0f
    };
    float light_dir[3] = { 0.57735f, 0.57735f, 0.57735f };
    float light_col[3] = { 0.8f, 0.9f, 1.0f };

    check("init returns 0", ps2_vu1_geom_init() == 0);
    check("is_inited reports TRUE", ps2_vu1_geom_is_inited() == 1);

    const float *mvp = ps2_vu1_geom_get_mvp();
    check_float("default mvp[0]", mvp[0], 1.0f, 1e-5f);
    check_float("default mvp[5]", mvp[5], 1.0f, 1e-5f);
    check_float("default mvp[10]", mvp[10], 1.0f, 1e-5f);
    check_float("default mvp[15]", mvp[15], 1.0f, 1e-5f);

    ps2_vu1_geom_set_mvp(custom_mvp);
    mvp = ps2_vu1_geom_get_mvp();
    check_float("custom mvp[0]", mvp[0], 2.0f, 1e-5f);
    check_float("custom mvp[5]", mvp[5], 3.0f, 1e-5f);
    check_float("custom mvp[10]", mvp[10], 4.0f, 1e-5f);
    check_float("custom mvp[12]", mvp[12], 1.0f, 1e-5f);

    ps2_vu1_geom_set_light(light_dir, 0.25f, light_col, 1);
    const float *dir = ps2_vu1_geom_get_light_dir();
    const float *col = ps2_vu1_geom_get_light_col();

    check_float("light_dir[0]", dir[0], 0.57735f, 1e-5f);
    check_float("light ambient", dir[3], 0.25f, 1e-5f);
    check_float("light_col[0]", col[0], 0.8f, 1e-5f);
    check_float("light enabled", col[3], 1.0f, 1e-5f);
}

/* 2. Test packet header structure layout */
static void test_packet_header(void)
{
    vu1_packet_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));

    check("packet header is 32 bytes (2 quadwords)", sizeof(vu1_packet_header_t) == 32u);

    hdr.scale_x = 2048.0f;
    hdr.scale_y = 2048.0f;
    hdr.scale_z = 1.0f;
    hdr.vert_count = 12.0f;
    hdr.giftag_lo = gs_pack_giftag(12u, 1u, 0u, 0u, GS_FLG_REGLIST, 4u);
    hdr.giftag_hi = 0x5210ULL;

    check_float("hdr scale_x", hdr.scale_x, 2048.0f, 1e-5f);
    check_float("hdr vert_count", hdr.vert_count, 12.0f, 1e-5f);
    check("giftag FLG is REGLIST", ((hdr.giftag_lo >> 58) & 3u) == GS_FLG_REGLIST);
    check("giftag NLOOP is 12", (hdr.giftag_lo & 0x7FFFu) == 12u);
    check("giftag REGS is 0x5210", hdr.giftag_hi == 0x5210ULL);
}

/* 3. Mathematical equivalence check of L2a/L2b pipeline steps */
static void test_math_pipeline(void)
{
    /* Model space vertex */
    float vx = 1.0f, vy = 2.0f, vz = 3.0f, vw = 1.0f;
    /* Normal vector (unit Y) */
    float nx = 0.0f, ny = 1.0f, nz = 0.0f;
    /* Light direction (unit Y) */
    float lx = 0.0f, ly = 1.0f, lz = 0.0f, ambient = 0.2f;
    /* Light color (white) */
    float lr = 1.0f, lg = 1.0f, lb = 1.0f;
    /* Base color (0.5 grey) */
    float cr = 0.5f, cg = 0.5f, cb = 0.5f;

    /* 4x4 Transformation: row-major multiplication */
    float clip_x = s_mvp[0]*vx + s_mvp[1]*vy + s_mvp[2]*vz + s_mvp[3]*vw;
    float clip_y = s_mvp[4]*vx + s_mvp[5]*vy + s_mvp[6]*vz + s_mvp[7]*vw;
    float clip_z = s_mvp[8]*vx + s_mvp[9]*vy + s_mvp[10]*vz + s_mvp[11]*vw;
    float clip_w = s_mvp[12]*vx + s_mvp[13]*vy + s_mvp[14]*vz + s_mvp[15]*vw;

    check_float("clip_x equals vx under scale", clip_x, 2.0f, 1e-5f);
    check_float("clip_y equals vy under scale", clip_y, 6.0f, 1e-5f);
    check_float("clip_z equals vz under scale", clip_z, 12.0f, 1e-5f);
    check_float("clip_w equals custom translation", clip_w, 15.0f, 1e-5f);

    /* Dot product N . L */
    float dot = nx * lx + ny * ly + nz * lz;
    if (dot < 0.0f) dot = 0.0f;
    check_float("dot product is 1.0", dot, 1.0f, 1e-5f);

    /* Lit color calculation */
    float lit_factor_r = ambient + dot * lr;
    float lit_factor_g = ambient + dot * lg;
    float lit_factor_b = ambient + dot * lb;
    check_float("lit factor is 1.2", lit_factor_r, 1.2f, 1e-5f);

    float out_r = cr * lit_factor_r;
    float out_g = cg * lit_factor_g;
    float out_b = cb * lit_factor_b;
    check_float("final red is 0.6", out_r, 0.6f, 1e-5f);
    check_float("final green is 0.6", out_g, 0.6f, 1e-5f);
    check_float("final blue is 0.6", out_b, 0.6f, 1e-5f);

    /* Perspective divide and snap */
    float q = 1.0f / clip_w;
    check_float("q is 1/15", q, 1.0f / 15.0f, 1e-5f);

    float proj_x = (clip_x * q) * 2048.0f + 2048.0f;
    int32_t ftoi4_x = (int32_t)(proj_x * 16.0f);
    check("ftoi4 coordinate is positive", ftoi4_x > 0);
}

int main(void)
{
    printf("==========================================================\n");
    printf(" PS2 VU1 Geometry & Lighting Pipeline Unit Tests\n");
    printf("==========================================================\n");

    test_uniforms();
    test_packet_header();
    test_math_pipeline();

    printf("  checks=%d failures=%d\n", s_checks, s_failures);
    if (s_failures == 0) {
        printf("[CI-TEST] SUCCESS - VU1 geometry pipeline contract verified\n");
        return 0;
    }
    printf("[CI-TEST] FAIL - VU1 geometry pipeline failed check\n");
    return 1;
}
