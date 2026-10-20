/* B-System Binary Editor session. Fixed storage, CLU terminal and VFS only. */
#include "native.h"
#include "../vfs.h"
#include <btron/fs/vol_api.h>
#include <stdio.h>
#include <string.h>

enum { BE_STYLE_DEFAULT = 0, BE_STYLE_HEADER = 20, BE_STYLE_TEXT = 21,
       BE_STYLE_SELECT = 22, BE_STYLE_BYTE = 23, BE_STYLE_BYTE_CURSOR = 24,
       BE_STYLE_FOOTER = 25, BE_STYLE_ERROR = 26, BE_STYLE_MENU = 27,
       BE_STYLE_MENU_ITEM = 28, BE_STYLE_MENU_SELECT = 29,
       BE_STYLE_MENU_FOCUS = 30, BE_STYLE_LOGO = 31,
       BE_STYLE_LOGO_ICON = 26, BE_STYLE_ARCH_MENU = 32,
       BE_STYLE_ARCH_TITLE = 33, BE_STYLE_FIND = 34, BE_STYLE_FIND_TITLE = 35,
       BE_STYLE_FIND_FIELD = 36, BE_STYLE_RULE = 37 };
#if defined(BTRON_PS2_TARGET)
#define BE_NATIVE_CONTEXTS 1
#else
#define BE_NATIVE_CONTEXTS 2
#endif

static uint8_t g_be_bytes[BE_NATIVE_CONTEXTS][BE_NATIVE_MAX_BYTES];
static uint8_t g_be_read_probe[BE_NATIVE_MAX_BYTES + 1u];
static uint8_t g_be_context_used[BE_NATIVE_CONTEXTS];
static const char *g_be_arch_names[10] = {
    "Unknown", "EM64T", "AArch32/64", "RISC-V", "PowerPC",
    "SuperH-4", "M68000", "MIPS", "PDP-11", "nVidia"
};
static const int g_be_bitness_values[5] = { 8, 16, 32, 64, 128 };

static void be_setup_styles(void)
{
    term_style(BE_STYLE_DEFAULT, "0");
    term_style(BE_STYLE_HEADER, "90;106");
    term_style(BE_STYLE_TEXT, "0;93;104");
    term_style(BE_STYLE_SELECT, "1;97;45");
    /* Bright-blue foreground and background are identical in GTerm; use
       bright white text so highlighted bytes remain readable. */
    term_style(BE_STYLE_BYTE, "4;97;104");
    term_style(BE_STYLE_BYTE_CURSOR, "1;37;43");
    term_style(BE_STYLE_FOOTER, "37;44");
    term_style(BE_STYLE_ERROR, "1;37;41");
    term_style(BE_STYLE_MENU, "37;44");
    term_style(BE_STYLE_MENU_ITEM, "37;44");
    term_style(BE_STYLE_MENU_SELECT, "30;47");
    term_style(BE_STYLE_MENU_FOCUS, "1;37;44");
    term_style(BE_STYLE_ARCH_MENU, "30;43");
    term_style(BE_STYLE_ARCH_TITLE, "1;30;43");
    term_style(BE_STYLE_FIND, "30;43");           /* yellow, like the F2 menu */
    term_style(BE_STYLE_FIND_TITLE, "1;30;43");
    term_style(BE_STYLE_FIND_FIELD, "30;47");
    term_style(BE_STYLE_RULE, "97;104");
    term_style(BE_STYLE_LOGO, "48;2;255;105;180;1;37");
    term_style(BE_STYLE_LOGO_ICON, "1;33;44");
}

static uint8_t *be_buffer_acquire(void)
{
    int i;
    for (i = 0; i < BE_NATIVE_CONTEXTS; i++) {
        if (g_be_context_used[i] == 0u) {
            g_be_context_used[i] = 1u;
            return g_be_bytes[i];
        }
    }
    return NULL;
}

static void be_buffer_release(uint8_t *buffer)
{
    int i;
    for (i = 0; i < BE_NATIVE_CONTEXTS; i++) {
        if (buffer == g_be_bytes[i]) {
            memset(g_be_bytes[i], 0, sizeof(g_be_bytes[i]));
            g_be_context_used[i] = 0u;
            return;
        }
    }
}

typedef struct {
    char filename[BE_NATIVE_PATH_MAX];
    int octets;
    int bitness;
    int arch;
    int view_asm;
    int help;
    int version;
} BeOptions;

static int be_parse_int(const char *s, int min, int max, int *value)
{
    int n = 0;
    size_t i = 0;
    if (s == NULL || s[0] == '\0') return -1;
    while (s[i] != '\0' && i < 4u) {
        if (s[i] < '0' || s[i] > '9') return -1;
        n = n * 10 + (s[i] - '0');
        i++;
    }
    if (s[i] != '\0' || n < min || n > max) return -1;
    *value = n;
    return 0;
}

static int be_next_token(const char **cursor, char *out, size_t cap)
{
    size_t n = 0;
    size_t scanned = 0;
    char quote = '\0';
    const char *p = *cursor;
    for (scanned = 0; scanned < cap && (*p == ' ' || *p == '\t'); scanned++) p++;
    if (scanned == cap) return -1;
    scanned = 0;
    if (*p == '\0') { *cursor = p; return 0; }
    if (*p == '\'' || *p == '"') quote = *p++;
    while (*p != '\0' && ((quote != '\0') ? *p != quote : (*p != ' ' && *p != '\t')) && scanned < cap) {
        if (n + 1u >= cap) return -1;
        out[n++] = *p++;
        scanned++;
    }
    if (scanned == cap) return -1;
    if (quote != '\0') {
        if (*p != quote) return -1;
        p++;
    }
    out[n] = '\0';
    *cursor = p;
    return 1;
}

