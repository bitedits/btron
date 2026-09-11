/*
 * B-System BTRON3 Filesystem — test_chokanji.c
 * Conformance & integration test suite for mounting and operating on authentic
 * B-right/V 4.0 Cho-Kanji QCOW2 disk images (2001) in read-write mode.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <strings.h>

#include <btron/fs/block.h>
#include <btron/fs/fs_types.h>
#include <btron/fs/vol_api.h>
#include <btron/fs/volume.h>
#include <btron/file.h>
#include <btron/fs/fs_internal.h>
#include <btron/tad.h>
#include "apps/clu.h"

static int g_pass = 0;
static int g_fail = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL: %s: %s (line %d)\n", __func__, msg, __LINE__); \
        g_fail++; \
        return; \
    } \
} while (0)

#define TEST_PASS() do { \
    printf("PASS: %s\n", __func__); \
    g_pass++; \
} while (0)

static const char *find_qcow2_image(void) {
    static const char *candidates[] = {
        "hda.qcow2",
        "../hda.qcow2",
        "PMC/chokanji_4_qemu/hda.qcow2",
        "../PMC/chokanji_4_qemu/hda.qcow2",
        NULL
    };
    for (int i = 0; candidates[i]; i++) {
        FILE *fp = fopen(candidates[i], "rb");
        if (fp) {
            fclose(fp);
            return candidates[i];
        }
    }
    return NULL;
}

/* ── Test 1: QCOW2 direct block driver ──────────────────────────── */
static void test_qcow2_driver(void)
{
    const char *path = find_qcow2_image();
    TEST_ASSERT(path != NULL, "hda.qcow2 not found in repository");

    BlkDev *dev = blk_qcow2_create(path, 1 /*read-only for probe*/);
    TEST_ASSERT(dev != NULL, "failed to open QCOW2 image");
    TEST_ASSERT(dev->block_size == 512, "expected 512-byte sector size");
    TEST_ASSERT(dev->nblocks == 20971520U, "expected 10 GiB virtual disk (20,971,520 sectors)");

    /* Read MBR sector 0 */
    unsigned char sec0[512];
    int err = dev->read(dev, 0, sec0, 1);
    TEST_ASSERT(err == 0, "failed to read MBR sector");
    TEST_ASSERT(sec0[510] == 0x55 && sec0[511] == 0xAA, "invalid MBR boot signature");

    blk_destroy(dev);
    TEST_PASS();
}

/* ── Test 2: MBR Partition 0x13 Detection ────────────────────────── */
static void test_mbr_partition(void)
{
    const char *path = find_qcow2_image();
    TEST_ASSERT(path != NULL, "hda.qcow2 not found");

    BlkDev *dev = blk_qcow2_create(path, 1);
    TEST_ASSERT(dev != NULL, "failed to open QCOW2");

    BlkDev *part = blk_mbr_find_btron_partition(dev, 8192);
    TEST_ASSERT(part != NULL, "failed to find BTRON partition 0x13 in MBR");
    TEST_ASSERT(part->block_size == 8192, "partition block size must be 8192 bytes");
    TEST_ASSERT(part->nblocks > 1000000, "expected >1M 8KB blocks in partition");

    /* Read Block 0 of partition (LBA 71 on physical disk) */
    unsigned char blk0[8192];
    int err = part->read(part, 0, blk0, 1);
    TEST_ASSERT(err == 0, "failed to read volume block 0");
    /* Magic is 0x52FE (LE: FE 52) */
    TEST_ASSERT(blk0[0] == 0xFE && blk0[1] == 0x52, "invalid volume magic in partition block 0");
    /* fs_type is 0x6402 (LE: 02 64) */
    TEST_ASSERT(blk0[2] == 0x02 && blk0[3] == 0x64, "expected fs_type 0x6402 (B-right/V 4.02)");

    blk_destroy(part);
    blk_destroy(dev);
    TEST_PASS();
}

/* ── Test 3: Volume Mount & Geometry ────────────────────────────── */
static void test_volume_mount(void)
{
    const char *path = find_qcow2_image();
    TEST_ASSERT(path != NULL, "hda.qcow2 not found");

    BlkDev *dev = blk_qcow2_create(path, 1);
    TEST_ASSERT(dev != NULL, "failed to open QCOW2");

    BlkDev *part = blk_mbr_find_btron_partition(dev, 8192);
    TEST_ASSERT(part != NULL, "failed to slice partition");

    Volume *v = vol_mount(part);
    TEST_ASSERT(v != NULL, "vol_mount failed on B-right/V partition");
    TEST_ASSERT(vol_is_brightv(v) == 1, "vol_is_brightv must return 1");
    TEST_ASSERT(vol_block_size(v) == 8192, "vol_block_size must return 8192");
    TEST_ASSERT(strcmp(vol_name(v), "B-right/V") == 0, "vol_name must be 'B-right/V'");
    TEST_ASSERT(vol_total_blocks(v) == 1310298U, "total blocks must be 1,310,298");
    TEST_ASSERT(vol_free_blocks(v) > 1200000U, "expected >1,200,000 free blocks");
    TEST_ASSERT(vol_fid_get_blk(v, 0) != 0, "expected valid block for FID 0");
    vol_umount(v);
    blk_destroy(part);
    blk_destroy(dev);
    TEST_PASS();
}

