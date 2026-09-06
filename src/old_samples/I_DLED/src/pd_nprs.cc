//
//	pd_nprs.cc (偽仮身一覧/ポインタプレス処理-仮身がない部分)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/hmi.h>
#include	<btron/libapp.h>

#include	"val.h"
#include	"cval.h"
#include	"err.h"
#include	"except.h"

#include	"appl.h"
#include	"vobjs.h"
#include	"guiope.h"
#include	"mainwin.h"
#include	"pd_nprs.h"
#include	"misc.h"


// ----------------------------------------------------- PD_NPRS 内 public 関数
//
// constructor
//
PD_NPRS::PD_NPRS()
{
	appl->gui->mwin->disp_selfrm(-1);

	// drag 描画環境の生成
	dgid = wsta_drg(wid, 0);
	if (dgid < ER_OK) {
		throw EXCEPT_PDOPE(dgid);
	}
}


//
// destructor
//
PD_NPRS::~PD_NPRS()
{
	appl->gui->mwin->disp_selfrm(1);
	if (dgid >= 0) {
		// drag 描画環境の廃棄
		wend_drg();
	}
}
 

//
// 実行処理部
//
void	PD_NPRS::main()
{
	W	type;			// event type

	do {
		UW	time;
		PNT	p;		// drag 描画環境上での PD 位置
		PNT	pF;

		// event の取得
		pF = p;
		type = MISC::wget_drg_C(&p, &wevt);
		if (type == EV_BUTUP) {
			break;
		}

		// 位置の確認
		if (inrect(vrect, p)) {
			// 自ウィンドウの作業領域内にある時
			if ((p.x != pF.x) || (p.y != pF.y)) {
				// 選択枠を移動させる
				adsp_sel(dgid, &srgn, 0);
				gen_selfrm(sp, p);
				adsp_sel(dgid, &srgn, 1);
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
		// 選択範囲の反映
		bool	sel;

		if (mod) {
			// 修正選択
			sel = (!(appl->vobjs.chg_rectsel(srgn.rgn.r, true, true)));
		} else {
			// 通常の選択
			appl->vobjs.set_allsel(false, false);
			sel = true;
		}

		appl->vobjs.chg_rectsel(srgn.rgn.r, sel, false);

	}

	// 選択枠の生成
	appl->gui->mwin->disp_selfrm(-2);

	return;
}
