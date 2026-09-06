/*
	search.c	電子手帳 : 検索

	(C) Copyright 1997-98 by Personal Media Corporation
*/

#include "sched.h"
#include <tlang.h>
#include <mtstring.h>
#include <wtstring.h>

#define	MAXLEN	MAX_TBLEN	/* 検索最大文字数 */

#define	isLOWER(c)	(((c) & 0xffe0) == 0x2360)	/* 英小文字？ */
#define	isHIRA(c)	(((c) & 0xff80) == 0x2400)	/* ひらがな？ */
#define	isLGREEK(c)	(((c) & 0xffe0) == 0x2640)	/* ギリシャ小文字？ */
#define	isLRUSSIAN(c)	((((c)-0x10) & 0xffc0) == 0x2740)	/* ロシア小文字？ */

/*
 * 検索比較用標準文字へ変換
 *	英小文字は大文字に変換
 *	ひらがなはカタカナに変換
 */
LOCAL	TC	std_ch( TC c)
{
	if ( isLOWER(c)    ) c = c - 0x0020;
	if ( isHIRA(c)	   ) c = c + 0x0100;
	if ( isLGREEK(c)   ) c = c - 0x0020;
	if ( isLRUSSIAN(c) ) c = c - 0x0030;
	return c;
}

/*
 * 検索比較用標準文字列への変換
 */
LOCAL	W	to_std(WTC *dst, TC *src, W dst_length)
{
	W script = TSC_SYS;
	W bad_script = 0;
	W code;
	WTC* begin = dst;
	WTC* end = dst + dst_length;
	TC* cur = src;

	while (dst < end && *cur != TNULL) {
		code = isTLANG (cur, 0, &cur);
		if (code > 0) {
			script = code;
			bad_script = 0;
		} else if (code == 0 && !bad_script) {
			TC ch = *cur++;
			if (script == TSC_SYS) {
				if (tc_isspace (ch)) {
					continue;
				}
				ch = std_ch (ch);
			}
			*dst++ = towtc (ch < TK_KSP ? 0 : script, ch);
		} else {
			bad_script = 1;
		}
	}
	*dst = towtc (0, 0);
	return dst - begin;
}

/*
 *	検索
 */
LOCAL	W	strsch(WTC *s1, WTC *s2)
{
	W n, l, m;
	W found = -1;

	for (n = 0; s1 [n] != 0; n++) {
		if (s1 [n] == s2 [0]) {
			l = n + 1;
			m = 1;
			while (s1 [l] != 0 && s2 [m] != 0 && s1 [l++] == s2 [m]) {
				m++;
			}
			if (s2 [m] == 0) {
				found = 0;
				break;
			}
		}
	}
	return found;
}

/*
 * 文字列の検索
 */
LOCAL	BOOL	search_str(TC *s1,WTC *s2)
/* s1 : 非標準文字列 */
/* s2 : 標準文字列(検索文字列) */
{
	WTC	s[MAXLEN+1];

	/* 標準文字列に変換 */
	to_std(s, s1, MAXLEN);

	/* サーチ */
	return ( strsch(s, s2) >= 0L );
}

/* ------------------------------------------------------------------------ */

/*
 * 予定表の検索
 */
LOCAL	W	search_sch(TC *str,W mode)
{
	SCH_NODE	*np;
	W		i;
	WTC wstr [MAXLEN+1];

	if ( sch_data == NULL ) return -1; /* データがない */

	/* 検索開始位置の設定 */
	if ( mode == 0 ) {
		/* 最初のページから */
		np = sch_data;
	} else {
		/* 次のページから */
		np = cur_sch->next;
		if ( np == sch_data ) return -1; /* 次のページはない */
	}

	/* 瞹昧検索を行うために、標準文字に変換する */
	to_std (wstr, str, MAXLEN);

	do {
		for ( i = 0; i < MAX_SCH; ++i ) {
			if ( search_str(np->data[i], wstr) ) {
				/* 見つかった：カレント位置変更 */
				cur_sch = np;
				return i;
			}
		}
	} while ( (np = np->next) != sch_data );

	return -1; /* 見つからなかった */
}

/* ------------------------------------------------------------------------ */

/* フィールド名リストの構造 */
typedef	struct {
	UW		fmask;		/* マスク */
	TC		key[1];		/* フィールド名 (文字数任意の文字列) */
} FIELD_NAME;

