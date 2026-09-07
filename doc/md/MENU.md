# B-System (BTRON 3.20) Window Menu Architecture & Specification (MENU.md)

This document specifies the architecture, behavioral rules, and implementation details for the BTRON 3.20
Window Menu System and its modern BeOS/Haiku-style Menu Bar integration.

## 1. Architectural Foundation & Spec Conformance

### 1.1 BTRON3 SPEC 3.20 Menu Manager Conformance

According to BTRON3 Specification 3.20 (Chapter 7.2 *Menu Manager*), the system defines two complementary menu mechanisms:

1. **Standard Menu (Стандартне меню / Menu Bar & Pull-down Submenus)**:
   - Displayed in a horizontal row at the top of the window client area.
   - Registered and managed through data structures (`MENUITEM`, `MENUDISP`) and system calls (`mcre_men`, `mopn_men`, `mpop_men`, `mact_men`).
   - Items support activation states (`inact` bitmask), selection indicators (`select` bitmask), and keyboard macros (`MC_KEY1`..`MC_KEY15`).

2. **Generic Menu (GMENU / Pop-up Context Menu)**:
   - Free-floating popup menu positioned anywhere relative to cursor coordinates.
   - Supports custom rectangular layout zones (`RECT r[32]`).

### 1.2 Elimination of the Modal "Open Dialog" Window

- **Historical & Philosophical Context**:
  Traditional desktop systems (Macintosh, MS Windows, Unix CUA/X11) are **application-centric**.
  Launching an application forces the user to navigate a hierarchical directory tree (`/path/to/folder`) through a modal "File Open" dialog box.
  In Ken Sakamura's TRON Architecture, the operating system is **document-centric**:
  - The filing system is modeled as a hypermedia network of **Real Objects (実身)** and **Virtual Objects (仮身)**.
  - Documents are opened directly from Cabinets or embedded Virtual Object fusen links without a file picker dialog.

- **Cleanroom Implementation Rule**:
  Editor and BTRON applications do not use modal file-opening dialogs. Instead:
  - The `ファイル(F)` (File) menu contains an **`開く (Open) ▶` cascading dropdown menu**.
  - The menu dynamically scans available document repositories (e.g., `assets/texts/` filtering `.txt` files).
  - The user accesses any document in **1–2 fluid clicks** with zero modal interruption, zero path typing, and zero file picker friction.

## 2. Menu Bar Structure & Layout

### 2.1 Streamlined Vertical Window Geometry

Following the elimination of the redundant legacy CUA toolbar row and the consolidation of the TIP
indicator to the status bar footer, the Menu Bar is purely dedicated to application commands and document status:

```
┌─────────────────────────────────────────────────────────────┐
│ [Title Bar] Window Title                              [X]   │
├─────────────────────────────────────────────────────────────┤ y = 0
│ ファイル(F) 編集(E) 表示(V) 仮身(O) ヘルプ(H)           📄 Doc* │ Menu Bar (y = 0..21)
├─────────────────────────────────────────────────────────────┤ y = 22
│ 1| Document content canvas begins directly here...          │
│ 2|                                                          │ Editor Canvas (y = 24..)
│  |                                                          │
├─────────────────────────────────────────────────────────────┤
│ 📄 BTRON3_Report.txt                UTF-8 | Rows: 42 | [あ] │ Status Bar Footer
└─────────────────────────────────────────────────────────────┘
```

### 2.2 Standard Top-Level Menus & Non-Overfull Margins

All top-level headers are sized to strictly eliminate text overfulls, accounting for 16px wide JIS/Kanji
glyphs and 8px wide ASCII glyphs with a guaranteed 8px horizontal padding on each side:

| Menu Title | Text Width | Button Rect | Internal Margins | Items & Shortcuts |
| :--- | :--- | :--- | :--- | :--- |
| **ファイル(F)** | 88px | `[4, 0, 108, 21]` (104px) | 8px left / 8px right | `新規作成 Ctrl+N`, `開く ▶ Ctrl+O`, `---`, `上書き保存 Ctrl+S`, `閉じる Ctrl+W` |
| **編集(E)** | 56px | `[112, 0, 184, 21]` (72px) | 8px left / 8px right | `元に戻す Ctrl+Z`, `---`, `切り取り Ctrl+X`, `コピー Ctrl+C`, `貼り付け Ctrl+V`, `すべて選択 Ctrl+A` |
| **表示(V)** | 56px | `[188, 0, 260, 21]` (72px) | 8px left / 8px right | `拡大 +`, `縮小 -`, `---`, `行番号表示` |
| **仮身(O)** | 56px | `[264, 0, 336, 21]` (72px) | 8px left / 8px right | `仮身を挿入`, `実身キャビネット` |
| **ヘルプ(H)** | 72px | `[340, 0, 428, 21]` (88px) | 8px left / 8px right | `Editor について (About)` (Opens 280x135 nano About Box with 5HT attribution) |
| **Document Status** | Dynamic | `[width - title_w - 14, 3]` | 8px left / 14px right | Dynamic width right-aligned filename status |

