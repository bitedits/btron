#!/usr/bin/env bash
# scripts/test_ps2.sh — Cleanroom PlayStation 2 ELF & Boot Verification Suite
#
# Copyright 2026 Synrc Research Center. MIT License.

set -euo pipefail

ELF="btron-ps2.elf"
ISO="btron-ps2.iso"
READELF="mipsel-linux-gnu-readelf -W"
OBJDUMP="mipsel-linux-gnu-objdump"

echo "=========================================================="
echo " Testing B-System PS2 Kernel (ELF & Toolchain Validation) "
echo "=========================================================="

if [ ! -f "$ELF" ]; then
    echo "[ERROR] $ELF not found. Build with 'make ps2' first."
    exit 1
fi

TOTAL=0
PASSED=0

assert_check() {
    local desc="$1"
    local cmd="$2"
    TOTAL=$((TOTAL + 1))
    echo -n "  [TEST $TOTAL] $desc ... "
    if eval "$cmd" >/dev/null 2>&1; then
        echo "[PASS]"
        PASSED=$((PASSED + 1))
    else
        echo "[FAIL]"
        echo "        Failed command: $cmd"
        exit 1
    fi
}

# 1. Check ELF Class
assert_check "ELF is 32-bit Little-Endian (ELF32 LSB)" \
    "$READELF -h $ELF | grep -E 'Class:[[:space:]]+ELF32' && $READELF -h $ELF | grep -E 'Data:[[:space:]]+2.*little endian'"

# 2. Check Machine Architecture
assert_check "ELF Machine architecture is MIPS" \
    "$READELF -h $ELF | grep -E 'Machine:[[:space:]]+MIPS'"

# 3. Check Entry Point
assert_check "ELF Entry Point is 0x00100000" \
    "$READELF -h $ELF | grep -E 'Entry point address:[[:space:]]+0x100000'"

# 4. Check Mandatory Symbols
assert_check "Symbol '_start' exists in symbol table" \
    "$READELF -s $ELF | grep -E '[[:space:]]_start$'"

assert_check "Symbol 'ps2_kernel_main' exists" \
    "$READELF -s $ELF | grep -E '[[:space:]]ps2_kernel_main$'"

assert_check "Symbol 'ps2_gs_init' exists" \
    "$READELF -s $ELF | grep -E '[[:space:]]ps2_gs_init$'"

assert_check "Symbol 'ps2_gs_draw_rect' exists" \
    "$READELF -s $ELF | grep -E '[[:space:]]ps2_gs_draw_rect$'"

assert_check "Symbol 'ps2_sio_putc' exists" \
    "$READELF -s $ELF | grep -E '[[:space:]]ps2_sio_putc$'"

assert_check "Symbol 'ps2_sio_getc' exists" \
    "$READELF -s $ELF | grep -E '[[:space:]]ps2_sio_getc$'"

assert_check "Symbol 'ps2_set_gs_crt' exists" \
    "$READELF -s $ELF | grep -E '[[:space:]]ps2_set_gs_crt$'"

assert_check "Symbol 'ps2_gs_put_imr' exists" \
    "$READELF -s $ELF | grep -E '[[:space:]]ps2_gs_put_imr$'"

assert_check "Symbol 'ps2_gs_vsync' exists" \
    "$READELF -s $ELF | grep -E '[[:space:]]ps2_gs_vsync$'"

assert_check "Symbol 'ps2_gs_swap_buffers' exists" \
    "$READELF -s $ELF | grep -E '[[:space:]]ps2_gs_swap_buffers$'"

assert_check "Symbol 'ps2_pad_init' exists" \
    "$READELF -s $ELF | grep -E '[[:space:]]ps2_pad_init$'"

assert_check "Symbol 'ps2_pad_poll' exists" \
    "$READELF -s $ELF | grep -E '[[:space:]]ps2_pad_poll$'"

assert_check "Symbol 'ps2_usb_init' exists" \
    "$READELF -s $ELF | grep -E '[[:space:]]ps2_usb_init$'"

assert_check "Symbol 'ps2_usb_poll' exists" \
    "$READELF -s $ELF | grep -E '[[:space:]]ps2_usb_poll$'"

assert_check "Symbol 'ps2_usb_hid_to_btron_key' exists" \
    "$READELF -s $ELF | grep -E '[[:space:]]ps2_usb_hid_to_btron_key$'"

