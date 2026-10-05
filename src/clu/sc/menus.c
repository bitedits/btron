/*
 * B-System BTRON3 — menus.c
 * Sokhatsky Commander (SC) user interface and menu engine.
 * NASA JPL Power of Ten compliant, pure C99, universal portable code.
 * Zero host OS dependencies; cell diffing via term.h.
 */
#include "sokhatsky.h"

void draw_panel_border(int start_col, int start_row, int width, int height, int style)
{
    if (width < 2 || height < 2) return;

    scr_str(start_row, start_col, "┌", style);
    for (int i = 1; i < width - 1; i++) {
        scr_str(start_row, start_col + i, "─", style);
    }
    scr_str(start_row, start_col + width - 1, "┐", style);

    for (int r = start_row + 1; r < start_row + height - 1; r++) {
        scr_str(r, start_col, "│", style);
        scr_str(r, start_col + width - 1, "│", style);
    }

    scr_str(start_row + height - 1, start_col, "└", style);
    for (int i = 1; i < width - 1; i++) {
        scr_str(start_row + height - 1, start_col + i, "─", style);
    }
    scr_str(start_row + height - 1, start_col + width - 1, "┘", style);
}

void draw_panel(Panel *panel, int start_col, int width, int is_active)
{
    if (panel == NULL || width < 10) return;

    int panel_h = term_rows - 3;
    if (panel_h < 5) return;
    int visible_files = panel_h - 4;

    /* Scroll offset clamping */
    if (panel->cursor < panel->scroll_offset) {
        panel->scroll_offset = panel->cursor;
    } else if (panel->cursor >= panel->scroll_offset + visible_files) {
        panel->scroll_offset = panel->cursor - visible_files + 1;
    }
    if (panel->scroll_offset < 0) panel->scroll_offset = 0;

    /* Draw outer border */
    draw_panel_border(start_col, 1, width, panel_h, SC_STYLE_BORDER);

    /* Panel header: directory path */
    char path_hdr[VFS_MAX_PATH + 4];
    snprintf(path_hdr, sizeof(path_hdr), " %s ", panel->path);
    scr_text(1, start_col + 2, width - 4, path_hdr, strlen(path_hdr),
             is_active ? SC_STYLE_HEADER : SC_STYLE_TEXT, 0);

    /* Column layout: Name (variable), Size (9), Date (14) */
    int name_width = width - 4 - 23;
    if (name_width < 6) name_width = 6;
    int size_width = 9;
    int date_width = 14;
    int sep1 = start_col + 1 + name_width;
    int sep2 = sep1 + size_width;

    /* Column header bar at row 2 */
    char col_hdr[128];
    snprintf(col_hdr, sizeof(col_hdr), "%-*s│%-*s│%-*s",
             name_width, "Name", size_width, "Size/Type", date_width, "Date   |Time");
    scr_text(2, start_col + 1, width - 2, col_hdr, strlen(col_hdr), SC_STYLE_TEXT, 0);

    /* File entries */
    for (int i = 0; i < visible_files; i++) {
        int r = 3 + i;
        int f_idx = i + panel->scroll_offset;

        if (f_idx >= panel->file_count) {
            scr_fill(r, start_col + 1, width - 2, ' ', SC_STYLE_TEXT);
            if (sep1 < start_col + width - 1) scr_str(r, sep1, "│", SC_STYLE_BORDER);
            if (sep2 < start_col + width - 1) scr_str(r, sep2, "│", SC_STYLE_BORDER);
            continue;
        }

        int style = SC_STYLE_TEXT;
        if (is_active && f_idx == panel->cursor) {
            style = SC_STYLE_HIGHLIGHT;
        } else if (panel->files[f_idx].is_dir) {
            style = SC_STYLE_DIR;
        }

        char size_str[16];
        if (strcmp(panel->files[f_idx].name, "..") == 0) {
            snprintf(size_str, sizeof(size_str), "Up");
        } else if (panel->files[f_idx].is_link) {
            snprintf(size_str, sizeof(size_str), "<link>");
        } else if (panel->files[f_idx].is_dir) {
            snprintf(size_str, sizeof(size_str), "<dir>");
        } else {
            snprintf(size_str, sizeof(size_str), "%u", (unsigned)panel->files[f_idx].size);
        }

        uint32_t t = panel->files[f_idx].mtime;
        char dt_str[20];
        snprintf(dt_str, sizeof(dt_str), "%02u-%02u-%02u|%02u:%02u",
                 (unsigned)((t / 86400) % 12 + 1),
                 (unsigned)((t / 86400) % 28 + 1),
                 (unsigned)((t / 31536000 + 70) % 100),
                 (unsigned)((t % 86400) / 3600),
                 (unsigned)((t % 3600) / 60));

        scr_fill(r, start_col + 1, width - 2, ' ', style);
        scr_text(r, start_col + 1, name_width, panel->files[f_idx].name,
                 strlen(panel->files[f_idx].name), style, 0);

        if (sep1 < start_col + width - 1) {
            scr_str(r, sep1, "│", SC_STYLE_BORDER);
            scr_text(r, sep1 + 1, size_width - 1, size_str, strlen(size_str), style, 0);
        }
        if (sep2 < start_col + width - 1) {
            scr_str(r, sep2, "│", SC_STYLE_BORDER);
            scr_text(r, sep2 + 1, date_width - 1, dt_str, strlen(dt_str), style, 0);
        }
    }

    /* Status row at panel bottom */
    int total_files = 0, total_directories = 0;
    uint32_t total_size = 0;
    for (int i = 0; i < panel->file_count; i++) {
        if (panel->files[i].is_dir) {
            if (strcmp(panel->files[i].name, "..") != 0) total_directories++;
        } else {
            total_files++;
            total_size += panel->files[i].size;
        }
    }

    char stat_buf[64];
    if (total_size < 1024) {
        snprintf(stat_buf, sizeof(stat_buf), " %u B in %d files, %d dirs ",
                 (unsigned)total_size, total_files, total_directories);
    } else if (total_size < 1024 * 1024) {
        snprintf(stat_buf, sizeof(stat_buf), " %u KB in %d files, %d dirs ",
                 (unsigned)(total_size / 1024), total_files, total_directories);
    } else {
        snprintf(stat_buf, sizeof(stat_buf), " %u MB in %d files, %d dirs ",
                 (unsigned)(total_size / (1024 * 1024)), total_files, total_directories);
    }

    int bot_row = 1 + panel_h - 1;
    scr_text(bot_row, start_col + 2, width - 4, stat_buf, strlen(stat_buf), SC_STYLE_TEXT, 0);
}

