/*
 * ps2_gs_rec.c — vertices accumulated into GIF groups, with no register and no DMA in sight
 *
 * Increment 4 of the plan of record (doc/txt/PS2.txt, ch. 16 "The increments").  See ps2_gs_rec.h
 * for what a group holds and for the census rows that chose its register list.  What is only visible
 * here is the boundary rule: a buffer that fills mid-primitive has to be cut where no triangle loses
 * a vertex, and the vertices a strip needs in order to continue have to be written a second time.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "ps2_gs_rec.h"
#include "ps2_gs_pack.h"

/* A group occupies one tag quadword -- the tag itself in its low half, its REGS list in the high --
 * and then NLOOP * NREG registers laid end to end, which is what a dense `FLG=REGLIST` means.  With
 * the two lists this file writes, an untextured vertex is one whole quadword and a textured one is
 * one and a half, so this buffer's cursor is counted in 64-bit register halves and every count handed
 * to the sink is `s_w / 2u`.  A group of an odd number of registers ends on a half, and
 * close_group() pads that half with a zero: the DMA moves quadwords, so an unpadded last quadword
 * would carry whatever the previous frame left in its upper half -- a stale colour read as this
 * frame's vertex, in a packet whose length is a fact about memory it never wrote.
 *
 * The headroom is what lets a full buffer be cut at the *safe* boundary instead of at the moment it
 * fills: three vertices is the most any supported primitive type needs before its next whole
 * primitive starts (a TRI_LIST sitting at one vertex), plus the tag pair and the pad, so with this
 * much room left the recorder can keep appending until a boundary arrives, and the case where no
 * boundary arrives before the buffer is truly full becomes an error it can count instead of a vertex
 * it silently loses.
 *
 * It is written in halves because the cursor is in halves, and in GS_REC_REGS_MAX rather than a
 * literal because the per-vertex count changed under LANE 19 rung 12f: a headroom sized for the
 * four-register chain leaves the buffer half empty at every split, and one sized for two while a
 * textured group writes three and pads to four is an overrun at the boundary rather than a cut. */
#define GS_REC_HEADROOM (2u + 3u * GS_REC_REGS_MAX + 1u)   /* halves: tag pair, three vertices, pad */

#define GS_REC_NO_GROUP 0xFFFFFFFFu

/* PRIM's texture bit, the one field of the primitive descriptor this file reads.  The field table is
 * in ps2_gs_pack.h; naming it here keeps the choice of list below from looking like a magic shift. */
#define GS_REC_TME(prim) (((prim) >> 4) & 1ULL)

/* The two REGS lists, in the order their data registers pack: state first, the drawing kick last
 * because XYZ2 is what completes a vertex.  Neither names PRIM -- the tag carries it under PRE --
 * and neither carries the NOP slot that used to hold PRIM's old seat.  The two shapes are the two
 * LANE 19 rung 12e measured as INTERPOLATING, on this same tag framing (D and F in its census);
 * the four-register list this replaces is the one that measured FLAT (A and B), and ps2sdk's own
 * Gouraud cube draws the two-register form.  See ps2_gs_rec.h for the rows. */
static const uint32_t s_regs_flat[GS_REC_NREG_UNTEX] = { GS_NIB_RGBAQ, GS_NIB_XYZ2 };
static const uint32_t s_regs_tex[GS_REC_NREG_TEX] = { GS_NIB_RGBAQ, GS_NIB_ST, GS_NIB_XYZ2 };

/* Which of a vertex's three semantic registers each list names, and in what order.  Kept separate
 * from the nibble tables above because they answer different questions: those say what the GIF
 * decoder is told to expect, these say what the recorder has to hand over, and the two are only
 * consistent by construction.  `make test-ps2-gs-rec` reads a group back by nibble and compares it
 * against what the caller asked for, which is the check that they still agree. */
static const uint32_t s_slot_flat[GS_REC_NREG_UNTEX] = { 0u, 2u };
static const uint32_t s_slot_tex[GS_REC_NREG_TEX] = { 0u, 1u, 2u };

/* One quadword is 128 bits and every GS register is 64 of them, so `s_qw` is a stream of `uint64_t`
 * HALVES and the unit a caller sees -- the QWC handed to channel 2 -- is pairs of them.  What a half
 * MEANS depends on the group: a vertex group is a dense REGLIST, so its halves are consecutive
 * registers of the REGS list with no gaps, and a group of an odd register count carries a register
 * across a quadword boundary the way a 3-register chain does; an AD group is PACKED with NREG=1, so
 * its low half is the register's value and its high half is that register's GP offset, which is why
 * `send_trx_setup()` in ps2_gs.c fits four registers into five quadwords and the canvas is the proof
 * of that shape.  The two counts are not interchangeable, and the recorder keeps them apart by
 * tracking the cursor in halves and dividing once at the send: a vertex counted as four quadwords --
 * the sparse PACKED form this file used to emit -- both doubles the bytes on the bus and names a
 * primitive the GS never kicks (LANE 19 rung 5, ps2_gs_poster.h), and a 3-register chain counted as
 * floor(3/2) loses a vertex's texture coordinate and everything after it. */
static uint64_t s_qw[GS_REC_QW_CAP * 2u];
static gs_rec_sink s_sink;
static void *s_user;

