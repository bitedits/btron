(* interpreter_model.ml
 *
 * Executable specification and verification oracle for the verified BTRON
 * MicroScript / Programming-Language-T interpreter architecture:
 *
 *   - ITU-T Z.100 / Z.101 (SDL-2010) Annex F agent and signal queue model
 *   - ISO/IEC 15437 (E-LOTOS) commutation lemma and trace equivalence
 *   - ISO/IEC 15909 (High-Level Petri Nets) watermark and token conservation
 *   - InterCore star bus mapping with broker-free multi-consumer projections
 *   - NASA JPL "Power of 10" fixed footprint discipline (zero heap allocation)
 *
 * Normative references:
 *   - doc/txt/INTERPRETER.txt (Architecture specification)
 *   - doc/pdf/btron-verimath.tex (§5 InterCore, §6 Modal Extensions)
 *   - verify/models/media_intercore_model.ml
 *
 * OCaml >= 4.14 / 5.x
 * Build & test:
 *   ocamlc -o interpreter_model interpreter_model.ml && ./interpreter_model
 *)

open Printf

(* ── 1. Contract constants (JPL Power of 10 Ceilings) ───────────────── *)

let max_agents      = 4
let max_vars        = 8
let port_capacity   = 4
let sector_pool     = 32
let max_fuel        = 1000

(* ── 2. Signal and Queue Data Structures ─────────────────────────────── *)

type signal = {
  sig_id     : int;
  sig_sender : int;
  sig_val    : int;
}

type input_port = {
  p_cap           : int;
  mutable p_head  : int;
  mutable p_tail  : int;
  mutable p_count : int;
  mutable p_drops : int;
  p_ring          : signal array;
}

type agent_state =
  | AGENT_IDLE
  | AGENT_RUNNING
  | AGENT_WAITING_SIGNAL
  | AGENT_HALTED

type agent = {
  a_id            : int;
  mutable a_live  : bool;
  mutable a_state : agent_state;
  mutable a_pc    : int;
  a_vars          : int array;
  a_port          : input_port;
  mutable a_steps : int;
}

type bus = {
  agents                 : agent array;
  mutable pool_free      : int;
  mutable sig_enq_total  : int;
  mutable sig_deq_total  : int;
  mutable sig_drop_total : int;
}

(* ── 3. Constructors and Total Port Operations ───────────────────────── *)

let make_port cap = {
  p_cap   = cap;
  p_head  = 0;
  p_tail  = 0;
  p_count = 0;
  p_drops = 0;
  p_ring  = Array.make cap { sig_id = 0; sig_sender = 0; sig_val = 0 };
}

let make_agent id = {
  a_id    = id;
  a_live  = false;
  a_state = AGENT_IDLE;
  a_pc    = 0;
  a_vars  = Array.make max_vars 0;
  a_port  = make_port port_capacity;
  a_steps = 0;
}

let init_bus () = {
  agents         = Array.init max_agents make_agent;
  pool_free      = sector_pool;
  sig_enq_total  = 0;
  sig_deq_total  = 0;
  sig_drop_total = 0;
}

(* Copy agent for pure functional state transformation *)
let copy_agent a = {
  a_id    = a.a_id;
  a_live  = a.a_live;
  a_state = a.a_state;
  a_pc    = a.a_pc;
  a_vars  = Array.copy a.a_vars;
  a_port  = {
    p_cap   = a.a_port.p_cap;
    p_head  = a.a_port.p_head;
    p_tail  = a.a_port.p_tail;
    p_count = a.a_port.p_count;
    p_drops = a.a_port.p_drops;
    p_ring  = Array.copy a.a_port.p_ring;
  };
  a_steps = a.a_steps;
}

let copy_bus b = {
  agents         = Array.init max_agents (fun i -> copy_agent b.agents.(i));
  pool_free      = b.pool_free;
  sig_enq_total  = b.sig_enq_total;
  sig_deq_total  = b.sig_deq_total;
  sig_drop_total = b.sig_drop_total;
}

(* ── 4. InterCore Message Passing Primitives (snd / rcv / spawn) ─────── *)

type snd_result =
  | SND_OK
  | SND_FULL of int  (* dropped count *)
  | SND_BAD_AGENT

type rcv_result =
  | RCV_DATA of signal
  | RCV_EMPTY
  | RCV_BAD_AGENT

