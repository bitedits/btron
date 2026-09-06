/*
	scexec.c	電子手帳 : 仮身のオープン起動：メイン

	(C) Copyright 1997-99 by Personal Media Corporation
*/
#include "sched.h"
#include <mtstring.h>

/* 起動メッセージポインタ */
EXPORT	M_EXECREQ	*mycmd;

/*
 * イベント関数の設定
 */
LOCAL	WFUNCREC	wfunc = {
					/* (※)印は、未使用時 NULL 指定可能 */
	NULL /*sc_bgfn*/,	/* バックグラウンド処理 (※) */
	NULL, /*sc_idlefn,*/		/* ＩＤＬＥ処理 (※) */
	NULL, /*sc_msgfn,*/		/* 一般メッセージ処理 (※) */
	NULL, /*sc_menufn,*/		/* メニュー処理 (※) */
	NULL, /*sc_dspfn,*/		/* 表示処理 */
	NULL, /*sc_stsfn,*/		/* 状態変化処理 */
	NULL, /*sc_keyfn,*/		/* キー入力処理 (※) */
	NULL, /*sc_presfn,*/		/* ＰＤプレス処理 (※) */
	NULL, /*sc_finfn,*/		/* 終了処理 */
	NULL, /*sc_pastefn,*/		/* 貼り込み処理 (※) */
	NULL /*sc_respfn*/,	/* 応答処理 (※) */
	NULL /*sc_scrlfn*/,	/* スクロール処理 (※) */
	NULL /*sc_devfn*/,	/* デバイス・イベント処理 (※) */
	NULL, /*sc_vobjfn*/		/* 仮身要求イベント処理 (※) */
};

/* ウィンドウ背景色 */
EXPORT	PAT		WINDBG[MAX_WIN] = {
	{{
		0,		/* spat 形式を指定 */
		16,		/* パターンの横サイズ(ピクセル数) */
		16,		/* パターンの縦サイズ(ピクセル数) */
		RGB_BLACK,	/* 前景色 */
		RGB_WHITE,	/* 背景色 */
		FILL100		/* パターンマスクデータ */
	}},
	{{ 0, 16, 16, RGB_BLACK, RGB_WHITE, FILL100 }},
	{{ 0, 16, 16, RGB_BLACK, RGB_WHITE, FILL100 }},
	{{ 0, 16, 16, RGB_BLACK, RGB_WHITE, FILL100 }}
};

/* ウィンドウ描画環境 */
EXPORT	FSSPEC	c_font[MAX_WIN];
EXPORT	CGAP		c_gap[MAX_WIN];
EXPORT	RECT		visrect[MAX_WIN];

/* ウィンドウ固有データ */
EXPORT	W		adr_str_num;
EXPORT	STR_INFO	adr_str_info[MAX_STR];
EXPORT	RECT		adr_index_rect;
EXPORT	PNT		adr_page_disp;
EXPORT	RECT		cal_year;
EXPORT	PNT		cal_lefttop;
EXPORT	PNT		cal_daysize;
EXPORT	W		src_str_num;
EXPORT	STR_INFO	src_str_info[MAX_STR];

/* ウィンドウ名 */
EXPORT	TC		*window_name[MAX_WIN];
LOCAL	W		pict_num;	/* ピクトグラムのタイプ */
EXPORT	TC		f_name[20+1];	/* 実身名 */
EXPORT	W		root_wid = -1;	/* 元から生成したウィンドウID */
LOCAL	W		prev_wid;	/* 親ウインドウID */
LOCAL	RECT		prev_vrect;	/* 生成元仮身 */


/*
 * 対象ファイルのオープン
 */
EXPORT	W	open_file(LINK *f_link,F_STATE *f_state)
/* 対象実身(ファイル)へのリンク */
/* ファイル管理情報 */
{
	W		cnt;	/* ファイルの参照カウント */

	/* ファイルのオープン */
	if ( (tg_fp = ropen(f_link, (UW)RM_TADDATA, F_READ)) == NULL ) {
		return ropen_err; /* エラー */
	}

	/* ファイル情報の取得 */
	if ( (cnt = ofl_sts(rfileno(tg_fp), f_name, f_state, NULL)) < E_OK ) {
		rclose(tg_fp);
		return cnt; /* エラー */
	}

	return E_OK; /* 正常オープン */
}

