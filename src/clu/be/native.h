#ifndef BTRON_BE_NATIVE_H
#define BTRON_BE_NATIVE_H

#include "../term.h"
#include <stddef.h>
#include <stdint.h>

#if defined(BTRON_PS2_TARGET)
#define BE_NATIVE_MAX_BYTES 16384u
#else
#define BE_NATIVE_MAX_BYTES 65536u
#endif
#define BE_NATIVE_PATH_MAX  256u

typedef struct {
    uint8_t *bytes;
    char path[BE_NATIVE_PATH_MAX];
    char status[96];
    size_t length;
    size_t cursor;
    size_t top;
    int nibble;
    int insert_mode;
    int octets;
    int bitness;
    int arch;
    int view_asm;
    int dirty;
    int menu_open;
    int menu_focus;
    int menu_arch;
    int menu_bitness;
    int embedded;
    int exit_confirm;
    int rows;
    int cols;
    size_t asm_offsets[TERM_MAXR];
    uint8_t asm_lengths[TERM_MAXR];
    int asm_line_count;
    int asm_cursor_row;
} BeNativeContext;

/* Return 0 when a session starts, negative on error, 1 for help, 2 for version. */
int be_native_session_init(BeNativeContext *ctx, const char *args, int rows, int cols);
int be_native_session_step(BeNativeContext *ctx, int key);
void be_native_session_close(BeNativeContext *ctx);
void be_native_session_resize(BeNativeContext *ctx, int rows, int cols);
void be_native_command_info(const char *args, char *out, size_t out_size);
int be_native_disasm_init(void);
int be_native_disasm_decode(BeNativeContext *ctx, size_t offset,
                            char *out, size_t out_size, size_t *length);

#endif
