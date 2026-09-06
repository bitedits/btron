# B-System (BTRON 3.20) GUI Application Scalability & Architecture Review

**Universal Multi-Target GUI: Desktop Workstations & Mobile Handsets**

This document establishes the architecture for running real B-System desktop applications (`src/apps/`) across both classic multi-window desktop workstations and vertical screen mobile devices (e.g. 480x640 portrait NTT DoCoMo FOMA) **without conditional compilation (`#ifdef`)** and **without bespoke custom render functions**.

## 1. Architectural Principles: The Universal Viewport Contract

Desktop B-System and Mobile FOMA previously diverged in their windowing abstractions:

| Feature | Desktop Workstation | Mobile Handset (FOMA) |
|:---|:---|:---|
| **Form Factor** | Multi-window desktop (1024x768 / 800x600) | Single focused viewport (480x640 vertical portrait) |
| **Window Container** | Floating `WND` with titlebar, compact tab, resize grip | Persistent screen stack (`FOMA_SCREEN`), status + softkey bars |
| **Canvas Backing Store** | `wnd->dev` (`GDEV*`) sized to client rectangle | Framebuffer viewport (`y = 68..602`, `w = 480`, `h = 534`) |
| **Render Hook** | `wnd->paint(wnd, dev)` querying `dev->width` and `dev->height` | Bespoke `foma_render_list()` or `custom_render_hook()` |
| **Event Dispatch** | `wnd->event_handler(wnd, ev)` handling `EVT` | Keypad / softkey / D-pad navigator in `workbench_mobile.c` |

### The Universal Viewport Solution

Rather than maintaining separate UI implementations per form factor, **core applications in `src/apps/` are inherently viewport-driven**:

1. Their `paint(wnd, dev)` methods compute line wrapping, gutter layouts, row counts, and canvas bounds dynamically from `dev->width` and `dev->height`.

2. When hosted in mobile mode, the mobile coordinator simply creates a borderless `WND` (or viewport adapter) matching the content viewport (`480x534`), and delegates painting and event handling directly to the application's standard callbacks.

3. Core applications require **zero conditional compilation (`#ifdef`)**.

```
┌────────────────────────────────────────────────────────┐
│               FOMA Viewport Frame (480x640)            │
│  ┌──────────────────────────────────────────────────┐  │
│  │ 1. FOMA Status Bar (y = 0..32)                   │  │
│  ├──────────────────────────────────────────────────┤  │
│  │ 2. FOMA Title Bar: "gterm — 端末シェル" (32..68) │  │
│  ├──────────────────────────────────────────────────┤  │
│  │                                                  │  │
│  │ 3. Universal App Client Canvas (y = 68..602)     │  │
│  │    Width: 480px, Height: 534px                   │  │
│  │    Direct invocation:                            │  │
│  │      app_paint(wnd, sub_dev)                     │  │
│  │      app_event_handler(wnd, evt)                 │  │
│  │                                                  │  │
│  ├──────────────────────────────────────────────────┤  │
│  │ 4. FOMA Soft Key Bar: [操作] [実行] [戻る]       │  │
│  └──────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────┘
```

## 2. Candidate Evaluation Matrix

| Candidate App | Viewport Scalability | Layout & Menu Constraints | Input Compatibility | Readiness Tier |
|:---|:---|:---|:---|:---|
| **1. `gterm`** (Terminal)<br>`src/apps/gterm.c` | **100% Dynamic**<br>`max_disp_rows = (dev->height - canvas_top - row_h - 4) / row_h`<br>Dynamic y-offset for prompt/history. | In-window menu bar (`ファイル`, `編集`, `表示`, `端末`, `ヘルプ`) occupies ~380px, fitting within 480px width. | D-pad Up/Down maps to history, Return to command execution, Backspace to delete. Direct ASCII + TIP IME. | **Tier 1 (Selected for Integration)**<br>Zero `#ifdef`. Provides full live interactive shell on mobile. |
| **2. `t_editor`** (TAD Editor)<br>`src/apps/t_editor.c` | **100% Dynamic**<br>`view_rows = (dev->height - 60) / 18`<br>Horizontal line clipping at `dev->width - 16`. | Standard menu bar with right-aligned filename. Line number gutter automatically adjusts (`x = 36`). | Cursor key navigation, typing, backspace, selection. | **Tier 1 (Selected for Integration)**<br>Zero `#ifdef`. Real text and TAD body editor running directly on mobile. |
| **3. `tad_browser`** (Browser)<br>`src/apps/tad_browser.c` | **Adaptive Reflow**<br>`tad_browser_layout(tb, view_w)` dynamically recalculates word wrapping for any `view_w`. | Viewport clipped to `(0, 48, dev->width, dev->height - 22)`. | D-pad scrolling (`scroll_y`). Wide fixed tables or large images need panning. | **Tier 2 (Planned)**<br>Requires scrollbar and figure panning adaptation. |
| **4. `vobj_manager`** (Cabinet)<br>`src/apps/vobj_manager.c` | **Dynamic List View**<br>`visible_rows = (dev->height - 48) / 22`<br>Row width `dev->width - 4`. | Menu bar fits; toggle button at `x = 440` is guarded by `if (dev->width >= 560)`. | D-pad moves item selection, Return opens Real Body. | **Tier 2 (Planned)**<br>Overlaps with native Cho-Kanji mobile cabinet list. |
| **5. `chat`** (B-Chat)<br>`src/apps/chat.c` | **Card & Box Layout**<br>Roster box `(4, 72, dev->width - 4, dev->height - 34)`. | Menu bar occupies 220px width; status card dynamically sizes. | Roster list navigation, enter room. | **Tier 2 (Planned)**<br>Fits 480px width cleanly. |

## 3. Tier 1 Application Integration: `gterm` & `t_editor`

### 3.1 `gterm` (Live BTRON Shell)

- **Engine**: Full interactive shell with built-in commands (`ver`, `tk_get_tid`, `help`, `date`, `ls`, `cat`, `echo`, `clear`, etc.).
- **Viewport**: Rendered inside `480x534` content area (28 lines x 58 columns).
- **Controls**:
  - Keypad typing and ASCII input.
  - Return / Center key: Executes command.
  - D-pad Up / Down: Navigates command history.
  - Backspace / Clear key: Deletes characters.
  - Left Soft Key (`[操作]`): Menu / actions.
  - Right Soft Key (`[戻る]`): Returns to Home Cabinet.

### 3.2 `t_editor` (Full Text & TAD Editor)

- **Engine**: Real B-System document editor (`src/apps/t_editor.c`) with gutter line numbering, selection, search, and Japanese TIP/Mozc IME.
- **Viewport**: Rendered inside `480x534` content area (26 lines of text).
- **Controls**:
  - D-pad Up / Down / Left / Right: Caret movement.
  - Numeric / ASCII input: Direct text entry.
  - Return: Insert newline.
  - Backspace: Delete character.
  - Soft Keys: File operations (`[保存]`, `[戻る]`).
