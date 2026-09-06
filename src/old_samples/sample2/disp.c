/*
	disp.C		電子手帳 : ウインドウの表示

	(C) Copyright 1997-98 by Personal Media Corporation
*/
#include "sched.h"


EXPORT	RLIST		view_rl[MAX_RLST];	/* 再表示用 RECTLIST */
LOCAL	BOOL		v_init = FALSE;

/*
 *	固定した文字列の表示(住所録のみ)
 */
LOCAL	VOID	view_str(W ix)
{
	W		i;
	STR_INFO	*p;

	if (ix == ADR_WIN) {		/* 住所録 */
	    for (i = 0; i < adr_str_num; i++) {
		p = &adr_str_info[i];
		gdra_stp(win[ix].gid, p->pos.x, p->pos.y, p->str, tc_strlen(p->str), G_STORE);
	    }
	} else if (ix == SRC_WIN) {	/* 検索パネル */
	    for (i = 0; i < src_str_num; i++) {
		p = &src_str_info[i];
		gdra_stp(win[ix].gid, p->pos.x, p->pos.y, p->str, tc_strlen(p->str), G_STORE);
	    }
	}
}

/*
 *	テキストボックスの下の点線を表示する
 */
LOCAL	VOID	view_under(W ix)
{
	TEXTBOX	*tbox;
	W	pnum;		/* テキストボックスの数 */
	W	ofs;		/* 先頭テキストボックスまでのオフセット */
	W	i;
	PNT	sp, ep;

	pnum = wind_def[ix][0] - wpinfo[ix].tbox_start;
	ofs = wpinfo[ix].tbox_start;
	for( i = 0; i < pnum; i++ ) {
		tbox = (TEXTBOX*)wpinfo[ix].parts[i + ofs];
		sp.x = tbox->r.c.left;
		ep.x = tbox->r.c.right - 1;
		sp.y = ep.y = tbox->r.c.bottom;
		gdra_lin(win[ix].gid, sp, ep, (LN_DOT<<8)+1, BLACK100, G_STORE);
	}
}

/*
 *	ページの多角形の外枠を表示する
 */
LOCAL	VOID	view_poly(W ix)
{
	POLY	*poly;
	W*	p;
	W	n, i;

	n = wind_def[ix][POLY_START];
	poly = (POLY*)malloc(sizeof(POLY) + sizeof(PNT) * (n-1));
	if (poly != NULL) {
		poly->round = 8;
		poly->size = n;
		p = &wind_def[ix][POLY_START+1];
		for (i = 0; i < n; i++) {
			poly->pt[i].x = *p++;
			poly->pt[i].y = *p++;
		}
		gfra_pol(win[ix].gid, poly, (LN_SOLID<<8)+1, BLACK100,G_STORE);
		free(poly);
	}
}

/*
 *	住所録のインデックスとページの表示
 */
LOCAL	VOID	view_adr(W mode)
/* 0：全て、1：インデックスの枠は表示しない */
{
	W	gid;
	RECT	vr, er;
	TC	page_str[7+1];
	TC	idx_ch;
	FSSPEC	font;

	gid = win[ADR_WIN].gid;
	er = vr = adr_index_rect;

	/* インデックス文字表示 */
	idx_ch = midashi_char(cur_adr->index);
	if (mode) {
		sizerect(&er, -1, -1);
		gfil_rec(gid, er, &WINDBG[ADR_WIN], 0, G_STORE);
	}
	gdra_chp(gid, vr.c.left + 6, vr.c.bottom - 6, idx_ch, G_STORE);
	if (mode==0) {
		/*gfra_rec(gid, vr, (LN_SOLID<<8)+1, &BLACK100, 0, G_STORE);*/
		shadowrect_frame(gid, vr, BLACK100, 1, 2);	/* 940212 */
	}

	/* ページ番号表示 */
	if (mode) {
		setrect(er, adr_page_disp.x, adr_page_disp.y - 13,
				adr_page_disp.x + 64, adr_page_disp.y + 1);
		gfil_rec(gid, er, &WINDBG[ADR_WIN], 0, G_STORE);
	}
	get_adr_page_str(page_str);
	font = c_font[ADR_WIN];
	font.size.h = font.size.v = 8;		/* ページ数は小さく表示 */
	gset_fon(gid, &font);
	gdra_stp(gid, adr_page_disp.x, adr_page_disp.y,
						 page_str, 7, G_STORE);
	gset_fon(gid, &c_font[ADR_WIN]);
}

/*
 *	予定表の表示(ページ共通部分も再表示する(年、月、日など))
 */
