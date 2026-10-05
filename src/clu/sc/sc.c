/*
 * B-System BTRON3 — sc.c
 * Sokhatsky Commander (SC) Dual-Pane File Manager Main Program.
 * NASA JPL Power of Ten compliant, pure C99, universal portable code.
 * Zero host OS dependencies; cell diffing via term.h, VFS via vfs.h.
 */
#define SC_INTERNAL 1
#include "sokhatsky.h"

/* Global Context */
static ScContext g_default_sc = {
    ._insert_mode = 1
};
ScContext *g_sc = &g_default_sc;

ScContext *sc_context_create(void)
{
    ScContext *sc = (ScContext *)calloc(1, sizeof(ScContext));
    if (!sc) return NULL;
    sc->_active_panel = &sc->_left_panel;
    sc->_insert_mode = 1;
    return sc;
}

void sc_context_destroy(ScContext *sc)
{
    if (sc && sc != &g_default_sc) {
        if (g_sc == sc) g_sc = &g_default_sc;
        if (sc->tv_ctx) {
            tv_context_destroy(sc->tv_ctx);
            sc->tv_ctx = NULL;
        }
        free(sc);
    }
}

void sc_set_context(ScContext *sc)
{
    g_sc = sc ? sc : &g_default_sc;
    if (!g_sc->_active_panel) g_sc->_active_panel = &g_sc->_left_panel;
}

ScContext *sc_get_context(void)
{
    return g_sc;
}

static void sc_init_styles(void)
{
    term_style(SC_STYLE_RESET, "0");
    term_style(SC_STYLE_HEADER, "1;33;44");       /* bold yellow on blue */
    term_style(SC_STYLE_TEXT, "37;44");          /* white on blue */
    term_style(SC_STYLE_HIGHLIGHT, "30;46");     /* black on cyan */
    term_style(SC_STYLE_DIR, "1;37;44");         /* bold white on blue */
    term_style(SC_STYLE_BORDER, "36;44");        /* cyan on blue */
    term_style(SC_STYLE_BOTTOM, "30;47");        /* black on white */
    term_style(SC_STYLE_MENU, "30;46");          /* black on cyan */
    term_style(SC_STYLE_MENU_SEL, "30;47");      /* black on white */
    term_style(SC_STYLE_PINK, "37;45");          /* white on magenta */
    term_style(SC_STYLE_WHITE, "1;37;45");       /* bold white on magenta */
    term_style(SC_STYLE_BUTTON_HL, "30;47");     /* black on white */
}

void normalize(Panel *p)
{
    if (p == NULL || p->file_count <= 0) return;
    if (p->cursor < 0 || p->cursor >= p->file_count) return;

    File *f = &p->files[p->cursor];
    if (!f->is_dir) return;

    if (p->dir_history_count < MAX_DIR_HISTORY) {
        DirHistory *dh = &p->dir_history[p->dir_history_count++];
        size_t plen = strlen(p->path);
        if (plen >= sizeof(dh->parent_path)) plen = sizeof(dh->parent_path) - 1;
        memcpy(dh->parent_path, p->path, plen);
        dh->parent_path[plen] = '\0';

        size_t nlen = strlen(f->name);
        if (nlen >= sizeof(dh->dir_name)) nlen = sizeof(dh->dir_name) - 1;
        memcpy(dh->dir_name, f->name, nlen);
        dh->dir_name[nlen] = '\0';

        dh->cursor_pos = p->cursor;
    }

    char combined[VFS_MAX_PATH];
    snprintf(combined, sizeof(combined), "%s/%s", p->path, f->name);
    vfs_normalize_path(combined, p->path, sizeof(p->path));

    p->cursor = 0;
    p->scroll_offset = 0;
    load_files(p);
}

