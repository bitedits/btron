# B-System Clean-Room Volume V2 Format

Document version: 1.0  
Target: B-System (BTRON 3.20 clean-room)  
Scope: New volumes only. Historic Cho-Kanji / BrightV path remains untouched.  
Date: 2026-09-13

# Vector Extensions for LLM Agents and BTRON Users

The vector layer turns a BTRON volume into a **native semantic store** while remaining 100 % compatible with the classic Real-Body / TAD model. Documents, images, code, and even UI state keep their normal records; embeddings become additional typed records that any program (or LLM agent) can create, update, and query.

## 1. Core idea in BTRON terms

A Real Body can now contain:

| Record type | Meaning |
|-------------|---------|
| `RT_TADDATA` / `RT_TEXT` / `RT_IMAGE` … | The human-visible content (unchanged) |
| `RT_VECTOR` (new) | One or more embedding vectors that describe the content |
| Optional index records | Volume-level ANN structures that point back to FIDs |

Because everything lives inside the same Real Body graph, the usual BTRON operations (open, link, snapshot, delete) automatically keep the vectors consistent.

## 2. What an `RT_VECTOR` record looks like

```c
#define RT_VECTOR  17

typedef struct {
    UH   dim;          /* e.g. 384, 768, 1024, 1536 */
    UH   metric;       /* 0 = cosine, 1 = L2, 2 = inner-product */
    UH   quant;        /* 0 = f32, 1 = f16, 2 = int8, 3 = binary */
    UH   flags;        /* chunked, multi-vector, etc. */
    UW   nvec;         /* number of vectors in this record */
    UW   model_id;     /* optional: which embedding model produced them */
    /* followed by nvec × dim values (quantized according to quant) */
} VectorHeader;
```

A single document can therefore carry:

- one vector for the whole document,
- one vector per paragraph / section,
- separate vectors for embedded images or code blocks,
- different model versions side-by-side (so you can re-embed later).

## 3. Practical scenarios for LLM agents

**Semantic retrieval over the whole desktop**

```c
// “Find the 8 most similar documents to this paragraph”
float query[768] = { … };          // embedding of the current context
VectorHit hits[8];
vol_vector_search(vol, query, 768, 8, hits);

for (int i = 0; i < 8; i++) {
    // hits[i].fid  → open the Real Body
    // hits[i].rec  → the exact RT_TADDATA / RT_TEXT record
    // hits[i].score
}
```

The agent never has to build or maintain an external vector database; the volume *is* the database.

**Chunk-level RAG**

When a long TAD document is saved, the editor (or a background service) automatically:

1. Splits the text into overlapping chunks.
2. Embeds each chunk.
3. Stores the vectors as multiple entries inside one `RT_VECTOR` record (or as sibling records).
4. Updates the volume-level ANN index.

Later an agent can ask “which paragraphs are most relevant to this question?” and receive precise record offsets.

**Multi-modal search**

An image Real Body can carry its own visual embedding. A text query embedding can be compared against both text and image vectors (cross-modal search) because they live in the same index with a declared metric.

**Agent memory & workspace**

Each agent session can be a Real Body that accumulates:

- conversation turns (`RT_TADDATA`),
- tool results,
- intermediate reasoning,
- embeddings of the whole session.

Snapshots (CoW) give the agent free “time-travel” and the ability to branch alternative plans without duplicating data.

**Personal knowledge base**

A user (or an agent acting for the user) can drop any document, e-mail, PDF-extracted text, or screenshot into a volume. Background embedding keeps the semantic index fresh. Later the same agent can answer “what did I write about topic X last year?” by pure similarity search.

## 4. Benefits for ordinary BTRON desktop users

Even without any LLM the vector layer is useful:

- **Smarter “find similar”** in the file manager – right-click a document → “Show similar files”.
- **Better fusen / sticker suggestions** – the system can propose related documents or templates.
- **Content-aware search** that works across languages (because embeddings are language-agnostic).
- **Automatic tagging / clustering** of large collections without manual metadata.

