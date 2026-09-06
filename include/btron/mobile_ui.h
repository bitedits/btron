/*
 * B-System (BTRON 3.20) Mobile UI Toolkit Header: mobile_ui.h
 *
 * Dedicated reusable HMI toolkit for vertical screen mobile devices (2004–2009 FOMA era)
 * Focus-driven, keypad-centric, Real Body / Virtual Body (実身・仮身) navigation.
 * NASA JPL Rule 3 compliant: Bounded state, zero post-boot heap allocations.
 */

#ifndef _BTRON_MOBILE_UI_H_
#define _BTRON_MOBILE_UI_H_

#include <btron/types.h>
#include <btron/error.h>
#include <btron/dp.h>
#include <btron/event.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── FOMA Vertical Screen Geometry (480x640 VGA portrait minimum) ── */
#define FOMA_SCREEN_W          480
#define FOMA_SCREEN_H          640

#define FOMA_STATUS_BAR_H      32
#define FOMA_TITLE_BAR_H       36
#define FOMA_ROW_H             36
#define FOMA_SOFTKEY_BAR_H     38

#define FOMA_LIST_TOP          (FOMA_STATUS_BAR_H + FOMA_TITLE_BAR_H)
#define FOMA_LIST_BOTTOM       (FOMA_SCREEN_H - FOMA_SOFTKEY_BAR_H)
#define FOMA_LIST_H            (FOMA_LIST_BOTTOM - FOMA_LIST_TOP)
#define FOMA_VISIBLE_ROWS      (FOMA_LIST_H / FOMA_ROW_H)

#define FOMA_MAX_ITEMS         64
#define FOMA_MAX_SCREEN_DEPTH  16
#define FOMA_STR_MAX           96

#ifndef ARGB
#define ARGB(a,r,g,b) (((uint32_t)(a)<<24)|((uint32_t)(r)<<16)|((uint32_t)(g)<<8)|(uint32_t)(b))
#endif

/* ── Authentic BTRON / Cho-Kanji Mobile Colors ── */
#define FOMA_COL_BG            ARGB(0xFF, 0x00, 0x4D, 0x40) /* Deep teal pattern base */
#define FOMA_COL_BG_DOT        ARGB(0xFF, 0x00, 0x36, 0x2D) /* Dark teal texture dot */
#define FOMA_COL_PANEL_BG      ARGB(0xFF, 0xEB, 0xEE, 0xF2) /* Keitai off-white content plate */
#define FOMA_COL_STATUS_BG     ARGB(0xFF, 0x1A, 0x24, 0x2F) /* Dark slate status bar */
#define FOMA_COL_TITLE_BG      ARGB(0xFF, 0x00, 0x33, 0x66) /* Authentic BTRON Cho-Kanji Navy */
#define FOMA_COL_FOCUS_BG      ARGB(0xFF, 0x00, 0x55, 0x99) /* High-contrast focus row highlight */
#define FOMA_COL_FOCUS_TXT     COLOR_WHITE                  /* Text color in focused row */
#define FOMA_COL_ROW_ALT       ARGB(0xFF, 0xF5, 0xF7, 0xFA) /* Alternating list row tint */
#define FOMA_COL_SEPARATOR     ARGB(0xFF, 0x94, 0xA3, 0xB8) /* Line separator */
#define FOMA_COL_BADGE_BG      ARGB(0xFF, 0xCC, 0xD9, 0xE8) /* Item count pill background */
#define FOMA_COL_BADGE_TXT     ARGB(0xFF, 0x00, 0x28, 0x55) /* Item count text */
#define FOMA_COL_GOLD          ARGB(0xFF, 0xD4, 0xAF, 0x37) /* BTRON gold trim */
#define FOMA_COL_SOFTKEY_BG    ARGB(0xFF, 0xD0, 0xD7, 0xDE) /* Soft key button bevel */
#define FOMA_COL_SOFTKEY_HI    COLOR_WHITE                  /* Bevel highlight */
#define FOMA_COL_SOFTKEY_SH    ARGB(0xFF, 0x80, 0x88, 0x90) /* Bevel shadow */
#define FOMA_COL_FUSEN         ARGB(0xFF, 0x00, 0x66, 0x99) /* [仮身] Virtual Body link blue */

/* ── Item Types ── */
typedef enum {
    FOMA_ITEM_NORMAL = 0,     /* Standard selectable Real Body or menu item */
    FOMA_ITEM_VIRTUAL_BODY,   /* [仮身] Virtual Body hyper-link */
    FOMA_ITEM_SEPARATOR,      /* Non-selectable divider line */
    FOMA_ITEM_HEADER,         /* Group header label (e.g. あいうえおグループ) */
    FOMA_ITEM_INFO            /* Read-only information row */
} FOMA_ITEM_TYPE;

