/*
 * ps2_builtins.c - the compiler-runtime calls a -msoft-float MIPS/O32 image needs
 *
 * The Emotion Engine's FPU is single-precision: every ldc1/sdc1 and every `.d`
 * COP1 instruction clang emits for a `double` is illegal on this CPU, does
 * nothing there, and silently turns each double computation into garbage (the
 * PCSX2 log calls each one `Unknown R5900 COP1:`, 2,093 rows for one XMB frame).
 * -msoft-float is therefore built into the whole PS2 image, which moves every
 * floating-point operation -- float and double alike -- out of the FPU and into a
 * call to one of these symbols.  The link is -nostdlib, so the port carries the
 * runtime itself: the 32 names below are exactly what clang's relocations ask for
 * across the PS2 sources.
 *
 * Under -msoft-float the O32 ABI hands a floating-point value to a callee the same
 * way it hands a 64-bit integer: a `double` argument is an even/odd GPR pair with
 * the low word in the even register, a `double` result is $2:$3, a `float` is one
 * register.  These definitions therefore take and return integer types holding the
 * IEEE-754 bit pattern, which is register-for-register the call the compiler
 * emitted -- no caller has to know the difference.
 *
 * The compares return -1/0/1 for less/equal/greater and, for a NaN, the value that
 * makes the caller's test read "false": callers test `< 0` for lt, `< 1` for le,
 * `> -1` for ge, `> 0` for gt, `== 0` for eq and `!= 0` for ne (all four of those
 * tests came out of a disassembly of this very flag, not out of a manual).
 *
 * The arithmetic is exact IEEE-754 round-nearest-even -- subnormals, NaNs,
 * infinities, overflow and underflow included -- and is checked bit for bit on the
 * host against its own hardware FPU: see `make test-ps2-softfloat`.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdint.h>
#include "ps2_fp.h"

/* ── One engine, two formats ─────────────────────────────────────────────── */

typedef struct {
    uint32_t frac_bits;      /* mantissa bits in the encoding */
    uint32_t sign_shift;     /* 31 or 63 */
    uint32_t exp_max;        /* the exponent field that means inf/NaN */
    int32_t  exp_bias;
    uint64_t mag_mask;       /* the low frac_bits set */
    uint64_t quiet;          /* the default quiet NaN */
} fp_fmt;

static const fp_fmt FP_F32 = { 23, 31, 0xFFu, 127, 0x7FFFFFULL, 0x7FC00000ULL };
static const fp_fmt FP_F64 = { 52, 63, 0x7FFu, 1023, 0xFFFFFFFFFFFFFULL,
                               0x7FF8000000000000ULL };

#define FPK_ZERO 0
#define FPK_FIN  1
#define FPK_INF  2
#define FPK_NAN  3
#define FPK_BIG  4                   /* the integer part does not fit 64 bits */

/* A finite value is carried as `magnitude * 2^exp` with an explicit integer
 * significand, so no implicit bit and no per-format exponent law leaks into the
 * operations below. */
static int fp_decode(uint64_t bits, const fp_fmt *f, uint32_t *sign,
                     uint64_t *sig, int32_t *exp)
{
    const uint64_t m = bits & f->mag_mask;
    const uint32_t e = (uint32_t)((bits >> f->frac_bits) & f->exp_max);

    *sign = (uint32_t)((bits >> f->sign_shift) & 1u);
    *sig = m;
    *exp = 0;
    if (e == f->exp_max) return m ? FPK_NAN : FPK_INF;
    if (e == 0) {
        if (m == 0) return FPK_ZERO;
        *exp = 1 - f->exp_bias - (int32_t)f->frac_bits;
        return FPK_FIN;                                    /* subnormal */
    }
    *sig = m | ((uint64_t)1 << f->frac_bits);
    *exp = (int32_t)e - f->exp_bias - (int32_t)f->frac_bits;
    return FPK_FIN;
}

static uint64_t fp_bits(const fp_fmt *f, uint32_t sign, uint32_t biased, uint64_t field)
{
    return ((uint64_t)sign << f->sign_shift) | ((uint64_t)biased << f->frac_bits) | field;
}

static uint64_t fp_inf(const fp_fmt *f, uint32_t sign) { return fp_bits(f, sign, f->exp_max, 0); }
static uint64_t fp_zero(const fp_fmt *f, uint32_t sign) { return (uint64_t)sign << f->sign_shift; }

/* A NaN keeps the payload it came with and leaves quiet; the result is signless,
 * as every NaN on this path is. */
static uint64_t fp_quiet(uint64_t a, const fp_fmt *f)
{
    return (a | ((uint64_t)1 << (f->frac_bits - 1))) & ~((uint64_t)1 << f->sign_shift);
}

static uint64_t fp_nan_convert(uint64_t a, const fp_fmt *from, const fp_fmt *to)
{
    uint64_t payload = a & from->mag_mask;
    if (to->frac_bits > from->frac_bits) payload <<= to->frac_bits - from->frac_bits;
    else payload >>= from->frac_bits - to->frac_bits;
    payload |= (uint64_t)1 << (to->frac_bits - 1);
    return fp_bits(to, 0u, to->exp_max, payload);
}

static int32_t fp_msb(uint64_t v)
{
    int32_t r = 0;
    if (v >> 32) { r += 32; v >>= 32; }
    if (v >> 16) { r += 16; v >>= 16; }
    if (v >> 8)  { r += 8;  v >>= 8;  }
    if (v >> 4)  { r += 4;  v >>= 4;  }
    if (v >> 2)  { r += 2;  v >>= 2;  }
    if (v >> 1)  { r += 1;  }
    return r;
}

/* Split at bit n (0 <= n, and n >= 64 meaning "all of it") into the part that
 * stays and the part that leaves.  Every shift below is a 32-bit one: shifting a
 * 64-bit value by a variable amount is precisely the libcall this file exists to
 * keep out of the image. */
static void fp_split(uint64_t v, uint32_t n, uint64_t *keep, uint64_t *drop)
{
    const uint32_t hi = (uint32_t)(v >> 32), lo = (uint32_t)v;
    uint32_t kh, kl, dh, dl;

    if (n == 0) {
        kh = hi; kl = lo; dh = 0; dl = 0;
    } else if (n < 32) {
        kh = hi >> n; kl = (lo >> n) | (hi << (32 - n)); dh = 0;
        dl = lo & (((uint32_t)1 << n) - 1u);
    } else if (n == 32) {
        kh = 0; kl = hi; dh = 0; dl = lo;
    } else if (n < 64) {
        const uint32_t m = n - 32;
        kh = 0; kl = hi >> m; dh = hi & (((uint32_t)1 << m) - 1u); dl = lo;
    } else {
        kh = 0; kl = 0; dh = hi; dl = lo;
    }

    *keep = ((uint64_t)kh << 32) | kl;
    *drop = ((uint64_t)dh << 32) | dl;
}

static uint64_t fp_shl(uint64_t v, uint32_t n)   /* the caller guarantees the bits stay */
{
    while (n >= 32) { v <<= 32; n -= 32; }
    if (n) v <<= n;
    return v;
}

static int fp_is_nan(uint64_t bits, const fp_fmt *f)
{
    return ((bits >> f->frac_bits) & f->exp_max) == f->exp_max && (bits & f->mag_mask) != 0;
}

/* Round `magnitude * 2^exp` to nearest-even in f and encode it.  `extra` says
 * whether the true magnitude continues just below the significand (+1), stops
 * there (0), or falls short of it by a less-than-one-ulp remainder (-1); only a
 * subtraction can end in -1, and telling the three apart is what sends an exact
 * tie the right way. */
