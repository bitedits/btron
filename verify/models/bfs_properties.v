(* bfs_properties.v
 *
 * High-assurance formal model of B-System Clean-Room Volume V2 journal.
 * Mirrors bfs_model.ml (pure functional fragment: generation / dirty / log).
 *
 * Assurance style: safety + recovery properties suitable for audit
 * (NASA/NSA-style: total recovery, no durability of uncommitted work,
 *  monotonic generation, invariant preservation, inductive traces).
 *
 * Build (Rocq >= 9.0):
 *   coqc bfs_properties.v
 *
 * Oracle:
 *   ocamlc -o bfs_model bfs_model.ml && ./bfs_model
 *   bash verify_models.sh
 *)

From Stdlib Require Import List.
From Stdlib Require Import Arith.
From Stdlib Require Import Bool.
From Stdlib Require Import Lia.
Import ListNotations.

(* ═══════════════════════════════════════════════════════════════════
   §1  Core types
   ═══════════════════════════════════════════════════════════════════ *)

Definition fid := nat.
Definition rec_idx := nat.
Definition vec_id := nat.
Definition generation := nat.
Definition name_id := nat.

Inductive mutation : Type :=
| FidAlloc : fid -> name_id -> mutation
| FidFree : fid -> mutation
| SetRecord : fid -> rec_idx -> mutation
| DelRecord : fid -> rec_idx -> mutation
| IndexInsert : vec_id -> fid -> rec_idx -> mutation
| IndexRemove : vec_id -> mutation.

Record journal : Type := mkJournal {
  log : list mutation;
  committed : list mutation;
  in_tx : bool
}.

Definition empty_journal : journal :=
  {| log := []; committed := []; in_tx := false |}.

Record volume : Type := mkVolume {
  gen : generation;
  dirty : bool;
  j : journal
}.

Definition initial_volume : volume :=
  {| gen := 0; dirty := false; j := empty_journal |}.

(* ═══════════════════════════════════════════════════════════════════
   §2  Operations
   ═══════════════════════════════════════════════════════════════════ *)

Definition begin_tx (v : volume) : option volume :=
  if (j v).(in_tx) then None
  else Some {| gen := v.(gen);
               dirty := true;
               j := {| log := [];
                       committed := (j v).(committed);
                       in_tx := true |} |}.

Definition log_mut (v : volume) (m : mutation) : option volume :=
  if negb (j v).(in_tx) then None
  else Some {| gen := v.(gen);
               dirty := v.(dirty);
               j := {| log := m :: (j v).(log);
                       committed := (j v).(committed);
                       in_tx := true |} |}.

Definition commit (v : volume) : option volume :=
  if negb (j v).(in_tx) then None
  else
    let muts := List.rev (j v).(log) in
    Some {| gen := S v.(gen);
            dirty := false;
            j := {| log := [];
                    committed := (j v).(committed) ++ muts;
                    in_tx := false |} |}.

Definition abort (v : volume) : option volume :=
  if negb (j v).(in_tx) then None
  else Some {| gen := v.(gen);
               dirty := false;
               j := {| log := [];
                       committed := (j v).(committed);
                       in_tx := false |} |}.

Definition crash (v : volume) : volume :=
  {| gen := v.(gen);
     dirty := true;
     j := {| log := [];
             committed := (j v).(committed);
             in_tx := false |} |}.

Definition replay (v : volume) : volume :=
  {| gen := v.(gen);
     dirty := false;
     j := {| log := [];
             committed := (j v).(committed);
             in_tx := false |} |}.

(* ═══════════════════════════════════════════════════════════════════
   §3  Well-formedness (NASA-style invariant)
   ═══════════════════════════════════════════════════════════════════ *)

(* A volume is well-formed when:
   - if not in a transaction, the in-flight log is empty
   - generation is arbitrary nat (always true)
   This is the core structural invariant maintained by all ops. *)

Definition well_formed (v : volume) : Prop :=
  (j v).(in_tx) = false -> (j v).(log) = [].

Definition is_clean (v : volume) : Prop :=
  dirty v = false /\ (j v).(in_tx) = false /\ (j v).(log) = [].

Definition only_committed (v : volume) : Prop :=
  (j v).(log) = [] /\ (j v).(in_tx) = false.

