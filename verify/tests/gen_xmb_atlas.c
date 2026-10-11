/*
 * verify/tests/gen_xmb_atlas.c -- build-time generator for the XMB icon atlas.
 *
 * The bar used to bake its icons the first time a frame sampled them: 36 cells of
 * 64x64 signed-distance arithmetic, which on the PS2's software float measured
 * 132 ms a cell -- 2.8 s of the 3.9 s it took to open the bar, paid again on every
 * band change.  None of it depends on anything known only at run time: every glyph
 * here is a closed form over fixed constants.  So the arithmetic moves to build
 * time and the bar ships its result.
 *
 * The glyph code below is the bake's own, moved verbatim, which is what makes the
 * sheet it prints the same pixels the bar used to compute rather than a redrawn
 * guess at them.  `make xmb-atlas` regenerates src/apps/xmb_atlas.h;
 * `make test-xmb-atlas` regenerates it again and fails if the two disagree, so the
 * committed sheet cannot go stale against the geometry it came from.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */
#include <stdio.h>
#include <math.h>
#include "../../src/apps/xmb_icons.h"

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif

static float xb_sd_box(float x, float y, float cx, float cy, float hw, float hh)
{
    float dx = fabsf(x - cx) - hw, dy = fabsf(y - cy) - hh;
    float mx = dx > 0.0f ? dx : 0.0f;
    float my = dy > 0.0f ? dy : 0.0f;
    float inside = dx > dy ? dx : dy;
    return inside < 0.0f ? inside : sqrtf(mx * mx + my * my);
}

static float xb_sd_rbox(float x, float y, float cx, float cy,
                        float hw, float hh, float r)
{
    return xb_sd_box(x, y, cx, cy, hw - r, hh - r) - r;
}

static float xb_sd_disc(float x, float y, float cx, float cy, float r)
{
    float a = x - cx, b = y - cy;
    return sqrtf(a * a + b * b) - r;
}

static float xb_sd_ring(float x, float y, float cx, float cy, float r, float w)
{
    /* Annulus of half-thickness w: the distance to the circle, widened */
    return fabsf(xb_sd_disc(x, y, cx, cy, r)) - w;
}

static float xb_sd_capsule(float x, float y, float ax, float ay,
                           float bx, float by, float w)
{
    float vx = bx - ax, vy = by - ay;
    float wx = x - ax,  wy = y - ay;
    float len2 = vx * vx + vy * vy;
    float t = len2 > 1e-6f ? (wx * vx + wy * vy) / len2 : 0.0f;
    float px, py;
    t = xb_clampf(t, 0.0f, 1.0f);
    px = x - (ax + t * vx);
    py = y - (ay + t * vy);
    return sqrtf(px * px + py * py) - w;
}

#define UN(a, b)          ((a) < (b)  ? (a) : (b))        /* CSG union        */
#define INTER(a, b)       ((a) > (b)  ? (a) : (b))        /* CSG intersection */
#define SUB(a, b)         ((a) > (-(b)) ? (a) : (-(b)))   /* CSG difference   */
#define STROKE(a, w)      (fabsf(a) - (w))                /* outline          */
#define XB_W              0.05f            /* stroke weight, in cell units    */

/* Distance to one segment, always positive */
static float xb_sd_seg(float x, float y, float ax, float ay, float bx, float by)
{
    float pax = x - ax, pay = y - ay;
    float bax = bx - ax, bay = by - ay;
    float len2 = bax * bax + bay * bay;
    float h = len2 > 1e-6f ? pax * bax + pay * bay : 0.0f;
    float dx, dy;

    h = len2 > 1e-6f ? xb_clampf(h / len2, 0.0f, 1.0f) : 0.0f;
    dx = pax - bax * h;
    dy = pay - bay * h;
    return sqrtf(dx * dx + dy * dy);
}

/* Exact distance to a triangle: nearest of the three edges, signed by the
 * side of each edge the point sits on */
static float xb_sd_tri(float x, float y, float ax, float ay,
                       float bx, float by, float cx, float cy)
{
    float d   = UN(UN(xb_sd_seg(x, y, ax, ay, bx, by),
                      xb_sd_seg(x, y, bx, by, cx, cy)),
                   xb_sd_seg(x, y, cx, cy, ax, ay));
    float s1 = (bx - ax) * (y - ay) - (by - ay) * (x - ax);
    float s2 = (cx - bx) * (y - by) - (cy - by) * (x - bx);
    float s3 = (ax - cx) * (y - cy) - (ay - cy) * (x - cx);
    int neg = (s1 < 0.0f) + (s2 < 0.0f) + (s3 < 0.0f);
    int pos = (s1 > 0.0f) + (s2 > 0.0f) + (s3 > 0.0f);

    return (neg && pos) ? d : -d;
}

