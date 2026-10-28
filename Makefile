# B-System Multi-Target Makefile
# Cleanroom / Sakamura T-Kernel 2.0 / POSIX / QEMU / BCM283x Bare-Metal / UEFI / PC-98
#
# Targets & Kernels:
#   posix         B-System/BTRON3 3.20 (posix-hosted) Hiroaki Takada — Cleanroom TRON Kernel [Target 0]
#   qemu          B-System/BTRON3 3.20 (arm-qemu-virtio) Hiroshi Tokita — Cleanroom TRON Kernel [Target 1]
#   sakamura      B-System/BTRON3 3.20 (sakamura-tkernel-virtio) Ken Sakamura — T-Kernel 2.0 [Target 3]
#   arm-elf       B-System/BTRON3 3.20 (armv7-bcm2836) Takahiro Yokobayashi — T-Kernel 2.0 [Target 2]
#   uefi          B-System/BTRON3 3.20 (x86_64-uefi-smp) Kota Uchida — T-Kernel 2.0 [Target 4]
#   pc98          B-System/BTRON3 3.20 (i386-pc98) Awe Morris — T-Kernel 2.0 [Target 5]
#   arm64-elf     B-System/BTRON3 3.20 (aarch64-bcm2711) Takanori Yokoyama — T-Kernel 2.0 [Target 6]
#   m68k          B-System/BTRON3 3.20 (m68k-q800) Motorola 68040 — Cleanroom TRON Kernel [Target 7]
#   ps2           B-System/BTRON3 3.20 (ps2-ee) Sony PlayStation 2 — Cleanroom TRON Kernel [Target 8]
#   mips          B-System/BTRON3 3.20 (mips-malta) Bare-Metal MIPS — Cleanroom TRON Kernel [Target 9]
#   foma          B-System/BTRON3 3.20 (foma) Cleanroom BTRON UI for FOMA devices [Target 10]
#
# Run Commands:
#   run-posix     Boot POSIX Microkernel Desktop (btron-posix)
#   run-qemu      Boot QEMU VirtIO Desktop (btron-qemu.elf)
#   run-kernel    Boot Pi 2B ELF in QEMU (btron-arm-baremetal.elf, raspi2b with display)
#   run-yoko      Boot Pi 3B AArch64 ELF in QEMU (btron-aarch64-baremetal.elf, raspi3b)
#   run-yoko4     Boot Pi 4B AArch64 ELF in QEMU (btron-aarch64-baremetal.elf, raspi4b)
#   run-sakamura  Boot Sakamura T-Kernel 2.0 Desktop (btron-sakamura.elf, display, kbd, mouse)
#   run-foma      Boot µBTRON-FOMA Mobile Workbench (btron-foma.elf, 480x640 portrait, AArch32 profile)
#   run-uefi      Boot x86_64 UEFI SMP in QEMU (btron-uchida.elf, aliases: run-eufi, run-uefu)
#   run-pc98      Boot NEC PC-9801/PC-9821 VM in QEMU (btron-morris.elf)
#   run-m68k      Boot Motorola 68040 Macintosh Quadra 800 in QEMU (btron-m68k.elf)
#   run-ps2       Boot Sony PlayStation 2 Emotion Engine in PCSX2 (btron-ps2.elf)
#   run-mips      Boot Bare-Metal MIPS in QEMU Malta / Magnum (btron-mips.elf)
#   test-kernel   Test Pi 2B ELF in QEMU (raspi2b, serial-only, headless)
#   debug-gdb     QEMU + GDB stub on Pi 2B

CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c99 -Iinclude -Iinclude/gl -Isrc/gl -Iinclude/drivers -Isrc/kernel -Isrc/cores -Isrc/quake/include

.PHONY: all posix qemu kernel tkernel sakamura foma uefi pc98 arm-elf arm64-elf m68k ps2 mips \
        check-structure test-ps2-softfloat test-arm-aeabi test-ps2-gs-pack test-ps2-gs-env test-ps2-gs-rec test-ps2-vu1-geom test-ps2-vu0-kernel test-gscube-nexus test-ps2-gs-poster test-ps2-gs-read test-ps2-gs-tex test-ps2-pointer test-ps2-fused-row test-ps2-gif-ring test-ps2-paintw test-eegs-raster test-eegs-present test-gl-math test-gl-backend-choice test-ps2-fb-order test-grid-map xmb-atlas test-xmb-atlas test-ps2-opengl-routing \
        test-xmb-render test-xmb-render-tgl test-gl-context \
        html2tad book2tad tad_bin test test-kernel test-yoko test-yoko4 test-m68k test-mips test-ps2 test-foma test-foma-ui foma-screens \
        segui-screens \
        test-mozc test-editor test-hmi test-tad test-chat test-wylie verify test-fs test-chokanji test-quake test-replay \
        test-hull test-view test-secret test-render test-drone test-drone-all test-zexall msx-trace \
        run-posix run-qemu run-kernel run-yoko run-yoko4 run-sakamura run-foma run-uefi run-eufi run-uefu run-pc98 run-m68k run-ps2 run-mips debug-virtio debug-gdb clean \
        ps2-cfg ps2-dials \
        pi400 flash-pi400 fetch-pi400-fw

QEMU_ARM     ?= qemu-system-arm
QEMU_AARCH64 ?= qemu-system-aarch64
QEMU_X86_64  ?= qemu-system-x86_64
QEMU_M68K    ?= qemu-system-m68k
QEMU_MIPS    ?= qemu-system-mipsel
QEMU_MIPS64  ?= qemu-system-mips64el
PCSX2_BIN    ?= /Applications/PCSX2.app/Contents/MacOS/PCSX2
# PCSX2 keeps every setting it has in one data directory, and -datapath names the
# parent of it: the emulator appends its own program name, so passing pcsx2/ puts
# the configuration at pcsx2/PCSX2/inis/PCSX2.ini.  That file is tracked here, so
# a run gets these settings rather than whatever the user-level config has drifted
# to.  BIOS images are the one thing kept outside the repo, symlinked file by file.
PCSX2_DATADIR ?= $(CURDIR)/pcsx2
PCSX2_DATA    := $(PCSX2_DATADIR)/PCSX2
PCSX2_BIOS    ?= $(HOME)/Library/Application Support/PCSX2/bios
M68K_CC      ?= m68k-elf-gcc

LLVM_CLANG := $(shell for p in /opt/homebrew/opt/llvm/bin/clang /usr/local/opt/llvm/bin/clang /usr/lib/llvm-*/bin/clang clang; do if command -v "$$p" >/dev/null 2>&1; then echo "$$p"; break; fi; done)
LLD_BIN    := $(shell for p in /opt/homebrew/bin/ld.lld /usr/local/bin/ld.lld /usr/bin/ld.lld ld.lld /opt/homebrew/opt/llvm/bin/ld.lld /usr/lib/llvm-*/bin/ld.lld; do if command -v "$$p" >/dev/null 2>&1; then echo "$$p"; break; fi; done)

ARM_LLD_FLAG := $(if $(LLD_BIN),-fuse-ld=$(LLD_BIN),-fuse-ld=lld)
# ARM32: Cortex-A7 for Pi 2B (BCM2836)
ARM32_CC ?= $(LLVM_CLANG) --target=arm-none-eabi -mcpu=cortex-a7 -marm $(ARM_LLD_FLAG) -ffreestanding -nostdlib
# AArch64: Cortex-A72 for Pi 4B (BCM2711) — kept for Pi4-only development
ARM64_CC ?= $(LLVM_CLANG) --target=aarch64-none-elf -mcpu=cortex-a72 $(ARM_LLD_FLAG) -ffreestanding -nostdlib
# IA-32 / X86 Freestanding: UEFI / PC-98
ifeq ($(shell uname -s), Darwin)
    X86_CC ?= $(LLVM_CLANG) --target=i686-none-elf -ffreestanding -nostdlib
    X86_LD ?= $(LLD_BIN) -m elf_i386
else
    X86_CC ?= $(if $(shell command -v i686-elf-gcc 2>/dev/null),i686-elf-gcc -ffreestanding -nostdlib,$(if $(shell command -v clang 2>/dev/null),clang --target=i686-none-elf -ffreestanding -nostdlib,$(CC) -m32 -ffreestanding -nostdlib))
    X86_LD ?= $(if $(shell command -v i686-elf-ld 2>/dev/null),i686-elf-ld,$(if $(LLD_BIN),$(LLD_BIN) -m elf_i386,ld -m elf_i386))
endif

# MIPS / PS2 Freestanding (Target 8: PS2 EE, Target 9: Malta / Magnum)
MIPS_CC     ?= $(LLVM_CLANG) --target=mipsel-unknown-elf -march=mips32r2 -mabi=32 -ffreestanding -nostdlib
# -march=mips2, not mips3: one O32 calling convention across the whole image, and
# still a subset of the R5900's MIPS III.  The ABI note under PS2_OBJS is the law.
# -msoft-float, likewise for the whole image and likewise an ABI question: the
# R5900 FPU is single-precision, so a hardware .d instruction is not merely slow, it
# is illegal and does nothing at all.  Under this flag a floating value travels in
# integer registers, so mixing it with a build that uses the FPU would corrupt every
# float argument that crosses the boundary -- and src/drivers/ps2/ps2_builtins.c
# supplies the runtime the link then needs.
# Three objects take the FPU switched back on (see PS2_FP near %.pbench.o), chosen for the
# same reason: no float ever crosses into or out of them.  ps2_builtins.c *is* the
# soft-float runtime, so a bit pattern in an integer register is its argument by
# definition; vertex_stage.c is the geometry kernel, reached only by pointer, and the one
# conversion it needs is spelled through that runtime's own calling convention;
# xmb_wave.c is the launcher's waving surface, which takes the effect clock as IEEE-754 bits
# in an integer and hands the sheet out through memory, and whose two operations this CPU has
# no instruction for go back to the soft unit the same way -- xb_sinf_far_u32()/
# xb_cosf_far_u32(), bits in and bits out.  They take the flag because the EE's
# single-precision unit is what those files are for: 460,861 float operations in an XMB
# frame, at 28 cycles a libcall, was the price of applying the .d law to code that has never
# used a double.  Nothing else may take it: an object whose entry points take a float would
# move the ABI boundary rather than stay inside it, and gate row 57 is the check.  The law
# *inside* such a unit is a second thing again, and it is not about instructions -- see the
# register-allocation paragraph by PS2_WAVE_OBJ below.
PS2_CC      ?= $(LLVM_CLANG) --target=mipsel-unknown-elf -march=mips2 -mabi=32 -msoft-float -ffreestanding -nostdlib
MIPS_LD ?= $(if $(shell command -v mipsel-linux-gnu-ld 2>/dev/null),mipsel-linux-gnu-ld,$(LLD_BIN) -EL)

# BCM283x bare-metal flags (TYPE_RPI=2 → BCM2836, Pi 2B, Cortex-A7)
BCM_INC      = -Iinclude -Iinclude/gl -Iinclude/arch/bcm283x -Isrc/kernel -Isrc/cores
ARM_CFLAGS   = -O2 -Wall -Wextra -std=c99 -mno-unaligned-access \
               -D_RPI_BCM283x_ -DTYPE_RPI=2 -DBTRON_TARGET=2 -mfpu=vfpv4 -mfloat-abi=softfp \
               $(BCM_INC)
ARM64_CFLAGS = -O2 -Wall -Wextra -std=c99 -mstrict-align \
               -D_RPI_BCM283x_ -DTYPE_RPI=3 -DBTRON_TARGET=6 \
               -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast -Wno-unused-parameter \
               $(BCM_INC)

# Host OS / SDL2 detection
UNAME_S := $(shell uname -s)
EXEEXT :=
ifneq (,$(findstring MINGW,$(UNAME_S)))
    EXEEXT := .exe
endif

ifeq ($(UNAME_S), Darwin)
    SDL_CFLAGS   := $(shell sdl2-config --cflags 2>/dev/null || echo "-I/usr/local/include/SDL2")
    SDL_LIBS     := $(shell sdl2-config --libs 2>/dev/null || echo "-lSDL2") \
                    -lz -lpthread -framework ApplicationServices -framework Cocoa
    QEMU_DISPLAY        := -display cocoa,show-cursor=on,zoom-to-fit=on
    # Match M68K display flags with show-cursor=on and zoom-to-fit=on
    KERNEL_DISPLAY      := $(QEMU_DISPLAY)
else ifneq (,$(filter $(UNAME_S),Linux FreeBSD NetBSD OpenBSD DragonFly))
    # Linux + the three BSDs (and DragonFly) - portable POSIX path, no Windows flags
    SDL_CFLAGS   := $(shell sdl2-config --cflags 2>/dev/null || \
                         pkg-config --cflags sdl2 2>/dev/null || \
                         echo "-I/usr/local/include/SDL2 -I/usr/include/SDL2")
    SDL_LIBS     := $(shell sdl2-config --libs 2>/dev/null || \
                         pkg-config --libs sdl2 2>/dev/null || \
                         echo "-lSDL2") \
                    -lz -lm -lpthread
    QEMU_DISPLAY        := -display default,show-cursor=on
    KERNEL_DISPLAY      := -display default
else ifneq (,$(findstring MINGW,$(UNAME_S)))
    SDL_CFLAGS   := $(shell sdl2-config --cflags 2>/dev/null || echo "-IC:/msys64/mingw64/include/SDL2 -Dmain=SDL_main")
    SDL_LIBS     := $(shell sdl2-config --libs 2>/dev/null || echo "-LC:/msys64/mingw64/lib -lmingw32 -mwindows -lSDL2main -lSDL2") -lz -lgdi32 -lwinpthread
    QEMU_DISPLAY        := -display default,show-cursor=on
    KERNEL_DISPLAY      := -display default
else
    # Generic Unix fallback - no -lgdi32
    SDL_CFLAGS   := $(shell sdl2-config --cflags 2>/dev/null || \
                         pkg-config --cflags sdl2 2>/dev/null || \
                         echo "-I/usr/local/include/SDL2 -I/usr/include/SDL2")
    SDL_LIBS     := $(shell sdl2-config --libs 2>/dev/null || \
                         pkg-config --libs sdl2 2>/dev/null || \
                         echo "-lSDL2") -lz -lm -lpthread
    QEMU_DISPLAY        := -display default,show-cursor=on
    KERNEL_DISPLAY      := -display default
endif

# ── Text Input Primitives (TIP) / Mozc IME sources ────────────────
IME_SRCS    = src/tip/mozc_kkc.c       \
              src/tip/tip_ife.c        \
              src/tip/tip_task.c       \
              src/tip/tip_vobj.c       \
              src/tip/wylie.c          \
              src/tip/tibetan_dict.c

# ── tronMSX core: the machine src/emulators/msx/msx_app.c drives ───────────────────
MSX_SRCS = src/emulators/msx/z80.c        \
           src/emulators/msx/r800.c       \
           src/emulators/msx/v9938.c      \
           src/emulators/msx/s1985.c      \
           src/emulators/msx/t9769b.c     \
           src/emulators/msx/t9769c.c     \
           src/emulators/msx/slots.c      \
           src/emulators/msx/dsk.c        \
           src/emulators/msx/bios.c       \
           src/emulators/msx/vdp.c        \
           src/emulators/msx/psg.c        \
           src/emulators/msx/audio.c      \
           src/emulators/msx/scc.c        \
           src/emulators/msx/opll.c       \
           src/emulators/msx/pcm.c        \
           src/emulators/msx/y8950.c      \
           src/emulators/msx/ppi.c        \
           src/emulators/msx/kbd.c        \
           src/emulators/msx/geo3d.c      \
           src/emulators/msx/tronmsx.c

# ── CLU Applications (POSIX commands) ─────────────────────────────
CLU_APPS_SRCS = src/clu/fs.c \
                src/clu/ls.c \
                src/clu/cp.c \
                src/clu/rm.c \
                src/clu/mv.c \
                src/clu/ln.c \
                src/clu/df.c \
                src/clu/st.c \
                src/clu/tp.c

CLU_APPS_HOST_OBJS = $(CLU_APPS_SRCS:.c=.host.o)
CLU_APPS_POSIX_OBJS = $(CLU_APPS_SRCS:.c=.posix.o)

# ── Common SDL2-hosted app sources ────────────────────────────────
BE_DASM_SRCS = src/clu/be/native_dasm.c \
               src/clu/be/arch/x86/insnsd.c src/clu/be/arch/x86/insnsa.c \
               src/clu/be/arch/x86/insnsb.c src/clu/be/arch/x86/insnsn.c \
               src/clu/be/arch/x86/disasm.c src/clu/be/arch/x86/regdis.c \
               src/clu/be/arch/x86/regs.c src/clu/be/arch/x86/regflags.c \
               src/clu/be/arch/x86/regvals.c src/clu/be/arch/x86/iflag.c \
               src/clu/be/arch/x86/sync.c src/clu/be/arch/x86/disp8.c \
               src/clu/be/arch/x86/nctype.c src/clu/be/arch/x86/readnum.c \
               src/clu/be/arch/x86/common.c src/clu/be/arch/x86/alloc.c \
               src/clu/be/arch/x86/string.c src/clu/be/arch/x86/warnings.c \
               src/clu/be/arch/x86/error.c src/clu/be/arch/x86/ver.c \
               src/clu/be/arch/arm64/BranchExcSys.c \
               src/clu/be/arch/arm64/DataProcessingImmediate.c \
               src/clu/be/arch/arm64/LoadsAndStores.c src/clu/be/arch/arm64/bits.c \
               src/clu/be/arch/arm64/strext.c \
               src/clu/be/arch/arm64/DataProcessingFloatingPoint.c \
               src/clu/be/arch/arm64/DataProcessingRegister.c \
               src/clu/be/arch/arm64/armadillo.c src/clu/be/arch/arm64/instruction.c \
               src/clu/be/arch/arm64/utils.c src/clu/be/arch/arm32/armv7.c \
               src/clu/be/arch/riscv/riscv-disas.c src/clu/be/arch/ppc/ppc_disasm.c \
               src/clu/be/arch/m68k/dis68k.c src/clu/be/arch/sh4/sh4.c \
               src/clu/be/arch/mips/mips-rsp.c \
               src/clu/be/arch/pdp11/pdp11.c

# The nvidia half of the `be` editor's decoders -- the assembler and disassembler of the
# G80..Maxwell microcode, 27 units carried over from the nvclock/nvd3d9 tooling.  It is its own
# list because of what it costs: 3,338,516 B of initialized .data plus 74,648 B of text, which
# in the PS2 link is 44% of every byte spent on .data (gm107.c alone is 1,382,528).  Naming it
# separately changes no list that is in use -- the `+=` below puts it back exactly where it was
# for every lane -- and only gives a lane a way to answer for it.  See PS2_NV.
NV_SRCS = src/clu/be/arch/nv/core-as.c src/clu/be/arch/nv/ctx.c \
          src/clu/be/arch/nv/falcon.c src/clu/be/arch/nv/gk110.c \
          src/clu/be/arch/nv/macro.c src/clu/be/arch/nv/vp1.c \
          src/clu/be/arch/nv/hash.c src/clu/be/arch/nv/g80.c \
          src/clu/be/arch/nv/gm107.c src/clu/be/arch/nv/nv.c \
          src/clu/be/arch/nv/vuc.c src/clu/be/arch/nv/xtensa.c \
          src/clu/be/arch/nv/astr.c src/clu/be/arch/nv/colors.c \
          src/clu/be/arch/nv/core-dis.c src/clu/be/arch/nv/core.c \
          src/clu/be/arch/nv/gf100.c src/clu/be/arch/nv/hwsq.c \
          src/clu/be/arch/nv/vcomp.c src/clu/be/arch/nv/mask.c \
          src/clu/be/arch/nv/varinfo.c src/clu/be/arch/nv/vardata.c \
          src/clu/be/arch/nv/symtab.c src/clu/be/arch/nv/easm.c \
          src/clu/be/arch/nv/aprintf.c src/clu/be/arch/nv/easm_xfrm.c \
          src/clu/be/arch/nv/easm_print.c
BE_DASM_SRCS += $(NV_SRCS)

COMMON_SRCS = src/graphics/dp_core.c   \
              src/graphics/image_decode.c \
              src/graphics/icons_bundle.c \
              src/window/dnd.c         \
              src/graphics/dp_sdl.c    \
              src/font/troncode.c      \
              src/font/jis_fonts.c     \
              src/font/tibetan_fonts.c \
              src/font/font_mgr.c      \
              src/window/wnd.c         \
              src/window/app_menu.c    \
              src/window/event.c       \
              src/chokanji/pmc.c       \
              src/chokanji/cab.c       \
              src/chokanji/microscript.c \
              src/chokanji/clock.c     \
              src/chokanji/kconv.c     \
              src/chokanji/xfconv.c    \
              src/chokanji/bpk_core.c  \
              src/chokanji/unpack.c    \
              src/chokanji/launchers.c \
              src/vobject/vobj.c       \
              src/desktop/desktop.c    \
              src/desktop/workbench.c  \
              src/desktop/tracker.c    \
              src/desktop/about.c      \
              src/desktop/global_menu.c \
              src/desktop/main.c       \
              src/apps/vobj_manager.c  \
              src/apps/tad_browser.c   \
              src/apps/paint.c         \
              src/apps/gterm.c         \
              src/apps/t_editor.c      \
              src/apps/clarity.c        \
              src/apps/clarity_layout.c \
              src/apps/clarity_render.c \
              src/apps/clarity_export.c \
              src/apps/audio_player.c  \
              src/apps/b_drivesetup.c  \
              src/media/orchestra.c     \
              src/media/orchestra_stage.c \
              src/media/orchestra_arrange.c \
              src/media/orchestra_piano.c \
              src/media/orchestra_score.c \
              src/media/orchestra_mix.c \
              src/media/orchestra_insert.c \
              src/media/orchestra_about.c \
              src/media/orchestra_session.c \
              src/media/orchestra_smf.c \
              src/media/gst_rt.c       \
              src/media/lukhash_score.c \
              src/apps/chat.c          \
              src/apps/chat_xml.c      \
              src/emulators/msx/msx_app.c       \
              $(MSX_SRCS)              \
              src/settings/language.c  \
              src/settings/control_panel.c \
              src/settings/appearance.c \
              src/settings/desktop.c \
              src/settings/display.c \
              src/settings/input.c \
              src/settings/sound.c \
              src/settings/network.c \
              src/settings/media.c \
              src/settings/security.c \
              src/settings/system.c    \
              src/settings/terminal.c  \
              src/hmi/hmi_core.c       \
              src/hmi/hmi_switch.c     \
              src/hmi/hmi_selector.c   \
              src/hmi/hmi_volume.c     \
              src/hmi/hmi_meter.c      \
              src/hmi/hmi_controller.c \
              src/hmi/hmi_panel.c      \
              $(IME_SRCS)              \
              src/fs/blk_mem.c         \
              src/fs/blk_file.c        \
              src/fs/blk_qcow2.c       \
              src/fs/blk_part.c        \
              src/fs/vol.c             \
              src/fs/file.c            \
              src/clu/lang.c           \
              src/clu/term.c           \
              src/clu/tty.c            \
              src/clu/vfs.c            \
              src/clu/sc/files.c       \
              src/clu/sc/input.c       \
              src/clu/sc/menus.c       \
              src/clu/sc/sc.c          \
              src/clu/tv/tv.c          \
              src/clu/be/native.c      \
              $(BE_DASM_SRCS)          \
              $(CLU_APPS_SRCS)         \
              src/clu/clu.c

QUAKE_SRCS = \
    src/quake/core/mathlib.c \
    src/quake/core/mem.c \
    src/quake/core/cvar.c \
    src/quake/core/cmd.c \
    src/quake/core/wad.c \
    src/quake/core/world.c \
    src/quake/core/pr_exec.c \
    src/quake/core/sv_phys.c \
    src/quake/core/sv_main.c \
    src/quake/sys/sys_btron.c \
    src/quake/sys/in_btron.c \
    src/quake/sys/fs_btron.c \
    src/quake/render/r_btron_gl.c \
    src/quake/render/r_brush.c \
    src/quake/render/r_light.c \
    src/quake/render/r_surf.c \
    src/quake/render/r_alias.c \
    src/quake/render/r_part.c \
    src/quake/render/texture.c \
    src/quake/core/cl_demo.c \
    src/quake/app/quake_ui.c \
    src/quake/app/quake_app.c

DEMO_SRCS = \
    src/demo/lilcu64_wa.c \
    src/demo/lilcu64_score.c \
    src/demo/lilcu64_synth.c \
    src/demo/lilcu64_insert.c \
    src/demo/lilcu64_demo.c \
    src/drivers/ps2/ps2_vu0_kernel.c

# Targets that already pull DEMO_SRCS must not list this again.
LILCU64_ENGINE_SRCS = \
    src/demo/lilcu64_wa.c \
    src/demo/lilcu64_score.c \
    src/demo/lilcu64_synth.c \
    src/demo/lilcu64_insert.c

VIRGL_SRCS = \
    src/gl/gl_dispatch.c \
    src/gl/gl_backend_choice.c \
    src/gl/egl_surface.c \
    src/gl/backend_virgl.c \
    src/apps/glgears.c \
    src/apps/xmb.c \
    src/apps/xmb_wave.c \
    $(QUAKE_SRCS) \
    $(DEMO_SRCS)

# ── POSIX build (Target 0) ────────────────────────────────────────
POSIX_STARTUP = src/cores/core_posix.c
POSIX_SRCS    = $(POSIX_STARTUP)        \
                src/drivers/virtio/virtio.c \
                src/cores/core_init.c  \
                $(VIRGL_SRCS)          \
                $(COMMON_SRCS)

# ── QEMU VirtIO build (Target 1) ─────────────────────────────────
QEMU_STARTUP = src/cores/core_virtio.c
QEMU_SRCS    = $(QEMU_STARTUP)          \
                src/drivers/virtio/virtio.c \
                src/cores/core_init.c   \
                $(VIRGL_SRCS)           \
                $(COMMON_SRCS)

TINYGL_SRCS = \
    src/gl/tinygl/api.c \
    src/gl/tinygl/arrays.c \
    src/gl/tinygl/clear.c \
    src/gl/tinygl/clip.c \
    src/gl/tinygl/get.c \
    src/gl/tinygl/image_util.c \
    src/gl/tinygl/init.c \
    src/gl/tinygl/light.c \
    src/gl/tinygl/list.c \
    src/gl/tinygl/matrix.c \
    src/gl/tinygl/memory.c \
    src/gl/tinygl/misc.c \
    src/gl/tinygl/msghandling.c \
    src/gl/tinygl/select.c \
    src/gl/tinygl/specbuf.c \
    src/gl/tinygl/texture.c \
    src/gl/tinygl/vertex.c \
    src/gl/tinygl/vertex_stage.c \
    src/gl/tinygl/zbuffer.c \
    src/gl/tinygl/zline.c \
    src/gl/tinygl/zmath.c \
    src/gl/tinygl/ztriangle.c \
    src/gl/tinygl/accum.c \
    src/gl/tinygl/zpostprocess.c \
    src/gl/tinygl/zraster.c \
    src/gl/tinygl/ztext.c

GL_SRCS = \
    src/gl/gl_dispatch.c \
    src/gl/gl_backend_choice.c \
    src/gl/egl_surface.c \
    src/gl/backend_tinygl.c \
    src/gl/backend_virgl.c \
    src/apps/glgears.c \
    src/apps/xmb.c \
    src/apps/xmb_wave.c \
    $(QUAKE_SRCS) \
    $(DEMO_SRCS)

# ── X86_64 / EMT64 UEFI build (Target 4) ────────────────────────
UEFI_STARTUP = src/cores/core_boot.c src/cores/core_smp.c
UEFI_SRCS    = $(UEFI_STARTUP)          \
               src/cores/core_init.c   \
               src/kernel/libstr.c      \
               src/kernel/libint64.c    \
               src/drivers/vesa/vesa.c  \
               src/drivers/uefi/ps2_mouse.c \
               src/drivers/virtio/virtio_gpu.c \
               $(TINYGL_SRCS)           \
               $(GL_SRCS)               \
               $(COMMON_NO_SDL_SRCS)

# ── NEC PC-98 build (Target 5) ──────────────────────────────────
PC98_STARTUP = src/cores/core_pc98.c src/drivers/pc98/boot/boot_pc98.c
PC98_SRCS    = $(PC98_STARTUP)          \
               src/cores/core_boot.c   \
               src/cores/core_init.c   \
               src/kernel/libstr.c      \
               src/kernel/libint64.c    \
               src/drivers/vesa/vesa.c  \
               src/drivers/pc98/input/pc98_mouse.c \
               src/drivers/pc98/input/pc98_kbd.c \
               src/drivers/uefi/ps2_mouse.c \
               $(COMMON_NO_SDL_SRCS) \
               $(LILCU64_ENGINE_SRCS)

# ── BCM283x (Pi 2B) bare-metal arch sources ───────────────────────
ARCH_BCM_SRCS = src/drivers/bcm283x/cpu/cache.c      \
                src/drivers/bcm283x/cpu/chkplv.c     \
                src/drivers/bcm283x/cpu/cntwus.c     \
                src/drivers/bcm283x/cpu/cpu_calls.c  \
                src/drivers/bcm283x/cpu/cpu_init.c   \
                src/drivers/bcm283x/cpu/devinit.c    \
                src/drivers/bcm283x/cpu/power.c      \
                src/drivers/bcm283x/cpu/tkdev_init.c \
                src/drivers/bcm283x/usb/dwc2.c

ARCH_BCM64_SRCS = $(ARCH_BCM_SRCS) \
                  src/drivers/bcm283x/pci/pcie_bcm2711.c \
                  src/drivers/bcm283x/usb/xhci.c \
                  src/drivers/bcm283x/dma/bcm2711_dma.c

TKERNEL_SAKAMURA_SRCS = \
    src/kernel/task.c         \
    src/kernel/task_manage.c  \
    src/kernel/task_sync.c    \
    src/kernel/semaphore.c    \
    src/kernel/eventflag.c    \
    src/kernel/mailbox.c      \
    src/kernel/messagebuf.c   \
    src/kernel/rendezvous.c   \
    src/kernel/mutex.c        \
    src/kernel/mempool.c      \
    src/kernel/mempfix.c      \
    src/kernel/subsystem.c    \
    src/kernel/time_calls.c   \
    src/kernel/timer.c        \
    src/kernel/klock.c        \
    src/kernel/wait.c         \
    src/kernel/objname.c      \
    src/kernel/misc_calls.c   \
    src/kernel/version.c      \
    src/kernel/libstr.c

