//
//	main.cc (壁紙変更/メイン)
//
//	(C) Copyright 2001 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/libapp.h>
#include	<errcode.h>
#include	<bstdlib.h>

#include	<new>

#include	"val.h"
#include	"dbox.h"
#include	"except.h"
#include	"err.h"
#include	"debug.h"

#include	"main.h"
#include	"appl.h"


// 広域変数
EXPORT	WALLCHG_APPL*	appl = NULL;	// application 基幹


// ----------------------------------------------------------------------- MAIN
extern "C" W MAIN(MESSAGE* msg)
{
	ERR	er;
	IMPORT	ERR	_StartupError;

	if (_StartupError < ER_OK) {
		er = _StartupError;
		DPRINT(("WARNING : startup error : %d\n", er >> 16));
		goto EXIT;
	}
#ifdef	DEBUG
	malloctest(1);
#endif	// DEBUG
	try {
		appl = new WALLCHG_APPL(msg);	// application 基幹の生成
		er = appl->main();	// application の実行
	} catch (EXCEPT_EXECMSG& err) {	// 起動方法不正
					// (appl の constructor でのみ発生)
		er = err.get_err();
		DPRINT(("%s : %d\n", err.what(), er >> 16));
		errpanel(DBOX::EPNL_EXECMSG, er);
		closedbox();
	} catch (EXCEPT_INITERR& err) {	// application の初期化行程での例外
					// (panel通知後にくるので panel は無し)
		er = err.get_err();
		DPRINT(("%s : %d\n", err.what(), er >> 16));
	} catch (std::bad_alloc) {
		DPRINT(("memory allocation error.\n"));
		errpanel(DBOX::EPNL_MEMORY, 0);	// 開けられないこともあるかも
		er = ER_NOMEM;
	} catch (exception& err) {
		DPRINT(("other exception.\n"));
		errpanel(DBOX::EPNL_SYSTEM, 0);	// 開けられないこともあるかも
		er = ER_SYS;
	} catch (...) {
		DPRINT(("unknown exception.\n"));
		errpanel(DBOX::EPNL_SYSTEM, 0);	// 開けられないこともあるかも
		er = ER_SYS;
	}
	sysmsg(0);

EXIT:
	delete appl;			// application 基幹の廃棄
#ifdef	DEBUG
	return er;
#else	// DEBUG
	ext_prc(er);
	return er;
#endif	// DEBUG
}
