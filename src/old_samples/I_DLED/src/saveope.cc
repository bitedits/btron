//
//	saveope.cc (偽仮身一覧/保存処理基幹部)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/vobj.h>
#include	<btron/libapp.h>

#include	"dbox.h"
#include	"val.h"
#include	"err.h"
#include	"except.h"
#include	"debug.h"

#include	"appl.h"
#include	"editobj.h"
#include	"vobjs.h"
#include	"saveope.h"
#include	"tadsave.h"


// -------------------------------------- SAVEOPE 内 public 関数(static member)
//
// 新規保存時の保存先の生成
//
// rv == 0 : 中止/取り消し/その他のエラー
//    >  0 : 保存先生成
//
UW	SAVEOPE::make_newfile(W& vid, LINK& lnk)
{
	W	att;
	UW	rv;

	rv = 0;
	att = oatt_vob(appl->get_vid(), 0);
	if (att <= 0) {
		// 保存先の file system は切断状態
		if (att != 0) {
			errpanel(DBOX::EPNL_WRITE, att); 
		}
	} else {
		// 保存先実身の生成/仮身の貼り込み
		W	fd;
		TC	name[L_FNM + 1]; 
 
		name[0] = TNULL;
		fd = ocre_obj(appl->get_vid(), name, &vid, &lnk, 1);
		if (fd < ER_OK) {
			// 取り消しか保存先が生成できない
			if (fd != EX_PAR) {
				errpanel(DBOX::EPNL_WRITE, vid); 
			}
		} else {
			rv = 1;
			cls_fil(fd);
		}
	}

	return rv;
}


// ----------------------------------------------------- SAVEOPE 内 public 関数
//
// constructor(default)(非確認保存)
//
SAVEOPE::SAVEOPE()
	: fd(-1), vid(appl->get_vid()), newfile(false), hidden(true),
	  lnk(*appl->get_link())
{
	DPRINT(("SAVEOPE deafult constructor(no ask save).\n"));

	fd = opn_fil(&lnk, F_UPDATE | F_EXCL, NULL);
	if (fd < ER_OK) {
		// 実身が開けられないなら非保存終了
		throw EXCEPT_SAVEOPE(I_DLEDERR::NO_ALLSAVE);
	}
}