### 2.3 Explicit Item Division: 3D Etched Distance Shadow / Separator

To maximize CPU rendering efficiency while delivering a distinct visual separation between top-level menu items:

- **Etched Dual-Line Groove**: Each horizontal item boundary is demarcated by a 2-pixel vertical etched groove:
  - Left column (`COLOR_DKGRAY`): 1px shadow line from `y = 3` to `y = 19`.
  - Right column (`COLOR_WHITE`): 1px highlight line directly adjacent (`x + 1`) from `y = 3` to `y = 19`.

- **CPU Efficiency**:
  - Implemented via contiguous 1px vertical pixel writes (`fill_rec`).
  - Exactly 16 dword writes per line (32 writes total per separator).
  - Sub-microsecond execution time with zero overdraw, zero blending, and zero matrix transformations.

- **Placement**:
  - Between `ファイル(F)` and `編集(E)` (`x = 110`)
  - Between `編集(E)` and `表示(V)` (`x = 186`)
  - Between `表示(V)` and `仮身(O)` (`x = 262`)
  - Between `仮身(O)` and `ヘルプ(H)` (`x = 338`)
  - Before the Document Status Title (`x = width - title_w - 22`)

## 3. BeOS/Haiku & Modern Menu Bar Behavioral Rules

### 3.1 Hover Selection Architecture

Hover selection is supported across all layers of the window and desktop event hierarchy:

1. **Desktop Event Forwarding (`src/desktop/main.c`)**:
   - The desktop event loop dispatches `EV_MOUSE_MOVE` events directly to the active top window (`top->event_handler(top, &ev)`), allowing in-window controls to track cursor movements continuously without waiting for clicks.

2. **Closed State Header Hover**:
   - When no menu is currently dropped down (`active_menu == -1`), hovering over any top-level header (`ファイル(F)`, `編集(E)`, etc.) highlights the header with a crisp white bevel border and navy text (`COLOR_NAVY`), visually signaling interactivity.
   - Moving the pointer off the Menu Bar cleanly restores standard inactive presentation.

3. **Active Menu Tracking & Hot Header Switching**:
   - Once a menu is clicked open, moving the pointer across to another top-level menu header immediately closes the current menu and opens the target menu without requiring any further clicks.

4. **Dropdown & Cascading Item Hover Selection**:
   - Hovering over valid items highlights them with `COLOR_NAVY` fill and `COLOR_WHITE` text.
   - Hovering over items with child submenus (`開く (Open) ▶`) **automatically expands the cascading submenu on hover**, eliminating redundant clicks.
   - Submenu items similarly highlight on hover and execute on a single click.

5. **Execution & Dismissal**:
   - Clicking an enabled item executes its command and dismisses all menus.
   - Clicking outside any active menu closes the menu system and returns focus to the editor canvas.
   - Pressing `Escape` cancels and dismisses active menus cleanly.

## 4. Dynamic Asset Filesystem Discovery (`assets/texts/*.txt`)

### 4.1 Asset Scanner Architecture

The `開く (Open) ▶` cascading menu dynamically scans the filesystem to populate available document items:

- Target Directory: `assets/texts/` (with fallback to `assets/`).
- File Filter: Only files ending in `.txt` (case-insensitive).
- Invariant & Safety:
  - NASA JPL Rule 3 compliant (bounded buffer array `MAX_MENU_FILES 32`, zero post-boot heap allocations).
  - Deterministic fallback list if running on baremetal/unhosted environments without POSIX `dirent`.
- Item Representation:
  - Displays icon and filename: `📄 <filename>`.
  - Selecting an item invokes `Editor_load_file()` directly.

## 5. Keyboard Accelerator Specifications

| Action | Accelerator Key | Behavior |
| :--- | :--- | :--- |
| **New Document** | `Ctrl + N` | Clears document buffer, resets to untitled state |
| **Open Menu** | `Ctrl + O` | Directly drops down the `開く (Open) ▶` document menu |
| **Save Document** | `Ctrl + S` | Saves document to current filename |
| **Close Window** | `Ctrl + W` | Closes active editor window |
| **Cut** | `Ctrl + X` | Deletes selected text and copies to system clipboard |
| **Copy** | `Ctrl + C` | Copies selected text to system clipboard |
| **Paste** | `Ctrl + V` | Inserts system clipboard text at cursor position |
| **Select All** | `Ctrl + A` | Selects entire document buffer |
| **Zoom In** | `+` (or `=`) | Increases window size / visible text lines |
| **Zoom Out** | `-` | Decreases window size / visible text lines |
| **Toggle IME** | `F10` | Switches TRON IME mode (ASCII → Hiragana → Katakana → Tibetan) |

