/*
 * src/apps/xmb_wave.c - the launcher's waving surface, on the Emotion EE's FPU
 *
 * Plan row 8 of doc/md/PS2.md.  The header says why the file exists; what is only
 * visible here is what it took to make the move legal on this CPU, because there are
 * three hazards and they are three different problems:
 *
 *  - *Inside* 4*pi, `sinf`/`cosf` are `include/gl/math.h`'s float fold plus a five-term
 *    polynomial, every operation of which is single precision, so this unit inlines them
 *    and the Emotion EE executes them.  Their one non-arithmetic step is the fold's turn
 *    count, `(float)(int)(x / 2*pi)`, and `trunc.w.s` is the instruction this CPU does not
 *    answer: the destination keeps the previous result, so a fold that cannot truncate
 *    folds every angle to somebody else's residue.  The header now spells that cast
 *    `BTRON_F2I()`, which under the FPU flag is `ps2_sf_fixsfsi()` -- the exponent-band
 *    truncate the soft runtime's own `__fixsfsi` already runs -- and a plain `(int)`
 *    everywhere else.
 *
 *  - *Outside* 4*pi -- the noise hash's integer lattice coordinates always, and every
 *    phase once the effect clock has run long enough -- the reduction needs double
 *    precision, and on this CPU a double is not slow but absent: `ldc1`/`sdc1` and every
 *    `.d` form do nothing at all.  Those arguments go back to the soft unit through
 *    `xb_sinf_far_u32()` / `xb_cosf_far_u32()`, bits in and bits out.  That is also what
 *    keeps `btron_fp_on`'s run-time valve honest: a `PS2_FP=soft` build compiles this same
 *    source `-msoft-float`, the header's own `sinf`/`cosf` come back, and no arithmetic
 *    changes.
 *
 *  - The third is register allocation rather than instruction choice, and it is the one
 *    defect in this port that reading the source cannot predict.  Under the O32 FP ABI the
 *    callee-saved single-precision registers are $f20, $f22 ... $f30 -- even members of the
 *    even/odd *pairs* -- and a function that uses one preserves it with `sdc1` and restores
 *    it with `ldc1`.  This CPU ignores both instructions, so on the Emotion EE there is no
 *    way at all to preserve a callee-saved float: the register file has 32 singles, and half
 *    of them are only ever callable as scratch.  Any use of $f20-$f31 in a unit that is
 *    called therefore silently destroys the caller's value, and it happens for two reasons
 *    that both look correct in the source:
 *
 *    - the working set exceeds the ten caller-saved singles, or
 *    - a loop-invariant float (`1.0f`, `13.0f`, a loaded clock scalar) is hoisted out of a
 *      loop that calls, because keeping it in a register across the call is free for the
 *      allocator on a machine where callee-saved registers really are preserved.
 *
 *    Measured on this file's first arrangement: 52 pair stores, from a unit with no double
 *    anywhere in it.  Neither reason is reachable by a source rewrite alone -- the second is
 *    LICM, and the constants are in the loop -- so the build turns off the allocator's
 *    preference as well: `-mllvm -regalloc-csr-first-time-cost=1000000000` prices a
 *    callee-saved register's first use at a billion, which makes the greedy allocator keep
 *    every live value in $f0-$f19 and spill to `swc1`/`lwc1` stack slots instead, both of
 *    which this CPU does answer.  That is 52 pair stores down to 4; the remaining 4 were one
 *    genuine working-set overflow, `wv_shade_cc()`'s eleven live floats, which is now two
 *    passes.  The flag is not a relic of that first arrangement: on the file as it stands
 *    now, compiling without it puts 14 pair stores and 32 references to $f20-$f31 back into
 *    this unit, against the shipped object's zero of each.  (An unknown `-mllvm` argument
 *    fails the compile rather than being ignored, so a future toolchain that renames it
 *    stops the build instead of quietly re-opening the hole.  `scripts/test_ps2.sh` carries
 *    both halves of the promise: a row naming this object, which requires the arithmetic to
 *    be present and the callee-saved half of the register file to be absent, and the
 *    image-wide byte scan, which says no `ldc1`/`sdc1` encoding exists anywhere in it.)
 *
 *    The source is still written to the shape that makes the guarantee need no help --
 *    **no float survives a call** -- in three constructs that each appear below:
 *
 *    a. a pass ends by storing to memory and the next pass loads from it, which is why the
 *       sheet is computed in eight passes with tables between them, and why each
 *       is `WV_PASS` rather than inlined into `xb_wave_step()`, which would be live over the
 *       coordinates, the lattice and the projection at once;
 *    b. a loop that calls is *bare*: integers and indices only across the call, with the
 *       values it needs re-loaded after it -- the hash's eight-corner loop and the two trig
 *       table loops, each split from its arithmetic loop for exactly this reason;
 *    c. a function whose working set is large calls nothing -- `wv_shade_nm()` holds the
 *       five grid differences and the first quotient, and reaches no function at all, so
 *       even a billion-dollar allocator has nothing to preserve.
 *
 *    The comparisons that decide the tiers are written `BTRONFLT_LT()`, a bit-domain test in
 *    `src/drivers/ps2/ps2_fp.h`, because `a > b` lowers to `c.ule.s` and this CPU does not
 *    decode the unordered family: it leaves the condition code holding whatever the
 *    *previous* compare said, so a guard written that way routes angles to the wrong tier
 *    while looking exactly right in the source.
 *
 * What the move does not do is change an answer.  Every expression here is the same
 * operations on the same operands in the same order as the line it replaced; the screen
 * projection is the same `(e + 1) * 0.5 * w`, evaluated per grid point instead of per
 * vertex; and where a value that used to be recomputed twelve times a cell is computed
 * once, it is recomputed *nothing*, because the lattice index is an integral float and its
 * neighbours are that integer plus a whole number.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "xmb_wave.h"
#include <math.h>

/* Every function below that computes anything is one pass, and no pass is folded into the
 * one that calls it -- corollary (a) of the register-file rule at the top of this file.
 * The passes hand their results through memory, so each one is alive only over its own
 * handful of values, while a single `xb_wave_step()` inlined over all of them would be
 * alive over the sheet's coordinates, the noise's lattice and the projection's two scale
 * factors at once, and the eleventh live float is where clang reaches for a callee-saved
 * register and preserves it with the `sdc1` pair-store this CPU ignores.  That is also why
 * the trig and the root below are calls rather than the header's inlined series: the fold
 * is six or seven live values on its own, and inlined it adds them to the loop that asked
 * for the angle.  Row 7's geometry kernel met the same thing and was split the same way;
 * the price here is one `jal` per angle against the thirty operations it calls. */
