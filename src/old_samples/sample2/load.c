/*
	load.c		電子手帳 : ファイルの読み出し

	(C) Copyright 1997-99 by Personal Media Corporation
*/

#include "sched.h"
#include <tlang.h>
#include <mtstring.h>

EXPORT	BOOL	nosupport = FALSE;	/* ファイルに未サポートデータが
					   含まれていたとき TRUE */
EXPORT	BOOL	illegal = FALSE;	/* ファイルに無効データが
					   含まれているときに TRUE */

#define	READBUF_SIZE	(64)		/* 文章付箋セグメントのヘッダ部分が
					   入るぐらいの適当な大きさ */

LOCAL	W	textvres;	/* 画面解像度 */

/*
 * セグメント全体が読み出されているか確認し、
 * すべてが読み出されていなければ読み直す
 * 読み直しが必要な場合には必要なバッファを malloc する
 * 読み直しが不要ならば０、読み直しを行ったら１を返す
 * エラーが起きたときにはエラーステータスを返す
 */
/* LOCAL */
	W	read_check( sp )
	B	**sp;
{
	B	*seg = *sp;
	W	s;

	/* セグメントサイズの確認 */
	if ( isLSEG(seg) ) {
		s = ConvEndianW(((LSEG*)seg)->len) + 8;
	} else {
		s = ConvEndianH(((SEG*)seg)->len) + 4;
	}
	if ( s <= READBUF_SIZE ) return E_OK; /* 読み直し不要 */

	if ( s >= 0x8000L ) return ER_SZOVR; /* 大きすぎて読めない */

	/* 読み直し用メモリーの確保 */
	if ( (seg = (B*)malloc(s)) == NULL ) return ER_NOMEM;

	/* １セグメント読み直し */
	if ( runget(tg_fp) < E_OK || rgetseg(seg, (W)s, tg_fp) != s ) {
		free(seg);
		return rerror(tg_fp);
	}

	*sp = seg;
	return 1; /* 読み直した */
}

/*
 * 文章開始セグメントの読み出し
 */
/* LOCAL */
	W	load_start_text( seg )
	B		*seg;
{
	W		err;
	SEG_TEXT	*textseg;

	/* 読み込み確認 */
	if ( (err = read_check(&seg)) < E_OK ) return err;

	if ( isLSEG(seg) ) {
		textseg = (SEG_TEXT*)&((LSEG*)seg)->tx;
	} else {
		textseg = (SEG_TEXT*)&((SEG*)seg)->tx;
	}
	ConvEndianStruct((B*)textseg, (B*)textseg, SEG_TEXT_STRUCT, sizeof(SEG_TEXT));

	/* 解像度を保存 */
	textvres = textseg->v_unit;

	if ( err > 0 ) free(seg);

	return E_OK;
}

/*
 * 文章ページ割付け指定付箋の読み出し
 */
/* LOCAL */
	W	load_pageform( seg )
	B	*seg;
{
	W	err;
	B	*bp;
	W	len;
	PNT	unit;

	/* 読み込み確認 */
	if ( (err = read_check(&seg)) < E_OK ) return err;

	if ( isLSEG(seg) ) {
		bp = (B*)&((LSEG*)seg)->tx;
		len = ((LSEG*)seg)->len;
	} else {
		bp = (B*)&((SEG*)seg)->tx;
		len = ((SEG*)seg)->len;
	}

	/* レイアウト用紙設定 */
	unit.x = unit.y = textvres;
	switch ( loadform(bp, len, unit) ) {
	  case 0:	break;
	  case -1:	nosupport = TRUE; break;
	  default:	illegal   = TRUE;
	}

	if ( err > 0 ) free(seg);

	return E_OK;
}

/*
 * セグメントの種類を確認し、必要なセグメントは読み出す
 * エラーステータスを返す
 */