/*
 * 付箋固有情報の正規化
 */
EXPORT	VOID	swab_fusen(SCHED_FUSEN *fsn, xSCHED_FUSEN *xf, W tomem)
{
	W	i, j, k;

	if (!tomem) {
		/* メモリー上の fsn からファイル形式の xf に */
		xf->len = fsn->len;
		for (j=i=0; i<MAX_WIN; i++) {
			xf->data[j++] = ConvEndianH(fsn->winfo[i].w_rect.c.left);
			xf->data[j++] = ConvEndianH(fsn->winfo[i].w_rect.c.top);
			xf->data[j++] = ConvEndianH(fsn->winfo[i].w_rect.c.right);
			xf->data[j++] = ConvEndianH(fsn->winfo[i].w_rect.c.bottom);
			*(W*)(&xf->data[j]) = ConvEndianW(fsn->winfo[i].bgcolor);
			j += sizeof(W)/sizeof(H);
			xf->data[j++] = ConvEndianH(fsn->winfo[i].bgmask);
			for (k=0;k<3;k++)
				xf->data[j++] = ConvEndianH(fsn->winfo[i].page[k]);
			for (k=0;k<6;k++)
				xf->data[j++] = ConvEndianH(fsn->winfo[i].reserved[k]);
		}
		xf->data[j++] = ConvEndianH(fsn->win_sw);
		xf->data[j++] = ConvEndianH(fsn->win_top);
		for (i=0; i<6; i++)
			xf->data[j++] = ConvEndianH(fsn->reserved[i]);
	} else {
		/* ファイル形式の xf からメモリー上の fsn に */
		fsn->len = xf->len;
		for (j=i=0; i<MAX_WIN; i++) {
			fsn->winfo[i].w_rect.c.left = ConvEndianH(xf->data[j++]);
			fsn->winfo[i].w_rect.c.top = ConvEndianH(xf->data[j++]);
			fsn->winfo[i].w_rect.c.right = ConvEndianH(xf->data[j++]);
			fsn->winfo[i].w_rect.c.bottom = ConvEndianH(xf->data[j++]);
			fsn->winfo[i].bgcolor = ConvEndianW(*(W*)(&xf->data[j]));
			j += sizeof(W)/sizeof(H);
			fsn->winfo[i].bgmask = ConvEndianH(xf->data[j++]);
			for (k=0;k<3;k++)
				fsn->winfo[i].page[k] = ConvEndianH(xf->data[j++]);
			for (k=0;k<6;k++)
				fsn->winfo[i].reserved[k] = ConvEndianH(xf->data[j++]);
		}
		fsn->win_sw = ConvEndianH(xf->data[j++]);
		fsn->win_top = ConvEndianH(xf->data[j++]);
		for (i=0; i<6; i++)
			fsn->reserved[i] = ConvEndianH(xf->data[j++]);
	}
}

/*
 * 付箋 読み込み ＆ 初期値設定
 */
LOCAL	VOID	read_fusen(M_EXECREQ *exec_msg)
/* 起動メッセージ */
{
	SCHED_FUSEN	fsn;		/* 機能付箋へのポインタ */
	xSCHED_FUSEN	*xfsn;
	xSCHED_FUSEN	tmp;

	if ( (exec_msg->mode & 0x0001) != 0 ) {
		/* 実行メニューによる非デフォルトアプリケーションの起動
		 * exec_msg->info は、実行機能付箋のレコード番号なので
		 * 付箋レコードを読み出す
		 */
		if ( oget_fsn(exec_msg->vid, rfileno(tg_fp), &tmp, sizeof(tmp))
							< E_OK ) return;
		xfsn = &tmp;
	} else {
		/* デフォルトアプリケーションの起動
		 * exec_msg->info は、付箋の固有データへのポインタ
		 */
		xfsn = (xSCHED_FUSEN*)exec_msg->info;
	}

	swab_fusen(&fsn, xfsn, 1);
	if ( xfsn->len == sizeof(xSCHED_FUSEN)-2 ) {
		/* 初期値設定 */
		sched_fusen = fsn;
	}
}

/*
 * 描画環境の文字関係の設定
 */
