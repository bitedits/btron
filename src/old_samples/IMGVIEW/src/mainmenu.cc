//
//	mainmenu.cc (画像閲覧/メインメニュー管理系)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/hmi.h>
#include	<btron/vobj.h>
#include	<btron/libapp.h>

#include	<memory>

#include	"val.h"
#include	"cval.h"
#include	"dbox.h"
#include	"err.h"
#include	"except.h"

#include	"appl.h"
#include	"fusen.h"
#include	"imgope.h"
#include	"guiope.h"
#include	"evtope.h"
#include	"mainwin.h"
#include	"mainmenu.h"
#include	"trayope.h"
#include	"misc.h"


// --------------------------------------- MAINMENU 内 static member 変数の実体
FUNCP	MAINMENU::mfun[MENU_NUM];


// ---------------------------------- MAINMENU 内 public 関数(非 static member)
//
// constructor
//
MAINMENU::MAINMENU()
	: mid(-1)
{
	// メニュー関数テーブルの初期化
	mfun[MIDX_VOBJ] = NULL;
	mfun[MIDX_TOOL] = reinterpret_cast<FUNCP>(&mn_tool);
	mfun[MIDX_EXEC] = NULL;
	mfun[MIDX_EXIT] = reinterpret_cast<FUNCP>(&mn_exit);
	mfun[MIDX_FWINDOW] = reinterpret_cast<FUNCP>(&mn_fwindow);
	mfun[MIDX_REDISP] = reinterpret_cast<FUNCP>(&mn_redisp);
	mfun[MIDX_TOTRAY] = reinterpret_cast<FUNCP>(&mn_totray);
	mfun[MIDX_ZFACT] = reinterpret_cast<FUNCP>(&mn_zfact);

	// メニューを登録する
	mid = openmenu(DBOX::MENU_MAIN);
	if (mid < ER_OK) {
		throw EXCEPT_MAINMENU(mid);
	}
}


//
// destructor
//
MAINMENU::~MAINMENU()
{
	closemenu();
}


//
// メニューの実行処理
//
W	MAINMENU::exec()
{
	set_indi();			// インジケータの設定
	set_enable();			// 有効・不能の設定

	return selmenu(-1, mfun);
}


// ------------------------------------- MAINMENU 内 public 関数(static member)
//
// [終了]
//
W	MAINMENU::mn_exit(W par)
{
	return EVTOPE::evt_finish(0, 0);
}


//
// [表示]-[全画面表示]
//
// 表示要求処理側で付箋固有データの更新をするので、ここでは変更しない
//
W	MAINMENU::mn_fwindow(W par)
{
	W	mode;
	RECT	r;

	mode = wchg_wnd(appl->gui->mwin->get_wid(), &r, W_MOVE);
	if (mode >= 0) {
		EVTOPE::evt_disp(CVAL::WIDX_MAINWIN, mode | 0x04, &r);
	}

	return 0;			// 常時 0
}


//
// [表示]-[再表示]
//
W	MAINMENU::mn_redisp(W par)
{
	appl->gui->mwin->redisp();

	return 0;			// 常時 0
}


//
// [編集]-[トレーへ複写]
//
W	MAINMENU::mn_totray(W par)
{
	if (appl->img != NULL) {
		try {
			TRAYOPE::push_tray(appl->img->get_dbmp(), appl->img->get_cspec());
		} catch (std::bad_alloc) {
			errpanel(DBOX::EPNL_PUSHTRAY, ER_NOMEM);
		} catch (...) {
			errpanel(DBOX::EPNL_PUSHTRAY, ER_SYS);
		}
	}

	return 0;			// 常時 0
}


//
// [倍率]-[標準]/[縮小]/[拡大]
//
W	MAINMENU::mn_zfact(W par)
{
	W	idx;
	UW	zfact = static_cast<UW>(appl->fsn->get_zfact());

	idx = MISC::zfact_to_tbl(zfact);

	// 値を変動させる
	switch (par) {
		case 1:			// [標準]
			zfact = CVAL::Z_MEDIUM;
			break;
		case 2:			// [拡大]
			if (idx < CVAL::Z_TBL_MAX) {
				zfact = CVAL::Z_TBL[idx + 1];
			}
			break;
		case 3:			// [縮小]
			if (idx > 0) {
				zfact = CVAL::Z_TBL[idx - 1];
			}
			break;
	}

	// 倍率の変更を反映させる
	if (zfact != appl->fsn->get_zfact()) {
		appl->fsn->set_zfact(zfact);
		appl->gui->mwin->chg_zfact();
	}

	return 0;			// 常時 0
}


//
// 小物系
//
W	MAINMENU::mn_tool(W par)
{
	appl_exec(par, 0);

	return 0;			// 常時 0
}


//
// application の実行(非 menu 項目)
//
void	MAINMENU::appl_exec(W par, W vid)
{
	setpointer(PS_BUSY, NULL);
	if (oexe_apg(vid, par) > 0) {
		setpointer(0x8001, NULL);
	}

	return;
}


// --------------------------------------------------- MAINMENU 内 private 関数
//
// インジケータの設定
//
void	MAINMENU::set_indi()
{
	const	WINATTR	wattr = appl->fsn->get_wattr();

	indmenu(MINUM_FWINDOW, (W)(wattr.fwindow), 0);

	return;
}


//
// 有効・不能の設定
//
void	MAINMENU::set_enable()
{
	if (appl->img == NULL) {
		// 画像が表示されていない(読み込めていない)
		inactmenu(MMASK_TOTRAY | MMASK_ZFACT, 0);
	} else {
		// 画像が表示されている
		const	UW	zfact = appl->fsn->get_zfact();

		// 内部番号が同じなので、実番号で切り替える
		mchg_atr(mid, 0x0301, (zfact != CVAL::Z_MEDIUM) ? M_ACT : M_INACT);
		mchg_atr(mid, 0x0302, (zfact < CVAL::Z_MAX) ? M_ACT : M_INACT);
		mchg_atr(mid, 0x0303, (zfact > CVAL::Z_MIN) ? M_ACT : M_INACT);
	}

	return;
}
