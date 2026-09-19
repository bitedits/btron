# BTRON3 Pi 400 (BCM2711) — IRQ / Exception-Vector / EL-Plane Map

Living reference for the 1 kHz timer-IRQ bring-up. Every entry below is taken
from the current source (`src/drivers/bcm283x/cpu/startup_arm.c`,
`src/drivers/bcm283x/cpu/armstub8.S`) or from on-hardware observation
(console photos, 2026-09-19/20). Addresses are physical; MMU is off.

## 1. EL planes and the boot hand-off chain

```
 EL3  start4.elf firmware monitor  (SCR_EL3 owned here; NOT readable from NS)
   |   [optional] armstub8.bin  -- our EL3 stub, config.txt armstub= (REVERTED OFF)
   |     sets SCR_EL3=0x581 (NS|SMD|HCE|RW, IRQ=FIQ=EA=0), GICD_IGROUPR0..7=~0,
   |     parks cores>0, eret -> EL2h @0x80000
   v
 EL2  kernel8.img _start  (firmware's stock drop lands here, AArch64)
   |     HCR_EL2 = 0x80000000   (RW=1; IMO=FMO=AMO=0  -> phys IRQ/FIQ NOT routed to EL2)
   |     CPTR_EL2=0, HSTR_EL2=0, CNTHCTL_EL2=3, CNTVOFF_EL2=0
   |     VBAR_EL2 = arm64_vector_table (safety net for the brief EL2 phase)
   |     SPSR = 0x3C4 (EL1t, DAIF masked); eret
   v
 EL1t kernel main plane   (SPSel=0 -> uses SP_EL0; CurrentEL observed = 1)
         SP_EL0 = thread stack top 0x80000
         SP_EL1 = s_exc_stack top   (set in arm64_irq_init, NOT in _start:
                                     writing SP_EL1 right after the eret bricks boot)
         VBAR_EL1 = arm64_vector_table
```

Observed: `CurrentEL=1`, `at_el2=0`. The EL2->EL1 drop works. EL choice is NOT
the IRQ problem.

## 2. VBAR_EL1 vector table map (`arm64_vector_table`, 2048-aligned)

Kernel runs EL1t (SPSel=0), so same-EL exceptions use the **SP0 group**.
The SPx group is mirrored for safety; lower-EL groups are unreachable (no
lower EL exists).

| Offset | Group / type          | Current handler                       | Notes |
|--------|-----------------------|---------------------------------------|-------|
| 0x000  | Current EL SP0 Sync   | `b 95f` -> fault-skip                 | elr+=4, eret (EL-aware) |
| 0x080  | Current EL SP0 IRQ    | `b 90f` -> IRQ stub                   | **live IRQ entry** |
| 0x100  | Current EL SP0 FIQ    | `b 90f` -> IRQ stub                   | live FIQ entry |
| 0x180  | Current EL SP0 SError | bare `eret`                           | re-enters faulting pt (livelock risk) |
| 0x200  | Current EL SPx Sync   | `95:` fault-skip (in-slot)            | mirrors 0x000 |
| 0x280  | Current EL SPx IRQ    | `b 90f` -> IRQ stub                   | mirrors 0x080 |
| 0x300  | Current EL SPx FIQ    | `b 90f` -> IRQ stub                   | mirrors 0x100 |
| 0x380  | Current EL SPx SError | bare `eret`                           | |
| 0x400..0x780 | Lower EL A64/A32 | bare `eret` (8 slots)           | unreachable (no lower EL) |

`95:` fault-skip: reads CurrentEL; if EL2 fix `elr_el2+=4` else `elr_el1+=4`; eret.
`90:` IRQ stub: spill x16/x17 to `s_stub_scratch`, `s_stub_entries++`,
`sub sp,#272`, save x0-x30 + elr/spsr, `bl arm64_trace_pre_dispatch`,
`bl arm64_irq_dispatch`, `bl arm64_trace_post_dispatch`, restore, `eret`.
(Framebuffer paint breadcrumbs removed 2026-09-20; only the entry counter remains.)

VBAR_EL2 points at the same table (safety net during the EL2 phase).

EL3 (only when armstub enabled): local 2048-aligned table, all 16 entries
`b el3_skip` (elr_el3+=4; eret) so a refused EL3 write degrades to "not applied"
instead of a silent hang.

## 3. Interrupt routing decision tree (where a timer tick can die)