/* Coverage of one icon cell, in centred coordinates of -0.5 .. 0.5 with y
 * downwards.  Every glyph is a white silhouette at one stroke weight, which is
 * how the PS3 bar's own pictograms read at icon_size. */
static float xb_icon_cov(int cell, float x, float y)
{
    float d = 1.0f;
    int i;

    switch (cell) {
    case IC_CAT_APPS:
        /* The applications group: four rounded squares */
        d = UN(UN(xb_sd_rbox(x, y, -0.20f, -0.20f, 0.14f, 0.14f, 0.05f),
                  xb_sd_rbox(x, y,  0.20f, -0.20f, 0.14f, 0.14f, 0.05f)),
               UN(xb_sd_rbox(x, y, -0.20f,  0.20f, 0.14f, 0.14f, 0.05f),
                  xb_sd_rbox(x, y,  0.20f,  0.20f, 0.14f, 0.14f, 0.05f)));
        break;

    case IC_CAT_SETTINGS:
    case IC_GEAR: {
        /* Ring, eight studs and a hub.  The studs sit at multiples of pi/4, so their
         * offsets are eight constants -- but writing them as literals would be a
         * second truth about the gear, and one rounding step away from what this
         * series answers for those angles.  They are therefore computed once, with
         * the same two expressions the per-pixel path used, and kept. */
        static float stud_dx[8], stud_dy[8];
        static unsigned char studs_baked;

        if (!studs_baked) {
            for (i = 0; i < 8; i++) {
                const float a = (float)i * ((float)M_PI / 4.0f);
                stud_dx[i] = cosf(a) * 0.36f;
                stud_dy[i] = sinf(a) * 0.36f;
            }
            studs_baked = 1;
        }
        d = xb_sd_ring(x, y, 0.0f, 0.0f, 0.28f, XB_W);
        for (i = 0; i < 8; i++)
            d = UN(d, xb_sd_disc(x, y, stud_dx[i], stud_dy[i], XB_W));
        d = UN(d, xb_sd_disc(x, y, 0.0f, 0.0f, 0.12f));
        break;
    }

    case IC_SPEAKER:
        /* Throat, cone and two sound arcs on the right */
        d = xb_sd_rbox(x, y, -0.30f, 0.0f, 0.07f, 0.14f, 0.03f);
        d = UN(d, xb_sd_tri(x, y, -0.20f, -0.14f, -0.20f, 0.14f, 0.04f,  0.32f));
        d = UN(d, xb_sd_tri(x, y, -0.20f, -0.14f,  0.04f,  0.32f, 0.04f, -0.32f));
        d = UN(d, INTER(xb_sd_ring(x, y, 0.06f, 0.0f, 0.30f, 0.04f), x - 0.16f));
        d = UN(d, INTER(xb_sd_ring(x, y, 0.06f, 0.0f, 0.44f, 0.04f), x - 0.16f));
        break;

    case IC_CAT_COMMANDS:
    case IC_PROMPT:
    case IC_APP_TERM:
        /* Window frame with a prompt inside */
        d = STROKE(xb_sd_rbox(x, y, 0.0f, 0.0f, 0.36f, 0.30f, 0.06f), XB_W * 0.7f);
        d = UN(d, xb_sd_capsule(x, y, -0.20f, -0.12f, -0.05f, 0.0f, 0.04f));
        d = UN(d, xb_sd_capsule(x, y, -0.05f,  0.0f, -0.20f, 0.12f, 0.04f));
        d = UN(d, xb_sd_capsule(x, y,  0.03f,  0.14f,  0.22f, 0.14f, 0.04f));
        break;

    case IC_CAT_GAMES:
        /* The Games band: a cartridge shell with its label rules and the two
         * slots a reader grips. */
        d = STROKE(xb_sd_rbox(x, y, 0.0f, 0.02f, 0.28f, 0.36f, 0.05f), XB_W * 0.8f);
        d = UN(d, xb_sd_capsule(x, y, -0.15f, -0.20f, 0.15f, -0.20f, 0.032f));
        d = UN(d, xb_sd_capsule(x, y, -0.15f, -0.06f, 0.15f, -0.06f, 0.032f));
        d = UN(d, xb_sd_capsule(x, y, -0.15f,  0.08f, 0.02f,  0.08f, 0.032f));
        d = UN(d, xb_sd_rbox(x, y, -0.13f, 0.30f, 0.07f, 0.028f, 0.014f));
        d = UN(d, xb_sd_rbox(x, y,  0.13f, 0.30f, 0.07f, 0.028f, 0.014f));
        break;

    case IC_ARROW:
        d = xb_sd_capsule(x, y, -0.12f, -0.24f, 0.16f, 0.0f, 0.06f);
        d = UN(d, xb_sd_capsule(x, y, 0.16f, 0.0f, -0.12f, 0.24f, 0.06f));
        break;

    case IC_APP_EDITOR:
        /* A page ruled with three lines of text */
        d = STROKE(xb_sd_rbox(x, y, 0.0f, -0.02f, 0.28f, 0.34f, 0.04f), XB_W * 0.7f);
        d = UN(d, xb_sd_capsule(x, y, -0.14f, -0.20f, 0.14f, -0.20f, 0.032f));
        d = UN(d, xb_sd_capsule(x, y, -0.14f, -0.06f, 0.14f, -0.06f, 0.032f));
        d = UN(d, xb_sd_capsule(x, y, -0.14f,  0.08f, 0.14f,  0.08f, 0.032f));
        d = UN(d, xb_sd_capsule(x, y, -0.14f,  0.22f, 0.02f,  0.22f, 0.032f));
        break;

    case IC_APP_PAINT:
        /* Palette: a ring with three wells and a brush */
        d = SUB(xb_sd_disc(x, y, 0.0f, 0.04f, 0.34f),
                xb_sd_disc(x, y, 0.0f, 0.04f, 0.34f - XB_W * 1.4f));
        d = UN(d, xb_sd_disc(x, y,  0.16f, -0.16f, 0.06f));
        d = UN(d, xb_sd_disc(x, y, -0.18f, -0.04f, 0.06f));
        d = UN(d, xb_sd_disc(x, y, -0.02f,  0.22f, 0.06f));
        d = UN(d, xb_sd_capsule(x, y, 0.20f, -0.38f, 0.38f, -0.20f, 0.05f));
        break;

    case IC_APP_MUSIC:
        d = UN(xb_sd_disc(x, y, -0.16f,  0.20f, 0.11f),
               xb_sd_disc(x, y,  0.18f,  0.12f, 0.11f));
        d = UN(d, xb_sd_capsule(x, y, -0.06f, 0.20f, -0.06f, -0.26f, 0.04f));
        d = UN(d, xb_sd_capsule(x, y,  0.28f, 0.12f,  0.28f, -0.34f, 0.04f));
        d = UN(d, xb_sd_capsule(x, y, -0.06f, -0.26f,  0.28f, -0.34f, 0.05f));
        break;

    case IC_APP_ORCHESTRA:
        d = xb_sd_capsule(x, y, -0.30f, -0.18f, -0.30f, 0.18f, 0.045f);
        d = UN(d, xb_sd_capsule(x, y, -0.15f, -0.30f, -0.15f, 0.30f, 0.045f));
        d = UN(d, xb_sd_capsule(x, y,  0.00f, -0.12f,  0.00f, 0.12f, 0.045f));
        d = UN(d, xb_sd_capsule(x, y,  0.15f, -0.32f,  0.15f, 0.32f, 0.045f));
        d = UN(d, xb_sd_capsule(x, y,  0.30f, -0.20f,  0.30f, 0.20f, 0.045f));
        break;

    case IC_APP_VOBJ:
        /* Wireframe cube: the virtual object database */
        d = xb_sd_capsule(x, y, 0.0f, -0.32f, 0.30f, -0.14f, 0.04f);
        d = UN(d, xb_sd_capsule(x, y, 0.30f, -0.14f, 0.30f, 0.18f, 0.04f));
        d = UN(d, xb_sd_capsule(x, y, 0.30f, 0.18f, 0.0f, 0.36f, 0.04f));
        d = UN(d, xb_sd_capsule(x, y, 0.0f, 0.36f, -0.30f, 0.18f, 0.04f));
        d = UN(d, xb_sd_capsule(x, y, -0.30f, 0.18f, -0.30f, -0.14f, 0.04f));
        d = UN(d, xb_sd_capsule(x, y, -0.30f, -0.14f, 0.0f, 0.02f, 0.04f));
        d = UN(d, xb_sd_capsule(x, y, 0.0f, 0.02f, 0.30f, -0.14f, 0.04f));
        d = UN(d, xb_sd_capsule(x, y, 0.0f, 0.02f, 0.0f, 0.36f, 0.04f));
        break;

    case IC_APP_TAD:
    case IC_FOLDER:
        d = xb_sd_rbox(x, y, 0.0f, 0.10f, 0.36f, 0.22f, 0.05f);
        d = UN(d, xb_sd_rbox(x, y, -0.18f, -0.18f, 0.16f, 0.08f, 0.03f));
        break;

    case IC_MINIDISC:
        /* The MiniDisc case the Discs band carries: a shell with its corner cut,
         * the hub window in the middle and the two notches a drive reads. */
        d = STROKE(SUB(xb_sd_rbox(x, y, 0.0f, 0.0f, 0.28f, 0.34f, 0.06f),
                       xb_sd_tri(x, y, -0.10f, -0.30f,
                                    -0.30f, -0.10f,
                                    -0.62f, -0.62f)), XB_W * 0.8f);
        d = UN(d, xb_sd_ring(x, y, 0.0f, 0.06f, 0.13f, XB_W * 0.8f));
        d = UN(d, xb_sd_disc(x, y, 0.0f, 0.06f, 0.045f));
        d = UN(d, xb_sd_rbox(x, y, -0.17f, 0.28f, 0.05f, 0.025f, 0.015f));
        d = UN(d, xb_sd_rbox(x, y,  0.17f, 0.28f, 0.05f, 0.025f, 0.015f));
        break;

    case IC_FILE:
        /* A body: a page with its top-right corner turned back */
        d = STROKE(SUB(xb_sd_rbox(x, y, 0.0f, 0.02f, 0.24f, 0.32f, 0.03f),
                       xb_sd_tri(x, y,  0.10f, -0.34f,
                                    0.30f, -0.14f,
                                    0.62f, -0.62f)), XB_W * 0.7f);
        d = UN(d, xb_sd_capsule(x, y, -0.12f, 0.12f, 0.12f, 0.12f, 0.032f));
        break;

    case IC_APP_DRIVE:
        d = xb_sd_ring(x, y, 0.0f, 0.0f, 0.32f, 0.10f);
        d = UN(d, xb_sd_disc(x, y, 0.0f, 0.0f, 0.09f));
        break;

    case IC_APP_CHAT:
        d = xb_sd_rbox(x, y, 0.02f, -0.08f, 0.34f, 0.24f, 0.12f);
        d = UN(d, xb_sd_tri(x, y, -0.14f, 0.10f, -0.26f, 0.38f, 0.02f, 0.16f));
        break;

    case IC_APP_PHOTO:
        /* Picture frame, sun and a mountain range inside it */
        d = STROKE(xb_sd_rbox(x, y, 0.0f, 0.0f, 0.36f, 0.28f, 0.04f), XB_W * 0.7f);
        d = UN(d, xb_sd_disc(x, y, 0.20f, -0.12f, 0.06f));
        d = UN(d, SUB(xb_sd_tri(x, y, -0.34f, 0.26f, -0.06f, -0.04f, 0.16f, 0.26f),
                      STROKE(xb_sd_rbox(x, y, 0.0f, 0.0f, 0.36f, 0.28f, 0.04f),
                             XB_W * 0.7f)));
        break;

    case IC_APP_QUAKE:
        d = xb_sd_ring(x, y, 0.0f, 0.0f, 0.28f, 0.045f);
        d = UN(d, xb_sd_capsule(x, y, 0.0f, -0.46f, 0.0f, -0.34f, 0.045f));
        d = UN(d, xb_sd_capsule(x, y, 0.0f,  0.34f, 0.0f,  0.46f, 0.045f));
        d = UN(d, xb_sd_capsule(x, y, -0.46f, 0.0f, -0.34f, 0.0f, 0.045f));
        d = UN(d, xb_sd_capsule(x, y,  0.34f, 0.0f,  0.46f, 0.0f, 0.045f));
        break;

    case IC_INFO:
        d = xb_sd_disc(x, y, 0.0f, -0.28f, 0.075f);
        d = UN(d, xb_sd_capsule(x, y, 0.0f, -0.10f, 0.0f, 0.30f, 0.065f));
        break;

    case IC_CLOCK:
        d = xb_sd_ring(x, y, 0.0f, 0.0f, 0.32f, 0.045f);
        d = UN(d, xb_sd_capsule(x, y, 0.0f, 0.0f, 0.0f, -0.18f, 0.035f));
        d = UN(d, xb_sd_capsule(x, y, 0.0f, 0.0f, 0.14f, 0.06f, 0.035f));
        break;

    case IC_SLIDER:
        d = xb_sd_capsule(x, y, -0.34f, 0.08f, 0.34f, 0.08f, 0.03f);
        d = UN(d, xb_sd_disc(x, y, -0.08f, -0.10f, 0.12f));
        break;

    case IC_DROPLET:
        d = xb_sd_disc(x, y, 0.0f, 0.0f, 0.30f);
        break;

    case IC_BLANK:
    default:
        return 0.0f;
    }

    /* Soft edge: 0.022 cell units of falloff */
    return xb_clampf(0.5f - d / 0.044f, 0.0f, 1.0f);
}

