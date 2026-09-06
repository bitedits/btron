/*
	menu.c		サンプルプログラム : メニュー処理

	(C) Copyright 1998 by Personal Media Corporation
*/
#include	"sample.h"

/*
	終了メニュー処理 (fin_fn() からも呼び出される)
		mode = 0: 通常、1: 削除要求
*/
EXPORT	W	cmd_close(W mode)
{
	/* 通常は、mode に従って、以下の処理を行う :
		mode = 0:  データの変更があればパネルを出して確認の後、
			    実身へ保存する
		mode = 1:  データの変更があればパネルを出さずに実身へ保存する

	   このサンプルでは、保存処理は省略する
	*/
	/* 終了する場合は 0 以外の値を戻す(この値は evt_loop() の戻値となる)
	   終了できない場合は 0 を戻す
	*/
	return 1;
}

/*
	再表示メニュー処理
		par =	未使用
*/
EXPORT	W	cmd_redsp(W par)
{
	view(vrect);		/* 全表示領域を再表示する */
	return	0;
}

/*
	背景色設定処理
*/
EXPORT	VOID	setup_bgcol(VOID)
{
	W	msk;

	msk = para.bgmask & 7;			/* 背景色設定済み */
	windpat.spat.fgcol = (msk != 0) ? para.bgcol : mycmd->bgcol;
	windpat.spat.mask =  (msk != 0) ? (UB*)msk : FILL100;
	windpat.spat.bgcol = (msk != 0) ? RGB_WHITE : COL_TRANS;
}

/*
	背景色変更メニュー処理
		par =	未使用
*/
EXPORT	W	cmd_bgcol(W par)
{
	W	msk;

	msk = para.bgmask;
	if (chgbgcol(&msk, &para.bgcol, mycmd->bgcol) > 0) {
		para.bgmask = msk;
		setup_bgcol();
		wset_bgp(mywid, &windpat);
		view(vrect);
	}
	return 0;
}

/*
	編集メニュー処理
		par =	1: １つ削除、0：全削除
*/
EXPORT	W	cmd_edit(W par)
{
	SEGMENT	*s;
	SEGMENT	*ss;
	RECT	r;

	if (segroot != NULL) {		/* セグメントが存在する場合 */
		if (par == 1 && segroot != segtop) {
			/* 1 つ削除で、2 つ以上セグメントがある場合 */
			for (s = segroot; segtop != s->next; s = s->next);
			/* segtop をリンクからはずして削除する */
			r = segtop->frame;
			free(segtop);
			s->next = (SEGMENT*)NULL;
			segtop = s;
		} else {
			/* 全削除の場合 */
			nullrect(r);
			for (s = segroot; s != NULL; s = ss) {
				ss = s->next;
				orrect2(&r, &s->frame);
				free(s);
			}
			segroot = segtop = (SEGMENT*)NULL;
		}
		view(r);		/* 削除された領域を再表示する */
	}
	return	0;
}

/*
	線幅メニュー処理
		par =	選択した線幅
*/
EXPORT	W	cmd_lwidth(W par)
{
	l_width = par;			/* 線幅を変更する */
	return	0;
}

/*
	線色メニュー処理
		par = 選択した線色
*/
EXPORT	W	cmd_lcolor(W par)
{
	l_color = (COLOR)par;		/* 線色を変更する */
	return	0;
}

/*
	小物メニュー処理
		item = 選択した項目番号
*/
LOCAL	W	cmd_tool(W item)
{
	oexe_apg(0, item);		/* 小物を実行する */
	return	0;
}

/*
	メニュー処理関数テーブル：
		データボックスに定義したメニュー内部番号に対応した処理関数
*/
LOCAL	FUNCP	mfunc[] = {
	NULL,		/* 仮身／実身／ディスク操作用 : 未使用	*/
	cmd_tool,	/* 小物メニュー用			*/
	NULL,		/* 実行メニュー用 : 未使用		*/
	cmd_close,	/* 内部番号 0 用			*/
	cmd_redsp,	/* 内部番号 1 用			*/
	cmd_bgcol,	/* 内部番号 2 用			*/
	cmd_edit,	/* 内部番号 3 用			*/
	cmd_lwidth,	/* 内部番号 4 用			*/
	cmd_lcolor	/* 内部番号 5 用			*/
};

#define	M_EDIT		3	/* 編集メニューの内部番号 */
#define	M_LWIDTH	4	/* 線幅メニューの内部番号 */
#define	M_LCOLOR	5	/* 線色メニューの内部番号 */

/*
	メニューイベント処理(キーメニューの場合も key_fn() から呼ばれる)
*/
EXPORT	W	menu_fn(VOID)
{
	/* セグメントが 1 つもない場合は、編集メニューを不能状態とする */
	inactmenu((1 << M_EDIT), (segroot == NULL) ? M_INACT : M_ACT);

	/* 線幅メニューのインジケータを設定する */
	indmenu(M_LWIDTH,  l_width, 1);

	/* 線色メニューのインジケータを設定する */
	indmenu(M_LCOLOR,  l_color, 1);

	/* メニューの選択動作を行う */
	return selmenu(-1, mfunc);
}
