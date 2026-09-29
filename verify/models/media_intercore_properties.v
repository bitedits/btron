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
 * the BTRON_MP gate between them (I10).
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
 *     against the slice.  Deriving it makes the receipt-integrity group (I4)
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

Definition cores_shipped : nat := 1.
Definition cores_target : nat := 4.
Definition io_core_id : nat := 3.

Definition io_period_us : nat := 250.
Definition io_budget_us : nat := 500.

Definition max_writers : nat := 4.
Definition max_readers : nat := 8.
Definition max_tasks : nat := 8.
Definition ring_capacity : nat := 4.
Definition sector_pool : nat := 64.

Definition event_queue_size : nat := 2 ^ 8.

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

Record task : Type := mk_task {
    t_live : bool;
    t_core : nat;
    t_curs : list cref
  }.

Record bus : Type := mk_bus {
    b_cores : nat;
    b_w : nat -> writer;
    b_r : nat -> reader;
    b_t : nat -> task
  }.

Definition empty_writer : writer := mk_writer false ring_capacity nil 0.
Definition empty_reader : reader := mk_reader false 0 0 0.
Definition empty_task   : task   := mk_task false 0 nil.

Definition bus0 : bus :=
  mk_bus cores_shipped (fun _ => empty_writer)
                     (fun _ => empty_reader)
                     (fun _ => empty_task).

Definition upd {A : Type} (t : nat -> A) (i : nat) (v : A) : nat -> A :=
  fun j => if Nat.eqb i j then v else t j.

Lemma upd_same : forall (A : Type) (t : nat -> A) i (v : A), @upd A t i v i = v.
Proof. intros A t i v. unfold upd. rewrite Nat.eqb_refl. reflexivity. Qed.

Lemma upd_other : forall (A : Type) (t : nat -> A) i (v : A) j,
    i <> j -> @upd A t i v j = t j.
Proof.
  intros A t i v j H. unfold upd.
  destruct (Nat.eqb_spec i j) as [|Hij]; [contradiction|reflexivity].
Qed.

Definition bus_w (b : bus) (i : nat) (w : writer) : bus :=
  mk_bus (b_cores b) (@upd writer (b_w b) i w) (b_r b) (b_t b).

Definition bus_r (b : bus) (i : nat) (r : reader) : bus :=
  mk_bus (b_cores b) (b_w b) (@upd reader (b_r b) i r) (b_t b).

Definition bus_t (b : bus) (i : nat) (t : task) : bus :=
  mk_bus (b_cores b) (b_w b) (b_r b) (@upd task (b_t b) i t).

Lemma at_w_same_w : forall b i (w:writer), b_w (bus_w b i w) i = w.
Proof. intros b i w. unfold bus_w. apply upd_same. Qed.

Lemma at_w_other_w : forall b i (w:writer) j, i <> j -> b_w (bus_w b i w) j = b_w b j.
Proof. intros b i w j H. unfold bus_w. apply upd_other. exact H. Qed.

Lemma at_r_bus_w : forall b i (w:writer) j, b_r (bus_w b i w) j = b_r b j.
Proof. reflexivity. Qed.

Lemma at_t_bus_w : forall b i (w:writer) j, b_t (bus_w b i w) j = b_t b j.
Proof. reflexivity. Qed.

Lemma at_r_same_r : forall b i (r:reader), b_r (bus_r b i r) i = r.
Proof. intros b i r. unfold bus_r. apply upd_same. Qed.

Lemma at_r_other_r : forall b i (r:reader) j, i <> j -> b_r (bus_r b i r) j = b_r b j.
Proof. intros b i r j H. unfold bus_r. apply upd_other. exact H. Qed.

Lemma at_w_bus_r : forall b i (r:reader) j, b_w (bus_r b i r) j = b_w b j.
Proof. reflexivity. Qed.

Lemma at_t_bus_r : forall b i (r:reader) j, b_t (bus_r b i r) j = b_t b j.
Proof. reflexivity. Qed.

Lemma at_t_same_t : forall b i (t:task), b_t (bus_t b i t) i = t.
Proof. intros b i t. unfold bus_t. apply upd_same. Qed.

Lemma at_t_other_t : forall b i (t:task) j, i <> j -> b_t (bus_t b i t) j = b_t b j.
Proof. intros b i t j H. unfold bus_t. apply upd_other. exact H. Qed.

Lemma at_w_bus_t : forall b i (t:task) j, b_w (bus_t b i t) j = b_w b j.
Proof. reflexivity. Qed.

Lemma at_r_bus_t : forall b i (t:task) j, b_r (bus_t b i t) j = b_r b j.
Proof. reflexivity. Qed.

Lemma cores_inert_w : forall b i (w:writer), b_cores (bus_w b i w) = b_cores b.
Proof. reflexivity. Qed.

Lemma cores_inert_r : forall b i (r:reader), b_cores (bus_r b i r) = b_cores b.
Proof. reflexivity. Qed.

Lemma cores_inert_t : forall b i (t:task), b_cores (bus_t b i t) = b_cores b.
Proof. reflexivity. Qed.

Fixpoint nat_list (len : nat) : list nat :=
  match len with
  | 0 => nil
  | S k => k :: nat_list k
  end.

Lemma in_nat_list : forall i len, i < len -> In i (nat_list len).
Proof.
  intros i len. induction len as [|k IH]; intros Lt; [lia|].
  cbn [nat_list]. destruct (Nat.eqb_spec i k) as [->|Hne].
  - left; reflexivity.
  - right; apply IH; lia.
Qed.

(* ── 3. Derived accounting ───────────────────────────────────────── *)

Definition w_enq (w : writer) : nat := length (w_hist w).
Definition r_deq (r : reader) : nat := r_cur r - r_start r.

Fixpoint sum_f (f : nat -> nat) (n : nat) : nat :=
  match n with
  | 0 => 0
  | S k => f k + sum_f f k
  end.

Lemma sum_f_le : forall (f g : nat -> nat) n,
    (forall j, j < n -> f j <= g j) -> sum_f f n <= sum_f g n.
Proof.
  intros f g n. revert f g. induction n as [|k IH]; intros f g Hn; cbn [sum_f]; [lia|].
  apply Nat.add_le_mono; [apply Hn; lia|apply IH; intros j Lt; apply Hn; lia].
Qed.

Fixpoint min_fold (f : nat -> bool) (g : nat -> nat) (fuel : nat) (i acc : nat) : nat :=
  match fuel with
  | 0 => acc
  | S k => min_fold f g k (S i) (if f i then Nat.min acc (g i) else acc)
  end.

Lemma min_fold_le : forall f g fuel i acc, min_fold f g fuel i acc <= acc.
Proof.
  intros f g fuel. induction fuel as [|k IH]; intros i acc; cbn [min_fold].
  - lia.
  - destruct (f i).
    + apply Nat.le_trans with (Nat.min acc (g i)); [apply IH|lia].
    + apply IH.
Qed.

Lemma min_fold_monotone : forall f g fuel i e1 e2,
    e1 <= e2 -> min_fold f g fuel i e1 <= min_fold f g fuel i e2.
Proof.
  intros f g fuel. induction fuel as [|k IH]; intros i e1 e2 H; cbn [min_fold]; [lia|].
  destruct (f i); apply IH; lia.
Qed.

Lemma min_fold_hit : forall f g fuel i e r,
    i <= r -> r < i + fuel -> f r = true -> min_fold f g fuel i e <= g r.
Proof.
  intros f g fuel. induction fuel as [|k IH]; intros i e r Le Lt F; cbn [min_fold].
  { lia. }
  destruct (Nat.eqb_spec r i) as [Hr|Hr].
  { subst i. apply Nat.le_trans with (if f r then Nat.min e (g r) else e).
    - apply min_fold_le.
    - rewrite F. lia. }
  apply IH with (i := S i); [lia|lia|exact F].
Qed.

Lemma min_fold_skip : forall f g fuel i acc,
    (forall j, i <= j -> j < i + fuel -> f j = false) -> min_fold f g fuel i acc = acc.
Proof.
  intros f g fuel. induction fuel as [|k IH]; intros i acc H; cbn [min_fold]; [reflexivity|].
  destruct (f i) eqn:Fi.
  - assert (Hf : f i = false) by (apply H; lia). rewrite Hf in Fi. discriminate.
  - apply IH; intros j Le Lt; apply H; lia.
Qed.

Definition sub_reader (wid : nat) (r : reader) : bool :=
  andb (r_live r) (Nat.eqb (r_wid r) wid).

Definition floor_of b (wid e : nat) : nat :=
  min_fold (fun r => sub_reader wid (b_r b r)) (fun r => r_cur (b_r b r)) max_readers 0 e.

Definition held b (wid : nat) : nat :=
  if w_live (b_w b wid)
  then w_enq (b_w b wid) - floor_of b wid (w_enq (b_w b wid))
  else 0.

Definition total_held b : nat := sum_f (held b) max_writers.
Definition pool_free b : nat := sector_pool - total_held b.

Lemma floor_of_le : forall b wid e, floor_of b wid e <= e.
Proof. intros b wid e. unfold floor_of. apply min_fold_le. Qed.

Lemma floor_of_monotone : forall b wid e1 e2, e1 <= e2 ->
    floor_of b wid e1 <= floor_of b wid e2.
Proof. intros b wid e1 e2 H. unfold floor_of. apply min_fold_monotone. exact H. Qed.

Lemma floor_of_hit : forall b r e, r < max_readers ->
    r_live (b_r b r) = true -> r_cur (b_r b r) <= e ->
    floor_of b (r_wid (b_r b r)) e <= r_cur (b_r b r).
Proof.
  intros b r e Lt L Le. unfold floor_of.
  apply min_fold_hit with (r := r); [lia|lia|].
  cbn [sub_reader]. apply andb_true_intro. split; [exact L|apply Nat.eqb_refl].
Qed.

Lemma floor_of_w_inert : forall b i (w:writer) wid e,
    floor_of (bus_w b i w) wid e = floor_of b wid e.
Proof. intros b i w wid e. unfold floor_of. reflexivity. Qed.

Lemma held_le_enq : forall b wid, held b wid <= w_enq (b_w b wid).
Proof. intros b wid. unfold held. destruct (w_live (b_w b wid)); lia. Qed.

Lemma held_w_inert : forall b i (w:writer) j, i <> j -> held (bus_w b i w) j = held b j.
Proof. intros b i w j H. unfold held. rewrite at_w_other_w; [reflexivity|exact H]. Qed.

Lemma floor_of_no_subscriber : forall b wid,
    (forall r, r < max_readers -> r_live (b_r b r) = true -> r_wid (b_r b r) <> wid) ->
    forall e, floor_of b wid e = e.
Proof.
  intros b wid Hun e. unfold floor_of. apply min_fold_skip.
  intros j Le Lt. change (sub_reader wid (b_r b j) = false). unfold sub_reader.
  destruct (r_live (b_r b j)) eqn:L;
    destruct (Nat.eqb (r_wid (b_r b j)) wid) eqn:W; cbn [andb]; try reflexivity.
  exfalso. apply (Hun j Lt L). apply (proj1 (Nat.eqb_eq _ _)) in W. exact W.
Qed.

Lemma held_zero_when_unread : forall b wid,
    (forall r, r < max_readers -> r_live (b_r b r) = true -> r_wid (b_r b r) <> wid) ->
    held b wid = 0.
Proof.
  intros b wid Hun. unfold held. destruct (w_live (b_w b wid)); [|reflexivity].
  rewrite (floor_of_no_subscriber b wid Hun). lia.
Qed.

(* ── 4. Ownership: the rule that removes the mutex ───────────────── *)

Definition owns b (t : nat) (c : cref) : bool :=
  andb (t_live (b_t b t)) (existsb (cref_eqb c) (t_curs (b_t b t))).

Inductive caller : Type :=
  | NoTask : caller
  | ByTask : nat -> caller.

Definition cursor_taken b (c : cref) : bool :=
  existsb (fun t => owns b t c) (nat_list max_tasks).

Definition usable b (cl : caller) (c : cref) : bool :=
  match cl with
  | NoTask => negb (cursor_taken b c)
  | ByTask t => owns b t c
  end.

Lemma existsb_true : forall (f : nat -> bool) l x,
    In x l -> f x = true -> existsb f l = true.
