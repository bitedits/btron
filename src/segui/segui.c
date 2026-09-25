/*
 * SegUI (Segmentation UI) Toolkit Core
 * src/segui/segui.c
 *
 * A lightweight, modular and scalable user interface toolkit for embedded
 * systems, home appliances, kiosks and retro machines.
 *
 * SegUI's premise is that a UI is not a pile of absolute coordinates but a
 * *budget*.  Every device screen is a fixed height, and that height is divided
 * into a short, ordered list of named segments (status, tab strip, title, body,
 * soft keys).  A device is therefore described by data - which segments it has,
 * how tall each is, and how the body segment divides its width - and never by a
 * conditional compilation branch.  The same widget code below paints a 480x800
 * handset, a 480x272 landscape appliance panel and a 640x480 retro desktop,
 * because each of those is only a different segment table.
 *
 * Three layers, each knowing the one below it only through plain data:
 *   1. SEGUI_PROFILE  which segments a device has, and its theme color table
 *   2. SEGUI_LAYOUT   the arithmetic that spends the budget on those segments
 *                     and then spends the body segment on the active tab
 *   3. widget draws   each receives an already-resolved RECT and knows nothing
 *                     about the device it is on
 *
 * Bounded state, zero post-boot heap: every structure is fixed-size and owned
 * by the caller, so this file needs no allocator and no libc.
 */

#include <btron/segui.h>
#include <btron/troncode.h>

/* The glyph cell of the built-in TRONCODE font: 8x16 ASCII, 16 px wide for
 * Kanji.  Every vertical centring here is expressed against it. */
#define SEGUI_FONT_W   8
#define SEGUI_FONT_H  16

/* ── Self-contained helpers (no libc: freestanding targets) ─────────────── */

static void segui_strl(char *dst, int cap, const char *src)
{
    int i = 0;
    if (!dst || cap <= 0) return;
    if (src) {
        for (; i < cap - 1 && src[i]; i++) dst[i] = src[i];
    }
    dst[i] = '\0';
}

static int rect_w(const RECT *r) { return (int)r->right - (int)r->left; }
static int rect_h(const RECT *r) { return (int)r->bottom - (int)r->top; }

static void fill_rect(GDEV *dev, const RECT *r, COLOR c)
{
    if (rect_w(r) > 0 && rect_h(r) > 0) fill_rec(dev, r, c);
}

/* DP's point and line primitives carry no colour in this kernel - set_col() is
 * a stub and drw_pnt()/drw_lin()/drw_rec()/drw_ovl() always paint black - so
 * every mark SegUI draws in the theme's own colour is built from filled
 * rectangles here.  A 1 px rect is the same write the primitive would do.
 */
static void dot(GDEV *dev, H x, H y, COLOR c)
{
    const RECT p = { x, y, (H)(x + 1), (H)(y + 1) };
    fill_rect(dev, &p, c);
}

static void hline(GDEV *dev, H x0, H x1, H y, COLOR c)
{
    const RECT r = { x0, y, (H)(x1 + 1), (H)(y + 1) };
    if (x1 < x0) return;
    fill_rect(dev, &r, c);
}

static void vline(GDEV *dev, H x, H y0, H y1, COLOR c)
{
    const RECT r = { x, y0, (H)(x + 1), (H)(y1 + 1) };
    if (y1 < y0) return;
    fill_rect(dev, &r, c);
}

static void seg(GDEV *dev, H x0, H y0, H x1, H y1, COLOR c)
{
    int dx = (int)x1 - (int)x0, dy = (int)y1 - (int)y0;
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    int err;

    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    err = dx - dy;

    for (;;) {
        int e2;
        dot(dev, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 = (H)(x0 + sx); }
        if (e2 <  dx) { err += dx; y0 = (H)(y0 + sy); }
    }
}

static void outline(GDEV *dev, const RECT *r, COLOR c)
{
    hline(dev, r->left, (H)(r->right - 1), r->top, c);
    hline(dev, r->left, (H)(r->right - 1), (H)(r->bottom - 1), c);
    vline(dev, r->left, r->top, (H)(r->bottom - 1), c);
    vline(dev, (H)(r->right - 1), r->top, (H)(r->bottom - 1), c);
}

/* The filled circle the ellipse primitive cannot give in a theme colour. */
static void disc_fill(GDEV *dev, const RECT *r, COLOR c)
{
    int x, y, w = rect_w(r), h = rect_h(r);
    long rr = (long)w * w * h * h;

    if (w < 1 || h < 1) return;
    for (y = r->top; y < r->bottom; y++) {
        for (x = r->left; x < r->right; x++) {
            long dx = 2 * (x - r->left) - w + 1;
            long dy = 2 * (y - r->top) - h + 1;
            if (dx * dx * h * h + dy * dy * w * w <= rr) dot(dev, (H)x, (H)y, c);
        }
    }
}

static void inset_rect(RECT *r, int dx, int dy)
{
    r->left   = (H)(r->left   + dx);
    r->top    = (H)(r->top    + dy);
    r->right  = (H)(r->right  - dx);
    r->bottom = (H)(r->bottom - dy);
}

/* Text vertically centred in `r`, drawn at x. */
static void draw_text(GDEV *dev, H x, const RECT *r, const char *s,
                      COLOR fg, COLOR bg)
{
    drw_tc_string(dev, x, (H)(r->top + (rect_h(r) - SEGUI_FONT_H) / 2),
                  s && s[0] ? s : " ", fg, bg);
}

static H text_w(const char *s)
{
    return s && s[0] ? tc_calc_string_width(s, SEGUI_TEXT_MAX) : 0;
}

static const SEGUI_THEME *theme_of(const SEGUI_APP *app)
{
    return app->theme ? app->theme : app->prof->theme;
}

void segui_draw_bevel(GDEV *dev, const RECT *r, COLOR hi, COLOR sh, int depth)
{
    int d, w = rect_w(r), h = rect_h(r);
    for (d = 0; d < depth; d++) {
        if (w - 2 * d <= 1 || h - 2 * d <= 1) break;
        hline(dev, (H)(r->left + d), (H)(r->right - d), (H)(r->top + d), hi);
        vline(dev, (H)(r->left + d), (H)(r->top + d), (H)(r->bottom - d), hi);
        hline(dev, (H)(r->left + d), (H)(r->right - d), (H)(r->bottom - 1 - d), sh);
        vline(dev, (H)(r->right - 1 - d), (H)(r->top + d), (H)(r->bottom - d), sh);
    }
}

/* ── Icon glyph set: 8x8 monochrome bitmaps owned by SegUI ────────────────
 * A target with no font engine still gets recognisable controls, and a target
 * with one can put a caption beside the icon anyway.
 */
