/*
	adr.C		電子手帳 : 住所録データの管理

	(C) Copyright 1997-98 by Personal Media Corporation
*/
#include "sched.h"

/*
 * 見出し単位のページ番号
 *	カレントデータを含む見出しグループのページ番号情報
 */
LOCAL	struct {
	W	no;	/* 同じ見出しグループ内でのページ番号 */
	W	all;	/* 同じ見出しグループ内のページ数 */
} cur_page;

/*
 * データメモリーの確保
 */
EXPORT	ADR_DATA*	alloc_adr_data(void)
{
	ADR_DATA	*dp;

	/* メモリー確保 */
	dp = (ADR_DATA*)malloc(sizeof(ADR_DATA));

	if ( dp != NULL ) {
		/* データクリア */
		memset(dp, 0, sizeof(ADR_DATA));
	}

	return dp;
}

/*
 * 読みからインデックス文字を得る
 *	読みの最初の文字がひらがなの場合にはその文字、
 *	カタカナの場合にはひらがなに変換した文字、
 *	その他の文字の場合には TNULL が返される
 */
LOCAL	TC	get_yomi_index(TC *yomi)
{
	TC	yo;

	yo = tohira(yomi[0]);

	if ( tc_ishira(yo) ) return yo;
	return TNULL;
}

/*
 * インデックス文字の比較
 *	ひらがなは文字コード順に比較
 *	他の文字(TNULL)はひらがなより大きい
 *	node の文字の方が大きかったら ( node->index > yo )   １
 *	等しければ		      ( node->index = yo )   ０
 *	node の方が小さければ	      ( node->index < yo ) −１
 */
LOCAL	W	cmp_adr_index(ADR_NODE *node, TC yo)
{
	TC		ch;

	if (node == 0) {
		return 1;
	}
	ch = node->index;

	if ( ch == yo ) return 0;
	if ( yo == TNULL ) return -1;
	if ( ch == TNULL ) return 1;
	if ( ch < yo ) return -1;
	return 1;
}

/*
 * よみの比較
 *	カタカナはひらがなに変換して比較する
 *	yomi1 >  yomi2 の時には > 0
 *	yomi1 == yomi2 の時には = 0
 *	yomi1 <  yomi2 の時には < 0
 *	を返す
 *	なお、ヌルストリングは最も大きいものとする。
 */
