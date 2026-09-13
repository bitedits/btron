# B-System BTRON3 — DriveSetup Application Specification (DRIVESETUP.md)

**Document Version**: 2.1  
**Target Subsystem**: Storage & Volume Management (`src/apps/b_drivesetup.c`, `src/apps/b_drivesetup.h`)  
**Design Guidelines**: Authentic BTRON3 Window UI, B-System HMI Standards, NASA/JPL Rule 3 (100% Static Non-Allocating Memory Model)  

## 1. Executive Summary & Purpose

`b_drivesetup` is the official clean-room graphical storage device and filesystem management application for BTRON3. It provides an interactive workstation interface for discovering physical disks, raw disk image files, partitioning schemes (MBR & GPT), and formatting/inspecting BTRON Filesystem (B-FS V1 & V2) volumes.

### Key Architectural Tenets

1. **Pure Graphical BTRON Window UI**:
   - ASCII representations are **design specifications and wireframe blueprints only**.
   - The runtime implementation is **100% native 2D/3D graphical BTRON Window UI** using `opn_wnd`, `drw_rec`, `drw_lin`, `fill_rec`, `drw_tc_string`, and beveled 3D borders (`DS_COL_BG`, `DS_COL_BORDER_HI`, `DS_COL_BORDER_LO`).
   - No terminal escape codes, text mock renderers, or ASCII text dumping.
2. **Production POSIX Storage Quality (Zero Mocks)**:
   - Device scanning (`b_drivesetup_scan_devices`) must inspect **real mounted volumes** (`g_sys_vol`, `g_anders_vol`, `g_chokanji_vol`) and **real host backing image files** (`btron_sys.vol`, `btron_anders.vol`, `hda.qcow2`, `*.vol`).
   - **Never return fabricated mock hardware** (`/dev/disk/raw/0`, `ATA QEMU HARDDISK`, `NVMe BTRON FAST-STORE`). If no storage devices or disk image files exist on the host, the device count is `0`.
3. **NASA/JPL Rule 3 Compliance**:
   - 100% static memory allocation model. All state structures (`DriveSetupState`, `DriveSetupDevice`, `DriveSetupPartition`) have compile-time fixed bounds (`DRIVESETUP_MAX_DEVICES = 16`, `DRIVESETUP_MAX_PARTITIONS = 8`).
   - Zero dynamic heap allocation (`malloc`, `calloc`, `free`) during application lifecycle or window resizing.
4. **Responsive Dynamic Resizing (`WND_ATTR_RESIZE`)**:
   - The window supports freeform resizing (`WND_ATTR_RESIZE`) with minimum bounds 520x420 and default 740x512.
   - Dynamic geometry engine (`drivesetup_calc_layout`) ensures strict 10px outer margins, fixed 4-item viewports for both devices and partitions, proportional column scaling, anti-overflow clipping, and centered modal dialogs.
5. **Real End-to-End System Integration**:
   - Creating a disk image creates an **actual file on the host filesystem** (`blk_file_create`).
   - Creating a slice defines real partition geometry.
   - Formatting with B-FS writes real filesystem superblocks and FID tables (`vol_format`).
   - Mounting connects the volume to BTRON's active volume table (`vol_mount`, `g_anders_vol`), making it immediately accessible via `/ANDERS/...`.

## 2. Window Architecture & Visual Layout

### 2.1 Visual Geometry Structure

