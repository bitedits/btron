(* opengl_model.ml
 *
 * Executable Operational Oracle for B-System OpenGL ES 1.1 / EGL Subsystem.
 * Covers:
 *  - EGL Surface Bridge & double buffering (WND / GDEV integration)
 *  - Uniform dispatch table & matrix stacks (Modelview 32, Projection 8)
 *  - Homogeneous coordinate transformations & Viewport mapping
 *  - Primitive assembly (GL_TRIANGLES, GL_TRIANGLE_STRIP)
 *  - Barycentric triangle rasterization & Z-buffer depth test
 *  - Color packing / unpacking (RGB565 & RGBA8888)
 *
 * Build & run:
 *   ocamlc -o opengl_model opengl_model.ml && ./opengl_model
 *)

open Printf

(* ═══════════════════════════════════════════════════════════════════════
   §1  Matrix & Vector Algebra
   ═══════════════════════════════════════════════════════════════════════ *)

type mat4 = float array (* 16 elements, row-major *)

let mat4_identity () : mat4 =
  [| 1.0; 0.0; 0.0; 0.0;
     0.0; 1.0; 0.0; 0.0;
     0.0; 0.0; 1.0; 0.0;
     0.0; 0.0; 0.0; 1.0 |]

let mat4_copy (m : mat4) : mat4 = Array.copy m

let mat4_mult (a : mat4) (b : mat4) : mat4 =
  let r = Array.make 16 0.0 in
  for i = 0 to 3 do
    for j = 0 to 3 do
      let sum = ref 0.0 in
      for k = 0 to 3 do
        sum := !sum +. a.(i * 4 + k) *. b.(k * 4 + j)
      done;
      r.(i * 4 + j) <- !sum
    done
  done;
  r

let mat4_translate (x : float) (y : float) (z : float) : mat4 =
  [| 1.0; 0.0; 0.0; x;
     0.0; 1.0; 0.0; y;
     0.0; 0.0; 1.0; z;
     0.0; 0.0; 0.0; 1.0 |]

let mat4_scale (sx : float) (sy : float) (sz : float) : mat4 =
  [|  sx; 0.0; 0.0; 0.0;
     0.0;  sy; 0.0; 0.0;
     0.0; 0.0;  sz; 0.0;
     0.0; 0.0; 0.0; 1.0 |]

type vec4 = { x : float; y : float; z : float; w : float }

let mat4_vec4_mult (m : mat4) (v : vec4) : vec4 =
  {
    x = m.(0) *. v.x +. m.(1) *. v.y +. m.(2) *. v.z +. m.(3) *. v.w;
    y = m.(4) *. v.x +. m.(5) *. v.y +. m.(6) *. v.z +. m.(7) *. v.w;
    z = m.(8) *. v.x +. m.(9) *. v.y +. m.(10) *. v.z +. m.(11) *. v.w;
    w = m.(12) *. v.x +. m.(13) *. v.y +. m.(14) *. v.z +. m.(15) *. v.w;
  }

(* ═══════════════════════════════════════════════════════════════════════
   §2  Matrix Stacks & Capacity Limits
   ═══════════════════════════════════════════════════════════════════════ *)

type matrix_stack = {
  name : string;
  max_depth : int;
  mutable stack : mat4 list;
}

let create_stack (name : string) (max_depth : int) : matrix_stack =
  { name; max_depth; stack = [mat4_identity ()] }

let stack_depth (s : matrix_stack) : int = List.length s.stack

let push_matrix (s : matrix_stack) : bool =
  if List.length s.stack >= s.max_depth then false
  else begin
    let top = List.hd s.stack in
    s.stack <- (mat4_copy top) :: s.stack;
    true
  end

let pop_matrix (s : matrix_stack) : bool =
  match s.stack with
  | _ :: (second :: rest) ->
      s.stack <- second :: rest;
      true
  | _ -> false (* Cannot pop base matrix *)

let current_matrix (s : matrix_stack) : mat4 = List.hd s.stack

(* ═══════════════════════════════════════════════════════════════════════
   §3  Viewport, Screen Coordinates & Bounds
   ═══════════════════════════════════════════════════════════════════════ *)

type viewport = {
  vp_x : int;
  vp_y : int;
  vp_w : int;
  vp_h : int;
}

type screen_pt = {
  sx : int;
  sy : int;
  sz : float;
}

