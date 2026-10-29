/*
 * B-System BTRON3 — ps_vm.c
 * Secure PostScript (sps) Forth Virtual Machine
 * Pure C99, adheres to NASA/JPL Power of Ten rules.
 */

#include "ps_vm.h"
#include <btron/libc_shim.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Forward declarations */
static void ps_eval_value(PSContext *ctx, PSValue val);
static void ps_exec_proc(PSContext *ctx, PSValue proc);
static BOOL ps_tokenize_and_eval(PSContext *ctx, const char *code, size_t len);

/* ── Value Constructors ─────────────────────────────────────────── */

PSValue ps_val_null(void) {
    PSValue v;
    memset(&v, 0, sizeof(v));
    v.type = PS_VAL_NULL;
    return v;
}

PSValue ps_val_bool(BOOL b) {
    PSValue v = ps_val_null();
    v.type = PS_VAL_BOOL;
    v.u.b = b;
    return v;
}

PSValue ps_val_int(W i) {
    PSValue v = ps_val_null();
    v.type = PS_VAL_INT;
    v.u.i = i;
    return v;
}

PSValue ps_val_real(float r) {
    PSValue v = ps_val_null();
    v.type = PS_VAL_REAL;
    v.u.r = r;
    return v;
}

PSValue ps_val_name(PSContext *ctx, const char *name, BOOL is_executable) {
    PSValue v = ps_val_null();
    v.type = PS_VAL_NAME;
    v.flags = is_executable ? 1 : 0;

    /* Store name in string pool if not literal static string */
    if (ctx && name) {
        size_t nlen = strlen(name);
        if (ctx->str_pool_used + nlen + 1 < PS_STR_POOL_SIZE) {
            char *dest = &ctx->str_pool[ctx->str_pool_used];
            memcpy(dest, name, nlen);
            dest[nlen] = '\0';
            ctx->str_pool_used += (UW)(nlen + 1);
            v.u.name = dest;
        } else {
            v.u.name = name;
        }
    } else {
        v.u.name = name ? name : "";
    }
    return v;
}

PSValue ps_val_string(PSContext *ctx, const char *str, UW len) {
    PSValue v = ps_val_null();
    v.type = PS_VAL_STRING;
    v.u.s.len = len;

    if (ctx && str && len > 0) {
        if (ctx->str_pool_used + len + 1 < PS_STR_POOL_SIZE) {
            char *dest = &ctx->str_pool[ctx->str_pool_used];
            memcpy(dest, str, len);
            dest[len] = '\0';
            ctx->str_pool_used += len + 1;
            v.u.s.str = dest;
        } else {
            v.u.s.str = (char *)str;
        }
    } else if (ctx && len > 0) {
        /* Allocate uninitialized string */
        if (ctx->str_pool_used + len + 1 < PS_STR_POOL_SIZE) {
            char *dest = &ctx->str_pool[ctx->str_pool_used];
            memset(dest, 0, len + 1);
            ctx->str_pool_used += len + 1;
            v.u.s.str = dest;
        } else {
            v.u.s.str = NULL;
            v.u.s.len = 0;
        }
    } else {
        v.u.s.str = (char *)(str ? str : "");
    }
    return v;
}

PSValue ps_val_proc(PSValue *elems, UW len) {
    PSValue v = ps_val_null();
    v.type = PS_VAL_PROC;
    v.flags = 1; /* Procedures are executable */
    v.u.a.elems = elems;
    v.u.a.len = len;
    return v;
}

PSValue ps_val_op(void (*op)(PSContext *ctx)) {
    PSValue v = ps_val_null();
    v.type = PS_VAL_OP;
    v.flags = 1; /* Operators are executable */
    v.u.op = op;
    return v;
}

/* ── Stack Primitives ───────────────────────────────────────────── */

BOOL ps_push(PSContext *ctx, PSValue val) {
    if (!ctx) return FALSE;
    if (ctx->op_top >= PS_STACK_MAX - 1) {
        ctx->error_flag = TRUE;
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "operand stack overflow");
        return FALSE;
    }
    ctx->op_stack[++ctx->op_top] = val;
    return TRUE;
}

PSValue ps_pop(PSContext *ctx) {
    if (!ctx || ctx->op_top < 0) {
        if (ctx) {
            ctx->error_flag = TRUE;
            snprintf(ctx->error_msg, sizeof(ctx->error_msg), "operand stack underflow");
        }
        return ps_val_null();
    }
    return ctx->op_stack[ctx->op_top--];
}

PSValue ps_peek(PSContext *ctx) {
    if (!ctx || ctx->op_top < 0) return ps_val_null();
    return ctx->op_stack[ctx->op_top];
}

BOOL ps_has_n(const PSContext *ctx, int n) {
    if (!ctx) return FALSE;
    return (ctx->op_top + 1 >= n);
}

BOOL ps_pop_num(PSContext *ctx, float *out_num) {
    if (!ctx || !out_num) return FALSE;
    PSValue v = ps_pop(ctx);
    if (v.type == PS_VAL_INT) {
        *out_num = (float)v.u.i;
        return TRUE;
    } else if (v.type == PS_VAL_REAL) {
        *out_num = v.u.r;
        return TRUE;
    }
    return FALSE;
}

/* ── Dictionary Operations ──────────────────────────────────────── */

