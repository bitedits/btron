/*
	sched.h		電子手帳 : 共通ヘッダ

	(C) Copyright 1997-99 by Personal Media Corporation
*/
#include <basic.h>
#include <bstdlib.h>
#include <bstring.h>
#include <errcode.h>
#include <tcode.h>
#include <tctype.h>
#include <bstring.h>
#include <tstring.h>
#include <btron/file.h>
#include <btron/message.h>
#include <btron/dp.h>
#include <btron/hmi.h>
#include <btron/vobj.h>
#include <btron/libapp.h>
#include <btron/cnvend.h>
#include "fileio.h"

/*
 * 外部ライブラリで定義
 */
IMPORT	DEV_SPEC	SCREEN;		/* スクリーンの情報 */
IMPORT	W		CHSSTD;		/* 標準ウィンドウ文字サイズ */

/*
 * データボックス定義
 */
#define	SCHED_DTYP	64	/* データタイプ */
#define	SCHED_DNUM	0x2007	/* データ番号 */

#define	DB_SPNL		0x8000	/* 単純パネルデータ番号 */
#define	DB_PSTR		0x9000	/* パネル文字バッファデータ番号 */
#define	DB_MENU		0xA000	/* メニューデータ番号 */
#define	DB_SMSG		0xB000	/* システムメッセージデータ番号 */
#define	DB_MISC		0xF000	/* その他のデータ番号 */


/* システムメッセージ定義 */
#define	MSG_LOAD	(DB_SMSG + 0)	/* 読み込み待ちメッセージ	*/
#define	MSG_SAVE	(DB_SMSG + 1)	/* 書き込み待ちメッセージ	*/
#define	MSG_TMPFL	(DB_SMSG + 2)	/* 一時ファイル生成待ちメッセージ */

/* 通常パネル定義 */
#define	PNL_CLOSE	(DB_SPNL + 0)	/* 終了時更新確認 */
#define	PNL_XCLOSE	(DB_SPNL + 1)	/* 終了時更新確認 */
#define	PNL_UPDATE	(DB_SPNL + 2)	/* 保存時更新確認 */
#define	PNL_ERSAVE	(DB_SPNL + 3)	/* 更新禁止実身での確認 */
#define	PNL_CUPD	(DB_SPNL + 4)	/* 他のアプリケーションによる変更時 */
#define	PNL_CHK		(DB_SPNL + 5)	/* 再度更新確認 */
#define	PNL_NOSPTL	(DB_SPNL + 6)	/* 未サポート有(ファイル読み込み時) */
#define	PNL_NOSPTT	(DB_SPNL + 7)	/* 未サポート有(トレー読み込み時) */
#define	PNL_WRONGL	(DB_SPNL + 8)	/* 不正データ有(ファイル読み込み時) */
#define	PNL_WRONGT	(DB_SPNL + 9)	/* 不正データ有(ファイル読み込み時) */
#define	PNL_CLRSCH	(DB_SPNL + 10)	/* 予定空欄時の確認 */
#define	PNL_DELADR	(DB_SPNL + 11)	/* 住所削除時の確認 */
#define	PNL_PRPAGE	(DB_SPNL + 12)	/* 印刷ページ選択 */

/* エラーパネル定義 */
#define	ER_OPEN		(DB_SPNL + 20)	/* 対象ファイルオープン不可 */
#define	ER_WIND		(DB_SPNL + 21)	/* ウインドウオープン不可 */
#define	ER_WIND_NOE	(DB_SPNL + 29)	/* ウインドウオープン不可（終了しない） */
#define	ER_MENU		(DB_SPNL + 22)	/* メニュー初期化不可 */
#define	ER_READ		(DB_SPNL + 23)	/* ファイル読み込み不可 */
#define	ER_WRITE	(DB_SPNL + 24)	/* ファイル書き込み不可 */
#define	ER_CUT		(DB_SPNL + 25)	/* トレーへの複写／移動不可 */
#define	ER_MOVE		(DB_SPNL + 26)	/* ウィンドウへの複写／移動不可 */
#define	ER_PASTE	(DB_SPNL + 27)	/* トレーからの複写／移動不可 */
#define	ER_MEMORY	(DB_SPNL + 28)	/* メモリが不足 */
#define	ER_SRCH		(DB_SPNL + 30)	/* 検索文字列が見つからない */
#define	ER_TMPFL	(DB_SPNL + 33)	/* 一時ファイル生成エラー */
#define	ER_FSNREQ	(DB_SPNL + 34)	/* 付箋起動不可 */
#define	ER_REGIST	(DB_SPNL + 35)	/* データ登録エラー */

