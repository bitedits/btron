# BTRON Application Databus (TAD) in B-System

This document provides a comprehensive technical overview of **TAD (TRON Application Databus)** in B-System, explaining the unified `./tad_bin/` ecosystem, the Elixir multi-catalog batch compiler, native **GIF & PNG** diagram rasterization, binary segment format conforming to **BTRON3 SPEC 3.20**, compatibility with **B-right/V Cho-Kanji (超漢字)**, and runtime usage across the **Cabinet Explorer** and **Native TAD Document Browser**.

## 1. Overview: What is TAD?

In the TRON architecture designed by Prof. Ken Sakamura, **TAD (TRON Application Databus)** is the foundational, multi-modal data exchange format. Unlike standard linear text files (ASCII/UTF-8) or rigid relational databases, TAD is a **hyper-data format** composed of a continuous stream of text and embedded **Fusen (付箋 - Sticky Tabs/Tags)**.

In B-System, TAD serves two critical roles:
1. **Hyper-Object Document Storage:** Organizes system documentation, books, and user documents into first-class **Real Bodys (実身 - Jitsushin)** with embedded **Virtual Body (仮身 - Kashin)** links.
2. **Deterministic Bounded Layout:** A cleanroom linear layout engine replaces legacy complex DOM trees with a single-pass stream engine adhering strictly to **NASA Safety-Critical C Rules (JPL Rule 3: Zero post-boot dynamic heap allocations)**.

## 2. Directory Structure & Unified Packaging Pipeline

All documentation, specifications, and foundational books in B-System are packaged into a single, canonical directory: `./tad_bin/`.

```
btron/
└── tad_bin/                      # ◄ Unified Canonical TAD Real Bodys Ecosystem
    ├── 01_btron3_spec.tad        # Book 1: BTRON3 3.20 共通仕様書 (Canonical Book)
    ├── 02_tkernel_book.tad       # Book 2: T-Kernel 2.0 リアルタイムOS完全解説
    ├── 03_tron_hmi_book.tad      # Book 3: TRON HMI 意匠設計指針と部品カタログ
    ├── 04_bfree_os_book.tad      # Book 4: B-Free 自由OS読本とマニフェスト
    ├── index.tad                 # Головний портал документації B-System
    │
    ├── shared_data/              # [tad_bin][Специфікація] Загальні специфікації даних
    │   ├── data_type.tad         # Типи даних, коди помилок та C99 межі
    │   ├── tron_code.tad         # Багатомовне кодування TRON Code
    │   ├── tad1.tad              # Специфікація структури TAD сегментів
    │   ├── tad2.tad              # Текстові фусени (Text Fusen)
    │   ├── tad3.tad              # Графічні фусени (Figure Fusen)
    │   ├── fd_format.tad         # Файлова система реальних та віртуальних об'єктів
    │   └── gif/*.gif             # 218 Genuine Specification GIF Diagrams
    │
    ├── os_spec/                  # Специфікації операційної системи BTRON3
    │   ├── kernel/*.tad          # [tad_bin][Ядро] Ядро μITRON 3.0, Задачі, Пам'ять, IPC, Таймери
    │   ├── dp/*.tad              # [tad_bin][Графіка] Графічні примітиви DP (2D Display Primitives)
    │   ├── shell/*.tad           # [tad_bin][Оболонка] Графічна оболонка, Вікна, Меню, Панелі, TIP/IME
    │   └── indexfig.tad          # [tad_bin][Надійність] Статичний аналіз пам'яті (NASA JPL Rule 3)
    │
    ├── b-hmi/                    # TRON Human-Machine Interface Guidelines & 220+ Parts
    │   ├── part1/*.tad           # SUI, GUI, Panels, Display Standards
    │   ├── part2/*.tad           # Layout, Procedures, Multipanel, Safety Standards
    │   ├── part_book/*.tad       # Switches, Volumes, Selectors Catalog
    │   └── img/*.png             # 220+ PNG High-Resolution Technical Illustrations
    │
    ├── b-free/                   # Free Software BTRON3 Implementation
    │   ├── manifest.tad          # B-Free Manifesto
    │   ├── kernel.tad            # Microkernel Architecture
    │   └── posix.tad             # POSIX Compatibility Layer
    │
    ├── b-system/                 # B-System POSIX & VirtIO Specs
    │   ├── virtio.tad            # VirtIO Core Architecture & System Specifications
    │   └── btron_spec.tad        # System Call Matrix
    │
    ├── t-kernel/                 # T-Kernel 2.0 Deployment Manuals
    │   ├── tkernel_spec.tad      # T-Kernel Core Specifications
    │   ├── tkernel_startup.tad   # Startup & Boot Sequence
    │   └── tkernel_qemu.tad      # QEMU Virtual Machine Manual
    │
    └── *.tad.txt                 # Companion human-readable symbolic TAD files
```

