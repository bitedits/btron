/*
 * BTRON 3.20 Real and Virtual Object Manager API (Менеджер Реальних та Віртуальних об'єктів)
 * include/btron/omgr.h
 *
 * Normative source: b-spec/os_spec/shell/omgr.html
 */

#ifndef __BTRON_OMGR_H__
#define __BTRON_OMGR_H__

#include <btron/types.h>
#include <btron/event.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Virtual Object Flags ─────────────────────────────────────── */
#define V_NODISP        0x0000  /* Do not display */
#define V_DISP          0x0001  /* Normal display */
#define V_DISPALL       0x0002  /* Display all */
#define V_DISPAREA      0x0004  /* Display specified area */
#define V_NOFRAME       0x0080  /* Do not display frame */
#define V_AUTEXE        0x4000  /* Auto execute */

#define V_CHKDUP        0x9999  /* Check link duplicate */
#define V_NONAME        0x0001  /* Do not display name */
#define V_NORELN        0x0002  /* Do not display relation */
#define V_NOTYPE        0x0004  /* Do not display data type */
#define V_NOTIME        0x0008  /* Do not display modification time */
#define V_FIXDEF        0x0010  /* Fixed default application */
#define V_NOIMG         0x0020  /* Do not save display image */
#define V_NOPICT        0x0040  /* Do not display icon */
#define V_NOFDISP       0x0080  /* Do not display border */
#define V_NOEXPND       0x0200  /* Do not expand on print */

/* ── Error Codes ──────────────────────────────────────────────── */
#define EX_VID          (-301)  /* Invalid virtual object ID */
#define EX_WID          (-302)  /* Invalid window ID */
#define EX_LIMIT        (-34)   /* Limit exceeded */
#define EX_NOSPC        (-10)   /* Out of memory */
#define EX_PAR          (-33)   /* Parameter error */
#define EX_ADR          (-34)   /* Invalid address pointer */

/* ── Data Types and Structures ────────────────────────────────── */
typedef ID      VID;    /* Virtual Object ID */
typedef ID      GID;    /* Graphics Context ID */

typedef struct {
    UW          attr;
    ID          target_obj;
    TC          name[32];
    PNT         pos;
    RECT        rect;
} VLINK;

typedef struct {
    UW          sts;
    RECT        rect;
    PNT         pts[8];
    H           num_pts;
} SEL_RGN;

typedef struct {
    H           count;
    SEL_RGN     *rgns;
} SEL_LIST;

/* ── System Calls: Object Manager (§3.8.5) ────────────────────── */

VID  oreg_vob(VLINK *vlnk, VP vseg, W wid, UW disp);
VID  b_odup_vob(W org);
W    odel_vob(W vid, W clr);
ERR  ocls_wnd(W wid);
ERR  odsp_vob(W vid, UW disp);
ERR  odsp_vor(W vid, RECT *r, UW disp);
ERR  odra_vob(W vid, GID gid, UW disp);
ERR  odra_vor(W vid, RECT *r, GID gid, UW disp);
VID  ofnd_vob(W wid, PNT pos);
ERR  omov_vob(W vid, PNT pos);
ERR  orsz_vob(W vid, RECT *rect);
ERR  ochg_chs(W vid, W chs);
ERR  ochg_col(W vid, COLOR col);
ERR  ochg_nam(W vid, const TC *name);
ERR  ochg_rel(W vid, const TC *rel);
VID  onew_obj(const TC *name, W type);
VID  ocre_obj(const TC *name, W type, W wid);
ERR  odsp_inf(W vid);
ERR  odsk_inf(W vid);
ERR  oget_vob(W vid, VLINK *vlnk);
ERR  ochg_sts(W vid, UW sts);
WERR osta_prc(W vid, W opt);
ERR  oend_prc(W vid);
ERR  oend_req(W vid);
WERR oreq_prc(W vid, VP req, W len);
ERR  orsp_prc(W vid, VP rsp, W len);
WERR oput_dat(W vid, VP data, W len);
ERR  oupd_fil(W vid);
ERR  oset_tmf(W vid);
WERR oget_men(W vid, VP menu);
WERR oget_vmn(W vid, VP menu);
ERR  oexe_vmn(W vid, W item);
ERR  ochg_env(W vid, VP env);
ERR  oget_env(W vid, VP env);
ERR  oreg_apg(const TC *name, W apgid);
ERR  odel_apg(W apgid);
ERR  oset_sea(W sea_id);
WERR oget_sea(void);
WERR ofnd_apg(const TC *name);
ERR  oexe_apg(W apgid, W vid);
ERR  odet_fls(W vid);
ERR  ofdt_fls(W vid);
ERR  oatt_fls(W vid);
ERR  ofat_fls(W vid);
ERR  odet_vob(W vid);
ERR  oatt_vob(W vid);
ERR  oprc_dev(W dev_id);
ERR  ofmt_vob(W vid);
ERR  ocnv_vob(W vid, W format);
VID  oopn_obj(W vid);

/* ── Application Helpers (§3.8.6) ─────────────────────────────── */
ERR  adsp_sel(GID gid, SEL_RGN *selp, W mode);
ERR  adsp_slt(GID gid, SEL_LIST *selp, W mode, W dh, W dv);
W    apnl_men(W pnid, W pid, EVT *epv);
W    achg_bgc(W *mask, COLOR *color, COLOR bgc);

#ifdef __cplusplus
}
#endif

#endif /* __BTRON_OMGR_H__ */
