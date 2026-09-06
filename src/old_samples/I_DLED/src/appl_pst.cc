//
//	appl_pst.cc (偽仮身一覧/application 基幹部-データ貼り込み起動)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/dp.h>
#include	<btron/hmi.h>
#include	<btron/vobj.h>
#include	<btron/libapp.h>
#include	<errcode.h>

#include	<new>
#include	<memory>

#include	"val.h"
#include	"err.h"
#include	"except.h"
#include	"debug.h"

#include	"appl.h"
#include	"editobj.h"
#include	"fusen.h"
#include	"trayope.h"
#include	"tadload.h"
#include	"saveope.h"


// ----------------------------------------------- I_DLED_PSTREQ 内 public 関数
//
// constructor
//
I_DLED_PSTREQ::I_DLED_PSTREQ(const M_PASTEREQ* msg)
	: rspflg(1), igid(-1), iwid(-1)
{
	DPRINT(("I_DLED_PSTREQ constructor\n"));

	// 情報の取得
	mydat.msg.pstmsg = msg;
	mydat.vid = msg->vid;
	mydat.pwid = msg->pwid;
	mydat.lnk = msg->lnk;
}


//
// destructor
//
I_DLED_PSTREQ::~I_DLED_PSTREQ()
{
	DPRINT(("I_DLED_PSTREQ destructor\n"));
	if (iwid >= 0) {
		odel_vob(-iwid, NOCLR);
		wcls_wnd(iwid, NOCLR);
	}
	if (igid >= 0) {
		gcls_env(igid);
	}
	oend_req(mydat.vid, rspflg);
}


//
// application の入り口
//
ERR	I_DLED_PSTREQ::main()
{
	ERR	er;

	er = ER_OK;

	try {
		// 管理下の必要な class の生成
		fsn = std::auto_ptr<FUSEN>(new FUSEN(mydat.msg.msg));
		eobj = std::auto_ptr<EDITOBJ>(new EDITOBJ(&mydat.lnk));

		// 作業用仮描画環境の生成
		igid = gnew_env((mydat.pwid < 0) ? -mydat.pwid : wget_gid(mydat.pwid));
		if (igid < ER_OK) {
			throw EXCEPT_INITERR(igid);
		}
		iwid = wopn_iwd(igid);
		if (iwid < ER_OK) {
			throw EXCEPT_INITERR(iwid);
		}
		vobjs.set_regid(-igid);

		// 実身からの読み込み
		TADLOAD*	load;

		load = new TADLOAD(&mydat.lnk);
		load->main();
		delete load;

		// 内容の確認
		if (TRAYOPE::pop_tray(true, false, false, false, (PNT){0, 0}) > 0) {
			// 貼り込み処理
			TRAYOPE::pop_tray(false, false, false, false, (PNT){0, 0});

			// 実身に保存
			SAVEOPE*	save;

			save = new SAVEOPE();
			save->main();
			delete save;

			// 貼り込みに成功
			rspflg = 0;
		}
	} catch (EXCEPT_FUSEN& err) {	// 付箋の読み込み障害
		er = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), er, er >> 16));
		throw EXCEPT_INITERR(er);
	} catch (EXCEPT_TADLOAD& err) {	// 実身からの読み込みの障害
		er = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), er, er >> 16));
		throw EXCEPT_INITERR(er);
	} catch (EXCEPT_VOBJ& err) {	// 単一仮身管理系での障害
		er = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), er, er >> 16));
		throw EXCEPT_INITERR(er);
	} catch (EXCEPT_VOBJS& err) {	// 仮身群管理系での障害
		er = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), er, er >> 16));
		throw EXCEPT_INITERR(er);
	} catch (EXCEPT_SAVEOPE& err) {	// 実身への書き込みの障害
		er = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), er, er >> 16));
		throw EXCEPT_INITERR(er);
	} catch (std::bad_alloc) {	// メモリ不足
		DPRINT(("memory allocation error.\n"));
		throw EXCEPT_INITERR(ER_NOMEM);
	} catch (...) {			// 不定
		throw;
	}

	setpointer(PS_SELECT, NULL);
	DPRINT(("appl exit : %d(%d)\n", er, er >> 16));
	return er;
}
