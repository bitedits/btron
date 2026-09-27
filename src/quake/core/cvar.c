/*
 * src/quake/core/cvar.c — Console Variables for Quake
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#include "../include/cvar.h"
#include <stdlib.h>

static cvar_t *s_cvar_vars = NULL;


void Cvar_RegisterVariable(cvar_t *variable) {
    if (!variable || !variable->name) return;

    /* Check if already registered */
    for (cvar_t *v = s_cvar_vars; v; v = v->next) {
        if (strcmp(v->name, variable->name) == 0) return;
    }

    variable->value = q_atof(variable->string ? variable->string : "0");
    variable->next = s_cvar_vars;
    s_cvar_vars = variable;
}

void Cvar_Set(const char *var_name, const char *value) {
    if (!var_name || !value) return;
    for (cvar_t *v = s_cvar_vars; v; v = v->next) {
        if (strcmp(v->name, var_name) == 0) {
            v->string = value;
            v->value = q_atof(value);
            return;
        }
    }
}

void Cvar_SetValue(const char *var_name, float value) {
    static char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", value);
    Cvar_Set(var_name, buf);
}

float Cvar_VariableValue(const char *var_name) {
    if (!var_name) return 0.0f;
    for (cvar_t *v = s_cvar_vars; v; v = v->next) {
        if (strcmp(v->name, var_name) == 0) {
            return v->value;
        }
    }
    return 0.0f;
}

const char *Cvar_VariableString(const char *var_name) {
    if (!var_name) return "";
    for (cvar_t *v = s_cvar_vars; v; v = v->next) {
        if (strcmp(v->name, var_name) == 0) {
            return v->string ? v->string : "";
        }
    }
    return "";
}
