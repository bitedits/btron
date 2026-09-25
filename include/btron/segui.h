/*
 * SegUI (Segmentation UI) Toolkit Header: segui.h
 *
 * A lightweight, modular and scalable user interface toolkit for embedded
 * systems, home appliances, kiosks and retro machines.
 *
 * SegUI's premise is that a UI is not a pile of absolute coordinates but a
 * *budget*: every device screen is a fixed height, and that height is divided
 * into a small, ordered set of named segments (status band, tab segment,
 * title segment, body segment, soft key segment).  A device is therefore
 * described by data - which segments it has, how tall each one is, and how the
 * body grid divides its width - never by a conditional compilation branch.
 * The same widget code paints a 480x800 handset, a 480x272 landscape
 * appliance panel and a 640x480 retro desktop workstation, because each of
 * those is only a different segment table.
 *
 * NASA JPL Rule 3 compliant, as the rest of this tree: bounded static state,
 * zero post-boot heap allocations.  Every structure here is fixed-size and is
 * owned by the caller (a file-scope static in the application is the intended
 * allocation site), so a SegUI application links into a freestanding target
 * without a libc allocator.
 */

#ifndef _BTRON_SEGUI_H_
#define _BTRON_SEGUI_H_

#include <btron/types.h>
#include <btron/error.h>
#include <btron/dp.h>
#include <btron/event.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Fixed capacity: the whole toolkit's footprint is these six numbers ── */
#define SEGUI_LABEL_MAX    32   /* caption / label bytes incl. NUL          */
#define SEGUI_CTRL_MAX      8   /* widgets per tab segment                  */
#define SEGUI_TAB_MAX       6   /* tabs (the horizontal segment selector)   */
#define SEGUI_BAND_MAX      5   /* segments per profile                     */
#define SEGUI_SOFTKEY_MAX   3   /* soft keys / window caption buttons       */

#ifndef ARGB
#define ARGB(a,r,g,b) (((uint32_t)(a)<<24)|((uint32_t)(r)<<16)|((uint32_t)(g)<<8)|(uint32_t)(b))
#endif

/* ── Segments ───────────────────────────────────────────────────────────
 * Each SEG_ID is one horizontal slice of the screen budget.  A profile
 * declares an *ordered* list of them; the list order is the stacking order,
 * so STATUS/TABBAR/BODY/SOFTKEY and TITLE/BODY are both expressible without
 * either one knowing about the other.
 *
 * Exactly one segment in a profile sets stretch = TRUE (normally BODY).  It
 * absorbs the leftover budget, which is what makes the toolkit scale rather
 * than clip: shrink the screen and the body segment shrinks first.
 */
typedef enum {
    SEG_STATUS = 0,   /* clock / radio / battery, or any device telemetry   */
    SEG_TABBAR,       /* horizontal segment selector (tabs)                 */
    SEG_TITLE,        /* application or window title                        */
    SEG_BODY,         /* flexible content segment                           */
    SEG_SOFTKEY,      /* labelled actions pinned to the bottom edge         */
    SEG_ID_COUNT
} SEG_ID;

typedef struct {
    SEG_ID id;
    H      height;    /* nominal band height in px                          */
    BOOL   stretch;   /* TRUE = takes every pixel the others leave over     */
} SEG_BAND;

/* ── Profile flags ── */
#define SEGP_WINDOWED   0x0001u  /* body sits in a titled window frame with
                                  * caption buttons instead of flush to the
                                  * screen edge (desktop form factors)       */
#define SEGP_TAB_BOX    0x0002u  /* draw tab separators as boxes (retro)
                                  * instead of an underline (touch)          */
#define SEGP_DOTTED_FOCUS 0x0004u /* dotted focus rectangle, not a solid ring */
#define SEGP_PORTRAIT   0x0008u  /* eligible only when height > width.  The
                                  * size windows in the table alone are not
                                  * enough: 640x480 sits inside a 240..720 x
                                  * 400..1400 box, so orientation is part of
                                  * what makes a profile match one device.    */

