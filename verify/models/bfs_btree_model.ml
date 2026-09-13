(* bfs_btree_model.ml
 *
 * Self-contained executable model of B-System B-FS Parameterized B+Tree.
 * Supports:
 *  - Parameterized node sizes (1024, 2048, 4096 bytes)
 *  - 64-bit FIDs (Int64)
 *  - Node splitting, internal routing, parent key promotion
 *  - Leaf sibling links (left/right) for O(1) range iteration
 *  - Rigorous invariant verification (sortedness, balance, leaf chaining)
 *
 * Build & run:
 *   ocamlc -o bfs_btree_model bfs_btree_model.ml && ./bfs_btree_model
 *)

open Printf

type fid64 = int64
type node_id = int64

let null_node : node_id = -1L

type node_size = Size1K | Size2K | Size4K

let bytes_of_node_size = function
  | Size1K -> 1024
  | Size2K -> 2048
  | Size4K -> 4096

let max_keys_of_node_size = function
  | Size1K -> 8     (* scaled down for model testing of splits *)
  | Size2K -> 16
  | Size4K -> 32

type btree_node = {
  id : node_id;
  is_leaf : bool;
  mutable left_link : node_id;
  mutable right_link : node_id;
  mutable overflow_link : node_id; (* for internal nodes *)
  mutable keys : string list;
  mutable values : fid64 list;     (* for leaf: FID64; for internal: child node_ids *)
}

type btree = {
  node_size : node_size;
  max_keys : int;
  mutable root_id : node_id;
  nodes : (node_id, btree_node) Hashtbl.t;
  mutable next_node_id : node_id;
}

let create_tree (ns : node_size) : btree =
  let max_keys = max_keys_of_node_size ns in
  let nodes = Hashtbl.create 64 in
  let root = {
    id = 1L;
    is_leaf = true;
    left_link = null_node;
    right_link = null_node;
    overflow_link = null_node;
    keys = [];
    values = [];
  } in
  Hashtbl.add nodes 1L root;
  {
    node_size = ns;
    max_keys;
    root_id = 1L;
    nodes;
    next_node_id = 2L;
  }

let alloc_node (t : btree) (is_leaf : bool) : btree_node =
  let nid = t.next_node_id in
  t.next_node_id <- Int64.add t.next_node_id 1L;
  let n = {
    id = nid;
    is_leaf;
    left_link = null_node;
    right_link = null_node;
    overflow_link = null_node;
    keys = [];
    values = [];
  } in
  Hashtbl.add t.nodes nid n;
  n

let get_node (t : btree) (id : node_id) : btree_node =
  Hashtbl.find t.nodes id

(* Binary/Linear search for key in sorted keys list *)
let find_key_pos (keys : string list) (k : string) : int =
  let rec aux i = function
    | [] -> i
    | x :: xs ->
        if String.compare k x <= 0 then i
        else aux (i + 1) xs
  in
  aux 0 keys

let list_insert_at (idx : int) (x : 'a) (xs : 'a list) : 'a list =
  let rec aux i = function
    | [] -> [x]
    | y :: ys ->
        if i = 0 then x :: y :: ys
        else y :: aux (i - 1) ys
  in
  aux idx xs

