// BE: INFOSEC BINARY HEX EDITOR WITH DASM
// Synrc Research (c) 2022-2023
// 5HT DHARMA License

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

#include "../term/buffer.h"
#include "../term/terminal.h"
#include "../hex/hex.h"
#include "../dasm/dasm.h"
#include "../editor.h"

#include "../arch/x86/disasm.h"
#include "../arch/arm64/armadillo.h"
#include "../arch/arm32/armv7.h"
#include "../arch/riscv/riscv-disas.h"
#include "../arch/ppc/ppc_disasm.h"
#include "../arch/mips/mips.h"
#include "../arch/m68k/m68k.h"
#include "../arch/sh4/sh4.h"
#include "../arch/pdp11/pdp11.h"
#include "../arch/nv/nv.h"

#define LINES 140
#define DUMP  32
#define ADDR  16
#define CODE  256
#define ADDRWIN 16
#define DUMPWIN 32
#define CODEWIN 64

int  dump_win = DUMPWIN;
int  addr[LINES][ADDR];
int  codelen[LINES];
int  dumplen[LINES];
unsigned char dump[LINES][DUMP*20];
unsigned char code[LINES][CODE];

void nasm_init(struct editor* e) { (void)e; nasm_ctype_init();  init_sync(); }

void setup_instruction(int i, struct editor* e, struct charbuf* b,
    uint64_t offset, uint8_t *data, int datalen, char *insn)
{
    (void)e; (void)b; (void)offset;
    dumplen[i] = datalen;
    memcpy(&dump[i],data,dumplen[i]);
    codelen[i] = strlen(insn) + 1;
    memcpy(&code[i],insn,codelen[i]);
}

void draw_instruction(int i, struct editor* e, struct charbuf* b,
    uint64_t offset, uint8_t *data, int datalen, char *insn)
{
    (void)data; (void)datalen; (void)insn;

    if (i + 1 == e->cursor_y) charbuf_appendf(b, "\x1b[1;97m\x1b[45m");
    else charbuf_appendf(b, "\x1b[0;93m\x1b[0;104m");
    charbuf_appendf(b, "%016x\x1b[0m ", offset);

    for (int j = 0; j < dumplen[i] && j < dump_win; j++)
        if (e->cursor_y - 1 == i && e->cursor_x - 1 == j)
        {
            charbuf_appendf(b, "\x1b[1;37m\x1b[43m");
            memset(hexstr()+1, '\0', 1);
            /* hex.c:91-111 renders its cursor cell from the snprintf'ed hex[]
             * buffer, which is what turns a half-typed octet into a live
             * preview.  Here the same snprintf wrote to a hex[] that nothing
             * read, and the cell below is what actually reached the screen,
             * so the dead statement is removed rather than "fixed" into a
             * behaviour change.  See the note in the task log. */
            charbuf_appendf(b, "%02X", (unsigned char)dump[i][j]);
            charbuf_appendf(b, "\x1b[4;94m\x1b[49m");
        } else {
            charbuf_appendf(b, "\x1b[4;94m\x1b[49m");
            charbuf_appendf(b, "%02X", dump[i][j]);
        }

    charbuf_appendf(b, "\x1b[0m ");
    charbuf_appendf(b, "\x1b[0;93m\x1b[0;104m");
    for (int j = 0; j < dump_win - 2 * dumplen[i]; j++) charbuf_appendf(b, " ");

    charbuf_appendf(b, "\x1b[0m ");
    if (i + 1 == e->cursor_y) charbuf_appendf(b, "\x1b[1;97m\x1b[45m");
    else charbuf_appendf(b, "\x1b[0;93m\x1b[0;104m");
    charbuf_appendf(b, "%s", (char *)&code[i]);
    /* The bound was size_t: 62 - strlen() of a 63-character instruction
     * wrapped to 18446744073709551551 and the padding loop never ended. */
    for (int j = 0; j < 62 - (int)strlen((char *)code[i]); j++) charbuf_appendf(b, " ");
    charbuf_appendf(b, "\r\n");

}

int offset_at_cursor_dasm(struct editor* e) {
    int offset = e->offset_dasm;
    for (int i = 0; i < e->cursor_y - 1; i++) offset += dumplen[i];
    offset += e->cursor_x - 1;
    if (offset <= 0) return 0;
    /* content_length is unsigned; compared as-is, a negative offset would have
     * converted to a huge unsigned value and clamped instead of returning it. */
    if ((unsigned int)offset >= e->content_length) return (int)e->content_length - 1;
    return offset;
}