/* Droplets use a radial falloff instead of a hard SDF edge */
static float xb_droplet_cov(float x, float y)
{
    float r = sqrtf(x * x + y * y) * 2.0f;
    float v = 1.0f - r;
    if (v < 0.0f) v = 0.0f;
    return v * v;
}

/* One texel, exactly as xb_bake_cell() used to compute it: the same sample
 * positions, the same coverage, and the same rule that an uncovered texel is left
 * alone rather than drawn with zero ink.  A texel the bake *did* write can still
 * round to alpha 0 (its coverage sits in the last 1/255 of the soft edge), and the
 * sheet has to tell that texel from one the bake never touched, because the
 * untouched one is black in all four channels while the written one is ink with no
 * alpha.  Clamping the written one to 1 keeps its ink, which is what the atlas
 * reads as, and costs 1/255 of coverage on a ring one texel wide. */
static unsigned char xb_texel_alpha(int cell, int px, int py)
{
    const float x = (float)px / (float)(XB_ICON_CELL - 1) - 0.5f;
    const float y = (float)py / (float)(XB_ICON_CELL - 1) - 0.5f;
    const float cov = (cell == IC_DROPLET) ? xb_droplet_cov(x, y)
                                          : xb_icon_cov(cell, x, y);
    unsigned char a;

    if (cov <= 0.0f)
        return 0;
    a = (unsigned char)(cov * 255.0f);
    return a ? a : 1;
}