PSDict *ps_dict_create(PSContext *ctx, UW capacity) {
    if (!ctx) return NULL;
    if (ctx->dict_pool_used >= sizeof(ctx->dict_pool) / sizeof(ctx->dict_pool[0])) {
        return NULL;
    }
    PSDict *d = &ctx->dict_pool[ctx->dict_pool_used++];
    memset(d, 0, sizeof(PSDict));
    d->capacity = capacity > PS_DICT_CAP_MAX ? PS_DICT_CAP_MAX : capacity;
    return d;
}

BOOL ps_dict_put(PSDict *d, const char *key, PSValue val) {
    if (!d || !key) return FALSE;
    for (UW i = 0; i < d->count; i++) {
        if (strcmp(d->entries[i].key, key) == 0) {
            d->entries[i].val = val;
            return TRUE;
        }
    }
    if (d->count >= d->capacity) return FALSE;
    strncpy(d->entries[d->count].key, key, PS_NAME_MAX - 1);
    d->entries[d->count].key[PS_NAME_MAX - 1] = '\0';
    d->entries[d->count].val = val;
    d->count++;
    return TRUE;
}

BOOL ps_dict_get(const PSDict *d, const char *key, PSValue *out_val) {
    if (!d || !key || !out_val) return FALSE;
    for (UW i = 0; i < d->count; i++) {
        if (strcmp(d->entries[i].key, key) == 0) {
            *out_val = d->entries[i].val;
            return TRUE;
        }
    }
    return FALSE;
}

BOOL ps_dict_def(PSContext *ctx, const char *key, PSValue val) {
    if (!ctx || !key || ctx->dict_top < 0) return FALSE;
    return ps_dict_put(ctx->dict_stack[ctx->dict_top], key, val);
}

BOOL ps_lookup(const PSContext *ctx, const char *key, PSValue *out_val) {
    if (!ctx || !key || !out_val) return FALSE;
    /* Search from top of dictionary stack to bottom */
    for (W i = ctx->dict_top; i >= 0; i--) {
        if (ps_dict_get(ctx->dict_stack[i], key, out_val)) {
            return TRUE;
        }
    }
    return FALSE;
}

/* ── Standard Built-In Operators ────────────────────────────────── */

/* Stack Operators */
static void op_pop(PSContext *ctx)   { ps_pop(ctx); }
static void op_dup(PSContext *ctx)   { ps_push(ctx, ps_peek(ctx)); }
static void op_exch(PSContext *ctx)  {
    if (!ps_has_n(ctx, 2)) return;
    PSValue a = ps_pop(ctx);
    PSValue b = ps_pop(ctx);
    ps_push(ctx, a);
    ps_push(ctx, b);
}
static void op_copy(PSContext *ctx) {
    if (!ps_has_n(ctx, 1)) return;
    PSValue n = ps_pop(ctx);
    if (n.type == PS_VAL_INT && n.u.i > 0 && ps_has_n(ctx, n.u.i)) {
        W count = n.u.i;
        W base = ctx->op_top - count + 1;
        for (W i = 0; i < count; i++) {
            ps_push(ctx, ctx->op_stack[base + i]);
        }
    }
}
static void op_index(PSContext *ctx) {
    if (!ps_has_n(ctx, 1)) return;
    PSValue n = ps_pop(ctx);
    if (n.type == PS_VAL_INT && n.u.i >= 0 && ctx->op_top >= n.u.i) {
        ps_push(ctx, ctx->op_stack[ctx->op_top - n.u.i]);
    }
}
static void op_roll(PSContext *ctx) {
    if (!ps_has_n(ctx, 2)) return;
    PSValue j_val = ps_pop(ctx);
    PSValue n_val = ps_pop(ctx);
    if (n_val.type != PS_VAL_INT || j_val.type != PS_VAL_INT) return;
    W n = n_val.u.i;
    W j = j_val.u.i;
    if (n <= 1 || !ps_has_n(ctx, n)) return;
    j = (j % n + n) % n;
    if (j == 0) return;
    PSValue temp[64];
    if (n > 64) return;
    for (W i = 0; i < n; i++) temp[i] = ctx->op_stack[ctx->op_top - n + 1 + i];
    for (W i = 0; i < n; i++) ctx->op_stack[ctx->op_top - n + 1 + ((i + j) % n)] = temp[i];
}
static void op_clear(PSContext *ctx) { ctx->op_top = -1; }
static void op_count(PSContext *ctx) { ps_push(ctx, ps_val_int(ctx->op_top + 1)); }

