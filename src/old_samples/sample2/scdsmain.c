/*
	scdsmain.c	電子手帳 : 開いた仮身の表示要求ハンドラ

	(C) Copyright 1997-98 by Personal Media Corporation
*/
#include    "sched.h"

EXPORT	W	disp_vobj = 0;

/*
 *
 */
LOCAL	W	set_page_data(W ix)
{
	BOOL	ret;
	W	er;

	switch(ix) {
	case CAL_WIN:
		dispdate.year  = sched_fusen.winfo[CAL_WIN].page[0];
		dispdate.month = sched_fusen.winfo[CAL_WIN].page[1];
		if ( dispdate.year < FIRST_YEAR || dispdate.year > LAST_YEAR
		  || dispdate.month < 1 || dispdate.month > 12) {
			dispdate = todaydate;
		}
		break;
	case SCH_WIN:
		selectdate.year  = sched_fusen.winfo[SCH_WIN].page[0];
		selectdate.month = sched_fusen.winfo[SCH_WIN].page[1];
		selectdate.day	 = sched_fusen.winfo[SCH_WIN].page[2];
		if ( selectdate.year < FIRST_YEAR
		  || selectdate.year > LAST_YEAR
		  || selectdate.month < 1 || selectdate.month > 12) {
			selectdate = todaydate;
		}
		if ((er = change_date(selectdate)) < E_OK) return er;
		break;
	case ADR_WIN:
		ret = set_cur_adr_page( sched_fusen.winfo[ADR_WIN].page[0],
					sched_fusen.winfo[ADR_WIN].page[1] );
		if ( !ret ) cur_adr = adr_data;
		break;
	default:
		return -1;
		break;
	}

	return E_OK;
}

/*
 * 開いた仮身の表示
 *	(パーツは使えないのでテキストボックスの内容は、ここで描画する)
 */
LOCAL	VOID	disp_vobj_window(W ix,RECT *vr)
/* ウィンドウ番号 */
/* 表示範囲 */
{
	W	pno, ofs;
	W	gid, x, y, i;
	TEXTBOX	*tbox;

	gid = win[ix].gid;
	gset_vis(gid, *vr);

	/* テキストボックスの文字列表示 */
	pno = wind_def[ix][0] - wpinfo[ix].tbox_start;
	ofs = wpinfo[ix].tbox_start;
	for ( i = 0; i < pno; i++ ) {
		tbox = (TEXTBOX*)wpinfo[ix].parts[i + ofs];
		x = tbox->r.c.left + 4;
		y = tbox->r.c.bottom - 4;
		gdra_stp(gid, x, y, tbufp[ix][i], MAX_TBLEN, G_STORE);
	}

	/* 開いた仮身ではめくりボタンは表示しない */

	/* その他の部分の表示 */
	view_etc(ix, 0);
}

/*
 * 開いた仮身の表示処理
 */
EXPORT	VOID	sc_dsproc(W wid, W gid, W vid, COLOR bgcol)
{
	RECT	viewr;
	W	ix, er = 0;

	if (wid >= 0) {
		ix = sched_fusen.win_top;
		win[ix].id = wid;
		win[ix].gid = gid;
		gget_fra(gid, &viewr);
		gset_vis(gid, viewr);
		moverect(&viewr, -viewr.c.left, -viewr.c.top);

		WINDBG[ix].spat.fgcol = bgcol;
		WINDBG[ix].spat.mask  = FILL100;
		WINDBG[ix].spat.bgcol = COL_TRANS;
		gfil_rec(gid, viewr, &WINDBG[ix], 0, G_STORE);	/* 背景	*/

		init_ch_env(gid, ix);

		set_curdata(ix);

	/*	vdm_opt = V_DISPALL|V_NOFRAME;*//*仮身の外枠は点線で表示*/

		disp_vobj_window(ix, &viewr);		/* 開いた仮身表示 */

		odel_vob(-wid, 0);			/* 仮身の削除 */
		wcls_wnd (wid, NOCLR);
		if (er >= 0) wid = 0;
	}
	oend_req(vid, wid);			/* 終了通知 */
}

/*
 * 開いた仮身の表示起動
 */
EXPORT	W	sc_dsmain(M_DISPREQ *disp_msg)
{
	W	er, ix;
	W	wid, gid;
	H*	p;
	RECT	vrect;

	disp_vobj = 1;

	/* ファイルのオープン */
	if ( ( wid = er = wopn_iwd( gid = disp_msg->gid ) ) >= 0
	  && ( er = open_file( &disp_msg->lnk, &tg_sts) ) >= 0 ) {

		/* 付箋データの読み込み */
		if( (p = (H*)disp_msg->info) &&
		    (*p == sizeof(xSCHED_FUSEN)-sizeof(H)) ) {
			swab_fusen(&sched_fusen, (xSCHED_FUSEN*)p, 1);
		}
		ix = sched_fusen.win_top;

		vrect = sched_fusen.winfo[ix].w_rect;
		vrect.c.right -= 20;
		moverect(&vrect, -vrect.c.left, -vrect.c.top);
		visrect[ix] = vrect;

		/* データの読み込み */
		er = sc_load();
		rclose(tg_fp);

		if (er >= 0) {
			set_wind_def(ix);
			set_parts_data(ix);
			er = set_page_data(ix);
		}
	}
	if (er < 0) {
		/* sc_dsproc()では、wid >= 0 の時だけクローズする */
		if (wid >= 0) wcls_wnd(wid, NOCLR);
		wid = er;	/* oend_req() を sc_dsproc() で行うため */
	}
	/* 表示 */
	sc_dsproc(wid, gid, disp_msg->vid, disp_msg->bgcol);

	/* @@クリーンアップ? */

	disp_vobj = 0;

	return	er;
}
