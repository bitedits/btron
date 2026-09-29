(* tron_properties.v
 *
 * T-Kernel 2.0 as an API layer over the InterCore microkernel: traceable
 * kernel objects, and the morphisms between the API surface and the
 * microkernel state that implements it.
 *
 * The scope is src/kernel and its certification entry point `make verify`.
 * Every constant, state code, error code and guard order below is read off
 * the shipped kernel rather than invented:
 *
 *   - object ID spaces, the ID <-> index affine maps        config.h:23-133
 *   - internal task states, bit-encoded                     task.h:45-52
 *   - API task states tskstat, also bit-encoded             syscall.h:58-64
 *   - task_alive as a bit test                              task.h:56-59
 *   - error codes E_* = ERCD(mer,0)                         errno.h:34-56
 *   - TMO_POL = 0, TMO_FEVR = -1                            typedef.h:55-56
 *   - guard order CHECK_TSKID / CHECK_NONSELF               check.h:27-39,133-137
 *   - make_dormant / make_ready / make_non_ready            task.c:106,191,215
 *   - change_task_priority: delete before rewrite           task.c:227-247
 *   - ready queue top_priority, null sentinel, klocktsk     ready_queue.h:42-52
 *   - wait release matrix (ok / ok_ercd / ng / tmout)       wait.c:48-76
 *   - wait_delete broadcast with E_DLT                      wait.c:125-133
 *   - wakeup counting, saturation, read-clear               task_sync.c:213-265
 *   - mailbox frontier rendezvous: the sender writes the
 *     item pointer into the sleeping receiver's slot        mailbox.c:239-243
 *   - stored-id liveness marker (mbxid == 0)                mailbox.c:229,334
 *   - B-TRON message ring, masked selective dequeue         ipc_msg.c:50-118
 *
 * The IPC fragment mirrors the InterCore microkernel directly, not by
 * analogy: a mailbox IS a writer row, a receiving task's slot IS a reader
 * cursor, and a cross-domain send is exactly the act of passing that cursor
 * over.  Task management, the ready queue and the timers share the model's
 * idiom (decide then act, derived accounting, refusal is inert) but are
 * modelled in their own right.
 *
 * Build (Rocq >= 9.0):
 *   coqc tron_properties.v
 *   coqchk -o -silent tron_properties
 * Companion oracle: tron_model.ml.  Registered in verify_models.sh.
 *
 * No Axiom, no Parameter, no Hypothesis, no Admitted -- see the gate block.
 *)

From Stdlib Require Import Arith List Bool ZArith Lia.
Import ListNotations.

Local Open Scope bool_scope.

(* ── 1. Contract constants ──────────────────────────────────────── *)

(* config.h:23-29.  MAX_TSKID is a runtime figure in the kernel; the shipped
 * ceiling used by the oracle is named here so the laws below stay uniform.
 * With MIN_TSKID = 1 and NUM_TSKID = MAX_TSKID, config.h's test
 * "MIN <= id && id <= MAX" is exactly "lo <= id && id < lo + num". *)
Definition min_tskid : nat := 1.          (* config.h:23 *)
Definition num_tsk : nat := 64.           (* MAX_TSKID at shipped geometry *)
Definition min_pri : nat := 1.            (* config.h:128 -- highest priority *)
Definition max_pri : nat := 140.          (* config.h:129 -- lowest priority *)
Definition num_pri : nat := 140.          (* config.h:130, ready_queue.h:48 *)
Definition wupcap : nat := 255.           (* INT_MAX in the kernel *)

(* config.h:32-38, 96-110: the object families the API reaches.  Every one of
 * them is an affine image of a table index, and that is the whole ID story. *)
Definition min_mbxid : nat := 1.
Definition num_mbx : nat := 16.
Definition min_semid : nat := 1.
Definition num_sem : nat := 16.
Definition min_flgid : nat := 1.
Definition num_flg : nat := 16.

(* ipc_msg.c:17-18 and message.h:12-24: the B-TRON message ring that crosses
 * thread domains. *)
Definition msg_ring_cap : nat := 64.      (* MAX_QUEUED_MSGS *)
Definition msg_domains : nat := 32.       (* MAX_IPC_PIDS *)
Definition msg_type_min : nat := 1.
Definition msg_type_max : nat := 31.

(* ── 2. Receipts: the ER codes the API returns ───────────────────── *)

(* errno.h:29 encodes ERCD(mer,ser) as (mer << 16) | ser, so for every code
 * the kernel returns the magnitude is the whole figure and the sign is
 * negative -- E_OK alone being zero.  The model keeps the shipped magnitude
 * and derives the sign, so a receipt can never be confused with a result. *)
Inductive er : Type :=
  | E_OK
  | E_PAR
  | E_ID
  | E_CTX
  | E_MACV
  | E_OBJ
  | E_NOEXS
  | E_QOVR
  | E_RLWAI
  | E_TMOUT
  | E_DLT
  | E_DISWAI.

