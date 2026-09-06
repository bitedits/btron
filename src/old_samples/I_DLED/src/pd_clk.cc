//
//	pd_dclk.cc (偽仮身一覧/ポインタクリック処理)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/libapp.h>

#include	"val.h"
#include	"err.h"
#include	"except.h"

#include	"appl.h"
#include	"vobjope.h"
#include	"vobjs.h"
#include	"guiope.h"
#include	"mainwin.h"
#include	"pd_clk.h"


// ------------------------------------------------------ PD_CLK 内 public 関数
//
// constructor
//
PD_CLK::PD_CLK()
	: vid(-1)
{
	// PD 位置に応じた仮身を探す
	vid = appl->vobjs.fnd_posvobj(wevt.s.pos);
	if (vid < 0) {
		// 選択枠の廃棄
		if (!(wevt.s.stat & (ES_RSHFT | ES_LSHFT))) {
			appl->vobjs.set_allsel(false, false);
			appl->gui->mwin->disp_selfrm(-1);
		}
		throw EXCEPT_PDOPE(I_DLEDERR::PD_NOOPE);
	}
 
	// 仮身情報を取得する
	vobj = const_cast<VOBJ*>(appl->vobjs.srch_vobj(vid));
	if (vobj == NULL) {
		// 選択枠の廃棄
		if (!(wevt.s.stat & (ES_RSHFT | ES_LSHFT))) {
			appl->vobjs.set_allsel(false, false);
			appl->gui->mwin->disp_selfrm(-1);
		}
		throw EXCEPT_PDOPE(I_DLEDERR::PD_NOOPE);
	}
}


//
// destructor
//
PD_CLK::~PD_CLK()
{
}


//
// 実行処理部
//
void	PD_CLK::main()
{
	// 非選択状態の仮身を選択した
	if (!(vobj->get_sel())) {
		// 他の選択状態を解除
		if (!(wevt.s.stat & (ES_RSHFT | ES_LSHFT))) {
			appl->vobjs.set_allsel(false, false);
		}

		// 選択状態を設定
		vobj->set_sel(true);

		// 選択枠の生成/表示
		appl->gui->mwin->disp_selfrm(-2);
		appl->gui->mwin->disp_selfrm(1);
	} else if (wevt.s.stat & (ES_RSHFT | ES_LSHFT)) {
		// 修正選択指で選択状態のものなら、選択解除
		vobj->set_sel(false);

		// 選択枠の生成/表示
		appl->gui->mwin->disp_selfrm(-2);
		appl->gui->mwin->disp_selfrm(1);
	}

	return;
}

