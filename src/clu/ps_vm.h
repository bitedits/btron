/*
 * B-System BTRON3 — ps_vm.h
 * Secure PostScript (sps) Forth Virtual Machine
 * Pure C99, adheres to NASA/JPL Power of Ten rules.
 */

#ifndef _BTRON_PS_VM_H_
#define _BTRON_PS_VM_H_

#include <btron/types.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PS_STACK_MAX       512
#define PS_EXEC_MAX        256
#define PS_DICT_STACK_MAX  32
#define PS_DICT_CAP_MAX    128
#define PS_NAME_MAX        64
#define PS_STR_POOL_SIZE   (128 * 1024)
#define PS_OBJ_POOL_SIZE   4096

typedef enum {
    PS_VAL_NULL = 0,
    PS_VAL_BOOL,
    PS_VAL_INT,
    PS_VAL_REAL,
    PS_VAL_NAME,
    PS_VAL_STRING,
    PS_VAL_ARRAY,
    PS_VAL_DICT,
    PS_VAL_PROC,
    PS_VAL_OP
} PSType;

struct PSContext;
struct PSDict;

typedef struct PSValue {
    PSType type;
    UW     flags;        /* bit 0: is_executable, bit 1: is_literal */
    union {
        BOOL         b;
        W            i;
        float        r;
        const char  *name;
        struct {
            char    *str;
            UW       len;
        } s;
        struct {
            struct PSValue *elems;
            UW              len;
        } a;
        struct PSDict      *d;
        void              (*op)(struct PSContext *ctx);
    } u;
} PSValue;

typedef struct {
    char    key[PS_NAME_MAX];
    PSValue val;
} PSDictEntry;

typedef struct PSDict {
    UW          count;
    UW          capacity;
    PSDictEntry entries[PS_DICT_CAP_MAX];
} PSDict;

/* Graphics state record for vector recording */
typedef struct {
    float cur_x;
    float cur_y;
    float line_width;
    float rgb[3];
    char  font_name[PS_NAME_MAX];
    float font_size;
} PSGraphicsState;

/* Callbacks for vector graphics recording (emits to PDF or DP) */
typedef struct PSGraphicsSink {
    void *user_data;
    void (*moveto)(void *ud, float x, float y);
    void (*lineto)(void *ud, float x, float y);
    void (*curveto)(void *ud, float x1, float y1, float x2, float y2, float x3, float y3);
    void (*closepath)(void *ud);
    void (*stroke)(void *ud, const PSGraphicsState *gs);
    void (*fill)(void *ud, const PSGraphicsState *gs, BOOL eofill);
    void (*show)(void *ud, const char *str, UW len, const PSGraphicsState *gs);
    void (*showpage)(void *ud, int page_num);
} PSGraphicsSink;

typedef struct PSContext {
    /* Operand Stack */
    PSValue op_stack[PS_STACK_MAX];
    W       op_top;

    /* Execution Stack */
    PSValue exec_stack[PS_EXEC_MAX];
    W       exec_top;

    /* Dictionary Stack */
    PSDict *dict_stack[PS_DICT_STACK_MAX];
    W       dict_top;

    /* Standard Dictionaries */
    PSDict  systemdict;
    PSDict  globaldict;
    PSDict  userdict;

    /* Preallocated =string buffer for cvs */
    char    eq_string_buf[128];
    PSValue eq_string_val;

    /* Static Arena Memory Pools (Zero dynamic malloc during run) */
    char    str_pool[PS_STR_POOL_SIZE];
    UW      str_pool_used;

    PSValue obj_pool[PS_OBJ_POOL_SIZE];
    UW      obj_pool_used;

    PSDict  dict_pool[64];
    UW      dict_pool_used;

    /* Graphics State & Sink */
    PSGraphicsState gs;
    PSGraphicsState gs_save_stack[16];
    W               gs_save_top;
    PSGraphicsSink  sink;
    int             page_count;

    /* Interpreter Control Flags */
    BOOL            error_flag;
    char            error_msg[128];
    BOOL            exit_flag;
} PSContext;

/* VM Lifecycle & Execution */
void    ps_ctx_init(PSContext *ctx, const PSGraphicsSink *sink);
void    ps_ctx_reset(PSContext *ctx);
BOOL    ps_run_token_stream(PSContext *ctx, const char *script, size_t len);
BOOL    ps_run_file(PSContext *ctx, const char *filepath);

/* Stack Operations */
BOOL    ps_push(PSContext *ctx, PSValue val);
PSValue ps_pop(PSContext *ctx);
PSValue ps_peek(PSContext *ctx);
BOOL    ps_has_n(const PSContext *ctx, int n);

/* Value Constructors */
PSValue ps_val_null(void);
PSValue ps_val_bool(BOOL b);
PSValue ps_val_int(W i);
PSValue ps_val_real(float r);
PSValue ps_val_name(PSContext *ctx, const char *name, BOOL is_executable);
PSValue ps_val_string(PSContext *ctx, const char *str, UW len);
PSValue ps_val_proc(PSValue *elems, UW len);
PSValue ps_val_op(void (*op)(PSContext *ctx));

/* Dictionary Operations */
PSDict *ps_dict_create(PSContext *ctx, UW capacity);
BOOL    ps_dict_put(PSDict *d, const char *key, PSValue val);
BOOL    ps_dict_get(const PSDict *d, const char *key, PSValue *out_val);
BOOL    ps_dict_def(PSContext *ctx, const char *key, PSValue val);
BOOL    ps_lookup(const PSContext *ctx, const char *key, PSValue *out_val);

/* Helper for numeric retrieval (handles both int and real) */
BOOL    ps_pop_num(PSContext *ctx, float *out_num);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_PS_VM_H_ */