static int be_parse_options(const char *args, BeOptions *o)
{
    char token[BE_NATIVE_PATH_MAX];
    const char *p = args ? args : "";
    int result;
    unsigned int seen = 0u;
    memset(o, 0, sizeof(*o));
    o->octets = 24;
    o->bitness = 64;
    o->arch = 1;
    {
        int token_count;
        for (token_count = 0; token_count < 16; token_count++) {
            result = be_next_token(&p, token, sizeof(token));
            if (result <= 0) break;
        if (strcmp(token, "-h") == 0) { if (seen & 1u) return -1; seen |= 1u; o->help = 1; continue; }
        if (strcmp(token, "-v") == 0) { if (seen & 2u) return -1; seen |= 2u; o->version = 1; continue; }
        if (strcmp(token, "-d") == 0) { if (seen & 4u) return -1; seen |= 4u; o->view_asm = 1; continue; }
        if (token[0] == '-' && (token[1] == 'b' || token[1] == 'a' || token[1] == 'o') &&
            (token[2] == '\0' || (token[2] >= '0' && token[2] <= '9'))) {
            char opt = token[1];
            char number[8];
            int n;
            unsigned int option_bit = (opt == 'b') ? 8u : (opt == 'a') ? 16u : 32u;
            if (seen & option_bit) return -1;
            seen |= option_bit;
            if (token[2] != '\0') {
                size_t nlen = strlen(&token[2]);
                if (nlen >= sizeof(number)) return -1;
                memcpy(number, &token[2], nlen + 1u);
            } else {
                result = be_next_token(&p, number, sizeof(number));
                if (result != 1) return -1;
            }
            if (opt == 'b') {
                if (be_parse_int(number, 16, 128, &n) != 0 || (n != 16 && n != 32 && n != 64 && n != 128)) return -1;
                o->bitness = n;
            } else if (opt == 'a') {
                if (be_parse_int(number, 1, 9, &n) != 0) return -1;
                o->arch = n;
            } else {
                if (be_parse_int(number, 16, 64, &n) != 0) return -1;
                o->octets = n;
            }
            continue;
        }
        if (token[0] == '-' || o->filename[0] != '\0') return -1;
        memcpy(o->filename, token, strlen(token) + 1u);
        }
        if (token_count == 16 && result > 0) return -1;
    }
    if (result < 0) return -1;
    if (o->help || o->version) return 0;
    return o->filename[0] != '\0' ? 0 : -1;
}

static int be_screen_octets(const BeNativeContext *ctx)
{
    int group = ctx->bitness / 8;
    int n = ctx->octets;
    if (group < 1) group = 1;
    while (n > 1 && 19 + n * 4 + (n - 1) / group > ctx->cols) n--;
    return n;
}

void be_native_command_info(const char *args, char *out, size_t out_size)
{
    BeOptions o;
    int rc;
    if (out == NULL || out_size == 0u) return;
    rc = be_parse_options(args, &o);
    if (rc == 0 && o.version) {
        (void)snprintf(out, out_size, "Binary Editor (B-System native) version 1.0");
    } else if (rc == 0 && o.help) {
        (void)snprintf(out, out_size, "Usage: be [-vhd] [-b bitness] [-a arch] [-o octets] <filename>");
    } else if (rc == 0) {
        (void)snprintf(out, out_size, "be: cannot open file or file exceeds the %u-byte native limit",
                       (unsigned int)BE_NATIVE_MAX_BYTES);
    } else {
        (void)snprintf(out, out_size, "be: expected a file and valid options; use be -h for help");
    }
}

static int be_resolve_path(const char *name, char *path, size_t cap)
{
    char joined[BE_NATIVE_PATH_MAX];
    const char *cwd = g_cwd_path[0] ? g_cwd_path : "/SYS";
    int n;
    if (name[0] == '/') {
        if (strlen(name) >= cap) return -1;
        vfs_normalize_path(name, path, cap);
        return 0;
    }
    n = snprintf(joined, sizeof(joined), "%s/%s", cwd, name);
    if (n < 0 || (size_t)n >= sizeof(joined)) return -1;
    vfs_normalize_path(joined, path, cap);
    return 0;
}

static int be_bitness_index(int bitness)
{
    int i;
    for (i = 0; i < 5; i++) {
        if (g_be_bitness_values[i] == bitness) return i;
    }
    return 3;
}

static void be_set_decoder_status(BeNativeContext *ctx)
{
    if (!ctx->view_asm) {
        ctx->status[0] = '\0';
    } else if (ctx->arch == 9) {
        (void)snprintf(ctx->status, sizeof(ctx->status), "nVidia uses bounded byte fallback");
    } else if (ctx->arch == 2 && ctx->bitness != 32) {
        (void)snprintf(ctx->status, sizeof(ctx->status), "ARM64 requires non-bounded decoder");
    } else {
        ctx->status[0] = '\0';
    }
}

