# B-System Documentation Publishing & Screenshot Pipeline Specification

This document specifies the automated, deterministic, and lightweight screenshot extraction and publishing pipeline for **B-System (BTRON 3.20)** documentation.

## 1. Design Philosophy & Requirements

Traditional OS documentation pipelines rely on heavyweight virtualization (QEMU, VirtualBox), display servers (X11, Wayland), or simulated mock DOMs. These approaches are slow, fragile, resource-heavy, and prone to visual divergence from the real C codebase.

The B-System documentation pipeline is designed to be **cheap, instant, authentic, transparent, and unembellished**:
- **0 Virtual Machines / 0 Emulators**: Executes directly as native C99 test harness binaries.
- **0 External GUI Server Dependencies**: Requires no X11, Wayland, SDL, or browser automation.
- **100% Native C99 Rendering**: Calls actual window creation (`open_*_window()`) and graphics primitives (`redraw_all_windows()`, `dp_core.c`, `wnd.c`, `troncode.c`, `jis_fonts.c`, `tibetan_fonts.c`).
- **Interactive State & Active Content**: Populates realistic content (live document text, specifications, shell command history) and triggers interactive dropdown menus directly via event simulation (`EV_BUT_DOWN`).
- **Single Opened-Menu Showcase for Apps**: For application pages, embeds a single comprehensive screenshot showing the active window with its primary menu dropped down over authentic content.
- **Alpha Transparency for Window Surrounds**: The headless framebuffer canvas initializes to `0x00000000` (Alpha = 0). Outer pixels around title tabs, corner chamfers, and bevels are fully transparent in the final PNGs.
- **Strict Window Isolation**: The harness resets the window manager list and zeroes the canvas before every single window capture, preventing any window crosstalk.
- **Plain Unstyled HTML Image Embedding**: Injects clean, raw `<img>` elements into HTML pages with **no artificial CSS box-shadows, borders, or rounded corners**—preserving the exact retro pixel-art aesthetic of authentic BTRON3.
- **Clear File Suffix Hierarchy**:
  - Settings applets use the `_Settings.png` suffix (e.g. `Appearance_Settings.png`).
  - Core applications use the `_Application.png` or `_Menu_Opened.png` suffix (e.g. `Editor_Menu_Opened.png`, `Terminal_Menu_Opened.png`).
  - System desktop menus use descriptive opened names (e.g. `Desktop_MainMenu_Opened.png`, `GlobalMenu_System_Opened.png`).
- **Sub-Second Batch Execution**: Renders all 27+ system windows and menus, encodes compressed PNGs, and populates documentation pages in **<100 milliseconds** total.
- **Lightweight Lossless Storage**: Pure RGBA PNGs (zlib level 9 compression) averaging **4 KB to 28 KB** per window frame.

## 2. Pipeline Architecture & Execution Flow

```
┌────────────────────────────────────────────────────────┐
│               Actual BTRON C99 Codebase                │
│  src/settings/*.c, src/apps/*.c, src/window/wnd.c      │
└──────────────────────────┬─────────────────────────────┘
                           │
                           ▼
┌────────────────────────────────────────────────────────┐
│        Headless C99 Capturer (capture_screens.c)       │
│  - Resets window manager state (init_wnd_mgr)          │
│  - Clears canvas with transparent Alpha 0x00000000     │
│  - Invokes window initializers, loads content & menus  │
│  - Paints widgets, 3D borders, tabs, icons, and text   │
│  - Calculates geometric crop bounding boxes            │
│  - Dumps raw ARGB8888 binary slices to /tmp            │
└──────────────────────────┬─────────────────────────────┘
                           │
                           ▼
┌────────────────────────────────────────────────────────┐
│        Zero-Dependency PNG Encoder (raw_to_png.py)     │
│  - Reads raw ARGB bytes & 8-byte geometry header       │
│  - Converts Little-Endian ARGB to standard RGBA8888    │
│  - Preserves Alpha channel for non-window background   │
│  - Compresses scanlines with Python stdlib zlib        │
│  - Emits valid IHDR, IDAT, IEND PNG binary chunks      │
└──────────────────────────┬─────────────────────────────┘
                           │
                           ▼
┌────────────────────────────────────────────────────────┐
│     Optimized Asset Directory (b-system/img/screens/)  │
│  - <Name>_Settings.png, <Name>_Menu_Opened.png         │
│  - Desktop_MainMenu_Opened.png                         │
│  - Pixel-perfect, isolated & transparent background    │
└──────────────────────────┬─────────────────────────────┘
                           │
                           ▼
┌────────────────────────────────────────────────────────┐
│   Doc Screen Populator (populate_doc_screens.py)       │
│  - Maps each HTML page to its primary screenshot asset │
│  - Injects clean plain <img> tags (no CSS shadows)     │
│  - Uses single opened-menu+content captures for Apps   │
└──────────────────────────┬─────────────────────────────┘
                           │
                           ▼
┌────────────────────────────────────────────────────────┐
│     TAD Binary Document Compiler (html2tad.exs)        │
│  - Compiles all 87 HTML pages into native binary TAD   │
└────────────────────────────────────────────────────────┘
```