/* メニュー定義 */
#define	SCHED_MENU	(DB_MENU + 0)	/* メインメニュー */
#define	POPM_INDEX	(DB_MENU + 1)	/* ポップアップメニュー */

/* その他のデータ定義 */
#define	TX_TRAYTL	(DB_MISC + 0)	/* トレーデータのタイトル */
#define	WIND_NAME	(DB_MISC + 1)	/* ウィンドウ名 */
#define	FLD_NAMES	(DB_MISC + 60)	/* 検索用フィールド名のリスト */
#define	MIDASHI		(DB_MISC + 70)	/* 検索見出し */
#define	WEEK_STR	(DB_MISC + 71)	/* カレンダ用 */
#define	IMG_PAGEPICT0	(DB_MISC + 110)	/* 開いた仮身のスイッチイメージ */
#define	IMG_PAGEPICT1	(DB_MISC + 111)	/* 開いた仮身のスイッチイメージ */

/* パーツ定義 */
#define	WCAL_DEF	(DB_MISC + 230)	/* カレンダー用 */
#define	WSCH_DEF	(DB_MISC + 231)	/* 予定表用 */
#define	WADR_DEF	(DB_MISC + 232)	/* 住所録用 */
#define	WSRC_DEF	(DB_MISC + 233)	/* 検索パネル用 */

#define	SCHED_PICT	28	/* ピクトグラム番号 */

#define	POLY_START	4	/* 多角形定義開始オフセット */

/*
 * 色定義 (COLOR)
 *	COL_xxx = ピクセル値指定
 *	RGB_xxx = 絶対色指定(RGB値指定)
 */
#define	COL_BLACK	0x0fffffffL
#define	COL_WHITE	0x00000000L
#define	COL_TRANS	0xffffffffL	/* 透明 */
#define	RGB_BLACK	0x10000000L
#define	RGB_WHITE	0x10ffffffL

/*
 * ウインドウ管理
 */

#define	MAX_RLST	6	/* 再表示用 RECTLIST 数 */

#define	CAL_WIN		0	/* カレンダ・ウインドウの番号 */
#define	SCH_WIN		1	/* 予定表ウインドウの番号 */
#define	ADR_WIN		2	/* 住所録ウインドウの番号 */
#define	SRC_WIN		3	/* 検索ウインドウの番号 */

#define	MAX_MW		3		/* 最大主ウインドウ数 */
#define	MAX_SW		1		/* 最大サブウインドウ数 */
#define	MAX_WIN		(MAX_MW+MAX_SW)	/* 最大ウインドウ数 */

IMPORT	WINFOREC	winfo[MAX_WIN+1];	/* evt_loop()用管理情報 */

typedef	struct {
	W		id;			/* ウインドウID */
	W		gid;			/* 描画環境ID */
	W		pwid;			/* 親ウインドウID */
}	WIN_INFO;

IMPORT	WIN_INFO	win[MAX_WIN];		/* ウインドウ管理情報 */

IMPORT	W*		wind_def[MAX_WIN];	/* ウィンドウ用データ */

/* パーツ定義 */
#define	MAX_PARTS	12		/* パーツ最大数 */

/* パーツ情報 */
typedef	struct	{
	W	parts_num;		/* パーツ数 */
	W	tbox_start;		/* テキストボックスのオフセット */
	W	tbox_no;		/* 選択されているテキストボックス */
	W	pid[MAX_PARTS];		/* パーツＩＤ */
	PARTS	*parts[MAX_PARTS];	/* パーツへのポインタ */
} PARTS_INFO;

IMPORT	PARTS_INFO	wpinfo[MAX_WIN];

/* 表示文字列情報 */
#define	MAX_STR		10		/* 表示文字列最大数 */