void left_navigation(Panel *p)
{
    if (p == NULL) return;
    if (strcmp(p->path, "/") == 0) return;

    char dir_name[VFS_MAX_NAME];
    dir_name[0] = '\0';

    char *last_slash = strrchr(p->path, '/');
    if (last_slash != NULL) {
        if (last_slash == p->path) {
            /* Parent is root "/" */
            size_t nlen = strlen(last_slash + 1);
            if (nlen >= sizeof(dir_name)) nlen = sizeof(dir_name) - 1;
            memcpy(dir_name, last_slash + 1, nlen);
            dir_name[nlen] = '\0';
            p->path[1] = '\0';
        } else {
            size_t nlen = strlen(last_slash + 1);
            if (nlen >= sizeof(dir_name)) nlen = sizeof(dir_name) - 1;
            memcpy(dir_name, last_slash + 1, nlen);
            dir_name[nlen] = '\0';
            *last_slash = '\0';
        }
    }

    load_files(p);

    /* Restore cursor position if found in dir_history */
    for (int i = p->dir_history_count - 1; i >= 0; i--) {
        DirHistory *dh = &p->dir_history[i];
        if (strcmp(dh->parent_path, p->path) == 0 && strcmp(dh->dir_name, dir_name) == 0) {
            p->cursor = dh->cursor_pos;
            if (p->cursor >= p->file_count) p->cursor = p->file_count - 1;
            if (p->cursor < 0) p->cursor = 0;
            p->dir_history_count--;
            break;
        }
    }
}

int sc_is_tv_active(void)
{
    return g_sc_tv_active;
}

int sc_launch_tv(const char *filepath, int view_only)
{
    if (!g_sc->tv_ctx) {
        g_sc->tv_ctx = tv_context_create();
    }
    tv_set_context(g_sc->tv_ctx);
    if (tv_session_init(filepath, view_only, term_rows, term_cols) == 0) {
        g_sc_tv_active = 1;
        return 0;
    }
    return -1;
}

int sc_session_init(const char *start_path, int rows, int cols)
{
    g_sc_tv_active = 0;
    if (vfs_init() < 0) return -1;
    if (rows > 0 && cols > 0) {
        term_set_size(rows, cols);
    }
    sc_init_styles();

    /* Initialize left and right panels */
    memset(&left_panel, 0, sizeof(left_panel));
    memset(&right_panel, 0, sizeof(right_panel));

    const char *p = (start_path && start_path[0]) ? start_path : "/SYS";
    vfs_normalize_path(p, left_panel.path, sizeof(left_panel.path));
    vfs_normalize_path(p, right_panel.path, sizeof(right_panel.path));

    left_panel.sort_type = 0;
    right_panel.sort_type = 0;
    active_panel = &left_panel;

    load_files(&left_panel);
    load_files(&right_panel);

    command_buffer[0] = '\0';
    cmd_cursor_pos = 0;
    cmd_display_offset = 0;
    show_command_buffer = 0;

    draw_interface();
    scr_flush();
    return 0;
}

void sc_session_close(void)
{
    if (g_sc_tv_active) {
        if (g_sc->tv_ctx) tv_set_context(g_sc->tv_ctx);
        tv_session_close();
        g_sc_tv_active = 0;
    }
    term_close();
}