## 3. Binary TAD Segment Structure (BTRON3 SPEC 3.20)

Every `.tad` file in `./tad_bin/` begins with the 16-bit **TAD Main Record Header** followed by structured Fusen segments:

```
┌──────────────────────────────────────────────────────────────────────────────────┐
│  Record Header: Type = 0x0001 (Main Record) | 32-bit Payload Length (N bytes)   │
├──────────────────────────────────────────────────────────────────────────────────┤
│  TS_TPAGE   (0xFFA0) : Page Geometry (Width, Height, Top/Left/Right Margins)     │
├──────────────────────────────────────────────────────────────────────────────────┤
│  TS_TRULER  (0xFFA1) : Text Ruler (Line pitch: 18..24px, Left indent, Tabs)      │
├──────────────────────────────────────────────────────────────────────────────────┤
│  TS_TFONT   (0xFFA2) : Font Fusen (Font ID: Mincho/Gothic/Mono, TRON Plane: 0/1) │
├──────────────────────────────────────────────────────────────────────────────────┤
│  TS_TCHAR   (0xFFA3) : Character Style (Point Size: 10..22pt, Weight, RGB Color) │
├──────────────────────────────────────────────────────────────────────────────────┤
│  TS_VOBJ    (0xFFA8) : Embedded Virtual Body Link [仮身] (Target Real Body)  │
├──────────────────────────────────────────────────────────────────────────────────┤
│  TS_FPRIM   (0xFFB0) : Figure Primitive (Vector lines, Boxes, GIF & PNG Pictures)│
├──────────────────────────────────────────────────────────────────────────────────┤
│  Raw Text Payload    : Multi-byte TRON-Code / UTF-8 character byte stream        │
└──────────────────────────────────────────────────────────────────────────────────┘
```

### Segment Opcode Reference

| Segment Opcode | Hex Tag | Description | Payload Attributes |
|:---|:---:|:---|:---|
| `TS_TPAGE` | `0xFFA0` | Text Page Fusen | Page width, page height, margin top/bottom/left/right |
| `TS_TRULER` | `0xFFA1` | Text Ruler Fusen | Line pitch (px), paragraph indent, tab stops |
| `TS_TFONT` | `0xFFA2` | Text Font Fusen | Font typeface (0=System, 1=Mincho, 2=Gothic, 3=Mono), TRON Plane |
| `TS_TCHAR` | `0xFFA3` | Character Fusen | Font size (10..24pt), bold w### Build Commands

```bash
# Compile all documentation trees and 4 canonical books into ./tad_bin/:
make tad_bin

# Run the complete C-level unit test harness (95 tests):
make test-tad
```

## 5. Runtime Usage in B-System

### 1. Cabinet Explorer (実身キャビネット - `src/apps/vobj_manager.c`)
- **Central Real Body Library:**
  - **Canonical Books:** Tagged with `[TAD Spec]`, `[T-Kernel]`, `[HMI Guide]`, `[B-Free OS]`.
  - **System Specifications:** Tagged with `[tad_bin][Ядро]`, `[tad_bin][Специфікація]`, `[tad_bin][Графіка]`, `[tad_bin][Оболонка]`, and `[tad_bin][Надійність]`.
- **Interactive Navigation:**
  - **Single Click / Key Arrow:** Selects an object, highlighting it in BTRON Navy Blue (`COLOR_NAVY`) and displaying metadata (Real Body ID, category, size in bytes) in the status bar.
  - **Double-Click / [開く (Open)] / [閲覧 (View)]:** Instantly opens the selected Real Body in the **TAD Document Browser**.
  - **Smooth Scrolling:** Scrollbar thumb and keyboard scrolling (<kbd>Page Up</kbd>, <kbd>Page Down</kbd>, <kbd>j</kbd>, <kbd>k</kbd>) for navigating large document libraries.

### 2. Native TAD Document Browser (`src/apps/tad_browser.c`)

```
┌──────────────────────────────────────────────────────────────────────────────────────────┐
│ [◄ Назад] [► Вперед] [⌂ Дім] [↻ Оновити]  [ 📄 tad_bin/os_spec/kernel/kernel.tad      ] │ ◄ Fixed Toolbar (0..30px)
├──────────────────────────────────────────────────────────────────────────────────────────┤
│ ■ 第5章 μITRON リアルタイムカーネル (Real-Time Kernel & Tasking)                         │ │
│ ────────────────────────────────────────────────────────────────────────                 │ │
│ B-System μITRON 3.0 コアアーキテクチャ。タスクコンテキスト、ディスパッチャ、セマフォ。    │ │ Content Area
│                                                                                          │ │
│ [仮身] 第1節 タスク管理とコンテキスト切替 (proc.tad) ───────────────────────► [Click]    │█│ Scrollbar Track
│ [仮身] 第2節 リアルタイムメモリプール (memory.tad)                                       │ │
│                                                                                          │ │
├──────────────────────────────────────────────────────────────────────────────────────────┤
│ 🔗 [仮身] Перехід до: proc.tad (Real Body #122) [Клацніть для переходу]                 │ ◄ Status Bar (Preview)
└──────────────────────────────────────────────────────────────────────────────────────────┘
```

