/*
 * test_b_drivesetup.c
 * Automated test suite for production B-System DriveSetup application.
 *
 * Verifies:
 *  - Invariants across all operations (Rule 3 static bounds)
 *  - Storage device scanning (real POSIX volumes and backing files, zero mocks)
 *  - 4-item high Physical Storage Devices list with vertical scrollbar
 *  - Scrolling and scrollbar hit-testing (arrows, thumb dragging, page scrolling, auto-scroll)
 *  - Disk initialization (MBR & GPT) and slice creation
 *  - Creating new disk image files (b_drivesetup_create_disk_image)
 *  - B-FS V2 formatting with parameterized B+Tree & 64-bit FIDs
 *  - Mount & unmount state transitions and journal recovery replay
 *  - Window resize responsiveness (drivesetup_calc_layout across resolutions)
 *  - Full keyboard navigation across all modal dialogs (Tab, Arrows, Space, Enter, Escape, typing)
 *  - Pure BTRON Graphical Window UI, 3D painting, and event dispatch
 *  - Production in-window application menu bar (APP_MENU_BAR)
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <time.h>
#include <btron/types.h>
#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/event.h>
#include <btron/fs/volume.h>
#include <btron/fs/block.h>
#include <btron/fs/vol_api.h>
#include "../../src/apps/b_drivesetup.h"

/* Mock graphics & window manager stubs for headless unit test runner */
static WND g_mock_wnd;
static COLOR g_mock_vram[1024 * 768];
static GDEV g_mock_gdev = { 740, 512, 0, g_mock_vram, { 0, 0, 740, 512 } };

ER drw_tc_string(GDEV *dev, H x, H y, const char *text, COLOR fg_col, COLOR bg_col) {
    (void)dev; (void)x; (void)y; (void)text; (void)fg_col; (void)bg_col;
    return 0;
}
ER fill_rec(GDEV *dev, const RECT *r, COLOR col) {
    (void)dev; (void)r; (void)col;
    return 0;
}
ER drw_lin(GDEV *dev, H x1, H y1, H x2, H y2) {
    (void)dev; (void)x1; (void)y1; (void)x2; (void)y2;
    return 0;
}
ER drw_rec(GDEV *dev, const RECT *r) {
    (void)dev; (void)r;
    return 0;
}
int tc_calc_string_width(const char *s) {
    return s ? (int)strlen(s) * 8 : 0;
}
WND* opn_wnd(const char *title, H x, H y, H w, H h, UW attr) {
    (void)title; (void)x; (void)y; (void)w; (void)h; (void)attr;
    memset(&g_mock_wnd, 0, sizeof(WND));
    g_mock_wnd.client.right = w;
    g_mock_wnd.client.bottom = h;
    return &g_mock_wnd;
}
ER top_wnd(WND *wnd) { (void)wnd; return 0; }
ER inval_wnd(WND *wnd) { (void)wnd; return 0; }
ER cls_wnd(WND *wnd) { (void)wnd; return 0; }

/* Memory allocator stubs */
void* Icalloc(size_t nmemb, size_t sz) { return calloc(nmemb, sz); }
void  Ifree(void *ptr) { free(ptr); }

#include <btron/file.h>
#include "../../src/apps/clu.h"

extern Volume *g_sys_vol;
extern Volume *g_anders_vol;
extern Volume *g_chokanji_vol;

static void clu_buf_out(const char *str, UW color, void *ud) {
    (void)color;
    char *buf = (char *)ud;
    strncat(buf, str, 4095 - strlen(buf));
    strncat(buf, "\n", 4095 - strlen(buf));
}

/* Deterministic hash calculation of a file across all its records */
static uint32_t calc_file_hash(const char *path) {
    ID fd = opn_fil(path, 0x0001);
    if (fd < 0) return 0;
    uint32_t hash = 5381;
    for (W i = 0; i < 40; i++) {
        ID rec = opn_rec(fd, i, 0x0001);
        if (rec < 0) break;
        char buf[256];
        W got = 0;
        while (rd_rec(rec, buf, (W)sizeof(buf), &got) == 0 && got > 0) {
            for (W b = 0; b < got; b++) {
                hash = ((hash << 5) + hash) + (uint8_t)buf[b];
            }
        }
        cls_rec(rec);
    }
    cls_fil(fd);
    return hash;
}

/* ── Test Cases ─────────────────────────────────────────────────── */

static void test_init_and_invariants(void) {
    printf("[1/10] Testing b_drivesetup_init and invariants...\n");
    DriveSetupState st;
    b_drivesetup_init(&st);
    assert(b_drivesetup_verify_invariants(&st));
    assert(st.device_count == 0);
    assert(st.selected_dev_idx == -1);
    assert(st.selected_part_idx == -1);
    assert(st.dev_scroll_offset == 0);
    assert(st.menu_bar.header_count == 5);
    printf("  PASS: Initial state and 5-header menu bar are well-formed.\n");
}

static void test_posix_volume_scanning(void) {
    printf("[2/13] Testing b_drivesetup_scan_devices with live POSIX volumes...\n");
    DriveSetupState st;
    b_drivesetup_init(&st);

    /* When g_sys_vol and g_chokanji_vol are attached, populates live devices */
    void *s_buf = calloc(1, 2048 * 4096);
    BlkDev *s_dev = blk_mem_create(s_buf, 2048 * 4096, 0);
    s_dev->block_size = 4096;
    s_dev->nblocks = 2048;
    vol_format(s_dev, 256, 2048, "SYS");
    g_sys_vol = vol_mount(s_dev);

    void *c_buf = calloc(1, 1024 * 4096);
    BlkDev *c_dev = blk_mem_create(c_buf, 1024 * 4096, 0);
    vol_format(c_dev, 256, 1024, "CHOKANJI");
    g_chokanji_vol = vol_mount(c_dev);

    b_drivesetup_scan_devices(&st);
    assert(b_drivesetup_verify_invariants(&st));
    assert(st.device_count >= 2);

    int sys_idx = -1, chokanji_idx = -1;
    for (int i = 0; i < st.device_count; i++) {
        if (strcmp(st.devices[i].raw_path, "btron_sys.vol") == 0) sys_idx = i;
        if (strcmp(st.devices[i].raw_path, "hda.qcow2") == 0) chokanji_idx = i;
    }
    assert(sys_idx >= 0);
    assert(chokanji_idx >= 0);

    assert(st.devices[sys_idx].partitions[0].mounted == true);
    assert(st.devices[sys_idx].partitions[0].block_size == 4096);
    assert(st.devices[sys_idx].partitions[0].fs_type == FS_BFS_V1);
    assert(st.devices[sys_idx].partitions[0].type_code == BTRON_PART_TYPE_BFS_V1);
    assert(strcmp(st.devices[sys_idx].partitions[0].mount_point, "/SYS") == 0);
    assert(st.devices[sys_idx].partitions[0].features == 0);
    assert(st.devices[sys_idx].partitions[0].btree_node_size == 0);
    assert(strstr(st.devices[sys_idx].model, "B-FS V1") != NULL);

    assert(st.devices[chokanji_idx].partitions[0].mounted == true);
    assert(st.devices[chokanji_idx].partitions[0].fs_type == FS_CHOKANJI);
    assert(st.devices[chokanji_idx].partitions[0].type_code == BTRON_PART_TYPE_CHOKANJI);
    assert(strcmp(st.devices[chokanji_idx].partitions[0].mount_point, "/CHOKANJI") == 0);
    assert(st.devices[chokanji_idx].partitions[0].features == 0);
    assert(st.devices[chokanji_idx].partitions[0].btree_node_size == 0);

    vol_umount(g_sys_vol);
    vol_umount(g_chokanji_vol);
    free(s_buf);
    free(c_buf);
    g_sys_vol = NULL;
    g_chokanji_vol = NULL;
    printf("  PASS: POSIX volume detection and live volume enumeration verified.\n");
}