static void be_draw_menu(BeNativeContext *ctx)
{
    char line[TERM_MAXC + 1];
    int width = ctx->cols - 4;
    int height = 16;
    int left, top, row;
    int arch_col, bits_col;
    if (width > 62) width = 62;
    if (width < 12) width = ctx->cols;
    if (height > ctx->rows) height = ctx->rows;
    left = (ctx->cols - width) / 2;
    top = (ctx->rows - height) / 2;
    arch_col = left + 2;
    bits_col = left + (width > 44 ? 34 : width / 2);
    for (row = 0; row < height; row++)
        scr_fill(top + row, left, width, ' ', BE_STYLE_ARCH_MENU);
    scr_str(top, left + 2, "Supported Architectures", BE_STYLE_ARCH_TITLE);
    scr_str(top + 1, arch_col, "Architecture", BE_STYLE_ARCH_MENU);
    scr_str(top + 1, bits_col, "Bitness", BE_STYLE_ARCH_MENU);
    for (row = 0; row < 9 && row + 3 < height; row++) {
        int selected = ctx->menu_arch == row + 1;
        int style = (ctx->menu_focus == 0 && selected) ?
                    BE_STYLE_MENU_SELECT : BE_STYLE_ARCH_MENU;
        (void)snprintf(line, sizeof(line), "%c %d  %-12s",
                       ctx->menu_focus == 0 && selected ? '>' : (selected ? '*' : ' '),
                       row + 1, g_be_arch_names[row + 1]);
        scr_str(top + 2 + row, arch_col, line, style);
    }
    for (row = 0; row < 5 && row + 3 < height; row++) {
        int style = (ctx->menu_focus == 1 && ctx->menu_bitness == row) ?
                    BE_STYLE_MENU_SELECT : BE_STYLE_ARCH_MENU;
        (void)snprintf(line, sizeof(line), "(%c) %d  %3d-bit",
                       ctx->menu_bitness == row ? 'o' : ' ',
                       row + 1, g_be_bitness_values[row]);
        scr_str(top + 2 + row, bits_col, line, style);
    }
    if (height >= 2)
        scr_str(top + height - 1, left + 2, "Arrows/Tab choose | Enter apply | Esc cancel", BE_STYLE_ARCH_MENU);
    scr_box(top, left, width, height, BE_STYLE_ARCH_TITLE);
}

static void be_draw_asm(BeNativeContext *ctx)
{
    char instruction[256];
    char line[TERM_MAXC + 1];
    size_t offset = ctx->top;
    int visible = ctx->rows - 3;
    int divider = ctx->cols > 66 ? 50 : ctx->cols - 16;
    int row;
    ctx->asm_line_count = 0;
    ctx->asm_cursor_row = -1;
    for (row = 0; row < visible && row < TERM_MAXR && offset < ctx->length; row++) {
        size_t length = 0u;
        size_t at;
        size_t used;
        int count;
        int row_style = BE_STYLE_TEXT;
        instruction[0] = '\0';
        if (be_native_disasm_decode(ctx, offset, instruction, sizeof(instruction), &length) != 0 ||
            length == 0u || length > ctx->length - offset) length = 1u;
        ctx->asm_offsets[row] = offset;
        ctx->asm_lengths[row] = (uint8_t)length;
        ctx->asm_line_count++;
        if (ctx->cursor >= offset && ctx->cursor < offset + length) {
            row_style = BE_STYLE_SELECT;
            ctx->asm_cursor_row = row;
        }
        count = snprintf(line, sizeof(line), "%016lX: ", (unsigned long)offset);
        if (count < 0) count = 0;
        used = (size_t)count < sizeof(line) ? (size_t)count : sizeof(line) - 1u;
        for (at = 0u; at < length && at < 15u && used + 2u < (size_t)divider; at++) {
            count = snprintf(&line[used], sizeof(line) - used, "%02X", ctx->bytes[offset + at]);
            if (count < 0 || (size_t)count >= sizeof(line) - used) break;
            used += 2u;
        }
        if (used >= (size_t)divider) used = (size_t)divider - 1u;
        line[used] = '\0';
        scr_str(row + 1, 0, line, row_style);
        for (at = 0u; at < length && at < 15u; at++) {
            char byte_text[3];
            int byte_style = (ctx->cursor == offset + at) ? BE_STYLE_BYTE_CURSOR : BE_STYLE_BYTE;
            int byte_col = 18 + (int)at * 2;
            if (byte_col + 2 < divider) {
                (void)snprintf(byte_text, sizeof(byte_text), "%02X", ctx->bytes[offset + at]);
                scr_str(row + 1, byte_col, byte_text, byte_style);
            }
        }
        scr_str(row + 1, divider, "│", BE_STYLE_RULE);
        scr_str(row + 1, divider + 1, instruction, row_style);
        offset += ctx->asm_lengths[row];
    }
}

static void be_draw_find(BeNativeContext *ctx);