/* ── Theme: a color table, so a look is data too ── */
typedef struct {
    const char *name;
    COLOR desktop_bg;    /* behind everything (retro teal, handset pattern) */
    COLOR panel_bg;      /* body segment                                   */
    COLOR frame_hi;      /* 3D bevel highlight (top/left)                   */
    COLOR frame_sh;      /* 3D bevel shadow (bottom/right)                  */
    COLOR band_bg;       /* status / title / tab segments                   */
    COLOR band_fg;       /* text on band_bg                                 */
    COLOR accent;        /* selection: tab underline, focus ring, check mark */
    COLOR btn_face;      /* widget face, unselected                         */
    COLOR btn_fg;
    COLOR btn_sel_face;  /* widget face, selected or pressed                */
    COLOR btn_sel_fg;
    COLOR text;          /* body text                                       */
    COLOR text_dim;      /* secondary text, disabled widgets                */
    COLOR field_bg;      /* checkbox well, toggle track background          */
    COLOR toggle_on;     /* active toggle track                             */
} SEGUI_THEME;

extern const SEGUI_THEME segui_theme_handset;
extern const SEGUI_THEME segui_theme_retro;
extern const SEGUI_THEME segui_theme_appliance;

/* ── Profile: one device class, described entirely as numbers ── */
typedef struct {
    const char *name;
    H form_w, form_h;                 /* the reference form factor          */
    H min_w, min_h, max_w, max_h;     /* eligibility window for auto-match  */
    UW  flags;
    SEG_BAND bands[SEGUI_BAND_MAX];
    int n_bands;
    H margin;                         /* screen edge inset                  */
    H pad;                            /* frame inset to the body content    */
    H btn_h;                          /* preferred widget height            */
    H row_h;                          /* one stacked row (check, toggle)    */
    H min_col_w;                      /* a grid column is never narrower    */
    H max_cols;                       /* widest grid this form factor shows */
    H gap;                            /* spacing between widgets            */
    const SEGUI_THEME *theme;
    const char *softkeys[SEGUI_SOFTKEY_MAX];
} SEGUI_PROFILE;

/* Handset: 480x800 portrait, vertical screen, touch + keypad. */
extern const SEGUI_PROFILE segui_profile_handset;
/* Appliance / kiosk TFT: 480x272 landscape.  No status band; the whole
 * budget goes to content because a 272 px panel cannot pay for chrome. */
extern const SEGUI_PROFILE segui_profile_tft272;
/* Retro desktop: 640x480, windowed, no tab or status segment. */
extern const SEGUI_PROFILE segui_profile_retro;

/* ── Widgets ── */
typedef enum {
    SEGUI_W_BUTTON = 0,  /* momentary, or latched when SEGUI_ST_SELECTED    */
    SEGUI_W_CHECK,       /* checkbox: a labelled boolean                  */
    SEGUI_W_TOGGLE,      /* switch: a labelled boolean drawn as a pill    */
    SEGUI_W_LABEL,       /* text only; never focusable, never hit-tested  */
    SEGUI_W_LIST,        /* scrollable row list: the phone screen workhorse */
    SEGUI_W_CHAT         /* message log with left/right bubbles           */
} SEGUI_WTYPE;

/* Widget captions may carry a small vector icon.  SegUI owns its glyph bitmaps
 * so a target needs no font engine to draw a recognisable control. */
typedef enum {
    SEGUI_ICON_NONE = 0,
    SEGUI_ICON_BOLT,     /* fast                                       */
    SEGUI_ICON_MODULAR,  /* four blocks                                */
    SEGUI_ICON_SCALABLE, /* numeral grid                               */
    SEGUI_ICON_WINDOW,   /* kiosk / app tile                           */
    SEGUI_ICON_INFO,     /* lower case i in a disc                     */
    SEGUI_ICON_COUNT
} SEGUI_ICON;

