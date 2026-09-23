# PS2 roadmap — GS hardware BitBlt + USB HID keyboard/mouse in PCSX2

Goal: `make run-ps2` boots BTRON in PCSX2 with (1) hardware-accelerated BitBlt on the
Graphics Synthesizer instead of the current whole-frame CPU upload, and (2) a real
keyboard and mouse. Reuse the Pi 400 port's designs wherever they transfer.

Status of this file: plan only. No PS2 code changed yet.

---

## 0. Three premise corrections (they change the plan, so they go first)

**0.1 There is no PS/2 keyboard port on the PS2.** The front jacks that look like PS/2
are the SIO2 multi-purpose EXP ports (controller / memory card / rumble), not an 8042
PS/2 host. Keyboard and mouse on PS2 are **USB 1.1 through the IOP-bus OHCI
controller** — which is exactly what PS2/T2 Linux ships (`drivers/usb/host/ohci-ps2.c`,
plus HID on top, and PS2SDK's `iop/usb/keyboard` + `iop/usb/mouse` for the IOP-side
route). So "PS/2 input" in this plan means **OHCI + USB HID boot protocol**, and
`src/drivers/ps2/ps2_usb.c` is already aimed at the right chip.
The in-tree `src/drivers/ps2/ps2_sio.c` is the SIO0 **UART** (serial console), not an
input device — do not confuse the two.

**0.2 `ps2fb.c` was never mainlined.** The GS fbdev driver exists in Fredrik Noring's
2019 RFC series *Linux for the PlayStation 2* (`frno7/linux`, branch `ps2-main`), under
GPL-2.0; the T2 SDE tree dropped PS2 support, and PS2SDK's USB stack is AFL-2.0. This
repo's headers claim *"Cleanroom … Zero proprietary Sony SDK dependencies"* under MIT, so
those sources are **specification only**: read the register semantics there, write our own
implementation. No vendoring, no code import.

**0.3 PCSX2 can do the input, but cannot certify the graphics.** PCSX2 emulates the
IOP-side OHCI (`pcsx2/USB/qemu-usb/usb-ohci.cpp`) and registers generic HID devices —
`hidkbd` ("HID Keyboard") and `hidmouse` — attachable per port in Settings → USB. So the
USB route is testable in the emulator. What PCSX2 does **not** give us: cycle-accurate
GIF/PATH3 arbitration or real GS bandwidth, so every throughput number stays
UNVERIFIED until hardware. Conversely PS2 *Linux* is broken in PCSX2, which is
irrelevant to us since we are not running Linux.

---

## 1. Verified starting point (clean rebuild, this session)

* `find src -name '*.ps2.o' -delete && make ps2` builds `btron-ps2.elf` and packs
  bootable `btron-ps2.iso` (SYSTEM.CNF
  `BOOT2 = cdrom0:\BTRON.ELF;1`, `VMODE = NTSC`). One warning, pre-existing and unrelated
  (`src/apps/clu.h:25` duplicated `ShellOutputFn` typedef). `/Applications/PCSX2.app`
  exists, so `make run-ps2` works today.
* **The hardware blit plumbing already exists.** `ps2_gs.c:77 ps2_gs_flush()` programs
  `BITBLTBUF`/`TRXPOS`/`TRXREG`/`TRXDIR` and streams the frame as GIF `IMAGE` packets
  through EE **DMAC channel 2** (`ps2_gs.c:55 ps2_dma_gif_send`), i.e. a Host→Local
  MOVE_IMAGE. It is hard-coded to 800×600 from (0,0) and is therefore not rect-capable
  and not a VRAM-local copy.
* Mode is **800×600 non-interlaced, PSMCT32, DBW=13 (832-px pitch)** —
  `ps2_gs.h:36`, `ps2_gs.c:125`. `doc/md/PS2.md` and `PORTING.md` still describe
  640×448/640×480: stale.
* Presentation path is **two** full-screen passes per frame:
  `core_ps2.c:107 blit_backbuffer_to_ps2fb()` converts the 1.92 MB ARGB canvas to a
  second 1.92 MB RGBA RDRAM buffer, then `ps2_gs_flush()` uploads all of it. No damage
  list, no vsync gating, and `ps2_dma_gif_send` spins on `D2_CHCR.STR` per chunk, so CPU
  and GIF never overlap.
