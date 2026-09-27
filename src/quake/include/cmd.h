/*
 * src/quake/include/cmd.h — Console Command Engine for Quake
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#ifndef QUAKE_CMD_H
#define QUAKE_CMD_H

#include "quakedef.h"

typedef void (*xcommand_t)(void);

void  Cmd_Init(void);
void  Cmd_AddCommand(const char *cmd_name, xcommand_t function);
int   Cmd_Argc(void);
char *Cmd_Argv(int arg);
void  Cmd_ExecuteString(const char *text);

#endif /* QUAKE_CMD_H */