/* LOCAL */
	W	chk_loadseg( seg, top )
	B	*seg;
	W	top;	/* 1 = ファイル先頭部分の読み出し
			   2 = ファイル終端部分の読み出し */
{
	TC	id;

	if ( (id = ConvEndianH(*(TC*)seg)) < 0xff80 ) return E_OK;

	switch ( id & 0x00ff ) {
	  case TS_INFO:
	  case TS_TEXTEND:
	  case TS_TRULER:
		break;
	  case TS_TEXT:
		return load_start_text(seg);
	  case TS_TPAGE:
		return load_pageform(seg);
	  default:
		nosupport = TRUE; /* 未サポートデータがある */
	}

	return E_OK;
}

/*
 * タブ書式セグメントまで読み飛ばす
 */
/* LOCAL */
	W	skip_first()
{
	B	buf[READBUF_SIZE+1];	/* 文章付箋セグメントのヘッダ部分が
					   入るぐらいの適当な大きさ */
	W	err;

	/* エラーかファイル終端まで */
	while ( !(rerror(tg_fp) || reof(tg_fp)) ) {

		/* １セグメント読み出し */
		if ( rgetseg(buf, sizeof(buf), tg_fp) <= 0 ) continue;

		/* セグメントの種類確認 */
		if ( (err = chk_loadseg(buf, 1)) < E_OK ) return err;

		/* タブ書式セグメントなら終り */
		if ( istabseg(buf) ) break;
	}
	return rerror(tg_fp);
}

/*
 * ファイル終端まで読み飛ばす
 */
/* LOCAL */
	W	skip_to_eof()
{
	B	buf[READBUF_SIZE+1];	/* 文章付箋セグメントのヘッダ部分が
					   入るぐらいの適当な大きさ */
	W	err;

	/* エラーかファイル終端まで */
	while ( !(rerror(tg_fp) || reof(tg_fp)) ) {

		/* １セグメント読み出し */
		if ( rgetseg(buf, sizeof(buf), tg_fp) <= 0 ) continue;

		/* セグメントの種類確認 */
		if ( (err = chk_loadseg(buf, 2)) < E_OK ) return err;
	}
	return rerror(tg_fp);
}

/*
 * フィールド読み出し
 * tg_fp から文字だけを、タブ または 改段落 に出会うか、最大 n 文字に
 * なるまで str に読み出す
 * タブ または 改段落 に出会う前に n 文字に達した場合には、
 * 残りの文字は読み捨てられる
 * 入力エラーが発生するか、ファイルの終端に達するか、フィールドの先頭で
 * タブ書式セグメントにであったらリターン値として 0xffff を返す
 * それ以外は、フィールドの区切りとなった タブ または 改段落 のコードを返す
 * なお、タブ および 改段落 は TNULL に置き換えられ、
 * n 文字に達した場合には n+1 文字目に TNULL が入れられる
 */
