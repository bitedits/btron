//
//	mainmenu.cc (偽仮身一覧/メインメニュー管理系)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/hmi.h>
#include	<btron/vobj.h>
#include	<btron/libapp.h>

#include	<memory>

#include	"val.h"
#include	"cval.h"
#include	"dbox.h"
#include	"err.h"
#include	"except.h"
#include	"debug.h"

#include	"appl.h"
#include	"editobj.h"
#include	"vobjope.h"
#include	"vobjs.h"
#include	"fusen.h"
#include	"guiope.h"
#include	"evtope.h"
#include	"mainwin.h"
#include	"mainmenu.h"
#include	"ud_ope.h"
#include	"trayope.h"
#include	"saveope.h"
#include	"tidypnl.h"


// 内部変数(メニュー処理関数参照用)
LOCAL	W	svid = -1;		// メニュー実行時に選択中の仮身 ID


// 広域変数の取り込み(from libapp)
IMPORT	W	_inact_vmitem[];	// 仮身操作系メニューの禁止の設定


// --------------------------------------- MAINMENU 内 static member 変数の実体
FUNCP	MAINMENU::mfun[MENU_NUM];


// ---------------------------------- MAINMENU 内 public 関数(非 static member)
//
// constructor
//
MAINMENU::MAINMENU()
	: mid(-1)
{
	// メニュー関数テーブルの初期化
	mfun[MIDX_VOBJ] = reinterpret_cast<FUNCP>(&mn_vobj);
	mfun[MIDX_TOOL] = reinterpret_cast<FUNCP>(&mn_tool);
	mfun[MIDX_EXEC] = reinterpret_cast<FUNCP>(&mn_exec);
	mfun[MIDX_EXIT] = reinterpret_cast<FUNCP>(&mn_exit);
	mfun[MIDX_NEWSAVE] = reinterpret_cast<FUNCP>(&mn_newsave);
	mfun[MIDX_SAVE] = reinterpret_cast<FUNCP>(&mn_save);
	mfun[MIDX_FWINDOW] = reinterpret_cast<FUNCP>(&mn_fwindow);
	mfun[MIDX_REDISP] = reinterpret_cast<FUNCP>(&mn_redisp);
	mfun[MIDX_CHGBACK] = reinterpret_cast<FUNCP>(&mn_chgback);
	mfun[MIDX_HIDDEN] = reinterpret_cast<FUNCP>(&mn_hidden);
	mfun[MIDX_UNDO] = reinterpret_cast<FUNCP>(&mn_undo);
	mfun[MIDX_TOTRAY] = reinterpret_cast<FUNCP>(&mn_totray);
	mfun[MIDX_FROMTRAY] = reinterpret_cast<FUNCP>(&mn_fromtray);
	mfun[MIDX_DELETE] = reinterpret_cast<FUNCP>(&mn_delete);
	mfun[MIDX_ALLSEL] = reinterpret_cast<FUNCP>(&mn_allsel);
	mfun[MIDX_AUTOTIDY] = reinterpret_cast<FUNCP>(&mn_autotidy);
	mfun[MIDX_SELFTIDY] = reinterpret_cast<FUNCP>(&mn_selftidy);
	mfun[MIDX_NUMBER] = reinterpret_cast<FUNCP>(&mn_number);
	mfun[MIDX_HOLD] = reinterpret_cast<FUNCP>(&mn_hold);
	mfun[MIDX_BACK] = reinterpret_cast<FUNCP>(&mn_back);

	// メニューを登録する
	mid = openmenu(DBOX::MENU_MAIN);
	if (mid < ER_OK) {
		throw EXCEPT_MAINMENU(mid);
	}
}


//
// destructor
//
MAINMENU::~MAINMENU()
{
	closemenu();
}


//
// メニューの実行処理
//
W	MAINMENU::exec()
{
	svid = appl->vobjs.get_selvid();

	set_indi();			// インジケータの設定
	set_enable();			// 有効・不能の設定

	return selmenu(svid, mfun);
}


