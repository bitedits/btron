/*=============================================================================

	tsdisp.c : 見出しパネルサンプル 表示・描画処理

	(C) Copyright 1999 by Personal Media Corporation

=============================================================================*/
#include	"tagsamp.h"
#include	"prototype.h"

/*=========================================================外部宣言の組み込み*/
IMPORT	TC	pnl_tbl[MAX_SUBPNL];

/*=================================================================再表示処理*/
LOCAL	VOID	redisp(RECT *rp, Bool sw)
{

	dsp_tagpnl(tagpnl, rp);		/* 見出しパネルの再表示		*/

	gset_vis(mygid, *rp);		/* 表示領域の設定・初期化	*/

	/* その他の内容（文字など）を表示するのなら、ここで表示したりする*/

	gset_vis(mygid, vrect);		/* 表示領域の復帰		*/

}

/*=========================================================見出しパネルの処理*/
EXPORT	ERR	init_subpnl(RECT *subrp)	/* 見出しパネルの初期化	*/
{
	W	rv;

						/* 見出しパネルの生成	*/
	rv = cre_tagpnl(mywid, MAX_SUBPNL, (TC *)pnl_tbl, &vrect, subrp,
						wbgpat, pbgpat, &tagpnl);

	return rv;
}

EXPORT	VOID	switch_subpnl(W pnum)		/* 見出しの切り替え	*/
{
	RECT	r;
	W	i;

	i = sw_tagpnl(tagpnl, pnum, &r);
	if (i > 0) redisp(&r, True);
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