Lemma initial_well_formed : well_formed initial_volume.
Proof. unfold well_formed, initial_volume, empty_journal. simpl. intros. reflexivity. Qed.

Lemma initial_clean : is_clean initial_volume.
Proof. unfold is_clean, initial_volume, empty_journal. simpl. auto. Qed.

(* ═══════════════════════════════════════════════════════════════════
   §4  Basic operation safety (existing + strengthened)
   ═══════════════════════════════════════════════════════════════════ *)

Theorem commit_clears_inflight :
  forall v v',
    commit v = Some v' ->
    (j v').(in_tx) = false /\ (j v').(log) = [] /\ dirty v' = false.
Proof.
  intros v v' H. unfold commit in H.
  destruct (j v).(in_tx) eqn:E; simpl in H.
  - inversion H; subst; simpl; auto.
  - discriminate.
Qed.

Theorem crash_drops_inflight :
  forall v, (j (crash v)).(log) = [] /\ (j (crash v)).(in_tx) = false.
Proof. intros v. simpl. auto. Qed.

Theorem crash_preserves_committed :
  forall v, (j (crash v)).(committed) = (j v).(committed).
Proof. intros v. reflexivity. Qed.

Theorem crash_preserves_generation :
  forall v, gen (crash v) = gen v.
Proof. intros v. reflexivity. Qed.

Theorem replay_after_crash_clean :
  forall v,
    let v' := replay (crash v) in
    dirty v' = false /\
    (j v').(in_tx) = false /\
    (j v').(log) = [] /\
    (j v').(committed) = (j v).(committed).
Proof. intros v. simpl. auto. Qed.

Theorem generation_monotonic_on_commit :
  forall v v', commit v = Some v' -> gen v' = S (gen v).
Proof.
  intros v v' H. unfold commit in H.
  destruct (j v).(in_tx); simpl in H.
  - inversion H; subst; reflexivity.
  - discriminate.
Qed.

Lemma begin_tx_sets_in_tx :
  forall v v',
    begin_tx v = Some v' ->
    (j v').(in_tx) = true /\ (j v').(log) = [] /\ dirty v' = true.
Proof.
  intros v v' H. unfold begin_tx in H.
  destruct (j v).(in_tx); simpl in H.
  - discriminate.
  - inversion H; subst; simpl; auto.
Qed.

Theorem commit_extends_committed :
  forall v v' muts,
    (j v).(in_tx) = true ->
    (j v).(log) = muts ->
    commit v = Some v' ->
    (j v').(committed) = (j v).(committed) ++ List.rev muts.
Proof.
  intros v v' muts Hin Hlog Hcom.
  unfold commit in Hcom. rewrite Hin in Hcom. simpl in Hcom.
  assert (HEq : v' = {| gen := S (gen v); dirty := false;
                        j := {| log := [];
                                committed := (j v).(committed) ++ List.rev (j v).(log);
                                in_tx := false |} |})
    by (inversion Hcom; reflexivity).
  rewrite HEq. simpl. rewrite Hlog. reflexivity.
Qed.

Theorem abort_discards_inflight :
  forall v v',
    abort v = Some v' ->
    (j v').(log) = [] /\
    (j v').(in_tx) = false /\
    dirty v' = false /\
    (j v').(committed) = (j v).(committed) /\
    gen v' = gen v.
Proof.
  intros v v' H. unfold abort in H.
  destruct (j v).(in_tx); simpl in H.
  - inversion H; subst; simpl; auto.
  - discriminate.
Qed.

Theorem replay_idempotent :
  forall v, only_committed v -> replay (replay v) = replay v.
Proof. intros v [Hlog Hin]. unfold replay. simpl. reflexivity. Qed.

Theorem crash_replay_safety :
  forall v,
    is_clean (replay (crash v)) /\
    (j (replay (crash v))).(committed) = (j v).(committed).
Proof. intros v. unfold is_clean. simpl. repeat split; auto. Qed.

(* ═══════════════════════════════════════════════════════════════════
   §5  High-assurance: precondition discipline
       (operations refuse illegal states — total partial functions)
   ═══════════════════════════════════════════════════════════════════ *)