static uint64_t fp_pack(uint32_t sign, uint64_t sig, int32_t exp, int32_t extra,
                        const fp_fmt *f)
{
    const int32_t frac = (int32_t)f->frac_bits;
    const int32_t e_min = 1 - f->exp_bias;
    uint64_t kept, dropped;
    int32_t drop, up = 0, biased, m, E;

    if (sig == 0) return fp_zero(f, sign);

    m = fp_msb(sig);
    E = exp + m;                              /* the value's unbiased exponent */

    /* How many bits leave the bottom to reach the grid we will encode on: the
     * normal grid keeps frac+1 bits, the subnormal grid only frac. */
    if (E >= e_min) drop = m - frac;
    else            drop = (e_min - frac) - exp;

    if (drop <= 0) {
        kept = fp_shl(sig, (uint32_t)(-drop));
        dropped = 0;
    } else if (drop > 64) {
        return fp_zero(f, sign);              /* below half of the smallest one */
    } else {
        fp_split(sig, (uint32_t)drop, &kept, &dropped);
        const uint64_t half = (uint64_t)1 << (uint32_t)(drop - 1);
        if (dropped > half) up = 1;
        else if (dropped == half && (extra > 0 || (extra == 0 && (kept & 1u)))) up = 1;
    }

    kept += (uint64_t)up;

    if (E < e_min) {
        const uint64_t min_normal = (uint64_t)1 << (uint32_t)frac;
        if (kept == min_normal) return fp_bits(f, sign, 1u, 0);  /* rounded up out of
                                                                 * the subnormal band */
        if (kept == 0) return fp_zero(f, sign);
        return fp_bits(f, sign, 0u, kept);
    }

    if (kept >> (uint32_t)(frac + 1)) { kept >>= 1; E += 1; }   /* carried one out */
    biased = E + f->exp_bias;
    if ((uint32_t)biased >= f->exp_max) return fp_inf(f, sign);
    return fp_bits(f, sign, (uint32_t)biased, kept - ((uint64_t)1 << (uint32_t)frac));
}

/* a + b, or a - b when flip_b. */
static uint64_t fp_addsub(uint64_t a, uint64_t b, uint32_t flip_b, const fp_fmt *f)
{
    uint32_t sa, sb;
    uint64_t ma, mb;
    int32_t ea, eb;
    const int ka = fp_decode(a, f, &sa, &ma, &ea);
    const int kb = fp_decode(b, f, &sb, &mb, &eb);
    int32_t grid;
    uint64_t ka_m, kb_m, da, db;

    if (ka == FPK_NAN) return fp_quiet(a, f);
    if (kb == FPK_NAN) return fp_quiet(b, f);
    if (flip_b) sb ^= 1u;
    if (ka == FPK_INF || kb == FPK_INF) {
        if (ka == FPK_INF && kb == FPK_INF && sa != sb) return f->quiet;   /* inf - inf */
        return (ka == FPK_INF) ? fp_inf(f, sa) : fp_inf(f, sb);
    }
    if (ma == 0 || mb == 0) {
        if (ma == 0 && mb == 0) return fp_zero(f, (sa == sb) ? sa : 0u);
        return fp_pack(ma ? sa : sb, ma ? ma : mb, ma ? ea : eb, 0, f);
    }

    /* Line both magnitudes up on a grid three bits *below* the larger exponent:
     * those three are the guard, round and sticky bits, and keeping them inside the
     * significand is what lets an exactly half-way sum, like 1/3 + 2/3, round up the
     * way the FPU does.  Only an operand that is shifted down can have bits below
     * the grid, so at most one of da, db is nonzero, and which side of the
     * difference it sits on is known exactly. */
    grid = (ea >= eb) ? ea : eb;
    grid -= 3;
    ka_m = ma; kb_m = mb; da = 0; db = 0;
    if (ea - grid >= 0) ka_m = fp_shl(ma, (uint32_t)(ea - grid));
    else                fp_split(ma, (uint32_t)(grid - ea), &ka_m, &da);
    if (eb - grid >= 0) kb_m = fp_shl(mb, (uint32_t)(eb - grid));
    else                fp_split(mb, (uint32_t)(grid - eb), &kb_m, &db);

    if (sa == sb) {
        /* A fractional sum can never reach one ulp of the grid, so the integer
         * part is the whole answer and the fraction only breaks a later tie. */
        return fp_pack(sa, ka_m + kb_m, grid, (da != 0 || db != 0) ? 1 : 0, f);
    }
    if (ka_m > kb_m) return fp_pack(sa, ka_m - kb_m, grid, da ? 1 : (db ? -1 : 0), f);
    if (ka_m < kb_m) return fp_pack(sb, kb_m - ka_m, grid, db ? 1 : (da ? -1 : 0), f);
    if (da != db) {
        /* The integers cancel and the answer is the leftover fraction, with the
         * sign of the operand that still holds it. */
        if (da > db) return fp_pack(sa, da, ea, 0, f);
        return fp_pack(sb, db, eb, 0, f);
    }
    return fp_zero(f, 0u);                    /* exact cancellation, and +0 */
}

static uint64_t fp_mul(uint64_t a, uint64_t b, const fp_fmt *f)
{
    uint32_t sa, sb;
    uint64_t ma, mb;
    int32_t ea, eb;
    const int ka = fp_decode(a, f, &sa, &ma, &ea);
    const int kb = fp_decode(b, f, &sb, &mb, &eb);

    if (ka == FPK_NAN) return fp_quiet(a, f);
    if (kb == FPK_NAN) return fp_quiet(b, f);
    if (ka == FPK_INF || kb == FPK_INF) {
        if ((ka == FPK_INF && kb == FPK_ZERO) || (kb == FPK_INF && ka == FPK_ZERO))
            return f->quiet;                                        /* 0 * inf */
        return fp_inf(f, sa ^ sb);
    }
    if (ma == 0 || mb == 0) return fp_zero(f, sa ^ sb);

    if (f->frac_bits == 23) {
        /* 24 x 24 bits: the whole product fits one 64-bit word, so nothing is
         * dropped and the multiply is exact before the single rounding in pack. */
        return fp_pack(sa ^ sb, ma * mb, ea + eb, 0, f);
    } else {
        /* 53 x 53 needs 106 bits, which no 32-bit-GPR MIPS word holds and no
         * 64 x 64 instruction is available to build: four 32 x 32 products, kept
         * as the high 64 bits plus a sticky bit for the low 64. */
        const uint32_t a1 = (uint32_t)(ma >> 32), a0 = (uint32_t)ma;
        const uint32_t b1 = (uint32_t)(mb >> 32), b0 = (uint32_t)mb;
        const uint64_t P = (uint64_t)a1 * b1;
        const uint64_t M = (uint64_t)a1 * b0 + (uint64_t)a0 * b1;
        const uint64_t L = (uint64_t)a0 * b0;
        const uint64_t S = (uint32_t)M + (L >> 32);
        const uint64_t T = P + (M >> 32) + (S >> 32);
        /* product = T*2^64 + (S&~32)*2^32 + (uint32_t)L, in 106 bits at most. */
        const uint64_t hi64 = T;
        const uint64_t lo64 = ((S & 0xFFFFFFFFULL) << 32) | (uint64_t)(uint32_t)L;
        uint64_t sig, dropped, keep;
        uint32_t shift, sticky;

        if (hi64 == 0) {
            shift = ((uint32_t)fp_msb(lo64) > 54u) ? (uint32_t)fp_msb(lo64) - 54u : 0u;
            fp_split(lo64, shift, &keep, &dropped);
            sig = keep;
        } else {
            /* Keep the 55 leading bits: the exponent of the top one is 64+msb(hi64),
             * so everything above bit (that - 54) is what pack gets to see, and the
             * rest is the sticky bit it rounds on. */
            shift = (uint32_t)fp_msb(hi64) + 10u;      /* 64+msb-54, msb<=51 here */
            sig = fp_shl(hi64, 64u - shift) | (lo64 >> shift);
            dropped = 1u;                              /* placeholder, set below */
            {
                uint64_t low_keep, low_drop;
                fp_split(lo64, shift, &low_keep, &low_drop);
                sig |= low_keep;
                dropped = low_drop;
            }
        }
        sticky = dropped != 0;
        return fp_pack(sa ^ sb, sig, ea + eb + (int32_t)shift, sticky ? 1 : 0, f);
    }
}

