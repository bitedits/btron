/*
	scparts.c	電子手帳 : パーツ表示・操作

	(C) Copyright 1997-98 by Personal Media Corporation
*/
#include "sched.h"
#include <tlang.h>
#include <mtstring.h>

/* パーツ動作中ウィンドウ(動作中でない場合は、-1) */
EXPORT	W	in_act_parts = -1;

/* データボックス */
EXPORT	W*	wind_def[MAX_WIN] = {NULL, NULL, NULL, NULL};

EXPORT	PARTS_INFO	wpinfo[MAX_WIN] = {
	{ 2, 2, 2, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
			{0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L}},
	{ 8, 2, 2, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
			{0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L}},
	{12, 2, 2, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
			{0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L}},
	{ 3, 2, 2, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
			{0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L}}
};


/* 予定表一時領域 */
LOCAL	SCH_NODE	*tmp_sch;

/* 住所録一時領域 */
LOCAL	ADR_DATA	dmy_adata;
LOCAL	ADR_DATA	*tmp_adata;

/* テキストボックス・バッファ */
EXPORT	TC	*tbufp[MAX_WIN][10];
LOCAL	W	txsize[MAX_WIN][10];
EXPORT	TC	srcbuf[TBUFLEN];		/* 検索文字列 */
EXPORT	W	maxtxsz[MAX_WIN][10] = {
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{SCH_LEN, SCH_LEN, SCH_LEN, SCH_LEN, SCH_LEN, SCH_LEN, 0, 0, 0, 0},
	{YOMI_LEN, YUBIN_LEN, ADDR1_LEN, ADDR2_LEN, ADDR3_LEN,
	 NAME_LEN, TEL_LEN, FAX_LEN, MEMO1_LEN, MEMO2_LEN},
	{MAX_TBLEN, 0, 0, 0, 0, 0, 0, 0, 0, 0}
};

#define	DEF_CHSZ	0x0c

/* テキストボックス初期値 */
LOCAL	TC	init_text[8] = {
		/* COLOR	 FCLASS  FATTR	  SIZE	  */
	MC_ATTR, 0xffff, 0xffff, 0xffff,  0,  DEF_CHSZ, DEF_CHSZ, 0
};


/*
 *	現在のデータをパーツに設定する
 */
EXPORT	W	set_curdata(W ix)
{
	W	i;

	switch (ix) {
	case CAL_WIN:
		/* 開いた仮身では、前回の表示ページを表示する。
		 *	（設定は、set_page_data (scdamain.c) で行っている。）
		 * 実行では、今日の年月を表示する。
		 */
		if (disp_vobj == 0) {
			dispdate.year = todaydate.year;
			dispdate.month = todaydate.month;
			dispdate.day = todaydate.day;
		}
		break;
	case SCH_WIN:
		if (cur_sch != (SCH_NODE*)NULL && cur_sch->data[0] != (TC*)NULL) {
			tmp_sch = cur_sch;
		} else {
		    if (sch_data != (SCH_NODE*)NULL) {
			tmp_sch = sch_data;
		    } else {
			tmp_sch = insert_sch_node(NULL, 1);
			if (tmp_sch == (SCH_NODE*)NULL) return ER_NOMEM;

			tmp_sch->year = todaydate.year;
			tmp_sch->month = todaydate.month;
			tmp_sch->day = todaydate.day;

			cur_sch = tmp_sch;

		    }

		}

		selectdate.year = tmp_sch->year;
		selectdate.month = tmp_sch->month;
		selectdate.day = tmp_sch->day;

		for (i = 0; i < MAX_SCH; i++) {
			tbufp[ix][i] = tmp_sch->data[i];
		}
		break;
	case ADR_WIN:
		if (cur_adr != NULL && cur_adr->data[cur_adr_ofs] != NULL) {
			tmp_adata = cur_adr->data[cur_adr_ofs];
		} else {
			tmp_adata = &dmy_adata;
		}
		tbufp[ix][0]  = tmp_adata->yomi;
		tbufp[ix][1]  = tmp_adata->yubin;
		tbufp[ix][2]  = tmp_adata->addr1;
		tbufp[ix][3]  = tmp_adata->addr2;
		tbufp[ix][4]  = tmp_adata->addr3;
		tbufp[ix][5]  = tmp_adata->name;
		tbufp[ix][6]  = tmp_adata->tel;
		tbufp[ix][7]  = tmp_adata->fax;
		tbufp[ix][8]  = tmp_adata->memo1;
		tbufp[ix][9]  = tmp_adata->memo2;
		break;
	case SRC_WIN:
		tbufp[ix][0] = srcbuf;
		break;
	}
	return E_OK;
}

