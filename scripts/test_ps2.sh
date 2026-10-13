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

# 6. One Calling Convention (Normative -- see "One Calling Convention" in doc/txt/PS2.txt)
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

# Increment 3 of the EE/GS backend (doc/txt/PS2.txt, ch. 16 "The increments", row 3): the drawing
# environment -- XYOFFSET, SCISSOR, FRAME, ZBUF and the four control registers a primitive
# reads -- reaches the GS as one GIF packet, the same way a triangle does, and not as nine
# privileged register writes.  The shape matters for two reasons the pokes do not serve: a
# packet costs one DMAC start instead of nine separate bus transactions, and a packet is a
# record of words, which is what lets increment 4 replay it after the canvas stream instead
# of re-executing it.
#
# Read off the image, because "is the environment a packet" is a property of what executes
# rather than of how the source is phrased.  What makes it readable is the translation-unit
# split: gs_draw_env_packet lives in its own file and PS2_CFLAGS carries no -flto, so the
# call into it is a real jal and its absence would mean the builder had been bypassed.  The
# sender is the opposite case -- it is in ps2_gs.c and -O2 inlines it into the apply path --
# so the DMA is asserted through the DMAC window it writes instead of through a symbol that
# is not there.  Measured on the linked image: MADR (0xa010), QWC (0xa020) and D_STAT (0xe010)
# together appear in exactly three function ranges -- ps2_gs_gif_send, ps2_gs_upload and
# ps2_gs_draw_env_apply -- and in nothing else, so the triple is a precise fingerprint of a
# channel-2 producer.
if [ -n "$OBJDUMP64" ]; then
    # The awk stops printing at the blank line but keeps reading, rather than exiting there:
    # an early `exit` SIGPIPEs the disassembler, and under `set -o pipefail` the assignment
    # then fails with status 141 before a single row runs.
    ENV_APPLY_ASM="$($OBJDUMP64 -d --triple=mips64el-unknown-elf $ELF | awk '/<ps2_gs_draw_env_apply>:/{f=1;print;next} f&&/^[[:space:]]*$/{f=0;next} f')"
    ENV_BUILDER_ASM="$($OBJDUMP64 -d --triple=mips64el-unknown-elf $ELF | awk '/<gs_draw_env_packet>:/{f=1;print;next} f&&/^[[:space:]]*$/{f=0;next} f')"
    # Integers rather than disassembly text inside the evaluated command, for the reason the
    # CP0 write test gives: a `$` pasted into an eval'd string is expanded by the shell.
    ENV_APPLY_BUILDER="$(printf '%s\n' "$ENV_APPLY_ASM" | grep -c 'jal.*<gs_draw_env_packet>' || true)"
    ENV_APPLY_POKES="$(printf '%s\n' "$ENV_APPLY_ASM" | grep -c 'ps2_gs_poke64' || true)"
    ENV_APPLY_CHCR="$(printf '%s\n' "$ENV_APPLY_ASM" | grep -c '0xa000' || true)"
    ENV_APPLY_MADR="$(printf '%s\n' "$ENV_APPLY_ASM" | grep -c '0xa010' || true)"
    ENV_APPLY_QWC="$(printf '%s\n' "$ENV_APPLY_ASM" | grep -c '0xa020' || true)"
    ENV_APPLY_STAT="$(printf '%s\n' "$ENV_APPLY_ASM" | grep -c '0xe010' || true)"
    # A missing function makes its own counter 0, so this fails loudly rather than vacuously:
    # no branch of the suite treats an absent ps2_gs_draw_env_apply as a pass.
    assert_check "The draw environment is a GIF packet: ps2_gs_draw_env_apply calls the builder once, pokes no GS register" \
        "[ $ENV_APPLY_BUILDER -eq 1 ] && [ $ENV_APPLY_POKES -eq 0 ]"
    assert_check "...and it leaves through DMAC channel 2: CHCR, MADR, QWC and D_STAT all written in that path" \
        "[ $ENV_APPLY_CHCR -ge 1 ] && [ $ENV_APPLY_MADR -ge 1 ] && [ $ENV_APPLY_QWC -ge 1 ] && [ $ENV_APPLY_STAT -ge 1 ]"
    # The third increment-3 law is the one that makes the host suite possible: the builder
    # computes words and touches no memory-mapped register, so the identical packet the EE
    # sends can be checked word-for-word on a machine with no GS in it at all.
    ENV_BUILDER_REGS="$(printf '%s\n' "$ENV_BUILDER_ASM" | grep -cE 'ps2_gs_poke64|ps2_gs_gif_send|0xa0[0-9]0|0xe010' || true)"
    assert_check "The environment builder is pure computation: gs_draw_env_packet touches no GS or DMAC register" \
        "[ $ENV_BUILDER_REGS -eq 0 ]"
else
    echo "  [SKIP] 64-bit-GPR disassembler unavailable -- GIF-packet environment laws not run"
fi