## 6. Visual Design & Theme Tokens

- **Menu Bar Background**: `COLOR_LTGRAY` (`0xFFD0D0D0`) with bottom separator line `0xFF808080`.
- **Menu Panel Fill**: `COLOR_WHITE` (`0xFFFFFFFF`) or `0xFFF6F6F6`.
- **Bevel Border**: 2px 3D border (`COLOR_WHITE` top/left, `COLOR_DKGRAY` bottom/right) conforming to BTRON / BeOS styling.
- **Hover Selection**: `COLOR_NAVY` (`0xFF003366`) with `COLOR_WHITE` text.
- **Separator Lines**: Inset etched horizontal line (`drw_lin` with `COLOR_GRAY` and `COLOR_WHITE` 1px offset).
- **Submenu Arrow**: `▶` (Unicode `U+25B6` / TRON Code) placed at right margin.

## 7. Global System Menu (B-right/V Chokanji & Haiku Desktop Control Bar)

### 7.1 Architectural Role: System Control Bar vs. In-Window Menus

In strict conformance with Ken Sakamura's BTRON specification, B-right/V, and Chokanji (超漢字):

- **Autonomous Window Menus**: Individual application windows (such as Editor and TAD Browser) possess their own in-window Menu Bar for document operations (`ファイル(F)`, `編集(E)`, `表示(V)`, etc.).
- **Global Desktop Control Bar (システム操作バー)**: The top bar of the screen (`y = 0..25`) belongs exclusively to the Desktop Shell & System Manager. It does not mirror or duplicate active window file menus, eliminating visual collision and confusion.

```
┌────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│ ［BTRON］ システム(S) ▼  実身・仮身(O) ▼  ウィンドウ(W) ▼  道具・文字(T) ▼   [TIP: あ]  9月4日(金) 00:57:30 │
└────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

### 7.2 Global Menu Hierarchy (Full Chokanji)

| Header | Mnemonic | Purpose | Dropdown Items |
| :--- | :--- | :--- | :--- |
| **［BTRON］** | Start / Deskbar | Haiku-style Root Launcher | Quick App Launcher, Recent Files, Active Tasks, Power |
| **システム(S)** | Alt+S | System Environment & Hardware | `システム情報 (About B-System...)`, `環境設定 (Settings Cabinet...)`, `---`, `音響機器 (Cassette Audio...)`, `画面表示 (Display Settings...)`, `---`, `デスクトップ再起動 (Restart Desktop)`, `システムの終了 (Shutdown)` |
| **実身・仮身(O)** | Alt+O | Real/Virtual Object Hypermedia | `実身キャビネットを開く (Open Root Cabinet)`, `実身・仮身の検索 (Search Real/Virtual Bodies)`, `新規実身の作成 (New Real Object Fusen)`, `共有実身保管庫 (Shared Storage Pool)` |
| **ウィンドウ(W)** | Alt+W | Window Management & Task List | `重ねて整列 (Cascade Windows)`, `並べて整列 (Tile Windows)`, `すべて隠す (Hide All)`, `次のウィンドウ (Cycle Focus - Alt+Tab)`, `---`, `[*] Window Title 1`, `[ ] Window Title 2...` |
| **道具・文字(T)** | Alt+T | Tools, Character Palette & Code | `文字パレット (TRON Character Palette)`, `TRONコード検索 (TRON-Code Plane Lookup)`, `Mozc 日本語辞書 (IME Dictionary Tool)`, `---`, `表計算・計算機 (Matrix & Calculator)`, `端末 (gterm Terminal)` |

### 7.3 Authentic Japanese Tray & Cultural Ergonomics

- **Japanese Calendar & Kanji Weekday (曜日表示)**:
  - Real-time display: `M月D日(曜日) HH:MM:SS` (e.g. `9月4日(金) 00:57:30`).
  - Kanji weekday dynamically derived from `tm_wday`: `(日)`, `(月)`, `(火)`, `(水)`, `(木)`, `(金)`, `(土)`.
  - Recessed 3D plate in `COLOR_LTGRAY` with dark gray top/left shadow.

- **Global TIP Input Method Status**:
  - Direct interactive indicator: `[TIP: あ (F10)]` (Hiragana `あ`, Katakana `ア`, ASCII `A`, Tibetan `བོད`).
  - Clicking cycles mode globally; reflects active Mozc composition state.

- **CPU & Resource Gauge**:
  - Compact SMP multi-core load indicator showing kernel thread activity.

### 7.4 Non-Overfull Margins & Etched Distance Shadows

- Sized with guaranteed 8px margins per side accounting for 16px JIS kanji glyphs.
- Separated by CPU-efficient 2px 3D etched grooves (`draw_menu_separator_v`).
- Full BeOS-style live hover selection:
  - Closed header hover highlight (white bevel + navy text).
  - Hot header tracking when a global menu is open.
  - Automatic dismissal when clicking on the desktop or any window.

## 8. TAD Browser Menu Specification (実身閲覧・TAD表示)

### 8.1 Menu Bar Layout (`y = 0..21`)

```
┌────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│ ファイル(F) │ 表示(V) │ 仮身(O) │ ヘルプ(H) │ [◄ 戻る] [► 進む]  tad_bin/sample.tad                   │
└────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

