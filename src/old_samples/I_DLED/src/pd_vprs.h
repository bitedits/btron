//
//	pd_vprs.h (偽仮身一覧/ポインタプレス処理ヘッダ-仮身をつかむ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLED_PD_VPRS_H
#define	_I_DLED_PD_VPRS_H

#include	<basic.h>
#include	<btron/btron.h>

#include	"pd_ope.h"


// -------------------------------------------------------------- class PD_VPRS
class	PD_VPRS : public PD_OPE {
public:
	PD_VPRS(W vid);			// constructor
	~PD_VPRS();			// destructor

	// 実行処理部
	void	main();

private:
	SIZE	gap;			// 枠の左上原点からの間隙
	RECT	vr;			// 元々の選択枠群の最外接長方形
	RECT	frect;			// 全画面領域
};

#endif	// _I_DLED_PD_VPRS_H
