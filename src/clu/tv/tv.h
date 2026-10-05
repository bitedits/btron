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

