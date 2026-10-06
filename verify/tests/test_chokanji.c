#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
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
static int g_skip = 0;

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

#define TEST_SKIP(why) do { \
    printf("SKIP: %s: %s\n", __func__, why); \
    g_skip++; \
    return; \
} while (0)

static const char *find_qcow2_image(void) {
    static const char *candidates[] = {
        "hda.qcow2",
        "../hda.qcow2",
        "PMC/chokanji_4_qemu/hda.qcow2",
        "../PMC/chokanji_4_qemu/hda.qcow2",
        NULL
    };
    const char *env = getenv("BTRON_CHOKANJI_IMAGE");
    if (env && *env) {
        FILE *fp = fopen(env, "rb");
        if (fp) { fclose(fp); return env; }
    }
    for (int i = 0; candidates[i]; i++) {
        FILE *fp = fopen(candidates[i], "rb");
        if (fp) {
            fclose(fp);
            return candidates[i];
        }
    }
    return NULL;
}

/*
 * The image the read-write tests may modify.  BTRON_CHOKANJI_IMAGE is set by
 * `make test-chokanji` to a scratch clone of the golden disk, because these
 * tests allocate real blocks and a failed run must never damage the reference
 * image.  Without it the read-only tests still run against ./hda.qcow2, and the
 * write test reports SKIP rather than writing to that image.
 */