typedef	struct	{
	PNT	pos;			/* 文字列 */
	TC	*str;			/* 表示位置 */
} STR_INFO;

IMPORT	STR_INFO	adr_str_info[MAX_STR];
IMPORT	W		adr_str_num;

/*
 * 付箋固有データ
 */
typedef	struct {
	RECT		w_rect;		/* ウインドウの表示位置 */
	COLOR		bgcolor;	/* 背景色 */
	H		bgmask;		/* 背景マスク */
	H		page[3];	/* 終了時の表示ページ
					**   カレンダーは、年月。
					**   予定表は、年月日。
					**   住所録は、インデックスとページ。
					**   検索パネルは未使用。
					*/
	H		reserved[6];
}	FSN_WINFO;

typedef	struct {
	H		len;		/* 固有データの長さ */
	FSN_WINFO	winfo[MAX_WIN];	/* 各ウインドウごとの設定値 */
	H		win_sw;		/* ウインドウ表示スイッチ */
	H		win_top;	/* 最前面ウインドウの番号 */
	H		reserved[6];
}	SCHED_FUSEN;

typedef	struct	{			/* ファイル上の構造 */
	H	len;
	H	data[(sizeof(FSN_WINFO)/sizeof(H))*MAX_WIN+8];
}	xSCHED_FUSEN;

IMPORT	SCHED_FUSEN	sched_fusen;	/* 付箋固有データ */

/*
 * メニュー管理
 */
typedef	struct {
	W		reserved;	/* ?????????????????????????? */
}	MN_STATUS;

IMPORT	MN_STATUS	mn_sts;		/* メインメニュー管理情報 */

/*
 * 対象実身(ファイル)
 */
IMPORT	RFILE		*tg_fp;		/* 対象ファイル */
IMPORT	F_STATE		tg_sts;		/* 対象ファイル情報 */
IMPORT	W		tg_update;	/* 実身変更フラグ */
			/* =0 : 実身は更新されていない
			 * >0 : 実身は更新されているが、実行メニューは更新不要
			 * <0 : 実身は更新されており、実行メニューの更新も必要
			 */

/* 変更フラグ(編集が行われた場合に、TRUE にする) */
IMPORT	BOOL		modified;

/* テキストボックスの長さ */
#define	MAX_TBLEN	20			/* 最大表示文字数 */
#define	TBUFLEN		(MAX_TBLEN+1)		/* 最大バッファ長 */

/*
 * 予定表データ構造
 */
#define	MAX_SCH		6	/* １ノード内の最大データ数 */
#define	SCH_LEN		20	/* 予定データの最大長 */

typedef	struct sch_node {
	struct sch_node	*next;		/* 次のノードへのポインタ */
	struct sch_node	*prev;		/* 前のノードへのポインタ */
	H		year;		/* 予定の年 */
	H		month;		/*	 月 */
	H		day;		/*	 日 */
	H		reserved;
	TC		*data[MAX_SCH];	/* 予定データ(文字列へのポインタ) */
} SCH_NODE;

IMPORT	SCH_NODE	*sch_data;	/* 最初の予定表データへのポインタ */
IMPORT	SCH_NODE	*cur_sch;	/* 現在ノード(ページ)へのポインタ */

/*
 * 住所録データ構造
 */
#define	MAX_ADR		6	/* １ノード内の最大データ数 */

#define	YOMI_LEN	8	/* 「よみ」の最大長 */
#define	YUBIN_LEN	8	/* 「〒」の最大長 */
#define	ADDR1_LEN	20	/* 「住所１」の最大長 */
#define	ADDR2_LEN	20	/* 「住所２」の最大長 */
#define	ADDR3_LEN	20	/* 「住所３」の最大長 */
#define	NAME_LEN	20	/* 「氏名」の最大長 */
#define	TEL_LEN		20	/* 「電話」の最大長 */
#define	FAX_LEN		20	/* 「FAX」の最大長 */
#define	MEMO1_LEN	20	/* 「メモ１」の最大長 */
#define	MEMO2_LEN	12	/* 「メモ２」の最大長 */

