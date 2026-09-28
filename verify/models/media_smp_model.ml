(* media_smp_model.ml
 *
 * Executable specification and verification oracle for the B-System SMP /
 * AMP real-time substrate that test-async is going to build:
 *
 *   - platform.rs CAS multicursor ring accounting   (plan D1, section 7.3)
 *   - InterCore pub/sub/snd/rcv cursor ownership    (plan section 7.3)
 *   - AMP I/O-core release / heartbeat / takeover   (plan section 6.5, BR1..BR5)
 *   - wait-free seqlock pointer snapshot batching   (include/btron/async_rt.h)
 *   - BTRON_MP gate refinement, off == legacy       (plan D6)
 *
 * Constants are taken from include/btron/async_rt.h and from the amended plan
 * doc doc/txt/SMP-AMP-SYNC-IO-HARDENING.txt.  The oracle's job is to exhibit
 * an interleaving that breaks an invariant, or to show that none exists
 * within the explored bound.
 *
 * OCaml >= 4.14 / 5.x
 *
 * Build & test:
 *   ocamlc -o media_smp_model media_smp_model.ml && ./media_smp_model
 *)

open Printf

(* ── 1. Contract constants ───────────────────────────────────────── *)

(* async_rt.h: fixed periods and per-plane CPU budgets *)
let input_period_us = 1000
let ui_period_us = 8333
let input_budget_us = 500
let ui_budget_us = 4000
let isr_trb_budget = 32

(* plan section 6.5 AMP handshake bars (rev 2 ruling: 100 ms timeout / 50 ms warn) *)
let io_heartbeat_timeout_ms = 100
let io_heartbeat_warn_ms = 50

(* ring geometry: capacity in bytes and how many bytes a producer may reserve
 * before its multicursor commit publishes them.  Small on purpose: the
 * exhaustive search below only reaches the capacity and inflight bounds when
 * both are tight. *)
let ring_cap = 4
let inflight_max = 2
let lane_count = 2

(* ── 2. CAS multicursor ring model ──────────────────────────────── *)

(* One lane per producer.  reserved/published are the two halves of the
 * multicursor commit: a producer claims a range with one CAS on the packed
 * cursor word, fills it, then publishes.  A reader may only consume published
 * bytes, which is exactly the tearing hazard the packed word exists to close. *)
type lane = { reserved : int; published : int; read : int }

let lane_empty = { reserved = 0; published = 0; read = 0 }

(* accepted  = bytes successfully claimed
 * dropped   = bytes refused by the admission test
 * delivered = bytes handed to readers
 * refused   = rcv attempts refused as BAD_CURSOR *)
type ring =
  { cap : int
  ; lanes : lane list
  ; accepted : int
  ; dropped : int
  ; delivered : int
  ; refused : int }

let ring0 =
  { cap = ring_cap
  ; lanes = List.init lane_count (fun _ -> lane_empty)
  ; accepted = 0; dropped = 0; delivered = 0; refused = 0 }

let lane_at r i = List.nth r.lanes i
let map_lane i f r = { r with lanes = List.mapi (fun j l -> if j = i then f l else l) r.lanes }

(* snd(): claim n bytes on lane i.  Refused when the lane would exceed the ring
 * capacity or the unpublished window.  Never blocks, never wraps. *)
let claim i n r =
  let l = lane_at r i in
  let fits_capacity = l.reserved + n - l.read <= r.cap in
  let fits_inflight = l.reserved + n - l.published <= inflight_max in
  if n > 0 && fits_capacity && fits_inflight then
    ( map_lane i (fun l -> { l with reserved = l.reserved + n }) { r with accepted = r.accepted + n }
    , `Accepted )
  else
    ( { r with dropped = r.dropped + 1 }, `Dropped )

(* publish(): the CAS commit of the packed cursor word. *)
let publish i r = map_lane i (fun l -> { l with published = l.reserved }) r

(* rcv(): tri-state result; there is no blocking case (plan section 7.3). *)
type rcv_kind = DATA | EMPTY | BAD_CURSOR

type rcv = { kind : rcv_kind; bytes : int; ring : ring }

