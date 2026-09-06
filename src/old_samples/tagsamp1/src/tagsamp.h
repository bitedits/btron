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

IMPORT	VP	tagpnl;			/* 見出しパネル操作用		*/