static const char *find_writable_image(void)
{
    const char *env = getenv("BTRON_CHOKANJI_IMAGE");
    if (!env || !*env) return NULL;
    FILE *fp = fopen(env, "r+b");
    if (!fp) return NULL;
    fclose(fp);
    return env;
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
    TEST_ASSERT(vol_fs_type(v) == FS_TYPE_BRIGHTV, "vol_fs_type must return FS_TYPE_BRIGHTV");
    TEST_ASSERT(vol_block_size(v) == 8192, "vol_block_size must return 8192");
    TEST_ASSERT(strcmp(vol_name(v), "B-right/V") == 0, "vol_name must be 'B-right/V'");
    TEST_ASSERT(vol_total_blocks(v) == 1310298U, "total blocks must be 1,310,298");
    UW act_fids = 0;
    for (UW i = 0; i < vol_nfmax(v); i++) {
        if (vol_fid_refcount(v, i) > 0) act_fids++;
    }
    TEST_ASSERT(act_fids == 4457, "expected 4457 active FIDs on B-right/V volume");
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

    /*
     * FID 0 of a B-right/V volume IS its root drawer: its header is the body
     * named after the volume label (block 105 on the golden volume) and its 52
     * link rows are the top level (FS.md 3.2).  rd_dir() replays those rows, and
     * clu's fs tree takes the same list from the hierarchy snapshot -- the two
     * must agree.
     *
     * So the root mixes drawers and plain bodies, and the assertions name both
     * halves: bodies the root drawer names must be here, and bodies a *different*
     * drawer names -- Template Box, Drawing Pad, Text Pad, English, vesainf, etc,
     * LC_TIME, Mail Manager, Makefile, makerules -- must not be, because listing
     * them at the root beside their parent is the duplication this rule exists to
     * remove.
     */
    ID dir = opn_dir("/B-right/V");
    TEST_ASSERT(dir >= 0, "opn_dir(/B-right/V) failed");

    enum { MAXE = 256 };
    static DIR_ENTRY ent[MAXE];
    int count = 0;
    int n_dirs = 0, n_files = 0;
    int dup_fid = 0;
    int self_entry = 0;
    while (count < MAXE && rd_dir(dir, &ent[count]) == 0) {
        if (ent[count].attr & OBJ_DIRECTORY) n_dirs++; else n_files++;
        if (strcasecmp(ent[count].name, vol_name(v)) == 0) self_entry = 1;
        for (int i = 0; i < count; i++) {
            if (ent[i].robj_id == ent[count].robj_id) dup_fid++;
        }
        count++;
    }
    cls_dir(dir);

    TEST_ASSERT(count == fil_hier_nroot(v), "root listing must be the root drawer's rows");
    TEST_ASSERT(count > 10 && count < 100, "root listing is not the root drawer's row count");
    TEST_ASSERT(dup_fid == 0, "a root entry FID may not appear twice");
    TEST_ASSERT(!self_entry, "the root drawer must not list itself");
    TEST_ASSERT(n_dirs > 0 && n_files > 0, "root must mix drawers and plain bodies");

    int nroot = fil_hier_nroot(v);
    TEST_ASSERT(nroot == count, "rd_dir's root must equal the hierarchy snapshot");

    static const char *must_have[] = { "bin", "lib", "unix", "DIC", "FONT",
                                       "USR", "__PROGRAM.BOX", "SBOOT", NULL };
    static const char *must_not[]  = { "Template Box", "Drawing Pad", "Text Pad",
                                       "English", "vesainf", "etc", "LC_TIME",
                                       "Mail Manager", "Makefile", "makerules", NULL };
    for (int k = 0; must_have[k]; k++) {
        int found = 0;
        for (int i = 0; i < count; i++) if (strcasecmp(ent[i].name, must_have[k]) == 0) found = 1;
        TEST_ASSERT(found, "root listing is missing a body the root drawer names");
    }
    for (int k = 0; must_not[k]; k++) {
        int found = 0;
        for (int i = 0; i < count; i++) if (strcasecmp(ent[i].name, must_not[k]) == 0) found = 1;
        /* RUN() unmounts v if this returns, so the failure cannot cascade. */
        if (found) printf("  '%s' has a parent drawer and must not be at the root\n", must_not[k]);
        TEST_ASSERT(!found, "a parented body appeared at the root");
    }

    /*
     * The continuation index (FS.md 7.3).  fid 139 `index`, at
     * /B-right/V/__PROGRAM.BOX/MANUAL/index, declares 1442 rows but its header
     * area holds none of them -- they are filed in the four level-1 index blocks
     * its header points at, and 1440 of those rows are RT_LINK.  Until those
     * entries were followed, 573 bodies -- the whole Japanese B-Book tree -- were
     * named by no drawer the root could walk, so sc could neither list nor open
     * them.  Measured now: `index` is a drawer, its deduped child list is those
     * 573 bodies, and no live body is left without a parent.
     */
    TEST_ASSERT(fil_hier_is_dir(v, 139) == 1, "`index` must be a drawer once its index rows decode");
    TEST_ASSERT(fil_hier_nchild(v, 139) == 573, "`index` must name all 573 B-Book drawers");

    ID bbook = opn_dir("/B-right/V/__PROGRAM.BOX/MANUAL/index");
    TEST_ASSERT(bbook >= 0, "the B-Book tree must open as a directory");
    int bbook_count = 0;
    if (bbook >= 0) {
        DIR_ENTRY be;
        while (rd_dir(bbook, &be) == 0 && bbook_count < 2048) bbook_count++;
        cls_dir(bbook);
    }
    TEST_ASSERT(bbook_count == 573, "rd_dir must hand out `index`'s 573 children");

    int unreached = 0;
    for (UW f = 0; f < vol_nfmax(v); f++) {
        if (f == FID_ROOT || fil_hier_hdr_blk(v, (FID)f) == 0) continue;
        if (fil_hier_parent(v, (FID)f) == FID_INVALID) unreached++;
    }
    TEST_ASSERT(unreached == 0, "every live body must be reachable from the root drawer");

    /* Verify plugins directory entry exists and can be opened */
    ID pfd = opn_fil("/B-right/V/plugins", 0x0001);
    TEST_ASSERT(pfd >= 0, "failed to opn_fil /B-right/V/plugins");
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

    ID fd = opn_fil("/B-right/V/English", 0x0001 /* F_READ */);
    TEST_ASSERT(fd >= 0, "opn_fil(/B-right/V/English) failed");

    /*
     * English is a drawer: four of its records are RT_LINK Virtual Bodies that
     * carry no payload of their own (FS.md 7.4), the rest are data rows.  The
     * old "every record returns bytes" loop passed only because the pre-fix
     * engine decoded no records at all, so opn_rec failed on record 0 and the
     * loop broke out immediately.
     */
    OpenFile *eo = &g_open_files[fd];
    TEST_ASSERT(eo->nrec >= 5, "English must decode both its link and its data records");
    int n_link = 0, n_data = 0;
    static unsigned char ebuf[4096];
    for (unsigned int i = 0; i < eo->nrec; i++) {
        ID rec = opn_rec(fd, (W)i, 0x0001);
        TEST_ASSERT(rec >= 0, "opn_rec on an English record failed");
        if (fil_rec_is_link(fd, (W)i)) {
            FID tfid = FID_INVALID;
            char tname[64] = "";
            ER lerr = fil_get_rec_link_info(fd, (W)i, &tfid, tname, sizeof(tname), NULL);
            TEST_ASSERT(lerr == 0, "English's link record must resolve");
            TEST_ASSERT(tfid != FID_INVALID && tname[0] != '\0',
                        "link record must name a live child");
            n_link++;
            cls_rec(rec);
            continue;
        }
        W read_sz = 0;
        ER err = rd_rec(rec, ebuf, sizeof(ebuf), &read_sz);
        TEST_ASSERT(err == 0, "rd_rec failed");
        TEST_ASSERT(read_sz == (W)eo->ridx[i].size,
                    "a data record must return exactly its indexed size");
        n_data++;
        cls_rec(rec);
    }
    TEST_ASSERT(n_link == 4, "English must expose its four drawer children");
    TEST_ASSERT(n_data >= 1, "English must expose its own data records");
    cls_fil(fd);

    /* Open Text Pad */
    ID fd_text = opn_fil("/B-right/V/Text Pad", 0x0001);
    TEST_ASSERT(fd_text >= 0, "opn_fil(/B-right/V/Text Pad) failed");
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

    ID fd = opn_fil("/B-right/V/vesainf", 0x0001);
    TEST_ASSERT(fd >= 0, "opn_fil(/B-right/V/vesainf) failed");

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
    const char *path = find_writable_image();
    if (!path)
        TEST_SKIP("no scratch image: run `make test-chokanji`, which sets "
                  "BTRON_CHOKANJI_IMAGE to a clone of the golden disk");

    /* Open in read-write mode (read_only = 0) */
    BlkDev *dev = blk_qcow2_create(path, 0);
    TEST_ASSERT(dev != NULL, "failed to open QCOW2 for read-write");

    BlkDev *part = blk_mbr_find_btron_partition(dev, 8192);
    TEST_ASSERT(part != NULL, "failed to slice partition");

    Volume *v = vol_mount(part);
    TEST_ASSERT(v != NULL, "mount failed");

    g_chokanji_vol = v;

    /* Create new file on Cho-Kanji volume (cleanup previous if needed) */
    const char *test_path = "/B-right/V/BTRON_TEST.TXT";
    del_fil(test_path);
    ID wfd = cre_fil(test_path, F_WRITE | F_APPEND | F_CREATE);
    TEST_ASSERT(wfd >= 0, "cre_fil on Cho-Kanji volume failed");

    /*
     * Two records, not one: a single record cannot show whether the row array
     * written by write_header_block() comes back in the same record order the
     * reader assigns (FS.md 7.4 consequence 6).
     */
    const char payload_a[] = "Hello from B-System native Cho-Kanji 4.02 read-write driver!";
    const char payload_b[] = "Second record: pins record numbering across a write/read round trip.";
    ER err = ins_rec(wfd, 0, payload_a, (W)sizeof(payload_a) - 1);
    TEST_ASSERT(err == 0, "ins_rec(record 0) failed");
    err = ins_rec(wfd, 1, payload_b, (W)sizeof(payload_b) - 1);
    TEST_ASSERT(err == 0, "ins_rec(record 1) failed");

    W len_a = (W)strlen(payload_a), len_b = (W)strlen(payload_b);
    unsigned int hdr_blk = (unsigned int)g_open_files[wfd].hdr_blk;
    cls_fil(wfd);

    /* Flush all changes to disk */
    vol_sync(v);

    /* Re-open and verify */
    ID rfd = opn_fil(test_path, 0x0001);
    TEST_ASSERT(rfd >= 0, "re-opening newly created file failed");
    OpenFile *ro = &g_open_files[rfd];
    TEST_ASSERT(ro->nrec == 2, "created file must carry two records after re-open");
    TEST_ASSERT(ro->hdr.total_size == (UW)(len_a + len_b), "total size must be the two payloads");

    /* The header's own row count must agree with what the reader decoded. */
    unsigned char hbuf[8192];
    TEST_ASSERT(vol_read_blk(v, (BLK)hdr_blk, hbuf) == 0, "reading the created header failed");
    unsigned int decl = hbuf[0x50] | (hbuf[0x51] << 8) | (hbuf[0x52] << 16) | ((unsigned int)hbuf[0x53] << 24);
    TEST_ASSERT(decl == 2, "+0x50 row count must be written for a created file");

    struct { W want; const char *text; } expect[2] = {
        { len_a, payload_a }, { len_b, payload_b },
    };
    for (int i = 0; i < 2; i++) {
        ID rrec = opn_rec(rfd, (W)i, 0x0001);
        TEST_ASSERT(rrec >= 0, "opn_rec on new file failed");
        char readback[128];
        memset(readback, 0, sizeof(readback));
        W read_sz = 0;
        err = rd_rec(rrec, readback, sizeof(readback) - 1, &read_sz);
        TEST_ASSERT(err == 0, "rd_rec on newly created file failed");
        TEST_ASSERT(read_sz == expect[i].want, "read size mismatch");
        TEST_ASSERT(strcmp(readback, expect[i].text) == 0,
                    "record content mismatch (wrong record order?)");
        cls_rec(rrec);
    }
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
    if (cur < 500000) {
        snprintf(buf + cur, 524288 - cur, "%s\n", msg);
    }
}