LOCAL	VOID	view_sch(W mode)
/* 0：全体、1：ページ切り替え時(可変部分) */
{
	W		i;
	TEXTBOX		*tbox;
	PNT		sp;
	RECT		vr;
	TC		str[14+1];
	FSSPEC	font;

	i = wpinfo[SCH_WIN].tbox_start;
	tbox = (TEXTBOX*)wpinfo[SCH_WIN].parts[i];
	sp.x = tbox->r.c.left + 8;
	sp.y = tbox->r.c.top - 8;

	if (mode) {
		vr.c.right = (vr.c.left = sp.x) + 13 * 14;
		vr.c.top = (vr.c.bottom = sp.y + 1) - 14;
		gfil_rec(win[SCH_WIN].gid, vr, &WINDBG[SCH_WIN], 0, G_STORE);
	}

	/* 日付を文字列に変換 */
	date_to_jstr(str, &selectdate);

	if ((i = is_holiday(&selectdate)) != 0 ) {
		/* 休日は太字に設定 */
		font = c_font[SCH_WIN];
		font.attr = FT_BOLD;
		gset_fon(win[SCH_WIN].gid, &font);
	}
	gdra_stp(win[SCH_WIN].gid, sp.x, sp.y, str, 14, G_STORE);
	if ( i ) {
		/* 元に戻す */
		gset_fon(win[SCH_WIN].gid, &c_font[SCH_WIN]);
	}
}

/*
 *	日付と週の枠の表示・消去
 */
EXPORT	VOID	view_2rect(W mode,DATE date,PNT sp,PNT size)
/* 1：表示、0：消去 */
{
	W	start, no, week;
	RECT	vr;
	PAT	*pat;

	pat = (mode) ? BLACK100 : &WINDBG[CAL_WIN];

	start = get_week1st(date);			/* 月の１日の曜日 */
	no = ((date.day + start - 1) / 7) + 1;		/* 第no週 */
	week = (date.day + start - 1) % 7;		/* 曜日 */

	/* 週の枠の表示 */
	vr.c.right = (vr.c.left = sp.x) + size.x * 7;
	vr.c.bottom = (vr.c.top = sp.y + size.y * no) + size.y;

	gfra_rec(win[CAL_WIN].gid, vr, (LN_SOLID<<8)+1, pat, 0, G_STORE);

	/* 日の枠の表示 */
	vr.c.right = (vr.c.left = sp.x + size.x * week) + size.x;
	vr.c.left++; vr.c.right--;
	vr.c.top++; vr.c.bottom--;

	gfra_rec(win[CAL_WIN].gid, vr, (LN_SOLID<<8)+1, pat, 0, G_STORE);
}

/*
 *	カレンダーの表示
 */
/*LOCAL*/
	VOID	view_cal(W mode)
/* 0：全て、1：曜日以外 */
{
	UW		*cal;
	FSSPEC	font;
	W		x, y;
	UW		c;
	W		gid;
	PNT		sp, size;
	RECT		vr;
	TC		*calstr;
	TC		datestr[14+1 + 20];

	sp = cal_lefttop;
	size = cal_daysize;
	gid = win[CAL_WIN].gid;

	/* 太字属性設定 */
	font = c_font[CAL_WIN];
	font.attr = FT_BOLD;

	/* カレンダー作成 */
	cal = make_calendar(&dispdate);

	/* 曜日表示 */
	if (mode == 0) {
		setrect(vr, sp.x, sp.y,
				 sp.x + size.x, sp.y + size.y);
		calstr = (TC*) getdbox (WEEK_STR);
		for (x = 0; x < 7; x++) {
			gdra_stp( gid, vr.c.left+4, vr.c.bottom-2,
						&calstr[x*2], 2, G_STORE );

			vr.c.left = vr.c.right;
			vr.c.right += size.x;
		}
	} else {
		setrect(vr, sp.x, sp.y + size.y,
			sp.x + size.x * 7, sp.y + size.y * 7);
		gfil_rec(gid, vr, &WINDBG[CAL_WIN], 0, G_STORE);

		vr = cal_year;
		gfil_rec(gid, vr, &WINDBG[CAL_WIN], 0, G_STORE);
	}

	/* 年月表示 */
	/* set_month1( datestr, dispdate.year, dispdate.month ); */
	date_to_kstr(datestr, &dispdate);
	gdra_stp(gid, cal_year.c.left, cal_year.c.bottom - 1,
							 datestr, 14, G_STORE);

	/* 日付表示 */
	setrect(vr, sp.x, sp.y + size.y,
			 sp.x + size.x, sp.y + size.y * 2);

	for ( y = 0; y < 6; y++ ) {
		for ( x = 0; x < 7; x++ ) {

			if ( (c = *cal++) == 0 ) {
				/* 空白 */
				datestr[0] = datestr[1] = TK_KSP;
				datestr[2] = TNULL;
			} else {
				/* 数字 */
				stoal(datestr, c & ~M_HOLIDAY, 2);
			}

			if ( (c & M_HOLIDAY) != 0 ) {
				/* 太字に設定 */
				gset_fon(win[CAL_WIN].gid, &font);
			}

			/* 描画 */
			gdra_stp( gid, vr.c.left+4, vr.c.bottom-2,
						datestr, 2, G_STORE );

			if ( (c & M_HOLIDAY) != 0 ) {
				/* 標準に戻す */
				gset_fon(win[CAL_WIN].gid, &c_font[CAL_WIN]);
			}

			vr.c.left = vr.c.right;
			vr.c.right += size.x;
		}

		vr.c.top = vr.c.bottom;
		vr.c.bottom += size.y;

		vr.c.left = sp.x;
		vr.c.right = sp.x + size.x;
	}

	/* 枠の表示 */
	if (IS_SAME_MONTH(todaydate, dispdate)) {
		view_2rect( 1, todaydate, sp, size );
	}
}

