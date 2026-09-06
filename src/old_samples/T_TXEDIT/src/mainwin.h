//
//	mainwin.h (簡易文字列編集/主ウィンドウ操舵系ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_T_TXEDIT_MAINWIN_H
#define	_T_TXEDIT_MAINWIN_H

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/dp.h>

#include	"struct.h"


// 参照 class の宣言
class	TEXTIN;
class	CARETOPE;


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

	// 描画書体に関する設定
	void	set_char();

	// caret 関連
	void	disp_caret(W type);

	// 表示をスクロールさせる
	void	scroll_work(PNT p);

	// 再描画処理
	void	redisp(RECT* r = NULL);

	// event loop 用
	void	idle_fn(bool pdflg);
	void	disp_fn(W mode, RECT* newr);
	W	key_fn();
	W	press_fn();
	void	scroll_fn(W type, W diff);

private:
	W	wid;			// window ID
	W	gid;			// window 内描画環境 ID
	W	bar[2];			// scroll bar parts ID
	PAT	wbgpat;			// ウィンドウ背景パターン
	PNT	ppos;			// 貼り込み要求位置
	RECT	vrect;			// 作業領域
	WINDEF	wdef;			// ウィンドウ定義
	RLIST	rlist[4];		// 部分再描画領域

	std::auto_ptr<TEXTIN>	tin;	// テキスト入力ポート管理系
	std::auto_ptr<CARETOPE>	car;	// caret 管理系

	// スクロールバーの値を設定する
	void	sbar_setup();

	// caret が見えるように scroll する
	void	car_scroll(bool hscrl = true);

	// キー入力処理関連
	// かな漢字変換中での scroll 要求
	void	kcnv_scroll(TEXTPORT* tport);

	// 確定文字列の挿入
	void	ins_str(TC* str, W len);

	// KC_CC_L/R/U/D の挙動(ES_CMD 同時押し含まず)
	void	act_KC_CC(TC code);

	// KC_BS の挙動
	void	act_KC_BS();
};

#endif	// _T_TXEDIT_MAINWIN_H