/* Count occurrences of a literal, e.g. how many lines a view tagged [STR]. */
static int count_marker(const char *buf, const char *needle)
{
    int n = 0;
    const char *p = buf;
    while ((p = strstr(p, needle)) != NULL) { n++; p++; }
    return n;
}

/*
 * Count the non-empty lines a clu listing wrote into the buffer: one line per
 * entry. Blank names on the volume print empty lines and are not counted.
 */
static int count_listing_lines(const char *buf)
{
    int n = 0;
    const char *p = buf;
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len > 0) n++;
        if (!e) break;
        p = e + 1;
    }
    return n;
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

    BlkDev *sys_dev = blk_file_create("btron_sys.vol", 0, 1024);
    Volume *sv = NULL;
    if (sys_dev) {
        sv = vol_mount(sys_dev);
        g_sys_vol = sv;
    }

    static char out_buf[262144];
    memset(out_buf, 0, sizeof(out_buf));

    /* Test clu_df with /B-right/V */
    clu_df("/B-right/V", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "/B-right/V") != NULL, "clu_df output missing /B-right/V");
    TEST_ASSERT(strstr(out_buf, "hda1") != NULL, "clu_df output missing device hda1");
    TEST_ASSERT(strstr(out_buf, "8192") != NULL, "clu_df output missing block size 8192");
    TEST_ASSERT(strstr(out_buf, "B-right/V") != NULL, "clu_df output missing volume name B-right/V");

    /* Test clu_cd to /B-right/V */
    memset(out_buf, 0, sizeof(out_buf));
    clu_cd("/B-right/V", clu_buf_out, out_buf);
    TEST_ASSERT(strcmp(g_cwd_path, "/B-right/V") == 0, "g_cwd_path must be /B-right/V");

    /*
     * ls at the volume root lists the root drawer's own link rows, which is what
     * rd_dir() and therefore sc show (FS.md 3.2).  The flat FID-table dump used
     * to print an entry per live body here -- 307 with the magic check, 4457
     * without -- so etc, LC_TIME, Mail Manager, Makefile and makerules sat at
     * the root beside the drawers that actually name them, once per alias FID.
     */
    memset(out_buf, 0, sizeof(out_buf));
    clu_ls("", clu_buf_out, out_buf);
    const int n_entries = count_listing_lines(out_buf);
    TEST_ASSERT(n_entries > 10, "root listing must show the volume's top level");
    TEST_ASSERT(n_entries == fil_hier_nroot(g_chokanji_vol),
                "clu ls at the root must equal the hierarchy snapshot sc lists");
    TEST_ASSERT(strstr(out_buf, "bin") != NULL, "clu_ls output missing drawer 'bin'");
    TEST_ASSERT(strstr(out_buf, "lib") != NULL, "clu_ls output missing drawer 'lib'");
    /* SBOOT is named by the root drawer's first row, so it belongs here; the
     * drawer itself, named after the volume, must not be listed in it. */
    TEST_ASSERT(strstr(out_buf, "SBOOT") != NULL, "root ls must list 'SBOOT', a root child");
    TEST_ASSERT(strstr(out_buf, "B-right/V") == NULL, "root ls must not list the root drawer itself");
    /* Bodies that exist on the volume but are named by some other drawer: */
    TEST_ASSERT(strstr(out_buf, "vesainf") == NULL, "root ls must not list 'vesainf'");
    TEST_ASSERT(strstr(out_buf, "Makefile") == NULL, "root ls must not list 'Makefile'");
    TEST_ASSERT(strstr(out_buf, "LC_TIME") == NULL, "root ls must not list 'LC_TIME'");
    TEST_ASSERT(strstr(out_buf, "Template Box") == NULL, "root ls must not list 'Template Box'");

    /*
     * And they are not lost, just listed where they belong.  Each path below was
     * measured against the drawer whose link record names the body, walking the
     * corrected tree from the root (FS.md 3.2): the Unix hierarchy hangs off the
     * root drawer's `unix` child, not off a top-level `usr`/`locale`/`mnt`.
     */
    memset(out_buf, 0, sizeof(out_buf));
    clu_cd("/B-right/V/bin/hwtool", clu_buf_out, out_buf);
    clu_ls("", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "vesainf") != NULL, "'vesainf' must list under bin/hwtool");
    clu_cd("/B-right/V", clu_buf_out, out_buf);

    memset(out_buf, 0, sizeof(out_buf));
    clu_cd("/B-right/V/unix/usr/local/brightv", clu_buf_out, out_buf);
    clu_ls("", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "etc") != NULL, "'etc' must list under unix/usr/local/brightv");
    clu_cd("/B-right/V", clu_buf_out, out_buf);

    memset(out_buf, 0, sizeof(out_buf));
    clu_cd("/B-right/V/unix/usr/share/locale", clu_buf_out, out_buf);
    clu_ls("", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "en_AU.ISO_8859-1") != NULL,
                "the locale drawer must list its locales");
    clu_cd("/B-right/V/unix/usr/share/locale/en_AU.ISO_8859-1", clu_buf_out, out_buf);
    clu_ls("", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "LC_TIME") != NULL, "'LC_TIME' must list under its locale");
    clu_cd("/B-right/V", clu_buf_out, out_buf);

    /*
     * Two defects meet in this one drawer.  'English' keeps four child links and a
     * 2084-byte record below a run of five unused row slots (FS.md 7.4
     * consequence 5), so the scan that stopped at the first hole listed nothing;
     * and its child 'Limitations on Use' is 18 characters, which the 16-unit name
     * decode cut to 'Limitations on U' -- a segment no later lookup could resolve
     * (FS.md 5.1).  'Template Box' is not English's own child, it hangs one level
     * below, measured against the drawer whose link record names it.
     */
    memset(out_buf, 0, sizeof(out_buf));
    clu_cd("/B-right/V/English", clu_buf_out, out_buf);
    TEST_ASSERT(strcmp(g_cwd_path, "/B-right/V/English") == 0,
                "cd must land on the English drawer");
    memset(out_buf, 0, sizeof(out_buf));
    clu_ls("", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "Installation") != NULL,
                "English's four link records must enumerate as children");
    TEST_ASSERT(strstr(out_buf, "Limitations on Use") != NULL,
                "a body's full name must be listed, not cut at 16 characters");
    TEST_ASSERT(count_listing_lines(out_buf) == 4,
                "English must list its own four children only");

    /*
     * Quoted, the way clu's own argument parser wants a name with spaces in it
     * (get_targets(), src/apps/clu.c).
     */
    memset(out_buf, 0, sizeof(out_buf));
    clu_cd("\"/B-right/V/English/Limitations on Use\"", clu_buf_out, out_buf);
    TEST_ASSERT(strcmp(g_cwd_path, "/B-right/V/English/Limitations on Use") == 0,
                "cd must resolve a path segment longer than 16 characters");
    memset(out_buf, 0, sizeof(out_buf));
    clu_ls("", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "Template Box") != NULL,
                "'Template Box' must list under its own drawer");
    clu_cd("/B-right/V", clu_buf_out, out_buf);

    /* Test clu_ls -l inside /B-right/V */
    memset(out_buf, 0, sizeof(out_buf));
    clu_ls("-l", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "ATYPE") != NULL, "clu_ls -l header missing");

    /* Test cd /B-right/V/plugins and ls: empty directory must NOT show root folder */
    memset(out_buf, 0, sizeof(out_buf));
    clu_cd("/B-right/V/plugins", clu_buf_out, out_buf);
    TEST_ASSERT(strcmp(g_cwd_path, "/B-right/V/plugins") == 0, "g_cwd_path must be /B-right/V/plugins");

    memset(out_buf, 0, sizeof(out_buf));
    clu_ls("", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "SBOOT") == NULL, "ls in /B-right/V/plugins must NOT show root folder SBOOT");
    TEST_ASSERT(strstr(out_buf, "Template Box") == NULL, "ls in /B-right/V/plugins must NOT show root folder Template Box");
    TEST_ASSERT(strstr(out_buf, "Drawing Pad") == NULL, "ls in /B-right/V/plugins must NOT show root folder Drawing Pad");

    /* Return to /B-right/V */
    memset(out_buf, 0, sizeof(out_buf));
    clu_cd("/B-right/V", clu_buf_out, out_buf);
    TEST_ASSERT(strcmp(g_cwd_path, "/B-right/V") == 0, "g_cwd_path must be /B-right/V");

    /* Test clu_fs_cmd inside /CHOKANJI */
    static char fs_buf[524288];
    static char fs_r_buf[524288];
    static char fs_l_buf[524288];
    memset(fs_buf, 0, sizeof(fs_buf));
    memset(fs_r_buf, 0, sizeof(fs_r_buf));
    memset(fs_l_buf, 0, sizeof(fs_l_buf));
    clu_fs_cmd("", clu_buf_out, fs_buf);
    clu_fs_cmd("-r", clu_buf_out, fs_r_buf);
    clu_fs_cmd("-l", clu_buf_out, fs_l_buf);

    static char fs_a_buf[524288];
    memset(fs_a_buf, 0, sizeof(fs_a_buf));
    clu_fs_cmd("-a", clu_buf_out, fs_a_buf);
    TEST_ASSERT(strstr(fs_a_buf, "Real Bodies on") != NULL, "fs -a must have Real Bodies title");
    TEST_ASSERT(strstr(fs_a_buf, "[ELF]") != NULL, "fs -a must identify ELF executables");
    TEST_ASSERT(strstr(fs_a_buf, "real bodies total") != NULL, "fs -a must report total real bodies");
    TEST_ASSERT(strstr(fs_a_buf, "PARENT") != NULL, "fs -a must have PARENT column");
    TEST_ASSERT(strstr(fs_a_buf, "cat") != NULL, "fs -a must find true name cat");
    TEST_ASSERT(strstr(fs_a_buf, "ls") != NULL, "fs -a must find true name ls");
    TEST_ASSERT(strstr(fs_a_buf, "grep") != NULL, "fs -a must find true name grep");
    TEST_ASSERT(strstr(fs_a_buf, "bumount") != NULL, "fs -a must find true name bumount");
    TEST_ASSERT(strstr(fs_a_buf, "bmount") != NULL, "fs -a must find true name bmount");
    /*
     * The flat FID-table view prints the class as a tag and keeps names
     * undecorated, so the stream census is what belongs here -- and it has to be
     * a split: a direct stream is one record of type 0x1F (1925 bodies), not
     * "every body whose header precedes the FID entry" (all 4457), which left
     * nothing to read as a document (FS.md 7.1).
     */
    TEST_ASSERT(strstr(fs_a_buf, "[STR]") != NULL, "fs -a must tag direct-stream bodies");
    TEST_ASSERT(strstr(fs_a_buf, "[TAD]") != NULL, "fs -a must tag multi-record documents");
    const int n_str = count_marker(fs_a_buf, "[STR]");
    const int n_tad = count_marker(fs_a_buf, "[TAD]");
    TEST_ASSERT(n_str > 1000 && n_str < 2500, "the stream census must count one-record stream bodies");
    TEST_ASSERT(n_tad > 1000, "documents with several records must not read as streams");

    /* Test fs -g (Group by directory structure) */
    static char fs_g_buf[524288];
    memset(fs_g_buf, 0, sizeof(fs_g_buf));
    clu_fs_cmd("-g", clu_buf_out, fs_g_buf);
    TEST_ASSERT(strstr(fs_g_buf, "Grouped by [DIR]") != NULL, "fs -g must have Grouped by [DIR] title");
    TEST_ASSERT(strstr(fs_g_buf, "Template Box") != NULL, "fs -g must contain Template Box");
    TEST_ASSERT(strstr(fs_g_buf, "  Packing Box") != NULL, "fs -g must indent child Packing Box under Template Box");

    /* Test fs -t (Normalized Tree Structure) */
    static char fs_t_buf[524288];
    memset(fs_t_buf, 0, sizeof(fs_t_buf));
    clu_fs_cmd("-t", clu_buf_out, fs_t_buf);
    TEST_ASSERT(strstr(fs_t_buf, "Tree Structure") != NULL, "fs -t must have Tree Structure title");
    TEST_ASSERT(strstr(fs_t_buf, "Template Box") != NULL, "fs -t must contain Template Box");
    /* The stream marker belongs to the tree and group views, not the flat table. */
    TEST_ASSERT(strstr(fs_t_buf, "[*]") != NULL, "fs -t must mark direct-stream bodies with [*]");

    /* Test fs -t 3938 (Tree rooted at container 3938 with children 3940-3944 sorted) */
    static char fs_t_3938[16384];
    memset(fs_t_3938, 0, sizeof(fs_t_3938));
    clu_fs_cmd("-t 3938", clu_buf_out, fs_t_3938);
    TEST_ASSERT(strstr(fs_t_3938, "Tree Structure") != NULL, "fs -t 3938 must have Tree Structure title");
    TEST_ASSERT(strstr(fs_t_3938, "3938") != NULL, "fs -t 3938 missing root node 3938");
    TEST_ASSERT(strstr(fs_t_3938, "3940") != NULL, "fs -t 3938 missing child 3940");
    TEST_ASSERT(strstr(fs_t_3938, "3941") != NULL, "fs -t 3938 missing child 3941");
    TEST_ASSERT(strstr(fs_t_3938, "3942") != NULL, "fs -t 3938 missing child 3942");
    TEST_ASSERT(strstr(fs_t_3938, "3943") != NULL, "fs -t 3938 missing child 3943");
    TEST_ASSERT(strstr(fs_t_3938, "3944") != NULL, "fs -t 3938 missing child 3944");
    TEST_ASSERT(strstr(fs_t_3938, "Makefile") != NULL, "fs -t 3938 missing Makefile");
    TEST_ASSERT(strstr(fs_t_3938, "rsdrv.h") != NULL, "fs -t 3938 missing rsdrv.h");
    /* Verify sorted order: 3940 appears before 3941 before 3942 before 3943 before 3944 */
    char *p40 = strstr(fs_t_3938, "3940");
    char *p41 = strstr(fs_t_3938, "3941");
    char *p42 = strstr(fs_t_3938, "3942");
    char *p43 = strstr(fs_t_3938, "3943");
    char *p44 = strstr(fs_t_3938, "3944");
    TEST_ASSERT(p40 && p41 && p42 && p43 && p44 && p40 < p41 && p41 < p42 && p42 < p43 && p43 < p44,
                "fs -t 3938 children must be sorted in ascending order (3940..3944)");

    /*
     * Test fs -g 3938 (Grouped container 3938 with 2-space indented direct children)
     *
     * Measured against the corrected tree: 'sample' (3938) carries three rows --
     * the drawers 'src' (3939) and 'pcat' (3945) and the body 'ReadMe' -- and
     * Makefile is a child of 'src', not of 'sample'.  'src' appearing once here is
     * the whole of the user's "multiple src in root" report: the old flat dump
     * listed it at the root as well, next to the drawer that names it.  'pcat'
     * holds a second body named Makefile, a different FID, which is why the views
     * print the FID and the VFS carries it (FS.md 3.2).
     */
    static char fs_g_3938[16384];
    memset(fs_g_3938, 0, sizeof(fs_g_3938));
    clu_fs_cmd("-g 3938", clu_buf_out, fs_g_3938);
    TEST_ASSERT(strstr(fs_g_3938, "Grouped by [DIR]") != NULL, "fs -g 3938 must have Grouped by [DIR] title");
    TEST_ASSERT(strstr(fs_g_3938, "3938") != NULL, "fs -g 3938 missing parent 3938");
    TEST_ASSERT(strstr(fs_g_3938, "  src") != NULL, "fs -g 3938 must indent its own child src");
    TEST_ASSERT(strstr(fs_g_3938, "  pcat") != NULL, "fs -g 3938 must indent its own child pcat");
    TEST_ASSERT(strstr(fs_g_3938, "[*] ReadMe") != NULL, "fs -g 3938 must indent its stream child ReadMe");
    TEST_ASSERT(strstr(fs_g_3938, "Makefile") == NULL, "fs -g 3938 must not reach past its own rows to Makefile");

    static char fs_g_3939[16384];
    memset(fs_g_3939, 0, sizeof(fs_g_3939));
    clu_fs_cmd("-g 3939", clu_buf_out, fs_g_3939);
    TEST_ASSERT(strstr(fs_g_3939, "  [*] Makefile") != NULL, "fs -g 3939 must indent direct child Makefile under src");
    TEST_ASSERT(strstr(fs_g_3939, "accept.c") != NULL, "fs -g 3939 must list accept.c");
    TEST_ASSERT(strstr(fs_g_3939, "rsdrv.h") != NULL, "fs -g 3939 must list rsdrv.h");

    /* Test fs -l -t and fs -l -g (Attributes included) */
    static char fs_lt_buf[16384];
    memset(fs_lt_buf, 0, sizeof(fs_lt_buf));
    clu_fs_cmd("-l -t 3938", clu_buf_out, fs_lt_buf);
    TEST_ASSERT(strstr(fs_lt_buf, "Tree Structure") != NULL, "fs -l -t 3938 must have Tree Structure title");
    TEST_ASSERT(strstr(fs_lt_buf, "STYPE") != NULL, "fs -l -t must contain STYPE header");
    TEST_ASSERT(strstr(fs_lt_buf, "PDID") != NULL, "fs -l -t must contain PDID header");

    static char fs_lg_buf[16384];
    memset(fs_lg_buf, 0, sizeof(fs_lg_buf));
    clu_fs_cmd("-l -g 3938", clu_buf_out, fs_lg_buf);
    TEST_ASSERT(strstr(fs_lg_buf, "Grouped by [DIR]") != NULL, "fs -l -g 3938 must have Grouped by [DIR] title");
    TEST_ASSERT(strstr(fs_lg_buf, "STYPE") != NULL, "fs -l -g must contain STYPE header");
    TEST_ASSERT(strstr(fs_lg_buf, "PDID") != NULL, "fs -l -g must contain PDID header");

    /* Verify PARENT column is present in both headers */
    TEST_ASSERT(strstr(fs_buf, "PARENT") != NULL, "fs header must contain PARENT column");
    TEST_ASSERT(strstr(fs_r_buf, "PARENT") != NULL, "fs -r header must contain PARENT column");
    TEST_ASSERT(strstr(fs_l_buf, "PARENT") != NULL, "fs -l header must contain PARENT column");

    /*
     * The no-flag view replays the current drawer's rows, so at the root it shows
     * the root drawer's 52 entries and nothing else.  'English' is not one of
     * them -- measured, it is fid 226, a row of fid 223, three levels down inside
     * 'USR' -- so it belongs to that drawer's listing, asserted below.
     */
    TEST_ASSERT(strstr(fs_buf, "USR") != NULL, "clu_fs_cmd on /B-right/V must list the root drawer's USR row");
    TEST_ASSERT(strstr(fs_buf, "bin") != NULL, "clu_fs_cmd on /B-right/V must list the root drawer's bin row");
    TEST_ASSERT(strstr(fs_buf, "English") == NULL, "root fs output must not reach past the root drawer's rows");
    TEST_ASSERT(strstr(fs_buf, "foundations") == NULL, "clu_fs_cmd on /B-right/V should not show ANDERS entries");

    static char fs_g_223[16384];
    memset(fs_g_223, 0, sizeof(fs_g_223));
    clu_fs_cmd("-g 223", clu_buf_out, fs_g_223);
    TEST_ASSERT(strstr(fs_g_223, "English") != NULL, "fs -g 223 must list English, the row it carries");

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

            /* Test fs -t on /ANDERS: natural folded tree structure */
            memset(out_buf, 0, sizeof(out_buf));
            clu_fs_cmd("-t", clu_buf_out, out_buf);
            TEST_ASSERT(strstr(out_buf, "Tree Structure") != NULL, "fs -t on /ANDERS must have Tree Structure title");
            TEST_ASSERT(strstr(out_buf, "ANDERS") != NULL, "fs -t on /ANDERS missing root ANDERS");
            TEST_ASSERT(strstr(out_buf, "  foundations") != NULL, "fs -t on /ANDERS must indent foundations under root");
            TEST_ASSERT(strstr(out_buf, "    logic") != NULL, "fs -t on /ANDERS must indent logic under foundations");
            TEST_ASSERT(strstr(out_buf, "      awodey.anders.txt") != NULL, "fs -t on /ANDERS must indent awodey under logic");

            /* Test fs -t foundations on /ANDERS */
            memset(out_buf, 0, sizeof(out_buf));
            clu_fs_cmd("-t foundations", clu_buf_out, out_buf);
            TEST_ASSERT(strstr(out_buf, "foundations") != NULL, "fs -t foundations missing root node foundations");
            TEST_ASSERT(strstr(out_buf, "  logic") != NULL, "fs -t foundations must indent logic under foundations");
            TEST_ASSERT(strstr(out_buf, "    awodey.anders.txt") != NULL, "fs -t foundations must indent awodey under logic");
            TEST_ASSERT(strstr(out_buf, "mathematics") == NULL, "fs -t foundations must not show sibling mathematics");

            /* Test fs -g on /ANDERS: grouped by container */
            memset(out_buf, 0, sizeof(out_buf));
            clu_fs_cmd("-g", clu_buf_out, out_buf);
            TEST_ASSERT(strstr(out_buf, "Grouped by [DIR]") != NULL, "fs -g on /ANDERS must have Grouped by [DIR] title");
            TEST_ASSERT(strstr(out_buf, "foundations") != NULL, "fs -g on /ANDERS missing foundations group");
            TEST_ASSERT(strstr(out_buf, "  logic") != NULL, "fs -g on /ANDERS must indent logic under foundations");

            vol_umount(g_anders_vol);
            g_anders_vol = NULL;
        }
        blk_destroy(anders_dev);
    }

    /* Switch back to /SYS */
    clu_cd("/SYS", clu_buf_out, out_buf);
    TEST_ASSERT(strcmp(g_cwd_path, "/SYS") == 0, "g_cwd_path must be restored to /SYS");

    if (sv) {
        g_sys_vol = NULL;
        vol_umount(sv);
        blk_destroy(sys_dev);
    }

    g_chokanji_vol = NULL;
    vol_umount(v);
    blk_destroy(part);
    blk_destroy(dev);
    TEST_PASS();
}

