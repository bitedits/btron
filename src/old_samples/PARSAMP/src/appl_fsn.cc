//
//	appl_fsn.cc (パーツ操作例/application 基幹部-付箋のオープン起動)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/vobj.h>
#include	<btron/libapp.h>
#include	<errcode.h>

#include	<new>
#include	<memory>

#include	"dbox.h"
#include	"err.h"
#include	"except.h"
#include	"debug.h"

#include	"appl.h"
#include	"fusen.h"
#include	"guiope.h"
#include	"evtope.h"
#include	"mainwin.h"


// ---------------------------------------------- PARSAMP_FSNREQ 内 public 関数
//
// constructor
//
PARSAMP_FSNREQ::PARSAMP_FSNREQ(const M_FUSENREQ* msg)
	: endprc(true)
{
	DPRINT(("PARSAMP_FSNREQ constructor\n"));

	// 情報の取得
	mydat.msg.fsnmsg = msg;
	mydat.vid = msg->vid;
	mydat.pwid = msg->pwid;
	mydat.pr = msg->r;
}


//
// destructor
//
PARSAMP_FSNREQ::~PARSAMP_FSNREQ()
{
	DPRINT(("PARSAMP_FSNREQ destructor\n"));
	if (endprc) {
		oend_prc(mydat.vid, NULL, 0);
	}
}


//
// application の入り口
//
ERR	PARSAMP_FSNREQ::main()
{
	ERR	er;

	er = ER_OK;

	// 管理化の必要な class の生成
	try {
		fsn = std::auto_ptr<FUSEN>(new FUSEN(mydat.msg.msg));
		gui = std::auto_ptr<GUIOPE>(new GUIOPE);

		// GUI 系の初期化
		gui->init();
	} catch (EXCEPT_FUSEN& err) {	// 付箋の読み込み障害
		er = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), er, er >> 16));
		errpanel(DBOX::EPNL_FREAD, er);
		throw EXCEPT_INITERR(er);
	} catch (std::bad_alloc) {	// メモリ不足
		DPRINT(("memory allocation error.\n"));
		errpanel(DBOX::EPNL_MEMORY, ER_NOMEM);
		throw EXCEPT_INITERR(ER_NOMEM);
	} catch (...) {			// 不定
		throw;
	}

	// 仮身の実行を宣言
	osta_prc(mydat.vid, gui->mwin->get_wid());

	// イベントループの開始
	DPRINT(("event loop start.\n"));
	er = gui->evtope->eventloop();
	DPRINT(("evt_loop exit : %d(%d)\n", er, er >> 16));

	// 終了処理
	if (er >= ER_OK) {
		// 付箋の更新(正常終了時のみ)
		try {
			// 小物なので、開いた仮身の処理はありえない
			fsn->write_fusen(mydat.msg.msg, mydat.vid);
			endprc = false;
		} catch (EXCEPT_FUSEN& err) {
			er = err.get_err();
			DPRINT(("%s : %d(%d)\n", err.what(), er, er >> 16));
			errpanel(DBOX::EPNL_FWRITE, er);
		}
	}
	gui->dest();

	setpointer(PS_SELECT, NULL);
	DPRINT(("appl exit : %d(%d)\n", er, er >> 16));
	return er;
}
