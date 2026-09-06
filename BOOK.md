# B-Book Developer Documentation Specification (BOOK.md)

This specification governs the structure, style, and formal presentation of **B-Book**, the definitive Developer's API and Systems Reference Manual for B-System (inspired by the classic BeOS/Haiku *BeBook*).

## 1. Documentation Principles & Linguistic Standard

### 1.1 Pure English Technical Standard

Unlike end-user or operational system documentation, **B-Book is written entirely in English**. It maintains a rigorous, academic, and unambiguous engineering tone. Marketing language, hyperbole, and speculative prose are strictly prohibited.

```
┌────────────────────────────────────────────────────────────────────────┐
│                        B-Book Documentation Model                      │
│                                                                        │
│   [ Architectural Model ] ◄── Strict Parity ──► [ C99 Formal APIs ]    │
│   • Subsystem state machines                    • Type definitions     │
│   • Memory & concurrency guarantees             • System call tables   │
│   • Coordinate & event systems                  • Invariant contracts  │
└────────────────────────────────────────────────────────────────────────┘
```

### 1.2 Target Audience

- System software engineers developing core services, daemons, and device drivers.
- Application developers building native B-System / Cho-Kanji graphical applications.
- Porting engineers targeting architectures supported by B-System (POSIX, Sakamura T-Kernel 2.0, FOMA Mobile ARMv6, x86_64 UEFI SMP, and NEC PC-98).

## 2. Directory Structure & Naming Conventions

B-Book is organized under the root directory `./b-book/` with twelve subsystem chapters:

```
./b-book/
├── b-book.css      # Unified B-Book responsive design system
├── index.html      # Master portal, architecture overview & type index
├── kernel/         # Sakamura T-Kernel 2.0 Real-Time OS Specification
├── cores/          # Real-Time Kernel, Lifecycle, Memory & SMP
├── graphics/       # Display Primitives (GDEV), ROP, Geometry & Clipping
├── tip/            # Text Input Primitives, IME (Mozc) & Multilingual TIP
├── vobject/        # Real Object (ROBJ) & Virtual Object (VOBJ_LINK) Fusen
├── window/         # Sakamura Window Manager (WND), Events & App Menus
├── desktop/        # Desktop Shell, Workbench, Tracker & Mobile UI
├── hmi/            # TRON Human-Machine Interface Component Subsystem
├── font/           # Multi-Script Typography & Font Management
├── settings/       # Control Panel & System Configuration Management
├── drivers/        # Hardware Abstraction Layer & VirtIO MMIO
└── apps/           # Application Framework & Tier-1 Suite (gterm, t_editor)
```

### 2.1 Single-Word Subsystem Identifiers

Each subsystem directory and title identifier must use an exact single-word title:
- `Kernel` (Sakamura T-Kernel 2.0 Real-Time OS)
- `Cores` (Real-Time Kernel & Process Lifecycle)
- `Graphics` (Display Primitives & 2D Graphics Engine)
- `Tip` (Text Input Primitives & Multilingual IME)
- `VObject` (Real Objects & Virtual Object Links)
- `Window` (Window Management, Event Routing & Application Menus)
- `Desktop` (Desktop Shell, Workbench & Mobile UI)
- `HMI` (Human-Machine Interface Component Library)
- `Font` (Multi-Script Typography & Font Management)
- `Settings` (System Configuration & Control Panel)
- `Drivers` (Hardware Abstraction & VirtIO MMIO)
- `Apps` (Application Architecture & Tier-1 Suite)

## 3. BeBook-Style Section Topology

Every subsystem manual page in `./b-book/<subsystem>/index.html` must follow this standardized four-part topology:

### Part 1: Architecture & Conceptual Model

- **Subsystem Scope**: Operational boundaries, design philosophy, and hardware interaction.
- **State Machines**: Formal lifecycle states, transition diagrams, and event sequences.
- **Concurrency & Safety**: Thread-safety models, reentrancy constraints, and NASA JPL Rule 3 compliance (zero post-boot heap allocations, bounded recursion).

### Part 2: Data Structures, Types & Enumerations

- **Complete C99 Definitions**: Syntax-highlighted code blocks for every `struct`, `union`, `typedef`, and `enum`.
- **Memory Layout & Invariant Tables**:
  - Struct member name.
  - C99 type and bit width.
  - Byte offset and struct alignment rules.
  - Valid value range and default state.
  - Invariant description and constraints.