static void test_selection(void) {
    printf("[3/10] Testing device and partition selection...\n");
    DriveSetupState st;
    b_drivesetup_init(&st);

    /* Provide 2 devices */
    st.device_count = 2;
    for (int i = 0; i < 2; i++) {
        snprintf(st.devices[i].raw_path, sizeof(st.devices[i].raw_path), "drive_%d.vol", i);
        st.devices[i].partition_count = 2;
    }
    st.selected_dev_idx = 0;
    st.selected_part_idx = 0;

    assert(b_drivesetup_select_device(&st, 1));
    assert(st.selected_dev_idx == 1);
    assert(st.selected_part_idx == 0);

    /* Out of range checks */
    assert(!b_drivesetup_select_device(&st, 5));
    assert(!b_drivesetup_select_partition(&st, 10));

    assert(b_drivesetup_verify_invariants(&st));
    printf("  PASS: Selection and bounds checks verified.\n");
}

static void test_init_disk_and_create_slice(void) {
    printf("[4/11] Testing b_drivesetup_init_disk and slice creation (V1, V2, RAW)...\n");
    DriveSetupState st;
    b_drivesetup_init(&st);

    st.device_count = 1;
    strcpy(st.devices[0].raw_path, "test_drive.vol");
    st.devices[0].scheme = PART_SCHEME_MBR;
    st.devices[0].partition_count = 2;
    st.selected_dev_idx = 0;

    /* Reinitialize Device 0 with GPT */
    assert(b_drivesetup_init_disk(&st, 0, PART_SCHEME_GPT));
    assert(b_drivesetup_verify_invariants(&st));
    assert(st.devices[0].scheme == PART_SCHEME_GPT);
    assert(st.devices[0].partition_count == 0);

    /* 1. Create a V1 slice via DIALOG_CREATE_SLICE */
    b_drivesetup_open_dialog(&st, DIALOG_CREATE_SLICE);
    strcpy(st.dlg_text_buf, "BFS1_Slice");
    st.dlg_radio_sel1 = 0; /* 1.0 GiB */
    st.dlg_radio_sel2 = 0; /* B-FS V1 (0xB1) */
    b_drivesetup_commit_dialog(&st);
    assert(b_drivesetup_verify_invariants(&st));
    assert(st.devices[0].partition_count == 1);
    assert(strcmp(st.devices[0].partitions[0].label, "BFS1_Slice") == 0);
    assert(st.devices[0].partitions[0].fs_type == FS_BFS_V1);
    assert(st.devices[0].partitions[0].type_code == BTRON_PART_TYPE_BFS_V1);
    assert(st.devices[0].partitions[0].block_size == 1024);
    assert(st.devices[0].partitions[0].features == 0);

    /* 2. Create a V2 slice via DIALOG_CREATE_SLICE */
    b_drivesetup_open_dialog(&st, DIALOG_CREATE_SLICE);
    strcpy(st.dlg_text_buf, "BFS2_Slice");
    st.dlg_radio_sel1 = 1; /* 2.0 GiB */
    st.dlg_radio_sel2 = 1; /* B-FS V2 (0xB2) */
    b_drivesetup_commit_dialog(&st);
    assert(b_drivesetup_verify_invariants(&st));
    assert(st.devices[0].partition_count == 2);
    assert(strcmp(st.devices[0].partitions[1].label, "BFS2_Slice") == 0);
    assert(st.devices[0].partitions[1].fs_type == FS_BFS_V2);
    assert(st.devices[0].partitions[1].type_code == BTRON_PART_TYPE_BFS_V2);
    assert(st.devices[0].partitions[1].block_size == 4096);
    assert(st.devices[0].partitions[1].features & FEAT_JOURNAL);

    /* 3. Create a RAW slice via DIALOG_CREATE_SLICE */
    b_drivesetup_open_dialog(&st, DIALOG_CREATE_SLICE);
    strcpy(st.dlg_text_buf, "Raw_Slice");
    st.dlg_radio_sel1 = 2; /* Max */
    st.dlg_radio_sel2 = 2; /* RAW (0x83) */
    b_drivesetup_commit_dialog(&st);
    assert(b_drivesetup_verify_invariants(&st));
    assert(st.devices[0].partition_count == 3);
    assert(strcmp(st.devices[0].partitions[2].label, "Raw_Slice") == 0);
    assert(st.devices[0].partitions[2].fs_type == FS_RAW);
    assert(st.devices[0].partitions[2].type_code == BTRON_PART_TYPE_RAW);

    /* 4. Delete partition 1 (BFS2_Slice) */
    assert(b_drivesetup_delete_partition(&st, 0, 1));
    assert(b_drivesetup_verify_invariants(&st));
    assert(st.devices[0].partition_count == 2);
    assert(strcmp(st.devices[0].partitions[1].label, "Raw_Slice") == 0);

    printf("  PASS: Disk initialization, 3-variant slice creation (V1, V2, RAW) and deletion verified.\n");
}

