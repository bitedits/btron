//
//	appl_ex.cc (偽仮身一覧/application 基幹部-仮身のオープン起動)
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
#include	"editobj.h"
#include	"vobjs.h"
#include	"opndsp.h"
#include	"fusen.h"
#include	"guiope.h"
#include	"evtope.h"
#include	"mainwin.h"
#include	"tadload.h"


// ------------------------------------------------ I_DLED_EXREQ 内 public 関数
//
// constructor
//
I_DLED_EXREQ::I_DLED_EXREQ(const M_EXECREQ* msg)
	: endprc(true)
{
	DPRINT(("I_DLED_EXREQ constructor\n"));

	// 情報の取得
	mydat.msg.exmsg = msg;
	mydat.vid = msg->vid;
	mydat.pwid = msg->pwid;
	mydat.pr = msg->r;
	mydat.lnk = msg->lnk;
	mydat.bgcol = msg->bgcol;
}


//
// destructor
//
I_DLED_EXREQ::~I_DLED_EXREQ()
{
	DPRINT(("I_DLED_EXREQ destructor\n"));
	if (endprc) {
		oend_prc(mydat.vid, NULL, 0);
	}
}


//
// application の入り口
//
ERR	I_DLED_EXREQ::main()
{
	ERR	er;

	er = ER_OK;

	try {
		// 管理下の必要な class の生成
		fsn = std::auto_ptr<FUSEN>(new FUSEN(mydat.msg.msg));
		gui = std::auto_ptr<GUIOPE>(new GUIOPE);
		eobj = std::auto_ptr<EDITOBJ>(new EDITOBJ(&mydat.lnk));

		// GUI 系の初期化
		gui->init();
		vobjs.set_regid(gui->mwin->get_wid());	// 基本 ID の設定

		// 実身からの読み込み
		TADLOAD	load(&mydat.lnk);

		load.main();
	} catch (EXCEPT_FUSEN& err) {	// 付箋の読み込み障害
		er = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), er, er >> 16));
		errpanel(DBOX::EPNL_FREAD, er);
		throw EXCEPT_INITERR(er);
	} catch (EXCEPT_TADLOAD& err) {	// 実身からの読み込みの障害
		er = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), er, er >> 16));
		errpanel(DBOX::EPNL_READ, er);
		throw EXCEPT_INITERR(er);
	} catch (EXCEPT_VOBJ& err) {	// 単一仮身管理系での障害
		er = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), er, er >> 16));
		errpanel(DBOX::EPNL_SYSTEM, er);
		throw EXCEPT_INITERR(er);
	} catch (EXCEPT_VOBJS& err) {	// 仮身群管理系での障害
		er = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), er, er >> 16));
		errpanel(DBOX::EPNL_SYSTEM, er);
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
	W	vgid;

	if (er >= ER_OK) {
		// 付箋の更新(正常終了時のみ)
		try {
			vgid = fsn->write_fusen(mydat.msg.msg, mydat.vid,true);
			endprc = false;
		} catch (EXCEPT_FUSEN& err) {
			er = err.get_err();
			DPRINT(("%s : %d(%d)\n", err.what(), er, er >> 16));
//			errpanel(DBOX::EPNL_FWRITE, er);
		}
	}

	if (vgid > 0) {
		// 先に表示内容を初期化しておく(表示に失敗した時用)
		RECT	r;
                
		gget_fra(vgid, &r);
		gfil_rec(vgid, r, WHITE0, 0, G_STORE);
		--r.c.right;
		--r.c.bottom;
		gdra_lin(vgid, r.p.lefttop, r.p.rightbot, 0x0001, BLACK100, G_STORE);
		try {
			// 現在登録中の仮身の削除
			vobjs.del_allvobj();

			// 開いた仮身の表示要求
			OPNDSP	odsp(vgid);

			odsp.main();
		} catch (...) {		// 何でもいいから受け取るだけ
			DPRINT(("opened virtual-object display error.\n"));
			;
		}
	}

	// 開いた仮身の処理をしてからウィンドウは閉じる
	gui->dest();

	setpointer(PS_SELECT, NULL);
	DPRINT(("appl exit : %d(%d)\n", er, er >> 16));
	return er;
}
