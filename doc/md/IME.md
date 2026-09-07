B-System TIP
============

Requirement Specification: Character Encoding and Japanese Input Method for B-TRON
Document purpose: This specification defines the mandatory requirements for character
encoding and Japanese text input in a cleanroom B-TRON implementation. It ensures
historical fidelity to the TRON Code model while achieving practical interoperability
with modern systems.

TOC:

* [1]. Character Encoding Model
* [2]. Japanese Input Method (IME)
* [3]. Application Integration
* [4]. Compatibility and Extensibility
* [5]. Visual Identity & User Experience
* [6]. Summary of Design Intent
* [7]. IME Windows

## 1. Character Encoding Model

### REQ-1.1 Hybrid Representation

The system shall use a hybrid character model:

* Internal native representation: TRON Code (multi-plane), extended with a Unicode mapping plane.
* External interchange format: UTF-8.

### REQ-1.2 Conversion Functions

The system shall provide bidirectional, lossless conversion for the common Japanese character repertoire:

* UTF-8 → TRON Code
* TRON Code → UTF-8

Conversion shall correctly handle ASCII, Hiragana, Katakana, JIS X 0208, and frequently used Unicode CJK characters.
Unsupported or rare characters shall map to a defined fallback or private plane without data loss where possible.

### REQ-1.3 Storage Rules

* TAD documents and internal text objects shall prefer TRON Code.
* Plain-text files, clipboard data, and host-OS interchange shall use UTF-8 by default.
* Full-width / half-width distinction shall be handled via TAD formatting attributes (fusen), not by distinct code points.

### REQ-1.4 Glyph Rendering

* The font engine shall support:Classic bitmap glyphs (8×16 / 16×16) for retro compatibility.
* Modern scalable fonts (via FreeType or equivalent) for Unicode coverage.
* Optional loading of extended Japanese administrative character sets.

## 2. Japanese Input Method (IME)

### REQ-2.1 Architecture

The system shall implement a separated IME architecture consistent with original B-TRON design:

* Input front-end (composition UI and candidate display)
* Conversion engine (Romaji/Kana → Kanji)

### REQ-2.2 Supported Input Modes

The IME shall support at minimum:

* Romaji input (primary mode for modern users)
* Direct Kana input

### REQ-2.3 Conversion Engine

The conversion engine shall provide accurate kana-kanji conversion.
Integration of Mozc (or an equivalent high-quality open engine) is the preferred implementation path.
A minimal dictionary-based engine is acceptable as an interim solution.

### REQ-2.4 Composition Handling

The system shall:

* Maintain an active composition state.
* Display composition feedback (underline or equivalent B-TRON visual style).
* Present conversion candidates in a dedicated window or panel.
* Support standard confirmation, cancellation, and candidate selection actions.
* Integrate cleanly with the existing B-TRON event queue.

### REQ-2.5 User Dictionary

The system shall support a persistent user dictionary stored as a Real Body.

## 3. Application Integration

### REQ-3.1 Text Components

The text editor (t_editor) and terminal (gterm) shall accept both UTF-8 and TRON Code input streams
and render them correctly.

### REQ-3.2 Event Flow

Keyboard events shall be routed through the IME front-end when Japanese input mode is active.
Confirmed text shall be inserted into the target application as TRON Code or UTF-8 according to
the storage rules in REQ-1.3.

## 4. Compatibility and Extensibility

### REQ-4.1 Round-trip Safety

Common Japanese text shall survive UTF-8 <-> TRON Code conversion without corruption.

### REQ-4.2 Extensibility

The design shall allow future addition of:

* Ideographic Variation Sequences (IVS)
* Additional TRON Code planes for rare/historical characters
* TRON keyboard layout support

### REQ-4.3 Host Integration

When running under a host operating system, the implementation may initially leverage
the host IME and convert the resulting UTF-8 text into the internal representation.
A native B-System IME remains the target architecture.
Note that No binary compatibility with legacy B-right is required or planned.

## 5. Visual Identity & User Experience

To avoid the “invisible upgrade” problem observed when Mozc was ported to commercial B-right/V,
the modern conversion engine shall be made lightly visible:

* Small persistent engine indicator (e.g. “Mozc”) in the composition or candidate window.
* Distinct but still B-TRON-styled candidate annotations or list appearance.
* Optional status information (engine name / dictionary version).
* Clear first-run or settings indication that a modern open-source conversion engine is active.

The classic double-bordered Sakamura window style and overall interaction model shall be preserved.

## 6. Summary of Design Intent

B-System IME keeps the original B-TRON character-set philosophy (multi-plane TRON Code,
Real/Virtual Body model, separated front-end/conversion architecture) while adopting
modern libraries and practices (UTF-8, Mozc, FreeType, SDL2).

The design deliberately rejects legacy binary constraints and the complete visual invisibility
of engine changes, ensuring both historical fidelity and a satisfying contemporary user experience.

## 7. IME Windows

