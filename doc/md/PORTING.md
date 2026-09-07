# B-System (BTRON 3.20) Kernel Porting Guide & BSP Specification

## 1. Overview & Architectural Principles: The 3-Tier Layering

The B-System (BTRON 3.20 Specification) Workstation Operating System is designed for portable execution across diverse CPU architectures and hardware targets—ranging from classic 32-bit CISC workstations (Motorola 68040 Macintosh Quadra 800) to modern 32-bit and 64-bit RISC microcomputers (ARMv7 Cortex-A7, AArch64 Cortex-A72).

Every target follows a standardized **`Image > Core > Kernel`** 3-tier cleanroom architecture:

```
┌────────────────────────────────────────────────────────────────────────┐
│                        Image (Final Run Target)                        │
│   btron-posix, btron-arm-baremetal.elf, btron-aarch64-baremetal.elf    │
├────────────────────────────────────────────────────────────────────────┤
│                       Cores (src/cores/) Layer                         │
│  Board / Machine Integration: Memory Maps, GPU Framebuffer, Bus I/O    │
│  core_yoko.c, core_arm64.c, core_m68k.c, core_posix.c, core_boot.c    │
├────────────────────────────────────────────────────────────────────────┤
│                       Kernel (src/kernel/) Layer                       │
│  Pure Portable RTOS Executive: uITRON 3.0 / Sakamura T-Kernel 2.0      │
│  Tasks, Semaphores, Mutexes, Mailboxes, Eventflags, Timers, Libstr     │
├────────────────────────────────────────────────────────────────────────┤
│                       Drivers (src/drivers/) Layer                     │
│  Silicon & Bus Drivers: bcm283x (DWC2, PL011, VC Mailbox), m68k (ADB) │
└────────────────────────────────────────────────────────────────────────┘
```

By strictly separating platform-specific cores from kernel primitives and desktop windowing logic, **all window management, dragging, sliding tabs, desktop icons, drop-down menus, and Japanese Mozc/TIP IME operate identically on every port with zero duplicated code.**

## 2. Architecture & Target Matrix

| Target ID | Architecture | Typical Hardware / VM | Core Entry File | Video Mode | Input Subsystem |
|:---|:---|:---|:---|:---|:---|
| **Target 0** | POSIX / Hosted | Linux / macOS Host | `src/cores/core_posix.c` | Host SDL2 Window | SDL2 Events |
| **Target 1** | ARM32 VirtIO | QEMU ARM VirtIO | `src/cores/core_virtio.c` | VirtIO GPU | VirtIO Input |
| **Target 2** | ARMv7 AArch32 | Raspberry Pi 2B (BCM2836) | `src/cores/core_yoko.c` | Mailbox FB 1024x768 32bpp | DWC2 USB HID + PL011 UART |
| **Target 3** | Sakamura VirtIO | QEMU VirtIO T-Kernel | `src/cores/core_tkernel.c` | VirtIO GPU | VirtIO Input |
| **Target 4** | x86_64 UEFI | PC / QEMU x86_64 SMP | `src/cores/core_boot.c` | VESA / GOP FB | PS/2 + Serial |
| **Target 5** | i386 PC-98 | NEC PC-9801 / PC-9821 | `src/cores/core_pc98.c` | 640x400 Plane FB | PC-98 Bus Mouse + Kbd |
| **Target 6** | AArch64 | Raspberry Pi 3B (BCM2837) & 4B (BCM2711) | `src/cores/core_arm64.c` | Mailbox FB 1024x768 32bpp | DWC2 USB HID + PL011 UART |
| **Target 7** | M68K CISC | Apple Mac Quadra 800 | `src/cores/core_m68k.c` | NuBus DAFB 800x600 8bpp | ADB (VIA1) + Z8530 ESCC |
| **Target 8** | MIPS R5900 | Sony PlayStation 2 (PCSX2) | `src/cores/core_ps2.c` | GS Framebuffer 640x480 32bpp | EE SIO / BIOS Syscall TTY |
| **Target 9** | MIPS-III/64 | QEMU Malta & Magnum | `src/cores/core_mips.c` | 16550 Serial Console | NS16550 UART (COM1 115200) |