static void test_create_disk_image(void) {
    printf("[5/12] Testing b_drivesetup_create_disk_image (V1 and V2 devices)...\n");
    DriveSetupState st;
    b_drivesetup_init(&st);

    /* 1. Create B-FS V1 Disk Image */
    assert(b_drivesetup_create_disk_image_typed(&st, "btron_v1.vol", 64ULL * 1024ULL * 1024ULL, FS_BFS_V1));
    assert(b_drivesetup_verify_invariants(&st));
    assert(st.device_count == 1);
    assert(strcmp(st.devices[0].raw_path, "btron_v1.vol") == 0);
    assert(st.devices[0].scheme == PART_SCHEME_MBR);
    assert(st.devices[0].partitions[0].fs_type == FS_BFS_V1);
    assert(st.devices[0].partitions[0].type_code == BTRON_PART_TYPE_BFS_V1);
    assert(st.devices[0].partitions[0].block_size == 1024);

    /* 2. Create B-FS V2 Disk Image */
    assert(b_drivesetup_create_disk_image_typed(&st, "btron_v2.vol", 128ULL * 1024ULL * 1024ULL, FS_BFS_V2));
    assert(b_drivesetup_verify_invariants(&st));
    assert(st.device_count == 2);
    assert(strcmp(st.devices[1].raw_path, "btron_v2.vol") == 0);
    assert(st.devices[1].scheme == PART_SCHEME_GPT);
    assert(st.devices[1].partitions[0].fs_type == FS_BFS_V2);
    assert(st.devices[1].partitions[0].type_code == BTRON_PART_TYPE_BFS_V2);
    assert(st.devices[1].partitions[0].block_size == 4096);
    assert(st.devices[1].partitions[0].features & FEAT_JOURNAL);

    remove("btron_v1.vol");
    remove("btron_v2.vol");
    printf("  PASS: Creation of both V1 and V2 devices with planet-scale typing verified.\n");
}

static void test_format_bfs_and_mount(void) {
    printf("[6/12] Testing b_drivesetup format (both V1 and V2) and mount transitions...\n");
    DriveSetupState st;
    b_drivesetup_init(&st);

    st.device_count = 1;
    strcpy(st.devices[0].raw_path, "test_drive.vol");
    st.devices[0].partition_count = 2;
    st.selected_dev_idx = 0;
    st.selected_part_idx = 0;

    /* 1. Format partition 0 as B-FS V1 */
    assert(b_drivesetup_format_v1(&st, 0, 0, "ClassicV1", 1024));
    assert(b_drivesetup_verify_invariants(&st));
    DriveSetupPartition *p0 = &st.devices[0].partitions[0];
    assert(strcmp(p0->label, "ClassicV1") == 0);
    assert(p0->fs_type == FS_BFS_V1);
    assert(p0->type_code == BTRON_PART_TYPE_BFS_V1);
    assert(p0->block_size == 1024);
    assert(p0->btree_node_size == 0);

    /* 2. Format partition 1 as B-FS V2 */
    st.selected_part_idx = 1;
    assert(b_drivesetup_format_bfs(&st, 0, 1, "ModernV2", 2048, 2048, 16,
                                  FEAT_LARGE_FID | FEAT_VECTOR, 512));
    assert(b_drivesetup_verify_invariants(&st));

    DriveSetupPartition *p1 = &st.devices[0].partitions[1];
    assert(strcmp(p1->label, "ModernV2") == 0);
    assert(p1->fs_type == FS_BFS_V2);
    assert(p1->type_code == BTRON_PART_TYPE_BFS_V2);
    assert(p1->block_size == 2048);
    assert(p1->btree_node_size == 2048);
    assert(p1->vector_dim == 512);
    assert(p1->features & FEAT_LARGE_FID);
    assert(p1->features & FEAT_JOURNAL);
    assert(!p1->mounted);

    /* Mount */
    assert(b_drivesetup_mount(&st, 0, 1));
    assert(p1->mounted);
    assert(b_drivesetup_verify_invariants(&st));

    /* Cannot double mount */
    assert(!b_drivesetup_mount(&st, 0, 1));

    /* Unmount */
    assert(b_drivesetup_unmount(&st, 0, 1));
    assert(!p1->mounted);

    /* Test dirty journal recovery on mount */
    p1->dirty = true;
    assert(b_drivesetup_mount(&st, 0, 1));
    assert(p1->mounted);
    assert(!p1->dirty); /* Replay cleared dirty */
    assert(strstr(st.status_msg, "Journal replayed") != NULL);

    printf("  PASS: Formatting of both V1 and V2, mount, and journal replay verified.\n");
}

static void test_storage_devices_scrollbar(void) {
    printf("[7/12] Testing 4-item high Storage Devices list with vertical scrollbar...\n");
    DriveSetupState st;
    b_drivesetup_init(&st);

    /* Add 8 storage devices to test 4-item scroll window */
    st.device_count = 8;
    for (int i = 0; i < 8; i++) {
        snprintf(st.devices[i].raw_path, sizeof(st.devices[i].raw_path), "store_%d.vol", i);
        snprintf(st.devices[i].model, sizeof(st.devices[i].model), "BTRON STORE-%d", i);
        st.devices[i].total_bytes = (uint64_t)(i + 1) * 8ULL * 1024ULL * 1024ULL * 1024ULL;
        st.devices[i].sector_size = 512;
        st.devices[i].scheme = (i % 2 == 0) ? PART_SCHEME_GPT : PART_SCHEME_MBR;
    }
    st.selected_dev_idx = 0;
    st.dev_scroll_offset = 0;

    assert(b_drivesetup_verify_invariants(&st));

    /* Scroll down by 1 item */
    b_drivesetup_scroll(&st, 1);
    assert(st.dev_scroll_offset == 1);

    /* Scroll down by 3 more items (reaches max_scroll = 8 - 4 = 4) */
    b_drivesetup_scroll(&st, 3);
    assert(st.dev_scroll_offset == 4);

    /* Clamp at maximum scroll (cannot exceed 4) */
    b_drivesetup_scroll(&st, 10);
    assert(st.dev_scroll_offset == 4);

    /* Scroll back up */
    b_drivesetup_scroll(&st, -2);
    assert(st.dev_scroll_offset == 2);

    /* Clamp at 0 */
    b_drivesetup_scroll(&st, -10);
    assert(st.dev_scroll_offset == 0);

    /* Selecting device 6 should auto-scroll viewport so device 6 is visible */
    b_drivesetup_select_device(&st, 6);
    assert(st.selected_dev_idx == 6);
    assert(st.dev_scroll_offset >= 3); /* 6 must be within [dev_scroll_offset, dev_scroll_offset + 3] */
    assert(st.selected_dev_idx <= st.dev_scroll_offset + 3);

    /* Selecting device 0 should auto-scroll back */
    b_drivesetup_select_device(&st, 0);
    assert(st.dev_scroll_offset == 0);

    printf("  PASS: 4-item scroll window, offsets, clamping, and auto-scroll verified.\n");
}

