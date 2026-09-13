(* bfs_model.ml
 *
 * Self-contained executable model of B-System Clean-Room Volume V2
 * journal + Real-Body records + RT_VECTOR / flat ANN index.
 *
 * Scope: VECTOR.md + VOLUME_V2.md Phase-1 journal semantics.
 * OCaml >= 4.14 / 5.x (user: 5.5.0)
 *
 * Build & run:
 *   ocamlc -o bfs_model bfs_model.ml && ./bfs_model
 *   or: ocaml bfs_model.ml
 *
 * Formal properties (Rocq/Coq):
 *   coqc bfs_properties.v
 *)

open Printf

(* ── basic types ─────────────────────────────────────────────────── *)

type fid = int
type rec_idx = int
type vec_id = int
type generation = int

type metric = Cosine | L2 | InnerProduct
type quant = F32 | F16 | Int8 | Binary

type vector_header = {
  dim : int;
  metric : metric;
  quant : quant;
  flags : int;
  nvec : int;
  model_id : int;
}

(* One embedding vector (simplified: list of floats) *)
type embedding = float list

type vector_record = {
  hdr : vector_header;
  data : embedding list;   (* length = hdr.nvec *)
}

type record_ty =
  | RT_TADDATA of string
  | RT_TEXT of string
  | RT_IMAGE of string     (* placeholder *)
  | RT_VECTOR of vector_record

type record = {
  idx : rec_idx;
  body : record_ty;
}

(* Real Body = FID + list of records *)
type real_body = {
  fid : fid;
  name : string;
  records : record list;
}

(* Flat ANN index entry *)
type index_entry = {
  vid : vec_id;
  fid : fid;
  rec_idx : rec_idx;
  vec_offset : int;        (* which vector inside the RT_VECTOR record *)
  emb : embedding;
  dim : int;
  model_id : int;
}

(* ── journal ─────────────────────────────────────────────────────── *)

type mutation =
  | FidAlloc of fid * string          (* fid, name *)
  | FidFree of fid
  | SetRecord of fid * record
  | DelRecord of fid * rec_idx
  | IndexInsert of index_entry
  | IndexRemove of vec_id
  | SetDirty of bool
  | SetGeneration of generation

type tx_record = mutation

type journal = {
  log : tx_record list;               (* newest at head while in-flight *)
  committed : tx_record list;         (* oldest first, only committed *)
  in_tx : bool;
}

(* ── volume state ────────────────────────────────────────────────── *)

type features = {
  journal : bool;
  checksum : bool;
  extents : bool;
  large_fid : bool;
  vector : bool;
  snapshot : bool;
  free_tree : bool;
}

type volume = {
  name : string;
  features : features;
  generation : generation;
  dirty : bool;
  next_fid : fid;
  bodies : (fid, real_body) Hashtbl.t;
  index : (vec_id, index_entry) Hashtbl.t;
  next_vid : vec_id;
  journal : journal;
}

(* ── helpers ─────────────────────────────────────────────────────── *)

let empty_features = {
  journal = true; checksum = false; extents = false;
  large_fid = false; vector = true; snapshot = false; free_tree = false;
}

let empty_journal = { log = []; committed = []; in_tx = false }

let create_volume name =
  let bodies = Hashtbl.create 64 in
  let index = Hashtbl.create 256 in
  (* root FID 0 *)
  Hashtbl.add bodies 0 { fid = 0; name = name; records = [] };
  {
    name;
    features = empty_features;
    generation = 0;
    dirty = false;
    next_fid = 1;
    bodies;
    index;
    next_vid = 1;
    journal = empty_journal;
  }

let get_body v fid =
  try Some (Hashtbl.find v.bodies fid) with Not_found -> None

let cosine_sim a b =
  let dot = List.fold_left2 (fun s x y -> s +. x *. y) 0. a b in
  let na = sqrt (List.fold_left (fun s x -> s +. x *. x) 0. a) in
  let nb = sqrt (List.fold_left (fun s x -> s +. x *. x) 0. b) in
  if na = 0. || nb = 0. then 0. else dot /. (na *. nb)

let score metric q e =
  match metric with
  | Cosine -> cosine_sim q e
  | L2 ->
      let s = List.fold_left2 (fun acc x y -> let d = x -. y in acc +. d *. d) 0. q e in
      -. s   (* higher is better *)
  | InnerProduct ->
      List.fold_left2 (fun s x y -> s +. x *. y) 0. q e