assert_check "Symbol 'ps2_usb_process_keyboard_report' exists" \
    "$READELF -s $ELF | grep -E '[[:space:]]ps2_usb_process_keyboard_report$'"

assert_check "Symbol 'ps2_usb_inject_keyboard' exists" \
    "$READELF -s $ELF | grep -E '[[:space:]]ps2_usb_inject_keyboard$'"

assert_check "Symbol 'init_baremetal_desktop' exists" \
    "$READELF -s $ELF | grep -E '[[:space:]]init_baremetal_desktop$'"

assert_check "Symbol 'workbench_init' exists" \
    "$READELF -s $ELF | grep -E '[[:space:]]workbench_init$'"

assert_check "Symbol 'workbench_process_event' exists" \
    "$READELF -s $ELF | grep -E '[[:space:]]workbench_process_event$'"

assert_check "Symbol 'workbench_render' exists" \
    "$READELF -s $ELF | grep -E '[[:space:]]workbench_render$'"

assert_check "Symbol 'global_menu_init' exists" \
    "$READELF -s $ELF | grep -E '[[:space:]]global_menu_init$'"

assert_check "Symbol 'open_vobj_manager_window' exists" \
    "$READELF -s $ELF | grep -E '[[:space:]]open_vobj_manager_window$'"

assert_check "Symbol 'open_t_editor_window' exists" \
    "$READELF -s $ELF | grep -E '[[:space:]]open_t_editor_window$'"

assert_check "Symbol 'open_gterm_window' exists" \
    "$READELF -s $ELF | grep -E '[[:space:]]open_gterm_window$'"

assert_check "Symbol 'launch_ps2_desktop_session' exists (Two-Stage GUI loader)" \
    "$READELF -s $ELF | grep -E '[[:space:]]launch_ps2_desktop_session$'"

assert_check "Symbol 'ps2_gs_text_puts' exists (Stage 1 Text Console)" \
    "$READELF -s $ELF | grep -E '[[:space:]]ps2_gs_text_puts$'"

assert_check "Symbol 'ps2_gs_set_mode' exists (800x600 VESA Mode Switcher)" \
    "$READELF -s $ELF | grep -E '[[:space:]]ps2_gs_set_mode$'"

assert_check "Symbol 'ps2_gs_get_width' exists" \
    "$READELF -s $ELF | grep -E '[[:space:]]ps2_gs_get_width$'"

assert_check "Symbol 'ps2_gs_get_height' exists" \
    "$READELF -s $ELF | grep -E '[[:space:]]ps2_gs_get_height$'"

# 5. Check Instruction Encoding for Emotion Engine MIPS-III Compatibility
# Emotion Engine R5900 does not support MIPS32r2 opcodes like seb, seh, ins, ext
assert_check "Zero invalid MIPS32r2 opcodes (seb, seh, ins, ext)" \
    "! ($OBJDUMP -d $ELF | grep -E '\<(seb|seh|ins|ext)\>')"

# 6. One Calling Convention (Normative -- see "One Calling Convention" in doc/md/PS2.md)
# -march=mips3 and -march=mips2 disagree on where O32 puts a C function's 5th..8th
# fixed argument (registers $t0..$t3 vs the stack), so in a mixed image every such
# call that crosses the boundary receives garbage from argument 5 on.  The PS2 mouse
# pointer disappeared because draw_baremetal_cursor_raw() -- five arguments -- was
# exactly one of those calls.
assert_check "PS2 image is 32-bit-GPR O32 (MIPS ABI flags: ISA MIPS2, GPR size 32)" \
    "$READELF -A $ELF | grep -E 'ISA:[[:space:]]+MIPS2' && $READELF -A $ELF | grep -E 'GPR size:[[:space:]]+32'"

# The ELF's flags are the linker's merge of the objects' and a stale ELF can outlive a
# flag change, so check the translation units themselves: one object built for a
# 64-bit-GPR MIPS would put the mixed-ABI bug back.
assert_check "Every PS2 object is 32-bit-GPR (no mips3 translation unit left in the link)" \
    "! (find src -name '*.ps2.o' -exec $READELF -A {} + | grep -E 'GPR size:[[:space:]]+64')"

