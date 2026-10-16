/*
 * ps2_gs_read.c — the Local->Host request, as words
 *
 * See ps2_gs_read.h for what these words are for (censusing what the GS drew, with no human looking
 * at a window) and for the two facts that decide their shape: the download is a VIF1/channel-1
 * operation and not a channel-2 one, and its length is RRW*RRH*bpp>>7 quadwords, which the caller has
 * to match or the transfer is silently short.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "ps2_gs_read.h"

#include "ps2_gs_pack.h"
#include "ps2_gs_vram.h"

/* Every nibble of this group is AD: the five things it sends are register writes, not primitive
 * data, and NLOOP 5 with NREG 1 is ps2sdk's tested screenshot tag for the same five. */
static const uint32_t s_regs_ad[1] = { GS_NIB_AD };

static gs_read_err s_last_err = GS_READ_OK;

gs_read_err gs_read_last_err(void)
{
    return s_last_err;
}

uint32_t gs_read_bpp(uint32_t psm)
{
    /* The low three bits are the raster's byte width; the bits above them in a PSM byte are the
     * bit-select and interleave fields, which a read does not use and which must not make a CT32
     * picture look like an unsupported format.  4..7 are the 4-bit and swizzled classes the GS
     * refuses to download (PCSX2: "Illegal format for GS upload"), so they answer 0 and the packet
     * is refused rather than sized by a guess. */
    switch (psm & 7u) {
    case 0u: return 32u;
    case 1u: return 24u;
    case 2u: return 16u;
    case 3u: return  8u;
    default: return  0u;
    }
}

uint32_t gs_read_image_qwc(uint32_t w, uint32_t h, uint32_t psm)
{
    const uint32_t bpp = gs_read_bpp(psm);

    if (bpp == 0u) return 0u;
    /* The same 12-bit bound the request enforces, checked here so the multiply below cannot wrap a
     * uint32_t on a caller that skipped the packet builder and asked for a 64k-wide strip. */
    if (w > GS_READ_MAX_TRX || h > GS_READ_MAX_TRX) return 0u;

    /* Pixels x bits, over 128 bits per quadword.  >> 7 rather than / (16 / bytes_per_pixel) because
     * it is the hardware's own expression and it rounds the same way: down, losing any partial
     * quadword -- which is the loss gs_read_request_packet()'s alignment law refuses to allow. */
    return (w * h * bpp) >> 7;
}

/* Every law the request has to satisfy, in the order a caller is most likely to break them.  Returns
 * the reason, or GS_READ_OK.  The sums that decide the two address bounds are 64-bit: their operands
 * are a base, a row pitch in bytes and a row COUNT, and a reader checking the bound should not have to
 * prove three intermediate products fit in a uint32_t first. */
static gs_read_err request_law(const gs_read_req *r, uint32_t *out_sba)
{
    const uint32_t bpp = gs_read_bpp(r->psm);
    const uint32_t bytes_per_px = bpp / 8u;
    uint64_t pitch_bytes, last_row_end;
    uint32_t sba;

    if (bpp == 0u)
        return GS_READ_E_PSM;

    if (r->w == 0u || r->h == 0u || r->w > GS_READ_MAX_TRX || r->h > GS_READ_MAX_TRX)
        return GS_READ_E_SIZE;

    /* The 16-bit DMA count, checked on the 64-bit product so a rectangle at the top of both 12-bit
     * fields cannot wrap the comparison into an acceptance: 4095 x 4095 at CT32 is 523,804,800
     * pixels' worth of quadwords, and the number a caller would have written to CH_QWC is that
     * modulo 65,536.  A request that passes everything else and fails here is not a broken idea --
     * it is a page, and a page is a loop of bands. */
    if ((uint64_t)r->w * (uint64_t)r->h * (uint64_t)bpp
            > (uint64_t)GS_READ_MAX_QWC * 128u)
        return GS_READ_E_QWC;

    if (r->x > GS_READ_MAX_SS || r->y > GS_READ_MAX_SS)
        return GS_READ_E_POS;

    /* SPL/SPI are 0, so each row is taken from its first byte as whole quadwords: the area has to
     * BEGIN on one and its row has to END on one.  For CT32 that is x % 4 and w % 4; the general form
     * is in bytes because CT24's 3 bytes a pixel make 4 px an insufficient rule. */
    if (((uint64_t)r->x * bytes_per_px) % 16u != 0u)
        return GS_READ_E_ALIGN;
    if (((uint64_t)r->w * bytes_per_px) % 16u != 0u)
        return GS_READ_E_ALIGN;

    /* SBW is a 6-bit field of 64-pixel units, so 63 names the widest buffer the transfer can walk. */
    if (r->sbw64 > 63u)
        return GS_READ_E_PITCH;

    /* The transfer advances one SBW row per RRH line, so the area lives inside a buffer of that
     * pitch: a rectangle that reaches past its own row reads the NEXT row's pixels at the wrong x,
     * and a census built on that counts colours the screen never showed. */
    pitch_bytes = (uint64_t)r->sbw64 * 64u * bytes_per_px;
    if ((uint64_t)r->x + r->w > (uint64_t)r->sbw64 * 64u)
        return GS_READ_E_PITCH;

    /* ...and the last of those rows has to be inside VRAM.  There is no height field in a
     * Local->Host request -- the GS just walks RRH rows from SASY -- so this bound exists only here. */
    last_row_end = (uint64_t)r->vram_byte + (uint64_t)r->y * pitch_bytes
                 + (uint64_t)(r->h - 1u) * pitch_bytes
                 + ((uint64_t)r->x + r->w) * bytes_per_px;
    if (last_row_end > GS_VRAM_BYTES)
        return GS_READ_E_PITCH;

    if (gs_vram_tba(r->vram_byte, &sba) != 0)
        return GS_READ_E_PAGE;

    *out_sba = sba;
    return GS_READ_OK;
}