// ------------------------------------- MAINMENU 内 public 関数(static member)
//
// [終了]
//
W	MAINMENU::mn_exit(W par)
{
	return EVTOPE::evt_finish(0, 0);
}


//
// [保存]-[新しい実身へ]
//
W	MAINMENU::mn_newsave(W par)
{
	W	rv;
	W	vid;
	LINK	lnk;

	rv = 0;
	if (SAVEOPE::make_newfile(vid, lnk) > 0) {
		// 保存処理
		try {
			SAVEOPE	save(vid, &lnk);

			save.main();
		} catch (EXCEPT_SAVEOPE& err) {
			ERR	er;

			er = err.get_err();
			DPRINT(("%s : %d(%d)\n", err.what(), er, er >> 16));
			if (er == I_DLEDERR::SAVE_CANCEL) {
				;
			} else if ((er == ER_NOMEM) || (er == ER_NOSPC)) {
				errpanel(DBOX::EPNL_MEMORY, er);
			} else {
				errpanel(DBOX::EPNL_WRITE, er);
			}
		} catch (std::bad_alloc) {
			DPRINT(("memory allocation error.\n"));
			errpanel(DBOX::EPNL_MEMORY, ER_NOMEM);
		} catch (...) {
			DPRINT(("other exception.\n"));
			rv = ER_SYS;
		}
	}

	return rv;
}


//
// [保存]-[元の実身へ]
//
W	MAINMENU::mn_save(W par)
{
	W	rv;

	rv = 0;
	try {
		SAVEOPE	save(false);

		save.main();
		UD_OPE::clear_undo();
	} catch (EXCEPT_SAVEOPE& err) {
		ERR	er;

		er = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), er, er >> 16));
		if (er == I_DLEDERR::SAVE_CANCEL) {
		} else if ((er == ER_NOMEM) || (er == ER_NOSPC)) {
			errpanel(DBOX::EPNL_MEMORY, er);
		} else {
			errpanel(DBOX::EPNL_WRITE, er);
		}
	} catch (std::bad_alloc) {
		DPRINT(("memory allocation error.\n"));
		errpanel(DBOX::EPNL_MEMORY, ER_NOMEM);
	} catch (...) {
		DPRINT(("other exception.\n"));
		rv = ER_SYS;
	}

	return rv;
}


//
// [表示]-[全画面表示]
//
// 表示要求処理側で付箋固有データの更新をするので、ここでは変更しない
//
W	MAINMENU::mn_fwindow(W par)
{
	W	mode;
	RECT	r;

	mode = wchg_wnd(appl->gui->mwin->get_wid(), &r, W_MOVE);
	if (mode >= 0) {
		EVTOPE::evt_disp(CVAL::WIDX_MAINWIN, mode | 0x04, &r);
	}

	return 0;			// 常時 0
}


//
// [表示]-[再表示]
//
W	MAINMENU::mn_redisp(W par)
{
	appl->gui->mwin->disp_selfrm(0);
	appl->gui->mwin->redisp(NULL);
	appl->gui->mwin->disp_selfrm(1);

	return 0;			// 常時 0
}


//
// [表示]-[背景色変更]
//
W	MAINMENU::mn_chgback(W par)
{
	WINCOL	wcol = static_cast<WINCOL>(appl->fsn->get_wcol());

	if (chgbgcol(&wcol.mask, &wcol.col, appl->get_bgcol()) == 1) {
		appl->gui->mwin->disp_selfrm(0);
		appl->fsn->set_wcol(wcol);
		appl->gui->mwin->set_wbgpat(true);
		appl->gui->mwin->disp_selfrm(1);
	}

	return 0;			// 常時 0
}