#define WV_PASS __attribute__((noinline))

/* ── The two tiers, split by what this CPU can execute ─────────────────────────── */

#if defined(BTRON_PS2_TARGET) && defined(BTRON_PS2_FP_HW)

/* The header's own boundary -- `sinf`/`cosf` fold in float down to 4*pi and hand anything
 * wider to the double tier -- written as one comparison of a magnitude against a positive
 * constant, because the shape of the test decides the instruction here and only its result
 * decides the answer.  `x > -12.5663706f` is `c.ule.s`, and LLVM canonicalising the
 * constant to the right hand side means operand order in the source is not a fix;
 * `BTRONFLT_LT()` is the spelling that cannot be canonicalised back, because it never
 * reaches an FP condition code at all.
 *
 * It costs nothing in answers.  `|x| < C` and `-C < x && x < C` agree on every finite x
 * because fabs is exact, and they agree on NaN too -- the magnitude of a NaN is a NaN, so
 * both forms are false and both take the far tier, which is where the header's own
 * `sinf` would send it. */
#define WV_NEAR(x) BTRONFLT_LT(fabsf(x), 12.5663706f)

WV_PASS static float wv_sinf(float x)
{
    float s, c;

    if (WV_NEAR(x)) {
        btron_sincosf(x, &s, &c);
        return s;
    }
    return xb_float_u32(xb_sinf_far_u32(xb_bits_f(x)));
}

WV_PASS static float wv_cosf(float x)
{
    float s, c;

    if (WV_NEAR(x)) {
        btron_sincosf(x, &s, &c);
        return c;
    }
    return xb_float_u32(xb_cosf_far_u32(xb_bits_f(x)));
}

