(* ui_elements_model.ml
 *
 * Executable Operational Oracle for B-System Window Manager & UI Elements Subsystem.
 * Reflects canonical BTRON C99 APIs:
 *  - Window Structures & Bounds (WND, RECT, PNT, wnd_attr)
 *  - Window Lifecycle & Stacking (opn_wnd, cls_wnd, top_wnd, mov_wnd, rsz_wnd)
 *  - Retro Tab Geometry & Hit Testing (wget_tab_rect, whit_test_tab, whit_test_close_btn)
 *  - Desktop Layout Management (wnd_cascade_all, wnd_tile_all, wnd_cycle_focus)
 *  - Menu Bar & Dropdown Modal Grab (global_menu.h, app_menu.h)
 *  - Event Queue & TIP Input Dispatch (event.h: EVT, EV_TYPE, EVENT_QUEUE_SIZE 256)
 *
 * Build & run:
 *   ocamlc -o ui_elements_model ui_elements_model.ml && ./ui_elements_model
 *)

open Printf

(* =====================================================================
   §1  Geometry & Types (dp.h / wnd.h / event.h)
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

let point_in_rect (x : int) (y : int) (r : rect) : bool =
  r.left <= x && x < r.right && r.top <= y && y < r.bottom

let rect_width (r : rect) : int =
  if r.right > r.left then r.right - r.left else 0

let rect_height (r : rect) : int =
  if r.bottom > r.top then r.bottom - r.top else 0

(* BTRON Window Attributes (wnd.h) *)
let wnd_attr_title       = 1 lsl 0
let wnd_attr_close       = 1 lsl 1
let wnd_attr_max         = 1 lsl 2
let wnd_attr_resize      = 1 lsl 3
let wnd_attr_border      = 1 lsl 4
let wnd_attr_compact_tab = 1 lsl 5
let wnd_attr_sliding_tab = 1 lsl 6

let wnd_title_height = 28

type wnd = {
  id : int;
  title : string;
  mutable bounds : rect;
  attr : int;
  mutable visible : bool;
  mutable focused : bool;
  mutable tab_offset_x : int;
  mutable tab_width : int;
  mutable img_valid : bool;
}

type wm_state = {
  mutable windows : wnd list; (* Head is top-most, tail is bottom-most *)
  mutable next_id : int;
  screen_w : int;
  screen_h : int;
}

let create_wm (w : int) (h : int) : wm_state = {
  windows = [];
  next_id = 1;
  screen_w = w;
  screen_h = h;
}

(* =====================================================================
   §2  Window Geometry: Tab & Controls (wget_tab_rect, whit_test)
   ===================================================================== *)

let wget_tab_rect (wnd : wnd) : rect =
  if (wnd.attr land wnd_attr_title) = 0 then
    rect_empty
  else
    let w = rect_width wnd.bounds in
    let tw =
      if wnd.tab_width <= 0 || wnd.tab_width > w || (wnd.attr land wnd_attr_compact_tab) = 0 then
        w
      else
        wnd.tab_width
    in
    let off_x =
      if wnd.tab_offset_x < 0 then 0
      else if wnd.tab_offset_x + tw > w then
        max 0 (w - tw)
      else
        wnd.tab_offset_x
    in
    {
      left = wnd.bounds.left + off_x;
      top = wnd.bounds.top;
      right = wnd.bounds.left + off_x + tw;
      bottom = wnd.bounds.top + wnd_title_height;
    }

let whit_test_tab (wnd : wnd) (x : int) (y : int) : bool =
  let tab_r = wget_tab_rect wnd in
  point_in_rect x y tab_r

let whit_test_close_btn (wnd : wnd) (x : int) (y : int) : bool =
  if (wnd.attr land wnd_attr_close) = 0 then false
  else
    let tab_r = wget_tab_rect wnd in
    let btn_r = {
      left = tab_r.right - 24;
      top = tab_r.top + 6;
      right = tab_r.right - 6;
      bottom = tab_r.top + 22;
    } in
    point_in_rect x y btn_r

(* =====================================================================
   §3  Window Management Primitives (opn_wnd, cls_wnd, top_wnd, mov, rsz)
   ===================================================================== *)

let opn_wnd (wm : wm_state) (title : string) (x : int) (y : int) (w : int) (h : int) (attr : int) : wnd =
  let id = wm.next_id in
  wm.next_id <- wm.next_id + 1;
  (* Unfocus current windows *)
  List.iter (fun (w : wnd) -> w.focused <- false) wm.windows;
  let new_wnd = {
    id = id;
    title = title;
    bounds = { left = x; top = y; right = x + max 32 w; bottom = y + max 32 h };
    attr = attr;
    visible = true;
    focused = true;
    tab_offset_x = 0;
    tab_width = 120;
    img_valid = false;
  } in
  wm.windows <- new_wnd :: wm.windows;
  new_wnd

let top_wnd (wm : wm_state) (target : wnd) : unit =
  if not target.visible then ()
  else
    let rest = List.filter (fun (w : wnd) -> w.id <> target.id) wm.windows in
    List.iter (fun (w : wnd) -> w.focused <- false) rest;
    target.focused <- true;
    wm.windows <- target :: rest

let cls_wnd (wm : wm_state) (target : wnd) : unit =
  wm.windows <- List.filter (fun (w : wnd) -> w.id <> target.id) wm.windows;
  (* Refocus new top visible window if target was focused *)
  if target.focused then
    match List.find_opt (fun (w : wnd) -> w.visible) wm.windows with
    | Some top_vis -> top_vis.focused <- true
    | None -> ()

let mov_wnd (wnd : wnd) (x : int) (y : int) : unit =
  let w = rect_width wnd.bounds in
  let h = rect_height wnd.bounds in
  wnd.bounds <- { left = x; top = y; right = x + w; bottom = y + h }

let rsz_wnd (wnd : wnd) (w : int) (h : int) : unit =
  let nw = max 32 w in
  let nh = max 32 h in
  wnd.bounds <- {
    left = wnd.bounds.left;
    top = wnd.bounds.top;
    right = wnd.bounds.left + nw;
    bottom = wnd.bounds.top + nh;
  };
  wnd.img_valid <- false

let find_wnd_at (wm : wm_state) (x : int) (y : int) : wnd option =
  List.find_opt (fun (w : wnd) -> w.visible && point_in_rect x y w.bounds) wm.windows

let get_top_wnd (wm : wm_state) : wnd option =
  match wm.windows with
  | [] -> None
  | h :: _ -> Some h

(* Layout algorithms *)
let wnd_cascade_all (wm : wm_state) : unit =
  let step = 32 in
  let idx = ref 0 in
  List.iter (fun (w : wnd) ->
    if w.visible then (
      let x = 40 + (!idx * step) in
      let y = 40 + (!idx * step) in
      mov_wnd w x y;
      idx := !idx + 1
    )
  ) (List.rev wm.windows)

let wnd_tile_all (wm : wm_state) : unit =
  let vis = List.filter (fun (w : wnd) -> w.visible) wm.windows in
  let count = List.length vis in
  if count = 0 then ()
  else
    let w_per = wm.screen_w / count in
    let idx = ref 0 in
    List.iter (fun (w : wnd) ->
      let x = !idx * w_per in
      w.bounds <- { left = x; top = 30; right = x + w_per; bottom = wm.screen_h - 10 };
      w.img_valid <- false;
      idx := !idx + 1
    ) vis

(* =====================================================================
   §4  Menu Subsystem & Modal Grab (global_menu.h / app_menu.h)
   ===================================================================== *)

type menu_item = {
  item_id : int;
  label : string;
  enabled : bool;
}

type menu_dropdown = {
  menu_id : int;
  title : string;
  bar_rect : rect;
  drop_rect : rect;
  items : menu_item list;
}

type menu_bar_state = {
  menus : menu_dropdown list;
  mutable active_menu_id : int option;
  mutable hovered_item : int option;
}

let create_menu_bar (menus : menu_dropdown list) : menu_bar_state = {
  menus = menus;
  active_menu_id = None;
  hovered_item = None;
}

let mnu_open (mb : menu_bar_state) (menu_id : int) : rect option =
  match List.find_opt (fun m -> m.menu_id = menu_id) mb.menus with
  | Some m ->
      mb.active_menu_id <- Some menu_id;
      Some m.drop_rect
  | None -> None

let mnu_close (mb : menu_bar_state) : rect option =
  match mb.active_menu_id with
  | Some id ->
      mb.active_menu_id <- None;
      mb.hovered_item <- None;
      (match List.find_opt (fun m -> m.menu_id = id) mb.menus with
       | Some m -> Some m.drop_rect
       | None -> None)
  | None -> None

(* Returns (handled, opt_damage_to_inval, opt_selected_item) *)
let mnu_handle_mouse (mb : menu_bar_state) (x : int) (y : int) (clicked : bool) : bool * rect option * int option =
  match mb.active_menu_id with
  | None ->
      (* Check if mouse clicks on any menu header in top bar *)
      if clicked then
        match List.find_opt (fun m -> point_in_rect x y m.bar_rect) mb.menus with
        | Some m ->
            mb.active_menu_id <- Some m.menu_id;
            (true, Some m.drop_rect, None)
        | None -> (false, None, None)
      else (false, None, None)
  | Some id ->
      let active_menu = List.find (fun m -> m.menu_id = id) mb.menus in
      if point_in_rect x y active_menu.drop_rect then
        let item_h = 24 in
        let idx = (y - active_menu.drop_rect.top) / item_h in
        if idx >= 0 && idx < List.length active_menu.items then
          let item = List.nth active_menu.items idx in
          if clicked && item.enabled then (
            let dmg = mnu_close mb in
            (true, dmg, Some item.item_id)
          ) else (
            mb.hovered_item <- Some item.item_id;
            (true, None, None)
          )
        else (true, None, None)
      else if clicked then
        (* Click outside active menu dropdown -> dismiss menu modal grab *)
        let dmg = mnu_close mb in
        (true, dmg, None)
      else
        (true, None, None)

(* =====================================================================
   §5  Event Queue & Input Dispatcher (event.h / TIP subsystem)
   ===================================================================== *)

type ev_type =
  | EV_NONE
  | EV_BUT_DOWN
  | EV_BUT_UP
  | EV_MOUSE_MOVE
  | EV_KEY_DOWN
  | EV_KEY_UP
  | EV_WND_CLOSE
  | EV_WND_FOCUS
  | EV_MENU_SELECT

type evt = {
  type_ : ev_type;
  wndid : int;
  pos_x : int;
  pos_y : int;
  key : int;
  button : int;
}

let empty_event = { type_ = EV_NONE; wndid = 0; pos_x = 0; pos_y = 0; key = 0; button = 0 }

let event_queue_size = 256

type event_queue = {
  buffer : evt array;
  mutable head : int;
  mutable tail : int;
  mutable count : int;
}

let create_event_queue () : event_queue = {
  buffer = Array.make event_queue_size empty_event;
  head = 0;
  tail = 0;
  count = 0;
}

let enqueue_event (eq : event_queue) (ev : evt) : bool =
  if eq.count >= event_queue_size then false
  else (
    eq.buffer.(eq.tail) <- ev;
    eq.tail <- (eq.tail + 1) mod event_queue_size;
    eq.count <- eq.count + 1;
    true
  )

let dequeue_event (eq : event_queue) : evt option =
  if eq.count = 0 then None
  else (
    let ev = eq.buffer.(eq.head) in
    eq.head <- (eq.head + 1) mod event_queue_size;
    eq.count <- eq.count - 1;
    Some ev
  )

(* Unified Window Manager Dispatcher *)
let wnd_mgr_handle_event (wm : wm_state) (mb : menu_bar_state) (ev : evt) : evt option =
  match ev.type_ with
  | EV_BUT_DOWN ->
      let (handled, _dmg, sel) = mnu_handle_mouse mb ev.pos_x ev.pos_y true in
      if handled then
        match sel with
        | Some item_id -> Some { ev with type_ = EV_MENU_SELECT; key = item_id }
        | None -> None
      else
        (* Hit test window stack *)
        (match find_wnd_at wm ev.pos_x ev.pos_y with
         | Some target ->
             if whit_test_close_btn target ev.pos_x ev.pos_y then (
               cls_wnd wm target;
               Some { ev with type_ = EV_WND_CLOSE; wndid = target.id }
             ) else (
               top_wnd wm target;
               Some { ev with wndid = target.id }
             )
         | None -> None)
  | _ -> Some ev

(* =====================================================================
   §6  Validation & Invariant Verification Suite
   ===================================================================== *)

let run_window_stack_and_focus_tests () =
  printf "--- Running Window Manager Stacking & Focus Invariant Tests ---\n";
  let wm = create_wm 1024 768 in
  let w1 = opn_wnd wm "Document 1" 50 50 300 200 (wnd_attr_title lor wnd_attr_close lor wnd_attr_compact_tab) in
  assert (w1.focused = true);
  assert (List.length wm.windows = 1);

  let w2 = opn_wnd wm "Spreadsheet" 100 100 400 300 (wnd_attr_title lor wnd_attr_close) in
  assert (w2.focused = true);
  assert (w1.focused = false); (* Focus uniqueness *)
  assert (get_top_wnd wm = Some w2);

  (* Bring w1 to top *)
  top_wnd wm w1;
  assert (w1.focused = true);
  assert (w2.focused = false);
  assert (get_top_wnd wm = Some w1);

  (* Close top window w1 -> focus returns to w2 *)
  cls_wnd wm w1;
  assert (w2.focused = true);
  assert (get_top_wnd wm = Some w2);

  printf "  [OK] Stacking & Focus Uniqueness Invariants Verified\n"

let run_tab_and_control_tests () =
  printf "--- Running Tab Geometry & Close Button Hit Testing Tests ---\n";
  let wm = create_wm 1024 768 in
  let w = opn_wnd wm "Editor" 100 100 400 300 (wnd_attr_title lor wnd_attr_close lor wnd_attr_compact_tab) in
  w.tab_width <- 150;
  w.tab_offset_x <- 20;

  let tab_r = wget_tab_rect w in
  assert (tab_r.left = 100 + 20);
  assert (tab_r.right = 100 + 20 + 150);
  assert (tab_r.top = 100);
  assert (tab_r.bottom = 100 + wnd_title_height);

  (* Hit test inside tab *)
  assert (whit_test_tab w (120 + 10) (100 + 10) = true);
  (* Hit test outside tab *)
  assert (whit_test_tab w (100 + 5) (100 + 10) = false);

  (* Close button hit test *)
  let btn_center_x = tab_r.right - 15 in
  let btn_center_y = tab_r.top + 14 in
  assert (whit_test_close_btn w btn_center_x btn_center_y = true);
  assert (whit_test_close_btn w (tab_r.left + 10) btn_center_y = false);

  printf "  [OK] Tab Geometry & Control Hit Testing Verified\n"

let run_menu_and_event_queue_tests () =
  printf "--- Running Menu Subsystem & Event Queue Invariant Tests ---\n";
  let items = [
    { item_id = 101; label = "New"; enabled = true };
    { item_id = 102; label = "Open"; enabled = true };
    { item_id = 103; label = "Save"; enabled = false };
  ] in
  let m1 = {
    menu_id = 1;
    title = "File";
    bar_rect = { left = 10; top = 0; right = 60; bottom = 24 };
    drop_rect = { left = 10; top = 24; right = 110; bottom = 24 + 3 * 24 };
    items = items;
  } in
  let mb = create_menu_bar [m1] in

  (* Open menu *)
  let dmg = mnu_open mb 1 in
  assert (dmg = Some m1.drop_rect);
  assert (mb.active_menu_id = Some 1);

  (* Mouse click inside menu on "Open" item (idx = 1) *)
  let (handled, close_dmg, sel) = mnu_handle_mouse mb 30 (24 + 24 + 10) true in
  assert (handled = true);
  assert (sel = Some 102);
  assert (mb.active_menu_id = None);
  assert (close_dmg = Some m1.drop_rect);

  (* Event Queue FIFO *)
  let eq = create_event_queue () in
  let ev1 = { empty_event with type_ = EV_BUT_DOWN; pos_x = 50; pos_y = 50 } in
  let ev2 = { empty_event with type_ = EV_KEY_DOWN; key = 65 } in
  assert (enqueue_event eq ev1);
  assert (enqueue_event eq ev2);
  assert (eq.count = 2);

  (match dequeue_event eq with
   | Some de1 -> assert (de1.type_ = EV_BUT_DOWN)
   | None -> assert false);
  (match dequeue_event eq with
   | Some de2 -> assert (de2.type_ = EV_KEY_DOWN)
   | None -> assert false);
  assert (dequeue_event eq = None);

  printf "  [OK] Menu Modal Grab & Event Queue FIFO Verified\n"

let () =
  printf "=======================================================\n";
  printf " B-System Window Manager & UI Elements Model Oracles   \n";
  printf "=======================================================\n";
  run_window_stack_and_focus_tests ();
  run_tab_and_control_tests ();
  run_menu_and_event_queue_tests ();
  printf "\nAll UI Elements model invariants passed successfully!\n"