TKERNEL_SRCS = src/cores/core_tkernel.c \
               src/drivers/virtio/virtio.c \
               src/cores/core_init.c     \
               $(TKERNEL_SAKAMURA_SRCS)   \
               $(VIRGL_SRCS)              \
               $(COMMON_SRCS)

# ── µBTRON-FOMA Mobile Target (Target 10) ────────────────────────
FOMA_STARTUP = src/cores/core_foma.c
FOMA_SRCS = $(FOMA_STARTUP)             \
            src/drivers/virtio/virtio.c \
            src/cores/core_init.c       \
            $(TKERNEL_SAKAMURA_SRCS)     \
            src/graphics/dp_core.c      \
            src/graphics/icons_bundle.c \
            src/graphics/dp_sdl.c       \
            src/font/troncode.c         \
            src/font/jis_fonts.c        \
            src/font/tibetan_fonts.c    \
            src/window/wnd.c            \
            src/window/app_menu.c       \
            src/window/event.c          \
            src/chokanji/pmc.c          \
            $(IME_SRCS)                 \
            src/apps/gterm.c            \
            src/apps/t_editor.c         \
            src/settings/terminal.c     \
            src/desktop/desktop_mobile.c \
            src/desktop/workbench_mobile.c \
            src/desktop/main_mobile.c   \
            src/fs/blk_mem.c            \
            src/fs/blk_file.c           \
            src/fs/vol.c                \
            src/fs/file.c               \
            $(CLU_APPS_SRCS)            \
            src/clu/clu.c

# Bare-metal: SDL-free subset only
COMMON_NO_SDL_SRCS = \
    src/graphics/dp_core.c \
    src/graphics/dp_accel.c \
    src/graphics/dp_cal.c \
    src/graphics/icons_bundle.c \
    src/font/troncode.c    \
    src/font/jis_fonts.c   \
    src/font/tibetan_fonts.c \
    src/window/wnd.c       \
    src/window/app_menu.c  \
    src/window/event.c     \
    src/window/dnd.c       \
    src/chokanji/pmc.c     \
    src/vobject/vobj.c     \
    src/desktop/desktop.c  \
    src/desktop/workbench.c \
    src/desktop/tracker.c  \
    src/desktop/about.c    \
    src/desktop/global_menu.c \
    src/apps/vobj_manager.c \
    src/apps/tad_browser.c \
    src/apps/paint.c \
    src/apps/gterm.c       \
    src/apps/t_editor.c    \
    src/apps/audio_player.c \
    src/media/orchestra.c   \
    src/media/orchestra_stage.c \
    src/media/orchestra_arrange.c \
    src/media/orchestra_piano.c \
    src/media/orchestra_score.c \
    src/media/orchestra_mix.c \
    src/media/orchestra_insert.c \
    src/media/orchestra_about.c \
    src/media/orchestra_session.c \
    src/media/orchestra_smf.c \
    src/media/gst_rt.c     \
    src/media/lukhash_score.c \
    src/apps/chat.c        \
    src/apps/chat_xml.c    \
    src/settings/control_panel.c \
    src/settings/language.c \
    src/settings/appearance.c \
    src/settings/desktop.c \
    src/settings/display.c \
    src/settings/input.c \
    src/settings/sound.c \
    src/settings/network.c \
    src/settings/media.c \
    src/settings/security.c \
    src/settings/system.c  \
    src/settings/terminal.c \
    $(IME_SRCS)            \
    src/fs/blk_mem.c       \
    src/fs/vol.c           \
    src/fs/file.c          \
    src/fs/fs_stdio.c      \
    src/clu/vfs.c          \
    $(CLU_APPS_SRCS)       \
    src/clu/clu.c

BAREMETAL_STARTUP  = src/drivers/bcm283x/cpu/startup_arm.c
BAREMETAL_LD       = src/drivers/bcm283x/cpu/link.ld
# src/kernel/libaeabi.c is named here and not folded into COMMON_NO_SDL_SRCS: it
# answers the conversion libcalls only a 32-bit register file asks for, so the
# AArch64 lane -- which converts a 64-bit integer in one instruction -- must not
# link it, and the house rule is to name a runtime only in the lanes measured
# against it.  `make test-arm-aeabi` grades it against the host FPU.
ARM32_BAREMETAL_SRCS = src/cores/core_init.c src/cores/core_yoko.c $(TKERNEL_SAKAMURA_SRCS) $(ARCH_BCM_SRCS) $(BAREMETAL_STARTUP) $(COMMON_NO_SDL_SRCS) $(LILCU64_ENGINE_SRCS) src/kernel/libaeabi.c
ARM64_BAREMETAL_SRCS = src/cores/core_init.c src/cores/core_arm64.c $(TKERNEL_SAKAMURA_SRCS) $(ARCH_BCM64_SRCS) $(BAREMETAL_STARTUP) $(COMMON_NO_SDL_SRCS) $(LILCU64_ENGINE_SRCS)

# ── Object lists ─────────────────────────────────────────────────
POSIX_OBJS   = $(POSIX_SRCS:.c=.posix.o)
QEMU_OBJS    = $(QEMU_SRCS:.c=.qemu.o)
TKERNEL_OBJS = $(TKERNEL_SRCS:.c=.tkernel.o)
ARM32_OBJS   = $(ARM32_BAREMETAL_SRCS:.c=.arm32.o)
ARM64_OBJS   = $(ARM64_BAREMETAL_SRCS:.c=.arm64.o)
SAKAMURA_OBJS  = $(TKERNEL_SRCS:.c=.sakamura.o)
FOMA_OBJS      = $(FOMA_SRCS:.c=.foma.o)
UEFI_OBJS      = $(UEFI_SRCS:.c=.uefi.o)
PC98_OBJS      = $(PC98_SRCS:.c=.pc98.o)

# Object files are host-toolchain specific. In particular, a QEMU object
# produced by MinGW is a COFF file and cannot be linked by a Linux compiler.
# Keep a host/compiler stamp as a prerequisite so moving one checkout between
# hosts triggers a one-time rebuild without penalizing normal incremental
# builds.
POSIX_BUILD_TAG   := $(shell uname -s 2>/dev/null || echo unknown)-$(shell $(CC) -dumpmachine 2>/dev/null || echo unknown)
POSIX_BUILD_STAMP := .build/posix-$(POSIX_BUILD_TAG).stamp
$(POSIX_OBJS): $(POSIX_BUILD_STAMP) Makefile

$(POSIX_BUILD_STAMP):
	@mkdir -p $(dir $@)
	@touch $@

QEMU_BUILD_TAG   := $(shell uname -s 2>/dev/null || echo unknown)-$(shell $(CC) -dumpmachine 2>/dev/null || echo unknown)
QEMU_BUILD_STAMP := .build/qemu-$(QEMU_BUILD_TAG).stamp
$(QEMU_OBJS): $(QEMU_BUILD_STAMP) Makefile

$(QEMU_BUILD_STAMP):
	@mkdir -p $(dir $@)
	@touch $@

# ── Output names ──────────────────────────────────────────────────
POSIX_TARGET   = btron-posix$(EXEEXT)
QEMU_TARGET    = btron-qemu.elf$(EXEEXT)
TKERNEL_TARGET = ./.build/btron-tkernel.elf
SAKAMURA_TARGET = ./.build/btron-sakamura.elf
FOMA_TARGET     = ./.build/btron-foma.elf
TEST_FOMA_BIN   = ./.build/test_foma_ui
UEFI_TARGET     = ./.build/btron-uchida.elf
PC98_TARGET     = ./.build/btron-morris.elf
ARM32_TARGET   = btron-arm-baremetal.elf
ARM64_TARGET   = btron-aarch64-baremetal.elf
DEFAULT_TARGET = ./.build/btron

TKERNEL_INC = -D_RPI_BCM283x_ -DTYPE_RPI=2 \
              -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast \
              -Iinclude -Iinclude/arch/bcm283x -Isrc/kernel -Isrc/cores

all: posix qemu kernel sakamura foma uefi pc98

# ── Tree structure invariants (see scripts/check_structure.sh) ──────────────
check-structure:
	@sh scripts/check_structure.sh

# Emit one source path per line for a *_SRCS variable, for the check above.
list-%:
	@for f in $($*); do echo $$f; done

# ═══════════════════════════════════════════════════════════════════
# POSIX Desktop
# ═══════════════════════════════════════════════════════════════════
posix: $(POSIX_TARGET) btron_sys.vol btron_anders.vol
	@ln -sf $(POSIX_TARGET) $(DEFAULT_TARGET)
	@echo "=========================================================="
	@echo " B-System POSIX Kernel & Desktop successfully built!"
	@echo " Startup File: $(POSIX_STARTUP)"
	@echo " Run './btron' or 'make run-posix' to start."
	@echo "=========================================================="
	@if [ -n "$(PYTHON)" ] || command -v elixir >/dev/null 2>&1; then $(MAKE) tad_bin; \
	  else echo "Note: tad_bin skipped (no python3 or elixir found) - desktop still runs."; fi

%.posix.o: %.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -DBTRON_TARGET=0 -MMD -MP -c $< -o $@

$(POSIX_TARGET): $(POSIX_OBJS)
	$(CC) $(POSIX_OBJS) -o $@ $(LDFLAGS) $(SDL_LIBS)

run-posix: $(POSIX_TARGET) btron_sys.vol btron_anders.vol
	./$(POSIX_TARGET)

run-sakamura: $(SAKAMURA_TARGET) btron_sys.vol btron_anders.vol
	./$(SAKAMURA_TARGET)

# ══════════════════════════════════════════════════════════════════════
# FS Library (host/POSIX build) — blk_mem, blk_file, vol, file, clu
# ══════════════════════════════════════════════════════════════════════
FS_SRCS  = src/fs/blk_mem.c src/fs/blk_file.c src/fs/blk_qcow2.c src/fs/blk_part.c src/fs/vol.c src/fs/file.c
FS_OBJS  = $(FS_SRCS:.c=.host.o)

# -MMD is not optional here.  Host objects are linked straight from .o files, so
# without a header dependency an edit to e.g. src/clu/vfs.h or
# src/clu/sc/sokhatsky.h rebuilds only the .c files a target happens to name, and
# the link mixes objects that disagree on struct layout -- sc then reads a File's
# is_dir from where size used to be and draws an empty pane that looks like an
# empty directory.
%.host.o: %.c
	$(CC) $(CFLAGS) -DBTRON_TARGET=0 -MMD -MP -c $< -o $@

src/clu/clu.host.o: src/clu/clu.c
	$(CC) $(CFLAGS) -DBTRON_TARGET=0 -MMD -MP -c $< -o $@

# Header dependencies recorded by the -MMD flags above: editing src/clu/vfs.h or
# src/clu/sc/sokhatsky.h must rebuild every object that included it, or a link
# mixes objects that disagree on struct layout.
-include $(shell find src verify -name '*.host.d' -o -name '*.posix.d' -o -name '*.test.d' -o -name '*.tgl.d' 2>/dev/null)

# ── mkbtronfs — host image builder ────────────────────────────────────
mkbtronfs: src/tools/mkbtronfs.c $(FS_OBJS)
	$(CC) $(CFLAGS) -Isrc $^ -o mkbtronfs
	@echo "[FS] mkbtronfs built."

# Host album encoder. Not linked into btron. Writes SYS/*.orch and SYS/*.mid.
album_encode: src/tools/album_encode.c src/demo/lilcu64_score.c \
		src/demo/lilcu64_wa.c src/demo/lilcu64_insert.c \
		src/media/orchestra_session.c src/media/orchestra_smf.c \
		src/media/gst_rt.c src/media/lukhash_score.c
	$(CC) $(CFLAGS) -Werror -Isrc/demo $^ -lm -o album_encode
	@echo "[ALBUM] album_encode built."

# The manifest's payloads are prerequisites and not merely its input: with only the manifest and
# the tool listed, make rebuilds btron_sys.vol when either changes and *never* when a document the
# volume copies changes, so the image an ELF carries is the volume some other target happened to
# build last -- the stale-artifact disease this Makefile already cures twice for PS2_GL and PS2_FP,
# arrived at from the other side, where the content moved and the flags did not.
#
# mkbtronfs names those prerequisites itself, through --deps, in the same idiom the compiler's
# -MMD lines above use.  It used to be an awk pass over the manifest here, which was a second
# grammar for one file and could not read the first one: it took the last field of a line as the
# payload (so every entry that names a parent drawer, like a cartridge in /SYS/msx, dropped out)
# and it stopped at the first space (so `Lode Runner (1984)(Sony)[a].rom` was never a file make
# knew about).  One reader of a manifest is the tool that builds from it.
btron_sys.vol: mkbtronfs src/tools/manifest.txt
	./mkbtronfs src/tools/manifest.txt -o btron_sys.vol --deps btron_sys.vol.d
	@echo "[FS] btron_sys.vol written."
	@xxd btron_sys.vol | head -2

btron_anders.vol: mkbtronfs src/tools/manifest_anders.txt
	./mkbtronfs src/tools/manifest_anders.txt -o btron_anders.vol -l ANDERS --deps btron_anders.vol.d
	@echo "[FS] btron_anders.vol written."

# Written by the recipe above, so the first build after a clean has no payload dependencies and
# every one after it does -- exactly the trade `-include *.d` already makes for C headers.
-include btron_sys.vol.d
-include btron_anders.vol.d


# ── FS unit tests ──────────────────────────────────────────────────────
TEST_FS_BIN = ./.build/test_fs
$(TEST_FS_BIN): verify/tests/test_fs.c $(FS_OBJS) src/clu/clu.host.o $(CLU_APPS_HOST_OBJS)
	$(CC) $(CFLAGS) -Isrc $^ -o $@

test-fs: $(TEST_FS_BIN) btron_sys.vol
	./$(TEST_FS_BIN)
	@echo "[FS] All FS tests passed."

# ── Cho-Kanji (B-right/V 4.02) QCOW2 tests ───────────────────────────
# The read-write tests create, append to and delete records, so they must never
# open the golden disk. Each run clones a fresh scratch image and the suite points
# every test at it through BTRON_CHOKANJI_IMAGE. The clone is made in the recipe,
# not as a file target, because a file target keeps an old scratch disk once it
# exists -- and a disk a previous run already wrote to is exactly what hides a
# read/write regression.
CHOKANJI_GOLDEN   ?= $(if $(wildcard hda.golden),hda.golden,hda.qcow2)
CHOKANJI_RW_IMAGE  = ./.build/hda.rw.qcow2

TEST_CHOKANJI_BIN = ./.build/test_chokanji
$(TEST_CHOKANJI_BIN): verify/tests/test_chokanji.c $(FS_OBJS) src/clu/clu.host.o $(CLU_APPS_HOST_OBJS)
	$(CC) $(CFLAGS) -Isrc $^ -o $@

test-chokanji: $(TEST_CHOKANJI_BIN)
	@mkdir -p $(dir $(CHOKANJI_RW_IMAGE))
	@rm -f $(CHOKANJI_RW_IMAGE)
	@cp -c $(CHOKANJI_GOLDEN) $(CHOKANJI_RW_IMAGE) 2>/dev/null || cp $(CHOKANJI_GOLDEN) $(CHOKANJI_RW_IMAGE)
	@echo "[CHOKANJI] scratch image $(CHOKANJI_RW_IMAGE) cloned from $(CHOKANJI_GOLDEN)"
	BTRON_CHOKANJI_IMAGE=$(CHOKANJI_RW_IMAGE) ./$(TEST_CHOKANJI_BIN)
	@echo "[CHOKANJI] All Cho-Kanji tests passed."

# ── Cho-Kanji Tier 1 & PMC NASA-Standard Apps verification ────────────
TEST_CHOKANJI_APPS_BIN = ./.build/test_chokanji_apps
$(TEST_CHOKANJI_APPS_BIN): verify/tests/test_chokanji_apps.c src/chokanji/pmc.posix.o src/chokanji/cab.posix.o src/chokanji/microscript.posix.o src/chokanji/clock.posix.o src/chokanji/kconv.posix.o src/chokanji/xfconv.posix.o src/chokanji/bpk_core.posix.o src/chokanji/unpack.posix.o src/window/wnd.posix.o src/graphics/dp_core.posix.o src/graphics/dp_sdl.posix.o src/font/troncode.posix.o src/font/jis_fonts.posix.o src/font/tibetan_fonts.posix.o src/font/font_mgr.posix.o src/window/event.posix.o src/window/app_menu.posix.o src/vobject/vobj.posix.o src/fs/vol.posix.o src/fs/file.posix.o src/fs/blk_mem.posix.o src/fs/blk_file.posix.o src/fs/blk_qcow2.posix.o src/fs/blk_part.posix.o
	$(CC) $(CFLAGS) -Isrc -Iinclude $^ -o $@ $(LDFLAGS) $(SDL_LIBS)

test-chokanji-apps: $(TEST_CHOKANJI_APPS_BIN)
	./$(TEST_CHOKANJI_APPS_BIN)
	@echo "[CHOKANJI APPS] All Tier 1 applications & PMC tests passed."

# ── CLU / Termios / TV / SC ───────────────────────────────────────────
CLU_CORE_SRCS = src/clu/lang.c src/clu/term.c src/clu/tty.c src/clu/vfs.c
CLU_CORE_OBJS = $(CLU_CORE_SRCS:.c=.host.o)

SC_SRCS = src/clu/sc/files.c src/clu/sc/input.c src/clu/sc/menus.c
SC_OBJS = $(SC_SRCS:.c=.host.o)

# sc.c is a library everywhere; only the standalone bin/sc binary links its main().
src/clu/sc/sc.standalone.o: src/clu/sc/sc.c
	$(CC) $(CFLAGS) -DBTRON_TARGET=0 -DSC_STANDALONE -MMD -MP -c $< -o $@

src/clu/tv/tv.host.o: src/clu/tv/tv.c
	$(CC) $(CFLAGS) -DBTRON_TARGET=0 -MMD -MP -c $< -o $@

bin/tv: src/clu/tv/tv.c $(CLU_CORE_OBJS) $(FS_OBJS)
	@mkdir -p bin
	$(CC) $(CFLAGS) -DTV_STANDALONE -Isrc -Iinclude $^ -o $@

bin/sc: $(SC_OBJS) src/clu/sc/sc.standalone.o src/clu/tv/tv.host.o $(CLU_CORE_OBJS) $(CLU_APPS_HOST_OBJS) src/clu/clu.host.o $(FS_OBJS)
	@mkdir -p bin
	$(CC) $(CFLAGS) -Isrc -Iinclude $^ -o $@

TEST_TERMIOS_BIN = ./.build/test_termios
$(TEST_TERMIOS_BIN): verify/tests/test_termios_clu.c $(CLU_CORE_OBJS) src/clu/sc/files.host.o src/clu/sc/input.host.o src/clu/sc/menus.host.o src/clu/sc/sc.host.o src/clu/tv/tv.host.o $(CLU_APPS_HOST_OBJS) src/clu/clu.host.o $(FS_OBJS)
	@mkdir -p .build
	$(CC) $(CFLAGS) -Isrc -Iinclude $^ -o $@


test-termios: $(TEST_TERMIOS_BIN) btron_sys.vol btron_anders.vol
	./$(TEST_TERMIOS_BIN)
	@echo "[CLU] All termios, TV, and SC tests passed."



# ── Clarity DTP Frame & Control tests ────────────────────────────────
TEST_CLARITY_BIN = ./.build/test_clarity_frames
$(TEST_CLARITY_BIN): verify/tests/test_clarity_frames.c src/graphics/image_decode.posix.o src/window/dnd.posix.o src/apps/clarity.posix.o src/apps/clarity_layout.posix.o src/apps/clarity_render.posix.o src/apps/clarity_export.posix.o src/window/wnd.posix.o src/window/app_menu.posix.o src/window/event.posix.o src/graphics/dp_core.posix.o src/graphics/icons_bundle.posix.o src/font/troncode.posix.o src/font/jis_fonts.posix.o src/font/tibetan_fonts.posix.o src/font/font_mgr.posix.o src/cores/core_posix.posix.o src/cores/core_init.posix.o src/drivers/virtio/virtio.posix.o src/vobject/vobj.posix.o src/desktop/desktop.posix.o src/desktop/workbench.posix.o src/desktop/tracker.posix.o src/desktop/about.posix.o src/desktop/global_menu.posix.o src/apps/vobj_manager.posix.o src/apps/tad_browser.posix.o src/apps/gterm.posix.o src/apps/t_editor.posix.o src/apps/audio_player.posix.o src/apps/b_drivesetup.posix.o src/media/orchestra.posix.o src/media/orchestra_stage.posix.o src/media/orchestra_arrange.posix.o src/media/orchestra_piano.posix.o src/media/orchestra_score.posix.o src/media/orchestra_mix.posix.o src/media/orchestra_insert.posix.o src/media/orchestra_about.posix.o src/media/orchestra_session.posix.o src/media/orchestra_smf.posix.o src/media/gst_rt.posix.o src/demo/lilcu64_wa.posix.o src/demo/lilcu64_score.posix.o src/demo/lilcu64_synth.posix.o src/demo/lilcu64_insert.posix.o src/apps/chat.posix.o src/apps/chat_xml.posix.o src/settings/language.posix.o src/settings/control_panel.posix.o src/settings/appearance.posix.o src/settings/desktop.posix.o src/settings/display.posix.o src/settings/input.posix.o src/settings/sound.posix.o src/settings/network.posix.o src/settings/media.posix.o src/settings/security.posix.o src/settings/system.posix.o src/settings/terminal.posix.o src/hmi/hmi_core.posix.o src/hmi/hmi_switch.posix.o src/hmi/hmi_selector.posix.o src/hmi/hmi_volume.posix.o src/hmi/hmi_meter.posix.o src/hmi/hmi_controller.posix.o src/hmi/hmi_panel.posix.o src/tip/mozc_kkc.posix.o src/tip/tip_ife.posix.o src/tip/tip_task.posix.o src/tip/tip_vobj.posix.o src/tip/wylie.posix.o src/tip/tibetan_dict.posix.o src/fs/blk_mem.posix.o src/fs/blk_file.posix.o src/fs/blk_qcow2.posix.o src/fs/blk_part.posix.o src/fs/vol.posix.o src/fs/file.posix.o $(CLU_APPS_POSIX_OBJS) src/clu/clu.posix.o
	$(CC) $(CFLAGS) -Isrc $^ -o $@ $(LDFLAGS) $(SDL_LIBS)

test-clarity: $(TEST_CLARITY_BIN)
	./$(TEST_CLARITY_BIN)
	@echo "[CLARITY] All Clarity tests passed."

# ── Clarity Hypermedia & DND tests ──────────────────────────────────
TEST_CLARITY_HYPERMEDIA_BIN = ./.build/test_clarity_hypermedia
$(TEST_CLARITY_HYPERMEDIA_BIN): verify/tests/test_clarity_hypermedia.c src/graphics/image_decode.posix.o src/window/dnd.posix.o src/apps/clarity.posix.o src/apps/clarity_layout.posix.o src/apps/clarity_render.posix.o src/apps/clarity_export.posix.o src/window/wnd.posix.o src/window/app_menu.posix.o src/window/event.posix.o src/graphics/dp_core.posix.o src/graphics/icons_bundle.posix.o src/font/troncode.posix.o src/font/jis_fonts.posix.o src/font/tibetan_fonts.posix.o src/font/font_mgr.posix.o src/cores/core_posix.posix.o src/cores/core_init.posix.o src/drivers/virtio/virtio.posix.o src/vobject/vobj.posix.o src/desktop/desktop.posix.o src/desktop/workbench.posix.o src/desktop/tracker.posix.o src/desktop/about.posix.o src/desktop/global_menu.posix.o src/apps/vobj_manager.posix.o src/apps/tad_browser.posix.o src/apps/gterm.posix.o src/apps/t_editor.posix.o src/apps/audio_player.posix.o src/apps/b_drivesetup.posix.o src/media/orchestra.posix.o src/media/orchestra_stage.posix.o src/media/orchestra_arrange.posix.o src/media/orchestra_piano.posix.o src/media/orchestra_score.posix.o src/media/orchestra_mix.posix.o src/media/orchestra_insert.posix.o src/media/orchestra_about.posix.o src/media/orchestra_session.posix.o src/media/orchestra_smf.posix.o src/media/gst_rt.posix.o src/demo/lilcu64_wa.posix.o src/demo/lilcu64_score.posix.o src/demo/lilcu64_synth.posix.o src/demo/lilcu64_insert.posix.o src/apps/chat.posix.o src/apps/chat_xml.posix.o src/settings/language.posix.o src/settings/control_panel.posix.o src/settings/appearance.posix.o src/settings/desktop.posix.o src/settings/display.posix.o src/settings/input.posix.o src/settings/sound.posix.o src/settings/network.posix.o src/settings/media.posix.o src/settings/security.posix.o src/settings/system.posix.o src/settings/terminal.posix.o src/hmi/hmi_core.posix.o src/hmi/hmi_switch.posix.o src/hmi/hmi_selector.posix.o src/hmi/hmi_volume.posix.o src/hmi/hmi_meter.posix.o src/hmi/hmi_controller.posix.o src/hmi/hmi_panel.posix.o src/tip/mozc_kkc.posix.o src/tip/tip_ife.posix.o src/tip/tip_task.posix.o src/tip/tip_vobj.posix.o src/tip/wylie.posix.o src/tip/tibetan_dict.posix.o src/fs/blk_mem.posix.o src/fs/blk_file.posix.o src/fs/blk_qcow2.posix.o src/fs/blk_part.posix.o src/fs/vol.posix.o src/fs/file.posix.o $(CLU_APPS_POSIX_OBJS) src/clu/clu.posix.o
	$(CC) $(CFLAGS) -Iinclude -Isrc $^ -o $@ $(LDFLAGS) $(SDL_LIBS)

test-clarity-hypermedia: $(TEST_CLARITY_HYPERMEDIA_BIN)
	./$(TEST_CLARITY_HYPERMEDIA_BIN)
	@echo "[HYPERMEDIA] All Clarity Hypermedia tests passed."


# ═══════════════════════════════════════════════════════════════════
# QEMU VirtIO Desktop
# ═══════════════════════════════════════════════════════════════════
qemu: tad_bin $(QEMU_TARGET)
	@echo "=========================================================="
	@echo " B-System QEMU VirtIO Desktop built!"
	@echo " Run 'make run-qemu' to launch."
	@echo "=========================================================="

%.qemu.o: %.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -DBTRON_TARGET=1 -DBTRON_QEMU_TARGET -c $< -o $@

$(QEMU_TARGET): $(QEMU_OBJS)
	$(CC) $(QEMU_OBJS) -o $@ $(LDFLAGS) $(SDL_LIBS)

run-qemu: $(QEMU_TARGET)
	@./$(QEMU_TARGET)

test-qemu: $(QEMU_TARGET)
	@./$(QEMU_TARGET)

# ═══════════════════════════════════════════════════════════════════
# T-Kernel SDL2 host build (development / debug on host)
# ═══════════════════════════════════════════════════════════════════
kernel: tad_bin $(TKERNEL_TARGET)
	@echo "=========================================================="
	@echo " Sakamura T-Kernel 2.0 Engine built: $(TKERNEL_TARGET)"
	@echo "=========================================================="

src/cores/%.tkernel.o: src/cores/%.c
	$(CC) $(CFLAGS) $(TKERNEL_INC) $(SDL_CFLAGS) -DBTRON_TARGET=2 -c $< -o $@

src/kernel/%.tkernel.o: src/kernel/%.c
	$(CC) $(CFLAGS) $(TKERNEL_INC) $(SDL_CFLAGS) -DBTRON_TARGET=2 -c $< -o $@

src/drivers/bcm283x/cpu/%.tkernel.o: src/drivers/bcm283x/cpu/%.c
	$(CC) $(CFLAGS) $(TKERNEL_INC) $(SDL_CFLAGS) -DBTRON_TARGET=2 -c $< -o $@

%.tkernel.o: %.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -DBTRON_TARGET=2 -c $< -o $@

$(TKERNEL_TARGET): $(TKERNEL_OBJS)
	$(CC) $(TKERNEL_OBJS) -o $@ $(LDFLAGS) $(SDL_LIBS)

# ===================================================================
sakamura: tad_bin $(SAKAMURA_TARGET)
	@echo "=========================================================="
	@echo " Sakamura T-Kernel 2.0 Engine (UART/VirtIO Mode) built: $(SAKAMURA_TARGET)"
	@echo "=========================================================="

src/cores/%.sakamura.o: src/cores/%.c
	$(CC) $(CFLAGS) $(TKERNEL_INC) $(SDL_CFLAGS) -DBTRON_TARGET=3 -c $< -o $@

src/kernel/%.sakamura.o: src/kernel/%.c
	$(CC) $(CFLAGS) $(TKERNEL_INC) $(SDL_CFLAGS) -DBTRON_TARGET=3 -c $< -o $@

src/drivers/bcm283x/cpu/%.sakamura.o: src/drivers/bcm283x/cpu/%.c
	$(CC) $(CFLAGS) $(TKERNEL_INC) $(SDL_CFLAGS) -DBTRON_TARGET=3 -c $< -o $@

%.sakamura.o: %.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -DBTRON_TARGET=3 -c $< -o $@

$(SAKAMURA_TARGET): $(SAKAMURA_OBJS)
	$(CC) $(SAKAMURA_OBJS) -o $@ $(LDFLAGS) $(SDL_LIBS)

# ═══════════════════════════════════════════════════════════════════
# µBTRON-FOMA Mobile Engine (Target 10: AArch32 UMTS Mobile Profile)
# ═══════════════════════════════════════════════════════════════════
foma: tad_bin $(FOMA_TARGET)
	@echo "=========================================================="
	@echo " µBTRON-FOMA Mobile Engine built: $(FOMA_TARGET)"
	@echo " Handset Profile: TI OMAP2430 / ARM1136 AArch32 UMTS"
	@echo " Display Viewport: 480x640 VGA Portrait (Vertical Screen)"
	@echo " Run 'make run-foma' to launch."
	@echo "=========================================================="