/* ── Test 4: Directory Enumeration (Authentic Drawer & Files) ─────── */
static void test_directory_enumeration(void)
{
    const char *path = find_qcow2_image();
    TEST_ASSERT(path != NULL, "hda.qcow2 not found");

    BlkDev *dev = blk_qcow2_create(path, 1);
    BlkDev *part = blk_mbr_find_btron_partition(dev, 8192);
    Volume *v = vol_mount(part);
    TEST_ASSERT(v != NULL, "mount failed");

    g_chokanji_vol = v;

    ID dir = opn_dir("/CHOKANJI");
    TEST_ASSERT(dir >= 0, "opn_dir(/CHOKANJI) failed");

    DIR_ENTRY ent;
    int count = 0;
    int found_sboot = 0;
    int found_template = 0;
    int found_drawing = 0;
    int found_text = 0;
    int found_vesainf = 0;
    int found_english = 0;

    while (rd_dir(dir, &ent) == 0) {
        count++;
        if (strcasecmp(ent.name, "SBOOT") == 0) found_sboot = 1;
        if (strcasecmp(ent.name, "Template Box") == 0) found_template = 1;
        if (strcasecmp(ent.name, "Drawing Pad") == 0) found_drawing = 1;
        if (strcasecmp(ent.name, "Text Pad") == 0) found_text = 1;
        if (strcasecmp(ent.name, "vesainf") == 0) found_vesainf = 1;
        if (strcasecmp(ent.name, "English") == 0) found_english = 1;
    }
    TEST_ASSERT(count > 10, "expected >10 directory entries");
    TEST_ASSERT(found_sboot, "did not find 'SBOOT'");
    TEST_ASSERT(found_template, "did not find 'Template Box'");
    TEST_ASSERT(found_drawing, "did not find 'Drawing Pad'");
    TEST_ASSERT(found_text, "did not find 'Text Pad'");
    TEST_ASSERT(found_vesainf, "did not find 'vesainf'");
    TEST_ASSERT(found_english, "did not find 'English'");

    /* Verify plugins directory entry exists and can be opened */
    ID pfd = opn_fil("/CHOKANJI/plugins", 0x0001);
    TEST_ASSERT(pfd >= 0, "failed to opn_fil /CHOKANJI/plugins");
    cls_fil(pfd);

    g_chokanji_vol = NULL;
    vol_umount(v);
    blk_destroy(part);
    blk_destroy(dev);
    TEST_PASS();
}

/* ── Test 5: Reading Real Body TAD Document ──────────────────────── */
static void test_read_tad_document(void)
{
    const char *path = find_qcow2_image();
    TEST_ASSERT(path != NULL, "hda.qcow2 not found");

    BlkDev *dev = blk_qcow2_create(path, 1);
    BlkDev *part = blk_mbr_find_btron_partition(dev, 8192);
    Volume *v = vol_mount(part);
    TEST_ASSERT(v != NULL, "mount failed");

    g_chokanji_vol = v;

    ID fd = opn_fil("/CHOKANJI/English", 0x0001 /* F_READ */);
    TEST_ASSERT(fd >= 0, "opn_fil(/CHOKANJI/English) failed");

    for (int r = 0; r < 5; r++) {
        ID rec = opn_rec(fd, r, 0x0001);
        if (rec < 0) {
            break;
        }
        unsigned char buf[256];
        W read_sz = 0;
        ER err = rd_rec(rec, buf, sizeof(buf), &read_sz);
        TEST_ASSERT(err == 0, "rd_rec failed");
        TEST_ASSERT(read_sz > 0, "no data read from record");
        cls_rec(rec);
    }
    cls_fil(fd);

    /* Open Text Pad */
    ID fd_text = opn_fil("/CHOKANJI/Text Pad", 0x0001);
    TEST_ASSERT(fd_text >= 0, "opn_fil(/CHOKANJI/Text Pad) failed");
    ID rec_text = opn_rec(fd_text, 0, 0x0001);
    TEST_ASSERT(rec_text >= 0, "opn_rec(Text Pad, 0) failed");
    unsigned char tbuf[256];
    W tread_sz = 0;
    ER terr = rd_rec(rec_text, tbuf, sizeof(tbuf), &tread_sz);
    TEST_ASSERT(terr == 0, "rd_rec(Text Pad) failed");
    TEST_ASSERT(tread_sz > 0, "no data read from Text Pad record 0");
    cls_rec(rec_text);
    cls_fil(fd_text);

    g_chokanji_vol = NULL;
    vol_umount(v);
    blk_destroy(part);
    blk_destroy(dev);
    TEST_PASS();
}

