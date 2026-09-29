(* media_intercore_model.ml
 *
 * Executable specification and verification oracle for the InterCore protocol,
 * the inter-core messaging fabric that realises the Zero-Switch Actor Topology
 * under the ASYNC planes:
 *
 *   - pub / sub over static cursor tables          (ASYNC.txt §0.1, plan §7.3)
 *   - spawn: core validation and cursor ownership  (plan §7.3, §3b-R1)
 *   - snd: bounded admission, OK / FULL / BAD      (plan §7.3, decision D1)
 *   - rcv: strictly non-blocking, three states     (plan §7.3, ruling R7)
 *   - star topology, one publisher per core, no broker (ASYNC.txt §0.1)
 *   - sector-pool conservation, no post-boot allocation (plan §7.4)
 *   - receipt integrity: no torn read, no replay, no cross-talk
 *   - EVT queue: power-of-two SPSC, one writer per field (IO.txt §5.1-5.2)
 *   - publication has two phases: a store is in the fabric, a publish is visible
 *   - a subscriber's cursor is a floor, and catch-up is bounded by the gap
 *   - mode and gate reporting: SKIP is not PASS    (plan §7.2, §7.4)
 *
 * Normative sources: doc/txt/ASYNC.txt §0.1 for the canonical taxonomy,
 * doc/txt/SMP-AMP-SYNC-IO-HARDENING.txt §7.3 for the R7 resolutions that
 * supersede the earlier text and §7.4 for the metric rows, doc/txt/IO.txt §5
 * for the EVT queue conversion.  The companion proof file is
 * media_intercore_properties.v.
 *
 * OCaml >= 4.14 / 5.x
 *
 * Build & test:
 *   ocamlc -o media_intercore_model media_intercore_model.ml && ./media_intercore_model
 *)

open Printf

(* ── 1. Contract constants ───────────────────────────────────────── *)

(* §3b-R1 and §6.0: no port has a second core running today.  IO.txt §2 makes
 * core 3 the AMP I/O core, so the target topology is four cores. *)
let cores_shipped = 1
let cores_target = 4
let io_core_id = 3

(* IO.txt §6 SR1/SR2: the I/O core free-runs 250 us with a 500 us WCET budget. *)
let io_period_us = 250
let io_budget_us = 500

(* IO.txt §5.1: the EVT queue is 256 deep, a power of two, indexed by mask. *)
let event_queue_size = 256

(* §7.3: cursor ids are dense small ints into static tables - no handle, no
 * pointer escape, so spawn can transfer ownership by value.  The §7.4 example
 * row is pub=4 sub=8, so that is exactly the width of the shipped tables. *)
let max_writers = 4
let max_readers = 8
let max_tasks = 8

(* D1: the transport is a bounded ring, so a full publisher refuses rather than
 * wrapping.  The sector pool is reserved at init and never grows. *)
let writer_capacity = 4
let sector_pool = 64

(* §7.4's shipped ring row.  The three numbers are only evidence as an
 * identity: every message that left the producer was either delivered or
 * dropped, and nothing else is possible. *)
let shipped_enq = 1_000_000
let shipped_deq = 998_412
let shipped_drop = 1_588

(* ── 2. Cursors and the bus state ───────────────────────────────── *)

type cursor_ref = CPub of int | CSub of int

type writer = {
  w_id : int;
  w_live : bool;
  w_cap : int;
  w_enq : int; (* messages accepted; also the next sequence number *)
  w_drop : int; (* messages refused because the queue was full *)
  w_hist : int list; (* payload at absolute sequence number, oldest first *)
}

type reader = {
  r_id : int;
  r_live : bool;
  r_writer : int;
  r_start : int; (* watermark pinned by sub: a late reader never replays *)
  r_cursor : int;
  r_deq : int;
  r_log : int list; (* everything this reader actually received, in order *)
}

type task = { t_id : int; t_live : bool; t_core : int; t_prog : int; t_cursors : cursor_ref list }

type bus = {
  cores : int; (* cores the port actually has, not the cores somebody asked for *)
  writers : writer list;
  readers : reader list;
  tasks : task list;
  pool_free : int;
}

(* The tables exist before the first call: nothing here allocates at run time. *)
let bus0 =
  {
    cores = cores_shipped;
    writers =
      List.init max_writers (fun i ->
          { w_id = i; w_live = false; w_cap = writer_capacity; w_enq = 0; w_drop = 0; w_hist = [] });
    readers =
      List.init max_readers (fun i ->
          { r_id = i; r_live = false; r_writer = -1; r_start = 0; r_cursor = 0; r_deq = 0; r_log = [] });
    tasks = List.init max_tasks (fun i -> { t_id = i; t_live = false; t_core = -1; t_prog = 0; t_cursors = [] });
    pool_free = sector_pool;
  }

let map_writer id f b = { b with writers = List.map (fun w -> if w.w_id = id then f w else w) b.writers }
let map_reader id f b = { b with readers = List.map (fun r -> if r.r_id = id then f r else r) b.readers }
let map_task id f b = { b with tasks = List.map (fun t -> if t.t_id = id then f t else t) b.tasks }

let writer_at b i = if i >= 0 && i < List.length b.writers then Some (List.nth b.writers i) else None
let reader_at b i = if i >= 0 && i < List.length b.readers then Some (List.nth b.readers i) else None

let live_writer b i =
  match writer_at b i with
  | Some w when w.w_live -> Some w
  | _ -> None

let live_reader b i =
  match reader_at b i with
  | Some r when r.r_live -> Some r
  | _ -> None

(* [fst] is still available but [snd] now names the protocol call, so the pair
 * is taken apart by name instead. *)
let state_of bp = match bp with b, _ -> b

(* A subscriber pins to its own publisher's queue.  That is the whole point of
 * the star topology, and the reason no broker can accumulate on the side. *)
let subscribers_of b id = List.filter (fun r -> r.r_live && r.r_writer = id) b.readers

(* How long a message stays resident: until every live subscriber has passed it.
 * A publisher nobody reads holds nothing, because there is no broker to keep it
 * for an absent consumer. *)
let floor_of b w =
  match subscribers_of b w.w_id with
  | [] -> w.w_enq
  | rs -> List.fold_left (fun m r -> min m r.r_cursor) w.w_enq rs

let resident b w = w.w_enq - floor_of b w
let total_resident b = List.fold_left (fun acc w -> if w.w_live then acc + resident b w else acc) 0 b.writers

let cursor_live b cr =
  match cr with
  | CPub i -> live_writer b i <> None
  | CSub i -> live_reader b i <> None

let is_pub cr = match cr with CPub _ -> true | CSub _ -> false
let is_sub cr = match cr with CSub _ -> true | CPub _ -> false
let publishers_in crs = List.length (List.filter is_pub crs)
let subscribers_in crs = List.length (List.filter is_sub crs)

(* ── 3. Ownership: the rule that removes the mutex ──────────────── *)

(* Exclusive ownership is transferred by the spawn argument list, so a cursor
 * owned by a task may be touched by that task and by nobody else - including
 * not by "no task at all", which is the unowned pre-spawn phase.  This is what
 * eliminates shared mutable state, and with it priority inversion between the
 * equal-priority planes. *)
let owner_of b cr =
  match List.filter (fun t -> t.t_live && List.mem cr t.t_cursors) b.tasks with
  | [ t ] -> Some t.t_id
  | _ -> None

let usable b cr by =
  match owner_of b cr with
  | None -> by = None
  | Some t -> by = Some t

(* ASYNC.txt §0.1: each core owns exactly one publisher queue. *)
let core_holds_publisher b core =
  List.exists (fun t -> t.t_live && t.t_core = core && publishers_in t.t_cursors > 0) b.tasks

(* ── 4. pub / sub ───────────────────────────────────────────────── *)

(* pub [capacity]: create a publisher CAS cursor, return the system-wide writer
 * id.  A full static table refuses; it never mints an id out of thin air. *)