LOCAL	W	cmp_adr_yomi(TC *yomi1,TC *yomi2)
{
	TC	c1, c2;

	c1 = *yomi1;
	c2 = *yomi2;
	if ( c1 == TNULL || c2 == TNULL ) {

		/* ヌルストリングに対する判定 */
		if ( c1 == c2 ) return 0;
		return ( c1 == TNULL )? 1: -1;
	}

	/* 通常の判定 */
	do {
		c1 = tohira(*yomi1++);
		c2 = tohira(*yomi2++);
		if ( c1 > c2 ) return  1;
		if ( c1 < c2 ) return -1;
	} while ( c1 != TNULL );
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
LOCAL	ADR_NODE*	add_adr_node( ADR_NODE *np,TC yo)
{
	ADR_NODE	*new;
	W		i;

	/* メモリー確保 */
	new = (ADR_NODE*)malloc(lsizeof(ADR_NODE));
	if ( new == NULL ) return NULL;

	/* 初期設定 */
	new->index = yo;
	for ( i = 0; i < MAX_ADR; ++i ) new->data[i] = NULL;

	if ( adr_data == NULL ) {
		/* ルートにリンク */
		new->next = new;
		new->prev = new;
		adr_data  = new;
	} else {
		/* リンクの先頭への接続なら、adr_data を新しいノードへ */
		if ( adr_data == np ) adr_data = new;

		if ( np == NULL ) np = adr_data; /* 最後へ接続 */

		/* ノードをリンク */
		(np->prev)->next = new;
		new->prev	 = np->prev;
		new->next	 = np;
		np->prev	 = new;
	}

	return new;
}

/*
 * ノードの削除
 *	np のノードを削除する
 *	np には既にデータは一つもないことを前提としている
 *	np のノードを削除した結果、ノードが一つもなくなったときには
 *	cur_adr を NULL にして FALSE を返す
 *	それ以外では cur_adr は変化せず TRUE が返される
 */
LOCAL	BOOL	del_adr_node( ADR_NODE *np)
{
	/* 残り１つのノードか */
	if ( np->next == np ) adr_data = cur_adr = NULL;

	if ( np == adr_data ) {
		/* 先頭ノードを次のノードに移す */
		adr_data = np->next;
	}

	/* ノード削除 */
	(np->next)->prev = np->prev;
	(np->prev)->next = np->next;
	free(np);

	return ( cur_adr != NULL );
}

/*
 * 次のデータの「よみ」との比較
 *	次のデータのよみの方が後にくる場合	> 0
 *	よみが等しい場合			= 0
 *	次のデータのよみの方が前にくる場合	< 0
 *	を返す
 */
LOCAL	W	cmp_next( ADR_NODE *np, W i,TC *yomi)
/* np->data[i] が比較開始位置 */
/* 比較する「よみ」 */
{
	ADR_DATA	*dp;
	TC		yo;

	/* インデックス文字を取り出す */
	yo = get_yomi_index(yomi);

	/* 次のデータを探す */
	do {
		/* インデックス文字を比較 */
		if ( cmp_adr_index(np, yo) > 0 ) return 1;

		while ( i < MAX_ADR ) {
			if ( (dp = np->data[i]) != NULL ) {
				/* 次のデータが見つかった：文字列を比較 */
				return cmp_adr_yomi(dp->yomi, yomi);
			}
			++i;
		}
		i = 0;
	} while ( (np = np->next) != adr_data );

	return 1; /* 次のデータはない */
}

/*
 * ノードにデータを追加
 *	「よみ」が昇順になるようにデータを追加する
 *	正常に追加できれば NULL を返す
 *	追加した結果、ノードからはみ出すデータがあった場合には、
 *	そのデータのポインタを返す
 */
LOCAL	ADR_DATA*	add_adr_data( ADR_NODE *np,ADR_DATA *data)
{
	ADR_DATA	*dp;
	W		i;

	for ( i = 0; i < MAX_ADR; ++i ) {

		/* よみの比較
		 * ノードに登録されているデータが、追加するデータより
		 * 後ろにくる場合には、そこへデータを挿入する
		 * ノードに空きがあった場合には、次に登録されているデータが
		 * 追加するデータより後にくるならば、そこへデータを追加する
		 */
		if ( ( np->data[i] == NULL )?
			cmp_next(np, i+1, data->yomi) > 0 :
			cmp_adr_yomi((np->data[i])->yomi, data->yomi) > 0
		) {
			/* データ登録 */
			for ( ; i < MAX_ADR; ++i ) {
				dp = np->data[i];
				np->data[i] = data;
				data->node = np;
				if ( (data = dp) == NULL ) break;
				data->node = NULL;
			}
			return data;
		}
	}

	return data;
}

/*
 * データ登録
 */
EXPORT	W	insert_adr(ADR_DATA *data)
/* 住所データ */
{
	TC		yo;	/* 読みの最初の１文字(インデックス文字) */
	ADR_NODE	*np;
	W		i;

	/* インデックス文字を取り出す */
	yo = get_yomi_index(data->yomi);

	if ( adr_data == NULL ) {
		/* まだ１つもノードがないので、新たにノードを追加する */
		if ( add_adr_node(NULL, yo) == NULL ) return ER_NOMEM;
	}

	np = adr_data;
	do {
		if ( (i = cmp_adr_index(np, yo)) < 0 ) continue;

		if ( i > 0 ) {	/* yo より後ろのノード */
			/* 新しいノードを追加 */
			np = add_adr_node(np, yo);
			if ( np == NULL ) return ER_NOMEM;
		}

		/* 住所データを登録 */
		if ( (data = add_adr_data(np, data)) == NULL ) return E_OK;

	} while ( (np = np->next) != adr_data );

	/* yo と同じインデックスのノードも、yo より後のノードもない */
	/* リンクの最後に新しいノードを追加 */
	if ( (np = add_adr_node(NULL, yo)) == NULL ) return ER_NOMEM;

	/* 住所データを登録 */
	add_adr_data(np, data);

	return E_OK;
}

/*
 * 見出し番号に変換
 */
EXPORT	W	midashi_idx( TC yo)
{
	if ( yo < 0x2421 ) return 10; /* 他 */
	if ( yo < 0x242b ) return  0; /* あ */
	if ( yo < 0x2435 ) return  1; /* か */
	if ( yo < 0x243f ) return  2; /* さ */
	if ( yo < 0x244a ) return  3; /* た */
	if ( yo < 0x244f ) return  4; /* な */
	if ( yo < 0x245e ) return  5; /* は */
	if ( yo < 0x2463 ) return  6; /* ま */
	if ( yo < 0x2469 ) return  7; /* や */
	if ( yo < 0x246e ) return  8; /* ら */
	if ( yo < 0x2473 ) return  9; /* わ */
			   return 10; /* 他 */
}

/*
 * 見出し文字に変換
 */
EXPORT	TC	midashi_char(TC yo)
{
	TC*	midashi = (TC*) getdbox (MIDASHI);

	return midashi[midashi_idx(yo)];
}

/*
 * カレントデータのページ番号を再計算
 */
EXPORT	BOOL	calc_adr_page(void)
{
	ADR_NODE	*np = adr_data;
	W		idx;
	W		i;

	cur_page.no  = 0;
	cur_page.all = 0;

	/* 現在のインデックス番号を取り出す */
	idx = midashi_idx(cur_adr->index);

	do {
		i = midashi_idx(np->index);
		if ( i < idx ) continue;
		if ( i > idx ) break;

		/* 同じ見出しのグループ内なのでカウントする */
		for ( i = 0; i < MAX_ADR; ++i ) {
			if ( np->data[i] != NULL ) {
				/* ページ数カウント */
				cur_page.all++;
				if ( np == cur_adr && i == cur_adr_ofs ) {
					/* 現在ページ番号の設定 */
					cur_page.no = cur_page.all;
				}
			}
		}
	} while ( (np = np->next) != adr_data );

	return TRUE;
}

/*
 * 指定の見出し文字の最初のデータに位置付ける
 * 指定のデータがなければ FALSE を返す
 *	idx = 0:あ 1:か ... 9:わ 10:他
 */
EXPORT	BOOL	set_adr_page(W idx,W mode)
/* ページ番号の再計算：０＝なし／１＝あり */
{
	ADR_NODE	*np = adr_data;
	W		i;

	if ( adr_data == NULL ) return FALSE; /* データがない */

	do {
		if ( midashi_idx(np->index) == idx ) {
			for ( i = 0; i < MAX_ADR; ++i ) {
				if ( np->data[i] != NULL ) {
					/* カレント位置設定 */
					cur_adr     = np;
					cur_adr_ofs = i;

					/* ページ番号再計算 */
					if ( (mode & 1) != 0 ) {
						calc_adr_page();
					}
					return TRUE; /* 移動完了 */
				}
			}
		}
	} while ( (np = np->next) != adr_data );

	return FALSE; /* 指定の見出しのデータがない */
}

/*
 * 指定のデータをカレントに設定する
 */
LOCAL	BOOL	set_cur_data(ADR_DATA *dp)
{
	ADR_NODE	*np;
	W		i;

	np = dp->node;
	if ( np == NULL ) return FALSE; /* ノードに登録されていない */

	for ( i = 0; i < MAX_ADR; ++i ) {
		if ( np->data[i] == dp ) {
			/* カレントに設定 */
			cur_adr = np;
			cur_adr_ofs = i;
			calc_adr_page(); /* ページ番号再計算 */
			return TRUE; /* 設定完了 */
		}
	}
	return FALSE; /* ？見つからない */
}

/*
 * np, i で指定された位置から、順に後ろに進んでデータのある位置に位置付ける
 * データがなければ FALSE を返す
 */
LOCAL	BOOL	next_find( ADR_NODE *np, W i)
{
	do {
		while ( i < MAX_ADR ) {
			if ( np->data[i] != NULL ) {
				cur_adr     = np;
				cur_adr_ofs = i;
				return TRUE; /* 移動完了 */
			}
			++i;
		}
		i = 0;
	} while ( (np = np->next) != adr_data );

	return FALSE; /* データがない */
}

/*
 * np, i で指定された位置から、順に前に戻ってデータのある位置に位置付ける
 * データがなければ FALSE を返す
 */
LOCAL	BOOL	prev_find( ADR_NODE *np, W i)
{
	ADR_NODE	*p;

	do {
		while ( i >= 0 ) {
			if ( np->data[i] != NULL ) {
				cur_adr     = np;
				cur_adr_ofs = i;
				return TRUE; /* 移動完了 */
			}
			--i;
		}
		i = MAX_ADR-1;
		p = np; np = np->prev;
	} while ( p != adr_data );

	return FALSE; /* データがない */
}

/*
 * 次のページに位置付ける
 * ページの移動がなければ FALSE を返す
 */
EXPORT	BOOL	next_adr_page(void)
{
	W	idx;

	if ( adr_data == NULL ) return FALSE; /* データがない */

	idx = midashi_idx(cur_adr->index); /* 現在の見出し番号 */

	/* ページ移動 */
	if ( !next_find(cur_adr, cur_adr_ofs+1) ) return FALSE;

	/* ページ番号の更新 */
	if ( idx == midashi_idx(cur_adr->index) ) {
		cur_page.no++;
	} else {
		/* ページ番号の再計算 */
		calc_adr_page();
	}

	return TRUE;
}

/*
 * 前のページに位置付ける
 * ページの移動がなければ FALSE を返す
 */
EXPORT	BOOL	prev_adr_page(void)
{
	W	idx;

	if ( adr_data == NULL ) return FALSE; /* データがない */

	idx = midashi_idx(cur_adr->index); /* 現在の見出し番号 */

	/* ページ移動 */
	if ( !prev_find(cur_adr, cur_adr_ofs-1) ) return FALSE;

	/* ページ番号の更新 */
	if ( idx == midashi_idx(cur_adr->index) ) {
		cur_page.no--;
	} else {
		/* ページ番号の再計算 */
		calc_adr_page();
	}

	return TRUE;
}

/*
 * 最初のページに位置付ける
 * ページの移動がなければ FALSE を返す
 */
EXPORT	BOOL	first_adr_page(void)
{
	if ( adr_data == NULL ) return FALSE; /* データがない */

	/* ページ移動 */
	if ( !next_find(adr_data, 0) ) return FALSE;

	/* 見出し番号の再計算 */
	calc_adr_page();

	return TRUE;
}

/*
 * 最後のページに位置付ける
 * ページの移動がなければ FALSE を返す
 */
EXPORT	BOOL	last_adr_page(void)
{
	if ( adr_data == NULL ) return FALSE; /* データがない */

	if ( !prev_find(adr_data->prev, MAX_ADR-1) ) return FALSE;

	/* 見出し番号の再計算 */
	calc_adr_page();

	return TRUE;
}

/*
 * 新規登録用データを設定
 */
EXPORT	W	set_new_adr(void)
{
	ADR_DATA	*dp;
	W		err;

	/* メモリー確保 */
	if ( (dp = alloc_adr_data()) == NULL ) return ER_NOMEM;

	/* ノードに登録 */
	if ( (err = insert_adr(dp)) < E_OK ) {
		free(dp);
		return err;
	}

	/* 登録したデータをカレントに設定 */
	set_cur_data(dp);

	modified = TRUE; /* 編集された */
	return E_OK;
}

/*
 * データクリア
 *	現在位置のデータをクリアする
 */
LOCAL	BOOL	clear_adr_data(void)
{
	ADR_DATA	*dp;

	if ( cur_adr == NULL ) return FALSE; /* ？データがない？ */
	dp = cur_adr->data[cur_adr_ofs];
	if ( dp == NULL ) return FALSE; /* ？データがない？ */

	/* データクリア */
	dp->yomi[0]  = TNULL;	dp->yubin[0] = TNULL;
	dp->addr1[0] = TNULL;	dp->addr2[0] = TNULL;
	dp->addr3[0] = TNULL;	dp->name[0]  = TNULL;
	dp->tel[0]   = TNULL;	dp->fax[0]   = TNULL;
	dp->memo1[0] = TNULL;	dp->memo2[0] = TNULL;

	return TRUE;
}

/*
 * データが空か？
 */
LOCAL	BOOL	is_data_empty( ADR_DATA *dp)
{
	return (
		dp->yomi[0]  == TNULL && dp->yubin[0] == TNULL
	     &&	dp->addr1[0] == TNULL && dp->addr2[0] == TNULL
	     &&	dp->addr3[0] == TNULL && dp->name[0]  == TNULL
	     &&	dp->tel[0]   == TNULL && dp->fax[0]   == TNULL
	     &&	dp->memo1[0] == TNULL && dp->memo2[0] == TNULL
	);
}

/*
 * データをノードから切り離す
 * 切り離したデータへのポインタを返す
 */
LOCAL	ADR_DATA*	remove_data( ADR_NODE *np,W ofs)
{
	ADR_DATA	*dp = np->data[ofs];
	W		i;

	if ( dp != NULL ) {
		/* ノードから切り離す */
		np->data[ofs] = NULL;
		dp->node = NULL;
	}

	/* ノードの空いた分を詰める */
	for ( i = ofs+1; i < MAX_ADR; ++i ) {
		if ( np->data[i] != NULL ) {
			np->data[ofs++] = np->data[i];
			np->data[i] = NULL;
		}
	}

	if ( ofs == 0 ) {
		/* ノードにデータが一つもなくなったので、ノードも削除する */
		del_adr_node(np);
	}

	return dp;
}

/*
 * カレントノードのインデックスを更新
 */
LOCAL	BOOL	update_cur_index(void)
{
	ADR_DATA	*dp;

	if ( cur_adr == NULL ) return FALSE; /* データなし */

	dp = cur_adr->data[cur_adr_ofs];
	if ( dp == NULL ) return FALSE; /* ？データがない？ */

	cur_adr->index = get_yomi_index(dp->yomi);

	return TRUE;
}

/*
 * カレントデータを削除
 */
EXPORT	W	delete_adr_data(void)
{
	ADR_NODE	*np = cur_adr;
	W		i   = cur_adr_ofs;
	ADR_DATA	*dp;
	ADR_DATA	*curp;

	/* 次、または前のページに移動 */
	if ( !( next_adr_page() || prev_adr_page() ) ) {
		/* 最後の一件なのでクリアするのみ */
		clear_adr_data();
		update_cur_index(); /* インデックス更新 */
		modified = TRUE; /* 編集された */
		return E_OK;
	}
	curp = cur_adr->data[cur_adr_ofs]; /* データへのポインタを保存 */

	/* データ切り離し */
	dp = remove_data(np, i);
	if ( dp == NULL ) return E_OK; /* ？データがない？ */

	/* データを削除 */
	free(dp);

	/* 現在位置を設定し直す */
	set_cur_data(curp);

	modified = TRUE; /* 編集された */

	return E_OK;
}

/*
 * カレントデータを再登録
 *	登録し直すことで、ソートを行う
 */
LOCAL	W	re_regist_adr(W mode)
/* カレントデータが空だった場合
				 *  0 : 削除しない
				 * -1 : 削除してカレントを前のデータに移動する
				 *  1 : 削除してカレントを次のデータに移動する
				 */
{
	ADR_NODE	*np = cur_adr;
	W		i   = cur_adr_ofs;
	ADR_DATA	*dp;
	W		err;

	/* 次、または前のページに移動 */
	if ( ( mode >= 0 )?
		!( next_adr_page() || prev_adr_page() ):
		!( prev_adr_page() || next_adr_page() ) ) {
		/* 最後の一件なのでそのまま：インデックス文字のみ更新 */
		update_cur_index();
		return E_OK;
	}

	/* データ切り離し */
	dp = remove_data(np, i);
	if ( dp == NULL ) return E_OK; /* ？データがない？ */

	if ( mode != 0 && is_data_empty(dp) ) {
		/* データは空：再登録不要(削除) */
		free(dp);
		calc_adr_page(); /* ページ番号の再計算 */
		modified = TRUE; /* 編集された */
	} else {
		/* 再登録 */
		if ( (err = insert_adr(dp)) < E_OK ) {
			/* 登録できなかった：エラーパネル表示 */
			errpanel(ER_REGIST, err);
			/*
			 * ※※ このままだとデータが脱落する ※※
			 * どうする？？  (911112 fujita)
			 */
			return err;
		}

		/* 再登録した位置をカレントに設定 */
		set_cur_data(dp);
	}

	return E_OK;
}

/*
 * よみ変更後のソート、及び再表示処理
 */
EXPORT	W	chg_adr_yomi(void)
{
	W	err;

	/* 現在ページを登録し直して、ソートを行う */
	if ( (err = re_regist_adr(0)) >= E_OK ) {

		/* ソート完了：ページ番号等の変更部分のみ再表示 */
		win_dispsw(ADR_WIN);
		view_window(ADR_WIN, &visrect[ADR_WIN], 1);

		/* 検索パネルのボタンを再表示 */
		if ( win[SRC_WIN].id > 0 ) pnl_dispsw(SRC_WIN, 1);
	} else {

		/* エラー：ソート後再登録できなかった
		 * 登録できなかったデータは失われている
		 */
		redisp_adr(1); /* 全再表示 */
	}

	return err;
}

/*
 * カレントページ番号を文字列に変換
 *	"９９９／９９９" の形式
 *   右詰め↑	   ↑左詰め
 */
EXPORT	VOID	get_adr_page_str(TC *str)
/* ここに文字列を返す：７＋１文字分必要 */
{
LOCAL	TC	warning[] = {0x2176,0x2176,0x2176,TNULL}; /* "＊＊＊" */

	if ( cur_page.no <= 999 ) {
		stoal(str, cur_page.no, 3);
	} else {
		tc_strcpy(str, warning);
	}

	str[3] = 0x213f; /* "／" */

	if ( cur_page.all <= 999 ) {
		stoa(str+4, cur_page.all);
	} else {
		tc_strcpy(str+4, warning);
	}
}

/*
 * 最初に表示する住所データをカレントとして設定する
 */
EXPORT	W	set_cur_adr(void)
{
	W	err;

	/* 最初のデータに位置付ける */
	if ( !first_adr_page() ) {

		/* データが空なので、新規登録用のデータを設定 */
		if ( (err = set_new_adr()) < E_OK ) return err;
	}

	return E_OK;
}

/*
 * カレントの見出し番号とページ番号を返す
 */
EXPORT	BOOL	get_cur_adr_page(W *idx,W *no)
{
	if ( cur_adr == NULL ) return FALSE;

	*idx = midashi_idx(cur_adr->index);
	*no  = cur_page.no;
	return TRUE;
}

/*
 * 指定の見出し番号・ページ番号のデータに移動する
 */
EXPORT	BOOL	set_cur_adr_page( W idx, W no)
{
	ADR_NODE	*np = cur_adr;		/* 現在位置を保存 */
	W		ofs = cur_adr_ofs;

	/* 指定の見出しの先頭ページに移動 */
	if ( !set_adr_page(idx, 0) ) goto err_exit;

	/* ページ番号分進める */
	while ( --no > 0 ) {
		if ( !next_find(cur_adr, cur_adr_ofs+1) ) goto err_exit;
	}

	/* 次の見出しに、はみ出ていないか */
	if ( idx != midashi_idx(cur_adr->index) ) goto err_exit;

	calc_adr_page(); /* ページ番号再計算 */

	return TRUE; /* 移動完了 */

err_exit:
	/* 指定のページが見つからないので、元に戻す */
	cur_adr     = np;
	cur_adr_ofs = ofs;
	return FALSE;
}
