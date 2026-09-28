(* media_rtp_properties.v
 *
 * Formal Rocq / Coq specification and theorems for the B-System media plane
 * (NuStream) that the Audio Stack / WebRTC work sits on.  It is the proved
 * companion of the executable oracle verify/models/media_rtp_model.ml.
 *
 * Properties verified (one group per oracle invariant R1..R9):
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
 *      balanced, the only headroom is the sub-millisecond tail, and a run with
 *      no overrun carries no debt.
 *   7. Peer lifecycle and grid geometry: join cannot exceed the 16 slots,
 *      live resources track peers exactly, teardown restores the session, and
 *      the documented half-cell mapping cannot address a 4x4 grid.
 *   8. BTRON_GST tier scoping: off is the empty table, T4 stays host-only, the
 *      media symbol table is constant along the BTRON_MP axis (orthogonality).
 *   9. The additive-only fork rule: stripping the shimmed lines reproduces the
 *      pristine upstream text and the fork only ever adds lines.
 *
 * No Axiom, no Parameter, no Admitted -- see the Print Assumptions block.
 *)

From Stdlib Require Import Arith.
From Stdlib Require Import List.
From Stdlib Require Import String.
From Stdlib Require Import Lia.

Import ListNotations.

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

  Definition all_ids (p : pool) : list nat := free_ids p ++ active_ids p.

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

  End MediaRtpModel.