/* Arithmetic */
static void op_add(PSContext *ctx) {
    if (!ps_has_n(ctx, 2)) return;
    PSValue b = ps_pop(ctx);
    PSValue a = ps_pop(ctx);
    if (a.type == PS_VAL_INT && b.type == PS_VAL_INT) {
        ps_push(ctx, ps_val_int(a.u.i + b.u.i));
    } else {
        float f1 = (a.type == PS_VAL_INT) ? (float)a.u.i : a.u.r;
        float f2 = (b.type == PS_VAL_INT) ? (float)b.u.i : b.u.r;
        ps_push(ctx, ps_val_real(f1 + f2));
    }
}
static void op_sub(PSContext *ctx) {
    if (!ps_has_n(ctx, 2)) return;
    PSValue b = ps_pop(ctx);
    PSValue a = ps_pop(ctx);
    if (a.type == PS_VAL_INT && b.type == PS_VAL_INT) {
        ps_push(ctx, ps_val_int(a.u.i - b.u.i));
    } else {
        float f1 = (a.type == PS_VAL_INT) ? (float)a.u.i : a.u.r;
        float f2 = (b.type == PS_VAL_INT) ? (float)b.u.i : b.u.r;
        ps_push(ctx, ps_val_real(f1 - f2));
    }
}
static void op_mul(PSContext *ctx) {
    if (!ps_has_n(ctx, 2)) return;
    PSValue b = ps_pop(ctx);
    PSValue a = ps_pop(ctx);
    if (a.type == PS_VAL_INT && b.type == PS_VAL_INT) {
        ps_push(ctx, ps_val_int(a.u.i * b.u.i));
    } else {
        float f1 = (a.type == PS_VAL_INT) ? (float)a.u.i : a.u.r;
        float f2 = (b.type == PS_VAL_INT) ? (float)b.u.i : b.u.r;
        ps_push(ctx, ps_val_real(f1 * f2));
    }
}
static void op_div(PSContext *ctx) {
    if (!ps_has_n(ctx, 2)) return;
    float b = 0.0f, a = 0.0f;
    if (ps_pop_num(ctx, &b) && ps_pop_num(ctx, &a)) {
        if (b != 0.0f) ps_push(ctx, ps_val_real(a / b));
        else           ps_push(ctx, ps_val_real(0.0f));
    }
}
static void op_idiv(PSContext *ctx) {
    if (!ps_has_n(ctx, 2)) return;
    PSValue b = ps_pop(ctx);
    PSValue a = ps_pop(ctx);
    if (b.type == PS_VAL_INT && a.type == PS_VAL_INT && b.u.i != 0) {
        ps_push(ctx, ps_val_int(a.u.i / b.u.i));
    } else {
        ps_push(ctx, ps_val_int(0));
    }
}
static void op_mod(PSContext *ctx) {
    if (!ps_has_n(ctx, 2)) return;
    PSValue b = ps_pop(ctx);
    PSValue a = ps_pop(ctx);
    if (b.type == PS_VAL_INT && a.type == PS_VAL_INT && b.u.i != 0) {
        ps_push(ctx, ps_val_int(a.u.i % b.u.i));
    } else {
        ps_push(ctx, ps_val_int(0));
    }
}
static void op_neg(PSContext *ctx) {
    if (!ps_has_n(ctx, 1)) return;
    PSValue a = ps_pop(ctx);
    if (a.type == PS_VAL_INT)  ps_push(ctx, ps_val_int(-a.u.i));
    else if (a.type == PS_VAL_REAL) ps_push(ctx, ps_val_real(-a.u.r));
    else ps_push(ctx, a);
}
static void op_abs(PSContext *ctx) {
    if (!ps_has_n(ctx, 1)) return;
    PSValue a = ps_pop(ctx);
    if (a.type == PS_VAL_INT)  ps_push(ctx, ps_val_int(a.u.i < 0 ? -a.u.i : a.u.i));
    else if (a.type == PS_VAL_REAL) ps_push(ctx, ps_val_real(a.u.r < 0.0f ? -a.u.r : a.u.r));
    else ps_push(ctx, a);
}

/* Dictionary Operations */
static void op_dict(PSContext *ctx) {
    if (!ps_has_n(ctx, 1)) return;
    PSValue n = ps_pop(ctx);
    UW cap = (n.type == PS_VAL_INT && n.u.i > 0) ? (UW)n.u.i : 32;
    PSDict *d = ps_dict_create(ctx, cap);
    PSValue v = ps_val_null();
    v.type = PS_VAL_DICT;
    v.u.d = d;
    ps_push(ctx, v);
}
static void op_begin(PSContext *ctx) {
    if (!ps_has_n(ctx, 1)) return;
    PSValue v = ps_pop(ctx);
    if (v.type == PS_VAL_DICT && v.u.d && ctx->dict_top < PS_DICT_STACK_MAX - 1) {
        ctx->dict_stack[++ctx->dict_top] = v.u.d;
    }
}
static void op_end(PSContext *ctx) {
    if (ctx->dict_top > 2) { /* Never pop userdict/globaldict/systemdict */
        ctx->dict_top--;
    }
}
static void op_def(PSContext *ctx) {
    if (!ps_has_n(ctx, 2)) return;
    PSValue val = ps_pop(ctx);
    PSValue key = ps_pop(ctx);
    const char *name = (key.type == PS_VAL_NAME) ? key.u.name : "";
    ps_dict_def(ctx, name, val);
}
static void op_put(PSContext *ctx) {
    if (!ps_has_n(ctx, 3)) return;
    PSValue val = ps_pop(ctx);
    PSValue key = ps_pop(ctx);
    PSValue target = ps_pop(ctx);
    if (target.type == PS_VAL_DICT && target.u.d) {
        const char *name = (key.type == PS_VAL_NAME) ? key.u.name : "";
        ps_dict_put(target.u.d, name, val);
    }
}
static void op_get(PSContext *ctx) {
    if (!ps_has_n(ctx, 2)) return;
    PSValue key = ps_pop(ctx);
    PSValue target = ps_pop(ctx);
    if (target.type == PS_VAL_DICT && target.u.d) {
        const char *name = (key.type == PS_VAL_NAME) ? key.u.name : "";
        PSValue res = ps_val_null();
        if (ps_dict_get(target.u.d, name, &res)) {
            ps_push(ctx, res);
        } else {
            ps_push(ctx, ps_val_null());
        }
    }
}

