//
//	guiope.cc (パーツ操作例/GUI 操舵系主幹部)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/libapp.h>
#include	<errcode.h>

#include	<new>
#include	<memory>

#include	"val.h"
#include	"dbox.h"
#include	"err.h"
#include	"except.h"
#include	"debug.h"

#include	"appl.h"
#include	"guiope.h"
#include	"evtope.h"
#include	"mainwin.h"
#include	"mainmenu.h"


// ------------------------------------------------------ GUIOPE 内 public 関数
//
// constructor
//
GUIOPE::GUIOPE()
{
}


//
// destructor
//
GUIOPE::~GUIOPE()
{
}


//
// 各種管理化 class の生成
//
void	GUIOPE::init()
{
	ERR	er;

	try {
		// 管理部の生成
		evtope = std::auto_ptr<EVTOPE>(new EVTOPE());
		mwin = std::auto_ptr<MAINWIN>(new MAINWIN());
		menu = std::auto_ptr<MAINMENU>(new MAINMENU());

		// 初期化
		mwin->open_win(appl->get_pwid(), appl->get_pr());
		mwin->par_create();
	} catch (EXCEPT_MAINWIN& err) {	// 主ウィンドウの初期化に失敗
		er = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), er, er >> 16));
		errpanel(DBOX::EPNL_WINDOW, er);
		throw EXCEPT_INITERR(er);
	} catch (EXCEPT_MAINMENU& err) {// メインメニューの初期化に失敗
		er = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), er, er >> 16));
		errpanel(DBOX::EPNL_MENU, er);
		throw EXCEPT_INITERR(er);
	} catch (std::bad_alloc) {	// メモリ不足
		DPRINT(("memory allocation error.\n"));
		errpanel(DBOX::EPNL_MEMORY, ER_NOMEM);
		throw EXCEPT_INITERR(ER_NOMEM);
	} catch (...) {			// その他
		throw;
	}

	return;
}


//
// 各管理化 class の資源の廃棄を通知
//
void	GUIOPE::dest()
{
	// 主ウィンドウの資源の開放を指示
	mwin->par_destroy();
	mwin->close_win();

	return;
}
