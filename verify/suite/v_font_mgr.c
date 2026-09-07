/*
 * v_font_mgr.c — BTRON 3.20 Font Manager Verification Suite
 *
 * Normative source: b-spec/os_spec/shell/font_mgr.html
 */

#include "../btron_verify.h"
#include <btron/font_mgr.h>
#include <btron/error.h>

#define S "FontManager"

void vfy_suite_font_mgr(void)
{
    /* ── Structure Sizes ────────────────────────────────────── */
    VFY_ASSERT_TRUE(S, "sizeof(FDEF)>0", sizeof(FDEF) > 0);
    VFY_ASSERT_TRUE(S, "sizeof(FSSPEC)>0", sizeof(FSSPEC) > 0);
    VFY_ASSERT_TRUE(S, "sizeof(FCDATA)>0", sizeof(FCDATA) > 0);
    VFY_ASSERT_TRUE(S, "sizeof(FDATA)>0", sizeof(FDATA) > 0);
    VFY_ASSERT_TRUE(S, "sizeof(FLIST)>0", sizeof(FLIST) > 0);
    VFY_ASSERT_TRUE(S, "sizeof(FNTINFO)>0", sizeof(FNTINFO) > 0);

    /* ── Constants ──────────────────────────────────────────── */
    VFY_ASSERT_EQ(S, "FT_MEM", FT_MEM, 0x0001);
    VFY_ASSERT_EQ(S, "FT_FILE", FT_FILE, 0x0002);
    VFY_ASSERT_EQ(S, "FTC_DEFAULT", FTC_DEFAULT, 0);
    VFY_ASSERT_EQ(S, "FTC_MINCHO", FTC_MINCHO, 1);
    VFY_ASSERT_EQ(S, "FTC_GOTHIC", FTC_GOTHIC, 2);

    /* ── Behavioral API Verification (Spec Conformance) ─────── */

    /* Open font set */
    WERR fdesc = fopn_fon();
    VFY_ASSERT_GE(S, "fopn_fon()", fdesc, 0);

    /* Define font */
    WERR fid = fdef_fnt((FLOC)0x1000, FT_MEM);
    VFY_ASSERT_GE(S, "fdef_fnt()", fid, 0);

    /* Get font definition */
    FDEF fdef;
    memset(&fdef, 0, sizeof(fdef));
    ERR def_er = fget_def(fid > 0 ? fid : 1, &fdef);
    VFY_ASSERT_EQ(S, "fget_def()", def_er, E_OK);

    /* Get font note */
    TC note[64];
    WERR not_len = fget_not(fid > 0 ? fid : 1, note, sizeof(note));
    VFY_ASSERT_GE(S, "fget_not()", not_len, 0);

    /* List fonts */
    FLIST flist[8];
    WERR flst_cnt = flst_fon(FT_ALL, flist, sizeof(flist));
    VFY_ASSERT_GE(S, "flst_fon()", flst_cnt, 0);

    /* Set / Get font parameters */
    FSSPEC spec;
    memset(&spec, 0, sizeof(spec));
    spec.size.width = 16;
    spec.size.height = 16;
    ERR set_er = fset_fon(fdesc > 0 ? fdesc : 1, &spec);
    VFY_ASSERT_EQ(S, "fset_fon()", set_er, E_OK);

    ERR get_er = fget_fon(fdesc > 0 ? fdesc : 1, &spec);
    VFY_ASSERT_EQ(S, "fget_fon()", get_er, E_OK);

    /* Angle settings */
    ERR ang_er = fset_ang(fdesc > 0 ? fdesc : 1, 90);
    VFY_ASSERT_EQ(S, "fset_ang()", ang_er, E_OK);

    WERR got_ang = fget_ang(fdesc > 0 ? fdesc : 1);
    VFY_ASSERT_EQ(S, "fget_ang()", got_ang, 90);

    /* Family metrics */
    FNTINFO finf;
    memset(&finf, 0, sizeof(finf));
    WERR fam_er = fget_fam(fdesc > 0 ? fdesc : 1, 0, &finf);
    VFY_ASSERT_GE(S, "fget_fam()", fam_er, 0);

    /* Glyph raster image */
    char cimg_buf[256];
    memset(cimg_buf, 0, sizeof(cimg_buf));
    WERR img_er = fget_img(fdesc > 0 ? fdesc : 1, (FDATA*)cimg_buf, sizeof(cimg_buf), 0, 0x2121, FT_IMAGE);
    VFY_ASSERT_GE(S, "fget_img()", img_er, 0);

    /* Delete font */
    ERR del_er = fdel_fnt(fid > 0 ? fid : 1);
    VFY_ASSERT_EQ(S, "fdel_fnt()", del_er, E_OK);

    /* Close font set */
    ERR cls_er = fcls_fon(fdesc > 0 ? fdesc : 1);
    VFY_ASSERT_EQ(S, "fcls_fon()", cls_er, E_OK);
}
