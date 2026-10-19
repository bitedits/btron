/*
 * ps2_gs_rec.c — Graphics Synthesizer GIF vertex packet recorder implementation
 *
 * Implements pure GIF packet recording and primitive boundary management.
 * Formats vertex streams into dense REGLIST packets with safe boundary clipping
 * and continuation across DMA bursts.
 *
 * Buffer Headroom & Safe Boundary Splitting:
 * When the staging buffer approaches capacity, primitive streams cannot be abruptly
 * truncated without breaking triangle topologies (losing vertices from triangle strips
 * or fans).
 * Headroom (`GS_REC_HEADROOM`) ensures the buffer reserves enough space to complete
 * the current primitive boundary before closing the packet group and re-emitting
 * continuation vertices in the subsequent group.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "ps2_gs_rec.h"
#include "ps2_gs_pack.h"

/* Headroom: tag pair, 3 vertices (worst-case for triangle list boundary), and padding */
#define GS_REC_HEADROOM (2u + 3u * GS_REC_REGS_MAX + 1u)

#define GS_REC_NO_GROUP 0xFFFFFFFFu

/* Extracts texture mapping enable (TME) flag from packed PRIM word */
#define GS_REC_TME(prim) (((prim) >> 4) & 1ULL)

/*
 * Register Lists for REGLIST Packets:
 * 1. Flat/Untextured (NREG=2): RGBAQ (colour), XYZ2 (coordinates and raster kick).
 * 2. Textured (NREG=3): RGBAQ (colour), ST (texture coordinates), XYZ2 (kick).
 */
static const uint32_t s_regs_flat[GS_REC_NREG_UNTEX] = { GS_NIB_RGBAQ, GS_NIB_XYZ2 };
static const uint32_t s_regs_tex[GS_REC_NREG_TEX] = { GS_NIB_RGBAQ, GS_NIB_ST, GS_NIB_XYZ2 };

/* Slot indices mapping internal vertex values to the active register list */
static const uint32_t s_slot_flat[GS_REC_NREG_UNTEX] = { 0u, 2u };
static const uint32_t s_slot_tex[GS_REC_NREG_TEX] = { 0u, 1u, 2u };

/* Staging buffer: 64-bit register halves (2 halves per 128-bit quadword) */
static uint64_t s_qw[GS_REC_QW_CAP * 2u];
static gs_rec_sink s_sink;
static void *s_user;

static uint64_t s_prim;                  /* Active primitive configuration word */
static uint32_t s_op;                    /* Active primitive opcode */
static int      s_prim_set;              /* Non-zero if gs_rec_prim() has been called */
static uint64_t s_rgbaq;
static uint64_t s_st;
static uint32_t s_tag_at = GS_REC_NO_GROUP;  /* Buffer index of active GIF tag */
static uint32_t s_nloop;                 /* Vertex count in active group */
static uint32_t s_w;                     /* 64-bit words written in active buffer */
static uint32_t s_nreg = GS_REC_NREG_UNTEX;  /* Active register count (2 or 3) */

/* The two vertices a continued strip needs, and the apex a continued fan needs.  Kept as whole
 * register sets -- the three values a vertex is made of, not the two or three a list happens to
 * name -- because a re-emitted vertex has to reproduce exactly what the first copy wrote, including
 * the colour and texture coordinate it carried, which the next glColor would otherwise quietly
 * change.  The untextured group simply does not hand over the ST it is holding, which is why the
 * window is sized by GS_REC_REGS_MAX and not by the open group. */
static uint64_t s_tail[2][GS_REC_REGS_MAX];
static uint64_t s_apex[GS_REC_REGS_MAX];

static uint32_t s_sends, s_verts, s_unsplittable;
static uint32_t s_prim_writes;   /* AD groups that programmed PRIM, one per vertex group sent */

uint32_t gs_rec_sends(void) { return s_sends; }
uint32_t gs_rec_verts(void) { return s_verts; }
uint32_t gs_rec_unsplittable(void) { return s_unsplittable; }
uint32_t gs_rec_prim_writes(void) { return s_prim_writes; }

/* The first vertex group's span, in the same whole pixels gs_rec_vertex() is handed.  It used to
 * be kept as floats and converted once at the freeze, because this file is one of the units
 * compiled -msoft-float and a floor/ceil per vertex would be a libcall per vertex in the same
 * frame the row is timing; now that the vertices are integers the accumulation is four integer
 * compares, the conversion is a copy, and the two libcalls at the freeze went with it.  While the
 * first group is open the extra work is four compares a vertex; the moment it closes,
 * `s_span_done` makes the whole thing one integer test for the rest of the frame.
 * See ps2_gs_rec.h for what the span is for and why its edges round inward. */
