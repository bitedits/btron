/*
 * B-System BTRON3 — input.c
 * Sokhatsky Commander input dispatcher and internal B-System command executor.
 * Completely self-contained; NEVER portals commands to host OS.
 */
#define SC_INTERNAL 1
#include "sokhatsky.h"
#include "../../apps/clu.h"

typedef struct {
    char  *buf;
    size_t max;
    size_t len;
} CmdOutputCtx;

static void clu_collect_output(const char *line, COLOR col, void *ud)
{
    (void)col;
    CmdOutputCtx *ctx = (CmdOutputCtx *)ud;
    if (ctx == NULL || ctx->buf == NULL || line == NULL) return;

    size_t llen = strlen(line);
    if (ctx->len + llen + 2 < ctx->max) {
        memcpy(ctx->buf + ctx->len, line, llen);
        ctx->len += llen;
        ctx->buf[ctx->len++] = '\n';
        ctx->buf[ctx->len] = '\0';
    }
}

void finalize_exec(const char *cmd, const char *out_text)
{
    if (cmd == NULL) return;

    CommandEntry *e = &history[history_start];
    size_t clen = strlen(cmd);
    if (clen >= sizeof(e->command)) clen = sizeof(e->command) - 1;
    memcpy(e->command, cmd, clen);
    e->command[clen] = '\0';

    if (out_text != NULL) {
        size_t olen = strlen(out_text);
        if (olen >= sizeof(e->output)) olen = sizeof(e->output) - 1;
        memcpy(e->output, out_text, olen);
        e->output[olen] = '\0';
    } else {
        e->output[0] = '\0';
    }

    history_start = (history_start + 1) % MAX_HISTORY;
    if (history_count < MAX_HISTORY) {
        history_count++;
    }

    append_to_history_display(cmd, out_text ? out_text : "");
    command_buffer[0] = '\0';
    cmd_cursor_pos = 0;
    cmd_display_offset = 0;
}

