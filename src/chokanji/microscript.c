/*
 * B-TRON Retro OS — src/chokanji/microscript.c
 *
 * Clean-room MicroScript / Programming-Language-T hypermedia interpreter and
 * runtime for B-System. Grammar, keywords, types, operators, built-in
 * functions and system variables follow the official PMC (Personal Media Corp)
 * Chokanji command-name correspondence table — NOT HyperTalk and NOT any
 * third-party reimplementation.
 *
 * Language model implemented here (authentic surface, bounded subset):
 *   - C-family, case-insensitive, block-structured (explicit ENDxxx, no braces).
 *   - Types G/S/F/I/C/B declared via VARIABLE / LOCAL name:type.
 *   - Arrow or '=' assignment; '#' line comments; ';' / newline statements;
 *     '\' line continuations; full-width operator aliases.
 *   - Control flow: IF/ELSEIF/ELSE/ENDIF, WHILE/ENDWHILE, REPEAT/ENDREPEAT,
 *     SWITCH/CASE/DEFAULT/ENDCASE, BREAK, CONTINUE.
 *   - Procedures: SCRIPT name(params) … END, CALL, EXIT (return value).
 *   - Actor / stage commands: SCENE, APPEAR, DISAPPEAR, MOVE, TEXT, LOG,
 *     UPDATE, BEEP, WAIT, SLEEP, SET.
 *   - Built-ins: arithmetic / trig / string functions from the official table.
 *   - System variables: SCRW, SCRH, CNT, RAND, TIME, MSEC, ERR, VERS, PID.
 *
 * NASA JPL "Power of 10" discipline:
 *   - Zero dynamic allocation after init; every structure is a fixed array.
 *   - Every loop is bounded (lex, compile, evaluate, render, dispatch).
 *   - Recursion (expression parse, procedure call) is depth-capped.
 *   - Execution is step-capped so a runaway script cannot hang the runtime.
 *   - Pure client-local coordinate space (0,0)..(w,h) on wnd->dev.
 */

#include <btron/types.h>
#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/pmc.h>
#include <btron/troncode.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>
#include <time.h>

/* ── Fixed footprint contracts ──────────────────────────────────────── */

#define MS_MAX_CARDS       8
#define MS_MAX_FIGS        16
#define MS_MAX_VARS        48
#define MS_MAX_PROCS       16
#define MS_MAX_PARAMS      8
#define MS_MAX_LINES       200
#define MS_MAX_ARGS        16
#define MS_MAX_CALL        16
#define MS_MAX_STR_LEN     64
#define MS_LINE_LEN        160
#define MS_NAME_LEN        24
#define MS_EXPR_DEPTH      24
#define MS_LOOP_BOUND      512
#define MS_MAX_STEPS       200000LL
#define MS_SWITCH_STACK    8
#define MS_IF_STACK        16
#define MS_LOOP_STACK      16
#define MS_BREAK_PER_LOOP  24

/* ── Value model (G/S/F/I/C/B surface) ──────────────────────────────── */

typedef enum { VT_EMPTY = 0, VT_INT, VT_REAL, VT_STR } VType;

typedef struct {
    VType    t;
    long long i;
    double   r;
    char     s[MS_MAX_STR_LEN];
} Value;

typedef struct {
    char      name[MS_NAME_LEN];
    VType     t;
    long long i;
    double    r;
    char      str_val[MS_MAX_STR_LEN];
} MsVar;

typedef enum { FIG_BUTTON = 0, FIG_LABEL, FIG_RECT, FIG_LINE } FigType;

typedef struct {
    char    name[MS_NAME_LEN];
    FigType type;
    RECT    bounds;
    char    text[MS_MAX_STR_LEN];
    COLOR   fill_col;
    COLOR   border_col;
    int     target_card;
    bool    is_pressed;
    bool    visible;
} MsFigure;

typedef struct {
    char     title[32];
    MsFigure figures[MS_MAX_FIGS];
    int      fig_count;
    char     script[512];
} MsCard;

typedef struct {
    char name[MS_NAME_LEN];
    char params[MS_MAX_PARAMS][MS_NAME_LEN];
    int  nparams;
    int  start;   /* index of the SCRIPT line  */
    int  end;     /* index of the matching END */
    int  defined;
} MsProc;

typedef enum {
    S_PLAIN = 0, S_IF, S_ELSEIF, S_ELSE, S_ENDIF,
    S_WHILE, S_ENDWHILE, S_REPEAT, S_ENDREPEAT,
    S_SWITCH, S_CASE, S_DEFAULT, S_ENDCASE,
    S_SCRIPT, S_END, S_BREAK, S_CONTINUE, S_EXIT
} SType;

typedef struct {
    char  text[MS_LINE_LEN];
    SType st;
    int   j1;   /* IF/ELSEIF: first branch idx · ENDWHILE/ENDREPEAT: loop head idx · BREAK/CONTINUE target */
    int   j2;   /* matching terminator idx (ENDIF/ENDWHILE/ENDREPEAT/ENDCASE/END)                          */
} Stmt;

typedef struct {
    WND   *wnd;
    MsCard cards[MS_MAX_CARDS];
    int    card_count;
    int    current_card;
    MsVar  vars[MS_MAX_VARS];
    int    var_count;
    char   console_log[128];
    bool   is_running;

    /* interpreter state */
    Stmt   prog[MS_MAX_LINES];
    int    nlines;
    MsProc procs[MS_MAX_PROCS];
    int    nprocs;
    int    call_ret[MS_MAX_CALL];
    int    call_depth;
    Value  retval;
    int    enter;   /* branch idx we intentionally jumped into (IF/ELSEIF/ELSE chain) */
    int    sw_end[MS_SWITCH_STACK];
    Value  sw_val[MS_SWITCH_STACK];
    int    sw_sp;
    long long steps;
    long long cnt;  /* loop counter exposed as system variable CNT */
    int    err;
} MsEngine;

static MsEngine g_ms;
static int      g_pc;   /* live program counter, shared by the executor + call_proc */

/* ── Small helpers ──────────────────────────────────────────────────── */

static void ms_log(const char *msg) {
    assert(msg != NULL);
    strncpy(g_ms.console_log, msg, sizeof(g_ms.console_log) - 1);
    g_ms.console_log[sizeof(g_ms.console_log) - 1] = '\0';
}

static int ch_lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

