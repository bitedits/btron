(* media_rtp_model.ml
 *
 * Executable specification and verification oracle for the B-System media
 * plane that the Audio Stack / WebRTC work is going to sit on:
 *
 *   - NuStream static buffer pools and refcounts       (GST-SYNC-RTP.md S4.4)
 *   - leaky temporal queues and the drop policy        (GST-SYNC-RTP.md S3.3)
 *   - PTS monotonicity and timestamp dilation          (GST-SYNC-RTP.md S3.4)
 *   - isochronous interpolation (videorate + caps)     (GST-SYNC-RTP.md S3.4)
 *   - per-plane CPU budgets and UI non-interference    (plan S6.3, async_rt.h)
 *   - peer join / six-stage teardown conservation      (GST-SYNC-RTP.md S2.4)
 *   - compositor grid geometry                         (GST-SYNC-RTP.md S2.4)
 *   - BTRON_GST tier scoping, additive-only fork rule  (plan D7..D9)
 *   - the RTP constants as GStreamer defines them      (rtp, videorate, clock)
 *
 * OCaml >= 4.14 / 5.x
 *
 * Build & test:
 *   ocamlc -o media_rtp_model media_rtp_model.ml && ./media_rtp_model
 *)

open Printf

(* ── 1. Contract constants ───────────────────────────────────────── *)

(* async_rt.h: the two equal-priority planes the media loop is budgeted on *)
let input_period_us = 1000
let ui_period_us = 8333
let input_budget_us = 500
let ui_budget_us = 4000

(* GST-SYNC-RTP.md S2.6: Raspberry-Pi-class latency knobs *)
let jitter_window_us = 500_000 (* ingest jitter buffer *)
let preencode_window_us = 300_000
let period_30fps = 33_333
let period_15fps = 66_666

(* NuStream: fixed pools, zero allocation in the data plane *)
let pool_capacity = 24

(* GST-SYNC-RTP.md S2.4: grid topology capacity *)
let grid_slots = 16
let canvas_w = 1920
let canvas_h = 1080

type gst_mode = GST_ON | GST_OFF

type gst_tier =
  | T1_graph
  | T2_object_props
  | T3_signal_json
  | T4_library

(* ── 2. NuStream static buffer pool ─────────────────────────────── *)

type active = { id : int; rc : int }

(* The pool is created once at init with a fixed set of ids.  acquire never
 * mints a new id, which is the zero-runtime-allocation requirement made
 * checkable. *)
type pool =
  { cap : int
  ; free_ids : int list
  ; active : active list
  ; refused : int
  ; illegal : int }

let pool0 =
  { cap = pool_capacity
  ; free_ids = List.init pool_capacity Fun.id
  ; active = []
  ; refused = 0
  ; illegal = 0 }

let find id p = List.find_opt (fun a -> a.id = id) p.active

let acquire p =
  match p.free_ids with
  | [] -> ({ p with refused = p.refused + 1 }, None)
  | id :: rest -> ({ p with free_ids = rest; active = { id; rc = 1 } :: p.active }, Some id)

let buffer_ref id p =
  match find id p with
  | Some _ -> { p with active = List.map (fun a -> if a.id = id then { a with rc = a.rc + 1 } else a) p.active }
  | None -> { p with illegal = p.illegal + 1 }

(* unref: rc drops to zero and the id returns to the free list.  A buffer is
 * never recycled while a reference is held, and never recycled twice. *)
let unref id p =
  match find id p with
  | None -> { p with illegal = p.illegal + 1 }
  | Some a when a.rc > 1 ->
      { p with active = List.map (fun b -> if b.id = id then { b with rc = b.rc - 1 } else b) p.active }
  | Some _ ->
      { p with active = List.filter (fun b -> b.id <> id) p.active; free_ids = id :: p.free_ids }

let check_pool (p : pool) : string list =
  let bad = ref [] in
  let n_free = List.length p.free_ids and n_active = List.length p.active in
  if n_free + n_active <> p.cap then bad := sprintf "pool conservation %d+%d<>%d" n_free n_active p.cap :: !bad;
  if List.exists (fun a -> a.rc < 1) p.active then bad := "active buffer with rc<1" :: !bad;
  let all = p.free_ids @ List.map (fun a -> a.id) p.active in
  let uniq = List.sort_uniq Int.compare all in
  if List.length uniq <> List.length all then bad := "id is both free and in use (double recycle)" :: !bad;
  if List.length uniq <> p.cap then bad := "id set drifted (runtime allocation?)" :: !bad;
  if p.illegal > 0 then bad := "reference on a recycled or unknown buffer" :: !bad;
  !bad

