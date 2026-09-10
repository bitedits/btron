/*
 * B-System BTRON3 Filesystem — test_fs.c
 * 10 assert-based unit tests for the FS engine.
 *
 * Compile (via Makefile):  make test-fs
 * Run:                     ./tests/test_fs
 *
 * Each test prints  "PASS: test_name"  or  "FAIL: test_name: reason"
 * and returns exit-code 0 on all-pass, 1 on any failure.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Include public FS headers */
#include <btron/fs/block.h>
#include <btron/fs/vol_api.h>
#include <btron/fs/volume.h>
#include <btron/fs/header.h>
#include <btron/fs/record.h>
#include <btron/fs/fs_internal.h>
#include <btron/file.h>
#include <btron/tad.h>

/* Include CLU header for test 10 */
#include "apps/clu.h"

/* ── Test helpers ────────────────────────────────────────────────── */
static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) \
    do { if (!(cond)) { printf("FAIL: %s: %s\n", __func__, msg); g_fail++; return; } } while(0)

#define TEST_PASS() \
    do { printf("PASS: %s\n", __func__); g_pass++; } while(0)

/* ── Helpers to create temporary volumes ─────────────────────────── */
#define VOL_NFMAX 64
#define VOL_NLB   128  /* 128 KiB — enough for tests */

static unsigned char s_vol_buf[128 * 1024];  /* memdisk buffer */

static BlkDev *make_mem_vol(void)
{
    memset(s_vol_buf, 0, sizeof(s_vol_buf));
    return blk_mem_create(s_vol_buf, sizeof(s_vol_buf), 0 /*writable*/);
}

/* ── _Static_assert sizes ─────────────────────────────────────────── */
_Static_assert(sizeof(VolumeHeader) == 128, "VolumeHeader size");
_Static_assert(sizeof(FileHeader)   == 192, "FileHeader size");
_Static_assert(sizeof(RecordIndex)  ==  16, "RecordIndex size");
_Static_assert(sizeof(FragEntry)    ==   6, "FragEntry size");
_Static_assert(sizeof(LinkRecord)   ==  16, "LinkRecord size");

/* ─────────────────────────────────────────────────────────────────── */
/* TEST 1: vol_format — magic and FID 0 check                         */
/* ─────────────────────────────────────────────────────────────────── */
static void test_vol_format(void)
{
    BlkDev *dev = make_mem_vol();
    CHECK(dev != NULL, "blk_mem_create returned NULL");
    int r = vol_format(dev, VOL_NFMAX, VOL_NLB, "SYS");
    CHECK(r == 0, "vol_format failed");

    /* Read back header block */
    unsigned char hbuf[1024];
    dev->read(dev, 0, hbuf, 1);

    /* magic is big-endian in first 2 bytes */
    unsigned short magic = ((unsigned short)hbuf[0] << 8) | hbuf[1];
    CHECK(magic == 0x42FE, "wrong magic (expected 0x42FE)");

    blk_destroy(dev);
    TEST_PASS();
}

/* ─────────────────────────────────────────────────────────────────── */
/* TEST 2: vol_mount / vol_umount — round trip free_blocks            */
/* ─────────────────────────────────────────────────────────────────── */
static void test_vol_mount_umount(void)
{
    BlkDev *dev = make_mem_vol();
    CHECK(dev != NULL, "blk_mem_create");

    int r = vol_format(dev, VOL_NFMAX, VOL_NLB, "SYS");
    CHECK(r == 0, "vol_format");

    Volume *v1 = vol_mount(dev);
    CHECK(v1 != NULL, "first mount failed");
    UW free1 = vol_free_blocks(v1);
    vol_umount(v1);

    /* Re-mount and check free_blocks is stable */
    Volume *v2 = vol_mount(dev);
    CHECK(v2 != NULL, "second mount failed");
    UW free2 = vol_free_blocks(v2);
    CHECK(free1 == free2, "free_blocks changed across mount/umount cycle");
    vol_umount(v2);

    blk_destroy(dev);
    TEST_PASS();
}

/* ─────────────────────────────────────────────────────────────────── */
/* TEST 3: FID 0 root exists after format                             */
/* ─────────────────────────────────────────────────────────────────── */
static void test_fid0_root(void)
{
    BlkDev *dev = make_mem_vol();
    CHECK(dev != NULL, "blk_mem_create");
    vol_format(dev, VOL_NFMAX, VOL_NLB, "SYS");
    Volume *v = vol_mount(dev);
    CHECK(v != NULL, "vol_mount");

    g_sys_vol = v;

    BLK root_blk = vol_fid_get_blk(v, FID_ROOT);
    CHECK(root_blk != 0 && root_blk != FID_INVALID, "FID 0 has no block");

    /* Read FileHeader of root; flags type nibble should be 1 (normal file) */
    unsigned char buf[1024];
    vol_read_blk(v, root_blk, buf);
    unsigned short flags = ((unsigned short)buf[0] << 8) | buf[1];
    unsigned int ftype = (flags >> 12) & 0xF;
    CHECK(ftype == 1, "root FileHeader type != FTYPE_NORMAL(1)");

    vol_umount(v);
    g_sys_vol = NULL;
    blk_destroy(dev);
    TEST_PASS();
}