//
// [表示]-[隠蔽]
//
W	MAINMENU::mn_hidden(W par)
{
	appl->gui->mwin->disp_selfrm(0);
	appl->vobjs.set_hidden(!(appl->vobjs.get_hidden()));
	appl->gui->mwin->redisp(NULL);
	appl->gui->mwin->disp_selfrm(1);

	return 0;			// 常時 0
}


//
// [編集]-[取り消し]
//
W	MAINMENU::mn_undo(W par)
{
	UD_OPE::exec_undo();

	return 0;			// 常時 0
}


//
// [編集]-[トレーへ複写]/[トレーへ移動]
//
W	MAINMENU::mn_totray(W par)
{
	W	rv;

	rv = 0;
	try {
		TRAYOPE::push_tray(-1, (par == 2), false, (SIZE){0, 0});
	} catch (std::bad_alloc) {
		DPRINT(("memory allocaiton error.\n"));
		errpanel(DBOX::EPNL_MEMORY, ER_NOMEM);
	} catch (...) {
		DPRINT(("other exception.\n"));
		rv = ER_SYS;
	}

	return rv;
}


//
// [編集]-[トレーから複写]/[トレーから移動]
//
W	MAINMENU::mn_fromtray(W par)
{
	W	rv;

	rv = appl->gui->mwin->do_poptray(false, (par == 2));

	return ((rv >= 0) ? 0 : rv);
}


//
// [編集]-[削除]
//
W	MAINMENU::mn_delete(W par)
{
	UD_OPE::make_undo(UD_OPE::UT_DEL);
	appl->gui->mwin->disp_selfrm(0);
	appl->vobjs.set_alldel(true, true);
	appl->gui->mwin->disp_selfrm(-2);
	appl->gui->mwin->disp_selfrm(1);
	appl->gui->mwin->upd_narea();
	appl->eobj->set_editflg(true);	// 更新

	return 0;			// 常時 0
}


//
// [編集]-[すべて選択]
//
W	MAINMENU::mn_allsel(W par)
{
	appl->vobjs.set_allsel(true, false);
	appl->gui->mwin->disp_selfrm(-2);
	appl->gui->mwin->disp_selfrm(1);

	return 0;			// 常時 0
}


//
// [編集]-[自動配置]
//
W	MAINMENU::mn_autotidy(W par)
{
	UD_OPE::make_undo(UD_OPE::UT_MOVE);
	appl->gui->mwin->disp_selfrm(0);
	appl->vobjs.do_autotidy(appl->gui->mwin->get_vrect());
	appl->gui->mwin->redisp(NULL);
	appl->gui->mwin->upd_narea();
	appl->gui->mwin->disp_selfrm(-2);
	appl->gui->mwin->disp_selfrm(1);
	appl->eobj->set_editflg(true);	// 更新

	return 0;			// 常時 0
}


//
// [編集]-[整頓]
//
W	MAINMENU::mn_selftidy(W par)
{
	bool	flg;

	flg = false;

	// 設定パネルを開ける
	try {
		TIDYPNL	pnl(svid > 0);

		flg = (pnl.exec() > 0);
	} catch (...) {
		;
	}

	// 結果の反映
	appl->gui->mwin->disp_selfrm(0);
	if (flg) {
		UD_OPE::make_undo(UD_OPE::UT_MOVE);
		appl->vobjs.do_selftidy();
		appl->gui->mwin->redisp(NULL);
		appl->gui->mwin->upd_narea();
		appl->gui->mwin->disp_selfrm(-2);
		appl->eobj->set_editflg(true);	// 更新
	}
	appl->gui->mwin->disp_selfrm(1);

	return 0;
}


//
// [編集]-[いちばん前へ]/[いちばん後ろへ]
//
W	MAINMENU::mn_number(W par)
{
	UD_OPE::make_undo((par == 1) ? UD_OPE::UT_FRONT : UD_OPE::UT_REAR);
	appl->gui->mwin->disp_selfrm(0);
	appl->vobjs.renum_selvobj(par == 1);
	appl->gui->mwin->disp_selfrm(-2);
	appl->gui->mwin->disp_selfrm(1);
	appl->eobj->set_editflg(true);	// 更新

	return 0;			// 常時 0
}