EXPORT	W	init_ch_env(W gid,W ix)
{
#define	CALFNTSZ	8
	W	er;

	if ( (er = gget_chd(gid, &c_gap[ix])) < E_OK
	  || (er = gget_fon(gid, &c_font[ix], NULL)) < E_OK) goto EEXIT;

	if (ix == CAL_WIN) {
		c_font[ix].size.h = c_font[ix].size.v = CALFNTSZ;

		/* カレンダーの年月の位置 */
		cal_year.c.left   = cal_lefttop.x - 3;
		cal_year.c.right  = cal_year.c.left + (CALFNTSZ + 1) * 14;
		cal_year.c.bottom = cal_lefttop.y - 6;
		cal_year.c.top	  = cal_year.c.bottom - CALFNTSZ - 2;

	} else {
		c_font[ix].size.h = c_font[ix].size.v = 12;
	}

	if ((er = gset_fon(gid, &c_font[ix])) < E_OK) goto EEXIT;

	c_gap[ix].hgap.gap = 1 | 0x8000;	/* 絶対ピクセル数 */
	if ((er = gset_chd(gid, TORIGHT, &c_gap[ix])) < E_OK) goto EEXIT;

	er = gset_chc(gid, RGB_BLACK, COL_TRANS);

EEXIT:
	return er;
}

/*
 * ウインドウオープン
 *
 *	ウインドウ属性	atr
 *	WA_FRONT (0x0001) 通常／前面ウィンドウ	  (０:通常)	= 0
 *	WA_SUBW  (0x0002) 主／従属ウィンドウ	  (０:主)	= 0
 *	WA_SIZE  (0x0004) ドラッグ変形の可否	  (０:否)	= 0
 *	WA_HHDL  (0x0008) ドラッグ左右変形の可否  (０:否)	= 0
 *	WA_VHDL  (0x0010) ドラッグ上下変形の可否  (０:否)	= 0
 *	WA_RBAR  (0x0020) 右スクロールバーの有無  (０:無)	= 0
 *	WA_BBAR  (0x0040) 下スクロールバーの有無  (０:無)	= 0
 *	WA_LBAR  (0x0080) 左スクロールバーの有無  (０:無)	= 0
 *	WA_TITL  (0x0100) タイトルバーの有無	  (０:有)	= 0
 *	WA_BGDSP (0x0200) バックグラウンド表示属性(０:無)	= 0
 */
LOCAL	W	open_win(W ix,W atr,W pwid,RECT *wrect,RECT *vrect,W disp)
/* ウインドウ番号 */
/* ウインドウ属性 */
/* 親ウインドウID */
/* ウインドウ外枠 */
/* 起動元仮身の枠 */
/* 内容表示、1：する、0：しない */
{
	W		wid;		/* 生成したウインドウのID */
	W		er;		/* パーツ登録結果 */
	W		gid;		/* 生成したウインドウの描画環境ID */
	TC		name[96+1];	/* タイトル文字列 */

IMPORT	PAT		*window_bgp;

	/* タイトル文字列の設定 */
	if (ix < MAX_MW) {
		/* 実身名の後に（カレンダー）（予定表）（住所録）を加える */
		mtc_strjoin (name, sizeof (name)/sizeof (name [0]) - 1, f_name, window_name [ix], 0);

		/* ウインドウ背景色指定 */
		WINDBG[ix].spat.fgcol = RGB_WHITE;
	} else {
		/* 従属ウィンドウはタイトルのみ */
		tc_strcpy(name, window_name[ix]);

		/* ウインドウ背景色指定 */
		if (window_bgp != NULL) WINDBG[ix] = *window_bgp;
		else	WINDBG[ix].spat.fgcol = RGB_WHITE;
	}

	/* ウインドウの生成 */
	wid = wopn_wnd(	atr,		/* ウインドウ属性 */
			pwid,		/* 親ウインドウID */
			wrect,		/* ウインドウ外枠の矩形 */
			vrect,		/* 起動対象の仮身の矩形 */
			pict_num,	/* ピクトグラム番号 */
			name,		/* タイトルバー文字列 */
			&WINDBG[ix],	/* 背景パターン */
			NULL		/* ウインドウ表示属性 */
	);
	if ( wid < E_OK ) return wid;

	win[ix].id = wid;
	win[ix].pwid = pwid;
	winfo[ix].wid = wid;

	/* 描画環境IDの取得 */
	win[ix].gid = gid = wget_gid(wid);

	/* 描画環境の設定 */
	if ((er = gget_vis(gid, &visrect[ix])) < E_OK) goto EEXIT;

	/* 文字描画環境の設定 */
	if ((er = init_ch_env(gid, ix)) < E_OK) goto EEXIT;

	/* パーツの登録 */
	if ((er = create_parts(ix, disp)) < E_OK) {
EEXIT:
		wcls_wnd(wid, CLR);
		win[ix].id = -1;
		win[ix].pwid = -1;
		winfo[ix].wid = -1;
		return er;
	}

	if (root_wid < 0) root_wid = wid;

	return E_OK;
}