/* ─────────────────────────────────────────────────────────────────── */
/* TEST 4: cre_fil / ins_rec / rd_rec round trip                      */
/* ─────────────────────────────────────────────────────────────────── */
static void test_cre_ins_rd(void)
{
    BlkDev *dev = make_mem_vol();
    CHECK(dev != NULL, "blk_mem_create");
    vol_format(dev, VOL_NFMAX, VOL_NLB, "SYS");
    Volume *v = vol_mount(dev);
    CHECK(v != NULL, "vol_mount");
    g_sys_vol = v;

    const char *payload = "Hello BTRON Filesystem World!";
    int payload_len = (int)strlen(payload);

    ID fd = cre_fil("Hello", 0x0002);
    CHECK(fd >= 0, "cre_fil failed");

    ER err = ins_rec(fd, 0, (void*)payload, payload_len);
    CHECK(err == 0, "ins_rec failed");
    cls_fil(fd);

    /* Re-open and read back */
    fd = opn_fil("Hello", 0x0001);
    CHECK(fd >= 0, "opn_fil failed after cre_fil");

    ID rec = opn_rec(fd, 0, 0x0001);
    CHECK(rec >= 0, "opn_rec failed");

    char dst[64] = {0};
    W got = 0;
    rd_rec(rec, dst, payload_len, &got);
    CHECK(got == (W)payload_len, "rd_rec: short read");
    CHECK(memcmp(dst, payload, (size_t)payload_len) == 0, "rd_rec: data mismatch");

    cls_rec(rec);
    cls_fil(fd);

    vol_umount(v);
    g_sys_vol = NULL;
    blk_destroy(dev);
    TEST_PASS();
}

/* ─────────────────────────────────────────────────────────────────── */
/* TEST 5: free_block accounting                                       */
/* ─────────────────────────────────────────────────────────────────── */
static void test_free_block_accounting(void)
{
    BlkDev *dev = make_mem_vol();
    CHECK(dev != NULL, "blk_mem_create");
    vol_format(dev, VOL_NFMAX, VOL_NLB, "SYS");
    Volume *v = vol_mount(dev);
    CHECK(v != NULL, "vol_mount");
    g_sys_vol = v;

    UW free_before = vol_free_blocks(v);

    /* Create a file with 1 KiB of payload */
    unsigned char data[1024];
    memset(data, 0xAB, sizeof(data));
    ID fd = cre_fil("BigFile", 0x0002);
    CHECK(fd >= 0, "cre_fil");
    ins_rec(fd, 0, data, sizeof(data));
    cls_fil(fd);
    vol_sync(v);

    UW free_after = vol_free_blocks(v);
    /* At least one block should have been consumed */
    CHECK(free_after < free_before, "free_blocks did not decrease after insert");

    /* Delete the file and check blocks are returned */
    del_fil("BigFile");
    vol_sync(v);
    UW free_del = vol_free_blocks(v);
    CHECK(free_del > free_after, "free_blocks did not increase after del_fil");

    vol_umount(v);
    g_sys_vol = NULL;
    blk_destroy(dev);
    TEST_PASS();
}

/* ─────────────────────────────────────────────────────────────────── */
/* TEST 6: round-trip through file-backed volume                       */
/* ─────────────────────────────────────────────────────────────────── */
static void test_round_trip_image(void)
{
    const char *vol_path = "/tmp/btron_test_roundtrip.vol";
    /* Format */
    {
        BlkDev *dev = blk_file_create(vol_path, 1 /*create*/, 128);
        CHECK(dev != NULL, "blk_file_create (create)");
        int r = vol_format(dev, 64, 128, "SYS");
        CHECK(r == 0, "vol_format");
        Volume *v = vol_mount(dev);
        CHECK(v != NULL, "vol_mount");
        g_sys_vol = v;

        ID fd1 = cre_fil("Alpha", 0x0002);  CHECK(fd1 >= 0, "cre_fil Alpha");
        ins_rec(fd1, 0, "aaa", 3);          cls_fil(fd1);
        ID fd2 = cre_fil("Beta",  0x0002);  CHECK(fd2 >= 0, "cre_fil Beta");
        ins_rec(fd2, 0, "bbb", 3);          cls_fil(fd2);
        ID fd3 = cre_fil("Gamma", 0x0002);  CHECK(fd3 >= 0, "cre_fil Gamma");
        ins_rec(fd3, 0, "ccc", 3);          cls_fil(fd3);

        vol_umount(v);
        g_sys_vol = NULL;
        blk_file_close(dev);
    }
    /* Re-open and enumerate */
    {
        BlkDev *dev = blk_file_create(vol_path, 0 /*open*/, 0);
        CHECK(dev != NULL, "blk_file_create (open)");
        Volume *v = vol_mount(dev);
        CHECK(v != NULL, "vol_mount (2nd)");
        g_sys_vol = v;

        int found_alpha = 0, found_beta = 0, found_gamma = 0;
        ID dir = opn_dir("/SYS");
        CHECK(dir >= 0, "opn_dir");
        DIR_ENTRY entry;
        while (rd_dir(dir, &entry) == 0) {
            if (strcmp(entry.name, "Alpha") == 0) found_alpha = 1;
            if (strcmp(entry.name, "Beta")  == 0) found_beta  = 1;
            if (strcmp(entry.name, "Gamma") == 0) found_gamma = 1;
        }
        cls_dir(dir);

        CHECK(found_alpha && found_beta && found_gamma,
              "one or more files missing after round-trip");

        /* Verify payload of Alpha */
        ID fd = opn_fil("Alpha", 0x0001);
        CHECK(fd >= 0, "opn_fil Alpha after round-trip");
        ID rec = opn_rec(fd, 0, 0x0001);
        CHECK(rec >= 0, "opn_rec Alpha");
        char buf[8] = {0};
        W got = 0;
        rd_rec(rec, buf, 3, &got);
        CHECK(got == 3 && memcmp(buf, "aaa", 3) == 0, "Alpha payload mismatch");
        cls_rec(rec); cls_fil(fd);

        vol_umount(v);
        g_sys_vol = NULL;
        blk_file_close(dev);
    }
    remove(vol_path);
    TEST_PASS();
}