static void be_draw(BeNativeContext *ctx)
{
    static const char *const labels[10] = {
        "F1 Help", "F2 Arch", "F3 Quit", "F4 Ins/Rep", "F5 Save",
        "F6 Edit/View", "F7 Find", "F8 Empty", "F9 Empty", "F10 Quit"
    };
    char line[BE_NATIVE_PATH_MAX];
    int r, visible, per_line, start_col;
    int cursor_row, cursor_col;
    unsigned int i;
    const char *arch_name;
    if (ctx->rows < 3 || ctx->cols < 16) return;
    arch_name = ctx->arch >= 1 && ctx->arch <= 9 ? g_be_arch_names[ctx->arch] : "Unknown";
    if (ctx->arch == 2) arch_name = ctx->bitness > 32 ? "AArch64" : "AArch32";
    scr_clear(BE_STYLE_TEXT);
    scr_fill(0, 0, ctx->cols, ' ', BE_STYLE_HEADER);
    (void)snprintf(line, sizeof(line), "%12s [%03i][%s][%s][%02x][%016lx] Size: %012luB",
                   arch_name,
                   ctx->bitness, ctx->view_asm ? "ASM" : "HEX", ctx->view_only ? "VIEW" : "EDIT",
                   ctx->cursor < ctx->length ? ctx->bytes[ctx->cursor] : 0u,
                   (unsigned long)ctx->cursor, (unsigned long)ctx->length);
    scr_str(0, 0, "▄", BE_STYLE_LOGO_ICON);
    scr_str(0, 1, " BE ", BE_STYLE_LOGO);
    scr_str(0, 5, line, BE_STYLE_HEADER);
    (void)snprintf(line, sizeof(line), "%3lu%%",
                   ctx->length ? (unsigned long)((ctx->cursor + 1u) * 100u / ctx->length) : 0u);
    scr_str(0, ctx->cols - 5, line, BE_STYLE_HEADER);
    if (ctx->view_asm) {
        be_draw_asm(ctx);
    } else {
        per_line = be_screen_octets(ctx);
        visible = ctx->rows - 3;
        start_col = 18;
        for (r = 0; r < visible; r++) {
            size_t base = ctx->top + (size_t)r * (size_t)per_line;
            int group = ctx->bitness / 8;
            int ascii_col;
            if (group < 1) group = 1;
            ascii_col = start_col + per_line * 3 + (per_line - 1) / group + 1;
            (void)snprintf(line, sizeof(line), "%016lX:", (unsigned long)base);
            scr_str(r + 1, 0, line, BE_STYLE_TEXT);
            for (i = 0; i < (unsigned int)per_line; i++) {
                size_t at = base + i;
                int col = start_col + (int)i * 3 + (int)i / group;
                int style = at == ctx->cursor ? BE_STYLE_BYTE_CURSOR :
                            (at >= ctx->cursor && at - ctx->cursor < ctx->match_len) ? BE_STYLE_SELECT :
                            BE_STYLE_BYTE;
                char ascii_char[2] = { ' ', '\0' };
                if (at < ctx->length) {
                    (void)snprintf(line, sizeof(line), "%02X", ctx->bytes[at]);
                    scr_str(r + 1, col, line, style);
                    ascii_char[0] = (ctx->bytes[at] >= 0x20u && ctx->bytes[at] <= 0x7eu) ?
                                     (char)ctx->bytes[at] : '.';
                } else {
                    scr_str(r + 1, col, "  ", BE_STYLE_TEXT);
                }
                scr_str(r + 1, ascii_col + (int)i, ascii_char, style);
            }
        }
    }
    scr_fill(ctx->rows - 2, 0, ctx->cols, ' ', BE_STYLE_FOOTER);
    (void)snprintf(line, sizeof(line), " %s | %02X @ %08X | Size %lu B | %lu%% | F6 edit/view D disasm A asm F2 arch F5 save",
                   ctx->status[0] ? ctx->status : (ctx->view_asm ? "ASM" : "HEX"),
                   ctx->cursor < ctx->length ? ctx->bytes[ctx->cursor] : 0u,
                   (unsigned int)ctx->cursor, (unsigned long)ctx->length,
                   ctx->length ? (unsigned long)((ctx->cursor + 1u) * 100u / ctx->length) : 0u);
    scr_str(ctx->rows - 2, 0, line, BE_STYLE_FOOTER);
    term_draw_function_bar(ctx->rows - 1, labels);
    per_line = be_screen_octets(ctx);
    cursor_row = ctx->view_asm ? ctx->asm_cursor_row + 1 : (int)((ctx->cursor - ctx->top) / (size_t)per_line) + 1;
    if (cursor_row > ctx->rows - 3) cursor_row = ctx->rows - 3;
    if (cursor_row < 1) cursor_row = 1;
    cursor_col = ctx->view_asm ?
                 (ctx->asm_cursor_row >= 0 ? 18 + (int)(ctx->cursor - ctx->asm_offsets[ctx->asm_cursor_row]) * 2 : 0) :
                 start_col + (int)(ctx->cursor % (size_t)per_line) * 3 +
                 (int)(ctx->cursor % (size_t)per_line) / (ctx->bitness / 8 > 0 ? ctx->bitness / 8 : 1);
    scr_cursor(cursor_row, cursor_col, 1);
    if (ctx->menu_open) {
        be_draw_menu(ctx);
        scr_cursor(0, 0, 0);
    } else if (ctx->find_open) {
        be_draw_find(ctx);
    } else if (ctx->exit_confirm) {
        int modal_width = ctx->cols < 46 ? ctx->cols : 46;
        int modal_height = 6;
        int modal_left = (ctx->cols - modal_width) / 2;
        int modal_top = (ctx->rows - modal_height) / 2;
        if (modal_top < 0) modal_top = 0;
        for (int r = 0; r < modal_height; r++)
            scr_fill(modal_top + r, modal_left, modal_width, ' ', BE_STYLE_MENU);
        scr_box(modal_top, modal_left, modal_width, modal_height, BE_STYLE_MENU_FOCUS);
        scr_str(modal_top + 2, modal_left + 4, "File modified. Save before exiting?", BE_STYLE_MENU_FOCUS);
        scr_str(modal_top + 4, modal_left + 3, "[ Save & Exit ]", ctx->exit_sel == 0 ? BE_STYLE_MENU_SELECT : BE_STYLE_MENU_ITEM);
        scr_str(modal_top + 4, modal_left + 20, "[ Discard ]", ctx->exit_sel == 1 ? BE_STYLE_MENU_SELECT : BE_STYLE_MENU_ITEM);
        scr_str(modal_top + 4, modal_left + 33, "[ Cancel ]", ctx->exit_sel == 2 ? BE_STYLE_MENU_SELECT : BE_STYLE_MENU_ITEM);
        scr_cursor(0, 0, 0);
    }
    scr_flush();
}

