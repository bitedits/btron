/*
 * src/quake/include/cvar.h — Console Variables for Quake
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#ifndef QUAKE_CVAR_H
#define QUAKE_CVAR_H

#include "quakedef.h"

#define CVAR_ARCHIVE 1
#define CVAR_NOTIFY  2

typedef struct cvar_s {
    const char *name;
    const char *string;
    int         flags;
    float       value;
    struct cvar_s *next;
} cvar_t;

void  Cvar_RegisterVariable(cvar_t *variable);
void  Cvar_Set(const char *var_name, const char *value);
void  Cvar_SetValue(const char *var_name, float value);
float Cvar_VariableValue(const char *var_name);
const char *Cvar_VariableString(const char *var_name);

#endif /* QUAKE_CVAR_H */
