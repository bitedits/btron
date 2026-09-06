/*
	save.c		電子手帳 : ファイルの保存

	(C) Copyright 1997-99 by Personal Media Corporation
*/

#include "sched.h"
#include <tlang.h>
#include <mtstring.h>

LOCAL	struct {	/* 管理情報セグメント */
	TC		id;
	UH		len;
	SEG_INFO	s;
} info_seg = {
	TS_INFO|0xff00, 6, {0, 2, {TAD_VERSION}}
};
#define	INFO_SEG_STRUCT	"hhhhh"

LOCAL	struct {	/* 文章開始セグメント */
	TC		id;
	UH		len;
	SEG_TEXT	tx;
} text_seg = {
	TS_TEXT|0xff00, 24,
	{{{0,0,0,0}}, {{0,0,0,0}}, DISPLAY_RESOLUTION, DISPLAY_RESOLUTION,
	 TAD_LANG_JAPANESE, 0}
};
#define	TEXT_SEG_STRUCT	"hhhhhhhhhhhhhh"

LOCAL	struct {	/* 文章終了セグメント */
	TC		id;
	UH		len;
} textend_seg = {
	TS_TEXTEND|0xff00, 0
};
#define	TEXTEND_SEG_STRUCT	"hh"

LOCAL	struct {	/* タブ書式セグメント */
	TC		id;
	UH		len;
	SEG_TAB		tx;
} tab_seg = {
	TS_TRULER|0xff00, 14,
	{0, 2,
	 DEF_PGAP, DEF_PGAP, DEF_LM, DEF_RM, DEF_IM, 0}
};
#define	TAB_SEG_STRUCT	"hhbbhhhhhh"

/*
 * タブ書式セグメントの書き出し
 */
LOCAL
	W	put_tabseg(void)
{
	W	ret;

	ConvEndianStruct((B*)&tab_seg, (B*)&tab_seg, TAB_SEG_STRUCT, sizeof(tab_seg));
	ret = rputseg((B*)&tab_seg, tg_fp);
	ConvEndianStruct((B*)&tab_seg, (B*)&tab_seg, TAB_SEG_STRUCT, sizeof(tab_seg));
	return ret;
}

/* システムスクリプトコードの書き出し */
LOCAL void
put_sys_script (RFILE* file)
{
	rputc (ConvEndianH (TSC_SYS|0xFE00), file);
}

/*
 * 予定表データを書き出す
 */
/*LOCAL*/
	W	save_sch(void)
{
	DATE		date;			/* 予定日(数値) */
	TC		str[DATE_LEN+1];	/* 予定日(文字列) */
	TC		buf[SCH_LEN+1];
	SCH_NODE	*np;
	W		i;
	W length;
	W lang;

	put_tabseg(); /* タブ書式セグメント書き出し */

	if ( (np = sch_data) == NULL ) return E_OK; /* データなし */

	do {
		/* 日付を文字列に変換 */
		date.year  = np->year;
		date.month = np->month;
		date.day   = np->day;
		date_to_str(str, &date);
		ConvEndianHs(str, str, DATE_LEN+1);

		for ( i = 0; i < MAX_SCH; ++i ) {
			if ( *(np->data[i]) == TNULL ) continue;

			/* 予定表データ書き出し */

			length = mtc_unique (buf, np->data [i], sizeof (buf)/sizeof (buf [0]) - 1);
			if (length == 0) {
				continue;
			}
			buf [length] = TNULL;
			lang = mtc_poslang (buf, buf + length - 1);
			rputs((B*)str, tg_fp);
			rputc(ConvEndianH(TC_TAB), tg_fp);
			ConvEndianHs(buf, buf, SCH_LEN+1);
			rputs((B*)buf, tg_fp);
			if (lang > 0 && lang != TSC_SYS) {
				/* システムスクリプト以外で終了している文字列 */
				put_sys_script (tg_fp);
			}
			rputc(ConvEndianH(TC_NL), tg_fp);
		}

	} while ( !rerror(tg_fp) && (np = np->next) != sch_data );

	return rerror(tg_fp);
}

LOCAL
	VOID	swap_puts(TC *str)
{
	TC	buf[1024];
	W length;
	W lang;

	length = mtc_unique (buf, str, sizeof (buf) / sizeof (buf [0]) - 1);
	if (length == 0) {
		return;
	}
	buf [length] = TNULL;
	lang = mtc_poslang (buf, buf + length - 1);
	ConvEndianHs(buf, buf, sizeof (buf));
	rputs((B*)buf, tg_fp);
	if (lang > 0 && lang != TSC_SYS) {
		/* システムスクリプト以外で終了している文字列 */
		put_sys_script (tg_fp);
	}
}

