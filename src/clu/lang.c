/*
 * B-System BTRON3 — lang.c
 * Terminal language system (see lang.h).  Pure C99, no allocation, no libc
 * dependency beyond <stdint.h>/<stddef.h>.
 */
#include "lang.h"

typedef struct { uint32_t lo, hi; } Range;

/* Wide (2-cell) code points: East-Asian W/F plus emoji.  Sorted, disjoint. */
static const Range k_wide[] = {
    {0x1100,0x115F},{0x231A,0x231B},{0x2329,0x232A},{0x23E9,0x23EC},
    {0x23F0,0x23F0},{0x23F3,0x23F3},{0x25FD,0x25FE},{0x2614,0x2615},
    {0x2648,0x2653},{0x267F,0x267F},{0x2693,0x2693},{0x26A1,0x26A1},
    {0x26AA,0x26AB},{0x26BD,0x26BE},{0x26C4,0x26C5},{0x26CE,0x26CE},
    {0x26D4,0x26D4},{0x26EA,0x26EA},{0x26F2,0x26F3},{0x26F5,0x26F5},
    {0x26FA,0x26FA},{0x26FD,0x26FD},{0x2705,0x2705},{0x270A,0x270B},
    {0x2728,0x2728},{0x274C,0x274C},{0x274E,0x274E},{0x2753,0x2755},
    {0x2757,0x2757},{0x2795,0x2797},{0x27B0,0x27B0},{0x27BF,0x27BF},
    {0x2B1B,0x2B1C},{0x2B50,0x2B50},{0x2B55,0x2B55},{0x2E80,0x303E},
    {0x3041,0x33FF},{0x3400,0x4DBF},{0x4E00,0xA4CF},{0xA960,0xA97F},
    {0xAC00,0xD7A3},{0xF900,0xFAFF},{0xFE10,0xFE19},{0xFE30,0xFE6F},
    {0xFF01,0xFF60},{0xFFE0,0xFFE6},{0x1F004,0x1F004},{0x1F0CF,0x1F0CF},
    {0x1F18E,0x1F18E},{0x1F191,0x1F19A},{0x1F200,0x1F2FF},{0x1F300,0x1F64F},
    {0x1F680,0x1F6FF},{0x1F7E0,0x1F7EB},{0x1F90C,0x1F9FF},{0x1FA70,0x1FAFF},
    {0x20000,0x3FFFD}
};

/* Zero-width: combining marks and format controls.  Sorted, disjoint. */
static const Range k_zero[] = {
    {0x0300,0x036F},{0x0483,0x0489},{0x0591,0x05BD},{0x05BF,0x05BF},
    {0x05C1,0x05C2},{0x05C4,0x05C5},{0x05C7,0x05C7},{0x0610,0x061A},
    {0x064B,0x065F},{0x0670,0x0670},{0x06D6,0x06DC},{0x06DF,0x06E4},
    {0x06E7,0x06E8},{0x06EA,0x06ED},{0x0900,0x0902},{0x093A,0x093A},
    {0x093C,0x093C},{0x0941,0x0948},{0x094D,0x094D},{0x0951,0x0957},
    {0x0962,0x0963},{0x0981,0x0981},{0x09BC,0x09BC},{0x09C1,0x09C4},
    {0x09CD,0x09CD},{0x0E31,0x0E31},{0x0E34,0x0E3A},{0x0E47,0x0E4E},
    {0x0EB1,0x0EB1},{0x0EB4,0x0EBC},{0x0EC8,0x0ECD},{0x0F18,0x0F19},
    {0x0F35,0x0F35},{0x0F37,0x0F37},{0x0F39,0x0F39},{0x0F71,0x0F7E},
    {0x0F80,0x0F84},{0x0F86,0x0F87},{0x0F8D,0x0FBC},{0x0FC6,0x0FC6},
    {0x1AB0,0x1AFF},{0x1DC0,0x1DFF},{0x200B,0x200F},{0x202A,0x202E},
    {0x2060,0x2064},{0x20D0,0x20FF},{0x302A,0x302D},{0x3099,0x309A},
    {0xFE00,0xFE0F},{0xFE20,0xFE2F},{0xFEFF,0xFEFF},{0xE0100,0xE01EF}
};

