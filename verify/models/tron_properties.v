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

(* Deliberately no "Open Scope bool_scope": under that scope "<>" is the
 * boolean test rather than the proposition, and the locality hypotheses below
 * are propositions.  Boolean conjunction is spelled andb/orb/negb. *)

(* ── 1. Contract constants ──────────────────────────────────────── *)

(* config.h:24-29.  The ceilings are runtime data, not constants: task.c:46
 * declares max_tskid and task.c:61 fills it from SCTAG_TMAXTSKID, so
 * MAX_TSKID (config.h:25) is a variable.  The oracle therefore fixes one
 * geometry -- 64 task slots -- and every law below is uniform in num_tsk
 * rather than tied to that figure.  With MIN_TSKID = 1 and the ceiling named
 * NUM_TSKID = MAX_TSKID, config.h:27's test
 * "MIN_TSKID <= id && id <= MAX_TSKID" is exactly "lo <= id && id < lo + num",
 * which is what chk_id below encodes once for every object family. *)
Definition min_tskid : nat := 1.          (* config.h:24 *)
Definition num_tsk : nat := 64.           (* oracle geometry for max_tskid *)
Definition min_pri : nat := 1.            (* config.h:128 -- highest priority *)
Definition max_pri : nat := 140.          (* config.h:129 -- lowest priority *)
Definition num_pri : nat := 140.          (* config.h:130, ready_queue.h:48 *)
(* The two counters saturate in the kernel at INT_MAX (task_sync.c:232 for
 * wupcnt, :51 for suscnt), reached by an equality test rather than a range
 * test.  The figure is data, not structure: the laws below only need a bound
 * that has a successor and that the oracle can exhaust in a test run, so the
 * model names 255 and the correspondence to INT_MAX is a change of constant,
 * not of shape. *)
Definition wupcap : nat := 255.

(* config.h:32-37 (semaphore), :48-53 (event flag), :56-61 (mailbox): the
 * object families the API reaches, each with the same MIN/MAX/INDEX/ID
 * quartet.  Every ID is an affine image of a table index, and that is the
 * whole ID story -- the same morphism, family by family. *)
Definition min_mbxid : nat := 1.          (* config.h:56 *)
Definition num_mbx : nat := 16.           (* oracle geometry for max_mbxid *)
Definition min_semid : nat := 1.          (* config.h:32 *)
Definition num_sem : nat := 16.           (* oracle geometry for max_semid *)
Definition min_flgid : nat := 1.          (* config.h:48 *)
Definition num_flg : nat := 16.           (* oracle geometry for max_flgid *)
Definition name_len : nat := 8.           (* config.h:165, USE_OBJECT_NAME :163 *)

(* ipc_msg.c:17-18 and message.h:12-24: the B-TRON message ring that crosses
 * thread domains. *)
Definition msg_ring_cap : nat := 64.      (* MAX_QUEUED_MSGS *)
Definition msg_domains : nat := 32.       (* MAX_IPC_PIDS *)
Definition msg_type_min : nat := 1.
Definition msg_type_max : nat := 31.

(* ── 2. Receipts: the ER codes the API returns ───────────────────── *)

(* The receipts the T-Kernel API hands back.  check.h names them by their main
 * code (E_PAR, E_ID, ...), and errno.h:27 turns that name into the ER a
 * caller sees, so the model keeps the main code and derives the wire figure
 * below rather than hard-coding either. *)
Inductive er : Type :=
  | E_OK
  | E_PAR
  | E_ID
  | E_CTX
  | E_MACV
  | E_LIMIT
  | E_OBJ
  | E_NOEXS
  | E_QOVR
  | E_RLWAI
  | E_TMOUT
  | E_DLT
  | E_DISWAI.

(* |MERCD| for each code, straight out of errno.h: E_OK :34, E_PAR :41,
 * E_ID :42, E_CTX :43, E_MACV :44, E_LIMIT :48, E_OBJ :49, E_NOEXS :50,
 * E_QOVR :51, E_RLWAI :52, E_TMOUT :53, E_DLT :54, E_DISWAI :55.  E_SYS (-5,
 * :36), E_NOSPT (-9, :38) and E_NOMEM (-33, :47) also ship, but only on the
 * boot, unlisted-service and dynamic-allocation paths (mailbox.c:73,79,
 * objname.c:154); no guard in the modelled cascade can produce them, so they
 * are outside the vocabulary rather than forgotten. *)
Definition er_mer (e : er) : nat :=
  match e with
  | E_OK     => 0
  | E_PAR    => 17
  | E_ID     => 18
  | E_CTX    => 25
  | E_MACV   => 26
  | E_LIMIT  => 34
  | E_OBJ    => 41
  | E_NOEXS  => 42
  | E_QOVR   => 43
  | E_RLWAI  => 49
  | E_TMOUT  => 50
  | E_DLT    => 51
  | E_DISWAI => 52
  end.

(* errno.h:29 -- ERCD(mer,ser) = (ER)(((UW)mer << 16) | (ser & 0xFFFF)).  Every
 * code the kernel returns here has ser = 0, so the shipped figure is the main
 * code scaled by 2^16 and negated; E_PAR is -1114112, not -17.  The model
 * keeps the main code as a nat (that is what the check.h macros name) and
 * derives the wire figure, so a receipt can never be confused with a result
 * and never with the wrong namespace either.  The scale is written as an
 * integer literal: 2^16 as a nat would be a unary term of 65536 constructors,
 * and nothing below needs it as a nat. *)
Definition er_code (e : er) : Z := Z.opp (Z.mul (Z.of_nat (er_mer e)) 65536).

Definition er_ok (e : er) : bool := Nat.eqb (er_mer e) 0.

Lemma er_mer_distinct : forall e1 e2, er_mer e1 = er_mer e2 -> e1 = e2.
Proof. destruct e1, e2; cbn [er_mer]; intros H; try discriminate H; reflexivity. Qed.

Lemma er_ok_iff : forall e, er_ok e = true <-> e = E_OK.
Proof.
  intros e. unfold er_ok. split.
  - intros H. rewrite Nat.eqb_eq in H. apply (er_mer_distinct e E_OK). cbn [er_mer]. exact H.
  - intros H; subst e. cbn [er_mer]. apply Nat.eqb_refl.
Qed.

Lemma er_code_is_shipped_figure : forall e,
    er_code e = Z.opp (Z.mul (Z.of_nat (er_mer e)) 65536).
Proof. intros e. reflexivity. Qed.

(* The two figures a reader of check.h expects: errno.h:41 and :54. *)
Lemma er_code_par : er_code E_PAR = Z.opp 1114112.
Proof. unfold er_code, er_mer. reflexivity. Qed.

Lemma er_code_dlt : er_code E_DLT = Z.opp 3342336.
Proof. unfold er_code, er_mer. reflexivity. Qed.

Lemma er_code_of_ok : er_code E_OK = Z0.
Proof. unfold er_code, er_mer. reflexivity. Qed.

Lemma er_code_separates : forall e1 e2, er_code e1 = er_code e2 -> e1 = e2.
Proof.
  destruct e1, e2; cbn [er_code er_mer]; intros H;
    try discriminate H; reflexivity.
Qed.

(* ── 3. Time-outs ───────────────────────────────────────────────── *)

(* typedef.h:55-56: the two special figures of the shipped TMO type are
 * TMO_POL = 0 and TMO_FEVR = -1, and TMO is a signed relative time -- this
 * kernel has no absolute-timeout attribute, so the model names three cases
 * rather than four.  check.h:185 refuses anything below -1 with E_PAR, which
 * means the legal range is exactly the three cases below: the type carries
 * the check, so no modelled call can produce that receipt. *)
Inductive tmo : Type :=
  | TMO_POLL
  | TMO_REL
  | TMO_FEVR.

Definition tmo_code (t : tmo) : Z :=
  match t with
  | TMO_POLL => Z0
  | TMO_REL  => Z.pos 1          (* any positive figure: no clock in scope *)
  | TMO_FEVR => Z.opp 1
  end.

Definition tmo_blocks (t : tmo) : bool :=
  match t with
  | TMO_POLL => false
  | TMO_REL | TMO_FEVR => true
  end.

(* check.h:254-258 (CHECK_DISPATCH_POL) refuses a wait whose figure is not
 * TMO_POL while dispatch is disabled: the test is against the sentinel, and
 * the model's blocking predicate agrees with that test rather than with the
 * arithmetic reading. *)
Definition tmo_not_poll (t : tmo) : bool := negb (Z.eqb (tmo_code t) Z0).

Lemma tmo_blocks_is_the_sentinel_test : forall t, tmo_blocks t = tmo_not_poll t.
Proof. destruct t; reflexivity. Qed.

Lemma tmo_code_is_shipped_figure :
    tmo_code TMO_POLL = Z0 /\ tmo_code TMO_FEVR = Z.opp 1.
Proof. split; reflexivity. Qed.

Lemma tmo_code_separates : forall t1 t2, tmo_code t1 = tmo_code t2 -> t1 = t2.
Proof. destruct t1, t2; intros H; cbn in H; try discriminate H; reflexivity. Qed.

(* "Blocks" is not "positive": a permanent wait is negative, so an arithmetic
 * guard would classify it as a poll and let a blocking call through in a
 * dispatch-disabled context.  The shipped code tests the sentinel. *)
Lemma positive_test_misclassifies_fevr : Z.ltb Z0 (tmo_code TMO_FEVR) = false.
Proof. reflexivity. Qed.

Lemma tmo_legal_is_total : forall t, Z.leb (Z.opp 1) (tmo_code t) = true.
Proof. destruct t; reflexivity. Qed.

(* ── 4. Two bit encodings of one state lattice ──────────────────── *)

(* task.h:45-52 gives the kernel's internal TSTAT as a bitmask in which
 * TS_WAITSUS is literally TS_WAIT | TS_SUSPEND; syscall.h:58-64 gives the
 * API's tskstat in which TTS_WAS is TTS_WAI | TTS_SUS, and every API figure
 * is the internal figure doubled.  The morphism between the two is therefore
 * not a lookup table of names but a bit shift, with bit 0 of the API word
 * reserved for TTS_RUN -- the one state the internal encoding does not have,
 * because task.c:191 uses TS_READY for running and ready tasks and names the
 * running one by the separate coordinate schedtsk (task.h:200).  That is the
 * observation this section turns into theorems. *)
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

(* The two masks used by task.h:56-59 and by the exclusion law below. *)
Definition live_mask : nat := Nat.lor ts_ready (Nat.lor ts_wait ts_suspend).
Definition wait_mask : nat := Nat.lor ts_wait ts_suspend.

Definition bits (s : tstat) : nat :=
  match s with
  | S_NONEXIST => ts_nonexist | S_READY => ts_ready | S_WAIT => ts_wait
  | S_SUSPEND => ts_suspend  | S_WAITSUS => ts_waitsus | S_DORMANT => ts_dormant
  end.

(* "Any of these bits is set" and "all of these bits are set": the kernel
 * tests the first form for liveness and the second for WAITSUS. *)
Definition bit_any (w m : nat) : bool := negb (Nat.eqb (Nat.land w m) 0).
Definition bit_all (w m : nat) : bool := Nat.eqb (Nat.land w m) m.

(* RUN is not a state of the internal encoding, so the projection takes the
 * running coordinate as an argument. *)
Definition api_of (run : bool) (s : tstat) : nat :=
  match s with
  | S_NONEXIST => 0
  | S_READY    => if run then tts_run else tts_rdy
  | S_WAIT     => tts_wai
  | S_SUSPEND  => tts_sus
  | S_WAITSUS  => tts_was
  | S_DORMANT  => tts_dmt
  end.

(* task.h:56-59: task_alive(state) == (state & (TS_READY|TS_WAIT|TS_SUSPEND))
 * != 0.  Note the shape of the test: a non-zero mask, not a positive figure.
 * Written as "bits > 0" the predicate reports TS_DORMANT (= 8, whose three
 * low bits are clear) as alive, which is exactly the defect the mask avoids.
 * The oracle in tron_model.ml uses the same mask. *)
Definition alive (s : tstat) : bool := bit_any (bits s) live_mask.

Lemma live_mask_is_7 : live_mask = 7.
Proof. reflexivity. Qed.

Lemma alive_table : forall s,
  alive s = match s with
            | S_NONEXIST | S_DORMANT => false
            | _ => true
            end.
Proof. destruct s; vm_compute; reflexivity. Qed.

Lemma alive_is_not_positivity : Nat.ltb (bits S_DORMANT) 0 = false /\ alive S_DORMANT = false.
Proof. split; vm_compute; reflexivity. Qed.

Lemma bits_injective : forall s1 s2, bits s1 = bits s2 -> s1 = s2.
Proof. destruct s1, s2; intros H; cbn [bits] in H; try discriminate H; reflexivity. Qed.

(* State equality as a boolean, in the kernel's own encoding.  The C guards
 * compare the TSTAT word with ==, and bits is injective, so comparing the
 * words decides comparing the states -- which is what lets a guard list be
 * built out of booleans at all. *)
Definition stat_eqb (a b : tstat) : bool := Nat.eqb (bits a) (bits b).

Lemma stat_eqb_iff : forall a b, stat_eqb a b = true <-> a = b.
Proof.
  intros a b. unfold stat_eqb. split.
  - intros H. apply bits_injective. apply Nat.eqb_eq. exact H.
  - intros H. rewrite H. apply Nat.eqb_refl.
Qed.

Lemma stat_eqb_refl : forall s, stat_eqb s s = true.
Proof. intros s. apply stat_eqb_iff. reflexivity. Qed.

Lemma bits_waitsus_is_join : bits S_WAITSUS = ts_wait + ts_suspend.
Proof. reflexivity. Qed.

Lemma api_waitsus_is_join : forall r,
    api_of r S_WAITSUS = api_of true S_WAIT + api_of true S_SUSPEND.
Proof. destruct r; reflexivity. Qed.

(* MORPHISM (1): the API word is the internal word shifted left by one, for
 * every state including NONEXIST, whose two figures are both zero. *)
Lemma api_of_is_doubling : forall s, api_of false s = Nat.mul 2 (bits s).
Proof. destruct s; vm_compute; reflexivity. Qed.

(* MORPHISM (2): the shift preserves disjointness of state bits, so the API
 * encoding inherits the exclusion structure rather than restating it. *)
Lemma doubling_preserves_disjointness : forall s1 s2,
    Nat.eqb (Nat.land (bits s1) (bits s2)) 0
    = Nat.eqb (Nat.land (api_of false s1) (api_of false s2)) 0.
Proof. destruct s1, s2; vm_compute; reflexivity. Qed.

(* EXCLUSION: no encoding ever sets the ready bit together with a waiting or
 * a suspended bit; equivalently bit 0 of the internal word and bit 1 of the
 * API word are the only places READY appears, and both are free for RUN. *)
Lemma ready_excludes_waiting : forall s,
    bit_any (bits s) ts_ready = true -> bit_any (bits s) wait_mask = false.
Proof. destruct s; vm_compute; congruence. Qed.

Lemma api_ready_excludes_waiting : forall s r,
    bit_any (api_of r s) tts_rdy = true -> bit_any (api_of r s) tts_was = false.
Proof. destruct s, r; vm_compute; congruence. Qed.

Lemma ready_bit_is_dormant_free : forall s,
    bit_all (bits s) ts_dormant = negb (Nat.ltb (bits s) ts_dormant).
Proof. destruct s; vm_compute; reflexivity. Qed.

(* The projection never identifies two internal states, for either value of
 * the running coordinate. *)
Lemma api_of_separates_states : forall r s1 s2,
    api_of r s1 = api_of r s2 -> s1 = s2.
Proof. destruct r, s1, s2; intros H; cbn [api_of] in H; try discriminate H; reflexivity. Qed.

(* NONEXIST projects to 0, which no TTS_* figure names: the API cannot report
 * an unregistered task, which is why the services answer E_NOEXS. *)
Lemma api_nonexistent_is_zero : forall r, api_of r S_NONEXIST = 0.
Proof. destruct r; reflexivity. Qed.

