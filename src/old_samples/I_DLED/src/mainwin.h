//
//	mainwin.h (偽仮身一覧/主ウィンドウ操舵系ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLED_MAINWIN_H
#define	_I_DLED_MAINWIN_H

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/dp.h>

#include	"struct.h"


// 参照 class の宣言
class	PD_OPE;
class	PD_NPRS;
class	PD_HPRS;
class	PD_VPRS;
class	PD_VQP;
class	PD_CLK;
class	PD_DCLK;
class	KB_OPE;
class	KB_CAN;
class	KB_DEL;
class	KB_MOVE;
class	KB_SCRL;
class	SELFRM;


// -------------------------------------------------------------- class MAINWIN
class	MAINWIN {
public:
	MAINWIN();			// constructor
	~MAINWIN();			// destructor

	// window ID の取得
	const	W	get_wid() {return wid;}

	// 作業領域の取得
	const	RECT	get_vrect() {return vrect;}

	// ウィンドウを開ける
	void	open_win(const W pwid, const RECT* pr);

	// ウィンドウを閉じる
	void	close_win();

	// ウィンドウ背景パターンを設定する
	void	set_wbgpat(bool set = false);

	// 選択枠関連
	void	disp_selfrm(W type);

	// 表示をスクロールさせる
	void	scroll_work(PNT p);

	// 実作業領域とスクロールバーの設定
	void	upd_narea();

	// (一時含む)トレー経由でのトレーから複写/移動の実行
	W	do_poptray(bool tmp, bool cut);

	// 再描画処理
	void	redisp(RECT* r = NULL);

	// event loop 用
	void	idle_fn(bool pdflg);
	void	disp_fn(W mode, RECT* newr);
	W	key_fn();
	W	press_fn();
	W	paste_fn(PNT p);
	void	scroll_fn(W type, W diff);

private:
	W	wid;			// window ID
	W	gid;			// window 内描画環境 ID
	W	bar[2];			// scroll bar parts ID
	PAT	wbgpat;			// ウィンドウ背景パターン
	PNT	ppos;			// 貼り込み要求位置
	RECT	vrect;			// 作業領域
	WINDEF	wdef;			// ウィンドウ定義
	SELFRM*	selfrm;			// 選択枠管理系
	RLIST	rlist[4];		// 部分再描画領域

	friend	PD_OPE;
	friend	PD_NPRS;
	friend	PD_HPRS;
	friend	PD_VPRS;
	friend	PD_VQP;
	friend	PD_CLK;
	friend	PD_DCLK;
	friend	KB_OPE;
	friend	KB_CAN;
	friend	KB_DEL;
	friend	KB_MOVE;
	friend	KB_SCRL;

	// スクロールバーの値を設定する
	void	sbar_setup();

	// 仮身の配置領域と実作業領域の調節
	void	set_narea();
};

#endif	// _I_DLED_MAINWIN_H
