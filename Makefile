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
CFLAGS ?= -O2 -Wall -Wextra -std=c99 -Iinclude -Iinclude/drivers -Isrc/kernel -Isrc/cores

.PHONY: all posix qemu kernel tkernel sakamura foma uefi pc98 arm-elf arm64-elf m68k ps2 mips \
        html2tad book2tad tad_bin test test-kernel test-yoko test-yoko4 test-m68k test-mips test-ps2 test-foma foma-screens \
        test-mozc test-editor test-hmi test-tad test-chat test-wylie verify \
        run-posix run-qemu run-kernel run-yoko run-yoko4 run-sakamura run-foma run-uefi run-eufi run-uefu run-pc98 run-m68k run-ps2 run-mips debug-virtio debug-gdb clean

QEMU_ARM     ?= qemu-system-arm
QEMU_AARCH64 ?= qemu-system-aarch64
QEMU_X86_64  ?= qemu-system-x86_64
QEMU_M68K    ?= qemu-system-m68k
QEMU_MIPS    ?= qemu-system-mipsel
QEMU_MIPS64  ?= qemu-system-mips64el
PCSX2_BIN    ?= /Applications/PCSX2.app/Contents/MacOS/PCSX2
M68K_CC      ?= m68k-elf-gcc

LLVM_CLANG := $(shell for p in /opt/homebrew/opt/llvm/bin/clang /usr/local/opt/llvm/bin/clang /usr/lib/llvm-*/bin/clang clang; do if command -v "$$p" >/dev/null 2>&1; then echo "$$p"; break; fi; done)
LLD_BIN    := $(shell for p in /opt/homebrew/bin/ld.lld /usr/local/bin/ld.lld /usr/bin/ld.lld ld.lld /opt/homebrew/opt/llvm/bin/ld.lld /usr/lib/llvm-*/bin/ld.lld; do if command -v "$$p" >/dev/null 2>&1; then echo "$$p"; break; fi; done)

# ARM32: Cortex-A7 for Pi 2B (BCM2836)
ARM32_CC ?= $(LLVM_CLANG) --target=arm-none-eabi -mcpu=cortex-a7 -marm -fuse-ld=lld -ffreestanding -nostdlib
# AArch64: Cortex-A72 for Pi 4B (BCM2711) — kept for Pi4-only development
ARM64_CC ?= $(LLVM_CLANG) --target=aarch64-none-elf -mcpu=cortex-a72 -fuse-ld=lld -ffreestanding -nostdlib
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
PS2_CC      ?= $(LLVM_CLANG) --target=mipsel-unknown-elf -march=mips3 -mabi=32 -ffreestanding -nostdlib
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
ifeq ($(UNAME_S), Darwin)
    SDL_CFLAGS   := $(shell sdl2-config --cflags 2>/dev/null || echo "-I/usr/local/include/SDL2")
    SDL_LIBS     := $(shell sdl2-config --libs 2>/dev/null || echo "-lSDL2") \
                    -lz -lpthread -framework ApplicationServices -framework Cocoa
    QEMU_DISPLAY        := -display cocoa,show-cursor=on,zoom-to-fit=on
    # Match M68K display flags with show-cursor=on and zoom-to-fit=on
    KERNEL_DISPLAY      := $(QEMU_DISPLAY)
else ifeq ($(UNAME_S), Linux)
    SDL_CFLAGS   := $(shell sdl2-config --cflags 2>/dev/null || pkg-config --cflags sdl2 2>/dev/null || echo "-I/usr/include/SDL2")
    SDL_LIBS     := $(shell sdl2-config --libs 2>/dev/null || pkg-config --libs sdl2 2>/dev/null || echo "-lSDL2") \
                    -lz -lm -lpthread
    QEMU_DISPLAY        := -display default,show-cursor=on
    KERNEL_DISPLAY      := -display default
else
    SDL_CFLAGS   := -I/usr/include/SDL2
    SDL_LIBS     := -lSDL2 -lz -lgdi32 -lpthread
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

