# B-System Volume & File System Architecture

**Binary-compatible reference for BTRON3 / B-right/V (Cho-Kanji) style volumes**

Document version: 1.0
Target: B-System (BTRON 3.20 clean-room)
Sources: BTRON3 Shared Data §4 (Floppy / Volume Format), B-right/V public docs, TAD record model

This document is the single reference for implementing a **full binary-compatible** BTRON volume layer: on-disk layout, Real Body records, block backends, and multi-target embedding (MCU flash, UEFI, PC-98, POSIX).

## 1. Layered architecture

```
┌─────────────────────────────────────────────────────────┐
│  Kernel / apps (ELF or XIP in flash)                    │
├─────────────────────────────────────────────────────────┤
│  Block device layer                                     │
│    • memdisk  (embedded volume image)                   │
│    • flash partition / SPI NOR                          │
│    • UEFI file / VirtIO-blk / IDE (PC-98)               │
├─────────────────────────────────────────────────────────┤
│  BTRON volume (same on-disk format everywhere)          │
│    Volume Header · FID table · bitmap · Data Blocks     │
│    Real Bodies = records (RT_LINK, RT_TADDATA, RT_PROG…)│
└─────────────────────────────────────────────────────────┘
```

Rules:

- One **on-disk volume format** for all targets.
- Only the **block backend** changes (memdisk, flash, UEFI file, PC-98 disk, VirtIO).
- Do **not** store the FS as ad-hoc C structs in `.data`. Embed a **raw volume image** (sequence of logical blocks).
- Public API stays in `file.h`; on-disk structs live under `include/btron/fs/`.


## 2. Fundamental units

| Unit | Size | Notes |
|------|------|--------|
| Physical sector | implementation-defined | Often 512 or 1024 |
| **Logical block** | **1024 bytes** | BTRON addressing unit |
| System header | 128 bytes | Start of system area |
| File header | **192 bytes** | Start of each Real Body header block |
| Record index entry | **16 bytes** | One descriptor per record (plus continuation entries) |

Multi-byte fields on classic volumes are **big-endian** (MSB first).  
B-right/V on x86 often uses **quasi** little-endian variants; detect via volume magic / policy and convert at the block boundary if needed.

## 3. Volume layout (whole disk / image)

```
+------------------+  Block 0 …
| System area      |  System header (128 B) + FID table + Short-name table
|                  |  + allocation bitmaps
+------------------+
| File data region |  Header blocks + data/index blocks of all Real Bodies
|                  |  Root file is FID = 0
+------------------+
```

### 3.1 System header (128 bytes)

Created at format time; most fields are fixed after creation (except free-space / dirty state).

| Field (logical) | Typical meaning |
|-----------------|-----------------|
| Magic / format ID | **0x42FE** standard · **0x52FE** extended (official BTRON3 FD format) |
| FS type | **0x6400** standard · **0x6401** extended |
| NFMAX | Max number of files (FIDs) on volume |
| NLB | Total logical blocks |
| SFIDT | Size of FID table in logical blocks ≈ `(NFMAX × 4) / 1024` |
| SFNMT | Size of short-name table in logical blocks ≈ `(NFMAX × 4) / 1024` |
| NBMP | Bitmap size in blocks (see below) |
| Access management level | 0 = none · 1 = partial · 2 = full |
| Volume / FS name | Matches root file name |
| Free block count, mount state, etc. | Implementation / extended fields |

**Bitmap sizing (standard vs extended)**

- Standard: usage bitmap uses 1 bit per logical block → size related to `NLB / 8`.
- Extended: denser / different packing (e.g. related to `NLB / 32` in the official text).

There are typically **two** bitmaps:

1. **Used-block bitmap** — 0 = free, 1 = used or bad  
2. **Bad-block bitmap** — 0 = OK, 1 = bad (bad blocks must also be marked used)

### 3.2 FID table (File ID table)

- One **4-byte entry per FID**, indexed by FID order.
- Entry **0** means unused FID.
- Standard interpretation: **3-byte start block address** of the file’s header block + **1-byte reference count** (fixed links within the volume).  
  Extended format may park large refcounts in the file header when count would exceed 255.
- Max refcount on standard format: **255**.
- **FID 0** = root file (always exists after format). Root name equals the file-system / volume name; initial refcount = 1.

**Measured on B-right/V** `[measured]`: the table starts at block `nbmp`
(41 on the golden volume), one 4-byte entry per FID read as **LE24 block
address in bytes 0–2 plus a 1-byte reference count in byte 3**, 2048 entries
per 8 KiB block, `nfmax = sfidt × (block_size/4)` = 65 536. That parse scores
better than every LE32/BE24/LE16 alternative tested against the volume, and
gives 4457 live FIDs.

**The entry's block address names the body's first *data* block [measured].**
Over all 4457 live FIDs of the golden volume the Real Body header magic
(`norT`/`Tron`) is at `ptr-1` for **4457 of 4457** and at `ptr` for **0**: 4150
bodies have magic only at `ptr-1`, and the remaining 307 have it at `ptr` as
well because that block is the *next* body's header (for 197 of them `+0x4C == 1`,
so the body owns no data block at all and `ptr` is exactly its successor's
header). One rule therefore covers the whole volume, and it is a **bijection**:
4457 FIDs reach 4457 *distinct* header blocks.