# The one intentional exception: the GS latches a privileged register on a single
# 64-bit store, which 32-bit GPRs cannot express, so ps2_gs_reg.s holds the only
# 64-bit instructions in the image.  Its two accessors take and return 32-bit pieces,
# so they stay O32-legal across the boundary.  GNU objdump decodes MIPS32 here and
# prints these as .word, so this needs a 64-bit-capable disassembler.
OBJDUMP64=""
for p in llvm-objdump /opt/homebrew/opt/llvm/bin/llvm-objdump /usr/local/opt/llvm/bin/llvm-objdump /usr/lib/llvm-*/bin/llvm-objdump; do
    if command -v "$p" >/dev/null 2>&1; then OBJDUMP64="$p"; break; fi
done
if [ -n "$OBJDUMP64" ]; then
    assert_check "GS register write is one 64-bit sd (ps2_gs_poke64)" \
        "[ \$($OBJDUMP64 -d --triple=mips64el-unknown-elf $ELF | awk '/<ps2_gs_poke64>:/{f=1} f&&/^[[:space:]]*\$/{exit} f' | grep -cE '[[:space:]]sd[[:space:]]') -eq 1 ]"
    assert_check "GS register read is one 64-bit ld (ps2_gs_peek64)" \
        "[ \$($OBJDUMP64 -d --triple=mips64el-unknown-elf $ELF | awk '/<ps2_gs_peek64>:/{f=1} f&&/^[[:space:]]*\$/{exit} f' | grep -cE '[[:space:]]ld[[:space:]]') -eq 1 ]"
else
    echo "  [SKIP] 64-bit-GPR disassembler unavailable -- GS sd/ld accessor tests not run"
fi

# 7. The R5900 FPU is single-precision (Normative -- see "The FPU law" in doc/md/PS2.md)
# ldc1/sdc1 and every .d COP1 op are not merely slow here, they are *illegal*: PCSX2
# prints "Unknown R5900 COP1:" and the instruction does nothing at all, so every double
# the image computes comes out as garbage.  A black XMB was exactly that: 2,093 such rows
# per bench run and 383,999 of 384,000 surface pixels unlit.
#
# The law has two halves now and they are different assertions, so they get separate
# tests.  The image is built -msoft-float, which is what keeps the doubles off the FPU;
# one file, src/drivers/ps2/ps2_builtins.c, is deliberately built -mhard-float so its
# single-precision libcalls can execute on COP1 (measured 20 cycles/op against 160 for
# the bit engine in that same file).  "Zero FP instructions in the image" was therefore
# replaced by:
#
#   (a) nothing but that one object may hold a COP1 instruction -- a leftover .s op in
#       any other translation unit means a file that escaped -msoft-float, and its float
#       arguments would arrive in registers the rest of the image does not use;
#   (b) no op of double width, and none of the .s encodings this CPU does not implement
#       (c.un.s, trunc.w.s -- both proven illegal by byte-scanning the linked image).
#
# GNU objdump decodes MIPS3 here and can print these as .word, so use the 64-bit
# decoder that sees every COP1 encoding.
FP_PAT='(^|[[:space:]])(ldc1|sdc1|lwc1|swc1|(add|sub|mul|div|sqrt|abs|neg|mov|cvt|trunc|round|ceil|floor)[a-z]*\.[sdfw](\.[sdfw])?|c\.(eq|neq|lt|leq|ngt|nge|un|ord|sf|t|f)\.[sdfw])([[:space:]]|$)'
# What may not appear anywhere, in any object: the double-width forms and the two
# single-precision encodings proven not to exist on this CPU.  The width class is `d`
# alone -- writing [sd] here would flag every legal add.s and cvt.s.w, which is exactly
# the mistake the first version of this test made.
FORBIDDEN_PAT='(^|[[:space:]])(ldc1|sdc1|(add|sub|mul|div|sqrt|abs|neg|mov|cvt|rem)[a-z]*\.d(\.[sdw])?|c\.(eq|neq|lt|leq|ngt|nge|un|ord|sf|t|f)\.d|c\.un\.s|trunc\.w\.s|(round|ceil|floor)\.[lw]*\.s)([[:space:]]|$)'
if [ -n "$OBJDUMP64" ]; then
    assert_check "Only ps2_builtins.ps2.o holds COP1 instructions (every other unit is -msoft-float)" \
        "! (find src -name '*.ps2.o' ! -name 'ps2_builtins.ps2.o' -exec $OBJDUMP64 -d --triple=mips64el-unknown-elf {} + | grep -cE '$FP_PAT')"
    assert_check "Zero double-width or EE-illegal FPU op in the image (.d ops, c.un.s, trunc.w.s)" \
        "! ($OBJDUMP64 -d --triple=mips64el-unknown-elf $ELF | grep -cE '$FORBIDDEN_PAT')"
    # The same law read off the bytes rather than off a disassembler's naming, because an
    # encoder this toolchain changes its mind about is exactly the one that would slip
    # through a mnemonic grep: c.un.s is 0x46020031 and trunc.w.s is 0x4600010d, stored
    # little-endian.  Scanned over the whole file, not just .text -- a section-name typo
    # would otherwise turn this test into a silent pass.
    ELF_HEX="$(xxd -p $ELF | tr -d '\n')"
    # The variable reference survives to eval time -- a 4.6 MB image is a 9 MB hex string
    # and pasting it into the command text would make every failure print it.
    assert_check "The two illegal COP1 encodings are absent from the image byte-for-byte" \
        "! (grep -q '31000246\|0d010046' <<<\"\$ELF_HEX\")"
    # And the positive half of the same check: the sanctioned object really does contain
    # them, so a stale -msoft-float build of it cannot pass by having no FPU code at all.
    assert_check "ps2_builtins.ps2.o really emits COP1 arithmetic (the hardware path is compiled in)" \
        "[ \$($OBJDUMP64 -d --triple=mips64el-unknown-elf src/drivers/ps2/ps2_builtins.ps2.o | grep -cE '[[:space:]](add|sub|mul|div)\.s[[:space:]]') -ge 4 ]"
