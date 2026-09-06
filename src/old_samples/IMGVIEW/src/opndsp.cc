//
//	opndsp.cc (画像閲覧/開いた仮身の表示処理)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/dp.h>
#include	<btron/hmi.h>
#include	<btron/vobj.h>

#include	"val.h"
#include	"err.h"
#include	"except.h"

#include	"appl.h"
#include	"opndsp.h"
#include	"imgope.h"
#include	"fusen.h"
#include	"misc.h"


// ------------------------------------------------------ OPNDSP 内 public 関数
//
// constructor
//
OPNDSP::OPNDSP(W gid)
	: iwid(-1), igid(gid)
{
	// 内部 window を開ける
	iwid = wopn_iwd(gid);
	if (iwid < ER_OK) {
		// 内部 window が開けられない
		throw EXCEPT_OVDISP(iwid);
	}

	// 内部 window に対する諸情報の取得
	gget_fra(igid, &vr);
}


//
// destructor
//
OPNDSP::~OPNDSP()
{
	wcls_wnd(iwid, CLR);
}


//
// 表示の主幹処理
//
// ここから呼び出した関数で発生した exception は上位で受け取ること
//
void	OPNDSP::main()
{
	UW	zfact = static_cast<UW>(appl->fsn->get_zfact());

	MISC::zfact_to_tbl(zfact);
	appl->img->disp_grph(igid, vr, vr, appl->fsn->get_vpos(), zfact);

	return;
}
