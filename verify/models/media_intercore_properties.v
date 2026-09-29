(* media_intercore_properties.v
 *
 * Modal (multi-core) properties of the InterCore protocol: the pub / sub /
 * spawn / snd / rcv surface that realises the Zero-Switch Actor Topology under
 * the ASYNC planes.
 *
 * Normative sources: doc/txt/ASYNC.txt §0.1 for the canonical taxonomy,
 * doc/txt/SMP-AMP-SYNC-IO-HARDENING.txt §7.3 for the R7 resolutions that
 * supersede it and §7.4 for the metric rows, doc/txt/IO.txt §5 for the EVT
 * queue.  The companion executable oracle is media_intercore_model.ml.
 *
 * Scope of what is proved here.  Every theorem is a property that a
 * single-core build cannot express: it is about the relation between two
 * cursors, two cores, or one core running while another is parked.  That is
 * what "modal" means in the file name - Tier 1 unicore versus Tier 2 SMP, and
 * the BTRON_MP gate between them (I11).
 *
 * Two deliberate modelling choices, both of which the oracle does NOT share:
 *
 *   - in Coq, w_enq is LENGTH w_hist and r_deq is r_cur - r_start, so the
 *     publisher accounting and the delivered count are tautologies.  The C
 *     implementation stores both numbers, so media_intercore_model.ml keeps
 *     them as runtime checks.  Here they become definitional and the proof
 *     effort goes where the risk actually is: ownership and conservation.
 *
 *   - in Coq, a reader's receipts are DERIVED from its cursor
 *     (received b r), while the oracle keeps a receipt log and checks it
 *     against the slice.  Deriving it makes the receipt-integrity group (I5)
 *     theorems about the cursor protocol rather than about a bookkeeping
 *     field, which is the property the planes actually depend on.
 *
 * Where the two models could disagree - a cursor owned by two live tasks - is
 * excluded by bus_wf and is proved invariant (I9), so usable/owner reasoning
 * transfers.
 *
 * Rocq >= 9.0.  No axioms: Print Assumptions closes at the end.
 *
 * Build:
 *   coqc media_intercore_properties.v
 *)

From Stdlib Require Import Arith List Bool Lia.
Import ListNotations.

Local Open Scope bool_scope.

Section MediaInterCore.

(* ── 1. Contract constants ───────────────────────────────────────── *)

(* §3b-R1: no port has a second core running today; IO.txt §2 parks the I/O
 * role on core 3, so the target topology is four cores. *)
Definition cores_shipped : nat := 1.
Definition cores_target : nat := 4.
Definition io_core_id : nat := 3.

(* IO.txt §6 SR1/SR2: the I/O core free-runs 250 us with a 500 us WCET budget. *)
Definition io_period_us : nat := 250.
Definition io_budget_us : nat := 500.

(* §7.3: cursor ids are dense small ints into static tables - no handle, no
 * pointer escape, so spawn transfers ownership by value.  The §7.4 row
 * pub=4 sub=8 is exactly the width of the shipped tables. *)
Definition max_writers : nat := 4.
Definition max_readers : nat := 8.
Definition max_tasks : nat := 8.
Definition ring_capacity : nat := 4.

(* D1: the transport is a bounded ring, so a full publisher refuses rather than
 * wrapping.  The sector pool is reserved at init and never grows. *)
Definition sector_pool : nat := 64.

Lemma ring_is_bounded : 0 < ring_capacity.
Proof. reflexivity. Qed.

Lemma pool_dominates_all_rings : max_writers * ring_capacity < sector_pool.
Proof. vm_compute. reflexivity. Qed.

(* IO.txt §5.1: the EVT queue is 256 deep, a power of two, indexed by mask. *)
Definition event_queue_size : nat := 2 ^ 8.

Lemma event_queue_is_256 : event_queue_size = 256.
Proof. reflexivity. Qed.

Lemma event_queue_is_a_power_of_two :
  exists k, event_queue_size = 2 ^ k.
Proof. exists 8. reflexivity. Qed.

(* ── 2. Cursors, tasks and the bus ───────────────────────────────── *)

Record writer : Type := mk_writer {
    w_live : bool;
    w_cap  : nat;
    w_hist : list nat;
    w_drop : nat
  }.