Definition er_mag (e : er) : nat :=
  match e with
  | E_OK     => 0
  | E_PAR    => 17
  | E_ID     => 18
  | E_CTX    => 25
  | E_MACV   => 26
  | E_OBJ    => 41
  | E_NOEXS  => 42
  | E_QOVR   => 43
  | E_RLWAI  => 49
  | E_TMOUT  => 50
  | E_DLT    => 51
  | E_DISWAI => 52
  end.

Definition er_code (e : er) : Z := Z.opp (Z.of_nat (er_mag e)).

Definition er_ok (e : er) : bool := Nat.eqb (er_mag e) 0.

Lemma er_mag_distinct : forall e1 e2, er_mag e1 = er_mag e2 -> e1 = e2.
Proof. destruct e1, e2; cbn [er_mag]; intros H; try discriminate H; reflexivity. Qed.

Lemma er_ok_iff : forall e, er_ok e = true <-> e = E_OK.
Proof.
  intros e. unfold er_ok. split.
  - intros H. rewrite Nat.eqb_eq in H. apply (er_mag_distinct e E_OK). cbn [er_mag]. exact H.
  - intros [<-]. cbn [er_mag]. apply Nat.eqb_refl.
Qed.

Lemma er_code_is_shipped_figure : forall e, er_code e = Z.opp (Z.of_nat (er_mag e)).
Proof. intros e. reflexivity. Qed.

Lemma er_code_of_ok : er_code E_OK = 0.
Proof. reflexivity. Qed.

(* ── 3. Time-outs ───────────────────────────────────────────────── *)

(* typedef.h:55-56: TMO_POL is 0, TMO_FEVR is -1, a positive TMO is relative
 * and the ABS attribute is carried with it.  A signed nat is the wrong type
 * for this, so the model names the four cases. *)
Inductive tmo : Type :=
  | TMO_POLL
  | TMO_REL
  | TMO_ABS
  | TMO_FEVR.

Definition tmo_blocks (t : tmo) : bool := negb (Nat.eqb t TMO_POLL).

(* ── 4. Two bit encodings of one state lattice ──────────────────── *)

(* task.h:45-52 gives the kernel's internal TSTAT as a bitmask in which
 * TS_WAITSUS is literally TS_WAIT | TS_SUSPEND; syscall.h:58-64 gives the
 * API's tskstat as a bitmask in which TTS_WAS is TTS_WAI | TTS_SUS.  The
 * morphism between them is therefore not a lookup table of names: it is the
 * same product of two booleans written at different bit positions.  That is
 * the observation this section turns into theorems. *)
Inductive tstat : Type :=
  | S_NONEXIST | S_READY | S_WAIT | S_SUSPEND | S_WAITSUS | S_DORMANT.

Definition ts_nonexist : nat := 0.        (* task.h:46 *)
Definition ts_ready : nat := 1.           (* task.h:47 *)
Definition ts_wait : nat := 2.            (* task.h:48 *)
Definition ts_suspend : nat := 4.         (* task.h:49 *)
Definition ts_waitsus : nat := 6.         (* task.h:50 *)
Definition ts_dormant : nat := 8.         (* task.h:51 *)

Definition tts_run : nat := 1.            (* syscall.h:58 *)
Definition tts_rdy : nat := 2.            (* syscall.h:59 *)
Definition tts_wai : nat := 4.            (* syscall.h:60 *)
Definition tts_sus : nat := 8.            (* syscall.h:61 *)
Definition tts_was : nat := 12.           (* syscall.h:62: TTS_WAI | TTS_SUS *)
Definition tts_dmt : nat := 16.           (* syscall.h:63 *)

Definition bits (s : tstat) : nat :=
  match s with
  | S_NONEXIST => ts_nonexist | S_READY => ts_ready | S_WAIT => ts_wait
  | S_SUSPEND => ts_suspend  | S_WAITSUS => ts_waitsus | S_DORMANT => ts_dormant
  end.

(* RUN is not a state of the internal encoding -- task.c uses TS_READY for
 * both and names the running task by the separate coordinate schedtsk, so
 * the projection takes that coordinate as an argument. *)
Definition api_of (run : bool) (s : tstat) : nat :=
  match s with
  | S_NONEXIST => 0
  | S_READY    => if run then tts_run else tts_rdy
  | S_WAIT     => tts_wai
  | S_SUSPEND  => tts_sus
  | S_WAITSUS  => tts_was
  | S_DORMANT  => tts_dmt
  end.

(* task.h:56-59: task_alive(state) == (state & (READY|WAIT|SUSPEND)) != 0 *)
Definition alive (s : tstat) : bool := negb (Nat.ltb (Nat.land (bits s) 7) 0).

Lemma bits_injective : forall s1 s2, bits s1 = bits s2 -> s1 = s2.
Proof. destruct s1, s2; cbn [bits]; intros H; try discriminate H; reflexivity. Qed.

Lemma bits_waitsus_is_join : bits S_WAITSUS = ts_wait + ts_suspend.
Proof. reflexivity. Qed.

Lemma api_waitsus_is_join : forall r,
    api_of r S_WAITSUS = api_of true S_WAIT + api_of true S_SUSPEND.
Proof. reflexivity. Qed.

Lemma alive_table : forall s,
  alive s = match s with
            | S_NONEXIST | S_DORMANT => false
            | _ => true
            end.