/* Shift-and-subtract: one bit of quotient per step, so the remainder always
 * stays below the divisor and no 64-bit division is needed.  frac_bits+2 bits are
 * produced -- kept, guard and round -- which is exactly enough for pack to round
 * to nearest-even with the leftover remainder as the sticky bit. */
static uint64_t fp_div(uint64_t a, uint64_t b, const fp_fmt *f)
{
    uint32_t sa, sb;
    uint64_t ma, mb;
    int32_t ea, eb;
    const int ka = fp_decode(a, f, &sa, &ma, &ea);
    const int kb = fp_decode(b, f, &sb, &mb, &eb);
    uint64_t rem, quot = 0;
    int32_t i, shift;

    if (ka == FPK_NAN) return fp_quiet(a, f);
    if (kb == FPK_NAN) return fp_quiet(b, f);
    if (ka == FPK_INF || kb == FPK_INF) {
        if (ka == kb) return f->quiet;                    /* inf/inf, 0/0 */
        if (ka == FPK_INF) return fp_inf(f, sa ^ sb);
        return fp_zero(f, sa ^ sb);
    }
    if (ma == 0 && mb == 0) return f->quiet;              /* 0/0 */
    if (ma == 0) return fp_zero(f, sa ^ sb);
    if (mb == 0) return fp_inf(f, sa ^ sb);

    /* Both operands can carry a subnormal, so their leading ones can sit 52 bits
     * apart; shifting the lower one up (which is exact) puts them on the same
     * footing and the exponent is corrected by the same amount. */
    {
        const int32_t n = fp_msb(ma) - fp_msb(mb);
        if (n >= 0) mb = fp_shl(mb, (uint32_t)n);
        else        ma = fp_shl(ma, (uint32_t)(-n));
        shift = n;
    }

    rem = ma;
    if (rem >= mb) { rem -= mb; quot = 1; }
    for (i = 0; i < (int32_t)f->frac_bits + 2; i++) {
        uint32_t bit;
        rem <<= 1;                       /* rem < mb <= 2^53, so this never carries */
        bit = (rem >= mb);
        if (bit) rem -= mb;
        quot = (quot << 1) | bit;
    }
    return fp_pack(sa ^ sb, quot, ea - eb + shift - (int32_t)f->frac_bits - 2, rem ? 1 : 0, f);
}

/* Three-way order, with the value that makes every caller read "unordered" as
 * false. */
static int32_t fp_order(uint64_t a, uint64_t b, const fp_fmt *f, int32_t unordered)
{
    const uint32_t sa = (uint32_t)((a >> f->sign_shift) & 1u);
    const uint32_t sb = (uint32_t)((b >> f->sign_shift) & 1u);
    const uint64_t ea = (a >> f->frac_bits) & f->exp_max, ma = a & f->mag_mask;
    const uint64_t eb = (b >> f->frac_bits) & f->exp_max, mb = b & f->mag_mask;
    int32_t mag;

    if ((ea == f->exp_max && ma) || (eb == f->exp_max && mb)) return unordered;

    /* The biased exponent with its mantissa field is monotonic in magnitude,
     * subnormals, zeros and infinities included, so no decode is needed here. */
    if (ea != eb) mag = (ea > eb) ? 1 : -1;
    else if (ma != mb) mag = (ma > mb) ? 1 : -1;
    else mag = 0;

    if (sa != sb) {
        /* Equal magnitude with opposite signs is only "equal" for the two zeros;
         * +1e308 and -1e308 are as far apart as they come. */
        if (ea == 0 && eb == 0 && ma == 0 && mb == 0) return 0;
        return sa ? -1 : 1;
    }
    return sa ? -mag : mag;
}

/* Truncate toward zero, saturating outside the range -- the law the EE's own cvt
 * instructions follow, and the one the two int64 conversions used before. */
static uint64_t fp_trunc(uint64_t bits, const fp_fmt *f, uint32_t *sign, int *cls)
{
    uint64_t sig, whole, dropped;
    int32_t exp;
    const int kind = fp_decode(bits, f, sign, &sig, &exp);

    *cls = kind;
    if (kind != FPK_FIN) return 0;
    if (sig == 0) return 0;
    if (exp >= 0) {
        if ((int32_t)fp_msb(sig) + exp > 63) { *cls = FPK_BIG; return 0; }
        return fp_shl(sig, (uint32_t)exp);
    }
    fp_split(sig, (uint32_t)(-exp), &whole, &dropped);
    return whole;
}

/* ── Single precision, two engines ───────────────────────────────────────── */

/* The bit-level engine, named for what it is now: the reference the hardware path
 * below is checked against, and the fallback it falls back to.  These are not the
 * names clang calls -- the `__*sf*` entry points at the end of this section choose an
 * engine at run time -- so nothing but the probe and the self-test reaches them. */

uint32_t ps2_sf_addsf3(uint32_t a, uint32_t b) { return (uint32_t)fp_addsub(a, b, 0, &FP_F32); }
uint32_t ps2_sf_subsf3(uint32_t a, uint32_t b) { return (uint32_t)fp_addsub(a, b, 1, &FP_F32); }
uint32_t ps2_sf_mulsf3(uint32_t a, uint32_t b) { return (uint32_t)fp_mul(a, b, &FP_F32); }
uint32_t ps2_sf_divsf3(uint32_t a, uint32_t b) { return (uint32_t)fp_div(a, b, &FP_F32); }

int32_t ps2_sf_ltsf2(uint32_t a, uint32_t b)  { return fp_order(a, b, &FP_F32, 1); }
int32_t ps2_sf_lesf2(uint32_t a, uint32_t b)  { return fp_order(a, b, &FP_F32, 1); }
int32_t ps2_sf_gtsf2(uint32_t a, uint32_t b)  { return fp_order(a, b, &FP_F32, -1); }
int32_t ps2_sf_gesf2(uint32_t a, uint32_t b)  { return fp_order(a, b, &FP_F32, -1); }
int32_t ps2_sf_eqsf2(uint32_t a, uint32_t b)  { return fp_order(a, b, &FP_F32, 1); }
int32_t ps2_sf_nesf2(uint32_t a, uint32_t b)  { return fp_order(a, b, &FP_F32, 1); }

uint32_t ps2_sf_floatsisf(int32_t v)
{
    const uint32_t sign = (v < 0) ? 1u : 0u;
    return (uint32_t)fp_pack(sign, (v < 0) ? (uint64_t)-(int64_t)v : (uint64_t)v, 0, 0, &FP_F32);
}

uint32_t ps2_sf_floatunsisf(uint32_t v)
{
    return (uint32_t)fp_pack(0u, (uint64_t)v, 0, 0, &FP_F32);
}

uint32_t ps2_sf_fixsfsi(uint32_t a)
{
    uint32_t sign;
    int cls;
    const uint64_t mag = fp_trunc(a, &FP_F32, &sign, &cls);
    if (cls == FPK_NAN) return 0;
    if (cls == FPK_BIG || cls == FPK_INF || mag > 0x7FFFFFFFu) return sign ? 0x80000000u : 0x7FFFFFFFu;
    return sign ? (uint32_t)0u - (uint32_t)mag : (uint32_t)mag;
}

uint32_t ps2_sf_fixunssfsi(uint32_t a)
{
    uint32_t sign;
    int cls;
    const uint64_t mag = fp_trunc(a, &FP_F32, &sign, &cls);
    if (cls == FPK_NAN) return 0;
    if (sign) return 0;                                   /* negatives clamp, not wrap */
    if (cls == FPK_BIG || cls == FPK_INF || mag > 0xFFFFFFFFu) return 0xFFFFFFFFu;
    return (uint32_t)mag;
}