/* ASCII-insensitive, byte-exact otherwise (kanji passes straight through). */
static int name_eq(const char *a, const char *b) {
    while (*a && *b) {
        if (ch_lower((unsigned char)*a) != ch_lower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == *b;
}

static int is_ident_start(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           ((unsigned char)c >= 0x80);
}
static int is_ident_char(int c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

/* Copy leading word (ASCII-lowered); return its byte length. */
static int lead_word(const char *s, char *out, int outsz) {
    int n = 0;
    while (s[n] && is_ident_char((unsigned char)s[n])) n++;
    int m = 0;
    for (int i = 0; i < n && m < outsz - 1; i++) out[m++] = (char)ch_lower((unsigned char)s[i]);
    out[m] = '\0';
    return n;
}

static void str_trim(char *s) {
    size_t i = 0, n;
    while (s[i] == ' ' || s[i] == '\t' || s[i] == '\r') i++;
    if (i) memmove(s, s + i, strlen(s + i) + 1);
    n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) s[--n] = '\0';
}

/* Pointer to text after the leading keyword and following spaces. */
static const char *keyword_operand(const char *text) {
    const char *p = text;
    while (*p && is_ident_char((unsigned char)*p)) p++;
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

/* ── Value conversion helpers ───────────────────────────────────────── */

static Value v_int(long long x) { Value v; memset(&v, 0, sizeof v); v.t = VT_INT;  v.i = x; return v; }
static Value v_real(double x)  { Value v; memset(&v, 0, sizeof v); v.t = VT_REAL; v.r = x; v.i = (long long)x; return v; }
static Value v_empty(void)     { Value v; memset(&v, 0, sizeof v); v.t = VT_EMPTY; return v; }
static Value v_str_of(const char *s) { Value v; memset(&v, 0, sizeof v); v.t = VT_STR; strncpy(v.s, s, sizeof v.s - 1); return v; }

static void v_str(Value *v) {
    if (v->t == VT_STR) return;
    if (v->t == VT_INT)       snprintf(v->s, sizeof v->s, "%lld", v->i);
    else if (v->t == VT_REAL) snprintf(v->s, sizeof v->s, "%g", v->r);
    else v->s[0] = '\0';
    v->t = VT_STR;
}

static double val_real(const Value *v) {
    if (v->t == VT_REAL) return v->r;
    if (v->t == VT_INT)  return (double)v->i;
    if (v->t == VT_STR)  return atof(v->s);
    return 0.0;
}
static long long val_int(const Value *v) {
    if (v->t == VT_INT)  return v->i;
    if (v->t == VT_REAL) return (long long)v->r;
    if (v->t == VT_STR)  return strtoll(v->s, NULL, 10);
    return 0;
}
static int truthy(const Value *v) {
    if (v->t == VT_STR)  return v->s[0] != '\0';
    if (v->t == VT_REAL) return v->r != 0.0;
    return v->i != 0;
}
static int val_cmp(const Value *a, const Value *b) {
    if (a->t == VT_STR && b->t == VT_STR) return strcmp(a->s, b->s);
    double x = val_real(a), y = val_real(b);
    return (x < y) ? -1 : (x > y ? 1 : 0);
}

/* ── Variable table (shared with the public C API) ──────────────────── */

static MsVar *var_find(const char *name) {
    for (int i = 0; i < g_ms.var_count && i < MS_LOOP_BOUND; i++)
        if (name_eq(g_ms.vars[i].name, name)) return &g_ms.vars[i];
    return NULL;
}
static MsVar *var_touch(const char *name) {
    MsVar *v = var_find(name);
    if (v) return v;
    if (g_ms.var_count >= MS_MAX_VARS) return NULL;
    v = &g_ms.vars[g_ms.var_count++];
    memset(v, 0, sizeof *v);
    strncpy(v->name, name, sizeof v->name - 1);
    v->t = VT_INT;
    return v;
}
static void var_set(const char *name, Value val) {
    MsVar *v = var_touch(name);
    if (!v) return;
    v->t = val.t;
    v->i = val.i;
    v->r = (val.t == VT_INT) ? (double)val.i : val.r;
    if (val.t == VT_STR) { strncpy(v->str_val, val.s, sizeof v->str_val - 1); v->str_val[sizeof v->str_val - 1] = '\0'; }
    else v->str_val[0] = '\0';
}

static int sysvar(const char *name, Value *out) {
    char buf[MS_NAME_LEN];
    int m = 0;
    for (; name[m] && m < MS_NAME_LEN - 1; m++) buf[m] = (char)ch_lower((unsigned char)name[m]);
    buf[m] = '\0';
    if (strcmp(buf, "scrw") == 0) { *out = v_int(g_ms.wnd && g_ms.wnd->dev ? g_ms.wnd->dev->width  : 0); return 1; }
    if (strcmp(buf, "scrh") == 0) { *out = v_int(g_ms.wnd && g_ms.wnd->dev ? g_ms.wnd->dev->height : 0); return 1; }
    if (strcmp(buf, "cnt") == 0)  { *out = v_int(g_ms.cnt); return 1; }
    if (strcmp(buf, "rand") == 0) { *out = v_int(rand());   return 1; }
    if (strcmp(buf, "pid") == 0)  { *out = v_int(1);        return 1; }
    if (strcmp(buf, "tid") == 0)  { *out = v_int(0);        return 1; }
    if (strcmp(buf, "err") == 0)  { *out = v_int(g_ms.err); return 1; }
    if (strcmp(buf, "vers") == 0) { *out = v_str_of("MicroScript 3.20"); return 1; }
    if (strcmp(buf, "time") == 0 || strcmp(buf, "msec") == 0) {
        *out = v_int((long long)clock() * 1000 / CLOCKS_PER_SEC); return 1;
    }
    return 0;
}

static Value var_get(const char *name) {
    MsVar *v = var_find(name);
    Value out;
    if (v) {
        memset(&out, 0, sizeof out);
        out.t = v->t; out.i = v->i; out.r = v->r;
        strncpy(out.s, v->str_val, sizeof out.s - 1);
        return out;
    }
    if (sysvar(name, &out)) return out;
    return v_empty();
}

void ms_set_var(const char *name, int val, const char *str) {
    assert(name != NULL);
    MsVar *v = var_touch(name);
    if (!v) return;
    v->t = VT_INT; v->i = val; v->r = val;
    if (str) { strncpy(v->str_val, str, sizeof v->str_val - 1); v->str_val[sizeof v->str_val - 1] = '\0'; }
    else v->str_val[0] = '\0';
}

int ms_get_var(const char *name) {
    assert(name != NULL);
    Value v = var_get(name);
    return (int)val_int(&v);
}

/* ── Expression evaluator (recursive descent, depth-capped) ─────────── */

typedef struct { const char *s; int p; int depth; int err; } Cx;

static void cx_ws(Cx *c) { while (c->s[c->p] == ' ' || c->s[c->p] == '\t') c->p++; }

static Value ev_expr(Cx *c, int min_prec);

/* Peek a binary operator at c->p: fill op[] (<=2 chars + NUL), set *prec,
 * return its length, or 0 if the next token is not a binary operator. */
static int op_peek(Cx *c, char *op, int *prec) {
    const char *s = c->s + c->p;
    static const struct { const char *o; int p; } t2[] = {
        {"||",1},{"&&",2},{"==",3},{"!=",3},{"<=",4},{">=",4},{">>",5},{"<<",5}
    };
    for (int k = 0; k < 8; k++)
        if (s[0] == t2[k].o[0] && s[1] == t2[k].o[1]) { op[0] = s[0]; op[1] = s[1]; op[2] = 0; *prec = t2[k].p; return 2; }
    static const struct { char o; int p; } t1[] = {
        {'|',6},{'^',7},{'&',8},{'=',3},{'<',4},{'>',4},{'+',9},{'-',9},{'*',10},{'/',10},{'%',10}
    };
    for (int k = 0; k < 11; k++)
        if (s[0] == t1[k].o && !(s[0] == '=' && s[1] == '=')) { op[0] = s[0]; op[1] = 0; op[2] = 0; *prec = t1[k].p; return 1; }
    return 0;
}

static Value apply_bin(const char *op, Value l, Value r) {
    int isstr = (l.t == VT_STR || r.t == VT_STR);
    int two   = (op[1] != '\0');

    if (op[0] == '+' && isstr) {
        Value a = l, b = r, out; memset(&out, 0, sizeof out); out.t = VT_STR;
        v_str(&a); v_str(&b);
        strncpy(out.s, a.s, sizeof out.s - 1);
        size_t la = strlen(out.s);
        if (la < sizeof out.s - 1) strncpy(out.s + la, b.s, sizeof out.s - 1 - la);
        out.s[sizeof out.s - 1] = '\0';
        return out;
    }
    if (op[0] == '+' && !isstr) return (l.t == VT_REAL || r.t == VT_REAL) ? v_real(val_real(&l) + val_real(&r)) : v_int(val_int(&l) + val_int(&r));
    if (op[0] == '-' && !two)   return v_real(val_real(&l) - val_real(&r));
    if (op[0] == '*' && !two)   return v_real(val_real(&l) * val_real(&r));
    if (op[0] == '/' && !two) {
        double d = val_real(&r);
        if (d == 0.0) { g_ms.err = 1; return v_int(0); }
        if (!isstr && l.t == VT_INT && r.t == VT_INT) return v_int(val_int(&l) / val_int(&r));
        return v_real(val_real(&l) / d);
    }
    if (op[0] == '%' && !two) {
        long long b = val_int(&r);
        if (b == 0) { g_ms.err = 1; return v_int(0); }
        return v_int(val_int(&l) % b);
    }
    if (strcmp(op, "&&") == 0) return v_int(truthy(&l) && truthy(&r));
    if (strcmp(op, "||") == 0) return v_int(truthy(&l) || truthy(&r));
    if (strcmp(op, "==") == 0 || (op[0] == '=' && !two)) return v_int(val_cmp(&l, &r) == 0);
    if (strcmp(op, "!=") == 0) return v_int(val_cmp(&l, &r) != 0);
    if (strcmp(op, "<=") == 0) return v_int(val_cmp(&l, &r) <= 0);
    if (strcmp(op, ">=") == 0) return v_int(val_cmp(&l, &r) >= 0);
    if (strcmp(op, ">>") == 0) return v_int(val_int(&l) >> val_int(&r));
    if (strcmp(op, "<<") == 0) return v_int(val_int(&l) << val_int(&r));
    if (op[0] == '<' && !two) return v_int(val_cmp(&l, &r) < 0);
    if (op[0] == '>' && !two) return v_int(val_cmp(&l, &r) > 0);
    if (op[0] == '&' && !two) return v_int(val_int(&l) & val_int(&r));
    if (op[0] == '|' && !two) return v_int(val_int(&l) | val_int(&r));
    if (op[0] == '^' && !two) return v_int(val_int(&l) ^ val_int(&r));
    return v_int(0);
}

static Value builtin(const char *id, Value *a, int n) {
    double x = n > 0 ? val_real(&a[0]) : 0.0;
    double y = n > 1 ? val_real(&a[1]) : 0.0;
    if (strcmp(id, "sqrt") == 0)   return v_real(sqrt(x));
    if (strcmp(id, "abs") == 0 || strcmp(id, "fabs") == 0) return v_real(fabs(x));
    if (strcmp(id, "exp") == 0)    return v_real(exp(x));
    if (strcmp(id, "log") == 0)    return v_real(x > 0 ? log(x) : 0.0);
    if (strcmp(id, "log10") == 0)  return v_real(x > 0 ? log10(x) : 0.0);
    if (strcmp(id, "sin") == 0)    return v_real(sin(x));
    if (strcmp(id, "cos") == 0)    return v_real(cos(x));
    if (strcmp(id, "tan") == 0)    return v_real(tan(x));
    if (strcmp(id, "asin") == 0)   return v_real(asin(x));
    if (strcmp(id, "acos") == 0)   return v_real(acos(x));
    if (strcmp(id, "atan") == 0)   return v_real(atan2(x, y));
    if (strcmp(id, "pow") == 0)    return v_real(pow(x, y));
    if (strcmp(id, "ceil") == 0)   return v_real(ceil(x));
    if (strcmp(id, "floor") == 0)  return v_real(floor(x));
    if (strcmp(id, "round") == 0)  return v_real(floor(x + 0.5));
    if (strcmp(id, "max") == 0)    return v_real(x > y ? x : y);
    if (strcmp(id, "min") == 0)    return v_real(x < y ? x : y);
    if (strcmp(id, "slen") == 0)   { Value s = a[0]; v_str(&s); return v_int((long long)strlen(s.s)); }
    if (strcmp(id, "scmp") == 0 || strcmp(id, "acmp") == 0) return v_int(val_cmp(&a[0], &a[1]));
    if (strcmp(id, "number") == 0) { Value s = a[0]; v_str(&s); char *e; strtod(s.s, &e); return v_int(s.s[0] && *e == '\0'); }
    if (strcmp(id, "strnum") == 0) {
        Value s = a[0]; v_str(&s); char *e; double d = strtod(s.s, &e);
        if (e == s.s) return v_int(0);
        return strchr(s.s, '.') ? v_real(d) : v_int((long long)d);
    }
    if (strcmp(id, "valid") == 0)  return v_int(a[0].t != VT_EMPTY);
    return v_int(0); /* declared but inert in this bounded build */
}

static Value ev_primary(Cx *c) {
    cx_ws(c);
    char ch = c->s[c->p];
    if (ch == '\0') { c->err = 1; return v_int(0); }

    if (ch == '(') {
        c->p++;
        Value v = ev_expr(c, 1);
        cx_ws(c);
        if (c->s[c->p] == ')') c->p++; else c->err = 1;
        return v;
    }
    if (ch == '"') {
        Value v; memset(&v, 0, sizeof v); v.t = VT_STR;
        c->p++;
        int k = 0;
        while (c->s[c->p] && c->s[c->p] != '"') {
            char d = c->s[c->p];
            if (d == '\\' && c->s[c->p + 1]) {
                c->p++;
                char e = c->s[c->p];
                d = (e == 'n') ? '\n' : (e == 't') ? '\t' : e;
            }
            if (k < MS_MAX_STR_LEN - 1) v.s[k++] = d;
            c->p++;
        }
        v.s[k] = '\0';
        if (c->s[c->p] == '"') c->p++; else c->err = 1;
        return v;
    }
    if ((ch >= '0' && ch <= '9') || ch == '.') {
        const char *start = c->s + c->p;
        int isreal = (ch == '.');
        while (is_ident_char((unsigned char)c->s[c->p]) || c->s[c->p] == '.') {
            if (c->s[c->p] == '.' || c->s[c->p] == 'e' || c->s[c->p] == 'E') isreal = 1;
            c->p++;
        }
        char num[40];
        size_t n = (size_t)((c->s + c->p) - start);
        if (n >= sizeof num) n = sizeof num - 1;
        memcpy(num, start, n); num[n] = '\0';
        return isreal ? v_real(atof(num)) : v_int(strtoll(num, NULL, 10));
    }
    if (is_ident_start((unsigned char)ch)) {
        char id[MS_NAME_LEN];
        int k = 0;
        while (is_ident_char((unsigned char)c->s[c->p]) && k < MS_NAME_LEN - 1)
            id[k++] = (char)ch_lower((unsigned char)c->s[c->p++]);
        id[k] = '\0';
        cx_ws(c);
        if (c->s[c->p] == '(') {
            Value args[MS_MAX_ARGS]; int n = 0;
            c->p++;
            cx_ws(c);
            if (c->s[c->p] != ')') {
                while (n < MS_MAX_ARGS) {
                    args[n++] = ev_expr(c, 1);
                    cx_ws(c);
                    if (c->s[c->p] == ',') { c->p++; continue; }
                    break;
                }
            }
            cx_ws(c);
            if (c->s[c->p] == ')') c->p++; else c->err = 1;
            return builtin(id, args, n);
        }
        return var_get(id);
    }
    c->err = 1;
    return v_int(0);
}

static Value ev_unary(Cx *c) {
    cx_ws(c);
    char ch = c->s[c->p];
    if (ch == '-') { c->p++; Value v = ev_unary(c); return (v.t == VT_REAL) ? v_real(-v.r) : v_int(-val_int(&v)); }
    if (ch == '+') { c->p++; return ev_unary(c); }
    if (ch == '!') { c->p++; Value v = ev_unary(c); return v_int(!truthy(&v)); }
    if (ch == '~') { c->p++; Value v = ev_unary(c); return v_int(~val_int(&v)); }
    return ev_primary(c);
}

static Value ev_expr(Cx *c, int min_prec) {
    if (++c->depth > MS_EXPR_DEPTH) { c->err = 1; return v_int(0); }
    Value left = ev_unary(c);
    for (;;) {
        cx_ws(c);
        char op[3]; int prec;
        int len = op_peek(c, op, &prec);
        if (len == 0 || prec < min_prec) break;
        c->p += len;
        Value right = ev_expr(c, prec + 1);
        left = apply_bin(op, left, right);
    }
    c->depth--;
    return left;
}

static Value eval_string(const char *e) {
    Cx c; c.s = e; c.p = 0; c.depth = 0; c.err = 0;
    Value v = ev_expr(&c, 1);
    cx_ws(&c);
    if (c.s[c.p] != '\0') c.err = 1;
    if (c.err) g_ms.err = 1;
    return v;
}

/* Evaluate an operand that may be a well-formed expression, else fall back to
 * the raw literal text (so `LOG Hello World` prints "Hello World"). */
static Value eval_or_literal(const char *e) {
    int save = g_ms.err;
    g_ms.err = 0;
    Value v = eval_string(e);
    if (g_ms.err) { g_ms.err = save; return v_str_of(e); }
    return v;
}

/* ── Source splitting + operator normalization ──────────────────────── */

static void normalize_ops(const char *in, char *out, int outsz) {
    int o = 0, q = 0;
    for (int i = 0; in[i] && o < outsz - 4; ) {
        unsigned char b = (unsigned char)in[i];
        if (b == '"' && (i == 0 || in[i - 1] != '\\')) q = !q;
        if (!q && b == 0xE2 && (unsigned char)in[i + 1] == 0x89) {
            unsigned char c2 = (unsigned char)in[i + 2];
            if (c2 == 0xA0) { out[o++] = '!'; out[o++] = '='; i += 3; continue; } /* ≠ */
            if (c2 == 0xA7) { out[o++] = '>'; out[o++] = '='; i += 3; continue; } /* ≧ */
            if (c2 == 0xA6) { out[o++] = '<'; out[o++] = '='; i += 3; continue; } /* ≦ */
        }
        if (!q && b == 0xE2 && (unsigned char)in[i + 1] == 0x86 && (unsigned char)in[i + 2] == 0x90) { out[o++] = '='; i += 3; continue; } /* ← */
        if (!q && b == 0xC3 && (unsigned char)in[i + 1] == 0x97) { out[o++] = '*'; i += 2; continue; } /* × */
        if (!q && b == 0xC3 && (unsigned char)in[i + 1] == 0xB7) { out[o++] = '/'; i += 2; continue; } /* ÷ */
        out[o++] = in[i++];
    }
    out[o] = '\0';
}

static void push_line(const char *text) {
    if (g_ms.nlines >= MS_MAX_LINES) return;
    Stmt *s = &g_ms.prog[g_ms.nlines];
    memset(s, 0, sizeof *s);
    strncpy(s->text, text, sizeof s->text - 1);
    s->j1 = -1; s->j2 = -1; s->st = S_PLAIN;
    g_ms.nlines++;
}

static void split_statements(const char *script) {
    g_ms.nlines = 0;
    char cur[MS_LINE_LEN], norm[MS_LINE_LEN];
    int cl = 0, q = 0;
    size_t i = 0, n = strlen(script);
    while (i <= n) {
        char ch = (i < n) ? script[i] : '\0';
        if (ch == '\0' || ch == '\n' || (ch == ';' && !q)) {
            cur[cl] = '\0';
            normalize_ops(cur, norm, sizeof norm);
            str_trim(norm);
            if (norm[0]) push_line(norm);
            cl = 0; q = 0; i++; continue;
        }
        if (ch == '#' && !q) { while (i < n && script[i] != '\n') i++; continue; }
        if (ch == '\\' && i + 1 < n && script[i + 1] == '\n') { i += 2; continue; }
        if (ch == '"') q = !q;
        if (cl < MS_LINE_LEN - 1) cur[cl++] = ch;
        i++;
    }
}

/* ── Compile pass: classify keywords + resolve block jumps ──────────── */

static void classify(void) {
    for (int i = 0; i < g_ms.nlines; i++) {
        char kw[MS_NAME_LEN];
        lead_word(g_ms.prog[i].text, kw, sizeof kw);
        Stmt *s = &g_ms.prog[i];
        if      (!strcmp(kw, "if"))        s->st = S_IF;
        else if (!strcmp(kw, "elseif"))    s->st = S_ELSEIF;
        else if (!strcmp(kw, "else"))      s->st = S_ELSE;
        else if (!strcmp(kw, "endif"))     s->st = S_ENDIF;
        else if (!strcmp(kw, "while"))     s->st = S_WHILE;
        else if (!strcmp(kw, "endwhile"))  s->st = S_ENDWHILE;
        else if (!strcmp(kw, "repeat"))    s->st = S_REPEAT;
        else if (!strcmp(kw, "endrepeat")) s->st = S_ENDREPEAT;
        else if (!strcmp(kw, "switch"))    s->st = S_SWITCH;
        else if (!strcmp(kw, "case"))      s->st = S_CASE;
        else if (!strcmp(kw, "default"))   s->st = S_DEFAULT;
        else if (!strcmp(kw, "endcase"))   s->st = S_ENDCASE;
        else if (!strcmp(kw, "script") || !strcmp(kw, "func") ||
                 !strcmp(kw, "action") || !strcmp(kw, "segment"))
                                           s->st = S_SCRIPT;
        else if (!strcmp(kw, "end"))       s->st = S_END;
        else if (!strcmp(kw, "break"))     s->st = S_BREAK;
        else if (!strcmp(kw, "continue"))  s->st = S_CONTINUE;
        else if (!strcmp(kw, "exit"))      s->st = S_EXIT;
        else                               s->st = S_PLAIN;
    }

    int if_sp = 0, loop_sp = 0, sw_sp = 0, proc_sp = 0;
    int  if_open[MS_IF_STACK], if_branch[MS_IF_STACK];
    int  sw_open[MS_SWITCH_STACK];
    int  proc_open[MS_IF_STACK];
    int  lp_open[MS_LOOP_STACK], lp_iswh[MS_LOOP_STACK];
    int  lp_brk[MS_LOOP_STACK][MS_BREAK_PER_LOOP], lp_nbrk[MS_LOOP_STACK];
    int  lp_con[MS_LOOP_STACK][MS_BREAK_PER_LOOP], lp_ncon[MS_LOOP_STACK];

    for (int i = 0; i < g_ms.nlines; i++) {
        Stmt *s = &g_ms.prog[i];
        switch (s->st) {
        case S_IF:
            if (if_sp < MS_IF_STACK) { if_open[if_sp] = i; if_branch[if_sp] = i; if_sp++; }
            break;
        case S_ELSEIF:
        case S_ELSE:
            if (if_sp > 0) { int prev = if_branch[if_sp - 1]; g_ms.prog[prev].j1 = i; if_branch[if_sp - 1] = i; }
            break;
        case S_ENDIF:
            if (if_sp > 0) {
                if_sp--;
                int b = if_open[if_sp];
                while (b >= 0) {
                    g_ms.prog[b].j2 = i;
                    int nx = g_ms.prog[b].j1;
                    if (nx < 0 || (g_ms.prog[nx].st != S_ELSEIF && g_ms.prog[nx].st != S_ELSE)) break;
                    b = nx;
                }
            }
            break;
        case S_WHILE:
        case S_REPEAT:
            if (loop_sp < MS_LOOP_STACK) {
                lp_open[loop_sp] = i; lp_iswh[loop_sp] = (s->st == S_WHILE);
                lp_nbrk[loop_sp] = 0; lp_ncon[loop_sp] = 0; loop_sp++;
            }
            break;
        case S_ENDWHILE:
        case S_ENDREPEAT:
            if (loop_sp > 0) {
                loop_sp--;
                s->j1 = lp_open[loop_sp];
                g_ms.prog[lp_open[loop_sp]].j2 = i;
                for (int b = 0; b < lp_nbrk[loop_sp]; b++) g_ms.prog[lp_brk[loop_sp][b]].j1 = i + 1;
                for (int cn = 0; cn < lp_ncon[loop_sp]; cn++)
                    g_ms.prog[lp_con[loop_sp][cn]].j1 = lp_iswh[loop_sp] ? lp_open[loop_sp] : i;
            }
            break;
        case S_BREAK:
            if (loop_sp > 0 && lp_nbrk[loop_sp - 1] < MS_BREAK_PER_LOOP) lp_brk[loop_sp - 1][lp_nbrk[loop_sp - 1]++] = i;
            break;
        case S_CONTINUE:
            if (loop_sp > 0 && lp_ncon[loop_sp - 1] < MS_BREAK_PER_LOOP) lp_con[loop_sp - 1][lp_ncon[loop_sp - 1]++] = i;
            break;
        case S_SWITCH:
            if (sw_sp < MS_SWITCH_STACK) sw_open[sw_sp++] = i;
            break;
        case S_ENDCASE:
            if (sw_sp > 0) g_ms.prog[sw_open[--sw_sp]].j2 = i;
            break;
        case S_SCRIPT:
            if (proc_sp < MS_IF_STACK) proc_open[proc_sp++] = i;
            break;
        case S_END:
            if (proc_sp > 0) g_ms.prog[proc_open[--proc_sp]].j2 = i;
            break;
        default:
            break;
        }
    }
}

static void register_procs(void) {
    g_ms.nprocs = 0;
    for (int i = 0; i < g_ms.nlines; i++) {
        Stmt *s = &g_ms.prog[i];
        if (s->st != S_SCRIPT) continue;
        if (g_ms.nprocs >= MS_MAX_PROCS) break;
        MsProc *p = &g_ms.procs[g_ms.nprocs];
        memset(p, 0, sizeof *p);
        const char *q = s->text;
        while (*q && is_ident_char((unsigned char)*q)) q++;
        while (*q == ' ' || *q == '\t') q++;
        int k = 0;
        while (is_ident_char((unsigned char)*q) && k < MS_NAME_LEN - 1) { p->name[k++] = *q; q++; }
        p->name[k] = '\0';
        p->start = i;
        p->end = (s->j2 >= 0) ? s->j2 : i;
        p->defined = 1;
        const char *par = strchr(q, '(');
        if (par) {
            par++;
            while (*par && *par != ')' && p->nparams < MS_MAX_PARAMS) {
                while (*par == ' ' || *par == ',' || *par == '\t') par++;
                if (*par == ')' || !*par) break;
                int m = 0;
                while (is_ident_char((unsigned char)*par) && m < MS_NAME_LEN - 1) p->params[p->nparams][m++] = *par++;
                p->params[p->nparams][m] = '\0';
                while (*par && *par != ',' && *par != ')') par++;
                p->nparams++;
            }
        }
        g_ms.nprocs++;
    }
}

static MsProc *proc_find(const char *name) {
    for (int i = 0; i < g_ms.nprocs && i < MS_LOOP_BOUND; i++)
        if (g_ms.procs[i].defined && name_eq(g_ms.procs[i].name, name)) return &g_ms.procs[i];
    return NULL;
}

static int split_args(char *src, char bufs[MS_MAX_ARGS][MS_LINE_LEN], int max) {
    int n = 0, q = 0, b = 0;
    for (char *p = src; ; p++) {
        char ch = *p;
        if (ch == '"') q = !q;
        if (!q && (ch == ',' || ch == '\0')) {
            bufs[n][b] = '\0';
            str_trim(bufs[n]);
            n++; b = 0;
            if (ch == '\0' || n >= max) break;
            continue;
        }
        if (b < MS_LINE_LEN - 1) bufs[n][b++] = ch;
    }
    return n;
}

/* ── Stage / figure model ───────────────────────────────────────────── */

void ms_go_card(int card_idx) {
    if (card_idx >= 0 && card_idx < g_ms.card_count) {
        g_ms.current_card = card_idx;
        char buf[64];
        snprintf(buf, sizeof buf, "Scene: %d (%s)", card_idx + 1, g_ms.cards[card_idx].title);
        ms_log(buf);
        if (g_ms.wnd) inval_wnd(g_ms.wnd);
    }
}

static MsFigure *fig_find(MsCard *c, const char *name) {
    char nm[MS_NAME_LEN];
    int k = 0;
    while (is_ident_char((unsigned char)name[k]) && k < MS_NAME_LEN - 1) { nm[k] = (char)name[k]; k++; }
    nm[k] = '\0';
    for (int i = 0; i < c->fig_count && i < MS_LOOP_BOUND; i++)
        if (c->figures[i].name[0] && name_eq(c->figures[i].name, nm)) return &c->figures[i];
    return NULL;
}

/* ── Output formatting (light 表示 / LOG engine) ────────────────────── */

static void do_output(const char *operand_text) {
    char bufs[MS_MAX_ARGS][MS_LINE_LEN];
    char work[MS_LINE_LEN];
    strncpy(work, operand_text, sizeof work - 1); work[sizeof work - 1] = '\0';
    int argc = split_args(work, bufs, MS_MAX_ARGS);

    char out[MS_LINE_LEN * 2];
    int o = 0;

    if (argc >= 1 && strchr(bufs[0], '%') != NULL) {
        const char *f = bufs[0];
        int ai = 1;
        while (*f && o < (int)sizeof out - 4) {
            if (*f == '%' && f[1]) {
                f++;
                Value arg = (ai < argc) ? eval_or_literal(bufs[ai++]) : v_int(0);
                char tmp[MS_MAX_STR_LEN];
                switch (*f) {
                case 'd': snprintf(tmp, sizeof tmp, "%lld", val_int(&arg)); break;
                case 'x': snprintf(tmp, sizeof tmp, "%llx", (unsigned long long)val_int(&arg)); break;
                case 'f': snprintf(tmp, sizeof tmp, "%g", val_real(&arg)); break;
                case 'c': snprintf(tmp, sizeof tmp, "%c", (int)val_int(&arg)); break;
                case 's': v_str(&arg); snprintf(tmp, sizeof tmp, "%s", arg.s); break;
                default:  snprintf(tmp, sizeof tmp, "%s", "?"); break;
                }
                size_t l = strlen(tmp);
                if (o + (int)l < (int)sizeof out - 1) { memcpy(out + o, tmp, l); o += (int)l; }
                f++;
            } else out[o++] = *f++;
        }
    } else {
        Value v = eval_or_literal(operand_text);
        v_str(&v);
        snprintf(out, sizeof out, "%s", v.s);
        o = (int)strlen(out);
    }
    out[o] = '\0';
    ms_log(out);
    if (g_ms.wnd) inval_wnd(g_ms.wnd);
}

/* ── Statement dispatch for S_PLAIN ─────────────────────────────────── */

static void call_proc(MsProc *p, const char *argtext, int ret_pc) {
    if (g_ms.call_depth >= MS_MAX_CALL) { g_ms.err = 1; return; }
    char bufs[MS_MAX_ARGS][MS_LINE_LEN];
    char work[MS_LINE_LEN];
    strncpy(work, argtext, sizeof work - 1); work[sizeof work - 1] = '\0';
    int argc = split_args(work, bufs, MS_MAX_ARGS);
    for (int i = 0; i < p->nparams && i < argc; i++)
        var_set(p->params[i], eval_or_literal(bufs[i]));
    g_ms.call_ret[g_ms.call_depth++] = ret_pc;
    g_pc = p->start + 1;
}

static int parse_and_call_named(const char *text, int ret_pc) {
    char name[MS_NAME_LEN];
    const char *q = text;
    while (*q == ' ' || *q == '\t') q++;
    int k = 0;
    while (is_ident_char((unsigned char)*q) && k < MS_NAME_LEN - 1) name[k++] = (char)ch_lower((unsigned char)*q++);
    name[k] = '\0';
    MsProc *p = proc_find(name);
    if (!p) return 0;
    const char *lp = strchr(text, '(');
    const char *rp = strrchr(text, ')');
    char args[MS_LINE_LEN];
    args[0] = '\0';
    if (lp && rp && rp > lp) {
        size_t n = (size_t)(rp - lp - 1);
        if (n >= sizeof args) n = sizeof args - 1;
        memcpy(args, lp + 1, n); args[n] = '\0';
    }
    call_proc(p, args, ret_pc);
    return 1;
}

/* Index of the top-level assignment '=' (not ==, <=, >=, !=). */
static int find_assign(const char *s) {
    int q = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] == '"') q = !q;
        if (q) continue;
        if (s[i] == '=' && s[i + 1] != '=' &&
            (i == 0 || (s[i - 1] != '<' && s[i - 1] != '>' && s[i - 1] != '!' && s[i - 1] != '=')))
            return i;
    }
    return -1;
}

