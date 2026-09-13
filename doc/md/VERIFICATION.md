# B-System B-FS Volume V2 — Modeling & Verification

Executable oracle, formal properties, and **agent requirements** for keeping verification alive while developing the C99 implementation.
Related docs: `VECTOR.md` (architecture / OKRs, RT_VECTOR layer).

## 1. Artifacts

| File | Role |
|------|------|
| `bfs_model.ml` | OCaml executable reference (journal, Real Bodies, RT_VECTOR, flat ANN) |
| `bfs_properties.v` | Rocq/Coq pure journal state machine + proved theorems |
| `verify_models.sh` | Integration script: OCaml invariants + `coqc` |
| `MODELING.md` | This file — process + agent obligations |

Local naming (`models/` on the development machine):

```text
bfs_model.ml / bfs_model
bfs_properties.v / bfs_properties.vo
verify_models.sh
MODELING.md
```

## 2. Build & verification process

### 2.1 Commands

```bash
# Executable oracle
ocamlc -o bfs_model bfs_model.ml && ./bfs_model
# Expect: "All invariants passed."

# Formal proofs (Rocq >= 9.0)
coqc bfs_properties.v
# Expect: "Closed under the global context" (no axioms)

# Combined
bash verify_models.sh              # OCaml + Coq
bash verify_models.sh --skip-coq   # OCaml only
bash verify_models.sh --skip-ocaml # Coq only
# Exit 0 only if selected checks pass
```

### 2.2 Rocq / Coq imports

Rocq 9.x:

```coq
From Stdlib Require Import List.
From Stdlib Require Import Arith.
From Stdlib Require Import Bool.
From Stdlib Require Import Lia.
```

Older Coq 8.x:

```coq
From Coq Require Import List.
From Coq Require Import Arith.
From Coq Require Import Bool.
From Coq Require Import Lia.
```

### 2.3 Correspondence (OCaml ↔ Coq)

| OCaml (`bfs_model.ml`) | Coq (`bfs_properties.v`) |
|------------------------|---------------------------|
| `begin_tx` | `begin_tx` |
| `log_mut` | `log_mut` |
| `commit` | `commit` |
| `abort` | `abort` |
| `crash` | `crash` |
| `replay` | `replay` |
| `generation`, `dirty` | `gen`, `dirty` |
| `journal.log` / `.committed` / `.in_tx` | `j.log` / `j.committed` / `j.in_tx` |

Coq is **abstract** (journal protocol + generation/dirty). OCaml is the **executable oracle** (bodies, vectors, index, search).

### 2.4 Proved theorems (Coq)

- `commit_clears_inflight`
- `crash_drops_inflight`
- `crash_preserves_committed`
- `replay_after_crash_clean`
- `generation_monotonic_on_commit`
- `commit_extends_committed`
- `abort_discards_inflight`
- `replay_idempotent`
- `crash_replay_safety`
- `demo_tx`

### 2.5 Runtime invariants (OCaml)

- `inv_no_inflight_after_commit`
- `inv_vector_index_consistent`
- `inv_dirty_cleared_after_replay`

### 2.6 Cross-check discipline (human or agent)

For any new journal behaviour:

1. Update `bfs_model.ml` and extend demo / asserts.
2. Mirror the transition in `bfs_properties.v`.
3. Prove or adjust the corresponding theorem.
4. Run `bash verify_models.sh` — must exit 0.

## 3. C99 development path

Production volume code is **hand-written C99** under `src/fs/`. Models are reference oracles, not codegen sources.

### 3.1 Rules

- Implement `vol_*` / journal in C99 per `VOLUME_V2.md` Phase 1.
- Reuse the same operation names: `begin_tx`, `log_mut`, `commit`, `crash`, `replay`.
- Do **not** auto-extract C from OCaml for on-disk layout, endianness, or BlkDev I/O.

### 3.2 Shared scenarios (preferred integration)

