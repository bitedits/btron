/*
 * ps2_gs_pack.c — field packing for the GS's bus words
 *
 * Every function here is a shift, a mask and an or: no memory, no register write, no DMA.
 * That is deliberate for increment 2 -- the words can be checked against hand-computed
 * bit patterns on a host (verify/tests/test_ps2_gs_pack.c), which is the only way a field
 * that lands one bit off is caught before there is a screen to look at.
 *
 * Inputs are masked rather than asserted.  The GS truncates a field that does not fit, so
 * truncating here is the hardware's behaviour, not a licence to pass nonsense: a caller
 * that hands a 21-bit texture base gets what the GS would get.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "ps2_gs_pack.h"

#define F(v, w, s) (((uint64_t)(v) & ((1ULL << (w)) - 1ULL)) << (s))

uint64_t gs_pack_prim(uint32_t op, uint32_t iip, uint32_t tme, uint32_t abe,
                      uint32_t ctxt, uint32_t fix)
{
    return F(op, 3, 0) | F(iip, 1, 3) | F(tme, 1, 4) | F(abe, 1, 6) |
           F(ctxt, 1, 9) | F(fix, 2, 10);
}

uint64_t gs_pack_rgbaq(uint32_t r, uint32_t g, uint32_t b, uint32_t a, uint32_t q)
{
    return F(r, 8, 0) | F(g, 8, 8) | F(b, 8, 16) | F(a, 8, 24) | F(q, 32, 32);
}

uint64_t gs_pack_st(float s, float t)
{
    /* The GS reads these two halves as IEEE-754 singles, so the words are the floats' own
     * bits.  A union is the portable way to say that on a target with no inttypes.h and no
     * soft-float library beyond ps2_builtins.c. */
    union { float f; uint32_t u; } cs, ct;

    cs.f = s;
    ct.f = t;

    return F(cs.u, 32, 0) | F(ct.u, 32, 32);
}

uint64_t gs_pack_uv(uint32_t u, uint32_t v)
{
    return F(u, 14, 0) | F(v, 14, 16);
}

uint32_t gs_vertex_coord16(int32_t px)
{
    /* Truncation, not rounding: the GS's own ftoi4 drops the fraction, and a vertex that
     * rounded up onto the next pixel would move a sprite's edge by a whole pixel on half
     * the screen.  With a whole-pixel argument there is nothing to drop, and the multiply
     * by 16 is the field's own scale -- exact, and the same word the float spelling
     * returned, which is what ps2_gs_pack.h's comment prices and the host gate sweeps.
     * The callers are clipped screen coordinates, so no saturation is done here -- a value
     * that overflows the 12-bit signed range is a caller that clipped nothing, and the
     * & 0xFFFF it then goes through is what the hardware would have seen. */
    return ((uint32_t)(px * 16) + GS_XYOFFSET_PAGE) & 0xFFFFu;
}

uint64_t gs_pack_xyz2(uint32_t x16, uint32_t y16, uint32_t z)
{
    return F(x16, 16, 0) | F(y16, 16, 16) | F(z, 32, 32);
}

uint64_t gs_pack_xyzf2(uint32_t x16, uint32_t y16, uint32_t z, uint32_t fog)
{
    return F(x16, 16, 0) | F(y16, 16, 16) | F(z, 24, 32) | F(fog, 8, 56);
}

uint64_t gs_pack_tex0(uint32_t tba, uint32_t tbw, uint32_t psm, uint32_t tw, uint32_t th,
                      uint32_t tint, uint32_t tfunc, uint32_t cba, uint32_t cpsm,
                      uint32_t csm, uint32_t csa, uint32_t cld)
{
    return F(tba, 14, 0) | F(tbw, 6, 14) | F(psm, 6, 20) | F(tw, 4, 26) |
           F(th, 4, 30) | F(tint, 1, 34) | F(tfunc, 2, 35) | F(cba, 13, 37) |
           F(cpsm, 4, 51) | F(csm, 1, 55) | F(csa, 5, 56) | F(cld, 3, 61);
}

uint64_t gs_pack_tex1(uint32_t lcm, uint32_t mxl, uint32_t mmag, uint32_t mmin,
                      uint32_t mtba, uint32_t l, uint32_t k)
{
    return F(lcm, 1, 0) | F(mxl, 3, 2) | F(mmag, 1, 5) | F(mmin, 3, 6) |
           F(mtba, 1, 9) | F(l, 2, 19) | F(k, 12, 32);
}

