/*
 * ps2_gs_rec.c — vertices accumulated into GIF groups, with no register and no DMA in sight
 *
 * Increment 4 of the plan of record (doc/md/PS2.md, "The EE/GS backend").  See ps2_gs_rec.h
 * for what a group holds and why PRIM travels with every vertex.  What is only visible here is
 * the boundary rule: a buffer that fills mid-primitive has to be cut where no triangle loses a
 * vertex, and the vertices a strip needs in order to continue have to be written a second time.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "ps2_gs_rec.h"
#include "ps2_gs_pack.h"

/* A group occupies one tag quadword and then four quadwords per vertex.  The headroom is what lets
 * a full buffer be cut at the *safe* boundary instead of at the moment it fills: three vertices is
 * the most any supported primitive type needs before its next whole primitive starts (a TRI_LIST
 * sitting at one vertex), plus the tag, so with this much room left the recorder can keep
 * appending until a boundary arrives, and the case where no boundary arrives before the buffer
 * is truly full becomes an error it can count instead of a vertex it silently loses. */
#define GS_REC_HEADROOM (1u + 3u * GS_REC_REGS_N)

#define GS_REC_NO_GROUP 0xFFFFFFFFu

/* The group's REGS list, in the order its data quadwords are consumed: state first, the drawing
 * kick last. */
static const uint32_t s_regs[GS_REC_REGS_N] = {
    GS_NIB_PRIM, GS_NIB_RGBAQ, GS_NIB_ST, GS_NIB_XYZ2
};

/* One quadword is 128 bits and every GS register is 64 of them, so a packed register occupies a
 * pair of `uint64_t` -- the value in the low half, nothing in the high -- exactly as the blit
 * path's own AD packets do it, where the high half instead carries the register offset.  That
 * equivalence is not a style choice: `send_trx_setup()` in ps2_gs.c sends a tag and four AD
 * registers as five quadwords with NLOOP=4 and NREG=1, which is one data quadword per REGS
 * entry, and the canvas is the proof.  A recorder that stored one 64-bit word per register would
 * hand the GIF alternating registers (PRIM, then ST in RGBAQ's place) and a QWC twice the bytes it
 * was given.  So `s_qw` is pairs, and every index below that says "quadword" multiplies by two. */
static uint64_t s_qw[GS_REC_QW_CAP * 2u];
static gs_rec_sink s_sink;
static void *s_user;

static uint64_t s_prim;                  /* this group's PRIM word */
static uint32_t s_op;                    /* its op code: the low three bits of that word */
static int      s_prim_set;              /* gs_rec_prim() has been called at least once */
static uint64_t s_rgbaq;
static uint64_t s_st;
static uint32_t s_tag_at = GS_REC_NO_GROUP;
static uint32_t s_nloop;                 /* vertices in the open group */
static uint32_t s_n;                     /* quadwords written since the last send */

/* The two vertices a continued strip needs, and the apex a continued fan needs.  Kept as whole
 * quadword sets rather than as coordinates, because a re-emitted vertex has to reproduce exactly
 * what the first copy wrote -- including the colour and texture coordinate it carried, which the
 * next glColor would otherwise quietly change. */
static uint64_t s_tail[2][GS_REC_REGS_N];
static uint64_t s_apex[GS_REC_REGS_N];

static uint32_t s_sends, s_verts, s_unsplittable;

uint32_t gs_rec_sends(void) { return s_sends; }
uint32_t gs_rec_verts(void) { return s_verts; }
uint32_t gs_rec_unsplittable(void) { return s_unsplittable; }

void gs_rec_init(gs_rec_sink sink, void *user)
{
    s_sink = sink;
    s_user = user;
    s_prim = 0u;
    s_op = 0u;
    s_prim_set = 0;
    s_rgbaq = gs_pack_rgbaq(255u, 255u, 255u, 255u, GS_Q_ONE);
    s_st = gs_pack_st(0.0f, 0.0f);
    s_tag_at = GS_REC_NO_GROUP;
    s_nloop = 0u;
    s_n = 0u;
    s_sends = 0u;
    s_verts = 0u;
    s_unsplittable = 0u;
}

/* One vertex's four quadwords, appended to the open group, and the sliding window that a
 * continued primitive needs updated on the way. */
static void write_vertex(const uint64_t v[GS_REC_REGS_N]);

