//
//	pd_vprs.cc (偽仮身一覧/ポインタプレス処理-仮身をつかむ)
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
#include	"vobjope.h"
#include	"vobjs.h"
#include	"guiope.h"
#include	"mainwin.h"
#include	"pd_vprs.h"
#include	"selfrm.h"
#include	"ud_ope.h"
#include	"trayope.h"
#include	"misc.h"


// ----------------------------------------------------- PD_VPRS 内 public 関数
//
// constructor
//
PD_VPRS::PD_VPRS(W vid)
{
	if (!(appl->vobjs.get_sel(vid))) {
		// 非選択状態なら選択状態にする
		appl->vobjs.set_allsel(false, false);
		appl->vobjs.set_sel(vid, true);
	}

	appl->gui->mwin->disp_selfrm(-2);

	// drag 描画環境の生成
	dgid = wsta_drg(wid, 0);
	if (dgid < ER_OK) {
		throw EXCEPT_PDOPE(dgid);
	}
	setpointer(PS_GRIP, NULL);	// 握り

	// 他の変数の設定
	sp = wevt.s.pos;
	gget_fra(dgid, &frect);
	vr = appl->gui->mwin->selfrm->get_frect();
	gap = (SIZE){sp.x - vr.c.left, sp.y - vr.c.top};
	srgn.sts = 0x0000;

	appl->gui->mwin->disp_selfrm(-1);
}


//
// destructor
//
PD_VPRS::~PD_VPRS()
{
	appl->gui->mwin->disp_selfrm(1);
	if (dgid >= 0) {
		wend_drg();
	}
}
 

//
// 実行処理部
//
void	PD_VPRS::main()
{
	W	type;			// event type

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
			if (lock) {
				// 描画環境の lock を解除する
				adsp_sel(dgid, &srgn, 0);
				gset_vis(dgid, vrect);
				gloc_env(dgid, 0);
				lock = false;
			}
			if ((p.x != pF.x) || (p.y != pF.y) || (mod != modF)) {
				// 選択枠を移動させる
				move_selfrm(vr, p, gap);
			}
			on = false;
			in = true;
		} else if (wevt.s.wid == wid) {
			// ウィンドウ枠上にある時
			adsp_sel(dgid, &srgn, 0);
			if (lock) {
				// 描画環境の lock を解除する
				gset_vis(dgid, vrect);
				gloc_env(dgid, 0);
				lock = false;
			}
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
			// 自ウィンドウ外に持っていこうとしている
			if (!(lock)) {
				// 描画環境を lock する
				adsp_sel(dgid, &srgn, 0);
				gloc_env(dgid, 1);
				gset_vis(dgid, frect);
				lock = true;
			}
			if ((p.x != pF.x) || (p.y != pF.y)) {
				// 選択枠を移動させる
				move_selfrm(vr, p, gap);
			}
			in = false;
			on = false;
		}

		// scroll 量の確認と scroll 処理
		if ((vp.x != vpN.x) || (vp.y != vpN.y)) {
			adsp_sel(dgid, &srgn, 0);
			appl->gui->mwin->scroll_work((PNT){vpN.x - vp.x, vpN.y - vp.y});
			vrect = appl->gui->mwin->vrect;
			vp = vrect.p.lefttop;
			vpN = vp;
			gget_fra(dgid, &frect);	// wscr_wnd() で動いている為
		}
	} while (type != EV_BUTUP);

	// drag 描画環境の廃棄
	adsp_sel(dgid, &srgn, 0);
	if (lock) {
		// 描画環境の lock を解除する
		gset_vis(dgid, vrect);
		gloc_env(dgid, 0);
		lock = false;
	}
	wend_drg();
	dgid = -1;

	// 選択枠の生成
	appl->gui->mwin->disp_selfrm(-2);

	// 結果の反映
	if ((!(on)) && (wevt.s.wid >= 1)) {
		// 仮身の複写/移動
		bool	mov;		// true : 移動

		mov = ((wevt.s.stat & (ES_CMD | ES_BUT2)) == 0x0000);
		if (in) {
			// 自ウィンドウの作業領域内
			SIZE	dl;

			dl.h = srgn.rgn.r.c.left - vr.c.left;
			dl.v = srgn.rgn.r.c.top - vr.c.top;
			appl->eobj->set_editflg(true);	// 更新
			try {
				if (mov) {
					// 移動の場合は先に undo 生成
					UD_OPE::make_undo(UD_OPE::UT_MOVE);
				}
				appl->vobjs.copy_selvobj((!(mov)), dl);
			} catch (EXCEPT_VOBJ& err) {
				// VOBJS のものに読み換える
				appl->gui->mwin->upd_narea();
				appl->gui->mwin->disp_selfrm(-2);
				throw EXCEPT_VOBJS(err.get_err());
			} catch (...) {
				appl->gui->mwin->upd_narea();
				appl->gui->mwin->disp_selfrm(-2);
				throw;
			}
			appl->gui->mwin->upd_narea();
			appl->gui->mwin->disp_selfrm(-2);
			if (!(mov)) {
				// 複写の場合は後で undo 生成
				UD_OPE::make_undo(UD_OPE::UT_ADD);
			}
		} else {
			// 自ウィンドウ外のどこか(一時トレー経由)
			TRAYOPE::push_tray(-1, mov, true, gap);
		}
	}

	return;
}
