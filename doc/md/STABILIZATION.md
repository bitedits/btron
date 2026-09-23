# Stabilization and dead-code review plan

Scope: the Pi 400 / BCM2711 / AArch64 target (`ARM64_BAREMETAL_SRCS`,
Makefile:348 + :241-244). Everything below was traced against the working
tree, not against a document. Each phase ends at a hardware gate: nothing
moves to the next phase until the Pi 400 confirms the previous one.

## Ground rules carried into this plan

- The board is flashed by hand. No phase may claim a fix on QEMU evidence
  alone; QEMU has no caches and takes the DWC2 branch, so it cannot validate
  `xhci.c`, the MMU map, or any timing change.
- A measured cost going down is not a symptom going away. Where a phase
  removes a cost but the artefact survives, model the subsystem before
  touching another rectangle.
- Cross-boundary changes are listed explicitly so scope can be declined
  without accepting a workaround in its place.

## Phase 0 — drag-trail fix, awaiting hardware validation

Status: implemented in `src/cores/core_arm64.c`, `arm64-elf` and `pi400` both
build clean. `btron-pi400.img` is ready to flash.

Root cause of the photographed cascade: a window *move* is geometry damage,
but two content-only fast paths outrank the move branch and never heal the
rectangle the window vacated.

- `redraw_top_window()` (wnd.c:581) calls `redraw_all_windows_clip()` and
  nothing else. It draws windows over the existing backbuffer; it can never
  erase. Correct only when the window has not moved.
- The in-app-menu branch used it and presented only `top->bounds`.
- Event classification checks `top_window_menu_open()` (core_arm64.c:1753)
  *before* `wnd_mgr_is_interacting()` (:1761), so a held title-bar drag with
  a menu open sets `appmenu_redraw`, which then blocks the move branch by its
  own guard (:2014-2015).
- `present_full_render()` presents `union(cur, s_prev_win_union, extra)` —
  one generation of history (:1145-1156) — so a later full render rebuilds
  Tier B clean but cannot heal Tier C. That is why the trail is permanent.

Changes:

1. `appmenu_redraw` branch: composite via `workbench_render_damage_paint()`
   over `union(top->bounds, move_damage)` and present that rect.
2. `overlay_redraw` branch: same hole, same rule — heal `move_damage` with
   `present_damage_render()` before the overlay is stamped.
3. Compact-tab STAMP (:1031-1038): `tab_left`/`tab_width` came from the
   window's live geometry while the stamp followed the preview's, unclamped.
   A negative width is a huge `size_t`. Now clamped to the snapshot row.

Gate G0 — flash, then:
- Reproduce the original gesture: open 編集(E) on a window and drag it by the
  title bar. Expected: no trail.
- Read the HUD `W` field during it. It reports `trip_branch`; confirm which
  branch the drag now takes.
- Drag a compact-tab window (the sliding title tab) across the desktop.
  Expected: no smear along the tab row.
- Regression check: menu hover highlight still tracks without a full
  composite. `CMP` should stay in the sub-millisecond range for a hover.

If G0 shows a trail still, the remaining suspect is the opacity memo (Phase 1
item 4) choosing the non-preview route; do not widen the damage rectangles
further without checking it.

## Phase 1 — compositor correctness

Work top to bottom; each item is independently verifiable on the board.

1. **The preview never hides its target.** `target_hidden` (core_arm64.c:207,
   set at :1062, tested at :914) is bookkeeping only — `visible = FALSE`
   appears exactly once in the file, inside `drag_preview_cache_tile()` (:877),
   and is restored on the next line. So during a drag the compositor will
   draw the window at its live `bounds` while the preview stamps a snapshot at
   its own lagging position. Whenever the two disagree you get two images.
   Fix is cross-boundary: give the window list a real "suppressed for preview"
   state that `redraw_all_windows_clip()` honours, rather than a flag the
   preview carries privately.

2. **`drag_preview_reset()` abandons its own stamp.** It restores
   `target->visible` but never restores the backbuffer from
   `s_drag_preview_underlay`, and it reports `redraw_needed` only when
   `target_hidden` is set — which is false for every abort before the first
   completed PRESENT. Callers at :969, :992, :2019, :2024 discard the return
   value entirely.

3. **The opacity memo outlives its objects.**
   `s_drag_preview_opacity_{target,dev,valid,value}` (:219-222) are written at
   :988 and :1001 and read as a pointer-identity test at :935 and :954.
   `cls_wnd` frees the `WND` and `cls_dev` frees `dev->pixels`; nothing
   invalidates the memo. A recycled pair yields a false "opaque" verdict, and
   because the memo also decides whether the preview engages at all, a false
   "transparent" verdict silently routes a drag down the non-preview path.

