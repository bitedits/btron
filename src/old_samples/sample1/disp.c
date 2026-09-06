/*
	disp.c		サンプルプログラム : 表示処理

	(C) Copyright 1998 by Personal Media Corporation
*/
#include	"sample.h"

/* スクロールバーの設定値 */
typedef	struct	{
	W	clo, chi, lo, hi;
} SCRL_BAR;

LOCAL	SCRL_BAR	hbar = {0,0,0,0};	/* 下スクロールバーの設定値 */
LOCAL	SCRL_BAR	vbar = {0,0,0,0};	/* 右スクロールバーの設定値 */

/* 再表示用 RLIST */
#define	N_VIEWR		4
LOCAL	RLIST		view_rl[N_VIEWR];

/*
	スクロールバー表示の更新
*/
LOCAL	VOID	disp_sbar(VOID)
{
	W	lo, hi;

	if (window[0].wid <= 0) return;		/* ウィンドウは開いていない */

	/* このサンプルでは、スクロールバーの設定は、
	   全体領域 (frect) と作業領域 (vrect) の値そのものを使用する
	*/

	/* 右スクロールバー:
		hi  (上端)	: frect.c.top
		chi (上)	: vrect.c.top
		clo (下)	: vrect.c.bottom
		lo  (下端)	: frect.c.bottom
	*/
	hi = frect.c.bottom;
	if (hi < vrect.c.bottom) hi = vrect.c.bottom;

	/* 設定値に変更があれば、設定する */
	if (vbar.clo != vrect.c.top || vbar.chi != vrect.c.bottom ||
	    vbar.lo  != frect.c.top || vbar.hi != hi) {
		vbar.clo = vrect.c.top;
		vbar.chi = vrect.c.bottom;
		vbar.lo  = frect.c.top;
		vbar.hi  = hi;
		update_sbar(0, 0, (W*)&vbar);
	}

	/* 下スクロールバー:
		hi  (左端)	: frect.c.left
		chi (左)	: vrect.c.left
		clo (右)	: vrect.c.right
		lo  (右端)	: frect.c.right
	*/
	lo = frect.c.right;
	if (lo < vrect.c.right) lo = vrect.c.right;

	/* 設定値に変更があれば、設定する */
	if (hbar.clo != vrect.c.right || hbar.chi != vrect.c.left ||
	    hbar.lo  != lo  || hbar.hi != frect.c.left ) {
		hbar.clo = vrect.c.right;
		hbar.chi = vrect.c.left;
		hbar.lo  = lo;
		hbar.hi  = frect.c.left;
		update_sbar(0, 1, (W*)&hbar);
	}
}

/*
	ウィンドウの表示処理
*/
EXPORT	VOID	view(RECT r)
{
	SEGMENT	*s;

	/* 表示する領域のみをクリップ領域とする */
	gset_vis(mygid, r);

	/* まず、背景を背景色で塗りつぶす */
	gfil_rec(mygid, r, &windpat, 0, G_STORE);

	/* 再表示領域にかかっているセグメントを下から順に描画する */
	for (s = segroot; s != NULL; s = s->next) {
		if (sectrect(r, s->frame)) {
			/* 作業用パターンを使用して描画する */
			wkpat.spat.fgcol = s->lcolor;
			gdra_pln(mygid, (POLY*)&s->round, s->lwidth,
						&wkpat, G_STORE);
		}
	}

	/* クリップ領域を元に戻す */
	gset_vis(mygid, vrect);
}

/*
	再表示処理
*/
LOCAL VOID	redisplay(VOID)
{
	W	nr;
	RECT	r;

	do {
		/* 再表示が必要な領域を取り出す */
		nr  = wsta_dsp(mywid, &r, view_rl);
		if (nr <= 0) break;	/* 再表示領域なし */

		/* 再表示が必要な領域の再表示を行う */
		if (nr > N_VIEWR)	view(r);
		else	while (--nr >= 0) view(view_rl[nr].rcomp);
	} while (wend_dsp(mywid) > 0);
}

/*
	表示のスクロール処理
*/
LOCAL	VOID	scrl_disp(W dx, W dy)
{
	W	sts;

	if (dx != 0 || dy != 0) {
		/* 作業領域の内容をスクロールし再表示が必要な領域を設定する */
		sts = wscr_wnd(mywid, NULL, dx, dy, W_SCRL | W_RDSET);
		if (sts >= ER_OK) {
			/* frame 領域を新しい作業領域(vrect) とする */
			gget_fra(mygid, &vrect);
			gset_vis(mygid, vrect);

			/* 再表示する */
			if ((sts & W_RDSET) != 0) redisplay();
			else	view(vrect);	/* 全作業領域の再表示 */
		}
	}
}

/*
	スクロールイベント処理

		sts =	xxx..xxxxCCTTDD
			CC = 0: 右、1: 下、2: 左
			TT = 0: SMOOTH、1:AREA、2: JUMP
			DD = 0: UP、1: DOWN、2:LEFT、3:RIGHT
*/
EXPORT	VOID	scrl_fn(W ix, W sts, W diff)
{
	PNT	p;

	/* スクロール量を計算する:
		スムーススクロールの単位は 標準文字サイズ(CHSSTD) とする
	*/
	p = chk_scroll(sts, diff, CHSSTD, vrect, frect);

	/* スクロールする必要がある場合はスクロールする */
	if (p.x != 0 || p.y != 0) {
		scrl_disp(p.x, p.y);
		/* スクロールバー表示の更新 */
		disp_sbar();
	}
}

/*
	表示イベント処理

		mode =	-1: 初期表示要求
			0 : 再表示要求	(要再表示)
			1 : 移動	(要/否再表示)
			2 : サイズ変更
			3 : サイズ変更	(要再表示)
			4 : 通常モード
			5 : 全面モード	(要再表示)
			6 : 通常モード	(要再表示)
			7 : パネル削除	(要再表示)
			8 : 配置変更
			9 : 配置変更	(要再表示)
*/
EXPORT	VOID	disp_fn(W ix, W mode, RECT *new)
{
	if (mode < 0) {		/* 初期表示要求 */
		genrectlist(N_VIEWR, view_rl);	/* view_rl の初期化 */
		view(vrect);			/* 全表示領域を再表示する */
		disp_sbar();			/* スクロールバー表示 */

	} else {
		/* 作業領域の変更があった場合は、新しい作業領域を設定する */
		if (mode >= 2 && mode != 7) {
			vrect = *new;			/* 新 vrect */
			gset_vis(mygid, vrect);		/* 新 vrect の設定 */
		}
		/* 再表示処理を行う */
		redisplay();

		/* 作業領域の変更があった場合は、スクロールバーを更新する */
		if (mode >= 2 && mode != 7) {
			/* 左/上側拡大の時、作業領域が負になる場合は、
			   スクロールして作業領域が負にならないようにする
			*/
			if (vrect.c.left < 0 || vrect.c.top < 0) {
				scrl_disp(vrect.c.left, vrect.c.top);
			}
			/* スクロールバー表示の更新 */
			disp_sbar();
		}
	}
}