char *decodeARM(unsigned long int start, char *outbuf, int *lendis, unsigned long int offset) {
    struct editor *e = editor();
   /* Both branches return the callee's buffer; falling off the end of a
    * non-void function handed the caller whatever $v0 happened to hold. */
   if (e->seg_size < 64) return decodeARM32 (start, outbuf, lendis, offset);
   else return decodeARM64 (start, outbuf, lendis, offset);
}

char *decode(unsigned long int start, char *outbuf, int *lendis, unsigned long int offset)
{
    struct editor *e = editor();
    switch (e->arch) {
         case ARCH_INTEL: decodeEM64T (start, outbuf, lendis, offset); break;
         case ARCH_ARM:   decodeARM   (start, outbuf, lendis, offset); break;
         case ARCH_PPC:   decodePPC   (start, outbuf, lendis, offset); break;
         case ARCH_RISCV: decodeRISCV (start, outbuf, lendis, offset); break;
         case ARCH_SH4:   decodeSH4   (start, outbuf, lendis, offset); break;
         case ARCH_M68K:  decodeM68K  (start, outbuf, lendis, offset); break;
         case ARCH_MIPS:  decodeMIPS  (start, outbuf, lendis, offset); break;
         case ARCH_PDP11: decodePDP11 (start, outbuf, lendis, offset); break;
         case ARCH_NVIDIA:   decodeNV (start, outbuf, lendis, offset); break;
         default: break;
     }
     return outbuf;
}

void disassemble_screen(struct editor* e, struct charbuf* b)
{
    int lendis=0;
    unsigned long int offset=0;
    char outbuf[2048], *q;
    offset = e->offset_dasm;
    q = &e->contents[offset];
    for (int i = 0; i < e->screen_rows - 2; i++) if (offset < e->content_length)
    {
        decode((unsigned long)q, outbuf, &lendis, offset);
        setup_instruction(i, e, b, offset, (uint8_t *) q, lendis, outbuf);
        q += lendis;
        offset += lendis;
    }
}

void editor_render_dasm(struct editor* e, struct charbuf* b)
{
    disassemble_screen(e, b);
    int offset = e->offset_dasm;
    for (int i = 0; i < e->screen_rows - 2; i++) {
        draw_instruction(i, e, b, offset, (uint8_t *)&dump[i][0], dumplen[i], (char *)&code[i][0]);
        offset += dumplen[i];
    }
}

void editor_move_cursor_dasm(struct editor* e, int dir, int amount)
{
    switch (dir) {
        case KEY_UP:    e->cursor_y-=amount; break; case KEY_DOWN:  e->cursor_y+=amount; break;
        case KEY_LEFT:  e->cursor_x-=amount; break; case KEY_RIGHT: e->cursor_x+=amount; break;
    }

    if (e->cursor_y <= 1) e->cursor_y = 1;
    if (e->cursor_y > e->screen_rows - 2) e->cursor_y = e->screen_rows - 2;
    if (e->cursor_x <= 1 && e->cursor_y <= 1 && e->line <= 0) { e->cursor_x = e->cursor_y = 1; return; }
    if (e->cursor_x < 1) {
        if (dir == KEY_LEFT) e->cursor_y--; else e->cursor_y++;
        if (e->cursor_y >= 1) e->cursor_x = dumplen[e->cursor_y - 1];
    } else if (e->cursor_x > dumplen[e->cursor_y - 1] && (dir == KEY_RIGHT || dir == KEY_LEFT)) {
        e->cursor_y++;
        e->cursor_x = 1;
    } else if (e->cursor_x > dumplen[e->cursor_y - 1] && (dir == KEY_DOWN || dir == KEY_UP)) {
        e->cursor_x = dumplen[e->cursor_y - 1];
    }
    if (e->cursor_y <= 1) e->cursor_y = 1;
    if (e->cursor_x <= 1) e->cursor_x = 1;
    if (e->cursor_y > e->screen_rows - 2) e->cursor_y = e->screen_rows - 2;
}

void editor_replace_byte_dasm(struct editor* e, char x) {
    hexstr_set(0,0); hexstr_set(1,0); hexstr_set(2,0);
    hexstr_idx_set(0);
    unsigned int offset = offset_at_cursor_dasm(e);
    e->contents[offset] = x;
    editor_refresh_screen(e);
    editor_move_cursor(e, KEY_RIGHT, 1);
    editor_statusmessage(e, STATUS_INFO, "Replaced byte at offset %09x with %02x", offset, (unsigned char) x);
    e->dirty = true;
}

/* The DASM view has no insert and no independent scroll: the callbacks exist
 * to complete the editor vtable, so their parameters are named and unused. */
void editor_insert_byte_dasm(struct editor* e, char x, bool after) {
    (void)e; (void)x; (void)after;
}

void editor_scroll_dasm(struct editor* e, int units) {
    (void)e; (void)units;
}
