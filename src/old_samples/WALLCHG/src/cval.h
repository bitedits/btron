//
//	cval.h (壁紙変更/定数)
//
//	(C) Copyright 2001 by Personal Media Corporation.
//

#ifndef	_WALLCHG_CVAL_H
#define	_WALLCHG_CVAL_H

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/dp.h>


// ------------------------------------------------------------- namespace CVAL
namespace	CVAL {
	// application ID
	static	const	UH	APPID_0 = 0x8000;
	static	const	UH	APPID_1 = 0xc000;
	static	const	UH	APPID_2 = 0xbfff;

	// 色指定
	static	const	COLOR	COL_TRANS = 0x90000000;
	static	const	COLOR	RGB_BLACK = 0x10000000;
	static	const	COLOR	RGB_WHITE = 0x10ffffff;

	// option
	static	const	UW	OPT_NONE   = 0x00000000;
	static	const	UW	OPT_PANEL  = 0x00000001;
	static	const	UW	OPT_CHANGE = 0x00000002;
};

#endif	// _WALLCHG_CVAL_H