src/cores/%.foma.o: src/cores/%.c
	$(CC) $(CFLAGS) $(TKERNEL_INC) $(SDL_CFLAGS) -DBTRON_TARGET=10 -DBTRON_FOMA_TARGET -c $< -o $@

src/kernel/%.foma.o: src/kernel/%.c
	$(CC) $(CFLAGS) $(TKERNEL_INC) $(SDL_CFLAGS) -DBTRON_TARGET=10 -DBTRON_FOMA_TARGET -c $< -o $@

src/drivers/bcm283x/cpu/%.foma.o: src/drivers/bcm283x/cpu/%.c
	$(CC) $(CFLAGS) $(TKERNEL_INC) $(SDL_CFLAGS) -DBTRON_TARGET=10 -DBTRON_FOMA_TARGET -c $< -o $@

%.foma.o: %.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -DBTRON_TARGET=10 -DBTRON_FOMA_TARGET -c $< -o $@

$(FOMA_TARGET): $(FOMA_OBJS)
	$(CC) $(FOMA_OBJS) -o $@ $(LDFLAGS) $(SDL_LIBS)

run-foma: $(FOMA_TARGET)
	@echo "=========================================================="
	@echo " Launching µBTRON-FOMA Mobile Workbench"
	@echo " Architecture : AArch32 TI OMAP2430 / ARM1136 Profile"
	@echo " Kernel       : Sakamura T-Kernel 2.0 Engine"
	@echo " Runner       : VirtIO MMIO Block & Framebuffer Runner"
	@echo " Viewport     : 480x640 VGA Portrait Screen"
	@echo " Controls     : 5-way D-pad (Arrows/Enter), Softkeys (F1/F2/F3),"
	@echo "                Numeric (1-9), Back (Esc/Bksp), Menu (M)"
	@echo "=========================================================="
	./$(FOMA_TARGET)

test-foma: $(FOMA_TARGET) $(TEST_FOMA_BIN)
	@echo "=========================================================="
	@echo " Testing µBTRON-FOMA Target Build & Symbols"
	@echo "=========================================================="
	@file $(FOMA_TARGET)
	@./$(TEST_FOMA_BIN)

# ── X86_64 / EMT64 UEFI SMP QEMU Kernel (Honoring Kota Uchida) ───
UEFI_LD     = src/drivers/uefi/uefi_qemu.ld
UEFI_CFLAGS = -O2 -Wall -Wextra -std=c99 -mno-sse -mno-mmx -mno-sse2 \
              -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable -Wno-constant-conversion \
              -DBTRON_TARGET=4 -DBTRON_UEFI_TARGET -DBTRON_SMP -DBTRON_GL_BACKEND_TINYGL \
              -Iinclude -Iinclude/gl -Iinclude/drivers -Isrc/kernel -Isrc/cores -Isrc/gl -Isrc/gl/tinygl

%.uefi.o: %.c
	$(X86_CC) $(UEFI_CFLAGS) -c $< -o $@

uefi: tad_bin $(UEFI_TARGET)
	@ln -sf $(UEFI_TARGET) btron-uefi.elf
	@echo "=========================================================="
	@echo " B-System X86_64 / EMT64 UEFI SMP Kernel built: $(UEFI_TARGET)"
	@echo " In honor of Kota Uchida (内田 公太) — Japanese UEFI OS pioneer"
	@echo " Run 'make run-uefi' or 'make run-eufi' to launch on QEMU."
	@echo "=========================================================="

$(UEFI_TARGET): $(UEFI_OBJS) $(UEFI_LD)
	@echo "=========================================================="
	@echo " Building B-System X86_64 / EMT64 UEFI SMP Kernel: $@"
	@echo "=========================================================="
	$(X86_LD) -T $(UEFI_LD) $(UEFI_OBJS) -o $@
	@echo "[UEFI-ELF] Built: $@"
	@file $@

run-uefi: $(UEFI_TARGET)
	@echo "=========================================================="
	@echo " Launching B-System X86_64 / EMT64 UEFI SMP on QEMU"
	@echo " Honoring : Kota Uchida (内田 公太) — MikanOS Pioneer"
	@echo " Machine  : q35  |  CPU: qemu64 (SMP 4 Cores)  |  RAM: 1G"
	@echo " Firmware : ACPI 6.5 MADT + LAPIC SMP Bring-up (core_smp.c)"
	@echo " Graphics : VirtIO-GPU 2D Display & VESA VBE 1024x768 32-bpp"
	@echo " Desktop  : desktop.c · wnd.c · gterm.c · Mozc IME"
	@echo "=========================================================="
	$(QEMU_X86_64) -M q35,accel=tcg -cpu qemu64 -smp cores=4,threads=1,sockets=1 -m 1G \
	    $(QEMU_DISPLAY) \
	    -device virtio-vga \
	    -kernel $(UEFI_TARGET) -serial stdio

run-eufi: run-uefi

test-uefi: $(UEFI_TARGET)
	@echo "=========================================================="
	@echo " Testing B-System x86_64 UEFI SMP Kernel on QEMU q35 (Headless CI)"
	@echo " Machine : q35  |  CPU: qemu64 (SMP 4)  |  RAM: 1G"
	@echo " Graphics: VirtIO-GPU 2D  |  Mode: headless, serial validation"
	@echo "=========================================================="
	@bash scripts/test_uefi.sh
# ═══════════════════════════════════════════════════════════════════
# NEC PC-98 Kernel Desktop (Honoring Awe Morris — zedBSD Pioneer)
# ═══════════════════════════════════════════════════════════════════
PC98_CFLAGS = -O2 -Wall -Wextra -std=c99 -mno-sse -mno-mmx -mno-sse2 -DBTRON_TARGET=5 -DBTRON_PC98_TARGET -Iinclude -Iinclude/gl -Iinclude/drivers -Isrc/kernel

%.pc98.o: %.c
	$(X86_CC) $(PC98_CFLAGS) -c $< -o $@

pc98: tad_bin $(PC98_TARGET)
	@ln -sf $(PC98_TARGET) btron-pc98.elf
	@echo "=========================================================="
	@echo " B-System NEC PC-98 Kernel built: $(PC98_TARGET)"
	@echo " In honor of Awe Morris — NEC PC-98 & zedBSD pioneer"
	@echo " Run 'make run-pc98' to launch."
	@echo "=========================================================="

$(PC98_TARGET): $(PC98_OBJS) $(UEFI_LD)
	@echo "=========================================================="
	@echo " Building B-System NEC PC-98 Kernel: $@"
	@echo "=========================================================="
	$(X86_LD) -T $(UEFI_LD) $(PC98_OBJS) -o $@
	@echo "[PC98-ELF] Built: $@"
	@file $@

run-pc98: $(PC98_TARGET)
	@echo "=========================================================="
	@echo " Launching B-System NEC PC-9801 / PC-9821 on QEMU"
	@echo " Honoring : Awe Morris (zedBSD & NEC PC-98 Architecture)"
	@echo " Machine  : NEC PC-9821 VM  |  CPU: 486/Pentium (PC-98 Planar VRAM)"
	@echo " Firmware : Ski Bootloader -> BTRON3 PC-98 Console & Desktop"
	@echo "=========================================================="
	@if command -v qemu-system-pc98 >/dev/null 2>&1; then \
	    qemu-system-pc98 -M pc98 -m 64M -kernel $(PC98_TARGET) -serial stdio; \
	elif [ -f tools/np2kai_bin ]; then \
	    ./tools/np2kai_bin; \
	else \
	    $(QEMU_X86_64) -M q35,accel=tcg -cpu qemu64 -m 1G $(QEMU_DISPLAY) -vga std -kernel $(PC98_TARGET) -serial stdio; \
	fi

test-pc98: $(PC98_TARGET)
	@echo "=========================================================="
	@echo " Testing B-System NEC PC-98 Kernel on QEMU (Headless CI)"
	@echo " Honoring : Awe Morris — zedBSD & NEC PC-98 Pioneer"
	@echo " Machine  : q35 (PC-98 compat)  |  Mode: headless, serial validation"
	@echo "=========================================================="
	@bash scripts/test_pc98.sh

# ═══════════════════════════════════════════════════════════════════
# Motorola 68040 Macintosh Quadra 800 Kernel (q800)
# ═══════════════════════════════════════════════════════════════════
M68K_TARGET     = ./.build/btron-m68k.elf
M68K_LD_SCRIPT  = src/drivers/m68k/m68k_q800.ld
M68K_CFLAGS     = -O2 -Wall -Wextra -std=c99 -mcpu=68040 -ffreestanding -nostdlib -DBTRON_TARGET=7 -DBTRON_M68K_TARGET -Iinclude -Iinclude/drivers -Isrc/kernel -Isrc/cores
M68K_STARTUP    = src/cores/core_m68k.c
M68K_SRCS       = $(M68K_STARTUP)          \
                  src/cores/core_init.c   \
                  src/kernel/libstr.c      \
                  $(COMMON_NO_SDL_SRCS)    \
                  $(LILCU64_ENGINE_SRCS)
M68K_OBJS       = src/drivers/m68k/boot_m68k.m68k.o $(M68K_SRCS:.c=.m68k.o)

%.m68k.o: %.s
	$(M68K_CC) -mcpu=68040 -c $< -o $@

%.m68k.o: %.c
	$(M68K_CC) $(M68K_CFLAGS) -c $< -o $@

m68k: $(M68K_TARGET)

$(M68K_TARGET): $(M68K_OBJS) $(M68K_LD_SCRIPT)
	@echo "=========================================================="
	@echo " Building B-System M68K Quadra 800 Kernel: $@"
	@echo "=========================================================="
	$(M68K_CC) -mcpu=68040 -nostdlib -T $(M68K_LD_SCRIPT) $(M68K_OBJS) -o $@
	@echo "[M68K-ELF] Built: $@"
	@file $@

run-m68k: $(M68K_TARGET)
	@echo "=========================================================="
	@echo " Launching B-System M68K Macintosh Quadra 800 on QEMU"
	@echo " Machine  : Apple Macintosh Quadra 800 (-M q800)"
	@echo " CPU      : Motorola 68040 @ 33 MHz (MMU / FPU Active)"
	@echo " RAM      : 128 MB (32-Bit Linear Address Space)"
	@echo " Display  : NuBus Slot 9 DAFB Framebuffer 800x600 @ 8-bpp"
	@echo " Input    : MOS 6522 VIA1 / VIA2 System Controllers & ADB"
	@echo " Serial   : Zilog Z8530 ESCC Dual UART (Port A Active)"
	@echo " Storage  : NCR 53C96 ESP SCSI Host Adapter"
	@echo "=========================================================="
	$(QEMU_M68K) -M q800 -cpu m68040 -m 128M \
	    $(QEMU_DISPLAY) \
	    -kernel $(M68K_TARGET) -serial stdio

test-m68k: $(M68K_TARGET)
	@echo "=========================================================="
	@echo " Testing B-System M68K Kernel on QEMU Quadra 800 (Headless CI)"
	@echo " Honoring : Fumihiko Itagaki — uITRON 3.0 Pioneer"
	@echo " Machine  : q800  |  CPU: m68040  |  RAM: 128M"
	@echo " Mode     : headless, serial validation"
	@echo "=========================================================="
	@bash scripts/test_m68k.sh

# ═══════════════════════════════════════════════════════════════════
# Sony PlayStation 2 Emotion Engine Kernel (ps2 / PCSX2) [Target 8]
# ═══════════════════════════════════════════════════════════════════
AUTO_GUI       ?= 1
# HIDTRACE=1 make ps2  -- per-keypress and per-control-transfer rows.  Off by
# default because a [KBD] row costs the console a line for every keystroke, which
# is what scrolls the prompt away while it is being typed into.
HIDTRACE       ?= 0
# Which rasteriser a GL window on this image gets by default.  The port links both software
# ones -- src/gl/tinygl/ and src/gl/backend_virgl.c are in the source list -- so tinygl and
# virgl are a choice, not a capability, and it is worth choosing because the two are not the
# same size: backend_virgl.c keeps one float per pixel for depth, which at 768x500 is 1.5 MB
# to allocate and 384k software-float stores to fill, against TinyGL's 16-bit z of half that.
# PS2_GL=virgl builds the old combination, and the [XMBT] rows time the same load against both.
#
# The third value is the Graphics Synthesizer: it makes the build's default the backend the
# Display applet's "OpenGL PS2" row offers, so an A/B run against TinyGL needs a make flag and
# no clicking.  Until src/gl/backend_eegs.c exists (row 5 of the plan of record in
# doc/txt/PS2.txt) that request is answered by the linked software rasterizer with a console line
# saying so, which is why this is a default and not a capability either -- the settings row can
# still outrank it, and a built image reports what actually drew its frames.
# A value that is none of the three is a typo, and a typo here builds a working image whose
# timing rows describe a different pipeline than the one the operator asked for, so it is an
# error rather than a silent tinygl.
PS2_GL          ?= eegs
ifeq (,$(filter $(PS2_GL),tinygl virgl eegs))
$(error PS2_GL=$(PS2_GL) is not one of tinygl, virgl, eegs)
endif
PS2_TARGET     = btron-ps2.elf
PS2_ISO        = btron-ps2.iso
PS2_LD_SCRIPT  = src/drivers/ps2/ps2.ld
PS2_CFLAGS     = -O2 -Wall -Wextra -std=c99 -ffreestanding -nostdlib \
                 -DBTRON_TARGET=8 -DBTRON_PS2_TARGET -DBTRON_AUTO_GUI=$(AUTO_GUI) \
                 -DBTRON_HID_TRACE=$(HIDTRACE) -DBTRON_GL_BACKEND_TINYGL \
                 -Iinclude -Iinclude/gl -Iinclude/drivers -Isrc/kernel -Isrc/cores -Isrc/drivers/ps2 -Isrc/quake/include
ifeq ($(PS2_GL),virgl)
PS2_CFLAGS    += -DBTRON_GL_PREFER_VIRGL
endif
ifeq ($(PS2_GL),eegs)
PS2_CFLAGS    += -DBTRON_GL_PREFER_EEGS
endif
# FP_COUNT=1 makes the soft-float libcalls count themselves, and the [XMBT] rows print
# that count per stage boundary: it is how a stage's microseconds are split between
# arithmetic and everything else, which no disassembly reading can settle because the
# number of calls a vertex actually executes is a property of the compiled path, not of
# the source.  Costs a load, an add and a store on the hot side of a call worth ~28
# cycles, so an instrumented run's microseconds are inflated and only its counts are
# read.  0 compiles the counters away.
PS2_FP_COUNT    ?= 0
PS2_CFLAGS    += -DBTRON_PS2_FP_COUNT=$(PS2_FP_COUNT)
# PS2_IDENT=1 is LANE 12h's deciding instrument: the present folds a per-group hash over the staged
# GIF stream, carries it one frame forward, and prints how many of this frame's groups and quadwords
# are identical to the previous presented frame's same-index group (`[GSTID]`).  It decides between
# the lane's two rungs -- a replay of an invariant span versus a textured batch map -- and nothing
# else in the port can answer the question, because LANE 10's buffer-lifetime law forbids keeping
# the words.  Cost is two 32-bit multiplies per quadword inside the copy the sink already does, so an
# ident run's microseconds are slightly inflated and only its invariance columns are read: the same
# rule PS2_FP_COUNT states for its counters.  0 compiles the instrument out of the shipped image.
PS2_IDENT       ?= 0
PS2_CFLAGS    += -DBTRON_PS2_GS_IDENT=$(PS2_IDENT)
# PS2_PW=1 is LANE 22 (task #165): the per-app paint worker.  It compiles the policy in
# src/drivers/ps2/ps2_paint_worker.c and the seam that uses it in src/cores/core_ps2.c, so a pass can
# run one window's paint callback once -- priced by what it cost last time, admitted only while the
# frame's remaining slice can afford it, and forced through when the price would starve the app.
# The claim is not parallelism: the Emotion EE has one main core, `sta_tsk()` is synchronous and
# `slp_tsk()` is a no-op, so the only three wins available are fewer redundant callbacks, a bounded
# pass, and a cost named per application.  Which of the three it actually buys is measured on the
# [PW] rows and the `paint:`/`loop:` columns, and the answer is readable at run time rather than
# baked in: `paintw 0|1` and Ctrl+O switch it on one image, so the operator A/B's the smoothness
# themselves instead of rebuilding for it.  0 leaves the shipped image exactly as it was: the module
# is not even linked (see PS2_PW_SRCS below), and the compositor path every other target shares is
# untouched.
PS2_PW          ?= 0
ifeq (,$(filter $(PS2_PW),0 1))
$(error PS2_PW=$(PS2_PW) is not 0 or 1)
endif
PS2_CFLAGS    += -DBTRON_PS2_PAINT_WORKER=$(PS2_PW)
PS2_PW_SRCS     = $(if $(filter 1,$(PS2_PW)),src/drivers/ps2/ps2_paint_worker.c,)
PS2_STARTUP    = src/cores/core_ps2.c
# The termios-scope applications gterm hosts: Sokhatsky Commander and Terminal
# Vision.  They sit in COMMON_SRCS, so the hosted targets have always had them and
# a bare-metal link has not -- without which gterm.c's weak sc_session_init() is
# the definition that wins, returns -1, and leaves the terminal window in its shell
# forever.  Named here rather than folded into COMMON_NO_SDL_SRCS because the other
# bare-metal targets have not been measured against it.
CLU_TERMIO_SRCS = src/clu/lang.c           \
                  src/clu/term.c           \
                  src/clu/tty.c            \
                  src/clu/sc/files.c       \
                  src/clu/sc/input.c       \
                  src/clu/sc/menus.c       \
                  src/clu/sc/sc.c          \
                  src/clu/tv/tv.c          \
                  src/clu/be/native.c
# PS2_NV parks the nvidia decoder island (NV_SRCS, named just above BE_DASM_SRCS) out of this
# link: `nm` over every `*.ps2.o` reads zero undefined references into those 27 units, because
# the only caller of their entry point -- `case ARCH_NVIDIA: decodeNV(...)` in
# src/clu/be/dasm/dasm.c -- is not in any PS2 source list at all.  It is therefore 3,414,308 B
# of unreachable statics, not a feature this image loses; the host and POSIX lanes still get it
# through the `+=` that puts it back.  The dial and its stamp are here for the same reason as
# PS2_QUAKE's: an un-park is the control, and a stale object is the failure mode.
CLU_TERMIO_SRCS += $(if $(filter 1,$(PS2_NV)),$(BE_DASM_SRCS),$(filter-out $(NV_SRCS),$(BE_DASM_SRCS)))
# The GL layer this port links, minus the app bundle GL_SRCS also carries.  Quake's
# hunk arena alone is a 64 MiB static array -- 1 MiB under the PS2 branch of mem.c's
# HUNK_TOTAL_SIZE, but 3,911,851 B across its 22 units either way -- and the Lil Cu demo's
# node table 3 MiB
# more, against an Emotion Engine with 32 MB of RDRAM: an image that big does not
# fail to build, it simply puts every later .bss object -- the event queue, the
# deskbar, the tracker -- past the end of memory, where a write to them is accepted
# and a read of them comes back zero.  The desktop noticed as input going dead
# (2026-10-09, see the [BENCH] echo row); ps2.ld now refuses the link instead.
# The launcher reaches its apps through weak open_*_window() externs, so an app that
# is not linked here is a menu row that does nothing rather than a link error.
PS2_GL_SRCS    = src/gl/gl_dispatch.c     \
                 src/gl/gl_backend_choice.c \
                 src/gl/egl_surface.c     \
                 src/gl/backend_tinygl.c  \
                 src/gl/backend_virgl.c   \
                 src/gl/backend_eegs.c    \
                 src/gl/eegs_present.c    \
                 src/gl/gscube_nexus.c    \
                 src/drivers/ps2/ps2_vu1_geom.c \
                 src/apps/xmb.c           \
                 src/apps/xmb_wave.c      \
                 src/apps/glgears.c       \
                 $(DEMO_SRCS)             \
                 $(PS2_QUAKE_SRCS)
# B-MSX rides in the image when PS2_MSX=1: the tronMSX core reads no float, no host
# clock and no libc beyond what src/kernel/libstr.c provides, and its two pieces of
# media -- C-BIOS and the cartridge catalogue -- are bodies of the /SYS volume the
# image already carries (src/tools/manifest.txt, the `cbios` and `msx` drawers).  So
# the XMB Games band lists what the machine holds and Cross opens it, with no file
# system below the program at all.
#
# It is PARKED at 0 as of 2026-10-14, and the reason changed within that day, which is
# worth keeping in the comment because it is the reason the dial has a STAMP.  It landed
# as a link rescue: the objects of that morning summed 35,809,162 B of .bss against the
# ceiling ps2.ld ASSERTs at 0x01FF0000 = 33,488,896 B, with slots.c alone at 5,496,436.
# The control for a park is the un-park, and `make ps2 PS2_MSX=1 PS2_GL=eegs PS2_FP=hw`
# re-run at 02:24 the same day LINKS: ELF 6,018,220 bytes, _end = 0x01F89A00, 419,328 B
# under the ceiling.  The B-MSX lane's own 01:01 commit (9bbec9d) rewrote slots.c and
# geo3d.c under the failure, and today's objects read slots.c 2,350,712, bios.c 442,368,
# vdp.c 393,998, msx_app.c 129,580, summing 3,316,862 B over the fifteen units the dial
# adds; the two linked images differ by 3,316,864 B of .bss and 3,402,880 B of _end.
# So the dial is not what makes the image link any more.  It stays at 0 because the
# un-park spends 3.4 MB of the port's 3.8 MB headroom -- 419,328 B left, one
# cartridge -- and because that headroom is not free to spend: the image's 11 MiB heap
# (PS2_HEAP_SIZE, src/cores/core_ps2.c:112) is NOT MSX-gated and sits in both builds,
# which also means the old "12 MiB MSX pool" line in this comment was wrong twice over.
# And tronMSX still asks the host folder for a C-BIOS the volume holds, so the Games
# band would ship a window saying it cannot find its own BIOS (doc/txt/PS2.txt M17 and
# the PS2_MSX row of chapter 21).  With the dial at 0 the
# five B-MSX gate rows print SKIP by name, and src/apps/xmb.c's weak open_msx_window*()
# stubs mean a Games row is a menu row that does nothing rather than a link error.  The
# stamp exists because a source-list change is not a recompile: make compares file
# mtimes, so without it yesterday's [MSX] rows link into today's parked image -- and two
# .bss readings of the same filename three megabytes apart, on one day, is the evidence
# for that sentence in this very comment.
#
# The dial now also answers to the GL backend, because one of this port's two heaps has to be
# the bigger one.  `PS2_HEAP_SIZE` (src/cores/core_ps2.c) is `7500 * 1024` under `-DBTRON_PS2_MSX`
# and `9 * 1024 * 1024` without it -- the MSX core's .bss (cartridge buffers, V9938 VRAM, S1985/
# S1990, Geo3D) takes what the extra 1.5 MB of heap would have been.  The XMB's GL window asks for
# ~8.7 MB of planes against that: 1500 kB surface + 750 kB depth plane (TinyGL's `ZB_open`) +
# 576 kB icon atlas, on a desktop baseline already holding ~5.85 MB.  At 7500 kB the ask fails by
# name on both halves -- `[TinyGL] ERROR: ZB_open failed (OOM?)` and `[GL] xmb: no memory for the
# icon atlas` -- and the operator sees a framed window with nothing inside it.  That is the shipped
# "empty inside" of `make run-ps2` under `PS2_GL=eegs`, and the LANE 19 census cleared the stream
# of it: with the heap at 9 MB the same eegs page paints 383,951 of its own 384,000 client pixels.
# So eegs parks MSX and takes the 9 MB heap.  `origin` is what makes this a DEFAULT and not an
# override: `make ps2 PS2_MSX=1 PS2_GL=eegs` is still the falsifying control, and it still OOMs the
# atlas and the depth plane by name; a `PS2_MSX=0` in the environment is honoured unchanged.
ifeq ($(origin PS2_MSX),undefined)
PS2_MSX         := $(if $(filter eegs,$(PS2_GL)),0,1)
else
PS2_MSX         ?= 1
endif
PS2_MSX_SRCS    = $(if $(filter 1,$(PS2_MSX)),$(MSX_SRCS) src/emulators/msx/msx_app.c,)
# Quake is PARKED at 0 as of 2026-10-24 by the operator, for the same reason B-MSX is and with
# the same shape: it is a Games-band row, and the image it lives in is over ps2.ld's budget.
# The measurement, taken against a link with both ASSERTs relaxed (`_end = 0x02621700`):
#
#   the image needs                          38,934,272 B under the stack top
#   RDRAM under the stack, with reserve      32,178,176 B  (0x01FB0000 - 0x00100000)
#   over by                                  6,756,096 B
#   $(QUAKE_SRCS) is                         3,911,851 B of that  (145,304 text + 104 data
#                                                + 3,766,443 .bss across 22 objects; the three
#                                                1 MiB arrays are mem.c's hunk buffer, r_alias's
#                                                skin plane and texture.c's RGBA scratch)
#
# The other 2,842,496 B is not Quake's, and this dial does not pretend to have paid it -- it is
# what the remaining levers are for.  What it does pay is the single largest app bundle in the
# link, and it pays it because the machine's purpose is the DAW: CLU, Lil Cu and the orchestra
# are what this image is for, and a brush model of E1M1 is not.
#
# Parking is safe here in a way B-MSX's park was not quite: the twenty-two units take no call
# from anywhere else in the image.  Measured with `nm` over every `*.ps2.o`: 437 symbols
# defined among them, zero of them named as an undefined reference by any other unit.  The one
# name the desktop does ask for -- `open_quake_window()` -- is answered by src/clu/clu.c's weak
# stub, which the strong definition in quake_app.c has been overriding all along; the four
# consumers (desktop.c, global_menu.c, tracker.c, xmb.c) already null-test it, and core_ps2.c's
# `quake` console command does not, so at PS2_QUAKE=0 it runs the stub and then prints
# `[PS2-UI] Opened Quake 3D.`, which is the same silent lie the MSX park already prints and is
# left alone rather than patched into a lane that is not being measured.
#
# The stamp is the law that a source-list change is not a recompile: without it the twenty-two
# objects of the last un-parked build survive into the parked link, and the ASSERT then charges
# 3.9 MB of .bss to an app nobody can call.  `make -n PS2_QUAKE=0` purges them too, because
# `$(shell ...)` runs while parsing.
PS2_QUAKE      ?= 0
PS2_QUAKE_SRCS = $(if $(filter 1,$(PS2_QUAKE)),$(QUAKE_SRCS),)
PS2_QUAKE_STAMP = .build/ps2-quake.stamp
PS2_QUAKE_PREV := $(shell cat $(PS2_QUAKE_STAMP) 2>/dev/null)
ifneq ($(PS2_QUAKE_PREV),$(PS2_QUAKE))
$(shell mkdir -p ./.build && echo '$(PS2_QUAKE)' >$(PS2_QUAKE_STAMP) && \
        rm -f $(QUAKE_SRCS:.c=.ps2.o) $(QUAKE_SRCS:.c=.pbench.o))
$(info [PS2] PS2_QUAKE is now $(PS2_QUAKE) (was $(if $(PS2_QUAKE_PREV),$(PS2_QUAKE_PREV),unset)) -- the Quake units will rebuild)
endif
# PS2_NV is the same shape and the bigger half of the answer: 27 units, 3,414,308 B, and no
# caller at all in this link.  Read the reason where the `$(filter-out)` sits, at
# CLU_TERMIO_SRCS above.  The stamp is here because a park the objects do not know about is a
# park that still charges 3.4 MB of .data -- mostly gm107.c's 1,382,528 B of opcode tables -- to
# the image-budget ASSERT.
PS2_NV         ?= 0
PS2_NV_STAMP    = .build/ps2-nv.stamp
PS2_NV_PREV    := $(shell cat $(PS2_NV_STAMP) 2>/dev/null)
ifneq ($(PS2_NV_PREV),$(PS2_NV))
$(shell mkdir -p ./.build && echo '$(PS2_NV)' >$(PS2_NV_STAMP) && \
        rm -f $(NV_SRCS:.c=.ps2.o) $(NV_SRCS:.c=.pbench.o))
$(info [PS2] PS2_NV is now $(PS2_NV) (was $(if $(PS2_NV_PREV),$(PS2_NV_PREV),unset)) -- the nvidia decoder units will rebuild)
endif
PS2_SRCS       = $(PS2_STARTUP)           \
                 src/cores/core_init.c    \
                 src/drivers/ps2/ps2_gs.c \
                 src/drivers/ps2/ps2_gif_ring.c \
                 src/drivers/ps2/ps2_gs_pack.c \
                 src/drivers/ps2/ps2_gs_vram.c \
                 src/drivers/ps2/ps2_gs_env.c \
                 src/drivers/ps2/ps2_gs_poster.c \
                 src/drivers/ps2/ps2_gs_rec.c \
                 src/drivers/ps2/ps2_gs_tex.c \
                 src/drivers/ps2/ps2_gs_read.c \
                 src/drivers/ps2/ps2_builtins.c \
                 src/drivers/ps2/ps2_sio.c \
                 src/drivers/ps2/ps2_pad.c \
                 src/drivers/ps2/ps2_iopram.c \
                 src/drivers/ps2/ps2_irq.c \
                 src/drivers/ps2/ps2_usb.c \
                 $(CLU_TERMIO_SRCS)       \
                 $(PS2_MSX_SRCS)          \
                 $(PS2_PW_SRCS)           \
                 $(TINYGL_SRCS)           \
                 $(PS2_GL_SRCS)           \
                 src/kernel/libstr.c      \
                 src/kernel/libint64.c    \
                 $(COMMON_NO_SDL_SRCS)
PS2_OBJS       = src/drivers/ps2/boot_ps2.ps2.o         \
                 src/drivers/ps2/ps2_gs_reg.ps2.o      \
                 $(PS2_SYS_VOL_OBJ)                    \
                 $(PS2_SRCS:.c=.ps2.o)