#define NELEM(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* Binary search; at most 6 iterations for these tables. */
static int in_ranges(const Range *t, int n, uint32_t cp)
{
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (cp < t[mid].lo) hi = mid - 1;
        else if (cp > t[mid].hi) lo = mid + 1;
        else return 1;
    }
    return 0;
}

static int sorted(const Range *t, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        if (t[i].lo > t[i].hi) return 0;
        if (i > 0 && t[i].lo <= t[i - 1].hi) return 0;
    }
    return 1;
}

int lang_tables_ok(void)
{
    return sorted(k_wide, NELEM(k_wide)) && sorted(k_zero, NELEM(k_zero));
}

/* ── UTF-8 ─────────────────────────────────────────────────────────── */

static int is_cont(const char *s, size_t len, size_t i)
{
    return i < len && ((unsigned char)s[i] & 0xC0u) == 0x80u;
}

size_t lang_decode(const char *s, size_t len, uint32_t *cp)
{
    unsigned char c;
    if (len == 0) { *cp = 0; return 0; }
    c = (unsigned char)s[0];
    *cp = LANG_REPL;
    if (c < 0x80u) { *cp = c; return 1; }
    if (c >= 0xC2u && c <= 0xDFu && is_cont(s, len, 1)) {
        *cp = ((uint32_t)(c & 0x1Fu) << 6) | ((unsigned char)s[1] & 0x3Fu);
        return 2;
    }
    if (c >= 0xE0u && c <= 0xEFu && is_cont(s, len, 1) && is_cont(s, len, 2)) {
        unsigned char c1 = (unsigned char)s[1];
        uint32_t v = ((uint32_t)(c & 0x0Fu) << 12) | ((uint32_t)(c1 & 0x3Fu) << 6)
                   | ((unsigned char)s[2] & 0x3Fu);
        if (v >= 0x800u && !(v >= 0xD800u && v <= 0xDFFFu)) { *cp = v; return 3; }
        return 1;
    }
    if (c >= 0xF0u && c <= 0xF4u && is_cont(s, len, 1) && is_cont(s, len, 2)
        && is_cont(s, len, 3)) {
        uint32_t v = ((uint32_t)(c & 0x07u) << 18)
                   | ((uint32_t)((unsigned char)s[1] & 0x3Fu) << 12)
                   | ((uint32_t)((unsigned char)s[2] & 0x3Fu) << 6)
                   | ((unsigned char)s[3] & 0x3Fu);
        if (v >= 0x10000u && v <= 0x10FFFFu) { *cp = v; return 4; }
    }
    return 1;
}

size_t lang_encode(uint32_t cp, char *out)
{
    if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) cp = LANG_REPL;
    if (cp < 0x80u) { out[0] = (char)cp; return 1; }
    if (cp < 0x800u) {
        out[0] = (char)(0xC0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3Fu));
        return 2;
    }
    if (cp < 0x10000u) {
        out[0] = (char)(0xE0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (cp & 0x3Fu));
        return 3;
    }
    out[0] = (char)(0xF0u | (cp >> 18));
    out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[3] = (char)(0x80u | (cp & 0x3Fu));
    return 4;
}

size_t lang_next(const char *s, size_t len, size_t pos)
{
    uint32_t cp;
    size_t n;
    if (pos >= len) return len;
    n = lang_decode(s + pos, len - pos, &cp);
    return pos + n;
}

