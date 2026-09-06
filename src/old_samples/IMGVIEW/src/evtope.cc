//
//	evtope.cc (画像閲覧/イベント操舵系主幹)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/libapp.h>
#include	<errcode.h>
#include	<keycode.h>

#include	<new>

#include	"appl.h"
#include	"dbox.h"
#include	"val.h"
#include	"cval.h"
#include	"macro.h"
#include	"err.h"
#include	"except.h"
#include	"debug.h"

#include	"guiope.h"
#include	"evtope.h"
#include	"mainwin.h"
#include	"mainmenu.h"


// ----------------------------------------- EVTOPE 内 static member 変数の実体
//bool	EVTOPE::pflg = false;
WFUNCREC	EVTOPE::wfunc;
WINFOREC	EVTOPE::winfo[CVAL::WIN_NUM + 1];


// ------------------------------------------------------ EVTOPE 内 public 関数
//
// constructor
//
EVTOPE::EVTOPE()
{
	// ウィンドウ情報レコードの初期化
	W	l;

	for (l = 0; l <= CVAL::WIN_NUM; ++l) {
		winfo[l] = DEF_WINFO;
	}
	winfo[CVAL::WIN_NUM].wid = 0;

	// ウィンドウイベント関数テーブルの設定
	wfunc.bgfn = NULL;		// バックグラウンド処理
	wfunc.idlefn = evt_idle;	// アイドル処理
	wfunc.msgfn = NULL;		// 一般メッセージ処理
	wfunc.menufn = evt_menu;	// メニューイベント
	wfunc.dspfn = evt_disp;		// 表示要求
	wfunc.stsfn = evt_chgsts;	// 状態変化処理
	wfunc.keyfn = evt_key;		// キー入力処理
	wfunc.presfn = evt_press;	// 主ボタンプレス処理
	wfunc.finfn = evt_finish;	// 終了要求
	wfunc.pastefn = NULL;		// 貼り込み要求
	wfunc.respfn = NULL;		// 応答要求
	wfunc.scrlfn = evt_scroll;	// スクロール処理
	wfunc.devfn = NULL;		// デバイスイベント処理
	wfunc.vobjfn = NULL;		// 仮身要求イベント
}


//
// destructor
//
EVTOPE::~EVTOPE()
{
}


// --------------------------------------- EVTOPE 内 public 関数(static member)
//
// event loop の入り口
//
ERR	EVTOPE::eventloop()
{
	ERR	er;

	er = ER_OK;
	try {
		WERR	rv;

		rv = evt_loop(&wfunc, winfo);
		er = (rv >= ER_OK) ? ER_OK : rv;
	} catch (...) {			// 本当はあってはならない事態
		DPRINT(("!! CAUTION !! : evt_loop() was gone to down by exception.\n"));
		throw;
	}
	switch (er) {
		case ER_SYS:
			errpanel(DBOX::EPNL_SYSTEM, 0);
			break;
	}

	return er;
}


//
// 定常処理
//
void	EVTOPE::evt_idle()
{
	// 普通の定常処理
	if ((!(wevt.s.stat & (ES_CMD | ES_BUT2))) &&
	    (wevt.s.wid == winfo[CVAL::WIDX_MAINWIN].wid) &&
	    (wevt.s.cmd == W_WORK)) {
		if (cidl_par(wevt.s.wid, &wevt.s.pos) == 0) {
			setpointer(PS_SELECT, NULL);
		}
	}

	// ウィンドウに回送
	appl->gui->mwin->idle_fn();

	return;
}


//
// メニューイベント
//
W	EVTOPE::evt_menu()
{
	return appl->gui->menu->exec();
}


//
// 表示処理
//
void	EVTOPE::evt_disp(W ix, W mode, RECT* newr)
{
	appl->gui->mwin->disp_fn(mode, newr);

	return;
}


//
// 状態変化処理
//
void	EVTOPE::evt_chgsts(W ix, W sts)
{
	setpointer(0x8000, NULL);
	if (sts != 0x100) {
		// パーツの復帰(evt_loop() で非表示に落とされているため)
		cdsp_pwd(appl->gui->mwin->get_wid(), NULL, P_RDISP);
	}

	return;
}


//
// キー入力処理
//
W	EVTOPE::evt_key()
{
	W	rv;

	if ((wevt.s.stat & ES_CMD) && (wevt.e.data.key.code > KC_SPACE)) {
		// [命令] 同時押しならキーマクロ
		rv = appl->gui->menu->exec();
	} else {
		// 通常の key 入力処理
		rv = appl->gui->mwin->key_fn();
	}

	return rv;
}


//
// 主ボタンプレス処理
//
W	EVTOPE::evt_press(W ix)
{
	W	rv;

	rv = appl->gui->mwin->press_fn();

	return rv;
}


//
// 終了要求
//
W	EVTOPE::evt_finish(W ix, W mode)
{
	W	rv;

	rv = 1;				// 普段は受け入れとしておく
	switch (mode) {
		case 1:			// 保存強制終了(W_DELETE)
		case 2:			// 非保存強制終了(W_FINISH)
			break;
		default:		// その他(一般的な終了処理)
			break;
	}

	return rv;
}


//
// スクロール処理
//
void	EVTOPE::evt_scroll(W ix, W type, W diff)
{
	appl->gui->mwin->scroll_fn(type, diff);

	return;
}