#define SEGUI_ST_SELECTED 0x0001u   /* latched / current choice                */
#define SEGUI_ST_PRESSED  0x0002u   /* pointer is currently down on it         */
#define SEGUI_ST_DISABLED 0x0004u
#define SEGUI_ST_LATCHED  0x0008u   /* activate() toggles instead of pulses    */

struct SEGUI_APP;
struct SEGUI_CTRL;

/* ── Rows: the minimal phone screen primitive ───────────────────────────
 * Contacts, People and Settings are the same shape on a small phone - one
 * scrollable list - so SegUI expresses them as one widget whose *rows* differ
 * only in what their right edge carries: a value, a switch, an arrow into a
 * deeper screen, or an initial disc for a person.  A row is 24 px on a
 * handset and can be shortened to 20 on a low panel; the list scrolls by
 * rows, never by pixels, so no row is ever drawn half-off a segment.
 */
#define SEGUI_ROW_MAX    16   /* rows a single list widget holds            */
#define SEGUI_MSG_MAX    12   /* bubbles a single chat widget holds         */
#define SEGUI_TEXT_MAX   48   /* chat bubble bytes incl. NUL                */

typedef enum {
    SEGUI_ROW_PLAIN = 0,   /* caption only                              */
    SEGUI_ROW_HEADER,      /* section band; not focusable               */
    SEGUI_ROW_VALUE,       /* caption + right-aligned value             */
    SEGUI_ROW_TOGGLE,      /* caption + ON/OFF pill (SEGUI_ST_SELECTED) */
    SEGUI_ROW_ARROW,       /* caption + forward marker into a screen    */
    SEGUI_ROW_PERSON       /* initial disc + caption + value            */
} SEGUI_ROW_TYPE;

typedef struct SEGUI_ROW {
    SEGUI_ROW_TYPE type;
    char caption[SEGUI_LABEL_MAX];
    char value[SEGUI_LABEL_MAX];
    UW   state;                              /* SEGUI_ST_*                */
    void (*on_activate)(struct SEGUI_APP *app, struct SEGUI_CTRL *ctrl, int row);
} SEGUI_ROW;

typedef enum {
    SEGUI_MSG_THEM = 0,    /* from the peer: bubble on the left          */
    SEGUI_MSG_ME,          /* from this device: bubble on the right      */
    SEGUI_MSG_NOTE         /* centred, no bubble: timestamp / delivered  */
} SEGUI_MSG_WHO;

typedef struct SEGUI_MSG {
    SEGUI_MSG_WHO who;
    char text[SEGUI_TEXT_MAX];
} SEGUI_MSG;

typedef struct SEGUI_CTRL {
    SEGUI_WTYPE type;
    char     label[SEGUI_LABEL_MAX];
    SEGUI_ICON icon;
    RECT     box;          /* filled in by segui_layout(), device coordinates */
    UW       state;
    void   (*on_activate)(struct SEGUI_APP *app, struct SEGUI_CTRL *ctrl);

    /* Container payload: caller-owned static tables, never copied. */
    SEGUI_ROW *rows;       /* SEGUI_W_LIST                              */
    int        n_row;
    int        row_top;    /* first visible row (scroll position)       */
    int        row_focus;  /* current row within the visible window     */
    SEGUI_MSG *msgs;       /* SEGUI_W_CHAT                              */
    int        n_msg;
    char       peer[SEGUI_LABEL_MAX];   /* chat header / list footer     */
} SEGUI_CTRL;

/* ── A tab is one view of the body segment ── */
typedef struct {
    char        caption[SEGUI_LABEL_MAX];
    SEGUI_CTRL  ctrl[SEGUI_CTRL_MAX];
    int         n_ctrl;
    int         focus;       /* index into ctrl[] of the current item        */
    BOOL        grid;        /* TRUE = buttons tile as a column grid         */
} SEGUI_TAB;