/*
 *	ウィンドウ毎の可変データを表示する
 */
/*LOCAL*/
	VOID	view_each(W ix,W mode)
/* 0：全体、1：ページ切り替え時(可変部分) */
{
	switch(ix) {
	case CAL_WIN:	view_cal(mode);	break;
	case SCH_WIN:	view_sch(mode);	break;
	case ADR_WIN:	view_adr(mode);	break;
	default:	break;
	}
}

/*
 *	パネルの表示
 */
EXPORT	VOID	view_etc(W ix,W mode)
/* 0：全体、1：ページ切り替え時(可変部分) */
{
	if (mode == 0) {
		view_str(ix);
		if (ix < MAX_MW) view_under(ix);
		if (ix < MAX_MW) view_poly(ix);
	}
	if (ix < MAX_MW) view_each(ix, mode);
}

/*
 *	テキストボックスの表示
 */
LOCAL	VOID	view_textbox(W ix,RECT *rp)
{
	W	i, num;

	num = wpinfo[ix].parts_num;
	for (i = wpinfo[ix].tbox_start; i < num; i++) {
		if (sectrect(*rp, ((TEXTBOX*)wpinfo[ix].parts[i])->r)) {
			cdsp_par( wpinfo[ix].pid[i], P_RDISP );
		}
	}
}

/*
 *	ウィンドウの表示
 */
EXPORT	VOID	view_window(W ix,RECT *rp,W mode)
/* 0：全体、1：ページ切り替え時(可変部分) */
{
	gset_vis( win[ix].gid, *rp );
	if (mode == 0) {
		wera_wnd( win[ix].id, rp);

		/* テキストボックスは、ページ切り替え時には、設定時に
		 * 表示する。(set_textbox)
		 * スイッチは、ENABLE, DISABLE の処理で表示する。
		 */
		view_textbox(ix, rp);

		if (ix == CAL_WIN || ix == SCH_WIN || ix == ADR_WIN) {
			win_dispsw(ix);		/* めくりボタン設定 */
		} else if (ix == SRC_WIN) {
			pnl_dispsw(ix, 0);	/* 検索パネルのスイッチ表示 */
		}

	}
	view_etc(ix, mode);
	gset_vis( win[ix].gid, visrect[ix] );
}

/*
 * 表示処理
 *
 *					  再表示の要否	new の値
 *	mode	＝ −１ ： 初期表示要求		要	NULL
 *		＝  ０	： 再表示要求イベント	要	NULL
 *		＝  １	： ドラッグ移動		要	NULL
 *		＝  ２	： ドラッグ変形		否	新作業領域
 *		＝  ３	： ドラッグ変形		要	新作業領域
 *		＝  ４	： 通常モード切換	否	新作業領域
 *		＝  ５	： 全面モード切換	要	新作業領域
 *		＝  ６	： 通常モード切換	要	新作業領域
 *		＝  ７	： パネル削除		要	NULL
 *		＝  ８	： 配置変更		否	新作業領域
 *		＝  ９	： 配置変更		要	新作業領域
 */
EXPORT	VOID	sc_dspfn(W ix,W mode,RECT* new)
/* 対象ウインドウの情報レコード番号 */
/* 再表示要因 */
{
	W	i;
	RECT	r;

	if ( ix < 0 || ix >= MAX_WIN) return; /* ウインドウなし */

	if ( mode < 0 ) {	/* 初期表示 */
		/* 初期化 */
		if (!v_init) {
			genrectlist(MAX_RLST, view_rl);
			v_init = TRUE;
		}
		/* 全体表示 */
		if (win[ix].id >= 0) {

			gget_vis( win[ix].gid, &visrect[ix] );
			set_textbox(ix, 0);
			view_window(ix, &visrect[ix], 0);

		}
		return;
	}

	if (win[ix].id >= 0) {
		do {
			if ( (i = wsta_dsp(win[ix].id, &r, view_rl)) == 0 )
								break;
			if ( i >= MAX_RLST ) view_window(ix, &r, 0);
			else while(i--) view_window(ix, &view_rl[i].rcomp, 0);
		} while ( wend_dsp(win[ix].id) > 0 );
	}
}

/*
 * 住所録ウインドウの再表示
 */
EXPORT	VOID	redisp_adr(W mode)
{
	set_curdata(ADR_WIN);
	set_textbox(ADR_WIN, 0);
	win_dispsw(ADR_WIN);
	view_window(ADR_WIN, &visrect[ADR_WIN], mode);

	/* 検索パネルのボタンを再表示 */
	if (win[SRC_WIN].id > 0) pnl_dispsw(SRC_WIN, 1);
}