/* ── Test 9: CLU tp on Cho-Kanji Headers, Streams, and Binaries ──── */
static void test_clu_tp_chokanji_streams_and_binaries(void)
{
    const char *path = find_qcow2_image();
    TEST_ASSERT(path != NULL, "hda.qcow2 not found");

    BlkDev *dev = blk_qcow2_create(path, 1);
    TEST_ASSERT(dev != NULL, "failed to open QCOW2");

    BlkDev *part = blk_mbr_find_btron_partition(dev, 8192);
    TEST_ASSERT(part != NULL, "failed to slice partition");

    Volume *v = vol_mount(part);
    TEST_ASSERT(v != NULL, "mount failed");
    g_chokanji_vol = v;

    static char out_buf[32768];
    clu_cd("/B-right/V", clu_buf_out, out_buf);

    /* Test 1: tp C header real body by FID (FID 779: elfh) */
    memset(out_buf, 0, sizeof(out_buf));
    clu_tp("/B-right/V#779", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "Polstra") != NULL || strstr(out_buf, "ELF") != NULL,
                "tp /B-right/V#779 should print elfh header content");

    /* Test 2: tp TRON-coded ASCII link header (FID 781: errnoh) */
    memset(out_buf, 0, sizeof(out_buf));
    clu_tp("/B-right/V#781", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "sys/errno.h") != NULL,
                "tp /B-right/V#781 should decode and print sys/errno.h link destination");

    /* Test 3: tp raw stream / config file (FID 3: DEVCONF) */
    memset(out_buf, 0, sizeof(out_buf));
    clu_tp("/B-right/V#3", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "DEVCONF") != NULL,
                "tp /B-right/V#3 should print DEVCONF text stream content");

    /* Test 4: tp ELF binary default mode (FID 4087: cat) */
    memset(out_buf, 0, sizeof(out_buf));
    clu_tp("/B-right/V#4087", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "[ELF 32-bit LSB Executable (i386)]") != NULL,
                "tp /B-right/V#4087 should print ELF executable banner");
    TEST_ASSERT(strstr(out_buf, "7F 45 4C 46") != NULL,
                "tp /B-right/V#4087 preview should include ELF magic hex bytes");

    /* Test 5: tp ELF binary with -x hex dump */
    memset(out_buf, 0, sizeof(out_buf));
    clu_tp("-x /B-right/V#4087", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "0000: 7F 45 4C 46") != NULL,
                "tp -x /B-right/V#4087 should print formatted hex dump starting with ELF magic");

    /* Test 6: stat by FID (FID 781: errno.h) */
    memset(out_buf, 0, sizeof(out_buf));
    clu_stat("/B-right/V#781", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "File: errno.h") != NULL, "stat /B-right/V#781 missing File: errno.h");
    TEST_ASSERT(strstr(out_buf, "FID: 781") != NULL, "stat /B-right/V#781 missing FID: 781");
    TEST_ASSERT(strstr(out_buf, "Size: 22") != NULL, "stat /B-right/V#781 missing Size: 22");

    /* Test 7: stat by name (DEVCONF) */
    memset(out_buf, 0, sizeof(out_buf));
    clu_stat("DEVCONF", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "File: DEVCONF") != NULL, "stat DEVCONF missing File: DEVCONF");
    TEST_ASSERT(strstr(out_buf, "FID: 3") != NULL, "stat DEVCONF missing FID: 3");

    /* Test 8: stat ELF binary by numeric FID (4087) */
    memset(out_buf, 0, sizeof(out_buf));
    clu_stat("4087", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "File: cat") != NULL, "stat 4087 missing File: cat");
    TEST_ASSERT(strstr(out_buf, "Executable") != NULL, "stat 4087 missing Executable");

    clu_cd("/SYS", clu_buf_out, out_buf);
    g_chokanji_vol = NULL;
    vol_umount(v);
    blk_destroy(part);
    blk_destroy(dev);
    TEST_PASS();
}

