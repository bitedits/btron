//
//	pd_dclk.h (偽仮身一覧/ポインタダブルクリック処理ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLED_PD_DCLK_H
#define	_I_DLED_PD_DCLK_H

#include	<basic.h>
#include	<btron/btron.h>

#include	"pd_ope.h"


// -------------------------------------------------------------- class PD_DCLK
class	PD_DCLK : public PD_OPE {
public:
	PD_DCLK();			// constructor
	~PD_DCLK();			// destructor

	// 実行処理部
	void	main();

private:
	W	vid;			// 操作対象仮身 ID
	VOBJ*	vobj;			// 操作対象仮身情報
};

#endif	// _I_DLED_PD_DCLK_H