Here are clean wireframe concepts for the IME UI, designed in the classic B-TRON style (double-bordered windows,
simple title bars, minimal chrome, clear hierarchy).

### 7.1. Composition Window (Inline / Floating)

```
┌══════════════════════════════════════════════════════┐
║  Composition                                      [×]║
╠══════════════════════════════════════════════════════╣
║                                                      ║
║   わたしのなまえは  中野です                         ║
║   ‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾                          ║
║   (reading / converted clauses with underline)       ║
║                                                      ║
║   Mode: あ  Romaji → Kana → Kanji                    ║
╚══════════════════════════════════════════════════════╝
```

Notes:

* Double outer border (classic B-TRON).
* Underline (or dotted line) under the active composition string.
* Optional small mode indicator (あ / A / ア).
* Can appear as a floating window or as an overlay directly above the caret in the text editor.

### 7.2. Candidate Window (Selection List)

```
┌══════════════════════════════┐
║  Candidates               [×]║
╠══════════════════════════════╣
║ 1  中野                      ║
║ 2  仲野                      ║
║ 3  なかの                    ║
║ 4  ナカノ                    ║
║ 5  中埜                      ║
║──────────────────────────────║
║ ↑↓ Select   Enter Confirm    ║
║ Space Next page              ║
╚══════════════════════════════╝
```

Variant with annotations (recommended):

```
┌══════════════════════════════════┐
║  Candidates                   [×]║
╠══════════════════════════════════╣
║ 1  中野     (surname)            ║
║ 2  仲野     (surname)            ║
║ 3  なかの   (hiragana)           ║
║ 4  ナカノ   (katakana)           ║
║ 5  中埜     (rare)               ║
║──────────────────────────────────║
║ 1-9 Select   Esc Cancel          ║
╚══════════════════════════════════╝
```

### 7.3. Combined View (Typical Usage)

Text Editor Window:

```
┌══════════════════════════════════════════════════════┐
║  t_editor – Untitled                              [×]║
╠══════════════════════════════════════════════════════╣
║                                                      ║
║  今日は  [わたしのなまえは  中野です]  です。        ║
║             ‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾                ║
║                                                      ║
└══════════════════════════════════════════════════════┘
                          │
                          ▼
              ┌══════════════════════════════┐
              ║  Candidates               [×]║
              ╠══════════════════════════════╣
              ║ 1  中野                      ║
              ║ 2  仲野                      ║
              ║ 3  なかの                    ║
              ║ 4  ナカノ                    ║
              ║ 5  中埜                      ║
              ╚══════════════════════════════╝
```

## 8. Design Rules

* Strict double-line outer borders
* Single-line inner separators
* Minimal title bar with close button only
* Clear numeric selection (1–9) + keyboard hints at the bottom
* Compact vertical candidate list (classic Japanese IME style)
* Composition string always shows reading + current conversion state with underline

These wireframes can be implemented directly with the existing B-TRON window manager (wnd.h) primitives.
We need a more detailed pixel-level version or additional states (e.g. prediction list, error state, or vertical candidate layout).

## 9. TRON Language Plane Switching & Input Modality Architecture

### 9.1. Historical TRON Specification Architecture
In the original TRON and BTRON specifications (Sakamura Lab, TRON Association BTRON3 v3.20, and the TRON Keyboard Hardware standard):

1. **TRON Code Plane Partitioning**:
   - **Plane 0 (0x0021–0x007E)**: Direct ASCII / English Alphanumeric character set.
   - **Plane 1 (0x2121–0x7E7E)**: Japanese Kanji, Hiragana, Katakana, and JIS X 0208 character set.
   - Decoupling character encoding from rendering allows seamless modality switching without stream corruption.

2. **TRON Keyboard Hardware Mode Keys**:
   - **[英数] (Eisu / Alphanumeric)**: Hardware latch/switch directly activating **Plane 0 (Direct English)** input mode.
   - **[かな] (Kana) / [漢字] (Kanji)**: Hardware latch activating **Plane 1 (Japanese Kana-Kanji)** conversion front-end.
   - **[変換] (Henkan)**: Triggers morphological clause segmentation and Viterbi lattice search (Space key fallback).
   - **[無変換] (Muhenkan)**: Discards conversion candidates or cycles unconverted phonetic readings.

3. **Standard Function Key Mapping (TRON DP / JIS Standard)**:
   - **F10**: Primary Functional Key toggling between **Plane 0 (Direct English / ASCII)** and **Plane 1 (Japanese IME)**.
   - **F7**: Converts active composition buffer to **Fullwidth Katakana (全角カタカナ)**.
   - **F8**: Converts active composition buffer to **Halfwidth Katakana (半角カタカナ)**.
   - **F9**: Converts active composition buffer to **Fullwidth Alphanumeric (全角英数)**.
   - **F6**: Converts active composition buffer to **Hiragana (ひらがな)**.

