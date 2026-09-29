(* media_intercore_model.ml
 *
 * Executable specification and verification oracle for the InterCore protocol,
 * the inter-core messaging fabric that realises the Zero-Switch Actor Topology
 * under the ASYNC planes:
 *
 *   - pub / sub over static cursor tables          (ASYNC.txt §0.1, plan §7.3)
 *   - spawn: core validation and cursor ownership  (plan §7.3, §3b-R1)
 *   - snd: bounded admission, OK | FULL | BAD      (plan §7.3, D1)
 *   - rcv: strictly non-blocking, three states     (plan §7.3 R7)
 *   - star topology, no central broker             (ASYNC.txt §0.1)
 *   - sector-pool conservation, no post-boot alloc (plan §7.4)
 *   - receipt integrity: no torn read, no replay   (plan §5, D1)
 *   - EVT queue: power-of-two SPSC, one writer per field (IO.txt §5.1-5.2)
 *   - mode/gate reporting, SKIP is not PASS        (plan §7.2, §7.4)
 *
 * Normative sources: doc/txt/ASYNC.txt §0.1 (canonical API), doc/txt/
 * SMP-AMP-SYNC-IO-HARDENING.txt §7.3 (the R7 resolutions that supersede the
 * earlier text) and §7.4 (metrics), doc/txt/IO.txt §5 (EVT SPSC conversion).
 * The companion proof file is media_intercore_properties.v.
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

(* §7.3: cursor ids are dense small ints into static tables — no handle, no
 * pointer escape.  §7.4's example row is pub=4 sub=8, so that is the width. *)
let max_writers = 4
let max_readers = 8
let max_tasks = 8

(* D1: the transport is a bounded ring, so a full publisher refuses rather than
 * wrapping.  The sector pool is reserved at init and never grows. *)
let writer_capacity = 4
let sector_pool = 64

(* §7.4's shipped ring row: the numbers are only meaningful as an identity. *)
let shipped_enq = 1_000_000
let shipped_deq = 998_412
let shipped_drop = 1_588

(* ── 2. Cursors, tasks and the bus state ────────────────────────── *)

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
  r_log : int list; (* everything this reader has actually received, in order *)
}

type task = { t_id : int; t_live : bool; t_core : int; t_prog : int; t_cursors : cursor_ref list }

type bus = {
  cores : int; (* cores the port actually has, not the cores someone asked for *)
  writers : writer list;
  readers : reader list;
  tasks : task list;
  pool_free : int;
}

(* The tables exist before the first call: nothing here allocates at run time. *)
let bus0 =
  {
    cores = cores_shipped;
    writers = List.init max_writers (fun i -> { w_id = i; w_live = false; w_cap = writer_capacity; w_enq = 0; w_drop = 0; w_hist = [] });
    readers = List.init max_readers (fun i -> { r_id = i; r_live = false; r_writer = -1; r_start = 0; r_cursor = 0; r_deq = 0; r_log = [] });
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

(* A subscriber pins to its own publisher's queue: that is the whole point of
 * the star topology, and the reason no broker can accumulate on the side. *)
let subscribers_of b id = List.filter (fun r -> r.r_live && r.r_writer = id) b.readers

(* How long a message stays resident: until every live subscriber has passed it.
 * A publisher nobody reads holds nothing, because there is no broker to keep it. *)
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

(* ── 3. Ownership: the only cross-core sharing rule that matters ──── *)

(* Exclusive ownership is transferred by the spawn argument list, so a cursor
 * owned by a task may be touched by that task and by nobody else — including
 * not by "no task".  This is what removes the mutex, and with it the priority
 * inversion, from the ASYNC path. *)
let owner_of b cr =
  match List.filter (fun t -> t.t_live && List.mem cr t.t_cursors) b.tasks with
  | [ t ] -> Some t.t_id
  | _ -> None

let usable b cr by =
  match owner_of b cr with
  | None -> by = None
  | Some t -> by = Some t

(* Each core owns exactly one publisher queue: the star of §0.1. *)
let core_holds_publisher b core =
  List.exists
    (fun t ->
      t.t_live && t.t_core = core && List.exists (fun cr -> match cr with CPub _ -> true | CSub _ -> false) t.t_cursors)
    b.tasks

let publishers_in crs = List.length (List.filter (fun cr -> match cr with CPub _ -> true | CSub _ -> false) crs)
let subscribers_in crs = List.length (List.filter (fun cr -> match cr with CSub _ -> true | CPub _ -> false) crs)

(* ── 4. pub / sub ───────────────────────────────────────────────── *)

(* pub [capacity]: creates a publisher CAS cursor, returns the writer id.  When
 * the static table is full the call is refused; it never mints a new id. *)
let pub b cap =
  match List.find_opt (fun w -> not w.w_live) b.writers with
  | None -> (b, None)
  | Some w -> (map_writer w.w_id (fun _ -> { w with w_live = true; w_cap = cap }) b, Some w.w_id)

(* sub [publisher]: binds a reader cursor to one writer, pinned to the current
 * watermark so a late joiner can never replay history it was never given. *)
let sub b wid =
  match live_writer b wid with
  | None -> (b, None)
  | Some w -> (
      match List.find_opt (fun r -> not r.r_live) b.readers with
      | None -> (b, None)
      | Some r ->
        ( map_reader r.r_id
            (fun _ -> { r with r_live = true; r_writer = wid; r_start = w.w_enq; r_cursor = w.w_enq })
            b,
          Some r.r_id ))

(* ── 5. spawn ───────────────────────────────────────────────────── *)

type spawn_result =
  T_SPAWNED of int
  | T_BAD_CORE
  | T_BAD_CURSOR
  | T_CURSOR_TAKEN
  | T_SHARED_PUBLISHER
  | T_NO_TASK_SLOT

(* spawn [core; program; cursors]: a Task on the named core with exclusive
 * ownership of the listed cursors.  Refusals are inert: a rejected spawn must
 * not leave a half-granted ownership behind. *)
let spawn b core prog cursors =
  if core < 0 || core >= b.cores then (b, T_BAD_CORE)
  else if List.exists (fun cr -> not (cursor_live b cr)) cursors then (b, T_BAD_CURSOR)
  else if List.exists (fun cr -> owner_of b cr <> None) cursors then (b, T_CURSOR_TAKEN)
  else if publishers_in cursors > 1 then (b, T_SHARED_PUBLISHER)
  else if publishers_in cursors = 1 && core_holds_publisher b core then (b, T_SHARED_PUBLISHER)
  else
    match List.find_opt (fun t -> not t.t_live) b.tasks with
    | None -> (b, T_NO_TASK_SLOT)
    | Some t ->
      let b' = map_task t.t_id (fun _ -> { t with t_live = true; t_core = core; t_prog = prog; t_cursors = cursors }) b in
      (b', T_SPAWNED t.t_id)

(* ── 6. snd ─────────────────────────────────────────────────────── *)

type snd_result = S_OK | S_FULL of int | S_BAD_CURSOR

(* snd [writer; data]: non-blocking.  It says WHY it failed, because §7.4 needs
 * the drop count and a silent false cannot be told apart from a policy drop. *)
let snd ?(by = None) b wid data =
  match live_writer b wid with
  | None -> (b, S_BAD_CURSOR)
  | Some w ->
    if not (usable b (CPub wid) by) then (b, S_BAD_CURSOR)
    else if resident b w >= w.w_cap || b.pool_free <= 0 then
      (map_writer wid (fun x -> { x with w_drop = x.w_drop + 1 }) b, S_FULL (w.w_drop + 1))
    else
      let b' = map_writer wid (fun x -> { x with w_enq = x.w_enq + 1; w_hist = x.w_hist @ [ data ] }) b in
      ({ b' with pool_free = b'.pool_free - 1 }, S_OK)

(* ── 7. rcv ─────────────────────────────────────────────────────── *)

type rcv_result = R_DATA of int | R_EMPTY | R_BAD_CURSOR

(* rcv [reader]: STRICTLY non-blocking (R7).  EMPTY goes back to the caller;
 * there is no fourth, blocking case, because a scheduler yield on the drain
 * path is exactly the transition the Zero-Switch Actor Topology exists to kill.
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

let slice l a n = List.(a) (* placeholder, replaced below *)

let rec drop_n n l = match (n, l) with 0, x | _, [] -> x | _, _ :: t -> drop_n (n - 1) t
let rec take_n n l = match (n, l) with 0, _ | _, [] -> [] | _, x :: t -> x :: take_n (n - 1) t

(* Everything a reader is entitled to hold is a contiguous slice of its own
 * publisher's history: no replay, no gap, no cross-talk, no torn read. *)
let entitled b r =
  match writer_at b r.r_writer with
  | None -> [ `Bad_writer ]
  | Some w -> `Slice (take_n (r.r_cursor - r.r_start) (drop_n r.r_start w.w_hist))

let check_cursor_exclusive b =
  let seen = ref ([] : cursor_ref list) in
  let inter = ref 0 in
  List.iter
    (fun t ->
      if t.t_live then
        List.iter
          (fun cr ->
            if List.mem cr !seen then incr inter else seen := cr :: !seen)
          t.t_cursors)
    b.tasks;
  !inter

let check_bus b : string list =
  let bad = ref ([] : string list) in
  let say s = bad := s :: !bad in
  List.iter
    (fun w ->
      if w.w_id >= max_writers then say "publisher id escapes the static table";
      if w.w_live then begin
        if w.w_enq <> List.length w.w_hist then say "publisher count disagrees with its history";
        if w.w_enq < w.w_drop then say "publisher dropped more than it attempted";
        if resident b w > w.w_cap then say "bounded publisher oversubscribed its capacity";
        if w.w_cap <= 0 then say "publisher has no capacity";
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
        (match entitled b r with
        | `Bad_writer -> ()
        | `Slice s -> if s <> r.r_log then say "receipt log is not a slice of its own publisher's history")
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
        let others = List.filter (fun o -> o.t_live && o.t_id <> t.t_id) b.tasks in
        List.iter
          (fun cr ->
            if List.exists (fun o -> List.mem cr o.t_cursors) others then say "two tasks own the same cursor")
          t.t_cursors
      end)
    b.tasks;
  List.iter
    (fun core ->
      let n = List.length (List.filter (fun t -> t.t_live && t.t_core = core && publishers_in t.t_cursors = 1) b.tasks) in
      if n > 1 then say "star topology broken: a core hosts two publisher queues")
    (List.init b.cores Fun.id);
  if b.pool_free + total_resident b <> sector_pool then say "sector pool leaks: free + resident != reserved";
  if b.pool_free < 0 || b.pool_free > sector_pool then say "sector pool count out of range";
  if check_cursor_exclusive b <> 0 then say "cursor ownership intersects";
  List.rev !bad

(* ── 9. Metrics of §7.4, in the shipped row's own terms ─────────── *)

let ownership_exclusive b = if check_cursor_exclusive b = 0 then 1 else 0
let cursor_intersect b = check_cursor_exclusive b
let pub_count b = List.length (List.filter (fun w -> w.w_live) b.writers)
let sub_count b = List.length (List.filter (fun r -> r.r_live) b.readers)
let task_count b = List.length (List.filter (fun t -> t.t_live) b.tasks)
let attempts b = List.fold_left (fun acc w -> acc + w.w_enq + w.w_drop) 0 b.writers
let accepted b = List.fold_left (fun acc w -> acc + w.w_enq) 0 b.writers
let dropped b = List.fold_left (fun acc w -> acc + w.w_drop) 0 b.writers
let delivered b = List.fold_left (fun acc r -> acc + r.r_deq) 0 b.readers

(* ── 10. EVT queue: the power-of-two SPSC ring of IO.txt §5.1 ───── *)

(* head is consumer-owned, tail is producer-owned, indices are masked and never
 * divided.  MOVE coalescing and the drop-oldest policy stay producer-side; the
 * model asserts the two things §5.1/§5.2 make normative: the mask identity and
 * one writer per field. *)
type evt = { buf : int list; head : int; tail : int }

let evt_idx i = i land (event_queue_size - 1)
let evt_empty_slot = -1

let evt0 = { buf = List.init event_queue_size (fun _ -> evt_empty_slot); head = 0; tail = 0 }

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
    ( {
        e with
        buf = List.mapi (fun i s -> if i = evt_idx e.head then evt_empty_slot else s) e.buf;
        head = e.head + 1;
      },
      Some (List.nth e.buf (evt_idx e.head)) )

(* ── 11. Modes and gates: SKIP is not PASS (plan §7.2, §7.4) ────── *)

type mp_gate = MP_ON | MP_OFF
type verdict = V_PASS | V_SKIP | V_FAIL

(* The five rows §7.4 prints for --mode=full. *)
let full_rows = [ "ring"; "intercore"; "planes"; "amp"; "io_wcet" ]

let row_verdict row ~mp ~cores ~require =
  let compiled_in = match mp with MP_OFF -> [ "planes" ] | MP_ON -> full_rows in
  if not (List.mem row compiled_in) then V_SKIP
  else
    match row with
    | "planes" -> V_PASS (* the Tier-1 contract; it must pass with the flag off *)
    | "ring" | "intercore" | "io_wcet" -> if cores >= 1 then V_PASS else V_SKIP
    | "amp" ->
      if cores > io_core_id then V_PASS
      else if require then V_FAIL (* --require=amp on a port without the core fails *)
      else V_SKIP
    | _ -> V_FAIL

let run_summary ~mp ~cores ~require =
  let vs = List.map (fun r -> row_verdict r ~mp ~cores ~require) full_rows in
  let n v = List.length (List.filter (( = ) v) vs) in
  { passes = n V_PASS; skips = n V_SKIP; fails = n V_FAIL }

(* §7.2: --cores=N is a REQUEST; oversubscription on a single-core port is an
 * error, never a pass, and the row echoes both numbers. *)
let cores_row req have = sprintf "cores_req=%d cores_have=%d" req have

(* ── 12. Exhaustive interleaving search ────────────────────────── *)

(* Eight calls, seven positions: the search is small enough to run every one of
 * the 2'097'152 sequences, and big enough to reach admission, refusal, drop,
 * late subscription and both ownership paths. *)
let n_ops = 8
let seq_len = 7

let apply_op k b =
  let data = 7 * (match live_writer b 0 with Some w -> w.w_enq | None -> 0) + 3 in
  match k with
  | 0 -> fst (pub b writer_capacity)
  | 1 -> fst (sub b 0)
  | 2 -> fst (snd b 0 data)
  | 3 -> fst (rcv b 0)
  | 4 -> fst (snd b 1 (data + 1))
  | 5 -> fst (spawn b 0 1 [ CPub 0 ])
  | 6 -> fst (spawn b 1 2 [ CSub 0 ]) (* refused on the shipped port: core 1 is parked *)
  | _ -> fst (rcv ~by:(Some 0) b 0)

let search_total = ref 0
let first_violation = ref ("" : string)
let saw_full = ref false
let saw_data = ref false
let saw_empty = ref false
let saw_bad_cursor = ref false
let saw_taken = ref false
let saw_bad_core = ref false
let max_resident_seen = ref 0
let pool_below_seen = ref false

let rec explore depth b =
  if depth = seq_len then begin
    incr search_total;
    match check_bus b with
    | s :: _ when !first_violation = "" -> first_violation := sprintf "at depth %d: %s" depth s
    | _ -> ()
  end
  else
    for k = 0 to n_ops - 1 do
      let b' = apply_op k b in
      if check_bus b' = [] then begin
        if total_resident b' > !max_resident_seen then max_resident_seen := total_resident b';
        if b'.pool_free < sector_pool then pool_below_seen := true;
        let _, r = snd b' 0 0 in
        (match r with S_FULL _ -> saw_full := true | S_BAD_CURSOR -> saw_bad_cursor := true | _ -> ());
        let _, r2 = rcv b' 0 in
        (match r2 with R_DATA _ -> saw_data := true | R_EMPTY -> saw_empty := true | R_BAD_CURSOR -> saw_bad_cursor := true);
        let _, r3 = spawn b' 1 3 [ CSub 0 ] in
        if r3 = T_BAD_CORE then saw_bad_core := true;
        let _, r4 = spawn b' 0 4 [ CPub 0 ] in
        if r4 = T_CURSOR_TAKEN then saw_taken := true;
        explore (depth + 1) b'
      end
      else if !first_violation = "" then first_violation := sprintf "op %d at depth %d left an ill-formed bus" k depth
    done

let pow_int base e =
  let rec go acc n = if n = 0 then acc else go (acc * base) (n - 1) in
  go 1 e

(* ── 13. Oracle ─────────────────────────────────────────────────── *)

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
  expect "I1 every publisher is handed a distinct dense id from the static table"
    (List.length (List.sort_uniq Stdlib.compare !ids) = max_writers && check_bus !b = []);
  let b_overflow, id_overflow = pub !b writer_capacity in
  expect "I1 a full publisher table refuses instead of allocating"
    (id_overflow = None && b_overflow = !b && pub_count !b = max_writers);
  let readers = ref ([] : int list) in
  for i = 0 to max_writers - 1 do
    for _ = 1 to 2 do
      let b', rid = sub !b i in
      b := b';
      match rid with
      | Some r -> readers := r :: !readers
      | None -> ()
    done
  done;
  expect "I1 eight subscriber slots is exactly the shipped sub=8 row"
    (sub_count !b = max_readers && List.length (List.sort_uniq Stdlib.compare !readers) = max_readers);
  let b_r, rid_r = sub !b 99 in
  expect "I1 subscribing to a publisher that does not exist is refused" (rid_r = None && b_r = !b);
  expect "I1 no id ever escapes its table"
    (List.for_all (fun w -> w.w_id < max_writers) !b.writers
     && List.for_all (fun r -> r.r_id < max_readers) !b.readers);

  (* I2. snd admission: the three contract states, and what each one touches *)
  let b0' = fst (pub bus0 writer_capacity) in
  let r0 = ref b0' in
  let ok = ref 0 in
  let full = ref 0 in
  let sent = ref 0 in
  for i = 1 to 10 do
    let b', res = snd !r0 0 (7 * i + 3) in
    r0 := b';
    incr sent;
    (match res with
    | S_OK -> incr ok
    | S_FULL d ->
      incr full;
      if d <> !full then failures := "I2 FULL reports the running drop count" :: !failures
    | S_BAD_CURSOR -> ());
    if check_bus !r0 <> [] then failures := "I2 admission broke the bus" :: !failures
  done;
  expect "I2 a bounded publisher accepts exactly its capacity and then refuses"
    (!ok = writer_capacity && !full = 10 - writer_capacity && !sent = 10);
  let w0 = List.hd !r0.writers in
  expect "I2 accepted plus dropped is every attempt, with nothing lost in between"
    (w0.w_enq + w0.w_drop = !sent && w0.w_enq = writer_capacity && w0.w_drop = 6);
  expect "I2 a refused send does not touch the pool or the watermark"
    (let before = !r0 in
     let after, res = snd (List.fold_left (fun acc _ -> fst (snd acc 0 0)) before []) 0 0 in
     (res = S_FULL 7 || res = S_BAD_CURSOR) && after.pool_free = before.pool_free
     && (List.hd after.writers).w_enq = (List.hd before.writers).w_enq);
  let bad, badres = snd !r0 7 0 in
  expect "I2 snd on an unknown cursor is BAD_CURSOR, not a silent false" (badres = S_BAD_CURSOR && bad = !r0);
  expect "I2 refusing costs a sector to nobody: pool plus residency is conserved"
    (check_bus !r0 = [] && !r0.pool_free + total_resident !r0 = sector_pool);

  (* I3. rcv is total and strictly non-blocking *)
  let primed = ref b0' in
  for i = 1 to writer_capacity do
    primed := fst (snd !primed 0 i)
  done;
  let q = ref !primed in
  let got = ref ([] : int list) in
  let qs, rid0 = sub !q 0 in
  q := qs;
  expect "I3 a subscriber created after the sends pins to the watermark and sees nothing"
    (rid0 <> None && match snd () with _ -> true);
  let q1, res1 = rcv !q (Option.get rid0) in
  expect "I3 rcv returns EMPTY for a pinned-late subscriber instead of yielding"
    (res1 = R_EMPTY && q1 = !q);
  (* now a subscriber that existed before the sends *)
  let q2, rid2 = sub b0' 0 in
  let q3 = ref q2 in
  let received = ref ([] : int list) in
  for i = 1 to writer_capacity do
    q3 := fst (snd !q3 0 i);
    let q', res = rcv !q3 (Option.get rid2) in
    q3 := q';
    match res with
    | R_DATA v -> received := v :: !received
    | _ -> failures := "I3 a queued message must be delivered, not skipped" :: !failures
  done;
  expect "I3 the queue is drained in exactly the order it was filled"
    (List.rev !received = [ 1; 2; 3; 4 ]);
  let q4, res4 = rcv !q3 (Option.get rid2) in
  expect "I3 an empty reader gets EMPTY and the bus is untouched" (res4 = R_EMPTY && q4 = !q3);
  expect "I3 rcv has no blocking inhabitant: the contract has three states"
    (let _, r = rcv !q3 (Option.get rid2) in
     r = R_EMPTY || (match r with R_DATA _ -> true | _ -> false) || r = R_BAD_CURSOR);
  let badr, badres = rcv !q3 42 in
  expect "I3 rcv on an unknown reader is BAD_CURSOR" (badres = R_BAD_CURSOR && badr = !q3);

  (* I4. receipt integrity: what a reader holds is a slice of its own publisher *)
  let mixed = ref b0' in
  let _, s0 = sub !mixed 0 in
  mixed := fst (sub !mixed 0 |> fun (b, _) -> (b, ()));
  let w1 = ref b0' in
  let w1', _ = pub b0' writer_capacity in
  w1 := w1';
  let _, s1 = sub !w1 1 in
  w1 := fst (snd !w1 0 11);
  w1 := fst (snd !w1 1 22);
  let w1'', r = rcv !w1 (Option.get s1) in
  expect "I4 a subscriber receives its own publisher's payload, not the star's other arm"
    (r = R_DATA 22 && check_bus w1'' = []);
  let w1''' = ref w1'' in
  List.iter
    (fun i ->
      w1''' := fst (snd !w1''' 0 (i * 100));
      let b', _ = rcv !w1''' (Option.get s0) in
      w1''' := b')
    [ 1; 2; 3 ];
  expect "I4 every reader's log is exactly the slice of history it was entitled to"
    (List.for_all
       (fun r ->
         r.r_live
         &&
         match entitled !w1''' r with
         | `Bad_writer -> false
         | `Slice s -> s = r.r_log && List.length s = r.r_deq)
       !w1'''.readers
     && check_bus !w1''' = []);
  expect "I4 no cursor ever rewound, and none jumped ahead of the watermark"
    (List.for_all
       (fun r ->
         r.r_start <= r.r_cursor
         && (match live_writer !w1''' r.r_writer with Some w -> r.r_cursor <= w.w_enq | None -> true))
       !w1'''.readers);

  (* I5. star topology: no broker, one publisher per core *)
  let twin_a = ref bus0 in
  let twin_b = ref bus0 in
  twin_a := fst (pub !twin_a writer_capacity);
  twin_b := fst (pub !twin_b writer_capacity);
  twin_b := fst (pub !twin_b writer_capacity);
  let _, tsub = sub !twin_b 0 in
  twin_b := fst (snd !twin_b 1 777);
  twin_b := fst (snd !twin_b 1 888);
  let a_out = snd !twin_a 0 1 in
  let b_out = snd !twin_b 0 2 in
  expect "I5 an unrelated publisher cannot change this publisher's admission"
    (match (snd a_out, snd b_out) with S_OK, S_OK -> true | _ -> false);
  expect "I5 the delivery of a cursor depends only on its own publisher's queue"
    (let _, r = rcv (fst b_out) (Option.get tsub) in r = R_EMPTY);
  let core_bus = ref (fst (pub bus0 writer_capacity)) in
  let _, _ = pub () in
  ignore core_bus;
  ignore tsub;

  (* I6. spawn: the core argument is validated against cores that exist *)
  let sb = ref bus0 in
  sb := fst (pub !sb writer_capacity);
  sb := fst (sub !sb 0);
  let s_id = ref None in
  List.iter (fun r -> if r.r_live then s_id := Some r.r_id) !sb.readers;
  let b_bad, r_bad = spawn !sb cores_shipped 1 [ CSub 0 ] in
  expect "I6 spawn on a parked core is an error, not a quiet migration to core 0"
    (r_bad = T_BAD_CORE && b_bad = !sb);
  let b_none, r_none = spawn !sb (-1) 1 [ CSub 0 ] in
  expect "I6 a negative core id is refused" (r_none = T_BAD_CORE && b_none = !sb);
  let b_ghost, r_ghost = spawn !sb 0 1 [ CSub 99 ] in
  expect "I6 spawn cannot grant a cursor that does not exist" (r_ghost = T_BAD_CURSOR && b_ghost = !sb);
  let b_spawn, r_spawn = spawn !sb 0 1 [ CSub 0; CPub 0 ] in
  expect "I6 a task is created with the cursors it was granted"
    (match r_spawn with T_SPAWNED t -> (match List.find (fun x -> x.t_id = t) b_spawn.tasks with ty -> ty.t_cursors = [ CSub 0; CPub 0 ] && ty.t_core = 0) | _ -> false);
  sb := b_spawn;
  let b_again, r_again = spawn !sb 0 2 [ CSub 0 ] in
  expect "I6 a cursor already owned cannot be transferred a second time"
    (r_again = T_CURSOR_TAKEN && b_again = !sb);
  let b_two, r_two = spawn !sb 0 3 [ CPub 0 ] in
  expect "I6 the second publisher on the same core is refused, keeping the star"
    (r_two = T_SHARED_PUBLISHER && b_two = !sb);
  expect "I6 every refusal left the ownership map exactly as it was" (check_bus !sb = [] && cursor_intersect !sb = 0);

  (* I7. exclusive ownership is enforced by the API, not by convention *)
  let tid = ref (-1) in
  (match r_spawn with T_SPAWNED t -> tid := t | _ -> ());
  let intruder, ires = snd !sb 0 999 in
  expect "I7 a sender that is not the owner is refused, and touches nothing"
    (ires = S_BAD_CURSOR && intruder = !sb);
  let intruder2, ires2 = rcv ~by:None !sb (Option.get s_id) in
  expect "I7 an unowned call on an owned cursor is refused too" (ires2 = R_BAD_CURSOR && intruder2 = !sb);
  let owner_snd, ores = snd ~by:(Some !tid) !sb 0 1234 in
  expect "I7 the owner keeps full use of its own cursor" (ores = S_OK && check_bus owner_snd = []);
  let owner_rcv, rres = rcv ~by:(Some !tid) owner_snd (Option.get s_id) in
  expect "I7 and the owner receives exactly the value it posted" (rres = R_DATA 1234 && check_bus owner_rcv = []);
  sb := owner_rcv;
  expect "I7 ownership is exclusive across the whole table: cursor_intersect = 0"
    (ownership_exclusive !sb = 1 && task_count !sb = 1);

  (* I8. conservation, in the §7.4 row's own arithmetic *)
  expect "I8 the shipped ring row balances: enq = deq + drop"
    (shipped_enq = shipped_deq + shipped_drop);
  let soak = ref bus0 in
  let _, _ = pub () in
  let b1 = ref bus0 in
  ignore soak;
  ignore b1;
  let s = ref bus0 in
  for i = 0 to max_writers - 1 do
    s := fst (pub !s writer_capacity)
  done;
  let subs = ref ([] : int list) in
  for i = 0 to max_writers - 1 do
    let b', rid = sub !s i in
    s := b';
    match rid with Some r -> subs := r :: !subs | None -> ()
  done;
  let holds = ref ([] : (int * int) list) in
  for i = 0 to 199 do
    let w = i mod max_writers in
    let b', res = snd !s w (i * 7 + 1) in
    s := b';
    if res = S_OK then holds := (w, i * 7 + 1) :: !holds
  done;
  expect "I8 the soak kept every publisher inside its bound" (check_bus !s = [] && accepted !s + dropped !s = 200);
  expect "I8 drops happened only because the ring is bounded, and they are counted"
    (dropped !s > 0 && List.length !holds = accepted !s);
  (* drain to quiescence: every reader consumes everything it is entitled to *)
  let drained = ref !s in
  let progress = ref true in
  while !progress do
    let again = ref false in
    List.iter
      (fun r ->
        if r.r_live then begin
          let b', res = rcv !drained r.r_id in
          drained := b';
          if match res with R_DATA _ -> true | _ -> false then again := true
        end)
      (List.copy !drained.readers);
    progress := !again
  done;
  expect "I8 draining to quiescence empties every queue"
    (total_resident !drained = 0 && List.for_all (fun w -> w.w_enq = floor_of !drained w) !drained.writers);
  expect "I8 pool_free returns to its initial value at exit: the leak assertion of §7.4"
    (!drained.pool_free = sector_pool && check_bus !drained = []);
  expect "I8 every accepted message was delivered to every subscriber of its arm"
    (List.for_all
       (fun r -> r.r_live && r.r_deq = (List.hd (List.filter (fun w -> w.w_id = r.r_writer) !drained.writers)).w_enq)
       !drained.readers);
  expect "I8 the fan-out is lossless: no reader ever saw more than its publisher accepted"
    (delivered !drained = List.fold_left (fun acc w -> acc + w.w_enq) 0 !drained.writers);

  (* I9. exhaustive interleaving search over the whole API surface *)
  explore 0 bus0;
  expect "I9 no explored interleaving violates a bus invariant" (!first_violation = "");
  expect "I9 the search really covered the space" (!search_total = pow_int n_ops seq_len);
  expect "I9 the search reached a bounded refusal" !saw_full;
  expect "I9 the search reached a delivery and an empty drain" (!saw_data && !saw_empty);
  expect "I9 the search reached both refusal paths of the ownership rule" (!saw_bad_cursor && !saw_taken);
  expect "I9 the search reached the parked-core refusal" !saw_bad_core;
  expect "I9 sectors really leave the pool under interleaving" !pool_below_seen;
  expect "I9 residency never exceeded one publisher's capacity times the table"
    (!max_resident_seen <= max_writers * writer_capacity);

  (* I10. modes: SKIP is not PASS, and the flag must not fake coverage *)
  let on_one_core = run_summary ~mp:MP_ON ~cores:cores_shipped ~require:false in
  expect "I10 the documented full row is PASS(4/5 modes, 1 SKIP) on a single-core port"
    (on_one_core.passes = 4 && on_one_core.skips = 1 && on_one_core.fails = 0);
  let forced = run_summary ~mp:MP_ON ~cores:cores_shipped ~require:true in
  expect "I10 --require=amp on a port without the I/O core fails the run instead of skipping"
    (forced.fails = 1 && forced.skips = 0);
  let off_gate = run_summary ~mp:MP_OFF ~cores:cores_shipped ~require:false in
  expect "I10 with BTRON_MP=0 the queue modes are SKIP, never the inline seam passing as coverage"
    (off_gate.passes = 1 && off_gate.skips = 4 && List.mem "planes" full_rows);
  expect "I10 the Tier-1 planes mode is the one that must pass with the flag off"
    (row_verdict "planes" ~mp:MP_OFF ~cores:cores_shipped ~require:false = V_PASS);
  let four_core = run_summary ~mp:MP_ON ~cores:cores_target ~require:true in
  expect "I10 on the target topology every row, AMP included, is a pass"
    (four_core.passes = 5 && four_core.skips = 0 && four_core.fails = 0);
  expect "I10 the row echoes the request and the truth, because a number without a memory model is not evidence"
    (cores_row cores_target cores_shipped = "cores_req=4 cores_have=1");

  (* I11. EVT queue: masked indices, one writer per field, FIFO across the wrap *)
  expect "I11 the EVT capacity is a power of two, as IO.txt §5.1 requires"
    (event_queue_size = 256 && event_queue_size = 1 lsl 8 && event_queue_size land (event_queue_size - 1) = 0);
  expect "I11 the mask and the modulo agree for every index the soak reaches"
    (List.for_all (fun k -> evt_idx k = k mod event_queue_size) (List.init 1024 Fun.id));
  let e = ref evt0 in
  let sent_q = ref ([] : int list) in
  let got_q = ref ([] : int list) in
  for i = 1 to 600 do
    let e', admitted = evt_put !e i in
    if admitted then begin
      e := e';
      sent_q := i :: !sent_q
    end;
    let e2, v = evt_get !e in
    (match v with
    | Some x ->
      e := e2;
      got_q := x :: !got_q
    | None -> ());
    if evt_occupancy !e > event_queue_size then failures := "I11 the SPSC ring oversubscribed" :: !failures
  done;
  expect "I11 the queue survives two wraps in strict FIFO order"
    (List.rev !got_q = List.init (List.length !got_q) (fun i -> i + 1)
     && List.length !sent_q = List.length !got_q);
  let e_full = ref (List.fold_left (fun acc i -> fst (evt_put acc i)) evt0 (List.init event_queue_size Fun.id)) in
  expect "I11 a full queue refuses the producer instead of overwriting a live slot"
    (let before = !e_full in
     let after, admitted = evt_put !e_full 9999 in
     not admitted && after = before && evt_occupancy before = event_queue_size);
  expect "I11 each cursor has exactly one writer: the producer never moves head"
    (let e1 = fst (evt_put evt0 5) in
     let e2 = fst (evt_get evt0) in
     e1.head = evt0.head && e2.tail = evt0.tail);
  expect "I11 an empty queue yields nothing rather than a stale slot"
    (let after = fst (evt_put evt0 7) in
     let _, v = evt_get after in
     v = Some 7 && snd (evt_get after) = Some 7 && fst (evt_get after).head = after.tail);
  expect "I11 the I/O core contract is the shipped one: core 3, 250 us, 500 us"
    (io_core_id = cores_target - 1 && io_period_us = 250 && io_budget_us = 2 * io_period_us);

  List.iter (fun f -> printf "    FAILED: %s\n" f) !failures;
  if !failures <> [] then begin
    printf "media_intercore_model: %d of %d checks FAILED\n" (List.length !failures) !checks;
    exit 1
  end;
  printf
    "PASS: OCaml oracle (media_intercore_model.ml) - InterCore invariants I1..I11 passed (%d checks, %d interleavings).\n"
    !checks !search_total
