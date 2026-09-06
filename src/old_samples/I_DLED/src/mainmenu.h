//
//	mainmenu.h (偽仮身一覧/メインメニュー管理系ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLED_MAINMENU_H
#define	_I_DLED_MAINMENU_H

#include	<basic.h>
#include	<btron/btron.h>


// ------------------------------------------------------------- class MAINMENU
class	MAINMENU {
	enum MENUIDX {			// メニューの項参照番号
		MIDX_VOBJ     = 0,	// 仮身操作系
		MIDX_TOOL     = 1,	// 小物系
		MIDX_EXEC     = 2,	// 実行系
		MIDX_EXIT     = 3,	// [終了]
		MIDX_NEWSAVE  = 4,	// [保存]-[新しい実身へ]
		MIDX_SAVE     = 5,	//       -[元の実身へ]
		MIDX_FWINDOW  = 6,	// [表示]-[全画面表示]
		MIDX_REDISP   = 7,	//       -[再表示]
		MIDX_CHGBACK  = 8,	//       -[背景色変更]
		MIDX_HIDDEN   = 9,	//       -[隠蔽]
		MIDX_UNDO     = 10,	// [編集]-[取り消し]
		MIDX_TOTRAY   = 11,	//       -[トレーへ複写/移動]
		MIDX_FROMTRAY = 12,	//       -[トレーから複写/移動]
		MIDX_DELETE   = 13,	//       -[削除]
		MIDX_ALLSEL   = 14,	//       -[すべて選択]
		MIDX_AUTOTIDY = 15,	//       -[自動配置]
		MIDX_SELFTIDY = 16,	//       -[整頓]
		MIDX_NUMBER   = 17,	//       -[いちばん前へ/後ろへ]
		MIDX_HOLD     = 18,	// [保護]-[固定化/固定解除]
		MIDX_BACK     = 19,	//       -[背景化/背景解]

		MENU_NUM = 20		// 総数
	};

	enum MENUINUM {			// メニューの内部番号
		MINUM_EXIT     = (MIDX_EXIT - 3),
		MINUM_NEWSAVE  = (MIDX_NEWSAVE - 3),
		MINUM_SAVE     = (MIDX_SAVE - 3),
		MINUM_FWINDOW  = (MIDX_FWINDOW - 3),
		MINUM_REDISP   = (MIDX_REDISP - 3),
		MINUM_CHGBACK  = (MIDX_CHGBACK - 3),
		MINUM_HIDDEN   = (MIDX_HIDDEN - 3),
		MINUM_UNDO     = (MIDX_UNDO - 3),
		MINUM_TOTRAY   = (MIDX_TOTRAY - 3),
		MINUM_FROMTRAY = (MIDX_FROMTRAY - 3),
		MINUM_DELETE   = (MIDX_DELETE - 3),
		MINUM_ALLSEL   = (MIDX_ALLSEL - 3),
		MINUM_AUTOTIDY = (MIDX_AUTOTIDY - 3),
		MINUM_SELFTIDY = (MIDX_SELFTIDY - 3),
		MINUM_NUMBER   = (MIDX_NUMBER - 3),
		MINUM_HOLD     = (MIDX_HOLD - 3),
		MINUM_BACK     = (MIDX_BACK - 3)
	};

	#define	MMASK(n)	(1 << n)
	enum MENUMASK {			// メニューのマスク
		MMASK_EXIT     = MMASK(MINUM_EXIT),
		MMASK_NEWSAVE  = MMASK(MINUM_NEWSAVE),
		MMASK_SAVE     = MMASK(MINUM_SAVE),
		MMASK_FWINDOW  = MMASK(MINUM_FWINDOW),
		MMASK_REDISP   = MMASK(MINUM_REDISP),
		MMASK_CHGBACK  = MMASK(MINUM_CHGBACK),
		MMASK_HIDDEN   = MMASK(MINUM_HIDDEN),
		MMASK_UNDO     = MMASK(MINUM_UNDO),
		MMASK_TOTRAY   = MMASK(MINUM_TOTRAY),
		MMASK_FROMTRAY = MMASK(MINUM_FROMTRAY),
		MMASK_DELETE   = MMASK(MINUM_DELETE),
		MMASK_ALLSEL   = MMASK(MINUM_ALLSEL),
		MMASK_AUTOTIDY = MMASK(MINUM_AUTOTIDY),
		MMASK_SELFTIDY = MMASK(MINUM_SELFTIDY),
		MMASK_NUMBER   = MMASK(MINUM_NUMBER),
		MMASK_HOLD     = MMASK(MINUM_HOLD),
		MMASK_BACK     = MMASK(MINUM_BACK)
	};

	enum MENUPIDX {			// 親項目の番号
		MPIDX_EXIT    = 0,	// [終了] 系
		MPIDX_SAVE    = 1,	// [保存] 系
		MPIDX_DISP    = 2,	// [表示] 系
		MPIDX_EDIT    = 3,	// [編集] 系
		MPIDX_PROTECT = 4,	// [保護] 系
		MPIDX_VOBJ    = 5,	// [仮身操作] 系
		MPIDX_FILE    = 6,	// [実身操作] 系
		MPIDX_DISK    = 7,	// [ディスク操作] 系
		MPIDX_WINDOW  = 8,	// [ウィンドウ] 系
		MPIDX_TOOL    = 9,	// [小物] 系
		MPIDX_EXEC    = 10	// [実行] 系
	};

public:
	MAINMENU();			// constructor
	~MAINMENU();			// destructor

	static	FUNCP	mfun[MENU_NUM];	// メニュー関数テーブル

	// メニューの実行処理
	W	exec();

	// 各メニュー処理関数(static member)
	static	W	mn_exit(W par);		// [終了]
	static	W	mn_newsave(W par);	// [保存]-[新しい実身へ]
	static	W	mn_save(W par);		//       -[元の実身へ]
	static	W	mn_fwindow(W par);	// [表示]-[全画面表示]
	static	W	mn_redisp(W par);	//       -[再表示]
	static	W	mn_chgback(W par);	//       -[背景色変更]
	static	W	mn_hidden(W par);	//       -[隠蔽]
	static	W	mn_undo(W par);		// [編集]-[取り消し]
	static	W	mn_totray(W par);	//       -[トレーへ複写/移動]
	static	W	mn_fromtray(W par);	//       -[トレーから複写/移動]
	static	W	mn_delete(W par);	//       -[削除]
	static	W	mn_allsel(W par);	//       -[すべて選択]
	static	W	mn_autotidy(W par);	//       -[自動配置]
	static	W	mn_selftidy(W par);	//       -[整頓]
	static	W	mn_number(W par);	//       -[いちばん前へ/後ろへ]
	static	W	mn_hold(W par);		// [保護]-[固定化/固定解除]
	static	W	mn_back(W par);		//       -[背景化/背景解除]
	static	W	mn_vobj(W par);		// [仮身操作] 系
	static	W	mn_tool(W par);		// [小物] 系
	static	W	mn_exec(W par);		// [実行] 系

	// application の実行(static memer)
	static	void	appl_exec(W par, W vid = 0);

private:
	W	mid;			// menu ID

	// インジケータの設定
	void	set_indi();

	// 有効・不能の設定
	void	set_enable();

	// 他のものの有効・不能の設定
	void	set_enable_other();
};

#endif	// _I_DLED_MAINMENU_H