Lemma tts_figures_avoid_zero :
    Nat.ltb 0 tts_run = true /\ Nat.ltb 0 tts_rdy = true /\ Nat.ltb 0 tts_wai = true
    /\ Nat.ltb 0 tts_sus = true /\ Nat.ltb 0 tts_was = true /\ Nat.ltb 0 tts_dmt = true.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* MORPHISM (3): the running coordinate is genuinely needed.  READY and RUN
 * share one internal bit, so no function of the internal state alone can
 * produce the API figure -- the API word has a bit the internal word lacks. *)
Lemma api_needs_the_running_coordinate : exists s,
    api_of true s <> api_of false s /\ bit_all (bits s) ts_ready = true.
Proof. exists S_READY. split; vm_compute; [discriminate | reflexivity]. Qed.

Lemma api_of_ready_only_run_sensitive : forall s r,
    s <> S_READY -> api_of r s = api_of false s.
Proof.
  destruct s; intros r H.
  - reflexivity.
  - exfalso. apply H. reflexivity.
  - reflexivity.
  - reflexivity.
  - reflexivity.
  - reflexivity.
Qed.

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

(* The direction a service actually needs: an ID that passed its range test
 * is recovered from the index its handler computed, so the index can be
 * handed back as an ID (config.h:29 ID_TSK). *)
Lemma id_of_index_roundtrip : forall lo num id,
    chk_id lo num id = true -> id_of lo (index_of lo id) = id.
Proof.
  intros lo num id H. unfold id_of, index_of.
  apply chk_id_true in H. destruct H as [H1 _]. lia.
Qed.

Lemma chk_id_iff_index : forall lo num id,
    chk_id lo num id = true <-> exists i, i < num /\ id = id_of lo i.
Proof.
  intros lo num id. split.
  - intros H. apply chk_id_true in H. destruct H as [H1 H2]. exists (id - lo).
    unfold id_of. lia.
  - intros [i [Hi_lt Hid]]. subst id. apply chk_id_complete; unfold id_of; lia.
Qed.

Lemma zero_is_never_a_legal_id : forall lo num, 0 < lo -> chk_id lo num 0 = false.
Proof.
  intros lo num H. unfold chk_id.
  destruct (Nat.leb lo 0) eqn:E.
  - exfalso. apply Nat.leb_le in E. lia.
  - cbn [andb]. reflexivity.
Qed.

Lemma index_in_range : forall lo num id, chk_id lo num id = true -> index_of lo id < num.
Proof. intros lo num id H. apply chk_id_true in H. destruct H as [H1 H2]. unfold index_of. lia. Qed.

(* The kernel compares with "==" and a property states the same fact as a
 * proposition; the guard cascade below has to move between the two, and
 * these are the only two directions used.  The obvious-looking
 * Nat.neq_true_eq is not in the Rocq 9.2 standard library, so both
 * directions are derived here from Nat.eqb_eq. *)
Lemma neq_true : forall a b, negb (Nat.eqb a b) = true -> a <> b.
Proof.
  intros a b H Ex.
  assert (E : Nat.eqb a b = true) by (apply Nat.eqb_eq; exact Ex).
  rewrite E in H. cbn [negb] in H. discriminate H.
Qed.

Lemma ne_true_intro : forall a b, a <> b -> negb (Nat.eqb a b) = true.
Proof. intros a b H. apply negb_true_iff. apply Nat.eqb_neq. exact H. Qed.

(* ── 6. The kernel state: three object tables and one coordinate ─── *)

(* Tables are functions from the table index, as everywhere in this corpus
 * (\S ref:tables of the paper); the coordinate that is NOT a table is the
 * running task, because src/kernel/task.h:200 keeps it as the pointer
 * schedtsk.  The stored ID marker of each control block (TCB.tskid,
 * MBXCB.mbxid, SEMCB.semid) is what makes a cell traceable: a cell whose
 * marker is 0 has never been adopted, and 0 is not a legal ID (\ref{5}). *)
Definition upd (A : Type) (t : nat -> A) (i : nat) (v : A) : nat -> A :=
  fun j => if Nat.eqb i j then v else t j.

Lemma upd_same : forall (A : Type) (t : nat -> A) i (v : A), @upd A t i v i = v.
Proof. intros A t i v. unfold upd. cbn iota. rewrite Nat.eqb_refl. reflexivity. Qed.

Lemma upd_other : forall (A : Type) (t : nat -> A) i (v : A) j,
    i <> j -> @upd A t i v j = t j.
Proof.
  intros A t i v j H. unfold upd. cbn iota.
  destruct (Nat.eqb i j) eqn:E.
  - exfalso. apply H. apply Nat.eqb_eq. exact E.
  - reflexivity.
Qed.

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
    b_indp : bool;              (* cpu_status.h:73 in_indp(): the task-independent part *)
    b_ddsp : bool               (* cpu_status.h:77 in_ddsp(): dispatch disabled *)
  }.

Definition bus_t (st : kst) (i : nat) (v : tcb) : kst :=
  mk_kst (@upd tcb (b_t st) i v) (b_m st) (b_s st) (b_run st) (b_indp st) (b_ddsp st).
Definition bus_m (st : kst) (i : nat) (v : mbx) : kst :=
  mk_kst (b_t st) (@upd mbx (b_m st) i v) (b_s st) (b_run st) (b_indp st) (b_ddsp st).
Definition bus_s (st : kst) (i : nat) (v : sem) : kst :=
  mk_kst (b_t st) (b_m st) (@upd sem (b_s st) i v) (b_run st) (b_indp st) (b_ddsp st).

Lemma at_t_same_t : forall st i (t : tcb), b_t (bus_t st i t) i = t.
Proof. intros st i t. unfold bus_t. apply upd_same. Qed.

Lemma at_t_other_t : forall st i (t : tcb) j, i <> j -> b_t (bus_t st i t) j = b_t st j.
Proof. intros st i t j H. unfold bus_t. apply upd_other; exact H. Qed.

Lemma at_m_bus_t : forall st i (t : tcb) j, b_m (bus_t st i t) j = b_m st j.
Proof. intros st i t j. unfold bus_t. reflexivity. Qed.

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

Lemma ddsp_inert_t : forall st i (t : tcb), b_ddsp (bus_t st i t) = b_ddsp st.
Proof. intros st i t. unfold bus_t. reflexivity. Qed.

Lemma ddsp_inert_m : forall st i (v : mbx), b_ddsp (bus_m st i v) = b_ddsp st.
Proof. intros st i v. unfold bus_m. reflexivity. Qed.

Lemma ddsp_inert_s : forall st i (v : sem), b_ddsp (bus_s st i v) = b_ddsp st.
Proof. intros st i v. unfold bus_s. reflexivity. Qed.

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

(* Reading a field back through the bus, in one step.  Every service below
 * writes exactly one cell, so the receipt can be tied to the field it
 * changed without re-deriving the update each time. *)
Lemma t_stat_after : forall st i t s,
    t_stat (b_t (bus_t st i (t_stat_ s t)) i) = s.
Proof. intros st i t s. rewrite at_t_same_t. unfold t_stat_. reflexivity. Qed.

(* Record eta for a state write: the rest of the TCB carries through, which is
 * what makes "changes the state" and "changes nothing else" provable at the
 * same time.  task_manage.c:257 (sta) and :266-268 (chg_pri) both assign a
 * single field of an existing TCB. *)
Lemma t_stat_carries : forall t s,
    t_stat t = s -> t = mk_tcb (t_id t) s (t_pri t) (t_bpri t)
                            (t_wup t) (t_sus t) (t_klock t) (t_wait t) (t_slot t).
Proof. intros t s H. rewrite <- H. destruct t; reflexivity. Qed.

Lemma t_pri_after : forall st i t p,
    t_pri (b_t (bus_t st i (t_pri_ p t)) i) = p.
Proof. intros st i t p. rewrite at_t_same_t. unfold t_pri_. reflexivity. Qed.

Lemma t_wup_after : forall st i t n,
    t_wup (b_t (bus_t st i (t_wup_ n t)) i) = n.
Proof. intros st i t n. rewrite at_t_same_t. unfold t_wup_. reflexivity. Qed.

Lemma t_sus_after : forall st i t n,
    t_sus (b_t (bus_t st i (t_sus_ n t)) i) = n.
Proof. intros st i t n. rewrite at_t_same_t. unfold t_sus_. reflexivity. Qed.

Lemma t_klock_after : forall st i t b,
    t_klock (b_t (bus_t st i (t_klock_ b t)) i) = b.
Proof. intros st i t b. rewrite at_t_same_t. unfold t_klock_. reflexivity. Qed.

Lemma t_wait_after : forall st i t w,
    t_wait (b_t (bus_t st i (t_wait_ w t)) i) = w.
Proof. intros st i t w. rewrite at_t_same_t. unfold t_wait_. reflexivity. Qed.

Lemma t_slot_after : forall st i t w,
    t_slot (b_t (bus_t st i (t_slot_ w t)) i) = w.
Proof. intros st i t w. rewrite at_t_same_t. unfold t_slot_. reflexivity. Qed.

(* task.c:106-131 make_dormant: state DORMANT, priority restored from
 * bpriority, wupcnt and suscnt and the kernel-lock flags cleared.  The
 * model resets the whole adoptable record, which is what makes the reset a
 * projection. *)
Definition free_tcb : tcb :=
  mk_tcb 0 S_NONEXIST min_pri min_pri 0 0 false None None.
Definition free_mbx : mbx := mk_mbx 0 false [] [].
Definition free_sem : sem := mk_sem 0 0 [].

Definition make_dormant (t : tcb) : tcb :=
  t_slot_ None (t_wait_ None (t_klock_ false (t_sus_ 0 (t_wup_ 0 (t_pri_ (t_bpri t) (t_stat_ S_DORMANT t)))))).

(* The empty kernel has no running task.  ctxtsk == NULL is one of the two arms
 * of in_indp() (cpu_status.h:73), and in_indp() is one of the three arms of
 * in_ddsp() (:77), so an idle state is both independent and dispatch-disabled
 * -- the honest initial values are true, not false. *)
Definition st0 : kst :=
  mk_kst (fun _ => free_tcb) (fun _ => free_mbx) (fun _ => free_sem) 0 true true.