#### How to Navigate in TAD Browser

1. **Hyper-Link Navigation (Virtual Bodys `[仮身]`):**
   - **Click on any Link Box:** Clicking any blue `[仮身] ...` box loads the destination document in-place.
   - **Hover Preview:** Hovering over any link highlights it with a light-gray border and displays the exact target path and Real Body ID in the bottom status bar (`🔗 [仮身] Перехід до: ...`).
   - **Automatic Path Resolution (`tad_browser_resolve_path`):**
     - Automatically translates legacy `.html` links to native `.tad` files.
     - Strips anchor `#fragments` and `?queries`.
     - Resolves relative child paths (e.g. `kernel/kernel.tad`), sibling paths (`../dp/dp.tad`), and fallback lookups (`tad_bin/`, `tad_bin/b-hmi/`, etc.).

2. **Toolbar Navigation Controls:**
   - **`[◄ Назад (Back)]`**: Jumps back to the previous document in the history stack.
   - **`[► Вперед (Forward)]`**: Navigates forward in the history stack.
   - **`[⌂ Дім (Home)]`**: Instantly returns to the foundational canonical portal (`tad_bin/01_btron3_spec.tad`).
   - **`[↻ Оновити (Reload)]`**: Re-reads the current document from disk and preserves the active scroll position.
   - **Location / Breadcrumb Bar:** Displays the current active Real Body path (`📄 tad_bin/...`).

3. **Keyboard Shortcuts:**

| Action | Primary Key | Alternate Key | Description |
|:---|:---:|:---:|:---|
| **Go Back** | <kbd>Backspace</kbd> | <kbd>b</kbd> / <kbd>B</kbd> | Navigate to the previous document in history |
| **Go Forward** | <kbd>f</kbd> / <kbd>F</kbd> | <kbd>Alt+Right</kbd> | Navigate forward in history |
| **Go Home** | <kbd>h</kbd> / <kbd>H</kbd> | <kbd>Alt+Home</kbd> | Jump to `tad_bin/01_btron3_spec.tad` |
| **Reload** | <kbd>r</kbd> / <kbd>R</kbd> | <kbd>F5</kbd> | Reload current document |
| **Scroll Line Down** | <kbd>↓</kbd> | <kbd>j</kbd> | Scroll down by 24 pixels |
| **Scroll Line Up** | <kbd>↑</kbd> | <kbd>k</kbd> | Scroll up by 24 pixels |
| **Scroll Page Down**| <kbd>Page Down</kbd> | <kbd>Space</kbd> | Scroll down by one viewport page |
| **Scroll Page Up**  | <kbd>Page Up</kbd> | <kbd>Shift+Space</kbd> | Scroll up by one viewport page |

4. **NASA Safety-Critical C (JPL Rule 3) Layout:**
   - Single-pass layout engine strictly computes bounding boxes without dynamic heap allocations, ensuring sub-millisecond document loading and guaranteed bounded memory execution on all embedded targets.

## 6. UTF-8 Cyrillic & Ukrainian Font Engine

BTRON3 SPEC 3.20 specifies that TRON Code Plane 1 (`0x2700..0x27FF`) is dedicated to **Cyrillic (ISO 8859-5) & Ukrainian Extensions**:

```
UTF-8 Cyrillic (2 bytes) ──────► [ utf8_to_tc ] ──────► TRON Code Plane 1 (0x27xx)
  - А..Я (U+0410..U+042F)                                - 0x2710..0x272F
  - а..я (U+0430..U+044F)                                - 0x2730..0x274F
  - Є, І, Ї, Ґ (U+0404, 0406, 0407, 0490)                - 0x2704, 0x2706, 0x2707, 0x2790
  - є, і, ї, ґ (U+0454, 0456, 0457, 0491)                - 0x2754, 0x2756, 0x2757, 0x2791
                                                                  │
                                                                  ▼
                                                      [ get_glyph_bitmap ]
                                                                  │
                                                                  ▼
                                                      16x16 Dot-Matrix Bitmap
                                                      (Pixel-Perfect Rendering)
```

