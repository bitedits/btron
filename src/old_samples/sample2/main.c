/*
	main.c		電子手帳 : メイン

	(C) Copyright 1997-98 by Personal Media Corporation
*/
#include "sched.h"
#include <btron/process.h>

/*
 * グローバルデータ
 */
EXPORT	WINFOREC	winfo[MAX_WIN+1] = {	/* ウインドウ管理レコード */
	/* デフォルト値 */
	{-1,		/* カレンダ・ウインドウ */
	0x0020,		/* ウインドウの移動許可、変形禁止、前面モード禁止 */
	{{0,0,0,0}},	/* 変形リミット(無意味) */
	{-1,-1,-1},	/* スクロールバーID (未定義) */
	{0L,0L}		/* アプリケーションデータ (未使用) */
	},
	{-1,		/* 予定表ウインドウ */
	0x0020,		/* ウインドウの移動許可、変形禁止、前面モード禁止 */
	{{0,0,0,0}},	/* 変形リミット(無意味) */
	{-1,-1,-1},	/* スクロールバーID (未定義) */
	{0L,0L}		/* アプリケーションデータ (未使用) */
	},
	{-1,		/* 住所録ウインドウ */
	0x0020,		/* ウインドウの移動許可、変形禁止、前面モード禁止 */
	{{0,0,0,0}},	/* 変形リミット(無意味) */
	{-1,-1,-1},	/* スクロールバーID (未定義) */
	{0L,0L}		/* アプリケーションデータ (未使用) */
	},
	{-1,		/* 検索ウインドウ */
	0x0020,		/* ウインドウの移動許可、変形禁止、前面モード禁止 */
	{{0,0,0,0}},	/* 変形リミット(無意味) */
	{-1,-1,-1},	/* スクロールバーID (未定義) */
	{0L,0L}		/* アプリケーションデータ (未使用) */
	},
	{0}		/* end mark */
};
EXPORT	WIN_INFO	win[MAX_WIN] = {	/* ウインドウ管理情報 */
	/* デフォルト値 */
	{					/* カレンダ・ウインドウ */
	-1,					/* ウインドウID */
	-1,					/* 描画環境ID */
	-1
	},
	{					/* 予定表ウインドウ */
	-1,					/* ウインドウID */
	-1,					/* 描画環境ID */
	-1
	},
	{					/* 住所録ウインドウ */
	-1,					/* ウインドウID */
	-1,					/* 描画環境ID */
	-1
	},
	{					/* 検索ウインドウ */
	-1,					/* ウインドウID */
	-1,					/* 描画環境ID */
	-1
	}
};
EXPORT	RFILE		*tg_fp = NULL;		/* 対象ファイル */
EXPORT	F_STATE		tg_sts;			/* 対象ファイル情報 */
EXPORT	W		tg_update = 0;		/* 対象更新フラグ */
EXPORT	BOOL		modified = FALSE;	/* 変更フラグ */
EXPORT	SCHED_FUSEN	sched_fusen = {		/* 付箋固有データ */
	/* デフォルト値 */
	144,		/* データ長 */
	0,0,0,0,	/* カレンダ・ウインドウ枠 */
	RGB_WHITE,	/* 背景色 */
	0,		/* 背景マスク */
	0,0,0,0,0,0,0,0,0,
	0,0,0,0,	/* 予定表ウインドウ枠 */
	RGB_WHITE,	/* 背景色 */
	0,		/* 背景マスク */
	0,0,0,0,0,0,0,0,0,
	0,0,0,0,	/* 住所録ウインドウ枠 */
	RGB_WHITE,	/* 背景色 */
	0,		/* 背景マスク */
	0,0,0,0,0,0,0,0,0,
	0,0,0,0,	/* 検索ウインドウ枠 */
	RGB_WHITE,	/* 背景色 */
	0,		/* 背景マスク */
	0,0,0,0,0,0,0,0,0,
	0x0001,		/* ウインドウ表示スイッチ */
	0,		/* 最前面表示ウインドウ番号 */
	0,0,0,0,0,0
};
EXPORT	MN_STATUS	mn_sts = {		/* メインメニュー管理情報 */
	/* デフォルト値 */
	0		/* ??????????????????????????? */
};
EXPORT	SCH_NODE	*sch_data = NULL; /* 最初の予定表データへのポインタ */
EXPORT	SCH_NODE	*cur_sch  = NULL; /* 現在ノード(ページ)へのポインタ */
EXPORT	ADR_NODE	*adr_data = NULL; /* 最初の住所録データへのポインタ */
EXPORT	ADR_NODE	*cur_adr  = NULL; /* 現在ノードへのポインタ */
EXPORT	W		cur_adr_ofs = 0;  /* 現在ページのノード内オフセット */
EXPORT	CAL_HOLIDAY	*holiday  = NULL; /* 休日データ配列 */
EXPORT	W		nholiday  = 0;	  /* 有効休日データ数 */