(* Two coordinates, one implication.  The kernel's own header comments the
 * subsumption ("Also include the task independent part as during dispatch
 * disable", cpu_status.h:76-77), and the second arm says a missing ctxtsk is
 * an independent call.  A state that violates either is not a state the shipped
 * macros can produce, so the services that read one coordinate and refuse on
 * the other are stated over ctx_wf rather than over every kst. *)
Definition impliesb (a b : bool) : bool := orb (negb a) b.

Definition ctx_wf (st : kst) : bool :=
  andb (impliesb (b_indp st) (b_ddsp st))
       (impliesb (Nat.eqb (b_run st) 0) (b_indp st)).

Lemma st0_is_a_wellformed_context : ctx_wf st0 = true.
Proof. unfold ctx_wf, st0. cbn [impliesb negb orb Nat.eqb]. reflexivity. Qed.

(* The consequence the two guards are read for: an independent call may name
 * TSK_SELF (check.h:27 admits it) and may not block (check.h:250 refuses it).
 * So the §9 self guard and the §13 dispatch guard cannot both be satisfied by
 * one state -- which is exactly what a single b_indp coordinate would have
 * claimed they could. *)
Lemma independent_calls_may_not_wait : forall st,
    ctx_wf st = true -> b_indp st = true -> b_ddsp st = true.
Proof.
  intros st W I. unfold ctx_wf, impliesb in W. rewrite I in W.
  cbn [negb orb] in W. apply andb_true_iff in W. destruct W as [H1 _].
  exact H1.
Qed.

Lemma dispatching_calls_are_dependent : forall st,
    ctx_wf st = true -> b_ddsp st = false -> b_indp st = false /\ 0 < b_run st.
Proof.
  intros st W D. unfold ctx_wf, impliesb in W. rewrite D in W.
  apply andb_true_iff in W. destruct W as [X Y].
  apply orb_true_iff in X. destruct X as [Q | F]; [ | discriminate F].
  apply negb_true_iff in Q.
  assert (Z : b_run st <> 0).
  { apply orb_true_iff in Y. destruct Y as [Q2 | F2].
    - apply negb_true_iff in Q2. intros E. rewrite E in Q2.
      cbn [Nat.eqb] in Q2. discriminate Q2.
    - rewrite F2 in Q. discriminate Q. }
  split; [ exact Q | ].
  destruct (b_run st); [ contradiction | ]; lia.
Qed.

Lemma some_context_admits_a_wait : exists st, ctx_wf st = true /\ b_ddsp st = false.
Proof.
  exists (mk_kst (b_t st0) (b_m st0) (b_s st0) min_tskid false false). split.
  - unfold ctx_wf, impliesb. cbn [impliesb negb Nat.eqb orb andb]. reflexivity.
  - reflexivity.
Qed.

(* Existence is a stored marker, not a bit: mailbox.c:229 and 334 both test
 * mbxcb->mbxid == 0 after the range check. *)
Definition tsk_used (st : kst) (i : nat) : bool := negb (Nat.eqb (t_id (b_t st i)) 0).
Definition mbx_used (st : kst) (i : nat) : bool := negb (Nat.eqb (m_id (b_m st i)) 0).
Definition sem_used (st : kst) (i : nat) : bool := negb (Nat.eqb (s_id (b_s st i)) 0).

Definition tsk_live (st : kst) (id : nat) : bool :=
    andb (chk_id min_tskid num_tsk id) (tsk_used st (index_of min_tskid id)).
Definition mbx_live (st : kst) (id : nat) : bool :=
    andb (chk_id min_mbxid num_mbx id) (mbx_used st (index_of min_mbxid id)).

(* ── 7. The preflight cascade: which guard fires is which receipt ── *)

(* check.h:27-39 gives the order explicitly: the self test precedes the range
 * test, and each guard carries its own ER.  A service is therefore a list of
 * (does-this-pass, otherwise-this-code) pairs, and the receipt of a refusal
 * names the guard that refused it.  first_bad reads that list left to right. *)
(* Written as a pattern-matching fixpoint rather than a fold over fst/snd:
 * a projection of a pair does not reduce under cbn, and every law below is
 * a computation on the head of the list. *)
Fixpoint first_bad (gs : list (bool * er)) : option er :=
  match gs with
  | nil => @None er
  | (p, e) :: rest => if p then first_bad rest else Some e
  end.

(* The conjunction of the guard tests. *)
Fixpoint all_pass (gs : list (bool * er)) : bool :=
  match gs with
  | nil => true
  | (p, _) :: rest => andb p (all_pass rest)
  end.

Lemma first_bad_nil : first_bad (@nil (bool * er)) = @None er.
Proof. reflexivity. Qed.

Lemma first_bad_pass : forall e gs, first_bad ((true, e) :: gs) = first_bad gs.
Proof. reflexivity. Qed.

Lemma first_bad_fail : forall e gs, first_bad ((false, e) :: gs) = Some e.
Proof. reflexivity. Qed.

(* A refusal at the head names the head guard. *)
Lemma first_bad_head : forall e gs e', first_bad ((false, e) :: gs) = Some e' -> e' = e.
Proof. intros e gs e' H. injection H. auto. Qed.

(* A cascade that is silent on its prefix is decided by its suffix: this is
 * how a service's own state guards are read once the shared ID guards have
 * passed. *)
Lemma first_bad_app : forall (l m : list (bool * er)),
    first_bad l = None -> first_bad (l ++ m) = first_bad m.
Proof.
  induction l as [|a l IH]; intros m H.
  - reflexivity.
  - destruct a as [p e]. destruct p.
    + cbn [first_bad app] in H |- *. apply IH. exact H.
    + cbn [first_bad app] in H. discriminate H.
Qed.

(* The cascade is silent exactly when every guard passes. *)
Lemma all_pass_cons : forall p e gs, all_pass ((p, e) :: gs) = andb p (all_pass gs).
Proof. reflexivity. Qed.

Lemma all_pass_app : forall (l m : list (bool * er)),
    all_pass (l ++ m) = andb (all_pass l) (all_pass m).
Proof.
  induction l as [|a l IHl]; intros m.
  - reflexivity.
  - destruct a as [p e]. simpl. rewrite IHl. apply andb_assoc.
Qed.

Lemma first_bad_none_all_pass : forall gs, first_bad gs = None <-> all_pass gs = true.
Proof.
  induction gs as [|g gs IHe].
  - split; reflexivity.
  - destruct g as [p e]. rewrite all_pass_cons. destruct p.
    + rewrite first_bad_pass, IHe. reflexivity.
    + rewrite first_bad_fail. split; intros H; discriminate H.
Qed.

(* The receipt of a refusal names a guard that fired, and every guard before
 * it passed -- which is what makes an ER traceable to a check.h line. *)
Lemma first_bad_some_is_a_failing_guard : forall gs e,
    first_bad gs = Some e ->
    exists pre, exists q, exists post,
      gs = pre ++ ((q, e) :: post) /\ all_pass pre = true /\ q = false.
Proof.
  induction gs as [|g gs IHe].
  - intros e H. discriminate H.
  - destruct g as [p e0]. destruct p.
    + intros e H. rewrite first_bad_pass in H.
      destruct (IHe e H) as [pre [q [post [Hid [Hp Hq]]]]].
      exists ((true, e0) :: pre). exists q. exists post. split.
      * rewrite Hid. cbn [app]. reflexivity.
      * split.
        -- cbn [all_pass]. rewrite Hp. reflexivity.
        -- exact Hq.
    + intros e H. rewrite first_bad_fail in H. injection H. intros E. subst e.
      exists (@nil (bool * er)). exists false. exists gs. split.
      * cbn [app]. reflexivity.
      * split; reflexivity.
Qed.

(* Two guards, not one.  CHECK_TSKID admits nothing outside the ID range, and
 * the stored marker admits nothing outside the table; the range test alone
 * cannot tell an unadopted cell from an adopted one, which is why
 * task_manage.c:233 and mailbox.c:229 both continue past CHECK_*ID. *)
Lemma range_is_not_existence : exists st id,
    chk_id min_tskid num_tsk id = true /\ tsk_live st id = false.
Proof. exists st0, min_tskid. split.
  - apply chk_id_complete; unfold min_tskid, num_tsk; lia.
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
    + apply IH in H. destruct H as [H1 H2]. lia.
    + injection H as E. rewrite <- E. lia.
Qed.

Lemma first_free_found_free : forall used fuel i o,
    first_free used fuel i = Some o -> used o = false.
Proof.
  intros used fuel; induction fuel as [|k IH]; intros i o H; cbn [first_free] in H.
  - destruct o; discriminate H.
  - destruct (used i) eqn:U; cbn [negb] in H.
    + exact (IH (S i) o H).
    + injection H as E; subst o; exact U.
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
  intros st pri i j H Hi. unfold cre. rewrite H. cbn [fst].
  apply at_t_other_t; exact Hi.
Qed.

Lemma cre_stamps_the_marker : forall st pri i,
    first_free (tsk_used st) num_tsk 0 = Some i ->
    t_id (b_t (fst (cre st pri)) i) = id_of min_tskid i /\
    tsk_used (fst (cre st pri)) i = true.
Proof.
  intros st pri i H. unfold cre. rewrite H. cbn [fst]. split.
  - rewrite at_t_same_t. unfold adopt_tcb. reflexivity.
  - unfold tsk_used. rewrite at_t_same_t. cbn [t_id adopt_tcb].
    apply ne_true_intro. unfold id_of, min_tskid. lia.
Qed.

Lemma cre_dormant_on_arrival : forall st pri i,
    first_free (tsk_used st) num_tsk 0 = Some i ->
    t_stat (b_t (fst (cre st pri)) i) = S_DORMANT.
Proof.
  intros st pri i H. unfold cre. rewrite H. cbn [fst].
  rewrite at_t_same_t. unfold adopt_tcb. reflexivity.
Qed.

Lemma cre_index_in_range : forall st (pri : nat) i,
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

(* The cascade read as a case table.  What the table records is the order:
 * TSK_SELF in a dependent context is E_OBJ, and the same ID in an
 * independent context is admitted by the self test only to be refused by the
 * range test with E_ID, because MIN_TSKID is 1 and self_is_out_of_range
 * applies.  That asymmetry is check.h:27-31 read literally, and it is the
 * reason a receipt identifies a guard rather than merely a failure. *)
Lemma id_guards_self_dependent : forall st,
    b_indp st = false -> first_bad (id_guards st tsk_self) = Some E_OBJ.
Proof.
  intros st I. unfold id_guards, guard_self, guard_nonself.
  cbn [first_bad fst orb negb]. rewrite I, Nat.eqb_refl. cbn [orb negb]. reflexivity.
Qed.

Lemma id_guards_self_independent : forall st,
    b_indp st = true -> first_bad (id_guards st tsk_self) = Some E_ID.
Proof.
  intros st I. unfold id_guards, guard_self, guard_nonself, guard_range.
  cbn [first_bad fold_right]. rewrite I. cbn [orb]. rewrite self_is_out_of_range. reflexivity.
Qed.

Lemma id_guards_run_dependent : forall st,
    b_indp st = false -> chk_id min_tskid num_tsk (b_run st) = true ->
    first_bad (id_guards st (b_run st)) = Some E_OBJ.
Proof.
  intros st I C. unfold id_guards, guard_self, guard_nonself, guard_range.
  cbn [first_bad fold_right]. rewrite I, C. cbn [orb].
  rewrite (ne_true_intro (b_run st) tsk_self).
  - cbn [negb]. rewrite Nat.eqb_refl. reflexivity.
  - intros Ex. rewrite Ex in C. rewrite self_is_out_of_range in C. discriminate C.
Qed.

Lemma id_guards_pass_independent : forall st id,
    b_indp st = true -> chk_id min_tskid num_tsk id = true ->
    first_bad (id_guards st id) = None.
Proof.
  intros st id I C. unfold id_guards, guard_self, guard_nonself, guard_range.
  cbn [first_bad fold_right]. rewrite I, C. cbn [orb]. reflexivity.
Qed.

Lemma id_guards_pass_dependent : forall st id,
    b_indp st = false -> negb (Nat.eqb id tsk_self) = true ->
    chk_id min_tskid num_tsk id = true -> negb (Nat.eqb id (b_run st)) = true ->
    first_bad (id_guards st id) = None.
Proof.
  intros st id I S C N. unfold id_guards, guard_self, guard_nonself, guard_range.
  cbn [first_bad fold_right]. rewrite I, S, C, N. cbn [orb negb]. reflexivity.
Qed.

(* The range test is not one of the two escape hatches: no context admits an
 * out-of-range ID through this cascade. *)
Lemma id_guards_range_required : forall st id,
    first_bad (id_guards st id) = None -> chk_id min_tskid num_tsk id = true.
Proof.
  intros st id H.
  assert (A : all_pass (id_guards st id) = true)
    by (apply first_bad_none_all_pass; exact H).
  unfold id_guards in A. cbn [all_pass] in A.
  apply andb_true_iff in A. destruct A as [_ H2].
  apply andb_true_iff in H2. destruct H2 as [Hr _].
  unfold guard_range in Hr. exact Hr.
Qed.

(* sta: task_manage.c:248-270.  DORMANT is the only startable state; the
 * nested if at :261-263 splits into E_NOEXS and E_OBJ exactly as check.h
 * orders the tests. *)
Definition sta_guards (st : kst) (id : nat) : list (bool * er) :=
  id_guards st id ++
  ((negb (stat_eqb (t_stat (b_t st (index_of min_tskid id))) S_NONEXIST)), E_NOEXS) ::
  ((stat_eqb (t_stat (b_t st (index_of min_tskid id))) S_DORMANT), E_OBJ) :: nil.

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
  intros st id H. unfold sta. rewrite H. cbn [fst]. apply t_stat_after.
Qed.

(* ter: task_manage.c:381-402 -- alive required, and the caller may not
 * terminate itself; the target goes back through make_dormant. *)
Definition ter_guards (st : kst) (id : nat) : list (bool * er) :=
  id_guards st id ++
  ((alive (t_stat (b_t st (index_of min_tskid id)))), E_NOEXS) ::
  ((negb (stat_eqb (t_stat (b_t st (index_of min_tskid id))) S_DORMANT)), E_OBJ) :: nil.

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
  intros st id H. unfold ter. rewrite H. cbn [fst].
  rewrite !at_t_same_t. unfold make_dormant. repeat split; reflexivity.
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
  ((negb (stat_eqb (t_stat (b_t st (index_of min_tskid id))) S_NONEXIST)), E_NOEXS) ::
  ((stat_eqb (t_stat (b_t st (index_of min_tskid id))) S_DORMANT), E_OBJ) :: nil.

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
  intros st id pri j H Hj. unfold chg_pri. rewrite H. cbn [fst].
  apply at_t_other_t. exact Hj.
Qed.

(* del: task_manage.c:220-247 -- only a DORMANT task can be released, and
 * releasing zeroes the stored marker, so the cell becomes unadoptable
 * evidence of its own past. *)
Definition del_guards (st : kst) (id : nat) : list (bool * er) :=
  id_guards st id ++
  ((negb (stat_eqb (t_stat (b_t st (index_of min_tskid id))) S_NONEXIST)), E_NOEXS) ::
  ((stat_eqb (t_stat (b_t st (index_of min_tskid id))) S_DORMANT), E_OBJ) :: nil.

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
  intros st id H. unfold del. rewrite H. cbn [fst]. unfold tsk_used.
  rewrite at_t_same_t. unfold free_tcb. reflexivity.
Qed.

(* A released cell is adoptable again: the marker is gone, and the ID the next
 * cre would hand back for that index is still inside the range -- task
 * recycling is legal at the table level, which is why the stored marker and
 * not the index is the evidence of existence. *)
Lemma del_then_cre_is_adoptable : forall st id i,
    first_bad (del_guards st id) = None -> index_of min_tskid id = i ->
    tsk_used (fst (del st id)) i = false /\ chk_id min_tskid num_tsk (id_of min_tskid i) = true.
Proof.
  intros st id i H H2.
  assert (C : chk_id min_tskid num_tsk id = true).
  { unfold del_guards in H.
    apply first_bad_none_all_pass in H. rewrite all_pass_app in H.
    apply andb_true_iff in H. destruct H as [P _].
    apply first_bad_none_all_pass in P.
    apply (id_guards_range_required st id). exact P. }
  split.
  - rewrite <- H2. apply del_clears_the_marker; exact H.
  - rewrite <- H2. rewrite (id_of_index_roundtrip min_tskid num_tsk id C). exact C.
Qed.

(* task_manage.c:227-247: del_tsk refuses a task that is not DORMANT, and the
 * two reasons for refusing are separated -- the state test is reached only
 * once the shared ID guards have passed, so a receipt naming E_NOEXS or E_OBJ
 * comes from the state and not from the handle. *)
Lemma del_state_guard : forall st id e,
    first_bad (id_guards st id) = None ->
    first_bad (del_guards st id) = Some e ->
    stat_eqb (t_stat (b_t st (index_of min_tskid id))) S_DORMANT = false ->
    e = E_OBJ \/ e = E_NOEXS.
Proof.
  intros st id e I H D. unfold del_guards in H.
  rewrite (first_bad_app (id_guards st id)) in H; [ | exact I ].
  rewrite D in H.
  destruct (stat_eqb (t_stat (b_t st (index_of min_tskid id))) S_NONEXIST) eqn:N;
    cbn [first_bad negb] in H.
  - right. injection H. auto.
  - left. injection H. auto.
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
  destruct (guard_self st id);
  destruct (guard_range id);
  destruct (guard_nonself st id);
  cbn [first_bad] in H;
  try (exfalso; discriminate H).
  - right. split; [reflexivity | split; [reflexivity | reflexivity]].
  - left; reflexivity.
  - left; reflexivity.
  - left; reflexivity.
  - left; reflexivity.
Qed.

(* E_ID is the unique receipt of the range test: it can only be reached when the
 * context test has already passed.  Under TSK_SELF in a dependent context the
 * cascade stops earlier and reports E_OBJ, so an E_ID never witnesses a
 * self-reference -- check.h:27-39 in one line. *)
Lemma receipt_E_ID_means_out_of_range : forall st id,
    first_bad (id_guards st id) = Some E_ID -> guard_self st id = true /\ guard_range id = false.
Proof.
  intros st id H. unfold id_guards in H.
  destruct (guard_self st id);
  destruct (guard_range id);
  destruct (guard_nonself st id);
  cbn [first_bad] in H;
  try (exfalso; discriminate H).
  - split; reflexivity.
  - split; reflexivity.
Qed.

Lemma no_two_receipts_same_code : forall e1 e2,
    er_mer e1 = er_mer e2 -> e1 = e2.
Proof. exact er_mer_distinct. Qed.

(* ── 11. Ready queue: rows per priority, one empty sentinel ──── *)

(* ready_queue.h:44-51 keeps one task queue per priority ('tskque'), a bitmap
 * marking the nonempty queues, 'top_priority', and 'klocktsk'.  The bitmap is
 * exactly the DOMAIN of the row map and top_priority is exactly its minimum, so
 * the model stores a sparse association list keyed by the internal priority:
 * domain = bitmap, minimum key = top_priority, absent key = cleared bit.
 * NUM_PRI itself is the empty sentinel (ready_queue.h:26-32): tskque[NUM_PRI]
 * falls off the end of the array onto the adjacent 'null' field, which
 * ready_queue_initialize sets to NULL (ready_queue.h:66), so the sentinel row
 * reads empty by construction. *)

(* task.h:65-66 is the affine map between the two priority namespaces; MIN_PRI is
 * 1, so the row index is the API number minus one. *)
Definition pri_index (pri : nat) : nat := pri - min_pri.
Definition pri_api (idx : nat) : nat := idx + min_pri.

Lemma pri_index_of_api : forall pri, chk_id min_pri num_pri pri = true ->
    pri_api (pri_index pri) = pri.
Proof.
  intros pri H. unfold chk_id, min_pri, num_pri in H. apply andb_true_iff in H.
  destruct H as [H1 H2].
  apply Nat.leb_le in H1. apply Nat.ltb_lt in H2.
  unfold pri_index, pri_api, min_pri. lia.
Qed.

Lemma pri_api_of_index : forall idx, idx < num_pri -> pri_index (pri_api idx) = idx.
Proof.
  intros idx H. unfold pri_api, pri_index, min_pri. lia.
Qed.

(* CHK_PRI is not cosmetic: pri_index saturates at 0, so API priority 0 -- which
 * the guard rejects -- would be admitted into row 0, the HIGHEST priority row.
 * In C the same expression is (INT)(0 - 1) = -1 and tskque[-1] reads the header
 * word top_priority instead of a row, because ready_queue.h:95-104 does no bounds
 * check of its own.  The model can only express the saturation, which is the
 * milder of the two failures. *)
Lemma chk_pri_refuses_zero : chk_id min_pri num_pri 0 = false.
Proof. unfold chk_id, min_pri, num_pri. reflexivity. Qed.

Lemma pri_index_saturates_at_zero : pri_index 0 = pri_index min_pri.
Proof. unfold pri_index, min_pri. reflexivity. Qed.

Definition rrow : Set := list nat.
Definition rows : Set := list (nat * rrow).

(* isQueEmpty is a pointer compare, not a length computation. *)
Definition nilp (r : rrow) : bool :=
  match r with nil => true | _ :: _ => false end.

Fixpoint row_of (pri : nat) (rq : rows) : option rrow :=
  match rq with
  | nil => None
  | (p, r) :: tl => if Nat.eqb p pri then Some r else row_of pri tl
  end.

Fixpoint put_row (pri : nat) (r : rrow) (rq : rows) : rows :=
  match rq with
  | nil => (pri, r) :: nil
  | (p, r0) :: tl => if Nat.eqb p pri then (pri, r) :: tl else (p, r0) :: put_row pri r tl
  end.

(* One queue per priority, so a key is never repeated (ready_queue.h:46 sizes the
 * array at NUM_PRI).  Skipping every match makes del_row the exact inverse of
 * row_of without needing that invariant as a hypothesis. *)
Fixpoint del_row (pri : nat) (rq : rows) : rows :=
  match rq with
  | nil => nil
  | (p, r) :: tl => if Nat.eqb p pri then del_row pri tl else (p, r) :: del_row pri tl
  end.

(* Writing an empty row clears the bit instead of leaving a set-but-empty row:
 * ready_queue.h:143 BitClr.  That is what makes top_priority recomputable from
 * the domain alone. *)
Definition set_row (pri : nat) (r : rrow) (rq : rows) : rows :=
  if nilp r then del_row pri rq else put_row pri r rq.

Definition row_at (pri : nat) (rq : rows) : rrow :=
  match row_of pri rq with Some r => r | None => nil end.

(* QueRemove unlinks one node; a row has no duplicates, so dropping every match
 * is the same observable move. *)
Fixpoint without (tid : nat) (r : rrow) : rrow :=
  match r with
  | nil => nil
  | t :: tl => if Nat.eqb t tid then without tid tl else t :: without tid tl
  end.

Definition memb (tid : nat) (r : rrow) : bool := existsb (Nat.eqb tid) r.

Fixpoint top_pri (rq : rows) : nat :=
  match rq with
  | nil => num_pri
  | (p, r) :: tl => if nilp r then top_pri tl else Nat.min p (top_pri tl)
  end.

Record rdy : Set := mk_rdy { rd_rows : rows; rd_klock : option nat }.

(* ready_queue.h:68-76: ready_queue_top answers with klocktsk when it is set and
 * with the head of the top row otherwise.  klocktsk is a SHADOW pointer that
 * outranks the queue structure; it is not a member of it. *)
Definition front (rd : rdy) : option nat :=
  match rd_klock rd with
  | Some t => Some t
  | None => match row_of (top_pri (rd_rows rd)) (rd_rows rd) with
            | Some (t :: _) => Some t
            | _ => None
            end
  end.

(* ready_queue.h:95-105: append to the row of the task's priority. *)
Definition rq_insert (pri : nat) (tid : nat) (rq : rows) : rows :=
  set_row pri (row_at pri rq ++ [tid]) rq.

(* ready_queue.h:110-122: prepend to the row. *)
Definition rq_insert_top (pri : nat) (tid : nat) (rq : rows) : rows :=
  set_row pri (tid :: row_at pri rq) rq.

(* ready_queue.h:124-155: unlink from the row, clear the bit if it empties. *)
Definition rq_delete (pri : nat) (tid : nat) (rq : rows) : rows :=
  set_row pri (without tid (row_at pri rq)) rq.

(* ready_queue.h:183-196.  The header says "move the task, whose ready queue
 * priority is 'priority', at head of queue to the end of queue.  Do nothing if
 * the queue is empty": ONE row, head to tail.  It is not a cyclic permutation of
 * the priorities -- no task changes row, and every other row is untouched.
 * Modelling tk_rot_rdq as a rotation of the priority set would be a false claim
 * about the kernel; the two lemmas below are the correction. *)
Definition rotate_row (r : rrow) : rrow :=
  match r with nil => nil | t :: tl => tl ++ [t] end.

Definition rq_rotate (pri : nat) (rq : rows) : rows :=
  set_row pri (rotate_row (row_at pri rq)) rq.

Lemma min_swap : forall a b c, Nat.min a (Nat.min b c) = Nat.min b (Nat.min a c).
Proof.
  intros a b c. rewrite Nat.min_assoc, (Nat.min_comm a), Nat.min_assoc. reflexivity.
Qed.

Lemma min_min_l : forall n m, Nat.min n (Nat.min n m) = Nat.min n m.
Proof. intros n m. rewrite Nat.min_assoc, Nat.min_id. reflexivity. Qed.

Lemma nilp_nil : nilp nil = true.
Proof. reflexivity. Qed.

Lemma nilp_cons : forall t r, nilp (t :: r) = false.
Proof. reflexivity. Qed.

Lemma nilp_app_one : forall (l : rrow) t, nilp (l ++ [t]) = false.
Proof. intros l t. induction l as [|a l IH]; cbn [app]; reflexivity. Qed.

Lemma nilp_true_iff : forall r, nilp r = true -> r = nil.
Proof. intros [|t l] N; [reflexivity | discriminate N]. Qed.

Lemma nilp_rotate : forall r, nilp (rotate_row r) = nilp r.
Proof.
  intros r. destruct r as [|t l]; [reflexivity |].
  unfold rotate_row. rewrite nilp_app_one. reflexivity.
Qed.

Lemma memb_app_one : forall t l, memb t (l ++ [t]) = true.
Proof.
  induction l as [|a l IH].
  - unfold memb. cbn [app existsb]. rewrite Nat.eqb_refl. cbn [orb]. reflexivity.
  - unfold memb in *. cbn [app existsb].
    destruct (Nat.eqb t a) eqn:E; cbn [orb]; [reflexivity |].
    rewrite IH. cbn [orb]. reflexivity.
Qed.

Lemma row_of_put_same : forall pri r rq, row_of pri (put_row pri r rq) = Some r.
Proof.
  intros pri r rq. induction rq as [|(p, r0) tl IH]; [| cbn [put_row]; destruct (Nat.eqb p pri) eqn:E].
  - cbn [put_row row_of]. rewrite Nat.eqb_refl. reflexivity.
  - apply Nat.eqb_eq in E. subst p. cbn [row_of]. rewrite Nat.eqb_refl. reflexivity.
  - cbn [row_of]. rewrite E. exact IH.
Qed.

Lemma row_of_put_other : forall q pri r rq, Nat.eqb q pri = false ->
    row_of q (put_row pri r rq) = row_of q rq.
Proof.
  intros q pri r rq E. induction rq as [|(p, r0) tl IH].
  - cbn [put_row row_of]. destruct (Nat.eqb pri q) eqn:Q.
    + apply Nat.eqb_eq in Q. rewrite <- Q in E.
      rewrite Nat.eqb_refl in E. discriminate E.
    + reflexivity.
  - cbn [put_row]. destruct (Nat.eqb p pri) eqn:P; cbn [row_of].
    + apply Nat.eqb_eq in P. subst p. destruct (Nat.eqb pri q) eqn:Q.
      * apply Nat.eqb_eq in Q. rewrite <- Q in E.
        rewrite Nat.eqb_refl in E. discriminate E.
      * reflexivity.
    + destruct (Nat.eqb p q) eqn:Q; [reflexivity | exact IH].
Qed.

Lemma row_of_del_gone : forall pri rq, row_of pri (del_row pri rq) = None.
Proof.
  intros pri rq. induction rq as [|(p, r) tl IH]; [reflexivity |].
  cbn [del_row]. destruct (Nat.eqb p pri) eqn:E.
  - exact IH.
  - cbn [row_of]. rewrite E. exact IH.
Qed.

Lemma row_of_del_other : forall q pri rq, Nat.eqb q pri = false ->
    row_of q (del_row pri rq) = row_of q rq.
Proof.
  intros q pri rq E. induction rq as [|(p, r) tl IH].
  - reflexivity.
  - cbn [del_row]. destruct (Nat.eqb p pri) eqn:P; cbn [row_of].
    + apply Nat.eqb_eq in P. subst p. destruct (Nat.eqb pri q) eqn:Q.
      * apply Nat.eqb_eq in Q. rewrite <- Q in E.
        rewrite Nat.eqb_refl in E. discriminate E.
      * exact IH.
    + destruct (Nat.eqb p q) eqn:Q; [reflexivity | exact IH].
Qed.

Lemma del_row_absent : forall pri rq, row_of pri rq = None -> del_row pri rq = rq.
Proof.
  intros pri. induction rq as [|(p, r) tl IH]; [reflexivity |].
  intros H. cbn [del_row]. destruct (Nat.eqb p pri) eqn:E.
  - cbn [row_of] in H. rewrite E in H. cbn [row_of] in H. discriminate H.
  - cbn [row_of] in H. rewrite E in H. cbn [row_of] in H.
    f_equal. apply IH. exact H.
Qed.

(* The workhorse: reading back the row that was just written. *)
Lemma row_at_set_row : forall pri r rq, row_at pri (set_row pri r rq) = r.
Proof.
  intros pri r rq. unfold set_row, row_at.
  destruct (nilp r) eqn:N.
  - rewrite (nilp_true_iff r N), (row_of_del_gone pri rq). reflexivity.
  - rewrite (row_of_put_same pri r rq). reflexivity.
Qed.

Lemma row_at_other_untouched : forall q pri r rq, Nat.eqb q pri = false ->
    row_at q (set_row pri r rq) = row_at q rq.
Proof.
  intros q pri r rq E. unfold set_row, row_at.
  destruct (nilp r) eqn:N.
  - rewrite (row_of_del_other q pri rq E). reflexivity.
  - rewrite (row_of_put_other q pri r rq E). reflexivity.
Qed.

Lemma insert_adds_to_its_row : forall pri tid rq,
    row_at pri (rq_insert pri tid rq) = row_at pri rq ++ [tid].
Proof. intros pri tid rq. apply row_at_set_row. Qed.

Lemma insert_top_adds_to_head : forall pri tid rq,
    row_at pri (rq_insert_top pri tid rq) = tid :: row_at pri rq.
Proof. intros pri tid rq. apply row_at_set_row. Qed.

Lemma insert_other_rows_untouched : forall q pri tid rq, Nat.eqb q pri = false ->
    row_at q (rq_insert pri tid rq) = row_at q rq.
Proof. intros q pri tid rq E. apply (row_at_other_untouched q pri _ rq E). Qed.

Lemma rotate_other_rows_untouched : forall q pri rq, Nat.eqb q pri = false ->
    row_at q (rq_rotate pri rq) = row_at q rq.
Proof. intros q pri rq E. apply (row_at_other_untouched q pri _ rq E). Qed.

Lemma delete_other_rows_untouched : forall q pri tid rq, Nat.eqb q pri = false ->
    row_at q (rq_delete pri tid rq) = row_at q rq.
Proof. intros q pri tid rq E. apply (row_at_other_untouched q pri _ rq E). Qed.

Lemma without_removes : forall t r, memb t (without t r) = false.
Proof.
  intros t r. induction r as [|a l IH]; [reflexivity |].
  unfold memb in *. cbn [without].
  destruct (Nat.eqb a t) eqn:E; cbn [existsb].
  - exact IH.
  - rewrite IH.
    assert (F : Nat.eqb t a = false).
    { apply Nat.eqb_neq. intros N. rewrite N in E.
      rewrite Nat.eqb_refl in E. discriminate E. }
    rewrite F. cbn [orb]. reflexivity.
Qed.

Lemma delete_removes_from_row : forall pri tid rq,
    memb tid (row_at pri (rq_delete pri tid rq)) = false.
Proof.
  intros pri tid rq. unfold rq_delete.
  rewrite row_at_set_row. apply without_removes.
Qed.

Lemma delete_absent_is_identity : forall pri tid rq, row_of pri rq = None ->
    rq_delete pri tid rq = rq.
Proof.
  intros pri tid rq O. unfold rq_delete, set_row, row_at. rewrite O.
  cbn [without nilp]. apply del_row_absent. exact O.
Qed.

Lemma row_at_present : forall pri r rq, row_of pri rq = Some r -> row_at pri rq = r.
Proof. intros pri r rq O. unfold row_at. rewrite O. reflexivity. Qed.

Lemma row_at_absent : forall pri rq, row_of pri rq = None -> row_at pri rq = nil.
Proof. intros pri rq O. unfold row_at. rewrite O. reflexivity. Qed.

(* An empty row is deleted, never stored: ready_queue.h:143 BitClr. *)
Lemma set_row_nonempty : forall pri r rq, nilp r = false ->
    set_row pri r rq = put_row pri r rq.
Proof. intros pri r rq N. unfold set_row. rewrite N. reflexivity. Qed.

Lemma top_pri_empty_row_ignored : forall pri rq,
    top_pri ((pri, nil) :: rq) = top_pri rq.
Proof. intros pri rq. reflexivity. Qed.

Lemma top_pri_of_present_row : forall pri r rq, row_of pri rq = Some r ->
    nilp r = false -> top_pri rq <= pri.
Proof.
  intros pri r. induction rq as [|(p, r0) tl IH]; intros H N.
  - cbn [row_of] in H. discriminate H.
  - cbn [row_of] in H. destruct (Nat.eqb p pri) eqn:E; cbn in H.
    + inversion H. cbn [top_pri]. rewrite N.
      apply Nat.eqb_eq in E. subst p. apply Nat.le_min_l.
    + cbn [top_pri]. destruct (nilp r0) eqn:R.
      * exact (IH H N).
      * apply Nat.le_trans with (m := top_pri tl).
        { apply Nat.le_min_r. }
        { exact (IH H N). }
Qed.

Lemma top_pri_put_row : forall pri r rq, nilp r = false ->
    top_pri (put_row pri r rq) = Nat.min pri (top_pri rq).
Proof.
  intros pri r rq N. destruct r as [|rt rtv]; [discriminate N |].
  induction rq as [|(p, r0) tl IH]; [reflexivity |].
  cbn [put_row]. destruct (Nat.eqb p pri) eqn:P.
  - apply Nat.eqb_eq in P. subst p. cbn [top_pri].
    destruct (nilp r0) eqn:R; cbn [top_pri].
    + reflexivity.
    + rewrite min_min_l. reflexivity.
  - cbn [top_pri]. destruct (nilp r0) eqn:R.
    + exact IH.
    + rewrite IH, min_swap. reflexivity.
Qed.

Lemma top_pri_insert : forall pri tid rq,
    top_pri (rq_insert pri tid rq) = Nat.min pri (top_pri rq).
Proof.
  intros pri tid rq. unfold rq_insert.
  rewrite (set_row_nonempty pri (row_at pri rq ++ [tid]) rq (nilp_app_one _ _)).
  apply top_pri_put_row. exact (nilp_app_one _ _).
Qed.

Lemma top_pri_insert_top : forall pri tid rq,
    top_pri (rq_insert_top pri tid rq) = Nat.min pri (top_pri rq).
Proof.
  intros pri tid rq. unfold rq_insert_top.
  rewrite (set_row_nonempty pri (tid :: row_at pri rq) rq (nilp_cons _ _)).
  apply top_pri_put_row. exact (nilp_cons _ _).
Qed.

Lemma top_pri_rotate : forall pri rq, nilp (row_at pri rq) = false ->
    top_pri (rq_rotate pri rq) = top_pri rq.
Proof.
  intros pri rq N. destruct (row_of pri rq) as [r|] eqn:O.
  - rewrite (row_at_present pri r rq O) in N.
    assert (RR : nilp (rotate_row r) = false).
    { rewrite nilp_rotate. exact N. }
    unfold rq_rotate. rewrite (row_at_present pri r rq O).
    rewrite (set_row_nonempty pri (rotate_row r) rq RR).
    rewrite (top_pri_put_row pri (rotate_row r) rq RR).
    apply Nat.min_r. apply (top_pri_of_present_row pri r rq O N).
  - exfalso. rewrite (row_at_absent pri rq O) in N.
    rewrite nilp_nil in N. discriminate N.
Qed.

(* ready_queue.h:100-104 returns TRUE exactly when it lowered top_priority, and
 * reschedule() consumes that bit.  The bit is the strict comparison, not a
 * test of membership. *)
Lemma insert_changes_top_iff_higher : forall pri tid rq,
    top_pri (rq_insert pri tid rq) =
    if Nat.ltb pri (top_pri rq) then pri else top_pri rq.
Proof.
  intros pri tid rq. rewrite top_pri_insert.
  destruct (Nat.ltb pri (top_pri rq)) eqn:L.
  - apply Nat.ltb_lt in L. apply Nat.min_l. lia.
  - apply Nat.ltb_ge in L. apply Nat.min_r. lia.
Qed.

Lemma sentinel_reads_empty : top_pri nil = num_pri /\ front (mk_rdy nil None) = None.
Proof. split; reflexivity. Qed.

Lemma front_prefers_the_shadow : forall rd t, rd_klock rd = Some t -> front rd = Some t.
Proof. intros rd t H. unfold front. rewrite H. reflexivity. Qed.

Lemma front_without_shadow_needs_a_row : forall rd,
    rd_klock rd = None -> row_of (top_pri (rd_rows rd)) (rd_rows rd) = None ->
    front rd = None.
Proof. intros rd K O. unfold front. rewrite K, O. reflexivity. Qed.

(* What tk_rot_rdq is for, computed: the task that was first in line goes last,
 * so the next dispatch picks the other one.  With a kernel lock set, the shadow
 * outranks the reordering entirely. *)
Lemma rotate_changes_the_dispatch_choice :
    row_at 3 (rq_insert 3 7 (rq_insert 3 8 nil)) = [8; 7] /\
    row_at 3 (rq_rotate 3 (rq_insert 3 7 (rq_insert 3 8 nil))) = [7; 8] /\
    front (mk_rdy (rq_insert 3 7 (rq_insert 3 8 nil)) None) = Some 8 /\
    front (mk_rdy (rq_rotate 3 (rq_insert 3 7 (rq_insert 3 8 nil))) None) = Some 7 /\
    front (mk_rdy (rq_rotate 3 (rq_insert 3 7 (rq_insert 3 8 nil))) (Some 9)) = Some 9.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* ready_queue.h:100-102: a klocked task is appended to its row AND recorded in
 * klocktsk.  The two are not exclusive -- the task is double-booked, and the
 * shadow is what ready_queue_top sees. *)
Definition rdy_insert (pri : nat) (locked : bool) (tid : nat) (rd : rdy) : rdy :=
  mk_rdy (rq_insert pri tid (rd_rows rd)) (if locked then Some tid else rd_klock rd).

Definition klock_after_delete (tid : nat) (k : option nat) : option nat :=
  match k with
  | None => None
  | Some t => if Nat.eqb t tid then None else k
  end.

Definition rdy_delete (pri tid : nat) (rd : rdy) : rdy :=
  mk_rdy (rq_delete pri tid (rd_rows rd)) (klock_after_delete tid (rd_klock rd)).

Lemma rdy_insert_shadow : forall pri t rd, rd_klock (rdy_insert pri true t rd) = Some t.
Proof. reflexivity. Qed.

Lemma rdy_insert_inert_shadow : forall pri t rd,
    rd_klock (rdy_insert pri false t rd) = rd_klock rd.
Proof. reflexivity. Qed.

Lemma klocked_task_is_double_booked : forall pri t rd,
    memb t (row_at pri (rd_rows (rdy_insert pri true t rd))) = true.
Proof.
  intros pri t rd. unfold rdy_insert. cbn [rd_rows].
  rewrite insert_adds_to_its_row. apply memb_app_one.
Qed.

Lemma rdy_delete_clears_own_shadow : forall pri t rq,
    rd_klock (rdy_delete pri t (mk_rdy rq (Some t))) = None.
Proof.
  intros pri t rq. unfold rdy_delete, klock_after_delete. cbn [rd_klock].
  rewrite Nat.eqb_refl. reflexivity. Qed.

Lemma rdy_delete_keeps_other_shadow : forall pri t u rq, Nat.eqb t u = false ->
    rd_klock (rdy_delete pri t (mk_rdy rq (Some u))) = Some u.
Proof.
  intros pri t u rq E. unfold rdy_delete, klock_after_delete. cbn [rd_klock].
  rewrite Nat.eqb_sym, E. reflexivity. Qed.

(* ready_queue.h:33 states the uniqueness of a READY kernel-locked task as a
 * comment, and RDYQUE carries one pointer, so the type allows one.  If the
 * invariant were ever violated the second insert would overwrite klocktsk and
 * the first task would stay queued but unfindable by ready_queue_top. *)
Lemma shadow_holds_one_pointer : forall p1 p2 t1 t2 rd,
    rd_klock (rdy_insert p2 true t2 (rdy_insert p1 true t1 rd)) = Some t2.
Proof. reflexivity. Qed.

Lemma lost_shadow_still_queues : forall p1 p2 t1 t2 rq, Nat.eqb p1 p2 = false ->
    memb t1 (row_at p1 (rq_insert p2 t2 (rq_insert p1 t1 rq))) = true.
Proof.
  intros p1 p2 t1 t2 rq E.
  rewrite (insert_other_rows_untouched p1 p2 t2 _ E).
  rewrite insert_adds_to_its_row. unfold memb. apply memb_app_one.
Qed.

(* task.c:231-241.  ready_queue_delete locates the row through tcb->priority, so
 * the unlink has to come BEFORE the new priority is written -- the source spells
 * this out.  The wrong order unlinks from the row the task is not in, then
 * appends it to the new row, leaving one task queued at two priorities with both
 * bits set. *)
Definition chg_pri_good (old new tid : nat) (rq : rows) : rows :=
  rq_insert new tid (rq_delete old tid rq).

Definition chg_pri_bad (old new tid : nat) (rq : rows) : rows :=
  rq_insert new tid (rq_delete new tid rq).

Lemma chg_pri_bad_keeps_the_old_row : forall old new tid rq,
    Nat.eqb old new = false ->
    memb tid (row_at old (chg_pri_bad old new tid rq)) =
    memb tid (row_at old rq).
Proof.
  intros old new tid rq E. unfold chg_pri_bad.
  rewrite (insert_other_rows_untouched old new tid (rq_delete new tid rq) E).
  rewrite (delete_other_rows_untouched old new tid rq E).
  reflexivity.
Qed.

Lemma chg_pri_bad_double_queues :
    memb 3 (row_at 5 (chg_pri_bad 5 9 3 (rq_insert 5 3 nil))) = true /\
    memb 3 (row_at 9 (chg_pri_bad 5 9 3 (rq_insert 5 3 nil))) = true /\
    memb 3 (row_at 5 (chg_pri_good 5 9 3 (rq_insert 5 3 nil))) = false /\
    memb 3 (row_at 9 (chg_pri_good 5 9 3 (rq_insert 5 3 nil))) = true.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* ── 12. The wait engine: wait specifications, the release matrix ────── *)