static void read_name(const char *src, char *out, int outsz) {
    const char *p = src;
    while (*p == ' ' || *p == '\t') p++;
    int n = 0;
    while (is_ident_char((unsigned char)*p) && n < outsz - 1) out[n++] = *p++;
    out[n] = '\0';
}

static void do_declare(const char *op) {
    char work[MS_LINE_LEN];
    strncpy(work, op, sizeof work - 1); work[sizeof work - 1] = '\0';
    int eq = find_assign(work);
    Value init = v_int(0);
    if (eq >= 0) { work[eq] = '\0'; init = eval_or_literal(work + eq + 1); }
    char name[MS_NAME_LEN];
    read_name(work, name, sizeof name);
    if (name[0]) { var_touch(name); var_set(name, init); }
}

static void do_set(const char *op) {
    char work[MS_LINE_LEN];
    strncpy(work, op, sizeof work - 1); work[sizeof work - 1] = '\0';
    int eq = find_assign(work);
    if (eq < 0) return;
    work[eq] = '\0';
    char name[MS_NAME_LEN];
    read_name(work, name, sizeof name);
    if (name[0]) var_set(name, eval_or_literal(work + eq + 1));
}

static void run_plain(Stmt *s) {
    char kw[MS_NAME_LEN];
    lead_word(s->text, kw, sizeof kw);
    const char *op = keyword_operand(s->text);

    if (!strcmp(kw, "set"))       { do_set(op);  g_pc += 1; return; }
    if (!strcmp(kw, "variable"))  { do_declare(op); g_pc += 1; return; }
    if (!strcmp(kw, "local"))     { do_declare(op); g_pc += 1; return; }
    if (!strcmp(kw, "log") || !strcmp(kw, "text") || !strcmp(kw, "prput")) { do_output(op); g_pc += 1; return; }
    if (!strcmp(kw, "call"))      { if (!parse_and_call_named(op, g_pc + 1)) g_pc += 1; return; }
    if (!strcmp(kw, "scene"))     { Value v = eval_or_literal(op); ms_go_card((int)val_int(&v) - 1); g_pc += 1; return; }
    if (!strcmp(kw, "beep"))      { ms_log("[beep]"); g_pc += 1; return; }
    if (!strcmp(kw, "update"))    { if (g_ms.wnd) inval_wnd(g_ms.wnd); g_pc += 1; return; }
    if (!strcmp(kw, "wait") || !strcmp(kw, "sleep")) { eval_or_literal(op); g_pc += 1; return; }
    if (!strcmp(kw, "finish") || !strcmp(kw, "terminate")) { g_pc = g_ms.nlines; return; }
    if (!strcmp(kw, "appear") || !strcmp(kw, "disappear")) {
        if (g_ms.current_card >= 0 && g_ms.current_card < g_ms.card_count) {
            MsCard *c = &g_ms.cards[g_ms.current_card];
            MsFigure *f = fig_find(c, op);
            if (f) f->visible = (kw[0] == 'a');
            if (g_ms.wnd) inval_wnd(g_ms.wnd);
        }
        g_pc += 1; return;
    }
    if (!strcmp(kw, "move")) {
        char bufs[MS_MAX_ARGS][MS_LINE_LEN]; char work[MS_LINE_LEN];
        strncpy(work, op, sizeof work - 1); work[sizeof work - 1] = '\0';
        int argc = split_args(work, bufs, MS_MAX_ARGS);
        if (g_ms.current_card >= 0 && g_ms.current_card < g_ms.card_count && argc >= 3) {
            MsCard *c = &g_ms.cards[g_ms.current_card];
            MsFigure *f = fig_find(c, bufs[0]);
            if (f) {
                Value x = eval_or_literal(bufs[1]), y = eval_or_literal(bufs[2]);
                H wdt = (H)(f->bounds.right - f->bounds.left), hgt = (H)(f->bounds.bottom - f->bounds.top);
                f->bounds.left = (H)val_int(&x); f->bounds.top = (H)val_int(&y);
                f->bounds.right = (H)(f->bounds.left + wdt); f->bounds.bottom = (H)(f->bounds.top + hgt);
                if (g_ms.wnd) inval_wnd(g_ms.wnd);
            }
        }
        g_pc += 1; return;
    }

    /* bare statement: name(args) call, assignment, or expression */
    if (parse_and_call_named(s->text, g_pc + 1)) return;
    if (find_assign(s->text) >= 0) { do_set(s->text); g_pc += 1; return; }
    eval_or_literal(op);
    g_pc += 1;
}