/*
 * 住所録データを書き出す
 */
/*LOCAL*/
	W	save_adr(void)
{
	ADR_NODE	*np;
	ADR_DATA	*data;		/* 住所１件分のデータ */
	W		i;

	put_tabseg(); /* タブ書式セグメント書き出し */

	if ( (np = adr_data) == NULL ) return E_OK; /* データなし */

	do {
		for ( i = 0; i < MAX_ADR; ++i ) {
			if ( (data = np->data[i]) == NULL ) continue;

			/* 住所録データの書き出し */
			swap_puts(data->yomi);	rputc(ConvEndianH(TC_TAB), tg_fp);
			swap_puts(data->yubin); rputc(ConvEndianH(TC_TAB), tg_fp);
			swap_puts(data->addr1); rputc(ConvEndianH(TC_TAB), tg_fp);
			swap_puts(data->addr2); rputc(ConvEndianH(TC_TAB), tg_fp);
			swap_puts(data->addr3); rputc(ConvEndianH(TC_TAB), tg_fp);
			swap_puts(data->name);	rputc(ConvEndianH(TC_TAB), tg_fp);
			swap_puts(data->tel);	rputc(ConvEndianH(TC_TAB), tg_fp);
			swap_puts(data->fax);	rputc(ConvEndianH(TC_TAB), tg_fp);
			swap_puts(data->memo1); rputc(ConvEndianH(TC_TAB), tg_fp);
			swap_puts(data->memo2); rputc(ConvEndianH(TC_NL),  tg_fp);
		}

	} while ( !rerror(tg_fp) && (np = np->next) != adr_data );

	return rerror(tg_fp);
}

/*
 * 休日データを書き出す
 */
/*LOCAL*/
	W	save_cal(void)
{
	CAL_HOLIDAY	*hp;
	W		n;
	TC		str[DATE_LEN * 2];	/* 休日(文字列) */

	put_tabseg(); /* タブ書式セグメント書き出し */

	if ( (hp = holiday) == NULL ) return E_OK; /* データなし */

	for ( n = nholiday; n > 0; --n, ++hp ) {

		/* 休日を文字列に変換 */
		cal_to_str(str, hp);

		/* 休日データ書き出し */
		ConvEndianHs(str, str, DATE_LEN * 2);
		rputs((B*)str, tg_fp);
		rputc(ConvEndianH(TC_NL), tg_fp);

		if ( rerror(tg_fp) ) break;
	}

	return rerror(tg_fp);
}

/*
 * ブロック書き出し
 */
/*LOCAL*/
	W	write_tg_fp(B *buf,W size)
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
/*LOCAL*/
	W	put_header(void)
{
	/* 管理情報セグメントの書き出し */
	ConvEndianStruct((B*)&info_seg, (B*)&info_seg, INFO_SEG_STRUCT, sizeof(info_seg));
	rputseg((B*)&info_seg, tg_fp);
	ConvEndianStruct((B*)&info_seg, (B*)&info_seg, INFO_SEG_STRUCT, sizeof(info_seg));

	/* 文章開始セグメントの書き出し */
	ConvEndianStruct((B*)&text_seg, (B*)&text_seg, TEXT_SEG_STRUCT, sizeof(text_seg));
	rputseg((B*)&text_seg, tg_fp);
	ConvEndianStruct((B*)&text_seg, (B*)&text_seg, TEXT_SEG_STRUCT, sizeof(text_seg));

	/* 用紙関連付箋の書き出し */
	saveform(write_tg_fp, 1);

	return rerror(tg_fp);
}

/*
 * トレーラーの書き出し
 */
/*LOCAL*/
	W	put_trailer(void)
{
	/* タブ書式セグメントの書き出し */
	put_tabseg();

	/* 文章終了セグメントの書き出し */
	ConvEndianStruct((B*)&textend_seg, (B*)&textend_seg, TEXTEND_SEG_STRUCT, sizeof(textend_seg));
	rputseg((B*)&textend_seg, tg_fp);
	ConvEndianStruct((B*)&textend_seg, (B*)&textend_seg, TEXTEND_SEG_STRUCT, sizeof(textend_seg));

	return rerror(tg_fp);
}

