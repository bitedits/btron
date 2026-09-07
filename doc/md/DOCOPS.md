# B-System OS Documentation Specification for Automatic Generation

This document defines the technical documentation specification and structural requirements for system components in `./b-system/apps/` (corresponding to `./src/apps/`) and `./b-system/settings/` (corresponding to `./src/settings/`).

## 1. Documentation Architecture & Tone

Documentation across B-System must maintain a cold, precise, and academic technical standard. Marketing announcements, promotional prose, and emotional hyperbole are prohibited.

```
┌────────────────────────────────────────────────────────────────────────┐
│                   Dual-Audience Technical Specification                │
│                                                                        │
│   [ Functional User Specification ]  ◄── Strict ──►  [ C99 Interface ] │
│   • Operational procedures                           • Struct & enums  │
│   • UI interaction semantics                         • Header mappings │
│   • Visual parameter scopes                          • Build overrides │
└────────────────────────────────────────────────────────────────────────┘
```

### 1.1 Scope and Audience Definition

- **Functional Specification (User Operations)**: Rigorous and unambiguous descriptions of system behavior, window semantics, coordinate transformations, and user-facing parameters.
- **Systems Architecture (C99 Interface)**: Precise mapping of every UI widget and configuration state to underlying C99 data structures, enumeration identifiers, header definitions, and system calls.

### 1.2 Language Hierarchy

- **Primary Language**: Japanese technical standard (JIS/TRON technical prose style).
- **Secondary Language**: English technical summary, maintained at approximately **40% volume** relative to the Japanese text.

### 1.3 The 40% English Academic Summary Rule

- **Volume Ratio**: English text comprises approximately 40% of the total text volume.
- **Tone and Style**: Concise, formal, and academic English technical prose. Focus strictly on architectural properties, state transitions, mathematical geometry, and systems design principles.
- **Placement**: Located in the section introductory block directly beneath each `<h2>` header (paired with the Japanese overview sentence), as well as in the hero subtitle and technical abstract.

### 1.4 Component Naming & Title Conventions

- **Single-Word English Rule**: The English name/identifier must **always be exactly one single word** across `<title>`, `<h1>` headers, breadcrumbs, and navigation matrices (e.g., `Appearance`, `Desktop`, `Display`, `Input`, `Workbench`, `Cabinet`, `Terminal`, `Browser`, `Editor`, `Commander`, `Cassette`). Multi-word descriptors (e.g., *Appearance Subsystem*, *Media Deck*, *GTerm*, *Desk Calculator*) are strictly prohibited in titles and headers.
- **Concise Japanese Specifier (No Nested Braces)**: The Japanese title must be simple, concise, and **strictly without nested parentheses or braces** (e.g., `外観設定 (Appearance)`, `コマンダー (Commander)`, `カセット (Cassette)`, `端末 (Terminal)`, `電卓 (Calculator)`). Triple-tagging or descriptive parentheticals like `コマンダー (二面ファイル管理) (Commander)`, `カセット (Media Deck) (Cassette)`, or `ターミナル (GTerm) (Terminal)` are prohibited.

## 2. Structural & Layout Requirements

### 2.1 Tabular Parameter Documentation (`.market-table`)

- Parameter groups (Themes, Geometry, Menu Models, Glyph Planes, Subsystems) must be documented in structured HTML tables (`<table class="market-table">`).

- **Table Columns**:
  1. **パラメータ・設定項目**: Parameter identifier, widget type, and bilingual title.
  2. **機能仕様・操作説明 (Functional Specification)**: Objective description of runtime behavior and visual impact.
  3. **技術仕様・C99定義・幾何学 (Technical & C99 Specification)**: Type definitions, constants, memory fields, pixel coordinates, and API signatures.

### 2.2 Mandatory Configuration Parameter Specification Table

Every settings page must include an exhaustive **Configuration Parameter Specification Table (全設定パラメータ仕様一覧)** indexing 100% of the applet's state variables:

| Column Header | Description | Example |
| :--- | :--- | :--- |
| **セクション (Section)** | Subsystem or parameter group | `[2] アイコン・メニュー` |
| **パラメータ名 / UI要素** | UI widget type and label | `Icon Display Size (Radio)` |
| **標準値 (Default)** | Reset / factory default value | `64×64 (BTRON_ICON_SIZE_64)` |
| **設定値 / 選択肢** | Permissible state values | `32×32 / 64×64` |
| **C99 内部変数・シンボル** | C variable, struct field, or API | `appearance_set_icon_size()` |

## 3. C99 Implementation Parity Requirements

Documentation must maintain complete parity with the C99 implementation in `./src/apps/` and `./src/settings/`.