| Header | Mnemonic | Items | Purpose |
| :--- | :--- | :--- | :--- |
| **ファイル(F)** | Alt+F | `開く (Open Document...) ▶`<br>`---`<br>`印刷 (Print...)`<br>`---`<br>`閉じる (Close Window)` | Document selection cascading submenu, printing, and window closing (`Ctrl+W`) |
| **表示(V)** | Alt+V | `戻る (Back)` (`Alt+Left`)<br>`進む (Forward)` (`Alt+Right`)<br>`ホーム / 先頭 (Home)`<br>`再読込 (Reload)` (`Ctrl+R`)<br>`---`<br>`拡大 (Zoom In)` (`+`)<br>`縮小 (Zoom Out)` (`-`)<br>`標準サイズ (100%)`<br>`---`<br>`行折り返し (Wrap Text)` | Navigation history, zoom scaling, and dynamic word wrapping |
| **仮身(O)** | Alt+O | `リンク先を開く (Follow Fusen Link)` (`Enter`)<br>`リンク先を別窓で開く (Open Link in New Window)`<br>`実身キャビネットで表示 (Show in Cabinet)` | Hypermedia Fusen link traversal and cabinet cross-referencing |
| **ヘルプ(H)** | Alt+H | `TADブラウザ について (About...)`<br>`BTRON TAD 仕様書 (TAD Spec...)` | Nano About Box ("Brought to B-System by 5HT") and technical documentation |

## 9. Cabinet App Menu Specification (実身・仮身キャビネット)

### 9.1 Menu Bar Layout (`y = 0..21`)