# The /SYS volume rides inside the image's .data (doc/txt/PS2.txt, chapter 20): the bytes are
# btron_sys.vol, which the host tool mkbtronfs writes from src/tools/manifest.txt, and the object is
# the one .incbin of them.  It is a prerequisite of nothing here -- the volume's own rule now waits
# on the manifest's payloads -- so a document edit rebuilds the volume, which rebuilds this object,
# which relinks the image, and no step of that is a human remembering to run the tool first.
# `-I.` is what makes the .incbin path mean the file at the top of the checkout rather than a file
# next to the .s, because make runs from the repository root and the assembler resolves that path
# the way it resolves an include.
PS2_SYS_VOL_OBJ     = src/drivers/ps2/ps2_sys_vol.ps2.o
PS2_SYS_VOL_SOURCE  = src/drivers/ps2/ps2_sys_vol.s
# And the prerequisite is NOT a read-only input, which is the part learned the hard way on
# 2026-10-14: three host suites mount the real btron_sys.vol read/write -- test_b_drivesetup.c
# mounts it as /SYS at :158 and then writes its scratch `sys_boot_%d.cfg` into it at :959,
# test_editor_ui.c:1043 and test_fs.c:978/:1027 open it the same way -- so after a `make test`
# the working file is no longer what the manifest says (measured: 90 bytes, once 40,841).  A
# mutation that bumps its mtime makes this rule run, and the naive rule then .incbin's a test's
# scratch file into the shipped image.  test-ps2 cannot see that happening, because its own pair
# of payload rows compares the ELF against the same mutated file; the row that can see it is
# #90, and it went red on exactly this.  So the link repairs its own input first: mkbtronfs is
# byte-reproducible (two fresh builds compare clean), so rebuild the manifest elsewhere and, if
# the working file is not that, replace it.  The repair happens before the compile line, so the
# object always ends up newer than the volume and a clean tree still says "nothing to do".
$(PS2_SYS_VOL_OBJ): $(PS2_SYS_VOL_SOURCE) btron_sys.vol
	@./mkbtronfs src/tools/manifest.txt -o .build/ps2-vol-fresh.vol >/dev/null 2>&1; \
	 if ! cmp -s .build/ps2-vol-fresh.vol btron_sys.vol; then \
	   echo "[PS2] btron_sys.vol is not the manifest ($$(cmp -l .build/ps2-vol-fresh.vol btron_sys.vol | wc -l | tr -d ' ') bytes): a host FS suite wrote into it, so the image relinks from a fresh build"; \
	   cp .build/ps2-vol-fresh.vol btron_sys.vol; \
	 fi; \
	 rm -f .build/ps2-vol-fresh.vol
	$(PS2_CC) -I. -c $< -o $@

# One calling convention for the whole PS2 image.  A 64-bit-GPR MIPS under O32
# passes fixed arguments 5..8 in $t0..$t3 where a 32-bit-GPR MIPS puts them on
# the stack, and it lays its vararg save area out in 8-byte slots while va_arg
# still walks four bytes at a time.  Neither difference shows up inside one
# translation unit, which is why -march=mips3 for most files and -march=mips2 for
# the two that own a va_list looked harmless; but every fixed call of five or more
# arguments crossing between them silently passed garbage, and so did every console
# line printed by a mips3 file.  The cursor is what made it visible:
# draw_baremetal_cursor_raw() takes five arguments, its fifth -- the canvas height
# -- arrived as junk when core_ps2.c called it, `py >= h` rejected every row, and
# the sprite vanished on the first banded pass after the whole-canvas repaint that
# draws it from inside desktop.c, where the call stays mips3 -> mips3 (2026-10-09).
#
# mips2 is the side that has to win: it is the only O32 flavour whose vararg layout
# agrees with the va_arg walk, and its instruction set is a subset of MIPS III, so
# nothing illegal for the Emotion Engine is emitted.  (mips32r2 would be 32-bit-GPR
# too, but clang may reach for ext/ins/clz/madd, which a R5900 does not have -- the
# reason this port originally moved off it.)  The one thing that does need a 64-bit
# instruction is the GS, which latches a privileged register on a single sd and
# takes two writes if a mips2 build splits the store in half; that access lives in
# src/drivers/ps2/ps2_gs_reg.s, whose three 32-bit arguments mean exactly the same
# thing under either ISA, so it cannot be corrupted by a mixed link.  The other
# thing a 32-bit-GPR MIPS cannot do is convert a double to or from a 64-bit integer
# -- the FPU speaks 32-bit GPRs here, and so does the Emotion Engine's -- so clang
# emits the O32 libcall names __floatdidf/__fixdfdi at the float-heavy files, and
# src/drivers/ps2/ps2_builtins.c defines them for the -nostdlib link.

%.ps2.o: %.s
	$(PS2_CC) -c $< -o $@

%.ps2.o: %.c
	$(PS2_CC) $(PS2_CFLAGS) -MMD -MP -c $< -o $@

# src/media/gst_rt.c crashes LLVM 22.1.8 in MachineFunction's ShrinkWrap pass, on
# `Shrink Wrapping analysis' against @clock_block: the same file at commit 544425f, with the
# flags at commit 544425f, dies identically, so this is the toolchain and not the lane.
# Reproduce with `make ps2`, or by compiling that revision's copy with PS2_CFLAGS.
# -mllvm -enable-shrink-wrap=false is the suppression, measured rather than guessed: with
# the base flags the compile crashes, adding this one emits a 71420-byte object.  The
# clang-native spelling is -fno-shrink-wrap and this build of clang does not accept it;
# -mllvm -no-shrink-wrap is likewise unknown.  Nothing here moves the ISA, the 32-bit GPR
# O32 ABI or the soft-float contract in doc/txt/PS2.txt -- shrink-wrap only decides where
# a function's prologue and epilogue go -- and `make test-ps2` measures that on the object
# instead of trusting this comment.
# Scoped to the one object on purpose: PS2_CFLAGS is what every [BENCH] row was measured
# under, and widening it here would silently re-baseline the numbers the bench reports.
src/media/gst_rt.ps2.o: PS2_CFLAGS += -mllvm -enable-shrink-wrap=false

# `PS2_GL=` changes the flags three translation units compile under, and make does not see a
# flag change: their objects stay newer than their sources and get linked into the image built
# for the other value, which is worse than a build failure because the `[GL]` row then describes
# a pipeline nobody asked for and the [XMBT] timings describe it too.  So the value itself is
# what gets tracked: it is written into a stamp file and, when the two disagree, the objects
# that read it are removed before anything is linked.  The same law scripts/ps2_smooth.sh
# applies to PS2_FP_COUNT, here so a bare `make ps2 PS2_GL=eegs` is covered by it too -- and it
# has to be a content comparison rather than a stamp target with a recipe, because a target with
# no prerequisites is never considered out of date and would sit there holding the old value.
PS2_GL_STAMP  = .build/ps2-gl.stamp
PS2_GL_PREV  := $(shell cat $(PS2_GL_STAMP) 2>/dev/null)
ifneq ($(PS2_GL_PREV),$(PS2_GL))
$(shell mkdir -p ./.build && echo '$(PS2_GL)' >$(PS2_GL_STAMP) && \
        rm -f src/gl/gl_backend_choice.ps2.o src/gl/gl_dispatch.ps2.o src/gl/egl_surface.ps2.o)
$(info [PS2] PS2_GL is now $(PS2_GL) (was $(if $(PS2_GL_PREV),$(PS2_GL_PREV),unset)) -- the objects that read it will rebuild)
endif

# PS2_MSX is a flag as well as a source list: src/cores/core_ps2.c compiles its [MSX] bench
# phase under it, and a stale object there would print a row about a machine the image does
# not contain -- or omit a row about one it does.  Same content-comparison stamp, same
# disease as PS2_GL, and it removes the bench object too because that is a second compile
# of the same source under the same define.
PS2_MSX_STAMP = .build/ps2-msx.stamp
PS2_MSX_PREV := $(shell cat $(PS2_MSX_STAMP) 2>/dev/null)
ifneq ($(PS2_MSX_PREV),$(PS2_MSX))
$(shell mkdir -p ./.build && echo '$(PS2_MSX)' >$(PS2_MSX_STAMP) && \
        rm -f src/cores/core_ps2.ps2.o src/cores/core_ps2.pbench.o)
$(info [PS2] PS2_MSX is now $(PS2_MSX) (was $(if $(PS2_MSX_PREV),$(PS2_MSX_PREV),unset)) -- the unit that reads it will rebuild)
endif
ifeq ($(PS2_MSX),1)
PS2_CFLAGS    += -DBTRON_PS2_MSX=1
endif

# LANE 22's dial is a flag on two units and a source list on one, so it is the same staleness as
# PS2_MSX in three places at once: core_ps2.c's seam compiles under it, the bench object is a second
# compile of the same source, and ps2_paint_worker.ps2.o must not survive a switch to 0 as an object
# no header declares any more -- which is what the image-budget ASSERT then charges for .bss nobody
# references.  A switch to 1 removes nothing but itself: the leaf appears in PS2_SRCS by the same
# $(filter) test.
PS2_PW_STAMP = .build/ps2-pw.stamp
PS2_PW_PREV := $(shell cat $(PS2_PW_STAMP) 2>/dev/null)
ifneq ($(PS2_PW_PREV),$(PS2_PW))
$(shell mkdir -p ./.build && echo '$(PS2_PW)' >$(PS2_PW_STAMP) && \
        rm -f src/cores/core_ps2.ps2.o src/cores/core_ps2.pbench.o \
              src/drivers/ps2/ps2_paint_worker.ps2.o)
