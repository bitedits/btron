/*
 * B-TRON Specification Header: btron/tad_browser.h
 * Stream-oriented Native Binary & Symbolic TAD Document Browser.
 * Cleanroom implementation conforming to BTRON3 SPEC 3.20 & NASA JPL Scope.
 */

#ifndef _BTRON_TAD_BROWSER_H_
#define _BTRON_TAD_BROWSER_H_

#include <btron/types.h>
#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/vobj.h>
#include <btron/app_menu.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TAD_MAX_SPANS   1024
#define TAD_MAX_LINKS   128
#define TAD_MAX_TITLE   128
#define TAD_MAX_PATH    256
#define TAD_MAX_HISTORY 32

/* Text Span Style Attributes */
typedef struct {
    UH font_id;     /* 0=Mincho, 1=Gothic, 2=Monospace */
    UH font_size;   /* 10, 12, 14, 16, 22 pt */
    UH weight;      /* 400=Regular, 700=Bold */
    COLOR color;    /* RGB Text Color */
    UH indent;      /* Left margin indent in pixels */
    UH line_pitch;  /* Line height in pixels */
    BOOL is_hr;     /* Horizontal vector separator */
    BOOL is_vobj;   /* Virtual Body Link */
    BOOL is_image;  /* BTRON3 Figure / Picture Segment (TS_FPRIM SubID 10/2) */
    H img_w;        /* Image width in pixels */
    H img_h;        /* Image height in pixels */
    UB img_type;    /* 0=Architecture Diagram, 1=State Transition, 2=Hyper-Object Tree, 3=Window Schema */
    char img_src[128];
    char img_caption[128];
    ID target_robj; /* Target Real Object ID if VOBJ */
    char vobj_label[64];
    char vobj_path[128];
} TAD_STYLE;

/* Text Line Wrapping & Layout Callback */
typedef void (*tad_wrap_line_cb)(const char *line_str, int line_len, H line_w, int line_idx, void *user_data);

/* Computed Layout Span */
typedef struct {
    RECT bounds;            /* Visual bounding box (x, y, w, h) in document space */
    TAD_STYLE style;        /* Fusen styling */
    char text[512];         /* Text segment buffer */
    BOOL is_link;           /* Clickable VOBJ link */
} TAD_SPAN;

/* Browser Navigation History Item */
typedef struct {
    char path[TAD_MAX_PATH];
    int scroll_y;
} TAD_HISTORY_ENTRY;

/* TAD Browser State */
typedef struct {
    char doc_title[TAD_MAX_TITLE];
    char file_path[TAD_MAX_PATH];
    TAD_SPAN spans[TAD_MAX_SPANS];
    int span_count;

    int doc_height;         /* Total document pixel height */
    int doc_width;          /* Document pixel width */
    int scroll_y;           /* Viewport vertical scroll offset */
    int page_height;        /* Viewport visible height */

    /* Interactive Hyper-Data Links */
    int hovered_link_idx;   /* Currently mouse-hovered link span index (-1 if none) */
    int active_link_idx;    /* Clicked link span index */

    /* Navigation History Stack */
    TAD_HISTORY_ENTRY history[TAD_MAX_HISTORY];
    int history_count;
    int history_idx;

    BOOL is_binary_tad;     /* TRUE if parsed from binary TAD stream */
    UW total_bytes;         /* Document byte size */

    /* In-Window Application Menu State */
    int active_menu;        /* -1 = closed, 0=File, 1=View, 2=VObj, 3=Help */
    int hover_menu;         /* -1 = none, 0..3 = hovered header in closed state */
    int hover_item;         /* -1 = none, 0..N = hovered dropdown item */
    int hover_submenu;      /* -1 = none, 0..N = hovered cascading submenu item */
    BOOL wrap_text;         /* TRUE by default */
    int zoom_percent;       /* 100 default */
    APP_MENU_BAR menu_bar;

    /* Interactive Address Bar */
    BOOL addr_active;
    char addr_input[TAD_MAX_PATH];
    int addr_cursor;
} TAD_BROWSER;

/* Lifecycle & Window APIs */
void tad_browser_init(TAD_BROWSER *tb);
ER tad_browser_load_file(TAD_BROWSER *tb, const char *filepath);
ER tad_browser_load_buffer(TAD_BROWSER *tb, const void *buf, UW len, const char *doc_title);
void tad_browser_layout(TAD_BROWSER *tb, int view_width);
int tad_browser_wrap_text(const char *utf8_text, int max_w, tad_wrap_line_cb cb, void *user_data);
void tad_browser_paint(TAD_BROWSER *tb, GDEV *dev, const RECT *client_rect);
BOOL tad_browser_handle_mouse(TAD_BROWSER *tb, int mouse_x, int mouse_y, BOOL is_click, ID *out_clicked_robj, char *out_clicked_path);
void tad_browser_scroll(TAD_BROWSER *tb, int delta_y);

/* In-place Navigation & Path Resolution APIs */
void tad_browser_resolve_path(const char *current_path, const char *target, char *out_resolved, size_t max_len);
ER tad_browser_navigate(TAD_BROWSER *tb, const char *target_path);
BOOL tad_browser_can_go_back(const TAD_BROWSER *tb);
BOOL tad_browser_can_go_forward(const TAD_BROWSER *tb);
void tad_browser_go_back(TAD_BROWSER *tb);
void tad_browser_go_forward(TAD_BROWSER *tb);
void tad_browser_go_home(TAD_BROWSER *tb);
void tad_browser_reload(TAD_BROWSER *tb);

/* Image Raster Decoders (GIF & PNG) */
int decode_and_draw_gif(GDEV *dev, const char *file_path, int dst_x, int dst_y, int max_w, int max_h);
int decode_and_draw_png(GDEV *dev, const char *file_path, int dst_x, int dst_y, int max_w, int max_h);

/* Open Standard Application Windows */
WND* open_tad_browser_window(const char *filepath, const char *title);
WND* open_tad_browser_about_window(void);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_TAD_BROWSER_H_ */