# ── Common SDL2-hosted app sources ────────────────────────────────
COMMON_SRCS = src/graphics/dp_core.c   \
              src/graphics/icons_bundle.c \
              src/graphics/dp_sdl.c    \
              src/font/troncode.c      \
              src/font/jis_fonts.c     \
              src/font/tibetan_fonts.c \
              src/window/wnd.c         \
              src/window/app_menu.c    \
              src/window/event.c       \
              src/vobject/vobj.c       \
              src/desktop/desktop.c    \
              src/desktop/workbench.c  \
              src/desktop/tracker.c    \
              src/desktop/about.c      \
              src/desktop/global_menu.c \
              src/desktop/main.c       \
              src/apps/vobj_manager.c  \
              src/apps/tad_browser.c   \
              src/apps/gterm.c         \
              src/apps/t_editor.c      \
              src/apps/audio_player.c  \
              src/apps/orchestra.c     \
              src/apps/chat.c          \
              src/apps/chat_xml.c      \
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
              $(IME_SRCS)

# ── POSIX build (Target 0) ────────────────────────────────────────
POSIX_STARTUP = src/cores/core_posix.c
POSIX_SRCS    = $(POSIX_STARTUP)        \
                src/drivers/virtio/virtio.c \
                src/cores/core_init.c  \
                $(COMMON_SRCS)

# ── QEMU VirtIO build (Target 1) ─────────────────────────────────
QEMU_STARTUP = src/cores/core_virtio.c
QEMU_SRCS    = $(QEMU_STARTUP)          \
               src/drivers/virtio/virtio.c \
               src/cores/core_init.c   \
               $(COMMON_SRCS)

# ── X86_64 / EMT64 UEFI build (Target 4) ────────────────────────
UEFI_STARTUP = src/cores/core_boot.c src/cores/core_smp.c
UEFI_SRCS    = $(UEFI_STARTUP)          \
               src/cores/core_init.c   \
               src/kernel/libstr.c      \
               src/drivers/vesa/vesa.c  \
               src/drivers/uefi/ps2_mouse.c \
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
            $(IME_SRCS)                 \
            src/apps/gterm.c            \
            src/apps/t_editor.c         \
            src/settings/terminal.c     \
            src/desktop/desktop_mobile.c \
            src/desktop/workbench_mobile.c \
            src/desktop/main_mobile.c

# Bare-metal: SDL-free subset only
COMMON_NO_SDL_SRCS = \
    src/graphics/dp_core.c \
    src/graphics/icons_bundle.c \
    src/font/troncode.c    \
    src/font/jis_fonts.c   \
    src/font/tibetan_fonts.c \
    src/window/wnd.c       \
    src/window/app_menu.c  \
    src/window/event.c     \
    src/vobject/vobj.c     \
    src/desktop/desktop.c  \
    src/desktop/workbench.c \
    src/desktop/tracker.c  \
    src/desktop/about.c    \
    src/desktop/global_menu.c \
    src/apps/vobj_manager.c \
    src/apps/tad_browser.c \
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
    $(IME_SRCS)

BAREMETAL_STARTUP  = src/drivers/bcm283x/cpu/startup_arm.c
BAREMETAL_LD       = src/drivers/bcm283x/cpu/link.ld
ARM32_BAREMETAL_SRCS = src/cores/core_init.c src/cores/core_yoko.c $(TKERNEL_SAKAMURA_SRCS) $(ARCH_BCM_SRCS) $(BAREMETAL_STARTUP) $(COMMON_NO_SDL_SRCS)
ARM64_BAREMETAL_SRCS = src/cores/core_init.c src/cores/core_arm64.c $(TKERNEL_SAKAMURA_SRCS) $(ARCH_BCM_SRCS) $(BAREMETAL_STARTUP) $(COMMON_NO_SDL_SRCS)

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

# ── Output names ──────────────────────────────────────────────────
POSIX_TARGET   = btron-posix
QEMU_TARGET    = btron-qemu.elf
TKERNEL_TARGET = btron-tkernel.elf
SAKAMURA_TARGET = btron-sakamura.elf
FOMA_TARGET     = btron-foma.elf
UEFI_TARGET     = btron-uchida.elf # In honor of Kota Uchida (MikanOS UEFI pioneer)
PC98_TARGET     = btron-morris.elf # In honor of Awe Morris (zedBSD PC-98 pioneer)
ARM32_TARGET   = btron-arm-baremetal.elf     # Pi 2B — BCM2836, Cortex-A7, ARMv7
ARM64_TARGET   = btron-aarch64-baremetal.elf # Pi 4B — BCM2711, Cortex-A72, AArch64
DEFAULT_TARGET = btron

