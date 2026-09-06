//
//	main.cc (簡易文字列編集/メイン)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/libapp.h>
#include	<btron/cnvend.h>
#include	<errcode.h>

#include	<new>

#include	"val.h"
#include	"dbox.h"
#include	"except.h"
#include	"err.h"
#include	"debug.h"

#include	"main.h"
#include	"appl.h"


// 内部関数プロトタイプ
LOCAL	ERR	ex_main(const MESSAGE* msg);
LOCAL	VOID	errext_rsp(const MESSAGE* msg) throw();


// 広域変数
EXPORT	T_TXEDIT_APPL*	appl = NULL;	// application 基幹


//
// 起動方法の確認と基幹部の生成
//
LOCAL	ERR	ex_main(const MESSAGE* msg)
{
	ERR	er;

	// little endian を宣言
	bigEndian = False;   
 
	// 基本的な初期化
	getscreen();   
	initstdpnl();
	chg_wrk(NULL);
 
	// databox を開ける
	LINK	lnk;

	prc_inf(0, PI_LINK, &lnk, sizeof(LINK));
	if (opendatabox(&lnk, DBOX::T_TXEDIT_DTYP,DBOX::T_TXEDIT_DNUM,1) < ER_OK) {
		throw EXCEPT_INITERR(T_TXEDITERR::DBOXOPEN);
	}

	switch (msg->msg_type) {
		case EXECREQ:		// 仮身のオープン起動
			DPRINT(("execution : EXECREQ\n"));
			if (((M_EXECREQ*)msg)->mode & 0x0002) {
				// 小物起動の場合
				oend_prc(((M_EXECREQ*)msg)->vid, NULL, 0);
				throw EXCEPT_EXECMSG(ER_NOSPT);
			} else {
				// 小物以外の起動方法の場合
				appl = new T_TXEDIT_EXREQ((M_EXECREQ*)msg);
			}
			break;   
		case FUSENREQ:		// 付箋のオープン起動
			DPRINT(("execution : FUSENREQ\n"));
			oend_prc(((M_FUSENREQ*)msg)->vid, NULL, 0);
			throw EXCEPT_EXECMSG(T_TXEDITERR::FSNEXEC);
			break;
		case DISPREQ:		// 開いた仮身の表示起動
			DPRINT(("execution : DISPREQ\n"));
			oend_req(((M_DISPREQ*)msg)->vid, 1);
			throw EXCEPT_EXECMSG(T_TXEDITERR::NOEXEC);
			break;
		case PASTEREQ:		// データ貼り込み起動
			DPRINT(("execution : PASTEREQ\n"));
			oend_req(((M_PASTEREQ*)msg)->vid, 1);
			throw EXCEPT_EXECMSG(T_TXEDITERR::NOEXEC);
			break;
		case TADREQ:		// 開いた仮身の TAD データ作成起動
			DPRINT(("execution : TADREQ\n"));
			oend_req(((M_TADREQ*)msg)->vid, 1);
			throw EXCEPT_EXECMSG(T_TXEDITERR::NOEXEC);
			break;
		default:		// その他(cli 起動など)
			DPRINT(("execution : other\n"));
			throw EXCEPT_EXECMSG(ER_NOSPT);
			break;
	}

	return er;
}


//
// 起動時の error の通知
//
// oend_prc() や oend_req() が発行されない場合のためのものです。
// application 基幹 class の生成に失敗した場合が主な用途です。
//
LOCAL	VOID	errext_rsp(const MESSAGE* msg) throw()
{
	switch (msg->msg_type) {
		case EXECREQ:		// 仮身のオープン起動
			oend_prc(((M_EXECREQ*)msg)->vid, NULL, 0);
			break;
		case FUSENREQ:		// 付箋のオープン起動
			oend_prc(((M_FUSENREQ*)msg)->vid, NULL, 0);
			break;
		case DISPREQ:		// 開いた仮身の表示起動
			oend_req(((M_DISPREQ*)msg)->vid, 1);
			break;
		case PASTEREQ:		// データ貼り込み起動
			oend_req(((M_PASTEREQ*)msg)->vid, 1);
			break;
		case TADREQ:		// 開いた仮身の TAD データ作成起動
			oend_req(((M_TADREQ*)msg)->vid, 1);
			break;
		default:		// その他(cli 起動など)
			break;		// 特になにもしない
	}

	return;
}


// ----------------------------------------------------------------------- MAIN
extern "C" W MAIN(MESSAGE* msg)
{
	ERR	er;
	IMPORT	ERR	_StartupError;

	if (_StartupError < ER_OK) {
		er = _StartupError;
		errext_rsp(msg);
		DPRINT(("WARNING : startup error : %d\n", er >> 16));
		goto EXIT;
	}
#ifdef	DEBUG
	malloctest(1);
#endif	// DEBUG
	try {
		ex_main(msg);		// 起動方法の確認と基幹部の生成
		er = appl->main();	// application の実行
	} catch (EXCEPT_EXECMSG& err) {	// 起動方法不正
					// (application 基底 class の生成前
					//  ただし OS への終了通知後にくる)
		er = err.get_err();
		DPRINT(("%s : %d\n", err.what(), er >> 16));
		if (er == T_TXEDITERR::FSNEXEC) {
			// 付箋起動禁止
			errpanel(DBOX::EPNL_FSNEXEC, 0);
		} else if (er == T_TXEDITERR::NOEXEC) {
			// 他(panel は出さない)
			er = ER_NOSPT;
		} else {
			// 他
			errpanel(DBOX::EPNL_EXECMSG, er);
		}
	} catch (EXCEPT_INITERR& err) {	// application の初期化行程での例外
					// (panel通知後にくるので panel は無し)
					// (application 基幹 class 生成後にくる
					//  ので OS への終了通知も不要)
		er = err.get_err();
		DPRINT(("%s : %d\n", err.what(), er >> 16));
	} catch (std::bad_alloc) {
		DPRINT(("memory allocation error.\n"));
		errpanel(DBOX::EPNL_MEMORY, 0);	// 開けられないこともあるかも
		er = ER_NOMEM;
		errext_rsp(msg);
	} catch (exception& err) {
		DPRINT(("other exception.\n"));
		errpanel(DBOX::EPNL_SYSTEM, 0);	// 開けられないこともあるかも
		er = ER_SYS;
		errext_rsp(msg);
	} catch (...) {
		DPRINT(("unknown exception.\n"));
		errpanel(DBOX::EPNL_SYSTEM, 0);	// 開けられないこともあるかも
		closedbox();
		er = ER_SYS;
		errext_rsp(msg);
	}
	closedbox();
	sysmsg(0);

EXIT:
	delete appl;			// application 基幹の廃棄
	setpointer(PS_SELECT, NULL);
	DPRINT(("T_TXEDIT exit code : %d(%d)\n", er, er >> 16));
#ifdef	DEBUG
	return er;
#else	// DEBUG
	ext_prc(er);
	return er;
#endif	// DEBUG
}