(** begin_tx is refused while already in a transaction *)
Theorem begin_tx_refuses_nested :
  forall v, (j v).(in_tx) = true -> begin_tx v = None.
Proof.
  intros v H. unfold begin_tx. rewrite H. reflexivity.
Qed.

(** log_mut is refused outside a transaction *)
Theorem log_mut_requires_tx :
  forall v m, (j v).(in_tx) = false -> log_mut v m = None.
Proof.
  intros v m H. unfold log_mut. rewrite H. simpl. reflexivity.
Qed.

(** commit is refused outside a transaction *)
Theorem commit_requires_tx :
  forall v, (j v).(in_tx) = false -> commit v = None.
Proof.
  intros v H. unfold commit. rewrite H. simpl. reflexivity.
Qed.

(** abort is refused outside a transaction *)
Theorem abort_requires_tx :
  forall v, (j v).(in_tx) = false -> abort v = None.
Proof.
  intros v H. unfold abort. rewrite H. simpl. reflexivity.
Qed.

(* ═══════════════════════════════════════════════════════════════════
   §6  High-assurance: durability of committed work only
   ═══════════════════════════════════════════════════════════════════ *)

(** Uncommitted log is never part of the durable committed history
    after crash — the fundamental durability property. *)
Theorem durable_state_after_crash :
  forall v,
    (j (crash v)).(committed) = (j v).(committed) /\
    (j (crash v)).(log) = [].
Proof. intros v. simpl. auto. Qed.

(** Abort and crash agree on durable history and generation *)
Theorem abort_crash_same_durable :
  forall v v_ab,
    abort v = Some v_ab ->
    (j v_ab).(committed) = (j (crash v)).(committed) /\
    gen v_ab = gen (crash v).
Proof.
  intros v v_ab H.
  unfold abort in H.
  destruct (j v).(in_tx); simpl in H.
  - inversion H; subst; simpl; auto.
  - discriminate.
Qed.

(** After recovery, only committed mutations remain observable
    in the abstract log model. *)
Theorem recovery_exposes_only_committed :
  forall v,
    (j (replay (crash v))).(log) = [] /\
    (j (replay (crash v))).(committed) = (j v).(committed) /\
    (j (replay (crash v))).(in_tx) = false /\
    dirty (replay (crash v)) = false.
Proof. intros v. simpl. auto. Qed.

(* ═══════════════════════════════════════════════════════════════════
   §7  High-assurance: well-formedness preservation
   ═══════════════════════════════════════════════════════════════════ *)

Theorem begin_tx_preserves_wf :
  forall v v', begin_tx v = Some v' -> well_formed v'.
Proof.
  intros v v' H. unfold begin_tx in H.
  destruct (j v).(in_tx); simpl in H; try discriminate.
  inversion H; subst. unfold well_formed. simpl. intros Hfalse. discriminate.
Qed.

Theorem log_mut_preserves_wf :
  forall v m v', log_mut v m = Some v' -> well_formed v'.
Proof.
  intros v m v' H. unfold log_mut in H.
  destruct (j v).(in_tx) eqn:E; simpl in H; try discriminate.
  inversion H; subst. unfold well_formed. simpl. intros Hfalse. discriminate.
Qed.

Theorem commit_preserves_wf :
  forall v v', commit v = Some v' -> well_formed v'.
Proof.
  intros v v' H. unfold commit in H.
  destruct (j v).(in_tx); simpl in H; try discriminate.
  inversion H; subst. unfold well_formed. simpl. intros _. reflexivity.
Qed.

Theorem abort_preserves_wf :
  forall v v', abort v = Some v' -> well_formed v'.
Proof.
  intros v v' H. unfold abort in H.
  destruct (j v).(in_tx); simpl in H; try discriminate.
  inversion H; subst. unfold well_formed. simpl. intros _. reflexivity.
Qed.

Theorem crash_preserves_wf :
  forall v, well_formed (crash v).
Proof.
  intros v. unfold well_formed, crash. simpl. intros _. reflexivity.
Qed.

Theorem replay_preserves_wf :
  forall v, well_formed (replay v).
Proof.
  intros v. unfold well_formed, replay. simpl. intros _. reflexivity.
Qed.