## 3. Geometric Crop & Framing Formula

When a window is rendered to the headless canvas, the standard client bounds (`wnd->bounds`) do not include the BTRON3 title tab, 3D outer bevels, and drop shadow. The capture harness calculates the bounding box using the following formula:

$$\begin{aligned}
x_0 &= \max(0, \text{wnd}\to\text{bounds.left} - 4) \\
y_0 &= \max(0, \text{wnd}\to\text{bounds.top} - 30) \\
x_1 &= \min(\text{dev}\to\text{width}, \text{wnd}\to\text{bounds.right} + 6) \\
y_1 &= \min(\text{dev}\to\text{height}, \text{wnd}\to\text{bounds.bottom} + 6)
\end{aligned}$$

- **Top Offset ($-30\text{ px}$)**: Captures the BTRON3 sliding title tab, window caption, and close/zoom widgets.
- **Left Offset ($-4\text{ px}$)**: Captures the 3D raised light bevel.
- **Right/Bottom Offset ($+6\text{ px}$)**: Captures the 3D recessed shadow bevel and resize thumb grip.

## 4. Pixel Format & Color Fidelity

The headless canvas uses a 32-bit ARGB pixel format (`COLOR = uint32_t`, `0xAARRGGBB`).

| Component | Offset in Memory (LE) | Conversion to PNG Scanline |
| :--- | :--- | :--- |
| **Blue (B)** | Byte 0 | Byte 2 |
| **Green (G)** | Byte 1 | Byte 1 |
| **Red (R)** | Byte 2 | Byte 0 |
| **Alpha (A)** | Byte 3 | Preserved from Framebuffer (`0x00` outside window, `0xFF` window surface) |

The pipeline ensures authentic reproduction of:

- **SONY Dark Charcoal Palette** (`#2E3436`, `#1E2224`)
- **BTRON Classic Desktop Teal** (`#008080`)
- **3D Surface Bevels** (Highlight `#FFFFFF`, Shadow `#404040`)
- **Multi-Script Fonts** (TRONCode Kanji, Kana, Ascii, Tibetan dBu-can)
- **Transparent Desktop Cutouts** (Clean overlay on both dark and light documentation themes)

## 5. Screen Asset Inventory & HTML Page Mapping

Screenshots are generated into `b-system/img/screens/` and populated across documentation pages:

### 5.1 Settings Applets (`./b-system/settings/*.html`)