(* winfo.h:135-143: WSPEC is the three-field struct tskwait (a UINT wait
 * factor), chg_pri_hook (a pointer to a function taking TCB and INT) and
 * rel_wai_hook (a pointer to a function taking TCB).  A C function pointer is
 * not a datum this model can invoke, so a hook is recorded by whether it is
 * present.  No observation is lost: chg_pri_hook is reached only through
 * gcb_change_priority (wait.c:181-186) and rel_wai_hook only from
 * wait_release_ng and wait_release_tmout (wait.c:60-76); nothing else reads
 * the struct. *)
Record wspec : Set := mk_wspec {
    ws_tskwait : nat;     (* the TTW_* bit *)
    ws_chg_pri : bool;    (* NULL => false *)
    ws_rel_wai : bool
  }.

(* syscall.h:67-87.  One bit per wait reason, but the positions are SPARSE:
 * TTW_FLG is 0x8 and TTW_MBX is 0x40, so 0x10 and 0x20 name nothing and the
 * run of task-event factors starts at 0x10000.  A wait mask can therefore be
 * tested but never ordered -- (ctxtsk->waitmask & TTW_SLP) != 0
 * (task_sync.c:189) is the only legal reading of it. *)
Definition ttw_slp  : nat := 1.
Definition ttw_dly  : nat := 2.
Definition ttw_sem  : nat := 4.
Definition ttw_flg  : nat := 8.
Definition ttw_mbx  : nat := 64.
Definition ttw_mtx  : nat := 128.
Definition ttw_smbf : nat := 256.
Definition ttw_rmbf : nat := 512.
Definition ttw_cal  : nat := 1024.
Definition ttw_acp  : nat := 2048.
Definition ttw_rdv  : nat := 4096.
Definition ttw_mpf  : nat := 8192.
Definition ttw_mpl  : nat := 16384.