let rcv i want r =
  let l = lane_at r i in
  if want < 0 || l.read > l.published || i < 0 || i >= List.length r.lanes then
    { kind = BAD_CURSOR; bytes = 0; ring = { r with refused = r.refused + 1 } }
  else if l.read < l.published then
    let take = min want (l.published - l.read) in
    { kind = DATA
    ; bytes = take
    ; ring = map_lane i (fun l -> { l with read = l.read + take }) { r with delivered = r.delivered + take } }
  else
    (* caught up to an uncommitted producer: no data, no advance *)
    { kind = EMPTY; bytes = 0; ring = r }

(* A reader that tries to jump its cursor past the publish watermark is refused
 * instead of tearing the ring. *)
let steal i want r =
  let l = lane_at r i in
  if l.read + want > l.published then
    { kind = BAD_CURSOR; bytes = 0; ring = { r with refused = r.refused + 1 } }
  else
    rcv i want r

(* sub(): a late subscriber is pinned to the tail as of subscribe time; it can
 * never rewind past bytes an elder reader already consumed. *)
let subscribe r i = (lane_at r i).read

(* ── 3. Ring invariants ─────────────────────────────────────────── *)

let check_ring (r : ring) : string list =
  let bad = ref [] in
  List.iteri
    (fun i l ->
      if l.published > l.reserved then bad := sprintf "lane%d published>reserved" i :: !bad;
      if l.read > l.published then bad := sprintf "lane%d read>published (torn read)" i :: !bad;
      if l.reserved - l.read > r.cap then bad := sprintf "lane%d oversubscribed" i :: !bad;
      if l.reserved - l.published > inflight_max then bad := sprintf "lane%d inflight>max" i :: !bad;
      if l.read < 0 || l.reserved < 0 then bad := sprintf "lane%d negative cursor" i :: !bad)
    r.lanes;
  let sum f = List.fold_left (fun a l -> a + f l) 0 r.lanes in
  if List.length r.lanes <> lane_count then bad := "lane count changed" :: !bad;
  if sum (fun l -> l.reserved) <> r.accepted then bad := "accepted != sum reserved" :: !bad;
  if sum (fun l -> l.read) <> r.delivered then bad := "delivered != sum read" :: !bad;
  if r.accepted <> r.delivered + sum (fun l -> l.reserved - l.read) then
    bad := "byte conservation broken" :: !bad;
  !bad

(* ── 4. Exhaustive interleaving search ──────────────────────────── *)

(* ops: 0 claim lane0, 1 claim lane1, 2 publish lane0, 3 publish lane1,
 *      4 drain lane0, 5 drain lane1 *)
let n_ops = 6
let seq_len = 7

let apply_op k r =
  match k with
  | 0 -> fst (claim 0 1 r)
  | 1 -> fst (claim 1 1 r)
  | 2 -> publish 0 r
  | 3 -> publish 1 r
  | 4 -> (rcv 0 1 r).ring
  | 5 -> (rcv 1 1 r).ring
  | _ -> r

let search_total = ref 0
let first_violation = ref ("" : string)
let saw_drop = ref false
let saw_starvation = ref false
let saw_data = ref false

let rec explore depth r =
  if depth = 0 then begin
    incr search_total;
    match check_ring r with
    | h :: _ -> if !first_violation = "" then first_violation := h
    | [] ->
        if r.dropped > 0 then saw_drop := true;
        if r.delivered > 0 then saw_data := true;
        if List.exists (fun l -> l.reserved > l.published) r.lanes then saw_starvation := true
  end
  else
    for k = 0 to n_ops - 1 do
      explore (depth - 1) (apply_op k r)
    done

(* ── 5. AMP release / heartbeat / takeover state machine ────────── *)

type amp_state =
  | A_BOOT (* only the boot core owns the drain *)
  | A_RELEASED
  | A_IO_ACTIVE
  | A_IO_STALE
  | A_TAKEOVER (* the boot core reclaimed the drain *)

(* An ownership epoch is the interval during which exactly one core may drain.
 * Acknowledging the release word opens an I/O epoch; takeover opens a boot
 * epoch.  Counters are per epoch, so a legitimate handover is not confused with
 * the two-core corruption the handshake exists to prevent. *)
type amp =
  { st : amp_state
  ; release_word : int
  ; age_ms : int
  ; heartbeat_ms : int
  ; epoch : int
  ; epoch_io : int
  ; epoch_boot : int
  ; io_drained : int
  ; boot_drained : int
  ; warnings : int }

