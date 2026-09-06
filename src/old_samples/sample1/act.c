/*
	act.c		サンプルプログラム : その他のイベント処理

	(C) Copyright 1998 by Personal Media Corporation
*/
#include	"sample.h"

/*
	IDLE イベント処理
*/
EXPORT	VOID	idle_fn(VOID)
{
	/* 通常は以下のような処理を行う:
		パーツの定常処理 ( cidl_par() )
		カレット、チラツキ枠の点滅表示 (idsp_car(), adsp_sel())
		ポインタ形状の変更
		その他の定常的な処理

	   このサンプルではポインタ形状の変更のみを行う
	*/

	/* ポインタ形状の変更:
		プリメニュー状態でなく、自ウィンドウの作業領域内に
		ある場合のみポインタ形状を変更する
	*/

	if ( !(wevt.s.stat & ES_CMND) &&
			wevt.s.wid == mywid && wevt.s.cmd == W_WORK) {
		/* 線幅に対応したポインタ形状をデータボックスから取り出して
		   設定する。（ style は 線幅の負の数とする)	*/

		setpointer( (- l_width), (VP)getdbox(PTR_SHAPE + l_width));
	}
}

/*
	状態変更イベント処理
		sts =	0:	EV_INACT (他ウィンドウへの切り換え)
			1:	EV_SWITCH または EV_RSWITCH
			2:	W_SWITCH (他ウィンドウからの復帰)
			0x100:	EV_INACT (パネルへの切り換え)
			0x102:	W_SWITCH (パネルからの復帰)
			< 0:	W_CLOSED (-sts は wid)
*/
EXPORT	VOID	sts_fn(W ix, W sts)
{
	/* 通常は sts に応じて、チラツキ枠、カレットの表示/消去等の処理を行う:
	   特にウィンドウ内にパーツを含む時は、以下の処理を行う必要がある:
		if (sts != 0x100) {
			cdsp_pwd(mywid, NULL, P_RDISP)
		}
	   このサンプルでは特に何も行わない
	*/
}

/*
	終了イベント処理
		mode =	0: 通常
			1: 削除要求 (W_DELETE)
			2: 終了要求 (W_FINISH)
*/
EXPORT	W	fin_fn(W ix, W mode)
{
	/* W_FINISH 要求の時は、即座に処理を終了する:
	   その他の要求の場合は、終了コマンドの処理を行う
	*/
	return (mode == 2) ? -1 : cmd_close(mode);
}

/*
	キー入力イベント処理
*/
EXPORT	W	key_fn(VOID)
{
	/* キーメニューの場合は、メニュー処理を実行する */
	if ((wevt.s.stat & ES_CMND) != 0) return menu_fn();

	/* wevt.e.data.key.code に得られたキーコードが入っているので、
	   キー入力に対応した処理を行う。
	   このサンプルでは、例として、削除、取消キーのみの処理のみを行う
	*/

	switch(wevt.e.data.key.code) {
	case TK_DEL:	/* 削除キー */
		cmd_edit(0);		/* 「全削除」の処理を行う */
		break;
	case TK_CAN:	/* 取消キー */
		cmd_edit(1);		/* 「１つ削除」の処理を行う */
		break;
	}
	return	0;
}