/* ── Computed geometry: the answer to "where does each segment go" ── */
typedef struct {
    RECT outer;                          /* the frame SegUI owns             */
    RECT band[SEG_ID_COUNT];             /* used only where band_used        */
    BOOL band_used[SEG_ID_COUNT];
    RECT body;                           /* inside the body band's padding   */
    H    cols, cell_w;
    H    grid_rows, cell_h;
    RECT grid;                           /* the area the button grid got     */
    int  row_first;                      /* body slot of the first stacked
                                          * row widget; -1 when there is none*/
    const SEGUI_PROFILE *prof;
} SEGUI_LAYOUT;

/* ── Application instance: caller-owned, fixed size, no allocator ── */
typedef struct SEGUI_APP {
    const SEGUI_PROFILE *prof;
    const SEGUI_THEME   *theme;        /* NULL = the profile's theme        */
    SEGUI_TAB    tabs[SEGUI_TAB_MAX];
    int          n_tab;
    int          active_tab;
    char         title[SEGUI_LABEL_MAX];   /* window / title segment text   */
    char         clock[8], radio[8];       /* status segment telemetry      */
    int          battery_pct;
    SEGUI_LAYOUT lay;
    /* Transient interaction state */
    int          pressed_ctrl;         /* ctrl index under the pointer, or -1 */
} SEGUI_APP;

/* ── Profile & theme selection (data, never a preprocessor branch) ── */
const SEGUI_PROFILE *segui_profile_for(H screen_w, H screen_h);
const SEGUI_PROFILE *segui_profile_named(const char *name);
const SEGUI_THEME   *segui_theme_named(const char *name);

/* ── Construction ── */
void         segui_app_init(SEGUI_APP *app, const char *title,
                            const SEGUI_PROFILE *prof); /* prof NULL = auto */
int          segui_add_tab(SEGUI_APP *app, const char *caption, BOOL grid);
SEGUI_CTRL  *segui_add_button(SEGUI_APP *app, int tab, const char *caption,
                             SEGUI_ICON icon,
                             void (*cb)(SEGUI_APP *, SEGUI_CTRL *));
SEGUI_CTRL  *segui_add_check(SEGUI_APP *app, int tab, const char *caption,
                             BOOL init,
                             void (*cb)(SEGUI_APP *, SEGUI_CTRL *));
SEGUI_CTRL  *segui_add_toggle(SEGUI_APP *app, int tab, const char *caption,
                              BOOL init,
                              void (*cb)(SEGUI_APP *, SEGUI_CTRL *));
SEGUI_CTRL  *segui_add_label(SEGUI_APP *app, int tab, const char *caption);
/* A list or chat widget owns the body segment: the grid maths steps aside for
 * it, which is why a phone screen needs exactly one of these per tab.  The
 * `rows` / `msgs` table is caller-owned static data and is never copied. */
SEGUI_CTRL  *segui_add_list(SEGUI_APP *app, int tab, SEGUI_ROW *rows, int n_row);
SEGUI_CTRL  *segui_add_chat(SEGUI_APP *app, int tab, SEGUI_MSG *msgs, int n_msg,
                            const char *peer);
void         segui_list_move(SEGUI_APP *app, int delta);
SEGUI_ROW   *segui_list_focused(SEGUI_APP *app, int *out_index);
SEGUI_CTRL  *segui_list_of(SEGUI_APP *app);        /* active tab's list, or NULL */
void         segui_set_clock(SEGUI_APP *app, const char *clock);
void         segui_set_title(SEGUI_APP *app, const char *title);
void         segui_set_status(SEGUI_APP *app, const char *clock,
                              const char *radio, int battery_pct);
void         segui_set_str(char *dst, int cap, const char *src);
void         segui_set_peer(SEGUI_APP *app, SEGUI_CTRL *chat, const char *peer);

/* ── Layout: resolve the screen budget into segment rectangles and give every
 * widget in the active tab its share of the body segment.  Call after the
 * widget set changes or the device is resized; it is pure and idempotent. ── */
void segui_layout(SEGUI_APP *app, GDEV *dev);

