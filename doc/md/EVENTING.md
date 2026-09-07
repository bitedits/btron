# BTRON 3.20 Eventing Architecture & Specification

## 1. Overview & Architectural Principles

The B-System (BTRON 3.20) Graphical Workbench uses a layered, cleanroom event distribution architecture modeled after the Sakamura BTRON HMI specifications:

1. **Hardware Driver Layer**: Interrupt service routines and polling drivers for ADB (Apple Desktop Bus on Macintosh Quadra 800), PS/2, and Serial consoles translate hardware signals into standardized BTRON `EVT` packets.
2. **System Event Queue**: Events are enqueued via `snd_evt()` and retrieved via `get_evt()`.
3. **Workbench Event Coordinator (`src/desktop/workbench.c`)**: Serves as the top-level dispatcher for desktop shell components without hardcoded mocks, coordinating the Global Menu Bar, Tracker Start Menu, Window Manager, and Desktop Virtual Object Icons.
4. **Window Manager (`src/window/wnd.c`)**: Manages window-level interaction primitives according to BTRON specifications (hit testing, window z-order focus, corner resizing, titlebar dragging, compact sliding tab adjustment, close button, and client area dispatch).
5. **Application Layer**: Each window's `event_handler` receives client-area events, managing application-specific widgets, text editing, and menu bars.

This architecture ensures:

- **Zero code duplication** across baremetal targets (Motorola 68040 Quadra 800, UEFI x86_64, ARM64 Raspberry Pi) and hosted POSIX/SDL2.
- **Clean separation of concerns**: `src/desktop/desktop.c` remains a pure compositor and background/icon renderer without event state entanglement.

## 2. Event Distribution Flow

```
                     ┌──────────────────────────────────────────────┐
                     │ Hardware Drivers (ADB, PS/2, Serial Console) │
                     └──────────────────────┬───────────────────────┘
                                            │ snd_evt()
                                            ▼
                              ┌───────────────────────────┐
                              │  BTRON System Event Queue │
                              └─────────────┬─────────────┘
                                            │ get_evt()
                                            ▼
                     ┌──────────────────────────────────────────────┐
                     │          workbench_process_event()           │
                     │          (src/desktop/workbench.c)           │
                     └──────┬───────────────┬──────────────┬────────┘
                            │               │              │
               ┌────────────▼───────────┐   │   ┌──────────▼───────────┐
               │   Global System Menu   │   │   │  Tracker Start Menu  │
               │   & Deskbar Dropdowns  │   │   │  (tracker.c)         │
               │   (global_menu.c)      │   │   └──────────────────────┘
               └────────────────────────┘   │
                                            ▼
                            ┌───────────────────────────────┐
                            │     wnd_mgr_handle_event()    │
                            │     (src/window/wnd.c)        │
                            │  • Hit testing & Focus        │
                            │  • Corner Resize (16x16 grip) │
                            │  • Titlebar / Tab Dragging    │
                            │  • Sliding Tab Offset         │
                            │  • Close Button [X]           │
                            │  • Client event_handler()     │
                            └───────────────┬───────────────┘
                                            │ (if unhandled / clicked background)
                                            ▼
                            ┌───────────────────────────────┐
                            │     desktop_handle_click()    │
                            │  Desktop Virtual Object Icons │
                            │  (Cabinet, Editor, CLI, Audio)│
                            └───────────────────────────────┘
```

## 3. Window Manager Event Interaction (`src/window/wnd.c`)

The Window Manager encapsulates the following standard BTRON interaction primitives:

### 3.1 Window Hit Testing & Focus Elevation

- `find_wnd_at(x, y)` evaluates windows from top of the z-stack to bottom.

- If a background window is clicked:
  - Any pending TIP / IME composition is cancelled.
  - The clicked window is moved to the top of the stack (`top_wnd`).

### 3.2 Bottom-Right Corner Resize Grip (`WND_ATTR_RESIZE`)

- BTRON3 windows with `WND_ATTR_RESIZE` feature a 16x16 diagonal hatch handle in the lower-right corner (`bounds.right - 16` to `bounds.right`, `bounds.bottom - 16` to `bounds.bottom`).

