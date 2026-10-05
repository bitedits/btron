/*
 * B-System BTRON3 — term.h
 * Terminal toolkit for the CLU applications: key decoder + cell screen that
 * only sends what changed (diff against the last frame) over the tty.
 *
 *   tty (termios scope)  ->  term (keys, styles, cell diff)  ->  tv / sc
 *
 * Storage is static; dimensions are bounded by TERM_MAXR x TERM_MAXC.
 */
#ifndef CLU_TERM_H
#define CLU_TERM_H

#include <stddef.h>
#include <stdint.h>

#ifndef TERM_MAXR
#define TERM_MAXR 80
#endif
#ifndef TERM_MAXC
#define TERM_MAXC 256
#endif
#define TERM_STYLES 32

/* Keys: Unicode scalar values below K_BASE are text; everything else here. */
#define K_BASE 0x110000
enum {
    K_NONE = K_BASE, K_RESIZE, K_EOF, K_ESC,
    K_UP, K_DOWN, K_RIGHT, K_LEFT, K_PGUP, K_PGDOWN, K_HOME, K_END,
    K_INSERT, K_DELETE,
    K_F1, K_F2, K_F3, K_F4, K_F5, K_F6, K_F7, K_F8, K_F9, K_F10,
    K_ENTER, K_TAB, K_BTAB, K_BACKSPACE,
    K_CTRL_LEFT, K_CTRL_RIGHT, K_SHIFT_LEFT, K_SHIFT_RIGHT,
    K_CTRLB = K_BASE + 0x100        /* K_CTRL(n): Ctrl-A..Ctrl-Z = 1..26 */
};
#define K_CTRL(n) (K_CTRLB + (((n) >= 0x40) ? ((n) & 0x1F) : (n)))

/*
 * Pure incremental decoder.  Returns the key and sets *used on success, -1 if
 * more bytes are required, or K_NONE (with *used>=1) for ignorable input.
 */
int term_parse(const uint8_t *b, size_t n, size_t *used);

/* Session: nestable (tv started from sc shares the same screen). */
int  term_open(void);                 /* 0 ok, <0 no tty */
void term_close(void);
void term_suspend(void);              /* give the tty to a child program */
void term_resume(void);
int  term_key(void);                  /* blocks <=100ms; K_NONE on idle tick */
int  term_resize(void);               /* re-query size; 1 if changed */

extern int term_rows, term_cols;

/* Styles are SGR parameter strings, e.g. "1;97;104".  id 0 is the default. */
void term_style(int id, const char *sgr);

/* Drawing (back buffer). Coordinates are 0-based; all calls clip. */
void scr_fill(int row, int col, int w, uint32_t cp, int style);
void scr_clear(int style);
/* Draw UTF-8 text; skips `skip` leading display columns (tab-aware, from the
 * start of s), stops after maxw cells; returns cells drawn. */
int  scr_text(int row, int col, int maxw, const char *s, size_t len,
              int style, int skip);
int  scr_str(int row, int col, const char *s, int style);
void scr_cursor(int row, int col, int visible);

/* Send the minimal escape stream to turn the last frame into this one. */
void scr_flush(void);
/* Forget the terminal contents (after resize / child program output). */
void scr_invalidate(void);
/* Bytes written to the tty by scr_flush since the last reset (for tests). */
unsigned long scr_bytes(void);

/* Screen inspection & sizing for GUI terminal emulators (e.g. gterm) */
int  term_get_cell(int r, int c, uint32_t *cp, int *style);
void term_set_size(int rows, int cols);
void term_get_style_colors(int style, uint32_t *fg, uint32_t *bg);
void term_get_cursor(int *row, int *col, int *visible);

#endif /* CLU_TERM_H */