/*
 * ウインドウ表示位置の調整
 */
LOCAL	VOID	adj_win(RECT *rect,W x_wid,W y_wid)
/* ウインドウ枠 */
/* ウインドウの横幅 */
/* ウインドウの縦幅 */
{
	W		half_char, left_limit, top_limit;

	/* ウインドウがスクリーンから完全にはみ出してしまわないように調整 */
	half_char = CHSSTD / 2;
	left_limit = SCREEN.hpixels - half_char;
	top_limit = SCREEN.vpixels - (CHSSTD + 4) - half_char;

	/* ウィンドウのサイズは固定。right, bottom を設定する。 */

	if ( rect->c.left > left_limit ) {			/* 左側 */
		rect->c.left = left_limit;

	} else if ( rect->c.left + x_wid < half_char ) {	/* 右側 */
		rect->c.left = half_char - x_wid;
	}

	if ( rect->c.top > top_limit ) {			/* 上側 */
		rect->c.top = top_limit;

	} else if ( rect->c.top + y_wid < half_char ) {		/* 下側 */
		rect->c.top = half_char - y_wid;
	}

	/* 右下を設定する */
	rect->c.right = rect->c.left + x_wid;
	rect->c.bottom = rect->c.top + y_wid;
}

/*
 *  親ウィンドウIDを決める。
 *	始めのウィンドウの親は、起動元。２番目以降は、他のウィンドウ。
 */
LOCAL	W	get_parent_wid(W ix,RECT **vp)
{
	if (root_wid > 0) {
		*vp = 0;
		return root_wid;
	}
	*vp = &prev_vrect;
	return prev_wid;
}

/*
 * タイトル表示属性の設定
 */
LOCAL	VOID	init_title(W vid,UW f_atype)
/* 起動元仮身のID */
/* アプリケーションタイプ */
{
	/* ピクトグラム指定 */
	if ( (pict_num = f_atype & 0xff) == 0 ) pict_num = SCHED_PICT;
}

/*
 * データボックスのウィンドウデータの取り出し
 */
EXPORT	VOID	set_wind_def(W ix)
{
	W	i, n;
	W	j;
	W*	p;

	wind_def[ix] = (W*)getdbox(WCAL_DEF + ix);

	if (ix == CAL_WIN) {
		/* カレンダー固有データの設定 */
		/* (「日」の左上の位置とひとつの日付のサイズ) */
		i = POLY_START + 1 + wind_def[CAL_WIN][POLY_START] * 2;
		cal_lefttop.x	= wind_def[CAL_WIN][i++];
		cal_lefttop.y	= wind_def[CAL_WIN][i++];
		cal_daysize.x	= wind_def[CAL_WIN][i++];
		cal_daysize.y	= wind_def[CAL_WIN][i++];

	} else if (ix == ADR_WIN) {
		/* 住所録固有データの設定 */
		/* (表示文字列の位置と文字列へのポインタ) */
		i = wind_def[ix][0] + wind_def[ix][1];
		for (j = 0; (n = wind_def[ix][i]); i++, j++) {
			p = (W*)ptrdbox(n);
			/* 座標単位の変換 */
			if (p[0] & 0x8000) {
				p[0] = (p[0] & 0x7fff) * CHSSTD >> 4;
				p[1] = p[1] * CHSSTD >> 4;
			}
			adr_str_info[j].pos.x = p[0];
			adr_str_info[j].pos.y = p[1];
			adr_str_info[j].str = (TC*)&p[2];
		}
		adr_str_num = j;

		/* (インデックスとページ表示の位置) */
		i = POLY_START + 1 + wind_def[ADR_WIN][POLY_START] * 2;
		adr_index_rect.c.left	= wind_def[ADR_WIN][i++];
		adr_index_rect.c.top	= wind_def[ADR_WIN][i++];
		adr_index_rect.c.right	= wind_def[ADR_WIN][i++];
		adr_index_rect.c.bottom	= wind_def[ADR_WIN][i++];
		adr_page_disp.x	= wind_def[ADR_WIN][i++];
		adr_page_disp.y	= wind_def[ADR_WIN][i++];

	} else if (ix == SRC_WIN) {
		/* 検索パネル固有データの設定 */
		/* (表示文字列の位置と文字列へのポインタ) */
		i = wind_def[ix][0] + wind_def[ix][1];
		for (j = 0; (n = wind_def[ix][i]); i++, j++) {
			p = (W*)ptrdbox(n);
			/* 座標単位の変換 */
			if (p[0] & 0x8000) {
				p[0] = (p[0] & 0x7fff) * CHSSTD >> 4;
				p[1] = p[1] * CHSSTD >> 4;
			}
			src_str_info[j].pos.x = p[0];
			src_str_info[j].pos.y = p[1];
			src_str_info[j].str = (TC*)&p[2];
		}
		src_str_num = j;

	}
}

