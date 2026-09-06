/*
	lib.c		電子手帳 : 各種のライブラリ関数群

	(C) Copyright 1997-98 by Personal Media Corporation
*/
#include "sched.h"

/*
 * 基準日からの通算日数：UW days
 *
 * 日数のカウント方法は、B-TRON仕様に基づき１９８５年１月１日を基準(０日)とする
 *
 *	   January 1985
 *	Su Mo Tu We Th Fr Sa
 *	       1  2  3	4  5
 *	 6  7  8  9 10 11 12
 *	13 14 15 16 17 18 19
 *	20 21 22 23 24 25 26
 *	27 28 29 30 31
 */

#define	MINUS		(0x215d)	/* 「−」 */
#define	ASTERISK	(0x2176)	/* 「＊」 */
#define	SUN		(0x467c)	/* 「日」日曜 */
#define	MON		(0x376e)	/* 「月」月曜 */
#define	TUE		(0x3250)	/* 「火」火曜 */
#define	WED		(0x3f65)	/* 「水」水曜 */
#define	THU		(0x4c5a)	/* 「木」木曜 */
#define	FRI		(0x3662)	/* 「金」金曜 */
#define	SAT		(0x455a)	/* 「土」土曜 */
#define	NEN		(0x472f)	/* 「年」 */
#define	TSUKI		(0x376e)	/* 「月」 */
#define	HI		(0x467c)	/* 「日」 */
#define	LKAKKO		(0x214a)	/* 「（」左括弧 */
#define	RKAKKO		(0x214b)	/* 「）」右括弧 */

LOCAL	TC	wk[] = {SUN,MON,TUE,WED,THU,FRI,SAT,TNULL};

/* 年初めから各月の１日までの通算日数 (２月は２８日で計算) */
LOCAL	UW	month_days[13] = {
     /* 1   2	3   4	 5    6    7	8    9	 10   11   12 月 */
	0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365
};

/*
 *	文字列を数値に変換
 */
LOCAL	W	atos(TC *str)
{
	TC	*tail;
	W	num;
	num = tc_strtol(str, &tail, 10);
	return num;
}

/*
 *	文字位置を検索
 */
LOCAL	W	strpos(TC *str, TC c)
{
	TC	*p;

	if ((p = tc_strchr(str, c)) == NULL) return -1;
	return p - str;
}

/*
 * 閏年ならば TRUE を返す
 */
LOCAL	BOOL	is_uruudoshi( W year)
{
	return (((year % 4) == 0 && (year % 100) != 0) || (year % 400) == 0);
}

/*
 * 一ヵ月の日数を返す
 */
EXPORT	UW	month_day(DATE *date)
{
	W	month = date->month;
	UW	days;

	days = month_days[month] - month_days[month-1];
	if ( month == 2 && is_uruudoshi(date->year) ) days++;

	return days;
}

/*
 * 基準日から指定年の１月１日までの通算日数を返す
 */
LOCAL	UW	year_to_num( W year)
{
	UW	i;

	/* 多少時間がかかるが、単純に積算するのが一番安全 */
	for (i = 0; --year >= 1985; i += is_uruudoshi(year) ? 366 : 365);
	return i;
}

/*
 * 基準日からの通算日数を返す
 */
EXPORT	UW	date_to_num( DATE *date)
{
	UW	days;
	UW	i;

	/* 年 */
	days = year_to_num(date->year);

	/* 月 */
	i = ( date->month > 2 && is_uruudoshi(date->year) )? 1: 0;
	days += month_days[date->month-1] + i;

	/* 日 */
	days += date->day - 1;

	return days;
}

/*
 * 通算日数から年月日を求める
 */
EXPORT	VOID	num_to_date( DATE *date,UW num)
{
	UW	i;

	/* 年 */
	date->year = num / 365 + 1985;
	i = year_to_num(date->year);
	if ( num < i ) i = year_to_num(--date->year);
	num -= i;

	/* 月 */
	i = ( is_uruudoshi(date->year) )? 1: 0;
	if ( num >= month_days[2]+i ) {
		/* ３月以降 */
		num -= i;
		for ( i = 3; i <= 12; ++i ) {
			if ( num < month_days[i] ) break;
		}
	} else {
		/* ２月以前 */
		i = ( num < month_days[1] )? 1: 2;
	}
	date->month = i;

	/* 日 */
	date->day = num - month_days[i-1] + 1;
}

/*
 * 通算日数から曜日を求める
 *	0:日、1:月、2:火、3:水、4:木、5:金、6:土
 */
EXPORT	W	num_to_week(UW days)
{
	return (days + 2) % 7;
}

/*
 * 日付から曜日を求める
 *	0:日、1:月、2:火、3:水、4:木、5:金、6:土
 */
EXPORT	W	date_to_week(DATE *date)
{
	return num_to_week(date_to_num(date));
}

/*
 * 指定の月の一日の曜日を求める
 *	0:日、1:月、2:火、3:水、4:木、5:金、6:土
 */
EXPORT	W	get_week1st(DATE date)
/* ！ポインタではないので注意！ */
{
	date.day = 1;
	return date_to_week(&date);
}