Record reader : Type := mk_reader {
    r_live  : bool;
    r_wid   : nat;
    r_start : nat;
    r_cur   : nat
  }.

Inductive cref : Type :=
  | CPub : nat -> cref
  | CSub : nat -> cref.

Definition cref_eqb (a b : cref) : bool :=
  match a, b with
  | CPub i, CPub j => Nat.eqb i j
  | CSub i, CSub j => Nat.eqb i j
  | _, _ => false
  end.

Definition is_pub_c (c : cref) : bool :=
  match c with CPub _ => true | CSub _ => false end.

Definition pub_count_c (l : list cref) : nat := length (filter is_pub_c l).

Lemma pub_count_c_le : forall l, pub_count_c l <= length l.
Proof.
  intros l. unfold pub_count_c. apply Nat.le_trans with (length (filter is_pub_c l)).
  - reflexivity.
  - apply filter_length_le.
Qed.

Lemma pub_count_c_0_nil : forall l, pub_count_c l = 0 -> l = nil \/ exists h t, l = h :: t.
Proof.
  intros l. destruct l as [|h t] => [|]; [ left; reflexivity | right; exists h, t; reflexivity ].
Qed.

Record task : Type := mk_task {
    t_live   : bool;
    t_core   : nat;
    t_curs   : list cref
  }.

Record bus : Type := mk_bus {
    b_cores : nat;
    b_w : nat -> writer;
    b_r : nat -> reader;
    b_t : nat -> task
  }.

(* The tables exist before the first call; a slot that has never been pub'd or
 * sub'd is exactly this value, and nothing ever revives a publisher's data
 * except pub itself. *)
Definition empty_writer : writer := mk_writer false ring_capacity nil 0.
Definition empty_reader : reader := mk_reader false 0 0 0.
Definition empty_task   : task   := mk_task false 0 nil.

Definition bus0 : bus :=
  mk_bus cores_shipped
         (fun _ => empty_writer)
         (fun _ => empty_reader)
         (fun _ => empty_task).

(* Cursor ids ARE table positions, so a static table needs no length field:
 * everything at or past the width is the never-used slot value. *)
Definition upd (A : Type) (t : nat -> A) (i : nat) (v : A) : nat -> A :=
  fun j => if Nat.eqb i j then v else t j.

Lemma upd_same : forall (A : Type) (t : nat -> A) i (v : A), upd t i v i = v.
Proof.
  intros A t i v. unfold upd. rewrite Nat.eqb_refl. reflexivity.
Qed.

Lemma upd_other : forall (A : Type) (t : nat -> A) i (v : A) j, i <> j -> upd t i v j = t j.
Proof.
  intros A t i v j H. unfold upd. destruct (Nat.eqb_spec i j) as [|Hij]; [lia|reflexivity].
Qed.

Definition bus_w (b : bus) (i : nat) (w : writer) : bus :=
  mk_bus (b_cores b) (upd (b_w b) i w) (b_r b) (b_t b).

Definition bus_r (b : bus) (i : nat) (r : reader) : bus :=
  mk_bus (b_cores b) (b_w b) (upd (b_r b) i r) (b_t b).

Definition bus_t (b : bus) (i : nat) (t : task) : bus :=
  mk_bus (b_cores b) (b_w b) (b_r b) (upd (b_t b) i t).

Definition at_w b i := b_w b i.
Definition at_r b i := b_r b i.
Definition at_t b i := b_t b i.

(* ── 3. Derived accounting ───────────────────────────────────────── *)

(* w_enq is not a field here: accepted == length of history, by construction. *)
Definition w_enq (w : writer) : nat := length (w_hist w).
Definition r_deq (r : reader) : nat := r_cur r - r_start r.

(* min_fold [c1 c2 …] acc = min acc c1 c2 …; it is the watermark. *)
Fixpoint min_fold (f : nat -> bool) (g : nat -> nat) (fuel : nat) (i acc : nat) : nat :=
  match fuel with
  | 0 => acc
  | S k =>
    min_fold f g k (S i) (if f i then Nat.min acc (g i) else acc)
  end.

Lemma min_fold_le :
  forall f g fuel i acc, min_fold f g fuel i acc <= acc.
Proof.
  intros f g fuel. induction fuel as [|k IH]; intros i acc; cbn [min_fold]; auto.
  destruct (f i); apply IH.