/* Can the open group be closed right here, so that the next vertex starts a whole primitive?
 * The types that share vertices between primitives say yes once enough of them exist; a list
 * type says yes only between complete primitives, because a cut after one vertex of a triangle
 * would draw that triangle with two vertices and take its third from the next group's first. */
static int can_split(void)
{
    switch (s_op) {
    case GS_OP_POINT:   return 1;                    /* every vertex is one primitive */
    case GS_OP_LINE:    return (s_nloop % 2u) == 0u;
    case GS_OP_LINESTP: return s_nloop >= 2u;
    case GS_OP_TRI:     return (s_nloop % 3u) == 0u;
    case GS_OP_TRISTP:  return s_nloop >= 2u;
    case GS_OP_TRIFAN:  return s_nloop >= 2u;
    case GS_OP_SPRITE:  return (s_nloop % 2u) == 0u;
    default:            return 0;
    }
}

/* Which of the vertices already written have to be written again for the primitive to continue
 * in the new group: the edge a strip hangs its next triangle on, or the apex and last edge of a
 * fan.  The list types need nothing, which is why can_split() only offers their boundaries.
 *
 * A restarted strip loses the hardware's vertex-parity alternation, so its first triangle in the
 * new group keeps the re-emitted edge's order rather than flipping it -- harmless for this layer
 * because nothing culls: the GS rasterises either winding onto the same pixels. */
static void reemit_continuation(void)
{
    const uint64_t *first;
    uint64_t a[GS_REC_REGS_N], b[GS_REC_REGS_N];
    uint32_t i;

    if (s_op != GS_OP_TRISTP && s_op != GS_OP_LINESTP && s_op != GS_OP_TRIFAN) return;

    /* Both copies are taken before either is written, because writing is what moves the sliding
     * window: reading the second vertex out of s_tail *after* the first has been handed over
     * returns the first one again.  The stream then holds the same vertex twice in a row -- a
     * well-formed packet, a degenerate first triangle, and a strip continuing from an edge it
     * never had, with nothing on the bus to show for it. */
    first = (s_op == GS_OP_TRIFAN) ? s_apex : s_tail[0];
    for (i = 0; i < GS_REC_REGS_N; i++) {
        a[i] = first[i];
        b[i] = s_tail[1][i];
    }

    write_vertex(a);
    write_vertex(b);
}

static void close_group(void)
{
    if (s_tag_at == GS_REC_NO_GROUP) return;

    if (s_nloop == 0u) {
        /* A tag with NLOOP 0 is not a group, it is a malformed packet, so the two quadwords
         * written for it are simply forgotten rather than sent. */
        s_n = 0u;
        s_tag_at = GS_REC_NO_GROUP;
        return;
    }

    /* Back-patch the tag now that the loop count is final.  EOP is set because each send here is
     * one complete packet on a DMA transfer of its own, never a fragment of a larger one. */
    s_qw[2u * s_tag_at] = gs_pack_giftag(s_nloop, 1u, 1u, s_prim, GS_FLG_PACKED, GS_REC_REGS_N);

    if (s_sink) s_sink(s_user, s_qw, s_n);
    s_sends++;

    s_n = 0u;
    s_nloop = 0u;
    s_tag_at = GS_REC_NO_GROUP;
}

static void open_group(void)
{
    if (s_tag_at != GS_REC_NO_GROUP) return;

    /* One quadword: the tag's own 64 bits and then the REGS list in its high half.  It is not an
     * AD pair, and writing the list through gs_pack_pair() would put the four nibbles where a
     * register's value belongs -- which the GIF then decodes as PRIM, RGBAQ, PRIM, PRIM, i.e. the
     * same group's four data quadwords fed to the wrong four registers. */
    s_qw[2u * s_n]      = gs_pack_giftag(0u, 1u, 1u, s_prim, GS_FLG_PACKED, GS_REC_REGS_N);
    s_qw[2u * s_n + 1u] = gs_pack_regs(s_regs, GS_REC_REGS_N);
    s_tag_at = s_n;
    s_n += 1u;
}