int32_t gs_read_request_packet(const gs_read_req *r, uint64_t *qw, uint32_t cap_qw)
{
    uint32_t sba = 0u;

    /* The counter is per call, so a caller's `refuse=` column cannot inherit the last verb's reason
     * -- the same discipline gs_poster_dropped() keeps. */
    s_last_err = GS_READ_OK;

    if (r == 0 || qw == 0) { s_last_err = GS_READ_E_ARGS;  return -1; }
    if (cap_qw < GS_READ_REQ_QWC) { s_last_err = GS_READ_E_ARGS; return -1; }

    s_last_err = request_law(r, &sba);
    if (s_last_err != GS_READ_OK) return -1;

    /* Nothing above wrote a byte, so every refusal path leaves the caller's buffer exactly as it
     * found it.  From here the packet is known to be buildable and is written in one pass.
     *
     * The tag: NLOOP 5, EOP because this block is a DMA of its own, PRE 0 and PRIM 0 because it draws
     * nothing, FLG PACKED with NREG 1 -- one nibble per loop, and that nibble is always AD. */
    qw[0] = gs_pack_giftag(GS_READ_REQ_NREG, 1u, 0u, 0u, GS_FLG_PACKED, 1u);
    qw[1] = gs_pack_regs(s_regs_ad, 1u);

    /* BITBLTBUF's destination half (DBA/DBW) is unread by a Local->Host transfer -- the destination
     * is wherever DMAC's MADR points -- and DPSM is left equal to SPSM as the tested recipe does, so
     * the word matches a known-good one byte for byte instead of differing in fields nobody reads. */
    qw[2] = gs_pack_bitbltbuf(sba, r->sbw64, r->psm, 0u, 0u, r->psm);
    qw[3] = GS_GP_BITBLTBUF;

    /* The area's top-left, in pixels of the source buffer.  The destination position (DASX/DASY) is
     * 0 because there is no destination rectangle: the EE buffer is linear, and DIR 0 is the source
     * row order H->L. */
    qw[4] = gs_pack_trxpos(r->x, r->y, 0u, 0u, 0u);
    qw[5] = GS_GP_TRXPOS;

    qw[6] = gs_pack_trxreg(r->w, r->h);
    qw[7] = GS_GP_TRXREG;

    /* The receipt, armed before the request exists so it can only fire for this packet.  Its data
     * word is 0, as in the tested recipe, and that is not an oversight: the event is raised by the AD
     * WRITE to FINISH rather than by the value it carries -- PCSX2 installs GSState::GIFRegHandlerNull
     * for register 0x61 and sets CSR bit 1 from the register NUMBER, in Gif_FinishIRQ() -- and a value
     * of 1 would be a SIGNAL-style mask with no mask field to apply it to. */
    qw[8] = 0u;
    qw[9] = GS_GP_FINISH;

    /* INCD 1 = Local->Host.  This is the word that makes the download exist: PCSX2 reads it as
     * "(data[0] & 3) == 1", sizes the request from TRXREG and the format, and holds that many
     * quadwords for channel 1 to take. */
    qw[10] = gs_pack_trxdir(1u);
    qw[11] = GS_GP_TRXDIR;

    return (int32_t)GS_READ_REQ_QWC;
}

int32_t gs_read_block_packet(const gs_read_req *r, uint64_t *qw, uint32_t cap_qw)
{
    int32_t got;

    s_last_err = GS_READ_OK;

    if (qw == 0 || cap_qw < GS_READ_BLOCK_QWC) {
        /* Ask for the room first: writing the code quadword and then refusing the packet behind it
         * would hand VIF1 a DIRECT whose quadwords are not there. */
        s_last_err = GS_READ_E_ARGS;
        return -1;
    }

    got = gs_read_request_packet(r, qw + 2u, cap_qw - 1u);
    if (got < 0) return -1;                 /* s_last_err already names the law */

    /* VIF1 takes its commands as u32 words in order, four to a quadword, and the quadword here is
     * {NOP, NOP, FLUSHA, DIRECT(6)} -- FLUSHA and DIRECT in the same two slots the tested recipe puts
     * them in, with that recipe's MSKPATH3 slot left a NOP because this port does not mask (see the
     * header).  NOP is 0, so the first u64 being zero is a written value and not an absent one: the
     * gate fills the buffer with a sentinel and requires 0 there.
     *
     * DIRECT's immediate is the PACKET's quadword count and NOT the block's: the code quadword is
     * VIF1's own and never reaches the GIF, and a DIRECT that counted it would hand the GIF six
     * quadwords, decode five, and leave TRXDIR sitting in the FIFO for the next block to serve -- a
     * second request armed from someone else's bytes. */
    qw[0] = 0u;                                     /* GS_VIF1_NOP, GS_VIF1_NOP */
    qw[1] = (uint64_t)GS_VIF1_FLUSHA
          | ((uint64_t)GS_VIF1_DIRECT(GS_READ_REQ_QWC) << 32);

    return (int32_t)GS_READ_BLOCK_QWC;
}