/* System Dictionaries */
static void op_systemdict(PSContext *ctx) {
    PSValue v = ps_val_null();
    v.type = PS_VAL_DICT;
    v.u.d = &ctx->systemdict;
    ps_push(ctx, v);
}
static void op_globaldict(PSContext *ctx) {
    PSValue v = ps_val_null();
    v.type = PS_VAL_DICT;
    v.u.d = &ctx->globaldict;
    ps_push(ctx, v);
}
static void op_userdict(PSContext *ctx) {
    PSValue v = ps_val_null();
    v.type = PS_VAL_DICT;
    v.u.d = &ctx->userdict;
    ps_push(ctx, v);
}

/* Comparison & Control */
static void op_eq(PSContext *ctx) {
    if (!ps_has_n(ctx, 2)) return;
    PSValue b = ps_pop(ctx);
    PSValue a = ps_pop(ctx);
    BOOL eq = FALSE;
    if (a.type == b.type) {
        if (a.type == PS_VAL_INT) eq = (a.u.i == b.u.i);
        else if (a.type == PS_VAL_REAL) eq = (a.u.r == b.u.r);
        else if (a.type == PS_VAL_BOOL) eq = (a.u.b == b.u.b);
        else if (a.type == PS_VAL_NAME) eq = (strcmp(a.u.name, b.u.name) == 0);
        else if (a.type == PS_VAL_STRING) eq = (a.u.s.len == b.u.s.len && memcmp(a.u.s.str, b.u.s.str, a.u.s.len) == 0);
    }
    ps_push(ctx, ps_val_bool(eq));
}
static void op_ne(PSContext *ctx) { op_eq(ctx); ctx->op_stack[ctx->op_top].u.b = !ctx->op_stack[ctx->op_top].u.b; }

static void ps_exec_proc(PSContext *ctx, PSValue proc) {
    if (!ctx) return;
    if (proc.type == PS_VAL_PROC && proc.u.a.elems) {
        for (UW i = 0; i < proc.u.a.len && !ctx->exit_flag && !ctx->error_flag; i++) {
            ps_eval_value(ctx, proc.u.a.elems[i]);
        }
    }
}

static void op_if(PSContext *ctx) {
    if (!ps_has_n(ctx, 2)) return;
    PSValue proc = ps_pop(ctx);
    PSValue cond = ps_pop(ctx);
    if (cond.type == PS_VAL_BOOL && cond.u.b) {
        ps_exec_proc(ctx, proc);
    }
}
static void op_ifelse(PSContext *ctx) {
    if (!ps_has_n(ctx, 3)) return;
    PSValue proc2 = ps_pop(ctx);
    PSValue proc1 = ps_pop(ctx);
    PSValue cond = ps_pop(ctx);
    if (cond.type == PS_VAL_BOOL) {
        if (cond.u.b) {
            ps_exec_proc(ctx, proc1);
        } else {
            ps_exec_proc(ctx, proc2);
        }
    }
}
static void op_repeat(PSContext *ctx) {
    if (!ps_has_n(ctx, 2)) return;
    PSValue proc = ps_pop(ctx);
    PSValue count_val = ps_pop(ctx);
    if (count_val.type == PS_VAL_INT && count_val.u.i > 0) {
        W n = count_val.u.i;
        if (n > 65536) n = 65536; /* NASA JPL bounded loop */
        for (W i = 0; i < n && !ctx->exit_flag && !ctx->error_flag; i++) {
            ps_exec_proc(ctx, proc);
        }
        ctx->exit_flag = FALSE;
    }
}

/* String & Type Conversions */
static void op_string(PSContext *ctx) {
    if (!ps_has_n(ctx, 1)) return;
    PSValue n = ps_pop(ctx);
    UW len = (n.type == PS_VAL_INT && n.u.i >= 0) ? (UW)n.u.i : 0;
    ps_push(ctx, ps_val_string(ctx, NULL, len));
}

static void op_cvs(PSContext *ctx) {
    if (!ps_has_n(ctx, 2)) return;
    PSValue str_dest = ps_pop(ctx);
    PSValue any_val = ps_pop(ctx);
    char buf[64] = {0};

    if (any_val.type == PS_VAL_INT) {
        snprintf(buf, sizeof(buf), "%d", any_val.u.i);
    } else if (any_val.type == PS_VAL_REAL) {
        snprintf(buf, sizeof(buf), "%.2f", any_val.u.r);
    } else if (any_val.type == PS_VAL_BOOL) {
        snprintf(buf, sizeof(buf), "%s", any_val.u.b ? "true" : "false");
    } else if (any_val.type == PS_VAL_NAME) {
        snprintf(buf, sizeof(buf), "%s", any_val.u.name ? any_val.u.name : "");
    } else if (any_val.type == PS_VAL_STRING) {
        size_t l = any_val.u.s.len < sizeof(buf) - 1 ? any_val.u.s.len : sizeof(buf) - 1;
        memcpy(buf, any_val.u.s.str, l);
        buf[l] = '\0';
    }

    size_t wlen = strlen(buf);
    if (str_dest.type == PS_VAL_STRING && str_dest.u.s.str && str_dest.u.s.len >= wlen) {
        memcpy(str_dest.u.s.str, buf, wlen);
        PSValue res = str_dest;
        res.u.s.len = (UW)wlen;
        ps_push(ctx, res);
    } else {
        ps_push(ctx, ps_val_string(ctx, buf, (UW)wlen));
    }
}