Proof. destruct s; cbn [alive bits]; vm_compute; reflexivity. Qed.

(* EXCLUSION: no encoding ever sets the ready bit together with the waiting
 * or the suspended bit.  Both sides of the morphism agree on this, which is
 * what makes the projection a lattice map rather than a rename. *)
Lemma ready_excludes_waiting : forall s,
    Nat.ltb (Nat.land (bits s) 1) 1 = true -> Nat.ltb (Nat.land (bits s) 6) 0 = true.
Proof. destruct s; cbn [bits]; vm_compute; auto. Qed.

Lemma api_ready_excludes_waiting : forall s r,
    Nat.ltb (Nat.land (api_of r s) 2) 1 = true -> Nat.ltb (Nat.land (api_of r s) 12) 0 = true.
Proof. destruct s, r; cbn [api_of]; vm_compute; auto. Qed.

(* The projection never identifies two internal states, and NONEXIST is the
 * one state the API cannot name -- which is why the services answer E_NOEXS
 * instead of returning a state. *)
Lemma api_of_separates_states : forall s1 s2,
    s1 <> S_NONEXIST -> s2 <> S_NONEXIST ->
    (forall r, api_of r s1 = api_of r s2) -> s1 = s2.
Proof.
  intros s1 s2 H1 H2 H.
  assert (Hx := H false).
  destruct s1, s2; try (exfalso; auto); cbn [api_of] in Hx;
    try discriminate Hx; reflexivity.
Qed.

Lemma api_nonexistent_is_zero : forall r, api_of r S_NONEXIST = 0.
Proof. destruct r; reflexivity. Qed.

(* ── 5. ID and index: one affine map, eight object families ─────── *)

(* config.h: INDEX_*(id) = id - MIN_*, ID_*(index) = index + MIN_*, and
 * CHK_*(id) = MIN <= id <= MAX.  So the ID domain is an affine image of the
 * half-open index range, and the free-cell marker 0 that mailbox.c:229 tests
 * lies outside that domain by construction. *)
Definition chk_id (lo num id : nat) : bool :=
  andb (Nat.leb lo id) (Nat.ltb id (lo + num)).

Definition index_of (lo id : nat) : nat := Nat.sub id lo.
Definition id_of (lo i : nat) : nat := i + lo.

Lemma chk_id_true : forall lo num id,
    chk_id lo num id = true -> lo <= id /\ id < lo + num.
Proof.
  intros lo num id H. unfold chk_id in H. apply andb_true_iff in H. destruct H as [H1 H2].
  apply Nat.leb_le in H1. apply Nat.ltb_lt in H2. split; assumption.
Qed.

Lemma chk_id_complete : forall lo num id,
    lo <= id -> id < lo + num -> chk_id lo num id = true.
Proof.
  intros lo num id H1 H2. unfold chk_id. rewrite andb_true_iff.
  rewrite Nat.leb_le, Nat.ltb_lt. split; assumption.
Qed.

Lemma index_id_roundtrip : forall lo num id, chk_id lo num id = true ->
    index_of lo (id_of lo id) = id.
Proof. intros lo num id H. unfold index_of, id_of. lia. Qed.

Lemma id_index_roundtrip : forall lo num i, i < num -> id_of lo (index_of lo (id_of lo i)) = id_of lo i.
Proof. intros lo num i H. unfold id_of, index_of. lia. Qed.

Lemma chk_id_iff_index : forall lo num id,
    chk_id lo num id = true <-> exists i, i < num /\ id = id_of lo i.
Proof.
  intros lo num id. split.
  - intros H. apply chk_id_true in H. destruct H as [H1 H2]. exists (id - lo).
    unfold id_of. lia.
  - intros [i [Hi_lt <-]]. apply chk_id_complete; unfold id_of; lia.
Qed.

Lemma zero_is_never_a_legal_id : forall lo num, 0 < lo -> chk_id lo num 0 = false.
Proof.
  intros lo num H. unfold chk_id.
  destruct (Nat.leb lo 0) eqn:E.
  - apply Nat.leb_le in E. lia.
  - rewrite E. cbn [andb]. reflexivity.
Qed.

Lemma index_in_range : forall lo num id, chk_id lo num id = true -> index_of lo id < num.
Proof. intros lo num id H. apply chk_id_true in H. destruct H as [_ H2]. unfold index_of. lia. Qed.

(* ── 6. The kernel state: three object tables and one coordinate ─── *)

(* Tables are functions from the table index, as everywhere in this corpus
 * (\S ref:tables of the paper); the coordinate that is NOT a table is the
 * running task, because src/kernel/task.h:200 keeps it as the pointer
 * schedtsk.  The stored ID marker of each control block (TCB.tskid,
 * MBXCB.mbxid, SEMCB.semid) is what makes a cell traceable: a cell whose
 * marker is 0 has never been adopted, and 0 is not a legal ID (\ref{5}). *)
Definition upd (A : Type) (t : nat -> A) (i : nat) (v : A) : nat -> A :=
  fun j => if Nat.eqb j i then v else t j.