static int32_t s_span[4];               /* x low, y low, x high, y high */
static uint32_t s_span_n;              /* vertices in the span so far */
static uint32_t s_span_done;           /* the first group has closed: stop accumulating */
static int32_t s_span0[4];             /* the frozen span, outward-conservative integers */
static uint32_t s_span0_valid;

int gs_rec_bbox_first(int32_t *l, int32_t *t, int32_t *r, int32_t *b)
{
    if (!s_span0_valid) return -1;
    if (l) *l = s_span0[0];
    if (t) *t = s_span0[1];
    if (r) *r = s_span0[2];
    if (b) *b = s_span0[3];
    return 0;
}

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
    s_w = 0u;
    s_nreg = GS_REC_NREG_UNTEX;
    s_sends = 0u;
    s_verts = 0u;
    s_unsplittable = 0u;
    s_prim_writes = 0u;
    s_span_n = 0u;
    s_span_done = 0u;
    s_span0_valid = 0u;
}

/* One vertex's registers, handed over in the order the open group's REGS list names them and
 * appended to that group, and the sliding window that a
 * continued primitive needs updated on the way. */
static void write_vertex(const uint64_t v[GS_REC_REGS_MAX]);

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
    uint64_t a[GS_REC_REGS_MAX], b[GS_REC_REGS_MAX];
    uint32_t i;

    if (s_op != GS_OP_TRISTP && s_op != GS_OP_LINESTP && s_op != GS_OP_TRIFAN) return;

    /* Both copies are taken before either is written, because writing is what moves the sliding
     * window: reading the second vertex out of s_tail *after* the first has been handed over
     * returns the first one again.  The stream then holds the same vertex twice in a row -- a
     * well-formed packet, a degenerate first triangle, and a strip continuing from an edge it
     * never had, with nothing on the bus to show for it. */
    first = (s_op == GS_OP_TRIFAN) ? s_apex : s_tail[0];
    for (i = 0; i < GS_REC_REGS_MAX; i++) {
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
        /* A tag with NLOOP 0 is not a group, it is a malformed packet, so the two halves
         * written for it are simply forgotten rather than sent. */
        s_w = 0u;
        s_tag_at = GS_REC_NO_GROUP;
        return;
    }

    /* The frame's first vertex group has just completed, so its span is now a fact about the
     * picture rather than about geometry still to come.  The copy is the whole conversion: the
     * span is accumulated in the pixels the vertices arrived in, so the inward rounding the float
     * accumulator needed is identity here, and the frozen rect is still one the group provably
     * reaches -- it is the group's own extreme vertices, named exactly. */
    if (!s_span_done) {
        if (s_span_n != 0u) {
            s_span0[0] = s_span[0];
            s_span0[1] = s_span[1];
            s_span0[2] = s_span[2];
            s_span0[3] = s_span[3];
            s_span0_valid = 1u;
        }
        s_span_done = 1u;
    }

    /*
     * Finalize GIF Tag and Quadword Alignment:
     * Back-patches the tag with the finalized loop count (`s_nloop`).
     * EOP is set because each group forms an autonomous DMA transfer burst.
     */
    s_qw[s_tag_at] = gs_pack_giftag(s_nloop, 1u, 1u, s_prim, GS_FLG_REGLIST, s_nreg);

    /*
     * 128-bit Quadword Padding:
     * Textured groups (NREG=3) produce an odd number of 64-bit register halves when
     * `s_nloop` is odd. Pad the trailing half with zero to prevent transferring stale
     * garbage over the DMA bus.
     */
    if ((s_w & 1u) != 0u) s_qw[s_w++] = 0u;

    if (s_sink) s_sink(s_user, s_qw, s_w / 2u);
    s_sends++;

    s_w = 0u;
    s_nloop = 0u;
    s_tag_at = GS_REC_NO_GROUP;
}

/* High half of an A+D quadword addressing GS_REG_PRIM */
static const uint32_t s_gp_prim[1] = { GS_GP_PRIM };

static void open_group(void)
{
    const uint32_t *regs;
    uint64_t prim = s_prim;

    if (s_tag_at != GS_REC_NO_GROUP) return;

    /*
     * Hardware Primitive Preamble:
     * Before opening a vertex burst, issue an explicit A+D packed register write to
     * GS_REG_PRIM. This guarantees primitive mode selection and Gouraud interpolation
     * regardless of whether the hardware revision latches the GIF tag's embedded PRIM field.
     */
    if (gs_rec_ad(&prim, s_gp_prim, 1u) == 0) s_prim_writes++;

    /* Select register list based on texture mapping enablement */
    s_nreg = GS_REC_TME(s_prim) ? GS_REC_NREG_TEX : GS_REC_NREG_UNTEX;
    regs = (s_nreg == GS_REC_NREG_TEX) ? s_regs_tex : s_regs_flat;

    /* Allocate GIF tag quadword (tag header in low 64 bits, REGS list in high 64 bits) */
    s_qw[s_w]      = gs_pack_giftag(0u, 1u, 1u, s_prim, GS_FLG_REGLIST, s_nreg);
    s_qw[s_w + 1u] = gs_pack_regs(regs, s_nreg);
    s_tag_at = s_w;
    s_w += 2u;
    s_span_n = 0u;
}