## 3. Kernel Memory Layout & Heap Contract

Every bare-metal port must establish linear memory regions for:

1. **Kernel Image & Stack**: Low memory (e.g. `0x00000000` to `0x00200000` or `0x80000` entry point).

2. **Kernel Dynamic Heap**: Minimum 14 MB (e.g. `HEAP_BASE = 0x02000000`, `HEAP_LIMIT = 0x38000000`). Used by `tkl_malloc`, font tables, window backing stores, and icon bundles.

3. **Screen Backbuffer**: Statically allocated contiguous memory:
   ```c
   static COLOR s_desktop_backbuffer[BTRON_SCREEN_W * BTRON_SCREEN_H] __attribute__((aligned(64)));
   ```

4. **Hardware Framebuffer (VRAM)**:
   - TrueColor 32-bpp (`0xAARRGGBB` format) on modern display controllers (VideoCore, VESA, VirtIO).
   - Indexed 8-bpp palette with software color quantization (`color_to_pal()`) on legacy hardware (Mac Quadra 800 NuBus DAFB).

## 4. Display Subsystem & Compositing Contract

### 4.1 Initialization Sequence

1. Allocate or obtain hardware framebuffer pointer `gpu_fb`.

2. Initialize desktop backing structures via:
   ```c
   GDEV *screen = init_baremetal_desktop((uint32_t*)s_desktop_backbuffer, BTRON_SCREEN_W, BTRON_SCREEN_H);
   workbench_init(BTRON_SCREEN_W);
   ```

3. Perform initial desktop render pass and blit to physical VRAM:
   ```c
   workbench_render(screen, BTRON_SCREEN_W, BTRON_SCREEN_H);
   blit_backbuffer_to_fb(gpu_fb);
   ```

### 4.2 Double-Buffered Blitter

All drawing primitives (windows, fonts, icons, menus, and cursor) render into `s_desktop_backbuffer`. When `need_redraw` is asserted, the kernel synchronizes the backbuffer to physical VRAM:

```c
void blit_backbuffer_to_fb(volatile uint32_t *gpu_fb) {
    if (!gpu_fb) return;
    tkl_memcpy((void*)gpu_fb, s_desktop_backbuffer, BTRON_SCREEN_W * BTRON_SCREEN_H * sizeof(COLOR));
    /* Issue memory barrier for GPU coherency */
#if defined(__aarch64__)
    __asm__ volatile("dsb sy" : : : "memory");
#elif defined(__arm__)
    __asm__ volatile("dsb" : : : "memory");
#endif
}
```

## 5. Event Distribution Pipeline

Input events from hardware controllers MUST NOT interact with windows or desktop state directly. Instead, they follow the unified cleanroom pipeline:

```
[Hardware Interrupt / Poll] (USB HID / ADB / PS2 / UART)
                     │
                     ▼
             Translate to EVT
     (EV_MOUSE_MOVE, EV_BUT_DOWN, EV_BUT_UP, EV_KEY_DOWN, EV_KEY_UP)
                     │
                     ▼
                 snd_evt(&ev)
                     │
                     ▼
          [System Event Queue (event.c)]
                     │
                     ▼
                 get_evt(&ev, 0)
                     │
                     ▼
        workbench_process_event(screen, &ev)
```

### 5.1 Mouse Reporting Contract

- Update internal coordinates with hardware deltas:
  ```c
  s_mouse_x += (H)mouse_dx;
  s_mouse_y += (H)mouse_dy;
  if (s_mouse_x < 0) s_mouse_x = 0;
  if (s_mouse_x >= BTRON_SCREEN_W) s_mouse_x = BTRON_SCREEN_W - 1;
  if (s_mouse_y < 0) s_mouse_y = 0;
  if (s_mouse_y >= BTRON_SCREEN_H) s_mouse_y = BTRON_SCREEN_H - 1;
  ```