/* The Emotion Engine does have a floating-point unit, and the BIOS hands it over
 * enabled: CP0.Status bit 29 (CU1) reads 1 at boot, which is the row
 * `[CPU] CP0.Status=0x70030c11 CU3..CU0=0111` prints.  What the EE has not got is a
 * *double*-precision unit -- `.d` is no such instruction, it is an illegal encoding
 * that the BIOS swallows, which is what turned one XMB frame black in 2026-10.  The
 * whole image is therefore -msoft-float (the law is written up at Makefile:85), and
 * that flag also takes the FPU away from single precision: every float add, multiply
 * and compare in TinyGL and every transcendental in gl/math.h has been running
 * through the engine above.  It is why one XMB frame costs 23.9 s and its icon bake
 * 14.4 s, measured by scripts/ps2_smooth.sh on 2026-10-10.
 *
 * The block below is the way back to the FPU without touching the ABI.  -msoft-float
 * is an *argument passing* law -- a float crosses a boundary in an integer register --
 * and this is the one file in the image allowed to compile with the FPU switched on,
 * because nothing here crosses a boundary as a float: every entry point takes and
 * returns uint32_t/int32_t holding IEEE-754 bit patterns, and the only float-shaped
 * things in the file are the COP1 instructions inside `__asm__` strings.  The link
 * therefore still reports `FP ABI: Soft float` for the whole image, and callers cannot
 * tell the difference.  Nothing here may mention a double: the double entry points
 * stay in the engine above, on purpose.
 *
 * Which engine runs is decided at run time, not at link time: btron_fp_init() below
 * proves the FPU answers before any entry point uses it, so a machine whose BIOS left
 * CU1 clear -- or a `.s` op this port gets wrong -- costs time, not the screen.
 *
 * Every op is the same shape: mtc1 the operands in, two nops (the EE requires two
 * instructions between an mtc1 and the COP1 op that reads that register), the op, two
 * nops, mfc1 the result out, two nops (that same GPR is then read by the caller).
 *
 * The result comes back through `mfc1`, and that verb is the whole story: `cfc1` is a
 * different COP1 move (CO=0x02 where mfc1 is CO=0x00 -- the encodings are one bit apart)
 * and its number field names a *control* register, so `cfc1 rt, $4` asks the FPU for
 * control register 4, which the Emotion Engine does not have.  That is what the first
 * version of this block did, and the proof read back 0x00002e30 for 1.0f + 2.0f -- a
 * plausible-looking integer from an undefined read, which made btron_fp_init() declare
 * the FPU dead and leave the whole image on the bit engine.  Control moves are only for
 * the two registers that are actually control registers, FCSR read and write at $31. */

#if defined(BTRON_PS2_FP_HW)

int btron_fp_on = 0;                       /* 0 = bit engine, 1 = COP1 */

#define FP_IN  "mtc1 %1, $f0\n\tmtc1 %2, $f2\n\tnop\n\tnop\n\t"
#define FP_OUT "nop\n\tnop\n\tmfc1 %0, $f4\n\tnop\n\tnop"

#define FP_BIN(NAME, OP)                                                     \
static uint32_t NAME(uint32_t a, uint32_t b)                                 \
{                                                                            \
    uint32_t r;                                                              \
    __asm__ (FP_IN OP " $f4, $f0, $f2\n\t" FP_OUT                           \
             : "=r" (r) : "r" (a), "r" (b));                                \
    return r;                                                                \
}

FP_BIN(fp_hw_add, "add.s")
FP_BIN(fp_hw_sub, "sub.s")
FP_BIN(fp_hw_mul, "mul.s")
FP_BIN(fp_hw_div, "div.s")

/* The EE's sqrt.s is a single instruction here, against six Babylonian steps and a
 * soft double division each in gl/math.h's series branch. */
static uint32_t fp_hw_sqrt(uint32_t a)
{
    uint32_t r;
    __asm__ ("mtc1 %1, $f0\n\tnop\n\tnop\n\t"
             "sqrt.s $f4, $f0\n\t" FP_OUT
             : "=r" (r) : "r" (a));
    return r;
}

/* int -> float through the FPU: cvt.s.w is the whole job.  The other direction is
 * NOT here, and that is a fact about the EE rather than a choice: the self-test made
 * PCSX2 log `Unrecognized FPU/COP1 op 4600010d`, which is the word LLVM writes for
 * `trunc.w.s $f4, $f0`, so the Emotion Engine has no float-to-integer conversion in
 * single-precision form.  __fixsfsi and __fixunssfsi therefore stay on the bit engine,
 * which is exact, and the row that proves the two are not asked of the FPU is in
 * btron_fp_selftest(). */
static uint32_t fp_hw_cvt_s_w(int32_t v)
{
    uint32_t r;
    __asm__ ("mtc1 %1, $f0\n\tnop\n\tnop\n\t"
             "cvt.s.w $f4, $f0\n\t" FP_OUT
             : "=r" (r) : "r" ((uint32_t)v));
    return r;
}

/* fp_hw_cvt_w_s used to live here: `trunc.w.s $f4, $f0`, which the Emotion Engine
 * does not have.  The self-test made PCSX2 log `Unrecognized FPU/COP1 op 4600010d`,
 * the word LLVM writes for it at -march=mips2, and an illegal op here did not merely
 * cost time -- it left $f4 holding the previous result, so __fixsfsi answered with a
 * stale number.  float -> int therefore stays on the bit engine. */

/* Three-way order, -1/0/1.  Two of these exist because the callers' tests want
 * opposite answers for an unordered pair: `nan_gt` reads a NaN as greater (what lt
 * and le need, so `x < 0` and `x < 1` both come out false) and `nan_lt` reads it as
 * less (what gt and ge need, so `x > 0` and `x > -1` both come out false).
 *
 * Built from the two ordered forms only, and that is not a style choice: the Emotion
 * Engine has no `c.un.s`.  The self-test proved it -- PCSX2's EE decoder logged
 * `Unrecognized FPU/COP1 op 46020031`, which is exactly the word LLVM writes for
 * `c.un.s $f0, $f2` at -march=mips2, and the illegal compare left the condition code
 * holding whatever the previous compare set, so the branch took the wrong way and 1910
 * of 4570 assertions disagreed with the bit engine.
 *
 * The unordered pair does not need its own test, because of how it falls out of the
 * ordered ones: neither `a < b` nor `a <= b` is true for a NaN, so a pair that fails
 * both is either greater-than or unordered, and both of those are the answer the
 * caller's test wants for a NaN anyway (nan_gt calls it greater, nan_lt calls it
 * less).  So the whole job is: is LO < HI, is LO <= HI, and otherwise MORE.
 * The delay slot after every branch is a nop, since the assembler does not fill it for
 * inline asm, and one nop sits between each compare and its branch because the EE's
 * condition code is written one cycle after the compare retires. */
#define FP_CMP3(NAME, LO, HI, LESS, MORE)                                          \
static int32_t NAME(uint32_t a, uint32_t b)                                         \
{                                                                                   \
    int32_t r = (MORE);                                                             \
    __asm__ (FP_IN                                                                  \
             "c.olt.s " LO ", " HI "\n\t"                                           \
             "nop\n\t"                                                              \
             "bc1t 2f\n\t"                                                          \
             "nop\n\t"                                                              \
             "c.ole.s " LO ", " HI "\n\t"                                           \
             "nop\n\t"                                                              \
             "bc1t 1f\n\t"                                                          \
             "nop\n\t"                                                              \
             "j 3f\n\t"                                                             \
             "nop\n\t"                                                              \
             "1:\n\tli %0, 0\n\tj 3f\n\tnop\n\t"                                    \
             "2:\n\tli %0, " LESS "\n\t"                                            \
             "3:\n\t"                                                               \
             : "+r" (r) : "r" (a), "r" (b));                                        \
    return r;                                                                       \
}

FP_CMP3(fp_hw_cmp_nan_gt, "$f0", "$f2", "-1", 1)
FP_CMP3(fp_hw_cmp_nan_lt, "$f2", "$f0", "1", -1)

/* Equal-or-not only: 0 when the pair is ordered-equal, 1 otherwise, NaN included --
 * which is exactly what eqsf2 (callers test ==0) and nesf2 (!=0) both need, and what
 * fp_order() returns for the two reference entry points they are checked against.
 * Ordered-equal is `a <= b` and `b <= a`, both with the same ordered form the compare
 * above uses; -0.0 and +0.0 pass both, so they read equal, which is what the bit
 * engine says and what the rasterizer's `if (x == 0.0f)` needs. */