int main(void)
{
    static const char head[] =
        "/*\n"
        " * src/apps/xmb_atlas.h -- GENERATED, do not edit.\n"
        " *\n"
        " * The XMB icon atlas: one alpha byte per texel of the 384x384 sheet, in texel\n"
        " * order, row 0 being texture row 0 (v = 0).  Produced by\n"
        " * verify/tests/gen_xmb_atlas.c from the glyph geometry that the bar itself used\n"
        " * to evaluate at run time, so the icons are data now instead of 2.8 s of\n"
        " * software-float arithmetic on the way through the door.\n"
        " *\n"
        " * Only alpha is stored because that is all the bake ever wrote: an uncovered\n"
        " * texel was left at zero in every channel, and a covered one always got the same\n"
        " * ink (248,250,255) with its coverage as alpha.  xb_bake_textures() re-expands\n"
        " * the plane to RGBA on that rule.  Regenerate with `make xmb-atlas`; the sheet\n"
        " * and the geometry are checked against each other by `make test-xmb-atlas`.\n"
        " */\n"
        "#include \"xmb_icons.h\"\n\n";
    char line[16 * 6 + 2];
    int i, col = 0, len = 0;

    fputs(head, stdout);
    fputs("static const unsigned char xb_icon_alpha[XB_ICON_TEX * XB_ICON_TEX] = {\n",
          stdout);

    for (i = 0; i < XB_ICON_TEX * XB_ICON_TEX; i++) {
        const int x = i % XB_ICON_TEX, y = i / XB_ICON_TEX;
        const int cell = (y / XB_ICON_CELL) * XB_ATLAS_GRID + (x / XB_ICON_CELL);

        len += sprintf(line + len, "0x%02x,",
                       xb_texel_alpha(cell, x % XB_ICON_CELL, y % XB_ICON_CELL));
        if (++col == 16) {
            fwrite(line, 1, (size_t)len, stdout);
            putchar('\n');
            col = len = 0;
        }
    }
    fputs("};\n", stdout);
    /* 384*384 is a multiple of 16, so a leftover column means the sheet that shipped
     * is short: say so instead of writing a truncated array. */
    return col ? 1 : 0;
}
