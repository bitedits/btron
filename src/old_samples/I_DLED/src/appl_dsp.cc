//
//	appl_dsp.cc (偽仮身一覧/application 基幹部-開いた仮身の表示起動)
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
#include	"opndsp.h"
#include	"fusen.h"


// ----------------------------------------------- I_DLED_DSPREQ 内 public 関数
//
// constructor
//
I_DLED_DSPREQ::I_DLED_DSPREQ(const M_DISPREQ* msg)
	: rspflg(1)
{
	DPRINT(("I_DLED_DSPREQ constructor\n"));

	// 情報の取得
	mydat.msg.dspmsg = msg;
	mydat.vid = msg->vid;
	mydat.pwid = msg->pwid;
	mydat.lnk = msg->lnk;
	mydat.bgcol = msg->bgcol;
}


//
// destructor
//
I_DLED_DSPREQ::~I_DLED_DSPREQ()
{
	DPRINT(("I_DLED_DSPREQ destructor\n"));
	oend_req(mydat.vid, rspflg);
}


//
// application の入り口
//
ERR	I_DLED_DSPREQ::main()
{
	ERR	er;
	W	vgid;

	er = ER_OK;
	vgid = mydat.msg.dspmsg->gid;

	// 先に表示内容を初期化しておく(表示に失敗した時用)
	RECT	r;

	gget_fra(vgid, &r);
	gfil_rec(vgid, r, WHITE0, 0, G_STORE);
	--r.c.right;
	--r.c.bottom;
	gdra_lin(vgid, r.p.lefttop, r.p.rightbot, 0x0001, BLACK100, G_STORE);

	try {
		// 管理下の必要な class の生成
		fsn = std::auto_ptr<FUSEN>(new FUSEN(mydat.msg.msg));
		eobj = std::auto_ptr<EDITOBJ>(new EDITOBJ(&mydat.lnk));

		// 表示処理
		OPNDSP	odsp(vgid);

		odsp.main();
		rspflg = 0;		// 表示に成功して終了
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
	} catch (EXCEPT_OVDISP& err) {	// 開いた仮身の表示上の障害
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