Resolving a header by "magic at `ptr`, else `ptr-1`" — the order the engine
first used — breaks that bijection: the 197 header-only bodies become unreachable
and the 197 successors after them are named by two FIDs at once, so one body is
listed twice under two different names at the same size and date. That is the
mechanism behind the duplicated siblings, and it is why a body the engine creates
must register `hdr + 1` in the FID table rather than `hdr`: one on-disk
convention, read and written, not two.

**Root namespace** `[measured]`: **FID 0 is the root drawer.** Its header is
block `105`, its T-code name at `+0x6C` is the volume label `B-right/V`, and it
carries **52 `RT_LINK` rows** (`+0x44` = 52) in the tail area of its header
block. The root listing is those rows, read through the ordinary container path
— a positive enumeration, not a derivation.

The engine previously decoded FID 0 as `SBOOT`, because `SBOOT` is the body whose
header is block `106` = FID 0's `ptr`, and the "magic at `ptr` first" rule took
it. With FID 0's real drawer never read, all 52 of its children looked like
nobody's child and were dumped at the top level, next to the drawers that really
do name them (`etc`, `LC_TIME`, `Mail Manager`, `Makefile`, `makerules`, the
`*.h` includes). See HISTORY at the end of this section for the derivation that
masked the bug.

A link row's `+8` target-location data cannot rescue any genuinely unreached
drawer: it is `0x8000xxxx`/`0x00000000` on all 8919 rows and names no block.

This is the single source of truth for both consumers — the engine's `rd_dir()`
root enumeration (what `sc` and the VFS list) and clu's `fs -t/-l/-g` tree — so
the two cannot disagree; see §7.4 consequence 8.

#### HISTORY: the derived root list (superseded)

Until the `ptr-1` rule above was measured, §3.2 asserted that FID 0 was not a
drawer and derived the root list as "a drawer is a root child iff no link row in
the volume names any FID whose header block is its own". Judged by FID alone
**42** of the 1232 drawers looked like nobody's child, judged by header block
**38** did, on **37** distinct bodies. That census was the fingerprint of the
wrong pointer rule, not a property of the volume: the real top level is the root
drawer's 52 rows.

### 3.3 Short-name (hash) table

- One **4-byte hash per FID**, same order as FID table.
- Speeds name → FID lookup. Full name (up to **40 bytes**) still lives in the file header area.
- Hash algorithm (official):

```text
Pack file name into NAME[10] as 10 × 32-bit words (pad with 0 if name < 40 bytes).
hash = 0;
for (i = 0; i < 10; i++) {
    hash = hash ^ NAME[i];
    rotate hash right by 1 bit;
}
```

(Exact rotate direction/width follows the BTRON3 text; implement to match golden images from Cho-Kanji when available.)

### 3.4 File data region

All Real Bodies live here. Each file is located by FID → header block address from the FID table.

## 4. Real Body (file) structure

A BTRON “file” is an ordered list of **records**, not a single byte stream.

```
Real Body (FID)
├── Header block(s)
│   ├── File Header          (192 bytes)
│   ├── Fragment table       (or location data if link-file)
│   └── Record index         (level 0: up to 40 × 16-byte entries in header;
│                            up to 496 slots in a B-right/V 8 KiB row area, §7.4)
├── Index blocks             (if index level 1 or 2)
├── Indirect blocks          (level 2)
└── Data blocks              (concatenated record payloads)
```

**Index level**

| Level | Max record index entries (approx.) | Where index lives |
|-------|-------------------------------------|-------------------|
| 0 | 40 (clean-room 1 KiB header) · **496** slots in a B-right/V 8 KiB row area, 246 in use, see §7.4 | Header block |
| 1 | 5120 (64 × 80) | Index blocks + indirect in header |
| 2 | 655360 (64 × 128 × 80) | Two-level indirection |

**Link-files** (special file type) consist of a header block only; the fragment-table region holds **90-byte location data** pointing at the target, instead of a normal fragment table.

## 5. File Header (192 bytes)

Exactly **192 bytes**. Multi-byte fields big-endian on classic media.

Flags word layout (official bit roles):

```text
TTTT xxxx BAPO xRWE
```

| Bits | Meaning |
|------|---------|
| T | File type: 0 = link-file · 1 = normal file · 2+ reserved |
| P | Permanent (delete-protect) |
| O | Write-protect |
| A, B | Application attributes |
| RWE | Owner access (Read / Write / Execute) |
| x | Reserved (0) |

Other header content (logical; pack into 192 bytes):

- Application type (ATYPE) — ties to pictogram / handler  
- Timestamps (create / modify / access)  
- Owner / group  
- Link count (nlnk)  
- Index level (idxlv)  
- Record count, total payload size  
- Full file name (up to 40 bytes) in header area as specified  
- Reserved / padding to 192  

Implementation tip: keep a `FileHeader` struct of exactly 192 bytes; fill unknown trailing bytes with 0 until byte-exact Cho-Kanji dumps are matched.

### 5.1 Measured: B-right/V Real Body header

Source of truth: the golden `/B-right/V` volume in `hda.qcow2` (little-endian,
8192-byte logical blocks). Each field below is labelled **[measured]** (decoded
byte-exactly against that volume) or **[inferred]** (interpretation not yet
proved). This layout *replaces* the §5 clean-room field list for
`FS_TYPE_BRIGHTV` volumes; the two encodings are not both authoritative.