/* LOCAL */
TC	read_field(
	TC	*str,
	W	n)
{
	B	buf[READBUF_SIZE+1];	/* 文章付箋セグメントのヘッダ部分が
					   入るぐらいの適当な大きさ */
	TC	ch;
	W	i = 0;
	W	length;
	W code;
	TC *multi_tlang = 0;
	W illegal_mode = 0;
	static W work = 0;	/* 関数呼びだしを越えて保持する必要がある */
	static W lang = TSC_SYS;

	/* エラーかファイル終端まで */
	while ( !(rerror(tg_fp) || reof(tg_fp)) ) {

		/* １セグメント読み出し */
		if ((length = rgetseg(buf, sizeof(buf), tg_fp)) <= 0 ) continue;

		if ( isTC(buf) ) {
			ch = ConvEndianH(*(TC*)buf);

			/* フィールド終端確認 */
			if ( ch == TC_TAB || ch == TC_NL ) {
				goto ret;
			}

			/* 最大文字数をオーバーしていなければ、読み出す */
			if (ch < 0xfeff) {
				code = isTLANGch (ch, &work);
				if (code != 0) {
					if (code > 0) {
						illegal_mode = 0;
						multi_tlang = 0;
						if (lang == code) {
							/* 冗長言語指定 */
							continue;
						}
						lang = code;
					} else if (code == -2) {
						/* 複数バイト言語指定 */
						illegal_mode = 0;
						if (!multi_tlang) {
							multi_tlang = str;
						}
					} else /*if (code == -1)*/ {
						illegal_mode = 1;
						illegal = TRUE;
						continue;
					}
				}
				if (i < n) {
					if (i == 0) {
						if (ch == (TSC_SYS|0xFE00)) {
							/* 最初のシステムスクリプト指定は冗長 */
							continue;
						} else if (lang != TSC_SYS && code == 0) {
							/* 以前の言語指定が有効である */
							W lang_length = TLANGtoTC (0, 0, lang);
							if (n - 1 <= lang_length) {
								/* 格納できないほど長いスクリプト指定コードは
								   不正コードとみなす */
								illegal_mode = 1;
							} else {
								TLANGtoTC (str, lang_length, lang);
								str += lang_length;
								i += lang_length;
							}
						}
					}
					/* 不正言語指定のときはその間じゅう文字を無視する */
					if (!illegal_mode) {
						*str++ = ch;
						i++;
					}
				} else if (code == 0) {

					illegal = TRUE;
				}
			} else {
				illegal = TRUE; /* 無効データがある */
			}
		} else {
			/* タブ書式セグメントなら終了 */
			if ( i == 0 && istabseg(buf) ) break;
			nosupport = TRUE; /* 未サポートデータがある */
		}
	}
	ch = 0xffff;
ret:
	if (multi_tlang) {
		/* 完結しない複数バイト言語指定があった */
		str = multi_tlang;
		illegal = TRUE;
		work = 0;
	}
	if (illegal_mode) {
		/* フィールドを越えた不正状態はないものとする。
		   でないと、一部の不正言語指定の影響がファイル終端までおよんで不便なはず。*/
		work = 0;
	}
	*str = TNULL;
	return ch;
}

/*
 * 予定表データを読み出す
 */
/* LOCAL */
	W	load_sch()
{
	TC	dstr[DATE_LEN+1];	/* 予定日(文字列) */
	DATE	date;			/* 予定日(数値) */
	TC	str[SCH_LEN+1];		/* 予定内容 */
	TC	ch;
	W	err;

	/* 予定表データの終りまで */
	while ( (ch = read_field(dstr, DATE_LEN)) != 0xffff ) {
		if ( ch != TC_TAB ) {
			illegal = TRUE;	/* 無効データがある */
			continue;
		}

		/* 日付文字列を数値に変換 */
		if ( !str_to_date(&date, dstr) ) {
			illegal = TRUE;	/* 無効データがある */
			continue;
		}

		do {
			/* 予定読み込み */
			ch = read_field(str, SCH_LEN);
			if ( ch == 0xffff ) return E_OK; /* 予定表データ終り */

			/* 写身にデータ登録 */
			if ( (err = insert_sch(&date,str)) < E_OK ) return err;
			if ( err > E_OK ) {
				/* データ数超過 */
				illegal = TRUE; /* 無効データがある */
			}

		} while ( ch == TC_TAB );
	}

	return rerror(tg_fp);
}

/*
 * 住所録データを読み出す
 */
/* LOCAL */
	W	load_adr()
{
	ADR_DATA	*data;		/* 住所１件分のデータ */
	TC		yomi[YOMI_LEN+1]; /* 「よみ」の一時読み込み領域 */
	TC		ch;
	W		err;

	/* 住所録データの終りまで */
	while ( (ch = read_field(yomi, YOMI_LEN)) != 0xffff ) {

		/* データメモリー確保 ＆ 初期化 */
		if ( (data = alloc_adr_data()) == NULL ) return ER_NOMEM;

		/* データ読み込み */
		tc_strcpy(data->yomi, yomi);
		(void) ( ch == TC_TAB
		 && (ch = read_field(data->yubin, YUBIN_LEN)) == TC_TAB
		 && (ch = read_field(data->addr1, ADDR1_LEN)) == TC_TAB
		 && (ch = read_field(data->addr2, ADDR2_LEN)) == TC_TAB
		 && (ch = read_field(data->addr3, ADDR3_LEN)) == TC_TAB
		 && (ch = read_field(data->name,  NAME_LEN )) == TC_TAB
		 && (ch = read_field(data->tel,   TEL_LEN  )) == TC_TAB
		 && (ch = read_field(data->fax,   FAX_LEN  )) == TC_TAB
		 && (ch = read_field(data->memo1, MEMO1_LEN)) == TC_TAB
		 && (ch = read_field(data->memo2, MEMO2_LEN)) == TC_NL
		);

		if ( ch == TC_TAB ) {
			/* 無効データを読み飛ばす */
			while ( (ch = read_field(yomi, YOMI_LEN)) == TC_TAB );

			illegal = TRUE; /* 無効データがある */
		}

		/* 写身にデータ登録 */
		if ( (err = insert_adr(data)) < E_OK ) return err;

		if ( ch == 0xffff ) break; /* 住所録データ終り */
	}

	return rerror(tg_fp);
}