static void be_reveal_cursor(BeNativeContext *ctx)
{
    if (ctx->view_asm) {
        size_t end = ctx->top;
        if (ctx->asm_line_count > 0) {
            int last = ctx->asm_line_count - 1;
            end = ctx->asm_offsets[last] + ctx->asm_lengths[last];
        }
        if (ctx->cursor < ctx->top || ctx->cursor >= end)
            ctx->top = ctx->cursor;
        return;
    }
    int per_line = be_screen_octets(ctx);
    size_t page;
    page = (size_t)per_line * (size_t)(ctx->rows > 3 ? ctx->rows - 3 : 1);
    if (ctx->cursor < ctx->top) ctx->top = (ctx->cursor / (size_t)per_line) * (size_t)per_line;
    if (ctx->cursor >= ctx->top + page) ctx->top = (ctx->cursor / (size_t)per_line) * (size_t)per_line;
}

/* ── Hex find ──────────────────────────────────────────────────────── */

#define BE_FIND_MAX 32

/*
 * Pattern syntax: hex digits (spaces ignored), '?' = any nibble ("4? 5A ??"),
 * or a leading '"' for literal ASCII text.  Returns pattern length or -1.
 */
static int be_find_parse(const BeNativeContext *ctx, uint8_t *pat, uint8_t *mask)
{
    int n = 0, nib = 0, i;
    if (ctx->find_len > 0 && ctx->find_text[0] == '"') {
        for (i = 1; i < ctx->find_len && n < BE_FIND_MAX; i++) {
            if (ctx->find_text[i] == '"' && i == ctx->find_len - 1) break;
            pat[n] = (uint8_t)ctx->find_text[i];
            mask[n++] = 0xFFu;
        }
        return n;
    }
    for (i = 0; i < ctx->find_len; i++) {
        char c = ctx->find_text[i];
        int v;
        uint8_t m;
        if (c == ' ') continue;
        if (c == '?') { v = 0; m = 0u; }
        else if (c >= '0' && c <= '9') { v = c - '0'; m = 0xFu; }
        else if (c >= 'a' && c <= 'f') { v = c - 'a' + 10; m = 0xFu; }
        else if (c >= 'A' && c <= 'F') { v = c - 'A' + 10; m = 0xFu; }
        else return -1;
        if ((nib & 1) == 0) {
            if (n >= BE_FIND_MAX) return -1;
            pat[n] = (uint8_t)(v << 4);
            mask[n] = (uint8_t)(m << 4);
        } else {
            pat[n] = (uint8_t)(pat[n] | v);
            mask[n] = (uint8_t)(mask[n] | m);
            n++;
        }
        nib++;
    }
    return (nib & 1) ? -1 : n;
}

/* Search forward (dir > 0) or backward from the cursor, wrapping once. */
static int be_find_run(BeNativeContext *ctx, int dir)
{
    uint8_t pat[BE_FIND_MAX], mask[BE_FIND_MAX];
    int n = be_find_parse(ctx, pat, mask);
    size_t span, start, step, at;
    if (n < 0) { (void)snprintf(ctx->find_status, sizeof(ctx->find_status), "Invalid pattern"); return 0; }
    if (n == 0) { ctx->find_status[0] = '\0'; return 0; }
    if (ctx->length < (size_t)n) { (void)snprintf(ctx->find_status, sizeof(ctx->find_status), "Not found"); return 0; }
    span = ctx->length - (size_t)n + 1u;        /* number of candidate offsets */
    start = ctx->cursor < span ? ctx->cursor : span - 1u;
    for (step = 1; step <= span; step++) {
        size_t k;
        int ok = 1;
        at = dir > 0 ? (start + step) % span : (start + span - (step % span)) % span;
        for (k = 0; k < (size_t)n; k++) {
            if (((ctx->bytes[at + k] ^ pat[k]) & mask[k]) != 0u) { ok = 0; break; }
        }
        if (!ok) continue;
        ctx->cursor = at;
        ctx->match_len = (size_t)n;
        ctx->nibble = 0;
        if (ctx->view_asm) ctx->top = at;
        (void)snprintf(ctx->find_status, sizeof(ctx->find_status), "Found at %08lX%s",
                       (unsigned long)at,
                       ((dir > 0 && at <= start) || (dir < 0 && at >= start)) ? "  (wrapped)" : "");
        return 1;
    }
    ctx->match_len = 0;
    (void)snprintf(ctx->find_status, sizeof(ctx->find_status), "Not found");
    return 0;
}

static void be_draw_find(BeNativeContext *ctx)
{
    int dw = ctx->cols < 56 ? ctx->cols : 56;
    int dh = 7;
    int left = (ctx->cols - dw) / 2;
    int top = ctx->rows - 2 - dh;
    int shown = ctx->find_len;
    int r;
    if (top < 1) top = 1;
    if (dw < 20) return;
    for (r = 0; r < dh; r++) scr_fill(top + r, left, dw, ' ', BE_STYLE_FIND);
    scr_box(top, left, dw, dh, BE_STYLE_FIND);
    scr_str(top, left + 2, " Find bytes ", BE_STYLE_FIND_TITLE);
    scr_text(top + 1, left + 2, dw - 4, "Hex: 4D 5A ?? 90   ?=any nibble   \"text\"=ASCII", 44u,
             BE_STYLE_FIND, 0);
    scr_fill(top + 2, left + 2, dw - 4, ' ', BE_STYLE_FIND_FIELD);
    if (shown > dw - 7) shown = dw - 7;
    scr_text(top + 2, left + 3, dw - 6, ctx->find_text + (ctx->find_len - shown),
             (size_t)shown, BE_STYLE_FIND_FIELD, 0);
    scr_str(top + 4, left + 2, ctx->find_status, BE_STYLE_FIND_TITLE);
    scr_text(top + 5, left + 2, dw - 4, "Enter/Down next   Up previous   Esc close", 39u,
             BE_STYLE_FIND, 0);
    scr_cursor(top + 2, left + 3 + shown, 1);
}