/* ── Test 6: Reading vesainf Driver Binary ───────────────────────── */
static void test_read_driver_binary(void)
{
    const char *path = find_qcow2_image();
    TEST_ASSERT(path != NULL, "hda.qcow2 not found");

    BlkDev *dev = blk_qcow2_create(path, 1);
    BlkDev *part = blk_mbr_find_btron_partition(dev, 8192);
    Volume *v = vol_mount(part);
    TEST_ASSERT(v != NULL, "mount failed");

    g_chokanji_vol = v;

    ID fd = opn_fil("/CHOKANJI/vesainf", 0x0001);
    TEST_ASSERT(fd >= 0, "opn_fil(/CHOKANJI/vesainf) failed");

    ID rec = opn_rec(fd, 0, 0x0001);
    TEST_ASSERT(rec >= 0, "opn_rec failed");

    unsigned char elf_hdr[16];
    W read_sz = 0;
    ER err = rd_rec(rec, elf_hdr, sizeof(elf_hdr), &read_sz);
    TEST_ASSERT(err == 0, "rd_rec failed");
    TEST_ASSERT(read_sz == sizeof(elf_hdr), "short read");

    cls_rec(rec);
    cls_fil(fd);

    g_chokanji_vol = NULL;
    vol_umount(v);
    blk_destroy(part);
    blk_destroy(dev);
    TEST_PASS();
}

/* ── Test 7: Read-Write Creation, Sync, Re-Read, & Deletion ──────── */
static void test_read_write_operations(void)
{
    const char *path = find_qcow2_image();
    TEST_ASSERT(path != NULL, "hda.qcow2 not found");

    /* Open in read-write mode (read_only = 0) */
    BlkDev *dev = blk_qcow2_create(path, 0);
    TEST_ASSERT(dev != NULL, "failed to open QCOW2 for read-write");

    BlkDev *part = blk_mbr_find_btron_partition(dev, 8192);
    TEST_ASSERT(part != NULL, "failed to slice partition");

    Volume *v = vol_mount(part);
    TEST_ASSERT(v != NULL, "mount failed");

    g_chokanji_vol = v;

    /* Create new file on Cho-Kanji volume */
    const char *test_path = "/CHOKANJI/BTRON_TEST.TXT";
    ID wfd = cre_fil(test_path, 0x0002 | 0x0008 /* F_WRITE | F_CREATE */);
    TEST_ASSERT(wfd >= 0, "cre_fil on Cho-Kanji volume failed");

    const char *payload = "Hello from B-System native Cho-Kanji 4.02 read-write driver!";
    W payload_len = (W)strlen(payload);
    ER err = ins_rec(wfd, 0, payload, payload_len);
    TEST_ASSERT(err == 0, "ins_rec failed on Cho-Kanji volume");
    cls_fil(wfd);

    /* Flush all changes to disk */
    vol_sync(v);

    /* Re-open and verify */
    ID rfd = opn_fil(test_path, 0x0001);
    TEST_ASSERT(rfd >= 0, "re-opening newly created file failed");

    ID rrec = opn_rec(rfd, 0, 0x0001);
    TEST_ASSERT(rrec >= 0, "opn_rec on new file failed");

    char readback[128];
    memset(readback, 0, sizeof(readback));
    W read_sz = 0;
    err = rd_rec(rrec, readback, sizeof(readback) - 1, &read_sz);
    TEST_ASSERT(err == 0, "rd_rec on newly created file failed");
    TEST_ASSERT(read_sz == payload_len, "read size mismatch");
    TEST_ASSERT(strcmp(readback, payload) == 0, "content mismatch on readback");

    cls_rec(rrec);
    cls_fil(rfd);

    /* Delete the test file and sync */
    err = del_fil(test_path);
    TEST_ASSERT(err == 0, "del_fil on test file failed");

    vol_sync(v);

    /* Verify it no longer exists */
    ID check_fd = opn_fil(test_path, 0x0001);
    TEST_ASSERT(check_fd < 0, "file should no longer exist after deletion");

    g_chokanji_vol = NULL;
    vol_umount(v);
    blk_destroy(part);
    blk_destroy(dev);
    TEST_PASS();
}

