//
//	struct.h (簡易文字列編集/広域構造体)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//
 
#ifndef	_T_TXEDIT_STRUCT_H
#define	_T_TXEDIT_STRUCT_H

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/dp.h>


// ------------------------------------------------------------- ウィンドウ定義
typedef	struct {			// ウィンドウ定義
	UW	attr;			// ウィンドウ属性
	RECT	r;			// ウィンドウ枠(外枠)
	W	parts;			// parts table offset(DATABOX 内)
	W	pnum;			// パーツ個数
	TC	title[20];		// タイトル文字列定義
} WINDEF;
#define	WINDEF_STRUCT	"w4hww20h"
#define	PNL_ITEM_STRUCT	"ww4hwww"	// PNL_ITEM の構造


// --------------------------------------------------------- ウィンドウ表示属性
typedef	struct {			// ウィンドウ表示属性定義
	RECT	rect;			// ウィンドウ位置(矩形領域)
	bool	fwindow:1;		// 全画面表示かどうか
	bool	gray:1;			// 文字階調表示かどうか
	UW	rsv:30;			// 拡張余白
} WINATTR;
#define	WINATTR_STRUCT	"4hw"


// ----------------------------------------------------------- ウィンドウ背景色
typedef	struct {			// ウィンドウ背景色定義
	W	mask;			// 背景色マスク
	COLOR	col;			// ウィンドウ背景色
} WINCOL;
#define	WINCOL_STRUCT	"ww"

 
// --------------------------------------------------------- 付箋固有データ定義
typedef	struct {			// 付箋定義
	UH	dlen;			// データ長さ(byte 単位)
	UH	ver;			// version(0xXYYY = X.YYY)
	PNT	vp;			// 表示原点(左上)
	WINATTR	wattr;			// ウィンドウ表示属性
	WINCOL	wcol;			// ウィンドウ背景色
} T_TXEDIT_FUSEN;
#define	T_TXEDIT_FUSEN_STRUCT	"hh2h" WINATTR_STRUCT WINCOL_STRUCT
#define	T_TXEDIT_FUSEN_DLEN	(sizeof(T_TXEDIT_FUSEN) - sizeof(UH))
#define	T_TXEDIT_FUSEN_VERSION	0x1000

#endif	// _T_TXEDIT_STRUCT_H