/* The root the fragment stage's normal needs, as a call for the same reason the two above
 * are: `include/gl/math.h`'s `sqrtf` is a mantissa fold, a subnormal scale and three Newton
 * steps, which is five or six live values that would otherwise join the five grid
 * differences of the cell that asked for it.  Same function, same operations, same answer. */
WV_PASS static float wv_sqrtf(float x)
{
    return sqrtf(x);
}

#else /* the soft build and every host build: the header's own dispatch, unchanged */

#define wv_sinf sinf
#define wv_cosf cosf
#define wv_sqrtf sqrtf

#endif

/* ── Ribbon: the waving surface ─────────────────────────────────────────────────── */

/* iqhash/noise/xmb_noise2 from pipeline_xmb_ribbon.cg.h, and the vertex stage
 * of that shader, evaluated per grid point.  vEC is kept so the fragment
 * stage's normal can be recovered from grid differences. */

/* The sheet itself.  These six used to be four file-static arrays in `src/apps/xmb.c`,
 * which is the shape that let a *reader* (the ribbon's draw loop, the particle surface
 * hook, the calibrate row) recompute the screen projection from the clip-space value:
 * `(e0 + 1) * 0.5 * w` was evaluated once per vertex per primitive instead of once per
 * grid point per step, and the two extra arrays are that saved work rather than new
 * state.  Moving the storage here is not a change of owner -- the producer of a table is
 * the file that writes every element of it, and xmb.c now reads what this pass leaves in
 * memory, which is also the only channel a float may cross between the image's two
 * floating-point conventions. */
float xb_rib_e0[XB_RIBBON_ROWS][XB_RIBBON_COLS];   /* clip x, the surface's u */
float xb_rib_e1[XB_RIBBON_ROWS][XB_RIBBON_COLS];   /* clip y, negated for the screen */
float xb_rib_e2[XB_RIBBON_ROWS][XB_RIBBON_COLS];   /* clip z */
float xb_rib_bri[XB_RIBBON_ROWS][XB_RIBBON_COLS];  /* baked per-vertex brightness */
float xb_rib_px[XB_RIBBON_ROWS][XB_RIBBON_COLS];   /* (e0 + 1) * 0.5 * w_px */
float xb_rib_py[XB_RIBBON_ROWS][XB_RIBBON_COLS];   /* (1 - e1) * 0.5 * h_px */

static float xb_frac(float v) { return v - floorf(v); }

/* The effect clock's four derived scalars, in memory rather than in `xb_wave_step()`'s
 * registers: `wv_time()` computes them once, every pass that wants one loads it, and the
 * values are the same operations on the same `t` the old inline expressions performed --
 * `t/5` and `t/2` used to be recomputed for all 32 columns, `t/10` for all 32 rows. */
#define WV_T_HALF   0     /* t / 2          */
#define WV_T_FIFTH  1     /* t / 5          */
#define WV_T_TENTH  2     /* t / 10         */
#define WV_T_V3Y7   3     /* (-t/100) * 7   */
static float s_tk[4];

WV_PASS static void wv_time(uint32_t t_bits)
{
    const float t = xb_float_u32(t_bits);

    s_tk[WV_T_HALF]  = t / 2.0f;
    s_tk[WV_T_FIFTH] = t / 5.0f;
    s_tk[WV_T_TENTH] = t / 10.0f;
    s_tk[WV_T_V3Y7]  = (-t / 100.0f) * 7.0f;
}

/* iqhash's argument is always one integer of the noise lattice -- it is built as
 * px + py*57 + 113*pz from three floors, and the eight calls a cell makes add whole
 * numbers to it -- and |n| sits far outside sinf's 4*pi guard, so every answer here
 * is a software double sine.  One frame asked for 32,768 of them while the lattice
 * under the sheet holds a few hundred distinct integers, because neighbouring cells
 * share their corners: measured, those 32,768 calls were 9.6 s of the 10.4 s a paint
 * cost.  Remembering the value per integer leaves the arithmetic bit-identical -- the
 * same sine, the same rounding, the same sheet -- and asks for one only when the wave
 * has scrolled into lattice points it has not visited yet. */