size_t lang_prev(const char *s, size_t len, size_t pos)
{
    size_t i, n;
    uint32_t cp;
    if (pos > len) pos = len;
    if (pos == 0) return 0;
    i = pos - 1;
    while (i > 0 && (pos - i) < 4 && is_cont(s, len, i)) i--;
    n = lang_decode(s + i, len - i, &cp);
    return (i + n == pos) ? i : pos - 1;
}

/* ── Width ─────────────────────────────────────────────────────────── */

int lang_width(uint32_t cp)
{
    if (cp < 0x300u) return 1;               /* ASCII, Latin-1, controls */
    if (in_ranges(k_zero, NELEM(k_zero), cp)) return 0;
    if (in_ranges(k_wide, NELEM(k_wide), cp)) return 2;
    return 1;
}

int lang_adv(uint32_t cp, int col)
{
    if (cp == '\t') return LANG_TAB - (col % LANG_TAB);
    return lang_width(cp);
}

size_t lang_cl_next(const char *s, size_t len, size_t pos)
{
    size_t p = lang_next(s, len, pos);
    while (p < len) {                        /* bounded: p strictly grows */
        uint32_t cp;
        size_t n = lang_decode(s + p, len - p, &cp);
        if (lang_width(cp) != 0 || cp < 0x300u) break;
        p += n;
    }
    return p;
}

size_t lang_cl_prev(const char *s, size_t len, size_t pos)
{
    size_t p = lang_prev(s, len, pos);
    while (p > 0) {                          /* bounded: p strictly shrinks */
        uint32_t cp;
        (void)lang_decode(s + p, len - p, &cp);
        if (lang_width(cp) != 0 || cp < 0x300u) break;
        p = lang_prev(s, len, p);
    }
    return p;
}

int lang_cols(const char *s, size_t len)
{
    size_t p = 0;
    int col = 0;
    while (p < len) {
        uint32_t cp;
        size_t n = lang_decode(s + p, len - p, &cp);
        col += lang_adv(cp, col);
        p += n;
    }
    return col;
}

size_t lang_at_col(const char *s, size_t len, int col)
{
    size_t p = 0;
    int c = 0;
    while (p < len && c < col) {
        uint32_t cp;
        size_t n = lang_decode(s + p, len - p, &cp);
        c += lang_adv(cp, c);
        p += n;
    }
    return p;
}

/* ── Classes and line breaking ─────────────────────────────────────── */

static int is_space(uint32_t cp)
{
    return cp == ' ' || cp == '\t' || cp == 0xA0u || cp == 0x3000u;
}