let ndc_to_screen (vp : viewport) (ndc : vec4) : screen_pt option =
  if ndc.w <= 0.0 then None
  else
    let nx = ndc.x /. ndc.w in
    let ny = ndc.y /. ndc.w in
    let nz = ndc.z /. ndc.w in
    if nx < -1.0 || nx > 1.0 || ny < -1.0 || ny > 1.0 || nz < -1.0 || nz > 1.0 then None
    else
      let sx = vp.vp_x + int_of_float ((nx +. 1.0) *. 0.5 *. float_of_int vp.vp_w) in
      let sy = vp.vp_y + int_of_float ((ny +. 1.0) *. 0.5 *. float_of_int vp.vp_h) in
      let sz = (nz +. 1.0) *. 0.5 in
      Some { sx; sy; sz }

(* ═══════════════════════════════════════════════════════════════════════
   §4  Color Encoding & Packing Inversion
   ═══════════════════════════════════════════════════════════════════════ *)

type color_rgba = { r : int; g : int; b : int; a : int } (* 0..255 *)

let pack_rgb565 (c : color_rgba) : int =
  let r5 = (c.r lsr 3) land 0x1F in
  let g6 = (c.g lsr 2) land 0x3F in
  let b5 = (c.b lsr 3) land 0x1F in
  (r5 lsl 11) lor (g6 lsl 5) lor b5

let unpack_rgb565 (p : int) : color_rgba =
  let r5 = (p lsr 11) land 0x1F in
  let g6 = (p lsr 5) land 0x3F in
  let b5 = p land 0x1F in
  let r = (r5 lsl 3) lor (r5 lsr 2) in
  let g = (g6 lsl 2) lor (g6 lsr 4) in
  let b = (b5 lsl 3) lor (b5 lsr 2) in
  { r; g; b; a = 255 }

let pack_rgba8888 (c : color_rgba) : int32 =
  let r = Int32.of_int (c.r land 0xFF) in
  let g = Int32.of_int (c.g land 0xFF) in
  let b = Int32.of_int (c.b land 0xFF) in
  let a = Int32.of_int (c.a land 0xFF) in
  Int32.logor (Int32.shift_left a 24)
    (Int32.logor (Int32.shift_left b 16)
       (Int32.logor (Int32.shift_left g 8) r))

let unpack_rgba8888 (p : int32) : color_rgba =
  let r = Int32.to_int (Int32.logand p 0xFFl) in
  let g = Int32.to_int (Int32.logand (Int32.shift_right_logical p 8) 0xFFl) in
  let b = Int32.to_int (Int32.logand (Int32.shift_right_logical p 16) 0xFFl) in
  let a = Int32.to_int (Int32.logand (Int32.shift_right_logical p 24) 0xFFl) in
  { r; g; b; a }

(* ═══════════════════════════════════════════════════════════════════════
   §5  EGL Surface Bridge & Double Buffering
   ═══════════════════════════════════════════════════════════════════════ *)

type egl_surface = {
  wnd_id : int;
  width : int;
  height : int;
  mutable front_buffer : int array array; (* RGB565 / RGBA pixels *)
  mutable back_buffer  : int array array;
  mutable is_current   : bool;
}

let egl_create_window_surface (wnd_id : int) (width : int) (height : int) : egl_surface =
  let make_buf () = Array.make_matrix height width 0 in
  {
    wnd_id;
    width;
    height;
    front_buffer = make_buf ();
    back_buffer = make_buf ();
    is_current = false;
  }

let egl_make_current (surf : egl_surface) : unit =
  surf.is_current <- true

let egl_swap_buffers (surf : egl_surface) : unit =
  let tmp = surf.front_buffer in
  surf.front_buffer <- surf.back_buffer;
  surf.back_buffer <- tmp

(* ═══════════════════════════════════════════════════════════════════════
   §6  Primitive Assembly
   ═══════════════════════════════════════════════════════════════════════ *)

type vertex = {
  pos : vec4;
  color : color_rgba;
}

type triangle = {
  v0 : vertex;
  v1 : vertex;
  v2 : vertex;
}

let assemble_triangles (vertices : vertex list) : triangle list =
  let rec aux acc = function
    | v0 :: v1 :: v2 :: rest -> aux ({ v0; v1; v2 } :: acc) rest
    | _ -> List.rev acc
  in
  aux [] vertices

let assemble_triangle_strip (vertices : vertex list) : triangle list =
  let rec aux acc = function
    | v0 :: v1 :: v2 :: rest ->
        let tri = { v0; v1; v2 } in
        aux (tri :: acc) (v1 :: v2 :: rest)
    | _ -> List.rev acc
  in
  aux [] vertices

