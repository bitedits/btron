# Bopomofo (McBopomofo) Integration into BTRON / B-System

## Overview

Analysis of the feasibility of including https://github.com/openvanilla/McBopomofo (and its fcitx5-mcbopomofo C++ port) into https://github.com/bitedits/btron as **minimally and fully as possible**.

BTRON already provides a clean TIP (Text Input Primitives) subsystem and a lightweight Mozc-inspired Japanese KKC engine. Adding Traditional Chinese Bopomofo (Zhuyin) support is a natural extension of the same architecture.

## Project Contexts

### McBopomofo
- Mature Traditional Chinese Bopomofo IME.
- Core algorithm: **Gramambular** — unigram language model + ReadingGrid (DAG of spans/nodes). Highest-scoring path found via topological sort + relaxation (maximum-likelihood walk).
- Data-driven (BPMF mappings, phrases, frequencies, heterophony lists).
- macOS version: mostly Swift + C++ engine.
- fcitx5 port: pure C++ (~1.15 M characters of C++).
- License: MIT.

### BTRON / B-System
- Cleanroom C99 implementation of BTRON3 SPEC 3.20.
- Extremely minimalist, VirtIO-focused.
- Existing TIP subsystem with DFA states, clauses, candidates, key settings.
- Lightweight Mozc-inspired Japanese engine (`mozc_engine.h`) using trie + lattice / Viterbi-style search.
- TRON Code multi-plane character system.
- User dictionaries stored as Real Objects.
- Philosophy: pure C99, fixed/arena allocation preferred, tiny footprint.

## Feasibility

| Aspect                          | Difficulty   | Notes |
|--------------------------------|--------------|-------|
| Core algorithm (Gramambular)   | Low–Medium   | Directly analogous to existing Mozc lattice. Reimplementable in pure C99 with fixed-size arrays / static pools. |
| Dictionary / LM                | Medium       | Convert to compact binary trie or sorted array + binary search. Avoid heavy memory-mapping, ICU, fmt, json-c. |
| Keyboard layouts               | Low          | Standard + Eten are simple tables. Hsu / 26-key also feasible. |
| TIP integration                | Low          | Extend `TIP_INPUT_MODE`, reuse `TIP_CONTEXT`, `TIP_CLAUSE`, `TIP_CANDIDATE`. |
| Character encoding             | Medium       | Map to/from TRON Code planes (or Unicode fallback). |
| User dictionary                | Low          | Mirror existing Mozc Real Object load/save pattern. |
| Advanced features              | High         | Associated phrases, macros, heterophony priorities, Braille, Chinese numbers, etc. |
| Dependencies / coding style    | High (full)  | McBopomofo C++ uses modern features and external libs. BTRON demands pure C99 and minimal footprint. |

**License compatibility**: Excellent (MIT ↔ ISC).

**Conclusion**: High feasibility for a minimal viable integration. Moderate for a fuller feature set. A wholesale inclusion of the McBopomofo tree is neither practical nor desirable.

## Acceptance Criteria

### Minimal (MVP — Primary Target)

1. New input mode: `TIP_MODE_BOPOMOFO` (or `TIP_MODE_ZH_TW`).
2. Standard Bopomofo keyboard layout (preferably also Eten).
3. Reading buffer → unigram lookup → candidate list (single characters + short phrases).
4. Lattice / DAG walk (or simplified Viterbi-style search) to select the highest-scoring path.
5. Candidate selection (numeric keys / arrows) and commit into the composition buffer.
6. Basic user-phrase registration and persistence as a Real Object.
7. Output via existing TRON Code / UTF-8 paths.
8. Clean integration with the existing TIP DFA states and key-settings schema.
9. Pure C99. No external libraries. Fixed-size or arena allocation only.
10. Phrase / mapping data compiled into a compact binary form (linkable or loadable as a Real Object).

### Full (Upper Aspiration)

Everything in the MVP, plus:
- Multiple keyboard layouts (Standard, Eten, Hsu, Eten-26, etc.).
- Associated phrases.
- User exclude list.
- Simple macros (date/time etc.).
- Heterophony priority handling.
- Plain Bopomofo mode (one character at a time).

Still excluded: Swift/ObjC layers, fcitx5 UI, ICU, large dynamic allocations, full dictionary curation pipeline, Braille, complex number converters.

## CLOC Upper Bound (Strict)

BTRON’s philosophy demands extreme minimalism. The existing Mozc engine is the size reference.

- **Hard upper bound** for new Bopomofo engine + TIP glue + keyboard tables: **≤ 4 000 lines of C**.
- Preferred target for a solid MVP: **1 500 – 2 500 LOC**.
- Full feature set should still stay **≤ 6 000 – 7 000 LOC** total new C.
- Data files (phrase DB, mappings) are **not** counted in the CLOC budget. They must be compact binary (ideally < 1–2 MB total, preferably much smaller via pruning or on-demand Real Object loading).

This bound forces a true cleanroom-style reimplementation rather than a mechanical translation of the C++ codebase.

## Recommended Implementation Strategy

1. Reimplement only the Gramambular core (ReadingGrid + unigram LM + walk) in pure C99, modelled on the existing `MozcLatticeNode` / trie style.
2. Convert the essential McBopomofo data (`BPMFBase`, `BPMFMappings`, high-frequency phrases, occurrence scores) offline into a BTRON-friendly binary format.
3. Add a thin TIP front-end: key mapping → reading buffer → lattice search → candidates.
4. Keep advanced features behind compile-time or runtime flags, or as separate Real Objects.
5. Document the conceptual mapping between McBopomofo and BTRON TIP / TRON Code.

## Bottom Line

A high-quality, usable Bopomofo IME that feels native to BTRON is realistic and valuable within a few thousand lines of careful C.

The correct goal is:

> **“McBopomofo-inspired core engine inside BTRON’s TIP”**

not

> “ship the whole McBopomofo tree”.

## Credits