TKERNEL_INC = -D_RPI_BCM283x_ -DTYPE_RPI=2 \
              -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast \
              -Iinclude -Iinclude/arch/bcm283x -Isrc/kernel -Isrc/cores

all: posix qemu kernel sakamura foma uefi pc98

# ═══════════════════════════════════════════════════════════════════
# POSIX Desktop
# ═══════════════════════════════════════════════════════════════════
posix: tad_bin $(POSIX_TARGET)
	@ln -sf $(POSIX_TARGET) $(DEFAULT_TARGET)
	@echo "=========================================================="
	@echo " B-System POSIX Kernel & Desktop successfully built!"
	@echo " Startup File: $(POSIX_STARTUP)"
	@echo " Run './btron' or 'make run-posix' to start."
	@echo "=========================================================="

%.posix.o: %.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -DBTRON_TARGET=0 -c $< -o $@

$(POSIX_TARGET): $(POSIX_OBJS)
	$(CC) $(POSIX_OBJS) -o $@ $(LDFLAGS) $(SDL_LIBS)

run-posix: $(POSIX_TARGET)
	./$(POSIX_TARGET)

run-sakamura: $(SAKAMURA_TARGET)
	./$(SAKAMURA_TARGET)

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
UEFI_CFLAGS = -O2 -Wall -Wextra -std=c99 -mno-sse -mno-mmx -mno-sse2 -DBTRON_TARGET=4 -DBTRON_UEFI_TARGET -DBTRON_SMP -Iinclude -Iinclude/drivers -Isrc/kernel -Isrc/cores

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
	@echo " Graphics : VESA VBE 1024x768 32-bpp Linear Framebuffer"
	@echo " Desktop  : desktop.c · wnd.c · gterm.c · Mozc IME"
	@echo "=========================================================="
	$(QEMU_X86_64) -M q35,accel=tcg -cpu qemu64 -smp cores=4,threads=1,sockets=1 -m 1G \
	    $(QEMU_DISPLAY) \
	    -vga std \
	    -kernel $(UEFI_TARGET) -serial stdio

run-eufi: run-uefi

test-uefi: $(UEFI_TARGET)
	@./$(UEFI_TARGET)
# ═══════════════════════════════════════════════════════════════════
# NEC PC-98 Kernel Desktop (Honoring Awe Morris — zedBSD Pioneer)
# ═══════════════════════════════════════════════════════════════════
PC98_CFLAGS = -O2 -Wall -Wextra -std=c99 -mno-sse -mno-mmx -mno-sse2 -DBTRON_TARGET=5 -DBTRON_PC98_TARGET -Iinclude -Iinclude/drivers -Isrc/kernel

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
	@./$(PC98_TARGET)

# ═══════════════════════════════════════════════════════════════════
# Motorola 68040 Macintosh Quadra 800 Kernel (q800)
# ═══════════════════════════════════════════════════════════════════
M68K_TARGET     = btron-m68k.elf
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
	$(QEMU_M68K) -M q800 -cpu m68040 -m 128M \
	    -kernel $(M68K_TARGET) -serial stdio -display none

# ═══════════════════════════════════════════════════════════════════
# Sony PlayStation 2 Emotion Engine Kernel (ps2 / PCSX2) [Target 8]
# ═══════════════════════════════════════════════════════════════════
AUTO_GUI       ?= 1
PS2_TARGET     = btron-ps2.elf
PS2_ISO        = btron-ps2.iso
PS2_LD_SCRIPT  = src/drivers/ps2/ps2.ld
PS2_CFLAGS     = -O2 -Wall -Wextra -std=c99 -ffreestanding -nostdlib \
                 -DBTRON_TARGET=8 -DBTRON_PS2_TARGET -DBTRON_AUTO_GUI=$(AUTO_GUI) \
                 -Iinclude -Iinclude/drivers -Isrc/kernel -Isrc/cores -Isrc/drivers/ps2