- Send `EV_MOUSE_MOVE` on coordinate changes.

- Detect button transitions (Left Button = bit 0):
  ```c
  uint8_t btn_now = mouse_rep.buttons & 1u;
  if (btn_now != btn_prev) {
      EVT ev;
      ev.type   = btn_now ? EV_BUT_DOWN : EV_BUT_UP;
      ev.button = 1;
      ev.pos.x  = s_mouse_x;
      ev.pos.y  = s_mouse_y;
      snd_evt(&ev);
  }
  ```

### 5.2 Keyboard Reporting Contract

- Translate hardware scancodes to standard BTRON keys (`BTRON_KEY_*` or ASCII).

- Map hardware modifiers to standardized BTRON modifier bitmasks (`include/btron/event.h`):
  ```c
  static inline uint16_t usb_to_btron_modifiers(uint8_t usb_mod) {
      uint16_t bmod = BTRON_KMOD_NONE;
      if (usb_mod & 0x01) bmod |= BTRON_KMOD_LCTRL;
      if (usb_mod & 0x02) bmod |= BTRON_KMOD_LSHIFT;
      if (usb_mod & 0x04) bmod |= BTRON_KMOD_LALT;
      if (usb_mod & 0x10) bmod |= BTRON_KMOD_RCTRL;
      if (usb_mod & 0x20) bmod |= BTRON_KMOD_RSHIFT;
      if (usb_mod & 0x40) bmod |= BTRON_KMOD_RALT;
      return bmod;
  }
  ```

- Pass the translated modifier mask in `ev.data`:
  ```c
  ev.type = EV_KEY_DOWN;
  ev.key  = btron_key_code;
  ev.data = (VW)(uintptr_t)bmod;
  ev.pos.x = s_mouse_x;
  ev.pos.y = s_mouse_y;
  snd_evt(&ev);
  ```

### 5.3 Serial Console Fallback Parity

All bare-metal ports must support interactive terminal controls over their primary serial port (PL011, Z8530, or 16550 UART):

- **ANSI Arrow Keys** (`\033[A`, `\033[B`, `\033[C`, `\033[D`) move `s_mouse_x` and `s_mouse_y` smoothly by 16 pixels.

- **Regular Characters** (ASCII 32..126, Enter `\r` -> `\n`, Backspace `\b`, Tab `\t`) are queued as `EV_KEY_DOWN` to the focused window.

## 6. System Timer & Rate Throttling Contract

### 6.1 Accurate 60Hz System Tick

Do not increment system ticks inside an unthrottled loop. Drive `s_system_ticks` from hardware clock counters (e.g. BCM System Timer `0x3F003004` or ARM Generic Timer `cntvct_el0`):

```c
uint32_t now_us = rpi_get_system_timer_usec();
s_system_ticks = now_us / 16666; /* 60Hz tick */
```

The desktop clock updates once per second when `s_system_ticks - last_clock_tick >= 60`.

### 6.2 USB HID Periodic Polling (100 Hz)

Interrupt IN endpoints in USB (Keyboard & Mouse) require periodic polling according to `bInterval` (typically 10 ms = 100 Hz).

- **CRITICAL ANTI-PATTERN**: Do NOT re-queue channels immediately upon receiving `NAK` in a tight loop. Re-queuing on NAK generates millions of MMIO transactions per second, saturating QEMU and host vCPUs.

- **CORRECT PATTERN**: Clear `HCINT`, mark channel inactive, and poll USB devices every ~10 ms:
  ```c
  if (now_us - last_usb_poll >= 10000) {
      last_usb_poll = now_us;
      if (usb_poll_devices(screen)) {
          need_redraw = 1;
      }
  }
  ```

## 7. Canonical Reference Implementations

When developing or reviewing a port, refer to these canonical implementations:

