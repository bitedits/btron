//
//	struct.h (画像閲覧/広域構造体)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//
 
#ifndef	_IMGVIEW_STRUCT_H
#define	_IMGVIEW_STRUCT_H

#include	<basic.h>
#include	<btron/btron.h>


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

 
// --------------------------------------------------------- 付箋固有データ定義
typedef	struct {			// 付箋定義
	UH	dlen;			// データ長さ(byte 単位)
	UH	ver;			// version(0xXYYY = X.YYY)
	WINATTR	wattr;			// ウィンドウ表示属性
	PNT	vp;			// 表示起点(左上/実寸)
	UW	zfact;			// 表示倍率(1000 = 100.0[%])
} IMGVIEW_FUSEN;
#define	IMGVIEW_FUSEN_STRUCT	"hh" WINATTR_STRUCT "2hw"
#define	IMGVIEW_FUSEN_DLEN	(sizeof(IMGVIEW_FUSEN) - sizeof(UH))
#define	IMGVIEW_FUSEN_VERSION	0x1000

#endif	// _IMGVIEW_STRUCT_H