Inductive wobj : Set :=
    WO_SLP | WO_DLY | WO_SEM | WO_FLG | WO_MBX | WO_MTX | WO_SMBF
  | WO_RMBF | WO_CAL | WO_ACP | WO_RDV | WO_MPF | WO_MPL.

Definition ttw_of (o : wobj) : nat :=
  match o with
  | WO_SLP  => ttw_slp  | WO_DLY  => ttw_dly  | WO_SEM  => ttw_sem
  | WO_FLG  => ttw_flg  | WO_MBX  => ttw_mbx  | WO_MTX  => ttw_mtx
  | WO_SMBF => ttw_smbf | WO_RMBF => ttw_rmbf | WO_CAL  => ttw_cal
  | WO_ACP  => ttw_acp  | WO_RDV  => ttw_rdv  | WO_MPF  => ttw_mpf
  | WO_MPL  => ttw_mpl
  end.

Lemma ttw_is_a_single_bit : forall o, Nat.land (ttw_of o) (Nat.pred (ttw_of o)) = 0.
Proof. destruct o; vm_compute; reflexivity. Qed.

Lemma ttw_is_nonzero : forall o, Nat.ltb 0 (ttw_of o) = true.
Proof. destruct o; vm_compute; reflexivity. Qed.

Lemma ttw_pairwise_disjoint : forall o1 o2, o1 <> o2 ->
    Nat.land (ttw_of o1) (ttw_of o2) = 0.
Proof.
  intros o1 o2 H. destruct o1, o2;
    try (exfalso; apply H; reflexivity); vm_compute; reflexivity.
Qed.

(* rel_wai is present in exactly the classes whose waiter holds a claim on a
 * finite resource: semaphore (semaphore.c:134-135), variable-size memory pool
 * (mempool.c:418-419), message-buffer SEND wait (messagebuf.c:245-246).
 * Mutex is the single class decided by a third attribute -- the hook exists
 * only for TA_INHERIT (mutex.c:308-310, selected at mutex.c:483-485). *)
Definition rel_claimed (o : wobj) : bool :=
  match o with WO_SEM | WO_MPL | WO_SMBF => true | _ => false end.

Definition rel_of (o : wobj) (inh : bool) : bool :=
  orb (rel_claimed o) (match o with WO_MTX => inh | _ => false end).

Definition wspec_of (o : wobj) (tpri inh : bool) : wspec :=
  mk_wspec (ttw_of o) tpri (rel_of o inh).

(* The 22 shipped literals, each with its source line. *)
Definition w_slp        : wspec := mk_wspec ttw_slp  false false.  (* task_sync.c:167 *)
Definition w_dly        : wspec := mk_wspec ttw_dly  false false.  (* time_calls.c:156 *)
Definition w_mbx_tfifo  : wspec := mk_wspec ttw_mbx  false false.  (* mailbox.c:136 *)
Definition w_mbx_tpri   : wspec := mk_wspec ttw_mbx  true  false.  (* mailbox.c:137 *)
Definition w_sem_tfifo  : wspec := mk_wspec ttw_sem  false true.   (* semaphore.c:134 *)
Definition w_sem_tpri   : wspec := mk_wspec ttw_sem  true  true.   (* semaphore.c:135 *)
Definition w_flg_tfifo  : wspec := mk_wspec ttw_flg  false false.  (* eventflag.c:109 *)
Definition w_flg_tpri   : wspec := mk_wspec ttw_flg  true  false.  (* eventflag.c:110 *)
Definition w_mtx_tfifo  : wspec := mk_wspec ttw_mtx  false false.  (* mutex.c:308 *)
Definition w_mtx_tpri   : wspec := mk_wspec ttw_mtx  true  false.  (* mutex.c:309 *)
Definition w_mtx_inherit: wspec := mk_wspec ttw_mtx  true  true.   (* mutex.c:310 *)
Definition w_mpf_tfifo  : wspec := mk_wspec ttw_mpf  false false.  (* mempfix.c:122 *)
Definition w_mpf_tpri   : wspec := mk_wspec ttw_mpf  true  false.  (* mempfix.c:123 *)
Definition w_smbf_tfifo : wspec := mk_wspec ttw_smbf false true.   (* messagebuf.c:245 *)
Definition w_smbf_tpri  : wspec := mk_wspec ttw_smbf true  true.   (* messagebuf.c:246 *)
Definition w_rmbf       : wspec := mk_wspec ttw_rmbf false false.  (* messagebuf.c:247 *)
Definition w_mpl_tfifo  : wspec := mk_wspec ttw_mpl  false true.   (* mempool.c:418 *)
Definition w_mpl_tpri   : wspec := mk_wspec ttw_mpl  true  true.   (* mempool.c:419 *)
Definition w_cal_tfifo  : wspec := mk_wspec ttw_cal  false false.  (* rendezvous.c:133 *)
Definition w_cal_tpri   : wspec := mk_wspec ttw_cal  true  false.  (* rendezvous.c:134 *)
Definition w_acp        : wspec := mk_wspec ttw_acp  false false.  (* rendezvous.c:135 *)
Definition w_rdv        : wspec := mk_wspec ttw_rdv  false false.  (* rendezvous.c:136 *)

(* MORPHISM (traceability): the whole shipped table is generated by three
 * independent coordinates -- the class, the TA_TPRI attribute, and for mutex
 * alone the TA_INHERIT attribute.  There is no exception to the pattern in the
 * kernel, which is what makes the hook law below a law rather than a list. *)
Lemma shipped_table_is_generated :
  w_slp = wspec_of WO_SLP false false /\
  w_dly = wspec_of WO_DLY false false /\
  w_mbx_tfifo = wspec_of WO_MBX false false /\
  w_mbx_tpri = wspec_of WO_MBX true false /\
  w_sem_tfifo = wspec_of WO_SEM false false /\
  w_sem_tpri = wspec_of WO_SEM true false /\
  w_flg_tfifo = wspec_of WO_FLG false false /\
  w_flg_tpri = wspec_of WO_FLG true false /\
  w_mtx_tfifo = wspec_of WO_MTX false false /\
  w_mtx_tpri = wspec_of WO_MTX true false /\
  w_mtx_inherit = wspec_of WO_MTX true true /\
  w_mpf_tfifo = wspec_of WO_MPF false false /\
  w_mpf_tpri = wspec_of WO_MPF true false /\
  w_smbf_tfifo = wspec_of WO_SMBF false false /\
  w_smbf_tpri = wspec_of WO_SMBF true false /\
  w_rmbf = wspec_of WO_RMBF false false /\
  w_mpl_tfifo = wspec_of WO_MPL false false /\
  w_mpl_tpri = wspec_of WO_MPL true false /\
  w_cal_tfifo = wspec_of WO_CAL false false /\
  w_cal_tpri = wspec_of WO_CAL true false /\
  w_acp = wspec_of WO_ACP false false /\
  w_rdv = wspec_of WO_RDV false false.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* HOOK LAW 1: the priority hook is exactly the TA_TPRI coordinate.  A FIFO
 * wait queue never needs one, because its order does not depend on priority. *)
Lemma hook_law_chg_pri : forall o tpri inh, ws_chg_pri (wspec_of o tpri inh) = tpri.
Proof. intros o tpri inh. reflexivity. Qed.

(* HOOK LAW 2: the abort hook does not depend on the ordering attribute at all.
 * It answers a different question -- does the departing waiter owe the object
 * anything -- and the two variants of every class agree on it. *)
Lemma hook_law_rel_wai : forall o tpri inh, ws_rel_wai (wspec_of o inh tpri) = rel_of o tpri.
Proof. intros o tpri inh. reflexivity. Qed.

Lemma hook_law_rel_wai_order_independent : forall o tpri1 tpri2 inh,
    ws_rel_wai (wspec_of o tpri1 inh) = ws_rel_wai (wspec_of o tpri2 inh).
Proof. intros o tpri1 tpri2 inh. unfold wspec_of. destruct o; reflexivity. Qed.

Lemma hook_law_tskwait : forall o tpri inh, ws_tskwait (wspec_of o tpri inh) = ttw_of o.
Proof. intros o tpri inh. reflexivity. Qed.