Because the vectors are ordinary records, classic BTRON tools continue to work; they simply ignore the `RT_VECTOR` records they do not understand.

## 5. Volume-level index (optional but powerful)

When the superblock feature flag `VECTOR` is set, the volume maintains a secondary structure (HNSW, IVF-Flat, or a simple flat index for small volumes). The index stores:

```
(vector_id) → (FID, record_index, offset_inside_record)
```

Updates are transactional with the rest of the filesystem (journal or CoW), so the index never goes out of sync with the Real Bodies.

For very large volumes the index itself can be sharded or stored as a special Real Body that the volume manager treats specially.

## 6. Embedding pipeline (how vectors get created)

Typical flow:

1. User or agent writes / imports content → normal Real Body + TAD records.
2. A lightweight service (or the editor itself) detects the change.
3. It calls an embedding model (local GGUF, remote API, or an on-device NPU).
4. It writes one or more `RT_VECTOR` records into the same Real Body.
5. It notifies the volume to update the ANN index.

Because the model identifier is stored inside the vector header, you can later re-embed everything with a newer model without losing the old vectors.

## 7. API surface (minimal additions)

```c
/* Create / update vectors for an open file */
ER fil_set_vectors(ID fd, const VectorHeader *hdr, const void *data);

/* Search */
typedef struct {
    FID  fid;
    W    rec_idx;
    UW   vec_id;
    float score;
} VectorHit;

ER vol_vector_search(Volume *v,
                     const float *query, UW dim,
                     UW k, VectorHit *out);

/* Convenience: embed + search in one call (if the volume has a default model) */
ER vol_semantic_search(Volume *v, const char *text, UW k, VectorHit *out);
```

Everything else (open, read, link, snapshot, delete) stays exactly the same.

## 8. Why this is powerful for both worlds

- **LLM agents** get a persistent, versioned, multi-modal memory that is just “files”.
- **Classic BTRON users** get semantic search and smarter organisation without leaving the familiar Real-Body desktop.
- **Developers** keep a single coherent model: everything is still a Real Body containing typed records.
- **Future models** can be swapped in by writing new `RT_VECTOR` records; old ones remain readable.

In short, the vector extension turns every BTRON volume into a lightweight, crash-safe, snapshot-capable vector database whose primary key is the same FID that the rest of the system already understands.

# Implementation Plan

## 1. Objectives & Key Results (OKRs)

### Objective 1 — Crash-safe by default

New volumes survive power loss and unclean shutdown without `fsck`-style repair.

| Key Result | Metric | Target |
|------------|--------|--------|
| KR1.1 | Metadata consistency after forced power-cut | 100 % recoverable via journal replay |
| KR1.2 | Mount time after dirty shutdown | < 200 ms on 1 GB volume (journal replay only) |
| KR1.3 | Data loss window | Only the last uncommitted transaction (≤ journal size) |

### Objective 2 — Scalable beyond classic limits

Support volumes and file counts far larger than the historic 64 K FID limit.

| Key Result | Metric | Target |
|------------|--------|--------|
| KR2.1 | Max FIDs | ≥ 2³² (practical), design for 2⁶⁴ |
| KR2.2 | Max volume size | ≥ 16 TiB with 4 KiB blocks |
| KR2.3 | Allocation performance | O(log n) free-block find for large volumes |

### Objective 3 — Integrity & observability

Silent corruption is detectable; volume state is inspectable.

| Key Result | Metric | Target |
|------------|--------|--------|
| KR3.1 | Per-block / per-record checksum coverage | 100 % of metadata + optional data |
| KR3.2 | Scrub can run online | Background, non-blocking |
| KR3.3 | Feature discovery | `vol_features()` returns exact capability set |

### Objective 4 — Extensible for future work (vectors, snapshots)