/* ─────────────────────────────────────────────────────────────────── */
/* TEST 7: cre_lnk / opn_fil / rd_rec — RT_LINK payload              */
/* ─────────────────────────────────────────────────────────────────── */
static void test_link_record(void)
{
    BlkDev *dev = make_mem_vol();
    CHECK(dev != NULL, "blk_mem_create");
    vol_format(dev, VOL_NFMAX, VOL_NLB, "SYS");
    Volume *v = vol_mount(dev);
    CHECK(v != NULL, "vol_mount");
    g_sys_vol = v;

    FS_LINK lnk;
    memset(&lnk, 0, sizeof(lnk));
    lnk.target_fid = 42;
    lnk.attr[0]    = 0xAAAA;

    ER err = cre_lnk("MyLink", &lnk);
    CHECK(err == 0, "cre_lnk failed");

    ID fd = opn_fil("MyLink", 0x0001);
    CHECK(fd >= 0, "opn_fil MyLink");

    OpenFile *of = &g_open_files[(int)fd];
    CHECK(of->nrec == 1, "link file must have exactly 1 record");
    CHECK(of->ridx[0].type == (UH)RT_LINK, "record type must be RT_LINK");

    ID rec = opn_rec(fd, 0, 0x0001);
    CHECK(rec >= 0, "opn_rec for link");

    unsigned char payload[32] = {0};
    W got = 0;
    rd_rec(rec, payload, 16, &got);
    CHECK(got >= 4, "link payload too short");

    unsigned int tfid = ((unsigned int)payload[0]<<24)|((unsigned int)payload[1]<<16)|
                        ((unsigned int)payload[2]<<8)|(unsigned int)payload[3];
    CHECK(tfid == 42, "link target_fid mismatch");

    cls_rec(rec);
    cls_fil(fd);
    vol_umount(v);
    g_sys_vol = NULL;
    blk_destroy(dev);
    TEST_PASS();
}

/* ─────────────────────────────────────────────────────────────────── */
/* TEST 8: vol_sync clears dirty flag on disk                          */
/* ─────────────────────────────────────────────────────────────────── */
static void test_vol_sync_dirty(void)
{
    BlkDev *dev = make_mem_vol();
    CHECK(dev != NULL, "blk_mem_create");
    vol_format(dev, VOL_NFMAX, VOL_NLB, "SYS");
    Volume *v = vol_mount(dev);
    CHECK(v != NULL, "vol_mount");
    g_sys_vol = v;

    /* Write something to trigger dirty */
    ID fd = cre_fil("DirtyTest", 0x0002);
    ins_rec(fd, 0, "x", 1);
    cls_fil(fd);

    /* After sync, header dirty byte should be 1 (still mounted) */
    vol_sync(v);
    unsigned char hbuf[1024];
    dev->read(dev, 0, hbuf, 1);
    /* dirty=1 means "currently mounted"; it's only 0 after umount */
    unsigned char dirty = hbuf[19]; /* VolumeHeader.dirty at offset 19 */
    CHECK(dirty == 1, "header.dirty should be 1 (mounted) after vol_sync");

    /* After umount, dirty=0 */
    vol_umount(v);
    dev->read(dev, 0, hbuf, 1);
    dirty = hbuf[19];
    CHECK(dirty == 0, "header.dirty should be 0 after vol_umount");

    g_sys_vol = NULL;
    blk_destroy(dev);
    TEST_PASS();
}

/* ─────────────────────────────────────────────────────────────────── */
/* TEST 9: vol_name_hash is deterministic                              */
/* ─────────────────────────────────────────────────────────────────── */
static void test_name_hash_stable(void)
{
    UW h1 = vol_name_hash("BTRON Spec Book 1");
    UW h2 = vol_name_hash("BTRON Spec Book 1");
    UW h3 = vol_name_hash("BTRON Spec Book 1");
    CHECK(h1 == h2 && h2 == h3, "hash is not deterministic");

    /* Different names should (normally) produce different hashes */
    UW ha = vol_name_hash("Alpha");
    UW hb = vol_name_hash("Beta");
    CHECK(ha != hb, "hash collision between Alpha and Beta");

    TEST_PASS();
}

/* ─────────────────────────────────────────────────────────────────── */
/* TEST 10: clu_ls -l output contains expected column header          */
/* ─────────────────────────────────────────────────────────────────── */
typedef struct { char lines[32][256]; int n; } CapBuf;
static void capture_fn(const char *line, COLOR col, void *ud) {
    CapBuf *cb = (CapBuf *)ud;
    (void)col;
    if (cb->n < 32) {
        strncpy(cb->lines[cb->n++], line, 255);
    }
}

static void test_clu_ls_output(void)
{
    BlkDev *dev = make_mem_vol();
    CHECK(dev != NULL, "blk_mem_create");
    vol_format(dev, VOL_NFMAX, VOL_NLB, "SYS");
    Volume *v = vol_mount(dev);
    CHECK(v != NULL, "vol_mount");
    g_sys_vol = v;

    /* Create a couple of files */
    ID fd = cre_fil("TestFile", 0x0002);
    CHECK(fd >= 0, "cre_fil TestFile");
    ins_rec(fd, 0, "hello", 5);
    cls_fil(fd);

    /* Capture clu_ls -l output */
    CapBuf cb; memset(&cb, 0, sizeof(cb));
    clu_ls("-l", capture_fn, &cb);

    /* Header line must be first non-empty line */
    int found_header = 0;
    for (int i = 0; i < cb.n; i++) {
        if (strstr(cb.lines[i], "ATYPE") && strstr(cb.lines[i], "NREC"))
            found_header = 1;
    }
    CHECK(found_header, "clu_ls -l: header line 'ATYPE ATR NREC' not found");

    /* Check that TestFile appears in output */
    int found_file = 0;
    for (int i = 0; i < cb.n; i++) {
        if (strstr(cb.lines[i], "TestFile")) found_file = 1;
    }
    CHECK(found_file, "clu_ls -l: 'TestFile' not found in output");

    vol_umount(v);
    g_sys_vol = NULL;
    blk_destroy(dev);
    TEST_PASS();
}

