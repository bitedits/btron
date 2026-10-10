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
        check-structure test-ps2-softfloat test-gl-math \
        html2tad book2tad tad_bin test test-kernel test-yoko test-yoko4 test-m68k test-mips test-ps2 test-foma test-foma-ui foma-screens \
        segui-screens \
        test-mozc test-editor test-hmi test-tad test-chat test-wylie verify test-fs test-chokanji test-quake test-replay \
        test-hull test-view test-secret test-render test-drone test-drone-all test-zexall msx-trace \
        mkbtronfs btron_sys.vol \
        run-posix run-qemu run-kernel run-yoko run-yoko4 run-sakamura run-foma run-uefi run-eufi run-uefu run-pc98 run-m68k run-ps2 run-mips debug-virtio debug-gdb clean \
        ps2-cfg \
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
# The one exception is that same file, which crosses no float argument at all and so
# is assembled with the FPU switched back on (see PS2_FP near %.pbench.o): the EE's
# single-precision unit has been paying for the .d law ever since, at 23.9 s an XMB
# frame.  Nothing else in the image may take that flag.
PS2_CC      ?= $(LLVM_CLANG) --target=mipsel-unknown-elf -march=mips2 -mabi=32 -msoft-float -ffreestanding -nostdlib
MIPS_LD ?= $(if $(shell command -v mipsel-linux-gnu-ld 2>/dev/null),mipsel-linux-gnu-ld,$(LLD_BIN) -EL)

# BCM283x bare-metal flags (TYPE_RPI=2 → BCM2836, Pi 2B, Cortex-A7)
BCM_INC      = -Iinclude -Iinclude/arch/bcm283x -Isrc/kernel -Isrc/cores
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

# ── tronMSX core: the machine src/apps/msx_app.c drives ───────────────────
MSX_SRCS = src/emulators/msx/z80.c        \
           src/emulators/msx/slots.c      \
           src/emulators/msx/bios.c       \
           src/emulators/msx/vdp.c        \
           src/emulators/msx/psg.c        \
           src/emulators/msx/ppi.c        \
           src/emulators/msx/kbd.c        \
           src/emulators/msx/tronmsx.c

# ── Common SDL2-hosted app sources ────────────────────────────────
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
              src/apps/orchestra.c     \
              src/apps/chat.c          \
              src/apps/chat_xml.c      \
              src/apps/msx_app.c       \
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
              src/apps/clu.c

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
    src/demo/lilcu64_demo.c

VIRGL_SRCS = \
    src/gl/gl_dispatch.c \
    src/gl/egl_surface.c \
    src/gl/backend_virgl.c \
    src/apps/glgears.c \
    src/apps/xmb.c \
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
    src/gl/egl_surface.c \
    src/gl/backend_tinygl.c \
    src/gl/backend_virgl.c \
    src/apps/glgears.c \
    src/apps/xmb.c \
    $(QUAKE_SRCS) \
    $(DEMO_SRCS)

# ── X86_64 / EMT64 UEFI build (Target 4) ────────────────────────
UEFI_STARTUP = src/cores/core_boot.c src/cores/core_smp.c
UEFI_SRCS    = $(UEFI_STARTUP)          \
               src/cores/core_init.c   \
               src/kernel/libstr.c      \
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
               src/drivers/vesa/vesa.c  \
               src/drivers/pc98/input/pc98_mouse.c \
               src/drivers/pc98/input/pc98_kbd.c \
               src/drivers/uefi/ps2_mouse.c \
               $(COMMON_NO_SDL_SRCS)

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
            src/apps/clu.c

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
    src/apps/orchestra.c   \
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
    src/clu/vfs.c          \
    src/apps/clu.c

BAREMETAL_STARTUP  = src/drivers/bcm283x/cpu/startup_arm.c
BAREMETAL_LD       = src/drivers/bcm283x/cpu/link.ld
ARM32_BAREMETAL_SRCS = src/cores/core_init.c src/cores/core_yoko.c $(TKERNEL_SAKAMURA_SRCS) $(ARCH_BCM_SRCS) $(BAREMETAL_STARTUP) $(COMMON_NO_SDL_SRCS)
ARM64_BAREMETAL_SRCS = src/cores/core_init.c src/cores/core_arm64.c $(TKERNEL_SAKAMURA_SRCS) $(ARCH_BCM64_SRCS) $(BAREMETAL_STARTUP) $(COMMON_NO_SDL_SRCS)

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