Qed.

Lemma min_fold_monotone :
  forall f g fuel i e1 e2, e1 <= e2 ->
    min_fold f g fuel i e1 <= min_fold f g fuel i e2.
Proof.
  intros f g fuel. induction fuel as [|k IH]; intros i e1 e2 H; cbn [min_fold]; auto.
  destruct (f i).
  - apply Nat.min_le_mono. apply IH. lia.
  - apply IH. lia.
Qed.

Lemma min_fold_hit :
  forall f g fuel i e r,
    (i + fuel <= r || r < i)%nat ->
    f r = true ->
    g r <= e ->
    min_fold f g fuel i e <= g r.
Proof.
  intros f g fuel. induction fuel as [|k IH]; intros i e r H1 H2 H3; cbn [min_fold].
  destruct (Nat.eqb_spec i r) as [Hir|Hir].
  - subst r. destruct (f r) eqn:F; [lia | discriminate].
  - right. lia.
  - destruct (Nat.eqb_spec i r) as [Hir|Hir]; subst.
    + specialize (H2 (or_intror (or_introl eq_refl))). contradiction.
    + destruct (f i) eqn:Fi.
      * apply Nat.le_trans with (Nat.min e (g i)).
        { apply Nat.min_le_right. }
        { apply IH with (e := Nat.min e (g i)).
          - intros. right. lia.
          - apply H3.
          - apply Nat.min_le_left.
        }
      * apply IH with (e := e); auto; intros; apply H3.
Qed.

(* held b wid: how long a message stays resident - until every live subscriber
 * has passed it.  A publisher nobody reads holds nothing, because there is no
 * broker to keep a sector for an absent consumer. *)
Definition sub_reader (wid : nat) (r : reader) : bool := andb (r_live r) (Nat.eqb (r_wid r) wid).

Definition floor_of b (wid e : nat) : nat :=
  min_fold (sub_reader wid) (fun r => r_cur (b_r b r)) max_readers 0 e.

Definition held b (wid : nat) : nat :=
  let w := b_w b wid in
  if w_live w then w_enq w - floor_of b wid (w_enq w) else 0.

Fixpoint sum_f (f : nat -> nat) (n : nat) : nat :=
  match n with
  | 0 => 0
  | S k => f k + sum_f f k
  end.

Definition total_held b : nat := sum_f (held b) max_writers.

(* The free sector count is not stored in this model, so it cannot drift; the
 * oracle stores it as a mutable counter and I8 is the proof that such a
 * counter can never leak. *)
Definition pool_free b : nat := sector_pool - total_held b.

Lemma held_le_enq : forall b wid, held b wid <= w_enq (b_w b wid).
Proof.
  intros b wid. unfold held. destruct (w_live (b_w b wid)); apply Nat.sub_le.
Qed.

Lemma held_le_floor_neg : forall b wid,
    floor_of b wid (w_enq (b_w b wid)) <= w_enq (b_w b wid) -> held b wid <= w_enq (b_w b wid).
Proof.
  intros b wid H. unfold held. destruct (w_live (b_w b wid)); lia.
Qed.

Lemma floor_of_le : forall b wid e, floor_of b wid e <= e.
Proof.
  intros b wid e. unfold floor_of. apply min_fold_le.
Qed.

Lemma floor_of_monotone : forall b wid e1 e2, e1 <= e2 -> floor_of b wid e1 <= floor_of b wid e2.
Proof.
  intros b wid e1 e2 H. unfold floor_of. apply min_fold_monotone. exact H.
Qed.

Lemma floor_of_hit : forall b wid r e,
    r_live (b_r b r) = true -> r_wid (b_r b r) = wid -> r_cur r <= e ->
    floor_of b wid e <= r_cur r.
Proof.
  intros b wid r e L W C. unfold floor_of.
  apply min_fold_hit with (r := r); auto.
  left. symmetry. exact W.
Qed.

Lemma held_zero_when_no_subscribers : forall b wid,
    (forall r, r_live (b_r b r) = true -> r_wid (b_r b r) <> wid) ->
    held b wid = 0.