PS2_STARTUP    = src/cores/core_ps2.c
PS2_SRCS       = $(PS2_STARTUP)           \
                 src/cores/core_init.c    \
                 src/drivers/ps2/ps2_gs.c \
                 src/drivers/ps2/ps2_sio.c \
                 src/drivers/ps2/ps2_pad.c \
                 src/drivers/ps2/ps2_usb.c \
                 src/kernel/libstr.c      \
                 $(COMMON_NO_SDL_SRCS)
PS2_OBJS       = src/drivers/ps2/boot_ps2.ps2.o $(PS2_SRCS:.c=.ps2.o)

%.ps2.o: %.s
	$(PS2_CC) -c $< -o $@

%.ps2.o: %.c
	$(PS2_CC) $(PS2_CFLAGS) -c $< -o $@

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

run-ps2: $(PS2_TARGET)
	@echo "=========================================================="
	@echo " Launching B-System PS2 on PCSX2 Emulator"
	@echo " Machine  : Sony PlayStation 2 (Emotion Engine R5900)"
	@echo " CPU      : 128-bit SIMD MIPS Core @ 294.912 MHz"
	@echo " RAM      : 32 MB RDRAM | VRAM: 4 MB GS eDRAM"
	@echo " Display  : Graphics Synthesizer 800x600 @ 32-bpp RGBA (VESA)"
	@echo " GUI Mode : $(if $(filter 1,$(AUTO_GUI)),Automatic Direct GUI,Two-Stage Console -> startx)"
	@echo " Target   : $(if $(filter 1,$(ISO)),Disc ISO: $(PS2_ISO),Direct ELF: $(PS2_TARGET))"
	@echo " Emulator : $(PCSX2_BIN)"
	@echo "=========================================================="
	@if [ -x "$(PCSX2_BIN)" ]; then \
	    if [ "$(ISO)" = "1" ]; then \
	        "$(PCSX2_BIN)" -fastboot $(CURDIR)/$(PS2_ISO); \
	    else \
	        "$(PCSX2_BIN)" -fastboot $(CURDIR)/$(PS2_TARGET); \
	    fi; \
	elif [ -d "/Applications/PCSX2.app" ]; then \
	    if [ "$(ISO)" = "1" ]; then \
	        open -a /Applications/PCSX2.app --args -fastboot $(CURDIR)/$(PS2_ISO); \
	    else \
	        open -a /Applications/PCSX2.app --args -fastboot $(CURDIR)/$(PS2_TARGET); \
	    fi; \
	else \
	    echo "[ERROR] PCSX2 not found at $(PCSX2_BIN)"; \
	    exit 1; \
	fi

test-ps2: $(PS2_TARGET) $(PS2_ISO)
	@./scripts/test_ps2.sh

# ═══════════════════════════════════════════════════════════════════
# Bare-Metal MIPS Malta / Magnum Kernel (mips / QEMU) [Target 9]
# ═══════════════════════════════════════════════════════════════════
MIPS_TARGET     = btron-mips.elf
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
TEST_MOZC_SRCS = src/tip/test_mozc.c src/tip/mozc_kkc.c src/tip/tip_ife.c src/tip/wylie.c src/tip/tibetan_dict.c \
                 src/font/troncode.c src/font/jis_fonts.c src/font/tibetan_fonts.c src/tip/tip_vobj.c src/window/wnd.c \
                 src/graphics/dp_core.c
TEST_MOZC_OBJS = $(TEST_MOZC_SRCS:.c=.test.o)
TEST_MOZC_BIN  = test_mozc

%.test.o: %.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c $< -o $@

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
TEST_EDITOR_SRCS = src/apps/test_editor_ui.c src/apps/t_editor.c src/window/app_menu.c src/tip/mozc_kkc.c src/tip/tip_ife.c src/tip/wylie.c src/tip/tibetan_dict.c \
                   src/font/troncode.c src/font/jis_fonts.c src/font/tibetan_fonts.c src/tip/tip_vobj.c src/window/wnd.c \
                   src/graphics/dp_core.c