* Input is not real: `ps2_usb.c` reads `HcRevision` and issues a port reset but builds no
  HCCA/ED/TD and submits no transfers; `ps2_pad.c` only accepts injected state.
  `ps2_gs_get_framebuffer`, `snd_evt`/`get_evt` (`include/btron/event.h`) and the HID
  report decoders `ps2_usb_process_keyboard_report`/`_mouse_report` are the seams to
  build on.
* Console geometry at 8×16 `troncode` glyphs: **100 columns × 37 rows**. The Pi 400
  one-screen boot-log budget becomes 37 rows here; the 24-row log we just finished fits
  with room to spare, so port it as-is rather than re-tuning it.

**Open question that gates Phase 1:** the EE reaching OHCI at `0xBF801600`
(`ps2_usb.h:15`) is architecturally what PS2 Linux does, but our probe never proved PCSX2
decodes an **EE-originated** access to it — PCSX2 may only wire OHCI into the IOP bus
handlers. Measure before writing an ED/TD engine.

---

## 2. Phase 0 — baseline numbers and two probes (no features)

Do this before any driver work; it is one build and one screenshot each.

* **0.1 Flush cost, measured.** Stamp `ps2_gs_flush()` with the EE's `Count` cop0
  register (or TM0) and print `present=<us>us` on a boot row. Everything in Phase 2 is
  judged against this number. Pi 400 rule: instrument first, never ship a guess-fix.
* **0.2 OHCI visibility probe.** Read `HcRevision` (offset 0x00), `HcControl` (0x04),
  `RhHubStatus` (0x50), `RhPortStatus[1..2]` (0x54/0x58) from the EE and print them as
  one row, then attach HID Mouse to USB Port 1 in PCSX2 and print the port registers
  again.
  * Sane revision (0x10) + a port status that changes on attach → **Phase 1A** (EE-side
    OHCI driver).
  * All-ones or zeros → EE cannot reach OHCI in PCSX2 → **Phase 1B**.
* **0.3 Cache-coherency probe.** The R5900 has 32 KB I / 24 KB D caches and no MMU.
  `ps2_dma_gif_send` consumes physical RDRAM, so any buffer the CPU has written through
  the D-cache must be writeback-invalidated (or reached through KSEG1) before the GIF
  reads it. `UNCACHED()` is already used for the packets; confirm the *canvas* is on the
  uncached side too, and print whether it is. This is the same failure mode the Pi 400
  MMU work settled as "bus-master buffers must be non-cacheable".
* **0.4 Docs.** Correct `doc/md/PS2.md` + `PORTING.md:41,235-238` to 800×600, and fix
  the two comments that cite a nonexistent `third_party/ps2sdk`.

**Gate G-P0:** a PCSX2 screenshot showing `present=…us`, the OHCI probe row, the cache
row. Nothing in Phase 1/2 gets written before these are read.

---

## 3. Phase 1 — keyboard and mouse

### 1A. EE-side OHCI host + HID boot protocol (preferred, if the probe passes)

Work is confined to `src/drivers/ps2/ps2_usb.c` (+ its header) and one call site in
`core_ps2.c`.

1. Controller bring-up: `HcControl.HCFS=Operational`, `Control` CLE/BLE/IRE enables,
   `Hcca` pointer, `PeriodCurrentED`, `ControlHeadED`, `BulkHeadED`, `HcFmInterval`.
2. Coherent pool: HCCA (256 B, 256-byte aligned), one control ED, one bulk ED, and one
   ED per interrupt IN endpoint with a small TD ring. All in uncached RDRAM.
3. Root hub: power-up via `RhHubStatus.PUS`, wait `port-power-good`, read
   `RhPortStatus`, drive `PRS` reset ≥10 ms, then read **LowSpeed** on the connect
   status — USB HID devices on PS2 are low/full-speed only, there is no EHCI.
4. Enumeration: GET_DESCRIPTOR(device, 8) → real `bMaxPacketSize0` → SET_ADDRESS →
   re-evaluate the control pipe MPS → GET_DESCRIPTOR(device, 18) → SET_CONFIGURATION.
5. HID specifics: GET_DESCRIPTOR(report) (we only need to *validate* it, the boot
   protocol decoder is fixed-layout), SET_IDLE, SET_PROTOCOL(Boot) if the device reports
   a HID class with a non-boot default.
6. Interrupt IN: a chain of done-TD-owned buffers, resubmitted as consumed — the exact
   pattern of `xhci_queue_ep1_transfer` on the Pi 400 (`XHCI_KBD_TRB_DEPTH`/
   `XHCI_MOUSE_TRB_DEPTH`).