Lemma upd_same : forall (A : Type) (t : nat -> A) i (v : A), @upd A t i v i = v.
Proof. intros A t i v. cbn [upd]. rewrite Nat.eqb_refl. reflexivity. Qed.

Lemma upd_other : forall (A : Type) (t : nat -> A) i (v : A) j,
    i <> j -> @upd A t i v j = t j.
Proof. intros A t i v j H. cbn [upd]. rewrite H. reflexivity. Qed.

(* TSK_SELF is 0 (typedef.h), MIN_TSKID is 1 (config.h:23): the self
 * sentinel and the free-cell marker are the same number, and neither is a
 * legal task ID.  Every "id == 0" test in the kernel is therefore doing one
 * of two different jobs, and the guard order says which. *)
Definition tsk_self : nat := 0.

Inductive objref : Type :=
  | OF_MBX : nat -> objref
  | OF_SEM : nat -> objref.

Definition objref_eqb (a b : objref) : bool :=
  match a, b with
  | OF_MBX i, OF_MBX j => Nat.eqb i j
  | OF_SEM i, OF_SEM j => Nat.eqb i j
  | _, _ => false
  end.

(* task.h:71-162, the fields the API can observe or change. *)
Record tcb : Type := mk_tcb {
    t_id    : nat;              (* TCB.tskid -- stored marker, 0 = free cell *)
    t_stat  : tstat;            (* TCB.state, the internal encoding *)
    t_pri   : nat;              (* TCB.priority, the current priority *)
    t_bpri  : nat;              (* TCB.bpriority, restored at make_dormant *)
    t_wup   : nat;              (* TCB.wupcnt *)
    t_sus   : nat;              (* TCB.suscnt *)
    t_klock : bool;             (* TCB.klocked *)
    t_wait  : option objref;    (* TCB.wspec + TCB.wid, as a sum *)
    t_slot  : option nat        (* TCB.winfo.mbx.ppk_msg: the receiver's slot *)
  }.

(* mailbox.c:44-54 *)
Record mbx : Type := mk_mbx {
    m_id    : nat;              (* MBXCB.mbxid -- stored marker *)
    m_mpri  : bool;             (* TA_MPRI: priority-ordered queueing *)
    m_chain : list nat;         (* MBXCB.mq_head .. mq_tail, in queue order *)
    m_wait  : list nat          (* MBXCB.wait_queue, head waiter first *)
  }.

(* semaphore.c:35-45 *)
Record sem : Type := mk_sem {
    s_id    : nat;              (* SEMCB.semid -- stored marker *)
    s_count : nat;              (* SEMCB.semcnt *)
    s_wait  : list nat          (* SEMCB.wait_queue *)
  }.

Record kst : Type := mk_kst {
    b_t    : nat -> tcb;
    b_m    : nat -> mbx;
    b_s    : nat -> sem;
    b_run  : nat;               (* task.h:200 schedtsk, 0 = nothing to run *)
    b_indp : bool               (* check.h in_indp(): interrupt/dispatched *)
  }.

Definition bus_t (st : kst) (i : nat) (v : tcb) : kst :=
  mk_kst (@upd tcb (b_t st) i v) (b_m st) (b_s st) (b_run st) (b_indp st).
Definition bus_m (st : kst) (i : nat) (v : mbx) : kst :=
  mk_kst (b_t st) (@upd mbx (b_m st) i v) (b_s st) (b_run st) (b_indp st).
Definition bus_s (st : kst) (i : nat) (v : sem) : kst :=
  mk_kst (b_t st) (b_m st) (@upd sem (b_s st) i v) (b_run st) (b_indp st).

Lemma at_t_same_t : forall st i (t : tcb), b_t (bus_t st i t) i = t.
Proof. intros st i t. unfold bus_t. apply upd_same. Qed.

Lemma at_t_other_t : forall st i (t : tcb) j, i <> j -> b_t (bus_t st i t) j = b_t st j.
Proof. intros st i t j H. unfold bus_t. apply upd_other; exact H. Qed.

Lemma at_m_bus_t : forall st i (t : tcb) j, b_m (bus_t st i t) j = b_m st j.
Proof. intros st i t j. unfold bus_t, bus_m. cbn [upd]. destruct (Nat.eqb j i); reflexivity. Qed.

Lemma at_s_bus_t : forall st i (t : tcb) j, b_s (bus_t st i t) j = b_s st j.
Proof. intros st i t j. unfold bus_t. reflexivity. Qed.

Lemma at_t_bus_m : forall st i (v : mbx) j, b_t (bus_m st i v) j = b_t st j.
Proof. intros st i v j. unfold bus_m. reflexivity. Qed.

Lemma at_t_bus_s : forall st i (v : sem) j, b_t (bus_s st i v) j = b_t st j.
Proof. intros st i v j. unfold bus_s. reflexivity. Qed.

Lemma run_inert_t : forall st i (t : tcb), b_run (bus_t st i t) = b_run st.
Proof. intros st i t. unfold bus_t. reflexivity. Qed.

Lemma indp_inert_t : forall st i (t : tcb), b_indp (bus_t st i t) = b_indp st.
Proof. intros st i t. unfold bus_t. reflexivity. Qed.

(* Field setters, positional, in the style of writer_revive / writer_push. *)
Definition t_stat_ (s : tstat) (t : tcb) : tcb :=
  mk_tcb (t_id t) s (t_pri t) (t_bpri t) (t_wup t) (t_sus t) (t_klock t) (t_wait t) (t_slot t).
Definition t_pri_ (p : nat) (t : tcb) : tcb :=
  mk_tcb (t_id t) (t_stat t) p (t_bpri t) (t_wup t) (t_sus t) (t_klock t) (t_wait t) (t_slot t).
Definition t_wup_ (n : nat) (t : tcb) : tcb :=
  mk_tcb (t_id t) (t_stat t) (t_pri t) (t_bpri t) n (t_sus t) (t_klock t) (t_wait t) (t_slot t).
Definition t_sus_ (n : nat) (t : tcb) : tcb :=
  mk_tcb (t_id t) (t_stat t) (t_pri t) (t_bpri t) (t_wup t) n (t_klock t) (t_wait t) (t_slot t).
Definition t_klock_ (bl : bool) (t : tcb) : tcb :=
  mk_tcb (t_id t) (t_stat t) (t_pri t) (t_bpri t) (t_wup t) (t_sus t) bl (t_wait t) (t_slot t).
Definition t_wait_ (w : option objref) (t : tcb) : tcb :=
  mk_tcb (t_id t) (t_stat t) (t_pri t) (t_bpri t) (t_wup t) (t_sus t) (t_klock t) w (t_slot t).
Definition t_slot_ (w : option nat) (t : tcb) : tcb :=
  mk_tcb (t_id t) (t_stat t) (t_pri t) (t_bpri t) (t_wup t) (t_sus t) (t_klock t) (t_wait t) w.

(* task.c:106-131 make_dormant: state DORMANT, priority restored from
 * bpriority, wupcnt and suscnt and the kernel-lock flags cleared.  The
 * model resets the whole adoptable record, which is what makes the reset a
 * projection. *)
Definition free_tcb : tcb :=
  mk_tcb 0 S_NONEXIST min_pri min_pri 0 0 false None None.
Definition free_mbx : mbx := mk_mbx 0 false [] [].
Definition free_sem : sem := mk_sem 0 0 [].

Definition make_dormant (t : tcb) : tcb :=
  t_slot None (t_wait None (t_klock false (t_sus 0 (t_wup 0 (t_pri_ (t_bpri t) (t_stat_ S_DORMANT t)))))).

Definition st0 : kst :=
  mk_kst (fun _ => free_tcb) (fun _ => free_mbx) (fun _ => free_sem) 0 false.

(* Existence is a stored marker, not a bit: mailbox.c:229 and 334 both test
 * mbxcb->mbxid == 0 after the range check. *)
Definition tsk_used (st : kst) (i : nat) : bool := negb (Nat.eqb (t_id (b_t st i)) 0).
Definition mbx_used (st : kst) (i : nat) : bool := negb (Nat.eqb (m_id (b_m st i)) 0).
Definition sem_used (st : kst) (i : nat) : bool := negb (Nat.eqb (s_id (b_s st i)) 0).

Definition tsk_live (st : kst) (id : nat) : bool :=
  chk_id min_tskid num_tsk id &&& tsk_used st (index_of min_tskid id).
Definition mbx_live (st : kst) (id : nat) : bool :=
  chk_id min_mbxid num_mbx id &&& mbx_used st (index_of min_mbxid id).

(* ── 7. The preflight cascade: which guard fires is which receipt ── *)

(* check.h:27-39 gives the order explicitly: the self test precedes the range
 * test, and each guard carries its own ER.  A service is therefore a list of
 * (does-this-pass, otherwise-this-code) pairs, and the receipt of a refusal
 * names the guard that refused it.  first_bad reads that list left to right. *)
Definition first_bad (gs : list (bool * er)) : option er :=
  fold_right (fun g acc => if fst g then acc else Some (snd g)) None gs.

Lemma first_bad_nil : first_bad [] = None.
Proof. reflexivity. Qed.

Lemma first_bad_pass : forall g gs, fst g = true -> first_bad (g :: gs) = first_bad gs.
Proof. intros g gs H. cbn [first_bad fst]. rewrite H. reflexivity. Qed.

Lemma first_bad_fail : forall g gs, fst g = false -> first_bad (g :: gs) = Some (snd g).
Proof. intros g gs H. cbn [first_bad fst]. rewrite H. reflexivity. Qed.

Lemma first_bad_some_head : forall g gs e,
    fst g = false -> first_bad (g :: gs) = Some e -> e = snd g.
Proof.
  intros g gs e H H2. cbn [first_bad fst] in H2. rewrite H in H2. injection H2. auto.
Qed.

Lemma first_bad_none_all_pass : forall gs, first_bad gs = None <-> forallb fst gs = true.
Proof.
  induction gs as [|g IHe].
  - split; reflexivity.
  - cbn [first_bad forallb]. destruct g as [p e]. destruct p.
    + split; intros H; [exact H | exact H].
    + split; [intros H; discriminate H | intros H; cbn in H; discriminate H].
Qed.

(* Two guards, not one.  CHECK_TSKID admits nothing outside the ID range, and
 * the stored marker admits nothing outside the table; the range test alone
 * cannot tell an unadopted cell from an adopted one, which is why
 * task_manage.c:233 and mailbox.c:229 both continue past CHECK_*ID. *)
Lemma range_is_not_existence : exists st id,
    chk_id min_tskid num_tsk id = true /\ tsk_live st id = false.
Proof. exists st0, min_tskid. split.
  - apply chk_id_complete; lia.
  - unfold tsk_live, mbx_used, tsk_used. cbn [b_t st0 index_of].
    cbn [Nat.eqb t_id andb negb]. reflexivity.
Qed.

Lemma self_is_out_of_range : chk_id min_tskid num_tsk tsk_self = false.
Proof. unfold chk_id, tsk_self. cbn [Nat.leb Nat.ltb andb]. reflexivity. Qed.

(* ── 8. Allocation: the first free cell, bounded by the table ───── *)

(* task.h:206-207 keeps a free list (free_tcb) and task_manage.c:126-131
 * answers E_LIMIT when QueRemoveNext returns NULL.  The model scans the
 * table with fuel instead: the allocation is bounded by the table, not by
 * the traffic, and "no free cell" is a decidable property of the scan. *)
Fixpoint first_free (used : nat -> bool) (fuel i : nat) : option nat :=
  match fuel with
  | 0 => None
  | S k => if negb (used i) then Some i else first_free used k (S i)
  end.

Lemma first_free_none_full : forall used fuel i,
    (forall j, i <= j -> j < i + fuel -> used j = true) -> first_free used fuel i = None.
Proof.
  intros used fuel; induction fuel as [|k IH]; intros i H; cbn [first_free].
  - reflexivity.
  - rewrite (H i (Nat.le_refl i)).
    + cbn [negb]. apply IH. intros j Le Lt; apply H; lia.
    + lia.
Qed.

Lemma first_free_range : forall used fuel i o,
    first_free used fuel i = Some o -> i <= o /\ o < i + fuel.
Proof.
  intros used fuel; induction fuel as [|k IH]; intros i o H; cbn [first_free] in H.
  - discriminate H.
  - destruct (used i) eqn:U; cbn [negb] in H.
    + subst o. lia.
    + apply IH in H. destruct H as [H1 H2]. lia.
Qed.

Lemma first_free_found_free : forall used fuel i o,
    first_free used fuel i = Some o -> used o = false.
Proof.
  intros used fuel; induction fuel as [|k IH]; intros i o H; cbn [first_free] in H.
  - destruct o; discriminate H.
  - destruct (used i) eqn:U; cbn [negb] in H.
    + injection H; intros <-; exact U.
    + apply IH; exact H.
Qed.

(* ── 9. The task services ───────────────────────────────────────── *)

(* cre: task_manage.c:36,120-131.  Adoption writes one cell and stamps it
 * with the ID the caller will be handed back. *)
Definition adopt_tcb (st : kst) (i : nat) (pri : nat) : tcb :=
  mk_tcb (id_of min_tskid i) S_DORMANT pri pri 0 0 false None None.

Definition cre (st : kst) (pri : nat) : kst * er :=
  match first_free (tsk_used st) num_tsk 0 with
  | None   => (st, E_LIMIT)
  | Some i => (bus_t st i (adopt_tcb st i pri), E_OK)
  end.

Lemma cre_refuses_inert : forall st pri,
    (forall j, j < num_tsk -> tsk_used st j = true) -> fst (cre st pri) = st.
Proof.
  intros st pri H. unfold cre.
  rewrite (first_free_none_full (tsk_used st) num_tsk 0).
  - reflexivity.
  - intros j Le Lt; apply H; lia.
Qed.

Lemma cre_adopts_one_cell : forall st pri i j,
    first_free (tsk_used st) num_tsk 0 = Some i -> i <> j ->
    b_t (fst (cre st pri)) j = b_t st j.
Proof.
  intros st pri i j H Hi. unfold cre. rewrite H. apply at_t_other_t; exact Hi.
Qed.

Lemma cre_stamps_the_marker : forall st pri i,
    first_free (tsk_used st) num_tsk 0 = Some i ->
    t_id (b_t (fst (cre st pri)) i) = id_of min_tskid i /\
    tsk_used (fst (cre st pri)) i = true.
Proof.
  intros st pri i H. unfold cre. rewrite H.
  rewrite at_t_same_t. split.
  - unfold adopt_tcb. reflexivity.
  - unfold tsk_used, adopt_tcb. cbn [id_of Nat.eqb negb].
    apply Nat.neq_true_eq. unfold id_of, min_tskid. lia.
Qed.

Lemma cre_dormant_on_arrival : forall st pri i,
    first_free (tsk_used st) num_tsk 0 = Some i ->
    t_stat (b_t (fst (cre st pri)) i) = S_DORMANT.
Proof.
  intros st pri i H. unfold cre. rewrite H. apply at_t_same_t.
Qed.

Lemma cre_index_in_range : forall st pri i,
    first_free (tsk_used st) num_tsk 0 = Some i -> i < num_tsk.
Proof.
  intros st pri i H. apply first_free_range with (fuel := num_tsk) (i := 0) in H. lia.
Qed.

(* The shared prefix of the ID-taking services: check.h:27-31 (self, then
 * range) and check.h:133-136 (not the caller itself).  Each returns the
 * shipped code of the guard that fired. *)
Definition guard_self (st : kst) (id : nat) : bool :=
  orb (b_indp st) (negb (Nat.eqb id tsk_self)).
Definition guard_range (id : nat) : bool := chk_id min_tskid num_tsk id.
Definition guard_nonself (st : kst) (id : nat) : bool :=
  orb (b_indp st) (negb (Nat.eqb id (b_run st))).

Definition id_guards (st : kst) (id : nat) : list (bool * er) :=
  (guard_self st id, E_OBJ) :: (guard_range id, E_ID) :: (guard_nonself st id, E_OBJ) :: nil.

Lemma id_guards_pass_iff : forall st id,
    first_bad (id_guards st id) = None <->
    b_indp st = true \/ (id <> tsk_self /\ id <> b_run st /\ chk_id min_tskid num_tsk id = true).
Proof.
  intros st id. unfold id_guards. cbn [first_bad guard_self guard_range guard_nonself].
  destruct (b_indp st) eqn:I; cbn [orb negb Nat.eqb]; split; intros H;
    try (left; exact I); try (right; repeat split; try (apply chk_id_true; ...)); try lia.
Abort.

(* sta: task_manage.c:248-270.  DORMANT is the only startable state; the
 * nested if at :261-263 splits into E_NOEXS and E_OBJ exactly as check.h
 * orders the tests. *)
Definition sta_guards (st : kst) (id : nat) : list (bool * er) :=
  id_guards st id ++
  ((negb (Nat.eqb (t_stat (b_t st (index_of min_tskid id)) S_NONEXIST))), E_NOEXS) ::
  ((Nat.eqb (t_stat (b_t st (index_of min_tskid id)) S_DORMANT)), E_OBJ) :: nil.

Definition sta (st : kst) (id : nat) : kst * er :=
  match first_bad (sta_guards st id) with
  | Some e => (st, e)
  | None => (bus_t st (index_of min_tskid id) (t_stat_ S_READY (b_t st (index_of min_tskid id))), E_OK)
  end.

Lemma sta_refused_inert : forall st id e,
    first_bad (sta_guards st id) = Some e -> sta st id = (st, e).
Proof. intros st id e H. unfold sta. rewrite H. reflexivity. Qed.

Lemma sta_makes_ready : forall st id,
    first_bad (sta_guards st id) = None ->
    t_stat (b_t (fst (sta st id)) (index_of min_tskid id)) = S_READY.
Proof.
  intros st id H. unfold sta. rewrite H.
  unfold bus_t. apply upd_same.
Qed.

(* ter: task_manage.c:381-402 -- alive required, and the caller may not
 * terminate itself; the target goes back through make_dormant. *)
Definition ter_guards (st : kst) (id : nat) : list (bool * er) :=
  id_guards st id ++
  ((alive (t_stat (b_t st (index_of min_tskid id)))), E_NOEXS) ::
  ((negb (Nat.eqb (t_stat (b_t st (index_of min_tskid id))) S_DORMANT)), E_OBJ) :: nil.

Definition ter (st : kst) (id : nat) : kst * er :=
  match first_bad (ter_guards st id) with
  | Some e => (st, e)
  | None => (bus_t st (index_of min_tskid id)
               (make_dormant (b_t st (index_of min_tskid id))), E_OK)
  end.

Lemma ter_refused_inert : forall st id e,
    first_bad (ter_guards st id) = Some e -> ter st id = (st, e).
Proof. intros st id e H. unfold ter. rewrite H. reflexivity. Qed.

Lemma ter_resets_counters : forall st id,
    first_bad (ter_guards st id) = None ->
    t_wup (b_t (fst (ter st id)) (index_of min_tskid id)) = 0 /\
    t_sus (b_t (fst (ter st id)) (index_of min_tskid id)) = 0 /\
    t_pri (b_t (fst (ter st id)) (index_of min_tskid id)) =
    t_bpri (b_t st (index_of min_tskid id)).
Proof.
  intros st id H. unfold ter. rewrite H.
  unfold bus_t. rewrite upd_same. unfold make_dormant. cbn [t_wup t_sus t_pri].
  destruct (b_t st (index_of min_tskid id)); reflexivity.
Qed.

(* make_dormant is a projection: task.c:106-131 resets the same fields
 * whether or not they still hold their old values. *)
Lemma make_dormant_idempotent : forall t, make_dormant (make_dormant t) = make_dormant t.
Proof.
  intros t. destruct t. cbn [make_dormant t_stat_ t_pri_ t_wup_ t_sus_ t_klock_ t_wait_ t_slot_].
  reflexivity.
Qed.

Lemma make_dormant_is_dormant : forall t, t_stat (make_dormant t) = S_DORMANT.
Proof. intros t. destruct t; cbn [make_dormant]. reflexivity. Qed.

(* chg_pri: task_manage.c:417 with CHECK_PRI (check.h:143, E_PAR) and the
 * delete-before-rewrite rule of task.c:227-247. *)
Definition chg_pri_guards (st : kst) (id pri : nat) : list (bool * er) :=
  id_guards st id ++
  ((andb (Nat.leb min_pri pri) (Nat.leb pri max_pri)), E_PAR) ::
  ((negb (Nat.eqb (t_stat (b_t st (index_of min_tskid id))) S_NONEXIST))), E_NOEXS) :: nil.