/*
 *	ウィンドウＩＤからウィンドウ番号を求める
 */
LOCAL	W	get_window_ix(W wid)
{
	W	i;

	for (i = 0; i < MAX_WIN; i++) {
		if (wid == win[i].id) return i;
	}
	return -1;
}

/*
 *	ccut_txt()で、文字列を取り込んで、取り込んだ文字数を返す。
 */
EXPORT	W	tx_cut_txt(W pid,W sz,TC *buf,W cut)
{
	W	er;
	if ((er = ccut_txt( pid, sz, buf, cut )) < 0) return er;
	return ( buf ) ? tc_strlen( buf ) : 0;
}

/*
 *	テキストボックスを入力不可状態とする。
 */
EXPORT	VOID	inact_tbox(W pid)
{
	tx_cut_txt( pid, TBUFLEN, NULL, 0 );
	cchg_par( pid, P_INACT );
}

/*
 * 現在の予定表のページ(cur_sch)が、先頭なら TRUE
 */
LOCAL	BOOL	is_first_sch_data(void)
{
	return IS_FIRST_DATE(selectdate) && cur_sch == sch_data;
}

/*
 * 現在の予定表のページ(cur_sch)が、最後なら TRUE
 */
LOCAL	BOOL	is_last_sch_data(void)
{
	return IS_LAST_DATE(selectdate) && cur_sch->next == sch_data;
}

/*
 * 現在の住所録のページ(cur_adr, cur_adr_ofs)が、先頭なら TRUE
 */
LOCAL	BOOL	is_first_adr_data(void)
{
	W	i;

	if (cur_adr != adr_data) return FALSE;
	for (i = cur_adr_ofs - 1; i >= 0; i--) {
		if (cur_adr->data[i] != NULL) return FALSE;
	}
	return TRUE;
}

/*
 * 現在の住所録のページ(cur_adr, cur_adr_ofs)が、最後なら TRUE
 */
LOCAL	BOOL	is_last_adr_data(void)
{
	W	i;

	if (cur_adr->next != adr_data) return FALSE;
	for (i = cur_adr_ofs + 1; i < MAX_ADR; i++) {
		if (cur_adr->data[i] != NULL) return FALSE;
	}
	return TRUE;
}

/*
 * ページ変更の不能か否かを、disable[] に設定する。
 *	前のページがない場合は、disable[0] = TRUE、ある場合は、FALSE
 *	後のページがない場合は、disable[1] = TRUE、ある場合は、FALSE
 */
EXPORT	VOID	chk_page_change(int ix,BOOL disable[])
{
	switch (ix) {
	case CAL_WIN:
		disable[0] = IS_FIRST_MONTH(dispdate);
		disable[1] = IS_LAST_MONTH(dispdate);
		break;
	case SCH_WIN:
		disable[0] = is_first_sch_data();
		disable[1] = is_last_sch_data();
		break;
	case ADR_WIN:
		if ( adr_data == NULL ) {
			disable[0] = disable[1] = TRUE;
		} else {
			disable[0] = is_first_adr_data();
			disable[1] = is_last_adr_data();
		}
		break;
	default:
		disable[0] = disable[1] = TRUE;
		break;
	}
}

/*
 *	ウィンドウのスイッチの状態変更 ＆ 再表示
 */
EXPORT	VOID	win_dispsw(W ix)
{
	BOOL	disable[2];
	W	i;

	if (wget_act(NULL) != win[ix].id) {
		/*入力受付状態でないウィンドウはめくりボタンを不能表示*/
		disable[0] = disable[1] = TRUE;
	} else {
		chk_page_change(ix, disable);
	}

	for (i=0; i<2; i++) {
		cchg_par(wpinfo[ix].pid[i], disable[i] ? P_DISABLE : P_ENABLE);
		cdsp_par(wpinfo[ix].pid[i], P_RDISP);
	}
	return;
}

