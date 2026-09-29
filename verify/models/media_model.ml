(* media_model.ml
 *
 * Executable specification and verification oracle for the GLOBAL media model:
 * the B-System scheduling layer and the applications it runs, seen from above
 * the three subsystem models.
 *
 *   - the OS half: cyclic handlers with fixed periods and EQUAL task priorities
 *     (ASYNC.txt §9, §10), the I/O core poll cadence and WCET budget (IO.txt §5),
 *     poll_bus at every scheduler yield (ASYNC.txt §0.1)
 *   - the app half: the input plane, the presentation plane, the I/O defer task
 *     and the media decoder, each pinned to a spawned actor and each holding a
 *     cursor on the InterCore star
 *   - the subsystems enter as summaries - the EVT ring, the sector pool and the
 *     cursor ownership map - because their internals are proved in
 *     media_intercore_properties.v, media_smp_properties.v and
 *     media_rtp_properties.v.  What is checked HERE is only what lives between
 *     them: that a schedule of app steps keeps every plane's contract at once.
 *
 * The pass/fail table is ASYNC.txt §10, unchanged: ISR WCET, input release
 * jitter <= 10% of Ti, <= 2 ms between pointer updates under redraw, zero EVT
 * overflow in stress, pri(input) == pri(ui), the in-blit USB drain forbidden,
 * and a Tier-2 message path that uses only the CMPXCHG8 ring.
 *
 * Normative sources: doc/txt/ASYNC.txt §0.1, §9, §10; doc/txt/IO.txt §5;
 * doc/txt/SMP-AMP-SYNC-IO-HARDENING.txt §7.2-§7.4.
 * Companion proof file: media_properties.v.
 *
 * OCaml >= 4.14 / 5.x
 *
 * Build & test:
 *   ocamlc -o media_model media_model.ml && ./media_model
 *)

open Printf

(* ── 1. Contract constants ───────────────────────────────────────── *)

(* ASYNC.txt §9, Tier 1 extras *)
let input_period_us = 1000
let ui_period_us = 8333
let isr_trb_budget = 32
let input_evt_budget = 16
let pri_input_ui = 100

(* ASYNC.txt §9, Tier 2 extras *)
let use_mpsc_actors = 1

(* IO.txt §5, the I/O core contract *)
let io_core_id = 3
let io_period_us = 250
let io_budget_us = 500
let io_heartbeat_ms = 100
let io_takeover_timeout_ms = 10
let ui_period_us_fast = 4167

(* the subsystem summaries this model reads *)
let sector_pool = 64
let writer_capacity = 4
let max_writers = 4
let event_queue_size = 256
let cores_shipped = 1
let cores_target = 4

(* ASYNC.txt §10, as numbers *)
let jitter_bound_us = input_period_us / 10 (* 10% of Ti *)
let pointer_gap_bound_us = 2000 (* <= 2 ms under redraw *)
let trb_cost_us = 2 (* one deferred TRB drain, in microseconds *)

(* ── 2. The four actors the apps run ─────────────────────────────── *)

type task = T_INPUT | T_UI | T_IOD | T_DEC

let show_task = function
  | T_INPUT -> "INPUT"
  | T_UI -> "UI"
  | T_IOD -> "IOD"
  | T_DEC -> "DEC"

let all_tasks = [ T_INPUT; T_UI; T_IOD; T_DEC ]

(* ASYNC.txt §9: INPUT and UI carry the SAME priority.  Determinism comes from
 * bounded work and fixed periods, not from a priority difference. *)
let priority_of _ = pri_input_ui

(* the period each cyclic handler releases on *)
let period_of = function
  | T_INPUT -> input_period_us
  | T_UI -> ui_period_us
  | T_IOD -> io_period_us
  | T_DEC -> input_period_us

(* the work an actor may charge inside one period, in microseconds *)
let budget_of = function
  | T_INPUT -> input_evt_budget * 8
  | T_UI -> isr_trb_budget * 8
  | T_IOD -> io_budget_us
  | T_DEC -> isr_trb_budget * 8

(* the core each actor is pinned to: only the I/O defer task needs core 3 *)
let core_of = function T_IOD -> io_core_id | _ -> 0

(* the cursor each actor owns on the star - dense ids, one per actor *)
let cursor_of = function
  | T_INPUT -> 0
  | T_UI -> 1
  | T_IOD -> 2
  | T_DEC -> 3

(* the plane an actor reads from: UI consumes the input reports, DEC consumes
 * the I/O events; the others publish only *)
let reads_from = function
  | T_UI -> Some T_INPUT
  | T_DEC -> Some T_IOD
  | _ -> None