static uint64_t s_prim;                  /* this group's PRIM word, carried by its tag */
static uint32_t s_op;                    /* its op code: the low three bits of that word */
static int      s_prim_set;              /* gs_rec_prim() has been called at least once */
static uint64_t s_rgbaq;
static uint64_t s_st;
static uint32_t s_tag_at = GS_REC_NO_GROUP;  /* half index of the open group's tag */
static uint32_t s_nloop;                 /* vertices in the open group */
static uint32_t s_w;                     /* register HALVES written since the last send */
static uint32_t s_nreg = GS_REC_NREG_UNTEX;  /* the open group's register count: 2 or 3 */

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

    /* Back-patch the tag now that the loop count is final. EOP is set because each send here is
     * one complete packet on a DMA transfer of its own. PRE is SET to 1 and PRIM carries s_prim,
     * the same word open_group() writes into the PRIM register with an AD group of its own: this GS
     * reads the write and (per ps2sdk) a real one may also read the field, so naming it twice from
     * one variable is the only way both machines get the primitive the packet asked for.  The old
     * comment on this line claimed the tag field did the programming on its own -- and that
     * repeating PRIM per vertex was what kept a strip's texture and blend flags alive.  Rungs 12e
     * and 12u separate the two halves of that claim: the repetition is unnecessary (one AD write
     * stands a whole group's worth of loops, and 12u's V2 group of three loops proves it on this
     * machine), and the tag field is NOT sufficient -- with no other packet in front of it, the
     * identical chain laid one pixel at either value of PRE. */
    s_qw[s_tag_at] = gs_pack_giftag(s_nloop, 1u, 1u, s_prim, GS_FLG_REGLIST, s_nreg);

    /* A group whose register count is odd ends between two quadwords, and the DMA moves quadwords:
     * the half that would be read as the last register's partner is whatever the buffer held before
     * this frame wrote over it.  Zero it, which is what ps2sdk does for its own odd chains
     * (texture.c:259-263) and the exact shape boxes E and F measured.  An even group -- every
     * untextured one, always two registers to a vertex -- takes no branch here, which is why the
     * count below needs no rounding term. */
    if ((s_w & 1u) != 0u) s_qw[s_w++] = 0u;

    if (s_sink) s_sink(s_user, s_qw, s_w / 2u);
    s_sends++;

    s_w = 0u;
    s_nloop = 0u;
    s_tag_at = GS_REC_NO_GROUP;
}

/* The high half of an AD data quadword that programs PRIM.  The low half is `s_prim` itself -- the
 * same 64-bit word the group's tag names -- so the write and the field cannot disagree. */
static const uint32_t s_gp_prim[1] = { GS_GP_PRIM };

static void open_group(void)
{
    const uint32_t *regs;
    uint64_t prim = s_prim;

    if (s_tag_at != GS_REC_NO_GROUP) return;

    /* LANE 19 rung 12u measured this on the machine, with no neighbouring packet around to lend it
     * state: box D's captured recorder group, replayed alone on a canvas-reset band, laid ONE pixel
     * at its own apex vertex at BOTH values of PRE (V0 as captured, V1 with PRE cleared so the tag's
     * PRIM field was the only thing left speaking), and laid a correct 610-pixel triangle --
     * x539..620 of a 536..623 ask, which is the GS's own edge correction, not this file's arithmetic
     * -- the moment an AD write of the identical prim word went in front of it.  The two AD writes
     * that followed it differed in IIP alone and came out field=609 and field=0, which is the same
     * run naming the bit the rasteriser interpolates on.
     *
     * So PRIM is a register this stream has to WRITE, not a field it can merely mention, and nothing
     * upstream leaves it where the next group wants it: eegs_present.c's depth clear is an eight-write
     * AD group that ends on PRIM=SPRITE, so a recorder group that inherits its primitive draws a
     * sprite with three vertices -- one whole quad and one leftover kick, which is what a page of
     * solid rectangles and dots looks like.  That is the mechanism behind the operator's flat gears,
     * and it is why this file, not the backend, owns the fix: the packet is where the ask lives.
     *
     * The write goes out as its own group, in exactly the shape the run measured, by way of the
     * mechanism the texture switches already ship through: PACKED, NLOOP=1, NREG=1, PRE clear, one
     * data quadword whose low half is the word and whose high half is GS_GP_PRIM.  Writing it here
     * rather than in gs_rec_prim() means a declared primitive that never gets a vertex costs nothing,
     * and a group that reaches the bus always has its own primitive in front of it.
     *
     * Once per GROUP and not once per CHANGE, because the recorder does not own the other writers in
     * the frame; a cached "PRIM already says this" would be a claim about machine state that another
     * packet can move.  The cost is two quadwords and one DMA start per group -- a measured XMB frame
     * is 47 groups against 5,322 quadwords -- and the next bench run owns the number rather than this
     * comment guessing at it. */
    /* n=1 is two quadwords, so gs_rec_ad()'s buffer test cannot refuse it in any build (the shipped
     * GS_REC_QW_CAP is 4,096 and the host gate's is 32).  Counting the write only when it went out
     * keeps gs_rec_prim_writes() a fact about the bus rather than an intention. */
    if (gs_rec_ad(&prim, s_gp_prim, 1u) == 0) s_prim_writes++;

    /* The primitive decides the list, not the caller: a textured stream that left its ST out of the
     * chain would sample the page with the previous vertex's coordinate, and an untextured one that
     * carried an ST slot would pay a register for a value the rasteriser never reads. */
    s_nreg = GS_REC_TME(s_prim) ? GS_REC_NREG_TEX : GS_REC_NREG_UNTEX;
    regs = (s_nreg == GS_REC_NREG_TEX) ? s_regs_tex : s_regs_flat;

    /* One quadword: the tag's own 64 bits with PRE=1 and s_prim, and then the REGS list in the half
     * above them.  NREG is written here as 0 and rewritten by close_group() with the real loop
     * count; the register count cannot change once the group is open, because a data stream whose
     * second vertex does not have the length its first one has is a stream with no decoder. */
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