typedef	struct {
	struct adr_node	*node;	/* 接続されているノードへのポインタ */
	TC		yomi [YOMI_LEN+1];	/* よみ */
	TC		yubin[YUBIN_LEN+1];	/* 〒郵便番号 */
	TC		addr1[ADDR1_LEN+1];	/* 住所１ */
	TC		addr2[ADDR2_LEN+1];	/* 住所２ */
	TC		addr3[ADDR3_LEN+1];	/* 住所３ */
	TC		name [NAME_LEN+1];	/* 氏名 */
	TC		tel  [TEL_LEN+1];	/* 電話 */
	TC		fax  [FAX_LEN+1];	/* FAX */
	TC		memo1[MEMO1_LEN+1];	/* メモ１ */
	TC		memo2[MEMO2_LEN+1];	/* メモ２ */
} ADR_DATA;

typedef	struct adr_node {
	struct adr_node	*next;		/* 次のノードへのポインタ */
	struct adr_node	*prev;		/* 前のノードへのポインタ */
	TC		index;		/* 見出し文字(「あ」「い」「う」...) */
	H		reserved;
	ADR_DATA	*data[MAX_ADR];	/* 住所データへのポインタ */
} ADR_NODE;

IMPORT	ADR_NODE	*adr_data;	/* 最初の住所録データへのポインタ */
IMPORT	ADR_NODE	*cur_adr;	/* 現在ノードへのポインタ */
IMPORT	W		cur_adr_ofs;	/* 現在ページのノード内オフセット */

/*
 * 休日データの構造
 */
#define	MAX_HOLIDAY	128	/* 休日データレコードの最大数 */

#define	M_HOLIDAY	0x8000	/* 休日指定フラグ */

typedef	struct {
	H		syear;		/* 休みの年   (負数は毎年を示す)   */
	H		eyear;		/* 休みの年   (負数は毎年を示す)   */
	H		month;		/*	 月   (負数は毎月を示す)   */
	H		day;		/*	 日   (負数は毎日を示す)   */
	H		week;		/*	 曜日 (曜日を示すマスク値) */
} CAL_HOLIDAY;

IMPORT	CAL_HOLIDAY	*holiday;	/* 休日データ配列 */
IMPORT	W		nholiday;	/* 有効データ数 */

/*
 * 日付関連
 */
typedef	struct {
	H		year;		/* 年 */
	H		month;		/* 月 */
	H		day;		/* 日 */
} DATE;

#define	DATE_LEN	10		/* 日付文字列の文字数 */
#define	DATE_SEP	0x213f		/* 日付の区切り記号 "／" */

IMPORT	DATE	todaydate;	/* 今日の日付。カレンダーで枠囲み中 */
IMPORT	DATE	dispdate;	/* カレンダーに表示中(dayは未使用) */
IMPORT	DATE	selectdate;	/* 予定表に表示中 */

#define	IS_SAME_MONTH(a,b)	((a).year==(b).year && (a).month==(b).month)
#define	IS_SAME_DATE(a,b)	(IS_SAME_MONTH((a),(b)) && (a).day==(b).day)

#define	FIRST_YEAR	1990
#define	LAST_YEAR	2050

#define	IS_FIRST_MONTH(a)	((a).year==FIRST_YEAR && (a).month==1)
#define	IS_FIRST_DATE(a)	(IS_FIRST_MONTH(a) && (a).day==1)
#define	IS_LAST_MONTH(a)	((a).year==LAST_YEAR && (a).month==12)
#define	IS_LAST_DATE(a)		(IS_LAST_MONTH(a) && (a).day==31)

/*
 * TAD 関連
 */
typedef	struct {
	B		attr;		/* 属性 */
	B		subid;		/* サブID */
	UW		data[1];	/* データ本体 */
} TX_SEG;

typedef	struct {
	TC		id;		/* セグメントID */
	UH		len;		/* データ長 */
	TX_SEG		tx;		/* 文章付箋セグメント */
} SEG;

typedef	struct {
	TC		id;		/* セグメントID */
	UH		flg;		/* 0xffff */
	UW		len;		/* データ長 */
	TX_SEG		tx;		/* 文章付箋セグメント */
} LSEG;