(* ── 3. Leaky temporal queue ────────────────────────────────────── *)

(* Purely temporal bounds (max-size-time), leaky, oldest jettisoned the instant
 * the window is exceeded.  push never blocks: it answers kept or dropped. *)
type leakq =
  { window_us : int
  ; pts : int list (* ascending, oldest first *)
  ; pushed : int
  ; dropped : int
  ; refused_regress : int }

let q_of window = { window_us = window; pts = []; pushed = 0; dropped = 0; refused_regress = 0 }

let newest q =
  match q.pts with
  | [] -> None
  | l -> Some (List.nth l (List.length l - 1))

let span q =
  match (q.pts, newest q) with
  | [], _ -> 0
  | h :: _, Some t -> t - h
  | _ -> 0

(* pairwise neighbours, without List.combine's equal-length requirement *)
let rec adjacent = function
  | a :: ((b :: _) as t) -> (a, b) :: adjacent t
  | _ -> []

let is_strictly_ascending l =
  match l with
  | [] | [ _ ] -> true
  | _ -> List.for_all (fun (a, b) -> a < b) (adjacent l)

(* returns the new queue and whether the incoming buffer survived *)
let rec trim q ts dropped =
  match q.pts with
  | h :: t when ts - h > q.window_us -> trim { q with pts = t; dropped = dropped + 1 } ts (dropped + 1)
  | _ -> (q, dropped)

let push q ts =
  match newest q with
  | Some last when ts <= last ->
      (* timestamp monotonicity: a regressed stamp is refused, never reordered *)
      ({ q with refused_regress = q.refused_regress + 1 }, false)
  | _ ->
      let added = { q with pts = q.pts @ [ ts ]; pushed = q.pushed + 1 } in
      let trimmed, d = trim added ts q.dropped in
      ({ trimmed with dropped = d }, true)

let check_q (q : leakq) : string list =
  let bad = ref [] in
  if span q > q.window_us then bad := sprintf "temporal window exceeded %d>%d" (span q) q.window_us :: !bad;
  if not (is_strictly_ascending q.pts) then bad := "queue not strictly ascending" :: !bad;
  if q.pushed <> List.length q.pts + q.dropped then bad := "buffer conservation broken" :: !bad;
  !bad

(* ── 4. Timestamp dilation and isochronous interpolation ────────── *)

(* Naive stamping: the producer declares 30 fps but the CPU sustains 15 fps, so
 * stamps keep advancing by the declared interval while wall time advances by
 * the capacity interval.  The receiver plays slow motion. *)
let naive_stamp declared i = i * declared
let wall_at capacity i = i * capacity

(* The remedy (GST-SYNC-RTP.md S3.4): lower the declared framerate to the
 * hardware's guaranteed capacity so stamped intervals match wall clock. *)
let corrected_stamp capacity i = i * capacity

let dilation_ratio declared capacity = float_of_int capacity /. float_of_int declared

(* videorate-style interpolation: (output stamp, source stamp used) pairs on a
 * fixed output cadence from an irregular input sequence.  A duplicated frame
 * must never claim a source that has not arrived yet. *)
let interpolate src_pts dst_period =
  match src_pts with
  | [] -> []
  | _ ->
      let last = List.nth src_pts (List.length src_pts - 1) in
      let n = last / dst_period in
      List.init (n + 1) (fun k ->
          let t = k * dst_period in
          let best = ref (-1) in
          List.iter (fun p -> if p <= t then best := p) src_pts;
          (t, !best))

(* ── 5. Per-plane budget simulation ─────────────────────────────── *)

(* Each 1 ms INPUT slice consumes its budget first (the 1 kHz cadence is hard);
 * the rest of the slice is available to the UI band.  Whole slices inside one
 * UI period give supply_whole; the fractional tail adds supply_tail. *)
let whole_slices = ui_period_us / input_period_us
let tail_us = ui_period_us mod input_period_us
let supply_whole = whole_slices * (input_period_us - input_budget_us)
let supply_tail = supply_whole + tail_us

type plane_state = { debt_us : int; peak_debt_us : int; starved_periods : int }

(* overrun_us is how far the INPUT plane exceeds its budget in its first period;
 * use_tail says whether the UI band may run in the sub-millisecond tail of the
 * UI period (i.e. whether it is preempted at the slice edge). *)
let run_periods n ~use_tail overrun_us =
  let supply = if use_tail then supply_tail else supply_whole in
  List.fold_left
    (fun st i ->
      let demand = ui_budget_us + (if i = 0 then overrun_us else 0) in
      let raw = st.debt_us + demand - supply in
      { debt_us = max 0 raw
      ; peak_debt_us = max st.peak_debt_us raw
      ; starved_periods = st.starved_periods + (if raw > 0 then 1 else 0) })
    { debt_us = 0; peak_debt_us = 0; starved_periods = 0 }
    (List.init n Fun.id)

(* ── 6. Peer lifecycle and grid geometry ───────────────────────── *)

let join_steps =
  [ "alloc grid slot"
  ; "create webrtcbin (latency budget)"
  ; "create leaky broadcast queues"
  ; "link vtee/atee -> queue -> webrtcbin"
  ; "sync_state_with_parent"
  ; "async SDP offer" ]

let leave_steps =
  [ "stop, unlink broadcast queues"
  ; "remove v/a decode chains"
  ; "release compositor pad"
  ; "release audiomixer pad"
  ; "destroy webrtcbin"
  ; "free grid slot" ]

(* Resources acquired by join; each must be released by leave. *)
let join_resources = [ "slot"; "webrtcbin"; "v_queue"; "a_queue"; "comp_pad"; "amix_pad" ]
let leave_releases = [ "v_queue"; "a_queue"; "decode_chains"; "comp_pad"; "amix_pad"; "webrtcbin"; "slot" ]

type session =
  { slots : bool list
  ; peers : int
  ; joins : int
  ; leaves : int
  ; refused_full : int
  ; live_resources : int }

let session0 =
  { slots = List.init grid_slots (fun _ -> false)
  ; peers = 0; joins = 0; leaves = 0; refused_full = 0; live_resources = 0 }

let first_free s =
  let idx = ref None in
  List.iteri (fun i b -> if not b && !idx = None then idx := Some i) s.slots;
  !idx

let join s =
  match first_free s with
  | None -> ({ s with refused_full = s.refused_full + 1 }, None)
  | Some i ->
      let slots = List.mapi (fun j b -> if j = i then true else b) s.slots in
      ( { s with
          slots
        ; peers = s.peers + 1
        ; joins = s.joins + 1
        ; live_resources = s.live_resources + List.length join_resources }
      , Some i )

(* Teardown releases resources first and frees the slot last, so a slot is never
 * reusable while a pad or queue still points into it. *)
let leave s i =
  if i < 0 || i >= List.length s.slots then (s, `Bad_slot)
  else if not (List.nth s.slots i) then (s, `Not_a_member)
  else
    let slots = List.mapi (fun j b -> if j = i then false else b) s.slots in
    ( { s with
        slots
      ; peers = s.peers - 1
      ; leaves = s.leaves + 1
      ; live_resources = s.live_resources - List.length join_resources }
    , `Released )

let check_session (s : session) : string list =
  let bad = ref [] in
  let used = List.length (List.filter Fun.id s.slots) in
  if List.length s.slots <> grid_slots then bad := "grid width changed" :: !bad;
  if used <> s.peers then bad := sprintf "slot/peer mismatch %d<>%d" used s.peers :: !bad;
  if s.live_resources <> s.peers * List.length join_resources then bad := "resource leak in join/leave" :: !bad;
  if s.peers > grid_slots then bad := "more peers than grid slots" :: !bad;
  !bad

(* Quadrant geometry.  The documented mapping halves the canvas on both axes,
 * which addresses a 2x2 grid; a 4x4 grid of 16 slots needs quarter cells. *)
type cell = { x : int; y : int; w : int; h : int }

let cell_doc idx = { x = (idx mod 2) * (canvas_w / 2); y = (idx / 2) * (canvas_h / 2); w = canvas_w / 2; h = canvas_h / 2 }
let cell_4x4 idx = { x = (idx mod 4) * (canvas_w / 4); y = (idx / 4) * (canvas_h / 4); w = canvas_w / 4; h = canvas_h / 4 }

let cells_on_canvas ?(n = grid_slots) f =
  List.init n (fun i -> i)
  |> List.map f
  |> List.filter (fun c -> c.x >= 0 && c.y >= 0 && c.x + c.w <= canvas_w && c.y + c.h <= canvas_h)
  |> List.length

let cells_disjoint f =
  let cs = List.init grid_slots (fun i -> i) |> List.map f in
  let overlaps =
    List.fold_left
      (fun n a ->
        List.fold_left
          (fun m b ->
            if a <> b && a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y then m + 1 else m)
          n cs)
      0 cs
  in
  overlaps = 0

(* ── 7. BTRON_GST tiers and the additive-only fork rule ─────────── *)

(* The fork is expressed as tagged lines: removing every Shimmed line must
 * reproduce the pristine upstream text exactly (plan D8). *)
type line_tag = Upstream | Shimmed

type fork = { upstream : string list; lines : (line_tag * string) list }

let pristine =
  [ "#include <gst/gst.h>"
  ; "gint w = WIDTH/2, h = HEIGHT/2;"
  ; "g_object_set(comp_pad, \"xpos\", x, \"ypos\", y, NULL);" ]

let gobject_whitelist =
  [ "xpos"; "ypos"; "width"; "height"; "zorder"; "sizing-policy"; "leaky"; "max-size-time"; "latency" ]

(* the property names the shim would actually have to support *)
let property_names_used = gobject_whitelist

let tier_symbols = function
  | T1_graph -> [ "rt_element_t"; "rt_pad_t"; "rt_buffer_t"; "rt_pipeline_link_pads" ]
  | T2_object_props -> [ "g_object_set"; "g_object_get" ]
  | T3_signal_json -> [ "g_signal_connect"; "json_object_new_int" ]
  | T4_library -> [ "g_main_loop_run"; "gst_element_get_state" ]

(* T1 and T3 are shimmed for audio first; T2 is limited to whitelisted property
 * names; T4 stays host-only (plan D9).  BTRON_MP does not appear in this
 * function at all, which is the formal statement of gate orthogonality. *)
let shim_symbols mode tier =
  match mode with
  | GST_OFF -> []
  | GST_ON -> (
      match tier with
      | T1_graph | T3_signal_json -> tier_symbols tier
      | T2_object_props -> List.filter (fun s -> s = "g_object_set") (tier_symbols tier)
      | T4_library -> [])

let make_fork shim_lines =
  { upstream = pristine
  ; lines = List.map (fun l -> (Upstream, l)) pristine @ List.map (fun l -> (Shimmed, l)) shim_lines }

let strip_shimmed f = List.filter_map (fun (t, l) -> if t = Upstream then Some l else None) f

(* ── 8. GStreamer grounding of the RTP numbers ─────────────────── *)

(* gstrtpbuffer.c:1304-1311: the RTP serial number is a 16-bit ring, so every
 * difference between stamps is measured modulo it, in the forward direction. *)
let seq_mod = 1 lsl 16
let seq_next s = (s + 1) mod seq_mod
let diff16 s1 s2 = (s2 + seq_mod - s1) mod seq_mod

(* gstrtpjitterbuffer.c:1691-1695: the buffer declares itself full only when a
 * half-ring of span is paired with a five-figure packet count. *)
let jb_full span packets = span >= 32765 && packets > 10000

(* the same file's probability watermarks: flush above 15 %, hold below 90 % *)
let jb_low_ms probs = probs * 15 / 100
let jb_high_ms probs = probs * 90 / 100

(* the intra-arrival jitter estimate, an exponential average with a +8 round *)
let ewma j d = j + d - ((j + 8) lsr 4)

(* rtpsource.h:35 RTP_DEFAULT_PROBATION: two good packets make a source valid *)
let probation_needed = 2
let probation_done seen = seen >= probation_needed

(* gstclock.h GST_CLOCK_TIME_NONE is (GstClockTime) -1, the unsigned top of the
 * 64-bit range.  OCaml's int is signed 63-bit, so the clock is modelled with
 * int64 and compared after flipping the sign bit, i.e. as unsigned. *)
let clock_time_none = -1L
let uint_cmp a b = Int64.compare (Int64.logxor a Int64.min_int) (Int64.logxor b Int64.min_int)
let clock_time_max = Int64.sub Int64.max_int 1L

(* gstbuffer.h:110: a muxer-supplied DTS is the ordering stamp, PTS is only the
 * display fallback. *)
let stamp_for pts dts = match dts with None -> pts | Some d -> d

(* gstatomicqueue.c:68-87: a fixed power-of-two ring indexed with a bit mask
 * instead of a division. *)
let event_queue_size = 256
let ring_idx k cap = k mod cap
let ring_mask k cap = k land (cap - 1)

(* gstvideorate.c: unless the timestamp moves forward the pad emits a GAP. *)
let rate_gate last next = last < next

(* ── 9. Oracle ──────────────────────────────────────────────────── *)

let failures = ref ([] : string list)
let checks = ref 0

let expect name cond =
  incr checks;
  if not cond then failures := name :: !failures

let () =
  printf
    "==> B-System media_rtp model: NuStream pools / leaky queues / PTS / budgets / grid / BTRON_GST / \
     GStreamer RTP constants\n";

  (* R1. static pool: zero allocation, conservation, no double recycle *)
  let p = ref pool0 in
  let acquired = ref ([] : int list) in
  for _ = 1 to pool_capacity do
    let r, id = acquire !p in
    p := r;
    match id with
    | Some i -> acquired := i :: !acquired
    | None -> ()
  done;
  expect "R1 every fixed-pool id is handed out exactly once"
    (List.length !acquired = pool_capacity && List.length (List.sort_uniq Int.compare !acquired) = pool_capacity);
  expect "R1 draining the pool never allocated a new id" (check_pool !p = []);
  let exhausted, again = acquire !p in
  expect "R1 an exhausted pool refuses instead of allocating"
    (again = None && exhausted.refused = 1 && check_pool exhausted = []);
  let first = List.nth !acquired 0 in
  let shared = buffer_ref first exhausted in
  expect "R1 a second reference keeps the buffer out of the free list"
    (find first shared <> None && shared.free_ids = exhausted.free_ids);
  let once = unref first shared in
  expect "R1 one unref of a shared buffer leaves it still in use"
    (find first once <> None && once.free_ids = shared.free_ids);
  let freed = unref first once in
  expect "R1 the last unref recycles the id exactly once"
    (find first freed = None && List.length freed.free_ids = 1 && List.hd freed.free_ids = first && check_pool freed = []);
  let double = unref first freed in
  expect "R1 recycling an already released id is flagged, not silently accepted"
    (double.illegal = 1 && double.free_ids = freed.free_ids && double.active = freed.active);
  let restored = ref freed in
  List.iter (fun i -> restored := unref i !restored) (List.tl !acquired);
  expect "R1 releasing every held buffer restores the initial pool exactly"
    (check_pool !restored = [] && !restored.free_ids = pool0.free_ids && !restored.active = [] && !restored.illegal = 0);

  (* R2. leaky temporal queue: the window is a hard bound and push never blocks *)
  let n_frames = 300 in
  let qr = ref (q_of jitter_window_us) in
  for i = 0 to n_frames - 1 do
    let r, _ = push !qr (i * period_30fps) in
    qr := r
  done;
  expect "R2 the ingest jitter queue never exceeds its temporal window" (check_q !qr = []);
  expect "R2 old frames are jettisoned, so the queue actually drops" (!qr.dropped > 0);
  expect "R2 buffer conservation: pushed = retained + dropped"
    (!qr.pushed = List.length !qr.pts + !qr.dropped);
  expect "R2 the newest frame is always retained, so live continuity survives"
    (newest !qr = Some ((n_frames - 1) * period_30fps));
  expect "R2 a burst is bounded by the window, not by the burst size"
    (List.length !qr.pts <= jitter_window_us / period_30fps + 1);
  expect "R2 the retained span really is at the window edge" (span !qr >= jitter_window_us / 2);

  (* R3. timestamp monotonicity *)
  let m0 = q_of preencode_window_us in
  let m1, kept1 = push m0 100_000 in
  let m2, kept2 = push m1 90_000 in
  let m3, kept3 = push m2 90_000 in
  expect "R3 the first stamp is accepted" (kept1 && List.length m1.pts = 1);
  expect "R3 a regressed stamp is refused, never reordered"
    (not kept2 && m2.pts = m1.pts && m2.pushed = m1.pushed && m2.refused_regress = 1);
  expect "R3 a duplicate stamp is refused too"
    (not kept3 && m3.pts = m2.pts && m3.refused_regress = 2 && check_q m3 = []);

  (* R4. timestamp dilation: declared rate vs real capacity *)
  let naive_gap = naive_stamp period_30fps 5 - naive_stamp period_30fps 4 in
  let wall_gap = wall_at period_15fps 5 - wall_at period_15fps 4 in
  expect "R4 naive stamping keeps the declared interval while wall time drifts"
    (naive_gap = period_30fps && wall_gap = period_15fps && naive_gap <> wall_gap);
  expect "R4 30 fps declared at 15 fps capacity dilates by exactly 2:1"
    (abs_float (dilation_ratio period_30fps period_15fps -. 2.0) < 1e-9);
  expect "R4 after 60 buffers the naive stamps claim 30 fps while the wall says 15 fps"
    (naive_stamp period_30fps 60 < wall_at period_15fps 60);
  let fixed_ok =
    List.for_all
      (fun i ->
        corrected_stamp period_15fps (i + 1) - corrected_stamp period_15fps i
        = wall_at period_15fps (i + 1) - wall_at period_15fps i)
      (List.init 100 Fun.id)
  in
  expect "R4 lowering the declared framerate to capacity removes the dilation" fixed_ok;

  (* R5. isochronous interpolation duplicates without inventing future stamps *)
  let irregular = [ 0; 40_000; 80_000; 200_000 ] in
  let out = interpolate irregular period_30fps in
  let out_pts = List.map fst out in
  expect "R5 the interpolated stream is strictly monotone" (is_strictly_ascending out_pts);
  expect "R5 the interpolated cadence is exactly the declared period"
    (List.for_all (fun (a, b) -> b - a = period_30fps) (adjacent out_pts));
  expect "R5 no interpolated frame claims a source that has not arrived"
    (List.for_all (fun (t, src) -> src >= 0 && src <= t) out);
  let srcs = List.map snd out in
  expect "R5 the source selection never rewinds"
    (List.for_all (fun (a, b) -> a <= b) (adjacent srcs));
  expect "R5 the gap in the input is filled, so the compositor is fed" (List.length out > List.length irregular);
  expect "R5 no stamp lies beyond the last real input stamp" (List.for_all (fun t -> t <= 200_000) out_pts);

  (* R6. per-plane budgets: the shipped numbers are exactly balanced *)
  expect "R6 the INPUT budget fits its period" (input_budget_us <= input_period_us);
  expect "R6 integer utilization of both planes stays below one core"
    (input_budget_us * ui_period_us + ui_budget_us * input_period_us < input_period_us * ui_period_us);
  expect "R6 whole 1 ms slices give the UI band exactly its budget, zero slack" (supply_whole = ui_budget_us);
  expect "R6 the only headroom is the sub-millisecond tail of the UI period"
    (supply_tail - supply_whole = tail_us && tail_us = ui_period_us mod input_period_us && tail_us > 0);
  (* so a single-period INPUT overrun is absorbable only if the UI band may run
   * in that tail; a UI band preempted at the slice edge never recovers *)
  let absorbable = run_periods 200 ~use_tail:true 500 in
  let unrecoverable = run_periods 200 ~use_tail:false 500 in
  expect "R6 with the tail usable a 500 us INPUT overrun drains inside the horizon"
    (absorbable.debt_us = 0 && absorbable.peak_debt_us > 0 && absorbable.starved_periods = 1);
  expect "R6 with the tail unusable the same overrun never drains"
    (unrecoverable.debt_us = 500 && unrecoverable.starved_periods = 200);
  let balanced = run_periods 200 ~use_tail:false 0 in
  expect "R6 no overrun means no debt on either plane" (balanced.debt_us = 0 && balanced.starved_periods = 0);
  expect "R6 the non-interference rule is load-bearing, not decoration"
    (supply_tail > ui_budget_us && supply_whole <= ui_budget_us);

  (* R7. peer lifecycle: join/leave conserves slots and resources *)
  let s = ref session0 in
  let taken = ref ([] : int list) in
  let refusals = ref 0 in
  for _ = 1 to 40 do
    let s2, i = join !s in
    s := s2;
    match i with
    | Some i -> taken := i :: !taken
    | None -> incr refusals
  done;
  expect "R7 the grid never over-subscribes its slots" (!s.peers = grid_slots && check_session !s = []);
  expect "R7 joining past capacity is refused without leaking resources"
    (!refusals = 40 - grid_slots && !s.live_resources = grid_slots * List.length join_resources);
  expect "R7 all slots handed out are distinct"
    (List.length (List.sort_uniq Int.compare !taken) = grid_slots);
  let s2 = ref !s in
  let outcome = List.map (fun i -> let r, o = leave !s2 i in s2 := r; o) !taken in
  expect "R7 a full join/leave cycle returns the session to its initial state"
    (check_session !s2 = [] && !s2.peers = 0 && !s2.live_resources = 0 && !s2.slots = session0.slots);
  expect "R7 every release succeeded and in slot order" (outcome = List.init grid_slots (fun _ -> `Released));
  let ghost, g = leave !s2 3 in
  expect "R7 releasing a free slot is refused" (g = `Not_a_member && ghost = !s2);
  let oob, o = leave !s2 grid_slots in
  expect "R7 an out-of-range slot index is refused" (o = `Bad_slot && oob = !s2);
  expect "R7 teardown releases every resource join acquired"
    (List.for_all (fun r -> List.mem r leave_releases) join_resources);
  expect "R7 teardown frees the slot only last"
    (List.nth leave_releases (List.length leave_releases - 1) = "slot");
  expect "R7 join and leave have the same number of ordered stages"
    (List.length join_steps = 6 && List.length leave_steps = 6);
  expect "R7 the first join step acquires and the last leave step releases the same thing"
    (List.hd join_steps = "alloc grid slot" && List.nth leave_steps (List.length leave_steps - 1) = "free grid slot");

  (* R8. grid geometry: the documented 2x2 half-cell mapping cannot address 16 slots *)
  expect "R8 the documented half-cell mapping does not keep 16 slots on canvas"
    (cells_on_canvas cell_doc < grid_slots);
  expect "R8 the documented mapping is correct for the 4 quadrants it was written for"
    (cells_on_canvas ~n:4 cell_doc = 4);
  expect "R8 a quarter-cell 4x4 mapping keeps all 16 slots on canvas" (cells_on_canvas cell_4x4 = grid_slots);
  expect "R8 the 4x4 cells do not overlap" (cells_disjoint cell_4x4);
  expect "R8 the 4x4 grid tiles the canvas exactly"
    (let w = canvas_w / 4 and h = canvas_h / 4 in 4 * w = canvas_w && 4 * h = canvas_h);

  (* R9. BTRON_GST gate: off is the identity, on is additive only *)
  let fork_on = make_fork [ "#if BTRON_GST"; "rt_pipeline_link_pads(&p, e, \"src\", s, \"sink_0\");"; "#endif" ] in
  expect "R9 stripping the shimmed lines reproduces the pristine upstream text"
    (strip_shimmed fork_on.lines = pristine);
  expect "R9 the fork only ever adds lines, never replaces one"
    (List.length fork_on.lines = List.length pristine + 3);
  expect "R9 BTRON_GST=0 exposes no shim symbols in any tier"
    (List.for_all (fun t -> shim_symbols GST_OFF t = []) [ T1_graph; T2_object_props; T3_signal_json; T4_library ]);
  expect "R9 BTRON_GST=1 shims the graph and signalling tiers (audio first)"
    (shim_symbols GST_ON T1_graph <> [] && shim_symbols GST_ON T3_signal_json <> []);
  expect "R9 the real glib event loop stays host-only (T4 not shimmed)"
    (shim_symbols GST_ON T4_library = []);
  expect "R9 the T2 shim is limited to the object-set path, not the getter"
    (shim_symbols GST_ON T2_object_props = [ "g_object_set" ]);
  expect "R9 only whitelisted GObject properties are referenced"
    (List.for_all (fun n -> List.mem n gobject_whitelist) property_names_used);
  (* The media symbol table must be constant along the BTRON_MP axis: four gate
   * combinations, and the media plane depends only on the media gate. *)
  let media_table mp gst =
    match (mp, gst) with
    | _, GST_OFF -> []
    | true, GST_ON -> shim_symbols GST_ON T1_graph
    | false, GST_ON -> shim_symbols GST_ON T1_graph
  in
  expect "R9 BTRON_GST is orthogonal to BTRON_MP along the whole gate matrix"
    (media_table true GST_ON = media_table false GST_ON
     && media_table true GST_OFF = media_table false GST_OFF
     && media_table false GST_ON = shim_symbols GST_ON T1_graph
     && media_table true GST_ON <> media_table true GST_OFF);

  (* R10. the RTP constants are the values GStreamer itself commits to *)
  expect "R10 the serial number ring is exactly sixteen bits"
    (seq_mod = 65536 && seq_mod = 1 lsl 16 && seq_mod = 2 lsl 15);
  expect "R10 the next serial number stays in the ring and wraps at its end"
    (List.for_all (fun s -> seq_next s < seq_mod) [ 0; 1; 32767; 65534 ]
     && seq_next (seq_mod - 1) = 0 && seq_next 0 = 1);
  expect "R10 the distance from a stamp to itself is zero, everywhere in the ring"
    (List.for_all (fun s -> diff16 s s = 0) [ 0; 1; 40000; 65535 ]);
  expect "R10 diff16 measures the forward span, across the wrap included"
    (diff16 100 146 = 46 && diff16 (seq_mod - 36) 10 = 46 && diff16 (seq_mod - 1) 0 = 1);
  expect "R10 a forward run is measured exactly, never as a negative gap"
    (List.for_all (fun s -> diff16 0 s = s) [ 0; 1; 1000; 32768 ]);
  (* 32765 sits just under half the ring: past it a span stops being a plain
   * forward run, which is why the full test is anchored there. *)
  expect "R10 the full test is anchored below half the serial ring" (32765 < seq_mod / 2);
  expect "R10 the full test is the conjunction of a span bound and a count bound"
    (jb_full 32765 10001
     && List.for_all (fun (s, p) -> jb_full s p = (32765 <= s && 10000 < p))
          [ (0, 0); (32764, 10001); (32765, 10001); (65535, 20000); (65535, 3) ]);
  expect "R10 a short span is never full, however many packets it holds"
    (List.for_all (fun p -> not (jb_full 0 p || jb_full 32764 p)) [ 0; 1; 10001; 100000 ]);
  expect "R10 the watermarks are ordered and never exceed the configured maximum"
    (List.for_all (fun n -> jb_low_ms n <= jb_high_ms n && jb_high_ms n <= n) (List.init 200 Fun.id)
     && jb_low_ms 1000 = 150 && jb_high_ms 1000 = 900);
  expect "R10 the jitter average never overshoots the sample it is fed"
    (List.for_all (fun (j, d) -> ewma j d <= j + d) [ (0, 0); (7, 0); (8, 0); (1000, 500); (1_000_000, 3) ]);
  expect "R10 the jitter average drains towards zero once the traffic goes quiet"
    (List.for_all (fun j -> ewma j 0 < j) (List.init 200 (fun i -> i + 8))
     && ewma 0 0 = 0 && ewma 7 0 = 7);
  (* the round term is a pure loss: adding it back recovers the exact sample,
   * so the estimate truncates nowhere once the average has left its first slice *)
  expect "R10 the jitter average is the difference form of the C update, without truncation"
    (List.for_all
       (fun (j, d) ->
         let r = (j + 8) / 16 in
         ewma j d + r = j + d && ewma j d >= 0)
         [ (8, 0); (16, 4); (500, 500); (1000, 1); (1_000_000, 3) ]);
  expect "R10 a source becomes valid after exactly two good packets"
    (probation_needed = 2 && not (probation_done 0) && not (probation_done 1)
     && probation_done 2 && probation_done 1000);
  expect "R10 the clock sentinel is the unsigned top of the 64-bit range"
    (clock_time_none = Int64.lognot 0L && Int64.add clock_time_none 1L = 0L);
  expect "R10 the sentinel is greater than every timestamp the plane can carry"
    (uint_cmp clock_time_max clock_time_none < 0
     && uint_cmp 0L clock_time_none < 0
     && uint_cmp (Int64.of_int jitter_window_us) clock_time_none < 0);
  expect "R10 a muxed DTS overrides the PTS, which is only the fallback"
    (List.for_all (fun (p, d) -> stamp_for p (Some d) = d && stamp_for p None = p)
         [ (0, 0); (100, 0); (0, 100); (33333, 66666) ]);
  expect "R10 the event queue capacity really is a power of two"
    (event_queue_size = 256 && event_queue_size = 1 lsl 8
     && event_queue_size land (event_queue_size - 1) = 0);
  expect "R10 the bit mask indexes exactly where the modulo indexes"
    (List.for_all (fun k -> ring_idx k event_queue_size = ring_mask k event_queue_size)
         (List.init 2048 Fun.id)
     && List.for_all (fun k -> ring_idx k event_queue_size < event_queue_size) (List.init 2048 Fun.id));
  expect "R10 the mask identity fails off the power-of-two lattice, so the bound is load-bearing"
    (ring_idx 300 250 <> ring_mask 300 250);
  expect "R10 videorate advances only on a strictly forward stamp, a duplicate is a GAP"
    (rate_gate 0 1 && not (rate_gate 1 1) && not (rate_gate 2 1)
     && List.for_all (fun (l, n) -> rate_gate l n = (l < n)) [ (0, 0); (5, 5); (5, 6); (7, 3) ]);
  (* the two GStreamer facts that the rest of the model leans on: the ingest
   * window is a whole number of 30 fps periods inside the half-ring, and the
   * probation count is small enough that a live join is never held back *)
  expect "R10 the jitter window holds an integral run of 30 fps stamps under half the ring"
    (jitter_window_us / period_30fps > probation_needed
     && (jitter_window_us / period_30fps) * period_30fps <= jitter_window_us
     && diff16 0 (jitter_window_us / period_30fps) = jitter_window_us / period_30fps);

  List.iter (fun f -> printf "    FAILED: %s\n" f) !failures;
  if !failures <> [] then begin
    printf "media_rtp_model: %d of %d checks FAILED\n" (List.length !failures) !checks;
    exit 1
  end;
  printf "PASS: OCaml oracle (media_rtp_model.ml) - media invariants R1..R10 passed (%d checks).\n" !checks
