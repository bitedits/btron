#include <stdint.h>
#include <string.h>

/*
 * sh4dis.h and sh4asm_txt_emit.h are snickerbockers' sh4_asm, imported
 * verbatim (BSD-3, see the header copyright).  Its per-opcode decoders all
 * share one signature that carries the program counter, and most individual
 * decoders never read it:
 * -Wunused-parameter: decoders that ignore the pc their signature hands them.
 * The island is push/pop so it covers the two vendored headers only -- the
 * CLU glue below keeps -Wall -Wextra.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "sh4dis.h"
#include "sh4asm_txt_emit.h"
#pragma GCC diagnostic pop

#define SH4ASM_TXT_LEN 228
static char sh4asm_disas[SH4ASM_TXT_LEN];
unsigned sh4asm_disas_len;
static void clear_asm(void) { sh4asm_disas_len = 0; }
static void neo_asm_emit(char ch) {
    if (sh4asm_disas_len < SH4ASM_TXT_LEN)
        sh4asm_disas[sh4asm_disas_len++] = ch;
}

char * decodeSH4(unsigned long int address, char *outbuf, int *lendis, unsigned long int offset0)
{
     (void)offset0;  /* the SH4 table emits no PC-relative operand yet */
     uint16_t inst16 = (uint16_t)*((unsigned long int *)address);
     memset(sh4_buf, 0, sizeof(sh4_buf));
     clear_asm();
     sh4asm_disas_inst(inst16, neo_asm_emit, 0);
     memcpy(outbuf,sh4asm_disas,sh4asm_disas_len);
     memcpy(outbuf+sh4asm_disas_len,"\0",1);
     *lendis = 2;
     /* decodeSH4 fills the caller's outbuf; without this the return value was
      * the previous call's register content. */
     return outbuf;
}