/* Forward declaration */
struct FOMA_SCREEN_S;

/* ── Item Definition ── */
typedef struct FOMA_ITEM_S {
    char title[FOMA_STR_MAX];      /* Primary label (e.g. 連絡先 (Contacts)) */
    char badge[32];                /* Right-aligned count/detail (e.g. "48", "090-XXXX-XXXX") */
    char tag[32];                  /* ID / internal tag */
    FOMA_ITEM_TYPE type;           /* Item type */
    COLOR icon_color;              /* Optional icon accent color */
    void (*action)(struct FOMA_SCREEN_S *scr, int item_idx); /* Trigger callback */
    void *user_data;               /* Context pointer */
} FOMA_ITEM;

/* ── Screen Definition ── */
typedef struct FOMA_SCREEN_S {
    int screen_id;                 /* Unique screen identifier */
    char title[FOMA_STR_MAX];      /* Title bar string */
    char subtitle[64];             /* Optional subtitle or counter (e.g. "48 items") */
    FOMA_ITEM items[FOMA_MAX_ITEMS];
    int item_count;
    int focus_index;               /* Currently highlighted row */
    int top_index;                 /* Top row shown in viewport (scrolling) */

    /* Soft Key Button Labels */
    char softkey_left[24];         /* e.g. [選択], [詳細], [開く], [決定] */
    char softkey_center[24];       /* e.g. [メニュー], [発信], [情報] */
    char softkey_right[24];        /* e.g. [戻る], [キャンセル] */

    /* Custom hooks */
    BOOL (*custom_key_handler)(struct FOMA_SCREEN_S *scr, const EVT *ev);
    void (*custom_render_hook)(GDEV *dev, const struct FOMA_SCREEN_S *scr);
} FOMA_SCREEN;

/* ── Modal Dialog State ── */
typedef struct {
    BOOL is_active;
    char title[FOMA_STR_MAX];
    char message[256];
    char btn_left[24];
    char btn_right[24];
    int selected_btn;              /* 0 = Left, 1 = Right */
    void (*on_confirm)(void);
    void (*on_cancel)(void);
} FOMA_MODAL;

/* ── Mobile UI Coordinator APIs ── */
void foma_ui_init(void);
void foma_push_screen(const FOMA_SCREEN *scr);
void foma_pop_screen(void);
FOMA_SCREEN* foma_get_active_screen(void);
int  foma_get_screen_depth(void);

/* Navigation & Focus control */
void foma_nav_move_focus(FOMA_SCREEN *scr, int delta);
void foma_nav_set_focus(FOMA_SCREEN *scr, int index);
void foma_nav_activate_selected(FOMA_SCREEN *scr);

/* Soft Key Triggers */
void foma_trigger_softkey_left(FOMA_SCREEN *scr);
void foma_trigger_softkey_center(FOMA_SCREEN *scr);
void foma_trigger_softkey_right(FOMA_SCREEN *scr);

/* Modal Dialog APIs */
void foma_show_modal(const char *title, const char *message,
                     const char *btn_left, const char *btn_right,
                     void (*on_confirm)(void), void (*on_cancel)(void));
void foma_close_modal(void);
BOOL foma_is_modal_active(void);
FOMA_MODAL* foma_get_active_modal(void);

/* ── Drawing & Compositing APIs (desktop_mobile.c) ── */
void foma_render_desktop(GDEV *dev, const FOMA_SCREEN *scr);
void foma_render_status_bar(GDEV *dev, const char *carrier, const char *clock_str, int battery_bars, int signal_bars);
void foma_render_title_bar(GDEV *dev, const char *title, const char *subtitle);
void foma_render_list(GDEV *dev, const FOMA_SCREEN *scr);
void foma_render_softkey_bar(GDEV *dev, const char *left_lbl, const char *mid_lbl, const char *right_lbl);
void foma_render_modal(GDEV *dev, const FOMA_MODAL *modal);

/* ── Event Dispatcher & Screen Openers (workbench_mobile.c) ── */
void foma_workbench_init(void);
BOOL foma_workbench_process_event(const EVT *ev);

void foma_show_home_cabinet(void);
void foma_show_contacts(void);
void foma_show_memos(void);
void foma_show_apps_launcher(void);
void foma_show_control_panel(void);
void foma_show_system_menu(void);
void foma_show_device_info(void);
void foma_show_contact_detail_sample(void);
void foma_show_editor_sample(void);
void foma_show_calculator(void);
void foma_show_terminal(void);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_MOBILE_UI_H_ */