# Increment 4, first half: the vertex recorder, and the Makefile's `PS2_GL=` dial reaching the
# image it is read out of.
#
# The recorder's law is the increment-3 one again and for the same reason: a stream that pokes a
# register cannot be replayed, and cannot be checked on a host.  It is asserted off the image
# because "no DMA in sight" is a property of what the compiler kept, not of how the source reads.
#
# The dial's two rows are the recursion law.  The Display applet and `PS2_GL=eegs` now make the
# same request, and while that backend has no ops table the request is answered by a software
# rasteriser -- which has to be the *software rule*, because on a `PS2_GL=eegs` build the build
# default is the GS itself, and a placeholder that fell back to the default would call itself
# until the stack ran off.  Both calls are inter-translation-unit, so both are readable as jal.
if [ -n "$OBJDUMP64" ]; then
    REC_ASM="$($OBJDUMP64 -d --triple=mips64el-unknown-elf $ELF | awk '/<gs_rec_[a-z_]+>:/{f=1;print;next} f&&/^[[:space:]]*$/{f=0;next} f')"
    REC_REGS="$(printf '%s\n' "$REC_ASM" | grep -cE 'ps2_gs_poke64|ps2_gs_gif_send|0xa0[0-9]0|0xe0[0-9]0' || true)"
    REC_LINES="$(printf '%s\n' "$REC_ASM" | grep -c '^\ *[0-9a-f]' || true)"
    # Both counters are read before the assertion: an empty $REC_ASM would make the first one 0
    # and pass the row vacuously, which is the false negative the recorder's own link into the
    # image is here to rule out.
    assert_check "The vertex recorder is linked into the image (gs_rec_* present, $(printf '%s\n' "$REC_ASM" | grep -cE '^0*[0-9a-f]+ <gs_rec_') functions)" \
        "[ $REC_LINES -gt 100 ]"
    assert_check "...and it is pure computation: the whole recorder touches no GS register and no DMAC register" \
        "[ $REC_REGS -eq 0 ]"

    # Increment 5b: the texture-plane registry, its purity, and the one seam that makes its words
    # reach the bus.  Purity is the increment-3 and -4 law again (a stream that pokes a register
    # cannot be replayed or gated on a host); the seam row is 5b's own, because a registry that is
    # linked but never handed a sink plans planes whose bytes never cross, and the symptom is an
    # icon drawn from a page that holds whatever was uploaded last.
    TEX_ASM="$($OBJDUMP64 -d --triple=mips64el-unknown-elf $ELF | awk '/<gs_tex_[a-z_]+>:/{f=1;print;next} f&&/^[[:space:]]*$/{f=0;next} f')"
    TEX_REGS="$(printf '%s\n' "$TEX_ASM" | grep -cE 'ps2_gs_poke64|ps2_gs_gif_send|send_trx|send_image|0xa0[0-9]0|0xe0[0-9]0' || true)"
    TEX_LINES="$(printf '%s\n' "$TEX_ASM" | grep -c '^\ *[0-9a-f]' || true)"
    assert_check "The texture-plane registry is linked into the image (gs_tex_* present, $(printf '%s\n' "$TEX_ASM" | grep -cE '^0*[0-9a-f]+ <gs_tex_') functions)" \
        "[ $TEX_LINES -gt 40 ]"
    assert_check "...and it is pure computation: the whole registry touches no GS register, no DMAC register and no transfer" \
        "[ $TEX_REGS -eq 0 ]"

    GSINIT_ASM="$($OBJDUMP64 -d --triple=mips64el-unknown-elf $ELF | awk '/<ps2_gs_init>:/{f=1;print;next} f&&/^[[:space:]]*$/{f=0;next} f')"
    GSINIT_SINK="$(printf '%s\n' "$GSINIT_ASM" | grep -c 'jal.*<gs_tex_install_sink>' || true)"
    assert_check "The GS driver installs itself as the registry's sink at init, so a plane's bytes cross while the app still draws" \
        "[ $GSINIT_SINK -ge 1 ]"

    # The same function's other seam, gated separately because the two failures it prevents are
    # different: without the sink a plane's bytes never cross, without `gs_tex_init` the cursor
    # starts at zero -- which is not an empty page but the canvas the PCRTC reads, so the first
    # uploaded icon would overwrite the screen it is drawn on.  The static initializer cannot show
    # that on a host (a zeroed `gs_vram_t` is what the host suite sees too), so it is an image row.
    GSINIT_VRAM="$(printf '%s\n' "$GSINIT_ASM" | grep -c 'jal.*<gs_tex_init>' || true)"
    assert_check "The registry's VRAM cursor is started above the canvas at init, not at page 0" \
        "[ $GSINIT_VRAM -ge 1 ]"

    # The upload seam: `gl_tex_plane_publish()` is texture.c's call into a different translation
    # unit, and `gl_image_store()` is static and inlined, so the count is taken over the whole
    # image rather than inside one named function.  Dead-code elimination here is the silent
    # 5b failure: no plane registered means the stage clears TME on every textured primitive,
    # and with the canvas still blitted underneath, the screen looks exactly as it did before.
    TEX_JAL="$($OBJDUMP64 -d --triple=mips64el-unknown-elf $ELF | grep -c 'jal.*<gs_tex_register>' || true)"
    assert_check "A GL texture upload reaches the plane registry from the downstack ($TEX_JAL call sites)" \
        "[ $TEX_JAL -ge 1 ]"

    # The other half of the same seam, in the other direction: the fill path resolves the bound
    # image's plane and names it to the stage, which is what makes the recorded `PRIM` carry TME.
    # Both are inter-translation-unit calls into files this one cannot see, so both read as jal.
    FILL_ASM="$($OBJDUMP64 -d --triple=mips64el-unknown-elf $ELF | awk '/<gl_draw_triangle_fill>:/{f=1;print;next} f&&/^[[:space:]]*$/{f=0;next} f')"
    FILL_TEX="$(printf '%s\n' "$FILL_ASM" | grep -c 'jal.*<gs_tex_find>' || true)"
    FILL_STAGE="$(printf '%s\n' "$FILL_ASM" | grep -c 'jal.*<eegs_texture>' || true)"
    assert_check "The textured fill looks its plane up in the registry and hands it to the raster stage" \
        "[ $FILL_TEX -ge 1 ] && [ $FILL_STAGE -ge 1 ]"

    # Increment 5c retired the placeholder.  `GL_BACKEND_EEGS` no longer resolves to "whatever the
    # software rule says": gl_init's EEGS arm installs the *linked* CPU front end, because row 5
    # refuses a second rasteriser and the GS stage hangs on TinyGL's own draw_triangle hooks.  So
    # the two arms that name a pipeline are the same call twice, and the arm that used to fall back
    # is compiled out of this build entirely -- which is what the second row reads.  A third
    # `gl_backend_default` call in here would mean the EEGS case had become recursive (the dial
    # asking itself what to be), and that is the shape this row catches.
    GLINIT_ASM="$($OBJDUMP64 -d --triple=mips64el-unknown-elf $ELF | awk '/<gl_init>:/{f=1;print;next} f&&/^[[:space:]]*$/{f=0;next} f')"
    GLINIT_TG="$(printf '%s\n' "$GLINIT_ASM" | grep -c 'jal.*<tinygl_backend_init>' || true)"
    GLINIT_SW="$(printf '%s\n' "$GLINIT_ASM" | grep -c 'jal.*<gl_backend_software_default>' || true)"
    GLINIT_DEF="$(printf '%s\n' "$GLINIT_ASM" | grep -c 'jal.*<gl_backend_default>' || true)"
    assert_check "EE/GS installs the linked TinyGL pipeline (gl_init calls tinygl_backend_init $GLINIT_TG times: the TINYGL arm and the EE/GS arm)" \
        "[ $GLINIT_TG -eq 2 ]"
    assert_check "...and no arm of it resolves a backend name to a placeholder or to the dial again" \
        "[ $GLINIT_SW -eq 0 ] && [ $GLINIT_DEF -eq 0 ]"

    # What the image was dialled to, read off the constant it returns rather than off the flags
    # the build printed.  Until 5c this row's whole subject was that 3 was not a legal answer; now
    # it is, because the two rows above are what make the name mean something.  What remains a
    # defect is a value the link does not have at all, and an unreadable line leaves GLDEF_RET at
    # 9 -- which fails the row rather than passing it.
    #
    # The register is spelled `$2` because this is llvm-objdump's numeric GPR naming, and the
    # value is a `0x` field, so the two greps below take the whole fixed string and the trailing
    # hex.
    GLDEF_ASM="$($OBJDUMP64 -d --triple=mips64el-unknown-elf $ELF | awk '/<gl_backend_default>:/{f=1;print;next} f&&/^[[:space:]]*$/{f=0;next} f')"
    GLDEF_HEX="$(printf '%s\n' "$GLDEF_ASM" | grep -m1 -oE 'addiu[[:space:]]+\$2,[[:space:]]+\$zero,[[:space:]]+0x[0-9a-f]+' | grep -oE '0x[0-9a-f]+$' || true)"
    if [ -n "$GLDEF_HEX" ]; then GLDEF_RET=$(( GLDEF_HEX )); else GLDEF_RET=9; fi
    assert_check "The image's build default names a backend the link can install (GLDEF_RET=$GLDEF_RET; 3 is EE/GS and is legal from 5c)" \
        "[ $GLDEF_RET -le 3 ]"

    # ── Increment 5c: the present path, in the order the hardware forces ──────────────
    #
    # The staging, the ordering and the two seams are this increment's whole content, and none of
    # them is a word: eegs_present's own suite reads the packets, but *where* the calls sit in the
    # present is a property of the image.  `ps2_paint_region` is static and -O2 folds it into its
    # caller, so the slice is taken over both spellings and the measurement is the relative address
    # of three call sites inside whichever one exists:
    #
    #   ps2_gs_upload            (the canvas)      must come FIRST
    #   eegs_present_send        (env + primitives)
    #   ps2_gs_upload            (the cover)       must come LAST
    #
    # A present that sent the stream before the upload would have the canvas erase it -- the
    # failure is a layer that draws and then disappears, which is exactly the kind of thing that
    # reads as a GS bug on the screen and is a call-order bug in the source.
    PAINT_ASM="$($OBJDUMP64 -d --triple=mips64el-unknown-elf $ELF | awk '/<ps2_paint_(region|bands)>:/{f=1;print;next} f&&/^[[:space:]]*$/{f=0;next} f')"
    PAINT_BEGIN="$(printf '%s\n' "$PAINT_ASM" | grep -c 'jal.*<eegs_present_begin>' || true)"
    PAINT_END="$(printf '%s\n' "$PAINT_ASM" | grep -c 'jal.*<eegs_present_end>' || true)"
    PAINT_SEND="$(printf '%s\n' "$PAINT_ASM" | grep -c 'jal.*<eegs_present_send>' || true)"
    assert_check "The present brackets the composite with the recorder ($PAINT_BEGIN begin, $PAINT_END end, $PAINT_SEND send)" \
        "[ $PAINT_BEGIN -ge 1 ] && [ $PAINT_END -ge 1 ] && [ $PAINT_SEND -eq 1 ]"

    PAINT_LAYER="$(printf '%s\n' "$PAINT_ASM" | grep -c 'jal.*<wnd_gl_layer_take>' || true)"
    PAINT_COVER="$(printf '%s\n' "$PAINT_ASM" | grep -c 'jal.*<wnd_gl_cover_take>' || true)"
    assert_check "...and takes both of the layer's facts from the window manager, not from a copy of them" \
        "[ $PAINT_LAYER -ge 1 ] && [ $PAINT_COVER -ge 1 ]"

    UP1="$(printf '%s\n' "$PAINT_ASM" | grep -E 'jal.*<ps2_gs_upload>'  | head -1 | grep -oE '^ *[0-9a-f]+' | tr -d ' ' || true)"
    SEND="$(printf '%s\n' "$PAINT_ASM" | grep -E 'jal.*<eegs_present_send>' | head -1 | grep -oE '^ *[0-9a-f]+' | tr -d ' ' || true)"
    UP2="$(printf '%s\n' "$PAINT_ASM" | grep -E 'jal.*<ps2_gs_upload>'  | tail -1 | grep -oE '^ *[0-9a-f]+' | tr -d ' ' || true)"
    assert_check "The bus order is canvas, stream, cover: the upload's call site precedes the send's, and the cover re-upload follows it" \
        "[ -n \"$UP1\" ] && [ -n \"$SEND\" ] && [ -n \"$UP2\" ] && [ $((0x$UP1)) -lt $((0x$SEND)) ] && [ $((0x$SEND)) -lt $((0x$UP2)) ]"

    PRESENT_FN="$(printf '%s\n' "$($OBJDUMP64 -d --triple=mips64el-unknown-elf $ELF | grep -cE '^0*[0-9a-f]+ <eegs_present_')")"
    # The read is part of the link claim, not of the order claim: `[GSTS]`'s qw, drop, unsplit,
    # texsw and texmiss columns come out of `eegs_present_get_stats()` and nowhere else, so a
    # present that links all four functions but never reads them prints a row of zeros and looks
    # like a frame that drew nothing.
    PAINT_STATS="$(printf '%s\n' "$PAINT_ASM" | grep -c 'jal.*<eegs_present_get_stats>' || true)"
    assert_check "The staged present is linked into the image and read by the present ($PRESENT_FN eegs_present_* functions, $PAINT_STATS stats read)" \
        "[ $PRESENT_FN -ge 4 ] && [ $PAINT_STATS -ge 1 ]"
