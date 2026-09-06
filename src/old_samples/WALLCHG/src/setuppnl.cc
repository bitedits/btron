//
//	setuppnl.cc (壁紙変更/設定パネル操舵系)
//
//	(C) Copyright 2001 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/libapp.h>

#include	"cval.h"
#include	"val.h"
#include	"dbox.h"
#include	"struct.h"
#include	"err.h"
#include	"except.h"
#include	"debug.h"

#include	"appl.h"
#include	"fusen.h"
#include	"setuppnl.h"


// 内部変数
LOCAL	SETUPPNL*	pnl = NULL;	// panel 処理関数からの参照用


// 内部関数プロトタイプ
LOCAL	W	setuppnl_fn(W pnid, W ino, W val);


// ----------------------------------------------------------- class 外内部関数
//
// 設定パネル操舵関数
//
LOCAL	W	setuppnl_fn(W pnid, W ino, W val)
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
		case SETUPPNL::STPIDX_MS_EXIT:	// [終了]
			pnl->get_setdat();
			rv = 0;
			break;
		case SETUPPNL::STPIDX_MS_SET:	// [常駐]
			pnl->get_setdat();
			rv = 1;
			break;
#if	0				// 見ない
		case SETUPPNL::STPIDX_SB_MIN:	// 分設定
			break;
		case SETUPPNL::STPIDX_SB_SEC:	// 秒設定
			break;
		case SETUPPNL::STPIDX_AS_PANEL:	// [起動時のパネル]
			break;
		case SETUPPNL::STPIDX_AS_CHANGE:// [起動時にも壁紙変更]
			break;
#endif	// 0
	}

	return rv;
}


// ---------------------------------------------------- SETUPPNL 内 public 関数
//
// constructor
//
SETUPPNL::SETUPPNL()
	: pnid(-1)
{
	// パネルを開ける
	pnid = opnstdpnl(DBOX::OPNL_SETUP, NULL);
	if (pnid < ER_OK) {
		throw EXCEPT_SETUP(pnid);
	}
	DPRINT(("panel ID : %d\n", pnid));

	// 各パーツの状態の設定
	W	val;
	const	UW	opt = appl->fsn->get_option();

	val = static_cast<W>(appl->fsn->get_min());
	cset_val(pidstdpnl(STPIDX_SB_MIN), 1, &val);
	val = static_cast<W>(appl->fsn->get_sec());
	cset_val(pidstdpnl(STPIDX_SB_SEC), 1, &val);
	val = (opt & CVAL::OPT_PANEL) ? 1 : 0;
	cset_val(pidstdpnl(STPIDX_AS_PANEL), 1, &val);
	val = (opt & CVAL::OPT_CHANGE) ? 1 : 0;
	cset_val(pidstdpnl(STPIDX_AS_CHANGE), 1, &val);

	pnl = this;
}


//
// destructor
//
SETUPPNL::~SETUPPNL()
{
	if (pnid >= 0) {
		clsstdpnl();
	}
}


//
// 各設定値の取得
//
void	SETUPPNL::get_setdat()
{
	W	val;
	UW	opt;

	// 分の取り出し
	cget_val(pidstdpnl(STPIDX_SB_MIN), 1, &val);
	appl->fsn->set_min(val);

	// 秒の取り出し
	cget_val(pidstdpnl(STPIDX_SB_SEC), 1, &val);
	appl->fsn->set_sec(val);

	// パネル・変更の取り出し
	opt = CVAL::OPT_NONE;
	cget_val(pidstdpnl(STPIDX_AS_PANEL), 1, &val);
	opt |= val * CVAL::OPT_PANEL;
	cget_val(pidstdpnl(STPIDX_AS_CHANGE), 1, &val);
	opt |= val * CVAL::OPT_CHANGE;
	appl->fsn->set_option(opt);

	return;
}


//
// パネルの実行
//	<  0 : error code
//	== 0 : 終了
//	== 1 : 設定
//
WERR	SETUPPNL::exec()
{
	WERR	rv;

	rv = exstdpnl(pnid, (FUNCP)(&setuppnl_fn));
	pnid = -1;

	return rv;
}
