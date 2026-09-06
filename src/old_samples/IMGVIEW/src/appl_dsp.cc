//
//	appl_dsp.cc (画像閲覧/application 基幹部-開いた仮身の表示起動)
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
#include	"opndsp.h"
#include	"imgope.h"
#include	"fusen.h"
#include	"misc.h"


// ---------------------------------------------- IMGVIEW_DSPREQ 内 public 関数
//
// constructor
//
IMGVIEW_DSPREQ::IMGVIEW_DSPREQ(const M_DISPREQ* msg)
	: rspflg(1)
{
	DPRINT(("IMGVIEW_DSPREQ constructor\n"));

	// 情報の取得
	mydat.msg.dspmsg = msg;
	mydat.vid = msg->vid;
	mydat.pwid = msg->pwid;
	mydat.lnk = msg->lnk;
}


//
// destructor
//
IMGVIEW_DSPREQ::~IMGVIEW_DSPREQ()
{
	DPRINT(("IMGVIEW_DSPREQ destructor\n"));
	oend_req(mydat.vid, rspflg);
}


//
// application の入り口
//
ERR	IMGVIEW_DSPREQ::main()
{
	ERR	er;

	er = ER_OK;

	// 管理化の必要な class の生成
	try {
		fsn = std::auto_ptr<FUSEN>(new FUSEN(mydat.msg.msg));
		load_img();

		// 表示処理
		OPNDSP	odsp(mydat.msg.dspmsg->gid);

		odsp.main();
		rspflg = 0;		// 表示に成功して終了
	} catch (EXCEPT_FUSEN& err) {	// 付箋の読み込み障害
		er = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), er, er >> 16));
		throw EXCEPT_INITERR(er);
	} catch (EXCEPT_IMGOPE& err) {	// 画像の読み込みの障害
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
