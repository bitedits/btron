//
//	kb_scrl.h (偽仮身一覧/キーボード操作系-スクロール処理)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLED_KB_SCRL_H
#define	_I_DLED_KB_SCRL_H

#include	<basic.h>
#include	<btron/btron.h>

#include	"kb_ope.h"


// -------------------------------------------------------------- class KB_SCRL
class	KB_SCRL : public KB_OPE {
	// スクロールバーの種別の表記(libapp用)
	enum SBAR_TYPE {		// スクロールバー種別
		TYPE_RBAR = 0x0000,	// 右スクロールバー
		TYPE_BBAR = 0x0010,	// 下スクロールバー
		TYPE_LBAR = 0x0020,	// 左スクロールバー
		TYPE_MASK = 0x0030
	};
	enum SCRL_TYPE {		// スクロール方法種別
		SCRL_SMTH = 0x0000,	// smooth scroll
		SCRL_AREA = 0x0004,	// area scroll
		SCRL_JUMP = 0x0008,	// jump(drag) scroll
		SCRL_MASK = 0x000c
	};
	enum SDRCT_TYPE {		// スクロール方向種別
		DRCT_UP   = 0x0000,	// 上方向
		DRCT_DOWN = 0x0001,	// 下方向
		DRCT_LEFT = 0x0002,	// 左方向
		DRCT_RIGHT= 0x0003,	// 右方向
		DRCT_MASK = 0x0003
	};

public:
	KB_SCRL(TC code, UW stat);	// constructor
	~KB_SCRL();			// destructor

	// 実行処理
	void	main();

private:
	W	type;
	W	diff;
	RECT	varea;
	RECT	vrect;
};

#endif	// _I_DLDE_KB_SCRL_H