static int32_t fp_hw_cmp_eq(uint32_t a, uint32_t b)
{
    int32_t r = 1;
    __asm__ (FP_IN
             "c.ole.s $f0, $f2\n\t"
             "nop\n\t"
             "bc1f 1f\n\t"
             "nop\n\t"
             "c.ole.s $f2, $f0\n\t"
             "nop\n\t"
             "bc1f 1f\n\t"
             "nop\n\t"
             "li %0, 0\n\t"
             "1:\n\t"
             : "+r" (r) : "r" (a), "r" (b));
    return r;
}

static uint32_t fp_hw_count(void)
{
    uint32_t v;
    __asm__ volatile ("mfc0 %0, $9, 0" : "=r" (v));
    return v;
}

uint32_t btron_fp_probe(uint32_t *fcsr)
{
    uint32_t st;
    __asm__ volatile ("mfc0 %0, $12, 0" : "=r" (st));
    if (fcsr) {
        uint32_t f;
        __asm__ ("cfc1 %0, $31\n\tnop\n\tnop" : "=r" (f));
        *fcsr = f;
    }
    return st;
}

/* Enable CU1 if the BIOS did not, clear the FPU's exception enables and its stale
 * flags (round-nearest is the field's reset value and is what fp_addsub() rounds
 * to, so both engines then agree), and prove the unit answers.  Called once at the
 * top of ps2_kernel_main(), before anything times or rasterises. */
/* The round trip.  1.0f + 2.0f is 0x40400000 in either engine -- and that is the only
 * number this proof may be checked against: an earlier version compared against
 * 0x40000000, which is 2.0f, so a perfectly answering FPU failed the test, the switch
 * stayed at 0, and a whole measurement run looked like a change that did nothing.
 * btron_fp_seen keeps the bits that were actually produced, so the row that says "COP1
 * did not answer" can be checked against the hardware rather than argued with. */
uint32_t btron_fp_seen;                      /* what the proof's fp_hw_add returned */

int btron_fp_init(void)
{
    uint32_t st = btron_fp_probe(0);
    if ((st & 0x10000000u) == 0) {
        st |= 0x20000000u;                          /* CU1 */
        __asm__ volatile ("mtc0 %0, $12, 0\n\tnop\n\tnop" : : "r" (st));
    }
    {
        const uint32_t fcsr_clear = 0u;             /* RN, every exception masked */
        __asm__ volatile ("ctc1 %0, $31\n\tnop\n\tnop" : : "r" (fcsr_clear));
    }
    btron_fp_seen = fp_hw_add(0x3F800000u, 0x40000000u);
    btron_fp_on = (btron_fp_seen == 0x40400000u) ? 1 : 0;
    return btron_fp_on;
}

/* A fixed vector of bit patterns rather than float literals, so the inputs are the
 * same bytes in both engines and no host compiler decides how 0.1f rounds.  Covers
 * RN's tie (1.0 + 1.5 ulp), subnormals in and out, overflow, divide-by-zero, the two
 * infinities (whose difference must read unordered) and a quiet NaN. */
static const uint32_t s_fp_vec[] = {
    0x3F800000u, 0x40000000u, 0x3FC00000u, 0x3F000000u, 0xBF800000u,
    0x00000000u, 0x80000000u, 0x00400000u, 0x00000001u, 0x007FFFFFu,
    0x7F800000u, 0xFF800000u, 0x7FC00000u, 0x42C80000u, 0x3DCCCCCDu,
    0x3E4CCCCDu, 0x47C35000u, 0x497423F6u, 0x3312D8E0u, 0xC1200000u
};
#define FP_VEC_N ((uint32_t)(sizeof s_fp_vec / sizeof s_fp_vec[0]))

/* Distance in representable steps, for a row a human can judge; both engines are
 * exact IEEE so any non-zero count here is a bug in one of them, not rounding. */
static uint32_t fp_ulp_diff(uint32_t a, uint32_t b)
{
    if (a == b) return 0;
    /* Two pairs read as opposite signs here and are not disagreements: the two zeros
     * are one value, and a NaN is a NaN whatever payload each engine chose to quiet it
     * with.  Reporting either as a gap would make the row lie about the hardware. */
    if ((a & 0x7FFFFFFFu) == 0u && (b & 0x7FFFFFFFu) == 0u) return 0;
    if ((((a >> 23) & 0xFFu) == 0xFFu) && (a & 0x7FFFFFu) &&
        (((b >> 23) & 0xFFu) == 0xFFu) && (b & 0x7FFFFFu)) return 0;
    if ((a >> 31) != (b >> 31)) return 0x7FFFFFFFu;         /* opposite signs */
    return (a > b) ? (a - b) : (b - a);
}

/* Which of the nine op classes disagreed, and whether with clean or with special
 * arguments.  A pair is `clean` when neither operand is zero, subnormal, infinite or
 * NaN, because those two halves answer different questions: disagreement only in the
 * special column is the FPU's non-arithmetic law (NaN payload, the flag bits, a
 * trapped divide) and the rasterizer is unaffected, while disagreement in the clean
 * column is the arithmetic or the mtc1 sequence itself.  The self-test without this
 * table said `disagree=1910` and left that question open.
 *
 * [class][0]=clean total, [1]=clean bad, [2]=special total, [3]=special bad.  The
 * classes and their names are declared in ps2_fp.h; these are the short spellings the
 * loop below uses. */
#define FP_CLS_ADD    BTRON_FP_CLS_ADD
#define FP_CLS_SUB    BTRON_FP_CLS_SUB
#define FP_CLS_MUL    BTRON_FP_CLS_MUL
#define FP_CLS_DIV    BTRON_FP_CLS_DIV
#define FP_CLS_LTLT   BTRON_FP_CLS_LTLT
#define FP_CLS_GTGTE  BTRON_FP_CLS_GTGTE
#define FP_CLS_EQNE   BTRON_FP_CLS_EQNE
#define FP_CLS_CVT    BTRON_FP_CLS_CVT
#define FP_CLS_SQRT   BTRON_FP_CLS_SQRT
#define FP_CLS_N      BTRON_FP_CLS_N

/* Every disagreement is sorted by what caused it, because the causes documented in
 * ps2_fp.h (daz, ftz, nonfinite, inexact) are properties of a single-precision unit
 * that flushes, while a fifth -- other -- is a bug.  The short spellings the loop
 * below uses; the meanings and the gate live in the header. */
#define FP_SHAPE_DAZ       BTRON_FP_SHAPE_DAZ
#define FP_SHAPE_FTZ       BTRON_FP_SHAPE_FTZ
#define FP_SHAPE_NONFINITE BTRON_FP_SHAPE_NONFINITE
#define FP_SHAPE_INEXACT   BTRON_FP_SHAPE_INEXACT
#define FP_SHAPE_OTHER     BTRON_FP_SHAPE_OTHER
#define FP_SHAPE_N         BTRON_FP_SHAPE_N

/* The three questions about a bit pattern that the classifier asks, each one an
 * exponent-field test: the values are bytes here, and a check that itself compared as
 * floating point would be asking the engine under test whether it works. */
static int fp_f_nonfinite(uint32_t v) { return ((v >> 23) & 0xFFu) == 0xFFu; }
static int fp_f_subnormal(uint32_t v)
{ return ((v >> 23) & 0xFFu) == 0u && (v & 0x7FFFFFu) != 0u; }
/* At or below the underflow boundary: zero or subnormal. */
static int fp_f_flushed(uint32_t v) { return ((v >> 23) & 0xFFu) == 0u; }
/* Within one exponent of it, which is where an abrupt-versus-gradual underflow shows. */
static int fp_f_barely_normal(uint32_t v) { return ((v >> 23) & 0xFFu) == 1u; }

/* Which cause explains one disagreement.  `numeric` says the two engines returned
 * floats; a compare returns an integer code instead, so only its operands can explain
 * anything.  The order is the argument: an infinite or NaN in the pair puts the whole
 * assertion under the unit's non-arithmetic law, and that has to be said before a
 * subnormal operand can be blamed for the same row. */
