//
//	mainmenu.cc (簡易文字列編集/メインメニュー管理系)
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
#include	"debug.h"

#include	"appl.h"
#include	"editobj.h"
#include	"fusen.h"
#include	"guiope.h"
#include	"evtope.h"
#include	"mainwin.h"
#include	"mainmenu.h"
#include	"saveope.h"


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
	mfun[MIDX_NEWSAVE] = reinterpret_cast<FUNCP>(&mn_newsave);
	mfun[MIDX_SAVE] = reinterpret_cast<FUNCP>(&mn_save);
	mfun[MIDX_FWINDOW] = reinterpret_cast<FUNCP>(&mn_fwindow);
	mfun[MIDX_REDISP] = reinterpret_cast<FUNCP>(&mn_redisp);
	mfun[MIDX_CHGBACK] = reinterpret_cast<FUNCP>(&mn_chgback);
	mfun[MIDX_GRAY] = reinterpret_cast<FUNCP>(&mn_gray);

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
// [保存]-[新しい実身へ]
//
W	MAINMENU::mn_newsave(W par)
{
	W	rv;
	W	vid;
	LINK	lnk;

	rv = 0;
	if (SAVEOPE::make_newfile(vid, lnk) > 0) {
		// 保存処理
		try {
			SAVEOPE	save(vid, &lnk);

			save.main();
		} catch (EXCEPT_SAVEOPE& err) {
			ERR	er;

			er = err.get_err();
			DPRINT(("%s : %d(%d)\n", err.what(), er, er >> 16));
			if (er == T_TXEDITERR::SAVE_CANCEL) {
				;
			} else if ((er == ER_NOMEM) || (er == ER_NOSPC)) {
				errpanel(DBOX::EPNL_MEMORY, er);
			} else {
				errpanel(DBOX::EPNL_WRITE, er);
			}
		} catch (std::bad_alloc) {
			DPRINT(("memory allocation error.\n"));
			errpanel(DBOX::EPNL_MEMORY, ER_NOMEM);
		} catch (...) {
			DPRINT(("other exception.\n"));
			rv = ER_SYS;
		}
	}

	return rv;
}


//
// [保存]-[元の実身へ]
//
W	MAINMENU::mn_save(W par)
{
	W	rv;

	rv = 0;
	try {
		// 保存の実行
		SAVEOPE	save(false);

		save.main();

		// 更新状態の解除
		appl->eobj->set_saveflg(false);
		appl->eobj->set_editflg(false);
		appl->eobj->set_skipflg(false);
		appl->eobj->upd_mydat();
	} catch (EXCEPT_SAVEOPE& err) {
		ERR	er;

		er = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), er, er >> 16));
		if (er == T_TXEDITERR::SAVE_CANCEL) {
		} else if ((er == ER_NOMEM) || (er == ER_NOSPC)) {
			errpanel(DBOX::EPNL_MEMORY, er);
		} else {
			errpanel(DBOX::EPNL_WRITE, er);
		}
	} catch (std::bad_alloc) {
		DPRINT(("memory allocation error.\n"));
		errpanel(DBOX::EPNL_MEMORY, ER_NOMEM);
	} catch (...) {
		DPRINT(("other exception.\n"));
		rv = ER_SYS;
	}

	return rv;
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
	appl->gui->mwin->disp_caret(0);
	appl->gui->mwin->redisp(NULL);
	appl->gui->mwin->disp_caret(1);

	return 0;			// 常時 0
}


//
// [表示]-[背景色変更]
//
W	MAINMENU::mn_chgback(W par)
{
	WINCOL	wcol = static_cast<WINCOL>(appl->fsn->get_wcol());

	if (chgbgcol(&wcol.mask, &wcol.col, appl->get_bgcol()) == 1) {
		appl->gui->mwin->disp_caret(0);
		appl->fsn->set_wcol(wcol);
		appl->gui->mwin->set_wbgpat(true);
		appl->gui->mwin->disp_caret(1);
	}

	return 0;			// 常時 0
}


//
// [表示]-[文字階調表示]
//
W	MAINMENU::mn_gray(W par)
{
	WINATTR	wattr = appl->fsn->get_wattr();

	appl->gui->mwin->disp_caret(0);
	wattr.gray = !(wattr.gray);
	appl->fsn->set_wattr(wattr);
	appl->gui->mwin->set_char();
	appl->gui->mwin->redisp(NULL);
	appl->gui->mwin->disp_caret(1);

	return 0;			// 常時 0
}


//
// [小物] 系
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
	indmenu(MINUM_GRAY, (W)(wattr.gray), 0);

	return;
}


//
// 有効・不能の設定
//
void	MAINMENU::set_enable()
{
	return;
}
