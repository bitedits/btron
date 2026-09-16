(* hypermedia_dnd_model.ml
 *
 * Self-contained executable specification and verification oracle for
 * BTRON 3.20 Real Body / Virtual Body Hypermedia & DND Protocol.
 *
 * Scope: HYPERMEDIA.md Phase 2 (Clarity DTP System, Cabinet DND, TAD Persistence).
 * OCaml >= 4.14 / 5.x
 *
 * Build & test:
 *   ocamlc -o hypermedia_dnd_model hypermedia_dnd_model.ml && ./hypermedia_dnd_model
 *)

open Printf

(* ── 1. Data Model Types ──────────────────────────────────────────── *)

type robj_id = int

type vobj_type =
  | VObjText
  | VObjDraw
  | VObjExec

type robj = {
  id : robj_id;
  name : string;
  kind : vobj_type;
  path : string;
  payload : string;
}

type vobj_link = {
  vid : int;
  target_robj : robj_id;
  label : string;
  path : string;
}

type frame_kind =
  | FrameText
  | FrameImage

type rect = int * int * int * int (* left, top, right, bottom *)

type frame = {
  fid : int;
  kind : frame_kind;
  bounds : rect;
  text : string;
  vobjs : vobj_link list;
  img_robj : robj_id option;
  bitmap_bytes : int;
}

type clarity_doc = {
  fmt : int; (* 0 = A4, 1 = Shiroku, 2 = Pecha *)
  frames : frame list;
}

(* ── 2. Real Body Storage Registry ───────────────────────────────── *)

module Storage = struct
  let registry : (robj_id, robj) Hashtbl.t = Hashtbl.create 32

  let reset () = Hashtbl.clear registry

  let register (r : robj) =
    Hashtbl.replace registry r.id r

  let lookup (id : robj_id) : robj option =
    Hashtbl.find_opt registry id
end

(* ── 3. Direct Manipulation DND Protocol State Machine ───────────── *)

type dnd_state =
  | Idle
  | Dragging of robj * (int * int)
  | Hovering of robj * int * (int * int) (* robj, target_frame_id, (x, y) *)

let dnd_begin (r : robj) (pos : int * int) : dnd_state =
  Dragging (r, pos)

let dnd_move (st : dnd_state) (pos : int * int) (target_fid_opt : int option) : dnd_state =
  match st with
  | Idle -> Idle
  | Dragging (r, _) | Hovering (r, _, _) ->
      (match target_fid_opt with
       | Some fid -> Hovering (r, fid, pos)
       | None -> Dragging (r, pos))

let dnd_cancel (_st : dnd_state) : dnd_state =
  Idle

let dnd_drop (st : dnd_state) (doc : clarity_doc) : clarity_doc * dnd_state =
  match st with
  | Idle -> (doc, Idle)
  | Dragging (r, (x, y)) ->
      (* Dropped onto empty canvas -> auto-create frame *)
      let new_fid = List.length doc.frames + 1 in
      let new_frame =
        if r.kind = VObjDraw then
          { fid = new_fid; kind = FrameImage; bounds = (x, y, x + 160, y + 120);
            text = ""; vobjs = []; img_robj = Some r.id; bitmap_bytes = String.length r.payload }
        else
          let v = { vid = 1000 + new_fid; target_robj = r.id; label = r.name; path = r.path } in
          { fid = new_fid; kind = FrameText; bounds = (x, y, x + 240, y + 100);
            text = sprintf "[* %s] " r.name; vobjs = [v]; img_robj = None; bitmap_bytes = 0 }
      in
      ({ doc with frames = doc.frames @ [new_frame] }, Idle)

  | Hovering (r, target_fid, _pos) ->
      (* Dropped into an existing frame *)
      let updated_frames =
        List.map (fun f ->
          if f.fid = target_fid then
            if r.kind = VObjDraw && f.kind = FrameImage then
              { f with img_robj = Some r.id; bitmap_bytes = String.length r.payload }
            else if f.kind = FrameText then
              let v = { vid = 2000 + List.length f.vobjs; target_robj = r.id; label = r.name; path = r.path } in
              { f with text = f.text ^ sprintf "[* %s] " r.name; vobjs = f.vobjs @ [v] }
            else
              f
          else
            f
        ) doc.frames
      in
      ({ doc with frames = updated_frames }, Idle)

