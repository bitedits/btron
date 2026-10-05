/*
 * B-System BTRON3 — sokhatsky.h
 * Sokhatsky Commander (SC) Dual-Pane File Manager
 * NASA JPL Power of Ten compliant, pure C99, universal portable code.
 */
#ifndef SC_H
#define SC_H

#include "../cluc.h"
#include "../term.h"
#include "../vfs.h"
#include "../lang.h"
#include "../tv/tv.h"
#include <stdint.h>
#include <stddef.h>

/* Screen Styles */
enum {
    SC_STYLE_RESET = 0,
    SC_STYLE_HEADER,
    SC_STYLE_TEXT,
    SC_STYLE_HIGHLIGHT,
    SC_STYLE_DIR,
    SC_STYLE_BORDER,
    SC_STYLE_BOTTOM,
    SC_STYLE_MENU,
    SC_STYLE_MENU_SEL,
    SC_STYLE_PINK,
    SC_STYLE_WHITE,
    SC_STYLE_BUTTON_HL
};

/* Key Definitions mapped to term.h */
#define KEY_ESC                      K_ESC
#define KEY_UP                       K_UP
#define KEY_DOWN                     K_DOWN
#define KEY_RIGHT                    K_RIGHT
#define KEY_LEFT                     K_LEFT
#define KEY_PGUP                     K_PGUP
#define KEY_PGDOWN                   K_PGDOWN
#define KEY_HOME                     K_HOME
#define KEY_END                      K_END
#define KEY_INSERT                   K_INSERT
#define KEY_DELETE                   K_DELETE
#define KEY_F1                       K_F1
#define KEY_F2                       K_F2
#define KEY_F3                       K_F3
#define KEY_F4                       K_F4
#define KEY_F5                       K_F5
#define KEY_F6                       K_F6
#define KEY_F7                       K_F7
#define KEY_F8                       K_F8
#define KEY_F9                       K_F9
#define KEY_F10                      K_F10
#define KEY_ENTER                    K_ENTER
#define KEY_TAB                      K_TAB
#define KEY_CTRL_O                   K_CTRL('O')
#define KEY_BACKSPACE                K_BACKSPACE
#define KEY_CTRL_LEFT                K_CTRL_LEFT
#define KEY_CTRL_RIGHT               K_CTRL_RIGHT
#define KEY_SHIFT_LEFT               K_SHIFT_LEFT
#define KEY_SHIFT_RIGHT              K_SHIFT_RIGHT

#define MAX_FILES       256
#define MAX_HISTORY     64
#define MAX_DIR_HISTORY 32

typedef struct {
    char     name[VFS_MAX_NAME];
    uint32_t size;
    uint32_t mtime;
    uint8_t  is_dir;
    uint8_t  is_link;
    uint16_t mode;
} File;

typedef struct {
    char parent_path[VFS_MAX_PATH];
    char dir_name[VFS_MAX_NAME];
    int  cursor_pos;
} DirHistory;

typedef struct {
    char       path[VFS_MAX_PATH];
    File       files[MAX_FILES];
    int        file_count;
    int        cursor;
    int        scroll_offset;
    int        selected[MAX_FILES];
    int        sort_type; /* 0: name, 1: size, 2: date */
    DirHistory dir_history[MAX_DIR_HISTORY];
    int        dir_history_count;
} Panel;

typedef struct {
    char command[512];
    char output[4096];
} CommandEntry;

/* Global state */
extern Panel left_panel, right_panel;
extern Panel *active_panel;
extern CommandEntry history[MAX_HISTORY];
extern int history_count, history_pos, history_start;
extern char command_buffer[512];
extern int show_command_buffer;
extern int history_scroll_pos;
extern int history_display_offset;
extern int total_lines;
extern int max_display;
extern int insert_mode;
extern int cmd_cursor_pos;
extern int cmd_display_offset;

/* Functions in input.c */
int  get_input(void);
void execute_command(const char *cmd);
void finalize_exec(const char *cmd, const char *out_text);

/* Functions in menus.c */
void draw_interface(void);
void draw_panel(Panel *panel, int start_col, int width, int is_active);
void draw_panel_border(int start_col, int start_row, int width, int height, int style);
void draw_menu(void);
void draw_command_line(void);
void draw_bottom_bar(void);
void draw_exit_dialog(int selected);
int  handle_menu(void);
int  handle_exit_dialog(void);
void append_to_history_display(const char *command, const char *output);

/* Functions in files.c */
void load_files(Panel *panel);
int  compare_files(const void *a, const void *b);

/* Navigation */
void left_navigation(Panel *p);
void normalize(Panel *p);

int  sc_main(int argc, char *argv[]);
int  sc_session_init(const char *start_path, int rows, int cols);
int  sc_session_step(int c);
void sc_session_close(void);
int  sc_launch_tv(const char *filepath, int view_only);
int  sc_is_tv_active(void);

#endif /* SC_H */

