//
//	mainwin.h (画像閲覧/主ウィンドウ操舵系ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_IMGVIEW_MAINWIN_H
#define	_IMGVIEW_MAINWIN_H

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/dp.h>

#include	"struct.h"


// -------------------------------------------------------------- class MAINWIN
class	MAINWIN {
	// スクロールバーの種別の表記(libapp用)
	enum SBAR_TYPE {		// スクロールバー種別
		TYPE_RBAR = 0x0000,	// 右スクロールバー
		TYPE_BBAR = 0x0010,	// 下スクロールバー
		TYPE_LBAR = 0x0020,	// 左スクロールバー
		TYPE_MASK = 0x0030
	};
	enum SCRL_TYPE {		// スクロール方法種別
		SCRL_SMTH = 0x0000,	// smooth scroll
		SCRL_AREA = 0x0004,	// area scroll
		SCRL_JUMP = 0x0008,	// jump(drag) scroll
		SCRL_MASK = 0x000c
	};
	enum SDRCT_TYPE {		// スクロール方向種別
		DRCT_UP   = 0x0000,	// 上方向
		DRCT_DOWN = 0x0001,	// 下方向
		DRCT_LEFT = 0x0002,	// 左方向
		DRCT_RIGHT= 0x0003,	// 右方向
		DRCT_MASK = 0x0003
	};

public:
	MAINWIN();			// constructor
	~MAINWIN();			// destructor

	// window ID の取得
	const	W	get_wid() {return wid;}

	// ウィンドウを開ける
	void	open_win(const W pwid, const RECT* pr);

	// ウィンドウを閉じる
	void	close_win();

	// 再描画処理
	void	redisp(RECT* r = NULL);

	// 倍率の変動に伴う処理
	void	chg_zfact();

	// event loop 用
	void	idle_fn();
	void	disp_fn(W mode, RECT* newr);
	W	key_fn();
	W	press_fn();
	void	scroll_fn(W type, W diff);

private:
	W	wid;			// window ID
	W	gid;			// window 内描画環境 ID
	W	bar[2];			// scroll bar parts ID
	RECT	vrect;			// 作業領域
	COLOR	bgcol;			// ウィンドウ内背景色
	WINDEF	wdef;			// ウィンドウ定義
	RLIST	rlist[4];		// 部分再描画領域

	// 画像の表示
	void	disp_img(RECT dr);

	// スクロールバーの値を設定する
	void	sbar_setup();

	// ウィンドウタイトルの設定
	void	set_wintit();
};

#endif	// _IMGVIEW_MAINWIN_H