(* ═══════════════════════════════════════════════════════════════════════
   §7  Barycentric Coordinates & Z-Buffer Depth Test
   ═══════════════════════════════════════════════════════════════════════ *)

type depth_test_func = GL_LESS | GL_LEQUAL | GL_ALWAYS

type zbuffer = {
  z_width : int;
  z_height : int;
  z_data : float array array; (* 0.0 = near, 1.0 = far *)
}

let create_zbuffer (w : int) (h : int) : zbuffer =
  { z_width = w; z_height = h; z_data = Array.make_matrix h w 1.0 }

let clear_zbuffer (zbuf : zbuffer) (clear_val : float) : unit =
  for y = 0 to zbuf.z_height - 1 do
    for x = 0 to zbuf.z_width - 1 do
      zbuf.z_data.(y).(x) <- clear_val
    done
  done

let depth_pass (func : depth_test_func) (incoming_z : float) (stored_z : float) : bool =
  match func with
  | GL_LESS   -> incoming_z < stored_z
  | GL_LEQUAL -> incoming_z <= stored_z
  | GL_ALWAYS -> true

(* Edge function for 2D triangle *)
let edge_func (ax : float) (ay : float) (bx : float) (by : float) (px : float) (py : float) : float =
  (px -. ax) *. (by -. ay) -. (py -. ay) *. (bx -. ax)

(* Barycentric weights for point P in triangle (A, B, C) *)
let barycentric (ax : float) (ay : float) (bx : float) (by : float) (cx : float) (cy : float) (px : float) (py : float) =
  let area = edge_func ax ay bx by cx cy in
  if abs_float area < 1e-6 then None
  else
    let w0 = edge_func bx by cx cy px py /. area in
    let w1 = edge_func cx cy ax ay px py /. area in
    let w2 = edge_func ax ay bx by px py /. area in
    Some (w0, w1, w2)

(* Rasterize a single fragment with depth testing *)
let render_fragment (zbuf : zbuffer) (surf : egl_surface) (x : int) (y : int) (z : float) (c : int) (func : depth_test_func) : bool =
  if x < 0 || x >= zbuf.z_width || y < 0 || y >= zbuf.z_height then false
  else
    let current_z = zbuf.z_data.(y).(x) in
    if depth_pass func z current_z then begin
      zbuf.z_data.(y).(x) <- z;
      surf.back_buffer.(y).(x) <- c;
      true
    end else false

(* ═══════════════════════════════════════════════════════════════════════
   §8  Self-Testing Oracle & Invariant Suite
   ═══════════════════════════════════════════════════════════════════════ *)