$(info [PS2] PS2_PW is now $(PS2_PW) (was $(if $(PS2_PW_PREV),$(PS2_PW_PREV),unset)) -- the paint worker's units will rebuild)
endif

# The PS2 lane owns its allocator: src/cores/core_ps2.c defines Imalloc, Icalloc, Ifree and
# Irealloc strong against `s_ps2_heap`, and libstr.c's four weak versions only ever answer a
# link that has nothing else.  libstr's fallback arena is therefore dead weight here, but dead
# weight make can still see: 1,048,576 B of .bss in a 32 MB machine whose image was over
# ps2.ld's budget by 2,842,496 B with Quake already parked.  The define shrinks it to the 64 B
# stub the ARM lanes use.
#
# It is a per-object flag rather than a PS2_CFLAGS one because the lane cannot afford the
# difference: a flag on the whole recipe is a change every `*.ps2.o` has to answer, and the
# staleness law (see PS2_MSX above) would have me purge the entire image to move one array.
# Same shape as the gst_rt shrink-wrap override above and the bench TU's defines below, and it
# needs the one-time purge only the first time, because make compares file dates and a Makefile
# is not a prerequisite of an object.
#
# The define is its own name and not the lane's existing -DBTRON_PS2_TARGET because that one is
# also handed to host suites' rules (see the note above TEST_PTRACCUM_OBJ: "must not reach any
# other suite's object through a pattern rule").  A hosted test that uses the fallback allocator
# and finds a 64-byte arena is a failure nobody could read off the console.
src/kernel/libstr.ps2.o: PS2_CFLAGS += -DBTRON_LIBSTR_NO_HEAP=1

# PS2 measurement build: the present/pointer bench lives only in core_ps2.c, so one
# object is rebuilt with the define and the shipped btron-ps2.elf keeps its own
# flags -- the artifact measured is not the artifact flashed.  scripts/ps2_bench.sh
# runs it under PCSX2 -nogui, so the table needs no window and no hand.
PS2_BENCH_OBJ  = src/cores/core_ps2.pbench.o
PS2_BENCH_OBJS = $(PS2_BENCH_OBJ) $(filter-out src/cores/core_ps2.ps2.o,$(PS2_OBJS))
PS2_BENCH_TARGET = btron-ps2-bench.elf
# BENCH_APP=1 scripts/ps2_bench.sh additionally opens xmb.c and counts the frames the
# task shim gives it.  Off by default: the app it opens does not reach its own exit yet
# (see the phase's note in core_ps2.c), so the phase would end the run at its timeout
# rather than at its sentinel.
BENCH_APP      ?= 0
# Frames the app phase asks for before it injects its own Escape.  The default keeps
# the table's resolution; scripts/ps2_smooth.sh lowers it so a run ends at its
# sentinel instead of at a harness timeout -- 60 frames of this app is 24 minutes.
BENCH_APP_FRAMES ?= 60
# Frames the launcher-by-key CYCLE phase asks for, which is the phase that ends the run at its own
# sentinel (the frame hook injects Escape when this budget is spent, and the harness stops the
# emulator at that sentinel).  It is 3 because the phase's question is "did the row open and draw",
# not "what does a long run look like".  It is a dial because LANE 22's paint worker is judged on a
# RATIO over many passes -- how often a frame refused the app, what it priced the refusal against --
# and three passes cannot show a policy deciding.  `BENCH_LAUNCH_FRAMES=40` is what #165's
# measurement uses.
BENCH_LAUNCH_FRAMES ?= 3
# BENCH_GS=1 makes the headless run the GS census instead of the desktop bench: the
# Local->Host read-back (LANE 19 rung 2) is only honest at the Stage-1 console, because
# the desktop's own presents erase the read blocks and its ring traffic makes a read
# refuse.  Off by default, and it replaces rather than joins the run -- see the rung's
# note in core_ps2.c.
# BENCH_GS=2 is the opposite rung: LANE 19 rung 6 reads the page a LIVE desktop made, so it boots
# the session like a normal bench run and censuses page 0 against its own canvas from the app
# phase's frame hook.  It therefore needs BENCH_APP=1 to have a window up, and it prints nothing
# between its drawn control and its bands -- the console's own repaint is what the read's idle
# precondition refuses.
# BENCH_GS=6 takes the census branch again, and adds LANE 21 rung 3a to it: one plate uploaded into a
# PSMCT16 display page and read back through VIF1, censused at 32 bits, at 16 with the GS's dither,
# and at 16 without it.  It is a Stage-1 rung because it is a read of a page nothing else may be
# writing -- inside a desktop session the present path uploads the same page between the plate and the
# read, and the verdict row would describe a race instead of a format.
# BENCH_GS=7 adds LANE 21 rung 4 to the same branch: the two-page rotation.  It dials PSMCT16, turns
# the second page on, plates four colours across three passes, and censuses the page each flip claims
# is now being scanned -- including the pass whose damage is one rect on a page that owes another,
# which is the reading that fails if ps2_rot_owe() is removed.  Stage-1 for the same reason as 6: the
# rotation is only measurable against a page no compositor is writing.  The flip's vsync wait and its
# unaligned count print as their own labelled row and are NOT part of the verdict, because whether
# PCSX2 raises CSR.VSYNC here is a fact about the emulator rather than about the rotation.
BENCH_GS       ?= 0
# LANE 24's two dials, both off so every older run keeps its rows.  BENCH_TEX=1 arms a paired cell
# census taken from the app phase's frame hook: the live display page and the desktop canvas read at
# the SAME box, plus every registered texture plane read out of VRAM, and the verdict row says which
# of the three holds the flat rectangles the user reports.  It is now a BEFORE/AFTER pair rather than
# one frame, because the report moved: "Icons show inititially fine but get corrupted after FPS ticker
# changed" names an event, so the hook watches the window caption (fps_overlay_tick() renames it through
# wnd_set_title(), whose damage is the tab rect alone) and censuses again on the next pass and at +4,
# diffing cell by cell and diffing the texture registry's VRAM spans.  BENCH_ORDER=1 opens `lilcu`
# before `xmb` -- the sequence the original report describes -- which is the variable; the census above
# is the measurement.  Both need BENCH_APP=1, since the windows they ask about only the app phase opens.
# With either dial up, the app phase runs WITHOUT the launcher-by-key cycle ahead of it: that phase
# never returns (its Escape does not close the launcher, task #30), and the census lives in the phase
# after it, which is why bench logs back to 2026-10-16 carry no `opened through the task shim` row.
BENCH_TEX      ?= 0
BENCH_ORDER    ?= 0
$(PS2_BENCH_OBJ): PS2_CFLAGS += -DBTRON_PS2_BENCH=1 -DBTRON_PS2_BENCH_APP=$(BENCH_APP) \
                                -DBTRON_PS2_BENCH_APP_FRAMES=$(BENCH_APP_FRAMES)u \
                                -DBTRON_PS2_BENCH_LAUNCH_FRAMES=$(BENCH_LAUNCH_FRAMES)u \
                                -DBTRON_PS2_BENCH_TEX=$(BENCH_TEX) \
                                -DBTRON_PS2_BENCH_ORDER=$(BENCH_ORDER)u \
                                -DBTRON_PS2_BENCH_GS=$(BENCH_GS)

PS2_BGS_STAMP = .build/ps2-bgs.stamp
# The frame budget is IN the key: it is a -D of this same object, and a loop that re-runs with
# BENCH_APP_FRAMES=12 after a run at 60 would otherwise silently reuse the 60-frame image.
PS2_BGS_KEY := $(BENCH_APP):$(BENCH_GS):$(BENCH_LAUNCH_FRAMES):$(BENCH_APP_FRAMES):$(BENCH_TEX):$(BENCH_ORDER)
PS2_BGS_PREV := $(shell cat $(PS2_BGS_STAMP) 2>/dev/null)
ifneq ($(PS2_BGS_PREV),$(PS2_BGS_KEY))
$(shell mkdir -p ./.build && echo '$(PS2_BGS_KEY)' >$(PS2_BGS_STAMP) && rm -f $(PS2_BENCH_OBJ))
endif

%.pbench.o: %.c
	$(PS2_CC) $(PS2_CFLAGS) -MMD -MP -c $< -o $@

# The three objects in the image that may hold COP1 instructions.  -msoft-float is an
# argument-passing law (the note at PS2_CC), and none of these breaks any part of it: every
# entry point in src/drivers/ps2/ps2_builtins.c takes and returns an integer register holding
# IEEE-754 bits, and the only float-shaped things in that file are inside __asm__ strings;
# the two functions in src/gl/tinygl/vertex_stage.c take a `GLVertex *` and return nothing,
# so the floats they compute never leave the unit; and src/apps/xmb_wave.c takes the effect
# clock as bits in an integer, takes the window's size as two integers, returns nothing, and
# hands the sheet out through memory the reader loads as a float -- its two wide-trig calls
# are bits in and bits out as well.  All three still need the flag off to assemble at all --
# under -msoft-float clang rejects `mtc1` with "instruction requires a CPU feature not
# currently enabled", which is why the EE's own FPU was unusable for single precision until
# that first file, unusable for the geometry until the second, and unusable for the wave's
# lattice until the third.  The link keeps recording `FP ABI: Soft float` for the whole
# image, and mipsel-linux-gnu-ld accepts the mixed .MIPS.abiflags the way it is.  PS2_FP=soft
# builds without any of them, and then the bit engine answers every call exactly as it did
# before 2026-10-10 -- which is also the escape hatch if a console ever boots with the FPU
# off, since a hard-float unit cannot ask the runtime valve (`btron_fp_on`) to substitute for
# instructions it has already emitted.
#
# Inside these three there is a second law, and it is not about instructions.  The R5900 has
# 32 single-precision registers and no double ones, so the O32 convention's callee-saved
# $f20/$f22/.../$f30 -- which a normal MIPS preserves with `sdc1` of the even/odd pair --
# cannot be preserved here at all: the store and its reload are both ignored, and a unit that
# lets the allocator put a live value in one of them silently destroys its caller's.  Two of
# the three ways that happens are visible in the source (a working set over the caller-saved
# ten, a loop whose invariant float is live across its own call); the third is LLVM's
# allocator preferring a callee-saved register for any long live range, which is a cost-model
# decision no source spelling changes.  So the three units are built with that preference
# priced out, and the image is then scanned for the pair instructions: `PS2_FP_CSR_COST` is
# the knob, and scripts/test_ps2.sh's FPU row is the proof.  Measured on the wave unit, this
# flag is 52 pair stores down to 4 and the four were one real working-set overflow that the
# source has since been split for; on the source as it now stands, dropping the flag gives
# 14 pair stores and 32 references to $f20-$f31 against the shipped object's zero of each,
# which is the control that says the knob is still load-bearing rather than a relic of the
# first arrangement.  An unknown `-mllvm` argument fails the compile, so a toolchain that
# drops the option stops the build instead of quietly re-opening the hole.
PS2_FP ?= hw
PS2_FP_OBJ     = src/drivers/ps2/ps2_builtins.ps2.o
PS2_KERNEL_OBJ = src/gl/tinygl/vertex_stage.ps2.o
PS2_WAVE_OBJ   = src/apps/xmb_wave.ps2.o
PS2_FP_OBJS    = $(PS2_FP_OBJ) $(PS2_KERNEL_OBJ) $(PS2_WAVE_OBJ)
PS2_FP_CSR_COST = -mllvm -regalloc-csr-first-time-cost=1000000000
# The flag is per object, and make does not see a flag change: switching PS2_FP leaves the
# three objects newer than their sources and links an image whose FPU law is whatever the
# previous value built -- the same disease PS2_GL has, and the same content-comparison stamp
# cures it.  Note that this `$(shell ...)` runs while *parsing*, so even `make -n PS2_FP=...`
# removes the three objects: pass the value you mean, dry runs included.
PS2_FP_STAMP = .build/ps2-fp.stamp
PS2_FP_PREV := $(shell cat $(PS2_FP_STAMP) 2>/dev/null)
ifneq ($(PS2_FP_PREV),$(PS2_FP))
$(shell mkdir -p ./.build && echo '$(PS2_FP)' >$(PS2_FP_STAMP) && rm -f $(PS2_FP_OBJS))
$(info [PS2] PS2_FP is now $(PS2_FP) (was $(if $(PS2_FP_PREV),$(PS2_FP_PREV),unset)) -- the units that read it will rebuild)
endif
ifeq ($(PS2_FP),hw)
$(PS2_FP_OBJS): PS2_CC := $(subst -msoft-float,-mhard-float,$(PS2_CC)) $(PS2_FP_CSR_COST)
$(PS2_FP_OBJS): PS2_CFLAGS += -DBTRON_PS2_FP_HW
endif

# PS2_FP_COUNT is named in three units and paid for by all of them: the counters themselves are
# incremented inside the soft-float runtime (ps2_builtins.c), xmb.c prints a stage's census from
# them, and core_ps2.c prices the classes and prints the [FPU] rows.  A link where those three
# disagree is worse than either value -- with ps2_builtins counted and xmb not, every call still
# pays the counter's load, add and store while no row explains why the microseconds moved -- and
# make cannot see the change, because the flag is not a file date.  Measured 2026-10-14: a
# supposedly-production image that `nm` showed holding `btron_fp_calls`, from a census round that
# left xmb.ps2.o newer than its source.  Same content-comparison stamp as the other dials, same
# object list as the disease, and note again that `$(shell ...)` runs while *parsing* -- a
# `make -n PS2_FP_COUNT=1` purges these too.
PS2_FPC_OBJS = src/drivers/ps2/ps2_builtins.ps2.o src/apps/xmb.ps2.o \
               src/cores/core_ps2.ps2.o src/cores/core_ps2.pbench.o
PS2_FPC_STAMP = .build/ps2-fpc.stamp
PS2_FPC_PREV := $(shell cat $(PS2_FPC_STAMP) 2>/dev/null)
ifneq ($(PS2_FPC_PREV),$(PS2_FP_COUNT))
$(shell mkdir -p ./.build && echo '$(PS2_FP_COUNT)' >$(PS2_FPC_STAMP) && rm -f $(PS2_FPC_OBJS))
$(info [PS2] PS2_FP_COUNT is now $(PS2_FP_COUNT) (was $(if $(PS2_FPC_PREV),$(PS2_FPC_PREV),unset)) -- the units that read it will rebuild)
endif

# PS2_IDENT is three units and the same disease: the hash and the row are built in eegs_present.c,
# the stage attribution column is printed by xmb.c from the present's own counter, and core_ps2.c is
# the half that prints [GSTID] -- the bench object included, because that is the one this dial is
# turned on for.  An image where eegs_present counted and core_ps2 did not print is an instrument
# nobody reads; one where xmb printed `g=` against a stream nothing hashed is a column that means
# nothing.  Same content-comparison stamp, same note that `$(shell ...)` runs while *parsing*.
PS2_IDENT_OBJS = src/gl/eegs_present.ps2.o src/apps/xmb.ps2.o \
                 src/cores/core_ps2.ps2.o src/cores/core_ps2.pbench.o
PS2_IDENT_STAMP = .build/ps2-ident.stamp
PS2_IDENT_PREV := $(shell cat $(PS2_IDENT_STAMP) 2>/dev/null)
ifneq ($(PS2_IDENT_PREV),$(PS2_IDENT))
$(shell mkdir -p ./.build && echo '$(PS2_IDENT)' >$(PS2_IDENT_STAMP) && rm -f $(PS2_IDENT_OBJS))
$(info [PS2] PS2_IDENT is now $(PS2_IDENT) (was $(if $(PS2_IDENT_PREV),$(PS2_IDENT_PREV),unset)) -- the units that read it will rebuild)
endif

ps2-bench: $(PS2_BENCH_TARGET)

# The bench TU is the LANE 19 census, and the census's cost is .rodata: every rung adds print rows, and
# `_end` climbs with them.  ps2.ld's stack reserve is a shipped-image guarantee (256 KiB, 11x the
# measured 22 KiB high-water of a desktop-plus-XMB session), and this image does not ship, so it takes
# 64 KiB -- still ~3x the measurement.  Production links without this flag and keeps the full reserve.
# If a bench run ever dies on stack, the census that prints `stack used=` is the one that will say so.
# The `=` form, not the comma: GNU ld accepts `--defsym SYM=EXPR` and `--defsym=SYM=EXPR` and rejects
# `--defsym,SYM=EXPR`, and ld.lld takes the `=` form too -- MIPS_LD is whichever of the two the host has.
$(PS2_BENCH_TARGET): $(PS2_BENCH_OBJS) $(PS2_LD_SCRIPT)
	$(MIPS_LD) -T $(PS2_LD_SCRIPT) --defsym=__ps2_stack_reserve=0x10000 $(PS2_BENCH_OBJS) -o $@
	@echo "[PS2-BENCH] Built: $@"
	@file $@

# ps2_usb.h changes the layout of ps2_ohci_probe_t, which core_ps2.c reads by
# offset.  Without generated header dependencies the two objects are built from
# different generations of that struct and the boot log prints shifted fields.
-include $(PS2_OBJS:.ps2.o=.ps2.d)

ps2: $(PS2_TARGET) $(PS2_ISO)

$(PS2_TARGET): $(PS2_OBJS) $(PS2_LD_SCRIPT)
	@echo "=========================================================="
	@echo " Building B-System PS2 Kernel: $@"
	@echo "=========================================================="
	$(MIPS_LD) -T $(PS2_LD_SCRIPT) $(PS2_OBJS) -o $@
	@echo "[PS2-ELF] Built: $@"
	@file $@

# LANE 14 Rung 1: the full-feature disc.  The image binary is FROZEN -- this rule
# does not touch the link line above it, and its own falsifying gate is that
# `make ps2` leaves btron-ps2.elf byte-identical -- so every app payload that is
# not already inside the ELF ships as a FILE on the CD9660 tree instead:
#
#   SYSTEM.CNF          the boot record the BIOS reads (cdrom0:\BTRON.ELF;1)
#   BTRON.ELF           the kernel image, unchanged
#   SYS.VOL             the /SYS volume, 1 MiB
#   ANDERS.VOL          the /ANDERS volume, 1 MiB
#   QUAKE/PAK0.PAK      18,689,235 B -- a STREAM read off the disc, never an embed
#   QUAKE/DRONE1MN.DEM  the eight replay demos
#
# Every name is 8.3-uppercase, so a level-1 Rock Ridge-less reader sees the same
# name the guest will ask for.  Both volumes are regenerated from their manifests
# INSIDE the recipe rather than copied from the working files, because a host FS
# suite writes scratch into the working btron_sys.vol -- that is exactly why the
# contamination guard at $(PS2_SYS_VOL_OBJ) exists -- and the disc must carry what
# the manifest says it carries, not whatever the last test left in the tree.
$(PS2_ISO): $(PS2_TARGET) mkbtronfs src/tools/manifest.txt src/tools/manifest_anders.txt assets/quake/pak/pak0.pak $(wildcard assets/quake/drone1m*.dem)
	@echo "=========================================================="
	@echo " Packaging Bootable PS2 Disc ISO: $@"
	@echo "=========================================================="
	@rm -rf build/ps2_iso
	@mkdir -p build/ps2_iso/QUAKE
	@printf "BOOT2 = cdrom0:\\\\BTRON.ELF;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n" > build/ps2_iso/SYSTEM.CNF
	@cp $(PS2_TARGET) build/ps2_iso/BTRON.ELF
	@./mkbtronfs src/tools/manifest.txt        -o build/ps2_iso/SYS.VOL
	@./mkbtronfs src/tools/manifest_anders.txt -o build/ps2_iso/ANDERS.VOL -l ANDERS
	@cp assets/quake/pak/pak0.pak build/ps2_iso/QUAKE/PAK0.PAK
	@for f in assets/quake/drone1m*.dem; do n=$$(basename "$$f" | tr '[:lower:]' '[:upper:]'); \
	    cp "$$f" "build/ps2_iso/QUAKE/$$n"; done
	@if command -v hdiutil >/dev/null 2>&1; then \
	    rm -f $@; \
	    hdiutil makehybrid -iso -joliet -default-volume-name "BTRON3_PS2" -o $@ build/ps2_iso >/dev/null; \
	elif command -v genisoimage >/dev/null 2>&1; then \
	    genisoimage -o $@ -V "BTRON3_PS2" build/ps2_iso >/dev/null; \
	elif command -v mkisofs >/dev/null 2>&1; then \
	    mkisofs -o $@ -V "BTRON3_PS2" build/ps2_iso >/dev/null; \
	elif command -v xorriso >/dev/null 2>&1; then \
	    xorriso -as mkisofs -o $@ -V "BTRON3_PS2" build/ps2_iso >/dev/null 2>&1; \
	fi
	@rm -rf build/ps2_iso
	@echo "[PS2-ISO] Packaged: $@"
	@echo "[PS2-ISO] ELF frozen: $$(shasum -a 1 $(PS2_TARGET) 2>/dev/null | cut -d' ' -f1) (link line untouched by this rule)"
	@file $@

run-ps: run-ps2

# Prepare the run's data directory.  PCSX2 writes the config file back on exit, so
# a run can dirty pcsx2/PCSX2/inis/PCSX2.ini in git -- revert it rather than
# delete it.  The [USB2] keyboard binding and the [Hotkeys] entries trimmed for it
# are what let a host keypress reach the emulated device at all, and the BIOS
# images are linked rather than copied because they are Sony's, not ours.
# The environment a measurement was made in, printed rather than guessed.  PS2_MSX is DERIVED from
# PS2_GL now, so reading the dial out of this file quotes a default instead of the value in force,
# and a run whose dials are not recorded cannot be re-run by somebody else.  scripts/ps2_eegs_repro.sh
# calls this first and puts the output in its manifest beside the ELF's hash.
ps2-dials:
	@echo "PS2_GL=$(PS2_GL)"
	@echo "PS2_MSX=$(PS2_MSX)"
	@echo "PS2_FP=$(PS2_FP)"
	@echo "PS2_FP_COUNT=$(PS2_FP_COUNT)"
	@echo "BENCH_APP=$(BENCH_APP)"
	@echo "BENCH_APP_FRAMES=$(BENCH_APP_FRAMES)"
	@echo "BENCH_GS=$(BENCH_GS)"
	@echo "AUTO_GUI=$(AUTO_GUI)"
	@echo "ISO=$(ISO)"
	@echo "PS2_TARGET=$(PS2_TARGET)"
	@echo "PS2_ISO=$(PS2_ISO)"

ps2-cfg:
	@if [ ! -f "$(PCSX2_DATA)/inis/PCSX2.ini" ]; then \
	    echo "[ERROR] $(PCSX2_DATA)/inis/PCSX2.ini is missing"; exit 1; fi
	@if [ ! -d "$(PCSX2_BIOS)" ]; then \
	    echo "[ERROR] No PS2 BIOS at $(PCSX2_BIOS) -- pass PCSX2_BIOS=<dir>"; exit 1; fi
	@for d in bios logs memcards sstates snaps cache; do mkdir -p "$(PCSX2_DATA)/$$d"; done
	@for f in "$(PCSX2_BIOS)"/*.bin; do n=$$(basename "$$f"); \
	    if [ ! -e "$(PCSX2_DATA)/bios/$$n" ] && [ ! -L "$(PCSX2_DATA)/bios/$$n" ]; then \
	        ln -s "$$f" "$(PCSX2_DATA)/bios/$$n"; fi; done

run-ps2: $(PS2_TARGET) ps2-cfg
	@echo "=========================================================="
	@echo " Launching B-System PS2 on PCSX2 Emulator"
	@echo " Machine  : Sony PlayStation 2 (Emotion Engine R5900)"
	@echo " CPU      : 128-bit SIMD MIPS Core @ 294.912 MHz"
	@echo " RAM      : 32 MB RDRAM | VRAM: 4 MB GS eDRAM"
	@echo " Display  : Graphics Synthesizer 800x600 @ 32-bpp RGBA (VESA)"
	@echo " GUI Mode : $(if $(filter 1,$(AUTO_GUI)),Automatic Direct GUI,Two-Stage Console -> startx)"
	@echo " Target   : $(if $(filter 1,$(ISO)),Disc ISO: $(PS2_ISO),Direct ELF: $(PS2_TARGET))"
	@echo " Emulator : $(PCSX2_BIN)"
	@echo " Config   : $(PCSX2_DATA)/inis/PCSX2.ini"
	@echo "=========================================================="
	@if [ -x "$(PCSX2_BIN)" ]; then \
	    if [ "$(ISO)" = "1" ]; then \
	        "$(PCSX2_BIN)" -datapath "$(PCSX2_DATADIR)" -fastboot $(CURDIR)/$(PS2_ISO); \
	    else \
	        "$(PCSX2_BIN)" -datapath "$(PCSX2_DATADIR)" -fastboot $(CURDIR)/$(PS2_TARGET); \
	    fi; \
	elif [ -d "/Applications/PCSX2.app" ]; then \
	    if [ "$(ISO)" = "1" ]; then \
	        open -a /Applications/PCSX2.app --args -datapath "$(PCSX2_DATADIR)" -fastboot $(CURDIR)/$(PS2_ISO); \
	    else \
	        open -a /Applications/PCSX2.app --args -datapath "$(PCSX2_DATADIR)" -fastboot $(CURDIR)/$(PS2_TARGET); \
	    fi; \
	else \
	    echo "[ERROR] PCSX2 not found at $(PCSX2_BIN)"; \
	    exit 1; \
	fi

# include/gl/math.h's large-argument trig fold, read as a shape law rather than as a
# value: past 4*pi a float angle must be folded with multiplies and no software double
# division, and not handed to the double series (whose thirteen __divdf3 are the reason
# the guard exists).  Needs the image's own flags -- and the image itself is no help,
# because sinf is static inline and dissolves into its call sites.  See
# verify/tests/ps2_farfold_probe.c; scripts/test_ps2.sh asserts the count and SKIPs, not
# PASSes, when the probe has not been built.
PS2_FARFOLD_OBJ = ./.build/ps2_farfold_probe.o

$(PS2_FARFOLD_OBJ): verify/tests/ps2_farfold_probe.c include/gl/math.h
	@mkdir -p ./.build
	$(PS2_CC) $(PS2_CFLAGS) -c $< -o $@

test-ps2: $(PS2_TARGET) $(PS2_ISO) $(PS2_FARFOLD_OBJ)
	@PS2_GL=$(PS2_GL) PS2_FP=$(PS2_FP) PS2_FP_COUNT=$(PS2_FP_COUNT) PS2_MSX=$(PS2_MSX) ./scripts/test_ps2.sh

# The PS2 image is built -msoft-float (the R5900 FPU has no double-precision
# instructions), so every floating-point operation in it runs through
# src/drivers/ps2/ps2_builtins.c -- a file no PS2-independent test can reach,
# because the compiler calls it by names that only exist at link time.  This
# compiles that same source on the host and throws each of its 32 symbols at the
# host FPU, comparing bit patterns: the only way to know the port's arithmetic is
# right before a screen can be looked at.
TEST_PSFLOAT_SRCS = verify/tests/test_ps2_softfloat.c
TEST_PSFLOAT_OBJS = $(TEST_PSFLOAT_SRCS:.c=.test.o)
TEST_PSFLOAT_BIN  = ./.build/test_ps2_softfloat

test-ps2-softfloat: $(TEST_PSFLOAT_BIN)
	@echo "=========================================================="
	@echo " Running PS2 soft-float runtime vs the host FPU..."
	@echo "=========================================================="
	@./$(TEST_PSFLOAT_BIN)

$(TEST_PSFLOAT_BIN): $(TEST_PSFLOAT_OBJS) src/drivers/ps2/ps2_builtins.c
	@mkdir -p ./.build
	$(CC) $(TEST_PSFLOAT_OBJS) -o $@ $(LDFLAGS) -lm

# The ARM32 spelling of the same instrument.  VFPv4 converts a *32-bit* core
# register to or from a floating one and nothing wider, and the Pi 2B image links
# -nostdlib, so the DAW's 64-bit sample/nanosecond arithmetic reaches the runtime
# only through names clang invents -- __aeabi_l2d, __aeabi_ul2d, __aeabi_l2f,
# __aeabi_ul2f, __aeabi_d2lz, __aeabi_f2ulz.  `make arm-elf` proves they exist; only
# this proves they are right, because a conversion that is 1 ulp off shows up as an
# audio glitch on a monitor and as nothing on a host.
TEST_ARMAEABI_SRCS = verify/tests/test_arm_aeabi.c
TEST_ARMAEABI_OBJS = $(TEST_ARMAEABI_SRCS:.c=.test.o)
TEST_ARMAEABI_BIN  = ./.build/test_arm_aeabi

test-arm-aeabi: $(TEST_ARMAEABI_BIN)
	@echo "=========================================================="
	@echo " Running ARM EABI conversion runtime vs the host FPU..."
	@echo "=========================================================="
	@./$(TEST_ARMAEABI_BIN)

$(TEST_ARMAEABI_BIN): $(TEST_ARMAEABI_OBJS) src/kernel/libaeabi.c
	@mkdir -p ./.build
	$(CC) $(TEST_ARMAEABI_OBJS) -o $@ $(LDFLAGS) -lm

# Increment 2 of the EE/GS plan of record (doc/txt/PS2.txt): the packet builder.  A GS field
# one bit off cannot be seen -- it draws a triangle somewhere the screen is not -- so the
# encoders are compiled into this program and every word is compared with a literal that
# was derived by hand from the manual's field table, the test_ps2_fb_order discipline of
# expectations that are not computed by the code they check.
TEST_GSPACK_SRCS = verify/tests/test_ps2_gs_pack.c
TEST_GSPACK_OBJS = $(TEST_GSPACK_SRCS:.c=.test.o)
TEST_GSPACK_BIN  = ./.build/test_ps2_gs_pack

test-ps2-gs-pack: $(TEST_GSPACK_BIN)
	@echo "=========================================================="
	@echo " Running PS2 GS packet builder vs hand-computed layouts..."
	@echo "=========================================================="
	@./$(TEST_GSPACK_BIN)

$(TEST_GSPACK_BIN): $(TEST_GSPACK_OBJS) src/drivers/ps2/ps2_gs_pack.c src/drivers/ps2/ps2_gs_pack.h
	@mkdir -p ./.build
	$(CC) $(TEST_GSPACK_OBJS) -o $@ $(LDFLAGS)

# Increment 3: the drawing environment and the VRAM arithmetic it rests on.  Same discipline as
# the builder's gate -- literals derived from the register tables, not from the code -- because
# the two things here are a page unit nobody can see on a screen and an eleven-word packet whose
# registers must arrive in the order the hardware expects.
TEST_GSENV_SRCS = verify/tests/test_ps2_gs_env.c
TEST_GSENV_OBJS = $(TEST_GSENV_SRCS:.c=.test.o)
TEST_GSENV_BIN  = ./.build/test_ps2_gs_env

test-ps2-gs-env: $(TEST_GSENV_BIN)
	@echo "=========================================================="
	@echo " Running PS2 GS draw environment and VRAM budget..."
	@echo "=========================================================="
	@./$(TEST_GSENV_BIN)

$(TEST_GSENV_BIN): $(TEST_GSENV_OBJS) src/drivers/ps2/ps2_gs_env.c src/drivers/ps2/ps2_gs_vram.c
	@mkdir -p ./.build
	$(CC) $(TEST_GSENV_OBJS) -o $@ $(LDFLAGS)

# Increment 4: the vertex recorder.  Same idiom as the two gates above -- the recorder and the
# packer are #included into the test, so the identical translation unit is what the gate measures
# and what links into btron-ps2.elf -- and the same reason: a GIF group whose NLOOP counts
# quadwords instead of vertices, or a vertex left in pixels instead of 12.4, is a packet the GS
# decodes happily and draws in the wrong place.  The buffer is overridden to 64 quadwords by the
# test's own #define so the split paths are reachable without ten thousand vertices.
TEST_GSREC_SRCS = verify/tests/test_ps2_gs_rec.c
TEST_GSREC_OBJS = $(TEST_GSREC_SRCS:.c=.test.o)
TEST_GSREC_BIN  = ./.build/test_ps2_gs_rec

test-ps2-gs-rec: $(TEST_GSREC_BIN)
	@echo "=========================================================="
	@echo " Running PS2 GS vertex recorder vs hand-built GIF groups..."
	@echo "=========================================================="
	@./$(TEST_GSREC_BIN)

$(TEST_GSREC_BIN): $(TEST_GSREC_OBJS) src/drivers/ps2/ps2_gs_rec.c src/drivers/ps2/ps2_gs_pack.c src/drivers/ps2/ps2_gs_rec.h src/drivers/ps2/ps2_gs_pack.h verify/tests/gs_group_view.h
	@mkdir -p ./.build
	$(CC) $(TEST_GSREC_OBJS) -o $@ $(LDFLAGS)

TEST_VU1GEOM_SRCS = verify/tests/test_ps2_vu1_geom.c
TEST_VU1GEOM_OBJS = $(TEST_VU1GEOM_SRCS:.c=.test.o)
TEST_VU1GEOM_BIN  = ./.build/test_ps2_vu1_geom

test-ps2-vu1-geom: $(TEST_VU1GEOM_BIN)
	@echo "=========================================================="
	@echo " Running PS2 VU1 geometry pipeline unit tests..."
	@echo "=========================================================="
	@./$(TEST_VU1GEOM_BIN)

$(TEST_VU1GEOM_BIN): $(TEST_VU1GEOM_OBJS) src/drivers/ps2/ps2_vu1_geom.c src/drivers/ps2/ps2_vu1_geom.h
	@mkdir -p ./.build
	$(CC) $(TEST_VU1GEOM_OBJS) -o $@ $(LDFLAGS)

TEST_VU0KERNEL_SRCS = verify/tests/test_ps2_vu0_kernel.c
TEST_VU0KERNEL_OBJS = $(TEST_VU0KERNEL_SRCS:.c=.test.o)
TEST_VU0KERNEL_BIN  = ./.build/test_ps2_vu0_kernel

test-ps2-vu0-kernel: $(TEST_VU0KERNEL_BIN)
	@echo "=========================================================="
	@echo " Running PS2 VU0 procedural geometry kernel unit tests..."
	@echo "=========================================================="
	@./$(TEST_VU0KERNEL_BIN)

$(TEST_VU0KERNEL_BIN): $(TEST_VU0KERNEL_OBJS) src/drivers/ps2/ps2_vu0_kernel.c src/drivers/ps2/ps2_vu0_kernel.h
	@mkdir -p ./.build
	$(CC) $(TEST_VU0KERNEL_OBJS) -o $@ $(LDFLAGS)

TEST_GSCUBE_SRCS = verify/tests/test_gscube_nexus.c
TEST_GSCUBE_OBJS = $(TEST_GSCUBE_SRCS:.c=.test.o)
TEST_GSCUBE_BIN  = ./.build/test_gscube_nexus

test-gscube-nexus: $(TEST_GSCUBE_BIN)
	@echo "=========================================================="
	@echo " Running GScube cluster scaling engine unit tests..."
	@echo "=========================================================="
	@./$(TEST_GSCUBE_BIN)

$(TEST_GSCUBE_BIN): $(TEST_GSCUBE_OBJS) src/gl/gscube_nexus.c src/gl/gscube_nexus.h
	@mkdir -p ./.build
	$(CC) $(TEST_GSCUBE_OBJS) -o $@ $(LDFLAGS)

TEST_PS2ROUTING_BIN = ./.build/test_ps2_opengl_routing

test-ps2-opengl-routing: $(TEST_PS2ROUTING_BIN)
	@echo "=========================================================="
	@echo " Running PS2 clean-room OpenGL routing & gterm test..."
	@echo "=========================================================="
	@./$(TEST_PS2ROUTING_BIN)

$(TEST_PS2ROUTING_BIN): verify/tests/test_ps2_opengl_routing.c $(FS_OBJS) src/clu/clu.host.o $(CLU_APPS_HOST_OBJS) \
                        src/gl/backend_eegs.c src/drivers/ps2/ps2_gs_rec.c src/drivers/ps2/ps2_gs_tex.c \
                        src/drivers/ps2/ps2_vu1_geom.c src/drivers/ps2/ps2_vu0_kernel.c \
                        src/gl/gscube_nexus.c src/drivers/ps2/ps2_gs_env.c src/drivers/ps2/ps2_gs_vram.c \
                        src/drivers/ps2/ps2_gif_ring.c
	@mkdir -p ./.build
	$(CC) $(CFLAGS) -Isrc verify/tests/test_ps2_opengl_routing.c $(FS_OBJS) src/clu/clu.host.o $(CLU_APPS_HOST_OBJS) src/gl/backend_eegs.c src/drivers/ps2/ps2_gs_rec.c src/drivers/ps2/ps2_gs_tex.c -o $@ $(LDFLAGS) -lm


# LANE 9b: the flat-colour SPRITE batch, the poster half of row 9.  Same #include idiom as the three
# gates above, with one difference that is not a style choice: the TU used to ship host-only because
# nothing in the image called it and this port's image has a size law ps2.ld ASSERTs.  Its first
# caller landed as LANE 19 rung 1 -- src/cores/core_ps2.c's `gsquad` probe, which sends one SPRITE to
# answer whether the GS rasterises on the displayed page at all -- so it is in PS2_SRCS now, and the
# caller's own gate is still the row that says a batch reached channel 2 (`[GSQUAD] bus:`).  What
# remains unshipped is row 9b's PRODUCTION caller, the background plate, which the census priced out
# (doc/txt/PS2.txt, LANE 9 Price).  What this suite owns in the meantime is the three things that
# cannot be seen on a screen and cannot be derived from the code: the colour word (a poster read in
# the desktop's own byte order paints teal as brown at an identical packet length), the corner word
# (compared against gs_vertex_coord16() over every coordinate the field holds, so the integer shift
# is shown to be the recorder's encoding rather than a rival one), and NLOOP (which must shrink with
# the rects a refusal removes, or the DMA's QWC is shorter than the tag and the GIF decodes the
# overflow as another packet).
TEST_GSPOSTER_SRCS = verify/tests/test_ps2_gs_poster.c
TEST_GSPOSTER_OBJS = $(TEST_GSPOSTER_SRCS:.c=.test.o)
TEST_GSPOSTER_BIN  = ./.build/test_ps2_gs_poster

test-ps2-gs-poster: $(TEST_GSPOSTER_BIN)
	@echo "=========================================================="
	@echo " Running PS2 GS poster batch vs hand-built SPRITE groups..."
	@echo "=========================================================="
	@./$(TEST_GSPOSTER_BIN)

$(TEST_GSPOSTER_BIN): $(TEST_GSPOSTER_OBJS) src/drivers/ps2/ps2_gs_poster.c \
                      src/drivers/ps2/ps2_gs_poster.h \
                      src/drivers/ps2/ps2_gs_pack.c src/drivers/ps2/ps2_gs_pack.h
	@mkdir -p ./.build
	$(CC) $(TEST_GSPOSTER_OBJS) -o $@ $(LDFLAGS)

# Increment 5b: the texture-plane registry.  Same idiom as the three gates above -- the registry
# and the two units it plans against are #included into the test -- and the same reason: a plane
# given the wrong 256-byte block is a triangle that samples its neighbour's texels, which draws
# something and draws it wrong, with nothing on the screen to say which of the four fields was
# off.  The three words are compared against literals written from the register tables, the spans
# against the budget doc/txt/PS2.txt states, and the sink against the law that a plane crosses the
# moment its bytes exist.
TEST_GSTEX_SRCS = verify/tests/test_ps2_gs_tex.c
TEST_GSTEX_OBJS = $(TEST_GSTEX_SRCS:.c=.test.o)
TEST_GSTEX_BIN  = ./.build/test_ps2_gs_tex

test-ps2-gs-tex: $(TEST_GSTEX_BIN)
	@echo "=========================================================="
	@echo " Running PS2 GS texture-plane registry vs hand-built TEX0..."
	@echo "=========================================================="
	@./$(TEST_GSTEX_BIN)

$(TEST_GSTEX_BIN): $(TEST_GSTEX_OBJS) src/drivers/ps2/ps2_gs_tex.c \
                   src/drivers/ps2/ps2_gs_pack.c src/drivers/ps2/ps2_gs_vram.c \
                   src/drivers/ps2/ps2_gs_tex.h src/drivers/ps2/ps2_gs_pack.h \
                   src/drivers/ps2/ps2_gs_vram.h
	@mkdir -p ./.build
	$(CC) $(TEST_GSTEX_OBJS) -o $@ $(LDFLAGS)

# LANE 19 rung 2: the Local->Host read request.  Same #include idiom as the four gates above, and a
# sharper reason to use it: every other unit in this suite draws something, so a wrong word is a
# wrong pixel -- visible, if anyone can see.  A read request that is wrong produces NO pixels and
# says nothing about why, and the census it returns is indistinguishable from a screen that really
# is black.  So the words themselves are the only part of this instrument that can be verified
# without eyes on the GS, and this gate owns exactly that: the VIF code quadword (FLUSHA must precede
# DIRECT, and DIRECT's immediate is the PACKET's 6 quadwords -- count the block's 7 and VIF1 waits
# forever for a quadword that does not exist), the five AD words against literals written from the
# register tables, and the refusal table, which is what gs_read_last_err() exists to make a single
# number: "the download was refused because the rect does not end on a 16-byte boundary" is a
# different fact from "the download ran and the page is black".  Two of the cases are controls with
# opposite answers on purpose -- a BITBLTBUF built as DBA/DBW instead of SBA/SBW is the same LENGTH,
# so only the field-by-field comparison catches it, and CT24 must accept x=16/w=16 while refusing
# x=4/w=4, which is PCSX2's own "transfer rectangles are exact multiples of a qword" law expressed in
# bytes rather than in pixels.  The bus turn that consumes these words -- DMAC channel 1, VIF1, the
# FINISH receipt -- is ps2_gs_vram_read(); its first caller is the `gsread` Stage-1 verb.
TEST_GSREAD_SRCS = verify/tests/test_ps2_gs_read.c
TEST_GSREAD_OBJS = $(TEST_GSREAD_SRCS:.c=.test.o)
TEST_GSREAD_BIN  = ./.build/test_ps2_gs_read

test-ps2-gs-read: $(TEST_GSREAD_BIN)
	@echo "=========================================================="
	@echo " Running PS2 GS Local->Host request vs hand-computed packet words..."
	@echo "=========================================================="
	@./$(TEST_GSREAD_BIN)

$(TEST_GSREAD_BIN): $(TEST_GSREAD_OBJS) src/drivers/ps2/ps2_gs_read.c \
                    src/drivers/ps2/ps2_gs_pack.c src/drivers/ps2/ps2_gs_vram.c \
                    src/drivers/ps2/ps2_gs_read.h src/drivers/ps2/ps2_gs_pack.h \
                    src/drivers/ps2/ps2_gs_vram.h
	@mkdir -p ./.build
	$(CC) $(TEST_GSREAD_OBJS) -o $@ $(LDFLAGS)

# The pointer's two-speed pair, on the PS2 side only.  The splitter lives in a header src/cores
# includes, so the test #includes that same header and measures the identical code the image links
# -- which matters here more than anywhere else in this suite, because the row it prints is the one
# that decides whether a guest gain can be shipped at all, and it is read in a session whose screen
# nobody can see.  Two of the cases are controls with opposite answers: counts that proportion to
# distance must read 1.00x and counts that proportion to speed must read as acceleration, and a
# harness that prints 1.00x for both is not an instrument.
TEST_PTRSTROKE_SRCS = verify/tests/test_ps2_ptr_stroke.c
TEST_PTRSTROKE_OBJS = $(TEST_PTRSTROKE_SRCS:.c=.test.o)
TEST_PTRSTROKE_BIN  = ./.build/test_ps2_ptr_stroke

# The accumulation differential, compiled with the PS2 dial because it reads the dial's own table:
# dp_accel.c is #included, so the arithmetic the gate checks is the arithmetic that links into
# btron-ps2.elf.  A private object suffix, not the shared %.test.o rule, because -DBTRON_PS2_TARGET
# must not reach any other suite's object through a pattern rule.
TEST_PTRACCUM_OBJ = verify/tests/test_ps2_ptr_accum.accum.o
TEST_PTRACCUM_BIN = ./.build/test_ps2_ptr_accum

test-ps2-pointer: $(TEST_PTRSTROKE_BIN) $(TEST_PTRACCUM_BIN)
	@echo "=========================================================="
	@echo " Running PS2 pointer stroke segmentation and the two-speed verdict..."
	@echo "=========================================================="
	@./$(TEST_PTRSTROKE_BIN)
	@echo ""
	@echo "=========================================================="
	@echo " Running PS2 pointer accumulation differential (dp_accel.c)..."
	@echo "=========================================================="
	@./$(TEST_PTRACCUM_BIN)

$(TEST_PTRSTROKE_BIN): $(TEST_PTRSTROKE_OBJS) src/cores/ps2_ptr_stroke.h
	@mkdir -p ./.build
	$(CC) $(TEST_PTRSTROKE_OBJS) -o $@ $(LDFLAGS)

$(TEST_PTRACCUM_OBJ): verify/tests/test_ps2_ptr_accum.c src/graphics/dp_accel.c include/btron/dp_accel.h
	$(CC) $(CFLAGS) -DBTRON_PS2_TARGET -MMD -MP -c $< -o $@

$(TEST_PTRACCUM_BIN): $(TEST_PTRACCUM_OBJ)
	$(CC) $(TEST_PTRACCUM_OBJ) -o $@ $(LDFLAGS)

# LANE 9c: the composite's row rule, after the walk's second read of the same band was removed.
# The header is #included rather than linked, so the identical code is what the gate checks and
# what wnd.c compiles on every port.  The suite runs TWICE, once per side of the port dial, and
# that is not decoration: the one fact a single run cannot see is the dial itself, because the
# dispatcher's output is compared against the shape its #if names -- an inverted #if would ship a
# row that passes every byte test on both ports, and only the second binary calls it wrong.
TEST_FUSEDROW_SRCS = verify/tests/test_ps2_fused_row.c
TEST_FUSEDROW_OBJS = $(TEST_FUSEDROW_SRCS:.c=.test.o)
TEST_FUSEDROW_BIN  = ./.build/test_ps2_fused_row
TEST_FUSEDROW_DIAL_OBJ = verify/tests/test_ps2_fused_row.ps2dial.o
TEST_FUSEDROW_DIAL_BIN = ./.build/test_ps2_fused_row_ps2dial

test-ps2-fused-row: $(TEST_FUSEDROW_BIN) $(TEST_FUSEDROW_DIAL_BIN)
	@echo "=========================================================="
	@echo " Running the composite row rule -- host dial, then the PS2 dial..."
	@echo "=========================================================="
	@./$(TEST_FUSEDROW_BIN)
	@./$(TEST_FUSEDROW_DIAL_BIN)

$(TEST_FUSEDROW_BIN): $(TEST_FUSEDROW_OBJS) include/btron/fused_row.h include/btron/fast_blit.h
	@mkdir -p ./.build
	$(CC) $(TEST_FUSEDROW_OBJS) -o $@ $(LDFLAGS)

# The forced dial, compiled from the same source with the PS2 target macro: this is the shape
# btron-ps2.elf carries, and the flag is the only thing that selects it.
$(TEST_FUSEDROW_DIAL_OBJ): verify/tests/test_ps2_fused_row.c include/btron/fused_row.h
	$(CC) $(CFLAGS) -DBTRON_PS2_TARGET -MMD -MP -c $< -o $@

$(TEST_FUSEDROW_DIAL_BIN): $(TEST_FUSEDROW_DIAL_OBJ) include/btron/fused_row.h
	@mkdir -p ./.build
	$(CC) $(TEST_FUSEDROW_DIAL_OBJ) -o $@ $(LDFLAGS)

# Increment 5a: the raster stage's decisions, on top of the recorder's words.  Same idiom again --
# backend_eegs.c and the recorder are #included into the test, so the identical translation unit is
# what the gate measures and what will link into btron-ps2.elf -- and the same reason: a triangle
# that asks for the wrong PRIM, or a vertex colour whose bytes are unpacked in the wrong order, is
# a packet the GS decodes happily and draws in the wrong colour.  The suite is the one law in this
# port a hand literal alone can pin, so it is compiled before any of it reaches a screen.
TEST_EEGSR_SRCS = verify/tests/test_eegs_raster.c
TEST_EEGSR_OBJS = $(TEST_EEGSR_SRCS:.c=.test.o)
TEST_EEGSR_BIN  = ./.build/test_eegs_raster

test-eegs-raster: $(TEST_EEGSR_BIN)
	@echo "=========================================================="
	@echo " Running PS2 EE/GS raster stage vs hand-built GIF groups..."
	@echo "=========================================================="
	@./$(TEST_EEGSR_BIN)

$(TEST_EEGSR_BIN): $(TEST_EEGSR_OBJS) src/gl/backend_eegs.c src/gl/backend_eegs.h \
                   src/drivers/ps2/ps2_gs_rec.c src/drivers/ps2/ps2_gs_pack.c \
                   src/drivers/ps2/ps2_gs_rec.h src/drivers/ps2/ps2_gs_pack.h \
                   verify/tests/gs_group_view.h
	@mkdir -p ./.build
	$(CC) $(TEST_EEGSR_OBJS) -o $@ $(LDFLAGS)

# LANE 12f gate (a): the VALUES.  `tgl_grid_map()` and `tgl_grid_map_points()` are a second encoding
# of `tgl_vertex_to_viewport()`, and the only honest comparator for a second encoding is the first
# one, so this suite links TinyGL and drives the real API -- glViewport, the app's own
# glOrtho(0,w,h,0,-1,1), identity modelview, blend on -- over a quarter-pixel lattice and the channel
# byte's own flip points, and requires BIT equality on both device pixels and all four colour bytes.
# -ffp-contract=off is insurance, not the load-bearing half of this gate: the Emotion Engine has no
# FMA at all, so `off` is the setting under which the host runs the IMAGE's arithmetic (the same
# reasoning is in that file's header), and it has to reach every TU in the link or the comparison
# would be between two different compilers rather than two encodings.  Measured 2026-10-14: dropping
# the flag leaves this suite green on Apple clang 21 / arm64, which does not contract these
# statements; it would not be green under gcc's default -ffp-contract=fast.  What does bite is the
# precision of the truncate -- a double-intermediate map loop moves 1,932 device pixels at 640x480 and
# none of them is visible on an interior lattice point; see the header of the suite.
# One trap for whoever runs those mutations: restoring vertex_stage.c with `cp` can leave the source
# and its *.gm.o with the same second of mtime, and make then re-runs the MUTATED object and reports
# failures that are no longer in the tree.  Delete the object, do not re-touch the source.
# backend_virgl.c is here as it is in the other GL suites: the GL_BACKEND_VIRGL case in gl_init is
# unconditional in the shipped image too.
TEST_GRIDMAP_SRCS = verify/tests/test_grid_map.c src/gl/gl_dispatch.c src/gl/backend_tinygl.c \
                    src/gl/backend_virgl.c $(TINYGL_SRCS) src/apps/xmb_wave.c
TEST_GRIDMAP_OBJS = $(TEST_GRIDMAP_SRCS:.c=.gm.o)
TEST_GRIDMAP_BIN  = ./.build/test_grid_map

%.gm.o: %.c
	$(CC) $(CFLAGS) -ffp-contract=off -DBTRON_GL_BACKEND_TINYGL -MMD -MP -c $< -o $@

test-grid-map: $(TEST_GRIDMAP_BIN)
	@echo "=========================================================="
	@echo " Running LANE 12f gate (a): the batch's map vs the vertex walk itself..."
	@echo "=========================================================="
	@./$(TEST_GRIDMAP_BIN)

$(TEST_GRIDMAP_BIN): $(TEST_GRIDMAP_OBJS) src/gl/grid_map.h src/gl/tinygl/vertex_stage.c
	@mkdir -p ./.build
	$(CC) $(TEST_GRIDMAP_OBJS) -o $@ $(LDFLAGS) -lm

# Increment 5c: the present's own law, which is an order and two counts rather than a set of words.
# eegs_present.c is #included with the stage, the recorder, the packer, the allocator and the
# environment builder, and the bus is the one stub -- so what the suite can see is exactly what a
# frame puts on DMAC channel 2 and in which order.  The environment-then-primitives half is the
# rule the hardware forces (a stream sent before the canvas upload is a stream the upload erases);
# the copy-not-alias half is differential, because the recorder reuses its buffer and each group is
# individually correct at the instant it is handed over -- which no word gate can notice.
TEST_EEGSP_SRCS = verify/tests/test_eegs_present.c
TEST_EEGSP_OBJS = $(TEST_EEGSP_SRCS:.c=.test.o)
TEST_EEGSP_BIN  = ./.build/test_eegs_present

test-eegs-present: $(TEST_EEGSP_BIN)
	@echo "=========================================================="
	@echo " Running PS2 EE/GS present: order, staging, capacity..."
	@echo "=========================================================="
	@./$(TEST_EEGSP_BIN)

$(TEST_EEGSP_BIN): $(TEST_EEGSP_OBJS) src/gl/eegs_present.c src/gl/eegs_present.h \
                   src/gl/backend_eegs.c src/gl/backend_eegs.h \
                   src/drivers/ps2/ps2_gs_rec.c src/drivers/ps2/ps2_gs_pack.c \
                   src/drivers/ps2/ps2_gs_env.c src/drivers/ps2/ps2_gs_vram.c \
                   src/drivers/ps2/ps2_gif_ring.c src/drivers/ps2/ps2_gif_ring.h \
                   src/drivers/ps2/ps2_gs.h verify/tests/gs_group_view.h
	@mkdir -p ./.build
	$(CC) $(TEST_EEGSP_OBJS) -o $@ $(LDFLAGS)

# LANE 10: the GIF writer ring, gated on a machine with no GIF in it.  This is the reason the ring
# is a translation unit of its own with three hooks rather than three more lines inside
# ps2_gs_gif_send(): "a producer is refused or accepted, never blocked" and "a completion check
# leaves nothing outstanding" are properties of a queue, and a queue can be driven against a channel
# that is held busy forever -- which is not a state any image row can conjure.  The capacity here is
# the suite's own 8 (overridden in the test source) so the refusal path and the index wrap are
# reachable; the shipped 512 and its relation to the stage's group table is compile-time law in
# src/gl/eegs_present.c, and the ordering a refusal would corrupt is asserted through the real
# staging unit, just above.
TEST_GIFRING_SRCS = verify/tests/test_ps2_gif_ring.c
TEST_GIFRING_OBJS = $(TEST_GIFRING_SRCS:.c=.test.o)
TEST_GIFRING_BIN  = ./.build/test_ps2_gif_ring

test-ps2-gif-ring: $(TEST_GIFRING_BIN)
	@echo "=========================================================="
	@echo " Running PS2 LANE 10: the GIF writer ring..."
	@echo "=========================================================="
	@./$(TEST_GIFRING_BIN)

$(TEST_GIFRING_BIN): $(TEST_GIFRING_OBJS) src/drivers/ps2/ps2_gif_ring.c \
                     src/drivers/ps2/ps2_gif_ring.h
	@mkdir -p ./.build
	$(CC) $(TEST_GIFRING_OBJS) -o $@ $(LDFLAGS)

# Task #165: the per-app paint policy, gated on a machine with no compositor in it, in the shape
# LANE 10's ring established.  The claims this module makes are all about states the shipped image
# cannot conjure -- a frame whose remaining slice is smaller than one window's price, the same
# window asked to paint by four bands in one pass, a job so expensive it would never be admitted
# again -- and a policy that has only ever run inside a live paint pass has not been shown to
# refuse anything.  The suite is #include-the-source again (PS2_PW_MAX 4 so capacity is reachable)
# because the table is file-static and the whole law is who gets in and who does not.
TEST_PAINTW_SRCS = verify/tests/test_ps2_paint_worker.c
TEST_PAINTW_OBJS = $(TEST_PAINTW_SRCS:.c=.test.o)
TEST_PAINTW_BIN  = ./.build/test_ps2_paint_worker

test-ps2-paintw: $(TEST_PAINTW_BIN)
	@echo "=========================================================="
	@echo " Running PS2 #165: the per-app paint budget and dedup..."
	@echo "=========================================================="
	@./$(TEST_PAINTW_BIN)

$(TEST_PAINTW_BIN): $(TEST_PAINTW_OBJS) src/drivers/ps2/ps2_paint_worker.c \
                    src/drivers/ps2/ps2_paint_worker.h
	@mkdir -p ./.build
	$(CC) $(TEST_PAINTW_OBJS) -o $@ $(LDFLAGS)

# The Makefile's `PS2_GL=` dial, measured against the rule it is supposed to drive.  The dial is
# three -D flags and the rule is a preprocessor branch, so one binary compiled with one value of
# it cannot see the other values -- which is exactly how a flag that names nothing at all looks
# from the inside.  This suite links src/gl/gl_backend_choice.c four times, each port TU carrying
# the flags spelled as PS2_CFLAGS spells them, and compares all four answers.
TEST_GLBLANE_SRCS = verify/tests/test_gl_backend_choice.c
TEST_GLBLANE_BIN  = ./.build/test_gl_backend_choice
GLBLANE_CFLAGS    = $(CFLAGS) -DBTRON_GL_BACKEND_TINYGL

# The measuring TU takes the plain flags: it names the four ports' functions and includes no
# implementation, so which arm of the rule it was compiled under would be meaningless.
%.blc.o: %.c
	@mkdir -p ./.build
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

test-gl-backend-choice: $(TEST_GLBLANE_BIN)
	@echo "=========================================================="
	@echo " Running PS2 GL backend selection (Makefile PS2_GL= dial vs rule)..."
	@echo "=========================================================="
	@./$(TEST_GLBLANE_BIN)

verify/tests/gl_backend_choice_plain_port.blc.o: verify/tests/gl_backend_choice_plain_port.c src/gl/gl_backend_choice.c src/gl/gl_backend_choice.h
	@mkdir -p ./.build
	$(CC) $(GLBLANE_CFLAGS) -MMD -MP -c $< -o $@

verify/tests/gl_backend_choice_virgl_port.blc.o: verify/tests/gl_backend_choice_virgl_port.c src/gl/gl_backend_choice.c src/gl/gl_backend_choice.h
	@mkdir -p ./.build
	$(CC) $(GLBLANE_CFLAGS) -DBTRON_GL_PREFER_VIRGL -MMD -MP -c $< -o $@

verify/tests/gl_backend_choice_eegs_port.blc.o: verify/tests/gl_backend_choice_eegs_port.c src/gl/gl_backend_choice.c src/gl/gl_backend_choice.h
	@mkdir -p ./.build
	$(CC) $(GLBLANE_CFLAGS) -DBTRON_GL_PREFER_EEGS -MMD -MP -c $< -o $@

# No -DBTRON_GL_BACKEND_TINYGL: the arm every target that does not list src/gl/tinygl/ compiles
# under, and the one the dial must leave alone.
verify/tests/gl_backend_choice_nostgl_port.blc.o: verify/tests/gl_backend_choice_nostgl_port.c src/gl/gl_backend_choice.c src/gl/gl_backend_choice.h
	@mkdir -p ./.build
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(TEST_GLBLANE_BIN): $(TEST_GLBLANE_SRCS:.c=.blc.o) \
                     verify/tests/gl_backend_choice_plain_port.blc.o \
                     verify/tests/gl_backend_choice_virgl_port.blc.o \
                     verify/tests/gl_backend_choice_eegs_port.blc.o \
                     verify/tests/gl_backend_choice_nostgl_port.blc.o
	@mkdir -p ./.build
	$(CC) $^ -o $@ $(LDFLAGS)

# include/gl/math.h is the freestanding math for every bare-metal target, and its
# no-x87 half is what the PS2 and the Malta MIPS actually execute: sqrtf/sinf/cosf/
# floorf as single-precision series, because the Emotion Engine runs single-precision
# on COP1 in ~20 cycles and double-precision not at all.  A wrong constant factor in
# one of them is invisible on a screen and unarguable without one, so each is measured
# against the host's libm per binade and per reduction region.  Two TUs because the
# shim's definitions are static inline under the names libm owns -- see
# verify/tests/gl_math_float_port.c.
TEST_GLMATH_BIN = ./.build/test_gl_math_float

# The port TU needs the branch forced two ways (BTRON_UEFI_TARGET inside the file, the
# ISA macros here) and needs the compiler to leave its own functions alone; at -O2, or
# with a -march=native default, a function named sqrtf is folded back into the hardware
# instruction and the run measures the machine it is standing on.  -fno-builtin-<name>
# is per-name: extend it in lockstep when the header gains a float-native function.
GLMATH_PORTFLAGS = -O1 -std=c11 -Wall -Wextra -ffreestanding \
                   -U__x86_64__ -U__i386__ -Iinclude -Iinclude/gl \
                   -fno-builtin-sqrt -fno-builtin-sqrtf -fno-builtin-sin -fno-builtin-sinf \
                   -fno-builtin-cos -fno-builtin-cosf -fno-builtin-fabs -fno-builtin-fabsf \
                   -fno-builtin-floor -fno-builtin-floorf -fno-builtin-exp -fno-builtin-expf \
                   -fno-builtin-pow -fno-builtin-powf -fno-builtin-atan -fno-builtin-atanf \
                   -fno-builtin-atan2 -fno-builtin-atan2f -fno-builtin-fmod -fno-builtin-fmodf

verify/tests/gl_math_float_port.test.o: verify/tests/gl_math_float_port.c include/gl/math.h
	$(CC) $(GLMATH_PORTFLAGS) -MMD -MP -c $< -o $@

test-gl-math: $(TEST_GLMATH_BIN)
	@echo "=========================================================="
	@echo " Running gl/math.h float-native series vs the host libm..."
	@echo "=========================================================="
	@./$(TEST_GLMATH_BIN)

$(TEST_GLMATH_BIN): verify/tests/test_gl_math_float.test.o verify/tests/gl_math_float_port.test.o
	@mkdir -p ./.build
	$(CC) $^ -o $@ $(LDFLAGS) -lm


# ═══════════════════════════════════════════════════════════════════
# PS2 canvas byte order -- one scene, both PIXEL domains, twice built
# ═══════════════════════════════════════════════════════════════════
# The GS reads CT32 memory as R,G,B,A and a BTRON COLOR is 0xAARRGGBB, so every canvas word
# the PS2 scans out has its R and B fields exchanged (include/btron/types.h), and TinyGL's
# whole PIXEL domain is declared in that order too because its pbuf *is* the canvas
# (src/gl/tinygl/zbuffer.h).  Both are compile-time decisions with no visible symptom until
# a colour is on a screen, and the macro that decides them appears in exactly three files --
# so this gate builds the same scene twice, once per domain, normalises each canvas word
# back through BTRON_PIXEL_FB(), and requires the two dumps to be byte-identical.  A store
# site that never converted itself, or a read of a canvas word that bypassed the macros,
# shows up as one differing word at a known offset; the program's own raw-word assertions
# then say which half of the flip failed.  See verify/tests/test_fb_order.c.
TEST_FBORDER_SRCS = verify/tests/test_fb_order.c src/graphics/dp_core.c \
                    src/font/troncode.c src/font/jis_fonts.c src/font/tibetan_fonts.c \
                    src/gl/gl_dispatch.c src/gl/backend_virgl.c \
                    src/gl/gl_backend_choice.c \
                    src/gl/backend_eegs.c \
                    src/drivers/ps2/ps2_gs_rec.c src/drivers/ps2/ps2_gs_pack.c \
                    src/drivers/ps2/ps2_gs_tex.c src/drivers/ps2/ps2_gs_vram.c \
                    $(TINYGL_SRCS)
TEST_FBORDER_COBJS = $(TEST_FBORDER_SRCS:.c=.fbo.o)
TEST_FBORDER_POBJS = $(TEST_FBORDER_SRCS:.c=.fbp.o)
TEST_FBORDER_CBIN  = ./.build/test_fb_order
TEST_FBORDER_PBIN  = ./.build/test_fb_order_canvas

%.fbo.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# The only difference between the two builds.  Two files read BTRON_PS2_TARGET besides
# src/cores/core_init.c (which is not linked here): src/gl/tinygl/clip.c's GS raster stage and
# src/gl/tinygl/texture.c's 5b plane publish, and therefore the five units they call -- the
# recorder, the packer, the registry and the VRAM arithmetic it plans against -- which is why they
# are in the list above for *both* builds.  The two binaries have to differ in one macro and
# nothing else, or a PASS would only say that two different links agree.
%.fbp.o: %.c
	$(CC) $(CFLAGS) -DBTRON_PS2_TARGET -MMD -MP -c $< -o $@

test-ps2-fb-order: $(TEST_FBORDER_CBIN) $(TEST_FBORDER_PBIN)
	@echo "=========================================================="
	@echo " PS2 canvas byte order: COLOR-order build vs canvas-order build..."
	@echo "=========================================================="
	@mkdir -p ./.build
	@./$(TEST_FBORDER_CBIN) ./.build/fb_order_color.u32
	@./$(TEST_FBORDER_PBIN)  ./.build/fb_order_canvas.u32
	@if cmp -s ./.build/fb_order_color.u32 ./.build/fb_order_canvas.u32; then \
		echo "[FBORDER] PASS: both domains normalise to the same 31,488 words"; \
	else \
		echo "[FBORDER] FAIL: a word of the canvas-order build is not BTRON_PIXEL_FB()"; \
		echo "[FBORDER]        of the same word in the COLOR-order build.  The byte offset"; \
		echo "[FBORDER]        (1-based, from cmp -l below) / 4 gives the word index:"; \
		echo "[FBORDER]        0..19199 the 160x120 display-primitive canvas, 19200..25343"; \
		echo "[FBORDER]        the clear-only GL canvas, 25344..31487 the GL frame."; \
		cmp -l ./.build/fb_order_color.u32 ./.build/fb_order_canvas.u32 | head -24; \
		exit 1; \
	fi

$(TEST_FBORDER_CBIN): $(TEST_FBORDER_COBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_FBORDER_COBJS) -o $@ $(LDFLAGS) -lm

$(TEST_FBORDER_PBIN): $(TEST_FBORDER_POBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_FBORDER_POBJS) -o $@ $(LDFLAGS) -lm

-include $(TEST_FBORDER_COBJS:.o=.d) $(TEST_FBORDER_POBJS:.o=.d)


# ═══════════════════════════════════════════════════════════════════
# Bare-Metal MIPS Malta / Magnum Kernel (mips / QEMU) [Target 9]
# ═══════════════════════════════════════════════════════════════════
MIPS_TARGET     = ./.build/btron-mips.elf
MIPS_LD_SCRIPT  = src/drivers/mips/mips_qemu.ld
MIPS_CFLAGS     = -O2 -Wall -Wextra -std=c99 -ffreestanding -nostdlib \
                  -DBTRON_TARGET=9 -DBTRON_MIPS_TARGET \
                  -Iinclude -Iinclude/drivers -Isrc/kernel -Isrc/cores -Isrc/drivers/mips
MIPS_STARTUP    = src/cores/core_mips.c
MIPS_SRCS       = $(MIPS_STARTUP)             \
                  src/drivers/mips/mips_uart.c \
                  src/kernel/libstr.c
MIPS_OBJS       = src/drivers/mips/boot_mips.mips.o $(MIPS_SRCS:.c=.mips.o)

%.mips.o: %.s
	$(MIPS_CC) -c $< -o $@

%.mips.o: %.c
	$(MIPS_CC) $(MIPS_CFLAGS) -c $< -o $@

mips: $(MIPS_TARGET)

$(MIPS_TARGET): $(MIPS_OBJS) $(MIPS_LD_SCRIPT)
	@echo "=========================================================="
	@echo " Building B-System MIPS Kernel: $@"
	@echo "=========================================================="
	$(MIPS_LD) -T $(MIPS_LD_SCRIPT) $(MIPS_OBJS) -o $@
	@ln -sf $@ btron-mips.elf
	@echo "[MIPS-ELF] Built: $@"
	@file $@

run-mips: $(MIPS_TARGET)
	@echo "=========================================================="
	@echo " Launching B-System MIPS Kernel on QEMU"
	@echo " Machine  : MIPS Malta Core LV (-M malta)"
	@echo " CPU      : MIPS 24Kf / 5KEc (Little-Endian)"
	@echo " RAM      : 256 MB (KSEG0 Mapped)"
	@echo " Console  : 16550 UART COM1 @ 0x180003F8 (stdio)"
	@echo "=========================================================="
	@if [ "$(MAGNUM)" = "1" ] && [ -f ntprom.raw ]; then \
	    echo "[QEMU] Booting MIPS Magnum (-M magnum) with ntprom.raw BIOS..."; \
	    $(QEMU_MIPS64) -M magnum -bios ./ntprom.raw -m 64M; \
	elif command -v $(QEMU_MIPS) >/dev/null 2>&1; then \
	    $(QEMU_MIPS) -M malta -cpu 24Kf -m 256M -kernel $(MIPS_TARGET) -nographic -monitor none; \
	elif command -v $(QEMU_MIPS64) >/dev/null 2>&1; then \
	    $(QEMU_MIPS64) -M malta -cpu 5KEc -m 256M -kernel $(MIPS_TARGET) -nographic -monitor none; \
	else \
	    echo "[ERROR] Neither $(QEMU_MIPS) nor $(QEMU_MIPS64) found"; \
	    exit 1; \
	fi

test-mips: $(MIPS_TARGET)
	@echo "=========================================================="
	@echo " Testing B-System MIPS Kernel on QEMU Malta (Headless CI)"
	@echo "=========================================================="
	@bash scripts/test_mips.sh

# ═══════════════════════════════════════════════════════════════════
# Bare-Metal ARM32 ELF — BCM283x Pi 2B (Cortex-A7 / ARMv7 / BCM2836)
# ═══════════════════════════════════════════════════════════════════
arm-elf: tad_bin $(ARM32_TARGET)

%.arm32.o: %.c
	$(ARM32_CC) $(ARM_CFLAGS) -c $< -o $@

$(ARM32_TARGET): $(ARM32_OBJS) $(BAREMETAL_LD)
	@echo "=========================================================="
	@echo " Building ARM32 ELF — BCM283x Pi 2B (Cortex-A7, ARMv7)"
	@echo " Startup: $(BAREMETAL_STARTUP)"
	@echo "=========================================================="
	$(ARM32_CC) $(ARM_CFLAGS) -Wl,-T,$(BAREMETAL_LD) $(ARM32_OBJS) -o $@
	@echo "[ARM-ELF] Built: $@"
	@file $@

# ═══════════════════════════════════════════════════════════════════
# Bare-Metal AArch64 ELF — Pi 4B (Cortex-A72 / BCM2711)
# ═══════════════════════════════════════════════════════════════════
arm64-elf: tad_bin $(ARM64_TARGET)

%.arm64.o: %.c
	$(ARM64_CC) $(ARM64_CFLAGS) -c $< -o $@

$(ARM64_TARGET): $(ARM64_OBJS) $(BAREMETAL_LD)
	@echo "=========================================================="
	@echo " Building AArch64 ELF — Pi 4B (Cortex-A72, BCM2711)"
	@echo " Startup: $(BAREMETAL_STARTUP)"
	@echo "=========================================================="
	$(ARM64_CC) $(ARM64_CFLAGS) -Wl,-T,$(BAREMETAL_LD) $(ARM64_OBJS) -o $@
	@echo "[ARM64-ELF] Built: $@"
	@file $@

# ═══════════════════════════════════════════════════════════════════
# Raspberry Pi 400 — One-Shot SD Card Image & Flash
# Target 6 (aarch64-bcm2711) Takanori Yokoyama — T-Kernel 2.0
#
# Usage:
#   make pi400              — build btron-pi400.img (64 MiB FAT32)
#   make flash-pi400        — auto-detect SD card & flash with dd
#   make flash-pi400 PI400_SDCARD=/dev/diskN  — explicit device
#   make fetch-pi400-fw     — download start4.elf + fixup4.dat
# ═══════════════════════════════════════════════════════════════════
PI400_IMG      = btron-pi400.img
PI400_IMG_SIZE = 64
PI400_FW_DIR   = third_party/pi400
PI400_CFG_DIR  = src/drivers/bcm283x/pi400

LLVM_OBJCOPY  ?= $(shell for p in \
    /opt/homebrew/opt/llvm/bin/llvm-objcopy \
    /usr/local/opt/llvm/bin/llvm-objcopy \
    llvm-objcopy; do \
    if command -v "$$p" >/dev/null 2>&1; then echo "$$p"; break; fi; done)

# Auto-detect the SD card that has a FAT32 volume named PiBoot (or BTRON3PI4).
# diskutil list -plist + grep is portable on any macOS version.
# Produces e.g. /dev/disk16  (parent disk, not the partition slice).
PI400_SDCARD ?= $(shell diskutil list | \
    awk '/PiBoot|BTRON3PI4/{print $$NF}' | \
    sed 's/s[0-9]*$$//' | head -1 | \
    sed 's|^|/dev/|')

# Resolve firmware paths — handle both upper and lower case (RISC OS copies uppercase)
PI400_START4  = $(firstword $(wildcard $(PI400_FW_DIR)/start4.elf $(PI400_FW_DIR)/START4.ELF))
PI400_FIXUP4  = $(firstword $(wildcard $(PI400_FW_DIR)/fixup4.dat $(PI400_FW_DIR)/fixup4.dat))

pi400: $(PI400_IMG)

# EL3 armstub: runs at Secure EL3 before the kernel to set SCR_EL3 (no IRQ/FIQ
# trap) and GICD_IGROUPR (Group 1) — both impossible from Non-secure EL1.
PI400_ARMSTUB_BIN = .build/pi400/armstub8.bin

$(PI400_ARMSTUB_BIN): src/drivers/bcm283x/cpu/armstub8.S
	@mkdir -p .build/pi400
	$(ARM64_CC) -c $< -o .build/pi400/armstub8.o
	$(ARM64_CC) -Wl,-Ttext=0x0 -Wl,--image-base=0x0 -Wl,--build-id=none .build/pi400/armstub8.o -o .build/pi400/armstub8.elf
	$(LLVM_OBJCOPY) -O binary .build/pi400/armstub8.elf $@
	@SZ=$$(wc -c < $@ | tr -d ' '); echo "[PI400] armstub8.bin: $$SZ bytes (EL3, linked @0x0)"

$(PI400_IMG): arm64-elf $(PI400_ARMSTUB_BIN)
	@echo "=========================================================="
	@echo " BTRON Pi 400 — SD Card Image Builder"
	@echo " ELF     : $(ARM64_TARGET)"
	@echo " Image   : $(PI400_IMG)  ($(PI400_IMG_SIZE) MiB FAT32)"
	@echo "=========================================================="
	# ── 1. Verify llvm-objcopy available ──────────────────────────
	@if [ -z "$(LLVM_OBJCOPY)" ]; then \
	    echo "[ERROR] llvm-objcopy not found. Install: brew install llvm"; exit 1; fi
	# ── 2. Verify firmware blobs (auto-copy from /Volumes/PiBoot) ─
	@if [ -z "$(PI400_START4)" ] || [ -z "$(PI400_FIXUP4)" ]; then \
	    SRC=$$(ls /Volumes/PiBoot/START4.ELF /Volumes/PiBoot/start4.elf 2>/dev/null | head -1); \
	    if [ -n "$$SRC" ]; then \
	        echo "[PI400] Auto-copying firmware from /Volumes/PiBoot..."; \
	        mkdir -p $(PI400_FW_DIR); \
	        cp /Volumes/PiBoot/START4.ELF  $(PI400_FW_DIR)/START4.ELF  2>/dev/null || \
	        cp /Volumes/PiBoot/start4.elf  $(PI400_FW_DIR)/start4.elf; \
	        cp /Volumes/PiBoot/fixup4.dat  $(PI400_FW_DIR)/fixup4.dat; \
	    else \
	        echo "[ERROR] GPU firmware not found in $(PI400_FW_DIR)/ or /Volumes/PiBoot/"; \
	        echo "  Run: make fetch-pi400-fw   OR  copy start4.elf + fixup4.dat manually."; \
	        exit 1; \
	    fi; \
	fi
	# ── 3. ELF → raw binary (kernel8.img) ─────────────────────────
	@mkdir -p .build/pi400
	$(LLVM_OBJCOPY) -O binary $(ARM64_TARGET) .build/pi400/kernel8.img
	@SZ=$$(wc -c < .build/pi400/kernel8.img | tr -d ' '); \
	 echo "[PI400] kernel8.img: $$SZ bytes ($$(( $$SZ / 1024 )) KiB)"
	# ── 4. Create FAT32 disk image via hdiutil (macOS native) ─────
	@rm -f $(PI400_IMG) .build/pi400/pi400_work.dmg
	hdiutil create -size $(PI400_IMG_SIZE)m -fs MS-DOS -volname BTRON3PI4 \
	    -layout NONE -type UDIF .build/pi400/pi400_work >/dev/null 2>&1 || \
	hdiutil create -size $(PI400_IMG_SIZE)m -fs MS-DOS -volname BTRON3PI4 \
	    .build/pi400/pi400_work >/dev/null
	@echo "[PI400] Created $(PI400_IMG_SIZE) MiB FAT32 image."
	# ── 5. Mount, copy files, and detach ──────────────────────────
	@MOUNT_PT=$$(mktemp -d /tmp/btron_pi400_XXXXXX); \
	DEV=$$(hdiutil attach .build/pi400/pi400_work.dmg \
	        -mountpoint $$MOUNT_PT -nobrowse 2>/dev/null | \
	        grep '/dev/disk' | awk '{print $$1}' | head -1); \
	echo "[PI400] Attached $$DEV → $$MOUNT_PT"; \
	cp $(PI400_START4)                  $$MOUNT_PT/start4.elf; \
	cp $(PI400_FW_DIR)/fixup4.dat       $$MOUNT_PT/fixup4.dat; \
	cp $(PI400_FW_DIR)/bcm2711-rpi-400.dtb $$MOUNT_PT/bcm2711-rpi-400.dtb 2>/dev/null || true; \
	cp $(PI400_FW_DIR)/bcm2711-rpi-4-b.dtb   $$MOUNT_PT/bcm2711-rpi-4-b.dtb 2>/dev/null || true; \
	cp $(PI400_CFG_DIR)/config.txt      $$MOUNT_PT/config.txt; \
	cp $(PI400_CFG_DIR)/cmdline.txt     $$MOUNT_PT/cmdline.txt; \
	cp .build/pi400/kernel8.img         $$MOUNT_PT/kernel8.img; \
	cp .build/pi400/armstub8.bin        $$MOUNT_PT/armstub8.bin; \
	echo "[PI400] SD image contents:"; \
	ls -lh $$MOUNT_PT/; \
	sync; \
	hdiutil detach $$DEV -force -quiet; \
	rmdir $$MOUNT_PT 2>/dev/null || true
	# ── 6. Convert UDIF .dmg → flat raw .img (dd-flashable) ───────
	@sleep 1
	hdiutil convert .build/pi400/pi400_work.dmg \
	    -format UDTO -o .build/pi400/pi400_raw >/dev/null 2>&1 || \
	hdiutil convert .build/pi400/pi400_work.dmg \
	    -format UDTO -o .build/pi400/pi400_raw
	@mv .build/pi400/pi400_raw.cdr $(PI400_IMG)
	@rm -f .build/pi400/pi400_work.dmg
	@echo "=========================================================="
	@echo " BTRON Pi 400 image ready: $(PI400_IMG)"
	@echo " Flash  : make flash-pi400"
	@echo " Device : $(PI400_SDCARD)  (auto-detected via 'PiBoot' label)"
	@echo "=========================================================="

fetch-pi400-fw:
	@echo "[PI400] Fetching GPU firmware from raspberrypi/firmware (master)..."
	@mkdir -p $(PI400_FW_DIR)
	curl -L --progress-bar \
	    -o $(PI400_FW_DIR)/start4.elf \
	    https://github.com/raspberrypi/firmware/raw/master/boot/start4.elf
	curl -L --progress-bar \
	    -o $(PI400_FW_DIR)/fixup4.dat \
	    https://github.com/raspberrypi/firmware/raw/master/boot/fixup4.dat
	@echo "[PI400] Firmware saved to $(PI400_FW_DIR)/"
	@ls -lh $(PI400_FW_DIR)/start4.elf $(PI400_FW_DIR)/fixup4.dat

flash-pi400: $(PI400_IMG)
	@echo "=========================================================="
	@echo " BTRON Pi 400 — Flash SD Card"
	@echo " Image  : $(PI400_IMG)"
	@echo " Device : $(PI400_SDCARD)"
	@echo "=========================================================="
	@if [ -z "$(PI400_SDCARD)" ]; then \
	    echo "[ERROR] Could not auto-detect SD card."; \
	    echo "  Plug in the card and run:"; \
	    echo "    make flash-pi400 PI400_SDCARD=/dev/diskN"; \
	    echo "  (find N with: diskutil list | grep -E 'FAT|PiBoot')"; \
	    exit 1; \
	fi
	@if [ ! -b "$(PI400_SDCARD)" ] && [ ! -c "$(PI400_SDCARD)" ]; then \
	    echo "[ERROR] Device $(PI400_SDCARD) does not exist."; \
	    exit 1; \
	fi
	@echo "[PI400] Unmounting all partitions on $(PI400_SDCARD)..."
	@diskutil unmountDisk $(PI400_SDCARD) || true
	@echo "[PI400] Writing image (sudo dd — enter macOS password if prompted):"
	sudo dd if=$(PI400_IMG) of=$(PI400_SDCARD) bs=4m conv=sync status=progress
	@echo "[PI400] Sync & eject..."
	@sync
	@diskutil eject $(PI400_SDCARD) || true
	@echo "=========================================================="
	@echo " SD card flashed! Safely remove, insert into Pi 400 & power on."
	@echo "  Serial debug (GPIO 14/15, 115200 baud) — optional USB-UART adapter."
	@echo "=========================================================="

# ═══════════════════════════════════════════════════════════════════
# Runs B-TRON on Raspberry Pi 2B (BCM2836, Cortex-A7, ARMv7 32-bit).
# Supports both qemu-system-arm and qemu-system-aarch64.
# ═══════════════════════════════════════════════════════════════════
run-kernel: $(ARM32_TARGET)
	@echo "=========================================================="
	@echo " Launching T-Kernel on QEMU Raspberry Pi 2B (BCM2836)"
	@echo " Machine : raspi2b  |  CPU: Cortex-A7  |  RAM: 1G"
	@echo " ELF     : $(ARM32_TARGET)"
	@echo " Devices : USB Keyboard, USB Mouse & VideoCore GPU Display"
	@echo "=========================================================="
	@echo " INPUT CAPTURE:"
	@echo "   Click inside the QEMU window to grab keyboard & mouse."
	@echo "   Press Ctrl+Alt+G (macOS: Ctrl+Option+G) to release grab."
	@echo "   Serial/UART input also works in THIS terminal window."
	@echo "=========================================================="
	@if command -v $(QEMU_AARCH64) >/dev/null 2>&1; then \
	    $(QEMU_AARCH64) -M raspi2b -m 1G $(KERNEL_DISPLAY) \
	        -device usb-kbd -device usb-mouse \
	        -kernel $(ARM32_TARGET) -serial stdio; \
	elif command -v $(QEMU_ARM) >/dev/null 2>&1; then \
	    $(QEMU_ARM) -M raspi2b -m 1G $(KERNEL_DISPLAY) \
	        -device usb-kbd -device usb-mouse \
	        -kernel $(ARM32_TARGET) -serial stdio; \
	else \
	    echo "[ERROR] QEMU not found — install qemu-system-aarch64 or qemu-system-arm"; \
	    exit 1; \
	fi

run-yoko: $(ARM64_TARGET)
	@echo "=========================================================="
	@echo " Launching T-Kernel AArch64 on QEMU Raspberry Pi 3B (BCM2837)"
	@echo " Honoring : Takanori Yokoyama (横山 孝徳) — T-Kernel Pioneer"
	@echo " Machine  : raspi3b  |  CPU: Cortex-A53 / AArch64  |  RAM: 1G"
	@echo " ELF      : $(ARM64_TARGET)"
	@echo " Devices  : USB Keyboard, USB Mouse & VideoCore GPU Display"
	@echo "=========================================================="
	@echo " INPUT CAPTURE:"
	@echo "   Click inside the QEMU window to grab keyboard & mouse."
	@echo "   Press Ctrl+Alt+G (macOS: Ctrl+Option+G) to release grab."
	@echo "   Serial/UART input also works in THIS terminal window."
	@echo "=========================================================="
	@if command -v $(QEMU_AARCH64) >/dev/null 2>&1; then \
	    $(QEMU_AARCH64) -M raspi3b -m 1G $(KERNEL_DISPLAY) \
	        -device usb-kbd -device usb-mouse \
	        -kernel $(ARM64_TARGET) -serial stdio; \
	else \
	    echo "[ERROR] $(QEMU_AARCH64) not found — install qemu-system-aarch64"; \
	    exit 1; \
	fi

run-yoko4: $(ARM64_TARGET)
	@echo "=========================================================="
	@echo " Launching T-Kernel AArch64 on QEMU Raspberry Pi 4B (BCM2711)"
	@echo " Honoring : Takanori Yokoyama (横山 孝徳) — T-Kernel Pioneer"
	@echo " Machine  : raspi4b  |  CPU: Cortex-A72 / AArch64  |  RAM: 2G"
	@echo " ELF      : $(ARM64_TARGET)"
	@echo " Devices  : USB Keyboard, USB Mouse & VideoCore GPU Display"
	@echo "=========================================================="
	@echo " INPUT CAPTURE:"
	@echo "   Click inside the QEMU window to grab keyboard & mouse."
	@echo "   Press Ctrl+Alt+G (macOS: Ctrl+Option+G) to release grab."
	@echo "   Serial/UART input also works in THIS terminal window."
	@echo "=========================================================="
	@if command -v $(QEMU_AARCH64) >/dev/null 2>&1; then \
	    $(QEMU_AARCH64) -M raspi4b -m 2G $(KERNEL_DISPLAY) \
	        -device usb-kbd -device usb-mouse \
	        -kernel $(ARM64_TARGET) -serial stdio; \
	else \
	    echo "[ERROR] $(QEMU_AARCH64) not found — install qemu-system-aarch64"; \
	    exit 1; \
	fi

test-kernel: $(ARM32_TARGET)
	@echo "=========================================================="
	@echo " Testing T-Kernel on QEMU Raspberry Pi 2B (BCM2836)"
	@echo " Machine : raspi2b  |  CPU: Cortex-A7  |  RAM: 1G"
	@echo " Mode    : automated CI test (headless, serial validation)"
	@echo "=========================================================="
	@bash scripts/test_tkernel.sh

test-yoko: $(ARM64_TARGET)
	@echo "=========================================================="
	@echo " Testing T-Kernel on QEMU Raspberry Pi 3B (BCM2837)"
	@echo " Machine : raspi3b  |  CPU: Cortex-A53 / AArch64  |  RAM: 1G"
	@echo " Mode    : automated CI test (headless, serial validation)"
	@echo "=========================================================="
	@bash scripts/test_arm64.sh

test-yoko4: $(ARM64_TARGET)
	@echo "=========================================================="
	@echo " Testing T-Kernel on QEMU Raspberry Pi 4B (BCM2711)"
	@echo " Machine : raspi4b  |  CPU: Cortex-A72 / AArch64  |  RAM: 2G"
	@echo " Mode    : automated CI test (headless, serial validation)"
	@echo "=========================================================="
	@bash scripts/test_arm64_rpi4.sh

# ═══════════════════════════════════════════════════════════════════
# Debug
# ═══════════════════════════════════════════════════════════════════
debug-virtio: $(ARM64_TARGET)
	$(QEMU_AARCH64) -M raspi4b -m 2G -trace "virtio_*" \
	    -kernel $(ARM64_TARGET) -serial stdio

debug-gdb: $(ARM32_TARGET)
	@if command -v $(QEMU_ARM) >/dev/null 2>&1; then \
	    $(QEMU_ARM) -M raspi2b -m 1G -s -S -kernel $(ARM32_TARGET) -serial stdio; \
	elif command -v $(QEMU_AARCH64) >/dev/null 2>&1; then \
	    $(QEMU_AARCH64) -M raspi2b -m 1G -s -S -kernel $(ARM32_TARGET) -serial stdio; \
	fi

# ═══════════════════════════════════════════════════════════════════
# Mozc Kana-Kanji Conversion & TIP Unit Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_MOZC_SRCS = verify/tests/test_mozc.c src/tip/mozc_kkc.c src/tip/tip_ife.c src/tip/wylie.c src/tip/tibetan_dict.c \
                 src/font/troncode.c src/font/jis_fonts.c src/font/tibetan_fonts.c src/tip/tip_vobj.c src/window/wnd.c \
                 src/graphics/dp_core.c src/chokanji/pmc.c
TEST_MOZC_OBJS = $(TEST_MOZC_SRCS:.c=.test.o)
TEST_MOZC_BIN  = ./.build/test_mozc

%.test.o: %.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -MMD -MP -c $< -o $@

test-mozc: $(TEST_MOZC_BIN)
	@echo "=========================================================="
	@echo " Running Mozc Kana-Kanji Conversion & TIP Unit Tests..."
	@echo "=========================================================="
	@./$(TEST_MOZC_BIN)

$(TEST_MOZC_BIN): $(TEST_MOZC_OBJS)
	$(CC) $(TEST_MOZC_OBJS) -o $@ $(LDFLAGS)

# ═══════════════════════════════════════════════════════════════════
# Editor UI & Internal Functions Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_EDITOR_SRCS = verify/tests/test_editor_ui.c src/apps/t_editor.c src/window/app_menu.c src/tip/mozc_kkc.c src/tip/tip_ife.c src/tip/wylie.c src/tip/tibetan_dict.c \
                   src/font/troncode.c src/font/jis_fonts.c src/font/tibetan_fonts.c src/tip/tip_vobj.c src/window/wnd.c \
                   src/graphics/dp_core.c src/chokanji/pmc.c src/fs/blk_mem.c src/fs/blk_file.c src/fs/vol.c src/fs/file.c
TEST_EDITOR_OBJS = $(TEST_EDITOR_SRCS:.c=.test.o)
TEST_EDITOR_BIN  = ./.build/test_editor

test-editor: $(TEST_EDITOR_BIN)
	@echo "=========================================================="
	@echo " Running B-System Editor UI & Internal Functions Tests..."
	@echo "=========================================================="
	@./$(TEST_EDITOR_BIN)

$(TEST_EDITOR_BIN): $(TEST_EDITOR_OBJS)
	$(CC) $(TEST_EDITOR_OBJS) -o $@ $(LDFLAGS)

# ═══════════════════════════════════════════════════════════════════
# TRON HMI Standard Library Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_HMI_SRCS = verify/tests/test_hmi.c src/hmi/hmi_core.c src/hmi/hmi_switch.c \
                src/hmi/hmi_selector.c src/hmi/hmi_volume.c src/hmi/hmi_meter.c \
                src/hmi/hmi_controller.c src/hmi/hmi_panel.c src/graphics/dp_core.c \
                src/font/troncode.c src/font/jis_fonts.c src/font/tibetan_fonts.c src/window/wnd.c src/chokanji/pmc.c
TEST_HMI_OBJS = $(TEST_HMI_SRCS:.c=.test.o)
TEST_HMI_BIN  = ./.build/test_hmi

test-hmi: $(TEST_HMI_BIN)
	@echo "=========================================================="
	@echo " Running TRON HMI Standard Library Unit Tests..."
	@echo "=========================================================="
	@./$(TEST_HMI_BIN)

$(TEST_HMI_BIN): $(TEST_HMI_OBJS)
	$(CC) $(TEST_HMI_OBJS) -o $@ $(LDFLAGS) -lm

# ═══════════════════════════════════════════════════════════════════
# TAD Unified Packing Pipeline — Python primary, Elixir fallback
# ═══════════════════════════════════════════════════════════════════
PYTHON ?= $(shell for py in python3 python3.14 python3.13 python3.12 python3.11 python3.10; do if command -v $$py >/dev/null 2>&1; then echo "$$py"; break; fi; done)

TAD_HTML2TAD := $(shell \
  if [ -n "$(PYTHON)" ]; then echo "$(PYTHON) scripts/html2tad.py"; \
  elif command -v elixir >/dev/null 2>&1; then echo "elixir scripts/html2tad.exs"; fi)
TAD_BOOK2TAD := $(shell \
  if [ -n "$(PYTHON)" ]; then echo "$(PYTHON) scripts/book2tad.py"; \
  elif command -v elixir >/dev/null 2>&1; then echo "elixir scripts/book2tad.exs"; fi)

tad_bin:
	@if [ -z "$(TAD_HTML2TAD)" ]; then \
	  echo "Note: neither python3 nor elixir found - tad_bin skipped (desktop still runs)."; \
	else $(TAD_HTML2TAD); $(TAD_BOOK2TAD); fi

html2tad:
	@$(TAD_HTML2TAD) --test
	@$(TAD_BOOK2TAD) --test

book2tad:
	@$(TAD_BOOK2TAD)

# ═══════════════════════════════════════════════════════════════════
# Native TAD Document Browser & Cabinet Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_TAD_SRCS = verify/tests/test_tad_browser.c src/apps/tad_browser.c src/apps/vobj_manager.c src/window/dnd.c src/window/app_menu.c \
                src/settings/appearance.c src/graphics/icons_bundle.c \
                src/tip/mozc_kkc.c src/font/troncode.c src/font/jis_fonts.c src/font/tibetan_fonts.c src/tip/tip_vobj.c \
                src/window/wnd.c src/graphics/dp_core.c src/chokanji/pmc.c
TEST_TAD_OBJS = $(TEST_TAD_SRCS:.c=.test.o)
TEST_TAD_BIN  = ./.build/test_tad_browser

test-tad: $(TEST_TAD_BIN) tad_bin
	@echo "=========================================================="
	@echo " Running B-System Native TAD Browser & Cabinet Tests..."
	@echo "=========================================================="
	@./$(TEST_TAD_BIN)

$(TEST_TAD_BIN): $(TEST_TAD_OBJS)
	$(CC) $(TEST_TAD_OBJS) -o $@ $(LDFLAGS) -lz

# ═══════════════════════════════════════════════════════════════════
# BeOS Chat & TRON IPC Pub/Sub Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_CHAT_SRCS = verify/tests/test_chat.c src/apps/chat.c src/apps/chat_xml.c \
                 src/tip/mozc_kkc.c src/font/troncode.c src/font/jis_fonts.c src/font/tibetan_fonts.c \
                 src/window/wnd.c src/graphics/dp_core.c src/chokanji/pmc.c
TEST_CHAT_OBJS = $(TEST_CHAT_SRCS:.c=.test.o)
TEST_CHAT_BIN  = ./.build/test_chat

test-chat: $(TEST_CHAT_BIN)
	@echo "=========================================================="
	@echo " Running B-System BeOS Chat (Blabber) & TRON IPC Tests..."
	@echo "=========================================================="
	@./$(TEST_CHAT_BIN)

$(TEST_CHAT_BIN): $(TEST_CHAT_OBJS)
	$(CC) $(TEST_CHAT_OBJS) -o $@ $(LDFLAGS) -lz

# ═══════════════════════════════════════════════════════════════════
# BTRON Deskbar Tracker & Task Manager Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_TRACKER_SRCS = verify/tests/test_tracker.c src/desktop/tracker.c src/desktop/desktop.c src/settings/appearance.c \
                    src/vobject/vobj.c src/desktop/about.c src/window/wnd.c src/chokanji/pmc.c \
                    src/window/app_menu.c \
                    src/graphics/dp_core.c src/graphics/icons_bundle.c src/font/troncode.c src/font/jis_fonts.c src/font/tibetan_fonts.c
TEST_TRACKER_OBJS = $(TEST_TRACKER_SRCS:.c=.test.o)
TEST_TRACKER_BIN  = ./.build/test_tracker

test-tracker: $(TEST_TRACKER_BIN)
	@echo "=========================================================="
	@echo " Running BTRON Deskbar Tracker Unit Tests..."
	@echo "=========================================================="
	@./$(TEST_TRACKER_BIN)

$(TEST_TRACKER_BIN): $(TEST_TRACKER_OBJS)
	$(CC) $(TEST_TRACKER_OBJS) -o $@ $(LDFLAGS) -lm

# ═══════════════════════════════════════════════════════════════════
# Desktop Banded-Present Losslessness (the PS2 present's contract, headless)
# ═══════════════════════════════════════════════════════════════════
TEST_CLIP_SRCS = verify/tests/test_desktop_clip.c verify/tests/test_desktop_clip_count_port.c \
                 src/desktop/tracker.c src/desktop/desktop.c src/settings/appearance.c \
                 src/vobject/vobj.c src/desktop/about.c src/window/wnd.c src/chokanji/pmc.c \
                 src/window/app_menu.c src/desktop/global_menu.c src/desktop/workbench.c \
                 src/graphics/dp_core.c src/graphics/icons_bundle.c src/font/troncode.c src/font/jis_fonts.c src/font/tibetan_fonts.c
TEST_CLIP_OBJS = $(TEST_CLIP_SRCS:.c=.clip.o)
TEST_CLIP_BIN  = ./.build/test_desktop_clip
# LANE 12i rung (ii)'s COUNT half: renaming the tracker renderer makes every call
# site in workbench.c / global_menu.c / the harness route through the counting
# wrapper in *_count_port.c, which delegates to the real definition (the two TUs
# without this flag).  The plain build reports SKIP for those two rows.  The
# rename TUs live in their OWN `.clip.o' namespace because these sources are
# shared with the other suites through `.test.o': a renamed
# src/desktop/global_menu.test.o would leak `counted_tracker_render_menu' into
# test_global_menu's link, which has no wrapper to resolve it.
verify/tests/test_desktop_clip.clip.o:             CFLAGS += -Dtracker_render_menu=counted_tracker_render_menu
src/desktop/global_menu.clip.o:                    CFLAGS += -Dtracker_render_menu=counted_tracker_render_menu
src/desktop/workbench.clip.o:                      CFLAGS += -Dtracker_render_menu=counted_tracker_render_menu
verify/tests/test_desktop_clip_count_port.clip.o:  CFLAGS += -Utracker_render_menu

%.clip.o: %.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -MMD -MP -c $< -o $@

test-deskclip: $(TEST_CLIP_BIN)
	@echo "=========================================================="
	@echo " Running Desktop Banded-Present Losslessness Tests..."
	@echo "=========================================================="
	@./$(TEST_CLIP_BIN)

$(TEST_CLIP_BIN): $(TEST_CLIP_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_CLIP_OBJS) -o $@ $(LDFLAGS) -lm

verify:
	@$(MAKE) -C verify run
# ═══════════════════════════════════════════════════════════════════
# Unified Test Suite Runner
# ═══════════════════════════════════════════════════════════════════

TEST_SETTINGS_BIN = ./.build/test_settings
TEST_SETTINGS_SRCS = verify/tests/test_language_settings.c \
                     src/settings/language.c \
                     src/settings/control_panel.c \
                     src/settings/appearance.c \
                     src/settings/desktop.c \
                     src/settings/display.c \
                     src/settings/input.c \
                     src/settings/sound.c \
                     src/settings/network.c \
                     src/settings/media.c \
                     src/settings/security.c \
                     src/settings/system.c \
                     src/settings/terminal.c \
                     src/tip/tip_ife.c \
                     src/tip/mozc_kkc.c \
                     src/tip/wylie.c \
                     src/tip/tibetan_dict.c \
                     src/font/troncode.c \
                     src/font/jis_fonts.c \
                     src/font/tibetan_fonts.c \
                     src/window/wnd.c \
                     src/chokanji/pmc.c \
                     src/window/app_menu.c \
                     src/graphics/dp_core.c \
                     src/graphics/icons_bundle.c \
                     src/tip/tip_vobj.c

TEST_SETTINGS_OBJS = $(TEST_SETTINGS_SRCS:.c=.test.o)

$(TEST_SETTINGS_BIN): $(TEST_SETTINGS_OBJS)
	$(CC) $(TEST_SETTINGS_OBJS) -o $@ $(LDFLAGS)

test-settings: $(TEST_SETTINGS_BIN)
	@echo "=========================================================="
	@echo " Running Settings Cabinet: Language & IME Settings Tests..."
	@echo "=========================================================="
	./$(TEST_SETTINGS_BIN)

# ═══════════════════════════════════════════════════════════════════
# BTRON Global System Menu (Chokanji & Haiku) Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_GMENU_SRCS = verify/tests/test_global_menu.c src/desktop/global_menu.c src/desktop/tracker.c \
                  src/window/app_menu.c src/graphics/icons_bundle.c \
                  src/desktop/about.c src/window/wnd.c src/chokanji/pmc.c src/graphics/dp_core.c src/font/troncode.c \
                  src/font/jis_fonts.c src/font/tibetan_fonts.c src/tip/tip_ife.c src/tip/mozc_kkc.c \
                  src/tip/wylie.c src/tip/tibetan_dict.c src/tip/tip_vobj.c
TEST_GMENU_OBJS = $(TEST_GMENU_SRCS:.c=.test.o)
TEST_GMENU_BIN  = ./.build/test_global_menu

test-global-menu: $(TEST_GMENU_BIN)
	@echo "=========================================================="
	@echo " Running BTRON Global System Menu & Japanese Deskbar Tests..."
	@echo "=========================================================="
	@./$(TEST_GMENU_BIN)

$(TEST_GMENU_BIN): $(TEST_GMENU_OBJS)
	$(CC) $(TEST_GMENU_OBJS) -o $@ $(LDFLAGS) -lm

# ═══════════════════════════════════════════════════════════════════
# XMB (XrossMediaBar) OpenGL Render Verification — pixels, not promises
# ═══════════════════════════════════════════════════════════════════
TEST_XMB_SRCS = verify/tests/test_xmb_render.c src/apps/xmb.c src/apps/xmb_wave.c \
                src/gl/gl_dispatch.c src/gl/egl_surface.c src/gl/backend_virgl.c \
                src/gl/gl_backend_choice.c \
                src/font/troncode.c src/font/jis_fonts.c src/font/tibetan_fonts.c \
                src/clu/vfs.c src/fs/blk_mem.c src/fs/blk_file.c \
                src/fs/vol.c src/fs/file.c
TEST_XMB_OBJS = $(TEST_XMB_SRCS:.c=.test.o)
TEST_XMB_BIN  = ./.build/test_xmb_render

test-xmb-render: $(TEST_XMB_BIN)
	@echo "=========================================================="
	@echo " Running XMB OpenGL Render Verification..."
	@echo "=========================================================="
	@./$(TEST_XMB_BIN)

$(TEST_XMB_BIN): $(TEST_XMB_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_XMB_OBJS) -o $@ $(LDFLAGS) -lm

# The same test, against the rasterizer the PS2 actually ships.  This one exists
# because the two backends disagree in a way no amount of reading reveals: the
# virgl path is real GL, so it honours the RGBA the bar uploads and the alpha it
# blends with, while src/gl/tinygl/ truncates any texture that is not exactly
# 256x256 and drops texture alpha on the floor.  Every icon, glyph and fade in
# the XMB goes through both of those, so a gate that only ever ran virgl could
# not see the corruption the console shows -- which is what this harness is for.
# backend_virgl.c is linked here as well, and not for its code: gl_dispatch's
# GL_BACKEND_VIRGL case is unconditional in the shipped image too, so this is the
# closer of the two, not the tidier one.
TEST_XMB_TGL_SRCS = verify/tests/test_xmb_render.c src/apps/xmb.c src/apps/xmb_wave.c \
                    src/gl/gl_dispatch.c src/gl/egl_surface.c \
                    src/gl/gl_backend_choice.c \
                    src/gl/backend_tinygl.c $(TINYGL_SRCS) src/gl/backend_virgl.c \
                    src/font/troncode.c src/font/jis_fonts.c src/font/tibetan_fonts.c \
                    src/clu/vfs.c src/fs/blk_mem.c src/fs/blk_file.c \
                    src/fs/vol.c src/fs/file.c
TEST_XMB_TGL_OBJS = $(TEST_XMB_TGL_SRCS:.c=.tgl.o)
TEST_XMB_TGL_BIN  = ./.build/test_xmb_render_tgl

%.tgl.o: %.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -DBTRON_GL_BACKEND_TINYGL \
	      -DXMB_OUT_DIR='"./.build/tgl/"' -MMD -MP -c $< -o $@
test-xmb-render-tgl: $(TEST_XMB_TGL_BIN)
	@echo "=========================================================="
	@echo " Running XMB Render Verification on the PS2's TinyGL backend..."
	@echo "=========================================================="
	@mkdir -p ./.build/tgl
	@./$(TEST_XMB_TGL_BIN)

$(TEST_XMB_TGL_BIN): $(TEST_XMB_TGL_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_XMB_TGL_OBJS) -o $@ $(LDFLAGS) -lm

# LANE 24's other half: the law that the icon rectangles were actually breaking.  egl_surface.c
# installs a GL pipeline for EVERY window surface and tears it down when any one of them closes,
# and TinyGL has one context -- so the second window orphaned the first's texture names, and a
# lost name is not an error to the sampler, it is the vertex colour drawn instead of the texel:
# one flat rectangle per icon.  See the header of the suite for the two operator reports that
# pinned it, and for why `lilcu then xmb` made it disappear.
# A private object suffix, because this is the only suite that includes tinygl/zgl.h.
TEST_GLCI_SRCS = verify/tests/test_gl_context_isolation.c \
                 src/gl/gl_dispatch.c src/gl/backend_tinygl.c src/gl/backend_virgl.c \
                 $(TINYGL_SRCS)
TEST_GLCI_OBJS = $(TEST_GLCI_SRCS:.c=.ci.o)
TEST_GLCI_BIN  = ./.build/test_gl_context_isolation

%.ci.o: %.c
	$(CC) $(CFLAGS) -DBTRON_GL_BACKEND_TINYGL -MMD -MP -c $< -o $@

test-gl-context: $(TEST_GLCI_BIN)
	@echo "=========================================================="
	@echo " Running LANE 24 gate: one context, N window surfaces..."
	@echo "=========================================================="
	@./$(TEST_GLCI_BIN)

$(TEST_GLCI_BIN): $(TEST_GLCI_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_GLCI_OBJS) -o $@ $(LDFLAGS) -lm

# ═══════════════════════════════════════════════════════════════════
# XMB icon atlas: geometry on the host, sheet in the image
# ═══════════════════════════════════════════════════════════════════
# The glyphs' signed-distance arithmetic now lives in verify/tests/gen_xmb_atlas.c
# (it moved out of the bar, where it measured 132 ms a cell -- 2.8 s of the load --
# and src/apps/xmb_atlas.h is what that program prints.  Nothing builds the sheet
# implicitly: it is committed like the other embedded assets, so test-xmb-atlas is
# what stops it going stale against the geometry.
GEN_XMB_ATLAS_OBJS = verify/tests/gen_xmb_atlas.test.o
GEN_XMB_ATLAS_BIN  = ./.build/gen_xmb_atlas

xmb-atlas: $(GEN_XMB_ATLAS_BIN)
	@$(GEN_XMB_ATLAS_BIN) > src/apps/xmb_atlas.h.tmp || { rm -f src/apps/xmb_atlas.h.tmp; exit 1; }
	@mv src/apps/xmb_atlas.h.tmp src/apps/xmb_atlas.h
	@echo "wrote src/apps/xmb_atlas.h ($$(wc -c < src/apps/xmb_atlas.h) bytes)"

test-xmb-atlas: $(GEN_XMB_ATLAS_BIN)
	@mkdir -p ./.build
	@$(GEN_XMB_ATLAS_BIN) > ./.build/xmb_atlas.h.new || { rm -f ./.build/xmb_atlas.h.new; exit 1; }
	@if cmp -s ./.build/xmb_atlas.h.new src/apps/xmb_atlas.h; then \
	    echo "[PASS] src/apps/xmb_atlas.h is still the sheet this geometry prints"; \
	 else \
	    echo "[FAIL] src/apps/xmb_atlas.h has drifted from verify/tests/gen_xmb_atlas.c -- run make xmb-atlas"; \
	    rm -f ./.build/xmb_atlas.h.new; exit 1; fi
	@rm -f ./.build/xmb_atlas.h.new

# ═══════════════════════════════════════════════════════════════════
# Common Application Menu Subsystem Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_APP_MENU_SRCS = verify/tests/test_app_menu.c src/window/app_menu.c src/window/wnd.c src/chokanji/pmc.c \
                     src/graphics/dp_core.c src/font/troncode.c src/font/jis_fonts.c src/font/tibetan_fonts.c
TEST_APP_MENU_OBJS = $(TEST_APP_MENU_SRCS:.c=.test.o)
TEST_APP_MENU_BIN  = ./.build/test_app_menu

test-app-menu: $(TEST_APP_MENU_BIN)
	@echo "=========================================================="
	@echo " Running Common Application Menu Subsystem Tests..."
	@echo "=========================================================="
	@./$(TEST_APP_MENU_BIN)

$(TEST_APP_MENU_BIN): $(TEST_APP_MENU_OBJS)
	$(CC) $(TEST_APP_MENU_OBJS) -o $@ $(LDFLAGS)

# ═══════════════════════════════════════════════════════════════════
# Mouse Drivers Test Suite (UEFI PS/2 & PC-98 Bus Mouse)
# ═══════════════════════════════════════════════════════════════════
TEST_MOUSE_SRCS = verify/tests/test_mouse_drivers.c \
                  src/drivers/uefi/ps2_mouse.c \
                  src/drivers/pc98/input/pc98_mouse.c \
                  src/drivers/pc98/input/pc98_kbd.c
TEST_MOUSE_OBJS = $(TEST_MOUSE_SRCS:.c=.test.o)
TEST_MOUSE_BIN  = ./.build/test_mouse

test-mouse: $(TEST_MOUSE_BIN)
	@echo "=========================================================="
	@echo " Running Mouse Drivers Unit Tests (UEFI PS/2 & PC-98)..."
	@echo "=========================================================="
	@./$(TEST_MOUSE_BIN)

$(TEST_MOUSE_BIN): $(TEST_MOUSE_OBJS)
	$(CC) $(TEST_MOUSE_OBJS) -o $@ $(LDFLAGS) -lm

# ═══════════════════════════════════════════════════════════════════
# B-System DriveSetup Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_DRIVESETUP_SRCS = verify/tests/test_b_drivesetup.c \
                       src/window/app_menu.c \
                       src/apps/b_drivesetup.c
TEST_DRIVESETUP_OBJS = $(TEST_DRIVESETUP_SRCS:.c=.test.o)
TEST_DRIVESETUP_BIN  = ./.build/test_b_drivesetup

test-drivesetup: $(TEST_DRIVESETUP_BIN)
	@echo "=========================================================="
	@echo " Running B-System Minimal DriveSetup (b_drivesetup) Tests..."
	@echo "=========================================================="
	@./$(TEST_DRIVESETUP_BIN)

$(TEST_DRIVESETUP_BIN): $(TEST_DRIVESETUP_OBJS) $(FS_OBJS) src/clu/clu.host.o $(CLU_APPS_HOST_OBJS)
	$(CC) $(TEST_DRIVESETUP_OBJS) $(FS_OBJS) src/clu/clu.host.o $(CLU_APPS_HOST_OBJS) -o $@ $(LDFLAGS) -lm

# ═══════════════════════════════════════════════════════════════════
# ZEXALL — Z80 instruction exerciser against the vendored MSX core
# ═══════════════════════════════════════════════════════════════════
TEST_ZEXALL_SRCS = src/emulators/msx/zexall.c \
                   src/emulators/msx/z80.c
TEST_ZEXALL_OBJS = $(TEST_ZEXALL_SRCS:.c=.test.o)
TEST_ZEXALL_BIN  = ./.build/test_zexall

test-zexall: $(TEST_ZEXALL_BIN)
	@echo "=========================================================="
	@echo " Running ZEXALL against src/emulators/msx/z80.c..."
	@echo "=========================================================="
	@./$(TEST_ZEXALL_BIN)

$(TEST_ZEXALL_BIN): $(TEST_ZEXALL_OBJS)
	$(CC) $(TEST_ZEXALL_OBJS) -o $@ $(LDFLAGS)

# ═══════════════════════════════════════════════════════════════════
# tronMSX boot tracer — headless ROM frames and P/VDP/PSG traffic
# Dev tool: msx-trace ROM=assets/msx/xxx.rom FRAMES=1300
# ═══════════════════════════════════════════════════════════════════
TEST_MSX_TRACE_SRCS = verify/tests/msx_trace.c $(MSX_SRCS)
TEST_MSX_TRACE_OBJS = $(TEST_MSX_TRACE_SRCS:.c=.test.o)
TEST_MSX_TRACE_BIN  = ./.build/msx_trace

msx-trace: $(TEST_MSX_TRACE_BIN)
	@echo "=========================================================="
	@echo " tronMSX boot trace: $(or $(ROM),assets/msx/Lode Runner (1984)(Sony)[a].rom)"
	@echo "=========================================================="
	@./$(TEST_MSX_TRACE_BIN) "$(or $(ROM),assets/msx/Lode Runner (1984)(Sony)[a].rom)" $(or $(FRAMES),300)

$(TEST_MSX_TRACE_BIN): $(TEST_MSX_TRACE_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_MSX_TRACE_OBJS) -o $@ $(LDFLAGS)

TEST_MSX_BOOT_SRCS  = verify/tests/test_msx_boot.c $(MSX_SRCS)
TEST_MSX_BOOT_OBJS  = $(TEST_MSX_BOOT_SRCS:.c=.test.o)
TEST_MSX_BOOT_BIN   = ./.build/test_msx_boot

msx-boot: $(TEST_MSX_BOOT_BIN)
	@./$(TEST_MSX_BOOT_BIN) "$(or $(ROM),assets/msx/Lode Runner (1984)(Sony)[a].rom)" $(or $(FRAMES),300) $(or $(DUMP),/tmp/msx_boot) "$(or $(MACH),)"

TEST_MSX_DSK_SRCS  = verify/tests/test_msx_dsk.c $(MSX_SRCS)
TEST_MSX_DSK_OBJS  = $(TEST_MSX_DSK_SRCS:.c=.test.o)
TEST_MSX_DSK_BIN   = ./.build/test_msx_dsk

msx-dsk: $(TEST_MSX_DSK_BIN)
	@./$(TEST_MSX_DSK_BIN)

$(TEST_MSX_DSK_BIN): $(TEST_MSX_DSK_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_MSX_DSK_OBJS) -o $@ $(LDFLAGS)

$(TEST_MSX_BOOT_BIN): $(TEST_MSX_BOOT_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_MSX_BOOT_OBJS) -o $@ $(LDFLAGS)

TEST_MSX_AUDIO_SRCS = verify/tests/test_msx_audio.c $(MSX_SRCS)
TEST_MSX_AUDIO_OBJS = $(TEST_MSX_AUDIO_SRCS:.c=.test.o)
TEST_MSX_AUDIO_BIN  = ./.build/test_msx_audio

msx-audio: $(TEST_MSX_AUDIO_BIN)
	@./$(TEST_MSX_AUDIO_BIN)

$(TEST_MSX_AUDIO_BIN): $(TEST_MSX_AUDIO_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_MSX_AUDIO_OBJS) -o $@ $(LDFLAGS)

test: test-tad test-editor test-chat test-mozc test-wylie test-hmi test-ski test-tracker test-deskclip test-settings test-global-menu test-app-menu test-drivesetup test-fs test-quake test-ps2-softfloat test-arm-aeabi test-ps2-gs-pack test-ps2-gs-env test-ps2-gs-rec test-ps2-vu1-geom test-ps2-vu0-kernel test-gscube-nexus test-ps2-gs-poster test-ps2-gs-read test-ps2-gs-tex test-ps2-pointer test-ps2-fused-row test-ps2-gif-ring test-ps2-paintw test-eegs-raster test-eegs-present test-gl-math test-gl-backend-choice test-ps2-fb-order test-grid-map test-xmb-atlas test-ps2-opengl-routing test-gl-context
	@echo "=========================================================="
	@echo " ALL B-SYSTEM TEST SUITES PASSED (100% SUCCESS)!"
	@echo "=========================================================="

# ═══════════════════════════════════════════════════════════════════
# Clean
# ═══════════════════════════════════════════════════════════════════
# ═══════════════════════════════════════════════════════════════════
# Ski Bootloader & Multi-Arch Boot Driver Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_SKI_SRCS = verify/tests/test_ski.c src/cores/core_smp.c \
                src/drivers/pc98/boot/boot_pc98.c src/drivers/bcm283x/boot/boot_arm_stub.c
TEST_SKI_OBJS = $(TEST_SKI_SRCS:.c=.test.o)
TEST_SKI_BIN  = ./.build/test_ski

test-ski: $(TEST_SKI_BIN)
	@echo "=========================================================="
	@echo " Running Ski Bootloader (Bootman) Unit Tests..."
	@echo "=========================================================="
	@./$(TEST_SKI_BIN)

$(TEST_SKI_BIN): $(TEST_SKI_OBJS)
	$(CC) $(TEST_SKI_OBJS) -o $@ $(LDFLAGS) -lm


# ═══════════════════════════════════════════════════════════════════
# Kernel TIP Extended Wylie (EWTS) Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_WYLIE_SRCS = verify/tests/test_wylie.c src/tip/wylie.c
TEST_WYLIE_OBJS = $(TEST_WYLIE_SRCS:.c=.test.o)
TEST_WYLIE_BIN  = ./.build/test_wylie

test-wylie: $(TEST_WYLIE_BIN)
	@echo "=========================================================="
	@echo " Running Kernel TIP Extended Wylie (EWTS) Tests..."
	@echo "=========================================================="
	@./$(TEST_WYLIE_BIN)

$(TEST_WYLIE_BIN): $(TEST_WYLIE_OBJS)
	$(CC) $(TEST_WYLIE_OBJS) -o $@ $(LDFLAGS) -lm

# ═══════════════════════════════════════════════════════════════════
# Quake Verification & Glitches Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_QUAKEC_SRCS = verify/tests/test_quakec_interpreter.c src/quake/core/pr_exec.c \
                   src/quake/sys/fs_btron.c src/quake/core/cmd.c src/quake/core/cvar.c \
                   src/quake/core/mem.c src/quake/core/mathlib.c \
                   src/quake/core/sv_phys.c src/quake/core/world.c
TEST_QUAKEC_OBJS = $(TEST_QUAKEC_SRCS:.c=.test.o)
TEST_QUAKEC_BIN  = ./.build/test_quakec

TEST_QUAKE_ENT_SRCS = verify/tests/test_quake_entities_glitches.c src/quake/core/pr_exec.c \
                      src/quake/sys/fs_btron.c src/quake/core/cmd.c src/quake/core/cvar.c \
                      src/quake/core/mem.c src/quake/core/mathlib.c \
                      src/quake/core/sv_phys.c src/quake/core/world.c
TEST_QUAKE_ENT_OBJS = $(TEST_QUAKE_ENT_SRCS:.c=.test.o)
TEST_QUAKE_ENT_BIN  = ./.build/test_quake_entities

TEST_QUAKE_CTRL_SRCS = verify/tests/test_quake_controls.c src/quake/sys/in_btron.c \
                       src/quake/core/sv_phys.c src/quake/core/sv_main.c src/quake/core/cmd.c \
                       src/quake/core/cvar.c src/quake/core/mathlib.c src/quake/core/mem.c \
                       src/quake/sys/fs_btron.c src/quake/core/world.c
TEST_QUAKE_CTRL_OBJS = $(TEST_QUAKE_CTRL_SRCS:.c=.test.o)
TEST_QUAKE_CTRL_BIN  = ./.build/test_quake_controls

$(TEST_QUAKEC_BIN): $(TEST_QUAKEC_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_QUAKEC_OBJS) -o $@ $(LDFLAGS) -lm

$(TEST_QUAKE_ENT_BIN): $(TEST_QUAKE_ENT_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_QUAKE_ENT_OBJS) -o $@ $(LDFLAGS) -lm

$(TEST_QUAKE_CTRL_BIN): $(TEST_QUAKE_CTRL_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_QUAKE_CTRL_OBJS) -o $@ $(LDFLAGS) -lm

TEST_QUAKE_REPLAY_SRCS = verify/tests/test_quake_replay.c src/quake/core/cl_demo.c \
                         src/quake/core/cmd.c src/quake/core/cvar.c src/quake/core/mem.c \
                         src/quake/core/mathlib.c src/quake/core/world.c src/quake/core/pr_exec.c \
                         src/quake/core/sv_phys.c src/quake/core/sv_main.c \
                         src/quake/sys/sys_btron.c src/quake/sys/fs_btron.c \
                         src/quake/app/quake_ui.c
TEST_QUAKE_REPLAY_OBJS = $(TEST_QUAKE_REPLAY_SRCS:.c=.test.o)
TEST_QUAKE_REPLAY_BIN  = ./.build/test_quake_replay

$(TEST_QUAKE_REPLAY_BIN): $(TEST_QUAKE_REPLAY_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_QUAKE_REPLAY_OBJS) -o $@ $(LDFLAGS) -lm

test-replay: $(TEST_QUAKE_REPLAY_BIN)
	@echo "=========================================================="
	@echo " Running Quake Demo Replay & Level Select Test Suite..."
	@echo "=========================================================="
	@./$(TEST_QUAKE_REPLAY_BIN)

TEST_QUAKE_HULL_SRCS = verify/tests/test_quake_hull.c src/quake/core/cl_demo.c \
                       src/quake/core/cmd.c src/quake/core/cvar.c src/quake/core/mem.c \
                       src/quake/core/mathlib.c src/quake/core/world.c src/quake/core/pr_exec.c \
                       src/quake/core/sv_phys.c src/quake/core/sv_main.c \
                       src/quake/sys/sys_btron.c src/quake/sys/fs_btron.c \
                       src/quake/app/quake_ui.c
TEST_QUAKE_HULL_OBJS = $(TEST_QUAKE_HULL_SRCS:.c=.test.o)
TEST_QUAKE_HULL_BIN  = ./.build/test_quake_hull

$(TEST_QUAKE_HULL_BIN): $(TEST_QUAKE_HULL_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_QUAKE_HULL_OBJS) -o $@ $(LDFLAGS) -lm

test-hull: $(TEST_QUAKE_HULL_BIN)
	@./$(TEST_QUAKE_HULL_BIN)

TEST_QUAKE_VIEW_SRCS = verify/tests/test_quake_visibility.c \
                       src/quake/core/cl_demo.c \
                       src/quake/core/cmd.c src/quake/core/cvar.c src/quake/core/mem.c \
                       src/quake/core/mathlib.c src/quake/core/world.c src/quake/core/pr_exec.c \
                       src/quake/core/sv_phys.c src/quake/core/sv_main.c \
                       src/quake/sys/sys_btron.c src/quake/sys/fs_btron.c \
                       src/quake/app/quake_ui.c
TEST_QUAKE_VIEW_OBJS = $(TEST_QUAKE_VIEW_SRCS:.c=.test.o)
TEST_QUAKE_VIEW_BIN  = ./.build/test_quake_visibility

$(TEST_QUAKE_VIEW_BIN): $(TEST_QUAKE_VIEW_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_QUAKE_VIEW_OBJS) -o $@ $(LDFLAGS) -lm

test-view: $(TEST_QUAKE_VIEW_BIN)
	@./$(TEST_QUAKE_VIEW_BIN)

TEST_QUAKE_SECRET_SRCS = verify/tests/test_quake_secret.c \
                         src/quake/core/cl_demo.c \
                         src/quake/core/cmd.c src/quake/core/cvar.c src/quake/core/mem.c \
                         src/quake/core/mathlib.c src/quake/core/world.c src/quake/core/pr_exec.c \
                         src/quake/core/sv_phys.c src/quake/core/sv_main.c \
                         src/quake/sys/sys_btron.c src/quake/sys/fs_btron.c \
                         src/quake/app/quake_ui.c
TEST_QUAKE_SECRET_OBJS = $(TEST_QUAKE_SECRET_SRCS:.c=.test.o)
TEST_QUAKE_SECRET_BIN  = ./.build/test_quake_secret

$(TEST_QUAKE_SECRET_BIN): $(TEST_QUAKE_SECRET_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_QUAKE_SECRET_OBJS) -o $@ $(LDFLAGS) -lm

test-secret: $(TEST_QUAKE_SECRET_BIN)
	@./$(TEST_QUAKE_SECRET_BIN)

TEST_QUAKE_RENDER_SRCS = verify/tests/test_quake_render.c \
                         src/quake/core/cl_demo.c \
                         src/quake/core/cmd.c src/quake/core/cvar.c src/quake/core/mem.c \
                         src/quake/core/mathlib.c src/quake/core/world.c src/quake/core/pr_exec.c \
                         src/quake/core/sv_phys.c src/quake/core/sv_main.c \
                         src/quake/render/r_brush.c src/quake/render/r_light.c \
                         src/quake/render/r_surf.c src/quake/render/r_alias.c \
                         src/quake/render/texture.c \
                         src/quake/sys/sys_btron.c src/quake/sys/fs_btron.c \
                         src/quake/app/quake_ui.c \
                         src/gl/gl_dispatch.c src/gl/gl_backend_choice.c src/gl/backend_virgl.c
TEST_QUAKE_RENDER_OBJS = $(TEST_QUAKE_RENDER_SRCS:.c=.test.o)
TEST_QUAKE_RENDER_BIN  = ./.build/test_quake_render

$(TEST_QUAKE_RENDER_BIN): $(TEST_QUAKE_RENDER_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_QUAKE_RENDER_OBJS) -o $@ $(LDFLAGS) -lm

test-render: $(TEST_QUAKE_RENDER_BIN)
	@./$(TEST_QUAKE_RENDER_BIN)

TEST_QUAKE_DRONE_SRCS = verify/tests/test_quake_drone.c \
                        src/quake/core/cl_demo.c \
                        src/quake/core/cmd.c src/quake/core/cvar.c src/quake/core/mem.c \
                        src/quake/core/mathlib.c src/quake/core/world.c src/quake/core/pr_exec.c \
                        src/quake/core/sv_phys.c src/quake/core/sv_main.c \
                        src/quake/sys/sys_btron.c src/quake/sys/fs_btron.c \
                        src/quake/app/quake_ui.c
TEST_QUAKE_DRONE_OBJS = $(TEST_QUAKE_DRONE_SRCS:.c=.test.o)
TEST_QUAKE_DRONE_BIN  = ./.build/test_quake_drone

$(TEST_QUAKE_DRONE_BIN): $(TEST_QUAKE_DRONE_OBJS)
	@mkdir -p ./.build
	$(CC) $(TEST_QUAKE_DRONE_OBJS) -o $@ $(LDFLAGS) -lm

# Leaf-graph route planner for a drone run.  DRONE_MAP=maps/e1m2.bsp picks the level.
test-drone: $(TEST_QUAKE_DRONE_BIN)
	@DRONE_MAP=$${DRONE_MAP:-maps/e1m1.bsp} ./$(TEST_QUAKE_DRONE_BIN)

# "Footage for each level": record and re-read a track for every base episode map.
# Each one writes assets/quake/drone<map>.dem and verifies it through the
# player's own packet framing; a failure stops the run with that map's log.
# The pak0.pak in the tree is the shareware one, so episodes 2-4 report SKIP and
# start being recorded as soon as a registered pak is dropped in.
test-drone-all: $(TEST_QUAKE_DRONE_BIN)
	@for m in e1m1 e1m2 e1m3 e1m4 e1m5 e1m6 e1m7 e1m8 \
	           e2m1 e2m2 e2m3 e2m4 e2m5 e2m6 e2m7 e2m8 \
	           e3m1 e3m2 e3m3 e3m4 e3m5 e3m6 e3m7 \
	           e4m1 e4m2 e4m3 e4m4 e4m5 e4m6 ; do \
	  log=.build/drone_$$m.log ; \
	  DRONE_MAP=maps/$$m.bsp ./$(TEST_QUAKE_DRONE_BIN) > $$log 2>&1 ; rc=$$? ; \
	  if [ $$rc -eq 77 ] ; then \
	    printf '%-6s SKIP — %s is not in this pak0.pak\n' $$m $$m; continue ; \
	  fi ; \
	  if [ $$rc -ne 0 ] ; then \
	    printf '%-6s FAILED — see %s\n' $$m $$log ; cat $$log ; exit 1 ; \
	  fi ; \
	  printf '%-6s %s\n' $$m "$$(grep 'assets/quake' $$log | head -1 | sed 's/^ *//')" ; \
	done

test-quake: $(TEST_QUAKEC_BIN) $(TEST_QUAKE_ENT_BIN) $(TEST_QUAKE_CTRL_BIN) $(TEST_QUAKE_REPLAY_BIN)
	@echo "=========================================================="
	@echo " Running Quake Verification, Glitches & Replay Suite..."
	@echo "=========================================================="
	@./$(TEST_QUAKEC_BIN)
	@./$(TEST_QUAKE_ENT_BIN)
	@./$(TEST_QUAKE_CTRL_BIN)
	@./$(TEST_QUAKE_REPLAY_BIN)

clean:
	@$(MAKE) -C verify clean >/dev/null 2>&1 || true
	rm -f *.toc
	rm -f *.aux
	rm -f *.log
	rm -f *.out
	rm -rf tad_bin
	rm -f $(POSIX_TARGET) $(QEMU_TARGET) $(TKERNEL_TARGET) $(SAKAMURA_TARGET) \
	      $(ARM32_TARGET) $(ARM64_TARGET) $(DEFAULT_TARGET) $(UEFI_TARGET) $(PC98_TARGET) \
	      $(M68K_TARGET) $(PS2_TARGET) $(PS2_ISO) $(MIPS_TARGET) $(TEST_MOZC_BIN) \
	      $(TEST_EDITOR_BIN) $(TEST_HMI_BIN) $(TEST_TAD_BIN) $(TEST_CHAT_BIN) \
	      $(TEST_SKI_BIN) $(TEST_GMENU_BIN) $(TEST_DRIVESETUP_BIN) \
	      $(TEST_QUAKEC_BIN) $(TEST_QUAKE_ENT_BIN) $(TEST_QUAKE_CTRL_BIN)
	find src verify -type f \( -name "*.o" \) -delete 2>/dev/null || true
	rm -f ./verify/models/bfs_allocator_model
	rm -f ./verify/models/bfs_btree_model
	rm -f ./verify/models/bfs_model


# ===================================================================
# Automated Headless Window Screenshot Capture Pipeline
# ===================================================================
CAPTURE_SCREENS_BIN = ./.build/capture_screens
CAPTURE_SCREENS_SRCS = src/tools/capture_screens.c \
                       src/emulators/msx/msx_app.c \
                       $(MSX_SRCS) \
                       src/window/dnd.c \
                       src/graphics/image_decode.c \
                       src/chokanji/pmc.c \
                       src/desktop/desktop.c \
                       src/settings/language.c \
                       src/settings/control_panel.c \
                       src/settings/appearance.c \
                       src/settings/desktop.c \
                       src/settings/display.c \
                       src/settings/input.c \
                       src/settings/sound.c \
                       src/settings/network.c \
                       src/settings/media.c \
                       src/settings/security.c \
                       src/settings/system.c \
                       src/settings/terminal.c \
                       src/desktop/global_menu.c \
                       src/desktop/about.c \
                       src/desktop/tracker.c \
                       src/apps/vobj_manager.c \
                       src/apps/t_editor.c \
                       src/apps/tad_browser.c \
                       src/apps/paint.c \
                       src/apps/gterm.c \
                       src/apps/audio_player.c \
                       src/media/orchestra.c \
                       src/media/orchestra_stage.c \
                       src/media/orchestra_arrange.c \
                       src/media/orchestra_piano.c \
                       src/media/orchestra_score.c \
                       src/media/orchestra_mix.c \
                       src/media/orchestra_insert.c \
                       src/media/orchestra_about.c \
                       src/media/orchestra_session.c \
                       src/media/orchestra_smf.c \
                       src/media/gst_rt.c \
                       src/media/lukhash_score.c \
                       src/apps/chat.c \
                       src/apps/chat_xml.c \
                       src/tip/tip_ife.c \
                       src/tip/mozc_kkc.c \
                       src/tip/wylie.c \
                       src/tip/tibetan_dict.c \
                       src/font/troncode.c \
                       src/font/jis_fonts.c \
                       src/font/tibetan_fonts.c \
                       src/window/wnd.c \
                       src/window/app_menu.c \
                       src/graphics/dp_core.c \
                       src/graphics/icons_bundle.c \
                       src/fs/blk_mem.c \
                       src/fs/blk_file.c \
                       src/fs/blk_qcow2.c \
                       src/fs/blk_part.c \
                       src/fs/vol.c \
                       src/fs/file.c \
                       src/clu/lang.c \
                       src/clu/term.c \
                       src/clu/tty.c \
                       src/clu/vfs.c \
                       src/clu/sc/files.c \
                       src/clu/sc/input.c \
                       src/clu/sc/menus.c \
                       src/clu/sc/sc.c \
                       src/clu/tv/tv.c \
                       src/clu/be/native.c \
                       $(BE_DASM_SRCS) \
                       $(CLU_APPS_SRCS) \
                       src/clu/clu.c \
                       src/apps/b_drivesetup.c \
                       src/apps/clarity.c \
                       src/apps/clarity_layout.c \
                       src/apps/clarity_render.c \
                       src/apps/clarity_export.c \
                       src/font/font_mgr.c \
                       src/gl/gl_dispatch.c \
                       src/gl/gl_backend_choice.c \
                       src/gl/egl_surface.c \
                       src/gl/backend_virgl.c \
                       src/apps/xmb.c \
                       src/apps/xmb_wave.c \
                       src/demo/lilcu64_wa.c \
                       src/demo/lilcu64_score.c \
                       src/demo/lilcu64_synth.c \
                       src/demo/lilcu64_insert.c \
                       src/demo/lilcu64_demo.c \
                       src/drivers/ps2/ps2_vu0_kernel.c

CAPTURE_SCREENS_OBJS = $(CAPTURE_SCREENS_SRCS:.c=.test.o)

$(CAPTURE_SCREENS_BIN): $(CAPTURE_SCREENS_OBJS)
	$(CC) $(CAPTURE_SCREENS_OBJS) -o $@ $(LDFLAGS) -lm -lz

# ═══════════════════════════════════════════════════════════════════
# µBTRON-FOMA Mobile UI Toolkit Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_FOMA_SRCS = verify/tests/test_foma_ui.c src/desktop/desktop_mobile.c \
                 src/graphics/dp_core.c src/font/troncode.c src/font/jis_fonts.c src/font/tibetan_fonts.c
TEST_FOMA_OBJS = $(TEST_FOMA_SRCS:.c=.test.o)
TEST_FOMA_BIN  = ./.build/test_foma_ui

test-foma-ui: $(TEST_FOMA_BIN)
	@echo "=========================================================="
	@echo " Running µBTRON-FOMA Mobile UI Toolkit Unit Tests..."
	@echo "=========================================================="
	@./$(TEST_FOMA_BIN)

$(TEST_FOMA_BIN): $(TEST_FOMA_OBJS)
	$(CC) $(TEST_FOMA_OBJS) -o $@ $(LDFLAGS) -lm

screenshots: $(CAPTURE_SCREENS_BIN)
	@echo "=========================================================="
	@echo " Generating Isolated Window Screenshots from C99 Source..."
	@echo "=========================================================="
	@./$(CAPTURE_SCREENS_BIN)
	@python3 scripts/raw_to_png.py
	@python3 scripts/populate_doc_screens.py

sreenshots: screenshots

# ═══════════════════════════════════════════════════════════════════
# µBTRON-FOMA Automated Screen Capture & Documentation
# ═══════════════════════════════════════════════════════════════════
CAPTURE_FOMA_BIN  = ./.build/capture_foma
CAPTURE_FOMA_SRCS = src/tools/capture_foma.c \
                    src/desktop/desktop_mobile.c \
                    src/desktop/workbench_mobile.c \
                    src/window/wnd.c \
                    src/chokanji/pmc.c \
                    src/window/app_menu.c \
                    src/apps/gterm.c \
                    src/apps/t_editor.c \
                    src/settings/terminal.c \
                    src/tip/tip_ife.c \
                    src/tip/mozc_kkc.c \
                    src/tip/wylie.c \
                    src/tip/tibetan_dict.c \
                    src/graphics/dp_core.c \
                    src/graphics/icons_bundle.c \
                    src/font/troncode.c \
                    src/font/jis_fonts.c \
                    src/font/tibetan_fonts.c \
                    src/fs/blk_mem.c \
                    src/fs/blk_file.c \
                    src/fs/vol.c \
                    src/fs/file.c \
                    $(CLU_APPS_SRCS) \
                    src/clu/clu.c

CAPTURE_FOMA_OBJS = $(CAPTURE_FOMA_SRCS:.c=.test.o)

$(CAPTURE_FOMA_BIN): $(CAPTURE_FOMA_OBJS)
	$(CC) $(CAPTURE_FOMA_OBJS) -o $@ $(LDFLAGS) -lm

foma-screens: $(CAPTURE_FOMA_BIN)
	@echo "=========================================================="
	@echo " Generating Isolated µBTRON-FOMA Mobile Screenshots..."
	@echo "=========================================================="
	@./$(CAPTURE_FOMA_BIN)
	@python3 scripts/update_foma_screens.py

# ═══════════════════════════════════════════════════════════════════
# SegUI (Segmentation UI) Automated Screen Capture - 480x272 panel
# ═══════════════════════════════════════════════════════════════════
CAPTURE_SEGUI_BIN  = ./.build/capture_segui
CAPTURE_SEGUI_SRCS = src/tools/capture_segui.c \
                     src/segui/segui.c \
                     src/segui/segui_demo.c \
                     src/segui/segui_phone.c \
                     src/graphics/dp_core.c \
                     src/font/troncode.c \
                     src/font/jis_fonts.c \
                     src/font/tibetan_fonts.c

CAPTURE_SEGUI_OBJS = $(CAPTURE_SEGUI_SRCS:.c=.test.o)

$(CAPTURE_SEGUI_BIN): $(CAPTURE_SEGUI_OBJS)
	$(CC) $(CAPTURE_SEGUI_OBJS) -o $@ $(LDFLAGS) -lm

segui-screens: $(CAPTURE_SEGUI_BIN)
	@echo "=========================================================="
	@echo " Generating SegUI 480x272 Screen Set..."
	@echo "=========================================================="
	@./$(CAPTURE_SEGUI_BIN)
	@python3 scripts/update_segui_screens.py

# ===================================================================
# Quake Automated Screenshot Pipeline — 21:9 (672×288) ARGB frames
# ===================================================================
CAPTURE_QUAKE_BIN  = ./.build/capture_quake
CAPTURE_QUAKE_SRCS = verify/scratch/capture_quake.c \
                     src/quake/core/mathlib.c \
                     src/quake/core/mem.c \
                     src/quake/core/cvar.c \
                     src/quake/core/cmd.c \
                     src/quake/core/wad.c \
                     src/quake/core/world.c \
                     src/quake/core/pr_exec.c \
                     src/quake/core/sv_phys.c \
                     src/quake/core/sv_main.c \
                     src/quake/sys/sys_btron.c \
                     src/quake/sys/in_btron.c \
                     src/quake/sys/fs_btron.c \
                     src/quake/render/r_btron_gl.c \
                     src/quake/render/r_brush.c \
                     src/quake/render/r_light.c \
                     src/quake/render/r_surf.c \
                     src/quake/render/r_alias.c \
                     src/quake/render/r_part.c \
                     src/quake/render/texture.c \
                     src/quake/core/cl_demo.c \
                     src/quake/app/quake_ui.c \
                     src/gl/gl_dispatch.c \
                     src/gl/gl_backend_choice.c \
                     src/gl/egl_surface.c \
                     src/gl/backend_virgl.c

CAPTURE_QUAKE_OBJS = $(CAPTURE_QUAKE_SRCS:.c=.test.o)

$(CAPTURE_QUAKE_BIN): $(CAPTURE_QUAKE_OBJS)
	@mkdir -p ./.build
	$(CC) $(CAPTURE_QUAKE_OBJS) -o $@ $(LDFLAGS) -lm -lz

quake-screens: $(CAPTURE_QUAKE_BIN)
	@echo "=========================================================="
	@echo " Generating Quake 21:9 (672x288) Screenshot Set..."
	@echo "=========================================================="
	@mkdir -p /tmp/btron_raw_screens
	@./$(CAPTURE_QUAKE_BIN)
	@python3 scripts/raw_to_png.py
	@python3 scripts/update_quake_screens.py
