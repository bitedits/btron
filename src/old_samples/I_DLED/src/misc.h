//
//	misc.h (偽仮身一覧/雑多関数群ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLED_MISC_H
#define	_I_DLED_MISC_H

#include	<basic.h>
#include	<btron/btron.h>


// ----------------------------------------------------------------- class MISC
class	MISC {
public:
	// 補正をかけた wget_drg()
	static	WERR	wget_drg_C(PNT* p, WEVENT* evt);

	// 制限範囲内に矩形領域を収める
	static	void	cliprect(RECT& r, const RECT limit);

private:
};

#endif	// _I_DLED_MISC_H
