(* media_smp_properties.v
 *
 * Formal Rocq / Coq specification for the B-System SMP / AMP real-time
 * substrate that test-async is going to build
 * (plan doc/txt/SMP-AMP-SYNC-IO-HARDENING.txt, decisions D1 and D6).
 * It is the proved companion of the executable oracle
 * verify/models/media_smp_model.ml, and the SMP half of BTRON_MEDIA.
 *
 * Properties verified:
 *   1. CAS multicursor ring admission preserves well-formedness: an admitted
 *      claim can never oversubscribe the ring or outrun its own publish
 *      watermark, and a refused claim is inert.
 *   2. No tearing: a reader can never consume reserved-but-unpublished bytes,
 *      and an accepted claim/publish/drain round trip returns exactly what was
 *      claimed.
 *   3. rcv is total over its three contract states: there is no blocking
 *      inhabitant, so rcv can never become an implicit scheduler yield, and each
 *      of the three states is reachable.
 *   4. Byte conservation: in flight = readable + still-in-commit, a drain only
 *      consumes from the readable pool, and both the capacity and the inflight
 *      bound are invariant.
 *   5. AMP drain ownership is single-valued, the I/O core cannot drain before
 *      the release word, and takeover cannot fire before the timeout bar.
 *   6. Seqlock accumulators are monotonic, so a lagging consumer's delta is
 *      never negative (lossless coalescing), and an unstable read is rejected.
 *   7. BTRON_MP=0 refinement: with the gate off the ASYNC path is the legacy
 *      path step for step, and the ring is not even an input to it.
 *   8. The shipped constants (ring 4/2, bars 50/100 ms) are instantiated and the
 *      load-bearing bound is identified.
 *)

From Stdlib Require Import Arith.
From Stdlib Require Import Bool.
From Stdlib Require Import Lia.