(* ═══════════════════════════════════════════════════════════════════
   §8  High-assurance: recovery is total and deterministic
   ═══════════════════════════════════════════════════════════════════ *)

(** Recovery always succeeds (total function) and is deterministic. *)
Theorem recovery_total :
  forall v, exists v', v' = replay (crash v).
Proof. intros v. exists (replay (crash v)). reflexivity. Qed.

Theorem recovery_deterministic :
  forall v v1 v2,
    v1 = replay (crash v) ->
    v2 = replay (crash v) ->
    v1 = v2.
Proof. intros v v1 v2 H1 H2. rewrite H1, H2. reflexivity. Qed.

(** Double recovery is stable (idempotent recovery). *)
Theorem recovery_idempotent :
  forall v,
    replay (crash (replay (crash v))) = replay (crash v).
Proof. intros v. reflexivity. Qed.

(** Clean volumes already satisfy recovery postconditions. *)
Theorem clean_implies_recovery_shape :
  forall v,
    is_clean v ->
    is_clean (replay (crash v)) /\
    (j (replay (crash v))).(committed) = (j v).(committed) /\
    gen (replay (crash v)) = gen v.
Proof.
  intros v [Hd [Hin Hlog]].
  unfold is_clean. simpl. repeat split; auto.
Qed.

(* ═══════════════════════════════════════════════════════════════════
   §9  High-assurance: inductive traces
   ═══════════════════════════════════════════════════════════════════ *)

Inductive step : volume -> volume -> Prop :=
| S_begin : forall v v', begin_tx v = Some v' -> step v v'
| S_log : forall v m v', log_mut v m = Some v' -> step v v'
| S_commit : forall v v', commit v = Some v' -> step v v'
| S_abort : forall v v', abort v = Some v' -> step v v'
| S_crash : forall v, step v (crash v)
| S_replay : forall v, step v (replay v).

Inductive steps : volume -> volume -> Prop :=
| steps_refl : forall v, steps v v
| steps_trans : forall v v' v'', step v v' -> steps v' v'' -> steps v v''.

(** Well-formedness is an inductive invariant of the transition system. *)
Theorem step_preserves_wf :
  forall v v', well_formed v -> step v v' -> well_formed v'.
Proof.
  intros v v' Hwf Hstep.
  inversion Hstep; subst.
  - eapply begin_tx_preserves_wf; eauto.
  - eapply log_mut_preserves_wf; eauto.
  - eapply commit_preserves_wf; eauto.
  - eapply abort_preserves_wf; eauto.
  - apply crash_preserves_wf.
  - apply replay_preserves_wf.
Qed.

Theorem steps_preserve_wf :
  forall v v', well_formed v -> steps v v' -> well_formed v'.
Proof.
  intros v v' Hwf Hs.
  induction Hs.
  - exact Hwf.
  - apply IHHs. eapply step_preserves_wf; eauto.
Qed.

(** Committed history only grows or stays on successful commit;
    crash/abort/replay never drop committed entries. *)
Theorem crash_committed_prefix :
  forall v,
    (j (crash v)).(committed) = (j v).(committed).
Proof. intros. apply crash_preserves_committed. Qed.

