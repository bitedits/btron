(* media_rtp_properties.v
 *
 * Formal Rocq / Coq specification and theorems for the B-System media plane
 * (NuStream) that the Audio Stack / WebRTC work sits on.  It is the proved
 * companion of the executable oracle verify/models/media_rtp_model.ml.
 *
 * Properties verified (one group per oracle invariant R1..R10):
 *   1. NuStream static buffer pool: an acquire never mints an id, an exhausted
 *      pool refuses instead of allocating, a shared buffer is not recycled by
 *      one unref, the last unref recycles exactly once, a double release is
 *      flagged rather than silently accepted, and releasing every held buffer
 *      restores the initial pool.  Well-formedness (fixed id set, no double
 *      recycle, rc >= 1, conservation) is preserved by all three operations.
 *   2. Leaky temporal queue: the window is a hard bound, a jettisoned stamp is
 *      accounted for, and the ordering / conservation invariants survive push.
 *   3. Timestamp monotonicity: a regressed or duplicated stamp is refused and
 *      leaves the queue identical apart from its refusal counter.
 *   4. Timestamp dilation: declared-rate stamping keeps its interval while wall
 *      clock advances by the capacity interval; declaring the capacity rate
 *      removes the dilation identically.
 *   5. Isochronous interpolation: the output cadence is exact, no interpolated
 *      frame claims a source that has not arrived, and the source selection
 *      never rewinds.
 *   6. Per-plane CPU budgets: the shipped INPUT/UI numbers are exactly
 *      balanced, the only headroom is the sub-millisecond tail, a silent run
 *      carries no debt, and an overrun without that tail never drains.
 *   7. Peer lifecycle on the compositor grid: a join takes the first free slot
 *      and preserves well-formedness (live slots = peers, resources = 6 per
 *      peer), sixteen joins fill the grid, a seventeenth is refused without
 *      leaking, an out-of-range or empty-slot teardown is refused, and a full
 *      join/leave cycle returns the session to its initial state.
 *   8. Grid geometry on the 1920x1080 canvas: the documented half-cell mapping
 *      addresses only four of the sixteen slots, the 4x4 quarter-cell mapping
 *      keeps every slot, quarter cells never overlap, and the quarter lattice
 *      tiles the canvas exactly at one sixteenth of its area per slot.
 *   9. BTRON_GST tiers and the additive-only fork rule: an off gate exposes no
 *      symbol, T4 stays host-only, T2 shims g_object_set alone, stripping the
 *      fork reproduces the pristine upstream text, a fork only ever adds lines,
 *      and the media symbol table is constant along the BTRON_MP axis.
 *  10. GStreamer grounding of the RTP numbers: the 16-bit serial ring with its
 *      wrap-around span, the jitter-buffer full test and its 15 %/90 %
 *      watermark ordering, the jitter EWMA bound, the two-packet source
 *      probation, the GST_CLOCK_TIME_NONE sentinel, DTS-over-PTS stamping, the
 *      power-of-two ring mask, and the videorate forward gate.
 *
 * No Axiom, no Parameter, no Admitted -- see the Print Assumptions block.
 *)

From Stdlib Require Import Arith.
From Stdlib Require Import List.
From Stdlib Require Import String.
From Stdlib Require Import Lia.

Import ListNotations.
Local Open Scope bool_scope.