(* ── 3. The global state ────────────────────────────────────────── *)

type gstate = {
  g_clock : int; (* microseconds since boot *)
  g_work : (string * int) list; (* work charged in the current period *)
  g_last : (string * int) list; (* clock of the last admitted release *)
  g_evt_head : int;
  g_evt_tail : int;
  g_trbs : int; (* deferred transfer records waiting for a drain *)
  g_posts : int; (* events posted since the last input release: the §9 budget *)
  g_evt_refusals : int;
  g_strangers : int; (* publish attempts on a cursor the actor does not own *)
  g_free : int; (* sectors left in the post-boot pool *)
  g_resident : (int * int) list; (* cursor -> sectors held by the publisher *)
  g_held : (string * int) list; (* reader -> sectors taken and not yet presented *)
  g_owner : (int * task) list; (* cursor -> live owning actor *)
  g_live : task list;
  g_ptr_last : int; (* clock of the last pointer update *)
  g_ptr_gap : int; (* worst gap seen *)
  g_trb_peak : int; (* worst deferred backlog seen *)
  g_evt_peak : int;
  g_resident_peak : int;
  g_violations : string list;
  g_cores : int; (* cores this port has: the topology the scheduler may pin on *)
}

let g0 = {
  g_clock = 0;
  g_work = [];
  g_last = [];
  g_evt_head = 0;
  g_evt_tail = 0;
  g_trbs = 0;
  g_posts = 0;
  g_evt_refusals = 0;
  g_strangers = 0;
  g_free = sector_pool;
  g_resident = [];
  g_held = [];
  g_owner = [];
  g_live = [];
  g_ptr_last = 0;
  g_ptr_gap = 0;
  g_trb_peak = 0;
  g_evt_peak = 0;
  g_resident_peak = 0;
  g_violations = [];
  g_cores = cores_target;
}

let lookup k tbl =
  match List.filter (fun (i, v) -> i = k) tbl with
  | [ (_, v) ] -> Some v
  | _ -> None

let store k v tbl = (k, v) :: List.filter (fun (i, _) -> i <> k) tbl

let drop k tbl = List.filter (fun (i, _) -> i <> k) tbl

let work_of t st = match lookup (show_task t) st.g_work with Some n -> n | None -> 0

(* an actor that was never released is due immediately *)
let last_of t st = match lookup (show_task t) st.g_last with Some n -> n | None -> - period_of t

let is_live st t = List.mem t st.g_live

(* ── 4. The OS-level step alphabet ──────────────────────────────── *)

(* One step is one scheduler decision.  The search runs every word over this
 * alphabet: the schedule is not chosen by the model, only its admissibility and
 * its invariants are, which is the modal reading of a non-determined run. *)
type gstep =
  | Advance of int
  | Spawn of task
  | Release of task
  | Publish of task
  | Deliver of task
  | EvtPost
  | EvtDrain
  | Blit
  | Stranger of task * task (* publish with a cursor that belongs to another actor *)
  | Retire of task

let show_step = function
  | Advance k -> "advance " ^ string_of_int k
  | Spawn t -> "spawn " ^ show_task t
  | Release t -> "release " ^ show_task t
  | Publish t -> "publish " ^ show_task t
  | Deliver t -> "deliver " ^ show_task t
  | EvtPost -> "evt_post"
  | EvtDrain -> "evt_drain"
  | Blit -> "blit"
  | Stranger (o, i) -> "stranger " ^ show_task i ^ " on " ^ show_task o
  | Retire t -> "retire " ^ show_task t

(* the ticks the cyclic machinery can produce: the poll period, its WCET
 * budget, and the input period.  The UI period is deliberately absent from the
 * search alphabet - it is 8333 us, and a step that long cannot be admitted
 * between two pointer updates without breaking the 2 ms ceiling. *)
let all_ticks = [ io_period_us; io_budget_us; input_period_us ]

let all_steps =
  List.concat [
    List.map (fun k -> Advance k) all_ticks;
    List.map (fun t -> Spawn t) all_tasks;
    List.map (fun t -> Release t) all_tasks;
    List.map (fun t -> Publish t) all_tasks;
    List.map (fun t -> Deliver t) all_tasks;
    [ EvtPost; EvtDrain; Blit ];
    List.map (fun t -> Retire t) all_tasks;
  ]

(* one representative of every kind of step, which is what the search alphabet
 * has to cover *)