static int fp_cause(uint32_t a, uint32_t b, uint32_t hw, uint32_t soft, int numeric)
{
    if (fp_f_nonfinite(a) || fp_f_nonfinite(b) ||
        (numeric && (fp_f_nonfinite(hw) || fp_f_nonfinite(soft))))
        return FP_SHAPE_NONFINITE;
    if (fp_f_subnormal(a) || fp_f_subnormal(b))
        return FP_SHAPE_DAZ;
    if (numeric && (fp_f_flushed(hw) || fp_f_flushed(soft)) &&
        (fp_f_flushed(hw) || fp_f_barely_normal(hw)) &&
        (fp_f_flushed(soft) || fp_f_barely_normal(soft)))
        return FP_SHAPE_FTZ;
    return FP_SHAPE_OTHER;
}

/* Zero, subnormal, infinite and NaN operands all land in the special column. */
static int fp_clean_pair(uint32_t a, uint32_t b)
{
    const uint32_t ea = (a >> 23) & 0xFFu, eb = (b >> 23) & 0xFFu;
    return (ea != 0u && ea != 0xFFu && eb != 0u && eb != 0xFFu);
}

/* How far apart two ordinary-operand results are.  This is the question the cause table
 * leaves open once `other` is non-zero: one step apart is a rounding decision made the
 * other way (a mode, which a rasterizer survives -- a pixel colour is 8 bits), while a
 * wide or opposite-sign gap is a different number entirely (a sequence, which it does
 * not).  fp_ulp_diff() returns its sentinel for the sign case, so that is the top bucket.
 *
 * The two direction buckets are what tell a mode from a bug, because the four modes do
 * not all move the same way: a unit that truncates answers one step small on every
 * inexact pair (toward0), one that ignores the sticky bit answers small or large
 * depending on the neighbouring mantissa (both counts non-zero), and one that is simply
 * wrong answers in neither pattern. */
static int fp_gap_bucket(uint32_t d)
{
    if (d == 1u) return 0;
    if (d == 2u) return 1;
    return (d >= 0x7FFFFFFFu) ? 3 : 2;
}

static int fp_gap_dir(uint32_t hw, uint32_t sf)
{
    return ((hw & 0x7FFFFFFFu) < (sf & 0x7FFFFFFFu)) ? 4 : 5;
}

/* The pairs themselves, for the first few unexplained gaps: [row][0] is the op class,
 * [1] and [2] the operands as the engine saw them (bit patterns for a float op, plain
 * integers for cvt.s.w), [3] the COP1 answer, [4] the bit engine's, [5] the gap in steps.
 * A count says how often the two engines part; only the bytes say which of them is right,
 * and that is decided off the device, on a host that can do the exact arithmetic -- see
 * the note above the cause table about the bit engine's own proof. */
#define FP_DUMP_N BTRON_FP_DUMP_N

/* One disagreement, one cause.  The `other` bucket is the one a reader has to act on,
 * so it keeps the name of the op that first filled it and the bytes of the first few
 * pairs: `other=3` says an engine is wrong, `other=3 first=mul` says which, and the pair
 * rows say what to do about it -- the exact answer is computable off the device. */
static void fp_shape_add(int cause, uint32_t idx, uint32_t a, uint32_t b,
                         uint32_t hw, uint32_t sf, uint32_t gap)
{
    btron_fp_shape[cause]++;
    if (cause != FP_SHAPE_OTHER) return;
    if (btron_fp_other_where == 0) btron_fp_other_where = btron_fp_cls_name[idx];
    if (btron_fp_dump_n < FP_DUMP_N) {
        btron_fp_dump[btron_fp_dump_n][0] = idx;
        btron_fp_dump[btron_fp_dump_n][1] = a;
        btron_fp_dump[btron_fp_dump_n][2] = b;
        btron_fp_dump[btron_fp_dump_n][3] = hw;
        btron_fp_dump[btron_fp_dump_n][4] = sf;
        btron_fp_dump[btron_fp_dump_n][5] = gap;
        btron_fp_dump_n++;
    }
}

/* ops/hw/soft out: how many comparisons were made, how many disagreed, and the CPU
 * ticks each engine took for the same 4,000-op chain.  Returns the largest ulp gap
 * seen.  The two timing runs are separate loops so neither engine's result feeds the
 * other's pipeline, and the chain is add/mul/div on purpose: those are the three the
 * rasterizer spends its frame on.
 *
 * It runs whether or not btron_fp_on says the FPU is live: the comparisons are the
 * hardware path's own evidence, and a proof that failed for the wrong reason -- as the
 * one above did, comparing against 2.0f instead of 3.0f -- is exactly what this table
 * is for.  A guest that has no FPU at all answers with its own trap handler here
 * rather than in the middle of a frame. */
uint32_t btron_fp_selftest(uint32_t *ops, uint32_t *bad, uint32_t *hw_ticks, uint32_t *soft_ticks)
{
    uint32_t worst = 0, n = 0, mismatches = 0, i, k, c;
    for (c = 0u; c < FP_CLS_N; c++)
        for (i = 0u; i < 4u; i++) btron_fp_cls[c][i] = 0u;
    for (i = 0u; i < FP_SHAPE_N; i++) btron_fp_shape[i] = 0u;
    for (i = 0u; i < BTRON_FP_GAP_N; i++) btron_fp_gap[i] = 0u;
    btron_fp_other_where = 0;
    btron_fp_dump_n = 0;
    /* One assertion, one count, at the place it is made.  The first version added 1 to
     * the denominator per pair but counted every comparison in the numerator, and
     * printed `checked=970 disagree=2930` -- a row that looks like a broken engine and
     * is only a broken denominator. */
#define FP_TALLY_B(IDX, CLEAN, IS_BAD, CAUSE, A_, B_, HW_, SF_, GAP_) do {     \
        const int s_ = (CLEAN) ? 0 : 2;                                        \
        btron_fp_cls[(IDX)][s_]++;                                             \
        n++;                                                                   \
        if (IS_BAD) { btron_fp_cls[(IDX)][s_ + 1]++; mismatches++;              \
                      if (worst < 1u) worst = 1u;                              \
                      fp_shape_add((CAUSE), (IDX), (A_), (B_), (HW_), (SF_),   \
                                   (GAP_)); }                                  \
    } while (0)
    /* An op whose two engines return floats: the results can explain the disagreement,
     * so it goes through fp_cause() with numeric set, and the size of the gap says how
     * far off it is and in which direction. */
#define FP_GAP(IDX, HW, SF)  do {                                              \
        const uint32_t h_ = (HW), f_ = (SF);                                   \
        const uint32_t d_ = fp_ulp_diff(h_, f_);                               \
        const int cl_ = fp_clean_pair(a, b);                                   \
        FP_TALLY_B(IDX, cl_, d_ != 0u, fp_cause(a, b, h_, f_, 1),               \
                   a, b, h_, f_, d_);                                          \
        if (d_ != 0u && cl_) { btron_fp_gap[fp_gap_bucket(d_)]++;               \
                               if (d_ == 1u) btron_fp_gap[fp_gap_dir(h_, f_)]++; } \
        if (d_ > worst) worst = d_;                                            \
    } while (0)
    /* An op that returns an integer code (compare), so only the operands say anything
     * about why the two disagree, and there is no gap to measure. */
