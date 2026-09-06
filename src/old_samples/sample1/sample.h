/*
	sample.h	サンプルプログラム: ヘッダファイル

	(C) Copyright 1998 by Personal Media Corporation
*/
#include	<basic.h>
#include	<bstdlib.h>
#include	<bstdio.h>
#include	<bstring.h>
#include	<errcode.h>
#include	<tstring.h>
#include	<tcode.h>
#include	<btron/btron.h>
#include	<btron/dp.h>
#include	<btron/hmi.h>
#include	<btron/vobj.h>
#include	<btron/libapp.h>

#define	ES_MODIFY	(ES_LSHFT | ES_RSHFT)		/* 修正選択用	*/
#define ES_CMND		(ES_CMD | ES_BUT2)		/* メニュー指定	*/

/* ピクトグラム番号 */
#define		SAMPLE_PICT	2

/* カラー定義 */
#define	RGB_WHITE	0x10FFFFFF	/* 白				*/
#define	RGB_BLACK	0x10000000	/* 黒				*/
#define	COL_TRANS	0xFFFFFFFF	/* 透明 			*/

/* 機能付箋の固有データ */
typedef struct {			/* 機能付箋の固有データ部分	*/
	UH	len;			/* 固有データ長さ = 18		*/
	UH	bgmask;			/* 背景色マスク			*/
	COLOR	bgcol;			/* 背景色			*/
	RECT	vrect;			/* ウィンドウ外枠		*/
	PNT	offset;			/* 表示位置のオフセット		*/
} PARA;

/* セグメントデータタイプ定義 */
#define		MAX_PT		1000		/* 最大点数		*/
typedef	struct segment {
	struct segment *next;			/* リンク		*/
	RECT		frame;			/* 全体を囲む枠		*/
	W		lwidth;			/* 線幅			*/
	COLOR		lcolor;			/* 線色			*/
	UH		round;			/* 以下は POLY 構造体	*/
	UH		np;
	PNT		pt[1];
} SEGMENT;

/* データボックス定義 */
#define	DB_SPNL		0x8000		/* 単純パネルデータ番号		*/
#define	DB_PSTR		0x9000		/* パネル文字バッファデータ番号	*/
#define	DB_MENU		0xA000		/* メニューデータ番号		*/
#define DB_SMSG		0xB000		/* システムメッセージデータ番号	*/
#define DB_MISC		0xF000		/* その他のデータ番号		*/

/* パネル定義 */
#define	EP_OPEN		(DB_SPNL + 1)		/* オープンエラー	*/
#define	EP_WIND		(DB_SPNL + 2)		/* ウィンドウエラー	*/
#define	EP_MENU		(DB_SPNL + 3)		/* メニューエラー	*/
#define	EP_MEMORY	(DB_SPNL + 4)		/* メモリ不足エラー	*/

/* メニュー定義 */
#define	MENU		(DB_MENU + 0)		/* メニュー定義		*/

/* その他の定義 */
#define	WIND_TL		(DB_MISC + 0)		/* ウィンドウタイトル	*/
#define	CANVAS_SZ	(DB_MISC + 1)		/* キャンバスサイズ	*/
#define	DEF_PARA	(DB_MISC + 2)		/* デフォールト固有データ */
#define	PTR_SHAPE	(DB_MISC + 10)		/* ポインタ形状		*/

/* グローバルデータ */
IMPORT	RECT		frect;			/* 全体領域(キャンバス)	*/
IMPORT	RECT		vrect;			/* 表示領域		*/
IMPORT	W		myfd;			/* ファイル ID		*/
IMPORT	W		mywid;			/* ウィンドウ ID	*/
IMPORT	W		mygid;			/* 描画環境 ID		*/
IMPORT	M_EXECREQ	*mycmd;			/* 起動コマンド		*/

IMPORT	PAT		windpat;		/* ウィンドウ背景パターン*/
IMPORT	PAT		wkpat;			/* 作業用パターン	*/

IMPORT	W		l_width;		/* 線幅 		*/
IMPORT	COLOR		l_color;		/* 線色(ピクセル値)	*/

IMPORT	SEGMENT		*segroot;		/* 一番下のセグメント	*/
IMPORT	SEGMENT		*segtop;		/* 一番上のセグメント	*/

IMPORT	WINFOREC	window[];		/* ウィンドウ情報レコード */

IMPORT	PARA		para;			/* 機能付箋の固有データ */

/* グローバル関数 */
/* exec.c */
IMPORT	W	exec_main(M_EXECREQ *cmd);

/* act.c */
IMPORT	VOID	idle_fn(VOID);
IMPORT	VOID	sts_fn(W ix, W sts);
IMPORT	W	fin_fn(W ix, W mode);
IMPORT	W	key_fn(VOID);

/* disp.c */
IMPORT	VOID	view(RECT r);
IMPORT	VOID	scrl_fn(W ix, W sts, W diff);
IMPORT	VOID	disp_fn(W ix, W mode, RECT *new);

/* menu.c */
IMPORT	W	cmd_close(W mode);
IMPORT	W	cmd_redsp(W par);
IMPORT	VOID	setup_bgcol(VOID);
IMPORT	W	cmd_bgcol(W par);
IMPORT	W	cmd_edit(W par);
IMPORT	W	cmd_lwidth(W par);
IMPORT	W	cmd_lcolor(W par);
IMPORT	W	menu_fn(VOID);

/* press.c */
IMPORT	W	pres_fn(W ix);
