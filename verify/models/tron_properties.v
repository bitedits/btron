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

(* config.h:32-37 (semaphore), :48-53 (event flag), :56-61 (mailbox), :64-69
 * (message buffer): the object families the API reaches, each with the same
 * MIN/MAX/INDEX/ID quartet.  Every ID is an affine image of a table index, and
 * that is the whole ID story -- the same morphism, family by family. *)
Definition min_mbxid : nat := 1.          (* config.h:56 *)
Definition num_mbx : nat := 16.           (* oracle geometry for max_mbxid *)
Definition min_semid : nat := 1.          (* config.h:32 *)
Definition num_sem : nat := 16.           (* oracle geometry for max_semid *)
Definition min_flgid : nat := 1.          (* config.h:48 *)
Definition num_flg : nat := 16.           (* oracle geometry for max_flgid *)
Definition min_mbfid : nat := 1.          (* config.h:64 *)
Definition num_mbf : nat := 16.           (* oracle geometry for max_mbfid *)
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

(* The contrapositive the services below use: a refusal can only name a code
 * the cascade itself carries, so an unlisted receipt is unreachable by
 * construction rather than by inspection of each guard. *)
Lemma first_bad_names_a_listed_receipt : forall gs e,
    first_bad gs = Some e -> In e (map snd gs).
Proof.
  induction gs as [|[p e0] l IH].
  - intros e H. discriminate H.
  - destruct p.
    + cbn [first_bad map]. intros e H. right. apply IH. exact H.
    + cbn [first_bad map]. intros e H. injection H. intros E. subst e0.
      left. reflexivity.
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


(* ── 14. Semaphore: the count, and the walk that spends it ──────── *)

(* semaphore.c is small enough to read as one sentence.  A semaphore is a count
 * with a ceiling, a queue of callers each of which named the number of units it
 * needs (TCB.winfo.sem.cnt), and two walks over that queue: the one a signal
 * runs as it hands units back (semaphore.c:245-265), and the one a released
 * waiter's departure runs (semaphore.c:99-120).  They are the same walk.  The
 * only difference is what happens to a caller the count cannot satisfy: a
 * TA_CNT semaphore steps over it and keeps going, a FIFO semaphore stops.
 * Everything in this section is built out of that one asymmetry. *)

(* A waiter is not an ID: the number it asked for is part of its identity in the
 * queue, because the walk subtracts it.  semaphore.c:310 writes exactly these
 * three things (the TCB, cnt, and the priority the queue is ordered by). *)
Record sem_who : Type := mk_who {
    w_tid  : nat;                 (* TCB.tskid *)
    w_need : nat;                 (* TCB.winfo.sem.cnt *)
    w_pri  : nat                  (* TCB.priority, the TPRI key *)
  }.

(* The units a set of waiters would take. *)
Fixpoint qsum (q : list sem_who) : nat :=
  match q with
  | nil => 0
  | x :: rest => w_need x + qsum rest
  end.

(* CHECK_PAR(cnt > 0) at the two entry points (semaphore.c:224, :287) is what
 * makes every queued need strictly positive; the model keeps that as a boolean
 * so the walk laws can be stated without a hypothesis about each element. *)
Definition every_needs (q : list sem_who) : bool :=
  forallb (fun x => Nat.ltb 0 (w_need x)) q.

(* The walk's state: what is left to hand out, who stays, who leaves.  Three
 * separate answers because the kernel's loop mutates one variable (semcnt) and
 * two list positions as it goes; splitting them is what lets the laws below say
 * anything about conservation. *)
Record drain : Type := mk_drain {
    d_left : nat;
    d_kept : list sem_who;
    d_gone : list sem_who
  }.

Definition drain_skip (x : sem_who) (r : drain) : drain :=
  mk_drain (d_left r) (x :: d_kept r) (d_gone r).

Definition drain_take (x : sem_who) (r : drain) : drain :=
  mk_drain (d_left r) (d_kept r) (x :: d_gone r).

(* semaphore.c:245-265 read as a fold.  gran is TA_CNT.  At each entry: if the
 * count cannot satisfy the need, the FIFO walk breaks (the rest of the queue is
 * kept, nothing further is released) and the granular walk steps over;
 * otherwise release the entry and subtract its need.  The kernel's closing
 * "if (semcb->semcnt <= 0) break" (semaphore.c:262) needs no counterpart here:
 * every need is positive, so a zero balance already makes the next test fail. *)
Fixpoint sig_walk (gran : bool) (count : nat) (q : list sem_who) : drain :=
  match q with
  | nil => mk_drain count nil nil
  | x :: rest =>
      if Nat.ltb count (w_need x) then
        if gran then drain_skip x (sig_walk gran count rest)
        else mk_drain count (x :: rest) nil
      else drain_take x (sig_walk gran (Nat.sub count (w_need x)) rest)
  end.

Lemma ltb_false_ge : forall a b, Nat.ltb a b = false -> b <= a.
Proof. intros a b H. apply Nat.ltb_ge. exact H. Qed.

Lemma sig_walk_nil : forall gran count, sig_walk gran count nil = mk_drain count nil nil.
Proof. intros gran count. reflexivity. Qed.

Lemma sig_walk_unsat_fifo : forall count x rest,
    Nat.ltb count (w_need x) = true ->
    sig_walk false count (x :: rest) = mk_drain count (x :: rest) nil.
Proof. intros count x rest N. unfold sig_walk. rewrite N. reflexivity. Qed.

Lemma sig_walk_unsat_gran : forall count x rest,
    Nat.ltb count (w_need x) = true ->
    sig_walk true count (x :: rest) = drain_skip x (sig_walk true count rest).
Proof. intros count x rest N. unfold sig_walk. rewrite N. reflexivity. Qed.

Lemma sig_walk_sat_gone : forall gran count x rest,
    Nat.ltb count (w_need x) = false ->
    d_gone (sig_walk gran count (x :: rest)) = x :: d_gone (sig_walk gran (Nat.sub count (w_need x)) rest).
Proof. intros gran count x rest N. unfold sig_walk. rewrite N. reflexivity. Qed.

Lemma sig_walk_sat_left : forall gran count x rest,
    Nat.ltb count (w_need x) = false ->
    d_left (sig_walk gran count (x :: rest)) = d_left (sig_walk gran (Nat.sub count (w_need x)) rest).
Proof. intros gran count x rest N. unfold sig_walk. rewrite N. reflexivity. Qed.

Lemma sig_walk_sat_kept : forall gran count x rest,
    Nat.ltb count (w_need x) = false ->
    d_kept (sig_walk gran count (x :: rest)) = d_kept (sig_walk gran (Nat.sub count (w_need x)) rest).
Proof. intros gran count x rest N. unfold sig_walk. rewrite N. reflexivity. Qed.

(* The units are neither created nor lost by the walk: what the signal put in is
 * exactly what stayed plus what the released waiters took out.  This is the
 * statement the kernel relies on when it decrements semcnt in place. *)
Lemma sig_walk_conserves : forall gran count q,
    count = d_left (sig_walk gran count q) + qsum (d_gone (sig_walk gran count q)).
Proof.
  intros gran count q. revert gran count.
  induction q as [|x rest IH]; intros gran count.
  - unfold sig_walk. cbn [d_left d_gone qsum]. lia.
  - destruct (Nat.ltb count (w_need x)) eqn:N.
    + destruct gran.
      * rewrite sig_walk_unsat_gran; [ | exact N]. specialize (IH true count).
        cbn [drain_skip d_left d_gone qsum]. lia.
      * rewrite sig_walk_unsat_fifo; [ | exact N]. specialize (IH false count).
        cbn [d_left d_gone qsum]. lia.
    + rewrite sig_walk_sat_gone; [ | exact N].
      rewrite sig_walk_sat_left; [ | exact N].
      assert (L : w_need x <= count). { apply ltb_false_ge. exact N. }
      specialize (IH gran (Nat.sub count (w_need x))).
      cbn [qsum]. lia.
Qed.

(* And the walk is a partition of the queue: every waiter is either still
 * waiting or released, exactly once.  A "step over" (TA_CNT) keeps the count
 * right; this says it also keeps the people right. *)
Lemma sig_walk_partitions : forall gran count q,
    length q = length (d_kept (sig_walk gran count q)) + length (d_gone (sig_walk gran count q)).
Proof.
  intros gran count q. revert gran count.
  induction q as [|x rest IH]; intros gran count.
  - unfold sig_walk. cbn [d_kept d_gone length]. reflexivity.
  - destruct (Nat.ltb count (w_need x)) eqn:N.
    + destruct gran.
      * rewrite sig_walk_unsat_gran; [ | exact N]. specialize (IH true count).
        cbn [drain_skip d_kept d_gone length]. lia.
      * rewrite sig_walk_unsat_fifo; [ | exact N]. specialize (IH false count).
        cbn [d_kept d_gone length]. lia.
    + rewrite sig_walk_sat_gone; [ | exact N].
      rewrite sig_walk_sat_kept; [ | exact N].
      specialize (IH gran (Nat.sub count (w_need x))).
      cbn [length]. lia.
Qed.

(* The FIFO shape, as a theorem rather than a comment: the released set is a
 * prefix, so a waiter can never be passed over.  This is what "sequential-
 * order wait queue" (T-Kernel 2.0 §7.4.2) buys, and what TA_CNT gives up. *)
Lemma fifo_drains_a_prefix : forall count q,
    exists s, q = d_gone (sig_walk false count q) ++ s /\
              d_kept (sig_walk false count q) = s.
Proof.
  intros count q. revert count.
  induction q as [|x rest IH]; intros count.
  - exists nil. unfold sig_walk. cbn [app]. split; reflexivity.
  - destruct (Nat.ltb count (w_need x)) eqn:N.
    + exists (x :: rest). rewrite (sig_walk_unsat_fifo count x rest N).
      cbn [app]. split; reflexivity.
    + rewrite (sig_walk_sat_gone false count x rest N).
      assert (L : w_need x <= count). { apply ltb_false_ge. exact N. }
      destruct (IH (Nat.sub count (w_need x))) as [s [H1 H2]].
      exists s. split.
      * cbn [app]. rewrite <- H1. reflexivity.
      * rewrite (sig_walk_sat_kept false count x rest N). exact H2.
Qed.

Lemma gran_releases_at_least_fifo : forall count q,
    length (d_gone (sig_walk false count q)) <= length (d_gone (sig_walk true count q)).
Proof.
  intros count q. revert count.
  induction q as [|x rest IH]; intros count.
  - unfold sig_walk. cbn [d_gone length]. lia.
  - destruct (Nat.ltb count (w_need x)) eqn:N.
    + rewrite (sig_walk_unsat_fifo count x rest N).
      cbn [sig_walk drain_skip d_gone length]. apply Nat.le_0_l.
    + assert (L : w_need x <= count). { apply ltb_false_ge. exact N. }
      rewrite (sig_walk_sat_gone false count x rest N), (sig_walk_sat_gone true count x rest N).
      cbn [length]. specialize (IH (Nat.sub count (w_need x))). lia.
Qed.

(* A signal of zero units walks nobody: with every need positive, each test
 * fails, and a failing test either stops a FIFO walk or steps over in a
 * granular one.  CHECK_PAR(cnt > 0) is therefore not politeness -- the only way
 * to run the drain walk without adding units is §14.6's rel_wai path. *)
Lemma sig_walk_zero_keeps_everyone : forall gran q,
    every_needs q = true -> sig_walk gran 0 q = mk_drain 0 q nil.
Proof.
  intros gran q. revert gran.
  induction q as [|x rest IH]; intros gran H.
  - reflexivity.
  - cbn [every_needs forallb] in H. apply andb_true_iff in H. destruct H as [Nx Hrest].
    destruct gran.
    + rewrite (sig_walk_unsat_gran 0 x rest Nx). rewrite (IH true Hrest).
      cbn [drain_skip]. reflexivity.
    + rewrite (sig_walk_unsat_fifo 0 x rest Nx). reflexivity.
Qed.

Lemma a_zero_signal_releases_nobody : forall gran q,
    every_needs q = true -> d_gone (sig_walk gran 0 q) = nil.
Proof.
  intros gran q H. rewrite (sig_walk_zero_keeps_everyone gran q H). reflexivity.
Qed.

Example granular_grants_out_of_turn :
    d_gone (sig_walk true 2 [mk_who 7 3 1; mk_who 8 1 2]) = [mk_who 8 1 2].
Proof. reflexivity. Qed.

Example fifo_refuses_to_grant_out_of_turn :
    d_gone (sig_walk false 2 [mk_who 7 3 1; mk_who 8 1 2]) = nil.
Proof. reflexivity. Qed.

(* ── 14.1 Where a new waiter is written ─────────────────────────── *)

(* wait.c:79-97, queue_insert_tpri: walk from the head and break at the first
 * entry of strictly lower priority figure (lower figure = higher priority), so
 * the queue is ascending and a tie keeps the task that was already waiting. *)
Fixpoint insert_tpri (who : sem_who) (q : list sem_who) : list sem_who :=
  match q with
  | nil => who :: nil
  | x :: rest => if Nat.ltb (w_pri who) (w_pri x)
                 then who :: q
                 else x :: insert_tpri who rest
  end.

Lemma insert_tpri_puts_the_better_task_first : forall who x q,
    Nat.ltb (w_pri who) (w_pri x) = true -> insert_tpri who (x :: q) = who :: x :: q.
Proof. intros who x q P. unfold insert_tpri. rewrite P. reflexivity. Qed.

Lemma insert_tpri_tie_defers : forall who x q,
    Nat.ltb (w_pri who) (w_pri x) = false -> insert_tpri who (x :: q) = x :: insert_tpri who q.
Proof. intros who x q P. unfold insert_tpri. rewrite P. reflexivity. Qed.

Lemma a_tie_keeps_the_waiting_task_first : forall who x q,
    Nat.ltb (w_pri who) (w_pri x) = false -> exists q', insert_tpri who (x :: q) = x :: q'.
Proof. intros who x q P. exists (insert_tpri who q). apply insert_tpri_tie_defers. exact P. Qed.

Lemma app_one_length : forall (q : list sem_who) (who : sem_who),
    length (q ++ [who]) = S (length q).
Proof.
  intros q. induction q as [|x rest IH]; intros who.
  - cbn [app length]. reflexivity.
  - cbn [app length]. rewrite IH. reflexivity.
Qed.

Lemma insert_tpri_adds_exactly_one : forall who q,
    length (insert_tpri who q) = S (length q).
Proof.
  intros who q. revert who.
  induction q as [|x rest IH]; intros who.
  - cbn [insert_tpri length]. reflexivity.
  - destruct (Nat.ltb (w_pri who) (w_pri x)) eqn:P.
    + rewrite (insert_tpri_puts_the_better_task_first who x rest P).
      cbn [length]. reflexivity.
    + rewrite (insert_tpri_tie_defers who x rest P).
      cbn [length]. rewrite (IH who). reflexivity.
Qed.

(* The order the insertion maintains, stated so the head-only test below is
 * seen to be enough.  A two-argument fixpoint because the nested pattern
 * "_ :: _ :: _ " is not a guarded recursion in Rocq. *)
Fixpoint pri_ascending_tail (prev : sem_who) (q : list sem_who) : bool :=
  match q with
  | nil => true
  | x :: rest => andb (Nat.leb (w_pri prev) (w_pri x)) (pri_ascending_tail x rest)
  end.

Definition pri_ascending (q : list sem_who) : bool :=
  match q with
  | nil => true
  | x :: rest => pri_ascending_tail x rest
  end.

(* wait.c:96 (gcb_make_wait) picks between the two by the TA_TPRI attribute: a
 * FIFO semaphore appends, a TPRI semaphore inserts. *)
Definition sem_enqueue (tpri : bool) (who : sem_who) (q : list sem_who) : list sem_who :=
  if tpri then insert_tpri who q else q ++ [who].

Lemma a_fifo_sem_enqueue_goes_to_the_tail : forall who q,
    sem_enqueue false who q = q ++ [who].
Proof. intros who q. reflexivity. Qed.

Lemma sem_enqueue_adds_exactly_one : forall tpri who q,
    length (sem_enqueue tpri who q) = S (length q).
Proof.
  intros tpri who q. destruct tpri.
  - apply insert_tpri_adds_exactly_one.
  - cbn [sem_enqueue]. apply app_one_length.
Qed.

(* The kernel's soundness argument for looking only at the head of the queue
 * (wait.c:196-207) is that the TPRI queue is kept sorted.  Here that is a
 * theorem about the insertion, not an assumption. *)
Lemma sem_enqueue_keeps_the_queue_ascending : forall q who,
    pri_ascending q = true -> pri_ascending (sem_enqueue true who q) = true.
Proof.
  unfold sem_enqueue. intros q who H. revert who H.
  induction q as [|x rest IH]; intros who H.
  - cbn [insert_tpri pri_ascending]. reflexivity.
  - destruct rest as [|y rest'] eqn:Er; subst rest.
    + destruct (Nat.ltb (w_pri who) (w_pri x)) eqn:P.
      * rewrite (insert_tpri_puts_the_better_task_first who x nil P).
        cbn [pri_ascending pri_ascending_tail].
        assert (B : Nat.leb (w_pri who) (w_pri x) = true).
        { apply Nat.leb_le. apply Nat.lt_le_incl. apply Nat.ltb_lt. exact P. }
        rewrite B. reflexivity.
      * rewrite (insert_tpri_tie_defers who x nil P).
        cbn [insert_tpri pri_ascending pri_ascending_tail].
        assert (B : Nat.leb (w_pri x) (w_pri who) = true).
        { apply Nat.leb_le. apply ltb_false_ge. exact P. }
        rewrite B. reflexivity.
    + cbn [pri_ascending pri_ascending_tail] in H. apply andb_true_iff in H.
      destruct H as [A C].
      destruct (Nat.ltb (w_pri who) (w_pri x)) eqn:P.
      * rewrite (insert_tpri_puts_the_better_task_first who x (y :: rest') P).
        cbn [pri_ascending pri_ascending_tail]. rewrite A, C.
        assert (B : Nat.leb (w_pri who) (w_pri x) = true).
        { apply Nat.leb_le. apply Nat.lt_le_incl. apply Nat.ltb_lt. exact P. }
        rewrite B. reflexivity.
      * rewrite (insert_tpri_tie_defers who x (y :: rest') P).
        destruct (Nat.ltb (w_pri who) (w_pri y)) eqn:Q.
        { rewrite (insert_tpri_puts_the_better_task_first who y rest' Q).
          cbn [pri_ascending pri_ascending_tail].
          assert (B1 : Nat.leb (w_pri x) (w_pri who) = true).
          { apply Nat.leb_le. apply ltb_false_ge. exact P. }
          assert (B2 : Nat.leb (w_pri who) (w_pri y) = true).
          { apply Nat.leb_le. apply Nat.lt_le_incl. apply Nat.ltb_lt. exact Q. }
          rewrite B1, B2, C. reflexivity. }
        { specialize (IH who C).
          rewrite (insert_tpri_tie_defers who y rest' Q) in IH.
          rewrite (insert_tpri_tie_defers who y rest' Q).
          cbn [pri_ascending pri_ascending_tail] in IH.
          cbn [pri_ascending pri_ascending_tail].
          apply andb_true_iff. split; [ exact A | exact IH ]. }
Qed.

(* ── 14.2 Who may take the count ────────────────────────────────── *)

(* wait.c:196-207, gcb_top_of_wait_queue.  An empty queue makes the caller the
 * top; a non-empty FIFO queue never does (the head is somebody else); a TPRI
 * queue does only on a strict improvement, and the comparison is against the
 * HEAD ONLY -- which is sound because §14.1 keeps the queue ascending. *)
Definition top_of_queue (tpri : bool) (pri : nat) (q : list sem_who) : bool :=
  match q with
  | nil => true
  | x :: _ => andb tpri (Nat.ltb pri (w_pri x))
  end.

Lemma an_empty_queue_admits_any_caller : forall tpri pri,
    top_of_queue tpri pri nil = true.
Proof. intros tpri pri. reflexivity. Qed.

Lemma a_plain_fifo_never_lets_anyone_cut : forall pri x q,
    top_of_queue false pri (x :: q) = false.
Proof. intros pri x q. reflexivity. Qed.

Lemma a_priority_waiter_must_be_strictly_better_than_the_head : forall pri x q,
    top_of_queue true pri (x :: q) = true <-> Nat.ltb pri (w_pri x) = true.
Proof.
  intros pri x q. split.
  - intros H. cbn [top_of_queue] in H. apply andb_true_iff in H.
    destruct H as [T P]. exact P.
  - intros P. cbn [top_of_queue]. apply andb_true_iff. split; [ reflexivity | exact P ].
Qed.

Lemma a_tie_is_not_a_head_place : forall x q tpri,
    top_of_queue tpri (w_pri x) (x :: q) = false.
Proof.
  intros x q tpri. cbn [top_of_queue].
  destruct tpri.
  - apply Nat.ltb_ge. lia.
  - reflexivity.
Qed.

(* semaphore.c:328-330, the claim test: (TA_CNT || top-of-queue) && semcnt >=
 * cnt.  Note which conjunct the attribute can substitute for. *)
Definition sem_claimed (gran head : bool) (count need : nat) : bool :=
  andb (orb gran head) (Nat.leb need count).

Lemma a_granular_semaphore_does_not_consult_the_frontier : forall head count need,
    sem_claimed true head count need = Nat.leb need count.
Proof. intros head count need. reflexivity. Qed.

Lemma a_fifo_semaphore_admits_only_the_head : forall head count need,
    sem_claimed false head count need = andb head (Nat.leb need count).
Proof. intros head count need. reflexivity. Qed.

Lemma no_claim_without_the_units : forall gran head count need,
    sem_claimed gran head count need = true -> Nat.leb need count = true.
Proof.
  intros gran head count need H. unfold sem_claimed in H.
  apply andb_true_iff in H. destruct H as [C U]. exact U.
Qed.

Lemma enough_units_at_a_head_place_is_a_claim : forall gran head count need,
    orb gran head = true -> Nat.leb need count = true ->
    sem_claimed gran head count need = true.
Proof.
  intros gran head count need F U. unfold sem_claimed. rewrite F, U. reflexivity.
Qed.

Lemma a_claim_is_either_granular_or_a_head_place : forall gran head count need,
    sem_claimed gran head count need = true ->
    orb gran head = true /\ Nat.leb need count = true.
Proof.
  intros gran head count need H. unfold sem_claimed in H.
  apply andb_true_iff in H. exact H.
Qed.

(* ── 14.3 The control block, and how the table sees it ──────────── *)

(* semaphore.c:35-45 is seven fields, of which §6's sem records the marker, the
 * count and the queue.  The ceiling and the three attribute bits are not
 * decoration here: they decide which guard fires (E_QOVR against maxsem at
 * :238, the break against TA_CNT at :254, the insertion point against TA_TPRI
 * at :321, the wait-disable test against TA_NODISWAI at :308), so the walk and
 * the entry points both need them.  semcb is the same object with those fields
 * exposed; sem_view is the projection back onto §6, through which the bus, the
 * stored-marker test and the ID/index map of §5 keep applying. *)
Record semcb : Type := mk_semcb {
    sc_id    : nat;             (* SEMCB.semid -- stored marker, 0 = free cell *)
    sc_max   : nat;             (* SEMCB.maxsem -- the ceiling *)
    sc_gran  : bool;            (* TA_CNT: a signal may step over an unsatisfied waiter *)
    sc_tpri  : bool;            (* TA_TPRI: the queue is ordered by priority *)
    sc_nodis : bool;            (* TA_NODISWAI: this object never disables waits *)
    sc_cnt   : nat;             (* SEMCB.semcnt *)
    sc_wait  : list sem_who     (* SEMCB.wait_queue, head waiter first *)
  }.

(* The kernel's queue is a queue of TCBs; an ID-only view sees the tasks and
 * forgets the need each of them named -- which is precisely the information a
 * refer call cannot return (semaphore.c:352 hands back only the count). *)
Definition sem_view (c : semcb) : sem :=
  mk_sem (sc_id c) (sc_cnt c) (map w_tid (sc_wait c)).

Lemma view_keeps_the_marker : forall c, s_id (sem_view c) = sc_id c.
Proof. intros c. reflexivity. Qed.

Lemma view_keeps_the_count : forall c, s_count (sem_view c) = sc_cnt c.
Proof. intros c. reflexivity. Qed.

Lemma map_w_tid_length : forall q : list sem_who, length (map w_tid q) = length q.
Proof.
  intros q. induction q as [|x q IH]; cbn [map length]; [reflexivity |].
  rewrite IH. reflexivity.
Qed.

Lemma view_keeps_the_length : forall c,
    length (s_wait (sem_view c)) = length (sc_wait c).
Proof. intros c. unfold sem_view. apply map_w_tid_length. Qed.

(* §6 wrote the bus read-back lemmas for the task table only; §13's mailbox is a
 * single cell so it never needed one.  The semaphore laws below are stated at a
 * table index, so the sem case is now load-bearing. *)
Lemma at_s_same_s : forall st i (v : sem), b_s (bus_s st i v) i = v.
Proof. intros st i v. unfold bus_s. apply upd_same. Qed.

Lemma at_s_other_s : forall st i (v : sem) j, i <> j -> b_s (bus_s st i v) j = b_s st j.
Proof. intros st i v j H. unfold bus_s. apply upd_other; exact H. Qed.

Lemma bus_s_reads_the_cell : forall st i (c : semcb),
    b_s (bus_s st i (sem_view c)) i = sem_view c.
Proof. intros st i c. apply at_s_same_s. Qed.

Lemma bus_s_leaves_the_task_table : forall st i (c : semcb) j,
    b_t (bus_s st i (sem_view c)) j = b_t st j.
Proof. intros st i c j. unfold bus_s. reflexivity. Qed.

Lemma bus_s_leaves_the_mailbox_table : forall st i (c : semcb) j,
    b_m (bus_s st i (sem_view c)) j = b_m st j.
Proof. intros st i c j. unfold bus_s. reflexivity. Qed.

Lemma indp_inert_s : forall st i (v : sem), b_indp (bus_s st i v) = b_indp st.
Proof. intros st i v. unfold bus_s. reflexivity. Qed.

(* semaphore.c:235 and :297 both read the stored marker INSIDE the critical
 * section, which is the E_NOEXS of this family.  §6's sem_used is that test;
 * here it is shown to survive the projection. *)
Lemma used_is_the_marker : forall st i (c : semcb),
    sem_used (bus_s st i (sem_view c)) i = negb (Nat.eqb (sc_id c) 0).
Proof.
  intros st i c. unfold sem_used.
  rewrite bus_s_reads_the_cell, view_keeps_the_marker. reflexivity.
Qed.

(* The two shape properties the C maintains.  semcnt <= maxsem is what makes
 * "cnt > maxsem - semcnt" (:238) a headroom test rather than a wrap-around
 * test, and CHECK_PAR(cnt > 0) at both entry points (:229, :289) is what makes
 * positive needs a standing property of the queue rather than an assumption
 * about one call. *)
Definition count_within_ceiling (c : semcb) : bool := Nat.leb (sc_cnt c) (sc_max c).
Definition waiters_have_needs (c : semcb) : bool := every_needs (sc_wait c).
Definition sem_wf (c : semcb) : bool :=
  andb (count_within_ceiling c) (waiters_have_needs c).

Lemma a_wellformed_cell_is_within_its_ceiling : forall c,
    sem_wf c = true -> sc_cnt c <= sc_max c.
Proof.
  intros c H. unfold sem_wf, count_within_ceiling in H.
  apply andb_true_iff in H. apply Nat.leb_le. exact (proj1 H).
Qed.

Lemma a_wellformed_cell_has_positive_needs : forall c,
    sem_wf c = true -> every_needs (sc_wait c) = true.
Proof.
  intros c H. unfold sem_wf, waiters_have_needs in H.
  apply andb_true_iff in H. exact (proj2 H).
Qed.

Lemma a_fresh_cell_is_wellformed : forall m g tp nd,
    sem_wf (mk_semcb 0 m g tp nd 0 nil) = true.
Proof.
  intros m g tp nd. unfold sem_wf, count_within_ceiling, waiters_have_needs, every_needs.
  cbn [forallb andb]. apply andb_true_iff. split.
  - destruct m; reflexivity.
  - reflexivity.
Qed.

(* ── 14.4 The wait-disable guard ────────────────────────────────── *)

(* wait.h:128-132, is_diswai.  Two independent coordinates, one read from the
 * task and one from the object:
 *   (tcb->waitmask & tskwait) != 0 && (gcb->objatr & TA_NODISWAI) == 0
 * The task side is a bit test on the sparse mask of §12 -- a mask can be
 * tested but never ordered -- and the object side is the negation of an
 * attribute bit.  semaphore.c:144-147 lists TA_NODISWAI among the attributes
 * cre_sem accepts, so both coordinates are reachable on this family. *)
Definition masked_for (mask : nat) (o : wobj) : bool := bit_any mask (ttw_of o).

Lemma a_task_waiting_on_a_semaphore_is_masked_for_it : masked_for ttw_sem WO_SEM = true.
Proof. unfold masked_for, bit_any. vm_compute. reflexivity. Qed.

Lemma a_sleeping_task_is_not_masked_for_a_semaphore : masked_for ttw_slp WO_SEM = false.
Proof. unfold masked_for, bit_any. vm_compute. reflexivity. Qed.

(* One bit per class is what makes the test conservative in the right
 * direction: a task masked for any OTHER reason is not disabled here. *)
Lemma a_disjoint_mask_never_disables : forall o1 o2, o1 <> o2 ->
    masked_for (ttw_of o1) o2 = false.
Proof.
  intros o1 o2 H. unfold masked_for, bit_any.
  rewrite (ttw_pairwise_disjoint o1 o2 H). reflexivity.
Qed.

Lemma the_mask_test_asks_for_the_object : forall o,
    masked_for (ttw_of o) o = true.
Proof.
  intros o. unfold masked_for, bit_any. destruct o; vm_compute; reflexivity.
Qed.

Definition diswai_of (masked nodiswai : bool) : bool := andb masked (negb nodiswai).

Lemma a_nodiswai_object_never_refuses_a_wait : forall m, diswai_of m true = false.
Proof. intros m. unfold diswai_of. destruct m; reflexivity. Qed.

Lemma an_unmasked_task_is_never_refused : forall n, diswai_of false n = false.
Proof. intros n. unfold diswai_of. reflexivity. Qed.

Lemma the_guard_needs_both_coordinates : forall m n,
    diswai_of m n = true -> m = true /\ n = false.
Proof.
  intros m n H. unfold diswai_of in H. apply andb_true_iff in H. destruct H as [M N].
  split; [ exact M | apply negb_true_iff; exact N ].
Qed.

(* The whole truth table, because this guard is the only one in the family
 * decided by a conjunction of two independent coordinates. *)
Lemma the_guard_is_beatable_from_either_side :
    diswai_of true false = true /\ diswai_of true true = false /\
    diswai_of false false = false /\ diswai_of false true = false.
Proof. repeat split; reflexivity. Qed.

(* A refusal is not a wait: §12.4's enqueues consumes the verdict, so the
 * refused caller neither registers on the queue nor has its receipt
 * pre-written -- semaphore.c:308-310 returns E_DISWAI as the service's OWN
 * figure, from the guard, before the claim test at :313 is even evaluated. *)
Lemma a_refused_wait_never_registers : forall m n t,
    diswai_of m n = true -> enqueues (diswai_of m n) t = false.
Proof. intros m n t H. rewrite H. apply diswai_never_enqueues. Qed.

Lemma diswai_is_a_refusal_not_a_prewrite :
    prewrite true = E_DISWAI /\ diswai_of true false = true.
Proof. split; reflexivity. Qed.

(* ── 14.5 The preflight cascades of this family ─────────────────── *)

(* cre_sem, semaphore.c:157-160, then the FreeQue failure at :166.  Two of the
 * four guards are arithmetic on the initialization figures and the third is the
 * table's own exhaustion; the fourth, isemcnt >= 0, is a signed test that every
 * natural passes, so the model's type carries it and the cascade does not need
 * it.  CHECK_RSATR (:157) is omitted for the same reason as in §12: the cell
 * already records the three bits cre_sem accepts (TA_TPRI | TA_CNT |
 * TA_NODISWAI, semaphore.c:144-147), so an illegal attribute is not a state the
 * model can name. *)
Definition sem_cre_guards (free_cell : bool) (isemcnt maxsem : nat) : list (bool * er) :=
  (Nat.ltb 0 maxsem, E_PAR) ::
  (Nat.leb isemcnt maxsem, E_PAR) ::
  (free_cell, E_LIMIT) :: nil.

Lemma cre_E_LIMIT_is_exhaustion : forall free i m,
    first_bad (sem_cre_guards free i m) = Some E_LIMIT ->
    Nat.ltb 0 m = true /\ Nat.leb i m = true /\ free = false.
Proof.
  intros free i m H. unfold sem_cre_guards in H.
  destruct (Nat.ltb 0 m); destruct (Nat.leb i m); destruct free;
    cbn [first_bad] in H; try discriminate H;
    repeat split; reflexivity.
Qed.

(* E_LIMIT is the only receipt of this service that is not a verdict on the two
 * numbers the caller supplied -- which is why a caller that gets E_LIMIT learns
 * nothing about its own ceiling. *)
Lemma cre_E_PAR_is_never_the_table : forall free i m,
    first_bad (sem_cre_guards free i m) = Some E_PAR ->
    Nat.ltb 0 m = false \/ Nat.leb i m = false.
Proof.
  intros free i m H. unfold sem_cre_guards in H.
  set (p := Nat.ltb 0 m) in *. set (q := Nat.leb i m) in *.
  destruct p; destruct q; destruct free;
    cbn [first_bad] in H; try discriminate H;
    first [left; reflexivity | right; reflexivity].
Qed.

(* sig_sem, semaphore.c:228-239: the range test, cnt > 0, the stored marker read
 * inside the critical section, and the ceiling as a HEADROOM test.  Note what
 * is absent: _tk_sig_sem has no CHECK_DISPATCH.  A signal is legal from a
 * dispatch-disabled or task-independent context, which is what makes the
 * release path usable from a hook. *)
Definition sem_sig_guards (used : bool) (id cnt max count : nat) : list (bool * er) :=
  (chk_id min_semid num_sem id, E_ID) ::
  (Nat.ltb 0 cnt, E_PAR) ::
  (used, E_NOEXS) ::
  (Nat.leb cnt (Nat.sub max count), E_QOVR) :: nil.

Lemma sig_E_ID_is_the_range : forall used id cnt max count,
    first_bad (sem_sig_guards used id cnt max count) = Some E_ID ->
    chk_id min_semid num_sem id = false.
Proof.
  intros used id cnt max count H. unfold sem_sig_guards in H.
  destruct (chk_id min_semid num_sem id);
    destruct (Nat.ltb 0 cnt); destruct used;
    destruct (Nat.leb cnt (Nat.sub max count));
    cbn [first_bad] in H; try discriminate H; reflexivity.
Qed.

Lemma sig_E_PAR_is_a_zero_request : forall used id cnt max count,
    first_bad (sem_sig_guards used id cnt max count) = Some E_PAR ->
    chk_id min_semid num_sem id = true /\ Nat.ltb 0 cnt = false.
Proof.
  intros used id cnt max count H. unfold sem_sig_guards in H.
  set (c := chk_id min_semid num_sem id) in *.
  destruct c; destruct (Nat.ltb 0 cnt); destruct used;
    destruct (Nat.leb cnt (Nat.sub max count));
    cbn [first_bad] in H; try discriminate H; split; reflexivity.
Qed.

(* The marker test comes BEFORE the ceiling test (:235 then :238): signalling
 * into a free cell reports E_NOEXS even when the figures would overflow it. *)
Lemma sig_E_NOEXS_is_the_stored_marker : forall used id cnt max count,
    first_bad (sem_sig_guards used id cnt max count) = Some E_NOEXS ->
    chk_id min_semid num_sem id = true /\ Nat.ltb 0 cnt = true /\ used = false.
Proof.
  intros used id cnt max count H. unfold sem_sig_guards in H.
  set (c := chk_id min_semid num_sem id) in *.
  set (p := Nat.ltb 0 cnt) in *.
  destruct c; destruct p; destruct used;
    destruct (Nat.leb cnt (Nat.sub max count));
    cbn [first_bad] in H; try discriminate H;
    repeat split; reflexivity.
Qed.

Lemma sig_E_QOVR_is_the_ceiling : forall used id cnt max count,
    first_bad (sem_sig_guards used id cnt max count) = Some E_QOVR ->
    chk_id min_semid num_sem id = true /\ Nat.ltb 0 cnt = true /\ used = true /\
    Nat.leb cnt (Nat.sub max count) = false.
Proof.
  intros used id cnt max count H. unfold sem_sig_guards in H.
  set (c := chk_id min_semid num_sem id) in *.
  set (p := Nat.ltb 0 cnt) in *.
  destruct c; destruct p; destruct used;
    destruct (Nat.leb cnt (Nat.sub max count));
    cbn [first_bad] in H; try discriminate H;
    repeat split; reflexivity.
Qed.

(* The C test is "cnt > maxsem - semcnt" (:238) in signed arithmetic.  On the
 * model's naturals the subtraction truncates, so the translation to the
 * addition form the invariant suggests is only valid under semcnt <= maxsem --
 * and that is exactly §14.3's count_within_ceiling, not a free assumption. *)
Lemma headroom_matches_addition : forall cnt max count, count <= max ->
    Nat.leb cnt (Nat.sub max count) = Nat.leb (cnt + count) max.
Proof.
  intros cnt max count H.
  destruct (Nat.leb cnt (Nat.sub max count)) eqn:E1;
  destruct (Nat.leb (cnt + count) max) eqn:E2.
  - reflexivity.
  - apply Nat.leb_le in E1. apply Nat.leb_gt in E2. lia.
  - apply Nat.leb_gt in E1. apply Nat.leb_le in E2. lia.
  - reflexivity.
Qed.

(* E_CTX is unreachable from the signal cascade and reachable from the wait
 * cascade.  One figure, ERCD aside, separates the two services on the context
 * coordinate alone. *)
Lemma a_signal_is_never_refused_on_context : forall used id cnt max count,
    first_bad (sem_sig_guards used id cnt max count) <> Some E_CTX.
Proof.
  intros used id cnt max count. unfold sem_sig_guards.
  destruct (chk_id min_semid num_sem id);
    destruct (Nat.ltb 0 cnt); destruct used;
    destruct (Nat.leb cnt (Nat.sub max count));
    cbn [first_bad]; discriminate.
Qed.

(* wai_sem, semaphore.c:288-309, in the order the receipts are produced.
 * CHECK_TMOUT (:290) is absent because §3's tmo_legal_is_total says the model's
 * timeout type already carries that check, and the cnt > maxsem test
 * (:300-303) is absent because it lives inside #if CHK_PAR and so is not in the
 * shipped build: an over-large request is not refused as a parameter error here,
 * it simply never satisfies the claim test and goes onto the queue. *)
Definition sem_wai_guards (st : kst) (used nodis mask : bool) (id cnt : nat) : list (bool * er) :=
  (chk_id min_semid num_sem id, E_ID) ::
  (Nat.ltb 0 cnt, E_PAR) ::
  (negb (b_ddsp st), E_CTX) ::
  (used, E_NOEXS) ::
  (negb (diswai_of mask nodis), E_DISWAI) :: nil.

Lemma wai_E_CTX_is_a_context_refusal : forall st used nodis mask id cnt,
    first_bad (sem_wai_guards st used nodis mask id cnt) = Some E_CTX ->
    chk_id min_semid num_sem id = true /\ Nat.ltb 0 cnt = true /\ b_ddsp st = true.
Proof.
  intros st used nodis mask id cnt H. unfold sem_wai_guards in H.
  set (c := chk_id min_semid num_sem id) in *.
  set (p := Nat.ltb 0 cnt) in *.
  set (d := b_ddsp st) in *.
  destruct c; destruct p; destruct d; destruct used; destruct (diswai_of mask nodis);
    cbn [first_bad negb] in H; try discriminate H;
    repeat split; reflexivity.
Qed.

Lemma wai_E_NOEXS_is_the_stored_marker : forall st used nodis mask id cnt,
    first_bad (sem_wai_guards st used nodis mask id cnt) = Some E_NOEXS ->
    chk_id min_semid num_sem id = true /\ Nat.ltb 0 cnt = true /\ b_ddsp st = false /\
    used = false.
Proof.
  intros st used nodis mask id cnt H. unfold sem_wai_guards in H.
  set (c := chk_id min_semid num_sem id) in *.
  set (p := Nat.ltb 0 cnt) in *.
  set (d := b_ddsp st) in *.
  destruct c; destruct p; destruct d; destruct used; destruct (diswai_of mask nodis);
    cbn [first_bad negb] in H; try discriminate H;
    repeat split; reflexivity.
Qed.

(* The last guard of the cascade, and the only one that reads both a task
 * coordinate and an object coordinate.  Everything before it passed, so an
 * E_DISWAI is evidence that the caller was masked AND the object was not
 * exempt -- the two halves §14.4 separates. *)
Lemma wai_E_DISWAI_is_a_diswai_refusal : forall st used nodis mask id cnt,
    first_bad (sem_wai_guards st used nodis mask id cnt) = Some E_DISWAI ->
    chk_id min_semid num_sem id = true /\ Nat.ltb 0 cnt = true /\ b_ddsp st = false /\
    used = true /\ diswai_of mask nodis = true.
Proof.
  intros st used nodis mask id cnt H. unfold sem_wai_guards in H.
  set (c := chk_id min_semid num_sem id) in *.
  set (p := Nat.ltb 0 cnt) in *.
  set (d := b_ddsp st) in *.
  set (v := diswai_of mask nodis) in *.
  destruct c; destruct p; destruct d; destruct used; destruct v;
    cbn [first_bad negb] in H; try discriminate H;
    repeat split; reflexivity.
Qed.

(* A wait can be refused for a reason a signal cannot, and the guard that does
 * it is the context one; st0 is §6's honest idle state, which is both
 * independent and dispatch-disabled. *)
Example the_two_services_differ_on_one_guard :
    first_bad (sem_sig_guards true 1 2 5 0) = None /\
    first_bad (sem_wai_guards st0 true false false 1 2) = Some E_CTX.
Proof. split; vm_compute; reflexivity. Qed.

(* ── 14.6 The two entry points as steps on the cell ─────────────── *)

(* sem_after is the walk's answer written back over an existing cell: the ID
 * marker, the ceiling and the three attribute bits are carried through, and the
 * count and the queue are the walk's two remaining fields.  The pair is
 * returned separately, never as a let, so every law below is a projection. *)
Definition sem_after (c : semcb) (d : drain) : semcb :=
  mk_semcb (sc_id c) (sc_max c) (sc_gran c) (sc_tpri c) (sc_nodis c) (d_left d) (d_kept d).

(* sig_sem, semaphore.c:244-266: add the units, then run the drain over the
 * queue.  The signal's own receipt is E_OK either way, so what the step returns
 * beside the cell is the list the kernel released -- the tasks that left the
 * queue are the whole observable content of the walk. *)
Definition sem_sig_step (cnt : nat) (c : semcb) : semcb * list sem_who :=
  (sem_after c (sig_walk (sc_gran c) (sc_cnt c + cnt) (sc_wait c)),
   d_gone (sig_walk (sc_gran c) (sc_cnt c + cnt) (sc_wait c))).

Lemma sig_step_conserves : forall cnt c,
    sc_cnt (fst (sem_sig_step cnt c)) + qsum (snd (sem_sig_step cnt c)) = sc_cnt c + cnt.
Proof.
  intros cnt c. unfold sem_sig_step. cbn [fst snd sem_after].
  symmetry. apply sig_walk_conserves.
Qed.

Lemma sig_step_keeps_the_survivors : forall cnt c,
    sc_wait (fst (sem_sig_step cnt c)) = d_kept (sig_walk (sc_gran c) (sc_cnt c + cnt) (sc_wait c)).
Proof. intros cnt c. reflexivity. Qed.

Lemma sig_step_releases_the_gone : forall cnt c,
    snd (sem_sig_step cnt c) = d_gone (sig_walk (sc_gran c) (sc_cnt c + cnt) (sc_wait c)).
Proof. intros cnt c. reflexivity. Qed.

(* A signal cannot invent units: the balance after the step is at most the old
 * balance plus what was handed in.  This is the conservation law read in one
 * direction, and it is the half the kernel's in-place decrement depends on. *)
Lemma sig_never_invents_units : forall cnt c,
    Nat.leb (sc_cnt (fst (sem_sig_step cnt c))) (sc_cnt c + cnt) = true.
Proof.
  intros cnt c. assert (C := sig_step_conserves cnt c).
  apply Nat.leb_le. lia.
Qed.

(* A signal moves the count and the queue and nothing else: the marker, the
 * ceiling and the three attribute bits are carried through by sem_after.  The
 * ceiling in particular has to be named, because every inequality below is
 * against sc_max c and the result cell is a different term. *)
Lemma sig_step_keeps_the_identity_fields : forall cnt c,
    sc_id (fst (sem_sig_step cnt c)) = sc_id c /\
    sc_max (fst (sem_sig_step cnt c)) = sc_max c /\
    sc_gran (fst (sem_sig_step cnt c)) = sc_gran c /\
    sc_tpri (fst (sem_sig_step cnt c)) = sc_tpri c /\
    sc_nodis (fst (sem_sig_step cnt c)) = sc_nodis c.
Proof.
  intros cnt c. unfold sem_sig_step. cbn [fst sem_after]. repeat split; reflexivity.
Qed.

Lemma sig_step_keeps_the_ceiling : forall cnt c,
    sc_max (fst (sem_sig_step cnt c)) = sc_max c.
Proof. intros cnt c. unfold sem_sig_step. cbn [fst sem_after]. reflexivity. Qed.

(* The other half of the accounting: a well-formed cell stays well-formed
 * through a signal that the ceiling guard admitted. *)
Lemma sig_step_within_ceiling : forall cnt c,
    sem_wf c = true -> Nat.leb cnt (Nat.sub (sc_max c) (sc_cnt c)) = true ->
    count_within_ceiling (fst (sem_sig_step cnt c)) = true.
Proof.
  intros cnt c W H.
  assert (U : sc_cnt c <= sc_max c) by (apply a_wellformed_cell_is_within_its_ceiling; exact W).
  assert (L : cnt <= Nat.sub (sc_max c) (sc_cnt c)). { apply Nat.leb_le in H. exact H. }
  assert (C := sig_step_conserves cnt c).
  unfold count_within_ceiling. rewrite sig_step_keeps_the_ceiling. apply Nat.leb_le. lia.
Qed.

(* Only a FIFO walk can be run with no units to hand out.  The zero signal is
 * not the identity on the cell -- it is exactly the drain that §14.7's rel_wai
 * hook runs, starting from the count the object already holds. *)
Lemma sig_zero_is_the_drain_from_the_current_count : forall c,
    fst (sem_sig_step 0 c) = sem_after c (sig_walk (sc_gran c) (sc_cnt c) (sc_wait c)) /\
    snd (sem_sig_step 0 c) = d_gone (sig_walk (sc_gran c) (sc_cnt c) (sc_wait c)).
Proof.
  intros c. unfold sem_sig_step. rewrite Nat.add_0_r. split; reflexivity.
Qed.

Lemma a_zero_signal_onto_an_empty_queue_is_the_identity : forall c,
    sc_wait c = nil -> sem_sig_step 0 c = (c, nil).
Proof.
  intros c E. destruct c as [i m g tp nd cnt q].
  cbn [sc_wait] in E. rewrite E.
  unfold sem_sig_step, sem_after. rewrite Nat.add_0_r, sig_walk_nil.
  cbn [d_left d_kept d_gone]. reflexivity.
Qed.

(* The queue a signal leaves behind is a sublist of the queue it started with,
 * so the positive-needs half of the invariant is a property of the walk rather
 * than of the request. *)
Lemma the_walk_keeps_only_positive_needs : forall gran q count,
    every_needs q = true -> every_needs (d_kept (sig_walk gran count q)) = true.
Proof.
  intros gran q. revert gran.
  induction q as [|x rest IH]; intros gran count H.
  - cbn [sig_walk d_kept every_needs forallb]. reflexivity.
  - cbn [every_needs forallb] in H. apply andb_true_iff in H. destruct H as [P Hrest].
    destruct (Nat.ltb count (w_need x)) eqn:N.
    + destruct gran.
      * rewrite (sig_walk_unsat_gran count x rest N).
        cbn [d_kept drain_skip every_needs forallb]. apply andb_true_iff.
        split; [ exact P | apply (IH true count Hrest) ].
      * rewrite (sig_walk_unsat_fifo count x rest N).
        cbn [d_kept every_needs forallb]. apply andb_true_iff.
        split; [ exact P | exact Hrest ].
    + rewrite (sig_walk_sat_kept gran count x rest N). apply IH. exact Hrest.
Qed.

Lemma sig_step_preserves_the_invariant : forall cnt c,
    sem_wf c = true -> Nat.leb cnt (Nat.sub (sc_max c) (sc_cnt c)) = true ->
    sem_wf (fst (sem_sig_step cnt c)) = true.
Proof.
  intros cnt c W H. unfold sem_wf, waiters_have_needs.
  apply andb_true_iff. split.
  - apply sig_step_within_ceiling. exact W. exact H.
  - apply the_walk_keeps_only_positive_needs.
    apply a_wellformed_cell_has_positive_needs. exact W.
Qed.

(* wai_sem, semaphore.c:308-325.  Three branches in the C order: the wait-
 * disable guard returns its own figure before anything is examined, the claim
 * test subtracts, and the failure branch registers the caller and pre-writes
 * E_TMOUT through §12.4 -- or, for a poll, does neither and reports the same
 * figure from the guard.  sem_take and sem_block are the two assignments the
 * kernel makes, each leaving the other fields alone. *)
Definition sem_take (need : nat) (c : semcb) : semcb :=
  mk_semcb (sc_id c) (sc_max c) (sc_gran c) (sc_tpri c) (sc_nodis c)
           (Nat.sub (sc_cnt c) need) (sc_wait c).

Definition sem_block (who : sem_who) (c : semcb) : semcb :=
  mk_semcb (sc_id c) (sc_max c) (sc_gran c) (sc_tpri c) (sc_nodis c)
           (sc_cnt c) (sem_enqueue (sc_tpri c) who (sc_wait c)).

Definition sem_wai (mask : nat) (t : tmo) (who : sem_who) (c : semcb) : semcb * er :=
  if diswai_of (masked_for mask WO_SEM) (sc_nodis c) then (c, E_DISWAI)
  else if sem_claimed (sc_gran c)
                      (top_of_queue (sc_tpri c) (w_pri who) (sc_wait c))
                      (sc_cnt c) (w_need who)
       then (sem_take (w_need who) c, E_OK)
       else if tmo_blocks t then (sem_block who c, prewrite false)
       else (c, prewrite false).

Lemma wai_refusal_leaves_the_cell_alone : forall mask t who c,
    diswai_of (masked_for mask WO_SEM) (sc_nodis c) = true ->
    fst (sem_wai mask t who c) = c.
Proof.
  intros mask t who c D. unfold sem_wai. rewrite D. reflexivity.
Qed.

Lemma wai_refusal_reports_its_own_figure : forall mask t who c,
    diswai_of (masked_for mask WO_SEM) (sc_nodis c) = true ->
    snd (sem_wai mask t who c) = E_DISWAI.
Proof.
  intros mask t who c D. unfold sem_wai. rewrite D. reflexivity.
Qed.

Lemma wai_take_subtracts_the_need : forall mask t who c,
    diswai_of (masked_for mask WO_SEM) (sc_nodis c) = false ->
    sem_claimed (sc_gran c) (top_of_queue (sc_tpri c) (w_pri who) (sc_wait c))
                (sc_cnt c) (w_need who) = true ->
    fst (sem_wai mask t who c) = sem_take (w_need who) c.
Proof.
  intros mask t who c D C. unfold sem_wai. rewrite D, C. reflexivity.
Qed.

Lemma wai_take_reports_E_OK : forall mask t who c,
    diswai_of (masked_for mask WO_SEM) (sc_nodis c) = false ->
    sem_claimed (sc_gran c) (top_of_queue (sc_tpri c) (w_pri who) (sc_wait c))
                (sc_cnt c) (w_need who) = true ->
    snd (sem_wai mask t who c) = E_OK.
Proof.
  intros mask t who c D C. unfold sem_wai. rewrite D, C. reflexivity.
Qed.

(* A wait that blocks registers the caller, and does so in the position §14.1
 * decides; a poll does not register anybody (§12.4's enqueues, and the
 * semaphore's own queue is untouched). *)
Lemma wai_block_registers_the_caller : forall mask t who c,
    diswai_of (masked_for mask WO_SEM) (sc_nodis c) = false ->
    sem_claimed (sc_gran c) (top_of_queue (sc_tpri c) (w_pri who) (sc_wait c))
                (sc_cnt c) (w_need who) = false ->
    tmo_blocks t = true ->
    sc_wait (fst (sem_wai mask t who c)) = sem_enqueue (sc_tpri c) who (sc_wait c).
Proof.
  intros mask t who c D C B. unfold sem_wai. rewrite D, C, B. reflexivity.
Qed.

Lemma wai_block_leaves_the_count_alone : forall mask t who c,
    diswai_of (masked_for mask WO_SEM) (sc_nodis c) = false ->
    sem_claimed (sc_gran c) (top_of_queue (sc_tpri c) (w_pri who) (sc_wait c))
                (sc_cnt c) (w_need who) = false ->
    tmo_blocks t = true ->
    sc_cnt (fst (sem_wai mask t who c)) = sc_cnt c.
Proof.
  intros mask t who c D C B. unfold sem_wai. rewrite D, C, B. reflexivity.
Qed.

Lemma a_poll_registers_nobody : forall mask who c,
    diswai_of (masked_for mask WO_SEM) (sc_nodis c) = false ->
    sem_claimed (sc_gran c) (top_of_queue (sc_tpri c) (w_pri who) (sc_wait c))
                (sc_cnt c) (w_need who) = false ->
    fst (sem_wai mask TMO_POLL who c) = c.
Proof.
  intros mask who c D C. unfold sem_wai. rewrite D, C.
  cbn [tmo_blocks]. reflexivity.
Qed.

(* Every branch of the wait reads the same figure beside the pair: a blocked
 * call and a poll are distinguished by the queue, not by the receipt -- §12.4
 * again, and exactly as in §13's mailbox. *)
Lemma wai_without_a_take_pre_writes_the_timeout : forall mask t who c,
    diswai_of (masked_for mask WO_SEM) (sc_nodis c) = false ->
    sem_claimed (sc_gran c) (top_of_queue (sc_tpri c) (w_pri who) (sc_wait c))
                (sc_cnt c) (w_need who) = false ->
    snd (sem_wai mask t who c) = prewrite false.
Proof.
  intros mask t who c D C. unfold sem_wai. rewrite D, C.
  destruct t; reflexivity.
Qed.

(* E_OK is the signature of the take branch and of nothing else. *)
Lemma an_E_OK_wait_is_a_take : forall mask t who c,
    snd (sem_wai mask t who c) = E_OK ->
    diswai_of (masked_for mask WO_SEM) (sc_nodis c) = false /\
    sem_claimed (sc_gran c) (top_of_queue (sc_tpri c) (w_pri who) (sc_wait c))
                (sc_cnt c) (w_need who) = true.
Proof.
  intros mask t who c H. unfold sem_wai in H.
  set (v := diswai_of (masked_for mask WO_SEM) (sc_nodis c)) in *.
  set (u := sem_claimed (sc_gran c) (top_of_queue (sc_tpri c) (w_pri who) (sc_wait c))
                        (sc_cnt c) (w_need who)) in *.
  destruct v; destruct u; destruct t; cbn [tmo_blocks prewrite snd] in H;
    try discriminate H; split; reflexivity.
Qed.

(* The wait never raises the balance, on any branch -- including the take, where
 * the subtraction truncates rather than going negative. *)
Lemma a_wait_never_adds_units : forall mask t who c,
    Nat.leb (sc_cnt (fst (sem_wai mask t who c))) (sc_cnt c) = true.
Proof.
  intros mask t who c. unfold sem_wai.
  destruct (diswai_of (masked_for mask WO_SEM) (sc_nodis c)).
  - cbn [fst]. apply Nat.leb_le. reflexivity.
  - destruct (sem_claimed (sc_gran c)
                          (top_of_queue (sc_tpri c) (w_pri who) (sc_wait c))
                          (sc_cnt c) (w_need who)).
    + cbn [fst]. unfold sem_take. cbn [sc_cnt]. apply Nat.leb_le. lia.
    + destruct t; cbn [fst tmo_blocks]; unfold sem_block; cbn [sc_cnt];
        apply Nat.leb_le; reflexivity.
Qed.

(* ── 14.7 The departure walk, and the delete broadcast ──────────── *)

(* semaphore.c:149-165 and :126-131.  sem_chg_pri re-orders the queue only when
 * a priority actually changed (gcb_change_priority at :157, skipped for
 * oldpri < 0) and then runs the drain -- but it RETURNS AT ONCE when TA_CNT is
 * set (:161-163). sem_rel_wai is sem_chg_pri with oldpri = -1.  So the hook that
 * fires when a waiter disappears runs the zero-signal FIFO walk: no units are
 * handed in, and a waiter can still be released.  That is the one path on which
 * §14.5's CHECK_PAR(cnt > 0) does not apply, which is why the sig_walk_zero laws
 * above are stated about the walk and not about the service.  The C loop of the
 * departure walk (:166-175) also lacks the "if (semcnt <= 0) break" of the
 * signal walk (:264); with positive needs the next test fails anyway, so the two
 * walks agree and are modelled by the same function. *)
Definition sem_rel_wai_step (c : semcb) : semcb * list sem_who :=
  if sc_gran c then (c, nil)
  else (sem_after c (sig_walk false (sc_cnt c) (sc_wait c)),
        d_gone (sig_walk false (sc_cnt c) (sc_wait c))).

Lemma a_granular_departure_does_nothing : forall c,
    sc_gran c = true -> sem_rel_wai_step c = (c, nil).
Proof. intros c G. unfold sem_rel_wai_step. rewrite G. reflexivity. Qed.

Lemma rel_wai_is_the_fifo_zero_signal : forall c,
    sc_gran c = false -> sem_rel_wai_step c = sem_sig_step 0 c.
Proof.
  intros c G. unfold sem_rel_wai_step, sem_sig_step.
  rewrite G, Nat.add_0_r. reflexivity.
Qed.

Lemma rel_wai_conserves : forall c, sc_gran c = false ->
    sc_cnt (fst (sem_rel_wai_step c)) + qsum (snd (sem_rel_wai_step c)) = sc_cnt c.
Proof.
  intros c G. unfold sem_rel_wai_step. rewrite G. cbn [fst snd sem_after].
  symmetry. apply sig_walk_conserves.
Qed.

(* A departure releases only from the head, in FIFO order -- §14's prefix law
 * read onto the hook, and the reason a semaphore's timeout can hand its place
 * to the NEXT waiter but never to a later one. *)
Lemma rel_wai_releases_a_fifo_prefix : forall c, sc_gran c = false ->
    exists s, sc_wait c = d_gone (sig_walk false (sc_cnt c) (sc_wait c)) ++ s /\
              sc_wait (fst (sem_rel_wai_step c)) = s.
Proof.
  intros c G. unfold sem_rel_wai_step. rewrite G. cbn [fst sem_after d_kept].
  destruct (fifo_drains_a_prefix (sc_cnt c) (sc_wait c)) as [s [H1 H2]].
  exists s. split; [ exact H1 | apply H2 ].
Qed.

(* The punchline of §14: a waiter leaves, the count is untouched by any signal,
 * and still another task is granted.  Computed, not asserted. *)
Example a_departure_releases_without_a_signal :
    snd (sem_rel_wai_step (mk_semcb 1 5 false false false 2 [mk_who 9 2 1]))
    = [mk_who 9 2 1] /\
    fst (sem_rel_wai_step (mk_semcb 1 5 false false false 2 [mk_who 9 2 1]))
    = mk_semcb 1 5 false false false 0 nil.
Proof. split; vm_compute; reflexivity. Qed.

(* del_sem, semaphore.c:197-215: wait_delete walks the queue and releases every
 * waiter through wait_release_ng with E_DLT, then the cell goes back to the
 * free list with its marker cleared (:210-211).  Same reading as §13's mailbox
 * broadcast: the receipt each waiter ends with is the composition of §12.4's
 * pre-write and a WRITING release, so E_DLT overrides the E_TMOUT the blocked
 * call parked in its own slot. *)
Definition sem_broadcast (c : semcb) : list er :=
  map (fun _ => final_receipt RK_del (prewrite false)) (sc_wait c).

Lemma sem_broadcast_is_all_E_DLT : forall c,
    sem_broadcast c = repeat E_DLT (length (sc_wait c)).
Proof.
  intros c. unfold sem_broadcast.
  assert (A : forall l : list sem_who,
            map (fun _ => final_receipt RK_del (prewrite false)) l = repeat E_DLT (length l)).
  { induction l as [|x l IH]; cbn [map repeat length]; [reflexivity |].
    cbn [final_receipt effect_of ef_write prewrite] in *. rewrite IH. reflexivity. }
  apply A.
Qed.

(* The cell the free list gets back: the marker is cleared, and nothing else is
 * reset -- semcnt and maxsem stay stale in the C, which is legal only because
 * every reader goes through the marker test first (semaphore.c:235, :297). *)
Definition sem_forget (c : semcb) : semcb :=
  mk_semcb 0 (sc_max c) (sc_gran c) (sc_tpri c) (sc_nodis c) (sc_cnt c) nil.

Lemma a_forgotten_cell_reports_no_existence : forall st i c,
    sem_used (bus_s st i (sem_view (sem_forget c))) i = false.
Proof.
  intros st i c. rewrite used_is_the_marker. unfold sem_forget.
  cbn [sc_id]. reflexivity.
Qed.

Lemma a_forgotten_cell_holds_no_waiters : forall c,
    length (s_wait (sem_view (sem_forget c))) = 0.
Proof. intros c. unfold sem_view. cbn [s_wait]. reflexivity. Qed.

(* ── 14.8 The two services, computed ───────────────────────────── *)

(* The wait's own invariant step, which needs §14.1's insertion law: a blocked
 * caller joins the queue, and the queue keeps positive needs only because the
 * caller was refused by the claim test rather than by CHECK_PAR. *)
Lemma every_needs_cons : forall x q,
    every_needs (x :: q) = andb (Nat.ltb 0 (w_need x)) (every_needs q).
Proof. intros x q. reflexivity. Qed.

Lemma every_needs_app : forall q1 q2,
    every_needs (q1 ++ q2) = andb (every_needs q1) (every_needs q2).
Proof.
  intros q1. induction q1 as [|x rest IH]; intros q2.
  - cbn [app every_needs forallb]. reflexivity.
  - cbn [app]. rewrite !every_needs_cons, IH. apply andb_assoc.
Qed.

Lemma insert_tpri_keeps_the_needs_positive : forall who q,
    every_needs q = true -> Nat.ltb 0 (w_need who) = true ->
    every_needs (insert_tpri who q) = true.
Proof.
  intros who q. revert who.
  induction q as [|x rest IH]; intros who H P.
  - cbn [insert_tpri]. rewrite every_needs_cons, P. reflexivity.
  - destruct (Nat.ltb (w_pri who) (w_pri x)) eqn:Pr.
    + rewrite (insert_tpri_puts_the_better_task_first who x rest Pr).
      rewrite every_needs_cons. apply andb_true_iff. split; [ exact P | exact H ].
    + rewrite every_needs_cons in H. apply andb_true_iff in H. destruct H as [Nx Hrest].
      rewrite (insert_tpri_tie_defers who x rest Pr).
      rewrite every_needs_cons, Nx. apply andb_true_iff.
      split; [ reflexivity | apply (IH who Hrest P) ].
Qed.

Lemma sem_enqueue_keeps_the_needs_positive : forall tpri who q,
    every_needs q = true -> Nat.ltb 0 (w_need who) = true ->
    every_needs (sem_enqueue tpri who q) = true.
Proof.
  intros tpri who q H P. unfold sem_enqueue. destruct tpri.
  - apply insert_tpri_keeps_the_needs_positive. exact H. exact P.
  - rewrite every_needs_app, H, every_needs_cons, P. reflexivity.
Qed.

(* Reading the invariant off a freshly built cell, so that no projection of a
 * constructor is left for a linear-arithmetic tactic to relate. *)
Lemma sem_wf_of_a_cell : forall i mx g tp nd cnt q,
    sem_wf (mk_semcb i mx g tp nd cnt q) = andb (Nat.leb cnt mx) (every_needs q).
Proof. intros i mx g tp nd cnt q. reflexivity. Qed.

Lemma a_blocking_wait_keeps_the_invariant : forall who c,
    sem_wf c = true -> Nat.ltb 0 (w_need who) = true ->
    sem_wf (sem_block who c) = true.
Proof.
  intros who c W P. unfold sem_block. rewrite sem_wf_of_a_cell.
  apply andb_true_iff. split.
  - apply Nat.leb_le. apply a_wellformed_cell_is_within_its_ceiling. exact W.
  - apply sem_enqueue_keeps_the_needs_positive.
    + apply a_wellformed_cell_has_positive_needs. exact W.
    + exact P.
Qed.

Lemma wai_step_preserves_the_invariant : forall mask t who c,
    sem_wf c = true -> Nat.ltb 0 (w_need who) = true ->
    sem_wf (fst (sem_wai mask t who c)) = true.
Proof.
  intros mask t who c W P.
  assert (U : sc_cnt c <= sc_max c) by (apply a_wellformed_cell_is_within_its_ceiling; exact W).
  assert (N : every_needs (sc_wait c) = true)
    by (apply a_wellformed_cell_has_positive_needs; exact W).
  unfold sem_wai.
  destruct (diswai_of (masked_for mask WO_SEM) (sc_nodis c)).
  - cbn [fst]. exact W.
  - destruct (sem_claimed (sc_gran c)
                          (top_of_queue (sc_tpri c) (w_pri who) (sc_wait c))
                          (sc_cnt c) (w_need who)).
    + cbn [fst]. unfold sem_take. rewrite sem_wf_of_a_cell. apply andb_true_iff. split.
      * apply Nat.leb_le. lia.
      * exact N.
    + destruct t; cbn [fst tmo_blocks].
      * exact W.
      * apply a_blocking_wait_keeps_the_invariant; assumption.
      * apply a_blocking_wait_keeps_the_invariant; assumption.
Qed.

(* A first FIFO caller takes the only unit; a second one blocks and is written at
 * the TAIL, and both the count and the queue move exactly as the C says. *)
Example a_fifo_wait_takes_then_queues :
    sem_wai 0 TMO_REL (mk_who 7 1 3) (mk_semcb 1 4 false false false 1 nil)
    = (mk_semcb 1 4 false false false 0 nil, E_OK) /\
    sem_wai 0 TMO_REL (mk_who 8 1 2) (mk_semcb 1 4 false false false 0 [mk_who 7 1 3])
    = (mk_semcb 1 4 false false false 0 [mk_who 7 1 3; mk_who 8 1 2], E_TMOUT).
Proof. split; vm_compute; reflexivity. Qed.

(* The two attributes the queue order depends on, on the shipped shapes: a TA_TPRI
 * caller with a strictly better priority takes the head place and the units, a
 * caller tied with the head does not (wait.c:196-207). *)
Example tpri_cuts_in_only_on_a_strict_improvement :
    sem_wai 0 TMO_REL (mk_who 8 1 3) (mk_semcb 1 4 false true false 1 [mk_who 7 1 5])
    = (mk_semcb 1 4 false true false 0 [mk_who 7 1 5], E_OK) /\
    sem_wai 0 TMO_REL (mk_who 8 1 5) (mk_semcb 1 4 false true false 0 [mk_who 7 1 5])
    = (mk_semcb 1 4 false true false 0 [mk_who 7 1 5; mk_who 8 1 5], E_TMOUT).
Proof. split; vm_compute; reflexivity. Qed.

(* A poll is refused by the same claim test and reports the same figure as a
 * blocking call that queued -- the difference is only in the queue (§12.4). *)
Example a_poll_and_a_wait_differ_only_in_the_queue :
    snd (sem_wai 0 TMO_POLL (mk_who 8 1 2) (mk_semcb 1 4 false false false 0 [mk_who 7 1 3]))
    = snd (sem_wai 0 TMO_REL (mk_who 8 1 2) (mk_semcb 1 4 false false false 0 [mk_who 7 1 3])) /\
    fst (sem_wai 0 TMO_POLL (mk_who 8 1 2) (mk_semcb 1 4 false false false 0 [mk_who 7 1 3]))
    = mk_semcb 1 4 false false false 0 [mk_who 7 1 3].
Proof. split; vm_compute; reflexivity. Qed.

(* The guard order of §14.5, computed: the wait-disable refusal beats a claim
 * that would have succeeded, because semaphore.c:308 precedes :313 -- and the
 * object's own TA_NODISWAI bit beats the refusal. *)
Example the_wait_disable_guard_precedes_the_claim :
    sem_wai ttw_sem TMO_FEVR (mk_who 8 1 2) (mk_semcb 1 4 false false false 5 nil)
    = (mk_semcb 1 4 false false false 5 nil, E_DISWAI) /\
    sem_wai ttw_sem TMO_FEVR (mk_who 8 1 2) (mk_semcb 1 4 false false true 5 nil)
    = (mk_semcb 1 4 false false true 4 nil, E_OK).
Proof. split; vm_compute; reflexivity. Qed.

(* A signal spends the units it handed in, in queue order.  Without TA_CNT the
 * walk breaks at the first waiter it cannot satisfy (semaphore.c:254-256); with
 * TA_CNT it continues past that waiter and releases a later one it can satisfy
 * (:253-254), leaving the unsatisfied task queued in its original place. *)
Example a_fifo_signal_stops_and_a_granular_one_steps_over :
    snd (sem_sig_step 3 (mk_semcb 1 8 false false false 0 [mk_who 7 2 3; mk_who 8 2 4]))
    = [mk_who 7 2 3] /\
    fst (sem_sig_step 3 (mk_semcb 1 8 false false false 0 [mk_who 7 2 3; mk_who 8 2 4]))
    = mk_semcb 1 8 false false false 1 [mk_who 8 2 4] /\
    snd (sem_sig_step 3 (mk_semcb 1 8 true false false 0 [mk_who 7 5 3; mk_who 8 1 4]))
    = [mk_who 8 1 4] /\
    fst (sem_sig_step 3 (mk_semcb 1 8 true false false 0 [mk_who 7 5 3; mk_who 8 1 4]))
    = mk_semcb 1 8 true false false 2 [mk_who 7 5 3].
Proof. repeat split; vm_compute; reflexivity. Qed.

(* The ceiling guard, computed against §14.5's cascade: three units into a
 * balance of 3 under a ceiling of 5 overflow, two do not -- and the receipt is
 * E_QOVR, which no other service in this family can produce. *)
Example the_ceiling_guard_is_the_only_state_dependent_one :
    first_bad (sem_sig_guards true 1 3 5 3) = Some E_QOVR /\
    first_bad (sem_sig_guards true 1 2 5 3) = None /\
    first_bad (sem_sig_guards false 1 3 5 3) = Some E_NOEXS /\
    first_bad (sem_sig_guards true 0 2 5 3) = Some E_ID /\
    first_bad (sem_sig_guards true 1 0 5 3) = Some E_PAR.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* The two encodings of this family agree: §6's sem, the projection of a full
 * control block, and the cell the walk moves are one object. *)
Example the_projection_is_the_shipped_shape :
    sem_view (mk_semcb 1 8 false false false 3 [mk_who 7 2 3; mk_who 8 2 4])
    = mk_sem 1 3 [7; 8] /\
    sem_view (mk_semcb 0 1 false false false 0 nil) = free_sem.
Proof. split; reflexivity. Qed.

(* ── 15. Two rings: the B-TRON message queue and the T-Kernel byte buffer ── *)

(* BTRON 3.20 gives a thread domain one message queue (src/kernel/ipc_msg.c) and
 * T-Kernel gives a pair of tasks one byte buffer (src/kernel/messagebuf.c).  The
 * two are the same construction read at two granularities: a fixed ring of
 * cells plus a cursor pair, where one API's cell is a whole message and the
 * other's is a byte.  They differ in exactly two ways, and both matter.
 *
 *   - The B-TRON receive is SELECTIVE.  rcv_msg(pid, msg, mask, tmo) scans the
 *     queue for the first message whose type bit is set in mask (ipc_msg.c:93-96)
 *     and removes THAT one -- from the middle of the queue, by shifting the
 *     survivors one cell back towards the head (:100-104).  _tk_rcv_mbf has no
 *     mask of any kind and always takes the front (:messagebuf.c:458-460).
 *   - The B-TRON ring counts MESSAGES, so a dequeue costs one cell; the message
 *     buffer counts BYTES and rounds each message up to a 4-byte boundary
 *     (messagebuf.c:105), so what a store costs is not what its own free-space
 *     test charged for: mbf_free admits a message at HEADERSZ+msgsz bytes (:113)
 *     while msg_to_mbf debits HEADERSZ+ROUNDSZ(msgsz) (:133).  15.5 shows the
 *     debt can exceed the admitted space -- 5 bytes admitted against 8 charged --
 *     and that the two nevertheless agree, because _tk_cre_mbf rounds the buffer
 *     itself to a multiple of 4 (:273) and every debit is a multiple of 4, so
 *     frbufsz can never sit at an unaligned value like 9.  An under-charge that
 *     the create-time alignment rescues, not a defect.
 *
 * The fact that makes this section provable is a hole in the C: nothing in
 * ipc_msg.c ever assigns to g_mailboxes[pid].head.  The memset at :35 puts 0
 * there and every later mention -- :94, :101, :102, :106 -- reads it.  So the
 * consumer's window is always the contiguous prefix of the array, the ring
 * never actually wraps for a reader, and the head is an inert coordinate. *)

(* ── 15.1 The message envelope and its type mask ────────────────── *)

(* message.h:47-51 is a three-field envelope.  The kernel reads exactly one of
 * the fields: msg_type decides the mask test, and msg_size plus the payload
 * union are copied verbatim in (:60) and out (:97) and never inspected.  bm_data
 * stands for the payload -- it has to be present, because 15.2's interesting
 * theorem is about the cell a dequeue forgets to clear. *)
Record bmsg : Set := mk_bmsg {
    bm_type : nat;            (* W msg_type, 1..31 (message.h:48) *)
    bm_size : nat;            (* W msg_size *)
    bm_data : nat             (* MSGBODY msg_body, one word read as the model's payload *)
  }.

(* ipc_msg.c:35 zeroes the whole mailbox, so a cell that has never been written
 * reads as this value -- with a type no sender could have stored. *)
Definition dead_cell : bmsg := mk_bmsg 0 0 0.

(* message.h:32, MSGMASK(t) = 1U << ((t) - 1).  *)
Definition msgmask (t : nat) : nat := Nat.pow 2 (Nat.pred t).

(* ipc_msg.c:48, "msg->msg_type < 1 || msg->msg_type > 31". *)
Definition type_in_range (t : nat) : bool :=
  andb (Nat.leb msg_type_min t) (Nat.ltb t (S msg_type_max)).

Lemma type_in_range_iff : forall t,
    type_in_range t = true <-> msg_type_min <= t /\ t < S msg_type_max.
Proof.
  unfold type_in_range, msg_type_min, msg_type_max.
  intros t. split.
  - intros H. apply andb_true_iff in H. destruct H as [A B].
    apply Nat.leb_le in A. apply Nat.ltb_lt in B. split; assumption.
  - intros [A B]. rewrite andb_true_iff. split.
    + apply Nat.leb_le. exact A.
    + apply Nat.ltb_lt. exact B.
Qed.

Lemma pow2_is_positive : forall k, Nat.ltb 0 (Nat.pow 2 k) = true.
Proof.
  intros k. apply Nat.ltb_lt. induction k as [|k IH]; cbn [Nat.pow]; lia.
Qed.

Lemma an_in_range_type_has_a_mask_bit : forall t,
    type_in_range t = true -> Nat.ltb 0 (msgmask t) = true.
Proof. intros t _. apply pow2_is_positive. Qed.

(* ipc_msg.c:96, "(mask == 0) || (mask & MSGMASK(mtype))".  A zero mask is a
 * wildcard rather than an empty set -- the one place in this corpus where
 * "no bits" means "all messages". *)
Definition masked_accepts (mask t : nat) : bool :=
  orb (Nat.eqb mask 0) (Nat.ltb 0 (Nat.land mask (msgmask t))).

Lemma a_zero_mask_accepts_every_type : forall t, masked_accepts 0 t = true.
Proof. intros t. unfold masked_accepts. cbn [Nat.eqb]. reflexivity. Qed.

Lemma a_nonzero_mask_asks_only_about_its_bits : forall mask t,
    Nat.ltb 0 mask = true ->
    masked_accepts mask t = Nat.ltb 0 (Nat.land mask (msgmask t)).
Proof.
  intros mask t H. unfold masked_accepts.
  destruct (Nat.eqb mask 0) eqn:E.
  - apply Nat.eqb_eq in E. rewrite <- E. cbn [Nat.land Nat.pow Nat.pred].
    apply Nat.ltb_lt in H. lia.
  - reflexivity.
Qed.

(* The two readings of a mask, computed.  MS_TYPE1..MS_TYPE7 are 25..31
 * (message.h:24-30), so the highest legal bit is bit 30 and MSGMASK needs no
 * wider a word than the UW it is stored in. *)
Example a_mask_of_two_types_accepts_exactly_those_two :
    masked_accepts (msgmask 2 + msgmask 5) 2 = true /\
    masked_accepts (msgmask 2 + msgmask 5) 5 = true /\
    masked_accepts (msgmask 2 + msgmask 5) 3 = false /\
    masked_accepts (msgmask 2 + msgmask 5) 0 = false.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* ── 15.2 The slot ring, and the coordinate that never moves ────── *)

(* ipc_msg.c:17-25, one mailbox per IPC pid: the array of cells plus the three
 * integers the C keeps beside them.  count is a stored field in C and a derived
 * quantity here, because both operations move it by exactly one and no other
 * statement writes it. *)
Record msg_ring : Set := mk_mring {
    mr_slots : list bmsg;     (* MESSAGE messages[64], in SLOT order *)
    mr_head  : nat;           (* int head   :19 *)
    mr_tail  : nat;           (* int tail   :20 *)
    mr_count : nat            (* int count  :21 *)
  }.

(* The window the API can reach and the cells outside it.  :93-94 reads cell
 * (head + i) mod 64 for i < count, and head is 0, so the reachable cells are the
 * first count ones -- in queue order, because nothing in this file reorders
 * them.  Both are structurally recursive on the ARRAY, not on the index: that is
 * what keeps them a rewriteable application while the index is still a variable,
 * instead of a stuck match that no lemma can name. *)
Fixpoint win (n : nat) (l : list bmsg) {struct l} : list bmsg :=
  match l with
  | nil => nil
  | a :: rest => match n with
                 | 0 => nil
                 | S k => a :: win k rest
                 end
  end.

Fixpoint cells_after (n : nat) (l : list bmsg) {struct l} : list bmsg :=
  match l with
  | nil => nil
  | a :: rest => match n with
                 | 0 => l
                 | S k => cells_after k rest
                 end
  end.

Lemma win_nil : forall n, win n nil = nil.
Proof. intros n. reflexivity. Qed.

Lemma win_cons : forall k a rest, win (S k) (a :: rest) = a :: win k rest.
Proof. intros k a rest. reflexivity. Qed.

Lemma win_at_zero : forall l, win 0 l = nil.
Proof. destruct l; reflexivity. Qed.

Lemma cells_after_nil : forall n, cells_after n nil = nil.
Proof. intros n. reflexivity. Qed.

Lemma cells_after_cons : forall k a rest,
    cells_after (S k) (a :: rest) = cells_after k rest.
Proof. intros k a rest. reflexivity. Qed.

Lemma cells_after_at_zero : forall l, cells_after 0 l = l.
Proof. destruct l; reflexivity. Qed.

Definition mqueue (r : msg_ring) : list bmsg := win (mr_count r) (mr_slots r).

(* What the initialisation (:31-43) establishes and both operations preserve.
 * The last clause is the ring coordinate: tail is not independent data, it is
 * (head + count) mod 64, and every theorem 15.2.2 proves is a consequence. *)
Definition ring_ok (r : msg_ring) : bool :=
  andb (Nat.eqb (length (mr_slots r)) msg_ring_cap)
       (andb (Nat.eqb (mr_head r) 0)
             (andb (Nat.leb (mr_count r) msg_ring_cap)
                   (Nat.eqb (mr_tail r)
                            (Nat.modulo (Nat.add (mr_head r) (mr_count r)) msg_ring_cap)))).


(* The one cell write, :60 and :103.  Again structurally recursive on the array:
 * a write past the end of the list runs off it and returns it unchanged, which is
 * what makes the capacity guard a guard rather than a formality. *)
Fixpoint slot_write (i : nat) (m : bmsg) (l : list bmsg) {struct l} : list bmsg :=
  match l with
  | nil => nil
  | a :: rest => match i with
                 | 0 => m :: rest
                 | S k => a :: slot_write k m rest
                 end
  end.
Lemma slot_write_off_the_end : forall i (m : bmsg), slot_write i m nil = nil.
Proof. intros i m. reflexivity. Qed.

Lemma slot_write_at_the_cursor : forall m (a : bmsg) rest,
    slot_write 0 m (a :: rest) = m :: rest.
Proof. intros m a rest. reflexivity. Qed.

Lemma slot_write_past_the_cursor : forall k m (a : bmsg) rest,
    slot_write (S k) m (a :: rest) = a :: slot_write k m rest.
Proof. intros k m a rest. reflexivity. Qed.

Lemma slot_write_beyond_is_inert : forall i (m : bmsg) l,
    length l <= i -> slot_write i m l = l.
Proof.
  intros i m l. revert i. induction l as [|a rest IH]; intros i H.
  - reflexivity.
  - destruct i as [|k].
    + cbn [length Nat.leb] in H. lia.
    + rewrite slot_write_past_the_cursor. f_equal. cbn [length] in H |- *.
      apply IH. lia.
Qed.

Lemma slot_write_keeps_the_length : forall i (m : bmsg) l,
    length (slot_write i m l) = length l.
Proof.
  intros i m l. revert i. induction l as [|a rest IH]; intros i; cbn [slot_write length].
  - reflexivity.
  - destruct i as [|k]; cbn [slot_write length]; [ reflexivity | rewrite IH; reflexivity ].
Qed.

Lemma min_succ : forall a b, Nat.min (S a) (S b) = S (Nat.min a b).
Proof. intros a b. cbn [Nat.min]. destruct (Nat.leb a b); reflexivity. Qed.

Lemma win_length : forall n l, length (win n l) = Nat.min n (length l).
Proof.
  intros n l. revert n. induction l as [|a rest IH]; intros n.
  - destruct n; cbn [win length Nat.min]; reflexivity.
  - destruct n as [|k]; cbn [win length].
    + cbn [Nat.min]. reflexivity.
    + rewrite min_succ, IH. reflexivity.
Qed.

Lemma cells_after_length : forall n l, length (cells_after n l) = Nat.sub (length l) n.
Proof.
  intros n l. revert n. induction l as [|a rest IH]; intros n.
  - destruct n; cbn [cells_after length Nat.sub]; reflexivity.
  - destruct n as [|k]; cbn [cells_after length Nat.sub].
    + reflexivity.
    + rewrite IH. reflexivity.
Qed.

Lemma win_cells_after_split : forall n l, win n l ++ cells_after n l = l.
Proof.
  intros n l. revert n. induction l as [|a rest IH]; intros n.
  - reflexivity.
  - destruct n as [|k]; cbn [win cells_after app].
    + destruct rest; reflexivity.
    + f_equal. apply IH.
Qed.

Lemma win_at_length : forall q rest, win (length q) (q ++ rest) = q.
Proof.
  intros q. induction q as [|a rest IH]; intros rest2.
  - rewrite win_at_zero. reflexivity.
  - cbn [app length win]. rewrite IH. reflexivity.
Qed.

Lemma cells_after_at_length : forall q rest, cells_after (length q) (q ++ rest) = rest.
Proof.
  intros q. induction q as [|a rest IH]; intros rest2.
  - rewrite cells_after_at_zero. reflexivity.
  - cbn [app length cells_after]. rewrite IH. reflexivity.
Qed.

Lemma win_slot_write_at_length : forall n (m : bmsg) l,
    n < length l -> win (S n) (slot_write n m l) = win n l ++ [m].
Proof.
  intros n m l. revert n. induction l as [|a rest IH]; intros n H.
  - cbn [length Nat.leb] in H. lia.
  - destruct n as [|k].
    + rewrite slot_write_at_the_cursor, win_cons, win_at_zero, win_at_zero.
      cbn [app]. reflexivity.
    + assert (Hk : k < length rest) by (cbn [length] in H |- *; lia).
      rewrite slot_write_past_the_cursor, win_cons, (IH k Hk), win_cons. reflexivity.
Qed.

(* ── 15.2.1 The two operations, as the C writes them ────────────── *)

(* ipc_msg.c:60-62: write at the cursor, advance it modulo the capacity, count
 * up. *)
Definition snd_store (m : bmsg) (r : msg_ring) : msg_ring :=
  mk_mring (slot_write (mr_tail r) m (mr_slots r))
           (mr_head r)
           (Nat.modulo (S (mr_tail r)) msg_ring_cap)
           (S (mr_count r)).

(* The compaction loop, :100-104, and the cell it leaves behind.  The loop runs
 * from i to count-2, so cell count-1 is never written and keeps the message that
 * used to be the last one. *)
Fixpoint last_of (d : bmsg) (l : list bmsg) : bmsg :=
  match l with nil => d | a :: rest => last_of a rest end.

Fixpoint remove_at (i : nat) (l : list bmsg) {struct l} : list bmsg :=
  match l with
  | nil => nil
  | a :: rest => match i with
                 | 0 => rest
                 | S k => a :: remove_at k rest
                 end
  end.

Lemma remove_at_nil : forall i, remove_at i nil = nil.
Proof. intros i. reflexivity. Qed.

Lemma remove_at_head : forall (a : bmsg) rest, remove_at 0 (a :: rest) = rest.
Proof. intros a rest. reflexivity. Qed.

Lemma remove_at_cons : forall k a rest,
    remove_at (S k) (a :: rest) = a :: remove_at k rest.
Proof. intros k a rest. reflexivity. Qed.

(* :97-106: hand the matched cell out, pull every survivor one cell back towards
 * the head, count down, and REBUILD tail from the coordinate. *)
Definition rcv_shift (i : nat) (r : msg_ring) : msg_ring :=
  mk_mring (remove_at i (mqueue r)
                ++ last_of dead_cell (mqueue r) :: cells_after (mr_count r) (mr_slots r))
           (mr_head r)
           (Nat.modulo (Nat.add (mr_head r) (Nat.pred (mr_count r))) msg_ring_cap)
           (Nat.pred (mr_count r)).

(* The compaction loop takes one item out of the middle and leaves the two
 * halves exactly where they were: everything before the index, then everything
 * after it. *)
Lemma remove_at_is_the_split : forall i l, i < length l ->
    remove_at i l = win i l ++ cells_after (S i) l.
Proof.
  intros i l. revert i. induction l as [|a rest IH]; intros i H.
  - cbn [length Nat.leb] in H. lia.
  - destruct i as [|k].
    + rewrite remove_at_head, win_at_zero, cells_after_cons, cells_after_at_zero.
      reflexivity.
    + assert (Hk : k < length rest) by (cbn [length] in H |- *; lia).
      rewrite remove_at_cons, win_cons, cells_after_cons, (IH k Hk). reflexivity.
Qed.

(* The item that leaves the queue is the one at the index. *)
Lemma cells_after_is_the_item_and_the_tail : forall i l, i < length l ->
    exists m, cells_after i l = m :: cells_after (S i) l.
Proof.
  intros i l. revert i. induction l as [|a rest IH]; intros i H.
  - cbn [length Nat.leb] in H. lia.
  - destruct i as [|k].
    + exists a. cbn [cells_after]. rewrite cells_after_at_zero. reflexivity.
    + assert (Hk : k < length rest) by (cbn [length] in H |- *; lia).
      destruct (IH k Hk) as [m Cm]. exists m.
      cbn [cells_after]. exact Cm.
Qed.

Lemma remove_at_splits : forall i l, i < length l ->
    exists m, l = win i l ++ m :: cells_after (S i) l
              /\ remove_at i l = win i l ++ cells_after (S i) l.
Proof.
  intros i l H. destruct (cells_after_is_the_item_and_the_tail i l H) as [m Cm].
  exists m. split.
  - rewrite <- Cm. symmetry. apply win_cells_after_split.
  - apply remove_at_is_the_split. exact H.
Qed.

Lemma remove_at_length : forall i l,
    i < length l -> length (remove_at i l) = Nat.pred (length l).
Proof.
  intros i l H. rewrite remove_at_is_the_split; [ | exact H ].
  rewrite length_app, win_length, cells_after_length.
  assert (M : Nat.min i (length l) = i) by (apply Nat.min_l; lia).
  rewrite M. lia.
Qed.

(* ── 15.2.2 The head is an inert coordinate ─────────────────────── *)

(* ipc_msg.c has four reads of mb->head (:94, :101, :102, :106) and no write to
 * it.  Both operations hand the field back unchanged, and that pair of refl
 * claims is the whole reason the window is a prefix rather than a wrap. *)
Lemma snd_store_leaves_the_head_alone : forall m r, mr_head (snd_store m r) = mr_head r.
Proof. intros m r. reflexivity. Qed.

Lemma rcv_shift_leaves_the_head_alone : forall i r, mr_head (rcv_shift i r) = mr_head r.
Proof. intros i r. reflexivity. Qed.

(* The four clauses of ring_ok, named once so the theorems below read as
 * statements about the queue and the cursor rather than about boolean tests. *)
Lemma ring_ok_parts : forall r, ring_ok r = true ->
    length (mr_slots r) = msg_ring_cap /\ mr_head r = 0
    /\ mr_count r <= msg_ring_cap
    /\ mr_tail r = Nat.modulo (Nat.add (mr_head r) (mr_count r)) msg_ring_cap.
Proof.
  intros r H. unfold ring_ok in H.
  apply andb_true_iff in H. destruct H as [A H2].
  apply andb_true_iff in H2. destruct H2 as [B H3].
  apply andb_true_iff in H3. destruct H3 as [C D].
  apply Nat.eqb_eq in A. apply Nat.eqb_eq in B. apply Nat.leb_le in C.
  apply Nat.eqb_eq in D. repeat split.
  - exact A. - exact B. - exact C. - exact D.
Qed.

(* ── 15.2.3 What the two operations do to the reachable window ───── *)

(* The send appends to the queue: :60 writes at tail, and tail is the first cell
 * the window does not cover. *)
Lemma snd_store_appends : forall (m : bmsg) r,
    ring_ok r = true -> mr_count r < msg_ring_cap ->
    mqueue (snd_store m r) = mqueue r ++ [m].
Proof.
  intros m r H OK. destruct (ring_ok_parts r H) as [A [B [C D]]].
  cbn [mr_count] in OK.
  unfold snd_store, mqueue. cbn [mr_slots mr_count].
  rewrite B in D. cbn [Nat.add] in D.
  rewrite (Nat.mod_small (mr_count r) msg_ring_cap OK) in D.
  rewrite D. apply win_slot_write_at_length. rewrite A. exact OK.
Qed.


(* The receive deletes exactly the cell the scan matched, and nothing else moves
 * relative to its neighbours. *)
Lemma rcv_shift_removes_the_index : forall i r,
    ring_ok r = true -> i < mr_count r ->
    mqueue (rcv_shift i r) = remove_at i (mqueue r).
Proof.
  intros i r H LT. destruct (ring_ok_parts r H) as [A [B [C D]]].
  unfold rcv_shift, mqueue. cbn [mr_slots mr_count mr_head].
  assert (M : Nat.min (mr_count r) msg_ring_cap = mr_count r)
    by (apply Nat.min_l; exact C).
  assert (WL : length (win (mr_count r) (mr_slots r)) = mr_count r).
  { rewrite win_length, A, M. reflexivity. }
  replace (Nat.pred (mr_count r))
    with (length (remove_at i (win (mr_count r) (mr_slots r)))).
  - rewrite win_at_length. reflexivity.
  - rewrite remove_at_length; [ rewrite WL; reflexivity | rewrite WL; exact LT ].
Qed.

Lemma a_dequeue_takes_one_item_out_of_the_middle : forall i r,
    ring_ok r = true -> i < mr_count r ->
    exists a b m, mqueue r = a ++ [m] ++ b /\ mqueue (rcv_shift i r) = a ++ b.
Proof.
  intros i r H LT. destruct (ring_ok_parts r H) as [A [B [C D]]].
  assert (M : Nat.min (mr_count r) msg_ring_cap = mr_count r)
    by (apply Nat.min_l; exact C).
  assert (LT' : i < length (mqueue r)).
  { unfold mqueue. rewrite win_length, A, M. exact LT. }
  rewrite rcv_shift_removes_the_index.
  { destruct (remove_at_splits i (mqueue r) LT') as [m [P Q]].
    exists (win i (mqueue r)). exists (cells_after (S i) (mqueue r)). exists m.
    split; [ exact P | exact Q ]. }
  { exact H. }
  { exact LT. }
Qed.

Lemma rcv_shift_consumes_one_message : forall i r,
    ring_ok r = true -> i < mr_count r ->
    length (mqueue (rcv_shift i r)) = Nat.pred (mr_count r).
Proof.
  intros i r H LT. destruct (ring_ok_parts r H) as [A [B [C D]]].
  assert (M : Nat.min (mr_count r) msg_ring_cap = mr_count r)
    by (apply Nat.min_l; exact C).
  assert (WL : length (mqueue r) = mr_count r).
  { unfold mqueue. rewrite win_length, A, M. reflexivity. }
  rewrite rcv_shift_removes_the_index.
  { rewrite remove_at_length; [ rewrite WL; reflexivity | rewrite WL; exact LT ]. }
  { exact H. }
  { exact LT. }
Qed.

(* THE PAYOFF.  Nothing in :100-106 erases a cell, so the cell immediately past
 * the new end still holds the message that used to be the last one, and the
 * cells beyond it are exactly what they were.  A dequeued payload therefore
 * stays in the mailbox array: unreachable through the API, because mqueue no
 * longer covers it, and plainly reachable by anyone who maps the array -- which
 * in this implementation every domain of the same process can do. *)
Lemma dequeue_leaves_one_stale_cell : forall i r,
    ring_ok r = true -> i < mr_count r ->
    cells_after (Nat.pred (mr_count r)) (mr_slots (rcv_shift i r))
    = last_of dead_cell (mqueue r) :: cells_after (mr_count r) (mr_slots r).
Proof.
  intros i r H LT. destruct (ring_ok_parts r H) as [A [B [C D]]].
  assert (M : Nat.min (mr_count r) msg_ring_cap = mr_count r)
    by (apply Nat.min_l; exact C).
  assert (WL : length (win (mr_count r) (mr_slots r)) = mr_count r).
  { rewrite win_length, A, M. reflexivity. }
  unfold rcv_shift, mqueue. cbn [mr_slots mr_count].
  replace (Nat.pred (mr_count r))
    with (length (remove_at i (win (mr_count r) (mr_slots r)))) at 1.
  - rewrite cells_after_at_length. reflexivity.
  - rewrite remove_at_length; [ rewrite WL; reflexivity | rewrite WL; exact LT ].
Qed.

(* Why :106 rebuilds the cursor instead of moving it: a full ring carries
 * count = 64 and therefore tail = (0 + 64) mod 64 = 0, so decrementing the
 * cursor would leave it at 0 where the next free cell is number 63. *)
Lemma rebuilding_the_cursor_differs_from_decrementing :
    Nat.modulo (Nat.add 0 (Nat.pred msg_ring_cap)) msg_ring_cap = Nat.pred msg_ring_cap /\
    Nat.modulo (Nat.pred (Nat.modulo (Nat.add 0 msg_ring_cap) msg_ring_cap)) msg_ring_cap = 0.
Proof. split; vm_compute; reflexivity. Qed.

(* The two halves of ring_ok, written for a record whose fields are already
 * separate, so that preserving the invariant is four obligations rather than a
 * fight with a chain of andb. *)
Lemma ring_ok_of_fields : forall s h t c,
    length s = msg_ring_cap -> h = 0 -> c <= msg_ring_cap ->
    t = Nat.modulo (Nat.add h c) msg_ring_cap ->
    ring_ok (mk_mring s h t c) = true.
Proof.
  intros s h t c A B C D.
  rewrite B in D. cbn [Nat.add] in D.
  unfold ring_ok. cbn [mr_slots mr_head mr_count mr_tail].
  rewrite A, B, D. cbn [Nat.add].
  apply andb_true_iff. split.
  - apply Nat.eqb_eq. reflexivity.
  - apply andb_true_iff. split.
    + apply Nat.eqb_eq. reflexivity.
    + apply andb_true_iff. split.
      * apply Nat.leb_le. exact C.
      * apply Nat.eqb_eq. reflexivity.
Qed.

Lemma ring_ok_fields : forall s h t c,
    ring_ok (mk_mring s h t c) = true ->
    length s = msg_ring_cap /\ h = 0 /\ c <= msg_ring_cap
    /\ t = Nat.modulo (Nat.add h c) msg_ring_cap.
Proof.
  intros s h t c H. unfold ring_ok in H.
  cbn [mr_slots mr_head mr_count mr_tail] in H.
  apply andb_true_iff in H. destruct H as [A H2].
  apply andb_true_iff in H2. destruct H2 as [B H3].
  apply andb_true_iff in H3. destruct H3 as [C D].
  apply Nat.eqb_eq in A. apply Nat.eqb_eq in B. apply Nat.leb_le in C.
  apply Nat.eqb_eq in D. repeat split; assumption.
Qed.

(* :55's guard is what keeps the count inside the array; both operations leave a
 * mailbox the initialisation could have produced. *)
Lemma push_keeps_the_ring_wellformed : forall (m : bmsg) r,
    ring_ok r = true -> mr_count r < msg_ring_cap -> ring_ok (snd_store m r) = true.
Proof.
  intros m r H OK. destruct r as [s h t c]. cbn [mr_count] in OK.
  destruct (ring_ok_fields s h t c H) as [A [B [C D]]].
  rewrite B in D. cbn [Nat.add] in D.
  rewrite (Nat.mod_small c msg_ring_cap OK) in D.
  unfold snd_store. cbn [mr_slots mr_head mr_count mr_tail].
  apply ring_ok_of_fields.
  - rewrite slot_write_keeps_the_length. exact A.
  - exact B.
  - exact OK.
  - rewrite D, B. cbn [Nat.add]. reflexivity.
Qed.

Lemma dequeue_keeps_the_ring_wellformed : forall i r,
    ring_ok r = true -> i < mr_count r -> ring_ok (rcv_shift i r) = true.
Proof.
  intros i r H LT. destruct r as [s h t c]. cbn [mr_count] in LT.
  destruct (ring_ok_fields s h t c H) as [A [B [C D]]].
  assert (M : Nat.min c msg_ring_cap = c) by (apply Nat.min_l; exact C).
  assert (WL : length (win c s) = c).
  { rewrite win_length, A, M. reflexivity. }
  unfold rcv_shift, mqueue. cbn [mr_slots mr_head mr_count mr_tail].
  apply ring_ok_of_fields.
  - rewrite length_app. cbn [length].
    rewrite remove_at_length.
    { rewrite WL, cells_after_length, A, <- Nat.sub_1_r. lia. }
    { rewrite WL. exact LT. }
  - exact B.
  - apply Nat.le_trans with (m := c); [ | exact C ].
    destruct c as [|k]; cbn [Nat.pred].
    + apply Nat.le_0_l.
    + apply Nat.le_succ_diag_r.
  - reflexivity.
Qed.

(* ── 15.2.4 The one writer validates the type, and the readers do not ── *)

(* :48 is the only range test in the file, and it is on the SEND side.  rcv_msg
 * never rechecks the type of what it finds (:96 reads it, :97 hands it over), so
 * the range of a queued type rests entirely on this invariant. *)
Definition types_in_range (q : list bmsg) : bool :=
  forallb (fun m => type_in_range (bm_type m)) q.

Lemma types_in_range_app : forall q1 q2,
    types_in_range (q1 ++ q2) = andb (types_in_range q1) (types_in_range q2).
Proof.
  intros q1. induction q1 as [|a rest IH]; intros q2; cbn [types_in_range forallb app].
  - reflexivity.
  - rewrite IH. destruct (type_in_range (bm_type a)); reflexivity.
Qed.

Lemma a_send_never_stores_a_type_its_receive_could_not_check : forall (m : bmsg) r,
    ring_ok r = true -> mr_count r < msg_ring_cap -> type_in_range (bm_type m) = true ->
    types_in_range (mqueue r) = true -> types_in_range (mqueue (snd_store m r)) = true.
Proof.
  intros m r H OK R T.
  rewrite snd_store_appends; [ | exact H | exact OK ].
  rewrite types_in_range_app, T. cbn [types_in_range forallb].
  rewrite R. reflexivity.
Qed.


(* ── 15.3 The masked scan: which message a receive takes ────────── *)

(* :96's test read off a whole cell.  The mask lives in the CALL, not in the
 * mailbox: no message carries a bit, its type does, and MSGMASK turns the type
 * into one (:96 against message.h:32). *)
Definition cell_accepts (mask : nat) (m : bmsg) : bool := masked_accepts mask (bm_type m).

Definition omap_succ (o : option nat) : option nat :=
  match o with None => None | Some k => Some (S k) end.

(* ipc_msg.c:93-96 is a linear walk i = 0 .. count-1 that stops at the first
 * cell passing :96.  That index is the whole service: :97 copies that cell into
 * the caller's MESSAGE and :100-104 deletes exactly it. *)
Fixpoint first_match (mask : nat) (q : list bmsg) : option nat :=
  match q with
  | nil => None
  | a :: rest => if cell_accepts mask a then Some O
                 else omap_succ (first_match mask rest)
  end.

Lemma first_match_nil : forall mask, first_match mask nil = None.
Proof. intros mask. reflexivity. Qed.

Lemma first_match_accepts : forall mask a rest,
    cell_accepts mask a = true -> first_match mask (a :: rest) = Some O.
Proof. intros mask a rest H. cbn [first_match]. rewrite H. reflexivity. Qed.

Lemma first_match_rejects : forall mask a rest,
    cell_accepts mask a = false ->
    first_match mask (a :: rest) = omap_succ (first_match mask rest).
Proof. intros mask a rest H. cbn [first_match]. rewrite H. reflexivity. Qed.

(* Reading past the end of the window yields the zeroed cell the memset puts in
 * an untouched slot, and nothing in the window can be read from outside it. *)
Lemma nth_in_range_is_in : forall q i,
    i < length q -> In (nth i q dead_cell) q.
Proof.
  intros q. induction q as [|a rest IH]; intros i H; cbn [length] in H.
  - lia.
  - destruct i as [|k]; cbn [nth].
    + left. reflexivity.
    + right. apply IH. lia.
Qed.

Lemma omap_succ_some : forall o i,
    omap_succ o = Some i -> exists k, o = Some k /\ i = S k.
Proof.
  intros o i H. destruct o as [k|].
  - exists k. split; [ reflexivity | ].
    cbn [omap_succ] in H. injection H as Hi. symmetry. exact Hi.
  - cbn [omap_succ] in H. discriminate H.
Qed.

(* The walk returns a position inside the window, that cell does pass the test,
 * and every cell ahead of it fails.  "First" is first in QUEUE order, which is
 * not arrival order once a mask is in play -- see the jump below. *)
Lemma the_scan_delivers_the_earliest_acceptable_cell : forall mask q i,
    first_match mask q = Some i ->
    i < length q
    /\ cell_accepts mask (nth i q dead_cell) = true
    /\ forall j, j < i -> cell_accepts mask (nth j q dead_cell) = false.
Proof.
  intros mask q. induction q as [|a rest IH]; intros i H.
  - rewrite first_match_nil in H. discriminate H.
  - destruct (cell_accepts mask a) eqn:Ha.
    + rewrite (first_match_accepts mask a rest Ha) in H.
      injection H as Hi. subst i. cbn [length nth]. repeat split.
      * lia.
      * exact Ha.
      * intros j Hj. exfalso. lia.
    + rewrite (first_match_rejects mask a rest Ha) in H.
      destruct (omap_succ_some (first_match mask rest) i H) as [k [Hk Hi]].
      subst i.
      destruct (IH k Hk) as [L [Acc Before]].
      cbn [length nth]. repeat split.
      -- lia.
      -- exact Acc.
      -- intros j Hj. destruct j as [|j'].
         ++ exact Ha.
         ++ cbn [nth]. apply Before. lia.
Qed.

(* The other half of :96's job: the walk gives up only when nothing passes. *)
Lemma a_walk_that_finds_nothing_has_skipped_everything : forall mask q,
    (forall m, In m q -> cell_accepts mask m = false) -> first_match mask q = None.
Proof.
  intros mask q. induction q as [|a rest IH]; intros H.
  - reflexivity.
  - destruct (cell_accepts mask a) eqn:Ha.
    + assert (HA : cell_accepts mask a = false) by (apply H; left; reflexivity).
      rewrite Ha in HA. discriminate HA.
    + rewrite (first_match_rejects mask a rest Ha).
      rewrite (IH (fun m Hin => H m (or_intror Hin))).
      reflexivity.
Qed.

(* :94's coordinate for the walk.  Nothing ever writes head (15's opening
 * observation), so under the ring invariant this is the identity on the range
 * the walk visits: the wrap the C guards against is unreachable here. *)
Definition cell_of (r : msg_ring) (i : nat) : nat :=
  Nat.modulo (Nat.add (mr_head r) i) msg_ring_cap.

Lemma cell_of_is_the_index : forall r i,
    ring_ok r = true -> i < mr_count r -> cell_of r i = i.
Proof.
  intros r i H LT. destruct (ring_ok_parts r H) as [A [B [C _]]].
  unfold cell_of. rewrite B. cbn [Nat.add].
  apply Nat.mod_small. apply Nat.lt_le_trans with (m := mr_count r).
  - exact LT.
  - exact C.
Qed.

(* The receive as the C leaves it: the matched index, the cell handed to the
 * caller, and the mailbox after :100-106. *)
Definition rcv_match (mask : nat) (r : msg_ring) : option nat :=
  first_match mask (mqueue r).

Definition rcv_take (mask : nat) (r : msg_ring) : option (bmsg * msg_ring) :=
  match rcv_match mask r with
  | Some i => Some (nth i (mqueue r) dead_cell, rcv_shift i r)
  | None => None
  end.

Lemma rcv_take_hit_is_the_matched_cell : forall mask r i,
    rcv_match mask r = Some i ->
    rcv_take mask r = Some (nth i (mqueue r) dead_cell, rcv_shift i r).
Proof.
  intros mask r i H. unfold rcv_take. rewrite H. reflexivity. Qed.

Lemma rcv_take_miss_is_a_miss : forall mask r,
    rcv_match mask r = None -> rcv_take mask r = None.
Proof. intros mask r H. unfold rcv_take. rewrite H. reflexivity. Qed.

(* ── 15.3.1 What the mask can and cannot do ─────────────────────── *)

(* mask == 0 is a WILDCARD, not an empty set (:96's first disjunct).  With it a
 * receive can never skip a cell, so the walk stops at the head and the dequeue
 * degenerates into a pop-front: the O(1) case of the copy-based design. *)
Lemma mask_zero_accepts_any_type : forall t, masked_accepts O t = true.
Proof.
  intros t. unfold masked_accepts. apply orb_true_iff. left.
  apply Nat.eqb_eq. reflexivity.
Qed.

Lemma the_wildcard_stops_at_the_front : forall a rest,
    first_match O (a :: rest) = Some O.
Proof.
  intros a rest. apply (first_match_accepts O a rest).
  unfold cell_accepts. apply mask_zero_accepts_any_type.
Qed.

Lemma the_window_is_the_count_under_the_ring_invariant : forall r,
    ring_ok r = true -> length (mqueue r) = mr_count r.
Proof.
  intros r H. unfold mqueue. rewrite win_length.
  destruct (ring_ok_parts r H) as [A [B [C _]]]. rewrite A.
  apply Nat.min_l. exact C.
Qed.

Lemma the_wildcard_matches_the_front_of_any_window : forall r,
    mqueue r <> nil -> rcv_match O r = Some O.
Proof.
  intros r Hq. unfold rcv_match.
  destruct (mqueue r) as [|a rest]; [ contradiction | apply the_wildcard_stops_at_the_front ].
Qed.

Lemma a_nonempty_ring_matches_zero_under_the_wildcard : forall r,
    ring_ok r = true -> mr_count r <> 0 -> rcv_match O r = Some O.
Proof.
  intros r H NZ. apply the_wildcard_matches_the_front_of_any_window.
  intros Hnil. apply NZ.
  rewrite <- (the_window_is_the_count_under_the_ring_invariant r H), Hnil. reflexivity.
Qed.

(* But a mask that is not 0 lets a receive reach PAST the front, and this is the
 * one place where the queue is not FIFO: an older message that does not match
 * stays queued while a newer one that does is delivered ahead of it. *)
Example a_masked_receive_reaches_past_an_older_message :
    let q := mk_bmsg 2 0 77 :: mk_bmsg 1 0 88 :: nil in
    cell_accepts (msgmask 1) (nth O q dead_cell) = false
    /\ first_match (msgmask 1) q = Some 1.
Proof. split; vm_compute; reflexivity. Qed.

(* Deleting that cell leaves the cells ahead of it exactly where they were and
 * pulls every cell behind it up by one: :100-104 is a deletion, not a rotation
 * and not a reorder. *)
(* Three reading facts about nth, stated as equations so the proofs below can
 * rewrite with them instead of letting cbn unfold a stuck nth. *)
Lemma nth_nil : forall j, nth j nil dead_cell = dead_cell.
Proof. intros j. destruct j; reflexivity. Qed.

Lemma nth_head : forall a rest, nth O (a :: rest) dead_cell = a.
Proof. reflexivity. Qed.

Lemma nth_tail : forall j a rest, nth (S j) (a :: rest) dead_cell = nth j rest dead_cell.
Proof. reflexivity. Qed.

(* Deleting that cell leaves the cells ahead of it exactly where they were and
 * pulls every cell behind it up by one: :100-104 is a deletion, not a rotation
 * and not a reorder. *)
Lemma remove_at_keeps_earlier_cells : forall i l j,
    j < i -> nth j (remove_at i l) dead_cell = nth j l dead_cell.
Proof.
  intros i. induction i as [|k IH]; intros l j H.
  - exfalso. lia.
  - destruct l as [|a rest]; [ reflexivity | ].
    cbn [remove_at]. destruct j as [|j']; [ rewrite !nth_head; reflexivity | ].
    rewrite !nth_tail. apply IH. lia.
Qed.

Lemma remove_at_shifts_later_cells : forall i l j,
    i <= j -> S j < length l ->
    nth j (remove_at i l) dead_cell = nth (S j) l dead_cell.
Proof.
  intros i. induction i as [|k IH]; intros l j LEJ LTJ.
  - destruct l as [|a rest]; [ rewrite nth_nil; reflexivity | ].
    rewrite nth_tail. reflexivity.
  - destruct l as [|a rest]; [ rewrite nth_nil; reflexivity | ].
    cbn [remove_at]. destruct j as [|j'].
    + exfalso. lia.
    + rewrite nth_tail. rewrite nth_tail. apply IH.
      * lia.
      * cbn [length] in LTJ. lia.
Qed.

(* The masked receive therefore invents, duplicates and reorders nothing: what it
 * hands back is a cell of the window, and what survives is the window with that
 * one cell deleted (15.2's rcv_shift_removes_the_index). *)
Lemma a_hit_returns_a_queued_cell : forall mask r i,
    rcv_match mask r = Some i ->
    exists m, In m (mqueue r) /\ m = nth i (mqueue r) dead_cell.
Proof.
  intros mask r i H.
  destruct (the_scan_delivers_the_earliest_acceptable_cell mask (mqueue r) i H)
    as [LT _].
  exists (nth i (mqueue r) dead_cell).
  split; [ apply nth_in_range_is_in; exact LT | reflexivity ].
Qed.

(* ── 15.3.2 The timeout figure chooses nothing ──────────────────── *)

(* tmo appears at :80-89 (the deadline) and at :114-128 (what to do after a
 * miss).  It appears nowhere inside the scan, so no two timeout figures can
 * select different messages: which message is decided by mask and window alone. *)
Definition rcv_choice (t : tmo) (mask : nat) (r : msg_ring) : option nat :=
  rcv_match mask r.

Example the_timeout_figure_never_selects_a_different_message :
    forall t1 t2 mask r, rcv_choice t1 mask r = rcv_choice t2 mask r.
Proof. intros t1 t2 mask r. reflexivity. Qed.

Inductive rcv_outcome : Type := R_OK | R_TMOUT | R_BLOCKED.

(* :114-116 against :120-128. *)
Definition miss_outcome (t : tmo) : rcv_outcome :=
  if tmo_blocks t then R_BLOCKED else R_TMOUT.

Definition rcv_service (t : tmo) (mask : nat) (r : msg_ring) : rcv_outcome :=
  match rcv_choice t mask r with
  | Some _ => R_OK
  | None => miss_outcome t
  end.

Lemma a_hit_is_the_same_service_whatever_the_timeout : forall i t1 t2 mask r,
    rcv_match mask r = Some i -> rcv_service t1 mask r = rcv_service t2 mask r.
Proof.
  intros i t1 t2 mask r H. unfold rcv_service, rcv_choice. rewrite H. reflexivity.
Qed.

(* A miss is the only place the figure is read, and it is read through the
 * blocking verdict alone: two timeouts that both block, or both do not, leave
 * a caller in the same position. *)
Lemma a_miss_decides_only_the_giving_up : forall t1 t2 mask r,
    tmo_blocks t1 = tmo_blocks t2 -> rcv_match mask r = None ->
    rcv_service t1 mask r = rcv_service t2 mask r.
Proof.
  intros t1 t2 mask r Hb Hm. unfold rcv_service, rcv_choice. rewrite Hm.
  unfold miss_outcome. rewrite Hb. reflexivity.
Qed.

Lemma a_poll_on_a_miss_times_out : forall mask r,
    rcv_match mask r = None -> rcv_service TMO_POLL mask r = R_TMOUT.
Proof. intros mask r H. unfold rcv_service, rcv_choice. rewrite H. reflexivity. Qed.

Example only_a_poll_gives_up_immediately :
    miss_outcome TMO_POLL = R_TMOUT
    /\ miss_outcome TMO_REL = R_BLOCKED
    /\ miss_outcome TMO_FEVR = R_BLOCKED.
Proof. repeat split; reflexivity. Qed.

Example a_miss_with_a_deadline_blocks_and_a_miss_as_a_poll_times_out :
    rcv_service TMO_POLL O (mk_mring nil O O O) = R_TMOUT
    /\ rcv_service TMO_REL (msgmask 1) (mk_mring nil O O O) = R_BLOCKED.
Proof. split; reflexivity. Qed.

(* rcv_msg's own reading of W tmo: zero polls, a positive figure is a relative
 * millisecond deadline, and ANY negative figure waits forever.  Unlike the
 * T-Kernel family of 3 and check.h:185, this service has no guard on the
 * timeout at all -- :71-72 check only the pointer and the pid -- so nothing
 * below -1 is refused and E_PAR can never come from tmo. *)
Definition btron_tmo (t : Z) : tmo :=
  if Z.ltb t Z0 then TMO_FEVR else if Z.eqb t Z0 then TMO_POLL else TMO_REL.

Lemma the_btron_poll_is_exactly_zero : forall t,
    Z.eqb t Z0 = true -> btron_tmo t = TMO_POLL.
Proof.
  intros t H. unfold btron_tmo.
  destruct (Z.ltb t Z0) eqn:HL.
  - exfalso. apply Z.ltb_lt in HL. apply Z.eqb_eq in H. lia.
  - rewrite H. reflexivity.
Qed.

Lemma any_negative_figure_waits_forever : forall t,
    Z.ltb t Z0 = true -> btron_tmo t = TMO_FEVR.
Proof. intros t H. unfold btron_tmo. rewrite H. reflexivity. Qed.

Example the_three_btron_timeout_cases :
    btron_tmo (Z.opp 5) = TMO_FEVR
    /\ btron_tmo Z0 = TMO_POLL
    /\ btron_tmo (Z.pos 7) = TMO_REL.
Proof. repeat split; reflexivity. Qed.

(* ── 15.3.3 The deadline arithmetic of :80-89 ───────────────────── *)

(* :83-84 build an absolute timespec from a relative millisecond figure:
 *
 *   ts.tv_sec  = now.tv_sec  + (tmo / 1000);
 *   ts.tv_nsec = (now.tv_usec + (tmo % 1000) * 1000) * 1000;
 *   if (ts.tv_nsec >= 1000000000L) { ts.tv_sec += 1; ts.tv_nsec -= 1000000000L; }
 *
 * The added time is split into whole seconds and a sub-second remainder, the
 * remainder is normalised by ONE conditional carry, and nothing is lost.  Those
 * four structural facts are what this section proves.
 *
 * The model reads the arithmetic in milliseconds rather than the C's
 * nanoseconds: dividing :84 and :85-88 through by 1000 twice turns
 * `(usec + rest*1000)*1000` compared with 1e9 into `now_rem + rest` compared
 * with 1000.  That unit reduction is INFERRED -- it is a rescaling of the same
 * test, not a transcription of a line -- while the four facts below are PROVEN
 * of the scaled model and are exactly the facts the unscaled one must satisfy.
 * Staying at ms scale also keeps every numeral here under 2000, so the proofs
 * use plain arithmetic instead of the folded representation large nats get. *)
Definition ms_per_s : nat := 1000.                                   (* :83, :84 *)

Definition deadline_sec (tmo : nat) : nat := tmo / ms_per_s.          (* :83 *)
Definition deadline_rem_ms (tmo : nat) : nat := tmo mod ms_per_s.     (* :84 *)
Definition deadline_rest (now_rem tmo : nat) : nat :=
  now_rem + deadline_rem_ms tmo.                                      (* :84 *)
Definition deadline_carry (now_rem tmo : nat) : bool :=
  Nat.leb ms_per_s (deadline_rest now_rem tmo).                       (* :85 *)
Definition deadline_extra_sec (now_rem tmo : nat) : nat :=
  if deadline_carry now_rem tmo then 1 else 0.                        (* :86 *)
Definition deadline_final_ms (now_rem tmo : nat) : nat :=
  deadline_rest now_rem tmo
  - (if deadline_carry now_rem tmo then ms_per_s else 0).             (* :87 *)

(* :83-84 split tmo without losing or inventing time. *)
Lemma the_millisecond_split_is_exact : forall tmo,
    ms_per_s * deadline_sec tmo + deadline_rem_ms tmo = tmo.
Proof.
  intros tmo. unfold deadline_sec, deadline_rem_ms, ms_per_s.
  assert (E : tmo = 1000 * (tmo / 1000) + tmo mod 1000) by (apply Nat.div_mod_eq).
  lia.
Qed.

(* :85's single conditional covers the whole overflow: a sub-second figure below
 * 1000 ms plus at most 999 ms of remainder is under two seconds, so one carry
 * suffices and the absence of a loop is correct. *)
Lemma the_remainder_needs_at_most_one_carry : forall now_rem tmo,
    now_rem < ms_per_s -> deadline_rest now_rem tmo < 2 * ms_per_s.
Proof.
  intros now_rem tmo H. unfold deadline_rest, deadline_rem_ms, ms_per_s in H |- *.
  assert (M : tmo mod 1000 < 1000) by (apply Nat.mod_upper_bound; lia).
  lia.
Qed.

(* Which way the conditional went, and what it therefore did. *)
Lemma carry_gives_a_whole_second : forall now_rem tmo,
    deadline_carry now_rem tmo = true -> ms_per_s <= deadline_rest now_rem tmo.
Proof.
  intros now_rem tmo HC.
  unfold deadline_carry, deadline_rest, deadline_rem_ms, ms_per_s in HC.
  apply Nat.leb_le in HC. exact HC.
Qed.

Lemma no_carry_leaves_a_sub_second : forall now_rem tmo,
    deadline_carry now_rem tmo = false -> deadline_rest now_rem tmo < ms_per_s.
Proof.
  intros now_rem tmo HC.
  unfold deadline_carry, deadline_rest, deadline_rem_ms, ms_per_s in HC.
  apply Nat.leb_gt in HC. exact HC.
Qed.

Lemma deadline_extra_when_carry : forall now_rem tmo,
    deadline_carry now_rem tmo = true -> deadline_extra_sec now_rem tmo = 1.
Proof. intros now_rem tmo HC. unfold deadline_extra_sec. rewrite HC. reflexivity. Qed.

Lemma deadline_extra_when_no_carry : forall now_rem tmo,
    deadline_carry now_rem tmo = false -> deadline_extra_sec now_rem tmo = 0.
Proof. intros now_rem tmo HC. unfold deadline_extra_sec. rewrite HC. reflexivity. Qed.

Lemma deadline_final_when_carry : forall now_rem tmo,
    deadline_carry now_rem tmo = true ->
    deadline_final_ms now_rem tmo = deadline_rest now_rem tmo - ms_per_s.
Proof. intros now_rem tmo HC. unfold deadline_final_ms. rewrite HC. reflexivity. Qed.

Lemma deadline_final_when_no_carry : forall now_rem tmo,
    deadline_carry now_rem tmo = false ->
    deadline_final_ms now_rem tmo = deadline_rest now_rem tmo.
Proof. intros now_rem tmo HC. unfold deadline_final_ms. rewrite HC. exact (Nat.sub_0_r _). Qed.

(* The normalised remainder is a legal sub-second figure, which is what makes the
 * timespec :88-89 hands to pthread_cond_timedwait well formed. *)
Lemma one_conditional_normalises : forall now_rem tmo,
    now_rem < ms_per_s -> deadline_final_ms now_rem tmo < ms_per_s.
Proof.
  intros now_rem tmo H.
  destruct (deadline_carry now_rem tmo) eqn:HC.
  - rewrite (deadline_final_when_carry now_rem tmo HC).
    assert (U : ms_per_s <= deadline_rest now_rem tmo)
      by (apply carry_gives_a_whole_second; exact HC).
    assert (B : deadline_rest now_rem tmo < 2 * ms_per_s)
      by (apply the_remainder_needs_at_most_one_carry; exact H).
    lia.
  - rewrite (deadline_final_when_no_carry now_rem tmo HC).
    apply (no_carry_leaves_a_sub_second now_rem tmo HC).
Qed.

(* The normalisation is lossless: the (:86,:87) pair names the same instant as
 * the un-normalised sum added to the whole seconds :83 took out.  No bound on
 * now_rem is needed here -- the carry branch is only taken when there really is
 * a whole second to subtract. *)
Lemma the_deadline_is_the_same_instant : forall now_rem tmo,
    ms_per_s * (deadline_sec tmo + deadline_extra_sec now_rem tmo)
    + deadline_final_ms now_rem tmo
    = deadline_rest now_rem tmo + ms_per_s * deadline_sec tmo.
Proof.
  intros now_rem tmo.
  destruct (deadline_carry now_rem tmo) eqn:HC.
  - rewrite (deadline_extra_when_carry now_rem tmo HC),
             (deadline_final_when_carry now_rem tmo HC).
    rewrite Nat.mul_add_distr_l, Nat.mul_1_r.
    assert (U : ms_per_s <= deadline_rest now_rem tmo)
      by (apply carry_gives_a_whole_second; exact HC).
    lia.
  - rewrite (deadline_extra_when_no_carry now_rem tmo HC),
             (deadline_final_when_no_carry now_rem tmo HC).
    lia.
Qed.

Example the_carry_case_is_a_figure_a_real_call_can_reach :
    deadline_carry 999 999 = true
    /\ deadline_extra_sec 999 999 = 1
    /\ deadline_final_ms 999 999 = 998.
Proof. repeat split; reflexivity. Qed.

Example the_no_carry_case_needs_no_normalisation :
    deadline_carry 250 400 = false
    /\ deadline_extra_sec 250 400 = 0
    /\ deadline_final_ms 250 400 = 650.
Proof. repeat split; reflexivity. Qed.

(* The test is `>=`, not `>`, so a remainder that lands exactly on a second
 * becomes a carry and a zero sub-second field rather than an illegal 1000. *)
Example a_remainder_of_exactly_one_second_carries :
    deadline_carry 1 999 = true
    /\ deadline_extra_sec 1 999 = 1
    /\ deadline_final_ms 1 999 = 0
    /\ deadline_sec 999 = 0.
Proof. repeat split; reflexivity. Qed.

(* :80 gates the whole deadline block on `tmo > 0`, which among the three figures
 * this service recognises is true of TMO_REL alone: a poll never builds a
 * timespec, and the forever path leaves `ts` uninitialised but never reads it,
 * because :126-128 waits with pthread_cond_wait.  Note this is the arithmetic
 * reading of the figure, whereas check.h:254-258 tests the sentinel; the two
 * tests disagree exactly on TMO_FEVR, and each is faithful to the code it models
 * (§3's positive_test_misclassifies_fevr). *)
Definition deadline_requested (t : tmo) : bool := Z.ltb Z0 (tmo_code t).

Lemma only_a_relative_figure_builds_a_deadline :
    deadline_requested TMO_REL = true
    /\ deadline_requested TMO_POLL = false
    /\ deadline_requested TMO_FEVR = false.
Proof. repeat split; reflexivity. Qed.

(* The figures that reach a wait are exactly the figures that block, and only
 * TMO_REL has a deadline there to time out against. *)
Lemma the_blocking_figures_are_the_figures_that_wait : forall t,
    tmo_blocks t = true ->
    match t with
    | TMO_REL => deadline_requested t = true
    | TMO_FEVR => deadline_requested t = false
    | TMO_POLL => False
    end.
Proof.
  destruct t; intros H; cbn [tmo_blocks] in H.
  - discriminate.
  - reflexivity.
  - reflexivity.
Qed.

(* ── 15.4 The receipts of this service: a second, flat error namespace ── *)

(* ipc_msg.c includes <btron/error.h> and returns its figures directly, so a
 * receipt from snd_msg, rcv_msg or chk_msg is a FLAT negative integer: there
 * E_PAR is -33 (error.h:21).  Every T-Kernel service §2 models hands back the
 * check.h main code scaled by 2^16 (errno.h:29), where E_PAR is -1114112.  One
 * spelling, two namespaces, and a caller that mixes the headers can only tell
 * them apart by the figure.  This section models the flat namespace and the
 * guard cascades that produce it: :46-58 for the send, :71-116 for the receive,
 * :132-134 for the check. *)

(* The five figures ipc_msg.c can hand back: E_OK (error.h:15), E_PAR (:21),
 * ER_ID, which :56 aliases to E_ID (:23), ER_NOSPC (:32) and E_TMOUT (:27).
 * E_SYS, E_NOMEM, E_NOSPT, E_RSVR, E_LIMIT, E_OBJ, E_NOEXS and E_BUSY ship in the
 * same header and ER_ADR ... ER_OVVR ship in its extended block, but no line of
 * this file can produce any of them -- they are outside the vocabulary rather
 * than forgotten.  ER_TIMEOUT (:48) is left out for the opposite reason: it is
 * not a sixth figure, it is E_TMOUT's other name, and 15.4.1 proves that. *)
Inductive ber : Type :=
  | BE_OK
  | BE_PAR
  | BE_ID
  | BE_NOSPC
  | BE_TMOUT.

Definition ber_code (b : ber) : Z :=
  match b with
  | BE_OK    => Z0
  | BE_PAR   => Z.opp 33
  | BE_ID    => Z.opp 35
  | BE_NOSPC => Z.opp 11
  | BE_TMOUT => Z.opp 69
  end.

Lemma ber_code_is_the_shipped_figure :
    ber_code BE_OK = Z0 /\ ber_code BE_PAR = Z.opp 33
    /\ ber_code BE_ID = Z.opp 35 /\ ber_code BE_NOSPC = Z.opp 11
    /\ ber_code BE_TMOUT = Z.opp 69.
Proof. repeat split; reflexivity. Qed.

Lemma ber_code_separates : forall b1 b2, ber_code b1 = ber_code b2 -> b1 = b2.
Proof.
  intros b1 b2 H. destruct b1, b2; try reflexivity.
  all: (vm_compute in H; discriminate H).
Qed.

(* types.h:19 is typedef int32_t ER, and every figure of either namespace fits
 * that word with room to spare: the collision between them is a naming hazard,
 * not an overflow. *)
Lemma every_flat_receipt_fits_the_shipped_int32 : forall b,
    Z.leb (Z.opp 2147483648) (ber_code b) = true.
Proof. destruct b; cbn [ber_code]; lia. Qed.

Lemma every_scaled_receipt_fits_the_shipped_int32 : forall e,
    Z.leb (Z.opp 2147483648) (er_code e) = true.
Proof. destruct e; vm_compute; reflexivity. Qed.

(* ── 15.4.1 The two vocabularies meet only at success ───────────── *)

(* A nonzero T-Kernel main code is at least 1, so its figure is at most -65536,
 * while the flat figures never go below -69.  The gap between the vocabularies
 * is wider than either of them. *)
Lemma er_nonzero_is_large : forall e,
    er_mer e <> 0 -> Z.leb (er_code e) (Z.opp 65536) = true.
Proof. intros e NE. unfold er_code. lia. Qed.

Lemma ber_is_small : forall b, Z.leb (Z.opp 69) (ber_code b) = true.
Proof. destruct b; cbn [ber_code]; lia. Qed.

Lemma the_two_nonzero_vocabularies_are_disjoint : forall e b,
    er_mer e <> 0 -> er_code e <> ber_code b.
Proof.
  intros e b NE EQ. apply er_nonzero_is_large in NE.
  assert (SB : Z.leb (Z.opp 69) (ber_code b) = true) by (apply ber_is_small).
  rewrite EQ in NE. lia.
Qed.

Lemma er_mer_is_zero_only_for_ok : forall e, er_mer e = 0 -> e = E_OK.
Proof. destruct e; cbn [er_mer]; intros H; try discriminate H; reflexivity. Qed.

(* So the single figure the namespaces share is the success code, and one spelled
 * name means two different numbers depending on which header the service
 * includes. *)
Lemma the_shared_figure_is_success_only :
    er_code E_OK = ber_code BE_OK
    /\ forall e b, er_code e = ber_code b -> e = E_OK /\ b = BE_OK.
Proof.
  split; [ reflexivity | intros e b H ].
  destruct (er_mer e) eqn:ZE.
  - assert (E : e = E_OK) by (apply er_mer_is_zero_only_for_ok; exact ZE).
    subst e. destruct b; cbn [ber_code] in H; try discriminate H.
    + split; reflexivity.
  - exfalso. apply (the_two_nonzero_vocabularies_are_disjoint e b).
    + intros EQ. rewrite EQ in ZE. discriminate ZE.
    + exact H.
Qed.

Lemma e_par_names_two_figures : er_code E_PAR <> ber_code BE_PAR.
Proof.
  intros H. unfold er_code, er_mer, ber_code in H. vm_compute in H.
  discriminate H.
Qed.

(* The alias block error.h:51-63 is the second hazard: ER_x expands to E_x, so
 * the two spellings in a caller's source are the same number.  Only the extended
 * block adds figures of its own (ER_NOSPC), and ER_TIMEOUT is a synonym of
 * E_TMOUT rather than a code in its own right. *)
Inductive receipt_name :=
  | RN_E_OK | RN_ER_OK | RN_E_PAR | RN_ER_PAR | RN_E_ID | RN_ER_ID
  | RN_ER_NOSPC | RN_E_TMOUT | RN_ER_TIMEOUT.

Definition name_code (n : receipt_name) : Z :=
  match n with
  | RN_E_OK | RN_ER_OK => Z0
  | RN_E_PAR | RN_ER_PAR => Z.opp 33
  | RN_E_ID | RN_ER_ID => Z.opp 35
  | RN_ER_NOSPC => Z.opp 11
  | RN_E_TMOUT | RN_ER_TIMEOUT => Z.opp 69
  end.

Lemma the_alias_block_is_two_spellings_of_one_figure :
    name_code RN_ER_OK = name_code RN_E_OK
    /\ name_code RN_ER_PAR = name_code RN_E_PAR
    /\ name_code RN_ER_ID = name_code RN_E_ID.
Proof. repeat split; reflexivity. Qed.

Lemma er_timeout_is_a_second_name_for_the_timeout :
    name_code RN_ER_TIMEOUT = name_code RN_E_TMOUT.
Proof. reflexivity. Qed.

Lemma the_alias_block_is_not_injective :
    name_code RN_E_PAR = name_code RN_ER_PAR /\ RN_E_PAR <> RN_ER_PAR.
Proof. split; [ reflexivity | discriminate ]. Qed.

Lemma every_shipped_spelling_names_a_modelled_receipt : forall n,
    exists b, name_code n = ber_code b.
Proof.
  destruct n; cbn [name_code];
    [ exists BE_OK | exists BE_OK | exists BE_PAR | exists BE_PAR
    | exists BE_ID | exists BE_ID | exists BE_NOSPC | exists BE_TMOUT
    | exists BE_TMOUT ]; reflexivity.
Qed.

(* §7's first_bad is monomorphic in `er`, so the flat namespace needs its own
 * reader.  The shape is the left-to-right one the C has: the first failing test
 * names the receipt, and a list with no failure is a call that proceeds. *)
Fixpoint ber_first_bad (gs : list (bool * ber)) : option ber :=
  match gs with
  | nil => None
  | (p, e) :: rest => if p then ber_first_bad rest else Some e
  end.

Lemma ber_first_bad_nil : ber_first_bad (@nil (bool * ber)) = @None ber.
Proof. reflexivity. Qed.

Lemma ber_first_bad_pass : forall e gs, ber_first_bad ((true, e) :: gs) = ber_first_bad gs.
Proof. intros e gs. reflexivity. Qed.

Lemma ber_first_bad_fail : forall e gs, ber_first_bad ((false, e) :: gs) = Some e.
Proof. intros e gs. reflexivity. Qed.

(* Reading a whole cascade at once, which is what lets the theorems below name
 * the receipt without case-splitting on four separate hypotheses. *)
Lemma four_guard_classification : forall p1 p2 p3 p4 : bool,
    ber_first_bad ((p1, BE_PAR) :: (p2, BE_ID) :: (p3, BE_PAR) :: (p4, BE_NOSPC) :: nil)
    = match p1, p2, p3, p4 with
      | true, true, true, true => None
      | true, true, true, false => Some BE_NOSPC
      | true, true, false, _ => Some BE_PAR
      | true, false, _, _ => Some BE_ID
      | false, _, _, _ => Some BE_PAR
      end.
Proof. destruct p1, p2, p3, p4; reflexivity. Qed.

Lemma two_guard_classification : forall p1 p2 : bool,
    ber_first_bad ((p1, BE_PAR) :: (p2, BE_ID) :: nil)
    = match p1, p2 with
      | true, true => None
      | true, false => Some BE_ID
      | false, _ => Some BE_PAR
      end.
Proof. destruct p1, p2; reflexivity. Qed.

(* ── 15.4.2 The send cascade: four tests, then one store ────────── *)

(* :47 is "pid < 0 || pid >= MAX_IPC_PIDS".  W is int32_t (types.h:21), so the
 * lower half of that test is reachable: a pid of -1 is a real caller error, and
 * ER_ID is what it gets. *)
Definition ipc_pid_ok (pid : Z) : bool :=
  andb (Z.ltb (Z.opp 1) pid) (Z.ltb pid (Z.of_nat msg_domains)).

Lemma a_negative_pid_fails_the_test : forall pid,
    Z.ltb pid Z0 = true -> ipc_pid_ok pid = false.
Proof. intros pid H. unfold ipc_pid_ok. lia. Qed.

Lemma a_pid_inside_the_domain_array_passes : forall pid,
    Z.ltb (Z.opp 1) pid = true -> Z.ltb pid (Z.of_nat msg_domains) = true ->
    ipc_pid_ok pid = true.
Proof. intros pid H1 H2. unfold ipc_pid_ok. lia. Qed.

Lemma the_endpoints_of_the_pid_test :
    ipc_pid_ok Z0 = true /\ ipc_pid_ok (Z.of_nat msg_domains) = false
    /\ ipc_pid_ok (Z.opp 1) = false.
Proof.
  split; [ | split ].
  - vm_compute; reflexivity.
  - vm_compute; reflexivity.
  - vm_compute; reflexivity.
Qed.

(* :46 asks whether the caller's buffer exists; :48 whether the type it names is
 * sendable.  With no buffer there is nothing to read a type from, so the third
 * test simply is not reached -- which is why None may carry it as passed while
 * the first test still refuses the call. *)
Definition msg_ptr_ok (m : option bmsg) : bool :=
  match m with Some _ => true | None => false end.

Definition type_ok_of (m : option bmsg) : bool :=
  match m with
  | Some b => type_in_range (bm_type b)
  | None => true
  end.

Lemma ptr_ok_of_a_buffer : forall b, msg_ptr_ok (Some b) = true.
Proof. intros b. reflexivity. Qed.

Lemma type_ok_of_a_buffer : forall b, type_ok_of (Some b) = type_in_range (bm_type b).
Proof. intros b. reflexivity. Qed.

Lemma zero_is_not_a_sendable_type : type_in_range 0 = false.
Proof. unfold type_in_range, msg_type_min, msg_type_max. cbn. reflexivity. Qed.

(* :55 "mb->count >= MAX_QUEUED_MSGS" -- the fourth test, and the only one that
 * reads state rather than arguments. *)
Definition has_room (r : msg_ring) : bool := Nat.ltb (mr_count r) msg_ring_cap.

Definition snd_guards (pid : Z) (m : option bmsg) (r : msg_ring) : list (bool * ber) :=
  (msg_ptr_ok m, BE_PAR)
  :: (ipc_pid_ok pid, BE_ID)
  :: (type_ok_of m, BE_PAR)
  :: (has_room r, BE_NOSPC)
  :: nil.

(* ipc_msg.c:45-68: refuse, or store at the cursor and come back E_OK.  Nothing
 * in the cascade writes -- the first write is :60, after all four tests -- so a
 * refusal leaves the mailbox exactly as the caller found it. *)
Definition snd_msg_service (pid : Z) (m : option bmsg) (r : msg_ring) : ber * msg_ring :=
  match ber_first_bad (snd_guards pid m r) with
  | Some e => (e, r)
  | None => (BE_OK, snd_store (match m with Some b => b | None => dead_cell end) r)
  end.

(* 1. A null buffer is refused before the pid, the type or the room are read. *)
Lemma a_null_buffer_is_E_PAR_and_stores_nothing : forall pid r,
    snd_msg_service pid None r = (BE_PAR, r).
Proof.
  intros pid r. unfold snd_msg_service, snd_guards.
  rewrite four_guard_classification. cbn [msg_ptr_ok]. reflexivity.
Qed.

(* 2. A bad pid gets the second test's own code, ER_ID, not the parameter error
 * its neighbours use. *)
Lemma a_bad_pid_is_ER_ID : forall pid b r,
    ipc_pid_ok pid = false -> snd_msg_service pid (Some b) r = (BE_ID, r).
Proof.
  intros pid b r I. unfold snd_msg_service, snd_guards.
  rewrite four_guard_classification.
  cbn [msg_ptr_ok type_ok_of]. rewrite I. reflexivity.
Qed.

(* 3. E_PAR comes from two different tests (:46 and :48), so the receipt alone
 * never says which one refused. *)
Example the_parameter_error_does_not_name_its_test :
    snd_msg_service 0 None (mk_mring nil O O O) = (BE_PAR, mk_mring nil O O O)
    /\ snd_msg_service 0 (Some (mk_bmsg 0 O O)) (mk_mring nil O O O)
       = (BE_PAR, mk_mring nil O O O).
Proof. split; vm_compute; reflexivity. Qed.

(* 4. Precedence: an illegal type on a full ring is E_PAR, and the caller never
 * learns that there was no room either. *)
Lemma the_parameter_test_outranks_the_room_test : forall pid b r,
    ipc_pid_ok pid = true -> type_in_range (bm_type b) = false -> has_room r = false ->
    snd_msg_service pid (Some b) r = (BE_PAR, r).
Proof.
  intros pid b r I T R. unfold snd_msg_service, snd_guards.
  rewrite four_guard_classification.
  cbn [msg_ptr_ok type_ok_of]. rewrite I, T. reflexivity.
Qed.

Lemma a_full_ring_refuses_another_message : forall pid b r,
    ipc_pid_ok pid = true -> type_in_range (bm_type b) = true -> has_room r = false ->
    snd_msg_service pid (Some b) r = (BE_NOSPC, r).
Proof.
  intros pid b r I T R. unfold snd_msg_service, snd_guards.
  rewrite four_guard_classification.
  cbn [msg_ptr_ok type_ok_of]. rewrite I, T, R. reflexivity.
Qed.

(* 5. Everything passing is the only route to the store, and it pays E_OK. *)
Lemma an_accepted_send_is_the_only_way_the_ring_moves : forall pid b r,
    ipc_pid_ok pid = true -> type_in_range (bm_type b) = true -> has_room r = true ->
    snd_msg_service pid (Some b) r = (BE_OK, snd_store b r).
Proof.
  intros pid b r I T R. unfold snd_msg_service, snd_guards.
  rewrite four_guard_classification.
  cbn [msg_ptr_ok type_ok_of]. rewrite I, T, R. reflexivity.
Qed.

Lemma a_refused_send_leaves_the_mailbox_untouched : forall pid m e r,
    ber_first_bad (snd_guards pid m r) = Some e ->
    snd_msg_service pid m r = (e, r).
Proof.
  intros pid m e r H. unfold snd_msg_service. rewrite H. reflexivity.
Qed.

(* ER_NOSPC is the only refusal this service reads out of state, and §15.2 shows
 * a successful send keeps count <= 64, so the 64 cells of MAX_QUEUED_MSGS are
 * the whole residency bound the API offers. *)
Lemma ER_NOSPC_is_the_only_state_dependent_refusal : forall pid m r,
    ber_first_bad (snd_guards pid m r) = Some BE_NOSPC -> has_room r = false.
Proof.
  intros pid m r H. unfold snd_guards in H.
  rewrite four_guard_classification in H.
  destruct (msg_ptr_ok m), (ipc_pid_ok pid), (type_ok_of m), (has_room r);
    cbn in H; try discriminate H; reflexivity.
Qed.

(* ── 15.4.3 The receive cascade, and chk_msg as its special case ─── *)

(* :71-72 is the whole guard block of rcv_msg: two tests, and nothing else.  The
 * scan of :93-111 is not a guard at all -- it cannot refuse the call, only fail
 * to find a cell -- and §15.3.2 is what proves that. *)
Definition rcv_guards (pid : Z) (m : option bmsg) : list (bool * ber) :=
  (msg_ptr_ok m, BE_PAR) :: (ipc_pid_ok pid, BE_ID) :: nil.

Lemma the_receive_guard_cascade_is_two_long : forall pid m,
    map snd (rcv_guards pid m) = BE_PAR :: BE_ID :: nil.
Proof. intros pid m. reflexivity. Qed.

Lemma rcv_guards_pass : forall pid m,
    msg_ptr_ok m = true -> ipc_pid_ok pid = true ->
    ber_first_bad (rcv_guards pid m) = None.
Proof.
  intros pid m P I. unfold rcv_guards. rewrite two_guard_classification.
  rewrite P, I. reflexivity.
Qed.

Lemma a_rcv_guard_refusal_is_one_of_the_two_codes : forall pid m e,
    ber_first_bad (rcv_guards pid m) = Some e -> e = BE_PAR \/ e = BE_ID.
Proof.
  intros pid m e H. unfold rcv_guards in H.
  rewrite two_guard_classification in H.
  destruct (msg_ptr_ok m), (ipc_pid_ok pid); cbn in H.
  - discriminate H.
  - injection H. intros X. subst e. right. reflexivity.
  - injection H. intros X. subst e. left. reflexivity.
  - injection H. intros X. subst e. left. reflexivity.
Qed.

(* None stands for "this call has not returned yet": a blocking miss parks in
 * :120-128 and hands back no figure at all. *)
Definition rcv_phase (t : tmo) (mask : nat) (r : msg_ring) : option ber :=
  match rcv_choice t mask r with
  | Some _ => Some BE_OK
  | None => if tmo_blocks t then None else Some BE_TMOUT
  end.

Definition rcv_msg_service (pid : Z) (m : option bmsg) (t : tmo)
  (mask : nat) (r : msg_ring) : option ber :=
  match ber_first_bad (rcv_guards pid m) with
  | Some e => Some e
  | None => rcv_phase t mask r
  end.

Lemma rcv_phase_hit : forall t mask r i,
    rcv_match mask r = Some i -> rcv_phase t mask r = Some BE_OK.
Proof.
  intros t mask r i H. unfold rcv_phase, rcv_choice. rewrite H. reflexivity.
Qed.

Lemma rcv_phase_poll_miss : forall mask r,
    rcv_match mask r = None -> rcv_phase TMO_POLL mask r = Some BE_TMOUT.
Proof.
  intros mask r H. unfold rcv_phase, rcv_choice. rewrite H. reflexivity.
Qed.

Lemma rcv_phase_blocking_miss : forall t mask r,
    tmo_blocks t = true -> rcv_match mask r = None -> rcv_phase t mask r = None.
Proof.
  intros t mask r HT HM. unfold rcv_phase, rcv_choice. rewrite HM.
  destruct t; cbn [tmo_blocks] in HT; try discriminate HT; reflexivity.
Qed.

Lemma a_poll_never_parks : forall mask r,
    rcv_phase TMO_POLL mask r = Some BE_OK
    \/ rcv_phase TMO_POLL mask r = Some BE_TMOUT.
Proof.
  intros mask r. unfold rcv_phase, rcv_choice.
  destruct (rcv_match mask r) as [i|]; [ left | right ]; reflexivity.
Qed.

Lemma a_null_receive_buffer_is_E_PAR : forall pid t mask r,
    rcv_msg_service pid None t mask r = Some BE_PAR.
Proof.
  intros pid t mask r. unfold rcv_msg_service, rcv_guards.
  rewrite two_guard_classification. cbn [msg_ptr_ok]. reflexivity.
Qed.

Lemma a_bad_receive_pid_is_ER_ID : forall pid m t mask r,
    ipc_pid_ok pid = false -> rcv_msg_service pid (Some m) t mask r = Some BE_ID.
Proof.
  intros pid m t mask r I. unfold rcv_msg_service, rcv_guards.
  rewrite two_guard_classification. cbn [msg_ptr_ok]. rewrite I. reflexivity.
Qed.

(* No timeout figure can be the reason a caller is refused: once the two tests
 * pass, the outcomes are success, time-out, or no return at all.  The T-Kernel
 * family refuses tmo < -1 from check.h:185; ipc_msg.c has no such guard, which is
 * why §3's legal range needs no counterpart here. *)
Lemma the_timeout_figure_is_never_guarded : forall pid m t mask r,
    msg_ptr_ok m = true -> ipc_pid_ok pid = true ->
    match rcv_msg_service pid m t mask r with
    | Some BE_PAR | Some BE_ID | Some BE_NOSPC => false
    | _ => true
    end = true.
Proof.
  intros pid m t mask r P I. unfold rcv_msg_service.
  rewrite rcv_guards_pass; [ | exact P | exact I ].
  unfold rcv_phase, rcv_choice.
  destruct (rcv_match mask r) as [i|]; destruct (tmo_blocks t); reflexivity.
Qed.

Lemma a_blocked_waiter_hands_back_nothing_yet : forall pid m t mask r,
    msg_ptr_ok (Some m) = true -> ipc_pid_ok pid = true -> tmo_blocks t = true ->
    rcv_match mask r = None -> rcv_msg_service pid (Some m) t mask r = None.
Proof.
  intros pid m t mask r P I HT HM. unfold rcv_msg_service.
  rewrite rcv_guards_pass; [ | exact P | exact I ].
  apply (rcv_phase_blocking_miss t mask r HT HM).
Qed.

Lemma a_poll_on_a_miss_is_E_TMOUT : forall pid m mask r,
    msg_ptr_ok (Some m) = true -> ipc_pid_ok pid = true -> rcv_match mask r = None ->
    rcv_msg_service pid (Some m) TMO_POLL mask r = Some BE_TMOUT.
Proof.
  intros pid m mask r P I HM. unfold rcv_msg_service.
  rewrite rcv_guards_pass; [ | exact P | exact I ].
  apply (rcv_phase_poll_miss mask r HM).
Qed.

Lemma a_hit_is_E_OK_whatever_the_timeout : forall pid m i t mask r,
    ipc_pid_ok pid = true -> rcv_match mask r = Some i ->
    rcv_msg_service pid (Some m) t mask r = Some BE_OK.
Proof.
  intros pid m i t mask r I HM. unfold rcv_msg_service.
  rewrite rcv_guards_pass; [ | apply ptr_ok_of_a_buffer | exact I ].
  apply (rcv_phase_hit t mask r i HM).
Qed.

Lemma ER_NOSPC_never_refuses_a_receive : forall pid m t mask r,
    rcv_msg_service pid m t mask r <> Some BE_NOSPC.
Proof.
  intros pid m t mask r H. unfold rcv_msg_service in H.
  destruct (ber_first_bad (rcv_guards pid m)) as [e|] eqn:G.
  - apply a_rcv_guard_refusal_is_one_of_the_two_codes in G.
    destruct G as [X|X]; subst e; discriminate H.
  - unfold rcv_phase, rcv_choice in H.
    destruct (rcv_match mask r) as [i|].
    + discriminate H.
    + destruct (tmo_blocks t); discriminate H.
Qed.

(* chk_msg (:132-134) is literally rcv_msg with the figure 0, so it can never
 * park: its receipt set is the four flat codes minus ER_NOSPC, which only the
 * send path reaches. *)
Definition chk_msg_service (pid : Z) (m : option bmsg) (mask : nat) (r : msg_ring)
  : option ber := rcv_msg_service pid m TMO_POLL mask r.

Lemma a_check_always_hands_back_a_figure : forall pid m mask r,
    rcv_msg_service pid m TMO_POLL mask r <> None.
Proof.
  intros pid m mask r H. unfold rcv_msg_service in H.
  destruct (ber_first_bad (rcv_guards pid m)) eqn:G.
  - discriminate H.
  - destruct (a_poll_never_parks mask r) as [A|A]; rewrite A in H; discriminate H.
Qed.

Lemma chk_receipts_are_the_four_enumerated : forall pid m mask r b,
    chk_msg_service pid m mask r = Some b ->
    b = BE_PAR \/ b = BE_ID \/ b = BE_TMOUT \/ b = BE_OK.
Proof.
  intros pid m mask r b H. unfold chk_msg_service, rcv_msg_service in H.
  destruct (ber_first_bad (rcv_guards pid m)) as [e|] eqn:G.
  - apply a_rcv_guard_refusal_is_one_of_the_two_codes in G.
    injection H. intros X. subst b.
    destruct G as [Y|Y]; subst e; [ left | right; left ]; reflexivity.
  - destruct (a_poll_never_parks mask r) as [A|A].
    + rewrite A in H. injection H. intros X. subst b.
      right. right. right. reflexivity.
    + rewrite A in H. injection H. intros X. subst b.
      right. right. left. reflexivity.
Qed.

Lemma chk_never_produces_the_room_refusal : forall pid m mask r,
    chk_msg_service pid m mask r <> Some BE_NOSPC.
Proof.
  intros pid m mask r H.
  exact (ER_NOSPC_never_refuses_a_receive pid m TMO_POLL mask r H).
Qed.

(* The window is never read by the guard block, so a receive with an illegal pid
 * is refused even though its ring is perfectly reachable -- the mirror image of
 * the send, where the ring is read only after the arguments are. *)
Lemma an_illegal_pid_is_refused_before_the_window_is_read : forall pid m mask r,
    ipc_pid_ok pid = false ->
    rcv_msg_service pid (Some m) TMO_REL mask r = Some BE_ID.
Proof.
  intros pid m mask r I. apply (a_bad_receive_pid_is_ER_ID pid m TMO_REL mask r I).
Qed.

(* ── 15.5 The byte ring: what a message costs and what it may spend ───── *)

(* T-Kernel's message buffer (src/kernel/messagebuf.c) is the construction of
 * 15.1-15.4 read at byte granularity: a fixed buffer, a pair of cursors, and a
 * counter.  Three things the message queue never had to deal with:
 *
 *   - every charge is rounded up to a 4-byte boundary (:105),
 *   - the free-space test that authorises the charge does not round (:113),
 *   - the emptiness test reads the counter, not the cursors (:121).
 *
 * 15.5.1 closes the gap between the first two: an unaligned free-space count
 * does admit a message it cannot pay for, and a created buffer's count is never
 * unaligned.  15.5.2 shows the third is not a stylistic preference -- the cursor
 * pair takes the same value in an empty buffer and in a full one, so it cannot
 * say whether there is anything to read.
 *
 * Scope, stated once so nothing below is read as more than it is: this models
 * the bookkeeping -- cursor pair, byte counter, guard cascades, receipts, and the
 * two wait queues.  It does not model the bytes of the buffer.  Nothing here says
 * the split copy at :141-147 cannot land on a byte an unread message still owns;
 * that is a memory-layout question, and this file neither answers it nor claims
 * to. *)

(* ── 15.5.1 The round, the admission, and the charge ─────────────────── *)

(* messagebuf.c:101-105: HEADER is an INT, so HEADERSZ = ROUNDSIZE = sizeof(INT)
 * = 4, and ROUNDSZ(sz) = (sz + 3) & ~3, i.e. the low two bits of sz+3 cleared.
 * The model writes that as "4 times the number of blocks sz needs";
 * roundsz_is_the_least_aligned_upper_bound is the theorem that makes the two the
 * same function, since a least such figure is unique. *)
Definition headersz : nat := 4.                          (* :102 *)
Definition roundsz (sz : nat) : nat := 4 * ((sz + 3) / 4).   (* :105 *)

Lemma roundsz_covers_the_size : forall sz, sz <= roundsz sz.
Proof.
  intros sz. unfold roundsz.
  assert (E : sz + 3 = 4 * ((sz + 3) / 4) + (sz + 3) mod 4) by apply Nat.div_mod_eq.
  assert (M : (sz + 3) mod 4 < 4) by (apply Nat.mod_upper_bound; lia).
  lia.
Qed.

Lemma roundsz_needs_at_most_three_more_bytes : forall sz, roundsz sz <= sz + 3.
Proof. intros sz. unfold roundsz. apply Nat.Div0.mul_div_le. Qed.

Lemma roundsz_is_aligned : forall sz, roundsz sz mod 4 = 0.
Proof. intros sz. unfold roundsz. rewrite Nat.mul_comm. apply Nat.Div0.mod_mul. Qed.

(* :105 read as a specification: no aligned figure rounds up past it. *)
Lemma roundsz_is_the_least_aligned_upper_bound : forall sz q,
    sz <= 4 * q -> roundsz sz <= 4 * q.
Proof.
  intros sz q H. unfold roundsz. apply Nat.mul_le_mono_l.
  assert (D : (sz + 3) / 4 < q + 1) by (apply Nat.Div0.div_lt_upper_bound; lia).
  lia.
Qed.

Lemma roundsz_of_an_aligned_size_is_itself : forall sz,
    sz mod 4 = 0 -> roundsz sz = sz.
Proof.
  intros sz A. apply Nat.le_antisymm.
  - assert (H4 : sz = 4 * (sz / 4)).
    { assert (E : sz = 4 * (sz / 4) + sz mod 4) by apply Nat.div_mod_eq.
      rewrite A in E. lia. }
    rewrite H4 at 2.
    apply (roundsz_is_the_least_aligned_upper_bound sz (sz / 4)). lia.
  - apply roundsz_covers_the_size.
Qed.

Lemma roundsz_is_idempotent : forall sz, roundsz (roundsz sz) = roundsz sz.
Proof.
  intros sz. apply (roundsz_of_an_aligned_size_is_itself (roundsz sz)).
  apply roundsz_is_aligned.
Qed.

(* The header is itself a whole aligned block, so rounding a message together
 * with its header rounds the message alone: :133's HEADERSZ + ROUNDSZ(msgsz) is
 * also ROUNDSZ(HEADERSZ + msgsz).  This is why a run of stores never pushes the
 * counter off the boundary. *)
Lemma the_header_needs_no_rounding_of_its_own : forall sz,
    roundsz (headersz + sz) = headersz + roundsz sz.
Proof.
  intros sz. unfold headersz, roundsz.
  replace (4 + sz + 3) with (1 * 4 + (sz + 3)) by lia.
  rewrite Nat.div_add_l; [ | lia ].
  lia.
Qed.

(* :113, the admission: HEADERSZ + msgsz bytes free, with no rounding.  The C
 * casts both sides to UINT; msgsz is positive by :370, so the signed figure and
 * the model's nat read the same way. *)
Definition mbf_admits (free sz : nat) : bool := Nat.leb (headersz + sz) free.

(* :133, the charge: HEADERSZ + ROUNDSZ(msgsz).  Strictly the larger figure. *)
Definition mbf_charge (sz : nat) : nat := headersz + roundsz sz.

Lemma mbf_admits_iff : forall free sz,
    mbf_admits free sz = true <-> headersz + sz <= free.
Proof. intros free sz. unfold mbf_admits, headersz. apply Nat.leb_le. Qed.

Lemma every_charge_is_aligned : forall sz, mbf_charge sz mod 4 = 0.
Proof.
  intros sz. unfold mbf_charge, headersz, roundsz.
  replace (4 + 4 * ((sz + 3) / 4)) with ((1 + (sz + 3) / 4) * 4) by lia.
  apply Nat.Div0.mod_mul.
Qed.

Lemma headersz_is_aligned : headersz mod 4 = 0.
Proof. unfold headersz. apply Nat.Div0.mod_same. Qed.
(* Two shapes lia can see but replace cannot match: a block taken away, and a
 * whole debit taken away. *)
Lemma four_times_minus_a_block : forall x, 4 * x - 4 = 4 * (x - 1).
Proof. intros x. lia. Qed.


(* The gap the two definitions open, at its narrowest: 5 bytes admitted on a
 * count of 9, which the rounding turns into a charge of 12.  An unaligned count
 * therefore makes the pair unsound, so the hypothesis of the next theorem is
 * doing work rather than tidying up. *)
Example an_unaligned_count_admits_more_than_it_can_pay :
    mbf_admits 9 5 = true /\ headersz + 5 = 9 /\ mbf_charge 5 = 12.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* The rounding never passes an aligned bound that the size itself respects --
 * the minimality fact above, stated so a proof can use a subtraction as the
 * bound instead of having to name a block count. *)
Lemma roundsz_never_passes_an_aligned_bound : forall sz bound,
    sz <= bound -> bound mod 4 = 0 -> roundsz sz <= bound.
Proof.
  intros sz bound H A.
  assert (GE : bound = 4 * (bound / 4)).
  { assert (D : bound = 4 * (bound / 4) + bound mod 4) by apply Nat.div_mod_eq.
    rewrite A in D. lia. }
  assert (U : sz <= 4 * (bound / 4)) by lia.
  assert (R : roundsz sz <= 4 * (bound / 4))
    by (apply (roundsz_is_the_least_aligned_upper_bound sz (bound / 4)); exact U).
  lia.
Qed.

(* :113 against :133, closed: with the count on a boundary, everything the
 * unrounded test admits the rounded charge can pay for.  The bound is free minus
 * the header, which is aligned exactly when free is. *)
Lemma an_aligned_count_never_underpays : forall free sz,
    free mod 4 = 0 -> mbf_admits free sz = true -> mbf_charge sz <= free.
Proof.
  intros free sz A P. unfold mbf_admits, headersz in P. apply Nat.leb_le in P.
  unfold mbf_charge, headersz.
  assert (E : free = 4 * (free / 4)).
  { assert (D : free = 4 * (free / 4) + free mod 4) by apply Nat.div_mod_eq.
    rewrite A in D. lia. }
  assert (A4 : Nat.sub free 4 mod 4 = 0).
  { rewrite E, four_times_minus_a_block, Nat.mul_comm. apply Nat.Div0.mod_mul. }
  assert (R : roundsz sz <= Nat.sub free 4)
    by (apply (roundsz_never_passes_an_aligned_bound sz (free - 4)); lia).
  lia.
Qed.

Lemma four_times_minus_four_times : forall x y, 4 * x - 4 * y = 4 * (x - y).
Proof. intros x y. lia. Qed.

Lemma aligned_plus_aligned_is_aligned : forall a b,
    a mod 4 = 0 -> b mod 4 = 0 -> (a + b) mod 4 = 0.
Proof.
  intros a b A B.
  assert (EA : a = 4 * (a / 4)).
  { assert (D : a = 4 * (a / 4) + a mod 4) by apply Nat.div_mod_eq.
    rewrite A in D. lia. }
  assert (EB : b = 4 * (b / 4)).
  { assert (D : b = 4 * (b / 4) + b mod 4) by apply Nat.div_mod_eq.
    rewrite B in D. lia. }
  rewrite EA, EB.
  replace (4 * (a / 4) + 4 * (b / 4)) with ((a / 4 + b / 4) * 4) by lia.
  apply Nat.Div0.mod_mul.
Qed.

Lemma a_block_is_aligned : forall k, 4 * k mod 4 = 0.
Proof. intros k. rewrite Nat.mul_comm. apply Nat.Div0.mod_mul. Qed.

Lemma a_difference_of_blocks_is_blocks : forall x y,
    Nat.sub (4 * x) (4 * y) mod 4 = 0.
Proof.
  intros x y. rewrite four_times_minus_four_times. apply a_block_is_aligned.
Qed.

Lemma the_difference_of_two_aligned_counts_is_aligned : forall a b,
    a mod 4 = 0 -> b mod 4 = 0 -> Nat.sub a b mod 4 = 0.
Proof.
  intros a b A B.
  assert (EA : a = 4 * (a / 4)).
  { assert (D : a = 4 * (a / 4) + a mod 4) by apply Nat.div_mod_eq.
    rewrite A in D. lia. }
  assert (EB : b = 4 * (b / 4)).
  { assert (D : b = 4 * (b / 4) + b mod 4) by apply Nat.div_mod_eq.
    rewrite B in D. lia. }
  rewrite EA, EB. apply a_difference_of_blocks_is_blocks.
Qed.

(* A history of stores debits nothing but whole aligned blocks (:133 through the
 * header lemma above), so a counter that starts on the boundary stays there
 * however the messages are sized. *)
Fixpoint mbf_debits (l : list nat) : nat :=
  match l with
  | nil => 0
  | sz :: rest => mbf_charge sz + mbf_debits rest
  end.

Lemma debits_are_a_multiple_of_the_round : forall l, exists k, mbf_debits l = 4 * k.
Proof.
  induction l as [|sz rest IH].
  - exists 0. reflexivity.
  - destruct IH as [k HK]. cbn [mbf_debits]. rewrite HK.
    unfold mbf_charge, headersz, roundsz. exists (1 + (sz + 3) / 4 + k). lia.
Qed.

(* The shipped reason the alignment hypothesis is not luck: _tk_cre_mbf rounds the
 * buffer itself, `bufsz = (INT)ROUNDSZ(pk_cmbf->bufsz)` at :273, and :299 then
 * sets the counter to that same rounded figure. *)
Definition created_count (requested : nat) : nat := roundsz requested.   (* :273 *)

Lemma a_created_buffer_starts_aligned : forall requested,
    created_count requested mod 4 = 0.
Proof. intros requested. unfold created_count. apply roundsz_is_aligned. Qed.

(* The sentence the 15 preamble promises: an under-charge that the create-time
 * alignment rescues, for any history of stores. *)
Theorem what_a_live_buffer_admits_it_can_pay : forall bufsz l sz,
    bufsz mod 4 = 0 ->
    mbf_admits (Nat.sub bufsz (mbf_debits l)) sz = true ->
    mbf_charge sz <= Nat.sub bufsz (mbf_debits l).
Proof.
  intros bufsz l sz A P. apply (an_aligned_count_never_underpays _ sz).
  - apply (the_difference_of_two_aligned_counts_is_aligned _ (mbf_debits l) A).
    destruct (debits_are_a_multiple_of_the_round l) as [k HK].
    rewrite HK. apply a_block_is_aligned.
  - exact P.
Qed.

(* ── 15.5.2 Cursors, counter, and what an empty buffer is ────────────── *)

(* MBFCB's bookkeeping fields: :299 for the size and the counter, :301 for the
 * cursors.  The bytes themselves (buffer, :298) are outside the model, per the
 * scope note; maxmsz and the two wait queues enter in 15.5.4. *)
Record mbf : Set := mk_mbf {
    mb_bufsz : nat;     (* INT bufsz -- aligned at create (:273,:299) *)
    mb_free  : nat;     (* INT frbufsz (:113,:121,:133,:168) *)
    mb_head  : nat;     (* INT head (:301,:162,:187) *)
    mb_tail  : nat      (* INT tail (:301,:129,:153) *)
  }.

(* :299 and :301 together. *)
Definition mbf_fresh (bufsz : nat) : mbf := mk_mbf bufsz bufsz 0 0.

(* :121: the emptiness test is on the counter. *)
Definition mbf_is_empty (m : mbf) : bool := Nat.eqb (mb_free m) (mb_bufsz m).

Lemma mbf_is_empty_iff : forall m,
    mbf_is_empty m = true <-> mb_free m = mb_bufsz m.
Proof. intros m. unfold mbf_is_empty. apply Nat.eqb_eq. Qed.

Lemma a_fresh_buffer_is_empty : forall bufsz, mbf_is_empty (mbf_fresh bufsz) = true.
Proof. intros bufsz. unfold mbf_is_empty, mbf_fresh. apply Nat.eqb_refl. Qed.

(* :137-139 and :149-151, the cursor's one conditional wrap: past the end of the
 * buffer is back to zero.  Transcribed as a clamp because that is what the C
 * says -- `if (x >= bufsz) x = 0`, not `x %= bufsz`.  The two readings agree
 * while a step never overshoots the end, which is the hypothesis below; without
 * it they do not agree, and the example after says so plainly. *)
Definition mbf_advance (cur delta cap : nat) : nat :=
  if Nat.ltb (cur + delta) cap then cur + delta else 0.

Lemma advance_is_modulo_while_the_step_fits : forall cur delta cap,
    cur + delta <= cap -> mbf_advance cur delta cap = (cur + delta) mod cap.
Proof.
  intros cur delta cap F. unfold mbf_advance.
  destruct (Nat.ltb (cur + delta) cap) eqn:L.
  - symmetry. apply Nat.mod_small. apply Nat.ltb_lt in L. exact L.
  - apply Nat.ltb_nlt in L.
    assert (E : cur + delta = cap) by lia.
    rewrite E. symmetry. apply Nat.Div0.mod_same.
Qed.

Example a_clamp_is_not_a_modulo : mbf_advance 0 11 8 = 0 /\ 11 mod 8 = 3.
Proof. repeat split; vm_compute; reflexivity. Qed.

Lemma advance_keeps_the_boundary : forall cur delta cap,
    cur mod 4 = 0 -> delta mod 4 = 0 -> mbf_advance cur delta cap mod 4 = 0.
Proof.
  intros cur delta cap C D. unfold mbf_advance.
  destruct (Nat.ltb (cur + delta) cap) eqn:L.
  - apply aligned_plus_aligned_is_aligned; assumption.
  - reflexivity.
Qed.

(* :136-151, transcribed in the C's own two steps: past the header, then past the
 * payload, which is split over the end of the buffer when it has to be, with the
 * second rounding taken of the REDUCED size (:141-148).  The read path
 * :170-185 is this same walk started from the other cursor -- the two services
 * are one function used twice, and :144's reduced rounding is why the walk is
 * written from the post-header position rather than from the start. *)
Definition mbf_payload_walk (pos sz cap : nat) : nat :=
  if Nat.ltb (Nat.sub cap pos) sz
  then mbf_advance 0 (roundsz (Nat.sub sz (Nat.sub cap pos))) cap
  else mbf_advance pos (roundsz sz) cap.

Definition mbf_walk (start sz cap : nat) : nat :=
  mbf_payload_walk (mbf_advance start headersz cap) sz cap.

(* Both cursors stay on the boundary: the header step is a whole block and the
 * payload step rounds to one, so no walk leaves the grid :273 put the buffer on.
 * That is the other half of why :113's unrounded test survives -- the counter and
 * the cursors are both reading the same 4-byte grid -- and note the buffer's own
 * alignment is not needed here, only the start cursor's. *)
Lemma payload_walk_keeps_the_boundary : forall pos sz cap,
    pos mod 4 = 0 -> mbf_payload_walk pos sz cap mod 4 = 0.
Proof.
  intros pos sz cap P. unfold mbf_payload_walk.
  destruct (Nat.ltb (Nat.sub cap pos) sz) eqn:L.
  - apply advance_keeps_the_boundary.
    + reflexivity.
    + unfold roundsz. rewrite Nat.mul_comm. apply Nat.Div0.mod_mul.
  - apply advance_keeps_the_boundary.
    + exact P.
    + unfold roundsz. rewrite Nat.mul_comm. apply Nat.Div0.mod_mul.
Qed.

Lemma a_walk_lands_on_the_boundary : forall start sz cap,
    start mod 4 = 0 -> mbf_walk start sz cap mod 4 = 0.
Proof.
  intros start sz cap S. unfold mbf_walk. apply payload_walk_keeps_the_boundary.
  apply (advance_keeps_the_boundary start headersz cap S headersz_is_aligned).
Qed.

(* :133 with :153: the counter pays and the tail moves; nothing else changes. *)
Definition mbf_store (m : mbf) (sz : nat) : mbf :=
  mk_mbf (mb_bufsz m) (Nat.sub (mb_free m) (mbf_charge sz)) (mb_head m)
         (mbf_walk (mb_tail m) sz (mb_bufsz m)).

(* :168 with :187: the counter is repaid and the head moves.  The C takes the
 * size back off the header it reads at :167; the model is handed that size, since
 * it keeps no bytes. *)
Definition mbf_read (m : mbf) (sz : nat) : mbf :=
  mk_mbf (mb_bufsz m) (Nat.add (mb_free m) (mbf_charge sz))
         (mbf_walk (mb_head m) sz (mb_bufsz m)) (mb_tail m).

(* One service, one cursor. *)
Lemma a_store_moves_only_the_tail : forall m sz,
    mb_head (mbf_store m sz) = mb_head m
    /\ mb_bufsz (mbf_store m sz) = mb_bufsz m.
Proof. intros m sz. unfold mbf_store. split; reflexivity. Qed.

Lemma a_read_moves_only_the_head : forall m sz,
    mb_tail (mbf_read m sz) = mb_tail m
    /\ mb_bufsz (mbf_read m sz) = mb_bufsz m.
Proof. intros m sz. unfold mbf_read. split; reflexivity. Qed.

(* The counter's debt is exactly what the next read repays (:133 against :168):
 * the counter moves by the charge, never by the message. *)
Lemma a_read_repays_exactly_the_stored_charge : forall m sz,
    mbf_charge sz <= mb_free m ->
    mb_free (mbf_read (mbf_store m sz) sz) = mb_free m.
Proof.
  intros m sz F. unfold mbf_store, mbf_read.
  cbn [mb_bufsz mb_free mb_head mb_tail]. lia.
Qed.

Lemma a_store_never_credits_the_counter : forall m sz,
    mb_free (mbf_store m sz) <= mb_free m
    /\ headersz <= mbf_charge sz.
Proof.
  intros m sz. split.
  - unfold mbf_store. cbn [mb_free]. lia.
  - unfold mbf_charge, headersz, roundsz. lia.
Qed.

(* The flagship of 15.5, and the reason :121 reads the counter.  bufsz=8 with one
 * 4-byte message: the header takes bytes 0-3 and the rounded payload takes 4-7,
 * so the cursor comes back to exactly where it started.  Head and tail are 0, as
 * in the never-touched buffer, and the buffer is entirely full.  A `head == tail`
 * emptiness test would report that full buffer empty and drop the message. *)
Definition mbf_empty8 : mbf := mbf_fresh 8.
Definition mbf_full8 : mbf := mbf_store mbf_empty8 4.

Example the_cursors_take_the_same_value_in_an_empty_and_a_full_buffer :
    mb_bufsz mbf_empty8 = mb_bufsz mbf_full8
    /\ mb_head mbf_empty8 = mb_tail mbf_empty8
    /\ mb_head mbf_full8 = mb_tail mbf_full8
    /\ mbf_is_empty mbf_empty8 = true
    /\ mbf_is_empty mbf_full8 = false.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* So the cursor pair is a position, not a usage: hold the position and the
 * counter still says something the position cannot.  The converse fails, and that
 * is fine -- the counter alone cannot say WHERE the next message goes, which is
 * why the state is the pair and each half of it is used for what it can say. *)
Lemma the_cursors_do_not_determine_the_counter :
    mb_free mbf_empty8 <> mb_free mbf_full8.
Proof. unfold mbf_empty8, mbf_full8. vm_compute. discriminate. Qed.

(* A round trip on a bigger buffer, computed: the counter comes back to its
 * starting value and the two cursors coincide again -- but they have moved on, so
 * the state they arrive at is empty in the :121 sense at a position the fresh
 * buffer never had. *)
Example a_round_trip_refills_the_counter_and_moves_both_cursors :
    mbf_read (mbf_store (mbf_fresh 16) 4) 4 = mk_mbf 16 16 8 8.
Proof. reflexivity. Qed.

Lemma a_store_then_a_read_leave_the_cursors_coincident : forall m sz,
    mb_head m = mb_tail m ->
    mb_head (mbf_read (mbf_store m sz) sz)
    = mb_tail (mbf_read (mbf_store m sz) sz).
Proof.
  intros m sz C. unfold mbf_store, mbf_read. cbn [mb_head mb_tail]. rewrite C.
  reflexivity.
Qed.

(* ── 15.5.3 The two context tests, and the figure that feeds them ── *)

(* check.h:184-188 is CHECK_TMOUT: "if (!((tmout) >= TMO_FEVR)) return E_PAR".
 * The test is a lower bound, not a membership test on the two sentinels, so
 * every figure from -1 upward passes and every figure below -1 is refused. *)
Definition tmout_ok (t : Z) : bool := Z.leb (Z.opp 1) t.   (* :185 *)

(* Section 3's three cases are exactly the figures this guard admits, which is
 * why the file can speak of TMO_POLL/TMO_REL/TMO_FEVR without a fourth case
 * for "illegal timeout": there is no such case to model. *)
Lemma the_timeout_guard_admits_exactly_the_modelled_figures : forall t,
    tmout_ok (tmo_code t) = true.
Proof. intros t. unfold tmout_ok. apply tmo_legal_is_total. Qed.

Lemma a_figure_below_the_sentinel_is_E_PAR : forall t,
    Z.ltb t (Z.opp 1) = true -> tmout_ok t = false.
Proof. intros t H. unfold tmout_ok. lia. Qed.

(* check.h:254-258 (CHECK_DISPATCH_POL) and :249-253 (CHECK_DISPATCH) are the
 * two context tests of this pair of services, and they are not the same test:
 * the send refuses a WAIT while dispatch is disabled, the receive refuses
 * ANYTHING.  _tk_snd_mbf :372 uses the _POL form because it is legal to send
 * without blocking from a disabled context; _tk_rcv_mbf :442 cannot offer that
 * escape, because a non-blocking miss still has nowhere to go. *)
Definition snd_ctx_ok (ddsp : bool) (t : Z) : bool :=
  negb (andb ddsp (negb (Z.eqb t Z0))).                  (* :255 *)

Definition rcv_ctx_ok (ddsp : bool) : bool := negb ddsp.  (* :250 *)

Lemma under_disabled_dispatch_only_a_poll_may_send : forall t,
    snd_ctx_ok true t = true -> Z.eqb t Z0 = true.
Proof.
  intros t H. destruct t; vm_compute in H;
    [ reflexivity | discriminate H | discriminate H ].
Qed.

Lemma a_poll_passes_the_dispatch_test_in_any_context : forall ddsp t,
    Z.eqb t Z0 = true -> snd_ctx_ok ddsp t = true.
Proof.
  intros ddsp t H. unfold snd_ctx_ok. rewrite H. cbn [negb andb].
  destruct ddsp; reflexivity.
Qed.

Lemma no_figure_saves_a_receive : forall (ddsp : bool) (t : Z),
    ddsp = true -> rcv_ctx_ok ddsp = false.
Proof. intros ddsp t H. unfold rcv_ctx_ok. rewrite H. reflexivity. Qed.

Example a_disabled_dispatch_allows_a_polling_send_but_no_receive :
    snd_ctx_ok true Z0 = true /\ rcv_ctx_ok true = false.
Proof. split; reflexivity. Qed.

(* The sentinel test and the arithmetic test agree because the only figure that
 * is neither positive nor negative is the poll; the receive's test has no
 * figure to agree with.  Stated as a law, this is what licenses modelling
 * "blocks" as the negation of "== 0" (section 3's tmo_blocks_is_the_sentinel_test)
 * inside a guard cascade that reads the raw caller figure. *)
Lemma the_sentinel_survives_the_reading : forall t,
    Z.eqb (tmo_code (btron_tmo t)) Z0 = Z.eqb t Z0.
Proof.
  intros t. destruct t; cbn [btron_tmo Z.ltb Z.eqb tmo_code]; reflexivity.
Qed.

(* The two timeout vocabularies of this pair of services now separate cleanly.
 * _tk_snd_mbf refuses -5 with E_PAR at :371; rcv_msg (ipc_msg.c:71-72) has no
 * timeout guard at all, so the same figure means "wait forever" there.  The
 * model has to keep both readings, and 15.3.2's btron_tmo is the second one. *)
Lemma the_btron_family_accepts_a_figure_the_kernel_refuses :
    btron_tmo (Z.opp 5) = TMO_FEVR /\ tmout_ok (Z.opp 5) = false.
Proof.
  split.
  - apply (any_negative_figure_waits_forever (Z.opp 5)). reflexivity.
  - unfold tmout_ok. reflexivity.
Qed.

(* ... and the order of the two guards decides which of the two readings a
 * caller of _tk_snd_mbf ever learns about: :371 fires before :372, so -5 is
 * a parameter error even in a context where the dispatch test would have let
 * it through. *)
Example the_figure_guard_precedes_the_context_guard :
    tmout_ok (Z.opp 5) = false /\ snd_ctx_ok false (Z.opp 5) = true.
Proof. split; reflexivity. Qed.

(* ── 15.5.4 The two guard cascades: seven tests to send, five to receive ── *)

(* config.h:64-69 gives the message-buffer family the same affine ID geometry
 * as the semaphore, the event flag and the mailbox, so CHK_MBFID (:67) is the
 * shared chk_id of section 5 rather than a fifth implementation of it.
 * MAX_MBFID is max_mbfid, i.e. MIN_MBFID + NUM_MBFID - 1, so the half-open
 * form chk_id uses is the closed form the macro writes.  The ID argument is a
 * signed int32 in C and a nat here, exactly as in sections 5-7: a negative
 * figure fails the same left conjunct, and the model's domain simply cannot
 * name it. *)
Definition mbfid_ok (id : nat) : bool := chk_id min_mbfid num_mbf id.  (* :67 *)

Lemma an_id_below_the_family_minimum_is_refused :
    mbfid_ok 0 = false /\ mbfid_ok (min_mbfid + num_mbf) = false.
Proof. unfold mbfid_ok, min_mbfid, num_mbf. vm_compute. split; reflexivity. Qed.

(* :370 is CHECK_PAR(msgsz > 0) and :382 is "msgsz > mbfcb->maxmsz".  The first
 * is a property of the argument, the second of the object, and both answer
 * E_PAR -- see the example below, which is the same hazard 15.4.2 records for
 * the B-TRON send, with three tests rather than two sharing one code. *)
Definition msgsz_positive (sz : nat) : bool := Nat.ltb 0 sz.             (* :370 *)
Definition within_maxmsz (sz maxmsz : nat) : bool := Nat.leb sz maxmsz.  (* :382 *)

(* :389 reads "!in_indp() && is_diswai(mbfcb, ctxtsk, TTW_SMBF)", and :453
 * reads "is_diswai(mbfcb, ctxtsk, TTW_RMBF)" -- the same test with no
 * independent-context escape.  (Each casts its first argument to a generic
 * object control block; the cast is dropped here only because the lexical
 * form of a pointer cast ends a Roq comment.)  The receive's predicate
 * therefore ignores the figure it is handed; that is the asymmetry, not a
 * modelling convenience, and the two-argument form is what makes it
 * stateable. *)
Definition send_wait_enabled (indp diswai : bool) : bool :=
  orb indp (negb diswai).                                                (* :389 *)

Definition rcv_wait_enabled (indp diswai : bool) : bool := negb diswai.  (* :453 *)

Example the_two_services_read_the_disable_flag_differently :
    send_wait_enabled true true = true /\ rcv_wait_enabled true true = false.
Proof. split; reflexivity. Qed.

Lemma the_receive_guard_has_no_independent_escape : forall indp diswai,
    rcv_wait_enabled indp diswai = rcv_wait_enabled (negb indp) diswai.
Proof. intros indp diswai. unfold rcv_wait_enabled. reflexivity. Qed.

(* messagebuf.c:369-392: the send's guard block, in source order.  Nothing in
 * this list reads the buffer's counters -- the first read of mbf_free is
 * :403 -- so a refusal here is a refusal the state cannot influence. *)
Record snd_req := mk_snd_req {
  sr_id : nat;
  sr_msgsz : nat;
  sr_tmo : Z;
  sr_ddsp : bool;
  sr_live : bool;
  sr_maxmsz : nat;
  sr_indp : bool;
  sr_top_of_send_queue : bool;
  sr_recv_waits : bool;
  sr_diswai : bool
}.

Definition mbf_snd_guards (q : snd_req) : list (bool * er) :=
  (mbfid_ok (sr_id q), E_ID)
  :: (msgsz_positive (sr_msgsz q), E_PAR)
  :: (tmout_ok (sr_tmo q), E_PAR)
  :: (snd_ctx_ok (sr_ddsp q) (sr_tmo q), E_CTX)
  :: (sr_live q, E_NOEXS)
  :: (within_maxmsz (sr_msgsz q) (sr_maxmsz q), E_PAR)
  :: (send_wait_enabled (sr_indp q) (sr_diswai q), E_DISWAI)
  :: nil.

Definition mbf_snd_refusal (q : snd_req) : option er :=
  first_bad (mbf_snd_guards q).

Lemma seven_guard_classification : forall p1 p2 p3 p4 p5 p6 p7 : bool,
    first_bad ((p1, E_ID) :: (p2, E_PAR) :: (p3, E_PAR) :: (p4, E_CTX)
      :: (p5, E_NOEXS) :: (p6, E_PAR) :: (p7, E_DISWAI) :: nil)
    = match p1, p2, p3, p4, p5, p6, p7 with
      | true, true, true, true, true, true, true => @None er
      | true, true, true, true, true, true, false => Some E_DISWAI
      | true, true, true, true, true, false, _ => Some E_PAR
      | true, true, true, true, false, _, _ => Some E_NOEXS
      | true, true, true, false, _, _, _ => Some E_CTX
      | true, true, false, _, _, _, _ => Some E_PAR
      | true, false, _, _, _, _, _ => Some E_PAR
      | false, _, _, _, _, _, _ => Some E_ID
      end.
Proof. destruct p1, p2, p3, p4, p5, p6, p7; reflexivity. Qed.

(* The seven tests answer with five codes.  Stated as a decidable predicate
 * rather than a disjunction, because three of the tests share E_PAR and the
 * interesting claim is the size of the image, not its enumeration. *)
Definition a_messagebuf_guard_code (e : er) : bool :=
  match e with
  | E_ID | E_PAR | E_CTX | E_NOEXS | E_DISWAI => true
  | _ => false
  end.

Lemma a_send_guard_refusal_names_a_listed_receipt : forall q e,
    mbf_snd_refusal q = Some e -> a_messagebuf_guard_code e = true.
Proof.
  intros q e H. unfold mbf_snd_refusal, mbf_snd_guards in H.
  rewrite seven_guard_classification in H.
  destruct (mbfid_ok (sr_id q)), (msgsz_positive (sr_msgsz q)),
    (tmout_ok (sr_tmo q)), (snd_ctx_ok (sr_ddsp q) (sr_tmo q)),
    (sr_live q), (within_maxmsz (sr_msgsz q) (sr_maxmsz q)),
    (send_wait_enabled (sr_indp q) (sr_diswai q)); cbn in H;
    try discriminate H; injection H; intros X; subst e; reflexivity.
Qed.

(* Every guard passes: the cascade is silent exactly when the arguments and the
 * object's own configuration agree. *)
Lemma mbf_snd_guards_pass : forall q,
    mbfid_ok (sr_id q) = true -> msgsz_positive (sr_msgsz q) = true ->
    tmout_ok (sr_tmo q) = true ->
    snd_ctx_ok (sr_ddsp q) (sr_tmo q) = true -> sr_live q = true ->
    within_maxmsz (sr_msgsz q) (sr_maxmsz q) = true ->
    send_wait_enabled (sr_indp q) (sr_diswai q) = true ->
    mbf_snd_refusal q = @None er.
Proof.
  intros q A B C D E F G. unfold mbf_snd_refusal, mbf_snd_guards.
  rewrite seven_guard_classification. rewrite A, B, C, D, E, F, G.
  reflexivity.
Qed.

(* 1. The ID test is first (:369), and it is the only test that does not need
 * the object to exist.  Blindness is stated as the sharpest form available:
 * two requests that agree on the ID answer alike whatever their buffers are. *)
Lemma a_bad_id_is_E_ID : forall q,
    mbfid_ok (sr_id q) = false -> mbf_snd_refusal q = Some E_ID.
Proof.
  intros q H. unfold mbf_snd_refusal, mbf_snd_guards.
  rewrite seven_guard_classification. rewrite H. reflexivity.
Qed.

Lemma the_id_test_blinds_the_rest : forall q1 q2,
    sr_id q1 = sr_id q2 -> mbfid_ok (sr_id q1) = false ->
    mbf_snd_refusal q1 = mbf_snd_refusal q2.
Proof.
  intros q1 q2 I H.
  assert (H2 : mbfid_ok (sr_id q2) = false).
  { rewrite <- I. exact H. }
  rewrite (a_bad_id_is_E_ID q1 H), (a_bad_id_is_E_ID q2 H2). reflexivity.
Qed.

(* 2. msgsz = 0 is refused at :370, before the timeout, the context, the
 * existence of the object, and its maxmsz.  A zero-length message is not a
 * special case anywhere else in the service: HEADERSZ alone would fit, and the
 * ring would then hold a message of no size, which mbf_to_msg :189 would hand
 * back as rcvsz = 0 -- indistinguishable to the caller from the E_OK figure.
 * This guard is what keeps 15.5.5's reply law decodable. *)
Lemma a_zero_size_is_E_PAR : forall q,
    mbfid_ok (sr_id q) = true -> msgsz_positive (sr_msgsz q) = false ->
    mbf_snd_refusal q = Some E_PAR.
Proof.
  intros q A B. unfold mbf_snd_refusal, mbf_snd_guards.
  rewrite seven_guard_classification. rewrite A, B. reflexivity.
Qed.

(* 3-4. :371 and :372 both read the caller's figure, and the order between them
 * is the one recorded in 15.5.3: the range test fires first, so an illegal
 * figure is E_PAR and never E_CTX. *)
Lemma an_illegal_figure_is_E_PAR : forall q,
    mbfid_ok (sr_id q) = true -> msgsz_positive (sr_msgsz q) = true ->
    tmout_ok (sr_tmo q) = false -> mbf_snd_refusal q = Some E_PAR.
Proof.
  intros q A B C. unfold mbf_snd_refusal, mbf_snd_guards.
  rewrite seven_guard_classification. rewrite A, B, C. reflexivity.
Qed.

Lemma a_disabled_dispatch_wait_is_E_CTX : forall q,
    mbfid_ok (sr_id q) = true -> msgsz_positive (sr_msgsz q) = true ->
    tmout_ok (sr_tmo q) = true -> snd_ctx_ok (sr_ddsp q) (sr_tmo q) = false ->
    mbf_snd_refusal q = Some E_CTX.
Proof.
  intros q A B C D. unfold mbf_snd_refusal, mbf_snd_guards.
  rewrite seven_guard_classification. rewrite A, B, C, D. reflexivity.
Qed.

(* 5. :377 is the existence test, and it is the first test inside the critical
 * section -- the object's own configuration (maxmsz) is not read until :382,
 * and the wait queues not until :394.  E_NOEXS is the receipt for a deleted or
 * never-created ID that still lies inside the family range, which is exactly
 * the gap 7's range_is_not_existence records for every family. *)
Lemma a_dead_object_is_E_NOEXS : forall q,
    mbfid_ok (sr_id q) = true -> msgsz_positive (sr_msgsz q) = true ->
    tmout_ok (sr_tmo q) = true -> snd_ctx_ok (sr_ddsp q) (sr_tmo q) = true ->
    sr_live q = false -> mbf_snd_refusal q = Some E_NOEXS.
Proof.
  intros q A B C D E. unfold mbf_snd_refusal, mbf_snd_guards.
  rewrite seven_guard_classification. rewrite A, B, C, D, E. reflexivity.
Qed.

(* 6. The maxmsz test compiles: config.h:146 sets CHK_PAR (1), so :381-386 is
 * live code and E_PAR is a reachable receipt here, not a build-option ghost. *)
Lemma an_oversized_message_is_E_PAR : forall q,
    mbfid_ok (sr_id q) = true -> msgsz_positive (sr_msgsz q) = true ->
    tmout_ok (sr_tmo q) = true -> snd_ctx_ok (sr_ddsp q) (sr_tmo q) = true ->
    sr_live q = true -> within_maxmsz (sr_msgsz q) (sr_maxmsz q) = false ->
    mbf_snd_refusal q = Some E_PAR.
Proof.
  intros q A B C D E F. unfold mbf_snd_refusal, mbf_snd_guards.
  rewrite seven_guard_classification. rewrite A, B, C, D, E, F. reflexivity.
Qed.

Lemma the_size_of_a_message_is_bounded_twice : forall q,
    mbfid_ok (sr_id q) = true -> msgsz_positive (sr_msgsz q) = true ->
    tmout_ok (sr_tmo q) = true -> snd_ctx_ok (sr_ddsp q) (sr_tmo q) = true ->
    sr_live q = true -> within_maxmsz (sr_msgsz q) (sr_maxmsz q) = true ->
    send_wait_enabled (sr_indp q) (sr_diswai q) = false ->
    mbf_snd_refusal q = Some E_DISWAI.
Proof.
  intros q A B C D E F G. unfold mbf_snd_refusal, mbf_snd_guards.
  rewrite seven_guard_classification. rewrite A, B, C, D, E, F, G.
  reflexivity.
Qed.

(* 7. Three of the seven tests answer E_PAR, so the receipt alone never names
 * its line.  The three concrete requests below differ in every field the
 * guards read after the one that fired, and agree on the figure. *)
Definition snd_zero_size : snd_req :=
  mk_snd_req 1 0 Z0 false true 32 false false false false.
Definition snd_bad_figure : snd_req :=
  mk_snd_req 1 8 (Z.opp 5) false true 32 false false false false.
Definition snd_over_maxmsz : snd_req :=
  mk_snd_req 1 40 Z0 false true 32 false false false false.

Example the_parameter_error_names_three_tests :
    mbf_snd_refusal snd_zero_size = Some E_PAR
    /\ mbf_snd_refusal snd_bad_figure = Some E_PAR
    /\ mbf_snd_refusal snd_over_maxmsz = Some E_PAR
    /\ msgsz_positive (sr_msgsz snd_zero_size) = false
    /\ tmout_ok (sr_tmo snd_bad_figure) = false
    /\ within_maxmsz (sr_msgsz snd_over_maxmsz) (sr_maxmsz snd_over_maxmsz) = false.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* messagebuf.c:440-456: the receive's guard block.  Five tests, no size test
 * (the caller supplies only a buffer to write into), and the context test in
 * its unconditional form. *)
Record rcv_req := mk_rcv_req {
  rr_id : nat;
  rr_tmo : Z;
  rr_ddsp : bool;
  rr_live : bool;
  rr_diswai : bool;
  rr_send_waits : bool;
  rr_stored_msgsz : nat;
  rr_handoff_msgsz : nat
}.

Definition mbf_rcv_guards (q : rcv_req) : list (bool * er) :=
  (mbfid_ok (rr_id q), E_ID)
  :: (tmout_ok (rr_tmo q), E_PAR)
  :: (rcv_ctx_ok (rr_ddsp q), E_CTX)
  :: (rr_live q, E_NOEXS)
  :: (rcv_wait_enabled false (rr_diswai q), E_DISWAI)
  :: nil.

Definition mbf_rcv_refusal (q : rcv_req) : option er :=
  first_bad (mbf_rcv_guards q).

Lemma five_guard_classification : forall p1 p2 p3 p4 p5 : bool,
    first_bad ((p1, E_ID) :: (p2, E_PAR) :: (p3, E_CTX)
      :: (p4, E_NOEXS) :: (p5, E_DISWAI) :: nil)
    = match p1, p2, p3, p4, p5 with
      | true, true, true, true, true => @None er
      | true, true, true, true, false => Some E_DISWAI
      | true, true, true, false, _ => Some E_NOEXS
      | true, true, false, _, _ => Some E_CTX
      | true, false, _, _, _ => Some E_PAR
      | false, _, _, _, _ => Some E_ID
      end.
Proof. destruct p1, p2, p3, p4, p5; reflexivity. Qed.

Lemma a_receive_guard_refusal_names_a_listed_receipt : forall q e,
    mbf_rcv_refusal q = Some e -> a_messagebuf_guard_code e = true.
Proof.
  intros q e H. unfold mbf_rcv_refusal, mbf_rcv_guards in H.
  rewrite five_guard_classification in H.
  destruct (mbfid_ok (rr_id q)), (tmout_ok (rr_tmo q)), (rcv_ctx_ok (rr_ddsp q)),
    (rr_live q), (rcv_wait_enabled false (rr_diswai q)); cbn in H;
    try discriminate H; injection H; intros X; subst e; reflexivity.
Qed.

Lemma mbf_rcv_guards_pass : forall q,
    mbfid_ok (rr_id q) = true -> tmout_ok (rr_tmo q) = true ->
    rcv_ctx_ok (rr_ddsp q) = true -> rr_live q = true ->
    rr_diswai q = false -> mbf_rcv_refusal q = @None er.
Proof.
  intros q A B C D E. unfold mbf_rcv_refusal, mbf_rcv_guards.
  rewrite five_guard_classification. rewrite A, B, C, D, E. reflexivity.
Qed.

Lemma a_receive_bad_id_is_E_ID : forall q,
    mbfid_ok (rr_id q) = false -> mbf_rcv_refusal q = Some E_ID.
Proof.
  intros q H. unfold mbf_rcv_refusal, mbf_rcv_guards.
  rewrite five_guard_classification. rewrite H. reflexivity.
Qed.

(* The receive has no msgsz argument, so its second test is the figure alone,
 * and the second place it holds in the cascade is the same one the send gives
 * it.  A bad timeout is one bug with one receipt across the pair. *)
Lemma a_bad_figure_is_E_PAR : forall q,
    mbfid_ok (rr_id q) = true -> tmout_ok (rr_tmo q) = false ->
    mbf_rcv_refusal q = Some E_PAR.
Proof.
  intros q A B. unfold mbf_rcv_refusal, mbf_rcv_guards.
  rewrite five_guard_classification. rewrite A, B. reflexivity.
Qed.

Lemma the_two_cascades_agree_on_the_figure_test : forall q s,
    tmout_ok (sr_tmo s) = false -> tmout_ok (rr_tmo q) = false ->
    mbfid_ok (sr_id s) = true -> msgsz_positive (sr_msgsz s) = true ->
    mbfid_ok (rr_id q) = true ->
    mbf_snd_refusal s = Some E_PAR /\ mbf_rcv_refusal q = Some E_PAR.
Proof.
  intros q s C D A B E. split.
  - apply (an_illegal_figure_is_E_PAR s A B C).
  - apply (a_bad_figure_is_E_PAR q E D).
Qed.

(* The remaining three tests, in source order.  A dead object still has an ID
 * inside the family range -- 7's range_is_not_existence holds for this family
 * too -- which is why the existence test is not a formality the range test
 * subsumes. *)
Lemma a_dead_receive_is_E_NOEXS : forall q,
    mbfid_ok (rr_id q) = true -> tmout_ok (rr_tmo q) = true ->
    rcv_ctx_ok (rr_ddsp q) = true -> rr_live q = false ->
    mbf_rcv_refusal q = Some E_NOEXS.
Proof.
  intros q A B C D. unfold mbf_rcv_refusal, mbf_rcv_guards.
  rewrite five_guard_classification. rewrite A, B, C, D. reflexivity.
Qed.

Lemma a_disabled_dispatch_is_E_CTX_for_the_receive_too : forall q,
    mbfid_ok (rr_id q) = true -> tmout_ok (rr_tmo q) = true ->
    rcv_ctx_ok (rr_ddsp q) = false -> mbf_rcv_refusal q = Some E_CTX.
Proof.
  intros q A B C. unfold mbf_rcv_refusal, mbf_rcv_guards.
  rewrite five_guard_classification. rewrite A, B, C. reflexivity.
Qed.

Lemma a_disabled_receive_wait_is_E_DISWAI : forall q,
    mbfid_ok (rr_id q) = true -> tmout_ok (rr_tmo q) = true ->
    rcv_ctx_ok (rr_ddsp q) = true -> rr_live q = true -> rr_diswai q = true ->
    mbf_rcv_refusal q = Some E_DISWAI.
Proof.
  intros q A B C D E. unfold mbf_rcv_refusal, mbf_rcv_guards.
  rewrite five_guard_classification. rewrite A, B, C, D, E. reflexivity.
Qed.

(* Both services read the object's existence before anything about the object's
 * contents, so the receipt of a call to a deleted buffer does not depend on
 * whether the buffer is empty, full, or has a sender parked in it -- the queue
 * state 15.5.5 defines the routes over is unreachable from here. *)
Lemma a_dead_object_blinds_the_queue_state : forall q1 q2,
    rr_id q1 = rr_id q2 -> rr_live q1 = false -> rr_live q2 = false ->
    mbfid_ok (rr_id q1) = true -> tmout_ok (rr_tmo q1) = true ->
    rcv_ctx_ok (rr_ddsp q1) = true -> tmout_ok (rr_tmo q2) = true ->
    rcv_ctx_ok (rr_ddsp q2) = true ->
    mbf_rcv_refusal q1 = mbf_rcv_refusal q2.
Proof.
  intros q1 q2 I L1 L2 A B C D E.
  assert (M : mbfid_ok (rr_id q2) = true).
  { rewrite <- I. exact A. }
  rewrite (a_dead_receive_is_E_NOEXS q1 A B C L1).
  rewrite (a_dead_receive_is_E_NOEXS q2 M D E L2). reflexivity.
Qed.

(* ── 15.5.5 Past the guards: three routes each, and the reply law ── *)

(* messagebuf.c:394-416 is the body of the send, and it is a three-way chain:
 * a waiting receiver takes the message directly out of the caller's buffer,
 * failing that an eligible caller with room stores it in the ring, failing
 * that the call answers E_TMOUT and -- unless the figure is a poll -- takes a
 * place in the send-wait queue.  :401's eligibility disjunct is the queue
 * ownership test, gcb_top_of_wait_queue(...) == ctxtsk: a caller may only
 * store ahead of tasks that asked to be served first. *)
Definition snd_eligible (q : snd_req) : bool :=
  orb (sr_indp q) (sr_top_of_send_queue q).                (* :401 *)

Inductive snd_route := RT_HANDOFF | RT_STORE | RT_TIMEOUT.  (* :394 :403 :406 *)

Definition mbf_snd_route (q : snd_req) (m : mbf) : snd_route :=
  if sr_recv_waits q
  then RT_HANDOFF
  else if andb (snd_eligible q) (mbf_admits (mb_free m) (sr_msgsz q))
       then RT_STORE
       else RT_TIMEOUT.

(* The three projections are the C's three effects: the figure it returns, the
 * buffer it leaves behind, and whether the call returns at all.  A parked send
 * has not returned, so mbf_snd_service hands back None, the same convention
 * 15.4.3 uses for a blocking receive miss; when the park does end, E_TMOUT is
 * the figure the kernel stored in ctxtsk->wercd at :410, which is why the
 * receipt below already names it. *)
Definition mbf_snd_receipt (q : snd_req) (m : mbf) : er :=
  match mbf_snd_refusal q with
  | Some e => e
  | None => match mbf_snd_route q m with
            | RT_HANDOFF | RT_STORE => E_OK
            | RT_TIMEOUT => E_TMOUT
            end
  end.

Definition mbf_snd_next (q : snd_req) (m : mbf) : mbf :=
  match mbf_snd_refusal q with
  | Some _ => m
  | None => match mbf_snd_route q m with
            | RT_STORE => mbf_store m (sr_msgsz q)
            | _ => m
            end
  end.

Definition mbf_snd_parked (q : snd_req) (m : mbf) : bool :=
  match mbf_snd_refusal q with
  | Some _ => false
  | None => match mbf_snd_route q m with
            | RT_TIMEOUT => negb (Z.eqb (sr_tmo q) Z0)
            | _ => false
            end
  end.

Definition mbf_snd_service (q : snd_req) (m : mbf) : option (er * mbf) :=
  if mbf_snd_parked q m
  then @None (er * mbf)
  else Some (mbf_snd_receipt q m, mbf_snd_next q m).

Lemma a_handoff_goes_around_the_ring : forall q m,
    mbf_snd_refusal q = @None er -> sr_recv_waits q = true ->
    mbf_snd_route q m = RT_HANDOFF
    /\ mbf_snd_next q m = m /\ mbf_snd_receipt q m = E_OK.
Proof.
  intros q m Ru W.
  assert (Rt : mbf_snd_route q m = RT_HANDOFF)
    by (unfold mbf_snd_route; rewrite W; reflexivity).
  unfold mbf_snd_next, mbf_snd_receipt. rewrite Ru, Rt.
  repeat split; reflexivity.
Qed.

(* :394 is tested before :403's mbf_free, so a receiver that is already waiting
 * takes the message from a full buffer: the ring is not consulted, and the
 * caller's own buffer is the destination.  This is the send-side reason
 * 15.5.2's empty/full ambiguity matters -- the buffer can be full and still
 * pass traffic. *)
Lemma a_handoff_needs_no_free_space : forall q m,
    mbf_snd_refusal q = @None er -> sr_recv_waits q = true ->
    mbf_snd_service q m = Some (E_OK, m).
Proof.
  intros q m Ru W.
  assert (Rt : mbf_snd_route q m = RT_HANDOFF)
    by (exact (proj1 (a_handoff_goes_around_the_ring q m Ru W))).
  unfold mbf_snd_service, mbf_snd_parked, mbf_snd_receipt, mbf_snd_next.
  rewrite Ru, Rt. reflexivity.
Qed.

Definition snd_to_a_waiting_receiver : snd_req :=
  mk_snd_req 1 4 Z0 false true 32 false false true false.

Example a_full_buffer_still_hands_a_message_over :
    mbf_snd_service snd_to_a_waiting_receiver mbf_full8 = Some (E_OK, mbf_full8)
    /\ mb_free mbf_full8 = 0.
Proof. repeat split; vm_compute; reflexivity. Qed.

Lemma a_store_is_eligible_and_there_is_room : forall q m,
    mbf_snd_route q m = RT_STORE ->
    snd_eligible q = true /\ mbf_admits (mb_free m) (sr_msgsz q) = true.
Proof.
  intros q m R. unfold mbf_snd_route in R.
  destruct (sr_recv_waits q) eqn:W; try discriminate R.
  destruct (andb (snd_eligible q) (mbf_admits (mb_free m) (sr_msgsz q))) eqn:Ad;
    try discriminate R.
  apply andb_true_iff in Ad. destruct Ad as [El Fr]. split; assumption.
Qed.

Lemma a_timeout_means_no_ownership_or_no_room : forall q m,
    mbf_snd_route q m = RT_TIMEOUT ->
    sr_recv_waits q = false
    /\ (snd_eligible q = false
        \/ mbf_admits (mb_free m) (sr_msgsz q) = false).
Proof.
  intros q m R. unfold mbf_snd_route in R.
  destruct (sr_recv_waits q) eqn:W; try discriminate R.
  destruct (andb (snd_eligible q) (mbf_admits (mb_free m) (sr_msgsz q))) eqn:Ad;
    try discriminate R.
  apply andb_false_iff in Ad. split; [ reflexivity | exact Ad ].
Qed.

(* The one write in the whole service.  Stated as the disjunction it is: either
 * the buffer is exactly as the caller found it, or this is the store route and
 * the change is the charge of the message the caller named. *)
Lemma nothing_but_a_store_moves_the_buffer : forall q m,
    mbf_snd_next q m = m
    \/ (mbf_snd_refusal q = @None er /\ mbf_snd_route q m = RT_STORE
        /\ mbf_snd_next q m = mbf_store m (sr_msgsz q)).
Proof.
  intros q m. unfold mbf_snd_next, mbf_snd_route, mbf_snd_refusal.
  destruct (first_bad (mbf_snd_guards q)) as [e|]; [ left; reflexivity | ].
  destruct (sr_recv_waits q); [ left; reflexivity | ].
  destruct (andb (snd_eligible q) (mbf_admits (mb_free m) (sr_msgsz q))).
  - right. repeat repeat split; reflexivity.
  - left. reflexivity.
Qed.

(* The admission test at :403 and the debit at :414->:133 are two different
 * figures (15.5.1), so the route test on its own does not say the debit fits.
 * On a live buffer it does: the counter is a difference of aligned quantities,
 * so the rounded charge is within what the unrounded test admitted. *)
Lemma a_free_boundary_is_a_multiple_of_the_round : forall m l,
    mb_free m = Nat.sub (mb_bufsz m) (mbf_debits l) ->
    mb_bufsz m mod 4 = 0 -> mb_free m mod 4 = 0.
Proof.
  intros m l E A. rewrite E.
  destruct (debits_are_a_multiple_of_the_round l) as [k Dk].
  assert (DL : mbf_debits l mod 4 = 0) by (rewrite Dk; apply a_block_is_aligned).
  apply (the_difference_of_two_aligned_counts_is_aligned (mb_bufsz m) (mbf_debits l));
    assumption.
Qed.

Theorem a_store_pays_what_it_was_admitted : forall q m l,
    mb_bufsz m mod 4 = 0 ->
    mb_free m = Nat.sub (mb_bufsz m) (mbf_debits l) ->
    mbf_snd_refusal q = @None er -> mbf_snd_route q m = RT_STORE ->
    mbf_charge (sr_msgsz q) <= mb_free m.
Proof.
  intros q m l A F Ru Rt.
  destruct (a_store_is_eligible_and_there_is_room q m Rt) as [El Fr].
  apply (an_aligned_count_never_underpays (mb_free m) (sr_msgsz q)).
  - apply (a_free_boundary_is_a_multiple_of_the_round m l F A).
  - exact Fr.
Qed.

(* A caller that cannot store learns it immediately when it only polled (:406
 * sets ercd = E_TMOUT and the :408 test declines to queue), and waits when it
 * did not.  Both branches leave the message in the caller's own buffer, so the
 * accept loop of 15.5.6 is what moves it. *)
Lemma a_polling_miss_returns_E_TMOUT_at_once : forall q m,
    mbf_snd_refusal q = @None er -> mbf_snd_route q m = RT_TIMEOUT ->
    Z.eqb (sr_tmo q) Z0 = true ->
    mbf_snd_service q m = Some (E_TMOUT, m).
Proof.
  intros q m Ru Rt P. unfold mbf_snd_service, mbf_snd_parked, mbf_snd_receipt,
    mbf_snd_next.
  rewrite Ru, Rt, P. cbn [negb]. reflexivity.
Qed.

Lemma a_blocking_miss_parks_and_writes_nothing : forall q m,
    mbf_snd_refusal q = @None er -> mbf_snd_route q m = RT_TIMEOUT ->
    Z.eqb (sr_tmo q) Z0 = false ->
    mbf_snd_service q m = @None (er * mbf) /\ mbf_snd_next q m = m.
Proof.
  intros q m Ru Rt P. split.
  - unfold mbf_snd_service, mbf_snd_parked. rewrite Ru, Rt, P. reflexivity.
  - unfold mbf_snd_next. rewrite Ru, Rt. reflexivity.
Qed.

(* The receive, messagebuf.c:458-484: read from the ring if it is not empty,
 * take from the head of the send-wait queue if a sender is parked, otherwise
 * time out or park.  The emptiness test is the counter at :121 -- the model
 * reads it from the buffer rather than carrying a flag, because 15.5.2 shows
 * the cursor pair does not determine it. *)
Inductive rcv_route := RR_READ | RR_HANDOFF | RR_TIMEOUT.  (* :458 :464 :472 *)

Definition mbf_rcv_route (q : rcv_req) (m : mbf) : rcv_route :=
  if mbf_is_empty m
  then if rr_send_waits q then RR_HANDOFF else RR_TIMEOUT
  else RR_READ.

Definition mbf_rcv_receipt (q : rcv_req) (m : mbf) : er :=
  match mbf_rcv_refusal q with
  | Some e => e
  | None => match mbf_rcv_route q m with
            | RR_READ | RR_HANDOFF => E_OK
            | RR_TIMEOUT => E_TMOUT
            end
  end.

(* rcvsz in the C (:437) is assigned only on the two accepting branches; on the
 * :472 branch it holds whatever the declaration left there.  The model writes 0
 * for that case, and the reply law below is the proof that the choice cannot be
 * seen: :489 reads rcvsz only when ercd is E_OK. *)
Definition mbf_rcv_size (q : rcv_req) (m : mbf) : nat :=
  match mbf_rcv_refusal q with
  | Some _ => 0
  | None => match mbf_rcv_route q m with
            | RR_READ => rr_stored_msgsz q
            | RR_HANDOFF => rr_handoff_msgsz q
            | RR_TIMEOUT => 0
            end
  end.

Definition mbf_rcv_next (q : rcv_req) (m : mbf) : mbf :=
  match mbf_rcv_refusal q with
  | Some _ => m
  | None => match mbf_rcv_route q m with
            | RR_READ => mbf_read m (rr_stored_msgsz q)
            | _ => m
            end
  end.

Definition mbf_rcv_parked (q : rcv_req) (m : mbf) : bool :=
  match mbf_rcv_refusal q with
  | Some _ => false
  | None => match mbf_rcv_route q m with
            | RR_TIMEOUT => negb (Z.eqb (rr_tmo q) Z0)
            | _ => false
            end
  end.

(* :489 is the whole of the reply law, and it is the reason this service is not
 * shaped like the others in the file: the success value is a length, not E_OK. *)
Definition mbf_rcv_reply (ercd : er) (rcvsz : nat) : Z :=
  if er_ok ercd then Z.of_nat rcvsz else er_code ercd.      (* :489 *)

Definition mbf_rcv_answer (q : rcv_req) (m : mbf) : Z :=
  mbf_rcv_reply (mbf_rcv_receipt q m) (mbf_rcv_size q m).

Definition mbf_rcv_service (q : rcv_req) (m : mbf) : option (Z * mbf) :=
  if mbf_rcv_parked q m
  then @None (Z * mbf)
  else Some (mbf_rcv_answer q m, mbf_rcv_next q m).

Lemma a_read_answers_with_the_size_it_read : forall q m,
    mbf_rcv_refusal q = @None er -> mbf_rcv_route q m = RR_READ ->
    mbf_rcv_answer q m = Z.of_nat (rr_stored_msgsz q)
    /\ mbf_rcv_next q m = mbf_read m (rr_stored_msgsz q).
Proof.
  intros q m Ru Rt. unfold mbf_rcv_answer, mbf_rcv_reply, mbf_rcv_receipt,
    mbf_rcv_size, mbf_rcv_next.
  rewrite Ru, Rt. repeat split; cbn [er_ok er_mer]; reflexivity.
Qed.

(* The read credits the counter by the charge of the size it just took out of
 * the header (:167), which is the same figure msg_to_mbf debited at :133 --
 * so the two services really are inverses on the counter, even though the
 * admission test that let the store through was the unrounded one. *)
Lemma a_read_credits_the_charge_of_its_own_header : forall q m,
    mbf_rcv_refusal q = @None er -> mbf_rcv_route q m = RR_READ ->
    mb_free (mbf_rcv_next q m) = mb_free m + mbf_charge (rr_stored_msgsz q).
Proof.
  intros q m Ru Rt. unfold mbf_rcv_next. rewrite Ru, Rt.
  unfold mbf_read. cbn [mb_free]. reflexivity.
Qed.

Lemma a_handoff_answers_with_the_senders_size : forall q m,
    mbf_rcv_refusal q = @None er -> mbf_is_empty m = true ->
    rr_send_waits q = true ->
    mbf_rcv_answer q m = Z.of_nat (rr_handoff_msgsz q)
    /\ mbf_rcv_next q m = m.
Proof.
  intros q m Ru E W. unfold mbf_rcv_answer, mbf_rcv_reply, mbf_rcv_receipt,
    mbf_rcv_size, mbf_rcv_next, mbf_rcv_route.
  rewrite Ru, E, W. repeat split; cbn [er_ok er_mer]; reflexivity.
Qed.

Lemma a_miss_with_a_poll_answers_the_timeout : forall q m,
    mbf_rcv_refusal q = @None er -> mbf_rcv_route q m = RR_TIMEOUT ->
    Z.eqb (rr_tmo q) Z0 = true ->
    mbf_rcv_service q m = Some (er_code E_TMOUT, m).
Proof.
  intros q m Ru Rt P. unfold mbf_rcv_service, mbf_rcv_parked, mbf_rcv_answer,
    mbf_rcv_reply, mbf_rcv_receipt, mbf_rcv_size, mbf_rcv_next.
  rewrite Ru, Rt, P. cbn [negb er_ok er_mer]. reflexivity.
Qed.

(* None of the five guard codes is E_OK, so a refused receive always takes the
 * else-branch of :489. *)
Lemma er_ok_is_false_for_a_guard_code : forall e,
    a_messagebuf_guard_code e = true -> er_ok e = false.
Proof. destruct e; vm_compute; intros C; try discriminate C; reflexivity. Qed.

Lemma a_refused_receive_answers_with_its_own_receipt : forall q m e,
    mbf_rcv_refusal q = Some e -> a_messagebuf_guard_code e = true ->
    mbf_rcv_answer q m = er_code e.
Proof.
  intros q m e G C. unfold mbf_rcv_answer, mbf_rcv_reply, mbf_rcv_receipt.
  rewrite G, (er_ok_is_false_for_a_guard_code e C). reflexivity.
Qed.

(* The hazard :489 creates for a caller: the return value lives in two
 * namespaces at once, and the only thing that separates them is the sign.
 * A length is positive (the send's :370 guard is what makes a stored message
 * non-empty) and a receipt is at most -65536, so no answer of this service is
 * ever the E_OK figure a task-sleep call would return. *)
Lemma the_receive_answer_is_never_the_success_figure : forall q m,
    mbf_rcv_refusal q = @None er -> 0 < rr_stored_msgsz q ->
    0 < rr_handoff_msgsz q ->
    Z.ltb (mbf_rcv_answer q m) Z0 = true
    \/ Z.ltb Z0 (mbf_rcv_answer q m) = true.
Proof.
  intros q m Ru S1 S2. unfold mbf_rcv_answer, mbf_rcv_reply, mbf_rcv_receipt,
    mbf_rcv_size.
  rewrite Ru.
  destruct (mbf_rcv_route q m); cbn [er_ok er_mer Nat.eqb].
  - right. lia.
  - right. lia.
  - left. unfold er_code, er_mer. vm_compute; reflexivity.
Qed.

(* The mirror image: a successful send has no size to report, so it answers with
 * the E_OK figure.  The two services of one pair therefore return different
 * shapes -- an ER on one side, a length on the other -- and the only figure
 * they share is zero, which neither of them can produce. *)
Lemma a_store_answers_E_OK_with_the_message_written : forall q m,
    mbf_snd_refusal q = @None er -> mbf_snd_route q m = RT_STORE ->
    mbf_snd_service q m = Some (E_OK, mbf_store m (sr_msgsz q)).
Proof.
  intros q m Ru Rt. unfold mbf_snd_service, mbf_snd_parked, mbf_snd_receipt,
    mbf_snd_next.
  rewrite Ru, Rt. reflexivity.
Qed.

(* ── 15.5.6 The accept loop: mbf_wakeup takes a prefix, not a scan ───── *)

(* messagebuf.c:198-214 drains the send-wait queue of the buffer whose space
 * just freed up.  Each step re-reads the free count (:206), so the loop stops
 * at the first sender it cannot serve and leaves that sender and everyone
 * behind it queued.  This is a different drain from section 14's cell drain and
 * from section 12's drain_head: it moves bytes, so whether a step happens at
 * all depends on the counter 15.5.2 introduced. *)
Fixpoint mbf_drain (m : mbf) (q : list nat) : mbf * list nat :=
  match q with
  | nil => (m, nil)
  | sz :: rest => if mbf_admits (mb_free m) sz
                  then mbf_drain (mbf_store m sz) rest
                  else (m, q)
  end.

Lemma a_refused_head_stops_the_loop : forall m sz rest,
    mbf_admits (mb_free m) sz = false ->
    mbf_drain m (sz :: rest) = (m, sz :: rest).
Proof. intros m sz rest A. cbn [mbf_drain]. rewrite A. reflexivity. Qed.

Lemma a_fitting_head_is_stored_before_the_next_is_seen : forall m sz rest,
    mbf_admits (mb_free m) sz = true ->
    mbf_drain m (sz :: rest) = mbf_drain (mbf_store m sz) rest.
Proof. intros m sz rest A. cbn [mbf_drain]. rewrite A. reflexivity. Qed.

Lemma the_free_count_never_rises_under_a_drain : forall q m,
    mb_free (fst (mbf_drain m q)) <= mb_free m.
Proof.
  induction q as [|sz rest IH]; intros m.
  - cbn [mbf_drain fst]. apply Nat.le_refl.
  - destruct (mbf_admits (mb_free m) sz) eqn:A.
    + cbn [mbf_drain]. rewrite A.
      assert (CS : mb_free (mbf_store m sz)
                     = Nat.sub (mb_free m) (mbf_charge sz))
        by (unfold mbf_store; reflexivity).
      assert (H := IH (mbf_store m sz)). rewrite CS in H. lia.
    + cbn [mbf_drain]. rewrite A. apply Nat.le_refl.
Qed.

(* The loop never reorders the queue and never loses a sender: what it accepts
 * is a prefix, and what it leaves is the matching suffix.  On a live buffer the
 * debit of that prefix is exact -- which is where 15.5.1's alignment work
 * earns its keep, since an unrounded admission on an unaligned counter would
 * make the subtracted debit larger than the space taken. *)
Theorem the_accept_loop_pays_for_what_it_accepts : forall q,
    forall (m : mbf) l,
      mb_bufsz m mod 4 = 0 ->
      mb_free m = Nat.sub (mb_bufsz m) (mbf_debits l) ->
      exists accepted,
        q = accepted ++ snd (mbf_drain m q)
        /\ mb_free (fst (mbf_drain m q))
           = Nat.sub (mb_free m) (mbf_debits accepted).
Proof.
  induction q as [|sz rest IH]; intros m l A F.
  - exists (@nil nat). split; cbn [mbf_drain snd app fst mbf_debits].
    + reflexivity.
    + lia.
  - destruct (mbf_admits (mb_free m) sz) eqn:Ad.
    + assert (ALG : mb_free m mod 4 = 0)
        by (apply (a_free_boundary_is_a_multiple_of_the_round m l); assumption).
      assert (CH : mbf_charge sz <= mb_free m)
        by (apply (an_aligned_count_never_underpays (mb_free m) sz ALG Ad)).
      cbn [mbf_drain]. rewrite Ad.
      destruct (IH (mbf_store m sz) (sz :: l)) as [accepted [Eq Ne]].
      * unfold mbf_store. cbn [mb_bufsz]. exact A.
      * replace (mbf_debits (sz :: l)) with (mbf_charge sz + mbf_debits l)
          by reflexivity.
        unfold mbf_store. rewrite F. cbn [mb_free mb_bufsz]. lia.
      * exists (sz :: accepted). split.
        -- cbn [app]. rewrite <- Eq. reflexivity.
        -- rewrite Ne. cbn [mbf_debits]. unfold mbf_store. cbn [mb_free]. lia.
    + exists (@nil nat). split.
      * cbn [app mbf_drain snd]. rewrite Ad. reflexivity.
      * cbn [mbf_drain fst mbf_debits]. rewrite Ad.
        replace (mbf_debits (@nil nat)) with 0 by reflexivity. rewrite Nat.sub_0_r. reflexivity.
Qed.

(* Head-of-line blocking, computed.  A queue whose first sender cannot be
 * served is not served at all, even though a later sender would fit: a greedy
 * scan would take the 1-byte message, and the ring would then hold a message
 * the receiver reads out of order.  The loop is a prefix loop for the same
 * reason the byte ring is a contiguous buffer. *)
Example a_large_head_blocks_a_small_one_behind_it :
    mbf_drain (mbf_fresh 16) (8 :: 1 :: nil) = (mk_mbf 16 4 0 12, 1 :: nil).
Proof. reflexivity. Qed.

Example an_oversized_head_is_refused_before_the_smaller_one_is_seen :
    mbf_drain (mk_mbf 16 8 0 8) (8 :: 1 :: nil) = (mk_mbf 16 8 0 8, 8 :: 1 :: nil)
    /\ mbf_admits (mb_free (mk_mbf 16 8 0 8)) 8 = false.
Proof. repeat split; reflexivity. Qed.

(* The store that ends at the boundary walks the tail back to zero (:137-139
 * and :149-151), so an accepted sender can leave head and tail coincident with
 * the counter drained -- the empty/full ambiguity of 15.5.2, produced by the
 * accept loop rather than by a send. *)
Example a_split_copy_at_the_end_of_the_buffer_lands_the_cursor_at_zero :
    mbf_drain (mk_mbf 16 8 0 8) (1 :: nil) = (mk_mbf 16 0 0 0, nil)
    /\ mbf_is_empty (mk_mbf 16 0 0 0) = false.
Proof. repeat split; reflexivity. Qed.

(* The counter that a read credits (:168) is the counter the next accept loop
 * reads, so the loop resumes exactly where the drained buffer stopped it. *)
Example the_accept_loop_resumes_when_a_read_frees_space :
    mbf_drain (mk_mbf 16 0 0 0) (1 :: nil) = (mk_mbf 16 0 0 0, 1 :: nil)
    /\ mbf_drain (mbf_read (mk_mbf 16 0 0 0) 4) (1 :: nil)
       = (mk_mbf 16 0 8 8, nil).
Proof. repeat split; reflexivity. Qed.

(* ── 15.6 The two services, computed ───────────────────────────────── *)

(* Everything above this point is a law with hypotheses.  This subsection is the
 * same laws read as figures: a request record per row, the buffer beside it, and
 * the value the service hands back.  These are reflexivity checks on the
 * transcription, which is the only form of evidence this file can offer that the
 * model and the C agree short of running the C -- and the oracle of task 9 runs
 * the same figures the other way round. *)

Definition empty16 : mbf := mbf_fresh 16.

(* The three requests that differ only in what the caller was willing to wait
 * for.  :406 sets E_TMOUT in all three cases; :408 alone decides whether the
 * caller learns it now or later. *)
Definition send_served : snd_req :=
  mk_snd_req 1 4 Z0 false true 32 false true false false.

Definition send_blocked_miss : snd_req :=
  mk_snd_req 1 4 (Z.pos 100) false true 32 false true false false.

Definition send_polled_miss : snd_req :=
  mk_snd_req 1 4 Z0 false true 32 false true false false.

Example a_store_moves_the_tail_and_pays_eight_bytes :
    mbf_snd_service send_served empty16
    = Some (E_OK, mk_mbf 16 8 0 8).
Proof. vm_compute. reflexivity. Qed.

Example the_same_miss_blocks_with_a_deadline_and_returns_with_a_poll :
    mbf_snd_service send_blocked_miss mbf_full8 = @None (er * mbf)
    /\ mbf_snd_service send_polled_miss mbf_full8 = Some (E_TMOUT, mbf_full8).
Proof. repeat split; vm_compute; reflexivity. Qed.

(* The ownership test at :401 is not a capacity test, and this is the case that
 * separates them: the buffer has nothing in it at all, every byte is free, and
 * the send still cannot store, because a message buffer serves its send-wait
 * queue in order and this caller is not at its head.  The E_TMOUT is not "the
 * ring is full"; it is "it is not your turn". *)
Definition send_behind_another_waiter : snd_req :=
  mk_snd_req 1 4 Z0 false true 32 false false false false.

Example an_untitled_sender_times_out_on_an_empty_buffer :
    mbf_snd_service send_behind_another_waiter empty16
    = Some (E_TMOUT, empty16)
    /\ mb_free empty16 = mb_bufsz empty16.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* The four refusals, each with the guard that produces it.  All four leave the
 * buffer exactly as it was, which is 15.5.5's nothing_but_a_store_moves_the_-
 * buffer read as figures rather than as a disjunction. *)
Definition send_to_a_dead_buffer : snd_req :=
  mk_snd_req 1 4 Z0 false false 32 false true false false.

Definition send_larger_than_maxmsz : snd_req :=
  mk_snd_req 1 40 Z0 false true 32 false true false false.

Definition send_while_dispatch_is_disabled : snd_req :=
  mk_snd_req 1 4 (Z.pos 100) true true 32 false true false false.

Definition send_with_an_id_outside_the_family : snd_req :=
  mk_snd_req 20 4 Z0 false true 32 false true false false.

Example each_send_refusal_names_its_own_guard :
    mbf_snd_service send_to_a_dead_buffer empty16 = Some (E_NOEXS, empty16)
    /\ mbf_snd_service send_larger_than_maxmsz empty16 = Some (E_PAR, empty16)
    /\ mbf_snd_service send_while_dispatch_is_disabled empty16
       = Some (E_CTX, empty16)
    /\ mbf_snd_service send_with_an_id_outside_the_family empty16
       = Some (E_ID, empty16).
Proof. repeat split; vm_compute; reflexivity. Qed.

(* The three receives, on the buffer the first send above leaves behind. *)
Definition rcv_a_stored_message : rcv_req :=
  mk_rcv_req 1 (Z.pos 100) false true false false 4 0.

Definition rcv_from_a_waiting_sender : rcv_req :=
  mk_rcv_req 1 (Z.pos 100) false true false true 0 6.

Definition rcv_polled_miss : rcv_req := mk_rcv_req 1 Z0 false true false false 0 0.

Definition rcv_blocked_miss : rcv_req :=
  mk_rcv_req 1 (Z.pos 100) false true false false 0 0.

Example a_read_answers_with_the_length_and_repays_the_charge :
    mbf_rcv_service rcv_a_stored_message (mbf_store empty16 4)
    = Some (4%Z, mk_mbf 16 16 8 8).
Proof. vm_compute. reflexivity. Qed.

Example a_handoff_answers_with_the_senders_length_and_touches_nothing :
    mbf_rcv_service rcv_from_a_waiting_sender empty16
    = Some (6%Z, empty16).
Proof. vm_compute. reflexivity. Qed.

Example a_polled_miss_answers_E_TMOUT_and_a_blocked_miss_does_not_answer :
    mbf_rcv_service rcv_polled_miss empty16
    = Some (er_code E_TMOUT, empty16)
    /\ mbf_rcv_service rcv_blocked_miss empty16 = @None (Z * mbf).
Proof. repeat split; vm_compute; reflexivity. Qed.

(* The wire figures themselves.  er_code is errno.h:27's composition -- the main
 * code shifted by 16 and negated -- and these are the six values this pair of
 * services can produce, so the oracle has constants to compare against rather
 * than names. *)
Example the_six_figures_this_pair_returns :
    er_code E_TMOUT = (Z.opp 3276800)
    /\ er_code E_ID = (Z.opp 1179648)
    /\ er_code E_PAR = (Z.opp 1114112)
    /\ er_code E_CTX = (Z.opp 1638400)
    /\ er_code E_NOEXS = (Z.opp 2752512)
    /\ er_code E_DISWAI = (Z.opp 3407872).
Proof. repeat split; reflexivity. Qed.

Definition rcv_a_dead_buffer : rcv_req :=
  mk_rcv_req 1 (Z.pos 100) false false false false 0 0.

Definition rcv_with_a_disabled_wait : rcv_req :=
  mk_rcv_req 1 (Z.pos 100) false true true false 0 0.

Example a_receive_refusal_is_a_length_that_is_never_zero :
    mbf_rcv_answer rcv_a_dead_buffer empty16 = er_code E_NOEXS
    /\ mbf_rcv_answer rcv_with_a_disabled_wait empty16 = er_code E_DISWAI
    /\ Z.ltb (mbf_rcv_answer rcv_a_dead_buffer empty16) Z0 = true.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* Send then receive, composed: the pair of services is a round trip, and the
 * buffer it returns is the one the store built with the read applied -- full
 * counter, cursors coincident at 8, which is 15.5.2's a_round_trip example seen
 * through the two service wrappers instead of the two cursor updates. *)
Example a_send_then_a_receive_on_the_same_buffer_is_a_round_trip :
    match mbf_snd_service send_served empty16 with
    | Some (_, m) => mbf_rcv_service rcv_a_stored_message m
    | None => @None (Z * mbf)
    end
    = Some (4%Z, mk_mbf 16 16 8 8).
Proof. vm_compute. reflexivity. Qed.

(* The accept loop (:198-214) at the two ends of its range, and the case the
 * split copy is for.  In the third row the header lands at 8, the eight payload
 * bytes straddle the end of the buffer, and the cursor comes back to 4 -- the
 * only place in this section where a message occupies two runs of cells, which
 * is exactly why :141-148 rounds the REDUCED size rather than the original one. *)
Example an_accept_loop_takes_a_prefix_and_leaves_the_rest_queued :
    mbf_drain (mbf_store empty16 4) (4 :: 4 :: nil)
    = (mk_mbf 16 0 0 0, 4 :: nil)
    /\ mbf_drain (mbf_store empty16 4) (8 :: nil)
       = (mk_mbf 16 8 0 8, 8 :: nil).
Proof. repeat split; reflexivity. Qed.

Example the_loop_resumes_after_a_read_and_a_message_can_straddle_the_end :
    mbf_drain (mbf_read (mbf_store empty16 4) 4) (8 :: nil)
    = (mk_mbf 16 4 8 4, @nil nat).
Proof. reflexivity. Qed.

(* ── 15.7 Three copy axes: what BTRON 3.20 mandates and what it does not ── *)

(* "Zero-copy" is not one claim but three, and the standard is not equally tight
 * about each of them.  A message crosses a domain boundary three ways:
 *
 *   - the BODY: the bytes the caller means to move,
 *   - the ENVELOPE: the struct that names the body,
 *   - the QUEUE: the bookkeeping that moves a cell out of the ring.
 *
 * Read against ipc_msg.c, 3.20 mandates exactly one of the three and leaves the
 * other two alone.  15.7.1 is the queue axis, fixable inside the standard;
 * 15.7.2 the body axis, which the standard already leaves to the caller; and
 * 15.7.3 the envelope axis, which it does not, and which therefore needs a
 * surface of its own.  Nothing in 15.7.3 is implemented in this tree: the code
 * below models a proposal and says so at each step. *)

(* ── 15.7.1 The queue axis: a rotating head is the same dequeue for free ── *)

(* ipc_msg.c:100-104 removes a cell by copying every survivor one slot towards
 * the head.  The array is a ring -- :61 advances tail modulo the capacity -- but
 * the dequeue is not: it is a move, and it costs one cell write per message that
 * stays.  A rotating head costs none: the cells do not move, the coordinate does.
 * That is what messagebuf.c:137-151 already does for T-Kernel's byte buffer,
 * where both cursors advance and a message wraps instead of shifting.  So this is
 * not an exotic idea bolted onto B-TRON; it is the neighbouring service's own
 * design.  15's opening observation is the whole obstacle: nothing in ipc_msg.c
 * ever assigns to mb->head, and 15.2.2's rcv_shift_leaves_the_head_alone is the
 * model saying the same thing about the transcription. *)

(* An array read as a function from index to cell.  The wrap lives in the index,
 * so the span below needs no modulo of its own: cell 64 of a 64-slot ring is a
 * caller's error, not a wrapped read, and folding the modulo in here would hide
 * that. *)
(* Three messages with three different types, small enough for the examples below
 * to compute on and distinct enough for a mask to tell them apart. *)
Definition hold_a : bmsg := mk_bmsg 1 4 100.
Definition hold_b : bmsg := mk_bmsg 2 4 200.
Definition hold_c : bmsg := mk_bmsg 3 4 300.

(* A well-formed mailbox holding exactly the queue given: the array is padded to
 * the ring's capacity with never-written cells, so ring_ok holds and the head is
 * the inert 0 of :35 -- the state space the C can actually reach. *)
Definition test_ring (q : list bmsg) : msg_ring :=
  mk_mring (q ++ repeat dead_cell (Nat.sub msg_ring_cap (length q)))
           0 (length q) (length q).

Definition slotfn : Set := nat -> bmsg.

(* The queue as a span: len cells starting at base, walked one cell at a time.
 * Because the walk carries the base along with it, both pop laws below are
 * reflexivity checks -- a pop either moves the base or moves the cells, and the
 * span reads the same indices either way. *)
Fixpoint span (cells : slotfn) (base len : nat) : list bmsg :=
  match len with
  | 0 => nil
  | S k => cells base :: span cells (S base) k
  end.

Lemma span_pops_at_either_end : forall cells base k,
    span cells base (S k) = cells base :: span cells (S base) k
    /\ tl (span cells base (S k)) = span cells (S base) k.
Proof. intros cells base k. repeat split; reflexivity. Qed.

(* The two designs, as functions on the same array. *)
Definition shift_pop (cells : slotfn) : slotfn := fun i => cells (S i).  (* :101 *)

Definition rotating_pop (base : nat) : nat := S base.                    (* :106 *)

(* Moving the cells and moving the base read the same span. *)
Lemma span_of_shift : forall cells k b,
    span (shift_pop cells) b k = span cells (S b) k.
Proof.
  intros cells. induction k as [|k IH]; intros b; cbn [span shift_pop].
  - reflexivity.
  - rewrite IH. reflexivity.
Qed.

Lemma a_shifting_body_pops_the_span : forall cells base len,
    0 < len ->
    tl (span cells base len) = span (shift_pop cells) base (Nat.pred len).
Proof.
  intros cells base len LT. destruct len as [|k]; [ lia | ].
  cbn [Nat.pred]. rewrite (span_of_shift cells k base).
  apply (proj2 (span_pops_at_either_end cells base k)).
Qed.

Lemma a_rotating_head_pops_the_span : forall cells base len,
    0 < len ->
    tl (span cells base len) = span cells (rotating_pop base) (Nat.pred len).
Proof.
  intros cells base len LT. destruct len as [|k]; [ lia | ].
  cbn [Nat.pred]. unfold rotating_pop.
  apply (proj2 (span_pops_at_either_end cells base k)).
Qed.

(* The refinement, stated as the API would see it: whatever the queue held, both
 * designs leave the same queue behind.  Nothing here is a change a caller can
 * observe. *)
Lemma the_two_pops_agree : forall cells base len,
    0 < len ->
    span (shift_pop cells) base (Nat.pred len)
    = span cells (rotating_pop base) (Nat.pred len).
Proof.
  intros cells base len LT.
  rewrite <- (a_rotating_head_pops_the_span cells base len LT).
  symmetry. apply (a_shifting_body_pops_the_span cells base len LT).
Qed.

(* The cost, which is the only thing that differs -- and it is the C's own loop
 * bound that says so: :101 runs while i is short of count-1, so every survivor is
 * assigned once and the last cell is left alone, which is the stale tail cell
 * 15.2.4's dequeue_leaves_one_stale_cell names. *)
Definition shift_writes (len : nat) : nat := Nat.pred len.    (* :100-104 *)
Definition rotating_writes (len : nat) : nat := 0.            (* :106 *)

Lemma a_shift_writes_one_cell_per_survivor : forall len,
    shift_writes (S len) = len /\ rotating_writes (S len) = 0.
Proof. intros len. repeat split; reflexivity. Qed.

Lemma a_rotating_head_never_costs_more : forall len,
    rotating_writes len <= shift_writes len.
Proof. intros len. unfold rotating_writes, shift_writes. apply Nat.le_0_l. Qed.

Example only_a_queue_of_one_or_fewer_costs_nothing_to_drain :
    shift_writes 1 = 0 /\ shift_writes 2 = 1 /\ shift_writes 64 = 63.
Proof. repeat split; reflexivity. Qed.

(* What the difference is worth at the capacity the ring actually has: a queue N
 * deep drained by N receives pays 0 + 1 + ... + (N-1) cell writes under the copy
 * design, and nothing under a rotating head. *)
Fixpoint writes_to_drain (len : nat) : nat :=
  match len with
  | 0 => 0
  | S k => shift_writes (S k) + writes_to_drain k
  end.

Lemma a_drain_of_one_more_costs_its_depth : forall len,
    writes_to_drain (S len) = len + writes_to_drain len.
Proof. intros len. reflexivity. Qed.

Example draining_a_full_ring_by_hand :
    writes_to_drain 1 = 0 /\ writes_to_drain 2 = 1 /\ writes_to_drain 64 = 2016
    /\ shift_writes 64 = 63 /\ rotating_writes 64 = 0.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* The bridge to 15.2 is checked by witness rather than by law: span over the
 * array function reads the same cells in the same order as win reads the prefix.
 * The laws above are about what a pop costs, which neither reading of the array
 * can change. *)
Example span_and_win_read_the_same_cells :
    span (fun i => nth i (hold_a :: hold_b :: hold_c :: nil) dead_cell) 0 2
    = win 2 (hold_a :: hold_b :: hold_c :: nil)
    /\ span (fun i => nth i (hold_a :: hold_b :: hold_c :: nil) dead_cell) 1 2
       = hold_b :: hold_c :: nil.
Proof. repeat split; reflexivity. Qed.

(* The scope of the claim, so it is not read as more than it is: shift_writes
 * counts the assignment in the loop at :101, which is the C's own unit of work.
 * It says nothing about cache lines, DMA, or whether the ring's memory is ever
 * fetched -- the model keeps no bytes to fetch. *)

(* ── 15.7.2 The body axis: the standard already leaves it to the caller ── *)

(* The cell a send writes is one cell (:60) whatever the message claims its body
 * is, and 15.1's observation -- the kernel reads msg_type and nothing else -- is
 * why a caller may put a segment number in the body and a byte count in msg_size
 * and move a whole segment for the price of one cell. *)
Lemma a_send_costs_one_cell_whatever_it_carries : forall m m' r,
    ring_ok r = true -> mr_count r < msg_ring_cap ->
    length (mqueue (snd_store m r)) = S (length (mqueue r))
    /\ length (mqueue (snd_store m' r)) = S (length (mqueue r)).
Proof.
  intros m m' r H OK. split;
    rewrite (snd_store_appends _ r H OK), length_app; cbn [length]; lia.
Qed.

(* The guard that refuses a send counts cells, never bytes: two sends of the same
 * size into two rings that differ by one cell go opposite ways. *)
Example a_full_ring_refuses_on_count_never_on_size :
    has_room (test_ring (repeat hold_a 64)) = false
    /\ has_room (test_ring (repeat hold_a 63)) = true.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* The T-Kernel byte buffer is the contrast: :133 debits HEADERSZ plus the ROUNDED
 * size, so its price is a function of the payload.  One ring charges by the
 * message, the other by the byte. *)
Example the_two_rings_price_a_kibibyte_differently :
    shift_writes 1 = 0 /\ mbf_charge 1024 = 1028.
Proof. repeat split; reflexivity. Qed.

(* The routing is blind to the body as well, which is what makes a descriptor
 * usable rather than merely cheap: a selective receive decides on the type bit
 * alone, so a message naming 4 KiB is matched and handed out exactly as a message
 * naming 4 bytes. *)
Lemma the_mask_reads_the_type_only : forall m1 m2 mask,
    bm_type m1 = bm_type m2 ->
    cell_accepts mask m1 = cell_accepts mask m2.
Proof.
  intros m1 m2 mask T. destruct m1 as [t1 s1 d1], m2 as [t2 s2 d2].
  cbn [bm_type] in T. unfold cell_accepts. cbn [bm_type]. rewrite T.
  reflexivity.
Qed.

Lemma a_kibibyte_and_a_word_cost_the_same_cell : forall r,
    ring_ok r = true -> mr_count r < msg_ring_cap ->
    length (mqueue (snd_store hold_a r))
    = length (mqueue (snd_store (mk_bmsg 1 4096 9) r)).
Proof.
  intros r H OK.
  rewrite (snd_store_appends hold_a r H OK), (snd_store_appends _ r H OK).
  rewrite !length_app. cbn [length]. lia.
Qed.

(* What this does not buy: the caller still stores its bytes into the struct it
 * hands to snd_msg, and the receiving task still reads them out of the struct the
 * kernel filled.  Those copies are real; they are just not this service's.  A
 * zero-copy claim that counts them is a claim about the application's memory, not
 * about BTRON 3.20's message queue. *)

(* ── 15.7.3 The envelope axis: what 3.20 mandates, and what it would take ── *)

(* NOT IMPLEMENTED.  Nothing in this tree provides the surface modelled below, and
 * 3.20 cannot: rcv_msg's second argument is a MESSAGE pointer the kernel writes
 * into (:97), so a conforming receive produces a copy of the envelope at the
 * caller's address by construction.  Removing that copy needs a call that returns
 * a POSITION rather than a value; that needs a second call to give the position
 * back; and that needs receipts for the states only the pair can reach.  Three
 * additions, none of them in the standard -- which is why this subsection takes
 * its own names instead of overloading the 3.20 ones. *)

(* The receipts.  The first five are 15.4's flat figures (error.h:15, :21, :23,
 * :32, :27) and mean what they mean there.  E_BUSY (:31) and E_OBJ (:26) are also
 * shipped figures, ones no line of ipc_msg.c can produce, so borrowing them costs
 * nothing.  A stale borrow has no shipped figure at all: -70 sits one below the
 * last code in error.h's standard block, and it is a proposal, not a citation. *)
Inductive zc_ber : Type :=
  | ZBE_OK
  | ZBE_PAR
  | ZBE_ID
  | ZBE_NOSPC
  | ZBE_TMOUT
  | ZBE_BUSY
  | ZBE_OBJ
  | ZBE_STALE.

Definition zc_ber_code (b : zc_ber) : Z :=
  match b with
  | ZBE_OK    => Z0
  | ZBE_PAR   => Z.opp 33
  | ZBE_ID    => Z.opp 35
  | ZBE_NOSPC => Z.opp 11
  | ZBE_TMOUT => Z.opp 69
  | ZBE_BUSY  => Z.opp 65
  | ZBE_OBJ   => Z.opp 41
  | ZBE_STALE => Z.opp 70
  end.

Lemma the_zc_figures_extend_the_shipped_ones :
    zc_ber_code ZBE_OK = ber_code BE_OK
    /\ zc_ber_code ZBE_PAR = ber_code BE_PAR
    /\ zc_ber_code ZBE_ID = ber_code BE_ID
    /\ zc_ber_code ZBE_NOSPC = ber_code BE_NOSPC
    /\ zc_ber_code ZBE_TMOUT = ber_code BE_TMOUT.
Proof. repeat split; reflexivity. Qed.

Lemma zc_ber_code_separates : forall b1 b2, zc_ber_code b1 = zc_ber_code b2 -> b1 = b2.
Proof.
  intros b1 b2 H. destruct b1, b2; try reflexivity.
  all: (vm_compute in H; discriminate H).
Qed.

(* Below the most negative figure error.h's standard block ships, and one below
 * the alias ER_TIMEOUT, so the new code cannot be mistaken for an old one. *)
Lemma a_stale_borrow_is_outside_the_shipped_range :
    Z.ltb (zc_ber_code ZBE_STALE) (Z.opp 69) = true
    /\ Z.leb (Z.opp 2147483648) (zc_ber_code ZBE_STALE) = true.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* The state: a mailbox, plus the one index the receiving task is holding.  One
 * borrow at a time is a design rule rather than an optimisation -- the second
 * example below is what a second concurrent borrow costs. *)
Record zc_box : Set := mk_zbox {
    zb_ring : msg_ring;
    zb_held : option nat            (* the borrowed cell, if any *)
  }.

(* zc_rcv_msg(pid, mask, TMO_POL): match, hand out the index, remove nothing.  The
 * polling shape alone is modelled; a blocking borrow would park on the queue
 * 15.3.2 already describes, and the borrow adds nothing to that story. *)
Definition zc_rcv (mask : nat) (b : zc_box) : zc_ber * zc_box :=
  match zb_held b with
  | Some _ => (ZBE_BUSY, b)
  | None => match rcv_match mask (zb_ring b) with
            | Some i => (ZBE_OK, mk_zbox (zb_ring b) (Some i))
            | None => (ZBE_TMOUT, b)
            end
  end.

(* zc_rel_msg(pid): the removal, deferred to the moment the caller is done with the
 * envelope.  It IS the C's dequeue -- rcv_shift, :100-106 -- so the copy this
 * design takes out of the receive reappears, once, at the release.  Taking 15.7.1
 * first is what stops it reappearing at all. *)
Definition zc_rel (b : zc_box) : zc_ber * zc_box :=
  match zb_held b with
  | None => (ZBE_OBJ, b)
  | Some i => (ZBE_OK, mk_zbox (rcv_shift i (zb_ring b)) None)
  end.

(* The borrow itself writes no cell: the ring field comes back identical in all
 * three branches. *)
Lemma a_borrow_writes_no_cell : forall mask b,
    zb_ring (snd (zc_rcv mask b)) = zb_ring b.
Proof.
  intros mask b. unfold zc_rcv. destruct (zb_held b) as [j|].
  - reflexivity.
  - destruct (rcv_match mask (zb_ring b)); reflexivity.
Qed.

Lemma a_release_with_nothing_held_moves_nothing : forall b,
    zb_held b = None -> zc_rel b = (ZBE_OBJ, b).
Proof. intros b H. unfold zc_rel. rewrite H. reflexivity. Qed.

Lemma a_second_borrow_while_held_changes_nothing : forall mask b j,
    zb_held b = Some j -> zc_rcv mask b = (ZBE_BUSY, b).
Proof. intros mask b j H. unfold zc_rcv. rewrite H. reflexivity. Qed.

Lemma the_release_of_a_held_cell_is_the_dequeue : forall r i,
    zc_rel (mk_zbox r (Some i)) = (ZBE_OK, mk_zbox (rcv_shift i r) None).
Proof. intros r i. reflexivity. Qed.

(* Why the names cannot be overloaded onto the 3.20 surface.  With a borrow in
 * hand the message is still queued, so the same scan finds it again -- and the
 * count says nothing was taken, while 15.3.1's a_hit_returns_a_queued_cell says a
 * hit hands out a queued cell.  A caller of rcv_msg can never see a cell twice; a
 * caller of zc_rcv_msg sees one until it gives it back.  That is a different
 * contract, not a faster one. *)
Definition three_deep : msg_ring := test_ring (hold_a :: hold_b :: hold_c :: nil).

Definition box0 : zc_box := mk_zbox three_deep None.
Definition box1 : zc_box := snd (zc_rcv (msgmask 2) box0).

Example a_borrowed_cell_is_still_there_to_be_found :
    ring_ok three_deep = true
    /\ fst (zc_rcv (msgmask 2) box0) = ZBE_OK
    /\ mr_count (zb_ring box1) = 3
    /\ rcv_match (msgmask 2) (zb_ring box1) = Some 1
    /\ fst (zc_rcv (msgmask 2) box1) = ZBE_BUSY.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* And the hazard the one-borrow rule is for.  A borrow names a cell by INDEX; a
 * receive whose match lies EARLIER in the queue shifts every later cell one slot
 * towards the head -- that is what :100-104 does -- so the index the borrower was
 * handed stops naming the borrowed message.  Three messages is enough: borrow the
 * middle one at index 1, let another receive take the front one, and index 1 now
 * names the THIRD message.  A release by index then deletes a message nobody has
 * read. *)
Example a_borrowed_index_is_not_a_stable_name :
    rcv_match (msgmask 2) three_deep = Some 1
    /\ mqueue (rcv_shift 0 three_deep) = hold_b :: hold_c :: nil
    /\ nth 1 (mqueue (rcv_shift 0 three_deep)) dead_cell = hold_c
    /\ rcv_match (msgmask 2) (rcv_shift 0 three_deep) = Some 0.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* Two ways out, and the model says which one it takes: a cookie the release CHECKS
 * rather than uses turns the stale case into a receipt instead of a silent
 * deletion, and 15.7.1's rotating head makes removals at the head leave every
 * index alone -- which covers the common case and, since the mask can match in
 * the middle, does not cover all of it. *)
Definition zc_rel_at (b : zc_box) (i : nat) : zc_ber * zc_box :=
  match zb_held b with
  | None => (ZBE_OBJ, b)
  | Some j => if Nat.eqb i j
              then (ZBE_OK, mk_zbox (rcv_shift j (zb_ring b)) None)
              else (ZBE_STALE, b)
  end.

Lemma a_stale_release_writes_nothing : forall b i j,
    zb_held b = Some j -> Nat.eqb i j = false -> zc_rel_at b i = (ZBE_STALE, b).
Proof. intros b i j H N. unfold zc_rel_at. rewrite H, N. reflexivity. Qed.

Lemma a_current_release_is_the_dequeue : forall b i j,
    zb_held b = Some j -> Nat.eqb i j = true ->
    zc_rel_at b i = (ZBE_OK, mk_zbox (rcv_shift j (zb_ring b)) None).
Proof. intros b i j H E. unfold zc_rel_at. rewrite H, E. reflexivity. Qed.

Lemma a_release_of_nothing_held_is_e_obj : forall b,
    zb_held b = None -> zc_rel_at b 0 = (ZBE_OBJ, b).
Proof. intros b H. unfold zc_rel_at. rewrite H. reflexivity. Qed.

Example the_three_release_receipts :
    zc_rel_at (mk_zbox (test_ring (hold_a :: nil)) None) 0 = (ZBE_OBJ, mk_zbox (test_ring (hold_a :: nil)) None)
    /\ fst (zc_rel_at (mk_zbox (test_ring (hold_a :: hold_b :: nil)) (Some 1)) 1) = ZBE_OK
    /\ fst (zc_rel_at (mk_zbox (test_ring (hold_a :: hold_b :: nil)) (Some 1)) 0) = ZBE_STALE.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* ── 15.7.4 The copy budget, as this file can claim it ─────────────── *)

(* Against the three axes: the shipped tree, 15.7.1's fix, and 15.7.3's proposal.
 * Each figure is a definition or a lemma above, not an estimate.

     axis      shipped             with 15.7.1        with 15.7.3
     body      0 kernel copies     0                  0
     envelope  1 in, 1 out         1 in, 1 out        0, with a borrow and a release
     queue     count minus 1       0                  0, at the release

 * So the honest sentence is this.  The payload path is zero-copy already, because
 * a cell costs one write at any declared size and the kernel never reads it.  The
 * dequeue is zero-copy after 15.7.1 -- inside the standard, invisible to the API,
 * and the design messagebuf.c has always used.  The envelope is copied once each
 * way in, and that last copy is the one BTRON 3.20 mandates. *)
Lemma the_three_axes_in_figures :
    shift_writes 64 = 63
    /\ rotating_writes 64 = 0
    /\ zc_ber_code ZBE_STALE = Z.opp 70
    /\ mbf_charge 1024 = 1028.
Proof. repeat split; reflexivity. Qed.


(* ── 16. Event flags: one word, many tests ─────────────────────── *)

(* §14's semaphore and this section's event flag are the two objects in the
 * family whose state is a single number, and they are the family's best
 * controls on each other.  A semaphore holds a count and every waiter asks the
 * same question of it, "is at least my number still available"; an event flag
 * holds a PATTERN and every waiter carries its OWN test, "have the bits I named
 * all arrived" or "has any one of them".  That single difference generates
 * everything below.  It is why the kernel's release loop (eventflag.c:224-226)
 * must write each released caller's answer OUT OF THE CELL as it walks, why a
 * waiter can be starved by the waiter in front of it (eventflag.c:230-237) in
 * a way no semaphore can do, and why the same attribute that lets several
 * tasks wait -- TA_WMUL -- is also the only attribute under which order
 * matters at all.  The sources are eventflag.c in full plus wait.c and
 * wait.h for the shared engine, whose two-phase write §12.4 already settled
 * and which this section reuses unchanged. *)

(* ── 16.1 The pattern word ────────────────────────────────────── *)

(* FLGCB.flgptn is a UINT and every operation on it is bitwise.  Two
 * combinations do all the work in this file: the union a set performs
 * (eventflag.c:211 "flgcb->flgptn |= setptn") and the difference a
 * bit-oriented clear performs (eventflag.c:229 "flgcb->flgptn &= ~waiptn").
 * The difference is spelled here with Nat.ldiff, whose specification
 * (Nat.ldiff_spec) says its test bit at position i is "a!!i && negb b!!i" --
 * which is exactly the C's mask-then-AND, without needing a complement of the
 * whole word. *)
Definition pat_or (a b : nat) : nat := Nat.lor a b.
Definition pat_and (a b : nat) : nat := Nat.land a b.
Definition pat_clr (p w : nat) : nat := Nat.ldiff p w.

(* A UINT is thirty-two bits wide.  The model does not materialise 2^32 as a
 * unary numeral -- that would be four billion constructors in every
 * computation -- so the bound is stated as a proposition about test bits,
 * which is what the kernel actually relies on: nothing above bit 31 can be
 * set, because the object's own word has no room for it. *)
Definition in_word (p : nat) : Prop :=
  forall i : nat, 32 <= i -> Nat.testbit p i = false.

(* The one library tool this section leans on repeatedly: two naturals with the
 * same test bits at every position are equal.  It converts a law about
 * operations into a law about bits, which is where bitwise reasoning happens. *)
Lemma bit_ext : forall a b,
    (forall i : nat, Nat.testbit a i = Nat.testbit b i) -> a = b.
Proof.
  intros a b H. apply Nat.bits_inj. unfold Nat.eqf. exact H.
Qed.

(* Clearing removes exactly the bits named and keeps the rest.  Both halves of
 * the sentence are used below: the first says a bit-clear really is a clear,
 * the second says it is nothing else. *)
Lemma cleared_bits_are_gone : forall p w,
    Nat.land (pat_clr p w) w = 0.
Proof.
  intros p w. unfold pat_clr. apply Nat.land_ldiff.
Qed.

Lemma a_clear_never_grows_the_pattern : forall p w,
    Nat.leb (pat_clr p w) p = true.
Proof.
  intros p w. apply Nat.leb_le. apply Nat.ldiff_le_l.
Qed.

Lemma clearing_nothing_changes_nothing : forall p,
    pat_clr p 0 = p.
Proof.
  intros p. unfold pat_clr. apply Nat.ldiff_0_r.
Qed.

Lemma an_empty_pattern_clears_to_zero : forall w,
    pat_clr 0 w = 0.
Proof.
  intros w. unfold pat_clr. apply Nat.ldiff_0_l.
Qed.

Lemma setting_bits_is_idempotent : forall p,
    pat_or p p = p.
Proof.
  intros p. unfold pat_or. apply Nat.lor_diag.
Qed.

(* Every legal pattern is in-word, and both operations preserve it.  These three
 * are the shape of the standing invariant §16.4 will state as a boolean. *)
Lemma pat_or_stays_in_word : forall p q, in_word p -> in_word q -> in_word (pat_or p q).
Proof.
  intros p q P Q i N. unfold pat_or. rewrite Nat.lor_spec.
  rewrite (P i N), (Q i N). destruct (Nat.testbit p i), (Nat.testbit q i); reflexivity.
Qed.

Lemma pat_and_stays_in_word : forall p q, in_word p -> in_word (pat_and p q).
Proof.
  intros p q P i N. unfold pat_and. rewrite Nat.land_spec.
  rewrite (P i N). destruct (Nat.testbit p i); reflexivity.
Qed.

Lemma pat_clr_stays_in_word : forall p w, in_word p -> in_word (pat_clr p w).
Proof.
  intros p w P i N. unfold pat_clr. rewrite Nat.ldiff_spec.
  rewrite (P i N). reflexivity.
Qed.

(* ── 16.2 The two wait modes ──────────────────────────────────── *)

(* syscall.h:123-125.  TWF_ORW is the wait MODE (any-of versus all-of), the
 * other two are what to do with the pattern once the wait succeeds, and the C
 * applies them in that order: ORW decides the test, then BITCLR, then CLR. *)
Definition twf_orw : nat := 1.
Definition twf_clr : nat := 16.
Definition twf_bitclr : nat := 32.

(* The C's test is the bit at eventflag.c:87, "(wfmode & TWF_ORW) != 0", and
 * the model writes it as an equality against the bit's own value below.  The
 * two forms could disagree on a word whose ORW bit is set alongside something
 * else -- wfmode 17 is ORW|CLR, which is exactly the combination the reference
 * names -- and they do not, because TWF_ORW is 1: a land with 1 is either 0 or
 * 1.  orw_mode_is_the_bit below states that equivalence outright, so no mode
 * word needs a hypothesis to be read the same way here and in the C, and
 * §16.9 computes the ORW|CLR case rather than leaving it as a comment. *)
Definition orw_mode (m : nat) : bool := Nat.eqb (Nat.land m twf_orw) twf_orw.
Definition bitclr_mode (m : nat) : bool := Nat.eqb (Nat.land m twf_bitclr) twf_bitclr.
Definition clr_mode (m : nat) : bool := Nat.eqb (Nat.land m twf_clr) twf_clr.

Lemma orw_mode_is_a_bit_at_most : forall m,
    Nat.land m twf_orw <= twf_orw.
Proof.
  intros m. unfold twf_orw. apply Nat.land_le_r.
Qed.

Lemma orw_mode_is_the_bit : forall m,
    orw_mode m = negb (Nat.eqb (Nat.land m twf_orw) 0).
Proof.
  intros m. unfold orw_mode, twf_orw.
  assert (L : Nat.land m 1 <= 1) by (apply Nat.land_le_r).
  destruct (Nat.land m 1); cbn [Nat.eqb negb].
  - reflexivity.
  - assert (L' : S n <= 1) by lia. destruct n; [reflexivity | lia].
Qed.

(* CHECK_PAR((wfmode & ~(TWF_ORW | TWF_CLR | TWF_BITCLR)) == 0) at
 * eventflag.c:285, inside the CHK_PAR the build turns on.  The C's complement
 * runs over the whole UINT.  The model takes it against 127, the first
 * all-ones word past the highest legal bit, so the refused set is exactly the
 * spacings between and above the three named bits; the computed figure below
 * is what that leaves.  A stray bit at 128 or above is outside the tested
 * range -- the C would refuse it and this model does not, which is the price
 * of not materialising 2^32 as a unary numeral (see in_word above).  No
 * caller in the tree passes one, and the oracle's own parameter check can. *)
Definition wfmode_mask : nat :=
  Nat.ldiff (Nat.pred (Nat.pow 2 7)) (Nat.lor twf_orw (Nat.lor twf_clr twf_bitclr)).

Lemma wfmode_mask_computes : wfmode_mask = 78.
Proof. unfold wfmode_mask. vm_compute. reflexivity. Qed.

Definition wfmode_ok (m : nat) : bool := Nat.eqb (Nat.land m wfmode_mask) 0.

Lemma the_three_modes_pass :
    wfmode_ok 0 = true /\ wfmode_ok twf_orw = true
    /\ wfmode_ok twf_clr = true /\ wfmode_ok twf_bitclr = true
    /\ wfmode_ok (pat_or twf_orw twf_clr) = true
    /\ wfmode_ok (pat_or twf_bitclr twf_clr) = true
    /\ wfmode_ok (pat_or twf_orw (pat_or twf_bitclr twf_clr)) = true.
Proof. repeat split; vm_compute; reflexivity. Qed.

Lemma a_stray_bit_is_refused :
    wfmode_ok 2 = false /\ wfmode_ok 8 = false /\ wfmode_ok 64 = false.
Proof. repeat split; vm_compute; reflexivity. Qed.

Lemma a_bit_above_the_range_is_not : wfmode_ok 128 = true.
Proof. unfold wfmode_ok. vm_compute. reflexivity. Qed.

(* eventflag_cond, eventflag.c:86-93, in the C order: an ORW waiter is
 * satisfied by ANY named bit, an all-of waiter by ALL named bits. *)
Definition flg_cond (p w m : nat) : bool :=
  if orw_mode m then Nat.ltb 0 (pat_and p w)
  else Nat.eqb (pat_and p w) w.

Lemma and_mode_needs_every_bit : forall p w m,
    orw_mode m = false ->
    flg_cond p w m = true <-> Nat.land p w = w.
Proof.
  intros p w m M. unfold flg_cond. rewrite M. apply Nat.eqb_eq.
Qed.

(* The forward half on its own, in the form the later sections rewrite with:
 * an all-of waiter that has been answered has every bit it asked for. *)
Lemma and_mode_satisfied : forall p w m,
    orw_mode m = false -> flg_cond p w m = true -> Nat.land p w = w.
Proof.
  intros p w m M H. unfold flg_cond in H. rewrite M in H.
  rewrite Nat.eqb_eq in H. exact H.
Qed.

Lemma or_mode_needs_one_bit : forall p w m,
    orw_mode m = true ->
    flg_cond p w m = true <-> 0 < Nat.land p w.
Proof.
  intros p w m M. unfold flg_cond. rewrite M.
  split.
  - intros H. apply Nat.ltb_lt. exact H.
  - intros H. apply Nat.ltb_lt. exact H.
Qed.

(* The first thing the test does not look at is the caller's own parameter.
 * CHECK_PAR(waiptn != 0) at eventflag.c:284 is not a tidiness rule: with an
 * empty pattern, the all-of test is satisfied by EVERY pattern, so a wai_flg
 * that named no bits would return immediately and behave as a read of the
 * flag through the ORW branch -- which is precisely the reading the
 * reference call at :339 is for. *)
Lemma an_empty_test_is_always_ready : forall p m,
    orw_mode m = false -> flg_cond p 0 m = true.
Proof.
  intros p m M. unfold flg_cond. rewrite M.
  unfold pat_and. rewrite Nat.land_0_r. reflexivity.
Qed.

(* And it does the same for the OR branch, so the guard is not a quirk of one
 * mode. *)
(* The library's bitwise operations compute when BOTH arguments are numerals,
 * and never otherwise -- Nat.land m 1 with m a variable is stuck.  The three
 * concrete figures this section needs are therefore recorded once here. *)
Lemma land_1_1 : Nat.land 1 1 = 1. Proof. reflexivity. Qed.

(* Adding TWF_ORW to a wfmode always turns it into an OR-mode, by whichever
 * reading of the C's test one takes. *)
Lemma oring_in_orw_sets_the_bit : forall m,
    orw_mode (pat_or m twf_orw) = true.
Proof.
  intros m. unfold orw_mode, pat_or, twf_orw.
  assert (L : Nat.land m 1 <= 1) by (apply Nat.land_le_r).
  rewrite (Nat.land_lor_distr_l m 1 1), land_1_1.
  destruct (Nat.land m 1); [reflexivity |].
  replace (S n) with 1 by lia. rewrite Nat.lor_diag. apply Nat.eqb_refl.
Qed.

(* And the two modes part company on an empty test: the any-of reading is
 * NEVER satisfied by it.  A caller that passed waiptn = 0 with TWF_ORW would
 * therefore not return at once but block forever -- a different bad behaviour
 * from the silent read above, and the same guard at :284 is what prevents
 * it. *)
Lemma an_empty_test_never_readies_an_or_waiter : forall p m,
    flg_cond p 0 (pat_or m twf_orw) = false.
Proof.
  intros p m. unfold flg_cond. rewrite (oring_in_orw_sets_the_bit m).
  unfold pat_and. rewrite Nat.land_0_r. cbn [Nat.ltb]. reflexivity.
Qed.

(* An all-of waiter is automatically an any-of waiter, provided it named
 * something.  This is the one direction the two modes agree, and the reason a
 * mixed queue cannot deadlock on the MODE alone. *)
Lemma and_mode_readies_or_mode : forall p w m,
    orw_mode m = false -> 0 < w -> flg_cond p w m = true ->
    flg_cond p w (pat_or m twf_orw) = true.
Proof.
  intros p w m M N H.
  assert (A : Nat.land p w = w) by (apply (and_mode_satisfied p w m M); exact H).
  unfold flg_cond. rewrite (oring_in_orw_sets_the_bit m).
  apply Nat.ltb_lt. unfold pat_and. rewrite A. lia.
Qed.

(* The monotone behaviour a kernel designer would expect, in the direction the
 * library actually supports: a union on the TEST side preserves both readings,
 * and a union on the PATTERN side preserves the any-of reading outright. *)

(* Nat.lor has no order lemma in this library -- no monotone, no absorption --
 * so "a union keeps a figure positive" goes through the single zero-test it
 * does provide, Nat.lor_eq_0_l.  Both monotonicity facts below reuse it. *)
Lemma a_union_keeps_a_nonzero : forall a b, 0 < a -> 0 < Nat.lor a b.
Proof.
  intros a b H. apply Nat.neq_0_lt_0.
  intro Heq. apply Nat.lor_eq_0_l in Heq. lia.
Qed.

Lemma a_set_never_unreadies_an_or_waiter : forall p s w m,
    orw_mode m = true ->
    flg_cond p w m = true -> flg_cond (pat_or p s) w m = true.
Proof.
  intros p s w m M H. unfold flg_cond in *. rewrite M in H. rewrite M.
  unfold pat_and, pat_or in *.
  apply Nat.ltb_lt. apply Nat.ltb_lt in H.
  assert (D : Nat.land (Nat.lor p s) w = Nat.lor (Nat.land p w) (Nat.land s w)).
  { apply Nat.land_lor_distr_l. }
  rewrite D. apply a_union_keeps_a_nonzero. exact H.
Qed.

Lemma an_or_of_the_tests_readies_an_or_waiter : forall p w x m,
    orw_mode m = true ->
    flg_cond p w m = true -> flg_cond p (pat_or w x) m = true.
Proof.
  intros p w x m M H. unfold flg_cond in *. rewrite M in H. rewrite M.
  unfold pat_and, pat_or in *.
  apply Nat.ltb_lt. apply Nat.ltb_lt in H.
  assert (D : Nat.land p (Nat.lor w x) = Nat.lor (Nat.land p w) (Nat.land p x)).
  { apply Nat.land_lor_distr_r. }
  rewrite D. apply a_union_keeps_a_nonzero. exact H.
Qed.

(* The AND-mode twin goes the other way, and that asymmetry is the whole
 * difference between the two readings: naming MORE bits can strand an
 * all-of waiter, naming FEWER can never strand it.  Narrowing is intersection
 * with an arbitrary figure, and intersection is associative. *)
Lemma an_and_of_the_tests_readies_an_and_waiter : forall p w x m,
    orw_mode m = false ->
    flg_cond p w m = true -> flg_cond p (pat_and w x) m = true.
Proof.
  intros p w x m M H.
  assert (A : Nat.land p w = w) by (apply (and_mode_satisfied p w m M); exact H).
  unfold flg_cond. rewrite M. unfold pat_and.
  assert (L : Nat.land p (Nat.land w x) = Nat.land (Nat.land p w) x)
    by (apply Nat.land_assoc).
  rewrite L, A. apply Nat.eqb_refl.
Qed.

(* What this section deliberately does NOT claim.  The complements of the two
 * laws above -- "a set never un-readies an ALL-OF waiter", i.e. land p w = w
 * implies land (p | s) w = w, and "widening a test never un-readies an all-of
 * waiter" -- are true of the bits, and the C relies on the first of them
 * silently.  But they are exactly the bitwise absorption laws, and neither
 * this file's library nor any rewrite chain available here proves them: Nat
 * has no land_lor_absorb, and the extensionality principle of §16.1 cannot
 * turn the bit-level argument into a numeral equality without it.  The walk
 * laws in §16.7 are therefore stated about the released waiter's OWN answer,
 * which needs no absorption, and never about what a set does to a waiter still
 * on the queue. *)

(* Intersecting never grows a figure -- p & s <= p -- so an all-of test is
 * never easier to satisfy than the pattern it is measured against. *)
Lemma an_intersect_never_grows_the_pattern : forall p s, pat_and p s <= p.
Proof.
  intros p s. unfold pat_and. apply Nat.land_le_l.
Qed.

(* clr_flg, eventflag.c:263, is "flgcb->flgptn &= clrptn" -- the argument is a
 * KEEP-mask, not a drop-mask.  The repository's own specification text says
 * the opposite (b-spec/os_spec/kernel/taskcomm.html:341-368 describes clrptn
 * as an AND with the INVERTED mask).  The two readings disagree on concrete
 * figures, so this is a real divergence and not a translation nicety; the
 * model follows the code. *)
Example the_clr_flg_divergence :
    Nat.land 11 3 = 3 /\ Nat.ldiff 11 3 = 8.
Proof. split; vm_compute; reflexivity. Qed.

(* ── 16.3 The waiter, and the two orders ──────────────────────── *)

(* eventflag.c:324-326 writes three fields of tcb->winfo.flg -- waiptn, wfmode
 * and p_flgptn -- and the queue itself is ordered by tcb->priority when the
 * object carries TA_TPRI (:321 selects wspec_flg_tpri, whose chg_pri hook is
 * flg_chg_pri at :98-104).  The pointer p_flgptn is not a natural: §16.6
 * records its effect as a separate answer field instead of modelling C
 * addresses. *)
Record flg_who : Type := mk_flg_who {
    fw_tid    : nat;          (* TCB.tskid *)
    fw_waiptn : nat;          (* TCB.winfo.flg.waiptn *)
    fw_wfmode : nat;          (* TCB.winfo.flg.wfmode *)
    fw_pri    : nat           (* TCB.priority, the TPRI key *)
  }.

(* queue_insert_tpri (wait.c:79-97) again, read for this record type: walk from
 * the head, stop at the first entry whose priority figure is not better, so a
 * tie leaves the task that was already waiting in front. *)
Fixpoint flg_insert_tpri (who : flg_who) (q : list flg_who) : list flg_who :=
  match q with
  | nil => who :: nil
  | x :: rest => if Nat.ltb (fw_pri who) (fw_pri x)
                 then who :: q
                 else x :: flg_insert_tpri who rest
  end.

Lemma flg_insert_puts_the_better_task_first : forall who x q,
    Nat.ltb (fw_pri who) (fw_pri x) = true ->
    flg_insert_tpri who (x :: q) = who :: x :: q.
Proof. intros who x q P. unfold flg_insert_tpri. rewrite P. reflexivity. Qed.

Lemma flg_insert_tie_defers : forall who x q,
    Nat.ltb (fw_pri who) (fw_pri x) = false ->
    flg_insert_tpri who (x :: q) = x :: flg_insert_tpri who q.
Proof. intros who x q P. unfold flg_insert_tpri. rewrite P. reflexivity. Qed.

Lemma flg_insert_adds_exactly_one : forall who q,
    length (flg_insert_tpri who q) = S (length q).
Proof.
  intros who q. revert who.
  induction q as [|x rest IH]; intros who.
  - cbn [flg_insert_tpri length]. reflexivity.
  - destruct (Nat.ltb (fw_pri who) (fw_pri x)) eqn:P.
    + rewrite (flg_insert_puts_the_better_task_first who x rest P).
      cbn [length]. reflexivity.
    + rewrite (flg_insert_tie_defers who x rest P).
      cbn [length]. rewrite (IH who). reflexivity.
Qed.

Fixpoint flg_ascending_tail (prev : flg_who) (q : list flg_who) : bool :=
  match q with
  | nil => true
  | x :: rest => andb (Nat.leb (fw_pri prev) (fw_pri x)) (flg_ascending_tail x rest)
  end.

Definition flg_ascending (q : list flg_who) : bool :=
  match q with
  | nil => true
  | x :: rest => flg_ascending_tail x rest
  end.

(* gcb_make_wait (wait.c:152-161) chooses between the two by TA_TPRI. *)
Definition flg_enqueue (tpri : bool) (who : flg_who) (q : list flg_who) : list flg_who :=
  if tpri then flg_insert_tpri who q else q ++ [who].

Lemma flg_enqueue_adds_exactly_one : forall tpri who q,
    length (flg_enqueue tpri who q) = S (length q).
Proof.
  intros tpri who q. destruct tpri.
  - apply flg_insert_adds_exactly_one.
  - cbn [flg_enqueue]. rewrite length_app. cbn [length]. lia.
Qed.

Lemma flg_enqueue_keeps_the_queue_ascending : forall q who,
    flg_ascending q = true -> flg_ascending (flg_enqueue true who q) = true.
Proof.
  unfold flg_enqueue. intros q who H. revert who H.
  induction q as [|x rest IH]; intros who H.
  - cbn [flg_insert_tpri flg_ascending]. reflexivity.
  - destruct rest as [|y rest'] eqn:Er; subst rest.
    + destruct (Nat.ltb (fw_pri who) (fw_pri x)) eqn:P.
      * rewrite (flg_insert_puts_the_better_task_first who x nil P).
        cbn [flg_ascending flg_ascending_tail].
        assert (B : Nat.leb (fw_pri who) (fw_pri x) = true).
        { apply Nat.leb_le. apply Nat.lt_le_incl. apply Nat.ltb_lt. exact P. }
        rewrite B. reflexivity.
      * rewrite (flg_insert_tie_defers who x nil P).
        cbn [flg_insert_tpri flg_ascending flg_ascending_tail].
        assert (B : Nat.leb (fw_pri x) (fw_pri who) = true).
        { apply Nat.leb_le. apply ltb_false_ge. exact P. }
        rewrite B. reflexivity.
    + cbn [flg_ascending flg_ascending_tail] in H. apply andb_true_iff in H.
      destruct H as [A C].
      destruct (Nat.ltb (fw_pri who) (fw_pri x)) eqn:P.
      * rewrite (flg_insert_puts_the_better_task_first who x (y :: rest') P).
        cbn [flg_ascending flg_ascending_tail]. rewrite A, C.
        assert (B : Nat.leb (fw_pri who) (fw_pri x) = true).
        { apply Nat.leb_le. apply Nat.lt_le_incl. apply Nat.ltb_lt. exact P. }
        rewrite B. reflexivity.
      * rewrite (flg_insert_tie_defers who x (y :: rest') P).
        destruct (Nat.ltb (fw_pri who) (fw_pri y)) eqn:Q.
        { rewrite (flg_insert_puts_the_better_task_first who y rest' Q).
          cbn [flg_ascending flg_ascending_tail].
          assert (B1 : Nat.leb (fw_pri x) (fw_pri who) = true).
          { apply Nat.leb_le. apply ltb_false_ge. exact P. }
          assert (B2 : Nat.leb (fw_pri who) (fw_pri y) = true).
          { apply Nat.leb_le. apply Nat.lt_le_incl. apply Nat.ltb_lt. exact Q. }
          rewrite B1, B2, C. reflexivity. }
        { specialize (IH who C).
          rewrite (flg_insert_tie_defers who y rest' Q) in IH.
          rewrite (flg_insert_tie_defers who y rest' Q).
          cbn [flg_ascending flg_ascending_tail] in IH.
          cbn [flg_ascending flg_ascending_tail].
          apply andb_true_iff. split; [ exact A | exact IH ]. }
Qed.

(* ── 16.4 The control block ───────────────────────────────────── *)

(* FLGCB, eventflag.c:31-44, projected onto what an API call can observe: the
 * stored marker, the three attributes the entry points read, the pattern, and
 * the queue.  The exinf and dsname fields are the same decoration §14 left out
 * of semcb. *)
Record flgcb : Type := mk_flgcb {
    fc_id    : nat;             (* FLGCB.flgid -- stored marker, 0 = free cell *)
    fc_tpri  : bool;            (* TA_TPRI: the queue is ordered by priority *)
    fc_wmul  : bool;            (* TA_WMUL: more than one task may wait *)
    fc_nodis : bool;            (* TA_NODISWAI: this object never disables waits *)
    fc_pat   : nat;             (* FLGCB.flgptn *)
    fc_wait  : list flg_who     (* FLGCB.wait_queue, head waiter first *)
  }.

Definition free_flgcb : flgcb := mk_flgcb 0 false false false 0 nil.

Definition flg_used (c : flgcb) : bool := negb (Nat.eqb (fc_id c) 0).
Definition flg_live (c : flgcb) : bool := flg_used c.

(* The two shape properties the C maintains.  The pattern is a UINT, so every
 * queued waiptn is in-word as well, and every queued wfmode passed the
 * CHECK_PAR at :285.  Like §14.3's every_needs, these are STANDING properties
 * of the cell rather than hypotheses about one call, because the guard that
 * establishes them runs before the queue is touched. *)
Definition every_test_nonzero (q : list flg_who) : bool :=
  forallb (fun x => negb (Nat.eqb (fw_waiptn x) 0)) q.

Definition every_mode_legal (q : list flg_who) : bool :=
  forallb (fun x => wfmode_ok (fw_wfmode x)) q.

Definition flg_wf (c : flgcb) : bool :=
  andb (every_test_nonzero (fc_wait c)) (every_mode_legal (fc_wait c)).

(* Reading the two halves off a cell in the form the later lemmas rewrite with,
 * which is §14.3's sem_wf_of_a_cell idiom: no projection of a constructor is
 * left for a tactic to relate. *)
Lemma flg_wf_of_a_cell : forall i tp wm nd p q,
    flg_wf (mk_flgcb i tp wm nd p q) = andb (every_test_nonzero q) (every_mode_legal q).
Proof. intros i tp wm nd p q. reflexivity. Qed.

Lemma a_fresh_flag_cell_is_wellformed : flg_wf free_flgcb = true.
Proof. reflexivity. Qed.

Lemma flg_wf_gives_positive_tests : forall c,
    flg_wf c = true -> every_test_nonzero (fc_wait c) = true.
Proof.
  intros c W. unfold flg_wf in W. apply andb_true_iff in W.
  destruct W as [E _]. exact E.
Qed.

Lemma flg_wf_gives_legal_modes : forall c,
    flg_wf c = true -> every_mode_legal (fc_wait c) = true.
Proof.
  intros c W. unfold flg_wf in W. apply andb_true_iff in W.
  destruct W as [_ E]. exact E.
Qed.

(* The per-waiter reading the walk needs is about the HEAD of the queue, not
 * about an arbitrary member: memb (§10) is keyed on task ids, and a flg_who is
 * not one, so the two cons laws below give the reading directly. *)
Lemma every_test_nonzero_cons : forall x q,
    every_test_nonzero (x :: q) = negb (Nat.eqb (fw_waiptn x) 0) && every_test_nonzero q.
Proof. intros x q. reflexivity. Qed.

Lemma every_mode_legal_cons : forall x q,
    every_mode_legal (x :: q) = wfmode_ok (fw_wfmode x) && every_mode_legal q.
Proof. intros x q. reflexivity. Qed.

Lemma head_test_nonzero : forall x q,
    every_test_nonzero (x :: q) = true -> negb (Nat.eqb (fw_waiptn x) 0) = true.
Proof.
  intros x q H. rewrite every_test_nonzero_cons in H.
  apply andb_true_iff in H. destruct H as [P _]. exact P.
Qed.

Lemma head_mode_legal : forall x q,
    every_mode_legal (x :: q) = true -> wfmode_ok (fw_wfmode x) = true.
Proof.
  intros x q H. rewrite every_mode_legal_cons in H.
  apply andb_true_iff in H. destruct H as [P _]. exact P.
Qed.

(* A test that is not zero is a test that names at least one bit -- the guard
 * at eventflag.c:284 restated as a figure, which is what makes the two silent
 * failure modes of §16.2 unreachable from a well-formed cell. *)
Lemma a_nonzero_test_names_a_bit : forall w,
    negb (Nat.eqb w 0) = true -> 0 < w.
Proof.
  intros w H. destruct w as [|n]; [ | lia ].
  cbn [Nat.eqb negb] in H. discriminate H.
Qed.

Lemma every_test_nonzero_app : forall q1 q2,
    every_test_nonzero (q1 ++ q2) = every_test_nonzero q1 && every_test_nonzero q2.
Proof.
  intros q1. induction q1 as [|x rest IH]; intros q2.
  - cbn [app every_test_nonzero forallb]. reflexivity.
  - cbn [app]. rewrite !every_test_nonzero_cons, IH. apply andb_assoc.
Qed.

Lemma every_mode_legal_app : forall q1 q2,
    every_mode_legal (q1 ++ q2) = every_mode_legal q1 && every_mode_legal q2.
Proof.
  intros q1. induction q1 as [|x rest IH]; intros q2.
  - cbn [app every_mode_legal forallb]. reflexivity.
  - cbn [app]. rewrite !every_mode_legal_cons, IH. apply andb_assoc.
Qed.

(* Both halves survive a join by priority, which is the invariant step of the
 * blocking path: the caller that reaches the queue has already been through
 * CHECK_PAR, so its own test and mode are in order. *)
Lemma flg_insert_keeps_the_tests_nonzero : forall who q,
    every_test_nonzero q = true -> negb (Nat.eqb (fw_waiptn who) 0) = true ->
    every_test_nonzero (flg_insert_tpri who q) = true.
Proof.
  intros who q. revert who.
  induction q as [|x rest IH]; intros who H P.
  - cbn [flg_insert_tpri]. rewrite every_test_nonzero_cons, P. reflexivity.
  - destruct (Nat.ltb (fw_pri who) (fw_pri x)) eqn:Pr.
    + rewrite (flg_insert_puts_the_better_task_first who x rest Pr).
      rewrite every_test_nonzero_cons. apply andb_true_iff.
      split; [ exact P | exact H ].
    + rewrite every_test_nonzero_cons in H. apply andb_true_iff in H.
      destruct H as [Nx Hrest].
      rewrite (flg_insert_tie_defers who x rest Pr), every_test_nonzero_cons, Nx.
      apply andb_true_iff. split; [ reflexivity | apply (IH who Hrest P) ].
Qed.

Lemma flg_insert_keeps_the_modes_legal : forall who q,
    every_mode_legal q = true -> wfmode_ok (fw_wfmode who) = true ->
    every_mode_legal (flg_insert_tpri who q) = true.
Proof.
  intros who q. revert who.
  induction q as [|x rest IH]; intros who H Q.
  - cbn [flg_insert_tpri]. rewrite every_mode_legal_cons, Q. reflexivity.
  - destruct (Nat.ltb (fw_pri who) (fw_pri x)) eqn:Pr.
    + rewrite (flg_insert_puts_the_better_task_first who x rest Pr).
      rewrite every_mode_legal_cons. apply andb_true_iff.
      split; [ exact Q | exact H ].
    + rewrite every_mode_legal_cons in H. apply andb_true_iff in H.
      destruct H as [L Hrest].
      rewrite (flg_insert_tie_defers who x rest Pr), every_mode_legal_cons, L.
      apply andb_true_iff. split; [ reflexivity | apply (IH who Hrest Q) ].
Qed.

Lemma flg_enqueue_keeps_the_tests_nonzero : forall tpri who q,
    every_test_nonzero q = true -> negb (Nat.eqb (fw_waiptn who) 0) = true ->
    every_test_nonzero (flg_enqueue tpri who q) = true.
Proof.
  intros tpri who q H P. unfold flg_enqueue. destruct tpri.
  - apply flg_insert_keeps_the_tests_nonzero. exact H. exact P.
  - rewrite every_test_nonzero_app, H. rewrite every_test_nonzero_cons, P.
    reflexivity.
Qed.

Lemma flg_enqueue_keeps_the_modes_legal : forall tpri who q,
    every_mode_legal q = true -> wfmode_ok (fw_wfmode who) = true ->
    every_mode_legal (flg_enqueue tpri who q) = true.
Proof.
  intros tpri who q H Q. unfold flg_enqueue. destruct tpri.
  - apply flg_insert_keeps_the_modes_legal. exact H. exact Q.
  - rewrite every_mode_legal_app, H. rewrite every_mode_legal_cons, Q.
    reflexivity.
Qed.

Lemma flg_wf_survives_an_enqueue : forall tpri c who,
    flg_wf c = true -> negb (Nat.eqb (fw_waiptn who) 0) = true ->
    wfmode_ok (fw_wfmode who) = true ->
    flg_wf (mk_flgcb (fc_id c) (fc_tpri c) (fc_wmul c) (fc_nodis c) (fc_pat c)
                      (flg_enqueue tpri who (fc_wait c))) = true.
Proof.
  intros tpri c who W P Q. rewrite flg_wf_of_a_cell. apply andb_true_iff. split.
  - apply flg_enqueue_keeps_the_tests_nonzero.
    + apply (flg_wf_gives_positive_tests c W).
    + exact P.
  - apply flg_enqueue_keeps_the_modes_legal.
    + apply (flg_wf_gives_legal_modes c W).
    + exact Q.
Qed.

(* The pattern is not part of the shape: no guard reads it, so set_flg's
 * union and clr_flg's intersection both leave flg_wf exactly where it was.
 * Stated as an equality of figures, so §16.8 can rewrite with it. *)
Lemma a_clear_leaves_the_shape_alone : forall c p,
    flg_wf (mk_flgcb (fc_id c) (fc_tpri c) (fc_wmul c) (fc_nodis c) p (fc_wait c)) =
    flg_wf c.
Proof.
  intros c p. unfold flg_wf. destruct c; reflexivity.
Qed.

(* ── 16.5 The preflight cascades of this family ────────────────── *)

(* cre_flg, eventflag.c:116-158.  The only pre-creation test the shipped build
 * runs is CHECK_RSATR (:132), and §12's reason for leaving E_RSATR out of every
 * cascade applies here unchanged: the cell records the three attribute bits
 * cre_flg accepts (TA_TPRI | TA_WMUL | TA_NODISWAI, :119-126), so an illegal
 * attribute is not a state the model can name.  What is left is the FreeQue
 * failure at :137, which is the whole cascade -- and the reason this service is
 * the only one of the six with no E_ID receipt: cre_flg allocates its own id,
 * so it has no range to check. *)
Definition flg_cre_guards (free_cell : bool) : list (bool * er) :=
  (free_cell, E_LIMIT) :: nil.

Lemma flag_cre_E_LIMIT_is_exhaustion : forall free,
    first_bad (flg_cre_guards free) = Some E_LIMIT -> free = false.
Proof. intros free H. unfold flg_cre_guards in H. destruct free; cbn [first_bad] in H;
  try discriminate H; reflexivity.
Qed.

Lemma cre_has_no_range_test : forall free,
    first_bad (flg_cre_guards free) <> Some E_ID.
Proof. intros free H. unfold flg_cre_guards in H. destruct free; cbn [first_bad] in H;
  discriminate H.
Qed.

(* set_flg (:200, :205), clr_flg (:255, :260), del_flg (:169, :174) and
 * ref_flg (:344, :349) all run the SAME two tests in the same order: the id
 * range, then the stored marker inside the critical section.  One cascade
 * therefore serves four services, which is a fact about this family and not a
 * shortcut: none of the four has a CHECK_PAR, a CHECK_TMOUT or a
 * CHECK_DISPATCH, so none of them can refuse on a parameter, a timeout figure
 * or a context.  wai_flg is the one that has all three. *)
Definition flg_object_guards (used : bool) (id : nat) : list (bool * er) :=
  (chk_id min_flgid num_flg id, E_ID) :: (used, E_NOEXS) :: nil.

Lemma an_object_refusal_is_the_range_or_the_marker : forall used id,
    first_bad (flg_object_guards used id) = Some E_ID \/
    first_bad (flg_object_guards used id) = Some E_NOEXS ->
    chk_id min_flgid num_flg id = false \/ used = false.
Proof.
  intros used id H. unfold flg_object_guards in H.
  destruct (chk_id min_flgid num_flg id); destruct used;
    cbn [first_bad] in H;
    first [destruct H; discriminate | left; reflexivity | right; reflexivity].
Qed.

Lemma the_range_test_comes_first : forall id,
    first_bad (flg_object_guards false id) = Some E_ID ->
    chk_id min_flgid num_flg id = false.
Proof.
  intros id H. unfold flg_object_guards in H.
  destruct (chk_id min_flgid num_flg id); cbn [first_bad] in H;
    [ discriminate H | reflexivity ].
Qed.

(* A deletion of a nonexistent flag reports E_NOEXS, not E_OBJ: the marker is
 * the test the C actually makes (:174), and it is the same figure for all four
 * services above.  This is worth writing down because §13's mailbox deletion
 * reports E_OBJ once a waiter is present -- the two families differ there. *)
Example a_missing_flag_is_not_an_object_error :
    first_bad (flg_object_guards false 0) = Some E_ID /\
    first_bad (flg_object_guards false 1) = Some E_NOEXS /\
    first_bad (flg_object_guards true 1) = None.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* The guard at :296-299, on its own before the cascade that uses it.  TA_WMUL
 * is the attribute that makes a second waiter possible at all; without it, a
 * non-empty queue is a refusal rather than a queueing decision. *)
Definition another_waiter_is_present (q : list flg_who) : bool :=
  match q with nil => false | _ :: _ => true end.

Definition queue_refuses_a_second_waiter (wmul : bool) (q : list flg_who) : bool :=
  andb (negb wmul) (another_waiter_is_present q).

Lemma the_second_waiter_guard_needs_both_coordinates : forall w q,
    queue_refuses_a_second_waiter w q = true ->
    w = false /\ another_waiter_is_present q = true.
Proof.
  intros w q H. unfold queue_refuses_a_second_waiter in H. apply andb_true_iff in H.
  destruct H as [A B]. split; [ apply negb_true_iff; exact A | exact B ].
Qed.

Lemma the_second_waiter_guard_is_beatable_from_either_side :
    queue_refuses_a_second_waiter false [mk_flg_who 7 1 0 1] = true /\
    queue_refuses_a_second_waiter true [mk_flg_who 7 1 0 1] = false /\
    queue_refuses_a_second_waiter false nil = false.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* wai_flg, eventflag.c:283-306, in the order the receipts are produced.  Seven
 * tests, one more than §14.5's semaphore cascade, and the extra one is not a
 * variant of a semaphore test: the two E_PAR entries in a row are unique to
 * this family (CHECK_PAR(waiptn != 0) at :284 and the wfmode complement at
 * :285), and the E_OBJ at :296-299 has no semaphore counterpart.
 * CHECK_TMOUT (:286) is absent for §3's reason: tmo_legal_is_total says the
 * model's timeout type already carries that test.
 *
 * Every test but the stored marker is written as its own definition, and that
 * is not decoration: the cascade has to mention the same term the lemmas below
 * conclude about, or a case analysis on the guard cannot reduce the goal.  Four
 * of the six are negations in the C -- waiptn != 0 (:284), the complement of the
 * mode mask (:285), !isQueEmpty (:297) and the double negation of is_diswai
 * (:303) -- so each one also gets a bridge lemma, which is where the double
 * negative is discharged once instead of in every statement below. *)
Definition flg_guard_range (id : nat) : bool := chk_id min_flgid num_flg id.
Definition flg_guard_test_nonzero (w : nat) : bool := negb (Nat.eqb w 0).
Definition flg_guard_mode_legal (m : nat) : bool := wfmode_ok m.
Definition flg_guard_dispatchable (st : kst) : bool := negb (b_ddsp st).
Definition flg_guard_admits_a_waiter (wmul : bool) (q : list flg_who) : bool :=
  negb (queue_refuses_a_second_waiter wmul q).
Definition flg_guard_unmasked (mask : nat) (nodis : bool) : bool :=
  negb (diswai_of (masked_for mask WO_FLG) nodis).

Definition flg_wai_guards (st : kst) (used wmul nodis : bool) (mask : nat)
           (q : list flg_who) (id ptn mode : nat) : list (bool * er) :=
  (flg_guard_range id, E_ID) ::
  (flg_guard_test_nonzero ptn, E_PAR) ::
  (flg_guard_mode_legal mode, E_PAR) ::
  (flg_guard_dispatchable st, E_CTX) ::
  (used, E_NOEXS) ::
  (flg_guard_admits_a_waiter wmul q, E_OBJ) ::
  (flg_guard_unmasked mask nodis, E_DISWAI) :: nil.

(* :284 refused the call exactly when the test named no bit; the converse, that
 * a positive test passes, is §16.4's a_nonzero_test_names_a_bit read the other
 * way round, and the two together make the guard a restatement of the C line. *)
Lemma an_empty_test_is_what_refuses_the_nonzero_guard : forall w,
    flg_guard_test_nonzero w = false -> w = 0.
Proof.
  intros w H. unfold flg_guard_test_nonzero in H. apply negb_false_iff in H.
  apply Nat.eqb_eq in H. exact H.
Qed.

Lemma a_failed_dispatch_guard_disables_the_context : forall st,
    flg_guard_dispatchable st = false -> b_ddsp st = true.
Proof.
  intros st H. unfold flg_guard_dispatchable in H.
  apply negb_false_iff in H. exact H.
Qed.

Lemma a_disabled_context_fails_the_dispatch_guard : forall st,
    b_ddsp st = true -> flg_guard_dispatchable st = false.
Proof. intros st D. unfold flg_guard_dispatchable. rewrite D. reflexivity. Qed.

Lemma a_failed_waiter_guard_refuses_a_second_waiter : forall w q,
    flg_guard_admits_a_waiter w q = false ->
    queue_refuses_a_second_waiter w q = true.
Proof.
  intros w q H. unfold flg_guard_admits_a_waiter in H.
  apply negb_false_iff in H. exact H.
Qed.

Lemma a_refused_second_waiter_fails_the_guard : forall w q,
    queue_refuses_a_second_waiter w q = true ->
    flg_guard_admits_a_waiter w q = false.
Proof. intros w q R. unfold flg_guard_admits_a_waiter. rewrite R. reflexivity. Qed.

Lemma a_failed_diswai_guard_masks_the_task : forall mask nodis,
    flg_guard_unmasked mask nodis = false ->
    diswai_of (masked_for mask WO_FLG) nodis = true.
Proof.
  intros mask nodis H. unfold flg_guard_unmasked in H.
  apply negb_false_iff in H. exact H.
Qed.

(* Each receipt names its own guard, and every guard before it passed -- the §5
 * property of first_bad, instantiated on this family's seven tests.  The
 * conclusions stay in the guard vocabulary for the reason above: the case
 * analysis abstracts exactly the terms the cascade mentions. *)
Lemma flg_wai_E_ID_is_the_range : forall st used wmul nodis mask q id ptn mode,
    first_bad (flg_wai_guards st used wmul nodis mask q id ptn mode) = Some E_ID ->
    flg_guard_range id = false.
Proof.
  intros st used wmul nodis mask q id ptn mode H. unfold flg_wai_guards in H.
  destruct (flg_guard_range id); destruct (flg_guard_test_nonzero ptn);
    destruct (flg_guard_mode_legal mode); destruct (flg_guard_dispatchable st);
    destruct used; destruct (flg_guard_admits_a_waiter wmul q);
    destruct (flg_guard_unmasked mask nodis);
    cbn [first_bad] in H; try discriminate H; reflexivity.
Qed.

(* The two parameter errors share a receipt and are told apart only by which
 * figure failed.  Both are worth keeping because §16.2 shows each one prevents
 * a DIFFERENT silent misbehaviour: the empty test turns the call into a read
 * (or, in OR-mode, into a wait that never ends), and a stray mode bit turns a
 * named mode into an unnamed one. *)
Lemma flg_wai_E_PAR_is_the_empty_or_the_mode_test :
  forall st used wmul nodis mask q id ptn mode,
    first_bad (flg_wai_guards st used wmul nodis mask q id ptn mode) = Some E_PAR ->
    flg_guard_test_nonzero ptn = false \/ flg_guard_mode_legal mode = false.
Proof.
  intros st used wmul nodis mask q id ptn mode H. unfold flg_wai_guards in H.
  destruct (flg_guard_range id); destruct (flg_guard_test_nonzero ptn);
    destruct (flg_guard_mode_legal mode); destruct (flg_guard_dispatchable st);
    destruct used; destruct (flg_guard_admits_a_waiter wmul q);
    destruct (flg_guard_unmasked mask nodis);
    cbn [first_bad] in H; try discriminate H;
    first [left; reflexivity | right; reflexivity].
Qed.

(* The receipt E_PAR never arrives with the range test still owed: :283 runs
 * before :284, so a parameter error is evidence that the id was in range. *)
Lemma flg_wai_E_PAR_needs_a_passed_range :
  forall st used wmul nodis mask q id ptn mode,
    first_bad (flg_wai_guards st used wmul nodis mask q id ptn mode) = Some E_PAR ->
    flg_guard_range id = true.
Proof.
  intros st used wmul nodis mask q id ptn mode H. unfold flg_wai_guards in H.
  destruct (flg_guard_range id); destruct (flg_guard_test_nonzero ptn);
    destruct (flg_guard_mode_legal mode); destruct (flg_guard_dispatchable st);
    destruct used; destruct (flg_guard_admits_a_waiter wmul q);
    destruct (flg_guard_unmasked mask nodis);
    cbn [first_bad] in H; try discriminate H; reflexivity.
Qed.

Lemma flg_wai_E_CTX_is_a_context_refusal :
  forall st used wmul nodis mask q id ptn mode,
    first_bad (flg_wai_guards st used wmul nodis mask q id ptn mode) = Some E_CTX ->
    flg_guard_range id = true /\ flg_guard_test_nonzero ptn = true /\
    flg_guard_mode_legal mode = true /\ flg_guard_dispatchable st = false.
Proof.
  intros st used wmul nodis mask q id ptn mode H. unfold flg_wai_guards in H.
  destruct (flg_guard_range id); destruct (flg_guard_test_nonzero ptn);
    destruct (flg_guard_mode_legal mode); destruct (flg_guard_dispatchable st);
    destruct used; destruct (flg_guard_admits_a_waiter wmul q);
    destruct (flg_guard_unmasked mask nodis);
    cbn [first_bad] in H; try discriminate H;
    repeat split; reflexivity.
Qed.

Lemma flg_wai_E_NOEXS_is_the_stored_marker :
  forall st used wmul nodis mask q id ptn mode,
    first_bad (flg_wai_guards st used wmul nodis mask q id ptn mode) = Some E_NOEXS ->
    flg_guard_range id = true /\ flg_guard_test_nonzero ptn = true /\
    flg_guard_mode_legal mode = true /\ flg_guard_dispatchable st = true /\
    used = false.
Proof.
  intros st used wmul nodis mask q id ptn mode H. unfold flg_wai_guards in H.
  destruct (flg_guard_range id); destruct (flg_guard_test_nonzero ptn);
    destruct (flg_guard_mode_legal mode); destruct (flg_guard_dispatchable st);
    destruct used; destruct (flg_guard_admits_a_waiter wmul q);
    destruct (flg_guard_unmasked mask nodis);
    cbn [first_bad] in H; try discriminate H;
    repeat split; reflexivity.
Qed.

(* :296-299: E_OBJ here means the queue was already occupied and the cell has no
 * TA_WMUL.  The marker guard before it passed, so the cell IS live -- the
 * refusal is about the queue, not about the object existing.  This is the
 * receipt with no semaphore counterpart. *)
Lemma flg_wai_E_OBJ_is_a_second_waiter :
  forall st used wmul nodis mask q id ptn mode,
    first_bad (flg_wai_guards st used wmul nodis mask q id ptn mode) = Some E_OBJ ->
    used = true /\ flg_guard_admits_a_waiter wmul q = false.
Proof.
  intros st used wmul nodis mask q id ptn mode H. unfold flg_wai_guards in H.
  destruct (flg_guard_range id); destruct (flg_guard_test_nonzero ptn);
    destruct (flg_guard_mode_legal mode); destruct (flg_guard_dispatchable st);
    destruct used; destruct (flg_guard_admits_a_waiter wmul q);
    destruct (flg_guard_unmasked mask nodis);
    cbn [first_bad] in H; try discriminate H;
    repeat split; reflexivity.
Qed.

(* The last guard of the cascade, and like §14's semaphore counterpart it reads
 * both a task coordinate (the mask) and an object coordinate (TA_NODISWAI). *)
Lemma flg_wai_E_DISWAI_is_a_diswai_refusal :
  forall st used wmul nodis mask q id ptn mode,
    first_bad (flg_wai_guards st used wmul nodis mask q id ptn mode) = Some E_DISWAI ->
    used = true /\ flg_guard_dispatchable st = true /\
    flg_guard_admits_a_waiter wmul q = true /\
    flg_guard_unmasked mask nodis = false.
Proof.
  intros st used wmul nodis mask q id ptn mode H. unfold flg_wai_guards in H.
  destruct (flg_guard_range id); destruct (flg_guard_test_nonzero ptn);
    destruct (flg_guard_mode_legal mode); destruct (flg_guard_dispatchable st);
    destruct used; destruct (flg_guard_admits_a_waiter wmul q);
    destruct (flg_guard_unmasked mask nodis);
    cbn [first_bad] in H; try discriminate H;
    repeat split; reflexivity.
Qed.

(* A wai call that reaches the critical section is a whole cascade that passes:
 * the figure below is the geometry the rest of §16 assumes -- an in-range id, a
 * test that names a bit, a legal mode, a dispatchable context, a live cell,
 * either TA_WMUL or an empty queue, and an unmasked task.  §6's st0 is
 * dispatch-disabled by construction, so the calls that have to get past
 * CHECK_DISPATCH are computed against st_disp, the other half of §6's existence
 * lemma at :710. *)
Definition st_disp : kst :=
  mk_kst (fun _ => free_tcb) (fun _ => free_mbx) (fun _ => free_sem) 7 false false.

Example a_wai_call_that_reaches_the_critical_section :
    first_bad (flg_wai_guards st_disp true true false 0 [mk_flg_who 8 1 0 2] 1 1 0) = None.
Proof. vm_compute. reflexivity. Qed.

(* The family comparison §14 invited: the semaphore's cascade spends one test on
 * the requested count and one on the ceiling headroom, and it has no queue guard
 * at all; the flag has no ceiling and spends its extra guard on TA_WMUL.  Same
 * first test, same last test, and five figures between them that are not the
 * same service. *)
Example the_two_families_differ_in_the_middle :
    first_bad (sem_wai_guards st_disp true false false 1 0) = Some E_PAR /\
    first_bad (flg_wai_guards st_disp true true false 0 nil 1 0 0) = Some E_PAR /\
    first_bad (sem_wai_guards st_disp true false false 1 1) = None /\
    first_bad (flg_wai_guards st_disp true true false 0 nil 1 1 0) = None.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* ── 16.6 The wait, as a step on the cell ──────────────────────── *)

(* eventflag.c:313-318, the clear the answer path performs, and :229-237, the
 * same two tests inside the set walk.  They are NOT an else-if chain in the C:
 * a wfmode with both TWF_BITCLR and TWF_CLR drops the tested bits, tests the
 * figure for zero, and then wipes it regardless.  The order is visible only
 * through the intermediate zero-test, which is §16.7's stop condition. *)
Definition wai_clear (mode : nat) (p w : nat) : nat :=
  let p1 := if bitclr_mode mode then pat_clr p w else p in
  if clr_mode mode then 0 else p1.

Lemma a_full_clear_wipes_the_pattern : forall mode p w,
    clr_mode mode = true -> wai_clear mode p w = 0.
Proof. intros mode p w C. unfold wai_clear. rewrite C. reflexivity. Qed.

Lemma a_bit_clear_only_drops_the_tested_bits : forall mode p w,
    clr_mode mode = false -> bitclr_mode mode = true ->
    wai_clear mode p w = pat_clr p w.
Proof. intros mode p w C B. unfold wai_clear. rewrite C, B. reflexivity. Qed.

Lemma no_clear_mode_is_the_identity : forall mode p w,
    clr_mode mode = false -> bitclr_mode mode = false -> wai_clear mode p w = p.
Proof. intros mode p w C B. unfold wai_clear. rewrite C, B. reflexivity. Qed.

Lemma the_answered_clear_never_grows_the_pattern : forall mode p w,
    Nat.leb (wai_clear mode p w) p = true.
Proof.
  intros mode p w. unfold wai_clear. destruct (clr_mode mode) eqn:C.
  - apply Nat.leb_le. apply Nat.le_0_l.
  - destruct (bitclr_mode mode) eqn:B.
    + apply Nat.leb_le. unfold pat_clr. apply Nat.ldiff_le_l.
    + apply Nat.leb_le. apply Nat.le_refl.
Qed.

(* The stop condition of the set walk (:230-236).  TWF_CLR always stops;
 * TWF_BITCLR stops only when its own drop emptied the pattern.  Written as a
 * disjunction of the two branches in the C's order. *)
Definition flg_stop_after (mode : nat) (p_after : nat) : bool :=
  orb (clr_mode mode) (andb (bitclr_mode mode) (Nat.eqb p_after 0)).

Lemma a_full_clear_always_stops : forall mode p,
    clr_mode mode = true -> flg_stop_after mode p = true.
Proof. intros mode p C. unfold flg_stop_after. rewrite C. reflexivity. Qed.

Lemma an_ordinary_waiter_never_stops_the_walk : forall mode p,
    clr_mode mode = false -> bitclr_mode mode = false ->
    flg_stop_after mode p = false.
Proof.
  intros mode p C B. unfold flg_stop_after. rewrite C, B.
  cbn [orb negb]. reflexivity.
Qed.

(* The reply is a pair, because wai_flg has two outputs: the receipt and the
 * figure it leaves in the caller's UINT.  Some p means "*p_flgptn was written
 * with p during this call" (:310); None covers BOTH a refusal (:294, :298, :304
 * return without touching the slot) and a block, where the write happens later,
 * in the set walk at :225, with a value this call cannot know yet.  Collapsing
 * those two into one answer is the error the §15 reply law was written to avoid. *)
Record flg_reply : Type := mk_flg_reply {
    fr_ptn : option nat;
    fr_rc  : er
  }.

Definition flg_block (who : flg_who) (c : flgcb) : flgcb :=
  mk_flgcb (fc_id c) (fc_tpri c) (fc_wmul c) (fc_nodis c) (fc_pat c)
           (flg_enqueue (fc_tpri c) who (fc_wait c)).

(* wai_flg, :289-328, after §16.5's cascade.  The three state tests are in the
 * C's order: TA_WMUL, then the wait-disable, then the condition. *)
Definition flg_wai (mask : nat) (t : tmo) (who : flg_who) (c : flgcb) : flgcb * flg_reply :=
  if queue_refuses_a_second_waiter (fc_wmul c) (fc_wait c)
  then (c, mk_flg_reply None E_OBJ)
  else if diswai_of (masked_for mask WO_FLG) (fc_nodis c)
  then (c, mk_flg_reply None E_DISWAI)
  else if flg_cond (fc_pat c) (fw_waiptn who) (fw_wfmode who)
  then (mk_flgcb (fc_id c) (fc_tpri c) (fc_wmul c) (fc_nodis c)
                  (wai_clear (fw_wfmode who) (fc_pat c) (fw_waiptn who)) (fc_wait c),
        mk_flg_reply (Some (fc_pat c)) E_OK)
  else if tmo_blocks t
  then (flg_block who c, mk_flg_reply None (prewrite false))
  else (c, mk_flg_reply None (prewrite false)).

(* The answer reads the cell BEFORE it clears it: :310 stores flgcb->flgptn and
 * :313-318 mutate it afterwards.  So the figure the caller gets is the pattern
 * that satisfied the test, not the pattern the object is left with -- and for a
 * TWF_CLR waiter those two differ by everything the pattern had. *)
Lemma the_answer_is_the_pattern_before_the_clear : forall mask t who c,
    queue_refuses_a_second_waiter (fc_wmul c) (fc_wait c) = false ->
    diswai_of (masked_for mask WO_FLG) (fc_nodis c) = false ->
    flg_cond (fc_pat c) (fw_waiptn who) (fw_wfmode who) = true ->
    fr_ptn (snd (flg_wai mask t who c)) = Some (fc_pat c).
Proof.
  intros mask t who c A B C. unfold flg_wai. rewrite A, B, C. reflexivity.
Qed.

Lemma an_answered_wait_returns_E_OK : forall mask t who c,
    queue_refuses_a_second_waiter (fc_wmul c) (fc_wait c) = false ->
    diswai_of (masked_for mask WO_FLG) (fc_nodis c) = false ->
    flg_cond (fc_pat c) (fw_waiptn who) (fw_wfmode who) = true ->
    fr_rc (snd (flg_wai mask t who c)) = E_OK.
Proof.
  intros mask t who c A B C. unfold flg_wai. rewrite A, B, C. reflexivity.
Qed.

(* and the cell it leaves is the cleared one, with the queue exactly as it was:
 * a waiter that is answered was never on this queue. *)
Lemma an_answered_wait_keeps_the_queue : forall mask t who c,
    queue_refuses_a_second_waiter (fc_wmul c) (fc_wait c) = false ->
    diswai_of (masked_for mask WO_FLG) (fc_nodis c) = false ->
    flg_cond (fc_pat c) (fw_waiptn who) (fw_wfmode who) = true ->
    fc_wait (fst (flg_wai mask t who c)) = fc_wait c.
Proof.
  intros mask t who c A B C. unfold flg_wai. rewrite A, B, C. reflexivity.
Qed.

Lemma an_answered_wait_leaves_the_identity_fields : forall mask t who c,
    queue_refuses_a_second_waiter (fc_wmul c) (fc_wait c) = false ->
    diswai_of (masked_for mask WO_FLG) (fc_nodis c) = false ->
    flg_cond (fc_pat c) (fw_waiptn who) (fw_wfmode who) = true ->
    fc_id (fst (flg_wai mask t who c)) = fc_id c /\
    fc_tpri (fst (flg_wai mask t who c)) = fc_tpri c /\
    fc_wmul (fst (flg_wai mask t who c)) = fc_wmul c /\
    fc_nodis (fst (flg_wai mask t who c)) = fc_nodis c.
Proof.
  intros mask t who c A B C. unfold flg_wai. rewrite A, B, C.
  repeat split; reflexivity.
Qed.

(* Every refusal writes nothing.  Three of the four branches below return
 * straight out of the critical section without touching p_flgptn -- :294, :298
 * and :304 -- and the fourth is the block, whose write belongs to the release. *)
Lemma a_second_waiter_is_refused_without_a_write : forall mask t who c,
    queue_refuses_a_second_waiter (fc_wmul c) (fc_wait c) = true ->
    flg_wai mask t who c = (c, mk_flg_reply None E_OBJ).
Proof. intros mask t who c A. unfold flg_wai. rewrite A. reflexivity. Qed.

Lemma a_diswai_refusal_writes_nothing : forall mask t who c,
    queue_refuses_a_second_waiter (fc_wmul c) (fc_wait c) = false ->
    diswai_of (masked_for mask WO_FLG) (fc_nodis c) = true ->
    flg_wai mask t who c = (c, mk_flg_reply None E_DISWAI).
Proof.
  intros mask t who c A B. unfold flg_wai. rewrite A, B. reflexivity.
Qed.

Lemma a_poll_failure_writes_nothing_and_registers_nobody : forall mask who c,
    queue_refuses_a_second_waiter (fc_wmul c) (fc_wait c) = false ->
    diswai_of (masked_for mask WO_FLG) (fc_nodis c) = false ->
    flg_cond (fc_pat c) (fw_waiptn who) (fw_wfmode who) = false ->
    flg_wai mask TMO_POLL who c = (c, mk_flg_reply None (prewrite false)).
Proof.
  intros mask who c A B C. unfold flg_wai. rewrite A, B, C.
  cbn [tmo_blocks]. reflexivity.
Qed.

(* The blocking half: the caller joins the queue, the pattern is untouched, and
 * the receipt is §12.4's pre-write, which is the figure the release path will
 * later override. *)
Lemma a_blocked_wait_registers_the_caller : forall mask t who c,
    queue_refuses_a_second_waiter (fc_wmul c) (fc_wait c) = false ->
    diswai_of (masked_for mask WO_FLG) (fc_nodis c) = false ->
    flg_cond (fc_pat c) (fw_waiptn who) (fw_wfmode who) = false ->
    tmo_blocks t = true ->
    fc_wait (fst (flg_wai mask t who c)) =
    flg_enqueue (fc_tpri c) who (fc_wait c).
Proof.
  intros mask t who c A B C D. unfold flg_wai. rewrite A, B, C, D. reflexivity.
Qed.

Lemma a_blocked_wait_leaves_the_pattern : forall mask t who c,
    queue_refuses_a_second_waiter (fc_wmul c) (fc_wait c) = false ->
    diswai_of (masked_for mask WO_FLG) (fc_nodis c) = false ->
    flg_cond (fc_pat c) (fw_waiptn who) (fw_wfmode who) = false ->
    tmo_blocks t = true ->
    fc_pat (fst (flg_wai mask t who c)) = fc_pat c.
Proof.
  intros mask t who c A B C D. unfold flg_wai, flg_block. rewrite A, B, C, D.
  reflexivity.
Qed.

Lemma a_blocked_wait_writes_nothing_yet : forall mask t who c,
    queue_refuses_a_second_waiter (fc_wmul c) (fc_wait c) = false ->
    diswai_of (masked_for mask WO_FLG) (fc_nodis c) = false ->
    flg_cond (fc_pat c) (fw_waiptn who) (fw_wfmode who) = false ->
    fr_ptn (snd (flg_wai mask t who c)) = None.
Proof.
  intros mask t who c A B C. unfold flg_wai. rewrite A, B, C.
  destruct (tmo_blocks t); reflexivity.
Qed.

Lemma a_blocked_wait_keeps_the_shape : forall mask t who c,
    flg_wf c = true -> negb (Nat.eqb (fw_waiptn who) 0) = true ->
    wfmode_ok (fw_wfmode who) = true ->
    queue_refuses_a_second_waiter (fc_wmul c) (fc_wait c) = false ->
    diswai_of (masked_for mask WO_FLG) (fc_nodis c) = false ->
    flg_cond (fc_pat c) (fw_waiptn who) (fw_wfmode who) = false ->
    tmo_blocks t = true ->
    flg_wf (fst (flg_wai mask t who c)) = true.
Proof.
  intros mask t who c W P Q A B C D.
  unfold flg_wai. rewrite A, B, C, D.
  unfold flg_block. apply flg_wf_survives_an_enqueue.
  - exact W.
  - exact P.
  - exact Q.
Qed.

(* The two ways a wai_flg can be refused on STATE alone, computed: the same
 * cell, the same caller, and the attribute bit as the only difference. *)
Example the_attribute_bit_decides_the_second_waiter :
    fr_rc (snd (flg_wai 0 TMO_FEVR (mk_flg_who 8 1 0 2)
               (mk_flgcb 1 false false false 0 [mk_flg_who 7 1 0 1]))) = E_OBJ /\
    fr_rc (snd (flg_wai 0 TMO_FEVR (mk_flg_who 8 1 0 2)
               (mk_flgcb 1 false true false 0 [mk_flg_who 7 1 0 1]))) = E_TMOUT /\
    fr_ptn (snd (flg_wai 0 TMO_FEVR (mk_flg_who 8 1 0 2)
               (mk_flgcb 1 false true false 0 [mk_flg_who 7 1 0 1]))) = None.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* ── 16.7 The set walk ────────────────────────────────────────── *)

(* eventflag.c:214-239 as a fold.  Three answers, not two, because the walk
 * mutates the pattern as it goes and each released task is handed the value the
 * pattern had AT ITS OWN TEST (:225), which is a different figure for every
 * release once a clearing mode is in the queue.  The released list therefore
 * carries the waiter together with the answer it was given. *)
Record flg_release : Type := mk_flg_release {
    rl_who : flg_who;
    rl_pat : nat                    (* *p_flgptn as the kernel wrote it, :225 *)
  }.

Record flg_walk : Type := mk_flg_walk {
    k_pat  : nat;                   (* the pattern the object is left with *)
    k_kept : list flg_who;          (* still waiting, in queue order *)
    k_gone : list flg_release       (* released, in the order they were met *)
  }.

(* The one way an entry stays and the two ways it leaves, as functions rather
 * than as inline record constructions -- §14.7's drain_skip and drain_take
 * again, for the same reason: a branch equation can then name the step without
 * talking about a projection of a constructor. *)
Definition flg_walk_keeps (x : flg_who) (w : flg_walk) : flg_walk :=
  mk_flg_walk (k_pat w) (x :: k_kept w) (k_gone w).

Definition flg_walk_releases (x : flg_who) (ans : nat) (w : flg_walk) : flg_walk :=
  mk_flg_walk (k_pat w) (k_kept w) (mk_flg_release x ans :: k_gone w).

(* :222 tests, :225 answers, :229-237 clears and perhaps breaks.  Note the two
 * different patterns in the body: a released entry is recorded with the pattern
 * BEFORE its own clear, because :225 writes the caller's slot before :230
 * mutates the object, while the walk continues from the pattern AFTER it.  That
 * is the whole content of §16.6's "the answer is not the state", and it is why
 * the answer travels in the release record.
 *
 * Written without let-bindings, so that each branch test occurs in the term
 * exactly as the four equations below name it. *)
Fixpoint flg_set_walk (p : nat) (q : list flg_who) : flg_walk :=
  match q with
  | nil => mk_flg_walk p nil nil
  | x :: rest =>
      if flg_cond p (fw_waiptn x) (fw_wfmode x) then
        if flg_stop_after (fw_wfmode x)
                          (wai_clear (fw_wfmode x) p (fw_waiptn x)) then
          mk_flg_walk (wai_clear (fw_wfmode x) p (fw_waiptn x)) rest
                      (mk_flg_release x p :: nil)
        else
          flg_walk_releases x p
            (flg_set_walk (wai_clear (fw_wfmode x) p (fw_waiptn x)) rest)
      else
        flg_walk_keeps x (flg_set_walk p rest)
  end.

(* The four steps of the walk, each under its own branch test.  Everything below
 * rewrites through these instead of case-analysing the fixpoint, which is what
 * keeps the conservation laws honest about which pattern the recursion used --
 * §14.7's sig_walk_* equations, one per branch. *)
Lemma flg_set_walk_nil : forall p,
    flg_set_walk p nil = mk_flg_walk p nil nil.
Proof. intros p. reflexivity. Qed.

Lemma flg_set_walk_not_answered : forall p x rest,
    flg_cond p (fw_waiptn x) (fw_wfmode x) = false ->
    flg_set_walk p (x :: rest) = flg_walk_keeps x (flg_set_walk p rest).
Proof. intros p x rest C. unfold flg_set_walk. rewrite C. reflexivity. Qed.

Lemma flg_set_walk_answered_stops : forall p x rest,
    flg_cond p (fw_waiptn x) (fw_wfmode x) = true ->
    flg_stop_after (fw_wfmode x)
                   (wai_clear (fw_wfmode x) p (fw_waiptn x)) = true ->
    flg_set_walk p (x :: rest) =
    mk_flg_walk (wai_clear (fw_wfmode x) p (fw_waiptn x)) rest
                (mk_flg_release x p :: nil).
Proof.
  intros p x rest C S. unfold flg_set_walk. rewrite C, S. reflexivity.
Qed.

Lemma flg_set_walk_answered_continues : forall p x rest,
    flg_cond p (fw_waiptn x) (fw_wfmode x) = true ->
    flg_stop_after (fw_wfmode x)
                   (wai_clear (fw_wfmode x) p (fw_waiptn x)) = false ->
    flg_set_walk p (x :: rest) =
    flg_walk_releases x p
      (flg_set_walk (wai_clear (fw_wfmode x) p (fw_waiptn x)) rest).
Proof.
  intros p x rest C S. unfold flg_set_walk. rewrite C, S. reflexivity.
Qed.

(* The two list answers are a partition of the queue the walk started from:
 * every entry is either released or kept, exactly once, and none is lost -- the
 * law the C's "queue = queue->next" step before each release exists to
 * guarantee.  Stated as a count because the two lists interleave, so there is
 * no equation between their concatenation and the queue. *)
Lemma the_walk_never_loses_a_waiter : forall q p,
    length q = length (k_kept (flg_set_walk p q)) + length (k_gone (flg_set_walk p q)).
Proof.
  induction q as [|x rest IH]; intros p.
  - cbn [flg_set_walk k_kept k_gone length]. reflexivity.
  - destruct (flg_cond p (fw_waiptn x) (fw_wfmode x)) eqn:C.
    + destruct (flg_stop_after (fw_wfmode x)
                               (wai_clear (fw_wfmode x) p (fw_waiptn x))) eqn:S.
      * rewrite (flg_set_walk_answered_stops p x rest C S).
        cbn [k_kept k_gone length]. lia.
      * rewrite (flg_set_walk_answered_continues p x rest C S).
        cbn [flg_walk_releases k_kept k_gone length].
        specialize (IH (wai_clear (fw_wfmode x) p (fw_waiptn x))). lia.
    + rewrite (flg_set_walk_not_answered p x rest C).
      cbn [flg_walk_keeps k_kept k_gone length]. specialize (IH p). lia.
Qed.

(* What the walk gives back: the pattern it ends on is never larger than the one
 * it started with.  Every step of the C is either a read of flgptn or an
 * intersection with its complement, and §16.1 proved both shrink. *)
Lemma the_walk_never_grows_the_pattern : forall q p,
    Nat.leb (k_pat (flg_set_walk p q)) p = true.
Proof.
  induction q as [|x rest IH]; intros p.
  - cbn [flg_set_walk k_pat]. apply Nat.leb_le. apply Nat.le_refl.
  - destruct (flg_cond p (fw_waiptn x) (fw_wfmode x)) eqn:C.
    + destruct (flg_stop_after (fw_wfmode x)
                               (wai_clear (fw_wfmode x) p (fw_waiptn x))) eqn:S.
      * rewrite (flg_set_walk_answered_stops p x rest C S). cbn [k_pat].
        apply the_answered_clear_never_grows_the_pattern.
      * rewrite (flg_set_walk_answered_continues p x rest C S).
        cbn [flg_walk_releases k_pat].
        apply Nat.leb_le.
        apply Nat.le_trans with (m := wai_clear (fw_wfmode x) p (fw_waiptn x)).
        { apply Nat.leb_le. apply (IH (wai_clear (fw_wfmode x) p (fw_waiptn x))). }
        { apply Nat.leb_le. apply the_answered_clear_never_grows_the_pattern. }
    + rewrite (flg_set_walk_not_answered p x rest C). cbn [flg_walk_keeps k_pat].
      apply (IH p).
Qed.

(* The soundness half, in the shape §16.2's scope note allows: every release is
 * justified by the answer that release was given, not by the figure the object
 * ends with.  This is the claim the C relies on at :222, and it needs no
 * absorption law, because the pattern recorded beside each waiter IS the one
 * that was tested. *)
Definition every_release_is_an_answer (w : flg_walk) : bool :=
  forallb (fun r => flg_cond (rl_pat r) (fw_waiptn (rl_who r)) (fw_wfmode (rl_who r)))
          (k_gone w).

(* What the two list steps do to that verdict -- each side of the pair is a
 * reflexive equation, so the induction below never has to look inside forallb. *)
Lemma every_release_is_an_answer_of_keeps : forall x w,
    every_release_is_an_answer (flg_walk_keeps x w) = every_release_is_an_answer w.
Proof. intros x w. reflexivity. Qed.

Lemma every_release_is_an_answer_of_releases : forall x ans w,
    every_release_is_an_answer (flg_walk_releases x ans w) =
    andb (flg_cond ans (fw_waiptn x) (fw_wfmode x)) (every_release_is_an_answer w).
Proof. intros x ans w. reflexivity. Qed.

Lemma the_walk_only_releases_answered_waiters : forall q p,
    every_release_is_an_answer (flg_set_walk p q) = true.
Proof.
  induction q as [|x rest IH]; intros p.
  - reflexivity.
  - destruct (flg_cond p (fw_waiptn x) (fw_wfmode x)) eqn:C.
    + destruct (flg_stop_after (fw_wfmode x)
                               (wai_clear (fw_wfmode x) p (fw_waiptn x))) eqn:S.
      * rewrite (flg_set_walk_answered_stops p x rest C S).
        unfold every_release_is_an_answer. cbn [forallb rl_pat rl_who].
        apply andb_true_iff. split; [ exact C | reflexivity ].
      * rewrite (flg_set_walk_answered_continues p x rest C S).
        rewrite every_release_is_an_answer_of_releases, C. cbn [andb].
        apply (IH (wai_clear (fw_wfmode x) p (fw_waiptn x))).
    + rewrite (flg_set_walk_not_answered p x rest C).
      rewrite every_release_is_an_answer_of_keeps. apply (IH p).
Qed.

(* The inert set: a pattern that suits nobody leaves the object exactly as it
 * was found -- same pattern, same queue, no release.  This is what makes
 * set_flg with an irrelevant setptn safe to call from a hook, and it is the
 * reason the walk's own induction has to carry the pattern. *)
Lemma a_walk_with_nothing_to_give_is_the_identity : forall q p,
    (forall x, In x q -> flg_cond p (fw_waiptn x) (fw_wfmode x) = false) ->
    flg_set_walk p q = mk_flg_walk p q nil.
Proof.
  induction q as [|x rest IH]; intros p H.
  - apply flg_set_walk_nil.
  - rewrite (flg_set_walk_not_answered p x rest (H x (or_introl eq_refl))).
    unfold flg_walk_keeps.
    rewrite (IH p (fun y Hin => H y (or_intror Hin))). reflexivity.
Qed.

(* The head that stops the walk.  A TWF_CLR waiter that is satisfied takes the
 * whole pattern and leaves everyone behind it waiting: the C's break at :236 is
 * unconditional, so this is exact rather than a bound. *)
Lemma a_full_clear_head_stops_the_walk : forall p x rest,
    flg_cond p (fw_waiptn x) (fw_wfmode x) = true ->
    clr_mode (fw_wfmode x) = true ->
    flg_set_walk p (x :: rest) = mk_flg_walk 0 rest (mk_flg_release x p :: nil).
Proof.
  intros p x rest C F.
  assert (S : flg_stop_after (fw_wfmode x)
                             (wai_clear (fw_wfmode x) p (fw_waiptn x)) = true).
  { unfold flg_stop_after. rewrite F. reflexivity. }
  rewrite (flg_set_walk_answered_stops p x rest C S).
  rewrite (a_full_clear_wipes_the_pattern (fw_wfmode x) p (fw_waiptn x) F).
  reflexivity.
Qed.

(* A TWF_BITCLR head that empties the pattern stops for the same reason, and the
 * zero-test at :230 is what decides it -- so the same attribute can stop one
 * walk and continue another, depending only on the figure. *)
Lemma a_bit_clear_head_stops_only_by_emptied_pattern : forall p x rest,
    flg_cond p (fw_waiptn x) (fw_wfmode x) = true ->
    clr_mode (fw_wfmode x) = false -> bitclr_mode (fw_wfmode x) = true ->
    Nat.eqb (pat_clr p (fw_waiptn x)) 0 = true ->
    flg_set_walk p (x :: rest) =
    mk_flg_walk (pat_clr p (fw_waiptn x)) rest (mk_flg_release x p :: nil).
Proof.
  intros p x rest C F B Z.
  assert (Q : wai_clear (fw_wfmode x) p (fw_waiptn x) = pat_clr p (fw_waiptn x))
    by (apply (a_bit_clear_only_drops_the_tested_bits (fw_wfmode x) p (fw_waiptn x) F B)).
  assert (S : flg_stop_after (fw_wfmode x)
                             (wai_clear (fw_wfmode x) p (fw_waiptn x)) = true).
  { rewrite Q. unfold flg_stop_after. rewrite F, B, Z. reflexivity. }
  rewrite (flg_set_walk_answered_stops p x rest C S), Q. reflexivity.
Qed.

(* ... and the complement: the same head, with one bit left over, keeps walking,
 * now from the cleared figure.  This is the branch in which the order of the
 * queue changes the answers, and the three examples at the end of §16.7 compute
 * exactly that. *)
Lemma a_bit_clear_head_that_leaves_something_continues : forall p x rest,
    flg_cond p (fw_waiptn x) (fw_wfmode x) = true ->
    clr_mode (fw_wfmode x) = false -> bitclr_mode (fw_wfmode x) = true ->
    Nat.eqb (pat_clr p (fw_waiptn x)) 0 = false ->
    flg_set_walk p (x :: rest) =
    flg_walk_releases x p (flg_set_walk (pat_clr p (fw_waiptn x)) rest).
Proof.
  intros p x rest C F B Z.
  assert (Q : wai_clear (fw_wfmode x) p (fw_waiptn x) = pat_clr p (fw_waiptn x))
    by (apply (a_bit_clear_only_drops_the_tested_bits (fw_wfmode x) p (fw_waiptn x) F B)).
  assert (S : flg_stop_after (fw_wfmode x)
                             (wai_clear (fw_wfmode x) p (fw_waiptn x)) = false).
  { rewrite Q. unfold flg_stop_after. rewrite F, B, Z. reflexivity. }
  rewrite (flg_set_walk_answered_continues p x rest C S), Q. reflexivity.
Qed.

(* set_flg, :204-245: the union first (:211), then the walk over the queue with
 * the unioned figure.  The receipt is E_OK once the cell exists -- :244 returns
 * the initialiser -- so the observable content of a set is entirely the pair
 * (the cell it leaves, the tasks it released). *)
Definition flg_set_step (setptn : nat) (c : flgcb) : flgcb * list flg_release :=
  let w := flg_set_walk (pat_or (fc_pat c) setptn) (fc_wait c) in
  (mk_flgcb (fc_id c) (fc_tpri c) (fc_wmul c) (fc_nodis c) (k_pat w) (k_kept w),
   k_gone w).

Lemma a_set_step_leaves_the_kept : forall s c,
    fc_wait (fst (flg_set_step s c)) = k_kept (flg_set_walk (pat_or (fc_pat c) s) (fc_wait c)).
Proof. intros s c. unfold flg_set_step. reflexivity. Qed.

Lemma a_set_step_releases_the_gone : forall s c,
    snd (flg_set_step s c) = k_gone (flg_set_walk (pat_or (fc_pat c) s) (fc_wait c)).
Proof. intros s c. unfold flg_set_step. reflexivity. Qed.

Lemma a_set_step_never_grows_the_union : forall s c,
    Nat.leb (fc_pat (fst (flg_set_step s c))) (pat_or (fc_pat c) s) = true.
Proof.
  intros s c. unfold flg_set_step. apply the_walk_never_grows_the_pattern.
Qed.

Lemma a_set_step_only_releases_answered_waiters : forall s c,
    every_release_is_an_answer
      (flg_set_walk (pat_or (fc_pat c) s) (fc_wait c)) = true.
Proof. intros s c. apply the_walk_only_releases_answered_waiters. Qed.

(* The cell as the services leave it: every field but the pattern survives,
 * because the C mutates flgptn in place (eventflag.c:211, :263) and touches
 * nothing else in the object's own record.  Named because §16.8's clear step and
 * the two laws below all speak it. *)
Definition flg_with_pattern (p : nat) (c : flgcb) : flgcb :=
  mk_flgcb (fc_id c) (fc_tpri c) (fc_wmul c) (fc_nodis c) p (fc_wait c).

(* A set that suits nobody is the union and nothing else.  The claim is NOT that
 * the cell is unchanged -- :211 unions setptn into flgptn whatever the queue
 * does, and my first draft of this lemma said otherwise -- but that the union is
 * the only difference, and that no task leaves the queue. *)
Lemma a_set_onto_an_uninterested_queue_only_unions : forall s c,
    (forall x, In x (fc_wait c) ->
       flg_cond (pat_or (fc_pat c) s) (fw_waiptn x) (fw_wfmode x) = false) ->
    flg_set_step s c = (flg_with_pattern (pat_or (fc_pat c) s) c, nil).
Proof.
  intros s c H. unfold flg_set_step, flg_with_pattern.
  rewrite (a_walk_with_nothing_to_give_is_the_identity (fc_wait c)
            (pat_or (fc_pat c) s) H).
  cbn [k_pat k_kept k_gone]. destruct c. reflexivity.
Qed.

(* §16.4's shape law, read through the walk: the pattern is not part of the shape
 * (a_clear_leaves_the_shape_alone), so an inert set leaves a well-formed cell
 * well-formed. *)
Lemma a_set_onto_an_uninterested_queue_keeps_the_shape : forall s c,
    flg_wf c = true ->
    (forall x, In x (fc_wait c) ->
       flg_cond (pat_or (fc_pat c) s) (fw_waiptn x) (fw_wfmode x) = false) ->
    flg_wf (fst (flg_set_step s c)) = true.
Proof.
  intros s c W H.
  rewrite (a_set_onto_an_uninterested_queue_only_unions s c H).
  cbn [fst]. unfold flg_with_pattern.
  rewrite (a_clear_leaves_the_shape_alone c (pat_or (fc_pat c) s)). exact W.
Qed.

(* The queue the walk keeps is made of entries the queue it was given already
 * had: a release only ever takes an entry out, never invents one.  This is the
 * structural half of the shape law below. *)
Lemma the_walk_keeps_only_who_it_was_given : forall q p x,
    In x (k_kept (flg_set_walk p q)) -> In x q.
Proof.
  induction q as [|y rest IH]; intros p x Hin.
  - cbn [flg_set_walk k_kept In] in Hin. exact Hin.
  - destruct (flg_cond p (fw_waiptn y) (fw_wfmode y)) eqn:C.
    + destruct (flg_stop_after (fw_wfmode y)
                               (wai_clear (fw_wfmode y) p (fw_waiptn y))) eqn:S.
      * rewrite (flg_set_walk_answered_stops p y rest C S) in Hin.
        cbn [k_kept] in Hin. right. exact Hin.
      * rewrite (flg_set_walk_answered_continues p y rest C S) in Hin.
        cbn [flg_walk_releases k_kept] in Hin.
        apply IH in Hin. right. exact Hin.
    + rewrite (flg_set_walk_not_answered p y rest C) in Hin.
      cbn [flg_walk_keeps k_kept In] in Hin.
      destruct Hin as [E|K].
      { left. exact E. }
      { right. apply IH in K. exact K. }
Qed.

Lemma every_test_nonzero_of_a_subqueue : forall q1 q2,
    (forall x, In x q2 -> In x q1) ->
    every_test_nonzero q1 = true -> every_test_nonzero q2 = true.
Proof.
  intros q1 q2 S E. unfold every_test_nonzero in *. rewrite forallb_forall in E. rewrite forallb_forall.
  intros x Hx. apply E. apply S. exact Hx.
Qed.

Lemma every_mode_legal_of_a_subqueue : forall q1 q2,
    (forall x, In x q2 -> In x q1) ->
    every_mode_legal q1 = true -> every_mode_legal q2 = true.
Proof.
  intros q1 q2 S E. unfold every_mode_legal in *. rewrite forallb_forall in E. rewrite forallb_forall.
  intros x Hx. apply E. apply S. exact Hx.
Qed.

(* A set never makes a well-formed cell ill-formed, whether or not it releases
 * anybody: both halves of flg_wf (§16.4) are forallb over the queue, and the
 * queue the walk keeps is a sublist of the queue it was given.  §16.6 needed a
 * hypothesis for the same claim about wai_flg because there a NEW entry joined
 * the queue; here nothing enters. *)
Lemma a_set_step_always_keeps_the_shape : forall s c,
    flg_wf c = true -> flg_wf (fst (flg_set_step s c)) = true.
Proof.
  intros s c W.
  assert (E : fst (flg_set_step s c) =
      mk_flgcb (fc_id c) (fc_tpri c) (fc_wmul c) (fc_nodis c)
        (k_pat (flg_set_walk (pat_or (fc_pat c) s) (fc_wait c)))
        (k_kept (flg_set_walk (pat_or (fc_pat c) s) (fc_wait c)))) by reflexivity.
  rewrite E. rewrite flg_wf_of_a_cell.
  apply andb_true_iff. split.
  - apply (every_test_nonzero_of_a_subqueue (fc_wait c)).
    + intros x Hin. apply (the_walk_keeps_only_who_it_was_given (fc_wait c) _ x Hin).
    + apply (flg_wf_gives_positive_tests c W).
  - apply (every_mode_legal_of_a_subqueue (fc_wait c)).
    + intros x Hin. apply (the_walk_keeps_only_who_it_was_given (fc_wait c) _ x Hin).
    + apply (flg_wf_gives_legal_modes c W).
Qed.

(* The count form of the same fact: the queue can only shrink. *)
Lemma the_walk_keeps_at_most_what_it_was_given : forall q p,
    length (k_kept (flg_set_walk p q)) <= length q.
Proof.
  intros q p. assert (L : length q =
      length (k_kept (flg_set_walk p q)) + length (k_gone (flg_set_walk p q)))
    by (apply the_walk_never_loses_a_waiter).
  apply Nat.le_trans with (m := length (k_kept (flg_set_walk p q)) +
                                     length (k_gone (flg_set_walk p q))).
  - apply Nat.le_add_r.
  - rewrite <- L. apply Nat.le_refl.
Qed.

(* A walk that is given nobody to walk over: the set that finds an empty queue
 * is the union and nothing else, which is the ordinary case in shipped code and
 * the reason the C's loop body never runs for a single-waiter flag. *)
Example a_set_onto_an_empty_queue_releases_nobody :
    flg_set_walk 5 nil = mk_flg_walk 5 nil nil /\
    snd (flg_set_step 2 (mk_flgcb 1 false true false 5 nil)) = nil /\
    fc_pat (fst (flg_set_step 2 (mk_flgcb 1 false true false 5 nil))) = 7.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* TA_WMUL's price, computed.  The same two waiters and the same figure give the
 * same task different outcomes -- released, or still blocked -- when the queue
 * order changes, because a clearing mode consumes the bits the next waiter would
 * have tested and then stops the walk.  §14 could not show this: its walk spends
 * a count that no waiter can remove from a later waiter's identity, and the
 * semaphore's releasee learns nothing about the order it was met in. *)
Example the_release_order_is_observable :
    k_gone (flg_set_walk 3 [mk_flg_who 7 1 16 1; mk_flg_who 8 2 0 1])
    = [mk_flg_release (mk_flg_who 7 1 16 1) 3] /\
    k_kept (flg_set_walk 3 [mk_flg_who 7 1 16 1; mk_flg_who 8 2 0 1])
    = [mk_flg_who 8 2 0 1] /\
    k_pat (flg_set_walk 3 [mk_flg_who 7 1 16 1; mk_flg_who 8 2 0 1]) = 0.
Proof. repeat split; vm_compute; reflexivity. Qed.

Example the_same_queue_in_the_other_order :
    k_gone (flg_set_walk 3 [mk_flg_who 8 2 0 1; mk_flg_who 7 1 16 1])
    = [mk_flg_release (mk_flg_who 8 2 0 1) 3;
       mk_flg_release (mk_flg_who 7 1 16 1) 3] /\
    k_kept (flg_set_walk 3 [mk_flg_who 8 2 0 1; mk_flg_who 7 1 16 1]) = nil /\
    k_pat (flg_set_walk 3 [mk_flg_who 8 2 0 1; mk_flg_who 7 1 16 1]) = 0.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* The two walks above hand out one answer in one order and two in the other --
 * which is what an oracle has to reproduce, and the reason §16.9 records the
 * pair as a computation rather than asserting a general law about it. *)
Example the_released_answers_differ_by_order :
    map rl_pat (k_gone (flg_set_walk 3 [mk_flg_who 7 1 16 1; mk_flg_who 8 2 0 1]))
    = [3] /\
    map rl_pat (k_gone (flg_set_walk 3 [mk_flg_who 8 2 0 1; mk_flg_who 7 1 16 1]))
    = [3; 3].
Proof. repeat split; vm_compute; reflexivity. Qed.

(* ── 16.8 The other four entry points, as steps on the cell ─────── *)

(* cre_flg (:116-158), del_flg (:164-187), clr_flg (:250-268) and ref_flg
 * (:339-359) are the four services §16.6 and §16.7 left aside.  Three of them
 * are straight-line: a creation fills a cell in, a deletion takes it out, a
 * clear intersects the pattern with its argument.  The fourth, ref_flg, is the
 * only one that reads, and what it reads is exactly the pair a blocked caller
 * will eventually be answered with -- which is why an observer can watch a flag
 * without perturbing it.
 *
 * Like §14.6 and §14.7, these are the critical-section bodies only: the marker
 * test that guards each of them is §16.5's flg_object_guards, and the steps here
 * are reached exactly when that cascade returns None.
 *
 * §14 could project its cells onto §6's global table because that table has a
 * semaphore column.  It has no flag column, so this section stays cell-local:
 * flg_view and free_flg record what an observer of a flag can see, and the laws
 * below relate a cell to its own receipts rather than to a bus index. *)
Record flg : Type := mk_flg {
    f_id   : nat;             (* FLGCB.flgid, the stored marker *)
    f_pat  : nat;             (* FLGCB.flgptn, what a refer call reports *)
    f_wait : list nat         (* the waiting tasks, head first, ids only *)
  }.

Definition free_flg : flg := mk_flg 0 0 nil.

Definition flg_view (c : flgcb) : flg :=
  mk_flg (fc_id c) (fc_pat c) (map fw_tid (fc_wait c)).

Lemma view_keeps_the_flag_marker : forall c, f_id (flg_view c) = fc_id c.
Proof. intros c. reflexivity. Qed.

Lemma view_keeps_the_pattern : forall c, f_pat (flg_view c) = fc_pat c.
Proof. intros c. reflexivity. Qed.

Lemma map_fw_tid_length : forall q : list flg_who, length (map fw_tid q) = length q.
Proof.
  intros q. induction q as [|x q IH]; cbn [map length]; [reflexivity |].
  rewrite IH. reflexivity.
Qed.

Lemma view_keeps_the_flag_length : forall c,
    length (f_wait (flg_view c)) = length (fc_wait c).
Proof. intros c. unfold flg_view. apply map_fw_tid_length. Qed.

(* A flag exists for its readers exactly when its stored marker is non-zero --
 * the same reading as §14.3's for semaphores, and the reason all four services
 * here have one marker test and no other existence test at all. *)
Lemma flag_used_is_the_view_marker : forall c,
    flg_used c = negb (Nat.eqb (f_id (flg_view c)) 0).
Proof. intros c. unfold flg_used. rewrite view_keeps_the_flag_marker. reflexivity. Qed.

(* cre_flg, eventflag.c:139-154.  The cell it builds has the caller's attributes,
 * the caller's initial pattern and nobody in its queue, and its receipt is the
 * new id (:154) -- the only service of this family that returns a number rather
 * than an ER, which is why §16.5's cre cascade has no E_ID: the id is an output
 * here, not an input.  Note what is NOT tested: anything about iflgptn.  :147
 * copies it verbatim, so a created flag can already satisfy a wait. *)
Definition flg_created (id : nat) (tpri wmul nodis : bool) (initial : nat) : flgcb :=
  mk_flgcb id tpri wmul nodis initial nil.

Lemma a_created_cell_holds_nobody : forall i tp wm nd p,
    fc_wait (flg_created i tp wm nd p) = nil.
Proof. intros i tp wm nd p. unfold flg_created. reflexivity. Qed.

Lemma a_created_cell_keeps_the_initial_pattern : forall i tp wm nd p,
    fc_pat (flg_created i tp wm nd p) = p.
Proof. intros i tp wm nd p. reflexivity. Qed.

Lemma a_created_cell_keeps_the_three_attributes : forall i tp wm nd p,
    fc_tpri (flg_created i tp wm nd p) = tp /\
    fc_wmul (flg_created i tp wm nd p) = wm /\
    fc_nodis (flg_created i tp wm nd p) = nd.
Proof. intros i tp wm nd p. unfold flg_created. repeat split; reflexivity. Qed.

Lemma a_created_cell_is_wellformed : forall i tp wm nd p,
    flg_wf (flg_created i tp wm nd p) = true.
Proof.
  intros i tp wm nd p. unfold flg_created, flg_wf, every_test_nonzero, every_mode_legal.
  cbn [forallb andb]. reflexivity.
Qed.

(* A flag created with a pattern that already matches is satisfied before any
 * set: the wait takes its answer at :309-310 without queueing at all. *)
Example a_creation_can_leave_a_flag_already_satisfied :
    flg_cond (fc_pat (flg_created 1 false false false 6)) 2 0 = true /\
    flg_cond (fc_pat (flg_created 1 false false false 4)) 2 0 = false /\
    snd (flg_wai 0 TMO_REL (mk_flg_who 8 2 0 1) (flg_created 1 false false false 6))
    = mk_flg_reply (Some 6) E_OK.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* clr_flg, eventflag.c:263: the one line this service executes on the object.
 * Two facts fall out of reading it literally.  First it is a KEEP-mask --
 * flgptn &= clrptn retains the bits named -- which is the spec-versus-code
 * divergence §16.2 records and §16.9 computes.  Second there is no release walk
 * at all: the queue is untouched, so clearing bits never wakes the tasks that
 * were waiting for them.  A task waiting on a bit another task clears keeps
 * waiting; only a set can answer it, and the C's set walk is the sole place
 * where a release happens (:222-238). *)
Definition flg_clr_step (clrptn : nat) (c : flgcb) : flgcb :=
  flg_with_pattern (pat_and (fc_pat c) clrptn) c.

Lemma a_clear_step_never_grows_the_pattern : forall p c,
    Nat.leb (fc_pat (flg_clr_step p c)) (fc_pat c) = true.
Proof.
  intros p c. unfold flg_clr_step, flg_with_pattern. apply Nat.leb_le. apply Nat.land_le_l.
Qed.

Lemma a_clear_leaves_the_queue_alone : forall p c,
    fc_wait (flg_clr_step p c) = fc_wait c.
Proof. intros p c. unfold flg_clr_step, flg_with_pattern. reflexivity. Qed.

Lemma a_clear_keeps_the_marker : forall p c, fc_id (flg_clr_step p c) = fc_id c.
Proof. intros p c. unfold flg_clr_step, flg_with_pattern. reflexivity. Qed.

Lemma a_clear_keeps_the_shape : forall p c, flg_wf (flg_clr_step p c) = flg_wf c.
Proof. intros p c. unfold flg_clr_step, flg_with_pattern.
  apply a_clear_leaves_the_shape_alone. Qed.

(* What the mask does not name is dropped, and what it names survives: the two
 * halves of the keep-mask reading, with no hypothesis about the cell. *)
Lemma a_clear_leaves_a_subset_alone : forall clr c,
    Nat.eqb (pat_and (fc_pat c) clr) (fc_pat c) = true ->
    fc_pat (flg_clr_step clr c) = fc_pat c.
Proof.
  intros clr c E. unfold flg_clr_step, flg_with_pattern. cbn [fc_pat].
  apply Nat.eqb_eq in E. exact E.
Qed.

Lemma a_clear_of_a_disjoint_mask_is_the_zero : forall clr c,
    Nat.eqb (pat_and (fc_pat c) clr) 0 = true ->
    fc_pat (flg_clr_step clr c) = 0.
Proof.
  intros clr c E. unfold flg_clr_step, flg_with_pattern. apply Nat.eqb_eq. exact E.
Qed.

Lemma a_clear_is_idempotent : forall p c,
    flg_clr_step p (flg_clr_step p c) = flg_clr_step p c.
Proof.
  intros p c. unfold flg_clr_step, flg_with_pattern.
  destruct c as [i tp wm nd pt q]; cbn [fc_pat].
  rewrite <- Nat.land_assoc. rewrite Nat.land_diag. reflexivity.
Qed.

Lemma a_clear_of_the_pattern_itself_is_the_identity : forall c, flg_clr_step (fc_pat c) c = c.
Proof.
  intros c. unfold flg_clr_step, flg_with_pattern, pat_and.
  destruct c as [i tp wm nd pt q]; cbn [fc_id fc_tpri fc_wmul fc_nodis fc_pat fc_wait].
  rewrite Nat.land_diag. reflexivity.
Qed.

(* Setting then clearing is not clearing then setting: the two are ordered
 * operations on the pattern, and a set between two clears feeds the second
 * one's mask.  No commutativity law holds here, so the section records the
 * counterexample instead of a lemma. *)
Example the_set_and_the_clear_do_not_commute :
    fc_pat (flg_clr_step 5 (fst (flg_set_step 6 (flg_created 1 false false false 1)))) = 5 /\
    fc_pat (fst (flg_set_step 6 (flg_clr_step 5 (flg_created 1 false false false 1)))) = 7.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* ref_flg, eventflag.c:352-354, and the wait.c:139-146 helper it calls.  The
 * reference reports the head waiter's id, or 0 when nobody waits, and the
 * pattern as it stands.  Everything else about the queue -- how many wait, what
 * each of them named -- is invisible through this service, and through every
 * other one in 3.20: the only enumerator is the debugger call at :400. *)
Definition flg_head_tid (ids : list nat) : nat :=
  match ids with nil => 0 | t :: _ => t end.

Record flg_stat : Type := mk_flg_stat { fs_wtsk : nat; fs_pat : nat }.

Definition flg_ref (c : flgcb) : flg_stat :=
  mk_flg_stat (flg_head_tid (f_wait (flg_view c))) (fc_pat c).

Lemma a_reference_reports_the_stored_pattern : forall c, fs_pat (flg_ref c) = fc_pat c.
Proof. intros c. reflexivity. Qed.

Lemma an_empty_flag_reports_no_waiting_task : forall c,
    fc_wait c = nil -> fs_wtsk (flg_ref c) = 0.
Proof. intros c. unfold flg_ref, flg_view, f_wait. intros ->. reflexivity. Qed.

Lemma a_reference_names_the_head_waiter : forall x q,
    fs_wtsk (flg_ref (mk_flgcb 1 false false false 0 (x :: q))) = fw_tid x.
Proof. intros x q. unfold flg_ref. reflexivity. Qed.

(* The read is the promise: a blocked wait leaves the pattern where a reference
 * finds it (§16.6), and §16.7's walk answers with the pattern the union built.
 * So the figure a caller reads is the figure it will be given, as long as no
 * other call intervenes -- and the second conjunct below is the case where one
 * does, since the read of the EMPTY cell and the answer to the later set
 * differ by exactly the setptn. *)
Example a_reference_and_the_answer_it_promises :
    fs_pat (flg_ref (mk_flgcb 1 false true false 7 nil)) = 7 /\
    snd (flg_wai 0 TMO_REL (mk_flg_who 8 4 0 1) (mk_flgcb 1 false true false 7 nil))
    = mk_flg_reply (Some 7) E_OK /\
    fc_pat (fst (flg_set_step 8 (mk_flgcb 1 false true false 7 nil))) = 15.
Proof. repeat split; vm_compute; reflexivity. Qed.

Example a_reference_sees_only_the_head :
    fs_wtsk (flg_ref (mk_flgcb 1 false true false 0
                        [mk_flg_who 9 2 0 1; mk_flg_who 10 1 0 2])) = 9 /\
    f_wait (flg_view (mk_flgcb 1 false true false 0
                        [mk_flg_who 9 2 0 1; mk_flg_who 10 1 0 2])) = [9; 10].
Proof. repeat split; vm_compute; reflexivity. Qed.

(* del_flg, eventflag.c:176-182.  Two effects, in this order: wait_delete (:178)
 * releases every waiter with E_DLT, and then the cell goes back to the free list
 * with ONLY its marker cleared (:182).  The C does not reset flgptn or the
 * attributes, and that asymmetry is legal solely because every reader tests the
 * marker first (:174, :205, :260, :349) -- so flg_forget below clears fc_id and
 * the queue and deliberately keeps the stale pattern and all three attribute
 * bits, exactly as the kernel does.  A law that said otherwise would be a model
 * of tidiness, not of this code. *)
Definition flg_del_broadcast (c : flgcb) : list er :=
  map (fun _ => final_receipt RK_del (prewrite false)) (fc_wait c).

Lemma flg_del_broadcast_is_all_E_DLT : forall c,
    flg_del_broadcast c = repeat E_DLT (length (fc_wait c)).
Proof.
  intros c. unfold flg_del_broadcast.
  assert (A : forall l : list flg_who,
            map (fun _ => final_receipt RK_del (prewrite false)) l = repeat E_DLT (length l)).
  { induction l as [|x l IH]; cbn [map repeat length]; [reflexivity |].
    cbn [final_receipt effect_of ef_write prewrite] in *. rewrite IH. reflexivity. }
  apply A.
Qed.

Lemma the_delete_broadcast_has_one_receipt_per_waiter : forall c,
    length (flg_del_broadcast c) = length (fc_wait c).
Proof.
  intros c. unfold flg_del_broadcast.
  assert (A : forall l : list flg_who,
              length (map (fun _ => final_receipt RK_del (prewrite false)) l) = length l).
  { induction l as [|x l IH]; cbn [map length]; [reflexivity |]. rewrite IH. reflexivity. }
  apply A.
Qed.

Definition flg_forget (c : flgcb) : flgcb :=
  mk_flgcb 0 (fc_tpri c) (fc_wmul c) (fc_nodis c) (fc_pat c) nil.

Definition flg_del_step (c : flgcb) : flgcb * list er :=
  (flg_forget c, flg_del_broadcast c).

Lemma a_forgotten_flag_cell_reports_no_existence : forall c, flg_used (flg_forget c) = false.
Proof. intros c. unfold flg_used, flg_forget. reflexivity. Qed.

Lemma a_forgotten_flag_cell_holds_no_waiters : forall c, fc_wait (flg_forget c) = nil.
Proof. intros c. unfold flg_forget. reflexivity. Qed.

Lemma a_forgotten_cell_keeps_its_attributes : forall c,
    fc_tpri (flg_forget c) = fc_tpri c /\
    fc_wmul (flg_forget c) = fc_wmul c /\
    fc_nodis (flg_forget c) = fc_nodis c.
Proof. intros c. unfold flg_forget. repeat split; reflexivity. Qed.

Lemma a_forgotten_cell_keeps_the_stale_pattern : forall c,
    fc_pat (flg_forget c) = fc_pat c.
Proof. intros c. unfold flg_forget. reflexivity. Qed.

Lemma a_forgotten_cell_is_wellformed : forall c, flg_wf (flg_forget c) = true.
Proof.
  intros c. unfold flg_forget, flg_wf, every_test_nonzero, every_mode_legal.
  cbn [forallb andb]. reflexivity.
Qed.

Lemma a_forgotten_cell_views_as_the_free_cell : forall c p,
    f_pat (flg_view (flg_forget c)) = p -> flg_view (flg_forget c) =
    mk_flg 0 p nil.
Proof.
  intros c p H. unfold flg_forget, flg_view. cbn [fc_id fw_tid map].
  rewrite <- H. reflexivity.
Qed.

(* The two readings of the marker test, side by side: the guard refuses a
 * deletion of a cell whose marker is 0 (§16.5), while the step itself -- the C's
 * else-branch, reached only when the guard passes -- broadcasts one E_DLT per
 * waiter and returns the cell to the free list. *)
Example a_delete_asks_the_marker_before_it_asks_the_queue :
    first_bad (flg_object_guards false 3) = Some E_NOEXS /\
    fst (flg_del_step (mk_flgcb 3 false true false 5 [mk_flg_who 9 2 0 1]))
    = mk_flgcb 0 false true false 5 nil /\
    snd (flg_del_step (mk_flgcb 3 false true false 5 [mk_flg_who 9 2 0 1])) = [E_DLT].
Proof. repeat split; vm_compute; reflexivity. Qed.

(* The composition a driver actually relies on: a flag used as a one-shot.  The
 * waiter names TWF_CLR, so the set that answers it also empties the object --
 * no clr_flg call is needed to make the flag ready for the next round.  This is
 * §16.6's answer law and §16.7's stop law read together on shipped shapes. *)
Example a_one_shot_flag_round_trip :
    flg_wai 0 TMO_REL (mk_flg_who 9 2 16 1) (flg_created 1 false true false 0)
    = (mk_flgcb 1 false true false 0 [mk_flg_who 9 2 16 1], mk_flg_reply None E_TMOUT) /\
    fst (flg_set_step 2 (fst (flg_wai 0 TMO_REL (mk_flg_who 9 2 16 1)
                                                (flg_created 1 false true false 0))))
    = flg_created 1 false true false 0 /\
    snd (flg_set_step 2 (fst (flg_wai 0 TMO_REL (mk_flg_who 9 2 16 1)
                                                (flg_created 1 false true false 0))))
    = [mk_flg_release (mk_flg_who 9 2 16 1) 2].
Proof. repeat split; vm_compute; reflexivity. Qed.

(* ── 16.9 The family, computed ────────────────────────────────── *)

(* Every Example in this subsection is a FIXTURE, not an illustration: the
 * executable oracle in verify/models/tron_model.ml has to reproduce each of
 * these figures from its own independently written code, and the harness (§15's
 * verify_models.sh) compares the two.  A law says the model agrees with itself;
 * a fixture says the model agrees with the C, provided the fixture was read off
 * the C -- which the comment above each one records.  Where a fixture disagrees
 * with the shipped kernel it is the kernel that is right, so the readings here
 * are line-by-line against src/kernel/eventflag.c. *)

(* eventflag_cond, :86-93: any-of versus all-of, on figures where the two differ
 * and on one where they agree.  (6&5)=4 is non-zero but not 5, so the pair
 * (6,5) separates the readings; (7,5) satisfies both. *)
Example fixture_the_two_wait_modes_separate_on_one_pair :
    flg_cond 6 4 1 = true /\
    flg_cond 6 5 1 = true /\
    flg_cond 6 5 0 = false /\
    flg_cond 7 5 0 = true /\
    flg_cond 2 3 1 = true.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* CHECK_PAR(wfmode & ~(TWF_ORW|TWF_CLR|TWF_BITCLR)) == 0, :285.  The legal set
 * below 128 is the eight combinations of the three named bits; every other
 * figure in range is refused.  The oracle's own parameter check should reproduce
 * this table, and it should refuse 79 for the reason 78 gives (bit 1 and bit 2
 * are named by nothing). *)
Example fixture_every_legal_mode_word_passes :
    wfmode_ok 0 = true /\ wfmode_ok 1 = true /\ wfmode_ok 16 = true /\
    wfmode_ok 17 = true /\ wfmode_ok 32 = true /\ wfmode_ok 33 = true /\
    wfmode_ok 48 = true /\ wfmode_ok 49 = true.
Proof. repeat split; vm_compute; reflexivity. Qed.

Example fixture_an_unnamed_bit_in_range_is_refused :
    wfmode_ok 2 = false /\ wfmode_ok 8 = false /\ wfmode_ok 64 = false /\
    wfmode_ok 78 = false /\ wfmode_ok 79 = false.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* §16.2's note on the ORW bit read as a bit rather than as a whole word: both
 * readings agree on 17, which is the combination the reference names ORW-and-CLR.
 * The condition is any-of, the clear is a full wipe -- the answer is the pattern
 * the caller found, not the pattern the object keeps. *)
Example fixture_orw_combined_with_clr_is_an_any_of_wait_that_wipes :
    orw_mode 17 = true /\
    flg_cond 6 5 17 = true /\
    flg_cond 6 5 16 = false /\
    flg_wai 0 TMO_REL (mk_flg_who 8 4 17 1) (mk_flgcb 1 false false false 7 nil)
    = (mk_flgcb 1 false false false 0 nil, mk_flg_reply (Some 7) E_OK).
Proof. repeat split; vm_compute; reflexivity. Qed.

(* :310 writes the answer BEFORE :313-318 clear, and the two clear modes are
 * independent: BITCLR drops only the named bits, CLR drops everything, neither
 * drops anything.  One figure and one test, five mode words, five cell
 * patterns, ONE answer.  This is the single most misread pair in the family --
 * the releasee's figure and the object's figure are different observables. *)
Example fixture_the_clear_modes_price_the_object_not_the_answer :
    snd (flg_wai 0 TMO_REL (mk_flg_who 8 4 0 1) (mk_flgcb 1 false false false 7 nil))
    = mk_flg_reply (Some 7) E_OK /\
    fc_pat (fst (flg_wai 0 TMO_REL (mk_flg_who 8 4 0 1) (mk_flgcb 1 false false false 7 nil))) = 7 /\
    fc_pat (fst (flg_wai 0 TMO_REL (mk_flg_who 8 4 1 1) (mk_flgcb 1 false false false 7 nil))) = 7 /\
    fc_pat (fst (flg_wai 0 TMO_REL (mk_flg_who 8 4 32 1) (mk_flgcb 1 false false false 7 nil))) = 3 /\
    fc_pat (fst (flg_wai 0 TMO_REL (mk_flg_who 8 4 16 1) (mk_flgcb 1 false false false 7 nil))) = 0.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* The set walk, :214-239, on three waiters that all name TWF_BITCLR.  Each
 * releaseee is answered with the pattern as the walk found it for THAT waiter:
 * 7, then 7-1=6, then 6-2=4, and the object ends empty.  The answer list is
 * therefore not the union and not the final pattern; it is a trace.  §16.2's
 * note on absorption is what makes this the honest statement -- the model can
 * say what each released task received, and says nothing about what a task that
 * STAYED would have received. *)
Example fixture_a_bitclr_chain_answers_with_a_trace :
    map rl_pat (k_gone (flg_set_walk 7 [mk_flg_who 9 1 32 1; mk_flg_who 10 2 32 1;
                                        mk_flg_who 11 4 32 1])) = [7; 6; 4] /\
    k_pat (flg_set_walk 7 [mk_flg_who 9 1 32 1; mk_flg_who 10 2 32 1;
                           mk_flg_who 11 4 32 1]) = 0 /\
    k_kept (flg_set_walk 7 [mk_flg_who 9 1 32 1; mk_flg_who 10 2 32 1;
                            mk_flg_who 11 4 32 1]) = nil.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* The same walk with the FIRST waiter on TWF_BITCLR and the second on an
 * ordinary all-of wait: the first drop removes the bit the second was waiting
 * for, so the second is not answered and stays queued -- and the pattern the
 * object keeps, 2, is the figure a later reference call reports.  A driver that
 * assumed "everyone whose bits are set wakes" is wrong here. *)
Example fixture_a_bitclr_head_can_starve_the_waiter_behind_it :
    flg_set_walk 3 [mk_flg_who 9 1 32 1; mk_flg_who 10 1 0 1]
    = mk_flg_walk 2 [mk_flg_who 10 1 0 1] [mk_flg_release (mk_flg_who 9 1 32 1) 3].
Proof. vm_compute. reflexivity. Qed.

(* :211 unions setptn into the pattern and only THEN walks, so a set of 0 is not
 * a no-op: it re-runs the test against whatever the object already held.  This
 * is why the family has no CHECK_PAR on setptn -- eventflag.c:192-245 checks
 * only the id -- and an oracle that skips the walk on setptn == 0 diverges. *)
Example fixture_a_set_of_zero_still_walks_the_queue :
    fst (flg_set_step 0 (mk_flgcb 1 false true false 7 [mk_flg_who 9 1 0 1]))
    = mk_flgcb 1 false true false 7 nil /\
    snd (flg_set_step 0 (mk_flgcb 1 false true false 7 [mk_flg_who 9 1 0 1]))
    = [mk_flg_release (mk_flg_who 9 1 0 1) 7].
Proof. repeat split; vm_compute; reflexivity. Qed.

(* With a union, the release figure is the union: 3|4 = 7 is what the answered
 * task receives, and the object keeps 7 because that waiter cleared nothing. *)
Example fixture_the_union_is_what_the_released_task_reads :
    fst (flg_set_step 4 (mk_flgcb 1 false true false 3 [mk_flg_who 9 1 0 1]))
    = mk_flgcb 1 false true false 7 nil /\
    snd (flg_set_step 4 (mk_flgcb 1 false true false 3 [mk_flg_who 9 1 0 1]))
    = [mk_flg_release (mk_flg_who 9 1 0 1) 7].
Proof. repeat split; vm_compute; reflexivity. Qed.

(* The queue order TA_TPRI decides, :321 and wait.c:196-207: a lower number is
 * the higher priority and takes the earlier place, and a tie defers to the
 * incumbent -- so the insertion goes BETWEEN the 3 and the 7, not at the end. *)
Example fixture_tpri_inserts_by_priority_not_by_arrival :
    flg_insert_tpri (mk_flg_who 8 2 0 5)
      [mk_flg_who 9 2 0 3; mk_flg_who 10 1 0 7]
    = [mk_flg_who 9 2 0 3; mk_flg_who 8 2 0 5; mk_flg_who 10 1 0 7].
Proof. vm_compute. reflexivity. Qed.

(* The seven wai receipts, each isolated by making exactly one guard fail --
 * :283, :284, :285, :287, the marker at :289, :296, :303.  The ORDER is the
 * fixture: an oracle that tests the dispatch context after the marker reports
 * E_NOEXS where the kernel reports E_CTX, and the difference is observable to
 * any caller that passes a bad id to a disabled dispatcher.  st0 is §6's
 * dispatch-disabled state, st_disp the enabled one. *)
Example fixture_the_wai_cascade_in_the_kernels_order :
    first_bad (flg_wai_guards st_disp true false false 0 nil 17 2 0) = Some E_ID /\
    first_bad (flg_wai_guards st_disp true false false 0 nil 1 0 0) = Some E_PAR /\
    first_bad (flg_wai_guards st_disp true false false 0 nil 1 2 79) = Some E_PAR /\
    first_bad (flg_wai_guards st0 true false false 0 nil 1 2 0) = Some E_CTX /\
    first_bad (flg_wai_guards st_disp false false false 0 nil 1 2 0) = Some E_NOEXS /\
    first_bad (flg_wai_guards st_disp true false false 0 [mk_flg_who 9 2 0 1] 1 2 0)
      = Some E_OBJ /\
    first_bad (flg_wai_guards st_disp true false false ttw_flg nil 1 2 0) = Some E_DISWAI.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* The other five services: cre has only the table's own exhaustion (:138) and
 * no id test at all, because the id is its output (:154); set, clr, del and ref
 * share the two-test cascade of §16.5. *)
Example fixture_the_creation_and_the_object_services :
    first_bad (flg_cre_guards false) = Some E_LIMIT /\
    first_bad (flg_cre_guards true) = None /\
    first_bad (flg_object_guards true 16) = None /\
    first_bad (flg_object_guards true 17) = Some E_ID /\
    first_bad (flg_object_guards false 1) = Some E_NOEXS.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* The wait-disable pair, wait.h:128-132 through §12's mask vocabulary: the
 * task's own bit (ttw_flg) refuses the wait, and the object's TA_NODISWAI bit
 * overrides the refusal -- the second coordinate is the object's, which is why
 * a driver can make a flag waitable from inside a masked region. *)
Example fixture_the_wait_disable_needs_both_coordinates :
    masked_for 0 WO_FLG = false /\
    masked_for ttw_flg WO_FLG = true /\
    diswai_of true false = true /\
    diswai_of true true = false /\
    diswai_of false false = false /\
    snd (flg_wai ttw_flg TMO_FEVR (mk_flg_who 8 8 0 1) (mk_flgcb 1 false true false 7 nil))
    = mk_flg_reply None E_DISWAI /\
    snd (flg_wai ttw_flg TMO_FEVR (mk_flg_who 8 4 0 1) (mk_flgcb 1 false true true 7 nil))
    = mk_flg_reply (Some 7) E_OK.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* A poll and a block take the same test and differ only in the queue (:321-327
 * versus :328): the figure the refused poll returns is the figure the blocked
 * call will be given LATER, so both are None here -- §15's reply law is what
 * this fixture is checking against. *)
Example fixture_a_poll_and_a_block_differ_only_in_the_queue :
    flg_wai 0 TMO_POLL (mk_flg_who 8 8 0 1) (mk_flgcb 1 false true false 7 nil)
    = (mk_flgcb 1 false true false 7 nil, mk_flg_reply None E_TMOUT) /\
    fc_wait (fst (flg_wai 0 TMO_REL (mk_flg_who 8 8 0 1) (mk_flgcb 1 false true false 7 nil)))
    = [mk_flg_who 8 8 0 1] /\
    snd (flg_wai 0 TMO_REL (mk_flg_who 8 8 0 1) (mk_flgcb 1 false true false 7 nil))
    = mk_flg_reply None E_TMOUT.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* clr_flg:263 as the keep-mask, computed on the figure §16.2 uses for the spec
 * divergence.  11 & 3 keeps 3; the specification text's reading (an AND with the
 * inverted mask) would give 8.  An oracle that follows b-spec reproduces 8 and
 * fails this fixture; an oracle that follows the code reproduces 3. *)
Example fixture_the_clear_is_a_keep_mask :
    fc_pat (flg_clr_step 3 (mk_flgcb 1 false false false 11 nil)) = 3 /\
    pat_and 11 3 = 3 /\
    pat_clr 11 3 = 8 /\
    fc_wait (flg_clr_step 3 (mk_flgcb 1 false false false 11 [mk_flg_who 9 1 0 1]))
    = [mk_flg_who 9 1 0 1].
Proof. repeat split; vm_compute; reflexivity. Qed.

(* ref_flg:352-354 with wait.c:139-146, and del_flg's broadcast (:178) -- the
 * two read-only and destructive bookends of the family.  The reference reports
 * the head, 9, and the stored pattern; the delete hands both waiters E_DLT and
 * leaves the cell's pattern stale at 5. *)
Example fixture_the_reference_and_the_deletion :
    flg_ref (mk_flgcb 1 false true false 5 [mk_flg_who 9 2 0 3; mk_flg_who 10 1 0 2])
    = mk_flg_stat 9 5 /\
    flg_del_broadcast (mk_flgcb 3 false true false 5
                         [mk_flg_who 9 2 0 1; mk_flg_who 10 1 0 2]) = [E_DLT; E_DLT] /\
    flg_forget (mk_flgcb 3 false true false 5 [mk_flg_who 9 2 0 1])
    = mk_flgcb 0 false true false 5 nil.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* ── 16.10 What this family adds, and what it does not claim ───── *)

(* TA_WMUL, and the reason this section is the third one rather than an
 * appendix to §14.  Three attributes drive three separate decisions in this
 * family: TA_TPRI chooses the insertion point (:321, and the reinsertion hook at
 * :98-104), TA_WMUL decides whether a second waiter is a queueing event or an
 * E_OBJ refusal (:296), TA_NODISWAI decides whether the task's own wait mask can
 * refuse the call (:303).  The semaphore family has all three as WELL, but the
 * flag's queue carries a PATTERN in each entry, and a pattern is consumable:
 *
 *   §14's walk conserves units -- count = what stays + what was taken -- and no
 *   waiter's identity is altered by the release of another, so the semaphore's
 *   releasee learns nothing about the order it was met in.
 *   §16's walk consumes a figure.  The queue order is then observable twice
 *   over: in WHO is answered (§16.7's pair of Examples) and in what each
 *   answered task READS (§16.9's BITCLR trace, [7;6;4] from one union of 7).
 *   A model that released flags in the order-independent style of §14 would
 *   reproduce neither.
 *
 * Divergences this section records rather than repairs, in one place:
 *   1. clr_flg (:263) ANDs the argument as a KEEP-mask; b-spec/os_spec/kernel/
 *      taskcomm.html:341-368 describes it as an AND with the INVERTED mask.  The
 *      model follows the code; fixture_the_clear_is_a_keep_mask computes both.
 *   2. CHECK_PAR on wfmode (:285) complements over the whole UINT, the model's
 *      wfmode_mask stops at bit 7, so 128 and above pass here and are refused by
 *      the kernel.  Materialising 2^32 unary is what costs, and §16.1's in_word
 *      keeps the width as a Prop instead.
 *   3. E_RSATR is in no cascade of this file, as in §12: the cell records only
 *      the three bits cre_flg accepts (:119-126), so an illegal attribute is not
 *      a state the model can name.
 *   4. Deleting a flag whose cell does not exist reports E_NOEXS.  §13's mailbox
 *      deletion reports E_OBJ once somebody is waiting.  Both are faithful to
 *      their own C; the families simply differ.
 *   5. clr_flg has no release walk.  A task waiting on a bit another task clears
 *      keeps waiting, and nothing in the model wakes it -- matching :250-268.
 *   6. §6's global state has no flag column, so every law here is cell-local:
 *      there is no bus_f, no flag-index lemma, and no cross-object statement.
 *      §14 could state those for semaphores; this family cannot, and the
 *      difference is in the model's tables, not in the kernel.
 *   7. The pointer UINT *p_flgptn is modelled as an option field in the reply,
 *      not as memory: Some p means the slot was written during this call, None
 *      covers both a refusal and a block whose write has not happened yet.
 *
 * Not modelled at all, and claimed nowhere: TA_DSNAME and the exinf field
 * (:141, :145), the debugger services _td_lst_flg (:400) and _td_ref_flg (:424),
 * the priority-change hook flg_chg_pri (:98-104) that reinserts a queued waiter
 * when its task's priority moves, the timer expiry that turns a blocked wait into
 * E_TMOUT on its own, and relwai's multi-object release.  Each is a live part of
 * the shipped kernel; none of them changes a receipt in §16.5-§16.8, and the
 * absence is stated so that a reader does not infer coverage from silence.
 *
 * The executable partner is verify/models/tron_model.ml, which reimplements
 * §16.1's bit tests, §16.5's cascades, §16.6's wait and §16.7's walk without
 * reference to this file, and is checked against the fixtures above by the same
 * harness that runs §12-§15. *)