| Applet Document | Japanese Title | Embedded Screenshot File | Typical Size |
| :--- | :--- | :--- | :--- |
| [`Appearance.html`](file:///Ubuntu-22.04/home/maxim/depot/bitedits/btron/b-system/settings/Appearance.html) | 外観 (Appearance) | `Appearance_Settings.png` | ~10.6 KB |
| [`Desktop.html`](file:///Ubuntu-22.04/home/maxim/depot/bitedits/btron/b-system/settings/Desktop.html) | デスクトップ (Desktop) | `Desktop_Settings.png` | ~7.6 KB |
| [`Display.html`](file:///Ubuntu-22.04/home/maxim/depot/bitedits/btron/b-system/settings/Display.html) | 画面表示 (Display) | `Display_Settings.png` | ~7.3 KB |
| [`Input.html`](file:///Ubuntu-22.04/home/maxim/depot/bitedits/btron/b-system/settings/Input.html) | 入力環境 (Input) | `Input_Settings.png` | ~7.4 KB |
| [`Language.html`](file:///Ubuntu-22.04/home/maxim/depot/bitedits/btron/b-system/settings/Language.html) | 言語・文字 (Language) | `Language_Settings.png` | ~11.1 KB |
| [`Media.html`](file:///Ubuntu-22.04/home/maxim/depot/bitedits/btron/b-system/settings/Media.html) | メディア (Media) | `Media_Settings.png` | ~6.2 KB |
| [`Network.html`](file:///Ubuntu-22.04/home/maxim/depot/bitedits/btron/b-system/settings/Network.html) | 通信網 (Network) | `Network_Settings.png` | ~7.4 KB |
| [`Preferences.html`](file:///Ubuntu-22.04/home/maxim/depot/bitedits/btron/b-system/settings/Preferences.html) | 環境設定 (Preferences) | `Preferences_Settings.png` | ~28.8 KB |
| [`Security.html`](file:///Ubuntu-22.04/home/maxim/depot/bitedits/btron/b-system/settings/Security.html) | 保全・権限 (Security) | `Security_Settings.png` | ~6.3 KB |
| [`Sound.html`](file:///Ubuntu-22.04/home/maxim/depot/bitedits/btron/b-system/settings/Sound.html) | 音響・音声 (Sound) | `Sound_Settings.png` | ~7.2 KB |
| [`System.html`](file:///Ubuntu-22.04/home/maxim/depot/bitedits/btron/b-system/settings/System.html) | 基本情報 (System) | `System_Settings.png` | ~6.4 KB |
| [`Terminal.html`](file:///Ubuntu-22.04/home/maxim/depot/bitedits/btron/b-system/settings/Terminal.html) | 端末・通信設定 (Terminal) | `Terminal_Settings.png` | ~10.7 KB |

### 5.2 Core Applications (`./b-system/apps/*.html` — Single Opened-Menu & Content Screenshots)

> **App Screenshot Standard**: For Applications documentation, only single screenshots featuring an active window with its dropdown menu opened and authentic document/shell content are embedded:

| Application Document | Japanese Title | Embedded Screenshot File (Opened Menu & Content) | Typical Size |
| :--- | :--- | :--- | :--- |
| [`Cabinet.html`](file:///Ubuntu-22.04/home/maxim/depot/bitedits/btron/b-system/apps/Cabinet.html) | 実身キャビネット (Cabinet) | `Cabinet_Menu_Opened.png` (ファイルメニュー展開・実身一覧) | ~9.2 KB |
| [`Editor.html`](file:///Ubuntu-22.04/home/maxim/depot/bitedits/btron/b-system/apps/Editor.html) | 基本文章編集 (Editor) | `Editor_Menu_Opened.png` (ファイルメニュー展開・本文テキスト) | ~17.8 KB |
| [`Browser.html`](file:///Ubuntu-22.04/home/maxim/depot/bitedits/btron/b-system/apps/Browser.html) | 仕様書閲覧 (Browser) | `Browser_Menu_Opened.png` (ファイルメニュー展開・仕様書TAD文書) | ~6.0 KB |
| [`Terminal.html`](file:///Ubuntu-22.04/home/maxim/depot/bitedits/btron/b-system/apps/Terminal.html) | 端末 (Terminal / GTerm) | `Terminal_Menu_Opened.png` (ファイルメニュー展開・シェル対話) | ~5.2 KB |
| [`Cassette.html`](file:///Ubuntu-22.04/home/maxim/depot/bitedits/btron/b-system/apps/Cassette.html) | カセット (Cassette) | `Cassette_Application.png` (ステレオカセットデッキ) | ~10.1 KB |
| [`Preferences.html`](file:///Ubuntu-22.04/home/maxim/depot/bitedits/btron/b-system/apps/Preferences.html) | 環境設定 (Preferences) | `Preferences_Settings.png` (コントロールパネルハブ) | ~28.8 KB |
| [`Workbench.html`](file:///Ubuntu-22.04/home/maxim/depot/bitedits/btron/b-system/apps/Workbench.html) | ワークベンチ (Workbench) | `Desktop_Full.png` (初期起動画面 Full Desktop), `About_Application.png` (システム情報ダイアログ) | ~41.2 KB |
| [`Preferences.html`](file:///Ubuntu-22.04/home/maxim/depot/bitedits/btron/b-system/apps/Preferences.html) | 環境設定 (Preferences) | `Preferences_Settings.png` (コントロールパネルハブ) | ~28.8 KB |

### 5.3 Desktop BTRON Main Menu & System Overlays

| Component | State / Dropdown | Generated Screenshot | Typical Size |
| :--- | :--- | :--- | :--- |
| **GlobalMenu** | システムメニューバー (通常状態) | `GlobalMenu.png` | ~1.7 KB |
| **Desktop_MainMenu** | ［BTRON］メインメニュー展開 (Deskbar Hub) | `Desktop_MainMenu_Opened.png` | ~5.0 KB |
| **GlobalMenu_System** | システム(S) ドロップダウン展開 | `GlobalMenu_System_Opened.png` | ~3.5 KB |
| **GlobalMenu_Window** | ウィンドウ(W) ドロップダウン展開 | `GlobalMenu_Window_Opened.png` | ~2.8 KB |

## 6. HTML & TAD Embedding Standard (Plain Images)

All documentation pages link to their corresponding isolated window screenshot using plain semantic markup with **no CSS shadows or artificial borders**, injected automatically via `scripts/populate_doc_screens.py`:

```html
<div class="section">
  <h2>画面プレビュー (Screen Preview)</h2>
  <div class="screen-preview">
    <img src="../img/screens/Editor_Menu_Opened.png" alt="基本文章編集 (Editor Application - Menu Opened)">
    <p>実機描画フレームバッファより自動抽出された基本文章編集 Editor (ファイルメニュー展開・本文テキスト表示)</p>
  </div>
</div>
```

## 7. Makefile Commands

```bash
# 1. Generate screenshots, encode transparent PNGs, and populate clean plain HTML pages
make screenshots

# 2. Recompile all HTML documentation pages to TAD binary format
elixir scripts/html2tad.exs

# 3. Run entire test suite (including GUI, kernel, and app tests)
make test
```

# Credits

* Namdak Tonpa and Grok 4.5