/* ─────────────────────────────────────────────────────────────────── */
/* TEST 11: Multi-record operations (insert, seek, shift, delete, reopen)*/
/* ─────────────────────────────────────────────────────────────────── */
static void test_multirec_operations(void)
{
    BlkDev *dev = make_mem_vol();
    CHECK(dev != NULL, "blk_mem_create");
    vol_format(dev, VOL_NFMAX, VOL_NLB, "SYS");
    Volume *v = vol_mount(dev);
    CHECK(v != NULL, "vol_mount");
    g_sys_vol = v;

    ID fd = cre_fil("MultiRec", 0x0002);
    CHECK(fd >= 0, "cre_fil MultiRec");

    const char *p0 = "Record Zero Data";
    const char *p1 = "Record One Longer Content Here";
    const char *p2 = "Rec 2";
    const char *p3 = "Record Three Final Payload";

    CHECK(ins_rec(fd, 0, p0, (W)strlen(p0)) == 0, "ins_rec 0");
    CHECK(ins_rec(fd, 1, p1, (W)strlen(p1)) == 0, "ins_rec 1");
    CHECK(ins_rec(fd, 2, p2, (W)strlen(p2)) == 0, "ins_rec 2");
    CHECK(ins_rec(fd, 3, p3, (W)strlen(p3)) == 0, "ins_rec 3");

    OpenFile *of = &g_open_files[(int)fd];
    CHECK(of->nrec == 4, "nrec must be 4");

    /* Delete record 1 ("Record One...") */
    CHECK(del_rec(fd, 1) == 0, "del_rec 1");
    CHECK(of->nrec == 3, "nrec must be 3 after deletion");

    cls_fil(fd);

    /* Re-open and verify shifted contents */
    fd = opn_fil("MultiRec", 0x0001);
    CHECK(fd >= 0, "opn_fil MultiRec");
    of = &g_open_files[(int)fd];
    CHECK(of->nrec == 3, "re-opened file must have 3 records");

    /* Rec 0 should be p0 */
    ID r0 = opn_rec(fd, 0, 0x0001);
    char buf[64]; memset(buf, 0, sizeof(buf));
    W got = 0;
    rd_rec(r0, buf, (W)strlen(p0), &got);
    CHECK(got == (W)strlen(p0) && strcmp(buf, p0) == 0, "rec 0 mismatch");
    cls_rec(r0);

    /* Rec 1 should be p2 ("Rec 2") */
    ID r1 = opn_rec(fd, 1, 0x0001);
    memset(buf, 0, sizeof(buf));
    rd_rec(r1, buf, (W)strlen(p2), &got);
    CHECK(got == (W)strlen(p2) && strcmp(buf, p2) == 0, "rec 1 mismatch");
    cls_rec(r1);

    /* Rec 2 should be p3 ("Record Three...") */
    ID r2 = opn_rec(fd, 2, 0x0001);
    memset(buf, 0, sizeof(buf));
    rd_rec(r2, buf, (W)strlen(p3), &got);
    CHECK(got == (W)strlen(p3) && strcmp(buf, p3) == 0, "rec 2 mismatch");
    cls_rec(r2);

    cls_fil(fd);
    CHECK(del_fil("MultiRec") == 0, "del_fil MultiRec");

    vol_umount(v);
    g_sys_vol = NULL;
    blk_destroy(dev);
    TEST_PASS();
}

/* ─────────────────────────────────────────────────────────────────── */
/* TEST 12: CLU commands (cp, ren, tp, rm, df)                        */
/* ─────────────────────────────────────────────────────────────────── */
static void test_clu_file_operations(void)
{
    BlkDev *dev = make_mem_vol();
    CHECK(dev != NULL, "blk_mem_create");
    vol_format(dev, VOL_NFMAX, VOL_NLB, "SYS");
    Volume *v = vol_mount(dev);
    CHECK(v != NULL, "vol_mount");
    g_sys_vol = v;

    /* Create DocA */
    ID fd = cre_fil("DocA", 0x0002);
    CHECK(fd >= 0, "cre_fil DocA");
    const char *msg = "Alpha Content";
    ins_rec(fd, 0, msg, (W)strlen(msg));
    cls_fil(fd);

    CapBuf cb;
    /* cp DocA DocB */
    memset(&cb, 0, sizeof(cb));
    clu_cp("DocA DocB", capture_fn, &cb);
    ID fdb = opn_fil("DocB", 0x0001);
    CHECK(fdb >= 0, "DocB must exist after clu_cp");
    cls_fil(fdb);

    /* ren DocB DocC */
    memset(&cb, 0, sizeof(cb));
    clu_ren("DocB DocC", capture_fn, &cb);
    CHECK(opn_fil("DocB", 0x0001) < 0, "DocB must not exist after clu_ren");
    ID fdc = opn_fil("DocC", 0x0001);
    CHECK(fdc >= 0, "DocC must exist after clu_ren");
    cls_fil(fdc);

    /* tp DocC */
    memset(&cb, 0, sizeof(cb));
    clu_tp("DocC", capture_fn, &cb);
    int found_tp = 0;
    for (int i = 0; i < cb.n; i++) {
        if (strstr(cb.lines[i], msg)) found_tp = 1;
    }
    CHECK(found_tp, "clu_tp did not output file payload");

    /* rm DocC */
    memset(&cb, 0, sizeof(cb));
    clu_rm("DocC", capture_fn, &cb);
    CHECK(opn_fil("DocC", 0x0001) < 0, "DocC must not exist after clu_rm");

    /* df */
    memset(&cb, 0, sizeof(cb));
    clu_df("", capture_fn, &cb);
    int found_df = 0;
    for (int i = 0; i < cb.n; i++) {
        if (strstr(cb.lines[i], "SYS") || strstr(cb.lines[i], "blocks"))
            found_df = 1;
    }
    CHECK(found_df, "clu_df did not output volume stats");

    vol_umount(v);
    g_sys_vol = NULL;
    blk_destroy(dev);
    TEST_PASS();
}

