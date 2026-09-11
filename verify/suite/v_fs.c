/*
 * v_fs.c — BTRON 3.20 Record Stream File System Verification Suite
 *
 * Tests opn_fil, cls_fil, cre_fil, del_fil, chg_fil, mov_fil,
 * opn_rec, cls_rec, rd_rec, wr_rec, ins_rec, del_rec, pos_rec, trn_rec,
 * opn_dir, rd_dir, cls_dir, cre_lnk, del_lnk, ref_vol.
 */

#include "../btron_verify.h"
#include <btron/file.h>
#include <string.h>

#define S "FileSystem"

void vfy_suite_fs(void)
{
    /* ── Structure Sizes ────────────────────────────────────── */
    VFY_ASSERT_TRUE(S, "sizeof(DIR_ENTRY)>0", sizeof(DIR_ENTRY) > 0);
    VFY_ASSERT_TRUE(S, "sizeof(VOL_INFO)>0",  sizeof(VOL_INFO) > 0);

    /* ── File creation & opening ────────────────────────────── */
    ID fd = cre_fil("/test_doc.tad", F_READ | F_WRITE);
    VFY_ASSERT_TRUE(S, "cre_fil(valid)>0", fd > 0);

    if (fd > 0) {
        /* Change file mode */
        ER er = chg_fil(fd, F_READ | F_WRITE | F_APPEND);
        VFY_ASSERT_EQ(S, "chg_fil(valid)", er, E_OK);

        /* ── Record operations ──────────────────────────────── */
        ID rid = opn_rec(fd, 0, F_READ | F_WRITE);
        VFY_ASSERT_TRUE(S, "opn_rec(valid)>0", rid > 0);

        if (rid > 0) {
            /* Write record data */
            const char payload[] = "BTRON Record 0 Content";
            W wrote = 0;
            er = wr_rec(rid, payload, (W)strlen(payload), &wrote);
            VFY_ASSERT_EQ(S, "wr_rec(valid)", er, E_OK);
            VFY_ASSERT_EQ(S, "wr_rec wrote bytes", wrote, (W)strlen(payload));

            /* Seek back to start */
            er = pos_rec(rid, 0, REC_POS_SET);
            VFY_ASSERT_EQ(S, "pos_rec(valid)", er, E_OK);

            /* Read record data */
            char buf[64];
            W read_bytes = 0;
            memset(buf, 0, sizeof(buf));
            er = rd_rec(rid, buf, sizeof(buf), &read_bytes);
            VFY_ASSERT_EQ(S, "rd_rec(valid)", er, E_OK);
            VFY_ASSERT_EQ(S, "rd_rec read bytes", read_bytes, (W)strlen(payload));
            VFY_ASSERT_TRUE(S, "rd_rec round-trip", strcmp(buf, payload) == 0);

            /* Truncate record */
            er = trn_rec(rid, 5);
            VFY_ASSERT_EQ(S, "trn_rec(valid)", er, E_OK);

            /* Close record */
            er = cls_rec(rid);
            VFY_ASSERT_EQ(S, "cls_rec(valid)", er, E_OK);
        }

        /* Insert record at index 1 */
        const char r1_data[] = "Record 1 Data";
        er = ins_rec(fd, 1, r1_data, (W)strlen(r1_data));
        VFY_ASSERT_EQ(S, "ins_rec(valid)", er, E_OK);

        /* Delete record */
        er = del_rec(fd, 1);
        VFY_ASSERT_EQ(S, "del_rec(valid)", er, E_OK);

        /* Close file */
        er = cls_fil(fd);
        VFY_ASSERT_EQ(S, "cls_fil(valid)", er, E_OK);
    }

    /* Move / rename file */
    ER mov_er = mov_fil("/test_doc.tad", "/renamed_doc.tad");
    VFY_ASSERT_EQ(S, "mov_fil(valid)", mov_er, E_OK);

    /* Delete file */
    ER del_er = del_fil("/renamed_doc.tad");
    VFY_ASSERT_EQ(S, "del_fil(valid)", del_er, E_OK);

    /* ── Directory & Link operations ────────────────────────── */
    ID did = opn_dir("/sys");
    VFY_ASSERT_TRUE(S, "opn_dir(valid)>0", did > 0);
    if (did > 0) {
        DIR_ENTRY entry;
        ER er = rd_dir(did, &entry);
        VFY_ASSERT_EQ(S, "rd_dir(valid)", er, E_OK);
        VFY_ASSERT_TRUE(S, "entry.name", strlen(entry.name) > 0);
        cls_dir(did);
    }

    FS_LINK lnk = { 1, 100, {0}, 0 };
    ER lnk_er = cre_lnk("/lnk1", &lnk);
    VFY_ASSERT_EQ(S, "cre_lnk(valid)", lnk_er, E_OK);
    del_lnk("/lnk1");

    VOL_INFO vinfo;
    ER vol_er = ref_vol(1, &vinfo);
    VFY_ASSERT_EQ(S, "ref_vol(valid)", vol_er, E_OK);
    VFY_ASSERT_TRUE(S, "vinfo.block_size==4096", vinfo.block_size == 4096);
}