(* The classes with neither hook: sleep, delay, mailbox, event flag, the two
 * rendezvous receive sides and the receive side of a message buffer.  A hook
 * here would be a bug: the wait holds nothing. *)
Lemma bare_classes_are_bare : forall inh,
  ws_chg_pri w_slp = false /\ ws_rel_wai w_slp = false /\
  ws_rel_wai w_dly = false /\ ws_rel_wai w_mbx_tpri = false /\
  ws_rel_wai w_flg_tpri = false /\ ws_rel_wai w_mpf_tpri = false /\
  ws_rel_wai w_rmbf = false /\ ws_rel_wai w_acp = false /\
  ws_rel_wai w_rdv = false /\ ws_rel_wai w_cal_tpri = false /\
  ws_rel_wai (wspec_of WO_MTX true inh) = inh.
Proof. intros inh. repeat split; vm_compute; reflexivity. Qed.

(* semaphore.c:88-105 and mempool.c / messagebuf.c share the hook body shape:
 * reorder the departing task if oldpri >= 0, then re-run the head-claim pass.
 * The *_rel_wai hook is literally the same function at oldpri = -1
 * (semaphore.c:126-129, messagebuf.c:237-240), so only the pass runs.  The
 * test is on the signed figure, which is why -1 is the sentinel: *)
Lemma minus_one_skips_the_reorder : Z.leb 0 (Z.opp 1) = false.
Proof. reflexivity. Qed.

Lemma nonneg_arms_the_reorder : forall n : nat, Z.leb 0 (Z.of_nat n) = true.
Proof. intros n. apply Z.leb_le. lia. Qed.

(* mutex.c:290-303 is the exception worth recording: mtx_rel_wai does not call
 * mtx_chg_pri at all, it recomputes the OWNER's inherited priority.  The claim
 * it returns is a priority, not a resource. *)
Lemma mtx_hook_is_claimed : rel_claimed WO_MTX = false.
Proof. reflexivity. Qed.

(* ── 12.2 Entering and leaving the wait state ───────────────────────── *)

(* wait.c:91-104.  Two arms and NO default: any other state is left exactly as
 * it was.  READY gives up the ready bit and takes the wait bit; SUSPEND keeps
 * the suspend bit and adds the wait bit -- TS_WAITSUS = 6 is the join of
 * TS_WAIT = 2 and TS_SUSPEND = 4 (task.h:48-50). *)
Definition make_wait (s : tstat) : tstat :=
  match s with
  | S_READY   => S_WAIT
  | S_SUSPEND => S_WAITSUS
  | _         => s
  end.

(* wait.c:29-36: make_non_wait.  The test is the WORD EQUALITY state == TS_WAIT,
 * not a bit test, so a WAITSUS task (6) takes the else arm and lands on plain
 * TS_SUSPEND.  The suspension coordinate survives; only the wait coordinate is
 * cleared. *)
Definition make_non_wait (s : tstat) : tstat :=
  if stat_eqb s S_WAIT then S_READY else S_SUSPEND.

Lemma waitsus_is_the_join : bits (make_wait S_SUSPEND) = Nat.lor ts_wait ts_suspend.
Proof. reflexivity. Qed.

(* The two are inverses on the reachable domain: a task that waits from READY
 * or SUSPEND and is then released is exactly where it started.  This is the
 * structural fact that makes the wait state a coordinate rather than a copy. *)
Lemma release_undoes_wait :
  make_non_wait (make_wait S_READY) = S_READY /\
  make_non_wait (make_wait S_SUSPEND) = S_SUSPEND.
Proof. split; reflexivity. Qed.

Lemma wait_undoes_release :
  make_wait (make_non_wait S_WAIT) = S_WAIT /\
  make_wait (make_non_wait S_WAITSUS) = S_WAITSUS.
Proof. split; reflexivity. Qed.

(* make_wait is a closure on the wait bit: idempotent for every state,
 * including the ones the C switch leaves alone. *)
Lemma make_wait_idempotent : forall s, make_wait (make_wait s) = make_wait s.
Proof. destruct s; reflexivity. Qed.

(* Releasing a task that is genuinely waiting never dispatches a suspended task
 * and never leaves a wait bit behind. *)
Lemma released_waiter_is_not_waiting : forall s,
    bit_any (bits s) ts_wait = true ->
    bit_any (bits (make_non_wait s)) ts_wait = false /\
    bit_any (bits (make_non_wait s)) ts_suspend = bit_any (bits s) ts_suspend.
Proof.
  intros s H. destruct s; vm_compute in H; try discriminate H.
  - split; vm_compute; reflexivity.
  - split; vm_compute; reflexivity.
Qed.

(* make_non_wait is NOT idempotent.  A double release -- the shape a timeout
 * racing an event would leave behind if the timer cell were not inert -- moves
 * a task from READY to SUSPEND, which no caller asked for.  The kernel avoids
 * it by making the timer path the one that does not unlink twice (12.3). *)
Lemma double_release_suspends : make_non_wait (make_non_wait S_WAIT) = S_SUSPEND.
Proof. reflexivity. Qed.

Lemma make_non_wait_is_idempotent_elsewhere : forall s,
    bit_any (bits s) ts_wait = false ->
    make_non_wait (make_non_wait s) = make_non_wait s.
Proof. intros s H. destruct s; vm_compute in H; try discriminate H; reflexivity. Qed.

(* The C switch's missing default is a total function here; recording what it
 * would do to a state that cannot reach it keeps the hazard visible. *)
Lemma make_wait_leaves_dead_states_alone :
  make_wait S_DORMANT = S_DORMANT /\ make_wait S_NONEXIST = S_NONEXIST /\
  make_wait S_WAITSUS = S_WAITSUS.
Proof. repeat split; reflexivity. Qed.


(* ── 12.3 The release matrix ───────────────────────────────────────── *)

(* wait.c:41-76 plus wait.c:125-133 give five release paths.  They differ in
 * exactly four independent observations, recorded here in source order.  The
 * C spellings below drop the outer paren of a pointer deref, because an
 * asterisk right after one opens a nested comment in this language:
 *   ef_timer   timer_delete of the wtmeb field       wait.c:43
 *   ef_unqueue QueRemove of the tskque field         wait.c:44
 *   ef_hook    the object's rel_wai_hook on the TCB  wait.c:64, wait.c:73
 *   ef_write   the TCB's wercd slot                  wait.c:51,56,66,132
 *              None means the path writes nothing at all. *)
Record effect : Type := mk_eff {
    ef_timer   : bool;
    ef_unqueue : bool;
    ef_hook    : bool;
    ef_write   : option er
  }.

Inductive relkind : Type :=
  | RK_release                     (* wait.c:41-47, the Inline body alone *)
  | RK_ok                          (* wait.c:48-52 *)
  | RK_oke  (e : er)               (* wait.c:54-57 *)
  | RK_ng   (e : er)               (* wait.c:60-67 *)
  | RK_tmout                       (* wait.c:69-76 *)
  | RK_del.                        (* wait.c:125-133, the delete broadcast *)

Definition effect_of (k : relkind) : effect :=
  match k with
  | RK_release   => mk_eff true true false None
  | RK_ok        => mk_eff true true false (Some E_OK)
  | RK_oke e     => mk_eff true true false (Some e)
  | RK_ng e      => mk_eff true true true (Some e)
  | RK_tmout     => mk_eff false true true None
  | RK_del       => mk_eff true true false (Some E_DLT)
  end.

Definition silent_kind (k : relkind) : bool :=
  match k with RK_release | RK_tmout => true | _ => false end.

Definition hooked_kind (k : relkind) : bool :=
  match k with RK_ng _ | RK_tmout => true | _ => false end.

(* Every path unlinks.  There is no release that leaves the TCB linked into an
 * object's wait queue, which is what lets the same TCB be re-queued by the next
 * gcb_make_wait without corrupting the list. *)
Lemma every_release_unqueues : forall k, ef_unqueue (effect_of k) = true.
Proof. destruct k; reflexivity. Qed.

(* The two hooks and the two silent paths are different coordinates: RK_ng is
 * hooked AND writing, RK_tmout is hooked AND silent, RK_del writes and is
 * unhooked.  So "ran the abort hook" and "wrote the caller's slot" are not the
 * same observation, and no release is both silent and unhooked by accident.
 * The option is read with a match rather than with an equation: silent_kind is
 * a boolean, and bool is not Prop here. *)
Lemma classification_of_hooks : forall k, ef_hook (effect_of k) = hooked_kind k.
Proof. destruct k; reflexivity. Qed.

Lemma classification_of_silence : forall k,
    match ef_write (effect_of k) with None => true | Some _ => false end
    = silent_kind k.
Proof. destruct k; reflexivity. Qed.

Lemma timeout_is_the_only_path_that_keeps_its_timer : forall k,
    ef_timer (effect_of k) = false <-> k = RK_tmout.
Proof.
  destruct k; split; intros H; try discriminate H; try reflexivity.
Qed.

(* Two code paths, one observable: the delete broadcast is exactly a release
 * that carries E_DLT, and a grant is exactly a release that carries E_OK.  A
 * caller that ignores the receipt cannot tell deletion from granting at all. *)
Lemma delete_collapses_into_a_written_receipt :
    effect_of RK_del = effect_of (RK_oke E_DLT) /\
    effect_of RK_ok = effect_of (RK_oke E_OK).
Proof. split; reflexivity. Qed.

(* wait_release_ok_ercd has exactly ONE call site in the kernel: task_sync.c:348
 * in _tk_ssig_tsk, where the value handed over is (ER)(tcb->tskevt | evtmsk) --
 * the OR'd task-event bits, not an error at all.  Together with _tk_can_wup
 * (task_sync.c:243-266), which returns a count in the same word, the ercd
 * cursor is a data channel as much as an error channel.  The two uses stay
 * distinguishable because every E_* figure is non-positive. *)
Lemma every_receipt_is_nonpositive : forall e, Z.leb (er_code e) Z0 = true.
Proof. destruct e; vm_compute; reflexivity. Qed.

Lemma a_count_is_not_a_receipt : forall n, 0 < n -> Z.ltb Z0 (Z.of_nat n) = true.
Proof. intros n H. apply Z.ltb_lt. change ((0 < Z.of_nat n)%Z). lia. Qed.

(* ── 12.4 The two-phase write: where a blocked call's receipt comes from ── *)

(* wait.c:166-177 (gcb_make_wait_with_diswai) writes the caller's OWN local
 * through the cursor ctxtsk->wercd BEFORE it decides whether to enqueue:
 *   if (is_diswai(...)) *wercd = E_DISWAI;
 *   else { *wercd = E_TMOUT; if (tmout != TMO_POL) gcb_make_wait(...); }
 * So E_TMOUT is never produced by a release: it is the pre-write that the
 * silent timeout release (RK_tmout) leaves standing.  A poll's E_TMOUT comes
 * from the guard alone, because a poll never enqueues at all. *)
Definition prewrite (diswai : bool) : er := if diswai then E_DISWAI else E_TMOUT.

Definition enqueues (diswai : bool) (t : tmo) : bool := negb diswai && tmo_blocks t.

Lemma poll_never_enqueues : forall d, enqueues d TMO_POLL = false.
Proof. intros d. unfold enqueues, tmo_blocks. destruct d; reflexivity. Qed.

Lemma diswai_never_enqueues : forall t, enqueues true t = false.
Proof. intros t. unfold enqueues. destruct t; reflexivity. Qed.

Lemma enqueues_is_exactly_blocking : forall t, enqueues false t = tmo_blocks t.
Proof. intros t. unfold enqueues. destruct t; reflexivity. Qed.

Definition final_receipt (k : relkind) (pre : er) : er :=
  match ef_write (effect_of k) with Some e => e | None => pre end.

Lemma a_writing_release_overrides_the_prewrite : forall e pre,
    final_receipt (RK_oke e) pre = e /\ final_receipt (RK_ng e) pre = e /\
    final_receipt RK_ok pre = E_OK /\ final_receipt RK_del pre = E_DLT.
Proof. intros e pre. repeat split; reflexivity. Qed.

Lemma a_silent_release_keeps_the_prewrite : forall pre,
    final_receipt RK_tmout pre = pre /\ final_receipt RK_release pre = pre.
Proof. intros pre. split; reflexivity. Qed.

(* The two halves together are the whole reason the timeout path can be silent.
 * Delete either half and the composition breaks: without the pre-write a
 * timed-out wait reports success, and without the silent release a poll's
 * E_TMOUT would be overwritten by whatever the object later decided. *)
Lemma timeout_needs_the_prewrite :
    final_receipt RK_tmout (prewrite false) = E_TMOUT /\
    final_receipt RK_tmout E_OK = E_OK.
Proof. split; reflexivity. Qed.

Lemma diswai_receipt_survives_because_nothing_enqueues :
    prewrite true = E_DISWAI /\ final_receipt RK_release (prewrite true) = E_DISWAI.
Proof. split; reflexivity. Qed.

(* The cursor pair a blocked call leaves behind: the state coordinate and the
 * receipt slot.  A release is a function of the effect and the slot ONLY -- it
 * never consults the object, and the object cannot see whether the slot was
 * pre-written.  This is the InterCore shape exactly: one domain registers a
 * location, the other writes into it, and the roles never swap. *)
Definition wcell : Type := tstat * (option er).

Definition block_cell (t : tstat) (pre : er) : wcell := (make_wait t, Some pre).

Definition release_cell (k : relkind) (c : wcell) : wcell :=
  (make_non_wait (fst c),
   match ef_write (effect_of k) with Some e => Some e | None => snd c end).

Lemma release_cell_state_is_independent_of_the_object : forall k s slot,
    fst (release_cell k (s, slot)) = make_non_wait s.
Proof. intros k s slot. unfold release_cell. cbn [fst]. reflexivity. Qed.

Lemma release_cell_delivers_at_the_registered_slot : forall k s slot,
    snd (release_cell k (s, slot)) =
    match ef_write (effect_of k) with Some e => Some e | None => slot end.
Proof.
  intros k s slot. unfold release_cell. destruct (ef_write (effect_of k)); reflexivity.
Qed.

Lemma timeout_delivers_the_prewrite :
    snd (release_cell RK_tmout (block_cell S_READY (prewrite false))) = Some E_TMOUT.
Proof. vm_compute. reflexivity. Qed.

Lemma grant_delivers_E_OK :
    snd (release_cell RK_ok (block_cell S_READY (prewrite false))) = Some E_OK.
Proof. vm_compute. reflexivity. Qed.

Lemma delete_delivers_E_DLT :
    snd (release_cell RK_del (block_cell S_WAITSUS (prewrite false))) = Some E_DLT.
Proof. vm_compute. reflexivity. Qed.

Lemma blocked_from_ready_or_suspended_never_stays_waiting :
    forall k, bit_any (bits (fst (release_cell k (block_cell S_READY (prewrite false))))) ts_wait = false /\
              bit_any (bits (fst (release_cell k (block_cell S_SUSPEND (prewrite false))))) ts_wait = false /\
              bit_any (bits (fst (release_cell k (block_cell S_READY (prewrite false))))) ts_suspend = false /\
              bit_any (bits (fst (release_cell k (block_cell S_SUSPEND (prewrite false))))) ts_suspend = true.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* ── 13. Mailbox: the frontier rendezvous ───────────────────────── *)

(* mailbox.c:239-241 is the whole interesting part of the send.  Quoted in
 * prose, because a C pointer cast contains the two characters that close a
 * comment in this language: if the wait queue is not empty, take its head as
 * the TCB, assign the message pointer through that TCB's winfo.mbx.ppk_msg,
 * and call wait_release_ok on it.  Otherwise connect the message to the queue.

 * A send does not deposit a message in the object and then wake somebody; it
 * writes the item into a cursor the receiver registered when it blocked
 * (mailbox.c:310) and releases the receiver through the grant path of §12.3.
 * So the mailbox is one queue plus one cursor, and the cursor is the only
 * place the two domains touch.  The write and the release are separate
 * statements in C, so they are separate functions here: the transient state
 * the kernel passes through is observable in the model and named by FR_full. *)
