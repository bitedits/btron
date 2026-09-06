//
//	evtope.cc (偽仮身一覧/イベント操舵系主幹)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/libapp.h>
#include	<errcode.h>
#include	<keycode.h>

#include	<new>

#include	"dbox.h"
#include	"val.h"
#include	"cval.h"
#include	"macro.h"
#include	"err.h"
#include	"except.h"
#include	"debug.h"

#include	"appl.h"
#include	"fusen.h"
#include	"guiope.h"
#include	"evtope.h"
#include	"mainwin.h"
#include	"mainmenu.h"
#include	"saveope.h"


// ----------------------------------------- EVTOPE 内 static member 変数の実体
bool	EVTOPE::pflg = false;
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
	wfunc.pastefn = evt_paste;	// 貼り込み要求
	wfunc.respfn = NULL;		// 応答要求
	wfunc.scrlfn = evt_scroll;	// スクロール処理
	wfunc.devfn = NULL;		// デバイスイベント処理
	wfunc.vobjfn = evt_vobj;	// 仮身要求イベント
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
	bool	pdflg;			// PD 形状は主ウィンドウに依存する

	pdflg = false;
	if ((!(wevt.s.stat & (ES_CMD | ES_BUT2))) &&
	    (wevt.s.wid == winfo[CVAL::WIDX_MAINWIN].wid) &&
	    (wevt.s.cmd == W_WORK)) {
		if (cidl_par(wevt.s.wid, &wevt.s.pos) == 0) {
			pdflg = true;
		}
	}

	// ウィンドウに回送
	appl->gui->mwin->idle_fn(pdflg);

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
		if (sts == 2) {		// W_SWITCH 時は貼り込みの可能性あり
			if (pflg) {
				// 貼り込み処理の実行
				appl->gui->mwin->do_poptray(true, true);
				pflg = false;
			}
		}
	}

	// 選択枠の操作
	if (sts <= 0) {
		appl->gui->mwin->disp_selfrm(0);
	} else {
		appl->gui->mwin->disp_selfrm(1);
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

	rv = 1;				// 普段は受け入れ状態にする
	try {
		std::auto_ptr<SAVEOPE>	save;

		if (mode == 1) {
			// W_DELETE(保存強制終了)
			save = std::auto_ptr<SAVEOPE>(new SAVEOPE());
		} else if (mode == 2) {
			// W_FINISH(非保存強制終了)
			throw EXCEPT_SAVEOPE(I_DLEDERR::NO_ALLSAVE);
		} else {
			// その他(一般的な終了処理)
			save = std::auto_ptr<SAVEOPE>(new SAVEOPE(true));
		}

		// 保存の実行
		save->main();
	} catch (EXCEPT_SAVEOPE& err) {
		rv = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), rv, rv >> 16));
		if (rv == I_DLEDERR::NO_ALLSAVE) {
			// 非保存強制終了(付箋固有データも実身も更新しない)
			appl->fsn->undo_fusen();
			rv = 1;
		} else if (rv == I_DLEDERR::NO_UPDATE) {
			// 非保存終了(付箋固有データは更新する)
			rv = 1;
		} else if (rv == I_DLEDERR::SAVE_CANCEL) {
			// 終了拒否(取り消し)
			rv = 0;
		} else if ((rv == ER_NOMEM) || (rv == ER_NOSPC)) {
			// memory allocation error
			errpanel(DBOX::EPNL_MEMORY, ER_NOMEM);
			rv = 0;
		} else {
			// 何か
			errpanel(DBOX::EPNL_WRITE, rv);
			rv = 0;
		}
	} catch (std::bad_alloc) {
		DPRINT(("memory allocation error.\n"));
		errpanel(DBOX::EPNL_MEMORY, ER_NOMEM);
		rv = 0;
	} catch (...) {
		DPRINT(("other exception.\n"));
		rv = ER_SYS;
	}

	return rv;
}


//
// 貼り込み要求処理
//
W	EVTOPE::evt_paste(W ix, PNT pos)
{
	W	rv;

	rv = appl->gui->mwin->paste_fn(pos);
	pflg = (rv == W_ACK);

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


//
// 仮身要求イベント
//
void	EVTOPE::evt_vobj(W ix)
{
	try { 
		appl->vobjs.vobj_fn();
	} catch (EXCEPT_VOBJ& err) {
		ERR	er;

		er = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), er, er >> 16));
		if ((er == ER_NOMEM) || (er == ER_NOSPC)) {
			errpanel(DBOX::EPNL_MEMORY, er);
		}
	} catch (EXCEPT_VOBJS& err) {
		ERR	er;

		er = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), er, er >> 16));
		if ((er == ER_NOMEM) || (er == ER_NOSPC)) {
			errpanel(DBOX::EPNL_MEMORY, er);
		}
	} catch (std::bad_alloc) {
		DPRINT(("memory allocation error.\n"));
		errpanel(DBOX::EPNL_MEMORY, ER_NOMEM);
	} catch (...) {
		;
	}
 
	return;
}