static void write_vertex(const uint64_t v[GS_REC_REGS_MAX])
{
    const uint32_t *slot = (s_nreg == GS_REC_NREG_TEX) ? s_slot_tex : s_slot_flat;
    uint32_t i;

    /* The first vertex of a fan is the apex every following triangle shares, so it outlives the
     * two-deep window the other types need. */
    if (s_nloop == 0u) {
        for (i = 0; i < GS_REC_REGS_MAX; i++) s_apex[i] = v[i];
    }

    for (i = 0; i < GS_REC_REGS_MAX; i++) s_tail[0][i] = s_tail[1][i];
    for (i = 0; i < GS_REC_REGS_MAX; i++) s_tail[1][i] = v[i];

    /* The list's registers, hand over fist, in the order the tag names: RGBAQ then XYZ2 for an
     * untextured group, RGBAQ then ST then the drawing kick for a textured one.  Nothing is padded
     * here and nothing is dropped -- the pad belongs to the group and close_group() owns it, because
     * whether one is needed is a fact about the LOOP count, not about this vertex.  `s_w` advances by
     * s_nreg so the count the sink is handed, after the header and the pad, is the count the tag's
     * NLOOP times its NREG claims: a stride that drifted by a half names the next vertex's colour
     * where this one's coordinate lives, which is a malformed group rather than a wrong pixel. */
    for (i = 0u; i < s_nreg; i++) s_qw[s_w++] = v[slot[i]];
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

int gs_rec_vertex(int32_t x_px, int32_t y_px, uint32_t z)
{
    const uint32_t nreg = GS_REC_TME(s_prim) ? GS_REC_NREG_TEX : GS_REC_NREG_UNTEX;
    /* Halves this vertex needs: its own registers, the tag pair if the group is not open yet, and
     * one more for the pad an odd-count group will end up owing -- charged to every textured vertex
     * rather than to the one that triggers it, which costs at most a half of buffer and keeps the
     * invariant "the pad always has somewhere to go" true at every append instead of only at the end. */
    uint32_t room = nreg + (nreg & 1u) + (s_tag_at == GS_REC_NO_GROUP ? 2u : 0u);
    uint64_t v[GS_REC_REGS_MAX];

    if (!s_prim_set) return -1;

    v[0] = s_rgbaq;
    v[1] = s_st;
    v[2] = gs_pack_xyz2(gs_vertex_coord16(x_px), gs_vertex_coord16(y_px), z);

    /* The span of the group being written, while it is still the frame's first one.  Four integer
     * compares and no conversion at all: the pixels accumulated here are the same integers the
     * vertex word is built from, so this reads the geometry rather than decoding the packet back. */
    if (!s_span_done) {
        if (s_span_n == 0u) {
            s_span[0] = s_span[2] = x_px;
            s_span[1] = s_span[3] = y_px;
            s_span_n = 1u;
        } else {
            if (x_px < s_span[0]) s_span[0] = x_px;
            if (x_px > s_span[2]) s_span[2] = x_px;
            if (y_px < s_span[1]) s_span[1] = y_px;
            if (y_px > s_span[3]) s_span[3] = y_px;
            s_span_n++;
        }
    }

    if (s_w + room > GS_REC_QW_CAP * 2u) {
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
    } else if (s_w + room > GS_REC_QW_CAP * 2u - GS_REC_HEADROOM && can_split()) {
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

    total = GS_REC_AD_QWC(n);          /* one tag quadword, then one per register write */
    if (total > GS_REC_QW_CAP) return -1;

    /* The vertices recorded before this call draw with the state before it, so whatever is open
     * has to be sent first.  close_group() leaves the cursor at 0, which is what makes writing the
     * AD group at the start of the buffer safe -- and nothing here touches the cursor afterwards: the
     * buffer is empty again once the sink has taken these words.  An AD group needs no pad and is
     * already whole quadwords: NLOOP=n, NREG=1, PACKED, so 2 + 2n halves is 1 + n of them per
     * register write, which is GS_REC_AD_QWC(n) read as a halves count rather than as a second one. */
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