/*
 * 書き込みレコードの作成 ＆ そのレコードへの位置付け
 */
/*LOCAL*/
	W	creat_rec(void)
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
 * 旧データレコードの削除
 */
/*LOCAL*/
	W	del_old_rec(void)
{
	W	err;
	W	fd = rfileno(tg_fp);

	/* 現在レコードが新規に保存したデータなので、
	   次のレコード以降のデータレコードを削除する */

	if ( (err = see_rec(fd, 1L, 0, NULL)) < E_OK ) return err;

	while ( (err = fnd_rec(fd, F_FWD, RM_TADDATA, 0, NULL)) >= E_OK ) {
		if ( del_rec(fd) < E_OK ) see_rec(fd, 1L, 0, NULL);
	}

	/* 削除の途中でエラーが起きても復旧できないので、正常終了とする */
	return E_OK;
}

/*
 * 実身を保存する
 */
/*LOCAL*/
	W	sc_save(void)
{
	W		err;
	W		newrec;		/* 新規作成した保存用レコードの番号 */

	/* 書き込むレコードの作成 */
	if ( (err = creat_rec()) < E_OK ) return err;
	newrec = rrecordno(tg_fp);

	/* ヘッダの書き出し */
	if ( (err = put_header()) < E_OK ) goto err_exit;

	/* 予定表データを書き出す */
	if ( (err = save_sch()) < E_OK ) goto err_exit;

	/* 住所録データを書き出す */
	if ( (err = save_adr()) < E_OK ) goto err_exit;

	/* 休日データを書き出す */
	if ( (err = save_cal()) < E_OK ) goto err_exit;

	/* トレーラーの書き出し */
	if ( (err = put_trailer()) < E_OK ) goto err_exit;

	/* 書き込みバッファフラッシュ */
	if ( (err = rflush(tg_fp)) < E_OK ) goto err_exit;

	/* 旧データレコードの削除 */
	if ( (err = del_old_rec()) < E_OK ) goto err_exit;

	return err;

err_exit:
	/* 書き込んだデータレコードを削除 */
	if ( see_rec(rfileno(tg_fp), newrec, 1, NULL) >= E_OK ) {
		del_rec(rfileno(tg_fp));
	}

	return err;
}

/*
 * 保存元ファイル変更確認
 *	ファイルが他のプログラムによって書き換えられていないか確認する
 */
/*LOCAL*/
	W	change_ok(void)
{
	F_STATE		sts;
	W		err;

	/* ファイルステータスの取り出し */
	err = ofl_sts(rfileno(tg_fp), NULL, &sts, NULL);
	if ( err < E_OK ) return err;

	if ( tg_sts.f_mtime == sts.f_mtime ) return E_OK; /* 更新OK */

	/* 元の内容は他のアプリケーションにより変更されています。
	   変更すると他からの変更内容は廃棄されますがよいですか？ */
	if ( panel(PNL_CUPD) <= 0 ) return 1; /* 取消 */
	return E_OK; /* 更新OK */
}

/*
 * 既存ファイルのオープン
 */
/*LOCAL*/
	W	open_upfile(void)
{
	W	fd;

	fd = oopn_obj(mycmd->vid, NULL, F_UPDATE|F_EXCL, NULL);
	if ( fd < E_OK ) return fd;

	tg_fp = rfdopen(fd, (UW)RM_TADDATA, F_UPDATE|F_EXCL);
	return ropen_err;
}

/*
 * 新規ファイルのオープン
 */
/*LOCAL*/
	W	open_newfile(void)
{
	W	fd;
	TC	fname[20+1];

	fname[0] = TNULL;
	setpointer(PS_BUSY, NULL);

	/* ファイル名入力パネルの表示、及び新規ファイルのオープン */
	if ( (fd = ocre_obj(mycmd->vid, fname, NULL, NULL, 1)) < E_OK ) {
		setpointer(PS_SELECT, NULL);
		if ( fd == EX_PAR ) return 1; /* 新規ファイル作成を取り消す */
		errpanel(ER_WRITE, fd);
		return fd;
	}

	tg_fp = rfdopen(fd, (UW)RM_TADDATA, F_UPDATE);
	return ropen_err;
}

