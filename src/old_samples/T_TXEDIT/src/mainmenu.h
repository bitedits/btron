//
//	mainmenu.h (簡易文字列編集/メインメニュー管理系ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_T_TXEDIT_MAINMENU_H
#define	_T_TXEDIT_MAINMENU_H

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
		MIDX_GRAY     = 9,	//       -[文字階調表示]

		MENU_NUM = 10		// 総数
	};

	enum MENUINUM {			// メニューの内部番号
		MINUM_EXIT     = (MIDX_EXIT - 3),
		MINUM_NEWSAVE  = (MIDX_NEWSAVE - 3),
		MINUM_SAVE     = (MIDX_SAVE - 3),
		MINUM_FWINDOW  = (MIDX_FWINDOW - 3),
		MINUM_REDISP   = (MIDX_REDISP - 3),
		MINUM_CHGBACK  = (MIDX_CHGBACK - 3),
		MINUM_GRAY     = (MIDX_GRAY - 3)
	};

	#define	MMASK(n)	(1 << n)
	enum MENUMASK {			// メニューのマスク
		MMASK_EXIT     = MMASK(MINUM_EXIT),
		MMASK_NEWSAVE  = MMASK(MINUM_NEWSAVE),
		MMASK_SAVE     = MMASK(MINUM_SAVE),
		MMASK_FWINDOW  = MMASK(MINUM_FWINDOW),
		MMASK_REDISP   = MMASK(MINUM_REDISP),
		MMASK_CHGBACK  = MMASK(MINUM_CHGBACK),
		MMASK_GRAY     = MMASK(MINUM_GRAY)
	};

	enum MENUPIDX {			// 親項目の番号
		MPIDX_EXIT    = 0,	// [終了] 系
		MPIDX_SAVE    = 1,	// [保存] 系
		MPIDX_DISP    = 2,	// [表示] 系
		MPIDX_WINDOW  = 3,	// [ウィンドウ] 系
		MPIDX_TOOL    = 4	// [小物] 系
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
	static	W	mn_gray(W par);		//       -[文字階調表示]
	static	W	mn_tool(W par);		// [小物] 系

	// application の実行(static memer)
	static	void	appl_exec(W par, W vid = 0);

private:
	W	mid;			// menu ID

	// インジケータの設定
	void	set_indi();

	// 有効・不能の設定
	void	set_enable();
};

#endif	// _T_TXEDIT_MAINMENU_H
