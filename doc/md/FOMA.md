# µBTRON-FOMA — Mouse-free BTRON for FOMA Devices

**Concept document based on the B-System / bitedits/btron cleanroom recreation of BTRON3 3.20**
Design language taken from the authentic Sakamura Cho-Kanji screenshots in `./b-system/img/`.

## 1. Core Concept

A compact, real-time, hypermedia-oriented personal computing environment derived from the BTRON3 / micro-BTRON lineage, stripped of any reliance on a free-roaming pointer and redesigned for the constraints and interaction model of NTT DoCoMo FOMA keitai (feature phones / early 3G smartphones of the 2001–2010s era).

### Preserved BTRON Principles

- **Real Body / Virtual Body (実身・仮身) model** — fundamental data abstraction. Documents, applications and media are Real Bodies stored in hierarchical Cabinets. On-screen representations and cross-links are Virtual Bodies. Everything is a typed, embeddable part that can contain other parts.
- **TAD (TRON Application Databus)** as the universal interchange and storage format.
- **TRON Code** multi-plane character system (with UTF-8 bridge) for full Japanese + multilingual support.
- **µITRON** real-time kernel foundation for responsive behaviour under severe resource limits.
- **EnableWare** philosophy — accessibility and consistent operation are first-class.
- Standardized, predictable HMI that does not require learning a new interface per application.

### Removed / Radically Changed

- No free-floating mouse cursor, no relative pointing device, no “grab-and-poi” drag-and-drop.
- No assumption of a full keyboard or large pointing surface.
- Window system is no longer a free-floating multi-window desktop; it becomes a constrained, focus-driven, hierarchical navigation surface suited to 2–3 inch screens and 12–20 key input.

### Input Model (purely button / keypad driven)

Primary controls (mapping to typical FOMA hardware):