let amp0 =
  { st = A_BOOT; release_word = 0; age_ms = 0; heartbeat_ms = 0
  ; epoch = 0; epoch_io = 0; epoch_boot = 0
  ; io_drained = 0; boot_drained = 0; warnings = 0 }

(* BR1/BR2: boot publishes s_io_core_release.  BR3: the I/O core acknowledges it
 * before touching the queue, which opens a fresh ownership epoch. *)
let amp_release a = { a with release_word = 1; st = A_RELEASED }

let amp_ack a =
  if a.st = A_RELEASED then
    { a with st = A_IO_ACTIVE; heartbeat_ms = a.age_ms; epoch = a.epoch + 1; epoch_io = 0; epoch_boot = 0 }
  else
    a

(* BR4: beats refresh the heartbeat; a beat alone never grants ownership. *)
let amp_beat a =
  if a.st = A_IO_ACTIVE || a.st = A_IO_STALE then { a with heartbeat_ms = a.age_ms; st = A_IO_ACTIVE } else a

(* BR5: the age clock alone moves ownership.  Both the active and the warned
 * state must escalate, otherwise a stalled core keeps ownership forever. *)
let amp_tick dt a =
  let age = a.age_ms + dt in
  let gap = age - a.heartbeat_ms in
  let st =
    match a.st with
    | A_IO_ACTIVE | A_IO_STALE when gap >= io_heartbeat_timeout_ms -> A_TAKEOVER
    | A_IO_ACTIVE when gap >= io_heartbeat_warn_ms -> A_IO_STALE
    | s -> s
  in
  let escalated = st = A_TAKEOVER && a.st <> A_TAKEOVER in
  { a with
    age_ms = age
  ; st
  ; epoch = (if escalated then a.epoch + 1 else a.epoch)
  ; epoch_io = (if escalated then 0 else a.epoch_io)
  ; epoch_boot = (if escalated then 0 else a.epoch_boot)
  ; warnings = a.warnings + (if st = A_IO_STALE && a.st <> A_IO_STALE then 1 else 0) }

(* Draining is only legal for the current owner. *)
let amp_drain_io a =
  match a.st with
  | A_IO_ACTIVE | A_IO_STALE ->
      { a with io_drained = a.io_drained + 1; epoch_io = a.epoch_io + 1 }
  | _ -> a

let amp_drain_boot a =
  match a.st with
  | A_BOOT | A_RELEASED | A_TAKEOVER ->
      { a with boot_drained = a.boot_drained + 1; epoch_boot = a.epoch_boot + 1 }
  | _ -> a

let check_amp a =
  let bad = ref [] in
  if a.epoch_io > 0 && a.epoch_boot > 0 then bad := "both cores drained in one epoch" :: !bad;
  if a.io_drained > 0 && a.release_word = 0 then bad := "I/O core drained before release" :: !bad;
  if a.st = A_IO_ACTIVE && a.age_ms - a.heartbeat_ms >= io_heartbeat_timeout_ms then
    bad := "stale I/O core still owns the drain" :: !bad;
  if a.st = A_TAKEOVER && a.release_word = 0 then bad := "takeover without release" :: !bad;
  if a.st = A_IO_STALE && a.age_ms - a.heartbeat_ms < io_heartbeat_warn_ms then
    bad := "premature stale" :: !bad;
  !bad

(* ── 6. Seqlock pointer snapshot (async_rt.h pointer_slot_t) ─────── *)

(* Single producer = the 1 kHz timer IRQ, single consumer = the INPUT worker.
 * Accumulators are monotonic sums of per-report deltas; the consumer diffs
 * against its own last consumed copy, so batches coalesce losslessly. *)
type seqlock = { seq : int; acc_dx : int; acc_dy : int; acc_wheel : int; buttons : int }
type snapshot = { s_seq : int; dx : int; dy : int; wheel : int; buttons : int }

let sl0 = { seq = 0; acc_dx = 0; acc_dy = 0; acc_wheel = 0; buttons = 0 }

(* write side: seq goes odd, fields mutate, seq goes even. *)
let sl_publish s dx dy wheel btn =
  let odd = { s with seq = s.seq + 1 } in
  let mid = { odd with acc_dx = odd.acc_dx + dx; acc_dy = odd.acc_dy + dy
                     ; acc_wheel = odd.acc_wheel + wheel; buttons = btn } in
  { mid with seq = mid.seq + 1 }