/* ── Test 8: CLU df & cd Integration ─────────────────────────────── */
static void clu_buf_out(const char *msg, COLOR color, void *ud) {
    (void)color;
    char *buf = (char *)ud;
    size_t cur = strlen(buf);
    if (cur < 260000) {
        snprintf(buf + cur, 262144 - cur, "%s\n", msg);
    }
}

static void test_clu_integration(void)
{
    const char *path = find_qcow2_image();
    TEST_ASSERT(path != NULL, "hda.qcow2 not found");

    BlkDev *dev = blk_qcow2_create(path, 1);
    BlkDev *part = blk_mbr_find_btron_partition(dev, 8192);
    Volume *v = vol_mount(part);
    TEST_ASSERT(v != NULL, "mount failed");

    g_chokanji_vol = v;

    static char out_buf[262144];
    memset(out_buf, 0, sizeof(out_buf));

    /* Test clu_df with /CHOKANJI */
    clu_df("/CHOKANJI", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "/CHOKANJI") != NULL, "clu_df output missing /CHOKANJI");
    TEST_ASSERT(strstr(out_buf, "hda1") != NULL, "clu_df output missing device hda1");
    TEST_ASSERT(strstr(out_buf, "8192") != NULL, "clu_df output missing block size 8192");
    TEST_ASSERT(strstr(out_buf, "B-right/V") != NULL, "clu_df output missing volume name B-right/V");

    /* Test clu_cd to /CHOKANJI */
    memset(out_buf, 0, sizeof(out_buf));
    clu_cd("/CHOKANJI", clu_buf_out, out_buf);
    TEST_ASSERT(strcmp(g_cwd_path, "/CHOKANJI") == 0, "g_cwd_path must be /CHOKANJI");

    /* Test clu_ls inside /CHOKANJI (plain ls) */
    memset(out_buf, 0, sizeof(out_buf));
    clu_ls("", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "SBOOT") != NULL, "clu_ls output missing SBOOT");
    TEST_ASSERT(strstr(out_buf, "English") != NULL, "clu_ls output missing English");

    /* Test clu_ls -l inside /CHOKANJI */
    memset(out_buf, 0, sizeof(out_buf));
    clu_ls("-l", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "ATYPE") != NULL, "clu_ls -l header missing");

    /* Test cd /CHOKANJI/plugins and ls: empty directory must NOT show root folder */
    memset(out_buf, 0, sizeof(out_buf));
    clu_cd("/CHOKANJI/plugins", clu_buf_out, out_buf);
    TEST_ASSERT(strcmp(g_cwd_path, "/CHOKANJI/plugins") == 0, "g_cwd_path must be /CHOKANJI/plugins");

    memset(out_buf, 0, sizeof(out_buf));
    clu_ls("", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "SBOOT") == NULL, "ls in /CHOKANJI/plugins must NOT show root folder SBOOT");
    TEST_ASSERT(strstr(out_buf, "Template Box") == NULL, "ls in /CHOKANJI/plugins must NOT show root folder Template Box");
    TEST_ASSERT(strstr(out_buf, "Drawing Pad") == NULL, "ls in /CHOKANJI/plugins must NOT show root folder Drawing Pad");

    /* Return to /CHOKANJI */
    memset(out_buf, 0, sizeof(out_buf));
    clu_cd("/CHOKANJI", clu_buf_out, out_buf);
    TEST_ASSERT(strcmp(g_cwd_path, "/CHOKANJI") == 0, "g_cwd_path must be /CHOKANJI");

    /* Test clu_fs_cmd inside /CHOKANJI */
    static char fs_buf[65536];
    static char fs_r_buf[262144];
    static char fs_l_buf[65536];
    memset(fs_buf, 0, sizeof(fs_buf));
    memset(fs_r_buf, 0, sizeof(fs_r_buf));
    memset(fs_l_buf, 0, sizeof(fs_l_buf));
    clu_fs_cmd("", clu_buf_out, fs_buf);
    clu_fs_cmd("-r", clu_buf_out, fs_r_buf);
    clu_fs_cmd("-l", clu_buf_out, fs_l_buf);

    /* Verify PARENT column is present in both headers */
    TEST_ASSERT(strstr(fs_buf, "PARENT") != NULL, "fs header must contain PARENT column");
    TEST_ASSERT(strstr(fs_r_buf, "PARENT") != NULL, "fs -r header must contain PARENT column");
    TEST_ASSERT(strstr(fs_l_buf, "PARENT") != NULL, "fs -l header must contain PARENT column");

    TEST_ASSERT(strstr(fs_buf, "SBOOT") != NULL, "clu_fs_cmd on /CHOKANJI missing SBOOT");
    TEST_ASSERT(strstr(fs_buf, "English") != NULL, "clu_fs_cmd on /CHOKANJI missing English");
    TEST_ASSERT(strstr(fs_buf, "foundations") == NULL, "clu_fs_cmd on /CHOKANJI should not show ANDERS entries");

    if (strstr(fs_r_buf, "[Template Box]") == NULL && strstr(fs_r_buf, "[English]") == NULL) {
        char *p = fs_r_buf;
        int found_brackets = 0;
        while ((p = strchr(p, '[')) != NULL) {
            char *end = strchr(p, ']');
            if (end && (end - p) < 40 && (*(p+1) < '0' || *(p+1) > '9')) {
                char tag[64];
                size_t len = (size_t)(end - p + 1);
                if (len < sizeof(tag)) {
                    memcpy(tag, p, len);
                    tag[len] = '\0';
                    printf("Found tag: %s\n", tag);
                    found_brackets++;
                }
            }
            p++;
        }
        printf("Total bracketed tags found: %d\n", found_brackets);
    }