static const UB ICON_BITS[SEGUI_ICON_COUNT][8] = {
    /* NONE     */ { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    /* BOLT     */ { 0x1C, 0x38, 0x70, 0xFC, 0x1C, 0x38, 0x70, 0x00 },
    /* MODULAR  */ { 0xE7, 0xE7, 0xE7, 0x00, 0xE7, 0xE7, 0xE7, 0x00 },
    /* SCALABLE */ { 0x00, 0x2A, 0x00, 0x2A, 0x00, 0x2A, 0x00, 0x00 },
    /* WINDOW   */ { 0xFF, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0xFF },
    /* INFO     */ { 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x18, 0x18 }
};

void segui_draw_icon(GDEV *dev, H x, H y, SEGUI_ICON icon, H size, COLOR col)
{
    const UB *bits;
    H cell, row, bit;

    if (icon <= SEGUI_ICON_NONE || icon >= SEGUI_ICON_COUNT || size < 4) return;
    bits = ICON_BITS[icon];
    cell = (H)(size / 8);
    if (cell < 1) cell = 1;

    for (row = 0; row < 8; row++) {
        if (!bits[row]) continue;
        for (bit = 0; bit < 8; bit++) {
            RECT px;
            if (!(bits[row] & (UB)(0x80 >> bit))) continue;
            px = (RECT){ (H)(x + bit * cell), (H)(y + row * cell),
                         (H)(x + (bit + 1) * cell), (H)(y + (row + 1) * cell) };
            fill_rect(dev, &px, col);
        }
    }
}

/* ── Themes: a look is a color table, so it too is data ─────────────────── */

const SEGUI_THEME segui_theme_handset = {
    "Handset",
    ARGB(0xFF, 0x10, 0x2A, 0x33),   /* chassis behind the screen edge        */
    ARGB(0xFF, 0xF2, 0xF5, 0xF8),   /* light content plate                   */
    ARGB(0xFF, 0xFF, 0xFF, 0xFF),
    ARGB(0xFF, 0x9A, 0xA8, 0xB6),
    ARGB(0xFF, 0x17, 0x23, 0x2E),   /* status / tab strip                    */
    ARGB(0xFF, 0xE8, 0xEE, 0xF6),
    ARGB(0xFF, 0x13, 0x6E, 0xE4),   /* touch blue                            */
    ARGB(0xFF, 0xE3, 0xE9, 0xF0),
    ARGB(0xFF, 0x10, 0x18, 0x20),
    ARGB(0xFF, 0x13, 0x6E, 0xE4),
    ARGB(0xFF, 0xFF, 0xFF, 0xFF),
    ARGB(0xFF, 0x10, 0x18, 0x20),
    ARGB(0xFF, 0x5A, 0x68, 0x76),
    ARGB(0xFF, 0xFF, 0xFF, 0xFF),
    ARGB(0xFF, 0x2E, 0xA0, 0x4F)    /* switch ON                             */
};

const SEGUI_THEME segui_theme_retro = {
    "Retro Gray",
    ARGB(0xFF, 0x00, 0x80, 0x80),   /* classic B-TRON teal desktop           */
    ARGB(0xFF, 0xD4, 0xD0, 0xC8),   /* chrome gray                           */
    ARGB(0xFF, 0xFF, 0xFF, 0xFF),
    ARGB(0xFF, 0x40, 0x40, 0x40),
    ARGB(0xFF, 0x00, 0x00, 0x80),   /* navy title bar                        */
    ARGB(0xFF, 0xFF, 0xFF, 0xFF),
    ARGB(0xFF, 0x00, 0x00, 0x80),
    ARGB(0xFF, 0xD4, 0xD0, 0xC8),
    ARGB(0xFF, 0x00, 0x00, 0x00),
    ARGB(0xFF, 0xB8, 0xBC, 0xC4),   /* a selected face sinks, it does not tint */
    ARGB(0xFF, 0x00, 0x00, 0x00),
    ARGB(0xFF, 0x00, 0x00, 0x00),
    ARGB(0xFF, 0x60, 0x60, 0x60),
    ARGB(0xFF, 0xFF, 0xFF, 0xFF),
    ARGB(0xFF, 0x00, 0x80, 0x00)
};

const SEGUI_THEME segui_theme_appliance = {
    "Appliance",
    ARGB(0xFF, 0x14, 0x18, 0x1C),
    ARGB(0xFF, 0x1E, 0x26, 0x2E),   /* dark front panel                      */
    ARGB(0xFF, 0x3C, 0x48, 0x54),
    ARGB(0xFF, 0x08, 0x0C, 0x10),
    ARGB(0xFF, 0x0C, 0x12, 0x18),
    ARGB(0xFF, 0x9F, 0xE0, 0x62),   /* phosphor telemetry                    */
    ARGB(0xFF, 0xF0, 0xA5, 0x00),   /* amber selection                       */
    ARGB(0xFF, 0x2A, 0x34, 0x40),
    ARGB(0xFF, 0xE6, 0xEE, 0xF6),
    ARGB(0xFF, 0xF0, 0xA5, 0x00),
    ARGB(0xFF, 0x14, 0x18, 0x1C),
    ARGB(0xFF, 0xE6, 0xEE, 0xF6),
    ARGB(0xFF, 0x8A, 0x96, 0xA2),
    ARGB(0xFF, 0x10, 0x16, 0x1C),
    ARGB(0xFF, 0x9F, 0xE0, 0x62)
};

/* ── Profiles: the device classes SegUI ships with ────────────────────────
 * Adding a device means adding a row to this table, not a branch elsewhere.
 * The order of `bands` IS the top-to-bottom stacking order.
 */

const SEGUI_PROFILE segui_profile_handset = {
    "Handset 480x800",
    480, 800,
    240, 400, 720, 1400,
    SEGP_PORTRAIT,
    { { SEG_STATUS,  30, FALSE },
      { SEG_TABBAR,  38, FALSE },
      { SEG_BODY,     0, TRUE  },
      { SEG_SOFTKEY, 34, FALSE } },
    4,
    0,      /* a handset screen has no decorative margin worth paying for  */
    8,
    120,    /* the body is tall, so buttons are big and thumb-sized        */
    36,     /* one list row                                                */
    180,    /* a grid column may not go below this width                   */
    2,      /* two columns: the 2x2 grid a phone shows                     */
    10,
    &segui_theme_handset,
    { "[選択]", "[切替]", "[戻る]" }
};

const SEGUI_PROFILE segui_profile_tft272 = {
    "TFT 480x272",
    480, 272,
    /* The panel class stops at 359 px tall: 360 and above is desktop space. */
    320, 160, 1024, 359,
    SEGP_TAB_BOX,
    /* No status band and a 24 px tab strip: on a 272 px panel the chrome
     * budget is the design constraint, so SegUI spends 54 px of 272 on it. */
    { { SEG_TABBAR,  24, FALSE },
      { SEG_BODY,     0, TRUE  },
      { SEG_SOFTKEY, 30, FALSE } },
    3,
    0,
    4,
    56,
    26,
    104,
    4,      /* a landscape panel tiles four across                         */
    4,
    &segui_theme_appliance,
    { "SET", "MODE", "BACK" }
};

const SEGUI_PROFILE segui_profile_retro = {
    "Retro 640x480",
    640, 480,
    560, 360, 2048, 1199,
    SEGP_WINDOWED | SEGP_TAB_BOX | SEGP_DOTTED_FOCUS,
    /* A desktop window is a title segment over a body segment.  The status
     * band and the tab strip are handset notions, so they simply are not in
     * this table - which is why one paint loop serves both. */
    { { SEG_TITLE,   26, FALSE },
      { SEG_BODY,     0, TRUE  } },
    2,
    28,
    6,
    40,
    24,
    120,
    2,
    6,
    &segui_theme_retro,
    { "", "", "" }
};

static const SEGUI_PROFILE *const PROFILES[] = {
    &segui_profile_handset,
    &segui_profile_tft272,
    &segui_profile_retro
};
#define N_PROFILES ((int)(sizeof(PROFILES) / sizeof(PROFILES[0])))

const SEGUI_PROFILE *segui_profile_for(H w, H h)
{
    int i;
    /* Orientation is part of eligibility: 640x480 sits inside the handset's
     * size box, and a phone layout on a desktop monitor is not a near miss.
     * With that test applied the windows are disjoint, so the first match is
     * the only match.  A screen outside every window still gets a layout. */
    for (i = 0; i < N_PROFILES; i++) {
        const SEGUI_PROFILE *p = PROFILES[i];
        BOOL portrait = (h > w);
        if ((p->flags & SEGP_PORTRAIT) ? !portrait : portrait) continue;
        if (w >= p->min_w && w <= p->max_w && h >= p->min_h && h <= p->max_h)
            return p;
    }
    return w > h ? &segui_profile_tft272 : &segui_profile_handset;
}

static int ci_prefix(const char *a, const char *b)
{
    int i;
    for (i = 0; b[i]; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return 0;
    }
    return 1;
}

const SEGUI_PROFILE *segui_profile_named(const char *name)
{
    int i;
    if (!name || !name[0]) return NULL;
    for (i = 0; i < N_PROFILES; i++)
        if (ci_prefix(PROFILES[i]->name, name)) return PROFILES[i];
    return NULL;
}

const SEGUI_THEME *segui_theme_named(const char *name)
{
    if (!name || !name[0]) return NULL;
    if (ci_prefix("Handset", name))     return &segui_theme_handset;
    if (ci_prefix("Retro", name))       return &segui_theme_retro;
    if (ci_prefix("Appliance", name))   return &segui_theme_appliance;
    return NULL;
}

/* ── Construction ───────────────────────────────────────────────────────── */

void segui_app_init(SEGUI_APP *app, const char *title, const SEGUI_PROFILE *prof)
{
    if (!app) return;
    app->prof = prof ? prof : &segui_profile_handset;
    app->theme = NULL;
    app->n_tab = 0;
    app->active_tab = 0;
    app->pressed_ctrl = -1;
    app->battery_pct = 80;
    segui_strl(app->title, (int)sizeof(app->title), title);
    app->clock[0] = '\0';
    app->radio[0] = '\0';
}

void segui_set_title(SEGUI_APP *app, const char *title)
{
    if (app && title) segui_strl(app->title, (int)sizeof(app->title), title);
}

void segui_set_clock(SEGUI_APP *app, const char *clock)
{
    if (app && clock) segui_strl(app->clock, (int)sizeof(app->clock), clock);
}

void segui_set_status(SEGUI_APP *app, const char *clock, const char *radio,
                      int battery_pct)
{
    if (!app) return;
    segui_set_clock(app, clock);
    if (radio) segui_strl(app->radio, (int)sizeof(app->radio), radio);
    if (battery_pct >= 0) app->battery_pct = battery_pct;
}

void segui_set_str(char *dst, int cap, const char *src)
{
    segui_strl(dst, cap, src);
}

void segui_set_peer(SEGUI_APP *app, SEGUI_CTRL *chat, const char *peer)
{
    (void)app;
    if (!chat || chat->type != SEGUI_W_CHAT) return;
    segui_strl(chat->peer, (int)sizeof(chat->peer), peer);
}

int segui_add_tab(SEGUI_APP *app, const char *caption, BOOL grid)
{
    SEGUI_TAB *t;
    if (!app || app->n_tab >= SEGUI_TAB_MAX) return -1;
    t = &app->tabs[app->n_tab];
    t->n_ctrl = 0;
    t->focus = 0;
    t->grid = grid ? TRUE : FALSE;
    segui_strl(t->caption, (int)sizeof(t->caption), caption);
    return app->n_tab++;
}

static SEGUI_CTRL *add_ctrl(SEGUI_APP *app, int tab, SEGUI_WTYPE type,
                            const char *caption, SEGUI_ICON icon,
                            void (*cb)(SEGUI_APP *, SEGUI_CTRL *))
{
    SEGUI_TAB *t;
    SEGUI_CTRL *c;

    if (!app || tab < 0 || tab >= app->n_tab) return NULL;
    t = &app->tabs[tab];
    if (t->n_ctrl >= SEGUI_CTRL_MAX) return NULL;

    c = &t->ctrl[t->n_ctrl++];
    c->type = type;
    c->icon = icon;
    c->state = 0;
    c->on_activate = cb;
    c->rows = NULL;
    c->n_row = 0;
    c->row_top = 0;
    c->row_focus = 0;
    c->msgs = NULL;
    c->n_msg = 0;
    c->peer[0] = '\0';
    c->box = (RECT){ 0, 0, 0, 0 };
    segui_strl(c->label, (int)sizeof(c->label), caption);
    return c;
}

SEGUI_CTRL *segui_add_button(SEGUI_APP *app, int tab, const char *caption,
                             SEGUI_ICON icon,
                             void (*cb)(SEGUI_APP *, SEGUI_CTRL *))
{
    return add_ctrl(app, tab, SEGUI_W_BUTTON, caption, icon, cb);
}

SEGUI_CTRL *segui_add_check(SEGUI_APP *app, int tab, const char *caption,
                            BOOL init, void (*cb)(SEGUI_APP *, SEGUI_CTRL *))
{
    SEGUI_CTRL *c = add_ctrl(app, tab, SEGUI_W_CHECK, caption, SEGUI_ICON_NONE, cb);
    if (c) c->state = SEGUI_ST_LATCHED | (init ? SEGUI_ST_SELECTED : 0u);
    return c;
}

SEGUI_CTRL *segui_add_toggle(SEGUI_APP *app, int tab, const char *caption,
                             BOOL init, void (*cb)(SEGUI_APP *, SEGUI_CTRL *))
{
    SEGUI_CTRL *c = add_ctrl(app, tab, SEGUI_W_TOGGLE, caption, SEGUI_ICON_NONE, cb);
    if (c) c->state = SEGUI_ST_LATCHED | (init ? SEGUI_ST_SELECTED : 0u);
    return c;
}

SEGUI_CTRL *segui_add_label(SEGUI_APP *app, int tab, const char *caption)
{
    return add_ctrl(app, tab, SEGUI_W_LABEL, caption, SEGUI_ICON_NONE, NULL);
}

SEGUI_CTRL *segui_add_list(SEGUI_APP *app, int tab, SEGUI_ROW *rows, int n_row)
{
    SEGUI_CTRL *c = add_ctrl(app, tab, SEGUI_W_LIST, NULL, SEGUI_ICON_NONE, NULL);
    if (!c) return NULL;
    if (n_row > SEGUI_ROW_MAX) n_row = SEGUI_ROW_MAX;
    c->rows = rows;
    c->n_row = n_row;
    return c;
}

SEGUI_CTRL *segui_add_chat(SEGUI_APP *app, int tab, SEGUI_MSG *msgs, int n_msg,
                           const char *peer)
{
    SEGUI_CTRL *c = add_ctrl(app, tab, SEGUI_W_CHAT, NULL, SEGUI_ICON_NONE, NULL);
    if (!c) return NULL;
    if (n_msg > SEGUI_MSG_MAX) n_msg = SEGUI_MSG_MAX;
    c->msgs = msgs;
    c->n_msg = n_msg;
    segui_strl(c->peer, (int)sizeof(c->peer), peer);
    return c;
}

/* ── Segment resolution ──────────────────────────────────────────────────
 * The fixed bands are paid for out of the screen height first and whatever is
 * left belongs to the stretch band.  When a panel is too short even for the
 * fixed bands they shrink by the same ratio instead of being clipped, so a
 * segment keeps its identity and its relative share.
 */
static void resolve_segments(SEGUI_APP *app, GDEV *dev)
{
    const SEGUI_PROFILE *p = app->prof;
    SEGUI_LAYOUT *L = &app->lay;
    int i, y, fixed_sum = 0, avail;

    L->prof = p;
    L->outer = (RECT){ p->margin, p->margin,
                       (H)(dev->width - p->margin), (H)(dev->height - p->margin) };
    if (L->outer.right <= L->outer.left)  L->outer.right  = (H)(L->outer.left + 1);
    if (L->outer.bottom <= L->outer.top)  L->outer.bottom = (H)(L->outer.top + 1);

    for (i = 0; i < SEG_ID_COUNT; i++) {
        L->band_used[i] = FALSE;
        L->band[i] = (RECT){ 0, 0, 0, 0 };
    }

    for (i = 0; i < p->n_bands; i++)
        if (!p->bands[i].stretch) fixed_sum += p->bands[i].height;

    avail = rect_h(&L->outer);

    y = L->outer.top;
    for (i = 0; i < p->n_bands; i++) {
        SEG_ID id = p->bands[i].id;
        int left = avail - (y - L->outer.top);
        int hgt  = p->bands[i].height;

        if (left <= 0) break;
        if (p->bands[i].stretch) {
            hgt = avail - fixed_sum;
            if (hgt > left) hgt = left;
        } else if (avail < fixed_sum) {
            hgt = hgt * avail / fixed_sum;
            if (hgt > left) hgt = left;
        }
        if (hgt < 6) hgt = 6;
        if (hgt > left) hgt = left;

        L->band[id] = (RECT){ L->outer.left, (H)y, L->outer.right, (H)(y + hgt) };
        L->band_used[id] = TRUE;
        y += hgt;
    }

    if (L->band_used[SEG_BODY]) {
        L->body = L->band[SEG_BODY];
        inset_rect(&L->body, p->pad, p->pad);
    } else {
        L->body = (RECT){ 0, 0, 0, 0 };
    }
}

H segui_row_h(const SEGUI_APP *app)
{
    int h = app->prof->row_h;
    if (h < 20) h = 20;
    if (h > 48) h = 48;
    return (H)h;
}

static BOOL is_container(const SEGUI_CTRL *c)
{
    return c->type == SEGUI_W_LIST || c->type == SEGUI_W_CHAT;
}

/* ── Body budget: labels, then a container or a grid plus stacked rows ───
 * A list or chat widget takes the whole body under the labels - that is what
 * makes a phone screen one widget per tab.  Otherwise the content flows from
 * the top: the grid is sized by the profile's preferred button height and is
 * never stretched, the stacked rows follow it at their own pitch, and whatever
 * is left stays at the bottom of the body.  A 272 px panel shows the same tab
 * as four narrow columns because a column may not go below min_col_w.
 */
static void resolve_body(SEGUI_APP *app)
{
    const SEGUI_PROFILE *p = app->prof;
    SEGUI_LAYOUT *L = &app->lay;
    SEGUI_TAB *t = &app->tabs[app->active_tab];
    int i, n_btn = 0, n_row_w = 0, n_lbl = 0, container = -1;
    int cols, rows, y, gap = p->gap, grid_h, cell_w, cell_h, lbl_h, row_h;
    int row_block, room;

    for (i = 0; i < t->n_ctrl; i++) {
        const SEGUI_CTRL *c = &t->ctrl[i];
        if (is_container(c)) { if (container < 0) container = i; continue; }
        switch (c->type) {
        case SEGUI_W_LABEL:  n_lbl++;  break;
        case SEGUI_W_CHECK:
        case SEGUI_W_TOGGLE: n_row_w++; break;
        default:             n_btn++;  break;
        }
    }

    lbl_h = SEGUI_FONT_H + 4;
    row_h = segui_row_h(app);
    y = L->body.top;

    L->cols = 1; L->cell_w = (H)rect_w(&L->body);
    L->grid_rows = 0; L->cell_h = 0;
    L->grid = (RECT){ L->body.left, (H)y, L->body.right, (H)y };

    /* 1. Labels claim their lines from the top. */
    for (i = 0; i < t->n_ctrl; i++) {
        SEGUI_CTRL *c = &t->ctrl[i];
        if (c->type != SEGUI_W_LABEL) continue;
        c->box = (RECT){ L->body.left, (H)y, L->body.right, (H)(y + lbl_h) };
        y += lbl_h + (gap > 4 ? 4 : gap);
    }

    if (container >= 0) {
        SEGUI_CTRL *c = &t->ctrl[container];
        c->box = (RECT){ L->body.left, (H)y, L->body.right, L->body.bottom };
        if (rect_h(&c->box) < row_h) c->box.bottom = (H)(c->box.top + row_h);
        return;
    }

    /* 2. The grid and the stacked rows flow from the top: the grid gets the
     *    profile's button height, the rows take their own pitch under it, and
     *    the leftover falls to the bottom of the body.  The rows are measured
     *    first so a tall screen cannot let the grid push them off it. */
    row_block = n_row_w * (row_h + gap / 2);
    room = L->body.bottom - y - row_block;
    if (room < 0) room = 0;

    cols = n_btn > 0 ? n_btn : 1;
    if (!t->grid) cols = 1;
    if (p->max_cols > 0 && cols > p->max_cols) cols = p->max_cols;
    while (cols > 1 && (rect_w(&L->body) - (cols - 1) * gap) / cols < p->min_col_w)
        cols--;

    rows = (n_btn + cols - 1) / cols;
    if (rows < 1) rows = 1;
    while (rows > 1 && (room - (rows - 1) * gap) / rows < 24) {
        cols++;
        rows = (n_btn + cols - 1) / cols;
    }

    cell_w = (rect_w(&L->body) - (cols - 1) * gap) / cols;
    cell_h = n_btn > 0 ? (room - (rows - 1) * gap) / rows : 0;
    if (cell_h > p->btn_h) cell_h = p->btn_h;
    if (n_btn > 0 && cell_h < 24) cell_h = room > 24 ? 24 : room;
    grid_h = n_btn > 0 ? rows * cell_h + (rows - 1) * gap : 0;

    L->cols = (H)cols;
    L->cell_w = (H)cell_w;
    L->grid_rows = (H)rows;
    L->cell_h = (H)cell_h;
    L->grid = (RECT){ L->body.left, (H)y, L->body.right, (H)(y + grid_h) };

    {
        int k = 0, ry = y + grid_h + gap;
        for (i = 0; i < t->n_ctrl; i++) {
            SEGUI_CTRL *c = &t->ctrl[i];
            int bx, by;
            if (c->type == SEGUI_W_LABEL || c->type == SEGUI_W_CHECK ||
                c->type == SEGUI_W_TOGGLE) continue;
            bx = L->grid.left + (k % cols) * (cell_w + gap);
            by = L->grid.top  + (k / cols) * (cell_h + gap);
            c->box = (RECT){ (H)bx, (H)by, (H)(bx + cell_w), (H)(by + cell_h) };
            k++;
        }
        for (i = 0; i < t->n_ctrl; i++) {
            SEGUI_CTRL *c = &t->ctrl[i];
            if (c->type != SEGUI_W_CHECK && c->type != SEGUI_W_TOGGLE) continue;
            c->box = (RECT){ L->body.left, (H)ry, L->body.right, (H)(ry + row_h) };
            ry += row_h + gap / 2;
        }
    }
    (void)n_lbl;
}

void segui_layout(SEGUI_APP *app, GDEV *dev)
{
    if (!app || !dev || app->n_tab <= 0) return;
    if (app->active_tab >= app->n_tab) app->active_tab = app->n_tab - 1;
    resolve_segments(app, dev);
    resolve_body(app);
}

/* ── Widget painting ────────────────────────────────────────────────────── */

static BOOL is_on(const SEGUI_CTRL *c) { return (c->state & SEGUI_ST_SELECTED) ? TRUE : FALSE; }

void segui_draw_focus(GDEV *dev, const RECT *r, const SEGUI_THEME *th, UW flags)
{
    int x, y;
    RECT o = *r;
    inset_rect(&o, -2, -2);

    if (flags & SEGP_DOTTED_FOCUS) {
        /* A dotted rectangle reads on any face colour without knowing what is
         * under it, which is why the desktop look asks for it. */
        for (x = o.left; x < o.right; x++) {
            if ((x & 1) == 0) dot(dev, (H)x, o.top, th->text);
            if ((x & 1) == 1) dot(dev, (H)x, (H)(o.bottom - 1), th->text);
        }
        for (y = o.top; y < o.bottom; y++) {
            if ((y & 1) == 0) dot(dev, o.left, (H)y, th->text);
            if ((y & 1) == 1) dot(dev, (H)(o.right - 1), (H)y, th->text);
        }
        return;
    }
    outline(dev, &o, th->accent);
    inset_rect(&o, -1, -1);
    outline(dev, &o, th->accent);
}

void segui_draw_button(GDEV *dev, const RECT *r, const SEGUI_CTRL *c,
                       const SEGUI_THEME *th, BOOL focused)
{
    BOOL pressed = (c->state & SEGUI_ST_PRESSED) ? TRUE : FALSE;
    COLOR face = pressed ? th->btn_sel_face : th->btn_face;
    COLOR fg   = (c->state & SEGUI_ST_DISABLED) ? th->text_dim
             : (pressed ? th->btn_sel_fg : th->btn_fg);
    int depth = rect_h(r) > 2 * SEGUI_FONT_H ? 3 : 2;
    int tw = text_w(c->label), x;
    RECT text_r;

    fill_rect(dev, r, face);
    /* A press sinks the bevel: the same two colors, exchanged. */
    if (pressed || (is_on(c) && th->btn_sel_face != th->btn_face))
        segui_draw_bevel(dev, r, th->frame_sh, th->frame_hi, depth);
    else
        segui_draw_bevel(dev, r, th->frame_hi, th->frame_sh, depth);

    if (c->icon && rect_w(r) > SEGUI_FONT_W * 4) {
        /* Icon over caption: the tall touch button and the desktop shortcut
         * are the same composition, only the pitch differs.  The pair is
         * centred as one group, so a short button cannot overlap the two. */
        H sz = (H)(SEGUI_FONT_H + (rect_h(r) >= 64 ? 16 : 0));
        int group = sz + 4 + SEGUI_FONT_H;
        int gy = r->top + (rect_h(r) - group) / 2;

        x = r->left + (rect_w(r) - tw) / 2;
        if (!c->label[0]) {
            segui_draw_icon(dev, (H)(r->left + (rect_w(r) - sz) / 2),
                            (H)(r->top + (rect_h(r) - sz) / 2), c->icon, sz, fg);
        } else if (rect_h(r) >= group) {
            RECT cr;
            segui_draw_icon(dev, (H)(r->left + (rect_w(r) - sz) / 2), (H)gy,
                            c->icon, sz, fg);
            cr = (RECT){ (H)x, (H)(gy + sz + 4), (H)(x + tw),
                         (H)(gy + sz + 4 + SEGUI_FONT_H) };
            draw_text(dev, (H)x, &cr, c->label, fg, face);
        } else {
            /* Too short for both: the caption is what identifies a button. */
            text_r = (RECT){ (H)x, r->top, (H)(x + tw), r->bottom };
            draw_text(dev, (H)x, &text_r, c->label, fg, face);
        }
    } else {
        x = r->left + (rect_w(r) - tw) / 2;
        text_r = (RECT){ (H)x, r->top, (H)(x + tw), r->bottom };
        draw_text(dev, (H)x, &text_r, c->label, fg, face);
    }

    if (is_on(c) && !pressed && rect_h(r) >= 48) {
        /* Selected tile keeps a marker line so state survives a repaint of an
         * unfocused grid. */
        hline(dev, (H)(r->left + 10), (H)(r->right - 10),
              (H)(r->bottom - 6), th->accent);
    }
    if (focused) segui_draw_focus(dev, r, th, 0);
}

/* The tick is geometry, not a glyph, so no font coverage is assumed. */
static void draw_tick(GDEV *dev, const RECT *well, COLOR c)
{
    H x = well->left, y = well->top, w = (H)(well->right - well->left);
    H h = (H)(well->bottom - well->top);
    seg(dev, (H)(x + 3), (H)(y + h / 2 + 1), (H)(x + w / 2 - 1), (H)(y + h - 4), c);
    seg(dev, (H)(x + w / 2 - 1), (H)(y + h - 4), (H)(x + w - 3), (H)(y + 3), c);
}

void segui_draw_check(GDEV *dev, const RECT *r, const SEGUI_CTRL *c,
                      const SEGUI_THEME *th, BOOL focused)
{
    H box = SEGUI_FONT_H;
    RECT well = { (H)(r->left + 4), (H)(r->top + (rect_h(r) - box) / 2),
                  (H)(r->left + 4 + box), (H)(r->top + (rect_h(r) + box) / 2) };
    RECT tr = { (H)(well.right + 8), r->top, r->right, r->bottom };

    fill_rect(dev, &well, th->field_bg);
    segui_draw_bevel(dev, &well, th->frame_sh, th->frame_hi, 1);
    if (is_on(c)) {
        draw_tick(dev, &well, th->accent);
    }
    draw_text(dev, tr.left, &tr, c->label, th->text, th->panel_bg);
    if (focused) segui_draw_focus(dev, r, th, SEGP_DOTTED_FOCUS);
}

void segui_draw_toggle(GDEV *dev, const RECT *r, const SEGUI_CTRL *c,
                       const SEGUI_THEME *th, BOOL focused)
{
    BOOL on = is_on(c);
    H track_h = (H)(rect_h(r) - 8);
    H track_w, k;
    RECT track, knob;
    RECT tr;

    if (track_h < 12) track_h = 12;
    if (track_h > 26) track_h = 26;
    track_w = (H)(track_h * 2 + 2);
    track = (RECT){ (H)(r->right - 4 - track_w), (H)(r->top + (rect_h(r) - track_h) / 2),
                    (H)(r->right - 4), (H)0 };
    track.bottom = (H)(track.top + track_h);
    k = (H)(track_h - 4);

    fill_rect(dev, &track, on ? th->toggle_on : th->field_bg);
    segui_draw_bevel(dev, &track, th->frame_sh, th->frame_hi, 1);

    knob = (RECT){ (H)(on ? track.right - 2 - k : track.left + 2),
                   (H)(track.top + 2), 0, 0 };
    knob.right = (H)(knob.left + k);
    knob.bottom = (H)(track.bottom - 2);
    fill_rect(dev, &knob, th->btn_face);
    segui_draw_bevel(dev, &knob, th->frame_hi, th->frame_sh, 2);

    tr = (RECT){ r->left, r->top, (H)(track.left - 10), r->bottom };
    draw_text(dev, tr.left, &tr, c->label, th->text, th->panel_bg);
    if (focused) segui_draw_focus(dev, &track, th, SEGP_DOTTED_FOCUS);
}

void segui_draw_label_text(GDEV *dev, const RECT *r, const SEGUI_CTRL *c,
                           const SEGUI_THEME *th)
{
    draw_text(dev, (H)(r->left + 2), r, c->label, th->text_dim, th->panel_bg);
}

/* ── Rows: one list entry, whatever screen it is on ───────────────────── */

/* A forward marker drawn from three lines, so no glyph coverage is assumed. */
static void ay_forward(GDEV *dev, H x, H y, COLOR c)
{
    seg(dev, x, (H)(y - 4), (H)(x + 4), y, c);
    seg(dev, (H)(x + 4), y, x, (H)(y + 4), c);
}

void segui_draw_row(GDEV *dev, const RECT *r, const SEGUI_ROW *row, int row_h,
                    BOOL focused, const SEGUI_THEME *th, UW flags)
{
    COLOR bg = focused ? th->btn_sel_face : th->panel_bg;
    COLOR fg = focused ? th->btn_sel_fg   : th->text;
    COLOR dim = focused ? th->btn_sel_fg  : th->text_dim;
    int tw;
    RECT tr;

    if (row_h < 6) return;
    fill_rect(dev, r, bg);

    if (row->type == SEGUI_ROW_HEADER) {
        RECT hr = { r->left, r->top, r->right, (H)(r->top + row_h) };
        fill_rect(dev, &hr, th->band_bg);
        hline(dev, hr.left, (H)(hr.right - 1), (H)(hr.bottom - 1), th->accent);
        draw_text(dev, (H)(hr.left + 8), &hr, row->caption, th->band_fg, th->band_bg);
        return;
    }

    tw = text_w(row->caption);
    tr = (RECT){ r->left, r->top, r->right, (H)(r->top + row_h) };

    if (row->type == SEGUI_ROW_PERSON) {
        /* An initial disc stands in for an avatar: one circle and one glyph
         * cost nothing and every contacts screen on a small phone needs one. */
        H d = (H)(row_h - 8);
        RECT disc = { (H)(r->left + 6), (H)(r->top + 4),
                      (H)(r->left + 6 + d), (H)(r->top + 4 + d) };
        COLOR disc_bg = focused ? th->btn_sel_fg   : th->band_bg;
        COLOR disc_fg = focused ? th->btn_sel_face : th->band_fg;
        disc_fill(dev, &disc, disc_bg);
        {
            RECT ir = { (H)(disc.left + d / 2 - SEGUI_FONT_W / 2), disc.top,
                        (H)(disc.left + d / 2 + SEGUI_FONT_W / 2), disc.bottom };
            char initial[2];
            initial[0] = row->caption[0] ? row->caption[0] : '?';
            initial[1] = '\0';
            drw_tc_string(dev, ir.left, (H)(disc.top + (d - SEGUI_FONT_H) / 2),
                          initial, disc_fg, disc_bg);
        }
        tr.left = (H)(disc.right + 8);
    } else {
        tr.left = (H)(r->left + 10);
    }

    {
        RECT cap = tr;
        cap.right = (H)(tr.left + tw);
        draw_text(dev, tr.left, &cap, row->caption, fg, bg);
    }

    switch (row->type) {
    case SEGUI_ROW_VALUE: {
        int vw = text_w(row->value);
        RECT vr = { (H)(r->right - 10 - vw), r->top, (H)(r->right - 10),
                    (H)(r->top + row_h) };
        draw_text(dev, vr.left, &vr, row->value, dim, bg);
        break;
    }
    case SEGUI_ROW_ARROW: {
        /* The value carries the count ("Work  9"), so an arrow row reads as a
         * screen that has something behind it rather than a bare command. */
        H ax = (H)(r->right - 16);
        if (row->value[0]) {
            int vw = text_w(row->value);
            RECT vr = { (H)(ax - 8 - vw), r->top, (H)(ax - 8),
                        (H)(r->top + row_h) };
            draw_text(dev, vr.left, &vr, row->value, dim, bg);
        }
        ay_forward(dev, ax, (H)(r->top + row_h / 2), focused ? th->btn_sel_fg : th->text_dim);
        break;
    }
    case SEGUI_ROW_TOGGLE: {
        BOOL on = (row->state & SEGUI_ST_SELECTED) ? TRUE : FALSE;
        int th_h = row_h - 10, ty = r->top + 5;
        RECT track = { (H)(r->right - 8 - (th_h * 2)), (H)ty,
                       (H)(r->right - 8), (H)(ty + th_h) };
        RECT knob = { (H)(on ? track.right - 3 - (th_h - 4) : track.left + 3),
                      (H)(ty + 3), 0, 0 };
        knob.right = (H)(knob.left + (th_h - 4));
        knob.bottom = (H)(track.bottom - 3);
        if (th_h < 10) break;
        fill_rect(dev, &track, on ? th->toggle_on : th->field_bg);
        fill_rect(dev, &knob, th->btn_face);
        segui_draw_bevel(dev, &track, th->frame_sh, th->frame_hi, 1);
        break;
    }
    default:
        break;
    }

    if (!focused)
        hline(dev, (H)(r->left + 8), (H)(r->right - 8), (H)(r->bottom - 1),
              th->frame_sh);
    if (focused && (flags & SEGP_DOTTED_FOCUS))
        segui_draw_focus(dev, r, th, flags);
}

int segui_list_rows_visible(const SEGUI_CTRL *c, const SEGUI_APP *app)
{
    int row_h = segui_row_h(app);
    int n = row_h > 0 ? rect_h(&c->box) / row_h : 0;
    return n < 1 ? 1 : n;
}

void segui_draw_chat(GDEV *dev, const SEGUI_CTRL *c, const SEGUI_THEME *th)
{
    int i, y, top, row_h = SEGUI_FONT_H + 8;
    int box_w = rect_w(&c->box);
    int max_bubble = box_w * 3 / 4;

    if (box_w < 40 || rect_h(&c->box) < row_h) return;

    y = top = c->box.top;
    if (c->peer[0]) {
        RECT hr = { c->box.left, (H)y, c->box.right, (H)(y + row_h) };
        fill_rect(dev, &hr, th->band_bg);
        draw_text(dev, (H)(hr.left + 8), &hr, c->peer, th->band_fg, th->band_bg);
        y += row_h + 2;
        top = y;
    }

    /* Newest at the bottom, like every phone log; old bubbles that do not fit
     * fall off the top rather than the bottom, so the latest is always seen. */
    for (i = c->n_msg - 1, y = c->box.bottom; i >= 0; i--) {
        const SEGUI_MSG *m = &c->msgs[i];
        int tw = text_w(m->text);
        RECT bub;
        if (tw > max_bubble - 12) tw = max_bubble - 12;
        if (y - row_h < top) break;
        y -= row_h;
        bub.top = (H)y;
        bub.bottom = (H)(y + row_h - 2);
        if (m->who == SEGUI_MSG_ME) {
            bub.left = (H)(c->box.right - 6 - tw - 12);
            bub.right = (H)(c->box.right - 6);
        } else {
            bub.left = (H)(c->box.left + 6);
            bub.right = (H)(bub.left + tw + 12);
        }
        if (m->who == SEGUI_MSG_NOTE) {
            RECT nr = { c->box.left, bub.top, c->box.right, bub.bottom };
            int nw = text_w(m->text);
            draw_text(dev, (H)(c->box.left + (box_w - nw) / 2), &nr, m->text,
                      th->text_dim, th->panel_bg);
            continue;
        }
        fill_rect(dev, &bub, m->who == SEGUI_MSG_ME ? th->accent : th->btn_face);
        segui_draw_bevel(dev, &bub, th->frame_hi, th->frame_sh, 1);
        {
            RECT tr = { (H)(bub.left + 6), bub.top, (H)(bub.left + 6 + tw),
                        bub.bottom };
            draw_text(dev, tr.left, &tr, m->text,
                      m->who == SEGUI_MSG_ME ? th->btn_sel_fg : th->btn_fg,
                      m->who == SEGUI_MSG_ME ? th->accent : th->btn_face);
        }
    }
}

/* ── Segment painting ───────────────────────────────────────────────────── */

int segui_battery_bars(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return (pct * 4 + 99) / 100;
}

static void paint_status(SEGUI_APP *app, GDEV *dev, const SEGUI_THEME *th)
{
    const RECT *b = &app->lay.band[SEG_STATUS];
    int i, bars = segui_battery_bars(app->battery_pct);
    H bh = 11, bw = 22, bx, by;
    RECT cell, cap;

    fill_rect(dev, b, th->band_bg);
    hline(dev, b->left, (H)(b->right - 1), (H)(b->bottom - 1), th->accent);

    draw_text(dev, (H)(b->left + 8), b, app->clock, th->band_fg, th->band_bg);
    {
        int rw = text_w(app->radio);
        RECT tr = { (H)(b->right - 40 - rw), b->top, (H)(b->right - 40), b->bottom };
        draw_text(dev, tr.left, &tr, app->radio, th->band_fg, th->band_bg);
    }

    bx = (H)(b->right - 32);
    by = (H)(b->top + (rect_h(b) - bh) / 2);
    cell = (RECT){ bx, by, (H)(bx + bw), (H)(by + bh) };
    cap  = (RECT){ cell.right, (H)(by + 3), (H)(cell.right + 2), (H)(by + bh - 3) };
    fill_rect(dev, &cell, th->field_bg);
    outline(dev, &cell, th->band_fg);
    fill_rect(dev, &cap, th->band_fg);
    for (i = 0; i < bars; i++) {
        RECT seg = { (H)(cell.left + 2 + i * 5), (H)(cell.top + 2),
                     (H)(cell.left + 6 + i * 5), (H)(cell.bottom - 2) };
        fill_rect(dev, &seg, th->accent);
    }
}

static void paint_tabbar(SEGUI_APP *app, GDEV *dev, const SEGUI_THEME *th)
{
    const RECT *b = &app->lay.band[SEG_TABBAR];
    int i, cell = rect_w(b) / (app->n_tab > 0 ? app->n_tab : 1);
    UW flags = app->prof->flags;

    fill_rect(dev, b, th->band_bg);
    hline(dev, b->left, (H)(b->right - 1), (H)(b->bottom - 1), th->frame_sh);

    for (i = 0; i < app->n_tab; i++) {
        RECT cr = { (H)(b->left + i * cell), b->top,
                    (H)(b->left + (i + 1) * cell), b->bottom };
        BOOL active = (i == app->active_tab);
        COLOR bg = th->band_bg, fg = th->band_fg;

        if (flags & SEGP_TAB_BOX) {
            if (active) { bg = th->panel_bg; fg = th->text; }
            fill_rect(dev, &cr, bg);
            if (active)
                segui_draw_bevel(dev, &cr, th->frame_hi, th->frame_sh, 1);
        } else if (active) {
            RECT under = { (H)(cr.left + 8), (H)(cr.bottom - 4),
                           (H)(cr.right - 8), cr.bottom };
            fill_rect(dev, &under, th->accent);
            fg = th->accent;
        }
        {
            int tw = text_w(app->tabs[i].caption);
            RECT t = cr;
            draw_text(dev, (H)(cr.left + (cell - tw) / 2), &t,
                      app->tabs[i].caption, fg, bg);
        }
        if (!(flags & SEGP_TAB_BOX) && i < app->n_tab - 1)
            vline(dev, (H)(cr.right - 1), (H)(b->top + 6), (H)(b->bottom - 7),
                  th->frame_sh);
    }
}

/* Caption buttons of a desktop window: minimise, maximise, close - drawn as
 * geometry so they need no glyph coverage beyond ASCII. */
static void paint_caption_buttons(GDEV *dev, const RECT *b, const SEGUI_THEME *th)
{
    int i, bw = 18, bh = 14;
    for (i = 0; i < SEGUI_SOFTKEY_MAX; i++) {
        RECT br = { (H)(b->right - (SEGUI_SOFTKEY_MAX - i) * (bw + 2) - 3),
                    (H)(b->top + (rect_h(b) - bh) / 2), 0, 0 };
        H x0, y0;
        br.right = (H)(br.left + bw);
        br.bottom = (H)(br.top + bh);
        fill_rect(dev, &br, th->btn_face);
        segui_draw_bevel(dev, &br, th->frame_hi, th->frame_sh, 1);

        x0 = (H)(br.left + bw / 2);
        y0 = (H)(br.top + bh / 2);
        if (i == 0) {
            hline(dev, (H)(x0 - 3), (H)(x0 + 3), (H)(br.bottom - 4), th->btn_fg);
        } else if (i == 1) {
            RECT s = { (H)(x0 - 3), (H)(y0 - 3), (H)(x0 + 4), (H)(y0 + 4) };
            outline(dev, &s, th->btn_fg);
        } else {
            seg(dev, (H)(x0 - 3), (H)(y0 - 3), (H)(x0 + 3), (H)(y0 + 3), th->btn_fg);
            seg(dev, (H)(x0 - 3), (H)(y0 + 3), (H)(x0 + 3), (H)(y0 - 3), th->btn_fg);
        }
    }
}

static void paint_title(SEGUI_APP *app, GDEV *dev, const SEGUI_THEME *th)
{
    const RECT *b = &app->lay.band[SEG_TITLE];

    fill_rect(dev, b, th->band_bg);
    draw_text(dev, (H)(b->left + 6), b, app->title, th->band_fg, th->band_bg);
    if (app->prof->flags & SEGP_WINDOWED) {
        paint_caption_buttons(dev, b, th);
    } else {
        int pw = text_w(app->prof->name);
        RECT r = { (H)(b->right - 6 - pw), b->top, (H)(b->right - 6), b->bottom };
        draw_text(dev, r.left, &r, app->prof->name, th->band_fg, th->band_bg);
    }
}

static void paint_softkeys(SEGUI_APP *app, GDEV *dev, const SEGUI_THEME *th)
{
    const RECT *b = &app->lay.band[SEG_SOFTKEY];
    int i, gap = 4;
    int cell = (rect_w(b) - gap * (SEGUI_SOFTKEY_MAX + 1)) / SEGUI_SOFTKEY_MAX;

    hline(dev, b->left, (H)(b->right - 1), b->top, th->accent);
    for (i = 0; i < SEGUI_SOFTKEY_MAX; i++) {
        RECT k = { (H)(b->left + gap + i * (cell + gap)), (H)(b->top + 5),
                   (H)(b->left + gap + (i + 1) * cell + i * gap),
                   (H)(b->bottom - 5) };
        const char *lbl = app->prof->softkeys[i];
        int lw = text_w(lbl);
        fill_rect(dev, &k, th->btn_face);
        segui_draw_bevel(dev, &k, th->frame_hi, th->frame_sh, 2);
        if (lbl && lbl[0]) {
            RECT t = k;
            draw_text(dev, (H)(k.left + (rect_w(&k) - lw) / 2), &t, lbl,
                      th->btn_fg, th->btn_face);
        }
    }
}

void segui_paint_tabbar(SEGUI_APP *app, GDEV *dev)
{
    if (app && dev && app->lay.band_used[SEG_TABBAR])
        paint_tabbar(app, dev, theme_of(app));
}

void segui_paint_segment(SEGUI_APP *app, GDEV *dev, SEG_ID id)
{
    const SEGUI_THEME *th;
    if (!app || !dev || id >= SEG_ID_COUNT || id == SEG_BODY) return;
    if (!app->lay.band_used[id]) return;
    th = theme_of(app);
    switch (id) {
    case SEG_STATUS:  paint_status(app, dev, th);   break;
    case SEG_TABBAR:  paint_tabbar(app, dev, th);   break;
    case SEG_TITLE:   paint_title(app, dev, th);    break;
    case SEG_SOFTKEY: paint_softkeys(app, dev, th); break;
    default: break;
    }
}

void segui_paint_body(SEGUI_APP *app, GDEV *dev)
{
    const SEGUI_THEME *th = theme_of(app);
    SEGUI_TAB *t;
    int i, row_h;

    if (!app || !dev || app->n_tab <= 0) return;
    t = &app->tabs[app->active_tab];
    row_h = segui_row_h(app);

    fill_rect(dev, &app->lay.band[SEG_BODY], th->panel_bg);

    for (i = 0; i < t->n_ctrl; i++) {
        SEGUI_CTRL *c = &t->ctrl[i];
        BOOL focused = (i == t->focus);

        switch (c->type) {
        case SEGUI_W_LABEL:
            segui_draw_label_text(dev, &c->box, c, th);
            break;
        case SEGUI_W_CHECK:
            segui_draw_check(dev, &c->box, c, th, focused &&
                             !(app->prof->flags & SEGP_WINDOWED));
            break;
        case SEGUI_W_TOGGLE:
            segui_draw_toggle(dev, &c->box, c, th, focused &&
                              !(app->prof->flags & SEGP_WINDOWED));
            break;
        case SEGUI_W_LIST: {
            int vis = segui_list_rows_visible(c, app);
            int k;
            for (k = 0; k < vis; k++) {
                int idx = c->row_top + k;
                RECT rr;
                if (idx >= c->n_row) break;
                rr = (RECT){ c->box.left, (H)(c->box.top + k * row_h),
                             c->box.right, (H)(c->box.top + (k + 1) * row_h) };
                /* A header band is not a stop, so it never draws a focus. */
                segui_draw_row(dev, &rr, &c->rows[idx], row_h,
                               c->rows[idx].type != SEGUI_ROW_HEADER &&
                               idx == c->row_focus, th, app->prof->flags);
            }
            if (c->n_row > vis) {
                /* Proportional scroll indicator: its length is the visible
                 * share of the list, so a long contact list reads as long. */
                int track = rect_h(&c->box);
                int thumb = track * vis / c->n_row;
                int span = c->n_row - vis;
                int pos;
                RECT gutter, bar;
                if (thumb < 12) thumb = 12;
                if (thumb > track) thumb = track;
                pos = span > 0 ? (track - thumb) * c->row_top / span : 0;
                gutter = (RECT){ (H)(c->box.right - 5), c->box.top,
                                 c->box.right, c->box.bottom };
                fill_rect(dev, &gutter, th->field_bg);
                bar = (RECT){ (H)(c->box.right - 5), (H)(c->box.top + pos),
                              c->box.right, (H)(c->box.top + pos + thumb) };
                fill_rect(dev, &bar, th->accent);
            }
            break;
        }
        case SEGUI_W_CHAT:
            segui_draw_chat(dev, c, th);
            break;
        default:
            segui_draw_button(dev, &c->box, c, th, focused);
            break;
        }
    }
}

void segui_paint(SEGUI_APP *app, GDEV *dev)
{
    const SEGUI_THEME *th;
    SEG_ID id;

    if (!app || !dev) return;
    th = theme_of(app);

    fill_rect(dev, &(RECT){ 0, 0, dev->width, dev->height }, th->desktop_bg);

    if (app->prof->flags & SEGP_WINDOWED) {
        fill_rect(dev, &app->lay.outer, th->panel_bg);
        segui_draw_bevel(dev, &app->lay.outer, th->frame_hi, th->frame_sh, 2);
    }

    for (id = SEG_STATUS; id < SEG_ID_COUNT; id++)
        if (id != SEG_BODY) segui_paint_segment(app, dev, id);
    segui_paint_body(app, dev);
}

/* ── Interaction ────────────────────────────────────────────────────────── */

SEGUI_TAB *segui_active_tab(SEGUI_APP *app)
{
    if (!app || app->n_tab <= 0) return NULL;
    return &app->tabs[app->active_tab];
}

SEGUI_CTRL *segui_list_of(SEGUI_APP *app)
{
    SEGUI_TAB *t = segui_active_tab(app);
    int i;
    if (!t) return NULL;
    for (i = 0; i < t->n_ctrl; i++)
        if (t->ctrl[i].type == SEGUI_W_LIST) return &t->ctrl[i];
    return NULL;
}

SEGUI_CTRL *segui_focused(SEGUI_APP *app)
{
    SEGUI_TAB *t = segui_active_tab(app);
    if (!t || t->n_ctrl <= 0) return NULL;
    if (t->focus >= t->n_ctrl) t->focus = 0;
    return &t->ctrl[t->focus];
}

static BOOL focusable(const SEGUI_CTRL *c)
{
    return c->type != SEGUI_W_LABEL && !(c->state & SEGUI_ST_DISABLED);
}

static BOOL row_focusable(const SEGUI_ROW *r)
{
    return r->type != SEGUI_ROW_HEADER && !(r->state & SEGUI_ST_DISABLED);
}

static void normalise_focus(SEGUI_TAB *t)
{
    int i;
    if (t->n_ctrl <= 0) { t->focus = 0; return; }
    if (t->focus < 0 || t->focus >= t->n_ctrl) t->focus = 0;
    for (i = 0; i < t->n_ctrl; i++)
        if (focusable(&t->ctrl[t->focus])) return;
    t->focus = 0;
}

/* Keep c->row_focus inside the visible window, scrolling the least needed. */
static void list_reveal(SEGUI_CTRL *c, const SEGUI_APP *app)
{
    int vis = segui_list_rows_visible(c, app);
    if (c->row_focus < 0) c->row_focus = 0;
    if (c->row_focus >= c->n_row) c->row_focus = c->n_row - 1;
    if (c->row_focus < 0) c->row_focus = 0;
    if (c->row_focus < c->row_top) c->row_top = c->row_focus;
    if (c->row_focus >= c->row_top + vis) c->row_top = c->row_focus - vis + 1;
    if (c->row_top + vis > c->n_row) c->row_top = c->n_row - vis;
    if (c->row_top < 0) c->row_top = 0;
}

void segui_list_move(SEGUI_APP *app, int delta)
{
    SEGUI_CTRL *c = segui_list_of(app);
    int steps = 0;
    if (!c || c->n_row <= 0) return;
    list_reveal(c, app);
    do {
        int next = c->row_focus + (delta < 0 ? -1 : 1);
        if (next < 0 || next >= c->n_row) break;
        c->row_focus = next;
        steps++;
    } while (!row_focusable(&c->rows[c->row_focus]) && steps < c->n_row);
    list_reveal(c, app);
}

SEGUI_ROW *segui_list_focused(SEGUI_APP *app, int *out_index)
{
    SEGUI_CTRL *c = segui_list_of(app);
    if (!c || c->n_row <= 0) return NULL;
    if (out_index) *out_index = c->row_focus;
    return &c->rows[c->row_focus];
}

void segui_move_focus(SEGUI_APP *app, int delta)
{
    SEGUI_TAB *t = segui_active_tab(app);
    SEGUI_CTRL *c;
    int steps = 0;

    if (!t || t->n_ctrl <= 0) return;
    c = &t->ctrl[t->focus >= 0 && t->focus < t->n_ctrl ? t->focus : 0];

    /* A one-widget-per-screen tab is the phone shape: the rows are the focus
     * chain, and stepping the tab's focus would do nothing. */
    if (c->type == SEGUI_W_LIST) { segui_list_move(app, delta); return; }

    normalise_focus(t);
    do {
        t->focus = (t->focus + (delta < 0 ? t->n_ctrl - 1 : 1)) % t->n_ctrl;
        steps++;
    } while (!focusable(&t->ctrl[t->focus]) && steps < t->n_ctrl);
}

int segui_select_tab(SEGUI_APP *app, int index)
{
    if (!app || app->n_tab <= 0) return -1;
    if (index < 0) index += app->n_tab;
    index %= app->n_tab;
    app->active_tab = index;
    app->pressed_ctrl = -1;
    normalise_focus(&app->tabs[index]);
    return index;
}

static int ctrl_index(const SEGUI_TAB *t, const SEGUI_CTRL *c)
{
    return (int)(c - t->ctrl);
}

SEGUI_CTRL *segui_hit_test(SEGUI_APP *app, H x, H y)
{
    SEGUI_TAB *t = segui_active_tab(app);
    int i;
    if (!t) return NULL;
    for (i = 0; i < t->n_ctrl; i++) {
        SEGUI_CTRL *c = &t->ctrl[i];
        if (!focusable(c)) continue;
        if (x < c->box.left || x >= c->box.right ||
            y < c->box.top  || y >= c->box.bottom) continue;
        t->focus = i;
        if (c->type == SEGUI_W_LIST && c->n_row > 0) {
            int row_h = segui_row_h(app);
            int idx = c->row_top + (row_h > 0 ? (y - c->box.top) / row_h : 0);
            if (idx >= 0 && idx < c->n_row && row_focusable(&c->rows[idx]))
                c->row_focus = idx;
        }
        return c;
    }
    return NULL;
}

void segui_activate(SEGUI_APP *app, SEGUI_CTRL *c)
{
    SEGUI_TAB *t = segui_active_tab(app);

    if (!app || !c || (c->state & SEGUI_ST_DISABLED)) return;

    if (c->type == SEGUI_W_LIST && c->n_row > 0) {
        SEGUI_ROW *r = &c->rows[c->row_focus];
        if (r->state & SEGUI_ST_DISABLED) return;
        if (r->type == SEGUI_ROW_TOGGLE) r->state ^= SEGUI_ST_SELECTED;
        list_reveal(c, app);
        if (r->on_activate) r->on_activate(app, c, c->row_focus);
        return;
    }

    if (c->type != SEGUI_W_BUTTON || (c->state & SEGUI_ST_LATCHED))
        c->state ^= SEGUI_ST_SELECTED;
    c->state &= ~SEGUI_ST_PRESSED;
    if (c->on_activate) c->on_activate(app, c);
    (void)t;
}

void segui_softkey(SEGUI_APP *app, int key)
{
    if (!app) return;
    if (key == 0) {
        SEGUI_CTRL *c = segui_focused(app);
        if (c) { c->state |= SEGUI_ST_PRESSED; segui_activate(app, c); }
    } else if (key == 1) {
        segui_select_tab(app, app->active_tab + 1);
    } else if (key == 2) {
        segui_select_tab(app, app->active_tab - 1);
    }
}

BOOL segui_event(SEGUI_APP *app, GDEV *dev, const EVT *ev)
{
    SEGUI_TAB *t;
    SEGUI_CTRL *c;

    if (!app || !dev || !ev) return FALSE;
    t = segui_active_tab(app);
    if (!t) return FALSE;

    switch (ev->type) {
    case EV_BUT_DOWN:
        c = segui_hit_test(app, ev->pos.x, ev->pos.y);
        if (!c) { app->pressed_ctrl = -1; return FALSE; }
        if (c->type != SEGUI_W_LIST) c->state |= SEGUI_ST_PRESSED;
        app->pressed_ctrl = ctrl_index(t, c);
        return TRUE;

    case EV_BUT_UP: {
        int was = app->pressed_ctrl;
        app->pressed_ctrl = -1;
        if (was < 0 || was >= t->n_ctrl) return FALSE;
        c = &t->ctrl[was];
        c->state &= ~SEGUI_ST_PRESSED;
        if (ev->pos.x >= c->box.left && ev->pos.x < c->box.right &&
            ev->pos.y >= c->box.top  && ev->pos.y < c->box.bottom)
            segui_activate(app, c);
        return TRUE;
    }
    case EV_MOUSE_MOVE: {
        BOOL inside;
        if (app->pressed_ctrl < 0 || app->pressed_ctrl >= t->n_ctrl) return FALSE;
        c = &t->ctrl[app->pressed_ctrl];
        inside = ev->pos.x >= c->box.left && ev->pos.x < c->box.right &&
                 ev->pos.y >= c->box.top  && ev->pos.y < c->box.bottom;
        if (inside == (BOOL)((c->state & SEGUI_ST_PRESSED) ? TRUE : FALSE))
            return FALSE;
        if (inside) c->state |= SEGUI_ST_PRESSED;
        else        c->state &= ~SEGUI_ST_PRESSED;
        return TRUE;
    }
    case EV_KEY_DOWN:
        if (ev->key == BTRON_KEY_UP || ev->key == BTRON_KEY_LEFT) {
            segui_move_focus(app, -1);
            return TRUE;
        }
        if (ev->key == BTRON_KEY_DOWN || ev->key == BTRON_KEY_RIGHT) {
            segui_move_focus(app, +1);
            return TRUE;
        }
        if (ev->key == BTRON_KEY_RETURN || ev->key == BTRON_KEY_KP_ENTER ||
            ev->key == BTRON_KEY_SPACE) {
            c = segui_focused(app);
            if (c) { c->state |= SEGUI_ST_PRESSED; segui_activate(app, c); }
            return TRUE;
        }
        if (ev->key == BTRON_KEY_TAB || ev->key == BTRON_KEY_PAGE_DOWN) {
            segui_select_tab(app, app->active_tab + 1);
            return TRUE;
        }
        if (ev->key == BTRON_KEY_PAGE_UP) {
            segui_select_tab(app, app->active_tab - 1);
            return TRUE;
        }
        if (ev->key == BTRON_KEY_F1) { segui_softkey(app, 0); return TRUE; }
        if (ev->key == BTRON_KEY_F2) { segui_softkey(app, 1); return TRUE; }
        if (ev->key == BTRON_KEY_F3) { segui_softkey(app, 2); return TRUE; }
        if (ev->key >= '1' && ev->key <= '0' + SEGUI_TAB_MAX) {
            /* The digit keys are the tab strip's shortcuts, exactly as a
             * handset's soft keys are its F1-F3. */
            int idx = (int)(ev->key - '1');
            if (idx < app->n_tab) { segui_select_tab(app, idx); return TRUE; }
        }
        return FALSE;
    default:
        return FALSE;
    }
}

const char *segui_segment_name(SEG_ID id)
{
    switch (id) {
    case SEG_STATUS:  return "STATUS";
    case SEG_TABBAR:  return "TABBAR";
    case SEG_TITLE:   return "TITLE";
    case SEG_BODY:    return "BODY";
    case SEG_SOFTKEY: return "SOFTKEY";
    default:          return "?";
    }
}