The on-disk format has explicit extension points so later features do not require a format break.

| Key Result | Metric | Target |
|------------|--------|--------|
| KR4.1 | Feature flags present and versioned | Yes |
| KR4.2 | Reserved space for journal, vector index, generation | Yes |
| KR4.3 | New record types (e.g. RT_VECTOR) can be added without touching superblock | Yes |

### Objective 5 — Zero regression for existing clean-room volumes

Classic `FS_TYPE_STD` volumes continue to mount and work unchanged.

| Key Result | Metric | Target |
|------------|--------|--------|
| KR5.1 | `vol_mount` on classic volume | Identical behaviour to V1 |
| KR5.2 | New code path activated only when magic / fs_type indicates V2 | Yes |

## 2. Requirements (derived from OKRs)

### Functional

| ID | Requirement | Priority | OKR |
|----|-------------|----------|-----|
| F1 | Superblock remains exactly 128 bytes for the first header | Must | 5 |
| F2 | Feature flags word inside the 128-byte header | Must | 4 |
| F3 | Optional journal area described by superblock fields | Must | 1 |
| F4 | Journal logs metadata only (FID table, bitmaps, FileHeaders, RecordIndexes) | Must | 1 |
| F5 | On mount, if dirty → replay journal then clear dirty | Must | 1 |
| F6 | Optional per-block CRC32C (or xxHash64) | Should | 3 |
| F7 | 64-bit block addresses and free-block counters | Should | 2 |
| F8 | Extents for large files (keep classic fragment table for small files) | Should | 2 |
| F9 | Generation / snapshot counter in superblock | Could | 4 |
| F10 | Vector-index root pointer (for later RT_VECTOR work) | Could | 4 |

### Non-functional

| ID | Requirement | Priority |
|----|-------------|----------|
| N1 | All multi-byte fields little-endian on V2 volumes (or explicit flag) | Must |
| N2 | Block size power-of-two, 1024–8192 (default 4096) | Must |
| N3 | No change to public `file.h` / `vol_api.h` signatures for classic callers | Must |
| N4 | `vol_format_v2()` is the only entry point that creates V2 volumes | Must |
| N5 | Implementation lives under `src/fs/`; no new top-level subsystems required for Phase 1 | Should |

### Explicit non-goals (Phase 1–2)

- Binary compatibility with BeFS / Haiku BFS
- Changing the BrightV / Cho-Kanji path
- Full CoW snapshots (deferred to later phase)
- Online volume grow/shrink
- Multi-device spanning

## 3. Architecture

### 3.1 On-disk superblock (VolumeHeaderV2)

Still exactly 128 bytes. Classic fields stay in the same places so a V1 reader can at least recognise the volume; new fields occupy the former `_pad` region.

```c
typedef struct __attribute__((packed)) {
    /* ── classic 128-byte layout (offsets 0–67) ── */
    UH   magic;           /* 0x62FE = VOL_MAGIC_V2  (or keep 0x42FE + fs_type) */
    UH   fs_type;         /* 0x6403 = FS_TYPE_MODERN */
    UW   nfmax;           /* soft limit; real capacity may be higher */
    UW   nlb;             /* total logical blocks (32-bit for now) */
    UH   sfidt;
    UH   sfnmt;
    UH   nbmp;
    UB   access_level;
    UB   dirty;           /* 0 = clean, 1 = dirty, 2 = needs-replay */
    UW   free_blocks;
    UW   data_start;
    UB   vol_name[40];

    /* ── V2 extensions (former _pad, offsets 68–127) ── */
    UW   features;        /* bit-field — see below */
    UW   journal_start;   /* first block of journal, 0 = none */
    UW   journal_blocks;  /* size of journal in blocks */
    UW   checksum_algo;   /* 0=none, 1=CRC32C, 2=xxHash64 */
    UW   uuid[4];         /* 128-bit volume UUID */
    UW   generation;      /* snapshot / transaction generation */
    UW   vector_index;    /* root block of vector index, 0 = none */
    UW   free_space_root; /* root of free-space tree (0 = classic bitmap) */
    UB   _reserved[12];   /* future */
} VolumeHeaderV2;

_Static_assert(sizeof(VolumeHeaderV2) == 128, "must stay 128 bytes");
```

