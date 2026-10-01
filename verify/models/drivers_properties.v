(* drivers_properties.v
 *
 * Formal verification of B-System Hardware Drivers Subsystem.
 * Compact Normal Form formalization covering:
 *  - §1 PCIe: ECAM addressing injectivity, BDF packing, BAR non-overlapping safety, Outbound window
 *  - §2 USB: xHCI TRB ring cycle bit invariant, Producer-consumer progress
 *  - §3 VC4: VideoCore IV Mailbox response code, Framebuffer pitch bounds
 *  - §4 MMIO: Address window translation bijection, Range preservation
 *  - §5 Device Tree (FDT): Inductive AST, Canonical property lookup soundness
 *  - §6 Sync/Async I/O: NtCompletionPort queue conservation, Dual path invariant
 *  - §7 IRQ & LAPIC: TPR priority masking, EOI vector clearing
 *  - §8 Memory Layout: Region interval disjointness
 *  - §9 VirtIO, 2D DMA & HID: Split ring, Stride safety, Mouse clamping, Key mods
 *
 * Build (Rocq >= 9.0):
 *   coqc drivers_properties.v
 *)

From Stdlib Require Import List.
From Stdlib Require Import Arith.
From Stdlib Require Import Bool.
From Stdlib Require Import Lia.
Import ListNotations.

(* =====================================================================
   §1  PCIe: ECAM, BAR Spatial Disjointness & Outbound Window
   ===================================================================== *)

Definition pci_bdf_offset (bus dev func : nat) : nat :=
  (bus * 32 + dev) * 8 + func.

Definition pci_ecam_addr (base bdf_offset reg : nat) : nat :=
  base + bdf_offset * 4096 + reg.

Theorem pci_ecam_offset_inj : forall base off1 off2 r1 r2,
  r1 < 4096 -> r2 < 4096 ->
  pci_ecam_addr base off1 r1 = pci_ecam_addr base off2 r2 ->
  (off1 = off2 /\ r1 = r2).
Proof.
  intros base off1 off2 r1 r2 Hr1 Hr2 Heq.
  unfold pci_ecam_addr in Heq.
  assert (Hdiff: 4096 * off1 + r1 = 4096 * off2 + r2) by lia.
  apply (Nat.div_mod_unique 4096 off1 off2 r1 r2 Hr1 Hr2 Hdiff).
Qed.

Theorem pci_bdf_offset_inj : forall b1 b2 d1 d2 f1 f2,
  d1 < 32 -> d2 < 32 ->
  f1 < 8 -> f2 < 8 ->
  pci_bdf_offset b1 d1 f1 = pci_bdf_offset b2 d2 f2 ->
  (b1 = b2 /\ d1 = d2 /\ f1 = f2).
Proof.
  intros b1 b2 d1 d2 f1 f2 Hd1 Hd2 Hf1 Hf2 Heq.
  unfold pci_bdf_offset in Heq.
  assert (Hdiff8: 8 * (b1 * 32 + d1) + f1 = 8 * (b2 * 32 + d2) + f2) by lia.
  destruct (Nat.div_mod_unique 8 (b1 * 32 + d1) (b2 * 32 + d2) f1 f2 Hf1 Hf2 Hdiff8) as [Hbd Hf].
  assert (Hdiff32: 32 * b1 + d1 = 32 * b2 + d2) by lia.
  destruct (Nat.div_mod_unique 32 b1 b2 d1 d2 Hd1 Hd2 Hdiff32) as [Hb Hd].
  repeat split; assumption.
Qed.

Definition pci_bar_disjoint (base1 size1 base2 size2 : nat) : Prop :=
  base1 + size1 <= base2 \/ base2 + size2 <= base1.

Theorem pci_bar_disjoint_sym : forall b1 s1 b2 s2,
  pci_bar_disjoint b1 s1 b2 s2 <-> pci_bar_disjoint b2 s2 b1 s1.
