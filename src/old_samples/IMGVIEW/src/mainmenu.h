//
//	mainmenu.h (画像閲覧/メインメニュー管理系ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_IMGVIEW_MAINMENU_H
#define	_IMGVIEW_MAINMENU_H

#include	<basic.h>
#include	<btron/btron.h>


// ------------------------------------------------------------- class MAINMENU
class	MAINMENU {
	enum MENUIDX {			// メニューの項参照番号
		MIDX_VOBJ    = 0,	// 仮身操作系(無効)
		MIDX_TOOL    = 1,	// 小物系
		MIDX_EXEC    = 2,	// 実行系(無効)
		MIDX_EXIT    = 3,	// [終了]
		MIDX_FWINDOW = 4,	// [表示]-[全画面表示]
		MIDX_REDISP  = 5,	//       -[再表示]
		MIDX_TOTRAY  = 6,	// [編集]-[トレーへ複写]
		MIDX_ZFACT   = 7,	// [倍率]-[標準]/[拡大]/[縮小]

		MENU_NUM = 8		// 総数
	};

	enum MENUINUM {			// メニューの内部番号
		MINUM_EXIT    = (MIDX_EXIT - 3),
		MINUM_FWINDOW = (MIDX_FWINDOW - 3),
		MINUM_REDISP  = (MIDX_REDISP - 3),
		MINUM_TOTRAY  = (MIDX_TOTRAY - 3),
		MINUM_ZFACT   = (MIDX_ZFACT - 3)
	};

	#define	MMASK(n)	(1 << n)
	enum MENUMASK {			// メニューのマスク
		MMASK_EXIT    = MMASK(MINUM_EXIT),
		MMASK_FWINDOW = MMASK(MINUM_FWINDOW),
		MMASK_REDISP  = MMASK(MINUM_REDISP),
		MMASK_TOTRAY  = MMASK(MINUM_TOTRAY),
		MMASK_ZFACT   = MMASK(MINUM_ZFACT)
	};

	enum MENUPIDX {			// 親項目の番号
		MPIDX_EXIT   = 0,	// [終了]系
		MPIDX_DISP   = 1,	// [表示]系
		MPIDX_EDIT   = 2,	// [編集]系
		MPIDX_ZFACT  = 3,	// [倍率]系
		MPIDX_WINDOW = 4,	// [ウィンドウ]系
		MPIDX_TOOL   = 5	// [小物]系
	};

public:
	MAINMENU();			// constructor
	~MAINMENU();			// destructor

	static	FUNCP	mfun[MENU_NUM];	// メニュー関数テーブル

	// メニューの実行処理
	W	exec();

	// 各メニュー処理関数(static member)
	static	W	mn_exit(W par);		// [終了]
	static	W	mn_fwindow(W par);	// [表示]-[全画面表示]
	static	W	mn_redisp(W par);	//       -[再表示]
	static	W	mn_totray(W par);	// [編集]-[トレーへ複写]
	static	W	mn_zfact(W par);	// [倍率]-[標準]/[拡大]/[縮小]
	static	W	mn_tool(W par);		// 小物系

	// application の実行(static memer)
	static	void	appl_exec(W par, W vid = 0);

private:
	W	mid;			// menu ID

	// インジケータの設定
	void	set_indi();

	// 有効・不能の設定
	void	set_enable();
};

#endif	// _IMGVIEW_MAINMENU_H