4. **`rsz_wnd()` overflows `H`.** wnd.c:280-281 computes
   `bounds.right = bounds.left + w` into `int16_t` with only a lower clamp.
   Every rectangle downstream inherits it. This is why resize profiling
   (task #47) has been hard to attribute.

5. **`present_full_render()` history depth.** `s_prev_win_union` holds one
   generation. Decide deliberately: either accumulate the dirty bounding box
   until a full-screen present resets it, or keep one generation and accept
   that any branch which fails to heal its own old rect is permanent. Phase 0
   chose the latter and patched the branches; record that as the contract in
   RENDER.txt so the next fast path added knows the rule.

Gate G1 — drag every window kind (normal, compact-tab, overlapping the panel
and the test bar) for a few minutes. No artefact may survive a release.

## Phase 2 — boot-path timing, the bug class that already bricked USB once

Enabling the caches shortened every iteration-counted delay by roughly 14x
and killed USB HID; task #97 fixed the driver-side loops with CLO polling.
The boot path still has the same shape and it gates display and PCIe bring-up.

- `startup_arm.c:466-483, :526-543, :564-581, :600-617` — VideoCore mailbox
  polls, `to = 2000000` / `to = 1000`.
- `pcie_bcm2711.c:145-158` — VL805 firmware bootstrap, `to = 2000` iterations.
- **Nested loops share one counter** (`startup_arm.c:475-479`,
  `pcie_bcm2711.c:152-158`): the inner poll consumes the outer budget, so the
  effective timeout is neither value and is cache-state dependent.
- `bcm2711_dma.c:53-58` `bcm2711_dma_wait_timeout(channel, loops)` — a
  deadline expressed in iterations, called with 5000000 (:92) and 1000000
  (core_arm64.c:3067).
- `startup_arm.c:84` — on TXFF exhaustion `uart_putc` silently drops the
  character (:87-90), so console evidence disappears exactly when the bus is
  busy.
- `dwc2.c:158, :207, :240, :363, :366, :424` — same class, but QEMU-only on
  this target. Lowest priority; fix for consistency or leave with a comment.

Convert all of these to CLO-deadline waits using the existing `systimer_clo()`
pattern (xhci.c:77-84). Not compiled for pi400, so leave alone:
`ps2_gs.c`, `core_boot.c`, `core_smp.c`, `core_m68k.c`, `core_ps2.c`.

Gate G2 — twenty consecutive cold boots, no display or keyboard failure, and
`CLK ST == CT` on the diag band confirming the timer is still honest.

## Phase 3 — make faults visible before hunting the next one

Highest-severity finding in the audit, and it is not a render bug.

- `startup_arm.c:623-647` — Current-EL synchronous exceptions route to label
  `95`, which reads `ELR_EL1/EL2`, adds 4 and `eret`s. It silently skips one
  instruction, does not read `ESR_EL1` or `FAR_EL1`, does not distinguish a
  data abort from an undefined instruction or a permission fault, cannot
  detect a repeating fault, and does not halt. Lower-EL vectors (:660-672) are
  bare `eret`.
- No watchdog exists. `platform.h:190/204` define the BCM2708/9 PM watchdog;
  nothing in `src/drivers/bcm283x` or `core_arm64.c` writes `WDOG`. A hang is
  indistinguishable from a slow boot.
- Timeouts reported as success: `pcie_bcm2711.c:322-324` logs a VL805
  bootstrap failure, including timeout, as "completed (pre-loaded/EEPROM)";
  `dwc2.c:259-260` is the same shape; `startup_arm.c:499-503` falls back to a
  hardcoded `g_pi_fb_ptr = 0x3c000000` indistinguishable from success.
- `xwrite32()` (xhci.c:38-40) carries no barrier, unlike `xwrite64()`
  (:42-54). The PORTSC reset write (:910) is followed only by `delay_us(5000)`
  before the poll; :1006, :1047, :1051 write port registers with no `dsb`.
  Prime candidate for the next enumeration flake.

Gate G3 — deliberately fault the running kernel and confirm the board prints
`ESR`/`FAR`/`ELR` and halts instead of limping.

## Phase 4 — dead-code removal

Order matters: delete only what a build target proves unreachable, and never
in the same commit as a behaviour change.

**Excluded from review entirely:** `third_party/haiku`,
`third_party/stellux-xhci-tutorial`, `third_party/pi400`,
`include/arch/bcm283x/**`, `include/{tk,sys,tm}`, `t-kernel/`,
`b-book/ b-free/ b-spec/ b-system/`, `assets/`, `tad_bin/`, `licenses/`,
root `*.img/*.elf/*.vol`, and the in-tree `*.arm64 N.o` strays (those strays
deserve their own `gitignore` + cleanup pass).

1. **Zero-cost deletions, provably dead in the arm64 build.**
   `s_drag_preview_dma_enabled` (:223) and `s_drag_preview_dma_rect` (:225)
   have one hit each — their declarations — and are the two warnings the arm64
   build emits today. `s_drag_preview_dma_start_us` (:226) is written at :921
   and never read. `s_drag_preview_dma_active` is never set to 1, so the
   guards at :1538, :1908, :2216 are always true and `bcm2711_dma_abort(0)` at
   :913 is unreachable. Removing these also removes the temptation to
   re-enable channel-0 preview DMA, which is deliberately off (pi400 DMA
   status note).
2. **Orphaned public functions.** `startup_arm.c:548 bcm283x_power_usb`
   (one hit, no prototype); `bcm2711_dma_blit2d`/`_blit`/`_blit_linear`
   (`_linear`'s only caller is the dead `_blit`); `xhci_kbd_dropped`,
   `xhci_mouse_dropped`, `xhci_has_devices`; `dwc2_has_devices`. Verify each
   against the non-arm64 targets before deleting — several are used by m68k,
   ps2 or mips builds.
3. **Dead telemetry.** `g_irq_trace_fb` (startup_arm.c:831) is written at
   core_arm64.c:366 and never read; `arm64_trace_pre/post_dispatch`
   (startup_arm.c:839/840) are empty bodies still `bl`-ed from the vector stub
   (:721, :723) — removing them shrinks the fault path, so do it after G3.
   Write-only: `s_present_dma_max_us`, `s_input_wcet_us`, and
   `async_rt.h:80-83` `isr_us/isr_max_us/key_enqueue_us/key_dispatch_us`.
   `ASYNC_TELEMETRY` (core_arm64.c:68-70) is defined and never tested.
   `ASYNC_INPUT_BUDGET_US`/`ASYNC_UI_BUDGET_US` are referenced only in
   comments — the header claims an overrun check that does not exist; either
   add the check or drop the claim.
4. **Unreachable on target 6.** `async_rt_format_status` (async_rt.h:136) is
   only called under `dev->width >= 1280` (global_menu.c:294) while the target
   is 1024. `mailbox_set_virtual_offset`'s page-flip call (core_arm64.c:1533)
   is dead; only the `(0,0)` reset at :371 runs. `BTRON_AUTO_GUI` (:3369) is
   defined only for ps2. `QEMU_RASPI2B` (dwc2.h:20) is never defined anywhere.
   `startup_arm.c:16` and `:22` are dead for the `-DTYPE_RPI=2/3` the Makefile
   passes.
5. **Duplicated implementations to consolidate.** Five blit shapes:
   `btron_row_blit`, the wrapper at :307, the rect presenter at :806, the
   banded presenter at :779, the full-frame at :2340, plus open-coded loops at
   :764-776 and core_yoko.c:191. Four delay helpers plus `delay_cycles` twice.
   `arm64_irq_diag_t` (startup_arm.c:879-898) is hand-copied as `irqdiag_t`
   (core_arm64.c:3301-3309).
6. **Never compiled by any target: 29 files**, including
   `src/apps/{clock,paint,mail,commander,workbench}.c` and
   `src/kernel/{mem_mgr,proc_mgr,tcpip,clk_mgr,dev_mgr,sys_mgmt,ipc_msg,fs_record}.c`,
   `src/vobject/omgr.c`, `src/tools/databox.c`. Some are referenced by
   `verify/Makefile` host tests. Per-file verification required before any
   deletion; do not bulk-remove on the strength of "no target lists it".
7. **Whole-file twin.** `core_yoko.c` (555 lines, ARM32) is a superseded
   pre-compositor core, compiled by `arm-elf` (:882) — a target with no CI
   job, so compiled-but-unverified. Decide: fix `arm-elf` or retire it.

Gate G4 — `make all` plus `arm64-elf` and `pi400` all build clean with zero
warnings, and the HUD still reports the same values it did before the sweep.

## Phase 5 — documentation reconciliation

Already known stale; fix as part of this pass rather than as a separate task.

- `GPU.txt §7` and `RENDER.txt §6` still describe `drag_preview_repair_strips()`
  and "no more than four non-overlapping strips". That code no longer exists;
  the implementation is a lazy 32x32 tile machine
  (`drag_preview_cache_tile`, `drag_preview_prepare_one_tile`).
- `GPU.txt §5` bandwidths predate the cached-MMU build. Re-measure and
  re-record `NE`/`SC`/`FB` on the current image.
- `doc/md/GPU.md` is declared superseded by `GPU.txt:8` — either delete it or
  make it a pointer.
- `RENDER.txt` remains authoritative on the deliberate presenter contracts
  (single scanout page, DMA off, bounded CPU presenter, cursor stamped into
  the visible FB, 32-window snapshot, half-open damage rects). Do not treat
  those as hazards; they are documented decisions.

## Deferred, tracked elsewhere

- Task #47 resize profiling: all recorded numbers are uncached and stale.
  Redo after Phase 1 item 4, since the `H` overflow may have been part of it.
- Task #90 held-button latency (G99/W99) and task #91 capture-deferred
  repaints: both predate the caches being enabled, so their measurements no
  longer describe the build.
- Boot-time reduction: PCIe RC + VL805 firmware bring-up is a per-boot
  necessity. Phase 2 makes those waits *correct*, not shorter; making them
  shorter stays deferred by choice.
