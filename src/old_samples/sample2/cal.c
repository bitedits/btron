/*
	cal.c		電子手帳 : カレンダー &  休日データの管理

	(C) Copyright 1997-98 by Personal Media Corporation
*/
#include "sched.h"

/*
 * 休日のテスト
*/
LOCAL	BOOL	tst_holiday(DATE *date, W week)
{
	W		n, i;
	CAL_HOLIDAY	*hp = holiday;

	for (n = nholiday; n > 0; n--, hp++) {
		/* 年と月のチェック */
		if ( (hp->month > 0 && date->month != hp->month) ||
		     (hp->syear > 0 && (date->year < hp->syear ||
					date->year > hp->eyear)) ) continue;

		if (hp->week > 0) {	/* 曜日のチェック */
			if ((week & hp->week) == 0) continue;
			if (hp->day <= 0) return TRUE;
			/* 第ｎ曜日のチェック */
			i = hp->day * 7;
			if (date->day >= i - 6 && date->day <= i) return TRUE;
		} else {		/* 日のチェック */
			if (hp->day <= 0 || date->day == hp->day) return TRUE;
		}
	}
	return FALSE;
}
/*
 * 休日チェック
 *	date が休日ならば TRUE を返す
 */
EXPORT	BOOL	is_holiday( DATE *date)
{
	DATE		yd;
	W		week;
	UW		i;

	i = date_to_num(date);
	week = BIT_MASK(num_to_week(i));	/* 指定日(date)の曜日 */

	/* 指定日(date)が休日か？ */
	if (tst_holiday(date, week) == TRUE) return TRUE;

	/* 振替休日のチェック */
	if (week == 2) {
		num_to_date(&yd, --i);		/* 前日の日付 */
		if (tst_holiday(&yd, 0) == TRUE) {	/* 前日は休日 */
			/* 日曜は休日 ? */
			yd.year = yd.month = yd.day = 0;
			if (tst_holiday(&yd, 1) == TRUE) return TRUE;
		}
	}
	return FALSE;
}
/*
 * 一ヵ月分のカレンダーを作成する
 */
EXPORT	UW*	make_calendar(DATE *date)
/* day = 1 であること */
{
LOCAL	UW	cal[6][7];
	UW	*p;
	UW	week;
	UW	i, j;

	date->day = 1;
	week = num_to_week(date_to_num(date));	/* 一日の曜日 */
	j = month_day(date);			/* 一ヵ月の日数 */

	/* 日にち & 休日を設定 */
	memset(cal, 0, sizeof(cal));
	p = &cal[0][week];
	for (i = 1; i <= j; i++, p++) {
		date->day = *p = i;
		if (is_holiday(date)) *p |= M_HOLIDAY;
	}
	date->day = 1;
	return &cal[0][0];
}

/*
 * 休日データの設定
 *	str を解析して hp に設定する
 *	str が無効なデータ形式のときには FALSE を返す
 * データ形式
 *	ｙｙｙｙ／ｍｍ／ｄｄ	年・月・日
 *	ｗｗｗｗｗｗｗ		曜日
 *	年・月・日は 数値 または ＊ で、＊ は無指定(毎年・毎月・毎日)を示す
 *	曜日は、「日月火水木金土」の１文字以上の組み合わせ
 */
EXPORT	BOOL	set_holiday( CAL_HOLIDAY *hp,TC *str)
{
	W		w;

	/* 年・月・日 の形式？ */
	if (to_holiday_date(hp, str)) {
		return TRUE;
	}

	/* 曜日の形式？ */
	if ((w = str_to_weeks(str)) > 0) {
		hp->syear  = -1;
		hp->eyear  = -1;
		hp->month = -1;
		hp->day   = -1;
		hp->week  = w;
		return TRUE;
	}

	return FALSE;
}

/*
 * 休日データを文字列に変換
 */
EXPORT	VOID	cal_to_str(TC *str, CAL_HOLIDAY *hp)
{
	/* 曜日の形式？ */
	if (hp->week > 0 && hp->syear < 0 && hp->month < 0 && hp->day < 0) {
		/* 曜日の形式に変換 */
		weeks_to_str(str, hp->week);
	} else {
		/* 日付の形式に変換 */
		to_holiday_str(str, hp);
	}
}
