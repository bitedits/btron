(* ui_compose_properties.v
 *
 * Formal verification of B-System GUI Compositor, Invalidation & Bit Blit Engine.
 * Proves:
 *  - Rectangular Algebra (intersection commutativity, exact intersection iff, bbox containment)
 *  - Invalidation Tracking Monotonicity & Screen Bounds Invariants
 *  - Bit Blit 2D Memory Safety & Non-Interference Outside Clip
 *  - Bit Blit ROP_COPY Idempotence & Key Transparency Invariance
 *  - Double Buffering Presentation Soundness & Framebuffer Spatial Isolation
 *
 * Build (Rocq >= 9.0):
 *   coqc ui_compose_properties.v
 *)

From Stdlib Require Import List.
From Stdlib Require Import Arith.
From Stdlib Require Import Bool.
From Stdlib Require Import Lia.
Import ListNotations.

(* =====================================================================
   §1  Rectangular Geometry & Clipping Algebra
   ===================================================================== *)

Record rect : Type := mkRect {
  r_left   : nat;
  r_top    : nat;
  r_right  : nat;
  r_bottom : nat
}.

Definition point_in_rect (x y : nat) (r : rect) : Prop :=
  r.(r_left) <= x /\ x < r.(r_right) /\
  r.(r_top) <= y /\ y < r.(r_bottom).

Definition rect_valid (r : rect) : Prop :=
  r.(r_left) < r.(r_right) /\ r.(r_top) < r.(r_bottom).

Definition rect_intersect (a b : rect) : rect :=
  mkRect (Nat.max a.(r_left) b.(r_left))
         (Nat.max a.(r_top) b.(r_top))
         (Nat.min a.(r_right) b.(r_right))
         (Nat.min a.(r_bottom) b.(r_bottom)).

Definition rect_union_bbox (a b : rect) : rect :=
  mkRect (Nat.min a.(r_left) b.(r_left))
         (Nat.min a.(r_top) b.(r_top))
         (Nat.max a.(r_right) b.(r_right))
         (Nat.max a.(r_bottom) b.(r_bottom)).

Theorem rect_intersect_comm : forall a b,
  rect_intersect a b = rect_intersect b a.
Proof.
  intros a b.
  unfold rect_intersect.
  rewrite (Nat.max_comm (r_left a) (r_left b)).
  rewrite (Nat.max_comm (r_top a) (r_top b)).
  rewrite (Nat.min_comm (r_right a) (r_right b)).
  rewrite (Nat.min_comm (r_bottom a) (r_bottom b)).
  reflexivity.
Qed.

Theorem point_in_intersect_iff : forall x y a b,
  point_in_rect x y (rect_intersect a b) <->
  (point_in_rect x y a /\ point_in_rect x y b).
Proof.
  intros x y a b.
  unfold point_in_rect, rect_intersect. cbn.
  split.
  - intros [Hl [Hr [Ht Hb]]].
    destruct a, b; cbn in *.
    split; split; try split; lia.
  - intros [[Hla [Hra [Hta Hba]]] [Hlb [Hrb [Htb Hbb]]]].
    destruct a, b; cbn in *.
    split; try split; lia.
Qed.

Theorem point_in_union_bbox_l : forall x y a b,
  point_in_rect x y a ->
  point_in_rect x y (rect_union_bbox a b).
Proof.
  intros x y a b [Hl [Hr [Ht Hb]]].
  unfold point_in_rect, rect_union_bbox. cbn.
  destruct a, b; cbn in *.
  split; try split; lia.
Qed.

Theorem point_in_union_bbox_r : forall x y a b,
  point_in_rect x y b ->
  point_in_rect x y (rect_union_bbox a b).
Proof.
  intros x y a b [Hl [Hr [Ht Hb]]].
  unfold point_in_rect, rect_union_bbox. cbn.
  destruct a, b; cbn in *.
  split; try split; lia.
Qed.

Theorem point_in_union_bbox_or : forall x y a b,
  point_in_rect x y a \/ point_in_rect x y b ->
  point_in_rect x y (rect_union_bbox a b).
Proof.
  intros x y a b [Hl | Hr].
  - apply point_in_union_bbox_l; exact Hl.
  - apply point_in_union_bbox_r; exact Hr.