/* Graphics State & Path Primitives */
static void op_gsave(PSContext *ctx) {
    if (ctx->gs_save_top < 15) {
        ctx->gs_save_stack[++ctx->gs_save_top] = ctx->gs;
    }
}
static void op_grestore(PSContext *ctx) {
    if (ctx->gs_save_top >= 0) {
        ctx->gs = ctx->gs_save_stack[ctx->gs_save_top--];
    }
}
static void op_setlinewidth(PSContext *ctx) {
    float w = 1.0f;
    if (ps_pop_num(ctx, &w)) ctx->gs.line_width = w;
}
static void op_setgray(PSContext *ctx) {
    float g = 0.0f;
    if (ps_pop_num(ctx, &g)) {
        ctx->gs.rgb[0] = g;
        ctx->gs.rgb[1] = g;
        ctx->gs.rgb[2] = g;
    }
}
static void op_setrgbcolor(PSContext *ctx) {
    float b = 0.0f, g = 0.0f, r = 0.0f;
    if (ps_pop_num(ctx, &b) && ps_pop_num(ctx, &g) && ps_pop_num(ctx, &r)) {
        ctx->gs.rgb[0] = r;
        ctx->gs.rgb[1] = g;
        ctx->gs.rgb[2] = b;
    }
}
static void op_newpath(PSContext *ctx) { (void)ctx; }
static void op_moveto(PSContext *ctx) {
    float y = 0.0f, x = 0.0f;
    if (ps_pop_num(ctx, &y) && ps_pop_num(ctx, &x)) {
        ctx->gs.cur_x = x;
        ctx->gs.cur_y = y;
        if (ctx->sink.moveto) ctx->sink.moveto(ctx->sink.user_data, x, y);
    }
}
static void op_rmoveto(PSContext *ctx) {
    float dy = 0.0f, dx = 0.0f;
    if (ps_pop_num(ctx, &dy) && ps_pop_num(ctx, &dx)) {
        ctx->gs.cur_x += dx;
        ctx->gs.cur_y += dy;
        if (ctx->sink.moveto) ctx->sink.moveto(ctx->sink.user_data, ctx->gs.cur_x, ctx->gs.cur_y);
    }
}
static void op_lineto(PSContext *ctx) {
    float y = 0.0f, x = 0.0f;
    if (ps_pop_num(ctx, &y) && ps_pop_num(ctx, &x)) {
        ctx->gs.cur_x = x;
        ctx->gs.cur_y = y;
        if (ctx->sink.lineto) ctx->sink.lineto(ctx->sink.user_data, x, y);
    }
}
static void op_rlineto(PSContext *ctx) {
    float dy = 0.0f, dx = 0.0f;
    if (ps_pop_num(ctx, &dy) && ps_pop_num(ctx, &dx)) {
        ctx->gs.cur_x += dx;
        ctx->gs.cur_y += dy;
        if (ctx->sink.lineto) ctx->sink.lineto(ctx->sink.user_data, ctx->gs.cur_x, ctx->gs.cur_y);
    }
}
static void op_curveto(PSContext *ctx) {
    if (!ps_has_n(ctx, 6)) return;
    float y3 = 0.0f, x3 = 0.0f, y2 = 0.0f, x2 = 0.0f, y1 = 0.0f, x1 = 0.0f;
    if (ps_pop_num(ctx, &y3) && ps_pop_num(ctx, &x3) &&
        ps_pop_num(ctx, &y2) && ps_pop_num(ctx, &x2) &&
        ps_pop_num(ctx, &y1) && ps_pop_num(ctx, &x1)) {
        ctx->gs.cur_x = x3;
        ctx->gs.cur_y = y3;
        if (ctx->sink.curveto) {
            ctx->sink.curveto(ctx->sink.user_data, x1, y1, x2, y2, x3, y3);
        }
    }
}
static void op_closepath(PSContext *ctx) {
    if (ctx->sink.closepath) ctx->sink.closepath(ctx->sink.user_data);
}
static void op_stroke(PSContext *ctx) {
    if (ctx->sink.stroke) ctx->sink.stroke(ctx->sink.user_data, &ctx->gs);
}
static void op_fill(PSContext *ctx) {
    if (ctx->sink.fill) ctx->sink.fill(ctx->sink.user_data, &ctx->gs, FALSE);
}
static void op_eofill(PSContext *ctx) {
    if (ctx->sink.fill) ctx->sink.fill(ctx->sink.user_data, &ctx->gs, TRUE);
}

