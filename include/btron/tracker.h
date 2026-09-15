/*
 * B-TRON Specification Compatible Header: tracker.h
 * Low-Latency Haiku-style Deskbar / Tracker Root Menu & Window Tracker
 * Pure C99, bounded static allocation, verifiable state machine.
 */

#ifndef _BTRON_TRACKER_H_
#define _BTRON_TRACKER_H_

#include <btron/types.h>
#include <btron/dp.h>
#include <btron/wnd.h>
#include <btron/error.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TRACKER_MAX_ITEMS      24
#define TRACKER_BTN_WIDTH      80
#define TRACKER_BTN_HEIGHT     20
#define TRACKER_MENU_MIN_WIDTH 190
#define TRACKER_ITEM_HEIGHT    20

typedef enum {
    TRACKER_STATE_NORMAL  = 0,
    TRACKER_STATE_HOVER   = 1,
    TRACKER_STATE_PRESSED = 2,
    TRACKER_STATE_OPEN    = 3
} TRACKER_STATE;

typedef enum {
    TRACKER_CMD_NONE = 0,
    TRACKER_CMD_ABOUT,
    TRACKER_CMD_CABINET,
    TRACKER_CMD_SETTINGS,
    TRACKER_CMD_TEDITOR,
    TRACKER_CMD_MATRIX,
    TRACKER_CMD_TERMINAL,
    TRACKER_CMD_AUDIODECK,
    TRACKER_CMD_ORCHESTRA,
    TRACKER_CMD_CHAT,
    TRACKER_CMD_DRIVESETUP,
    TRACKER_CMD_CLARITY,
    TRACKER_CMD_WND_FOCUS,
    TRACKER_CMD_RESTART,
    TRACKER_CMD_SHUTDOWN,
    TRACKER_CMD_SEPARATOR
} TRACKER_CMD_TYPE;

typedef struct {
    TRACKER_CMD_TYPE type;
    char             label[48];
    WND              *target_wnd; /* Associated window for window tracking */
    BOOL             enabled;
} TRACKER_ITEM;

typedef struct {
    TRACKER_STATE state;
    RECT          btn_rect;
    RECT          menu_rect;
    H             hover_index;
    H             item_count;
    TRACKER_ITEM  items[TRACKER_MAX_ITEMS];
    UW            launch_count;
} TRACKER;

/* Lifecycle & Core API */
ER   tracker_init(void);
void tracker_render_button(GDEV *dev);
void tracker_render_menu(GDEV *dev);

/* Low-latency Event Dispatching */
BOOL tracker_hit_button(H x, H y);
BOOL tracker_hit_menu(H x, H y);
BOOL tracker_handle_mouse_down(H x, H y);
BOOL tracker_handle_mouse_move(H x, H y);
BOOL tracker_handle_mouse_up(H x, H y);
BOOL tracker_handle_key(W key_code);

/* State Inspection & Control */
BOOL tracker_is_menu_open(void);
void tracker_open_menu(void);
void tracker_close_menu(void);
void tracker_toggle_menu(void);
void tracker_refresh_windows(void);

/* Dynamic Sizing (Calculates widest menu item before drop down to prevent overflow) */
H tracker_calc_text_width(const char *text);
H tracker_calc_widest_item_width(void);

/* Formal Invariant Verification (NASA JPL rules: bounded state, assertable) */
BOOL tracker_verify_invariants(void);
const TRACKER* tracker_get_state(void);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_TRACKER_H_ */