else
    echo "  [SKIP] 64-bit-GPR disassembler unavailable -- recorder purity and PS2_GL dial laws not run"
fi

# 7. The R5900 FPU is single-precision (Normative -- see "The FPU law" in doc/txt/PS2.txt)
# ldc1/sdc1 and every .d COP1 op are not merely slow here, they are *illegal*: PCSX2
# prints "Unknown R5900 COP1:" and the instruction does nothing at all, so every double
# the image computes comes out as garbage.  A black XMB was exactly that: 2,093 such rows
# per bench run and 383,999 of 384,000 surface pixels unlit.
#
# The law has two halves now and they are different assertions, so they get separate
# tests.  The image is built -msoft-float, which is what keeps the doubles off the FPU;
# three files are deliberately built -mhard-float so their single-precision arithmetic can
# execute on COP1 (measured 20 cycles/op against 160 for the bit engine): the runtime
# src/drivers/ps2/ps2_builtins.c, whose libcalls are the FPU's own, and the geometry
# kernel src/gl/tinygl/vertex_stage.c (plan row 7), which is reached only by pointer so
# no float crosses its boundary and the ABI law is unbroken.  Since row 8 the third is
# src/apps/xmb_wave.c, the launcher's waving surface (plan row 8): its clock arrives as
# IEEE-754 bits in an integer and its sheet leaves through memory, so it earns the flag
# the same way the kernel does.  "Zero FP instructions in the image" was therefore
# replaced by:
#
#   (a) nothing but those three objects may hold a COP1 instruction -- a leftover .s op in
#       any other translation unit means a file that escaped -msoft-float, and its float
#       arguments would arrive in registers the rest of the image does not use;
#   (b) no op of double width, and none of the .s encodings this CPU does not implement
#       (the unordered compare family -- c.un.s, c.ueq.s, c.ult.s, c.ule.s -- and
#       trunc.w.s, all proven illegal by byte-scanning the linked image).
#
# Half (b) is what the kernel is guarded by, and it is not decoration: the first build of
# that kernel put sdc1/ldc1 into the image unasked, because a `-mhard-float` unit that
# *calls* out for its float -> int conversion has to preserve live FPRs across the call,
# and with the O32 32-bit-FPR file the compiler saves them as even/odd pairs with the
# double-width store.  The conversions are inlined from ps2_fp.h instead, and this sweep is
# the row that says so.
#
# The pair-store is not merely one illegal op among the ones (b) names; on this CPU it is
# the reason a whole register class is unusable, and row 8 measured it.  Under O32 FR=0 the
# caller-saved singles are $f0-$f18 and the callee-saved ones are $f20, $f22, ... $f30, and
# preserving one of those is *only* ever done with `sdc1` of the even/odd pair -- there is
# no single-precision callee-save instruction.  So a callee-saved float cannot survive a
# call on the Emotion EE at all: the store does nothing, the reload does nothing, and the
# callee returns having silently overwritten it.  That is a third failure mode, distinct
# from (a) and (b), and clang reaches for it by register allocation rather than by any
# instruction the source asks for -- a working set over ten floats, a float live across a
# call, or simply a loop-invariant literal hoisted out of a loop that calls, which the
# greedy allocator ranks as a long live range and therefore puts in a CSR.  Measured on
# src/apps/xmb_wave.c: 52 pair stores in the first four-pass arrangement, 36 in the eight
# passes, 18 after the trig and the root were outlined, 4 with the allocator stopped from
# choosing a CSR at all, and 0 once the shading pass's eight live values were split into
# two passes.  The control for the rows below is that last number taken away: compiling this
# same source without the flag puts 14 pair stores and 32 CSR references back in, so the
# knob is load-bearing on the file as it stands and not only on its first arrangement.  The flag is `-mllvm -regalloc-csr-first-time-cost=1000000000`, attached to
# those three objects in the Makefile; an unknown `-mllvm` argument fails the compile, so a
# toolchain that renames it stops the build instead of quietly reopening the hole.
#
# The law inside a hard-float unit is therefore *no float survives a call*, and the check
# is the register file itself: no $f20-$f31 may appear in the image.  Naming the pair-store
# opcode would not be enough, because a function that reads a CSR without ever storing one
# is already broken -- it holds whatever boot code left there.
#
# GNU objdump decodes MIPS3 here and can print these as .word, so use the 64-bit
# decoder that sees every COP1 encoding.
FP_PAT='(^|[[:space:]])(ldc1|sdc1|lwc1|swc1|(add|sub|mul|div|sqrt|abs|neg|mov|cvt|trunc|round|ceil|floor)[a-z]*\.[sdfw](\.[sdfw])?|c\.(eq|neq|lt|leq|ngt|nge|un|ueq|ult|ule|olt|ole|ord|sf|t|f)\.[sdfw])([[:space:]]|$)'
# The three units that may hold COP1, spelled once because four rows below read this list
# and a fifth must not be able to forget to update one of them.  The whitelist row needs
# them as `-name` patterns (a basename, matched against every path find walks), so that
# form is derived here rather than written out a second time.
FP_OBJECTS='src/drivers/ps2/ps2_builtins.ps2.o src/gl/tinygl/vertex_stage.ps2.o src/apps/xmb_wave.ps2.o'
FP_FIND_EXCLUDES=''
for _fp_obj in $FP_OBJECTS; do FP_FIND_EXCLUDES="$FP_FIND_EXCLUDES ! -name $(basename $_fp_obj)"; done
unset _fp_obj
# A reference to a callee-saved single.  The `([^0-9]|$)` is what keeps $f2 and $f19 out of
# a net for $f20; the `.w` spellings some decoders use are caught by the same dot.
CSR_PAT='\$f(2[0-9]|3[01])([^0-9]|$)'
# What may not appear anywhere, in any object: the double-width forms and the
# single-precision encodings proven not to exist on this CPU.  The width class is `d`
# alone -- writing [sd] here would flag every legal add.s and cvt.s.w, which is exactly
# the mistake the first version of this test made.
#
# The compare family is the whole-function-low-bit pair `c.X.s` / `c.uX.s`, and the
# unordered side of that pair is EE-illegal as a family, not one instruction at a time:
# `c.un.s` was caught by the self-test, and `c.ule.s` was caught three weeks later by the
# XMB drawing nothing at all -- a `-mhard-float` unit that compares with `>` reaches for
# `c.ule.s` (clang lowers `a > b` as `b < a` in the unordered-inclusive form), the decoder
# ignores it, the condition code keeps the *previous* compare's answer, and every face in
# the frame came out of the transform clipped.  Naming only `c.un.s` is what let that
# through: the four members of the family are one law, so all four are named.
FORBIDDEN_PAT='(^|[[:space:]])(ldc1|sdc1|(add|sub|mul|div|sqrt|abs|neg|mov|cvt|rem)[a-z]*\.d(\.[sdw])?|c\.(eq|neq|lt|leq|ngt|nge|un|ord|sf|t|f)\.d|c\.(un|ueq|ult|ule)\.s|trunc\.w\.s|(round|ceil|floor)\.[lw]*\.s)([[:space:]]|$)'
if [ -n "$OBJDUMP64" ]; then
    assert_check "Only ps2_builtins.ps2.o, vertex_stage.ps2.o and xmb_wave.ps2.o hold COP1 instructions (every other unit is -msoft-float)" \
        "! (find src -name '*.ps2.o' $FP_FIND_EXCLUDES -exec $OBJDUMP64 -d --triple=mips64el-unknown-elf {} + | grep -cE '$FP_PAT')"
    assert_check "Zero double-width or EE-illegal FPU op in the image (.d ops, c.un.s, trunc.w.s)" \
        "! ($OBJDUMP64 -d --triple=mips64el-unknown-elf $ELF | grep -cE '$FORBIDDEN_PAT')"
    # The register-file half of the pair-store law, read off the linked image rather than
    # off the three objects: an object that lost its CSR to the linker's relaxation is
    # still the same bytes, but so is one the linker dropped -- and the row that matters is
    # what the console executes.  `grep -c` prints 0 and exits 1 on no match, which is the
    # idiom every row in this block uses; the `!` inverts it to the pass the law wants.
    assert_check "No callee-saved single (\$f20-\$f31) is referenced anywhere in the image (sdc1 cannot preserve one on this CPU)" \
        "! ($OBJDUMP64 -d --triple=mips64el-unknown-elf $ELF | grep -cE '$CSR_PAT')"
    # The same law read off the bytes rather than off a disassembler's naming, because an
    # encoder this toolchain changes its mind about is exactly the one that would slip
    # through a mnemonic grep.  Not as one literal each: 0x4600010d and 0x46020031 are the
    # words LLVM writes for the operand pairings the FPU self-test hand-wrote, and a real
    # cast is `trunc.w.s $f6, $f6` -- 0x4600318d -- so a single fixed word scans clean over
    # an image that is not.  Measured on the control build that put the plain casts back:
    # zero hits on the literal, nine trunc.w.s in the disassembly.  The op is
    # cop1 | 16 | rt=0 | fs | fd | 0x0d, which little-endian lays out as [048c]d ?? 00 46,
    # fd being even because this file has 32-bit FPRs -- hence the four choices in byte 0 and
    # the whole register space covered.  The c.un.s form stays the literal for its own
    # pairing; the general net for the compare family is row 58.
    #
    # Read out of .text and not out of the whole file, because a byte-granular sweep of the
    # data would match a pixel pattern by luck: the measured version of this row that scanned
    # the whole image reported 10 aligned hits where the disassembly held 9 instructions.
    # readelf's dump is 4-byte aligned by construction (.text starts on a paragraph), which is
    # what makes a window here an instruction word rather than a coincidence.  The size test
    # is the other half: it fails loudly if the section is ever renamed, which is exactly the
    # way a whole-file scan's silence used to hide a typo.
    ELF_TEXT="$($READELF --hex-dump=.text $ELF | awk '/^  0x/{print $2$3$4$5$6$7$8$9}' | tr -d '\n')"
    # The variable references survive to eval time -- a 830 KB section is a 1.7 MB hex string
    # and pasting it into the command text would make every failure print it.
    assert_check "The illegal COP1 encodings are absent from .text byte-for-byte (.text is ${#ELF_TEXT} hex chars)" \
        "[ \${#ELF_TEXT} -ge 1000000 ] && ! (grep -qE '([048c]d[0-9a-f]{2}0046|31000246)' <<<\"\$ELF_TEXT\")"
    # The unordered compare family as a *class* rather than as one literal, because the class
    # is the law and the literal was the gap: the row above names `31000246`, which is
    # `c.un.s $f0,$f2` with the operand pairing the self-test happened to hand-write, and a
    # compiler's `c.ule.s $f0,$f6` is `37000646` -- no literal on that list, one encoding away
    # in the funct field, and the same missing instruction.  What shipped on it was an XMB that
    # drew nothing: `total=5406 out=5406 drawn=0`, every face clipped by a compare that never
    # answered.  The op is cop1 | fmt | fs | ft | funct, and funct is the low byte: 0x31 un,
    # 0x33 ueq, 0x35 ult, 0x37 ule, with the even neighbours 0x30/0x32/0x34/0x36 (f, eq, olt,
    # ole) being the forms this CPU does answer -- so the four odd ones are the net, and the
    # high byte 0x46 says the word is a COP1 instruction at all.
    #
    # One word per line rather than one long string, and anchored at both ends: a compare
    # family this wide matches at a byte offset that is not an instruction boundary, which
    # measured 22 spurious hits on an image whose disassembly holds none.  readelf prints the
    # dump in 4-byte groups already, so the alignment is the toolchain's promise and not this
    # script's arithmetic.
    ELF_WORDS="$($READELF --hex-dump=.text $ELF | awk '/^  0x/{for (i = 2; i <= 9; i++) if (length($i) == 8) print $i}')"
    assert_check "No COP1 compare of the unordered family anywhere in .text (${#ELF_WORDS} dump chars, funct 0x31/33/35/37)" \
        "[ \$(grep -cE '^(31|33|35|37)[0-9a-f]{4}46\$' <<<\"\$ELF_WORDS\") -eq 0 ]"
    # And the positive half of the same check: the sanctioned objects really do contain
    # them, so a stale -msoft-float build of any of them cannot pass by having no FPU code at
    # all.  One row per object because each earns its flag differently and each can lose it
    # separately: the runtime's arithmetic is what the libcalls dispatch to, the kernel's is
    # the whole point of row 7, and the wave's is the point of row 8 -- the -mhard-float
    # substitution is a per-object flag, and a flag that silently does not reach its file is
    # the failure these rows exist to see.
    assert_check "ps2_builtins.ps2.o really emits COP1 arithmetic (the hardware path is compiled in)" \
        "[ \$($OBJDUMP64 -d --triple=mips64el-unknown-elf src/drivers/ps2/ps2_builtins.ps2.o | grep -cE '[[:space:]](add|sub|mul|div)\.s[[:space:]]') -ge 4 ]"
    # The kernel's own two halves: the arithmetic is inline on COP1, ...
    assert_check "vertex_stage.ps2.o emits COP1 mul.s/add.s arithmetic (PS2_FP=hw reached the file)" \
        "[ \$($OBJDUMP64 -d --triple=mips64el-unknown-elf src/gl/tinygl/vertex_stage.ps2.o | grep -cE '[[:space:]](mul|add)\.s[[:space:]]') -ge 40 ]"
    # ... and its six clip plane tests are in the form this CPU answers.  Six rather than
    # twelve because all six go through `c.olt.s`: the three that C writes as `>` reach it
    # with the operands swapped, which is what `gl_clipcode()`'s TGL_CLIP_LT does
    # (src/gl/tinygl/zgl.h).  The second half is the requirement that the image-wide row above
    # cannot make: a kernel with no compares at all also scans clean, and a kernel that lost
    # its clip tests is the shape the black screen arrived in.
    assert_check "vertex_stage.ps2.o's clip tests are ordered c.olt.s and none of the unordered family" \
        "[ \$($OBJDUMP64 -d --triple=mips64el-unknown-elf src/gl/tinygl/vertex_stage.ps2.o | grep -cE '[[:space:]]c\.olt\.s[[:space:]]') -ge 6 ] && ! (\$OBJDUMP64 -d --triple=mips64el-unknown-elf src/gl/tinygl/vertex_stage.ps2.o | grep -cE '[[:space:]]c\.(un|ueq|ult|ule)\.s[[:space:]]')"
    # ... and the linked image actually branches into it, which is what says the unit is a
    # kernel and not dead weight the linker kept.  Read off $ELF rather than off the object:
    # an object's `jal 0x0` carries no name until the link resolves it, and a call the linker
    # left behind is the one case the object file cannot show.
    assert_check "glopVertex calls into both kernel halves in the linked image" \
        "[ \$($OBJDUMP64 -d --triple=mips64el-unknown-elf $ELF | grep -cE 'jal[[:space:]]+0x[0-9a-f]+ <tgl_vertex_transform>') -ge 1 ] && [ \$($OBJDUMP64 -d --triple=mips64el-unknown-elf $ELF | grep -cE 'jal[[:space:]]+0x[0-9a-f]+ <tgl_vertex_to_viewport>') -ge 1 ]"
    # Row 8's unit, the same three ways.  Its arithmetic first: measured 55 mul.s and 43
    # add.s on the sheet's eight passes, against a threshold of 40 because the mesh dial
    # (32 -> 16) is a real configuration and halving the working set must not read as the
    # flag having gone missing -- a -msoft-float build of this file has zero of both.
    assert_check "xmb_wave.ps2.o emits COP1 mul.s/add.s arithmetic (PS2_FP=hw reached the wave)" \
        "[ \$($OBJDUMP64 -d --triple=mips64el-unknown-elf src/apps/xmb_wave.ps2.o | grep -cE '[[:space:]](mul|add)\.s[[:space:]]') -ge 40 ]"
    # Its six compares are the fold's and the degenerate-cell test's, all in the ordered
    # form.  The tier guards are *not* among them: `BTRONFLT_LT()` compares bit patterns in
    # integer registers precisely because `>` would lower to `c.ule.s`.
    assert_check "xmb_wave.ps2.o's compares are ordered c.olt.s and none of the unordered family" \
        "[ \$($OBJDUMP64 -d --triple=mips64el-unknown-elf src/apps/xmb_wave.ps2.o | grep -cE '[[:space:]]c\.olt\.s[[:space:]]') -ge 6 ] && ! (\$OBJDUMP64 -d --triple=mips64el-unknown-elf src/apps/xmb_wave.ps2.o | grep -cE '[[:space:]]c\.(un|ueq|ult|ule)\.s[[:space:]]')"
    # The register-file law at the file that earned it, naming the file rather than leaving
    # the image-wide row to point at a diff.  Measured history: 52 pair stores -> 36 -> 18
    # -> 4 -> 0 by the shapes described in the file's header comment.
    assert_check "xmb_wave.ps2.o references no callee-saved single (no float survives a call)" \
        "! ($OBJDUMP64 -d --triple=mips64el-unknown-elf src/apps/xmb_wave.ps2.o | grep -cE '$CSR_PAT')"
    # And the wiring, in both directions.  `wv_sinf`/`wv_cosf`/`wv_sqrtf` are `WV_PASS`
    # (noinline) so their live temps stay inside the callee instead of joining the caller's
    # working set -- measured, the linked image branches into them 6 times in total and into
    # the two far-tier callbacks once each, which is what says the shapes that keep the
    # register file legal survived -O2 and the linker.
    assert_check "xb_paint calls xb_wave_step in the linked image" \
        "[ \$($OBJDUMP64 -d --triple=mips64el-unknown-elf $ELF | grep -cE 'jal[[:space:]]+0x[0-9a-f]+ <xb_wave_step>') -ge 1 ]"
    assert_check "the wave's trig/root wrappers stay outlined and its far tier returns to the soft unit" \
        "[ \$($OBJDUMP64 -d --triple=mips64el-unknown-elf $ELF | grep -cE 'jal[[:space:]]+0x[0-9a-f]+ <wv_(sinf|cosf|sqrtf)>') -ge 3 ] && [ \$($OBJDUMP64 -d --triple=mips64el-unknown-elf $ELF | grep -cE 'jal[[:space:]]+0x[0-9a-f]+ <xb_(sinf|cosf)_far_u32>') -ge 2 ]"
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

