(* ui_compose_model.ml
 *
 * Executable Operational Oracle for B-System GUI Compositor & Invalidation Subsystem.
 * Covers:
 *  - Rectangular Algebra (intersection, union bounding box, clipping, point containment)
 *  - Invalidation & Damage Accumulation (wnd_inval_damage_rect, wnd_take_inval_damage)
 *  - Bit Blitting Engine (btron_row_blit, copy_opaque_span, key transparency, ROPs)
 *  - Double Buffering & Presentation Backend (RAM backbuffer -> VideoCore/VRAM framebuffer)
 *  - Multi-layer Composition with Painter's Algorithm & Visible Region Clipping
 *
 * Build & run:
 *   ocamlc -o ui_compose_model ui_compose_model.ml && ./ui_compose_model
 *)

open Printf

(* =====================================================================
   §1  Rectangular Geometry & Clipping Algebra
   ===================================================================== *)

type rect = {
  left : int;
  top : int;
  right : int;
  bottom : int;
}

let rect_empty = { left = 0; top = 0; right = 0; bottom = 0 }

let rect_is_valid (r : rect) : bool =
  r.left < r.right && r.top < r.bottom

let rect_width (r : rect) : int =
  if r.right > r.left then r.right - r.left else 0

let rect_height (r : rect) : int =
  if r.bottom > r.top then r.bottom - r.top else 0

let rect_area (r : rect) : int =
  (rect_width r) * (rect_height r)

let point_in_rect (x : int) (y : int) (r : rect) : bool =
  r.left <= x && x < r.right && r.top <= y && y < r.bottom

let rect_intersect (a : rect) (b : rect) : rect =
  let l = max a.left b.left in
  let t = max a.top b.top in
  let r = min a.right b.right in
  let btm = min a.bottom b.bottom in
  if l < r && t < btm then
    { left = l; top = t; right = r; bottom = btm }
  else
    rect_empty

let rect_union_bbox (a : rect) (b : rect) : rect =
  if not (rect_is_valid a) then b
  else if not (rect_is_valid b) then a
  else
    {
      left = min a.left b.left;
      top = min a.top b.top;
      right = max a.right b.right;
      bottom = max a.bottom b.bottom;
    }

let rect_contains (outer : rect) (inner : rect) : bool =
  if not (rect_is_valid inner) then true
  else
    outer.left <= inner.left && inner.right <= outer.right &&
    outer.top <= inner.top && inner.bottom <= outer.bottom

(* =====================================================================
   §2  Invalidation & Damage Tracking
   ===================================================================== *)

type damage_tracker = {
  mutable damage : rect option;
  screen_w : int;
  screen_h : int;
}

let create_damage_tracker (w : int) (h : int) : damage_tracker = {
  damage = None;
  screen_w = w;
  screen_h = h;
}

let inval_damage_rect (dt : damage_tracker) (r : rect) : unit =
  let screen_rect = { left = 0; top = 0; right = dt.screen_w; bottom = dt.screen_h } in
  let clipped = rect_intersect r screen_rect in
  if rect_is_valid clipped then
    match dt.damage with
    | None -> dt.damage <- Some clipped
    | Some cur -> dt.damage <- Some (rect_union_bbox cur clipped)

let take_inval_damage (dt : damage_tracker) : rect option =
  let res = dt.damage in
  dt.damage <- None;
  res

let has_pending_damage (dt : damage_tracker) : bool =
  match dt.damage with
  | Some d -> rect_is_valid d
  | None -> false

(* =====================================================================
   §3  Bit Blitting Engine & Raster Operations
   ===================================================================== *)

type rop =
  | ROP_COPY
  | ROP_OR
  | ROP_XOR
  | ROP_AND
  | ROP_INVERT

type surface = {
  width : int;
  height : int;
  pixels : int array; (* 32-bit ARGB: 0xAARRGGBB *)
}

let create_surface (w : int) (h : int) (clear_col : int) : surface = {
  width = w;
  height = h;
  pixels = Array.make (w * h) clear_col;
}