/*
 * 終了・更新・新規保存 の処理
 *	mode	bit 0	０＝既存ファイルへの更新／１＝新規ファイルへの保存
 *		bit 1	書き込み確認パネル表示 ０＝なし／１＝あり
 *		bit 2	保存元ファイル変更確認 ０＝なし／１＝あり
 *		bit 3	書き込み禁止時の確認パネル表示 ０＝なし／１＝あり
 *	retuen	E_OK	正常終了
 *		1	書き込み取消
 *		< E_OK	エラー (エラーステータス)
 */
EXPORT	W	save_file( W mode)
{
	W	err, cls_err;

	/* ファイルシステムの接続
	 * 実身のあるファイルシステムが切り離されてしまっている場合には、
	 * 接続を行う。(FD などの挿入)
	 */
	if ( (err = oatt_vob(mycmd->vid, 0)) < E_OK ) return err;
	if ( err == 0 ) return 1; /* 取消 */

	if ( (mode & 1) == 0 ) {
		/* 書き込み確認：保存ファイルは書き込み可能か */
		if ( chk_fil(&mycmd->lnk, F_WRITE, NULL) < E_OK ) {
			/* 書き込みが禁止されている
			   新規ファイルに保存しますか？ */
			if ( (mode & 8) == 0 || panel(PNL_ERSAVE) <= 0 )
							return 1; /* 取消 */
			mode |= 1; /* 新規ファイルに保存 */
		} else {
			/* 元の内容を破棄して更新しますか？ */
			if ( (mode & 2) != 0 && panel(PNL_UPDATE) <= 0 )
							return 1; /* 取消 */
		}
	}

	/* 既存／新規ファイルのオープン */
	err = ( (mode & 1) == 0 )? open_upfile(): open_newfile();
	if ( err != E_OK ) return err;

	if ( (mode & 5) == 4 ) {
		/* 保存元ファイルの変更確認 */
		if ( (err = change_ok()) != E_OK ) {
			rclose(tg_fp);
			return err;
		}
	}

	/* 書き込み */
	sysmsg(MSG_SAVE);	/* 「書き込み中です。...」 */
	if ( (err = sc_save()) >= E_OK ) {
		if ( (mode & 1) == 0 ) {
			/* 既存ファイルへ保存した：ファイル情報の更新 */
			err = ofl_sts(rfileno(tg_fp), NULL, &tg_sts, NULL);

			modified = FALSE; /* 編集状態を変更なしに設定 */
			tg_update = 1; /* 実身が変更された */
		}
	}
	cls_err = rclose(tg_fp);
	if(err >= 0 && cls_err < 0) err = cls_err; /* !@ 980728 */
	sysmsg(0);
	if ( err < E_OK ) errpanel(ER_WRITE, err);

	return ( err > E_OK )? E_OK: err;
}

/*
 * 終了処理
 *	mode	０：通常終了 (確認パネルを表示する)
 *		１：強制終了 (確認パネルを表示せずに、保存して終了)
 *	return	TRUE ：終了ＯＫ
 *		FALSE：終了拒否
 */
EXPORT	BOOL	sc_exit(W mode)
{
	F_STATE		sts;
	BOOL		update;
	W		upmode;
	W		pnl;
	W		n;

	update = modified; /* 編集されているか */
	upmode = 0x0c;
	pnl    = PNL_CLOSE;

	if ( !update && (mode & 1) == 0 ) {
		/* 元ファイルが変更されているか確認 */
		if ( fil_sts(&mycmd->lnk, NULL, &sts, NULL) >= E_OK ) {
			update = ( tg_sts.f_mtime != sts.f_mtime );
			upmode = 0x08;
			pnl = PNL_CHK; /* 現在の内容で再度更新しますか？ */
		}
	}
	if ( !update ) return TRUE; /* 保存不要：終了 */

	do {
		if ( (mode & 1) == 0 ) {
			/* 終了確認パネル処理 */
			n = panel(pnl);
			if ( n <= 0 ) return FALSE; /* 終了拒否 */
			if ( n == 1 ) return TRUE;  /* 破棄して終了 */
		}

		/* 保存 */
		n = save_file(upmode);

		if ( (mode & 1) != 0 ) return TRUE; /* 強制終了 */

		/* 保存できなかった場合に、再度終了パネルを表示 */
		pnl = PNL_XCLOSE;

	} while ( n < E_OK ); /* 入出力エラーが起きたら、もう一度 */

	if ( n > 0 ) return FALSE; /* 終了拒否 */
	return TRUE; /* 保存終了 */
}