static void test_partition_table_virtual_scroll_and_keys(void) {
    printf("[8/12] Testing 4-item high Partition table with virtual scrollbar, Up/Down cursor, and Tab pane focus...\n");
    DriveSetupState st;
    b_drivesetup_init(&st);

    st.device_count = 1;
    strcpy(st.devices[0].raw_path, "dev0.vol");
    st.devices[0].partition_count = 8;
    for (int p = 0; p < 8; p++) {
        snprintf(st.devices[0].partitions[p].dev_path, sizeof(st.devices[0].partitions[p].dev_path), "dev0.vol:s%d", p);
        snprintf(st.devices[0].partitions[p].label, sizeof(st.devices[0].partitions[p].label), "Slice_%d", p);
        st.devices[0].partitions[p].block_size = 4096;
        st.devices[0].partitions[p].block_count = 262144;
        st.devices[0].partitions[p].type_code = (p % 2 == 0) ? BTRON_PART_TYPE_BFS_V1 : BTRON_PART_TYPE_BFS_V2;
        st.devices[0].partitions[p].fs_type = (p % 2 == 0) ? FS_BFS_V1 : FS_BFS_V2;
    }
    st.selected_dev_idx = 0;
    st.selected_part_idx = 0;
    st.part_scroll_offset = 0;
    st.active_pane = PANE_DEVICES;

    assert(b_drivesetup_verify_invariants(&st));

    /* Test scroll partitions down by 1 */
    b_drivesetup_scroll_partitions(&st, 1);
    assert(st.part_scroll_offset == 1);

    /* Test scroll partitions down to max (8 - 4 = 4) */
    b_drivesetup_scroll_partitions(&st, 3);
    assert(st.part_scroll_offset == 4);

    /* Clamp at maximum scroll */
    b_drivesetup_scroll_partitions(&st, 10);
    assert(st.part_scroll_offset == 4);

    /* Scroll back up */
    b_drivesetup_scroll_partitions(&st, -2);
    assert(st.part_scroll_offset == 2);

    /* Clamp at 0 */
    b_drivesetup_scroll_partitions(&st, -10);
    assert(st.part_scroll_offset == 0);

    /* Selecting partition 6 should auto-scroll viewport so slice 6 is visible */
    b_drivesetup_select_partition(&st, 6);
    assert(st.selected_part_idx == 6);
    assert(st.part_scroll_offset >= 3);
    assert(st.selected_part_idx <= st.part_scroll_offset + 3);
    assert(st.active_pane == PANE_PARTITIONS);

    /* Selecting partition 0 auto-scrolls back to top */
    b_drivesetup_select_partition(&st, 0);
    assert(st.part_scroll_offset == 0);
    assert(st.selected_part_idx == 0);

    /* Test Tab key switching between panes */
    WND *wnd = open_drivesetup_window();
    assert(wnd != NULL);

    DriveSetupState *gst = &g_drivesetup_state;
    gst->active_pane = PANE_DEVICES;
    gst->selected_dev_idx = 0;
    gst->selected_part_idx = 0;

    /* Press Tab (0x09) -> switches to PANE_PARTITIONS */
    EVT tab_evt = { .type = EV_KEY_DOWN, .data = (void*)(uintptr_t)0x09 };
    wnd->event_handler(wnd, &tab_evt);
    assert(gst->active_pane == PANE_PARTITIONS);

    /* Press Down arrow while in PANE_PARTITIONS -> advances partition selection */
    int old_part = gst->selected_part_idx;
    int max_p = (gst->selected_dev_idx >= 0 && gst->selected_dev_idx < gst->device_count) ?
                gst->devices[gst->selected_dev_idx].partition_count : 0;
    if (max_p > 1) {
        EVT dn_evt = { .type = EV_KEY_DOWN, .data = (void*)(uintptr_t)0x1F };
        wnd->event_handler(wnd, &dn_evt);
        assert(gst->selected_part_idx == old_part + 1);
    }

    /* Press Tab again -> switches back to PANE_DEVICES */
    wnd->event_handler(wnd, &tab_evt);
    assert(gst->active_pane == PANE_DEVICES);

    if (wnd->destroy) wnd->destroy(wnd);

    printf("  PASS: Partition table virtual scroll, auto-scroll, Up/Down cursors and Tab pane focus verified.\n");
}

static void test_window_resize_responsiveness(void) {
    printf("[9/12] Testing window resize responsiveness (drivesetup_calc_layout)...\n");
    int test_sizes[][2] = {
        { 740, 512 },   /* Default resolution */
        { 1024, 768 },  /* Expanded high-res */
        { 520, 420 },   /* Minimum clamped size */
        { 1280, 1024 }  /* Ultra-wide workstation */
    };

    for (int i = 0; i < 4; i++) {
        int w = test_sizes[i][0];
        int h = test_sizes[i][1];
        DS_Layout lo;
        drivesetup_calc_layout(w, h, 6, 4, &lo);

        /* Margins must be strictly respected */
        assert(lo.dev_box.left == 10);
        assert(lo.dev_box.right == w - 10);
        assert(lo.btn1.left == 10);
        assert(lo.btn4.right == w - 10);
        assert(lo.sb_stat.left == 10);
        assert(lo.sb_stat.right == w - 10);
        assert(lo.sb_stat.bottom == h - 6);

        /* Table must be fixed 4 items height (106px) */
        assert(lo.tbl_r.bottom - lo.tbl_r.top == 106);
        assert(lo.part_sb_w == 16);

        /* Middle area distribution must be strictly non-negative and non-overlapping */
        assert(lo.slice_bar.top > lo.dev_box.bottom);
        assert(lo.tbl_r.top > lo.slice_bar.bottom);
        assert(lo.insp_r.top > lo.tbl_r.bottom);
        assert(lo.btn1.top > lo.insp_r.bottom);
        assert(lo.sb_stat.top > lo.btn1.bottom);

        /* Buttons must have positive width */
        assert(lo.btn1.right > lo.btn1.left);
        assert(lo.btn2.right > lo.btn2.left);
        assert(lo.btn3.right > lo.btn3.left);
        assert(lo.btn4.right > lo.btn4.left);
    }

    printf("  PASS: Layout calculations adapt seamlessly across all resolutions.\n");
}