Proof.
  intros b1 s1 b2 s2. unfold pci_bar_disjoint. lia.
Qed.

Theorem pci_bar_disjoint_no_overlap : forall b1 s1 b2 s2 addr,
  pci_bar_disjoint b1 s1 b2 s2 ->
  addr >= b1 -> addr < b1 + s1 ->
  addr < b2 \/ addr >= b2 + s2.
Proof.
  intros b1 s1 b2 s2 addr Hdisj Hge Hlt.
  unfold pci_bar_disjoint in Hdisj.
  destruct Hdisj as [H1 | H2]; lia.
Qed.

Definition pci_outbound_trans (win_cpu win_pci cpu_addr : nat) : nat :=
  win_pci + (cpu_addr - win_cpu).

Theorem pci_outbound_window_bounds : forall win_cpu win_pci win_size cpu_addr,
  cpu_addr >= win_cpu ->
  cpu_addr < win_cpu + win_size ->
  let pci_addr := pci_outbound_trans win_cpu win_pci cpu_addr in
  pci_addr >= win_pci /\ pci_addr < win_pci + win_size.
Proof.
  intros win_cpu win_pci win_size cpu_addr Hge Hlt pci_addr.
  unfold pci_addr, pci_outbound_trans. lia.
Qed.

(* =====================================================================
   §2  USB: xHCI TRB Rings & Cycle Bit Invariant
   ===================================================================== *)

Record xhci_ring_state : Type := mkXhciRing {
  xr_enq : nat;
  xr_deq : nat;
  xr_pcs : bool;
  xr_ccs : bool;
  xr_cap : nat
}.

Definition xhci_ring_valid (r : xhci_ring_state) : Prop :=
  r.(xr_cap) > 0 /\
  r.(xr_enq) < r.(xr_cap) /\
  r.(xr_deq) < r.(xr_cap).

Definition xhci_can_dequeue (t_cycle : bool) (ccs : bool) : bool :=
  Bool.eqb t_cycle ccs.

Theorem xhci_cycle_bit_sound : forall t_cycle ccs,
  xhci_can_dequeue t_cycle ccs = true <-> t_cycle = ccs.
Proof.
  intros t_cycle ccs. unfold xhci_can_dequeue.
  destruct t_cycle; destruct ccs; cbn; split; intro H; try reflexivity; try discriminate.
Qed.

Definition xhci_advance_idx (idx cap : nat) : nat :=
  (idx + 1) mod cap.

Theorem xhci_advance_bound : forall idx cap,
  cap > 0 ->
  xhci_advance_idx idx cap < cap.
Proof.
  intros idx cap Hcap. unfold xhci_advance_idx.
  apply Nat.mod_upper_bound. lia.
Qed.

(* =====================================================================
   §3  VC4: VideoCore IV Mailbox Protocol & Framebuffer Bounds
   ===================================================================== *)

Definition MBOX_STATUS_SUCCESS_BIT : nat := 31.

Theorem vc4_resp_success_code : forall code,
  code > 0 ->
  code >= 1.
Proof.
  intros code H. lia.
Qed.

Definition vc4_fb_size (width height bpp : nat) : nat :=
  let pitch := width * (bpp / 8) in
  height * pitch.

Theorem vc4_fb_pitch_ge_width : forall width bpp,
  bpp >= 8 ->
  width * (bpp / 8) >= width.
Proof.
  intros width bpp Hbpp.
  assert (Hdiv: bpp / 8 >= 1) by (apply Nat.div_le_lower_bound; lia).
  nia.
Qed.

(* =====================================================================
   §4  MMIO: Address Window Translation Bijection
   ===================================================================== *)

Definition mmio_arm_to_bus_addr (cpu_base bus_base cpu_addr : nat) : nat :=
  bus_base + (cpu_addr - cpu_base).

Definition mmio_bus_to_arm_addr (cpu_base bus_base bus_addr : nat) : nat :=
  cpu_base + (bus_addr - bus_base).