```
+-------------------------------------------------------------------------------+
| [b_drivesetup] B-System DriveSetup v2.1                            [-] [^] [X] |
+-------------------------------------------------------------------------------+
| ファイル(F)  ディスク(D)  マウント(M)  表示(V)  ヘルプ(H)      | B-FS V2 64-bit |
+-------------------------------------------------------------------------------+
| 物理記憶装置 (Storage Devices) [●]:                                            |
| +---------------------------------------------------------------------------+ |
| | [DISK] btron_sys.vol       B-System /SYS Volume (B-FS V1)         [ MBR ]|^| |
| | [DISK] btron_anders.vol    B-System /ANDERS Volume (B-FS V1)      [ MBR ]|#| |
| | [DISK] hda.qcow2           B-right/V 4.02 /CHOKANJI (585.1 MiB)   [ MBR ]| | |
| | [DISK] extra_drive.vol     POSIX Raw Image (64.0 MiB)             [ MBR ]|v| |
| +---------------------------------------------------------------------------+ |
|                                                                               |
| 区画マップ Visual Layout (btron_sys.vol):                                     |
| +---------------------------------------------------------------------------+ |
| | [1] SYS (1.0M) B-FS V1 Standard               | [Free] (Unallocated)      | |
| +---------------------------------------------------------------------------+ |
|                                                                               |
| 区画一覧 Partitions & Slices:                                                 |
| +---------------------------------------------------------------------------+ |
| | Device/Slice    | Type      | Filesystem | Size      | Status   | Features |^|
| |-----------------+-----------+------------+-----------+----------+----------|-|
| | btron_sys.vol:s0| MBR-0xB1  | B-FS V1    | 1.0 MiB   | * Mounted|StandardV1|#|
| | btron_sys.vol:s1| MBR-0x83  | RAW        | 512.0 KiB | Unmounted|RawData   | |
| |                 |           |            |           |          |          | |
| |                 |           |            |           |          |          |v|
| +---------------------------------------------------------------------------+ |
|                                                                               |
| 区画詳細 Volume & Partition Details:                                          |
| +---------------------------------------------------------------------------+ |
| | Label:       "SYS"                   | Block Size:   1024 bytes           | |
| | Start LBA:   2048                    | B+Tree Node:  N/A (V1 FID Map)     | |
| | Blocks:      1024 (1.0 MiB)          | Active FIDs:  12 (32-bit)          | |
| | Alloc Grps:  N/A (Flat V1)           | Journal WAL:  None (V1 Volume)     | |
| +---------------------------------------------------------------------------+ |
|                                                                               |
| [ ディスク初期化... ] [ 区画作成... ] [ B-FS フォーマット... ] [ 接続切替 ]   |
| +---------------------------------------------------------------------------+ |
| | Discovered 3 active POSIX volume(s)                                       | |
| +---------------------------------------------------------------------------+ |
+-------------------------------------------------------------------------------+
```

### 2.2 Layout Geometry Engine (`drivesetup_calc_layout`)

- **Outer Margin**: Exactly 10px on Left, Right, and Bottom borders.
- **Top Margin**: 22px reserved for in-window `APP_MENU_BAR` (`y = 0..22`).
- **Storage Devices Viewport**: Fixed height of exactly 4 visible items (`80px` list height + 4px beveled border = 84px, `y = 44..128`).
- **Visual Disk Slice Map**: Positioned at `y = 152..192` (height 40px), rendering proportional slice widths based on sector/block count.
- **Partitions & Slices Table**: Fixed height of exactly 4 visible items (`22px` header + `80px` rows + 4px border = 106px, `y = 212..318`).
- **Partition Details Inspector**: Positioned at `lo.tbl_r.bottom + 8` (`y = 326..406`, height 80px).
- **Action Buttons**: 4 buttons evenly distributed horizontally across `w - 20` with 8px spacing, positioned at `h - 52`.
- **Status Bar**: Anchored to bottom at `h - 22`.

## 3. Dual Pane Navigation, Virtual Scrollbars & Focus Management

DriveSetup features dual independent viewports: **Physical Storage Devices** (`PANE_DEVICES = 0`) and **Partitions & Slices** (`PANE_PARTITIONS = 1`).

### 3.1 Dual 4-Item High Viewports

- Both lists display **exactly 4 items at a time** (`DRIVESETUP_VISIBLE_DEVS = 4`, `DRIVESETUP_VISIBLE_PARTS = 4`).
- Each table row is 20px high.
- Selected rows are highlighted with `DS_COL_SEL_BG` (Navy `0xFF000080`) and white text.

### 3.2 Vertical Scrollbar Architecture

Each viewport features an independent classic BTRON 3D vertical scrollbar (`sb_w = 16px`):
- **Up Arrow Button**: Top 16x16 3D button with upward triangle glyph (`^`).
- **Down Arrow Button**: Bottom 16x16 3D button with downward triangle glyph (`v`).
- **Scroll Track**: Inset groove between the up and down buttons.
- **Elevator / Thumb**: Proportional height:
  $$\text{thumb\_h} = \max\left(12, \frac{4 \times \text{track\_h}}{\text{item\_count}}\right)$$
- **Scroll Interactions**:
  - **Click Up Arrow**: Decrements scroll offset by 1.
  - **Click Down Arrow**: Increments scroll offset by 1.
  - **Click Track Above Thumb**: Page Up (scrolls by -4 items).
  - **Click Track Below Thumb**: Page Down (scrolls by +4 items).
  - **Thumb Drag**: `sb_dragging` / `part_sb_dragging` tracks mouse delta Y relative to `track_h - thumb_h`, dynamically clamping within `[0, max_scroll]`.
  - **Auto-Scroll**: Selecting an item outside the current 4-item window automatically clamps the scroll offset so the selected item is visible.