- **Collision Resolution:** Cyrillic 2-byte sequences are mapped directly to `0x2700..0x27FF`, completely avoiding collision with Japanese Hiragana (`0x2400..0x245F`) and Katakana (`0x2500..0x255F`).
- **Proportional 8x16 Typography (Zero Inter-Character Gap):** Cyrillic & Ukrainian glyphs are rendered in crisp 8x16 dot-matrix cells (`gw = 8, gh = 16`), matching English ASCII text width and kerning instead of wide 16x16 Zenkaku boxes.
- **Dedicated Ukrainian Glyphs:** High-contrast dot-matrix bitmaps for `Є`, `І`, `Ї`, `Ґ`, `є`, `і`, `ї`, `ґ`, `№`, `«`, and `»` ensure sharp, natural legibility in the TAD Document Browser, T-Editor, and Cabinet Explorer.
- **Clean Hyper-Data Links:** Virtual Body links are compiled as atomic binary `TS_VOBJ` segments without redundant raw text duplications, producing an uncluttered, modern layout.

## 7. BTRON3 3.20 Figure & Picture Segment Fusen (`TS_FPRIM`) with GIF & PNG Support

In BTRON3 SPEC 3.20, illustrations, diagrams, and raster images are embedded directly into TAD document streams using **Figure Fusen Segments (`TS_FPRIM` / `0xFFB0`)**:

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│  TS_FPRIM (0xFFB0) : Segment Tag (16-bit) | Payload Length (32-bit)                    │
├────────────────────────────────────────────────────────────────────────────────────────┤
│  SubID = 10 (Picture/Figure) | Width (16-bit) | Height (16-bit) | Diagram Type (8-bit) │
├────────────────────────────────────────────────────────────────────────────────────────┤
│  Caption Length (16-bit) | UTF-8 Caption Text (e.g. "図 1: アーキテクチャ階層構造")      │
├────────────────────────────────────────────────────────────────────────────────────────┤
│  Source Path Length (16-bit) | Asset URI / Real Body Path (e.g. "img/fig_01.png")    │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

- **Visual Frame & Geometry:** Rendered within a distinct 3D beveled BTRON frame with navy header banner (`[<image_path>]`), clear drawing canvas, and bottom caption.
- **Dual Raster Decoders (GIF & PNG):**
  - **GIF Decoder:** Decodes LZW-compressed 8-bit palette GIF specification diagrams (218 spec diagrams across `tad_bin/**/gif/`).
  - **PNG Decoder:** Decodes Deflate-compressed RGB and RGBA PNG diagrams with full scanline unfiltering (Paeth, Sub, Up, Average, None) (220+ illustrations across `tad_bin/b-hmi/img/`).
  - Implemented strictly with bounded static memory conforming to **NASA JPL Safety Rule 3** (zero dynamic heap allocation).
- **Exact HTML Specification Image Extraction:** The Elixir batch compiler (`scripts/html2tad.exs`) extracts exact diagram paths (`<IMG src="gif/*.gif">` and `<IMG src="img/*.png">`) directly from the original HTML specification documents, inspects their native dimensions (`w` and `h` in GIF87a/GIF89a and PNG IHDR headers), and copies all asset directories into `tad_bin/**/`.
- **Fallback Vector Schematics:** For synthetic documents without on-disk image assets, the browser falls back gracefully to topic-matched vector schematics.

## 8. Compatibility Matrix

| Feature | Legacy B-right/V Cho-Kanji (超漢字) | B-System Cleanroom OS | Status |
|:---|:---:|:---:|:---:|
| **Record Header (Type 1)** | `0x0001` (16-bit) | `0x0001` (16-bit) | **100% Binary Compatible** |
| **Virtual Body Fusen** | `TS_VOBJ` (`0xFFA8`) | `TS_VOBJ` (`0xFFA8`) | **100% Binary Compatible** |
| **Figure / Picture Fusen** | `TS_FPRIM` (`0xFFB0`) | `TS_FPRIM` (`0xFFB0` SubID 10) | **100% Binary Compatible** |
| **Font & Character Fusen** | `TS_TFONT` / `TS_TCHAR` | `TS_TFONT` / `TS_TCHAR` | **100% Binary Compatible** |
| **In-Place Navigation Stack** | Rudimentary window spawns | Multi-level history + Top Toolbar | **Superior Usability** |
| **Ukrainian & Cyrillic Engine** | Limited JIS extensions | True UTF-8 & TRON Plane 1 Engine | **Full Multilingual Support** |
| **Memory Allocation Model** | Unmanaged dynamic heap | Static bounded scope (NASA JPL Rule 3) | **Superior Safety & Predictability** |
| **Multilingual Character Code**| TRON Code Planes 0..3 | TRON Code + UTF-8 Dual Engine | **Modern Unicode Interop** |
| **Window Corner Resize** | BTRON 3.20 Resize Grip | Default 16x16 Corner Hatch Grip | **100% Spec Conformance** |
| **Platform Target** | Bare-metal PC-AT (x86) | POSIX, QEMU ARM/AArch64, Pi 2B/3B/4B | **Universal Deployment** |

# Credits

* Namdak Tonpa and Grok 4.5
