/*
 * src/apps/xmb_icons.h -- the icon atlas's sheet and its cell identities.
 *
 * Two programs have to agree on this and on nothing else: the bar, which samples
 * the atlas, and the host generator that fills it
 * (verify/tests/gen_xmb_atlas.c -> src/apps/xmb_atlas.h).  The glyphs' geometry is
 * evaluated only in the generator now, so it lives there; what is here is the
 * layout the sheet lands on, which both readers and writers must share.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */
#ifndef XMB_ICONS_H
#define XMB_ICONS_H

#define XB_ATLAS_GRID       6            /* 6x6 icon cells */
#define XB_ICON_CELL        64
#define XB_ICON_TEX         (XB_ATLAS_GRID * XB_ICON_CELL)
#define XB_ICON_CELLS       (XB_ATLAS_GRID * XB_ATLAS_GRID)

/* Icon atlas cells */
enum {
    IC_CAT_APPS = 0, IC_CAT_SETTINGS, IC_CAT_COMMANDS, IC_ARROW,
    IC_APP_TERM, IC_APP_EDITOR, IC_APP_PAINT, IC_APP_MUSIC, IC_APP_ORCHESTRA,
    IC_APP_VOBJ, IC_APP_TAD, IC_APP_DRIVE, IC_APP_CHAT, IC_APP_PHOTO,
    IC_GEAR, IC_SLIDER, IC_SPEAKER, IC_INFO, IC_PROMPT,
    IC_DROPLET, IC_FOLDER, IC_APP_QUAKE, IC_CLOCK, IC_BLANK,
    IC_MINIDISC, IC_FILE, IC_CAT_GAMES
};

/* The one arithmetic helper the bar and the glyph geometry both use.  Inline so
 * the generator, which has no other link, still rounds exactly like the bar. */
static inline float xb_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

#endif /* XMB_ICONS_H */
