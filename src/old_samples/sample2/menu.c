/*
	menu.c		電子手帳 : メニュー処理

	(C) Copyright 1997-98 by Personal Media Corporation
*/
#include "sched.h"

/* 実行 */
LOCAL	W	exec_prog(int vid,int item)
{
	setpointer(PS_BUSY, NULL);
	if (oexe_apg (vid, item) > 0) setpointer(0x8001, NULL);
					/* ポインタロック */
	return 0;
}

/*
 * 小物
 */
LOCAL	W	mn_tool(W para)
{
	return exec_prog( 0, para );
}

/*
 * 終了
 */
LOCAL	W	mn_exit(W para)
/* 常に０ */
{
	if ( sc_exit(0) ) {
		return 1; /* 付箋を更新して終了 */
	} else {
		return 0; /* 終了拒否 */
	}
}

/*
 * 保存
 *	param = 0 : 新しい実身に保存
 *		1 : 元の実身に保存
 */
LOCAL	W	mn_save(W para)
{
	save_file( ( para == 0 )? 0x01: 0x0e );
	return 0;
}

/*
 * 主ウィンドウのオープン
 */
EXPORT	W	main_win_open(W ix)
{
	W	err;

	if (win[SRC_WIN].id >= 0) {
		set_window_rect(SRC_WIN);
		close_win(SRC_WIN, 0);
	}
	if ((err = (*main_open[ix])(NULL, 1)) >= E_OK) {
		in_act_parts = sched_fusen.win_top = ix;
	}
	return err;
}

/*
 * 表示切替
 *	param = 1 : カレンダーを表示
 *		2 : 予定表を表示
 *		4 : 住所録を表示
 */
LOCAL	W	mn_view(W para)
{
	W	sw = sched_fusen.win_sw;
	W	err = E_OK;

	/* 全ての主ウインドウを閉じることは不可 */
	if ( DISP_MAIN(sw ^= para) == 0 ) return 0;

	switch ( para ) {
	  case BIT_MASK(CAL_WIN): /* カレンダ・ウインドウ */
		if ( (sw & BIT_MASK(CAL_WIN)) != 0 ) {
			err = main_win_open(CAL_WIN);
		} else {
			close_win(CAL_WIN, 0);
		}
		break;
	  case BIT_MASK(SCH_WIN): /* 予定表ウインドウ */
		if ( (sw & BIT_MASK(SCH_WIN)) != 0 ) {
			err = main_win_open(SCH_WIN);
		} else {
			close_win(SCH_WIN, 0);
		}
		break;
	  case BIT_MASK(ADR_WIN): /* 住所録ウインドウ */
		if ( (sw & BIT_MASK(ADR_WIN)) != 0 ) {
			err = main_win_open(ADR_WIN);
		} else {
			close_win(ADR_WIN, 0);
		}
		break;
	  case BIT_MASK(SRC_WIN): /* 検索ウインドウ */
		if ( (sw & BIT_MASK(SRC_WIN)) != 0 ) {
			if ((err = src_open()) >= E_OK) in_act_parts = SRC_WIN;
		} else {
			close_win(SRC_WIN, 0);
			in_act_parts = sched_fusen.win_top;
		}
		break;
	}
	if ((para != BIT_MASK (SRC_WIN))
	    && sw & BIT_MASK(SRC_WIN)) {
		if (src_open() < E_OK) {
			sw ^= BIT_MASK(SRC_WIN);
		}
	}
	if (err < E_OK) {
		errpanel (ER_WIND_NOE, err);
	} else {
		sched_fusen.win_sw = sw;
	}
	return 0;
}

/*
 * トレーへ複写・移動
 *	param = 0 : トレーへ複写
 *		1 : トレーへ移動
 */
LOCAL	W	mn_cut(W para)
{
	W	ix, er;

	if ((ix = in_act_parts) < 0 || ix == CAL_WIN) return 0;

	if ((er = text_to_tray(ix, para)) < E_OK) errpanel(ER_CUT, er);

	return 0;
}

/*
 * トレーから複写・移動
 *	param = 0 : トレーから複写
 *		1 : トレーから移動
 */
LOCAL	W	mn_paste(W para)
{
	W	ix, er;

	if ((ix = in_act_parts) < 0 || ix == CAL_WIN) return 0;

	if ((er = text_from_tray(ix, 0, (PNT){0x8000, 0x8000})) <= E_OK) {
		/* er==0 の場合は、文字データなし */
		errpanel(ER_PASTE, (er == 0) ? -10001 : er);
	} else {
		/* if (nosupport) panel(PNL_NOSPTT); */
		if (para) tdel_dat();		/* トレー内の削除 */
	}

	return 0;
}

