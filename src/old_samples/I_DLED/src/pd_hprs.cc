//
//	pd_hprs.cc (偽仮身一覧/ポインタプレス処理-仮身のハンドル)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/hmi.h>
#include	<btron/vobj.h>
#include	<btron/libapp.h>

#include	"val.h"
#include	"cval.h"
#include	"err.h"
#include	"except.h"

#include	"appl.h"
#include	"editobj.h"
#include	"vobjs.h"
#include	"guiope.h"
#include	"mainwin.h"
#include	"pd_hprs.h"
#include	"ud_ope.h"
#include	"misc.h"


// ----------------------------------------------------- PD_HPRS 内 public 関数
//
// constructor
//
PD_HPRS::PD_HPRS(W vid, W type)
	: svid(vid), vtype(type)
{
	appl->gui->mwin->disp_selfrm(-1);

	// 選択状態の設定
	if (!(appl->vobjs.get_sel(vid))) {
		// 非選択状態なら選択状態に切り替え
		appl->vobjs.set_allsel(false, false);
		appl->vobjs.set_sel(vid, true);
	}

	// drag 描画環境の生成
	dgid = wsta_drg(wid, 0);
	if (dgid < ER_OK) {
		appl->gui->mwin->disp_selfrm(-2);
		throw EXCEPT_PDOPE(dgid);
	}
	setpointer(PS_PICK, NULL);	// つまみ

	// つかんだ所に応じた固定点(始点)の設定
	RECT	r;

	orsz_vob(vid, &r, V_CHECK);
	if ((type == V_LTHD) || (type == V_LBHD)) {
		sp.x = r.c.right;
		np.x = r.c.left;
	} else {
		sp.x = r.c.left;
		np.x = r.c.right;
	}
	if ((type == V_LTHD) || (type == V_RTHD)) {
		sp.y = r.c.bottom;
		np.y = r.c.top;
	} else {
		sp.y = r.c.top;
		np.y = r.c.bottom;
	}

	// 他の変数の初期化
	W	attr;
	W	chs;

	attr = ochg_sts(vid, V_GETSTS);
	chs = ochg_chs(vid, -1, NULL, V_NODISP);
	if (attr & V_NOFDISP) {
		// 仮身枠なし
		if (!(attr & V_NOPICT)) {
			// ピクトグラムは有り
			chs += chs >> 2;
		}
		min.h = chs;
		min.v = chs;
	} else {
		// 仮身枠あり
		min.h = chs << 1;
		min.v = chs + (chs >> 2) + 2;
	}
}


//
// destructor
//
PD_HPRS::~PD_HPRS()
{
	if (dgid >= 0) {
		// drag 描画環境の廃棄
		wend_drg();
	}
}
 

//
// 実行処理部
//
void	PD_HPRS::main()
{
	W	type;			// event type;

	do {
		UW	time;
		PNT	p, pF;
		bool	modF;

		// event の取得
		pF = p;
		modF = mod;
		type = MISC::wget_drg_C(&p, &wevt);
		if (type == EV_BUTUP) {
			break;
		}
		mod = ((wevt.s.stat & (ES_LSHFT | ES_RSHFT)) != 0x0000);

		// 位置の確認
		if (inrect(vrect, p)) {
			// 自ウィンドウの作業領域内にある時
			if ((p.x != pF.x) || (p.y != pF.y) || (mod != modF)) {
				// 選択枠をつくる
				set_selfrm(p);
			}
			on = false;
			in = true;
		} else if (wevt.s.wid == wid) {
			// ウィンドウ枠上にある時
			adsp_sel(dgid, &srgn, 0);
			if (on) {
				// 既にウィンドウ枠上で時間が経ち始めている
				if ((wevt.s.time - time) > CVAL::SCRL_WAIT) {
					set_spos(p);
				}
			} else {
				// 初めて枠上にきた
				time = wevt.s.time;
				on = true;
			}
			in = false;
		} else {
			// どこでもない
			adsp_sel(dgid, &srgn, 0);
			in = false;
		}

		// scroll 量の確認と scroll 処理
		if ((vp.x != vpN.x) || (vp.y != vpN.y)) {
			adsp_sel(dgid, &srgn, 0);
			appl->gui->mwin->scroll_work((PNT){vpN.x - vp.x, vpN.y - vp.y});
			vrect = appl->gui->mwin->vrect;
			vp = vrect.p.lefttop;
			vpN = vp;
		}
	} while (type != EV_BUTUP);

	// drag 描画環境の廃棄
	adsp_sel(dgid, &srgn, 0);
	wend_drg();
	dgid = -1;

	if (in) {
		// 変形の反映
		SIZE	ds;

		ds.h = ((vtype == V_LTHD) || (vtype == V_LBHD)) ? (np.x - srgn.rgn.r.c.left) : (srgn.rgn.r.c.right - np.x);
		ds.v = ((vtype == V_LTHD) || (vtype == V_RTHD)) ? (np.y - srgn.rgn.r.c.top) : (srgn.rgn.r.c.bottom - np.y);
		if ((ds.h != 0) || (ds.v != 0)) {
			UD_OPE::make_undo(UD_OPE::UT_RESIZE);
			appl->vobjs.rsz_selvobj(ds, vtype);
			appl->gui->mwin->upd_narea();
			appl->eobj->set_editflg(true);	// 更新
		}
	}

	// 選択枠の生成
	appl->gui->mwin->disp_selfrm(-2);

	return;
}