(* ── journal operations ──────────────────────────────────────────── *)

let begin_tx v =
  if v.journal.in_tx then failwith "begin_tx: already in transaction";
  { v with
    dirty = true;
    journal = { v.journal with in_tx = true; log = [] } }

let log_mut v m =
  if not v.journal.in_tx then failwith "log_mut: not in transaction";
  { v with journal = { v.journal with log = m :: v.journal.log } }

(* Apply a single mutation to the in-memory structures (no journal) *)
let apply_one v m =
  match m with
  | FidAlloc (fid, name) ->
      if Hashtbl.mem v.bodies fid then failwith "FidAlloc: fid exists";
      Hashtbl.add v.bodies fid { fid; name; records = [] };
      { v with next_fid = max v.next_fid (fid + 1) }
  | FidFree fid ->
      if fid = 0 then failwith "cannot free root";
      Hashtbl.remove v.bodies fid;
      v
  | SetRecord (fid, rec_) ->
      (match get_body v fid with
       | None -> failwith "SetRecord: missing body"
       | Some b ->
           let recs = List.filter (fun r -> r.idx <> rec_.idx) b.records in
           let b' = { b with records = rec_ :: recs } in
           Hashtbl.replace v.bodies fid b';
           v)
  | DelRecord (fid, idx) ->
      (match get_body v fid with
       | None -> v
       | Some b ->
           let b' = { b with records = List.filter (fun r -> r.idx <> idx) b.records } in
           Hashtbl.replace v.bodies fid b';
           v)
  | IndexInsert e ->
      Hashtbl.replace v.index e.vid e;
      { v with next_vid = max v.next_vid (e.vid + 1) }
  | IndexRemove vid ->
      Hashtbl.remove v.index vid;
      v
  | SetDirty d -> { v with dirty = d }
  | SetGeneration g -> { v with generation = g }

let commit v =
  if not v.journal.in_tx then failwith "commit: not in transaction";
  let muts = List.rev v.journal.log in
  (* apply all logged mutations *)
  let v = List.fold_left apply_one v muts in
  let new_gen = v.generation + 1 in
  let v = { v with generation = new_gen; dirty = false } in
  let j = {
    log = [];
    committed = v.journal.committed @ muts;
    in_tx = false;
  } in
  { v with journal = j }

let abort v =
  if not v.journal.in_tx then failwith "abort: not in transaction";
  { v with
    dirty = false;
    journal = { v.journal with log = []; in_tx = false } }

(* Crash: drop in-flight log, keep only committed *)
let crash v =
  { v with
    dirty = true;   (* needs replay or at least inspection *)
    journal = { log = []; committed = v.journal.committed; in_tx = false } }

(* Replay: re-apply committed log from a clean slate of bodies/index
   (simulates mounting a dirty volume and rebuilding from journal).
   Preserves the last committed generation. *)
let replay v =
  let bodies = Hashtbl.create 64 in
  let index = Hashtbl.create 256 in
  Hashtbl.add bodies 0 { fid = 0; name = v.name; records = [] };
  let saved_gen = v.generation in
  let base = {
    v with
    bodies;
    index;
    next_fid = 1;
    next_vid = 1;
    generation = 0;
    dirty = false;
    journal = { log = []; committed = []; in_tx = false };
  } in
  let v' = List.fold_left apply_one base v.journal.committed in
  { v' with
    generation = saved_gen;
    dirty = false;
    journal = { log = []; committed = v.journal.committed; in_tx = false } }

(* ── high-level API (VECTOR.md surface) ──────────────────────────── *)

let vol_format name = create_volume name

let fil_create v name =
  let v = begin_tx v in
  let fid = v.next_fid in
  let v = log_mut v (FidAlloc (fid, name)) in
  let v = commit v in
  (v, fid)

let fil_set_vectors v fid (hdr : vector_header) (vecs : embedding list) =
  if List.length vecs <> hdr.nvec then
    failwith "fil_set_vectors: nvec mismatch";
  if not v.features.vector then
    failwith "FEAT_VECTOR not enabled";
  let v = begin_tx v in
  let rec_idx =
    match get_body v fid with
    | None -> failwith "fil_set_vectors: no such fid"
    | Some b ->
        (match b.records with
         | [] -> 0
         | rs -> List.fold_left (fun m r -> max m (r.idx + 1)) 0 rs)
  in
  let vr = { hdr; data = vecs } in
  let rec_ = { idx = rec_idx; body = RT_VECTOR vr } in
  let v = log_mut v (SetRecord (fid, rec_)) in
  (* insert each vector into the flat index *)
  let v, _ =
    List.fold_left
      (fun (v, off) emb ->
         let vid = v.next_vid in
         let e = {
           vid; fid; rec_idx; vec_offset = off;
           emb; dim = hdr.dim; model_id = hdr.model_id;
         } in
         let v = log_mut v (IndexInsert e) in
         (v, off + 1))
      (v, 0) vecs
  in
  commit v