- Clicking inside this grip initiates window resizing:
  - Tracks origin dimensions and initial mouse coordinates.
  - On `EV_MOUSE_MOVE`, calculates the delta:
    $$\Delta X = x - x_0, \quad \Delta Y = y - y_0$$
    $$W_{\text{new}} = \max(160, W_{\text{orig}} + \Delta X), \quad H_{\text{new}} = \max(100, H_{\text{orig}} + \Delta Y)$$
  - Invokes `rsz_wnd(wnd, W_{\text{new}}, H_{\text{new}})`, which updates window boundaries, re-clamps tab geometry, and resizes the backing `GDEV`.

### 3.3 Window Titlebar & Compact Sliding Tab (`WND_ATTR_SLIDING_TAB`)

- **Close Button**: Tested via `whit_test_close_btn()`. Clicking calls `cls_wnd()`.
- **Sliding Tab Grip**: Tested via left edge of the compact tab (`tab_r.left` to `tab_r.left + 14`). Dragging adjusts `tab_offset_x` across the top rail via `wset_tab_offset()`.
- **Window Dragging**: Clicking inside the title tab or top rail begins window dragging via `mov_wnd()`.

### 3.4 Client Area Event Routing

- If none of the window decorations intercepted the click, the event is translated into the client area and dispatched directly to `wnd->event_handler(wnd, ev)`.

## 4. Workbench Event Coordinator (`src/desktop/workbench.c`)

The coordinator processes each queued event in the following priority order:

1. **Global System Menu Bar**:
   - `global_menu_handle_mouse_down(x, y)`: Handles the Deskbar start button, `システム(S)`, `実身・仮身(O)`, `ウィンドウ(W)`, `道具・文字(T)`, and the Mozc/TIP input mode indicator badge.
   - If a menu dropdown is currently open and a click occurs outside, the dropdown closes.

2. **Tracker Start Menu**:
   - `tracker_handle_mouse_down(x, y)`: Handles clicks in the low-latency Haiku-style root application tracker menu.

3. **Window Manager**:
   - `wnd_mgr_handle_event(ev)`: Executes window hit testing, focus, dragging, resizing, and client dispatch.

4. **Desktop Virtual Object Icons**:
   - If no window intercepted the click, `desktop_handle_click(x, y)` tests the authentic 32x32 / 64x64 desktop pictograms:
     - 実身・仮身 (Cabinet Explorer) -> `open_vobj_manager_window()`
     - 基本エディタ (Text Editor) -> `open_t_editor_window()`
     - 端末シェル (GTerm Shell) -> `open_gterm_window()`
     - 音響機器 (Audio Player) -> `open_audio_player_window()`
     - 会話通信 (Chat Client) -> `launch_beos_chat()`

5. **Mouse Motion & Release Tracking**:
   - Updates `set_baremetal_mouse_pos(x, y)` for software cursor rendering.
   - Dispatches hover states to open menu bars and active window drag/resize handles.
   - On `EV_BUT_UP`, safely terminates all active dragging and resizing operations.

6. **Keystroke Routing**:
   - Dispatches global accelerator keys (e.g. menu shortcuts) before forwarding `EV_KEY_DOWN` to the focused window's `event_handler`.

## 5. Memory Management During Interactive Resizing

To support smooth, live window resizing on baremetal targets without running out of memory:

- `cls_dev()` in `src/graphics/dp_core.c` checks `dev->is_vram`. Off-screen backbuffers allocated for windows are properly deallocated via `free(dev->pixels); free(dev);`, while VRAM buffers pointing to the hardware framebuffer remain protected.
- `src/kernel/libstr.c` implements a first-fit coalescing heap allocator with `HeapBlock` headers. When `rsz_wnd()` repeatedly deallocates and reallocates backing graphics buffers during continuous mouse drag motion, adjacent free blocks coalesce immediately, preventing heap fragmentation and exhaustion.

# Credits

* Namdak Tonpa and Grok 4.5