#define XB_HASH_SLOTS 2048u
#define XB_HASH_SHIFT (32u - 11u)
static unsigned char s_hash_used[XB_HASH_SLOTS];    /* zero .bss means "slot empty" */
static int           s_hash_key[XB_HASH_SLOTS];
static float         s_hash_val[XB_HASH_SLOTS];
unsigned             xb_hash_hits, xb_hash_miss;

/* The key is an `int` rather than the float the shader hands over, because the float is
 * already an integer: it is `px + py*57 + 113*pz` built from three floors, so the eight
 * lattice neighbours of a cell are that value plus a whole number, and their
 * `float -> int` conversion is the same conversion plus that number.  Taking the one cast
 * per cell instead of twelve is 12,288 of the frame's `fs2i` calls down to 1,024 -- and
 * `float -> int` is the one operation that can never reach this CPU's FPU, there being no
 * trunc.w.s to reach.
 *
 * The miss path is a function of its own so the hit path stays inline in the caller's
 * probe and the cold call is the only thing a bare hash loop is ever live across. */
WV_PASS static float wv_hash_cold(int key, unsigned slot)
{
    const float v = xb_frac(wv_sinf((float)key) * 43758.5453f);

    s_hash_used[slot] = 1;
    s_hash_key[slot]  = key;
    s_hash_val[slot]  = v;
    xb_hash_miss++;
    return v;
}

static float wv_hash(int key)
{
    /* Fibonacci hashing, because the bits that vary in n are its low ones: indexing
     * straight by n would drop every point of a row into the same slot. */
    const unsigned slot = ((unsigned)key * 2654435761u) >> XB_HASH_SHIFT;

    if (s_hash_used[slot] && s_hash_key[slot] == key) {
        xb_hash_hits++;
        return s_hash_val[slot];
    }
    return wv_hash_cold(key, slot);
}

/* The eight corners and the three blend weights of one noise sample, in memory: written
 * by the loop that fills them and read by the bilinear product that consumes them, so the
 * loop that calls the hash is alive over nothing but integers.  The reference form asked
 * for two of the eight corners twice, which is four extra probes and four extra casts a
 * frame for no value at all; `s_off` is that deduplicated set, in the order the product
 * below reads them. */
static float s_h[8];
static float s_w[3];
static const int s_off[8] = { 0, 1, 57, 58, 113, 114, 170, 171 };

WV_PASS static float xb_noise3(float x, float y, float z)
{
    const float px = floorf(x), py = floorf(y), pz = floorf(z);
    float fx = xb_frac(x), fy = xb_frac(y);
    const int nk = BTRON_F2I(px + py * 57.0f + 113.0f * pz);
    float a, b, c1, c2;
    int j;

    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    s_w[0] = fx;
    s_w[1] = fy;
    s_w[2] = xb_frac(z);

    /* Bare across the call: `nk` and `j` are integers and the results land in memory.
     * Storing them before the blend is what keeps the eight of them out of FPRs that this
     * function would then have to preserve across the very loop that asks for them. */
    for (j = 0; j < 8; j++)
        s_h[j] = wv_hash(nk + s_off[j]);

    a  = s_h[0] + (s_h[1] - s_h[0]) * s_w[0];
    b  = s_h[2] + (s_h[3] - s_h[2]) * s_w[0];
    c1 = a + (b - a) * s_w[1];
    a  = s_h[4] + (s_h[5] - s_h[4]) * s_w[0];
    b  = s_h[6] + (s_h[7] - s_h[6]) * s_w[0];
    c2 = a + (b - a) * s_w[1];
    return c1 + (c2 - c1) * s_w[2];
}

/* The two grid coordinates the sheet is separable in.  Two of its three cosines depend on
 * one coordinate alone -- cos(4*gz) is the same for a whole row, cos(2*gx - t/2) the same
 * for a whole column -- and the noise's three coordinates factor the same way.  Lifting
 * them out costs the same 128 angles the inner loop used to pay 8,192 of, and every
 * remaining expression is evaluated in the same order as before, so the sheet is
 * bit-identical. */
static float s_gxv[XB_RIBBON_COLS], s_v3x7[XB_RIBBON_COLS], s_colcos[XB_RIBBON_COLS];
static float s_gz[XB_RIBBON_ROWS], s_lean[XB_RIBBON_ROWS], s_v3z7[XB_RIBBON_ROWS];
static float s_rowcos[XB_RIBBON_ROWS];
static float s_nz[XB_RIBBON_ROWS][XB_RIBBON_COLS];