let surface_get (s : surface) (x : int) (y : int) : int =
  if x >= 0 && x < s.width && y >= 0 && y < s.height then
    s.pixels.(y * s.width + x)
  else
    0

let surface_set (s : surface) (x : int) (y : int) (c : int) : unit =
  if x >= 0 && x < s.width && y >= 0 && y < s.height then
    s.pixels.(y * s.width + x) <- c

let apply_rop (op : rop) (src : int) (dst : int) : int =
  match op with
  | ROP_COPY -> src
  | ROP_OR -> src lor dst
  | ROP_XOR -> src lxor dst
  | ROP_AND -> src land dst
  | ROP_INVERT -> lnot dst land 0xFFFFFFFF

(* btron_row_blit operational oracle:
   copies a span of pixels from src into dst at specified row offsets.
   Handles key transparency (0x00000000 = transparent key) *)
let btron_row_blit ~(dst : surface) ~(dst_x : int) ~(dst_y : int)
                   ~(src : surface) ~(src_x : int) ~(src_y : int)
                   ~(span : int) ~(rop : rop) : unit =
  if dst_y < 0 || dst_y >= dst.height || src_y < 0 || src_y >= src.height then ()
  else
    let valid_span = min span (min (dst.width - dst_x) (src.width - src_x)) in
    if valid_span <= 0 then ()
    else
      (* Fast-path check: scan for key transparency *)
      let src_offset = src_y * src.width + src_x in
      let dst_offset = dst_y * dst.width + dst_x in
      let has_key = ref false in
      for i = 0 to valid_span - 1 do
        if src.pixels.(src_offset + i) = 0 then has_key := true
      done;
      if not !has_key && rop = ROP_COPY then
        (* Opaque copy block (NEON/vector equivalent) *)
        Array.blit src.pixels src_offset dst.pixels dst_offset valid_span
      else
        (* Per-pixel composite *)
        for i = 0 to valid_span - 1 do
          let sp = src.pixels.(src_offset + i) in
          if sp <> 0 then (* skip transparent key *)
            let dp = dst.pixels.(dst_offset + i) in
            dst.pixels.(dst_offset + i) <- apply_rop rop sp dp
        done

(* 2D Rect Blit with clip region *)
let blit_rect ~(dst : surface) ~(src : surface)
              ~(src_rect : rect) ~(dst_x : int) ~(dst_y : int)
              ~(clip : rect) ~(rop : rop) : unit =
  let eff_clip = rect_intersect clip { left = 0; top = 0; right = dst.width; bottom = dst.height } in
  if not (rect_is_valid eff_clip) then ()
  else
    let w = rect_width src_rect in
    let h = rect_height src_rect in
    for row = 0 to h - 1 do
      let sy = src_rect.top + row in
      let dy = dst_y + row in
      if dy >= eff_clip.top && dy < eff_clip.bottom then
        let cx0 = max eff_clip.left dst_x in
        let cx1 = min eff_clip.right (dst_x + w) in
        if cx1 > cx0 then
          let span = cx1 - cx0 in
          let sx = src_rect.left + (cx0 - dst_x) in
          btron_row_blit ~dst ~dst_x:cx0 ~dst_y:dy ~src ~src_x:sx ~src_y:sy ~span ~rop
    done

(* =====================================================================
   §4  Compositor Layer Stacking & Presentation Backend
   ===================================================================== *)

type layer = {
  layer_id : int;
  bounds : rect;
  surf : surface;
  visible : bool;
}

type compositor_state = {
  backbuffer : surface;
  framebuffer : surface; (* Hardware scanout display buffer *)
  tracker : damage_tracker;
  mutable layers : layer list; (* Head is top-most window, tail is bottom-most *)
}

let create_compositor (w : int) (h : int) : compositor_state = {
  backbuffer = create_surface w h 0xFF008080; (* Classic B-TRON Teal *)
  framebuffer = create_surface w h 0xFF000000;
  tracker = create_damage_tracker w h;
  layers = [];
}