void draw_menu(void)
{
    scr_fill(0, 0, term_cols, ' ', SC_STYLE_MENU);
    scr_str(0, 0, " SC ", SC_STYLE_HEADER);

    const char *menu_tabs[] = {"Left", "File", "Command", "Options", "Right"};
    int start_col = 12;
    for (int i = 0; i < 5 && start_col < term_cols; i++) {
        scr_str(0, start_col, menu_tabs[i], SC_STYLE_MENU);
        start_col += (int)strlen(menu_tabs[i]) + 3;
    }
}

void draw_command_line(void)
{
    int row = term_rows - 2;
    if (row < 0) return;

    scr_fill(row, 0, term_cols, ' ', SC_STYLE_TEXT);

    char prompt[VFS_MAX_PATH + 4];
    snprintf(prompt, sizeof(prompt), "%s> ", active_panel ? active_panel->path : "/");
    int plen = scr_str(row, 0, prompt, SC_STYLE_HEADER);

    int visible_cols = term_cols - plen - 2;
    if (visible_cols < 1) visible_cols = 1;

    int clen = (int)strlen(command_buffer);
    int start = cmd_display_offset;
    if (start > clen) start = clen;
    int avail = clen - start;
    if (avail > visible_cols) avail = visible_cols;

    scr_text(row, plen, visible_cols, command_buffer + start, (size_t)avail, SC_STYLE_TEXT, 0);

    /* Position cursor */
    int cur_col = plen + (cmd_cursor_pos - start);
    if (cur_col >= plen && cur_col < term_cols) {
        scr_cursor(row, cur_col, 1);
    } else {
        scr_cursor(0, 0, 0);
    }
}

void draw_bottom_bar(void)
{
    int row = term_rows - 1;
    if (row < 0) return;

    scr_fill(row, 0, term_cols, ' ', SC_STYLE_BOTTOM);

    static const struct { const char *num; const char *label; } fkeys[] = {
        {"1", "Help "},
        {"2", "App "},
        {"3", "View "},
        {"4", "Edit "},
        {"5", "Copy "},
        {"6", "Move "},
        {"7", "Mkdir "},
        {"8", "Delete "},
        {"9", "Menu "},
        {"10", "Exit "}
    };

    int c = 0;
    c += scr_str(row, c, "∀ ", SC_STYLE_HEADER);
    for (int i = 0; i < 10 && c < term_cols; i++) {
        c += scr_str(row, c, fkeys[i].num, SC_STYLE_TEXT);
        c += scr_str(row, c, fkeys[i].label, SC_STYLE_BOTTOM);
    }
}

void append_to_history_display(const char *command, const char *output)
{
    (void)command;
    (void)output;
    draw_interface();
}