/*
 * カレンダ・ウインドウのオープン
 */
EXPORT	W	cal_open(RECT *vrect,W disp)
/* 起動元仮身の枠
				 * NULL のときオープニングアニメーションなし
				 */
/* 内容表示、1：する、0：しない */
{
	RECT		wrect;	/* ウインドウ外枠 */
	W		pwid;	/* 親ウィンドウID */
	W		width, height;	/* ウィンドウのサイズ */

	/* 表示領域の設定 */
	wrect = sched_fusen.winfo[CAL_WIN].w_rect; /* 付箋からよみだし */

	if (! wind_def[CAL_WIN]) {	/* データボックスから取り出し */
		set_wind_def( CAL_WIN );
	}
	width = wind_def[CAL_WIN][2];
	height = wind_def[CAL_WIN][3];
	adj_win(&wrect, width, height);
	/* adj_win(&wrect, wind_def[CAL_WIN][2], wind_def[CAL_WIN][3]); */

	/* 親ウィンドウID設定 */
	pwid = get_parent_wid(CAL_WIN, &vrect);

	/* ウインドウオープン */
	return open_win(CAL_WIN, 0, pwid, &wrect, vrect, disp);
}

/*
 * 予定表ウインドウのオープン
 */
EXPORT	W	sch_open(RECT *vrect,W disp)
/* 起動元仮身の枠
				 * NULL のときオープニングアニメーションなし
				 */
/* 内容表示、1：する、0：しない */
{
	RECT		wrect;	/* ウインドウ外枠 */
	W		pwid;	/* 親ウィンドウID */

	/* 表示領域の設定 */
	wrect = sched_fusen.winfo[SCH_WIN].w_rect; /* 付箋からよみだし */

	if (! wind_def[SCH_WIN]) {	/* データボックスから取り出し */
		set_wind_def( SCH_WIN );
	}
	adj_win(&wrect, wind_def[SCH_WIN][2], wind_def[SCH_WIN][3]);

	/* 親ウィンドウID設定 */
	pwid = get_parent_wid(SCH_WIN, &vrect);

	/* ウインドウオープン */
	return open_win(SCH_WIN, 0, pwid, &wrect, vrect, disp);
}

/*
 * 住所録ウインドウのオープン
 */
EXPORT	W	adr_open(RECT *vrect,W disp)
/* 起動元仮身の枠
				 * NULL のときオープニングアニメーションなし
				 */
/* 内容表示、1：する、0：しない */
{
	RECT		wrect;	/* ウインドウ外枠 */
	W		pwid;	/* 親ウィンドウID */

	/* 表示領域の設定 */
	wrect = sched_fusen.winfo[ADR_WIN].w_rect; /* 付箋からよみだし */

	if (! wind_def[ADR_WIN]) {	/* データボックスから取り出し */
		set_wind_def( ADR_WIN );
	}
	adj_win(&wrect, wind_def[ADR_WIN][2], wind_def[ADR_WIN][3]);

	/* 親ウィンドウID設定 */
	pwid = get_parent_wid(ADR_WIN, &vrect);

	/* ウインドウオープン */
	return open_win(ADR_WIN, 0, pwid, &wrect, vrect, disp);
}