static void test_volume_isolation_security(void)
{
    const char *path = find_qcow2_image();
    TEST_ASSERT(path != NULL, "hda.qcow2 not found");

    BlkDev *dev = blk_qcow2_create(path, 1);
    TEST_ASSERT(dev != NULL, "failed to open QCOW2");

    BlkDev *part = blk_mbr_find_btron_partition(dev, 8192);
    TEST_ASSERT(part != NULL, "failed to slice partition");

    Volume *v = vol_mount(part);
    TEST_ASSERT(v != NULL, "failed to mount Cho-Kanji volume");
    g_chokanji_vol = v;

    BlkDev *sys_dev = blk_file_create("btron_sys.vol", 0, 1024);
    TEST_ASSERT(sys_dev != NULL, "failed to open btron_sys.vol");
    Volume *sv = vol_mount(sys_dev);
    TEST_ASSERT(sv != NULL, "failed to mount btron_sys.vol");
    g_sys_vol = sv;

    static char out_buf[16384];

    /* ── Case 1: In /SYS, accessing FID 781 (exists on Cho-Kanji, NOT on /SYS) ── */
    clu_cd("/SYS", clu_buf_out, out_buf);

    memset(out_buf, 0, sizeof(out_buf));
    clu_tp("781", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "tp: FID 781 not found on /SYS") != NULL, "tp 781 on /SYS must report FID 781 not found on /SYS");
    TEST_ASSERT(strstr(out_buf, "ERRNO") == NULL, "tp 781 on /SYS must not leak Cho-Kanji errno.h content");

    memset(out_buf, 0, sizeof(out_buf));
    clu_stat("781", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "stat: FID 781 not found on /SYS") != NULL, "stat 781 on /SYS must report FID 781 not found on /SYS");
    TEST_ASSERT(strstr(out_buf, "errno.h") == NULL, "stat 781 on /SYS must not leak Cho-Kanji errno.h metadata");

    memset(out_buf, 0, sizeof(out_buf));
    clu_info("781", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "info: FID 781 not found on /SYS") != NULL, "info 781 on /SYS must report FID 781 not found on /SYS");
    TEST_ASSERT(strstr(out_buf, "errno.h") == NULL, "info 781 on /SYS must not leak Cho-Kanji errno.h metadata");

    memset(out_buf, 0, sizeof(out_buf));
    clu_tp("/SYS#781", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "tp: FID 781 not found on /SYS") != NULL, "tp /SYS#781 must report FID 781 not found on /SYS");

    memset(out_buf, 0, sizeof(out_buf));
    clu_stat("/SYS#781", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "stat: FID 781 not found on /SYS") != NULL, "stat /SYS#781 must report FID 781 not found on /SYS");

    memset(out_buf, 0, sizeof(out_buf));
    clu_fs_cmd("/SYS#781", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "fs: FID 781 not found on /SYS") != NULL, "fs /SYS#781 must report FID 781 not found on /SYS");

    /* ── Case 2: In /B-right/V, accessing non-existent FID ── */
    clu_cd("/B-right/V", clu_buf_out, out_buf);

    memset(out_buf, 0, sizeof(out_buf));
    clu_tp("99999", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "tp: FID 99999 not found on /B-right/V") != NULL, "tp 99999 on /B-right/V must report FID 99999 not found on /B-right/V");

    memset(out_buf, 0, sizeof(out_buf));
    clu_stat("99999", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "stat: FID 99999 not found on /B-right/V") != NULL, "stat 99999 on /B-right/V must report FID 99999 not found on /B-right/V");

    memset(out_buf, 0, sizeof(out_buf));
    clu_tp("/B-right/V#99999", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "tp: FID 99999 not found on /B-right/V") != NULL, "tp /B-right/V#99999 must report not found on /B-right/V");

    /* Explicit cross-volume query for missing FID on /SYS while in /B-right/V */
    memset(out_buf, 0, sizeof(out_buf));
    clu_tp("/SYS#781", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "tp: FID 781 not found on /SYS") != NULL, "tp /SYS#781 from /B-right/V must report FID 781 not found on /SYS");

    /* Legitimate access on /B-right/V must succeed */
    memset(out_buf, 0, sizeof(out_buf));
    clu_stat("/B-right/V#781", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "File: errno.h") != NULL, "stat /B-right/V#781 must show File: errno.h");

    memset(out_buf, 0, sizeof(out_buf));
    clu_tp("/B-right/V#781", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "sys/errno.h") != NULL, "tp /B-right/V#781 must show errno.h contents");

    /* ── Case 3: Unmounted / unknown volume access ── */
    memset(out_buf, 0, sizeof(out_buf));
    clu_tp("/UNKNOWN#1", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "tp: volume '/UNKNOWN' is not mounted") != NULL, "tp /UNKNOWN#1 must report volume '/UNKNOWN' is not mounted");

    memset(out_buf, 0, sizeof(out_buf));
    clu_stat("/UNKNOWN#1", clu_buf_out, out_buf);
    TEST_ASSERT(strstr(out_buf, "stat: volume '/UNKNOWN' is not mounted") != NULL, "stat /UNKNOWN#1 must report volume '/UNKNOWN' is not mounted");

    clu_cd("/SYS", clu_buf_out, out_buf);
    g_chokanji_vol = NULL;
    vol_umount(v);
    blk_destroy(part);
    blk_destroy(dev);
    g_sys_vol = NULL;
    vol_umount(sv);
    blk_destroy(sys_dev);
    TEST_PASS();
}