Proof.
  intros f l x. induction l as [|a t IH]; intros Hin Hf; cbn [existsb].
  { contradiction. }
  destruct Hin as [Hx|Hin'].
  { subst a. rewrite Hf. reflexivity. }
  destruct (f a); [reflexivity|apply IH; auto].
Qed.

Lemma usable_free : forall b c, (forall t, owns b t c = false) -> usable b NoTask c = true.
Proof.
  intros b c H. unfold usable, cursor_taken.
  destruct (existsb (fun t => owns b t c) (nat_list max_tasks)) eqn:E.
  - apply (proj1 (@existsb_exists nat _ _)) in E. destruct E as [t [Hinl Hot]].
    specialize (H t). rewrite H in Hot. discriminate.
  - reflexivity.
Qed.

Lemma usable_taken : forall b t c, In t (nat_list max_tasks) -> owns b t c = true ->
    usable b NoTask c = false.
Proof.
  intros b t c Hin Ho. unfold usable, cursor_taken.
  assert (H : existsb (fun u => owns b u c) (nat_list max_tasks) = true)
    by (apply existsb_true with (x := t); auto).
  rewrite H. reflexivity.
Qed.

Lemma usable_owner : forall b t c, owns b t c = true -> usable b (ByTask t) c = true.
Proof. intros b t c H. unfold usable. exact H. Qed.

Lemma usable_stranger : forall b t c, owns b t c = false -> usable b (ByTask t) c = false.
Proof. intros b t c H. unfold usable. exact H. Qed.

Lemma owns_w_inert : forall b i (w:writer) t c, owns (bus_w b i w) t c = owns b t c.
Proof. reflexivity. Qed.

Lemma owns_r_inert : forall b i (r:reader) t c, owns (bus_r b i r) t c = owns b t c.
Proof. reflexivity. Qed.

Lemma usable_w_inert : forall b i (w:writer) cl c, usable (bus_w b i w) cl c = usable b cl c.
Proof. intros b i w cl c. reflexivity. Qed.

Lemma usable_r_inert : forall b i (r:reader) cl c, usable (bus_r b i r) cl c = usable b cl c.
Proof. intros b i r cl c. reflexivity. Qed.

Definition holds_pub b (t : nat) : bool :=
  andb (t_live (b_t b t)) (negb (Nat.eqb (pub_count_c (t_curs (b_t b t))) 0)).

Definition core_holds_publisher b (core : nat) : bool :=
  existsb (fun t => andb (holds_pub b t) (Nat.eqb (t_core (b_t b t)) core))
          (nat_list max_tasks).

Definition cursor_exists (c : cref) : bool :=
  match c with
  | CPub i => i <? max_writers
  | CSub i => i <? max_readers
  end.

Definition cursor_live b (c : cref) : bool :=
  match c with
  | CPub i => w_live (b_w b i)
  | CSub i => r_live (b_r b i)
  end.

Fixpoint cursors_live b (l : list cref) : bool :=
  match l with
  | nil => true
  | c :: t => andb (cursor_live b c) (cursors_live b t)
  end.

(* ── 5. The five calls ──────────────────────────────────────────── *)

(* The protocol's send call is named `snd`, which shadows the Stdlib pair
 * projection; `pr2` is that projection under its own name. *)
Definition pr2 {A B : Type} (p : A * B) : B := Datatypes.snd p.


Fixpoint first_dead (slot : nat -> bool) (fuel : nat) (i : nat) : option nat :=
  match fuel with
  | 0 => None
  | S k => if negb (slot i) then Some i else first_dead slot k (S i)
  end.

Lemma first_dead_Some_lt : forall slot fuel i o,
    first_dead slot fuel i = Some o -> o < i + fuel.
Proof.
  intros slot fuel. induction fuel as [|k IH]; intros i o H.
  { cbn [first_dead] in H. discriminate H. }
  cbn [first_dead] in H. destruct (slot i) eqn:Si; cbn [negb] in H.
  - apply IH with (i := S i) in H; lia.
  - inversion H; subst; lia.
Qed.

Lemma first_dead_Some_free : forall slot fuel i o,
    first_dead slot fuel i = Some o -> slot o = false.
Proof.
  intros slot fuel. induction fuel as [|k IH]; intros i o H; cbn [first_dead] in H.
  { discriminate H. }
  destruct (slot i) eqn:Si; cbn [negb] in H.
  - apply IH with (i := S i) in H. exact H.
  - inversion H; subst. exact Si.
Qed.

Lemma first_dead_None_live : forall slot fuel i,
    first_dead slot fuel i = None ->
    forall j, i <= j -> j < i + fuel -> slot j = true.
Proof.
  intros slot fuel. induction fuel as [|k IH]; intros i Hn j Le Lt.
  { lia. }
  cbn [first_dead] in Hn. destruct (slot i) eqn:Si; cbn [negb] in Hn.
  - destruct (Nat.eqb_spec j i) as [Hji|Hji]; [subst j; exact Si|].
    apply IH with (i := S i); [exact Hn|lia|lia].
  - discriminate.
Qed.

Definition writer_revive (w : writer) (cap : nat) : writer :=
  mk_writer true cap (w_hist w) (w_drop w).

Definition writer_push (w : writer) (d : nat) : writer :=
  mk_writer true (w_cap w) (w_hist w ++ [d]) (w_drop w).

Definition writer_drop1 (w : writer) : writer :=
  mk_writer true (w_cap w) (w_hist w) (S (w_drop w)).

Definition reader_advance (r : reader) : reader :=
  mk_reader true (r_wid r) (r_start r) (S (r_cur r)).

Definition task_revive (core : nat) (l : list cref) : task := mk_task true core l.

(* The slot each table-first call would take: None means the static table is
 * full, and a full table refuses rather than allocating. *)
Definition pub_slot (b : bus) : option nat :=
  first_dead (fun i => w_live (b_w b i)) max_writers 0.

Definition sub_slot (b : bus) : option nat :=
  first_dead (fun i => r_live (b_r b i)) max_readers 0.

Definition spawn_slot (b : bus) : option nat :=
  first_dead (fun i => t_live (b_t b i)) max_tasks 0.

Lemma first_dead_none : forall slot fuel i,
    (forall j, i <= j -> j < i + fuel -> slot j = true) -> first_dead slot fuel i = None.
Proof.
  intros slot fuel. induction fuel as [|k IH]; intros i H; cbn [first_dead].
  { reflexivity. }
  destruct (slot i) eqn:Si.
  - cbn [negb]. apply IH. intros j Le Lt; apply H; lia.
  - cbn [negb].
    assert (L : i <= i) by lia. assert (U : i < i + S k) by lia.
    specialize (H i L U). rewrite Si in H. discriminate H.
Qed.

Definition pub (b : bus) (cap : nat) : bus * option nat :=
  match pub_slot b with
  | None => (b, None)
  | Some i => (bus_w b i (writer_revive (b_w b i) cap), Some i)
  end.

Definition sub_admit (b : bus) (cl : caller) (wid : nat) : bool :=
  andb (w_live (b_w b wid)) (usable b cl (CPub wid)).

Definition sub (b : bus) (cl : caller) (wid : nat) : bus * option nat :=
  if sub_admit b cl wid then
    match sub_slot b with
    | None => (b, None)
    | Some i => (bus_r b i (mk_reader true wid (w_enq (b_w b wid)) (w_enq (b_w b wid))), Some i)
    end
  else (b, None).

Definition star_ok b (core : nat) (l : list cref) : bool :=
  if negb (Nat.eqb (pub_count_c l) 0) then negb (core_holds_publisher b core) else true.

Definition spawn_ok b (core : nat) (l : list cref) : bool :=
  andb (core <? b_cores b)
       (andb (negb (Nat.eqb (length l) 0))
             (andb (cursors_live b l)
                   (andb (forallb (fun c => negb (cursor_taken b c)) l)
                         (andb (Nat.leb (pub_count_c l) 1) (star_ok b core l))))).

Definition spawn (b : bus) (core : nat) (l : list cref) : bus * option nat :=
  match spawn_slot b with
  | None => (b, None)
  | Some i => if spawn_ok b core l then (bus_t b i (task_revive core l), Some i) else (b, None)
  end.

Inductive snd_result : Type :=
  | S_OK : snd_result
  | S_FULL : nat -> snd_result
  | S_BAD_CURSOR : snd_result.

Definition pool_room (b : bus) : bool := Nat.ltb (total_held b) sector_pool.

Definition snd_admit (b : bus) (cl : caller) (wid : nat) : bool :=
  andb (w_live (b_w b wid)) (usable b cl (CPub wid)).

Definition snd_fits (b : bus) (wid : nat) : bool :=
  andb (negb (Nat.leb (w_cap (b_w b wid)) (held b wid))) (pool_room b).

Definition snd (b : bus) (cl : caller) (wid : nat) (d : nat) : bus * snd_result :=
  if negb (snd_admit b cl wid) then (b, S_BAD_CURSOR)
  else if negb (snd_fits b wid) then
         (bus_w b wid (writer_drop1 (b_w b wid)), S_FULL (S (w_drop (b_w b wid))))
  else (bus_w b wid (writer_push (b_w b wid) d), S_OK).

Inductive rcv_result : Type :=
  | R_DATA : nat -> rcv_result
  | R_EMPTY : rcv_result
  | R_BAD_CURSOR : rcv_result.

Definition rcv_admit (b : bus) (cl : caller) (rid : nat) : bool :=
  andb (r_live (b_r b rid))
       (andb (w_live (b_w b (r_wid (b_r b rid)))) (usable b cl (CSub rid))).

Definition rcv_ready b (rid : nat) : bool :=
  Nat.ltb (r_cur (b_r b rid)) (w_enq (b_w b (r_wid (b_r b rid)))).

Definition rcv (b : bus) (cl : caller) (rid : nat) : bus * rcv_result :=
  if negb (rcv_admit b cl rid) then (b, R_BAD_CURSOR)
  else if negb (rcv_ready b rid) then (b, R_EMPTY)
  else (bus_r b rid (reader_advance (b_r b rid)),
        R_DATA (nth (r_cur (b_r b rid)) (w_hist (b_w b (r_wid (b_r b rid)))) 0)).

(* ── 6. Receipts: derived, not logged ───────────────────────────── *)

Definition received b (r : reader) : list nat :=
  firstn (r_deq r) (skipn (r_start r) (w_hist (b_w b (r_wid r)))).

Definition hist_at b (wid k : nat) : nat := nth k (w_hist (b_w b wid)) 0.

Lemma nth_firstn : forall (l : list nat) n k d, k < n -> nth k (firstn n l) d = nth k l d.
Proof.
  intros l n k d. revert l k d. induction n as [|m IH]; intros l k d Lt.
  { lia. }
  destruct l as [|a t]; [reflexivity|].
  destruct k as [|k']; [reflexivity|].
  cbn [firstn nth]. apply IH. lia.
Qed.

Lemma firstn_cons : forall n a (t : list nat), firstn (S n) (a :: t) = a :: firstn n t.
Proof. reflexivity. Qed.

Lemma nth_cons_tail : forall n a (t : list nat) d, nth (S n) (a :: t) d = nth n t d.
Proof. reflexivity. Qed.

Lemma firstn_S_app : forall (l : list nat) n d,
    n < length l -> firstn (S n) l = firstn n l ++ [nth n l d].
Proof.
  intros l n d. revert l. induction n as [|m IH]; intros l Lt.
  { destruct l; cbn [length] in Lt; [lia|reflexivity]. }
  destruct l; cbn [length] in Lt; [lia|].
  rewrite !firstn_cons, !nth_cons_tail, IH by lia. reflexivity.
Qed.

Lemma skipn_length : forall (l : list nat) n, length (skipn n l) = length l - n.
Proof.
  intros l n. revert l. induction n as [|k IH]; intros l.
  { cbn [skipn]. lia. }
  cbn [skipn]. destruct l as [|a t]; cbn [length]; [reflexivity|].
  specialize (IH t). lia.
Qed.

(* ── 7. Well-formedness of the bus ──────────────────────────────── *)

Definition wf_tables b :=
  (forall j, max_writers <= j -> b_w b j = empty_writer) /\
  (forall j, max_readers <= j -> b_r b j = empty_reader) /\
  (forall j, max_tasks <= j -> b_t b j = empty_task) /\
  (forall j, 0 < w_cap (b_w b j) /\ w_cap (b_w b j) <= ring_capacity).

Definition wf_bounded b := forall j, held b j <= w_cap (b_w b j).

Definition wf_reader_bounds b :=
  forall k, r_live (b_r b k) = true ->
    k < max_readers /\
    w_live (b_w b (r_wid (b_r b k))) = true /\
    r_start (b_r b k) <= r_cur (b_r b k) /\
    r_cur (b_r b k) <= w_enq (b_w b (r_wid (b_r b k))).

Definition wf_tasks b :=
  forall t, t_live (b_t b t) = true ->
    t < max_tasks /\
    t_core (b_t b t) < b_cores b /\
    (forall c, In c (t_curs (b_t b t)) -> cursor_exists c = true /\ cursor_live b c = true) /\
    NoDup (t_curs (b_t b t)) /\
    pub_count_c (t_curs (b_t b t)) <= 1 /\
    length (t_curs (b_t b t)) <> 0.

Definition wf_ownership b :=
  forall t1 t2 c, owns b t1 c = true -> owns b t2 c = true -> t1 = t2.

Definition wf_star b :=
  forall t1 t2, holds_pub b t1 = true -> holds_pub b t2 = true ->
    t_core (b_t b t1) = t_core (b_t b t2) -> t1 = t2.

Definition bus_wf b :=
  wf_tables b /\ wf_bounded b /\ wf_reader_bounds b /\
  wf_tasks b /\ wf_ownership b /\ wf_star b.

Lemma bus_wf0 : bus_wf bus0.
Proof.
  unfold bus_wf, wf_tables, wf_bounded, wf_reader_bounds, wf_tasks,
         wf_ownership, wf_star, holds_pub, owns, pub_count_c, held, w_enq.
  repeat split; simpl; try lia; try discriminate; auto; vm_compute; auto.
Qed.

(* ── 8. Folds and tables: the general laws the proofs lean on ───── *)

Lemma sum_f_bound : forall (f : nat -> nat) n k,
    (forall j, j < n -> f j <= k) -> sum_f f n <= n * k.
Proof.
  intros f n k. induction n as [|m IH]; intros H; cbn [sum_f].
  - apply Nat.le_0_l.
  - rewrite Nat.mul_succ_l.
    assert (A1 : f m <= k) by (apply H; lia).
    assert (A2 : sum_f f m <= m * k) by (apply IH; intros j Lt; apply H; lia).
    lia.
Qed.

Lemma min_fold_cong : forall f1 g1 f2 g2 fuel i acc,
    (forall j, i <= j -> j < i + fuel -> f1 j = f2 j /\ g1 j = g2 j) ->
    min_fold f1 g1 fuel i acc = min_fold f2 g2 fuel i acc.
Proof.
  intros f1 g1 f2 g2 fuel. induction fuel as [|k IH]; intros i acc H; cbn [min_fold].
  { reflexivity. }
  assert (Hi : f1 i = f2 i /\ g1 i = g2 i) by (apply H; lia).
  destruct Hi as [Hf Hg]. rewrite Hf.
  destruct (f2 i) eqn:F.
  - rewrite Hg. apply IH. intros j Le Lt. apply H; lia.
  - apply IH. intros j Le Lt. apply H; lia.
Qed.

Lemma min_fold_cong_if : forall f1 g1 f2 g2 fuel i acc,
    (forall j, i <= j -> j < i + fuel -> f1 j = f2 j /\ (f2 j = true -> g1 j = g2 j)) ->
    min_fold f1 g1 fuel i acc = min_fold f2 g2 fuel i acc.
Proof.
  intros f1 g1 f2 g2 fuel. induction fuel as [|k IH]; intros i acc H; cbn [min_fold].
  { reflexivity. }
  assert (Hi : f1 i = f2 i /\ (f2 i = true -> g1 i = g2 i)) by (apply H; lia).
  destruct Hi as [Hf Hg]. rewrite Hf.
  destruct (f2 i) eqn:F.
  - rewrite (Hg eq_refl). apply IH. intros j Le Lt. apply H; lia.
  - apply IH. intros j Le Lt. apply H; lia.
Qed.

Lemma min_fold_unconstrained : forall f g fuel i acc,
    (forall j, i <= j -> j < i + fuel -> acc <= g j) -> min_fold f g fuel i acc = acc.
Proof.
  intros f g fuel. induction fuel as [|k IH]; intros i acc H; cbn [min_fold].
  { reflexivity. }
  destruct (f i) eqn:Fi.
  - assert (Hi : acc <= g i) by (apply H; lia).
    replace (Nat.min acc (g i)) with acc by lia.
    apply IH. intros j Le Lt. apply H; lia.
  - apply IH. intros j Le Lt. apply H; lia.
Qed.

Lemma min_fold_lower : forall m f g fuel i acc,
    (forall j, i <= j -> j < i + fuel -> f j = true -> m <= g j) ->
    m <= acc -> m <= min_fold f g fuel i acc.
Proof.
  intros m f g fuel. induction fuel as [|k IH]; intros i acc H Le; cbn [min_fold].
  { lia. }
  destruct (f i) eqn:Fi.
  - assert (Hi : m <= g i) by (apply H; [lia|lia|exact Fi]).
    apply IH.
    + intros j Le2 Lt2 F. apply H; [lia|lia|exact F].
    + lia.
  - apply IH.
    + intros j Le2 Lt2 F. apply H; [lia|lia|exact F].
    + exact Le.
Qed.

Lemma min_fold_g_mono : forall f g1 g2 fuel i acc,
    (forall j, i <= j -> j < i + fuel -> g1 j <= g2 j) ->
    min_fold f g1 fuel i acc <= min_fold f g2 fuel i acc.
Proof.
  intros f g1 g2 fuel. induction fuel as [|k IH]; intros i acc H; cbn [min_fold].
  { reflexivity. }
  destruct (f i).
  - apply Nat.le_trans with (min_fold f g2 k (S i) (Nat.min acc (g1 i))).
    + apply IH. intros j Le Lt. apply H; lia.
    + apply min_fold_monotone.
      assert (Hi : g1 i <= g2 i) by (apply H; lia). lia.
  - apply IH. intros j Le Lt. apply H; lia.
Qed.

Lemma min_fold_acc_succ : forall f g fuel i acc,
    min_fold f g fuel i (S acc) <= S (min_fold f g fuel i acc).
Proof.
  intros f g fuel. induction fuel as [|k IH]; intros i acc; cbn [min_fold].
  { reflexivity. }
  destruct (f i).
  - apply Nat.le_trans with (min_fold f g k (S i) (S (Nat.min acc (g i)))).
    + apply min_fold_monotone. lia.
    + apply IH.
  - apply IH.
Qed.

Lemma min_fold_pin : forall f g pin fuel acc,
    pin < fuel -> f pin = false ->
    min_fold (fun j => if Nat.eqb pin j then true else f j)
             (fun j => if Nat.eqb pin j then acc else g j) fuel 0 acc
    = min_fold f g fuel 0 acc.
Proof.
  intros f g pin fuel acc Hpin Hf.
  set (f' := fun j => if Nat.eqb pin j then true else f j).
  set (g' := fun j => if Nat.eqb pin j then acc else g j).
  assert (GPI : g' pin = acc).
  { unfold g'. cbn. rewrite Nat.eqb_refl. reflexivity. }
  assert (EQF : forall j, pin <> j -> f' j = f j).
  { intros j Hne. unfold f'. cbn.
    destruct (Nat.eqb_spec pin j) as [Hij|_].
    - exfalso. apply Hne. exact Hij.
    - reflexivity. }
  assert (EQG : forall j, pin <> j -> g' j = g j).
  { intros j Hne. unfold g'. cbn.
    destruct (Nat.eqb_spec pin j) as [Hij|_].
    - exfalso. apply Hne. exact Hij.
    - reflexivity. }
  assert (FP : forall j, f j = true -> f' j = true).
  { intros j Hj.
    destruct (Nat.eqb_spec pin j) as [Hij|Hne].
    - exfalso. subst j. rewrite Hf in Hj. discriminate Hj.
    - rewrite (EQF j Hne). exact Hj. }
  apply Nat.le_antisymm.
  - apply min_fold_lower.
    + intros j Le Lt Hj.
      destruct (Nat.eqb_spec pin j) as [Hij|Hne].
      * exfalso. subst j. rewrite Hf in Hj. discriminate Hj.
      * rewrite <- (EQG j Hne).
        apply min_fold_hit with (fuel := fuel) (i := 0) (e := acc) (r := j);
          [lia|lia|exact (FP j Hj)].
    + apply min_fold_le.
  - apply min_fold_lower.
    + intros j Le Lt Hj.
      destruct (Nat.eqb_spec pin j) as [Hij|Hne].
      * subst j. rewrite GPI. apply min_fold_le.
      * rewrite (EQF j Hne) in Hj. rewrite (EQG j Hne).
        apply min_fold_hit with (fuel := fuel) (i := 0) (e := acc) (r := j);
          [lia|lia|exact Hj].
    + apply min_fold_le.
Qed.

Lemma forallb_here_false : forall (A : Type) (f : A -> bool) l x,
    In x l -> f x = false -> forallb f l = false.
Proof.
  intros A f l x. induction l as [|a t IH]; intros Hin Hf; cbn [forallb].
  { contradiction. }
  destruct Hin as [He|Hin'].
  { rewrite <- He in Hf. rewrite Hf. reflexivity. }
  destruct (f a) eqn:Fa.
  { exact (IH Hin' Hf). }
  reflexivity.
Qed.

Lemma nat_list_succ : forall n, nat_list (S n) = n :: nat_list n.
Proof. reflexivity. Qed.

Lemma filter_cons_true_len : forall (f : nat -> bool) n acc,
    f n = true ->
    length (filter f (nat_list (S n)) ++ acc) = S (length (filter f (nat_list n) ++ acc)).
Proof.
  intros f n acc Hn. rewrite nat_list_succ.
  assert (A : filter f (n :: nat_list n) = n :: filter f (nat_list n)).
  { cbn [filter]. rewrite Hn. reflexivity. }
  rewrite A. reflexivity.
Qed.

Lemma existsb_monotone : forall (A : Type) (f g : A -> bool) l,
    (forall x, In x l -> f x = true -> g x = true) ->
    existsb f l = true -> existsb g l = true.
Proof.
  intros A f g l H E.
  destruct (proj1 (@existsb_exists A f l) E) as [x [Hin Hfx]].
  apply (proj2 (@existsb_exists A g l)). exists x. split.
  - exact Hin.
  - apply H; auto.
Qed.

Lemma owns_bus_t_same : forall b i (t : task) c,
    owns (bus_t b i t) i c = andb (t_live t) (existsb (cref_eqb c) (t_curs t)).
Proof. intros b i t c. unfold owns. rewrite at_t_same_t. reflexivity. Qed.

Lemma owns_bus_t_other : forall b i (t : task) j c, j <> i ->
    owns (bus_t b i t) j c = owns b j c.
Proof. intros b i t j c H. unfold owns. rewrite at_t_other_t by lia. reflexivity. Qed.

Lemma usable_t_inert : forall b i (t : task) t' c, t' <> i ->
    usable (bus_t b i t) (ByTask t') c = usable b (ByTask t') c.
Proof.
  intros b i t t' c H. unfold usable.
  change (owns (bus_t b i t) t' c = owns b t' c).
  apply owns_bus_t_other. exact H.
Qed.

Lemma length_app1 : forall (l : list nat) x, length (l ++ [x]) = S (length l).
Proof. intros l x. rewrite app_length. cbn. lia. Qed.

Lemma sum_f_pointwise : forall (f g : nat -> nat) n,
    (forall j, j < n -> f j = g j) -> sum_f f n = sum_f g n.
Proof.
  intros f g n. induction n as [|k IH]; intros H; cbn [sum_f]; [reflexivity|].
  assert (HS : sum_f f k = sum_f g k)
    by (apply IH; intros j Lt; apply H; lia).
  assert (HK : f k = g k) by (apply H; lia).
  rewrite HK, HS. reflexivity.
Qed.
(* ── 9. I1: the cursor tables are static; a full table refuses ──── *)

Lemma pub_full_refuses : forall b cap,
    (forall j, j < max_writers -> w_live (b_w b j) = true) -> pub b cap = (b, None).
Proof.
  intros b cap Hall. unfold pub, pub_slot.
  assert (Hs : first_dead (fun j => w_live (b_w b j)) max_writers 0 = None)
    by (apply first_dead_none; intros j _ Lt; apply Hall; lia).
  rewrite Hs. reflexivity.
Qed.

Lemma pub_slot_bounded : forall b i, pub_slot b = Some i -> i < max_writers.
Proof.
  intros b i H. unfold pub_slot in H.
  apply first_dead_Some_lt with (fuel := max_writers) (i := 0) in H. lia.
Qed.

Lemma pub_slot_offers_a_dead_slot : forall b i,
    pub_slot b = Some i -> w_live (b_w b i) = false.
Proof.
  intros b i H. unfold pub_slot in H.
  apply first_dead_Some_free with (fuel := max_writers) (i := 0) (o := i) in H.
  exact H.
Qed.

Lemma pub_grant_matches_slot : forall b cap i,
    pub_slot b = Some i -> pr2 (pub b cap) = Some i.
Proof. intros b cap i H. unfold pub. rewrite H. reflexivity. Qed.

Lemma pub_grant_shape : forall b cap i, pub_slot b = Some i ->
    pub b cap = (bus_w b i (writer_revive (b_w b i) cap), Some i).
Proof. intros b cap i H. unfold pub. rewrite H. reflexivity. Qed.

Lemma pub_grant_is_live : forall b cap i, pub_slot b = Some i ->
    w_live (b_w (fst (pub b cap)) i) = true.
Proof.
  intros b cap i H. rewrite (pub_grant_shape b cap i H). cbn [fst].
  rewrite at_w_same_w. reflexivity.
Qed.

Lemma pub_grant_sets_the_capacity_only : forall b cap i, pub_slot b = Some i ->
    w_cap (b_w (fst (pub b cap)) i) = cap /\
    w_hist (b_w (fst (pub b cap)) i) = w_hist (b_w b i) /\
    w_drop (b_w (fst (pub b cap)) i) = w_drop (b_w b i).
Proof.
  intros b cap i H. rewrite (pub_grant_shape b cap i H). cbn [fst].
  split.
  - rewrite at_w_same_w. reflexivity.
  - split.
    + rewrite at_w_same_w. reflexivity.
    + rewrite at_w_same_w. reflexivity.
Qed.

Lemma pub_grant_others_untouched : forall b cap i j, pub_slot b = Some i -> i <> j ->
    b_w (fst (pub b cap)) j = b_w b j.
Proof.
  intros b cap i j H Hij. rewrite (pub_grant_shape b cap i H). cbn [fst].
  rewrite at_w_other_w by lia. reflexivity.
Qed.

Lemma pub_grant_leaves_the_other_tables_alone : forall b cap i j,
    pub_slot b = Some i ->
    b_r (fst (pub b cap)) j = b_r b j /\ b_t (fst (pub b cap)) j = b_t b j.
Proof.
  intros b cap i j H. rewrite (pub_grant_shape b cap i H). cbn [fst]. split; reflexivity.
Qed.

Lemma pub_never_makes_a_core : forall b cap,
    b_cores (fst (pub b cap)) = b_cores b.
Proof.
  intros b cap. unfold pub. destruct (pub_slot b); cbn; reflexivity.
Qed.

Lemma pub_grants_are_distinct : forall b c1 i j,
    pub_slot b = Some i -> pub_slot (fst (pub b c1)) = Some j -> i <> j.
Proof.
  intros b c1 i j H1 H2 Heq. subst j.
  apply pub_slot_offers_a_dead_slot in H2.
  unfold pub in H2. rewrite H1 in H2. cbn [fst] in H2.
  rewrite at_w_same_w in H2. unfold writer_revive in H2. discriminate H2.
Qed.

Lemma sub_full_refuses : forall b cl wid,
    (forall j, j < max_readers -> r_live (b_r b j) = true) ->
    sub_admit b cl wid = true -> sub b cl wid = (b, None).
Proof.
  intros b cl wid Hall Ha. unfold sub. rewrite Ha. unfold sub_slot.
  assert (Hs : first_dead (fun j => r_live (b_r b j)) max_readers 0 = None)
    by (apply first_dead_none; intros j _ Lt; apply Hall; lia).
  rewrite Hs. reflexivity.
Qed.

Lemma sub_slot_bounded : forall b i, sub_slot b = Some i -> i < max_readers.
Proof.
  intros b i H. unfold sub_slot in H.
  apply first_dead_Some_lt with (fuel := max_readers) (i := 0) in H. lia.
Qed.

Lemma sub_slot_offers_a_dead_slot : forall b i,
    sub_slot b = Some i -> r_live (b_r b i) = false.
Proof.
  intros b i H. unfold sub_slot in H.
  apply first_dead_Some_free with (fuel := max_readers) (i := 0) (o := i) in H.
  exact H.
Qed.

Lemma sub_refuses_a_dead_publisher : forall b cl wid,
    w_live (b_w b wid) = false -> sub b cl wid = (b, None).
Proof. intros b cl wid H. unfold sub, sub_admit. rewrite H. reflexivity. Qed.

Lemma sub_refuses_an_unusable_publisher : forall b cl wid,
    usable b cl (CPub wid) = false -> w_live (b_w b wid) = true -> sub b cl wid = (b, None).
Proof. intros b cl wid Hu Hw. unfold sub, sub_admit. rewrite Hu, Hw. reflexivity. Qed.
(* ── 10. I2a: a subscriber joins at the publisher's tail and pins nothing ── *)

Lemma bus_r_apply : forall b i (r : reader) j,
    b_r (bus_r b i r) j = if Nat.eqb i j then r else b_r b j.
Proof. reflexivity. Qed.

Lemma floor_of_join_at_seed : forall b i (wid e : nat),
    r_live (b_r b i) = false -> i < max_readers ->
    floor_of (bus_r b i (mk_reader true wid e e)) wid e = floor_of b wid e.
Proof.
  intros b i wid e Hd Lt. unfold floor_of.
  assert (E : forall j, 0 <= j -> j < 0 + max_readers ->
      sub_reader wid (b_r (bus_r b i (mk_reader true wid e e)) j)
      = (if Nat.eqb i j then true else sub_reader wid (b_r b j)) /\
      r_cur (b_r (bus_r b i (mk_reader true wid e e)) j)
      = (if Nat.eqb i j then e else r_cur (b_r b j))).
  { intros j _ _.
    rewrite !bus_r_apply. destruct (Nat.eqb i j) eqn:X.
    - unfold sub_reader. cbn. rewrite Nat.eqb_refl. split; reflexivity.
    - split; reflexivity. }
  transitivity (min_fold (fun j => if Nat.eqb i j then true else sub_reader wid (b_r b j))
                         (fun j => if Nat.eqb i j then e else r_cur (b_r b j))
                         max_readers 0 e).
  - apply (min_fold_cong _ _ _ _ max_readers 0 e E).
  - apply (min_fold_pin (fun j => sub_reader wid (b_r b j))
                        (fun j => r_cur (b_r b j)) i max_readers e).
    + exact Lt.
    + unfold sub_reader. rewrite Hd. reflexivity.
Qed.

Lemma sub_grant_shape : forall b cl wid i,
    sub_slot b = Some i -> sub_admit b cl wid = true ->
    sub b cl wid =
      (bus_r b i (mk_reader true wid (w_enq (b_w b wid)) (w_enq (b_w b wid))), Some i).
Proof. intros b cl wid i Ha Hs. unfold sub. rewrite Ha, Hs. reflexivity. Qed.

Lemma sub_grant_matches_slot : forall b cl wid i,
    sub_slot b = Some i -> sub_admit b cl wid = true -> pr2 (sub b cl wid) = Some i.
Proof. intros b cl wid i Ha Hs. rewrite (sub_grant_shape b cl wid i Ha Hs). reflexivity. Qed.

Lemma sub_new_reader_live : forall b cl wid i,
    sub_slot b = Some i -> sub_admit b cl wid = true ->
    r_live (b_r (fst (sub b cl wid)) i) = true.
Proof.
  intros b cl wid i Ha Hs. rewrite (sub_grant_shape b cl wid i Ha Hs). cbn [fst].
  rewrite at_r_same_r. reflexivity.
Qed.

Lemma sub_joins_at_the_tail : forall b cl wid i,
    sub_slot b = Some i -> sub_admit b cl wid = true ->
    r_start (b_r (fst (sub b cl wid)) i) = w_enq (b_w b wid) /\
    r_cur (b_r (fst (sub b cl wid)) i) = w_enq (b_w b wid) /\
    r_wid (b_r (fst (sub b cl wid)) i) = wid.
Proof.
  intros b cl wid i Ha Hs. rewrite (sub_grant_shape b cl wid i Ha Hs). cbn [fst].
  rewrite at_r_same_r. repeat split; reflexivity.
Qed.

Lemma sub_deq_zero_at_join : forall b cl wid i,
    sub_slot b = Some i -> sub_admit b cl wid = true ->
    r_deq (b_r (fst (sub b cl wid)) i) = 0.
Proof.
  intros b cl wid i Ha Hs. unfold r_deq.
  destruct (sub_joins_at_the_tail b cl wid i Ha Hs) as [H1 [H2 _]].
  rewrite H1, H2. lia.
Qed.

Lemma sub_late_joiner_has_nothing_to_read : forall b cl wid i,
    sub_slot b = Some i -> sub_admit b cl wid = true ->
    rcv_ready (fst (sub b cl wid)) i = false.
Proof.
  intros b cl wid i Ha Hs. rewrite (sub_grant_shape b cl wid i Ha Hs). cbn [fst].
  unfold rcv_ready. rewrite at_r_same_r, at_w_bus_r. cbn.
  apply Nat.ltb_ge. lia.
Qed.

Lemma sub_others_untouched : forall b cl wid i j,
    sub_slot b = Some i -> sub_admit b cl wid = true -> i <> j ->
    b_r (fst (sub b cl wid)) j = b_r b j.
Proof.
  intros b cl wid i j Ha Hs Hne. rewrite (sub_grant_shape b cl wid i Ha Hs). cbn [fst].
  apply at_r_other_r. exact Hne.
Qed.

Lemma sub_writers_untouched : forall b cl wid i j,
    sub_slot b = Some i -> sub_admit b cl wid = true ->
    b_w (fst (sub b cl wid)) j = b_w b j.
Proof.
  intros b cl wid i j Ha Hs. rewrite (sub_grant_shape b cl wid i Ha Hs). cbn [fst].
  apply at_w_bus_r.
Qed.

Lemma sub_tasks_untouched : forall b cl wid i j,
    sub_slot b = Some i -> sub_admit b cl wid = true ->
    b_t (fst (sub b cl wid)) j = b_t b j.
Proof.
  intros b cl wid i j Ha Hs. rewrite (sub_grant_shape b cl wid i Ha Hs). cbn [fst].
  apply at_t_bus_r.
Qed.

Lemma sub_never_makes_a_core : forall b cl wid, b_cores (fst (sub b cl wid)) = b_cores b.
Proof.
  intros b cl wid. unfold sub.
  destruct (sub_admit b cl wid) eqn:S; [ | reflexivity ].
  destruct (sub_slot b) as [i|]; [ apply cores_inert_r | reflexivity ].
Qed.

Lemma sub_admit_true_gives_live_publisher : forall b cl wid,
    sub_admit b cl wid = true -> w_live (b_w b wid) = true.
Proof.
  intros b cl wid H. unfold sub_admit in H.
  destruct (w_live (b_w b wid)) eqn:E; [ reflexivity | simpl in H; discriminate H ].
Qed.

Lemma sub_admit_true_gives_usable : forall b cl wid,
    sub_admit b cl wid = true -> usable b cl (CPub wid) = true.
Proof.
  intros b cl wid H. unfold sub_admit in H.
  destruct (w_live (b_w b wid)) eqn:E; simpl in H; [ exact H | discriminate H ].
Qed.

(* The sector cost of a publisher is set by its subscribers, not by the number
 * of subscribers: joining at the tail moves neither the watermark nor the hold. *)
Lemma sub_floor_of_unchanged_at_join : forall b cl wid i,
    sub_slot b = Some i -> sub_admit b cl wid = true ->
    floor_of (fst (sub b cl wid)) wid (w_enq (b_w b wid)) = floor_of b wid (w_enq (b_w b wid)).
Proof.
  intros b cl wid i Ha Hs. rewrite (sub_grant_shape b cl wid i Ha Hs). cbn [fst].
  apply floor_of_join_at_seed;
    [ apply (sub_slot_offers_a_dead_slot b i Ha) | apply (sub_slot_bounded b i Ha) ].
Qed.

(* ── 11. I2b: snd — the three contract states and what each one touches ── *)

Lemma writer_push_live : forall w d, w_live (writer_push w d) = true.
Proof. reflexivity. Qed.

Lemma writer_drop1_live : forall w, w_live (writer_drop1 w) = true.
Proof. reflexivity. Qed.

Lemma writer_revive_live : forall w cap, w_live (writer_revive w cap) = true.
Proof. reflexivity. Qed.

Lemma writer_push_enq : forall w d, w_enq (writer_push w d) = S (w_enq w).
Proof.
  intros w d. unfold w_enq, writer_push. cbn. rewrite app_length. cbn. lia.
Qed.

Lemma writer_push_hist : forall w d, w_hist (writer_push w d) = w_hist w ++ [d].
Proof. reflexivity. Qed.

Lemma writer_push_cap : forall w d, w_cap (writer_push w d) = w_cap w.
Proof. reflexivity. Qed.

Lemma writer_push_drop : forall w d, w_drop (writer_push w d) = w_drop w.
Proof. reflexivity. Qed.

Lemma writer_drop1_enq : forall w, w_enq (writer_drop1 w) = w_enq w.
Proof. reflexivity. Qed.

Lemma writer_drop1_hist : forall w, w_hist (writer_drop1 w) = w_hist w.
Proof. reflexivity. Qed.

Lemma writer_drop1_cap : forall w, w_cap (writer_drop1 w) = w_cap w.
Proof. reflexivity. Qed.

Lemma writer_drop1_drop : forall w, w_drop (writer_drop1 w) = S (w_drop w).
Proof. reflexivity. Qed.

Lemma writer_revive_enq : forall w cap, w_enq (writer_revive w cap) = w_enq w.
Proof. reflexivity. Qed.

Lemma writer_revive_drop : forall w cap, w_drop (writer_revive w cap) = w_drop w.
Proof. reflexivity. Qed.

Lemma writer_revive_hist : forall w cap, w_hist (writer_revive w cap) = w_hist w.
Proof. reflexivity. Qed.

Lemma writer_revive_cap : forall w cap, w_cap (writer_revive w cap) = cap.
Proof. reflexivity. Qed.

Lemma reader_advance_cur : forall r, r_cur (reader_advance r) = S (r_cur r).
Proof. reflexivity. Qed.

Lemma reader_advance_start : forall r, r_start (reader_advance r) = r_start r.
Proof. reflexivity. Qed.

Lemma reader_advance_wid : forall r, r_wid (reader_advance r) = r_wid r.
Proof. reflexivity. Qed.

Lemma reader_advance_live : forall r, r_live (reader_advance r) = true.
Proof. reflexivity. Qed.

Lemma snd_admit_live : forall b cl wid,
    snd_admit b cl wid = true -> w_live (b_w b wid) = true.
Proof.
  intros b cl wid H. unfold snd_admit in H.
  destruct (w_live (b_w b wid)) eqn:E; [ reflexivity | simpl in H; discriminate H ].
Qed.

Lemma snd_admit_live_usable : forall b cl wid,
    snd_admit b cl wid = true -> usable b cl (CPub wid) = true.
Proof.
  intros b cl wid H. unfold snd_admit in H.
  destruct (w_live (b_w b wid)) eqn:E; simpl in H; [ exact H | discriminate H ].
Qed.

Lemma snd_bad_cursor_inert : forall b cl wid d,
    snd_admit b cl wid = false -> snd b cl wid d = (b, S_BAD_CURSOR).
Proof. intros b cl wid d H. unfold snd. rewrite H. reflexivity. Qed.

Lemma snd_bad_cursor_bus : forall b cl wid d,
    snd_admit b cl wid = false -> fst (snd b cl wid d) = b.
Proof.
  intros b cl wid d H. rewrite (snd_bad_cursor_inert b cl wid d H). reflexivity.
Qed.

Lemma snd_full_shape : forall b cl wid d,
    snd_admit b cl wid = true -> snd_fits b wid = false ->
    snd b cl wid d =
      (bus_w b wid (writer_drop1 (b_w b wid)), S_FULL (S (w_drop (b_w b wid)))).
Proof. intros b cl wid d Ha Hf. unfold snd. rewrite Ha, Hf. reflexivity. Qed.

Lemma snd_ok_shape : forall b cl wid d,
    snd_admit b cl wid = true -> snd_fits b wid = true ->
    snd b cl wid d = (bus_w b wid (writer_push (b_w b wid) d), S_OK).
Proof. intros b cl wid d Ha Hf. unfold snd. rewrite Ha, Hf. reflexivity. Qed.

Lemma snd_full_bus : forall b cl wid d,
    snd_admit b cl wid = true -> snd_fits b wid = false ->
    fst (snd b cl wid d) = bus_w b wid (writer_drop1 (b_w b wid)).
Proof.
  intros b cl wid d Ha Hf. rewrite (snd_full_shape b cl wid d Ha Hf). cbn [fst].
  reflexivity.
Qed.

Lemma snd_ok_bus : forall b cl wid d,
    snd_admit b cl wid = true -> snd_fits b wid = true ->
    fst (snd b cl wid d) = bus_w b wid (writer_push (b_w b wid) d).
Proof.
  intros b cl wid d Ha Hf. rewrite (snd_ok_shape b cl wid d Ha Hf). cbn [fst].
  reflexivity.
Qed.

Lemma snd_full_receipt : forall b cl wid d,
    snd_admit b cl wid = true -> snd_fits b wid = false ->
    pr2 (snd b cl wid d) = S_FULL (S (w_drop (b_w b wid))).
Proof. intros b cl wid d Ha Hf. rewrite (snd_full_shape b cl wid d Ha Hf). reflexivity. Qed.

Lemma snd_ok_receipt : forall b cl wid d,
    snd_admit b cl wid = true -> snd_fits b wid = true -> pr2 (snd b cl wid d) = S_OK.
Proof. intros b cl wid d Ha Hf. rewrite (snd_ok_shape b cl wid d Ha Hf). reflexivity. Qed.

Lemma held_bus_w_same : forall b i (w : writer), w_live w = true ->
    held (bus_w b i w) i = w_enq w - floor_of b i (w_enq w).
Proof.
  intros b i w H. unfold held. rewrite at_w_same_w, floor_of_w_inert, H. reflexivity.
Qed.

Lemma held_of_live_writer : forall b wid, w_live (b_w b wid) = true ->
    held b wid = w_enq (b_w b wid) - floor_of b wid (w_enq (b_w b wid)).
Proof. intros b wid H. unfold held. rewrite H. reflexivity. Qed.

Lemma snd_full_keeps_the_history : forall b cl wid d,
    snd_admit b cl wid = true -> snd_fits b wid = false ->
    w_hist (b_w (fst (snd b cl wid d)) wid) = w_hist (b_w b wid) /\
    w_enq (b_w (fst (snd b cl wid d)) wid) = w_enq (b_w b wid).
Proof.
  intros b cl wid d Ha Hf. rewrite (snd_full_bus b cl wid d Ha Hf).
  rewrite at_w_same_w. repeat split; reflexivity.
Qed.

Lemma snd_full_held_bus : forall b wid, w_live (b_w b wid) = true ->
    held (bus_w b wid (writer_drop1 (b_w b wid))) wid = held b wid.
Proof.
  intros b wid Hw.
  rewrite (held_bus_w_same b wid (writer_drop1 (b_w b wid))) by apply writer_drop1_live.
  rewrite writer_drop1_enq, (held_of_live_writer b wid Hw). reflexivity.
Qed.

Lemma snd_full_held_unchanged : forall b cl wid d,
    snd_admit b cl wid = true -> snd_fits b wid = false ->
    held (fst (snd b cl wid d)) wid = held b wid.
Proof.
  intros b cl wid d Ha Hf. rewrite (snd_full_bus b cl wid d Ha Hf).
  apply snd_full_held_bus. exact (snd_admit_live b cl wid Ha).
Qed.

Lemma held_update_preserved : forall b i (w : writer),
    (held (bus_w b i w) i = held b i) ->
    (forall j, j <> i -> held (bus_w b i w) j = held b j) ->
    total_held (bus_w b i w) = total_held b.
Proof.
  intros b i w Hi Hj. unfold total_held. apply sum_f_pointwise. intros j Lt.
  destruct (Nat.eqb_spec j i) as [Hji|Hne].
  - rewrite Hji. exact Hi.
  - apply Hj. lia.
Qed.

Lemma snd_full_total_held_bus : forall b wid, w_live (b_w b wid) = true ->
    total_held (bus_w b wid (writer_drop1 (b_w b wid))) = total_held b.
Proof.
  intros b wid Hw. apply held_update_preserved.
  - apply snd_full_held_bus. exact Hw.
  - intros j Hne. apply held_w_inert. lia.
Qed.

Lemma snd_full_total_held_unchanged : forall b cl wid d,
    snd_admit b cl wid = true -> snd_fits b wid = false ->
    total_held (fst (snd b cl wid d)) = total_held b.
Proof.
  intros b cl wid d Ha Hf. rewrite (snd_full_bus b cl wid d Ha Hf).
  apply snd_full_total_held_bus. exact (snd_admit_live b cl wid Ha).
Qed.

Lemma snd_full_pool_free_unchanged : forall b cl wid d,
    snd_admit b cl wid = true -> snd_fits b wid = false ->
    pool_free (fst (snd b cl wid d)) = pool_free b.
Proof.
  intros b cl wid d Ha Hf. unfold pool_free.
  rewrite (snd_full_total_held_unchanged b cl wid d Ha Hf). reflexivity.
Qed.

Lemma snd_full_watermark_unchanged : forall b cl wid d e,
    snd_admit b cl wid = true -> snd_fits b wid = false ->
    floor_of (fst (snd b cl wid d)) wid e = floor_of b wid e.
Proof.
  intros b cl wid d e Ha Hf. rewrite (snd_full_bus b cl wid d Ha Hf).
  apply floor_of_w_inert.
Qed.

Lemma snd_attempts_counted : forall b cl wid d,
    snd_admit b cl wid = true ->
    w_enq (b_w (fst (snd b cl wid d)) wid) + w_drop (b_w (fst (snd b cl wid d)) wid)
    = w_enq (b_w b wid) + w_drop (b_w b wid) + 1.
Proof.
  intros b cl wid d Ha.
  destruct (snd_fits b wid) eqn:F.
  - rewrite (snd_ok_bus b cl wid d Ha F). rewrite !at_w_same_w.
    rewrite writer_push_enq, writer_push_drop. lia.
  - rewrite (snd_full_bus b cl wid d Ha F). rewrite !at_w_same_w.
    rewrite writer_drop1_enq, writer_drop1_drop. lia.
Qed.

Lemma held_push_bus : forall b wid d, w_live (b_w b wid) = true ->
    held (bus_w b wid (writer_push (b_w b wid) d)) wid <= S (held b wid).
Proof.
  intros b wid d Hw.
  rewrite (held_bus_w_same b wid (writer_push (b_w b wid) d)) by apply writer_push_live.
  rewrite writer_push_enq, (held_of_live_writer b wid Hw).
  assert (A : floor_of b wid (w_enq (b_w b wid))
              <= floor_of b wid (S (w_enq (b_w b wid))))
    by (apply floor_of_monotone; lia).
  assert (B : floor_of b wid (w_enq (b_w b wid)) <= w_enq (b_w b wid))
    by (apply floor_of_le).
  lia.
Qed.

Lemma snd_held_grows_by_one : forall b cl wid d,
    snd_admit b cl wid = true ->
    held (fst (snd b cl wid d)) wid <= S (held b wid).
Proof.
  intros b cl wid d Ha.
  destruct (snd_fits b wid) eqn:F.
  - rewrite (snd_ok_bus b cl wid d Ha F).
    apply held_push_bus. exact (snd_admit_live b cl wid Ha).
  - rewrite (snd_full_bus b cl wid d Ha F).
    rewrite snd_full_held_bus by (exact (snd_admit_live b cl wid Ha)). lia.
Qed.

Lemma snd_fits_room : forall b wid, snd_fits b wid = true -> total_held b < sector_pool.
Proof.
  intros b wid H. unfold snd_fits in H. unfold pool_room in H.
  apply andb_true_iff in H. destruct H as [_ H2]. apply Nat.ltb_lt in H2. exact H2.
Qed.

Lemma snd_fits_below_cap : forall b wid,
    snd_fits b wid = true -> held b wid < w_cap (b_w b wid).
Proof.
  intros b wid H. unfold snd_fits in H. apply andb_true_iff in H. destruct H as [H1 _].
  rewrite negb_true_iff in H1.
  exact (proj1 (Nat.leb_gt (w_cap (b_w b wid)) (held b wid)) H1).
Qed.

Lemma total_held_within_rings : forall b, bus_wf b ->
    total_held b <= max_writers * ring_capacity.
Proof.
  intros b Wb. destruct Wb as [Wt [Wbnd _]].
  unfold total_held.
  assert (H1 : sum_f (held b) max_writers <= sum_f (fun j => w_cap (b_w b j)) max_writers)
    by (apply sum_f_le; intros j _; apply Wbnd).
  assert (Hcap : forall j, w_cap (b_w b j) <= ring_capacity).
  { intros j. assert (Hd := proj2 (proj2 (proj2 Wt)) j). exact (proj2 Hd). }
  assert (H2 : sum_f (fun j => w_cap (b_w b j)) max_writers <= max_writers * ring_capacity)
    by (apply sum_f_bound; intros j _; apply Hcap).
  lia.
Qed.

Lemma pool_free_plus_held : forall b,
    total_held b <= sector_pool -> pool_free b + total_held b = sector_pool.
Proof. intros b H. unfold pool_free. lia. Qed.

Lemma sum_f_add : forall (f g : nat -> nat) n,
    sum_f (fun j => f j + g j) n = sum_f f n + sum_f g n.
Proof.
  intros f g n. induction n as [|m IH]; cbn [sum_f]; [reflexivity|].
  rewrite IH. lia.
Qed.

(* one publisher id can appear at most once in a table scan *)
Lemma indicator_ge_zero : forall i n, n <= i ->
    sum_f (fun j => if Nat.eqb j i then 1 else 0) n = 0.
Proof.
  intros i n. induction n as [|k IH]; intros Hle; cbn [sum_f].
  - reflexivity.
  - destruct (Nat.eqb k i) eqn:E.
    + apply Nat.eqb_eq in E. lia.
    + assert (K : (if Nat.eqb k i then 1 else 0) = 0) by (rewrite E; reflexivity).
      rewrite IH by lia. lia.
Qed.

Lemma indicator_sum : forall n i,
    sum_f (fun j => if Nat.eqb j i then 1 else 0) n <= 1.
Proof.
  intros n i. revert i. induction n as [|k IH]; intros j; cbn [sum_f].
  - lia.
  - destruct (Nat.eqb k j) eqn:E.
    + apply Nat.eqb_eq in E.
      assert (HZ : sum_f (fun i0 => if Nat.eqb i0 j then 1 else 0) k = 0)
        by (apply (indicator_ge_zero j k); lia).
      rewrite HZ. lia.
    + assert (K : (if Nat.eqb k j then 1 else 0) = 0) by (rewrite E; reflexivity).
      specialize (IH j). lia.
Qed.

Lemma total_held_after_snd : forall b cl wid d,
    total_held (fst (snd b cl wid d)) <= S (total_held b).
Proof.
  intros b cl wid d.
  destruct (snd_admit b cl wid) eqn:A.
  - destruct (snd_fits b wid) eqn:F.
    + rewrite (snd_ok_bus b cl wid d A F). unfold total_held.
      apply Nat.le_trans with
        (sum_f (fun j => held b j + if Nat.eqb j wid then 1 else 0) max_writers).
      * apply sum_f_le. intros j Lt.
        destruct (Nat.eqb j wid) eqn:E.
        { apply Nat.eqb_eq in E. rewrite E.
          assert (HP : held (bus_w b wid (writer_push (b_w b wid) d)) wid
                        <= S (held b wid))
            by (apply held_push_bus; exact (snd_admit_live b cl wid A)).
          lia. }
        { assert (Hne : j <> wid)
            by (intros Hc; rewrite Hc, Nat.eqb_refl in E; discriminate E).
          assert (Hne2 : wid <> j) by lia.
          rewrite (held_w_inert b wid (writer_push (b_w b wid) d) j Hne2).
          apply Nat.le_add_r. }
      * rewrite sum_f_add. unfold total_held.
        assert (Hs : sum_f (fun j => if Nat.eqb j wid then 1 else 0) max_writers <= 1)
          by (apply indicator_sum).
        lia.
    + rewrite (snd_full_bus b cl wid d A F).
      rewrite snd_full_total_held_bus by (exact (snd_admit_live b cl wid A)). lia.
  - rewrite (snd_bad_cursor_bus b cl wid d A). lia.
Qed.

Lemma snd_conserves_the_pool : forall b cl wid d, bus_wf b ->
    pool_free (fst (snd b cl wid d)) + total_held (fst (snd b cl wid d)) = sector_pool.
Proof.
  intros b cl wid d Wb. apply pool_free_plus_held.
  assert (H1 : total_held b <= max_writers * ring_capacity)
    by (apply total_held_within_rings; exact Wb).
  assert (H3 : max_writers * ring_capacity + 1 <= sector_pool).
  { unfold max_writers, ring_capacity, sector_pool. cbn. lia. }
  assert (H4 : total_held (fst (snd b cl wid d)) <= S (total_held b))
    by (apply total_held_after_snd).
  lia.
Qed.

Lemma snd_never_overfills : forall b cl wid d, bus_wf b ->
    held (fst (snd b cl wid d)) wid <= w_cap (b_w b wid).
Proof.
  intros b cl wid d Wb.
  assert (H0 : held b wid <= w_cap (b_w b wid)) by (apply (proj1 (proj2 Wb))).
  destruct (snd_admit b cl wid) eqn:A.
  - destruct (snd_fits b wid) eqn:F.
    + rewrite (snd_ok_bus b cl wid d A F).
      apply Nat.le_trans with (S (held b wid)).
      * apply held_push_bus. exact (snd_admit_live b cl wid A).
      * assert (HL : held b wid < w_cap (b_w b wid))
          by (apply snd_fits_below_cap; exact F).
        lia.
    + rewrite (snd_full_bus b cl wid d A F).
      rewrite snd_full_held_bus by (exact (snd_admit_live b cl wid A)). exact H0.
  - rewrite (snd_bad_cursor_bus b cl wid d A). exact H0.
Qed.
(* ── 10. I3: rcv is total, and strictly non-blocking ─────────────── *)

(* A live cursor id is inside its static table by construction: no id
 * ever escapes the table it came from. *)
Lemma w_live_lt : forall b i, bus_wf b -> w_live (b_w b i) = true -> i < max_writers.
Proof.
  intros b i Wb H. destruct Wb as [Wt _].
  destruct (Nat.leb max_writers i) eqn:L.
  - rewrite Nat.leb_le in L.
    assert (E : b_w b i = empty_writer) by (apply (proj1 Wt); exact L).
    unfold empty_writer in E. rewrite E in H. cbn in H. discriminate H.
  - rewrite Nat.leb_gt in L. exact L.
Qed.

Lemma r_live_lt : forall b i, bus_wf b -> r_live (b_r b i) = true -> i < max_readers.
Proof.
  intros b i Wb H. destruct Wb as [Wt _].
  destruct (Nat.leb max_readers i) eqn:L.
  - rewrite Nat.leb_le in L.
    assert (E : b_r b i = empty_reader) by (apply (proj1 (proj2 Wt)); exact L).
    unfold empty_reader in E. rewrite E in H. cbn in H. discriminate H.
  - rewrite Nat.leb_gt in L. exact L.
Qed.

Lemma t_live_lt : forall b i, bus_wf b -> t_live (b_t b i) = true -> i < max_tasks.
Proof.
  intros b i Wb H. destruct Wb as [Wt _].
  destruct (Nat.leb max_tasks i) eqn:L.
  - rewrite Nat.leb_le in L.
    assert (E : b_t b i = empty_task) by (apply (proj1 (proj2 (proj2 Wt))); exact L).
    unfold empty_task in E. rewrite E in H. cbn in H. discriminate H.
  - rewrite Nat.leb_gt in L. exact L.
Qed.

(* the admission guard destructs into its three contract checks *)
Lemma rcv_admit_live_reader : forall b cl rid,
    rcv_admit b cl rid = true -> r_live (b_r b rid) = true.
Proof.
  intros b cl rid H. unfold rcv_admit in H.
  apply andb_true_iff in H. destruct H as [E _]. exact E.
Qed.

Lemma rcv_admit_live_publisher : forall b cl rid,
    rcv_admit b cl rid = true -> w_live (b_w b (r_wid (b_r b rid))) = true.
Proof.
  intros b cl rid H. unfold rcv_admit in H.
  apply andb_true_iff in H. destruct H as [_ Y].
  apply andb_true_iff in Y. destruct Y as [W _]. exact W.
Qed.

Lemma rcv_admit_usable : forall b cl rid,
    rcv_admit b cl rid = true -> usable b cl (CSub rid) = true.
Proof.
  intros b cl rid H. unfold rcv_admit in H.
  apply andb_true_iff in H. destruct H as [_ Y].
  apply andb_true_iff in Y. destruct Y as [_ U]. exact U.
Qed.

Lemma rcv_admit_in_range : forall b cl rid, bus_wf b ->
    rcv_admit b cl rid = true -> rid < max_readers.
Proof.
  intros b cl rid Wb A. apply (r_live_lt b rid Wb).
  exact (rcv_admit_live_reader b cl rid A).
Qed.

(* reading one's own cursor is allowed; reading anybody else's is refused *)
Lemma rcv_refuses_a_stranger : forall b t rid,
    owns b t (CSub rid) = false -> rcv_admit b (ByTask t) rid = false.
Proof.
  intros b t rid H. unfold rcv_admit, usable. rewrite H.
  destruct (r_live (b_r b rid)) eqn:E;
    destruct (w_live (b_w b (r_wid (b_r b rid)))) eqn:W; cbn; reflexivity.
Qed.

Lemma rcv_owner_admits_when_live : forall b t rid,
    r_live (b_r b rid) = true -> w_live (b_w b (r_wid (b_r b rid))) = true ->
    owns b t (CSub rid) = true -> rcv_admit b (ByTask t) rid = true.
Proof.
  intros b t rid Hr Hw Ho. unfold rcv_admit, usable.
  rewrite Hr, Hw, Ho. reflexivity.
Qed.

(* the three receipts, in shape form and in projection form *)
Lemma rcv_bad_cursor_shape : forall b cl rid,
    rcv_admit b cl rid = false -> rcv b cl rid = (b, R_BAD_CURSOR).
Proof. intros b cl rid H. unfold rcv. rewrite H. reflexivity. Qed.

Lemma rcv_bad_cursor_bus : forall b cl rid,
    rcv_admit b cl rid = false -> fst (rcv b cl rid) = b.
Proof.
  intros b cl rid H. rewrite (rcv_bad_cursor_shape b cl rid H). reflexivity.
Qed.

Lemma rcv_bad_cursor_receipt : forall b cl rid,
    rcv_admit b cl rid = false -> pr2 (rcv b cl rid) = R_BAD_CURSOR.
Proof.
  intros b cl rid H. rewrite (rcv_bad_cursor_shape b cl rid H). reflexivity.
Qed.

Lemma rcv_empty_shape : forall b cl rid,
    rcv_admit b cl rid = true -> rcv_ready b rid = false ->
    rcv b cl rid = (b, R_EMPTY).
Proof. intros b cl rid Ha Hr. unfold rcv. rewrite Ha, Hr. reflexivity. Qed.

Lemma rcv_empty_bus : forall b cl rid,
    rcv_admit b cl rid = true -> rcv_ready b rid = false -> fst (rcv b cl rid) = b.
Proof.
  intros b cl rid Ha Hr. rewrite (rcv_empty_shape b cl rid Ha Hr). reflexivity.
Qed.

Lemma rcv_empty_receipt : forall b cl rid,
    rcv_admit b cl rid = true -> rcv_ready b rid = false -> pr2 (rcv b cl rid) = R_EMPTY.
Proof.
  intros b cl rid Ha Hr. rewrite (rcv_empty_shape b cl rid Ha Hr). reflexivity.
Qed.

Lemma rcv_data_shape : forall b cl rid,
    rcv_admit b cl rid = true -> rcv_ready b rid = true ->
    rcv b cl rid =
      (bus_r b rid (reader_advance (b_r b rid)),
       R_DATA (nth (r_cur (b_r b rid)) (w_hist (b_w b (r_wid (b_r b rid)))) 0)).
Proof. intros b cl rid Ha Hr. unfold rcv. rewrite Ha, Hr. reflexivity. Qed.

Lemma rcv_data_bus : forall b cl rid,
    rcv_admit b cl rid = true -> rcv_ready b rid = true ->
    fst (rcv b cl rid) = bus_r b rid (reader_advance (b_r b rid)).
Proof.
  intros b cl rid Ha Hr. rewrite (rcv_data_shape b cl rid Ha Hr).
  cbn [fst]. reflexivity.
Qed.

Lemma rcv_data_receipt : forall b cl rid,
    rcv_admit b cl rid = true -> rcv_ready b rid = true ->
    pr2 (rcv b cl rid) = R_DATA (hist_at b (r_wid (b_r b rid)) (r_cur (b_r b rid))).
Proof.
  intros b cl rid Ha Hr. rewrite (rcv_data_shape b cl rid Ha Hr). reflexivity.
Qed.

(* a caller cannot drain a cursor its task does not own *)
Lemma rcv_stranger_gets_bad_cursor : forall b t rid,
    owns b t (CSub rid) = false -> rcv b (ByTask t) rid = (b, R_BAD_CURSOR).
Proof.
  intros b t rid H.
  assert (A : rcv_admit b (ByTask t) rid = false)
    by (apply rcv_refuses_a_stranger; exact H).
  rewrite (rcv_bad_cursor_shape b (ByTask t) rid A). reflexivity.
Qed.
(* reading is what frees a sector: the watermark can only move forward *)
Lemma min_fold_fg_mono : forall f1 g1 f2 g2 fuel i acc,
    (forall j, i <= j -> j < i + fuel -> f1 j = f2 j /\ g1 j <= g2 j) ->
    min_fold f1 g1 fuel i acc <= min_fold f2 g2 fuel i acc.
Proof.
  intros f1 g1 f2 g2 fuel. induction fuel as [|k IH]; intros i acc H; cbn [min_fold].
  { lia. }
  assert (Hi : f1 i = f2 i /\ g1 i <= g2 i) by (apply H; lia).
  destruct Hi as [Hf Hg]. rewrite Hf.
  destruct (f2 i) eqn:Fi.
  - apply Nat.le_trans with (min_fold f2 g2 k (S i) (Nat.min acc (g1 i))).
    + apply IH. intros j Le Lt. apply H; lia.
    + apply min_fold_monotone. apply Nat.min_le_compat; lia.
  - apply IH. intros j Le Lt. apply H; lia.
Qed.

Lemma sub_reader_advance : forall wid r, r_live r = true ->
    sub_reader wid (reader_advance r) = sub_reader wid r.
Proof.
  intros wid r H. unfold sub_reader, reader_advance. cbn. rewrite H. reflexivity.
Qed.

Lemma floor_of_advance_rise : forall b rid wid e, rid < max_readers ->
    r_live (b_r b rid) = true ->
    floor_of b wid e <= floor_of (bus_r b rid (reader_advance (b_r b rid))) wid e.
Proof.
  intros b rid wid e Lt Hlive. unfold floor_of.
  apply (min_fold_fg_mono (fun j => sub_reader wid (b_r b j)) (fun j => r_cur (b_r b j))
                    (fun j => sub_reader wid (b_r (bus_r b rid (reader_advance (b_r b rid))) j))
                    (fun j => r_cur (b_r (bus_r b rid (reader_advance (b_r b rid))) j))
                    max_readers 0 e).
  intros j Le Hj. rewrite !bus_r_apply.
  destruct (Nat.eqb rid j) eqn:R.
  - apply Nat.eqb_eq in R. subst j. split.
    + rewrite sub_reader_advance by exact Hlive. reflexivity.
    + rewrite reader_advance_cur. lia.
  - split; reflexivity.
Qed.

Lemma held_advance_le : forall b rid wid, rid < max_readers ->
    r_live (b_r b rid) = true ->
    held (bus_r b rid (reader_advance (b_r b rid))) wid <= held b wid.
Proof.
  intros b rid wid Lt Hlive. unfold held. rewrite at_w_bus_r.
  destruct (w_live (b_w b wid)) eqn:W.
  - apply Nat.sub_le_mono_l.
    apply floor_of_advance_rise; [ exact Lt | exact Hlive ].
  - reflexivity.
Qed.

Lemma rcv_never_increases_holding : forall b cl rid wid, rid < max_readers ->
    held (fst (rcv b cl rid)) wid <= held b wid.
Proof.
  intros b cl rid wid Lt.
  destruct (rcv_admit b cl rid) eqn:A.
  - destruct (rcv_ready b rid) eqn:R.
    + rewrite (rcv_data_bus b cl rid A R).
      apply held_advance_le; [ exact Lt |
        exact (rcv_admit_live_reader b cl rid A) ].
    + rewrite (rcv_empty_bus b cl rid A R). apply Nat.le_refl.
  - rewrite (rcv_bad_cursor_bus b cl rid A). apply Nat.le_refl.
Qed.

Lemma rcv_never_increases_total_holding : forall b cl rid, bus_wf b ->
    total_held (fst (rcv b cl rid)) <= total_held b.
Proof.
  intros b cl rid Wb. unfold total_held.
  destruct (rcv_admit b cl rid) eqn:A.
  - destruct (rcv_ready b rid) eqn:R.
    + rewrite (rcv_data_bus b cl rid A R). apply sum_f_le. intros j Lt.
      apply held_advance_le.
      * apply (r_live_lt b rid Wb). exact (rcv_admit_live_reader b cl rid A).
      * exact (rcv_admit_live_reader b cl rid A).
    + rewrite (rcv_empty_bus b cl rid A R). apply Nat.le_refl.
  - rewrite (rcv_bad_cursor_bus b cl rid A). apply Nat.le_refl.
Qed.

Lemma rcv_never_spends_a_slot : forall b cl rid, bus_wf b ->
    pool_free b <= pool_free (fst (rcv b cl rid)).
Proof.
  intros b cl rid Wb. unfold pool_free. apply Nat.sub_le_mono_l.
  apply rcv_never_increases_total_holding. exact Wb.
Qed.

(* the cursor: it never rewinds, never jumps, and never reads past the tail *)
Lemma rcv_cursor_never_rewinds : forall b cl rid,
    r_cur (b_r b rid) <= r_cur (b_r (fst (rcv b cl rid)) rid).
Proof.
  intros b cl rid.
  destruct (rcv_admit b cl rid) eqn:A.
  - destruct (rcv_ready b rid) eqn:R.
    + rewrite (rcv_data_bus b cl rid A R). rewrite at_r_same_r.
      rewrite reader_advance_cur. lia.
    + rewrite (rcv_empty_bus b cl rid A R). apply Nat.le_refl.
  - rewrite (rcv_bad_cursor_bus b cl rid A). apply Nat.le_refl.
Qed.

Lemma rcv_cursor_moves_at_most_one : forall b cl rid,
    r_cur (b_r (fst (rcv b cl rid)) rid) <= S (r_cur (b_r b rid)).
Proof.
  intros b cl rid.
  destruct (rcv_admit b cl rid) eqn:A.
  - destruct (rcv_ready b rid) eqn:R.
    + rewrite (rcv_data_bus b cl rid A R). rewrite at_r_same_r.
      rewrite reader_advance_cur. lia.
    + rewrite (rcv_empty_bus b cl rid A R). lia.
  - rewrite (rcv_bad_cursor_bus b cl rid A). lia.
Qed.

Lemma rcv_cursor_stays_within_the_publisher : forall b cl rid, bus_wf b ->
    r_live (b_r b rid) = true ->
    r_cur (b_r (fst (rcv b cl rid)) rid) <= w_enq (b_w b (r_wid (b_r b rid))).
Proof.
  intros b cl rid Wb Hlive.
  destruct (rcv_admit b cl rid) eqn:A.
  - destruct (rcv_ready b rid) eqn:R.
    + rewrite (rcv_data_bus b cl rid A R). rewrite at_r_same_r.
      rewrite reader_advance_cur.
      unfold rcv_ready in R. rewrite Nat.ltb_lt in R. lia.
    + rewrite (rcv_empty_bus b cl rid A R).
      destruct Wb as [_ [_ [Wr _]]].
      assert (B := Wr rid Hlive). destruct B as [_ [_ [_ Le]]]. exact Le.
  - rewrite (rcv_bad_cursor_bus b cl rid A).
    destruct Wb as [_ [_ [Wr _]]].
    assert (B := Wr rid Hlive). destruct B as [_ [_ [_ Le]]]. exact Le.
Qed.

Lemma rcv_start_never_moves : forall b cl rid,
    r_start (b_r (fst (rcv b cl rid)) rid) = r_start (b_r b rid).
Proof.
  intros b cl rid.
  destruct (rcv_admit b cl rid) eqn:A.
  - destruct (rcv_ready b rid) eqn:R.
    + rewrite (rcv_data_bus b cl rid A R). rewrite at_r_same_r.
      rewrite reader_advance_start. reflexivity.
    + rewrite (rcv_empty_bus b cl rid A R). reflexivity.
  - rewrite (rcv_bad_cursor_bus b cl rid A). reflexivity.
Qed.

Lemma rcv_publisher_never_switches : forall b cl rid,
    r_wid (b_r (fst (rcv b cl rid)) rid) = r_wid (b_r b rid).
Proof.
  intros b cl rid.
  destruct (rcv_admit b cl rid) eqn:A.
  - destruct (rcv_ready b rid) eqn:R.
    + rewrite (rcv_data_bus b cl rid A R). rewrite at_r_same_r.
      rewrite reader_advance_wid. reflexivity.
    + rewrite (rcv_empty_bus b cl rid A R). reflexivity.
  - rewrite (rcv_bad_cursor_bus b cl rid A). reflexivity.
Qed.

(* nothing else moves: rcv touches exactly one reader slot *)
Lemma rcv_other_readers_untouched : forall b cl rid j, j <> rid ->
    b_r (fst (rcv b cl rid)) j = b_r b j.
Proof.
  intros b cl rid j Hne.
  destruct (rcv_admit b cl rid) eqn:A.
  - destruct (rcv_ready b rid) eqn:R.
    + rewrite (rcv_data_bus b cl rid A R). apply at_r_other_r. lia.
    + rewrite (rcv_empty_bus b cl rid A R). reflexivity.
  - rewrite (rcv_bad_cursor_bus b cl rid A). reflexivity.
Qed.

Lemma rcv_writers_untouched : forall b cl rid j,
    b_w (fst (rcv b cl rid)) j = b_w b j.
Proof.
  intros b cl rid j.
  destruct (rcv_admit b cl rid) eqn:A.
  - destruct (rcv_ready b rid) eqn:R.
    + rewrite (rcv_data_bus b cl rid A R). apply at_w_bus_r.
    + rewrite (rcv_empty_bus b cl rid A R). reflexivity.
  - rewrite (rcv_bad_cursor_bus b cl rid A). reflexivity.
Qed.

Lemma rcv_tasks_untouched : forall b cl rid j,
    b_t (fst (rcv b cl rid)) j = b_t b j.
Proof.
  intros b cl rid j.
  destruct (rcv_admit b cl rid) eqn:A.
  - destruct (rcv_ready b rid) eqn:R.
    + rewrite (rcv_data_bus b cl rid A R). apply at_t_bus_r.
    + rewrite (rcv_empty_bus b cl rid A R). reflexivity.
  - rewrite (rcv_bad_cursor_bus b cl rid A). reflexivity.
Qed.

Lemma rcv_never_makes_a_core : forall b cl rid,
    b_cores (fst (rcv b cl rid)) = b_cores b.
Proof.
  intros b cl rid.
  destruct (rcv_admit b cl rid) eqn:A.
  - destruct (rcv_ready b rid) eqn:R.
    + rewrite (rcv_data_bus b cl rid A R). apply cores_inert_r.
    + rewrite (rcv_empty_bus b cl rid A R). reflexivity.
  - rewrite (rcv_bad_cursor_bus b cl rid A). reflexivity.
Qed.

Lemma rcv_never_drops : forall b cl rid wid,
    w_drop (b_w (fst (rcv b cl rid)) wid) = w_drop (b_w b wid).
Proof.
  intros b cl rid wid. rewrite (rcv_writers_untouched b cl rid wid). reflexivity.
Qed.

Lemma rcv_ownership_untouched : forall b cl rid t c,
    owns (fst (rcv b cl rid)) t c = owns b t c.
Proof.
  intros b cl rid t c.
  destruct (rcv_admit b cl rid) eqn:A.
  - destruct (rcv_ready b rid) eqn:R.
    + rewrite (rcv_data_bus b cl rid A R). apply owns_r_inert.
    + rewrite (rcv_empty_bus b cl rid A R). reflexivity.
  - rewrite (rcv_bad_cursor_bus b cl rid A). reflexivity.
Qed.

(* what each receipt says about the queue *)
Lemma rcv_empty_means_nothing_pending : forall b cl rid,
    rcv_admit b cl rid = true -> rcv_ready b rid = false ->
    w_enq (b_w b (r_wid (b_r b rid))) <= r_cur (b_r b rid).
Proof.
  intros b cl rid A R. unfold rcv_ready in R.
  rewrite Nat.ltb_ge in R. exact R.
Qed.

Lemma rcv_data_means_pending : forall b cl rid,
    rcv_admit b cl rid = true -> rcv_ready b rid = true ->
    r_cur (b_r b rid) < w_enq (b_w b (r_wid (b_r b rid))).
Proof.
  intros b cl rid A R. unfold rcv_ready in R.
  rewrite Nat.ltb_lt in R. exact R.
Qed.

Lemma rcv_ready_gives_data : forall b cl rid,
    rcv_admit b cl rid = true -> rcv_ready b rid = true ->
    exists d, pr2 (rcv b cl rid) = R_DATA d.
Proof.
  intros b cl rid A R.
  exists (hist_at b (r_wid (b_r b rid)) (r_cur (b_r b rid))).
  apply (rcv_data_receipt b cl rid A R).
Qed.

Lemma rcv_empty_is_not_data : forall b cl rid,
    rcv_admit b cl rid = true -> rcv_ready b rid = false ->
    forall d, pr2 (rcv b cl rid) <> R_DATA d.
Proof.
  intros b cl rid A R d H.
  rewrite (rcv_empty_receipt b cl rid A R) in H. discriminate H.
Qed.

Lemma rcv_data_means_ready : forall b cl rid,
    rcv_admit b cl rid = true -> (exists d, pr2 (rcv b cl rid) = R_DATA d) ->
    rcv_ready b rid = true.
Proof.
  intros b cl rid A Hex.
  destruct (rcv_ready b rid) eqn:R; [reflexivity|].
  exfalso. destruct Hex as [d Hd].
  rewrite (rcv_empty_receipt b cl rid A R) in Hd. discriminate Hd.
Qed.

(* totality: every call returns one of the three states, so no fourth
 * state (a wait, a yield, a block) exists in the contract *)
Lemma rcv_receipt_exhaustive : forall b cl rid,
    pr2 (rcv b cl rid) = R_BAD_CURSOR \/ pr2 (rcv b cl rid) = R_EMPTY
    \/ (exists d, pr2 (rcv b cl rid) = R_DATA d).
Proof.
  intros b cl rid.
  destruct (rcv_admit b cl rid) eqn:A.
  - destruct (rcv_ready b rid) eqn:R.
    + right; right. apply (rcv_ready_gives_data b cl rid A R).
    + right; left. apply (rcv_empty_receipt b cl rid A R).
  - left. apply (rcv_bad_cursor_receipt b cl rid A).
Qed.

(* strictly non-blocking: EMPTY is an immediate answer, not a wait state *)
Lemma rcv_never_yields : forall b cl rid,
    rcv_admit b cl rid = true -> rcv_ready b rid = false ->
    fst (rcv b cl rid) = b /\ pr2 (rcv b cl rid) = R_EMPTY.
Proof.
  intros b cl rid A R. split.
  - apply (rcv_empty_bus b cl rid A R).
  - apply (rcv_empty_receipt b cl rid A R).
Qed.

(* a read consumes exactly one item, and only a read consumes one *)
Lemma rcv_data_deq_one_more : forall b cl rid,
    rcv_admit b cl rid = true -> rcv_ready b rid = true ->
    r_start (b_r b rid) <= r_cur (b_r b rid) ->
    r_deq (b_r (fst (rcv b cl rid)) rid) = S (r_deq (b_r b rid)).
Proof.
  intros b cl rid A R Hst. rewrite (rcv_data_bus b cl rid A R), at_r_same_r.
  unfold r_deq. rewrite reader_advance_cur, reader_advance_start. lia.
Qed.

Lemma rcv_data_deq_grows : forall b cl rid, bus_wf b ->
    rcv_admit b cl rid = true -> rcv_ready b rid = true ->
    r_deq (b_r (fst (rcv b cl rid)) rid) = S (r_deq (b_r b rid)).
Proof.
  intros b cl rid Wb A R.
  destruct Wb as [_ [_ [Wr _]]].
  assert (B := Wr rid (rcv_admit_live_reader b cl rid A)).
  destruct B as [_ [_ [Hst _]]].
  apply rcv_data_deq_one_more; [ exact A | exact R | exact Hst ].
Qed.

Lemma rcv_refused_deq_unchanged : forall b cl rid,
    rcv_admit b cl rid = false ->
    r_deq (b_r (fst (rcv b cl rid)) rid) = r_deq (b_r b rid).
Proof.
  intros b cl rid A. rewrite (rcv_bad_cursor_bus b cl rid A). reflexivity.
Qed.

Lemma rcv_empty_deq_unchanged : forall b cl rid,
    rcv_admit b cl rid = true -> rcv_ready b rid = false ->
    r_deq (b_r (fst (rcv b cl rid)) rid) = r_deq (b_r b rid).
Proof.
  intros b cl rid A R. rewrite (rcv_empty_bus b cl rid A R). reflexivity.
Qed.
(* ── 11. I4: receipt integrity ──────────────────────────────────── *)
(* A subscriber's receipts are exactly the publisher's own items, from the
 * index it joined at, in publication order, one per R_DATA. Nothing is
 * invented, dropped or reordered by the protocol. *)

Lemma nth_length_app : forall (l : list nat) d, nth (length l) (l ++ [d]) 0 = d.
Proof.
  induction l as [|a t IH]; cbn [length app nth].
  - reflexivity.
  - apply IH.
Qed.

Lemma skipn_hist_length : forall b r,
    length (skipn (r_start r) (w_hist (b_w b (r_wid r))))
    = w_enq (b_w b (r_wid r)) - r_start r.
Proof. intros b r. rewrite skipn_length. reflexivity. Qed.

Lemma received_is_a_slice : forall b r n, r_deq r = n ->
    received b r = firstn n (skipn (r_start r) (w_hist (b_w b (r_wid r)))).
Proof. intros b r n H. unfold received. rewrite H. reflexivity. Qed.

Lemma received_length_le : forall b r, length (received b r) <= r_deq r.
Proof. intros b r. unfold received. apply firstn_le_length. Qed.

Lemma received_complete : forall b r,
    r_deq r <= w_enq (b_w b (r_wid r)) - r_start r ->
    length (received b r) = r_deq r.
Proof.
  intros b r H. unfold received. apply firstn_length_le.
  rewrite skipn_length. exact H.
Qed.

(* every received item is a published item, at a computable index *)
Lemma received_item_is_published : forall b r k, k < length (received b r) ->
    nth k (received b r) 0 = hist_at b (r_wid r) (r_start r + k).
Proof.
  intros b r k Hk.
  assert (Hl : k < r_deq r).
  { apply Nat.lt_le_trans with (length (received b r)); [| unfold received; apply firstn_le_length].
    exact Hk. }
  unfold received, hist_at.
  rewrite (nth_firstn _ _ _ _ Hl). apply nth_skipn.
Qed.

(* a fresh subscriber has received nothing: it joins at the tail *)
Lemma received_of_fresh : forall b wid e,
    received b (mk_reader true wid e e) = nil.
Proof.
  intros b wid e. unfold received, r_deq. rewrite Nat.sub_diag. cbn. reflexivity.
Qed.

(* each R_DATA appends exactly the item that was under the cursor *)
Lemma rcv_appends_the_item_read : forall b cl rid, bus_wf b ->
    rcv_admit b cl rid = true -> rcv_ready b rid = true ->
    received (fst (rcv b cl rid)) (b_r (fst (rcv b cl rid)) rid)
      = received b (b_r b rid)
        ++ [hist_at b (r_wid (b_r b rid)) (r_cur (b_r b rid))].
Proof.
  intros b cl rid Wb A R.
  assert (Hst : r_start (b_r b rid) <= r_cur (b_r b rid)).
  { destruct Wb as [_ [_ [Wr _]]].
    assert (B := Wr rid (rcv_admit_live_reader b cl rid A)).
    destruct B as [_ [_ [H1 _]]]. exact H1. }
  assert (Hp : r_cur (b_r b rid) < w_enq (b_w b (r_wid (b_r b rid))))
    by (apply (rcv_data_means_pending b cl rid A R)).
  assert (Hlen : r_cur (b_r b rid) - r_start (b_r b rid)
                 < length (skipn (r_start (b_r b rid)) (w_hist (b_w b (r_wid (b_r b rid)))))).
  { rewrite skipn_hist_length. lia. }
  rewrite (rcv_data_bus b cl rid A R), at_r_same_r.
  unfold received, r_deq, hist_at.
  rewrite !reader_advance_cur, !reader_advance_start, !reader_advance_wid, !at_w_bus_r.
  replace (S (r_cur (b_r b rid)) - r_start (b_r b rid))
    with (S (r_cur (b_r b rid) - r_start (b_r b rid))) by lia.
  rewrite (firstn_S_app _ _ 0) by exact Hlen.
  assert (HT : nth (r_cur (b_r b rid) - r_start (b_r b rid))
                  (skipn (r_start (b_r b rid)) (w_hist (b_w b (r_wid (b_r b rid))))) 0
                = nth (r_cur (b_r b rid)) (w_hist (b_w b (r_wid (b_r b rid)))) 0).
  { rewrite nth_skipn.
    replace (r_start (b_r b rid) + (r_cur (b_r b rid) - r_start (b_r b rid)))
      with (r_cur (b_r b rid)) by lia. reflexivity. }
  rewrite HT. reflexivity.
Qed.

(* the send of one core is the receive of the next: values cross the star
 * intact, with no broker in between *)
Lemma snd_then_rcv_returns_the_item : forall b cl wid rid d,
    snd_admit b cl wid = true -> snd_fits b wid = true ->
    r_live (b_r b rid) = true -> r_wid (b_r b rid) = wid ->
    usable b cl (CSub rid) = true -> r_cur (b_r b rid) = w_enq (b_w b wid) ->
    pr2 (rcv (fst (snd b cl wid d)) cl rid) = R_DATA d.
Proof.
  intros b cl wid rid d A F Hr Hwid Hu Hcur.
  rewrite (snd_ok_bus b cl wid d A F).
  assert (A' : rcv_admit (bus_w b wid (writer_push (b_w b wid) d)) cl rid = true).
  { unfold rcv_admit.
    rewrite at_r_bus_w, Hwid, at_w_same_w, writer_push_live, usable_w_inert, Hu, Hr.
    reflexivity. }
  assert (R' : rcv_ready (bus_w b wid (writer_push (b_w b wid) d)) rid = true).
  { unfold rcv_ready.
    rewrite at_r_bus_w, Hwid, at_w_same_w, writer_push_enq, Hcur.
    apply Nat.ltb_lt. lia. }
  rewrite (rcv_data_receipt _ _ rid A' R').
  unfold hist_at.
  rewrite at_r_bus_w, Hwid, at_w_same_w, writer_push_hist, Hcur.
  unfold w_enq. rewrite nth_length_app. reflexivity.
Qed.

(* a reader that is not admitted reads nothing: the receipt says why *)
Lemma rcv_stranger_receives_nothing : forall b t rid,
    owns b t (CSub rid) = false ->
    received (fst (rcv b (ByTask t) rid)) (b_r (fst (rcv b (ByTask t) rid)) rid)
      = received b (b_r b rid).
Proof.
  intros b t rid H.
  assert (A : rcv_admit b (ByTask t) rid = false)
    by (apply rcv_refuses_a_stranger; exact H).
  rewrite (rcv_bad_cursor_bus b (ByTask t) rid A). reflexivity.
Qed.
(* ── 12. I5: the star is the topology: no broker, one queue per core *)

Lemma nat_eqb_false : forall x y, x <> y -> (x =? y) = false.
Proof. intros x y H. apply (proj2 (Nat.eqb_neq _ _)). exact H. Qed.

Lemma andb_false_right : forall a b, b = false -> andb a b = false.
Proof. intros a b E. rewrite E. destruct a; reflexivity. Qed.

Lemma in_nat_list_lt : forall i len, In i (nat_list len) -> i < len.
Proof.
  intros i len. induction len as [|k IH]; intros Hin.
  - inversion Hin.
  - cbn [nat_list] in Hin. destruct Hin as [He|Hin'].
    + rewrite <- He. lia.
    + apply IH in Hin'. lia.
Qed.

Lemma existsb_none : forall (f : nat -> bool) l,
    (forall x, In x l -> f x = false) -> existsb f l = false.
Proof.
  intros f l. induction l as [|a t IH]; intros H; cbn [existsb].
  - reflexivity.
  - destruct (f a) eqn:Fa.
    + exfalso.
      assert (Hf : f a = false) by (apply (H a); left; reflexivity).
      rewrite Fa in Hf. discriminate Hf.
    + apply IH. intros x Hin. apply H. right. exact Hin.
Qed.

(* The data path never touches the task table: there is no broker process
 * to schedule, no shared queue to lock. *)
Lemma snd_tasks_untouched : forall b cl wid d j,
    b_t (fst (snd b cl wid d)) j = b_t b j.
Proof.
  intros b cl wid d j.
  destruct (snd_admit b cl wid) eqn:A.
  - destruct (snd_fits b wid) eqn:F.
    + rewrite (snd_ok_bus b cl wid d A F). cbn [fst]. apply at_t_bus_w.
    + rewrite (snd_full_bus b cl wid d A F). cbn [fst]. apply at_t_bus_w.
  - rewrite (snd_bad_cursor_bus b cl wid d A). reflexivity.
Qed.

Lemma pub_readers_untouched : forall b cap j,
    b_r (fst (pub b cap)) j = b_r b j.
Proof.
  intros b cap j. unfold pub. destruct (pub_slot b) eqn:S.
  - cbn [fst]. apply at_r_bus_w.
  - reflexivity.
Qed.

Lemma spawn_writers_untouched : forall b core l j,
    b_w (fst (spawn b core l)) j = b_w b j.
Proof.
  intros b core l j. unfold spawn. destruct (spawn_slot b) eqn:S.
  - destruct (spawn_ok b core l) eqn:O.
    + cbn [fst]. apply at_w_bus_t.
    + reflexivity.
  - reflexivity.
Qed.

Lemma spawn_readers_untouched : forall b core l j,
    b_r (fst (spawn b core l)) j = b_r b j.
Proof.
  intros b core l j. unfold spawn. destruct (spawn_slot b) eqn:S.
  - destruct (spawn_ok b core l) eqn:O.
    + cbn [fst]. apply at_r_bus_t.
    + reflexivity.
  - reflexivity.
Qed.

(* a reader pins only its own publisher: the watermark fold is per queue.
 * installing a reader that does not pin wid into a slot that was already
 * dead cannot move wid's watermark: the guard is false on both sides, so
 * the cursor values never have to agree. *)
Lemma andb_false_left : forall a b, a = false -> andb a b = false.
Proof. intros a b E. rewrite E. destruct b; reflexivity. Qed.

Lemma floor_of_dead_slot_inert : forall b rid (r : reader) wid e,
    r_live (b_r b rid) = false ->
    r_wid r <> wid -> floor_of (bus_r b rid r) wid e = floor_of b wid e.
Proof.
  intros b rid r wid e Hd H. unfold floor_of.
  apply (min_fold_cong_if _ _ _ _ max_readers 0 e).
  intros j Le Hj. rewrite !bus_r_apply.
  destruct (Nat.eqb rid j) eqn:R.
  - apply Nat.eqb_eq in R. subst j. split.
    + unfold sub_reader. rewrite Hd, (andb_false_right _ _ (nat_eqb_false _ _ H)).
      reflexivity.
    + intros K. unfold sub_reader in K. rewrite Hd in K. discriminate.
  - split; reflexivity.
Qed.

Lemma held_dead_slot_inert : forall b rid (r : reader) wid,
    r_live (b_r b rid) = false ->
    r_wid r <> wid -> held (bus_r b rid r) wid = held b wid.
Proof.
  intros b rid r wid Hd H. unfold held.
  rewrite at_w_bus_r, (floor_of_dead_slot_inert b rid r wid (w_enq (b_w b wid)) Hd H).
  reflexivity.
Qed.

(* an unread publisher queue costs the pool nothing *)
Lemma unread_publisher_holds_nothing : forall b wid,
    (forall r, r < max_readers -> r_live (b_r b r) = true -> r_wid (b_r b r) <> wid) ->
    held b wid = 0.
Proof. intros b wid H. apply held_zero_when_unread. exact H. Qed.

(* the shipped MPSC row: at most one publisher cursor per task, at most one
 * publisher-holding task per core *)
Lemma one_publisher_per_task : forall b t, bus_wf b -> t_live (b_t b t) = true ->
    pub_count_c (t_curs (b_t b t)) <= 1.
Proof.
  intros b t Wb H. destruct Wb as [_ [_ [_ [Wk _]]]].
  assert (B := Wk t H). destruct B as [_ [_ [_ [_ [Hpc _]]]]]. exact Hpc.
Qed.

Lemma a_task_has_work : forall b t, bus_wf b -> t_live (b_t b t) = true ->
    length (t_curs (b_t b t)) <> 0.
Proof.
  intros b t Wb H. destruct Wb as [_ [_ [_ [Wk _]]]].
  assert (B := Wk t H). destruct B as [_ [_ [_ [_ [_ Hn]]]]]. exact Hn.
Qed.

Lemma publisher_per_core_is_unique : forall b t1 t2, bus_wf b ->
    holds_pub b t1 = true -> holds_pub b t2 = true ->
    t_core (b_t b t1) = t_core (b_t b t2) -> t1 = t2.
Proof.
  intros b t1 t2 Wb H1 H2 Hc. destruct Wb as [_ [_ [_ [_ [_ Ws]]]]].
  apply (Ws t1 t2); [exact H1|exact H2|exact Hc].
Qed.

Lemma core_holds_publisher_witness : forall b core t, t < max_tasks ->
    holds_pub b t = true -> t_core (b_t b t) = core ->
    core_holds_publisher b core = true.
Proof.
  intros b core t Lt Hp Hc. unfold core_holds_publisher.
  apply (existsb_true (fun u => andb (holds_pub b u) (Nat.eqb (t_core (b_t b u)) core))
                      (nat_list max_tasks) t).
  - apply in_nat_list. exact Lt.
  - rewrite Hp, Hc, Nat.eqb_refl. reflexivity.
Qed.

Lemma core_holds_publisher_none : forall b core,
    (forall t, t < max_tasks -> holds_pub b t = false \/ t_core (b_t b t) <> core) ->
    core_holds_publisher b core = false.
Proof.
  intros b core H. unfold core_holds_publisher. apply existsb_none.
  intros t Hin. destruct (H t (in_nat_list_lt t max_tasks Hin)) as [F|Ne].
  - rewrite F. reflexivity.
  - apply (andb_false_right _ _ (nat_eqb_false _ _ Ne)).
Qed.

(* star admission: a subscriber-only task is always shaped right, and a
 * second publisher on a core that already holds one is refused *)
Lemma star_ok_allows_a_subscriber_only_task : forall b core l,
    pub_count_c l = 0 -> star_ok b core l = true.
Proof. intros b core l H. unfold star_ok. rewrite H, Nat.eqb_refl. reflexivity. Qed.

Lemma star_ok_refuses_a_second_publisher : forall b core l t, t < max_tasks ->
    holds_pub b t = true -> t_core (b_t b t) = core -> pub_count_c l <> 0 ->
    star_ok b core l = false.
Proof.
  intros b core l t Lt Hp Hc Hn. unfold star_ok.
  assert (E : Nat.eqb (pub_count_c l) 0 = false)
    by (destruct (Nat.eqb_spec (pub_count_c l) 0); [contradiction|reflexivity]).
  rewrite E. cbn [negb].
  rewrite (core_holds_publisher_witness b core t Lt Hp Hc). reflexivity.
Qed.

Lemma star_ok_when_core_is_free : forall b core l,
    (forall t, t < max_tasks -> holds_pub b t = false \/ t_core (b_t b t) <> core) ->
    pub_count_c l <> 0 -> star_ok b core l = true.
Proof.
  intros b core l H Hn. unfold star_ok.
  assert (E : Nat.eqb (pub_count_c l) 0 = false)
    by (destruct (Nat.eqb_spec (pub_count_c l) 0); [contradiction|reflexivity]).
  rewrite E. cbn [negb].
  rewrite (core_holds_publisher_none b core H). reflexivity.
Qed.
End MediaInterCore.