static int be_find_step(BeNativeContext *ctx, int key)
{
    if (key == K_ESC || key == K_F7 || key == K_F3 || key == K_F10) {
        ctx->find_open = 0;
    } else if (key == K_ENTER || key == K_DOWN) {
        (void)be_find_run(ctx, 1);
    } else if (key == K_UP) {
        (void)be_find_run(ctx, -1);
    } else if (key == K_BACKSPACE) {
        if (ctx->find_len > 0) ctx->find_text[--ctx->find_len] = '\0';
        ctx->find_status[0] = '\0';
        ctx->match_len = 0;
    } else if (key >= 0x20 && key < 0x7F && ctx->find_len < (int)sizeof(ctx->find_text) - 1) {
        ctx->find_text[ctx->find_len++] = (char)key;
        ctx->find_text[ctx->find_len] = '\0';
        ctx->find_status[0] = '\0';
        ctx->match_len = 0;
    }
    be_reveal_cursor(ctx);
    be_draw(ctx);
    return 1;
}

static int be_save(BeNativeContext *ctx)
{
    if (vfs_write_file(ctx->path, (const char *)ctx->bytes, ctx->length) != 0) {
        (void)snprintf(ctx->status, sizeof(ctx->status), "Save failed");
        return -1;
    }
    ctx->dirty = 0;
    (void)snprintf(ctx->status, sizeof(ctx->status), "Saved");
    return 0;
}

int be_native_session_init(BeNativeContext *ctx, const char *args, int rows, int cols)
{
    BeOptions options;
    size_t size = 0;
    int exists;
    if (ctx == NULL || be_parse_options(args, &options) != 0) return -1;
    if (options.help) return 1;
    if (options.version) return 2;
    if (ctx->bytes != NULL) return -1;
    memset(ctx, 0, sizeof(*ctx));
    ctx->bytes = be_buffer_acquire();
    if (ctx->bytes == NULL) return -1;
    ctx->rows = rows;
    ctx->cols = cols;
    ctx->octets = options.octets;
    ctx->bitness = options.bitness;
    ctx->arch = options.arch;
    ctx->view_asm = options.view_asm;
    ctx->insert_mode = 0;
    if (ctx->rows > TERM_MAXR) ctx->rows = TERM_MAXR;
    if (ctx->cols > TERM_MAXC) ctx->cols = TERM_MAXC;
    if (ctx->rows < 3 || ctx->cols < 16) {
        be_native_session_close(ctx);
        return -1;
    }
    if (be_resolve_path(options.filename, ctx->path, sizeof(ctx->path)) != 0 || vfs_init() < 0) {
        be_native_session_close(ctx);
        return -1;
    }
    exists = vfs_exists(ctx->path);
    if (exists < 0 || (exists && vfs_read_blob(ctx->path, g_be_read_probe,
                                                sizeof(g_be_read_probe), &size) != 0) ||
        size > BE_NATIVE_MAX_BYTES) {
        be_native_session_close(ctx);
        return -1;
    }
    if (size > 0u) memcpy(ctx->bytes, g_be_read_probe, size);
    ctx->length = size;
    be_setup_styles();
    be_set_decoder_status(ctx);
    be_draw(ctx);
    return 0;
}

void be_native_session_resize(BeNativeContext *ctx, int rows, int cols)
{
    if (ctx == NULL) return;
    ctx->rows = rows > TERM_MAXR ? TERM_MAXR : rows;
    ctx->cols = cols > TERM_MAXC ? TERM_MAXC : cols;
    if (ctx->rows < 3 || ctx->cols < 16) return;
    be_reveal_cursor(ctx);
    scr_invalidate();
    be_draw(ctx);
}

static size_t be_asm_previous(BeNativeContext *ctx, size_t target, size_t *length)
{
    char out[256];
    size_t at = 0u, previous = 0u, previous_length = 1u;
    while (at < target) {
        previous = at;
        (void)be_native_disasm_decode(ctx, at, out, sizeof(out), &previous_length);
        if (previous_length == 0u || previous_length > ctx->length - at) previous_length = 1u;
        if (at + previous_length >= target) break;
        at += previous_length;
    }
    *length = previous_length;
    return previous;
}

static size_t be_asm_next(BeNativeContext *ctx, size_t at, size_t *length)
{
    char out[256];
    *length = 1u;
    if (at < ctx->length)
        (void)be_native_disasm_decode(ctx, at, out, sizeof(out), length);
    if (*length == 0u || at >= ctx->length || *length > ctx->length - at) *length = 1u;
    return at + *length;
}