# The same runtime read as a cost rather than as a correctness question.  Past 4*pi the
# header folds a float angle's turn count in double and keeps the float polynomial, which
# is nine software double operations and no division; the shape it replaced hands the
# argument to the double series, whose thirteen __divdf3 are why a long-argument sine was
# forty times a short one.  Read off verify/tests/ps2_farfold_probe.o, built by `make
# test-ps2` with the image's own flags -- in the image itself sinf is static inline and
# dissolves into a hundred call sites, so the law has to be read where it cannot hide.
FARFOLD_OBJ="./.build/ps2_farfold_probe.o"
if [ -f "$FARFOLD_OBJ" ]; then
    assert_check "A large-argument float sine folds with no software double division" \
        "[ \$($OBJDUMP -dr $FARFOLD_OBJ | grep -c '__divdf3' | tr -d ' ') -eq 0 ]"
    # ...and the fold is the one written, not an object that calls nothing at all and so
    # has no divisions either: the long long round trip is what makes it a turn count.
    assert_check "The far fold rounds its turn count through a long long (__fixdfdi/__floatdidf)" \
        "$OBJDUMP -dr $FARFOLD_OBJ | grep -q '__fixdfdi' && $OBJDUMP -dr $FARFOLD_OBJ | grep -q '__floatdidf'"