int main(void)
{
    printf("=== B-System BTRON3 Cho-Kanji (B-right/V 4.02) Mount Tests ===\n\n");

    /*
     * TEST_ASSERT returns, so a test that fails after vol_mount() leaves the
     * volume mounted -- and vol_find_by_prefix() answers a "/B-right/V/..." path
     * with the first mount whose name matches, so a leaked read-only mount
     * silently redirects every later test's writes and one defect reports as
     * three. Reaping between tests keeps each failure its own.
     */
#define RUN(test) do {                                                    \
        int before_ = vol_mounted_count();                                \
        test();                                                           \
        for (int i_ = vol_mounted_count() - 1; i_ >= before_; i_--) {     \
            Volume *leaked_ = vol_get_mounted(i_);                        \
            if (leaked_) {                                                \
                vol_umount(leaked_);                                      \
                printf("  [cleanup] %s left a volume mounted; unmounted\n", #test); \
            }                                                             \
        }                                                                 \
    } while (0)

    RUN(test_qcow2_driver);
    RUN(test_mbr_partition);
    RUN(test_volume_mount);
    RUN(test_directory_enumeration);
    RUN(test_read_tad_document);
    RUN(test_read_driver_binary);
    RUN(test_read_write_operations);
    RUN(test_clu_integration);
    RUN(test_clu_tp_chokanji_streams_and_binaries);
    RUN(test_volume_isolation_security);

#undef RUN

    printf("\n=== Results: %d PASS  %d FAIL  %d SKIP ===\n", g_pass, g_fail, g_skip);
    return (g_fail == 0) ? 0 : 1;
}