/* ─────────────────────────────────────────────────────────────────── */
/* TEST 13: NASA Defensive Programming & Bounds Safety               */
/* ─────────────────────────────────────────────────────────────────── */
static void test_nasa_defensive_and_bounds(void)
{
    BlkDev *dev = make_mem_vol();
    CHECK(dev != NULL, "blk_mem_create");
    vol_format(dev, VOL_NFMAX, VOL_NLB, "SYS");
    Volume *v = vol_mount(dev);
    CHECK(v != NULL, "vol_mount");
    g_sys_vol = v;

    /* 1. NULL safety */
    CHECK(opn_fil(NULL, 0) == -1, "opn_fil(NULL) must fail");
    CHECK(cre_fil(NULL, 0) == -1, "cre_fil(NULL) must fail");
    CHECK(del_fil(NULL) == -1, "del_fil(NULL) must fail");
    CHECK(vol_mount(NULL) == NULL, "vol_mount(NULL) must return NULL");
    vol_sync(NULL);   /* must not crash */
    vol_umount(NULL); /* must not crash */

    /* 2. Bounds and invalid handle checks */
    CHECK(cls_fil(-1) == -1, "cls_fil(-1) must fail");
    CHECK(cls_fil(MAX_OPEN_FILES) == -1, "cls_fil(OOB) must fail");
    CHECK(ins_rec(-1, 0, "a", 1) == -1, "ins_rec(-1) must fail");
    CHECK(del_rec(-1, 0) == -1, "del_rec(-1) must fail");
    CHECK(cls_rec(-1) == -1, "cls_rec(-1) must fail");
    CHECK(pos_rec(-1, 0, 0) == -1, "pos_rec(-1) must fail");
    char temp[4]; W dummy = 0;
    CHECK(rd_rec(-1, temp, 1, &dummy) == -1, "rd_rec(-1) must fail");
    CHECK(wr_rec(-1, temp, 1, &dummy) == -1, "wr_rec(-1) must fail");

    /* 3. Non-existent and root protection */
    CHECK(del_fil("NonExistentFile123") == -1, "del_fil on non-existent must fail");
    CHECK(del_fil("SYS") == -1, "del_fil on root FID 0 must fail (protected)");

    /* 4. Duplicate creation protection */
    ID f1 = cre_fil("UniqueFile", 0x0002);
    CHECK(f1 >= 0, "first cre_fil UniqueFile");
    ID f2 = cre_fil("UniqueFile", 0x0002);
    CHECK(f2 == -1, "second cre_fil UniqueFile must fail");
    cls_fil(f1);

    vol_umount(v);
    g_sys_vol = NULL;
    blk_destroy(dev);
    TEST_PASS();
}

/* ─────────────────────────────────────────────────────────────────── */
/* TEST 14: CLU 'fs' command inspection (root, link, data, -l flag)    */
/* ─────────────────────────────────────────────────────────────────── */
static void test_clu_fs_inspection(void)
{
    BlkDev *dev = make_mem_vol();
    CHECK(dev != NULL, "blk_mem_create");
    vol_format(dev, VOL_NFMAX, VOL_NLB, "SYS");
    Volume *v = vol_mount(dev);
    CHECK(v != NULL, "vol_mount");
    g_sys_vol = v;

    /* Create Chapter 1 with 1 data record */
    ID f1 = cre_fil("Chapter 1", 0x0002);
    CHECK(f1 >= 0, "cre_fil Chapter 1");
    const char *chap_data = "Chapter 1 Introduction Text";
    CHECK(ins_rec(f1, 0, chap_data, (W)strlen(chap_data)) == 0, "ins_rec chap");
    fil_set_rec_type(f1, 0, RT_TADDATA);
    FID fid1 = g_open_files[(int)f1].fid;
    cls_fil(f1);

    /* Insert link to Chapter 1 into root container */
    ID root_fd = opn_fil("SYS", 0x0002);
    CHECK(root_fd >= 0, "opn_fil SYS");
    unsigned char lbuf[16 + 40];
    memset(lbuf, 0, sizeof(lbuf));
    lbuf[0] = (unsigned char)(fid1 >> 24);
    lbuf[1] = (unsigned char)(fid1 >> 16);
    lbuf[2] = (unsigned char)(fid1 >> 8);
    lbuf[3] = (unsigned char)(fid1);
    const char *lname = "Chapter 1";
    unsigned int nlen = (unsigned int)strlen(lname);
    lbuf[14] = (unsigned char)(nlen >> 8);
    lbuf[15] = (unsigned char)(nlen);
    memcpy(lbuf + 16, lname, nlen);
    CHECK(ins_rec(root_fd, 0, lbuf, (W)(16 + nlen)) == 0, "ins link to root");
    fil_set_rec_type(root_fd, 0, RT_LINK);
    cls_fil(root_fd);

    CapBuf cb;
    /* 1. fs with no args on root: must list Chapter 1 link record */
    memset(&cb, 0, sizeof(cb));
    clu_fs_cmd("", capture_fn, &cb);
    CHECK(cb.n >= 2, "fs on root must output header and entries");
    int found_hdr = 0, found_link = 0;
    for (int i = 0; i < cb.n; i++) {
        if (strstr(cb.lines[i], "NO: TYPE STYPE")) found_hdr = 1;
        if (strstr(cb.lines[i], "Chapter 1")) found_link = 1;
    }
    CHECK(found_hdr, "fs normal header missing");
    CHECK(found_link, "fs output did not list 'Chapter 1' link record");

    /* 2. fs -l on root: must list link attributes and FID */
    memset(&cb, 0, sizeof(cb));
    clu_fs_cmd("-l", capture_fn, &cb);
    int found_l_hdr = 0, found_fid_attrs = 0;
    for (int i = 0; i < cb.n; i++) {
        if (strstr(cb.lines[i], "NO: 0 STYPE : FID")) found_l_hdr = 1;
        if (strstr(cb.lines[i], "[0000 0000 0000 0000 0000]") && strstr(cb.lines[i], "Chapter 1"))
            found_fid_attrs = 1;
    }
    CHECK(found_l_hdr, "fs -l header missing");
    CHECK(found_fid_attrs, "fs -l formatted link record missing");

    /* 3. fs on specific file with quotes: fs "Chapter 1" */
    memset(&cb, 0, sizeof(cb));
    clu_fs_cmd("\"Chapter 1\"", capture_fn, &cb);
    int found_chap_rec = 0;
    for (int i = 0; i < cb.n; i++) {
        if (strstr(cb.lines[i], "0:  1") || strstr(cb.lines[i], "27"))
            found_chap_rec = 1;
    }
    CHECK(found_chap_rec, "fs \"Chapter 1\" did not show data record");

    /* 4. fs -l with quotes: fs -l "Chapter 1" */
    memset(&cb, 0, sizeof(cb));
    clu_fs_cmd("-l \"Chapter 1\"", capture_fn, &cb);
    int found_data_rec = 0;
    for (int i = 0; i < cb.n; i++) {
        if (strstr(cb.lines[i], "(data record)")) found_data_rec = 1;
    }
    CHECK(found_data_rec, "fs -l \"Chapter 1\" did not show (data record)");

    vol_umount(v);
    g_sys_vol = NULL;
    blk_destroy(dev);
    TEST_PASS();
}