let run_all_invariant_tests () : bool =
  printf "─── Running B-System OpenGL ES 1.1 / EGL Oracle Verification ───\n";
  let passed = ref true in

  (* Test 1: Matrix stack push/pop inversion & depth ceilings *)
  let s_modelview = create_stack "Modelview" 32 in
  let s_proj = create_stack "Projection" 8 in
  let t_trans = mat4_translate 10.0 20.0 30.0 in
  assert (push_matrix s_modelview);
  s_modelview.stack <- t_trans :: List.tl s_modelview.stack;
  assert (stack_depth s_modelview = 2);
  assert (pop_matrix s_modelview);
  assert (stack_depth s_modelview = 1);
  (* Check max depth ceiling of projection stack (8) *)
  for _ = 1 to 7 do assert (push_matrix s_proj) done;
  assert (stack_depth s_proj = 8);
  assert (not (push_matrix s_proj)); (* 9th push rejected *)
  printf "  [x] Test 1: Matrix stack push/pop inversion & capacity ceilings passed\n";

  (* Test 2: Viewport NDC bounds safety *)
  let vp = { vp_x = 0; vp_y = 0; vp_w = 640; vp_h = 480 } in
  let valid_ndc = { x = 0.0; y = 0.0; z = 0.0; w = 1.0 } in
  let pt = match ndc_to_screen vp valid_ndc with Some p -> p | None -> failwith "ndc error" in
  assert (pt.sx = 320 && pt.sy = 240);
  assert (pt.sx >= vp.vp_x && pt.sx < vp.vp_x + vp.vp_w);
  assert (pt.sy >= vp.vp_y && pt.sy < vp.vp_y + vp.vp_h);
  (* Out of bounds NDC rejection *)
  let out_ndc = { x = 2.5; y = 0.0; z = 0.0; w = 1.0 } in
  assert (ndc_to_screen vp out_ndc = None);
  printf "  [x] Test 2: Viewport screen mapping & NDC bounds preservation passed\n";

  (* Test 3: Primitive assembly conservation *)
  let dummy_v = { pos = { x=0.0; y=0.0; z=0.0; w=1.0 }; color = { r=255; g=255; b=255; a=255 } } in
  let v_list_7 = [dummy_v; dummy_v; dummy_v; dummy_v; dummy_v; dummy_v; dummy_v] in
  let tris = assemble_triangles v_list_7 in
  assert (List.length tris = 7 / 3); (* 2 triangles, 1 leftover dropped *)
  let strip_tris = assemble_triangle_strip v_list_7 in
  assert (List.length strip_tris = 7 - 2); (* 5 triangles *)
  printf "  [x] Test 3: Primitive assembly conservation (triangles & strips) passed\n";

  (* Test 4: Barycentric coordinates partition of unity *)
  let (ax, ay) = (0.0, 0.0) in
  let (bx, by) = (100.0, 0.0) in
  let (cx, cy) = (0.0, 100.0) in
  let (px, py) = (20.0, 30.0) in
  let (w0, w1, w2) = match barycentric ax ay bx by cx cy px py with Some w -> w | None -> failwith "bary error" in
  assert (w0 >= 0.0 && w1 >= 0.0 && w2 >= 0.0);
  assert (abs_float (w0 +. w1 +. w2 -. 1.0) < 1e-5);
  printf "  [x] Test 4: Barycentric weights partition of unity (sum = 1.0) passed\n";

  (* Test 5: Color packing round-trip *)
  let c_orig = { r = 240; g = 128; b = 64; a = 255 } in
  let packed565 = pack_rgb565 c_orig in
  let unpacked565 = unpack_rgb565 packed565 in
  assert (abs (c_orig.r - unpacked565.r) <= 8);
  assert (abs (c_orig.g - unpacked565.g) <= 4);
  assert (abs (c_orig.b - unpacked565.b) <= 8);
  let packed32 = pack_rgba8888 c_orig in
  let unpacked32 = unpack_rgba8888 packed32 in
  assert (c_orig = unpacked32);
  printf "  [x] Test 5: Color packing & unpacking bijection (RGB565/RGBA8888) passed\n";

  (* Test 6: Z-Buffer depth monotonicity & occlusion soundness *)
  let zbuf = create_zbuffer 100 100 in
  let surf = egl_create_window_surface 1 100 100 in
  (* Draw near fragment at z = 0.3 *)
  assert (render_fragment zbuf surf 50 50 0.3 0xFF00 GL_LESS);
  assert (zbuf.z_data.(50).(50) = 0.3);
  assert (surf.back_buffer.(50).(50) = 0xFF00);
  (* Attempt to draw farther fragment at z = 0.7 -> must be occluded/rejected *)
  assert (not (render_fragment zbuf surf 50 50 0.7 0x00FF GL_LESS));
  assert (zbuf.z_data.(50).(50) = 0.3); (* Unchanged *)
  assert (surf.back_buffer.(50).(50) = 0xFF00); (* Unchanged *)
  printf "  [x] Test 6: Z-Buffer depth monotonicity & occlusion culling passed\n";

  (* Test 7: Z-Buffer idempotence *)
  assert (not (render_fragment zbuf surf 50 50 0.3 0xFF00 GL_LESS));
  assert (zbuf.z_data.(50).(50) = 0.3);
  printf "  [x] Test 7: Z-Buffer redraw idempotence under GL_LESS passed\n";

  (* Test 8: EGL swap buffers dimension preservation & involution *)
  let orig_front = surf.front_buffer in
  let orig_back = surf.back_buffer in
  egl_swap_buffers surf;
  assert (surf.front_buffer == orig_back);
  assert (surf.back_buffer == orig_front);
  assert (Array.length surf.front_buffer = 100 && Array.length surf.front_buffer.(0) = 100);
  egl_swap_buffers surf;
  assert (surf.front_buffer == orig_front);
  assert (surf.back_buffer == orig_back);
  printf "  [x] Test 8: EGL swap buffers involution & geometry conservation passed\n";

  if !passed then begin
    printf "PASS: OpenGL ES Model (all 8 invariant tests passed)\n";
    true
  end else begin
    printf "FAIL: OpenGL ES Model invariant violations\n";
    false
  end

let () =
  if not (run_all_invariant_tests ()) then exit 1