### 3.1 Settings Parity Requirements (`./b-system/settings/`)
- **Visual Palettes**: `COLOR_TEAL` (`#008080`), `COLOR_DKNAVY` (`#0A192F`), `COLOR_DKAMBER` (`#1A1000`), and `COLOR_LTGRAY` bezel chrome.
- **Icon Metrics**: `BTRON_ICON_SIZE_32` (32×32) and `BTRON_ICON_SIZE_64` (64×64); grid spacing (Cabinet 96×72px vs. 110×104px; Control Panel 520px vs. 660px); fixed 32×32 window header policy (`draw_setting_gif_icon_scaled`).
- **Menu Architecture**: `APP_MENU_STYLE_CLASSIC_3D` (4-sided beveled box) and `APP_MENU_STYLE_MODERN_CARD` (1px boundary with 3px drop shadow); propagation across in-window menus, Global Menu Bar (`global_menu.c`), and Deskbar Start Menu (`tracker.c`).
- **Typography Engine**: 16-plane TRON code rasterizer, Classical Tibetan Jomolhari Unicode plane with EWTS stack subjoining, and subpixel anti-aliasing.
- **State Control**: Lifecycle handling for Default, Apply (`is_dirty` tracking and `redraw_all_windows()`), and Close (`cls_wnd`).

### 3.2 Application Parity Requirements (`./b-system/apps/`)
- **Data Model**: Real Object (実身) and Virtual Object (仮身) network traversal without modal file-picker dialogs.
- **Input Method Processor**: TIP input engine state transitions, F6–F10 functional transliteration keys, and pre-edit buffer handling.

## 4. Standard Document Blueprint

Every document in `./b-system/` must conform to the following markup structure:

```html
<!DOCTYPE html>
<html lang="ja">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>[Identifier] – [Japanese Title] | B-System Specification</title>
  <meta name="description" content="[Academic technical summary]">
  <link rel="stylesheet" href="../../index.css">
  <link rel="stylesheet" href="./settings.css">
  <link rel="stylesheet" href="../apps/apps.css">
</head>
<body>
  <nav class="setting-nav">
    <a href="../../b-system.html">B-System</a>
    <span class="sep">/</span>
    <a href="../../b-system.html#[section]">Settings</a>
    <span class="sep">/</span>
    <span class="current">[Component Name]</span>
  </nav>

  <div class="setting-hero">
    <div class="setting-hero-icon"><img src="[OpenMoji SVG URL]" alt="Icon" width="64" height="64"></div>
    <h1>[Japanese Title] ([English Title])</h1>
    <div class="setting-hero-badge">[Formal Subsystem Badges]</div>
    <div class="subtitle">[Bilingual Academic Subtitle]</div>
  </div>

  <div class="setting-content">
    <a href="../../b-system.html#[section]" class="back-link">← [Section Index]</a>

    <div class="tagline-card">
      <strong>[Component Name]</strong> — [Bilingual Technical Abstract]
    </div>

    <div class="section">
      <h2>概要 (Overview)</h2>
      <p>
        [Japanese Overview Text]<br>
        <span style="font-size:0.9em; opacity:0.85;">"[40% English Academic Summary]"</span>
      </p>
      <div class="setting-desc-jp">[Formal Operations / Event Dispatch]</div>
    </div>

    <div class="section">
      <h2>1. [Parameter Subsystem 1]</h2>
      <p>
        [Japanese Subsystem Intro]<br>
        <span style="font-size:0.9em; opacity:0.85;">"[40% English Technical Synthesis]"</span>
      </p>
      <table class="market-table">
        <thead>
          <tr>
            <th>パラメータ・設定項目</th>
            <th>機能仕様・操作説明 (Functional Specification)</th>
            <th>技術仕様・C99定義・幾何学 (Technical &amp; C99 Definition)</th>
          </tr>
        </thead>
        <tbody>
          <!-- Technical specifications without promotional language -->
        </tbody>
      </table>
    </div>

    <div class="section">
      <h2>[N]. 全設定パラメータ仕様一覧 (Configuration Parameter Specifications)</h2>
      <table class="market-table">
        <!-- Exhaustive Parameter Matrix -->
      </table>
    </div>

    <div class="section">
      <h2>関連コンポーネント (Related Components)</h2>
      <div class="related-apps">
        <!-- Navigation References -->
      </div>
    </div>
  </div>

  <footer class="app-footer">
    <!-- Standard Grid Footer -->
  </footer>
</body>
</html>
```

## 5. Verification Pipeline

All documentation pages must pass the native build and test harness:

```bash
# 1. Compile HTML to Native TAD binary files
elixir scripts/html2tad.exs

# 2. Execute full regression test suite
make test
```

# Credits

* Namdak Tonpa and Grok 4.5