typedef	struct {	/* 管理情報セグメント (id = TS_INFO) (len = 〜) */
	UH		subid;		/* 項目ＩＤ */
	UH		sublen;		/* 項目のバイト数 */
	UH		data[1];	/* 項目データ本体 */
					/* ＜上記の繰り返し＞ */
} SEG_INFO;

typedef	struct {	/* 文章開始セグメント (id = TS_TEXT) (len = 24) */
	RECT		view;		/* 表示領域 */
	RECT		draw;		/* 描画領域 */
	H		h_unit;		/* 水平ユニット */
	H		v_unit;		/* 垂直ユニット */
	UH		lang;		/* デフォールト言語 */
	UH		bgpat;		/* 背景パターンＩＤ */
} SEG_TEXT;
#define	SEG_TEXT_STRUCT	"hhhhhhhhhhhh"

typedef	struct {	/* タブ書式セグメント (id = TS_TRULER) (len = 〜) */
	B		attr;		/* 属性 */
	B		subid;		/* 2 */
	UH		height;		/* 書式高さ指定 */
	UH		pargap;		/* 段落間隔指定 */
	H		left;		/* 行頭マージン */
	H		right;		/* 行末マージン */
	H		indent;		/* インデントマージン */
	H		ntabs;		/* タブストップ設定数 */
	UH		tabs[1];	/* タブストップ位置(ntabs個) */
} SEG_TAB;

#define	isTC(p)		(ConvEndianH(*((TC*)(p))) < 0xff80)
#define	isLSEG(p)	(ConvEndianH(((LSEG*)(p))->flg) == 0xffff)
#define	isTABSEG(p)	(ConvEndianH((p)->id) == (TS_TRULER|0xff00) && (p)->tx.subid == 2)

#define	TAD_VERSION		0x0121	/* ＴＡＤ規格のバージョン番号(V1.21) */
#define	TAD_LANG_JAPANESE	0x0021	/* ＴＡＤ言語指定(日本語) */
#define	DISPLAY_RESOLUTION	(-120)	/* 画面解像度(120 dot/inch) */

/* タブ書式セグメントのデフォルト値 */
#define	DEF_PGAP	0x0304	/* 段落間隔 (相対の 3/4) */
#define	DEF_LM		0	/* 行頭マージン */
#define	DEF_RM		0	/* 行末マージン */
#define	DEF_IM		(18*1)	/* インデントマージン (標準１文字＋文字間隔) */

/*
 * その他
 */
#define	BIT_MASK(n)	(1<<(n))
#define	BIT_MASKL(n)	(1L<<(n))
#define	DISP_MAIN(s)	((s) & 7)

#define	lsizeof(n)	((W)sizeof(n))

/*
 *	Globals
 */
IMPORT	RLIST		view_rl[MAX_RLST];

IMPORT	BOOL		nosupport;
IMPORT	BOOL		illegal;

IMPORT	B		cpyr[];
IMPORT	WINFOREC	winfo[MAX_WIN+1];
IMPORT	WIN_INFO	win[MAX_WIN];
IMPORT	RFILE		*tg_fg;
IMPORT	F_STATE		tg_sts;
IMPORT	W		tg_update;
IMPORT	SCHED_FUSEN	sched_fusen;
IMPORT	MN_STATUS	mn_sts;
IMPORT	SCH_NODE	*sch_date;
IMPORT	SCH_NODE	*cur_sch;
IMPORT	ADR_NODE	*adr_date;
IMPORT	ADR_NODE	*cur_adr;
IMPORT	W		cur_adr_ofs;
IMPORT	CAL_HOLIDAY	*holiday;
IMPORT	W		nholiday;
IMPORT	DATE		todaydate;
IMPORT	DATE		dispdate;
IMPORT	DATE		selectdate;

IMPORT	FUNCP		cmd_page[MAX_WIN];

IMPORT	W		disp_vobj;