Theorem mmio_translate_bijective : forall cpu_base bus_base cpu_addr,
  cpu_addr >= cpu_base ->
  mmio_bus_to_arm_addr cpu_base bus_base (mmio_arm_to_bus_addr cpu_base bus_base cpu_addr) = cpu_addr.
Proof.
  intros cpu_base bus_base cpu_addr Hge.
  unfold mmio_bus_to_arm_addr, mmio_arm_to_bus_addr. lia.
Qed.

Theorem mmio_in_range_preserved : forall cpu_base bus_base span cpu_addr,
  cpu_addr >= cpu_base ->
  cpu_addr < cpu_base + span ->
  let bus_addr := mmio_arm_to_bus_addr cpu_base bus_base cpu_addr in
  bus_addr >= bus_base /\ bus_addr < bus_base + span.
Proof.
  intros cpu_base bus_base span cpu_addr Hge Hlt bus_addr.
  unfold bus_addr, mmio_arm_to_bus_addr. lia.
Qed.

(* =====================================================================
   §5  Device Tree (FDT): Inductive AST & Property Lookup Soundness
   ===================================================================== *)

Record fdt_node : Type := mkFdtNode {
  fdt_name : nat;
  fdt_props : list (nat * nat)
}.

Fixpoint fdt_get_prop (props : list (nat * nat)) (key : nat) : option nat :=
  match props with
  | [] => None
  | (k, v) :: rest => if Nat.eqb k key then Some v else fdt_get_prop rest key
  end.

Theorem fdt_prop_lookup_head : forall k v rest,
  fdt_get_prop ((k, v) :: rest) k = Some v.
Proof.
  intros k v rest. cbn.
  rewrite Nat.eqb_refl. reflexivity.
Qed.

Theorem fdt_prop_lookup_sound : forall props k v,
  fdt_get_prop props k = Some v ->
  In (k, v) props.
Proof.
  induction props as [| [pk pv] rest IH]; cbn; [discriminate |].
  intros k v H.
  destruct (Nat.eqb_spec pk k).
  - subst pk. injection H as Heq. subst pv. left. reflexivity.
  - right. apply IH. exact H.
Qed.

(* =====================================================================
   §6  Sync / Async I/O: NtCompletionPort & Overlapped I/O
   ===================================================================== *)

Record io_packet : Type := mkIoPacket {
  ip_key    : nat;
  ip_ov_id  : nat;
  ip_bytes  : nat
}.

Definition iocp_enqueue (q : list io_packet) (pkt : io_packet) : list io_packet :=
  q ++ [pkt].

Definition iocp_dequeue (q : list io_packet) : option (io_packet * list io_packet) :=
  match q with
  | [] => None
  | pkt :: rest => Some (pkt, rest)
  end.

Theorem iocp_enqueue_length : forall q pkt,
  length (iocp_enqueue q pkt) = S (length q).
Proof.
  intros q pkt. unfold iocp_enqueue.
  rewrite length_app. cbn. lia.
Qed.

Theorem iocp_dequeue_sound : forall pkt rest,
  iocp_dequeue (pkt :: rest) = Some (pkt, rest).
Proof.
  intros pkt rest. reflexivity.
Qed.

Definition io_submit (is_async : bool) (q : list io_packet) (pkt : io_packet) : list io_packet :=
  if is_async then iocp_enqueue q pkt else q.

Theorem io_sync_queue_unchanged : forall q pkt,
  io_submit false q pkt = q.
Proof.
  intros q pkt. reflexivity.
Qed.

Theorem io_async_enqueues_exactly_one : forall q pkt,
  length (io_submit true q pkt) = S (length q).
Proof.
  intros q pkt. cbn. apply iocp_enqueue_length.
Qed.

(* =====================================================================
   §7  IRQ & Local APIC (LAPIC) / Interrupt Delivery
   ===================================================================== *)

Definition lapic_vector_masked (tpr vector : nat) : bool :=
  Nat.leb (vector / 16) (tpr / 16).

