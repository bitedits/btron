//
//	ud_ope.cc (偽仮身一覧/取り消し動作基底 class)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/libapp.h>
#include	<errcode.h>

#include	<new>

#include	"dbox.h"
#include	"val.h"
#include	"err.h"
#include	"except.h"

#include	"appl.h"
#include	"editobj.h"
#include	"guiope.h"
#include	"ud_ope.h"
#include	"ud_move.h"
#include	"ud_add.h"
#include	"ud_del.h"
#include	"ud_tadd.h"
#include	"ud_rsz.h"
#include	"ud_hold.h"
#include	"ud_uhold.h"
#include	"ud_back.h"
#include	"ud_uback.h"
#include	"ud_renum.h"


// --------------------------------------- UD_OPE 内 public 関数(static member)
//
// undo の生成(新規生成)
//
void	UD_OPE::make_undo(UW utype) throw()
{
	ERR	er;

	er = ER_OK;
	try {
		// undo の生成
		switch (utype) {
			case UT_MOVE:	// 位置移動
				appl->gui->undo = new UD_MOVE(utype);
				break;
			case UT_ADD:	// 仮身追加
				appl->gui->undo = new UD_ADD(utype);
				break;
			case UT_DEL:	// 仮身削除(一時削除化)
				appl->gui->undo = new UD_DEL(utype);
				break;
			case UT_TRAYADD:// 仮身追加(トレーから移動のみ)
				appl->gui->undo = new UD_TRAYADD(utype);
				break;
			case UT_TRAYDEL:// 仮身削除(トレーから移動の取り消し時)
				throw EXCEPT_UNDO(0);	// 新規操作はないはず
				break;
			case UT_RESIZE:	// 形状変更
				appl->gui->undo = new UD_RESIZE(utype);
				break;
			case UT_HOLD:	// 固定化
				appl->gui->undo = new UD_HOLD(utype);
				break;
			case UT_UNHOLD:	// 固定解除
				appl->gui->undo = new UD_UNHOLD(utype);
				break;
			case UT_BACK:	// 背景化
				appl->gui->undo = new UD_BACK(utype);
				break;
			case UT_UNBACK:	// 背景解除
				appl->gui->undo = new UD_UNBACK(utype);
				break;
			case UT_FRONT:	// いちばん前へ
			case UT_REAR:	// いちばん後ろへ
				appl->gui->undo = new UD_RENUM(utype);
				break;
			default:	// 不定
			case UT_NONE:	// 不定
				throw EXCEPT_UNDO(0);
				break;
		}
		if (appl->gui->undo != NULL) {
			appl->gui->undo->make();
		}
	} catch (EXCEPT_UNDO& err) {
		er = err.get_err();
	} catch (std::bad_alloc) {
		er = ER_NOMEM;
	} catch (...) {
		;
	}

	if (er < ER_OK) {
		errpanel(DBOX::EPNL_UNDOMAKE, er);
		appl->gui->undo = NULL;
	}

	return;
}


//
// undo の実行 - redo の生成
//
void	UD_OPE::exec_undo() throw()
{
	ERR	er;

	er = ER_OK;
	try {
		// redo 情報の生成
		const	UW	utype = appl->gui->undo->get_type();
		UD_OPE*	rbuf;

		rbuf = NULL;
		switch (utype) {
			case UT_MOVE:	// 位置移動
				rbuf = new UD_MOVE(utype);
				rbuf->make();
				appl->gui->undo->exec();
				break;
			case UT_ADD:	// 仮身追加
				rbuf = new UD_DEL(UT_DEL);
				rbuf->redo(appl->gui->undo->get_udat(), appl->gui->undo->get_ucnt());
				appl->gui->undo->exec();
				break;
			case UT_DEL:	// 仮身削除(一時削除化)
				rbuf = new UD_ADD(UT_ADD);
				rbuf->redo(appl->gui->undo->get_udat(), appl->gui->undo->get_ucnt());
				appl->gui->undo->exec();
				break;
			case UT_TRAYADD:// 仮身追加(トレーから移動のみ)
				rbuf = new UD_DEL(UT_TRAYDEL);
				rbuf->redo(appl->gui->undo->get_udat(), appl->gui->undo->get_ucnt());
				appl->gui->undo->exec();
				break;
			case UT_TRAYDEL:// 仮身削除(トレーから移動の取り消し時)
				rbuf = new UD_TRAYADD(UT_TRAYADD);
				rbuf->redo(appl->gui->undo->get_udat(), appl->gui->undo->get_ucnt());
				appl->gui->undo->exec();
				break;
			case UT_RESIZE:	// 形状変更
				rbuf = new UD_RESIZE(utype);
				rbuf->make();
				appl->gui->undo->exec();
				break;
			case UT_HOLD:	// 固定化
				rbuf = new UD_UNHOLD(UT_UNHOLD);
				rbuf->redo(appl->gui->undo->get_udat(), appl->gui->undo->get_ucnt());
				appl->gui->undo->exec();
				break;
			case UT_UNHOLD:	// 固定解除
				rbuf = new UD_HOLD(UT_HOLD);
				rbuf->redo(appl->gui->undo->get_udat(), appl->gui->undo->get_ucnt());
				appl->gui->undo->exec();
				break;
			case UT_BACK:	// 背景化
				rbuf = new UD_UNBACK(UT_UNBACK);
				rbuf->redo(appl->gui->undo->get_udat(), appl->gui->undo->get_ucnt());
				appl->gui->undo->exec();
				break;
			case UT_UNBACK:	// 背景解除
				rbuf = new UD_BACK(UT_BACK);
				rbuf->redo(appl->gui->undo->get_udat(), appl->gui->undo->get_ucnt());
				appl->gui->undo->exec();
				break;
			case UT_FRONT:	// いちばん前へ
			case UT_REAR:	// いちばん後ろへ
				rbuf = new UD_RENUM(utype);
				rbuf->make();
				appl->gui->undo->exec();
				break;
			default:	// 不定
			case UT_NONE:	// 不定
				break;
		}

		// undo の差し替え
		delete appl->gui->undo;
		appl->gui->undo = rbuf;
	} catch (EXCEPT_UNDO& err) {
		er = err.get_err();
	} catch (std::bad_alloc) {
		er = ER_NOMEM;
	} catch (...) {
		;
	}

	if (er < ER_OK) {
		errpanel(DBOX::EPNL_UNDOMAKE, er);
		delete appl->gui->undo;
		appl->gui->undo = NULL;
	}

	return;
}


//
// undo の廃棄 - 更新状態の解除
//
void	UD_OPE::clear_undo() throw()
{
	delete appl->gui->undo;
	appl->gui->undo = NULL;
	appl->eobj->set_saveflg(false);
	appl->eobj->set_editflg(false);
	appl->eobj->set_skipflg(false);
	appl->eobj->upd_mydat();

	return;
}


// ------------------------------------------------------ UD_OPE 内 public 関数
//
// constructor
//
UD_OPE::UD_OPE()
	: type(UT_NONE)
{
}