Theorem abort_committed_unchanged :
  forall v v',
    abort v = Some v' ->
    (j v').(committed) = (j v).(committed).
Proof.
  intros v v' H. unfold abort in H.
  destruct (j v).(in_tx); simpl in H; try discriminate.
  inversion H; subst; simpl; reflexivity.
Qed.

(** Generation never decreases under any single step. *)
Theorem step_generation_ge :
  forall v v', step v v' -> gen v' >= gen v.
Proof.
  intros v v' Hstep.
  inversion Hstep; subst; simpl; try lia.
  - (* begin *) unfold begin_tx in H.
    destruct (j v).(in_tx); simpl in H; try discriminate.
    inversion H; subst; simpl; lia.
  - (* log *) unfold log_mut in H.
    destruct (j v).(in_tx); simpl in H; try discriminate.
    inversion H; subst; simpl; lia.
  - (* commit *)
    pose proof (generation_monotonic_on_commit v v' H) as Hg.
    rewrite Hg. lia.
  - (* abort *) unfold abort in H.
    destruct (j v).(in_tx); simpl in H; try discriminate.
    inversion H; subst; simpl; lia.
Qed.

(* ═══════════════════════════════════════════════════════════════════
   §10  High-assurance: crash during an open transaction
   ═══════════════════════════════════════════════════════════════════ *)

(** If a crash occurs mid-transaction, recovery restores the pre-tx
    durable history (committed prefix) and a clean state. *)
Theorem crash_during_tx_recovery :
  forall v v_tx,
    begin_tx v = Some v_tx ->
    is_clean (replay (crash v_tx)) /\
    (j (replay (crash v_tx))).(committed) = (j v).(committed) /\
    gen (replay (crash v_tx)) = gen v.
Proof.
  intros v v_tx Hbeg.
  unfold begin_tx in Hbeg.
  destruct (j v).(in_tx); simpl in Hbeg; try discriminate.
  inversion Hbeg; subst. simpl.
  unfold is_clean. simpl. repeat split; auto.
Qed.

(** Logging then crashing does not publish those logs. *)
Theorem log_then_crash_not_durable :
  forall v v_tx v_log m,
    begin_tx v = Some v_tx ->
    log_mut v_tx m = Some v_log ->
    (j (replay (crash v_log))).(committed) = (j v).(committed) /\
    (j (replay (crash v_log))).(log) = [].
Proof.
  intros v v_tx v_log m Hb Hl.
  unfold begin_tx in Hb.
  destruct (j v).(in_tx); simpl in Hb; try discriminate.
  inversion Hb; subst. clear Hb.
  unfold log_mut in Hl. simpl in Hl.
  inversion Hl; subst. simpl. auto.
Qed.

(* ═══════════════════════════════════════════════════════════════════
   §11  Example scenarios (console-auditable)
   ═══════════════════════════════════════════════════════════════════ *)

Example demo_tx :
  exists v1 v2 v3,
    begin_tx initial_volume = Some v1 /\
    log_mut v1 (FidAlloc 1 42) = Some v2 /\
    commit v2 = Some v3 /\
    is_clean v3 /\
    gen v3 = 1 /\
    (j v3).(committed) = [FidAlloc 1 42] /\
    is_clean (replay (crash v3)) /\
    (j (replay (crash v3))).(committed) = [FidAlloc 1 42].
Proof.
  set (v1 := {| gen := 0; dirty := true;
                j := {| log := []; committed := []; in_tx := true |} |}).
  set (v2 := {| gen := 0; dirty := true;
                j := {| log := [FidAlloc 1 42]; committed := []; in_tx := true |} |}).
  set (v3 := {| gen := 1; dirty := false;
                j := {| log := []; committed := [FidAlloc 1 42]; in_tx := false |} |}).
  exists v1, v2, v3.
  unfold initial_volume, empty_journal, is_clean, crash, replay.
  repeat split; simpl; try reflexivity; auto.
Qed.

(** Crash mid-transaction: durable history stays empty. *)
Example demo_crash_mid_tx :
  exists v1 v2 vR,
    begin_tx initial_volume = Some v1 /\
    log_mut v1 (FidAlloc 7 99) = Some v2 /\
    vR = replay (crash v2) /\
    is_clean vR /\
    (j vR).(committed) = [] /\
    gen vR = 0.
Proof.
  set (v1 := {| gen := 0; dirty := true;
                j := {| log := []; committed := []; in_tx := true |} |}).
  set (v2 := {| gen := 0; dirty := true;
                j := {| log := [FidAlloc 7 99]; committed := []; in_tx := true |} |}).
  set (vR := {| gen := 0; dirty := false;
                j := {| log := []; committed := []; in_tx := false |} |}).
  exists v1, v2, vR.
  unfold initial_volume, empty_journal, is_clean, crash, replay.
  repeat split; simpl; try reflexivity; auto.
Qed.

(* ═══════════════════════════════════════════════════════════════════
   §12  Audit: no axioms
   ═══════════════════════════════════════════════════════════════════ *)

Print Assumptions crash_replay_safety.
Print Assumptions recovery_idempotent.
Print Assumptions steps_preserve_wf.
Print Assumptions crash_during_tx_recovery.
Print Assumptions log_then_crash_not_durable.
Print Assumptions step_generation_ge.