void draw_interface(void)
{
    draw_menu();

    if (show_command_buffer) {
        int max_disp = term_rows - 3;
        for (int r = 1; r <= max_disp; r++) {
            scr_fill(r, 0, term_cols, ' ', SC_STYLE_TEXT);
        }

        int cur_row = 1;
        int items = history_count < max_disp ? history_count : max_disp;
        int start_idx = (history_count > max_disp) ?
            (history_start - max_disp + MAX_HISTORY) % MAX_HISTORY : 0;

        for (int i = 0; i < items && cur_row <= max_disp; i++) {
            int idx = (start_idx + i) % MAX_HISTORY;
            char cmd_hdr[516];
            snprintf(cmd_hdr, sizeof(cmd_hdr), "> %s", history[idx].command);
            scr_text(cur_row++, 0, term_cols, cmd_hdr, strlen(cmd_hdr), SC_STYLE_HEADER, 0);

            const char *p = history[idx].output;
            while (*p != '\0' && cur_row <= max_disp) {
                const char *nl = strchr(p, '\n');
                size_t l = nl ? (size_t)(nl - p) : strlen(p);
                scr_text(cur_row++, 2, term_cols - 2, p, l, SC_STYLE_TEXT, 0);
                p = nl ? nl + 1 : p + l;
            }
        }
    } else {
        int panel_width = (term_cols - 1) / 2;
        draw_panel(&left_panel, 0, panel_width, active_panel == &left_panel);

        for (int r = 1; r < term_rows - 2; r++) {
            scr_str(r, panel_width, "│", SC_STYLE_BORDER);
        }

        draw_panel(&right_panel, panel_width + 1, term_cols - panel_width - 1, active_panel == &right_panel);
    }

    draw_command_line();
    draw_bottom_bar();
    scr_flush();
}

static void draw_submenu(const char *items[], int item_count, int start_row, int start_col, int selected)
{
    int width = 30;
    int height = item_count + 2;
    if (start_col + width > term_cols) start_col = term_cols - width;
    if (start_col < 0) start_col = 0;

    draw_panel_border(start_col, start_row, width, height, SC_STYLE_MENU);

    for (int i = 0; i < item_count; i++) {
        int r = start_row + 1 + i;
        int style = (i == selected) ? SC_STYLE_MENU_SEL : SC_STYLE_MENU;
        scr_fill(r, start_col + 1, width - 2, ' ', style);
        scr_text(r, start_col + 2, width - 4, items[i], strlen(items[i]), style, 0);
    }
}