### Part 3: C99 API & System Call Reference

Every public function must be documented with an exhaustive specification block:

1. **Signature**: Exact C99 prototype with parameter names.
2. **Parameters Table**: Name, type, direction (`[in]`, `[out]`, `[in,out]`), and functional description.
3. **Return Value & Error Codes**: Explanation of normal returns and all standard TRON error codes (`ER_OK`, `ER_PAR`, `ER_NOMEM`, `ER_OBJ`, `ER_BUSY`, etc.).
4. **Preconditions**: Required system, memory, or object states prior to invocation.
5. **Postconditions**: Resulting state mutations, side effects, and memory ownership changes.

### Part 4: Canonical Code Examples

- Idiomatic, complete, and runnable C99 snippets demonstrating real-world usage patterns.
- Error handling according to TRON standard conventions.

## 4. UI Layout & CSS Design System (`b-book.css`)

The B-Book design system draws inspiration from the classic BeBook, modernized for high-density reading:

- **Color Palette**:
  - Primary Base: High-contrast clean light mode (`#FAFAFA` canvas, `#FFFFFF` panels).
  - Accents: B-System Classic Teal (`#008080`), Dark Navy (`#0A192F`), Slate Bezel (`#D4D0C8`).
  - Code & Syntax: Monospace font stack (`JetBrains Mono`, `Fira Code`, `Consolas`, `monospace`), dark slate background (`#1E293B`) with vibrant syntax highlighting.

- **Layout Matrix**:
  - Two-column responsive architecture:
    - **Sidebar Navigation** (280px fixed/sticky): Links to all 5 subsystems, active section indicator, and in-page table of contents.
    - **Main Documentation Canvas**: Max width 960px, comfortable reading margins, clear typography.

- **Specialized UI Components**:
  - `.api-card`: Distinct container for each function reference.
  - `.api-signature`: Full-width code block containing the formal C prototype.
  - `.api-table`: Structured parameter and return code tables.
  - `.type-badge`: Visual chips designating scalar types (`UW`, `UH`, `COLOR`, `WND*`, etc.).
  - `.ret-badge`: Status badge indicating TRON `ER` or pointer return semantics.

## 5. TAD Compilation Pipeline Integration (`book2tad`)

All documents in `./b-book/` are typeset with BeBook-style specialized layouts (API cards, prototypes, invariant tables, metadata badges, ASCII state machine diagrams). They are compiled into binary BTRON Application Databus (`.tad`) files via the dedicated **`scripts/book2tad.exs`** compiler:

- **B-Book Semantic Structure Mapping**:
  - `<div class="api-card">`: Extracted as structured function entries with Teal heading chips (`@ts_tfont` 1, `@ts_tchar` 14pt bold), C99 prototypes in Monospace blocks (`@ts_tfont` 2, `@ts_tchar` 11pt), aligned invariant tables, and preconditions/postconditions metadata.
  - `<div class="callout">`: Converted to highlighted note boxes with blue header indicators.
  - `<pre><code>`: Preserves exact indentation, line breaks, comments, and UTF-8 box-drawing characters for C99 examples and ASCII diagrams.
  - `<table class="api-table">`: Neatly formatted ASCII aligned columns with monospace font.
  - `<a href="...">`: Native hyper-data Virtual Body links (`@ts_vobj` 0xFFA8) with resolved target IDs.

- **Compilation Targets**:
  ```bash
  $ make book2tad     # Compiles b-book/ into tad_bin/b-book/
  $ make html2tad     # Runs test suite and batch compiles all catalogs
  $ make tad_bin      # Full build of foundational books and b-book
  ```
  Produces `./tad_bin/b-book/*.tad` as the **5th B-System Book** catalog.

## 6. Verification & Quality Invariants

1. **Zero Omission**: Every function declared in `include/btron/` that belongs to one of the five core subsystems must have a formal entry in B-Book.
2. **Standard TRON Types**: Strict adherence to TRON integer types (`B`, `H`, `W`, `D`, `UB`, `UH`, `UW`, `UD`, `ID`, `ER`, `COLOR`).
3. **HTML5 Validation**: Pure semantic HTML5, zero inline CSS hacks, strict UTF-8 encoding.

# Credits

* Namdak Tonpa and Grok 4.5