```text
OCaml / hand-written  -->  tests/journal_scenarios/*.txt
C99 test harness      -->  steps real journal, compares abstract state
```

Example scenario format:

```text
BEGIN
LOG FidAlloc 1
LOG IndexInsert 1 1 0
COMMIT
CRASH
REPLAY
ASSERT gen=1 dirty=0
```

### 3.3 Optional later linkage

| Method | When |
|--------|------|
| Scenario files | Default — CI-friendly |
| ctypes | Call one C function from OCaml tests |
| ocamlopt -output-obj | Embed oracle in C binary (rarely needed) |
| coq-of-c / VST | Out of scope for now |

## 4. Agent requirements (mandatory while developing C99)

Any coding agent (human-directed or autonomous) working on Volume V2 C99 **must** keep the verification process current.
These are hard process requirements, not suggestions.

### R1 — Verification gate before claiming done

Before reporting a journal-related C change as complete, the agent **must** run:

```bash
bash verify_models.sh
```

(or the equivalent OCaml + `coqc` steps) and ensure exit code 0.
If Rocq/OCaml tools are unavailable in the environment, the agent must say so explicitly and still:

- update or note impact on `bfs_model.ml` / `bfs_properties.v`
- provide scenario steps that C tests should cover

### R2 — Model-first for protocol changes

If the C change alters journal protocol semantics (when dirty is set/cleared, what is logged, generation rules, crash/replay behaviour):

1. Update `bfs_model.ml` first (or in the same change set).
2. Update `bfs_properties.v` and keep theorems valid.
3. Only then implement or adjust C99.

Do not invent C behaviour that contradicts the models.

### R3 — Name and state alignment

C99 symbols and observable state must stay aligned with the models:

| Concept | Must remain consistent |
|---------|------------------------|
| Operations | begin / log / commit / abort / crash / replay |
| Flags | dirty, in_tx (or equivalent) |
| Counter | generation (monotonic on successful commit) |
| Durability | in-flight mutations not visible after crash; committed log preserved |

### R4 — Scenario coverage for new C paths

Every new C journal path (e.g. FID alloc through journal, bitmap bit flip, FileHeader write)
should gain at least one shared scenario (or an OCaml demo assert) that exercises:

- happy path commit
- crash before commit (in-flight lost)
- replay after dirty mount

### R5 — No silent model drift

If the agent cannot update models (missing tools, out of scope for the task), it must:

- list the semantic delta vs current models
- mark follow-up: “models need update for …”

Never leave C and models knowingly divergent without a recorded gap.

### R6 — Verification artifacts stay in tree

Do not delete or rename `bfs_model.ml`, `bfs_properties.v`, or `verify_models.sh` as part of C refactors.
Path moves must update this document and CI.

### R7 — CI expectation

When CI exists for the repo, it should run at least:

```bash
bash models/verify_models.sh
# plus C unit/scenario tests when available
```

Agent-submitted changes that break this gate are incomplete.

## 5. Definition of done (journal feature)

A journal-related feature is done only when:

1. C99 implementation exists and matches intended semantics.
2. `bfs_model.ml` reflects the same protocol (if behaviour changed).
3. `bfs_properties.v` still compiles with theorems closed under the global context (if behaviour changed).
4. `verify_models.sh` passes (or documented exception).
5. At least one crash/replay scenario covers the new path.

## 6. Quick reference for agents

```text
Change journal semantics?
  → bfs_model.ml + bfs_properties.v + verify_models.sh
  → then C99

Only C bugfix, semantics unchanged?
  → C99 + existing scenarios
  → still run verify_models.sh if models touched; else C tests

Add RT_VECTOR / index behaviour?
  → bfs_model.ml (oracle) first
  → Coq only if journal protocol affected
  → then C99

Blocked on coqc/ocaml?
  → state the gap, list scenarios, do not claim formal gate passed
```

## 7. Credits

Namdak Tonpa and Grok 4.5
Models: OCaml oracle + Rocq properties for B-System Clean-Room Volume V2 B-FS File System
