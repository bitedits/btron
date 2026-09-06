/*
	evtetc.c	電子手帳 : 各種イベント処理関数

	(C) Copyright 1997-98 by Personal Media Corporation
*/
#include "sched.h"
#include <mtstring.h>

/*
 * アイドル処理
 */
EXPORT	VOID	sc_idlefn(void)
{
	W	ix;

	/* 動作する部分がパーツのみなので、実行直後や枠のクリックによる
	** ウィンドウの切り替え後のパーツの動作を行うために、
	** pres_parts() を呼ぶ。
	*/

	if ((ix = in_act_parts) >= 0 && ix != CAL_WIN) {
		if (pres_parts(ix, wpinfo[ix].pid[ wpinfo[ix].tbox_no ]) != 0) {
			if (sc_exit (0) == TRUE) {
				ext_prc (0);
			}
		}
	} else {
		if (  wevt.s.cmd == W_WORK
		  && (wevt.s.stat & (ES_CMD|ES_BUT2)) == 0 ) {
			setpointer(0x4000|PS_SELECT, NULL);	/* 選択指 */
		}
	}
}

/*
 * 一般メッセージ処理
 */
EXPORT	W	sc_msgfn( MESSAGE *msg)
/* メッセージの先頭４バイト */
{
	/* ポインターのビジーロック解除
	 *	ウインドウを開かずに異常終了してしまった場合の対処
	 */
	setpointer(0x8000, NULL);

	/* 先頭のメッセージをクリア (読み飛ばす) */
	clr_msg(MM_ALL, MM_ALL);

	return E_OK;
}

/*
 * ウィンドウをアクティブに。
 *	従属ウィンドウをアクティブウィンドウからオープンしなおす。
 */
LOCAL	VOID	set_active_win(W ix, W sts)
{
	cdsp_pwd(winfo[ix].wid, NULL, P_DISP);

	win_dispsw(ix);		/* ページめくりボタン設定 */

	if (sts != 0) {

		if (sched_fusen.win_top != ix) {
			if (win[SRC_WIN].id >= 0) {
				set_window_rect(SRC_WIN);
				close_win(SRC_WIN, 0);
			}
			sched_fusen.win_top = ix;
			if ( (sched_fusen.win_sw & BIT_MASK(SRC_WIN)) ) {
				if (src_open() < E_OK)
					sched_fusen.win_sw ^=
							BIT_MASK(SRC_WIN);
			}
		}
		in_act_parts = ix;
	}
}

/*
 * 状態変化処理
 *
 *	sts
 *	= 0x0000：EV_INACT が発生 (他ウィンドウへの切り換え)
 *	= 0x0001：EV_SWITCH または EV_RSWITCH が発生
 *	= 0x0002：W_SWITCH が発生 (他ウィンドウからの復帰)
 *	= 0x0100：EV_INACT が発生 (パネルへの切り換え)
 *	= 0x0102：W_SWITCH が発生 (パネルからの復帰)
 *	< 0x0000：W_CLOSED が発生 (クローズした wid  は -sts となる)
 */
EXPORT	VOID	sc_stsfn( W ix, W sts)
/* ウインドウ情報レコード番号 */
/* 状態変更要因 */
{
	/* ポインターのビジーロック解除 */
	setpointer(0x8000, NULL);

	switch ( sts ) {
	  case 0x0000: /* 入力受付状態が他のウインドウに移った */
	  case 0x0001: /* 自ウインドウが入力受付状態になった */
	  case 0x0002: /* プログラムにより入力受付状態になった */
		set_active_win(ix, sts);
		break;
	  case 0x0100: /* 入力受付状態がパネルに移った */
	  case 0x0102: /* 入力受付状態がパネルから復帰した */
		break;
	  default:
		if ( sts < 0x0000 ) { /* 子ウインドウがクローズされ
						親に入力受付状態が移った */
			set_active_win(ix, sts);
		}
	}
}

/*
 * キー入力処理
 */
EXPORT	W	sc_keyfn(void)
{
	if (wevt.s.stat & (ES_CMD|ES_BUT2)) return sc_menufn();
	return E_OK;
}

/*
 * (x, y) の位置は、date の年月の何日か
 */
LOCAL	W	get_date(W x,W y,DATE date)
/* 日付の位置：(0, 0) - (6, 5) */
/* 日付は使用しない */
{
	W	day;

	day = y * 7 + x + 1 - get_week1st( date );
	if (day > month_day(&date)/*get_maxdate(date)*/) return -1;
	return day;
}