/*
 *	検索パネルのスイッチの状態変更 ＆ 再表示
 */
EXPORT	VOID	pnl_dispsw(W ix,W mode)
/* 0：無条件、1：変更時のみ */
{
	BOOL	disable[2];
	W	cmd[2], sts[2];

	if (ix == SRC_WIN) {
		if (tc_strlen( srcbuf ) <= 0 || sched_fusen.win_top == CAL_WIN) {
			disable[0] = disable[1] = TRUE;
		} else {
			chk_page_change(sched_fusen.win_top, disable);
		}

		sts[0] = cget_sts(wpinfo[SRC_WIN].pid[0], (W*)NULL);
		sts[1] = cget_sts(wpinfo[SRC_WIN].pid[1], (W*)NULL);

		cmd[0] = disable[0] ? P_DISABLE : P_ENABLE;
		cmd[1] = disable[1] ? P_DISABLE : P_ENABLE;

		if ( mode == 0
		     || ((cmd[0] & P_DISABLE) && !(sts[0] & P_DISABLE))
		     || (!(cmd[0] & P_DISABLE) && (sts[0] & P_DISABLE)) ) {
			cchg_par(wpinfo[SRC_WIN].pid[0], cmd[0]);
			cdsp_par(wpinfo[SRC_WIN].pid[0], P_RDISP);
		}
		if ( mode == 0
		     || ((cmd[1] & P_DISABLE) && !(sts[1] & P_DISABLE))
		     || (!(cmd[1] & P_DISABLE) && (sts[1] & P_DISABLE)) ) {
			cchg_par(wpinfo[SRC_WIN].pid[1], cmd[1]);
			cdsp_par(wpinfo[SRC_WIN].pid[1], P_RDISP);
		}
	}
}

/*
 * savebuf に保存してある内容を buff とテキストボックス(pid) に再設定する
 */
EXPORT	VOID	txt_recover(W pid,TC buff[],TC savebuf[])
{
	W	len;

	tc_strcpy( buff, savebuf );
	len = tc_strlen(savebuf);
	cset_val( pid, len + 1, (W*)buff );
}

/* 住所録のよみの変更があれば、1 にする */
LOCAL	W	yomichange;

/*
 * テキストボックスの変更の有無を調べ、変更があったら、バッファを更新する
 *	住所録のよみに変更があった場合は、ソートする。
 */
LOCAL	W	chk_and_chg(W ix,W tmpid,TC *buff,W change,BOOL set)
/* ウィンドウ番号 */
/* 操作中のパーツＩＤ */
/* テキストボックス用バッファ */
/* その他のパーツの変更有無 */
/* バッファへの設定  TRUE：設定する */
{
	TC	tmpbuf[TBUFLEN];
	W	err;
	W length;

	err = cget_val( tmpid, TBUFLEN, (W*)tmpbuf );
	if (err < 0) {
		return err;
	}
	length = err;
	/* まず一意表現に変換する */
	length = mtc_unique (tmpbuf, tmpbuf, length);
	tmpbuf [length] = TNULL;
	length = tc_strlen (buff);
	length = mtc_unique (buff, buff, length);
	buff [length] = TNULL;

	/* 内容に変更があるか */
	if ( mtc_strcmp( tmpbuf, buff ) || change || yomichange) {
		if (ix != SRC_WIN || change || yomichange) {
			modified = TRUE;
		}
		if (set) tc_strcpy( buff, tmpbuf );

		if ((ix == ADR_WIN && buff == tbufp[ix][0]) || yomichange) {
			/* 住所録のソートを行う */
			chg_adr_yomi();
		}
	}
	return err;
}

/* トレーバッファ */
LOCAL	TC	tray_buff[TBUFLEN];

/*
 *	他のプロセスのウィンドウへの移動／複写
 */
