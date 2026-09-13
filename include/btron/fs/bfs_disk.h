/*
 * B-System BTRON3 Filesystem — bfs_disk.h
 * Cleanroom on-disk structures for B-FS File System (BeFS/BFS inspired).
 *
 * Supports:
 *  - Direct 64-bit FIDs (block_run_64)
 *  - Dual-Anchor Superblock (Block 0: VolumeHeaderV2 128B; Block 1: bfs_super_block_v2 512B)
 *  - 512-byte Inode with inline small_data / Real-Body records (RT_TADDATA, RT_VECTOR)
 *  - Parameterized B+Tree nodes (1024, 2048, 4096 bytes)
 */

#ifndef _BTRON_FS_BFS_DISK_H_
#define _BTRON_FS_BFS_DISK_H_

#include <btron/types.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── 1. 64-bit Block Run & FID Addressing ───────────────────────── */

typedef struct __attribute__((packed)) {
    int64_t  allocation_group; /* up to 48-bit allocation group index */
    uint16_t start;            /* 16-bit block offset in AG (0..65535) */
    uint16_t length;           /* contiguous run length */
} block_run_64;

typedef uint64_t fid64_t;

#define BFS_DEFAULT_AG_SHIFT    16
#define BFS_MAX_RUN_LENGTH      65535

#define FID64_ENCODE(ag, start, ag_shift) \
    (((uint64_t)(ag) << (ag_shift)) | (uint64_t)(start))

#define FID64_TO_AG(fid, ag_shift) \
    ((int64_t)((fid) >> (ag_shift)))

#define FID64_TO_START(fid, ag_shift) \
    ((uint16_t)((fid) & (((uint64_t)1 << (ag_shift)) - 1)))

/* ── 2. Superblock Block 0: VolumeHeaderV2 (128 bytes) ──────────── */

#define VOL_MAGIC_V2        0x62FE
#define FS_TYPE_MODERN      0x6403

#define FEAT_JOURNAL        (1u << 0)
#define FEAT_CHECKSUM       (1u << 1)
#define FEAT_EXTENTS        (1u << 2)
#define FEAT_LARGE_FID      (1u << 3)
#define FEAT_VECTOR         (1u << 4)
#define FEAT_SNAPSHOT       (1u << 5)
#define FEAT_FREE_TREE      (1u << 6)

typedef struct __attribute__((packed)) {
    UH   magic;           /* +0   2 B  VOL_MAGIC_V2 (0x62FE) */
    UH   fs_type;         /* +2   2 B  FS_TYPE_MODERN (0x6403) */
    UW   nfmax;           /* +4   4 B  soft limit max files */
    UW   nlb;             /* +8   4 B  32-bit logical blocks shadow */
    UH   sfidt;           /* +12  2 B  FID-table blocks */
    UH   sfnmt;           /* +14  2 B  short-name blocks */
    UH   nbmp;            /* +16  2 B  bitmap blocks per AG */
    UB   access_level;    /* +18  1 B  access level */
    UB   dirty;           /* +19  1 B  0=clean, 1=dirty, 2=replay */
    UW   free_blocks;     /* +20  4 B  free blocks (shadow) */
    UW   data_start;      /* +24  4 B  block# of data start */
    UB   vol_name[40];    /* +28 40 B  UTF-8 volume name */

    /* ── V2 extensions (offsets 68–127) ── */
    UW   features;        /* +68  4 B  FEAT_* bitmask */
    UW   journal_start;   /* +72  4 B  journal first block */
    UW   journal_blocks;  /* +76  4 B  journal block count */
    UW   checksum_algo;   /* +80  4 B  0=none, 1=CRC32C, 2=xxHash64 */
    UW   uuid[4];         /* +84 16 B  128-bit UUID */
    UW   generation;      /* +100 4 B  transaction generation */
    UW   vector_index;    /* +104 4 B  vector index root block */
    UW   free_space_root; /* +108 4 B  free-space tree root block */
    UB   _reserved[16];   /* +112 16 B reserved, zero (total 128 B) */
} VolumeHeaderV2;

/* ── 3. Superblock Block 1: bfs_super_block_v2 (512 bytes) ──────── */

#define BFS_SUPER_MAGIC1    0x42465331  /* 'BFS1' */
#define BFS_SUPER_MAGIC2    0xdd121031
#define BFS_SUPER_MAGIC3    0x15b6830e

#define BFS_DISK_CLEAN      0x434c454e  /* 'CLEN' */
#define BFS_DISK_DIRTY      0x44495254  /* 'DIRT' */

