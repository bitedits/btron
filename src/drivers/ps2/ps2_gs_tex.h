/*
 * ps2_gs_tex.h — a GL texture plane, planned into GS local memory
 *
 * Row 5, step 5b of the plan of record (doc/md/PS2.md, "The EE/GS backend").  TinyGL owns the
 * pixels -- an `upload` extent inside a plane allocated at that extent rounded up to a power of
 * two on each axis (`texture.c:111-153`) -- and the GS owns an address for the same bytes in its
 * own 4 MB.  This file is the arithmetic between the two, and nothing else: like the recorder it
 * computes words and never touches a register or the DMA, so every one of them is gated against a
 * hand literal on a host.
 *
 * Three laws decide the whole shape, all three measured rather than remembered:
 *
 *   The plane is what the GS addresses, not the upload.  `TEX0.TW`/`TH` are exponents and
 *   `TBW` is a count of 64-texel buffer widths, so a 384x384 upload into a 512x512 plane is
 *   described as 512x512; the padding is transparent black, so a coordinate that overshoots the
 *   app's own extent reads a texel rather than somebody else's page.
 *
 *   The bytes cross verbatim.  On this target the GL pixel domain is already canvas order --
 *   `zbuffer.h:64-75` and `BTRON_PIXEL_FB()` -- which is the order CT32 holds, so an upload is a
 *   quadword stream of `im->pixmap` and no conversion pass.
 *
 *   A texture switch is a mid-stream state write.  The environment packet is one per frame and
 *   the XMB binds the icon sheet and the font sheet inside the same frame, so `TEX0` cannot live
 *   only there: the three words go between the vertex groups, as a recorder AD group.
 *
 * Zero proprietary Sony SDK code: the field tables are in ps2_gs_pack.h and doc/md/PS2.md.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef PS2_GS_TEX_H
#define PS2_GS_TEX_H

#include <stdint.h>

/* How many planes the registry holds.  Two is what the XMB binds (icon atlas, font sheet); the
 * room is for a wallpaper-as-texture and the app that asks for it, because the alternative --
 * a full registry -- silently drops a plane and then draws it from a page nobody allocated. */
#define GS_TEX_MAX 8u

/* One texture plane as both sides can use it: the RDRAM bytes TinyGL allocated, the VRAM address
 * the GS reads them from, and the three words that point the first at the second. */
typedef struct {
    const void *plane;        /* the first byte of the GLImage's pixmap, the registry's key */
    uint32_t px_w, px_h;      /* the uploaded extent: what a texture coordinate maps onto */
    uint32_t pad_w, pad_h;    /* the allocated plane: each axis a power of two */

    uint32_t byte;            /* VRAM base offset, a whole GS_PAGE_TEX_PLANE */
    uint32_t tba;             /* TEX0.TBA and BITBLTBUF.DBA: 256-byte blocks */
    uint32_t tbw;             /* TEX0.TBW and BITBLTBUF.DBW: units of 64 texels */
    uint32_t tw_exp, th_exp;  /* TEX0.TW/TH: exponents of pad_w/pad_h */
    uint32_t qwc;             /* the plane's size in quadwords: pad_w*pad_h*4/16 */

    uint64_t tex0, tex1, clamp;   /* the three words, as gs_pack_tex0/tex1/clamp build them */

    int pending;              /* 1 until the bytes have crossed; see gs_tex_uploaded() */
} gs_tex_t;

/* The cursor's owner: one VRAM cursor, one registry, both here, so that a texture allocation and
 * a frame allocation cannot be handed the same addresses.  Call at boot, before any plane exists;
 * the cursor starts above the canvas exactly as gs_vram_init() says. */
void gs_tex_init(void);

/* Whoever owns DMAC channel 2 installs itself here, and every plane this file plans is handed to
 * it once, at the moment its bytes exist -- which is the only order that can work, because an
 * application uploads and draws in the same frame (`xmb.c:1193-1203` does) and the recorder sends
 * each full group to its own sink immediately, so a drain at frame end would put the primitive
 * stream that samples a plane ahead of the transfer that fills it.
 *
 * The sink is a pointer and not a call for the same reason the recorder's is: this file is
 * compiled into the host gate, where there is no bus to send to and no register to write, and a
 * build that installs nothing still plans, still allocates, and still gets every word right.
 * Passing 0 clears it.  An entry no installed sink has handed over keeps `pending` set, so
 * gs_tex_pending() reads 0 on a target and counts the planes on a host.
 *
 * `index` is this file's own registry slot, valid for the duration of the call: the sink takes the
 * bytes with gs_tex_at() and says it has with gs_tex_uploaded(). */
typedef void (*gs_tex_sink)(uint32_t index);
void gs_tex_install_sink(gs_tex_sink sink);

/* Register, or re-register, a plane of `px_w` x `px_h` uploaded pixels living in an
 * `pad_w` x `pad_h` allocation at `plane`.  Returns the entry, or 0 when the extent is not a
 * power-of-two pair the GS can name, when the plane pointer is 0, or when VRAM has no room for
 * it -- and in every one of those cases the cursor is untouched and the registry unchanged,
 * because a half-registered plane is a drawing from an address nobody owns.
 *
 * The plane pointer is the identity.  Same pointer, same padded geometry is `gl_image_alloc()`'s
 * "same plane, new contents" path (`texture.c:127-139`): the entry keeps its address and its
 * words and the bytes cross again.  Same pointer, *different* geometry is the C heap recycling a
 * freed plane's address, so the entry is re-planned -- it takes a fresh span and abandons the old
 * one, which `gs_tex_vram_left()` accounts for rather than hides, because a stale span would put
 * two planes in the same page. */
const gs_tex_t *gs_tex_register(const void *plane, uint32_t px_w, uint32_t px_h,
                                uint32_t pad_w, uint32_t pad_h);

/* The entry for a plane pointer, or 0.  This is what a draw call asks: "have these bytes been
 * given a VRAM address yet?"  A texture bound but never registered answers 0, and the caller
 * draws it untextured rather than from a stale page. */
const gs_tex_t *gs_tex_find(const void *plane);

/* The sink's own API: entries are indexed 0..gs_tex_count()-1 in registration order, and
 * gs_tex_uploaded() clears `pending` on the one whose bytes crossed. */
uint32_t gs_tex_count(void);
const gs_tex_t *gs_tex_at(uint32_t i);
void gs_tex_uploaded(uint32_t i);

/* The number of entries waiting for their bytes, which is what a present path logs: a frame that
 * uploads one plane costs one Host->Local DMA, and a frame that claims to have uploaded none while
 * `pending` is set says the drain was skipped. */
uint32_t gs_tex_pending(void);

/* Bytes of VRAM left once every registered plane has its address.  Same unit as gs_vram_left(). */
uint32_t gs_tex_vram_left(void);

#endif /* PS2_GS_TEX_H */