int handle_menu(void)
{
    const char *menu_tabs[] = {"Left", "File", "Command", "Options", "Right"};
    int tab_count = 5;
    int selected_tab = 0;
    int submenu_active = 0;
    int selected_item = 0;

    const char *left_items[] = {
        "Listing format...",
        "Sort by name",
        "Sort by size",
        "Sort by date",
        "Reload         C-r"
    };
    const char *file_items[] = {
        "User Manual        F1",
        "Applications       F2",
        "View               F3",
        "Edit               F4",
        "Copy               F5",
        "Rename/Move        F6",
        "Mkdir              F7",
        "Delete             F8",
        "Exit               F10"
    };
    const char *command_items[] = {
        "Command history   C-o",
        "Swap panels       Tab",
        "Clear history"
    };
    const char *options_items[] = {
        "Volume statistics",
        "Mounted volumes",
        "About BTRON3 SC"
    };
    const char *right_items[] = {
        "Listing format...",
        "Sort by name",
        "Sort by size",
        "Sort by date",
        "Reload         C-r"
    };

    int item_counts[] = {5, 9, 3, 3, 5};
    const char **submenus[] = {left_items, file_items, command_items, options_items, right_items};

    draw_interface();

    while (1) {
        draw_menu();

        /* Highlight tab */
        int tab_col = 12;
        for (int i = 0; i < selected_tab; i++) {
            tab_col += (int)strlen(menu_tabs[i]) + 3;
        }
        scr_str(0, tab_col, menu_tabs[selected_tab], SC_STYLE_MENU_SEL);

        if (submenu_active) {
            draw_submenu(submenus[selected_tab], item_counts[selected_tab], 1, tab_col, selected_item);
        }

        scr_flush();

        int c = term_key();
        if (submenu_active) {
            if (c == KEY_UP && selected_item > 0) {
                selected_item--;
            } else if (c == KEY_DOWN && selected_item < item_counts[selected_tab] - 1) {
                selected_item++;
            } else if (c == KEY_LEFT) {
                if (selected_tab > 0) {
                    selected_tab--;
                    selected_item = 0;
                    draw_interface();
                }
            } else if (c == KEY_RIGHT) {
                if (selected_tab < tab_count - 1) {
                    selected_tab++;
                    selected_item = 0;
                    draw_interface();
                }
            } else if (c == KEY_ESC) {
                submenu_active = 0;
                draw_interface();
            } else if (c == KEY_ENTER) {
                if (selected_tab == 0) { /* Left panel */
                    if (selected_item >= 1 && selected_item <= 3) {
                        left_panel.sort_type = selected_item - 1;
                        load_files(&left_panel);
                    } else if (selected_item == 4) {
                        load_files(&left_panel);
                    }
                } else if (selected_tab == 1) { /* File */
                    if (selected_item == 2) { /* View F3 */
                        if (active_panel->file_count > 0 && !active_panel->files[active_panel->cursor].is_dir) {
                            char fpath[VFS_MAX_PATH];
                            snprintf(fpath, sizeof(fpath), "%s/%s", active_panel->path, active_panel->files[active_panel->cursor].name);
                            vfs_normalize_path(fpath, fpath, sizeof(fpath));
                            (void)sc_launch_tv(fpath, 1);
                            return 0;
                        }
                    } else if (selected_item == 3) { /* Edit F4 */
                        if (active_panel->file_count > 0 && !active_panel->files[active_panel->cursor].is_dir) {
                            char fpath[VFS_MAX_PATH];
                            snprintf(fpath, sizeof(fpath), "%s/%s", active_panel->path, active_panel->files[active_panel->cursor].name);
                            vfs_normalize_path(fpath, fpath, sizeof(fpath));
                            (void)sc_launch_tv(fpath, 0);
                            return 0;
                        }
                    } else if (selected_item == 8) { /* Exit F10 */
                        if (handle_exit_dialog()) {
                            return 1;
                        }
                    }
                } else if (selected_tab == 2) { /* Command */
                    if (selected_item == 0) {
                        show_command_buffer ^= 1;
                    } else if (selected_item == 1) {
                        active_panel = (active_panel == &left_panel) ? &right_panel : &left_panel;
                    } else if (selected_item == 2) {
                        history_count = 0;
                        history_start = 0;
                    }
                } else if (selected_tab == 4) { /* Right panel */
                    if (selected_item >= 1 && selected_item <= 3) {
                        right_panel.sort_type = selected_item - 1;
                        load_files(&right_panel);
                    } else if (selected_item == 4) {
                        load_files(&right_panel);
                    }
                }
                submenu_active = 0;
                draw_interface();
                break;
            }
        } else {
            if (c == KEY_LEFT && selected_tab > 0) {
                selected_tab--;
            } else if (c == KEY_RIGHT && selected_tab < tab_count - 1) {
                selected_tab++;
            } else if (c == KEY_ENTER || c == KEY_DOWN) {
                submenu_active = 1;
                selected_item = 0;
            } else if (c == KEY_ESC) {
                draw_interface();
                break;
            }
        }
    }
    return 0;
}

void draw_exit_dialog(int selected)
{
    int dwidth = 46;
    int dheight = 6;
    int srow = (term_rows - dheight) / 2;
    int scol = (term_cols - dwidth) / 2;
    if (srow < 0) srow = 0;
    if (scol < 0) scol = 0;

    for (int r = 0; r < dheight; r++) {
        scr_fill(srow + r, scol, dwidth, ' ', SC_STYLE_PINK);
    }
    draw_panel_border(scol, srow, dwidth, dheight, SC_STYLE_PINK);

    const char *msg = "Do you want to exit Sokhatsky Commander?";
    int mcol = scol + (dwidth - (int)strlen(msg)) / 2;
    scr_str(srow + 2, mcol, msg, SC_STYLE_WHITE);

    int brow = srow + 4;
    int yes_col = scol + (dwidth / 2) - 8;
    int no_col = scol + (dwidth / 2) + 2;

    scr_str(brow, yes_col, "[ Yes ]", (selected == 0) ? SC_STYLE_BUTTON_HL : SC_STYLE_WHITE);
    scr_str(brow, no_col, "[  No ]", (selected == 1) ? SC_STYLE_BUTTON_HL : SC_STYLE_WHITE);

    scr_flush();
}

int handle_exit_dialog(void)
{
    int selected = 1;
    draw_exit_dialog(selected);

    while (1) {
        int c = term_key();
        if (c == 'y' || c == 'Y') {
            return 1;
        } else if (c == 'n' || c == 'N' || c == KEY_ESC) {
            return 0;
        } else if (c == KEY_TAB || c == KEY_LEFT || c == KEY_RIGHT) {
            selected ^= 1;
            draw_exit_dialog(selected);
        } else if (c == KEY_ENTER) {
            return (selected == 0) ? 1 : 0;
        }
    }
}
