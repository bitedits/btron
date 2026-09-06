//
//	vs_vreq.cc (偽仮身一覧/仮身/付箋群管理系-仮身要求イベント処理系)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/vobj.h>
#include	<btron/libapp.h>

#include	"val.h"

#include	"appl.h"
#include	"editobj.h"
#include	"vobjope.h"
#include	"vobjs.h"
#include	"guiope.h"
#include	"mainwin.h"
#include	"saveope.h"


// ------------------------------------------------------- VOBJS 内 public 関数
//
// 仮身要求イベント処理部
//
void	VOBJS::vobj_fn()
{
	// 要求内容と仮身 ID の取得
	W	vid;
	W	type;

	type = wevt.g.data[2];
	vid = wevt.g.data[3];

	// 要求内容毎の処理
	VOBJ*	ptr;
	RECT	r;

	switch (type) {
		case 15:		// 仮身変更通知
			appl->eobj->set_saveflg(true);	// 隠匿
		case 0:			// 名称変更
		case 1:			// 処理状態へ遷移
		case 2:			// 非処理状態へ復帰
		case 3:			// 虚身状態へ移行
		case 4:			// 虚身状態から復帰
			ptr = const_cast<VOBJ*>(srch_vobj(vid));
			ptr->upd_mydat(true, true);
			orsz_vob(ptr->get_vid(), &r, V_CHECK);
			dsp_vobj(-1, &r);
			appl->gui->mwin->upd_narea();
			break;
		case 5:			// 虚身ウィンドウへ移行
		case 6:			// 虚身ウィンドウから復帰
			dsp_vobj(-1, NULL);
			break;
		case 16:		// 仮身挿入要求(デバイス挿入)
		case 17:		// 仮身挿入要求(新規実身生成)
			appl->eobj->set_editflg(true);	// 更新
			ins_vobj(vid);
			ptr = const_cast<VOBJ*>(vobj->get_prev());
			orsz_vob(ptr->get_vid(), &r, V_CHECK);
			dsp_vobj(-1, &r);
			appl->gui->mwin->upd_narea();
			break;
		case 128:		// 一時ファイル要求
			make_tmpfile();
			break;
		default:		// その他
			if (type > 128) {
				orsp_prc((EVENT*)&wevt, NULL, 0, ER_NOSPT);
			}
			break;
	}

	return;
}


// ------------------------------------------------------ VOBJS 内 private 関数
//
// 一時ファイルの生成
//
void	VOBJS::make_tmpfile()
{
	ERR	er;

	er = ER_NOSPT;
	if (oatt_vob(appl->get_vid(), 0) >= 0) {
		W	fd;
		LINK	lnk;

		lnk = *(appl->get_link());
		fd = cre_fil(&lnk, (TC*)appl->eobj->get_fname(), NULL, 0, F_FLOAT);
		if (fd >= ER_OK) {
			cls_fil(fd);
			try {
				SAVEOPE	save(appl->get_vid(), &lnk);

				save.main();
				orsp_prc((EVENT*)&wevt, (B*)&lnk, sizeof(LINK), 0);
				er = ER_OK;
			} catch (...) {
				// 何でもいい
			}
		}
	}
	if (er < ER_OK) {
		orsp_prc((EVENT*)&wevt, NULL, 0, ER_NOSPT);
	}

	return;
}
