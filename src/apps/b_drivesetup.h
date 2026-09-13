/*
 * B-System BTRON3 — b_drivesetup.h
 * Clean-Room Authentic BTRON Window UI DriveSetup Application.
 *
 * Adheres to B-System HMI guidelines and NASA/JPL Rule 3 (no runtime heap allocations).
 * Production POSIX storage volume management and B-FS V2 filesystem inspector.
 */

#ifndef _B_DRIVESETUP_H_
#define _B_DRIVESETUP_H_

#include <btron/types.h>
#include <btron/fs/bfs_disk.h>
#include <btron/app_menu.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DRIVESETUP_MAX_DEVICES     16
#define DRIVESETUP_MAX_PARTITIONS  8
#define DRIVESETUP_NAME_LEN        32
#define DRIVESETUP_VISIBLE_DEVS    4
#define DRIVESETUP_VISIBLE_PARTS   4

typedef enum {
    PANE_DEVICES    = 0,
    PANE_PARTITIONS = 1
} DriveSetupPane;

typedef enum {
    DSCMD_NONE = 0,
    DSCMD_FILE_SCAN         = 10,
    DSCMD_FILE_CREATE_IMG   = 11,
    DSCMD_FILE_CLOSE        = 12,
    DSCMD_DISK_INIT_MBR     = 20,
    DSCMD_DISK_INIT_GPT     = 21,
    DSCMD_PART_CREATE       = 22,
    DSCMD_PART_DELETE       = 23,
    DSCMD_PART_FMT_BFS      = 24,
    DSCMD_MOUNT_VOLUME      = 30,
    DSCMD_UNMOUNT_VOLUME    = 31,
    DSCMD_MOUNT_SYS         = 32,
    DSCMD_MOUNT_ANDERS      = 33,
    DSCMD_MOUNT_QCOW2       = 34,
    DSCMD_VIEW_REFRESH      = 40,
    DSCMD_HELP_ABOUT        = 50
} DriveSetupCommand;

typedef enum {
    PART_SCHEME_NONE = 0,
    PART_SCHEME_MBR  = 1,
    PART_SCHEME_GPT  = 2
} PartitionScheme;

typedef enum {
    FS_UNKNOWN  = 0,
    FS_BFS_V1   = 1,
    FS_BFS_V2   = 2,
    FS_CHOKANJI = 3,
    FS_FAT32    = 4,
    FS_RAW      = 5
} FileSystemType;

/* Planet-Scale Partition & Filesystem Unique Identification Codes */
#define BTRON_PART_TYPE_CHOKANJI    0x13  /* Historical Personal Media BTRON / Chokanji MBR code */
#define BTRON_PART_TYPE_BFS_V1      0xB1  /* Unique Planet-Scale B-FS V1 MBR Partition Code */
#define BTRON_PART_TYPE_BFS_V2      0xB2  /* Unique Planet-Scale B-FS V2 MBR Partition Code */
#define BTRON_PART_TYPE_RAW         0x83  /* Standard RAW data slice */

#define BTRON_GPT_GUID_CHOKANJI     "706D636F-7270-4254-524E-000000000001"
#define BTRON_GPT_GUID_BFS_V1       "8B684653-BTRN-3100-BEEF-000000000001"
#define BTRON_GPT_GUID_BFS_V2       "8B684653-BTRN-3200-BEEF-000000000002"

#define BTRON_FS_CODE_BFS_V1        0x6400  /* Classic BTRON FS_TYPE_STD (Magic 0x42FE) */
#define BTRON_FS_CODE_CHOKANJI      0x6402  /* Chokanji B-right/V FS_TYPE_BRIGHTV (Magic 0x52FE) */
#define BTRON_FS_CODE_BFS_V2        0x6403  /* Modern B-FS V2 FS_TYPE_MODERN (Magic 0x62FE) */