IMPORT	M_EXECREQ	*mycmd;
IMPORT	PAT		WINDBG[MAX_WIN];
IMPORT	FSSPEC		c_font[MAX_WIN];
IMPORT	CGAP		c_gap[MAX_WIN];
IMPORT	RECT		visrect[MAX_WIN];
IMPORT	W		adr_str_num;
IMPORT	STR_INFO	adr_str_info[MAX_STR];
IMPORT	RECT		adr_index_rect;
IMPORT	PNT		adr_page_disp;
IMPORT	RECT		cal_year;
IMPORT	PNT		cal_lefttop;
IMPORT	PNT		cal_daysize;
IMPORT	W		src_str_num;
IMPORT	STR_INFO	src_str_info[MAX_STR];
IMPORT	TC		*window_name[MAX_WIN];
IMPORT	TC		f_name[20+1];
IMPORT	W		root_wid;
IMPORT	FUNCP		main_open[MAX_MW];

IMPORT	W		in_act_parts;
IMPORT	W		*wind_def[MAX_WIN];
IMPORT	PARTS_INFO	wpinfo[MAX_WIN];
IMPORT	TC		*tbufp[MAX_WIN][10];
IMPORT	TC		srcbuf[TBUFLEN];
IMPORT	W		maxtxsz[MAX_WIN][10];

IMPORT	ADR_DATA*	alloc_adr_data(void);
IMPORT	W		insert_adr(ADR_DATA *data);
IMPORT	W		midashi_idx(TC yo);
IMPORT	TC		midashi_char(TC yo);
IMPORT	BOOL		calc_adr_page(void);
IMPORT	BOOL		set_adr_page(W idx, W mode);
IMPORT	BOOL		next_adr_page(void);
IMPORT	BOOL		prev_adr_page(void);
IMPORT	BOOL		first_adr_page(void);
IMPORT	BOOL		last_adr_page(void);
IMPORT	W		set_new_adr(void);
IMPORT	W		delete_adr_data(void);
IMPORT	W		chg_adr_yomi(void);
IMPORT	VOID		get_adr_page_str(TC *str);
IMPORT	W		set_cur_adr(void);
IMPORT	BOOL		get_cur_adr_page(W *idx, W *no);
IMPORT	BOOL		set_cur_adr_page(W idx, W no);

IMPORT	BOOL		is_holiday(DATE *date);
IMPORT	UW		*make_calendar(DATE *date);
IMPORT	BOOL		set_holiday(CAL_HOLIDAY *hp, TC *str);
IMPORT	VOID		cal_to_str(TC *str, CAL_HOLIDAY *hp);

IMPORT	VOID		view_2rect(W mode, DATE date, PNT sp, PNT size);
IMPORT	VOID		view_etc(W ix, W mode);
IMPORT	VOID		view_window(W ix, RECT *rp, W mode);
IMPORT	VOID		sc_dspfn(W ix, W mode, RECT *new);
IMPORT	VOID		redisp_adr(W mode);

IMPORT	VOID		sc_idlefn(void);
IMPORT	W		sc_msgfn(MESSAGE *msg);
IMPORT	VOID		sc_stsfn(W ix, W sts);
IMPORT	W		sc_keyfn(void);
IMPORT	W		redisp_date(DATE prevdata, DATE date);
IMPORT	W		change_date(DATE date);
IMPORT	W		sc_presfn(W ix);
IMPORT	W		sc_finfn(W ix, W mode);
IMPORT	W		sc_pastefn(W ix, PNT pos);
IMPORT	VOID		sc_vobjfn(W ix);

IMPORT	UW		month_day(DATE *date);
IMPORT	UW		date_to_num(DATE *date);
IMPORT	VOID		num_to_date(DATE *date, UW num);
IMPORT	W		num_to_week(UW days);
IMPORT	W		date_to_week(DATE *date);
IMPORT	W		get_week1st(DATE date);
IMPORT	TC		*stoa(TC *str, W num);
IMPORT	TC		*stoal(TC *str, W num, W len);
IMPORT	W		str_to_weeks(TC *str);
IMPORT	TC		*weeks_to_str(TC *str, W w);
IMPORT	BOOL		to_date(DATE *date, TC *str);
IMPORT	BOOL		to_holiday_date( CAL_HOLIDAY *hp, TC *str);
IMPORT	TC		*to_str(TC *str, DATE *date);
IMPORT	TC*		to_holiday_str( TC *str, CAL_HOLIDAY *hp);
IMPORT	TC		*date_to_jstr(TC *str, DATE *date);
IMPORT	TC		*date_to_kstr(TC *str, DATE *date);
IMPORT	BOOL		chk_date(DATE *date);
IMPORT	BOOL		str_to_date(DATE *date, TC *str);
IMPORT	BOOL		date_to_str(TC *str, DATE *date);
IMPORT	TC		tohira(TC ch);
IMPORT	BOOL		istabseg(B *seg);