- 4-way / 5-way directional pad (or equivalent rocker) — primary navigation.
- Center / Select / OK key.
- Soft keys (usually 2–4, left/right under the screen) — context-sensitive actions.
- Numeric keypad (0–9, *, #) — both for text entry and as accelerator shortcuts.
- Clear / Back / Menu dedicated keys.
- Optional long-press and multi-press modifiers.

**Navigation paradigm**

- Focus rectangle / highlight bar moves among selectable Virtual Bodies, menu items, text runs and control widgets.
- Directional pad moves focus; Select activates / opens / executes.
- Soft keys change meaning according to current focus and mode.
- Hierarchical “Cabinet → Real Body → Virtual Body links” navigation replaces free mouse exploration.
- “Drag” operations become explicit “Select source → Menu → Move/Copy/Link → Select destination”.
- Text selection uses start-mark / end-mark with directional movement.

This is a direct evolution of the one-button PD + menu-button adaptations already present in the historical micro-BTRON / B-right specification for PDAs.

### Screen & Window Model

- Single primary viewport (or at most two stacked layers) sized for typical FOMA resolutions (QVGA, HVGA, early WVGA).
- Top status bar (carrier, battery, time, signal) + optional soft-key labels at bottom.
- “Windows” become full-screen or nearly full-screen pages with a clear title and focus ring.
- Modal dialogs and pop-up menus are list- or grid-based and fully keypad-navigable.
- Desktop is replaced by a persistent **Home Cabinet** (or launcher Real Body).
- Z-order and free window placement disappear; navigation is stack- or tree-based.

### System Services & Applications (minimal FOMA-friendly set)

- Cabinet / Real-Body manager (hierarchical browser with focus navigation).
- T-Editor (text + embedded Virtual Bodies) optimized for numeric-key Japanese input + optional predictive / TRON Code entry.
- Simple graphics / sketch tool that works with directional drawing + soft-key tools.
- Terminal / console (for power users / debugging).
- i-mode / browser-like TAD viewer (or lightweight HTML→TAD converter path).
- Contact / schedule / memo Real Bodies that interlink.
- Power-management and suspend/resume hooks native to µITRON + FOMA hardware.
- Optional XMPP or simple messaging layer.

### Hardware Target Profile

- ARM / early RISC processors typical of FOMA handsets (or modern re-implementations on similar constrained SoCs).
- 2–16 MB RAM class, flash storage, small colour LCD, numeric keypad + directional controls, optional camera / IrDA / early Bluetooth.
- Battery life and instant-on behaviour are primary design goals.

### Relationship to Existing Work

Natural “FOMA profile” of the micro-BTRON / B-right line (BrainPad TiPO era) and the modern cleanroom B-System (bitedits/btron). Keeps the real/virtual object engine, TAD, TRON Code and µITRON core while discarding the mouse-centric HMI assumptions.

**Result:** BTRON’s document-centric, hyperlinked, multilingual world, delivered through the interaction language of a classic Japanese keitai.

## 2. Design Language (from `./b-system/img/` screenshots)

- Background: classic BTRON teal/purple with fine dotted texture.
- Windows: double-line borders, solid blue title bars with white text, close “×”.
- Focus: high-contrast inverted or thick rectangular highlight (for directional-pad navigation).
- Soft-key labels always visible at bottom.
- Typography: Cho-Kanji Gothic / TRON Code style.
- Icons: simple monochrome or flat colored rectangles (as in the left tool strip of the existing screenshots).
- No free mouse cursor — only focus ring + soft keys + 5-way pad.
- Japanese + English mixed labels, [仮身] hyperlink notation, list-based Cabinet explorers, status bars with TAD SPEC info.

Left vertical tool strip (always present in Workbench-style screens):
実身 / 実身・仮身 / 文書 / 基本エディタ / 端末 / 音響

## 3. Prototype Screens

### 3.1 Workbench / Home Cabinet (起動キャビネット)

```
┌─────────────────────────────────────┐
│ B-TRON FOMA          22:14  ████    │  ← status (time, battery, signal)
├─────────────────────────────────────┤
│  実身キャビネット / Workbench       │  ← title bar
├─────────────────────────────────────┤
│ ▶ 連絡先 (Contacts)          48     │  ← focus highlight
│   メモ (Memos)               23     │
│   予定 (Schedule)            12     │
│   文書 (Documents)           17     │
│   アプリ (Apps)               9     │
│   設定 (Control Panel)              │
│   端末情報 (Device Info)            │
├─────────────────────────────────────┤
│ [選択]          [メニュー]   [戻る] │  ← soft keys
└─────────────────────────────────────┘
```

### 3.2 Main Menu (システムメニュー)

```
┌─────────────────────────────────────┐
│ メニュー / System Menu              │
├─────────────────────────────────────┤
│ ▶ 新規実身 (New Real Body)          │
│   開く (Open)                       │
│   検索 (Search)                     │
│   仮身リンク作成 (Create V-Body)    │
│   コピー / 移動                     │
│   削除                              │
│   ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─     │
│   コントロールパネル                │
│   電源管理 (Power)                  │
│   終了 / サスペンド                 │
├─────────────────────────────────────┤
│ [決定]          [キャンセル]        │
└─────────────────────────────────────┘
```

### 3.3 Control Panel (コントロールパネル)

```
┌─────────────────────────────────────┐
│ コントロールパネル                  │
├─────────────────────────────────────┤
│ ▶ 表示設定 (Display)                │
│   入力設定 (Input / TIP / MOZC)     │
│   電源・スリープ                    │
│   音量・バイブ                      │
│   ネットワーク (FOMA / i-mode)      │
│   日時・時計                        │
│   言語・TRON Code 平面              │
│   アクセシビリティ (EnableWare)     │
│   バージョン情報                    │
├─────────────────────────────────────┤
│ [選択]          [戻る]              │
└─────────────────────────────────────┘
```

### 3.4 Apps Launcher

```
┌─────────────────────────────────────┐
│ アプリ / Applications               │
├─────────────────────────────────────┤
│ ▶ T-Editor (テキスト)               │
│   gterm (端末)                      │
│   TAD Browser                       │
│   簡易ペイント                      │
│   XMPP Chat (30KB)                  │
│   Cabinet Explorer                  │
│   計算機                            │
│   カメラ (if hardware)              │
├─────────────────────────────────────┤
│ [起動]          [情報]      [戻る]  │
└─────────────────────────────────────┘
```

### 3.5 Contacts (連絡先キャビネット)

```
┌─────────────────────────────────────┐
│ 連絡先 / Contacts          48 items │
├─────────────────────────────────────┤
│ ▶ あいうえおグループ                │
│   かきくけこグループ                │
│   さしすせそグループ                │
│   ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─     │
│   坂村 健         090-XXXX-XXXX     │
│   小島 秀樹       03-XXXX-XXXX      │
│   Namdak Tonpa    xmpp:…            │
├─────────────────────────────────────┤
│ [詳細]  [発信]  [新規]      [戻る]  │
└─────────────────────────────────────┘
```

Detail view shows Real Body with embedded Virtual Body links (phone, mail, memo, schedule).

### 3.6 Memos (メモ)

```
┌─────────────────────────────────────┐
│ メモ / Memos               23 items │
├─────────────────────────────────────┤
│ ▶ 2026-09-05  BTRON FOMA設計        │
│   2026-09-04  仮身リンク実験        │
│   2026-09-03  TAD変換メモ           │
│   ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─     │
│   [仮身] 連絡先:坂村健              │
│   [仮身] 予定:明日の会議            │
├─────────────────────────────────────┤
│ [開く]  [新規]  [検索]      [戻る]  │
└─────────────────────────────────────┘
```

Editor inherits the T-Editor look from the original B-System screenshots (line numbers, [仮身] embeds, status bar with TAD SPEC info), but full-screen and keypad-driven.

### Interaction Rules (FOMA-native)

- 5-way pad moves the focus highlight.
- Center / Select activates.
- Soft keys change meaning contextually (always shown).
- Long-press numeric keys or dedicated Menu key opens the system menu.
- “Back” always returns one level in the Real/Virtual Body hierarchy.
- No free-floating windows — every screen is essentially a full-viewport Real Body view or list.

## 4. TIP IME Japanese Input on Telephonic Keyboards

In the B-System / µBTRON-FOMA design, **TIP** (the Japanese input front-end) is tightly integrated with a cleanroom **Mozc** engine. On a classic FOMA-style 12-key telephonic keypad there is no full QWERTY or TRON keyboard, so the system uses the traditional Japanese *keitai* input methods, adapted to the Real Body / Virtual Body model and TRON Code.

### 4.1 Hardware Mapping (Standard FOMA 12-key layout)

| Key | Hiragana cycle (toggle)          | Notes |
|-----|----------------------------------|-------|
| 1   | あいうえお ぁぃぅぇぉ            |       |
| 2   | かきくけこ がぎぐげご            |       |
| 3   | さしすせそ ざじずぜぞ            |       |
| 4   | たちつてと だぢづでど っ         |       |
| 5   | なにぬねの                       |       |
| 6   | はひふへほ ばびぶべぼ ぱぴぷぺぽ |       |
| 7   | まみむめも                       |       |
| 8   | やゆよ ゃゅょ                    |       |
| 9   | らりるれろ                       |       |
| 0   | わをん ー                        |       |
| *   | ゛ ゜ 小 (voiced / semi-voiced / small) | Modifier |
| #   | Mode switch / symbols            |       |

Soft keys and the 5-way pad handle Confirm, Cancel, candidate selection and navigation.

### 4.2 Input Modes Supported by TIP on FOMA

- **Toggle / Multi-tap (ケータイ入力)** — default and most FOMA-native.
  Press the same key repeatedly to cycle through the characters assigned to it.
  Example: “く” = press **2** three times (か → き → く).
  A short timeout or pressing another key commits the current character.

- **2-touch / Pager style (ポケベル入力)**
  First digit = row (1–0), second digit = position in the row.
  Faster for experienced users (always exactly two presses per kana).

- **Flick** (if the device has a touch overlay or later FOMA models)
  Press and slide in one of four directions from the base key.

- **T9-style predictive single-tap** (optional)
  One press per key; Mozc’s statistical model immediately offers the most likely reading + kanji candidates.

Mode switching is done with the `#` key, a dedicated soft-key, or a long-press.

### 4.3 Conversion Pipeline (TIP → Mozc → TRON Code)

1. **Kana composition buffer**
   User produces a string of hiragana (or katakana).
   The buffer is shown underlined or highlighted in the T-Editor / memo field.

2. **Mozc statistical conversion**
   TIP feeds the reading to the Mozc engine.
   Mozc returns ranked candidates (single-word or phrase kanji, mixed kana/kanji, katakana, numbers, symbols, etc.).

3. **Candidate selection**
   - Up/Down on the 5-way pad or soft keys cycles through candidates.
   - Center / Select / soft-key “変換” commits the chosen candidate.
   - “無変換” or a dedicated key leaves the reading as-is.

4. **Output as TRON Code**
   The final character(s) are inserted into the Real Body as native TRON Code (multi-plane).
   TIP also supports a direct “TRON Code entry” mode for rare characters.

### 4.4 Integration with µBTRON-FOMA UI

- Composition window and candidate list are full-screen or bottom-panel overlays that follow the same double-border / blue-title / focus-highlight design language.
- Soft-key labels change dynamically: `[変換]` `[候補]` `[確定]` `[取消]` etc.
- Because everything is a Real Body / Virtual Body, a memo or contact field can contain live links while the user is still composing.
- Power and memory constraints are respected: Mozc’s dictionary can be kept in a compact form suitable for FOMA-class RAM.

### 4.5 Practical Example

User wants to type **電話** (でんわ):

1. Toggle: **4** (た) → **4** (ち) → **4** (つ) → **4** (て) → timeout/commit → **で** (add dakuten with `*`)
2. **0** twice → **ん**
3. **0** once → **わ**
4. Press Convert soft-key → Mozc offers **電話**, **電和**, **でんわ** …
5. Select first candidate → **電話** is inserted as TRON Code into the current Real Body.

The same pipeline works for Contacts names, Memo bodies, Schedule titles, TAD documents, etc.

## 5. Summary

µBTRON-FOMA is a pure keypad + focus-driven hypermedia environment that would have felt native on a 2000s
FOMA handset while remaining philosophically pure BTRON. It keeps the exact visual DNA of the existing
B-System screenshots while making every screen fully operable with only a directional pad + soft keys — the classic FOMA interaction language.

All design, interaction and input details above are derived from:

- The BTRON3 / micro-BTRON / B-right historical lineage
- The modern cleanroom B-System implementation (bitedits/btron)
- Authentic Sakamura Cho-Kanji visual language from `./b-system/img/`
- Classic Japanese keitai (FOMA) input conventions

# Credits

* Namdak Tonpa and Grok 4.5