/* ─────────────────────────────────────────────────────────────────── */
/* TEST 15: CLU 'tp' command variants (-x hex, -a ascii, plain text)   */
/* ─────────────────────────────────────────────────────────────────── */
static void test_clu_tp_variants(void)
{
    BlkDev *dev = make_mem_vol();
    CHECK(dev != NULL, "blk_mem_create");
    vol_format(dev, VOL_NFMAX, VOL_NLB, "SYS");
    Volume *v = vol_mount(dev);
    CHECK(v != NULL, "vol_mount");
    g_sys_vol = v;

    /* Create sample TAD file */
    ID fd = cre_fil("SampleDoc", 0x0002);
    CHECK(fd >= 0, "cre_fil SampleDoc");
    const char *payload = "Hello BTRON World 2026";
    CHECK(ins_rec(fd, 0, payload, (W)strlen(payload)) == 0, "ins_rec");
    fil_set_rec_type(fd, 0, RT_TADDATA);
    cls_fil(fd);

    CapBuf cb;
    /* 1. Plain tp */
    memset(&cb, 0, sizeof(cb));
    clu_tp("SampleDoc", capture_fn, &cb);
    int found_plain = 0;
    for (int i = 0; i < cb.n; i++) {
        if (strstr(cb.lines[i], "Hello BTRON World")) found_plain = 1;
    }
    CHECK(found_plain, "tp plain text output missing");

    /* 2. Hex dump tp -x */
    memset(&cb, 0, sizeof(cb));
    clu_tp("-x SampleDoc", capture_fn, &cb);
    int found_hex = 0;
    for (int i = 0; i < cb.n; i++) {
        /* 'H' = 0x48, 'e' = 0x65, 'l' = 0x6C */
        if (strstr(cb.lines[i], "0000:") && strstr(cb.lines[i], "48 65 6C"))
            found_hex = 1;
    }
    CHECK(found_hex, "tp -x hex dump missing");

    /* 3. ASCII dump tp -a */
    memset(&cb, 0, sizeof(cb));
    clu_tp("-a SampleDoc", capture_fn, &cb);
    int found_ascii = 0;
    for (int i = 0; i < cb.n; i++) {
        if (strstr(cb.lines[i], "Hello BTRON")) found_ascii = 1;
    }
    CHECK(found_ascii, "tp -a ascii dump missing");

    /* 4. Quoted target with flag */
    memset(&cb, 0, sizeof(cb));
    clu_tp("-x \"SampleDoc\"", capture_fn, &cb);
    int found_quoted = 0;
    for (int i = 0; i < cb.n; i++) {
        if (strstr(cb.lines[i], "0000:")) found_quoted = 1;
    }
    CHECK(found_quoted, "tp -x \"SampleDoc\" quoted failed");

    vol_umount(v);
    g_sys_vol = NULL;
    blk_destroy(dev);
    TEST_PASS();
}