IMPORT	W		sc_load(void);

IMPORT	VOID		MAIN(MESSAGE *exec_msg);

IMPORT	W		main_win_open(W ix);
IMPORT	VOID		setup_bgcol(W ix);
IMPORT	W		sc_menufn(void);
IMPORT	VOID		init_menu(void);

IMPORT	W		save_file(W mode);
IMPORT	BOOL		sc_exit(W mode);

IMPORT	VOID		sc_dsproc(W wid, W gid, W vid, COLOR bgcol);
IMPORT	W		sc_dsmain(M_DISPREQ *disp_msg);

IMPORT	VOID		swab_fusen(SCHED_FUSEN *f, xSCHED_FUSEN *x, W tomem);
IMPORT	W		open_file(LINK *lnk, F_STATE *state);
IMPORT	W		init_ch_env(W gid, W ix);
IMPORT	VOID		set_wind_def(W ix);
IMPORT	W		cal_open(RECT *vr, W disp);
IMPORT	W		sch_open(RECT *vr, W disp);
IMPORT	W		adr_open(RECT *vr, W disp);
IMPORT	W		src_open(void);
IMPORT	VOID		set_page_to_fusen(void);
IMPORT	VOID		set_window_rect(W ix);
IMPORT	VOID		close_win(W ix, W all);
IMPORT	W		sc_exec(M_EXECREQ *exec_msg);

IMPORT	W		insert_sch(DATE *date, TC *str);
IMPORT	W		set_cur_sch(void);
IMPORT	BOOL		clear_sch(void);

IMPORT	SCH_NODE	*alloc_sch_node(void);
IMPORT	VOID		free_sch_node(SCH_NODE *np);
IMPORT	SCH_NODE	*insert_sch_node(SCH_NODE *np, W mode);
IMPORT	SCH_NODE	*delete_sch_node(SCH_NODE *np);
IMPORT	W		search_sch_node(DATE *date, SCH_NODE **npp);
IMPORT	W		get_sch_node(DATE *date, SCH_NODE **npp);
IMPORT	BOOL		is_empty_sch_node(SCH_NODE *np);
IMPORT	VOID		get_today_date(DATE *date);

IMPORT	W		set_curdata(W ix);
IMPORT	W		tx_cut_txt(W pid, W sz, TC *buf, W cut);
IMPORT	VOID		inact_tbox(W pid);
IMPORT	VOID		chk_page_change(W ix, BOOL disable[]);
IMPORT	VOID		win_dispsw(W ix);
IMPORT	VOID		pnl_dispsw(W ix, W mode);
IMPORT	VOID		txt_recover(W pid, TC *buff, TC *save);
IMPORT	W		no_tboxpid(W ix, W pid);
IMPORT	W		pres_parts(W ix, W pid);
IMPORT	VOID		delete_parts(W ix);
IMPORT	VOID		set_textbox(W ix, W disp);
IMPORT	VOID		set_parts_data(W ix);
IMPORT	W		create_parts(W ix, W disp);

IMPORT	W		sc_print(W para);

IMPORT	W		put_tray(W to, TC *buf, W len, RECT *rp);
IMPORT	W		text_to_tray(W ix, W cut);
IMPORT	W		init_poptray(void);
IMPORT	VOID		fin_poptray(void);
IMPORT	W		pop_tray(W from, TC **buf, UW *len, W *eof);
IMPORT	W		text_from_tray(W ix, W from, PNT pnt);

IMPORT	BOOL		get_field_names(void);
IMPORT	W		sc_search(W ix, TC *str, W mode);

IMPORT	W		gdra_chp(GID gid, W x, W y, TC ch, DCM mode);
IMPORT	W		gdra_stp(GID gid, W x, W y, TC *str, W len, DCM mode);