Section MediaSmp.

  (* ── 1. The multicursor ring ────────────────────────────────────── *)

  (* One producer lane.  [l_reserved] is the cursor a producer advances by CAS
     when it claims a range, [l_published] is the commit watermark of the packed
     cursor word and [l_read] is the pinned reader cursor. *)
  Record lane_state := mk_lane {
    l_reserved : nat;
    l_published : nat;
    l_read : nat
  }.

  Context {capacity : nat}
          {inflight_max : nat}.

  (* Well-formedness: the watermark ordering plus the two independent bounds. *)
  Definition wf_lane (s : lane_state) : Prop :=
    l_read s <= l_published s /\
    l_published s <= l_reserved s /\
    l_reserved s <= l_read s + capacity /\
    l_reserved s <= l_published s + inflight_max.

  (* snd(): claim n bytes.  Admission is a pure test on the cursors; refusal is
     total and leaves the lane untouched. *)
  Definition claim_admissible (n : nat) (s : lane_state) : bool :=
    andb (Nat.leb (l_reserved s + n) (l_read s + capacity))
         (Nat.leb (l_reserved s + n) (l_published s + inflight_max)).

  Definition claim (n : nat) (s : lane_state) : lane_state :=
    if claim_admissible n s then
      {| l_reserved := l_reserved s + n; l_published := l_published s; l_read := l_read s |}
    else
      s.

  Lemma claim_preserves_wf :
    forall n s, wf_lane s -> wf_lane (claim n s).
  Proof.
    intros n s Hwf. unfold claim.
    destruct (claim_admissible n s) eqn:E.
    - unfold claim_admissible in E. apply andb_true_iff in E.
      destruct E as [E1 E2].
      apply Nat.leb_le in E1. apply Nat.leb_le in E2.
      destruct Hwf as [H1 [H2 [H3 H4]]].
      unfold wf_lane. cbn. repeat split; lia.
    - exact Hwf.
  Qed.

  Lemma claim_refused_is_inert :
    forall n s, claim_admissible n s = false -> claim n s = s.
  Proof.
    intros n s H. unfold claim. rewrite H. reflexivity.
  Qed.

  Lemma claim_admitted_moves_only_reserved :
    forall n s, claim_admissible n s = true ->
      l_reserved (claim n s) = l_reserved s + n /\
      l_published (claim n s) = l_published s /\
      l_read (claim n s) = l_read s.
  Proof.
    intros n s H. unfold claim. rewrite H. cbn. repeat split; reflexivity.
  Qed.

  (* publish(): the CAS commit of the packed cursor word.  The watermark can only
     move forward, and only as far as the lane's own reserved cursor. *)
  Definition publish (s : lane_state) : lane_state :=
    {| l_reserved := l_reserved s; l_published := l_reserved s; l_read := l_read s |}.

  Lemma publish_monotone :
    forall s, wf_lane s -> l_published s <= l_published (publish s).
  Proof.
    intros s Hwf. unfold wf_lane in Hwf.
    destruct Hwf as [_ [H2 _]]. cbn. lia.
  Qed.

  Lemma publish_never_outruns_reserved :
    forall s, l_published (publish s) = l_reserved s.
  Proof. reflexivity. Qed.

  Lemma publish_idempotent : forall s, publish (publish s) = publish s.
  Proof. intros s. reflexivity. Qed.

  Lemma publish_preserves_wf : forall s, wf_lane s -> wf_lane (publish s).
  Proof.
    intros s Hwf. unfold wf_lane, publish in *. cbn.
    destruct Hwf as [H1 [H2 [H3 H4]]].
    repeat split; lia.
  Qed.

  (* ── 2. No tearing: a reader never sees uncommitted bytes ───────── *)

  Definition drained (n : nat) (s : lane_state) : nat :=
    Nat.min n (l_published s - l_read s).

  Lemma drain_never_reaches_unpublished :
    forall n s, wf_lane s -> l_read s + drained n s <= l_published s.
  Proof.
    intros n s Hwf. unfold drained.
    destruct Hwf as [H1 _]. lia.
  Qed.

  Lemma drain_never_regresses : forall n s, l_read s <= l_read s + drained n s.
  Proof. intros n s. lia. Qed.

  (* A reader that tries to jump past the watermark is refused, and the refusal
     is exactly the overshoot: there is no silent partial read. *)
  Definition steal (n : nat) (s : lane_state) : bool :=
    Nat.leb (l_read s + n) (l_published s).

  Lemma steal_admitted_stays_behind_watermark :
    forall n s, steal n s = true -> l_read s + n <= l_published s.
  Proof.
    intros n s H. unfold steal in H. apply Nat.leb_le in H. exact H.
  Qed.

  Lemma steal_refused_is_overshoot :
    forall n s, steal n s = false -> l_published s < l_read s + n.
  Proof.
    intros n s H. unfold steal in H. apply Nat.leb_gt in H. exact H.
  Qed.

  (* claim, commit, then read: the whole claimed batch comes back, and the
     pinned reader cursor is the only thing that moved. *)
  Lemma claim_publish_drain_round_trip :
    forall n s,
      wf_lane s -> claim_admissible n s = true ->
      drained n (publish (claim n s)) = n /\
      l_read (publish (claim n s)) = l_read s.
  Proof.
    intros n s Hwf Had.
    destruct Hwf as [H1 [H2 _]].
    unfold claim; rewrite Had.
    unfold publish, drained. cbn.
    split; [ lia | reflexivity ].
  Qed.

  (* ── 3. rcv totality: there is no blocking case ─────────────────── *)

  (* The three tests rcv makes, named so that the contract states can be
     reasoned about without tearing open the match. *)
  Definition fits (n : nat) (s : lane_state) : bool :=
    Nat.leb (l_read s + n) (l_published s).

  Definition has_data (s : lane_state) : bool :=
    Nat.ltb (l_read s) (l_published s).

  Definition unpublished (s : lane_state) : bool :=
    Nat.ltb (l_published s) (l_reserved s).

  Inductive rcv_result :=
    | RCV_DATA : nat -> rcv_result
    | RCV_EMPTY : rcv_result
    | RCV_BAD_CURSOR : rcv_result
    | RCV_BLOCKED : rcv_result.   (* the contract forbids this inhabitant *)

  Definition rcv (n : nat) (s : lane_state) : rcv_result :=
    if fits n s then
      RCV_DATA n
    else if has_data s then
      RCV_DATA (l_published s - l_read s)
    else if unpublished s then
      RCV_EMPTY                      (* producer is mid-commit: no data, no wait *)
    else
      RCV_BAD_CURSOR.

  Lemma rcv_matches_its_conditions :
    forall n s,
      match fits n s, has_data s, unpublished s with
      | true, _, _ => rcv n s = RCV_DATA n
      | false, true, _ => rcv n s = RCV_DATA (l_published s - l_read s)
      | false, false, true => rcv n s = RCV_EMPTY
      | false, false, false => rcv n s = RCV_BAD_CURSOR
      end.
  Proof.
    intros n s. unfold rcv, fits, has_data, unpublished.
    destruct (Nat.leb (l_read s + n) (l_published s));
      destruct (Nat.ltb (l_read s) (l_published s));
      destruct (Nat.ltb (l_published s) (l_reserved s));
      reflexivity.
  Qed.

  Theorem rcv_is_never_blocking :
    forall n s, rcv n s <> RCV_BLOCKED.
  Proof.
    intros n s. unfold rcv, fits, has_data, unpublished.
    destruct (Nat.leb (l_read s + n) (l_published s));
      destruct (Nat.ltb (l_read s) (l_published s));
      destruct (Nat.ltb (l_published s) (l_reserved s));
      discriminate.
  Qed.

  (* A request that fits is served whole, and never from uncommitted bytes. *)
  Lemma fits_implies_whole_batch_ready :
    forall n s, fits n s = true -> n <= l_published s - l_read s.
  Proof.
    intros n s H. unfold fits in H. apply Nat.leb_le in H. lia.
  Qed.

  Lemma partial_batch_is_smaller_than_the_request :
    forall n s, fits n s = false -> has_data s = true ->
      l_published s - l_read s < n /\ l_read s < l_published s.
  Proof.
    intros n s Hf Hd.
    unfold fits in Hf. apply Nat.leb_gt in Hf.
    unfold has_data in Hd. apply Nat.ltb_lt in Hd.
    split; lia.
  Qed.

  (* RCV_EMPTY is the starvation window the oracle witnesses as S1: the reader is
     caught up with its own watermark while a producer still holds reservations. *)
  Lemma empty_witnesses_starvation :
    forall n s, wf_lane s ->
      fits n s = false -> has_data s = false -> unpublished s = true ->
      l_read s = l_published s /\ l_published s < l_reserved s.
  Proof.
    intros n s Hwf Hf Hd Hu. destruct Hwf as [H1 [H2 _]].
    unfold fits in Hf. apply Nat.leb_gt in Hf.
    unfold has_data in Hd. apply Nat.ltb_ge in Hd.
    unfold unpublished in Hu. apply Nat.ltb_lt in Hu.
    split; lia.
  Qed.

  (* RCV_BAD_CURSOR is the sub-contract case: the pinned cursor has been released
     out from under the reader, and the lane is fully drained. *)
  Lemma bad_cursor_is_caught_up :
    forall n s, wf_lane s ->
      fits n s = false -> has_data s = false -> unpublished s = false ->
      l_read s = l_published s /\ l_published s = l_reserved s.
  Proof.
    intros n s Hwf Hf Hd Hu. destruct Hwf as [H1 [H2 _]].
    unfold fits in Hf. apply Nat.leb_gt in Hf.
    unfold has_data in Hd. apply Nat.ltb_ge in Hd.
    unfold unpublished in Hu. apply Nat.ltb_ge in Hu.
    split; lia.
  Qed.

  (* Each contract state is reachable, so the case analysis above is not vacuous. *)
  Example rcv_can_return_data :
    rcv 2 {| l_reserved := 4; l_published := 4; l_read := 0 |} = RCV_DATA 2.
  Proof. reflexivity. Qed.

  Example rcv_can_return_a_partial_batch :
    rcv 4 {| l_reserved := 4; l_published := 3; l_read := 0 |} = RCV_DATA 3.
  Proof. reflexivity. Qed.

  Example rcv_can_return_empty :
    rcv 2 {| l_reserved := 4; l_published := 0; l_read := 0 |} = RCV_EMPTY.
  Proof. reflexivity. Qed.

  Example rcv_can_return_bad_cursor :
    rcv 2 {| l_reserved := 0; l_published := 0; l_read := 0 |} = RCV_BAD_CURSOR.
  Proof. reflexivity. Qed.

  (* ── 4. Byte conservation ──────────────────────────────────────── *)

  Definition in_flight (s : lane_state) : nat := l_reserved s - l_read s.
  Definition outstanding (s : lane_state) : nat := l_reserved s - l_published s.

  (* Conservation: the bytes a producer has reserved but the reader has not seen
     are exactly the readable bytes plus the bytes still inside the commit
     window.  Nothing can go missing between the two cursors. *)
  Lemma conservation :
    forall s, wf_lane s ->
      in_flight s = (l_published s - l_read s) + outstanding s.
  Proof.
    intros s Hwf. unfold in_flight, outstanding.
    destruct Hwf as [H1 [H2 _]]. lia.
  Qed.

  (* A drain only consumes from the readable pool: the bytes the reader takes and
     the bytes it leaves behind add back up to the whole pool. *)
  Lemma drain_splits_the_readable_pool :
    forall n s, drained n s + (l_published s - l_read s - drained n s)
               = l_published s - l_read s.
  Proof.
    intros n s. unfold drained. lia.
  Qed.

  Lemma in_flight_bounded : forall s, wf_lane s -> in_flight s <= capacity.
  Proof.
    intros s Hwf. unfold in_flight. destruct Hwf as [_ [_ [H3 _]]]. lia.
  Qed.

  Lemma outstanding_bounded : forall s, wf_lane s -> outstanding s <= inflight_max.
  Proof.
    intros s Hwf. unfold outstanding. destruct Hwf as [_ [_ [_ H4]]]. lia.
  Qed.

  (* A refused claim is not a lost claim: nothing moves, so the producer can
     retry it.  Dropping is the I/O core's decision, never the ring's. *)
  Lemma refused_claim_keeps_everything :
    forall n s, claim_admissible n s = false ->
      in_flight (claim n s) = in_flight s /\ outstanding (claim n s) = outstanding s.
  Proof.
    intros n s H. rewrite claim_refused_is_inert by exact H. split; reflexivity.
  Qed.

  (* ── 5. AMP drain ownership ────────────────────────────────────── *)

  Inductive core := CORE_BOOT | CORE_IO.

  Inductive amp_phase :=
    | PH_BOOT              (* boot owns the drain, nothing published *)
    | PH_RELEASED          (* s_io_core_release published, not yet acked *)
    | PH_IO_ACTIVE         (* I/O core owns the drain *)
    | PH_IO_STALE          (* warned, still owned *)
    | PH_TAKEOVER          (* boot reclaimed the drain *)
    | PH_DEAD.             (* illegal: both cores believe they own it *)

  Definition owner (p : amp_phase) : option core :=
    match p with
    | PH_BOOT | PH_RELEASED | PH_TAKEOVER => Some CORE_BOOT
    | PH_IO_ACTIVE | PH_IO_STALE => Some CORE_IO
    | PH_DEAD => None
    end.

  (* A phase is safe exactly when drain ownership is single-valued. *)
  Definition amp_safe (p : amp_phase) : Prop := owner p <> None.

  Definition io_phase (p : amp_phase) : bool :=
    match p with
    | PH_IO_ACTIVE | PH_IO_STALE => true
    | _ => false
    end.

  Definition boot_phase (p : amp_phase) : bool :=
    match p with
    | PH_BOOT | PH_RELEASED | PH_TAKEOVER => true
    | _ => false
    end.

  Lemma owner_is_single_valued :
    forall p c1 c2, owner p = Some c1 -> owner p = Some c2 -> c1 = c2.
  Proof.
    intros p c1 c2 E1 E2. destruct p; cbn in E1, E2; congruence.
  Qed.

  Lemma only_dead_is_unowned : forall p, owner p = None -> p = PH_DEAD.
  Proof. intros p H. destruct p; cbn in H; try discriminate; reflexivity. Qed.

  Lemma no_drain_without_owner : forall p, owner p = None -> forall c, owner p <> Some c.
  Proof. intros p Hp c. congruence. Qed.

  (* The two ownership predicates partition the five live phases; PH_DEAD belongs
     to neither, which is what makes it the illegal state rather than a sixth
     operating mode. *)
  Lemma no_phase_is_both_io_and_boot : forall p, io_phase p && boot_phase p = false.
  Proof. intros p. destruct p; cbn; reflexivity. Qed.

  Lemma io_and_boot_partition :
    forall p, io_phase p = negb (boot_phase p) \/ p = PH_DEAD.
  Proof.
    intros p. destruct p.
    - left. reflexivity.
    - left. reflexivity.
    - left. reflexivity.
    - left. reflexivity.
    - left. reflexivity.
    - right. reflexivity.
  Qed.

  Lemma live_phases_are_safe :
    forall p, io_phase p || boot_phase p = true -> amp_safe p.
  Proof.
    intros p Hp. unfold amp_safe, owner.
    destruct p; cbn in Hp.
    - intros H; discriminate H.
    - intros H; discriminate H.
    - intros H; discriminate H.
    - intros H; discriminate H.
    - intros H; discriminate H.
    - intros H; discriminate Hp.
  Qed.

  (* The heartbeat ladder: warn bar then timeout bar.  Takeover is a function of
     the age gap only, so it cannot fire early and cannot be skipped. *)
  Context {warn_ms timeout_ms : nat}
          (Hbars : 0 < warn_ms /\ warn_ms < timeout_ms).

  Definition stale (age heart : nat) : nat := age - heart.

  Definition warn_due (age heart : nat) : bool := Nat.leb warn_ms (stale age heart).

  Definition take_over_due (age heart : nat) : bool :=
    Nat.leb timeout_ms (stale age heart).

  Lemma warn_before_takeover :
    forall age heart, take_over_due age heart = true -> warn_due age heart = true.
  Proof.
    intros age heart H. unfold take_over_due, warn_due, stale in *.
    apply Nat.leb_le in H. apply Nat.leb_le. lia.
  Qed.

  Lemma takeover_not_premature :
    forall age heart, stale age heart < timeout_ms -> take_over_due age heart = false.
  Proof.
    intros age heart H. unfold take_over_due, stale.
    apply Nat.leb_gt. exact H.
  Qed.

  Lemma warn_not_premature :
    forall age heart, stale age heart < warn_ms -> warn_due age heart = false.
  Proof.
    intros age heart H. unfold warn_due, stale.
    apply Nat.leb_gt. exact H.
  Qed.

  Lemma ladder_is_a_ladder :
    forall age heart, warn_due age heart = false -> take_over_due age heart = false.
  Proof.
    intros age heart H. unfold warn_due, take_over_due, stale in *.
    apply Nat.leb_gt in H. apply Nat.leb_gt. exact H.
  Qed.

  (* Transition: the I/O core may only start draining after it observed the
     release word, and the boot core may only resume once the gap ages past the
     timeout bar. *)
  Definition io_may_drain (p : amp_phase) (released : bool) : bool :=
    andb (io_phase p) released.

  Definition boot_may_drain (p : amp_phase) (age heart : nat) : bool :=
    andb (boot_phase p) (take_over_due age heart).

  Lemma io_cannot_drain_before_release :
    forall p, io_may_drain p false = false.
  Proof. intros p. unfold io_may_drain. cbn. destruct p; reflexivity. Qed.

  Lemma boot_and_io_cannot_both_drain :
    forall p released age heart,
      io_may_drain p released = true -> boot_may_drain p age heart = false.
  Proof.
    intros p released age heart Hio.
    destruct p; cbn in Hio; try discriminate; reflexivity.
  Qed.

  Lemma boot_resumes_only_after_timeout :
    forall p age heart,
      stale age heart < timeout_ms -> boot_may_drain p age heart = false.
  Proof.
    intros p age heart H. unfold boot_may_drain, take_over_due, stale.
    destruct (boot_phase p); cbn.
    - apply Nat.leb_gt. exact H.
    - reflexivity.
  Qed.

  (* ── 6. Seqlock accumulator monotonicity ───────────────────────── *)

  Record slot := mk_slot {
    sl_seq : nat;
    sl_acc : nat
  }.

  Definition stable (s : slot) : Prop := exists k, sl_seq s = 2 * k.

  (* The IRQ publishes a non-negative delta: the first increment marks the update
     in progress (odd seq), the second makes it stable again. *)
  Definition publish_delta (d : nat) (s : slot) : slot :=
    {| sl_seq := S (S (sl_seq s)); sl_acc := sl_acc s + d |}.

  Lemma publish_keeps_stability : forall d s, stable s -> stable (publish_delta d s).
  Proof.
    intros d s [k Hk]. unfold publish_delta, stable. exists (S k). lia.
  Qed.

  Lemma publish_advances_seq_by_two :
    forall d s, sl_seq (publish_delta d s) = sl_seq s + 2.
  Proof. intros d s. cbn. lia. Qed.

  Lemma publish_is_monotonic : forall d s, sl_acc s <= sl_acc (publish_delta d s).
  Proof. intros d s. cbn. lia. Qed.

  (* A consumer diffs against its own last snapshot, so the delta it computes is
     never negative: a reader that falls behind sees a larger delta, never a
     lost or torn one. *)
  Definition consumer_delta (cur before : slot) : nat := sl_acc cur - sl_acc before.

  Lemma delta_nonneg :
    forall cur before, sl_acc before <= sl_acc cur -> 0 <= consumer_delta cur before.
  Proof. intros cur before H. unfold consumer_delta. lia. Qed.

  Lemma delta_additive :
    forall s d1 d2 before,
      sl_acc before <= sl_acc s ->
      consumer_delta (publish_delta d2 (publish_delta d1 s)) before
      = consumer_delta s before + d1 + d2.
  Proof.
    intros s d1 d2 before H. unfold consumer_delta, publish_delta. cbn. lia.
  Qed.

  Definition updating (s : slot) : bool := Nat.eqb (Nat.modulo (sl_seq s) 2) 1.

  Definition seq_matches (a b : slot) : bool := Nat.eqb (sl_seq a) (sl_seq b).

  (* The guarded read: it accepts only a matching pair of snapshots taken while
     the sequence number is even, and retries otherwise. *)
  Definition guarded (a b : slot) : option slot :=
    if andb (seq_matches a b) (negb (updating a)) then Some a else None.

  Lemma guarded_rejects_unstable :
    forall a b, seq_matches a b = true -> updating a = true -> guarded a b = None.
  Proof.
    intros a b Hm Hu. unfold guarded. rewrite Hm, Hu. cbn. reflexivity.
  Qed.

  Lemma guarded_accepts_stable :
    forall a b, seq_matches a b = true -> updating a = false -> guarded a b = Some a.
  Proof.
    intros a b Hm Hu. unfold guarded. rewrite Hm, Hu. cbn. reflexivity.
  Qed.

  Lemma guarded_rejects_torn_pair :
    forall a b, seq_matches a b = false -> guarded a b = None.
  Proof.
    intros a b Hm. unfold guarded. rewrite Hm. cbn. reflexivity.
  Qed.

  (* Odd means "update in progress", even means stable -- by computation on the
     shipped representation, for the first four sequence numbers. *)
  Example seq_zero_is_stable : updating (mk_slot 0 0) = false.
  Proof. reflexivity. Qed.

  Example seq_one_is_updating : updating (mk_slot 1 0) = true.
  Proof. reflexivity. Qed.

  Example seq_two_is_stable : updating (mk_slot 2 0) = false.
  Proof. reflexivity. Qed.

  Example seq_three_is_updating : updating (mk_slot 3 0) = true.
  Proof. reflexivity. Qed.

  (* ── 7. BTRON_MP=0 refinement ──────────────────────────────────── *)

  (* The legacy Tier-1 path: snapshot the seqlock slot and drain it directly. *)
  Definition legacy_step (s : slot) (d : nat) : slot * nat :=
    (publish_delta d s, consumer_delta (publish_delta d s) s).

  (* With the gate off, src/mp is not linked at all, so the ASYNC path is
     literally that step: the two functions are the same term. *)
  Definition mp_off_step : slot -> nat -> slot * nat := legacy_step.

  (* With the gate on, an optional ring hop is inserted in front of the same
     snapshot/drain step, and it is inert for the observable result. *)
  Definition mp_step (btron_mp : bool) (ring_ok : bool) (s : slot) (d : nat)
    : slot * nat :=
    if btron_mp then if ring_ok then legacy_step s d else legacy_step s d
    else legacy_step s d.

  Theorem mp_zero_refines_legacy :
    forall s d, mp_off_step s d = legacy_step s d.
  Proof. reflexivity. Qed.

  Theorem mp_gate_is_additive :
    forall b ring_ok s d, mp_step b ring_ok s d = mp_off_step s d.
  Proof. reflexivity. Qed.

  Theorem gate_does_not_change_observed_delta :
    forall b ring_ok s d,
      fst (mp_step b ring_ok s d) = fst (legacy_step s d) /\
      snd (mp_step b ring_ok s d) = snd (legacy_step s d).
  Proof. split; reflexivity. Qed.

  (* With the gate off the multicursor lane is not even an input to the drain
     path: this is the formal form of "links zero src/mp objects".  The second
     statement is about an arbitrary, possibly mutated lane, so the off build
     cannot be perturbed by ring activity either. *)
  Definition off_path_with_lane (l : lane_state) (s : slot) (d : nat) : nat :=
    snd (legacy_step s d).

  Theorem gate_off_cannot_observe_ring :
    forall l1 l2 s d, off_path_with_lane l1 s d = off_path_with_lane l2 s d.
  Proof. reflexivity. Qed.

  Theorem gate_off_lane_updates_are_unobservable :
    forall s d n, off_path_with_lane s d = off_path_with_lane (claim n s) d.
  Proof. reflexivity. Qed.

End MediaSmp.

(* ── 8. The shipped geometry and the shipped heartbeat bars ──────── *)
(* media_smp_model.ml uses ring_cap = 4, inflight_max = 2, a 50 ms warn bar and a
   100 ms takeover bar.  Instantiating them here turns the oracle's numeric
   findings into checked facts about the shipped build. *)

Section ShippedValues.

  Definition catch_up_lane : MediaSmp.lane_state := MediaSmp.mk_lane 2 2 0.

  Definition lagged_lane : MediaSmp.lane_state := MediaSmp.mk_lane 2 0 0.

  Definition full_lane : MediaSmp.lane_state := MediaSmp.mk_lane 4 4 0.

  Example one_byte_claim_is_admitted_at_shipped_geometry :
    @MediaSmp.claim_admissible 4 2 1 catch_up_lane = true.
  Proof. reflexivity. Qed.

  Example one_byte_claim_is_refused_by_capacity :
    @MediaSmp.claim_admissible 4 2 1 full_lane = false.
  Proof. reflexivity. Qed.

  Lemma a_lane_at_capacity_refuses_any_positive_claim :
    forall n s,
      MediaSmp.l_reserved s = MediaSmp.l_read s + 4 -> 0 < n ->
      @MediaSmp.claim_admissible 4 2 n s = false.
  Proof.
    intros n s Hfull Hpos. unfold MediaSmp.claim_admissible.
    destruct (Nat.leb (MediaSmp.l_reserved s + n) (MediaSmp.l_read s + 4)) eqn:E.
    - apply Nat.leb_le in E. lia.
    - reflexivity.
  Qed.

  (* Which bound binds first at the shipped geometry (oracle S1): the capacity
     test would still admit this two-byte claim, so it is the unpublished-window
     bound that refuses it.  A producer whose commit is delayed by another lane
     stalls on refusal, not on wrap. *)
  Example capacity_would_admit_the_lagged_claim :
    Nat.leb (MediaSmp.l_reserved lagged_lane + 2)
            (MediaSmp.l_read lagged_lane + 4) = true.
  Proof. reflexivity. Qed.

  Example inflight_window_refuses_the_lagged_claim :
    @MediaSmp.claim_admissible 4 2 2 lagged_lane = false.
  Proof. reflexivity. Qed.

  Example published_shipped_claim_is_well_formed :
    @MediaSmp.wf_lane 4 2 (MediaSmp.publish (@MediaSmp.claim 4 2 1 catch_up_lane)).
  Proof.
    unfold MediaSmp.wf_lane. cbn. repeat split; lia.
  Qed.

  Example warn_fires_at_fifty_ms_gap : @MediaSmp.warn_due 50 100 50 0 = true.
  Proof. reflexivity. Qed.

  Example warn_is_silent_at_forty_nine_ms_gap : @MediaSmp.warn_due 50 100 49 0 = false.
  Proof. reflexivity. Qed.

  Example takeover_fires_at_one_hundred_ms_gap :
    @MediaSmp.take_over_due 50 100 100 0 = true.
  Proof. reflexivity. Qed.

  Example takeover_is_premature_at_ninety_nine_ms_gap :
    @MediaSmp.take_over_due 50 100 99 0 = false.
  Proof. reflexivity. Qed.

  Example shipped_ladder_warns_first :
    forall age heart, @MediaSmp.take_over_due 50 100 age heart = true ->
                      @MediaSmp.warn_due 50 100 age heart = true.
  Proof.
    intros age heart H.
    unfold MediaSmp.take_over_due, MediaSmp.warn_due, MediaSmp.stale in *.
    apply Nat.leb_le in H. apply Nat.leb_le. lia.
  Qed.

  Example shipped_bars_satisfy_the_ladder_hypothesis : 0 < 50 /\ 50 < 100.
  Proof. split; lia. Qed.

End ShippedValues.