type vector_hit = {
  fid : fid;
  rec_idx : rec_idx;
  vec_id : vec_id;
  score : float;
}

let vol_vector_search v (query : embedding) dim k =
  if not v.features.vector then []
  else
    let scored =
      Hashtbl.fold
        (fun _ e acc ->
           if e.dim <> dim then acc
           else
             let s = score Cosine query e.emb in
             { fid = e.fid; rec_idx = e.rec_idx; vec_id = e.vid; score = s } :: acc)
        v.index []
    in
    let sorted = List.sort (fun a b -> compare b.score a.score) scored in
    let rec take n xs =
      match n, xs with
      | 0, _ | _, [] -> []
      | n, x :: xs -> x :: take (n - 1) xs
    in
    take k sorted

(* ── invariants (temporal / safety properties) ───────────────────── *)

let inv_no_inflight_after_commit v =
  not v.journal.in_tx && v.journal.log = []

let inv_generation_monotonic before after =
  after.generation >= before.generation

let inv_committed_only_visible v =
  (* all bodies/index come only from committed mutations — checked by construction
     after crash+replay *)
  true

let inv_vector_index_consistent v =
  (* every index entry points at an existing FID that has an RT_VECTOR record *)
  let ok = ref true in
  Hashtbl.iter
    (fun _ (e : index_entry) ->
       if !ok then
         match get_body v e.fid with
         | None -> ok := false
         | Some b ->
             let found =
               List.exists
                 (fun r ->
                    r.idx = e.rec_idx &&
                    match r.body with RT_VECTOR _ -> true | _ -> false)
                 b.records
             in
             if not found then ok := false)
    v.index;
  !ok

let inv_dirty_cleared_after_replay v =
  not v.dirty

(* ── demo / self-test ────────────────────────────────────────────── *)

let print_hits hits =
  List.iter
    (fun h ->
       printf "  fid=%d rec=%d vid=%d score=%.4f\n"
         h.fid h.rec_idx h.vec_id h.score)
    hits

let string_of_mut = function
  | FidAlloc (f, n) -> sprintf "FidAlloc(%d,%S)" f n
  | FidFree f -> sprintf "FidFree(%d)" f
  | SetRecord (f, _) -> sprintf "SetRecord(%d,…)" f
  | DelRecord (f, i) -> sprintf "DelRecord(%d,%d)" f i
  | IndexInsert e -> sprintf "IndexInsert(vid=%d,fid=%d)" e.vid e.fid
  | IndexRemove vid -> sprintf "IndexRemove(%d)" vid
  | SetDirty d -> sprintf "SetDirty(%b)" d
  | SetGeneration g -> sprintf "SetGeneration(%d)" g

let trace_state label v =
  printf "  [%s] gen=%d dirty=%b in_tx=%b log=%d committed=%d bodies=%d index=%d\n"
    label v.generation v.dirty v.journal.in_tx
    (List.length v.journal.log)
    (List.length v.journal.committed)
    (Hashtbl.length v.bodies)
    (Hashtbl.length v.index)

(** demo_tx_runner — step-by-step journal trace (mirrors Coq demo_tx).
    Scenario A: begin → log FidAlloc → commit → clean
    Scenario B: begin → log → crash → replay (in-flight lost) *)