src/apps/clu.host.o: src/apps/clu.c
	$(CC) $(CFLAGS) -DBTRON_TARGET=0 -MMD -MP -c $< -o $@

# Header dependencies recorded by the -MMD flags above: editing src/clu/vfs.h or
# src/clu/sc/sokhatsky.h must rebuild every object that included it, or a link
# mixes objects that disagree on struct layout.
-include $(shell find src verify -name '*.host.d' -o -name '*.posix.d' -o -name '*.test.d' 2>/dev/null)

# ── mkbtronfs — host image builder ────────────────────────────────────
mkbtronfs: src/tools/mkbtronfs.c $(FS_OBJS)
	$(CC) $(CFLAGS) -Isrc $^ -o mkbtronfs
	@echo "[FS] mkbtronfs built."

btron_sys.vol: mkbtronfs src/tools/manifest.txt
	./mkbtronfs src/tools/manifest.txt -o btron_sys.vol
	@echo "[FS] btron_sys.vol written."
	@xxd btron_sys.vol | head -2

btron_anders.vol: mkbtronfs src/tools/manifest_anders.txt
	./mkbtronfs src/tools/manifest_anders.txt -o btron_anders.vol -l ANDERS
	@echo "[FS] btron_anders.vol written."


# ── FS unit tests ──────────────────────────────────────────────────────
TEST_FS_BIN = ./.build/test_fs
$(TEST_FS_BIN): verify/tests/test_fs.c $(FS_OBJS) src/apps/clu.host.o
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
$(TEST_CHOKANJI_BIN): verify/tests/test_chokanji.c $(FS_OBJS) src/apps/clu.host.o
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

bin/sc: $(SC_OBJS) src/clu/sc/sc.standalone.o src/clu/tv/tv.host.o $(CLU_CORE_OBJS) src/apps/clu.host.o $(FS_OBJS)
	@mkdir -p bin
	$(CC) $(CFLAGS) -Isrc -Iinclude $^ -o $@

TEST_TERMIOS_BIN = ./.build/test_termios
$(TEST_TERMIOS_BIN): verify/tests/test_termios_clu.c $(CLU_CORE_OBJS) src/clu/sc/files.host.o src/clu/sc/input.host.o src/clu/sc/menus.host.o src/clu/sc/sc.host.o src/clu/tv/tv.host.o src/apps/clu.host.o $(FS_OBJS)
	@mkdir -p .build
	$(CC) $(CFLAGS) -Isrc -Iinclude $^ -o $@


test-termios: $(TEST_TERMIOS_BIN) btron_sys.vol btron_anders.vol
	./$(TEST_TERMIOS_BIN)
	@echo "[CLU] All termios, TV, and SC tests passed."



