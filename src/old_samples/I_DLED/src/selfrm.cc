//
//	selfrm.cc (偽仮身一覧/選択枠管理系)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/hmi.h>
#include	<btron/vobj.h>
#include	<btron/libapp.h>

#include	<vector>

#include	"val.h"
#include	"err.h"
#include	"except.h"

#include	"appl.h"
#include	"vobjope.h"
#include	"vobjs.h"
#include	"selfrm.h"


// ------------------------------------------------------ SELFRM 内 public 関数
//
// constructor
//
SELFRM::SELFRM(W pgid)
	: gid(pgid), slist(appl->vobjs.get_selcnt())
{
	r = (RECT){{0, 0, 0, 0}};

	// 選択中の仮身がない
	if (slist.size() == 0) {
		throw EXCEPT_SELFRM(I_DLEDERR::NO_SELFRM);
	}

	// 選択枠群の生成
	W	idx;
	bool	first;
	VOBJ*	ptr = const_cast<VOBJ*>(appl->vobjs.get_vobj());

	idx = 0;
	first = true;
	do {
		if (ptr->get_sel()) {
			slist[idx].next = (idx >= slist.size() - 1) ? NULL : &slist[idx + 1];
			slist[idx].rgn.sts =(ptr->get_hold()) ? 0x0100 : 0x000;
			orsz_vob(ptr->get_vid(),&slist[idx].rgn.rgn.r,V_CHECK);
			if (first) {
				r = slist[idx].rgn.rgn.r;
				first = false;
			} else {
				orrect(&r, &r, &slist[idx].rgn.rgn.r);
			}
			++idx;
		}
		ptr = const_cast<VOBJ*>(ptr->get_next());
	} while (ptr != appl->vobjs.get_vobj());
}


//
// destructor
//
SELFRM::~SELFRM()
{
	off();
}