LOCAL	W	paste_on_other(W ix,W pid,TC *buff,W cut)
/* ウィンドウ番号 */
/* パーツＩＤ */
/* pid のバッファ */
/* 移動／複写 (!0/0) */
{
	TC	buf[TBUFLEN];
	TC	erbuf[TBUFLEN];
	W	len, err;
	RECT	r;

	/* エラー時のためにテキストボックス全体を読み出す */
	cget_val( pid, TBUFLEN, (W*)erbuf );

	/* 選択領域を取り込む */
	if ((len = tx_cut_txt(pid, TBUFLEN, buf, cut)) <= 0) return -1;

	cget_val( pid, TBUFLEN, (W*)buff );

	/*カレット・選択枠を消すために空読みして不能状態にする*/
	inact_tbox( pid );

	/* トレーバッファにセットする */
	memcpy(tray_buff, buf, len * sizeof(TC));

	/* 一時トレーへデータをセットする */
	setrect(r, 0, 0, (DEF_CHSZ + (DEF_CHSZ>>3)) * len, DEF_CHSZ);
	if ( (err = put_tray( 1, tray_buff, len, &r )) <= 0 ) goto EEXIT;

	/* 貼り込み要求を送信する */
	setpointer(PS_BUSY, NULL);
	wevt.r.type = EV_REQUEST;
	wevt.r.cmd = W_PASTE;

	/* e.time に ＰＤ位置設定。e.pos に点線枠の左上設定。 */
	/* 左上の位置はドラッグ文字の中央にあるとして設定する。 */
	wevt.e.time = (wevt.e.pos.x << 16) | (wevt.e.pos.y & 0xffff);

	wevt.e.pos.x -= rectwidth(r) >> 1;
	wevt.e.pos.y -= rectheight(r) >> 1;

	if ( wsnd_evt (&wevt) >= 0 ) {
	    /* レスポンス待ち */
	    if ( wwai_rsp(&wevt,W_PASTE,60000) == W_ACK ) {
		if (cut != 0) {
		    if (PNTtoUW(wevt.e.pos) == 0x80008000) {
			/* 移動に対して複写応答が返ったら、元に戻す */
			txt_recover( pid, buff, erbuf );
		    } else {
			/* 移動した場合は、変更チェック */
			if (ix != SRC_WIN) {
				modified = TRUE;
			}
			if ( ix == ADR_WIN
			  && pid == wpinfo[ix].pid[wpinfo[ix].tbox_start]) {
				chg_adr_yomi();
			}
		    }
		}
		wswi_wnd(wevt.s.wid, 0L); /* 相手にスイッチする */
		setpointer(PS_SELECT, NULL);
		return 0;
	    }
	}
	err = 1;
EEXIT:
	/* 移動、複写は失敗 */
	setpointer(PS_SELECT, NULL);
	tset_dat (NULL, 0);		/* 一時トレーの解放 */
	if (err) {
		errpanel((err < 0)? ER_CUT : ER_MOVE, err);
		txt_recover( pid, buff, erbuf );
	}
	return -1;
}


/*
 *	他のウィンドウへの移動／複写
 */
LOCAL	W	paste_on_mywin(W ix,W pid,TC *buff,W cut)
/* ウィンドウ番号 */
/* パーツＩＤ */
/* pid のバッファ */
/* 移動／複写 (!0/0) */
{
	TC	buf[TBUFLEN];
	TC	*tbuf;
	W	len,err;
	W	i, toid, toix, ts;
	W	*lenptr = 0;
	PNT pnt = wevt.e.pos;

	toix = get_window_ix(wevt.s.wid);

	/* 検索パネルへの移動は、複写とする */
	if (toix == SRC_WIN) cut = 0;

	if (cfnd_par(wevt.s.wid, pnt, &toid) <= 0) return -1;

	tbuf = (TC*)NULL;
	for (i = ts = wpinfo[toix].tbox_start; i < wpinfo[toix].parts_num; i++ ) {
		if (toid == wpinfo[toix].pid[i]) {
			tbuf = tbufp[toix][i - ts]; lenptr = &txsize[toix][i - ts];
			break;
		}
	}
	if (tbuf == (TC*)NULL) return -1;

	/* 選択領域のみ取り出す */
	if ((len = tx_cut_txt(pid, TBUFLEN, buf, cut) ) <= 0) return len;

	len = cins_txt (toid, pnt, buf);
	if (len < 0 && len != EX_PAR) {
		/* 一文字も入らなかった時、cins_txt は EX_PAR を返すので無視 */
		return len;
	}
	if (lenptr) {
		*lenptr = len;
	}
	/* 住所録のよみへ移動・複写した */
	if (toix == ADR_WIN && i == ts) yomichange = 1;

	/*カレット・選択枠を消すために空読みして不能状態にする*/
	inact_tbox( pid );

	/* 移動先のウィンドウの変更チェック */
	chk_and_chg(toix, toid, tbuf, 0, FALSE);
	err = cget_val (toid, TBUFLEN, (VP) tbuf);

	/* 移動元のウィンドウの変更チェックと設定 */
	chk_and_chg(ix, pid, buff, cut, TRUE);

	if (toix == SRC_WIN)	pnl_dispsw(toix, 1);
	if (ix == SRC_WIN)	pnl_dispsw(ix, 1);

	wpinfo[toix].tbox_no = i;

	if (err >= 0 && toix != SRC_WIN) wswi_wnd( win[toix].id, NULL );

	return err;
}

