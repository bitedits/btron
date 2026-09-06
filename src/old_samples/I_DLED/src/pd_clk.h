//
//	pd_dclk.h (偽仮身一覧/ポインタクリック処理ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLED_PD_CLK_H
#define	_I_DLED_PD_CLK_H

#include	<basic.h>
#include	<btron/btron.h>

#include	"pd_ope.h"


// --------------------------------------------------------------- class PD_CLK
class	PD_CLK : public PD_OPE {
public:
	PD_CLK();			// constructor
	~PD_CLK();			// destructor

	// 実行処理部
	void	main();

private:
	W	vid;
	VOBJ*	vobj;
};

#endif	// _I_DLED_PD_CLK_H
