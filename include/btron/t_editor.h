#ifndef _BTRON_T_EDITOR_H_
#define _BTRON_T_EDITOR_H_

#include <btron/types.h>
#include <btron/wnd.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TEDITOR_MAX_ROWS  1000
#define TEDITOR_MAX_LINES 1000
#define TEDITOR_MAX_COLS  512
#define TEDITOR_VIEW_ROWS 14
#define TEDITOR_VIEW_COLS 80

#include <btron/app_menu.h>

typedef struct {
    char lines[TEDITOR_MAX_ROWS][TEDITOR_MAX_COLS];
    int total_lines;

    int cursor_row;
    int cursor_col;

    /* Selection */
    BOOL sel_active;
    int sel_start_r, sel_start_c;
    int sel_end_r, sel_end_c;
    int sel_anchor_r, sel_anchor_c;

    /* Scrolling */
    int scroll_row;
    int scroll_col;

    /* File State */
    char filename[128];
    BOOL is_modified;

    /* Word Wrap */
    BOOL wrap_text;

    /* VOBJ Embed */
    BOOL has_vobj;
    char vobj_name[64];

    /* Menu Bar State (BeOS BMenuBar / BTRON 3.20 Menu Manager) */
    int active_menu;       /* -1 = closed, 0=File, 1=Edit, 2=View, 3=Objects, 4=Help */
    int hover_menu;        /* -1 = none, 0..4 = hovered top-level menu header */
    int hover_item;        /* -1 = none, 0..N = hovered item in active menu */
    int active_submenu;    /* -1 = none, 0..N = item index spawning cascading submenu */
    int hover_subitem;     /* -1 = none, 0..N = hovered item in cascading submenu */
    int tree_hover[4];     /* Cascading tree menu hover indices for Open menu levels */
    BOOL show_line_nums;   /* TRUE = display gutter line numbers */
    APP_MENU_BAR menu_bar;
} TEditor;

WND* open_t_editor_window(void);
WND* open_t_editor_window_with_file(const char *filepath);
TEditor* teditor_get_current(void);
int teditor_load_file(TEditor *ed, const char *filepath);
int teditor_save_file(TEditor *ed, const char *filepath);
int teditor_close_file(TEditor *ed);

/* Menu Manager APIs */
int teditor_get_asset_files(char files[][64], int max_files);
void teditor_open_menu(TEditor *ed, int menu_idx);
void teditor_close_menu(TEditor *ed);
WND* open_teditor_about_window(void);

/* CUA Selection & Word Wrap APIs */
void teditor_get_selection_range(const TEditor *ed, int *r1, int *c1, int *r2, int *c2);
void teditor_toggle_wrap(TEditor *ed);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_T_EDITOR_H_ */