let split_list_at (idx : int) (xs : 'a list) : 'a list * 'a list =
  let rec aux i acc = function
    | [] -> (List.rev acc, [])
    | y :: ys ->
        if i = 0 then (List.rev acc, y :: ys)
        else aux (i - 1) (y :: acc) ys
  in
  aux idx [] xs

(* Search in tree *)
let rec search (t : btree) (curr : btree_node) (k : string) : fid64 option =
  if curr.is_leaf then
    let rec find_kv ks vs =
      match ks, vs with
      | [], _ | _, [] -> None
      | x :: xs, v :: vs' ->
          if String.compare k x = 0 then Some v
          else if String.compare k x < 0 then None
          else find_kv xs vs'
    in
    find_kv curr.keys curr.values
  else
    let rec route i = function
      | [] -> curr.overflow_link
      | k_node :: rest ->
          if String.compare k k_node <= 0 then
            List.nth curr.values i
          else
            route (i + 1) rest
    in
    let child_id = route 0 curr.keys in
    search t (get_node t child_id) k

(* Range scan over leaf links *)
let range_scan (t : btree) (k_min : string) (k_max : string) : (string * fid64) list =
  (* 1. Find leaf for k_min *)
  let rec find_leaf (curr : btree_node) =
    if curr.is_leaf then curr
    else
      let rec route i = function
        | [] -> curr.overflow_link
        | k_node :: rest ->
            if String.compare k_min k_node <= 0 then
              List.nth curr.values i
            else
              route (i + 1) rest
      in
      let child_id = route 0 curr.keys in
      find_leaf (get_node t child_id)
  in
  let start_leaf = find_leaf (get_node t t.root_id) in
  (* 2. Follow right_link *)
  let rec scan_leaves (leaf : btree_node) acc =
    let rec gather_in_leaf ks vs cur_acc =
      match ks, vs with
      | [], _ | _, [] -> cur_acc
      | k :: rest_k, v :: rest_v ->
          if String.compare k k_min < 0 then
            gather_in_leaf rest_k rest_v cur_acc
          else if String.compare k k_max > 0 then
            cur_acc
          else
            gather_in_leaf rest_k rest_v ((k, v) :: cur_acc)
    in
    let acc' = gather_in_leaf leaf.keys leaf.values acc in
    let exceeded =
      match List.rev leaf.keys with
      | [] -> false
      | last_k :: _ -> String.compare last_k k_max >= 0
    in
    if exceeded || leaf.right_link = null_node then
      List.rev acc'
    else
      scan_leaves (get_node t leaf.right_link) acc'
  in
  scan_leaves start_leaf []

(* Split a full leaf or internal node *)
type split_result = {
  promoted_key : string;
  left_child : node_id;
  right_child : node_id;
}

let rec insert_rec (t : btree) (curr : btree_node) (k : string) (v : fid64) : split_result option =
  if curr.is_leaf then begin
    let pos = find_key_pos curr.keys k in
    if pos < List.length curr.keys && String.compare (List.nth curr.keys pos) k = 0 then begin
      let rec update_nth i x = function
        | [] -> []
        | y :: ys -> if i = 0 then x :: ys else y :: update_nth (i - 1) x ys
      in
      curr.values <- update_nth pos v curr.values;
      None
    end else begin
      curr.keys <- list_insert_at pos k curr.keys;
      curr.values <- list_insert_at pos v curr.values;
      if List.length curr.keys > t.max_keys then begin
        let mid = List.length curr.keys / 2 in
        let (left_k, right_k) = split_list_at mid curr.keys in
        let (left_v, right_v) = split_list_at mid curr.values in
        let sibling = alloc_node t true in
        sibling.keys <- right_k;
        sibling.values <- right_v;
        curr.keys <- left_k;
        curr.values <- left_v;

        sibling.right_link <- curr.right_link;
        sibling.left_link <- curr.id;
        if curr.right_link <> null_node then begin
          let next_leaf = get_node t curr.right_link in
          next_leaf.left_link <- sibling.id
        end;
        curr.right_link <- sibling.id;

        Some {
          promoted_key = List.hd (List.rev left_k);
          left_child = curr.id;
          right_child = sibling.id;
        }
      end else
        None
    end
  end else begin
    (* Internal node *)
    let rec route i = function
      | [] -> (i, true, curr.overflow_link)
      | k_node :: rest ->
          if String.compare k k_node <= 0 then
            (i, false, List.nth curr.values i)
          else
            route (i + 1) rest
    in
    let (route_idx, is_overflow, child_id) = route 0 curr.keys in
    let child = get_node t child_id in
    match insert_rec t child k v with
    | None -> None
    | Some split ->
        let promo_pos = find_key_pos curr.keys split.promoted_key in
        curr.keys <- list_insert_at promo_pos split.promoted_key curr.keys;
        if is_overflow then begin
          curr.values <- curr.values @ [split.left_child];
          curr.overflow_link <- split.right_child
        end else begin
          let rec replace_and_insert i lc rc = function
            | [] -> [lc; rc]
            | y :: ys ->
                if i = 0 then lc :: rc :: ys
                else y :: replace_and_insert (i - 1) lc rc ys
          in
          curr.values <- replace_and_insert route_idx split.left_child split.right_child curr.values
        end;
        if List.length curr.keys > t.max_keys then begin
          let mid = List.length curr.keys / 2 in
          let promo_k = List.nth curr.keys mid in
          let (left_k, right_k_with_mid) = split_list_at mid curr.keys in
          let right_k = List.tl right_k_with_mid in
          let (left_v, right_v) = split_list_at (mid + 1) curr.values in
          let sibling = alloc_node t false in
          sibling.keys <- right_k;
          sibling.values <- right_v;
          sibling.overflow_link <- curr.overflow_link;
          curr.keys <- left_k;
          curr.overflow_link <- List.hd (List.rev left_v);
          curr.values <- List.rev (List.tl (List.rev left_v));

          Some {
            promoted_key = promo_k;
            left_child = curr.id;
            right_child = sibling.id;
          }
        end else
          None
  end

let insert (t : btree) (k : string) (v : fid64) : unit =
  let root = get_node t t.root_id in
  match insert_rec t root k v with
  | None -> ()
  | Some split ->
      (* Root split -> create new root *)
      let new_root = alloc_node t false in
      new_root.keys <- [split.promoted_key];
      new_root.values <- [split.left_child];
      new_root.overflow_link <- split.right_child;
      t.root_id <- new_root.id

(* ── Invariant Verifications ────────────────────────────────────── *)

let rec is_sorted = function
  | [] | [_] -> true
  | x :: (y :: _ as rest) ->
      String.compare x y < 0 && is_sorted rest

let inv_all_nodes_sorted (t : btree) : bool =
  let ok = ref true in
  Hashtbl.iter (fun _ n ->
    if not (is_sorted n.keys) then ok := false
  ) t.nodes;
  !ok

let inv_leaf_chaining (t : btree) : bool =
  let rec check_chain curr_id prev_id =
    if curr_id = null_node then true
    else
      let curr = get_node t curr_id in
      if not curr.is_leaf then false
      else if curr.left_link <> prev_id then false
      else check_chain curr.right_link curr_id
  in
  (* Find leftmost leaf *)
  let rec find_leftmost curr =
    if curr.is_leaf then curr.id
    else
      let first_child = List.hd curr.values in
      find_leftmost (get_node t first_child)
  in
  let leftmost = find_leftmost (get_node t t.root_id) in
  check_chain leftmost null_node

let inv_depth_uniformity (t : btree) : bool =
  let leaf_depths = ref [] in
  let rec traverse depth curr =
    if curr.is_leaf then
      leaf_depths := depth :: !leaf_depths
    else
      List.iter (fun cid -> traverse (depth + 1) (get_node t cid))
        (curr.values @ [curr.overflow_link])
  in
  traverse 0 (get_node t t.root_id);
  match !leaf_depths with
  | [] -> true
  | d :: ds -> List.for_all (fun x -> x = d) ds

(* ── Demonstration & Invariant Runner ───────────────────────────── *)

let run_btree_suite (ns : node_size) =
  let sz_bytes = bytes_of_node_size ns in
  printf "\n--- Testing B-FS B+Tree Model (Node Size %d Bytes) ---\n" sz_bytes;
  let t = create_tree ns in

  (* Insert 50 test entries with 64-bit FIDs *)
  let n = 50 in
  for i = 1 to n do
    let k = sprintf "file_%04d.tad" i in
    let fid = Int64.of_int (0x10000000 + i * 4096) in
    insert t k fid;
  done;

  printf "Inserted %d records. Total nodes in tree: %d\n" n (Hashtbl.length t.nodes);

  (* 1. Sorted keys invariant *)
  assert (inv_all_nodes_sorted t);
  printf "  [PASS] inv_all_nodes_sorted\n";

  (* 2. Leaf chaining invariant *)
  assert (inv_leaf_chaining t);
  printf "  [PASS] inv_leaf_chaining\n";

  (* 3. Uniform tree depth invariant *)
  assert (inv_depth_uniformity t);
  printf "  [PASS] inv_depth_uniformity\n";

  (* 4. Point lookup correctness *)
  for i = 1 to n do
    let k = sprintf "file_%04d.tad" i in
    let expected = Int64.of_int (0x10000000 + i * 4096) in
    match search t (get_node t t.root_id) k with
    | None -> failwith (sprintf "Key %s missing!" k)
    | Some found ->
        if found <> expected then failwith (sprintf "FID mismatch for %s" k)
  done;
  printf "  [PASS] Point lookups 100%% correct\n";

  (* 5. Range scan correctness *)
  let range = range_scan t "file_0010.tad" "file_0020.tad" in
  assert (List.length range = 11);
  assert (fst (List.hd range) = "file_0010.tad");
  assert (fst (List.hd (List.rev range)) = "file_0020.tad");
  printf "  [PASS] Range scan verified (returned %d records)\n" (List.length range);
  printf "All invariants for Node Size %d passed successfully!\n" sz_bytes

let () =
  printf "=== B-System B-FS Parameterized B+Tree Formal Model ===\n";
  run_btree_suite Size1K;
  run_btree_suite Size2K;
  run_btree_suite Size4K;
  printf "\n=== ALL B+TREE SUITES PASSED (1K, 2K, 4K) ===\n"
