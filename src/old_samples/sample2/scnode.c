/*
	scnode.c	電子手帳 : 予定表のノード処理

	(C) Copyright 1997-98 by Personal Media Corporation
*/
#include "sched.h"
#include <btron/clk.h>

/*
 * 予定表のノード領域の確保
 */
EXPORT	SCH_NODE *alloc_sch_node(void)
{
	SCH_NODE	*newp;
	W		size;
	W		i;

	size = sizeof(SCH_NODE) + sizeof(TC) * (SCH_LEN+1) * MAX_SCH;

	if ((newp = (SCH_NODE*)malloc(size)) != (SCH_NODE*)NULL) {
		memset(newp, 0, size);
		newp->data[0] = (TC*)(newp + 1);
		for (i = 1; i < MAX_SCH; i++) {
			newp->data[i] = newp->data[i - 1] + (SCH_LEN + 1);
		}
	}
	return newp;
}

/*
 * 予定表のノード領域の解放
 */
EXPORT	VOID	free_sch_node(SCH_NODE *nodep)
{
	free(nodep);
}

/*
 * 予定表のノード追加
 */
EXPORT	SCH_NODE *insert_sch_node(SCH_NODE *nodep,W mode)
/* -1：nodeの前に追加、1：次に追加 */
{
	SCH_NODE	*newp, *tmpp;

	if ((newp = alloc_sch_node()) == NULL) return newp;

	if (sch_data == NULL) {
		newp->next = newp->prev = newp;
		sch_data = newp;
	} else {
		tmpp = (mode > 0) ? nodep : nodep->prev;
		newp->next = tmpp->next; tmpp->next = newp;
		newp->prev = newp->next->prev; newp->next->prev = newp;

		/* 最初のデータの前に追加する場合 */
		if (mode < 0 && sch_data == nodep) sch_data = newp;
	}
	return newp;
}

/*
 * 予定表のノード削除
 */
EXPORT	SCH_NODE *delete_sch_node(SCH_NODE *nodep)
{
	SCH_NODE	*nextp, *prevp;

	prevp = nodep->prev;
	nextp = nodep->next;

	/* ひとつしかノードがない場合 */
	if (sch_data == nodep && prevp == nodep && nextp == nodep) {
		nextp = prevp = (SCH_NODE*)NULL;
	} else {
		prevp->next = nextp;
		nextp->prev = prevp;
	}

	free_sch_node(nodep);

	/* 最初のデータを削除した場合 */
	if (sch_data == nodep) sch_data = nextp;
	return nextp;
}

/*
 * 予定表のノードサーチ
 *	datep の日付のノードのポインタ *nodepp を返す。
 *	戻値	-1：ノードがひとつもない(nodepp は設定しない)、
 *		 0：ノード有、
 *		 1：ノードなし(nodeppは、datepの後の最初のノード)、
 *		 2：ノードなし(nodeppは、datepの前のノード)
 */
EXPORT	W	search_sch_node(DATE *datep,SCH_NODE **nodepp)
{
	SCH_NODE	*tmpp;

	if ((tmpp = sch_data) == (SCH_NODE*)NULL) return -1;

	do {
		if ( tmpp->year < datep->year ) continue;
		if ( tmpp->year > datep->year ) goto EXIT;
		if ( tmpp->month < datep->month ) continue;
		if ( tmpp->month > datep->month ) goto EXIT;
		if ( tmpp->day < datep->day) continue;
		if ( tmpp->day > datep->day ) {
EXIT:
			*nodepp = tmpp;
			return 1;	/* ノードなし・前に挿入 */
		}

		/* ノード有 */
		*nodepp = tmpp;
		return 0;

	} while ( (tmpp = tmpp->next) != sch_data );

	*nodepp = tmpp->prev;
	return 2;			/* ノードなし・次に挿入 */
}

/*
 * 予定表のノードの取り出し
 */
EXPORT	W	get_sch_node(DATE *datep,SCH_NODE **nodepp)
{
	W		i;
	SCH_NODE	*schp;

	if ((i = search_sch_node( datep, &schp )) != 0) {
		i = (i == 1) ? -1 : 1;
		if ((schp = insert_sch_node( schp, i )) == (SCH_NODE*)NULL) {
			return ER_NOMEM;
		}
		schp->year = datep->year;
		schp->month = datep->month;
		schp->day = datep->day;
	}
	*nodepp = schp;
	return E_OK;
}

/*
 * 予定表のノード schp のデータが空か。(空：TRUE、有：FALSE)
 */
EXPORT	BOOL	is_empty_sch_node(SCH_NODE *schp)
{
	W	i;

	for ( i = 0; i < MAX_SCH; i++ ) {
		if (tc_strlen(schp->data[i]) > 0) return FALSE;
	}
	return TRUE;
}

/*
 * 今日の日付の取り出し
 */
EXPORT	VOID	get_today_date(DATE *datep)
{
	DATE_TIM	date_tim;

	get_tod( &date_tim, 0L, 1 );	/* 現在、ローカル時間 */

	datep->year = date_tim.d_year + 1900;
	datep->month = date_tim.d_month;
	datep->day = date_tim.d_day;
}