# ── Clarity DTP Frame & Control tests ────────────────────────────────
TEST_CLARITY_BIN = ./.build/test_clarity_frames
$(TEST_CLARITY_BIN): verify/tests/test_clarity_frames.c src/graphics/image_decode.posix.o src/window/dnd.posix.o src/apps/clarity.posix.o src/apps/clarity_layout.posix.o src/apps/clarity_render.posix.o src/apps/clarity_export.posix.o src/window/wnd.posix.o src/window/app_menu.posix.o src/window/event.posix.o src/graphics/dp_core.posix.o src/graphics/icons_bundle.posix.o src/font/troncode.posix.o src/font/jis_fonts.posix.o src/font/tibetan_fonts.posix.o src/font/font_mgr.posix.o src/cores/core_posix.posix.o src/cores/core_init.posix.o src/drivers/virtio/virtio.posix.o src/vobject/vobj.posix.o src/desktop/desktop.posix.o src/desktop/workbench.posix.o src/desktop/tracker.posix.o src/desktop/about.posix.o src/desktop/global_menu.posix.o src/apps/vobj_manager.posix.o src/apps/tad_browser.posix.o src/apps/gterm.posix.o src/apps/t_editor.posix.o src/apps/audio_player.posix.o src/apps/b_drivesetup.posix.o src/apps/orchestra.posix.o src/apps/chat.posix.o src/apps/chat_xml.posix.o src/settings/language.posix.o src/settings/control_panel.posix.o src/settings/appearance.posix.o src/settings/desktop.posix.o src/settings/display.posix.o src/settings/input.posix.o src/settings/sound.posix.o src/settings/network.posix.o src/settings/media.posix.o src/settings/security.posix.o src/settings/system.posix.o src/settings/terminal.posix.o src/hmi/hmi_core.posix.o src/hmi/hmi_switch.posix.o src/hmi/hmi_selector.posix.o src/hmi/hmi_volume.posix.o src/hmi/hmi_meter.posix.o src/hmi/hmi_controller.posix.o src/hmi/hmi_panel.posix.o src/tip/mozc_kkc.posix.o src/tip/tip_ife.posix.o src/tip/tip_task.posix.o src/tip/tip_vobj.posix.o src/tip/wylie.posix.o src/tip/tibetan_dict.posix.o src/fs/blk_mem.posix.o src/fs/blk_file.posix.o src/fs/blk_qcow2.posix.o src/fs/blk_part.posix.o src/fs/vol.posix.o src/fs/file.posix.o src/apps/clu.posix.o
	$(CC) $(CFLAGS) -Isrc $^ -o $@ $(LDFLAGS) $(SDL_LIBS)

test-clarity: $(TEST_CLARITY_BIN)
	./$(TEST_CLARITY_BIN)
	@echo "[CLARITY] All Clarity tests passed."

# ── Clarity Hypermedia & DND tests ──────────────────────────────────
TEST_CLARITY_HYPERMEDIA_BIN = ./.build/test_clarity_hypermedia
$(TEST_CLARITY_HYPERMEDIA_BIN): verify/tests/test_clarity_hypermedia.c src/graphics/image_decode.posix.o src/window/dnd.posix.o src/apps/clarity.posix.o src/apps/clarity_layout.posix.o src/apps/clarity_render.posix.o src/apps/clarity_export.posix.o src/window/wnd.posix.o src/window/app_menu.posix.o src/window/event.posix.o src/graphics/dp_core.posix.o src/graphics/icons_bundle.posix.o src/font/troncode.posix.o src/font/jis_fonts.posix.o src/font/tibetan_fonts.posix.o src/font/font_mgr.posix.o src/cores/core_posix.posix.o src/cores/core_init.posix.o src/drivers/virtio/virtio.posix.o src/vobject/vobj.posix.o src/desktop/desktop.posix.o src/desktop/workbench.posix.o src/desktop/tracker.posix.o src/desktop/about.posix.o src/desktop/global_menu.posix.o src/apps/vobj_manager.posix.o src/apps/tad_browser.posix.o src/apps/gterm.posix.o src/apps/t_editor.posix.o src/apps/audio_player.posix.o src/apps/b_drivesetup.posix.o src/apps/orchestra.posix.o src/apps/chat.posix.o src/apps/chat_xml.posix.o src/settings/language.posix.o src/settings/control_panel.posix.o src/settings/appearance.posix.o src/settings/desktop.posix.o src/settings/display.posix.o src/settings/input.posix.o src/settings/sound.posix.o src/settings/network.posix.o src/settings/media.posix.o src/settings/security.posix.o src/settings/system.posix.o src/settings/terminal.posix.o src/hmi/hmi_core.posix.o src/hmi/hmi_switch.posix.o src/hmi/hmi_selector.posix.o src/hmi/hmi_volume.posix.o src/hmi/hmi_meter.posix.o src/hmi/hmi_controller.posix.o src/hmi/hmi_panel.posix.o src/tip/mozc_kkc.posix.o src/tip/tip_ife.posix.o src/tip/tip_task.posix.o src/tip/tip_vobj.posix.o src/tip/wylie.posix.o src/tip/tibetan_dict.posix.o src/fs/blk_mem.posix.o src/fs/blk_file.posix.o src/fs/blk_qcow2.posix.o src/fs/blk_part.posix.o src/fs/vol.posix.o src/fs/file.posix.o src/apps/clu.posix.o
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
                  $(COMMON_NO_SDL_SRCS)
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
# Which of the two software rasterizers a GL window on this image gets.  The port
# links both -- src/gl/tinygl/ and src/gl/backend_virgl.c are in the source list --
# so this is a choice, not a capability, and it is worth choosing because the two
# are not the same size: backend_virgl.c keeps one float per pixel for depth, which
# at 768x500 is 1.5 MB to allocate and 384k software-float stores to fill, against
# TinyGL's 16-bit z of half that.  PS2_GL=virgl builds the old combination, and the
# [XMBT] rows time the same load against both.
PS2_GL          ?= tinygl
PS2_TARGET     = btron-ps2.elf
PS2_ISO        = btron-ps2.iso
PS2_LD_SCRIPT  = src/drivers/ps2/ps2.ld
PS2_CFLAGS     = -O2 -Wall -Wextra -std=c99 -ffreestanding -nostdlib \
                 -DBTRON_TARGET=8 -DBTRON_PS2_TARGET -DBTRON_AUTO_GUI=$(AUTO_GUI) \
                 -DBTRON_HID_TRACE=$(HIDTRACE) -DBTRON_GL_BACKEND_TINYGL \
                 -Iinclude -Iinclude/gl -Iinclude/drivers -Isrc/kernel -Isrc/cores -Isrc/drivers/ps2