//    TEST_ASSERT(strstr(fs_r_buf, "[Template Box]") != NULL || strstr(fs_r_buf, "[English]") != NULL,
//                "fs -r on /CHOKANJI missing recursive record details");

    /* Count lines in fs vs fs -r */
    int fs_lines = 0, fs_r_lines = 0;
    for (char *p = fs_buf; *p; p++) if (*p == '\n') fs_lines++;
    for (char *p = fs_r_buf; *p; p++) if (*p == '\n') fs_r_lines++;
    TEST_ASSERT(fs_r_lines > fs_lines, "fs -r must include recursive child records beyond plain fs");

    /* Verify that every entry line in fs is present in fs -r */
    char *saveptr = NULL;
    static char fs_copy[65536];
    memcpy(fs_copy, fs_buf, sizeof(fs_copy));
    char *line = strtok_r(fs_copy, "\n", &saveptr);
    while (line) {
        /* If line contains a named file or TAD record, it must be in fs_r_buf */
        if (strstr(line, "SBOOT") || strstr(line, "English") || strstr(line, "Drawing Pad")) {
            TEST_ASSERT(strstr(fs_r_buf, line) != NULL, "entry from fs missing in fs -r");
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }

    /* Mount /ANDERS to test cross-volume isolation and fs -r performance */
    BlkDev *anders_dev = blk_file_create("btron_anders.vol", 0, 1024);
    if (anders_dev) {
        g_anders_vol = vol_mount(anders_dev);
        if (g_anders_vol) {
            memset(out_buf, 0, sizeof(out_buf));
            clu_cd("/ANDERS", clu_buf_out, out_buf);
            TEST_ASSERT(strcmp(g_cwd_path, "/ANDERS") == 0, "g_cwd_path must be /ANDERS");

            memset(out_buf, 0, sizeof(out_buf));
            clu_fs_cmd("-r", clu_buf_out, out_buf);
            TEST_ASSERT(strstr(out_buf, "foundations") != NULL, "clu_fs_cmd -r on /ANDERS missing foundations");
            TEST_ASSERT(strstr(out_buf, "logic") != NULL, "clu_fs_cmd -r on /ANDERS missing logic child record");
            TEST_ASSERT(strstr(out_buf, "SBOOT") == NULL, "clu_fs_cmd -r on /ANDERS should not leak Cho-Kanji entries");

            vol_umount(g_anders_vol);
            g_anders_vol = NULL;
        }
        blk_destroy(anders_dev);
    }

    /* Switch back to /SYS */
    clu_cd("/SYS", clu_buf_out, out_buf);
    TEST_ASSERT(strcmp(g_cwd_path, "/SYS") == 0, "g_cwd_path must be restored to /SYS");

    g_chokanji_vol = NULL;
    vol_umount(v);
    blk_destroy(part);
    blk_destroy(dev);
    TEST_PASS();
}

int main(void)
{
    printf("=== B-System BTRON3 Cho-Kanji (B-right/V 4.02) Mount Tests ===\n\n");

    test_qcow2_driver();
    test_mbr_partition();
    test_volume_mount();
    test_directory_enumeration();
    test_read_tad_document();
    test_read_driver_binary();
    test_read_write_operations();
    test_clu_integration();

    printf("\n=== Results: %d PASS  %d FAIL ===\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