static void be_asm_move(BeNativeContext *ctx, int key)
{
    int row = ctx->asm_cursor_row < 0 ? 0 : ctx->asm_cursor_row;
    size_t current = ctx->asm_offsets[row];
    size_t byte_index = ctx->cursor >= current ? ctx->cursor - current : 0u;
    if (key == K_LEFT && ctx->cursor > 0u) ctx->cursor--;
    else if (key == K_RIGHT && ctx->cursor + 1u < ctx->length) ctx->cursor++;
    else if (key == K_HOME) ctx->cursor = current;
    else if (key == K_END) ctx->cursor = current + ctx->asm_lengths[row] - 1u;
    else if (key == K_UP || key == K_DOWN) {
        int target_row = row + (key == K_UP ? -1 : 1);
        size_t target, target_length;
        if (target_row < 0) {
            target = be_asm_previous(ctx, current, &target_length);
            ctx->top = target;
        } else if (target_row >= ctx->asm_line_count) {
            target = be_asm_next(ctx, current, &target_length);
            if (target >= ctx->length) target = current;
            else ctx->top = target;
        } else {
            target = ctx->asm_offsets[target_row];
            target_length = ctx->asm_lengths[target_row];
        }
        if (target_length == 0u || target_length > ctx->length - target) target_length = 1u;
        if (byte_index >= target_length) byte_index = target_length - 1u;
        ctx->cursor = target + byte_index;
    } else if (key == K_PGUP || key == K_PGDOWN) {
        int steps = ctx->rows > 3 ? ctx->rows - 3 : 1;
        int i;
        size_t target = current, length = 1u;
        for (i = 0; i < steps; i++) {
            size_t next = key == K_PGUP ? be_asm_previous(ctx, target, &length) : be_asm_next(ctx, target, &length);
            if (next == target || next >= ctx->length) break;
            target = next;
        }
        ctx->top = target;
        ctx->cursor = target;
    }
    ctx->nibble = 0;
}

static void be_move_cursor(BeNativeContext *ctx, int key)
{
    size_t amount = (size_t)be_screen_octets(ctx);
    if (ctx->view_asm && ctx->asm_line_count > 0) {
        be_asm_move(ctx, key);
        return;
    }
    if (key == K_LEFT && ctx->cursor > 0u) ctx->cursor--;
    else if (key == K_RIGHT && ctx->cursor < ctx->length) ctx->cursor++;
    else if (key == K_UP && ctx->cursor >= amount) ctx->cursor -= amount;
    else if (key == K_DOWN && ctx->cursor + amount <= ctx->length) ctx->cursor += amount;
    else if (key == K_PGUP || key == K_PGDOWN) {
        amount *= (size_t)(ctx->rows > 3 ? ctx->rows - 3 : 1);
        if (key == K_PGUP) ctx->cursor = ctx->cursor > amount ? ctx->cursor - amount : 0;
        else if (ctx->cursor + amount <= ctx->length) ctx->cursor += amount;
    } else if (key == K_HOME) ctx->cursor = (ctx->cursor / amount) * amount;
    else if (key == K_END) {
        ctx->cursor = (ctx->cursor / amount + 1u) * amount;
        if (ctx->cursor > ctx->length) ctx->cursor = ctx->length;
    }
    ctx->nibble = 0;
}

static void be_edit_key(BeNativeContext *ctx, int key)
{
    int digit = (key >= '0' && key <= '9') ? key - '0' :
                (key >= 'a' && key <= 'f') ? key - 'a' + 10 : key - 'A' + 10;
    if (ctx->nibble == 0) {
        if (ctx->cursor == ctx->length && !ctx->insert_mode) {
            ctx->bytes[ctx->cursor] = 0;
            ctx->length++;
        } else if (ctx->insert_mode) {
            if (ctx->length >= BE_NATIVE_MAX_BYTES) {
                (void)snprintf(ctx->status, sizeof(ctx->status), "File is at native size limit");
                return;
            }
            memmove(&ctx->bytes[ctx->cursor + 1u], &ctx->bytes[ctx->cursor], ctx->length - ctx->cursor);
            ctx->bytes[ctx->cursor] = 0;
            ctx->length++;
        }
        ctx->bytes[ctx->cursor] = (uint8_t)((ctx->bytes[ctx->cursor] & 0x0Fu) | (digit << 4));
        ctx->nibble = 1;
    } else {
        ctx->bytes[ctx->cursor] = (uint8_t)((ctx->bytes[ctx->cursor] & 0xF0u) | digit);
        ctx->nibble = 0;
        ctx->cursor++;
    }
    ctx->dirty = 1;
}

static void be_delete_byte(BeNativeContext *ctx, int backspace)
{
    if (backspace && ctx->cursor > 0u) ctx->cursor--;
    if (ctx->cursor < ctx->length) {
        memmove(&ctx->bytes[ctx->cursor], &ctx->bytes[ctx->cursor + 1u], ctx->length - ctx->cursor - 1u);
        ctx->length--;
        ctx->dirty = 1;
    }
    ctx->nibble = 0;
}

static int be_menu_step(BeNativeContext *ctx, int key)
{
    if (key == K_ESC || key == K_F2) {
        ctx->menu_open = 0;
    } else if (key == K_TAB || key == K_BTAB || key == K_LEFT || key == K_RIGHT) {
        ctx->menu_focus = 1 - ctx->menu_focus;
    } else if (key == K_UP || key == K_DOWN) {
        int delta = key == K_UP ? -1 : 1;
        if (ctx->menu_focus == 0) {
            ctx->menu_arch += delta;
            if (ctx->menu_arch < 1) ctx->menu_arch = 9;
            if (ctx->menu_arch > 9) ctx->menu_arch = 1;
        } else {
            ctx->menu_bitness += delta;
            if (ctx->menu_bitness < 0) ctx->menu_bitness = 4;
            if (ctx->menu_bitness > 4) ctx->menu_bitness = 0;
        }
    } else if (key >= '1' && key <= '9' && ctx->menu_focus == 0) {
        ctx->menu_arch = key - '0';
    } else if (key >= '1' && key <= '5' && ctx->menu_focus == 1) {
        ctx->menu_bitness = key - '1';
    } else if (key == K_ENTER) {
        ctx->arch = ctx->menu_arch;
        ctx->bitness = g_be_bitness_values[ctx->menu_bitness];
        ctx->menu_open = 0;
        if (ctx->view_asm) ctx->top = ctx->cursor;
        (void)snprintf(ctx->status, sizeof(ctx->status), "%s %d-bit", g_be_arch_names[ctx->arch], ctx->bitness);
        be_set_decoder_status(ctx);
        be_reveal_cursor(ctx);
    }
    be_draw(ctx);
    return 1;
}

