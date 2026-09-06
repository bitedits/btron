//
//	cval.h (簡易文字列編集/定数)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_T_TXEDIT_CVAL_H
#define	_T_TXEDIT_CVAL_H

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/dp.h>
#include	<btron/libapp.h>


// ------------------------------------------------------------- namespace CVAL
namespace	CVAL {
	// application ID
	static	const	UH	APPID_0 = 0x8000;
	static	const	UH	APPID_1 = 0xcf03;
	static	const	UH	APPID_2 = 0xbfff;

	// application ID(DLED)
	static	const	UH	DLED_APPID_0 = 0x8000;
	static	const	UH	DLED_APPID_1 = 0x0000;
	static	const	UH	DLED_APPID_2 = 0x8000;

	// ピクトグラム ID
	static	const	W	T_TXEDIT_PICT = 1;

	// 色指定
	static	const	COLOR	COL_TRANS = 0x90000000;
	static	const	COLOR	COL_GRAY  = 0x80000000;
	static	const	COLOR	RGB_BLACK = 0x10000000;
	static	const	COLOR	RGB_WHITE = 0x10ffffff;

	// ウィンドウ管理用
	static	const	W	WIDX_MAINWIN = 0;
	static	const	W	WIN_NUM = 1;

	// レイアウトの制限
	static	const	RECT	LIMIT_RECT = (RECT){{0, 0, 32000, 32000}};

	// 間隙
	static	const	UW	TEXT_HGAP = 4;
	static	const	UW	TEXT_VGAP = 4;
	static	const	UW	CH_HGAP = 2;
	static	const	UW	CH_VGAP = 2;

	// テキスト入力ポートの動作モード
	static	const	W	TIP_MODE = (TIP_CNVMD | TIP_MANUAL | TIP_TORIGHT | TIP_TCONLY | TXT_OVER);
};

#endif	// _T_TXEDIT_CVAL_H