TEST_EDITOR_OBJS = $(TEST_EDITOR_SRCS:.c=.test.o)
TEST_EDITOR_BIN  = test_editor

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
TEST_HMI_SRCS = src/hmi/test_hmi.c src/hmi/hmi_core.c src/hmi/hmi_switch.c \
                src/hmi/hmi_selector.c src/hmi/hmi_volume.c src/hmi/hmi_meter.c \
                src/hmi/hmi_controller.c src/hmi/hmi_panel.c src/graphics/dp_core.c \
                src/font/troncode.c src/font/jis_fonts.c src/font/tibetan_fonts.c src/window/wnd.c
TEST_HMI_OBJS = $(TEST_HMI_SRCS:.c=.test.o)
TEST_HMI_BIN  = test_hmi

test-hmi: $(TEST_HMI_BIN)
	@echo "=========================================================="
	@echo " Running TRON HMI Standard Library Unit Tests..."
	@echo "=========================================================="
	@./$(TEST_HMI_BIN)

$(TEST_HMI_BIN): $(TEST_HMI_OBJS)
	$(CC) $(TEST_HMI_OBJS) -o $@ $(LDFLAGS) -lm

# ═══════════════════════════════════════════════════════════════════
# TAD Unified Packing Pipeline & Elixir Batch Compiler
# ═══════════════════════════════════════════════════════════════════
tad_bin:
	@elixir scripts/html2tad.exs
	@elixir scripts/book2tad.exs

html2tad:
	@elixir scripts/html2tad.exs --test
	@elixir scripts/book2tad.exs --test

book2tad:
	@elixir scripts/book2tad.exs

# ═══════════════════════════════════════════════════════════════════
# Native TAD Document Browser & Cabinet Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_TAD_SRCS = src/apps/test_tad_browser.c src/apps/tad_browser.c src/apps/vobj_manager.c src/window/app_menu.c \
                src/settings/appearance.c src/graphics/icons_bundle.c \
                src/tip/mozc_kkc.c src/font/troncode.c src/font/jis_fonts.c src/font/tibetan_fonts.c src/tip/tip_vobj.c \
                src/window/wnd.c src/graphics/dp_core.c
TEST_TAD_OBJS = $(TEST_TAD_SRCS:.c=.test.o)
TEST_TAD_BIN  = test_tad_browser

test-tad: $(TEST_TAD_BIN) tad_bin
	@if [ ! -f tad_bin/shared_data/data_type.tad ]; then elixir scripts/html2tad.exs >/dev/null 2>&1; fi
	@if [ ! -f tad_bin/b-book/kernel/index.tad ]; then elixir scripts/book2tad.exs >/dev/null 2>&1; fi
	@echo "=========================================================="
	@echo " Running B-System Native TAD Browser & Cabinet Tests..."
	@echo "=========================================================="
	@./$(TEST_TAD_BIN)

$(TEST_TAD_BIN): $(TEST_TAD_OBJS)
	$(CC) $(TEST_TAD_OBJS) -o $@ $(LDFLAGS) -lz

# ═══════════════════════════════════════════════════════════════════
# BeOS Chat & TRON IPC Pub/Sub Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_CHAT_SRCS = src/apps/test_chat.c src/apps/chat.c src/apps/chat_xml.c \
                 src/tip/mozc_kkc.c src/font/troncode.c src/font/jis_fonts.c src/font/tibetan_fonts.c \
                 src/window/wnd.c src/graphics/dp_core.c
TEST_CHAT_OBJS = $(TEST_CHAT_SRCS:.c=.test.o)
TEST_CHAT_BIN  = test_chat

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
TEST_TRACKER_SRCS = src/desktop/test_tracker.c src/desktop/tracker.c src/desktop/desktop.c src/settings/appearance.c \
                    src/vobject/vobj.c src/desktop/about.c src/window/wnd.c \
                    src/window/app_menu.c \
                    src/graphics/dp_core.c src/graphics/icons_bundle.c src/font/troncode.c src/font/jis_fonts.c src/font/tibetan_fonts.c
TEST_TRACKER_OBJS = $(TEST_TRACKER_SRCS:.c=.test.o)
TEST_TRACKER_BIN  = test_tracker