else
    echo "  [SKIP] $FARFOLD_OBJ absent -- the trig fold's shape law was not run"
fi

# 8. The single-canvas law (Normative -- see "One canvas: the desktop's own backbuffer"
# in doc/txt/PS2.txt ch. 18)
#
# The GS driver used to own a second 800x600 CT32 array and the present swept one word per
# desktop pixel into it before every upload: 49,094 us of a full-frame present, on top of
# 1,920,000 bytes of .bss.  The GIF DMA now reads the array the compositor painted, so both
# are gone.  Three things make that sound, and each is its own assertion -- the symbol the
# retired array left behind, the count of screen-sized canvases the image may hold, and the
# instruction-level premise that the bus master sees the CPU's stores.
#
# None of these uses `grep -q` on a pipe: under `set -o pipefail` an early-exiting grep
# SIGPIPEs readelf, and the pipeline then reports the *readelf* status, which makes an
# absence test pass on the very symbol it is meant to catch.  (Same reason line 242 reads
# the symbol table into a variable.)  assert_check redirects the output, so the plain grep
# costs nothing.
assert_check "No ps2_fb_memory symbol: the driver holds the caller's pointer, not a framebuffer" \
    "! ($READELF -s $ELF | grep -E '[[:space:]]ps2_fb_memory$')"