static void write_vertex(const uint64_t v[GS_REC_REGS_N])
{
    uint32_t i;

    /* The first vertex of a fan is the apex every following triangle shares, so it outlives the
     * two-deep window the other types need. */
    if (s_nloop == 0u) {
        for (i = 0; i < GS_REC_REGS_N; i++) s_apex[i] = v[i];
    }

    for (i = 0; i < GS_REC_REGS_N; i++) s_tail[0][i] = s_tail[1][i];
    for (i = 0; i < GS_REC_REGS_N; i++) s_tail[1][i] = v[i];

    /* Each of the four registers takes a whole quadword; the half above the value is unused by a
     * 64-bit register and zeroed rather than left holding the previous group's neighbour. */
    for (i = 0; i < GS_REC_REGS_N; i++) {
        s_qw[2u * (s_n + i)]      = v[i];
        s_qw[2u * (s_n + i) + 1u] = 0u;
    }
    s_n += GS_REC_REGS_N;
    s_nloop++;
}

void gs_rec_prim(uint64_t prim)
{
    close_group();

    s_prim = prim;
    s_op = (uint32_t)(prim & 0x7u);
    s_prim_set = 1;
}

void gs_rec_color(uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{
    /* Q is the perspective divisor and this layer is affine, so it is always 1.0f: the GS would
     * otherwise divide the interpolated ST by whatever the vertex's own z implied. */
    s_rgbaq = gs_pack_rgbaq(r, g, b, a, GS_Q_ONE);
}

void gs_rec_texcoord(float s, float t)
{
    s_st = gs_pack_st(s, t);
}

int gs_rec_vertex(float x_px, float y_px, uint32_t z)
{
    uint64_t v[GS_REC_REGS_N];
    uint32_t room = GS_REC_REGS_N + (s_tag_at == GS_REC_NO_GROUP ? 1u : 0u);

    if (!s_prim_set) return -1;

    v[0] = s_prim;
    v[1] = s_rgbaq;
    v[2] = s_st;
    v[3] = gs_pack_xyz2(gs_vertex_coord16(x_px), gs_vertex_coord16(y_px), z);

    if (s_n + room > GS_REC_QW_CAP) {
        /* No room for this vertex.  A safe boundary exists only if the group can be cut here,
         * and cutting it is then mandatory: the alternative is a vertex neither sent nor
         * counted, which is a missing triangle with no evidence attached to it. */
        if (!can_split()) {
            s_unsplittable++;
            return -1;
        }
        close_group();
        open_group();
        reemit_continuation();
    } else if (s_n + room > GS_REC_QW_CAP - GS_REC_HEADROOM && can_split()) {
        /* Still room, but not for three more vertices plus a tag: take the boundary while it is
         * in hand rather than after the buffer is full and the headroom is spent. */
        close_group();
        open_group();
        reemit_continuation();
    } else if (s_tag_at == GS_REC_NO_GROUP) {
        open_group();
    }

    write_vertex(v);
    s_verts++;
    return 0;
}

void gs_rec_end(void)
{
    close_group();
}

/* The one-register nibble, and the group that carries register writes between two vertex groups.
 * See gs_rec_ad()'s header comment for why this is a group of its own rather than a fifth
 * register in the vertex group's list. */
static const uint32_t s_regs_ad[1] = { GS_NIB_AD };

int gs_rec_ad(const uint64_t *data, const uint32_t *gp_offs, uint32_t n)
{
    uint32_t i, total;

    if (n == 0u) return 0;
    if (data == 0 || gp_offs == 0) return -1;

    total = 1u + 2u * n;            /* one tag quadword, then one per register write */
    if (total > GS_REC_QW_CAP) return -1;

    /* The vertices recorded before this call draw with the state before it, so whatever is open
     * has to be sent first.  close_group() leaves s_n at 0, which is what makes writing the AD
     * group at the start of the buffer safe -- and nothing here touches s_n afterwards: the
     * buffer is empty again once the sink has taken these words. */
    close_group();

    /* NREG is 1 and PRE is clear: this group draws nothing, it only moves state, so the tag's
     * PRIM field stays out of it -- exactly as the environment packet and send_trx_setup()'s own
     * proven AD packet do. */
    s_qw[0]      = gs_pack_giftag(n, 1u, 0u, 0u, GS_FLG_PACKED, 1u);
    s_qw[1]      = gs_pack_regs(s_regs_ad, 1u);
    for (i = 0; i < n; i++) {
        /* The AD form is the one whose high half is not padding: it carries the register's GP
         * offset, which is how a data quadword knows which register to land in. */
        s_qw[2u + 2u * i]      = data[i];
        s_qw[2u + 2u * i + 1u] = (uint64_t)(gp_offs[i] & 0x7Fu);
    }

    if (s_sink) s_sink(s_user, s_qw, total);
    s_sends++;
    return 0;
}
