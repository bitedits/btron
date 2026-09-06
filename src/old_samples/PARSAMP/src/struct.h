//
//	struct.h (パーツ操作例/広域構造体)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//
 
#ifndef	_PARSAMP_STRUCT_H
#define	_PARSAMP_STRUCT_H

#include	<basic.h>
#include	<btron/btron.h>

#include	"cval.h"


// ------------------------------------------------------------- ウィンドウ定義
typedef	struct {			// ウィンドウ定義
	UW	attr;			// ウィンドウ属性
	RECT	r;			// ウィンドウ枠(外枠)
	W	parts;			// parts table offset(DATABOX 内)
	W	pnum;			// 項目個数
	TC	title[20];		// タイトル文字列定義
} WINDEF;
#define	WINDEF_STRUCT	"w4hww20h"
#define	PNL_ITEM_STRUCT	"ww4hwww"	// PNL_ITEM の構造


// ------------------------------------------------------------- 各種の設定内容
typedef	struct {			// 設定の保存内容
	TC	str1[CVAL::STR_LEN];	// 文字列1
	TC	str2[CVAL::STR_LEN];	// 文字列2
	UW	col;			// 色番号
	UW	size;			// サイズ
	bool	enbl:1;			// 有効/不能
	bool	pitch:1;		// false : 固定    true : 比例
	UW	rsv:30;			// 拡張余白
} PARDATA;
#define	PARDATA_STRUCT	"10h10hwww"

 
// --------------------------------------------------------- 付箋固有データ定義
typedef	struct {			// 付箋定義
	UH	dlen;			// データ長さ(byte 単位)
	UH	ver;			// version(0xXYYY = X.YYY)
	PNT	p;			// 主ウィンドウ位置
	PARDATA	pdat;			// 設定内容
} PARSAMP_FUSEN;
#define	PARSAMP_FUSEN_STRUCT	"hh2h" PARDATA_STRUCT
#define	PARSAMP_FUSEN_DLEN	(sizeof(PARSAMP_FUSEN) - sizeof(UH))
#define	PARSAMP_FUSEN_VERSION	0x1000

#endif	// _PARSAMP_STRUCT_H