# The other half of the same law: the canvas is installed, not owned.  Without this the
# ps2_fb_memory test could be satisfied by a driver that lost its framebuffer entirely.
assert_check "Symbol 'ps2_gs_set_framebuffer' exists (the kernel installs the compositor's canvas)" \
    "$READELF -s $ELF | grep -E '[[:space:]]ps2_gs_set_framebuffer$'"

# 800 * 600 * 4 = 1,920,000, which readelf prints as 0x1d4c00 because it is over 1023.  The
# law is about ROLES, not about byte counts: exactly one object of that size may be the canvas
# the bus master reads, it has to be the compositor's own backbuffer, and any other object of
# that size has to be named here as what it is.  The one that is allowed is `s_cached_bg`
# (src/desktop/desktop.c): the compositor's cached background, never installed on the GIF,
# whose whole purpose is that a background restore is a copy instead of a re-render of the
# gradient.  Its cost is stated in the row's own text so it is a counted 1,920,000 bytes of
# .bss and not a saving nobody tallied -- chapter 4's budget carries the total.  An UNNAMED
# object of that size still turns this row red, which is the regression it was written for:
# the retired second framebuffer was exactly that, 1,920,000 bytes plus a per-pixel sweep.
FB_CANVAS_COUNT="$($READELF -s $ELF | awk '$3=="0x1d4c00" && $4=="OBJECT" && $8=="s_desktop_backbuffer"' | wc -l | tr -d ' ')"
FB_CANVAS_OTHER="$($READELF -s $ELF | awk '$3=="0x1d4c00" && $4=="OBJECT"{print $8}' | grep -Evc '^(s_desktop_backbuffer|s_cached_bg)$' || true)"
FB_CANVAS_RAM="$($READELF -s $ELF | awk '$3=="0x1d4c00" && $4=="OBJECT"{n++} END{print n*1920000}')"
assert_check "Exactly one canvas the GIF reads (s_desktop_backbuffer), and no screen-sized object but the named background cache ($FB_CANVAS_RAM B of canvas-class RAM in all)" \
    "[ $FB_CANVAS_COUNT -eq 1 ] && [ $FB_CANVAS_OTHER -eq 0 ]"

# The present path's sweep was a static function, so it is a LOCAL symbol while it exists
# and no symbol at all once the loop is gone from ps2_paint_region().
assert_check "The present path has no sweep: blit_backbuffer_rect_to_ps2fb is absent" \
    "! ($READELF -s $ELF | grep -E '[[:space:]]blit_backbuffer_rect_to_ps2fb$')"