/*
 * 削除
 */
LOCAL	W	mn_delete(W para)
/* 常に０ */
{
	W	ix, pno;
	W	pid;
	TC	buf[TBUFLEN];

	if ((ix = in_act_parts) < 0 || ix == CAL_WIN) return 0;

	pid = wpinfo[ix].pid[ (pno = wpinfo[ix].tbox_no) ];
	if ( tx_cut_txt(pid, TBUFLEN, buf, 1) > 0) {

		/* 削除が行われた。 */
		/* テキストボックスの内容の読みだし */
		pno -= wpinfo[ix].tbox_start;
		cget_val(pid, maxtxsz[ix][pno] + 1, (W*)tbufp[ix][pno]);

		/* 変更後の処理 */
		if (ix != SRC_WIN) {
			modified = TRUE;
		}
		if (ix == ADR_WIN && pno == 0) {
			chg_adr_yomi();
		}
	}

	return 0;
}

/*
 * カレンダーのページ切替
 */
LOCAL	W	cal_page(W para)
{
	if ( (para <= 1 && IS_FIRST_MONTH(dispdate))
	  || (para > 1 && IS_LAST_MONTH(dispdate))  ) {
		return 0;
	}

	switch (para) {
	case 0:		/* 最初のページ */
		dispdate.year = FIRST_YEAR;
		dispdate.month = 1;
		break;
	case 1:		/* 前のページ */
		if (--dispdate.month < 1) {
			dispdate.year--;
			dispdate.month = 12;
		}
		break;
	case 2:		/* 次のページ */
		if (++dispdate.month > 12) {
			dispdate.year++;
			dispdate.month = 1;
		}
		break;
	case 3:		/* 最後のページ */
		dispdate.year = LAST_YEAR;
		dispdate.month = 12;
		break;
	}
	win_dispsw(CAL_WIN);
	view_window(CAL_WIN, &visrect[CAL_WIN], 1);
	return 0;
}

/*
 * 予定表のページ切替
 */
LOCAL	W	sch_page(W para)
{
	DATE		date, prevdate;
	SCH_NODE	*tmpp;
	W		i;

	date = prevdate = selectdate;

	if ( (para <= 1 && IS_FIRST_DATE(date))
	  || (para > 1 && IS_LAST_DATE(date))  ) {
		return 0;
	}

	switch (para) {
	case 0:		/* 最初のページ */
		date.year = FIRST_YEAR;
		date.month = 1;
		date.day = 1;
		break;
	case 1:		/* 前のページ */
		tmpp = cur_sch->prev;
		if ( cur_sch != sch_data	/* 最初のノードではない */
		  && tmpp->year == cur_sch->year
		  && tmpp->month == cur_sch->month
		  && tmpp->day == cur_sch->day ) {
			/* 前のノードも同じ日付の場合は、ノードだけ変更する */
			/* @@前の日付の２番目以降のノードの場合は？ */
			cur_sch = tmpp;
			set_curdata(SCH_WIN);
			selectdate = date;
			goto DISP;
		}
		if (--date.day < 1) {
			if (--date.month < 1) {
				date.year--;
				date.month = 12;
			}
			date.day = month_day(&date)/*get_maxdate(date)*/;
		}
		break;
	case 2:		/* 次のページ */
		tmpp = cur_sch->next;
		if ( tmpp != sch_data		/* 最後のノードではない */
		  && tmpp->year == cur_sch->year
		  && tmpp->month == cur_sch->month
		  && tmpp->day == cur_sch->day ) {
			/* 次のノードも同じ日付の場合は、ノードだけ変更する */
			cur_sch = tmpp;
			set_curdata(SCH_WIN);
			selectdate = date;
			goto DISP;
		}
		if (++date.day > month_day(&date)/*get_maxdate(date)*/) {
			if (++date.month > 12) {
				date.year++;
				date.month = 1;
			}
			date.day = 1;
		}
		break;
	case 3:		/* 最後のページ */
		date.year = LAST_YEAR;
		date.month = 12;
		date.day = 31;
		break;
	}

	if ((i = change_date(date)) < E_OK) {
		panel(ER_MEMORY);
		return 0;
	}
DISP:
	redisp_date(prevdate, date);

	/* if (win[SRC_WIN].id > 0) pnl_dispsw(SRC_WIN, 1); */

	return 0;
}

/*
 * 住所録のページ切替
 */
LOCAL	W	adr_page(W para)
{
	BOOL	chg;

	/* ページ切替 */
	switch ( para ) {
	  case 0:	/* 最初のページ */
		chg = first_adr_page();
		break;
	  case 1:	/* 前のページ */
		chg = prev_adr_page();
		break;
	  case 2:	/* 次のページ */
		chg = next_adr_page();
		break;
	  case 3:	/* 最後のページ */
		chg = last_adr_page();
		break;
	  default:
		chg = FALSE;
	}
	if ( chg ) {
		/* ページが切り替わったので、再表示 */
		redisp_adr(1);
	}

	return 0;
}

