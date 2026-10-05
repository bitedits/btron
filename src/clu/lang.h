/*
 * B-System BTRON3 — lang.h
 * Terminal language system: strict UTF-8, cell widths (CJK, Tibetan,
 * Indic/Thai combining marks), kinsoku line breaking and word classes.
 *
 * Replaces Terminal Vision's utf8.c.  All functions are total: invalid
 * input never reads out of bounds, it decodes to U+FFFD one byte at a time,
 * so a decode/encode walk always makes forward progress.
 */
#ifndef CLU_LANG_H
#define CLU_LANG_H

#include <stddef.h>
#include <stdint.h>

#define LANG_REPL 0xFFFDu
#define LANG_TAB  4

enum { LANG_WRAP_NONE = 0, LANG_WRAP_CHAR = 1, LANG_WRAP_WORD = 2, LANG_WRAP_MODES = 3 };

/* Word classes for Ctrl-Left/Right motion. */
enum { LANG_W_SPACE = 0, LANG_W_PUNCT, LANG_W_WORD, LANG_W_HAN,
       LANG_W_HIRA, LANG_W_KATA, LANG_W_HANGUL };

/* Decode one char at s (len>0). Returns bytes consumed (>=1); 0 iff len==0. */
size_t lang_decode(const char *s, size_t len, uint32_t *cp);
/* Encode cp into out[4]; returns 1..4 (invalid cp encodes U+FFFD). */
size_t lang_encode(uint32_t cp, char *out);

/* Byte index after / before the char at pos; clamped to [0,len]. */
size_t lang_next(const char *s, size_t len, size_t pos);
size_t lang_prev(const char *s, size_t len, size_t pos);
/* Same but over a whole cluster (base char + trailing zero-width marks). */
size_t lang_cl_next(const char *s, size_t len, size_t pos);
size_t lang_cl_prev(const char *s, size_t len, size_t pos);

/* Terminal cells: 0 (combining), 1 or 2.  Control chars report 1. */
int lang_width(uint32_t cp);
/* Advance at display column col (handles TAB stops). */
int lang_adv(uint32_t cp, int col);
/* Display columns of s[0,len) starting at column 0. */
int lang_cols(const char *s, size_t len);
/* Byte offset of the cluster that covers display column col (<= len). */
size_t lang_at_col(const char *s, size_t len, int col);

int lang_wclass(uint32_t cp);

/*
 * Bytes of the next visual row of s[0,len) for a row `width` cells wide.
 * mode NONE returns len.  Always returns >=1 when len>0 (width is clamped
 * to >=2 by the caller).  WORD mode breaks at spaces, after hyphens, after
 * Tibetan tsheg and between CJK chars honouring kinsoku (no line start with
 * closing punctuation / small kana, no line end with opening brackets).
 */
size_t lang_wrap(const char *s, size_t len, int width, int mode);

/* Self-check hooks for the unit tests: returns 1 if all tables are sorted. */
int lang_tables_ok(void);

#endif /* CLU_LANG_H */