1. **[src/cores/core_m68k.c](src/cores/core_m68k.c)**:
   - Motorola 68040 CISC architecture
   - Apple Macintosh Quadra 800 hardware
   - NuBus DAFB 800x600 8-bpp palette with `color_to_pal()` quantization
   - MOS 6522 VIA1 60Hz hardware timer & Apple Desktop Bus (ADB) keyboard/mouse
   - Zilog Z8530 ESCC Serial UART console

2. **[src/cores/core_yoko.c](src/cores/core_yoko.c)**:
   - ARMv7-A 32-bit RISC architecture
   - Raspberry Pi 2B (BCM2836) hardware
   - VideoCore Mailbox GPU 1024x768 32-bpp truecolor framebuffer
   - Synopsys DWC2 USB 2.0 host controller (Hub + USB keyboard + USB mouse)
   - ARM PrimeCell PL011 UART serial console

3. **[src/cores/core_arm64.c](src/cores/core_arm64.c)**:
   - AArch64 64-bit RISC architecture
   - Raspberry Pi 4B (BCM2711) hardware
   - 64-bit clean pointers and barrier operations (`dsb sy`)

4. **[src/cores/core_ps2.c](src/cores/core_ps2.c)**:
   - Sony PlayStation 2 Emotion Engine (R5900 MIPS-III little-endian)
   - Cleanroom Graphics Synthesizer (GS) 640x480 32-bpp RGBA video driver
   - SIO / EE BIOS syscall console output for PCSX2 emulator (`make run-ps2`)

5. **[src/cores/core_mips.c](src/cores/core_mips.c)**:
   - Bare-metal MIPS architecture on QEMU Malta & Magnum
   - NS16550 UART serial driver (`0x180003F8` ISA COM1)
   - Interactive shell monitor and RTOS heartbeat (`make run-mips`)

## 8. Step-by-Step Checklist for Porting to a New Architecture

- [ ] **Step 1: Low-Level Bootstrap & Memory Map**
  - Define CPU stack, vector table, and MMIO base addresses.
  - Setup BSS zeroing and configure kernel heap (`HEAP_BASE` and `HEAP_LIMIT`).

- [ ] **Step 2: Video Display & Double Buffering**
  - Initialize physical framebuffer resolution ($W \times H$, 32-bpp or 8-bpp).
  - Allocate `s_desktop_backbuffer[W * H]`.
  - Implement `blit_backbuffer_to_fb()`.

- [ ] **Step 3: Workbench Shell Initialization**
  - Call `init_baremetal_desktop((uint32_t*)s_desktop_backbuffer, W, H)`.
  - Call `workbench_init(W)`.
  - Call `workbench_render(screen, W, H)` and perform initial blit.

- [ ] **Step 4: Hardware Timer & Tick**
  - Read high-resolution monotonic timer (e.g. 1 MHz counter).
  - Compute 60Hz tick count: `s_system_ticks = now_us / 16666`.

- [ ] **Step 5: Hardware Input Subsystem**
  - Poll hardware keyboard and mouse at 100 Hz (every 10 ms).
  - Convert deltas to clamped coordinates `s_mouse_x`, `s_mouse_y`.
  - Translate scancodes and modifier bitmasks (`BTRON_KMOD_*`).
  - Queue `EVT` structures via `snd_evt()`.

- [ ] **Step 6: Serial Console Fallback**
  - Check UART for incoming characters.
  - Parse ANSI arrow sequences to move cursor.
  - Queue character events to active window via `snd_evt()`.

- [ ] **Step 7: Main Event Loop**
  - Dispatch events: `while (get_evt(&ev, 0) == E_OK) workbench_process_event(screen, &ev);`.
  - If `need_redraw`, call `workbench_render(screen, W, H)` and `blit_backbuffer_to_fb()`.
  - Add small CPU idle delay / `wfe` instruction.

# Credits

* Namdak Tonpa and Grok 4.5