**Feature flags (`features`)**

```c
#define FEAT_JOURNAL     (1u << 0)
#define FEAT_CHECKSUM    (1u << 1)
#define FEAT_EXTENTS     (1u << 2)
#define FEAT_LARGE_FID   (1u << 3)   /* 64-bit FID support */
#define FEAT_VECTOR      (1u << 4)
#define FEAT_SNAPSHOT    (1u << 5)
#define FEAT_FREE_TREE   (1u << 6)
```

### 3.2 In-memory model

```c
struct Volume {
    BlkDev          *dev;
    VolumeHeaderV2   hdr;          /* host-endian shadow */
    /* classic caches */
    UW              *fid_tbl;
    UW              *htbl;
    unsigned char   *ubmp;
    /* V2 additions */
    Journal         *journal;      /* NULL if !FEAT_JOURNAL */
    UW               generation;
    int              is_v2;
    /* … */
};
```

All existing `vol_*` helpers continue to operate on the shadow header. V2-specific behaviour is gated by `v->is_v2` / feature flags.

### 3.3 Journal (Phase 1 core)

- Fixed-size circular log described by `journal_start` + `journal_blocks`.
- Records only metadata mutations.
- Simple transaction format:

```
[ tx_header | records… | tx_commit ]
tx_header  = { magic, generation, nrecords, checksum }
record     = { type, size, payload }   /* FID update, bitmap bit, FileHeader write, … */
tx_commit  = { magic, generation, checksum }
```

- On `vol_sync` / commit: write records → write commit block → update superblock generation + dirty=0.
- On mount with dirty=1/2: replay from last valid commit, then clear dirty.

### 3.4 Layering

```
┌─────────────────────────────────────────────┐
│  file.c / CLU / desktop / agents            │  unchanged API
├─────────────────────────────────────────────┤
│  vol_api.h  (vol_mount, vol_format_v2, …)   │
├─────────────────────────────────────────────┤
│  Volume shadow + feature dispatch           │
├──────────────┬──────────────────────────────┤
│  Classic     │  V2 path                     │
│  (bitmap,    │  journal, checksum,          │
│   FID table) │  free-space tree (later)     │
├──────────────┴──────────────────────────────┤
│  BlkDev (mem, file, flash, virtio, …)       │
└─────────────────────────────────────────────┘
```

## 4. Implementation plan

### Phase 0 — Foundation (1–2 days)

- Add `FS_TYPE_MODERN` (0x6403) and `VOL_MAGIC_V2` (0x62FE) constants.
- Define `VolumeHeaderV2` in `volume.h` (keep old `VolumeHeader` for classic).
- Extend `struct Volume` with `is_v2`, `features`, journal pointers.
- `vol_format_v2(BlkDev*, …)` writes a clean V2 superblock with chosen feature flags.
- `vol_mount` detects V2 and populates the new fields; classic path unchanged.

**Exit criteria:** Can format and mount an empty V2 volume; classic volumes still work.

### Phase 1 — Journaling (core OKR 1)

- Implement minimal journal: allocate area, write tx_header/records/commit.
- Instrument FID alloc/free, bitmap bit flips, FileHeader writes to go through journal.
- Mount-time replay.
- `vol_sync` becomes a journal commit.

**Exit criteria:** Forced power-cut tests show clean recovery; KR1.1–KR1.3 met on test images.

### Phase 2 — Checksums (OKR 3)

- Optional CRC32C per metadata block (or per FileHeader / RecordIndex).
- Store checksum algorithm in superblock.
- Verify on read; fail or repair according to policy.