7. Reports into events: already written. `ps2_usb_process_keyboard_report`/
   `_mouse_report` + `ps2_usb_hid_to_btron_key` give us keys, relative deltas, buttons
   and wheel; keep the Pi 400 invariants that cost debugging there — button **edges**
   must survive (not level), and mouse object lifetimes must not outlive a disconnect.
8. **Cooperative polling first, interrupts later.** No EE-side interrupt story is proven
   on this target. Poll the frame-number / TD-done bits from the main loop, exactly like
   the Pi 400's cooperative xHCI drain that we kept after the 1 kHz IRQ plane failed.

Reuse map (Pi 400 → PS2): report decoders and key table → move as-is; ring-resubmission
→ same shape, TD instead of TRB; boot-protocol semantics → identical; `HUB_PORT_STAT_*`
handling → n/a (OHCI root hub registers replace the hub class requests).

**Gate G-P1:** PCSX2 with Settings → USB Port 1 = HID Keyboard, Port 2 = HID Mouse;
type in the Stage-1 shell, then move and drag a window in the GUI. Also note in the
commit whether PCSX2 grabs the mouse (it does; a pause hotkey is needed to get it back).

### 1B. Fallback if the probe fails

Map host keyboard/mouse onto the emulated DualShock via the pad plugin and synthesise a
virtual keyboard from pad state — `ps2_pad_set_state` is already the injection point.
This reaches the same `EVT` queue with near-zero risk but is not real HID, and it must be
labelled as an emulator-only path so nobody mistakes it for driver support.

---

## 4. Phase 2 — GS hardware BitBlt

### 2.1 Rect-limited upload (kill the whole-frame flush)

Replace the fixed packet with `ps2_gs_blit_rect(x, y, w, h, src_pitch)`:
`TRXPOS = {DSAX=x, DSAY=y}`, `TRXREG = {RRW=w, RRH=h}`, `DBA` unchanged.

Three hard constraints, all of which are correctness traps rather than optimizations:

* **Even width for CT32.** `TRXREG.RRW` must be a multiple of 2 in PSMCT32/PSMZ32 (4 for
  CT16/CT16S, 8 for CT24). An odd-width damage rect must be widened and masked, or split.
* **No row stride on the Host→Local image stream.** MOVE_IMAGE consumes consecutive
  quadwords, so a rect narrower than the canvas needs **one `IMAGE` packet per row**
  (height tags, not one tag). Keep the RDRAM canvas pitch equal to the VRAM pitch
  (832 px) so full-width rects stay contiguous and cheap.
* **16-byte row alignment.** Each row must start on a quadword, so `x` must be a
  multiple of 4 px. Quantise the damage list to 16-byte bands instead of chasing exact
  rectangles — the same lesson as the Pi 400's banded/sliced presenter.

### 2.2 VRAM→VRAM copy: the actual hardware BitBlt

`TRXDIR = Local→Local` copies inside GS VRAM with **no GIF pixel traffic and no EE
work** — this is the primitive the request is really asking for, and it is what window
moves, menu-close restore and scrolling should use. Overlap-safe by choosing
`TRXPOS` direction (upper-left→lower-right vs the reverse), as `ps2fb_cb_copyarea` does.

Design consequence to hold onto: VRAM is not cheaply readable back
(`local→host` STORE_IMAGE exists — PCSX2 implements it as `GSReadLocalMemoryUnsync` —
but its hardware throughput is UNVERIFIED), so the **RDRAM canvas stays the source of
truth** and VRAM holds only the last presented result. Local→Local is therefore valid
for moving *already-presented* content; anything the canvas does not agree with must be
re-uploaded from RDRAM. That is exactly the shape of the Pi 400 compositor: canvas
composits, presenter uploads damage.

### 2.3 Fills

`ps2_gs_fill_rect`/`draw_rect`/cursor are CPU today. Use a solid-colour PBM/tile fill
(one row + repeated `IMAGE` rows, or the GS `FRAME`/`DATM` masked-sprite path) so the
background clears stop costing 1.92 MB of stores.

### 2.4 Delete the ARGB→RGBA pass

`blit_backbuffer_to_ps2fb()` costs a 1.92 MB read + 1.92 MB write per frame for a byte
permutation. Preferred fix: have PS2 builds store the canvas in GS-native RGBA so the
swap disappears entirely — contingent on `COLOR`/`RGB()` construction being centralised
per target, which is Phase 2's first check. If it is not centralised, do the swap in
128-bit ops rather than a scalar loop (see the toolchain caveat in §6).