#define FP_SAME(IDX, TEST)   FP_TALLY_B(IDX, fp_clean_pair(a, b), (TEST),       \
                                        fp_cause(a, b, 0u, 0u, 0),              \
                                        a, b, 0u, 0u, 0u)

    for (i = 0; i < FP_VEC_N; i++) {
        for (k = 0; k < FP_VEC_N; k++) {
            const uint32_t a = s_fp_vec[i], b = s_fp_vec[k];
            FP_GAP(FP_CLS_ADD, fp_hw_add(a, b), ps2_sf_addsf3(a, b));
            FP_GAP(FP_CLS_SUB, fp_hw_sub(a, b), ps2_sf_subsf3(a, b));
            FP_GAP(FP_CLS_MUL, fp_hw_mul(a, b), ps2_sf_mulsf3(a, b));
            FP_GAP(FP_CLS_DIV, fp_hw_div(a, b), ps2_sf_divsf3(a, b));
            /* Each predicate as its caller asks it, against the hardware function
             * that is wired to it at the bottom of this file. */
            FP_SAME(FP_CLS_LTLT, (fp_hw_cmp_nan_gt(a, b) < 0) != (ps2_sf_ltsf2(a, b) < 0));
            FP_SAME(FP_CLS_LTLT, (fp_hw_cmp_nan_gt(a, b) < 1) != (ps2_sf_lesf2(a, b) < 1));
            FP_SAME(FP_CLS_GTGTE, (fp_hw_cmp_nan_lt(a, b) > 0) != (ps2_sf_gtsf2(a, b) > 0));
            FP_SAME(FP_CLS_GTGTE, (fp_hw_cmp_nan_lt(a, b) > -1) != (ps2_sf_gesf2(a, b) > -1));
            FP_SAME(FP_CLS_EQNE, (fp_hw_cmp_eq(a, b) == 0) != (ps2_sf_eqsf2(a, b) == 0));
            FP_SAME(FP_CLS_EQNE, (fp_hw_cmp_eq(a, b) != 0) != (ps2_sf_nesf2(a, b) != 0));
        }
    }

    /* cvt.s.w is the one conversion the EE performs, so it is the one asked of it
     * (int -> float; the other direction is not in its instruction set, which is why
     * the row above about trunc.w.s is gone).  The clean column is the range in which
     * every integer has a float to itself, |v| < 2^23, and the special column is the
     * rounding zone above it, where the two engines have to agree on how the tie fell.
     * The bit patterns are read as integers here rather than as floats, so the split
     * is stated on the value rather than inherited from fp_clean_pair() -- and so is
     * the cause, which for this class can only be `the integer did not fit in a
     * mantissa` (inexact) or a bug, because no float shape describes an int. */
    for (i = 0; i < FP_VEC_N; i++) {
        const int32_t v = (int32_t)s_fp_vec[i];
        const uint32_t av = (v < 0) ? (0u - (uint32_t)v) : (uint32_t)v;
        const int exact_ = av < 0x00800000u;
        const uint32_t h_ = fp_hw_cvt_s_w(v), f_ = ps2_sf_floatsisf(v);
        FP_TALLY_B(FP_CLS_CVT, exact_, h_ != f_,
                   exact_ ? FP_SHAPE_OTHER : FP_SHAPE_INEXACT,
                   (uint32_t)v, (uint32_t)v, h_, f_, fp_ulp_diff(h_, f_));
    }

    /* sqrt has no counterpart in the vector loop above (the bit engine has no sqrt),
     * so it is checked the other way round: the hardware answer, squared back, must
     * be the input.  Only the normals are asked, because for a subnormal the square
     * of the correctly rounded root cannot land within a few steps of the argument. */
    for (i = 0; i < FP_VEC_N; i++) {
        const uint32_t a = s_fp_vec[i];
        const uint32_t b = a;                 /* FP_SAME splits on a pair; this is one value */
        const uint32_t e = (a >> 23) & 0xFFu;
        uint32_t s, back, d;
        if (e == 0u || e == 0xFFu || (a >> 31)) continue;
        s = fp_hw_sqrt(a);
        back = fp_hw_mul(s, s);
        d = fp_ulp_diff(back, a);
        if (d > 3u && worst < 3u) worst = 3u;
        FP_SAME(FP_CLS_SQRT, d > 3u);
    }

    if (ops)        *ops        = n;
    if (bad)        *bad        = mismatches;
    if (hw_ticks || soft_ticks) {
        int rep;
        for (rep = 0; rep < 2; rep++) {          /* 0 = hardware, 1 = bit engine */
            uint32_t acc = 0x3F800000u, c0, c1t;
            int j;
            /* add, mul, div and a compare that steers the value: the four the
             * rasterizer spends a frame on, and the compare has to be consumed or
             * the whole chain is dead code the compiler could remove. */
            c0 = fp_hw_count();
            for (j = 0; j < 4000; j++) {
                acc = rep ? ps2_sf_addsf3(acc, 0x3F000000u) : fp_hw_add(acc, 0x3F000000u);
                acc = rep ? ps2_sf_mulsf3(acc, 0x3F800000u) : fp_hw_mul(acc, 0x3F800000u);
                acc = rep ? ps2_sf_divsf3(acc, 0x3F000000u) : fp_hw_div(acc, 0x3F000000u);
                acc = rep
                    ? ((ps2_sf_ltsf2(acc, 0x40000000u) < 0)
                        ? ps2_sf_addsf3(acc, 0x3D4CCCCDu) : ps2_sf_mulsf3(acc, 0x3FC00000u))
                    : ((fp_hw_cmp_nan_gt(acc, 0x40000000u) < 0)
                        ? fp_hw_add(acc, 0x3D4CCCCDu) : fp_hw_mul(acc, 0x3FC00000u));
            }
            c1t = fp_hw_count() - c0;
            if (rep == 0) { if (hw_ticks) *hw_ticks = c1t; }
            else          { if (soft_ticks) *soft_ticks = c1t; }
            if (acc == 0xDEADBEEFu) acc = 0x3F800000u;   /* keep the chain live */
        }
    }
    return worst;
}

#else  /* !BTRON_PS2_FP_HW: the host test and any target without the COP1 path */

int btron_fp_on = 0;
uint32_t btron_fp_seen;

uint32_t btron_fp_probe(uint32_t *fcsr) { if (fcsr) *fcsr = 0u; return 0u; }
int btron_fp_init(void) { return 0; }
uint32_t btron_fp_selftest(uint32_t *ops, uint32_t *bad, uint32_t *hw, uint32_t *soft)
{
    if (ops) *ops = 0u;
    if (bad) *bad = 0u;
    if (hw) *hw = 0u;
    if (soft) *soft = 0u;
    return 0u;
}

#endif /* BTRON_PS2_FP_HW */

/* The per-class table and its names, defined once for both builds so the row order
 * cannot disagree with the header that promises it.  With no COP1 path the self-test
 * is a stub that touches nothing, and the boot row reads "all classes agree" because
 * every count is zero -- which is true: nothing was rejected, nothing ran. */
uint32_t btron_fp_cls[BTRON_FP_CLS_N][4];
const char *const btron_fp_cls_name[BTRON_FP_CLS_N] = {
    "add", "sub", "mul", "div", "lt/le", "gt/ge", "eq/ne", "cvt.s.w", "sqrt"
};

/* The same for the causes: defined once, zero in a build that has no COP1 path, where
 * the row reads `other=0` and means it -- no assertion was made, so none was left
 * unexplained.  Which is also why the gate has to read the class table as well as this
 * one: a run that never reached the self-test would score perfectly on cause alone. */
uint32_t btron_fp_shape[BTRON_FP_SHAPE_N];
const char *const btron_fp_shape_name[BTRON_FP_SHAPE_N] = {
    "daz", "ftz", "nonfinite", "inexact", "other"
};
const char *btron_fp_other_where;

/* The size of the clean-operand gaps, for the four arithmetic classes: see
 * fp_gap_bucket().  Counted separately from the cause table because it answers the
 * follow-up question -- `how wrong, and in which direction` -- with the pairs the cause
 * table could not explain. */
uint32_t btron_fp_gap[BTRON_FP_GAP_N];
const char *const btron_fp_gap_name[BTRON_FP_GAP_N] = {
    "1ulp", "2ulp", "wide", "sign", "small", "large"
};
uint32_t btron_fp_dump[BTRON_FP_DUMP_N][6];
uint32_t btron_fp_dump_n;   /* how many of those rows hold a pair */

/* ── The entry points clang calls, single precision ──────────────────────── */