/* Typography */
static void op_findfont(PSContext *ctx) {
    /* Pops font name, pushes font name representation */
    if (!ps_has_n(ctx, 1)) return;
    PSValue fname = ps_pop(ctx);
    ps_push(ctx, fname);
}
static void op_scalefont(PSContext *ctx) {
    /* Pops size and font name, pushes font dict/spec */
    if (!ps_has_n(ctx, 2)) return;
    float size = 12.0f;
    ps_pop_num(ctx, &size);
    PSValue fname = ps_pop(ctx);
    ctx->gs.font_size = size;
    if (fname.type == PS_VAL_NAME && fname.u.name) {
        strncpy(ctx->gs.font_name, fname.u.name, sizeof(ctx->gs.font_name) - 1);
        ctx->gs.font_name[sizeof(ctx->gs.font_name) - 1] = '\0';
    }
    ps_push(ctx, fname);
}
static void op_setfont(PSContext *ctx) {
    if (!ps_has_n(ctx, 1)) return;
    PSValue f = ps_pop(ctx);
    if (f.type == PS_VAL_NAME && f.u.name) {
        strncpy(ctx->gs.font_name, f.u.name, sizeof(ctx->gs.font_name) - 1);
        ctx->gs.font_name[sizeof(ctx->gs.font_name) - 1] = '\0';
    }
}
static void op_show(PSContext *ctx) {
    if (!ps_has_n(ctx, 1)) return;
    PSValue s = ps_pop(ctx);
    const char *text = "";
    UW tlen = 0;
    if (s.type == PS_VAL_STRING) {
        text = s.u.s.str ? s.u.s.str : "";
        tlen = s.u.s.len;
    } else if (s.type == PS_VAL_NAME) {
        text = s.u.name ? s.u.name : "";
        tlen = (UW)strlen(text);
    }
    if (ctx->sink.show) {
        ctx->sink.show(ctx->sink.user_data, text, tlen, &ctx->gs);
    }
}
static void op_stringwidth(PSContext *ctx) {
    if (!ps_has_n(ctx, 1)) return;
    PSValue s = ps_pop(ctx);
    UW len = (s.type == PS_VAL_STRING) ? s.u.s.len : 0;
    float w = (float)len * (ctx->gs.font_size > 0 ? ctx->gs.font_size * 0.5f : 6.0f);
    ps_push(ctx, ps_val_real(w));
    ps_push(ctx, ps_val_real(0.0f));
}

/* Page Management */
static void op_showpage(PSContext *ctx) {
    ctx->page_count++;
    if (ctx->sink.showpage) {
        ctx->sink.showpage(ctx->sink.user_data, ctx->page_count);
    }
    /* Reset per-page path */
    ctx->gs.cur_x = 0;
    ctx->gs.cur_y = 0;
}

/* ── VM Initialization ──────────────────────────────────────────── */

void ps_ctx_init(PSContext *ctx, const PSGraphicsSink *sink) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(PSContext));

    ctx->op_top = -1;
    ctx->exec_top = -1;
    ctx->dict_top = -1;
    ctx->gs_save_top = -1;

    if (sink) ctx->sink = *sink;

    /* Initialize Default Graphics State */
    ctx->gs.cur_x = 0.0f;
    ctx->gs.cur_y = 0.0f;
    ctx->gs.line_width = 1.0f;
    ctx->gs.rgb[0] = 0.0f;
    ctx->gs.rgb[1] = 0.0f;
    ctx->gs.rgb[2] = 0.0f;
    strcpy(ctx->gs.font_name, "Palatino");
    ctx->gs.font_size = 10.0f;

    /* Set up standard dictionaries */
    ctx->systemdict.capacity = PS_DICT_CAP_MAX;
    ctx->globaldict.capacity = PS_DICT_CAP_MAX;
    ctx->userdict.capacity   = PS_DICT_CAP_MAX;

    ctx->dict_stack[++ctx->dict_top] = &ctx->systemdict;
    ctx->dict_stack[++ctx->dict_top] = &ctx->globaldict;
    ctx->dict_stack[++ctx->dict_top] = &ctx->userdict;

    /* Setup pre-allocated =string */
    memset(ctx->eq_string_buf, 0, sizeof(ctx->eq_string_buf));
    ctx->eq_string_val.type = PS_VAL_STRING;
    ctx->eq_string_val.u.s.str = ctx->eq_string_buf;
    ctx->eq_string_val.u.s.len = sizeof(ctx->eq_string_buf) - 1;
    ps_dict_put(&ctx->systemdict, "=string", ctx->eq_string_val);

    /* Register Operators into systemdict */
    ps_dict_put(&ctx->systemdict, "pop",          ps_val_op(op_pop));
    ps_dict_put(&ctx->systemdict, "dup",          ps_val_op(op_dup));
    ps_dict_put(&ctx->systemdict, "exch",         ps_val_op(op_exch));
    ps_dict_put(&ctx->systemdict, "copy",         ps_val_op(op_copy));
    ps_dict_put(&ctx->systemdict, "index",        ps_val_op(op_index));
    ps_dict_put(&ctx->systemdict, "roll",         ps_val_op(op_roll));
    ps_dict_put(&ctx->systemdict, "clear",        ps_val_op(op_clear));
    ps_dict_put(&ctx->systemdict, "count",        ps_val_op(op_count));

    ps_dict_put(&ctx->systemdict, "add",          ps_val_op(op_add));
    ps_dict_put(&ctx->systemdict, "sub",          ps_val_op(op_sub));
    ps_dict_put(&ctx->systemdict, "mul",          ps_val_op(op_mul));
    ps_dict_put(&ctx->systemdict, "div",          ps_val_op(op_div));
    ps_dict_put(&ctx->systemdict, "idiv",         ps_val_op(op_idiv));
    ps_dict_put(&ctx->systemdict, "mod",          ps_val_op(op_mod));
    ps_dict_put(&ctx->systemdict, "neg",          ps_val_op(op_neg));
    ps_dict_put(&ctx->systemdict, "abs",          ps_val_op(op_abs));

    ps_dict_put(&ctx->systemdict, "dict",         ps_val_op(op_dict));
    ps_dict_put(&ctx->systemdict, "begin",        ps_val_op(op_begin));
    ps_dict_put(&ctx->systemdict, "end",          ps_val_op(op_end));
    ps_dict_put(&ctx->systemdict, "def",          ps_val_op(op_def));
    ps_dict_put(&ctx->systemdict, "put",          ps_val_op(op_put));
    ps_dict_put(&ctx->systemdict, "get",          ps_val_op(op_get));

    ps_dict_put(&ctx->systemdict, "systemdict",   ps_val_op(op_systemdict));
    ps_dict_put(&ctx->systemdict, "globaldict",   ps_val_op(op_globaldict));
    ps_dict_put(&ctx->systemdict, "userdict",     ps_val_op(op_userdict));

    ps_dict_put(&ctx->systemdict, "eq",           ps_val_op(op_eq));
    ps_dict_put(&ctx->systemdict, "ne",           ps_val_op(op_ne));
    ps_dict_put(&ctx->systemdict, "if",           ps_val_op(op_if));
    ps_dict_put(&ctx->systemdict, "ifelse",       ps_val_op(op_ifelse));
    ps_dict_put(&ctx->systemdict, "repeat",       ps_val_op(op_repeat));

    ps_dict_put(&ctx->systemdict, "string",       ps_val_op(op_string));
    ps_dict_put(&ctx->systemdict, "cvs",          ps_val_op(op_cvs));

    ps_dict_put(&ctx->systemdict, "gsave",        ps_val_op(op_gsave));
    ps_dict_put(&ctx->systemdict, "grestore",     ps_val_op(op_grestore));
    ps_dict_put(&ctx->systemdict, "setlinewidth", ps_val_op(op_setlinewidth));
    ps_dict_put(&ctx->systemdict, "setgray",      ps_val_op(op_setgray));
    ps_dict_put(&ctx->systemdict, "setrgbcolor",  ps_val_op(op_setrgbcolor));
    ps_dict_put(&ctx->systemdict, "newpath",      ps_val_op(op_newpath));
    ps_dict_put(&ctx->systemdict, "moveto",       ps_val_op(op_moveto));
    ps_dict_put(&ctx->systemdict, "rmoveto",      ps_val_op(op_rmoveto));
    ps_dict_put(&ctx->systemdict, "lineto",       ps_val_op(op_lineto));
    ps_dict_put(&ctx->systemdict, "rlineto",      ps_val_op(op_rlineto));
    ps_dict_put(&ctx->systemdict, "curveto",      ps_val_op(op_curveto));
    ps_dict_put(&ctx->systemdict, "closepath",    ps_val_op(op_closepath));
    ps_dict_put(&ctx->systemdict, "stroke",       ps_val_op(op_stroke));
    ps_dict_put(&ctx->systemdict, "fill",         ps_val_op(op_fill));
    ps_dict_put(&ctx->systemdict, "eofill",       ps_val_op(op_eofill));

    ps_dict_put(&ctx->systemdict, "findfont",     ps_val_op(op_findfont));
    ps_dict_put(&ctx->systemdict, "scalefont",    ps_val_op(op_scalefont));
    ps_dict_put(&ctx->systemdict, "setfont",      ps_val_op(op_setfont));
    ps_dict_put(&ctx->systemdict, "show",         ps_val_op(op_show));
    ps_dict_put(&ctx->systemdict, "stringwidth",  ps_val_op(op_stringwidth));

    ps_dict_put(&ctx->systemdict, "showpage",     ps_val_op(op_showpage));
}

