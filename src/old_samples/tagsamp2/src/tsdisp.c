/*=============================================================================

	tsdisp.c : 見出しパネルサンプル 表示・描画処理

	(C) Copyright 1999 by Personal Media Corporation

=============================================================================*/
#include	"tagsamp.h"
#include	"prototype.h"

/*=================================================================再表示処理*/
LOCAL	VOID	redisp(RECT *rp, Bool sw)
{
	W	n;
	W	x;
	W	y;
	W	item;
	UH	*p;
	PNT	ep;
	PNT	pos;

	dsp_tagpnl(tagpnl, rp);		/* 見出しパネルの再表示		*/

	gset_vis(mygid, *rp);		/* 表示領域の設定・初期化	*/

		/* 見出しパネルのページを切り替えた時の初期化の呼び出し	*/
	if (sw) (*(subfn[subnum].init))(K_INIT);

	p = subpnl[subnum].items;	/* 見出しパネル内の表示		*/
	while ((item = *p++) != 0) {
		n = *p++;
		if (n >= 0x8000) {	/* 表示位置の設定		*/
			gget_chp(mygid, &x, &y);
			pos.x = x;
			pos.y = y;
		} else {
			pos.x = n * (CHSSTD >> 4);
			pos.y = (*p++) * (CHSSTD >> 4);
		}
		if (item < 0x8000) {	/* 文字列を表示			*/
			gdra_stp(mygid, pos.x, pos.y,
					(TC *)ptrdbox(item), 50, G_STORE);
		} else if (item == 0x8000) {	/* 直線を描画		*/
			ep.x = *p++;
			ep.y = *p++;
			gdra_lin(mygid, pos, ep, 1, BLACK100, G_STORE);
		}
	}
	gset_vis(mygid, vrect);		/* 表示領域の復帰		*/

	cdsp_pwd(mywid, rp, P_RDISP);	/* パーツ再表示			*/
}

/*=========================================================見出しパネルの処理*/
EXPORT	ERR	init_subpnl(RECT *subrp)	/* 見出しパネルの初期化	*/
{
	W	i;
	TC	*name[MAX_SUBPNL];
	UH	*p;
	UH	*pp;
	ERR	rv;

	memset((VP)&subpnl[0], 0, sizeof(SUBPNL_DEF) * MAX_SUBPNL);

	p = (UH *)getdbox(TS_DEF);
	for (i = 0; *p != 0 && i < MAX_SUBPNL; i++) {
		pp = (UH *)ptrdbox(*p++);

			/* ページが実際に存在して操作対象とするのか？	*/
		if ((*(subfn[i].init))(K_CHECK) < 0) {
			name[i] = NULL;		/* ページを無効とする	*/
		} else {
			name[i] = (TC *)ptrdbox(*pp++);	/* 見出しの設定	*/
			subpnl[i].parts = pp;	/* パーツの取り出し	*/
			while (*pp++);
			subpnl[i].items = pp;	/* 表示項目の取り出し	*/
		}
	}
						/* 見出しパネルの生成	*/
	rv = cre_tagpnl(mywid, MAX_SUBPNL, name, &vrect, subrp,
						wbgpat, pbgpat, &tagpnl);

	return rv;
}

EXPORT	VOID	switch_subpnl(W pnum)		/* 見出しの切り替え	*/
{
	RECT	r;
	W	i;
	W	*pid;

	i = sw_tagpnl(tagpnl, pnum, &r);
	if ((i > 0) || (i == 0 && subnum < 0)) {
		if (subnum >= 0) {
					/* 以前のページの終了処理	*/
			(*(subfn[subnum].init))(K_FINISH);
			for (pid = subpid; *pid; pid++) {
				cdel_par(*pid, NOCLR);
				*pid = 0;
			}

		}
		subpid = subpnl[--pnum].pid;
		subnum = pnum;
		cre_parts(subnum);
		act_box = -1;

		if (i > 0) redisp(&r, True);
	}
}

/*===============================================================パーツの操作*/
EXPORT	VOID	cre_parts(W pnum)		/* パーツの登録		*/
{
	W	n;
	UH	*p;
	SWSEL	*sw;

	for (p = subpnl[pnum].parts, n = 0; *p != 0; p++) {
		sw = (SWSEL *)ptrdbox(*p);

	/* データボックスのテーブルの内容はオフセット値なので、それを実際の
	   ポインタへ変換する	*/
		switch (sw->type & P_TYPE) {
			case 4:		/* シリアルボックス		*/
				offtoptr((W *)&(((SERBOX *)sw)->fmt));
				offtoptr((W *)&(((SERBOX *)sw)->cv ));
				break;
			case 1:		/* テキストボックス		*/
			case 2:		/* シークレットテキストボックス	*/
			case 5:		/* テキストオルタネートスイッチ	*/
			case 6:		/* テキストモーメンタリスイッチ	*/
			case 9:		/* スイッチセレクタ		*/
			case 10:	/* スクロールセレクタ		*/
				offtoptr((W *)&sw->name);
				break;
			case 3:		/* 数値ボックス			*/
			case 7:		/* ピクトグラムオルタネートスイッチ*/
			case 8:		/* ピクトグラムモーメンタリスイッチ*/
			case 11:	/* ボリューム（スクロールバー）	*/
				break;
		}
		adjscalr(&sw->r);
		subpid[n++] = ccre_par(mywid, (PARTS *)sw);
	}
	subpid[n] = 0;
}

EXPORT	VOID	disp_par(W pn, W sts)		/* パーツの再表示	*/
{

	cdsp_par(subpid[pn], sts);

}

/*===================================================================状態復帰*/
EXPORT	VOID	refresh(VOID)
{

	setpointer(PS_SELECT, NULL);

}

/*=========================================================イベント処理関数群*/
EXPORT	VOID	evt_disp(W ix, W mode, RECT *new)	/* 表示要求	*/
{
	W	i;
	RECT	r;
	static	RLIST	view_rl[4];

	if (mode < 0) {
		genrectlist(4, view_rl);
		redisp(&vrect, True);
		refresh();
	} else {
		do {
			i = wsta_dsp(mywid, &r, view_rl);
			if (i <= 0) break;
			if (i > 4) {
				redisp(&r, False);
			} else {
				while (i--) redisp(&view_rl[i].rcomp, False);
			}
		} while (wend_dsp(mywid) > 0);
	}
}