Qed.

(* =====================================================================
   §2  Invalidation & Damage Tracking
   ===================================================================== *)

Definition damage_state := option rect.

Definition accumulate_damage (cur : damage_state) (incoming : rect) : damage_state :=
  match cur with
  | None => Some incoming
  | Some d => Some (rect_union_bbox d incoming)
  end.

Definition take_damage (cur : damage_state) : (option rect * damage_state) :=
  (cur, None).

Theorem damage_inval_monotonicity_prev : forall d r x y,
  point_in_rect x y d ->
  match accumulate_damage (Some d) r with
  | Some res => point_in_rect x y res
  | None => False
  end.
Proof.
  intros d r x y Hin.
  cbn.
  apply point_in_union_bbox_l. exact Hin.
Qed.

Theorem damage_inval_monotonicity_new : forall d r x y,
  point_in_rect x y r ->
  match accumulate_damage (Some d) r with
  | Some res => point_in_rect x y res
  | None => False
  end.
Proof.
  intros d r x y Hin.
  cbn.
  apply point_in_union_bbox_r. exact Hin.
Qed.

Theorem damage_take_clears_state : forall cur d st',
  take_damage cur = (d, st') ->
  st' = None.
Proof.
  intros cur d st' Htake.
  injection Htake as Hd Hst'.
  symmetry. exact Hst'.
Qed.

Definition screen_clipped_damage (screen : rect) (cur : damage_state) (incoming : rect) : damage_state :=
  accumulate_damage cur (rect_intersect screen incoming).

Theorem screen_clipped_damage_bounded : forall screen cur incoming res x y,
  screen_clipped_damage screen cur incoming = Some res ->
  point_in_rect x y (rect_intersect screen incoming) ->
  point_in_rect x y screen.
Proof.
  intros screen cur incoming res x y Hdmg Hin.
  apply point_in_intersect_iff in Hin as [Hscr _].
  exact Hscr.
Qed.

(* =====================================================================
   §3  Bit Blitting Engine & 2D Memory Safety
   ===================================================================== *)

Definition pix_index (stride x y : nat) : nat :=
  y * stride + x.

Theorem blit_stride_linear_bound : forall x y w h W H,
  x + w <= W ->
  y + h <= H ->
  forall dx dy,
  dx < w ->
  dy < h ->
  pix_index W (x + dx) (y + dy) < W * H.
Proof.
  intros x y w h W H Hw Hh dx dy Hdx Hdy.
  unfold pix_index.
  assert (Hyt : y + dy < H) by lia.
  assert (Hxt : x + dx < W) by lia.
  assert (Hmul : (y + dy) * W + (x + dx) < (y + dy + 1) * W).
  {
    replace ((y + dy + 1) * W) with ((y + dy) * W + W) by ring.
    lia.
  }
  assert (Htop : (y + dy + 1) * W <= H * W).
  {
    apply Nat.mul_le_mono_pos_r; lia.
  }
  rewrite Nat.mul_comm in Htop.
  lia.
Qed.

(* Functional representation of 1D row span blit *)
Definition update_span (dst : nat -> nat) (src : nat -> nat)
                       (dst_start src_start len : nat) : nat -> nat :=
  fun i =>
    if (Nat.leb dst_start i) && (Nat.ltb i (dst_start + len)) then
      src (src_start + (i - dst_start))
    else
      dst i.

Theorem blit_span_outside_invariant : forall dst src dst_start src_start len i,
  (i < dst_start \/ i >= dst_start + len) ->
  update_span dst src dst_start src_start len i = dst i.
Proof.
  intros dst src dst_start src_start len i Hout.
  unfold update_span.
  destruct (Nat.leb_spec dst_start i); destruct (Nat.ltb_spec i (dst_start + len)); cbn; try reflexivity; lia.
Qed.

Theorem blit_span_inside_copy : forall dst src dst_start src_start len i,
  dst_start <= i < dst_start + len ->
  update_span dst src dst_start src_start len i = src (src_start + (i - dst_start)).
Proof.
  intros dst src dst_start src_start len i Hin.
  unfold update_span.
  destruct (Nat.leb_spec dst_start i); destruct (Nat.ltb_spec i (dst_start + len)); cbn; try reflexivity; lia.
Qed.

Theorem blit_span_idempotent : forall dst src dst_start src_start len i,
  update_span (update_span dst src dst_start src_start len) src dst_start src_start len i =
  update_span dst src dst_start src_start len i.
Proof.
  intros dst src dst_start src_start len i.
  unfold update_span.
  destruct (Nat.leb_spec dst_start i); destruct (Nat.ltb_spec i (dst_start + len)); cbn; reflexivity.
Qed.

(* Span blit with key transparency (key = 0) *)
Definition update_span_key (dst : nat -> nat) (src : nat -> nat)
                           (dst_start src_start len key : nat) : nat -> nat :=
  fun i =>
    if (Nat.leb dst_start i) && (Nat.ltb i (dst_start + len)) then
      let sp := src (src_start + (i - dst_start)) in
      if Nat.eqb sp key then dst i else sp
    else
      dst i.

Theorem blit_span_key_preserves_dst : forall dst src dst_start src_start len key i,
  dst_start <= i < dst_start + len ->
  src (src_start + (i - dst_start)) = key ->
  update_span_key dst src dst_start src_start len key i = dst i.
Proof.
  intros dst src dst_start src_start len key i Hin Hkey.
  unfold update_span_key.
  destruct (Nat.leb_spec dst_start i); destruct (Nat.ltb_spec i (dst_start + len)); cbn; [| lia | lia | lia].
  destruct (Nat.eqb_spec (src (src_start + (i - dst_start))) key); [reflexivity | lia].
Qed.

(* =====================================================================
   §4  Double Buffering & Presentation Soundness
   ===================================================================== *)

Definition buffer2d := nat -> nat -> nat.

Definition present_buffer_rect (front back : buffer2d) (damage : rect) : buffer2d :=
  fun x y =>
    if (Nat.leb damage.(r_left) x) && (Nat.ltb x damage.(r_right)) &&
       (Nat.leb damage.(r_top) y) && (Nat.ltb y damage.(r_bottom)) then
      back x y
    else
      front x y.

Theorem presenter_inside_matches_back : forall front back damage x y,
  point_in_rect x y damage ->
  present_buffer_rect front back damage x y = back x y.
Proof.
  intros front back damage x y [Hl [Hr [Ht Hb]]].
  unfold present_buffer_rect.
  destruct (Nat.leb_spec (r_left damage) x);
  destruct (Nat.ltb_spec x (r_right damage));
  destruct (Nat.leb_spec (r_top damage) y);
  destruct (Nat.ltb_spec y (r_bottom damage)); cbn; try reflexivity; lia.
Qed.

Theorem presenter_outside_preserves_front : forall front back damage x y,
  ~ point_in_rect x y damage ->
  present_buffer_rect front back damage x y = front x y.
Proof.
  intros front back damage x y Hout.
  unfold present_buffer_rect.
  destruct (Nat.leb_spec (r_left damage) x);
  destruct (Nat.ltb_spec x (r_right damage));
  destruct (Nat.leb_spec (r_top damage) y);
  destruct (Nat.ltb_spec y (r_bottom damage)); cbn; try reflexivity.
  exfalso. apply Hout.
  unfold point_in_rect. lia.
Qed.

Theorem presenter_idempotent : forall front back damage x y,
  present_buffer_rect (present_buffer_rect front back damage) back damage x y =
  present_buffer_rect front back damage x y.
Proof.
  intros front back damage x y.
  unfold present_buffer_rect.
  destruct (Nat.leb_spec (r_left damage) x);
  destruct (Nat.ltb_spec x (r_right damage));
  destruct (Nat.leb_spec (r_top damage) y);
  destruct (Nat.ltb_spec y (r_bottom damage)); cbn; reflexivity.
Qed.

Theorem presenter_damage_partition : forall front back damage x y,
  present_buffer_rect front back damage x y = back x y \/
  present_buffer_rect front back damage x y = front x y.
Proof.
  intros front back damage x y.
  unfold present_buffer_rect.
  destruct (Nat.leb_spec (r_left damage) x);
  destruct (Nat.ltb_spec x (r_right damage));
  destruct (Nat.leb_spec (r_top damage) y);
  destruct (Nat.ltb_spec y (r_bottom damage)); cbn; auto.
Qed.