Inductive frontier : Set :=
  | FR_free : frontier                      (* nobody registered at the cursor *)
  | FR_wait : frontier                      (* a receiver is blocked, nothing delivered *)
  | FR_full : nat -> frontier.              (* the sender wrote the item in *)

(* isQueEmpty is a pointer compare (mailbox.c:239), read here off the cursor. *)
Definition has_receiver (f : frontier) : bool :=
  match f with FR_wait | FR_full _ => true | FR_free => false end.

Definition waiting (f : frontier) : bool :=
  match f with FR_wait => true | _ => false end.

Definition has_item (f : frontier) : bool :=
  match f with FR_full _ => true | _ => false end.

Definition item_of (f : frontier) : option nat :=
  match f with FR_full msg => Some msg | _ => None end.

Lemma has_receiver_is_wait_or_full : forall f, has_receiver f = orb (waiting f) (has_item f).
Proof. destruct f; reflexivity. Qed.

Lemma waiting_excludes_item : forall f, andb (waiting f) (has_item f) = false.
Proof. destruct f; reflexivity. Qed.

Lemma item_of_none_when_free : forall f, item_of f = None <-> has_item f = false.
Proof.
  destruct f as [|_|msg]; cbn [item_of has_item]; split;
    intros H; try discriminate H; reflexivity.
Qed.

(* The write, mailbox.c:240.  Only a blocked receiver has a cursor to write
 * into; against a free frontier -- or a cursor already full, which the kernel
 * can never present twice in one critical section -- the step is the identity. *)
Definition snd_write (msg : nat) (f : frontier) : frontier :=
  match f with
  | FR_wait => FR_full msg
  | _       => f
  end.

(* The release, mailbox.c:241 -- wait_release_ok, i.e. RK_ok of §12.3.  The
 * receiver leaves with the item, so the cursor is empty behind it. *)
Definition retire (f : frontier) : frontier :=
  match f with FR_full _ => FR_free | _ => f end.

Lemma retire_empties_a_delivered_cursor : forall msg, retire (snd_write msg FR_wait) = FR_free.
Proof. intros msg. reflexivity. Qed.

Lemma retire_keeps_a_free_cursor : retire FR_free = FR_free.
Proof. reflexivity. Qed.

Lemma retire_is_inert_on_wait : retire FR_wait = FR_wait.
Proof. reflexivity. Qed.

Lemma retire_clears_the_item : forall f, item_of (retire f) = None.
Proof. destruct f; reflexivity. Qed.

(* A retire leaves nobody behind: the receiver that took the item is gone from
 * the cursor, and the only cursor that survives is one still waiting. *)
Lemma retire_leaves_only_a_waiter : forall f, has_receiver (retire f) = waiting f.
Proof. destruct f; reflexivity. Qed.

(* The one object plus the one cursor it owns. *)
Definition mbx_cell : Set := mbx * frontier.

(* mailbox.c:247-253, the FIFO tail attach: nextmsg(tail) = msg; tail = msg. *)
Definition enqueue (q : list nat) (msg : nat) : list nat := q ++ [msg].

(* mailbox.c:244-246, queue_insert_mpri.  The chain is kept in descending
 * msgpri, so the new item goes before the first queued message of strictly
 * lower priority -- which is what makes headmsg() (and therefore tk_rcv_mbx,
 * which only ever looks at the head) return the best available message.
 * pri is the message's own priority: a function, because the model has no
 * message payload to project. *)
Fixpoint insert_mpri (pri : nat -> nat) (msg : nat) (q : list nat) : list nat :=
  match q with
  | nil => msg :: nil
  | m :: rest => if Nat.ltb (pri m) (pri msg)
                 then msg :: q
                 else m :: insert_mpri pri msg rest
  end.

Lemma insert_mpri_length : forall pri msg q,
    length (insert_mpri pri msg q) = S (length q).
Proof.
  intros pri msg q. induction q as [|a rest IH]; cbn [insert_mpri length].
  - reflexivity.
  - destruct (Nat.ltb (pri a) (pri msg)); cbn [length]; [ | rewrite IH]; reflexivity.
Qed.

Lemma insert_mpri_memb : forall pri msg q, memb msg (insert_mpri pri msg q) = true.
Proof.
  intros pri msg q. induction q as [|a rest IH]; cbn [insert_mpri]; unfold memb.
  - cbn [existsb]. rewrite Nat.eqb_refl. reflexivity.
  - destruct (Nat.ltb (pri a) (pri msg)).
    + cbn [existsb]. rewrite Nat.eqb_refl. cbn [orb]. reflexivity.
    + cbn [existsb]. destruct (Nat.eqb msg a); [cbn [orb]; reflexivity | exact IH].
Qed.

(* The frontier of a priority-ordered chain is the best of what is there.  This
 * is why the receive side can afford to look only at headmsg (mailbox.c:302). *)
Lemma insert_mpri_head_is_best : forall pri msg q,
    hd msg (insert_mpri pri msg q) =
    if Nat.ltb (pri (hd msg q)) (pri msg) then msg else hd msg q.
Proof.
  intros pri msg q. destruct q as [|a rest].
  - cbn [insert_mpri hd]. destruct (Nat.ltb (pri msg) (pri msg)); reflexivity.
  - cbn [insert_mpri hd]. destruct (Nat.ltb (pri a) (pri msg)); reflexivity.
Qed.
Lemma enqueue_is_append_of_one : forall q msg, length (enqueue q msg) = S (length q).
Proof. intros q msg. unfold enqueue. rewrite length_app. cbn [length]. lia. Qed.

(* The two chain moves are one function decided by the attribute
 * (mbxcb->mbxatr & TA_MPRI), and the FIFO mailbox is the branch that never
 * reorders: its head is the oldest item, always. *)
Definition chain_move (mpri : bool) (pri : nat -> nat) (msg : nat) (m : mbx) : list nat :=
  if mpri then insert_mpri pri msg (m_chain m) else enqueue (m_chain m) msg.

Lemma chain_move_length : forall mpri pri msg m,
    length (chain_move mpri pri msg m) = S (length (m_chain m)).
Proof.
  intros mpri pri msg m. unfold chain_move. destruct mpri.
  - apply insert_mpri_length.
  - apply enqueue_is_append_of_one.
Qed.

(* wait.c:44 plus mailbox.c:241: the receiver that took the message leaves the
 * wait queue; the object keeps its order. *)
Definition drain_head (m : mbx) : mbx :=
  mk_mbx (m_id m) (m_mpri m) (m_chain m)
         (match m_wait m with nil => nil | _ :: rest => rest end).

Lemma drain_head_keeps_the_chain : forall m, m_chain (drain_head m) = m_chain m.
Proof. intros m. reflexivity. Qed.

Lemma drain_head_shortens_the_wait : forall m,
    length (m_wait (drain_head m)) = Nat.pred (length (m_wait m)).
Proof.
  intros m. unfold drain_head. cbn [m_wait].
  destruct (m_wait m); reflexivity.
Qed.

Lemma drain_head_is_inert_on_an_empty_queue : forall m, m_wait m = nil -> drain_head m = m.
Proof.
  intros m E. unfold drain_head.
  destruct m as [mi mm mc mw]; cbn [m_wait] in E; rewrite E; reflexivity.
Qed.

(* The cascade of _tk_snd_mbx: CHECK_MBXID (mailbox.c:225), the stored marker
 * (:229), and the TA_MPRI attribute test on the message's own priority
 * (:234-236).  The order is the order the receipts are reported in, and the
 * third guard exists only for a priority-ordered mailbox. *)
Definition mbx_snd_guards (st : kst) (mpri : bool) (id msgpri : nat) : list (bool * er) :=
  (chk_id min_mbxid num_mbx id, E_ID) ::
  (mbx_used st (index_of min_mbxid id), E_NOEXS) ::
  (orb (negb mpri) (Nat.ltb 0 msgpri), E_PAR) :: nil.

(* E_PAR on a send is unreachable for a FIFO mailbox: the attribute decides
 * whether the guard can fire at all.  The contrapositive is the load-bearing
 * half -- a receipt of E_PAR is evidence of TA_MPRI, not merely of a bad
 * message. *)
Lemma snd_E_PAR_is_evidence_of_mpri : forall st mpri id msgpri,
    first_bad (mbx_snd_guards st mpri id msgpri) = Some E_PAR ->
    mpri = true /\ Nat.ltb 0 msgpri = false.
Proof.
  intros st mpri id msgpri H. unfold mbx_snd_guards in H.
  destruct (chk_id min_mbxid num_mbx id) eqn:C;
  destruct (mbx_used st (index_of min_mbxid id)) eqn:U;
  destruct mpri eqn:M;
  destruct (Nat.ltb 0 msgpri) eqn:P;
  cbn [first_bad negb orb] in H;
  try discriminate H;
  split; first [reflexivity | assumption].
Qed.

(* msgpri <= 0 is the C test (mailbox.c:235); on the naturals the model can
 * name, that is exactly the figure 0, so the guard is a positivity test and
 * not a range test.  The guard itself is the attribute-conditional third
 * element of the cascade above, factored out so its truth table can be stated
 * without a kernel state. *)
Definition snd_attr_guard (mpri : bool) (msgpri : nat) : bool :=
  orb (negb mpri) (Nat.ltb 0 msgpri).

Lemma fifo_admits_every_priority : forall p, snd_attr_guard false p = true.
Proof. intros p. unfold snd_attr_guard. reflexivity. Qed.

Lemma mpri_admits_only_positive : forall p, snd_attr_guard true p = true <-> 0 < p.
Proof. intros p. unfold snd_attr_guard. apply Nat.ltb_lt. Qed.

(* One figure, two verdicts: the same message priority is refused by a
 * TA_MPRI mailbox and admitted by a FIFO one. *)
Lemma zero_priority_is_refused_only_by_mpri :
    snd_attr_guard true 0 = false /\ snd_attr_guard false 0 = true.
Proof. split; reflexivity. Qed.

(* The send itself, mailbox.c:222-267, as one step on the cell.  The two
 * branches are the two statements of the C if: the rendezvous drains the head
 * of the wait queue and hands the item over the cursor (and the item is
 * returned here so the write stays observable after the cursor is retired),
 * while the queueing branch moves the chain and leaves the cursor alone. *)
Definition mbx_snd (pri : nat -> nat) (msg : nat) (mc : mbx_cell) : mbx_cell * option nat :=
  match mc with
  | (m, FR_wait) => ((drain_head m, retire (snd_write msg FR_wait)), Some msg)
  | (m, f) => ((mk_mbx (m_id m) (m_mpri m) (chain_move (m_mpri m) pri msg m) (m_wait m), f), None)
  end.

(* The send's write and the send's release, read back off the step. *)
Lemma snd_rendezvous_never_queues : forall pri msg m,
    m_chain (fst (fst (mbx_snd pri msg (m, FR_wait)))) = m_chain m.
Proof. intros pri msg m. reflexivity. Qed.

Lemma snd_rendezvous_delivers_the_item : forall pri msg m,
    snd (mbx_snd pri msg (m, FR_wait)) = Some msg.
Proof. intros pri msg m. reflexivity. Qed.

Lemma snd_rendezvous_leaves_an_empty_cursor : forall pri msg m,
    snd (fst (mbx_snd pri msg (m, FR_wait))) = FR_free.
Proof. intros pri msg m. reflexivity. Qed.

Lemma snd_rendezvous_drains_one_receiver : forall pri msg m,
    length (m_wait (fst (fst (mbx_snd pri msg (m, FR_wait)))))
    = Nat.pred (length (m_wait m)).
Proof.
  intros pri msg m. unfold mbx_snd. cbn [fst snd].
  apply drain_head_shortens_the_wait.
Qed.

Lemma snd_writes_through_the_cursor_before_retiring : forall pri msg m,
    item_of (snd_write msg FR_wait) = Some msg /\ item_of (snd (fst (mbx_snd pri msg (m, FR_wait)))) = None.
Proof. split; reflexivity. Qed.

Lemma snd_against_a_free_frontier_queues : forall pri msg m,
    snd (fst (mbx_snd pri msg (m, FR_free))) = FR_free /\
    m_chain (fst (fst (mbx_snd pri msg (m, FR_free)))) = chain_move (m_mpri m) pri msg m.
Proof. split; reflexivity. Qed.

Lemma snd_queues_exactly_one_item : forall pri msg m,
    m_chain (fst (fst (mbx_snd pri msg (m, FR_free)))) = chain_move (m_mpri m) pri msg m /\
    length (m_chain (fst (fst (mbx_snd pri msg (m, FR_free))))) = S (length (m_chain m)).
Proof.
  intros pri msg m. split; [reflexivity | apply chain_move_length].
Qed.

Lemma snd_against_a_free_frontier_delivers_nothing : forall pri msg m,
    snd (mbx_snd pri msg (m, FR_free)) = None.
Proof. intros pri msg m. reflexivity. Qed.

(* The conservation law.  no_stalled says a message only sits in the chain
 * while nobody is waiting at the cursor -- which is exactly the shape of the
 * C if at mailbox.c:239, one branch taken to the exclusion of the other.
 * no_delivered says the transient FR_full cursor is never left standing
 * between two calls, because the write and the release are in the same
 * critical section. *)
Definition no_stalled (mc : mbx_cell) : bool :=
  negb (andb (has_receiver (snd mc)) (negb (nilp (m_chain (fst mc))))).

Definition no_delivered (mc : mbx_cell) : bool := negb (has_item (snd mc)).

Definition mbx_inv (mc : mbx_cell) : bool := andb (no_stalled mc) (no_delivered mc).

(* Each half of the invariant can be refuted from the shape of the cell alone,
 * which is what makes the impossible cases of the two preservation proofs
 * decidable without looking at the chain. *)
Lemma full_cursor_is_never_wellformed : forall m x, mbx_inv (m, FR_full x) = false.
Proof.
  intros m x. unfold mbx_inv, no_delivered, has_item.
  destruct (no_stalled (m, FR_full x)); reflexivity.
Qed.

Lemma waiting_with_a_message_is_never_wellformed : forall mi mm mw msg rest,
    mbx_inv (mk_mbx mi mm (msg :: rest) mw, FR_wait) = false.
Proof.
  intros mi mm mw msg rest. unfold mbx_inv, no_stalled, no_delivered.
  cbn [m_chain nilp has_receiver has_item negb andb]. reflexivity.
Qed.

Lemma mbx_snd_preserves_the_invariant : forall pri msg mc,
    mbx_inv mc = true -> mbx_inv (fst (mbx_snd pri msg mc)) = true.
Proof.
  intros pri msg mc H. destruct mc as [m f]. destruct f as [| |x].
  - reflexivity.
  - reflexivity.
  - rewrite (full_cursor_is_never_wellformed m x) in H. discriminate H.
Qed.

(* The receive, mailbox.c:279-317.  CHECK_MBXID (E_ID) at :284, CHECK_TMOUT at
 * :285, the stored marker (E_NOEXS) inside the critical section, and
 * CHECK_DISPATCH (E_CTX) at :286.  That last macro is the unconditional form
 * of check.h:249-253, not the TMO_POL-exempt form of check.h:254-258: here a
 * dispatch-disabled context refuses even a poll.  The time-out is therefore
 * not an input to the context guard at all, which is the difference between
 * this service and the message ring of §15. *)
Definition mbx_rcv_guards (st : kst) (id : nat) (t : tmo) : list (bool * er) :=
  (chk_id min_mbxid num_mbx id, E_ID) ::
  (mbx_used st (index_of min_mbxid id), E_NOEXS) ::
  (negb (b_ddsp st), E_CTX) :: nil.

Lemma rcv_E_CTX_is_a_context_refusal : forall st id t,
    first_bad (mbx_rcv_guards st id t) = Some E_CTX ->
    chk_id min_mbxid num_mbx id = true /\ mbx_used st (index_of min_mbxid id) = true /\
    b_ddsp st = true.