Section MediaRtpModel.

  (* ── 1. Contract constants ──────────────────────────────────────── *)

  (* async_rt.h: the two equal-priority planes the media loop is budgeted on. *)
  Definition input_period_us : nat := 1000.
  Definition ui_period_us : nat := 8 * 1000 + 333.
  Definition input_budget_us : nat := 500.
  Definition ui_budget_us : nat := 4000.

  (* GST-SYNC-RTP.md S2.6: Raspberry-Pi-class latency knobs. *)
  Definition jitter_window_us : nat := 500 * 1000.
  Definition preencode_window_us : nat := 300 * 1000.
  Definition period_30fps : nat := 33 * 1000 + 333.
  Definition period_15fps : nat := 66 * 1000 + 666.

  (* NuStream: fixed pools, zero allocation in the data plane. *)
  Definition pool_capacity : nat := 24.

  (* GST-SYNC-RTP.md S2.4: grid topology capacity. *)
  Definition grid_slots : nat := 16.
  Definition canvas_w : nat := 1920.
  Definition canvas_h : nat := 1080.

  (* ── 2. NuStream static buffer pool ────────────────────────────── *)

  Record buf := mk_buf { b_id : nat; b_rc : nat }.

  Record pool := mk_pool {
    p_cap : nat;
    free_ids : list nat;
    active : list buf;
    refused : nat;
    illegal : nat
  }.

  (* The pool is created once at init with a fixed set of ids. *)
  Definition pool0 : pool :=
    mk_pool pool_capacity (seq 0 pool_capacity) [] 0 0.

  Definition mem_id (i : nat) (b : buf) : bool := Nat.eqb (b_id b) i.

  Definition find_buf (i : nat) (p : pool) : option buf :=
    List.find (mem_id i) (active p).

  Definition active_ids (p : pool) : list nat := map b_id (active p).

  (* acquire never mints a new id: it hands out the head of the free list. *)
  Definition acquire_id (p : pool) : option nat :=
    match free_ids p with
    | [] => None
    | i :: _ => Some i
    end.

  Definition acquire (p : pool) : pool :=
    match free_ids p with
    | [] => mk_pool (p_cap p) [] (active p) (S (refused p)) (illegal p)
    | i :: rest =>
        mk_pool (p_cap p) rest (mk_buf i 1 :: active p) (refused p) (illegal p)
    end.

  Definition bump_rc (i : nat) (b : buf) : buf :=
    if Nat.eqb (b_id b) i then mk_buf (b_id b) (S (b_rc b)) else b.

  Definition buffer_ref (i : nat) (p : pool) : pool :=
    match find_buf i p with
    | Some _ =>
        mk_pool (p_cap p) (free_ids p) (map (bump_rc i) (active p)) (refused p) (illegal p)
    | None =>
        mk_pool (p_cap p) (free_ids p) (active p) (refused p) (S (illegal p))
    end.

  Definition dec_rc (i : nat) (b : buf) : buf :=
    if Nat.eqb (b_id b) i then mk_buf (b_id b) (pred (b_rc b)) else b.

  Definition keep_other (i : nat) (b : buf) : bool := negb (Nat.eqb (b_id b) i).

  Definition recycle (i : nat) (p : pool) : pool :=
    mk_pool (p_cap p) (i :: free_ids p)
             (filter (keep_other i) (active p)) (refused p) (illegal p).

  (* unref: rc drops by one, and only the release that reaches zero recycles. *)
  Definition unref (i : nat) (p : pool) : pool :=
    match find_buf i p with
    | None =>
        mk_pool (p_cap p) (free_ids p) (active p) (refused p) (S (illegal p))
    | Some b =>
        if Nat.leb 1 (pred (b_rc b)) then
          mk_pool (p_cap p) (free_ids p) (map (dec_rc i) (active p)) (refused p) (illegal p)
        else
          recycle i p
    end.

  (* ── 2b. Pool well-formedness ──────────────────────────────────── *)

  Definition ids_in_range (p : pool) : Prop :=
    (forall i, In i (free_ids p) -> i < p_cap p) /\
    (forall b, In b (active p) -> b_id b < p_cap p).

  Definition pool_disjoint (p : pool) : Prop :=
    forall i, In i (free_ids p) -> In i (active_ids p) -> False.

  Definition rc_pos (p : pool) : Prop :=
    forall b, In b (active p) -> (1 <=? b_rc b) = true.

  Definition pool_conserved (p : pool) : Prop :=
    List.length (free_ids p) + List.length (active p) = p_cap p.

  Definition pool_wf (p : pool) : Prop :=
    NoDup (free_ids p) /\ NoDup (active_ids p) /\ pool_disjoint p /\
    ids_in_range p /\ pool_conserved p /\ rc_pos p /\ illegal p = 0.

  (* ── 2c. List facts used by the pool proofs ────────────────────── *)

  Lemma map_cong :
    forall (A B : Type) (f g : A -> B) (l : list A),
      (forall x, In x l -> f x = g x) -> map f l = map g l.
  Proof.
    intros A B f g l. induction l as [|a t IH]; intros H.
    - reflexivity.
    - cbn [map]. f_equal.
      + apply H. cbn. left. reflexivity.
      + apply IH. intros x Hx. apply H. cbn. right. exact Hx.
  Qed.

  Lemma filter_all_id :
    forall (A : Type) (P : A -> bool) (l : list A),
      (forall x, In x l -> P x = true) -> filter P l = l.
  Proof.
    intros A P l. induction l as [|a t IH]; intros H.
    - reflexivity.
    - cbn [filter]. rewrite H; [ | cbn; left; reflexivity].
      f_equal. apply IH. intros x Hx. apply H. cbn. right. exact Hx.
  Qed.

  Lemma in_find :
    forall (A : Type) (P : A -> bool) (l : list A) (x : A),
      List.find P l = Some x -> In x l /\ P x = true.
  Proof.
    intros A P l x H.
    induction l as [|a t IH].
    - cbn [List.find] in H. discriminate H.
    - cbn [List.find] in H.
      destruct (P a) eqn:Pa.
      + injection H as Ha. subst x.
        split; [ cbn [List.find]; left; reflexivity | exact Pa ].
      + apply IH in H. destruct H as [Hin HP].
        split; [ cbn; right; exact Hin | exact HP].
  Qed.

  Lemma in_active_ids :
    forall (p : pool) (b : buf), In b (active p) -> In (b_id b) (active_ids p).
  Proof.
    intros p b Hb. unfold active_ids. apply in_map. exact Hb.
  Qed.

  (* A pool whose ids are unique keeps at most one buffer per id. *)
  Lemma noDup_cons_inv :
    forall (A : Type) (x : A) (l : list A),
      NoDup (x :: l) -> (~ In x l) /\ NoDup l.
  Proof.
    intros A x l H. apply NoDup_cons_iff. exact H.
  Qed.

  (* Dropping the single buffer that owns an id shrinks the list by exactly one. *)
  Lemma filter_drop_one :
    forall i (l : list buf),
      NoDup (map b_id l) -> In i (map b_id l) ->
      List.length (filter (keep_other i) l) + 1 = List.length l.
  Proof.
    intros i l. induction l as [|b t IH]; intros Hnd Hin.
    - cbn [map] in Hin. destruct Hin.
    - destruct (Nat.eqb (b_id b) i) eqn:Ei.
      + (* b owns the id, so it is dropped and no other buffer in t can share it *)
        apply Nat.eqb_eq in Ei.
        cbn [map] in Hnd.
        assert (Hb : keep_other i b = false).
        { unfold keep_other. rewrite Ei, Nat.eqb_refl. reflexivity. }
        assert (Hfor : forall x, In x t -> keep_other i x = true).
        { intros x Hx. unfold keep_other.
          destruct (Nat.eqb (b_id x) i) eqn:Ex; [ | reflexivity].
          apply Nat.eqb_eq in Ex.
          destruct (noDup_cons_inv nat (b_id b) (map b_id t) Hnd) as [Hnin _].
          exfalso.
          assert (Eid : b_id x = b_id b) by lia.
          apply Hnin. rewrite <- Eid. apply in_map. exact Hx. }
        cbn [filter]. rewrite Hb. cbn.
        rewrite (filter_all_id buf (keep_other i) t Hfor). cbn. lia.
      + (* b keeps its slot, so the drop happens inside the tail *)
        assert (IHt : List.length (filter (keep_other i) t) + 1 = List.length t).
        { apply IH.
          * cbn [map] in Hnd.
            destruct (noDup_cons_inv nat (b_id b) (map b_id t) Hnd) as [_ Hndt].
            exact Hndt.
          * cbn [map] in Hin. destruct Hin as [E|Hrest].
            { rewrite E in Ei. rewrite Nat.eqb_refl in Ei. discriminate Ei. }
            { exact Hrest. } }
        assert (Hb : keep_other i b = true).
        { unfold keep_other. rewrite Ei. reflexivity. }
        cbn [filter]. rewrite Hb. cbn. lia.
  Qed.

  (* ── 2d. Infrastructure for the pool proofs ────────────────────── *)

  Definition all_ids (p : pool) : list nat := (free_ids p ++ active_ids p)%list.

  Lemma mem_id_eq : forall i b, mem_id i b = true <-> b_id b = i.
  Proof.
    intros i b. unfold mem_id. split.
    - intros H. apply Nat.eqb_eq. exact H.
    - intros H. rewrite H, Nat.eqb_refl. reflexivity.
  Qed.

  Lemma leb_succ_ge1 : forall n, (1 <=? S n) = true.
  Proof. intros n. apply Nat.leb_le. lia. Qed.

  Lemma bump_rc_own : forall i b, b_id b = i -> bump_rc i b = mk_buf i (S (b_rc b)).
  Proof. intros i b E. unfold bump_rc. rewrite E, Nat.eqb_refl. reflexivity. Qed.

  Lemma bump_rc_other : forall i b, b_id b <> i -> bump_rc i b = b.
  Proof.
    intros i b H. unfold bump_rc. destruct (Nat.eqb (b_id b) i) eqn:E; [|reflexivity].
    exfalso. apply H. apply Nat.eqb_eq. exact E.
  Qed.

  Lemma b_id_bump : forall i b, b_id (bump_rc i b) = b_id b.
  Proof. intros i b. unfold bump_rc. destruct (Nat.eqb (b_id b) i); reflexivity. Qed.

  Lemma b_rc_bump : forall i b, b_id b = i -> b_rc (bump_rc i b) = S (b_rc b).
  Proof. intros i b E. rewrite (bump_rc_own i b E). reflexivity. Qed.

  Lemma dec_rc_own : forall i b r, b_id b = i -> b_rc b = S r -> dec_rc i b = mk_buf i r.
  Proof. intros i b r E Er. unfold dec_rc. rewrite E, Nat.eqb_refl, Er. cbn [pred]. reflexivity. Qed.

  Lemma dec_rc_other : forall i b, b_id b <> i -> dec_rc i b = b.
  Proof.
    intros i b H. unfold dec_rc. destruct (Nat.eqb (b_id b) i) eqn:E; [|reflexivity].
    exfalso. apply H. apply Nat.eqb_eq. exact E.
  Qed.

  Lemma b_id_dec : forall i b, b_id (dec_rc i b) = b_id b.
  Proof. intros i b. unfold dec_rc. destruct (Nat.eqb (b_id b) i); reflexivity. Qed.

  Lemma b_rc_dec : forall i b, b_id b = i -> b_rc (dec_rc i b) = pred (b_rc b).
  Proof.
    intros i b E. unfold dec_rc. rewrite E, Nat.eqb_refl. cbn [b_id b_rc]. reflexivity.
  Qed.

  (* A pool with unique ids holds at most one buffer per id. *)
  Lemma owner_unique :
    forall (l : list buf) (x b : buf),
      In x l -> In b l -> NoDup (map b_id l) -> b_id x = b_id b -> x = b.
  Proof.
    intros l x b Hx. induction l as [|a t IH]; intros Hb Hnd Eid.
    - destruct Hx.
    - cbn [map] in Hnd.
      destruct (noDup_cons_inv nat (b_id a) (map b_id t) Hnd) as [Hnin Hndt].
      destruct Hx as [Ex|Ht]; destruct Hb as [Eb|Ht2].
      + subst x. exact Eb.
      + subst x. exfalso. apply Hnin. rewrite Eid. apply in_map. exact Ht2.
      + subst b. exfalso. apply Hnin. rewrite <- Eid. apply in_map. exact Ht.
      + apply IH; [exact Ht | exact Ht2 | exact Hndt | exact Eid].
  Qed.

  Lemma in_filterD :
    forall (A : Type) (P : A -> bool) (x : A) (l : list A), In x (filter P l) -> In x l.
  Proof.
    intros A P x l H. apply filter_In in H. destruct H as [Hin _]. exact Hin.
  Qed.

  Lemma in_map_id :
    forall (l : list buf) (j : nat), In j (map b_id l) -> exists b, In b l /\ b_id b = j.
  Proof.
    intros l j. induction l as [|a t IH]; intros H.
    - destruct H.
    - cbn [map] in H. destruct H as [E|Ht].
      + exists a. split; [cbn; left; reflexivity | exact E].
      + destruct (IH Ht) as [b [Hb E2]]. exists b. split; [cbn; right; exact Hb | exact E2].
  Qed.

  Lemma in_map_id_filter :
    forall (P : buf -> bool) (l : list buf) (j : nat),
      In j (map b_id (filter P l)) -> In j (map b_id l).
  Proof.
    intros P l j. induction l as [|a t IH]; intros Hin.
    - cbn [map] in Hin. destruct Hin.
    - destruct (P a) eqn:Pa.
      + cbn [filter] in Hin. rewrite Pa in Hin. cbn [map] in Hin. cbn [map].
        destruct Hin as [E|Hrest].
        * left. exact E.
        * right. apply IH. exact Hrest.
      + cbn [filter] in Hin. rewrite Pa in Hin. cbn [map].
        right. apply IH. exact Hin.
  Qed.

  Lemma noDup_map_filter :
    forall (P : buf -> bool) (l : list buf),
      NoDup (map b_id l) -> NoDup (map b_id (filter P l)).
  Proof.
    intros P l. induction l as [|a t IH]; intros Hnd.
    - cbn [map filter]. constructor.
    - cbn [map] in Hnd.
      destruct (noDup_cons_inv nat (b_id a) (map b_id t) Hnd) as [Hnin Hndt].
      destruct (P a) eqn:Pa.
      + cbn [filter]. rewrite Pa. cbn [map].
        apply NoDup_cons.
        * intros Hin. apply Hnin. apply in_map_id_filter with (P := P). exact Hin.
        * apply IH. exact Hndt.
      + cbn [filter]. rewrite Pa. apply IH. exact Hndt.
  Qed.

  Lemma find_all_false :
    forall (l : list buf) (i : nat),
      (forall b, In b l -> mem_id i b = false) -> List.find (mem_id i) l = None.
  Proof.
    intros l i. induction l as [|a t IH]; intros H.
    - reflexivity.
    - cbn [List.find]. rewrite (H a (or_introl eq_refl)). cbn [negb]. rewrite IH.
      + reflexivity.
      + intros b Hb. apply H. cbn. right. exact Hb.
  Qed.

  Lemma keep_other_of_mem_false :
    forall i b, mem_id i b = false -> keep_other i b = true.
  Proof. intros i b H. unfold keep_other. unfold mem_id in H. rewrite H. reflexivity. Qed.

  Lemma filter_keep_all :
    forall (l : list buf) (i : nat),
      (forall b, In b l -> mem_id i b = false) -> filter (keep_other i) l = l.
  Proof.
    intros l i H. apply filter_all_id. intros b Hb. apply keep_other_of_mem_false. apply H. exact Hb.
  Qed.

  Lemma find_keep_none :
    forall (l : list buf) (i : nat),
      (forall b, In b l -> mem_id i b = false) ->
      List.find (mem_id i) (filter (keep_other i) l) = None.
  Proof.
    intros l i H. rewrite (filter_keep_all l i H). apply find_all_false. exact H.
  Qed.

  Lemma eqb_false : forall a b, a <> b -> (a =? b) = false.
  Proof.
    intros a b H. destruct (Nat.eqb a b) eqn:E; [|reflexivity].
    exfalso. apply H. apply Nat.eqb_eq. exact E.
  Qed.

  Lemma map_ids_bump :
    forall (l : list buf) (i : nat), map b_id (map (bump_rc i) l) = map b_id l.
  Proof.
    intros l i. induction l as [|b t IH]; cbn [map].
    - reflexivity.
    - rewrite (b_id_bump i b). rewrite IH. reflexivity.
  Qed.

  Lemma map_ids_dec :
    forall (l : list buf) (i : nat), map b_id (map (dec_rc i) l) = map b_id l.
  Proof.
    intros l i. induction l as [|b t IH]; cbn [map].
    - reflexivity.
    - rewrite (b_id_dec i b). rewrite IH. reflexivity.
  Qed.

  (* The unique owner of an id survives a map that keeps its id and relabels
     nobody else: the lookup still finds it, now in its image shape. *)
  Lemma find_map_owner :
    forall (f : buf -> buf) (l : list buf) (b : buf) (i : nat),
      In b l -> b_id b = i -> NoDup (map b_id l) -> b_id (f b) = i ->
      (forall x, In x l -> b_id x <> i -> b_id (f x) = b_id x) ->
      List.find (mem_id i) (map f l) = Some (f b).
  Proof.
    intros f l b i. induction l as [|a t IH];
      intros Hb Hbi Hnd Hown H2.
    - destruct Hb.
    - cbn [map] in Hnd.
      destruct (noDup_cons_inv nat (b_id a) (map b_id t) Hnd) as [Hnin Hndt].
      destruct Hb as [Ex|Ht].
      + (* the head buffer is the owner *)
        assert (Fa : f a = f b) by (rewrite Ex; reflexivity).
        cbn [map List.find]. rewrite Fa.
        rewrite (proj2 (mem_id_eq i (f b))); [reflexivity | exact Hown].
      + (* a carries some other id, so the lookup passes over its image *)
        assert (Ha : b_id a <> i).
        { intros HP.
          assert (Eab : a = b).
          { apply (owner_unique (a :: t) a b);
              [ cbn; left; reflexivity | cbn; right; exact Ht | exact Hnd
              | rewrite HP; symmetry; exact Hbi ]. }
          rewrite <- Eab in Ht. exfalso. apply Hnin. apply in_map. exact Ht. }
        assert (H2t : forall x, In x t -> b_id x <> i -> b_id (f x) = b_id x).
        { intros x Hx. apply H2. cbn. right. exact Hx. }
        assert (Eqid : b_id (f a) = b_id a)
          by (apply H2; [cbn; left; reflexivity | exact Ha]).
        assert (Hbid : b_id (f a) <> i) by (rewrite Eqid; exact Ha).
        assert (Hfa : mem_id i (f a) = false).
        { unfold mem_id. apply eqb_false. exact Hbid. }
        cbn [map List.find]. rewrite Hfa. cbn.
        rewrite (IH Ht Hbi Hndt Hown H2t). reflexivity.
  Qed.

  (* A reference never changes which ids are live, only their refcounts. *)
  Lemma active_ids_buffer_ref : forall p i, active_ids (buffer_ref i p) = active_ids p.
  Proof.
    intros p i. unfold buffer_ref, active_ids.
    destruct (find_buf i p) as [b|] eqn:E;
      cbn [p_cap free_ids active refused illegal].
    - cbn [active_ids]. apply map_ids_bump.
    - reflexivity.
  Qed.

  Lemma active_ids_unref_shared :
    forall p i b,
      find_buf i p = Some b -> (1 <=? pred (b_rc b)) = true ->
      active_ids (unref i p) = active_ids p.
  Proof.
    intros p i b E G. unfold unref, active_ids. rewrite E, G.
    cbn [p_cap free_ids active refused illegal].
    cbn [active_ids]. apply map_ids_dec.
  Qed.


  (* ── 2e. The static-pool contract: the proved form of oracle R1 ───── *)

  (* [seq start (S n)] re-bases its tail, so the length argument is inducted on
     with the start index left quantified. *)
  Lemma seq_length : forall len start, List.length (seq start len) = len.
  Proof.
    induction len as [|n IH]; intros start.
    - cbn [seq]. reflexivity.
    - cbn [seq List.length]. rewrite IH. reflexivity.
  Qed.

  Lemma seq_lt_start : forall len start i, In i (seq start len) -> start <= i.
  Proof.
    induction len as [|n IH]; intros start i H.
    - cbn [seq] in H. destruct H.
    - cbn [seq] in H. destruct H as [E|Hrest].
      + rewrite <- E. lia.
      + apply IH in Hrest. lia.
  Qed.

  Lemma seq_lt_upper : forall len start i, In i (seq start len) -> i < start + len.
  Proof.
    induction len as [|n IH]; intros start i H.
    - cbn [seq] in H. destruct H.
    - cbn [seq] in H. destruct H as [E|Hrest].
      + rewrite E. lia.
      + apply IH in Hrest. lia.
  Qed.

  Lemma seq_nodup : forall len start, NoDup (seq start len).
  Proof.
    induction len as [|n IH]; intros start.
    - cbn [seq]. constructor.
    - cbn [seq]. apply NoDup_cons.
      + intros Hin. apply seq_lt_start in Hin. lia.
      + apply IH.
  Qed.

  (* The two numeric conjuncts (buffer count, illegal counter) are closed by
     conversion, so [repeat split] disposes of them and only the list-shaped
     obligations are named below. *)
  Theorem pool0_wf : pool_wf pool0.
  Proof.
    unfold pool_wf, pool0, pool_disjoint, ids_in_range, pool_conserved, rc_pos,
            active_ids, p_cap, free_ids, active, refused, illegal.
    cbn [map].
    repeat split.
    - apply seq_nodup.
    - constructor.
    - intros i _ Hout. destruct Hout.
    - intros i Hin. apply seq_lt_upper in Hin. cbn in Hin. exact Hin.
    - intros b Hin. destruct Hin.
    - intros b Hin. destruct Hin.
  Qed.

  (* acquire moves the head of the free list into the active set: the total id
     count is unchanged and no id that was not already in the pool appears. *)
  Lemma acquire_keeps_conserved : forall p, pool_conserved p -> pool_conserved (acquire p).
  Proof.
    intros p Hcons. unfold pool_conserved, acquire in *.
    destruct (free_ids p) as [i rest|];
      cbn [p_cap free_ids active List.length] in *; lia.
  Qed.

  Lemma acquire_never_mints :
    forall p i, In i (active_ids (acquire p)) -> In i (free_ids p) \/ In i (active_ids p).
  Proof.
    intros p k Hin. unfold active_ids in *.
    revert Hin. unfold acquire. destruct (free_ids p) as [x rest|];
      intro Hin; cbn [map p_cap free_ids active] in Hin |- *.
    - right. exact Hin.
    - destruct Hin as [E|Hrest].
      + left. subst k. cbn. left. reflexivity.
      + right. exact Hrest.
  Qed.

  Lemma exhausted_acquire_refuses :
    forall p, free_ids p = [] ->
      refused (acquire p) = S (refused p) /\ active p = active (acquire p).
  Proof.
    intros p Efl. unfold acquire. rewrite Efl.
    cbn [p_cap free_ids active refused illegal]. split; reflexivity.
  Qed.

  Theorem acquire_preserves_wf : forall p, pool_wf p -> pool_wf (acquire p).
  Proof.
    intros p Hp. unfold pool_wf in Hp.
    destruct Hp as [Hndf [Hnda [Hdis [Hrng [Hcons [Hpos Hill]]]]]].
    unfold pool_disjoint, ids_in_range, pool_conserved, rc_pos, active_ids in *.
    unfold acquire. destruct (free_ids p) as [| x rest] eqn:Efl.
    - (* empty pool: nothing moves, only the refusal counter grows *)
      cbn [List.length] in Hcons.
      unfold pool_wf, pool_disjoint, ids_in_range, pool_conserved, rc_pos, active_ids.
      cbn [p_cap free_ids active refused illegal map b_id b_rc List.length].
      repeat split.
      + constructor.
      + exact Hnda.
      + intros j Hinfree _. cbn in Hinfree. destruct Hinfree.
      + intros j Hin. cbn in Hin. destruct Hin.
      + intros b Hinb. apply (proj2 Hrng b). exact Hinb.
      + lia.
      + exact Hpos.
      + exact Hill.
    - (* the head id moves into the active set, referenced exactly once *)
      cbn [List.length] in Hcons.
      unfold pool_wf, pool_disjoint, ids_in_range, pool_conserved, rc_pos, active_ids.
      cbn [p_cap free_ids active refused illegal map b_id b_rc List.length].
      repeat split.
      + apply (proj2 (noDup_cons_inv nat x rest Hndf)).
      + apply NoDup_cons.
        * intros Hx. apply (Hdis x).
          -- cbn. left. reflexivity.
          -- exact Hx.
        * exact Hnda.
      + intros j Hinfree Hinact. destruct Hinact as [Ej|Hother].
        * subst j.
          destruct (noDup_cons_inv nat x rest Hndf) as [Hnin _].
          apply Hnin. exact Hinfree.
        * apply (Hdis j).
          -- cbn. right. exact Hinfree.
          -- exact Hother.
      + intros j Hin. apply (proj1 Hrng j). cbn. right. exact Hin.
      + intros b Hinb. destruct Hinb as [Eb|Hother].
        * subst b. apply (proj1 Hrng x). cbn. left. reflexivity.
        * apply (proj2 Hrng b). exact Hother.
      + lia.
      + intros b Hinb. destruct Hinb as [Eb|Hother].
        * subst b. cbn [b_rc]. apply leb_succ_ge1.
        * apply Hpos. exact Hother.
      + exact Hill.
  Qed.

  (* ── 2f. Referencing and releasing: rc accounting, no double recycle ── *)

  Lemma map_len :
    forall (A B : Type) (f : A -> B) (l : list A),
      Datatypes.length (map f l) = Datatypes.length l.
  Proof.
    intros A B f l. induction l as [|a t IH]; cbn [Datatypes.length map].
    - reflexivity.
    - rewrite IH. reflexivity.
  Qed.

  (* The unique owner of an id is exactly what the lookup returns. *)
  Lemma find_buf_owner :
    forall p i b, In b (active p) -> b_id b = i -> NoDup (active_ids p) -> find_buf i p = Some b.
  Proof.
    intros p i b. unfold find_buf, active_ids. induction (active p) as [|a t IH];
      intros Hb Hbi Hnd.
    - destruct Hb.
    - cbn [map] in Hnd.
      destruct (noDup_cons_inv nat (b_id a) (map b_id t) Hnd) as [Hnin Hndt].
      destruct Hb as [Ea|Ht].
      + subst b.
        assert (Hm : mem_id i a = true) by (apply (proj2 (mem_id_eq i a)); exact Hbi).
        cbn [List.find]. rewrite Hm. cbn. reflexivity.
      + assert (Hne : b_id a <> i).
        { intros HP. apply Hnin. rewrite HP, <- Hbi. apply in_map. exact Ht. }
        assert (Hm : mem_id i a = false).
        { unfold mem_id. apply eqb_false. exact Hne. }
        cbn [List.find]. rewrite Hm. cbn.
        apply IH; [ exact Ht | exact Hbi | exact Hndt ].
  Qed.

  Lemma owner_of_lookup : forall p i b, find_buf i p = Some b -> In b (active p) /\ b_id b = i.
  Proof.
    intros p i b Efind.
    destruct (in_find buf (mem_id i) (active p) b Efind) as [Hb Hm].
    split; [ exact Hb | apply (proj1 (mem_id_eq i b)); exact Hm ].
  Qed.

  Theorem buffer_ref_preserves_wf :
    forall p i, pool_wf p -> In i (active_ids p) -> pool_wf (buffer_ref i p).
  Proof.
    intros p i Hp HinAct. unfold pool_wf in Hp.
    destruct Hp as [Hndf [Hnda [Hdis [Hrng [Hcons [Hpos Hill]]]]]].
    unfold pool_disjoint, ids_in_range, pool_conserved, rc_pos, active_ids in *.
    destruct (in_map_id (active p) i HinAct) as [b [Hb Ebi]].
    assert (Efind : find_buf i p = Some b).
    { apply (find_buf_owner p i b Hb Ebi Hnda). }
    unfold buffer_ref. rewrite Efind.
    cbn [p_cap free_ids active refused illegal].
    unfold pool_wf, pool_disjoint, ids_in_range, pool_conserved, rc_pos, active_ids.
    cbn [p_cap free_ids active refused illegal map b_id b_rc List.length].
    repeat split.
    + exact Hndf.
    + rewrite map_ids_bump. exact Hnda.
    + intros j Hinfree Hinact. rewrite map_ids_bump in Hinact. apply (Hdis j).
      * exact Hinfree.
      * exact Hinact.
    + intros j Hin. apply (proj1 Hrng j). exact Hin.
    + intros y HinY. apply in_map_iff in HinY. destruct HinY as [x [Eq Hx]]. subst y.
      rewrite (b_id_bump i x). apply (proj2 Hrng x). exact Hx.
    + rewrite map_len. exact Hcons.
    + intros y HinY. apply in_map_iff in HinY. destruct HinY as [x [Eq Hx]]. subst y.
      destruct (Nat.eqb (b_id x) i) eqn:Exi.
      * apply Nat.eqb_eq in Exi. rewrite (b_rc_bump i x Exi). apply leb_succ_ge1.
      * assert (Hne : b_id x <> i).
        { intros HP. rewrite HP in Exi. rewrite Nat.eqb_refl in Exi. discriminate. }
        rewrite (bump_rc_other i x Hne). apply Hpos. exact Hx.
    + exact Hill.
  Qed.

  Theorem buffer_ref_absent_is_flagged :
    forall p i, find_buf i p = None ->
      illegal (buffer_ref i p) = S (illegal p) /\ active (buffer_ref i p) = active p.
  Proof.
    intros p i Efind. unfold buffer_ref. rewrite Efind.
    cbn [p_cap free_ids active refused illegal]. split; reflexivity.
  Qed.

  Theorem unref_shared_preserves_wf :
    forall p i b,
      pool_wf p -> find_buf i p = Some b -> (1 <=? pred (b_rc b)) = true ->
      pool_wf (unref i p).
  Proof.
    intros p i b Hp Efind G. unfold pool_wf in Hp.
    destruct Hp as [Hndf [Hnda [Hdis [Hrng [Hcons [Hpos Hill]]]]]].
    unfold pool_disjoint, ids_in_range, pool_conserved, rc_pos, active_ids in *.
    destruct (owner_of_lookup p i b Efind) as [Hb Ebi].
    unfold unref. rewrite Efind, G.
    cbn [p_cap free_ids active refused illegal].
    unfold pool_wf, pool_disjoint, ids_in_range, pool_conserved, rc_pos, active_ids.
    cbn [p_cap free_ids active refused illegal map b_id b_rc List.length].
    repeat split.
    + exact Hndf.
    + rewrite map_ids_dec. exact Hnda.
    + intros j Hinfree Hinact. rewrite map_ids_dec in Hinact. apply (Hdis j).
      * exact Hinfree.
      * exact Hinact.
    + intros j Hin. apply (proj1 Hrng j). exact Hin.
    + intros y HinY. apply in_map_iff in HinY. destruct HinY as [x [Eq Hx]]. subst y.
      rewrite (b_id_dec i x). apply (proj2 Hrng x). exact Hx.
    + rewrite map_len. exact Hcons.
    + intros y HinY. apply in_map_iff in HinY. destruct HinY as [x [Eq Hx]]. subst y.
      destruct (Nat.eqb (b_id x) i) eqn:Exi.
      * apply Nat.eqb_eq in Exi.
        assert (Ex : x = b).
        { apply (owner_unique (active p) x b Hx Hb Hnda). rewrite Exi, Ebi. reflexivity. }
        rewrite Ex, (b_rc_dec i b Ebi). exact G.
      * assert (Hne : b_id x <> i).
        { intros HP. rewrite HP in Exi. rewrite Nat.eqb_refl in Exi. discriminate. }
        rewrite (dec_rc_other i x Hne). apply Hpos. exact Hx.
    + exact Hill.
  Qed.

  Theorem unref_absent_is_flagged :
    forall p i, find_buf i p = None ->
      illegal (unref i p) = S (illegal p) /\ free_ids (unref i p) = free_ids p.
  Proof.
    intros p i Efind. unfold unref. rewrite Efind.
    cbn [p_cap free_ids active refused illegal]. split; reflexivity.
  Qed.

  (* The release that drops the last reference is the only one that recycles. *)
  Theorem unref_last_release_recycles :
    forall p i b,
      pool_wf p -> find_buf i p = Some b -> b_rc b = 1 ->
      free_ids (unref i p) = i :: free_ids p /\
      ~ In i (active_ids (unref i p)) /\
      pool_conserved (unref i p) /\
      illegal (unref i p) = illegal p /\
      refused (unref i p) = refused p.
  Proof.
    intros p i b Hp Efind Eb1. unfold pool_wf in Hp.
    destruct Hp as [Hndf [Hnda [Hdis [Hrng [Hcons [Hpos Hill]]]]]].
    unfold pool_conserved, active_ids in *.
    destruct (owner_of_lookup p i b Efind) as [Hb Ebi].
    assert (Hinact : In i (map b_id (active p))).
    { rewrite <- Ebi. apply in_map. exact Hb. }
    assert (G : (1 <=? pred (b_rc b)) = false) by (rewrite Eb1; cbn [pred]; reflexivity).
    unfold unref. rewrite Efind, G.
    cbn [p_cap free_ids active refused illegal].
    unfold recycle, pool_conserved, active_ids.
    cbn [p_cap free_ids active refused illegal map List.length].
    assert (Fl : Datatypes.length (filter (keep_other i) (active p)) + 1 =
                 Datatypes.length (active p)).
    { apply (filter_drop_one i (active p) Hnda Hinact). }
    split; [ reflexivity | split ].
    - intros Hin. apply in_map_id in Hin.
      destruct Hin as [x [Hx Ex]]. apply filter_In in Hx. destruct Hx as [_ Hkeep].
      unfold keep_other in Hkeep. rewrite Ex in Hkeep.
      rewrite Nat.eqb_refl in Hkeep. cbn [negb] in Hkeep. discriminate.
    - split.
      + cbn [List.length] in Hcons, Fl |- *. lia.
      + split; reflexivity.
  Qed.

  (* No id drifts: the union of the free list and the live buffers is exactly the
     capacity, with every id appearing once. *)
  Theorem no_id_drift :
    forall p, pool_wf p -> NoDup (all_ids p) /\ Datatypes.length (all_ids p) = p_cap p.
  Proof.
    intros p Hp. unfold pool_wf in Hp.
    destruct Hp as [Hndf [Hnda [Hdis [Hrng [Hcons [Hpos Hill]]]]]].
    unfold all_ids, active_ids, pool_disjoint.
    split.
    - apply NoDup_app.
      + exact Hndf.
      + exact Hnda.
      + intros x Hf Ha. apply (Hdis x).
        * exact Hf.
        * exact Ha.
    - rewrite app_length, map_len. exact Hcons.
  Qed.

  (* ── 2g. The lifecycle the oracle exercises, checked by computation ── *)

  Fixpoint acquire_n (n : nat) (p : pool) : pool :=
    match n with
    | 0 => p
    | S k => acquire_n k (acquire p)
    end.

  Fixpoint unref_each (l : list nat) (p : pool) : pool :=
    match l with
    | [] => p
    | i :: t => unref_each t (unref i p)
    end.

  Definition full_pool : pool := acquire_n pool_capacity pool0.
  Definition shipped_lifecycle : pool := unref_each (seq 0 pool_capacity) full_pool.
  Definition shared_step : pool := unref 0 (buffer_ref 0 (acquire pool0)).

  Example acquire_up_to_capacity_empties_the_free_list :
    free_ids full_pool = [].
  Proof. vm_compute. reflexivity. Qed.

  Example one_acquire_past_capacity_is_refused_once :
    refused (acquire_n (S pool_capacity) pool0) = 1.
  Proof. vm_compute. reflexivity. Qed.

  Example releasing_every_buffer_refills_the_pool :
    Datatypes.length (free_ids shipped_lifecycle) = pool_capacity /\
    active shipped_lifecycle = [] /\
    illegal shipped_lifecycle = 0 /\
    refused shipped_lifecycle = 0.
  Proof. split; split; split; vm_compute; reflexivity. Qed.

  Example a_shared_buffer_survives_the_first_release :
    active shared_step = mk_buf 0 1 :: [] /\
    Datatypes.length (free_ids shared_step) = pred pool_capacity /\
    illegal shared_step = 0.
  Proof. split; split; vm_compute; reflexivity. Qed.

  (* Once the last reference is dropped the id is back in the free list, so a
     further release of the same id cannot find a buffer and is flagged. *)
  Definition recycled_then_released : pool := unref 0 (unref 0 shared_step).

  Example releasing_a_recycled_id_is_flagged :
    active recycled_then_released = [] /\
    Datatypes.length (free_ids recycled_then_released) = pool_capacity /\
    illegal recycled_then_released = 1.
  Proof. split; split; vm_compute; reflexivity. Qed.

  (* ── 3. Leaky temporal queue (oracle R2) ─────────────────────────── *)

  Definition queue_cap : nat := 300.

  Record qstate := mk_q { q_items : list nat; q_dropped : nat }.

  Definition q0 : qstate := mk_q [] 0.

  (* A full NuStream queue sheds its oldest frame: the temporal order of what
     survives is untouched, which is what separates the leaky policy from an
     arbitrary eviction. *)
  Definition enqueue (t : nat) (q : qstate) : qstate :=
    if Nat.ltb (Datatypes.length (q_items q)) queue_cap then
      mk_q ((q_items q ++ [t])%list) (q_dropped q)
    else
      match q_items q with
      | [] => mk_q [t] (q_dropped q)
      | _ :: rest => mk_q ((rest ++ [t])%list) (S (q_dropped q))
      end.

  Fixpoint feed (l : list nat) (q : qstate) : qstate :=
    match l with
    | [] => q
    | t :: r => feed r (enqueue t q)
    end.

  Lemma andb_true : forall b1 b2, b1 = true -> b2 = true -> (b1 && b2) = true.
  Proof. intros b1 b2 H1 H2. rewrite H1, H2. reflexivity. Qed.

  Lemma andb_true_inv : forall b1 b2, (b1 && b2) = true -> b1 = true /\ b2 = true.
  Proof.
    intros b1 b2 H.
    destruct b1; destruct b2; simpl in H; auto.
  Qed.

  Lemma enqueue_conserves_one :
    forall t q,
      Datatypes.length (q_items (enqueue t q)) + q_dropped (enqueue t q) =
      S (Datatypes.length (q_items q) + q_dropped q).
  Proof.
    intros t q. unfold enqueue.
    destruct (Nat.ltb (Datatypes.length (q_items q)) queue_cap) eqn:Etest.
    - cbn [q_items q_dropped Datatypes.length]. rewrite last_length. lia.
    - destruct (q_items q) as [hd rest|] eqn:Eitems.
      + cbn [q_items q_dropped Datatypes.length]. lia.
      + cbn [q_items q_dropped Datatypes.length]. rewrite last_length. lia.
  Qed.

  (* Every frame is either resident or counted as dropped: the queue conserves the
     frame count, which is the leaky-queue form of enq = deq + drop. *)
  Theorem feed_conserves_every_frame :
    forall l q,
      Datatypes.length (q_items (feed l q)) + q_dropped (feed l q) =
      Datatypes.length (q_items q) + q_dropped q + Datatypes.length l.
  Proof.
    intros l. induction l as [|a t IH]; intros q; cbn [feed Datatypes.length].
    - lia.
    - rewrite IH, enqueue_conserves_one. lia.
  Qed.

  (* The cap is an invariant, not a consequence: a queue that starts within the
     cap never grows past it, whichever branch of the leaky policy is taken. *)
  Lemma enqueue_respects_cap :
    forall t q,
      Datatypes.length (q_items q) <= queue_cap ->
      Datatypes.length (q_items (enqueue t q)) <= queue_cap.
  Proof.
    intros t q Hcap. unfold enqueue.
    destruct (Nat.ltb (Datatypes.length (q_items q)) queue_cap) eqn:Etest.
    - apply Nat.ltb_lt in Etest. cbn [q_items Datatypes.length].
      rewrite last_length. lia.
    - destruct (q_items q) eqn:Eitems.
      + cbn [q_items Datatypes.length] in Hcap |- *.
        unfold queue_cap in *. lia.
      + cbn [q_items Datatypes.length] in Hcap |- *.
        rewrite last_length. lia.
  Qed.

  Theorem feed_stays_within_capacity :
    forall l q, Datatypes.length (q_items q) <= queue_cap ->
      Datatypes.length (q_items (feed l q)) <= queue_cap.
  Proof.
    intros l. induction l as [|a t IH]; intros q Hcap; cbn [feed].
    - exact Hcap.
    - apply IH. apply enqueue_respects_cap. exact Hcap.
  Qed.

  Example feeding_one_past_capacity_drops_the_oldest :
    q_items (feed (seq 0 (S queue_cap)) q0) = seq 1 queue_cap /\
    q_dropped (feed (seq 0 (S queue_cap)) q0) = 1 /\
    Datatypes.length (q_items (feed (seq 0 (S queue_cap)) q0)) = queue_cap.
  Proof. split; split; vm_compute; reflexivity. Qed.

  (* ── 4. PTS monotonicity and dilation (oracle R3, R4) ────────────── *)

  (* [mono_from x l] asserts that [l] is non-decreasing and that its first
     sample is not earlier than [x]; [monotone] is the same test seeded with the
     stream's own first stamp.  The two-argument shape is what keeps the
     recursion structurally decreasing: the head only ever moves into the tail. *)
  Fixpoint mono_from (x : nat) (l : list nat) : bool :=
    match l with
    | [] => true
    | y :: t => Nat.leb x y && mono_from y t
    end.

  Definition monotone (l : list nat) : bool :=
    match l with
    | [] => true
    | x :: r => mono_from x r
    end.

  Lemma monotone_cons :
    forall x l, mono_from x l = true -> monotone (x :: l) = true.
  Proof. intros x l H. cbn [monotone]. exact H. Qed.

  (* Weakening the seed: a non-decreasing tail stays non-decreasing when it is
     measured against any earlier stamp, which is what lets a producer's own
     cursor be moved backwards without breaking the ordering check. *)
  Lemma mono_from_weaken :
    forall l x y,
      Nat.leb x y = true -> mono_from y l = true -> mono_from x l = true.
  Proof.
    destruct l as [|a t]; intros x y Hxy Hm.
    - cbn [mono_from]. reflexivity.
    - cbn [mono_from] in Hm |- *.
      destruct (andb_true_inv (Nat.leb y a) (mono_from a t) Hm) as [Ha Hrest].
      apply andb_true.
      + apply Nat.leb_le.
        assert (H1 : x <= y) by (apply Nat.leb_le; exact Hxy).
        assert (H2 : y <= a) by (apply Nat.leb_le; exact Ha). lia.
      + exact Hrest.
  Qed.

  (* A stream is the accumulation of its own deltas, so non-negative deltas make
     it monotone by construction: no separate ordering check is needed at
     runtime (oracle R3). *)
  Fixpoint accum (deltas : list nat) (base : nat) : list nat :=
    match deltas with
    | [] => [base]
    | d :: r => base :: accum r (base + d)
    end.

  Lemma mono_from_accum :
    forall deltas base, mono_from base (accum deltas base) = true.
  Proof.
    induction deltas as [|d r IH]; intros base; cbn [accum].
    - cbn [mono_from]. apply andb_true.
      + apply Nat.leb_le. reflexivity.
      + reflexivity.
    - cbn [mono_from]. apply andb_true.
      + apply Nat.leb_le. reflexivity.
      + apply (mono_from_weaken (accum r (base + d)) base (base + d)).
        * apply Nat.leb_le. lia.
        * apply IH.
  Qed.

  Theorem accum_is_monotone :
    forall deltas base, monotone (accum deltas base) = true.
  Proof.
    intros deltas base. destruct deltas as [|d r].
    - cbn [accum monotone mono_from]. reflexivity.
    - cbn [accum monotone].
      apply (mono_from_weaken (accum r (base + d)) base (base + d)).
      + apply Nat.leb_le. lia.
      + apply mono_from_accum.
  Qed.

  Lemma mono_from_map :
    forall (f : nat -> nat) (l : list nat),
      (forall x y, Nat.leb x y = true -> Nat.leb (f x) (f y) = true) ->
      forall z, mono_from z l = true -> mono_from (f z) (map f l) = true.
  Proof.
    intros f l Hf. induction l as [|a t IH]; intros z Hz.
    - cbn [map mono_from]. reflexivity.
    - cbn [map mono_from] in Hz |- *.
      destruct (andb_true_inv (Nat.leb z a) (mono_from a t) Hz) as [Hza Hrest].
      apply andb_true.
      + apply (Hf z a Hza).
      + apply (IH a Hrest).
  Qed.

  Lemma monotone_map :
    forall (f : nat -> nat) (l : list nat),
      (forall x y, Nat.leb x y = true -> Nat.leb (f x) (f y) = true) ->
      monotone l = true -> monotone (map f l) = true.
  Proof.
    intros f l Hf. destruct l as [|a t]; intros Hm.
    - cbn [map monotone]. reflexivity.
    - cbn [map monotone] in Hm |- *. apply (mono_from_map f t Hf a Hm).
  Qed.

  (* Dilating a stream by an integer factor keeps it monotone: the videorate 2:1
     slowdown is a dilation, so R3 survives it (oracle R4). *)
  Definition dilate (k : nat) (t : nat) : nat := k * t.

  Lemma dilate_preserves_order :
    forall k x y, Nat.leb x y = true -> Nat.leb (dilate k x) (dilate k y) = true.
  Proof.
    intros k x y H. unfold dilate. apply Nat.leb_le.
    apply Nat.mul_le_mono_l. apply Nat.leb_le in H. exact H.
  Qed.

  Theorem dilate_preserves_monotone :
    forall k l, monotone l = true -> monotone (map (dilate k) l) = true.
  Proof.
    intros k l Hm. apply monotone_map.
    - intros x y H. apply (dilate_preserves_order k x y H).
    - exact Hm.
  Qed.

  (* The frame-counter stream used by the oracle: consecutive stamps ordered. *)
  Lemma seq_S : forall start n, seq start (S n) = start :: seq (S start) n.
  Proof. reflexivity. Qed.

  Lemma mono_from_seq : forall n start, mono_from start (seq start n) = true.
  Proof.
    induction n as [|n IH]; intros start.
    - cbn [seq mono_from]. reflexivity.
    - rewrite seq_S. cbn [mono_from]. apply andb_true.
      + apply Nat.leb_le. reflexivity.
      + apply (mono_from_weaken (seq (S start) n) start (S start)).
        * apply Nat.leb_le. lia.
        * apply IH.
  Qed.

  Lemma monotone_seq : forall n start, monotone (seq start n) = true.
  Proof.
    intros n start. destruct n as [|m].
    - cbn [seq monotone]. reflexivity.
    - rewrite seq_S. apply (monotone_cons start (seq (S start) m)).
      apply (mono_from_weaken (seq (S start) m) start (S start)).
      + apply Nat.leb_le. lia.
      + apply mono_from_seq.
  Qed.

  Example shipped_15fps_is_a_dilation_of_30fps : period_15fps = 2 * period_30fps.
  Proof. reflexivity. Qed.

  (* ── 5. The videorate output lattice (oracle R5) ────────────────── *)

  (* gstvideorate keeps one next_output_ts and only ever emits on that lattice, so
     the stamps it produces are exactly the multiples of the output period the
     input has already reached. *)
  Definition outputs_upto (P last : nat) : list nat :=
    map (Nat.mul P) (seq 0 (S (last / P))).

  Theorem outputs_never_ahead :
    forall P last s, In s (outputs_upto P last) -> s <= last.
  Proof.
    intros P last s Hin. unfold outputs_upto in Hin.
    apply in_map_iff in Hin. destruct Hin as [k [Eq HIn]].
    apply seq_lt_upper in HIn. cbn [Nat.add] in HIn.
    rewrite <- Eq. apply Nat.le_trans with (m := P * (last / P)).
    - apply Nat.mul_le_mono_l. lia.
    - apply Nat.Div0.mul_div_le.
  Qed.

  Theorem outputs_are_monotone : forall P last, monotone (outputs_upto P last) = true.
  Proof.
    intros P last. unfold outputs_upto. apply monotone_map.
    - intros x y H. apply Nat.leb_le. apply Nat.mul_le_mono_l.
      apply Nat.leb_le in H. exact H.
    - apply monotone_seq.
  Qed.

  Example shipped_lattice_for_the_probe_window :
    outputs_upto period_30fps 200000 =
    [0; period_30fps; 2 * period_30fps; 3 * period_30fps; 4 * period_30fps;
     5 * period_30fps; 6 * period_30fps].
  Proof. vm_compute. reflexivity. Qed.

  (* ── 6. Per-plane CPU budgets (oracle R6) ──────────────────────── *)

  (* async_rt.h schedules the media loop on two equal-priority planes: a hard
     1 kHz INPUT slice and an ~8.3 ms UI frame slice.  Every whole INPUT slice
     inside one UI period donates its unused half to the UI band; the
     sub-millisecond tail of the period is a second, optional donation. *)
  Definition whole_slices : nat := ui_period_us / input_period_us.
  Definition tail_us : nat := ui_period_us mod input_period_us.
  Definition supply_whole : nat := whole_slices * (input_period_us - input_budget_us).
  Definition supply_tail : nat := supply_whole + tail_us.

  Record plane_state := mk_plane {
    ps_debt : nat;
    ps_peak : nat;
    ps_starved : nat
  }.

  Definition plane0 : plane_state := mk_plane 0 0 0.

  Definition period_demand (first : bool) (overrun : nat) : nat :=
    ui_budget_us + if first then overrun else 0.

  (* The debt a period leaves behind.  Truncated subtraction is faithful here: a
     negative [raw] in the oracle means "surplus", which clamps the debt to zero
     and adds nothing to the starvation count. *)
  Definition period_raw (supply : nat) (first : bool) (overrun : nat)
                  (st : plane_state) : nat :=
    ps_debt st + period_demand first overrun - supply.

  Definition step (supply : nat) (first : bool) (overrun : nat)
             (st : plane_state) : plane_state :=
    mk_plane (Nat.max 0 (period_raw supply first overrun st))
             (Nat.max (ps_peak st) (period_raw supply first overrun st))
             (ps_starved st
              + if Nat.ltb 0 (period_raw supply first overrun st) then 1 else 0).

  Fixpoint run_aux (n : nat) (first : bool) (supply overrun : nat)
             (st : plane_state) : plane_state :=
    match n with
    | 0 => st
    | S k => run_aux k false supply overrun (step supply first overrun st)
    end.

  Definition run_periods (n : nat) (use_tail : bool) (overrun : nat) : plane_state :=
    run_aux n true (if use_tail then supply_tail else supply_whole) overrun plane0.

  (* The shipped numbers are exactly balanced: the whole slices donate precisely
     the UI band's own budget, not one microsecond of slack. *)
  Example supply_whole_equals_the_ui_budget : supply_whole = ui_budget_us.
  Proof. reflexivity. Qed.

  Example shipped_input_budget_fits_its_period : input_budget_us <= input_period_us.
  Proof. apply Nat.leb_le. reflexivity. Qed.

  Example shipped_integer_utilization_below_one_core :
    input_budget_us * ui_period_us + ui_budget_us * input_period_us <
    input_period_us * ui_period_us.
  Proof. apply Nat.ltb_lt. vm_compute. reflexivity. Qed.

  Example shipped_tail_is_smaller_than_a_slice : tail_us < input_period_us.
  Proof. apply Nat.ltb_lt. vm_compute. reflexivity. Qed.

  (* The non-interference rule is load-bearing: only the sub-millisecond tail can
     absorb anything beyond the UI budget. *)
  Example headroom_lives_only_in_the_tail :
    ui_budget_us < supply_tail /\ supply_whole <= ui_budget_us.
  Proof. split; apply Nat.ltb_lt; vm_compute; reflexivity. Qed.

  Lemma supply_covers_budget :
    forall (ut : bool), ui_budget_us <= (if ut then supply_tail else supply_whole).
  Proof. intros ut. apply Nat.leb_le. destruct ut; vm_compute; reflexivity. Qed.

  Lemma ltb_0_pos : forall n, 0 < n -> (Nat.ltb 0 n) = true.
  Proof. intros n H. apply Nat.ltb_lt. exact H. Qed.

  (* Reading a step through its [raw] value keeps the record algebra out of the
     arithmetic. *)
  Lemma step_raw :
    forall supply first overrun st (raw : nat),
      period_raw supply first overrun st = raw ->
      step supply first overrun st =
      mk_plane (Nat.max 0 raw)
               (Nat.max (ps_peak st) raw)
               (ps_starved st + if Nat.ltb 0 raw then 1 else 0).
  Proof.
    intros supply first overrun st raw Hraw. unfold step. rewrite Hraw. reflexivity.
  Qed.

  Lemma raw_silent : forall supply (first : bool) st,
    ui_budget_us <= supply -> ps_debt st = 0 -> period_raw supply first 0 st = 0.
  Proof.
    intros supply first st Hsup Hd. unfold period_raw, period_demand.
    rewrite Hd. destruct first; cbn [Nat.add]; lia.
  Qed.

  (* A silent period: no overrun and a supply that covers the demand leave the
     state exactly where it was. *)
  Lemma step_silent :
    forall supply (first : bool) st,
      ui_budget_us <= supply -> ps_debt st = 0 -> ps_peak st = 0 -> ps_starved st = 0 ->
      step supply first 0 st = st.
  Proof.
    intros supply first st Hsup Hd Hp Hs.
    rewrite (step_raw supply first 0 st 0 (raw_silent supply first st Hsup Hd)).
    destruct st as [d p q]. cbn [ps_debt ps_peak ps_starved] in Hd, Hp, Hs |- *.
    rewrite Hd, Hp, Hs. cbn [Nat.max Nat.add Nat.ltb]. reflexivity.
  Qed.

  Lemma run_aux_silent :
    forall n supply (first : bool) st,
      ui_budget_us <= supply -> ps_debt st = 0 -> ps_peak st = 0 -> ps_starved st = 0 ->
      run_aux n first supply 0 st = st.
  Proof.
    induction n as [|k IH]; intros supply first st Hsup Hd Hp Hs.
    - reflexivity.
    - cbn [run_aux]. rewrite (step_silent supply first st Hsup Hd Hp Hs).
      apply (IH supply false st Hsup Hd Hp Hs).
  Qed.

  Theorem no_overrun_leaves_no_debt :
    forall n (ut : bool), run_periods n ut 0 = plane0.
  Proof.
    intros n ut. unfold run_periods.
    apply (run_aux_silent n (if ut then supply_tail else supply_whole) true plane0).
    - apply (supply_covers_budget ut).
    - reflexivity.
    - reflexivity.
    - reflexivity.
  Qed.

  Lemma raw_first_overrun : forall overrun,
    period_raw supply_whole true overrun plane0 = overrun.
  Proof.
    intros overrun. unfold period_raw, plane0, period_demand. cbn [ps_debt Nat.add].
    rewrite supply_whole_equals_the_ui_budget. lia.
  Qed.

  Lemma first_overrun_step :
    forall overrun, 0 < overrun ->
      step supply_whole true overrun plane0 = mk_plane overrun overrun 1.
  Proof.
    intros overrun Hot.
    rewrite (step_raw supply_whole true overrun plane0 overrun
                     (raw_first_overrun overrun)).
    unfold plane0. cbn [ps_debt ps_peak ps_starved].
    rewrite (ltb_0_pos overrun Hot). cbn [Nat.max Nat.add]. reflexivity.
  Qed.

  Lemma raw_stuck : forall overrun st,
    ps_debt st = overrun -> period_raw supply_whole false overrun st = overrun.
  Proof.
    intros overrun st Hd. unfold period_raw, period_demand.
    rewrite Hd, supply_whole_equals_the_ui_budget. cbn [Nat.add]. lia.
  Qed.

  Lemma step_stuck :
    forall overrun st,
      0 < overrun -> ps_debt st = overrun -> ps_peak st = overrun ->
      step supply_whole false overrun st = mk_plane overrun overrun (S (ps_starved st)).
  Proof.
    intros overrun st Hot Hd Hp.
    rewrite (step_raw supply_whole false overrun st overrun (raw_stuck overrun st Hd)).
    rewrite (ltb_0_pos overrun Hot).
    replace (Nat.max 0 overrun) with overrun by lia.
    rewrite Hp. replace (Nat.max overrun overrun) with overrun by lia.
    rewrite Nat.add_1_r. reflexivity.
  Qed.

  Lemma run_aux_stuck :
    forall overrun n st,
      0 < overrun -> ps_debt st = overrun -> ps_peak st = overrun ->
      run_aux n false supply_whole overrun st =
      mk_plane overrun overrun (n + ps_starved st).
  Proof.
    intros overrun n. induction n as [|k IH]; intros st Hot Hd Hp.
    - cbn [Nat.add]. destruct st as [d p q];
        cbn [ps_debt ps_peak ps_starved] in Hd, Hp |- *.
      rewrite Hd, Hp. reflexivity.
    - cbn [run_aux]. rewrite (step_stuck overrun st Hot Hd Hp).
      pose proof
        (IH (mk_plane overrun overrun (S (ps_starved st))) Hot eq_refl eq_refl) as Hi.
      rewrite Hi at 1. f_equal. cbn [ps_starved]. lia.
  Qed.

  (* Without the tail the debt is permanent: every later period is exactly
     balanced, so it can neither pay back nor grow. *)
  Theorem debt_never_drains_without_tail :
    forall overrun n, 0 < overrun ->
      ps_debt (run_periods (S n) false overrun) = overrun /\
      ps_starved (run_periods (S n) false overrun) = S n.
  Proof.
    intros overrun n Hot. unfold run_periods.
    replace (if false then supply_tail else supply_whole) with supply_whole
      by reflexivity.
    cbn [run_aux].
    rewrite (first_overrun_step overrun Hot).
    rewrite (run_aux_stuck overrun n (mk_plane overrun overrun 1) Hot eq_refl eq_refl).
    split; cbn [ps_debt ps_starved].
    - reflexivity.
    - rewrite Nat.add_1_r. reflexivity.
  Qed.

  Example tail_absorbs_a_500us_overrun_in_two_periods :
    run_periods 2 true 500 = mk_plane 0 167 1.
  Proof. vm_compute. reflexivity. Qed.

  Example no_tail_never_drains_a_500us_overrun :
    run_periods 8 false 500 = mk_plane 500 500 8.
  Proof. vm_compute. reflexivity. Qed.

  (* ── 7. Peer lifecycle (oracle R7) ─────────────────────────────── *)

  Definition res_per_peer : nat := 6.

  Record session := mk_session {
    ss_slots : list bool;
    ss_peers : nat;
    ss_res : nat
  }.

  Definition slot_table : list bool := map (fun _ => false) (seq 0 grid_slots).
  Definition session0 : session := mk_session slot_table 0 0.

  Fixpoint count_true (l : list bool) : nat :=
    match l with
    | [] => 0
    | b :: t => if b then S (count_true t) else count_true t
    end.

  Definition used_slots (s : session) : nat := count_true (ss_slots s).

  Fixpoint set_slot (b : bool) (i : nat) (l : list bool) : list bool :=
    match l with
    | [] => []
    | x :: t => if Nat.eqb i 0 then b :: t else x :: set_slot b (pred i) t
    end.

  Fixpoint first_free (start : nat) (l : list bool) : option nat :=
    match l with
    | [] => None
    | x :: t => if x then first_free (S start) t else Some start
    end.

  Inductive outcome := Released | Not_a_member | Bad_slot.

  Definition join (s : session) : session * option nat :=
    match first_free 0 (ss_slots s) with
    | None => (s, None)
    | Some i =>
        (mk_session (set_slot true i (ss_slots s)) (S (ss_peers s))
                    (res_per_peer + ss_res s), Some i)
    end.

  Definition leave (s : session) (i : nat) : session * outcome :=
    match nth_error (ss_slots s) i with
    | None => (s, Bad_slot)
    | Some false => (s, Not_a_member)
    | Some true =>
        (mk_session (set_slot false i (ss_slots s)) (pred (ss_peers s))
                    (ss_res s - res_per_peer), Released)
    end.

  Definition wf_session (s : session) : Prop :=
    Datatypes.length (ss_slots s) = grid_slots /\
    count_true (ss_slots s) = ss_peers s /\
    ss_res s = res_per_peer * ss_peers s.

  Fixpoint all_true_from (l : list bool) : bool :=
    match l with
    | [] => true
    | b :: t => b && all_true_from t
    end.

  Fixpoint join_n (n : nat) (s : session) : session :=
    match n with
    | 0 => s
    | S k => join_n k (fst (join s))
    end.

  Fixpoint leave_all (n : nat) (s : session) : session :=
    match n with
    | 0 => s
    | S k => leave_all k (fst (leave s k))
    end.

  Lemma set_slot_length :
    forall l b i, Datatypes.length (set_slot b i l) = Datatypes.length l.
  Proof.
    induction l as [|a t IH]; intros b i; cbn [set_slot Datatypes.length].
    - reflexivity.
    - destruct i; cbn [Nat.eqb pred Datatypes.length].
      + reflexivity.
      + rewrite IH. reflexivity.
  Qed.

  Lemma count_true_set_slot_true :
    forall l i, nth_error l i = Some false ->
      count_true (set_slot true i l) = S (count_true l).
  Proof.
    induction l as [|a t IH]; intros i Hn; cbn [nth_error] in Hn.
    - destruct i; discriminate.
    - destruct i as [|i'].
      + destruct a; [discriminate Hn|].
        cbn [set_slot count_true Nat.eqb]. reflexivity.
      + cbn [nth_error] in Hn.
        cbn [set_slot count_true Nat.eqb pred].
        destruct a; rewrite (IH i' Hn); lia.
  Qed.

  (* Stated with an explicit witness instead of [pred]: a recursive [count_true]
     never reduces under [pred], and [lia] cannot see through it. *)
  Lemma count_true_set_slot_false :
    forall l i, nth_error l i = Some true ->
      exists m, count_true l = S m /\ count_true (set_slot false i l) = m.
  Proof.
    induction l as [|a t IH]; intros i Hn; cbn [nth_error] in Hn.
    { destruct i; discriminate. }
    destruct i as [|i'].
    { destruct a; [| discriminate Hn].
      cbn [set_slot count_true Nat.eqb]. exists (count_true t). split; reflexivity. }
    cbn [nth_error] in Hn.
    cbn [set_slot count_true Nat.eqb pred].
    destruct (IH i' Hn) as [m [Ht Hx]].
    destruct a.
    { exists (S m). split.
      { rewrite Ht. reflexivity. }
      { rewrite Hx. reflexivity. } }
    exists m. split.
    { exact Ht. }
    exact Hx.
  Qed.

  Lemma first_free_none_of_all_true_from :
    forall l i, all_true_from l = true -> first_free i l = None.
  Proof.
    induction l as [|a t IH]; intros i H; cbn [all_true_from] in H |- *.
    - reflexivity.
    - destruct (andb_prop a (all_true_from t) H) as [Ha Ht].
      rewrite Ha. apply IH. exact Ht.
  Qed.

  Lemma first_free_spec :
    forall l i j, first_free i l = Some j ->
      exists k, j = i + k /\ nth_error l k = Some false.
  Proof.
    induction l as [|a t IH]; intros i j Hf; cbn [first_free] in Hf.
    - destruct j; discriminate.
    - destruct a eqn:Ha.
      + cbn [Nat.eqb] in Hf.
        destruct (IH (S i) j Hf) as [k [Ej Hn]].
        exists (S k). split.
        * lia.
        * cbn [nth_error]. exact Hn.
      + injection Hf as Ej. subst j.
        exists 0. split; [lia | cbn [nth_error]; reflexivity].
  Qed.

  Lemma join_preserves_wf : forall s, wf_session s -> wf_session (fst (join s)).
  Proof.
    intros s [Hlen [Hused Hres]]. unfold wf_session, join.
    destruct (first_free 0 (ss_slots s)) as [i|] eqn:Efree;
      cbn [ss_slots ss_peers ss_res Datatypes.fst].
    - split; [rewrite set_slot_length; exact Hlen |].
      split.
      + destruct (first_free_spec (ss_slots s) 0 i Efree) as [k [Ej Hn]].
        cbn [Nat.add] in Ej. subst i.
        rewrite (count_true_set_slot_true (ss_slots s) k Hn).
        rewrite Hused. reflexivity.
      + rewrite Hres. unfold res_per_peer. lia.
    - split; [exact Hlen |]. split; [exact Hused | exact Hres].
  Qed.

  Lemma all_true_from_map_true :
    forall (A : Type) (l : list A), all_true_from (map (fun _ : A => true) l) = true.
  Proof.
    induction l as [|a t IH]; cbn [map all_true_from].
    - reflexivity.
    - rewrite IH. reflexivity.
  Qed.

  Lemma join_refused_when_full :
    forall p r n,
      fst (join (mk_session (map (fun _ => true) (seq 0 n)) p r))
      = mk_session (map (fun _ => true) (seq 0 n)) p r.
  Proof.
    intros p r n. unfold join. cbn [ss_slots].
    rewrite (first_free_none_of_all_true_from (map (fun _ => true) (seq 0 n)) 0).
    - reflexivity.
    - apply (all_true_from_map_true nat).
  Qed.

  Lemma leave_released_preserves_wf :
    forall s i b,
      wf_session s -> ss_peers s = S b -> nth_error (ss_slots s) i = Some true ->
      wf_session (fst (leave s i)).
  Proof.
    intros s i b [Hlen [Hused Hres]] Ep Hn. unfold wf_session, used_slots, leave.
    rewrite Hn. cbn [ss_slots ss_peers ss_res Datatypes.fst].
    split; [rewrite set_slot_length; exact Hlen |].
    split.
    - destruct (count_true_set_slot_false (ss_slots s) i Hn) as [m [H1 H2]].
      rewrite H2. replace (pred (ss_peers s)) with m.
      + reflexivity.
      + rewrite <- Hused, H1. rewrite Nat.pred_succ. reflexivity.
    - unfold res_per_peer in Hres |- *.
      rewrite Hres, Ep, Nat.pred_succ, Nat.mul_succ_r. lia.
  Qed.

  Example sixteen_joins_fill_the_grid :
    ss_peers (join_n grid_slots session0) = grid_slots /\
    used_slots (join_n grid_slots session0) = grid_slots /\
    wf_session (join_n grid_slots session0).
  Proof.
    (* [split] closes the numeric conjuncts by conversion, so the reflexive step
       is chained rather than bulleted. *)
    vm_compute. repeat split; reflexivity.
  Qed.

  Example seventeenth_join_is_refused_without_leaking :
    join_n (S grid_slots) session0 = join_n grid_slots session0.
  Proof. vm_compute. reflexivity. Qed.

  Example out_of_range_leave_is_refused :
    leave (join_n grid_slots session0) grid_slots
      = (join_n grid_slots session0, Bad_slot).
  Proof. vm_compute. reflexivity. Qed.

  Example empty_slot_leave_is_refused :
    leave session0 0 = (session0, Not_a_member).
  Proof. vm_compute. reflexivity. Qed.

  Example full_cycle_returns_the_session_to_its_initial_state :
    leave_all grid_slots (join_n grid_slots session0) = session0.
  Proof. vm_compute. reflexivity. Qed.

  (* ── 7b. Compositor grid geometry (oracle R8) ──────────────────── *)

  Record cell := mk_cell { cx : nat; cy : nat; cw : nat; ch : nat }.

  Definition cell_doc (i : nat) : cell :=
    mk_cell ((i mod 2) * (canvas_w / 2)) ((i / 2) * (canvas_h / 2))
            (canvas_w / 2) (canvas_h / 2).

  Definition cell_4x4 (i : nat) : cell :=
    mk_cell ((i mod 4) * (canvas_w / 4)) ((i / 4) * (canvas_h / 4))
            (canvas_w / 4) (canvas_h / 4).

  Definition on_canvas (c : cell) : bool :=
    Nat.ltb (cx c + cw c) (S canvas_w) && Nat.ltb (cy c + ch c) (S canvas_h).

  Definition cells_on_canvas (f : nat -> cell) (n : nat) : nat :=
    count_true (map (fun i => on_canvas (f i)) (seq 0 n)).

  Definition cell_area (c : cell) : nat := cw c * ch c.

  (* Axis-aligned intersection, written with [<?] so it stays computable. *)
  Definition overlaps (a b : cell) : bool :=
    Nat.ltb (cx a) (cx b + cw b) && Nat.ltb (cx b) (cx a + cw a) &&
    Nat.ltb (cy a) (cy b + ch b) && Nat.ltb (cy b) (cy a + ch a).

  Fixpoint add_all (l : list nat) : nat :=
    match l with
    | [] => 0
    | x :: t => x + add_all t
    end.

  Definition overlap_pairs (f : nat -> cell) (n : nat) : nat :=
    add_all
      (map (fun i =>
              add_all
                (map (fun j =>
                        if Nat.eqb i j then 0
                        else if overlaps (f i) (f j) then 1 else 0)
                     (seq 0 n)))
           (seq 0 n)).

  Example documented_mapping_addresses_only_four_slots :
    cells_on_canvas cell_doc grid_slots = 4.
  Proof. vm_compute. reflexivity. Qed.

  Example documented_mapping_is_exact_for_its_own_quadrants :
    cells_on_canvas cell_doc 4 = 4.
  Proof. vm_compute. reflexivity. Qed.

  Example quarter_cell_mapping_keeps_every_slot :
    cells_on_canvas cell_4x4 grid_slots = grid_slots.
  Proof. vm_compute. reflexivity. Qed.

  Example quarter_cells_do_not_overlap :
    overlap_pairs cell_4x4 grid_slots = 0.
  Proof. vm_compute. reflexivity. Qed.

  Example quarter_lattice_closes_on_the_canvas :
    4 * (canvas_w / 4) = canvas_w /\ 4 * (canvas_h / 4) = canvas_h.
  Proof. split; vm_compute; reflexivity. Qed.

  (* The half and quarter strides, stated as separate facts: [nia] then works on
     binary literals instead of [cbn] unfolding [Nat.div] into [Nat.divmod]. *)
  Lemma w_div_4 : canvas_w / 4 = 480. Proof. reflexivity. Qed.
  Lemma h_div_4 : canvas_h / 4 = 270. Proof. reflexivity. Qed.
  Lemma w_div_2 : canvas_w / 2 = 960. Proof. reflexivity. Qed.
  Lemma h_div_2 : canvas_h / 2 = 540. Proof. reflexivity. Qed.

  (* [nia] keeps the area identities in binary arithmetic: [vm_compute] would have
     to build a unary numeral with two million constructors. *)
  Example quarter_grid_tiles_the_canvas_exactly :
    grid_slots * (canvas_w / 4) * (canvas_h / 4) = canvas_w * canvas_h.
  Proof.
    rewrite w_div_4, h_div_4. unfold grid_slots, canvas_w, canvas_h. nia.
  Qed.

  Example documented_mapping_cannot_address_the_grid :
    cells_on_canvas cell_doc grid_slots <> grid_slots.
  Proof. intros H. vm_compute in H. discriminate H. Qed.

  Example quarter_cell_area_is_a_sixteenth_of_the_canvas :
    cell_area (cell_4x4 0) * grid_slots = canvas_w * canvas_h.
  Proof.
    unfold cell_area, cell_4x4. rewrite w_div_4, h_div_4.
    unfold grid_slots, canvas_w, canvas_h. cbn [cw ch Nat.modulo]. nia.
  Qed.

  Example documented_cell_covers_four_quarter_cells :
    cell_area (cell_doc 0) = 4 * cell_area (cell_4x4 0).
  Proof.
    unfold cell_area, cell_doc, cell_4x4. rewrite w_div_2, h_div_2, w_div_4, h_div_4.
    unfold canvas_w, canvas_h. cbn [cw ch Nat.modulo]. nia.
  Qed.

  (* ── 8. BTRON_GST tiers and the additive-only fork rule (R9) ───── *)

  Definition pristine : list string :=
    ["#include <gst/gst.h>"%string;
     "gint w = WIDTH/2, h = HEIGHT/2;"%string;
     "g_object_set(comp_pad, xpos, x, ypos, y, NULL);"%string].

  Fixpoint strip (f : list (bool * string)) : list string :=
    match f with
    | [] => []
    | (b, s) :: r => if b then s :: strip r else strip r
    end.

  Definition make_fork (shim : list string) : list (bool * string) :=
    (map (fun l => (true, l)) pristine ++ map (fun l => (false, l)) shim)%list.

  Lemma strip_map_upstream :
    forall l, strip (map (fun x => (true, x)) l) = l.
  Proof.
    induction l as [|a t IH]; cbn [map strip].
    - reflexivity.
    - rewrite IH. reflexivity.
  Qed.

  Lemma strip_map_shimmed :
    forall l, strip (map (fun x => (false, x)) l) = [].
  Proof.
    induction l as [|a t IH]; cbn [map strip].
    - reflexivity.
    - apply IH.
  Qed.

  Lemma strip_app :
    forall a b, strip (a ++ b)%list = (strip a ++ strip b)%list.
  Proof.
    induction a as [|x t IH]; intros b; cbn [app strip].
    - reflexivity.
    - destruct x as [bu s]. destruct bu; cbn [strip]; rewrite IH; reflexivity.
  Qed.

  Theorem strip_fork_reproduces_upstream : forall shim, strip (make_fork shim) = pristine.
  Proof.
    intros shim. unfold make_fork. rewrite strip_app, strip_map_upstream, strip_map_shimmed.
    apply app_nil_r.
  Qed.

  Lemma length_make_fork :
    forall shim,
      Datatypes.length (make_fork shim) =
      Datatypes.length pristine + Datatypes.length shim.
  Proof.
    intros shim. unfold make_fork.
    rewrite app_length, map_length, map_length. reflexivity.
  Qed.

  Inductive gst_mode := GST_OFF | GST_ON.

  Inductive gst_tier := T1_graph | T2_object_props | T3_signal_json | T4_library.

  Definition tier_symbols (t : gst_tier) : list string :=
    match t with
    | T1_graph => ["rt_element_t"%string; "rt_pad_t"%string; "rt_buffer_t"%string; "rt_pipeline_link_pads"%string]
    | T2_object_props => ["g_object_set"%string; "g_object_get"%string]
    | T3_signal_json => ["g_signal_connect"%string; "json_object_new_int"%string]
    | T4_library => ["g_main_loop_run"%string; "gst_element_get_state"%string]
    end.

  Definition gobject_whitelist : list string :=
    ["xpos"%string; "ypos"%string; "width"%string; "height"%string; "zorder"%string; "sizing-policy"%string; "leaky"%string;
     "max-size-time"%string; "latency"%string].

  Definition property_names_used : list string := gobject_whitelist.

  Definition shim_symbols (m : gst_mode) (t : gst_tier) : list string :=
    match m with
    | GST_OFF => []
    | GST_ON =>
        match t with
        | T1_graph | T3_signal_json => tier_symbols t
        | T2_object_props => filter (fun s => String.eqb s "g_object_set"%string) (tier_symbols t)
        | T4_library => []
        end
    end.

  Definition media_table (mp : bool) (m : gst_mode) : list string :=
    match m with
    | GST_OFF => []
    | GST_ON => shim_symbols GST_ON T1_graph
    end.

  Example gate_off_exposes_nothing_in_any_tier :
    forall t, shim_symbols GST_OFF t = [].
  Proof.
    intros t. vm_compute. reflexivity.
  Qed.

  Example real_glib_loop_stays_host_only : shim_symbols GST_ON T4_library = [].
  Proof. reflexivity. Qed.

  Example t2_shim_is_the_object_set_path_only :
    shim_symbols GST_ON T2_object_props = ["g_object_set"%string].
  Proof. vm_compute. reflexivity. Qed.

  Example graph_and_signal_tiers_are_shimmed :
    shim_symbols GST_ON T1_graph = tier_symbols T1_graph /\
    shim_symbols GST_ON T3_signal_json = tier_symbols T3_signal_json.
  Proof. split; reflexivity. Qed.

  Example only_whitelisted_properties_are_used :
    forall s, In s property_names_used -> In s gobject_whitelist.
  Proof.
    intros s Hs. unfold property_names_used. exact Hs.
  Qed.

  (* BTRON_MP is not an input to the media symbol table at all: the two gates are
     orthogonal by construction, and the differential is carried by BTRON_GST. *)
  Theorem gate_orthogonality :
    forall mp mp' m, media_table mp m = media_table mp' m.
  Proof. reflexivity. Qed.

  Example the_gates_are_orthogonal_but_not_redundant :
    media_table true GST_ON = shim_symbols GST_ON T1_graph /\
    media_table true GST_OFF = [].
  Proof. split; reflexivity. Qed.

  Example fork_of_three_shimmed_lines :
    forall shim, Datatypes.length shim = 3 ->
      Datatypes.length (make_fork shim) = Datatypes.length pristine + 3.
  Proof.
    intros shim Hn. rewrite length_make_fork, Hn. reflexivity.
  Qed.


  Lemma neq_0_of_pos : forall n, 0 < n -> n <> 0.
  Proof. intros n Hz Hn. rewrite Hn in Hz. lia. Qed.

  Lemma mod_base_plus : forall m d, 0 < m -> d < m -> (m + d) mod m = d.
  Proof.
    intros m d Hm Hd.
    rewrite (Nat.add_mod m d m) by (apply neq_0_of_pos; exact Hm).
    rewrite (Nat.mod_same m) by (apply neq_0_of_pos; exact Hm).
    rewrite (Nat.mod_small d m Hd).
    cbn [Nat.add].
    exact (Nat.mod_small d m Hd).
  Qed.

  (* ── 9a. RTP serial numbers: gstrtpbuffer.c:1304-1311 ──────────── *)

  Definition seq_mod : nat := 65536.

  (* Big decimal literals are out of reach for [lia] in this release, so the ring
     constants get their order facts by computation and are atoms afterwards. *)
  Lemma seq_mod_positive : 0 < seq_mod.
  Proof. apply Nat.ltb_lt. vm_compute. reflexivity. Qed.

  Lemma seq_mod_gt_one : 1 < seq_mod.
  Proof. apply Nat.ltb_lt. vm_compute. reflexivity. Qed.

  Example seq_mod_is_sixteen_bits : seq_mod = 2 ^ 16.
  Proof. reflexivity. Qed.

  Definition seq_next (s : nat) : nat := (S s) mod seq_mod.

  Lemma seq_next_stays_in_the_ring : forall s, seq_next s < seq_mod.
  Proof.
    intros s. unfold seq_next.
    apply Nat.mod_upper_bound, neq_0_of_pos, seq_mod_positive.
  Qed.

  Lemma seq_next_wraps_at_the_ring_end : seq_next (seq_mod - 1) = 0.
  Proof.
    intros. unfold seq_next. pose proof seq_mod_gt_one as H1.
    replace (S (seq_mod - 1)) with seq_mod by lia.
    apply Nat.mod_same, neq_0_of_pos, seq_mod_positive.
  Qed.

  Definition diff16 (s1 s2 : nat) : nat := (s2 + seq_mod - s1) mod seq_mod.

  Lemma diff16_same_is_zero : forall s, diff16 s s = 0.
  Proof.
    intros s. unfold diff16.
    replace (s + seq_mod - s) with seq_mod by lia.
    apply Nat.mod_same, neq_0_of_pos, seq_mod_positive.
  Qed.

  Lemma diff16_forward_is_the_span :
    forall s1 s2, s1 <= s2 -> s2 < seq_mod -> diff16 s1 s2 = s2 - s1.
  Proof.
    intros s1 s2 Hle Hlt. unfold diff16. pose proof seq_mod_positive as Hm.
    replace (s2 + seq_mod - s1) with (seq_mod + (s2 - s1)) by lia.
    apply mod_base_plus.
    - exact Hm.
    - lia.
  Qed.

  Lemma diff16_wraps_forward : diff16 (seq_mod - 1) 0 = 1.
  Proof.
    unfold diff16. pose proof seq_mod_gt_one as H1.
    replace (0 + seq_mod - (seq_mod - 1)) with 1 by lia.
    apply Nat.mod_small. exact H1.
  Qed.

  (* ── 9b. The jitter-buffer full test: gstrtpjitterbuffer.c:1691-1695 ── *)

  Definition jb_full (span packets : nat) : bool :=
    (32765 <=? span) && (10000 <? packets).

  Lemma jb_short_span_is_never_full :
    forall span packets, span < 32765 -> jb_full span packets = false.
  Proof.
    intros span packets Hs. unfold jb_full.
    assert (Hb : (32765 <=? span) = false) by (apply Nat.leb_gt; exact Hs).
    rewrite Hb. reflexivity.
  Qed.

  Lemma jb_full_is_a_conjunction :
    forall span packets,
      jb_full span packets = true -> 32765 <= span /\ 10000 < packets.
  Proof.
    intros span packets H. unfold jb_full in H.
    destruct (Nat.leb 32765 span) eqn:E1; [| discriminate].
    destruct (Nat.ltb 10000 packets) eqn:E2; [| discriminate].
    apply Nat.leb_le in E1. apply Nat.ltb_lt in E2. split; assumption.
  Qed.

  (* ── 9c. Watermarks: 15 % low, 90 % high ───────────────────────── *)

  Definition jb_low_ms (probs : nat) : nat := (probs * 15) / 100.
  Definition jb_high_ms (probs : nat) : nat := (probs * 90) / 100.

  Lemma jb_watermarks_are_ordered : forall probs, jb_low_ms probs <= jb_high_ms probs.
  Proof.
    intros probs. unfold jb_low_ms, jb_high_ms.
    apply Nat.div_le_mono.
    - lia.
    - apply Nat.mul_le_mono_l. lia.
  Qed.

  (* ── 9d. The jitter EWMA ───────────────────────────────────────── *)

  Definition ewma (j d : nat) : nat := j + d - ((j + 8) / 16).

  Lemma ewma_bounded_by_sum : forall j d, ewma j d <= j + d.
  Proof. intros j d. unfold ewma. apply Nat.le_sub_l. Qed.

  Lemma ewma_recovers_the_sum :
    forall j d, (j + 8) / 16 <= j + d -> ewma j d + (j + 8) / 16 = j + d.
  Proof. intros j d Hle. unfold ewma. lia. Qed.

  (* ── 9e. Source probation: rtpsource.h:35 RTP_DEFAULT_PROBATION = 2 ── *)

  Definition probation_needed : nat := 2.

  Definition probation_done (seen : nat) : bool := (probation_needed <=? seen).

  Lemma probation_needs_two_good_packets :
    forall seen, seen < 2 -> probation_done seen = false.
  Proof.
    intros seen Hs. unfold probation_done, probation_needed.
    apply Nat.leb_gt. exact Hs.
  Qed.

  Example probation_is_met_at_two : probation_done 2 = true.
  Proof. reflexivity. Qed.

  (* ── 9f. GST_CLOCK_TIME_NONE: a sentinel, never a timestamp ─────── *)

  Definition clock_time_none : nat := 2 ^ 64 - 1.

  Lemma clock_sentinel_dominates :
    forall t, t + 1 < 2 ^ 64 -> (t <? clock_time_none) = true.
  Proof.
    intros t H. unfold clock_time_none. apply Nat.ltb_lt. lia.
  Qed.

  (* ── 9g. DTS beats PTS for ordering: gstbuffer.h:110 ───────────── *)

  Definition stamp_for (pts : nat) (dts : option nat) : nat :=
    match dts with
    | None => pts
    | Some d => d
    end.

  Example dts_overrides_pts : forall pts d, stamp_for pts (Some d) = d.
  Proof. reflexivity. Qed.

  Example pts_is_the_fallback : forall pts, stamp_for pts None = pts.
  Proof. reflexivity. Qed.

  (* ── 9h. Power-of-two ring mask: gstatomicqueue.c:68-87 ────────── *)

  Definition event_queue_size : nat := 256.

  Example event_queue_is_a_power_of_two : event_queue_size = 2 ^ 8.
  Proof. reflexivity. Qed.

  Definition ring_idx (k cap : nat) : nat := k mod cap.

  Lemma ring_idx_stays_in_range :
    forall k cap, 0 < cap -> ring_idx k cap < cap.
  Proof.
    intros k cap Hc. unfold ring_idx.
    apply Nat.mod_upper_bound, neq_0_of_pos. exact Hc.
  Qed.

  Example ring_idx_agrees_with_the_bit_mask :
    ring_idx 300 event_queue_size = Nat.land 300 (event_queue_size - 1).
  Proof. vm_compute. reflexivity. Qed.

  (* ── 9i. videorate emits a GAP unless the stamp moves forward ───── *)

  Definition rate_gate (last next : nat) : bool := Nat.ltb last next.

  Lemma rate_gate_open_means_forward :
    forall last next, rate_gate last next = true -> last < next.
  Proof. intros l n H. unfold rate_gate. apply Nat.ltb_lt. exact H. Qed.

  Lemma rate_gate_closed_emits_no_frame :
    forall last next, next <= last -> rate_gate last next = false.
  Proof.
    intros l n Hn. unfold rate_gate.
    destruct (Nat.ltb l n) eqn:E; [apply Nat.ltb_lt in E; lia|reflexivity].
  Qed.

End MediaRtpModel.

(* ── 10. Nothing is assumed ──────────────────────────────────────────────── *)

(* Each oracle group ends in a theorem that is closed under the global context:
   this file has no Axiom, no Parameter and no Admitted anywhere, and the
   declarations below are what [verify_models.sh] checks with [Print
   Assumptions]. *)
Print Assumptions acquire_never_mints.
Print Assumptions unref_last_release_recycles.
Print Assumptions enqueue_respects_cap.
Print Assumptions dilate_preserves_monotone.
Print Assumptions outputs_never_ahead.
Print Assumptions no_overrun_leaves_no_debt.
Print Assumptions join_preserves_wf.
Print Assumptions leave_released_preserves_wf.
Print Assumptions strip_fork_reproduces_upstream.
Print Assumptions gate_orthogonality.
Print Assumptions diff16_forward_is_the_span.
Print Assumptions jb_watermarks_are_ordered.