/* ── Program execution ──────────────────────────────────────────────── */

static void run_program(void) {
    g_ms.call_depth = 0;
    g_ms.sw_sp = 0;
    g_ms.enter = -1;
    g_ms.steps = 0;
    g_ms.cnt = 0;
    g_ms.err = 0;
    memset(&g_ms.retval, 0, sizeof g_ms.retval);
    g_pc = 0;

    while (g_pc >= 0 && g_pc < g_ms.nlines) {
        if (++g_ms.steps > MS_MAX_STEPS) { ms_log("step limit"); break; }
        Stmt *s = &g_ms.prog[g_pc];
        int pc = g_pc;

        switch (s->st) {
        case S_IF: {
            Value v = eval_or_literal(keyword_operand(s->text));
            if (truthy(&v))       { g_ms.enter = -1; g_pc = pc + 1; }
            else if (s->j1 >= 0)  { g_ms.enter = s->j1; g_pc = s->j1; }
            else                  g_pc = (s->j2 >= 0 ? s->j2 : pc) + 1;
            break;
        }
        case S_ELSEIF: {
            if (g_ms.enter == pc) {
                Value v = eval_or_literal(keyword_operand(s->text));
                if (truthy(&v))      { g_ms.enter = -1; g_pc = pc + 1; }
                else if (s->j1 >= 0) { g_ms.enter = s->j1; g_pc = s->j1; }
                else                 g_pc = (s->j2 >= 0 ? s->j2 : pc) + 1;
            } else { g_ms.enter = -1; g_pc = (s->j2 >= 0 ? s->j2 : pc) + 1; }
            break;
        }
        case S_ELSE:
            if (g_ms.enter == pc) { g_ms.enter = -1; g_pc = pc + 1; }
            else                  g_pc = (s->j2 >= 0 ? s->j2 : pc) + 1;
            break;
        case S_ENDIF: g_ms.enter = -1; g_pc = pc + 1; break;

        case S_WHILE: {
            Value v = eval_or_literal(keyword_operand(s->text));
            g_ms.cnt++;
            g_pc = truthy(&v) ? pc + 1 : (s->j2 >= 0 ? s->j2 : pc) + 1;
            break;
        }
        case S_ENDWHILE: g_pc = (s->j1 >= 0 ? s->j1 : pc); break;
        case S_REPEAT:   g_ms.cnt++; g_pc = pc + 1; break;
        case S_ENDREPEAT: g_pc = (s->j1 >= 0 ? s->j1 : pc); break;
        case S_BREAK:    g_pc = (s->j1 >= 0 ? s->j1 : g_ms.nlines); break;
        case S_CONTINUE: g_pc = (s->j1 >= 0 ? s->j1 : pc); break;

        case S_SWITCH: {
            Value v = eval_or_literal(keyword_operand(s->text));
            int end = (s->j2 >= 0) ? s->j2 : g_ms.nlines - 1;
            if (g_ms.sw_sp < MS_SWITCH_STACK) { g_ms.sw_end[g_ms.sw_sp] = end; g_ms.sw_val[g_ms.sw_sp] = v; g_ms.sw_sp++; }
            int match = -1, deflt = -1, nest = 0;
            for (int k = pc + 1; k < end && k < MS_MAX_LINES; k++) {
                Stmt *c = &g_ms.prog[k];
                if (c->st == S_SWITCH) nest++;
                else if (c->st == S_ENDCASE) { if (nest > 0) nest--; }
                else if (nest == 0 && c->st == S_CASE) {
                    Value cv = eval_or_literal(keyword_operand(c->text));
                    if (val_cmp(&cv, &v) == 0) { match = k; break; }
                } else if (nest == 0 && c->st == S_DEFAULT && deflt < 0) deflt = k;
            }
            if (match >= 0)      g_pc = match + 1;
            else if (deflt >= 0) g_pc = deflt + 1;
            else { if (g_ms.sw_sp > 0) g_ms.sw_sp--; g_pc = end + 1; }
            break;
        }
        case S_CASE:
        case S_DEFAULT:
            g_pc = (g_ms.sw_sp > 0 ? g_ms.sw_end[g_ms.sw_sp - 1] : pc) + 1;
            break;
        case S_ENDCASE:
            if (g_ms.sw_sp > 0) g_ms.sw_sp--;
            g_pc = pc + 1;
            break;

        case S_SCRIPT: g_pc = (s->j2 >= 0 ? s->j2 : pc) + 1; break;
        case S_END:
            g_pc = (g_ms.call_depth > 0) ? g_ms.call_ret[--g_ms.call_depth] : pc + 1;
            break;
        case S_EXIT: {
            const char *o2 = keyword_operand(s->text);
            if (o2 && *o2) g_ms.retval = eval_or_literal(o2);
            g_pc = (g_ms.call_depth > 0) ? g_ms.call_ret[--g_ms.call_depth] : g_ms.nlines;
            break;
        }
        case S_PLAIN: run_plain(s); break;
        default: g_pc = pc + 1; break;
        }
    }
}