(* Composite visible layers over damage rect using Painter's Algorithm (bottom to top) *)
let composite_scene (comp : compositor_state) (damage : rect) : unit =
  let eff_damage = rect_intersect damage
                     { left = 0; top = 0; right = comp.backbuffer.width; bottom = comp.backbuffer.height } in
  if not (rect_is_valid eff_damage) then ()
  else
    (* 1. Restore background over damage area *)
    for y = eff_damage.top to eff_damage.bottom - 1 do
      for x = eff_damage.left to eff_damage.right - 1 do
        comp.backbuffer.pixels.(y * comp.backbuffer.width + x) <- 0xFF008080 (* Desktop Teal *)
      done
    done;
    (* 2. Walk window stack bottom-to-top (reverse of head-is-top list) *)
    let stack_bottom_to_top = List.rev comp.layers in
    List.iter (fun (l : layer) ->
      if l.visible then
        let overlap = rect_intersect l.bounds eff_damage in
        if rect_is_valid overlap then
          let src_rect = {
            left = 0;
            top = 0;
            right = l.surf.width;
            bottom = l.surf.height;
          } in
          blit_rect ~dst:comp.backbuffer ~src:l.surf
                    ~src_rect ~dst_x:l.bounds.left ~dst_y:l.bounds.top
                    ~clip:eff_damage ~rop:ROP_COPY
    ) stack_bottom_to_top

(* Present dirty damage rect from backbuffer to hardware framebuffer *)
let present_damage (comp : compositor_state) (damage : rect) : unit =
  let eff = rect_intersect damage
              { left = 0; top = 0; right = comp.backbuffer.width; bottom = comp.backbuffer.height } in
  if rect_is_valid eff then
    let span = rect_width eff in
    for y = eff.top to eff.bottom - 1 do
      btron_row_blit ~dst:comp.framebuffer ~dst_x:eff.left ~dst_y:y
                     ~src:comp.backbuffer ~src_x:eff.left ~src_y:y
                     ~span ~rop:ROP_COPY
    done

(* Flush complete pending damage cycle *)
let flush_compositor (comp : compositor_state) : rect option =
  match take_inval_damage comp.tracker with
  | None -> None
  | Some dmg ->
      composite_scene comp dmg;
      present_damage comp dmg;
      Some dmg

(* =====================================================================
   §5  Validation & Invariant Verification Suite
   ===================================================================== *)

let run_invalidation_and_geometry_tests () =
  printf "--- Running Rectangular Geometry & Invalidation Tests ---\n";
  let r1 = { left = 10; top = 20; right = 100; bottom = 150 } in
  let r2 = { left = 50; top = 80; right = 200; bottom = 120 } in
  let inter = rect_intersect r1 r2 in
  assert (inter.left = 50 && inter.top = 80 && inter.right = 100 && inter.bottom = 120);
  assert (rect_width inter = 50);
  assert (rect_height inter = 40);
  assert (rect_area inter = 2000);

  (* Union bounding box *)
  let union = rect_union_bbox r1 r2 in
  assert (union.left = 10 && union.top = 20 && union.right = 200 && union.bottom = 150);
  assert (rect_contains union r1);
  assert (rect_contains union r2);

  (* Invalidation tracking *)
  let dt = create_damage_tracker 1024 768 in
  assert (not (has_pending_damage dt));
  inval_damage_rect dt r1;
  assert (has_pending_damage dt);
  inval_damage_rect dt r2;
  match take_inval_damage dt with
  | None -> assert false
  | Some d ->
      assert (d = union);
      assert (not (has_pending_damage dt));
  printf "  [OK] Invalidation & Geometry Invariants Verified\n"

let run_bitblt_tests () =
  printf "--- Running Bit Blit Engine & Raster Operation Tests ---\n";
  let src = create_surface 10 10 0xFFFFFFFF in
  (* Punch transparent hole in src *)
  surface_set src 2 2 0x00000000;
  surface_set src 3 2 0x00000000;

  let dst = create_surface 20 20 0xFF112233 in
  let clip = { left = 0; top = 0; right = 20; bottom = 20 } in
  let src_rect = { left = 0; top = 0; right = 10; bottom = 10 } in
  blit_rect ~dst ~src ~src_rect ~dst_x:5 ~dst_y:5 ~clip ~rop:ROP_COPY;

  (* Check destination values *)
  assert (surface_get dst 5 5 = 0xFFFFFFFF); (* copied *)
  (* (5+2, 5+2) was key transparent in src -> unchanged destination pixel 0xFF112233 *)
  assert (surface_get dst (5 + 2) (5 + 2) = 0xFF112233);
  assert (surface_get dst 0 0 = 0xFF112233); (* Outside blit untouched *)
  assert (surface_get dst 19 19 = 0xFF112233);

  (* Test ROP XOR idempotence *)
  let s_xor1 = create_surface 4 4 0x00FF00FF in
  let d_xor = create_surface 4 4 0x55555555 in
  let clip4 = { left = 0; top = 0; right = 4; bottom = 4 } in
  let r4 = { left = 0; top = 0; right = 4; bottom = 4 } in
  blit_rect ~dst:d_xor ~src:s_xor1 ~src_rect:r4 ~dst_x:0 ~dst_y:0 ~clip:clip4 ~rop:ROP_XOR;
  let intermediate = surface_get d_xor 0 0 in
  assert (intermediate = (0x00FF00FF lxor 0x55555555));
  blit_rect ~dst:d_xor ~src:s_xor1 ~src_rect:r4 ~dst_x:0 ~dst_y:0 ~clip:clip4 ~rop:ROP_XOR;
  assert (surface_get d_xor 0 0 = 0x55555555); (* Reversible XOR involution *)
  printf "  [OK] Bit Blit & ROP Invariants Verified\n"

let run_compositor_presentation_tests () =
  printf "--- Running Compositor & Presenter Verification ---\n";
  let comp = create_compositor 640 480 in
  let wnd1_surf = create_surface 100 100 0xFFFF0000 in (* Red window *)
  let wnd2_surf = create_surface 100 100 0xFF0000FF in (* Blue window *)
  let l1 = { layer_id = 1; bounds = { left = 50; top = 50; right = 150; bottom = 150 }; surf = wnd1_surf; visible = true } in
  let l2 = { layer_id = 2; bounds = { left = 100; top = 100; right = 200; bottom = 200 }; surf = wnd2_surf; visible = true } in
  comp.layers <- [l2; l1]; (* l2 is top-most, l1 is below *)

  (* Invalidate damage covering both windows *)
  inval_damage_rect comp.tracker { left = 40; top = 40; right = 210; bottom = 210 };
  let flushed = flush_compositor comp in
  assert (flushed <> None);

  (* Verify Painter's Algorithm result in backbuffer & framebuffer *)
  (* (60, 60): only wnd1 -> Red *)
  assert (surface_get comp.backbuffer 60 60 = 0xFFFF0000);
  assert (surface_get comp.framebuffer 60 60 = 0xFFFF0000);

  (* (120, 120): overlap -> top-most wnd2 wins -> Blue *)
  assert (surface_get comp.backbuffer 120 120 = 0xFF0000FF);
  assert (surface_get comp.framebuffer 120 120 = 0xFF0000FF);

  (* (10, 10): desktop background -> Teal *)
  assert (surface_get comp.backbuffer 10 10 = 0xFF008080);
  (* Outside damage (10, 10) in framebuffer remained unpresented black *)
  assert (surface_get comp.framebuffer 10 10 = 0xFF000000);

  printf "  [OK] Compositor Stacking & Presentation Invariants Verified\n"

let () =
  printf "=======================================================\n";
  printf " B-System GUI Compositor & Invalidation Model Oracles  \n";
  printf "=======================================================\n";
  run_invalidation_and_geometry_tests ();
  run_bitblt_tests ();
  run_compositor_presentation_tests ();
  printf "\nAll UI Compositor model invariants passed successfully!\n"
