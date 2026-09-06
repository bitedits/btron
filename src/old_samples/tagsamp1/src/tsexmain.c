/*=============================================================================

	tsexmain.c : 見出しパネルサンプル 実行処理部

	(C) Copyright 1999 by Personal Media Corporation

=============================================================================*/
#include	"tagsamp.h"
#include	"prototype.h"

/*=================================================================変数の宣言*/
EXPORT	W	mywid = -1;		/* ウィンドウID			*/
EXPORT	W	mygid = -1;		/* 描画環境ID			*/
EXPORT	RECT	vrect;			/* ウィンドウ表示領域		*/

EXPORT	PAT	*wbgpat = NULL;		/* ウィンドウ背景パターン	*/
EXPORT	PAT	*pbgpat = NULL;		/* パネル背景パターン		*/

EXPORT	VP	tagpnl = NULL;		/* 見出しパネル操作用		*/

/*=====================================================ウィンドウ情報レコード*/
LOCAL	WINFOREC	window[2] = {
		{ 0, 0x0010 | W_MOVE, {0, 0, 0, 0},  -1, -1, -1, 0L, 0L},
		{ 0,               0, {0, 0, 0, 0},  -1, -1, -1, 0L, 0L}
	};

/*=================================================イベントループ関数テーブル*/
LOCAL	WFUNCREC	wfunc = {
		NULL,			/* バックグラウンド処理		*/
		evt_idle,		/* アイドル処理			*/
		evt_msg,		/* 一般メッセージ処理		*/
		evt_menu,		/* メニュー処理			*/
		evt_disp,		/* 表示処理			*/
		evt_chgstate,		/* 状態変化処理			*/
		evt_key,		/* キー入力処理			*/
		evt_press,		/* ポインタプレス処理		*/
		evt_fin,		/* 終了処理			*/
		NULL,			/* 貼り込み要求処理		*/
		NULL,			/* 応答処理			*/
		NULL, 			/* スクロール処理		*/
		NULL,			/* デバイスイベント処理		*/
		NULL			/* 仮身要求イベント処理		*/
	};

/*===================================================================実行処理*/
EXPORT	W	ex_main(M_EXECREQ *msg)
{
	W	rv;				/* 関数返り値		*/
	RECT	subr;				/* 見出しパネルのrect	*/

	rv = wget_inf(WI_PANELBACK, NULL, 0);	/* 背景パターン取り出し	*/
	if (rv > 0) pbgpat = (PAT *)malloc(rv);
	if (pbgpat != NULL) {
		wget_inf(WI_PANELBACK, (VP)pbgpat, rv);
	} else {
		pbgpat = WHITE0;
	}
	rv = wget_inf(WI_ACTFRAME, NULL, 0);
	if (rv > 0) wbgpat = (PAT *)malloc(rv);
	if (wbgpat != NULL) {
		wget_inf(WI_ACTFRAME, (VP)wbgpat, rv);
	} else {
		wbgpat = WHITE0;
	}

	vrect = *((RECT *)getdbox(TS_WINDSZ));	/* ウィンドウを開く	*/
	centering(&vrect);
	rv = wopn_wnd(WA_FMOVE, 0, &vrect, NULL, TS_PICT_ID,
				(TC *)getdbox(TS_TITLE), wbgpat, NULL);
	if (rv < ER_OK) {
		errpanel(EPNL_WIND, rv);
		goto EXIT;
	}
	mywid = rv;
	window[0].wid = rv;
	if (msg->type == EXECREQ) {		/* 仮身の実行処理の開始	*/
		rv = osta_prc(myvid, mywid);
		if (rv < 0) {
			errpanel(EPNL_OSTA, rv);
			goto EXIT;
		}
	}

	mygid = wget_gid(mywid);		/* GID取得・描画領域設定*/
	gset_vis(mygid, vrect);
	gset_chc(mygid, RGB_BLACK, COL_TRANS);

	rv = openmenu(TS_MENU);
	if (rv < ER_OK) {			/* メニューの初期化	*/
		errpanel(EPNL_MENU, rv);
		goto EXIT;
	}

	subr = vrect;				/* 見出しパネルの初期化	*/
	sizerect(&subr, -FRAMEGAP, -FRAMEGAP);
	subr.c.top = subr.c.top + TITLEGAP + CHSSTD;
	posrect(&vrect, toPNT(-subr.c.left, -subr.c.top));
	posrect(&subr,  toPNT(           0,           0));
	wset_wrk(mywid, &vrect);
	rv = init_subpnl(&subr);
	if (rv < ER_OK) {
		errpanel(EPNL_SYSTEM, rv);
		goto EXIT;
	}
	switch_subpnl(num_tagpnl(tagpnl));

	evt_loop(&wfunc, window);

EXIT:
	return rv;
}