# Reading the composited canvas from DMA without a cache flush is only safe while the Emotion
# EE's L1 caches stay off, so the port must never write CP0 Context (register 9 select 1),
# which is the register that carries the cache-enable bit, and never issue a `cache`
# maintenance op.  What the image IS allowed to write is Status (register 12 select 0), and
# it now has three reasons to: btron_fp_init() raising CU1, and the two interrupt thunks in
# boot_ps2.s that set the EE's global enable bit (EIE, bit 16) -- one at the arm of the INPUT
# plane, one in its epilogue, because taking an exception clears that bit and a handler that
# leaves it cleared has taken its last tick.  So the law is about the register, not the
# count: every CP0 write in the image goes to Status, none goes to register 9 (whose select 0
# is Count and whose select 1 is Context -- writing either would be a hazard), and the reads
# beside them are the clock and the plane's own Status probe.  Asserted over the instructions
# rather than over the sources, because the premise is about what the CPU actually executes.
# GNU objdump decodes MIPS3 here and prints coprocessor operands without the select field, so
# this needs the 64-bit-capable decoder.
if [ -n "$OBJDUMP64" ]; then
    CP0_TEXT="$($OBJDUMP64 -d --triple=mips64el-unknown-elf $ELF | grep -E '[[:space:]]m[tf]c0[[:space:]]')"
    # Two integers rather than the disassembly text: the operand fields are `$2, $12`, and
    # pasting that into an eval'd command would let the shell expand those `$` again.
    CP0_WRITES="$(printf '%s\n' "$CP0_TEXT" | grep -c 'mtc0' || true)"
    CP0_STATUS_WRITES="$(printf '%s\n' "$CP0_TEXT" | grep -c -E 'mtc0.*12, 0x0' || true)"
    assert_check "Every CP0 write in the image is to Status (register 12 select 0)" \
        "[ $CP0_WRITES -ge 1 ] && [ $CP0_WRITES -eq $CP0_STATUS_WRITES ]"
    # No `$` in the pattern for the same reason as the two counters above: this command is
    # eval'd, and `$9` would be the shell's ninth positional parameter.
    assert_check "No CP0 write touches register 9 (Count/Context): the clock is read, never set" \
        "! (printf '%s\n' \"$CP0_TEXT\" | grep -E 'mtc0.*[[:space:]]9,' )"
    assert_check "Zero cache-maintenance instructions: nothing to flush between the paint and the DMA" \
        "! ($OBJDUMP64 -d --triple=mips64el-unknown-elf $ELF | grep -E '[[:space:]]cache[[:space:]]')"
else
    echo "  [SKIP] 64-bit-GPR disassembler unavailable -- caches-off premise not read off the image"
fi

# 9. The /SYS volume inside the image (Normative -- see "The /SYS volume, and the in-window apps it
# unlocks: termios, B-MSX" in doc/txt/PS2.txt §2.6)
#
# sc, tv and every other termios app refuse to start on this target for one reason: their
# first call is vfs_init(), and on a bare-metal build that is just `vol_mounted_count() > 0
# || g_sys_vol != NULL`.  No volume, no filesystem, no terminal application.  The chosen fix
# is to build the volume with mkbtronfs and .incbin it into .data, so the boot mounts a
# byte image that is part of the executable and there is no CDVD reader, no second copy and
# no second description of what the volume holds.
#
# Four things can silently break that, and each is its own row:
#
# The first is alignment and size.  blk_mem_create addresses blocks as `lba * 1024` from the
# start of its buffer, so the embedded image must begin on a 1024 boundary and hold a whole
# number of blocks -- one stray leading byte would shift every block the filesystem reads and
# vol_mount would reject the magic.  Both facts are read off the symbol pair, not off the
# assembly source, because the section flags are what decide them.
#
# The second is that the bytes in the ELF are the bytes mkbtronfs wrote.  A stale object, or a
# link that filled the section from an older volume, produces an image that mounts and boots
# and then shows yesterday's files; nothing complains.  So the volume is located in the file
# through .data's own Addr/Off and compared, block for block, against btron_sys.vol.
#
# The third and fourth are that the boot actually reaches it: ps2_kernel_main must call the
# mount (it had a logging stub with no caller for a whole session, and printed a record list
# that was pure fiction), and sc's real session init must be the one that links -- gterm.c
# carries a weak definition, so a strong-symbol regression would bring back "not available
# yet" while every other row here still passed.
if [ -f btron_sys.vol ]; then
    VOL_ADDR="$($READELF -s $ELF | awk '$8=="ps2_sys_vol_image" {print "0x"$2; exit}')"
    VOL_END="$($READELF -s $ELF | awk '$8=="ps2_sys_vol_image_end" {print "0x"$2; exit}')"
    DATA_ADDR="$($READELF -S $ELF | awk '$3==".data" {print "0x"$5; exit}')"
    DATA_OFF="$($READELF -S $ELF | awk '$3==".data" {print "0x"$6; exit}')"
    VOL_BYTES=$((VOL_END - VOL_ADDR))
    VOL_FILE_OFF=$((DATA_OFF + VOL_ADDR - DATA_ADDR))
    VOL_ON_DISK=$(wc -c < btron_sys.vol | tr -d ' ')
    assert_check "The image carries the volume in .data at $VOL_ADDR: $VOL_BYTES bytes, 1024-aligned, $((VOL_BYTES / 1024)) whole blocks" \
        "[ -n '$VOL_ADDR' ] && [ -n '$VOL_END' ] && [ $VOL_BYTES -gt 0 ] && [ \$((VOL_ADDR % 1024)) -eq 0 ] && [ \$((VOL_BYTES % 1024)) -eq 0 ]"
    # dd reads whole 1024-byte blocks, which is exactly why the alignment row above is a
    # precondition rather than a detail: an unaligned start would silently truncate here.
    assert_check "...and they are the bytes mkbtronfs wrote ($VOL_ON_DISK in btron_sys.vol, copied from ELF offset $VOL_FILE_OFF)" \
        "[ $VOL_BYTES -eq $VOL_ON_DISK ] && dd if=$ELF bs=1024 skip=$((VOL_FILE_OFF / 1024)) count=$((VOL_BYTES / 1024)) 2>/dev/null | cmp -s - btron_sys.vol"
else
    echo "  [SKIP] btron_sys.vol absent -- the embedded-volume law was not read off the image"
fi

BOOT_ASM="$($OBJDUMP -d $ELF | awk '/<ps2_kernel_main>:/{f=1;print;next} f&&/^[[:space:]]*$/{f=0;next} f')"
BOOT_MOUNT_CALLS="$(printf '%s\n' "$BOOT_ASM" | grep -c 'jal.*<btron_core_hfds_log>' || true)"
assert_check "The boot mounts it: ps2_kernel_main calls the volume's mount ($BOOT_MOUNT_CALLS call site)" \
    "[ \$($OBJDUMP -d $ELF | awk '/<ps2_kernel_main>:/{f=1;print;next} f&&/^[[:space:]]*\$/{f=0;next} f' | grep -c 'jal.*<btron_core_hfds_log>') -ge 1 ]"