//
// [保護]-[固定化]/[固定解除]
//
W	MAINMENU::mn_hold(W par)
{
	UD_OPE::make_undo((par == 1) ? UD_OPE::UT_HOLD : UD_OPE::UT_UNHOLD);
	appl->vobjs.set_allhold((par == 1), true);
	appl->gui->mwin->disp_selfrm(-2);
	appl->gui->mwin->disp_selfrm(1);
	appl->eobj->set_editflg(true);	// 更新

	return 0;			// 常時 0
}


//
// [保護]-[背景化]/[背景解除]
//
W	MAINMENU::mn_back(W par)
{
	switch (par) {
		case 1:
			// 背景化
			appl->gui->mwin->disp_selfrm(-1);
			UD_OPE::make_undo(UD_OPE::UT_BACK);
			appl->vobjs.set_allback(true, true);
			break;
		default:
		case 2:
			// 背景解除
			appl->gui->mwin->disp_selfrm(-1);
			UD_OPE::make_undo(UD_OPE::UT_UNBACK);
			appl->vobjs.set_allsel(false, false);
			appl->vobjs.set_allback(false, false);
			break;
	}
	appl->gui->mwin->disp_selfrm(-2);
	appl->gui->mwin->disp_selfrm(1);
	appl->eobj->set_editflg(true);	// 更新

	return 0;			// 常時 0
}


//
// [仮身操作] 系
//
W	MAINMENU::mn_vobj(W par)
{
	W	rv;

	rv = 0;
	try {
		appl->vobjs.mn_vobj(par);
	} catch (EXCEPT_VOBJ& err) {
		rv = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), rv, rv >> 16));
		if ((rv == ER_NOMEM) || (rv == ER_NOSPC)) {
			errpanel(DBOX::EPNL_MEMORY, rv);
			rv = 0;
		}
	} catch (EXCEPT_VOBJS& err) {
		rv = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), rv, rv >> 16));
		if ((rv == ER_NOMEM) || (rv == ER_NOSPC)) {
			errpanel(DBOX::EPNL_MEMORY, rv);
			rv = 0;
		}
	} catch (std::bad_alloc) {
		DPRINT(("memory allocation error.\n"));
		errpanel(DBOX::EPNL_MEMORY, ER_NOMEM);
	} catch (...) {
		;
	}

	return rv;
}


//
// [小物] 系
//
W	MAINMENU::mn_tool(W par)
{
	appl_exec(par, 0);

	return 0;			// 常時 0
}


//
// [実行] 系
//
W	MAINMENU::mn_exec(W par)
{
	appl_exec(par, appl->vobjs.get_selvid());

	return 0;			// 常時 0
}


//
// application の実行(非 menu 項目)
//
void	MAINMENU::appl_exec(W par, W vid)
{
	setpointer(PS_BUSY, NULL);
	if (oexe_apg(vid, par) > 0) {
		setpointer(0x8001, NULL);
	}

	return;
}


// --------------------------------------------------- MAINMENU 内 private 関数
//
// インジケータの設定
//
void	MAINMENU::set_indi()
{
	const	WINATTR	wattr = appl->fsn->get_wattr();

	indmenu(MINUM_FWINDOW, (W)(wattr.fwindow), 0);
	indmenu(MINUM_HIDDEN, (W)(appl->vobjs.get_hidden()), 0);

	return;
}