(* ── 4. TAD Stream Serialization & Deserialization ────────────────── *)

(* Encodes doc as a structured TAD byte stream representation *)
type tad_segment =
  | TS_INFO of int * int (* fmt, frame_count *)
  | TS_DFUSEN of int * frame_kind * rect * int (* fid, kind, bounds, robj_id *)
  | TS_TEXT of string * vobj_link list
  | TS_IMAGE of int * int * int (* w, h, bytes *)

let serialize_tad (doc : clarity_doc) : tad_segment list =
  let info = TS_INFO (doc.fmt, List.length doc.frames) in
  let frame_segs =
    List.fold_left (fun acc f ->
      let dfusen = TS_DFUSEN (f.fid, f.kind, f.bounds, Option.value f.img_robj ~default:0) in
      let payload =
        match f.kind with
        | FrameText -> [TS_TEXT (f.text, f.vobjs)]
        | FrameImage -> [TS_IMAGE (160, 120, f.bitmap_bytes)]
      in
      acc @ [dfusen] @ payload
    ) [] doc.frames
  in
  info :: frame_segs

let deserialize_tad (segs : tad_segment list) : clarity_doc =
  let fmt_ref = ref 0 in
  let frames_rev = ref [] in
  let cur_frame = ref None in

  List.iter (function
    | TS_INFO (f, _) -> fmt_ref := f
    | TS_DFUSEN (fid, k, bounds, robj_id) ->
        (match !cur_frame with
         | Some cf -> frames_rev := cf :: !frames_rev
         | None -> ());
        let img_r = if robj_id > 0 then Some robj_id else None in
        cur_frame := Some { fid; kind = k; bounds; text = ""; vobjs = []; img_robj = img_r; bitmap_bytes = 0 }
    | TS_TEXT (txt, vlist) ->
        (match !cur_frame with
         | Some cf -> cur_frame := Some { cf with text = txt; vobjs = vlist }
         | None -> ())
    | TS_IMAGE (_w, _h, bytes) ->
        (match !cur_frame with
         | Some cf -> cur_frame := Some { cf with bitmap_bytes = bytes }
         | None -> ())
  ) segs;

  (match !cur_frame with
   | Some cf -> frames_rev := cf :: !frames_rev
   | None -> ());

  { fmt = !fmt_ref; frames = List.rev !frames_rev }

(* ── 5. Invariant Checking & Verification Oracle ─────────────────── *)

let assert_prop name cond =
  if not cond then begin
    eprintf "Invariant Violation in: %s\n" name;
    exit 1
  end

let test_referential_integrity (doc : clarity_doc) : bool =
  List.for_all (fun f ->
    List.for_all (fun v ->
      Storage.lookup v.target_robj <> None
    ) f.vobjs
  ) doc.frames

let test_image_bindings (doc : clarity_doc) : bool =
  List.for_all (fun f ->
    match f.img_robj with
    | None -> true
    | Some rid ->
        (match Storage.lookup rid with
         | Some r -> r.kind = VObjDraw
         | None -> false)
  ) doc.frames