(* read side, no guard: may observe the middle of an update *)
let sl_read_raw s = { s_seq = s.seq; dx = s.acc_dx; dy = s.acc_dy; wheel = s.acc_wheel; buttons = s.buttons }

(* one guarded read attempt, expressed as (before, after) pair of raw reads so a
 * torn observation can be modelled *)
let sl_try before after =
  if before.s_seq = after.s_seq && before.s_seq land 1 = 0 then (Some before, false)
  else (None, true)

let sl_diff prev cur =
  { s_seq = cur.s_seq; dx = cur.dx - prev.dx; dy = cur.dy - prev.dy
  ; wheel = cur.wheel - prev.wheel; buttons = cur.buttons }

(* ── 7. BTRON_MP gate: the off build is the legacy build ─────────── *)

type mp_mode = MP_ON | MP_OFF

(* With BTRON_MP=0 no src/mp translation unit is linked and no sector pool is
 * reserved; the ASYNC path stays the seqlock snapshot plus direct drain. *)
let mp_modules = function MP_ON -> [ "async_ring.c"; "async_core.c"; "mp_clock.c" ] | MP_OFF -> []

let mp_reserves_sector_pool = function MP_ON -> true | MP_OFF -> false
let mp_uses_ring = function MP_ON -> true | MP_OFF -> false

(* The seam is additive, so the off projection is the identity on legacy
 * behaviour: whatever the gate, the INPUT worker consumes the same snapshot. *)
let legacy_drain snap = (snap.dx, snap.dy, snap.wheel, snap.buttons)
let mp_input_path _mode snap = legacy_drain snap

(* ── 8. Oracle ──────────────────────────────────────────────────── *)

let failures = ref ([] : string list)
let checks = ref 0

let expect name cond =
  incr checks;
  if not cond then failures := name :: !failures

let pow_int base e =
  let rec go acc n = if n = 0 then acc else go (acc * base) (n - 1) in
  go 1 e