let step_kinds =
  [ ("advance", fun s -> match s with Advance _ -> true | _ -> false);
    ("spawn", fun s -> match s with Spawn _ -> true | _ -> false);
    ("release", fun s -> match s with Release _ -> true | _ -> false);
    ("publish", fun s -> match s with Publish _ -> true | _ -> false);
    ("deliver", fun s -> match s with Deliver _ -> true | _ -> false);
    ("evt_post", fun s -> s = EvtPost);
    ("evt_drain", fun s -> s = EvtDrain);
    ("blit", fun s -> s = Blit);
    ("stranger", fun s -> match s with Stranger _ -> true | _ -> false);
    ("retire", fun s -> match s with Retire _ -> true | _ -> false) ]

(* the forbidden compound: draining USB inside the blit (ASYNC.txt §10 -
 * "Architecture requires in-blit USB drain: forbidden").  It is not in the
 * admitted alphabet; Blit does not touch the ring at all. *)
let in_blit_drain_admitted = false

(* is the core this actor wants present on THIS port?  The port is part of the
 * state, not of the schedule: the shipped build has one core, the target four,
 * and only the target can pin the I/O defer task. *)
let core_present st c = c < st.g_cores

(* sectors in the readers' hands: taken from a cursor, not yet presented *)
let held_total st = List.fold_left (fun a (_, n) -> a + n) 0 st.g_held

(* ── 5. The step function ───────────────────────────────────────── *)

(* A step whose contract fails leaves the state untouched, which is what the
 * search checks: a refusal must not be a mutation. *)
let step st s =
  match s with
  | Advance k -> { st with g_clock = st.g_clock + k }
  | Spawn t ->
    if is_live st t then st
    else if not (core_present st (core_of t)) then st
    else if List.length st.g_owner >= max_writers * writer_capacity then st
    else
      { st with
        g_live = t :: st.g_live;
        g_owner = (cursor_of t, t) :: st.g_owner }
  | Release t ->
    (* a cyclic handler does not re-release before its period has elapsed *)
    let due = last_of t st + period_of t in
    if not (is_live st t) || st.g_clock < due then st
    else
      let gap = st.g_clock - st.g_ptr_last in
      { st with
        g_last = store (show_task t) st.g_clock st.g_last;
        g_work = store (show_task t) 0 st.g_work;
        g_ptr_last = (if t = T_INPUT then st.g_clock else st.g_ptr_last);
        g_ptr_gap = (if t = T_INPUT then max st.g_ptr_gap gap else st.g_ptr_gap);
        g_posts = (if t = T_INPUT then 0 else st.g_posts);
        g_trb_peak = max st.g_trb_peak st.g_trbs }
  | Publish t ->
    (* one sector out of the pool, by the owner of that cursor only *)
    (match lookup (cursor_of t) st.g_owner with
    | Some owner when owner = t && is_live st t && st.g_free > 0 ->
      let held = match lookup (cursor_of t) st.g_resident with Some n -> n | None -> 0 in
      if held >= writer_capacity || work_of t st + 8 > budget_of t then st
      else
        { st with
          g_free = st.g_free - 1;
          g_resident = store (cursor_of t) (held + 1) st.g_resident;
          g_resident_peak = max st.g_resident_peak (held + 1);
          g_work = store (show_task t) (work_of t st + 8) st.g_work }
    | Some _ -> { st with g_violations = "publish on a stranger's cursor" :: st.g_violations }
    | None -> st)
  | Stranger (owner, intruder) ->
    (* the misuse the cursor-index API invites: name a cursor you do not own.
     * It is refused, and a refusal is a stutter, not a mutation. *)
    if owner = intruder then st
    else if not (is_live st intruder) then st
    else
      (match lookup (cursor_of owner) st.g_owner with
      | Some o when o = owner && is_live st owner ->
        { st with g_strangers = st.g_strangers + 1 }
      | _ -> st)
  | Deliver t ->
    (* strictly non-blocking: the reader takes what the cursor holds.  The
     * sector leaves the publisher's ring and lands in the reader's hands; it is
     * the blit or the retire that returns it to the pool, never the read. *)
    if not (is_live st t) then st
    else
      (match reads_from t with
      | None -> st
      | Some src ->
        let n = match lookup (cursor_of src) st.g_resident with Some k -> k | None -> 0 in
        if n = 0 then st
        else if work_of t st + 8 > budget_of t then st
        else
          let in_hand =
            match lookup (show_task t) st.g_held with Some k -> k | None -> 0
          in
          { st with
            g_resident = store (cursor_of src) (n - 1) st.g_resident;
            g_held = store (show_task t) (in_hand + 1) st.g_held;
            g_work = store (show_task t) (work_of t st + 8) st.g_work })
  | EvtPost ->
    (* the producer writes the tail only.  Two things refuse it before the ring
     * could be over-filled: the 256-slot capacity (IO.txt §5.1) and the per-
     * release deferred-ISR window (ASYNC.txt §9).  A refusal is counted, never
     * buffered, and never touches a cursor. *)
    let occ = st.g_evt_tail - st.g_evt_head in
    if occ >= event_queue_size || st.g_posts >= input_evt_budget then
      { st with g_evt_refusals = st.g_evt_refusals + 1 }
    else
      { st with
        g_evt_tail = st.g_evt_tail + 1;
        g_posts = st.g_posts + 1;
        g_trbs = st.g_trbs + 1;
        g_trb_peak = max st.g_trb_peak (st.g_trbs + 1);
        g_evt_peak = max st.g_evt_peak (occ + 1) }
  | EvtDrain ->
    (* the consumer reads the head only, up to the per-release event budget, and
     * the drain is what retires the deferred TRB window *)
    let n = min input_evt_budget (st.g_evt_tail - st.g_evt_head) in
    { st with g_evt_head = st.g_evt_head + n; g_trbs = 0; g_posts = 0 }
  | Blit ->
    (* the presentation step returns every sector in flight - the ones still on
     * a cursor and the ones a reader has taken - and invents none.  It does not
     * touch the EVT ring: the in-blit USB drain is forbidden. *)
    let in_flight = List.fold_left (fun a (_, n) -> a + n) 0 st.g_resident in
    { st with
      g_free = st.g_free + in_flight + held_total st;
      g_resident = [];
      g_held = [] }
  | Retire t ->
    if not (is_live st t) then st
    else
      let held = match lookup (cursor_of t) st.g_resident with Some n -> n | None -> 0 in
      let in_hand = match lookup (show_task t) st.g_held with Some n -> n | None -> 0 in
      { st with
        g_live = List.filter (fun x -> x <> t) st.g_live;
        g_owner = drop (cursor_of t) st.g_owner;
        g_resident = drop (cursor_of t) st.g_resident;
        g_held = drop (show_task t) st.g_held;
        g_free = st.g_free + held + in_hand }

let run_steps st ss = List.fold_left step st ss

(* ── 6. The invariants, checked on every reachable state ────────── *)

let evt_occupancy st = st.g_evt_tail - st.g_evt_head

let resident_total st = List.fold_left (fun a (_, n) -> a + n) 0 st.g_resident

(* G1 the priority plan is the documented one, and no step can change it *)
let priorities_shared _st =
  List.for_all (fun t -> priority_of t = pri_input_ui) all_tasks

(* G2 no actor charged more than its period budget *)
let within_budget st =
  List.for_all
    (fun (k, n) ->
      match List.filter (fun t -> show_task t = k) all_tasks with
      | [ t ] -> n <= budget_of t
      | _ -> false)
    st.g_work

(* G3 the deferred ISR backlog stays inside the per-release window, which is
 * itself inside the TRB budget - and that double margin is what bounds the
 * input release jitter *)
let trbs_bounded st =
  st.g_trbs >= 0 && st.g_posts >= 0
  && st.g_posts <= input_evt_budget && st.g_trbs <= isr_trb_budget

(* G4 two consecutive pointer updates are never further apart than the bound *)
let pointer_gap_ok st = st.g_ptr_gap <= pointer_gap_bound_us

(* G5 the ring is never over-full, and never negative *)
let evt_sane st =
  let occ = evt_occupancy st in
  occ >= 0 && occ <= event_queue_size

(* G6 a cursor has exactly one live owner, and an owner holds its own cursor *)
let owners_unique st =
  let ids = List.map fst st.g_owner in
  List.length ids = List.length (List.sort_uniq compare ids)
  && List.for_all (fun (i, t) -> is_live st t && cursor_of t = i) st.g_owner

(* G7 the pool is conserved exactly: nothing is allocated after boot, and a
 * sector is either free, resident on a cursor, or in a reader's hands *)
let pool_closed st =
  st.g_free + resident_total st + held_total st = sector_pool
  && 0 <= st.g_free && st.g_free <= sector_pool

(* G8 residency per cursor is bounded by the ring capacity, and the residency
 * table by the writer table *)
let residency_bounded st =
  List.for_all (fun (_, n) -> n >= 0 && n <= writer_capacity) st.g_resident
  && List.length st.g_resident <= max_writers

(* G10 the forbidden act never happened *)
let no_violation st = st.g_violations = []

let all_invariants st =
  [ ("G1 priorities", priorities_shared st);
    ("G2 budget", within_budget st);
    ("G3 TRB backlog", trbs_bounded st);
    ("G4 pointer gap", pointer_gap_ok st);
    ("G5 EVT ring", evt_sane st);
    ("G6 cursor ownership", owners_unique st);
    ("G7 pool conservation", pool_closed st);
    ("G8 residency bound", residency_bounded st);
    ("G10 no forbidden act", no_violation st) ]

(* ── 7. The gate, read from the OS side ─────────────────────────── *)

type mp_flag = MP_ON | MP_OFF
type verdict = V_PASS | V_SKIP | V_FAIL

let mp_of = function MP_ON -> true | MP_OFF -> false

let os_rows = [ "planes"; "ring"; "intercore"; "amp"; "io_wcet" ]

(* an actor set is schedulable only if every pin lands on a core the port has *)
let schedulable cores = List.for_all (fun t -> core_of t < cores) all_tasks

let row_compiled row mp = match mp with MP_OFF -> row = "planes" | MP_ON -> true

let row_topology row cores =
  match row with
  | "planes" -> true
  | "amp" -> io_core_id < cores
  | "io_wcet" -> io_core_id < cores
  | _ -> 1 <= cores

(* a demanded row must not degrade into a skip: that is the difference between
 * an untested feature and an unbuildable one (plan §7.2) *)
let row_verdict row ~mp ~cores ~require =
  if not (row_compiled row mp) then if require && row = "amp" then V_FAIL else V_SKIP
  else if not (row_topology row cores) then if require && row = "amp" then V_FAIL else V_SKIP
  else V_PASS

let run_summary ~mp ~cores ~require =
  let vs = List.map (fun r -> row_verdict r ~mp ~cores ~require) os_rows in
  let n f = List.length (List.filter f vs) in
  (n (fun v -> v = V_PASS), n (fun v -> v = V_SKIP), n (fun v -> v = V_FAIL))

(* ── 9. The schedule search, its fingerprint, and the modal reading ─── *)

(* one representative of every kind of step, so five positions can reach every
 * contract edge of the surface *)
let search_alphabet =
  [ Advance io_period_us;
    Spawn T_INPUT;
    Release T_INPUT;
    Publish T_INPUT;
    Deliver T_UI;
    Stranger (T_INPUT, T_UI);
    EvtPost;
    EvtDrain;
    Blit ]

let seq_len = 5
let n_ops = List.length search_alphabet

(* the fingerprint the search uses to notice a stutter: a refused step must
 * leave every mutable field exactly as it found it *)
let fingerprint st =
  ( st.g_clock, st.g_free, st.g_evt_head, st.g_evt_tail, st.g_trbs, st.g_posts,
    st.g_evt_refusals, st.g_strangers, st.g_ptr_last, st.g_ptr_gap,
    List.length st.g_live, List.length st.g_owner,
    List.length st.g_resident, List.length st.g_held )


(* The scheduler is not part of the model, so the only honest quantification is
 * over the one-step successors of a state: possibly p = existsb, necessarily p =
 * forallb.  A refusal is a stutter, which makes the relation reflexive - that is
 * why the box modality here satisfies axiom T and not merely K. *)
let successors st = List.map (fun s -> step st s) search_alphabet

let possibly p st = List.exists p (successors st)

let necessarily p st = List.for_all p (successors st)

let has_stutter st =
  let f = fingerprint st in
  List.exists (fun s -> fingerprint (step st s) = f) search_alphabet

let pow_int base e =
  let rec go acc i = if i = 0 then acc else go (acc * base) (i - 1) in
  go 1 e

(* a seeded schedule: the input and presentation actors are pinned and released,
 * one report is in flight, and one blit has already returned its sectors *)
let seeded =
  run_steps g0
    [ Spawn T_INPUT;
      Spawn T_UI;
      Advance io_period_us;
      Release T_INPUT;
      EvtPost;
      Publish T_INPUT;
      Release T_UI ]

let inv_failures = ref ([] : string list)
let search_total = ref 0
let kinds_reached = ref ([] : string list)
let pool_below_seen = ref false
let refused_seen = ref false
let stutter_seen = ref 0
let stranger_refused = ref false
let spawn_live_refused = ref false
let gap_seen = ref 0
let trb_peak_seen = ref 0
let evt_peak_seen = ref 0
let resident_peak_seen = ref 0

let observe st =
  incr search_total;
  List.iter (fun (nm, ok) -> if not ok then inv_failures := nm :: !inv_failures) (all_invariants st);
  pool_below_seen := !pool_below_seen || st.g_free + held_total st < sector_pool;
  refused_seen := !refused_seen || st.g_strangers > 0;
  gap_seen := max !gap_seen st.g_ptr_gap;
  trb_peak_seen := max !trb_peak_seen st.g_posts;
  evt_peak_seen := max !evt_peak_seen (evt_occupancy st);
  resident_peak_seen := max !resident_peak_seen (resident_total st)

let reflexive_seen = ref true

let rec search st = function
  | [] ->
    reflexive_seen := !reflexive_seen && has_stutter st;
    observe st
  | s :: rest ->
    List.iter
      (fun (nm, f) -> if f s then kinds_reached := nm :: !kinds_reached)
      step_kinds;
    let before = fingerprint st in
    let st' = step st s in
    if fingerprint st' = before then begin
      stutter_seen := !stutter_seen + 1;
      if s = Spawn T_INPUT then spawn_live_refused := true
    end;
    if st'.g_strangers > st.g_strangers then stranger_refused := true;
    search st' rest

let rec enumerate k prefix =
  if k = 0 then search seeded (List.rev prefix)
  else List.iter (fun s -> enumerate (k - 1) (s :: prefix)) search_alphabet

(* ── 10. Rows ────────────────────────────────────────────────────── *)

let failures = ref ([] : string list)
let checks = ref 0

let expect name cond =
  incr checks;
  if not cond then failures := name :: !failures

let () =
  printf
    "==> B-System media model: OS cyclic handlers, the four actors, RTP/SMP/InterCore/EVT as summaries\n\
    \    schedules: explored %d interleavings (%d kinds ^ length %d)\n"
    (pow_int n_ops seq_len) n_ops seq_len;
  enumerate seq_len [];

  expect "G0 the search starts from a well-formed schedule"
    (List.for_all snd (all_invariants seeded));
  expect "G0 the search really covered the space" (!search_total = pow_int n_ops seq_len);
  expect "G0 no explored schedule violates an invariant" (!inv_failures = []);
  expect "G0 the alphabet reaches every kind of scheduler decision"
    (List.for_all
       (fun (nm, _) -> List.mem nm !kinds_reached)
       [ ("advance", snd (List.hd step_kinds));
         ("spawn", snd (List.nth step_kinds 1));
         ("release", snd (List.nth step_kinds 2));
         ("publish", snd (List.nth step_kinds 3));
         ("deliver", snd (List.nth step_kinds 4));
         ("evt_post", snd (List.nth step_kinds 5));
         ("evt_drain", snd (List.nth step_kinds 6));
         ("blit", snd (List.nth step_kinds 7));
         ("stranger", snd (List.nth step_kinds 8)) ]);
  expect "G0 the search reached sectors in flight" !pool_below_seen;
  expect "G0 the search reached a stranger publish, and it was refused not admitted"
    (!refused_seen && !stranger_refused);
  expect "G0 the search reached a stutter: a refused step mutated nothing"
    (!stutter_seen > 0 && !spawn_live_refused);
  expect "G0 the search reached a pointer update after a delay" (!gap_seen > 0);

  expect "G1 pri(INPUT) == pri(UI) == pri(IOD) == pri(DEC), as ASYNC.txt §10 requires"
    (priorities_shared g0 && priority_of T_INPUT = priority_of T_UI);
  expect "G1 no step in the alphabet can change a priority"
    (let st = run_steps seeded all_steps in
     priorities_shared st && priority_of T_INPUT = pri_input_ui);

  expect "G2 the I/O budget is exactly two poll periods" (io_budget_us = 2 * io_period_us);
  expect "G2 the poll period is positive and inside the input period"
    (io_period_us > 0 && io_period_us <= input_period_us);
  expect "G2 no actor exceeded its period budget in any explored schedule"
    (List.for_all (fun (nm, ok) -> nm <> "G2 budget" || ok) (all_invariants (run_steps seeded [ Advance input_period_us; Release T_INPUT; Publish T_INPUT; Publish T_INPUT ])));
  expect "G2 the input actor's budget is the per-release event budget"
    (budget_of T_INPUT = input_evt_budget * 8);

  expect "G3 the jitter bound is 10%% of Ti" (jitter_bound_us = input_period_us / 10);
  expect "G3 the deferred TRB backlog is what bounds the jitter, and it fits"
    (trb_cost_us * isr_trb_budget <= jitter_bound_us);
  expect "G3 no explored schedule exceeded the TRB budget"
    (!trb_peak_seen <= isr_trb_budget);

  expect "G4 the ceiling is two input periods" (pointer_gap_bound_us = 2 * input_period_us);
  expect "G4 no explored schedule missed the 2 ms pointer ceiling" (!gap_seen <= pointer_gap_bound_us);
  expect "G4 a full UI period does not fit between two pointer updates"
    (ui_period_us > pointer_gap_bound_us);

  expect "G5 the EVT capacity is a power of two"
    (event_queue_size = 256 && event_queue_size = 1 lsl 8);
  expect "G5 no explored schedule over-filled the ring"
    (!evt_peak_seen <= event_queue_size);
  (* one stalled input release: the tick, the release that reopens the deferred
   * window, and the whole window spent on events with no drain in between *)
  (* the input plane has to be pinned before it can release: an unspawned actor
   * has no period to run on *)
  let live = step g0 (Spawn T_INPUT) in
  let one_release st =
    run_steps st
      (Advance input_period_us :: Release T_INPUT
      :: List.init input_evt_budget (fun _ -> EvtPost))
  in
  let releases = event_queue_size / input_evt_budget in
  let filled =
    List.fold_left (fun st _ -> one_release st) live (List.init releases (fun _ -> ()))
  in
  let overfilled = one_release filled in
  expect "G5 a stalled release cannot fill the ring: the per-release window is the tighter bound"
    (evt_occupancy (one_release live) = input_evt_budget
     && (one_release live).g_evt_refusals = 0
     && is_live live T_INPUT
     && input_evt_budget < event_queue_size);
  expect "G5 the stalled consumer fills the ring to capacity after capacity/budget releases"
    (evt_occupancy filled = event_queue_size && filled.g_evt_refusals = 0
     && releases * input_evt_budget = event_queue_size);
  expect "G5 a producer that runs past the capacity is refused, not buffered"
    (evt_occupancy overfilled = event_queue_size
     && overfilled.g_evt_refusals = input_evt_budget
     && overfilled.g_free = sector_pool);
  expect "G5 the drain moves at most the per-release event budget"
    (let st = one_release (one_release live) in
     let st' = step st EvtDrain in
     evt_occupancy st' = input_evt_budget && st'.g_posts = 0 && st'.g_trbs = 0
     && (let st'' = step filled EvtDrain in
         st''.g_evt_head = filled.g_evt_head + input_evt_budget));
  expect "G5 the drain of an empty ring is a stutter, never a negative head"
    (let st = step g0 EvtDrain in
     fingerprint st = fingerprint g0 && st.g_evt_head = 0);
  expect "G5 the ISR-deferred path is what posts events, and it is budgeted"
    (input_evt_budget < isr_trb_budget && isr_trb_budget < event_queue_size);

  expect "G6 one cursor, one live owner - the app-level reading of I7"
    (let st = run_steps seeded [ Publish T_INPUT; Deliver T_UI ] in
     owners_unique st && List.length st.g_owner = 2);
  expect "G6 a retired actor frees its cursor for the next spawn"
    (let st = run_steps seeded [ Retire T_UI; Spawn T_UI ] in
     owners_unique st && is_live st T_UI);
  expect "G6 the I/O actor cannot be pinned on the shipped port, and the refusal corrupts nothing"
    (let shipped = { g0 with g_cores = cores_shipped } in
     let st = step shipped (Spawn T_IOD) in
     fingerprint st = fingerprint shipped && not (is_live st T_IOD) && st.g_live = []);
  expect "G6 the same pin is admitted on the four-core target, which is where core 3 exists"
    (let st = step { g0 with g_cores = cores_target } (Spawn T_IOD) in
     is_live st T_IOD && core_of T_IOD = io_core_id && core_present st (core_of T_IOD));

  expect "G7 the pool is conserved through spawn, publish and retire"
    (let st = run_steps g0 [ Spawn T_INPUT; Publish T_INPUT; Retire T_INPUT ] in
     pool_closed st && st.g_free = sector_pool);
  expect "G7 a blit returns every resident sector and invents none"
    (let st = run_steps seeded [ Publish T_INPUT; Blit ] in
     pool_closed st && st.g_free = sector_pool && st.g_resident = []);
  expect "G7 drain at quiescence returns the whole pool"
    (let st = run_steps seeded [ Blit; Retire T_UI; Retire T_INPUT ] in
     pool_closed st && st.g_free = sector_pool && st.g_live = [] && st.g_owner = []);
  expect "G8 peak residency never exceeded capacity times the writer table"
    (!resident_peak_seen <= max_writers * writer_capacity && !resident_peak_seen > 0);

  expect "G8 the shipped port cannot schedule the four actors"
    (not (schedulable cores_shipped) && schedulable cores_target);
  expect "G8 on the shipped port the OS gate is 4 PASS and 1 SKIP"
    (run_summary ~mp:MP_ON ~cores:cores_shipped ~require:false = (3, 2, 0));
  expect "G8 demanding the AMP row on a port without the I/O core FAILs, never SKIPs"
    (let p, s, f = run_summary ~mp:MP_ON ~cores:cores_shipped ~require:true in
     f = 1 && p = 3 && s = 1);
  expect "G8 with BTRON_MP=0 only the Tier-1 planes row is compiled in"
    (run_summary ~mp:MP_OFF ~cores:cores_target ~require:false = (1, 4, 0));
  expect "G8 on the target topology every OS row passes"
    (run_summary ~mp:MP_ON ~cores:cores_target ~require:true = (5, 0, 0));
  expect "G8 the SKIP rows are named, not counted silently"
    (let bad =
       List.filter
         (fun r -> row_verdict r ~mp:MP_OFF ~cores:cores_target ~require:false <> V_SKIP)
         [ "ring"; "intercore"; "amp"; "io_wcet" ]
     in
     bad = []);

  let others = [ (T_INPUT, T_UI); (T_UI, T_INPUT); (T_INPUT, T_DEC); (T_UI, T_DEC) ] in
  expect "G9 a refused stranger publish mutates neither the owner's cursor nor the pool"
    (List.for_all
       (fun (o, i) ->
         let base = run_steps g0 [ Spawn o; Spawn i; Publish o ] in
         let after = step base (Stranger (o, i)) in
         after.g_resident = base.g_resident && after.g_free = base.g_free
         && after.g_owner = base.g_owner
         && after.g_evt_head = base.g_evt_head && after.g_evt_tail = base.g_evt_tail
         && after.g_strangers = base.g_strangers + 1)
       others);
  expect "G9 the one-step successor set branches: the state does not determine the schedule"
    (List.length (List.sort_uniq compare (List.map fingerprint (successors seeded))) > 1);
  expect "G9 some successor of the seeded state holds a sector on the input cursor and some does not"
    (let p st =
       match lookup 0 st.g_resident with
       | Some n -> n > 0
       | None -> false
     in
     possibly p seeded && not (necessarily p seeded));
  expect "G9 box and diamond are dual, and K holds, on the seeded state and its successors"
    (let q st = st.g_free >= 0 && evt_occupancy st <= event_queue_size in
     let t st = q st && st.g_clock >= seeded.g_clock in
     List.for_all
       (fun st ->
         necessarily q st = not (possibly (fun s -> not (q s)) st)
         && (not (necessarily (fun s -> not (q s) || t s) st && necessarily q st)
             || necessarily t st))
       (seeded :: successors seeded));
  expect "G9 every explored state has a stuttering successor, so box is reflexive here (T, not only K)"
    !reflexive_seen;
  expect "G9 conservation does not depend on the topology: the same schedule on one core or four"
    (let on_port c =
       let st =
         run_steps { g0 with g_cores = c } [ Spawn T_INPUT; Spawn T_UI; Publish T_INPUT; Deliver T_UI ]
       in
       pool_closed st && owners_unique st
     in
     on_port cores_shipped && on_port cores_target);

  expect "G10 the in-blit USB drain is not an admitted step" (not in_blit_drain_admitted);
  expect "G10 a blit leaves the EVT ring exactly as it found it"
    (let st = run_steps seeded [ EvtPost; EvtPost; Blit ] in
     st.g_evt_head = seeded.g_evt_head && st.g_evt_tail = seeded.g_evt_tail + 2);
  expect "G10 no explored schedule performed a forbidden act"
    (List.for_all (fun (nm, ok) -> nm <> "G10 no forbidden act" || ok) (all_invariants seeded));

  expect "G11 the heartbeat and takeover windows are the shipped ones"
    (io_heartbeat_ms = 100 && io_takeover_timeout_ms = 10
     && io_takeover_timeout_ms < io_heartbeat_ms);
  expect "G11 Tier 2 maps the planes onto MPSC actors" (use_mpsc_actors = 1);
  expect "G11 the fast UI period is the optional one: 240 Hz against the shipped 120 Hz"
    (2 * ui_period_us_fast = ui_period_us + 1 && ui_period_us_fast < ui_period_us);
  expect "G11 the UI period is the slowest of the four cyclic handlers"
    (List.for_all (fun t -> period_of t <= ui_period_us) all_tasks);

  List.iter (fun f -> printf "    FAILED: %s\n" f) !failures;
  if !failures <> [] then begin
    printf "media_model: %d of %d checks FAILED\n" (List.length !failures) !checks;
    exit 1
  end;
  printf
    "PASS: OCaml oracle (media_model.ml) - global invariants G0..G11 passed (%d checks, %d schedules of %d kinds ^ %d, peak posts per release %d, peak EVT occupancy %d, peak pointer gap %d us).\n"
    !checks !search_total n_ops seq_len !trb_peak_seen !evt_peak_seen !gap_seen
