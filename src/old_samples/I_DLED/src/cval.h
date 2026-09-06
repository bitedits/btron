//
//	cval.h (偽仮身一覧/定数)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLED_CVAL_H
#define	_I_DLED_CVAL_H

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/dp.h>


// ------------------------------------------------------------- namespace CVAL
namespace	CVAL {
	// application ID
	static	const	UH	APPID_0 = 0x8000;
	static	const	UH	APPID_1 = 0xc001;
	static	const	UH	APPID_2 = 0xbfff;

	// application ID(DLED)
	static	const	UH	DLED_APPID_0 = 0x8000;
	static	const	UH	DLED_APPID_1 = 0x0000;
	static	const	UH	DLED_APPID_2 = 0x8000;

	// ピクトグラム ID
	static	const	W	I_DLED_PICT = 2;

	// 色指定
	static	const	COLOR	COL_TRANS = 0x90000000;
	static	const	COLOR	RGB_BLACK = 0x10000000;
	static	const	COLOR	RGB_WHITE = 0x10ffffff;

	// ウィンドウ管理用
	static	const	W	WIDX_MAINWIN = 0;
	static	const	W	WIN_NUM = 1;

	// 配置の制限
	static	const	RECT	LIMIT_RECT = (RECT){{0, 0, 32000, 32000}};

	// 間隙
	static	const	UW	VOBJ_HGAP = 4;
	static	const	UW	VOBJ_VGAP = 4;

	// 自動スクロール開始までの待ち
	static	const	W	SCRL_WAIT = 400;	// 400[ms]

	// 整頓時の間隙
	static	const	W	MT_VGAP = 4;	// 仮身間の標準垂直間隙

	// 自動配置の間隙・仮身サイズ
	static	const	W	AT_LGAP = 4;	// 左辺間隙
	static	const	W	AT_TGAP = 4;	// 上底間隙
	static	const	W	AT_HGAP = 4;	// 仮身間の水平間隙
	static	const	W	AT_VGAP = 2;	// 仮身間の垂直間隙
	static	const	W	AT_WIDTH = 16 * 10;	// 仮身幅
	static	const	UH	AT_LIMIT = 0x0103;	// 仮身最小幅(比率)

	// 固定化/背景化の値(DLED 互換)
	static	const	UH	NONE_LOCK = 0x0000;	// 設定なし
	static	const	UH	HOLD_TYPE = 0x0001;	// 固定化
	static	const	UH	BACK_TYPE = 0x0002;	// 背景化
};

#endif	// _I_DLED_CVAL_H