int be_native_session_step(BeNativeContext *ctx, int key)
{
    if (ctx == NULL) return 0;
    if (ctx->menu_open) return be_menu_step(ctx, key);
    if (ctx->find_open) return be_find_step(ctx, key);
    if (key != K_RESIZE) ctx->match_len = 0;
    if (ctx->exit_confirm) {
        if (key == K_LEFT || key == K_BTAB || key == K_UP) ctx->exit_sel = (ctx->exit_sel + 2) % 3;
        else if (key == K_RIGHT || key == K_TAB || key == K_DOWN) ctx->exit_sel = (ctx->exit_sel + 1) % 3;
        else if (key == 'y' || key == 'Y') ctx->exit_sel = 0;
        else if (key == 'n' || key == 'N') ctx->exit_sel = 1;
        else if (key == K_ESC || key == K_F3 || key == K_F10 || key == K_CTRL('x')) ctx->exit_sel = 2;
        if (key == K_ENTER || key == 'y' || key == 'Y' || key == 'n' || key == 'N' ||
            key == K_ESC || key == K_F3 || key == K_F10 || key == K_CTRL('x')) {
            if (ctx->exit_sel == 0) {
                if (be_save(ctx) == 0) return 0;
            } else if (ctx->exit_sel == 1) {
                return 0;
            } else if (ctx->exit_sel == 2) {
                ctx->exit_confirm = 0;
            }
        }
        be_draw(ctx);
        return 1;
    }
    if (key == K_F6) {
        ctx->view_only = !ctx->view_only;
        (void)snprintf(ctx->status, sizeof(ctx->status), "%s mode", ctx->view_only ? "View" : "Edit");
        be_draw(ctx);
        return 1;
    }
    if (key == K_RESIZE) { be_draw(ctx); return 1; }
    if (key == K_ESC || key == K_F3 || key == K_F10 || key == K_CTRL('x')) {
        if (ctx->dirty) {
            ctx->exit_confirm = 1;
            ctx->exit_sel = 0;
            be_draw(ctx);
            return 1;
        }
        return 0;
    }
    if (key == K_F7 || key == K_CTRL('f')) {
        ctx->find_open = 1;
        ctx->find_status[0] = '\0';
        be_draw(ctx);
        return 1;
    }
    if (key == K_F2) {
        ctx->menu_open = 1;
        ctx->menu_focus = 0;
        ctx->menu_arch = ctx->arch;
        ctx->menu_bitness = be_bitness_index(ctx->bitness);
        be_draw(ctx);
    } else if ((key == K_F5 || key == K_CTRL('s')) && !ctx->view_only) {
        (void)be_save(ctx);
    } else if (key == K_TAB || key == K_F4) {
        ctx->view_asm = !ctx->view_asm;
        if (ctx->view_asm) ctx->top = ctx->cursor;
        be_set_decoder_status(ctx);
    } else if (key == K_LEFT || key == K_RIGHT || key == K_UP || key == K_DOWN ||
               key == K_PGUP || key == K_PGDOWN || key == K_HOME || key == K_END) {
        be_move_cursor(ctx, key);
    } else if (ctx->view_only && (key == 'd' || key == 'D')) {
        ctx->view_asm = 1;
        ctx->top = ctx->cursor;
        ctx->nibble = 0;
        be_set_decoder_status(ctx);
    } else if (ctx->view_only && (key == 'a' || key == 'A')) {
        if (!ctx->view_asm) ctx->top = ctx->cursor;
        ctx->view_asm = 1;
        ctx->nibble = 0;
        be_set_decoder_status(ctx);
    } else if (key == K_INSERT && !ctx->view_only) {
        ctx->insert_mode = !ctx->insert_mode;
        (void)snprintf(ctx->status, sizeof(ctx->status), "%s mode", ctx->insert_mode ? "Insert" : "Overwrite");
    } else if (key == K_BACKSPACE && ctx->cursor > 0u && !ctx->view_only) {
        be_delete_byte(ctx, 1);
    } else if (key == K_DELETE && ctx->cursor < ctx->length && !ctx->view_only) {
        be_delete_byte(ctx, 0);
    } else if (((key >= '0' && key <= '9') || (key >= 'a' && key <= 'f') ||
                (key >= 'A' && key <= 'F')) && ctx->cursor < BE_NATIVE_MAX_BYTES && !ctx->view_only) {
        be_edit_key(ctx, key);
    } else if (key == K_F1) {
        (void)snprintf(ctx->status, sizeof(ctx->status), "F6 edit/view | D disasm at cursor | A asm | F2 arch | F5 save");
    }
    be_reveal_cursor(ctx);
    be_draw(ctx);
    return 1;
}

void be_native_session_close(BeNativeContext *ctx)
{
    if (ctx != NULL) {
        be_buffer_release(ctx->bytes);
        memset(ctx, 0, sizeof(*ctx));
    }
    scr_invalidate();
}