/*
 *	テキストボックス間の移動／複写
 */
LOCAL	W	copy_tbox(W ix,W fromid,W *toid,W cut)
/* ウィンドウ */
/* 元のパーツＩＤ */
/* 先のパーツＩＤ */
/* 移動／複写 (!0/0) */
{
	TC	buf[TBUFLEN];
	TC	*tbuf;
	W	*lenptr = 0, len, i, ts;
	PNT pnt = wevt.e.pos;

	/* 他のテキストボックスでのリリース以外は、無視する */
	if (cfnd_par(win[ix].id, pnt, toid) <= 0) return -1;

	tbuf = (TC*)NULL;
	for (i = ts = wpinfo[ix].tbox_start; i < wpinfo[ix].parts_num; i++ ) {
		if (*toid == wpinfo[ix].pid[i] && fromid != *toid) {
			tbuf = tbufp[ix][i - ts]; lenptr = &txsize[ix][i - ts];
			break;
		}
	}
	if (tbuf == (TC*)NULL) return -1;

	/* 選択領域のみ取り出す */
	if ((len = tx_cut_txt(fromid, TBUFLEN, buf, cut) ) <= 0) return len;

	len = cins_txt (*toid, pnt, buf);
	if (len < 0 && len != EX_PAR) {
		/* 一文字も入らなかった時、cins_txt は EX_PAR を返すので無視 */
		return len;
	}
	if (lenptr) {
		*lenptr = len;
	}
	/* 住所録のよみへ移動・複写した */
	if (ix == ADR_WIN && i == ts) yomichange = 1;

	return cget_val (*toid, TBUFLEN, (VP) tbuf);
}

/*
 *	次のテキストボックスのＩＤを求める
 */
LOCAL	W	nextboxpid(W ix,W pid)
{
	W	i, start, max;
	W	*id;

	start = wpinfo[ix].tbox_start;
	max = wpinfo[ix].parts_num;
	id = wpinfo[ix].pid;

	if (wevt.s.stat & (ES_LSHFT|ES_RSHFT)) {
		/* 逆方向 */
		max--; start--;
		for (i = max; i > start; i--) {
			if (pid == id[i]) {
				pid = id[(--i > start) ? i : max];
				break;
			}
		}
	} else {
		/* 順方向 */
		for (i = start; i < max; i++) {
			if (pid == id[i]) {
				pid = id[(++i < max) ? i : start];
				break;
			}
		}
	}
	return pid;
}

/*
 *	テキストボックス配列番号を求める(ない場合は、０)
 */
EXPORT	W	no_tboxpid(W ix,W pid)
{
	W	i, start, max;
	W	*id;

	start = wpinfo[ix].tbox_start;
	max = wpinfo[ix].parts_num;
	id = wpinfo[ix].pid;
	for (i = start; i < max; i++) {
		if (pid == id[i]) return i;
	}
	return 0;
}

/*
 *	テキストボックス処理
 */