### 3.3 Keyboard Pane Switching & Cursor Navigation

- **`Tab` Key**: In the main window (when no dialog is open), `Tab` toggles keyboard focus between `PANE_DEVICES` and `PANE_PARTITIONS`.
- **Visual Focus Indicator**: The active pane displays a prominent `[●]` badge next to its title (e.g., `物理記憶装置 (Storage Devices) [●]` vs `区画一覧 Partitions & Slices [●]`).
- **`Up` / `Down` Arrow Keys**: Steps through and selects items in whichever pane currently holds active focus, auto-scrolling the viewport as needed.

## 4. Planet-Scale Partition & Volume Identifiers

To guarantee disambiguation between Classic BTRON, B-right/V Chokanji, and Modern 64-bit BTRON3 at global scale, standardized unique IDs and signatures are enforced:

### 4.1 MBR Partition Type Codes

| Code | Type Name | Operating System / Filesystem | Description |
|---|---|---|---|
| `0x13` | `BTRON_PART_TYPE_CHOKANJI` | B-right/V Chokanji 4.x | Legacy 32-bit BTRON Volume (`hda.qcow2`) |
| `0xB1` | `BTRON_PART_TYPE_BFS_V1` | BTRON3 B-FS V1 Classic | Standard V1 volume (`btron_sys.vol`, `btron_anders.vol`) |
| `0xB2` | `BTRON_PART_TYPE_BFS_V2` | BTRON3 B-FS V2 Modern | Next-gen 64-bit B-FS with WAL & Vector index |
| `0x83` | `BTRON_PART_TYPE_RAW` | POSIX / Raw Slice | Unformatted raw partition slice |

### 4.2 GPT GUID Partition Identifiers

- **Chokanji Legacy GUID**: `706D636F-7270-4254-524E-000000000001` (pmcorp BTRN)
- **B-FS V1 Classic GUID**: `8B684653-BTRN-3100-BEEF-000000000001`
- **B-FS V2 Modern GUID**: `8B684653-BTRN-3200-BEEF-000000000002`

### 4.3 Filesystem Superblock Type Codes & Magic

| FS Code | Magic | Filesystem | Block Size | Features & Structure |
|---|---|---|---|---|
| `0x6400` | `0x42FE` | `FS_BFS_V1` | 1024 B | Flat 32-bit FID table, no WAL journal |
| `0x6402` | `0x52FE` | `FS_CHOKANJI` | 1024 B | B-right/V 4.02 filesystem, 32-bit FID |
| `0x6403` | `0x62FE` | `FS_BFS_V2` | 4096 B | 64-bit FID, B+Tree indexing, 32 MiB circular WAL |

## 5. Production Application Menu Bar (`APP_MENU_BAR`)

The menu bar implements the clean 5-header hierarchy standard across BTRON workstation applications:

| Header | Width | Menu Item | Accelerator | Command ID | Action Description |
|---|---|---|---|---|---|
| **ファイル(F)** | 104 | 再認識 (Rescan Storage) | `Ctrl+R` | `DSCMD_FILE_SCAN` | Rescans host POSIX files and active mounts |
| | | 新規イメージ作成 (New Image)... | `Ctrl+N` | `DSCMD_FILE_CREATE_IMG` | Opens `DIALOG_CREATE_IMAGE` (V1 or V2) |
| | | ------------------------------ | | | Separator |
| | | 閉じる (Close) | `Ctrl+W` | `DSCMD_FILE_CLOSE` | Destroys window or closes app |
| **ディスク(D)** | 100 | 初期化 MBR (Initialize MBR)... | | `DSCMD_DISK_INIT_MBR` | Opens `DIALOG_INIT_DISK` with MBR selected |
| | | 初期化 GPT (Initialize GPT)... | | `DSCMD_DISK_INIT_GPT` | Opens `DIALOG_INIT_DISK` with GPT selected |
| | | ------------------------------ | | | Separator |
| | | 新規区画作成 (Create Slice)... | | `DSCMD_PART_CREATE` | Opens `DIALOG_CREATE_SLICE` (V1, V2, RAW) |
| | | 区画削除 (Delete Partition) | `Del` | `DSCMD_PART_DELETE` | Opens `DIALOG_WARN_WRITE` before deleting |
| | | ------------------------------ | | | Separator |
| | | B-FS フォーマット (Format)... | `Ctrl+F` | `DSCMD_PART_FMT_BFS` | Opens `DIALOG_FORMAT_BFS` (V1 or V2) |
| **マウント(M)** | 96 | マウント (Mount) | `Ctrl+M` | `DSCMD_MOUNT_VOLUME` | Mounts selected partition to real system |
| | | アンマウント (Unmount) | `Ctrl+U` | `DSCMD_UNMOUNT_VOLUME` | Flushes dirty state and unmounts |
| | | ------------------------------ | | | Separator |
| | | /SYS (btron_sys.vol - B-FS V1) を開く | | `DSCMD_MOUNT_SYS` | Direct mount of system volume (B-FS V1) |
| | | /ANDERS (btron_anders.vol - B-FS V1) を開く | | `DSCMD_MOUNT_ANDERS` | Direct mount of secondary user volume (B-FS V1) |
| | | /CHOKANJI (hda.qcow2 - BTRON) を開く | | `DSCMD_MOUNT_QCOW2` | Direct mount of B-right/V 4.02 QCOW2 image |
| **表示(V)** | 72 | 更新 (Refresh) | `F5` | `DSCMD_VIEW_REFRESH` | Refreshes UI and queries volume statistics |
| **ヘルプ(H)** | 84 | ドライブ設定について (About) | | `DSCMD_HELP_ABOUT` | Opens native BTRON About dialog |

## 6. Modal Dialogs & Safety Interceptors

All dialogs provide 100% full keyboard accessibility (`Tab`, `Shift+Tab`, `Arrow keys`, `Space`, `Enter`, `Escape`, typing, backspace).

### 6.1 Dialog Specifications

#### Dialog 1: Initialize Storage Disk (`DIALOG_INIT_DISK`)

- Wipes and sets up a fresh partition table on the selected device.
- Triggers safety confirmation `DIALOG_WARN_WRITE` (`WRITE_OP_INIT_DISK`).
- Supports MBR (`0xB1`/`0x13`) and GPT schemes.

#### Dialog 2: Create Partition Slice (`DIALOG_CREATE_SLICE`)

- Supports 3 partition types:
  - `0`: B-FS V1 (`0xB1`) — Standard 32-bit Classic Volume.
  - `1`: B-FS V2 (`0xB2`) — Modern 64-bit Volume with Journaling.
  - `2`: RAW (`0x83`) — Unformatted slice.
- Sizes: `1.0 GiB`, `2.0 GiB`, `最大 (Max)`.

#### Dialog 3: Create New Disk Image (`DIALOG_CREATE_IMAGE`)

- Creates real backing file on host filesystem (`blk_file_create`).
- Supports creating both:
  - `0`: B-FS V1 Volume (`.vol`) with MBR and 1024B blocks (`0xB1`).
  - `1`: B-FS V2 Volume (`.vol`) with GPT and 4096B blocks (`0xB2`).
- Sizes: `64 MiB`, `256 MiB`, `1.0 GiB`.

#### Dialog 4: Format B-FS Volume (`DIALOG_FORMAT_BFS`)

- Supports choosing formatting architecture:
  - `0`: B-FS V2 Modern (`0x6403`) with 4096B blocks, B+Tree indexing, and WAL.
  - `1`: B-FS V1 Classic (`0x6400`) with 1024B blocks, flat FID map, and no WAL.
- Triggers safety confirmation `DIALOG_WARN_WRITE` (`WRITE_OP_FORMAT_VOLUME`) before executing.

#### Dialog 5: Unified Write Warning Dialog (`DIALOG_WARN_WRITE`)

- Safety interceptor modal presented before any destructive write action:
  - `WRITE_OP_INIT_DISK`: Device initialization.
  - `WRITE_OP_DELETE_PART`: Partition slice deletion.
  - `WRITE_OP_FORMAT_VOLUME`: Filesystem formatting (V1 or V2).
- Controls: `[ 実行 (Proceed) ]` (Focus 0), `[ 中止 (Cancel) ]` (Focus 1).
- Confirmed by pressing `Enter` on Proceed or clicking.

## 7. Real System End-to-End Workflow