EXPORT	DATE	todaydate;	/* 今日の日付。カレンダーで枠囲み中 */
EXPORT	DATE	dispdate;	/* カレンダーに表示中の年月(dayは未使用) */
EXPORT	DATE	selectdate;	/* 予定表に表示中の日付 */

EXPORT	PAT	*window_bgp;	/* ウィンドウ背景色 */

/*
 * 初期設定
 */
LOCAL	W	init( M_EXECREQ *exec_msg)
{
	W	err, len;

	/* データボックスのオープン */
	err = opendbox(&exec_msg->self, SCHED_DTYP, SCHED_DNUM);
	if ( err != E_OK ) return err;

	/* パターン情報の取り出し */
	len = wget_inf(WI_PANELBACK, NULL, 0);
	if (len > 0 && (window_bgp = (PAT*)malloc(len)) != NULL) {
		wget_inf(WI_PANELBACK, (VP)window_bgp, len);
	} else	window_bgp = NULL;

	getscreen();	/* スクリーン情報の設定 */
	initstdpnl();	/* パネル処理の初期化 */

	initform();	/* 用紙関連の初期化 */

	get_today_date(&todaydate);	/* 現在の日付設定 */

	return err;
}

/*
 * 後始末
 */
LOCAL	VOID	finish(void)
{
	/* データボックスのクローズ */
	closedbox();
}

/*
 * 初めの一歩
 */
EXPORT	VOID	MAIN( MESSAGE *exec_msg)
{
	W		exit_code = EX_PAR;

	bigEndian = 0;

	/* 初期設定 */
	if ( (exit_code = init((M_EXECREQ*)exec_msg)) != E_OK ) goto err_exit;

	/* 起動種別 */
	switch ( exec_msg->msg_type ) {
	  case EXECREQ:		/* 仮身のオープン起動 */
		if ( exec_msg->msg_size == sizeof(M_EXECREQ)-8 )
			exit_code = sc_exec((M_EXECREQ*)exec_msg);
		break;
	  case DISPREQ:		/* 開いた仮身の表示起動 */
		if ( exec_msg->msg_size == sizeof(M_DISPREQ)-8 )
			exit_code = sc_dsmain((M_DISPREQ*)exec_msg);
		break;
	  case PASTEREQ:	/* データ貼り込み起動 */
		exit_code = EX_NOSPT;	/* 未実装 */
		break;
	  case FUSENREQ:	/* 付箋のオープン起動 */
		if ( exec_msg->msg_size == sizeof(M_FUSENREQ)-8 ) {
			panel(ER_FSNREQ);	/* 付箋からのオープンは不可 */
			exit_code = EX_NOSPT;
		}
		break;
	  default:		/* その他 */
		exit_code = EX_PAR;	/* パラメータエラー */
	}

err_exit:
	/* 後始末 */
	finish();

	/* プロセスの終了 */
	ext_prc(exit_code);
}