/*
 * 予定表の表示を prevdate から date にする
 */
EXPORT	W	redisp_date(DATE prevdate,DATE date)
{
	if (win[SCH_WIN].id > 0) {
		/*日付→欄→ボタンの順で表示する*/
		view_window(SCH_WIN, &visrect[SCH_WIN], 1);
		set_textbox(SCH_WIN, 1);
		win_dispsw(SCH_WIN);
	}

	/* 検索パネルのボタンの再表示 */
	if (win[SRC_WIN].id > 0) pnl_dispsw(SRC_WIN, 1);
	return 0;
}

/*
 * 予定表を date の日付にする
 */
EXPORT	W	change_date(DATE date)
{
	SCH_NODE	*schp;
	W		i;

	/* 空のノードは削除する */
	if (is_empty_sch_node(cur_sch)) cur_sch = delete_sch_node( cur_sch );

	/* 予定表を変える */
	if ((i = get_sch_node( &date, &schp )) < E_OK) return i;

	cur_sch = schp;
	set_curdata(SCH_WIN);

	selectdate = date;

	return 0;
}

/*
 * ＰＤプレス処理(パーツ以外：カレンダー)
 */
LOCAL	W	pres_calwin(void)
{
	W	x, y;
	W	err;
	DATE	presdate, prevdate;

	/* カレンダーの年月の上の場合は、現在の月に戻る */
	if ( (inrect(cal_year, wevt.e.pos))
	  && !(IS_SAME_MONTH(dispdate, todaydate)) ) {
		dispdate.year  = todaydate.year;
		dispdate.month = todaydate.month;
		win_dispsw(CAL_WIN);
		view_window(CAL_WIN, &visrect[CAL_WIN], 1);
		return E_OK;
	}

	/* カレンダーの日付の上か否か */
	/* cal_lefttop は、曜日の左上なので、日付は一行下から始まる */
	x = wevt.e.pos.x - cal_lefttop.x;
	y = wevt.e.pos.y - cal_lefttop.y - cal_daysize.y;
	if (x < 0 || y < 0) return -1;

	x /= cal_daysize.x;
	y /= cal_daysize.y;
	if (x > 6 || y > 5) return -1;

	prevdate = selectdate;
	presdate = dispdate;
	if ((presdate.day = get_date(x, y, presdate)) <= 0) return -1;

	if ( IS_SAME_DATE(presdate, selectdate) ) {
		/* 予定表が開いていて、同じ日なら何もしない */
		if ( win[SCH_WIN].id > 0 ) return E_OK;
	} else {
		/* 予定表の日付変更 */
		change_date(presdate);
	}

	/* 予定表が閉じている場合は、オープンする */
	if (win[SCH_WIN].id < 0) {
		err = main_win_open(SCH_WIN);
		if (err < E_OK)	panel(ER_MEMORY);
		else		sched_fusen.win_sw |= BIT_MASK(SCH_WIN);
	} else {

		redisp_date( prevdate, selectdate );
	}

	return E_OK;
}

/*
 * ＰＤプレス処理(パーツ以外：住所録)
 */
LOCAL	W	pres_adrwin(void)
{
	RECT	vr;
	PNT	dpos;
	W	idx;
	W	sel;

	vr = adr_index_rect;
	if ( !inrect(vr, wevt.e.pos) ) return -1; /* インデックス文字以外 */

	/* ポップアップメニューの登録 */
	if ( (idx = opengmenu(POPM_INDEX)) < E_OK ) {
		errpanel(ER_MENU, idx);
		return idx;
	}

	/* インジケータの設定 */
	idx = midashi_idx(cur_adr->index);
	chggmenu(POPM_INDEX, 1, idx, 0);

	/* ポップアップメニューの表示位置の調整：
		汎用メニューはインジケータが反転しないのでずらす */
	dpos.x = vr.c.left - 24;
	dpos.y = vr.c.bottom;
	gcnv_abs( win[ADR_WIN].gid, &dpos );

	/* ポップアップメニューの表示/選択 */
	if ( selgmenu(POPM_INDEX, dpos, &sel) == 1 ) {

		if ( idx == sel ) goto END; /* ページ切替なし */

		/* ページを切り替える */
		if ( set_adr_page(sel, 1) ) {
			/* 再表示 */
			redisp_adr(1);
		}
	}
END:
	closegmenu(POPM_INDEX);		/* ポップアップメニューのクローズ */
	return 0;
}