int sc_session_step(int c)
{
    if (c == K_NONE) return 1;
    if (c == K_EOF) return 0;

    if (g_sc_tv_active) {
        if (g_sc->tv_ctx) tv_set_context(g_sc->tv_ctx);
        int running = tv_session_step(c);
        if (!running) {
            tv_session_close();
            g_sc_tv_active = 0;
            load_files(&left_panel);
            load_files(&right_panel);
            scr_invalidate();
            draw_interface();
            scr_flush();
        }
        return 1;
    }

    if (c == K_RESIZE) {
        (void)term_resize();
        draw_interface();
        scr_flush();
        return 1;
    }


        if (c == KEY_CTRL_O) {
            show_command_buffer = !show_command_buffer;
            draw_interface();
        } else if (c == KEY_TAB) {
            active_panel = (active_panel == &left_panel) ? &right_panel : &left_panel;
            draw_interface();
        } else if (c == KEY_UP) {
            if (!show_command_buffer) {
                if (active_panel->cursor > 0) {
                    active_panel->cursor--;
                    draw_interface();
                }
            } else {
                if (history_display_offset > 0) {
                    history_display_offset--;
                    draw_interface();
                }
            }
        } else if (c == KEY_DOWN) {
            if (!show_command_buffer) {
                if (active_panel->cursor < active_panel->file_count - 1) {
                    active_panel->cursor++;
                    draw_interface();
                }
            } else {
                history_display_offset++;
                draw_interface();
            }
        } else if (c == KEY_PGUP) {
            if (!show_command_buffer) {
                int pg = term_rows - 7;
                if (pg < 1) pg = 1;
                active_panel->cursor -= pg;
                if (active_panel->cursor < 0) active_panel->cursor = 0;
                draw_interface();
            }
        } else if (c == KEY_PGDOWN) {
            if (!show_command_buffer) {
                int pg = term_rows - 7;
                if (pg < 1) pg = 1;
                active_panel->cursor += pg;
                if (active_panel->cursor >= active_panel->file_count) {
                    active_panel->cursor = active_panel->file_count > 0 ? active_panel->file_count - 1 : 0;
                }
                draw_interface();
            }
        } else if (c == KEY_HOME) {
            if (command_buffer[0] != '\0') {
                cmd_cursor_pos = 0;
                cmd_display_offset = 0;
                draw_command_line();
                scr_flush();
            } else if (!show_command_buffer) {
                active_panel->cursor = 0;
                active_panel->scroll_offset = 0;
                draw_interface();
            }
        } else if (c == KEY_END) {
            if (command_buffer[0] != '\0') {
                cmd_cursor_pos = (int)strlen(command_buffer);
                draw_command_line();
                scr_flush();
            } else if (!show_command_buffer) {
                if (active_panel->file_count > 0) {
                    active_panel->cursor = active_panel->file_count - 1;
                }
                draw_interface();
            }
        } else if (c == KEY_LEFT) {
            if (command_buffer[0] != '\0') {
                if (cmd_cursor_pos > 0) {
                    cmd_cursor_pos--;
                    if (cmd_cursor_pos < cmd_display_offset) {
                        cmd_display_offset = cmd_cursor_pos;
                    }
                    draw_command_line();
                    scr_flush();
                }
            } else if (!show_command_buffer) {
                left_navigation(active_panel);
                draw_interface();
            }
        } else if (c == KEY_RIGHT) {
            if (command_buffer[0] != '\0') {
                int len = (int)strlen(command_buffer);
                if (cmd_cursor_pos < len) {
                    cmd_cursor_pos++;
                    draw_command_line();
                    scr_flush();
                }
            } else if (!show_command_buffer) {
                if (active_panel->file_count > 0 && active_panel->files[active_panel->cursor].is_dir) {
                    if (strcmp(active_panel->files[active_panel->cursor].name, "..") == 0) {
                        left_navigation(active_panel);
                    } else {
                        normalize(active_panel);
                    }
                    draw_interface();
                }
            }
        } else if (c == KEY_ENTER) {
            if (command_buffer[0] != '\0') {
                show_command_buffer = 1;
                execute_command(command_buffer);
                command_buffer[0] = '\0';
                cmd_cursor_pos = 0;
                cmd_display_offset = 0;
                draw_interface();
            } else if (!show_command_buffer && active_panel->file_count > 0) {
                File *f = &active_panel->files[active_panel->cursor];
                if (strcmp(f->name, "..") == 0) {
                    left_navigation(active_panel);
                    draw_interface();
                } else if (f->is_dir) {
                    normalize(active_panel);
                    draw_interface();
                } else {
                    /* View file */
                    char fpath[VFS_MAX_PATH];
                    snprintf(fpath, sizeof(fpath), "%s/%s", active_panel->path, f->name);
                    vfs_normalize_path(fpath, fpath, sizeof(fpath));
                    if (sc_launch_tv(fpath, 1) == 0) return 1;
                    draw_interface();
                }
            }
        } else if (c == KEY_BACKSPACE) {
            int len = (int)strlen(command_buffer);
            if (cmd_cursor_pos > 0 && len > 0) {
                memmove(&command_buffer[cmd_cursor_pos - 1], &command_buffer[cmd_cursor_pos], len - cmd_cursor_pos + 1);
                cmd_cursor_pos--;
                if (cmd_cursor_pos < cmd_display_offset) cmd_display_offset = cmd_cursor_pos;
                draw_command_line();
                scr_flush();
            }
        } else if (c == KEY_DELETE) {
            int len = (int)strlen(command_buffer);
            if (cmd_cursor_pos < len) {
                memmove(&command_buffer[cmd_cursor_pos], &command_buffer[cmd_cursor_pos + 1], len - cmd_cursor_pos);
                draw_command_line();
                scr_flush();
            }
        } else if (c == KEY_INSERT) {
            insert_mode = !insert_mode;
        } else if (c >= 32 && c < K_BASE) {
            int len = (int)strlen(command_buffer);
            char enc[4];
            int elen = lang_encode((uint32_t)c, enc);

            if (len + elen < (int)sizeof(command_buffer) - 1) {
                if (insert_mode) {
                    memmove(&command_buffer[cmd_cursor_pos + elen], &command_buffer[cmd_cursor_pos], len - cmd_cursor_pos + 1);
                    memcpy(&command_buffer[cmd_cursor_pos], enc, elen);
                } else {
                    memcpy(&command_buffer[cmd_cursor_pos], enc, elen);
                    if (cmd_cursor_pos + elen > len) command_buffer[cmd_cursor_pos + elen] = '\0';
                }
                cmd_cursor_pos += elen;
                draw_command_line();
                scr_flush();
            }
        } else if (c == KEY_F1) {
            execute_command("help");
            show_command_buffer = 1;
            draw_interface();
        } else if (c == KEY_F3) {
            if (active_panel->file_count > 0 && !active_panel->files[active_panel->cursor].is_dir) {
                char fpath[VFS_MAX_PATH];
                snprintf(fpath, sizeof(fpath), "%s/%s", active_panel->path, active_panel->files[active_panel->cursor].name);
                vfs_normalize_path(fpath, fpath, sizeof(fpath));
                if (sc_launch_tv(fpath, 1) == 0) return 1;
                draw_interface();
            }
        } else if (c == KEY_F4) {
            if (active_panel->file_count > 0 && !active_panel->files[active_panel->cursor].is_dir) {
                char fpath[VFS_MAX_PATH];
                snprintf(fpath, sizeof(fpath), "%s/%s", active_panel->path, active_panel->files[active_panel->cursor].name);
                vfs_normalize_path(fpath, fpath, sizeof(fpath));
                if (sc_launch_tv(fpath, 0) == 0) return 1;
                load_files(active_panel);
                draw_interface();
            }
        } else if (c == KEY_F5) {
            /* Copy current file to the opposite panel */
            Panel *other = (active_panel == &left_panel) ? &right_panel : &left_panel;
            if (active_panel->file_count > 0 && !active_panel->files[active_panel->cursor].is_dir) {
                char src[VFS_MAX_PATH], dst[VFS_MAX_PATH];
                snprintf(src, sizeof(src), "%s/%s", active_panel->path, active_panel->files[active_panel->cursor].name);
                snprintf(dst, sizeof(dst), "%s/%s", other->path, active_panel->files[active_panel->cursor].name);
                vfs_normalize_path(src, src, sizeof(src));
                vfs_normalize_path(dst, dst, sizeof(dst));
                (void)vfs_copy(src, dst);
                load_files(other);
                draw_interface();
            }
        } else if (c == KEY_F7) {
            /* Create new file in current panel */
            char fpath[VFS_MAX_PATH];
            snprintf(fpath, sizeof(fpath), "%s/new.txt", active_panel->path);
            vfs_normalize_path(fpath, fpath, sizeof(fpath));
            (void)vfs_write_file(fpath, "", 0);
            load_files(active_panel);
            draw_interface();
        } else if (c == KEY_F8) {
            /* Delete current file */
            if (active_panel->file_count > 0 && !active_panel->files[active_panel->cursor].is_dir) {
                char fpath[VFS_MAX_PATH];
                snprintf(fpath, sizeof(fpath), "%s/%s", active_panel->path, active_panel->files[active_panel->cursor].name);
                vfs_normalize_path(fpath, fpath, sizeof(fpath));
                (void)vfs_delete(fpath);
                load_files(active_panel);
                draw_interface();
            }
        } else if (c == KEY_F9) {
            if (handle_menu()) return 0;
            draw_interface();
        } else if (c == KEY_F10 || c == K_CTRL('Q')) {
            if (handle_exit_dialog()) return 0;
            draw_interface();
        }

    scr_flush();
    return 1;
}

int sc_main(int argc, char *argv[])
{
    if (term_open() < 0) return -1;
    const char *start_path = (argc > 1 && argv[1] && argv[1][0]) ? argv[1] : "/SYS";
    if (sc_session_init(start_path, 0, 0) < 0) {
        term_close();
        return -1;
    }

    while (1) {
        int c = term_key();
        if (c == K_NONE) continue;
        if (!sc_session_step(c)) break;
    }

    sc_session_close();
    return 0;
}


#ifndef SC_NO_MAIN
int main(int argc, char *argv[])
{
    return sc_main(argc, argv);
}
#endif

