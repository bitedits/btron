/*
	scprint.c	電子手帳 : 印刷

	(C) Copyright 1997-98 by Personal Media Corporation
*/
#include "sched.h"
#include <tlang.h>

LOCAL	W		print_mode;	/* １ページか、全ページか */

LOCAL	W		pr_size = 16;		/* 文字サイズ */
LOCAL	W		pr_chgap = 2;		/* 文字間隔 */
LOCAL	W		pr_lngap = 4;		/* 段落(行)間隔 */

LOCAL	struct {	/* 管理情報セグメント */
	UH		id;
	UH		len;
	SEG_INFO	s;
} info_seg = {
	TS_INFO|0xff00, 6, {0, 2, {TAD_VERSION}}
};

LOCAL	struct {	/* 文章開始セグメント */
	UH		id;
	UH		len;
	SEG_TEXT	tx;
} text_seg = {
	TS_TEXT|0xff00, 24,
	{{{0,0,0,0}}, {{0,0,0,0}}, DISPLAY_RESOLUTION, DISPLAY_RESOLUTION,
	 TAD_LANG_JAPANESE, 0}
};

LOCAL	struct {	/* 文章終了セグメント */
	UH		id;
	UH		len;
} textend_seg = {
	TS_TEXTEND|0xff00, 0
};

LOCAL	struct {	/* 行間隔指定付箋 */
	TC		id;
	UH		len;
	UH		attrsubid;
	SCALE		pitch;
} lgap_seg = {
	TS_TRULER|0xff00, 4, 0x0001, 0x0104
};

LOCAL	VOID	prs(TC *str)
{
	W	nb;

	ConvEndianHs(str, str, nb = tc_strlen(str));
	rputs((B*)str, tg_fp);
	ConvEndianHs(str, str, nb);
}

LOCAL	VOID	nl(void)
{
	static TC system_script = TC_LANG | TSC_SYS;
	rputc(ConvEndianH(system_script),  tg_fp);
	rputc(ConvEndianH(TC_NL),  tg_fp);
}
LOCAL	VOID	sp(void)
{
	rputc(ConvEndianH(TK_KSP), tg_fp);
}

/*
 * 予定表の日付を書き出す
 */
LOCAL	VOID	put_sch_date( SCH_NODE *np)
{
	DATE		date;		/* 予定日(数値) */
	TC		str[14+1];	/* 予定日(文字列) */

	/* 日付を文字列に変換 */
	date.year  = np->year;
	date.month = np->month;
	date.day   = np->day;
	date_to_jstr(str, &date);

	/* 日付書き出し */
	prs(str);
	nl();
}

/*
 * 予定表データを書き出す
 */
LOCAL	VOID	put_sch_data(SCH_NODE *np)
{
	W	i;
static	TC	indent[] = { TK_KSP, TK_KSP, TK_KSP, TK_KSP, 0 };

	for ( i = 0; i < MAX_SCH; ++i ) {
		if ( *(np->data[i]) == TNULL ) continue;
		/* 予定表データ書き出し */
		prs(indent); prs(np->data[i]); nl();
	}
}

/*
 * 予定表データを書き出す
 */
LOCAL	W	save_sch(void)
{
	SCH_NODE	*np;

	if ( (np = sch_data) == NULL ) return E_OK; /* データなし */

	if (print_mode & 1) {		/* 全ページ */
		do {
			/* 日付書き出し */
			put_sch_date( np );

			/* データ書き出し */
			put_sch_data( np );

		} while ( !rerror(tg_fp) && (np = np->next) != sch_data );
	} else {			/* 表示中のページ */
		/* 日付書き出し */
		put_sch_date(cur_sch);

		/* データ書き出し */
		put_sch_data(cur_sch);
	}

	return rerror(tg_fp);
}

LOCAL	W	adr_per_page;	/* １ページに書き出す住所録の件数     */
LOCAL	W	adr_cnt;	/*			     カウント */

/*
 * 住所録データを書き出す
 */
LOCAL	VOID	put_adr_data(ADR_DATA *data)
{
	W	i;

	prs(adr_str_info[1].str); prs(data->yubin);
	for (i = YUBIN_LEN - tc_strlen(data->yubin) + 2; i > 0; i--) sp();
	prs(adr_str_info[0].str); sp();	prs(data->yomi);  nl();
	prs(adr_str_info[2].str); sp(); prs(data->addr1); nl();
	prs(adr_str_info[3].str); sp(); prs(data->addr2); nl();
	prs(adr_str_info[4].str); sp(); prs(data->addr3); nl();
	prs(adr_str_info[5].str); sp(); prs(data->name);  nl();
	prs(adr_str_info[6].str); sp(); prs(data->tel);   nl();
	prs(adr_str_info[7].str); sp(); prs(data->fax);   nl();
	prs(adr_str_info[8].str); sp(); prs(data->memo1); nl();
	prs(adr_str_info[9].str); sp(); prs(data->memo2); nl();
}

/*
 * 住所録データを書き出す
 */
LOCAL	W	save_adr(void)
{
	ADR_NODE	*np;
	ADR_DATA	*data;		/* 住所１件分のデータ */
	W		i;

	if ( (np = adr_data) == NULL ) return E_OK; /* データなし */

	if (print_mode & 1) {		/* 全ページ */

		do {
			for ( i = 0; i < MAX_ADR; ++i ) {
				if ( (data = np->data[i]) == NULL ) continue;

				/* ページ替え */
				if (adr_cnt >= adr_per_page) {
					rputc(TC_FF, tg_fp); adr_cnt = 0;
				}

				if (adr_cnt != 0) nl();

				/* 住所録データの書き出し */
				put_adr_data(data);

				adr_cnt++;
			}

		} while ( !rerror(tg_fp) && (np = np->next) != adr_data );

	} else {			/* 表示中のページ */

		put_adr_data(cur_adr->data[cur_adr_ofs]);
	}

	return rerror(tg_fp);
}

