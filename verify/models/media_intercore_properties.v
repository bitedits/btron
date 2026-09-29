From Stdlib Require Import Arith List Bool Lia.
Import ListNotations.
Local Open Scope bool_scope.
Definition input_period_us : nat := 1000.
Definition ui_period_us : nat := 8 * 1000 + 333.
Definition input_budget_us : nat := 500.
Definition ui_budget_us : nat := 4000.
Definition whole_slices : nat := ui_period_us / input_period_us.
Definition tail_us : nat := ui_period_us mod input_period_us.
Definition supply_whole : nat := whole_slices * (input_period_us - input_budget_us).
Definition supply_tail : nat := supply_whole + tail_us.
Record plane_state := mk_plane { ps_debt : nat; ps_peak : nat; ps_starved : nat }.
Definition plane0 : plane_state := mk_plane 0 0 0.
Definition period_demand (first : bool) (overrun : nat) : nat :=
  ui_budget_us + if first then overrun else 0.
Definition step (supply : nat) (first : bool) (overrun : nat) (st : plane_state) : plane_state :=
  let raw := ps_debt st + period_demand first overrun - supply in
  mk_plane (Nat.max 0 raw) (Nat.max (ps_peak st) raw)
           (ps_starved st + if Nat.ltb 0 raw then 1 else 0).
Fixpoint run_aux (n : nat) (first : bool) (supply overrun : nat) (st : plane_state) : plane_state :=
  match n with
  | 0 => st
  | S k => run_aux k false supply overrun (step supply first overrun st)
  end.
Definition run_periods (n : nat) (use_tail : bool) (overrun : nat) : plane_state :=
  run_aux n true (if use_tail then supply_tail else supply_whole) overrun plane0.
Lemma supply_eq_budget : supply_whole = ui_budget_us.
Proof. reflexivity. Qed.
Lemma supply_covers_budget :
  forall (ut : bool), ui_budget_us <= (if ut then supply_tail else supply_whole).
Proof. intros ut. apply Nat.leb_le. destruct ut; vm_compute; reflexivity. Qed.