test-tracker: $(TEST_TRACKER_BIN)
	@echo "=========================================================="
	@echo " Running BTRON Deskbar Tracker Unit Tests..."
	@echo "=========================================================="
	@./$(TEST_TRACKER_BIN)

$(TEST_TRACKER_BIN): $(TEST_TRACKER_OBJS)
	$(CC) $(TEST_TRACKER_OBJS) -o $@ $(LDFLAGS) -lm

verify:
	@$(MAKE) -C verify run
# ═══════════════════════════════════════════════════════════════════
# Unified Test Suite Runner
# ═══════════════════════════════════════════════════════════════════

TEST_SETTINGS_BIN = test_settings
TEST_SETTINGS_SRCS = src/settings/test_language_settings.c \
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
TEST_GMENU_SRCS = src/desktop/test_global_menu.c src/desktop/global_menu.c src/desktop/tracker.c \
                  src/window/app_menu.c src/graphics/icons_bundle.c \
                  src/desktop/about.c src/window/wnd.c src/graphics/dp_core.c src/font/troncode.c \
                  src/font/jis_fonts.c src/font/tibetan_fonts.c src/tip/tip_ife.c src/tip/mozc_kkc.c \
                  src/tip/wylie.c src/tip/tibetan_dict.c src/tip/tip_vobj.c
TEST_GMENU_OBJS = $(TEST_GMENU_SRCS:.c=.test.o)
TEST_GMENU_BIN  = test_global_menu

test-global-menu: $(TEST_GMENU_BIN)
	@echo "=========================================================="
	@echo " Running BTRON Global System Menu & Japanese Deskbar Tests..."
	@echo "=========================================================="
	@./$(TEST_GMENU_BIN)

$(TEST_GMENU_BIN): $(TEST_GMENU_OBJS)
	$(CC) $(TEST_GMENU_OBJS) -o $@ $(LDFLAGS) -lm

# ═══════════════════════════════════════════════════════════════════
# Common Application Menu Subsystem Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_APP_MENU_SRCS = src/window/test_app_menu.c src/window/app_menu.c src/window/wnd.c \
                     src/graphics/dp_core.c src/font/troncode.c src/font/jis_fonts.c src/font/tibetan_fonts.c
TEST_APP_MENU_OBJS = $(TEST_APP_MENU_SRCS:.c=.test.o)
TEST_APP_MENU_BIN  = test_app_menu

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
TEST_MOUSE_SRCS = tests/test_mouse_drivers.c \
                  src/drivers/uefi/ps2_mouse.c \
                  src/drivers/pc98/input/pc98_mouse.c \
                  src/drivers/pc98/input/pc98_kbd.c
TEST_MOUSE_OBJS = $(TEST_MOUSE_SRCS:.c=.test.o)
TEST_MOUSE_BIN  = test_mouse

test-mouse: $(TEST_MOUSE_BIN)
	@echo "=========================================================="
	@echo " Running Mouse Drivers Unit Tests (UEFI PS/2 & PC-98)..."
	@echo "=========================================================="
	@./$(TEST_MOUSE_BIN)

$(TEST_MOUSE_BIN): $(TEST_MOUSE_OBJS)
	$(CC) $(TEST_MOUSE_OBJS) -o $@ $(LDFLAGS) -lm

test: test-kernel test-tad test-editor test-chat test-mozc test-wylie test-hmi test-ski test-tracker test-settings test-global-menu test-app-menu
	@echo "=========================================================="
	@echo " ALL B-SYSTEM TEST SUITES PASSED (100% SUCCESS)!"
	@echo "=========================================================="

# ═══════════════════════════════════════════════════════════════════
# Clean
# ═══════════════════════════════════════════════════════════════════
# ═══════════════════════════════════════════════════════════════════
# Ski Bootloader & Multi-Arch Boot Driver Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_SKI_SRCS = src/apps/test_ski.c  src/cores/core_smp.c \
                src/drivers/pc98/boot/boot_pc98.c src/drivers/bcm283x/boot/boot_arm_stub.c