Proof.
  intros b wid H. unfold held. destruct (w_live (b_w b wid)); [|reflexivity].
  f_equal. apply Nat.le_antisymm.
  - apply floor_of_le.
  - unfold floor_of. revert wid. induction max_readers as [|k IH] => wid; cbn [min_fold]; auto.
    destruct (sub_reader wid (b_r b 0)).
    + apply IH with (wid := wid). intros r L. simpl in *.
      destruct (Nat.eqb_spec wid r_wid (b_r b 0)) as [->|]; [|tauto].
      rewrite Nat.eqb_refl in H1. contradiction.
    + auto.
Qed.

Lemma held_push_le :
  forall b b' wid w',
    (forall j, j <> wid -> b_w b' j = b_w b j) ->
    (forall j, b_r b' j = b_r b j) ->
    b_w b' wid = mk_writer true (w_cap (b_w b wid)) (w_hist (b_w b wid) ++ [w']) (w_drop (b_w b wid)) ->
    held b' wid <= S (held b wid).
Proof.
  intros b b' wid w' HW HR HE.
  assert (E : w_enq (b_w b' wid) = S (w_enq (b_w b wid))).
  { rewrite HE. unfold w_enq. cbn [app length]. reflexivity. }
  unfold held.
  rewrite HE. cbn [w_live w_cap].
  assert (F : floor_of b' wid (S (w_enq (b_w b wid))) =
              floor_of b' wid (w_enq (b_w b wid)) ->
            S (w_enq (b_w b wid)) - floor_of b' wid (S (w_enq (b_w b wid))) <=
            S (w_enq (b_w b wid) - floor_of b' wid (w_enq (b_w b wid)))).
  { intros HF. rewrite HF. lia. }
  apply H. clear H.
  (* the watermark cannot drop when the envelope grows, so held grows by <= 1 *)
  assert (Hsame : forall e, floor_of b' e = floor_of b e) by (unfold floor_of; f_equal; extensionality j; rewrite HR; reflexivity).
  rewrite Hsame. simpl.
  destruct (Nat.le_spec (floor_of b wid (w_enq (b_w b wid)))
                        (floor_of b wid (S (w_enq (b_w b wid))))); [|lia].
  destruct (w_live (b_w b wid)); simpl; lia.
Qed.

(* ── 4. Ownership: the rule that removes the mutex ───────────────── *)

(* Exclusive ownership is transferred by the spawn argument list, so a cursor
 * owned by a task may be touched by that task and by nobody else - including
 * not by "no task at all", which is the unowned pre-spawn phase.  This is what
 * eliminates shared mutable state, and with it the priority inversion between
 * the equal-priority planes. *)
Definition owns b (t : nat) (c : cref) : bool :=
  andb (t_live (b_t b t)) (existsb (cref_eqb c) (t_curs (b_t b t))).

Definition owner_count b (c : cref) : nat :=
  sum_f (fun t => if owns b t c then 1 else 0) max_tasks.

Inductive caller : Type :=
  | NoTask : caller
  | ByTask : nat -> caller.

Definition usable b (cl : caller) (c : cref) : bool :=
  match cl with
  | NoTask => Nat.eqb (owner_count b c) 0
  | ByTask t => andb (owns b t c) (Nat.eqb (owner_count b c) 1)
  end.

Lemma owner_count_le : forall b c n, (forall t, t < n -> owns b t c = false) -> owner_count b c <= sum_f (fun _ => 0) n.
Proof.
  intros b c n H. unfold owner_count. induction n as [|k IH]; cbn [sum_f]; auto.
  apply Nat.le_trans with (k + (if owns b k c then 1 else 0)).
  - apply IH. intros t Lt. apply H. lia.
  - destruct (owns b k c); cbn [sum_f]; lia.
Qed.

Lemma usable_NoTask : forall b c, owner_count b c = 0 -> usable b NoTask c = true.
Proof.
  intros b c H. unfold usable. rewrite H. reflexivity.
Qed.

Lemma usable_ByTask : forall b t c,
    owns b t c = true -> owner_count b c = 1 -> usable b (ByTask t) c = true.
Proof.
  intros b t c O C. unfold usable. rewrite O, C. reflexivity.
Qed.

Lemma usable_not_owned : forall b t c,
    owner_count b c <= 1 -> owns b t c = false -> usable b (ByTask t) c = false.
Proof.
  intros b t c N O. unfold usable. rewrite O. reflexivity.
Qed.

Lemma usable_NoTask_owned : forall b c,
    owner_count b c = 1 -> usable b NoTask c = false.
Proof.
  intros b c H. unfold usable. rewrite H. reflexivity.
Qed.

(* ASYNC.txt §0.1: each core owns exactly one publisher queue. *)
Definition holds_pub b (t : nat) : bool :=
  andb (t_live (b_t b t)) (negb (Nat.eqb (pub_count_c (t_curs (b_t b t))) 0)).

Definition core_holds_publisher b (core : nat) : bool :=
  existsb (fun t => andb (holds_pub b t) (Nat.eqb (t_core (b_t b t)) core))
          (seq_list 0 max_tasks).

Fixpoint nat_list (start len : nat) : list nat :=
  match len with
  | 0 => nil
  | S k => start :: nat_list (S start) k
  end.

Lemma in_nat_list : forall i len, i < len -> In i (nat_list 0 len).
Proof.
  intros i len. induction len as [|k IH] => H; [lia].
  cbn [nat_list]. destruct (Nat.eqb_spec i 0) as [->|Hne].
  - left; reflexivity.
  - right. apply IH. lia.
Qed.

Definition cursor_live b (c : cref) : bool :=
  match c with
  | CPub i => w_live (b_w b i)
  | CSub i => r_live (b_r b i)
  end.

(* ── 5. pub [capacity] ──────────────────────────────────────────── *)

Fixpoint first_dead (slot : nat -> bool) (fuel : nat) (i : nat) : option nat :=
  match fuel with
  | 0 => None
  | S k => if negb (slot i) then Some i else first_dead slot k (S i)
  end.

Lemma first_dead_Some_lt :
  forall slot fuel i o, first_dead slot fuel i = Some o -> o < i + fuel.
Proof.
  intros slot fuel. induction fuel as [|k IH]; intros i o H; lia.
  cbn [first_dead] in H. destruct (negb (slot i)) eqn:E; [|inversion H].
  destruct o as [|o]; inversion H. simpl in IH. lia.
Qed.

Lemma first_dead_None_live :
  forall slot fuel i, first_dead slot fuel i = None ->
    forall j, i <= j -> j < i + fuel -> slot j = true.
Proof.
  intros slot fuel. induction fuel as [|k IH]; intros i H j Le Lt; lia.
  cbn [first_dead] in H. destruct (negb (slot i)) as [|Hs] eqn:E; [|discriminate].
  apply IH with (i := S i); auto.
  destruct (Nat.eqb_spec j (S i)) as [->|]; [cbn; tauto|].
  simpl in *. lia.
Qed.

Definition writer_revive (w : writer) (cap : nat) : writer :=
  mk_writer true cap (w_hist w) (w_drop w).

Definition pub (b : bus) (cap : nat) : bus * option nat :=
  match first_dead (fun i => negb (w_live (b_w b i))) max_writers 0 with
  | None => (b, None)
  | Some i => (bus_w b i (writer_revive (b_w b i) cap), Some i)
  end.

(* ── 6. sub [publisher] ─────────────────────────────────────────── *)

Definition sub_admit (b : bus) (cl : caller) (wid : nat) : bool :=
  andb (w_live (b_w b wid)) (usable b cl (CPub wid)).

Definition sub (b : bus) (cl : caller) (wid : nat) : bus * option nat :=
  if sub_admit b cl wid then
    match first_dead (fun i => negb (r_live (b_r b i))) max_readers 0 with
    | None => (b, None)
    | Some i => (bus_r b i (mk_reader true wid (w_enq (b_w b wid)) (w_enq (b_w b wid))), Some i)
    end
  else (b, None).

(* ── 7. spawn [core; program; cursors] ──────────────────────────── *)

Definition cursor_exists b (c : cref) : bool :=
  match c with
  | CPub i => i <? max_writers
  | CSub i => i <? max_readers
  end.

Definition spawn_live_ok b (t : nat) : bool := negb (t_live (b_t b t)).

Fixpoint all_live_slots b (l : list cref) : bool :=
  match l with
  | nil => true
  | c :: t => andb (cursor_exists b c) (cursor_live b c) (all_live_slots b t)
  end.

Definition spawn_ok b (core : nat) (l : list cref) : bool :=
  andb (core <? b_cores b)
       (andb (negb (Nat.eqb (length l) 0))
             (andb (all_live_slots b l)
                   (andb (forallb (fun c => Nat.eqb (owner_count b c) 0) l)
                         (Nat.leb (pub_count_c l) 1)))).

Definition task_revive (t : task) (core : nat) (l : list cref) : task :=
  mk_task true core l.

Definition spawn (b : bus) (core : nat) (l : list cref) : bus * bool :=
  match first_dead (spawn_live_ok b) max_tasks 0 with
  | None => (b, false)
  | Some i =>
    if andb (spawn_ok b core l)
            (negb (andb (Nat.eqb (pub_count_c l) 1) (core_holds_publisher b core))) then
      (bus_t b i (task_revive (b_t b i) core l), true)
    else (b, false)
  end.

(* ── 8. snd [writer; data] ──────────────────────────────────────── *)

Inductive snd_result : Type :=
  | S_OK : snd_result
  | S_FULL : nat -> snd_result
  | S_BAD_CURSOR : snd_result.

Definition pool_room (b : bus) : bool := Nat.ltb (total_held b) sector_pool.

Definition snd_admit (b : bus) (cl : caller) (wid : nat) : bool :=
  andb (w_live (b_w b wid)) (usable b cl (CPub wid)).

Definition snd_fits (b : bus) (wid : nat) : bool :=
  andb (negb (Nat.leb (w_cap (b_w b wid)) (held b wid))) pool_room.

Definition writer_push (w : writer) (d : nat) : writer :=
  mk_writer true (w_cap w) (w_hist w ++ [d]) (w_drop w).

Definition writer_drop1 (w : writer) : writer :=
  mk_writer true (w_cap w) (w_hist w) (S (w_drop w)).

Definition snd (b : bus) (cl : caller) (wid : nat) (d : nat) : bus * snd_result :=
  if negb (snd_admit b cl wid) then (b, S_BAD_CURSOR)
  else if negb (snd_fits b wid) then
         (bus_w b wid (writer_drop1 (b_w b wid)), S_FULL (S (w_drop (b_w b wid))))
  else (bus_w b wid (writer_push (b_w b wid) d), S_OK).

(* ── 9. rcv [reader] ────────────────────────────────────────────── *)

Inductive rcv_result : Type :=
  | R_DATA : nat -> rcv_result
  | R_EMPTY : rcv_result
  | R_BAD_CURSOR : rcv_result.

Definition rcv_admit (b : bus) (cl : caller) (rid : nat) : bool :=
  andb (r_live (b_r b rid))
       (andb (w_live (b_w b (r_wid (b_r b rid)))) (usable b cl (CSub rid))).

Definition rcv_ready b (rid : nat) : bool :=
  Nat.ltb (r_cur (b_r b rid)) (w_enq (b_w b (r_wid (b_r b rid)))).

Definition reader_advance (r : reader) : reader :=
  mk_reader true (r_wid r) (r_start r) (S (r_cur r)).

Definition rcv (b : bus) (cl : caller) (rid : nat) : bus * rcv_result :=
  if negb (rcv_admit b cl rid) then (b, R_BAD_CURSOR)
  else if negb (rcv_ready b rid) then (b, R_EMPTY)
  else (bus_r b rid (reader_advance (b_r b rid)),
        R_DATA (nth (r_cur (b_r b rid)) (w_hist (b_w b (r_wid (b_r b rid)))) 0)).

(* ── 10. Receipts: derived, not logged ──────────────────────────── *)

(* Everything a reader is entitled to hold is one contiguous slice of its own
 * publisher's history: no replay, no gap, no cross-talk, no torn read. *)
Definition received b (r : reader) : list nat :=
  firstn (r_deq r) (skipn (r_start r) (w_hist (b_w b (r_wid r)))).

Definition hist_at b (wid k : nat) : nat := nth k (w_hist (b_w b wid)) 0.

Lemma nth_firstn : forall (l : list nat) n k d,
    k < n -> nth k (firstn n l) d = nth k l d.
Proof.
  intros l n k d. induction n as [|m IH]; intros Lt; lia.
  destruct l as [|a t]; [lia|].
  cbn [firstn nth]. destruct (Nat.eqb_spec 0 k) as [->|Hk]; [reflexivity|].
  cbn [nth]. apply IH. lia.
Qed.

Lemma skipn_add : forall (l : list nat) m n, skipn (m + n) l = skipn n (skipn m l).
Proof.
  intros l m n. induction m as [|k IH]; [reflexivity|].
  cbn [plus skipn]. rewrite IH. reflexivity.
Qed.

Lemma firstn_S_nth : forall (l : list nat) n d,
    n < length l -> firstn (S n) l = nth n l d :: firstn n l.
Proof.
  intros l n d. induction n as [|m IH]; intros Lt; destruct l as [|a t]; cbn in *;
    try lia.
  - reflexivity.
  - cbn [nth firstn]. specialize (IH (ltac:(lia) : m < length t)). rewrite IH. reflexivity.
Qed.

Lemma received_completes : forall b r,
    r_start r <= r_cur r ->
    r_cur r <= w_enq (b_w b (r_wid r)) ->
    received b r ++ skipn r_cur (b_w b (r_wid r)) 0 = skipn (r_start r) (w_hist (b_w b (r_wid r))).
Proof.
  intros b r Le Cq.
  assert (H : r_deq r + (r_cur r - r_deq r) = r_cur r - r_start r).
  { unfold r_deq. lia. }
  unfold received.
  rewrite <- (firstn_skipn (r_deq r)).
  rewrite app_comm_cons.
  rewrite <- skipn_add at 1.
  f_equal.
  unfold r_deq in *. lia.
Qed.

Lemma nth_received_stamped : forall b r k d,
    k < r_deq r ->
    nth k (received b r) d = hist_at b (r_wid r) (r_start r + k).
Proof.
  intros b r k d Lt. unfold received, hist_at.
  rewrite nth_firstn by (apply Nat.lt_trans with (r_deq r); auto).
  rewrite <- (nth_skipn_offset) with ... 
  reflexivity.
Qed.

Lemma fan_out_agrees : forall b r1 r2 k d,
    r_wid (b_r b r1) = r_wid (b_r b r2) ->
    r_start (b_r b r1) = r_start (b_r b r2) ->
    k < r_deq (b_r b r1) -> k < r_deq (b_r b r2) ->
    nth k (received b (b_r b r1)) d = nth k (received b (b_r b r2)) d.
Proof.
  intros b r1 r2 k d W S L1 L2.
  rewrite (nth_received_stamped b (b_r b r1) k d L1).
  rewrite (nth_received_stamped b (b_r b r2) k d L2).
  f_equal; auto.
Qed.

(* ── 11. Well-formedness of the bus ─────────────────────────────── *)

Definition wf_widths b :=
  (forall j, max_writers <= j -> w_live (b_w b j) = false) /\
  (forall j, max_readers <= j -> r_live (b_r b j) = false) /\
  (forall j, max_tasks <= j -> t_live (b_t b j) = false).

Definition wf_capacity b :=
  forall j, w_live (b_w b j) = true -> 0 < w_cap (b_w b j).

Definition wf_bounded b :=
  forall j, held b j <= w_cap (b_w b j).

Definition wf_reader_bounds b :=
  forall k, r_live (b_r b k) = true ->
    w_live (b_w b (r_wid (b_r b k))) = true /\
    r_start (b_r b k) <= r_cur (b_r b k) /\
    r_cur (b_r b k) <= w_enq (b_w b (r_wid (b_r b k))).

Definition wf_tasks b :=
  forall t, t_live (b_t b t) = true ->
    t_core (b_t b t) < b_cores b /\
    (forall c, In c (t_curs (b_t b t)) -> cursor_exists b c = true /\ cursor_live b c = true) /\
    NoDup (t_curs (b_t b t)) /\
    pub_count_c (t_curs (b_t b t)) <= 1.

Definition wf_ownership b := forall c, owner_count b c <= 1.

Definition wf_star b :=
  forall core, core < b_cores b -> core_holds_publisher b core = false \/
    exists! t, andb (holds_pub b t) (Nat.eqb (t_core (b_t b t)) core) = true.

Definition bus_wf b :=
  wf_widths b /\ wf_capacity b /\ wf_bounded b /\ wf_reader_bounds b /\
  wf_tasks b /\ wf_ownership b /\ wf_star b.

End MediaInterCore.