LOCAL	W		*field_names;	/* フィールド名のリスト */

#define	FLD_SEP		(0x2127)	/* 「：」フィールド指定の区切り文字 */

/*
 * 検索フィールド名のリストをデータボックスから読み出す
 */
EXPORT	BOOL	get_field_names(void)
{
	field_names = (W*)getdbox(FLD_NAMES);

	return ( field_names != NULL );
}

/*
 * 検索フィールドの指定を調べる
 *	検索欄の指定文字列の分だけ str を進める
 */
LOCAL	UW	get_adr_field(TC **str)
{
	FIELD_NAME	*fp;
	TC		*s = *str;
	W		len;
	W		n;
	W		i = 0;

	while ( (n = field_names[i++]) != 0 ) {
		fp = (FIELD_NAME*)ptrdbox(n);

		/* フィールド指定のチェック */
		len = tc_strlen(fp->key);
		if ( tc_strncmp(s, fp->key, len) == 0 && s[len] == FLD_SEP ) {
			*str += len + 1;
			return fp->fmask; /* フィールド指定あり */
		}
	}

	/* フィールド指定がないので、全フィールドを検索 */
	return 0xffff;
}

/*
 * 指定フィールドの検索
 */
LOCAL	W	search_adr_data( ADR_DATA *dp, WTC *str, UW field)
{
	if ( (field & 0x0001) != 0 && search_str(dp->yomi,  str) ) return 0;
	if ( (field & 0x0002) != 0 && search_str(dp->yubin, str) ) return 1;
	if ( (field & 0x0004) != 0 && search_str(dp->addr1, str) ) return 2;
	if ( (field & 0x0008) != 0 && search_str(dp->addr2, str) ) return 3;
	if ( (field & 0x0010) != 0 && search_str(dp->addr3, str) ) return 4;
	if ( (field & 0x0020) != 0 && search_str(dp->name,  str) ) return 5;
	if ( (field & 0x0040) != 0 && search_str(dp->tel,   str) ) return 6;
	if ( (field & 0x0080) != 0 && search_str(dp->fax,   str) ) return 7;
	if ( (field & 0x0100) != 0 && search_str(dp->memo1, str) ) return 8;
	if ( (field & 0x0200) != 0 && search_str(dp->memo2, str) ) return 9;
	return -1;
}

/*
 * 住所録の検索
 */
LOCAL	W	search_adr(TC *str, W mode)
{
	ADR_NODE	*np;
	W		i, n;
	UW		field;
	WTC wstr [MAXLEN+1];

	/* 検索フィールドの確認 */
	field = get_adr_field(&str);
	to_std (wstr, str, MAXLEN);

	if ( mode == 0 ) {
		/* 最初のページから検索 */
		np = adr_data;
		i  = 0;
	} else {
		/* 次のページから検索 */
		np = cur_adr;
		i  = cur_adr_ofs+1;
	}
	if ( np == NULL ) return -1; /* ？データがない？ */

	do {
		while ( i < MAX_ADR ) {
			if ( np->data[i] != NULL ) {
				/* 検索 */
				n = search_adr_data(np->data[i], wstr, field);
				if ( n >= 0 ) {
					/* 見つかった：カレント位置変更 */
					cur_adr     = np;
					cur_adr_ofs = i;
					calc_adr_page(); /* ページ番号再計算 */
					return n;
				}
			}
			++i;
		}
		i = 0;
	} while ( (np = np->next) != adr_data );

	return -1; /* 見つからなかった */
}

/* ------------------------------------------------------------------------ */

/*
 * 検索機能の入り口
 *	検索が正しく行われたときには、見つけたフィールドの番号(０〜)を返す
 *	見つからなかったり、検索を実行できなかったときには負数を返す
 */
EXPORT	W	sc_search(W ix,TC *str,W mode)
/* 検索対象データの種類 (SCH_WIN|ADR_WIN) */
/* 検索文字列 */
/* ０：最初のページから検索／１：次のページから検索 */
{
	if ( str[0] == TNULL ) return -1; /* 検索文字列がない */

	switch ( ix ) {
	  case SCH_WIN:	return search_sch(str, mode);
	  case ADR_WIN:	return search_adr(str, mode);
	}
	return -1;
}