### 2.5 Presentation policy: single VRAM page

Two 800×600 CT32 pages need 2 × 13 × 64 × 600 × 4 = **3.99 MB** because `DBW` is
quantised to 64-px units, and GS VRAM is 4 MB — leaving nothing for palettes or a second
page's worth of tiles. So **page flipping is effectively off the table at this mode and
bpp**, and damage-rect upload onto one page is the presentation model. (16 bpp would
double-buffer but breaks the ARGB pipeline; 640×480 would fit — a mode decision, not a
default.) Related: `GS_REG()` writes are privileged-register accesses at `0x12000000`
and work, so DISPFB1/2 can be re-pointed per frame if a mode change ever makes double
buffering affordable.

Also fix in this phase: issue chunks without spinning on `D2_CHCR.STR` every 8000 QWs,
and gate the final commit on vsync (`ps2_gs_wait_vsync` exists) to stop tearing.

**Gate G-P2:** `present=…us` drops by roughly the rect-area fraction, a 1-px-wide rect at
x=799 and an odd-width rect render correctly, and dragging a window shows no trail
(the G0 gate we already defined on Pi 400, reused verbatim).

---

## 5. Phase 3 — compositor and console parity with the Pi 400 port

* Drive `ps2_gs_blit_rect` from a damage list instead of calling `ps2_gs_flush()`
  unconditionally (`core_ps2.c:723,754`), and adopt the Pi 400's coalescing + bounded
  presenter semantics rather than reinventing them.
* Carry the `move-is-geometry-damage` invariant from `doc/md/STABILIZATION.md`: a window
  move must register damage for both the vacated and entered areas; local→local copy in
  2.2 is an implementation of that, not a substitute for it.
* Boot console: reuse the harmonised one-row-per-subsystem style — including the
  **unpadded tags** (`[GS]`, `[USB]`, `[BOOT]`, `[CORE]`) and the open-row-is-the-hang-
  marker trick — against a **37-row** budget instead of 48.
* Reuse the HUD's per-second counter convention (a zero means "idle during that second",
  never "path did not run") and keep the same field letters so a PS2 screenshot is
  comparable with a Pi 400 one.

---

## 6. Risks that will bite

* **Toolchain.** `PS2_CC` is clang `--target=mipsel-unknown-elf -march=mips3 -mabi=32`
  (Makefile:71). Clang has **no R5900 support**: `lq`/`sq`, `mtc3`/`cfc3` and the
  MMAX/SIMD pixel ops will not assemble for a 32-bit MIPS III target. Every 128-bit path
  (2.4, and any quadword copy) needs `.word` encodings or a hand-written `.s` — the
  `boot_ps2.s` precedent already covers syscall thunks, so extend that file rather than
  fighting the compiler.
* **No cache-coherency discipline = invisible wrong pixels.** See 0.3. A DMA source that
  is D-cache-dirty shows the previous frame; the failure looks like a blit bug.
* **PCSX2 is not the hardware.** GIF throughput, local→local speed, STORE_IMAGE cost and
  PATH3 arbitration are emulator-suspect. Any "PS2 is now faster than Pi 400" claim is
  UNVERIFIED until measured on a retail console.
* **PCSX2's HID keyboard device** is registered as a specific emulation ("HID Keyboard
  (Konami)"). Confirm it presents a standard boot-protocol 8-byte report before assuming
  the decoder works; if it does not, that is a Phase 1A blocker to know about early.
* **`BTRON_AUTO_GUI` is defined only for PS2** (`Makefile:738-740`, noted in
  STABILIZATION.md:202), so the PS2 boot path reaches the GUI without the shell being
  exercised. Flip it off for Phase 1 so the shell — where input is easiest to verify —
  is what boots.

---

## 7. Order of work

Phase 0 → 2.1 + 2.4 (biggest visible win, zero new hardware risk, all register semantics
already proven by the existing flush) → 1A gated by 0.2 → 2.2 + 2.5 → 3 → hardware pass.

Rationale: the flush path already proves GIF+MOVE_IMAGE on this codebase, so 2.1 is a
parameterisation of working code, while Phase 1 is a new controller driver whose
feasibility depends on a probe. Do not start the OHCI engine before 0.2 answers.