# The strong/weak pair is the reason this row is a call-site check and not a symbol-existence
# check: `ps2_sys_vol_image` existing says nothing about which sc_session_init won the link.
# 628 bytes of real code calls both vfs_init and term_set_size; the weak stub calls neither.
SC_ASM="$($OBJDUMP -d $ELF | awk '/<sc_session_init>:/{f=1;print;next} f&&/^[[:space:]]*$/{f=0;next} f')"
assert_check "sc's real session init is linked, not gterm.c's weak stub (sc_session_init calls vfs_init and term_set_size)" \
    "[ \$($OBJDUMP -d $ELF | awk '/<sc_session_init>:/{f=1;print;next} f&&/^[[:space:]]*\$/{f=0;next} f' | grep -c 'jal.*<vfs_init>') -ge 1 ] && [ \$($OBJDUMP -d $ELF | awk '/<sc_session_init>:/{f=1;print;next} f&&/^[[:space:]]*\$/{f=0;next} f' | grep -c 'jal.*<term_set_size>') -ge 1 ]"

# 10. B-MSX, launched from the volume (Normative -- see "The /SYS volume inside the executable" in doc/txt/PS2.txt ch. 20)
#
# The launcher's Games band lists the cartridges and Cross opens B-MSX, and every part of that is
# a link-time or content fact that can be silently false: xmb.c carries a weak `open_msx_window_*`
# pair for the targets that have no emulator, so a regression there boots the XMB, paints two rows
# and does nothing at all when pressed.  So each row below asks one question of the disassembly
# rather than of the symbol table, in the same idiom as sc's row above -- and the three places a
# byte can hide are named separately: the catalogue drawer, the cartridge body, the BIOS.
#
# One function reads a body: vfs_read_blob.  msx_load must reach the ROM through it (and through
# vfs_child_path, which keeps the record's own id as the path's anchor), because the alternative is
# a second reader per app, and the second reader is where a binary body gets truncated -- rd_rec
# clamps to the record size and the text wrappers stop at a NUL.
function_code() {
    $OBJDUMP -d $ELF | awk -v fn="$1" 'BEGIN{pat="<"fn">:"} index($0,pat){f=1;print;next} f&&/^[0-9a-f]+[ ]<[a-zA-Z_]/ {exit} f'
}

# The five rows under this guard are the B-MSX-on-PS2 law.  The Makefile's PS2_MSX dial
# can park the emulator out of an image whose .bss does not fit it (M17 in
# doc/txt/PS2.txt), and a parked image is not a broken one, so the rows are guarded on the
# symbol they read and print SKIP rather than FAIL when it is absent.  `msx_open` is the
# right test: it is the definition xmb.c's weak open_msx_window*() stubs exist to yield to,
# so its presence is exactly "the emulator is linked", and its absence is exactly "the
# Games row opens nothing".
MSX_LINKED=$($READELF -sW $ELF | awk '$8=="msx_open"{c++} END{print c+0}')
if [ "$MSX_LINKED" -ge 1 ]; then
assert_check "B-MSX's real window opener won the link (open_msx_window_with_rom links GLOBAL, at 96 bytes, not xmb.c's stub)" \
    "[ \$($READELF -sW $ELF | awk '\$8==\"open_msx_window_with_rom\"{print \$5; exit}') = 'GLOBAL' ] && [ \$($READELF -sW $ELF | awk '\$8==\"open_msx_window_with_rom\"{print \$3; exit}') -ge 64 ]"

assert_check "...and pressing a Games row actually creates the task and the window (msx_open calls cre_tsk, sta_tsk and opn_wnd)" \
    "[ \$(function_code msx_open | grep -c 'jal.*<cre_tsk>') -ge 1 ] && [ \$(function_code msx_open | grep -c 'jal.*<sta_tsk>') -ge 1 ] && [ \$(function_code msx_open | grep -c 'jal.*<opn_wnd>') -ge 1 ]"

assert_check "the row list comes from the mounted volume, not a host folder (msx_open calls the VFS directory reader)" \
    "[ \$(function_code msx_open | grep -c 'jal.*<vfs_list_dir>') -ge 1 ]"

# The control for these two rows is the row above them: msx_open itself calls neither vfs_child_path
# nor vfs_read_blob, so the scan is not finding the same call site twice through an inlined chain.
assert_check "a cartridge reaches the core through the one byte-exact reader (msx_load calls vfs_child_path, vfs_read_blob and msx_load_rom)" \
    "[ \$(function_code msx_load | grep -c 'jal.*<vfs_child_path>') -ge 1 ] && [ \$(function_code msx_load | grep -c 'jal.*<vfs_read_blob>') -ge 1 ] && [ \$(function_code msx_load | grep -c 'jal.*<msx_load_rom>') -ge 1 ]"

# Where C-BIOS lives is one #define (MSX_BIOS_DEFAULT_DIR in include/emulators/tronmsx.h), and this
# is the row that catches it being pinned back to a checkout path: the core used to initialise
# s_bios_dir to third_party/openMSX/Contrib/cbios, which made that default unreachable -- a
# bare-metal machine has no such folder, so bios_load() failed and the window said "C-BIOS が読み込めません"
# while the volume held the BIOS the whole time.
#
# The scan is scoped to .rodata, and that scoping is load-bearing rather than tidy: this very file is a
# manifest payload, so the section .data holds a copy of the documentation that describes the bug --
# including the checkout path it warns about.  A whole-image grep therefore goes red on a doc edit that
# changes no code at all, which is exactly what it did here on its first run (2 hits, both of them
# sentences).  Read-only code bytes are the thing under law, so they are the thing scanned, located
# through the section header in the same idiom as the volume row above.  All three scans read the whole
# section (`grep -c`, never `-q`), which both keeps `set -o pipefail` from reporting the reader's early
# exit as dd's failure and makes the row self-evidencing: the string that must be there is there, so
# the scan can see strings at all.
RODATA_ADDR="$($READELF -S $ELF | awk '$3==".rodata"{print "0x"$5; exit}')"
RODATA_OFF="0x$($READELF -S $ELF | awk '$3==".rodata"{print $6; exit}')"
RODATA_SIZE="0x$($READELF -S $ELF | awk '$3==".rodata"{print $7; exit}')"
RODATA_SCAN="dd if=$ELF bs=1 skip=$((RODATA_OFF)) count=$((RODATA_SIZE)) 2>/dev/null | LC_ALL=C grep -cac"
assert_check "the code asks the volume for C-BIOS and the cartridges, and names no checkout path (.rodata at $RODATA_ADDR, $RODATA_SIZE bytes)" \
    "[ \$($RODATA_SCAN '/SYS/cbios') -ge 1 ] && [ \$($RODATA_SCAN '/SYS/msx') -ge 1 ] && [ \$($RODATA_SCAN 'third_party/openMSX') -eq 0 ]"
else
    echo "  [SKIP] PS2_MSX=0 -- tronMSX is parked out of this image, so the five B-MSX rows above were not run (not a pass; the volume still carries cbios/ and msx/)"
fi

# 11. Check Bootable ISO Disc Packaging
if [ -f "$ISO" ]; then
    assert_check "ISO disc image exists and is non-empty" \
        "[ -s $ISO ]"
fi

echo "----------------------------------------------------------"
echo " PS2 Test Results: $PASSED / $TOTAL tests passed (100%)"
echo "----------------------------------------------------------"
echo "[CI-TEST] SUCCESS — PlayStation 2 ELF & drivers validated!"