**Exit criteria:** Corruption injected into a metadata block is detected on next mount/read.

### Phase 3 — Scalability basics (OKR 2)

- 64-bit free-block counters and block addresses inside the shadow (on-disk still 32-bit until LARGE_FID).
- Simple extent map for files larger than a threshold; keep classic fragment table for small files.
- Optional free-space tree root pointer (implementation can stay bitmap for a while).

**Exit criteria:** Volume ≥ 1 TiB can be formatted and basic allocate/free works.

### Phase 4 — Extension points (OKR 4)

- Wire `vector_index` and `generation` fields.
- Document how RT_VECTOR records and a future snapshot mechanism will use them.
- No functional vector search yet — only the hooks.

**Exit criteria:** Superblock contains stable, documented extension fields; VECTOR.md can reference them.

### Phase 5 — Hardening & polish

- Online scrub.
- `vol_features()`, `vol_description()` report V2 capabilities.
- Golden-image tests for V2 + regression suite for classic + BrightV.
- Documentation update in `doc/md/FS.md`.

## 5. API additions (minimal)

```c
/* New format entry point */
int vol_format_v2(BlkDev *dev, UW nfmax, UW nlb, const char *name,
                  UW features, UW block_size);

/* Query */
UW   vol_features(const Volume *v);
UW   vol_generation(const Volume *v);
int  vol_is_v2(const Volume *v);

/* Existing calls stay unchanged; behaviour becomes journaled when v->is_v2 */
```

No changes required to `opn_fil`, `ins_rec`, etc.

## 6. Testing strategy

| Test | Purpose |
|------|---------|
| Classic volume mount | KR5.1 regression |
| V2 format → mount → umount | Smoke |
| Power-cut during write | Journal replay (OKR 1) |
| Injected metadata CRC error | Checksum detection (OKR 3) |
| 100 k file create/delete | Allocation correctness |
| Parallel to BrightV path | No cross-contamination |

## 7. Success definition

Volume V2 is considered complete for the initial release when:

1. All Phase 0–1 exit criteria are green.
2. OKR 1 (crash safety) is demonstrably met.
3. Classic and BrightV volumes continue to work with zero behavioural change.
4. The superblock contains the extension points needed for vectors and future snapshots (OKR 4).

Later phases (extents, free-space tree, full vector index, CoW snapshots) build on this foundation without another format break.

## 8. Later phases (no format break)

All of the following reuse the same 128-byte `VolumeHeaderV2` and the feature-flag / extension-point fields already defined. Enabling a capability is a matter of setting a flag, allocating the indicated structures, and writing code that understands them. Existing V2 volumes remain mountable; new code simply ignores flags it does not implement.

### 8.1 Extents (Phase 3+)

**Goal.** Replace the classic fixed-size fragment table for large files with a scalable extent map.

**Design.**
- Small files continue to use the existing 6-byte fragment table (compatibility + density).
- When a file exceeds a threshold (e.g. 64 KiB or a configurable number of fragments), the FileHeader gains an extent-map root.
- An extent is `(start_block, length)` — 8 or 16 bytes depending on address width.
- Extent maps live in ordinary data blocks and can grow via indirect blocks, exactly as RecordIndexes already do.
- Feature flag: `FEAT_EXTENTS`.

**Implementation sketch.**
1. Add `extent_root` / `extent_count` fields inside the FileHeader reserved area (or a side record).
2. Allocation path: try fragment table first; if full or file is large, switch to extent mode.
3. Read path: if extent_root ≠ 0, walk the extent map; otherwise use classic fragments.
4. Journal every extent-map mutation.

**Exit criteria.** Sequential write of a multi-gigabyte file uses a handful of extent entries; random reads remain correct.

### 8.2 Free-space tree (Phase 3+)

