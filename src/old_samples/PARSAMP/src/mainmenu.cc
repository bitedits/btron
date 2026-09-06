//
//	mainmenu.cc (パーツ操作例/メインメニュー管理系)
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
#include	"guiope.h"
#include	"evtope.h"
#include	"mainwin.h"
#include	"mainmenu.h"
#include	"trayope.h"


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
	mfun[MIDX_REDISP] = reinterpret_cast<FUNCP>(&mn_redisp);
	mfun[MIDX_TOTRAY] = reinterpret_cast<FUNCP>(&mn_totray);
	mfun[MIDX_FROMTRAY] = reinterpret_cast<FUNCP>(&mn_fromtray);
	mfun[MIDX_DELETE] = reinterpret_cast<FUNCP>(&mn_delete);
	mfun[MIDX_INIT] = reinterpret_cast<FUNCP>(&mn_init);

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
// [表示]-[再表示]
//
W	MAINMENU::mn_redisp(W par)
{
	appl->gui->mwin->redisp();

	return 0;			// 常時 0
}


//
// [編集]-[トレーへ複写]/[トレーへ移動]
//
W	MAINMENU::mn_totray(W par)
{
	appl->gui->mwin->do_pushtray(false, (par == 2));

	return 0;			// 常時 0
}


//
// [編集]-[トレーから複写]/[トレーから移動]
//
W	MAINMENU::mn_fromtray(W par)
{
	appl->gui->mwin->do_poptray(false, (par == 2));

	return 0;			// 常時 0
}


//
// [編集]-[削除]
//
W	MAINMENU::mn_delete(W par)
{
	appl->gui->mwin->del_string();

	return 0;			// 常時 0
}


//
// [編集]-[初期化]
//
W	MAINMENU::mn_init(W par)
{
	appl->gui->mwin->init_data();

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
	// インジケータを持つものは特にない

	return;
}


//
// 有効・不能の設定
//
void	MAINMENU::set_enable()
{
	W	mask;

	mask = 0;

	// [トレーへ複写]/[トレーへ移動]/[削除] の設定
	mask |= (appl->gui->mwin->chk_totray()) ? 0 : (MMASK_TOTRAY | MMASK_DELETE);

	// [トレーから複写]/[トレーから移動] の設定
	mask |= (tget_sts(NULL, NULL) <= 0) ? MMASK_FROMTRAY : 0;

	inactmenu(mask, 0);

	return;
}
