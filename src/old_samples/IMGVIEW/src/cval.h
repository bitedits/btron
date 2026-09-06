//
//	cval.h (画像閲覧/定数)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_IMGVIEW_CVAL_H
#define	_IMGVIEW_CVAL_H

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/dp.h>


// ------------------------------------------------------------- namespace CVAL
namespace	CVAL {
	// application ID
	static	const	UH	APPID_0 = 0x8000;
	static	const	UH	APPID_1 = 0xc001;
	static	const	UH	APPID_2 = 0xbfff;

	// ピクトグラム ID
	static	const	W	IMGVIEW_PICT = 2;

	// 色指定
	static	const	COLOR	COL_TRANS = 0x90000000;
	static	const	COLOR	RGB_BLACK = 0x10000000;
	static	const	COLOR	RGB_WHITE = 0x10ffffff;

	// ウィンドウ管理用
	static	const	W	WIDX_MAINWIN = 0;
	static	const	W	WIN_NUM = 1;

	// 倍率
	static	const	UW	Z_TBL[] = {
					100, 200, 250, 500,
					1000,
					1500, 2000, 3000, 4000, 6000, 8000
				};
	static	const	UW	Z_TBL_MAX = 11;
	static	const	UW	Z_MIN = 100;
	static	const	UW	Z_MEDIUM = 1000;
	static	const	UW	Z_MAX = 8000;
};

#endif	// _IMGVIEW_CVAL_H