/* Per column: the grid abscissa, the noise's first coordinate, and cos(2*gx - t/2). */
WV_PASS static void wv_columns(void)
{
    int c;

    /* No call in this loop, so its arithmetic and its store can share registers; the
     * cosine loop below is the same pass split off because it *does* call. */
    for (c = 0; c < XB_RIBBON_COLS; c++) {
        s_gxv[c]  = (float)c / (float)(XB_RIBBON_COLS - 1) * 2.0f - 1.0f;
        s_v3x7[c] = ((s_gxv[c] - s_tk[WV_T_FIFTH]) / 4.0f) * 7.0f;
    }
    for (c = 0; c < XB_RIBBON_COLS; c++)
        s_colcos[c] = wv_cosf(s_gxv[c] * 2.0f - s_tk[WV_T_HALF]);
}

/* Per row: the grid ordinate, the two phases that lean with the clock, and cos(4*gz). */
WV_PASS static void wv_rows(void)
{
    int r;

    for (r = 0; r < XB_RIBBON_ROWS; r++) {
        const float gz = (float)r / (float)(XB_RIBBON_ROWS - 1) * 2.0f - 1.0f;

        s_gz[r]    = gz;
        s_lean[r]  = gz + s_tk[WV_T_TENTH];
        s_v3z7[r]  = (gz - s_tk[WV_T_TENTH]) * 7.0f;
    }
    for (r = 0; r < XB_RIBBON_ROWS; r++)
        s_rowcos[r] = wv_cosf(s_gz[r] * 4.0f);
}

/* The noise field, one sample per grid point. */
WV_PASS static void wv_noise(void)
{
    int r, c;

    for (r = 0; r < XB_RIBBON_ROWS; r++)
        for (c = 0; c < XB_RIBBON_COLS; c++)
            s_nz[r][c] = xb_noise3(s_v3x7[c], s_tk[WV_T_V3Y7], s_v3z7[r]);
}

/* The vertex stage: the sheet itself, in clip space.  The cosine is called for and dies
 * at the boundary; `nz` is loaded after it, which is why the noise is a pass of its own
 * rather than an argument expression here. */
WV_PASS static void wv_sheet(void)
{
    int r, c;

    for (r = 0; r < XB_RIBBON_ROWS; r++)
        for (c = 0; c < XB_RIBBON_COLS; c++) {
            const float lc = wv_cosf(s_lean[r] + s_gxv[c]);
            const float nz = s_nz[r][c];
            const float vy = s_rowcos[r] * lc / 8.0f
                           - nz / 15.0f - s_colcos[c] / 5.0f + 0.3f;

            xb_rib_e0[r][c] = s_gxv[c];
            xb_rib_e1[r][c] = -vy;
            xb_rib_e2[r][c] = s_gz[r] - nz / 15.0f;
        }
}

/* Fragment stage of pipeline_xmb_ribbon.cg.h:
 *   X = ddx(vEC);  Y = -ddy(vEC);  normal = normalize(cross(X, Y));
 *   c = 1 - dot(normal, up), up = (0,0,1);   bright = (1 - cos(c*c)) / 13.
 * ddx/ddy are per *screen* pixel, not per grid step.  Screen x tracks the
 * column index alone (ex = gx) and screen y tracks ey, so inverting the
 * (column,row) -> (x,y) Jacobian gives
 *   X = (1, 0, (az - ay*n) / ax)   with n = bz / by
 *   Y = (0, 1, bz / by)
 * (ex does not change along a row, which is what kills the x-component of
 * X's partner).  cross(X, -Y) then has z = -1, so
 *   dot(normal, up) = -1 / sqrt(M*M + n*n + 1)
 * and c = 1 + 1/len lies in [1, 2], so the sheet can never darken the
 * background and its lift runs from (1-cos 1)/13 = 3.5% at its flattest to
 * the expression's peak, 2/13 = 15.4%, where cc*cc reaches pi on a steep
 * fold (it falls back to (1-cos 4)/13 = 12.7% at the steepest cc of 2).
 *
 * Two passes rather than one, and the split is the register file's, not the maths': the
 * five grid differences, the two quotients and the inlined root's own temporaries are
 * eleven live floats, and the eleventh is a callee-saved pair the caller cannot preserve on
 * this CPU.  So the differences -- which call nothing -- end in two tables, and the pass
 * that wants the root and the cosine is alive over one quotient at a time.  `s_flat` is the
 * degenerate cell's answer carried across that boundary as an integer, because a float
 * comparison would be a condition code. */