Proof.
  intros st id t H. unfold mbx_rcv_guards in H.
  set (c := chk_id min_mbxid num_mbx id) in *.
  set (u := mbx_used st (index_of min_mbxid id)) in *.
  set (d := b_ddsp st) in *.
  destruct c; destruct u; destruct d;
  cbn [first_bad negb] in H;
  try discriminate H;
  repeat split; reflexivity.
Qed.

(* The guard list never reads the time-out argument, so two calls that differ
 * only in their timeout receive identically -- the shape of the C is the
 * theorem, and it is what makes the exempt form a different service. *)
Lemma the_mailbox_context_guard_ignores_the_time_out : forall st id t1 t2,
    mbx_rcv_guards st id t1 = mbx_rcv_guards st id t2.
Proof. intros st id t1 t2. reflexivity. Qed.

(* check.h:254-258, the exempt form.  messagebuf.c:372 is its only user in the
 * shipped kernel, and §3's positive_test_misclassifies_fevr applies to it: the
 * exemption is the sentinel test, not a sign test. *)
Definition ddsp_pol_guard (ddsp : bool) (t : tmo) : bool :=
  orb (negb ddsp) (negb (tmo_blocks t)).

Lemma a_poll_passes_the_exempt_guard : forall ddsp,
    ddsp_pol_guard ddsp TMO_POLL = true.
Proof. intros ddsp. unfold ddsp_pol_guard, tmo_blocks. destruct ddsp; reflexivity. Qed.

Lemma fevr_is_no_escape_from_the_exempt_guard : ddsp_pol_guard true TMO_FEVR = false.
Proof. reflexivity. Qed.

Lemma the_exemption_reaches_only_a_poll : forall ddsp,
    ddsp_pol_guard ddsp TMO_POLL = true /\
    ddsp_pol_guard ddsp TMO_REL = negb ddsp /\
    ddsp_pol_guard ddsp TMO_FEVR = negb ddsp.
Proof.
  intros ddsp. unfold ddsp_pol_guard, tmo_blocks.
  destruct ddsp; repeat split; reflexivity.
Qed.

Lemma rcv_guards_admit_a_blocking_wait_only_alone : forall st id t,
    b_ddsp st = false -> first_bad (mbx_rcv_guards st id t) = None <->
    chk_id min_mbxid num_mbx id = true /\ mbx_used st (index_of min_mbxid id) = true.
Proof.
  intros st id t I. unfold mbx_rcv_guards.
  destruct (chk_id min_mbxid num_mbx id);
  destruct (mbx_used st (index_of min_mbxid id)); rewrite I; cbn [first_bad negb];
  split; intros H.
  - split; reflexivity.
  - reflexivity.
  - discriminate H.
  - destruct H; discriminate.
  - discriminate H.
  - destruct H; discriminate.
  - discriminate H.
  - destruct H; discriminate.
Qed.

(* The receiver's own local, as an outcome record.  The head-take at
 * mailbox.c:302-304 writes it directly; a blocked call leaves it held by the
 * cursor and a later sender writes it from the other side (mailbox.c:240).
 * Reading the two as one observable is the point of caller_item. *)
Record rcv_res : Type := mk_rcv_res {
    rr_cell : mbx_cell;
    rr_item : option nat;
    rr_ercd : er
  }.

Definition caller_item (res : rcv_res) : option nat :=
  match rr_item res with
  | Some msg => Some msg
  | None => item_of (snd (rr_cell res))
  end.

Definition mbx_rcv (diswai : bool) (t : tmo) (mc : mbx_cell) : rcv_res :=
  match mc with
  | (m, f) => match m_chain m with
              | msg :: rest =>
                  mk_rcv_res (mk_mbx (m_id m) (m_mpri m) rest (m_wait m), f) (Some msg) E_OK
              | nil =>
                  if enqueues diswai t
                  then mk_rcv_res (m, FR_wait) None (prewrite diswai)
                  else mk_rcv_res (m, f) None (prewrite diswai)
              end
  end.

Lemma rcv_take_is_E_OK : forall diswai t msg rest m f,
    rr_ercd (mbx_rcv diswai t (mk_mbx (m_id m) (m_mpri m) (msg :: rest) (m_wait m), f)) = E_OK.
Proof. intros diswai t msg rest m f. reflexivity. Qed.

Lemma rcv_take_returns_the_head : forall diswai t msg rest m f,
    caller_item (mbx_rcv diswai t (mk_mbx (m_id m) (m_mpri m) (msg :: rest) (m_wait m), f))
    = Some msg.
Proof. intros diswai t msg rest m f. reflexivity. Qed.

Lemma rcv_take_shortens_the_chain : forall diswai t msg rest m f,
    m_chain (fst (rr_cell (mbx_rcv diswai t (mk_mbx (m_id m) (m_mpri m) (msg :: rest) (m_wait m), f))))
    = rest.
Proof. intros diswai t msg rest m f. reflexivity. Qed.

Lemma rcv_take_leaves_the_wait_queue_alone : forall diswai t msg rest m f,
    m_wait (fst (rr_cell (mbx_rcv diswai t (mk_mbx (m_id m) (m_mpri m) (msg :: rest) (m_wait m), f))))
    = m_wait m.
Proof. intros diswai t msg rest m f. reflexivity. Qed.

(* The receipt of a call that did not take a message is the pre-write of
 * §12.4 in both cases, whether or not the call went to sleep: polling and
 * blocking differ in the cursor, not in the figure. *)
Lemma rcv_without_a_message_pre_writes : forall diswai t m f,
    m_chain m = nil ->
    rr_ercd (mbx_rcv diswai t (m, f)) = prewrite diswai.
Proof.
  intros diswai t m f E. unfold mbx_rcv. rewrite E.
  destruct (enqueues diswai t); reflexivity.
Qed.

Lemma poll_registers_nobody : forall m f,
    m_chain m = nil -> rr_cell (mbx_rcv false TMO_POLL (m, f)) = (m, f).
Proof.
  intros m f E. unfold mbx_rcv. rewrite E, poll_never_enqueues. reflexivity.
Qed.

Lemma diswai_registers_nobody : forall t m f,
    m_chain m = nil -> rr_cell (mbx_rcv true t (m, f)) = (m, f).
Proof.
  intros t m f E. unfold mbx_rcv. rewrite E, diswai_never_enqueues. reflexivity.
Qed.

Lemma blocking_rcv_registers_the_cursor : forall m f,
    m_chain m = nil -> rr_cell (mbx_rcv false TMO_REL (m, f)) = (m, FR_wait).
Proof.
  intros m f E. unfold mbx_rcv. rewrite E, enqueues_is_exactly_blocking.
  cbn [tmo_blocks]. reflexivity.
Qed.

Lemma registered_rcv_delivers_nothing_yet : forall m,
    m_chain m = nil -> caller_item (mbx_rcv false TMO_REL (m, FR_free)) = None.
Proof.
  intros m E. unfold caller_item, mbx_rcv. rewrite E, enqueues_is_exactly_blocking.
  cbn [tmo_blocks]. reflexivity.
Qed.

(* The invariant says in particular that a well-formed mailbox is never
 * sitting on a half-delivered item: the transient cursor of §13 exists only
 * inside one send. *)
Lemma mbx_inv_has_no_item : forall mc, mbx_inv mc = true -> has_item (snd mc) = false.
Proof.
  intros mc H. unfold mbx_inv, no_delivered in H.
  apply andb_true_iff in H. destruct H as [_ H2].
  apply negb_true_iff in H2. exact H2.
Qed.

Lemma rcv_never_stalls_a_delivered_cursor : forall diswai t mc,
    mbx_inv mc = true -> has_item (snd (rr_cell (mbx_rcv diswai t mc))) = false.
Proof.
  intros diswai t mc H. destruct mc as [m f].
  assert (N : has_item f = false) by (apply (mbx_inv_has_no_item (m, f)); exact H).
  unfold mbx_rcv. destruct (m_chain m) as [|msg rest].
  - destruct (enqueues diswai t); cbn [rr_cell snd];
      first [ reflexivity | exact N ].
  - cbn [rr_cell snd]. exact N.
Qed.

(* The conservation law survives a receive: the take shrinks the chain, and the
 * only cursor a receive can install is FR_wait against an empty chain. *)
Lemma mbx_rcv_preserves_the_invariant : forall diswai t mc,
    mbx_inv mc = true -> mbx_inv (rr_cell (mbx_rcv diswai t mc)) = true.
Proof.
  intros diswai t mc H. destruct mc as [m f]. destruct m as [mi mm mch mw].
  destruct f as [| |x].
  - unfold mbx_rcv; cbn [m_chain]; destruct mch as [|msg rest].
    + destruct (enqueues diswai t); cbn [rr_cell]; reflexivity.
    + cbn [rr_cell]; reflexivity.
  - destruct mch as [|msg rest].
    + unfold mbx_rcv; cbn [m_chain]; destruct (enqueues diswai t);
      cbn [rr_cell]; reflexivity.
    + rewrite (waiting_with_a_message_is_never_wellformed mi mm mw msg rest) in H.
      discriminate H.
  - rewrite (full_cursor_is_never_wellformed (mk_mbx mi mm mch mw) x) in H.
    discriminate H.
Qed.

(* THE RENDEZVOUS.  A receiver blocks on an empty mailbox, a sender arrives,
 * and the item lands in the cursor the receiver registered -- with the chain
 * never having grown and one receiver drained from the object.  This is the
 * InterCore send/receive pair read in T-Kernel's own vocabulary, and it is the
 * reason a mailbox needs no buffer of its own on the awake-receiver path. *)
Record rdv : Type := mk_rdv {
    rd_delivered : option nat;      (* written into the cursor by a sender *)
    rd_taken     : option nat;      (* taken out of the chain by the receiver *)
    rd_chain     : list nat;        (* the chain afterwards *)
    rd_wait      : list nat         (* receivers still queued *)
  }.

Definition full_rendezvous (pri : nat -> nat) (msg : nat) (m : mbx) : rdv :=
  let res := mbx_rcv false TMO_REL (m, FR_free) in
  let (mc, d) := mbx_snd pri msg (rr_cell res) in
  mk_rdv d None (m_chain (fst mc)) (m_wait (fst mc)).

Lemma full_rendezvous_delivers_to_the_cursor : forall pri msg m,
    m_chain m = nil -> m_wait m = nil ->
    rd_delivered (full_rendezvous pri msg m) = Some msg.
Proof.
  intros pri msg m E W. unfold full_rendezvous, mbx_rcv.
  destruct m as [mi mm mc mw]; cbn [m_chain m_wait] in E, W; rewrite E, W.
  cbn [m_chain m_wait]. rewrite enqueues_is_exactly_blocking. cbn. reflexivity.
Qed.

Lemma rendezvous_leaves_no_message_behind : forall pri msg m,
    m_chain m = nil -> m_wait m = nil ->
    rd_chain (full_rendezvous pri msg m) = nil /\ rd_wait (full_rendezvous pri msg m) = nil.
Proof.
  intros pri msg m E W. unfold full_rendezvous, mbx_rcv.
  destruct m as [mi mm mc mw]; cbn [m_chain m_wait] in E, W; rewrite E, W.
  cbn [m_chain m_wait]. rewrite enqueues_is_exactly_blocking. cbn. split; reflexivity.
Qed.

Lemma full_rendezvous_drains_the_receiver : forall pri msg m,
    m_chain m = nil -> m_wait m = nil ->
    rd_wait (full_rendezvous pri msg m) = nil.
Proof.
  intros pri msg m E W. unfold full_rendezvous, mbx_rcv.
  destruct m as [mi mm mc mw]; cbn [m_chain m_wait] in E, W; rewrite E, W.
  cbn [m_chain m_wait]. rewrite enqueues_is_exactly_blocking. cbn. reflexivity.
Qed.

(* The other ordering, for contrast: a send that finds nobody queues, and the
 * receive that follows takes the item out of the object instead of meeting a
 * sender at the cursor.  The two paths deliver the same message and differ
 * only in whether the chain ever held it. *)
Definition queue_then_take (pri : nat -> nat) (msg : nat) (m : mbx) : rdv :=
  let (mc, d) := mbx_snd pri msg (m, FR_free) in
  let res := mbx_rcv false TMO_POLL mc in
  mk_rdv d (caller_item res) (m_chain (fst (rr_cell res))) (m_wait (fst (rr_cell res))).

Lemma take_delivers_once_and_empties_the_chain : forall pri msg m,
    m_chain m = nil -> m_mpri m = false ->
    rd_delivered (queue_then_take pri msg m) = None /\
    rd_taken (queue_then_take pri msg m) = Some msg /\
    rd_chain (queue_then_take pri msg m) = nil.
Proof.
  intros pri msg m E M. destruct m as [mi mm mc mw].
  cbn [m_chain m_mpri] in E, M. rewrite E, M.
  unfold queue_then_take, mbx_snd, mbx_rcv, chain_move, enqueue, caller_item. cbn.
  repeat split; reflexivity.
Qed.

(* One item per round trip, on either path, and it is the item that was sent.
 * The channels are different -- the receiver's own local on the take, the
 * registered cursor on the rendezvous -- but the accounting is the same, which
 * is the conservation property the frontier was introduced to express. *)
(* The rendezvous path never takes from the chain, so its own local records no
 * take.  Stated on the shipped shape: the let-bound cursor of a blocked
 * receiver is what makes the projection reduce at all. *)
Lemma rendezvous_records_no_take : forall pri msg m,
    m_chain m = nil -> m_wait m = nil ->
    rd_taken (full_rendezvous pri msg m) = None.
Proof.
  intros pri msg m E W. unfold full_rendezvous, mbx_rcv.
  destruct m as [mi mm mc mw]; cbn [m_chain m_wait] in E, W; rewrite E, W.
  cbn. reflexivity.
Qed.

Lemma one_item_per_path : forall pri msg m,
    m_chain m = nil -> m_wait m = nil -> m_mpri m = false ->
    rd_delivered (full_rendezvous pri msg m) = Some msg /\
    rd_taken (queue_then_take pri msg m) = Some msg /\
    rd_taken (full_rendezvous pri msg m) = None /\
    rd_delivered (queue_then_take pri msg m) = None.
Proof.
  intros pri msg m E W M.
  destruct (take_delivers_once_and_empties_the_chain pri msg m E M) as [T1 [T2 _]].
  split; [ apply full_rendezvous_delivers_to_the_cursor; assumption
         | split; [ exact T2
                  | split; [ apply (rendezvous_records_no_take pri msg m E W)
                           | exact T1 ] ] ].
Qed.

(* A priority-ordered chain is walked in descending msgpri, so the head -- the
 * only position tk_rcv_mbx looks at (mailbox.c:302) -- is the best message
 * available.  Computed on the shipped shape rather than asserted. *)
Example mpri_chain_is_ordered_at_the_frontier :
    chain_move true (fun n => n) 7 (mk_mbx 1 true [5] nil) = [7; 5] /\
    chain_move true (fun n => n) 3 (mk_mbx 1 true [5] nil) = [5; 3] /\
    chain_move false (fun n => n) 7 (mk_mbx 1 false [5] nil) = [5; 7].
Proof. repeat split; reflexivity. Qed.

(* wait.c:125-133 is the delete broadcast: every receiver at the object leaves
 * through wait_release_ng with E_DLT.  Read against §12.4 the receipt is the
 * composition of the pre-write and the writing release, which is what makes
 * E_DLT override the E_TMOUT the blocked call parked in its own slot. *)
Definition mbx_broadcast (m : mbx) : list er :=
  map (fun _ => final_receipt RK_del (prewrite false)) (m_wait m).

Lemma broadcast_is_all_E_DLT : forall m, mbx_broadcast m = repeat E_DLT (length (m_wait m)).
Proof.
  intros m. unfold mbx_broadcast.
  assert (A : forall l : list nat, map (fun _ => final_receipt RK_del (prewrite false)) l
                                    = repeat E_DLT (length l)).
  { induction l as [|x l IH]; cbn [map repeat length]; [reflexivity |].
    cbn [final_receipt effect_of ef_write prewrite] in *. rewrite IH. reflexivity. }
  apply A.
Qed.


