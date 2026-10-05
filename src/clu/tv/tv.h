/*
 * B-System BTRON3 — tv.h
 * Terminal Vision Editor & Viewer (TV)
 * NASA JPL Power of Ten compliant, Asian language aware, word-wrap enabled.
 */
#ifndef TV_H
#define TV_H

#include "../cluc.h"
#include "../lang.h"
#include "../term.h"
#include "../vfs.h"
#define TV_MAX_LINES 2048
#define TV_MAX_LINE_BYTES 1024

typedef struct {
    char   data[TV_MAX_LINE_BYTES];
    size_t len;
} TvLine;

typedef enum {
    TV_MODAL_NONE = 0,
    TV_MODAL_EXIT,
    TV_MODAL_HELP
} TvModalMode;

typedef struct TvContext {
    TvLine lines[TV_MAX_LINES];
    size_t line_count;
    char   filename[VFS_MAX_PATH];
    int    view_mode;
    int    insert_mode;
    int    modified;
    int    wrap_mode;
    size_t cur_line;
    size_t cur_byte;
    int    scroll_y;
    int    scroll_sub;
    int    scroll_x;
    int    modal_mode;
    int    modal_sel;
    int    sel_active;
    size_t sel_anchor_line;
    size_t sel_anchor_byte;
} TvContext;

TvContext *tv_context_create(void);
void       tv_context_destroy(TvContext *tv);
void       tv_set_context(TvContext *tv);
TvContext *tv_get_context(void);

/*
 * Run TV in-process on the given file path.
 * view_only: 1 for view mode (F3), 0 for edit mode (F4).
 * Returns 0 on normal exit, <0 on failure.
 */
int tv_run(const char *filepath, int view_only);
int tv_main(int argc, char *argv[]);

int tv_session_init(const char *filepath, int view_only, int rows, int cols);
int tv_session_step(int key);
void tv_session_close(void);

#endif /* TV_H */