void ms_eval_script(const char *script) {
    assert(script != NULL);
    if (!script) return;
    split_statements(script);
    classify();
    register_procs();
    run_program();
}

/* ── Rendering ──────────────────────────────────────────────────────── */

void ms_paint(WND *wnd, GDEV *dev) {
    (void)wnd;
    if (!g_ms.wnd || !dev) return;
    const H w = dev->width, h = dev->height;

    RECT bg = { 0, 0, w, h };
    fill_rec(dev, &bg, PMC_COL_BODY);

    RECT stage = { 8, 8, (H)(w - 8), (H)(h - 30) };
    fill_rec(dev, &stage, 0x00FFFFFFU);
    drw_rec(dev, &stage);

    if (g_ms.current_card >= 0 && g_ms.current_card < g_ms.card_count) {
        const MsCard *card = &g_ms.cards[g_ms.current_card];
        for (int i = 0; i < card->fig_count && i < MS_LOOP_BOUND; i++) {
            const MsFigure *f = &card->figures[i];
            if (!f->visible) continue;
            RECT r = { (H)(stage.left + f->bounds.left), (H)(stage.top + f->bounds.top),
                       (H)(stage.left + f->bounds.right), (H)(stage.top + f->bounds.bottom) };
            if (f->type == FIG_BUTTON) {
                pmc_draw_switch(dev, &r, f->text, f->is_pressed ? TRUE : FALSE, TRUE);
            } else if (f->type == FIG_RECT) {
                fill_rec(dev, &r, f->fill_col);
                drw_rec(dev, &r);
                drw_tc_string(dev, (H)(r.left + 8), (H)(r.top + 8), f->text, PMC_COL_OUTLINE, 0x00000000);
            } else if (f->type == FIG_LINE) {
                drw_lin(dev, r.left, r.top, r.right, r.bottom);
            } else {
                drw_tc_string(dev, r.left, r.top, f->text, PMC_COL_OUTLINE, 0x00000000);
            }
        }
    }

    RECT bar = { 0, (H)(h - 24), w, h };
    fill_rec(dev, &bar, PMC_COL_INACT_TITLE);
    drw_rec(dev, &bar);
    drw_tc_string(dev, 8, (H)(bar.top + 4), g_ms.console_log, 0x00202020U, 0x00000000);
}