```
 timer PPI asserts
   -> GICD group bit (IGROUPR) selects line:  Group1 -> IRQ line, Group0 -> FIQ line
        [IGROUPR is Secure-only: RAZ/WI from NS (observed readback 0x00000000)]
   -> SCR_EL3.{IRQ,FIQ}: if set, that line is taken to EL3 monitor regardless of EL
        [owned by firmware; NOT readable/writable from NS EL1/EL2]
   -> else HCR_EL2.{IMO,FMO}: if set (and at/near EL2), route to EL2
        [ours = 0, so NOT routed to EL2]
   -> else delivered to the current EL's VBAR group (SP0 vs SPx by SPSel),
      gated by PSTATE.I / PSTATE.F
```

Failure modes seen on hardware, mapped onto the tree:
- Pre-armstub, unmask -> CPU left main and NEVER returned, no stub paint:
  line was taken above EL1 (SCR_EL3 trap) -> EL3 monitor never hands back.
- With armstub (SCR cleared, groups=1): no hang, but also no tick and a
  stripe-corrupted console + dead keyboard even with all ints but the timer
  disabled -> damage from the stub's non-interrupt effects; route abandoned.

## 4. GIC-400 (GICv2) register map

GICD = 0xFF841000, GICC = 0xFF842000.

| Offset (GICD) | Reg        | NS access | Notes / observed |
|------|------------|-----------|------------------|
| 0x000 | CTLR      | grp1 bits | EnableGrp0/Grp1 |
| 0x004 | TYPER     | RO | ITLinesNumber |
| 0x080+4n | IGROUPRn | **RAZ/WI from NS** | group = Secure-owned; readback 0 |
| 0x100+4n | ISENABLERn | RW | enable per-int |
| 0x180+4n | ICENABLERn | RW | disable per-int |
| 0x200+4n | ISPENDRn | store **bus-locks** | forced-pending store hangs |
| 0x280+4n | ICPENDRn | RW | clear pending (used by ISR) |
| 0x400+ | IPRIORITYR | RW | |

| Offset (GICC) | Reg   | Notes |
|------|-------|-------|
| 0x000 | CTLR  | EnableGrp0/Grp1 (+AckCtl); NS-banked |
| 0x004 | PMR   | priority mask |
| 0x008 | BPR   | |
| 0x00C | IAR   | **never exercised on HW** (suspected bus-lock) |
| 0x010 | EOIR  | **never exercised on HW** |
| 0x018 | HPPIR | RO, safe; observed 0x1B (=27) with timer pending |
| 0x0FC | IIDR  | RO |

ISR ack scheme (IAR/EOIR-free): read HPPIR for intid; reload `cntv_tval_el0`;
store GICD_ICPENDR0 bit to drop pending. Stray non-timer intids get
ICENABLER+ICPENDR to stop storms.

## 5. Timer / PPI map

| Source | PPI | EL that owns it | Used here |
|--------|-----|-----------------|-----------|
| CNTV (virtual)   | 27 | EL1 | yes (s_arm64_at_el2==0) |
| CNTHP (hyp phys) | 26 | EL2 | only if at EL2 |
| CNTP (NS phys)   | 30 | NS EL1/EL2 | not used |
cntfrq = 54 000 000 Hz; 1 kHz reload = 54000. Only reliable non-trapping wall
clock = BCM2835 system timer CLO at g_mmio_base+0x3004 (1 MHz).

## 6. Current (reverted, known-good) configuration

- armstub: OFF (config.txt has no armstub= line).
- arm64_irq_init: probe runs MASKED; timer left DISARMED; DAIF masked
  (`daifset #3`); logs "left masked; cooperative input path".
- core_arm64: 50 ms confirm sees 0 ticks -> `s_async_irq_active=0` ->
  cooperative bounded xHCI drain + budget-scheduler GUI loop.
- Result: clean console, working keyboard/GUI; latency bound by the
  synchronous `workbench_render`+blit (the G99 term), not by IRQ.

## 7. Open root-cause avenues (not yet tried on HW)

1. **EL2-resident kernel**: never drop to EL1; set HCR_EL2.IMO=FMO=1 so BOTH
   IRQ and FIQ (whichever line the secure-locked group picks) trap to our EL2
   vectors. Bypasses SCR_EL3 and group opacity without a firmware component.
2. **armstub** (abandoned 2026-09-20): correct in principle (SCR + Group1 are
   Secure-only) but destabilised the board (corruption, dead keyboard, no tick).
3. **Bounded cooperative render**: split `workbench_render`/blit per loop trip
   to meet the latency goal without any hardware IRQ (pre-approved fallback).