LOCAL	W	txtbox(W ix,W *pid,W act,TC *buff)
/* ウィンドウ */
/* パーツＩＤ */
/* cact_par の返り値 */
/* 読み込み領域 */
{
	W	tmpid = *pid;
	W	unget = 0;
	W	cut;
	W	change;

	cut = change = yomichange = 0;
	if (act & P_BREAK) {
		if (act == P_EVENT) unget--;
		else if (act == P_MENU) unget--;
		else unget++;
	} else {
	    switch ( act & P_SMASK ) {
	    case P_NL:		/* 改段落 */
		/*unget++;*/	/* return  0 とするため */
	    case P_END:		/* 入力終 */
		wevt.e.type = EV_NULL;
		break;

	    case P_TAB:
		*pid = nextboxpid( ix, tmpid );
		wevt.e.type = EV_NULL;
		break;

	    case P_BUT:
		if (wevt.s.wid == win[ix].id) {
			if (cfnd_par( win[ix].id, wevt.e.pos, pid ) > 0) break;
			*pid = tmpid;
			/* 住所録のインデックス以外のウィンドウ内は無視する */
			if ( !(ix == ADR_WIN
				&& inrect(adr_index_rect, wevt.e.pos)) ) {
			    if (inrect(visrect[ix], wevt.e.pos)) goto IGNORE;
			}
			/* 移動の場合は、カレットを残しておく */
			unget--;
		} else unget++;
		break;

	    case P_MOVE:	cut = 1;	/* 移動 */
	    case P_COPY:			/* 複写 */
		if (wevt.s.wid == win[ix].id) {
		    /* 同じウィンドウ内でリリース */
		    /* 他のテキストボックスの場合のみ処理する */
		    if (copy_tbox( ix, tmpid, pid, cut ) <= 0) goto IGNORE;
		    change = 1;
		    wevt.e.type = EV_NULL;
		    break;
		} else if (   wevt.s.wid == win[CAL_WIN].id
			   || wevt.s.wid == win[SCH_WIN].id
			   || wevt.s.wid == win[ADR_WIN].id
			   || wevt.s.wid == win[SRC_WIN].id  ) {
		    /* 管理下の他のウィンドウ内でリリース */
		    if ( paste_on_mywin(ix, tmpid, buff, cut) >= 0 ) return 0;
		    goto IGNORE;
		} else if (wevt.s.wid > 0) {
		    /* ウィンドウ外でリリース */
		    if ( paste_on_other(ix, tmpid, buff, cut) == 0 ) return 0;
		    goto IGNORE;
		}
		/* no break */

	    default:
IGNORE:		*pid = tmpid;
		wevt.e.type = EV_NULL;
		return 1;
	    }
	}

	/* 内容に変更があるか */
	chk_and_chg(ix, tmpid, buff, change, TRUE);

	/*カレット・選択枠を消すために空読みして不能状態にする*/
	if (unget > 0) {
		inact_tbox( tmpid );
	}

	if ( unget ) {
		unget_wevt();

	}
	return ( unget != 0 ) ? 0 : 1;
}

/*
 *	プレス処理
 */
EXPORT	W	pres_parts(W ix,W pid)
{
	W	i, extact;
	W	no, n;

	if ( win[ix].id < 0 ) return 0;

	if ( pid > 0 ) wevt.e.type = EV_NULL;
	else if ( cfnd_par( win[ix].id, wevt.e.pos, &pid ) <= 0 ) return 0;

	/*caret_off();*/	/* 従属ウィンドウでは主ウィンドウのカレットを消去する */

	do {
		extact = 0;
		no = no_tboxpid(ix, pid);
	Reactivate:
		if ((i = cact_par( pid, &wevt )) > 0) {
			if ( (i & ~P_CHANGE) == P_EVENT ) {
				if ( wevt.r.type == EV_REQUEST
				     && wevt.r.cmd == W_REDISP ) {
					/* 再表示 */
					unget_wevt();
					evt_proc();
					wevt.e.type = EV_NULL;
					goto Reactivate;
				}
			}
			if ( no > 0 ) {
				/* テキストボックス */
				wpinfo[ix].tbox_no = no;
				n = no - wpinfo[ix].tbox_start;

				extact = txtbox( ix, &pid, i, tbufp[ix][n] );

				txsize[ix][n] = tc_strlen( tbufp[ix][n] );
				pnl_dispsw(ix, 1);

			} else if ( i & P_CHANGE ) {
				if ( pid == wpinfo[ix].pid[0] ) {
					/* 前ページ処理 */
					(*cmd_page[ix])((wevt.s.stat & (ES_LSHFT|ES_RSHFT)) ? 0 : 1);
					extact = 1;

				} else if ( pid == wpinfo[ix].pid[1] ) {
					/* 次ページ処理 */
					(*cmd_page[ix])((wevt.s.stat & (ES_LSHFT|ES_RSHFT)) ? 3 : 2);
					extact = 1;

				}
				break;
			}
		} else {
			inact_tbox( no );
		}
	} while ( extact != 0 );

	return 0;
}

