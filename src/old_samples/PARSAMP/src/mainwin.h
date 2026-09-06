//
//	mainwin.h (パーツ操作例/主ウィンドウ操舵系ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_PARSAMP_MAINWIN_H
#define	_PARSAMP_MAINWIN_H

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/dp.h>

#include	"struct.h"


// -------------------------------------------------------------- class MAINWIN
class	MAINWIN {
	enum PTIDX {			// ウィンドウ内項目内部番号
		PTIDX_STR_STR   = 0,	// [文字列：]
		PTIDX_TB_STR1   = 1,	// 文字列入力用 TB_PARTS (その1)
		PTIDX_TB_STR2   = 2,	// 文字列入力用 TB_PARTS (その2)
		PTIDX_AS_ENABLE = 3,	// [有効]
		PTIDX_STR_COLOR = 4,	// [色：]
		PTIDX_SS_COLOR  = 5,	// 色選択用 SS_PARTS
		PTIDX_STR_SIZE  = 6,	// [サイズ：]
		PTIDX_VL_SIZE   = 7,	// サイズ設定用 VL_PARTS
		PTIDX_SB_SIZE   = 8,	// サイズ設定用 SB_PARTS
		PTIDX_STR_PITCH = 9,	// [ピッチ：]
		PTIDX_WS_PITCH  = 10,	// ピッチ選択用 WS_PARTS
		PTIDX_MS_INIT   = 11,	// [初期化]

		PTIDX_ALL = 12		// 項目総数
	};

	#define	PTMASK(n)	(1 << n)
	enum PTMSK {			// ウィンドウ内項目のマスク
		PTMSK_STR_STR   = PTMASK(PTIDX_STR_STR),
		PTMSK_TB_STR1   = PTMASK(PTIDX_TB_STR1),
		PTMSK_TB_STR2   = PTMASK(PTIDX_TB_STR2),
		PTMSK_AS_ENABLE = PTMASK(PTIDX_AS_ENABLE),
		PTMSK_STR_COLOR = PTMASK(PTIDX_STR_COLOR),
		PTMSK_SS_COLOR  = PTMASK(PTIDX_SS_COLOR),
		PTMSK_STR_SIZE  = PTMASK(PTIDX_STR_SIZE),
		PTMSK_VL_SIZE   = PTMASK(PTIDX_VL_SIZE),
		PTMSK_SB_SIZE   = PTMASK(PTIDX_SB_SIZE),
		PTMSK_STR_PITCH = PTMASK(PTIDX_STR_PITCH),
		PTMSK_WS_PITCH  = PTMASK(PTIDX_WS_PITCH),
		PTMSK_MS_INIT   = PTMASK(PTIDX_MS_INIT),

		PTMSK_ALL = 0xffffffff	// 全て
	};

	// パーツ操舵関数の基本定義
	typedef	ERR	(MAINWIN::*PAR_FN)(W pid, W pidx);

public:
	MAINWIN();			// constructor
	~MAINWIN();			// destructor

	// window ID の取得
	const	W	get_wid() {return wid;}

	// ウィンドウを開ける
	void	open_win(const W pwid, const RECT* pr);

	// ウィンドウを閉じる
	void	close_win();

	// パーツを生成する
	void	par_create();

	// パーツを廃棄する
	void	par_destroy();

	// 設定を初期値に戻す
	void	init_data();

	// トレーに複写/移動する内容があるか確認する
	bool	chk_totray();

	// 選択文字列を削除する
	void	del_string();

	// (一時含む)トレー経由でのトレーへ複写/移動の実行
	void	do_pushtray(bool tmp, bool cut);

	// (一時含む)トレー経由でのトレーから複写/移動の実行
	void	do_poptray(bool tmp, bool cut);

	// 再描画処理
	void	redisp(RECT* r = NULL);

	// event loop 用
	void	idle_fn();
	void	disp_fn(W mode, RECT* newr);
	W	key_fn();
	W	press_fn();
	W	paste_fn(PNT pos);

private:
	W	wid;			// window ID
	W	gid;			// window 内描画環境 ID
	W	actbox;			// 実行されているべきボックス系パーツ
	W	pactbox;		// 貼り込みを受け入れた項目番号
	PNT	ppos;			// 貼り込みを受け入れた時の座標
	RECT	vrect;			// 作業領域
	WINDEF	wdef;			// ウィンドウ定義
	RLIST	rlist[4];		// 部分再描画領域
	PAR_FN	par_fn[PTIDX_ALL];	// パーツ操舵関数
	PNL_ITEM	parts[PTIDX_ALL];	// パーツ管理用

	// 文字列の再描画
	void	disp_string();

	// 自ウィンドウ内での文字列の移動/複写
	void	str_move(W pid, bool cut);

	// パーツ状態の初期化
	void	par_setup(UW msk = PTMSK_ALL);

	// パーツの状態を取得
	void	par_getdat(UW msk = PTMSK_ALL);

	// パーツの動作関数
	ERR	p_tb_str1_fn(W pid, W pidx);
	ERR	p_tb_str2_fn(W pid, W pidx);
	ERR	p_as_enable_fn(W pid, W pidx);
	ERR	p_ss_color_fn(W pid, W pidx);
	ERR	p_vl_size_fn(W pid, W pidx);
	ERR	p_sb_size_fn(W pid, W pidx);
	ERR	p_ws_pitch_fn(W pid, W pidx);
	ERR	p_ms_init_fn(W pid, W pidx);
};

#endif	// _PARSAMP_MAINWIN_H