4. **Modern Cross-Platform Shortcuts**:
   - **Ctrl + Space**, **Shift + Space**, **Alt + Space**: Instant IME mode toggle across macOS, Linux, and B-TRON.
   - **半角/全角 (Hankaku/Zenkaku)**: Hardware key toggle on standard Japanese PC keyboards.

### 9.2. Per-Window Input Focus & Modality Rules
1. **Active Window Exclusivity**:
   - Keystrokes (`EV_KEY_DOWN`) are routed strictly to the topmost focused window (`wnd->focused == TRUE`). Non-active windows shall never receive or process keystrokes.
2. **Focus-Change Composition Reset**:
   - Switching active window focus immediately cancels pending composition (`tip_cancel()`) to prevent leakage across applications.
3. **Application Modality Initialization**:
   - **Terminal Console (`gterm`)**: Starts strictly in **Direct English (`TIP_MODE_ASCII`)** mode upon launch and initialization.
   - **Text Editor (`t_editor`)**: Allows dynamic switching with status bar visual feedback (`[Mozc: A]` / `[Mozc: あ]`).

# Credits

* Namdak Tonpa


## 8. Multilingual Keyboard Schema & Settings Cabinet (EN / JP / TB)

### 8.1 Universal Keyboard Schema

The B-System TIP (Text Input Processor) provides a unified, deterministic keyboard schema across English (`[EN]`), Japanese (`[JP あ]` / `[JP ア]`), and Tibetan (`[TB བོད]`) input modes:

```
+---------------------------------------------------------------------------------------------------+
| Mode Switching:                                                                                   |
|   F10               : Cycle Input Mode: [EN] -> [JP あ] -> [JP ア] -> [TB བོད] -> [EN]            |
|   Ctrl+Space / Alt  : Toggle between Direct ASCII [EN] and active Asian Script mode                |
|   F6 / F7 / F8 / F9 : Direct Hiragana (F6) / Katakana (F7) / Tibetan (F8) / Fullwidth Alpha (F9)  |
+---------------------+----------------------------------+------------------------------------------+
| Script Mode         | Space Bar Key Action             | Tab / Shift+Space Action                 |
+---------------------+----------------------------------+------------------------------------------+
| Direct English [EN] | Standard ASCII space (0x20)      | Standard Tab indentation (\t)            |
| Japanese [JP あ/ア] | Viterbi KKC Kanji Conversion     | Candidate Suggestion Popup Window        |
| Tibetan [TB བོད]    | Tsheg Syllable Delimiter ('་')   | Dharma Dictionary Suggestion Popup       |
+---------------------+----------------------------------+------------------------------------------+
| Candidate Window    | Down / Ctrl+N : Next Candidate   | Up / Ctrl+P : Previous Candidate         |
| Navigation          | 1 - 9         : Direct Selection | Return / Space : Commit Candidate        |
| (All Modes)         | Esc           : Dismiss Popup    | Shift+Tab   : Previous Candidate         |
+---------------------------------------------------------------------------------------------------+
```

### 8.2 Ergonomic Rationale & Scriptio Continua

* **Japanese Scriptio Continua**: Standard Japanese sentences do not use spaces between words. The `Space` key is therefore dedicated to Kana-Kanji conversion (KKC). In IDLE state, pressing `Space` inserts a fullwidth Japanese space (`　` `U+3000`).
* **Tibetan Syllable Delimitation**: Every classical Tibetan syllable requires a terminal **Tsheg (`་` `U+0F0B`)**. Repurposing `Space` to emit a Tsheg allows continuous, natural typing of Tibetan text (`chos` + `Space` $\rightarrow$ `ཆོས་`), while `Tab` or `Shift+Space` accesses the rich Rangjung Yeshe / Mahāvyutpatti philosophical dictionary popup.

### 8.3 Settings Cabinet Application (`src/settings/language.c`)

The BTRON **Settings Cabinet (環境設定キャビネット)** provides an interactive GUI applet (`open_language_settings_window()`) for customizing IME keyboard shortcuts:

* **Mode Switching Configuration**: Customize default shortcut keys for cycling and direct language selection.
* **Japanese Mode Options**:
  - `jp_space_is_convert`: Toggle whether Space initiates Kanji conversion.
  - `jp_tab_is_popup`: Toggle whether Tab opens the candidate popup.
* **Tibetan Mode Options**:
  - `tb_space_is_tsheg`: Toggle whether Space inserts Tsheg (`་`) or raw ASCII space.
  - `tb_tab_is_popup`: Toggle whether Tab triggers dictionary lookups.
  - `tb_shift_space_popup`: Toggle whether Shift+Space opens dictionary popup.
* **Candidate Navigation Options**:
  - `arrow_nav_enabled`: Enable/disable Up/Down arrow list traversal.
  - `num_select_enabled`: Enable/disable 1–9 numeric candidate selection.
* **Control Actions**: `[Apply]` saves configuration to system preferences, `[Revert]` rolls back pending changes, and `[Default]` restores standard factory shortcuts.

# Credits

* Namdak Tonpa and Grok 4.5