/*
 * 検索パネルの場合
 *	メニューからは呼ばれないが、パーツの動作(pres_parts)で呼ばれる。
 */
LOCAL	W	search_text(W para)
{
	SCH_NODE	*sp = cur_sch;	/* 現在表示中のノード(予定表) */
	DATE		date;
	W		find;

	/* 検索 */
	setpointer(PS_BUSY, NULL); /* 湯のみ */
	find = sc_search(sched_fusen.win_top, srcbuf, (para <= 1)? 0:1);
	setpointer(PS_SELECT, NULL); /* 選択指 */

	if ( find >= 0 ) {
		/* 検索終了：見つかった */

		switch ( sched_fusen.win_top ) {
		  case SCH_WIN:	/* 予定表 */
			/* 検索前に表示していたノードが空なら削除 */
			if ( is_empty_sch_node(sp) ) delete_sch_node(sp);

			/* 再表示 */
			date.year  = cur_sch->year;
			date.month = cur_sch->month;
			date.day   = cur_sch->day;
			set_curdata(SCH_WIN);
			redisp_date(selectdate, date);
			selectdate = date;
			goto SETWIN;
			break;

		  case ADR_WIN: /* 住所録 */
			/* 再表示 */
			redisp_adr(1);
SETWIN:
			in_act_parts = sched_fusen.win_top;
			wpinfo[in_act_parts].tbox_no = find + wpinfo[in_act_parts].tbox_start;
			break;
		}
	} else {
		/* 見つからなかった */
		panel(ER_SRCH);
	}

	return 0;
}

EXPORT	FUNCP	cmd_page[MAX_WIN]/* = {cal_page, sch_page, adr_page, search_text}*/;


/*
 * ページ切替
 *	param = 0 : 最初のページ
 *		1 : 前のページ
 *		2 : 次のページ
 *		3 : 最後のページ
 */
LOCAL	W	mn_page(W para)
{
	return (*cmd_page[sched_fusen.win_top])(para);
}

/*
 * ページを追加(住所録のみ)
 */
LOCAL	W	mn_newadr(W para)
/* 常に０ */
{
	W		err;

	if ( sched_fusen.win_top == ADR_WIN ) {
		/* 住所録の追加 */
		if ( (err = set_new_adr()) >= E_OK ) {
			/* 再表示 */
			redisp_adr(1);
		} else {
			/* エラーパネル表示 */
			errpanel(ER_REGIST, err);
		}
	}

	return 0;
}

/*
 * ページを削除(住所録のみ)
 */
LOCAL	W	mn_delpage(W para)
/* 常に０ */
{
	if (sched_fusen.win_top == ADR_WIN) {
		/* 削除の確認 */
		if ( panel(PNL_DELADR) == 1 ) {
			/* 住所録の削除 */
			delete_adr_data();
			/* 再表示 */
			redisp_adr(1);
		}
	}
	return 0;
}

/*
 * 空欄にする(予定表のみ)
 */
LOCAL	W	mn_clrpage(W para)
/* 常に０ */
{
	if ( sched_fusen.win_top == SCH_WIN ) {
		/* 削除の確認 */
		if ( panel(PNL_CLRSCH) == 1 ) {
			/* 予定表：カレントデータをクリア */
			if ( !is_empty_sch_node(cur_sch) && clear_sch() ) {
				/* 再表示 */
				set_textbox(SCH_WIN, 0);
				modified = TRUE; /* 編集された */
			}
		}
	}
	return 0;
}

/*
 * 用紙設定
 */
LOCAL	W	mn_prform(W para)
/* 常に０ */
{
	if (changeform(0)) {
		modified = TRUE;
	}
	return 0;
}

/*
 * 印刷
 */
LOCAL	W	mn_print(W para)
/* 常に０ */
{
	return sc_print(0);
}

/*
 * メニュー処理関数群
 *	(内部番号で関数が選択される)
 */