else
    echo "  [SKIP] 64-bit-GPR disassembler unavailable -- FPU-opcode sweep not run"
fi

# A -msoft-float link needs its runtime: clang emits libcalls where the FPU would
# have been and -nostdlib provides none of them, so an unresolved one is a silent
# wrong answer rather than a link error.  Symbol 0 is the null local entry, which is
# UND by definition and not a reference, so only named UND entries count.
assert_check "Soft-float runtime resolves: no named undefined symbol in the image" \
    "[ \$($READELF -s $ELF | awk '\$7==\"UND\" && \$8!=\"\"' | wc -l | tr -d ' ') -eq 0 ]"

SOFTFLOAT_LIBCALLS="__adddf3 __subdf3 __muldf3 __divdf3 __ltdf2 __ledf2 __gtdf2 __gedf2 \
__eqdf2 __nedf2 __unorddf2 __floatsidf __floatunsidf __fixdfsi __floatdidf __fixdfdi \
__truncdfsf2 __extendsfdf2 __addsf3 __subsf3 __mulsf3 __divsf3 __ltsf2 __lesf2 __gtsf2 \
__gesf2 __eqsf2 __nesf2 __floatsisf __floatunsisf __fixsfsi __fixunssfsi"
# Read the table once into a variable: piping readelf straight into `grep -q` would
# make grep exit on the first match, readelf then dies on SIGPIPE and `set -o
# pipefail` reports that as the pipeline's status, so a symbol that IS present would
# be counted as missing.
PS2_SYMBOLS="$($READELF -s $ELF)"
SOFTFLOAT_CHECK="missing=0;"
for _s in $SOFTFLOAT_LIBCALLS; do
    SOFTFLOAT_CHECK="$SOFTFLOAT_CHECK grep -qE ' $_s\$' <<<\"\$PS2_SYMBOLS\" || missing=1;"
done
assert_check "All 32 soft-float libcalls are defined (src/drivers/ps2/ps2_builtins.c)" \
    "$SOFTFLOAT_CHECK [ \$missing -eq 0 ]"

# 8. Check Bootable ISO Disc Packaging
if [ -f "$ISO" ]; then
    assert_check "ISO disc image exists and is non-empty" \
        "[ -s $ISO ]"
fi

echo "----------------------------------------------------------"
echo " PS2 Test Results: $PASSED / $TOTAL tests passed (100%)"
echo "----------------------------------------------------------"
echo "[CI-TEST] SUCCESS — PlayStation 2 ELF & drivers validated!"