typedef struct {
    char            dev_path[DRIVESETUP_NAME_LEN];   /* e.g. "btron_sys.vol" */
    char            label[DRIVESETUP_NAME_LEN];      /* Volume label, e.g. "SYS" */
    uint8_t         type_code;                       /* 0xB1 (V1), 0xB2 (V2), 0x13 (Chokanji), 0x83 (RAW) */
    FileSystemType  fs_type;
    uint64_t        start_lba;
    uint64_t        block_count;
    uint32_t        block_size;                      /* 1024, 2048, 4096, 8192 */
    uint32_t        btree_node_size;                 /* 1024, 2048, 4096, 8192 */
    bool            mounted;
    bool            dirty;
    uint32_t        features;                        /* FEAT_* flags */
    uint32_t        journal_blocks;
    uint32_t        vector_dim;
    uint64_t        active_fids;
    uint64_t        free_blocks;
    uint64_t        total_fids;
    char            mount_point[DRIVESETUP_NAME_LEN];/* e.g. "/SYS", "/STORAGE_DEV", "Unmounted" */
    void           *vol_handle;                      /* Attached Volume* handle */
} DriveSetupPartition;

typedef struct {
    char                raw_path[DRIVESETUP_NAME_LEN]; /* e.g. "btron_sys.vol" */
    char                model[DRIVESETUP_NAME_LEN];    /* e.g. "B-System /SYS Volume" */
    uint64_t            total_bytes;
    uint32_t            sector_size;                   /* 512, 1024, 4096 */
    PartitionScheme     scheme;
    int                 partition_count;
    DriveSetupPartition partitions[DRIVESETUP_MAX_PARTITIONS];
} DriveSetupDevice;

typedef enum {
    DIALOG_NONE = 0,
    DIALOG_INIT_DISK,
    DIALOG_CREATE_SLICE,
    DIALOG_CREATE_IMAGE,
    DIALOG_FORMAT_BFS,
    DIALOG_WARN_WRITE
} DriveSetupDialog;

typedef enum {
    WRITE_OP_NONE = 0,
    WRITE_OP_INIT_DEVICE,       /* Device-level write: Initialize disk with MBR/GPT */
    WRITE_OP_FORMAT_VOLUME,     /* Volume-level write: Format with B-FS */
    WRITE_OP_DELETE_PARTITION   /* Partition-level write: Delete partition slice */
} WriteOperationType;

typedef struct {
    int                 device_count;
    DriveSetupDevice    devices[DRIVESETUP_MAX_DEVICES];
    int                 selected_dev_idx;
    int                 selected_part_idx;
    DriveSetupPane      active_pane;          /* PANE_DEVICES or PANE_PARTITIONS */

    /* Storage Devices List scrollbar state (4 items high viewport) */
    int                 dev_scroll_offset;    /* 0..max(0, device_count - 4) */
    bool                sb_dragging;          /* True while dragging scroll thumb */
    int                 sb_drag_start_y;      /* Initial mouse Y on thumb drag */
    int                 sb_drag_start_offset; /* Initial scroll offset on thumb drag */

    /* Partitions & Slices Table scrollbar state (fixed 4 items high viewport) */
    int                 part_scroll_offset;   /* 0..max(0, partition_count - 4) */
    bool                part_sb_dragging;     /* True while dragging partition scroll thumb */
    int                 part_sb_drag_start_y;
    int                 part_sb_drag_start_offset;

    DriveSetupDialog    active_dialog;
    int                 dlg_focus_idx;        /* Currently focused control in active dialog */
    char                dlg_text_buf[64];     /* Text input for dialogs */
    int                 dlg_radio_sel1;       /* First radio group selection */
    int                 dlg_radio_sel2;       /* Second radio group selection */
    int                 dlg_radio_sel3;       /* Third radio group selection */
    uint32_t            dlg_check_flags;      /* Checkbox bit flags */
    char                status_msg[128];

    /* Unified Write Warning Confirmation Context */
    WriteOperationType  pending_write_op;
    char                pending_target[64];
    char                pending_warn_msg[128];
    int                 pending_scheme;
    FileSystemType      pending_fmt_type;     /* FS_BFS_V1 or FS_BFS_V2 */
    uint32_t            pending_fmt_bsz;
    uint32_t            pending_fmt_tsz;
    uint32_t            pending_fmt_feat;
    char                pending_fmt_label[DRIVESETUP_NAME_LEN];

    /* Production in-window menu bar (same pattern as gterm / tad_browser) */
    APP_MENU_BAR        menu_bar;
} DriveSetupState;