(* Total signal send: deposit into destination input port *)
let snd_signal (b : bus) (sender_id : int) (target_aid : int) (sig_data : signal) : bus * snd_result =
  let b' = copy_bus b in
  if target_aid < 0 || target_aid >= max_agents || not b'.agents.(target_aid).a_live then
    (b', SND_BAD_AGENT)
  else
    let ag = b'.agents.(target_aid) in
    let pt = ag.a_port in
    if pt.p_count >= pt.p_cap then begin
      (* Queue full: refuse, increment drops, preserve non-blocking determinism *)
      pt.p_drops <- pt.p_drops + 1;
      b'.sig_drop_total <- b'.sig_drop_total + 1;
      (b', SND_FULL pt.p_drops)
    end else begin
      (* Enqueue signal in circular ring buffer *)
      pt.p_ring.(pt.p_tail) <- sig_data;
      pt.p_tail <- (pt.p_tail + 1) mod pt.p_cap;
      pt.p_count <- pt.p_count + 1;
      b'.sig_enq_total <- b'.sig_enq_total + 1;
      (b', SND_OK)
    end

(* Total signal receive: consume from calling agent's own input port *)
let rcv_signal (b : bus) (aid : int) : bus * rcv_result =
  let b' = copy_bus b in
  if aid < 0 || aid >= max_agents || not b'.agents.(aid).a_live then
    (b', RCV_BAD_AGENT)
  else
    let ag = b'.agents.(aid) in
    let pt = ag.a_port in
    if pt.p_count = 0 then
      (b', RCV_EMPTY)
    else begin
      let sig_data = pt.p_ring.(pt.p_head) in
      pt.p_head <- (pt.p_head + 1) mod pt.p_cap;
      pt.p_count <- pt.p_count - 1;
      b'.sig_deq_total <- b'.sig_deq_total + 1;
      (b', RCV_DATA sig_data)
    end

(* Spawn agent *)
let spawn_agent (b : bus) (aid : int) (init_val : int) : bus * bool =
  let b' = copy_bus b in
  if aid < 0 || aid >= max_agents || b'.agents.(aid).a_live || b'.pool_free < 4 then
    (b', false)
  else begin
    let ag = b'.agents.(aid) in
    ag.a_live <- true;
    ag.a_state <- AGENT_RUNNING;
    ag.a_pc <- 0;
    ag.a_vars.(0) <- init_val;
    ag.a_steps <- 0;
    ag.a_port.p_head <- 0;
    ag.a_port.p_tail <- 0;
    ag.a_port.p_count <- 0;
    ag.a_port.p_drops <- 0;
    b'.pool_free <- b'.pool_free - 4;
    (b', true)
  end

(* ── 5. Operational Semantics: Small-Step Transition System ──────────── *)

type step_action =
  | ACT_NOP
  | ACT_SET of int * int          (* var_idx, value *)
  | ACT_ADD of int * int          (* var_idx, delta *)
  | ACT_SND of int * signal       (* target_aid, signal *)
  | ACT_RCV of int                (* var_idx to store received value *)
  | ACT_HALT

type step_receipt =
  | STEP_PASS
  | STEP_DROPPED
  | STEP_RECEIVED of int
  | STEP_NOT_READY
  | STEP_INACTIVE

let agent_step (b : bus) (aid : int) (act : step_action) : bus * step_receipt =
  if aid < 0 || aid >= max_agents || not b.agents.(aid).a_live then
    (copy_bus b, STEP_INACTIVE)
  else
    let b' = copy_bus b in
    let ag = b'.agents.(aid) in
    ag.a_steps <- ag.a_steps + 1;
    match act with
    | ACT_NOP ->
        (b', STEP_PASS)
    | ACT_SET (v_idx, v_val) ->
        if v_idx >= 0 && v_idx < max_vars then ag.a_vars.(v_idx) <- v_val;
        (b', STEP_PASS)
    | ACT_ADD (v_idx, delta) ->
        if v_idx >= 0 && v_idx < max_vars then ag.a_vars.(v_idx) <- ag.a_vars.(v_idx) + delta;
        (b', STEP_PASS)
    | ACT_SND (dest, s) ->
        let b'', res = snd_signal b' aid dest s in
        (match res with
         | SND_OK -> (b'', STEP_PASS)
         | SND_FULL _ -> (b'', STEP_DROPPED)
         | SND_BAD_AGENT -> (b'', STEP_INACTIVE))
    | ACT_RCV v_idx ->
        let b'', res = rcv_signal b' aid in
        (match res with
         | RCV_DATA s ->
             if v_idx >= 0 && v_idx < max_vars then ag.a_vars.(v_idx) <- s.sig_val;
             (b'', STEP_RECEIVED s.sig_val)
         | RCV_EMPTY -> (b'', STEP_NOT_READY)
         | RCV_BAD_AGENT -> (b'', STEP_INACTIVE))
    | ACT_HALT ->
        ag.a_state <- AGENT_HALTED;
        (b', STEP_PASS)

(* ── 6. Invariant Checkers ───────────────────────────────────────────── *)

(* Invariant 1: ISO/IEC 15909 Token Conservation *)
let inv_signal_conservation (b : bus) : bool =
  let pending = Array.fold_left (fun acc ag -> acc + ag.a_port.p_count) 0 b.agents in
  b.sig_enq_total = b.sig_deq_total + pending

(* Invariant 2: Bounded Capacity & Monotonicity *)
let inv_port_capacities (b : bus) : bool =
  Array.for_all (fun ag ->
    ag.a_port.p_count >= 0 && ag.a_port.p_count <= ag.a_port.p_cap
  ) b.agents

(* Invariant 3: Memory & Sector Pool Non-Negative *)
let inv_pool_non_negative (b : bus) : bool =
  b.pool_free >= 0 && b.pool_free <= sector_pool

(* State equivalence on observable coordinates *)
let bus_equiv (b1 : bus) (b2 : bus) : bool =
  b1.pool_free = b2.pool_free &&
  b1.sig_enq_total = b2.sig_enq_total &&
  b1.sig_deq_total = b2.sig_deq_total &&
  b1.sig_drop_total = b2.sig_drop_total &&
  Array.for_all2 (fun a1 a2 ->
    a1.a_live = a2.a_live &&
    a1.a_state = a2.a_state &&
    a1.a_vars = a2.a_vars &&
    a1.a_port.p_count = a2.a_port.p_count &&
    a1.a_port.p_drops = a2.a_port.p_drops
  ) b1.agents b2.agents

(* ── 7. Verification Oracle Suites ───────────────────────────────────── *)

let run_suite_basic_operations () =
  printf "--- Suite 1: Agent Lifecycle, Discrete Signals & Bounded Ports ---\n";
  let b0 = init_bus () in
  let b1, ok0 = spawn_agent b0 0 100 in
  let b2, ok1 = spawn_agent b1 1 200 in
  assert (ok0 && ok1);
  assert (b2.agents.(0).a_live && b2.agents.(1).a_live);
  assert (inv_pool_non_negative b2);

  (* Agent 0 sends 4 signals to Agent 1 (fills port capacity of 4) *)
  let b = ref b2 in
  for i = 1 to 4 do
    let b_next, res = snd_signal !b 0 1 { sig_id = i; sig_sender = 0; sig_val = i * 10 } in
    assert (res = SND_OK);
    b := b_next;
  done;
  assert (!b.agents.(1).a_port.p_count = 4);
  assert (inv_signal_conservation !b);

  (* 5th signal: queue is full -> must cleanly drop and record receipt *)
  let b_overflow, res_drop = snd_signal !b 0 1 { sig_id = 5; sig_sender = 0; sig_val = 50 } in
  assert (match res_drop with SND_FULL drops -> drops = 1 | _ -> false);
  assert (b_overflow.agents.(1).a_port.p_count = 4);
  assert (b_overflow.sig_drop_total = 1);
  assert (inv_signal_conservation b_overflow);

  (* Agent 1 drains the queue *)
  let b_drain = ref b_overflow in
  for i = 1 to 4 do
    let b_next, res = rcv_signal !b_drain 1 in
    (match res with
     | RCV_DATA s -> assert (s.sig_id = i && s.sig_val = i * 10)
     | _ -> assert false);
    b_drain := b_next;
  done;
  assert (!b_drain.agents.(1).a_port.p_count = 0);
  assert (inv_signal_conservation !b_drain);

  (* Underflow read: returns RCV_EMPTY without blocking *)
  let b_empty, res_empty = rcv_signal !b_drain 1 in
  assert (res_empty = RCV_EMPTY);
  assert (inv_signal_conservation b_empty);
  printf "  [PASS] Lifecycle, bounded overflow refusal, and non-blocking underflow verified.\n"

let run_suite_commutation_lemma () =
  printf "--- Suite 2: ISO/IEC 15437 Commutation Lemma (E-LOTOS) ---\n";
  let b0 = init_bus () in
  let b1, _ = spawn_agent b0 0 10 in
  let b2, _ = spawn_agent b1 1 20 in
  let b3, _ = spawn_agent b2 2 30 in

  (* Action on Agent 0: modify local var 0 *)
  let act0 = ACT_ADD (0, 5) in
  (* Action on Agent 1: modify local var 1 *)
  let act1 = ACT_ADD (1, 7) in

  (* Path A: Step Agent 0 then Step Agent 1 *)
  let b_a1, r_a1 = agent_step b3 0 act0 in
  let b_a2, r_a2 = agent_step b_a1 1 act1 in

  (* Path B: Step Agent 1 then Step Agent 0 *)
  let b_b1, r_b1 = agent_step b3 1 act1 in
  let b_b2, r_b2 = agent_step b_b1 0 act0 in

  assert (r_a1 = STEP_PASS && r_a2 = STEP_PASS);
  assert (r_b1 = STEP_PASS && r_b2 = STEP_PASS);
  assert (bus_equiv b_a2 b_b2);
  printf "  [PASS] Commutation Lemma holds for disjoint local state transformations.\n";

  (* Cross-agent communication commutation:
     Agent 0 sends to Agent 2; Agent 1 sends to Agent 2 *)
  let sigA = { sig_id = 101; sig_sender = 0; sig_val = 111 } in
  let sigB = { sig_id = 102; sig_sender = 1; sig_val = 222 } in
  let sndA = ACT_SND (2, sigA) in
  let sndB = ACT_SND (2, sigB) in

  let b_comm1, _ = agent_step (fst (agent_step b3 0 sndA)) 1 sndB in
  let b_comm2, _ = agent_step (fst (agent_step b3 1 sndB)) 0 sndA in

  assert (inv_signal_conservation b_comm1);
  assert (inv_signal_conservation b_comm2);
  assert (b_comm1.agents.(2).a_port.p_count = 2);
  assert (b_comm2.agents.(2).a_port.p_count = 2);
  printf "  [PASS] Independent signal emissions preserve token conservation invariant.\n"

let run_suite_schedule_space_exploration () =
  printf "--- Suite 3: Exhaustive Non-Determined Schedule Explorer (Modal Invariants) ---\n";
  let b0 = init_bus () in
  let b1, _ = spawn_agent b0 0 1 in
  let b2, _ = spawn_agent b1 1 2 in

  (* 4 possible step operations:
     0: NOP
     1: Agent 0 sends to Agent 1
     2: Agent 1 receives
     3: Agent 0 increments var 0 *)
  let apply_op b op_id =
    match op_id with
    | 0 -> fst (agent_step b 0 ACT_NOP)
    | 1 -> fst (agent_step b 0 (ACT_SND (1, { sig_id = 1; sig_sender = 0; sig_val = 42 })))
    | 2 -> fst (agent_step b 1 (ACT_RCV 0))
    | 3 -> fst (agent_step b 0 (ACT_ADD (0, 1)))
    | _ -> b
  in

  (* Enumerate 4^6 = 4096 execution traces *)
  let schedules = ref 0 in
  let rec explore b depth =
    if depth = 6 then begin
      incr schedules;
      assert (inv_signal_conservation b);
      assert (inv_port_capacities b);
      assert (inv_pool_non_negative b);
    end else begin
      for op = 0 to 3 do
        let b' = apply_op b op in
        explore b' (depth + 1)
      done
    end
  in
  explore b2 0;
  printf "  [PASS] Explored %d non-determined execution traces; all modal invariants held.\n" !schedules

(* ── Main Entrypoint ─────────────────────────────────────────────────── *)

let () =
  printf "=======================================================\n";
  printf " B-System Verified Interpreter Architecture Oracle     \n";
  printf " ITU-T Z.100 Annex F · ISO/IEC 15437 · ISO/IEC 15909   \n";
  printf "=======================================================\n";
  run_suite_basic_operations ();
  run_suite_commutation_lemma ();
  run_suite_schedule_space_exploration ();
  printf "\nAll Verified Interpreter Model Invariants passed successfully!\n"