/*
 * 検索ウインドウのオープン
 */
EXPORT	W	src_open(void)
{
	RECT		wrect;	/* ウインドウ外枠 */
	W		pwid;	/* 親ウィンドウID */

	/* 表示領域の設定 */
	wrect = sched_fusen.winfo[SRC_WIN].w_rect; /* 付箋からよみだし */

	if (! wind_def[SRC_WIN]) {	/* データボックスから取り出し */
		set_wind_def( SRC_WIN );
	}
	adj_win(&wrect, wind_def[SRC_WIN][2], wind_def[SRC_WIN][3]);

	/* 親ウィンドウID設定 */
	/* (従属ウィンドウでは、アクティブウィンドウを親とする) */
	pwid = win[sched_fusen.win_top].id;

	/* ウインドウオープン */
	return open_win(SRC_WIN, WA_SUBW, pwid, &wrect, NULL, 1);
}

EXPORT	FUNCP	main_open[MAX_MW]/* = {cal_open, sch_open, adr_open}*/;

/*
 * 初期ウインドウオープン
 */
LOCAL	W	init_win(M_EXECREQ *exec_msg)
/* 起動メッセージ */
{
	W		sw;		/* ウインドウ表示スイッチ */
	W		err;
	W		i;

	/* 親ウインドウIDを保存 */
	prev_wid = exec_msg->pwid;
	prev_vrect = exec_msg->r;

	/* タイトル表示属性の設定 */
	init_title(exec_msg->vid, tg_sts.f_atype);

	/* ウインドウ表示スイッチを取り出す
	 *	もし何も表示されないようになっていたら、
	 *	カレンダーだけ表示するように設定する。
	 */
	sw = sched_fusen.win_sw;
	if ( DISP_MAIN(sw) == 0 ) {
		sched_fusen.win_sw = (sw |= BIT_MASK(CAL_WIN));
		sched_fusen.win_top = CAL_WIN;
	}

	/* 各ウインドウを表示 */
	/* 内容の表示は初期表示で行う */

	/* win_top のウィンドウを最後にオープンする */
	for (i = 0; i < MAX_MW; i++) {
	    if ( (sw & BIT_MASK(i)) != 0 && sched_fusen.win_top != i ) {
		if ( (err = (*main_open[i])(&exec_msg->r, 0)) < E_OK ) return err;
	    }
	}
	i = sched_fusen.win_top;

	/* win_top は、win_sw もＯＮのはずだが、念のため */
	if ( (sw & BIT_MASK(i)) == 0 ) sched_fusen.win_sw |= BIT_MASK(i);

	if ( (err = (*main_open[i])(&exec_msg->r, 0)) < E_OK ) return err;
	in_act_parts = i;

	/* 従属ウィンドウ */
	if ( (sw & BIT_MASK(SRC_WIN)) != 0 ) {
		if ( (err = src_open()) < E_OK ) return err;
	}

	return E_OK;
}

/*
 * 初期設定
 */
