//
//	ud_del.cc (偽仮身一覧/取り消し動作-仮身削除)
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
#include	"ud_del.h"


// ------------------------------------------------------ UD_DEL 内 public 関数
//
// constructor
//
UD_DEL::UD_DEL(UW ptype)
{
	type = ptype;
}


//
// destructor
//
UD_DEL::~UD_DEL()
{
}


//
// 内容の生成
//
void	UD_DEL::make()
{
	const	VOBJ*	vobj = appl->vobjs.get_vobj();

	if (vobj != NULL) {
		VOBJ*	ptr;

		ptr = const_cast<VOBJ*>(vobj);
		do {
			if ((ptr->get_sel()) && (!(ptr->get_del()))) {
				UDATA	dat;

				dat.vid = ptr->get_vid();
				if ((ptr->get_hold()) || (ptr->get_back())) {
					dat.chg = false;
				} else {
					dat.chg = true;
				}
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
void	UD_DEL::exec()
{
	// 戻すものがない時はなにもしない
	if (udat.empty()) {
		goto EXIT;
	}

	// 選択状態の全解除
	appl->gui->mwin->disp_selfrm(-1);
	appl->vobjs.set_allsel(false, false);

	// 実行
	W	idx;
	bool	first;
	RECT	dr;
 
	first = true;
	dr = (RECT){{0, 0, 0, 0}};

	for (idx = 0; idx < udat.size(); ++idx) {
		UDATA	dat = udat[idx];
		VOBJ*	ptr =const_cast<VOBJ*>(appl->vobjs.srch_vobj(dat.vid));

		if (ptr != NULL) {
			if (dat.chg) {
				// 状態の設定
				ptr->set_del(false);

				// 再描画領域の取得
				RECT	vr;
 
				orsz_vob(dat.vid, &vr, V_CHECK);
				if (first) {
					dr = vr;
					first = false;
				} else {    
					orrect(&dr, &dr, &vr);
				}
			}
			ptr->set_sel(true);
		}
	}

	// 選択枠の再設定/再描画
	appl->gui->mwin->redisp(&dr);
	appl->gui->mwin->disp_selfrm(-2);
	appl->gui->mwin->disp_selfrm(1);
	appl->gui->mwin->upd_narea();

EXIT:
	return;
}


//
// 取り消しの取り消し(redo)の生成
//
void	UD_DEL::redo(const VP dat, UW cnt)
{
	W	idx;
	UDATA*	rdat;

	rdat = reinterpret_cast<UDATA*>(dat);
	for (idx = 0; idx < cnt; ++idx) {
		udat.push_back(rdat[idx]);
	}

	return;
}