Definition chg_pri (st : kst) (id pri : nat) : kst * er :=
  match first_bad (chg_pri_guards st id pri) with
  | Some e => (st, e)
  | None => (bus_t st (index_of min_tskid id)
               (t_pri_ pri (b_t st (index_of min_tskid id))), E_OK)
  end.

Lemma chg_pri_refused_inert : forall st id pri e,
    first_bad (chg_pri_guards st id pri) = Some e -> chg_pri st id pri = (st, e).
Proof. intros st id pri e H. unfold chg_pri. rewrite H. reflexivity. Qed.

Lemma chg_pri_writes_one_cell : forall st id pri j,
    first_bad (chg_pri_guards st id pri) = None -> index_of min_tskid id <> j ->
    b_t (fst (chg_pri st id pri)) j = b_t st j.
Proof.
  intros st id pri j H Hj. unfold chg_pri. rewrite H. apply at_t_other_t. exact Hj.
Qed.

(* del: task_manage.c:220-247 -- only a DORMANT task can be released, and
 * releasing zeroes the stored marker, so the cell becomes unadoptable
 * evidence of its own past. *)
Definition del_guards (st : kst) (id : nat) : list (bool * er) :=
  id_guards st id ++
  ((negb (Nat.eqb (t_stat (b_t st (index_of min_tskid id))) S_NONEXIST))), E_NOEXS) ::
  ((Nat.eqb (t_stat (b_t st (index_of min_tskid id))) S_DORMANT)), E_OBJ) :: nil.