LOCAL	W	init_exec(M_EXECREQ *exec_msg)
/* 起動メッセージ */
{
	W	err, i;
	TC	*tp;

	wfunc.idlefn = sc_idlefn;
	wfunc.msgfn = sc_msgfn;
	wfunc.menufn = sc_menufn;
	wfunc.dspfn = sc_dspfn;
	wfunc.stsfn = sc_stsfn;
	wfunc.keyfn = sc_keyfn;
	wfunc.presfn = sc_presfn;
	wfunc.finfn = sc_finfn;
	wfunc.pastefn = sc_pastefn;
	wfunc.vobjfn = sc_vobjfn;

	main_open[0] = cal_open;
	main_open[1] = sch_open;
	main_open[2] = adr_open;

	init_menu();	/*menu.cのポインタ設定*/

	/* 作業ファイル「なし」を指定 */
	chg_wrk(NULL);

	/* ウィンドウ名の初期化 */
	tp = (TC*)getdbox(WIND_NAME);
	for (i = 0; i < MAX_WIN && *tp != TNULL; i++) {
		window_name[i] = tp;
		tp += tc_strlen(tp) + 1;
	}

	/* 対象ファイルのオープン */
	err = open_file(&exec_msg->lnk, &tg_sts);
	if ( err < E_OK ) {
		/* エラーパネル表示 */
		errpanel(ER_OPEN, err);
		return err;
	}

	/* 付箋 読み込み ＆ 初期値設定 */
	read_fusen(exec_msg);

	/* ウインドウのオープン */
	if ( (err = init_win(exec_msg)) < E_OK ) {
		/* エラーパネル表示 */
		errpanel(ER_WIND, err);
		return err;
	}

	/* 実身/仮身マネージャーへの処理開始通知 */
	/* osta_prc(exec_msg->vid, win[sched_fusen.win_top].id); */
	osta_prc(exec_msg->vid, root_wid);

	/* メニューの初期化 */
	if ( (err = openmenu(SCHED_MENU)) < E_OK ) {
		/* エラーパネル表示 */
		errpanel(ER_MENU, err);
		return err;
	}

	/* 検索用フィールド名のリストをデータボックスから読み出す */
	get_field_names();

	/* データ読み込み */
	sysmsg(MSG_LOAD);	/* 「読み込み中です。...」 */
	err = sc_load();
	rclose(tg_fp); /* 対象ファイルクローズ */
	sysmsg(0);
	if ( err < E_OK ) {
		/* エラーパネル表示 */
		errpanel(ER_READ, err);
		return err;
	}

	/* 読み込めないデータがあったか？ */
	if ( nosupport || illegal ) {
		/* 読み込めないデータがあった */
		panel(PNL_NOSPTL);
	}

	if ( (err = set_curdata(SCH_WIN)) < E_OK
	  || (err = set_curdata(ADR_WIN)) < E_OK
	  || (err = set_curdata(SRC_WIN)) < E_OK
	  || (err = set_curdata(CAL_WIN)) < E_OK ) {
		/* エラーパネル表示 */
		panel(ER_MEMORY);
		return err;
	}

	return E_OK;
}

/* 表示ページを記憶する */
EXPORT	VOID	set_page_to_fusen(void)
{
	W	index, page;

	sched_fusen.winfo[CAL_WIN].page[0] = dispdate.year;
	sched_fusen.winfo[CAL_WIN].page[1] = dispdate.month;

	sched_fusen.winfo[SCH_WIN].page[0] = selectdate.year;
	sched_fusen.winfo[SCH_WIN].page[1] = selectdate.month;
	sched_fusen.winfo[SCH_WIN].page[2] = selectdate.day;

	if ( get_cur_adr_page(&index, &page) ) {
		sched_fusen.winfo[ADR_WIN].page[0] = index;
		sched_fusen.winfo[ADR_WIN].page[1] = page;
	} else {
		sched_fusen.winfo[ADR_WIN].page[0] = 0;
		sched_fusen.winfo[ADR_WIN].page[1] = 1;
	}
}

/* ウィンドウの表示位置を記憶する */
EXPORT	VOID	set_window_rect(W ix)
{
	W		wid;
	WDSTAT		stat;

	if ( (wid = win[ix].id) < E_OK ) return;
	wget_sts(wid, &stat, NULL);

	/* 920421：タイトルバーの文字サイズが標準でない場合、ウィンドウオープン
	   時に上の位置が標準サイズで調整されるため、下を合わせるように位置を
	   調整する */

	stat.r.c.top += rectheight(stat.r) - rectheight(stat.wr) -
								(1+2+23+2+2);
	sched_fusen.winfo[ix].w_rect = stat.r;
}

/*
 * ウインドウのクローズ
 */