| Offset | Field | Label |
|--------|-------|-------|
| +0x00 | Magic `"norT"` / `"Tron"` | measured |
| +0x04 | UH flags | measured |
| +0x40 | Constant `0x0FFF0000` | measured (never decoded as meaning) |
| +0x44 | UW number of `RT_LINK` rows in the level-0 index | measured (§7.4: matches the row scan for 4456 of 4457 bodies) |
| +0x48 | UW total payload size | measured |
| +0x4C | UW block count claimed by the body | measured |
| +0x50 | UW index row count, **continuation rows excluded** | measured (§7.4) |
| +0x5C | Constant `0xFFFFFFFF` | measured |
| +0x60 / +0x64 / +0x68 | STIME create / modify / access | measured |
| +0x6C | Up to **20 × UH** T-code file name, zero-terminated. Measured over all 4457 bodies: names run 1..20 units and the 20-unit case is real (43 bodies), so the field ends at `+0x94`. Reading only 16 units truncates the 163 bodies named with 17..20 characters -- `Limitations on Use` becomes `Limitations on U`, and a path segment typed or listed from that name then resolves to nothing | measured |
| +0x94 … +0xFF | further per-body words, never decoded | measured **not** index rows: no body on the volume has an index row below `+0x1100` |

**The header is one block *before* the address in the FID table** (§3.2): the
table entry names the body's first data block. Every field in this table is read
from the block at `ptr-1`.

**No drawer-ID / parent-DID encoding was found.** `+0x64` / `+0x68` are the
modify and access timestamps, not identity fields — matching a body's `did` /
`pdid` against them reproduces at most 3–7 % of the parent edges that the real
link records give, and the only 100 % "agreements" were the two constant words
at `+0x40` and `+0x5C`. Directory hierarchy on B-right/V is carried **only** by
`RT_LINK` records (§7.4, §9).

## 6. Fragment table

- Tracks free fragments **inside** blocks already allocated to this file (after deletes / shrinks).
- Up to **32** entries; each entry **6 bytes**.
- Entries sorted by **decreasing fragment size**; first entry with size 0 ends the table.
- Even without deletes, the remainder of the last written block is usually one fragment.
- On allocation, search fragment table before allocating new logical blocks.

(For a first clean-room engine, a simplified extent list “start block + count” is acceptable **internally**, but on-disk compatibility requires the official 6-byte fragment entries when writing Cho-Kanji-readable volumes.)

## 7. Record Index (16 bytes per entry)

Record index maps **record number → type/size/location** inside the file’s data blocks.

### 7.1 Entry kinds (first two bytes)

| Kind | Byte0 | Byte1 |
|------|-------|-------|
| Continuation (connection) index | ≠ 0 | any |
| Normal index | 0 | ≠ 0 |
| Link index | 0 | 0x80 |
| Unused | 0 | 0 |

Continuation entries immediately follow a normal entry when one record spans multiple logical blocks. They are **not** counted in the user-visible record count.

**Confirmed on B-right/V [measured]:** the `escp_raster` body (fid 41) has four
data rows at `+0x1F90`, `+0x1FB0`, `+0x1FC0`, `+0x1FF0` and exactly one
continuation row, at `+0x1F90`'s immediate successor `+0x1FA0` — the one row
whose size (43 128 bytes) exceeds a single 8 KiB block. Reading the area in the
other direction would attach that continuation to a 2166-byte single-block
record, so the area is ordered with ascending record numbers at ascending
addresses. Continuation rows add **no** payload bytes: the sizes on the four
data rows already sum to the header's `+0x48` total, and that identity holds for
4457 of 4457 bodies on the volume (§7.4).

### 7.2 Normal entry (conceptual)

```c
typedef struct {
    UH kind;    /* classification via first 2 bytes */
    UH type;    /* record type (RT_*) + subtype bits */
    UW size;    /* payload size in bytes */
    UW offset;  /* byte offset into concatenated data, or block-oriented form */
    /* pad / flags to 16 bytes total */
} RecordIndex;  /* exactly 16 bytes on disk */
```

Official type nibble form includes `100T TTTT` style packing for record type in the index word; match golden dumps when implementing writers.

### 7.3 Indirect index (8-byte entries)

Used when index level > 0. Points at index blocks; carries count of valid records under that branch for seeking. Unused entries have logical block address 0.

**The format is measured (below); the record-index path is not yet implemented.**
A body whose header carries no level-0 rows (fid 139 `index` is the only one:
1442 declared records, 536 398 payload bytes, 71 blocks) still opens as a stream
of one record, because `read_header_block()` only ever reads the header's own row
area. Its *hierarchy* is complete, though: `fil_hier` follows these entries, so
the drawers behind the index are reachable and listed by `sc` and `fs -t`.

**Measured format of those entries** `[measured]`, on fid 139 `index` (header
block 58454, a row of fid 222, `+0x50` = 1442, `+0x48` = 536 398). Its header's
row area holds **no** RT_LINK rows at all -- the only in-use 16-byte slots are at
`+0x6C0` and `+0x6D0`, and they are index entries, not records. Read as 8-byte
`(UW row count, UW block)` pairs from `+0x6C8` they are:

```text
+0x6C0  0x00000014 0x0000D6A1      <- one UH(20) header word, then the first entry's block
+0x6C8  0x000001FE 0x0000E45A      510 rows @ block 0xE45A
+0x6D0  0x000001FE 0x0000E459      510 rows @ block 0xE459
+0x6D8  0x00000192 0x0000E458      402 rows @ block 0xE458
```

Subtracting 1 from every block word -- the same correction the FID table needs at
§7.4 consequence 4, because an entry names the block *after* the one holding the
thing described -- yields four index blocks that match their declared counts
exactly:

| entry | declared rows | block | rows measured | RT_LINK rows | targets |
|-------|---------------|-------|---------------|--------------|---------|
| 1 | 20 | 54944 | 20 | 18 (2 are `0x88`) | unreached bodies |
| 2 | 402 | 58455 | 402 | 402 | unreached bodies |
| 3 | 510 | 58456 | 510 | 510 | unreached bodies |
| 4 | 510 | 58457 | 510 | 510 | unreached bodies |

Inside an index block the rows are the same 16-byte slots, packed from offset
`+0x000` with no header prefix, and the count field says how many are live. Their
sum, 20 + 402 + 510 + 510 = 1442, is exactly `+0x50`. Blocks past the index
(`58458` onwards) are the payload and must not be scanned as rows -- scanning
them yields 34 102 row-shaped false positives.

**What this buys**: those 1440 link rows name **every one of the 573 bodies the
root cannot otherwise reach** (measured: 573 of 573 covered, and 0 reached by any
reached drawer's own rows). So `index` is not a document about the B-Book tree,
it *is* the container that holds it, and decoding its continuation entries is
what makes that subtree walkable. fid 139 is also the only body in the volume
whose `+0x50` exceeds its decoded level-0 rows (measured census: 1 of 4457), so
no other body needs this path yet.

### 7.4 Measured: B-right/V level-0 rows

On a B-right/V volume the level-0 index is an **array in the tail area of the
header block**: 16-byte slots from `+0x100` to `block_size`, ordered with
ascending record numbers at ascending addresses. It is **not** one contiguous
run — a row freed by a deleted record leaves its slot zeroed, so holes sit
inside the array, and the run does not necessarily end at the last slot. A
newly written body packs its rows to the end of the area (newest row in the last
slot), which is why a hole-free file reads back as a run.

An 8 KiB header block therefore holds up to **496 rows** in that area — not the
40 rows the clean-room 1024-byte layout allows. Measured maximum in use: 246
rows (fid 1091 `SI`), and no body on the volume exceeds the array's 512-row
in-core capacity.

Two header words describe the same array, and both hold for 4456 of the 4457
bodies (the exception is the §7.3 indirect-index body):

| Header word | Equals | Census over all 4457 bodies |
|-------------|--------|------------------------------|
| `+0x50` | rows in the area, continuation rows excluded | 4456 match; fid 139 declares 1442 |
| `+0x44` | of those, the rows with `byte1 == 0x80` (link rows) | 4456 match |
| `+0x48` | **sum of the data rows' `+8` sizes** | **4457 match, 0 exceptions** |

Row fields, measured against golden bodies:

| Offset | Data rows | Link (`RT_LINK`) rows | Label |
|--------|-----------|------------------------|-------|
| +0 byte0 | 0 | 0 | measured |
| +0 byte1 | packed record type, bit 7 always set on an in-use row, `RT = byte1 & 0x1F` | `0x80` (`RT_LINK` + bit 7) | measured |
| +2 | UH subtype / attribute | UH subtype | measured |
| +4 | UW position: **low half = block index**, high half = byte offset | **UW target: low half = target FID**, high half = further location data | measured |
| +8 | UW payload size | **target location data — not a size** (e.g. `0x800F0000`) | measured |
| +12 | UB block count for this record | UB(0) | measured |
| +13 | UB[3] LE24 block address (allocation hint) | same | measured |

**RT census** `[measured]`, over every in-use row of the golden volume's 4457 bodies:
`0x80` link 10526, `0x88` 3696, `0x9F` 2090, `0x81` 2041, `0x86` 419, `0x8B` 188,
`0x8A` 92, `0x87` 86, `0x90` 61, `0x89` 54, `0x83` 24, `0x8F` 20, `0x8C` 13,
`0x91` 2, `0x92` 2, `0x93` 3, `0x8E` 1. Splitting bodies by the rows they carry:
187 hold link rows only, 1180 hold links *and* data rows (a TAD document that is
also a container, e.g. `English`), **1925 are one `0x9F` row and nothing else**,
142 hold a single row of another type, 1008 hold several data rows, 15 hold no
in-use row. `RT 0x1F` (`byte1 0x9F`) is therefore the direct-stream record: a body
with exactly one such row and no link rows is a stream, and every other body is a
document. Flagging streams by *header position* instead -- "the header is the block
before the FID entry", which is true of all 4457 -- calls 2934 of them streams,
leaves no body to read as a document, and hides the record index from any view that
skips streams when it dumps records.

Consequences the engine must honour:

1. A link row's `+8` word is larger than any body, so a size sanity check used
   to end the row scan at the first link row — that is what made drawers read
   as empty. Link rows take **size 0** and the **low half of `+4`** as the
   target FID. Measured on the golden volume: 1695 of 1695 rows whose full `+4`
   word exceeds the FID space name a live FID once masked. With this mask, every
   link row names a live FID and every drawer's children come from its own rows —
   the root list being FID 0's 52 rows (§3.2). Re-measured through the corrected
   header resolution (consequence 4), the volume's shape is: **4457 live bodies,
   1364 of which carry at least one row naming a live body, 52 children on the root
   drawer, 3883 bodies reached from the root, 573 reached by no row the root can
   walk, deepest chain 12 hops.** The earlier census (1232 drawers, 2838 linked
   bodies, 38 unclaimed) was measured through the wrong header rule and is
   superseded.
   The 573 are not damage and not a second root. Measured over every reached
   drawer's link rows, **zero** of them name an unreached live body, and all 573
   are drawers that *are* named -- only ever by another one of the 573 -- so they
   form a closed subgraph: the Japanese B-Book document tree. Its entrance is
   decoded by no current path in the engine: the tree hangs off the level-1
   indirect index body (`index`, fid 139, header block 58454, a row of fid 222,
   `+0x50` = 1442 rows, `+0x48` = 536 398 bytes), whose four continuation entries
   name the index blocks holding 1440 RT_LINK rows, and those rows name **573 of
   the 573** unreached bodies (§7.3, measured). fid 139 is the only body in the
   volume whose declared row count exceeds its decoded level-0 rows, so it is the
   only remaining row source. Until that index is decoded these bodies are
   listable by FID and by `fs -a`, but no tree walk in `sc` or `fs -t` reaches
   them.
2. Only data rows contribute to the concatenated payload (§8), so rewriting
   offsets cumulatively must skip link rows or it destroys the target FID.
3. The data extent defaults to the header's successor block (or to the block the
   FID table named when the header was found at `blk-1`). Only when that
   successor block starts another body's Real Header does the engine take the
   row's `+13` hint as the extent instead.