void ps_ctx_reset(PSContext *ctx) {
    if (!ctx) return;
    ps_ctx_init(ctx, &ctx->sink);
}

/* ── Evaluation Engine ──────────────────────────────────────────── */

static void ps_eval_value(PSContext *ctx, PSValue val) {
    if (!ctx) return;

    if (val.type == PS_VAL_PROC) {
        /* A procedure literal passed here is pushed onto operand stack */
        ps_push(ctx, val);
        return;
    }

    if (val.flags & 1) { /* Executable */
        if (val.type == PS_VAL_OP && val.u.op) {
            val.u.op(ctx);
        } else if (val.type == PS_VAL_NAME) {
            PSValue resolved = ps_val_null();
            if (ps_lookup(ctx, val.u.name, &resolved)) {
                if (resolved.type == PS_VAL_PROC) {
                    ps_exec_proc(ctx, resolved);
                } else {
                    ps_eval_value(ctx, resolved);
                }
            } else {
                ctx->error_flag = TRUE;
                snprintf(ctx->error_msg, sizeof(ctx->error_msg), "undefined symbol: '%s'", val.u.name);
            }
        } else {
            ps_push(ctx, val);
        }
    } else {
        /* Literal value: push onto operand stack */
        ps_push(ctx, val);
    }
}

/* ── Tokenizer & Stream Parser ──────────────────────────────────── */

static BOOL is_ps_delim(char c) {
    return (c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
            c == '(' || c == ')' || c == '{' || c == '}' ||
            c == '[' || c == ']' || c == '<' || c == '>' ||
            c == '/' || c == '%');
}

