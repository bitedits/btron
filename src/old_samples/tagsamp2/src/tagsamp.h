/*=============================================================================

	tagsamp.h : 見出しパネルサンプル 共通ヘッダ

	(C) Copyright 1999 Personal Media Corporation

=============================================================================*/
#include        <basic.h>
#include        <bstdio.h>
#include        <bstdlib.h>
#include        <errcode.h>
#include        <tstring.h>
#include        <bstring.h>
#include        <tlang.h>
#include        <tcode.h>
#include        <btron/btron.h>
#include	<btron/dp.h>
#include	<btron/hmi.h>
#include        <btron/libapp.h>

/*=================================================================定数の宣言*/
#define	TS_PICT_ID	2

#define	RGB_WHITE	0x10ffffff
#define	RGB_BLACK	0x10000000
#define	COL_TRANS	0xffffffff

#define	TITLEGAP	8		/* 見出しパネルの見出しギャップ	*/
#define	FRAMEGAP	4		/* 見出しパネルの領域のギャップ	*/
#define	MAX_SUBPNL	4		/* 見出しパネルのページ数	*/
#define	MAX_PARTS	6		/* 見出しパネル内の最大パーツ数	*/
					/* （文字などもすべて含めた数）	*/

/*===============================================見出しパネルのイベント処理用*/
	/* 見出しパネルのイベント処理については、必要に応じてその種類の追加・
	   削除を行えばよく、必ずしもこのサンプルの例を使う必要もない	*/
			/* 雑多イベントの処理のルーチンで使うもの	*/
#define	K_CHECK		0		/* ページチェック処理		*/
#define	K_INIT		1		/* ページ初期化処理		*/
#define	K_DISP		2		/* ページ表示処理		*/
#define	K_FINISH	3		/* ページ終了処理		*/
#define	K_SETUP		(-1)		/* その他の起動時設定処理	*/
					/*      (cli起動の時などに使う)	*/

			/* 再表示処理などルーチンで使うもの		*/
#define	K_IDLE		0		/* アイドル処理			*/
#define	K_MSG		(-1)		/* メッセージ受信処理		*/

			/* パーツ類の実行動作のルーチンで使うもの	*/
#define	K_PARTS(n)	(n)		/* パーツ動作			*/
#define	K_PASTEREQ	(1000)		/* 貼り込み要求動作		*/
#define	K_PASTE		(1001)		/* 貼り込み動作			*/
#define	K_WSWITCH	(1002)		/* ウィンドウ切り替え動作	*/
#define	K_KEY		(-1)		/* キー入力動作			*/
#define	K_MENU(n)	(100 + (n))	/* メニュー選択動作		*/

typedef	struct {			/* 各パネル毎の表示内容の定義	*/
	UH	*parts;			/* パーツ定義へのポインタ	*/
	W	pid[MAX_PARTS];		/* パーツIDの配列		*/
	UH	*items;			/* 表示項目へのポインタ		*/
} SUBPNL_DEF;

typedef	struct {			/* 各パネル毎の処理関数の定義	*/
	FUNCP	init;			/* 雑多のイベント処理用		*/
	FUNCP	disp;			/* 再表示処理用			*/
	FUNCP	act;			/* パーツ類の実行動作用		*/
} SUBPNL_FN;

/*=======================================================データボックスの番号*/
#define	DB_SPNL		0x8000		/* 単純パネルデータ番号		*/
#define	DB_PSTR		0x9000		/* パネル文字バッファデータ番号	*/
#define	DB_MENU		0xa000		/* メニューデータ番号		*/
#define	DB_SMSG		0xb000		/* システムメッセージデータ番号	*/
#define	DB_MISC		0xf000		/* その他のデータ番号		*/

#define	EPNL_WIND	(DB_SPNL + 0)	/* パネル			*/
#define	EPNL_MENU	(DB_SPNL + 1)
#define	EPNL_MEMORY	(DB_SPNL + 2)
#define	EPNL_SYSTEM	(DB_SPNL + 3)
#define	EPNL_OSTA	(DB_SPNL + 4)
#define	PNL_TEST	(DB_SPNL + 5)

#define	TS_MENU		(DB_MENU + 0)	/* メニュー			*/

#define	STR_BUF1	(DB_PSTR + 0)	/* 文字バッファ			*/

#define	TS_TITLE	(DB_MISC + 0)	/* タイトル文字列		*/
#define	TS_WINDSZ	(DB_MISC + 1)	/* ウィンドウサイズ		*/

#define	TS_DEF		(DB_MISC + 10)

#define	WIN_DEF		(DB_MISC + 50)

/*=============================================================共通変数の宣言*/
IMPORT	W	mywid;			/* ウィンドウID			*/
IMPORT	W	mygid;			/* 描画環境ID			*/
IMPORT	W	myvid;			/* 仮身ID			*/
IMPORT	RECT	vrect;			/* ウィンドウ表示領域		*/

IMPORT	PAT	*wbgpat;		/* ウィンドウ背景パターン	*/
IMPORT	PAT	*pbgpat;		/* パネル背景パターン		*/

IMPORT	W	subnum;			/* 見出しパネルの表示番号	*/
IMPORT	VP	tagpnl;			/* 見出しパネル操作用		*/
IMPORT	W	*subpid;		/* 見出しパネルのPID配列	*/
IMPORT	W	act_box;		/* アクティブなボックスパーツ	*/
IMPORT	SUBPNL_DEF subpnl[];		/* 見出しパネル内の定義		*/
IMPORT	SUBPNL_FN  subfn[];		/* 見出しパネルの処理関数	*/
