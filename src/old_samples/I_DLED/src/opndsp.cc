//
//	opndsp.cc (偽仮身一覧/開いた仮身の表示処理)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/dp.h>
#include	<btron/hmi.h>
#include	<btron/vobj.h>

#include	"val.h"
#include	"cval.h"
#include	"err.h"
#include	"except.h"

#include	"appl.h"
#include	"editobj.h"
#include	"vobjs.h"
#include	"opndsp.h"
#include	"fusen.h"
#include	"tadload.h"


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
	appl->vobjs.set_regid(-igid);	// 基本 ID の設定

	// 座標系をずらす
	const	PNT	p = appl->fsn->get_vpos();

	gmov_cor(igid, p.x, p.y);
	gget_vis(igid, &vr);

	// 仮身の登録
	TADLOAD	load((LINK*)appl->get_link());

	load.main();

	// 開いた仮身の表示背景色を取得
	VOBJSEG	vseg;

	oget_vob(appl->get_vid(), NULL, &vseg, sizeof(VOBJSEG), NULL);

	// 表示処理
	PAT	bgpat = {{
			0, 16, 16, vseg.bgcol, CVAL::RGB_WHITE, FILL100
		}};

	gfil_rec(igid, vr, &bgpat, 0, G_STORE);
	appl->vobjs.dsp_vobj(-1, &vr);

	return;
}
