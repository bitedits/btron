(* bfs_properties.v
 *
 * Formal model of B-System Clean-Room Volume V2 journal + RT_VECTOR index.
 * Mirrors the executable OCaml model in bfs_model.ml (pure functional).
 *
 * Properties proved:
 *   1. commit_clears_inflight
 *   2. crash_drops_inflight
 *   3. replay_idempotent_on_committed
 *   4. replay_after_crash_agrees_with_commit_prefix
 *   5. generation_monotonic_on_commit
 *   ... (see theorems below)
 *
 * Build (Rocq / Coq >= 9.0):
 *   coqc bfs_properties.v
 *
 * Pairing with the executable oracle:
 *   ocaml bfs_model.ml
 *   coqc  bfs_properties.v
 *)

From Stdlib Require Import List.
From Stdlib Require Import Arith.
From Stdlib Require Import Bool.
From Stdlib Require Import Lia.
Import ListNotations.

(* ── basic identifiers ─────────────────────────────────────────── *)

Definition fid := nat.
Definition rec_idx := nat.
Definition vec_id := nat.
Definition generation := nat.
Definition name_id := nat.   (* abstract file name; avoids String dependency *)

(* ── mutations (journal records) ───────────────────────────────── *)

Inductive mutation : Type :=
| FidAlloc : fid -> name_id -> mutation
| FidFree : fid -> mutation
| SetRecord : fid -> rec_idx -> mutation   (* simplified: only idx tracked *)
| DelRecord : fid -> rec_idx -> mutation
| IndexInsert : vec_id -> fid -> rec_idx -> mutation
| IndexRemove : vec_id -> mutation.

(* ── journal ───────────────────────────────────────────────────── *)

Record journal : Type := mkJournal {
  log : list mutation;          (* in-flight, newest at head *)
  committed : list mutation;    (* oldest first *)
  in_tx : bool
}.

Definition empty_journal : journal :=
  {| log := []; committed := []; in_tx := false |}.

(* ── volume (abstract state) ───────────────────────────────────── *)

Record volume : Type := mkVolume {
  gen : generation;
  dirty : bool;
  j : journal
}.

Definition initial_volume : volume :=
  {| gen := 0; dirty := false; j := empty_journal |}.

(* ── journal operations ────────────────────────────────────────── *)

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

(* Crash: discard in-flight log, keep committed, mark dirty *)
Definition crash (v : volume) : volume :=
  {| gen := v.(gen);
     dirty := true;
     j := {| log := [];
             committed := (j v).(committed);
             in_tx := false |} |}.

(* Replay: clear dirty; state is already determined by committed log.
   In the abstract model, replay just acknowledges recovery. *)
Definition replay (v : volume) : volume :=
  {| gen := v.(gen);
     dirty := false;
     j := {| log := [];
             committed := (j v).(committed);
             in_tx := false |} |}.

(* ── helpers ───────────────────────────────────────────────────── *)

Definition is_clean (v : volume) : Prop :=
  dirty v = false /\ (j v).(in_tx) = false /\ (j v).(log) = [].

Definition only_committed (v : volume) : Prop :=
  (j v).(log) = [] /\ (j v).(in_tx) = false.

(* ── Theorems ──────────────────────────────────────────────────── *)

(* 1. Successful commit leaves no in-flight state *)
Theorem commit_clears_inflight :
  forall v v',
    commit v = Some v' ->
    (j v').(in_tx) = false /\ (j v').(log) = [] /\ dirty v' = false.
Proof.
  intros v v' H.
  unfold commit in H.
  destruct (j v).(in_tx) eqn:E; simpl in H.
  - inversion H; subst; simpl; auto.
  - discriminate.
Qed.

(* 2. Crash drops any in-flight log *)
Theorem crash_drops_inflight :
  forall v,
    (j (crash v)).(log) = [] /\ (j (crash v)).(in_tx) = false.
Proof.
  intros v. simpl. auto.
Qed.

(* 3. Crash preserves the committed history *)
Theorem crash_preserves_committed :
  forall v,
    (j (crash v)).(committed) = (j v).(committed).
Proof.
  intros v. reflexivity.
Qed.

(* 4. Replay after crash yields a clean volume with same committed log *)
Theorem replay_after_crash_clean :
  forall v,
    let v' := replay (crash v) in
    dirty v' = false /\
    (j v').(in_tx) = false /\
    (j v').(log) = [] /\
    (j v').(committed) = (j v).(committed).
Proof.
  intros v. simpl. auto.
Qed.

(* 5. Generation increases exactly by one on commit *)
Theorem generation_monotonic_on_commit :
  forall v v',
    commit v = Some v' ->
    gen v' = S (gen v).
Proof.
  intros v v' H.
  unfold commit in H.
  destruct (j v).(in_tx); simpl in H.
  - inversion H; subst; reflexivity.
  - discriminate.
Qed.

(* 6. begin_tx; log_mut*; commit  produces a clean volume
      with committed history extended by the logged mutations *)
Lemma begin_tx_sets_in_tx :
  forall v v',
    begin_tx v = Some v' ->
    (j v').(in_tx) = true /\ (j v').(log) = [] /\ dirty v' = true.
Proof.
  intros v v' H.
  unfold begin_tx in H.
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
  unfold commit in Hcom.
  rewrite Hin in Hcom. simpl in Hcom.
  (* Avoid subst wiping Hlog; project equality from inversion manually *)
  assert (HEq : v' = {| gen := S (gen v);
                        dirty := false;
                        j := {| log := [];
                                committed := (j v).(committed) ++ List.rev (j v).(log);
                                in_tx := false |} |}) by (inversion Hcom; reflexivity).
  rewrite HEq. simpl.
  rewrite Hlog. reflexivity.
Qed.

(* 7. Abort discards in-flight and restores clean flag without
      touching committed history or generation *)
Theorem abort_discards_inflight :
  forall v v',
    abort v = Some v' ->
    (j v').(log) = [] /\
    (j v').(in_tx) = false /\
    dirty v' = false /\
    (j v').(committed) = (j v).(committed) /\
    gen v' = gen v.
Proof.
  intros v v' H.
  unfold abort in H.
  destruct (j v).(in_tx); simpl in H.
  - inversion H; subst; simpl; auto.
  - discriminate.
Qed.

(* 8. Replay is idempotent on an already-clean committed state *)
Theorem replay_idempotent :
  forall v,
    only_committed v ->
    replay (replay v) = replay v.
Proof.
  intros v [Hlog Hin].
  unfold replay, only_committed.
  simpl.
  (* both sides construct the same record *)
  reflexivity.
Qed.

(* 9. Combined safety: after any crash+replay the volume is clean
      and its committed log is a prefix of what a successful commit
      sequence would have produced (here: identical to pre-crash committed). *)
Theorem crash_replay_safety :
  forall v,
    is_clean (replay (crash v)) /\
    (j (replay (crash v))).(committed) = (j v).(committed).
Proof.
  intros v.
  unfold is_clean. simpl. repeat split; auto.
Qed.

(* ── Example execution (as a proof-side illustration) ──────────── *)

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

Print Assumptions crash_replay_safety.
Print Assumptions generation_monotonic_on_commit.
Print Assumptions commit_clears_inflight.
