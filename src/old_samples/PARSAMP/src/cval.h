//
//	cval.h (パーツ操作例/定数)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_PARSAMP_CVAL_H
#define	_PARSAMP_CVAL_H

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/dp.h>


// ------------------------------------------------------------- namespace CVAL
namespace	CVAL {
	// application ID
	static	const	UH	APPID_0 = 0x8000;
	static	const	UH	APPID_1 = 0xc000;
	static	const	UH	APPID_2 = 0xbffe;

	// ピクトグラム ID
	static	const	W	PARSAMP_PICT = 2;

	// 色指定
	static	const	COLOR	COL_TRANS = 0x90000000;
	static	const	COLOR	RGB_BLACK = 0x10000000;
	static	const	COLOR	RGB_WHITE = 0x10ffffff;

	// ウィンドウ管理用
	static	const	W	WIDX_MAINWIN = 0;
	static	const	W	WIN_NUM = 1;

	// 文字列長さ
	static	const	W	STR_LEN = 10;
};

#endif	// _PARSAMP_CVAL_H
