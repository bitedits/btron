//
//	pd_vqp.cc (偽仮身一覧/ポインタクイックプレス処理-仮身をつかむ)
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
#include	"editobj.h"
#include	"vobjope.h"
#include	"vobjs.h"
#include	"guiope.h"
#include	"mainwin.h"
#include	"pd_vqp.h"
#include	"ud_ope.h"
#include	"trayope.h"
#include	"misc.h"


// ------------------------------------------------------ PD_VQP 内 public 関数
//
// constructor
//
PD_VQP::PD_VQP(W vid)
	: svid(vid), vobj(const_cast<VOBJ*>(appl->vobjs.srch_vobj(vid)))
{
	if (!(vobj->get_sel())) {
		// 非選択状態なら選択状態にする
		appl->vobjs.set_allsel(false, false);
		vobj->set_sel(true);
	} else if (appl->vobjs.get_selvid() != vid) {
		// 複数選択時は操作禁止
		throw EXCEPT_PDOPE(I_DLEDERR::PD_NOOPE);
	}

	// 付箋の場合は操作禁止
	if (!(vobj->get_type())) {
		appl->gui->mwin->disp_selfrm(-2);
		throw EXCEPT_PDOPE(I_DLEDERR::PD_NOOPE);
	}

	appl->gui->mwin->disp_selfrm(-1);

	// drag 描画環境の生成
	dgid = wsta_drg(wid, 0);
	if (dgid < ER_OK) {
		throw EXCEPT_PDOPE(dgid);
	}
	setpointer(PS_GRIP, NULL);	// 握り

	// 他の変数の設定
	sp = wevt.s.pos;
	gget_fra(dgid, &frect);
	orsz_vob(vid, &vr, V_CHECK);
	gap = (SIZE){sp.x - vr.c.left, sp.y - vr.c.top};
	srgn.sts = 0x0000;
}


//
// destructor
//
PD_VQP::~PD_VQP()
{
	appl->gui->mwin->disp_selfrm(1);
	if (dgid >= 0) {
		wend_drg();
	}
}
 

//
// 実行処理部
//
void	PD_VQP::main()
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
		// 実身複製
		W	cvid;		// 複製用 dummy
		W	nvid;		// 生成された実身への仮身

		cvid = odup_vob(svid);	// dummy を作って、それを複写先に移動
		omov_vob(cvid, wevt.s.wid, NULL, V_NODISP);
		nvid = onew_obj(cvid, NULL);
		odel_vob(cvid, 0);
		if (nvid < ER_OK) {
			// 取り消したか何らかの異常
			;		// 特になにもしない
		} else {
			// どこかに複製した実身の仮身を貼り付ける
			omov_vob(nvid, 0, &srgn.rgn.r, V_NODISP);
			if (in) {
				// 自ウィンドウの作業領域内
				appl->eobj->set_editflg(true);	// 更新
				appl->vobjs.ins_vobj(nvid);
				vobj->set_sel(false);
				appl->vobjs.set_sel(nvid, true);
				appl->vobjs.dsp_vobj(-1, &srgn.rgn.r);
				appl->gui->mwin->upd_narea();
				appl->gui->mwin->disp_selfrm(-2);
				UD_OPE::make_undo(UD_OPE::UT_ADD);
			} else {
				// 自ウィンドウ外のどこか(一時トレー経由)
				TRAYOPE::push_tray(nvid, false, true, gap);
				odel_vob(nvid, 0);
			}
		}
	}

	return;
}