uint64_t gs_pack_clamp(uint32_t wms, uint32_t wmt, uint32_t minu, uint32_t maxu,
                       uint32_t minv, uint32_t maxv)
{
    return F(wms, 2, 0) | F(wmt, 2, 2) | F(minu, 10, 4) | F(maxu, 10, 14) |
           F(minv, 10, 24) | F(maxv, 10, 34);
}

uint64_t gs_pack_xyoffset(uint32_t x16, uint32_t y16)
{
    return F(x16, 16, 0) | F(y16, 16, 32);
}

uint64_t gs_pack_scissor(uint32_t x0, uint32_t x1, uint32_t y0, uint32_t y1)
{
    return F(x0, 11, 0) | F(x1, 11, 16) | F(y0, 11, 32) | F(y1, 11, 48);
}

uint64_t gs_pack_alpha(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t fix)
{
    return F(a, 2, 0) | F(b, 2, 2) | F(c, 2, 4) | F(d, 2, 6) | F(fix, 8, 32);
}

uint64_t gs_pack_test(uint32_t atest_en, uint32_t atest_meth, uint32_t atest_ref,
                      uint32_t atest_fail, uint32_t dtest_en, uint32_t dtest_md,
                      uint32_t ztest_en, uint32_t ztest_meth)
{
    return F(atest_en, 1, 0) | F(atest_meth, 3, 1) | F(atest_ref, 8, 4) |
           F(atest_fail, 2, 12) | F(dtest_en, 1, 14) | F(dtest_md, 1, 15) |
           F(ztest_en, 1, 16) | F(ztest_meth, 2, 17);
}

uint64_t gs_pack_frame(uint32_t fba, uint32_t fbw, uint32_t psm, uint32_t fmsk)
{
    return F(fba, 9, 0) | F(fbw, 6, 16) | F(psm, 4, 24) | F(fmsk, 32, 32);
}

uint64_t gs_pack_zbuf(uint32_t zba, uint32_t zsm, uint32_t zmsk)
{
    return F(zba, 9, 0) | F(zsm, 4, 24) | F(zmsk, 1, 32);
}

uint64_t gs_pack_bitbltbuf(uint32_t sba, uint32_t sbw, uint32_t spsm,
                           uint32_t dba, uint32_t dbw, uint32_t dpsm)
{
    return F(sba, 14, 0) | F(sbw, 6, 16) | F(spsm, 6, 24) |
           F(dba, 14, 32) | F(dbw, 6, 48) | F(dpsm, 6, 56);
}

uint64_t gs_pack_trxpos(uint32_t sasx, uint32_t sasy, uint32_t dasx, uint32_t dasy,
                        uint32_t dir)
{
    return F(sasx, 11, 0) | F(sasy, 11, 16) | F(dasx, 11, 32) | F(dasy, 11, 48) |
           F(dir, 2, 59);
}

uint64_t gs_pack_trxreg(uint32_t w, uint32_t h)
{
    return F(w, 12, 0) | F(h, 12, 32);
}

uint64_t gs_pack_trxdir(uint32_t dir)
{
    return F(dir, 2, 0);
}

uint64_t gs_pack_giftag(uint32_t nloop, uint32_t eop, uint32_t pre, uint64_t prim,
                        uint32_t flg, uint32_t nreg)
{
    return F(nloop, 15, 0) | F(eop, 1, 15) | F(pre, 1, 46) | F(prim, 11, 47) |
           F(flg, 2, 58) | F(nreg, 4, 60);
}

uint64_t gs_pack_regs(const uint32_t *regs, uint32_t n)
{
    uint64_t out = 0;
    /* Ten nibbles, because that is how wide the field is: the SDK's flat triangle names six
     * registers and its rect outline names eight, so a cap of four would drop a vertex and leave the
     * primitive waiting for a drawing kick that never comes.  ps2_gs_pack.h carries the law. */
    if (n > GS_REGS_N_MAX) n = GS_REGS_N_MAX;
    for (uint32_t i = 0; i < n; i++) {
        out |= F(regs[i], 4, i * 4);
    }
    return out;
}

uint64_t *gs_pack_pair(uint64_t *cursor, uint64_t data, uint32_t gp_offset)
{
    cursor[0] = data;
    cursor[1] = (uint64_t)(gp_offset & 0x7Fu);
    return cursor + 2;
}
