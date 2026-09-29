(* media_intercore_properties.v
 *
 * Modal (multi-core) properties of the InterCore protocol: the pub / sub /
 * spawn / snd / rcv surface that realises the Zero-Switch Actor Topology under
 * the ASYNC planes, plus the EVT event ring that the I/O plane shares with it.
 *
 * Normative sources: doc/txt/ASYNC.txt §0.1 for the canonical taxonomy,
 * doc/txt/SMP-AMP-SYNC-IO-HARDENING.txt §7.3 for the R7 resolutions that
 * supersede it and §7.4 for the metric rows, doc/txt/IO.txt §5 for the EVT
 * queue.  The companion executable oracle is media_intercore_model.ml.
 *
 * Layout.  §1-§7 fix the bus and the five calls.  §8-§18 (I1-I9) are the modal
 * part: the cursor tables, the three states of snd, the totality of rcv, receipt
 * integrity, the star, the three shapes of spawn, ownership as the mutex that is
 * not there, conservation of sectors, and the class of states the five calls
 * cannot leave - §18.7 closes it with the theorem that no word over the five
 * calls escapes that class, which is what media_intercore_model.ml §12 measures
 * by enumerating 262,144 interleavings.  §19 (I10) is the shipped-mode gate
 * written as arithmetic on the contract constants, including the honest FAIL of
 * --require=amp on a one-core port and the four SKIP rows of MP_OFF.  §20-§21
 * (I11) model the EVT queue as a masked power-of-two ring and run its 600-step
 * soak.
 *
 * Scope of what is proved.  §14-§18 prove properties a single-core build cannot
 * express: the relation between two cursors, two cores, or one core running
 * while another is parked.  That is what "modal" means in the file name - Tier 1
 * unicore versus Tier 2 SMP, and the BTRON_MP gate between them.  §19 is
 * deliberately not modal: it is the arithmetic that decides which gate row can
 * pass at all, so that a row cannot report coverage the port does not have.
 * §20-§21 are single-producer/single-consumer, because that is the shape the I/O
 * plane actually uses; the two-writer case is where the ring would break, and
 * the rows say so.
 *
 * Four deliberate modelling choices, which the oracle does NOT share:
 *
 *   - in Coq, w_enq is LENGTH w_hist and r_deq is r_cur - r_start, so the
 *     publisher accounting and the delivered count are tautologies.  The C
 *     implementation stores both numbers, so media_intercore_model.ml keeps
 *     them as runtime checks.  Here they become definitional and the proof
 *     effort goes where the risk actually is: ownership and conservation.
 *
 *   - in Coq, a reader's receipts are DERIVED from its cursor (received b r),
 *     while the oracle keeps a receipt log and checks it against the slice.
 *     Deriving it makes the receipt-integrity group (I4) theorems about the
 *     cursor protocol rather than about a bookkeeping field, which is the
 *     property the planes actually depend on.
 *
 *   - the step alphabet of §18.7 carries the two contract clauses that the
 *     shipped guards do not check: pub rejects a cap outside 1..ring_capacity,
 *     and spawn rejects a cursor another live task already owns.  The shipped
 *     guards accept both, so the gap is in the guards, not in the model: a run
 *     of the modelled steps is a run of the protocol as specified, not as
 *     shipped.
 *
 *   - in Coq the EVT index is k mod event_queue_size, while the shipped C masks
 *     it with (k & (N-1)).  §20 proves the two agree, and proves that they agree
 *     because N is a power of two - the row that makes the missing divide safe
 *     rather than lucky.
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

(* ── 2. The three tables and the two cursor kinds ─────────────────── *)

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

(* A writer slot that is not live holds no sector.  The table-first calls only
 * ever revive a dead slot, so this is what makes a revived ring start empty:
 * without it, a stale history would be charged against the new ring's cap. *)
Definition wf_retired b :=
  forall j, w_live (b_w b j) = false -> w_enq (b_w b j) = 0 /\ w_hist (b_w b j) = nil.

Definition bus_wf b :=
  wf_tables b /\ wf_bounded b /\ wf_reader_bounds b /\
  wf_tasks b /\ wf_ownership b /\ wf_star b /\ wf_retired b.

Lemma bus_wf0 : bus_wf bus0.
Proof.
  unfold bus_wf, wf_tables, wf_bounded, wf_reader_bounds, wf_tasks,
         wf_ownership, wf_star, wf_retired, holds_pub, owns, pub_count_c, held, w_enq.
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

(* ── 12. I3: rcv is total, and strictly non-blocking ─────────────── *)

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

(* ── 13. I4: receipt integrity ──────────────────────────────────── *)
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

(* ── 14. I5: the star is the topology: no broker, one queue per core *)

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
  intros b t1 t2 Wb H1 H2 Hc. destruct Wb as [_ [_ [_ [_ [_ [Ws _]]]]]].
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

(* ── 15. I6: spawn validates the core, the shape, and the grant ───────── *)
(*
 * The oracle's I6 group asserts, for each refusal, both the receipt and the
 * identity `b' = !sb0`.  The Coq counterpart cannot name the oracle's
 * T_BAD_CORE / T_BAD_CURSOR / T_CURSOR_TAKEN / T_SHARED_PUBLISHER split --
 * the model collapses every refusal into `None`, because the R7 resolution
 * asks only that a refusal be distinguishable and harmless.  What is proved
 * here is the harmless half, in full, and the distinguishable half as far as
 * the collapsed receipt allows: one lemma per refusal reason, each of the
 * form "this state cannot be granted", plus the shape of a grant.
 *)

Lemma task_revive_live : forall core l, t_live (task_revive core l) = true.
Proof. reflexivity. Qed.

Lemma task_revive_core : forall core l, t_core (task_revive core l) = core.
Proof. reflexivity. Qed.

Lemma task_revive_curs : forall core l, t_curs (task_revive core l) = l.
Proof. reflexivity. Qed.

Lemma cref_eqb_true : forall c, cref_eqb c c = true.
Proof. destruct c; cbn [cref_eqb]; apply Nat.eqb_refl. Qed.

Lemma forallb_all : forall (A : Type) (f : A -> bool) (l : list A),
    (forall x, In x l -> f x = true) -> forallb f l = true.
Proof.
  intros A f l. induction l as [|a t IH]; intros H; cbn [forallb].
  - reflexivity.
  - rewrite (H a (or_introl eq_refl)). apply IH. intros x Hin. apply H. right. exact Hin.
Qed.

Lemma forallb_elim : forall (A : Type) (f : A -> bool) (l : list A) x,
    forallb f l = true -> In x l -> f x = true.
Proof.
  intros A f l. induction l as [|a t IH]; intros x H Hin; cbn [forallb] in H.
  - contradiction.
  - apply andb_true_iff in H. destruct H as [Ha Ht].
    destruct Hin as [He|Hin']; [subst x; exact Ha| apply IH; auto].
Qed.

Lemma cursors_live_elim : forall b l c,
    cursors_live b l = true -> In c l -> cursor_live b c = true.
Proof.
  intros b l. induction l as [|a t IH]; intros c H Hin; cbn [cursors_live] in H.
  - contradiction.
  - apply andb_true_iff in H. destruct H as [Ha Ht].
    destruct Hin as [He|Hin']; [subst a; exact Ha| apply IH; auto].
Qed.

(* polymorphic existsb introduction: owns/usable search a list of cref, not nat *)
Lemma existsb_here_true : forall (A : Type) (f : A -> bool) (l : list A) x,
    In x l -> f x = true -> existsb f l = true.
Proof.
  intros A f l x. induction l as [|a t IH]; intros Hin Hf; cbn [existsb].
  - contradiction.
  - destruct Hin as [Hx|Hin'].
    + subst a. rewrite Hf. reflexivity.
    + destruct (f a); [reflexivity|apply IH; auto].
Qed.

(* a task write cannot revive a cursor: liveness lives in the cell tables *)
Lemma cursor_live_bus_t : forall b i (t : task) c, cursor_live (bus_t b i t) c = cursor_live b c.
Proof. intros b i t c. destruct c; reflexivity. Qed.

(* ── 15.1 the three shapes of a spawn ────────────────────────────────── *)

Lemma spawn_no_slot_shape : forall b core l,
    spawn_slot b = None -> spawn b core l = (b, None).
Proof. intros b core l H. unfold spawn. rewrite H. reflexivity. Qed.

Lemma spawn_refused_shape : forall b core l i,
    spawn_slot b = Some i -> spawn_ok b core l = false -> spawn b core l = (b, None).
Proof. intros b core l i H K. unfold spawn. rewrite H, K. reflexivity. Qed.

Lemma spawn_granted_shape : forall b core l i,
    spawn_slot b = Some i -> spawn_ok b core l = true ->
    spawn b core l = (bus_t b i (task_revive core l), Some i).
Proof. intros b core l i H K. unfold spawn. rewrite H, K. reflexivity. Qed.

Lemma spawn_grant_is_the_slot : forall b core l i,
    spawn_slot b = Some i -> spawn_ok b core l = true -> pr2 (spawn b core l) = Some i.
Proof.
  intros b core l i H K. unfold spawn. rewrite H, K. reflexivity.
Qed.

(* the collapsed receipt: a full task table refuses, and refuses silently *)
Lemma spawn_without_a_slot_refuses : forall b core l,
    spawn_slot b = None -> pr2 (spawn b core l) = None.
Proof. intros b core l H. rewrite (spawn_no_slot_shape b core l H). reflexivity. Qed.

Lemma spawn_slot_bounded : forall b i, spawn_slot b = Some i -> i < max_tasks.
Proof.
  intros b i H. unfold spawn_slot in H.
  apply first_dead_Some_lt with (fuel := max_tasks) (i := 0) in H. lia.
Qed.

Lemma spawn_slot_offers_a_dead_slot : forall b i,
    spawn_slot b = Some i -> t_live (b_t b i) = false.
Proof.
  intros b i H. unfold spawn_slot in H.
  apply first_dead_Some_free with (fuel := max_tasks) (i := 0) (o := i) in H. exact H.
Qed.

(* ── 15.2 what an admitted grant implies, conjunct by conjunct ───────── *)

(* One decomposition of the guard, done by hand: `repeat (apply andb_true_iff in S)`
 * on this six-deep conjunction enumerates hypotheses quadratically and stalls the
 * kernel, so the shape is extracted once and every elimination reads it off. *)
Lemma spawn_ok_six : forall b core l, spawn_ok b core l = true ->
    (core <? b_cores b) = true /\
    negb (Nat.eqb (length l) 0) = true /\
    cursors_live b l = true /\
    (forallb (fun c => negb (cursor_taken b c)) l = true) /\
    (pub_count_c l <=? 1) = true /\
    star_ok b core l = true.
Proof.
  intros b core l S. unfold spawn_ok in S.
  apply andb_true_iff in S. destruct S as [H1 S].
  apply andb_true_iff in S. destruct S as [H2 S].
  apply andb_true_iff in S. destruct S as [H3 S].
  apply andb_true_iff in S. destruct S as [H4 S].
  apply andb_true_iff in S. destruct S as [H5 H6].
  split; [exact H1|split;[exact H2|split;[exact H3|split;[exact H4|split;[exact H5|exact H6]]]]].
Qed.

Lemma spawn_ok_core_exists : forall b core l,
    spawn_ok b core l = true -> core < b_cores b.
Proof.
  intros b core l S. destruct (spawn_ok_six b core l S) as [Hc _].
  apply Nat.ltb_lt in Hc. exact Hc.
Qed.

Lemma spawn_ok_grants_work : forall b core l,
    spawn_ok b core l = true -> length l <> 0.
Proof.
  intros b core l S. destruct (spawn_ok_six b core l S) as [_ [Hlen _]].
  apply negb_true_iff in Hlen.
  apply (proj1 (Nat.eqb_neq (length l) 0)) in Hlen. exact Hlen.
Qed.

Lemma spawn_ok_cursors_live : forall b core l,
    spawn_ok b core l = true -> cursors_live b l = true.
Proof.
  intros b core l S. destruct (spawn_ok_six b core l S) as [_ [_ [Hlive _]]]. exact Hlive.
Qed.

Lemma spawn_ok_one_publisher_per_task : forall b core l,
    spawn_ok b core l = true -> pub_count_c l <= 1.
Proof.
  intros b core l S. destruct (spawn_ok_six b core l S) as [_ [_ [_ [_ [Hpub _]]]]].
  apply Nat.leb_le in Hpub. exact Hpub.
Qed.

Lemma spawn_ok_cursors_free : forall b core l c,
    spawn_ok b core l = true -> In c l -> cursor_taken b c = false.
Proof.
  intros b core l c S Hin. destruct (spawn_ok_six b core l S) as [_ [_ [_ [Hfree _]]]].
  apply negb_true_iff. apply (forallb_elim _ _ l c Hfree Hin).
Qed.

Lemma spawn_ok_star_holds : forall b core l,
    spawn_ok b core l = true -> star_ok b core l = true.
Proof.
  intros b core l S. destruct (spawn_ok_six b core l S) as [_ [_ [_ [_ [_ Hstar]]]]]. exact Hstar.
Qed.

(* ── 15.3 the six refusal reasons, each leaving the bus untouched ───── *)

(* a live-listed cursor that is dead makes the whole grant dead *)
Lemma cursors_live_false : forall b l c,
    In c l -> cursor_live b c = false -> cursors_live b l = false.
Proof.
  intros b l c. induction l as [|a t IH]; intros Hin Hdead; cbn [cursors_live].
  - contradiction.
  - destruct Hin as [He|Hin'].
    + subst a. rewrite Hdead. reflexivity.
    + destruct (cursor_live b a) eqn:Ha; cbn [andb].
      * exact (IH Hin' Hdead).
      * reflexivity.
Qed.

(* each refusal is discharged by pointing at the conjunct that fails, so the
 * guard is read as data rather than exploded into hypotheses *)
Lemma spawn_not_ok_when_core_absent : forall b core l,
    b_cores b <= core -> spawn_ok b core l = false.
Proof.
  intros b core l H. unfold spawn_ok. apply andb_false_left.
  apply Nat.ltb_ge. exact H.
Qed.

Lemma spawn_not_ok_on_an_empty_grant : forall b core l,
    length l = 0 -> spawn_ok b core l = false.
Proof.
  intros b core l H. unfold spawn_ok. apply andb_false_right. apply andb_false_left.
  rewrite H. reflexivity.
Qed.

Lemma spawn_not_ok_on_a_dead_cursor : forall b core l c,
    In c l -> cursor_live b c = false -> spawn_ok b core l = false.
Proof.
  intros b core l c Hin Hdead. unfold spawn_ok.
  apply andb_false_right. apply andb_false_right. apply andb_false_left.
  apply (cursors_live_false b l c Hin Hdead).
Qed.

Lemma spawn_not_ok_on_a_taken_cursor : forall b core l c,
    In c l -> cursor_taken b c = true -> spawn_ok b core l = false.
Proof.
  intros b core l c Hin Ht. unfold spawn_ok.
  apply andb_false_right. apply andb_false_right. apply andb_false_right. apply andb_false_left.
  apply (forallb_here_false _ _ l c Hin). rewrite Ht. reflexivity.
Qed.

Lemma spawn_not_ok_with_two_publishers : forall b core l,
    2 <= pub_count_c l -> spawn_ok b core l = false.
Proof.
  intros b core l Hn. unfold spawn_ok.
  apply andb_false_right. apply andb_false_right. apply andb_false_right.
  apply andb_false_right. apply andb_false_left.
  apply (proj2 (Nat.leb_gt (pub_count_c l) 1)). lia.
Qed.

Lemma spawn_not_ok_on_a_held_core : forall b core l t,
    t < max_tasks -> holds_pub b t = true -> t_core (b_t b t) = core ->
    pub_count_c l <> 0 -> spawn_ok b core l = false.
Proof.
  intros b core l t Lt Hp Hc Hn. unfold spawn_ok.
  apply andb_false_right. apply andb_false_right. apply andb_false_right.
  apply andb_false_right. apply andb_false_right.
  apply (star_ok_refuses_a_second_publisher b core l t Lt Hp Hc Hn).
Qed.

(* ── 15.4 an admission, when it happens, is exactly the request ──────── *)

Lemma spawn_ok_grants_a_clean_actor : forall b core l,
    core < b_cores b -> length l <> 0 -> pub_count_c l = 0 ->
    cursors_live b l = true ->
    (forall c, In c l -> cursor_taken b c = false) ->
    spawn_ok b core l = true.
Proof.
  intros b core l Hcore Hlen Hpub Hlive Hfree. unfold spawn_ok.
  assert (E1 : (core <? b_cores b) = true)
    by (apply (proj2 (Nat.ltb_lt core (b_cores b))); exact Hcore).
  assert (E2 : (length l =? 0) = false)
    by (apply (proj2 (Nat.eqb_neq (length l) 0)); exact Hlen).
  assert (E3 : (pub_count_c l <=? 1) = true).
  { apply Nat.leb_le. rewrite Hpub. lia. }
  assert (E4 : forallb (fun c => negb (cursor_taken b c)) l = true).
  { apply forallb_all. intros c Hin. apply negb_true_iff. apply (Hfree c Hin). }
  rewrite E1, E2, Hlive, E4, E3, (star_ok_allows_a_subscriber_only_task b core l Hpub).
  reflexivity.
Qed.

Lemma spawn_grant_is_live_on_the_named_core : forall b core l i,
    spawn_slot b = Some i -> spawn_ok b core l = true ->
    let b' := fst (spawn b core l) in
    t_live (b_t b' i) = true /\ t_core (b_t b' i) = core /\ t_curs (b_t b' i) = l.
Proof.
  intros b core l i S Q. rewrite (spawn_granted_shape b core l i S Q). cbn [fst].
  split.
  - rewrite at_t_same_t, task_revive_live. reflexivity.
  - split.
    + rewrite at_t_same_t, task_revive_core. reflexivity.
    + rewrite at_t_same_t, task_revive_curs. reflexivity.
Qed.

Lemma spawn_grants_ownership : forall b core l i c,
    spawn_slot b = Some i -> spawn_ok b core l = true -> In c l ->
    owns (fst (spawn b core l)) i c = true.
Proof.
  intros b core l i c S Q Hin.
  rewrite (spawn_granted_shape b core l i S Q). cbn [fst].
  unfold owns. rewrite at_t_same_t, task_revive_curs, task_revive_live. cbn [andb].
  apply (existsb_here_true _ _ l c Hin). apply cref_eqb_true.
Qed.

Lemma spawn_other_tasks_untouched : forall b core l i j,
    spawn_slot b = Some i -> i <> j -> b_t (fst (spawn b core l)) j = b_t b j.
Proof.
  intros b core l i j S Hij.
  destruct (spawn_ok b core l) eqn:Q.
  - rewrite (spawn_granted_shape b core l i S Q). cbn [fst].
    rewrite at_t_other_t by exact Hij. reflexivity.
  - rewrite (spawn_refused_shape b core l i S Q). reflexivity.
Qed.

(* a spawn touches no ring, so it spends and returns no sector: the pool is a
 * property of the writer table and spawn does not read from it *)
Lemma total_held_bus_t : forall b i (t : task), total_held (bus_t b i t) = total_held b.
Proof.
  intros b i t. unfold total_held. apply sum_f_pointwise. intros j Lt.
  unfold held. rewrite at_w_bus_t. reflexivity.
Qed.

Lemma spawn_preserves_the_pool : forall b core l,
    pool_free (fst (spawn b core l)) = pool_free b.
Proof.
  intros b core l. unfold pool_free.
  destruct (spawn_slot b) as [i|] eqn:S.
  - destruct (spawn_ok b core l) eqn:Q.
    + rewrite (spawn_granted_shape b core l i S Q). cbn [fst].
      rewrite (total_held_bus_t b i (task_revive core l)). reflexivity.
    + rewrite (spawn_refused_shape b core l i S Q). reflexivity.
  - rewrite (spawn_no_slot_shape b core l S). reflexivity.
Qed.

Lemma spawn_never_makes_a_core : forall b core l, b_cores (fst (spawn b core l)) = b_cores b.
Proof.
  intros b core l.
  destruct (spawn_slot b) as [i|] eqn:S.
  - destruct (spawn_ok b core l) eqn:Q.
    + rewrite (spawn_granted_shape b core l i S Q). cbn [fst]. apply cores_inert_t.
    + rewrite (spawn_refused_shape b core l i S Q). reflexivity.
  - rewrite (spawn_no_slot_shape b core l S). reflexivity.
Qed.

(* ── 16. I7: one cursor, one owner; the caller is the mutex ───────────── *)
(*
 * I7 is the safety half of the mutex-elimination claim of §5: no call can be
 * made through a cursor the caller does not hold, and two holders cannot
 * exist.  The oracle checks this by pairing a stranger with an owner on the
 * same cursor and reading the two receipts; the theory proves the general
 * statement, which subsumes every pair the search can build.
 *)

(* two cursors are equal exactly when cref_eqb says so *)
Lemma cref_eqb_iff : forall a b, cref_eqb a b = true -> a = b.
Proof.
  intros a b H. destruct a as [i|j]; destruct b as [i'|j']; cbn [cref_eqb] in H.
  - apply f_equal. apply (proj1 (Nat.eqb_eq i i')) in H. exact H.
  - discriminate H.
  - discriminate H.
  - apply f_equal. apply (proj1 (Nat.eqb_eq j j')) in H. exact H.
Qed.

(* ── 16.1 who may own ─────────────────────────────────────────────────── *)

Lemma owns_dead_task : forall b t c,
    t_live (b_t b t) = false -> owns b t c = false.
Proof.
  intros b t c H. unfold owns. rewrite H. cbn [andb]. reflexivity.
Qed.

Lemma owns_of_empty_task : forall b t c,
    b_t b t = empty_task -> owns b t c = false.
Proof. intros b t c H. unfold owns. rewrite H. reflexivity. Qed.

(* a holder makes its cursor taken: the witness needs the table bound, which
 * bus_wf supplies for every live task *)
Lemma cursor_taken_if_owned : forall b t c,
    t < max_tasks -> owns b t c = true -> cursor_taken b c = true.
Proof.
  intros b t c Lt H. unfold cursor_taken.
  apply (existsb_true (fun u => owns b u c) (nat_list max_tasks) t).
  - apply in_nat_list. exact Lt.
  - exact H.
Qed.

Lemma free_cursor_is_usable_anonymously : forall b c,
    cursor_taken b c = false -> usable b NoTask c = true.
Proof. intros b c H. unfold usable. rewrite H. reflexivity. Qed.

Lemma owned_cursor_is_not_usable_anonymously : forall b t c,
    t < max_tasks -> owns b t c = true -> usable b NoTask c = false.
Proof.
  intros b t c Lt H. unfold usable.
  rewrite (cursor_taken_if_owned b t c Lt H). reflexivity.
Qed.

(* ── 16.2 exclusivity ─────────────────────────────────────────────────── *)

Lemma one_owner_per_cursor : forall b t1 t2 c,
    bus_wf b -> owns b t1 c = true -> owns b t2 c = true -> t1 = t2.
Proof.
  intros b t1 t2 c W H1 H2. destruct W as [_ [_ [_ [_ [Wown _]]]]].
  apply (Wown t1 t2 c); [exact H1|exact H2].
Qed.

Lemma usable_is_exclusive : forall b t1 t2 c,
    bus_wf b -> usable b (ByTask t1) c = true -> usable b (ByTask t2) c = true -> t1 = t2.
Proof.
  intros b t1 t2 c W U1 U2.
  apply (one_owner_per_cursor b t1 t2 c W).
  - unfold usable in U1. exact U1.
  - unfold usable in U2. exact U2.
Qed.

(* the bound of a live task is a clause of wf_tasks, so a holder is in range *)
Lemma a_holder_is_in_range : forall b t c,
    bus_wf b -> owns b t c = true -> t < max_tasks.
Proof.
  intros b t c W H. destruct W as [_ [_ [_ [Wt _]]]].
  unfold owns in H. apply andb_true_iff in H. destruct H as [Live _].
  apply (Wt t Live).
Qed.

Lemma no_anonymous_use_of_an_owned_cursor : forall b t c,
    bus_wf b -> owns b t c = true -> usable b NoTask c = false.
Proof.
  intros b t c W H.
  apply (owned_cursor_is_not_usable_anonymously b t c (a_holder_is_in_range b t c W H) H).
Qed.

(* the anonymous caller and the owner can never both be admitted *)
Lemma anonymous_and_owner_are_disjoint : forall b t c,
    bus_wf b -> usable b NoTask c = true -> usable b (ByTask t) c = false.
Proof.
  intros b t c W A. destruct (owns b t c) eqn:H.
  - exfalso.
    assert (B : usable b NoTask c = false)
      by (apply (no_anonymous_use_of_an_owned_cursor b t c W H)).
    unfold usable in A, B. rewrite B in A. cbn [negb] in A. discriminate A.
  - unfold usable. exact H.
Qed.

(* ── 16.3 the stranger receipts, by call ──────────────────────────────── *)

Lemma snd_refused_for_a_non_owner : forall b t wid d,
    owns b t (CPub wid) = false -> snd b (ByTask t) wid d = (b, S_BAD_CURSOR).
Proof.
  intros b t wid d H. apply snd_bad_cursor_inert. unfold snd_admit, usable.
  apply andb_false_right. exact H.
Qed.

Lemma rcv_refused_for_a_non_owner : forall b t rid,
    owns b t (CSub rid) = false -> rcv b (ByTask t) rid = (b, R_BAD_CURSOR).
Proof.
  intros b t rid H. apply rcv_bad_cursor_shape. unfold rcv_admit, usable.
  apply andb_false_right. apply andb_false_right. exact H.
Qed.

Lemma sub_refused_for_a_non_owner : forall b t wid,
    owns b t (CPub wid) = false -> sub_admit b (ByTask t) wid = false.
Proof.
  intros b t wid H. unfold sub_admit. apply andb_false_right. unfold usable. exact H.
Qed.

Lemma a_dead_task_may_send_nothing : forall b t wid d,
    t_live (b_t b t) = false -> snd b (ByTask t) wid d = (b, S_BAD_CURSOR).
Proof.
  intros b t wid d H.
  apply (snd_refused_for_a_non_owner b t wid d).
  apply (owns_dead_task b t (CPub wid) H).
Qed.

Lemma a_dead_task_may_read_nothing : forall b t rid,
    t_live (b_t b t) = false -> rcv b (ByTask t) rid = (b, R_BAD_CURSOR).
Proof.
  intros b t rid H.
  apply (rcv_refused_for_a_non_owner b t rid).
  apply (owns_dead_task b t (CSub rid) H).
Qed.

(* ── 16.4 the owner's own privileges ──────────────────────────────────── *)

Lemma sub_admit_by_the_publisher_owner : forall b t wid,
    w_live (b_w b wid) = true -> owns b t (CPub wid) = true ->
    sub_admit b (ByTask t) wid = true.
Proof.
  intros b t wid L H. unfold sub_admit, usable. rewrite L, H. reflexivity.
Qed.

Lemma snd_admit_by_the_publisher_owner : forall b t wid,
    w_live (b_w b wid) = true -> owns b t (CPub wid) = true ->
    snd_admit b (ByTask t) wid = true.
Proof.
  intros b t wid L H. unfold snd_admit, usable. rewrite L, H. reflexivity.
Qed.

Lemma rcv_admit_by_the_reader_owner : forall b t rid,
    r_live (b_r b rid) = true -> w_live (b_w b (r_wid (b_r b rid))) = true ->
    owns b t (CSub rid) = true -> rcv_admit b (ByTask t) rid = true.
Proof.
  intros b t rid L1 L2 H. unfold rcv_admit, usable. rewrite L1, L2, H. reflexivity.
Qed.

(* two admitted readers of one cursor are the same task *)
Lemma the_owner_is_the_only_reader : forall b t t' rid,
    bus_wf b -> rcv_admit b (ByTask t) rid = true -> rcv_admit b (ByTask t') rid = true ->
    t = t'.
Proof.
  intros b t t' rid W A1 A2.
  apply (usable_is_exclusive b t t' (CSub rid) W).
  - unfold rcv_admit in A1. apply andb_true_iff in A1. destruct A1 as [_ A1].
    apply andb_true_iff in A1. destruct A1 as [_ U]. exact U.
  - unfold rcv_admit in A2. apply andb_true_iff in A2. destruct A2 as [_ A2].
    apply andb_true_iff in A2. destruct A2 as [_ U]. exact U.
Qed.

(* ── 16.5 ownership is created once and survives every other call ─────── *)

Lemma pub_ownership_untouched : forall b cap t c,
    owns (fst (pub b cap)) t c = owns b t c.
Proof.
  intros b cap t c. unfold pub.
  destruct (pub_slot b) as [i|] eqn:S; [apply owns_w_inert|reflexivity].
Qed.

Lemma sub_ownership_untouched : forall b cl wid t c,
    owns (fst (sub b cl wid)) t c = owns b t c.
Proof.
  intros b cl wid t c. unfold sub.
  destruct (sub_admit b cl wid) eqn:A.
  - destruct (sub_slot b) as [i|] eqn:S; [apply owns_r_inert|reflexivity].
  - reflexivity.
Qed.

Lemma snd_ownership_untouched : forall b cl wid d t c,
    owns (fst (snd b cl wid d)) t c = owns b t c.
Proof.
  intros b cl wid d t c. unfold snd.
  destruct (snd_admit b cl wid) eqn:A.
  - destruct (snd_fits b wid) eqn:F; apply owns_w_inert.
  - reflexivity.
Qed.

(* existsb can only be satisfied by a member of the list it searches *)
Lemma existsb_witness : forall (A : Type) (f : A -> bool) (l : list A),
    existsb f l = true -> exists x, In x l /\ f x = true.
Proof.
  intros A f l. induction l as [|a t IH]; intros H; cbn [existsb] in H.
  - discriminate H.
  - destruct (f a) eqn:Fa.
    + exists a. split; [left; reflexivity|exact Fa].
    + destruct IH as [x [Hin Hf]]; [exact H|]. exists x. split; [right; exact Hin|exact Hf].
Qed.

(* the only way to acquire a cursor: it was in the grant *)
Lemma spawn_ownership_is_the_grant : forall b core l i t c,
    spawn_slot b = Some i -> pr2 (spawn b core l) = Some i ->
    owns (fst (spawn b core l)) t c = true ->
    (t = i /\ In c l) \/ owns b t c = true.
Proof.
  intros b core l i t c S G H.
  destruct (spawn_ok b core l) eqn:Q.
  - rewrite (spawn_granted_shape b core l i S Q) in G, H. cbn [pr2 fst] in G, H.
    destruct (Nat.eqb_spec t i) as [Hti|Hne].
    + left. split; [exact Hti|].
      rewrite Hti in H.
      rewrite owns_bus_t_same, task_revive_live, task_revive_curs in H. cbn [andb] in H.
      destruct (existsb_witness _ (cref_eqb c) l H) as [y [Hin Hy]].
      assert (Ey : c = y) by (apply cref_eqb_iff; exact Hy). subst c. exact Hin.
    + right. rewrite (owns_bus_t_other b i (task_revive core l) t c Hne) in H. exact H.
  - rewrite (spawn_refused_shape b core l i S Q) in H. cbn [fst] in H. right. exact H.
Qed.

(* a grant of a cursor somebody already holds is refused: the exclusivity that
 * spawn preserves is bought by the taken-cursor guard *)
Lemma spawn_does_not_take_an_owned_cursor : forall b core l c,
    bus_wf b -> spawn_ok b core l = true -> In c l ->
    (exists t, owns b t c = true) -> False.
Proof.
  intros b core l c W Q Hin [t H].
  assert (F : cursor_taken b c = false)
    by (apply (spawn_ok_cursors_free b core l c Q Hin)).
  rewrite (cursor_taken_if_owned b t c (a_holder_is_in_range b t c W H) H) in F.
  discriminate F.
Qed.

Lemma ownership_survives_a_spawn : forall b core l t c,
    spawn_ok b core l = true -> owns b t c = true ->
    owns (fst (spawn b core l)) t c = true.
Proof.
  intros b core l t c Q H.
  destruct (spawn_slot b) as [i|] eqn:S.
  - destruct (spawn_ok b core l) eqn:Q2.
    + rewrite (spawn_granted_shape b core l i S Q2). cbn [fst].
      destruct (Nat.eqb_spec t i) as [Hti|Hne].
      * subst t. exfalso.
        assert (Dead : t_live (b_t b i) = false) by (apply (spawn_slot_offers_a_dead_slot b i S)).
        unfold owns in H. apply andb_true_iff in H.
        destruct H as [Live _]. rewrite Dead in Live. discriminate Live.
      * rewrite (owns_bus_t_other b i (task_revive core l) t c Hne). exact H.
    + rewrite (spawn_refused_shape b core l i S Q2). cbn [fst]. exact H.
  - rewrite (spawn_no_slot_shape b core l S). cbn [fst]. exact H.
Qed.

Lemma a_taken_cursor_has_a_holder : forall b c,
    bus_wf b -> cursor_taken b c = true -> exists t, owns b t c = true.
Proof.
  intros b c W H.
  destruct (existsb_witness _ (fun t => owns b t c) (nat_list max_tasks) H) as [t [Hin Hown]].
  exists t. exact Hown.
Qed.

(* ── 17. I8: conservation of sectors ─────────────────────────────────── *)
(*
 * The oracle's I8 group is arithmetic on the shipped launch: accepted plus
 * dropped equals attempts, residency never exceeds the pool, and a drain
 * returns the pool to its initial value.  The numerals stay in the oracle;
 * the laws behind them are general, and are proved here for every state.
 *)

(* what a single subscriber still pins: the gap between the publisher's tail
 * and its own cursor *)
Definition pending b (r : reader) : nat :=
  w_enq (b_w b (r_wid r)) - r_cur r.

(* the fold only needs the bound at the slots it actually visits *)
Lemma min_fold_hits_ge : forall f g fuel i acc,
    (forall j, i <= j -> j < i + fuel -> f j = true -> acc <= g j) ->
    min_fold f g fuel i acc = acc.
Proof.
  intros f g fuel. induction fuel as [|k IH]; intros i acc H; cbn [min_fold]; [reflexivity|].
  destruct (f i) eqn:Fi.
  - assert (Hi : acc <= g i) by (apply H; [lia | lia | exact Fi]).
    replace (Nat.min acc (g i)) with acc by lia.
    apply IH. intros j Le Lt F. apply H; [lia | lia | exact F].
  - apply IH. intros j Le Lt F. apply H; [lia | lia | exact F].
Qed.

Lemma sum_f_all_zero : forall (f : nat -> nat) n,
    (forall j, j < n -> f j = 0) -> sum_f f n = 0.
Proof.
  intros f n. induction n as [|k IH]; intros H; cbn [sum_f].
  - reflexivity.
  - rewrite (H k) by lia. rewrite Nat.add_0_l. apply IH.
    intros j Lt. apply H. lia.
Qed.

(* one slot may exceed its mate by one; the fold then exceeds it by one *)
Lemma sum_f_all_but_one : forall (f g : nat -> nat) n i,
    (forall j, j < n -> i <> j -> f j <= g j) ->
    (forall j, j < n -> f j <= S (g j)) ->
    sum_f f n <= S (sum_f g n).
Proof.
  intros f g n. induction n as [|k IH]; intros i H1 H2; cbn [sum_f].
  - lia.
  - destruct (Nat.eqb_spec i k) as [Hik|Hne].
    + subst i.
      assert (A : f k <= S (g k)) by (apply H2; lia).
      assert (B : sum_f f k <= sum_f g k).
      { apply sum_f_le. intros j Lt. apply H1; lia. }
      lia.
    + assert (A : f k <= g k) by (apply H1; [lia | exact Hne]).
      assert (B : sum_f f k <= S (sum_f g k)).
      { apply (IH i).
        - intros j Lt Hj. apply H1; [lia | exact Hj].
        - intros j Lt. apply H2. lia. }
      lia.
Qed.

(* ── 17.1 the gap is the holding, seen from any subscriber ──────────── *)

Lemma gap_below_held : forall b wid rid,
    rid < max_readers -> r_live (b_r b rid) = true -> r_wid (b_r b rid) = wid ->
    w_live (b_w b wid) = true -> r_cur (b_r b rid) <= w_enq (b_w b wid) ->
    pending b (b_r b rid) <= held b wid.
Proof.
  intros b wid rid Lt Lr Rw Lw Le.
  assert (Lf : floor_of b (r_wid (b_r b rid)) (w_enq (b_w b wid)) <= r_cur (b_r b rid)).
  { apply (floor_of_hit b rid (w_enq (b_w b wid)) Lt Lr Le). }
  rewrite Rw in Lf.
  unfold pending. rewrite Rw, (held_of_live_writer b wid Lw). lia.
Qed.

(* the slowest subscriber is exactly the holding: conservation at the floor *)
Lemma conservation_at_the_floor : forall b wid rid,
    rid < max_readers -> r_live (b_r b rid) = true -> r_wid (b_r b rid) = wid ->
    w_live (b_w b wid) = true ->
    floor_of b wid (w_enq (b_w b wid)) = r_cur (b_r b rid) ->
    r_cur (b_r b rid) + held b wid = w_enq (b_w b wid).
Proof.
  intros b wid rid Lt Lr Rw Lw Hf.
  assert (Le : r_cur (b_r b rid) <= w_enq (b_w b wid)).
  { rewrite <- Hf. apply floor_of_le. }
  rewrite (held_of_live_writer b wid Lw), Hf. lia.
Qed.

(* a read retires exactly one sector of that subscriber's holding *)
Lemma a_read_retires_one_sector : forall b r,
    r_cur r < w_enq (b_w b (r_wid r)) ->
    S (pending b (reader_advance r)) = pending b r.
Proof.
  intros b r Lt. unfold pending.
  rewrite reader_advance_cur, reader_advance_wid.
  assert (Le : r_cur r <= w_enq (b_w b (r_wid r))) by lia. lia.
Qed.

(* an accepted write widens every subscriber's gap by one *)
Lemma a_write_widens_the_gap : forall b wid rid d,
    r_wid (b_r b rid) = wid ->
    r_cur (b_r b rid) <= w_enq (b_w b wid) ->
    pending (bus_w b wid (writer_push (b_w b wid) d))
            (b_r (bus_w b wid (writer_push (b_w b wid) d)) rid)
    = S (pending b (b_r b rid)).
Proof.
  intros b wid rid d Rw Le.
  unfold pending.
  rewrite at_r_bus_w, Rw, at_w_same_w, writer_push_enq. lia.
Qed.

(* attempts are conserved: one more datum is either in the ring or counted as
 * dropped -- the general form of the oracle's accepted + dropped = attempts *)
Lemma attempts_are_conserved : forall b cl wid d,
    snd_admit b cl wid = true ->
    w_enq (b_w (fst (snd b cl wid d)) wid) + w_drop (b_w (fst (snd b cl wid d)) wid)
    = S (w_enq (b_w b wid) + w_drop (b_w b wid)).
Proof.
  intros b cl wid d A. rewrite (snd_attempts_counted b cl wid d A). lia.
Qed.

(* ── 17.2 residency is bounded by the tables, not by the run ────────── *)

Lemma wf_cap_le_ring : forall b j, bus_wf b -> w_cap (b_w b j) <= ring_capacity.
Proof.
  intros b j W. destruct W as [Wtabs _]. destruct Wtabs as [_ [_ [_ Hcap]]].
  apply (Hcap j).
Qed.

Lemma resident_bounded_by_capacity : forall b wid,
    bus_wf b -> held b wid <= ring_capacity.
Proof.
  intros b wid W.
  pose proof (wf_cap_le_ring b wid W) as Cc.
  destruct W as [_ [Wb _]].
  apply Nat.le_trans with (w_cap (b_w b wid)); [ apply (Wb wid) | exact Cc ].
Qed.

Lemma total_resident_bounded : forall b,
    bus_wf b -> total_held b <= max_writers * ring_capacity.
Proof.
  intros b W. unfold total_held. apply sum_f_bound. intros j Lt.
  apply (resident_bounded_by_capacity b j W).
Qed.

Lemma pool_never_overdrawn : forall b, bus_wf b -> total_held b <= sector_pool.
Proof.
  intros b W.
  assert (K : max_writers * ring_capacity <= sector_pool).
  { apply (proj1 (Nat.leb_le (max_writers * ring_capacity) sector_pool)).
    vm_compute. reflexivity. }
  apply Nat.le_trans with (max_writers * ring_capacity).
  - apply (total_resident_bounded b W).
  - exact K.
Qed.

(* the pool survives every call: a write may pin one more sector, but only
 * when there was room, and a drop pins none *)
Lemma a_write_keeps_the_pool : forall b cl wid d,
    bus_wf b -> total_held (fst (snd b cl wid d)) <= sector_pool.
Proof.
  intros b cl wid d W.
  destruct (snd_admit b cl wid) eqn:A.
  - destruct (snd_fits b wid) eqn:F.
    + assert (Lw : w_live (b_w b wid) = true) by (apply (snd_admit_live b cl wid A)).
      assert (R : total_held b < sector_pool) by (apply (snd_fits_room b wid); exact F).
      assert (G : total_held (fst (snd b cl wid d)) <= S (total_held b)).
      { unfold total_held. rewrite (snd_ok_bus b cl wid d A F).
        apply sum_f_all_but_one with (i := wid).
        - intros j Lt Hj.
          rewrite (held_w_inert b wid (writer_push (b_w b wid) d) j Hj). apply Nat.le_refl.
        - intros j Lt.
          destruct (Nat.eqb_spec wid j) as [Hjw|Hne].
          * rewrite <- Hjw. apply (held_push_bus b wid d Lw).
          * rewrite (held_w_inert b wid (writer_push (b_w b wid) d) j Hne). lia. }
      lia.
    + rewrite (snd_full_total_held_unchanged b cl wid d A F).
      apply (pool_never_overdrawn b W).
  - rewrite (snd_bad_cursor_bus b cl wid d A). apply (pool_never_overdrawn b W).
Qed.

Lemma a_read_keeps_the_pool : forall b cl rid,
    bus_wf b -> total_held (fst (rcv b cl rid)) <= sector_pool.
Proof.
  intros b cl rid W.
  apply Nat.le_trans with (total_held b).
  - apply (rcv_never_increases_total_holding b cl rid W).
  - apply (pool_never_overdrawn b W).
Qed.

(* ── 17.3 drain: quiescence returns the whole pool ─────────────────── *)

Lemma quiescence_empties_a_publisher : forall b wid,
    (forall r, r < max_readers -> sub_reader wid (b_r b r) = true ->
               r_cur (b_r b r) = w_enq (b_w b wid)) ->
    held b wid = 0.
Proof.
  intros b wid H.
  destruct (w_live (b_w b wid)) eqn:Lw.
  - rewrite (held_of_live_writer b wid Lw).
    assert (F : floor_of b wid (w_enq (b_w b wid)) = w_enq (b_w b wid)).
    { unfold floor_of. apply min_fold_hits_ge.
      intros j Le Lt S. rewrite (H j Lt S). apply Nat.le_refl. }
    rewrite F. lia.
  - unfold held. rewrite Lw. reflexivity.
Qed.

Lemma quiescence_empties_the_bus : forall b,
    (forall r, r < max_readers -> r_live (b_r b r) = true ->
               r_cur (b_r b r) = w_enq (b_w b (r_wid (b_r b r)))) ->
    total_held b = 0.
Proof.
  intros b H. unfold total_held. apply sum_f_all_zero. intros j Lt.
  apply (quiescence_empties_a_publisher b j).
  intros r Hr Sr. cbn [sub_reader] in Sr. apply andb_true_iff in Sr.
  destruct Sr as [Lr Rw].
  apply (proj1 (Nat.eqb_eq (r_wid (b_r b r)) j)) in Rw.
  rewrite <- Rw. apply (H r Hr Lr).
Qed.

Lemma a_quiescent_pool_is_whole : forall b,
    total_held b = 0 -> pool_free b = sector_pool.
Proof.
  intros b H. unfold pool_free. rewrite H. apply Nat.sub_0_r.
Qed.

(* the empty bus holds nothing: the pool starts whole *)
Lemma bus0_holds_nothing : total_held bus0 = 0.
Proof.
  apply quiescence_empties_the_bus. intros r Hr Lr.
  unfold bus0 in Lr. cbn [b_r r_live empty_reader] in Lr. discriminate Lr.
Qed.

Lemma bus0_pool_is_whole : pool_free bus0 = sector_pool.
Proof.
  apply a_quiescent_pool_is_whole, bus0_holds_nothing.
Qed.

(* ── 18. I9: the class of states the five calls cannot leave ───── *)
(*
 * The oracle's I9 group enumerates every run of length six over the five calls
 * and reports that no invariant breaks.  What it searches, the theorems below
 * prove once: each call maps well-formed buses to well-formed buses, so every
 * word over the call alphabet does, in every interleaving.
 *
 * Two obligations are carried as hypotheses rather than checked by the shipped
 * guards, because the guards do not check them: pub accepts a capacity without
 * validating it against the ring bound, and spawn accepts a cursor list without
 * checking it for duplicates.  Both are contract gaps to close in the API, not
 * facts the model can invent.
 *)

(* ── 18.1 the conjuncts, read off a well-formed bus ─────────────── *)

Lemma bus_wf_tables : forall b, bus_wf b -> wf_tables b.
Proof. intros b W. exact (proj1 W). Qed.

Lemma bus_wf_bounded : forall b, bus_wf b -> wf_bounded b.
Proof. intros b W. exact (proj1 (proj2 W)). Qed.

Lemma bus_wf_reader_bounds : forall b, bus_wf b -> wf_reader_bounds b.
Proof. intros b W. exact (proj1 (proj2 (proj2 W))). Qed.

Lemma bus_wf_tasks : forall b, bus_wf b -> wf_tasks b.
Proof. intros b W. exact (proj1 (proj2 (proj2 (proj2 W)))). Qed.

Lemma bus_wf_ownership : forall b, bus_wf b -> wf_ownership b.
Proof. intros b W. exact (proj1 (proj2 (proj2 (proj2 (proj2 W))))). Qed.

Lemma bus_wf_star : forall b, bus_wf b -> wf_star b.
Proof. intros b W. exact (proj1 (proj2 (proj2 (proj2 (proj2 (proj2 W)))))). Qed.

Lemma bus_wf_retired : forall b, bus_wf b -> wf_retired b.
Proof. intros b W. exact (proj2 (proj2 (proj2 (proj2 (proj2 (proj2 W)))))). Qed.

Lemma wf_cap_range : forall b j, wf_tables b ->
    0 < w_cap (b_w b j) /\ w_cap (b_w b j) <= ring_capacity.
Proof. intros b j W. exact (proj2 (proj2 (proj2 W)) j). Qed.

Lemma bus_wf_cap : forall b j, bus_wf b ->
    0 < w_cap (b_w b j) /\ w_cap (b_w b j) <= ring_capacity.
Proof. intros b j W. apply (wf_cap_range b j). exact (bus_wf_tables b W). Qed.

(* ── 18.2 what each table update does to the derived predicates ─── *)

Lemma held_bus_t : forall b i (t : task) j, held (bus_t b i t) j = held b j.
Proof. intros b i t j. unfold held. rewrite at_w_bus_t. reflexivity. Qed.

Lemma b_cores_bus_t : forall b i (t : task), b_cores (bus_t b i t) = b_cores b.
Proof. reflexivity. Qed.

(* a reader that does not subscribe to this publisher, written into a dead
 * slot, cannot lower anybody's watermark *)
Lemma floor_of_bus_r_dead : forall b i (r : reader) wid e,
    r_live (b_r b i) = false -> sub_reader wid r = false ->
    floor_of (bus_r b i r) wid e = floor_of b wid e.
Proof.
  intros b i r wid e Hd Hr. unfold floor_of.
  apply (min_fold_cong_if (fun j => sub_reader wid (b_r (bus_r b i r) j))
                          (fun j => r_cur (b_r (bus_r b i r) j))
                          (fun j => sub_reader wid (b_r b j))
                          (fun j => r_cur (b_r b j)) max_readers 0 e).
  intros j Le Lt. destruct (Nat.eqb_spec i j) as [Hij|Hne].
  - subst j. split.
    + rewrite at_r_same_r, Hr. unfold sub_reader.
      rewrite Hd. cbn [andb]. reflexivity.
    + intros Hsub. unfold sub_reader in Hsub.
      rewrite Hd in Hsub. cbn [andb] in Hsub. discriminate Hsub.
  - split.
    + rewrite (at_r_other_r b i r j Hne). reflexivity.
    + intros _. rewrite (at_r_other_r b i r j Hne). reflexivity.
Qed.

Lemma held_bus_r_dead_inert : forall b i (r : reader) j,
    r_live (b_r b i) = false -> sub_reader j r = false ->
    held (bus_r b i r) j = held b j.
Proof.
  intros b i r j Hd Hj. unfold held.
  rewrite at_w_bus_r, (floor_of_bus_r_dead b i r j (w_enq (b_w b j)) Hd Hj).
  reflexivity.
Qed.

(* a subscriber that joins at the publisher's tail pins nothing *)
Lemma held_join_at_seed_same : forall b i wid,
    r_live (b_r b i) = false -> i < max_readers -> w_live (b_w b wid) = true ->
    held (bus_r b i (mk_reader true wid (w_enq (b_w b wid)) (w_enq (b_w b wid)))) wid = held b wid.
Proof.
  intros b i wid Hd Hlt Hw.
  assert (Hw' : w_live (b_w (bus_r b i (mk_reader true wid (w_enq (b_w b wid)) (w_enq (b_w b wid)))) wid) = true)
    by (rewrite at_w_bus_r; exact Hw).
  rewrite (held_of_live_writer _ _ Hw'), (held_of_live_writer b wid Hw).
  rewrite at_w_bus_r.
  rewrite (floor_of_join_at_seed b i wid (w_enq (b_w b wid)) Hd Hlt).
  reflexivity.
Qed.

Lemma cursor_exists_of_live : forall b c, bus_wf b ->
    cursor_live b c = true -> cursor_exists c = true.
Proof.
  intros b c W H. destruct c as [i|j].
  - cbn [cursor_exists]. apply (proj2 (Nat.ltb_lt i max_writers)).
    apply (w_live_lt b i W). exact H.
  - cbn [cursor_exists]. apply (proj2 (Nat.ltb_lt j max_readers)).
    apply (r_live_lt b j W). exact H.
Qed.

(* a live update can only make a cursor easier to name *)
Lemma cursor_live_bus_w_mono : forall b i (w : writer) c,
    cursor_live b c = true -> w_live w = true -> cursor_live (bus_w b i w) c = true.
Proof.
  intros b i w c H Hw. destruct c as [j|j]; cbn [cursor_live].
  - destruct (Nat.eqb_spec i j) as [Hij|Hne].
    + subst j. rewrite at_w_same_w. exact Hw.
    + rewrite (at_w_other_w b i w j Hne). exact H.
  - rewrite at_r_bus_w. exact H.
Qed.

Lemma cursor_live_bus_r_mono : forall b i (r : reader) c,
    cursor_live b c = true -> r_live r = true -> cursor_live (bus_r b i r) c = true.
Proof.
  intros b i r c H Hr. destruct c as [j|j]; cbn [cursor_live].
  - rewrite at_w_bus_r. exact H.
  - destruct (Nat.eqb_spec i j) as [Hij|Hne].
    + subst j. rewrite at_r_same_r. exact Hr.
    + rewrite (at_r_other_r b i r j Hne). exact H.
Qed.

Lemma wf_retired_bus_w : forall b i (w : writer), w_live w = true ->
    wf_retired b -> wf_retired (bus_w b i w).
Proof.
  intros b i w Hw H j.
  destruct (Nat.eqb_spec i j) as [Hij|Hne].
  - subst j. rewrite at_w_same_w. intros Hj. exfalso. rewrite Hw in Hj. discriminate Hj.
  - rewrite (at_w_other_w b i w j Hne). intros Hj. apply (H j Hj).
Qed.

Lemma wf_retired_bus_r : forall b i (r : reader), wf_retired b -> wf_retired (bus_r b i r).
Proof.
  intros b i r H j. rewrite at_w_bus_r. intros Hj. apply (H j Hj).
Qed.

Lemma wf_retired_bus_t : forall b i (t : task), wf_retired b -> wf_retired (bus_t b i t).
Proof.
  intros b i t H j. rewrite at_w_bus_t. intros Hj. apply (H j Hj).
Qed.

Lemma wf_tables_bus_w : forall b i (w : writer), i < max_writers ->
    0 < w_cap w /\ w_cap w <= ring_capacity ->
    wf_tables b -> wf_tables (bus_w b i w).
Proof.
  intros b i w Hlt Hcap W. unfold wf_tables in *.
  destruct W as [A [B [C D]]].
  split.
  { intros j Lt. destruct (Nat.eqb_spec i j) as [Hij|Hne].
    - subst j. exfalso. lia.
    - rewrite (at_w_other_w b i w j Hne). exact (A j Lt). }
  split. { intros k Lt. rewrite at_r_bus_w. exact (B k Lt). }
  split. { intros k Lt. rewrite at_t_bus_w. exact (C k Lt). }
  { intros k. destruct (Nat.eqb_spec i k) as [Hik|Hne].
    - subst k. rewrite at_w_same_w. exact Hcap.
    - rewrite (at_w_other_w b i w k Hne). exact (D k). }
Qed.

Lemma wf_tables_bus_r : forall b i (r : reader), i < max_readers ->
    wf_tables b -> wf_tables (bus_r b i r).
Proof.
  intros b i r Hlt W. unfold wf_tables in *.
  destruct W as [A [B [C D]]].
  split. { intros j Lt. rewrite at_w_bus_r. exact (A j Lt). }
  split.
  { intros k Lt. destruct (Nat.eqb_spec i k) as [Hik|Hne].
    - subst k. exfalso. lia.
    - rewrite (at_r_other_r b i r k Hne). exact (B k Lt). }
  split. { intros k Lt. rewrite at_t_bus_r. exact (C k Lt). }
  { intros k. rewrite at_w_bus_r. exact (D k). }
Qed.

Lemma wf_tables_bus_t : forall b i (t : task), i < max_tasks ->
    wf_tables b -> wf_tables (bus_t b i t).
Proof.
  intros b i t Hlt W. unfold wf_tables in *.
  destruct W as [A [B [C D]]].
  split. { intros j Lt. rewrite at_w_bus_t. exact (A j Lt). }
  split. { intros k Lt. rewrite at_r_bus_t. exact (B k Lt). }
  split.
  { intros u Lt. destruct (Nat.eqb_spec i u) as [Hiu|Hne].
    - subst u. exfalso. lia.
    - rewrite (at_t_other_t b i t u Hne). exact (C u Lt). }
  { intros u. rewrite at_w_bus_t. exact (D u). }
Qed.

(* ── 18.3 the two inert updates, proved once ────────────────────── *)

(* Everything the invariant says about the bus is either about slot i, which the
 * obligations below cover, or about some other slot, which is inert. *)
Lemma bus_w_preserves_wf : forall b i (w : writer),
    i < max_writers -> w_live w = true ->
    0 < w_cap w /\ w_cap w <= ring_capacity ->
    held (bus_w b i w) i <= w_cap w ->
    (forall k, k < max_readers -> r_live (b_r b k) = true ->
               r_wid (b_r b k) = i -> r_cur (b_r b k) <= w_enq w) ->
    bus_wf b -> bus_wf (bus_w b i w).
Proof.
  intros b i w Hlt Hw Hcap Hhb Hread W.
  unfold bus_wf.
  split. { apply (wf_tables_bus_w b i w Hlt Hcap (bus_wf_tables b W)). }
  split.
  { intros j. destruct (Nat.eqb_spec i j) as [Hij|Hne].
    - subst j. rewrite (at_w_same_w b i w). exact Hhb.
    - rewrite (held_w_inert b i w j Hne), (at_w_other_w b i w j Hne).
      apply (bus_wf_bounded b W j). }
  split.
  { intros k Hr. rewrite (at_r_bus_w b i w k) in Hr.
    destruct (bus_wf_reader_bounds b W k Hr) as [L1 [L2 [L3 L4]]].
    rewrite (at_r_bus_w b i w k).
    split. { exact L1. }
    split.
    { destruct (Nat.eqb_spec i (r_wid (b_r b k))) as [Hik|Hne].
      - rewrite <- Hik, at_w_same_w. exact Hw.
      - rewrite (at_w_other_w b i w (r_wid (b_r b k)) Hne). exact L2. }
    split. { exact L3. }
    { destruct (Nat.eqb_spec i (r_wid (b_r b k))) as [Hik|Hne].
      - assert (He : r_wid (b_r b k) = i) by (symmetry; exact Hik).
        rewrite <- Hik, at_w_same_w.
        apply (Hread k); [ exact L1 | exact Hr | exact He ].
      - rewrite (at_w_other_w b i w (r_wid (b_r b k)) Hne). exact L4. } }
  split.
  { intros t Ht. rewrite (at_t_bus_w b i w t) in Ht.
    destruct (bus_wf_tasks b W t Ht) as [L1 [L2 [L3 [L4 [L5 L6]]]]].
    rewrite !at_t_bus_w, cores_inert_w.
    split. { exact L1. }
    split. { exact L2. }
    split.
    { intros c Hin. destruct (L3 c Hin) as [Ex Lv].
      split. { exact Ex. }
      { apply (cursor_live_bus_w_mono b i w c Lv Hw). } }
    split. { exact L4. }
    split. { exact L5. }
    { exact L6. } }
  split. { intros t1 t2 c H1 H2. exact (bus_wf_ownership b W t1 t2 c H1 H2). }
  split. { intros t1 t2 H1 H2 Hc. exact (bus_wf_star b W t1 t2 H1 H2 Hc). }
  { exact (wf_retired_bus_w b i w Hw (bus_wf_retired b W)). }
Qed.

Lemma bus_r_preserves_wf : forall b i (r : reader),
    i < max_readers -> r_live r = true ->
    (forall j, held (bus_r b i r) j <= held b j) ->
    (r_start r <= r_cur r /\
     r_cur r <= w_enq (b_w b (r_wid r)) /\ w_live (b_w b (r_wid r)) = true) ->
    bus_wf b -> bus_wf (bus_r b i r).
Proof.
  intros b i r Hlt Hr Hheld Hb W.
  destruct Hb as [B1 [B2 B3]].
  unfold bus_wf.
  split. { apply (wf_tables_bus_r b i r Hlt (bus_wf_tables b W)). }
  split.
  { intros j. rewrite (at_w_bus_r b i r j).
    apply Nat.le_trans with (held b j);
      [ apply (Hheld j) | apply (bus_wf_bounded b W j) ]. }
  split.
  { intros k Hk. destruct (Nat.eqb_spec i k) as [Hik|Hne].
    - subst k. rewrite !at_r_same_r, !at_w_bus_r.
      split. { exact Hlt. }
      split. { exact B3. }
      split. { exact B1. }
      { exact B2. }
    - rewrite (at_r_other_r b i r k Hne) in Hk.
      destruct (bus_wf_reader_bounds b W k Hk) as [L1 [L2 [L3 L4]]].
      rewrite (at_r_other_r b i r k Hne), at_w_bus_r.
      split. { exact L1. }
      split. { exact L2. }
      split. { exact L3. }
      { exact L4. } }
  split.
  { intros t Ht. rewrite (at_t_bus_r b i r t) in Ht.
    destruct (bus_wf_tasks b W t Ht) as [L1 [L2 [L3 [L4 [L5 L6]]]]].
    rewrite !at_t_bus_r, cores_inert_r.
    split. { exact L1. }
    split. { exact L2. }
    split.
    { intros c Hin. destruct (L3 c Hin) as [Ex Lv].
      split. { exact Ex. }
      { apply (cursor_live_bus_r_mono b i r c Lv Hr). } }
    split. { exact L4. }
    split. { exact L5. }
    { exact L6. } }
  split. { intros t1 t2 c H1 H2. exact (bus_wf_ownership b W t1 t2 c H1 H2). }
  split. { intros t1 t2 H1 H2 Hc. exact (bus_wf_star b W t1 t2 H1 H2 Hc). }
  { exact (wf_retired_bus_r b i r (bus_wf_retired b W)). }
Qed.

(* ── 18.4 pub and snd ───────────────────────────────────────────── *)

Lemma pub_no_slot_refuses : forall b cap, pub_slot b = None -> pub b cap = (b, None).
Proof. intros b cap H. unfold pub. rewrite H. reflexivity. Qed.

(* a revived ring starts empty, because a retired slot holds nothing *)
Lemma pub_held_zero : forall b i cap, w_live (b_w b i) = false -> wf_retired b ->
    held (bus_w b i (writer_revive (b_w b i) cap)) i = 0.
Proof.
  intros b i cap Hd Hret.
  rewrite (held_bus_w_same b i (writer_revive (b_w b i) cap))
    by apply writer_revive_live.
  rewrite writer_revive_enq.
  destruct (Hret i Hd) as [En _]. lia.
Qed.

Lemma pub_preserves_bus_wf : forall b cap, 0 < cap -> cap <= ring_capacity ->
    bus_wf b -> bus_wf (fst (pub b cap)).
Proof.
  intros b cap Hpos Hle W.
  destruct (pub_slot b) as [i|] eqn:S.
  - rewrite (pub_grant_shape b cap i S). cbn [fst].
    assert (Hdead : w_live (b_w b i) = false)
      by (apply (pub_slot_offers_a_dead_slot b i S)).
    assert (Hh : held (bus_w b i (writer_revive (b_w b i) cap)) i
                  <= w_cap (writer_revive (b_w b i) cap)).
    { rewrite (pub_held_zero b i cap Hdead (bus_wf_retired b W)), writer_revive_cap.
      apply Nat.le_0_l. }
    assert (Hread : forall k, k < max_readers -> r_live (b_r b k) = true ->
                   r_wid (b_r b k) = i ->
                   r_cur (b_r b k) <= w_enq (writer_revive (b_w b i) cap)).
    { intros k Lk Rk Hwid.
      destruct (bus_wf_reader_bounds b W k Rk) as [_ [P _]].
      rewrite Hwid, Hdead in P. discriminate P. }
    apply (bus_w_preserves_wf b i (writer_revive (b_w b i) cap)
             (pub_slot_bounded b i S) (writer_revive_live (b_w b i) cap)
             (conj Hpos Hle) Hh Hread W).
  - rewrite (pub_no_slot_refuses b cap S). exact W.
Qed.

Lemma snd_preserves_bus_wf : forall b cl wid d, bus_wf b -> bus_wf (fst (snd b cl wid d)).
Proof.
  intros b cl wid d W.
  destruct (snd_admit b cl wid) eqn:A.
  - assert (Hw : w_live (b_w b wid) = true) by (apply (snd_admit_live b cl wid A)).
    assert (Hlt : wid < max_writers) by (apply (w_live_lt b wid W Hw)).
    destruct (snd_fits b wid) eqn:F.
    + rewrite (snd_ok_bus b cl wid d A F).
      assert (Hh : held (bus_w b wid (writer_push (b_w b wid) d)) wid
                    <= w_cap (writer_push (b_w b wid) d)).
      { rewrite writer_push_cap.
        apply Nat.le_trans with (S (held b wid)).
        - apply (held_push_bus b wid d Hw).
        - assert (HL : held b wid < w_cap (b_w b wid))
            by (apply (snd_fits_below_cap b wid); exact F).
          lia. }
      assert (Hcap : 0 < w_cap (writer_push (b_w b wid) d) /\
                     w_cap (writer_push (b_w b wid) d) <= ring_capacity).
      { rewrite writer_push_cap. apply (bus_wf_cap b wid W). }
      assert (Hread : forall k, k < max_readers -> r_live (b_r b k) = true ->
                     r_wid (b_r b k) = wid ->
                     r_cur (b_r b k) <= w_enq (writer_push (b_w b wid) d)).
      { intros k Lk Rk Hwid.
        destruct (bus_wf_reader_bounds b W k Rk) as [_ [_ [_ L4]]].
        rewrite Hwid in L4. rewrite writer_push_enq. lia. }
      apply (bus_w_preserves_wf b wid (writer_push (b_w b wid) d) Hlt
               (writer_push_live (b_w b wid) d) Hcap Hh Hread W).
    + rewrite (snd_full_bus b cl wid d A F).
      assert (Hh : held (bus_w b wid (writer_drop1 (b_w b wid))) wid
                    <= w_cap (writer_drop1 (b_w b wid))).
      { rewrite writer_drop1_cap, (snd_full_held_bus b wid Hw).
        apply (bus_wf_bounded b W wid). }
      assert (Hcap : 0 < w_cap (writer_drop1 (b_w b wid)) /\
                     w_cap (writer_drop1 (b_w b wid)) <= ring_capacity).
      { rewrite writer_drop1_cap. apply (bus_wf_cap b wid W). }
      assert (Hread : forall k, k < max_readers -> r_live (b_r b k) = true ->
                     r_wid (b_r b k) = wid ->
                     r_cur (b_r b k) <= w_enq (writer_drop1 (b_w b wid))).
      { intros k Lk Rk Hwid.
        destruct (bus_wf_reader_bounds b W k Rk) as [_ [_ [_ L4]]].
        rewrite Hwid in L4. rewrite writer_drop1_enq. exact L4. }
      apply (bus_w_preserves_wf b wid (writer_drop1 (b_w b wid)) Hlt
               (writer_drop1_live (b_w b wid)) Hcap Hh Hread W).
  - rewrite (snd_bad_cursor_bus b cl wid d A). exact W.
Qed.

(* ── 18.5 sub, rcv and spawn ────────────────────────────────────── *)

(* a late joiner seeded at the tail cannot deepen any publisher's holding: it
 * subscribes to exactly one writer, and for that writer the watermark does not
 * move because its cursor starts at the tail; for every other writer the new
 * slot is simply not a subscriber. *)
Lemma a_join_never_deepens : forall b i wid,
    r_live (b_r b i) = false -> i < max_readers ->
    forall j, held (bus_r b i (mk_reader true wid (w_enq (b_w b wid)) (w_enq (b_w b wid)))) j
               <= held b j.
Proof.
  intros b i wid Hd Hlt j.
  destruct (Nat.eqb_spec j wid) as [Hjw|Hne].
  - subst j.
    destruct (w_live (b_w b wid)) eqn:Lw.
    + rewrite (held_join_at_seed_same b i wid Hd Hlt Lw). apply Nat.le_refl.
    + assert (E : held (bus_r b i (mk_reader true wid (w_enq (b_w b wid)) (w_enq (b_w b wid)))) wid = 0).
      { unfold held. rewrite at_w_bus_r, Lw. reflexivity. }
      rewrite E. apply Nat.le_0_l.
  - assert (Hsub : sub_reader j
                   (mk_reader true wid (w_enq (b_w b wid)) (w_enq (b_w b wid))) = false).
    { unfold sub_reader. cbn.
      destruct (Nat.eqb_spec wid j) as [Heq|_].
      - exfalso. apply Hne. symmetry. exact Heq.
      - reflexivity. }
    rewrite (held_bus_r_dead_inert b i
               (mk_reader true wid (w_enq (b_w b wid)) (w_enq (b_w b wid))) j Hd Hsub).
    apply Nat.le_refl.
Qed.

Lemma sub_preserves_bus_wf : forall b cl wid, bus_wf b -> bus_wf (fst (sub b cl wid)).
Proof.
  intros b cl wid W.
  destruct (sub_admit b cl wid) eqn:A.
  - destruct (sub_slot b) as [i|] eqn:S.
    + rewrite (sub_grant_shape b cl wid i S A). cbn [fst].
      apply andb_true_iff in A. destruct A as [Lw _].
      apply (bus_r_preserves_wf b i
               (mk_reader true wid (w_enq (b_w b wid)) (w_enq (b_w b wid)))
               (sub_slot_bounded b i S)).
      * reflexivity.
      * apply (a_join_never_deepens b i wid (sub_slot_offers_a_dead_slot b i S)
                 (sub_slot_bounded b i S)).
      * split.
        { apply Nat.le_refl. }
        { split.
          { cbn. apply Nat.le_refl. }
          { cbn. exact Lw. } }
      * exact W.
    + unfold sub. rewrite A, S. cbn [fst]. exact W.
  - unfold sub. rewrite A. cbn [fst]. exact W.
Qed.

Lemma rcv_preserves_bus_wf : forall b cl rid, bus_wf b -> bus_wf (fst (rcv b cl rid)).
Proof.
  intros b cl rid W.
  destruct (rcv_admit b cl rid) eqn:A.
  - destruct (rcv_ready b rid) eqn:R.
    + rewrite (rcv_data_bus b cl rid A R).
      assert (Hlive : r_live (b_r b rid) = true)
        by (apply (rcv_admit_live_reader b cl rid A)).
      assert (Hlt : rid < max_readers) by (apply (r_live_lt b rid W Hlive)).
      destruct (bus_wf_reader_bounds b W rid Hlive) as [_ [P [B1 B2]]].
      apply (bus_r_preserves_wf b rid (reader_advance (b_r b rid)) Hlt
               (reader_advance_live (b_r b rid))
               (fun wid => held_advance_le b rid wid Hlt Hlive)).
      * rewrite reader_advance_start, reader_advance_cur, reader_advance_wid.
        unfold rcv_ready in R. rewrite Nat.ltb_lt in R.
        split.
        { lia. }
        { split.
          { lia. }
          { exact P. } }
      * exact W.
    + rewrite (rcv_empty_bus b cl rid A R). exact W.
  - rewrite (rcv_bad_cursor_bus b cl rid A). exact W.
Qed.

(* ── 18.6 spawn: the actor update, with the two carried obligations ─ *)

Lemma star_ok_empty_core : forall b core l, star_ok b core l = true ->
    pub_count_c l <> 0 -> core_holds_publisher b core = false.
Proof.
  intros b core l Hstar Hn. unfold star_ok in Hstar.
  assert (E : Nat.eqb (pub_count_c l) 0 = false)
    by (destruct (Nat.eqb_spec (pub_count_c l) 0) as [Heq|_]; [contradiction|reflexivity]).
  rewrite E in Hstar. cbn [negb] in Hstar. apply negb_true_iff in Hstar. exact Hstar.
Qed.

Lemma holds_pub_bus_t_same : forall b i (t : task),
    holds_pub (bus_t b i t) i =
      andb (t_live t) (negb (Nat.eqb (pub_count_c (t_curs t)) 0)).
Proof. intros b i t. unfold holds_pub. rewrite at_t_same_t. reflexivity. Qed.

Lemma holds_pub_bus_t_other : forall b i (t : task) u, i <> u ->
    holds_pub (bus_t b i t) u = holds_pub b u.
Proof.
  intros b i t u H. unfold holds_pub.
  rewrite (at_t_other_t b i t u H). reflexivity.
Qed.

Lemma new_task_publisher_count : forall b i core l,
    holds_pub (bus_t b i (task_revive core l)) i = true -> pub_count_c l <> 0.
Proof.
  intros b i core l H HC.
  rewrite holds_pub_bus_t_same, task_revive_live, task_revive_curs in H.
  cbn [andb] in H. apply negb_true_iff in H.
  exact ((proj1 (Nat.eqb_neq (pub_count_c l) 0) H) HC).
Qed.

(* the same frame fact as owns_bus_t_other, in the orientation the case analyses
 * of Nat.eqb i t produce *)
Lemma owns_slot_inert : forall b i (t : task) j c, i <> j ->
    owns (bus_t b i t) j c = owns b j c.
Proof.
  intros b i t j c H. apply (owns_bus_t_other b i t j c).
  intros Heq. apply H. symmetry. exact Heq.
Qed.

(* the cursor a grant names cannot already be somebody's: exclusivity is bought
 * by the taken-cursor guard, not by the table-first slot choice *)
Lemma owns_the_new_task_is_the_grant : forall b core l i c,
    owns (bus_t b i (task_revive core l)) i c = true -> In c l.
Proof.
  intros b core l i c H.
  rewrite owns_bus_t_same, task_revive_live, task_revive_curs in H. cbn [andb] in H.
  destruct (existsb_witness _ (cref_eqb c) l H) as [y [Hin Hy]].
  assert (Ey : c = y) by (apply cref_eqb_iff; exact Hy).
  subst c. exact Hin.
Qed.

Lemma spawn_keeps_ownership_unique : forall b core l i t1 t2 c,
    spawn_ok b core l = true -> bus_wf b ->
    owns (bus_t b i (task_revive core l)) t1 c = true ->
    owns (bus_t b i (task_revive core l)) t2 c = true -> t1 = t2.
Proof.
  intros b core l i t1 t2 c Q W H1 H2.
  destruct (Nat.eqb_spec i t1) as [Ht1|Hn1].
  - subst t1.
    destruct (Nat.eqb_spec i t2) as [Ht2|Hn2].
    + subst t2. reflexivity.
    + exfalso.
      assert (Hin : In c l) by (apply (owns_the_new_task_is_the_grant b core l i c H1)).
      assert (Free : cursor_taken b c = false)
        by (apply (spawn_ok_cursors_free b core l c Q Hin)).
      rewrite (owns_slot_inert b i (task_revive core l) t2 c Hn2) in H2.
      assert (Lt : t2 < max_tasks) by (apply (a_holder_is_in_range b t2 c W H2)).
      rewrite (cursor_taken_if_owned b t2 c Lt H2) in Free. discriminate Free.
  - destruct (Nat.eqb_spec i t2) as [Ht2|Hn2].
    + subst t2. exfalso.
      assert (Hin : In c l) by (apply (owns_the_new_task_is_the_grant b core l i c H2)).
      assert (Free : cursor_taken b c = false)
        by (apply (spawn_ok_cursors_free b core l c Q Hin)).
      rewrite (owns_slot_inert b i (task_revive core l) t1 c Hn1) in H1.
      assert (Lt : t1 < max_tasks) by (apply (a_holder_is_in_range b t1 c W H1)).
      rewrite (cursor_taken_if_owned b t1 c Lt H1) in Free. discriminate Free.
    + rewrite (owns_slot_inert b i (task_revive core l) t1 c Hn1) in H1.
      rewrite (owns_slot_inert b i (task_revive core l) t2 c Hn2) in H2.
      apply (bus_wf_ownership b W t1 t2 c H1 H2).
Qed.

Lemma spawn_keeps_the_star : forall b core l i t1 t2,
    spawn_ok b core l = true -> bus_wf b ->
    holds_pub (bus_t b i (task_revive core l)) t1 = true ->
    holds_pub (bus_t b i (task_revive core l)) t2 = true ->
    t_core (b_t (bus_t b i (task_revive core l)) t1) =
    t_core (b_t (bus_t b i (task_revive core l)) t2) -> t1 = t2.
Proof.
  intros b core l i t1 t2 Q W P1 P2 Hc.
  destruct (Nat.eqb_spec i t1) as [Ht1|Hn1].
  - destruct (Nat.eqb_spec i t2) as [Ht2|Hn2].
    + subst t1. subst t2. reflexivity.
    + subst t1. exfalso.
      assert (Hn : pub_count_c l <> 0)
        by (apply (new_task_publisher_count b i core l P1)).
      assert (Ch : core_holds_publisher b core = false)
        by (apply (star_ok_empty_core b core l (spawn_ok_star_holds b core l Q) Hn)).
      rewrite (holds_pub_bus_t_other b i (task_revive core l) t2 Hn2) in P2.
      assert (P2b : holds_pub b t2 = true) by exact P2.
      unfold holds_pub in P2. apply andb_true_iff in P2. destruct P2 as [Live _].
      assert (Lt : t2 < max_tasks) by (apply (t_live_lt b t2 W Live)).
      rewrite at_t_same_t, task_revive_core,
              (at_t_other_t b i (task_revive core l) t2 Hn2) in Hc.
      assert (Hp : core_holds_publisher b core = true).
      { apply (core_holds_publisher_witness b core t2 Lt P2b). symmetry. exact Hc. }
      rewrite Ch in Hp. discriminate Hp.
  - destruct (Nat.eqb_spec i t2) as [Ht2|Hn2].
    + subst t2. exfalso.
      assert (Hn : pub_count_c l <> 0)
        by (apply (new_task_publisher_count b i core l P2)).
      assert (Ch : core_holds_publisher b core = false)
        by (apply (star_ok_empty_core b core l (spawn_ok_star_holds b core l Q) Hn)).
      rewrite (holds_pub_bus_t_other b i (task_revive core l) t1 Hn1) in P1.
      assert (P1b : holds_pub b t1 = true) by exact P1.
      unfold holds_pub in P1. apply andb_true_iff in P1. destruct P1 as [Live _].
      assert (Lt : t1 < max_tasks) by (apply (t_live_lt b t1 W Live)).
      rewrite (at_t_other_t b i (task_revive core l) t1 Hn1),
              at_t_same_t, task_revive_core in Hc.
      assert (Hp : core_holds_publisher b core = true)
        by (apply (core_holds_publisher_witness b core t1 Lt P1b Hc)).
      rewrite Ch in Hp. discriminate Hp.
    + rewrite (holds_pub_bus_t_other b i (task_revive core l) t1 Hn1) in P1.
      rewrite (holds_pub_bus_t_other b i (task_revive core l) t2 Hn2) in P2.
      rewrite (at_t_other_t b i (task_revive core l) t1 Hn1),
              (at_t_other_t b i (task_revive core l) t2 Hn2) in Hc.
      apply (bus_wf_star b W t1 t2 P1 P2 Hc).
Qed.

Lemma spawn_preserves_bus_wf : forall b core l, NoDup l ->
    bus_wf b -> bus_wf (fst (spawn b core l)).
Proof.
  intros b core l Hnd W.
  destruct (spawn_slot b) as [i|] eqn:S.
  - destruct (spawn_ok b core l) eqn:Q.
    + rewrite (spawn_granted_shape b core l i S Q). cbn [fst].
      destruct (spawn_ok_six b core l Q) as [Hc [Hlen [Hcl [Hfree [Hpub1 Hstar]]]]].
      unfold bus_wf.
      split. { apply (wf_tables_bus_t b i (task_revive core l) (spawn_slot_bounded b i S)
                              (bus_wf_tables b W)). }
      split. { intros j. rewrite held_bus_t. apply (bus_wf_bounded b W j). }
      split.
      { intros k Hk. rewrite (at_r_bus_t b i (task_revive core l) k) in Hk.
        destruct (bus_wf_reader_bounds b W k Hk) as [R1 [R2 [R3 R4]]].
        rewrite !at_r_bus_t, !at_w_bus_t.
        split. { exact R1. } split. { exact R2. } split. { exact R3. } { exact R4. } }
      split.
      { intros t Ht. destruct (Nat.eqb_spec i t) as [Hti|Hne].
        { subst t.
          rewrite !at_t_same_t, !task_revive_core, !task_revive_curs, cores_inert_t.
          split. { exact (spawn_slot_bounded b i S). }
          split.
          { apply (proj1 (Nat.ltb_lt core (b_cores b))). exact Hc. }
          split.
          { intros c Hin. split.
            { apply (cursor_exists_of_live b c W (cursors_live_elim b l c Hcl Hin)). }
            { rewrite cursor_live_bus_t. apply (cursors_live_elim b l c Hcl Hin). } }
          split. { exact Hnd. }
          split.
          { apply (proj1 (Nat.leb_le (pub_count_c l) 1)). exact Hpub1. }
          { exact (spawn_ok_grants_work b core l Q). } }
        { rewrite (at_t_other_t b i (task_revive core l) t Hne) in Ht.
          destruct (bus_wf_tasks b W t Ht) as [T1 [T2 [T3 [T4 [T5 T6]]]]].
          rewrite (at_t_other_t b i (task_revive core l) t Hne), cores_inert_t.
          split. { exact T1. } split. { exact T2. } split.
          { intros c Hin. destruct (T3 c Hin) as [Ex Lv].
            split. { exact Ex. }
            { rewrite cursor_live_bus_t. exact Lv. } }
          split. { exact T4. } split. { exact T5. } { exact T6. } } }
      split.
      { intros t1 t2 c. apply (spawn_keeps_ownership_unique b core l i t1 t2 c Q W). }
      split.
      { intros t1 t2. apply (spawn_keeps_the_star b core l i t1 t2 Q W). }
      { apply (wf_retired_bus_t b i (task_revive core l) (bus_wf_retired b W)). }
    + rewrite (spawn_refused_shape b core l i S Q). cbn [fst]. exact W.
  - rewrite (spawn_no_slot_shape b core l S). cbn [fst]. exact W.
Qed.

(* ── 18.7 the closure: no word over the five calls leaves the class ── *)

(* The oracle's I9 enumerates runs; the alphabet below is the same five calls,
 * and a run is a word over it.  A refusal is in the alphabet too - it is the
 * stuttering step, the one that leaves the bus untouched. *)
Inductive step : Type :=
  | Pub_step : nat -> step
  | Sub_step : nat -> step
  | Snd_step : nat -> nat -> step
  | Rcv_step : nat -> step
  | Spawn_step : nat -> list cref -> step.

(* the two obligations the shipped guards do not check, read off the call *)
Definition wf_step (st : step) : Prop :=
  match st with
  | Pub_step cap => 0 < cap /\ cap <= ring_capacity
  | Spawn_step _ l => NoDup l
  | _ => True
  end.

Definition apply_step (b : bus) (st : step) : bus :=
  match st with
  | Pub_step cap => fst (pub b cap)
  | Sub_step wid => fst (sub b NoTask wid)
  | Snd_step wid d => fst (snd b NoTask wid d)
  | Rcv_step rid => fst (rcv b NoTask rid)
  | Spawn_step core l => fst (spawn b core l)
  end.

Fixpoint run (b : bus) (steps : list step) : bus :=
  match steps with
  | nil => b
  | st :: tl => run (apply_step b st) tl
  end.

Lemma snd_never_makes_a_core : forall b cl wid d,
    b_cores (fst (snd b cl wid d)) = b_cores b.
Proof.
  intros b cl wid d.
  destruct (snd_admit b cl wid) eqn:A.
  - destruct (snd_fits b wid) eqn:F.
    + rewrite (snd_ok_bus b cl wid d A F). apply cores_inert_w.
    + rewrite (snd_full_bus b cl wid d A F). apply cores_inert_w.
  - rewrite (snd_bad_cursor_bus b cl wid d A). reflexivity.
Qed.

Lemma step_preserves_bus_wf : forall b st, bus_wf b -> wf_step st ->
    bus_wf (apply_step b st).
Proof.
  intros b st W Hst. unfold apply_step.
  destruct st as [cap|wid|wid d|rid|core l]; cbn [wf_step] in Hst.
  - apply (pub_preserves_bus_wf b cap); [ apply (proj1 Hst) | apply (proj2 Hst) | exact W ].
  - apply (sub_preserves_bus_wf b NoTask wid W).
  - apply (snd_preserves_bus_wf b NoTask wid d W).
  - apply (rcv_preserves_bus_wf b NoTask rid W).
  - apply (spawn_preserves_bus_wf b core l Hst W).
Qed.

Lemma run_preserves_bus_wf : forall steps b,
    bus_wf b -> (forall st, In st steps -> wf_step st) -> bus_wf (run b steps).
Proof.
  intros steps. induction steps as [|a t IH]; intros b W Hall.
  - exact W.
  - cbn [run]. apply IH.
    + apply (step_preserves_bus_wf b a).
      * exact W.
      * exact (Hall a (or_introl eq_refl)).
    + intros st Hin. apply (Hall st). right. exact Hin.
Qed.

Theorem interleavings_preserve_bus_wf : forall steps,
    (forall st, In st steps -> wf_step st) -> bus_wf (run bus0 steps).
Proof.
  intros steps Hall. apply (run_preserves_bus_wf steps bus0 bus_wf0 Hall).
Qed.

(* the box: at every state of every admissible interleaving, the star's laws
 * hold - one owner per cursor, one publisher per core, the pool unbroken *)
Corollary no_run_overdraws_the_pool : forall steps,
    (forall st, In st steps -> wf_step st) ->
    total_held (run bus0 steps) <= sector_pool.
Proof.
  intros steps Hall. apply (pool_never_overdrawn (run bus0 steps)).
  apply (interleavings_preserve_bus_wf steps Hall).
Qed.

Corollary no_run_exceeds_the_rings : forall steps,
    (forall st, In st steps -> wf_step st) ->
    total_held (run bus0 steps) <= max_writers * ring_capacity.
Proof.
  intros steps Hall. apply (total_resident_bounded (run bus0 steps)).
  apply (interleavings_preserve_bus_wf steps Hall).
Qed.

Corollary no_run_leaves_a_ring_over_cap : forall steps wid,
    (forall st, In st steps -> wf_step st) ->
    held (run bus0 steps) wid <= w_cap (b_w (run bus0 steps) wid).
Proof.
  intros steps wid Hall.
  apply (bus_wf_bounded (run bus0 steps) (interleavings_preserve_bus_wf steps Hall) wid).
Qed.

Corollary no_run_shares_a_cursor : forall steps t1 t2 c,
    (forall st, In st steps -> wf_step st) ->
    owns (run bus0 steps) t1 c = true -> owns (run bus0 steps) t2 c = true -> t1 = t2.
Proof.
  intros steps t1 t2 c Hall H1 H2.
  apply (bus_wf_ownership (run bus0 steps) (interleavings_preserve_bus_wf steps Hall)
            t1 t2 c H1 H2).
Qed.

Corollary no_run_has_two_publishers_on_a_core : forall steps t1 t2,
    (forall st, In st steps -> wf_step st) ->
    holds_pub (run bus0 steps) t1 = true -> holds_pub (run bus0 steps) t2 = true ->
    t_core (b_t (run bus0 steps) t1) = t_core (b_t (run bus0 steps) t2) -> t1 = t2.
Proof.
  intros steps t1 t2 Hall H1 H2 Hc.
  apply (bus_wf_star (run bus0 steps) (interleavings_preserve_bus_wf steps Hall)
            t1 t2 H1 H2 Hc).
Qed.

Lemma run_never_makes_a_core : forall steps b, b_cores (run b steps) = b_cores b.
Proof.
  intros steps. induction steps as [|a t IH]; intros b; cbn [run].
  - reflexivity.
  - rewrite IH. unfold apply_step.
    destruct a as [cap|wid|wid d|rid|core l].
    + apply pub_never_makes_a_core.
    + apply sub_never_makes_a_core.
    + apply snd_never_makes_a_core.
    + apply rcv_never_makes_a_core.
    + apply spawn_never_makes_a_core.
Qed.

Corollary the_shipped_core_count_survives_every_run : forall steps,
    b_cores (run bus0 steps) = cores_shipped.
Proof.
  intros steps. rewrite (run_never_makes_a_core steps bus0). reflexivity.
Qed.

(* ── 19. I10: the shipped modes, and a gate that cannot fake coverage ── *)

(* The oracle's §7.4 gate decides, for each verification row, whether this port
 * may call itself covered.  Two things are modelled here: the flag (BTRON_MP)
 * decides which rows are compiled in at all, and the core count the port
 * actually has decides whether a compiled row is evidence.  A row that is not
 * compiled in is SKIP, and a row that is demanded but impossible is FAIL: the
 * gate can report neither as PASS. *)

Inductive mp_flag : Type := MP_ON | MP_OFF.

Inductive verdict : Type := V_PASS | V_SKIP | V_FAIL.

Definition verdict_eqb (a b : verdict) : bool :=
  match a, b with
  | V_PASS, V_PASS => true
  | V_SKIP, V_SKIP => true
  | V_FAIL, V_FAIL => true
  | _, _ => false
  end.

(* the five rows --mode=full prints *)
Inductive vrow : Type :=
  | R_ring | R_intercore | R_planes | R_amp | R_io_wcet.

Definition all_rows : list vrow :=
  [R_ring; R_intercore; R_planes; R_amp; R_io_wcet].

(* with the flag off, only the Tier-1 contract survives the build *)
Definition row_compiled (mp : mp_flag) (r : vrow) : bool :=
  match mp with
  | MP_OFF => match r with R_planes => true | _ => false end
  | MP_ON => true
  end.

(* the topology a row needs: the ring, the bus and the WCET row run on one core,
 * the AMP row needs the I/O core to exist *)
Definition row_has_the_topology (r : vrow) (cores : nat) : bool :=
  match r with
  | R_planes => true
  | R_amp => (io_core_id <? cores)
  | _ => Nat.leb 1 cores
  end.

Definition row_verdict (mp : mp_flag) (cores : nat) (require : bool) (r : vrow) : verdict :=
  if negb (row_compiled mp r) then V_SKIP
  else if row_has_the_topology r cores then V_PASS
       else if require then V_FAIL else V_SKIP.

Definition row_count (mp : mp_flag) (cores : nat) (require : bool) (v : verdict) : nat :=
  length (filter (fun r => verdict_eqb (row_verdict mp cores require r) v) all_rows).

Definition run_summary (mp : mp_flag) (cores : nat) (require : bool) : nat * (nat * nat) :=
  (row_count mp cores require V_PASS,
   (row_count mp cores require V_SKIP, row_count mp cores require V_FAIL)).

(* the arithmetic the rows rest on *)

Lemma bus0_has_the_shipped_core_count : b_cores bus0 = cores_shipped.
Proof. reflexivity. Qed.

Lemma shipped_is_below_the_target : cores_shipped < cores_target.
Proof. unfold cores_shipped, cores_target. lia. Qed.

Lemma the_io_core_is_the_last_target_core : io_core_id = cores_target - 1.
Proof. unfold io_core_id, cores_target. reflexivity. Qed.

Lemma the_io_core_is_not_shipped : cores_shipped <= io_core_id.
Proof. unfold cores_shipped, io_core_id. lia. Qed.

Lemma the_io_core_exists_only_on_the_target :
    forall cores, (io_core_id <? cores) = true <-> cores_target <= cores.
Proof.
  intros cores. unfold io_core_id, cores_target.
  split.
  { intros H. apply (proj1 (Nat.ltb_lt 3 cores)) in H. lia. }
  { intros H. apply (proj2 (Nat.ltb_lt 3 cores)). lia. }
Qed.

Lemma the_budget_is_two_periods : io_budget_us = 2 * io_period_us.
Proof. unfold io_budget_us, io_period_us. reflexivity. Qed.

Lemma the_period_fits_the_budget : io_period_us <= io_budget_us.
Proof. unfold io_period_us, io_budget_us. lia. Qed.

Lemma the_period_is_positive : 0 < io_period_us.
Proof. unfold io_period_us. lia. Qed.

Lemma the_ring_holds_four_sectors : ring_capacity = 4.
Proof. reflexivity. Qed.

Lemma the_pool_is_four_rings_per_writer : sector_pool = max_writers * 4 * ring_capacity.
Proof. unfold sector_pool, max_writers, ring_capacity. reflexivity. Qed.

(* the parked core: the shipped port refuses the I/O core in every argument,
 * and the refusal is the stuttering step - the bus is returned untouched *)
Lemma the_shipped_port_parks_the_io_core :
    forall l, spawn_ok bus0 io_core_id l = false.
Proof.
  intros l. unfold spawn_ok.
  rewrite (bus0_has_the_shipped_core_count). cbn. reflexivity.
Qed.

Theorem parked_core_never_gets_a_task : forall l, spawn bus0 io_core_id l = (bus0, None).
Proof.
  intros l. destruct (spawn_slot bus0) as [i|] eqn:S.
  - apply (spawn_refused_shape bus0 io_core_id l i S).
    exact (the_shipped_port_parks_the_io_core l).
  - exact (spawn_no_slot_shape bus0 io_core_id l S).
Qed.

(* the five §7.4 rows, computed *)

Theorem shipped_port_four_pass_one_skip :
    run_summary MP_ON cores_shipped false = (4, (1, 0)).
Proof. unfold run_summary, row_count, all_rows, cores_shipped. vm_compute. reflexivity. Qed.

Theorem demanding_amp_on_the_shipped_port_fails :
    run_summary MP_ON cores_shipped true = (4, (0, 1)).
Proof. unfold run_summary, row_count, all_rows, cores_shipped. vm_compute. reflexivity. Qed.

Theorem flag_off_is_one_pass_four_skip :
    run_summary MP_OFF cores_shipped false = (1, (4, 0)).
Proof. unfold run_summary, row_count, all_rows, cores_shipped. vm_compute. reflexivity. Qed.

Theorem planes_is_the_row_that_passes_with_the_flag_off :
    row_verdict MP_OFF cores_shipped false R_planes = V_PASS.
Proof. unfold row_verdict, row_compiled, row_has_the_topology. cbn. reflexivity. Qed.

Theorem amp_row_is_skip_not_pass_on_the_shipped_port :
    row_verdict MP_ON (b_cores bus0) false R_amp = V_SKIP.
Proof.
  unfold row_verdict, row_compiled, row_has_the_topology.
  rewrite bus0_has_the_shipped_core_count. unfold io_core_id, cores_shipped. cbn. reflexivity.
Qed.

Theorem no_gate_ever_reports_an_uncompiled_row :
    forall mp cores require r,
      row_verdict mp cores require r = V_PASS -> row_compiled mp r = true.
Proof.
  intros mp cores require r H. unfold row_verdict in H.
  destruct (row_compiled mp r) eqn:C; [| cbn in H; discriminate H].
  reflexivity.
Qed.

Theorem the_amp_row_passes_only_with_the_io_core :
    forall cores require, row_verdict MP_ON cores require R_amp = V_PASS -> io_core_id < cores.
Proof.
  intros cores require H. unfold row_verdict in H.
  cbn [row_compiled negb] in H.
  destruct (row_has_the_topology R_amp cores) eqn:T.
  - cbn [row_has_the_topology] in T.
    apply (proj1 (Nat.ltb_lt io_core_id cores)). exact T.
  - destruct require; discriminate H.
Qed.

Theorem the_target_topology_passes_every_row :
    forall r, row_verdict MP_ON cores_target true r = V_PASS.
Proof.
  intros r. unfold row_verdict, row_compiled, row_has_the_topology, io_core_id, cores_target.
  destruct r; cbn; reflexivity.
Qed.

(* a request is not a topology: --cores=N echoes both numbers because the row
 * must carry the memory model it was run under *)
Definition cores_echo (req have : nat) : nat * nat := (req, have).

Theorem the_request_does_not_change_the_port :
    forall req, pr2 (cores_echo req (b_cores bus0)) = cores_shipped.
Proof.
  intros req. unfold cores_echo.
  rewrite bus0_has_the_shipped_core_count. reflexivity.
Qed.

(* ── 20. I11: the EVT queue, a masked ring with one writer per field ── *)

(* IO.txt §5.1 gives the I/O core a lock-free event ring shared with the core
 * that produces the events.  The shipped C masks the index (i & (N-1)) because
 * the bus has no divide; the model uses mod, which agrees with the mask exactly
 * when N is a power of two - and the row below is the proof that it is.
 * §5.2's rule is the one that makes the lock unnecessary: head is written only
 * by the consumer, tail only by the producer. *)

Definition evt_idx (k : nat) : nat := k mod event_queue_size.

Record evtq : Type := mk_evtq {
    q_buf  : nat -> option nat;
    q_head : nat;
    q_tail : nat
  }.

Definition evtq0 : evtq := mk_evtq (fun _ => None) 0 0.

Definition occupancy (e : evtq) : nat := q_tail e - q_head e.

Definition evt_room (e : evtq) : bool := Nat.ltb (occupancy e) event_queue_size.

(* the buffer alone: a write never touches head or tail *)
Definition evt_write (e : evtq) (k : nat) (v : option nat) : evtq :=
  mk_evtq (@upd (option nat) (q_buf e) k v) (q_head e) (q_tail e).

(* the producer's step: store at the masked tail, then publish the new tail *)
Definition evt_advance (e : evtq) (v : nat) : evtq :=
  mk_evtq (@upd (option nat) (q_buf e) (evt_idx (q_tail e)) (Some v))
          (q_head e) (S (q_tail e)).

Definition evt_put (e : evtq) (v : nat) : evtq * bool :=
  if evt_room e then (evt_advance e v, true) else (e, false).

(* the consumer's step: clear the masked head slot, then publish the new head *)
Definition evt_retire (e : evtq) : evtq :=
  mk_evtq (@upd (option nat) (q_buf e) (evt_idx (q_head e)) None)
          (S (q_head e)) (q_tail e).

Definition evt_get (e : evtq) : evtq * option nat :=
  if Nat.eqb (q_head e) (q_tail e) then (e, None)
  else (evt_retire e, q_buf e (evt_idx (q_head e))).

(* the buffer is a total table, so a write is visible at its own index and
 * invisible anywhere else - that is the whole of the aliasing question *)
Lemma evt_write_buf_at_itself : forall e k v, q_buf (evt_write e k v) k = v.
Proof. intros e k v. unfold evt_write. cbn [q_buf]. apply upd_same. Qed.

Lemma evt_write_buf_elsewhere : forall e k v k', k <> k' ->
    q_buf (evt_write e k v) k' = q_buf e k'.
Proof.
  intros e k v k' H. unfold evt_write. cbn [q_buf].
  apply (upd_other (option nat) (q_buf e) k v k'). exact H.
Qed.

Lemma evt_write_leaves_head_alone : forall e k v, q_head (evt_write e k v) = q_head e.
Proof. reflexivity. Qed.

Lemma evt_write_leaves_tail_alone : forall e k v, q_tail (evt_write e k v) = q_tail e.
Proof. reflexivity. Qed.

Lemma evt_advance_stores_at_the_tail : forall e v,
    q_buf (evt_advance e v) (evt_idx (q_tail e)) = Some v.
Proof. intros e v. unfold evt_advance. cbn [q_buf]. apply upd_same. Qed.

Lemma evt_advance_leaves_head_alone : forall e v, q_head (evt_advance e v) = q_head e.
Proof. reflexivity. Qed.

Lemma evt_advance_publishes_the_tail : forall e v,
    q_tail (evt_advance e v) = S (q_tail e).
Proof. reflexivity. Qed.

Lemma evt_retire_clears_the_head_slot : forall e,
    q_buf (evt_retire e) (evt_idx (q_head e)) = None.
Proof. intros e. unfold evt_retire. cbn [q_buf]. apply upd_same. Qed.

Lemma evt_retire_leaves_tail_alone : forall e, q_tail (evt_retire e) = q_tail e.
Proof. reflexivity. Qed.

Lemma evt_retire_publishes_the_head : forall e, q_head (evt_retire e) = S (q_head e).
Proof. reflexivity. Qed.

(* the room test, read as a Prop *)
Lemma evt_room_true : forall e, evt_room e = true -> occupancy e < event_queue_size.
Proof.
  intros e R. unfold evt_room in R.
  apply (proj1 (Nat.ltb_lt (occupancy e) event_queue_size)). exact R.
Qed.

Lemma evt_room_false : forall e, evt_room e = false -> event_queue_size <= occupancy e.
Proof.
  intros e R. unfold evt_room in R.
  apply (proj1 (Nat.ltb_ge (occupancy e) event_queue_size)). exact R.
Qed.

(* the capacity row *)

Theorem evt_queue_is_256_slots : event_queue_size = 256.
Proof. vm_compute. reflexivity. Qed.

Theorem evt_queue_is_a_power_of_two : event_queue_size = 2 ^ 8.
Proof. reflexivity. Qed.

Theorem evt_index_is_always_in_range : forall k, evt_idx k < event_queue_size.
Proof.
  intros k. unfold evt_idx.
  apply Nat.mod_upper_bound. unfold event_queue_size. cbn. lia.
Qed.

Theorem evt_index_is_the_identity_below_the_ring :
    forall k, k < event_queue_size -> evt_idx k = k.
Proof.
  intros k H. unfold evt_idx. apply Nat.mod_small. exact H.
Qed.

(* the wrap: the mask of a lap is the mask of the mile *)
Theorem evt_index_wraps : forall k, evt_idx (k + event_queue_size) = evt_idx k.
Proof.
  intros k. unfold evt_idx.
  rewrite Nat.add_mod by (unfold event_queue_size; cbn; lia).
  rewrite (Nat.mod_same event_queue_size) by (unfold event_queue_size; cbn; lia).
  rewrite Nat.add_0_r.
  rewrite (Nat.mod_mod) by (unfold event_queue_size; cbn; lia).
  reflexivity.
Qed.

Theorem evt_index_wraps_twice : forall k, evt_idx (k + 2 * event_queue_size) = evt_idx k.
Proof.
  intros k.
  replace (k + 2 * event_queue_size) with ((k + event_queue_size) + event_queue_size) by lia.
  rewrite evt_index_wraps, evt_index_wraps. reflexivity.
Qed.

(* the full ring refuses: the producer is turned away, the queue is untouched *)
Theorem evt_full_refuses_the_producer : forall e v,
    event_queue_size <= occupancy e -> evt_put e v = (e, false).
Proof.
  intros e v H. unfold evt_put, evt_room.
  rewrite (proj2 (Nat.ltb_ge (occupancy e) event_queue_size)) by exact H.
  cbn. reflexivity.
Qed.

Theorem evt_room_admits_the_producer : forall e v,
    occupancy e < event_queue_size -> pr2 (evt_put e v) = true.
Proof.
  intros e v H. unfold evt_put, evt_room.
  rewrite (proj2 (Nat.ltb_lt (occupancy e) event_queue_size)) by exact H.
  cbn. reflexivity.
Qed.

(* §5.2: one writer per field.  The producer owns tail, the consumer owns head. *)
Theorem evt_put_never_moves_head : forall e v,
    q_head (fst (evt_put e v)) = q_head e.
Proof.
  intros e v. unfold evt_put, evt_advance.
  destruct (evt_room e); cbn [fst q_head]; reflexivity.
Qed.

Theorem evt_put_bumps_the_tail_only_when_admitted : forall e v,
    q_tail (fst (evt_put e v)) = if evt_room e then S (q_tail e) else q_tail e.
Proof.
  intros e v. unfold evt_put, evt_advance.
  destruct (evt_room e); cbn [fst q_tail]; reflexivity.
Qed.

Theorem evt_put_stores_the_item_only_when_admitted : forall e v,
    q_buf (fst (evt_put e v)) (evt_idx (q_tail e)) =
      if evt_room e then Some v else q_buf e (evt_idx (q_tail e)).
Proof.
  intros e v. unfold evt_put, evt_advance.
  destruct (evt_room e); cbn [fst].
  - cbn [q_buf]. apply upd_same.
  - reflexivity.
Qed.

Theorem evt_get_never_moves_tail : forall e, q_tail (fst (evt_get e)) = q_tail e.
Proof.
  intros e. unfold evt_get, evt_retire.
  destruct (Nat.eqb (q_head e) (q_tail e)); cbn [fst q_tail]; reflexivity.
Qed.

Theorem evt_get_bumps_the_head_only_with_data : forall e,
    q_head (fst (evt_get e)) =
      if Nat.eqb (q_head e) (q_tail e) then q_head e else S (q_head e).
Proof.
  intros e. unfold evt_get, evt_retire.
  destruct (Nat.eqb (q_head e) (q_tail e)); cbn [fst q_head]; reflexivity.
Qed.

(* the empty queue answers nothing, and never a stale slot *)
Theorem evt_get_on_empty_is_none : forall e, q_head e = q_tail e -> evt_get e = (e, None).
Proof.
  intros e H. unfold evt_get.
  rewrite (proj2 (Nat.eqb_eq (q_head e) (q_tail e))) by exact H.
  cbn. reflexivity.
Qed.

Theorem evt_get_reads_the_head_slot : forall e,
    q_head e <> q_tail e ->
    pr2 (evt_get e) = q_buf e (evt_idx (q_head e)).
Proof.
  intros e H. unfold evt_get.
  rewrite (proj2 (Nat.eqb_neq (q_head e) (q_tail e))) by exact H.
  cbn. reflexivity.
Qed.

Theorem evt_get_clears_the_slot_it_reads : forall e,
    q_head e <> q_tail e ->
    q_buf (fst (evt_get e)) (evt_idx (q_head e)) = None.
Proof.
  intros e H. unfold evt_get.
  rewrite (proj2 (Nat.eqb_neq (q_head e) (q_tail e))) by exact H.
  cbn [fst]. apply evt_retire_clears_the_head_slot.
Qed.

(* the handoff: a producer putting into an empty ring is read back by the
 * consumer at the very next position, and the ring is empty again *)
Theorem evt_handoff_delivers_the_item : forall e v,
    q_head e = q_tail e -> occupancy e < event_queue_size ->
    pr2 (evt_get (fst (evt_put e v))) = Some v
    /\ q_head (fst (evt_get (fst (evt_put e v))))
       = q_tail (fst (evt_get (fst (evt_put e v)))).
Proof.
  intros e v H0 Hroom.
  assert (Room : evt_room e = true).
  { unfold evt_room.
    apply (proj2 (Nat.ltb_lt (occupancy e) event_queue_size)). exact Hroom. }
  assert (Ne : q_head (fst (evt_put e v)) <> q_tail (fst (evt_put e v))).
  { rewrite evt_put_never_moves_head,
            evt_put_bumps_the_tail_only_when_admitted, Room.
    cbn. lia. }
  assert (HeadStep :
      q_head (fst (evt_get (fst (evt_put e v)))) = S (q_head (fst (evt_put e v)))).
  { rewrite evt_get_bumps_the_head_only_with_data.
    rewrite (proj2 (Nat.eqb_neq (q_head (fst (evt_put e v)))
                                (q_tail (fst (evt_put e v))))) by exact Ne.
    cbn. reflexivity. }
  rewrite (evt_get_reads_the_head_slot (fst (evt_put e v))) by exact Ne.
  rewrite evt_put_never_moves_head.
  replace (evt_idx (q_head e)) with (evt_idx (q_tail e)) by (rewrite H0; reflexivity).
  rewrite evt_put_stores_the_item_only_when_admitted, Room. cbn.
  split.
  { reflexivity. }
  { rewrite HeadStep, evt_get_never_moves_tail, evt_put_never_moves_head,
            evt_put_bumps_the_tail_only_when_admitted, Room.
    cbn. rewrite H0. reflexivity. }
Qed.

(* the occupancy accounting, in one step per call *)
Theorem occupancy_after_put : forall e v,
    q_head e <= q_tail e ->
    occupancy (fst (evt_put e v)) = if evt_room e then S (occupancy e) else occupancy e.
Proof.
  intros e v Hle. unfold evt_put, evt_advance, occupancy.
  destruct (evt_room e); cbn [fst q_head q_tail]; lia.
Qed.

Theorem occupancy_after_get : forall e,
    q_head e <= q_tail e ->
    occupancy (fst (evt_get e)) =
      if Nat.eqb (q_head e) (q_tail e) then occupancy e else occupancy e - 1.
Proof.
  intros e Hle. unfold evt_get, evt_retire, occupancy.
  destruct (Nat.eqb (q_head e) (q_tail e)); cbn [fst q_head q_tail]; lia.
Qed.

(* the class the ring stays in: head never overtakes tail, and the occupancy
 * never exceeds the capacity - the two facts the C relies on when it reads the
 * pair without a lock *)
Definition evt_wf (e : evtq) : Prop :=
  q_head e <= q_tail e /\ occupancy e <= event_queue_size.

Theorem evtq0_is_wf : evt_wf evtq0.
Proof. unfold evt_wf, occupancy. cbn. split; lia. Qed.

Theorem evt_put_preserves_wf : forall e v, evt_wf e -> evt_wf (fst (evt_put e v)).
Proof.
  intros e v W. unfold evt_put.
  destruct (evt_room e) eqn:R.
  - destruct W as [Hle Hold]. unfold occupancy in Hold.
    assert (LT : occupancy e < event_queue_size)
      by (apply (evt_room_true e); exact R).
    unfold occupancy in LT. cbn [fst]. unfold evt_wf, occupancy, evt_advance.
    cbn [q_head q_tail]. split; lia.
  - cbn [fst]. exact W.
Qed.

Theorem evt_get_preserves_wf : forall e, evt_wf e -> evt_wf (fst (evt_get e)).
Proof.
  intros e W. unfold evt_get.
  destruct (Nat.eqb (q_head e) (q_tail e)) eqn:R.
  - cbn [fst]. exact W.
  - destruct W as [Hle Hold]. unfold occupancy in Hold.
    assert (Ne : q_head e <> q_tail e)
      by (apply (proj1 (Nat.eqb_neq (q_head e) (q_tail e))); exact R).
    cbn [fst]. unfold evt_wf, occupancy, evt_retire.
    cbn [q_head q_tail]. split; lia.
Qed.

Theorem evt_wf_head_never_passes_tail : forall e, evt_wf e -> q_head e <= q_tail e.
Proof. intros e W. destruct W as [H _]. exact H. Qed.

Theorem evt_wf_occupancy_bounded : forall e, evt_wf e -> occupancy e <= event_queue_size.
Proof. intros e W. destruct W as [_ H]. exact H. Qed.

(* ── 21. I11 continued: the soak - 600 producer/consumer steps, in order ── *)

(* media_intercore.ml §10 runs one producer and one consumer for 600 steps and
 * checks that every item comes back, in the order it went in.  The loop below is
 * that soak written as a Coq function: one put, one get, per step.  The division
 * of labour is the honest one - the laws are proved for every n, and the
 * 600-step instance is computed. *)

Lemma pair_of_get : forall X, evt_get X = (fst (evt_get X), pr2 (evt_get X)).
Proof. intros X. destruct (evt_get X) as [e o]; cbn [fst pr2]; reflexivity. Qed.

(* one round trip: produce, then consume, and keep the item that came back.  The
 * 0 in the None arm is a sentinel that the laws below show is never reached. *)
Definition soak_step (i : nat) (e : evtq) : evtq * nat :=
  match evt_get (fst (evt_put e i)) with
  | (e2, Some v) => (e2, v)
  | (e2, None) => (e2, 0)
  end.

Definition step_q (i : nat) (e : evtq) : evtq := fst (soak_step i e).
Definition step_v (i : nat) (e : evtq) : nat := pr2 (soak_step i e).

Fixpoint soak (n i : nat) (e : evtq) (acc : list nat) : evtq * list nat :=
  match n with
  | 0 => (e, acc)
  | S n' => soak n' (S i) (step_q i e) (step_v i e :: acc)
  end.

(* one step, proved: the item that goes into an empty ring comes straight back,
 * the ring is empty again, and there is room for the next one *)
Theorem one_step_delivers_its_own_item : forall e i,
    q_head e = q_tail e -> occupancy e < event_queue_size ->
    pr2 (evt_get (fst (evt_put e i))) = Some i.
Proof.
  intros e i H0 H1. apply (proj1 (evt_handoff_delivers_the_item e i H0 H1)).
Qed.

Theorem one_step_leaves_the_ring_empty : forall e i,
    q_head e = q_tail e -> occupancy e < event_queue_size ->
    q_head (fst (evt_get (fst (evt_put e i)))) = q_tail (fst (evt_get (fst (evt_put e i)))).
Proof.
  intros e i H0 H1. apply (proj2 (evt_handoff_delivers_the_item e i H0 H1)).
Qed.

Theorem one_step_leaves_room : forall e i,
    q_head e = q_tail e -> occupancy e < event_queue_size ->
    occupancy (fst (evt_get (fst (evt_put e i)))) < event_queue_size.
Proof.
  intros e i H0 H1.
  assert (E : q_head (fst (evt_get (fst (evt_put e i))))
              = q_tail (fst (evt_get (fst (evt_put e i)))))
    by (apply one_step_leaves_the_ring_empty; assumption).
  unfold occupancy. rewrite E. rewrite Nat.sub_diag.
  unfold event_queue_size. cbn. lia.
Qed.

Lemma step_q_is_the_consumer_queue : forall i e,
    step_q i e = fst (evt_get (fst (evt_put e i))).
Proof.
  intros i e. unfold step_q, soak_step.
  destruct (evt_get (fst (evt_put e i))) as [e2 [v|]]; cbn [fst]; reflexivity.
Qed.

Lemma step_v_of_an_empty_ring : forall e i,
    q_head e = q_tail e -> occupancy e < event_queue_size -> step_v i e = i.
Proof.
  intros e i H0 H1. unfold step_v, soak_step.
  rewrite pair_of_get, (one_step_delivers_its_own_item e i H0 H1).
  cbn. reflexivity.
Qed.

Lemma step_q_stays_empty : forall e i,
    q_head e = q_tail e -> occupancy e < event_queue_size ->
    q_head (step_q i e) = q_tail (step_q i e).
Proof.
  intros e i H0 H1. rewrite step_q_is_the_consumer_queue.
  apply (one_step_leaves_the_ring_empty e i H0 H1).
Qed.

Lemma step_q_leaves_room : forall e i,
    q_head e = q_tail e -> occupancy e < event_queue_size ->
    occupancy (step_q i e) < event_queue_size.
Proof.
  intros e i H0 H1. rewrite step_q_is_the_consumer_queue.
  apply (one_step_leaves_room e i H0 H1).
Qed.

Lemma step_q_is_wf : forall i e, evt_wf e -> evt_wf (step_q i e).
Proof.
  intros i e W. rewrite step_q_is_the_consumer_queue.
  apply evt_get_preserves_wf, evt_put_preserves_wf. exact W.
Qed.

(* for every number of steps the soak reads back exactly one item per step:
 * nothing is lost, nothing is duplicated, and the ring never stalls *)
Theorem soak_reads_one_item_per_step : forall n i e acc,
    q_head e = q_tail e -> occupancy e < event_queue_size ->
    length (pr2 (soak n i e acc)) = n + length acc.
Proof.
  induction n as [| n IH]; intros i e acc H0 H1.
  - cbn. reflexivity.
  - cbn [soak].
    rewrite (IH (S i) (step_q i e) (step_v i e :: acc)).
    { rewrite (step_v_of_an_empty_ring e i H0 H1). cbn [length]. lia. }
    { apply (step_q_stays_empty e i H0 H1). }
    { apply (step_q_leaves_room e i H0 H1). }
Qed.

Theorem soak_preserves_evt_wf : forall n i e acc,
    evt_wf e -> evt_wf (fst (soak n i e acc)).
Proof.
  induction n as [| n IH]; intros i e acc W.
  - cbn [soak]. exact W.
  - cbn [soak]. apply (IH (S i)). apply step_q_is_wf. exact W.
Qed.

Theorem soak_head_never_passes_tail : forall n i e acc,
    evt_wf e -> q_head (fst (soak n i e acc)) <= q_tail (fst (soak n i e acc)).
Proof.
  intros n i e acc W.
  apply (evt_wf_head_never_passes_tail (fst (soak n i e acc))).
  apply (soak_preserves_evt_wf n i e acc). exact W.
Qed.

(* the computed instance: 600 steps through a 256-slot ring, so the masked index
 * wraps twice along the way *)
Theorem soak_of_eight_is_fifo : rev (pr2 (soak 8 1 evtq0 nil)) = seq 1 8.
Proof. vm_compute. reflexivity. Qed.

Theorem soak_of_six_hundred_is_fifo : rev (pr2 (soak 600 1 evtq0 nil)) = seq 1 600.
Proof. vm_compute. reflexivity. Qed.

Theorem soak_of_six_hundred_leaves_the_ring_empty :
    q_head (fst (soak 600 1 evtq0 nil)) = 600
    /\ q_tail (fst (soak 600 1 evtq0 nil)) = 600.
Proof. vm_compute. split; reflexivity. Qed.

Theorem soak_of_six_hundred_reads_its_own_count :
    length (pr2 (soak 600 1 evtq0 nil)) = 600.
Proof.
  rewrite (soak_reads_one_item_per_step 600 1 evtq0 nil).
  - cbn [length]. lia.
  - unfold occupancy, event_queue_size. cbn. lia.
  - unfold occupancy, event_queue_size. cbn. lia.
Qed.

Theorem soak_of_six_hundred_is_well_formed : evt_wf (fst (soak 600 1 evtq0 nil)).
Proof.
  apply (soak_preserves_evt_wf 600 1 evtq0 nil). apply evtq0_is_wf.
Qed.

(* the index at the two wrap points, computed - the row that ties the mask of the
 * shipped C to the mod of the model *)
Theorem masked_index_at_256 : evt_idx 256 = 0.
Proof. unfold evt_idx. vm_compute. reflexivity. Qed.

Theorem masked_index_at_512 : evt_idx 512 = 0.
Proof. unfold evt_idx. vm_compute. reflexivity. Qed.

Theorem masked_index_at_600 : evt_idx 600 = 88.
Proof. unfold evt_idx. vm_compute. reflexivity. Qed.

(* ── 22. I12: what two cores can and cannot tell about each other ───── *)

(* Everything above is a theorem about ONE cursor moving: an actor calls, the
 * bus answers, and the next call sees the answer.  That framing is a
 * sequential reading of the protocol, and it is exactly what a single-core
 * port can execute.  The five sections that follow ask the questions that only
 * make sense when two cores are running and nobody has decided the order
 * between them:
 *
 *   - if core A and core B each write their own cell, is there any observable
 *     difference between A-first and B-first?  (I12, the commutation)
 *   - what does it mean for a law to hold of a multicore run when the schedule
 *     is not chosen?  The honest answer is modal: quantify over the successors,
 *     box and diamond, and prove the logic that survives that quantification.
 *     (I13)
 *   - is the schedule genuinely non-determined, or is it a sequential loop with
 *     a new name?  A witness is required, not an assertion.  (I14)
 *   - what does a reader on core B see of a store core A has performed but not
 *     yet published?  (I15, the visibility gap)
 *   - how long can an enabled reader be kept from reading?  (I16, bounded
 *     starvation)
 *
 * The equality used throughout is OBSERVATIONAL, not propositional: two buses
 * agree when every cell of every table agrees and the core count agrees.  It is
 * deliberately weaker than bus equality, and it has to be - the bus is a record
 * of three functions nat -> cell, so propositional equality between two
 * differently-built buses needs functional extensionality, which is an axiom,
 * and this file closes on none.  The weakness costs nothing: §22.2 lifts the
 * whole bus_wf class across it, so every law the protocol is stated with is a
 * law of the observable bus, not of the representation. *)

Definition obs_eq (b1 b2 : bus) : Prop :=
  b_cores b1 = b_cores b2
  /\ (forall j, b_w b1 j = b_w b2 j)
  /\ (forall j, b_r b1 j = b_r b2 j)
  /\ (forall j, b_t b1 j = b_t b2 j).

Lemma obs_eq_refl : forall b, obs_eq b b.
Proof.
  intros b. unfold obs_eq. split.
  - reflexivity.
  - split.
    + intros j; reflexivity.
    + split.
      * intros j; reflexivity.
      * intros j; reflexivity.
Qed.

Lemma obs_eq_sym : forall b1 b2, obs_eq b1 b2 -> obs_eq b2 b1.
Proof.
  intros b1 b2 [Hc [Hw [Hr Ht]]]. unfold obs_eq. split.
  - symmetry; exact Hc.
  - split.
    + intros j; symmetry; apply Hw.
    + split.
      * intros j; symmetry; apply Hr.
      * intros j; symmetry; apply Ht.
Qed.

Lemma obs_eq_trans : forall b1 b2 b3, obs_eq b1 b2 -> obs_eq b2 b3 -> obs_eq b1 b3.
Proof.
  intros b1 b2 b3 [Hc1 [Hw1 [Hr1 Ht1]]] [Hc2 [Hw2 [Hr2 Ht2]]].
  unfold obs_eq. split.
  - rewrite Hc1, Hc2; reflexivity.
  - split.
    + intros j; rewrite (Hw1 j), (Hw2 j); reflexivity.
    + split.
      * intros j; rewrite (Hr1 j), (Hr2 j); reflexivity.
      * intros j; rewrite (Ht1 j), (Ht2 j); reflexivity.
Qed.

(* the arithmetic the commutation needs: two writes to different indices of the
 * same table agree pointwise in either order.  The four cases are the two
 * boolean tests inside upd, and the impossible one - both indices equal k while
 * the indices differ - closes itself. *)
Lemma upd_comm_pointwise : forall (A : Type) (t : nat -> A) i (x : A) j (y : A),
    i <> j ->
    forall k, @upd A (@upd A t i x) j y k = @upd A (@upd A t j y) i x k.
Proof.
  intros A t i x j y Hij k. unfold upd.
  destruct (Nat.eqb_spec i k) as [Hik|Hik];
    destruct (Nat.eqb_spec j k) as [Hjk|Hjk].
  - exfalso. apply Hij. rewrite Hik. symmetry. exact Hjk.
  - reflexivity.
  - reflexivity.
  - reflexivity.
Qed.

(* writing the same cell twice with the same value is a no-op - the shape of a
 * CAS that succeeds and is retried, or of a core that re-reads its own row *)
Lemma upd_idem : forall (A : Type) (t : nat -> A) i (x : A) k,
    @upd A (@upd A t i x) i x k = @upd A t i x k.
Proof.
  intros A t i x k. unfold upd.
  destruct (Nat.eqb_spec i k) as [Hik|Hik]; reflexivity.
Qed.


(* two cores, two grants, one publisher table: whichever way the scheduler
 * interleaves the writes, every cell of the table says the same thing *)
Lemma bus_w_bus_w_commute : forall b i1 (w1 : writer) i2 (w2 : writer), i1 <> i2 ->
    obs_eq (bus_w (bus_w b i1 w1) i2 w2) (bus_w (bus_w b i2 w2) i1 w1).
Proof.
  intros b i1 w1 i2 w2 Hij. unfold obs_eq, bus_w. split.
  - reflexivity.
  - split.
    + intros j; apply upd_comm_pointwise; exact Hij.
    + split.
      * intros j; reflexivity.
      * intros j; reflexivity.
Qed.

Lemma bus_r_bus_r_commute : forall b i1 (r1 : reader) i2 (r2 : reader), i1 <> i2 ->
    obs_eq (bus_r (bus_r b i1 r1) i2 r2) (bus_r (bus_r b i2 r2) i1 r1).
Proof.
  intros b i1 r1 i2 r2 Hij. unfold obs_eq, bus_r. split.
  - reflexivity.
  - split.
    + intros j; reflexivity.
    + split.
      * intros j; apply upd_comm_pointwise; exact Hij.
      * intros j; reflexivity.
Qed.

Lemma bus_t_bus_t_commute : forall b i1 (t1 : task) i2 (t2 : task), i1 <> i2 ->
    obs_eq (bus_t (bus_t b i1 t1) i2 t2) (bus_t (bus_t b i2 t2) i1 t1).
Proof.
  intros b i1 t1 i2 t2 Hij. unfold obs_eq, bus_t. split.
  - reflexivity.
  - split.
    + intros j; reflexivity.
    + split.
      * intros j; reflexivity.
      * intros j; apply upd_comm_pointwise; exact Hij.
Qed.

(* different tables cannot contend at all: no side condition is needed *)
Lemma bus_w_bus_r_commute : forall b i (w : writer) j (r : reader),
    obs_eq (bus_r (bus_w b i w) j r) (bus_w (bus_r b j r) i w).
Proof.
  intros b i w j r. unfold obs_eq, bus_w, bus_r. split.
  - reflexivity.
  - split.
    + intros k; reflexivity.
    + split.
      * intros k; reflexivity.
      * intros k; reflexivity.
Qed.

Lemma bus_w_bus_t_commute : forall b i (w : writer) j (t : task),
    obs_eq (bus_t (bus_w b i w) j t) (bus_w (bus_t b j t) i w).
Proof.
  intros b i w j t. unfold obs_eq, bus_w, bus_t. split.
  - reflexivity.
  - split.
    + intros k; reflexivity.
    + split.
      * intros k; reflexivity.
      * intros k; reflexivity.
Qed.

Lemma bus_r_bus_t_commute : forall b i (r : reader) j (t : task),
    obs_eq (bus_t (bus_r b i r) j t) (bus_r (bus_t b j t) i r).
Proof.
  intros b i r j t. unfold obs_eq, bus_r, bus_t. split.
  - reflexivity.
  - split.
    + intros k; reflexivity.
    + split.
      * intros k; reflexivity.
      * intros k; reflexivity.
Qed.

(* a core that re-reads its own cell and writes it back - the observable effect
 * of a CAS that succeeds - changes nothing at all, in any cell, for any core.
 * This is the refresh identity that makes a retry invisible across the fabric. *)
Lemma bus_w_cas_invisible : forall B j, obs_eq (bus_w B j (b_w B j)) B.
Proof.
  intros B j. unfold obs_eq. split.
  - reflexivity.
  - split.
    + intros k. destruct (Nat.eqb_spec j k) as [Hjk|Hjk].
        { subst k. apply at_w_same_w. }
        { apply at_w_other_w. exact Hjk. }
    + split.
      * intros k; reflexivity.
      * intros k; reflexivity.
Qed.

Lemma bus_r_cas_invisible : forall B j, obs_eq (bus_r B j (b_r B j)) B.
Proof.
  intros B j. unfold obs_eq. split.
  - reflexivity.
  - split.
    + intros k; reflexivity.
    + split.
      * intros k. destruct (Nat.eqb_spec j k) as [Hjk|Hjk].
        { subst k. apply at_r_same_r. }
        { apply at_r_other_r. exact Hjk. }
      * intros k; reflexivity.
Qed.

Lemma bus_t_cas_invisible : forall B j, obs_eq (bus_t B j (b_t B j)) B.
Proof.
  intros B j. unfold obs_eq. split.
  - reflexivity.
  - split.
    + intros k; reflexivity.
    + split.
      * intros k; reflexivity.
      * intros k. destruct (Nat.eqb_spec j k) as [Hjk|Hjk].
        { subst k. apply at_t_same_t. }
        { apply at_t_other_t. exact Hjk. }
Qed.

(* ── 22.1 the observations, lifted cell by cell ───────────────────────── *)

(* A task can only ever read a cell: a cursor, a tail, a grant.  Each of the
 * protocol's own numbers is therefore an observable, which is what makes the
 * weaker equality the right one to commute under. *)
Lemma obs_eq_cores : forall b1 b2, obs_eq b1 b2 -> b_cores b1 = b_cores b2.
Proof. intros b1 b2 [Hc _]. exact Hc. Qed.

Lemma obs_eq_w : forall b1 b2, obs_eq b1 b2 -> forall i, b_w b1 i = b_w b2 i.
Proof. intros b1 b2 E i. destruct E as [_ [Hw _]]. exact (Hw i). Qed.

Lemma obs_eq_r : forall b1 b2, obs_eq b1 b2 -> forall i, b_r b1 i = b_r b2 i.
Proof. intros b1 b2 E i. destruct E as [_ [_ [Hr _]]]. exact (Hr i). Qed.

Lemma obs_eq_t : forall b1 b2, obs_eq b1 b2 -> forall i, b_t b1 i = b_t b2 i.
Proof. intros b1 b2 E i. destruct E as [_ [_ [_ Ht]]]. exact (Ht i). Qed.

Lemma obs_eq_w_live : forall b1 b2, obs_eq b1 b2 -> forall i,
    w_live (b_w b1 i) = w_live (b_w b2 i).
Proof. intros b1 b2 E i. rewrite (obs_eq_w b1 b2 E i). reflexivity. Qed.

Lemma obs_eq_r_live : forall b1 b2, obs_eq b1 b2 -> forall i,
    r_live (b_r b1 i) = r_live (b_r b2 i).
Proof. intros b1 b2 E i. rewrite (obs_eq_r b1 b2 E i). reflexivity. Qed.

Lemma obs_eq_enq : forall b1 b2, obs_eq b1 b2 -> forall i,
    w_enq (b_w b1 i) = w_enq (b_w b2 i).
Proof. intros b1 b2 E i. unfold w_enq. rewrite (obs_eq_w b1 b2 E i). reflexivity. Qed.

Lemma obs_eq_cap : forall b1 b2, obs_eq b1 b2 -> forall i,
    w_cap (b_w b1 i) = w_cap (b_w b2 i).
Proof. intros b1 b2 E i. rewrite (obs_eq_w b1 b2 E i). reflexivity. Qed.

Lemma obs_eq_cursor_live : forall b1 b2, obs_eq b1 b2 -> forall c,
    cursor_live b1 c = cursor_live b2 c.
Proof.
  intros b1 b2 E c. unfold cursor_live.
  destruct c as [i|j].
  - apply obs_eq_w_live. exact E.
  - apply obs_eq_r_live. exact E.
Qed.

Lemma obs_eq_owned : forall b1 b2, obs_eq b1 b2 -> forall t c,
    owns b1 t c = owns b2 t c.
Proof. intros b1 b2 E t c. unfold owns. rewrite (obs_eq_t b1 b2 E t). reflexivity. Qed.

(* a fold over a table only sees the table, so it too is an observable *)
Lemma existsb_pointwise : forall (A : Type) (f g : A -> bool) l,
    (forall x, In x l -> f x = g x) -> existsb f l = existsb g l.
Proof.
  intros A f g l H. induction l as [|a t IH].
  - reflexivity.
  - cbn [existsb]. rewrite (H a) by (left; reflexivity).
    destruct (g a); cbn [orb]; [reflexivity|].
    apply IH. intros x Hin. apply H. right; exact Hin.
Qed.

Lemma obs_eq_usable : forall b1 b2, obs_eq b1 b2 -> forall cl c,
    usable b1 cl c = usable b2 cl c.
Proof.
  intros b1 b2 E cl c. destruct cl as [|t].
  - unfold usable, cursor_taken. f_equal.
    apply existsb_pointwise. intros x _. apply (obs_eq_owned b1 b2 E x c).
  - unfold usable. apply (obs_eq_owned b1 b2 E t c).
Qed.

Lemma obs_eq_holds_pub : forall b1 b2, obs_eq b1 b2 -> forall t,
    holds_pub b1 t = holds_pub b2 t.
Proof.
  intros b1 b2 E t. unfold holds_pub. rewrite (obs_eq_t b1 b2 E t). reflexivity.
Qed.

Lemma floor_of_obs : forall b1 b2, obs_eq b1 b2 -> forall wid e,
    floor_of b1 wid e = floor_of b2 wid e.
Proof.
  intros b1 b2 E wid e. unfold floor_of. apply min_fold_cong.
  intros j Le Lt. split.
  - unfold sub_reader. rewrite (obs_eq_r b1 b2 E j). reflexivity.
  - rewrite (obs_eq_r b1 b2 E j). reflexivity.
Qed.

Lemma obs_eq_held : forall b1 b2, obs_eq b1 b2 -> forall wid,
    held b1 wid = held b2 wid.
Proof.
  intros b1 b2 E wid. unfold held.
  rewrite (obs_eq_w b1 b2 E wid).
  rewrite (floor_of_obs b1 b2 E wid (w_enq (b_w b2 wid))).
  reflexivity.
Qed.

Lemma obs_eq_total_held : forall b1 b2, obs_eq b1 b2 -> total_held b1 = total_held b2.
Proof.
  intros b1 b2 E. unfold total_held. apply sum_f_pointwise. intros j Lt.
  apply (obs_eq_held b1 b2 E).
Qed.

Lemma obs_eq_pool_room : forall b1 b2, obs_eq b1 b2 -> pool_room b1 = pool_room b2.
Proof.
  intros b1 b2 E. unfold pool_room. rewrite (obs_eq_total_held b1 b2 E). reflexivity.
Qed.

Lemma obs_eq_pool_free : forall b1 b2, obs_eq b1 b2 -> pool_free b1 = pool_free b2.
Proof.
  intros b1 b2 E. unfold pool_free. rewrite (obs_eq_total_held b1 b2 E). reflexivity.
Qed.

(* a reader record is data the task carries, so only the bus half of these two
 * derived views needs lifting *)
Lemma obs_eq_pending : forall b1 b2, obs_eq b1 b2 -> forall r,
    pending b1 r = pending b2 r.
Proof.
  intros b1 b2 E r. unfold pending.
  rewrite (obs_eq_enq b1 b2 E (r_wid r)). reflexivity.
Qed.

Lemma obs_eq_received : forall b1 b2, obs_eq b1 b2 -> forall r,
    received b1 r = received b2 r.
Proof.
  intros b1 b2 E r. unfold received.
  rewrite (obs_eq_w b1 b2 E (r_wid r)). reflexivity.
Qed.

(* ── 22.2 the class is an observable ─────────────────────────────────── *)

(* Being well formed is a statement about cells, so it survives the weaker
 * equality: an interleaving cannot make a lawless bus look lawful, nor hide a
 * law from a lawful one. *)
Lemma obs_eq_wf_tables : forall b1 b2, obs_eq b1 b2 -> wf_tables b1 -> wf_tables b2.
Proof.
  intros b1 b2 E [H1 [H2 [H3 H4]]]. unfold wf_tables. split.
  - intros j Le. specialize (H1 j Le). rewrite <- (obs_eq_w b1 b2 E j). exact H1.
  - split.
    + intros j Le. specialize (H2 j Le). rewrite <- (obs_eq_r b1 b2 E j). exact H2.
    + split.
      * intros j Le. specialize (H3 j Le). rewrite <- (obs_eq_t b1 b2 E j). exact H3.
      * intros j. specialize (H4 j). rewrite <- (obs_eq_w b1 b2 E j). exact H4.
Qed.

Lemma obs_eq_wf_bounded : forall b1 b2, obs_eq b1 b2 -> wf_bounded b1 -> wf_bounded b2.
Proof.
  intros b1 b2 E Wb j. specialize (Wb j).
  rewrite (obs_eq_held b1 b2 E j), (obs_eq_cap b1 b2 E j) in Wb. exact Wb.
Qed.

Lemma obs_eq_wf_reader_bounds : forall b1 b2, obs_eq b1 b2 ->
    wf_reader_bounds b1 -> wf_reader_bounds b2.
Proof.
  intros b1 b2 E Wr k Lk.
  assert (RID : b_r b1 k = b_r b2 k) by (apply (obs_eq_r b1 b2 E)).
  rewrite <- RID in Lk.
  specialize (Wr k Lk). destruct Wr as [Lt [Wl [Hst Hcr]]].
  rewrite RID in Wl, Hst, Hcr.
  assert (WID : b_w b1 (r_wid (b_r b2 k)) = b_w b2 (r_wid (b_r b2 k)))
    by (apply (obs_eq_w b1 b2 E)).
  rewrite WID in Wl, Hcr.
  split; [exact Lt|]. split; [exact Wl|]. split; [exact Hst|]. exact Hcr.
Qed.

Lemma obs_eq_wf_tasks : forall b1 b2, obs_eq b1 b2 -> wf_tasks b1 -> wf_tasks b2.
Proof.
  intros b1 b2 E Wt t Lt.
  assert (TID : b_t b1 t = b_t b2 t) by (apply (obs_eq_t b1 b2 E t)).
  assert (CD : b_cores b1 = b_cores b2) by (apply (obs_eq_cores b1 b2 E)).
  rewrite <- TID in Lt.
  specialize (Wt t Lt). destruct Wt as [Lt2 [Lc [Hin [ND [Pc Nz]]]]].
  rewrite TID in Lc, Hin, ND, Pc, Nz.
  rewrite CD in Lc.
  split; [exact Lt2|]. split; [exact Lc|].
  split.
  - intros c Hin2. specialize (Hin c Hin2). destruct Hin as [Ex Liv].
    split; [exact Ex|].
    rewrite (obs_eq_cursor_live b1 b2 E c) in Liv. exact Liv.
  - split; [exact ND|]. split; [exact Pc|]. exact Nz.
Qed.

Lemma obs_eq_wf_ownership : forall b1 b2, obs_eq b1 b2 ->
    wf_ownership b1 -> wf_ownership b2.
Proof.
  intros b1 b2 E Wo t1 t2 c H1 H2.
  rewrite <- (obs_eq_owned b1 b2 E t1 c) in H1.
  rewrite <- (obs_eq_owned b1 b2 E t2 c) in H2.
  exact (Wo t1 t2 c H1 H2).
Qed.

Lemma obs_eq_wf_star : forall b1 b2, obs_eq b1 b2 -> wf_star b1 -> wf_star b2.
Proof.
  intros b1 b2 E Ws t1 t2 H1 H2 Hc.
  rewrite <- (obs_eq_holds_pub b1 b2 E t1) in H1.
  rewrite <- (obs_eq_holds_pub b1 b2 E t2) in H2.
  assert (C1 : t_core (b_t b1 t1) = t_core (b_t b2 t1))
    by (rewrite (obs_eq_t b1 b2 E t1); reflexivity).
  assert (C2 : t_core (b_t b1 t2) = t_core (b_t b2 t2))
    by (rewrite (obs_eq_t b1 b2 E t2); reflexivity).
  rewrite <- C1, <- C2 in Hc.
  apply (Ws t1 t2 H1 H2 Hc).
Qed.

Lemma obs_eq_wf_retired : forall b1 b2, obs_eq b1 b2 -> wf_retired b1 -> wf_retired b2.
Proof.
  intros b1 b2 E Wr j Lj.
  assert (W : b_w b1 j = b_w b2 j) by (apply (obs_eq_w b1 b2 E)).
  rewrite <- W in Lj.
  specialize (Wr j Lj). rewrite W in Wr. exact Wr.
Qed.

Theorem obs_eq_preserves_bus_wf : forall b1 b2, obs_eq b1 b2 -> bus_wf b1 -> bus_wf b2.
Proof.
  intros b1 b2 E W. destruct W as [Wt [Wb [Wr [Wf [Wo [Ws Wre]]]]]].
  unfold bus_wf. split; [apply (obs_eq_wf_tables b1 b2 E Wt)|].
  split; [apply (obs_eq_wf_bounded b1 b2 E Wb)|].
  split; [apply (obs_eq_wf_reader_bounds b1 b2 E Wr)|].
  split; [apply (obs_eq_wf_tasks b1 b2 E Wf)|].
  split; [apply (obs_eq_wf_ownership b1 b2 E Wo)|].
  split; [apply (obs_eq_wf_star b1 b2 E Ws)|].
  apply (obs_eq_wf_retired b1 b2 E Wre).
Qed.

(* ── 22.3 the multicore law, stated once and for all ─────────────────── *)

(* The two orders of two independent grants are the same run seen twice: if one
 * of them satisfies every contract, so does the other.  That is the whole
 * content of "no lock needed between the cores" - not that the hardware is
 * ordered, but that the protocol's laws are observables, and the observables do
 * not depend on the order. *)
Theorem independent_grants_are_interchangeable :
    forall b i (w : writer) j (r : reader),
      bus_wf (bus_r (bus_w b i w) j r) ->
      bus_wf (bus_w (bus_r b j r) i w).
Proof.
  intros b i w j r W.
  apply (obs_eq_preserves_bus_wf _ _ (bus_w_bus_r_commute b i w j r)).
  exact W.
Qed.

Theorem independent_publisher_grants_are_interchangeable :
    forall b i j (w1 w2 : writer), i <> j ->
      bus_wf (bus_w (bus_w b i w1) j w2) ->
      bus_wf (bus_w (bus_w b j w2) i w1).
Proof.
  intros b i j w1 w2 Hij W.
  apply (obs_eq_preserves_bus_wf _ _ (bus_w_bus_w_commute b i w1 j w2 Hij)).
  exact W.
Qed.

Theorem independent_reader_grants_are_interchangeable :
    forall b i j (r1 r2 : reader), i <> j ->
      bus_wf (bus_r (bus_r b i r1) j r2) ->
      bus_wf (bus_r (bus_r b j r2) i r1).
Proof.
  intros b i j r1 r2 Hij W.
  apply (obs_eq_preserves_bus_wf _ _ (bus_r_bus_r_commute b i r1 j r2 Hij)).
  exact W.
Qed.

Theorem independent_actor_grants_are_interchangeable :
    forall b i j (t1 t2 : task), i <> j ->
      bus_wf (bus_t (bus_t b i t1) j t2) ->
      bus_wf (bus_t (bus_t b j t2) i t1).
Proof.
  intros b i j t1 t2 Hij W.
  apply (obs_eq_preserves_bus_wf _ _ (bus_t_bus_t_commute b i t1 j t2 Hij)).
  exact W.
Qed.

(* the same reading for a grant and a refresh of an unrelated cell: a core that
 * completes its own CAS cannot spoil the other core's law either *)
Corollary foreign_refresh_preserves_the_class :
    forall b i (w : writer) j,
      bus_wf (bus_w b i w) ->
      bus_wf (bus_w (bus_w b i w) j (b_w (bus_w b i w) j)).
Proof.
  intros b i w j W.
  apply (obs_eq_preserves_bus_wf _ _
         (obs_eq_sym _ _ (bus_w_cas_invisible (bus_w b i w) j))).
  exact W.
Qed.

(* the flip side, which is the reason the star is not a broker: two writes to
 * the SAME cell are not interchangeable at all, and the one observable both of
 * them move is the pool watermark that snd_fits reads. *)
Theorem two_writes_to_one_cell_need_not_agree :
    ~ obs_eq (bus_w bus0 0 (mk_writer true ring_capacity nil 0))
             (bus_w bus0 0 empty_writer).
Proof.
  intros Hin. unfold obs_eq in Hin. destruct Hin as [_ [Hw _]].
  specialize (Hw 0). rewrite at_w_same_w, at_w_same_w in Hw.
  unfold empty_writer in Hw. discriminate Hw.
Qed.

(* ── 23. I13: the modal reading - laws with no chosen schedule ───────── *)

(* I12 compared two orders of two independent grants and found no observable
 * difference.  That leaves the cases where the order DOES matter - a reader and
 * a publisher contending for one cell - and the general question those cases
 * pose: what does it mean for a law to hold of a multicore run when the
 * schedule is not part of the model?
 *
 * The honest answer is modal.  The model has one states-to-states function,
 * apply_step, and one alphabet, step; it has no scheduler.  So the only thing a
 * law can be stated about is the set of successors of a state, and the only
 * honest quantifications over that set are the two: possibly, and necessarily.
 * This section defines those two, plus their reflexive-transitive closure
 * ("always"), and proves the modal logic that comes out of them: T, K, the
 * distribution laws, the constructive half of the box/diamond duality, the S4
 * fixed point, and the induction rule that turns a one-step preservation into a
 * run-wide guarantee.  It ends with the negative result that keeps the exercise
 * honest: the admissibility hypothesis on a step is load-bearing, so a port that
 * skips the two contract obligations really can reach an unlawful state, and no
 * modal machinery can argue it out of that. *)

Definition next (b b' : bus) : Prop := exists st, apply_step b st = b'.

(* the same, restricted to the steps a conforming port may issue: cap in range
 * for pub, NoDup for spawn.  Every modality below quantifies over THIS
 * relation, which is what makes the laws true; §23.5 shows the restriction is
 * not a cheat. *)
Definition next_adm (b b' : bus) : Prop := exists st, wf_step st /\ apply_step b st = b'.

Definition necessary (P : bus -> Prop) (b : bus) : Prop :=
  forall b', next_adm b b' -> P b'.

Definition possible (P : bus -> Prop) (b : bus) : Prop :=
  exists b', next_adm b b' /\ P b'.

Inductive reachable (from : bus) : bus -> Prop :=
  | R_here : reachable from from
  | R_next : forall b b', reachable from b -> next_adm b b' -> reachable from b'.

Definition always (P : bus -> Prop) (b : bus) : Prop :=
  forall b', reachable b b' -> P b'.

Lemma next_adm_imp_next : forall b b', next_adm b b' -> next b b'.
Proof. intros b b' [st [_ H]]. exists st. exact H. Qed.

Lemma reachable_one_step : forall b b', next_adm b b' -> reachable b b'.
Proof.
  intros b b' H. econstructor.
  - apply R_here.
  - exact H.
Qed.

Lemma reachable_trans : forall b1 b2 b3,
    reachable b1 b2 -> reachable b2 b3 -> reachable b1 b3.
Proof.
  intros b1 b2 b3 H1 H2. induction H2 as [|x y A IH].
  - exact H1.
  - apply (R_next _ _ _ IH). assumption.
Qed.

(* ── 23.1 the relation is reflexive, because a refusal is a stutter ──── *)

(* The alphabet contains refusals, and a refusal leaves the bus exactly as it
 * was found.  So every state has itself as a successor, which is why the box
 * modality here satisfies T rather than merely K - and why it satisfies it
 * through a WITNESS, not a convention about what "next" means. *)
Lemma NoDup_one : forall (A : Type) (x : A), NoDup [x].
Proof.
  intros A x. constructor.
  - intros Hin. simpl in Hin. destruct Hin.
  - constructor.
Qed.

(* the step refused at EVERY state: a grant asked of a core the machine does not
 * have.  It is admissible - a one-cursor grant has no duplicate - and it stutters
 * everywhere, so it is the reflexivity witness for the whole model. *)
Lemma refusal_stutters : forall b core l, b_cores b <= core ->
    apply_step b (Spawn_step core l) = b.
Proof.
  intros b core l Hc. cbn [apply_step].
  destruct (spawn_slot b) as [i|] eqn:S.
  - rewrite (spawn_refused_shape b core l i S).
    + reflexivity.
    + apply (spawn_not_ok_when_core_absent b core l). exact Hc.
  - rewrite (spawn_no_slot_shape b core l S). reflexivity.
Qed.

Lemma next_adm_refl : forall b, next_adm b b.
Proof.
  intros b. exists (Spawn_step (S (b_cores b)) [CPub 0]). split.
  - cbn [wf_step]. apply NoDup_one.
  - apply refusal_stutters. lia.
Qed.

Lemma next_refl : forall b, next b b.
Proof.
  intros b. exists (Spawn_step (S (b_cores b)) [CPub 0]).
  apply refusal_stutters. lia.
Qed.

(* axiom T, for both modalities *)
Theorem box_is_reflexive : forall P b, necessary P b -> P b.
Proof.
  intros P b H. apply (H b). apply next_adm_refl.
Qed.

Theorem always_is_reflexive : forall P b, always P b -> P b.
Proof.
  intros P b H. apply (H b). constructor.
Qed.

(* ── 23.2 the propositional skeleton: K, distribution, duality ───────── *)

Theorem axiom_K : forall P Q b,
    necessary (fun x => P x -> Q x) b -> necessary P b -> necessary Q b.
Proof.
  intros P Q b H1 H2 b' Hn. apply (H1 b' Hn). apply (H2 b' Hn).
Qed.

Theorem box_distributes_over_conjunction : forall P Q b,
    necessary (fun x => P x /\ Q x) b -> necessary P b /\ necessary Q b.
Proof.
  intros P Q b H. split.
  - intros b' Hn. destruct (H b' Hn). assumption.
  - intros b' Hn. destruct (H b' Hn). assumption.
Qed.

Theorem box_of_conjunction : forall P Q b,
    necessary P b /\ necessary Q b -> necessary (fun x => P x /\ Q x) b.
Proof.
  intros P Q b [H1 H2] b' Hn. split; [apply (H1 b' Hn) | apply (H2 b' Hn)].
Qed.

Theorem diamond_distributes_over_disjunction : forall P Q b,
    possible (fun x => P x \/ Q x) b -> possible P b \/ possible Q b.
Proof.
  intros P Q b [b' [Hn H]]. destruct H as [Hp|Hq].
  - left. exists b'. split; [exact Hn|exact Hp].
  - right. exists b'. split; [exact Hn|exact Hq].
Qed.

Theorem diamond_of_disjunction : forall P Q b,
    possible P b \/ possible Q b -> possible (fun x => P x \/ Q x) b.
Proof.
  intros P Q b [H|H].
  - destruct H as [b' [Hn Hp]]. exists b'. split; [exact Hn|left; exact Hp].
  - destruct H as [b' [Hn Hq]]. exists b'. split; [exact Hn|right; exact Hq].
Qed.

(* possibility is monotone, and so is necessity: the two modalities agree with
 * every implication the protocol proves *)
Theorem diamond_monotone : forall P Q b,
    (forall x, P x -> Q x) -> possible P b -> possible Q b.
Proof.
  intros P Q b HPQ [b' [Hn Hp]]. exists b'. split; [exact Hn|apply HPQ; exact Hp].
Qed.

Theorem box_monotone : forall P Q b,
    (forall x, P x -> Q x) -> necessary P b -> necessary Q b.
Proof.
  intros P Q b HPQ H b' Hn. apply HPQ. apply (H b' Hn).
Qed.

(* the constructive half of the box/diamond duality.  The converse - "not
 * possible P" implying "necessary (not P)" - is excluded middle over a set of
 * successors, and this file proves no such thing: it closes on no axioms, so the
 * half it can prove is the half it states. *)
Theorem box_diamond_duality : forall P b,
    possible P b -> ~ necessary (fun x => ~ P x) b.
Proof.
  intros P b [b' [Hn Hp]] H. unfold necessary in H. apply (H b' Hn). exact Hp.
Qed.

Theorem not_possible_is_necessary_not : forall P b,
    ~ possible P b -> forall b', next_adm b b' -> ~ P b'.
Proof.
  intros P b H b' Hn Hp. apply H. exists b'. split; assumption.
Qed.

(* ── 23.3 always: the closure, its axioms, and its induction rule ────── *)

Theorem always_implies_box : forall P b, always P b -> necessary P b.
Proof.
  intros P b H b' Hn. apply (H b'). apply (reachable_one_step b b' Hn).
Qed.

(* axiom 4, both ways: always is a fixed point of the box modality *)
Theorem always_box : forall P b, always P b -> always (always P) b.
Proof.
  intros P b H b' Hr b'' Hr'. apply (H b''). apply (reachable_trans b b' b'' Hr Hr').
Qed.

Theorem box_of_always : forall P b, always (always P) b -> always P b.
Proof.
  intros P b H b' Hr. apply (H b' Hr b'). constructor.
Qed.

(* the induction rule: the one-step preserver of §18.7 is exactly what turns
 * into a run-wide law.  This is the theorem the oracle's 262144 enumerated
 * interleavings sample without ever covering. *)
Theorem always_induction : forall P b,
    P b -> (forall x, P x -> necessary P x) -> always P b.
Proof.
  intros P b Hp Hstep b' Hr. induction Hr as [|x y A IH].
  - exact Hp.
  - apply (Hstep x IH). assumption.
Qed.

(* and the two notions of "reachable" agree: a word over the alphabet reaches
 * exactly the states the closure reaches *)
Theorem runs_are_reachable : forall steps b b',
    (forall st, In st steps -> wf_step st) -> run b steps = b' -> reachable b b'.
Proof.
  intros steps. induction steps as [|a t IH]; intros b b' Hall H.
  - rewrite <- H. constructor.
  - cbn [run] in H.
    assert (HA : next_adm b (apply_step b a))
      by (exists a; split; [apply Hall; left; reflexivity|reflexivity]).
    apply (reachable_trans b (apply_step b a) b').
    + apply (reachable_one_step b (apply_step b a) HA).
    + apply IH.
      * intros st Hin. apply Hall. right; exact Hin.
      * exact H.
Qed.

Corollary runs_are_reachable_from_bus0 : forall steps,
    (forall st, In st steps -> wf_step st) -> reachable bus0 (run bus0 steps).
Proof.
  intros steps Hall. apply (runs_are_reachable steps bus0 (run bus0 steps) Hall).
  reflexivity.
Qed.

(* ── 23.4 the class, read modally ──────────────────────────────────── *)

(* I9's closure, in the language of this section: the class of lawful buses is
 * necessary at each of its members, and therefore always true from each of them.
 * The step from §18.7 to here is only this - §18.7 speaks of words, and a word
 * is a schedule someone chose.  These two statements have no word in them. *)
Theorem bus_wf_is_necessary : forall b, bus_wf b -> necessary bus_wf b.
Proof.
  intros b W b' [st [Hadm Heq]]. rewrite <- Heq.
  apply (step_preserves_bus_wf b st W Hadm).
Qed.

Theorem bus_wf_is_always : forall b, bus_wf b -> always bus_wf b.
Proof.
  intros b W. apply always_induction.
  - exact W.
  - exact (fun x Wx => bus_wf_is_necessary x Wx).
Qed.

Theorem always_always_bus_wf : forall b, bus_wf b -> always (always bus_wf) b.
Proof.
  intros b W. apply always_box. apply (bus_wf_is_always b W).
Qed.

(* the three safety laws in their always form: what the oracle checks at every
 * interleaving it can enumerate, these check at every reachable state *)
Corollary always_within_the_pool : forall b,
    bus_wf b -> always (fun x => total_held x <= sector_pool) b.
Proof.
  intros b W b' Hr. apply (pool_never_overdrawn b'). apply (bus_wf_is_always b W b' Hr).
Qed.

Corollary always_one_owner_per_cursor : forall b,
    bus_wf b -> always (fun x => forall t1 t2 c,
        owns x t1 c = true -> owns x t2 c = true -> t1 = t2) b.
Proof.
  intros b W b' Hr. apply (bus_wf_ownership b').
  apply (bus_wf_is_always b W b' Hr).
Qed.

Corollary always_one_publisher_per_core : forall b, bus_wf b ->
    always (fun x => forall t1 t2, holds_pub x t1 = true -> holds_pub x t2 = true ->
        t_core (b_t x t1) = t_core (b_t x t2) -> t1 = t2) b.
Proof.
  intros b W b' Hr. apply (bus_wf_star b').
  apply (bus_wf_is_always b W b' Hr).
Qed.

Corollary always_no_ring_over_cap : forall b,
    bus_wf b -> always (fun x => forall wid,
        held x wid <= w_cap (b_w x wid)) b.
Proof.
  intros b W b' Hr. apply (bus_wf_bounded b').
  apply (bus_wf_is_always b W b' Hr).
Qed.

(* ── 23.5 the admissibility hypothesis is load bearing ──────────────── *)

(* Everything above is parameterised on next_adm, so the honest question is
 * whether that restriction did any work.  It did: a pub with cap = 0 is refused
 * by the contract (I1's own row says a ring must hold at least one sector), it
 * is admissible by no reading of wf_step, and it reaches a state that violates
 * bus_wf.  The obligation is therefore real, and the shipped guard that omits
 * it is a gap - which is the finding, not a hypothesis tidied away. *)
Lemma pub_slot_bus0_is_zero : pub_slot bus0 = Some 0.
Proof. unfold pub_slot, first_dead. reflexivity. Qed.

Lemma over_cap_step_shape :
    apply_step bus0 (Pub_step 0) = bus_w bus0 0 (writer_revive (b_w bus0 0) 0).
Proof.
  cbn [apply_step]. rewrite (pub_grant_shape bus0 0 0 pub_slot_bus0_is_zero).
  cbn [fst]. reflexivity.
Qed.

Theorem an_inadmissible_step_reaches_an_unlawful_state :
    ~ bus_wf (apply_step bus0 (Pub_step 0)).
Proof.
  rewrite over_cap_step_shape. intros H. destruct H as [Wt _].
  destruct (proj2 (proj2 (proj2 Wt)) 0) as [Pos _].
  rewrite at_w_same_w in Pos. unfold writer_revive in Pos. cbn [w_cap] in Pos. lia.
Qed.

(* so the box over ALL steps - the one a port with the shipped guards runs -
 * does NOT validate the class: bus_wf holds at bus0 and fails one step away. *)
Theorem box_over_all_steps_would_be_false :
    bus_wf bus0 /\ next bus0 (apply_step bus0 (Pub_step 0))
    /\ ~ bus_wf (apply_step bus0 (Pub_step 0)).
Proof.
  split.
  - exact bus_wf0.
  - split.
    + exists (Pub_step 0). reflexivity.
    + exact an_inadmissible_step_reaches_an_unlawful_state.
Qed.

(* ── 24. I14: the schedule is genuinely undetermined ───────────────── *)

(* §23 put a box and a diamond on top of a relation, and every law above it is
 * quantified over that relation.  The question this section has to answer is
 * whether the relation has any branching in it at all.  If apply_step were a
 * function - one successor per state - then "for every successor" would collapse
 * to "for the successor", the modal layer would be a relabelled sequential loop,
 * and I13 would be decoration.
 *
 * So this section produces WITNESSES rather than assertions: a state with two
 * enabled calls whose successors are observably different, a state with three
 * successors, a free operand that branches the first state of all, and - the
 * result that decides the question - two orderings of the same pair of calls that
 * leave the bus in states no observable can confuse.  The last one is also the
 * honest companion to §22.2: there, two grants with FIXED arguments commute;
 * here the protocol computes each call's arguments from the bus the call is
 * issued on, and then the order is visible in the state.  Both facts are in the
 * model, and the gap between them is what a lock-free protocol must be careful
 * about.
 *
 * The concrete states below are closed terms, so their derived values (admits,
 * watermarks, receipts) are established by computation the kernel re-checks, not
 * by a chain of rewrites.  Nothing here is an assumption: the last section of the
 * file prints the axioms, as always, and there are none. *)

(* ── 24.0 the tool the branching needs ─────────────────────────────── *)

(* equality is finer than observation, so a proved bus equality kills an
 * observational inequality - which is what lets the witnesses below be stated in
 * the weak, axiom-free equality of §22 ─ *)
Lemma obs_eq_of_eq : forall b1 b2, b1 = b2 -> obs_eq b1 b2.
Proof.
  intros b1 b2 H. rewrite H. apply obs_eq_refl.
Qed.

(* ── 24.1 one publisher, and the two calls enabled on it ───────────── *)

(* writer 0 revived on an empty machine: the same shape §23.5 reached with an
 * inadmissible pub, reached here the lawful way with cap = ring_capacity.  One
 * live writer, no readers, no tasks - the smallest state in which a real choice
 * exists. *)
Definition pub_bus : bus := bus_w bus0 0 (writer_revive (b_w bus0 0) ring_capacity).

Lemma pub_bus_is_a_step : apply_step bus0 (Pub_step ring_capacity) = pub_bus.
Proof.
  unfold apply_step, pub_bus.
  rewrite (pub_grant_shape bus0 ring_capacity 0 pub_slot_bus0_is_zero).
  cbn [fst]. reflexivity.
Qed.

Lemma pub_bus_is_a_successor_of_bus0 : next_adm bus0 pub_bus.
Proof.
  exists (Pub_step ring_capacity). split.
  - cbn [wf_step]. unfold ring_capacity. lia.
  - exact pub_bus_is_a_step.
Qed.

Lemma bus_wf_pub_bus : bus_wf pub_bus.
Proof.
  rewrite <- pub_bus_is_a_step.
  apply (step_preserves_bus_wf bus0 (Pub_step ring_capacity)).
  - exact bus_wf0.
  - cbn [wf_step]. unfold ring_capacity. lia.
Qed.

(* the tables this state does not touch *)
Lemma pub_bus_reader_dead : forall j, r_live (b_r pub_bus j) = false.
Proof. intros j. unfold pub_bus. rewrite at_r_bus_w. reflexivity. Qed.

Lemma pub_bus_task_dead : forall j, t_live (b_t pub_bus j) = false.
Proof. intros j. unfold pub_bus. rewrite at_t_bus_w. reflexivity. Qed.

Lemma pub_bus_no_owner : forall t c, owns pub_bus t c = false.
Proof.
  intros t c. unfold owns. rewrite pub_bus_task_dead. reflexivity.
Qed.

Lemma pub_bus_cursors_free : forall c, usable pub_bus NoTask c = true.
Proof.
  intros c. apply usable_free. intros t. apply pub_bus_no_owner.
Qed.

Lemma pub_bus_cores : b_cores pub_bus = cores_shipped.
Proof. unfold pub_bus. apply cores_inert_w. Qed.

(* and the values it does carry: the publisher is live and empty, its cap is the
 * whole ring, the pool is untouched, and both of its cursors are free *)
Lemma pub_bus_writer_live : w_live (b_w pub_bus 0) = true.
Proof. vm_compute. reflexivity. Qed.

Lemma pub_bus_enq_zero : w_enq (b_w pub_bus 0) = 0.
Proof. vm_compute. reflexivity. Qed.

Lemma pub_bus_held_zero : held pub_bus 0 = 0.
Proof. vm_compute. reflexivity. Qed.

Lemma pub_bus_admits_snd : snd_admit pub_bus NoTask 0 = true.
Proof. vm_compute. reflexivity. Qed.

Lemma pub_bus_admits_sub : sub_admit pub_bus NoTask 0 = true.
Proof. vm_compute. reflexivity. Qed.

Lemma pub_bus_fits : snd_fits pub_bus 0 = true.
Proof. vm_compute. reflexivity. Qed.

Lemma pub_bus_sub_slot : sub_slot pub_bus = Some 0.
Proof. vm_compute. reflexivity. Qed.

(* the two enabled calls, and the fact that neither is a refusal in disguise:
 * the send puts an item in the ring, the subscribe opens a cursor *)
Definition b_snd : bus := apply_step pub_bus (Snd_step 0 7).
Definition b_sub : bus := apply_step pub_bus (Sub_step 0).

Lemma b_snd_enabled : next_adm pub_bus b_snd.
Proof.
  exists (Snd_step 0 7). split.
  - cbn [wf_step]. exact I.
  - reflexivity.
Qed.

Lemma b_sub_enabled : next_adm pub_bus b_sub.
Proof.
  exists (Sub_step 0). split.
  - cbn [wf_step]. exact I.
  - reflexivity.
Qed.

Lemma b_snd_actually_pushes : w_hist (b_w b_snd 0) = [7].
Proof. vm_compute. reflexivity. Qed.

Lemma b_sub_actually_opens : r_live (b_r b_sub 0) = true.
Proof. vm_compute. reflexivity. Qed.

(* the refusal is a successor too: a grant asked of a core the machine does not
 * have leaves the state exactly as it was *)
Lemma pub_bus_refusal_stays :
    apply_step pub_bus (Spawn_step (S cores_shipped) nil) = pub_bus.
Proof.
  apply (refusal_stutters pub_bus (S cores_shipped) nil).
  rewrite pub_bus_cores. lia.
Qed.

Lemma pub_bus_refusal_enabled : next_adm pub_bus pub_bus.
Proof.
  exists (Spawn_step (S cores_shipped) nil). split.
  - cbn [wf_step]. constructor.
  - exact pub_bus_refusal_stays.
Qed.

(* ── 24.2 the enabled set is not a singleton ───────────────────────── *)

Lemma b_snd_b_sub_differ : ~ obs_eq b_snd b_sub.
Proof.
  intros E. destruct E as [_ [_ [Hr _]]]. specialize (Hr 0).
  vm_compute in Hr. discriminate.
Qed.

Lemma b_snd_b_pub_bus_differ : ~ obs_eq b_snd pub_bus.
Proof.
  intros E. destruct E as [_ [Hw _]]. specialize (Hw 0).
  vm_compute in Hw. discriminate.
Qed.

Lemma b_sub_b_pub_bus_differ : ~ obs_eq b_sub pub_bus.
Proof.
  intros E. destruct E as [_ [_ [Hr _]]]. specialize (Hr 0).
  vm_compute in Hr. discriminate.
Qed.

Theorem pub_bus_branches_three_ways :
    exists s1 s2 s3 : bus,
      next_adm pub_bus s1 /\ next_adm pub_bus s2 /\ next_adm pub_bus s3
      /\ ~ obs_eq s1 s2 /\ ~ obs_eq s1 s3 /\ ~ obs_eq s2 s3.
Proof.
  exists b_snd, b_sub, pub_bus.
  split; [exact b_snd_enabled|].
  split; [exact b_sub_enabled|].
  split; [exact pub_bus_refusal_enabled|].
  split; [exact b_snd_b_sub_differ|].
  split; [exact b_snd_b_pub_bus_differ|].
  exact b_sub_b_pub_bus_differ.
Qed.

(* the honest reading of the result above: the transition relation is not a
 * function, so "for every successor" is not "for the successor" ─ *)
Theorem the_alphabet_is_not_a_function :
    ~ (forall b b1 b2, next_adm b b1 -> next_adm b b2 -> b1 = b2).
Proof.
  intros H. apply b_snd_b_sub_differ.
  rewrite (H pub_bus b_snd b_sub b_snd_enabled b_sub_enabled).
  apply obs_eq_refl.
Qed.

(* and the branching is not one pair of lucky calls: the pub operand is a free
 * parameter of the alphabet, and every admissible value gives its own successor *)
Theorem the_cap_is_a_free_choice :
    forall c1 c2, 0 < c1 -> c1 <= ring_capacity -> 0 < c2 -> c2 <= ring_capacity ->
      c1 <> c2 ->
      exists s1 s2, next_adm bus0 s1 /\ next_adm bus0 s2 /\ ~ obs_eq s1 s2.
Proof.
  intros c1 c2 H1 L1 H2 L2 Hd.
  exists (bus_w bus0 0 (writer_revive (b_w bus0 0) c1)),
         (bus_w bus0 0 (writer_revive (b_w bus0 0) c2)).
  split.
  - exists (Pub_step c1). split.
    + cbn [wf_step]. split; assumption.
    + unfold apply_step. rewrite (pub_grant_shape bus0 c1 0 pub_slot_bus0_is_zero).
      cbn [fst]. reflexivity.
  - split.
    + exists (Pub_step c2). split.
      * cbn [wf_step]. split; assumption.
      * unfold apply_step. rewrite (pub_grant_shape bus0 c2 0 pub_slot_bus0_is_zero).
        cbn [fst]. reflexivity.
    + intros E. destruct E as [_ [Hw _]]. specialize (Hw 0).
      rewrite at_w_same_w, at_w_same_w in Hw. unfold writer_revive in Hw.
      apply Hd. apply (f_equal w_cap) in Hw. exact Hw.
Qed.

(* ── 24.3 the order of two protocol calls is visible ───────────────── *)

(* §22.2 commuted two grants whose ARGUMENTS were already fixed.  A real pair of
 * calls is different: sub builds the reader's start cursor out of the
 * publisher's watermark, so the same two calls in the other order build a
 * different reader.  Both orders stay in the class - this is not a law being
 * broken - and yet no observable can confuse the two states. *)
Definition sub_then_snd : bus := apply_step b_sub (Snd_step 0 7).
Definition snd_then_sub : bus := apply_step b_snd (Sub_step 0).

Lemma sub_then_snd_is_a_run : reachable pub_bus sub_then_snd.
Proof.
  unfold sub_then_snd.
  apply (reachable_trans pub_bus b_sub sub_then_snd).
  - apply (reachable_one_step pub_bus b_sub b_sub_enabled).
  - apply (reachable_one_step b_sub sub_then_snd).
    exists (Snd_step 0 7). split.
    + cbn [wf_step]. exact I.
    + reflexivity.
Qed.

Lemma snd_then_sub_is_a_run : reachable pub_bus snd_then_sub.
Proof.
  unfold snd_then_sub.
  apply (reachable_trans pub_bus b_snd snd_then_sub).
  - apply (reachable_one_step pub_bus b_snd b_snd_enabled).
  - apply (reachable_one_step b_snd snd_then_sub).
    exists (Sub_step 0). split.
    + cbn [wf_step]. exact I.
    + reflexivity.
Qed.

Theorem the_order_is_observable : ~ obs_eq sub_then_snd snd_then_sub.
Proof.
  intros E. destruct E as [_ [_ [Hr _]]]. specialize (Hr 0).
  vm_compute in Hr. discriminate.
Qed.

(* and the difference is not bookkeeping: it is the one thing the reader is
 * there for.  The subscriber that was already parked when the item arrived reads
 * it; the subscriber that arrived after it never will. *)
Theorem the_early_subscriber_reads_the_item :
    pr2 (rcv sub_then_snd NoTask 0) = R_DATA 7.
Proof. vm_compute. reflexivity. Qed.

Theorem the_late_subscriber_reads_empty :
    pr2 (rcv snd_then_sub NoTask 0) = R_EMPTY.
Proof. vm_compute. reflexivity. Qed.

(* both orders are lawful, so this is the protocol working as designed and not a
 * contract slipping: the schedule is undetermined, the class is not *)
Theorem both_orders_stay_in_the_class :
    bus_wf sub_then_snd /\ bus_wf snd_then_sub.
Proof.
  assert (WS : bus_wf b_snd /\ bus_wf b_sub).
  - split.
    + unfold b_snd. apply (step_preserves_bus_wf pub_bus (Snd_step 0 7)).
      * exact bus_wf_pub_bus.
      * cbn [wf_step]. exact I.
    + unfold b_sub. apply (step_preserves_bus_wf pub_bus (Sub_step 0)).
      * exact bus_wf_pub_bus.
      * cbn [wf_step]. exact I.
  - destruct WS as [Ws Wb]. split.
    + unfold sub_then_snd.
      apply (step_preserves_bus_wf b_sub (Snd_step 0 7)).
      * exact Wb.
      * cbn [wf_step]. exact I.
    + unfold snd_then_sub.
      apply (step_preserves_bus_wf b_snd (Sub_step 0)).
      * exact Ws.
      * cbn [wf_step]. exact I.
Qed.

(* the modal form of the section: at pub_bus it is NOT the case that every
 * successor satisfies "the item is readable", and not the case that none does -
 * the two modalities are genuinely both inhabited, which is the precise sense in
 * which the schedule has not been decided for us. *)
Theorem the_box_and_diamond_genuinely_differ_at_pub_bus :
    possible (fun b => pr2 (rcv b NoTask 0) = R_DATA 7) b_sub
    /\ ~ necessary (fun b => pr2 (rcv b NoTask 0) = R_DATA 7) b_sub.
Proof.
  split.
  - exists sub_then_snd. split.
    + unfold sub_then_snd. exists (Snd_step 0 7). split.
      * cbn [wf_step]. exact I.
      * reflexivity.
    + vm_compute. reflexivity.
  - intros H. specialize (H b_sub (next_adm_refl b_sub)).
    unfold b_sub in H. vm_compute in H. discriminate.
Qed.

(* ── 25. I15: the visibility gap ───────────────────────────────────── *)

(* §24 ended with a reader that subscribed after a send and therefore reads
 * nothing.  That is not an artefact of the example - it is the protocol's
 * definition of what is visible.  Two things are proved here, one per fabric.
 *
 * On the cursor fabric (the ring two cores share), visibility is a FLOOR.  A
 * subscriber joins at the publisher's watermark and its cursor only ever moves
 * forward, so the slots it joined past are unreachable for the whole life of the
 * run, whatever the publisher does next.  The floor is stated as an `always` of
 * §23, which is the honest way to say "forever" when no schedule is chosen: not
 * "after enough steps", but "at every reachable state".
 *
 * On the event queue, visibility is a PHASE.  The producer's call has two memory
 * effects - store the item in the slot, then publish the new tail - and only the
 * second enables a consumer.  The model already keeps the two halves apart, so
 * the gap can be proved rather than described: an item that is in the buffer and
 * below the published tail is invisible, and publishing is what makes exactly
 * that item readable.  Swapping the phases is a different program, and the model
 * shows the difference. *)

(* ── 25.1 the cursor is a floor, not a window ──────────────────────── *)

(* a read leaves its own reader alive: the three receipts are "unchanged",
 * "unchanged" and "advanced", and an advance keeps the live flag *)
Lemma rcv_keeps_a_reader_live : forall b cl rid,
    r_live (b_r b rid) = true -> r_live (b_r (fst (rcv b cl rid)) rid) = true.
Proof.
  intros b cl rid H.
  destruct (rcv_admit b cl rid) eqn:A.
  - destruct (rcv_ready b rid) eqn:R.
    + rewrite (rcv_data_bus b cl rid A R).
      rewrite at_r_same_r, reader_advance_live. reflexivity.
    + rewrite (rcv_empty_bus b cl rid A R). exact H.
  - rewrite (rcv_bad_cursor_bus b cl rid A). exact H.
Qed.

(* the two calls that cannot touch a reader at all, gathered here because the
 * step lemma below needs them for every member of the alphabet *)
Lemma snd_leaves_readers_alone : forall b cl wid d rid,
    b_r (fst (snd b cl wid d)) rid = b_r b rid.
Proof.
  intros b cl wid d rid.
  destruct (snd_admit b cl wid) eqn:A.
  - destruct (snd_fits b wid) eqn:F.
    + rewrite (snd_ok_bus b cl wid d A F). apply at_r_bus_w.
    + rewrite (snd_full_bus b cl wid d A F). apply at_r_bus_w.
  - rewrite (snd_bad_cursor_bus b cl wid d A). reflexivity.
Qed.

(* a subscribe that gets no reader slot leaves the whole table as it found it *)
Lemma sub_stutters_when_refused : forall b cl wid,
    sub_admit b cl wid = false -> fst (sub b cl wid) = b.
Proof. intros b cl wid A. unfold sub. rewrite A. reflexivity. Qed.

Lemma sub_stutters_when_no_slot : forall b cl wid,
    sub_admit b cl wid = true -> sub_slot b = None -> fst (sub b cl wid) = b.
Proof. intros b cl wid A S. unfold sub. rewrite A, S. reflexivity. Qed.

(* every call in the alphabet leaves a live reader live, and its cursor no
 * further back than it was *)
Lemma readers_never_recede : forall b st rid,
    r_live (b_r b rid) = true ->
      r_live (b_r (apply_step b st) rid) = true
      /\ r_cur (b_r b rid) <= r_cur (b_r (apply_step b st) rid).
Proof.
  intros b st rid H. destruct st as [cap|wid|wid d|readid|core l]; cbn [apply_step].
  - (* pub: the writer table only *)
    rewrite (pub_readers_untouched b cap rid). split; [exact H|apply Nat.le_refl].
  - (* sub: a fresh slot that was dead, or no grant at all *)
    destruct (sub_admit b NoTask wid) eqn:A.
    + destruct (sub_slot b) as [i|] eqn:S.
      * destruct (Nat.eqb_spec i rid) as [Hi|Hi].
        -- exfalso.
           assert (D : r_live (b_r b i) = false)
             by (apply (sub_slot_offers_a_dead_slot b i S)).
           rewrite <- Hi in H. rewrite D in H. discriminate H.
        -- rewrite (sub_others_untouched b NoTask wid i rid S A Hi).
           split; [exact H|apply Nat.le_refl].
      * rewrite (sub_stutters_when_no_slot b NoTask wid A S).
        split; [exact H|apply Nat.le_refl].
    + rewrite (sub_stutters_when_refused b NoTask wid A).
      split; [exact H|apply Nat.le_refl].
  - (* snd: the writer table only *)
    rewrite (snd_leaves_readers_alone b NoTask wid d rid).
    split; [exact H|apply Nat.le_refl].
  - (* rcv: the one call that moves a cursor - its own reader's, and forward.
       * another reader's row is untouched, exactly as it is by the other four. *)
    destruct (Nat.eqb_spec readid rid) as [Hr|Hr].
    + subst readid.
      split.
      * apply (rcv_keeps_a_reader_live b NoTask rid H).
      * apply (rcv_cursor_never_rewinds b NoTask rid).
    + assert (EQ : b_r (fst (rcv b NoTask readid)) rid = b_r b rid).
      { destruct (rcv_admit b NoTask readid) eqn:A.
        - destruct (rcv_ready b readid) eqn:R.
          + rewrite (rcv_data_bus b NoTask readid A R). apply at_r_other_r. exact Hr.
          + rewrite (rcv_empty_bus b NoTask readid A R). reflexivity.
        - rewrite (rcv_bad_cursor_bus b NoTask readid A). reflexivity. }
      rewrite EQ. split; [exact H|apply Nat.le_refl].
  - (* spawn: the task table only *)
    rewrite (spawn_readers_untouched b core l rid).
    split; [exact H|apply Nat.le_refl].
Qed.

(* the floor, at every reachable state - not "eventually", because the model
 * chooses no schedule that would make "eventually" mean anything *)
Theorem the_watermark_is_a_floor : forall b,
    always (fun x => forall rid, r_live (b_r b rid) = true ->
        r_live (b_r x rid) = true /\ r_cur (b_r b rid) <= r_cur (b_r x rid)) b.
Proof.
  intros b. apply always_induction.
  - intros rid H. split; [exact H|apply Nat.le_refl].
  - intros x Hx x' [st [_ Heq]]. rewrite <- Heq.
    intros rid Hlive. destruct (Hx rid Hlive) as [HL Le].
    destruct (readers_never_recede x st rid HL) as [HL' Le'].
    split; [exact HL'|]. lia.
Qed.

(* instantiated on the §24 state in which the subscribe came after the send:
 * item 7 sits at ring index 0, the reader's cursor starts at 1, and no further
 * sequence of calls - of any kind, in any order - brings the cursor back *)
Lemma item_seven_is_in_the_fabric : hist_at snd_then_sub 0 0 = 7.
Proof. unfold hist_at. vm_compute. reflexivity. Qed.

Lemma the_late_cursor_starts_at_one : r_cur (b_r snd_then_sub 0) = 1.
Proof. vm_compute. reflexivity. Qed.

Theorem the_missed_slot_is_never_read : forall x,
    reachable snd_then_sub x -> 0 < r_cur (b_r x 0).
Proof.
  intros x Hr.
  assert (LIVE : r_live (b_r snd_then_sub 0) = true).
  { unfold snd_then_sub. vm_compute. reflexivity. }
  destruct (the_watermark_is_a_floor snd_then_sub x Hr 0 LIVE) as [_ Le].
  rewrite the_late_cursor_starts_at_one in Le. lia.
Qed.

(* and a read that does deliver always delivers AT the cursor, so a delivery can
 * never come from below the floor *)
Theorem delivery_is_always_at_or_past_the_floor : forall x cl d,
    reachable snd_then_sub x -> pr2 (rcv x cl 0) = R_DATA d ->
    exists k, 0 < k /\ d = hist_at x (r_wid (b_r x 0)) k /\ r_cur (b_r x 0) = k.
Proof.
  intros x cl d Hr Hd.
  assert (FLOOR : 0 < r_cur (b_r x 0)) by (apply (the_missed_slot_is_never_read x Hr)).
  assert (A : rcv_admit x cl 0 = true).
  { destruct (rcv_admit x cl 0) eqn:Q.
    - reflexivity.
    - rewrite (rcv_bad_cursor_receipt x cl 0 Q) in Hd. discriminate. }
  assert (R : rcv_ready x 0 = true).
  { destruct (rcv_ready x 0) eqn:Q.
    - reflexivity.
    - rewrite (rcv_empty_receipt x cl 0 A Q) in Hd. discriminate. }
  rewrite (rcv_data_receipt x cl 0 A R) in Hd.
  injection Hd.
  exists (r_cur (b_r x 0)). split; [exact FLOOR|].
  split; [symmetry; assumption|reflexivity].
Qed.

(* ── 25.2 the queue: a store, and separately a publish ────────────── *)

(* the two memory effects of one producer call, taken apart *)
Definition evt_store (e : evtq) (v : nat) : evtq :=
  evt_write e (evt_idx (q_tail e)) (Some v).

Definition evt_publish (e : evtq) : evtq :=
  mk_evtq (q_buf e) (q_head e) (S (q_tail e)).

(* the shipped put IS store-then-publish, so this is the same program read in the
 * order the hardware performs it *)
Lemma evt_advance_is_store_then_publish : forall e v,
    evt_advance e v = evt_publish (evt_store e v).
Proof.
  intros e v. unfold evt_advance, evt_publish, evt_store, evt_write. reflexivity.
Qed.

(* each phase touches exactly one half of the queue, and that is the gap *)
Lemma evt_store_head_at_itself : forall e v, q_head (evt_store e v) = q_head e.
Proof. reflexivity. Qed.

Lemma evt_store_tail_at_itself : forall e v, q_tail (evt_store e v) = q_tail e.
Proof. reflexivity. Qed.

Lemma evt_store_puts_the_item_in_the_fabric : forall e v,
    q_buf (evt_store e v) (evt_idx (q_tail e)) = Some v.
Proof. intros e v. unfold evt_store. apply evt_write_buf_at_itself. Qed.

(* the other half: a publish announces and nothing more - every slot keeps its
 * contents, so what a consumer reads after a publish IS what the store left *)
Theorem publishing_leaves_the_buffer_alone : forall e v k,
    q_buf (evt_publish (evt_store e v)) k = q_buf (evt_store e v) k.
Proof. reflexivity. Qed.

(* the gap, stated once on each side of the same term: the item IS in memory, and
 * a consumer reading that very queue at that very moment gets nothing *)
Theorem an_unpublished_store_is_invisible : forall e v,
    q_head e = q_tail e -> pr2 (evt_get (evt_store e v)) = None.
Proof.
  intros e v H.
  assert (HS : q_head (evt_store e v) = q_tail (evt_store e v)).
  { rewrite evt_store_head_at_itself, evt_store_tail_at_itself. exact H. }
  rewrite (evt_get_on_empty_is_none (evt_store e v) HS). reflexivity.
Qed.

Theorem the_store_is_really_there : forall e v,
    q_head e = q_tail e ->
    q_buf (evt_store e v) (evt_idx (q_head (evt_store e v))) = Some v.
Proof.
  intros e v H. rewrite evt_store_head_at_itself, H.
  apply evt_store_puts_the_item_in_the_fabric.
Qed.

(* and the publish, which moves no data at all, is what makes that item readable *)
Theorem publishing_delivers_what_was_stored : forall e v,
    q_head e = q_tail e ->
    pr2 (evt_get (evt_publish (evt_store e v))) = Some v.
Proof.
  intros e v H.
  assert (HS : q_head (evt_store e v) = q_tail (evt_store e v)).
  { rewrite evt_store_head_at_itself, evt_store_tail_at_itself. exact H. }
  assert (NE : q_head (evt_publish (evt_store e v))
               <> q_tail (evt_publish (evt_store e v))).
  { unfold evt_publish. cbn [q_head q_tail]. rewrite HS. lia. }
  rewrite (evt_get_reads_the_head_slot (evt_publish (evt_store e v)) NE).
  change (q_buf (evt_store e v) (evt_idx (q_head (evt_store e v))) = Some v).
  rewrite evt_store_head_at_itself, H.
  apply evt_store_puts_the_item_in_the_fabric.
Qed.

(* the phases are not interchangeable: publishing first and storing afterwards
 * hands the consumer the stale slot, which is the row a driver with the two
 * writes swapped would show *)
Theorem the_phases_cannot_be_swapped :
    pr2 (evt_get (evt_store (evt_publish evtq0) 7)) = None.
Proof. vm_compute. reflexivity. Qed.

(* ── 25.3 the two fabrics agree on what "visible" means ──────────── *)

(* On both, the writer's data effect and the reader's enabling effect are
 * separate, and a reader is enabled by the second only: the ring by the cursor it
 * joined at, the queue by the published tail.  That is why no lock sits between
 * the two cores - a reader never observes a half-finished store, because it never
 * observes a store that has not been announced. *)
Theorem a_store_never_publishes_itself : forall e v,
    occupancy (evt_store e v) = occupancy e.
Proof.
  intros e v. unfold occupancy.
  rewrite evt_store_head_at_itself, evt_store_tail_at_itself. reflexivity.
Qed.

Theorem publishing_adds_one_to_the_occupancy : forall e,
    evt_wf e -> occupancy (evt_publish e) = S (occupancy e).
Proof.
  intros e W. unfold occupancy, evt_publish. cbn [q_head q_tail].
  pose proof (evt_wf_head_never_passes_tail e W). lia.
Qed.

Theorem a_store_leaves_the_occupancy_alone : forall e v,
    evt_wf e -> occupancy (evt_store e v) = occupancy e /\ evt_wf (evt_store e v).
Proof.
  intros e v W. split.
  - apply a_store_never_publishes_itself.
  - destruct W as [Hle Hold]. unfold evt_wf. split.
    + rewrite evt_store_head_at_itself, evt_store_tail_at_itself. exact Hle.
    + rewrite a_store_never_publishes_itself. exact Hold.
Qed.

(* the queue's own version of the floor: a consumer's head never passes the
 * published tail, so an item above the tail is as unreachable as a slot the
 * subscriber joined past - and for the same reason, the watermark it was given
 * when it joined *)
Theorem a_get_never_passes_the_published_head : forall e,
    evt_wf e -> q_head (fst (evt_get e)) <= q_tail (fst (evt_get e)).
Proof.
  intros e W. destruct (evt_get_preserves_wf e W) as [Hle _]. exact Hle.
Qed.

(* ── 26. I16: bounded catch-up - k reads close exactly k of the gap ─── *)

(* I9's oracle drains a subscriber one read at a time and reports that a soak of
 * N reads leaves nothing behind.  The general law behind that sample is a
 * budget: a subscriber that holds its cursor can close the gap to the publisher's
 * watermark at exactly one sector per read, no more and no less, so the number of
 * reads needed is the gap itself - bounded, computable, and independent of what
 * the publisher does next (it does nothing during a drain).
 *
 * Three things have to be true for that to hold, and all three are proved here
 * rather than assumed: the read stays ADMISSIBLE (reading my own cursor cannot
 * cost me the cursor), the publisher's watermark is FROZEN during the drain (a
 * read writes only the reader table), and each admitted read decrements the gap
 * by one - including the reads that find nothing to read, where the gap is
 * already zero and zero minus one is still zero. *)

(* ── 26.1 the drain, and what it cannot disturb ──────────────────── *)

Fixpoint read_k (b : bus) (rid : nat) (k : nat) : bus :=
  match k with
  | 0 => b
  | S j => read_k (fst (rcv b NoTask rid)) rid j
  end.

Lemma read_k_zero : forall b rid, read_k b rid 0 = b.
Proof. reflexivity. Qed.

Lemma read_k_step : forall b rid j,
    read_k b rid (S j) = read_k (fst (rcv b NoTask rid)) rid j.
Proof. reflexivity. Qed.

(* a read writes one row of one table: the publisher of the reader it drains is
 * the same writer cell at every state of the drain *)
Lemma rcv_leaves_the_publisher_alone : forall b rid wid,
    b_w (fst (rcv b NoTask rid)) wid = b_w b wid.
Proof.
  intros b rid wid.
  destruct (rcv_admit b NoTask rid) eqn:A.
  - destruct (rcv_ready b rid) eqn:R.
    + rewrite (rcv_data_bus b NoTask rid A R). apply at_w_bus_r.
    + rewrite (rcv_empty_bus b NoTask rid A R). reflexivity.
  - rewrite (rcv_bad_cursor_bus b NoTask rid A). reflexivity.
Qed.

Lemma read_k_leaves_the_publisher_alone : forall k b rid wid,
    b_w (read_k b rid k) wid = b_w b wid.
Proof.
  induction k as [|j IH]; intros b rid wid; cbn [read_k].
  - reflexivity.
  - rewrite (IH (fst (rcv b NoTask rid)) rid wid).
    apply (rcv_leaves_the_publisher_alone b rid wid).
Qed.

(* and the reader it drains keeps its publisher: an advance changes the cursor,
 * a refusal changes nothing *)
Lemma rcv_keeps_the_wid : forall b rid,
    r_wid (b_r (fst (rcv b NoTask rid)) rid) = r_wid (b_r b rid).
Proof.
  intros b rid.
  destruct (rcv_admit b NoTask rid) eqn:A.
  - destruct (rcv_ready b rid) eqn:R.
    + rewrite (rcv_data_bus b NoTask rid A R).
      rewrite at_r_same_r, reader_advance_wid. reflexivity.
    + rewrite (rcv_empty_bus b NoTask rid A R). reflexivity.
  - rewrite (rcv_bad_cursor_bus b NoTask rid A). reflexivity.
Qed.

Lemma read_k_keeps_the_wid : forall k b rid,
    r_wid (b_r (read_k b rid k) rid) = r_wid (b_r b rid).
Proof.
  induction k as [|j IH]; intros b rid; cbn [read_k].
  - reflexivity.
  - rewrite (IH (fst (rcv b NoTask rid)) rid).
    apply (rcv_keeps_the_wid b rid).
Qed.

Lemma read_k_keeps_the_reader_live : forall k b rid,
    r_live (b_r b rid) = true -> r_live (b_r (read_k b rid k) rid) = true.
Proof.
  induction k as [|j IH]; intros b rid H; cbn [read_k].
  - exact H.
  - apply (IH (fst (rcv b NoTask rid)) rid).
    apply (rcv_keeps_a_reader_live b NoTask rid H).
Qed.

(* ── 26.2 the drain stays admissible ────────────────────────────── *)

(* This is the lemma the budget needs before anything can be counted.  A read
 * writes the reader table, so the publisher's liveness (at_w_bus_r) and the
 * usability of the cursor (usable_r_inert, which is stated for every caller and
 * every cursor because ownership lives in the task table) both survive it, and
 * the only reader field that changes is the cursor itself. *)
Lemma rcv_admit_stable : forall b rid,
    rcv_admit b NoTask rid = true ->
    rcv_admit (fst (rcv b NoTask rid)) NoTask rid = true.
Proof.
  intros b rid A.
  assert (W : w_live (b_w b (r_wid (b_r b rid))) = true)
    by (apply (rcv_admit_live_publisher b NoTask rid A)).
  assert (U : usable b NoTask (CSub rid) = true)
    by (apply (rcv_admit_usable b NoTask rid A)).
  destruct (rcv_ready b rid) eqn:R.
  - rewrite (rcv_data_bus b NoTask rid A R). unfold rcv_admit.
    rewrite at_r_same_r, reader_advance_live, reader_advance_wid,
            at_w_bus_r, usable_r_inert.
    rewrite W, U. reflexivity.
  - rewrite (rcv_empty_bus b NoTask rid A R). exact A.
Qed.

Theorem read_k_never_loses_the_cursor : forall k b rid,
    rcv_admit b NoTask rid = true ->
    rcv_admit (read_k b rid k) NoTask rid = true.
Proof.
  induction k as [|j IH]; intros b rid A; cbn [read_k].
  - exact A.
  - apply (IH (fst (rcv b NoTask rid)) rid (rcv_admit_stable b rid A)).
Qed.

(* ── 26.3 one read, one sector out of the gap ───────────────────── *)

Lemma nat_sub_one_step : forall P j, (P - 1) - j = P - S j.
Proof. intros P j. destruct P as [|p]; lia. Qed.

(* the one-step budget.  The hypothesis is not decoration: a refused read leaves
 * the bus exactly as it was found, and a reader with a positive gap that is
 * refused keeps that gap, so the "-1" is false without it. *)
Lemma rcv_pending_step : forall b rid,
    rcv_admit b NoTask rid = true ->
    pending (fst (rcv b NoTask rid)) (b_r (fst (rcv b NoTask rid)) rid)
      = pending b (b_r b rid) - 1.
Proof.
  intros b rid A.
  destruct (rcv_ready b rid) eqn:R.
  - rewrite (rcv_data_bus b NoTask rid A R). unfold pending.
    rewrite at_r_same_r, reader_advance_cur, reader_advance_wid, at_w_bus_r.
    assert (Lt : r_cur (b_r b rid) < w_enq (b_w b (r_wid (b_r b rid)))).
    { apply (proj1 (Nat.ltb_lt (r_cur (b_r b rid))
                                (w_enq (b_w b (r_wid (b_r b rid)))))).
      unfold rcv_ready in R. exact R. }
    lia.
  - rewrite (rcv_empty_bus b NoTask rid A R). unfold pending.
    assert (Ge : w_enq (b_w b (r_wid (b_r b rid))) <= r_cur (b_r b rid)).
    { apply (proj1 (Nat.ltb_ge (r_cur (b_r b rid))
                                (w_enq (b_w b (r_wid (b_r b rid)))))).
      unfold rcv_ready in R. exact R. }
    lia.
Qed.

(* the whole budget: k admitted reads close k of the gap, and no more than the
 * gap - the truncated subtraction carries the second half by itself *)
Lemma read_k_pending : forall k b rid,
    rcv_admit b NoTask rid = true ->
    pending (read_k b rid k) (b_r (read_k b rid k) rid)
      = pending b (b_r b rid) - k.
Proof.
  induction k as [|j IH]; intros b rid A; cbn [read_k].
  - rewrite Nat.sub_0_r. reflexivity.
  - rewrite (IH (fst (rcv b NoTask rid)) rid (rcv_admit_stable b rid A)).
    rewrite (rcv_pending_step b rid A). apply nat_sub_one_step.
Qed.

(* the row the oracle samples, stated for every gap and every drain length *)
Theorem i16_bounded_catch_up : forall b rid k,
    rcv_admit b NoTask rid = true ->
    pending (read_k b rid k) (b_r (read_k b rid k) rid)
      = Nat.max 0 (pending b (b_r b rid) - k).
Proof.
  intros b rid k A. rewrite (read_k_pending k b rid A).
  symmetry. apply (Nat.max_0_l (pending b (b_r b rid) - k)).
Qed.

(* ── 26.4 the consequences the budget was bought for ────────────── *)

(* the gap is a bound in the strict sense: pending reads suffice, and no prefix of
 * the drain ever widens it *)
Theorem read_k_drains_within_the_pending_count : forall b rid,
    rcv_admit b NoTask rid = true ->
    pending (read_k b rid (pending b (b_r b rid)))
            (b_r (read_k b rid (pending b (b_r b rid))) rid) = 0.
Proof.
  intros b rid A.
  rewrite (read_k_pending (pending b (b_r b rid)) b rid A).
  unfold pending. lia.
Qed.

Theorem read_k_never_widens_the_gap : forall k b rid,
    rcv_admit b NoTask rid = true ->
    pending (read_k b rid k) (b_r (read_k b rid k) rid) <= pending b (b_r b rid).
Proof.
  intros k b rid A. rewrite (read_k_pending k b rid A). lia.
Qed.

(* and the drain never runs past the tail: it stops exactly where the publisher
 * stood, which is the same floor §25 proved it may not come back from *)
Theorem read_k_stops_at_the_tail : forall b rid,
    rcv_admit b NoTask rid = true ->
    rcv_ready (read_k b rid (pending b (b_r b rid))) rid = false.
Proof.
  intros b rid A. unfold rcv_ready. apply Nat.ltb_ge.
  rewrite read_k_leaves_the_publisher_alone, read_k_keeps_the_wid.
  assert (E : pending (read_k b rid (pending b (b_r b rid)))
                      (b_r (read_k b rid (pending b (b_r b rid))) rid) = 0)
    by (apply (read_k_drains_within_the_pending_count b rid A)).
  unfold pending in E.
  rewrite read_k_leaves_the_publisher_alone, read_k_keeps_the_wid in E.
  (* the drain length in the goal is still written pending b (b_r b rid); E has it
   * unfolded, so unfold the goal into the same shape and count *)
  unfold pending. lia.
Qed.

(* so a drained subscriber reads empty - the soak row of the oracle, as a law *)
Theorem the_drained_reader_reads_empty : forall b rid,
    rcv_admit b NoTask rid = true ->
    pr2 (rcv (read_k b rid (pending b (b_r b rid))) NoTask rid) = R_EMPTY.
Proof.
  intros b rid A.
  assert (A' : rcv_admit (read_k b rid (pending b (b_r b rid))) NoTask rid = true)
    by (apply (read_k_never_loses_the_cursor (pending b (b_r b rid)) b rid A)).
  assert (R' : rcv_ready (read_k b rid (pending b (b_r b rid))) rid = false)
    by (apply (read_k_stops_at_the_tail b rid A)).
  apply (rcv_empty_receipt (read_k b rid (pending b (b_r b rid))) NoTask rid A' R').
Qed.

(* ── 26.5 the same budget read modally ──────────────────────────── *)

(* §23's modalities ask what holds of the successors rather than of a chosen run,
 * and they are the honest home for the no-starvation half of I16.  Two
 * cautions, both proved rather than waved: the box modality is NOT claimed for
 * the gap, because a send from the publisher widens every subscriber's gap by
 * design and the box quantifies over that step too; and the diamond needs the
 * readiness test, because a subscriber that is already level with the tail has no
 * closer successor to reach.  What survives is the pair below - every one-step
 * successor along a drain is a non-increase, and while the gap is open a strict
 * decrease is available. *)

Theorem one_read_never_widens_the_gap : forall b rid,
    pending (fst (rcv b NoTask rid)) (b_r (fst (rcv b NoTask rid)) rid)
      <= pending b (b_r b rid).
Proof.
  intros b rid.
  destruct (rcv_admit b NoTask rid) eqn:A.
  - rewrite (rcv_pending_step b rid A). lia.
  - rewrite (rcv_bad_cursor_bus b NoTask rid A). apply Nat.le_refl.
Qed.

Theorem catching_up_is_possible : forall b rid,
    rcv_admit b NoTask rid = true -> rcv_ready b rid = true ->
    possible (fun x => pending x (b_r x rid) < pending b (b_r b rid)) b.
Proof.
  intros b rid A R. exists (fst (rcv b NoTask rid)). split.
  - exists (Rcv_step rid). split.
    + cbn [wf_step]. exact I.
    + reflexivity.
  - rewrite (rcv_pending_step b rid A). unfold pending.
    assert (Lt : r_cur (b_r b rid) < w_enq (b_w b (r_wid (b_r b rid)))).
    { apply (proj1 (Nat.ltb_lt (r_cur (b_r b rid))
                                (w_enq (b_w b (r_wid (b_r b rid)))))).
      unfold rcv_ready in R. exact R. }
    lia.
Qed.

(* the send is the step the box cannot be asked of, and saying so is part of the
 * row: the gap is a monovariant of the DRAIN, not of the protocol.  A push from
 * the publisher lengthens every gap of its own subscribers by exactly one, which
 * is why no box over the whole alphabet can carry the law above. *)
Theorem a_send_widens_the_gap_of_its_own_subscribers : forall b rid wid d,
    bus_wf b -> r_live (b_r b rid) = true ->
    snd_admit b NoTask wid = true -> snd_fits b wid = true ->
    r_wid (b_r b rid) = wid ->
    pending (fst (snd b NoTask wid d)) (b_r (fst (snd b NoTask wid d)) rid)
      = S (pending b (b_r b rid)).
Proof.
  intros b rid wid d W L As Fs Hr.
  assert (Hc : r_cur (b_r b rid) <= w_enq (b_w b wid)).
  { destruct (bus_wf_reader_bounds b W rid L) as [_ [_ [_ C]]].
    rewrite Hr in C. exact C. }
  rewrite (snd_ok_bus b NoTask wid d As Fs). unfold pending.
  rewrite at_r_bus_w, Hr, at_w_same_w, writer_push_enq. lia.
Qed.

End MediaInterCore.

(* ── 27. The gate: the file rests on nothing ────────────────────── *)

(* One line per headline result.  Each must print "Closed under the global
 * context"; anything else means the file has picked up an axiom, an unsafe
 * (co)fixpoint or a positivity assumption.  verify_models.sh re-checks the same
 * .vo with coqchk -o, so this block is the author-facing half of that gate.
 * The list runs in chapter order: the schedule-level safety groups, then the
 * modal layer (I13), the interleaving semantics (I14), visibility (I15) and
 * catch-up (I16). *)

Print Assumptions interleavings_preserve_bus_wf.
Print Assumptions the_shipped_core_count_survives_every_run.
Print Assumptions the_target_topology_passes_every_row.
Print Assumptions evt_handoff_delivers_the_item.
Print Assumptions soak_reads_one_item_per_step.
Print Assumptions soak_of_six_hundred_is_fifo.
Print Assumptions independent_grants_are_interchangeable.
Print Assumptions bus_wf_is_always.
Print Assumptions always_within_the_pool.
Print Assumptions the_alphabet_is_not_a_function.
Print Assumptions the_order_is_observable.
Print Assumptions the_watermark_is_a_floor.
Print Assumptions publishing_delivers_what_was_stored.
Print Assumptions i16_bounded_catch_up.
Print Assumptions the_drained_reader_reads_empty.