EXPORT	VOID	close_win(W ix,W all)
/* ウインドウ番号 */
/* 終了時：1、個別：0 */
{
	W		wid;
	W		i, j;

	/* ウインドウIDを得る */
	wid = win[ix].id;
	if ( wid < E_OK ) return; /* ウインドウはオープンされていない */

	/* ウィンドウの表示位置を記憶する */
	set_window_rect( ix );

	if ( !all && ix < MAX_MW ) {

	  /* 他のウィンドウがあれば親ウィンドウを変更する */
	  for (i = 0; i < MAX_MW; i++) {
	    if (ix == i) continue;
	    if (win[i].id > 0) {	/* 他のウィンドウがある場合 */

		/* 閉じるウィンドウが最前面のウィンドウの場合 */
		/* （閉じるウィンドウがサブウィンドウの親の場合） */
		/* サブウィンドウをいったん閉じる。 */
		/* set_active_win() でオープンすることになる */
		if (ix == sched_fusen.win_top) {
		    if (win[SRC_WIN].id > 0) {
			set_window_rect(SRC_WIN);
			delete_parts(SRC_WIN);
			wcls_wnd(win[SRC_WIN].id, CLR);

			/* ウインドウを閉じたことを示す */
			win[SRC_WIN].id = win[SRC_WIN].pwid = winfo[SRC_WIN].wid = -1;
		    }
		}

		/* 閉じるウィンドウが根のウィンドウ */
		if (wid == root_wid) {

		    /* 他のウィンドウの親を最初のウィンドウにする */
		    wset_org(win[i].id, prev_wid, &prev_vrect);
		    win[i].pwid = prev_wid;

		    /* 閉じるウィンドウの親を他のウィンドウにする */
		    wset_org(wid, win[i].id, NULL);

		    /* その他のウィンドウがあれば、親を他のウィンドウにする */
		    for (j = 0; j < MAX_WIN; j++) {
			if (j == i || j == ix) continue;
			if (win[j].id > 0) {
			    wset_org(win[j].id, win[i].id, NULL);
			    win[j].pwid = win[i].id;
			}
		    }

		    /* 生成元の仮身を処理状態に */
		    osta_prc(mycmd->vid , (root_wid = win[i].id));
		    break;
		}
	    }
	  }
	}

	/* パーツを削除する */
	delete_parts(ix);

	/* ウインドウを閉じる */
	wcls_wnd(wid, CLR);

	/* ウインドウを閉じたことを示す */
	win[ix].id = -1;
	win[ix].pwid = -1;
	winfo[ix].wid = -1;

}

/*
 * 後始末
 */
LOCAL	VOID	finish_exec(M_EXECREQ *exec_msg,W exit_code)
/* 起動メッセージ */
/* 終了コード */
{
	W		v_gid;		/* 起動元仮身の描画環境ID */
	W		fd;
	W		er;
	W		v_wid;		/* 開いた仮身の内部ウィンドウID */
	xSCHED_FUSEN	xfsn;

	setpointer(PS_BUSY, NULL); /* 湯のみ */

	/* 付箋へ現在情報を保存する */
	if ( exit_code >= E_OK ) {
		set_window_rect(CAL_WIN);
		set_window_rect(SCH_WIN);
		set_window_rect(ADR_WIN);
		set_window_rect(SRC_WIN);
		set_page_to_fusen();
		if ( (fd = opn_fil(&exec_msg->lnk, F_UPDATE, NULL)) >= E_OK ) {
			swab_fusen(&sched_fusen, &xfsn, 0);
			er = oput_fsn(exec_msg->vid, fd, &xfsn);
			cls_fil(fd);
		}
	}

	/* 実身/仮身マネージャーへの処理終了通知 */
	v_gid = oend_prc(exec_msg->vid, &xfsn, tg_update);

	/* er = wswi_wnd(prev_wid, NULL); */

	/* ウインドウのクローズ */
	close_win(SRC_WIN, 1);
	close_win(ADR_WIN, 1);
	close_win(SCH_WIN, 1);
	close_win(CAL_WIN, 1);

	/* 起動元仮身の処理 */
	if ( v_gid > 0 ) {
		/* 起動元仮身が開いた仮身なので、開いた仮身内の描画を行う */
		/* 現在は、仮身は扱っていないので、仮身の移動は行わない。 */
		v_wid = wopn_iwd( v_gid );
		sc_dsproc(v_wid, v_gid, exec_msg->vid, exec_msg->bgcol);
	}

	/* メニューのクローズ */
	closemenu();
}

/*
 * 仮身オープン起動のメイン
 */
EXPORT	W	sc_exec(M_EXECREQ *exec_msg)
/* 起動メッセージ */
{
	W		err;

	/* 初期設定 */
	if ( (err = init_exec(mycmd = exec_msg)) < E_OK ) goto err_exit;

	/* イベントループ */
	winfo[MAX_WIN].wid = 0;
	err = evt_loop(&wfunc, winfo);

err_exit:
	/* 後始末 */
	finish_exec(exec_msg, err);

	return ( err >= E_OK )? E_OK: err;
}
