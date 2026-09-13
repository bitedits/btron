# B-System B-FS File System Architecture Specification V2

Document version: 2.0  
Target: B-System (BTRON 3.20 clean-room)  
Inspirations: Haiku OS BeFS/BFS, Disk Device Manager (DDM), and DriveSetup  
Date: 2026-09-13  

## 1. Executive Architecture Overview

B-FS is the next-generation, high-assurance file system for B-System (BTRON 3.20). It synthesizes the high performance and metadata capabilities of Haiku's BeFS/BFS with the BTRON Real-Body / Virtual-Object (TAD) model, native vector embedding storage (`RT_VECTOR`), and verifiable formal contracts.

```
┌──────────────────────────────────────────────────────────────────────────────────┐
│                             B-System Desktop & Apps                              │
│         clu (shell) │ tad_browser │ t_editor │ b_drivesetup (Disk Setup)         │
├──────────────────────────────────────────────────────────────────────────────────┤
│ Level 3: BTRON File System Interface (B-VFS)                                     │
│  - opn_fil, cls_fil, rea_rec, wri_rec, fil_set_vectors, vol_vector_search         │
│  - Vnode lifecycle, Entry cache, Transaction context                             │
├──────────────────────────────────────────────────────────────────────────────────┤
│ Level 2: Inode, Data Stream & B+Tree Layer                                       │
│  - Real-Body Inode: bfs_inode_v2 (small_data inline records + RT_VECTOR records) │
│  - Data Stream: 12 Direct Runs, 1 Indirect Run, 1 Double Indirect Run            │
│  - B+Tree Engine: Directory Index, Attribute Index, Vector HNSW/ANN Leaf Links   │
├──────────────────────────────────────────────────────────────────────────────────┤
│ Level 1: Core Volume & Storage Allocation Engine                                 │
│  - Superblock (Dual-Anchor: 128B V2 Header + 512B Extended BFS SuperBlock)       │
│  - Block Allocator: 64-bit Allocation Groups (AGs), Bitmaps, Buddy allocator     │
│  - Journaling (WAL): Circular log, Transaction commit, Redo replay, Crash safety│
├──────────────────────────────────────────────────────────────────────────────────┤
│ Level 0: BTRON Device Manager (BDM) & Block Abstraction                         │
│  - BTRON Device Registry (opn_dev_mgr, rea_dev, wri_dev)                         │
│  - Partition Slicing (MBR 0x13, GPT B-FS GUID, blk_partition_create)             │
│  - BlkDev Abstraction (blk_mem, blk_file, blk_qcow2, physical NVMe/AHCI)        │
└──────────────────────────────────────────────────────────────────────────────────┘
```

## 2. Level 0: Storage Hardware & BTRON Device Manager (BDM)

Haiku's `disk_device_manager` (`KDiskDeviceManager`, `KDiskDevice`, `KPartition`, `KFileSystem`, `KPartitioningSystem`) maps directly to the BTRON clean-room architecture:

### 2.1 Component Mapping

1. **Raw Storage Devices (`KDiskDevice`)**:
   - Represented by base `BlkDev` instances (`blk_file`, `blk_qcow2`, `blk_mem`, AHCI / NVMe drivers).
   - Paths: `/dev/disk/raw/<index>` (e.g. `/dev/disk/raw/0`).
2. **Partitioning Systems (`KPartitioningSystem`)**:
   - Master Boot Record (MBR): Scans partition table at offset 446 (0x1BE). Detects type `0x13` (BTRON) and `0x14` (B-FS V2).
   - GUID Partition Table (GPT): Scans primary GPT header at LBA 1. Detects B-FS partition GUID `8B684653-BTRN-3200-BEEF-000000000002`.
   - Accounts for 8-sector BTRON IPL boot sector reservation.
3. **Partition Slices (`KPartition`)**:
   - Sliced `BlkDev` created via `blk_partition_create(parent, start_lba, nblocks, block_size)`.
   - Paths: `/dev/disk/<disk_id>/<slice_id>` (e.g. `/dev/disk/0/0`).
4. **File System Drivers (`KFileSystem`)**:
   - B-FS probe: Reads Block 0 and Block 1, verifies magic numbers (`0x62FE` and `'BFS1'`).

## 3. Level 1: Volume, Allocation Groups & Redo Journaling

### 3.1 Dual-Anchor Superblock

To preserve total compatibility with classic BTRON boot loaders while enabling full 64-bit BeFS features, B-FS V2 places two complementary headers on disk:

1. **Block 0 (offset 0, 128 bytes)**: `VolumeHeaderV2`
   - Keeps legacy BTRON fields at offsets 0..67 (`magic = 0x62FE`, `fs_type = 0x6403`).
   - Feature flags at offset 68 (`FEAT_JOURNAL`, `FEAT_LARGE_FID`, `FEAT_VECTOR`).
2. **Block 1 / Offset 512 (512 bytes)**: `bfs_super_block_v2`
   - Extended 64-bit superblock: `num_blocks` (64-bit), `used_blocks` (64-bit), `blocks_per_ag` (typically 65,536), `num_ags` (up to $2^{48}$), `log_blocks` (`block_run_64`), `root_dir` (`block_run_64`), `vector_index` (`block_run_64`).

### 3.2 Direct 64-bit File Identifiers (FIDs)

In B-FS, an Inode is addressed directly by its 64-bit physical location:
$$\text{FID}_{64} = (\text{allocation\_group} \ll \text{ag\_shift}) \mid \text{start\_block\_in\_ag}$$
- Max Allocation Groups: $2^{48}$
- Blocks per AG: $2^{16} = 65,536$
- Total addressable blocks: $2^{48} \times 2^{16} = 2^{64}$ blocks ($16 \text{ ZiB}$).
- Inode allocation guarantees that any valid FID directly maps to the block containing that Inode's `bfs_inode_v2`.

### 3.3 Write-Ahead Redo Journal (WAL)

- Fixed circular area on disk allocated at format time.
- All metadata mutations (Inode updates, B+Tree node allocations/splits, bitmap block bit flips) write to the log buffer first.
- Transactions are bounded and committed atomically with `tx_commit` records containing checksum and generation counter.
- On dirty mount, replay scans from `log_start` to `log_end`, applies all committed block mutations, and clears the dirty flag.

## 4. Level 2: Inodes, Data Streams, and Parameterized B+Tree

### 4.1 Inode Structure (`bfs_inode_v2`)

Each inode is 512 bytes:
- Header: `magic = 0x3bbe0ad9`, `inode_num` (`block_run_64`), timestamps, mode, UID/GID.
- `data_stream`: 12 direct `block_run_64` entries, 1 indirect run, 1 double indirect run.
- Inline `small_data` area: Directly stores BTRON Real-Body records:
  - `RT_TADDATA` / `RT_TEXT`: Primary document content for small files (< ~350 bytes).
  - `RT_VECTOR`: Embedding vectors, dimension, metric, quantization flag.
  - Large data automatically spills over into the `data_stream` block runs.

### 4.2 Parameterized B+Tree Engine

Directories, attribute indices, and vector search indices are backed by a scalable B+Tree:
- **Node Size**: Parameterized (1024 B, 2048 B, or 4096 B).
- **Node Structure**:
  - 64-bit sibling links: `left_link` and `right_link` for $O(1)$ leaf-level range scans.
  - `overflow_link` for non-leaf internal routing.
  - Ordered keys array with binary search ($O(\log N)$).
  - Values array storing 64-bit target FIDs (`fid64_t`).
- **Invariants**:
  - Tree depth uniformity (all leaf nodes at identical height $H$).
  - Strict key sorting: $\forall i < j, k_i < k_j$.
  - Split preservation: Splitting a full node moves half the keys to a newly allocated sibling and promotes the median key to the parent without data loss.

## 5. Level 3: VFS & BTRON File Interface

B-FS seamlessly exposes BTRON system calls:
- `opn_fil(name, mode)`: Walks directory B+Tree from root FID (`0`), locates target `fid64_t`, reads `bfs_inode_v2`.
- `cre_rec(fid, type, size)`: Appends record to inline small data or stream extents.
- `fil_set_vectors(fid, hdr, data)`: Inserts vector into `RT_VECTOR` record and indexes in vector B+Tree.
- `vol_vector_search(vol, query, dim, k, hits)`: Traverses vector B+Tree index and ranks top-$k$ nearest neighbors.

## 6. Level 4: Minimal B-System DriveSetup (`b_drivesetup`)

`b_drivesetup` is a minimal, robust management application built for the BTRON HMI:
- **Zero Runtime Heap Allocation**: Static state machine complying with NASA/JPL Rule 3.
- **Visual Disk Bar**: Renders physical disk slices graphically with color-coded partition states.
- **Partition Table**: Inspects slice types, filesystems, sizes, and active features (`JRNL`, `VEC`, `64B`).
- **Modal Dialogs**: Format B-FS V2, Initialize MBR/GPT, Mount/Unmount with live journal replay feedback.

# Credits

Namdak Tonpa and Grok 4.5