/*
 *	パーツの削除
 */
EXPORT	VOID	delete_parts(W ix)
{
	if (win[ix].id >= 0) {
		cdel_pwd( win[ix].id, /*CLR*/NOCLR );
	}
}

/*
 *	テキストボックス内の文字列の設定
 */
EXPORT	VOID	set_textbox(W ix,W disp)
/* 1：表示する、0：表示しない */
{
	W	i, pid;
	W	ts;
	TC	*newstr;
	TC	buf[4];

	for (i = ts = wpinfo[ix].tbox_start; i < wpinfo[ix].parts_num; i++) {
		pid = wpinfo[ix].pid[i];
		newstr = tbufp[ix][i - ts];

		/* 高速化のため、空の文字列を空のTEXTBOXにセットしようとした
		** ときには何もしない
		*/
		cget_val(pid, 4, (W*)buf);
		if (disp == 0 && *newstr == TNULL && *buf == TNULL) continue;

		cset_val(pid, ((TEXTBOX*)wpinfo[ix].parts[i])->txsize, (W*)newstr);

		if (disp) cdsp_par(pid, P_DISP);
	}
}

/*
 * パーツデータをデータボックスから設定する
 */
EXPORT	VOID	set_parts_data(W ix)
{
	W	i, ofs;
	W	pnum, type;
	PARTS	*p;

	pnum = wind_def[ix][0];		/* パーツの数 */
	ofs = wind_def[ix][1];		/* 先頭パーツまでのオフセット */
	for ( i = 0; i < pnum; i++ ) {
		p = (PARTS*)ptrdbox(wind_def[ix][i + ofs]);
		/* 座標単位の変換 */
		adjscalr(&p->ss.r);

		/* ポインタの設定 */
		type = p->ss.type & P_TYPE;
		if (type == TB_PARTS)
			p->tb.text = init_text;
		else if (type == MS_PARTS)
			offtoptr((UW*) &(p->ss.name) );

		/* wpinfo に設定 */
		wpinfo[ix].parts[i] = (PARTS*)p;
	}
}

/*
 *	パーツの生成
 */
EXPORT	W	create_parts(W ix,W disp)
/* 表示、1：する、0：しない */
{
	W	i;
	W	wid, pnum, ret = 0;

	if ((wid = win[ix].id) < 0) return 0;

	/* パーツの登録・表示 */
	/* wind_def は、open_win() で設定済 */

	set_parts_data(ix);		/* データボックスから wpinfo に */

	pnum = wpinfo[ix].parts_num;		/* パーツの数 */

	for ( i = 0; i < pnum; i++ ) {

	    /* パーツの登録(データボックスは表示をしない設定) */
	    if ((wpinfo[ix].pid[i] = ccre_par(wid, wpinfo[ix].parts[i])) < 0) {
		delete_parts(ix);
		return wpinfo[ix].pid[i];
	    }
	}

	/* 初期表示 */
	if (disp) {
		/* テキストボックス内の文字列の設定 */
		set_textbox(ix, disp);

		if (ix == CAL_WIN || ix == SCH_WIN || ix == ADR_WIN) {
			win_dispsw(ix);		/* ページめくりボタン表示 */
		} else if (ix == SRC_WIN) {
			pnl_dispsw(ix, 0);	/* 検索パネルのスイッチ表示 */
		}

		view_etc(ix, 0);	/* 固定文字列などの表示 */
	}

	return ret;
}