/*
 * ブロック書き出し
 */
LOCAL	W	write_tg_fp( B *buf,W size)
{
	W	s = 0;

	/* rwrite() は、一度に 32KB までしか書き出せないので、
	   32KB 以下に分割して書き出す */
	while ( (size -= (W)s) > 0L ) {
		s = ( size > 0x7ff0L )? 0x7ff0: (W)size;
		if ( rwrite(buf, s, 1, tg_fp) != s ) break;
		buf += s;
	}
	return rerror(tg_fp);
}

/*
 * ヘッダーの書き出し
 */
LOCAL	W	put_header(void)
{
	/* 管理情報セグメントの書き出し */
	rputseg((B*)&info_seg, tg_fp);

	/* 文章開始セグメントの書き出し */
	rputseg((B*)&text_seg, tg_fp);

	/* 用紙関連付箋の書き出し */
	saveform(write_tg_fp, 1);

	/* 行間隔指定付箋の書き出し */
	rputseg((B*)&lgap_seg, tg_fp);

	return rerror(tg_fp);
}

/*
 * トレーラーの書き出し
 */
LOCAL	W	put_trailer(void)
{
	/* 文章終了セグメントの書き出し */
	rputseg((B*)&textend_seg, tg_fp);

	return rerror(tg_fp);
}

/*
 * 書き込みレコードの作成 ＆ そのレコードへの位置付け
 */
LOCAL	W	creat_rec(void)
{
	W	err;

	/* 先頭のデータレコードへ移動 */
	if ( (err = rrewind(tg_fp)) < E_OK ) return err;
	if ( reof(tg_fp) ) {

		/* データレコードが存在しない
		   ファイルの最後にデータレコードを追加 */
		err = apd_rec(rfileno(tg_fp), NULL, 0L, RT_TADDATA, 0, 0);
		if ( err < E_OK ) return err;
	} else {

		/* データレコードを挿入 */
		err = ins_rec(rfileno(tg_fp), NULL, 0L, RT_TADDATA, 0, 0);
		if ( err < E_OK ) return err;
	}

	/* 書き込みレコード(先頭レコード)へ位置付ける */
	return rrewind(tg_fp);
}

/*
 * 実身を保存する
 */
LOCAL	W	sc_save_print(void)
{
	W	err;
	W	newrec;		/* 新規作成した保存用レコードの番号 */

	/* 書き込むレコードの作成 */
	if ( (err = creat_rec()) < E_OK ) return err;
	newrec = rrecordno(tg_fp);

	/* ヘッダの書き出し */
	if ( (err = put_header()) < E_OK ) goto err_exit;

	if (sched_fusen.win_top == SCH_WIN) {
		/* 予定表データを書き出す */

		if ( (err = save_sch()) < E_OK ) goto err_exit;

	} else if (sched_fusen.win_top == ADR_WIN) {
		/* 住所録データを書き出す */

		/* １ページに書く件数・・・最低１件 */
		adr_per_page = paper_size.y / ((pr_size + pr_lngap) * 10);
		if (adr_per_page <= 0) adr_per_page = 1;
		adr_cnt = 0;
		if ( (err = save_adr()) < E_OK ) goto err_exit;
	}

	/* トレーラーの書き出し */
	if ( (err = put_trailer()) < E_OK ) goto err_exit;

	/* 書き込みバッファフラッシュ */
	if ( (err = rflush(tg_fp)) < E_OK ) goto err_exit;

	return err;

err_exit:
	return err;
}
/*
 *	一時ファイルの生成（印刷でも使用する）
 */
LOCAL	W	save_to_tmpfl(LINK* lnk)
{
	W	vid, i, cls_er;
	W	fd;

	i = oatt_vob(vid = mycmd->vid, 0);
	if (i >= 0) {	/* FS 接続 */
		*lnk = mycmd->lnk;
		if ((fd = i = cre_fil(lnk, f_name, NULL, 0, F_FLOAT)) >= 0) {
			tg_fp = rfdopen(fd, (UW)RM_TADDATA, F_UPDATE);
			i = sc_save_print();	/* データの書き込み */
			cls_er = rclose(tg_fp);
			if(i >= 0 && cls_er < 0) i = cls_er;
		}
	}
	return i;
}

LOCAL	W	mk_prfile(LINK* lnk)
/* 印刷用ファイル生成 */
{
	W	er;

	sysmsg (MSG_TMPFL);		/* 一時ファイル生成中 */
	er = save_to_tmpfl(lnk);
	sysmsg (0);
	if (er < 0) errpanel(ER_TMPFL, er);	/* 一時ファイル生成エラー */
	return er;
}

/*
 * 印刷
 */
EXPORT	W	sc_print(W para)
{
	W	i;

	if (sched_fusen.win_top == CAL_WIN) return 0;

	if (sched_fusen.win_top == SCH_WIN) {
		if (paper_size.x < (pr_size + pr_chgap) * 20) goto ERRPNL;

	} else if (sched_fusen.win_top == ADR_WIN) {
		if (paper_size.x < (pr_size + pr_chgap) * 24) {
ERRPNL:
			/* panel(????); */
		}
	} else	return 0;

	i = panel(PNL_PRPAGE);

	if (i > 0) {	/* 0: 取り消し、1: 全ページ、2: 今のページ */

		print_mode = (i == 1) ? 1 : 0;

		/* 印刷処理実行＝ライブラリ */
		doprint (NULL, mk_prfile);
	}
	return 0;
}
