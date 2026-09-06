//
//	appl_tad.cc (画像閲覧/application 基幹部-開いた仮身のTAD data生成起動)
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

#include	"err.h"
#include	"except.h"
#include	"debug.h"

#include	"appl.h"
#include	"imgope.h"
#include	"fusen.h"
#include	"savedat.h"
#include	"misc.h"


// ---------------------------------------------- IMGVIEW_TADREQ 内 public 関数
//
// constructor
//
IMGVIEW_TADREQ::IMGVIEW_TADREQ(const M_TADREQ* msg)
	: rspflg(1)
{
	DPRINT(("IMGVIEW_TADREQ constructor\n"));

	// 情報の取得
	mydat.msg.tadmsg = msg;
	mydat.vid = msg->vid;
	mydat.pwid = msg->pwid;
	mydat.lnk = msg->lnk;
}


//
// destructor
//
IMGVIEW_TADREQ::~IMGVIEW_TADREQ()
{
	DPRINT(("IMGVIEW_TADREQ destructor\n"));
	oend_req(mydat.vid, rspflg);
}


//
// application の入り口
//
ERR	IMGVIEW_TADREQ::main()
{
	ERR	er;

	er = ER_OK;

	// 管理化の必要な class の生成
        try {
		fsn = std::auto_ptr<FUSEN>(new FUSEN(mydat.msg.msg));
		load_img();

		// 画像の生成
		UW	zfact = static_cast<UW>(fsn->get_zfact());
 
		MISC::zfact_to_tbl(zfact);
		img->disp_grph(-1, mydat.msg.tadmsg->r, mydat.msg.tadmsg->r, fsn->get_vpos(), zfact);

		// 保存処理の開始
		SAVEDAT	save(&mydat.msg.tadmsg->save);

		save.main(img->get_dbmp(), img->get_cspec());
		rspflg = 0;		// 保存に成功して終了
	} catch (EXCEPT_FUSEN& err) {	// 付箋の読み込み障害
		er = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), er, er >> 16));
		throw EXCEPT_INITERR(er);
	} catch (EXCEPT_IMGOPE& err) {	// 画像の読み込みの障害
		er = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), er, er >> 16));
		throw EXCEPT_INITERR(er);
	} catch (EXCEPT_SAVEDAT& err) {	// 保存処理系での障害
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