/*
 * short integer の数値を文字列に変換
 *	左詰めで変換
 *	変換された文字列の最後のTNULLの位置へのポインタを返す
 */
EXPORT	TC*	stoa(TC *str, W num)
{
	W	i;

	if ( num < 0 ) {
		*str++ = MINUS;
		num = -num;
	}

	for ( i = 10000; i > 1; i /= 10 ) if ( num >= i ) break;

	for ( ; i > 0; i /= 10 ) {
		*str++ = 0x2330 + (num / i);
		num %= i;
	}

	*str = TNULL;

	return str;
}

/*
 * short integer の数値を文字列に変換
 *	len 桁の右詰めで変換
 */
EXPORT	TC*	stoal(TC *str, W num, W len)
{
	TC	*ep;
	BOOL	minus = FALSE;

	ep = str += len;
	*str-- = TNULL;

	if ( num < 0 ) {
		num = -num;
		minus = TRUE;
	}

	while ( len-- > 0 ) {
		*str-- = 0x2330 + (num % 10);
		if ( (num /= 10) == 0 ) break;
	}

	if ( minus && len-- > 0 ) *str-- = MINUS;

	while ( len-- > 0 ) *str-- = TK_KSP; /* 空白を詰める */

	return ep;
}

/*
 * 文字列の曜日データをマスク値に変換
 */
EXPORT	W	str_to_weeks( TC *str)
{
	W	i;
	W	m = 0;
	TC	ch;

	while ( (ch = *str++) != TNULL ) {
		if ( (i = strpos(wk, ch)) < 0L ) break;
		m |= BIT_MASK((W)i);
	}
	return m;
}

/*
 * 曜日マスクデータを文字列に変換
 */
EXPORT	TC*	weeks_to_str( TC *str, W w)
{
	W	i;

	for ( i = 0; i < 7; ++i ) {
		if ( (w & 1) != 0 ) *str++ = wk[i];
		w >>= 1;
	}
	*str = TNULL;
	return str;
}

/*
 * 文字列の日付データを数値に変換
 */
EXPORT	BOOL	to_date( DATE *date,TC *str)
{
	TC	*p;

	/* 「年」の取り出し */
	p = str;
	str = tc_strchr(p, DATE_SEP);
	if ( str == NULL ) return FALSE;
	date->year = ( p[0] == ASTERISK )? -1: atos(p);

	/* 「月」の取り出し */
	p = str+1;
	str = tc_strchr(p, DATE_SEP);
	if ( str == NULL ) return FALSE;
	date->month = ( p[0] == ASTERISK )? -1: atos(p);

	/* 「日」の取り出し */
	p = str+1;
	date->day = ( p[0] == ASTERISK )? -1: atos(p);

	return TRUE;
}
/*
 * 休日の文字列の日付データを数値に変換
 *	ｙｙｙｙ／ｍｍ／ｄｄ	年・月・日
 *	ｗｗｗｗｗｗｗ		曜日
 *	年・月・日は 数値 または ＊ で、＊ は無指定(毎年・毎月・毎日)を示す
 *	曜日は、「日月火水木金土」の１文字以上の組み合わせ
 *	ｙｙｙｙ :	ｙｙｙｙ		: ｙ年
 *			ｓｓｓｓ−ｅｅｅｅ	: ｓ年〜ｅ年
 *	ｄｄ:		ｄｄ			: ｄ日
 *			ｎ曜			: 第ｎ曜日
 */
EXPORT	BOOL	to_holiday_date( CAL_HOLIDAY *hp, TC *str)
{
	TC	*p;
	W	m;

	/* 「年」の取り出し */
	p = str;
	str = tc_strchr(p, DATE_SEP);
	if ( str == NULL ) return FALSE;
	if ( p[0] == ASTERISK ) {
		hp->syear = hp->eyear = -1;
	} else {
		hp->syear = hp->eyear = tc_strtol(p, &p, 10);
		if (*p == TK_MINS) {
			hp->eyear = tc_strtol(p + 1, &p, 10);
			if (hp->eyear < hp->syear) hp->eyear = hp->syear;
		}
	}

	/* 「月」の取り出し */
	p = str + 1;
	str = tc_strchr(p, DATE_SEP);
	if ( str == NULL ) return FALSE;
	hp->month = ( p[0] == ASTERISK ) ? -1: atos(p);

	hp->week = 0;

	/* 「日」の取り出し */
	p = str + 1;
	if ( p[0] == ASTERISK ) hp->day = -1;
	else {
		hp->day = tc_strtol(p, &p, 10);
		if ((m = str_to_weeks(p)) != 0) hp->week = m;
	}
	return TRUE;
}
/*
 * 数値を文字に変換
 * ただし、０以下は「＊」とする
 */
LOCAL	TC*	tostr( TC *str,W num)
{
	if ( num > 0 ) {
		str = stoa(str, num);
	} else {
		*str++ = ASTERISK;
		*str = TNULL;
	}
	return str;
}

/*
 * 日付データを文字列に変換
 * ｙｙｙｙ／ｍｍ／ｄｄ の形式に変換
 */