void execute_command(const char *cmd)
{
    if (cmd == NULL || cmd[0] == '\0') return;

    /* Skip leading whitespace */
    const char *p = cmd;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0') return;

    char cmd_name[64];
    size_t cni = 0;
    while (*p != '\0' && *p != ' ' && *p != '\t' && cni + 1 < sizeof(cmd_name)) {
        cmd_name[cni++] = *p++;
    }
    cmd_name[cni] = '\0';

    while (*p == ' ' || *p == '\t') p++;
    const char *arg = p;

    static char out_buf[4096];
    out_buf[0] = '\0';
    CmdOutputCtx ctx = { out_buf, sizeof(out_buf), 0 };

    /* ── Internal B-System Command Dispatch ────────────────────────── */
    if (strcmp(cmd_name, "help") == 0) {
        snprintf(out_buf, sizeof(out_buf),
                 "Sokhatsky Commander (SC) B-System Commands:\n"
                 "  cd <path>        Change directory\n"
                 "  pwd              Print working directory\n"
                 "  ls [-l]          List directory / volume contents\n"
                 "  tp <file>        Display file contents (B-System Real Body)\n"
                 "  tv <file>        Open Terminal Vision editor\n"
                 "  view <file>      View file with Terminal Vision\n"
                 "  cp <src> <dst>   Copy file\n"
                 "  rm <file>        Remove file\n"
                 "  ren <src> <dst>  Rename file\n"
                 "  mkf <file>       Create file\n"
                 "  df               Volume free space statistics\n"
                 "  stat <file>      File attributes and record statistics\n"
                 "  info             Mounted volumes overview\n"
                 "  sync             Flush volume buffers to storage\n"
                 "  clear            Clear command history\n");
    } else if (strcmp(cmd_name, "pwd") == 0) {
        snprintf(out_buf, sizeof(out_buf), "%s\n", active_panel ? active_panel->path : "/");
    } else if (strcmp(cmd_name, "cd") == 0) {
        if (arg[0] == '\0' || strcmp(arg, "~") == 0) {
            strcpy(active_panel->path, "/SYS");
        } else if (arg[0] == '/') {
            vfs_normalize_path(arg, active_panel->path, sizeof(active_panel->path));
        } else {
            char combined[VFS_MAX_PATH];
            snprintf(combined, sizeof(combined), "%s/%s", active_panel->path, arg);
            vfs_normalize_path(combined, active_panel->path, sizeof(active_panel->path));
        }
        load_files(active_panel);
        snprintf(out_buf, sizeof(out_buf), "%s\n", active_panel->path);
    } else if (strcmp(cmd_name, "tv") == 0 || strcmp(cmd_name, "edit") == 0) {
        char fpath[VFS_MAX_PATH];
        if (arg[0] == '/') {
            vfs_normalize_path(arg, fpath, sizeof(fpath));
        } else {
            snprintf(fpath, sizeof(fpath), "%s/%s", active_panel->path, arg[0] ? arg : "new.txt");
            vfs_normalize_path(fpath, fpath, sizeof(fpath));
        }
        (void)sc_launch_tv(fpath, 0);
        snprintf(out_buf, sizeof(out_buf), "Edited: %s\n", fpath);
    } else if (strcmp(cmd_name, "view") == 0) {
        char fpath[VFS_MAX_PATH];
        if (arg[0] == '/') {
            vfs_normalize_path(arg, fpath, sizeof(fpath));
        } else {
            snprintf(fpath, sizeof(fpath), "%s/%s", active_panel->path, arg);
            vfs_normalize_path(fpath, fpath, sizeof(fpath));
        }
        (void)sc_launch_tv(fpath, 1);
        snprintf(out_buf, sizeof(out_buf), "Viewed: %s\n", fpath);
    } else if (strcmp(cmd_name, "clear") == 0) {
        history_count = 0;
        history_start = 0;
        history_scroll_pos = 0;
        out_buf[0] = '\0';
    } else if (strcmp(cmd_name, "ls") == 0) {
        clu_ls(arg, clu_collect_output, &ctx);
    } else if (strcmp(cmd_name, "tp") == 0 || strcmp(cmd_name, "cat") == 0) {
        clu_tp(arg, clu_collect_output, &ctx);
    } else if (strcmp(cmd_name, "df") == 0) {
        clu_df(arg, clu_collect_output, &ctx);
    } else if (strcmp(cmd_name, "stat") == 0) {
        clu_stat(arg, clu_collect_output, &ctx);
    } else if (strcmp(cmd_name, "info") == 0) {
        clu_info(arg, clu_collect_output, &ctx);
    } else if (strcmp(cmd_name, "finfo") == 0) {
        clu_finfo(arg, clu_collect_output, &ctx);
    } else if (strcmp(cmd_name, "cp") == 0) {
        clu_cp(arg, clu_collect_output, &ctx);
        load_files(&left_panel);
        load_files(&right_panel);
    } else if (strcmp(cmd_name, "rm") == 0) {
        clu_rm(arg, clu_collect_output, &ctx);
        load_files(&left_panel);
        load_files(&right_panel);
    } else if (strcmp(cmd_name, "ren") == 0 || strcmp(cmd_name, "mv") == 0) {
        clu_ren(arg, clu_collect_output, &ctx);
        load_files(&left_panel);
        load_files(&right_panel);
    } else if (strcmp(cmd_name, "mkf") == 0 || strcmp(cmd_name, "touch") == 0) {
        clu_mkf(arg, clu_collect_output, &ctx);
        load_files(&left_panel);
        load_files(&right_panel);
    } else if (strcmp(cmd_name, "sync") == 0) {
        clu_sync_cmd(arg, clu_collect_output, &ctx);
    } else {
        snprintf(out_buf, sizeof(out_buf), "Unknown B-System command: %s (type 'help')\n", cmd_name);
    }

    finalize_exec(cmd, out_buf);
}

int get_input(void)
{
    return term_key();
}