// ---------------------------------------------------- PD_HPRS 内 private 関数
//
// 修正選択状態の確認
//
void	PD_HPRS::chk_modify(PNT& p)
{
	W	ptype;

	ptype = PS_PICK;		// つまみ
	if ((wevt.s.stat & (ES_LSHFT | ES_RSHFT)) != 0x0000) {
		// 修正選択状態
		SIZE	dp;		// 変位

		switch (dir) {
			case 1:
				// 水平変形
				p.y = np.y;
				ptype = PS_HPICK;	// つまみ(横方向)
				break;
			case 2:
				// 垂直変形
				p.x = np.x;
				ptype = PS_VPICK;	// つまみ(縦方向)
				break;
			case 3:
				// 正方変形
				dp.h = ((sp.x > p.x) ? -1 : 1) * (p.x - sp.x);
				dp.v = ((sp.y > p.y) ? -1 : 1) * (p.y - sp.y);
				if (dp.h > dp.v) {
					p.x = sp.x + (((vtype == V_RTHD) || (vtype == V_LBHD)) ? -1 : 1) * (p.y - sp.y);
				} else {
					p.y = sp.y + (((vtype == V_RTHD) || (vtype == V_LBHD)) ? -1 : 1) * (p.x - sp.x);
				}
				break;
			default:
				// 新規(方法の設定)
				dp.h = ((np.x > p.x) ? -1 : 1) * (p.x - np.x);
				dp.v = ((np.y > p.y) ? -1 : 1) * (p.y - np.y);

				dir = (dp.h > dp.v) ? 1 : 2;
				if ((dp.v <= (dp.h << 1)) &&
				    (dp.h <= (dp.v << 1))) {
					dir = 3;
				}
				break;
		}
	} else {
		dir = 0;
	}

	setpointer(ptype, NULL);

	return;
}

//
// 選択枠を設定する
//
void	PD_HPRS::set_selfrm(PNT p)
{
	// 選択枠を消す
	adsp_sel(dgid, &srgn, 0);

	// 形状固定の確認
	chk_modify(p);

	// ハンドル位置に応じた最小枠の検査
	if ((vtype == V_LTHD) || (vtype == V_LBHD)) {
		// 左辺変動
		if ((sp.x - p.x) < min.h) {
			p.x = sp.x - min.h;
		}
	} else {
		// 右辺変動
		if ((p.x - sp.x) < min.h) {
			p.x = sp.x + min.h;
		}
	}
	if ((vtype == V_LTHD) || (vtype == V_RTHD)) {
		// 上底変動
		if ((sp.y - p.y) < min.v) {
			p.y = sp.y - min.v;
		}
	} else {
		// 下底変動
		if ((p.y - sp.y) < min.v) {
			p.y = sp.y + min.v;
		}
	}

	// 選択枠を生成する
	gen_selfrm(sp, p);

	// 選択枠を描く
	adsp_sel(dgid, &srgn, 1);

	return;
}