int lang_wclass(uint32_t cp)
{
    if (is_space(cp)) return LANG_W_SPACE;
    if (cp < 0x80u) {
        if ((cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') ||
            (cp >= 'a' && cp <= 'z') || cp == '_') return LANG_W_WORD;
        return LANG_W_PUNCT;
    }
    if ((cp >= 0x2000u && cp <= 0x206Fu) || (cp >= 0x3001u && cp <= 0x303Fu &&
        cp != 0x3005u && cp != 0x3007u) || (cp >= 0xFF01u && cp <= 0xFF0Fu) ||
        (cp >= 0xFF1Au && cp <= 0xFF20u) || cp == 0x0F0Bu || cp == 0x0F0Du)
        return LANG_W_PUNCT;
    if (cp >= 0x3041u && cp <= 0x309Fu) return LANG_W_HIRA;
    if ((cp >= 0x30A0u && cp <= 0x30FFu) || (cp >= 0xFF66u && cp <= 0xFF9Fu))
        return LANG_W_KATA;
    if ((cp >= 0xAC00u && cp <= 0xD7A3u) || (cp >= 0x1100u && cp <= 0x11FFu) ||
        (cp >= 0x3130u && cp <= 0x318Fu)) return LANG_W_HANGUL;
    if ((cp >= 0x3400u && cp <= 0x4DBFu) || (cp >= 0x4E00u && cp <= 0x9FFFu) ||
        (cp >= 0xF900u && cp <= 0xFAFFu) || (cp >= 0x20000u && cp <= 0x3FFFFu) ||
        cp == 0x3005u || cp == 0x3007u) return LANG_W_HAN;
    return LANG_W_WORD;
}

/* Kinsoku: characters that must not start a row. */
static int no_start(uint32_t cp)
{
    switch (cp) {
    case 0x3001: case 0x3002: case 0x3003: case 0x3005: case 0x3009: case 0x300B:
    case 0x300D: case 0x300F: case 0x3011: case 0x3015: case 0x3017: case 0x3019:
    case 0x301F: case 0x30FB: case 0x30FC: case 0x30FD: case 0x30FE: case 0x309D:
    case 0x309E: case 0x3041: case 0x3043: case 0x3045: case 0x3047: case 0x3049:
    case 0x3063: case 0x3083: case 0x3085: case 0x3087: case 0x308E: case 0x30A1:
    case 0x30A3: case 0x30A5: case 0x30A7: case 0x30A9: case 0x30C3: case 0x30E3:
    case 0x30E5: case 0x30E7: case 0x30EE: case 0x30F5: case 0x30F6: case 0x2019:
    case 0x201D: case 0x2026: case 0xFF01: case 0xFF09: case 0xFF0C: case 0xFF0E:
    case 0xFF1A: case 0xFF1B: case 0xFF1F: case 0xFF3D: case 0xFF5D: case 0x0F0D:
    case 0x0F0E:
        return 1;
    default:
        return cp == ')' || cp == ']' || cp == '}' || cp == '.' || cp == ',' ||
               cp == ';' || cp == ':' || cp == '!' || cp == '?' || cp == '%';
    }
}

/* Kinsoku: characters that must not end a row. */
static int no_end(uint32_t cp)
{
    switch (cp) {
    case 0x3008: case 0x300A: case 0x300C: case 0x300E: case 0x3010: case 0x3014:
    case 0x3016: case 0x3018: case 0x301D: case 0x2018: case 0x201C: case 0xFF08:
    case 0xFF3B: case 0xFF5B:
        return 1;
    default:
        return cp == '(' || cp == '[' || cp == '{';
    }
}

static int is_ideo(int wc)
{
    return wc == LANG_W_HAN || wc == LANG_W_HIRA || wc == LANG_W_KATA;
}

/* May a row end between a (left) and b (right)? */
static int can_break(uint32_t a, uint32_t b)
{
    if (is_space(a)) return !is_space(b);
    if (is_space(b)) return 0;               /* spaces hang on the old row */
    if (no_start(b) || no_end(a)) return 0;
    if (a == 0x0F0Bu || a == 0x0F0Du) return 1;           /* Tibetan tsheg/shad */
    if (a == '-' && lang_wclass(b) == LANG_W_WORD) return 1;
    return is_ideo(lang_wclass(a)) || is_ideo(lang_wclass(b));
}

size_t lang_wrap(const char *s, size_t len, int width, int mode)
{
    size_t pos = 0, brk = 0;
    int col = 0;
    uint32_t prev = 0;
    if (len == 0) return 0;
    if (mode == LANG_WRAP_NONE) return len;
    if (width < 2) width = 2;
    while (pos < len) {                      /* bounded: pos strictly grows */
        uint32_t cp;
        size_t n = lang_decode(s + pos, len - pos, &cp);
        int adv = lang_adv(cp, col);
        if (pos > 0 && mode == LANG_WRAP_WORD && can_break(prev, cp)) brk = pos;
        if (col + adv > width && pos > 0) {
            if (is_space(cp)) { pos += n; prev = cp; continue; }  /* hang */
            if (mode == LANG_WRAP_WORD && brk > 0) return brk;
            return pos;
        }
        if (pos == 0 && col + adv > width) return n;   /* always progress */
        col += adv;
        pos += n;
        prev = cp;
    }
    return len;
}