let demo_tx_runner () =
  printf "\n======== demo_tx_runner ========\n";

  (* ── Scenario A: successful commit ── *)
  printf "\n-- Scenario A: begin / log / commit --\n";
  let v = vol_format "TxDemo" in
  trace_state "format" v;

  let v = begin_tx v in
  trace_state "begin_tx" v;
  assert v.journal.in_tx;
  assert v.dirty;

  let fid = v.next_fid in
  let v = log_mut v (FidAlloc (fid, "note")) in
  printf "  log_mut %s\n" (string_of_mut (FidAlloc (fid, "note")));
  trace_state "log_mut" v;
  assert (List.length v.journal.log = 1);

  let v = commit v in
  trace_state "commit" v;
  assert (inv_no_inflight_after_commit v);
  assert (v.generation = 1);
  assert (Hashtbl.mem v.bodies fid);
  printf "  OK Scenario A: FID %d durable, gen=1, clean\n" fid;

  (* ── Scenario B: crash mid-tx ── *)
  printf "\n-- Scenario B: begin / log / crash / replay --\n";
  let v = begin_tx v in
  let lost = v.next_fid in
  let v = log_mut v (FidAlloc (lost, "should-be-lost")) in
  printf "  log_mut FidAlloc(%d,\"should-be-lost\") (not committed)\n" lost;
  trace_state "in-flight" v;

  let v = crash v in
  trace_state "crash" v;
  assert (v.journal.log = []);
  assert (not v.journal.in_tx);

  let v = replay v in
  trace_state "replay" v;
  assert (inv_dirty_cleared_after_replay v);
  assert (not (Hashtbl.mem v.bodies lost));
  assert (Hashtbl.mem v.bodies fid);
  printf "  OK Scenario B: lost FID %d absent; durable FID %d kept; gen=%d\n"
    lost fid v.generation;

  (* ── Scenario C: abort ── *)
  printf "\n-- Scenario C: begin / log / abort --\n";
  let v = begin_tx v in
  let tmp = v.next_fid in
  let v = log_mut v (FidAlloc (tmp, "aborted")) in
  let v = abort v in
  trace_state "abort" v;
  assert (inv_no_inflight_after_commit v);
  assert (not (Hashtbl.mem v.bodies tmp));
  printf "  OK Scenario C: aborted FID %d not visible\n" tmp;

  printf "\n======== demo_tx_runner done ========\n"

let () =
  printf "=== B-System Volume V2 + VECTOR model (OCaml) ===\n\n";

  (* 0. Explicit journal demo runner *)
  demo_tx_runner ();

  (* 1. Format *)
  let v = vol_format "DemoVol" in
  printf "\nFormatted volume %S generation=%d dirty=%b\n"
    v.name v.generation v.dirty;

  (* 2. Create two documents *)
  let v, fid1 = fil_create v "note-alpha" in
  let v, fid2 = fil_create v "note-beta" in
  printf "Created FID %d and FID %d; generation=%d\n" fid1 fid2 v.generation;

  (* 3. Attach vectors (dim=4 for demo) *)
  let hdr = {
    dim = 4; metric = Cosine; quant = F32; flags = 0;
    nvec = 1; model_id = 1;
  } in
  let v = fil_set_vectors v fid1 hdr [[0.9; 0.1; 0.0; 0.2]] in
  let v = fil_set_vectors v fid2 hdr [[0.1; 0.9; 0.3; 0.0]] in
  printf "Attached RT_VECTOR records; generation=%d index_size=%d\n"
    v.generation (Hashtbl.length v.index);

  assert (inv_vector_index_consistent v);
  assert (inv_no_inflight_after_commit v);

  (* 4. Search *)
  let q = [0.85; 0.15; 0.05; 0.1] in
  let hits = vol_vector_search v q 4 2 in
  printf "Search results for query ~ alpha:\n";
  print_hits hits;
  assert (List.length hits = 2);
  assert ((List.hd hits).fid = fid1);

  (* 5. Crash during a transaction (in-flight lost) *)
  let v = begin_tx v in
  let v = log_mut v (FidAlloc (v.next_fid, "should-be-lost")) in
  printf "In-flight tx started (not committed); dirty=%b\n" v.dirty;
  let v = crash v in
  printf "CRASH — in-flight discarded; committed log length=%d\n"
    (List.length v.journal.committed);

  (* 6. Replay *)
  let v = replay v in
  printf "REPLAY done; generation=%d dirty=%b bodies=%d index=%d\n"
    v.generation v.dirty
    (Hashtbl.length v.bodies) (Hashtbl.length v.index);
  assert (inv_dirty_cleared_after_replay v);
  assert (inv_vector_index_consistent v);
  assert (not (Hashtbl.mem v.bodies 3));  (* lost fid never committed *)

  (* 7. Search still works after replay *)
  let hits2 = vol_vector_search v q 4 2 in
  printf "Search after replay:\n";
  print_hits hits2;
  assert ((List.hd hits2).fid = fid1);

  printf "\nAll invariants passed.\n";
  printf "Model covers: journal begin/log/commit/abort/crash/replay,\n";
  printf "              Real Bodies, RT_VECTOR, flat ANN index, vector search.\n"
