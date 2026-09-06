/*
	exec.c		サンプルプログラム : 仮身のオープン起動/小物起動処理

	(C) Copyright 1998 by Personal Media Corporation
*/
#include	"sample.h"

/* グローバルデータ定義 */
EXPORT	RECT		frect;			/* 全体領域(キャンバス)	*/
EXPORT	RECT		vrect;			/* 表示領域		*/
EXPORT	W		myfd = -1;		/* ファイル ID		*/
EXPORT	W		mywid = -1;		/* ウィンドウ ID	*/
EXPORT	W		mygid = -1;		/* 描画環境 ID		*/
EXPORT	M_EXECREQ	*mycmd;			/* 起動コマンド		*/

EXPORT	PAT		windpat =		/* ウィンドウ背景パターン*/
			{{0, 16, 16, RGB_WHITE, RGB_WHITE, FILL100}};

EXPORT	PAT		wkpat =			/* 作業用パターン	*/
			{{0, 16, 16, RGB_WHITE, RGB_WHITE, FILL100}};

EXPORT	W		l_width = 1;		/* 線幅 		*/
EXPORT	COLOR		l_color = RGB_BLACK;	/* 線色			*/

EXPORT	SEGMENT		*segroot = NULL;	/* 一番下のセグメント	*/
EXPORT	SEGMENT		*segtop = NULL;		/* 一番上のセグメント	*/

EXPORT	WINFOREC	window[2] =		/* ウィンドウ情報レコード */
			{ {0, 0x10 + W_MOVE, {{0,0,0,0}},{-1,-1,-1},{0,0}},
			  {0, 0,	     {{0,0,0,0}},{-1,-1,-1},{0,0}} };

EXPORT	PARA	para;				/* 機能付箋の固有データ */

/* イベント処理関数 */
LOCAL WFUNCREC	wfunc = {
		NULL,			/* バックグラウンド処理 */
		idle_fn,		/* IDLE 処理		*/
		NULL,			/* 一般メッセージ処理	*/
		menu_fn,		/* メニュー処理 	*/
		disp_fn,		/* 表示処理		*/
		sts_fn, 		/* 状態変化処理 	*/
		key_fn, 		/* キー入力処理 	*/
		pres_fn,		/* PD プレス処理	*/
		fin_fn, 		/* 終了処理		*/
		NULL,			/* 張り込み処理 	*/
		NULL,			/* 応答処理		*/
		scrl_fn,		/* スクロール		*/
		NULL,			/* デバイスイベント	*/
		NULL			/* 仮身要求イベント	*/
};


/*
	仮身のオープン起動／小物起動処理
*/
EXPORT	W	exec_main(M_EXECREQ *cmd)
{
	ERR	er;				/* リターン値 */
	TC	title[L_FNM + 1];		/* ファイル名 */
	F_STATE	f_sts;				/* ファイル情報 */
	WDSTAT	w_sts;				/* ウィンドウ情報 */
	PARA	tmp;				/* 固有データ */
	VP	p;				/* 作業用ポインタ */

	/*
	   通常は、仮身のオープン起動と小物起動のどちらか一方のみであるが、
	   このサンプルでは両方サポートするものとする
	*/
	mycmd = cmd;

	/* 固有データの内容をデータボックスに定義してある
					デフォールト値に初期化する */
	p = (VP)getdbox(DEF_PARA);
	memcpy(&para, p, sizeof(PARA));

	if ((cmd->mode & 2) != 0) {		/* 小物起動 */
		/* ウィンドウタイトルをデータボックスに定義してある
			タイトルとする */
		p = (VP)getdbox(WIND_TL);
		tc_strcpy(title, (TC*)p);

	} else {			/* 仮身のオープン起動 */
		/* 対象となる実身のオープン */
		myfd = er = oopn_obj(cmd->vid, NULL, F_READ, NULL);
		if (er < ER_OK) {
			errpanel(EP_OPEN, er);		/* オープンエラー */
			goto EXIT;
		}

		/* ファイル情報の取り出し */
		er = ofl_sts(myfd, title, &f_sts, NULL);
		if (er < ER_OK) {
			errpanel(EP_OPEN, er);		/* オープンエラー */
			goto EXIT;
		}

		/* 付せん固有データの取り出し */
		if ((cmd->mode & 1) != 0) {
			if (oget_fsn(cmd->vid, myfd, &tmp, sizeof(tmp)) >= 0) {
				p = (VP)&tmp;
			}
		} else {
			p = (VP)cmd->info;
		}

		if (p != NULL && *((UH*)p) == sizeof(PARA) - sizeof(W)) {
			memcpy(&para, p, sizeof(PARA));
		}
		/* 固有データが不正の場合は、デフォールト値となる */
	}

	/* ウィンドウのオープン */
	vrect = para.vrect;
	setup_bgcol();
	mywid = er = wopn_wnd(WA_NORMAL, cmd->pwid, &vrect, &cmd->r,
					SAMPLE_PICT, title, &windpat, NULL);
	if (er < ER_OK) {
		errpanel(EP_WIND, er);
		goto EXIT;
	}

	/* キャンバスの大きさをデータボックスから取り出して初期化する */
	p = (VP)getdbox(CANVAS_SZ);
	frect.c.left = para.offset.x;			/* オフセット */
	frect.c.top = para.offset.y;			/* オフセット */
	frect.c.right = ((UH*)p)[0] + para.offset.x;
	frect.c.bottom = ((UH*)p)[1] + para.offset.y;

	/* 仮身要求処理の開始 */
	osta_prc(cmd->vid, mywid);

	/* 描画環境 ID の取り出し */
	mygid = wget_gid(mywid);

	/* メニューの初期化 */
	if ((er = openmenu(MENU)) < ER_OK) {
		errpanel(EP_MENU, er);
		goto EXIT;
	}

	/* データの読み込み */
	if ((cmd->mode & 2) == 0) {
		/* 通常は、実身の TAD データの読みを行うが、
				このサンプルでは省略する */
		cls_fil(myfd);
		myfd = -1;
	}

	/* 定常イベント処理 -- 初期表示要求が即時に発生する */
	window[0].wid = mywid;
	er = evt_loop(&wfunc, window);

	/* 終了処理 */
EXIT:
	if (myfd >= ER_OK) cls_fil(myfd);	/* ファイルのクローズ */

	if (er >= ER_OK) {
		/* 最後のウィンドウ位置を保存 */
		if (mywid >= 0 ) {
			wget_sts(mywid, &w_sts, NULL);
			para.vrect = w_sts.r;
			para.offset = vrect.p.lefttop;
		}
		/* 機能付箋を更新する */
		if ((myfd = oopn_obj(cmd->vid, NULL, F_UPDATE, NULL)) >= 0) {
			oput_fsn(cmd->vid, myfd, &para);
			cls_fil(myfd);
		}
	}

	if (cmd->vid > 0) {
		oend_prc(cmd->vid, &para, 0);	/* 処理終了の通知 */
	}

	if (mywid >= 0) {
		wcls_wnd(mywid, CLR);		/* ウィンドウのクローズ */
	}

	closemenu();				/* メニューのクローズ */

	return	er;
}