/* FP_PICK is what lets the same definitions serve both builds: with the COP1 path
 * compiled in, the run-time flag chooses; without it -- the host test, and any build
 * made with PS2_FP=soft -- the second argument is the whole body and the first is
 * never named, so the FPU's absence cannot break the compile. */
#if defined(BTRON_PS2_FP_HW)
#define FP_PICK(HW, SF, x, y) (btron_fp_on ? (HW)((x), (y)) : (SF)((x), (y)))
#define FP_PICK1(HW, SF, x)   (btron_fp_on ? (HW)(x) : (SF)(x))
#else
#define FP_PICK(HW, SF, x, y) (SF)((x), (y))
#define FP_PICK1(HW, SF, x)   (SF)(x)
#endif

uint32_t __addsf3(uint32_t a, uint32_t b) { return FP_PICK(fp_hw_add, ps2_sf_addsf3, a, b); }
uint32_t __subsf3(uint32_t a, uint32_t b) { return FP_PICK(fp_hw_sub, ps2_sf_subsf3, a, b); }
uint32_t __mulsf3(uint32_t a, uint32_t b) { return FP_PICK(fp_hw_mul, ps2_sf_mulsf3, a, b); }
uint32_t __divsf3(uint32_t a, uint32_t b) { return FP_PICK(fp_hw_div, ps2_sf_divsf3, a, b); }

/* Each predicate goes to the compare whose NaN reads the way its caller's test needs:
 * lt and le are tested `< 0` and `< 1`, so unordered must not be less -- nan_gt;
 * gt and ge are tested `> 0` and `> -1`, so unordered must not be greater -- nan_lt.
 * eq and ne share one function because both are asked whether the pair is equal. */
int32_t __ltsf2(uint32_t a, uint32_t b)  { return FP_PICK(fp_hw_cmp_nan_gt, ps2_sf_ltsf2, a, b); }
int32_t __lesf2(uint32_t a, uint32_t b)  { return FP_PICK(fp_hw_cmp_nan_gt, ps2_sf_lesf2, a, b); }
int32_t __gtsf2(uint32_t a, uint32_t b)  { return FP_PICK(fp_hw_cmp_nan_lt, ps2_sf_gtsf2, a, b); }
int32_t __gesf2(uint32_t a, uint32_t b)  { return FP_PICK(fp_hw_cmp_nan_lt, ps2_sf_gesf2, a, b); }
int32_t __eqsf2(uint32_t a, uint32_t b)  { return FP_PICK(fp_hw_cmp_eq, ps2_sf_eqsf2, a, b); }
int32_t __nesf2(uint32_t a, uint32_t b)  { return FP_PICK(fp_hw_cmp_eq, ps2_sf_nesf2, a, b); }

uint32_t __floatsisf(int32_t v)   { return FP_PICK1(fp_hw_cvt_s_w, ps2_sf_floatsisf, (uint32_t)v); }
uint32_t __floatunsisf(uint32_t v){ return ps2_sf_floatunsisf(v); }   /* cvt.s.w is signed: keep the engine */

/* float -> int stays on the bit engine in both builds: the EE has no trunc.w.s, so
 * there is no hardware answer to compare against here (see the note above where
 * fp_hw_cvt_w_s used to be).  ps2_sf_fixsfsi's own answers for the values a hardware
 * conversion would have to saturate -- 0 for a NaN, the clamp for an out-of-range
 * exponent -- are the ones the callers get, and they are the documented ones. */
uint32_t __fixsfsi(uint32_t a)    { return ps2_sf_fixsfsi(a); }
uint32_t __fixunssfsi(uint32_t a) { return ps2_sf_fixunssfsi(a); }

/* ── Double precision ────────────────────────────────────────────────────── */

uint64_t __adddf3(uint64_t a, uint64_t b) { return fp_addsub(a, b, 0, &FP_F64); }
uint64_t __subdf3(uint64_t a, uint64_t b) { return fp_addsub(a, b, 1, &FP_F64); }
uint64_t __muldf3(uint64_t a, uint64_t b) { return fp_mul(a, b, &FP_F64); }
uint64_t __divdf3(uint64_t a, uint64_t b) { return fp_div(a, b, &FP_F64); }

int32_t __ltdf2(uint64_t a, uint64_t b)  { return fp_order(a, b, &FP_F64, 1); }
int32_t __ledf2(uint64_t a, uint64_t b)  { return fp_order(a, b, &FP_F64, 1); }
int32_t __gtdf2(uint64_t a, uint64_t b)  { return fp_order(a, b, &FP_F64, -1); }
int32_t __gedf2(uint64_t a, uint64_t b)  { return fp_order(a, b, &FP_F64, -1); }
int32_t __eqdf2(uint64_t a, uint64_t b)  { return fp_order(a, b, &FP_F64, 1); }
int32_t __nedf2(uint64_t a, uint64_t b)  { return fp_order(a, b, &FP_F64, 1); }
int32_t __unorddf2(uint64_t a, uint64_t b)
{
    return (fp_is_nan(a, &FP_F64) || fp_is_nan(b, &FP_F64)) ? 1 : 0;
}

uint32_t __truncdfsf2(uint64_t a)
{
    uint32_t sign;
    uint64_t sig;
    int32_t exp;
    const int kind = fp_decode(a, &FP_F64, &sign, &sig, &exp);
    if (kind == FPK_NAN) return (uint32_t)fp_nan_convert(a, &FP_F64, &FP_F32);
    if (kind == FPK_INF) return (uint32_t)fp_inf(&FP_F32, sign);
    return (uint32_t)fp_pack(sign, sig, exp, 0, &FP_F32);
}

uint64_t __extendsfdf2(uint32_t a)
{
    uint32_t sign;
    uint64_t sig;
    int32_t exp;
    const int kind = fp_decode(a, &FP_F32, &sign, &sig, &exp);
    if (kind == FPK_NAN) return fp_nan_convert(a, &FP_F32, &FP_F64);
    if (kind == FPK_INF) return fp_inf(&FP_F64, sign);
    return fp_pack(sign, sig, exp, 0, &FP_F64);           /* exact: 52 bits above 23 */
}

uint64_t __floatsidf(int32_t v)
{
    const uint32_t sign = (v < 0) ? 1u : 0u;
    return fp_pack(sign, (v < 0) ? (uint64_t)-(int64_t)v : (uint64_t)v, 0, 0, &FP_F64);
}

uint64_t __floatunsidf(uint32_t v)
{
    return fp_pack(0u, (uint64_t)v, 0, 0, &FP_F64);
}

int32_t __fixdfsi(uint64_t a)
{
    uint32_t sign;
    int cls;
    const uint64_t mag = fp_trunc(a, &FP_F64, &sign, &cls);
    if (cls == FPK_NAN) return 0;
    if (cls == FPK_BIG || cls == FPK_INF || mag > 0x7FFFFFFFu) return sign ? (int32_t)0x80000000u : 0x7FFFFFFF;
    return (int32_t)(sign ? (uint32_t)0u - (uint32_t)mag : (uint32_t)mag);
}

/* int64 -> double.  This is one of the two conversions a 32-bit-GPR MIPS has no
 * instruction for at all, which is why it was already here before the FPU law
 * moved every operation off the FPU. */
uint64_t __floatdidf(int64_t v)
{
    const uint32_t sign = (v < 0) ? 1u : 0u;
    const uint64_t mag = (v < 0) ? (uint64_t)0u - (uint64_t)v : (uint64_t)v;
    return fp_pack(sign, mag, 0, 0, &FP_F64);
}

/* double -> int64, truncating toward zero and saturating outside int64. */
int64_t __fixdfdi(uint64_t a)
{
    uint32_t sign;
    int cls;
    const uint64_t mag = fp_trunc(a, &FP_F64, &sign, &cls);
    const uint64_t max_i64 = 0x7FFFFFFFFFFFFFFFULL;
    if (cls == FPK_NAN) return 0;
    if (cls == FPK_BIG || cls == FPK_INF || mag > max_i64) return sign ? (int64_t)(~max_i64) : (int64_t)max_i64;
    return sign ? -(int64_t)mag : (int64_t)mag;
}