ifeq ($(PS2_GL),virgl)
PS2_CFLAGS    += -DBTRON_GL_PREFER_VIRGL
endif
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
                  src/clu/tv/tv.c
# The GL layer this port links, minus the app bundle GL_SRCS also carries.  Quake's
# hunk arena alone is a 64 MiB static array and the Lil Cu demo's node table 3 MiB
# more, against an Emotion Engine with 32 MB of RDRAM: an image that big does not
# fail to build, it simply puts every later .bss object -- the event queue, the
# deskbar, the tracker -- past the end of memory, where a write to them is accepted
# and a read of them comes back zero.  The desktop noticed as input going dead
# (2026-10-09, see the [BENCH] echo row); ps2.ld now refuses the link instead.
# The launcher reaches its apps through weak open_*_window() externs, so an app that
# is not linked here is a menu row that does nothing rather than a link error.
PS2_GL_SRCS    = src/gl/gl_dispatch.c     \
                 src/gl/egl_surface.c     \
                 src/gl/backend_tinygl.c  \
                 src/gl/backend_virgl.c   \
                 src/apps/xmb.c           \
                 src/apps/glgears.c
PS2_SRCS       = $(PS2_STARTUP)           \
                 src/cores/core_init.c    \
                 src/drivers/ps2/ps2_gs.c \
                 src/drivers/ps2/ps2_builtins.c \
                 src/drivers/ps2/ps2_sio.c \
                 src/drivers/ps2/ps2_pad.c \
                 src/drivers/ps2/ps2_iopram.c \
                 src/drivers/ps2/ps2_usb.c \
                 $(CLU_TERMIO_SRCS)       \
                 $(TINYGL_SRCS)           \
                 $(PS2_GL_SRCS)           \
                 src/kernel/libstr.c      \
                 $(COMMON_NO_SDL_SRCS)
PS2_OBJS       = src/drivers/ps2/boot_ps2.ps2.o         \
                 src/drivers/ps2/ps2_gs_reg.ps2.o      \
                 $(PS2_SRCS:.c=.ps2.o)

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
$(PS2_BENCH_OBJ): PS2_CFLAGS += -DBTRON_PS2_BENCH=1 -DBTRON_PS2_BENCH_APP=$(BENCH_APP) \
                                -DBTRON_PS2_BENCH_APP_FRAMES=$(BENCH_APP_FRAMES)u

%.pbench.o: %.c
	$(PS2_CC) $(PS2_CFLAGS) -MMD -MP -c $< -o $@