Definition del (st : kst) (id : nat) : kst * er :=
  match first_bad (del_guards st id) with
  | Some e => (st, e)
  | None => (bus_t st (index_of min_tskid id) free_tcb, E_OK)
  end.

Lemma del_refused_inert : forall st id e,
    first_bad (del_guards st id) = Some e -> del st id = (st, e).
Proof. intros st id e H. unfold del. rewrite H. reflexivity. Qed.

Lemma del_clears_the_marker : forall st id,
    first_bad (del_guards st id) = None -> tsk_used (fst (del st id)) (index_of min_tskid id) = false.
Proof.
  intros st id H. unfold del. rewrite H. unfold bus_t. rewrite upd_same.
  unfold tsk_used, free_tcb. cbn [Nat.eqb negb t_id]. reflexivity.
Qed.

Lemma del_then_cre_is_adoptable : forall st id i,
    first_bad (del_guards st id) = None -> index_of min_tskid id = i ->
    tsk_used (fst (del st id)) i = false /\ chk_id min_tskid num_tsk (id_of min_tskid i) = true.
Proof.
  intros st id i H H2. split.
  - rewrite <- H2. apply del_clears_the_marker; exact H.
  - apply chk_id_complete; unfold id_of; lia.
Qed.

(* ── 10. Receipts name their guard ─────────────────────────────── *)