```
┌────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│ ファイル(F) │ 編集(E) │ 表示(V) │ 仮身(O) │ ヘルプ(H) │ [一覧/アイコン]         128 実身 / 32 保管庫    │
└────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

| Header | Mnemonic | Items | Purpose |
| :--- | :--- | :--- | :--- |
| **ファイル(F)** | Alt+F | `実身を開く (Open Real Body)` (`Enter`)<br>`実身を閲覧 (View in TAD Browser)` (`Space`)<br>`新規実身の作成 (New Real Object)` (`Ctrl+N`)<br>`---`<br>`実身の複製 (Duplicate Real Object)` (`Ctrl+D`)<br>`実身の削除 (Delete Real Object)` (`Delete`)<br>`---`<br>`閉じる (Close Cabinet)` (`Ctrl+W`) | Lifecycle management of Real Objects in storage |
| **編集(E)** | Alt+E | `すべて選択 (Select All)` (`Ctrl+A`)<br>`選択解除 (Deselect All)` (`Escape`)<br>`---`<br>`名称変更 (Rename Real Object)` (`F2`)<br>`プロパティ (Object Properties...)` (`Alt+Enter`) | Real Body item selection and metadata editing |
| **表示(V)** | Alt+V | `[*] 一覧表示 (List View)` (`Ctrl+1`)<br>`[ ] アイコン表示 (Icon Grid View)` (`Ctrl+2`)<br>`---`<br>`名前順で整列 (Sort by Name)` (`F6`)<br>`日付順で整列 (Sort by Date)` (`F7`)<br>`サイズ順で整列 (Sort by Size)` (`F8`)<br>`種類順で整列 (Sort by Type)` (`F9`)<br>`---`<br>`再走査・更新 (Rescan / Refresh)` (`F5`) | View switching and multi-criteria sorting |
| **仮身(O)** | Alt+O | `新規仮身リンクの作成 (Create Fusen Link)` (`Ctrl+L`)<br>`所属キャビネットの変更 (Move to Cabinet...)`<br>`総索引の表示 (Global Index Catalog)` (`Ctrl+I`) | Virtual Object link creation and hypermedia organizing |
| **ヘルプ(H)** | Alt+H | `キャビネット について (About Cabinet...)`<br>`実身・仮身モデル解説 (Hypermedia Architecture...)` | Nano About Box ("Brought to B-System by 5HT") and Sakamura hypermedia architecture guide |

## 10. Unified Application Menu Engine & Multi-Style System Settings

### 10.1 Architecture & Complete Deduplication

Prior to BTRON 3.20 Update 9, Editor, TAD Browser, and Cabinet Manager maintained independent in-window menu structures, event hit testers, and drawing algorithms. This led to code duplication and visual inconsistencies.

The **Unified Application Menu Engine** (`include/btron/app_menu.h`, `src/window/app_menu.c`) standardizes all application-level menus:

- **Shared Data Structures**:
  - `APP_MENU_BAR`: Encapsulates headers, items, bounding geometries, hover state, active header index, and right-aligned status text.
  - `APP_MENU_HEADER`: Represents top-level categories with computed margins and item arrays.
  - `APP_MENU_ITEM`: Unified entry supporting normal commands, checkable states, separators, and cascading submenus (`▶`).

- **Standardized Event Handling**:
  - `app_menu_handle_mouse_move`: Fluid BeOS-style hot header gliding, closed hover detection, item highlighting, and cascading submenu tracking.
  - `app_menu_handle_mouse_down`: Top-level header toggle, dropdown item selection, cascading item loading, and outside dismissal.
  - `app_menu_handle_key`: Universal `Escape` dismissal.

- **Zero Duplication**:
  - `Editor`, `TAD_BROWSER`, and `CABINET_EXPLORER` now directly use `APP_MENU_BAR` and common painting/event routines.
  - Legacy tracking fields (`active_menu`, `hover_menu`, `hover_item`) are synchronized seamlessly for full backward compatibility.

### 10.2 Dual Menu Styles Configurable in Appearance Settings

As requested by users who appreciate both authentic retro ergonomics and contemporary visual polish, B-System supports two selectable menu visual styles:

```
┌────────────────────────────────────────────────────────┐
│ [●] Classic Chokanji 3D (立体ベベル外枠 - 推奨)        │
│ [ ] Modern Flat Card (現代的カード・影付き)            │
└────────────────────────────────────────────────────────┘
```

1. **Classic Chokanji 3D (`APP_MENU_STYLE_CLASSIC_3D`) — Default**:
   - The gold standard established in Editor.
   - 4-sided crisp 3D bevel box (`draw_3d_bevel_box`) with `COLOR_LTGRAY` background fill.
   - `COLOR_WHITE` top/left highlight lines and `COLOR_DKGRAY` bottom/right shadow lines.
   - Clean 3D vertical etched separators between menu bar headers.

2. **Modern Flat Card (`APP_MENU_STYLE_MODERN_CARD`)**:
   - Flat card surface with 1px border.
   - 3px offset soft drop shadow (`COLOR_DKGRAY`) mimicking modern desktop window cards.
   - Minimalist vertical separator lines.

Both styles can be selected dynamically at runtime from **Appearance Settings (`[Settings Cabinet] Appearance`)** via `app_menu_set_global_style()`. Changes take effect immediately across all in-window menus (Editor, TAD Browser, Cabinet), the top-level **Global System Menu Bar (`src/desktop/global_menu.c`)**, and the **Haiku-Style Deskbar Start Menu (`src/desktop/tracker.c`)**.

### 10.3 Standardized Nano About Box Dialog

All applications instantiate their About Box using the unified `app_menu_create_about_dialog()` API:

- **Aligned Dimensions**: Exactly 380×155 pixels (aligned from tight 280×135 to provide comfortable breathing room).

- **Visual Presentation**:
  - Classic 3D beveled container (`COLOR_LTGRAY`) with inner card (`COLOR_WHITE`).
  - Integrated 32×32 raster application icon (`assets/icons/*.gif` / `*.png` converted from OpenMoji SVGs) with transparent color palette clipping.
  - Bilingual title duplication (e.g., `Editor 3.20 (文書編集)`).
  - Clean text alignment for application name, full description, and explicit attribution: **`"Brought to B-System by 5HT"`**.
  - Centered 3D `[ OK ]` button (72×22).

- **Universal Dismissal**: Closes immediately on `OK` click, window close button `[X]`, or pressing `Escape`, `Enter`, or `Space`.

# Credits

* Namdak Tonpa and Grok 4.5