static BOOL ps_parse_token(PSContext *ctx, const char **p_in, const char *end, PSValue *out_val) {
    const char *p = *p_in;

    while (p < end) {
        /* Skip whitespace */
        while (p < end && isspace((unsigned char)*p)) p++;
        if (p >= end) break;

        /* Comment: skip to newline */
        if (*p == '%') {
            while (p < end && *p != '\n' && *p != '\r') p++;
            continue;
        }

        /* String Literal: ( ... ) */
        if (*p == '(') {
            p++;
            int depth = 1;
            char sbuf[1024];
            size_t sidx = 0;

            while (p < end && depth > 0) {
                if (*p == '\\' && p + 1 < end) {
                    p++;
                    if (*p == 'n')      sbuf[sidx++] = '\n';
                    else if (*p == 'r') sbuf[sidx++] = '\r';
                    else if (*p == 't') sbuf[sidx++] = '\t';
                    else                sbuf[sidx++] = *p;
                    p++;
                    if (sidx >= sizeof(sbuf) - 1) break;
                    continue;
                }
                if (*p == '(') depth++;
                else if (*p == ')') {
                    depth--;
                    if (depth == 0) { p++; break; }
                }
                if (sidx < sizeof(sbuf) - 1) sbuf[sidx++] = *p;
                p++;
            }
            sbuf[sidx] = '\0';
            *p_in = p;
            *out_val = ps_val_string(ctx, sbuf, (UW)sidx);
            return TRUE;
        }

        /* Procedure Block: { ... } */
        if (*p == '{') {
            p++;
            UW start_pool = ctx->obj_pool_used;
            int depth = 1;

            while (p < end && depth > 0) {
                while (p < end && isspace((unsigned char)*p)) p++;
                if (p >= end) break;
                if (*p == '%') {
                    while (p < end && *p != '\n' && *p != '\r') p++;
                    continue;
                }
                if (*p == '}') {
                    depth--;
                    p++;
                    if (depth == 0) break;
                }
                if (*p == '{') {
                    depth++;
                    /* Recursively parse nested procedure */
                    PSValue sub_proc = ps_val_null();
                    if (!ps_parse_token(ctx, &p, end, &sub_proc)) break;
                    if (ctx->obj_pool_used < PS_OBJ_POOL_SIZE) {
                        ctx->obj_pool[ctx->obj_pool_used++] = sub_proc;
                    }
                    continue;
                }

                PSValue elem = ps_val_null();
                if (!ps_parse_token(ctx, &p, end, &elem)) break;
                if (ctx->obj_pool_used < PS_OBJ_POOL_SIZE) {
                    ctx->obj_pool[ctx->obj_pool_used++] = elem;
                }
            }
            *p_in = p;
            UW len = ctx->obj_pool_used - start_pool;
            *out_val = ps_val_proc(&ctx->obj_pool[start_pool], len);
            return TRUE;
        }

        /* Literal Name: /Identifier */
        if (*p == '/') {
            p++;
            const char *start = p;
            while (p < end && !is_ps_delim(*p)) p++;
            size_t nlen = (size_t)(p - start);
            char name_buf[PS_NAME_MAX];
            if (nlen >= sizeof(name_buf)) nlen = sizeof(name_buf) - 1;
            memcpy(name_buf, start, nlen);
            name_buf[nlen] = '\0';
            *p_in = p;
            *out_val = ps_val_name(ctx, name_buf, FALSE); /* Literal */
            return TRUE;
        }

        /* Array Delimiters */
        if (*p == '[' || *p == ']') {
            p++;
            continue; /* Ignored for basic drawing */
        }

        /* Standard Token: Number or Executable Name */
        const char *start = p;
        while (p < end && !is_ps_delim(*p)) p++;
        size_t tlen = (size_t)(p - start);
        char tok[PS_NAME_MAX];
        if (tlen >= sizeof(tok)) tlen = sizeof(tok) - 1;
        memcpy(tok, start, tlen);
        tok[tlen] = '\0';
        *p_in = p;

        /* Check for Number */
        char *endptr = NULL;
        long ival = strtol(tok, &endptr, 10);
        if (endptr && *endptr == '\0') {
            *out_val = ps_val_int((W)ival);
            return TRUE;
        }
        float rval = strtof(tok, &endptr);
        if (endptr && *endptr == '\0') {
            *out_val = ps_val_real(rval);
            return TRUE;
        }

        /* Boolean */
        if (strcmp(tok, "true") == 0) {
            *out_val = ps_val_bool(TRUE);
            return TRUE;
        }
        if (strcmp(tok, "false") == 0) {
            *out_val = ps_val_bool(FALSE);
            return TRUE;
        }

        /* Executable Name */
        *out_val = ps_val_name(ctx, tok, TRUE);
        return TRUE;
    }

    *p_in = end;
    return FALSE;
}

static BOOL ps_tokenize_and_eval(PSContext *ctx, const char *code, size_t len) {
    if (!ctx || !code) return FALSE;
    const char *p = code;
    const char *end = code + len;

    while (p < end && !ctx->error_flag) {
        PSValue val = ps_val_null();
        if (!ps_parse_token(ctx, &p, end, &val)) break;
        ps_eval_value(ctx, val);
    }

    return !ctx->error_flag;
}

BOOL ps_run_token_stream(PSContext *ctx, const char *script, size_t len) {
    return ps_tokenize_and_eval(ctx, script, len);
}

BOOL ps_run_file(PSContext *ctx, const char *filepath) {
    if (!ctx || !filepath) return FALSE;
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        ctx->error_flag = TRUE;
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "cannot open file: %s", filepath);
        return FALSE;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0 || sz > 10 * 1024 * 1024) {
        fclose(f);
        ctx->error_flag = TRUE;
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "invalid file size: %ld", sz);
        return FALSE;
    }

    char *buf = (char *)malloc(sz + 1);
    if (!buf) {
        fclose(f);
        ctx->error_flag = TRUE;
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "memory allocation failed");
        return FALSE;
    }

    size_t rd = fread(buf, 1, sz, f);
    fclose(f);
    buf[rd] = '\0';

    BOOL ok = ps_tokenize_and_eval(ctx, buf, rd);
    free(buf);
    return ok;
}