/* ── Painting ── */
void segui_paint(SEGUI_APP *app, GDEV *dev);           /* whole screen        */
void segui_paint_segment(SEGUI_APP *app, GDEV *dev, SEG_ID id);
void segui_paint_tabbar(SEGUI_APP *app, GDEV *dev);
void segui_paint_body(SEGUI_APP *app, GDEV *dev);

/* Individual widget draws are public so an application can compose a screen
 * SegUI has no segment name for. */
void segui_draw_button(GDEV *dev, const RECT *r, const SEGUI_CTRL *c,
                       const SEGUI_THEME *th, BOOL focused);
void segui_draw_check(GDEV *dev, const RECT *r, const SEGUI_CTRL *c,
                      const SEGUI_THEME *th, BOOL focused);
void segui_draw_toggle(GDEV *dev, const RECT *r, const SEGUI_CTRL *c,
                       const SEGUI_THEME *th, BOOL focused);
void segui_draw_label_text(GDEV *dev, const RECT *r, const SEGUI_CTRL *c,
                           const SEGUI_THEME *th);
int  segui_list_rows_visible(const SEGUI_CTRL *c, const SEGUI_APP *app);
void segui_draw_chat(GDEV *dev, const SEGUI_CTRL *c, const SEGUI_THEME *th);
void segui_draw_row(GDEV *dev, const RECT *r, const SEGUI_ROW *row, int row_h,
                    BOOL focused, const SEGUI_THEME *th, UW flags);
H    segui_row_h(const SEGUI_APP *app);   /* list row pitch for this profile */
void segui_draw_focus(GDEV *dev, const RECT *r, const SEGUI_THEME *th, UW flags);
void segui_draw_icon(GDEV *dev, H x, H y, SEGUI_ICON icon, H size, COLOR col);
void segui_draw_bevel(GDEV *dev, const RECT *r, COLOR hi, COLOR sh, int depth);

/* ── Interaction ── */
SEGUI_TAB  *segui_active_tab(SEGUI_APP *app);          /* the tab, or NULL   */
SEGUI_CTRL *segui_focused(SEGUI_APP *app);
SEGUI_CTRL *segui_hit_test(SEGUI_APP *app, H x, H y);
void        segui_move_focus(SEGUI_APP *app, int delta);
void        segui_activate(SEGUI_APP *app, SEGUI_CTRL *c);
int         segui_select_tab(SEGUI_APP *app, int index); /* wraps; -1 = no tab*/
void        segui_softkey(SEGUI_APP *app, int key);    /* 0 left, 1 mid, 2 right */
BOOL        segui_event(SEGUI_APP *app, GDEV *dev, const EVT *ev);
/* Returns TRUE when the event changed visible state.  The caller repaints:
 * SegUI never touches an event queue or a timer, so it composites into any
 * host loop (POSIX/SDL, FOMA keypad, PS2, an appliance front panel). */

/* ── Reference applications shipped with the toolkit ─────────────────────
 * Both are pure data + callbacks: neither one names a pixel, so either can be
 * hosted on any profile.  A host calls the builder, then segui_layout() and
 * segui_paint() with its own GDEV, and feeds events to segui_event().
 */
void        segui_demo_build(SEGUI_APP *app, const SEGUI_PROFILE *prof);
SEGUI_APP  *segui_demo_app(const SEGUI_PROFILE *prof);

typedef enum {
    SEGUI_SCREEN_CONTACTS = 0,
    SEGUI_SCREEN_CHAT,
    SEGUI_SCREEN_PEOPLE,
    SEGUI_SCREEN_SETTINGS
} SEGUI_PHONE_SCREEN;

void        segui_phone_build(SEGUI_APP *app, const SEGUI_PROFILE *prof);
SEGUI_APP  *segui_phone_app(const SEGUI_PROFILE *prof);
void        segui_phone_show(SEGUI_APP *app, int screen);
int         segui_phone_screen(void);

/* ── Introspection for tests and headless capture ── */
const char *segui_segment_name(SEG_ID id);
int         segui_battery_bars(int pct);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_SEGUI_H_ */