typedef struct __attribute__((packed)) {
    char         name[32];             /* Volume name */
    int32_t      magic1;               /* BFS_SUPER_MAGIC1 */
    int32_t      fs_byte_order;        /* host endian flag */
    uint32_t     block_size;           /* 1024, 2048, 4096 */
    uint32_t     block_shift;          /* log2(block_size) */
    int64_t      num_blocks;           /* true 64-bit block count */
    int64_t      used_blocks;          /* true 64-bit allocated count */
    int32_t      inode_size;           /* 512 */
    int32_t      magic2;               /* BFS_SUPER_MAGIC2 */
    int32_t      blocks_per_ag;        /* typically 65,536 */
    int32_t      ag_shift;             /* 16 */
    int32_t      num_ags;              /* up to 2^48 */
    int32_t      flags;                /* clean / dirty status */
    block_run_64 log_blocks;           /* circular redo journal block run */
    int64_t      log_start;            /* journal read ptr */
    int64_t      log_end;              /* journal write ptr */
    int32_t      magic3;               /* BFS_SUPER_MAGIC3 */
    block_run_64 root_dir;             /* direct 64-bit root dir FID */
    block_run_64 vector_index;         /* B+Tree vector index root */
    int32_t      _reserved[8];
    int32_t      pad_to_block[84];     /* pad to exactly 512 bytes */
} bfs_super_block_v2;

/* ── 4. Inode Structure: bfs_inode_v2 (512 bytes) ────────────────── */

#define BFS_INODE_MAGIC1        0x3bbe0ad9
#define BFS_NUM_DIRECT_BLOCKS   12

typedef struct __attribute__((packed)) {
    block_run_64 direct[BFS_NUM_DIRECT_BLOCKS];
    int64_t      max_direct_range;
    block_run_64 indirect;
    int64_t      max_indirect_range;
    block_run_64 double_indirect;
    int64_t      max_double_indirect_range;
    int64_t      size;
} bfs_data_stream_v2;

typedef struct __attribute__((packed)) {
    uint32_t type;                     /* BTRON record type: RT_TADDATA, RT_VECTOR */
    uint16_t name_size;
    uint16_t data_size;
    char     name_and_data[0];
} bfs_small_data;

typedef struct __attribute__((packed)) {
    int32_t            magic1;         /* BFS_INODE_MAGIC1 */
    block_run_64       inode_num;      /* this inode's direct 64-bit FID */
    int32_t            uid;
    int32_t            gid;
    int32_t            mode;
    int32_t            flags;
    int64_t            create_time;
    int64_t            last_modified_time;
    block_run_64       parent;         /* parent directory FID */
    block_run_64       attributes;     /* attribute directory FID */
    uint32_t           type;           /* file / directory / link type */
    int32_t            inode_size;     /* 512 bytes */
    uint32_t           etc;
    bfs_data_stream_v2 data;
    int64_t            status_change_time;
    int32_t            pad[2];
    uint8_t            small_data_start[212]; /* inline small_data / records (total 512 B) */
} bfs_inode_v2;

/* ── 5. Parameterized B+Tree Structures ─────────────────────────── */

#define BPLUSTREE_MAGIC         0x69f6c2e8
#define BPLUSTREE_NULL          (-1LL)

#define BPLUSTREE_NODE_1K       1024
#define BPLUSTREE_NODE_2K       2048
#define BPLUSTREE_NODE_4K       4096

typedef struct __attribute__((packed)) {
    uint32_t magic;                    /* BPLUSTREE_MAGIC */
    uint32_t node_size;                /* 1024, 2048, 4096 */
    uint32_t max_number_of_levels;
    uint32_t data_type;                /* 0=String, 1=Int64, 2=VectorId */
    int64_t  root_node_pointer;        /* 64-bit block LBA */
    int64_t  free_node_pointer;
    int64_t  maximum_size;
} bplustree_header_v2;

typedef struct __attribute__((packed)) {
    int64_t  left_link;                /* sibling leaf link (-1 if none) */
    int64_t  right_link;               /* sibling leaf link (-1 if none) */
    int64_t  overflow_link;            /* internal node link (-1 if leaf) */
    uint16_t all_key_count;            /* number of keys in node */
    uint16_t all_key_length;           /* total byte length of all keys */
    /* followed by:
     *   uint16_t key_lengths[all_key_count]
     *   int64_t  values[all_key_count] (or block_run_64 / fid64_t)
     *   uint8_t  keys[all_key_length]
     */
} bplustree_node_v2;

/* ── 6. Static Size Assertions ──────────────────────────────────── */

#ifndef __cplusplus
_Static_assert(sizeof(block_run_64) == 12, "block_run_64 must be 12 bytes");
_Static_assert(sizeof(VolumeHeaderV2) == 128, "VolumeHeaderV2 must be 128 bytes");
_Static_assert(sizeof(bfs_super_block_v2) == 512, "bfs_super_block_v2 must be 512 bytes");
_Static_assert(sizeof(bfs_inode_v2) == 512, "bfs_inode_v2 must be 512 bytes");
_Static_assert(sizeof(bplustree_header_v2) == 40, "bplustree_header_v2 must be 40 bytes");
_Static_assert(sizeof(bplustree_node_v2) == 28, "bplustree_node_v2 base must be 28 bytes");
#endif

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_FS_BFS_DISK_H_ */