let run_suite () =
  printf "==> Initializing BTRON Hypermedia DND & TAD Model\n";
  Storage.reset ();

  let r_spec = { id = 101; name = "01_btron3_spec.tad"; kind = VObjText; path = "tad_bin/01_btron3_spec.tad"; payload = "SPEC_DATA" } in
  let r_hyp  = { id = 102; name = "HYPERMEDIA.md"; kind = VObjText; path = "doc/md/HYPERMEDIA.md"; payload = "HYPER_DATA" } in
  let r_img  = { id = 103; name = "clarity.png"; kind = VObjDraw; path = "assets/icons/clarity.png"; payload = "PNG_PIXELS_RGBA" } in

  Storage.register r_spec;
  Storage.register r_hyp;
  Storage.register r_img;

  let base_doc = {
    fmt = 0; (* A4 *)
    frames = [
      { fid = 1; kind = FrameImage; bounds = (24, 24, 184, 184); text = ""; vobjs = []; img_robj = None; bitmap_bytes = 0 };
      { fid = 2; kind = FrameText; bounds = (200, 24, 600, 200); text = "Ceremony: "; vobjs = []; img_robj = None; bitmap_bytes = 0 };
    ]
  } in

  (* 1. Test DND Image Drag & Drop into FrameImage *)
  let st1 = dnd_begin r_img (50, 50) in
  assert_prop "st1 is Dragging" (match st1 with Dragging (r, _) -> r.id = 103 | _ -> false);

  let st2 = dnd_move st1 (100, 100) (Some 1) in
  assert_prop "st2 is Hovering over Frame 1" (match st2 with Hovering (_, 1, _) -> true | _ -> false);

  let (doc1, st3) = dnd_drop st2 base_doc in
  assert_prop "st3 is Idle" (st3 = Idle);
  let f1 = List.find (fun f -> f.fid = 1) doc1.frames in
  assert_prop "Frame 1 has r_img binding" (f1.img_robj = Some 103 && f1.bitmap_bytes = String.length r_img.payload);

  (* 2. Test DND Document Drag & Drop into FrameText *)
  let st4 = dnd_begin r_spec (300, 80) in
  let st5 = dnd_move st4 (320, 80) (Some 2) in
  let (doc2, st6) = dnd_drop st5 doc1 in
  assert_prop "st6 is Idle" (st6 = Idle);
  let f2 = List.find (fun f -> f.fid = 2) doc2.frames in
  assert_prop "Frame 2 contains Virtual Body link" (List.length f2.vobjs = 1);
  let vb1 = List.hd f2.vobjs in
  assert_prop "vb1 references r_spec" (vb1.target_robj = 101 && vb1.label = "01_btron3_spec.tad");

  (* 3. Test Second Virtual Body into FrameText *)
  let st7 = dnd_begin r_hyp (400, 80) in
  let st8 = dnd_move st7 (350, 80) (Some 2) in
  let (doc3, _) = dnd_drop st8 doc2 in
  let f2_updated = List.find (fun f -> f.fid = 2) doc3.frames in
  assert_prop "Frame 2 has 2 Virtual Bodies" (List.length f2_updated.vobjs = 2);

  (* 4. Test Referential Integrity & Invariant Preservation *)
  assert_prop "Referential Integrity Preserved" (test_referential_integrity doc3);
  assert_prop "Image Bindings Sound" (test_image_bindings doc3);

  (* 5. Test Double-Click Resolution *)
  let resolved_bodies =
    List.map (fun v ->
      match Storage.lookup v.target_robj with
      | Some r -> r.name
      | None -> failwith "Unresolved reference"
    ) f2_updated.vobjs
  in
  assert_prop "Double Click resolves target Real Bodies" (resolved_bodies = ["01_btron3_spec.tad"; "HYPERMEDIA.md"]);

  (* 6. Test TAD Stream Roundtrip Equivalence *)
  let tad_stream = serialize_tad doc3 in
  assert_prop "TAD stream generated" (List.length tad_stream >= 4);
  let restored_doc = deserialize_tad tad_stream in
  assert_prop "TAD Roundtrip Equivalence: fmt" (restored_doc.fmt = doc3.fmt);
  assert_prop "TAD Roundtrip Equivalence: frame_count" (List.length restored_doc.frames = List.length doc3.frames);

  let rf1 = List.find (fun f -> f.fid = 1) restored_doc.frames in
  let rf2 = List.find (fun f -> f.fid = 2) restored_doc.frames in
  assert_prop "Restored Frame 1 Image Real Body" (rf1.img_robj = Some 103);
  assert_prop "Restored Frame 2 Virtual Body count" (List.length rf2.vobjs = 2);
  assert_prop "Restored Frame 2 VB1 Target" ((List.nth rf2.vobjs 0).target_robj = 101);
  assert_prop "Restored Frame 2 VB2 Target" ((List.nth rf2.vobjs 1).target_robj = 102);

  printf "PASS: OCaml oracle (hypermedia_dnd_model.ml) - Invariants 1..6 passed.\n"

let () = run_suite ()
