//
//	ud_uback.cc (偽仮身一覧/取り消し動作-背景解除)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/vobj.h>
#include	<errcode.h>

#include	<vector>

#include	"val.h"

#include	"appl.h"
#include	"vobjope.h"
#include	"vobjs.h"
#include	"guiope.h"
#include	"mainwin.h"
#include	"ud_ope.h"
#include	"ud_uback.h"


// --------------------------------------------------- UD_UNBACK 内 public 関数
//
// constructor
//
UD_UNBACK::UD_UNBACK(UW ptype)
{
	type = ptype;
}


//
// destructor
//
UD_UNBACK::~UD_UNBACK()
{
}


//
// 内容の生成
//
void	UD_UNBACK::make()
{
	const	VOBJ*	vobj = appl->vobjs.get_vobj();

	if (vobj != NULL) {
		VOBJ*	ptr;

		ptr = const_cast<VOBJ*>(vobj);
		do {
			if ((ptr->get_back()) && (!(ptr->get_del()))) {
				UDATA	dat;

				dat.vid = ptr->get_vid();
				dat.chg = true;
				udat.push_back(dat);
			}
			ptr = const_cast<VOBJ*>(ptr->get_next());
		} while (ptr != vobj);
	}

	return;
}


//
// 取り消しの実行
//
void	UD_UNBACK::exec()
{
	// 戻すものがない時はなにもしない
	if (udat.empty()) {
		goto EXIT;
	}

	// 選択状態の解除
	appl->gui->mwin->disp_selfrm(-1);
	appl->vobjs.set_allsel(false, false);

	// 実行
	W	idx;

	for (idx = 0; idx < udat.size(); ++idx) {
		UDATA	dat = udat[idx];
		VOBJ*	ptr =const_cast<VOBJ*>(appl->vobjs.srch_vobj(dat.vid));

		if (ptr != NULL) {
			if (dat.chg) {
				ptr->set_back(true);
			}
		}
	}

	// 選択枠の再設定
	appl->gui->mwin->disp_selfrm(-2);
	appl->gui->mwin->disp_selfrm(1);

EXIT:
	return;
}


//
// 取り消しの取り消し(redo)の生成
//
void	UD_UNBACK::redo(const VP dat, UW cnt)
{
	W	idx;
	UDATA*	rdat;

	rdat = reinterpret_cast<UDATA*>(dat);
	for (idx = 0; idx < cnt; ++idx) {
		udat.push_back(rdat[idx]);
	}

	return;
}