# The one object in the image that may hold COP1 instructions.  -msoft-float is an
# argument-passing law (the note at PS2_CC), and src/drivers/ps2/ps2_builtins.c breaks
# none of it: every entry point there takes and returns an integer register holding
# IEEE-754 bits, and the only float-shaped things in the file are inside __asm__
# strings.  It still needs the flag off to assemble at all -- under -msoft-float clang
# rejects `mtc1` with "instruction requires a CPU feature not currently enabled", which
# is why the EE's own FPU has been unusable for single precision until now.  The link
# keeps recording `FP ABI: Soft float` for the whole image, and mipsel-linux-gnu-ld
# accepts the mixed .MIPS.abiflags the way it is.  PS2_FP=soft builds without it, and
# then the bit engine answers every call exactly as it did before 2026-10-10.
PS2_FP ?= hw
PS2_FP_OBJ = src/drivers/ps2/ps2_builtins.ps2.o
ifeq ($(PS2_FP),hw)
$(PS2_FP_OBJ): PS2_CC := $(subst -msoft-float,-mhard-float,$(PS2_CC))
$(PS2_FP_OBJ): PS2_CFLAGS += -DBTRON_PS2_FP_HW
endif

ps2-bench: $(PS2_BENCH_TARGET)

$(PS2_BENCH_TARGET): $(PS2_BENCH_OBJS) $(PS2_LD_SCRIPT)
	$(MIPS_LD) -T $(PS2_LD_SCRIPT) $(PS2_BENCH_OBJS) -o $@
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

$(PS2_ISO): $(PS2_TARGET)
	@echo "=========================================================="
	@echo " Packaging Bootable PS2 Disc ISO: $@"
	@echo "=========================================================="
	@rm -rf build/ps2_iso
	@mkdir -p build/ps2_iso
	@printf "BOOT2 = cdrom0:\\\\BTRON.ELF;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n" > build/ps2_iso/SYSTEM.CNF
	@cp $(PS2_TARGET) build/ps2_iso/BTRON.ELF
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
	@file $@

run-ps: run-ps2

# Prepare the run's data directory.  PCSX2 writes the config file back on exit, so
# a run can dirty pcsx2/PCSX2/inis/PCSX2.ini in git -- revert it rather than
# delete it.  The [USB2] keyboard binding and the [Hotkeys] entries trimmed for it
# are what let a host keypress reach the emulated device at all, and the BIOS
# images are linked rather than copied because they are Sony's, not ours.
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

test-ps2: $(PS2_TARGET) $(PS2_ISO)
	@./scripts/test_ps2.sh

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
TEST_CLIP_SRCS = verify/tests/test_desktop_clip.c src/desktop/tracker.c src/desktop/desktop.c src/settings/appearance.c \
                 src/vobject/vobj.c src/desktop/about.c src/window/wnd.c src/chokanji/pmc.c \
                 src/window/app_menu.c \
                 src/graphics/dp_core.c src/graphics/icons_bundle.c src/font/troncode.c src/font/jis_fonts.c src/font/tibetan_fonts.c
TEST_CLIP_OBJS = $(TEST_CLIP_SRCS:.c=.test.o)
TEST_CLIP_BIN  = ./.build/test_desktop_clip

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
TEST_XMB_SRCS = verify/tests/test_xmb_render.c src/apps/xmb.c \
                src/gl/gl_dispatch.c src/gl/egl_surface.c src/gl/backend_virgl.c \
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

$(TEST_DRIVESETUP_BIN): $(TEST_DRIVESETUP_OBJS) $(FS_OBJS) src/apps/clu.host.o
	$(CC) $(TEST_DRIVESETUP_OBJS) $(FS_OBJS) src/apps/clu.host.o -o $@ $(LDFLAGS) -lm

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

test: test-tad test-editor test-chat test-mozc test-wylie test-hmi test-ski test-tracker test-deskclip test-settings test-global-menu test-app-menu test-drivesetup test-fs test-quake test-ps2-softfloat test-gl-math
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
                         src/gl/gl_dispatch.c src/gl/backend_virgl.c
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
                       src/apps/msx_app.c \
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
                       src/apps/orchestra.c \
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
                       src/apps/clu.c \
                       src/apps/b_drivesetup.c \
                       src/apps/clarity.c \
                       src/apps/clarity_layout.c \
                       src/apps/clarity_render.c \
                       src/apps/clarity_export.c \
                       src/font/font_mgr.c

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
                    src/apps/clu.c

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