//
// 有効・不能の設定
//
void	MAINMENU::set_enable()
{
	W	mask;

	mask = 0;

	// [取り消し] の設定
	mask |= (appl->gui->undo == NULL) ? MMASK_UNDO : 0;

	// [トレーから複写]/[トレーから移動] の設定
	mask |= (tget_sts(NULL, NULL) <= 0) ? MMASK_FROMTRAY : 0;

	// mask の反映
	inactmenu(mask, 0);

	// 他の設定(実番号で設定するもの)
	set_enable_other();

	// 仮身操作系のメニューの設定
	if (svid > 0) {
		if (appl->vobjs.get_hold(svid)) {
			// 選択している仮身は固定化状態なら、[仮身操作] は禁止
			_inact_vmitem[0] = 0xffffffff;
		} else {
			_inact_vmitem[0] = 0;
		}
	}

	return;
}


//
// 他のものの有効・不能の設定
//
// 設定対象は以下のもの
//	[編集]-[トレーへ複写]
//	      -[トレーへ移動]
//	      -[削除]
//	      -[すべて選択]
//	      -[自動配置]
//	      -[整頓]
//            -[いちばん前へ]
//            -[いちばん後ろへ]
//	[保護]-[固定化]
//	      -[固定解除]
//	      -[背景化]
//	      -[背景解除]
//
void	MAINMENU::set_enable_other()
{
	UW	tocopy;			// [編集]-[トレーへ複写] 用
	UW	tomove;			//       -[トレーへ移動] 用
	UW	del;			//       -[削除] 用
	UW	allsel;			//       -[すべて選択] 用
	UW	autotidy;		//       -[自動配置] 用
	UW	tidy;			//       -[整頓] 用
	UW	number;			//       -[いちばん前へ/後ろへ] 用
	UW	hold;			// [保護]-[[固定化] 用
	UW	nothold;		//       -[固定解除] 用
	UW	back;			//       -[背景化] 用
	UW	notback;		//       -[背景解除] 用
	const	VOBJ*	vobj = appl->vobjs.get_vobj();

	tocopy = M_INACT;
	tomove = M_INACT;
	del = M_INACT;
	allsel = M_INACT;
	autotidy = M_INACT;
	tidy = M_INACT;
	number = M_INACT;
	hold = M_INACT;
	nothold = M_INACT;
	back = M_INACT;
	notback = M_INACT;

	if (vobj != NULL) {
		VOBJ*	ptr = const_cast<VOBJ*>(vobj);

		do {
			const	VLINK*	vlnk = ptr->get_vlnk();
			if ((!(vlnk->attr & V_HIDDEN)) ||
			    (appl->vobjs.get_hidden())) {
				// 表示状態の仮身がいるだけ
				allsel = M_ACT;
				autotidy = M_ACT;
			}

			if (ptr->get_back()) {
				// 背景化されているものがいるだけ
				notback = M_ACT;
			}

			if (ptr->get_sel()) {
				// 選択状態
				tocopy = M_ACT;
				number = M_ACT;

				if (ptr->get_hold()) {
					// 選択/固定状態
					nothold = M_ACT;
				} else {	
					// 選択/非固定状態
					tomove = M_ACT;
					del = M_ACT;
					hold = M_ACT;
					tidy = M_ACT;
				}

				if (!(ptr->get_back())) {
					// 選択/非背景状態
					back = M_ACT;
				}
			}
			ptr = const_cast<VOBJ*>(ptr->get_next());
		} while (ptr != vobj);
	}

	// 状態を設定する
	mchg_atr(mid, 0x0302, tocopy);
	mchg_atr(mid, 0x0304, tomove);
	mchg_atr(mid, 0x0306, del);
	mchg_atr(mid, 0x0308, allsel);
	mchg_atr(mid, 0x0309, autotidy);
	mchg_atr(mid, 0x030a, tidy);
	mchg_atr(mid, 0x030c, number);
	mchg_atr(mid, 0x030d, number);
	mchg_atr(mid, 0x0401, hold);
	mchg_atr(mid, 0x0402, nothold);
	mchg_atr(mid, 0x0404, back);
	mchg_atr(mid, 0x0405, notback);

	return;
}
