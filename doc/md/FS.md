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
│   └── Record index         (level 0: up to 40 × 16-byte entries in header)
├── Index blocks             (if index level 1 or 2)
├── Indirect blocks          (level 2)
└── Data blocks              (concatenated record payloads)
```

**Index level**

| Level | Max record index entries (approx.) | Where index lives |
|-------|-------------------------------------|-------------------|
| 0 | 40 | Header block |
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
#define REC_IDX_LEVEL0_MAX      40
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

## 17. References

- BTRON3 Shared Data, Chapter 4 — Floppy / volume format (Personal Media / Cho-Kanji developer docs)  
- BTRON3 TAD specification — record types 0–31, segment IDs  
- B-right/V R4 library headers: `tad.h`, `basic.h` naming conventions  
- micro-BTRON (B-right) ROM vs flash practice: system in ROM, user data on secondary storage  

# Credits

Namdak Tonpa and Grok 4.5

