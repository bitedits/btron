/*
 * v_omgr.c — BTRON 3.20 Real/Virtual Object Manager Verification Suite
 *
 * Normative source: b-spec/os_spec/shell/omgr.html (#ala)
 */

#include "../btron_verify.h"
#include <btron/omgr.h>
#include <btron/error.h>

#define S "ObjectManager"

void vfy_suite_omgr(void)
{
    /* ── Structure Sizes ────────────────────────────────────── */
    VFY_ASSERT_TRUE(S, "sizeof(VLINK)>0", sizeof(VLINK) > 0);
    VFY_ASSERT_TRUE(S, "sizeof(SEL_RGN)>0", sizeof(SEL_RGN) > 0);
    VFY_ASSERT_TRUE(S, "sizeof(SEL_LIST)>0", sizeof(SEL_LIST) > 0);

    /* ── Constants ──────────────────────────────────────────── */
    VFY_ASSERT_EQ(S, "V_NODISP", V_NODISP, 0x0000);
    VFY_ASSERT_EQ(S, "V_DISP", V_DISP, 0x0001);
    VFY_ASSERT_EQ(S, "V_CHKDUP", V_CHKDUP, 0x9999);
    VFY_ASSERT_EQ(S, "V_NONAME", V_NONAME, 0x0001);
    VFY_ASSERT_EQ(S, "EX_VID", EX_VID, -301);
    VFY_ASSERT_EQ(S, "EX_WID", EX_WID, -302);

    /* ── Behavioral API Verification (§3.8.5) ───────────────── */

    /* Register virtual object */
    VLINK vlnk;
    memset(&vlnk, 0, sizeof(vlnk));
    VID vid = oreg_vob(&vlnk, NULL, 1, V_DISP);
    VFY_ASSERT_GE(S, "oreg_vob()", vid, 0);

    /* Duplicate virtual object */
    VID dup_vid = b_odup_vob(vid > 0 ? vid : 1);
    VFY_ASSERT_GE(S, "b_odup_vob()", dup_vid, 0);

    /* Display virtual object */
    ERR dsp_er = odsp_vob(vid > 0 ? vid : 1, V_DISP);
    VFY_ASSERT_EQ(S, "odsp_vob()", dsp_er, E_OK);

    /* Find virtual object */
    PNT pt = { 10, 10 };
    VID fnd_vid = ofnd_vob(1, pt);
    VFY_ASSERT_GE(S, "ofnd_vob()", fnd_vid, 0);

    /* Move virtual object */
    ERR mov_er = omov_vob(vid > 0 ? vid : 1, pt);
    VFY_ASSERT_EQ(S, "omov_vob()", mov_er, E_OK);

    /* Resize virtual object */
    RECT r = { 0, 0, 100, 100 };
    ERR rsz_er = orsz_vob(vid > 0 ? vid : 1, &r);
    VFY_ASSERT_EQ(S, "orsz_vob()", rsz_er, E_OK);

    /* Create real object */
    TC objname[] = { 'T', 'e', 's', 't', 0 };
    VID new_obj = onew_obj(objname, 1);
    VFY_ASSERT_GE(S, "onew_obj()", new_obj, 0);

    VID cre_obj = ocre_obj(objname, 1, 1);
    VFY_ASSERT_GE(S, "ocre_obj()", cre_obj, 0);

    /* Process management via object */
    WERR sta_er = osta_prc(vid > 0 ? vid : 1, 0);
    VFY_ASSERT_GE(S, "osta_prc()", sta_er, 0);

    ERR end_er = oend_prc(vid > 0 ? vid : 1);
    VFY_ASSERT_EQ(S, "oend_prc()", end_er, E_OK);

    /* Data put & update */
    const char data[] = "test data";
    WERR put_er = oput_dat(vid > 0 ? vid : 1, (VP)data, (W)strlen(data));
    VFY_ASSERT_GE(S, "oput_dat()", put_er, 0);

    ERR upd_er = oupd_fil(vid > 0 ? vid : 1);
    VFY_ASSERT_EQ(S, "oupd_fil()", upd_er, E_OK);

    /* Delete virtual object */
    W del_cnt = odel_vob(vid > 0 ? vid : 1, 1);
    VFY_ASSERT_GE(S, "odel_vob()", del_cnt, 0);

    /* ── Application Helpers (§3.8.6) ───────────────────────── */
    SEL_RGN sel;
    memset(&sel, 0, sizeof(sel));
    ERR sel_er = adsp_sel(1, &sel, 1);
    VFY_ASSERT_EQ(S, "adsp_sel()", sel_er, E_OK);

    W mask = 0;
    COLOR col = 0xFFFFFFFF;
    W bgc_er = achg_bgc(&mask, &col, 0);
    VFY_ASSERT_GE(S, "achg_bgc()", bgc_er, 0);
}