LOCAL	FUNCP	menu_func[] = {
	NULL,		/*     仮身操作 */
	NULL, /*mn_tool,*/	/*     小物 */
	NULL,		/*     実行 */
	NULL, /*mn_exit,*/	/*  0: 終了 */
	NULL, /*mn_save,*/	/*  1: 保存 */
	NULL, /*mn_view,*/	/*  2: 表示切替 */
	NULL, /*mn_cut,	*/	/*  3: トレーへ複写・移動 */
	NULL, /*mn_paste,*/	/*  4: トレーから複写・移動 */
	NULL, /*mn_delete,*/	/*  5: 削除 */
	NULL, /*mn_page,*/	/*  6: ページ切替(先頭へ、前へ) */
	NULL, /*mn_page,*/	/*  7: ページ切替(最後へ、次へ) */
	NULL, /*mn_newadr,*/	/*  8: ページ追加 */
	NULL, /*mn_delpage,*/	/*  9: ページ削除 */
	NULL, /*mn_clrpage,*/	/* 10: 空欄にする */
	NULL, /*mn_prform,*/	/* 11: 用紙設定 */
	NULL, /*mn_print*/	/* 12: 印刷 */
};

/* 内部項目番号定義 */
#define	MN_EXIT		0	/* 終了 */
#define	MN_SAVE		1	/* [保存] */
#define	MN_VIEW		2	/* [表示] */
#define	MN_CUT		3	/* [編集]→[トレーへ複写][トレーへ移動] */
#define	MN_PASTE	4	/* [編集]→[トレーから複写][トレーから移動] */
#define	MN_DEL		5	/* [編集]→[削除] */
#define	MN_PAGEB	6	/* [ページ]→[先頭のページ][前のページ] */
#define	MN_PAGEF	7	/* [ページ]→[次のページ][最後のページ] */
#define	MN_PADD		8	/* [ページ]→[ページを追加] */
#define	MN_PDEL		9	/* [ページ]→[ページを削除] */
#define	MN_PCLR		10	/* [ページ]→[空欄にする] */
#define	MN_FORM		11	/* [印刷]→[用紙設定] */
#define	MN_PRINT	12	/* [印刷]→[印刷] */

/* 不能項目マスク定義 */
#define	BM(n)	((UW)(1L<<n))

/*
 * 不能項目の設定
 */
LOCAL	UW	set_inact(void)
{
	UW	inact;
	W	ix, pid;
	BOOL	disable[2];
	TC	buf[TBUFLEN];

	switch (sched_fusen.win_top) {
	case CAL_WIN:
		/* 印刷・ページ追加・削除・空欄に */
		inact = BM(MN_PRINT)|BM(MN_PADD)|BM(MN_PDEL)|BM(MN_PCLR);
		break;
	case SCH_WIN:
		/* ページ追加・削除 */
		inact = BM(MN_PADD)|BM(MN_PDEL);
		break;
	case ADR_WIN: default:
		/* 空欄に */
		inact = BM(MN_PCLR);
		break;
	}

	/* ページめくり */
	chk_page_change(sched_fusen.win_top, disable);
	if (disable[0]) inact |= BM(MN_PAGEB);
	if (disable[1]) inact |= BM(MN_PAGEF);

	/* トレー関係・削除 */
	if ((ix = in_act_parts) < 0 || ix == CAL_WIN) {

		inact |= ( BM(MN_CUT) | BM(MN_PASTE) | BM(MN_DEL) );

	} else {
		/* トレーからの移動・複写 */
		if (tget_sts((UW*)NULL, (UW*)NULL) <= 0) {
			/* トレーが空 */
			inact |= BM(MN_PASTE);
		}

		/* トレーへの移動・複写、削除 */
		pid = wpinfo[ix].pid[ wpinfo[ix].tbox_no ];
		if ( tx_cut_txt(pid, TBUFLEN, buf, 0) <= 0) {

			inact |= ( BM(MN_CUT) | BM(MN_DEL) );
		}
	}

	return inact;
}

/*
 * メインメニュー処理
 */
EXPORT	W	sc_menufn(void)
{
	W		ret;

	/* [表示]のインジケータ設定 */
	indmenu(MN_VIEW, sched_fusen.win_sw, 2);	/* 表示切替 */

	/* 不能項目設定 */
	inactmenu(set_inact(), 0);

	/* メニューハンドリング */
	ret = selmenu(-1, menu_func);

	return ret;
}

EXPORT	VOID	init_menu(void)
{

	cmd_page[0] = cal_page;
	cmd_page[1] = sch_page;
	cmd_page[2] = adr_page;
	cmd_page[3] = search_text;

	menu_func[1] = mn_tool;
	menu_func[3] = mn_exit;
	menu_func[4] = mn_save;
	menu_func[5] = mn_view;
	menu_func[6] = mn_cut;
	menu_func[7] = mn_paste;
	menu_func[8] = mn_delete;
	menu_func[9] = mn_page;
	menu_func[10] = mn_page;
	menu_func[11] = mn_newadr;
	menu_func[12] = mn_delpage;
	menu_func[13] = mn_clrpage;
	menu_func[14] = mn_prform;
	menu_func[15] = mn_print;

	return;
}
