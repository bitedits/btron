/*
	press.c		サンプルプログラム : PD プレス処理

	(C) Copyright 1998 by Personal Media Corporation
*/
#include	"sample.h"

/*
	PD プレスイベント処理
*/
EXPORT	W	pres_fn(W ix)
{
	W		n;
	W		dgid;			/* ドラッグ用 GID */
	W		dx, dy;
	Bool		outwind;
	PNT		sp, ep;			/* 開始点 / 終了点 */
	RECT		r;
	SEGMENT		*seg;			/* セグメントポインタ */
	PTRIMG		pimg;			/* ポインタイメージ */
	PTRSTS		psts;			/* ポインタ状態 */

	/* 一般に PD プレス処理としては、
	   選択処理、ドラッグによる移動/複写/変形/図形生成、パーツの実行
	   ダブルクリックによる仮身の実行、などの、プレス位置、プレス方法、
	   内部状態等に応じた各種の処理を行う必要があるが、
	   このサンプルでは、単純化のためドラッグによる図形生成のみとする
	*/

	/* 新しく生成するセグメントの領域を獲得する */
	seg = (SEGMENT*)malloc((sizeof(SEGMENT) + MAX_PT * sizeof(PNT)));
	if (seg == NULL) {
		panel(EP_MEMORY);		/* メモリ不足のエラー */
		return 0;
	}

	/* ドラッグ用描画環境を生成する */
	dgid = wsta_drg(mywid, 0);
	if (dgid < ER_OK) {
		free(seg);			/* セグメント領域の解放 */
		panel(EP_MEMORY);		/* メモリ不足のエラー */
		return 0;
	}

	seg->round = 0;				/* 角丸は常に 0 とする */
	seg->lwidth = l_width;			/* 選択されている線幅の設定 */
	seg->lcolor = l_color;			/* 選択されている線色の設定 */
	seg->np = 0;
	sp = wevt.e.pos;			/* プレス開始位置 */
	seg->pt[(seg->np)++] = sp;		/* 開始点 */

	gget_vis(dgid, &r);			/* 作業領域 */

	gget_ptr(&psts, &pimg);			/* ポインタ形状を保存 */
	outwind = False;

	/* ドラッグ処理開始 */
	while (wget_drg(&ep, &wevt) != EV_BUTUP) {

		/* 変位 > 1 の時のみ有効とする */
		dy = ep.y - sp.y;
		dx = ep.x - sp.x;
		if ((dy <= 1 && dy >= -1) && (dx <= 1 && dx >= -1)) continue;

		if (inrect(r, ep)) {	/* 作業領域内 */
			if (outwind == True) {
				/* ポインタ形状を元に戻す */
				setpointer(psts.style, &pimg);
				outwind = False;
			}

			/* 最大点数を超えた場合は無視する */
			if (seg->np < MAX_PT) {		/* 描画する */
				wkpat.spat.fgcol = l_color;
				gdra_lin(mygid, sp, ep, l_width,
							&wkpat, G_STORE);
				seg->pt[(seg->np)++] = ep;
			}
			sp = ep;
		} else {	/* 作業領域からはみ出た場合は無視して
					ポインタ形状を選択指にする */
			outwind = True;
			setpointer(PS_SELECT, NULL);
		}
	}

	/* ドラッグ処理終了 */
	wend_drg();

	if (seg->np <= 1) {	/* 1 点の場合は無視 */
		free(seg);

	} else {		/* 2 点以上の場合はセグメントを完成する*/
		/* 線列全体を囲む領域を計算する */
		sp = seg->pt[0];
		r.c.left = r.c.top = sp.x;
		r.c.top = r.c.bottom = sp.y;
		for (n = 0; n < seg->np; n++) {
			sp = seg->pt[n];
			if (sp.x < r.c.left) r.c.left = sp.x;
			else if (sp.x > r.c.right) r.c.right = sp.x;
			if (sp.y < r.c.top) r.c.top = sp.y;
			else if (sp.y > r.c.bottom) r.c.bottom = sp.y;
		}
		/* 線幅分だけ、領域の右下を拡げる */
		r.c.bottom += l_width;
		r.c.right += l_width;
		seg->frame = r;

		/* 余分な領域を解放する :
				サイズ縮小のため、エラーは発生しない前提 */
		seg = (SEGMENT*)realloc(seg, (sizeof(SEGMENT) +
						seg->np * sizeof(PNT)));

		/* 生成したセグメントをリストに追加する */
		seg->next = NULL;
		if (segtop != NULL)	segtop->next = seg;
		else			segroot = seg;
		segtop = seg;
	}
	return 0;
}