/*
 * ポインティングデバイス・プレス処理
 */
EXPORT	W	sc_presfn( W ix)
/* ウインドウ情報レコード番号 */
{
	W	ret = -1;

	/*無効なixは無視する*/
	if (!(ix < MAX_WIN) || winfo[ix].wid == -1) return E_OK;

	in_act_parts = ix;
	switch (ix) {
	case CAL_WIN:
		ret = pres_calwin();
		break;
	case ADR_WIN:
		ret = pres_adrwin();
		break;
	}
	return (ret != E_OK) ? pres_parts(ix, 0) : E_OK;
}

/*
 * 終了処理
 */
EXPORT	W	sc_finfn( W ix,W mode)
/* 対象ウインドウ番号 */
/* 終了要求要因
				 *	0 : ピクトグラムのダブルクリック
				 *	1 : W_DELETE イベント
				 *	2 : W_FINIFH イベント
				 */
{
	W		sw;	/* ウインドウ表示スイッチ */

	switch ( mode ) {
	  case 0:	/* ウインドウを閉じる */
		/* すべてのウインドウが閉じられたときには終了するが、
		 * それ以外は継続する
		 */
		sw = sched_fusen.win_sw;
		sw ^= BIT_MASK(ix);
		if ( DISP_MAIN(sw) == 0 ) {
			/* 全ウインドウが閉じられた：ファイルの保存 */
			if ( sc_exit(0) ) return 1; /* 付箋を更新して終了 */
			else		  return 0; /* 終了せず */
		} else {
			/* ウインドウ ix のみ閉じる */
			close_win(ix, 0);
			sched_fusen.win_sw = sw;
			if (ix == SRC_WIN) in_act_parts = sched_fusen.win_top;
			return 0; /* 終了せず */
		}

	  case 1:	/* ファイルに保存して即座に終了 */
		sc_exit(1);
		return 1; /* 付箋を更新して終了 */

	  case 2:	/* データを破棄して即座に終了 */
		return -1; /* 終了 */

	  default:
		return 0; /* 終了拒否 */
	}
}

/*
 * 貼り込み処理
 */
EXPORT	W	sc_pastefn( W ix,PNT pos)
/* 対象ウインドウ番号 */
/* 貼り込み位置(相対座標) */
{
	W	no;
	W	pid;

	/* pos は点線枠の左上の位置。wevt.s.time がＰＤの位置。 */
	/* 電子手帳では、ＰＤ位置に貼り込む。 */
	pos = UWtoPNT(wevt.s.time);

	if ( (ix == SCH_WIN || ix == ADR_WIN || ix == SRC_WIN)
	  &&  win[ix].id > 0 && cfnd_par(win[ix].id, pos, &pid) > 0) {

		/* パーツがテキストボックスのときだけ */
		if ((no = no_tboxpid(ix, pid)) >= wpinfo[ix].tbox_start) {
			wpinfo[ix].tbox_no = no;
			if (text_from_tray(ix, 1, pos) > 0) {
				tset_dat(NULL, 0);	/* 一時トレーの解放 */
				/* modified = 1; */
				return W_ACK;
			}
			/* 文字がない場合もエラー応答する */
		}
	}
	return W_NAK;
}

#define lengthof(array)	(sizeof (array)/sizeof (array [0]))
/*
 * 仮身要求イベントの処理
 */
EXPORT	VOID	sc_vobjfn(W ix)
/* 対象ウインドウ番号 */
{
	W	req, mode;
	W	i;
	W length;
	TC	title[96+1];

	if ((req = wevt.g.data[2]) >= 128) {
		orsp_prc((EVENT*)&wevt, NULL, 0, ER_NOSPT);

	} else if (req == 7) {	/* ウィンドウタイトル変更 */

		if ( (mode = wget_tit(root_wid, 0, title)) < 0 ) {
			return;
		}
		length = mtc_unique (f_name, title, lengthof (f_name) - 1);
		f_name [length] = TNULL;

		for (i = 0; i < MAX_MW; i++) {	/* 従属ウィンドウは変更なし */

			if (win[i].id < 0) continue;

			mtc_strjoin (title, lengthof (title) - 1,  f_name, window_name [i], 0);
			wset_tit(win[i].id, -1, title, mode);
		}
	}
	return;
}