/*
 * 休日データを読み出す
 */
/* LOCAL */
	W	load_cal()
{
	TC		str[DATE_LEN * 2];	/* 休日(文字列) */
	CAL_HOLIDAY	*hp;
	TC		ch;
	W		n = 0;

	/* メモリー確保 */
	hp = (CAL_HOLIDAY*)malloc(lsizeof(CAL_HOLIDAY)*MAX_HOLIDAY);
	if ( hp == NULL ) return ER_NOMEM;
	holiday = hp;

	/* 休日データの終りまで */
	while ( (ch = read_field(str, DATE_LEN * 2)) != 0xffff ) {

		if ( n >= MAX_HOLIDAY ) {
			/* 最大休日数をオーバーした */
			illegal = TRUE;
			continue;
		}

		/* 休日の設定 */
		if ( !set_holiday(hp, str) ) {
			illegal = TRUE;	/* 無効データがある */
			continue;
		}

		hp++; n++;

		if ( ch == TC_TAB ) {
			/* 無効データを読み飛ばす */
			while ((ch = read_field(str, DATE_LEN * 2)) == TC_TAB);

			illegal = TRUE; /* 無効データがある */
		}
		if ( ch == 0xffff ) break; /* 休日データ終り */
	}

	if ( n > 0 ) {
		/* 確保したメモリーのサイズを調整する */
		hp = (CAL_HOLIDAY*)realloc(holiday, lsizeof(CAL_HOLIDAY)*n);
		if ( hp == NULL ) return ER_NOMEM;
	} else {
		/* 休日データが１つもない */
		free(holiday);
		hp = NULL;
	}
	holiday = hp;
	nholiday = n;

	return rerror(tg_fp);
}

/*
 * 実身を読み出して、写身を構築する
 */
EXPORT	W	sc_load()
{
	W		err;

	nosupport = illegal = FALSE;
	textvres = DISPLAY_RESOLUTION;

	/* 写身の初期化 */
	sch_data = cur_sch = NULL;
	adr_data = cur_adr = NULL;
	cur_adr_ofs = 0;
	holiday = NULL;
	nholiday = 0;

	/* 先頭レコードへ移動 */
	if ( (err = rrewind(tg_fp)) < E_OK ) return err;
	if ( reof(tg_fp) ) goto load_ok;

	/* 最初のタブ書式セグメントまで読み飛ばす */
	if ( (err = skip_first()) < E_OK ) {
		return err;
	}
	if ( reof(tg_fp) ) goto load_ok;

	/* 予定表データを読み出す */
	err = load_sch();
	if ( err < E_OK ) return err;

	/* 住所録データを読み出す */
	err = load_adr();
	if ( err < E_OK ) return err;

	/* 休日データを読み出す */
	err = load_cal();
	if ( err < E_OK ) return err;

	/* ファイル終端まで読み飛ばす */
	if ( (err = skip_to_eof()) < E_OK ) return err;

load_ok:
	/* 各データの初期設定 */
	if ( (err = set_cur_sch()) < E_OK ) return err;
	if ( (err = set_cur_adr()) < E_OK ) return err;

	return err;
}
