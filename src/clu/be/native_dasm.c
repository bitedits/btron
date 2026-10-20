/* Bounded adapter from the legacy BE decoders to the native CLU editor. */
#include "native.h"
#include "editor.h"
#include "arch/x86/disasm.h"
#include "arch/x86/nctype.h"
#include "arch/x86/sync.h"
#include "arch/x86/iflag.h"
#include "arch/arm32/armv7.h"
#include "arch/riscv/riscv-disas.h"
#include "arch/ppc/ppc_disasm.h"
#include "arch/m68k/m68k.h"
#include "arch/sh4/sh4.h"
#include "arch/mips/mips.h"
#include "arch/pdp11/pdp11.h"
#include "arch/nv/nv.h"
#include <stdio.h>
#include <string.h>

#define BE_DASM_INPUT 32u
#define BE_DASM_TEXT  256u

static struct editor g_be_legacy_editor;
static int g_be_dasm_ready;
ARMSTATE arm;

struct editor *editor(void)
{
    return &g_be_legacy_editor;
}

int be_native_disasm_init(void)
{
    if (!g_be_dasm_ready) {
        nasm_ctype_init();
        init_sync();
        g_be_dasm_ready = 1;
    }
    return 0;
}

static void be_dasm_set_legacy_context(BeNativeContext *ctx,
                                      const uint8_t *input, size_t length)
{
    memset(&g_be_legacy_editor, 0, sizeof(g_be_legacy_editor));
    g_be_legacy_editor.contents = (char *)input;
    g_be_legacy_editor.content_length = (unsigned int)length;
    g_be_legacy_editor.seg_size = ctx->bitness;
    g_be_legacy_editor.arch = (enum dasm_arch)ctx->arch;
}

static int be_dasm_x86(const uint8_t *input, size_t available, int bitness,
                       size_t offset, char *out, size_t out_size, size_t *length)
{
    iflag_t preferred;
    int32_t decoded;
    size_t count = available < INSN_MAX ? available : INSN_MAX;
    iflag_clear_all(&preferred);
    decoded = disasm((uint8_t *)input, (int32_t)count, out, (int)out_size,
                     bitness, (int64_t)offset, false, &preferred);
    if (decoded <= 0) return -1;
    *length = (size_t)decoded;
    return 0;
}

static int be_dasm_decode_arch(BeNativeContext *ctx, const uint8_t *input,
                               size_t available, size_t offset, char *out,
                               size_t out_size, size_t *length)
{
    int used = 0;
    unsigned long address = (unsigned long)input;
    switch (ctx->arch) {
    case 1:
        return be_dasm_x86(input, available, ctx->bitness, offset, out, out_size, length);
    case 2:
        if (ctx->bitness == 32) decodeARM32(address, out, &used, (unsigned long)offset);
        else return -1;
        break;
    case 3: {
        rv_inst instruction = 0;
        rv_isa isa = ctx->bitness == 32 ? rv32 : ctx->bitness == 128 ? rv128 : rv64;
        inst_fetch((uint8_t *)input, &instruction, &used);
        if (used > 0) disasm_inst(out, out_size, isa, offset, instruction);
        break;
    }
    case 4: decodePPC(address, out, &used, (unsigned long)offset); break;
    case 5: decodeSH4(address, out, &used, (unsigned long)offset); break;
    case 6: decodeM68K(address, out, &used, (unsigned long)offset); break;
    case 7: decodeMIPS(address, out, &used, (unsigned long)offset); break;
    case 8: decodePDP11(address, out, &used, (unsigned long)offset); break;
    /* The bundled Envy engine allocates during each decode. Keep the
     * native editor's JPL-bounded edit loop on a byte fallback for now. */
    case 9: return -1;
    default: return -1;
    }
    if (used <= 0 || (size_t)used > available) return -1;
    *length = (size_t)used;
    out[out_size - 1u] = '\0';
    return 0;
}

int be_native_disasm_decode(BeNativeContext *ctx, size_t offset,
                            char *out, size_t out_size, size_t *length)
{
    union {
        uint64_t align;
        uint8_t bytes[BE_DASM_INPUT];
    } input;
    size_t available;
    size_t copy_count;
    if (ctx == NULL || ctx->bytes == NULL || out == NULL || out_size < 2u ||
        length == NULL || offset >= ctx->length) return -1;
    if (be_native_disasm_init() != 0) return -1;
    available = ctx->length - offset;
    copy_count = available < sizeof(input.bytes) ? available : sizeof(input.bytes);
    memset(input.bytes, 0, sizeof(input.bytes));
    memcpy(input.bytes, &ctx->bytes[offset], copy_count);
    be_dasm_set_legacy_context(ctx, input.bytes, available);
    out[0] = '\0';
    *length = 0u;
    if (be_dasm_decode_arch(ctx, input.bytes, available, offset,
                            out, out_size, length) == 0) return 0;
    (void)snprintf(out, out_size, ".byte 0x%02X", input.bytes[0]);
    *length = 1u;
    return 0;
}
