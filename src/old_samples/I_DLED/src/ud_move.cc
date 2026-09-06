//
//	ud_move.cc (偽仮身一覧/取り消し動作-位置移動)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/vobj.h>
#include	<btron/libapp.h>
#include	<errcode.h>

#include	<stack>

#include	"val.h"

#include	"appl.h"
#include	"vobjope.h"
#include	"vobjs.h"
#include	"guiope.h"
#include	"mainwin.h"
#include	"ud_ope.h"
#include	"ud_move.h"


// ----------------------------------------------------- UD_MOVE 内 public 関数
//
// constructor
//
UD_MOVE::UD_MOVE(UW ptype)
{
	type = ptype;
}


//
// destructor
//
UD_MOVE::~UD_MOVE()
{
}


//
// 内容の生成
//
void	UD_MOVE::make()
{
	const	VOBJ*	vobj = appl->vobjs.get_vobj();

	if (vobj != NULL) {
		VOBJ*	ptr;

		ptr = const_cast<VOBJ*>(vobj);
		do {
			if ((!(ptr->get_del())) &&
			    (!(ptr->get_hold())) &&
			    (!(ptr->get_back()))) {
				UDATA	dat;

				dat.vid = ptr->get_vid();
				dat.sel = ptr->get_sel();
				orsz_vob(dat.vid, &dat.r, V_CHECK);
				udat.push(dat);
			}
			ptr = const_cast<VOBJ*>(ptr->get_next());
		} while (ptr != vobj);
	}

	return;
}


//
// 取り消しの実行
//
void	UD_MOVE::exec()
{
	// 戻すものがない時はなにもしない
	if (udat.empty()) {
		goto EXIT;
	}

	// 実行
	bool	first;
	RECT	dr;			// 再描画領域

	first = true;
	dr = (RECT){{0, 0, 0, 0}};

	// 選択状態の全解除
	appl->gui->mwin->disp_selfrm(-1);
	appl->vobjs.set_allsel(false, false);

	while (!(udat.empty())) {
		UDATA	dat = udat.top();
		VOBJ*	ptr =const_cast<VOBJ*>(appl->vobjs.srch_vobj(dat.vid));

		if (ptr != NULL) {
			// undo 対象の必要性の確認
			RECT	vr;

			orsz_vob(dat.vid, &vr, V_CHECK);
			if ((ptr->get_sel() != dat.sel) ||
			    (!(equalrect(vr, dat.r)))) {
				// 再描画領域の設定
				if (first) {
					dr = vr;
					first = false;
				} else {
					orrect(&dr, &dr, &vr);
				}

				// 移動
				RECT	sr;

				sr = dat.r;
				omov_vob(dat.vid, 0, &sr, V_NODISP);
				orrect(&dr, &dr, &sr);

				// 大きさの復元
				dat.r.c.right = sr.c.left + rectwidth(dat.r);
				dat.r.c.bottom = sr.c.top + rectheight(dat.r);
				orsz_vob(dat.vid, &dat.r, V_SIZE | V_NODISP);
				orrect(&dr, &dr, &dat.r);

				ptr->set_sel(dat.sel);
				ptr->upd_mydat(false, true);
			}
		}
		udat.pop();
	}

	// 選択枠の再設定/再描画
	appl->gui->mwin->redisp(&dr);
	appl->gui->mwin->disp_selfrm(-2);
	appl->gui->mwin->disp_selfrm(1);
	appl->gui->mwin->upd_narea();

EXIT:
	return;
}
