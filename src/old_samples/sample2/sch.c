/*
	sch.c		電子手帳 : 予定表データの管理

	(C) Copyright 1997-98 by Personal Media Corporation
*/
#include "sched.h"

/*
 * 日付比較
 *	node の日付のほうが新しかったら ( node > date )   １
 *	同じ日付だったら		( node = date )   ０
 *	node の日付のほうが古かったら	( node < date ) ー１
 */
LOCAL	W	cmp_sch_date( SCH_NODE *node, DATE *date)
{
	if ( node->year  > date->year  ) return  1;
	if ( node->year  < date->year  ) return -1;

	if ( node->month > date->month ) return  1;
	if ( node->month < date->month ) return -1;

	if ( node->day	 > date->day   ) return  1;
	if ( node->day	 < date->day   ) return -1;

	return 0;
}

/*
 * ノードの追加
 *	np のノードの前にノードを追加する
 *	prev - np に prev - new - np のように追加する
 *	np == NULL の時には、最後のノードとして登録する
 *	新しいノードのアドレスを返す
 *	エラーのときは NULL を返す
 */
LOCAL	SCH_NODE*	add_sch_node( SCH_NODE *np,DATE *date)
{
	SCH_NODE	*new;

	/* メモリー確保 */
	if ( (new = alloc_sch_node()) == NULL ) return NULL;

	/* 日付設定 */
	new->year  = date->year;
	new->month = date->month;
	new->day   = date->day;

	if ( sch_data == NULL ) {
		/* ルートにリンク */
		new->next = new;
		new->prev = new;
		sch_data  = new;
	} else {
		/* リンクの先頭への接続なら、sch_data を新しいノードへ */
		if ( sch_data == np ) sch_data = new;

		if ( np == NULL ) np = sch_data; /* 最後へ接続 */

		/* ノードをリンク */
		(np->prev)->next = new;
		new->prev	 = np->prev;
		new->next	 = np;
		np->prev	 = new;
	}

	return new;
}

/*
 * ノードにデータを追加
 *	追加できれば TRUE を返す
 *	空きフィールドがなければ FALSE を返す
 */
LOCAL	BOOL	add_sch_field( SCH_NODE *np,TC *str)
{
	W		i;

	for ( i = 0; i < MAX_SCH; ++i ) {
		if ( *(np->data[i]) == TNULL ) {

			/* データ登録 */
			tc_strcpy(np->data[i], str);
			return TRUE;
		}
	}
	return FALSE;
}

/*
 * データ登録
 */
EXPORT	W	insert_sch(DATE *date,TC *str)
/* 予定日 */
/* 予定内容 */
{
	SCH_NODE	*np;
	W		i;

	if ( sch_data == NULL ) {
		/* まだ１つもノードがないので、新たにノードを追加する */
		if ( add_sch_node(NULL, date) == NULL ) return ER_NOMEM;
	}

	np = sch_data;
	do {
		if ( (i = cmp_sch_date(np, date)) < 0 ) continue;

		if ( i > 0 ) {	/* date より新しいノード */
			/* 新しいノードを追加 */
			np = add_sch_node(np, date);
			if ( np == NULL ) return ER_NOMEM;
		}

		/* フィールドへ登録 */
		if ( add_sch_field(np, str) ) return E_OK;
#if 1		/* MAX_SCH を超えたデータは登録しない */
		else return 1; /* データ数超過 */
#endif

	} while ( (np = np->next) != sch_data );

	/* date と同じ日のノードも、date より新しい日のノードもない */
	/* リンクの最後に新しいノードを追加 */
	if ( (np = add_sch_node(NULL, date)) == NULL ) return ER_NOMEM;

	/* フィールドへ登録 */
	add_sch_field(np, str);

	return E_OK;
}

/*
 * 今日の予定を含むノードをカレントとして設定する
 */
EXPORT	W	set_cur_sch(void)
{

	/* get_today_date(&todaydate); 今日の日付は、main.c で */

	return get_sch_node(&todaydate, &cur_sch);
}

/*
 * データクリア
 *	現在位置のデータを１ノード分クリアする
 */
EXPORT	BOOL	clear_sch(void)
{
	W	i;

	if ( cur_sch == NULL ) return FALSE; /* ？データがない？ */

	/* データクリア */
	for ( i = 0; i < MAX_SCH; ++i ) {
		*(cur_sch->data[i]) = TNULL;
	}
	return TRUE;
}