let pub b cap =
  match List.find_opt (fun w -> not w.w_live) b.writers with
  | None -> (b, None)
  | Some w -> (map_writer w.w_id (fun _ -> { w with w_live = true; w_cap = cap }) b, Some w.w_id)

(* sub [publisher]: bind a reader cursor to exactly one writer, pinned to that
 * writer's current watermark so a late joiner can never replay history. *)
let sub ?(by = None) b wid =
  match live_writer b wid with
  | None -> (b, None)
  | Some w ->
    if not (usable b (CPub wid) by) then (b, None)
    else
      match List.find_opt (fun r -> not r.r_live) b.readers with
      | None -> (b, None)
      | Some r ->
        ( map_reader r.r_id
            (fun _ -> { r with r_live = true; r_writer = wid; r_start = w.w_enq; r_cursor = w.w_enq })
            b,
          Some r.r_id )

(* ── 5. spawn ───────────────────────────────────────────────────── *)

type spawn_result =
  T_SPAWNED of int
  | T_BAD_CORE
  | T_BAD_CURSOR
  | T_CURSOR_TAKEN
  | T_SHARED_PUBLISHER
  | T_NO_TASK_SLOT

(* spawn [core; program; cursors]: a Task on the named core with exclusive
 * ownership of the listed cursors.  The core id is validated against the cores
 * that actually exist (§3b-R1), and every refusal is inert: a rejected spawn
 * must not leave half-granted ownership behind. *)
let spawn b core prog cursors =
  if core < 0 || core >= b.cores then (b, T_BAD_CORE)
  else if cursors = [] then (b, T_BAD_CURSOR)
  else if List.exists (fun cr -> not (cursor_live b cr)) cursors then (b, T_BAD_CURSOR)
  else if List.exists (fun cr -> owner_of b cr <> None) cursors then (b, T_CURSOR_TAKEN)
  else if publishers_in cursors > 1 then (b, T_SHARED_PUBLISHER)
  else if publishers_in cursors = 1 && core_holds_publisher b core then (b, T_SHARED_PUBLISHER)
  else
    match List.find_opt (fun t -> not t.t_live) b.tasks with
    | None -> (b, T_NO_TASK_SLOT)
    | Some t ->
      ( map_task t.t_id (fun _ -> { t with t_live = true; t_core = core; t_prog = prog; t_cursors = cursors }) b,
        T_SPAWNED t.t_id )

(* ── 6. snd ─────────────────────────────────────────────────────── *)

type snd_result = S_OK | S_FULL of int | S_BAD_CURSOR

(* snd [writer; data]: non-blocking, and it says WHY it failed.  A bare false
 * cannot be told apart from a policy drop, and §7.4 needs the drop count.
 * A sector is taken only for what a subscriber can still be handed, which is
 * what keeps the static pool conserved. *)
let snd ?(by = None) b wid data =
  match live_writer b wid with
  | None -> (b, S_BAD_CURSOR)
  | Some w ->
    if not (usable b (CPub wid) by) then (b, S_BAD_CURSOR)
    else if resident b w >= w.w_cap || b.pool_free <= 0 then
      (map_writer wid (fun x -> { x with w_drop = x.w_drop + 1 }) b, S_FULL (w.w_drop + 1))
    else
      let before = total_resident b in
      let b1 = map_writer wid (fun x -> { x with w_enq = x.w_enq + 1; w_hist = x.w_hist @ [ data ] }) b in
      let held = total_resident b1 - before in
      ({ b1 with pool_free = b1.pool_free - held }, S_OK)

(* ── 7. rcv ─────────────────────────────────────────────────────── *)

type rcv_result = R_DATA of int | R_EMPTY | R_BAD_CURSOR

(* rcv [reader]: STRICTLY non-blocking (R7 supersedes ASYNC.txt §0.1's "yields
 * to scheduler if empty"), because a scheduler yield on the drain path is the
 * sleep/wake transition the Zero-Switch Actor Topology exists to avoid.  So
 * EMPTY goes back to the caller and there is no fourth, blocking case.
 * Advancing the cursor is what releases sectors, so the pool cannot leak. *)
let rcv ?(by = None) b rid =
  match live_reader b rid with
  | None -> (b, R_BAD_CURSOR)
  | Some r -> (
      match live_writer b r.r_writer with
      | None -> (b, R_BAD_CURSOR)
      | Some w ->
        if not (usable b (CSub rid) by) then (b, R_BAD_CURSOR)
        else if r.r_cursor >= w.w_enq then (b, R_EMPTY)
        else
          let payload = List.nth w.w_hist r.r_cursor in
          let before = total_resident b in
          let b1 =
            map_reader rid
              (fun x -> { x with r_cursor = x.r_cursor + 1; r_deq = x.r_deq + 1; r_log = x.r_log @ [ payload ] })
              b
          in
          let after = total_resident b1 in
          ({ b1 with pool_free = b1.pool_free + (before - after) }, R_DATA payload))

(* ── 8. Well-formedness of the bus ──────────────────────────────── *)

let rec drop_n n l =
  match n, l with
  | 0, _ -> l
  | _, [] -> []
  | _, _ :: t -> drop_n (n - 1) t

let rec take_n n l =
  match n, l with
  | 0, _ -> []
  | _, [] -> []
  | _, x :: t -> x :: take_n (n - 1) t

(* Everything a reader is entitled to hold is one contiguous slice of its own
 * publisher's history: no replay, no gap, no cross-talk, no torn read. *)
let entitled b r =
  match writer_at b r.r_writer with
  | None -> `Bad_writer
  | Some w -> `Slice (take_n (r.r_cursor - r.r_start) (drop_n r.r_start w.w_hist))

let cursor_intersect b =
  let seen = ref ([] : cursor_ref list) in
  let hits = ref 0 in
  List.iter
    (fun t ->
      if t.t_live then
        List.iter
          (fun cr ->
            if List.mem cr !seen then incr hits else seen := cr :: !seen)
          t.t_cursors)
    b.tasks;
  !hits

let ownership_exclusive b = if cursor_intersect b = 0 then 1 else 0

(* The ownership map read without the task ids: two grants taken in opposite
 * orders name their tasks differently, and that is not an observable. *)
let cursor_key cr =
  match cr with
  | CPub i -> sprintf "pub%d" i
  | CSub i -> sprintf "sub%d" i

let grants_of b =
  List.map
    (fun t -> if t.t_live then String.concat "+" (List.map cursor_key t.t_cursors) else "")
    b.tasks

let check_bus b : string list =
  let bad = ref ([] : string list) in
  let say s = bad := s :: !bad in
  List.iter
    (fun w ->
      if w.w_id >= max_writers then say "publisher id escapes the static table";
      if w.w_live then begin
        if w.w_enq <> List.length w.w_hist then say "publisher count disagrees with its history";
        if w.w_cap <= 0 then say "publisher has no capacity";
        if resident b w > w.w_cap then say "bounded publisher oversubscribed its capacity";
        if List.exists (fun r -> r.r_live && r.r_writer = w.w_id && r.r_cursor > w.w_enq) b.readers then
          say "subscriber cursor is ahead of the publish watermark"
      end)
    b.writers;
  List.iter
    (fun r ->
      if r.r_id >= max_readers then say "subscriber id escapes the static table";
      if r.r_live then begin
        if live_writer b r.r_writer = None then say "subscriber bound to a publisher that does not exist";
        if r.r_start > r.r_cursor then say "subscriber cursor rewound";
        if r.r_deq <> r.r_cursor - r.r_start then say "delivered count disagrees with the cursor";
        if r.r_deq <> List.length r.r_log then say "receipt log disagrees with the delivered count";
        match entitled b r with
        | `Bad_writer -> ()
        | `Slice s -> if s <> r.r_log then say "receipt log is not a slice of its own publisher's history"
      end)
    b.readers;
  List.iter
    (fun t ->
      if t.t_id >= max_tasks then say "task id escapes the static table";
      if t.t_live then begin
        if t.t_core < 0 || t.t_core >= b.cores then say "live task is parked on a core the port does not have";
        if List.exists (fun cr -> not (cursor_live b cr)) t.t_cursors then say "task owns a cursor that does not exist";
        if List.length (List.sort_uniq Stdlib.compare t.t_cursors) <> List.length t.t_cursors then
          say "task was granted the same cursor twice";
        if publishers_in t.t_cursors > 1 then say "one task owns two publisher queues";
        if List.length t.t_cursors = 0 then say "task was spawned with no cursor to own";
        let others = List.filter (fun o -> o.t_live && o.t_id <> t.t_id) b.tasks in
        List.iter
          (fun cr -> if List.exists (fun o -> List.mem cr o.t_cursors) others then say "two tasks own the same cursor")
          t.t_cursors
      end)
    b.tasks;
  List.iter
    (fun core ->
      let n = List.length (List.filter (fun t -> t.t_live && t.t_core = core && publishers_in t.t_cursors > 0) b.tasks) in
      if n > 1 then say "star topology broken: a core hosts two publisher queues")
    (List.init b.cores Fun.id);
  if b.pool_free + total_resident b <> sector_pool then say "sector pool leaks: free + resident != reserved";
  if b.pool_free < 0 || b.pool_free > sector_pool then say "sector pool count out of range";
  if cursor_intersect b <> 0 then say "cursor ownership intersects";
  List.rev !bad

(* ── 9. The §7.4 row, in its own terms ──────────────────────────── *)

let pub_count b = List.length (List.filter (fun w -> w.w_live) b.writers)
let sub_count b = List.length (List.filter (fun r -> r.r_live) b.readers)
let task_count b = List.length (List.filter (fun t -> t.t_live) b.tasks)
let accepted b = List.fold_left (fun acc w -> acc + w.w_enq) 0 b.writers
let dropped b = List.fold_left (fun acc w -> acc + w.w_drop) 0 b.writers
let delivered b = List.fold_left (fun acc r -> acc + r.r_deq) 0 b.readers

(* ── 10. EVT queue: the power-of-two SPSC ring of IO.txt §5.1 ───── *)

(* head is consumer-owned, tail is producer-owned, indices are masked and never
 * divided.  Each field has exactly one writer (§5.2), which is what lets the
 * two cores share the queue without a lock. *)
type evt = { buf : int list; head : int; tail : int }

let evt_idx i = i land (event_queue_size - 1)
let evt_empty = -1
let evt0 = { buf = List.init event_queue_size (fun _ -> evt_empty); head = 0; tail = 0 }
let evt_occupancy e = e.tail - e.head

let evt_put e v =
  if evt_occupancy e >= event_queue_size then (e, false)
  else
    ( {
        e with
        buf = List.mapi (fun i s -> if i = evt_idx e.tail then v else s) e.buf;
        tail = e.tail + 1;
      },
      true )

let evt_get e =
  if e.head = e.tail then (e, None)
  else
    let slot = List.nth e.buf (evt_idx e.head) in
    ( { e with buf = List.mapi (fun i s -> if i = evt_idx e.head then evt_empty else s) e.buf; head = e.head + 1 },
      Some slot )

(* The shipped put is store-then-publish.  Taken apart, the two halves show the
 * visibility gap the hardware actually has: the store leaves the item in the
 * fabric without telling anybody, and only the publish moves the tail the
 * consumer reads.  §25 of the proof file makes I15 out of this split. *)
let evt_store e v =
  { e with buf = List.mapi (fun i s -> if i = evt_idx e.tail then v else s) e.buf }

let evt_publish e = { e with tail = e.tail + 1 }

(* ── 11. Modes and gates: SKIP is not PASS (plan §7.2, §7.4) ────── *)

type mp_gate = MP_ON | MP_OFF
type verdict = V_PASS | V_SKIP | V_FAIL
type summary = { passes : int; skips : int; fails : int }

(* The five rows §7.4 prints for --mode=full. *)
let full_rows = [ "ring"; "intercore"; "planes"; "amp"; "io_wcet" ]

let row_verdict row ~mp ~cores ~require =
  let compiled_in = match mp with MP_OFF -> [ "planes" ] | MP_ON -> full_rows in
  if not (List.mem row compiled_in) then V_SKIP
  else
    match row with
    | "planes" -> V_PASS (* Tier-1 contract: it must pass with the flag OFF *)
    | "ring" | "intercore" | "io_wcet" -> if cores >= 1 then V_PASS else V_SKIP
    | "amp" ->
      if cores > io_core_id then V_PASS
      else if require then V_FAIL (* --require=amp without the core fails the run *)
      else V_SKIP
    | _ -> V_FAIL

let run_summary ~mp ~cores ~require =
  let vs = List.map (fun r -> row_verdict r ~mp ~cores ~require) full_rows in
  let n v = List.length (List.filter (( = ) v) vs) in
  { passes = n V_PASS; skips = n V_SKIP; fails = n V_FAIL }

(* §7.2: --cores=N is a REQUEST.  The row echoes both numbers, because a figure
 * without its memory model is not evidence (§3b-R3). *)
let cores_row req have = sprintf "cores_req=%d cores_have=%d" req have

(* ── 12. The gap, and draining it in a bounded number of reads ──── *)

(* How far a subscriber has been left behind: its publisher's watermark minus
 * its own cursor.  I16's headline is the exact arithmetic of a bounded catch-up
 * run over this quantity. *)
let pending b rid =
  match reader_at b rid with
  | Some r -> (match live_writer b r.r_writer with Some w -> w.w_enq - r.r_cursor | None -> 0)
  | None -> 0

(* read_k: k drains back to back, the way an actor loops over its queue until it
 * runs dry or its budget runs out. *)
let rec read_k b rid k =
  match k with
  | 0 -> b
  | j -> read_k (state_of (rcv b rid)) rid (j - 1)

(* ── 13. Exhaustive interleaving search ─────────────────────────── *)

(* Eight calls over six positions: all 262144 sequences are run from the seeded
 * bus, which reaches admission, bounded refusal, drop, late subscription, both
 * ownership paths and the parked-core error. *)
let n_ops = 8
let seq_len = 6

type outcome =
  O_PUB
  | O_SUB
  | O_OK
  | O_FULL
  | O_DATA
  | O_EMPTY
  | O_BAD_CURSOR
  | O_BAD_CORE
  | O_TAKEN
  | O_SHARED
  | O_NO_SLOT

let reached = ref ([] : string list)

let reach o =
  let s =
    match o with
    | O_PUB -> "pub"
    | O_SUB -> "sub"
    | O_OK -> "snd OK"
    | O_FULL -> "snd FULL"
    | O_DATA -> "rcv DATA"
    | O_EMPTY -> "rcv EMPTY"
    | O_BAD_CURSOR -> "BAD_CURSOR"
    | O_BAD_CORE -> "BAD_CORE"
    | O_TAKEN -> "CURSOR_TAKEN"
    | O_SHARED -> "SHARED_PUBLISHER"
    | O_NO_SLOT -> "table full"
  in
  if not (List.mem s !reached) then reached := s :: !reached

let payload b = 7 * (match live_writer b 0 with Some w -> w.w_enq | None -> 0) + 3

let apply_op k b =
  match k with
  | 0 ->
    let b', id = pub b writer_capacity in
    (b', if id <> None then O_PUB else O_NO_SLOT)
  | 1 ->
    let b', id = sub b 0 in
    (b', if id <> None then O_SUB else O_NO_SLOT)
  | 2 ->
    let b', r = snd b 0 (payload b) in
    (b', match r with S_OK -> O_OK | S_FULL _ -> O_FULL | S_BAD_CURSOR -> O_BAD_CURSOR)
  | 3 ->
    let b', r = rcv b 0 in
    (b', match r with R_DATA _ -> O_DATA | R_EMPTY -> O_EMPTY | R_BAD_CURSOR -> O_BAD_CURSOR)
  | 4 ->
    let b', r = snd b 1 (payload b + 1) in
    (b', match r with S_OK -> O_OK | S_FULL _ -> O_FULL | S_BAD_CURSOR -> O_BAD_CURSOR)
  | 5 ->
    let b', r = spawn b 0 1 [ CPub 0 ] in
    ( b',
      match r with
      | T_SPAWNED _ -> O_OK
      | T_BAD_CORE -> O_BAD_CORE
      | T_CURSOR_TAKEN -> O_TAKEN
      | T_SHARED_PUBLISHER -> O_SHARED
      | T_NO_TASK_SLOT -> O_NO_SLOT
      | T_BAD_CURSOR -> O_BAD_CURSOR )
  | 6 ->
    let b', r = spawn b 1 2 [ CSub 0 ] in
    ( b',
      match r with
      | T_SPAWNED _ -> O_OK
      | T_BAD_CORE -> O_BAD_CORE
      | T_CURSOR_TAKEN -> O_TAKEN
      | T_SHARED_PUBLISHER -> O_SHARED
      | T_NO_TASK_SLOT -> O_NO_SLOT
      | T_BAD_CURSOR -> O_BAD_CURSOR )
  | _ ->
    let b', r = spawn b 0 3 [ CPub 1 ] in
    ( b',
      match r with
      | T_SPAWNED _ -> O_OK
      | T_SHARED_PUBLISHER -> O_SHARED
      | T_CURSOR_TAKEN -> O_TAKEN
      | T_NO_TASK_SLOT -> O_NO_SLOT
      | T_BAD_CORE -> O_BAD_CORE
      | T_BAD_CURSOR -> O_BAD_CURSOR )

(* The search starts from a bus that already has one publisher, one pinned
 * subscriber and three messages in flight, so that a handful of positions is
 * enough to reach the bound, both ownership refusals and the parked core. *)
let bus_seeded =
  let b1 = state_of (pub bus0 writer_capacity) in
  let b2 = state_of (sub b1 0) in
  List.fold_left (fun acc i -> state_of (snd acc 0 i)) b2 [ 1; 2; 3 ]

let search_total = ref 0
let first_violation = ref ("" : string)
let max_resident_seen = ref 0
let pool_below_seen = ref false

(* The modal layer reads the same tree this search walks.  A law that holds
 * "always" holds at every state the closure reaches, so these accumulators are
 * the box over the whole alphabet rather than a fresh sample of it. *)
let visited_states = ref 0
let floor_violation = ref ("" : string)
let pool_band_low = ref sector_pool
let pool_band_high = ref 0

(* every reachable state: 1 root plus n_ops children per node, down to seq_len *)
let closure_size base depth =
  let rec go acc n k = if n = 0 then acc else go (acc + k) (n - 1) (k * base) in
  go 1 depth base

let rec explore depth b =
  visited_states := !visited_states + 1;
  if b.pool_free < !pool_band_low then pool_band_low := b.pool_free;
  if b.pool_free > !pool_band_high then pool_band_high := b.pool_free;
  if total_resident b > !max_resident_seen then max_resident_seen := total_resident b;
  if b.pool_free < sector_pool then pool_below_seen := true;
  if depth = seq_len then incr search_total
  else
    for k = 0 to n_ops - 1 do
      let b', o = apply_op k b in
      reach o;
      (* the watermark is a floor: a cursor never recedes and a live reader is
       * never silently retired, whatever step was taken *)
      List.iter
        (fun r ->
          match reader_at b' r.r_id with
          | Some r' when r.r_cursor > r'.r_cursor || (r.r_live && not r'.r_live) ->
            if !floor_violation = "" then
              floor_violation := sprintf "reader %d receded at depth %d" r.r_id depth
          | _ -> ())
        b.readers;
      match check_bus b' with
      | [] -> explore (depth + 1) b'
      | s :: _ -> if !first_violation = "" then first_violation := sprintf "op %d at depth %d: %s" k depth s
    done

let pow_int base e =
  let rec go acc n = if n = 0 then acc else go (acc * base) (n - 1) in
  go 1 e

(* ── 14. Oracle ─────────────────────────────────────────────────── *)

let failures = ref ([] : string list)
let checks = ref 0

let expect name cond =
  incr checks;
  if not cond then failures := name :: !failures

let () =
  printf "==> B-System media_intercore model: pub/sub/spawn/snd/rcv cursors, star topology, sector pool\n";

  (* I1. static cursor tables: dense ids, hard capacity, refusal is inert *)
  let b = ref bus0 in
  let ids = ref ([] : int list) in
  for _ = 1 to max_writers do
    let b', id = pub !b writer_capacity in
    b := b';
    match id with
    | Some i -> ids := i :: !ids
    | None -> ()
  done;
  expect "I1 every publisher gets a distinct dense id from the static table"
    (List.length (List.sort_uniq Stdlib.compare !ids) = max_writers && check_bus !b = []);
  let b_over, id_over = pub !b writer_capacity in
  expect "I1 a full publisher table refuses instead of allocating"
    (id_over = None && b_over = !b && pub_count !b = max_writers);
  let sub_ids = ref ([] : int list) in
  for i = 0 to max_writers - 1 do
    for _ = 1 to 2 do
      let b', rid = sub !b i in
      b := b';
      match rid with
      | Some r -> sub_ids := r :: !sub_ids
      | None -> ()
    done
  done;
  expect "I1 eight subscriber slots is exactly the shipped sub=8 row"
    (sub_count !b = max_readers && List.length (List.sort_uniq Stdlib.compare !sub_ids) = max_readers);
  let b_ghost, rid_ghost = sub !b 99 in
  expect "I1 subscribing to a publisher that does not exist is refused and inert" (rid_ghost = None && b_ghost = !b);
  expect "I1 no cursor id ever escapes its table"
    (List.for_all (fun w -> w.w_id < max_writers) !b.writers
     && List.for_all (fun r -> r.r_id < max_readers) !b.readers);

  (* I2. snd admission: the three contract states, and what each one touches.
   * A subscriber pins the tail, and only then does the bound become visible. *)
  let one = state_of (sub (state_of (pub bus0 writer_capacity)) 0) in
  let r0 = ref one in
  let n_ok = ref 0 in
  let n_full = ref 0 in
  let last_drop = ref 0 in
  for i = 1 to 10 do
    let b', res = snd !r0 0 (7 * i + 3) in
    r0 := b';
    match res with
    | S_OK -> incr n_ok
    | S_FULL d ->
      incr n_full;
      last_drop := d
    | S_BAD_CURSOR -> ()
  done;
  expect "I2 a bounded publisher accepts exactly its capacity, then refuses"
    (!n_ok = writer_capacity && !n_full = 10 - writer_capacity);
  expect "I2 FULL reports the running drop count, not a bare false" (!last_drop = !n_full);
  let w0 = List.nth !r0.writers 0 in
  expect "I2 accepted plus dropped is every attempt, nothing lost in between"
    (w0.w_enq + w0.w_drop = 10 && w0.w_enq = writer_capacity && w0.w_drop = 6);
  let w_after_full = !r0 in
  let b_idle, idle_res = snd w_after_full 0 999 in
  expect "I2 a refused send moves the watermark and the pool not at all"
    (idle_res = S_FULL 7
     && b_idle.pool_free = w_after_full.pool_free
     && (List.nth b_idle.writers 0).w_enq = (List.nth w_after_full.writers 0).w_enq
     && (List.nth b_idle.writers 0).w_hist = (List.nth w_after_full.writers 0).w_hist);
  let b_bad, bad_res = snd !r0 7 0 in
  expect "I2 snd on an unknown cursor is BAD_CURSOR" (bad_res = S_BAD_CURSOR && b_bad = !r0);
  expect "I2 admission conserves the pool at every step" (check_bus !r0 = [] && !r0.pool_free + total_resident !r0 = sector_pool);

  (* I3. rcv is total and strictly non-blocking *)
  let primed = ref (state_of (pub bus0 writer_capacity)) in
  for i = 1 to writer_capacity do
    primed := state_of (snd !primed 0 i)
  done;
  let late = ref !primed in
  let l_id = ref (-1) in
  let bl, rl = sub !late 0 in
  late := bl;
  (match rl with Some i -> l_id := i | None -> ());
  let b_l, r_l = rcv !late !l_id in
  expect "I3 a subscriber created after the sends pins to the watermark and gets EMPTY"
    (r_l = R_EMPTY && b_l = !late && !l_id >= 0);
  let early = ref (state_of (pub bus0 writer_capacity)) in
  let e_id = ref (-1) in
  let be, re = sub !early 0 in
  early := be;
  (match re with Some i -> e_id := i | None -> ());
  let received = ref ([] : int list) in
  for i = 1 to writer_capacity do
    early := state_of (snd !early 0 i);
    let b', res = rcv !early !e_id in
    early := b';
    match res with
    | R_DATA v -> received := v :: !received
    | _ -> failures := "I3 a queued message must be delivered, not skipped" :: !failures
  done;
  expect "I3 the queue drains in exactly the order it was filled" (List.rev !received = [ 1; 2; 3; 4 ]);
  let b_e2, r_e2 = rcv !early !e_id in
  expect "I3 an exhausted reader gets EMPTY and leaves the bus untouched" (r_e2 = R_EMPTY && b_e2 = !early);
  let b_e3, r_e3 = rcv !early 42 in
  expect "I3 rcv on an unknown reader is BAD_CURSOR, never a fourth state" (r_e3 = R_BAD_CURSOR && b_e3 = !early);
  let d_prep = state_of (pub bus0 writer_capacity) in
  let d_prep = state_of (sub d_prep 0) in
  let d_prep = state_of (snd d_prep 0 42) in
  let d_bus, d_res = rcv d_prep 0 in
  expect "I3 all three states have an inhabitant, and only DATA moves the bus"
    (d_res = R_DATA 42 && r_e2 = R_EMPTY && r_e3 = R_BAD_CURSOR
     && d_bus.readers <> d_prep.readers && b_e2 = b_e3);

  (* I4. receipt integrity: what a reader holds is a slice of its own publisher *)
  let two = ref bus0 in
  two := state_of (pub !two writer_capacity);
  two := state_of (pub !two writer_capacity);
  let t1, s0 = sub !two 0 in
  two := t1;
  let t2, s1 = sub !two 1 in
  two := t2;
  two := state_of (snd !two 0 11);
  two := state_of (snd !two 1 22);
  let b4, r4 = rcv !two (Option.get s1) in
  expect "I4 a subscriber receives its own arm's payload, not the other branch of the star"
    (r4 = R_DATA 22 && check_bus b4 = []);
  two := b4;
  for i = 1 to 3 do
    two := state_of (snd !two 0 (i * 100))
  done;
  expect "I4 every reader's log is exactly the history slice it is entitled to"
    (List.for_all
       (fun r ->
         if r.r_live then
           match entitled !two r with
           | `Bad_writer -> false
           | `Slice s -> s = r.r_log && List.length s = r.r_deq
         else true)
       !two.readers
     && check_bus !two = []);
  expect "I4 no cursor rewound and none jumped ahead of the publish watermark"
    (List.for_all
       (fun r ->
         r.r_start <= r.r_cursor
         &&
         match live_writer !two r.r_writer with
         | Some w -> r.r_cursor <= w.w_enq
         | None -> true)
       !two.readers);
  let arm0 = Option.get s0 in
  expect "I4 the arm nobody read still holds nothing of the other arm"
    ((List.nth !two.writers 1).w_enq = 1
     && (List.find (fun r -> r.r_id = Option.get s1) !two.readers).r_log = [ 22 ]
     && (List.find (fun r -> r.r_id = arm0) !two.readers).r_log = []);

  (* I5. star topology: no broker can influence an unrelated publisher *)
  let busA = ref (state_of (pub bus0 writer_capacity)) in
  let busB = ref (state_of (pub bus0 writer_capacity)) in
  busB := state_of (pub !busB writer_capacity);
  let busB', tb = sub !busB 1 in
  busB := busB';
  for i = 1 to 3 do
    busB := state_of (snd !busB 1 (700 + i))
  done;
  let drain b =
    let out = ref ([] : string list) in
    let st = ref b in
    for i = 1 to writer_capacity + 2 do
      let b', r = snd !st 0 (i * 3) in
      st := b';
      out :=
        (match r with S_OK -> "ok" | S_FULL _ -> "full" | S_BAD_CURSOR -> "bad") :: !out
    done;
    (List.rev !out, !st)
  in
  let oa, sa = drain !busA in
  let ob, sb = drain !busB in
  expect "I5 an unrelated publisher cannot change this publisher's admission sequence"
    (oa = ob && List.length oa = writer_capacity + 2);
  expect "I5 and cannot change the bytes it accepted"
    ((List.nth sa.writers 0).w_hist = (List.nth sb.writers 0).w_hist
     && (List.nth sa.writers 0).w_drop = (List.nth sb.writers 0).w_drop);
  let arm = ref sb in
  let arm_saw = ref ([] : int list) in
  for _ = 1 to 4 do
    let b', r = rcv !arm (Option.get tb) in
    arm := b';
    arm_saw := (match r with R_DATA v -> v | R_EMPTY -> -1 | R_BAD_CURSOR -> -2) :: !arm_saw
  done;
  expect "I5 draining one arm leaves the other arm's publisher bit for bit unchanged"
    (List.rev !arm_saw = [ 701; 702; 703; -1 ] && (List.nth (!arm).writers 0) = (List.nth sb.writers 0));
  expect "I5 a publisher with no subscriber holds no sector, because there is no broker"
    (let bare = state_of (snd (state_of (pub bus0 writer_capacity)) 0 5) in
     total_resident bare = 0 && bare.pool_free = sector_pool && check_bus bare = []);

  (* I6. spawn: the core argument is validated against cores that exist *)
  let sb0 = ref bus0 in
  sb0 := state_of (pub !sb0 writer_capacity);
  sb0 := state_of (sub !sb0 0);
  let b_c1, r_c1 = spawn !sb0 cores_shipped 1 [ CSub 0 ] in
  expect "I6 spawn on a parked core is an error, not a quiet migration to core 0" (r_c1 = T_BAD_CORE && b_c1 = !sb0);
  let b_neg, r_neg = spawn !sb0 (-1) 1 [ CSub 0 ] in
  expect "I6 a negative core id is refused" (r_neg = T_BAD_CORE && b_neg = !sb0);
  let b_ph, r_ph = spawn !sb0 0 1 [ CSub 99 ] in
  expect "I6 spawn cannot grant a cursor that does not exist" (r_ph = T_BAD_CURSOR && b_ph = !sb0);
  let b_empty, r_empty = spawn !sb0 0 1 [] in
  expect "I6 spawn with an empty grant list is refused: a task must own something"
    (r_empty = T_BAD_CURSOR && b_empty = !sb0);
  let b_grant, r_grant = spawn !sb0 0 1 [ CSub 0; CPub 0 ] in
  expect "I6 a task is created owning exactly the cursors it was granted"
    (match r_grant with
    | T_SPAWNED t ->
      (match List.find_opt (fun x -> x.t_id = t) b_grant.tasks with
      | Some ty -> ty.t_cursors = [ CSub 0; CPub 0 ] && ty.t_core = 0 && ty.t_live
      | None -> false)
    | _ -> false);
  sb0 := b_grant;
  let b_twice, r_twice = spawn !sb0 0 2 [ CSub 0 ] in
  expect "I6 a cursor already owned cannot be transferred a second time" (r_twice = T_CURSOR_TAKEN && b_twice = !sb0);
  let star = state_of (pub (state_of (pub bus0 writer_capacity)) writer_capacity) in
  let star1 = state_of (spawn star 0 2 [ CPub 0 ]) in
  let b_second, r_second = spawn star1 0 3 [ CPub 1 ] in
  expect "I6 a second publisher on the same core is refused, keeping the star"
    (r_second = T_SHARED_PUBLISHER && b_second = star1);
  let star4 = { star with cores = cores_target } in
  let star4a = state_of (spawn star4 0 2 [ CPub 0 ]) in
  expect "I6 the same grant on a different core is the topology the plan wants"
    (match spawn star4a 1 3 [ CPub 1 ] with
    | b', T_SPAWNED _ -> check_bus b' = [] && task_count b' = 2 && b'.cores = cores_target
    | _ -> false);
  let wide_bus = state_of (pub (state_of (pub bus0 writer_capacity)) writer_capacity) in
  let wide_bus = { wide_bus with cores = cores_target } in
  let b_wide, r_wide = spawn wide_bus 0 4 [ CPub 0; CPub 1 ] in
  expect "I6 one task may not own two publisher queues even on four cores" (r_wide = T_SHARED_PUBLISHER && b_wide = wide_bus);
  expect "I6 the target topology does allow a publisher per core"
    (let step = state_of (spawn wide_bus 0 5 [ CPub 0 ]) in
     let step2 = state_of (spawn step 1 6 [ CPub 1 ]) in
     task_count step2 = 2 && check_bus step2 = [] && ownership_exclusive step2 = 1);
  let fan = ref { bus0 with cores = cores_target } in
  fan := state_of (pub !fan writer_capacity);
  fan := state_of (sub !fan 0);
  fan := state_of (pub !fan writer_capacity);
  fan := state_of (sub !fan 1);
  expect "I6 one task may own several subscriber cursors, as an actor drains its queues"
    (match spawn !fan 1 9 [ CSub 0; CSub 1 ] with
    | b', T_SPAWNED t ->
      (match List.find (fun x -> x.t_id = t) b'.tasks with
      | ty -> subscribers_in ty.t_cursors = 2 && publishers_in ty.t_cursors = 0 && check_bus b' = [])
    | _ -> false);
  expect "I6 every refusal left the ownership map exactly as it was" (check_bus !sb0 = [] && cursor_intersect !sb0 = 0);

  (* I7. exclusive ownership is enforced by the API, not by convention *)
  let owned = ref (state_of (pub bus0 writer_capacity)) in
  owned := state_of (sub !owned 0);
  let own_id = ref (-1) in
  let b_own, r_own = spawn !owned 0 7 [ CPub 0; CSub 0 ] in
  owned := b_own;
  (match r_own with T_SPAWNED t -> own_id := t | _ -> ());
  let b_intr, i_res = snd !owned 0 999 in
  expect "I7 a sender that is not the owner is refused and touches nothing" (i_res = S_BAD_CURSOR && b_intr = !owned);
  let b_intr2, i_res2 = rcv ~by:None !owned 0 in
  expect "I7 an unowned call on an owned cursor is refused too" (i_res2 = R_BAD_CURSOR && b_intr2 = !owned);
  let b_own_snd, o_res = snd ~by:(Some !own_id) !owned 0 1234 in
  expect "I7 the owner keeps full use of its own cursor" (o_res = S_OK && check_bus b_own_snd = []);
  let b_own_rcv, or_res = rcv ~by:(Some !own_id) b_own_snd 0 in
  expect "I7 and the owner receives exactly the value it posted" (or_res = R_DATA 1234 && check_bus b_own_rcv = []);
  owned := b_own_rcv;
  expect "I7 ownership is exclusive across the table: cursor_intersect = 0, ownership_exclusive = 1"
    (ownership_exclusive !owned = 1 && task_count !owned = 1 && !owned = b_own_rcv);
  expect "I7 a stranger task cannot borrow the cursor either"
    (match spawn !owned 0 8 [ CSub 0 ] with
    | b', T_CURSOR_TAKEN -> b' = !owned
    | _ -> false);
  expect "I7 subscribing to an owned publisher is refused: fan-out needs the owner"
    (match sub !owned 0 with
    | b', None -> b' = !owned
    | _ -> false);
  expect "I7 the owner may still widen its own fan-out"
    (match sub ~by:(Some !own_id) !owned 0 with
    | b', Some rid -> List.length (List.filter (fun r -> r.r_live) b'.readers) = 2 && check_bus b' = [] && rid = 1
    | _ -> false);

  (* I8. conservation, in the §7.4 row's own arithmetic *)
  expect "I8 the shipped ring row balances: enq = deq + drop" (shipped_enq = shipped_deq + shipped_drop);
  let s = ref bus0 in
  for _ = 1 to max_writers do
    s := state_of (pub !s writer_capacity)
  done;
  let subs = ref ([] : int list) in
  for i = 0 to max_writers - 1 do
    let b', rid = sub !s i in
    s := b';
    match rid with
    | Some r -> subs := r :: !subs
    | None -> ()
  done;
  let accepted_list = ref 0 in
  for i = 0 to 199 do
    let w = i mod max_writers in
    let b', res = snd !s w (i * 7 + 1) in
    s := b';
    if res = S_OK then incr accepted_list
  done;
  expect "I8 the soak kept every publisher inside its bound" (check_bus !s = [] && accepted !s + dropped !s = 200);
  expect "I8 drops happened only because D1 made the transport bounded, and they are counted"
    (dropped !s > 0 && !accepted_list = accepted !s && !accepted_list = max_writers * writer_capacity);
  expect "I8 the pool was spent exactly on what is still held" (!s.pool_free = sector_pool - total_resident !s);
  let drained = ref !s in
  let progress = ref true in
  while !progress do
    let again = ref false in
    let snapshot = List.filter (fun r -> r.r_live) !drained.readers in
    List.iter
      (fun r ->
        let b', res = rcv !drained r.r_id in
        drained := b';
        match res with
        | R_DATA _ -> again := true
        | _ -> ())
      snapshot;
    progress := !again
  done;
  expect "I8 draining to quiescence empties every queue"
    (total_resident !drained = 0 && List.for_all (fun w -> w.w_live = false || w.w_enq = floor_of !drained w) !drained.writers);
  expect "I8 pool_free returns to its initial value at exit: the leak assertion of §7.4"
    (!drained.pool_free = sector_pool && check_bus !drained = []);
  expect "I8 every accepted message reached every subscriber of its arm"
    (List.for_all
       (fun r ->
         if r.r_live then
           match live_writer !drained r.r_writer with
           | Some w -> r.r_deq = w.w_enq
           | None -> false
         else true)
       !drained.readers);
  expect "I8 the fan-out is lossless: delivered equals accepted summed over the arms"
    (delivered !drained = accepted !drained && delivered !drained = max_writers * writer_capacity);

  (* I9. exhaustive interleaving search over the whole API surface *)
  expect "I9 the search starts from a well-formed bus" (check_bus bus_seeded = [] && total_resident bus_seeded = 3);
  explore 0 bus_seeded;
  expect "I9 no explored interleaving violates a bus invariant" (!first_violation = "");
  expect "I9 the search really covered the space" (!search_total = pow_int n_ops seq_len);
  expect "I9 six positions is enough to reach every call of the surface"
    (List.for_all (fun k -> List.mem k !reached)
       [ "pub"; "sub"; "snd OK"; "snd FULL"; "rcv DATA"; "rcv EMPTY"; "BAD_CURSOR"; "BAD_CORE"; "CURSOR_TAKEN";
         "SHARED_PUBLISHER"; "table full" ]);
  expect "I9 the search reached a bounded refusal" (List.mem "snd FULL" !reached);
  expect "I9 the search reached both a delivery and an empty drain"
    (List.mem "rcv DATA" !reached && List.mem "rcv EMPTY" !reached);
  expect "I9 the search reached the ownership refusals"
    (List.mem "BAD_CURSOR" !reached && List.mem "CURSOR_TAKEN" !reached && List.mem "SHARED_PUBLISHER" !reached);
  expect "I9 the search reached the parked-core refusal" (List.mem "BAD_CORE" !reached);
  expect "I9 sectors really leave the pool under interleaving" !pool_below_seen;
  expect "I9 residency never exceeded capacity times the publisher table"
    (!max_resident_seen <= max_writers * writer_capacity);

  (* I10. modes: SKIP is not PASS, and the gate must not fake coverage *)
  let on_one_core = run_summary ~mp:MP_ON ~cores:cores_shipped ~require:false in
  expect "I10 the documented full row is PASS(4/5 modes, 1 SKIP) on a single-core port"
    (on_one_core.passes = 4 && on_one_core.skips = 1 && on_one_core.fails = 0);
  let forced = run_summary ~mp:MP_ON ~cores:cores_shipped ~require:true in
  expect "I10 --require=amp on a port without the I/O core fails the run instead of skipping"
    (forced.fails = 1 && forced.skips = 0);
  let off_gate = run_summary ~mp:MP_OFF ~cores:cores_shipped ~require:false in
  expect "I10 with BTRON_MP=0 the queue modes are SKIP, never the inline seam passed off as coverage"
    (off_gate.passes = 1 && off_gate.skips = 4 && List.mem "planes" full_rows);
  expect "I10 the Tier-1 planes row is the one that must pass with the flag off"
    (row_verdict "planes" ~mp:MP_OFF ~cores:cores_shipped ~require:false = V_PASS);
  let four_core = run_summary ~mp:MP_ON ~cores:cores_target ~require:true in
  expect "I10 on the target topology every row, AMP included, passes"
    (four_core.passes = 5 && four_core.skips = 0 && four_core.fails = 0);
  expect "I10 the row echoes the request and the truth it has" (cores_row cores_target cores_shipped = "cores_req=4 cores_have=1");

  (* I11. EVT queue: masked indices, one writer per field, FIFO across the wrap *)
  expect "I11 the EVT capacity is a power of two, as IO.txt §5.1 requires"
    (event_queue_size = 256 && event_queue_size = 1 lsl 8 && event_queue_size land (event_queue_size - 1) = 0);
  expect "I11 the mask and the modulo agree for every index the soak reaches"
    (List.for_all (fun k -> evt_idx k = k mod event_queue_size) (List.init 1024 Fun.id));
  let e = ref evt0 in
  let sent_n = ref 0 in
  let got = ref ([] : int list) in
  for i = 1 to 600 do
    let e', admitted = evt_put !e i in
    if admitted then begin
      e := e';
      incr sent_n
    end;
    let e2, v = evt_get !e in
    (match v with
    | Some x ->
      e := e2;
      got := x :: !got
    | None -> ());
    if evt_occupancy !e > event_queue_size then failures := "I11 the SPSC ring oversubscribed" :: !failures
  done;
  expect "I11 the queue survives two wraps in strict FIFO order"
    (List.rev !got = List.init (List.length !got) (fun i -> i + 1) && List.length !got = !sent_n);
  let fullq = ref (List.fold_left (fun acc i -> state_of (evt_put acc i)) evt0 (List.init event_queue_size Fun.id)) in
  expect "I11 a full queue refuses the producer instead of overwriting a live slot"
    (let before = !fullq in
     let after, admitted = evt_put !fullq 9999 in
     not admitted && after = before && evt_occupancy before = event_queue_size);
  expect "I11 each cursor has exactly one writer: the producer never moves head"
    (let p = state_of (evt_put evt0 5) in
     let g = state_of (evt_get evt0) in
     p.head = evt0.head && p.tail = evt0.tail + 1 && g.tail = evt0.tail && g.head = evt0.head);
  expect "I11 an empty queue yields nothing rather than a stale slot"
    (let e1 = state_of (evt_put evt0 7) in
     let e2, v2 = evt_get e1 in
     v2 = Some 7
     &&
     match evt_get e2 with
     | _, None -> e2.head = e2.tail
     | _ -> false);
  expect "I11 the I/O core contract is the shipped one: core 3, 250 us, 500 us WCET"
    (io_core_id = cores_target - 1 && io_period_us = 250 && io_budget_us = 2 * io_period_us);

  (* I12. two calls on disjoint cursors: the order of independent grants is not
   * an observable, which is the whole content of "no lock between the cores".
   * Not that the hardware is ordered - that the laws are observables, and the
   * observables do not depend on the order. *)
  let pair1 = state_of (pub bus0 writer_capacity) in
  let pair2 = state_of (pub pair1 writer_capacity) in
  let pair3 = state_of (sub pair2 0) in
  let pair4 = state_of (sub pair3 1) in
  let pair5 = state_of (snd pair4 0 111) in
  let pair6 = state_of (snd pair5 1 222) in
  let rcv_then_snd = state_of (snd (state_of (rcv pair6 0)) 1 333) in
  let snd_then_rcv = state_of (rcv (state_of (snd pair6 1 333)) 0) in
  expect "I12 a receive on one arm and a send on the other build one identical bus, sector ledger included"
    (rcv_then_snd = snd_then_rcv && check_bus rcv_then_snd = [] && check_bus snd_then_rcv = []
     && rcv_then_snd.pool_free = pair6.pool_free);
  let s0_then_s1 = state_of (snd (state_of (snd pair6 0 444)) 1 555) in
  let s1_then_s0 = state_of (snd (state_of (snd pair6 1 555)) 0 444) in
  expect "I12 two publishers accept their messages in either order and land on the same history"
    (s0_then_s1 = s1_then_s0 && (List.nth s0_then_s1.writers 0).w_hist = [ 111; 444 ]
     && (List.nth s1_then_s0.writers 1).w_hist = [ 222; 555 ]);
  let r0_then_r1 = state_of (rcv (state_of (rcv s0_then_s1 0)) 1) in
  let r1_then_r0 = state_of (rcv (state_of (rcv s0_then_s1 1)) 0) in
  expect "I12 two readers draining their own arms commute, and neither sees the other's arm"
    (r0_then_r1 = r1_then_r0 && r0_then_r1.pool_free = r1_then_r0.pool_free
     && check_bus r1_then_r0 = []
     && (List.nth r0_then_r1.writers 1).w_hist = (List.nth s0_then_s1.writers 1).w_hist);
  let gp, _ = spawn s0_then_s1 0 1 [ CPub 0 ] in
  let gb, rb_grant = spawn gp 0 2 [ CSub 1 ] in
  let gs, _ = spawn s0_then_s1 0 1 [ CSub 1 ] in
  let ge, re_grant = spawn gs 0 2 [ CPub 0 ] in
  expect "I12 the same two ownership grants, taken in opposite orders, leave the same cursors and the same exclusivity"
    ((match rb_grant, re_grant with
      | T_SPAWNED _, T_SPAWNED _ -> true
      | _ -> false)
     && gb.writers = ge.writers && gb.readers = ge.readers
     && List.sort Stdlib.compare (grants_of gb) = List.sort Stdlib.compare (grants_of ge)
     && ownership_exclusive gb = 1 && ownership_exclusive ge = 1
     && check_bus gb = [] && check_bus ge = []);

  (* I13. the modal layer: what "always" costs, and what it buys *)
  expect "I13 the base case of the induction: the empty bus is lawful and holds nothing"
    (check_bus bus0 = [] && total_resident bus0 = 0 && bus0.pool_free = sector_pool);
  expect "I13 bus_wf is always: the closure the box quantifies over is the whole tree, and every state in it is lawful"
    (!first_violation = "" && !visited_states = closure_size n_ops seq_len
     && !search_total = pow_int n_ops seq_len && !visited_states > !search_total);
  expect "I13 always_within_the_pool: free + resident stays conserved inside the reserved band at every reachable state"
    (!pool_band_low >= 0 && !pool_band_high <= sector_pool
     && sector_pool - !pool_band_low = !max_resident_seen
     && !pool_band_low < !pool_band_high && !pool_band_low < sector_pool);
  expect "I13 a step that is not admissible is a stutter, so the closure never grows sideways"
    (let bx, rx = snd s0_then_s1 9 0 in
     let by, ry = rcv s0_then_s1 42 in
     let bz, rz = spawn s0_then_s1 7 1 [ CPub 0 ] in
     rx = S_BAD_CURSOR && bx = s0_then_s1 && ry = R_BAD_CURSOR && by = s0_then_s1
     && rz = T_BAD_CORE && bz = s0_then_s1);

  (* I14. the alphabet is not a function, and the order of calls is visible *)
  let n_snd = state_of (snd pair6 0 7) in
  let n_rcv = state_of (rcv pair6 0) in
  expect "I14 one bus, two admissible steps, two different successors: next is not a function"
    (n_snd <> n_rcv && check_bus n_snd = [] && check_bus n_rcv = []
     && n_snd.pool_free < pair6.pool_free && n_rcv.pool_free > pair6.pool_free);
  expect "I14 the capacity is a free parameter of the alphabet, and every value is its own successor"
    (let ca = state_of (pub bus0 2) in
     let cc = state_of (pub bus0 5) in
     ca <> cc && check_bus ca = [] && check_bus cc = []
     && (List.nth ca.writers 0).w_cap = 2 && (List.nth cc.writers 0).w_cap = 5);
  let ordA1 = state_of (pub bus0 writer_capacity) in
  let ordA2 = state_of (sub ordA1 0) in
  let ordA3 = state_of (snd ordA2 0 1) in
  let ordA4, resA = rcv ordA3 0 in
  let ordB1 = state_of (pub bus0 writer_capacity) in
  let ordB2 = state_of (snd ordB1 0 1) in
  let ordB3, sidB = sub ordB2 0 in
  let ordB4, resB = rcv ordB3 0 in
  expect "I14 the order of sub and snd is observable: the parked reader reads the item, the late one never will"
    (resA = R_DATA 1 && resB = R_EMPTY && ordA4 <> ordB4 && sidB <> None);
  expect "I14 and both orders stay inside the class, so this is not a law being broken"
    (check_bus ordA4 = [] && check_bus ordB4 = [] && ownership_exclusive ordA4 = 1
     && ownership_exclusive ordB4 = 1);
  expect "I14 the difference is the watermark the reader pinned, not bookkeeping"
    (let ra = List.find (fun r -> r.r_id = 0) ordA4.readers in
     let rb = List.find (fun r -> r.r_id = 0) ordB4.readers in
     ra.r_start = 0 && rb.r_start = 1 && ra.r_log = [ 1 ] && rb.r_log = []
     && ra.r_deq = 1 && rb.r_deq = 0);

  (* I15. the cursor is a floor, and publication has two phases *)
  let fl1 = ref ordB4 in
  let fl_seen = ref ([] : int list) in
  for _ = 1 to 3 do
    let b', r = rcv !fl1 0 in
    fl1 := b';
    fl_seen := (match r with R_DATA v -> v | R_EMPTY -> -1 | R_BAD_CURSOR -> -2) :: !fl_seen
  done;
  expect "I15 an item already in the fabric, behind a cursor that joined late, is unreachable at every step"
    (List.rev !fl_seen = [ -1; -1; -1 ] && !fl1 = ordB4
     && (List.nth !fl1.writers 0).w_hist = [ 1 ]
     && (List.find (fun r -> r.r_id = 0) !fl1.readers).r_log = []);
  expect "I15 the watermark is a floor at every state the closure reaches: no cursor receded, no reader died"
    (!floor_violation = "" && !visited_states = closure_size n_ops seq_len);
  expect "I15 a store leaves the item in the fabric and the occupancy alone"
    (let e1 = evt_store evt0 77 in
     List.nth e1.buf (evt_idx 0) = 77 && e1.head = evt0.head && e1.tail = evt0.tail
     && evt_occupancy e1 = 0 && e1 <> evt0);
  expect "I15 and the consumer reading that very queue at that very moment gets nothing"
    (let e1 = evt_store evt0 77 in
     match evt_get e1 with
     | b, None -> b = e1
     | _ -> false);
  expect "I15 only the publish delivers what the store left there, and it adds exactly one to the occupancy"
    (let e2 = evt_publish (evt_store evt0 77) in
     (match evt_get e2 with
      | _, Some v -> v = 77
      | _ -> false)
     && evt_occupancy e2 = 1 && e2.head = evt0.head && e2.tail = evt0.tail + 1);
  expect "I15 the shipped put IS store-then-publish, read in the order the hardware performs it"
    (let p, ok = evt_put evt0 77 in ok && p = evt_publish (evt_store evt0 77));
  expect "I15 so the phases cannot be swapped: publishing first hands out the empty sentinel"
    (match evt_get (evt_store (evt_publish evt0) 77) with
     | _, Some v -> v = evt_empty && v <> 77
     | _ -> false);
  expect "I15 a get never passes the published head: it cannot read what was never published"
    (let e4 = evt_store evt0 9 in
     let _, v = evt_get e4 in
     let e5 = state_of (evt_get (evt_publish e4)) in
     v = None && e5.head = 1 && e5.tail = 1 && evt_occupancy e5 = 0);

  (* I16. bounded catch-up: the gap drains by exactly one per admitted read *)
  let catch1 = state_of (pub bus0 writer_capacity) in
  let catch2 = state_of (sub catch1 0) in
  let catch3 = List.fold_left (fun acc i -> state_of (snd acc 0 (100 + i))) catch2 (List.init writer_capacity (fun j -> j + 1)) in
  let caught = catch3 in
  expect "I16 read_k drains the gap exactly: pending (read_k k) = max 0 (pending - k), for every k"
    (pending caught 0 = writer_capacity
     && List.for_all
          (fun k -> pending (read_k caught 0 k) 0 = max 0 (pending caught 0 - k))
          (List.init (3 * writer_capacity + 1) Fun.id));
  expect "I16 one admitted read moves the gap by exactly one, and leaves the publisher where it stood"
    (let b1 = state_of (rcv caught 0) in
     pending b1 0 = pending caught 0 - 1
     && (List.nth b1.writers 0).w_enq = (List.nth caught.writers 0).w_enq
     && (List.nth b1.writers 0).w_hist = (List.nth caught.writers 0).w_hist
     && (List.nth b1.writers 0).w_drop = (List.nth caught.writers 0).w_drop);
  let drained = read_k caught 0 writer_capacity in
  let drained_reader = List.find (fun r -> r.r_id = 0) drained.readers in
  expect "I16 the drain never loses the subscription: the reader sits exactly where the publisher stood"
    (drained_reader.r_live && drained_reader.r_cursor = writer_capacity
     && pending drained 0 = 0 && check_bus drained = []);
  expect "I16 the drained reader reads empty, and the bus does not move at all"
    (let b', r = rcv drained 0 in r = R_EMPTY && b' = drained);
  expect "I16 a drain longer than the gap stops at the tail instead of coming back from it"
    (let b2 = read_k caught 0 (3 * writer_capacity) in
     let r2 = List.find (fun r -> r.r_id = 0) b2.readers in
     b2 = drained && r2.r_cursor = writer_capacity && r2.r_deq = writer_capacity
     && r2.r_log = [ 101; 102; 103; 104 ] && pending b2 0 = 0);
  expect "I16 no number of reads widens the gap: the drain is monotonically shrinking"
    (List.for_all
       (fun k -> pending (read_k caught 0 (k + 1)) 0 <= pending (read_k caught 0 k) 0)
       (List.init (2 * writer_capacity) Fun.id));
  expect "I16 a send widens the gap of its own subscriber by exactly one, and the pool pays for it"
    (let after_snd = state_of (snd drained 0 999) in
     pending after_snd 0 = pending drained 0 + 1 && check_bus after_snd = []
     && after_snd.pool_free = drained.pool_free - 1);

  List.iter (fun f -> printf "    FAILED: %s\n" f) !failures;
  if !failures <> [] then begin
    printf "media_intercore_model: %d of %d checks FAILED\n" (List.length !failures) !checks;
    exit 1
  end;
  printf
    "PASS: OCaml oracle (media_intercore_model.ml) - InterCore invariants I1..I16 passed (%d checks, %d interleavings).\n"
    !checks !search_total