//
// constructor(copy)(確認終了/確認保存)
//
// flg == true  : 終了確認
//     == false : 保存確認
//
SAVEOPE::SAVEOPE(bool flg)
	: fd(-1), vid(appl->get_vid()), newfile(false), hidden(false),
	  lnk(*appl->get_link())
{
	DPRINT(("SAVEOPE copy constructor(ask save).\n"));

	W	sel;

	if (flg) {
		// 終了確認
		if (appl->eobj->get_editflg()) {
			// 更新終了確認パネル
			if (appl->eobj->get_skipflg()) {
				// 読み飛ばしがあることを注意書き
				sel = panel(DBOX::PNL_UPESKIP);
			} else {
				// 普通の更新
				sel = panel(DBOX::PNL_UPDEXIT);
			}
			if (sel == 1) {
				// [廃棄して終了]
				throw EXCEPT_SAVEOPE(I_DLEDERR::NO_UPDATE);
			} else if (sel == 2) {
				// [更新して終了]
				if (appl->eobj->chk_ronly()) {
					// 書き込み不可属性となっている
					if (panel(DBOX::PNL_ERRSAVE) == 1) {
						// [保存]
						if (make_newfile(vid, lnk) == 0) {
							// 新規は取り消した
							throw EXCEPT_SAVEOPE(I_DLEDERR::SAVE_CANCEL);
						}
					} else {
						// [取り消し] か何か
						throw EXCEPT_SAVEOPE(I_DLEDERR::SAVE_CANCEL);
					}
				} else if (appl->eobj->chk_update()) {
					// 他から更新されている
					sel = panel(DBOX::PNL_OTHUPD);
					if (sel == 1) {
						// [更新]
					} else if (sel == 0) {
						// [取り消し]
						if (panel(DBOX::PNL_UPDEXIT2) == 1) {
							// [廃棄して終了]
							throw EXCEPT_SAVEOPE(I_DLEDERR::NO_UPDATE);
						} else {
							// [取り消し] か何か
							throw EXCEPT_SAVEOPE(I_DLEDERR::SAVE_CANCEL);
						}
					} else {
						// 何か
						throw EXCEPT_SAVEOPE(I_DLEDERR::SAVE_CANCEL);
					}
				}
			} else {
				// [取り消し] か何か
				throw EXCEPT_SAVEOPE(I_DLEDERR::SAVE_CANCEL);
			}
		} else if (appl->eobj->chk_update()) {
			// 他から更新を受けている
			sel = panel(DBOX::PNL_OTHEXIT);
			if (sel == 1) {
				// [廃棄して終了]
				throw EXCEPT_SAVEOPE(I_DLEDERR::NO_UPDATE);
			} else if (sel == 2) {
				// [更新して終了]
				;
			} else {
				// [取り消し] か何か
				throw EXCEPT_SAVEOPE(I_DLEDERR::SAVE_CANCEL);
			}
		} else if (appl->eobj->get_saveflg()) {
			// 隠匿保存
			hidden = true;
		} else {
			// 実身の保存の必要なし
			throw EXCEPT_SAVEOPE(I_DLEDERR::NO_UPDATE);
		}

		// 保存先の接続
		W	att;

		att = oatt_vob(appl->get_vid(), 0);
		if (att <= 0) {
			// 保存先の file system は切断状態
			throw EXCEPT_SAVEOPE((att == 0) ? I_DLEDERR::SAVE_CANCEL : att);
		}
	} else {
		// 保存先の接続(通常の更新)
		W	att;

		att = oatt_vob(appl->get_vid(), 0);
		if (att <= 0) {
			// 保存先の file system は切断状態
			throw EXCEPT_SAVEOPE((att == 0) ? I_DLEDERR::SAVE_CANCEL : att);
		}

		if (appl->eobj->chk_ronly()) {
			// 書き込み不可属性となっている
			if (panel(DBOX::PNL_ERRSAVE) == 1) {
				// [保存]
				if (make_newfile(vid, lnk) == 0) {
					// 新規は取り消した
					throw EXCEPT_SAVEOPE(I_DLEDERR::SAVE_CANCEL);
				}
			} else {
				// [取り消し] か何か
				throw EXCEPT_SAVEOPE(I_DLEDERR::SAVE_CANCEL);
			}
		} else {
			// 保存確認パネル
			if (appl->eobj->get_skipflg()) {
				// 読み飛ばしがあることを注意書き
				sel = panel(DBOX::PNL_UPDSKIP);
			} else {
				// 普通の更新
				sel = panel(DBOX::PNL_UPDATE);
			}
			if (sel == 1) {
				// [更新]
				if (appl->eobj->chk_update()) {
					// 他から更新されている
					if (panel(DBOX::PNL_OTHUPD) != 1) {
						// [取り消し] か何か
						throw EXCEPT_SAVEOPE(I_DLEDERR::SAVE_CANCEL);
					}
				}
			} else {
				// [取り消し] か何か
				throw EXCEPT_SAVEOPE(I_DLEDERR::SAVE_CANCEL);
			}
		}
	}

	if (!(hidden)) {
		// 隠匿保存でない場合は、(error 時に)明示的な操作が入る
		fd = opn_fil(&lnk, F_UPDATE | F_EXCL, NULL);
		if (fd < ER_OK) {
			// 失敗したなら、新規保存を促す
			if (make_newfile(vid, lnk) > 0) {
				fd = opn_fil(&lnk, F_UPDATE | F_EXCL, NULL);
				if (fd < ER_OK) {
					// 新規実身も無理なら error 通知
					throw EXCEPT_SAVEOPE(fd);
				}
			} else {
				// 新規は取り消した
				throw EXCEPT_SAVEOPE(I_DLEDERR::SAVE_CANCEL);
			}
		}
	} else {
		// 隠匿の場合は、保存先を開けるだけ
		fd = opn_fil(&lnk, F_UPDATE | F_EXCL, NULL);
		if (fd < ER_OK) {
			throw EXCEPT_SAVEOPE(I_DLEDERR::NO_ALLSAVE);
		}
	}
}


//
// constructor(copy)(新規保存)
//
SAVEOPE::SAVEOPE(W pvid, const LINK* plnk)
	: fd(-1), vid(pvid), newfile(true), hidden(false), lnk(*plnk)
{
	DPRINT(("SAVEOPE copy constructor(new save).\n"));

	fd = opn_fil(&lnk, F_UPDATE | F_EXCL, NULL);
	if (fd < ER_OK) {
		// 実身が開けられないなら非保存終了
		throw EXCEPT_SAVEOPE(fd);
	}
}


//
// destructor
//
SAVEOPE::~SAVEOPE()
{
	DPRINT(("SAVEOPE destructor.\n"));

	if (fd >= 0) {
		cls_fil(fd);
	}
}


//
// 保存の主幹処理部
//
void	SAVEOPE::main()
{
	DPRINT(("SAVEOPE main procedure.\n"));

	try {
		TADSAVE	tsave(fd, vid);

		tsave.main();
		if (!(newfile)) {
			// (更新の時は)一時削除状態のものを抹消
			appl->vobjs.dest_delvobj();
		}
	} catch (EXCEPT_TADSAVE& err) {
		throw EXCEPT_SAVEOPE(err.get_err());
	} catch (std::bad_alloc) {
		throw EXCEPT_SAVEOPE(ER_NOMEM);
	} catch (...) {
		throw EXCEPT_SAVEOPE(ER_SYS);
	}

	return;
}