/* ── Mouse events ───────────────────────────────────────────────────── */

void ms_handle_click(H rel_x, H rel_y) {
    if (!g_ms.wnd) return;
    if (g_ms.current_card < 0 || g_ms.current_card >= g_ms.card_count) return;
    MsCard *card = &g_ms.cards[g_ms.current_card];
    const H sl = 8, st = 8;
    for (int i = 0; i < card->fig_count && i < MS_LOOP_BOUND; i++) {
        MsFigure *f = &card->figures[i];
        if (f->type == FIG_BUTTON && f->visible) {
            RECT r = { (H)(sl + f->bounds.left), (H)(st + f->bounds.top),
                       (H)(sl + f->bounds.right), (H)(st + f->bounds.bottom) };
            if (rel_x >= r.left && rel_x <= r.right && rel_y >= r.top && rel_y <= r.bottom) {
                ms_go_card(f->target_card);
                return;
            }
        }
    }
}

static void ms_destroy(WND *wnd) { (void)wnd; g_ms.wnd = NULL; }

static void ms_event_handler(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;
    if (evt->type == EV_BUT_DOWN)
        ms_handle_click((H)(evt->pos.x - wnd->client.left), (H)(evt->pos.y - wnd->client.top));
}

/* ── Default stage ──────────────────────────────────────────────────── */

