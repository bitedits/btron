//
//	struct.h (偽仮身一覧/広域構造体)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//
 
#ifndef	_I_DLED_STRUCT_H
#define	_I_DLED_STRUCT_H

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
	UW	rsv:31;			// 拡張余白
} WINATTR;
#define	WINATTR_STRUCT	"4hw"


// ----------------------------------------------------------- ウィンドウ背景色
typedef	struct {			// ウィンドウ背景色定義
	W	mask;			// 背景色マスク
	COLOR	col;			// ウィンドウ背景色
} WINCOL;
#define	WINCOL_STRUCT	"ww"


// ------------------------------------------------------------- 整頓の設定内容
typedef	struct {			// 整頓の設定内容
	UW	htype;			// 横の選択内容
	UW	vtype;			// 縦の選択内容
	UW	ltype;			// 長さの選択内容
} TIDYDAT;
#define	TIDYDAT_STRUCT	"www"

 
// --------------------------------------------------------- 付箋固有データ定義
typedef	struct {			// 付箋定義
	UH	dlen;			// データ長さ(byte 単位)
	UH	ver;			// version(0xXYYY = X.YYY)
	PNT	vp;			// 表示原点(左上)
	RECT	area;			// 実作業範囲
	WINATTR	wattr;			// ウィンドウ表示属性
	WINCOL	wcol;			// ウィンドウ背景色
	TIDYDAT	tidy;			// 整頓の設定内容
} I_DLED_FUSEN;
#define	I_DLED_FUSEN_STRUCT	"hh2h4h" WINATTR_STRUCT WINCOL_STRUCT TIDYDAT_STRUCT
#define	I_DLED_FUSEN_DLEN	(sizeof(I_DLED_FUSEN) - sizeof(UH))
#define	I_DLED_FUSEN_VERSION	0x1000


// ---------------------------------------------------- 固定化/背景化状態の設定
typedef struct { 			// 固定化/背景化状態の設定(DLED 互換)
	UH	id_at;			// sub ID / attribute
	UH	app_id1;		// DLED の application ID
	UH	app_id2;		// DLED の application ID
	UH	app_id3;		// DLED の application ID
} LOCKSEG;

#endif	// _I_DLED_STRUCT_H