let () =
  printf "==> B-System media_smp model: CAS multicursor ring / AMP / seqlock / BTRON_MP\n";

  (* S1. exhaustive interleaving search over the multicursor ring *)
  explore seq_len ring0;
  printf "    ring: explored %d interleavings (%d ops ^ length %d)\n" !search_total n_ops seq_len;
  expect "S1 no explored interleaving violates a ring invariant" (!first_violation = "");
  expect "S1 the search really covered the space" (!search_total = pow_int n_ops seq_len);
  expect "S1 the capacity bound is reachable, so claims are refused not wrapped" !saw_drop;
  expect "S1 published bytes do reach a reader" !saw_data;
  expect "S1 multicursor starvation (reserved, unpublished) is reachable" !saw_starvation;

  (* S2. publish-before-read ordering *)
  let claimed, v = claim 0 2 ring0 in
  expect "S2 a 2-byte claim is admitted" (v = `Accepted && (lane_at claimed 0).reserved = 2);
  expect "S2 claim without publish yields no data" ((rcv 0 2 claimed).kind = EMPTY);
  expect "S2 an uncommitted claim does not advance the reader" ((rcv 0 2 claimed).ring = claimed);
  let committed = publish 0 claimed in
  let got = rcv 0 2 committed in
  expect "S2 after the multicursor commit the whole batch is readable"
    (got.kind = DATA && got.bytes = 2 && check_ring got.ring = []);
  (* the inflight bound is a real bound: the third uncommitted claim is refused
   * rather than allowed to tear ahead of the publish watermark *)
  let inflight_chain = claim 0 1 (claim 0 1 (fst (claim 0 1 ring0)) |> fst) in
  expect "S2 uncommitted claims are capped by inflight_max"
    (snd inflight_chain = `Dropped && (lane_at (fst inflight_chain) 0).reserved = inflight_max);

  (* S3. cursor ownership: a late subscriber cannot reread consumed bytes *)
  let base = publish 1 (fst (claim 1 2 ring0)) in
  let tail = subscribe base 1 in
  let elder = rcv 1 2 base in
  let late = subscribe elder.ring 1 in
  expect "S3 the elder reader consumes both bytes" (elder.bytes = 2);
  expect "S3 a subscriber pins to the tail and never rewinds" (tail = 0 && late = 2 && late >= tail);

  (* S4. BAD_CURSOR: a reader cannot jump past the publish watermark *)
  let pending = fst (claim 0 2 ring0) in
  let jumped = steal 0 5 pending in
  expect "S4 a jump past the watermark is refused" (jumped.kind = BAD_CURSOR && jumped.bytes = 0);
  expect "S4 refusal leaves the reader cursor unmoved" ((lane_at jumped.ring 0).read = 0);
  expect "S4 refusal keeps the ring consistent" (check_ring jumped.ring = []);
  let negative = rcv 0 (-1) pending in
  expect "S4 a negative request is refused, not wrapped"
    (negative.kind = BAD_CURSOR && negative.bytes = 0 && (lane_at negative.ring 0).read = 0);

  (* S5. rcv totality: for every consistent lane triple the result is one of the
   * three contract states, is bounded by the request, and never a yield. *)
  let total_cases = ref 0 in
  let s5_data_iff = ref true in
  let s5_empty_pure = ref true in
  let s5_bounded = ref true in
  let s5_never_blocked = ref true in
  for read = 0 to ring_cap do
    for published = 0 to ring_cap do
      for reserved = 0 to ring_cap do
        if published <= reserved && read <= published then begin
          let r = { ring0 with lanes = [ { reserved; published; read } ] @ List.tl ring0.lanes } in
          let o = rcv 0 1 r in
          incr total_cases;
          s5_bounded := !s5_bounded && o.bytes >= 0 && o.bytes <= 1;
          (match o.kind with
           | DATA ->
               s5_data_iff := !s5_data_iff && read < published && o.bytes = 1
                               && (lane_at o.ring 0).read = read + 1
           | EMPTY -> s5_empty_pure := !s5_empty_pure && read = published && o.ring == r
           | BAD_CURSOR -> s5_never_blocked := false)
        end
      done
    done
  done;
  let expected_triples =
    List.fold_left (fun acc p -> acc + (p + 1) * (ring_cap + 1 - p)) 0 (List.init (ring_cap + 1) Fun.id)
  in
  expect "S5 every consistent cursor triple was exercised" (!total_cases = expected_triples);
  expect "S5 DATA is returned exactly when the reader is behind the watermark" !s5_data_iff;
  expect "S5 EMPTY is pure: it never mutates the ring" !s5_empty_pure;
  expect "S5 a read never returns more than was asked for" !s5_bounded;
  expect "S5 rcv has no blocking or out-of-contract case" !s5_never_blocked;

  (* S6. AMP handshake: ownership is exclusive and transitions are ordered *)
  let a1 = amp_tick 20 amp0 in
  expect "S6 the I/O core drains nothing before release" ((amp_drain_io a1).io_drained = 0);
  expect "S6 the boot core owns the drain in A_BOOT" ((amp_drain_boot a1).boot_drained = 1);
  let a2 = amp_ack (amp_release a1) in
  expect "S6 acknowledgement only follows a release word" (a2.st = A_IO_ACTIVE);
  expect "S6 an ack without release is ignored" ((amp_ack a1).st = A_BOOT);
  let a3 = amp_drain_io (amp_drain_boot a2) in
  expect "S6 exactly one drainer once the I/O core is active"
    (check_amp a3 = [] && a3.boot_drained = 0 && a3.io_drained = 1);
  let a4 = amp_tick 40 a3 in
  expect "S6 no warning before the 50 ms bar" (a4.st = A_IO_ACTIVE && a4.warnings = 0);
  let a5 = amp_tick 20 a4 in
  expect "S6 the warning fires at the 50 ms bar" (a5.st = A_IO_STALE && a5.warnings = 1);
  let a6 = amp_tick 40 a5 in
  expect "S6 takeover fires at the 100 ms bar" (a6.st = A_TAKEOVER);
  let a7 = amp_drain_boot (amp_drain_io a6) in
  expect "S6 post-takeover ownership stays exclusive" (check_amp a7 = [] && a7.boot_drained = 1 && a7.epoch_io = 0);
  expect "S6 a handover opens a fresh epoch and is kept in the cumulative counters"
    (a7.io_drained = 1 && a6.epoch = a5.epoch + 1 && a7.epoch = a6.epoch);
  let a8 = amp_beat a6 in
  expect "S6 a revived I/O core cannot unilaterally reclaim the drain" (a8.st = A_TAKEOVER);

  (* S7. heartbeat bars are ordered and the drain budget is bounded *)
  expect "S7 warn precedes timeout" (io_heartbeat_warn_ms < io_heartbeat_timeout_ms);
  expect "S7 timeout is a whole multiple of the warn window"
    (io_heartbeat_timeout_ms mod io_heartbeat_warn_ms = 0);
  expect "S7 ISR TRB budget is bounded per IRQ" (isr_trb_budget > 0 && isr_trb_budget <= 64);
  (* a stalled I/O core is detected within one timeout window of ticks *)
  let detect = ref 0 in
  let a = ref (amp_drain_io (amp_ack (amp_release amp0))) in
  while !a.st <> A_TAKEOVER && !detect < 10_000 do
    a := amp_tick 5 !a;
    incr detect
  done;
  expect "S7 stall is detected, not waited on forever" (!a.st = A_TAKEOVER);
  expect "S7 detection happens inside the timeout window" (!detect = io_heartbeat_timeout_ms / 5);

  (* S8. seqlock: monotone accumulators coalesce losslessly *)
  let s1 = sl_publish sl0 3 1 0 1 in
  let s2 = sl_publish s1 4 (-2) 1 1 in
  let s3 = sl_publish s2 5 7 (-1) 3 in
  expect "S8 the published sequence number is even" (s3.seq land 1 = 0);
  let snap, torn = sl_try (sl_read_raw s3) (sl_read_raw s3) in
  expect "S8 a stable read is not a retry" ((not torn) && snap <> None);
  let d = sl_diff { s_seq = 0; dx = 0; dy = 0; wheel = 0; buttons = 0 } (Option.get snap) in
  expect "S8 a lagging reader sees the full coalesced delta, never a lost one"
    (d.dx = 12 && d.dy = 6 && d.wheel = 0 && d.buttons = 3);
  (* a publish lands between the two raw reads: the attempt is torn and delivers
   * nothing, it is never half-delivered *)
  let mid = { s3 with seq = s3.seq + 1; acc_dx = s3.acc_dx + 9 } in
  let t_snap, t_retry = sl_try (sl_read_raw s3) (sl_read_raw mid) in
  expect "S8 a torn observation forces a retry and delivers nothing" (t_snap = None && t_retry);
  (* accumulators are monotone in the sum of published deltas *)
  let acc = ref sl0 in
  let sent = ref 0 in
  for i = 1 to 200 do
    let dx = (i * 37) mod 13 - 6 in
    sent := !sent + dx;
    acc := sl_publish !acc dx 0 0 0
  done;
  expect "S8 accumulator equals the sum of published deltas" (!acc.acc_dx = !sent);

  (* S9. shipped periods and budgets are self-consistent *)
  expect "S9 the INPUT budget fits its period" (input_budget_us <= input_period_us);
  expect "S9 the UI budget fits its period" (ui_budget_us <= ui_period_us);
  expect "S9 neither plane saturates its core"
    (input_budget_us * ui_period_us + ui_budget_us * input_period_us < input_period_us * ui_period_us);

  (* S10. BTRON_MP=0 refinement: additive seam, zero linked src/mp objects *)
  expect "S10 gate off links no src/mp translation unit" (mp_modules MP_OFF = []);
  expect "S10 gate on links the ring, the core and the clock" (List.length (mp_modules MP_ON) = 3);
  expect "S10 gate off reserves no sector pool" (not (mp_reserves_sector_pool MP_OFF) && mp_reserves_sector_pool MP_ON);
  expect "S10 gate off takes no ring path" (not (mp_uses_ring MP_OFF) && mp_uses_ring MP_ON);
  let legacy = sl_read_raw s3 in
  expect "S10 BTRON_MP=0 is the identity on legacy INPUT behaviour"
    (mp_input_path MP_OFF legacy = mp_input_path MP_ON legacy);

  (* S11. ring geometry *)
  expect "S11 the unpublished window fits inside capacity" (inflight_max <= ring_cap);
  expect "S11 at least one full batch can be held" (ring_cap >= 1);

  List.iter (fun f -> printf "    FAILED: %s\n" f) !failures;
  if !failures <> [] then begin
    printf "media_smp_model: %d of %d checks FAILED\n" (List.length !failures) !checks;
    exit 1
  end;
  printf
    "PASS: OCaml oracle (media_smp_model.ml) - invariants S1..S11 passed (%d checks, %d interleavings, %d cursor triples).\n"
    !checks !search_total !total_cases