4. **The header is at `ptr-1`, always** — the FID table names the body's data
   block, never its header (§3.2). Reading `ptr` first and `ptr-1` only as a
   fallback silently aliases 197 bodies onto their successors and hides the root
   drawer at FID 0.
5. **The scan must tolerate holes.** Stopping at the first zero slot loses rows
   that live below the hole: measured on the golden volume, 6 bodies lost 27
   rows in total. `English` (fid 226, a row of fid 223) is the clearest case — its rows are 4 link
   rows at `+0x1F40…+0x1F70`, a data row at `+0x1F80`, five zero slots, then two
   data rows at `+0x1FE0`/`+0x1FF0`, so the truncating scan reported 2 records,
   no children, and 242 of its 2326 payload bytes. Scanning the whole area
   restores all 7 declared rows and every body's size sum then equals `+0x48`.
6. A writer must keep that contract: place row *i* so the newest row lands in
   the last slot (the reverse order silently re-numbers records across a
   write/read round trip), clear the area before writing so a stale row from a
   longer version of the file is not read back as a record, and write `+0x44`
   and `+0x50`, which readers take as the row counts.
7. **`+0` is two separate bytes, and byte 1 is what makes a row visible.** Byte
   0 is 0 (or a continuation code) and byte 1 is the packed record type with bit
   7 set: `0x80` for a link (`RT_LINK`), `0x81` for a TAD data row
   (`RT_TADDATA`), `0x88` for a fusen row (`RT_MFUSEN`). Readers treat
   `byte0 == 0 && byte1 == 0` as an empty slot, so writing the record type only
   at `+2` leaves a row area that reads back entirely as holes — the body then
   opens with no index and its whole payload streams as one record. Measured on
   a created body: with `+0 = 00 81` both rows round-trip in record order with
   their exact sizes; with `+0 = 00 00` neither row is seen.
8. **Hierarchy is resolved once per volume and shared.** Computing parentage per
   listing run means re-reading every body's header block, and two independent
   implementations drift: clu's tree judged edges by FID while `rd_dir()`'s root
   fallback judged nothing at all and dumped every FID whose own block carries a
   magic (307 entries, alias duplicates included), which is how the same drawer
   appeared twice in `sc`. The engine therefore builds one cached snapshot per
   mounted volume — a single pass over the live FIDs that records each body's
   header block (`ptr-1`, consequence 4), its link-row targets and its parent —
   and both `rd_dir()` and clu's `fs` views read their parent/child structure
   from it. The snapshot is invalidated on the volume's mount sequence plus every
   mutation that can add or remove a row (`cre_fil`, `cls_fil` of a dirty body,
   `del_fil`, `ins_rec`, `del_rec`, `cre_lnk`, `del_lnk`, `mov_fil`), so listings
   stay correct without re-deriving the volume per keystroke.
   The cache's job is to answer "who is this body's parent" and "does this body
   carry rows", **not** to invent a root: the root list is FID 0's own link rows,
   read through the ordinary container path (§3.2). Measured census backing the
   `+8` column: of 8919 link rows, 6339 are zero and 2580 have only the high half
   set (`0x8000xxxx`), none of them a block address, so no parent edge is
   recoverable from that field and `RT_LINK` rows are the only hierarchy on disk.

## 8. Data blocks

- Raw 1024-byte logical blocks.
- Hold **concatenated payloads** of records in record order.
- A single record may span multiple blocks (described by normal + continuation index entries).
- One block may contain tails/heads of multiple records.

