/*
 * src/quake/include/progs.h — QuakeC Progs.dat VM Types & Interfaces
 *
 * Defines the on-disk progs.dat format (header, statements, defs, functions)
 * and the in-memory edict + world state structures for the QuakeC interpreter.
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#ifndef QUAKE_PROGS_H
#define QUAKE_PROGS_H

#include "quakedef.h"

/* ── Progs.dat on-disk header ────────────────────────────────────────── */
#define PROG_VERSION  6

typedef struct {
    int version;
    int crc;           /* Checksum of function definitions */

    int ofs_statements;
    int num_statements;

    int ofs_globaldefs;
    int num_globaldefs;

    int ofs_fielddefs;
    int num_fielddefs;

    int ofs_functions;
    int num_functions;

    int ofs_strings;
    int num_strings;   /* Total byte count of string section */

    int ofs_globals;
    int num_globals;   /* 32-bit float count */

    int entityfields;  /* Number of 32-bit fields per edict */
} dprograms_t;

/* ── Statement (opcode + 3 operand offsets) ─────────────────────────── */
typedef struct {
    uint16_t op;
    int16_t  a, b, c;
} dstatement_t;

/* ── Definition (name/type/offset triplet) ──────────────────────────── */
#define DEF_SAVEGLOBAL  (1u << 15)
typedef struct {
    uint16_t type;
    uint16_t ofs;
    int      s_name;  /* Offset into string table */
} ddef_t;

/* ── Function definition ────────────────────────────────────────────── */
typedef struct {
    int  first_statement;   /* > 0 = QC, <= 0 = builtin index */
    int  parm_start;
    int  locals;
    int  profile;           /* Execution count (debug) */
    int  s_name;
    int  s_file;
    int  numparms;
    byte parm_size[8];
} dfunction_t;

/* ── QuakeC opcodes (subset needed for playable game) ───────────────── */
#define OP_DONE      0
#define OP_MUL_F     1
#define OP_MUL_V     2
#define OP_MUL_FV    3
#define OP_MUL_VF    4
#define OP_DIV_F     5
#define OP_ADD_F     6
#define OP_ADD_V     7
#define OP_SUB_F     8
#define OP_SUB_V     9
#define OP_EQ_F     10
#define OP_EQ_V     11
#define OP_EQ_S     12
#define OP_EQ_E     13
#define OP_EQ_FNC   14
#define OP_NE_F     15
#define OP_NE_V     16
#define OP_NE_S     17
#define OP_NE_E     18
#define OP_NE_FNC   19
#define OP_LE       20
#define OP_GE       21
#define OP_LT       22
#define OP_GT       23
#define OP_LOAD_F   24
#define OP_LOAD_V   25
#define OP_LOAD_S   26
#define OP_LOAD_ENT 27
#define OP_LOAD_FLD 28
#define OP_LOAD_FNC 29
#define OP_ADDRESS  30
#define OP_STORE_F  31
#define OP_STORE_V  32
#define OP_STORE_S  33
#define OP_STORE_ENT 34
#define OP_STORE_FLD 35
#define OP_STORE_FNC 36
#define OP_STOREP_F 37
#define OP_STOREP_V 38
#define OP_STOREP_S 39
#define OP_STOREP_ENT 40
#define OP_STOREP_FLD 41
#define OP_STOREP_FNC 42
#define OP_RETURN   43
#define OP_NOT_F    44
#define OP_NOT_V    45
#define OP_NOT_S    46
#define OP_NOT_ENT  47
#define OP_NOT_FNC  48
#define OP_IF       49
#define OP_IFNOT    50
#define OP_CALL0    51
#define OP_CALL1    52
#define OP_CALL2    53
#define OP_CALL3    54
#define OP_CALL4    55
#define OP_CALL5    56
#define OP_CALL6    57
#define OP_CALL7    58
#define OP_CALL8    59
#define OP_STATE    60
#define OP_GOTO     61
#define OP_AND      62
#define OP_OR       63
#define OP_BITAND   64
#define OP_BITOR    65

/* ── Edict (entity) ─────────────────────────────────────────────────── */
#define MAX_EDICTS      600
#define EDICT_FIELDS    256     /* Max 32-bit fields per edict (vanilla progs.dat has 195) */

typedef union {
    float   f;
    int     i;
    float   v[3];
} eval_t;

typedef struct edict_s {
    int   free;
    float free_time;
    eval_t v[EDICT_FIELDS];   /* Field values indexed by ddef_t.ofs */
} edict_t;

/* ── VM state ───────────────────────────────────────────────────────── */
#define MAX_STACK_DEPTH 32

typedef struct {
    int  s;                 /* Statement index */
    dfunction_t *f;
} prstack_t;

typedef struct {
    /* Loaded progs image */
    byte        *progs_data;
    int          progs_len;

    dprograms_t *header;
    dstatement_t *statements;
    ddef_t       *globaldefs;
    ddef_t       *fielddefs;
    dfunction_t  *functions;
    char         *strings;
    float        *globals;

    /* Edict table */
    edict_t      edicts[MAX_EDICTS];
    int          num_edicts;

    /* Execution state */
    prstack_t    stack[MAX_STACK_DEPTH];
    int          depth;
    int          xstatement;

    int          is_loaded;
} prvm_t;

extern prvm_t g_prvm;

/* ── API ────────────────────────────────────────────────────────────── */
int   PR_LoadProgs(const char *path);
void  PR_ExecuteProgram(int fnum);
void  PR_RunError(const char *fmt, ...);

/* String helpers */
const char *PR_GetString(int ofs);
int         PR_SetString(const char *s);

/* Edict helpers */
edict_t    *EDICT_NUM(int num);
int         NUM_FOR_EDICT(const edict_t *ed);
eval_t     *PR_GetEntityField(edict_t *ed, int ofs);

/* Builtins called from QC */
void        PR_ExecuteBuiltin(int bnum);

#endif /* QUAKE_PROGS_H */
