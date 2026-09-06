//
//	tidypnl.cc (偽仮身一覧/整頓設定パネル管理系)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/libapp.h>

#include	"val.h"
#include	"dbox.h"
#include	"struct.h"
#include	"err.h"
#include	"except.h"

#include	"appl.h"
#include	"fusen.h"
#include	"tidypnl.h"


// 内部関数プロトタイプ
LOCAL	W	tidypnl_fn(W pnid, W ino, W val);


// ----------------------------------------------------------- class 外内部関数
//
// 整頓設定パネル操舵関数
//
LOCAL	W	tidypnl_fn(W pnid, W ino, W val)
{
	W	rv;

	rv = -1;
	switch (ino) {
#if	0				// 見ない
		case -2:		// 再描画処理
			break;
		case -1:		// 生成前処理
			break;
		case 0:			// 初期化処理
			break;
#endif	// 0
		case TIDYPNL::TSPIDX_MS_CANCEL:	// [取り消し]
			rv = 0;
			break;
		case TIDYPNL::TSPIDX_MS_DO:	// [実行]
			rv = 0x8000;
			break;
#if	0				// 特に動作は関知しない
		case TIDYPNL::TSPIDX_WS_H:	// 横設定用 WS_PARTS
		case TIDYPNL::TSPIDX_WS_V:	// 縦設定用 WS_PARTS
		case TIDYPNL::TSPIDX_WS_L:	// 長さ設定用 WS_PARTS
			break;
#endif	// 0
	}

	return rv;
}


// ----------------------------------------------------- TIDYPNL 内 public 関数
//
// constructor
//
TIDYPNL::TIDYPNL(bool ponly)
	: pnid(-1), only(ponly),
	  tdat(appl->fsn->get_tidy())
{
	// panel を開ける
	pnid = opnstdpnl(DBOX::OPNL_TIDY, NULL);
	if (pnid < ER_OK) {
		throw EXCEPT_TIDYPNL(pnid);
	}

	// parts の状態を設定する
	if (only) {
		// 単一選択
		cchg_par(pidstdpnl(TSPIDX_WS_H), P_DISABLE);
		cchg_par(pidstdpnl(TSPIDX_WS_V), P_DISABLE);
		cset_val(pidstdpnl(TSPIDX_WS_L), 1, (W*)&tdat.ltype);
	} else {
		// 複数選択
		cset_val(pidstdpnl(TSPIDX_WS_H), 1, (W*)&tdat.htype);
		cset_val(pidstdpnl(TSPIDX_WS_V), 1, (W*)&tdat.vtype);
		cset_val(pidstdpnl(TSPIDX_WS_L), 1, (W*)&tdat.ltype);
	}
}


//
// destructor
//
TIDYPNL::~TIDYPNL()
{
	if (pnid >= 0) {
		clsstdpnl();
	}
}


//
// パネルの実行
//
W	TIDYPNL::exec()
{
	W	rv;

	rv = exstdpnl(pnid, (FUNCP)(&tidypnl_fn));
	if (rv == pnid) {
		// parts の設定内容の取り出し(付箋固有データに反映)
		if (only) {
			// 単一選択
			cget_val(pidstdpnl(TSPIDX_WS_L), 1, (W*)&tdat.ltype);
		} else {
			// 複数選択
			cget_val(pidstdpnl(TSPIDX_WS_H), 1, (W*)&tdat.htype);
			cget_val(pidstdpnl(TSPIDX_WS_V), 1, (W*)&tdat.vtype);
			cget_val(pidstdpnl(TSPIDX_WS_L), 1, (W*)&tdat.ltype);
		}
		appl->fsn->set_tidy(tdat);
		rv = 1;
	} else {
		// [取り消し] か他の error
		pnid = -1;		// 既に panel は閉じている
		rv = 0;
	}

	return rv;
}
