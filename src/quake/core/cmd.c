/*
 * src/quake/core/cmd.c — Console Command Engine for Quake
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#include "../include/cmd.h"

static inline int q_isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

#define MAX_COMMANDS 128
#define MAX_ARGS     16

typedef struct {
    const char *name;
    xcommand_t  function;
} cmd_function_t;

static cmd_function_t s_commands[MAX_COMMANDS];
static int            s_num_commands = 0;

static int   s_cmd_argc = 0;
static char  s_cmd_argv[MAX_ARGS][64];

void Cmd_Init(void) {
    s_num_commands = 0;
    s_cmd_argc = 0;
}

void Cmd_AddCommand(const char *cmd_name, xcommand_t function) {
    if (!cmd_name || !function || s_num_commands >= MAX_COMMANDS) return;
    s_commands[s_num_commands].name = cmd_name;
    s_commands[s_num_commands].function = function;
    s_num_commands++;
}

int Cmd_Argc(void) {
    return s_cmd_argc;
}

char *Cmd_Argv(int arg) {
    if (arg < 0 || arg >= s_cmd_argc) return "";
    return s_cmd_argv[arg];
}

void Cmd_ExecuteString(const char *text) {
    if (!text || !*text) return;

    /* Tokenize whitespace-separated line */
    s_cmd_argc = 0;
    const char *p = text;
    while (*p && s_cmd_argc < MAX_ARGS) {
        while (*p && q_isspace((unsigned char)*p)) p++;
        if (!*p) break;

        int len = 0;
        while (*p && !q_isspace((unsigned char)*p) && len < 63) {
            s_cmd_argv[s_cmd_argc][len++] = *p++;
        }
        s_cmd_argv[s_cmd_argc][len] = '\0';
        s_cmd_argc++;
    }

    if (s_cmd_argc == 0) return;

    /* Match command */
    for (int i = 0; i < s_num_commands; i++) {
        if (strcmp(s_commands[i].name, s_cmd_argv[0]) == 0) {
            s_commands[i].function();
            return;
        }
    }

    Con_DPrintf("Unknown command \"%s\"\n", s_cmd_argv[0]);
}