**Goal.** O(log n) allocation and free for volumes with tens of millions of blocks, replacing the linear bitmap scan.

**Design.**
- Superblock field `free_space_root` already exists.
- On-disk structure: a B+tree (or simple extent tree) of free runs, ordered by block number.
- Classic bitmap can remain as a cache or for tiny volumes; the tree is authoritative when `FEAT_FREE_TREE` is set.
- Updates are journaled.

**Implementation sketch.**
1. On format with `FEAT_FREE_TREE`, initialise a single free run covering the data region.
2. `vol_alloc_block` searches the tree; `vol_free_block` merges adjacent runs.
3. Periodic (or on-demand) rebuild from the bitmap is possible for recovery.

**Exit criteria.** Allocation latency stays flat as volume size grows from 1 GB to 1 TB+.

### 8.3 Full vector index (Phase 4+, see VECTOR.md)

**Goal.** Native approximate-nearest-neighbour search over `RT_VECTOR` records.

**Design.**
- Superblock field `vector_index` points to the root of an ANN structure (HNSW, IVF-Flat, or a flat index for small volumes).
- Index entries store `(FID, record_idx, vec_id) → embedding` (or a quantised form).
- All index mutations are journaled; snapshots (below) automatically version the index.
- Feature flag: `FEAT_VECTOR`.

**Implementation sketch.**
1. Define `RT_VECTOR` record layout (already sketched in VECTOR.md).
2. Background or on-write path embeds content and inserts into the index.
3. `vol_vector_search()` walks the index and returns ranked `VectorHit`s.
4. Re-embedding with a new model writes new vectors; old ones remain until explicitly pruned.

**Exit criteria.** Semantic search returns correct top-k results on a volume containing ≥ 10 k documents; index stays consistent across crash recovery.

### 8.4 Copy-on-Write snapshots (Phase 5+)

**Goal.** Instant, space-efficient volume or sub-tree snapshots for undo, agent workspaces, and backups.

**Design.**
- Superblock `generation` increments on every committed transaction.
- When `FEAT_SNAPSHOT` is set, metadata blocks (and optionally data blocks) become immutable once written.
- A write allocates a new block, updates the parent pointer, and leaves the old block alive while any snapshot still references it.
- Snapshots are named Real Bodies (or a special side table) that hold a generation number and a root pointer.
- Reference counts (or a reverse map) reclaim unreferenced blocks.

**Implementation sketch.**
1. Introduce a block reference-count array or per-block generation stamp.
2. FileHeader / extent-map / FID-table updates go through a CoW path when a snapshot exists.
3. `vol_snapshot_create(name)` records the current generation; `vol_snapshot_delete` drops the reference.
4. Journal records the CoW decisions so recovery reconstructs the correct generation graph.

**Exit criteria.** Creating a snapshot is O(1); writing after a snapshot does not modify the snapshotted view; deleting the last snapshot reclaims space.

### 8.5 Ordering and dependencies

```
Phase 1  Journal ← foundation for everything below
    ├─ Phase 2  Checksums
    └─ Phase 3  Extents + Free-space tree
            ├─ Phase 4  Vector index   (needs stable FIDs + journal)
            └─ Phase 5  CoW snapshots  (needs journal + generation + optional extents)
```

Each phase only sets additional feature flags and populates fields that are already reserved. A volume created with only `FEAT_JOURNAL` remains valid forever; later tools can upgrade it in place by enabling more flags and building the corresponding structures.

### 8.6 Upgrade path for existing V2 volumes

```c
int vol_upgrade(Volume *v, UW new_features);
```

- Validates that the requested features are a superset of the current ones.
- Allocates journal / free-space tree / vector index as needed.
- Rewrites the superblock atomically (via the existing journal).
- Never changes the 128-byte size or the meaning of already-written classic fields.

This guarantees that “later phases build on this foundation without another format break.”

# Credits

Namdak Tonpa and Grok 4.5