TEST_SKI_OBJS = $(TEST_SKI_SRCS:.c=.test.o)
TEST_SKI_BIN  = test_ski

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
TEST_WYLIE_SRCS = src/tip/test_wylie.c src/tip/wylie.c
TEST_WYLIE_OBJS = $(TEST_WYLIE_SRCS:.c=.test.o)
TEST_WYLIE_BIN  = test_wylie

test-wylie: $(TEST_WYLIE_BIN)
	@echo "=========================================================="
	@echo " Running Kernel TIP Extended Wylie (EWTS) Tests..."
	@echo "=========================================================="
	@./$(TEST_WYLIE_BIN)

$(TEST_WYLIE_BIN): $(TEST_WYLIE_OBJS)
	$(CC) $(TEST_WYLIE_OBJS) -o $@ $(LDFLAGS) -lm

clean:
	@$(MAKE) -C verify clean >/dev/null 2>&1 || true
	rm -f *.toc
	rm -f *.aux
	rm -f *.log
	rm -f *.out
	rm -rf tad_bin
	rm -f $(POSIX_TARGET) $(QEMU_TARGET) $(TKERNEL_TARGET) $(SAKAMURA_TARGET) \
	      $(ARM32_TARGET) $(ARM64_TARGET) $(DEFAULT_TARGET) $(UEFI_TARGET) btron-uefi.elf $(PC98_TARGET) btron-pc98.elf $(M68K_TARGET) $(PS2_TARGET) $(PS2_ISO) $(MIPS_TARGET) $(TEST_MOZC_BIN) $(TEST_EDITOR_BIN) $(TEST_HMI_BIN) $(TEST_TAD_BIN) $(TEST_CHAT_BIN) $(TEST_SKI_BIN) $(TEST_GMENU_BIN)
	find src tests -type f \( -name "*.posix.o" -o -name "*.qemu.o" \
	    -o -name "*.tkernel.o" -o -name "*.sakamura.o" -o -name "*.uefi.o" -o -name "*.pc98.o" -o -name "*.m68k.o" -o -name "*.ps2.o" -o -name "*.mips.o" -o -name "*.arm32.o" \
	    -o -name "*.arm64.o" -o -name "*.test.o" -o -name "*.o" \) -delete 2>/dev/null || true


# ===================================================================
# Automated Headless Window Screenshot Capture Pipeline
# ===================================================================
CAPTURE_SCREENS_BIN = capture_screens
CAPTURE_SCREENS_SRCS = src/tools/capture_screens.c \
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
                       src/graphics/icons_bundle.c

CAPTURE_SCREENS_OBJS = $(CAPTURE_SCREENS_SRCS:.c=.test.o)

# ═══════════════════════════════════════════════════════════════════
# µBTRON-FOMA Mobile UI Toolkit Test Suite
# ═══════════════════════════════════════════════════════════════════
TEST_FOMA_SRCS = src/desktop/test_foma_ui.c src/desktop/desktop_mobile.c \
                 src/graphics/dp_core.c src/font/troncode.c src/font/jis_fonts.c src/font/tibetan_fonts.c
TEST_FOMA_OBJS = $(TEST_FOMA_SRCS:.c=.test.o)
TEST_FOMA_BIN  = test_foma_ui

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

# ═══════════════════════════════════════════════════════════════════
# µBTRON-FOMA Automated Screen Capture & Documentation
# ═══════════════════════════════════════════════════════════════════
CAPTURE_FOMA_BIN  = capture_foma
CAPTURE_FOMA_SRCS = src/tools/capture_foma.c \
                    src/desktop/desktop_mobile.c \
                    src/desktop/workbench_mobile.c \
                    src/window/wnd.c \
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
                    src/font/tibetan_fonts.c

CAPTURE_FOMA_OBJS = $(CAPTURE_FOMA_SRCS:.c=.test.o)

$(CAPTURE_FOMA_BIN): $(CAPTURE_FOMA_OBJS)
	$(CC) $(CAPTURE_FOMA_OBJS) -o $@ $(LDFLAGS) -lm

foma-screens: $(CAPTURE_FOMA_BIN)
	@echo "=========================================================="
	@echo " Generating Isolated µBTRON-FOMA Mobile Screenshots..."
	@echo "=========================================================="
	@./$(CAPTURE_FOMA_BIN)
	@python3 scripts/update_foma_screens.py