**Logical view**

```text
records[]  = array of RecordIndex   (metadata)
Data Blocks = byte store addressed by offset/size (and block maps)
```

## 9. Record types (Real Body contents)

From BTRON3 / `tad.h` constants (compatible with Cho-Kanji):

| Type | Name | Content rules |
|------|------|----------------|
| 0 | RT_LINK | Link record (Virtual Body pointer) — standard C-like fields |
| 1 | RT_TADDATA | TAD main |
| 2 | RT_TADCMT | TAD comment |
| 3 | RT_TADSUB | TAD auxiliary |
| 4 | RT_TADRSV | Reserved |
| 5 | RT_SFUSEN | Setting fusen |
| 6 | RT_DFUSEN | Designated fusen |
| 7 | RT_FFUSEN | Function fusen |
| 8 | RT_MFUSEN | Execution-function fusen |
| 9 | RT_PROG | **Executable program** (native code; subtype ≈ CPU) |
| 10 | RT_DATABOX | Data box |
| 11 | RT_FONT | Font |
| 12 | RT_DICT | Dictionary |
| 13–14 | | System reserved |
| 15 | RT_SYSDATA | System data |
| 16–31 | | Application-defined |

**Executable Real Bodies** set file-level executable flag (e.g. `OBJ_EXEC 0x8000`) and include at least one **RT_PROG** record. CLI tools and GUI apps are Real Bodies, **not** ELF files on disk.

**Virtual Bodies** appear as:

1. **RT_LINK** records inside a parent Real Body, and/or  
2. **TS_VOBJ** segments inside TAD streams, ordered to match link records.

## 10. TAD segment stream (inside TAD-carrying records)

Variable-length segments (classic):

```text
+0  UB  0xFF        escape
+1  UB  segment_id  (TS_*)
+2  UH  length      (data size, endian per volume policy)
+4  … data …
```

| ID | Name |
|----|------|
| 0xE0 | TS_INFO |
| 0xE1 | TS_TEXT |
| 0xE2 | TS_TEXTEND |
| 0xE3 | TS_FIG |
| 0xE4 | TS_FIGEND |
| 0xE5 | TS_IMAGE |
| 0xE6 | TS_VOBJ |
| 0xE7 | TS_DFUSEN |
| 0xE8 | TS_FFUSEN |
| 0xE9 | TS_SFUSEN |


## 11. Block device abstraction (portable)

```c
typedef struct {
    ER (*read)(void *ctx, UW lba, void *buf, UW count);
    ER (*write)(void *ctx, UW lba, const void *buf, UW count);
    UW  block_size;   /* 1024 for BTRON logical blocks */
    UW  nblocks;
    void *ctx;
} BlkDev;
```

| Backend | Use |
|---------|-----|
| `blk_mem` | Embedded `.btron_vol` / memdisk |
| `blk_file` | POSIX file `btron.vol` |
| `blk_flash` | MCU NOR/NAND partition |
| `blk_uefi` | File on ESP or FV section |
| `blk_pc98` | BIOS INT disk / image |
| `blk_virtio` | QEMU VirtIO-blk |

FS code calls only `read` / `write` by **logical block address**.

## 12. Embedding system volume (MCU / UEFI / PC-98)

**Do not** imprint random structs into `.data`.

1. Host tool builds a raw image:

```text
mkbtronfs manifest.txt -o btron_sys.vol
```

2. Embed or flash that image:

```text
.section .btron_vol, "a"
.incbin "btron_sys.vol"
```

MCU map example:

```text
[boot + kernel XIP]
[.btron_vol]     read-only system volume  → mount as /SYS
[user partition] optional writable volume
```

3. Boot sequence:

```text
blk_register_mem("mem0", vol_base, vol_bytes, 1024);
fs_mount("mem0", "/SYS");
```

Same `btron_sys.vol` bytes work on UEFI (load file), PC-98 (write sectors), and QEMU (VirtIO or file-backed).

### ROM vs writable (micro-BTRON style)

| Volume | Contents | Media |
|--------|----------|--------|
| System | Templates, fonts, RT_PROG system apps, help TAD | ROM / `.btron_vol` |
| User | Documents, TRASH, app data | Flash / disk / RAM disk |

## 13. Public vs internal headers (B-System)

```text
include/btron/
├── basic.h          # first include (types, SPEC_*, tad)
├── file.h           # public API: opn_fil, ins_rec, …
├── tad.h            # RT_*, TS_*, TAD_SEG_HDR only (no on-disk RecordIndex)
├── vobj.h           # high-level Real/Virtual Body API
└── fs/              # on-disk binary layouts only
    ├── fs_types.h   # block size, magics, 192/16 constants
    ├── volume.h     # system / volume header
    ├── header.h     # 192-byte FileHeader
    ├── record.h     # 16-byte RecordIndex, LinkRecord payload
    └── block.h      # fragment / extent helpers
```

**Name rule:** one on-disk record descriptor type — `RecordIndex` in `fs/record.h` only. Do not also define `RECORD_INDEX` in `tad.h`.

## 14. Implementation checklist (binary-compatible path)

### Phase A — Structures