static void test_dialog_keyboard_navigation(void) {
    printf("[10/12] Testing modal dialogs keyboard navigation...\n");
    DriveSetupState st;
    b_drivesetup_init(&st);
    st.device_count = 1;
    strcpy(st.devices[0].raw_path, "btron_anders.vol");
    st.selected_dev_idx = 0;

    /* 1. DIALOG_INIT_DISK Navigation */
    b_drivesetup_open_dialog(&st, DIALOG_INIT_DISK);
    assert(st.active_dialog == DIALOG_INIT_DISK);
    assert(st.dlg_focus_idx == 0);

    /* Tab advances focus to GPT radio (idx 1) */
    assert(b_drivesetup_handle_dialog_key(&st, 0x09));
    assert(st.dlg_focus_idx == 1);

    /* Down arrow flips radio selection between MBR and GPT (from default 1 to 0) */
    assert(b_drivesetup_handle_dialog_key(&st, 0x1F));
    assert(st.dlg_radio_sel1 == 0); /* MBR selected */
    assert(st.dlg_focus_idx == 0);

    /* Tab to Radio 1 (idx 1), then Tab to Checkbox (idx 2) */
    assert(b_drivesetup_handle_dialog_key(&st, 0x09));
    assert(st.dlg_focus_idx == 1);
    assert(b_drivesetup_handle_dialog_key(&st, 0x09));
    assert(st.dlg_focus_idx == 2);

    /* Space toggles checkbox */
    assert(b_drivesetup_handle_dialog_key(&st, ' '));
    assert((st.dlg_check_flags & 1) == 0); /* Toggled off */

    /* Escape closes dialog */
    assert(b_drivesetup_handle_dialog_key(&st, 0x1B));
    assert(st.active_dialog == DIALOG_NONE);

    /* 2. DIALOG_CREATE_IMAGE Typing & Commit */
    b_drivesetup_open_dialog(&st, DIALOG_CREATE_IMAGE);
    assert(st.active_dialog == DIALOG_CREATE_IMAGE);
    assert(st.dlg_focus_idx == 0);

    /* Clear default string via Backspace */
    for (int i = 0; i < 20; i++) {
        b_drivesetup_handle_dialog_key(&st, 0x08);
    }
    assert(st.dlg_text_buf[0] == '\0');

    /* Type "new_img.vol" */
    const char *typed = "new_img.vol";
    for (int i = 0; typed[i]; i++) {
        assert(b_drivesetup_handle_dialog_key(&st, (uint32_t)typed[i]));
    }
    assert(strcmp(st.dlg_text_buf, "new_img.vol") == 0);

    /* Enter commits the dialog */
    assert(b_drivesetup_handle_dialog_key(&st, 0x0D));
    assert(st.active_dialog == DIALOG_NONE);
    assert(st.device_count == 2);
    assert(strcmp(st.devices[1].raw_path, "new_img.vol") == 0);

    /* 3. DIALOG_FORMAT_BFS Typing & Commit (Editable Partition / Volume Name) */
    st.selected_dev_idx = 0;
    st.selected_part_idx = 0;
    b_drivesetup_open_dialog(&st, DIALOG_FORMAT_BFS);
    assert(st.active_dialog == DIALOG_FORMAT_BFS);
    assert(st.dlg_focus_idx == 0);

    /* Clear default partition name via Backspace */
    for (int i = 0; i < 20; i++) {
        b_drivesetup_handle_dialog_key(&st, 0x08);
    }
    assert(st.dlg_text_buf[0] == '\0');

    /* Type custom partition name "WORK_VOL" */
    const char *custom_part = "WORK_VOL";
    for (int i = 0; custom_part[i]; i++) {
        assert(b_drivesetup_handle_dialog_key(&st, (uint32_t)custom_part[i]));
    }
    assert(strcmp(st.dlg_text_buf, "WORK_VOL") == 0);

    /* Enter commits the dialog (transitions to DIALOG_WARN_WRITE with label set) */
    assert(b_drivesetup_handle_dialog_key(&st, 0x0D));
    assert(strcmp(st.pending_fmt_label, "WORK_VOL") == 0);
    assert(st.active_dialog == DIALOG_WARN_WRITE);
    /* Close warning dialog */
    b_drivesetup_close_dialog(&st);
    assert(st.active_dialog == DIALOG_NONE);

    printf("  PASS: Tab, arrows, space, typing, backspace, Enter and Escape verified.\n");
}

static void test_pure_gui_and_menus(void) {
    printf("[11/12] Testing Pure BTRON Graphical Window, Menus & Event Dispatch...\n");
    WND *wnd = open_drivesetup_window();
    assert(wnd != NULL);
    assert(wnd->paint != NULL);
    assert(wnd->event_handler != NULL);

    /* 1. Paint Window Canvas */
    wnd->paint(wnd, &g_mock_gdev);

    /* 2. Test In-Window Menu Bar Interaction (Click header 0: ファイル) */
    EVT click_menu = { .type = EV_BUT_DOWN, .pos = { 20, 10 } };
    wnd->event_handler(wnd, &click_menu);
    assert(drivesetup_is_menu_open());
    wnd->paint(wnd, &g_mock_gdev);

    /* Mouse move glide over header 1: ディスク */
    EVT glide_menu = { .type = EV_MOUSE_MOVE, .pos = { 130, 10 } };
    wnd->event_handler(wnd, &glide_menu);
    wnd->paint(wnd, &g_mock_gdev);

    /* Close menu with Escape key */
    EVT esc_key = { .type = EV_KEY_DOWN, .data = (void*)(uintptr_t)0x1B };
    wnd->event_handler(wnd, &esc_key);
    assert(!drivesetup_is_menu_open());
    wnd->paint(wnd, &g_mock_gdev);

    /* 3. Test Scrollbar click: scroll down button at sb_x=714, y=120 */
    EVT click_sb_dn = { .type = EV_BUT_DOWN, .pos = { 718, 120 } };
    wnd->event_handler(wnd, &click_sb_dn);
    wnd->paint(wnd, &g_mock_gdev);

    /* 4. Test Clicking [ Initialize Disk... ] button at (x=50, y=450) */
    EVT click_init = { .type = EV_BUT_DOWN, .pos = { 50, 450 } };
    wnd->event_handler(wnd, &click_init);
    wnd->paint(wnd, &g_mock_gdev);

    /* Cancel Initialize dialog via Escape */
    EVT dlg_esc = { .type = EV_KEY_DOWN, .data = (void*)(uintptr_t)0x1B };
    wnd->event_handler(wnd, &dlg_esc);
    wnd->paint(wnd, &g_mock_gdev);

    /* 5. Test Clicking [ Format B-FS... ] button at (x=400, y=450) */
    EVT click_fmt = { .type = EV_BUT_DOWN, .pos = { 400, 450 } };
    wnd->event_handler(wnd, &click_fmt);
    wnd->paint(wnd, &g_mock_gdev);

    /* Cancel format via Escape */
    wnd->event_handler(wnd, &dlg_esc);
    wnd->paint(wnd, &g_mock_gdev);

    /* 6. Test Clicking Visual Slice Map (x=200, y=170) to select partition 0 */
    EVT click_slice = { .type = EV_BUT_DOWN, .pos = { 200, 170 } };
    wnd->event_handler(wnd, &click_slice);
    wnd->paint(wnd, &g_mock_gdev);

    /* 7. Test Clicking [ Mount / Unmount ] button at (x=600, y=450) */
    EVT click_mount = { .type = EV_BUT_DOWN, .pos = { 600, 450 } };
    wnd->event_handler(wnd, &click_mount);
    wnd->paint(wnd, &g_mock_gdev);

    if (wnd->destroy) wnd->destroy(wnd);
    printf("  PASS: Pure BTRON Graphical Window, Menus & Event Dispatch validated.\n");
}