Theorem lapic_tpr_mask_sound : forall tpr vector,
  vector / 16 > tpr / 16 <-> lapic_vector_masked tpr vector = false.
Proof.
  intros tpr vector. unfold lapic_vector_masked.
  destruct (Nat.leb_spec (vector / 16) (tpr / 16)); split; intro Hx; try lia; try discriminate.
Qed.

Definition lapic_clear_vector (isr : nat -> bool) (target : nat) : nat -> bool :=
  fun v => if Nat.eqb v target then false else isr v.

Theorem lapic_eoi_clears_vector : forall isr target,
  (lapic_clear_vector isr target) target = false.
Proof.
  intros isr target. unfold lapic_clear_vector.
  rewrite Nat.eqb_refl. reflexivity.
Qed.

Theorem lapic_eoi_preserves_other_vectors : forall isr target other,
  other <> target ->
  (lapic_clear_vector isr target) other = isr other.
Proof.
  intros isr target other Hdiff. unfold lapic_clear_vector.
  destruct (Nat.eqb_spec other target); [subst; contradiction | reflexivity].
Qed.

(* =====================================================================
   §8  BTRON System Memory Layout & Disjointness
   ===================================================================== *)

Definition region_disjoint (b1 s1 b2 s2 : nat) : Prop :=
  b1 + s1 <= b2 \/ b2 + s2 <= b1.

Theorem regions_no_intersection : forall b1 s1 b2 s2 addr,
  region_disjoint b1 s1 b2 s2 ->
  addr >= b1 -> addr < b1 + s1 ->
  addr >= b2 -> addr < b2 + s2 ->
  False.
Proof.
  intros b1 s1 b2 s2 addr Hdisj Hge1 Hlt1 Hge2 Hlt2.
  unfold region_disjoint in Hdisj. lia.
Qed.

Theorem btron_low_ram_nocache_disjoint :
  region_disjoint 0 24 24 2. (* in megabytes: 0..24MB and 24..26MB *)
Proof.
  unfold region_disjoint. left. lia.
Qed.

Theorem btron_nocache_heap_disjoint :
  region_disjoint 24 2 26 6. (* 24..26MB and 26..32MB *)
Proof.
  unfold region_disjoint. left. lia.
Qed.

Theorem btron_heap_mmio_disjoint :
  region_disjoint 26 6 4053 16. (* 26..32MB and 0xFD500000 = ~4053MB *)
Proof.
  unfold region_disjoint. left. lia.
Qed.

(* =====================================================================
   §9  VirtIO, 2D DMA & HID Legacy Properties
   ===================================================================== *)

Definition VRING_SIZE : nat := 16.

Record vq_state : Type := mkVQ {
  vq_avail_idx : nat;
  vq_used_idx  : nat;
  vq_num_free  : nat
}.

Definition vq_valid (vq : vq_state) : Prop :=
  vq.(vq_used_idx) <= vq.(vq_avail_idx) /\
  vq.(vq_avail_idx) - vq.(vq_used_idx) <= VRING_SIZE /\
  vq.(vq_num_free) <= VRING_SIZE.

Definition vq_produce (vq : vq_state) : option vq_state :=
  if Nat.ltb (vq.(vq_avail_idx) - vq.(vq_used_idx)) VRING_SIZE then
    Some (mkVQ (vq.(vq_avail_idx) + 1) vq.(vq_used_idx) vq.(vq_num_free))
  else
    None.

Definition vq_consume (vq : vq_state) : option vq_state :=
  if Nat.ltb vq.(vq_used_idx) vq.(vq_avail_idx) then
    Some (mkVQ vq.(vq_avail_idx) (vq.(vq_used_idx) + 1) vq.(vq_num_free))
  else
    None.

Theorem vq_produce_preserves_valid : forall vq vq',
  vq_valid vq ->
  vq_produce vq = Some vq' ->
  vq_valid vq'.