/* ─────────────────────────────────────────────────────────────────── */
/* TEST 16: Complete interactive CLU session (CLU.md verbatim cycle)  */
/* ─────────────────────────────────────────────────────────────────── */
static void test_clu_full_interactive_session(void)
{
    BlkDev *dev = make_mem_vol();
    CHECK(dev != NULL, "blk_mem_create");
    vol_format(dev, VOL_NFMAX, VOL_NLB, "SYS");
    Volume *v = vol_mount(dev);
    CHECK(v != NULL, "vol_mount");
    g_sys_vol = v;

    CapBuf cb;

    /* 1. cd with no args shows current */
    memset(&cb, 0, sizeof(cb));
    clu_cd("", capture_fn, &cb);
    CHECK(cb.n > 0 && strstr(cb.lines[0], "/SYS"), "cd with no args must show [/SYS]");

    /* 2. mkf "Project Plan" */
    memset(&cb, 0, sizeof(cb));
    clu_mkf("\"Project Plan\"", capture_fn, &cb);
    CHECK(opn_fil("Project Plan", 0x0001) >= 0, "Project Plan must exist");
    cls_fil(opn_fil("Project Plan", 0x0001));

    /* 3. touch "Project Plan" */
    memset(&cb, 0, sizeof(cb));
    clu_touch("\"Project Plan\"", capture_fn, &cb);
    CHECK(cb.n > 0 && strstr(cb.lines[0], "Touched"), "touch failed");

    /* 4. chtime "Project Plan" */
    memset(&cb, 0, sizeof(cb));
    clu_chtime("\"Project Plan\"", capture_fn, &cb);
    CHECK(cb.n > 0 && strstr(cb.lines[0], "Updated timestamps"), "chtime failed");

    /* 5. ln "Project Plan" "Plan Alias" */
    memset(&cb, 0, sizeof(cb));
    clu_ln("\"Project Plan\" \"Plan Alias\"", capture_fn, &cb);
    CHECK(opn_fil("Plan Alias", 0x0001) >= 0, "Plan Alias must exist");
    cls_fil(opn_fil("Plan Alias", 0x0001));

    /* 6. cp "Project Plan" "Plan Backup" */
    memset(&cb, 0, sizeof(cb));
    clu_cp("\"Project Plan\" \"Plan Backup\"", capture_fn, &cb);
    CHECK(opn_fil("Plan Backup", 0x0001) >= 0, "Plan Backup must exist");
    cls_fil(opn_fil("Plan Backup", 0x0001));

    /* 7. ren "Plan Backup" "Plan Archive" */
    memset(&cb, 0, sizeof(cb));
    clu_ren("\"Plan Backup\" \"Plan Archive\"", capture_fn, &cb);
    CHECK(opn_fil("Plan Backup", 0x0001) < 0, "Plan Backup must be moved");
    CHECK(opn_fil("Plan Archive", 0x0001) >= 0, "Plan Archive must exist");
    cls_fil(opn_fil("Plan Archive", 0x0001));

    /* 8. chmod -a1 "Plan Archive" (set write-protect) */
    memset(&cb, 0, sizeof(cb));
    clu_chmod("-a1 \"Plan Archive\"", capture_fn, &cb);
    CHECK(cb.n > 0 && strstr(cb.lines[0], "Attribute changed"), "chmod failed");

    /* 9. empf "Plan Archive" */
    memset(&cb, 0, sizeof(cb));
    clu_empf("\"Plan Archive\"", capture_fn, &cb);
    CHECK(cb.n > 0 && strstr(cb.lines[0], "Emptied"), "empf failed");

    /* 10. rm "Plan Alias" and "Plan Archive" */
    memset(&cb, 0, sizeof(cb));
    clu_rm("\"Plan Alias\"", capture_fn, &cb);
    CHECK(opn_fil("Plan Alias", 0x0001) < 0, "Plan Alias must be removed");

    memset(&cb, 0, sizeof(cb));
    clu_rm("\"Plan Archive\"", capture_fn, &cb);
    CHECK(opn_fil("Plan Archive", 0x0001) < 0, "Plan Archive must be removed");

    /* 11. cd "Project Plan" -> switches context */
    memset(&cb, 0, sizeof(cb));
    clu_cd("\"Project Plan\"", capture_fn, &cb);
    CHECK(strcmp(g_cwd_path, "/SYS/Project Plan") == 0, "cwd must update to /SYS/Project Plan");

    /* 12. cd .. -> switches back to /SYS */
    memset(&cb, 0, sizeof(cb));
    clu_cd("..", capture_fn, &cb);
    CHECK(strcmp(g_cwd_path, "/SYS") == 0, "cwd must return to /SYS");

    /* 13. df & sync */
    memset(&cb, 0, sizeof(cb));
    clu_df("", capture_fn, &cb);
    CHECK(cb.n >= 2, "df must output header and volume");

    memset(&cb, 0, sizeof(cb));
    clu_sync_cmd("", capture_fn, &cb);
    CHECK(cb.n > 0 && strstr(cb.lines[0], "caches flushed"), "sync failed");

    /* Cleanup */
    clu_rm("\"Project Plan\"", capture_fn, &cb);

    vol_umount(v);
    g_sys_vol = NULL;
    blk_destroy(dev);
    TEST_PASS();
}

/* ─────────────────────────────────────────────────────────────────── */
/* TEST 17: Real btron_sys.vol image inspection (fs, fs -l, tp)       */
/* ─────────────────────────────────────────────────────────────────── */
static void test_real_image_clu(void)
{
    BlkDev *dev = blk_file_create("btron_sys.vol", 0 /* read/write existing */, 1024);
    CHECK(dev != NULL, "open btron_sys.vol");
    Volume *v = vol_mount(dev);
    CHECK(v != NULL, "mount btron_sys.vol");
    g_sys_vol = v;

    CapBuf cb;
    /* 1. fs on root */
    memset(&cb, 0, sizeof(cb));
    clu_fs_cmd("", capture_fn, &cb);
    CHECK(cb.n >= 7, "fs on btron_sys.vol must list header + 6 manifest links");

    int found_spec = 0, found_guide = 0, found_trash = 0;
    for (int i = 0; i < cb.n; i++) {
        if (strstr(cb.lines[i], "BTRON Spec Book 1")) found_spec = 1;
        if (strstr(cb.lines[i], "Cho-Kanji Guide"))   found_guide = 1;
        if (strstr(cb.lines[i], "TRASH"))             found_trash = 1;
    }
    CHECK(found_spec, "BTRON Spec Book 1 missing from fs output");
    CHECK(found_guide, "Cho-Kanji Guide missing from fs output");
    CHECK(found_trash, "TRASH missing from fs output");

    /* 2. fs -l on root */
    memset(&cb, 0, sizeof(cb));
    clu_fs_cmd("-l", capture_fn, &cb);
    CHECK(cb.n >= 7, "fs -l on btron_sys.vol must list header + 6 links");
    int found_l_spec = 0;
    for (int i = 0; i < cb.n; i++) {
        if (strstr(cb.lines[i], "BTRON Spec Book 1") && strstr(cb.lines[i], "[0000 0000 0000 0000 0000]"))
            found_l_spec = 1;
    }
    CHECK(found_l_spec, "fs -l BTRON Spec Book 1 missing attribute brackets");

    /* 3. tp on TAD file */
    memset(&cb, 0, sizeof(cb));
    clu_tp("\"BTRON Spec Book 1\"", capture_fn, &cb);
    CHECK(cb.n > 0, "tp \"BTRON Spec Book 1\" must output content");

    vol_umount(v);
    g_sys_vol = NULL;
    blk_destroy(dev);
    TEST_PASS();
}