EXPORT	TC*	to_str( TC *str, DATE *date)
/* 10文字分の領域が必要 */
{
	str = tostr(str, date->year);	*str++ = DATE_SEP;
	str = tostr(str, date->month);	*str++ = DATE_SEP;
	str = tostr(str, date->day);

	return str;
}

/*
 * 休日日付データを文字列に変換
 * ｙｙｙｙ／ｍｍ／ｄｄ の形式に変換
 * ｙｙｙｙ−ｙｙｙｙ／ｍｍ／ｄｄ の形式に変換
 */
EXPORT	TC*	to_holiday_str( TC *str, CAL_HOLIDAY *hp)
{
	str = tostr(str, hp->syear);
	if (hp->eyear != hp->syear) {
		*str++ = TK_MINS;
		str = tostr(str, hp->eyear);
	}
	*str++ = DATE_SEP;
	str = tostr(str, hp->month);
	*str++ = DATE_SEP;
	str = tostr(str, hp->day);
	if (hp->week > 0) weeks_to_str(str, hp->week);
	return str;
}

/*
 * 日付データを文字列に変換
 * ｙｙｙｙ年ｍｍ月ｄｄ日（ｗ） の形式に変換
 */
EXPORT	TC*	date_to_jstr(TC *str,DATE *date)
/* 14文字分の領域が必要 */
{
	str = stoal(str, date->year,  4);	*str++ = NEN;
	str = stoal(str, date->month, 2);	*str++ = TSUKI;
	str = stoal(str, date->day,   2);	*str++ = HI;
	*str++ = LKAKKO;
	*str++ = wk[date_to_week(date)];
	*str++ = RKAKKO;
	*str = TNULL;

	return str;
}

/*
 * 日付データを文字列に変換
 * ｙｙｙｙ年ｍｍ月  平成ｈｈ年 の形式に変換
 */
EXPORT	TC*	date_to_kstr(TC *str,DATE *date)
/* 14文字分の領域が必要 */
{
LOCAL	TC	nengou[2] = { 0x4a3f,0x402e /* 平成 */ };

	str = stoal(str, date->year, 4);	*str++ = NEN;
	str = stoal(str, date->month, 2);	*str++ = TSUKI;
	*str++ = TK_KSP;

	tc_strcpy(str, nengou); str += 2;
	str = stoal(str, date->year-1988, 2);	*str++ = NEN;
	*str = TNULL;

	return str;
}

/*
 * 日付の範囲チェック
 */
EXPORT	BOOL	chk_date( DATE *date)
{
	static W ndate [] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	/*			↑閏年でない場合どうする？ */

	if ( date->year < FIRST_YEAR || date->year > LAST_YEAR ) return FALSE;
	if ( date->month < 1 || date->month > 12 ) return FALSE;
	if ( date->day	 < 1 || ndate [date->month -1] < date->day ) return FALSE;
	return TRUE;
}

/*
 * 文字列の日付データを数値に変換
 * 日付文字列のフォーマット：ｙｙｙｙ／ｍｍ／ｄｄ
 */
EXPORT	BOOL	str_to_date(DATE *date,TC *str)
{
	return to_date(date, str) && chk_date(date);
}

/*
 * 日付を文字列に変換
 * 日付文字列のフォーマット：ｙｙｙｙ／ｍｍ／ｄｄ
 */
EXPORT	BOOL	date_to_str( TC *str, DATE *date)
{
	*str = TNULL;

	if ( !chk_date(date) ) return FALSE;

	str = stoa(str, date->year);	*str++ = DATE_SEP;
	str = stoa(str, date->month);	*str++ = DATE_SEP;
	str = stoa(str, date->day);

	return TRUE;
}

/*
 * カタカナをひらがなに変換
 */
EXPORT	TC	tohira( TC ch)
{
	if ( !tc_iskata(ch) ) return ch;	/* カタカナ以外の文字 */

	if ( ch >= 0x2574 ) return ch;	/* 「ヴヵヶ」はそのまま */

	if (tc_ischoon(ch)) return ch;	/* 長音もそのまま */

	return ch - 0x0100;
}

/*
 * タブ書式セグメントであるか確認する
 */
EXPORT	BOOL	istabseg(B *seg)
{
	if ( isTC(seg) ) return FALSE;

	if ( isLSEG(seg) ) {
		if ( isTABSEG((LSEG*)seg) ) return TRUE; /* タブ書式 */
	} else {
		if ( isTABSEG((SEG*)seg) ) return TRUE; /* タブ書式 */
	}
	return FALSE;
}

/*
 *	旧仕様 DP 関数 gdra_chp
 */
W
gdra_chp(GID gid, W x, W y, TC ch, DCM mode)
{
	gset_chp(gid, x, y, True);
	gdra_chr(gid, ch, mode);
	return 1;
}

/*
 *	旧仕様 DP 関数 gdra_stp
 */
W
gdra_stp(GID gid, W x, W y, TC *str, W len, DCM mode)
{
	gset_chp(gid, x, y, True);
	return gdra_str(gid, str, len, mode);
}