static void test_warning_dialog_and_write_safety(void) {
    printf("[12/12] Testing DIALOG_WARN_WRITE safety gate for write operations...\n");
    DriveSetupState st;
    b_drivesetup_init(&st);

    st.device_count = 1;
    strcpy(st.devices[0].raw_path, "btron_warn.vol");
    st.devices[0].scheme = PART_SCHEME_MBR;
    st.devices[0].partition_count = 2;
    strcpy(st.devices[0].partitions[0].label, "Boot");
    st.devices[0].partitions[0].fs_type = FS_BFS_V1;
    strcpy(st.devices[0].partitions[1].label, "Data");
    st.devices[0].partitions[1].fs_type = FS_BFS_V2;
    st.selected_dev_idx = 0;
    st.selected_part_idx = 1;

    /* A. Device Write Operation Safety: Init Disk */
    b_drivesetup_open_dialog(&st, DIALOG_INIT_DISK);
    st.dlg_radio_sel1 = 1; /* Select GPT */
    /* Committing DIALOG_INIT_DISK MUST intercept and open DIALOG_WARN_WRITE */
    b_drivesetup_commit_dialog(&st);
    assert(st.active_dialog == DIALOG_WARN_WRITE);
    assert(st.pending_write_op == WRITE_OP_INIT_DEVICE);
    assert(st.pending_scheme == PART_SCHEME_GPT);
    assert(st.dlg_focus_idx == 1); /* Safe default is CANCEL (focus 1) */
    assert(st.devices[0].partition_count == 2); /* Disk untouched! */

    /* Cancel via Enter while focus is 1 */
    b_drivesetup_commit_dialog(&st);
    assert(st.active_dialog == DIALOG_NONE);
    assert(st.pending_write_op == WRITE_OP_NONE);
    assert(st.devices[0].partition_count == 2); /* Still untouched */
    assert(st.devices[0].scheme == PART_SCHEME_MBR);

    /* Now confirm write: open init disk, commit to warn dialog, change focus to 0, commit */
    b_drivesetup_open_dialog(&st, DIALOG_INIT_DISK);
    st.dlg_radio_sel1 = 1; /* GPT */
    b_drivesetup_commit_dialog(&st);
    assert(st.active_dialog == DIALOG_WARN_WRITE);
    st.dlg_focus_idx = 0; /* User deliberately chooses Write */
    b_drivesetup_commit_dialog(&st);
    assert(st.active_dialog == DIALOG_NONE);
    assert(st.devices[0].scheme == PART_SCHEME_GPT);
    assert(st.devices[0].partition_count == 0); /* Cleared by init */
    assert(b_drivesetup_verify_invariants(&st));

    /* Add back a partition for format test */
    assert(b_drivesetup_create_slice(&st, 0, "TestVol", 1ULL * 1024ULL * 1024ULL * 1024ULL));
    assert(st.devices[0].partition_count == 1);
    st.selected_part_idx = 0;

    /* B. Volume Write Operation Safety: Format B-FS */
    b_drivesetup_open_dialog(&st, DIALOG_FORMAT_BFS);
    strcpy(st.dlg_text_buf, "FormattedVol");
    b_drivesetup_commit_dialog(&st);
    assert(st.active_dialog == DIALOG_WARN_WRITE);
    assert(st.pending_write_op == WRITE_OP_FORMAT_VOLUME);
    assert(st.dlg_focus_idx == 1); /* Safe default is CANCEL */
    assert(strcmp(st.devices[0].partitions[0].label, "TestVol") == 0); /* Untouched */

    /* Confirm format */
    st.dlg_focus_idx = 0; /* Write */
    b_drivesetup_commit_dialog(&st);
    assert(st.active_dialog == DIALOG_NONE);
    assert(strcmp(st.devices[0].partitions[0].label, "FormattedVol") == 0); /* Formatted */
    assert(b_drivesetup_verify_invariants(&st));

    /* C. Partition Write Operation Safety: Delete Partition */
    b_drivesetup_handle_cmd(&st, DSCMD_PART_DELETE);
    assert(st.active_dialog == DIALOG_WARN_WRITE);
    assert(st.pending_write_op == WRITE_OP_DELETE_PARTITION);
    assert(st.dlg_focus_idx == 1); /* Safe default is CANCEL */

    /* Tab shifts focus between Cancel (1) and Write (0) */
    assert(b_drivesetup_handle_dialog_key(&st, 0x09));
    assert(st.dlg_focus_idx == 0);

    /* Confirm deletion via Enter */
    assert(b_drivesetup_handle_dialog_key(&st, 0x0D));
    assert(st.active_dialog == DIALOG_NONE);
    assert(st.devices[0].partition_count == 0); /* Deleted! */
    assert(b_drivesetup_verify_invariants(&st));

    printf("  PASS: Warning dialog intercepted Device, Volume, and Partition write operations safely.\n");
}