/* ─────────────────────────────────────────────────────────────────── */
/* TEST 18: Packed Markdown documents inspection on btron_sys.vol      */
/* ─────────────────────────────────────────────────────────────────── */
static void test_packed_markdown_files(void)
{
    BlkDev *dev = blk_file_create("btron_sys.vol", 0 /* read/write existing */, 1024);
    CHECK(dev != NULL, "open btron_sys.vol for md test");
    Volume *v = vol_mount(dev);
    CHECK(v != NULL, "mount btron_sys.vol for md test");
    g_sys_vol = v;

    CapBuf cb;
    /* 1. fs on root lists all packed .md files */
    memset(&cb, 0, sizeof(cb));
    clu_fs_cmd("", capture_fn, &cb);
    CHECK(cb.n >= 14, "fs on btron_sys.vol must list header + 14 manifest entries");

    int found_fs = 0, found_clu = 0, found_readme = 0, found_tad = 0;
    for (int i = 0; i < cb.n; i++) {
        if (strstr(cb.lines[i], "FS.md"))     found_fs = 1;
        if (strstr(cb.lines[i], "CLU.md"))    found_clu = 1;
        if (strstr(cb.lines[i], "README.md")) found_readme = 1;
        if (strstr(cb.lines[i], "TAD.md"))    found_tad = 1;
    }
    CHECK(found_fs, "FS.md missing from fs listing");
    CHECK(found_clu, "CLU.md missing from fs listing");
    CHECK(found_readme, "README.md missing from fs listing");
    CHECK(found_tad, "TAD.md missing from fs listing");

    /* 2. tp on FS.md must output markdown content */
    memset(&cb, 0, sizeof(cb));
    clu_tp("FS.md", capture_fn, &cb);
    CHECK(cb.n > 0, "tp FS.md must output lines");
    int found_fs_text = 0;
    for (int i = 0; i < cb.n; i++) {
        if (strstr(cb.lines[i], "B-System") || strstr(cb.lines[i], "BTRON") || strstr(cb.lines[i], "File System"))
            found_fs_text = 1;
    }
    CHECK(found_fs_text, "tp FS.md output does not contain expected BTRON text");

    /* 3. Open FS.md directly via opn_fil and verify record size */
    ID fd = opn_fil("FS.md", 0x0001);
    CHECK(fd >= 0, "opn_fil FS.md failed");
    OpenFile *of = &g_open_files[(int)fd];
    CHECK(of->nrec >= 1, "FS.md must have at least 1 record");
    CHECK(of->ridx[0].size > 1000, "FS.md record 0 size must be > 1000 bytes");
    cls_fil(fd);

    vol_umount(v);
    g_sys_vol = NULL;
    blk_destroy(dev);
    TEST_PASS();
}

/* ─────────────────────────────────────────────────────────────────── */
/* TEST 19: Volume /ANDERS & /SYS display in df                        */
/* ─────────────────────────────────────────────────────────────────── */
static void test_df_all_volumes(void)
{
    BlkDev *sys_dev = blk_file_create("btron_sys.vol", 0, 1024);
    CHECK(sys_dev != NULL, "must open btron_sys.vol");
    g_sys_vol = vol_mount(sys_dev);
    CHECK(g_sys_vol != NULL, "must mount btron_sys.vol");

    BlkDev *anders_dev = blk_file_create("btron_anders.vol", 0, 1024);
    CHECK(anders_dev != NULL, "must open btron_anders.vol");
    g_anders_vol = vol_mount(anders_dev);
    CHECK(g_anders_vol != NULL, "must mount btron_anders.vol");

    CapBuf cb;
    memset(&cb, 0, sizeof(cb));
    clu_df("", capture_fn, &cb);

    int found_sys = 0, found_anders = 0;
    for (int i = 0; i < cb.n; i++) {
        if (strstr(cb.lines[i], "/SYS") && strstr(cb.lines[i], "SYS"))
            found_sys = 1;
        if (strstr(cb.lines[i], "/ANDERS") && strstr(cb.lines[i], "ANDERS"))
            found_anders = 1;
    }
    CHECK(found_sys, "clu_df must output /SYS volume stats");
    CHECK(found_anders, "clu_df must output /ANDERS volume stats");

    vol_umount(g_anders_vol);
    g_anders_vol = NULL;
    blk_destroy(anders_dev);

    vol_umount(g_sys_vol);
    g_sys_vol = NULL;
    blk_destroy(sys_dev);

    TEST_PASS();
}

/* ─────────────────────────────────────────────────────────────────── */
/* Main                                                                */
/* ─────────────────────────────────────────────────────────────────── */
int main(void)
{
    printf("=== B-System BTRON3 FS Unit Tests ===\n\n");

    test_vol_format();
    test_vol_mount_umount();
    test_fid0_root();
    test_cre_ins_rd();
    test_free_block_accounting();
    test_round_trip_image();
    test_link_record();
    test_vol_sync_dirty();
    test_name_hash_stable();
    test_clu_ls_output();
    test_multirec_operations();
    test_clu_file_operations();
    test_nasa_defensive_and_bounds();
    test_clu_fs_inspection();
    test_clu_tp_variants();
    test_clu_full_interactive_session();
    test_real_image_clu();
    test_packed_markdown_files();
    test_df_all_volumes();

    printf("\n=== Results: %d PASS  %d FAIL ===\n", g_pass, g_fail);
    return (g_fail > 0) ? 1 : 0;
}

