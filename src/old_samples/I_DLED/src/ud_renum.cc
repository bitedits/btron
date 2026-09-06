//
//	ud_renum.cc (偽仮身一覧/取り消し動作-順位変動)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/vobj.h>
#include	<btron/libapp.h>
#include	<errcode.h>

#include	"val.h"

#include	"appl.h"
#include	"vobjope.h"
#include	"vobjs.h"
#include	"guiope.h"
#include	"mainwin.h"
#include	"ud_ope.h"
#include	"ud_renum.h"


// ---------------------------------------------------- UD_RENUM 内 public 関数
//
// constructor
//
UD_RENUM::UD_RENUM(UW ptype)
	: udat(appl->vobjs.get_cnt(false))
{
	type = ptype;
}


//
// destructor
//
UD_RENUM::~UD_RENUM()
{
}


//
// 内容の生成
//
void	UD_RENUM::make()
{
	W	idx;
	VOBJ*	ptr;

	ptr = const_cast<VOBJ*>(appl->vobjs.get_vobj());
	for (idx = 0; idx < udat.size(); ++idx) {
		udat[idx].vid = ptr->get_vid();
		udat[idx].vptr = ptr;
		ptr = const_cast<VOBJ*>(ptr->get_next());
	}

	return;
}


//
// 取り消しの実行
//
void	UD_RENUM::exec()
{
	// 内容がなければ何もしない
	if (udat.size() <= 0) {
		goto EXIT;
	}

	// 実行
	W	idx;
	bool	first;
	RECT	dr;

	first = true;
	dr = (RECT){{0, 0, 0, 0}};
	appl->gui->mwin->disp_selfrm(0);

	appl->vobjs.set_vobj(udat[0].vptr);
	for (idx = 0; idx < udat.size(); ++idx) {
		if (idx > 0) {
			udat[idx].vptr->set_prev(udat[idx - 1].vptr);
		}
		if (idx < udat.size()) {
			udat[idx].vptr->set_next(udat[idx + 1].vptr);
		}

		// 再描画範囲の取得
		RECT	vr;

		if (orsz_vob(udat[idx].vid, &vr, V_CHECK) >= ER_OK) {
			if (first) {
				dr = vr;
				first = false;
			} else {
				orrect(&dr, &dr, &vr);
			}
		}
	}
	udat[idx - 1].vptr->set_next(appl->vobjs.get_vobj());
	udat[0].vptr->set_prev(udat[idx - 1].vptr);

	// 再描画
	appl->gui->mwin->redisp(&dr);
	appl->gui->mwin->disp_selfrm(-2);
	appl->gui->mwin->disp_selfrm(1);

EXIT:
	return;
}