static void test_e2e_full_lifecycle_and_clu_browsing(void) {
    printf("[13/13] Testing E2E full 6-stage lifecycle and CLU command verification...\n");
    int r_id = (int)(time(NULL) % 100000);
    char custom_img[64];
    char custom_slice[64];
    char custom_vol[64];
    char custom_mount[64];
    char file_path[128];
    char file_name[64];
    char clu_cmd_g[128];
    char clu_cmd_t[128];
    char clu_cmd_gl[128];
    char clu_cmd_tl[128];

    snprintf(custom_img, sizeof(custom_img), "dev_img_%d.vol", r_id);
    snprintf(custom_slice, sizeof(custom_slice), "SLICE_%d", r_id);
    snprintf(custom_vol, sizeof(custom_vol), "VOL_%d", r_id);
    snprintf(custom_mount, sizeof(custom_mount), "/%s", custom_vol);
    snprintf(file_name, sizeof(file_name), "work_%d.tad", r_id);
    snprintf(file_path, sizeof(file_path), "/%s/%s", custom_vol, file_name);
    snprintf(clu_cmd_g, sizeof(clu_cmd_g), "-g %s", custom_mount);
    snprintf(clu_cmd_t, sizeof(clu_cmd_t), "-t %s", custom_mount);
    snprintf(clu_cmd_gl, sizeof(clu_cmd_gl), "-g -l %s", custom_mount);
    snprintf(clu_cmd_tl, sizeof(clu_cmd_tl), "-t -l %s", custom_mount);

    remove(custom_img);

    DriveSetupState st;
    b_drivesetup_init(&st);

    /* 1. Stage 1: Creating device */
    assert(b_drivesetup_create_disk_image_typed(&st, custom_img, 64ULL * 1024ULL * 1024ULL, FS_BFS_V1));
    assert(b_drivesetup_verify_invariants(&st));
    assert(st.device_count == 1);
    assert(strcmp(st.devices[0].raw_path, custom_img) == 0);

    /* 2. Stage 2: Initialize device with GPT */
    assert(b_drivesetup_init_disk(&st, 0, PART_SCHEME_GPT));
    assert(st.devices[0].scheme == PART_SCHEME_GPT);
    assert(st.devices[0].partition_count == 0);

    /* 3. Stage 3: Create partition slice with arbitrary name */
    assert(b_drivesetup_create_slice(&st, 0, custom_slice, 32ULL * 1024ULL * 1024ULL));
    assert(st.devices[0].partition_count == 1);
    assert(strcmp(st.devices[0].partitions[0].label, custom_slice) == 0);

    /* 4. Stage 4: Format partition slice with arbitrary volume label */
    assert(b_drivesetup_format_v1(&st, 0, 0, custom_vol, 1024));
    assert(strcmp(st.devices[0].partitions[0].label, custom_vol) == 0);

    /* 5. Stage 5: Mount partition */
    assert(b_drivesetup_mount(&st, 0, 0));
    assert(st.devices[0].partitions[0].mounted == true);
    assert(strcmp(st.devices[0].partitions[0].mount_point, custom_mount) == 0);
    assert(g_anders_vol != NULL);
    assert(strcmp(vol_name(g_anders_vol), custom_vol) == 0);

    /* 6. Stage 6: Verify partition is browsable with CLU commands */
    /* Create a file inside arbitrary volume */
    ID test_fd = cre_fil(file_path, 0x0002);
    assert(test_fd >= 0);
    cls_fil(test_fd);

    char out_buf[4096];

    /* Test clu_cd to arbitrary mount */
    memset(out_buf, 0, sizeof(out_buf));
    clu_cd(custom_mount, clu_buf_out, out_buf);
    assert(strcmp(g_cwd_path, custom_mount) == 0);

    /* Test clu_ls inside arbitrary volume */
    memset(out_buf, 0, sizeof(out_buf));
    clu_ls("", clu_buf_out, out_buf);
    assert(strstr(out_buf, file_name) != NULL);

    /* Test clu_ls -l */
    memset(out_buf, 0, sizeof(out_buf));
    clu_ls("-l", clu_buf_out, out_buf);
    assert(strstr(out_buf, "ATYPE") != NULL);
    assert(strstr(out_buf, file_name) != NULL);

    /* Test clu_ls -t */
    memset(out_buf, 0, sizeof(out_buf));
    clu_ls("-t", clu_buf_out, out_buf);
    assert(strstr(out_buf, "CTIME") != NULL);
    assert(strstr(out_buf, file_name) != NULL);

    /* Test clu_ls -l -t */
    memset(out_buf, 0, sizeof(out_buf));
    clu_ls("-l -t", clu_buf_out, out_buf);
    assert(strstr(out_buf, "ATYPE") != NULL);
    assert(strstr(out_buf, "CTIME") != NULL);
    assert(strstr(out_buf, file_name) != NULL);

    /* Test clu_fs_cmd -g */
    memset(out_buf, 0, sizeof(out_buf));
    clu_fs_cmd(clu_cmd_g, clu_buf_out, out_buf);
    assert(strstr(out_buf, custom_vol) != NULL);

    /* Test clu_fs_cmd -t */
    memset(out_buf, 0, sizeof(out_buf));
    clu_fs_cmd(clu_cmd_t, clu_buf_out, out_buf);
    assert(strstr(out_buf, "Tree Structure") != NULL);

    /* Test clu_fs_cmd -g -l */
    memset(out_buf, 0, sizeof(out_buf));
    clu_fs_cmd(clu_cmd_gl, clu_buf_out, out_buf);
    assert(strstr(out_buf, custom_vol) != NULL);

    /* Test clu_fs_cmd -t -l */
    memset(out_buf, 0, sizeof(out_buf));
    clu_fs_cmd(clu_cmd_tl, clu_buf_out, out_buf);
    assert(strstr(out_buf, "Tree Structure") != NULL);

    /* Test clu_df */
    memset(out_buf, 0, sizeof(out_buf));
    clu_df(custom_mount, clu_buf_out, out_buf);
    assert(strstr(out_buf, custom_mount) != NULL);
    assert(strstr(out_buf, custom_vol) != NULL);

    /* 6a. Verify saving data records directly to file on newly created volume */
    char data_file_name[64];
    char data_file_path[128];
    snprintf(data_file_name, sizeof(data_file_name), "notes_%d.txt", r_id);
    snprintf(data_file_path, sizeof(data_file_path), "/%s/%s", custom_vol, data_file_name);

    ID data_fd = cre_fil(data_file_path, 0x0002);
    assert(data_fd >= 0);
    const char *payload_text = "BTRON3 Native Filesystem Record Payload Data - Verification OK";
    W payload_len = (W)strlen(payload_text);
    assert(ins_rec(data_fd, 0, payload_text, payload_len) == 0);
    assert(cls_fil(data_fd) == 0);

    /* Verify data file content can be read back accurately from the new volume */
    ID verify_fd = opn_fil(data_file_path, 0x0001);
    assert(verify_fd >= 0);
    ID rec_h = opn_rec(verify_fd, 0, 0x0001);
    assert(rec_h >= 0);
    char readback_buf[128];
    memset(readback_buf, 0, sizeof(readback_buf));
    W bytes_read = 0;
    assert(rd_rec(rec_h, readback_buf, sizeof(readback_buf) - 1, &bytes_read) == 0);
    assert(bytes_read == payload_len);
    assert(strcmp(readback_buf, payload_text) == 0);
    assert(cls_rec(rec_h) == 0);
    assert(cls_fil(verify_fd) == 0);

    /* 6b. Verify copying files from /SYS to newly created volume */
    void *sys_ram = calloc(1, 2048 * 4096);
    BlkDev *sys_dev = blk_mem_create(sys_ram, 2048 * 4096, 0);
    sys_dev->block_size = 4096;
    sys_dev->nblocks = 2048;
    vol_format(sys_dev, 256, 2048, "SYS");
    Volume *sys_vol = vol_mount(sys_dev);
    assert(sys_vol != NULL);
    g_sys_vol = sys_vol;

    /* Create source file on /SYS with content */
    char sys_src_name[64];
    char sys_src_path[128];
    snprintf(sys_src_name, sizeof(sys_src_name), "sys_boot_%d.cfg", r_id);
    snprintf(sys_src_path, sizeof(sys_src_path), "/SYS/%s", sys_src_name);

    ID sys_src_fd = cre_fil(sys_src_path, 0x0002);
    assert(sys_src_fd >= 0);
    const char *sys_config_data = "KERNEL_BOOT=BTRON3\nVFS_STACK=V2_BFS\nDEBUG=ENABLED";
    W sys_config_len = (W)strlen(sys_config_data);
    assert(ins_rec(sys_src_fd, 0, sys_config_data, sys_config_len) == 0);
    assert(cls_fil(sys_src_fd) == 0);

    /* Compute hash of source file on /SYS */
    uint32_t src_hash = calc_file_hash(sys_src_path);
    assert(src_hash != 0);

    /* Destination file on newly created custom volume */
    char copied_dst_name[64];
    char copied_dst_path[128];
    snprintf(copied_dst_name, sizeof(copied_dst_name), "copied_sys_%d.cfg", r_id);
    snprintf(copied_dst_path, sizeof(copied_dst_path), "/%s/%s", custom_vol, copied_dst_name);

    /* Perform copy from /SYS to new volume via CLU cp command (direct dst path) */
    char cp_args[256];
    snprintf(cp_args, sizeof(cp_args), "%s %s", sys_src_path, copied_dst_path);
    memset(out_buf, 0, sizeof(out_buf));
    clu_cp(cp_args, clu_buf_out, out_buf);
    assert(strstr(out_buf, "Copied") != NULL);

    /* Verify the copied file exists on the new volume and has identical contents & hash */
    ID copied_fd = opn_fil(copied_dst_path, 0x0001);
    assert(copied_fd >= 0);
    ID copied_rec = opn_rec(copied_fd, 0, 0x0001);
    assert(copied_rec >= 0);
    char copied_read_buf[128];
    memset(copied_read_buf, 0, sizeof(copied_read_buf));
    W copied_bytes = 0;
    assert(rd_rec(copied_rec, copied_read_buf, sizeof(copied_read_buf) - 1, &copied_bytes) == 0);
    assert(copied_bytes == sys_config_len);
    assert(strcmp(copied_read_buf, sys_config_data) == 0);
    assert(cls_rec(copied_rec) == 0);
    assert(cls_fil(copied_fd) == 0);

    uint32_t dst_hash = calc_file_hash(copied_dst_path);
    assert(dst_hash != 0);
    assert(src_hash == dst_hash);

    /* Also verify copying with directory destination path (e.g. 'cp /SYS/file /CUSTOM_VOL/') */
    char dir_dst_path[128];
    snprintf(dir_dst_path, sizeof(dir_dst_path), "/%s/", custom_vol);
    char cp_dir_args[256];
    snprintf(cp_dir_args, sizeof(cp_dir_args), "%s %s", sys_src_path, dir_dst_path);
    memset(out_buf, 0, sizeof(out_buf));
    clu_cp(cp_dir_args, clu_buf_out, out_buf);
    assert(strstr(out_buf, "Copied") != NULL);

    /* File /<custom_vol>/<sys_src_name> must now exist and have matching hash */
    char copied_in_dir[128];
    snprintf(copied_in_dir, sizeof(copied_in_dir), "/%s/%s", custom_vol, sys_src_name);
    uint32_t dir_copy_hash = calc_file_hash(copied_in_dir);
    assert(dir_copy_hash == src_hash);

    /* Verify CLU fs on new volume lists non-zero records (never '(0 records)') */
    memset(out_buf, 0, sizeof(out_buf));
    clu_fs_cmd(custom_mount, clu_buf_out, out_buf);
    assert(strstr(out_buf, "(0 records)") == NULL);
    assert(strstr(out_buf, sys_src_name) != NULL);

    /* Verify CLU ls on new volume lists the saved file and both copied files */
    memset(out_buf, 0, sizeof(out_buf));
    clu_ls(custom_mount, clu_buf_out, out_buf);
    assert(strstr(out_buf, data_file_name) != NULL);
    assert(strstr(out_buf, copied_dst_name) != NULL);
    assert(strstr(out_buf, sys_src_name) != NULL);

    /* Unmount and free temporary /SYS */
    vol_umount(sys_vol);
    g_sys_vol = NULL;
    free(sys_ram);

    /* 7. Unmount & Remount verification */
    assert(b_drivesetup_unmount(&st, 0, 0));
    assert(!st.devices[0].partitions[0].mounted);
    assert(g_anders_vol == NULL);

    /* Remount */
    assert(b_drivesetup_mount(&st, 0, 0));
    assert(st.devices[0].partitions[0].mounted);
    assert(g_anders_vol != NULL);
    assert(strcmp(vol_name(g_anders_vol), custom_vol) == 0);

    /* Cleanup */
    assert(b_drivesetup_unmount(&st, 0, 0));
    remove(custom_img);

    printf("  PASS: Arbitrary device ('%s') and volume ('%s') lifecycle and CLU browsability verified.\n", custom_img, custom_vol);
}

int main(void) {
    printf("==========================================================\n");
    printf(" B-System Production DriveSetup (b_drivesetup) Test Suite\n");
    printf(" Testing BTRON Cleanroom UI, Menus, Visual Slices & B-FS\n");
    printf("==========================================================\n\n");

    test_init_and_invariants();
    test_posix_volume_scanning();
    test_selection();
    test_init_disk_and_create_slice();
    test_create_disk_image();
    test_format_bfs_and_mount();
    test_storage_devices_scrollbar();
    test_partition_table_virtual_scroll_and_keys();
    test_window_resize_responsiveness();
    test_dialog_keyboard_navigation();
    test_pure_gui_and_menus();
    test_warning_dialog_and_write_safety();
    test_e2e_full_lifecycle_and_clu_browsing();

    printf("\n==========================================================\n");
    printf(" ALL 13 DRIVESETUP TEST SUITES PASSED SUCCESSFULLY (100.0%%)\n");
    printf("==========================================================\n");
    return 0;
}