```
[ Step 1: Create Host Image File (V1 or V2) ]
  b_drivesetup_open_dialog(&st, DIALOG_CREATE_IMAGE);
  safe_strcpy(st.dlg_text_buf, "btron_extra.vol", sizeof(st.dlg_text_buf));
  st.dlg_radio_sel1 = 0; /* 64 MiB */
  st.dlg_radio_sel2 = 0; /* V1 (0xB1) or 1 for V2 (0xB2) */
  b_drivesetup_commit_dialog(&st);
  ==> Host file "btron_extra.vol" is created on disk, formatted, and added to devices[].

[ Step 2: Initialize Disk Scheme ]
  b_drivesetup_open_dialog(&st, DIALOG_INIT_DISK);
  st.dlg_radio_sel1 = 0; /* MBR */
  b_drivesetup_commit_dialog(&st);
  b_drivesetup_commit_dialog(&st); /* Confirm in DIALOG_WARN_WRITE */
  ==> Disk partition table initialized with MBR scheme.

[ Step 3: Create Partition Slice (V1, V2, or RAW) ]
  b_drivesetup_open_dialog(&st, DIALOG_CREATE_SLICE);
  safe_strcpy(st.dlg_text_buf, "EXTRA_DATA", sizeof(st.dlg_text_buf));
  st.dlg_radio_sel1 = 0; /* 1 GiB */
  st.dlg_radio_sel2 = 1; /* V2 Slice (0xB2) */
  b_drivesetup_commit_dialog(&st);
  ==> Slice "btron_extra.vol:s0" created with type 0xB2.

[ Step 4: Format with B-FS (V1 or V2) ]
  b_drivesetup_open_dialog(&st, DIALOG_FORMAT_BFS);
  safe_strcpy(st.dlg_text_buf, "EXTRA", sizeof(st.dlg_text_buf));
  st.dlg_radio_sel3 = 0; /* V2 Modern (0x6403) or 1 for V1 (0x6400) */
  b_drivesetup_commit_dialog(&st);
  b_drivesetup_commit_dialog(&st); /* Confirm in DIALOG_WARN_WRITE */
  ==> Real B-FS superblock and metadata written to backing file on disk.

[ Step 5: Mount to Real System ]
  b_drivesetup_mount(&st, st.selected_dev_idx, st.selected_part_idx);
  ==> Volume mounted into g_anders_vol.
  ==> Now accessible via BTRON file API as "/ANDERS/...".
```

## 8. Verification & Automated Test Suite

All DriveSetup functionality is verified by `verify/tests/test_b_drivesetup.c`:

1. `test_init_and_invariants`: Verifies initial zero-state, invariant checking, and 5-header menu bar.
2. `test_posix_volume_scanning`: Verifies detection of real live volumes (`g_sys_vol`, `g_anders_vol`, `g_chokanji_vol`) without any mock devices.
3. `test_selection`: Verifies device and partition selection bounds checking.
4. `test_init_disk_and_create_slice`: Verifies disk initialization, 3 slice variants (V1 `0xB1`, V2 `0xB2`, RAW `0x83`), and slice deletion.
5. `test_create_disk_image`: Verifies creating real host files on disk and populating both V1 and V2 device entries with planet-scale typing.
6. `test_format_bfs_and_mount`: Verifies formatting both V1 and V2, journal recovery, and real system mounting.
7. `test_storage_devices_scrollbar`: Verifies 4-item scroll window, offsets, clamping, paging, thumb dragging, and auto-scroll.
8. `test_partition_table_virtual_scroll_and_keys`: Verifies 4-item scroll window, virtual scrollbar, Up/Down cursors, and Tab pane focus switching.
9. `test_window_resize_responsiveness`: Verifies layout engine at standard (740x512), expanded (1024x768), and compact (520x420) resolutions with zero margin clipping or overflows.
10. `test_dialog_keyboard_navigation`: Verifies `Tab`, `Shift+Tab`, `Arrow keys`, `Space`, `Enter`, `Escape`, and character typing across all dialogs.
11. `test_pure_gui_and_menus`: Verifies pure 2D/3D graphical rendering, mouse clicks, and menu bar event dispatch.
12. `test_warn_write_safety_gate`: Verifies warning dialog intercepting Device, Volume, and Partition write operations safely.

### Execution Commands

```bash
make test-drivesetup                   # Build and run the DriveSetup standalone test suite
make test                              # Run all BTRON test suites
bash verify/models/verify_models.sh    # Verify formal Rocq/Coq models
make btron-posix                       # Build native POSIX binary
```

# Credits

* Namdak Tonpa and Grok 4.5