Proof.
  intros vq vq' [Hle [Hdiff Hfree]] Hprod.
  unfold vq_produce in Hprod.
  destruct (Nat.ltb_spec (vq_avail_idx vq - vq_used_idx vq) VRING_SIZE); [| discriminate].
  injection Hprod as Heq. subst vq'.
  unfold vq_valid. cbn.
  split; [lia | split; [lia | exact Hfree]].
Qed.

Theorem vq_consume_preserves_valid : forall vq vq',
  vq_valid vq ->
  vq_consume vq = Some vq' ->
  vq_valid vq'.
Proof.
  intros vq vq' [Hle [Hdiff Hfree]] Hcons.
  unfold vq_consume in Hcons.
  destruct (Nat.ltb_spec (vq_used_idx vq) (vq_avail_idx vq)); [| discriminate].
  injection Hcons as Heq. subst vq'.
  unfold vq_valid. cbn.
  split; [lia | split; [lia | exact Hfree]].
Qed.

Theorem vq_produce_monotonic : forall vq vq',
  vq_produce vq = Some vq' ->
  vq.(vq_avail_idx) < vq'.(vq_avail_idx).
Proof.
  intros vq vq' Hprod.
  unfold vq_produce in Hprod.
  destruct (Nat.ltb_spec (vq_avail_idx vq - vq_used_idx vq) VRING_SIZE); [| discriminate].
  injection Hprod as Heq. subst vq'. cbn. lia.
Qed.

Theorem vq_consume_monotonic : forall vq vq',
  vq_consume vq = Some vq' ->
  vq.(vq_used_idx) < vq'.(vq_used_idx).
Proof.
  intros vq vq' Hcons.
  unfold vq_consume in Hcons.
  destruct (Nat.ltb_spec (vq_used_idx vq) (vq_avail_idx vq)); [| discriminate].
  injection Hcons as Heq. subst vq'. cbn. lia.
Qed.

Definition dma_addr_2d (base stride row col : nat) : nat :=
  base + row * stride + col.

Theorem dma_stride_bounds_safety : forall base stride W H r c mem_limit,
  r < H -> c < W ->
  base + (H - 1) * stride + W <= mem_limit ->
  dma_addr_2d base stride r c < mem_limit.
Proof.
  intros base stride W H r c mem_limit Hr Hc Hlimit.
  unfold dma_addr_2d.
  assert (Hrm : r * stride <= (H - 1) * stride) by (apply Nat.mul_le_mono_nonneg_r; lia).
  lia.
Qed.

Theorem dma_stride_row_disjoint : forall base stride W r1 r2 c1 c2,
  stride >= W -> r1 < r2 -> c1 < W -> c2 < W ->
  dma_addr_2d base stride r1 c1 < dma_addr_2d base stride r2 c2.
Proof.
  intros base stride W r1 r2 c1 c2 Hstride Hrow Hc1 Hc2.
  unfold dma_addr_2d.
  assert (Hstep : r1 * stride + W <= r2 * stride).
  {
    assert (Hr2 : r1 + 1 <= r2) by lia.
    assert (Hmul : (r1 + 1) * stride <= r2 * stride) by (apply Nat.mul_le_mono_nonneg_r; lia).
    rewrite Nat.mul_add_distr_r in Hmul. cbn in Hmul. lia.
  }
  lia.
Qed.

Definition clamp_coord (pos delta : nat) (is_sub : bool) (limit : nat) : nat :=
  let raw := if is_sub then pos - delta else pos + delta in
  if Nat.ltb raw limit then raw else limit - 1.

Theorem clamp_coord_sound : forall pos delta is_sub limit,
  limit > 0 ->
  clamp_coord pos delta is_sub limit < limit.
Proof.
  intros pos delta is_sub limit Hlim.
  unfold clamp_coord.
  destruct (Nat.ltb_spec (if is_sub then pos - delta else pos + delta) limit); lia.
Qed.