static float s_nn[XB_RIBBON_ROWS][XB_RIBBON_COLS];
static float s_mm[XB_RIBBON_ROWS][XB_RIBBON_COLS];
static unsigned char s_flat[XB_RIBBON_ROWS][XB_RIBBON_COLS];

WV_PASS static void wv_shade_nm(void)
{
    int r, c;

    for (r = 0; r < XB_RIBBON_ROWS; r++) {
        int r0 = r > 0 ? r - 1 : r;
        int r1 = r < XB_RIBBON_ROWS - 1 ? r + 1 : r;
        for (c = 0; c < XB_RIBBON_COLS; c++) {
            int c0 = c > 0 ? c - 1 : c;
            int c1 = c < XB_RIBBON_COLS - 1 ? c + 1 : c;
            float ax = xb_rib_e0[r][c1] - xb_rib_e0[r][c0];
            float ay = xb_rib_e1[r][c1] - xb_rib_e1[r][c0];
            float az = xb_rib_e2[r][c1] - xb_rib_e2[r][c0];
            float by = xb_rib_e1[r1][c] - xb_rib_e1[r0][c];
            float bz = xb_rib_e2[r1][c] - xb_rib_e2[r0][c];

            if (BTRONFLT_LT(fabsf(ax), 1e-6f) || BTRONFLT_LT(fabsf(by), 1e-6f)) {
                /* Flat in screen y, or the last column: the screen derivatives blow up and
                 * the normal tends to (0,0,-1), which is cc = 1 -- the shader's minimum
                 * lift, applied in the pass that reads this flag. */
                s_flat[r][c] = 1u;
            } else {
                float n  = bz / by;

                s_flat[r][c] = 0u;
                s_nn[r][c] = n;
                s_mm[r][c] = (az - ay * n) / ax;
            }
        }
    }
}

WV_PASS static void wv_shade(void)
{
    int r, c;

    for (r = 0; r < XB_RIBBON_ROWS; r++)
        for (c = 0; c < XB_RIBBON_COLS; c++) {
            float cc;

            if (s_flat[r][c]) {
                cc = 1.0f;
            } else {
                float n = s_nn[r][c];
                float m = s_mm[r][c];

                /* Both quotients exist only inside this expression, so the call's boundary
                 * is alive over its argument and nothing else. */
                cc = 1.0f + 1.0f / wv_sqrtf(m * m + n * n + 1.0f);
            }
            xb_rib_bri[r][c] = (1.0f - wv_cosf(cc * cc)) / 13.0f;
        }
}

/* The same sheet in screen pixels, which is what the vertices are drawn from and what the
 * particles ride on.  Written once per grid point per step rather than once per vertex per
 * primitive -- the zigzag strip hands each interior point to two triangles, and the strip
 * the draw loop builds emits 3,968 vertices over 1,024 points.  Left to right, as the
 * expression was written there: a pixel coordinate is the same float the same operations
 * produce in the same order.  The window size enters as integers for the same reason the
 * clock does: nothing of this pass's needs to survive the pass's own boundary. */
WV_PASS static void wv_project(int w_px, int h_px)
{
    const float wf = (float)w_px, hf = (float)h_px;
    int r, c;

    for (r = 0; r < XB_RIBBON_ROWS; r++)
        for (c = 0; c < XB_RIBBON_COLS; c++) {
            xb_rib_px[r][c] = (xb_rib_e0[r][c] + 1.0f) * 0.5f * wf;
            xb_rib_py[r][c] = (1.0f - xb_rib_e1[r][c]) * 0.5f * hf;
        }
}

void xb_wave_step(uint32_t t_bits, int w_px, int h_px)
{
    wv_time(t_bits);
    wv_columns();
    wv_rows();
    wv_noise();
    wv_sheet();
    wv_shade_nm();
    wv_shade();
    wv_project(w_px, h_px);
}