static void ms_load_default_stack(void) {
    g_ms.card_count = 2;
    g_ms.current_card = 0;
    g_ms.var_count = 0;

    MsCard *c1 = &g_ms.cards[0];
    strncpy(c1->title, "超漢字 MicroScript 入門", sizeof c1->title - 1);
    c1->fig_count = 3;
    c1->figures[0].type = FIG_LABEL; c1->figures[0].visible = true;
    c1->figures[0].bounds = (RECT){ 20, 20, 360, 44 };
    strncpy(c1->figures[0].name, "title", sizeof c1->figures[0].name - 1);
    strncpy(c1->figures[0].text, "■ BTRON3 ハイパーメディア ステージ 1", sizeof c1->figures[0].text - 1);
    c1->figures[1].type = FIG_RECT; c1->figures[1].visible = true;
    c1->figures[1].bounds = (RECT){ 30, 60, 280, 140 };
    c1->figures[1].fill_col = 0x00E8F4F8U; c1->figures[1].border_col = PMC_COL_OUTLINE;
    strncpy(c1->figures[1].name, "box", sizeof c1->figures[1].name - 1);
    strncpy(c1->figures[1].text, "図形要素 (Figure Box)", sizeof c1->figures[1].text - 1);
    c1->figures[2].type = FIG_BUTTON; c1->figures[2].visible = true;
    c1->figures[2].bounds = (RECT){ 80, 160, 240, 196 };
    c1->figures[2].target_card = 1;
    strncpy(c1->figures[2].name, "nextbtn", sizeof c1->figures[2].name - 1);
    strncpy(c1->figures[2].text, "次のシーンへ >>", sizeof c1->figures[2].text - 1);

    MsCard *c2 = &g_ms.cards[1];
    strncpy(c2->title, "スクリプト実行ステージ", sizeof c2->title - 1);
    c2->fig_count = 2;
    c2->figures[0].type = FIG_LABEL; c2->figures[0].visible = true;
    c2->figures[0].bounds = (RECT){ 20, 20, 360, 44 };
    strncpy(c2->figures[0].name, "title", sizeof c2->figures[0].name - 1);
    strncpy(c2->figures[0].text, "■ シーン 2: 状態変数・連鎖制御", sizeof c2->figures[0].text - 1);
    c2->figures[1].type = FIG_BUTTON; c2->figures[1].visible = true;
    c2->figures[1].bounds = (RECT){ 80, 100, 240, 136 };
    c2->figures[1].target_card = 0;
    strncpy(c2->figures[1].name, "backbtn", sizeof c2->figures[1].name - 1);
    strncpy(c2->figures[1].text, "<< 表紙へ戻る", sizeof c2->figures[1].text - 1);

    ms_log("MicroScript Engine 3.20 Ready");
}

void ms_app_init(void) {
    if (g_ms.wnd) { top_wnd(g_ms.wnd); return; }
    memset(&g_ms, 0, sizeof g_ms);
    ms_load_default_stack();

    g_ms.wnd = opn_wnd("マイクロスクリプト (MicroScript)", 160, 120, 520, 400,
                       WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_RESIZE | WND_ATTR_BORDER);
    if (g_ms.wnd) {
        g_ms.wnd->paint = ms_paint;
        g_ms.wnd->event_handler = ms_event_handler;
        g_ms.wnd->destroy = ms_destroy;
        inval_wnd(g_ms.wnd);
    }
}