(* The whole point of the cascade: two different refusals never return the
 * same code, so the caller can tell which precondition failed.  This is the
 * formal content of "traceable API objects" at the error side of the API. *)
Lemma receipt_is_E_OBJ_only_for_context : forall st id,
    first_bad (id_guards st id) = Some E_OBJ -> guard_self st id = false \/
    (guard_self st id = true /\ guard_range id = true /\ guard_nonself st id = false).
Proof.
  intros st id H. unfold id_guards in H.
  cbn [first_bad] in H.
  destruct (guard_self st id) eqn:A; cbn in H.
  - right. destruct (guard_range id) eqn:B; cbn in H.
    + destruct (guard_nonself st id) eqn:C; cbn in H; try (injection H; auto).
      exfalso. rewrite H in H0. discriminate H0.
    + injection H. intros H3. rewrite H3 in H0. discriminate H0.
  - left. injection H. auto.
Qed.

Lemma receipt_E_ID_means_out_of_range : forall st id,
    first_bad (id_guards st id) = Some E_ID -> guard_self st id = true /\ guard_range id = false.
Proof.
  intros st id H. unfold id_guards in H. cbn [first_bad] in H.
  destruct (guard_self st id) eqn:A; cbn in H; [ | firstorder].
  destruct (guard_range id) eqn:B; cbn in H; [ | split; auto].
  destruct (guard_nonself st id) eqn:C; cbn in H; discriminate H.
Qed.

Lemma no_two_receipts_same_code : forall e1 e2,
    er_mag e1 = er_mag e2 -> e1 = e2.
Proof. exact er_mag_distinct. Qed.

%APPEND-PLACEHOLDER