/* ── Lifecycle & Operations API ─────────────────────────────────── */

void b_drivesetup_init(DriveSetupState *st);
bool b_drivesetup_verify_invariants(const DriveSetupState *st);
DriveSetupState* b_drivesetup_get_state(void);
extern DriveSetupState g_drivesetup_state;

/* Scans real POSIX storage volumes and backing files */
void b_drivesetup_scan_devices(DriveSetupState *st);

bool b_drivesetup_select_device(DriveSetupState *st, int dev_idx);
bool b_drivesetup_select_partition(DriveSetupState *st, int part_idx);
void b_drivesetup_scroll(DriveSetupState *st, int delta);
void b_drivesetup_scroll_partitions(DriveSetupState *st, int delta);
void b_drivesetup_handle_cmd(DriveSetupState *st, int cmd);

/* Actions */
bool b_drivesetup_init_disk(DriveSetupState *st, int dev_idx, PartitionScheme scheme);
bool b_drivesetup_delete_partition(DriveSetupState *st, int dev_idx, int part_idx);
bool b_drivesetup_create_slice(DriveSetupState *st, int dev_idx, const char *label, uint64_t size_bytes);
bool b_drivesetup_create_disk_image(DriveSetupState *st, const char *path, uint64_t size_bytes);
bool b_drivesetup_create_disk_image_typed(DriveSetupState *st, const char *path, uint64_t size_bytes, FileSystemType fs_type);
bool b_drivesetup_format_bfs(DriveSetupState *st, int dev_idx, int part_idx,
                             const char *name, uint32_t block_sz, uint32_t btree_sz,
                             uint32_t journal_mb, uint32_t features, uint32_t vec_dim);
bool b_drivesetup_format_v1(DriveSetupState *st, int dev_idx, int part_idx,
                            const char *name, uint32_t block_sz);
bool b_drivesetup_mount(DriveSetupState *st, int dev_idx, int part_idx);
bool b_drivesetup_unmount(DriveSetupState *st, int dev_idx, int part_idx);

/* Modal Dialog Lifecycle & Keyboard Navigation */
void b_drivesetup_open_dialog(DriveSetupState *st, DriveSetupDialog dlg);
void b_drivesetup_open_warn_dialog(DriveSetupState *st, WriteOperationType op, const char *target, const char *msg);
void b_drivesetup_close_dialog(DriveSetupState *st);
bool b_drivesetup_commit_dialog(DriveSetupState *st);
bool b_drivesetup_handle_dialog_key(DriveSetupState *st, uint32_t key);

/* Pure BTRON GUI Window & Event Loop */
#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/event.h>

WND* open_drivesetup_window(void);
void drivesetup_paint(WND *wnd, GDEV *dev);
void drivesetup_event_handler(WND *wnd, const EVT *evt);

/* Production menu helpers */
bool drivesetup_is_menu_open(void);

/* Responsive Dynamic Layout Geometry Engine */
typedef struct {
    int w, h;

    /* 1. Storage Devices List (fixed 4 items height) */
    RECT dev_box;
    int sb_x, sb_y, sb_w, sb_h, dy_b, track_top, track_h, thumb_h;

    /* 2. Visual Disk Slice Map */
    RECT slice_bar;

    /* 3. Partitions Table (fixed 4 items height + virtual scrollbar) */
    RECT tbl_r;
    int col_dev, col_type, col_fs, col_size, col_stat;
    int part_sb_x, part_sb_y, part_sb_w, part_sb_h, part_dy_b, part_track_top, part_track_h, part_thumb_h;

    /* 4. Volume Details Inspector Card */
    RECT insp_r;

    /* 5. Action Buttons (evenly distributed horizontally across width) */
    RECT btn1, btn2, btn3, btn4;

    /* 6. Status Bar */
    RECT sb_stat;
} DS_Layout;

void drivesetup_calc_layout(int w, int h, int dev_count, int part_count, DS_Layout *lo);

#ifdef __cplusplus
}
#endif

#endif /* _B_DRIVESETUP_H_ */