- [ ] `VolumeHeader` (128 B system header fields)
- [ ] FID table entry (4 B)
- [ ] Short-name hash (4 B) + hash function
- [ ] `FileHeader` (192 B, flags word exact)
- [ ] Fragment entry (6 B) or documented interim extent map
- [ ] `RecordIndex` (16 B) + continuation rules
- [ ] Link-record payload (RT_LINK)
- [ ] Endian helpers (BE classic / LE quasi)

### Phase B — Block + mount

- [ ] `BlkDev` + memdisk + file backends
- [ ] Format empty volume (header, bitmaps, FID0 root)
- [ ] `fs_mount` / `fs_umount` / `fs_sync`
- [ ] Dirty / clean mount state

### Phase C — Record stream (`file.h`)

- [ ] `cre_fil` / `opn_fil` / `cls_fil` / `del_fil`
- [ ] `ins_rec` / `del_rec` / `rd_rec` / `wr_rec` / `pos_rec`
- [ ] Index level 0 first; then level 1
- [ ] `cre_lnk` / `del_lnk` via RT_LINK

### Phase D — Compatibility

- [ ] Read a volume written by Cho-Kanji / B-right/V (if available)
- [ ] Write a minimal volume and verify with `fsrcv`-class checks
- [ ] Root FID 0, name hash, bitmap free counts
- [ ] RT_PROG Real Body load path for CLI tools

### Phase E — Multi-target imprint

- [ ] `mkbtronfs` tool
- [ ] `.btron_vol` link step for MCU/UEFI images
- [ ] PC-98 sector image generation

## 15. Constants summary

```c
#define BTRON_BLOCK_SIZE      1024
#define BTRON_SYS_HDR_SIZE     128
#define BTRON_FILE_HDR_SIZE    192
#define BTRON_REC_IDX_SIZE      16
#define BTRON_FRAG_ENT_SIZE      6
#define BTRON_FID_ENT_SIZE       4
#define BTRON_NAME_HASH_SIZE     4
#define BTRON_MAX_NAME_BYTES    40

#define VOL_MAGIC_STD         0x42FE   /* standard format ID */
#define VOL_MAGIC_EXT         0x52FE   /* extended format ID */
#define FS_TYPE_STD           0x6400
#define FS_TYPE_EXT           0x6401

#define FID_ROOT                 0
#define REC_IDX_LEVEL0_MAX     512   /* rows a level-0 index can hold in core;
                                        above the 496 slots a B-right/V 8 KiB
                                        row area (from +0x100) can ever
                                        contain (§7.4). The clean-room 1 KiB
                                        header still fits only (1024-192)/16
                                        = 52 rows; writers clamp per volume. */
#define BVR_RIDX_AREA_START  0x100   /* B-right/V: first byte of the row area */
```

## 16. Mental model (for implementers)

```text
Volume     = block device + system area + file region
FID        = stable Real Body id → header block
Real Body  = FileHeader + fragment map + RecordIndex[] + data blocks
Record     = typed payload (link | TAD | program | font | …)
Virtual Body = RT_LINK (+ optional TS_VOBJ in TAD), not a separate inode table
CLI / app  = Real Body with RT_PROG, registered for launch — not ELF on volume
```

## 18. Real Working Examples & On-Disk Layout

### 18.1 Volume Hex Dump Breakdown (btron_sys.vol)

Below is the verified binary structure of block 0 (`btron_sys.vol`), formatted with `NFMAX=256`, `NLB=1024` (1024 KiB total):

```text
00000000: 42fe 6400 0000 0100 0000 0400 0001 0001  B.d.............
00000010: 0001 0000 0000 03ed 0000 0005 5359 5300  ............SYS.
00000020: 0000 0000 0000 0000 0000 0000 0000 0000  ................
...
00000070: 0000 0000 0000 0000 0000 0000 0000 0004  ................
```

- `0x00..0x01` : `0x42FE` — `VOL_MAGIC_STD` (BTRON3 volume identifier)
- `0x02..0x03` : `0x6400` — `FS_TYPE_STD`
- `0x04..0x07` : `0x00000100` — `NFMAX` = 256 files max
- `0x08..0x0B` : `0x00000400` — `NLB` = 1024 logical blocks (1 MiB volume)
- `0x0C..0x0D` : `0x0001` — `SFIDT` = 1 block for FID table (`256 * 4 / 1024`)
- `0x0E..0x0F` : `0x0001` — `SFNMT` = 1 block for hash table
- `0x10..0x11` : `0x0001` — `NBMP` = 1 block for allocation bitmap
- `0x12..0x13` : `0x0000` — `access_level` = 0
- `0x14..0x17` : `0x000003ED` — `free_blocks` = 1005 blocks free
- `0x18..0x1B` : `0x00000005` — `data_start` = logical block 5 (after system area)
- `0x1C..0x43` : `"SYS\0..."` — Volume label / root name (40 bytes)
- `0x7C..0x7F` : `0x00000004` — `hdr_blk` (root FileHeader logical block)

### 18.2 On-Disk Real Body Structure (FID 0 Root Container)

In BTRON, a folder or volume root is a Real Body whose record stream consists of `RT_LINK` records pointing to member Real Bodies:

```text
Logical Block 4: Root Container Header (FileHeader + RecordIndex)
┌───────────────────────────────────────────────────────────────┐
│ FileHeader (192 bytes)                                        │
│   +00: 0x1003 (FTYPE_NORMAL, FFLG_READ | FFLG_WRITE)          │
│   +04: ctime | +08: mtime | +12: atime (BTRON epoch)          │
│   +20: 0x0001 (nlnk) | +22: 0x0000 (idxlv = 0)                │
│   +24: 0x00000006 (nrec = 6 member files)                     │
│   +28: 0x00000138 (total_size = 312 bytes)                   │
│   +32: "SYS" (UTF-8 name, 40 bytes)                           │
│   +72: data_blk = 5 (first allocated extent block)           │
├───────────────────────────────────────────────────────────────┤
│ RecordIndex Table (40 entries × 16 bytes = 640 bytes)         │
│   [0] kind=0 type=0x0000 (RT_LINK) size=50 offset=0           │
│   [1] kind=0 type=0x0000 (RT_LINK) size=47 offset=50          │
│   [2] kind=0 type=0x0000 (RT_LINK) size=49 offset=97          │
│   [3] kind=0 type=0x0000 (RT_LINK) size=51 offset=146         │
│   [4] kind=0 type=0x0000 (RT_LINK) size=50 offset=197         │
│   [5] kind=0 type=0x0000 (RT_LINK) size=37 offset=247         │
├───────────────────────────────────────────────────────────────┤
│ Fragment Table / Padding (192 bytes)                          │
└───────────────────────────────────────────────────────────────┘
```

### 18.3 Binary Layout of an `RT_LINK` Record (Virtual Body)

Each link record payload is laid out deterministically:

```text
Offset  Size  Field         Example
0       4     target_fid    0x00000001 (FID of "BTRON Spec Book 1")
4       10    attr[5]       00 00  00 00  00 00  00 00  00 00
14      2     name_len      0x0011 (17 bytes)
16      N     name          "BTRON Spec Book 1"
```

### 18.4 Real CLU Interactive Shell Session (CLU.md Conformance)

The following transcript demonstrates the exact behavior verified by the test suite:

```shell
[/SYS]% df
PATH  DEV   TOTAL   FREE    USED  UNIT  MAXFILE  NAME
/SYS  mem0  1024K   1005K    1%   1024  256      SYS

[/SYS]% fs
NO: TYPE STYPE : SIZE / NAME
0:  0    0000  : BTRON Spec Book 1
1:  0    0000  : Cho-Kanji Guide
2:  0    0000  : Kernel Internals
3:  0    0000  : Graphics & Display
4:  0    0000  : Applications & HMI
5:  0    0000  : TRASH

[/SYS]% fs -l
NO: 0 STYPE : FID [ATR1 ATR2 ATR3 ATR4 ATR5] : NAME
0:  0 0000  : 1   [0000 0000 0000 0000 0000] : BTRON Spec Book 1
1:  0 0000  : 2   [0000 0000 0000 0000 0000] : Cho-Kanji Guide
2:  0 0000  : 3   [0000 0000 0000 0000 0000] : Kernel Internals
3:  0 0000  : 4   [0000 0000 0000 0000 0000] : Graphics & Display
4:  0 0000  : 5   [0000 0000 0000 0000 0000] : Applications & HMI
5:  0 0000  : 0   [0000 0000 0000 0000 0000] : TRASH

[/SYS]% fs "BTRON Spec Book 1"
NO: TYPE STYPE : SIZE / NAME
0:  1    0000  : 23

[/SYS]% fs -l "BTRON Spec Book 1"
NO: 0 STYPE : FID [ATR1 ATR2 ATR3 ATR4 ATR5] : NAME
0:  1 0000  :     (data record)

[/SYS]% tp "BTRON Spec Book 1"
BTRON Spec Book 1

[/SYS]% tp -x "BTRON Spec Book 1"
0000: FF E1 00 11 42 54 52 4F 4E 20 53 70 65 63 20 42
0010: 6F 6F 6B 20 31

[/SYS]% tp -a "BTRON Spec Book 1"
....BTRON Spec B
ook 1

[/SYS]% mkf "My Note"
Created 'My Note'

[/SYS]% ln "My Note" "Note Alias"
Link 'Note Alias' -> 'My Note'

[/SYS]% cp "My Note" "Note Backup"
Copied 'My Note' -> 'Note Backup'

[/SYS]% ren "Note Backup" "Note Archive"
Renamed 'Note Backup' -> 'Note Archive'

[/SYS]% chmod -a1 "Note Archive"
Attribute changed

[/SYS]% touch "Note Archive"
Touched 'Note Archive'

[/SYS]% rm "Note Alias"
Removed 'Note Alias'

[/SYS]% empf "Note Archive"
Emptied 'Note Archive'

[/SYS]% rm "Note Archive"
Removed 'Note Archive'

[/SYS]% cd "My Note"
[My Note]

[/SYS/My Note]% cd ..
[/SYS]

[/SYS]% rm "My Note"
Removed 'My Note'

[/SYS]% sync
(all caches flushed)
```

## 19. References

- BTRON3 Shared Data, Chapter 4 — Floppy / volume format (Personal Media / Cho-Kanji developer docs)  
- BTRON3 TAD specification — record types 0–31, segment IDs  
- B-right/V R4 library headers: `tad.h`, `basic.h` naming conventions  
- micro-BTRON (B-right) ROM vs flash practice: system in ROM, user data on secondary storage  

# Credits

Namdak Tonpa and Grok 4.5


